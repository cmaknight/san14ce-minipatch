/* 그룹 4: 무장 목록(CCmnInfoList<CPersonData>)의 개성·전법 칸에 마우스를 올리면 설명 말풍선. 근거는 docs/04-tooltip.md.

정보 > 무장 목록과 각 기능의 무장 선택 목록이 같은 목록 부품을 쓴다. 무장 상세처럼 원래 툴팁이 있는 화면은 건드리지 않는다.
매 프레임: 목록의 갱신 슬롯(vtable 0x13ACF30 슬롯 1)에서
  1) 마우스 아래 위젯이 이 목록 현재 탭 본문의 행인지 확인하고, 행 → 데이터 인덱스(0xAE0370) → 무장
  2) 헤더 버튼을 x로 히트테스트해 보이는 열 → 탭의 열 ID → 열 정의(0x60 B)의 값 함수
  3) 값 함수(무장)가 돌려준 글자 포인터가 개성 객체 또는 전법 객체의 이름(+0x10)이면 그 개성·전법
개성은 게임의 CSimpleBalloonLayout(0x139C540)에 0x3594B0(개성 객체) 설명을 넣는다.
전법은 머리줄(의존능력·대거점·간격)까지 있는 CTacticsBalloonFullLayout(0x14778A0)을 쓰고
0x8A1330(말풍선, 전법)으로 전법을 넘긴다(게임: 0x88ABC0에서 생성·지정).

전법 말풍선 효과 줄: 효과 이름을 넣는 호출(0x8A2E18, 0x930D20(레이아웃, 자식 ID 16/18, 글자))을 감싸
"피해 (15)", "아군 공군상승 (3, 반경 1)"처럼 효과량·범위를 덧붙인다. 효과량 0이고 범위 0이면 그대로.
같은 말풍선 클래스라 무장 상세 화면에도 똑같이 적용된다.
*/
#include "common.h"
#include <wchar.h>

enum {
    VT_LIST = 0x13ACF30,
    SLOT_UPDATE = VT_LIST + 1 * 8, FN_UPDATE = 0x92AD0,
    SLOT_CLOSE = VT_LIST + 5 * 8, FN_CLOSE = 0x8FBD40,
    SLOT_DISABLE = VT_LIST + 8 * 8, FN_DISABLE = 0x7C9080,
    UI_MANAGER = 0x1B62110,      /* +0x170 마우스 아래 위젯, +0x18C ≠ 0이면 없음 */
    FN_INPUT = 0x11290,          /* 입력 상태: +0x34 x, +0x38 y */
    FN_ROW_INDEX = 0xAE0370,     /* int (scroll, 화면 행) → 데이터 인덱스 */
    FN_VALID = 0x32F500,
    FN_TRAIT_DESC = 0x3594B0,    /* const wchar_t* (개성 객체) */
    FN_NEW = 0x3F6BC0,
    FN_LAYOUT_CTOR = 0x864C50,
    FN_RES_CTOR = 0x92AB70,
    VT_BALLOON = 0x139C540,
    FN_BALLOON_INIT = 0x89C320,
    FN_ALPHA = 0x8A18C0,
    FN_BALLOON_TEXT = 0x8A2140,
    FN_BALLOON_PLACE = 0x8A5D60,
    ROOT_TRAITS = 0x7DD18,
    TRAIT_MAX = 0x162,
    ROOT_TACTICS = 0x774D0,      /* 전법 객체 id 0~0xC8, +0x10 이름 */
    TACTIC_MAX = 0xC8,
    FN_TBALLOON_CTOR = 0x89A300, /* CTacticsBalloonFullLayout, 0x1A8 B */
    FN_TBALLOON_SET = 0x8A1330,  /* (말풍선, 전법 객체): +0x178에 넣고 다시 그림(0x8A2B50) */
    SITE_EFFECT_TEXT = 0x8A2E18, /* 0x8A2B50 안의 call 0x930D20(효과 이름) */
    FN_CHILD_TEXT = 0x930D20,    /* (레이아웃+0x130, 자식 ID, 글자, 1) */
    FN_TACTIC_AMOUNT = 0x341000, /* int (전법, 효과 0/1): 효과량. 0이면 목록에 "-" */
    FN_TACTIC_KEY = 0x22E0A0,    /* 전법 덮어쓰기 값 getter의 넷째 인자 */
};

enum { KIND_NONE, KIND_TRAIT, KIND_TACTIC };

typedef void(__fastcall *Fn1)(void *);
typedef uint64_t(__fastcall *Fn4)(void *, void *, void *, void *);
typedef void *(__fastcall *PtrFn)(void *);
typedef void *(__fastcall *NewFn)(size_t);
typedef void *(__fastcall *InputFn)(void);
typedef int(__fastcall *RowFn)(void *, int);
typedef int(__fastcall *IntFn)(void *);
typedef void(__fastcall *Fn2b)(void *, int);
typedef void(__fastcall *TextFn)(void *, const wchar_t *);
typedef void *(__fastcall *GetterFn)(void *);
typedef void(__fastcall *Fn2p)(void *, void *);

#define CALL(type, rva) ((type)RVA(rva))
#define VCALL(obj, slot, type) ((type)((*(void ***)(obj))[slot]))

static uint8_t *g_balloon, *g_tballoon;
static int g_balloon_failed, g_opened, g_tballoon_failed;
static void *g_shown; /* 표시 중인 대상(개성·전법 객체 등) */

static int widget_visible_rect(uint8_t *w, int *x, int *y, int *wd, int *ht)
{
    int ax = 0, ay = 0;
    *wd = *(int *)(w + 0x28);
    *ht = *(int *)(w + 0x2C);
    if (*wd <= 0 || *ht <= 0)
        return 0;
    for (int depth = 0; w && depth < 16; depth++, w = *(uint8_t **)(w + 8)) {
        if ((*(uint32_t *)(w + 0x40) & 3) != 3)
            return 0;
        ax += *(int *)(w + 0x20);
        ay += *(int *)(w + 0x24);
    }
    *x = ax;
    *y = ay;
    return 1;
}

static int hit(uint8_t *w, int mx, int my, int check_y)
{
    int x, y, wd, ht;
    if (!w || !widget_visible_rect(w, &x, &y, &wd, &ht))
        return 0;
    if (mx < x || mx >= x + wd)
        return 0;
    return !check_y || (my >= y && my < y + ht);
}

/* 마우스 아래 개성·전법 객체(없으면 NULL). *kind에 종류 */
static void *hovered_obj(uint8_t *list, int mx, int my, int *kind)
{
    uint8_t *mgr = RVA(UI_MANAGER);
    if (*(int *)(mgr + 0x18C) != 0)
        return NULL;
    uint8_t *row = *(uint8_t **)(mgr + 0x170);
    int tab = *(int *)(list + 0x208);
    if (!row || tab < 0 || tab > 0x3F || tab >= *(int *)(list + 0x20C))
        return NULL;
    uint8_t *tabs = *(uint8_t **)(list + 0x150);
    uint8_t *coldefs = *(uint8_t **)(list + 0x158);
    uint8_t **bodies = *(uint8_t ***)(list + 0x130);
    if (!tabs || !coldefs || !bodies)
        return NULL;
    int *cols = *(int **)(tabs + (size_t)tab * 24);
    uint8_t *body = bodies[tab];
    if (!cols || !body || *(uint8_t **)(body + 8) != list || *(uint8_t **)(row + 0x48) != body)
        return NULL;
    if (!hit(body, mx, my, 1) || !hit(row, mx, my, 1))
        return NULL;

    /* 행 → 무장 */
    uint8_t *a = *(uint8_t **)(body + 0x138);
    a = a ? *(uint8_t **)(a + 0x20) : NULL;
    uint8_t *scroll = a ? *(uint8_t **)(a + 0x20) : NULL;
    int vrow = *(int *)(row + 0x80);
    if (!scroll || vrow < 0 || vrow >= *(int *)(scroll + 0x58))
        return NULL;
    int idx = CALL(RowFn, FN_ROW_INDEX)(scroll, vrow);
    void **items = *(void ***)(body + 0x198);
    if (idx < 0 || idx >= *(int *)(body + 0x1A0) || !items || !items[idx])
        return NULL;
    void *person = items[idx];

    /* x → 보이는 열 → 열 정의 */
    uint8_t **hb = *(uint8_t ***)(list + 0x210), **he = *(uint8_t ***)(list + 0x218);
    if (!hb || he <= hb || he - hb > 0x400)
        return NULL;
    int vis = -1;
    for (uint8_t **h = hb + 1; h < he; h++) {
        if (*h && hit(*h, mx, my, 0)) {
            vis = *(int *)(*h + 0x80);
            if (vis != (int)(h - hb) - 1)
                return NULL;
            break;
        }
    }
    if (vis < 0)
        return NULL;
    for (int i = 0; i <= vis; i++)
        if (cols[i] < 0)
            return NULL;
    int cid = cols[vis];
    if (cid > 0xFFF)
        return NULL;
    uint8_t *def = coldefs + (size_t)cid * 0x60;
    GetterFn getter = *(GetterFn *)(def + 0x18);
    if (!*(int *)(def + 0x5C) || !getter)
        return NULL;
    void *text = getter(person);

    /* 글자 포인터 → 개성·전법 */
    uint8_t *root = game_root();
    if (!root || !text)
        return NULL;
    for (int t = 1; t <= TRAIT_MAX; t++) {
        uint8_t *o = *(uint8_t **)(root + ROOT_TRAITS + (size_t)t * 8);
        if (o && text == o + 0x10) {
            *kind = KIND_TRAIT;
            return CALL(IntFn, FN_VALID)(o) ? o : NULL;
        }
    }
    for (int t = 0; t <= TACTIC_MAX; t++) {
        uint8_t *o = *(uint8_t **)(root + ROOT_TACTICS + (size_t)t * 8);
        if (o && text == o + 0x10) {
            *kind = KIND_TACTIC;
            return CALL(IntFn, FN_VALID)(o) ? o : NULL;
        }
    }
    return NULL;
}

static uint8_t *balloon(void)
{
    if (g_balloon || g_balloon_failed)
        return g_balloon;
    uint8_t *b = CALL(NewFn, FN_NEW)(0x180);
    if (!b) {
        g_balloon_failed = 1;
        return NULL;
    }
    CALL(Fn1, FN_LAYOUT_CTOR)(b);
    CALL(Fn1, FN_RES_CTOR)(b + 0x130);
    *(uint8_t **)b = RVA(VT_BALLOON);
    *(void **)(b + 0x178) = NULL;
    CALL(Fn2b, FN_BALLOON_INIT)(b, 0);
    VCALL(b, 25, Fn2b)(b, 0);
    if (!*(void **)(b + 0x138)) {
        plog("[개성 툴팁] 말풍선 리소스 없음. 툴팁 끔");
        g_balloon_failed = 1;
        return NULL;
    }
    *(uint8_t *)(b + 0x40) |= 8;
    CALL(Fn2b, FN_ALPHA)(b, 0xFF);
    VCALL(b, 14, Fn2b)(b, 6);
    g_balloon = b;
    return b;
}

/* 말풍선은 하나를 공유한다. key는 지금 표시 중인 대상(개성 객체 또는 다른 기능의 식별자).
   tooltip_show(key, NULL)은 이미 표시 중인 key의 위치만 갱신한다. */
void tooltip_hide(const void *key)
{
    if (g_balloon && g_shown && (!key || key == g_shown)) {
        VCALL(g_balloon, 25, Fn2b)(g_balloon, 0);
        g_shown = NULL;
    }
}

void tooltip_show(const void *key, const wchar_t *text)
{
    uint8_t *b = balloon();
    if (!b || ((!text || !text[0]) && key != g_shown))
        return;
    if (key != g_shown) {
        CALL(TextFn, FN_BALLOON_TEXT)(b, text);
        g_shown = (void *)key;
    }
    if (!g_opened) {
        VCALL(b, 7, Fn1)(b);
        g_opened = 1;
    }
    uint8_t place[0x40] = {0};
    *(uint8_t **)(place + 0x10) = b;
    *(int *)(place + 0x38) = 20;
    *(int *)(place + 0x3C) = 20;
    CALL(Fn1, FN_BALLOON_PLACE)(place);
    VCALL(b, 25, Fn2b)(b, 1);
}

/* 마우스가 위젯 w 안에 있으면 1(보이는 위젯만) */
int ui_mouse_in(void *w)
{
    uint8_t *in = CALL(InputFn, FN_INPUT)();
    return in && hit((uint8_t *)w, *(int *)(in + 0x34), *(int *)(in + 0x38), 1);
}

/* 마우스 아래 위젯(없으면 NULL) */
void *ui_hovered(void)
{
    uint8_t *mgr = RVA(UI_MANAGER);
    return *(int *)(mgr + 0x18C) ? NULL : *(void **)(mgr + 0x170);
}

/* 전법 말풍선. 생성 순서는 게임 0x88AEE6~: 생성 → 슬롯 49 초기화(0x89C3B0) → 0x8A1330(전법).
   층(슬롯 14), +0x40 |= 8, 열기(슬롯 7)는 개성 말풍선과 같게 한다. 보이기 슬롯 25, 위치 0x8A5D60. */
static uint8_t *tballoon(void)
{
    if (g_tballoon || g_tballoon_failed)
        return g_tballoon;
    uint8_t *b = CALL(NewFn, FN_NEW)(0x1A8);
    if (!b) {
        g_tballoon_failed = 1;
        return NULL;
    }
    CALL(Fn1, FN_TBALLOON_CTOR)(b);
    VCALL(b, 49, Fn1)(b);
    VCALL(b, 14, Fn2b)(b, 6);
    *(uint32_t *)(b + 0x40) |= 8;
    VCALL(b, 25, Fn2b)(b, 0);
    VCALL(b, 7, Fn1)(b);
    g_tballoon = b;
    return b;
}

static void *g_tshown; /* 전법 말풍선에 넣은 전법 */
static int g_tvisible;

static void tactic_hide(void)
{
    if (g_tballoon && g_tvisible) {
        VCALL(g_tballoon, 25, Fn2b)(g_tballoon, 0);
        g_tvisible = 0;
    }
}

static void tactic_show(void *tactic)
{
    uint8_t *b = tballoon();
    if (!b)
        return;
    if (!g_tvisible) {
        VCALL(b, 25, Fn2b)(b, 1);
        g_tvisible = 1;
    }
    if (tactic != g_tshown) {
        CALL(Fn2p, FN_TBALLOON_SET)(b, tactic);
        g_tshown = tactic;
    }
    uint8_t place[0x40] = {0};
    *(uint8_t **)(place + 0x10) = b;
    *(int *)(place + 0x38) = 20;
    *(int *)(place + 0x3C) = 20;
    CALL(Fn1, FN_BALLOON_PLACE)(place);
}

/* 효과 범위(0 없음, 1·2 반경). 목록 값 함수 0x358AF0/0x358950과 같은 방법: 덮어쓰기 목록(+0x70..+0x78이
   10칸)이 있으면 vfunc +0x48(전법, 필드 [목록+0xC / +0x10], 1, 0x22E0A0(전법)), 없으면 +0x64 / +0x65. */
static int tactic_range(uint8_t *t, int i)
{
    uint8_t *b = *(uint8_t **)(t + 0x70), *e = *(uint8_t **)(t + 0x78);
    if (b && (((uintptr_t)(e - b)) & ~(uintptr_t)3) == 0x28) {
        int key = ((int(__fastcall *)(void *))RVA(FN_TACTIC_KEY))(t);
        typedef uint8_t(__fastcall * FieldFn)(void *, int, char, int);
        return VCALL(t, 9, FieldFn)(t, b[i ? 0x10 : 0xC], 1, key);
    }
    return t[0x64 + i];
}

static void __fastcall effect_text_hook(uint8_t *layout, int id, const wchar_t *text, int flag)
{
    static wchar_t buf[2][128];
    const wchar_t *out = text;
    __try {
        uint8_t *t = *(uint8_t **)(layout - 0x130 + 0x178);
        int i = id == 16 ? 0 : id == 18 ? 1 : -1;
        if (i >= 0 && text && t && CALL(IntFn, FN_VALID)(t)) {
            int amount = ((int(__fastcall *)(void *, int))RVA(FN_TACTIC_AMOUNT))(t, i);
            int range = tactic_range(t, i);
            int n = -1;
            if (amount > 0 && range > 0)
                n = _snwprintf_s(buf[i], 128, _TRUNCATE, L"%s (%d, 반경 %d)", text, amount, range);
            else if (amount > 0)
                n = _snwprintf_s(buf[i], 128, _TRUNCATE, L"%s (%d)", text, amount);
            else if (range > 0)
                n = _snwprintf_s(buf[i], 128, _TRUNCATE, L"%s (반경 %d)", text, range);
            if (n > 0)
                out = buf[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = text;
    }
    ((void(__fastcall *)(void *, int, const wchar_t *, int))RVA(FN_CHILD_TEXT))(layout, id, out, flag);
}

static int g_list_owns; /* 목록 툴팁이 개성 말풍선을 쓰는 중 */

static void hide(void)
{
    if (g_list_owns) {
        tooltip_hide(NULL);
        g_list_owns = 0;
    }
    tactic_hide();
}

static void show(void *obj, int kind)
{
    if (kind == KIND_TACTIC) {
        if (g_list_owns) {
            tooltip_hide(NULL);
            g_list_owns = 0;
        }
        tactic_show(obj);
        return;
    }
    tactic_hide();
    if (obj == g_shown && g_list_owns) {
        tooltip_show(obj, NULL);
        return;
    }
    const wchar_t *desc = ((const wchar_t *(__fastcall *)(void *))RVA(FN_TRAIT_DESC))(obj);
    if (!desc || !desc[0]) {
        hide();
        return;
    }
    tooltip_show(obj, desc);
    g_list_owns = 1;
}

static uint64_t __fastcall update_hook(void *list, void *a, void *b, void *c)
{
    uint64_t r = CALL(Fn4, FN_UPDATE)(list, a, b, c);
    __try {
        uint8_t *in = CALL(InputFn, FN_INPUT)();
        int kind = KIND_NONE;
        void *t = in ? hovered_obj((uint8_t *)list, *(int *)(in + 0x34), *(int *)(in + 0x38), &kind) : NULL;
        if (t)
            show(t, kind);
        else
            hide();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hide();
    }
    return r;
}

static uint64_t __fastcall close_hook(void *list, void *a, void *b, void *c)
{
    hide();
    return CALL(Fn4, FN_CLOSE)(list, a, b, c);
}

static uint64_t __fastcall disable_hook(void *list, void *a, void *b, void *c)
{
    hide();
    return CALL(Fn4, FN_DISABLE)(list, a, b, c);
}

int feat_tooltip_init(void)
{
    if (!vtable_swap(SLOT_UPDATE, FN_UPDATE, (void *)update_hook))
        return 0;
    if (!vtable_swap(SLOT_CLOSE, FN_CLOSE, (void *)close_hook) ||
        !vtable_swap(SLOT_DISABLE, FN_DISABLE, (void *)disable_hook)) {
        vtable_unswap(SLOT_UPDATE, (void *)update_hook, FN_UPDATE);
        vtable_unswap(SLOT_CLOSE, (void *)close_hook, FN_CLOSE);
        return 0;
    }
    Patch p;
    if (patch_call(&p, SITE_EFFECT_TEXT, FN_CHILD_TEXT, (void *)effect_text_hook))
        patch_apply("전법 효과량·범위 표시", &p, 1);
    return 1;
}
