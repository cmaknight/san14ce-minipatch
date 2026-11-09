/* 그룹 1: 숙련 모드 보강. 기능 설명은 docs/01-expert.md.

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
4) 무장경험획득(칸 24) 최소 100 → 0, 자신/타세력 군정획득(칸 22·23) 최소 10 → 0.
   범위 표 사본만 고치고, 안내줄을 0 설명을 넣은 3줄 문구로 바꾼다.
   자신/타세력 인접지역효과(칸 4·5) 범위 50~200 → 10~500. 안내줄 첫 줄 끝에 지금 값의 완전 점유 지역 1곳당 보너스
   "(현재 1곳 당 x.xx%)"(k × 값 / 100, 타세력 k는 고른 난이도의 4·16·24)를 붙이고, 슬라이더 값 쓰기(0x660E3E)마다 다시 등록한다.
5) 수동 전법(칸 63): 탭 1의 제안(칸 46) 위에 켬/끔 행을 끼운다(제안 항목 복사, 값 0 켬 / 1 끔).
   - 값은 world_extra 3번 바이트(0 = 패치 전 세이브: 원본대로, 1 켬, 2 끔).
   - 지휘 잠금은 메인 화면 레이아웃(CMainJumpLayout)을 만들 때 난이도(0x3E9E60) == 4(궁극)인지로 정한다(0x884CBF).
     그 호출만 감싸 숙련 모드에서 켬이면 궁극이 아닌 값을, 끔이면 궁극을 돌려준다.
   - 설정 화면에서 난이도를 바꾸면(0x65EDD0 안의 쓰기 0x65EDFA) 7)처럼 통상 값으로 맞추고 탭을 다시 채운다.
6) 명령서 배율: 자신/타세력 군정획득(칸 22·23) 뒤에 "자신/타세력 명령서 획득" 두 행(칸 64·65, 50~1000)을
   끼운다. 행·범위 표·값 읽기/쓰기·안내줄·슬롯·확인 화면은 1)~2)와 같은 장치를 쓴다.
   - 값은 월드 +0x17A8 u16 2개(world_cmd, 0이면 100). 쓰는 때는 2)와 같다.
   - 적용: 명령서 재계산 0x2593E0 안 군단별 계산 0x267410의 유일한 호출(0x25952C)을 감싸 결과 × %/100(반올림)을
     CMD_CAP(100)으로 자른다(배율이 100%여도). 원래 코드가 그 뒤 0~200으로 자르고 군단 +0x14에 덮어쓴다(이월 없음). 숙련 모드이고 군단 세력(+0x10)이 플레이어
     세력(0x235150)이면 자신, 아니면 타세력 배율. 위임 군단은 자신세력이다.
   - 같은 호출에서 +0x16AC~+0x17A7이 모두 0인지 보고, 아니면 한 번 로그를 남긴다(저장 칸 안전 확인용).
7) 난이도를 바꿀 때 통상 모드 값으로(1.0.2 정적 분석으로 칸마다 통상 모드 동작과 맞춤):
   - 범위 표 사본의 난이도별 표준값(슬라이더 "표준" 눈금, 0x5FEE10·행별 표준 되돌리기 0x638B00이 읽음)을 통상 값으로 고친다:
     칸 22 자신 군정획득 200,150,100,100,60 → 100×5, 칸 33 자신 병사 수입 궁극 100 → 60, 칸 36 타세력 병사 수입
     특급·궁극 150·200 → 100. 칸 7 부상병 발생 특급·궁극 80은 그대로(통상은 플레이어 부대 부상 비율만 −20점이라 같은 값이
     없고, 게임 표의 근사를 둔다).
   - 난이도 라디오 값이 바뀌면 숫자 칸 0~42를 고친 표의 그 난이도 값으로 쓰고, 제안(46)·적부대 정보(52)는 궁극이면 1(없음·
     일부) 아니면 0, 우리 칸 61·62·64·65는 100, 수동 전법은 궁극이면 끔 아니면 켬으로 쓴다.
     통상에서도 난이도와 무관한 사용자 선택인 다른 열거형 칸(역사이벤트·수명·여성무장 등)은 그대로 둔다.
*/
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
    VT_SENIOR_LIST = 0x141EBA8,
    SLOT_SET_COUNT = VT_SENIOR_LIST + 53 * 8, FN_SET_COUNT = 0x875060,
    FN_ALLOC = 0x3F9390, FN_FREE = 0x3F93A0,   /* 게임 배열 할당/해제(0x466220과 같은 것) */
    SITE_READ_JA = 0x3EC923, READ_RET = 0x3ECBDF, /* 0x3EA400 안의 ja(칸 > 0x3C) */
    SITE_WRITE_JA = 0x3F2203, WRITE_RET = 0x3F24FB,
    RANGE_TABLE = 0x1403650, RANGE_N = 61, ADJ_MIN = 10, ADJ_MAX = 500,
    IDX_ADJ_SELF = 4, IDX_ADJ_OTHER = 5,         /* 자신/타세력 인접지역효과(#12·#13) */
    SITE_SLIDER_WRITE = 0x660E3E,                /* 슬라이더 이벤트의 값 쓰기(jmp 0x3F2200(사본, [항목+0x88], 0, [항목+0x80])) */
    ADJ_K_BASE = 4, ADJ_K_SPECIAL = 16, ADJ_K_ULTIMATE = 24, DIFF_SPECIAL = 3, /* 1.0.2 0x2B2A90: 타세력 k는 특급 16·궁극 24 */
    IDX_XP = 24, IDX_INTEL_SELF = 22, IDX_INTEL_OTHER = 23, /* 무장경험획득, 자신/타세력 군정획득 */
    IDX_DIFF = 0x2B, DIFF_ULTIMATE = 4,          /* 숙련 난이도 칸(0 초급 ~ 4 궁극) */
    IDX_PROPOSAL = 0x2E,                         /* 제안(라디오, 0 켬 / 1 끔). 이 칸 앞에 수동 전법을 넣는다 */
    IDX_MANUAL = 63, MANUAL_ON = 0, MANUAL_OFF = 1,
    FN_READ = 0x3EC920, FN_WRITE = 0x3F2200,
    SITE_RADIO_WRITE = 0x66149A,                 /* 라디오 이벤트 0x65EDD0의 값 쓰기 */
    FN_TAB_FILL = 0x658DF0,                      /* (레이아웃) 현재 탭 다시 채우기(불러오기·초기화 버튼과 같은 것) */
    SITE_JUMP_DIFF = 0x886FBF, FN_DIFFICULTY = 0x3EC370, /* CMainJumpLayout 생성: 궁극이면 지휘 잠금 */
    SITE_ROW_SETUP = 0x5BBA9E, FN_ROW_SETUP = 0x643720,
    FN_HELP_TEXT = 0x89D3F0,                    /* (관리자, 위젯, wchar_t*, 0) */
    SITE_NEW_GAME = 0x3379C7, SITE_WORLD_CTOR = 0x31CC06, FN_NEW_GAME = 0x327C30,
    FN_POWER = 0x2AAF30,
    FN_VALID = 0x331780, FN_UNIT_FORCE = 0x22E690, FN_IS_PLAYER = 0x235150,
    FN_SETTINGS = 0x3EC1A0, FN_IS_EXPERT = 0x3ED810,
    ROOT_FORCES = 0xDF30,
    IDX_ANCHOR = 5,                             /* 이 칸(타세력 인접효과) 뒤에 넣는다 */
    IDX_SELF = 61, IDX_OTHER = 62,
    IDX_CMD_SELF = 64, IDX_CMD_OTHER = 65, IDX_CMD_ANCHOR = IDX_INTEL_OTHER, /* 이 칸(타세력 군정획득) 뒤에 넣는다 */
    CMD_MIN = 50, CMD_MAX = 1000,
    CMD_CAP = 100, /* 군단당 명령서 상한(원작 200) */
    SITE_CMD_CALC = 0x25952C, FN_CMD_CALC = 0x267410, /* 재계산 0x2593E0 안 군단별 명령서 계산(유일한 호출) */
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

/* 칸 22·23 안내줄(원래 메시지 10613·10614: 10 = 10%, 300 = 300%) */
static const wchar_t *const INTEL_HELP[2] = {
    L"자신세력의 군정획득량을 변경합니다.\n"
    L"0으로 설정하면 군정을 획득하지 않습니다.\n"
    L"300으로 설정하면 군정획득량은 표준치의 300%가 됩니다.",
    L"타세력의 군정획득량을 변경합니다.\n"
    L"0으로 설정하면 군정을 획득하지 않습니다.\n"
    L"300으로 설정하면 군정획득량은 표준치의 300%가 됩니다.",
};

/* 칸 4·5 안내줄(원래 메시지 10566·10567: 50 = 50%, 200 = 200%). 범위를 10~500으로 바꿔 문구도 맞추고, 첫 줄 끝에
   지금 값으로 계산한 완전 점유 지역 1곳당 보너스(k × 값 / 100 %)를 붙인다. 타세력 k는 고른 난이도를 따른다(1.0.2) */
static const wchar_t *const ADJ_HELP[2][2] = {
    {L"자신세력의 인접지역효과의 효과량을 변경합니다.",
     L"10으로 설정하면 인접지역효과는 표준치의 10%가 됩니다.\n"
     L"500으로 설정하면 인접지역효과는 표준치의 500%가 됩니다."},
    {L"타세력의 인접지역효과의 효과량을 변경합니다.",
     L"10으로 설정하면 인접지역효과는 선택한 난이도 표준치의 10%가 됩니다.\n"
     L"500으로 설정하면 인접지역효과는 선택한 난이도 표준치의 500%가 됩니다."},
};

static const wchar_t MANUAL_LABEL[] = L"수동 전법";
static const wchar_t MANUAL_HELP[] = L"「지휘」로 전법을 직접 사용할 수 있는지를 변경합니다.\n"
                                     L"끄면 난이도와 관계없이「지휘」를 사용할 수 없습니다.\n"
                                     L"난이도를「궁극」으로 바꾸면 자동으로 꺼집니다.";

static const wchar_t *const CMD_LABEL[2] = {L"자신세력 명령서 획득", L"타세력 명령서 획득"};
static const wchar_t *const CMD_HELP[2] = {
    L"자신세력 군단(위임 군단 포함)이 매 페이즈 받는 명령서 수를 변경합니다.\n"
    L"100이면 표준치, 1000이면 표준치의 1000%가 됩니다.\n"
    L"군단당 명령서는 최대 100개까지만 받습니다.",
    L"타세력 군단이 매 페이즈 받는 명령서 수를 변경합니다.\n"
    L"100이면 표준치, 1000이면 표준치의 1000%가 됩니다.\n"
    L"군단당 명령서는 최대 100개까지만 받습니다.",
};

static int g_on_base, g_on_cmd;      /* 부대능력·범위·수동 전법 등 / 명령서 배율. 1번에서는 둘 다 켠다 */
static int g_pend[2] = {100, 100}; /* 설정 화면에서 정한 값(새 게임 시작 때 월드에 씀) */
static int g_manual = MANUAL_ON;    /* 수동 전법(라디오 값) */
static int g_cmd[2] = {100, 100};   /* 명령서 배율 */
static uint8_t g_item[5][0x60];      /* 우리 행 항목(게임 항목 0x60 B와 같은 배치): 칸 61, 62, 63, 64, 65 */
static void *g_work_cfg; /* 설정 작업 사본(레이아웃 +0x1C8). 화면을 열 때의 불러오기·초기화, 라디오·슬라이더 쓰기에서 얻는다 */

/* 행 위젯은 스크롤 때 다른 칸을 맡으므로 행마다 지금 맡은 칸을 둔다(탭을 채울 때 비운다) */
#define ROW_MAX 32
static struct {
    uint8_t *row;
    void *mgr;
    int idx;
} g_rows[ROW_MAX];

static int idx_of(int idx) { return idx == IDX_SELF ? 0 : idx == IDX_OTHER ? 1 : -1; }
static int cmd_of(int idx) { return idx == IDX_CMD_SELF ? 0 : idx == IDX_CMD_OTHER ? 1 : -1; }
static int ours(int idx) { return idx_of(idx) >= 0 || idx == IDX_MANUAL || cmd_of(idx) >= 0; }

static int read_cfg(void *cfg, int idx) { return ((int(__fastcall *)(void *, int, int))RVA(FN_READ))(cfg, idx, 0); }
static int manual_default(void *cfg) { return read_cfg(cfg, IDX_DIFF) == DIFF_ULTIMATE ? MANUAL_OFF : MANUAL_ON; }

/* 월드(기준 주소)에 켠 그룹의 값만 쓴다 */
static void store(uint8_t *world)
{
    if (!world)
        return;
    uint8_t *x = world + 0x17AC;
    if (g_on_base) {
        x[WX_BASE_SELF] = (uint8_t)(g_pend[0] == 100 ? 0 : g_pend[0]);
        x[WX_BASE_OTHER] = (uint8_t)(g_pend[1] == 100 ? 0 : g_pend[1]);
        x[WX_MANUAL] = (uint8_t)(g_manual + 1);
    }
    if (g_on_cmd) {
        uint16_t *c = (uint16_t *)(world + WORLD_CMD);
        c[0] = (uint16_t)(g_cmd[0] == 100 ? 0 : g_cmd[0]);
        c[1] = (uint16_t)(g_cmd[1] == 100 ? 0 : g_cmd[1]);
    }
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------- 값 읽기/쓰기 (0x3EA400 / 0x3EFCE0의 범위 밖 분기) ---------- */
static int __fastcall read_hook(void *cfg, int idx, int sub)
{
    (void)cfg;
    int i = idx_of(idx), c = cmd_of(idx);
    if (sub != 0)
        return 0;
    if (idx == IDX_MANUAL)
        return g_manual;
    return i >= 0 ? g_pend[i] : c >= 0 ? g_cmd[c] : 0;
}

static void __fastcall write_hook(void *cfg, int idx, int sub, int value)
{
    (void)cfg;
    int i = idx_of(idx), c = cmd_of(idx);
    if (sub != 0)
        return;
    if (idx == IDX_MANUAL) {
        g_manual = value ? MANUAL_OFF : MANUAL_ON;
        store(world_ptr());
    } else if (i >= 0) {
        int v = clamp(value, 50, 200);
        if (v != g_pend[i]) {
            g_pend[i] = v;
            store(world_ptr());
        }
    } else if (c >= 0) {
        int v = clamp(value, CMD_MIN, CMD_MAX);
        if (v != g_cmd[c]) {
            g_cmd[c] = v;
            store(world_ptr());
        }
    }
}

/* ---------- 행 끼워 넣기 ---------- */
static uint64_t __fastcall set_count_hook(uint8_t *list, void *n, void *c, void *d)
{
    int cnt = (int)(intptr_t)n;
    memset(g_rows, 0, sizeof g_rows); /* 탭을 새로 채울 때마다 행 설정이 다시 돈다. 닫힌 화면의 행을 남기지 않는다 */
    __try {
        /* 이 시점의 +0x160은 아직 이전 개수다. 배열(+0x158)은 이미 새 탭의 cnt개다(0x466220). */
        /* 탭 2 칸 5 뒤에 61·62, 탭 1 칸 46(제안) 앞에 63, 칸 23(타세력 군정획득) 뒤에 64·65 */
        void **items = *(void ***)(list + 0x158);
        int at = -1, at_prop = -1, at_cmd = -1, have = 0;
        for (int i = 0; items && i < cnt && cnt < 200; i++) {
            int idx = items[i] ? *(int *)items[i] : -1;
            if (idx == IDX_ANCHOR && g_on_base)
                at = i;
            if (idx == IDX_PROPOSAL && g_on_base)
                at_prop = i;
            if (idx == IDX_CMD_ANCHOR && g_on_cmd)
                at_cmd = i;
            if (ours(idx))
                have = 1;
        }
        int add = (at >= 0 ? 2 : 0) + (at_prop >= 0 ? 1 : 0) + (at_cmd >= 0 ? 2 : 0);
        if (add && !have) {
            void **arr = ((void **(__fastcall *)(size_t))RVA(FN_ALLOC))((size_t)(cnt + add) * 8);
            if (arr) {
                for (int k = 0; k < 2 && at >= 0; k++) {
                    memcpy(g_item[k], items[at], 0x60);
                    *(int *)(g_item[k] + 0) = k ? IDX_OTHER : IDX_SELF;
                    *(int *)(g_item[k] + 4) = g_pend[k];
                    *(const wchar_t **)(g_item[k] + 0x18) = LABEL[k];
                }
                if (at_prop >= 0) {
                    memcpy(g_item[2], items[at_prop], 0x60);
                    *(int *)(g_item[2] + 0) = IDX_MANUAL;
                    *(int *)(g_item[2] + 4) = g_manual;
                    *(const wchar_t **)(g_item[2] + 0x18) = MANUAL_LABEL;
                }
                for (int k = 0; k < 2 && at_cmd >= 0; k++) {
                    memcpy(g_item[3 + k], items[at_cmd], 0x60);
                    *(int *)(g_item[3 + k] + 0) = k ? IDX_CMD_OTHER : IDX_CMD_SELF;
                    *(int *)(g_item[3 + k] + 4) = g_cmd[k];
                    *(const wchar_t **)(g_item[3 + k] + 0x18) = CMD_LABEL[k];
                }
                int j = 0;
                for (int i = 0; i < cnt; i++) {
                    if (i == at_prop)
                        arr[j++] = g_item[2];
                    arr[j++] = items[i];
                    if (i == at) {
                        arr[j++] = g_item[0];
                        arr[j++] = g_item[1];
                    }
                    if (i == at_cmd) {
                        arr[j++] = g_item[3];
                        arr[j++] = g_item[4];
                    }
                }
                ((void(__fastcall *)(void *))RVA(FN_FREE))(items);
                *(void ***)(list + 0x158) = arr;
                cnt += add;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return CALL(Fn4, FN_SET_COUNT)(list, (void *)(intptr_t)cnt, c, d);
}

/* 행 위젯의 안내줄 등록(0x89D3F0은 글자를 복사해 위젯별 표에 덮어쓴다) */
static void help_register(uint8_t *row, void *mgr, const wchar_t *text)
{
    typedef void(__fastcall * HelpFn)(void *, void *, const wchar_t *, int);
    uint8_t *slider = *(uint8_t **)(row + 0x190);
    void *w[5] = {slider ? *(void **)(slider + 0x1C0) : NULL, slider ? *(void **)(slider + 0x1C8) : NULL,
                  *(void **)(row + 0x178), *(void **)(row + 0x198), *(void **)(row + 0x1A0)};
    for (int k = 0; k < 5; k++)
        if (w[k])
            CALL(HelpFn, FN_HELP_TEXT)(mgr, w[k], text, 0);
}

static void row_note(uint8_t *row, void *mgr, int idx)
{
    int at = -1;
    for (int k = 0; k < ROW_MAX; k++) {
        if (g_rows[k].row == row) {
            at = k;
            break;
        }
        if (at < 0 && !g_rows[k].row)
            at = k;
    }
    if (at < 0)
        at = 0; /* 넘치면 맨 앞을 덮는다(화면 행은 9개) */
    g_rows[at].row = row, g_rows[at].mgr = mgr, g_rows[at].idx = idx;
}

/* 칸 4·5 안내줄: 원래 문구(범위 10~500) + 첫 줄 끝에 "(현재 1곳 당 x.xx%)" */
static void adj_help(uint8_t *row, void *mgr, int idx, int value)
{
    int i = idx - IDX_ADJ_SELF, k = ADJ_K_BASE;
    if (i) {
        int d = -1;
        __try {
            d = g_work_cfg ? read_cfg(g_work_cfg, IDX_DIFF) : -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            d = -1;
        }
        k = d >= DIFF_ULTIMATE ? ADJ_K_ULTIMATE : d == DIFF_SPECIAL ? ADJ_K_SPECIAL : ADJ_K_BASE; /* 0x2B2D01 */
    }
    int bp = k * value; /* 1/100 % 단위 */
    wchar_t text[400];
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%s (현재 1곳 당 %d.%02d%%)\n%s", ADJ_HELP[i][0], bp / 100, bp % 100,
                 ADJ_HELP[i][1]);
    help_register(row, mgr, text);
}

/* 행 설정 뒤 우리 행의 안내줄 문구(게임은 항목 +0x48의 메시지 ID로 등록한다) */
static void __fastcall row_setup_hook(uint8_t *row, uint8_t *item, void *mgr, int diff, int tab)
{
    CALL(RowSetupFn, FN_ROW_SETUP)(row, item, mgr, diff, tab);
    __try {
        int idx = item ? *(int *)item : -1, i = idx_of(idx), c = cmd_of(idx);
        if (row)
            row_note(row, mgr, idx);
        const wchar_t *text = i >= 0 ? HELP[i] : idx == IDX_MANUAL ? MANUAL_HELP : c >= 0 ? CMD_HELP[c] : NULL;
        /* +0xC가 0이 아니면 게임은 DLC 없음 문구를 쓴다(0x641115) */
        if (g_on_base && !*(int *)(item + 0xC)) {
            if (idx == IDX_XP)
                text = XP_HELP;
            else if (idx == IDX_INTEL_SELF || idx == IDX_INTEL_OTHER)
                text = INTEL_HELP[idx - IDX_INTEL_SELF];
            else if ((idx == IDX_ADJ_SELF || idx == IDX_ADJ_OTHER) && mgr) {
                adj_help(row, mgr, idx, *(int *)(item + 4)); /* 항목 +4는 행 설정 전에 사본에서 다시 읽은 값 */
                return;
            }
        }
        if (!text || !mgr)
            return;
        help_register(row, mgr, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* 슬라이더 값 쓰기(0x660E20 → jmp 0x3F2200). 칸 4·5면 그 칸을 맡은 행의 안내줄을 새 값으로 다시 등록한다 */
static void __fastcall slider_write_hook(void *cfg, int idx, int sub, int value)
{
    typedef void(__fastcall * WriteFn)(void *, int, int, int);
    CALL(WriteFn, FN_WRITE)(cfg, idx, sub, value);
    g_work_cfg = cfg;
    if ((idx != IDX_ADJ_SELF && idx != IDX_ADJ_OTHER) || sub != 0)
        return;
    __try {
        int v = read_cfg(cfg, idx);
        for (int k = 0; k < ROW_MAX; k++)
            if (g_rows[k].row && g_rows[k].mgr && g_rows[k].idx == idx)
                adj_help(g_rows[k].row, g_rows[k].mgr, idx, v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* ---------- 새 게임: 월드에 기록 ---------- */
static uint64_t __fastcall new_game_hook(void *world, void *a, void *b, void *c)
{
    uint64_t r = CALL(Fn4, FN_NEW_GAME)(world, a, b, c);
    if (world)
        store((uint8_t *)world);
    return r;
}

static uint8_t *g_tab; /* 범위 표 사본(칸당 int 7: 최소, 최대, 난이도 0~4 표준값) */

/* ---------- 난이도를 바꿀 때 통상 값으로 ---------- */
enum { IDX_NUM_LAST = 42, IDX_ENEMY_INFO = 0x34 };
static void apply_difficulty(uint8_t *cfg, int d)
{
    typedef void(__fastcall * WriteFn)(void *, int, int, int);
    WriteFn wr = CALL(WriteFn, FN_WRITE);
    if (g_tab && d >= 0 && d <= DIFF_ULTIMATE)
        for (int k = 0; k <= IDX_NUM_LAST; k++)
            wr(cfg, k, 0, ((int *)(g_tab + k * 0x1C))[2 + d]);
    int ult = d == DIFF_ULTIMATE;
    wr(cfg, IDX_PROPOSAL, 0, ult);   /* 0 있음, 1 없음 */
    wr(cfg, IDX_ENEMY_INFO, 0, ult); /* 0 전부, 1 일부 */
    g_manual = ult ? MANUAL_OFF : MANUAL_ON;
    g_pend[0] = g_pend[1] = 100;
    if (g_on_cmd)
        g_cmd[0] = g_cmd[1] = 100;
    store(world_ptr());
}

/* ---------- 수동 전법 ---------- */
/* 라디오 값 쓰기(0x65EDD0 → 0x3EFCE0(사본 = 레이아웃+0x1C8, 칸, 0, 값)). 난이도가 바뀌면 모든 칸을 그 난이도의 통상 값으로
   쓰고(apply_difficulty) 탭을 다시 채워 화면에 보인다(행 값은 행 설정 전 0x5B915E가 사본에서 다시 읽는다). 바꾼 뒤 직접 고칠 수 있다.
   이 호출 뒤 게임이 0x5FEE10으로 슬라이더 표준 눈금을 새 난이도로 옮긴다. */
static void __fastcall radio_write_hook(uint8_t *cfg, int idx, int sub, int value)
{
    typedef void(__fastcall * WriteFn)(void *, int, int, int);
    int old = idx == IDX_DIFF ? read_cfg(cfg, IDX_DIFF) : -1;
    CALL(WriteFn, FN_WRITE)(cfg, idx, sub, value);
    g_work_cfg = cfg;
    if (idx != IDX_DIFF || sub != 0 || value == old)
        return;
    __try {
        apply_difficulty(cfg, value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        ((void(__fastcall *)(void *))RVA(FN_TAB_FILL))(cfg - 0x1C8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* 지휘 잠금 판정(메인 화면 레이아웃 생성 0x884CBF: 난이도 == 4이면 잠금). 숙련 모드이고 세이브에 값이 있을 때만 바꾼다 */
static int __fastcall jump_difficulty_hook(void *cfg)
{
    int d = ((int(__fastcall *)(void *))RVA(FN_DIFFICULTY))(cfg);
    __try {
        uint8_t *x = world_extra();
        if (x && x[WX_MANUAL] && cfg && ((int(__fastcall *)(void *))RVA(FN_IS_EXPERT))(cfg)) {
            if (x[WX_MANUAL] == MANUAL_OFF + 1)
                return DIFF_ULTIMATE;
            if (d == DIFF_ULTIMATE)
                return DIFF_ULTIMATE - 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return d;
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

/* ---------- 명령서 배율 ---------- */
/* +0x16AC~+0x17A7은 게임이 쓰지 않는 칸으로 본다(정적 분석). 0이 아닌 값이 보이면 실행 중 접근이 있다는 뜻이라 한 번 남긴다 */
static void check_block(uint8_t *world)
{
    static int warned;
    if (warned)
        return;
    for (int o = 0x16AC; o < WORLD_CMD; o++) {
        if (world[o]) {
            warned = 1;
            plog("[명령서 배율] 월드 +0x%X = %d: 저장 칸 앞 덩어리에 값이 있음. 저장 칸 재검토 필요", o, world[o]);
            return;
        }
    }
}

static int cmd_pct(uint8_t *corps)
{
    uint8_t *world = world_ptr();
    if (!world || !corps)
        return 100;
    check_block(world);
    uint16_t *c = (uint16_t *)(world + WORLD_CMD);
    if (!c[0] && !c[1])
        return 100;
    int fid = corps[0x10]; /* 0x267410과 같은 세력 번호(≤ 0x33) */
    uint8_t *root = game_root();
    if (!root || fid > 0x33)
        return 100;
    void *force = *(void **)(root + ROOT_FORCES + (size_t)fid * 8);
    int self = force && ((int(__fastcall *)(void *))RVA(FN_VALID))(force) &&
               ((int(__fastcall *)(void *))RVA(FN_IS_PLAYER))(force);
    int v = c[self ? 0 : 1];
    return v ? v : 100;
}

/* 숙련 모드일 때만: 배율을 곱하고 CMD_CAP으로 자른다(원래 코드가 뒤에서 0~200으로 한 번 더 자른다) */
static int __fastcall cmd_calc_hook(uint8_t *corps)
{
    int r = ((int(__fastcall *)(void *))RVA(FN_CMD_CALC))(corps);
    __try {
        void *cfg = ((void *(__fastcall *)(void))RVA(FN_SETTINGS))();
        if (r > 0 && cfg && ((int(__fastcall *)(void *))RVA(FN_IS_EXPERT))(cfg)) {
            int pct = cmd_pct(corps);
            if (pct != 100)
                r = (r * pct + 50) / 100;
            if (r > CMD_CAP)
                r = CMD_CAP;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

static int setup_cmd(void)
{
    Patch p;
    return patch_call(&p, SITE_CMD_CALC, FN_CMD_CALC, (void *)cmd_calc_hook) && patch_apply("명령서 배율", &p, 1);
}

static const uint32_t POWER_CALLS[] = {
    0x87DBD, 0x87E04, 0x87E70, 0xB6F62, 0xBC382, 0xC026E, 0xC981D, 0xC9912, 0xCE9EE, 0xCEB39, 0x16B13F,
    0x192D65, 0x342F14, 0x343027, 0x7FC971, 0x7FC9EC, 0x916332, 0x916399, 0x925E3E, 0x925E55, 0x929AE1, 0x929B45,
};
static const uint32_t POWER_JMP = 0xC00DB; /* 꼬리 호출 jmp */

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

/* 그룹 1 범위 바꾸기(사본에서) */
static void base_ranges(uint8_t *tab)
{
    /* 자신/타세력 인접지역효과(칸 4·5, #12·#13) 50~200 → 10~500(2026-10-21 요청 최대 2000, 2026-11-05 500, 2026-11-06 10~300, 같은 날 10~500).
       범위 표를 읽는 곳은 setup_screen의 lea 5곳뿐 */
    for (int k = IDX_ADJ_SELF; k <= IDX_ADJ_OTHER; k++) {
        int *r = (int *)(tab + k * 0x1C);
        if (r[0] == 50 && r[1] == 200)
            r[0] = ADJ_MIN, r[1] = ADJ_MAX;
        else
            plog("칸 %d 범위가 예상(50~200)과 다름: %d~%d. 그대로 둠", k, r[0], r[1]);
    }
    /* 최소값 0으로: 무장경험획득(원래 100), 자신/타세력 군정획득(원래 10). 게임은 값 × 설정 / 100만 한다 */
    static const struct { int idx, min; } ZERO[] = {{IDX_XP, 100}, {IDX_INTEL_SELF, 10}, {IDX_INTEL_OTHER, 10}};
    for (int k = 0; k < 3; k++) {
        int *r = (int *)(tab + ZERO[k].idx * 0x1C);
        if (r[0] == ZERO[k].min)
            r[0] = 0;
        else
            plog("칸 %d 최소값이 예상(%d)과 다름: %d. 그대로 둠", ZERO[k].idx, ZERO[k].min, r[0]);
    }
    /* 난이도별 표준값을 통상 모드 값으로. 원래 값일 때만 바꾼다 */
    static const struct { int idx, was[5], now[5]; } STD[] = {
        {IDX_INTEL_SELF, {200, 150, 100, 100, 60}, {100, 100, 100, 100, 100}}, /* 통상 군정은 난이도 보정 없음 */
        {33, {200, 150, 100, 100, 100}, {200, 150, 100, 100, 60}},             /* 자신 병사 수입: 통상 궁극 60(0x2BFB61) */
        {36, {100, 100, 100, 150, 200}, {100, 100, 100, 100, 100}},            /* 타세력 병사 수입: 통상 100(0x2BFC0C) */
    };
    for (int k = 0; k < 3; k++) {
        int *r = (int *)(tab + STD[k].idx * 0x1C) + 2;
        if (!memcmp(r, STD[k].was, sizeof STD[k].was))
            memcpy(r, STD[k].now, sizeof STD[k].now);
        else
            plog("칸 %d 표준값이 예상과 다름: %d,%d,%d,%d,%d. 그대로 둠", STD[k].idx, r[0], r[1], r[2], r[3], r[4]);
    }
}

static int setup_screen(void)
{
    /* 범위 표 66칸 사본(칸 61~65는 우리 행) */
    uint8_t *tab = near_alloc(0x1000, PAGE_READWRITE);
    if (!tab)
        return 0;
    memcpy(tab, RVA(RANGE_TABLE), RANGE_N * 0x1C);
    static const int row[7] = {50, 200, 100, 100, 100, 100, 100};
    static const int cmd_row[7] = {CMD_MIN, CMD_MAX, 100, 100, 100, 100, 100};
    memcpy(tab + IDX_SELF * 0x1C, row, sizeof row);
    memcpy(tab + IDX_OTHER * 0x1C, row, sizeof row);
    memcpy(tab + IDX_CMD_SELF * 0x1C, cmd_row, sizeof cmd_row);
    memcpy(tab + IDX_CMD_OTHER * 0x1C, cmd_row, sizeof cmd_row);
    /* 칸 63(라디오)은 표를 쓰지 않지만 읽혀도 무해한 값을 둔다 */
    static const int radio[7] = {0, 1, 0, 0, 0, 0, 1};
    memcpy(tab + IDX_MANUAL * 0x1C, radio, sizeof radio);
    if (g_on_base)
        base_ranges(tab);
    g_tab = tab;

    static const uint32_t LEA[] = {0x5FEE35, 0x629C12, 0x638B5E, 0x638C41, 0x6438B4};
    Patch p[5 + 5];
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
    if (g_on_base && (!patch_call(&p[n++], SITE_RADIO_WRITE, FN_WRITE, (void *)radio_write_hook) ||
                      !patch_branch(&p[n++], SITE_SLIDER_WRITE, 1, FN_WRITE, (void *)slider_write_hook)))
        return 0;
    if (!patch_apply("숙련 설정 행", p, n))
        return 0;
    if (!vtable_swap(SLOT_SET_COUNT, FN_SET_COUNT, (void *)set_count_hook))
        return 0;
    Patch g[2];
    if (!patch_call(&g[0], SITE_NEW_GAME, FN_NEW_GAME, (void *)new_game_hook) ||
        !patch_call(&g[1], SITE_WORLD_CTOR, FN_NEW_GAME, (void *)new_game_hook) || !patch_apply("숙련 새 게임 기록", g, 2))
        return 0;
    Patch j;
    if (g_on_base &&
        (!patch_call(&j, SITE_JUMP_DIFF, FN_DIFFICULTY, (void *)jump_difficulty_hook) || !patch_apply("숙련 수동 전법", &j, 1)))
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
   불러오기 버튼 0x5F5B03/0x5F5B0F, 저장 0x6353C0·0x614542. 우리 값의 슬롯은 settings.ini [expert]에 둔다
   (slotN_self/other/manual, 명령서 slotN_cmd_self/other)
   (게임 슬롯은 설정 파일 configS에 있어 칸을 늘릴 수 없다). 저장된 값이 없으면 100. */
static void slot_key(wchar_t *k, int slot, int i)
{
    wsprintfW(k, L"slot%d_%s", slot, i ? L"other" : L"self");
}

static void __fastcall slot_copy_hook(void *cfg, int from, int to)
{
    ((void(__fastcall *)(void *, int, int))RVA(0x3F1E50))(cfg, from, to);
    g_work_cfg = cfg;
    wchar_t k[32];
    if (from == 0 && to >= 1 && to <= 5) {
        if (g_on_base) {
            for (int i = 0; i < 2; i++) {
                slot_key(k, to, i);
                settings_set_int(L"expert", k, g_pend[i]);
            }
            wsprintfW(k, L"slot%d_manual", to);
            settings_set_int(L"expert", k, g_manual);
        }
        for (int i = 0; i < 2 && g_on_cmd; i++) {
            wsprintfW(k, L"slot%d_cmd_%s", to, i ? L"other" : L"self");
            settings_set_int(L"expert", k, g_cmd[i]);
        }
    } else if (to == 0 && from >= 1 && from <= 5) {
        if (g_on_base) {
            for (int i = 0; i < 2; i++) {
                slot_key(k, from, i);
                int v = settings_get_int(L"expert", k, 100);
                g_pend[i] = v < 50 || v > 200 ? 100 : v;
            }
            /* 없으면(패치 전 슬롯) 불러온 난이도의 기본값 */
            wsprintfW(k, L"slot%d_manual", from);
            int m = settings_get_int(L"expert", k, -1);
            g_manual = m == MANUAL_ON || m == MANUAL_OFF ? m : manual_default(cfg);
        }
        for (int i = 0; i < 2 && g_on_cmd; i++) {
            wsprintfW(k, L"slot%d_cmd_%s", from, i ? L"other" : L"self");
            int v = settings_get_int(L"expert", k, 100);
            g_cmd[i] = v < CMD_MIN || v > CMD_MAX ? 100 : v;
        }
        store(world_ptr());
    }
}

static void __fastcall reset_hook(void *cfg, int a)
{
    ((void(__fastcall *)(void *, int))RVA(0x3F1500))(cfg, a);
    g_work_cfg = cfg;
    g_pend[0] = g_pend[1] = 100;
    g_cmd[0] = g_cmd[1] = 100;
    g_manual = manual_default(cfg);
    store(world_ptr());
}

/* ---------- 설정 확인 화면(0x5D1310) ----------
   60칸 항목(0x28 B: +0 종류, +4 이름 메시지 ID, +8 값 글자 wstring)을 0x58820(목록, &항목)으로 넣는다(0x5D463F).
   13번(메시지 0x888) 뒤에 우리 항목 둘을, 제안(메시지 0x1B, 열거 항목 0x59BF70) 앞에 수동 전법을 넣는다.
   이름은 행 갱신 0x5B8680이 0x30CF80(+4)로 얻으므로(0x5B86C6) 가짜 메시지 ID일 때 우리 문구를 돌려준다.
   수동 전법 값 글자는 제안과 같은 0x30B5D0(0 켬 / 1 끔)으로 얻는다. */
enum {
    MSG_ADJ_OTHER = 0x888, MSG_PROPOSAL = 0x1B, MSG_INTEL_OTHER = 0x88B, /* 칸 23 타세력 군정획득(0x5D68E5) */
    MSG_FAKE_SELF = 0x7E0001, MSG_FAKE_OTHER = 0x7E0002, MSG_FAKE_MANUAL = 0x7E0003,
    MSG_FAKE_CMD_SELF = 0x7E0004, MSG_FAKE_CMD_OTHER = 0x7E0005,
    FN_ONOFF_TEXT = 0x30D660,
};

static void __fastcall confirm_push_hook(void *list, void **pitem)
{
    typedef void(__fastcall * PushFn)(void *, void **);
    __try {
        uint8_t *it = pitem ? (uint8_t *)*pitem : NULL;
        if (g_on_base && it && *(int *)it == 0 && *(int *)(it + 4) == MSG_PROPOSAL) {
            void *n = ((void *(__fastcall *)(size_t))RVA(0x3F90E0))(0x28);
            if (n) {
                typedef void *(__fastcall * MakeEnumFn)(void *, int, int, const wchar_t *);
                const wchar_t *v = ((const wchar_t *(__fastcall *)(int))RVA(FN_ONOFF_TEXT))(g_manual);
                CALL(MakeEnumFn, 0x59E6F0)(n, 0, MSG_PROPOSAL, v);
                *(int *)((uint8_t *)n + 4) = MSG_FAKE_MANUAL;
                CALL(PushFn, 0x93C30)(list, &n);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    CALL(PushFn, 0x93C30)(list, pitem);
    __try {
        uint8_t *it = pitem ? (uint8_t *)*pitem : NULL;
        int msg = it && *(int *)it == 0 ? *(int *)(it + 4) : -1;
        const int *vals = msg == MSG_ADJ_OTHER && g_on_base ? g_pend : msg == MSG_INTEL_OTHER && g_on_cmd ? g_cmd : NULL;
        for (int i = 0; vals && i < 2; i++) {
            void *n = ((void *(__fastcall *)(size_t))RVA(0x3F90E0))(0x28);
            if (!n)
                return;
            typedef void *(__fastcall * MakeFn)(void *, int, int, int);
            CALL(MakeFn, 0x59E510)(n, 0, msg, vals[i]);
            *(int *)((uint8_t *)n + 4) = vals == g_cmd ? (i ? MSG_FAKE_CMD_OTHER : MSG_FAKE_CMD_SELF)
                                                       : (i ? MSG_FAKE_OTHER : MSG_FAKE_SELF);
            CALL(PushFn, 0x93C30)(list, &n);
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
    if (id == MSG_FAKE_MANUAL)
        return MANUAL_LABEL;
    if (id == MSG_FAKE_CMD_SELF || id == MSG_FAKE_CMD_OTHER)
        return CMD_LABEL[id - MSG_FAKE_CMD_SELF];
    return ((const wchar_t *(__fastcall *)(int))RVA(0x30F010))(id);
}

static int setup_slots(void)
{
    static const struct { uint32_t site, fn; void *hook; } S[] = {
        {0x5F33E6, 0x3F1500, (void *)reset_hook},     {0x5F8333, 0x3F1500, (void *)reset_hook},
        {0x5F33FE, 0x3F1E50, (void *)slot_copy_hook}, {0x5F833F, 0x3F1E50, (void *)slot_copy_hook},
        {0x637C70, 0x3F1E50, (void *)slot_copy_hook}, {0x616D92, 0x3F1E50, (void *)slot_copy_hook},
        {0x5D6E9F, 0x93C30, (void *)confirm_push_hook}, {0x5BAFC6, 0x30F010, (void *)msg_hook},
    };
    enum { N = sizeof S / sizeof S[0] };
    Patch p[N];
    for (int i = 0; i < N; i++)
        if (!patch_call(&p[i], S[i].site, S[i].fn, S[i].hook))
            return 0;
    return patch_apply("숙련 설정 슬롯·확인 화면", p, N);
}

int feat_expert_init(int base, int cmd)
{
    g_on_base = base, g_on_cmd = cmd;
    int ok = base ? setup_power() : 1;
    if (ok)
        ok = setup_screen();
    if (ok)
        ok = setup_slots();
    if (ok && cmd)
        ok = setup_cmd();
    if (!ok)
        plog("[숙련] 설정 행·배율 일부 실패(기본 %d, 명령서 %d)", base, cmd);
    return ok;
}
