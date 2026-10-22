/* 프록시 진입점. dinput8.dll 이름으로 게임 폴더에 두면 게임이 먼저 이 DLL을 올린다.

DllMain 시점에는 SteamStub이 .text를 아직 복호화하지 않았다(docs/00-overview.md). 그래서 DllMain에서는 아무것도 하지 않고,
게임이 처음 DirectInput8Create를 부를 때(복호화 이후) 한 번만 초기화한다. 예비로 복호화를 기다리는 스레드도 둔다.
*/
#include "common.h"

uint8_t *g_base;
static HMODULE g_real;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

typedef HRESULT(WINAPI *DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, void *);

static HMODULE real_dinput8(void)
{
    if (!g_real) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n && n < MAX_PATH - 16) {
            lstrcatW(path, L"\\dinput8.dll");
            g_real = LoadLibraryW(path);
        }
    }
    return g_real;
}

/* .text 복호화 여부: 알려진 원본 바이트(0x21064E mov edx, 100) */
static int text_decrypted(void)
{
    static const uint8_t want[] = {0xBA, 0x64, 0x00, 0x00, 0x00};
    return bytes_equal(0x21064E, want, sizeof want);
}

static int check_build(void)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(g_base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != GAME_TIMESTAMP || nt->OptionalHeader.SizeOfImage != GAME_IMAGE_SIZE) {
        plog("지원하지 않는 게임 빌드: timestamp %08X size %08X. 아무것도 바꾸지 않음",
             nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return 0;
    }
    return 1;
}

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    log_open();
    config_load();
    plog("san14patch DLL 시작: 숙련=%d 개성=%d 자동탐색=%d 툴팁=%d 안개=%d 로그=%d 관직=%d 훈련비=%d 내정=%d",
         g_cfg.expert_scaling, g_cfg.trait_balance, g_cfg.auto_search, g_cfg.trait_tooltip, g_cfg.fog_of_war,
         g_cfg.log_plus, g_cfg.post_busy, g_cfg.train_cost, g_cfg.even_develop);
    if (!check_build())
        return TRUE;
    if (!text_decrypted()) {
        plog(".text가 아직 복호화되지 않음. 초기화 중단");
        return TRUE;
    }
    if (g_cfg.trait_balance)
        feat_jeodol_init();
    if (g_cfg.expert_scaling)
        feat_expert_init();
    if (g_cfg.auto_search)
        feat_autosearch_init();
    if (g_cfg.trait_tooltip)
        feat_tooltip_init();
    if (g_cfg.post_busy)
        feat_post_init();
    if (g_cfg.log_plus) {
        feat_logs_init();
        feat_logwnd_init();
    }
    if (g_cfg.train_cost)
        feat_train_init();
    if (g_cfg.even_develop)
        feat_develop_init();
    return TRUE;
}

static void init(void)
{
    if (!g_base)
        g_base = (uint8_t *)GetModuleHandleW(NULL);
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
}

/* 예비 경로: DirectInput8Create가 늦게 불리거나 안 불려도 복호화가 끝나면 초기화한다. */
static DWORD WINAPI waiter(LPVOID p)
{
    (void)p;
    for (int i = 0; i < 600; i++) { /* 최대 60초 */
        Sleep(100);
        if (text_decrypted()) {
            Sleep(500); /* OEP 진입 후 게임 전역 초기화가 진행될 시간을 준다 */
            init();
            return 0;
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_base = (uint8_t *)GetModuleHandleW(NULL);
        HANDLE t = CreateThread(NULL, 0, waiter, NULL, 0, NULL);
        if (t)
            CloseHandle(t);
    }
    return TRUE;
}

/* --- dinput8 export 전달 --- */

static FARPROC real(const char *name)
{
    HMODULE m = real_dinput8();
    return m ? GetProcAddress(m, name) : NULL;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID iid, LPVOID *out, void *outer)
{
    init();
    DI8Create f = (DI8Create)real("DirectInput8Create");
    return f ? f(inst, ver, iid, out, outer) : E_FAIL;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    FARPROC f = real("DllCanUnloadNow");
    return f ? ((HRESULT(WINAPI *)(void))f)() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID *o)
{
    FARPROC f = real("DllGetClassObject");
    return f ? ((HRESULT(WINAPI *)(REFCLSID, REFIID, LPVOID *))f)(c, i, o) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    FARPROC f = real("DllRegisterServer");
    return f ? ((HRESULT(WINAPI *)(void))f)() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    FARPROC f = real("DllUnregisterServer");
    return f ? ((HRESULT(WINAPI *)(void))f)() : E_FAIL;
}

LPCVOID WINAPI GetdfDIJoystick(void)
{
    FARPROC f = real("GetdfDIJoystick");
    return f ? ((LPCVOID(WINAPI *)(void))f)() : NULL;
}
