/* 그룹 6: 로그 기능 보강(로그 줄). 근거는 docs/06-logs.md. 로그 창은 feat_logwnd.c.

로그 줄은 게임 로그 기록기 0x934400(CLogManager 0x16EFC0)으로 쓴다. 메인 로그와 로그 목록에 함께 남는다.
기록기는 이벤트 중(`[0x27842A0]+0x1B0 != 0`) 아무것도 쓰지 않으므로, 우리가 쓰는 동안만 그 값을 0으로 내린다.

1) 사망(사망 처리 0x295AA0)
   - 관문 0x295C5F(`test ebx, ebx; je 0x296535`)는 군주·1품 관직·자세력·내 거점의 포로가 아니면 연출과 로그를 모두 건너뛴다.
     je의 대상을 중계 코드로 바꿔, 로그(arg8)가 켜져 있고 연출 생략(arg9)이 꺼져 있으면 표식을 세우고 로그 구간
     0x295FD6으로 보낸다. 미등장(7)·미발견(8) 무장은 원래대로 건너뛴다. 레지스터는 원래 건너뛰는 경로와 같다(edi=0).
   - 표식이 있으면 부고 컷인의 관계 판정 4곳(0x29640E·0x29641D·0x29642C·0x29643B)이 0을 돌려 컷인 없이 0x296525로 간다.
     원래 관문을 통과한 경우는 표식이 없어 원본과 같다. 표식은 함수 앞쪽 호출(0x295B03)에서 지운다.
   - 로그 구간의 기록 호출 2곳(0x296172·0x2963D3)은 이벤트 차단을 내리고 부른다(이벤트 사망 0x39EC7C·0x3AFC1A).
2) 이벤트 포로: 0x3AB280의 포획 호출 0x3AB312(0x2E0CF0, 원본은 로그 없음)를 감싼다. 포로가 되었고 잡은 세력이나
   원래 세력이 플레이어면 0x87E1 "{무장}, {세력}의 포로가 됨"을 남긴다.
3) 내정 Lv: 상업·농업·병영 세터(지역, 값, flag) 호출처 30곳(UI 0x621C70·0x621DA0 제외)을 감싼다. flag가 켜져 있고
   Lv(경계 0x13A6D9C)이 올랐고 지역의 거점이 플레이어 직할 군단(0x32E8D0)이면 "{지역}의 상업Lv이 3으로 상승"을 남긴다.
   0x27A660(최대치 달성 로그)과 같은 인자로 쓴다.
문구 빌더 0x1A23440은 다른 코드가 쓰는 중일 수 있어, 우리가 바꾸는 자리(무장·세력·장소 0번)는 쓰고 나서 되돌린다.
*/
#include "common.h"
#include <wchar.h>

enum {
    FN_LOG = 0x934400, FN_LOG_MGR = 0x16EFC0,
    MSG_BUILDER = 0x1A23440, B_PERSON0 = 0x1CD8, B_FORCE0 = 0x1D00, B_PLACE0 = 0x1DC8,
    EMPTY_STR_PTR = 0x1A47B98, EVENT_PTR = 0x27842A0, EVENT_BLOCK = 0x1B0,
    ROOT_WORLD = 0x871E8, WORLD_DATE = 0x450, ROOT_CITIES = 0xDD38, NO_PERSON = 0x1394,
    FN_VALID = 0x32F500, FN_FORCE_OF = 0x22CA00, FN_IS_PLAYER = 0x2337B0,
    FN_LOCATION = 0x22D580, FN_POS = 0x22DDA0, FN_REGION_IDX = 0x22D9E0, FN_DIRECT_CORPS = 0x32E8D0,
    LV_TABLE = 0x13A6D9C,
    /* 사망 */
    SITE_DEATH_ENTRY = 0x295B03, FN_DEATH_ENTRY = 0x32C130,
    SITE_GATE_JE = 0x295C61, DEATH_SKIP = 0x296535, DEATH_LOG = 0x295FD6,
    SITE_DEATH_LOG_A = 0x296172, SITE_DEATH_LOG_B = 0x2963D3,
    /* 포로 */
    SITE_EVENT_CAPTURE = 0x3AB312, FN_CAPTURE = 0x2E0CF0,
    MSG_CAPTURED = 0x87E1, MSG_ALL_MAX = 0x5F84,
    ST_PRISONER = 6, ST_UNAPPEARED = 7, ST_UNFOUND = 8,
};

typedef uint64_t U;
typedef U(__fastcall *Fn2)(U, U);
typedef U(__fastcall *Fn3)(U, U, U);
typedef U(__fastcall *Fn6)(U, U, U, U, U, U);
typedef U(__fastcall *Fn13)(U, U, U, U, U, U, U, U, U, U, U, U, U);
typedef int(__fastcall *ObjFn)(void *);
typedef void *(__fastcall *PtrFn)(void *);
#define CALL(type, rva) ((type)RVA(rva))

static int valid(void *o) { return o && CALL(ObjFn, FN_VALID)(o); }
static int is_player_force(void *f) { return valid(f) && CALL(ObjFn, FN_IS_PLAYER)(f); }

/* 이벤트 차단 칸(없으면 NULL) */
static int *event_block(void)
{
    uint8_t *e = *(uint8_t **)RVA(EVENT_PTR);
    return e ? (int *)(e + EVENT_BLOCK) : NULL;
}

/* 0x934400을 이벤트 차단을 내린 채로 부른다 */
static U call_log(U a1, U a2, U a3, U a4, U a5, U a6, U a7, U a8, U a9, U a10, U a11, U a12, U a13)
{
    int *ev = event_block(), saved = ev ? *ev : 0;
    if (ev)
        *ev = 0;
    U r = CALL(Fn13, FN_LOG)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
    if (ev)
        *ev = saved;
    return r;
}

/* 우리 로그 한 줄. 인자 배치는 0x27A660(종류 0)·0x2E10B2(종류 1)를 따른다 */
static void write_log(int kind, int corps, int pos, int f5, const wchar_t *text, int person, int f10, int f11)
{
    uint8_t *world = *(uint8_t **)(game_root() + ROOT_WORLD);
    void *mgr = ((void *(__fastcall *)(void))RVA(FN_LOG_MGR))();
    if (!world || !mgr)
        return;
    const wchar_t *empty = *(const wchar_t **)RVA(EMPTY_STR_PTR);
    call_log((U)mgr, kind, corps, pos, f5, *(int *)(world + WORLD_DATE), (U)text, (U)empty, person, f10, f11, 0, 0);
}

/* 문구 빌더: 0번 자리를 정하고 메시지를 펼쳐 out에 복사한 뒤 자리를 되돌린다 */
static int expand(int msg, int off, int value, int off2, int value2, wchar_t *out, size_t cap)
{
    uint8_t *b = RVA(MSG_BUILDER);
    int s1 = *(int *)(b + off), s2 = off2 ? *(int *)(b + off2) : 0;
    *(int *)(b + off) = value;
    if (off2)
        *(int *)(b + off2) = value2;
    typedef const wchar_t *(__fastcall * TextFn)(void *, int);
    const wchar_t *t = (*(TextFn **)b)[1](b, msg);
    int ok = t && wcsncpy_s(out, cap, t, _TRUNCATE) >= 0 && out[0];
    *(int *)(b + off) = s1;
    if (off2)
        *(int *)(b + off2) = s2;
    return ok;
}


/* ---------- 1) 사망 ---------- */
static volatile uint8_t *g_death_flag; /* 중계 코드 안의 1바이트 */

static U __fastcall death_entry_hook(U root, U id)
{
    if (g_death_flag)
        *g_death_flag = 0;
    return CALL(Fn2, FN_DEATH_ENTRY)(root, id);
}

#define REL_HOOK(name, rva)                                                                                        \
    static U __fastcall name(U a, U b) { return g_death_flag && *g_death_flag ? 0 : CALL(Fn2, rva)(a, b); }
REL_HOOK(rel_hook1, 0x2C9BD0)
REL_HOOK(rel_hook2, 0x2C8AC0)
REL_HOOK(rel_hook3, 0x2767E0)
REL_HOOK(rel_hook4, 0x232EE0)

static U __fastcall death_log_hook(U a1, U a2, U a3, U a4, U a5, U a6, U a7, U a8, U a9, U a10, U a11, U a12, U a13)
{
    return call_log(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

/* 관문 중계: 원래 건너뛸 때(ebx==0)만 온다. 로그 켜짐·연출 생략 아님·미등장/미발견 아님이면 표식 후 로그 구간으로 */
static uint8_t *build_gate_stub(void)
{
    static const uint8_t code[] = {
        0x83, 0xBD, 0x80, 0x01, 0x00, 0x00, 0x00,       /* 0  cmp dword [rbp+0x180], 0  (연출 생략) */
        0x75, 0x2E,                                     /* 7  jne skip */
        0x83, 0xBD, 0x78, 0x01, 0x00, 0x00, 0x00,       /* 9  cmp dword [rbp+0x178], 0  (로그) */
        0x74, 0x25,                                     /* 16 je skip */
        0x41, 0x0F, 0xB6, 0x85, 0xE6, 0x00, 0x00, 0x00, /* 18 movzx eax, byte [r13+0xE6] (신분) */
        0x3C, ST_UNAPPEARED,                            /* 26 cmp al, 7 */
        0x74, 0x19,                                     /* 28 je skip */
        0x3C, ST_UNFOUND,                               /* 30 cmp al, 8 */
        0x74, 0x15,                                     /* 32 je skip */
        0xC6, 0x05, 0x1C, 0x00, 0x00, 0x00, 0x01,       /* 34 mov byte [rip+0x1C], 1  (→ 69 표식) */
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 41 jmp [rip] → 로그 구간 */
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 55 skip: jmp [rip] → 원래 건너뜀 */
        0x00,                                           /* 69 표식 */
    };
    uint8_t *s = near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!s)
        return NULL;
    memcpy(s, code, sizeof code);
    uint64_t to_log = (uint64_t)RVA(DEATH_LOG), to_skip = (uint64_t)RVA(DEATH_SKIP);
    memcpy(s + 41 + 6, &to_log, 8);
    memcpy(s + 55 + 6, &to_skip, 8);
    FlushInstructionCache(GetCurrentProcess(), s, sizeof code);
    g_death_flag = s + 69;
    return s;
}

static int setup_death(void)
{
    uint8_t *stub = build_gate_stub();
    if (!stub)
        return 0;
    Patch p[8];
    int n = 0;
    /* je rel32(0F 84) 대상 교체 */
    int32_t old_rel = (int32_t)(DEATH_SKIP - (SITE_GATE_JE + 6));
    int64_t new_rel = (int64_t)(stub - RVA(SITE_GATE_JE + 6));
    if (new_rel != (int32_t)new_rel)
        return 0;
    p[n].rva = SITE_GATE_JE;
    p[n].len = 6;
    p[n].orig[0] = p[n].repl[0] = 0x0F;
    p[n].orig[1] = p[n].repl[1] = 0x84;
    memcpy(p[n].orig + 2, &old_rel, 4);
    int32_t r32 = (int32_t)new_rel;
    memcpy(p[n].repl + 2, &r32, 4);
    n++;
    static const struct { uint32_t site, fn; void *hook; } S[] = {
        {SITE_DEATH_ENTRY, FN_DEATH_ENTRY, (void *)death_entry_hook},
        {0x29640E, 0x2C9BD0, (void *)rel_hook1}, {0x29641D, 0x2C8AC0, (void *)rel_hook2},
        {0x29642C, 0x2767E0, (void *)rel_hook3}, {0x29643B, 0x232EE0, (void *)rel_hook4},
        {SITE_DEATH_LOG_A, FN_LOG, (void *)death_log_hook}, {SITE_DEATH_LOG_B, FN_LOG, (void *)death_log_hook},
    };
    for (int i = 0; i < (int)(sizeof S / sizeof S[0]); i++)
        if (!patch_call(&p[n++], S[i].site, S[i].fn, S[i].hook))
            return 0;
    return patch_apply("사망 로그", p, n);
}

/* ---------- 2) 이벤트 포로 ---------- */
static U __fastcall capture_hook(U p, U unit, U a3, U a4, U force, U a6)
{
    uint8_t *person = (uint8_t *)p;
    void *orig_force = NULL;
    int corps = 0;
    __try {
        if (valid(person)) {
            orig_force = CALL(PtrFn, FN_FORCE_OF)(person);
            corps = person[0xE0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    U r = CALL(Fn6, FN_CAPTURE)(p, unit, a3, a4, force, a6);
    __try {
        void *catcher = (void *)force;
        if (!orig_force || person[0xE6] != ST_PRISONER || !valid(catcher))
            return r;
        if (!is_player_force(catcher) && !is_player_force(orig_force))
            return r;
        int fid = (*(ObjFn **)catcher)[0x60 / 8](catcher);
        wchar_t text[512];
        if (!expand(MSG_CAPTURED, B_PERSON0, *(uint16_t *)(person + 0x10), B_FORCE0, fid, text, 512))
            return r;
        int pos = CALL(ObjFn, FN_POS)(CALL(PtrFn, FN_LOCATION)(person));
        write_log(1, corps, pos, 1, text, *(uint16_t *)(person + 0x10), 1, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

static int setup_capture(void)
{
    Patch p;
    return patch_call(&p, SITE_EVENT_CAPTURE, FN_CAPTURE, (void *)capture_hook) && patch_apply("이벤트 포로 로그", &p, 1);
}

/* ---------- 3) 내정 Lv ---------- */
static int dev_lv(int v)
{
    const int *t = (const int *)RVA(LV_TABLE);
    int lv = 1;
    for (int i = 0; i < 4 && v >= t[i]; i++)
        lv++;
    return lv;
}

static const wchar_t *const DEV_NAME[3] = {L"상업", L"농업", L"병영"};

static void dev_check(uint8_t *region, int kind, int before)
{
    int after = dev_lv(*(uint16_t *)(region + 0x3C + kind * 2));
    if (after <= before)
        return;
    uint8_t *root = game_root();
    unsigned cid = region[0x35];
    if (cid > 0x33)
        return;
    uint8_t *city = *(uint8_t **)(root + ROOT_CITIES + (size_t)cid * 8);
    if (!valid(city))
        return;
    typedef void *(__fastcall * VPtr)(void *);
    void *corps = (*(VPtr **)city)[0x80 / 8](city);
    void *direct = CALL(PtrFn, FN_DIRECT_CORPS)(*(void **)(root + ROOT_WORLD));
    if (!corps || corps != direct)
        return;
    /* 지역 이름: "{지역}의 모든 개발이 최대치가 됨"을 펼쳐 "의 모든 개발" 앞까지 */
    wchar_t all[512], text[512];
    int ridx = CALL(ObjFn, FN_REGION_IDX)(region);
    if (!expand(MSG_ALL_MAX, B_PLACE0, ridx, 0, 0, all, 512))
        return;
    wchar_t *cut = wcsstr(all, L"의 모든 개발");
    if (!cut)
        return;
    *cut = 0;
    swprintf_s(text, 512, L"%s의 %sLv이 %d%s 상승", all, DEV_NAME[kind], after, after == 3 ? L"으로" : L"로");
    int corps_id = (*(ObjFn **)city)[0x78 / 8](city);
    write_log(0, corps_id, *(uint16_t *)(region + 0x3A), 0, text, NO_PERSON, 0, 1);
}

#define DEV_HOOK(name, rva, kind)                                                                                  \
    static U __fastcall name(U region, U value, U flag)                                                            \
    {                                                                                                              \
        uint8_t *r = (uint8_t *)region;                                                                            \
        int before = 0, on = 0;                                                                                    \
        __try {                                                                                                    \
            on = (int)flag && r;                                                                                   \
            if (on)                                                                                                \
                before = dev_lv(*(uint16_t *)(r + 0x3C + (kind) * 2));                                             \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                                                   \
            on = 0;                                                                                                \
        }                                                                                                          \
        U ret = CALL(Fn3, rva)(region, value, flag);                                                               \
        if (on) {                                                                                                  \
            __try {                                                                                                \
                dev_check(r, kind, before);                                                                        \
            } __except (EXCEPTION_EXECUTE_HANDLER) {                                                               \
            }                                                                                                      \
        }                                                                                                          \
        return ret;                                                                                                \
    }
DEV_HOOK(commerce_hook, 0x240B20, 0)
DEV_HOOK(farm_hook, 0x2410C0, 1)
DEV_HOOK(barracks_hook, 0x241AA0, 2)

static int setup_domestic(void)
{
    /* 세터별 호출처(UI 0x621C70·0x621DA0 제외). docs/06-logs.md */
    static const uint32_t COMMERCE[] = {0x17500F, 0x1FC28F, 0x28E07F, 0x2955FD, 0x29D88B,
                                        0x2EC1A1, 0x3AD019, 0x3BE638, 0x41495C, 0x43CD1B};
    static const uint32_t FARM[] = {0x175032, 0x1FC2BF, 0x28E0A4, 0x295632, 0x29D8C2,
                                    0x2EC191, 0x3AA9C9, 0x3BD178, 0x41494A, 0x43CD43};
    static const uint32_t BARRACKS[] = {0x175055, 0x1FC2E9, 0x28E0CA, 0x295667, 0x29D8FB,
                                        0x2EC181, 0x3AE099, 0x3C0468, 0x414938, 0x43CD6B};
    Patch p[30];
    int n = 0;
    for (int i = 0; i < 10; i++)
        if (!patch_call(&p[n++], COMMERCE[i], 0x240B20, (void *)commerce_hook) ||
            !patch_call(&p[n++], FARM[i], 0x2410C0, (void *)farm_hook) ||
            !patch_call(&p[n++], BARRACKS[i], 0x241AA0, (void *)barracks_hook))
            return 0;
    return patch_apply("내정 Lv 로그", p, n);
}

int feat_logs_init(void)
{
    int a = setup_death(), b = setup_capture(), c = setup_domestic();
    if (!a || !b || !c)
        plog("[로그] 연결 실패: 사망 %d, 이벤트 포로 %d, 내정 Lv %d (1 = 정상)", a, b, c);
    return a && b && c;
}
