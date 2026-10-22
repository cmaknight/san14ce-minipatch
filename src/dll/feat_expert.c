/* 그룹 1: 숙련 모드 기본 부대능력 배율. 근거는 docs/01-expert.md.

1) 설정 화면(새 게임 숙련 상세 설정, 탭 2): 인접효과 두 행(칸 4·5) 뒤에 "자신/타세력 기본 부대능력" 두 행(칸 61·62)을 끼운다.
   - 목록(CSetUpScenarioSeniorList, vtable 0x141BB18)의 행 개수 설정(슬롯 53)을 감싸 항목 배열에 우리 항목 둘을 넣는다.
     탭을 채우는 0x466220이 배열을 만든 뒤 이 슬롯을 부르므로, 그 뒤 새로고침은 게임이 한다.
   - 값 읽기 0x3EA400·쓰기 0x3EFCE0은 칸 번호가 0x3C를 넘으면 그냥 돌아간다. 그 분기(ja)를 우리 함수로 돌려 칸 61·62를 처리한다.
   - 슬라이더 범위·기본값 표 0x14005C0(칸당 int 7개: 최소, 최대, 난이도별 기본 5개, 61칸)을 63칸 사본으로 옮기고
     그 표를 가리키는 lea 5곳을 사본으로 바꾼다.
   - 행 설정 0x640F20 호출(0x5B919E)을 감싸 우리 행의 안내줄 문구를 0x89B1C0으로 등록한다.
   - 설정 슬롯 저장·불러오기·초기화와 시작 전 설정 확인 화면 목록에도 두 값을 넣는다(아래 해당 절).
2) 값 저장: 화면에서 정한 값은 DLL이 들고 있다가 월드 +0x17AC의 1·2번 바이트(world_extra)에 쓴다. 0이면 100%.
   세이브와 함께 저장된다. 쓰는 때: 슬라이더를 바꿀 때(현재 월드)와 월드 초기화 0x325B80 직후(호출 2곳:
   0x31AB46 월드 생성자, 0x335767). 초기화는 한 번의 새 게임에 여러 번 불릴 수 있어(2026-10-20 로그) 값을 되돌리지 않는다.
   화면 값은 게임 설정처럼 이 실행 동안 유지된다.
3) 적용: 전투력 0x2A8D40의 모든 호출(23곳)을 감싸, 숙련 모드이고 부대일 때 결과에 배율을 곱한다.
   공군·공성·방어가 이 값에서 나오고 기동·전법 피해는 이 값을 쓰지 않는다. 자신 = 플레이어 세력(0x2337B0).
4) 무장경험획득(칸 24) 최소 100 → 0(docs/01-expert.md). 범위 표 사본만 고치고, 안내줄을 0 설명을 넣은 3줄 문구로 바꾼다.
*/
#include "common.h"
#include <string.h>
#include <wchar.h>

enum {
    VT_SENIOR_LIST = 0x141BB18,
    SLOT_SET_COUNT = VT_SENIOR_LIST + 53 * 8, FN_SET_COUNT = 0x872CB0,
    FN_ALLOC = 0x3F6E80, FN_FREE = 0x3F6E90,   /* 게임 배열 할당/해제(0x466220과 같은 것) */
    SITE_READ_JA = 0x3EA403, READ_RET = 0x3EA6BF, /* 0x3EA400 안의 ja(칸 > 0x3C) */
    SITE_WRITE_JA = 0x3EFCE3, WRITE_RET = 0x3EFFDB,
    RANGE_TABLE = 0x14005C0, RANGE_N = 61, ADJ_MAX = 2000,
    IDX_XP = 24,
    SITE_ROW_SETUP = 0x5B919E, FN_ROW_SETUP = 0x640F20,
    FN_HELP_TEXT = 0x89B1C0,                    /* (관리자, 위젯, wchar_t*, 0) */
    SITE_NEW_GAME = 0x335767, SITE_WORLD_CTOR = 0x31AB46, FN_NEW_GAME = 0x325B80,
    FN_POWER = 0x2A8D40,
    FN_VALID = 0x32F500, FN_UNIT_FORCE = 0x22CC40, FN_IS_PLAYER = 0x2337B0,
    FN_SETTINGS = 0x3E9C90, FN_IS_EXPERT = 0x3EB2F0,
    ROOT_FORCES = 0xDF30,
    IDX_ANCHOR = 5,                             /* 이 칸(타세력 인접효과) 뒤에 넣는다 */
    IDX_SELF = 61, IDX_OTHER = 62,
};

typedef uint64_t(__fastcall *Fn4)(void *, void *, void *, void *);
typedef void(__fastcall *RowSetupFn)(void *, void *, void *, int, int);
typedef float(__fastcall *PowerFn)(void *, void *, int, int);
#define CALL(type, rva) ((type)RVA(rva))

static const wchar_t *const LABEL[2] = {L"자신세력 기본 부대능력", L"타세력 기본 부대능력"};
static const wchar_t *const HELP[2] = {
    L"자신 세력 부대의 공군·공성·방어에 % 배율을 적용합니다(파성, 기동, 전법 피해 제외)",
    L"다른 세력 부대의 공군·공성·방어에 % 배율을 적용합니다(파성, 기동, 전법 피해 제외)",
};

/* 칸 24 안내줄. 원래 3줄(메시지 10587: 변경 안내, 100 = 표준, 500 = 500%)을 0 설명까지 3줄로 다시 쓴다.
   안내줄은 3줄까지만 보인다(4줄이면 잘림, 2026-10-22 게임 확인) */
static const wchar_t XP_HELP[] = L"무장의 획득경험치량을 변경합니다.\n"
                                 L"100이면 표준치, 500이면 표준치의 500%가 됩니다.\n"
                                 L"0으로 설정하면 무장 경험치를 얻지 않습니다.";

static int g_pend[2] = {100, 100}; /* 설정 화면에서 정한 값(새 게임 시작 때 월드에 씀) */
static uint8_t g_item[2][0x60];      /* 우리 행 항목(게임 항목 0x60 B와 같은 배치) */

static int idx_of(int idx) { return idx == IDX_SELF ? 0 : idx == IDX_OTHER ? 1 : -1; }

static void store(uint8_t *x)
{
    if (!x)
        return;
    x[WX_BASE_SELF] = (uint8_t)(g_pend[0] == 100 ? 0 : g_pend[0]);
    x[WX_BASE_OTHER] = (uint8_t)(g_pend[1] == 100 ? 0 : g_pend[1]);
}

/* ---------- 값 읽기/쓰기 (0x3EA400 / 0x3EFCE0의 범위 밖 분기) ---------- */
static int __fastcall read_hook(void *cfg, int idx, int sub)
{
    (void)cfg;
    int i = idx_of(idx);
    return i >= 0 && sub == 0 ? g_pend[i] : 0;
}

static void __fastcall write_hook(void *cfg, int idx, int sub, int value)
{
    (void)cfg;
    int i = idx_of(idx);
    if (i >= 0 && sub == 0) {
        int v = value < 50 ? 50 : value > 200 ? 200 : value;
        if (v != g_pend[i]) {
            g_pend[i] = v;
            store(world_extra());
        }
    }
}

/* ---------- 행 끼워 넣기 ---------- */
static uint64_t __fastcall set_count_hook(uint8_t *list, void *n, void *c, void *d)
{
    int cnt = (int)(intptr_t)n;
    __try {
        /* 이 시점의 +0x160은 아직 이전 개수다. 배열(+0x158)은 이미 새 탭의 cnt개다(0x466220). */
        void **items = *(void ***)(list + 0x158);
        int at = -1, ours = 0;
        for (int i = 0; items && i < cnt && cnt < 200; i++) {
            int idx = items[i] ? *(int *)items[i] : -1;
            if (idx == IDX_ANCHOR)
                at = i;
            if (idx_of(idx) >= 0)
                ours = 1;
        }
        if (at >= 0 && !ours) {
            void **arr = ((void **(__fastcall *)(size_t))RVA(FN_ALLOC))((size_t)(cnt + 2) * 8);
            if (arr) {
                for (int k = 0; k < 2; k++) {
                    memcpy(g_item[k], items[at], 0x60);
                    *(int *)(g_item[k] + 0) = k ? IDX_OTHER : IDX_SELF;
                    *(int *)(g_item[k] + 4) = g_pend[k];
                    *(const wchar_t **)(g_item[k] + 0x18) = LABEL[k];
                }
                int j = 0;
                for (int i = 0; i < cnt; i++) {
                    arr[j++] = items[i];
                    if (i == at) {
                        arr[j++] = g_item[0];
                        arr[j++] = g_item[1];
                    }
                }
                ((void(__fastcall *)(void *))RVA(FN_FREE))(items);
                *(void ***)(list + 0x158) = arr;
                cnt += 2;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return CALL(Fn4, FN_SET_COUNT)(list, (void *)(intptr_t)cnt, c, d);
}

/* 행 설정 뒤 우리 행의 안내줄 문구(게임은 항목 +0x48의 메시지 ID로 등록한다) */
static void __fastcall row_setup_hook(uint8_t *row, uint8_t *item, void *mgr, int diff, int tab)
{
    CALL(RowSetupFn, FN_ROW_SETUP)(row, item, mgr, diff, tab);
    __try {
        int idx = item ? *(int *)item : -1, i = idx_of(idx);
        const wchar_t *text = i >= 0 ? HELP[i] : NULL;
        /* +0xC가 0이 아니면 게임은 DLC 없음 문구를 쓴다(0x641115) */
        if (idx == IDX_XP && !*(int *)(item + 0xC))
            text = XP_HELP;
        if (!text || !mgr)
            return;
        typedef void(__fastcall * HelpFn)(void *, void *, const wchar_t *, int);
        uint8_t *slider = *(uint8_t **)(row + 0x190);
        void *w[5] = {slider ? *(void **)(slider + 0x1C0) : NULL, slider ? *(void **)(slider + 0x1C8) : NULL,
                      *(void **)(row + 0x178), *(void **)(row + 0x198), *(void **)(row + 0x1A0)};
        for (int k = 0; k < 5; k++)
            if (w[k])
                CALL(HelpFn, FN_HELP_TEXT)(mgr, w[k], text, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* ---------- 새 게임: 월드에 기록 ---------- */
static uint64_t __fastcall new_game_hook(void *world, void *a, void *b, void *c)
{
    uint64_t r = CALL(Fn4, FN_NEW_GAME)(world, a, b, c);
    if (world)
        store((uint8_t *)world + 0x17AC);
    return r;
}

/* ---------- 전투력 배율 ---------- */
static int base_pct(uint8_t *unit)
{
    uint8_t *x = world_extra();
    if (!x || (!x[WX_BASE_SELF] && !x[WX_BASE_OTHER]))
        return 100;
    void *cfg = ((void *(__fastcall *)(void))RVA(FN_SETTINGS))();
    if (!cfg || !((int(__fastcall *)(void *))RVA(FN_IS_EXPERT))(cfg))
        return 100;
    if (!unit || !((int(__fastcall *)(void *))RVA(FN_VALID))(unit))
        return 100;
    int fid = ((int(__fastcall *)(void *))RVA(FN_UNIT_FORCE))(unit);
    uint8_t *root = game_root();
    if (!root || fid < 1 || fid > 0x33)
        return 100;
    void *force = *(void **)(root + ROOT_FORCES + (size_t)fid * 8);
    int self = force && ((int(__fastcall *)(void *))RVA(FN_VALID))(force) &&
               ((int(__fastcall *)(void *))RVA(FN_IS_PLAYER))(force);
    int v = x[self ? WX_BASE_SELF : WX_BASE_OTHER];
    return v ? v : 100;
}

static float __fastcall power_hook(uint8_t *unit, void *city, int a, int b)
{
    float r = CALL(PowerFn, FN_POWER)(unit, city, a, b);
    __try {
        int pct = base_pct(unit);
        if (pct != 100)
            r = r * (float)pct / 100.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

static const uint32_t POWER_CALLS[] = {
    0x086D8D, 0x086DD4, 0x086E40, 0x0B5ED2, 0x0BB2F2, 0x0BF1DE, 0x0C878D, 0x0C8882, 0x0CD95E, 0x0CDAA9, 0x16A18F,
    0x1919D5, 0x340C34, 0x340D47, 0x7FA4A1, 0x7FA51C, 0x913E62, 0x913EC9, 0x92396E, 0x923985, 0x927631, 0x927695,
};
static const uint32_t POWER_JMP = 0x0BF04B; /* 꼬리 호출 jmp */

/* rel32 분기(E8/E9/0F 8x)를 relay로 돌리는 Patch. op_len = 1 또는 2, 원래 대상 확인 */
static int patch_branch(Patch *p, uint32_t site, int op_len, uint32_t expect, void *target)
{
    uint8_t *relay = relay_new(target);
    if (!relay)
        return 0;
    int32_t old_rel = (int32_t)(expect - (site + op_len + 4));
    int64_t new_rel = (int64_t)(relay - RVA(site + op_len + 4));
    if (new_rel != (int32_t)new_rel)
        return 0;
    p->rva = site;
    p->len = (uint8_t)(op_len + 4);
    memcpy(p->orig, RVA(site), op_len);
    memcpy(p->repl, RVA(site), op_len);
    memcpy(p->orig + op_len, &old_rel, 4);
    int32_t r32 = (int32_t)new_rel;
    memcpy(p->repl + op_len, &r32, 4);
    return 1;
}

static int setup_screen(void)
{
    /* 범위 표 63칸 사본 */
    uint8_t *tab = near_alloc(0x1000, PAGE_READWRITE);
    if (!tab)
        return 0;
    memcpy(tab, RVA(RANGE_TABLE), RANGE_N * 0x1C);
    static const int row[7] = {50, 200, 100, 100, 100, 100, 100};
    memcpy(tab + IDX_SELF * 0x1C, row, sizeof row);
    memcpy(tab + IDX_OTHER * 0x1C, row, sizeof row);
    /* 자신/타세력 인접지역효과(칸 4·5, #12·#13) 최대 200 → 2000(2026-10-21 요청). 범위 표를 읽는 곳은 위 lea 5곳뿐 */
    for (int k = 4; k <= 5; k++) {
        int *r = (int *)(tab + k * 0x1C);
        if (r[1] == 200)
            r[1] = ADJ_MAX;
        else
            plog("칸 %d 최대값이 예상(200)과 다름: %d. 그대로 둠", k, r[1]);
    }
    int *xp = (int *)(tab + IDX_XP * 0x1C);
    if (xp[0] == 100)
        xp[0] = 0;
    else
        plog("칸 %d 최소값이 예상(100)과 다름: %d. 그대로 둠", IDX_XP, xp[0]);

    static const uint32_t LEA[] = {0x5FC645, 0x627342, 0x6362AE, 0x636391, 0x6410B4};
    Patch p[5 + 3];
    int n = 0;
    for (int i = 0; i < 5; i++, n++) {
        uint8_t *s = RVA(LEA[i]);
        int32_t disp = *(int32_t *)(s + 3);
        if (LEA[i] + 7 + disp != RANGE_TABLE)
            return 0;
        int64_t nd = (int64_t)(tab - (s + 7));
        if (nd != (int32_t)nd)
            return 0;
        p[n].rva = LEA[i];
        p[n].len = 7;
        memcpy(p[n].orig, s, 7);
        memcpy(p[n].repl, s, 3);
        int32_t d32 = (int32_t)nd;
        memcpy(p[n].repl + 3, &d32, 4);
    }
    if (!patch_branch(&p[n++], SITE_READ_JA, 2, READ_RET, (void *)read_hook) ||
        !patch_branch(&p[n++], SITE_WRITE_JA, 2, WRITE_RET, (void *)write_hook) ||
        !patch_call(&p[n++], SITE_ROW_SETUP, FN_ROW_SETUP, (void *)row_setup_hook))
        return 0;
    if (!patch_apply("숙련 설정 행", p, n))
        return 0;
    if (!vtable_swap(SLOT_SET_COUNT, FN_SET_COUNT, (void *)set_count_hook))
        return 0;
    Patch g[2];
    if (!patch_call(&g[0], SITE_NEW_GAME, FN_NEW_GAME, (void *)new_game_hook) ||
        !patch_call(&g[1], SITE_WORLD_CTOR, FN_NEW_GAME, (void *)new_game_hook) || !patch_apply("숙련 새 게임 기록", g, 2))
        return 0;
    return 1;
}

static int setup_power(void)
{
    enum { N = sizeof POWER_CALLS / sizeof POWER_CALLS[0] };
    Patch p[N + 1];
    for (int i = 0; i < N; i++)
        if (!patch_call(&p[i], POWER_CALLS[i], FN_POWER, (void *)power_hook))
            return 0;
    if (!patch_branch(&p[N], POWER_JMP, 1, FN_POWER, (void *)power_hook))
        return 0;
    return patch_apply("숙련 기본 부대능력 배율", p, N + 1);
}

/* ---------- 슬롯 저장·불러오기·초기화 ----------
   게임 설정 칸은 int 6개: 0 = 현재값, 1~5 = 저장 슬롯 1~5. 화면 사본(레이아웃+0x1C8)에서
   0x3EF930(사본, from, to)이 모든 칸의 from → to를 복사한다(저장: 0 → 슬롯, 불러오기: 슬롯 → 0),
   0x3EEFE0(사본, 0)은 표준값으로 초기화한다. 호출처: 화면 열기 0x5F0B86/0x5F0B9E(마지막 슬롯 또는 표준),
   불러오기 버튼 0x5F5B03/0x5F5B0F, 저장 0x6353C0·0x614542. 우리 두 값의 슬롯은 settings.ini [expert]에 둔다
   (게임 슬롯은 설정 파일 configS에 있어 칸을 늘릴 수 없다). 저장된 값이 없으면 100. */
static void slot_key(wchar_t *k, int slot, int i)
{
    wsprintfW(k, L"slot%d_%s", slot, i ? L"other" : L"self");
}

static void __fastcall slot_copy_hook(void *cfg, int from, int to)
{
    ((void(__fastcall *)(void *, int, int))RVA(0x3EF930))(cfg, from, to);
    wchar_t k[32];
    if (from == 0 && to >= 1 && to <= 5) {
        for (int i = 0; i < 2; i++) {
            slot_key(k, to, i);
            settings_set_int(L"expert", k, g_pend[i]);
        }
    } else if (to == 0 && from >= 1 && from <= 5) {
        for (int i = 0; i < 2; i++) {
            slot_key(k, from, i);
            int v = settings_get_int(L"expert", k, 100);
            g_pend[i] = v < 50 || v > 200 ? 100 : v;
        }
        store(world_extra());
    }
}

static void __fastcall reset_hook(void *cfg, int a)
{
    ((void(__fastcall *)(void *, int))RVA(0x3EEFE0))(cfg, a);
    g_pend[0] = g_pend[1] = 100;
    store(world_extra());
}

/* ---------- 설정 확인 화면(0x5D1310) ----------
   60칸 항목(0x28 B: +0 종류, +4 이름 메시지 ID, +8 값 글자 wstring)을 0x58820(목록, &항목)으로 넣는다(0x5D463F).
   13번(메시지 0x888) 뒤에 우리 항목 둘을 넣는다. 이름은 행 갱신 0x5B8680이 0x30CF80(+4)로 얻으므로(0x5B86C6)
   가짜 메시지 ID일 때 우리 문구를 돌려준다. */
enum { MSG_ADJ_OTHER = 0x888, MSG_FAKE_SELF = 0x7E0001, MSG_FAKE_OTHER = 0x7E0002 };

static void __fastcall confirm_push_hook(void *list, void **pitem)
{
    typedef void(__fastcall * PushFn)(void *, void **);
    CALL(PushFn, 0x58820)(list, pitem);
    __try {
        uint8_t *it = pitem ? (uint8_t *)*pitem : NULL;
        if (!it || *(int *)it != 0 || *(int *)(it + 4) != MSG_ADJ_OTHER)
            return;
        for (int i = 0; i < 2; i++) {
            void *n = ((void *(__fastcall *)(size_t))RVA(0x3F6BC0))(0x28);
            if (!n)
                return;
            typedef void *(__fastcall * MakeFn)(void *, int, int, int);
            CALL(MakeFn, 0x59BD90)(n, 0, MSG_ADJ_OTHER, g_pend[i]);
            *(int *)((uint8_t *)n + 4) = i ? MSG_FAKE_OTHER : MSG_FAKE_SELF;
            CALL(PushFn, 0x58820)(list, &n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static const wchar_t *__fastcall msg_hook(int id)
{
    if (id == MSG_FAKE_SELF)
        return LABEL[0];
    if (id == MSG_FAKE_OTHER)
        return LABEL[1];
    return ((const wchar_t *(__fastcall *)(int))RVA(0x30CF80))(id);
}

static int setup_slots(void)
{
    static const struct { uint32_t site, fn; void *hook; } S[] = {
        {0x5F0B86, 0x3EEFE0, (void *)reset_hook},     {0x5F5B03, 0x3EEFE0, (void *)reset_hook},
        {0x5F0B9E, 0x3EF930, (void *)slot_copy_hook}, {0x5F5B0F, 0x3EF930, (void *)slot_copy_hook},
        {0x6353C0, 0x3EF930, (void *)slot_copy_hook}, {0x614542, 0x3EF930, (void *)slot_copy_hook},
        {0x5D463F, 0x058820, (void *)confirm_push_hook}, {0x5B86C6, 0x30CF80, (void *)msg_hook},
    };
    enum { N = sizeof S / sizeof S[0] };
    Patch p[N];
    for (int i = 0; i < N; i++)
        if (!patch_call(&p[i], S[i].site, S[i].fn, S[i].hook))
            return 0;
    return patch_apply("숙련 설정 슬롯·확인 화면", p, N);
}

int feat_expert_init(void)
{
    int ok = setup_power();
    if (ok)
        ok = setup_screen();
    if (ok)
        ok = setup_slots();
    if (!ok)
        plog("[숙련] 기본 부대능력 일부 실패");
    return ok;
}
