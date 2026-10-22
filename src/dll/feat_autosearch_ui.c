/* 그룹 3: 자동탐색 설정 화면(2단계)에 1~3행과 같은 모양의 4번째 행 "자동진행 [켬][끔]". 근거는 docs/03-autosearch.md.

2단계 레이아웃(리소스 "06_07_01_jinji_search_cn")은 장면 노드 트리다(노드 +0xB0 이름, +0xD0 부모,
[+0xE8]+0x30/+0x34 로컬 위치, +0xF0/+0xF2 크기). 행 하나 = Null_* 묶음(라벨 배경 + 글자) + 라디오 칸.
- 본 레이아웃: 구분선·명령서·실행무장 수(노드 42·43·51)를 한 행(97)만큼 내리고 패널 배경(1·5)을 97 늘린다.
- 4번째 행: 같은 리소스를 한 벌 더 불러와(복제 레이아웃) 3행 묶음(노드 33~35)만 남기고 숨긴 뒤 97 내린다.
  라벨 "자동진행", 라디오 0x24·0x27을 만들고 글자를 "켬/끔"으로 바꾼 뒤 라디오 컨트롤러로 묶는다.
- 선택은 컨트롤러가 스스로 바꾼다(0x4502D0이 클릭 리스너를 단다). "결정"(슬롯 5)에서 선택값(+0x18)을 읽어 저장.
- 복제 레이아웃과 컨트롤러는 화면을 닫을 때(슬롯 2) 게임과 같은 순서로 정리한다.
켜짐 상태는 세이브별(월드 +0x17AC). UI 추가 방법 전반은 docs/03-autosearch.md.
*/
#include "common.h"
#include <string.h>

enum {
    VT_AUTOSEARCH = 0x1447260,
    SLOT_DTOR = VT_AUTOSEARCH + 0 * 8, FN_DTOR = 0x6B16A0,
    SLOT_EXIT = VT_AUTOSEARCH + 2 * 8, FN_EXIT = 0x71E210, /* 화면 닫기: 컨트롤러 삭제 후 레이아웃 +0x40·+0x28·삭제 */
    SLOT_UPDATE = VT_AUTOSEARCH + 1 * 8, FN_UPDATE = 0x724B80, /* 화면 열기(한 번). 매 프레임이 아니다 */
    SLOT_DECIDE = VT_AUTOSEARCH + 5 * 8, FN_DECIDE = 0x732290, /* 결정: 1~3행 컨트롤러 +0x18을 월드 +0x17B0..에 저장 */
    STATE_LAYOUT = 0x488,
    FN_NEW = 0x3F6BC0,
    FN_MEMSET = 0x101B9B0,      /* (dst, 0, n) */
    FN_LAYOUT_CTOR = 0x864C50,
    VT_UILAYOUT = 0x1342530,
    FN_RES_CTOR = 0x92AB70,
    VSLOT_LOAD = 0x178 / 8,     /* (layout, 0, 0, 0, 이름, 0, 0) */
    VSLOT_OPEN = 0x38 / 8,
    FN_LAYOUT_PREP_OPEN = 0x873930,
    FN_CHILD = 0x92CEE0,        /* widget* (res, id) */
    FN_SET_VISIBLE = 0x92F810,  /* (res, id, bool) */
    FN_MAKE_CONTROL = 0x92E430, /* widget* (res, id, a3, kind, 1, 0) */
    FN_SET_CAPTION = 0x9310A0,  /* (res, id, const wchar_t*, 0xD) */
    FN_BUTTON_TEXT = 0x85C9C0,  /* (button, const wchar_t*) */
    VT_RADIO_CTRL = 0x13D64A0,
    VT_RADIO_LIST = 0x13D6490,
    FN_LIST_INIT = 0x21E2B0,
    FN_RADIO_ADD = 0x4502D0,    /* (ctrl, button) */
    FN_RADIO_SELECT = 0x451680, /* (ctrl, index) */
    FN_DLG_REGISTER = 0x570440, /* (state, 칸, layout) — 화면 열기·닫기 애니메이션 그룹(state +0x60)에 등록. 본 레이아웃은 칸 1 */
    FN_HELP_MGR = 0x84EC80,   /* (푸터 +0x180) → 하단 안내줄 관리자 */
    FN_HELP_SET = 0x89B1C0,   /* (관리자, 버튼, const wchar_t*, 0) — 마우스를 올리면 하단 안내줄에 표시 */
    STATE_FOOTER = 0x4A8,
    ROW = 97,                   /* 행 간격(노드 y −37, −134, −231) */
};

static const char LAYOUT_NAME[] = "06_07_01_jinji_search_cn";

typedef uint64_t(__fastcall *Fn4)(void *, void *, void *, void *);
typedef void *(__fastcall *PtrFn)(void *);
typedef void *(__fastcall *NewFn)(size_t);
typedef void(__fastcall *Fn1)(void *);
typedef void(__fastcall *MemsetFn)(void *, int, size_t);
typedef void(__fastcall *LoadFn)(void *, int, int, int, const char *, int, void *);
typedef void *(__fastcall *ChildFn)(void *, int);
typedef void(__fastcall *VisFn)(void *, int, int);
typedef void *(__fastcall *MakeFn)(void *, int, int, int, int, int);
typedef void(__fastcall *CaptionFn)(void *, int, const wchar_t *, int);
typedef void(__fastcall *TextFn)(void *, const wchar_t *);
typedef void(__fastcall *Ptr2Fn)(void *, void *);
typedef void(__fastcall *SelFn)(void *, int);
typedef void *(__fastcall *DelFn)(void *, int);
typedef void(__fastcall *RegFn)(void *, int, void *);
typedef void(__fastcall *HelpSetFn)(void *, void *, const wchar_t *, int);

#define CALL(type, rva) ((type)RVA(rva))

static int g_on;
static uint8_t *g_state, *g_clone, *g_ctrl;
static void *g_radio[2];

/* 켜짐 여부는 세이브별: 월드 +0x17AC의 0번 바이트(world_extra, common.h). 0 = 끔(새 게임, 패치 전 세이브), 1 = 켬. */
static uint8_t *auto_on_field(void)
{
    uint8_t *x = world_extra();
    return x ? x + WX_AUTO_ON : NULL;
}

int feat_autosearch_enabled(void)
{
    if (!g_cfg.auto_search)
        return 0;
    uint8_t *f = auto_on_field();
    g_on = f && *f == 1;
    return g_on;
}

static uint8_t *node(uint8_t *layout, int id)
{
    uint8_t *res = layout + 0x130;
    if (id < 0 || id >= *(int *)(res + 0x10))
        return NULL;
    return (*(uint8_t ***)(res + 8))[id];
}

static void move_node(uint8_t *layout, int id, float dy)
{
    uint8_t *n = node(layout, id);
    float *m = n ? *(float **)(n + 0xE8) : NULL;
    if (m)
        m[0x34 / 4] += dy;
}

static void grow_node(uint8_t *layout, int id, int dh)
{
    uint8_t *n = node(layout, id);
    if (n)
        *(int16_t *)(n + 0xF2) += (int16_t)dh;
}

static uint8_t *load_layout(void)
{
    uint8_t *l = CALL(NewFn, FN_NEW)(0x178);
    if (!l)
        return NULL;
    CALL(MemsetFn, FN_MEMSET)(l, 0, 0x178);
    CALL(Fn1, FN_LAYOUT_CTOR)(l);
    *(uint8_t **)l = RVA(VT_UILAYOUT);
    CALL(Fn1, FN_RES_CTOR)(l + 0x130);
    ((LoadFn)(*(void ***)l)[VSLOT_LOAD])(l, 0, 0, 0, LAYOUT_NAME, 0, NULL);
    return l;
}

static void build_row(uint8_t *state)
{
    uint8_t *main = *(uint8_t **)(state + STATE_LAYOUT);
    feat_autosearch_enabled();

    /* 본 레이아웃: 아래 줄을 한 행 내리고 패널을 늘린다 */
    move_node(main, 42, -ROW);
    move_node(main, 43, -ROW);
    move_node(main, 51, -ROW);
    grow_node(main, 1, ROW);
    grow_node(main, 5, ROW);

    /* 4번째 행: 같은 레이아웃을 한 벌 더 불러와 3행 묶음만 남긴다 */
    uint8_t *c = load_layout();
    if (!c) {
        plog("[자동 탐색] 4번째 행 레이아웃을 불러오지 못함");
        return;
    }
    uint8_t *res = c + 0x130;
    static const int hide[] = {1, 6, 21, 42, 43, 51};
    for (int i = 0; i < (int)(sizeof hide / sizeof hide[0]); i++)
        CALL(VisFn, FN_SET_VISIBLE)(res, hide[i], 0);
    move_node(c, 33, -ROW);
    g_radio[0] = CALL(MakeFn, FN_MAKE_CONTROL)(res, 0x24, 0x12, 1, 1, 0);
    g_radio[1] = CALL(MakeFn, FN_MAKE_CONTROL)(res, 0x27, 0x14, 1, 1, 0);
    CALL(CaptionFn, FN_SET_CAPTION)(res, 0x23, L"자동진행", 0xD);
    if (g_radio[0])
        CALL(TextFn, FN_BUTTON_TEXT)(g_radio[0], L"켬");
    if (g_radio[1])
        CALL(TextFn, FN_BUTTON_TEXT)(g_radio[1], L"끔");

    uint8_t *ctrl = CALL(NewFn, FN_NEW)(0x20);
    if (ctrl && g_radio[0] && g_radio[1]) {
        *(uint8_t **)ctrl = RVA(VT_RADIO_CTRL);
        *(uint8_t **)(ctrl + 8) = RVA(VT_RADIO_LIST);
        *(void **)(ctrl + 0x10) = NULL;
        CALL(Fn1, FN_LIST_INIT)(ctrl + 8);
        *(int *)(ctrl + 0x18) = 0x7FFFFFFF;
        CALL(Ptr2Fn, FN_RADIO_ADD)(ctrl, g_radio[0]);
        CALL(Ptr2Fn, FN_RADIO_ADD)(ctrl, g_radio[1]);
        CALL(SelFn, FN_RADIO_SELECT)(ctrl, g_on > 0 ? 0 : 1);
        g_ctrl = ctrl;
    }

    /* 1~3행처럼 하단 안내줄 문구 */
    uint8_t *footer = *(uint8_t **)(state + STATE_FOOTER);
    void *help = footer ? CALL(PtrFn, FN_HELP_MGR)(*(void **)(footer + 0x180)) : NULL;
    if (help && g_radio[0] && g_radio[1]) {
        CALL(HelpSetFn, FN_HELP_SET)(help, g_radio[0], L"페이즈 종료시 위 조건에 맞춰 남아있는 명령서를 이용해 탐색을 자동으로 진행합니다.", 0);
        CALL(HelpSetFn, FN_HELP_SET)(help, g_radio[1], L"페이즈 종료시 탐색을 자동으로 진행하지 않습니다.", 0);
    }

    *(uint32_t *)(c + 0x40) |= 8;
    CALL(Fn1, FN_LAYOUT_PREP_OPEN)(c);
    ((Fn1)(*(void ***)c)[VSLOT_OPEN])(c);
    /* 본 레이아웃과 같이 다음 단계로 넘어가면 숨고 돌아오면 보이도록 같은 애니메이션 그룹에 넣는다 */
    CALL(RegFn, FN_DLG_REGISTER)(state, 1, c);
    g_clone = c;
}

static uint64_t __fastcall update_hook(uint8_t *state, void *a, void *b, void *c)
{
    uint64_t r = CALL(Fn4, FN_UPDATE)(state, a, b, c);
    uint8_t *main = *(uint8_t **)(state + STATE_LAYOUT);
    if (state != g_state && main && CALL(ChildFn, FN_CHILD)(main + 0x130, 0x24)) {
        g_state = state;
        g_clone = g_ctrl = NULL;
        g_radio[0] = g_radio[1] = NULL;
        __try {
            build_row(state);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            plog("[자동 탐색] 4번째 행 생성 중 예외 %08X", GetExceptionCode());
        }
    }
    return r;
}

/* 1~3행과 같이 "결정"에서만 저장한다(뒤로는 버림). 클릭은 0x4525E0이 컨트롤러 +0x18에 바로 반영한다. */
static uint64_t __fastcall decide_hook(uint8_t *state, void *a, void *b, void *c)
{
    if (state == g_state && g_ctrl) {
        int sel = *(int *)(g_ctrl + 0x18);
        uint8_t *f = auto_on_field();
        if ((sel == 0 || sel == 1) && f) {
            g_on = sel == 0;
            *f = (uint8_t)g_on;
        }
    }
    return CALL(Fn4, FN_DECIDE)(state, a, b, c);
}

/* 게임의 화면 닫기(0x71E210)와 같은 순서로 정리한다: 컨트롤러를 먼저 지우고, 레이아웃은 닫기(+0x40)·해제(+0x28, 인자 그대로)·삭제 */
static void release(void *ctx)
{
    if (g_ctrl)
        ((DelFn)(*(void ***)g_ctrl)[0])(g_ctrl, 1);
    if (g_clone) {
        void **vt = *(void ***)g_clone;
        ((Fn1)vt[0x40 / 8])(g_clone);
        ((Ptr2Fn)vt[0x28 / 8])(g_clone, ctx);
        ((DelFn)(*(void ***)g_clone)[0])(g_clone, 1);
    }
    g_state = g_clone = g_ctrl = NULL;
    g_radio[0] = g_radio[1] = NULL;
}

static uint64_t __fastcall exit_hook(uint8_t *state, void *ctx, void *b, void *c)
{
    if (state == g_state) {
        __try {
            release(ctx);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            plog("[자동 탐색] 4번째 행 정리 중 예외 %08X", GetExceptionCode());
            g_state = g_clone = g_ctrl = NULL;
        }
    }
    return CALL(Fn4, FN_EXIT)(state, ctx, b, c);
}

/* 닫기 없이 소멸되는 경우의 안전망: 포인터만 잊는다(이미 게임이 정리했을 수 있으므로 지우지 않는다) */
static void *__fastcall dtor_hook(uint8_t *state, int flags)
{
    if (state == g_state) {
        g_state = g_clone = g_ctrl = NULL;
        g_radio[0] = g_radio[1] = NULL;
    }
    return CALL(DelFn, FN_DTOR)(state, flags);
}

int feat_autosearch_ui_init(void)
{
    if (!vtable_swap(SLOT_UPDATE, FN_UPDATE, (void *)update_hook))
        return 0;
    if (!vtable_swap(SLOT_EXIT, FN_EXIT, (void *)exit_hook)) {
        vtable_unswap(SLOT_UPDATE, (void *)update_hook, FN_UPDATE);
        return 0;
    }
    vtable_swap(SLOT_DTOR, FN_DTOR, (void *)dtor_hook);
    if (!vtable_swap(SLOT_DECIDE, FN_DECIDE, (void *)decide_hook))
        plog("[자동 탐색] 결정 연결 실패: 켬/끔이 저장되지 않음");
    return 1;
}
