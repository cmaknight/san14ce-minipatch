/* 로그·설정·패치 엔진 */
#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

Config g_cfg;
static CRITICAL_SECTION g_log_cs;
static HANDLE g_log = INVALID_HANDLE_VALUE;

void game_dir(wchar_t *out, size_t n)
{
    DWORD len = GetModuleFileNameW(NULL, out, (DWORD)n);
    while (len && out[len - 1] != L'\\')
        len--;
    out[len] = 0;
}

/* 로그: %LOCALAPPDATA%\san14patch\dll.log (게임 폴더가 Program Files라 쓰기 권한이 없을 수 있다) */
void log_open(void)
{
    wchar_t path[MAX_PATH];
    InitializeCriticalSection(&g_log_cs);
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n || n > MAX_PATH - 32)
        return;
    lstrcatW(path, L"\\san14patch");
    CreateDirectoryW(path, NULL);
    lstrcatW(path, L"\\dll.log");
    g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    /* 실행마다 시작 줄과 실패만 남지만 이어 쓰기라, 1 MB를 넘으면 비우고 새로 쓴다 */
    LARGE_INTEGER size;
    if (g_log != INVALID_HANDLE_VALUE && GetFileSizeEx(g_log, &size) && size.QuadPart > 1024 * 1024) {
        CloseHandle(g_log);
        g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

void plog(const char *fmt, ...)
{
    if (g_log == INVALID_HANDLE_VALUE)
        return;
    char buf[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int k = _snprintf_s(buf, sizeof buf, _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay,
                        t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    int m = _vsnprintf_s(buf + k, sizeof buf - k - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    k += m < 0 ? (int)strlen(buf + k) : m;
    buf[k++] = '\r';
    buf[k++] = '\n';
    EnterCriticalSection(&g_log_cs);
    DWORD w;
    WriteFile(g_log, buf, k, &w, NULL);
    LeaveCriticalSection(&g_log_cs);
}

/* 설정: 게임 폴더 san14patch.ini [groups]. 패처가 쓴다(patcher/dll.py). */
void config_load(void)
{
    wchar_t ini[MAX_PATH];
    game_dir(ini, MAX_PATH);
    lstrcatW(ini, L"san14patch.ini");
    g_cfg.trait_balance = GetPrivateProfileIntW(L"groups", L"trait_balance", 0, ini);
    g_cfg.expert_scaling = GetPrivateProfileIntW(L"groups", L"expert_scaling", 0, ini);
    g_cfg.auto_search = GetPrivateProfileIntW(L"groups", L"auto_search", 0, ini);
    g_cfg.trait_tooltip = GetPrivateProfileIntW(L"groups", L"trait_tooltip", 0, ini);
    g_cfg.fog_of_war = GetPrivateProfileIntW(L"groups", L"fog_of_war", 0, ini);
    g_cfg.log_plus = GetPrivateProfileIntW(L"groups", L"log_plus", 0, ini);
    g_cfg.post_busy = GetPrivateProfileIntW(L"groups", L"post_busy", 0, ini);
    g_cfg.train_cost = GetPrivateProfileIntW(L"groups", L"train_cost", 0, ini);
    g_cfg.even_develop = GetPrivateProfileIntW(L"groups", L"even_develop", 0, ini);
}

int bytes_equal(uint32_t rva, const void *want, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(RVA(rva), &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return memcmp(RVA(rva), want, n) == 0;
}

static int write_code(uint8_t *dst, const void *src, size_t n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return 1;
}

int patch_apply(const char *feature, Patch *p, int n)
{
    for (int i = 0; i < n; i++) {
        if (!bytes_equal(p[i].rva, p[i].orig, p[i].len)) {
            plog("[%s] 원래 바이트 불일치 @%06X. 이 기능은 적용하지 않음", feature, p[i].rva);
            return 0;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!write_code(RVA(p[i].rva), p[i].repl, p[i].len)) {
            plog("[%s] 쓰기 실패 @%06X (앞의 %d개는 이미 적용됨)", feature, p[i].rva, i);
            return 0;
        }
    }
    return 1;
}

/* 이미지 ±2 GB 안의 메모리(rip 상대 주소로 가리킬 데이터용). 실패하면 NULL */
void *near_alloc(size_t n, DWORD prot)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t lo = (uintptr_t)g_base > 0x7FF00000u ? (uintptr_t)g_base - 0x7FF00000u : 0x10000;
    for (uintptr_t a = ((uintptr_t)g_base - gran) & ~(gran - 1); a > lo; a -= gran) {
        void *p = VirtualAlloc((void *)a, n, MEM_RESERVE | MEM_COMMIT, prot);
        if (p)
            return p;
    }
    return NULL;
}

/* 중계기: 이미지 근처에 페이지를 잡고 14바이트씩 나눠 쓴다 */
static uint8_t *g_relay_page;
static size_t g_relay_used;

uint8_t *relay_new(void *target)
{
    if (!g_relay_page) {
        g_relay_page = near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
        if (!g_relay_page) {
            plog("중계기 페이지를 이미지 근처에 잡지 못함");
            return NULL;
        }
    }
    if (g_relay_used + 16 > 0x1000)
        return NULL;
    uint8_t *r = g_relay_page + g_relay_used;
    g_relay_used += 16;
    r[0] = 0xFF;
    r[1] = 0x25;
    memset(r + 2, 0, 4);
    memcpy(r + 6, &target, 8);
    FlushInstructionCache(GetCurrentProcess(), r, 14);
    return r;
}

int patch_call(Patch *p, uint32_t site_rva, uint32_t expect_callee_rva, void *target)
{
    uint8_t *relay = relay_new(target);
    if (!relay)
        return 0;
    int32_t old_rel = (int32_t)(expect_callee_rva - (site_rva + 5));
    int64_t new_rel = (int64_t)(relay - RVA(site_rva + 5));
    if (new_rel != (int32_t)new_rel)
        return 0;
    p->rva = site_rva;
    p->len = 5;
    p->orig[0] = 0xE8;
    memcpy(p->orig + 1, &old_rel, 4);
    p->repl[0] = 0xE8;
    int32_t r32 = (int32_t)new_rel;
    memcpy(p->repl + 1, &r32, 4);
    return 1;
}

static int slot_cas(uint32_t slot_rva, uint64_t want, uint64_t v)
{
    DWORD old;
    if (!VirtualProtect(RVA(slot_rva), 8, PAGE_READWRITE, &old))
        return 0;
    LONG64 prev = InterlockedCompareExchange64((LONG64 *)RVA(slot_rva), (LONG64)v, (LONG64)want);
    VirtualProtect(RVA(slot_rva), 8, old, &old);
    return prev == (LONG64)want;
}

int vtable_swap(uint32_t slot_rva, uint32_t expect_rva, void *target)
{
    uint64_t want = (uint64_t)RVA(expect_rva);
    if (!bytes_equal(slot_rva, &want, 8)) {
        plog("vtable 슬롯 @%06X 원래 값 불일치", slot_rva);
        return 0;
    }
    return slot_cas(slot_rva, want, (uint64_t)target);
}

void vtable_unswap(uint32_t slot_rva, void *ours, uint32_t orig_rva)
{
    slot_cas(slot_rva, (uint64_t)ours, (uint64_t)RVA(orig_rva));
}

static void settings_path(wchar_t *path)
{
    path[0] = 0;
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n || n > MAX_PATH - 32) {
        path[0] = 0;
        return;
    }
    lstrcatW(path, L"\\san14patch");
    CreateDirectoryW(path, NULL);
    lstrcatW(path, L"\\settings.ini");
}

int settings_get_int(const wchar_t *section, const wchar_t *key, int def)
{
    wchar_t path[MAX_PATH];
    settings_path(path);
    return path[0] ? (int)GetPrivateProfileIntW(section, key, def, path) : def;
}

void settings_set_int(const wchar_t *section, const wchar_t *key, int value)
{
    wchar_t path[MAX_PATH], v[16];
    settings_path(path);
    if (!path[0])
        return;
    wsprintfW(v, L"%d", value);
    WritePrivateProfileStringW(section, key, v, path);
}
