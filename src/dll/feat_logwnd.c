/* 그룹 6: 메인 로그 창 높이 조절(4~19줄)과 항상 휠. 근거는 docs/06-logs.md.

원본은 접힘(3줄)일 때 한 줄 위젯 4개만 있고, 확장 버튼을 누르면 0x66B2C0이 CLogCtrlList(최대 19행)를 만든다.
여기서는 목록을 항상 만들고(그래서 휠이 늘 동작) 행 수 N을 설정값으로 바꾼다.
- 행 수: 0x66B2C0 안의 상수 19 두 곳(0x66B484 `lea ecx,[r12+0x13]`, 0x66B53E `cmp esi,19; mov eax,19; cmovg`)을
  중계 코드로 N을 읽게 바꾼다. 행 위치는 게임의 행 수별 노드 표 0x142D9A0(1~19행)을 그대로 쓴다.
- 스크롤바(CCmnScrollbar, 레이아웃 +0x1E0): 원래는 19행 길이(노드 0x23)로 한 번 만들고 로그가 19개를 넘으면 보인다.
  만들 때 보이는 분기(0x66B6DA jbe)는 jmp로 바꾸고, 열 때마다 목록 높이에 맞춰 옮기고(vfunc+0x78) 길이를 바꾼 뒤
  (vfunc+0x158, +0x1A4) 화살표·트랙·손잡이를 다시 배치한다(0x84CE60). 로그가 N개를 넘을 때 보인다.
  길이가 원래 최소(화살표 높이 × 2 + 0x54, 0x851930)보다 짧으면 숨긴다.
- 목록 열기 호출 3곳(0x66C90F 새 로그 때 다시 만들기, 0x66F7DC 프레임 갱신, 0x67018B 토글)을 감싸,
  연 뒤 맨 아래(최신)로 스크롤하고 확장 버튼 아이콘을 맞춘다(최대면 접기 0x1CA, 아니면 펼치기 0x1C8).
- 프레임 갱신(CMainLogLayout vtable +0x10, 0x66F7C0): 보이는데 목록이 없으면 연다. 목록 위쪽 가장자리를 눌러 끌면
  1줄 단위로 N을 바꾸고 목록을 다시 만든다(닫기 0x66F5D0 → 열기). 놓으면 settings.ini [log] rows에 저장한다.
  커서는 입력 객체(0x11290) +0x34의 캔버스 좌표, 목록 위치는 위젯 판정 0x86EB40과 같은 방식(부모를 따라 +0x20 합,
  크기 +0x28)으로 구한다. 버튼은 GetAsyncKeyState로 본다(게임 창이 앞에 있을 때만).
  손잡이는 목록 안쪽 위 가장자리라 누르면 목록 위젯이 받는다(지도로 가지 않는다).
- 손잡이 표시: 손잡이 위에 있거나 끄는 동안 커서를 ↕로 바꾼다. WM_SETCURSOR 처리(vtable 0x13FE1F8, 0x574690)를 감싸
  Windows 커서를 IDC_SIZENS로 두고, 게임 커서 모드(설정 0x1EDA40 == 1, Windows 커서를 숨기고 그림을 그림)면
  그동안 커서 그림(CSoftwareMouseCursorManager 0x86B400 +8, vfunc+0xC8)을 숨긴다.
- 확장 버튼(토글 람다 vtable 0x142E320, 0x670170): 최대면 4줄, 아니면 19줄로 바꾼 뒤 원래 토글로 다시 연다.
*/
#include "common.h"
#include <string.h>

enum {
    VT_LAYOUT_TICK = 0x142D1A0, FN_TICK = 0x66F7C0,
    VT_TOGGLE = 0x142E320, FN_TOGGLE = 0x670170,
    FN_OPEN = 0x66B2C0, FN_CLOSE = 0x66F5D0, FN_VISIBLE = 0x86F250,
    SITE_OPEN_REFRESH = 0x66C90F, SITE_OPEN_TICK = 0x66F7DC, SITE_OPEN_TOGGLE = 0x67018B,
    SITE_ROWS_A = 0x66B484, RET_ROWS_A = 0x66B489, SITE_ROWS_B = 0x66B53E, RET_ROWS_B = 0x66B549,
    SITE_SCROLLBAR_JBE = 0x66B6DA,
    FN_INPUT = 0x11290, FN_ROW_SIZE = 0x92CF90, FN_SCROLL_TO = 0x875BC0,
    L_LIST = 0x1D8, L_SCROLLBAR = 0x1E0, L_BUTTON = 0x1A0, L_EXPANDED = 0x1E8, L_NODES = 0x130,
    ICON_EXPAND = 0x1C8, ICON_COLLAPSE = 0x1CA,
    ROWS_MIN = 4, ROWS_MAX = 19, /* 최소 = 처음 기본값. 3줄은 스크롤바가 들어가지 않아 4줄(2026-10-22 결정) */
    GRAB_ABOVE = 1, GRAB_BELOW = 8, /* 손잡이: 목록 위쪽 가장자리 위 1px ~ 안쪽 8px */
    FN_SB_RELAYOUT = 0x84CE60, FN_NODE_SIZE = 0x92D020, SB_MIN_EXTRA = 0x54,
    VT_SETCURSOR = 0x13FE1F8, FN_SETCURSOR = 0x574690,
    FN_CURSOR_MGR = 0x86B400, FN_SETTINGS = 0x3E9C90, FN_CURSOR_MODE = 0x1EDA40,
};

typedef uint64_t U;
typedef void(__fastcall *LayoutFn)(void *);
typedef int(__fastcall *VisFn)(void *);
#define CALL(type, rva) ((type)RVA(rva))

static volatile int *g_rows; /* 중계 코드 페이지 안. 0x66B2C0이 읽는다 */

typedef struct {
    uint8_t *layout;
    int active, was_down, bottom, rowh;
} Drag;
static Drag g_drag;
static volatile int g_cue;      /* 손잡이 위이거나 끄는 중: ↕ 커서 */
static int g_sprite_hidden;     /* 게임 커서 그림을 우리가 숨김 */
static uint8_t *g_sb_layout;    /* 스크롤바를 맞춘 레이아웃과 보일지 */
static int g_sb_want;

static int clamp_rows(int n) { return n < ROWS_MIN ? ROWS_MIN : n > ROWS_MAX ? ROWS_MAX : n; }

/* 위젯 절대 사각형(0x86EB40과 같은 계산) */
static void widget_rect(uint8_t *w, int r[4])
{
    int x = 0, y = 0;
    uint8_t *p = w;
    for (int i = 0; p && i < 30; i++, p = *(uint8_t **)(p + 8)) {
        x += *(int *)(p + 0x20);
        y += *(int *)(p + 0x24);
    }
    r[0] = x, r[1] = y, r[2] = *(int *)(w + 0x28), r[3] = *(int *)(w + 0x2C);
}

static void set_visible(uint8_t *w, int on)
{
    typedef void(__fastcall * ShowFn)(void *, int);
    (*(ShowFn **)w)[0xC8 / 8](w, on);
}

static int is_visible(uint8_t *w) { return (*(uint32_t *)(w + 0x40) & 3) == 3; }

/* 스크롤바를 목록 높이에 맞추고 보일지 정한다 */
static void fit_scrollbar(uint8_t *layout, uint8_t *list, uint8_t *sb)
{
    int want = 0;
    int count = *(int *)(list + 0x174), lr[4], sr[4];
    widget_rect(list, lr);
    widget_rect(sb, sr);
    uint8_t *nodes = *(uint8_t **)(sb + 0x138);
    uint64_t arrow = nodes ? ((uint64_t(__fastcall *)(void *))RVA(FN_NODE_SIZE))(*(void **)(nodes + 0x18)) : 0;
    int len = lr[3], min_len = (int)(arrow >> 32) * 2 + SB_MIN_EXTRA;
    if (nodes && count > *g_rows && len >= min_len) {
        if (sr[1] != lr[1] || sr[3] != len) {
            typedef void(__fastcall * PosFn)(void *, int, int);
            typedef void(__fastcall * LenFn)(void *, int);
            (*(PosFn **)sb)[0x78 / 8](sb, *(int *)(sb + 0x20), *(int *)(sb + 0x24) + lr[1] - sr[1]);
            (*(LenFn **)sb)[0x158 / 8](sb, len);
            *(int *)(sb + 0x1A4) = len;
            ((void(__fastcall *)(void *))RVA(FN_SB_RELAYOUT))(sb);
        }
        want = 1;
    }
    g_sb_layout = layout;
    g_sb_want = want;
    if (is_visible(sb) != want)
        set_visible(sb, want);
}

/* 열고 난 뒤: 최신으로 스크롤, 스크롤바 맞춤, 버튼 아이콘 */
static void after_open(uint8_t *layout)
{
    uint8_t *list = *(uint8_t **)(layout + L_LIST);
    if (list)
        ((void(__fastcall *)(void *, float, int))RVA(FN_SCROLL_TO))(list, 1.0e9f, 1);
    uint8_t *sb = *(uint8_t **)(layout + L_SCROLLBAR);
    if (sb && list)
        fit_scrollbar(layout, list, sb);
    uint8_t *btn = *(uint8_t **)(layout + L_BUTTON);
    if (btn) {
        typedef void(__fastcall * IconFn)(void *, int, int);
        (*(IconFn **)btn)[0x1F8 / 8](btn, 0xF, *g_rows >= ROWS_MAX ? ICON_COLLAPSE : ICON_EXPAND);
    }
}

static void __fastcall open_hook(uint8_t *layout)
{
    CALL(LayoutFn, FN_OPEN)(layout);
    __try {
        after_open(layout);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void rebuild(uint8_t *layout)
{
    if (*(void **)(layout + L_LIST))
        CALL(LayoutFn, FN_CLOSE)(layout);
    open_hook(layout);
}

static int game_foreground(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
}

/* ↕ 커서 켜고 끄기. Windows 커서는 WM_SETCURSOR 처리(창 스레드)가 바꾸고, 게임 커서 그림은 여기서 숨긴다 */
static void set_cue(int on)
{
    g_cue = on;
    void *cfg = ((void *(__fastcall *)(void))RVA(FN_SETTINGS))();
    int game_cursor = cfg && ((int(__fastcall *)(void *))RVA(FN_CURSOR_MODE))(cfg) == 1;
    uint8_t *mgr = ((uint8_t * (__fastcall *)(void)) RVA(FN_CURSOR_MGR))();
    uint8_t *sprite = mgr ? *(uint8_t **)(mgr + 8) : NULL;
    if (!sprite)
        return;
    if (on && game_cursor && !g_sprite_hidden) {
        set_visible(sprite, 0);
        g_sprite_hidden = 1;
    } else if (!on && g_sprite_hidden) {
        set_visible(sprite, 1);
        g_sprite_hidden = 0;
    }
}

static U __fastcall setcursor_hook(U self, U hwnd, U wparam, U hit, U msg)
{
    static HCURSOR sizens;
    if (g_cue && (hit & 0xFFFF) == HTCLIENT) {
        if (!sizens)
            sizens = LoadCursorW(NULL, MAKEINTRESOURCEW(32645)) /* IDC_SIZENS */;
        SetCursor(sizens);
        return TRUE;
    }
    return ((U(__fastcall *)(U, U, U, U, U))RVA(FN_SETCURSOR))(self, hwnd, wparam, hit, msg);
}

static void drag_tick(uint8_t *layout, uint8_t *list)
{
    int down = game_foreground() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    int pressed = down && !g_drag.was_down;
    g_drag.was_down = down;
    uint8_t *in = ((uint8_t * (__fastcall *)(void)) RVA(FN_INPUT))();
    if (!in)
        return;
    int cx = *(int *)(in + 0x34), cy = *(int *)(in + 0x38);
    if (g_drag.active && g_drag.layout != layout)
        g_drag.active = 0;
    int r[4];
    widget_rect(list, r);
    int on_grip = cx >= r[0] && cx < r[0] + r[2] && cy >= r[1] - GRAB_ABOVE && cy <= r[1] + GRAB_BELOW;
    set_cue(g_drag.active || (on_grip && (!down || pressed)));
    if (pressed && !g_drag.active) {
        int wh[2] = {0, 0};
        ((void(__fastcall *)(void *, int *, int))RVA(FN_ROW_SIZE))(layout + L_NODES, wh, 2);
        if (wh[1] > 0 && on_grip) {
            g_drag.layout = layout;
            g_drag.active = 1;
            g_drag.bottom = r[1] + r[3];
            g_drag.rowh = wh[1];
        }
        return;
    }
    if (!g_drag.active)
        return;
    if (!down) {
        g_drag.active = 0;
        settings_set_int(L"log", L"rows", *g_rows);
        return;
    }
    int n = clamp_rows((g_drag.bottom - cy + g_drag.rowh / 2) / g_drag.rowh);
    if (n != *g_rows) {
        *g_rows = n;
        rebuild(layout);
    }
}

static void __fastcall tick_hook(uint8_t *layout)
{
    __try {
        uint8_t *list = *(uint8_t **)(layout + L_LIST);
        if (CALL(VisFn, FN_VISIBLE)(layout)) {
            if (!list) {
                open_hook(layout);
                list = *(uint8_t **)(layout + L_LIST);
            }
            uint8_t *sb = *(uint8_t **)(layout + L_SCROLLBAR);
            if (sb && g_sb_layout == layout && is_visible(sb) != g_sb_want)
                set_visible(sb, g_sb_want);
            if (list)
                drag_tick(layout, list);
        } else if (g_drag.layout == layout) {
            g_drag.active = 0;
            set_cue(0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    CALL(LayoutFn, FN_TICK)(layout);
}

/* 확장 버튼: 최대 ↔ 최소. 닫은 뒤 원래 토글이 다시 연다(+0x1E8 == 0이면 연다) */
static void __fastcall toggle_hook(uint8_t *functor, void **arg)
{
    __try {
        uint8_t *layout = functor ? *(uint8_t **)(functor + 8) : NULL;
        if (layout && arg && *arg) {
            *g_rows = *g_rows >= ROWS_MAX ? ROWS_MIN : ROWS_MAX;
            settings_set_int(L"log", L"rows", *g_rows);
            if (*(void **)(layout + L_LIST) || *(int *)(layout + L_EXPANDED))
                CALL(LayoutFn, FN_CLOSE)(layout);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    ((void(__fastcall *)(void *, void **))RVA(FN_TOGGLE))(functor, arg);
}

/* 상수 19 대신 N: 중계 코드 두 개와 N 칸을 한 페이지에 */
static int build_row_stubs(Patch p[2])
{
    uint8_t *s = near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!s)
        return 0;
    uint8_t *a = s, *b = s + 0x40, *cell = s + 0x80;
    uint64_t ra = (uint64_t)RVA(RET_ROWS_A), rb = (uint64_t)RVA(RET_ROWS_B);
    /* a: mov ecx, [rip+cell]; jmp [rip] → 0x66B489 */
    a[0] = 0x8B, a[1] = 0x0D;
    *(int32_t *)(a + 2) = (int32_t)(cell - (a + 6));
    a[6] = 0xFF, a[7] = 0x25, *(int32_t *)(a + 8) = 0;
    memcpy(a + 12, &ra, 8);
    /* b: mov eax, [rip+cell]; cmp esi, eax; cmovg esi, eax; jmp [rip] → 0x66B549 */
    b[0] = 0x8B, b[1] = 0x05;
    *(int32_t *)(b + 2) = (int32_t)(cell - (b + 6));
    b[6] = 0x39, b[7] = 0xC6;
    b[8] = 0x0F, b[9] = 0x4F, b[10] = 0xF0;
    b[11] = 0xFF, b[12] = 0x25, *(int32_t *)(b + 13) = 0;
    memcpy(b + 17, &rb, 8);
    g_rows = (volatile int *)cell;
    *g_rows = clamp_rows(settings_get_int(L"log", L"rows", ROWS_MIN));
    FlushInstructionCache(GetCurrentProcess(), s, 0x100);

    static const uint8_t ORIG_A[] = {0x41, 0x8D, 0x4C, 0x24, 0x13};
    static const uint8_t ORIG_B[] = {0x83, 0xFE, 0x13, 0xB8, 0x13, 0x00, 0x00, 0x00, 0x0F, 0x4F, 0xF0};
    struct { uint32_t site; const uint8_t *orig; int len; uint8_t *to; } J[2] = {
        {SITE_ROWS_A, ORIG_A, sizeof ORIG_A, a}, {SITE_ROWS_B, ORIG_B, sizeof ORIG_B, b}};
    for (int i = 0; i < 2; i++) {
        int64_t rel = (int64_t)(J[i].to - RVA(J[i].site + 5));
        if (rel != (int32_t)rel)
            return 0;
        p[i].rva = J[i].site;
        p[i].len = (uint8_t)J[i].len;
        memcpy(p[i].orig, J[i].orig, J[i].len);
        memset(p[i].repl, 0x90, J[i].len);
        p[i].repl[0] = 0xE9;
        int32_t r32 = (int32_t)rel;
        memcpy(p[i].repl + 1, &r32, 4);
    }
    return 1;
}

int feat_logwnd_init(void)
{
    Patch p[6];
    int ok = build_row_stubs(p);
    if (ok) {
        p[2].rva = SITE_SCROLLBAR_JBE;
        p[2].len = 1;
        p[2].orig[0] = 0x76;
        p[2].repl[0] = 0xEB;
        ok = patch_call(&p[3], SITE_OPEN_REFRESH, FN_OPEN, (void *)open_hook) &&
             patch_call(&p[4], SITE_OPEN_TICK, FN_OPEN, (void *)open_hook) &&
             patch_call(&p[5], SITE_OPEN_TOGGLE, FN_OPEN, (void *)open_hook) && patch_apply("로그 창", p, 6);
    }
    if (ok)
        ok = vtable_swap(VT_LAYOUT_TICK, FN_TICK, (void *)tick_hook) &&
             vtable_swap(VT_TOGGLE, FN_TOGGLE, (void *)toggle_hook) &&
             vtable_swap(VT_SETCURSOR, FN_SETCURSOR, (void *)setcursor_hook);
    if (!ok)
        plog("[로그 창] 연결 실패");
    return ok;
}
