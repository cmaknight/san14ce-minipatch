/* 그룹 10: AI 출진 개선(요격·공격·행군·수송). 기능 설명은 docs/10-intercept.md.

출진 여부(나갈지·어디를 칠지·언제 보낼지)는 원작 AI가 정하고, 이 파일은 누가·얼마나·어떤 진형으로 나갈지를 바꾼다.
AI 행동 트리 데이터(0000/bt_exce.bin)는 그대로 두고, 트리 노드의 실행 함수(vtable 슬롯 16)와 노드가 부르는 함수 호출을
감싸 결과만 바꾼다. 감싼 곳에서 예외가 나면 그 호출은 원작대로 돈다.

A. 요격 출진 판정: 조건 "칸 거리 ≤ 5" → 7칸, 출진 문턱(근접·원거리·군주 거점·난이도 근접)을 낮추고, 적 선두가 물 위면
   수상 배율, 예상 만남 칸의 인접 효과 비를 문턱에 건다. 배 성능이 충분히 앞서면(수상 우위) 문턱 0.
B. 요격 동반 출진: 후보 무장을 종합 능력 A(통솔 0.76 · 무력 0.12 · 지력 0.12)로 줄 세워 1위의 GROUP_PCT% 이상을 그룹으로,
   병사를 STEP씩 돌아가며 나눠 그룹 전력이 적 전력 E를 넘을 때까지 채운다(지휘 상한·가용 병사 안). 부대마다 적 선두 전력의
   LEAD_PCT% 이상(각개격파 방지), 열세 하한, 관문 수비·관문 들여보내기, 빈 관문 태수, 지원 요격 예비(자기 거점 위협만큼 남김).
C. 공격 동반 출진: 선봉을 A 순으로, 나머지는 통솔 순 그룹이 (필요 + 지원 예상)을 지휘 상한 비례로 나눠 맡는다. 바닥(필요와
   원작 예상 사이), 출발 거점의 위협 예비·지원 몫 예비, 공성 편성(호위 비), 가망 없는 공격 막기(땅 40% / 강 건넘 65%).
   그룹으로 필요를 못 채우면 원작대로.
D. 진형: 요격은 적이 여럿이면 탱커 우선(봉시는 조건부), 안행은 사격형 무장 위주. 공격은 봉시를 탱커로, 노드 전체에서
   탱커 ≥ 안행 + 공성 병기.
F. 비군사 임무에서 군주·A 상위 제외, AI 전력 추정의 통솔항 보정, 수상 보정(공격 전력·요격 문턱).
G. 행군 대열: 같은 묶음의 앞선 부대가 가장 뒤 부대를 기다린다(기다림 한도 있음, 가장 뒤가 공성 병기면 다가오는 동안 호위).
H. 전선 보강 수송: 원작 수송의 빈도·명령서는 그대로, 직전 도시 출발은 최전선으로, 후방 출발은 자기 도시 2단계 안의
   직전 도시로만 대상을 바꾸고 양을 맞춘다.

V102_STAGE 3 이상과 V102_REMOVED 연결(지역 점령 출진 규칙·트리 공격 문턱·야전 묶음 재판단 등)은 게임 1.0.2에서 쓰지 않는다.
*/
#include "common.h"
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    /* 노드 실행 함수 = vtable 슬롯 16 */
    VT_MULTI = 0x1354AE8, FN_MULTI = 0x92130,             /* CVariableMultiValueNode */
    VT_DIVIDE = 0x1353478, FN_DIVIDE = 0x86540,           /* CVariableDivideValueNode */
    VT_PLUSVAR = 0x1354D00, FN_PLUSVAR = 0x922F0,         /* CVariablePlusVariableNode */
    VT_NOINTERCEPT = 0x1352E00, FN_NOINTERCEPT = 0x85810, /* CRemoveNoInterceptPersonNode */
    VT_SORT = 0x13527B8, FN_SORT = 0x84CB0,               /* CListSortEvaluationNode */
    VT_SOLDIERS = 0x134FC30, FN_SOLDIERS = 0x7BE80,       /* CGetBattlePowerSoldierNumNode */
    SLOT_MULTI = VT_MULTI + 16 * 8, SLOT_DIVIDE = VT_DIVIDE + 16 * 8, SLOT_PLUSVAR = VT_PLUSVAR + 16 * 8,
    SLOT_NOINTERCEPT = VT_NOINTERCEPT + 16 * 8, SLOT_SORT = VT_SORT + 16 * 8, SLOT_SOLDIERS = VT_SOLDIERS + 16 * 8,
    /* 조건 디스패처 0xC2610 안의 조건 함수 호출 */
    SITE_COND_DIST = 0xC3754, FN_COND_DIST = 0xA9BF0, /* 조건 0x05 */
    SITE_COND_GTK = 0xC38F8, FN_COND_GTK = 0xA35E0,   /* 조건 0x14 */
    SITE_COND_GT = 0xC38C0, FN_COND_GT = 0xA3550,     /* 조건 0x12 (변수 > 변수) */
    SITE_ATK_END_POWER = 0x30D33, /* 공격 노드 끝 취소의 부대 전력 합(0x30275에서 처음 필요와 비교) */
    /* 블랙보드 */
    FN_BLACKBOARD = 0x1137FF0, FN_BB_GET_INT = 0xB28BB0, FN_BB_GET_OBJ = 0xB28D20, FN_BB_SET_INT = 0xB29250,
    LIST_NODES = 0x27D4140, LIST_COUNT = 0x27D4170,
    /* 무장·임시 부대(0x7AEE0과 같은 순서) */
    FN_VALID = 0x331780, FN_CAP = 0x232C00, FN_ABILITY = 0x231770 /* (무장, 0 통솔, 0xFFF) */,
    FN_VAR_HEX = 0xBFBC0 /* (형, 변수, ctx) → 칸(조건 0x05와 같음) */, FN_HEX_DIST = 0x2ACB80, FN_FOOTHOLD = 0x32E3B0, FN_HEX = 0x22F190, FN_SPECIAL = 0x234AB0,
    FN_PERSON_FORCE = 0x22E450, FN_UNIT_CTOR = 0x222250, FN_UNIT_DTOR = 0x223560, FN_UNIT_MORALE = 0x242FC0,
    FN_UNIT_FORMATION = 0x2436C0, FN_UNIT_READY = 0x242EF0, FN_UNIT_POWER = 0xB6F50,
    PERSON_MAX = 0x1770,
    /* 공격 출진 모드 함수(0x44A80, 0x46B80, 0x48CE0, 0x4BA50) 안 호출 */
    FN_ATK_SORT = 0xD4350, /* 거점 무장 목록 정렬 */
    SITE_ATK_SORT_16 = 0x45A23, SITE_ATK_SORT_9 = 0x47B25, SITE_ATK_SORT_8 = 0x49C6F, SITE_ATK_SORT_A = 0x4C935,
    FN_ATK_SPEC = 0x43A50, /* 출진 사양: +8 병사 */
    SITE_ATK_SPEC_16 = 0x462DD, SITE_ATK_SPEC_9 = 0x483D0, SITE_ATK_SPEC_8 = 0x49EAF, SITE_ATK_SPEC_A = 0x4D1EE,
    ATK_NEED = 0x84, /* 노드: 남은 필요 전력 */
    /* 진형 */
    VT_FORMEVAL = 0x13531A0, FN_FORMEVAL = 0x6EFD0, /* CSetFormationEvaluationNode */
    VT_GETFORM = 0x1350078, FN_GETFORM = 0x7EEE0,   /* CGetFormationNode */
    SLOT_FORMEVAL = VT_FORMEVAL + 16 * 8, SLOT_GETFORM = VT_GETFORM + 16 * 8,
    FN_FORM_STAT = 0x2AAE20, /* (세력, 진형, n): 1 기동, 2 공군, 3 공성, 4 파성, 5 방어 */
    FN_FORM_ID = 0x22F7A0,
    FN_ATK_TACTICS = 0xD1120, /* (무장, 진형, 사양) 전법 고르기 */
    SITE_ATK_TACTICS_SPEC = 0x43BBB, SITE_ATK_TACTICS_16A = 0x45C88, SITE_ATK_TACTICS_16B = 0x46630,
    SITE_ATK_TACTICS_9A = 0x47D6C, SITE_ATK_TACTICS_9B = 0x486E6, SITE_ATK_TACTICS_8 = 0x4A1B6,
    SITE_ATK_TACTICS_B = 0x4BBC5, SITE_ATK_TACTICS_AA = 0x4CB97, SITE_ATK_TACTICS_AB = 0x4D569,
    /* B단계 부대 생성 0x1EC890(사양, 1). 0xD0090 뒤 병사를 예비로 자르고 3000 미만·진형 비용 초과면 건너뛴 다음 */
    SITE_ATK_MAKE_16 = 0x466E0, SITE_ATK_MAKE_9 = 0x48795, SITE_ATK_MAKE_8 = 0x4A35E, SITE_ATK_MAKE_B = 0x4BC62,
    SITE_ATK_MAKE_A = 0x4D616,
    ROOT_FORMATIONS = 0x77428, /* [ROOT + 이것 + id×8] 진형 객체, id 1~20 */
    ROOT_PERSONS = 0x158,      /* [ROOT + 이것 + id×8] 무장, id 0~PERSON_MAX */
    /* 선봉 부대 생성 0x1EC890(사양, 1): A단계 호출과 그 정렬 호출의 복귀 주소(그룹 10) */
    FN_UNIT_CREATE = 0x1EDC30, SITE_ATK_VAN_16 = 0x45E22, SITE_ATK_VAN_9 = 0x47F12, SITE_ATK_VAN_A = 0x4CD2F,
    RET_ATK_SORT_16 = 0x45A28, RET_ATK_SORT_9 = 0x47B2A, RET_ATK_SORT_A = 0x4C93A,
    RET_ATK_SORT_8 = 0x49C74, /* 모드 8(0x48CE0) 정렬 복귀. 선봉 단계가 없어 판단·위협 예비만(빌드 53) */
    /* 비군사 임무 후보 목록(군단, 목록, 거르개)과 목록에서 지우기(목록, &무장) */
    FN_LIST_INSPECT = 0x6DD090, SITE_LIST_INSPECT = 0x2E134, /* 정찰 */
    FN_LIST_SEARCH = 0x6DCD20, SITE_LIST_SEARCH = 0x3DD88,   /* 탐색 */
    FN_LIST_PLOT = 0x6DD230,                                  /* 계략 공통(호출 6곳) */
    FN_LIST_REMOVE = 0x21A350,
    VT_DUAL = 0x134BC78, FN_DUAL = 0x53270,      /* CCommandExecutionDualDestructionNode */
    VT_SPYAWAY = 0x134CE30, FN_SPYAWAY = 0x54BC0, /* CCommandExecutionSpyAwayNode */
    SLOT_DUAL = VT_DUAL + 16 * 8, SLOT_SPYAWAY = VT_SPYAWAY + 16 * 8,
    ROOT_CORPS = 0xE0D0, ROOT_FORCES = 0xDF30, ROOT_CITIES = 0xDD38, ROOT_REGIONS = 0x6CAF0, REGION_MAX = 0x1F4,
    FN_IS_PLAYER = 0x235150,
    /* 수상 보정(그룹 10) */
    VT_ATTACK = 0x13496F0, FN_ATTACK = 0x2FE10, SLOT_ATTACK = VT_ATTACK + 16 * 8, /* 공격 출진 노드, +0x60 대상 거점 */
    ATK_RESULT = 0x98,     /* 공격 노드 결과 목록(GameList 머리, 핸들 +0xA0). 원소 = 만든 묶음(Troops) */
    FN_TROOPS_ID = 0x22FB90, /* (묶음) → 번호(부대 +0x58) */
    FN_SETTINGS = 0x3EC1A0, FN_DIFFICULTY = 0x3EC370, /* () → 설정, (설정) → 난이도(4 궁극). 공격 끝 취소 0x30280과 같은 호출 */
    FN_SORTIE_CANCEL = 0x9F860, /* (묶음) 출진 취소. 공격 노드 끝 취소(0x302F2)와 같은 호출 */
    FN_UNIT_FORM_STAT = 0x2AF8B0, /* (부대, 0 지금·1 육상·2 수상, n) 진형 수치(부대 세력 보정) */
    ROOT_UNITS = 0x7E838, FN_UNIT_FORCE = 0x22E690, /* 부대 표, (부대) → 세력 id */
    FN_RANGED = 0x2C9080,       /* (세력, 진형) → 원거리(사거리 2) 진형이면 1 */
    FN_FH_LEADER = 0x22C5A0,    /* (거점) → 거점 대장 무장(+0x2E) */
    VT_TRANSPORT = 0x1349878, FN_TRANSPORT = 0x32D90, SLOT_TRANSPORT = VT_TRANSPORT + 16 * 8, /* CommandTransportDepature */
    SITE_TR_ISSUE = 0x3546E, FN_TR_ISSUE = 0x201C20, /* 0x32230 안 수송 부대 만들기(사양, 0). 명령서 −1 */
    FN_BATTLE_POWER = 0x2AAF30, /* (부대, 거점, 분할, 전투) → 전투력 p. 부대·거점 중 하나만 */
    FN_FH_DUR = 0x22E110,       /* (거점) → 지금 내구 */
    VT_DEPART = 0x1349AA0, FN_DEPART = 0x35A70, SLOT_DEPART = VT_DEPART + 16 * 8, /* Departure: +0x48 행동, +0x4C 대상 변수 */
    VT_ADDARMY = 0x1350F60, FN_ADDARMY = 0x81420, SLOT_ADDARMY = VT_ADDARMY + 16 * 8, /* ListAddArmy */
    FN_SET_ACTION = 0x2ECEA0, /* (부대, 행동) +0x37 */
    FN_SET_TARGET = 0x2ECF00, /* (부대, 종류, id) +0x38·+0x3A */
    FN_UNIT_TGT_OWNER = 0x2B7320, /* (부대) 대상 소유 세력(매일 처리 0x272FF0이 부대 세력과 비교) */
    FN_RETURN = 0x2EC570,         /* (부대, …) 귀환 */
    FN_RETREAT_STAY = 0x2CB6C0,   /* (부대) 퇴각 노드 0x61120이 귀환 대신 대기(행동 13)로 가는 조건(뜻 미확인) */
    FN_ADJ_EFFECT = 0x2B2A90,     /* (칸, 세력 번호, &합계) → 인접 지역 효과 계수(float) */
    VT_RETREAT = 0x134DEA0, FN_RETREAT = 0x61C90, SLOT_RETREAT = VT_RETREAT + 16 * 8, /* CArmySetRetreatNode: +0x38 부대 변수 */
    ROOT_TILES = 0xE270, ROOT_TERRAINS = 0x73860, HEX_COUNT = 220 * 220,
    SITE_MOVE = 0x2DBA75, FN_MOVE = 0x26AB00, /* 부대 처리 0x2D95F0 안 이번 이동량(부대) → 0x3011A0(부대, 다음 칸, 이동량) */
    ROOT_WORLD = 0x871E8, WORLD_YMD = 0x34, WORLD_COALITION = 0x165C, /* 게임 진행: 날짜, 연합 남은 개월(u8) */
    /* CCommandAreaOccupyDepatureNode: +0x60 대상 지역 목록, +0x54 예비. 모드 0x43130·0x43600·0x43AC0·0x43F80이 거점마다
       계획(0x3E5A0) 뒤 (병력 − 예비)로 0x27410 → 출진 0x1EC890(0x27775) */
    VT_AREA = 0x1349128, FN_AREA = 0x29BA0, SLOT_AREA = VT_AREA + 16 * 8, FN_AREA_PLAN = 0x3F060,
    SITE_AREA_CREATE = 0x27D65, SITE_TRYGET_CREATE_A = 0x2AC18, SITE_TRYGET_CREATE_B = 0x2BBE4, /* 0x2A5A0 지역 확보 출진 */
    VT_TRYGET = 0x13491E0, FN_TRYGET = 0x2A5A0, SLOT_TRYGET = VT_TRYGET + 16 * 8, /* CCommandAreaTryGetDepartureNode */
    FN_UNIT_TROOPS = 0x43630,  /* (부대) → 묶음 객체(+0x58 번호로 world+0x7F8D8) */
    FN_TROOPS_DISSOLVE = 0x2842E0, FN_SET_TROOPS = 0x243630, /* 묶음 해체, (부대, 묶음 번호) 넣기. 경로 A가 부대를 합칠 때와 같은 순서 */
    /* 무장 이동 명령: 사양 = {무장 목록 vtable, 목록 머리, 출발 칸 0x22D500(거점), 도착 거점} */
    FN_PERSON_MOVE = 0x1F2630, FN_FH_TILE = 0x22EF50, VT_PERSON_LIST = 0x13466B0,
    FN_PLIST_INIT = 0x1AC60, FN_PLIST_CLEAR = 0x37DF00, FN_PLIST_FREE = 0x1BFFF0, FN_POOL_ADD = 0x18D50,
    POOL_NODES = 0x27D4130, POOL_LOCK_ON = 0x27D4100, POOL_LOCK = 0x27D4108, IAT_LOCK = 0x1343328, IAT_UNLOCK = 0x13430D8,
};
/* 공격 노드 실행(0x2F2B0)과 모드 함수 안의 0xB5EC0 호출: 만든 부대 전력(수상 보정 대상) */
static const uint32_t ATTACK_POWER_CALLS[] = {0x30D33, 0x45EF7, 0x469EC, 0x47FE4, 0x48A82, 0x4A68C, 0x4BC87, 0x4CE02, 0x4D923};

/* 0xB6F50(1.0.1 0xB5EC0) 호출처 전부(E8, 2026-11-05 1.0.2 xref 31곳). 모두 AI 코드(트리 노드·공격 노드·출진 판정) */
static const uint32_t UNIT_POWER_CALLS[] = {
    0x2A573, 0x2A868, 0x2A8B4, 0x30D33, 0x45EF7, 0x469EC, 0x47FE4, 0x48A82, 0x4A68C, 0x4BC87, 0x4CE02,
    0x4D923, 0x63D73, 0x6B303, 0x6B438, 0x6BF7F, 0x6C139, 0x6C3E8, 0x6C4A6, 0x6C7F9, 0x6E45A, 0x73B5F,
    0x73C33, 0x73CBC, 0x7C0B3, 0x7ED1A, 0x7F396, 0x87CAB, 0x8825E, 0x883C3, 0x917A9,
};
/* 계략 후보 목록 0x6DAF20 호출: 이간, 독, 회유, 빈집, 시설 파괴, 유언 */
static const uint32_t AREA_PLAN_CALLS[] = {0x43DEB, 0x436CB, 0x43B8B, 0x4406B};
static const uint32_t PLOT_LIST_CALLS[] = {0x3A1D5, 0x3C305, 0x3DCA5, 0x3E502, 0x371E5, 0x2F84D};

/* 블랙보드 변수 이름 해시(요격 블록) */
static const uint32_t BT_TARGET = 0x006CE4F7u;     /* 위협의 목표 거점 */
static const uint32_t BT_LEAD = 0x3894C41Cu;       /* 적 선두 부대 */
static const uint32_t BT_NEAR = 0x82920E0Bu;       /* 근접 문턱 E×5/10 */
static const uint32_t BT_FAR = 0x91F558C9u;        /* 원거리 문턱 E×10/10(목표 거점) · E×7/10(지원) */
static const uint32_t BT_CAPITAL = 0x8A43B36Au;    /* 군주 거점 지원 문턱 E×4/10 */
static const uint32_t BT_HARD = 0xA158A387u;       /* 근접 문턱(난이도별) */
static const uint32_t BT_LEAD_ATK = 0xE468BDBFu;   /* 적 선두 공군 */
static const uint32_t BT_RELATED_N = 0xCA22CD94u;  /* 관계 무장 수 */
static const uint32_t BT_PERSONS = 0x5A6AF2F3u;    /* 후보 거점 무장 목록 */
static const uint32_t BT_FOOTHOLD = 0x659CC10Du;   /* 후보 거점 */
static const uint32_t BT_ENEMY = 0x01DFC17Du;      /* 적 전력 E(출진마다 줄어듦) */
static const uint32_t BT_RESERVE = 0x4889042Fu;    /* 예비 병력 R */
static const uint32_t BT_SOLDIERS = 0x608D13B6u;   /* 출진 병사 */
static const uint32_t BT_FILL = 0xDE61059Du;       /* [1725] 채울 병사 */
static const uint32_t BT_PERSON = 0x7EE148F5u;     /* [1687] 지금 무장 */
static const uint32_t BT_FORM = 0x80A48F4Fu;       /* [1695] 지금 진형 */
static const uint32_t BT_THREAT = 0x1F31AD1Au;     /* 위협 적 부대 목록(선두 = 0번) */
static const uint32_t BT_STRENGTH = 0x8DC2D548u;   /* 후보 거점 출격 가능 전력 S([1518]/[1534]) */
/* 공격 판단 [1883]: [2043] 참여 거점 전력 합 > 문턱(대상 방어 × 210%/170% + …) */
static const uint32_t BT_ATK_SUM = 0x3DE0871Fu;    /* 참여 거점 전력 합 */
static const uint32_t BT_ATK_BAR = 0x19FCD973u;    /* 문턱 */
static const uint32_t BT_ATK_TARGET = 0xAD8610FEu; /* 대상 거점 */
static const uint32_t BT_ATK_FHS = 0x3EEF1F81u;    /* 참여 거점 목록 */
static const uint32_t BT_ATK_DEF = 0xB57EAF65u;    /* 대상 방어 전력 */
static const uint32_t BT_MY_FORCE = 0xB2020915u;   /* [1892] GetMyselfForce */
static const uint32_t BT_ATK_MIN_SOL = 0x633D2FF2u; /* [1910] 참여 거점 병사 문턱 */
static const uint32_t BT_ATK_DANGER = 0xCEB8BE84u;  /* [454] 위협 거점 목록(다가오는 적 전력 > 거점 전력) */

#define NEAR_DIST 7
#define REF_SOLDIERS 5000
#define GROUP_PCT 80
/* 1.0.2 재구성(2026-11-05 사용자 결정): 출진 여부는 원작, 누가·얼마나·진형만 패치.
   1 = 요격·수비(요격 A·B·D, 열세 하한, 인접 효과 비, 관문 수비·태수·들여보내기, 전력 보정·임무).
   2 = 1 + 공격 편성·행군(공격 동반 출진·호위·진형, 지원 예상·수상 배율은 양에만, 위협 예비는 다가오는 부대만, 행군 대기)
       + 수상 우위 요격(문턱 0·최소 병력·예비 상한·상륙 재계산, 2026-11-05 사용자 결정으로 앞당김). 빌드 61.
       + 전선 보강 수송(빌드 53 원작 수송 감싸기, 2026-11-06 사용자 결정으로 앞당김). 빌드 65.
   3 = 보류 항목(물 위 요격 보정 배율·전과 확대·집 위협 대응·관문 함락 예상). 공격 판단·지역 점령 규칙은 어느 단계에도 없다(제거).
       3단계 코드 경로(atk_threat_reserve의 atk_hopeless, atk_undo)에는 제거한 판단이 아직 섞여 있어 켜기 전에 다시 짜야 한다.
   빌드 때 /DV102_STAGE=n(또는 CL 환경 변수)으로 바꿀 수 있다 */
#ifndef V102_STAGE
#define V102_STAGE 2
#endif
#define V102_REMOVED 0 /* 제거한 묶음(지역 점령 출진 규칙·트리 공격 문턱)의 연결. 코드는 기록용으로 남긴다 */
#define RELATED_BONUS 115
#define GROUP_MAX 5
#define ATK_GROUP_MAX 3
#define EVEN_PCT 100 /* 요격 그룹 전력이 E의 이 % 이상이면 GROUP_MAX명 */
#define WEAK_PCT 70  /* 이 % 이상이면 EVEN_MAX명, 밑이면 1위 혼자 */
#define EVEN_MAX 3
#define LEAD_PCT 50  /* 요격 부대마다 적 선두 부대 전력의 이 % 이상 */
#define MIN_SHARE 3000
#define MIN_SHARE_V3 5000
#define W_LEAD 0.76f /* 그룹 10 종합 능력 가중치 */
#define W_WAR 0.12f
#define W_INT 0.12f
#define MISSION_TOP 2 /* 그룹 10: 비군사 임무에서 빼는 군단 A 상위 인원(군주 별도) */
#define NAVAL_MIN 0.5f  /* 수상 배율 범위(요격) */
#define NAVAL_ATK_MIN 0.3f /* 공격 수상 배율 하한(2026-10-31: 누선 대 주가처럼 크게 밀리면 0.5로는 모자람) */
#define NAVAL_MAX 1.5f
#define NAVAL_RIVER 4   /* 공격: 직선 경로의 물 칸이 이 이상이면 강 건너 */
#define NAVAL_MARGIN 4  /* 요격: 적 선두 앞쪽 ⌈거리/2⌉ + 이만큼이 물이어야 물 위에서 맞선다 */
#define NAVAL_ADV_RUN 4 /* 빌드 48 수상 우위 요격: 적 선두 ~ 목표 직선에 물 칸이 이 이상 남고(빌드 49부터 이어지지 않아도 됨) */
#define ADJ_MIN 0.75f   /* 빌드 49 마: 요격 판단의 인접 효과 비(우리 계수 / 적 계수)를 이 범위로 자른다 */
#define ADJ_MAX 1.33f
#define NAVAL_ADV_RESERVE 2000 /* 빌드 52: 수상 우위 요격은 출진 거점 예비(원래 4000)를 이 이하로 */
#define NAVAL_ADV_MIN 3000 /* 빌드 50: 수상 우위 요격도 무장마다 병사 이 이상(못 채우면 그 무장은 나가지 않음) */
#define NAVAL_ADV_TIER 1 /* 목표 소유 세력의 수상 진형(주가 1 · 몽충 2 · 누선 3단계)이 적 선두보다 이 단계 이상 높으면 */
#define LAND_RETREAT_PCT 70 /* 수상 우위로 나간 요격 부대: 상대 선두가 상륙하면 아군(5칸) 전력이 적(10칸) 전력의 이 % 미만이면 귀환 */
#define NAVAL_TRACK_DAYS 60 /* 그 부대를 이 날수까지 지켜본다 */
#define MEET_SPAN 3     /* 빌드 48 요격 진형: 예상 만남 칸 앞뒤 이 칸까지 지역 소유를 본다 */
#define MISSION_LIST_MAX 512 /* 임무 후보 목록을 다 읽을 수 있는 크기(넘으면 거르지 않음) */
#define FEW_ENEMIES 2 /* 다가오는 적 부대가 이 이하면 봉시 허용 */
#define FEW_ENEMIES_V3 1 /* 그룹 10: 적 1부대일 때만 봉시 허용(2026-10-30 사용자 결정) */
#define ARROW_PER_ENEMY 0.5f /* 그룹 10: 적이 여럿이어도 부대 전력 ≥ 적 전력 합 × 이것 × 적 부대 수면 봉시 허용 */
#define ARROW_SINGLE 0.8f /* 그룹 10(빌드 61, 2026-11-05 사용자 결정): 적이 1부대면 부대 전력 ≥ 적 전력 × 이것일 때만 봉시 허용, 아니면 탱커 우선 */
#define ENEMY_NEAR 6  /* 그룹 10: 적 선두에서 이 칸 안의 선두와 같은 세력 부대도 다가오는 적으로 센다 */
#define UNIT_MAX 500  /* 부대 표(ROOT_UNITS) 칸 1..500 */
#define GATE_NEAR 2      /* 그룹 10: 적 선두가 관문에서 이 칸 안이면 관문 수비 규칙(관문 사격 사거리) */
#define GATE_INSIDE 2     /* 지원 거점에서 적 선두가 관문보다 이만큼 이상 가까우면 관문 안쪽 적으로 보고 일반 요격 */
#define GATE_LEAD_GAP 20 /* 관문에 남는 무장 중 통솔이 (나가는 무장 통솔 − 이것) 이상이 있어야 원거리 출진 */
#define GATE_DUR_PCT 30  /* 지원을 관문 안으로 들일 때 내구가 최대의 이 % 이상(빌드 63: 도착 때 예상 내구로) */
#define GATE_MARCH_HEX 6 /* 빌드 63: 들여보낼 부대가 한 순(10일)에 가는 칸(빌드 62 판 189·952: 순당 5~7칸) */
#define GATE_DUR_TURNS 3 /* 빌드 63: 관문 내구 감소 빠르기를 최근 이 순 수로 잰다 */
#define WEAK_FLOOR_PCT 25  /* 그룹 10: 계획 전력이 E의 이 % 미만이면(2026-10-31 50 → 25: 6~23%짜리 출진만 막으려고) */
#define WEAK_FLOOR 5000    /* 무장마다 병사 이 이상으로 올리고, 못 올리면 그 무장은 출진하지 않음 */
/* 거리: 이웃 거점(성·관문) 사이는 보통 17~24칸, 중앙 21칸(2026-10-31 지도 MapExCe.bin으로 잼) */
#define THREAT_UNIT_R 15   /* 그룹 10 공격: 출발 거점에서 이 칸 안 적 부대 병사 합을 */
#define THREAT_FH_R 28     /* 이 칸 안 적 거점(도시·관문) 병력 중 가장 큰 것의 */
#define THREAT_FH_R_TAKEN 40 /* 빌드 50: 점령한 지 TAKEN_WIDE_DAYS일 안인 거점은 이 반경 */
#define TAKEN_WIDE_DAYS 180
#define TAKEN_REST_DAYS 30 /* 빌드 50(2026-11-05 사용자 결정): 점령한 지 이 날수 안인 거점은 공격 출발 거점이 되지 않음(요격은 함) */
#define THREAT_FH_PCT 50   /* 이 %와 견줘 큰 쪽을 예비 병력으로 남긴다 */
#define FAR_ATTACK 50      /* 위협이 있는 출발 거점은 목표가 이 칸보다 멀면 출진하지 않음 */
#define SUPPORT_R 40       /* 그룹 10 공격: 목표에서 이 칸 안 목표 측(주인·동맹) 거점의 */
#define SUPPORT_KEEP 4000  /* 병력 − 이것(요격 기본 예비)의 */
#define SUPPORT_PCT 50     /* 이 %를 지원 예상 병사로 보고(목표 자신은 100%), 출진 무장 병사당 전력으로 바꿔 채울 목표에 더한다 */
#define SUPPORT_CAP_PCT 130 /* 출진 판단 기준에서 지원 예상 전력은 원작 필요의 이 %까지(계획 병력·원래 기준 통과의 끝 재확인은 그대로).
                               2026-11-02 100 → 130(사용자 결정: 상대 전력을 원작보다 넉넉히 크게 본다. 기준 = 필요 × 2.3 × 70%) */
#define SUPPORT_BUSY_R 15  /* 지원 거점 이 칸 안에 목표 주인의 다른 적 부대가 있으면(다른 전선에 묶임) */
#define FIELD_NEAR_R 15    /* 지원 예상에 더하는 목표 주변 적 야전 부대(공격 세력·그 동맹 아님): 이 칸 안 100% */
#define FIELD_FAR_R 25     /* 이 칸 안 */
#define FIELD_FAR_PCT 50   /* 이 % */
#define ALLY_MIN_MONTHS 4  /* 동맹이 이 개월 이상 남은 세력의 목표 주변 부대는 적 위험에서 뺀다(2026-11-03 사용자 결정) */
#define SUPPORT_BUSY_PCT 20 /* 그 거점은 SUPPORT_PCT 대신 이 % */
#define MAIN_AWAY_PCT 30   /* 목표 주인의 공격 중 야전 병사(목표에서 SUPPORT_R칸 밖)가 거점 병력 합의 이 %면 */
#define MAIN_AWAY_CUT 30   /* 주변 지원(목표 자신 제외)을 이 %까지 깎는다(그 아래는 비례) */
#define ATK_GO_PCT 70      /* 그룹 10 공격: 낼 수 있는 전력이 (필요 + 지원 예상)의 이 % 미만이면 공격하지 않는다 */
#define ATK_END_PCT 60     /* 끝 재확인: 만든 부대 전력이 판단 기준(필요 + 지원 예상, 상한 통과면 상한 기준)의 이 % 미만이면 취소 */
#define ATK_GO_CROSS_PCT 90  /* 강을 건너는 공격(crosses_river)의 판단 % (2026-11-03 사용자 결정 D) */
#define ATK_END_CROSS_PCT 80 /* 그 끝 재확인 % */
#define CROSS_SHARE_PCT 50 /* 강을 건너는 참여 거점의 낼 전력이 이 % 이상이면 강 건넘 기준(빌드 46) */
#define ATK_FUTILE_PCT 40       /* 빌드 70: 낼 수 있는 전력이 (필요 + 지원 예상)의 이 % 미만이면 공격하지 않는다(가망 없는 공격만) */
#define ATK_FUTILE_CROSS_PCT 65 /* 빌드 70(빌드 72에서 70 → 65): 강을 건너는 공격의 같은 문턱 */
#define SAT_PCT 70         /* 포화 출진(빌드 46, 2026-11-03 사용자 결정): 거점 병력이 상한(vfunc +0xC8)의 이 % 이상이고
                              강을 건너지 않거나 수군이 밀리지 않는 거점이 */
#define SAT_KEEP_PCT 30    /* 보통 위협 예비가 성 병력의 이 %보다 크면 이 %만 남기고(곧 적어도 70% 동원, 2026-11-03 사용자 결정), 목표를 뺀 위협 예비보다는 남긴다 */
#define COUNTER_TREE_PCT 120 /* 빌드 51 역습 창: 목표 병력이 빠졌을 때 원작 트리 문턱을 대상 방어 × 이 %로 */
#define COUNTER_DRAIN_PCT 50 /* 목표 병력이 최근 60일 최고의 이 % 미만이면 */
#define SAT_TREE_PCT 120   /* 원작 트리 문턱(대상 방어 × 210%)을 참여 거점 전력 합 ≥ 대상 방어 × 이 %로 낮춘다 */
#define SAT_LAND_R 28      /* 빌드 50: 포화 거점에서 이 칸 안에 강을 건너지 않고 닿는 적 거점이 있으면 강 건너 목표로는 포화 출진하지 않음 */
#define SAT_COOLDOWN 60   /* 포화 출진한 거점은 이 날수 동안 다시 포화 거점으로 보지 않는다 */
#define PLAYER_SUP_HARD 75 /* 대상이 플레이어 거점이면 지원 예상을 이 %로: 특급(난이도 3) */
#define PLAYER_SUP_ULT 50  /* 궁극(4). 그 밖 100 */
#define SAT_GO_PCT 50      /* 포화 출진 통과: 포화 거점이 낼 전력이 원작 필요의 이 % 이상(빌드 47. 빌드 46은 (필요 + 지원 예상 상한)의 60%) */
#define SAT_END_PCT 40     /* 포화 통과 공격은 만든 부대 전력이 원작 필요의 이 % 이상이면 취소하지 않는다(게임 끝 취소 포함) */
#define MARCH_GAP 3        /* 그룹 10 행군: 같은 묶음·같은 목표에서 가장 뒤처진 부대보다 목표에 이 칸 이상 가까우면 기다린다 */
#define MARCH_FREE 6       /* 목표에서 이 칸 안이면 기다리지 않는다(도시 사격 3칸 + 여유) */
#define MARCH_ENEMY 4      /* 이 칸 안에 적 부대가 있으면 기다리지 않는다 */
#define MARCH_HOLD_DAYS 10 /* 한 번에 이 날수보다 오래 기다리지 않는다(뒤 부대가 막혔을 때) */
#define MARCH_GROUP_DAYS 20 /* 빌드 50: 같은 묶음·목표의 첫 기다림부터 이 날수가 지나면 그 묶음은 더 기다리지 않는다 */
#define SIEGE_STALL_DAYS 10 /* 빌드 68: 가장 뒤가 공성 병기면 다가오는 동안은 계속 기다리고, 이 날수 동안 목표에 가까워지지 않으면 포기 */
#define GOV_ENEMY_R 10     /* 그룹 10 빈 관문 태수: 관문에서 이 칸 안에 적 부대가 있으면(2026-11-02 사용자 결정) */
#define GOV_FROM_R 30      /* 관문에서 이 칸 안 같은 세력 거점의 무장 중에서 보낸다 */
#define GOV_LOW_PCT 30     /* 관문 병력이 근처 적 병사 합의 이 % 미만이면 함락 위험(포로)이라 보내지 않는다 */
#define GOV_WAIT_DAYS 90   /* 보낸 뒤 이 날수 안에 도착하지 않으면 추적을 끝낸다 */
#define GOV_STAY_DAYS 180  /* 도착 뒤 이 날수까지 관문을 떠나는지 본다 */
#define F_BONGSI 2
#define F_ANHAENG 3
#define F_CHUHAENG 7    /* 공격 B단계는 진형이 추행으로 정해지면 그 무장을 건너뛴다(0x458C0) */
#define GATE_PRIO_PCT 125 /* 빌드 51: 도시에서 관문 지원은 관문 위험도(노리는 적 병사 / 병력)가 도시 위험도의 이 % 를 넘을 때만 */
#define GATE_DAYS 30     /* 빌드 50: 관문 공격은 만든 부대의 함락 예상이 이 날수 이하일 때만(넘으면 노드 끝에서 취소) */
#define GATE_CD_DAYS 60  /* 빌드 52: 함락 예상으로 취소한 (세력, 관문)은 이 날수 동안 공격하지 않음 */
#define GATE_SIM_MAX 120 /* 그 예상을 이 날수까지만 돌린다 */
#define GATE_ADJ 4       /* 관문 옆(거리 1) 칸 6개 중 지형에 막히지 않는다고 보는 칸 수(통행 판정 미확인) */
#define SIEGE_ESCORT 2  /* 그룹 10 공격: 공성 병기(정란 8·충차 9·투석 10)는 같은 거점에서 이미 나간 공성 아닌 부대의 수와 병사가
                           (공성 부대 + 이번)의 이 배 이상일 때만 */
#define FORM_LAST 20
#define STEP 500
#define ATK_MIN_SHARE 3000 /* 원작 공격 B단계·선봉은 병사 3000 미만이면 그 무장을 건너뛴다 */
#define FLOOR_TILT_C 2.0f /* 빌드 67: 공격 바닥 = 필요 + w × (원작 예상 − 필요), w = 0.5 + 0.5 × (r − 1) / (r − 1 + C), r = 원작 예상 / 필요 */
#define MAX_CAND 64
#define HEX_LAST (220 * 220 - 1) /* 0x7AEE0이 칸 번호를 자르는 값 */
#define HEX_NONE (220 * 220)     /* 0xBEB30이 칸을 못 구했을 때(0xA8C90이 비교) */

typedef void *(__fastcall *PtrFn)(void *);
typedef int(__fastcall *ObjFn)(void *);
typedef int(__fastcall *NodeFn)(uint8_t *node, void *ctx, void *a3);
typedef char(__fastcall *CondFn)(void *ctx, void *node, const int *args, int argc);
#define CALL(type, rva) ((type)RVA(rva))

/* ---------- 블랙보드 ---------- */

static void *bb(void *ctx) { return CALL(PtrFn, FN_BLACKBOARD)(ctx); }

static int bb_int(void *ctx, uint32_t name, int *out)
{
    *out = 0;
    return ((char(__fastcall *)(void *, uint32_t, int *, int))RVA(FN_BB_GET_INT))(bb(ctx), name, out, 0) != 0;
}

static void *bb_obj(void *ctx, uint32_t name)
{
    void *o = NULL;
    if (!((char(__fastcall *)(void *, uint32_t, void **, int))RVA(FN_BB_GET_OBJ))(bb(ctx), name, &o, 0))
        return NULL;
    return o;
}

static int bb_set(void *ctx, uint32_t name, int v)
{
    return ((char(__fastcall *)(void *, uint32_t, int))RVA(FN_BB_SET_INT))(bb(ctx), name, v) != 0;
}

static int valid(void *o) { return o && CALL(ObjFn, FN_VALID)(o); }

static int g_v3;  /* 1이면 비교용 그룹 10(F) */

static int ability(uint8_t *person, int kind)
{
    return ((int(__fastcall *)(void *, int, int))RVA(FN_ABILITY))(person, kind, 0xFFF);
}

/* 종합 능력 A(그룹 10) */
static float ability_a(uint8_t *person)
{
    return (float)ability(person, 0) * W_LEAD + (float)ability(person, 1) * W_WAR + (float)ability(person, 2) * W_INT;
}

/* 0x2A8D40의 통솔항 */
static float lead_term(float t, int soldiers)
{
    float q = (float)(int)(4 * t / 3);
    float sq = q * q < (float)soldiers ? q * q : (float)soldiers;
    return sqrtf(sq > 0 ? sq : 0) + 2 * t + 200;
}

/* 그룹 10: 부대 전력 추정에 곱할 값(대장의 통솔 대신 A를 넣은 통솔항의 비) */
static float a_factor(uint8_t *unit)
{
    if (!g_v3 || !unit)
        return 1;
    unsigned id = *(uint16_t *)(unit + 0x12);
    uint8_t *p = id <= PERSON_MAX ? *(uint8_t **)(game_root() + ROOT_PERSONS + (size_t)id * 8) : NULL;
    if (!valid(p))
        return 1;
    int t = ability(p, 0), s = *(uint16_t *)(unit + 0x16);
    float base = lead_term((float)t, s);
    return t > 0 && base > 0 ? lead_term(ability_a(p), s) / base : 1;
}

typedef float(__fastcall *UnitPowerFn)(void *);

/* 부대 전력 0xB5EC0 + 그룹 10 보정 */
static float est_power(void *unit)
{
    float r = CALL(UnitPowerFn, FN_UNIT_POWER)(unit);
    __try {
        r *= a_factor((uint8_t *)unit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

static float naval_intercept(void *ctx); /* F: 요격 수상 배율(적 선두 기준), 아니면 1 */
static __declspec(thread) int g_nv_water, g_nv_run, g_nv_dist; /* 마지막 요격 판정: 선두 물 위, 앞쪽 물 칸, 거리 */
static __declspec(thread) int g_nv_cross; /* 선두가 땅 위인데 목표까지 강을 건너야 해서 수상 배율을 걸었음(빌드 47) */
static int day_key(void);
static int fh_owner(uint8_t *fh);
static int list_items(void *list, uint8_t **out, int max);
static int naval_adv(void *ctx, int *ours, int *theirs); /* 빌드 48: 수상 우위 요격(배 단계 1 이상 높음, 적 선두 물 위) */
static int meet_enemy_ground(void *ctx, uint8_t *fh, char *note, size_t nsz, int *meet_hex); /* 빌드 48: 예상 만남 칸 근처가 적 지역 */
static float adj_ratio_at(uint8_t *fh, uint8_t *lead, int h, float *ours, float *theirs); /* 빌드 49 마: 만남 칸 인접 효과 비 */
static float adj_ratio(void *ctx, float *ours, float *theirs);
static __declspec(thread) float g_plan_adj = 1; /* 이번 요격 계획의 인접 효과 비(우리 / 적, ADJ_MIN~ADJ_MAX) */
static __declspec(thread) int g_no_enter; /* 빌드 51: 도시에서 관문 지원으로 돌린 요격은 들여보내지 않음(들여보내기는 블랙보드 목표 = 도시를 씀) */
static __declspec(thread) int g_plan_adv; /* 이번 요격 계획이 수상 우위: 열세 단계·열세 하한을 쓰지 않음 */
static void nvt_add(int pid, int enemy_pid, int fid, int th); /* 수상 우위로 나간 요격 무장 추적(상륙 재계산) */
static float naval_plan(uint8_t *fh, uint8_t *target, uint8_t *person); /* F: 공격 계획 수상 배율, 아니면 1 */
static int near_foe_except(uint8_t *fh, int fid, int r, uint8_t **skip, int ns, uint8_t *lead); /* 빌드 71 */
static int enemy_count(void *ctx, int listed, float *foe, int *foe_sol); /* F: 다가오는 적 부대 수와 전력·병사 합(그룹 10 아니면 listed, 0) */

/* ---------- A. 문턱 곱셈·거리 ---------- */

typedef struct {
    const uint32_t *var;
    int from, to;
} Scale;

static const Scale MULTI[] = {
    {&BT_NEAR, 5, 4}, {&BT_FAR, 10, 8}, {&BT_CAPITAL, 4, 35}, {&BT_HARD, 5, 4},
};
static const Scale DIVIDE[] = {
    {&BT_CAPITAL, 10, 100},
};

static int scaled(const Scale *t, int n, uint32_t var, int k)
{
    for (int i = 0; i < n; i++)
        if (*t[i].var == var && t[i].from == k)
            return t[i].to;
    return k;
}

static int __fastcall multi_hook(uint8_t *node, void *ctx, void *a3)
{
    uint32_t var = *(uint32_t *)(node + 0x38);
    int k = *(int *)(node + 0x3C), nk = scaled(MULTI, _countof(MULTI), var, k), v, adv = 0, ao = 0, at = 0;
    float r = 1, adj = 1, adj_o = 1, adj_t = 1; /* 그룹 10: 출진 문턱(E의 배수)에 적 선두의 수상 배율, 빌드 49 마: ÷ 인접 효과 비 */
    if (g_v3 && (var == BT_NEAR || var == BT_FAR || var == BT_CAPITAL || var == BT_HARD)) {
        __try {
            r = naval_intercept(ctx);
            adv = naval_adv(ctx, &ao, &at);
            adj = adj_ratio(ctx, &adj_o, &adj_t);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            r = 1, adv = 0, adj = 1;
        }
    }
    r /= adj;
    /* 빌드 48 수상 우위: 출진 문턱을 0으로 둬서 원작 출진 판정(S > 문턱)을 통과시킨다. 배 성능 차에 건다 */
    if (adv) {
        int ok = CALL(NodeFn, FN_MULTI)(node, ctx, a3);
        if (ok == 1)
            bb_set(ctx, var, 0);
        return ok;
    }
    if (nk == k && r == 1) {
        return CALL(NodeFn, FN_MULTI)(node, ctx, a3);
    }
    if (!bb_int(ctx, var, &v))
        return 2;
    int out = (int)((float)v * (float)nk * r);
    if (!bb_set(ctx, var, out))
        return 2;
    return 1;
}

static int __fastcall divide_hook(uint8_t *node, void *ctx, void *a3)
{
    uint32_t var = *(uint32_t *)(node + 0x38);
    int k = *(int *)(node + 0x3C), nk = scaled(DIVIDE, _countof(DIVIDE), var, k), v;
    if (nk == k)
        return CALL(NodeFn, FN_DIVIDE)(node, ctx, a3);
    if (!bb_int(ctx, var, &v))
        return 2;
    return bb_set(ctx, var, v / nk) ? 1 : 2;
}

static char __fastcall cond_dist(void *ctx, void *node, const int *args, int argc)
{
    if (argc == 5 && args[0] == 5 && (uint32_t)args[1] == BT_TARGET && args[2] == 3 && (uint32_t)args[3] == BT_LEAD &&
        args[4] == 5) {
        int a[5] = {args[0], args[1], args[2], args[3], NEAR_DIST};
        return CALL(CondFn, FN_COND_DIST)(ctx, node, a, argc);
    }
    return CALL(CondFn, FN_COND_DIST)(ctx, node, args, argc);
}

/* ---------- B. 동반 출진 계획 ---------- */

typedef struct {
    uint8_t *person;
    float power;  /* REF_SOLDIERS 기준 전력 */
    float rank;   /* 통솔(그룹 10은 A), 관계 보정 뒤 */
    int lead;     /* 통솔(부상·보너스 반영) */
    int war, intel; /* 무력·지력 */
    float abil;   /* 종합 능력 A(그룹 순위) */
    int cap, share;
    int used; /* 공격: 사양을 정했음. 요격: 병사를 정했음(진형 결정 끝) */
    int form; /* 요격: 정한 진형(0 = 원래대로) */
    int enter; /* 그룹 10 요격: 관문 안으로 들여보냄(출진 행동 3) */
    int siege; /* 그룹 10 공격: 편성 역할 0 정하지 않음, 1 공성, 2 일반(공성 진형이면 바꿈) */
} Member;

typedef struct {
    int stage; /* 0 없음, 1 후보 목록 준비됨, 2 계획 있음 */
    int n;
    uint8_t *fh; /* 공격: 계획한 거점 */
    void *list;  /* 공격: 정렬한 거점 무장 목록 */
    int van_id, van_soldiers, van_form; /* A단계 선봉 부대 생성 사양. -1이면 없음 */
    Member m[MAX_CAND];
} Plan;

/* 계획은 한 세력의 트리 실행 안에서 세우고 쓴다. 게임 목록 함수가 임계 구역을 쓰므로 스레드별로 둔다. */
static __declspec(thread) Plan g_plan, g_atk;

/* 0x7AEE0이 만드는 임시 부대와 같은 순서로 전력(0xB5EC0)을 잰다 */
static float unit_power(uint8_t *person, int soldiers)
{
    __declspec(align(16)) uint8_t unit[0x200];
    uint8_t *root = game_root();
    uint8_t *fh = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, *(uint16_t *)(person + 0xE2));
    if (!fh)
        return 0;
    memset(unit, 0, sizeof unit);
    CALL(void(__fastcall *)(void *), FN_UNIT_CTOR)(unit);
    unsigned id = *(uint16_t *)(person + 0x10);
    *(uint16_t *)(unit + 0x12) = (uint16_t)(id > PERSON_MAX ? PERSON_MAX : id);
    *(uint16_t *)(unit + 0x16) = (uint16_t)soldiers;
    int morale = fh[0x44] < 100 ? fh[0x44] : 100;
    ((void(__fastcall *)(void *, int))RVA(FN_UNIT_MORALE))(unit, morale);
    int hex = CALL(ObjFn, FN_HEX)(fh);
    hex = hex < 0 ? 0 : hex > HEX_LAST ? HEX_LAST : hex;
    *(uint16_t *)(unit + 0x2A) = (uint16_t)hex;
    *(uint16_t *)(unit + 0x152) = (uint16_t)hex;
    unit[0x10] = 1;
    int form = 0xB;
    if (!((int(__fastcall *)(void *, int))RVA(FN_SPECIAL))(person, 0)) {
        uint8_t *f = CALL(uint8_t * (__fastcall *)(void *), FN_PERSON_FORCE)(person);
        if (valid(f))
            form = f[0x15E];
    }
    ((void(__fastcall *)(void *, int))RVA(FN_UNIT_FORMATION))(unit, form);
    ((void(__fastcall *)(void *, int))RVA(FN_UNIT_READY))(unit, 1);
    float p = est_power(unit);
    CALL(void(__fastcall *)(void *), FN_UNIT_DTOR)(unit);
    return p;
}

/* 친애(+0xC0 u16×8, 양방향), 배우자·의형(+0xBA/+0xBC, 조건 0x32/0x31과 같은 칸). 같은 의형 묶음(+0xBC 같음)은
   게임의 의형제 목록 0x2B5620처럼 1 ≤ +0xBC ≤ PERSON_MAX일 때만 본다 */
static int related(uint8_t *a, uint8_t *b)
{
    uint16_t ia = *(uint16_t *)(a + 0x10), ib = *(uint16_t *)(b + 0x10);
    for (int k = 0; k < 8; k++)
        if (*(uint16_t *)(a + 0xC0 + k * 2) == ib || *(uint16_t *)(b + 0xC0 + k * 2) == ia)
            return 1;
    for (int off = 0xBA; off <= 0xBC; off += 2) {
        uint16_t sa = *(uint16_t *)(a + off), sb = *(uint16_t *)(b + off);
        if (sa == ib || sb == ia || (off == 0xBC && sa && sa <= PERSON_MAX && sa == sb))
            return 1;
    }
    return 0;
}

static int list_items(void *list, uint8_t **out, int max)
{
    uint32_t *h = list ? *(uint32_t **)((uint8_t *)list + 8) : NULL;
    void **tbl = *(void ***)RVA(LIST_NODES);
    if (!h || !tbl || *h >= *(uint32_t *)RVA(LIST_COUNT))
        return 0;
    int n = 0;
    for (void **node = tbl[*h]; node && n < max; node = (void **)node[1])
        out[n++] = node[0];
    return n;
}

/* n명에게 STEP씩 돌아가며 나눈다. 그룹 전력 추정이 enemy를 넘거나 더 줄 병사가 없으면 멈춘다. 준 병사 합을 돌려준다. */
static __declspec(thread) int g_split_multi; /* split: 다가오는 적이 2부대 이상(요격 계획) */
static __declspec(thread) int g_fill_cap;   /* 빌드 63: 1이면 공격 계획 — 몫을 지휘 상한 비례로 */

static int fill(Member *m, int n, int avail, int enemy)
{
    for (int i = 0; i < n; i++)
        m[i].share = 0;
    int total = 0;
    float s = 0;
    if (g_fill_cap) { /* 빌드 63(2026-11-06 사용자 결정): 공격은 몫/상한이 가장 작은 무장에게 STEP씩 → 지휘 상한 비례 */
        while (s <= (float)enemy && avail - total >= STEP) {
            int b = -1;
            for (int i = 0; i < n; i++)
                if (m[i].cap - m[i].share >= STEP && m[i].cap > 0 &&
                    (b < 0 || (float)(m[i].share + STEP) / m[i].cap < (float)(m[b].share + STEP) / m[b].cap))
                    b = i;
            if (b < 0)
                break;
            m[b].share += STEP;
            total += STEP;
            s += m[b].power * STEP / REF_SOLDIERS;
        }
        return total;
    }
    for (int moved = 1; moved && s <= (float)enemy;) {
        moved = 0;
        for (int i = 0; i < n && s <= (float)enemy; i++) {
            if (avail - total < STEP || m[i].cap - m[i].share < STEP)
                continue;
            m[i].share += STEP;
            total += STEP;
            s += m[i].power * STEP / REF_SOLDIERS;
            moved = 1;
        }
    }
    return total;
}

static int by_rank(const void *a, const void *b)
{
    float x = ((const Member *)a)->rank, y = ((const Member *)b)->rank;
    return x < y ? 1 : x > y ? -1 : 0;
}

/* 몫을 받은 부대 중 가장 작은 예상 전력 */
static float min_unit(const Member *m, int k)
{
    float lo = 0;
    for (int i = 0; i < k; i++) {
        float p = m[i].power * m[i].share / REF_SOLDIERS;
        if (m[i].share && (lo == 0 || p < lo))
            lo = p;
    }
    return lo;
}

/* 그룹 앞 k명에게 몫을 나눈다. 몫이 MIN_SHARE보다 작거나 가장 작은 부대 전력이 unit_min보다 작으면 인원을 줄여
   다시 나눈다(1명까지). 몫을 받은 인원을 돌려준다. */
static int split(Member *m, int k, int avail, int need, float unit_min)
{
    int min_share = g_v3 ? MIN_SHARE_V3 : MIN_SHARE, fb = 0;
    if (avail < STEP || k < 1)
        return 0;
    if (avail / min_share < k)
        k = avail / min_share > 1 ? avail / min_share : 1;
    for (; k > 1; k--) {
        int total = fill(m, k, avail, need), share_ok = total >= k * min_share || total >= avail - STEP;
        if (share_ok && !fb)
            fb = k; /* 몫은 되는 가장 큰 인원(부대당 최소 전력은 못 맞춤) */
        if (share_ok && min_unit(m, k) >= unit_min)
            break;
    }
    /* 빌드 49 다(2026-11-04 사용자 결정): 다가오는 적이 2부대 이상(g_split_multi)인데 부대당 최소 전력을 맞추는 2명 이상
       인원이 없으면 1명이 아니라 몫이 되는 가장 큰 인원으로(빌드 48b 판 강릉 219.6.11: 4명 → 관우 혼자) */
    if (k == 1 && fb > 1 && g_split_multi && unit_min > 0)
        k = fb, fill(m, k, avail, need);
    else if (k == 1)
        fill(m, 1, avail, need);
    while (k > 0 && m[k - 1].share == 0) /* 필요 전력을 먼저 채워 몫이 없는 뒤쪽 */
        k--;
    return k;
}

/* 몫대로 나간 그룹의 전력 추정 합 */
static float group_power(const Member *m, int k)
{
    float s = 0;
    for (int i = 0; i < k; i++)
        s += m[i].power * m[i].share / REF_SOLDIERS;
    return s;
}

/* 후보 n명을 재서 순위를 매긴다. m[0..k)가 그룹 후보(1위가 맨 앞, 몫은 아직 없음), *cand는 후보 수. k를 돌려준다. */
static int rank_group(Member *m, uint8_t **items, int n, int group_max, int *cand)
{
    int c = 0, top = -1;
    for (int i = 0; i < n && c < MAX_CAND; i++) {
        if (!valid(items[i]))
            continue;
        int cap = CALL(ObjFn, FN_CAP)(items[i]);
        if (cap < 1000)
            continue;
        m[c].person = items[i];
        m[c].cap = cap;
        m[c].power = unit_power(items[i], REF_SOLDIERS);
        m[c].lead = ability(items[i], 0);
        m[c].war = ability(items[i], 1);
        m[c].intel = ability(items[i], 2);
        m[c].abil = ability_a(items[i]);
        m[c].share = 0;
        m[c].used = 0;
        m[c].form = 0;
        m[c].enter = 0;
        m[c].siege = 0;
        if (top < 0 || m[c].power > m[top].power)
            top = c;
        c++;
    }
    *cand = c;
    if (top < 0 || m[top].power <= 0)
        return 0;
    for (int i = 0; i < c; i++) {
        m[i].rank = g_v3 ? m[i].abil : (float)m[i].lead; /* A 기준(확장 연결 실패면 통솔) */
        if (i != top && related(m[top].person, m[i].person))
            m[i].rank = m[i].rank * RELATED_BONUS / 100;
    }
    float limit = (g_v3 ? m[top].abil : (float)m[top].lead) * GROUP_PCT / 100;
    m[top].rank = 1e30f; /* 1위는 맨 앞 */
    qsort(m, c, sizeof *m, by_rank);
    int k = 0;
    while (k < c && k < group_max && (k == 0 || m[k].rank >= limit))
        k++;
    return k;
}

/* 무장의 세력 번호(없으면 0) */
static int force_id(uint8_t *person)
{
    uint8_t *f = valid(person) ? CALL(uint8_t * (__fastcall *)(void *), FN_PERSON_FORCE)(person) : NULL;
    return valid(f) ? (*(int(__fastcall **)(void *))(*(uint8_t **)f + 0x60))(f) : 0;
}

/* ---------- D. 진형 ---------- */

static const int TANKS[] = {1, 5, 4, 6}; /* 어린, 학익, 방원, 장사 */

static int is_tank(int id)
{
    for (int i = 0; i < (int)_countof(TANKS); i++)
        if (TANKS[i] == id)
            return 1;
    return 0;
}

static uint8_t *formation_by_id(int id)
{
    return id >= 1 && id <= FORM_LAST ? *(uint8_t **)(game_root() + ROOT_FORMATIONS + (size_t)id * 8) : NULL;
}

/* 무장이 가진 육상 진형이면 게임 요격 평가와 같은 공군 + 방어(세력 보정 포함), 아니면 -1 */
static int form_score(uint8_t *person, int id)
{
    uint8_t *f = formation_by_id(id), *force = CALL(uint8_t * (__fastcall *)(void *), FN_PERSON_FORCE)(person);
    if (!(*(uint32_t *)(person + 0x12C) >> id & 1) || !valid(f) || f[0x7E] == 2 || !valid(force))
        return -1;
    int(__fastcall *stat)(void *, void *, int) = (int(__fastcall *)(void *, void *, int))RVA(FN_FORM_STAT);
    return stat(force, f, 2) + stat(force, f, 5);
}

/* 점수가 가장 큰 진형. tanks_only면 탱커만, 안행은 빼고 고른다. max_cost ≥ 0이면 진형 +0x9A가 그 이하인 것만. 없으면 0 */
static int best_form(uint8_t *person, int tanks_only, int max_cost)
{
    int best = 0, bs = -1;
    for (int id = 1; id <= FORM_LAST; id++) {
        if (id == F_ANHAENG || (tanks_only && !is_tank(id)))
            continue;
        if (max_cost >= 0) {
            uint8_t *f = formation_by_id(id);
            if (!valid(f) || *(uint16_t *)(f + 0x9A) > max_cost)
                continue;
        }
        int s = form_score(person, id);
        if (s > bs) {
            bs = s;
            best = id;
        }
    }
    return best;
}

/* 빌드 53 진형 규칙(2026-11-05 사용자 결정). 전법 계통 = fixdataexce 전법 표 +48(KO CE, 번호 0~200): 1 근접, 2 사격.
   실행 중 전법 객체(ROOT+0x774D0)의 계통 위치는 확인하지 않아 표를 옮겨 둔다(바닐라 데이터, 모드가 바꾸면 다를 수 있음) */
static const char TACTIC_CAT[] = "011122233344455555666777888000042311111655261116154211325111255541141115151411413155451524511154155551111115544553351166165114642656623371236534115355411551565554516666451116011115446500000000000000000";

/* 사격형 장수: 전법(무장 +0x130 u8×10) 점수 일반 1·고유(31 이상) 2, 사격 ≥ 2이고 근접보다 크면 1, 그중 고유 사격이 있으면 2 */
static int archer_kind(uint8_t *person)
{
    int rs = 0, ms = 0, uq = 0;
    for (int i = 0; i < 10; i++) {
        int t = person[0x130 + i];
        if (t <= 0 || t >= (int)sizeof TACTIC_CAT - 1)
            continue;
        int w = t >= 31 ? 2 : 1, c = TACTIC_CAT[t] - '0';
        if (c == 2)
            rs += w, uq |= t >= 31;
        else if (c == 1)
            ms += w;
    }
    return rs >= 2 && rs > ms ? 1 + uq : 0;
}

/* 무장이 가진 육상 진형 id의 공군(세력 보정 포함). 없으면 -1 */
static int form_atk(uint8_t *person, int id)
{
    uint8_t *f = formation_by_id(id), *force = valid(person) ? CALL(uint8_t * (__fastcall *)(void *), FN_PERSON_FORCE)(person) : NULL;
    if (!valid(f) || !valid(force) || !(*(uint32_t *)(person + 0x12C) >> id & 1) || f[0x7E] == 2)
        return -1;
    return ((int(__fastcall *)(void *, void *, int))RVA(FN_FORM_STAT))(force, f, 2);
}


/* 안행 우선순위: 안행이 없으면 0. 고유 사격 300, 사격형 200, 원래 진형(base) 공군이 30 미만이면 30 − 공군, 아니면 0 */
static int goose_prio(uint8_t *person, int base)
{
    if (form_score(person, F_ANHAENG) < 0)
        return 0;
    int k = archer_kind(person);
    if (k)
        return 100 + 100 * k;
    int a = base ? form_atk(person, base) : -1;
    return a >= 0 && a < 30 ? 30 - a : 0;
}

/* 요격 그룹의 진형을 정한다. foe > 0(그룹 10)이면 몫 병사로 낸 부대 전력이 foe × ARROW_PER_ENEMY × 적 부대 수 이상인
   무장은 적이 여럿이어도 원래 점수대로(봉시 허용). 빌드 48: enemy_ground(예상 만남 칸 근처가 적 지역)면 적이 1부대여도
   탱커 우선이고 전력 예외도 없다 */
static void assign_forms(Member *m, int k, int enemies, float foe, int enemy_ground)
{
    int tanks = 0, ranged = 0;
    for (int i = 0; i < k; i++) {
        int few = enemies <= (g_v3 ? FEW_ENEMIES_V3 : FEW_ENEMIES);
        /* 빌드 61: 그룹 10은 적 1부대여도 몫 전력이 적 전력 × ARROW_SINGLE 미만이면 탱커 우선(빌드 60 판 궁극 동탁 봉시 109건) */
        int tank_first = enemy_ground || !few || (g_v3 && foe > 0);
        float bar = few ? foe * ARROW_SINGLE : foe * ARROW_PER_ENEMY * enemies;
        if (tank_first && !enemy_ground && foe > 0 && m[i].power * m[i].share / REF_SOLDIERS >= bar)
            tank_first = 0;
        int f = tank_first ? best_form(m[i].person, 1, -1) : 0;
        if (!f)
            f = best_form(m[i].person, 0, -1);
        if (!f && form_score(m[i].person, F_ANHAENG) >= 0)
            f = F_ANHAENG; /* 다른 육상 진형이 없음 */
        m[i].form = f;
        tanks += is_tank(f);
        ranged += f == F_ANHAENG;
    }
    /* 빌드 53: 안행은 사격형 장수이거나 원래 진형 공군이 30 미만인 무장만, 탱커 수 ≥ 원거리 수일 때 우선순위 순으로 */
    int done[ATK_GROUP_MAX > 8 ? ATK_GROUP_MAX : 8] = {0};
    for (int guard = 0; guard < k && k <= (int)_countof(done); guard++) {
        int bi = -1, bp = 0;
        for (int i = 0; i < k; i++) {
            int pr = done[i] || m[i].form == F_ANHAENG ? 0 : goose_prio(m[i].person, m[i].form);
            if (pr > bp)
                bp = pr, bi = i;
        }
        if (bi < 0)
            break;
        done[bi] = 1;
        int t = is_tank(m[bi].form);
        if (tanks - t >= ranged + 1) {
            tanks -= t, ranged++;
            m[bi].form = F_ANHAENG;
        }
    }
    /* 탱커가 모자라면 안행을 그 무장의 탱커 진형으로(사격형 아닌 쪽부터, 고유 사격 보유는 마지막) */
    while (tanks < ranged) {
        int bi = -1, bp = 1 << 20, bt = 0;
        for (int i = 0; i < k; i++) {
            if (m[i].form != F_ANHAENG)
                continue;
            int t = best_form(m[i].person, 1, -1), pr = goose_prio(m[i].person, best_form(m[i].person, 0, -1));
            if (t && pr < bp)
                bp = pr, bi = i, bt = t;
        }
        if (bi < 0)
            break;
        m[bi].form = bt;
        tanks++, ranged--;
    }
}

static Member *plan_member(uint8_t *person)
{
    for (int j = 0; j < g_plan.n; j++)
        if (g_plan.m[j].person == person)
            return &g_plan.m[j];
    return NULL;
}

/* 아직 진형을 정하는 중인 계획 무장(병사를 정하기 전) */
static Member *forming_member(void *ctx)
{
    if (g_plan.stage != 2)
        return NULL;
    Member *mb = plan_member(bb_obj(ctx, BT_PERSON));
    return mb && !mb->used && mb->form ? mb : NULL;
}

/* [1699] SetFormationEvaluation: 정한 진형이면 평가값을 맨 위로 */
static int __fastcall formeval_hook(uint8_t *node, void *ctx, void *a3)
{
    if (*(uint32_t *)(node + 0x38) == BT_FORM) {
        __try {
            Member *mb = forming_member(ctx);
            uint8_t *f = mb ? bb_obj(ctx, BT_FORM) : NULL;
            if (valid(f) && CALL(ObjFn, FN_FORM_ID)(f) == mb->form)
                bb_set(ctx, *(uint32_t *)(node + 0x3C), 1 << 30);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_plan.stage = 0;
            plog("[요격] 진형 평가 중 예외. 원래 동작으로");
        }
    }
    return CALL(NodeFn, FN_FORMEVAL)(node, ctx, a3);
}

/* [1713] GetFormation(3): 계획한 무장은 정한 진형을 유지 */
static int __fastcall getform_hook(uint8_t *node, void *ctx, void *a3)
{
    Member *mb = NULL;
    __try {
        mb = forming_member(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mb = NULL;
    }
    return mb ? 1 : CALL(NodeFn, FN_GETFORM)(node, ctx, a3);
}

static int first_e(uint8_t *target, void *lead, int enemy); /* F: 이 순 같은 위협의 처음 E */
static void atk_threat_reserve(void *list); /* F: 공격 출발 거점 위협 예비·먼 목표 */
static int atk_support(uint8_t *target, int fid, char *why, size_t wsz, int *field_out); /* F: 목표 주변 지원 예상 병사 */
static __declspec(thread) float g_atk_extra; /* 공격 노드마다 남은 지원 예상 전력(음수 = 아직 안 셈) */
static int atk_hopeless(uint8_t *node); /* F: 공격 출진 판단 */
static int atk_futile(uint8_t *node);   /* 빌드 70: 2단계 가망 없는 공격 막기 */
static int support_keep(uint8_t *fh, int fid, int *fhmax, int *units); /* F: 지원 예상 몫을 낼 때 출발 거점에 남길 병력 */
static __declspec(thread) int g_atk_go; /* 공격 노드의 출진 판단: -1 아직, 0 함, 1 하지 않음 */
static __declspec(thread) float g_atk_want; /* 출진 판단의 (필요 + 지원 예상) 전력. 노드 끝 재확인에 쓴다(음수 = 없음) */
static __declspec(thread) uint8_t *g_atk_node; /* 실행 중인 공격 출진 노드(+0x60 대상 거점) */
static __declspec(thread) int g_atk_bump; /* 지금 거점 계획 때문에 노드 +0x84(남은 필요)에 더해 둔 전력(0 = 없음) */
static __declspec(thread) int g_atk_sat; /* 출진 판단이 포화 전선으로 통과함: 포화 거점만 g_sat_res를 남기고 나감 */
static __declspec(thread) int g_atk_capped; /* 출진 판단이 지원 상한으로만 통과함(g_atk_want = 상한 기준) */
static __declspec(thread) int g_atk_cross; /* 출진 판단에서 강을 건너는 참여 거점이 있었음(끝 재확인 ATK_END_CROSS_PCT) */
static __declspec(thread) int g_sat_n, g_sat_res[64];
static volatile LONG g_sat_last[0x3E]; /* 포화 출진: 거점 id → 마지막 포화 출진 날 키(0 = 없음). SAT_COOLDOWN일 동안 쉼 */
static __declspec(thread) uint8_t *g_sat_fh[64];
static uint8_t *attacked_gate(uint8_t *lead, uint8_t *target, int fid, float *dc, float *dg); /* 빌드 51 */
static const char *siege_rules(uint8_t *fh, uint8_t *target, Member *m, int c, int *k, int enemy, int e0, int avail, int dist,
                               int lead_hex, uint8_t **items, int n, int foe_sol); /* F: 관문 수비·열세 하한. 적용한 규칙 이름 또는 "" */

static void build_plan(void *ctx)
{
    uint8_t *items[MAX_CAND];
    uint8_t *fh = bb_obj(ctx, BT_FOOTHOLD);
    int enemy, reserve, c;
    int n = list_items(bb_obj(ctx, BT_PERSONS), items, MAX_CAND);
    g_plan.stage = 0;
    if (!n || !valid(fh) || !bb_int(ctx, BT_ENEMY, &enemy) || !bb_int(ctx, BT_RESERVE, &reserve))
        return;
    int ao = 0, at = 0;
    /* 빌드 48 수상 우위: 열세 하한을 쓰지 않고 그룹을 그대로 보낸다(상륙하면 naval_landing이 다시 잰다).
       빌드 52(2026-11-05 사용자 결정): 수상 우위면 요격 예비를 NAVAL_ADV_RESERVE까지 줄인다(빌드 51 판 2 시상: 성 5천~7천 −
       예비 4,000 = 1,665~2,665 < 최소 3,000이라 706이 0명, 219.8.11 함락) */
    g_plan_adv = g_v3 && naval_adv(ctx, &ao, &at);
    if (g_plan_adv && reserve > NAVAL_ADV_RESERVE) {
        reserve = NAVAL_ADV_RESERVE;
        /* 빌드 62: 게임 출진 조건([1766] 잔여 ≥ R)과 [1741]도 블랙보드 R을 읽는다. 써 두지 않으면 남는 병력이 4,000 미만인
           계획은 나가지 않았다(빌드 52·61 판: 23번 중 23번). 거점마다 [1512]가 다시 정한다 */
        bb_set(ctx, BT_RESERVE, reserve);
    }
    /* 빌드 71(2026-11-07 사용자 결정): 지원 요격(나가는 거점 ≠ 지킬 목표)은 자기 거점 THREAT_UNIT_R칸 안 다른 적 부대
       병사(지금 막으러 가는 적 무리는 뺌)만큼 더 남긴다. 빌드 70 흑태자 상급 판 194.5.11 광릉 → 하비: 114가 1만 500을
       데려가 광릉 1만 4천 9백 → 4천 4백, 두 달 밖에서 싸우는 동안 동탁이 광릉을 쳤다. 자기 거점 요격은 그대로 */
    uint8_t *tgt0 = bb_obj(ctx, BT_TARGET);
    int sup_res = 0;
    if (g_v3 && valid(tgt0) && tgt0 != fh) {
        uint8_t *thr[MAX_CAND];
        int nt = list_items(bb_obj(ctx, BT_THREAT), thr, MAX_CAND);
        sup_res = near_foe_except(fh, force_id(items[0]), THREAT_UNIT_R, thr, nt, bb_obj(ctx, BT_LEAD));
        if (sup_res > reserve) {
            reserve = sup_res;
            bb_set(ctx, BT_RESERVE, reserve);
            if (*(int *)(fh + 0x3C) - reserve < STEP) {
                /* 남는 병력이 없으면 원작 몫으로 넘기지 않고 빈 계획으로 둔다(planned_share가 모두 0, 도시 우선과 같은 효과) */
                g_plan.n = 0;
                g_plan.stage = 2;
                return;
            }
        }
    }
    int avail = *(int *)(fh + 0x3C) - reserve;
    Member *m = g_plan.m;
    int k0 = split(m, rank_group(m, items, n, GROUP_MAX, &c), avail, enemy, 0);
    if (!k0)
        return;
    /* 열세 단계: 가용을 다 써도 E에 못 미치면 위험에 내놓는 무장 수를 줄인다. 빌드 49 나(2026-11-04): 그룹 10은 쓰지 않는다
       (빌드 48b 판 건업 219.8.21 66% → 손권 혼자, 강릉 219.7.11 65% → 한 명). 인원은 아래 split(가·다)이 정한다 */
    int pct = enemy > 0 ? (int)(group_power(m, k0) * 100 / enemy) : 100;
    int kmax = g_v3 || pct >= EVEN_PCT ? GROUP_MAX : pct >= WEAK_PCT ? EVEN_MAX : 1;
    uint8_t *threat[MAX_CAND];
    float foe;
    int foe_sol, listed = list_items(bb_obj(ctx, BT_THREAT), threat, MAX_CAND), enemies = enemy_count(ctx, listed, &foe, &foe_sol);
    /* 각개격파 방지: 부대마다 적 선두 부대 전력의 LEAD_PCT% 이상. 빌드 49 가: 적이 2부대 이상이면 맞추는 인원이 없을 때
       1명 대신 몫이 되는 가장 큰 인원(split 다), 그것도 안 되면 1명 */
    uint8_t *lead = bb_obj(ctx, BT_LEAD);
    float lead_power = valid(lead) ? est_power(lead) : 0;
    g_split_multi = g_v3 && enemies >= 2;
    int k = split(m, k0 < kmax ? k0 : kmax, avail, enemy, lead_power * LEAD_PCT / 100);
    g_split_multi = 0;
    if (!k)
        return;
    char meet[160] = "";
    int ground = 0, mh = -1;
    float adj_o = 1, adj_t = 1;
    g_plan_adj = 1;
    if (g_v3) {
        __try {
            ground = meet_enemy_ground(ctx, fh, meet, sizeof meet, &mh);
            g_plan_adj = adj_ratio_at(fh, lead, mh, &adj_o, &adj_t);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ground = 0, meet[0] = 0, g_plan_adj = 1;
        }
        if (g_plan_adj != 1)
            _snprintf_s(meet + strlen(meet), sizeof meet - strlen(meet), _TRUNCATE, ", 인접 ×%.2f(우리 %.2f / 적 %.2f)", g_plan_adj,
                        adj_o, adj_t);
    }
    assign_forms(m, k, enemies, foe, ground);

    /* 목표 거점 ~ 적 선두 칸 거리(조건 0x05와 같은 방법, 칸을 못 구하면 -1) */
    int(__fastcall *var_hex)(int, uint32_t, void *) = (int(__fastcall *)(int, uint32_t, void *))RVA(FN_VAR_HEX);
    int ht = var_hex(5, BT_TARGET, ctx), hl = var_hex(3, BT_LEAD, ctx);
    int dist = ht == HEX_NONE || hl == HEX_NONE ? -1 : ((int(__fastcall *)(int, int))RVA(FN_HEX_DIST))(ht, hl);
    uint8_t *target = bb_obj(ctx, BT_TARGET);
    int e0 = g_v3 ? first_e(target, lead, enemy) : enemy;
    /* 빌드 51: 적 선두가 우리 관문을 치는 중이면 관문 위험도가 도시의 GATE_PRIO_PCT% 를 넘을 때만 그 관문 지원 규칙으로,
       아니면 도시 우선(나가지 않음). 빌드 50 판 한중 220.10.11: 장비(어린)가 관문 뒤에 서서 녹음 */
    uint8_t *sr_target = target;
    int sr_dist = dist;
    int city_first = 0;
    g_no_enter = 0;
    if (g_v3 && valid(target) && *(uint16_t *)(target + 0x4E) <= 0x33) {
        float dc = 0, dg = 0;
        uint8_t *ag = attacked_gate(lead, target, force_id(m[0].person), &dc, &dg);
        if (ag) {
            if (dg * 100 > dc * GATE_PRIO_PCT) {
                int hg = CALL(ObjFn, FN_HEX)(ag);
                sr_target = ag, g_no_enter = 1;
                sr_dist = hl == HEX_NONE ? -1 : ((int(__fastcall *)(int, int))RVA(FN_HEX_DIST))(hg, hl);
            } else {
                for (int i = 0; i < k; i++)
                    m[i].share = 0;
                city_first = 1;
            }
        }
    }
    if (!city_first && g_v3)
        siege_rules(fh, sr_target, m, c, &k, enemy, e0, avail, sr_dist, hl, items, n, foe_sol);
    g_no_enter = 0;
    g_plan.n = k;
    g_plan.stage = 2; /* 몫이 모두 0이어도 계획은 둔다: 그 거점은 출진하지 않는다 */
    if (g_plan_adv && valid(lead))
        for (int i = 0; i < k; i++)
            if (m[i].share > 0)
                nvt_add(*(uint16_t *)(m[i].person + 0x10), *(uint16_t *)(lead + 0x12), force_id(m[i].person),
                        valid(target) ? CALL(ObjFn, FN_HEX)(target) : -1);
}

/* 계획 순서대로 평가값(무장 +8, ListSortEvaluation이 내림차순으로 정렬)을 덮어쓴다 */
static void apply_order(void *ctx)
{
    uint8_t *items[MAX_CAND];
    int n = list_items(bb_obj(ctx, BT_PERSONS), items, MAX_CAND);
    for (int i = 0; i < n; i++) {
        int score = -(1 << 20);
        for (int j = 0; j < g_plan.n; j++)
            if (g_plan.m[j].person == items[i])
                score = (1 << 30) - j;
        *(int *)(items[i] + 8) = score;
    }
}

static int planned_share(uint8_t *person)
{
    for (int j = 0; j < g_plan.n; j++)
        if (g_plan.m[j].person == person)
            return g_plan.m[j].share;
    return 0;
}

static int __fastcall nointercept_hook(uint8_t *node, void *ctx, void *a3)
{
    int r = CALL(NodeFn, FN_NOINTERCEPT)(node, ctx, a3);
    g_plan.stage = *(uint32_t *)(node + 0x38) == BT_PERSONS ? 1 : 0;
    g_plan.n = 0;
    return r;
}

static int __fastcall sort_hook(uint8_t *node, void *ctx, void *a3)
{
    if (g_plan.stage && *(int *)(node + 0x38) == 4 && *(uint32_t *)(node + 0x3C) == BT_PERSONS && *(int *)(node + 0x40) == 1) {
        __try {
            if (g_plan.stage == 1)
                build_plan(ctx);
            if (g_plan.stage == 2)
                apply_order(ctx);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_plan.stage = 0;
            g_plan.n = 0;
            plog("[요격] 출진 계획 중 예외. 원래 동작으로");
        }
    }
    return CALL(NodeFn, FN_SORT)(node, ctx, a3);
}

static int __fastcall soldiers_hook(uint8_t *node, void *ctx, void *a3)
{
    int r = CALL(NodeFn, FN_SOLDIERS)(node, ctx, a3);
    if (g_plan.stage == 2) {
        uint8_t *person = bb_obj(ctx, *(uint32_t *)(node + 0x38));
        Member *mb = plan_member(person);
        if (mb)
            mb->used = 1; /* 진형은 이미 정함 */
        if (person && bb_set(ctx, *(uint32_t *)(node + 0x40), planned_share(person)))
            r = 1;
    }
    return r;
}

/* [1725] 병사 채우기: 계획이 있으면 하지 않는다 */
static int __fastcall plusvar_hook(uint8_t *node, void *ctx, void *a3)
{
    if (g_plan.stage == 2 && *(uint32_t *)(node + 0x38) == BT_SOLDIERS && *(uint32_t *)(node + 0x3C) == BT_FILL)
        return 1;
    return CALL(NodeFn, FN_PLUSVAR)(node, ctx, a3);
}

/* 조건 0x14 "변수 > 상수": 특급·궁극 근접 문턱 공군 ≤ 800 → 799, 계획이 있으면 [1729] 관계 분배 끔 */
static char __fastcall cond_gtk(void *ctx, void *node, const int *args, int argc)
{
    if (argc == 2 && (uint32_t)args[0] == BT_LEAD_ATK && args[1] == 800) {
        int a[2] = {args[0], 799};
        return CALL(CondFn, FN_COND_GTK)(ctx, node, a, argc);
    }
    if (argc == 2 && (uint32_t)args[0] == BT_RELATED_N && args[1] == 0 && g_plan.stage == 2)
        return 0;
    return CALL(CondFn, FN_COND_GTK)(ctx, node, args, argc);
}

/* ---------- C. 공격 동반 출진 ---------- */

typedef uint64_t(__fastcall *AtkSortFn)(void *list, int a, int b, int c, int d, int e);
typedef uint64_t(__fastcall *AtkSpecFn)(uint8_t *node, void *target, uint8_t *person, int *spec, int avail);

static uint8_t *foothold_of(uint8_t *person)
{
    return ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(game_root(), *(uint16_t *)(person + 0xE2));
}

/* 목록 원소를 items 순서로 덮어쓴다(같은 원소의 순열일 때만) */
static void list_rewrite(void *list, uint8_t *const *items, int n)
{
    uint32_t *h = list ? *(uint32_t **)((uint8_t *)list + 8) : NULL;
    void **tbl = *(void ***)RVA(LIST_NODES);
    if (!h || !tbl || *h >= *(uint32_t *)RVA(LIST_COUNT))
        return;
    int i = 0;
    for (void **node = tbl[*h]; node && i < n; node = (void **)node[1])
        node[0] = items[i++];
}

/* 일반 무장 단계의 첫 사양 호출: person은 목록에서 막 꺼낸 무장, avail은 거점 병사 − 예비, target은 공격 대상 거점 */
/* 공격 노드에서 거점마다 이미 만든 부대(공성 / 그 밖)의 수와 병사. 거점 정렬·노드 시작에서 비운다 */
static __declspec(thread) struct {
    uint8_t *fh;
    int field_n, field_s, siege_n, siege_s;
} g_sg;

/* 무장이 거점 fh에서 고를 수 있는 공성 진형(투석 → 정란 → 충차, 원작 B단계 0x4583B~0x458B2와 같은 순서·비용 < 거점 값) */
static int siege_form_of(uint8_t *person, int fh_value)
{
    static const int ORDER[] = {10, 8, 9};
    for (int i = 0; i < (int)_countof(ORDER); i++) {
        uint8_t *f = formation_by_id(ORDER[i]);
        if ((*(uint32_t *)(person + 0x12C) >> ORDER[i] & 1) && valid(f) && *(uint16_t *)(f + 0x9A) < fh_value)
            return ORDER[i];
    }
    return 0;
}

/* 그룹 10 공격 편성: 그룹 안에서 공성 역할을 정하고 병사를 옮겨, 이 거점이 이번 공격에서 낼 공성 아닌 부대(선봉 등 이미 만든
   부대 포함)의 수와 병사가 공성 부대의 SIEGE_ESCORT배 이상이 되게 한다. 합계는 되도록 그대로 둔다.
   - 후보: 원작 B단계는 만든 부대 ≥ 1, 노드 +0xA8 == 0, 통솔 ≥ 60이면 가진 공성 진형으로 바꾸므로(0x45819) 그런 무장.
     지금 사양을 정하는 무장(먼저 나감)은 뺀다. 공성 수 s ≤ (일반 기존 + k − 2 × 공성 기존) / 3, 순위 낮은 쪽부터.
   - 병사: 공성 몫에서 일반 몫으로 옮기고(일반은 지휘 상한까지, 공성은 3000까지), 그래도 모자라면 공성 몫을 줄인다(성에 남음).
     3000 밑으로 내려가야 하면 그 무장을 일반 역할로 돌린다.
   - 순서: 일반 역할이 먼저, 공성 역할이 나중(atk_plan이 이 순서로 목록을 다시 쓴다) */
static void atk_siege_plan(uint8_t *node, uint8_t *fh, uint8_t *current, Member *m, int k, char *note, size_t nsz)
{
    if (g_sg.fh != fh)
        memset(&g_sg, 0, sizeof g_sg), g_sg.fh = fh;
    int fh_value = (*(int(__fastcall **)(void *))(*(uint8_t **)fh + 0x90))(fh), moved = 0, cut = 0;
    int smax = (g_sg.field_n + k - 2 * g_sg.siege_n) / 3, s = 0;
    for (int i = 0; i < k; i++)
        m[i].siege = 2;
    for (int i = k - 1; i >= 0 && s < smax; i--)
        if (m[i].person != current && *(int *)(node + 0xA8) == 0 && m[i].lead >= 60 && siege_form_of(m[i].person, fh_value))
            m[i].siege = 1, s++;
    for (int guard = 0; s > 0 && guard < ATK_GROUP_MAX + 1; guard++) {
        int fs = g_sg.field_s, ss = g_sg.siege_s;
        for (int i = 0; i < k; i++)
            *(m[i].siege == 1 ? &ss : &fs) += m[i].share;
        if (fs >= SIEGE_ESCORT * ss)
            break;
        /* x를 옮기면 fs + x ≥ 2(ss − x) → x ≥ (2ss − fs) / 3 */
        int want = (SIEGE_ESCORT * ss - fs + SIEGE_ESCORT) / (SIEGE_ESCORT + 1);
        for (int i = k - 1; i >= 0 && want > 0; i--) {
            if (m[i].siege != 1)
                continue;
            for (int j = 0; j < k && want > 0 && m[i].share > 3000; j++) {
                if (m[j].siege != 2 || m[j].share >= m[j].cap)
                    continue;
                int x = m[j].cap - m[j].share;
                x = x < want ? x : want;
                x = x < m[i].share - 3000 ? x : m[i].share - 3000;
                m[j].share += x, m[i].share -= x, want -= x, moved += x;
            }
        }
        fs = g_sg.field_s, ss = g_sg.siege_s;
        for (int i = 0; i < k; i++)
            *(m[i].siege == 1 ? &ss : &fs) += m[i].share;
        if (fs >= SIEGE_ESCORT * ss)
            break;
        int over = ss - fs / SIEGE_ESCORT;
        for (int i = k - 1; i >= 0 && over > 0; i--) {
            if (m[i].siege != 1)
                continue;
            int x = m[i].share - 3000 < over ? m[i].share - 3000 : over;
            if (x > 0)
                m[i].share -= x, over -= x, cut += x;
        }
        if (over > 0) /* 3000까지 줄여도 모자라면 마지막 공성 역할을 일반으로 */
            for (int i = k - 1; i >= 0; i--)
                if (m[i].siege == 1) {
                    m[i].siege = 2, s--;
                    break;
                }
    }
    /* 일반 먼저: 안정 분할 */
    Member tmp[ATK_GROUP_MAX];
    int t = 0;
    for (int i = 0; i < k && k <= ATK_GROUP_MAX; i++)
        if (m[i].siege != 1)
            tmp[t++] = m[i];
    for (int i = 0; i < k && k <= ATK_GROUP_MAX; i++)
        if (m[i].siege == 1)
            tmp[t++] = m[i];
    if (t == k)
        memcpy(m, tmp, sizeof *m * k);
    if (s > 0 || moved || cut) {
        int len = 0;
        for (int i = 0; i < k && len >= 0 && len < (int)nsz - 40; i++)
            len += _snprintf_s(note + len, nsz - len, _TRUNCATE, " %u(%s %d)", *(uint16_t *)(m[i].person + 0x10),
                               m[i].siege == 1 ? "공성" : "일반", m[i].share);
        if (len >= 0 && len < (int)nsz - 80)
            _snprintf_s(note + len, nsz - len, _TRUNCATE, ", 이미 만든 일반 %d개 %d명·공성 %d개 %d명, 옮김 %d 줄임 %d", g_sg.field_n,
                        g_sg.field_s, g_sg.siege_n, g_sg.siege_s, moved, cut);
    }
}

/* 빌드 63: 원작이 이 거점에서 낼 전력 추정. 목록 순서대로 min(지휘 상한, 남은 가용)(ATK_MIN_SHARE 미만 건너뜀, 선봉은 first_amt),
   계획과 같은 선형 근사(무장 power × 병사 / REF_SOLDIERS)로 need를 넘으면 멈춘다 */
static float atk_orig_est(const Member *m, int c, uint8_t **items, int n, int avail, int need, int first_amt)
{
    float pw = 0;
    int left = avail;
    for (int i = 0; i < n && pw < (float)need; i++) {
        const Member *x = NULL;
        for (int j = 0; j < c && !x; j++)
            if (m[j].person == items[i])
                x = &m[j];
        if (!x)
            continue;
        int amt = i == 0 && first_amt > 0 ? first_amt : x->cap < left ? x->cap : left;
        if (amt < ATK_MIN_SHARE)
            continue;
        pw += x->power * amt / REF_SOLDIERS;
        left -= amt;
    }
    return pw;
}

/* 빌드 63: 몫이 ATK_MIN_SHARE 미만인 무장(원작이 건너뛰고 다음 후보를 원작 양으로 보냄, 빌드 62 판 강릉 219.5.1)을 그룹 뒤로
   빼고 다시 나눈다. keep0이면 m[0](선봉)은 빼지 않는다 */
static int atk_split(Member *m, int k, int avail, int want, int keep0)
{
    k = split(m, k, avail, want, 0);
    while (k > 1) {
        int lo = -1;
        for (int i = keep0 ? 1 : 0; i < k; i++)
            if (m[i].share < ATK_MIN_SHARE && (lo < 0 || m[i].share < m[lo].share))
                lo = i;
        if (lo < 0)
            break;
        Member t = m[lo];
        for (int i = lo; i < k - 1; i++)
            m[i] = m[i + 1];
        m[k - 1] = t;
        m[k - 1].share = 0;
        k = split(m, k - 1, avail, want, 0);
    }
    return k;
}

/* 공격 계획. van_spec이 있으면 A단계 선봉 생성 때 부른 것이다(빌드 63, 2026-11-06 사용자 결정): 선봉 person을 그룹 1번으로 넣어
   필요 + 지원 예상을 지휘 상한 비례로 나누고 선봉 병사(van_spec[2])를 몫으로 바꾼다. 없으면 B단계 첫 사양 호출 때(선봉이 없는
   모드·거점). 계획 전력이 원작이 낼 전력(필요까지)에 못 미치면 지휘 상한 × 전력이 큰 무장으로 다시 고르고, 그래도 모자라면 계획을
   쓰지 않는다(원작 끝 취소: 만든 전력 합 < 처음 필요면 공격 전체 취소, 빌드 62 판 84~97% 취소 10번) */
static void atk_plan(uint8_t *node, uint8_t *target, uint8_t *person, int avail, int *van_spec)
{
    uint8_t *items[MAX_CAND], *rest[MAX_CAND];
    int need = *(int *)(node + ATK_NEED), c, van = van_spec != NULL;
    uint8_t *fh = foothold_of(person);
    g_atk.stage = 0;
    items[0] = person;
    int n = 1, vpos = -1; /* vpos: 선봉 경로에서 목록 안 선봉의 자리(원작이 선봉을 만든 뒤 그 자리를 지운다) */
    {
        uint8_t *li[MAX_CAND];
        int ln = list_items(g_atk.list, li, MAX_CAND - 1);
        for (int i = 0; i < ln && n < MAX_CAND; i++)
            if (li[i] != person)
                items[n++] = li[i];
            else if (vpos < 0)
                vpos = i;
    }
    if (need <= 0 || !valid(fh) || avail < ATK_MIN_SHARE)
        return;
    int k0 = rank_group(g_atk.m, items, n, ATK_GROUP_MAX, &c);
    if (van && k0 > 0) { /* 선봉을 그룹 1번으로 */
        int vi = -1;
        for (int i = 0; i < c; i++)
            if (g_atk.m[i].person == person)
                vi = i;
        if (vi < 0)
            return;
        Member t = g_atk.m[vi];
        for (int i = vi; i > 0; i--)
            g_atk.m[i] = g_atk.m[i - 1];
        g_atk.m[0] = t;
        if (vi >= k0 && k0 < ATK_GROUP_MAX)
            k0++;
    }
    float naval = naval_plan(fh, target, person); /* 그룹 10: 강 건너면 계획 전력도 만든 부대처럼 수상 배율 */
    for (int i = 0; i < c; i++)
        g_atk.m[i].power *= naval;
    /* 그룹 10: 게임 필요 전력(+0x84)은 그대로 두고 채울 목표에만 목표 주변 지원 예상을 더한다. 노드에서 한 번 세고
       거점마다 필요를 넘겨 채운 만큼 줄인다 */
    float extra = 0;
    if (g_v3) {
        if (g_atk_extra < 0) {
            float ppw = 0;
            for (int i = 0; i < k0; i++)
                ppw += g_atk.m[i].power / REF_SOLDIERS / (float)k0;
            g_atk_extra = (float)atk_support(target, force_id(person), NULL, 0, NULL) * ppw;
        }
        extra = g_atk_extra;
    }
    g_fill_cap = 1; /* 빌드 63: 공격 몫은 지휘 상한 비례 */
    /* 빌드 63: 원작이 낼 전력(선봉 원래 병사 포함)보다 덜 보내지 않는다(base_o). 빌드 67(2026-11-07 사용자 결정): 지원 몫 예비를
       받지 않는 바닥(base)은 필요와 원작 예상 사이로 낮춘다. 원작 예상은 무장을 지휘 상한까지 채우는 원작 습관이라 필요의 1.25~1.45배
       (중앙값)이고, 그 몫이 바닥 안에 있어 거점을 비웠다(빌드 65 판 강하 218.12.1: 필요 5,331·원작 예상 8,157, 1만 9천 8백 → 3백).
       w = 0.5 + 0.5 × (r − 1) / (r − 1 + FLOOR_TILT_C): r = 1이면 중간값, 차이가 클수록 원작 쪽. 바닥과 원작 몫 사이, 그리고 그 위
       지원 예상 몫은 거점에 support_keep을 남기는 안에서만 보낸다. 목표(want)와 지원 예상 소비 계산은 원작 몫(base_o) 기준 그대로 */
    float orig = atk_orig_est(g_atk.m, c, items, n, avail, need, van ? van_spec[2] : 0);
    int base_o = orig > (float)need ? (int)orig : need, want = need + (int)extra > base_o ? need + (int)extra : base_o;
    int base = need;
    float tilt = 0;
    if (orig > (float)need && need > 0) {
        float r = orig / (float)need;
        tilt = 0.5f + 0.5f * (r - 1.0f) / (r - 1.0f + FLOOR_TILT_C);
        base = need + (int)(tilt * (orig - (float)need));
    }
    /* 빌드 62(2026-11-06 사용자 결정): 지원 예상으로 더 채우는 몫은 출발 거점에 support_keep만큼 남기는 병력 안에서만
       (빌드 61 판 강릉 218.6.21: 관우 1만 7천에 183 8천이 붙어 2만 5천 → 1,946, 219.1.1 함락). 빌드 67부터 바닥 위 원작 몫에도 */
    int keep = -1, kfh = 0, kun = 0, avail_x = avail;
    if (g_v3 && want > base) {
        int kn = atk_split(g_atk.m, k0, avail, base, van), sn = 0;
        for (int i = 0; i < kn; i++)
            sn += g_atk.m[i].share;
        if (base < base_o)
            atk_split(g_atk.m, k0, avail, base_o, van); /* 원작 몫 기준으로도 한 번 나눈다(그룹 순서가 바뀔 수 있음) */
        keep = support_keep(fh, force_id(person), &kfh, &kun);
        int room = *(int *)(fh + 0x3C) - keep;
        avail_x = room < avail ? room : avail;
        if (avail_x < sn)
            avail_x = sn;
    }
    int k = atk_split(g_atk.m, k0, avail_x, want, van);
    float bar = orig < (float)need ? orig : (float)need;
    if (k && group_power(g_atk.m, k) < bar * 0.99f) {
        int lo = van ? 1 : 0;
        for (int i = lo + 1; i < c; i++) /* 낼 수 있는 최대 전력(전력 × min(상한, 가용)) 내림차순, 안정 */
            for (int j = i; j > lo; j--) {
                float x = g_atk.m[j].power * (float)(g_atk.m[j].cap < avail ? g_atk.m[j].cap : avail);
                float y = g_atk.m[j - 1].power * (float)(g_atk.m[j - 1].cap < avail ? g_atk.m[j - 1].cap : avail);
                if (x <= y)
                    break;
                Member t = g_atk.m[j];
                g_atk.m[j] = g_atk.m[j - 1];
                g_atk.m[j - 1] = t;
            }
        int ka = c < ATK_GROUP_MAX ? c : ATK_GROUP_MAX;
        k = atk_split(g_atk.m, ka, avail_x, want, van);
        if (k && group_power(g_atk.m, k) < bar * 0.99f && avail_x < avail)
            k = atk_split(g_atk.m, ka, avail, want, van); /* 원작 필요를 채우려면 지원 몫 예비 없이 */
    }
    g_fill_cap = 0;
    if (k && (group_power(g_atk.m, k) < bar * 0.99f || (van && (g_atk.m[0].person != person || g_atk.m[0].share < ATK_MIN_SHARE))))
        k = 0;
    if (!k) {
        return;
    }
    char siege_note[300] = "";
    if (g_v3) {
        __try {
            atk_siege_plan(node, fh, person, g_atk.m, k, siege_note, sizeof siege_note);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            for (int i = 0; i < k; i++)
                g_atk.m[i].siege = 0;
        }
        float planned = group_power(g_atk.m, k), over = planned - (float)need, over_x = planned - (float)base_o;
        g_atk_extra -= over_x > 0 ? over_x : 0;
        if (g_atk_extra < 0)
            g_atk_extra = 0;
        /* 원작 루프는 +0x84가 0 이하가 되면 멈춰, 지원 예상까지 나눈 그룹의 뒤쪽 무장이 사양 호출조차 받지 못했다(빌드 31 판
           강릉 첫 공격: 3명 중 2명, 1만 5천 5백). 그룹 전력이 필요를 넘는 만큼(+20% 여유) +0x84를 올려 두고, 그룹 밖 무장은
           원래 필요가 찼으면 막는다(atk_spec_hook). 다음 거점 정렬과 노드 끝에서 뺀다. 선봉 포함 계획은 선봉 생성 전에
           올리므로 선봉 뒤에도 동반 무장 사양 호출이 이어진다(빌드 60·62 판 관우 혼자 강하) */
        if (over > 0) {
            g_atk_bump = (int)(over + planned * 0.2f) + 1;
            *(int *)(node + ATK_NEED) += g_atk_bump;
        }
    }
    g_atk.n = k;
    g_atk.fh = fh;
    if (van) {
        g_atk.m[0].used = 1;
        van_spec[2] = g_atk.m[0].share;
    }
    g_atk.stage = 2;
    /* 남은 목록: 그룹(계획 순서) 다음 나머지(원래 순서). 목록이 MAX_CAND를 넘을 수 있으면 손대지 않는다 */
    if (n < MAX_CAND) {
        int r = 0;
        for (int j = 0; j < k; j++)
            if (g_atk.m[j].person != person)
                rest[r++] = g_atk.m[j].person;
        for (int i = 1; i < n; i++) {
            int in = 0;
            for (int j = 0; j < k; j++)
                in |= g_atk.m[j].person == items[i];
            if (!in)
                rest[r++] = items[i];
        }
        /* 빌드 64: 선봉 경로는 선봉을 원래 자리에 둔 채 나머지만 그룹 순으로(빌드 63 판: 선봉을 빼고 다시 써서 원작이
           선봉 대신 첫 동반 무장을 지움, 96번). 선봉이 목록에 없으면 다시 쓰지 않는다(그룹 밖 무장은 사양 훅이 0을 준다) */
        if (r == n - 1 && !van)
            list_rewrite(g_atk.list, rest, r);
        else if (r == n - 1 && vpos >= 0 && vpos <= r) {
            for (int i = r; i > vpos; i--)
                rest[i] = rest[i - 1];
            rest[vpos] = person;
            list_rewrite(g_atk.list, rest, r + 1);
        }
    }
}

/* 원래 정렬 키(0xD32C0)의 상태 감점을 A에 붙인 값 */
static float van_key(uint8_t *p)
{
    float k = ability_a(p);
    uint16_t st = *(uint16_t *)(p + 0x15E);
    if (st & 0x2000)
        k = k * 3 / 4;
    if (st & 0x4000)
        k = k / 2;
    return k;
}

/* 그룹 10: 선봉 단계 앞 목록을 A 내림차순으로(같으면 통솔, 그다음 원래 순서) */
static void van_sort(void *list)
{
    uint8_t *items[MAX_CAND];
    float key[MAX_CAND];
    int lead[MAX_CAND];
    int n = list_items(list, items, MAX_CAND);
    if (n < 2 || n >= MAX_CAND)
        return;
    for (int i = 0; i < n; i++) {
        if (!valid(items[i]))
            return;
        key[i] = van_key(items[i]);
        lead[i] = ability(items[i], 0);
    }
    for (int i = 1; i < n; i++) { /* 안정 삽입 정렬 */
        uint8_t *it = items[i];
        float k = key[i];
        int l = lead[i], j = i - 1;
        for (; j >= 0 && (key[j] < k || (key[j] == k && lead[j] < l)); j--) {
            items[j + 1] = items[j];
            key[j + 1] = key[j];
            lead[j + 1] = lead[j];
        }
        items[j + 1] = it;
        key[j + 1] = k;
        lead[j + 1] = l;
    }
    list_rewrite(list, items, n);
}

static uint8_t *root_slot(int table, int i);

static void atk_unbump(void)
{
    if (g_atk_bump && valid(g_atk_node))
        *(int *)(g_atk_node + ATK_NEED) -= g_atk_bump;
    g_atk_bump = 0;
}

static uint64_t __fastcall atk_sort_hook(void *list, int a, int b, int c, int d, int e)
{
    atk_unbump();
    g_sg.fh = NULL;
    uint64_t r = CALL(AtkSortFn, FN_ATK_SORT)(list, a, b, c, d, e);
    if (g_v3) {
        uint8_t *ret = (uint8_t *)_ReturnAddress();
        int mode8 = ret == RVA(RET_ATK_SORT_8);
        if (mode8 || ret == RVA(RET_ATK_SORT_16) || ret == RVA(RET_ATK_SORT_9) || ret == RVA(RET_ATK_SORT_A)) {
            /* 빌드 53: 모드 8도 출진 판단·위협 예비를 건다(빌드 52 판 유비 강릉 → 강하 220.5.11이 판단 없이 나감).
               모드 8은 정렬 뒤 무장마다 +0x88을 다시 읽는다(0x49265·0x4952F·0x49619). 선봉 단계는 없다 */
            __try {
                if (!mode8)
                    van_sort(list);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                plog("[공격] 선봉 정렬 중 예외. 원래 순서로");
            }
            __try {
                atk_threat_reserve(list);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                plog("[공격] 위협 예비 계산 중 예외. 원래 예비로");
            }
        }
    }
    g_atk.stage = 1;
    g_atk.n = 0;
    g_atk.list = list;
    g_atk.van_id = -1;
    return r;
}

static uint64_t __fastcall atk_spec_hook(uint8_t *node, void *target, uint8_t *person, int *spec, int avail)
{
    uint64_t r = CALL(AtkSpecFn, FN_ATK_SPEC)(node, target, person, spec, avail);
    __try {
        if (g_atk.stage == 1)
            atk_plan(node, (uint8_t *)target, person, avail, NULL);
        if (g_atk.stage == 2 && foothold_of(person) == g_atk.fh) {
            int j = 0, left = 0;
            while (j < g_atk.n && g_atk.m[j].person != person)
                j++;
            for (int i = 0; i < g_atk.n; i++)
                left |= !g_atk.m[i].used && i != j;
            if (j < g_atk.n) {
                g_atk.m[j].used = 1;
                if (spec[2] > g_atk.m[j].share)
                    spec[2] = g_atk.m[j].share;
            } else if (left) {
                spec[2] = 0; /* 그룹이 다 나간 뒤에 채운다 */
            } else if (g_atk_bump > 0 && *(int *)(node + ATK_NEED) - g_atk_bump <= 0) {
                spec[2] = 0; /* 원래 필요는 찼다. 올려 둔 몫은 그룹 몫이라 그룹 밖은 내보내지 않는다 */
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_atk.stage = 0;
        g_atk.n = 0;
        plog("[공격] 출진 계획 중 예외. 원래 동작으로");
    }
    return r;
}

typedef uint64_t(__fastcall *AtkTacticsFn)(uint8_t *person, uint8_t *formation, int *spec);
typedef uint64_t(__fastcall *UnitCreateFn)(int *spec, int flag);
static int is_person(void *o);

static int is_siege(int id) { return id >= 8 && id <= 10; }

static void siege_count(uint8_t *fh, int form, int soldiers)
{
    if (g_sg.fh != fh)
        memset(&g_sg, 0, sizeof g_sg), g_sg.fh = fh;
    if (is_siege(form))
        g_sg.siege_n++, g_sg.siege_s += soldiers;
    else
        g_sg.field_n++, g_sg.field_s += soldiers;
}

/* 공성 아닌 육상 진형: 탱커 우선, 없으면 그 밖(안행·추행·공성 제외) 점수 최대. 비용은 거점 값 이하 */
static int field_form(uint8_t *person, int max_cost)
{
    int t = best_form(person, 1, max_cost), bs = -1;
    for (int id = 1; id <= FORM_LAST && !t; id++) {
        uint8_t *f = formation_by_id(id);
        if (is_siege(id) || id == F_ANHAENG || id == F_CHUHAENG || !valid(f) || *(uint16_t *)(f + 0x9A) > max_cost)
            continue;
        int sc = form_score(person, id);
        if (sc > bs)
            bs = sc, t = id;
    }
    return t;
}

/* 공성 진형으로 정해진 무장: 이미 나간 공성 아닌 부대가 (공성 + 이번)의 SIEGE_ESCORT배(수·병사) 이상이 아니면 공성 아닌
   진형으로 바꾸고, 그런 진형이 없으면 병사를 맞는 만큼 줄인다(3000 미만이면 원작이 건너뜀). 바꾼 진형 객체를 돌려준다 */
static uint8_t *siege_escort(uint8_t *person, uint8_t *fh, uint8_t *formation, int *spec)
{
    if (g_sg.fh != fh)
        memset(&g_sg, 0, sizeof g_sg), g_sg.fh = fh;
    int s = spec[2];
    /* 빌드 50(2026-11-05 사용자 결정): 이미 만든 일반 부대에 계획에서 아직 안 나간 일반 몫을 더해 센다(빌드 49 판 B 관문 57:
       선봉 하나만 있을 때 공성 셋이 병사 0). 관문이 목표면 관문 옆 칸이 좁고 내구는 공성만 깎으므로 호위 비를 1로 */
    int fn = g_sg.field_n, fs = g_sg.field_s, esc = SIEGE_ESCORT;
    if (g_atk.stage == 2 && g_atk.fh == fh)
        for (int i = 0; i < g_atk.n; i++)
            if (!g_atk.m[i].used && g_atk.m[i].siege != 1 && g_atk.m[i].share >= 3000 && g_atk.m[i].person != person)
                fn++, fs += g_atk.m[i].share;
    uint8_t *tg = valid(g_atk_node) ? *(uint8_t **)(g_atk_node + 0x60) : NULL;
    if (valid(tg) && *(uint16_t *)(tg + 0x4E) > 0x33)
        esc = 1;
    if (!is_siege(spec[3]) || (fn >= esc * (g_sg.siege_n + 1) && fs >= esc * (g_sg.siege_s + s)))
        return formation;
    int cap = (*(int(__fastcall **)(void *))(*(uint8_t **)fh + 0x90))(fh);
    int t = field_form(person, cap < 0 ? 0 : cap);
    uint8_t *f = formation_by_id(t);
    if (t && valid(f)) {
        spec[3] = t;
    } else {
        int room = fn >= esc * (g_sg.siege_n + 1) ? fs / esc - g_sg.siege_s : 0;
        spec[2] = room < 0 ? 0 : room < s ? room : s;
        f = formation;
    }
    return f;
}

/* 빌드 53: 공격 노드 전체에서 만든 부대의 탱커·안행·공성 병기 수(attack_node_hook이 노드마다 비움) */
static __declspec(thread) struct {
    int tank, ranged, siege;
} g_fc;

static void form_count(int form)
{
    g_fc.tank += is_tank(form), g_fc.ranged += form == F_ANHAENG, g_fc.siege += is_siege(form);
}

/* 빌드 53 공격 진형 규칙(요격과 같음): 탱커 수 ≥ 안행 + 공성 병기 수. 이미 만든 부대로 세고 이번 부대를 더해 본다.
   - 안행: 사격형이거나 다른 진형 공군이 30 미만인 무장만, 탱커가 남을 때만. 아니면 탱커(없으면 안행 뺀 최대) 진형으로.
   - 탱커 등: 안행 우선 무장이고 탱커가 남으면 안행으로.
   - 공성 병기: 탱커가 모자라면 그 무장의 탱커 진형으로. 비용(+0x9A)은 거점 값 이하만 */
static uint8_t *goose_rule_attack(uint8_t *person, uint8_t *fh, uint8_t *formation, int *spec)
{
    int f = spec[3], cap = (*(int(__fastcall **)(void *))(*(uint8_t **)fh + 0x90))(fh), t = 0;
    cap = cap < 0 ? 0 : cap;
    int room = g_fc.tank >= g_fc.ranged + g_fc.siege + 1; /* 이번 부대가 원거리·병기여도 탱커가 모자라지 않음 */
    uint8_t *ga = formation_by_id(F_ANHAENG);
    int goose_ok = valid(ga) && *(uint16_t *)(ga + 0x9A) <= cap;
    if (f == F_ANHAENG) {
        if (!(room && goose_prio(person, best_form(person, 0, cap)) > 0)) {
            t = best_form(person, 1, cap);
            if (!t && !room)
                t = 0; /* 탱커 진형이 없으면 안행 그대로 */
            else if (!t)
                t = best_form(person, 0, cap);
        }
    } else if (is_siege(f)) {
        if (!room)
            t = best_form(person, 1, cap);
    } else if (room && goose_ok && goose_prio(person, f) > 0) {
        t = F_ANHAENG;
    }
    uint8_t *nf = t && t != f ? formation_by_id(t) : NULL;
    if (!valid(nf))
        return formation;
    spec[3] = t;
    return nf;
}

/* B단계 부대 생성: 만든 부대의 진형·병사를 거점별로 센다 */
static uint64_t __fastcall atk_made_hook(int *spec, int flag)
{
    uint64_t r = CALL(UnitCreateFn, FN_UNIT_CREATE)(spec, flag);
    __try {
        uint8_t *p = spec && spec[0] >= 0 && spec[0] < PERSON_MAX ? root_slot(ROOT_PERSONS, spec[0]) : NULL;
        if (g_v3 && r && valid((void *)r) && is_person(p)) {
            siege_count(foothold_of(p), spec[3], spec[2]);
            form_count(spec[3]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

/* 공격 진형: 봉시면 무장이 가진 탱커 진형(공군 + 방어 최대)으로 바꾼다. 사양 +0xC가 부대 진형이다.
   모드 함수는 이 호출 뒤 진형 +0x9A > 소속 거점 vfunc +0x90 값이면 그 무장을 건너뛰므로(0x46B80의 0x47A4E 등)
   그 검사를 통과하는 진형만 고른다 */
static uint64_t __fastcall atk_tactics_hook(uint8_t *person, uint8_t *formation, int *spec)
{
    __try {
        uint8_t *fh = valid(person) ? foothold_of(person) : NULL;
        if (valid(fh) && valid(formation) && CALL(ObjFn, FN_FORM_ID)(formation) == F_BONGSI) {
            int cost_cap = (*(int(__fastcall **)(void *))(*(uint8_t **)fh + 0x90))(fh);
            int t = best_form(person, 1, cost_cap < 0 ? 0 : cost_cap);
            uint8_t *f = formation_by_id(t);
            if (t && valid(f)) {
                spec[3] = t;
                formation = f;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[공격] 진형 바꾸기 중 예외. 원래 진형으로");
    }
    __try {
        uint8_t *fh = valid(person) ? foothold_of(person) : NULL;
        /* 출진 판단이 "안 함"이면 예비 = 거점 병력이라 아무도 나가지 않는데 원작 루프는 진형·전법을 돈다(빌드 33 판 잡음) */
        if (g_v3 && g_atk_go != 1 && valid(fh) && (uint8_t *)_ReturnAddress() != RVA(SITE_ATK_TACTICS_SPEC + 5)) {
            /* 편성에서 일반 역할인데 원작이 공성 진형을 고르면 공성 아닌 진형으로 */
            for (int j = 0; g_atk.stage == 2 && fh == g_atk.fh && j < g_atk.n; j++)
                if (g_atk.m[j].person == person && g_atk.m[j].siege == 2 && is_siege(spec[3])) {
                    int cap = (*(int(__fastcall **)(void *))(*(uint8_t **)fh + 0x90))(fh);
                    int t = field_form(person, cap < 0 ? 0 : cap);
                    uint8_t *f = formation_by_id(t);
                    if (t && valid(f)) {
                        spec[3] = t, formation = f;
                    }
                }
            formation = siege_escort(person, fh, formation, spec);
            formation = goose_rule_attack(person, fh, formation, spec);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return CALL(AtkTacticsFn, FN_ATK_TACTICS)(person, formation, spec);
}

/* ---------- F. 그룹 10 비군사 임무 ---------- */

typedef uint64_t(__fastcall *ListFn)(void *corps, void *list, void *filter);

static uint8_t *root_slot(int table, int i) { return *(uint8_t **)(game_root() + table + (size_t)i * 8); }

/* 세력 force와 세력 g의 동맹·연합 남은 개월(0 = 적). 동맹은 세력 +0xEA+g 하위 7비트. 연합은 게임 진행
   +WORLD_COALITION(남은 개월, 결성 때 24, 0x2FEE10이 매달 줄임)이고 두 세력 모두 +0x12 비트 1(연합 참가)일 때.
   원작 적대 판정 0x2CA280과 같은 조건이다(2026-11-03: 빌드 45 판 유비·손권 반조조 연합을 적으로 셌음) */
static int ally_months(uint8_t *force, int g)
{
    int m = force[0xEA + g] & 0x7F;
    uint8_t *world = *(uint8_t **)(game_root() + ROOT_WORLD), *other = g >= 1 && g <= 0x33 ? root_slot(ROOT_FORCES, g) : NULL;
    int c = world ? world[WORLD_COALITION] : 0;
    if (c > m && (force[0x12] & 2) && valid(other) && (other[0x12] & 2))
        m = c;
    return m;
}

static int is_person(void *o)
{
    if (!valid(o))
        return 0;
    unsigned id = *(uint16_t *)((uint8_t *)o + 0x10);
    return id <= PERSON_MAX && root_slot(ROOT_PERSONS, id) == o;
}

static uint8_t *corps_of(uint8_t *person) { return person[0xE0] <= 0x33 ? root_slot(ROOT_CORPS, person[0xE0]) : NULL; }

/* AI 세력(1~45, 플레이어 아님)의 군단이면 세력 번호, 아니면 0 */
static int ai_corps_force(uint8_t *corps)
{
    int fid = valid(corps) ? corps[0x10] : 0;
    if (fid < 1 || fid > 45)
        return 0;
    uint8_t *force = root_slot(ROOT_FORCES, fid);
    return valid(force) && !CALL(ObjFn, FN_IS_PLAYER)(force) ? fid : 0;
}

/* 군단 소유 지역(지역 +0x78 세력, +0x35 도시의 군단)의 인접 지역(+0x56 u16×8, +0x66 비트)에
   동맹·연합(ally_months) 아닌 다른 세력 1~45의 지역이 있으면 1 */
static int corps_at_war(uint8_t *corps, int fid)
{
    uint8_t *force = root_slot(ROOT_FORCES, fid);
    for (int r = 1; r <= REGION_MAX; r++) {
        uint8_t *reg = root_slot(ROOT_REGIONS, r);
        if (!reg || reg[0x78] != fid)
            continue;
        uint8_t *city = reg[0x35] <= 0x33 ? root_slot(ROOT_CITIES, reg[0x35]) : NULL;
        if (!valid(city) || (*(uint8_t * (__fastcall **)(void *))(*(uint8_t **)city + 0x80))(city) != corps)
            continue;
        for (int i = 0; i < 8; i++) {
            unsigned n = *(uint16_t *)(reg + 0x56 + i * 2);
            if (!(reg[0x66] >> i & 1) || n < 1 || n > REGION_MAX)
                continue;
            uint8_t *nr = root_slot(ROOT_REGIONS, (int)n);
            int g = nr ? nr[0x78] : 0;
            if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g))
                return 1;
        }
    }
    return 0;
}

/* 군단의 군주와 A 상위 MISSION_TOP명(군주 제외, 신분 1~4) */
static int mission_keep(uint8_t *corps, uint8_t **out)
{
    uint8_t *top[MISSION_TOP + 1] = {0};
    float ta[MISSION_TOP] = {0};
    int n = 0;
    for (int id = 0; id <= PERSON_MAX; id++) {
        uint8_t *p = root_slot(ROOT_PERSONS, id);
        if (!valid(p) || p[0xE6] < 1 || p[0xE6] > 4 || corps_of(p) != corps)
            continue;
        if (p[0xE6] == 1) {
            top[MISSION_TOP] = p;
            continue;
        }
        float a = ability_a(p);
        int j = MISSION_TOP - 1;
        if (top[j] && ta[j] >= a)
            continue;
        for (; j > 0 && (!top[j - 1] || ta[j - 1] < a); j--) {
            top[j] = top[j - 1];
            ta[j] = ta[j - 1];
        }
        top[j] = p;
        ta[j] = a;
    }
    for (int i = 0; i <= MISSION_TOP; i++)
        if (top[i])
            out[n++] = top[i];
    return n;
}

static int in_set(uint8_t *const *set, int n, void *p)
{
    for (int i = 0; i < n; i++)
        if (set[i] == p)
            return 1;
    return 0;
}

/* 후보 목록에서 군주·A 상위를 지운다. 남는 후보가 min_left 밑이면 지우지 않는다 */
static void mission_filter(uint8_t *corps, void *list, int min_left)
{
    uint8_t *items[MISSION_LIST_MAX], *keep[MISSION_TOP + 1], *drop[MISSION_TOP + 1];
    int fid = ai_corps_force(corps);
    int n = fid ? list_items(list, items, MISSION_LIST_MAX) : 0;
    if (!n || n >= MISSION_LIST_MAX)
        return;
    int nk = mission_keep(corps, keep), nd = 0;
    for (int i = 0; i < n && nd <= MISSION_TOP; i++)
        if (in_set(keep, nk, items[i]))
            drop[nd++] = items[i];
    if (!nd || n - nd < min_left || !corps_at_war(corps, fid))
        return;
    for (int i = 0; i < nd; i++) {
        uint8_t *p = drop[i];
        CALL(void(__fastcall *)(void *, void *), FN_LIST_REMOVE)(list, &p);
    }
}

static uint64_t __fastcall inspect_list_hook(void *corps, void *list, void *filter)
{
    uint64_t r = CALL(ListFn, FN_LIST_INSPECT)(corps, list, filter);
    __try {
        mission_filter((uint8_t *)corps, list, 2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[임무] 정찰 후보 거르기 중 예외. 원래 목록으로");
    }
    return r;
}

static uint64_t __fastcall search_list_hook(void *corps, void *list, void *filter)
{
    uint64_t r = CALL(ListFn, FN_LIST_SEARCH)(corps, list, filter);
    __try {
        mission_filter((uint8_t *)corps, list, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[임무] 탐색 후보 거르기 중 예외. 원래 목록으로");
    }
    return r;
}

static uint64_t __fastcall plot_list_hook(void *corps, void *list, void *filter)
{
    uint64_t r = CALL(ListFn, FN_LIST_PLOT)(corps, list, filter);
    __try {
        uint8_t *ret = (uint8_t *)_ReturnAddress();
        for (int i = 0; i < (int)_countof(PLOT_LIST_CALLS); i++)
            if (ret == RVA(PLOT_LIST_CALLS[i] + 5))
                mission_filter((uint8_t *)corps, list, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[임무] 계략 후보 거르기 중 예외. 원래 목록으로");
    }
    return r;
}

/* 목록을 쓰지 않는 실행 노드: 인자 변수 중 빼야 할 무장이 있으면 실패(2)로 끝낸다 */
static int mission_refuse(uint8_t *node, void *ctx, int argc)
{
    for (int k = 0; k < argc; k++) {
        uint8_t *p = bb_obj(ctx, *(uint32_t *)(node + 0x38 + k * 4));
        if (!is_person(p))
            continue;
        uint8_t *corps = corps_of(p), *keep[MISSION_TOP + 1];
        int fid = ai_corps_force(corps);
        if (!fid || !in_set(keep, mission_keep(corps, keep), p) || !corps_at_war(corps, fid))
            continue;
        return 1;
    }
    return 0;
}

static int __fastcall dual_hook(uint8_t *node, void *ctx, void *a3)
{
    int refuse = 0;
    __try {
        refuse = mission_refuse(node, ctx, 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        refuse = 0;
    }
    return refuse ? 2 : CALL(NodeFn, FN_DUAL)(node, ctx, a3);
}

static int __fastcall spyaway_hook(uint8_t *node, void *ctx, void *a3)
{
    int refuse = 0;
    __try {
        refuse = mission_refuse(node, ctx, 3);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        refuse = 0;
    }
    return refuse ? 2 : CALL(NodeFn, FN_SPYAWAY)(node, ctx, a3);
}

/* ---------- F. 그룹 10 수상 보정 ---------- */


static int hex_water(int h)
{
    uint8_t *tile = h >= 0 && h < HEX_COUNT ? root_slot(ROOT_TILES, h) : NULL;
    unsigned t = tile ? tile[0x10] : 0xFF;
    uint8_t *td = t <= 0x1E ? root_slot(ROOT_TERRAINS, (int)t) : NULL;
    return valid(td) && (td[0x2C] & 2);
}

/* 칸 ↔ 큐브 좌표. 열 x = 칸 % 220, 행 y = 칸 / 220, 홀수 열이 위(tools/mapdist.py·0x2AA850과 같은 거리) */
static void hex_cube(int h, int *q, int *r)
{
    int x = h % 220, y = h / 220;
    *q = x;
    *r = (2 * y + (x % 2 == 0 ? 1 : 0) + 1 - x) / 2;
}

static int cube_hex(int q, int r)
{
    int y2 = 2 * r + q - 1 - (q % 2 == 0 ? 1 : 0);
    if (q < 0 || q >= 220 || y2 < 0 || y2 / 2 >= 220)
        return -1;
    return y2 / 2 * 220 + q;
}

/* from에서 to까지 직선(양 끝 포함) 칸의 물 칸 수. *run = from부터 끊기지 않는 물 칸 수, *dist = 칸 거리 */
static int line_water(int from, int to, int *run, int *dist)
{
    int q0, r0, q1, r1;
    hex_cube(from, &q0, &r0);
    hex_cube(to, &q1, &r1);
    int dq = q1 - q0, dr = r1 - r0, ds = -dq - dr;
    int n = abs(dq) > abs(dr) ? abs(dq) : abs(dr);
    n = abs(ds) > n ? abs(ds) : n;
    int total = 0, lead = 0, broken = 0;
    for (int i = 0; i <= n; i++) {
        float t = n ? (float)i / (float)n : 0;
        float fq = (float)q0 + (float)dq * t + 1e-4f, fr = (float)r0 + (float)dr * t + 2e-4f, fs = -fq - fr;
        float rq = roundf(fq), rr = roundf(fr), rs = roundf(fs);
        float eq = fabsf(rq - fq), er = fabsf(rr - fr), es = fabsf(rs - fs);
        if (eq > er && eq > es)
            rq = -rr - rs;
        else if (er > es)
            rr = -rq - rs;
        int w = hex_water(cube_hex((int)rq, (int)rr));
        total += w;
        if (!broken && w)
            lead++;
        else
            broken = 1;
    }
    if (run)
        *run = lead;
    if (dist)
        *dist = n;
    return total;
}

/* 거점 소유 세력(거점 vfunc +0x80 군단 +0x10). 도시·관문 모두 +0x80이 군단(0x22BA30·0x22BA60) */
static uint8_t *foothold_force(uint8_t *fh)
{
    uint8_t *corps = valid(fh) ? (*(uint8_t * (__fastcall **)(void *))(*(uint8_t **)fh + 0x80))(fh) : NULL;
    int fid = valid(corps) ? corps[0x10] : 0;
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    return valid(force) ? force : NULL;
}

/* 세력의 수상 진형(+0x15E) 공군·방어(세력 보정 포함). 수상 진형(+0x7E == 2)이 아니면 0 */
static int force_naval(uint8_t *force, float *atk, float *def)
{
    uint8_t *f = valid(force) ? formation_by_id(force[0x15E]) : NULL;
    if (!valid(f) || f[0x7E] != 2)
        return 0;
    int(__fastcall *stat)(void *, void *, int) = (int(__fastcall *)(void *, void *, int))RVA(FN_FORM_STAT);
    *atk = (float)stat(force, f, 2);
    *def = (float)stat(force, f, 5);
    return *atk > 0 && *def > 0;
}

/* 부대의 수상 진형(+0x1C) 공군·방어 */
static int unit_naval(uint8_t *unit, float *atk, float *def)
{
    int(__fastcall *stat)(void *, int, int) = (int(__fastcall *)(void *, int, int))RVA(FN_UNIT_FORM_STAT);
    *atk = (float)stat(unit, 2, 2);
    *def = (float)stat(unit, 2, 5);
    return *atk > 0 && *def > 0;
}

/* lo·hi = 한계(공격은 NAVAL_ATK_MIN~1: 공성은 육지에서 끝나므로 수군이 강해도 가점 없음, 약하면 크게 벌점) */
static float naval_ratio(float a_atk, float a_def, float b_atk, float b_def, float lo, float hi)
{
    float r = sqrtf(a_atk * a_def / (b_atk * b_def));
    return r < lo ? lo : r > hi ? hi : r;
}

/* 강 건넘(2026-11-03): 직선 물 칸이 NAVAL_RIVER 이상이어도 물 칸을 밟지 않는 육로가 직선 거리 × 3/2 + RIVER_DETOUR칸
   안에 있으면 육로로 본다(217 시나리오 수춘 → 여강이 직선으로는 물 4칸 이상이라 0.58을 받던 문제). 너비 우선 탐색,
   (출발 칸, 도착 칸) 쌍마다 저장. 도착 칸에서 1칸 안이면 닿은 것으로 본다 */
#define RIVER_DETOUR 6
#define RIVER_CACHE 1024
static struct {
    int key, cross; /* key = 0이면 빈 칸 */
} g_river[RIVER_CACHE];
static SRWLOCK g_river_lock = SRWLOCK_INIT;
static int g_bfs_seen[HEX_COUNT], g_bfs_depth[HEX_COUNT], g_bfs_queue[HEX_COUNT], g_bfs_stamp;

static int land_route(int from, int to, int limit)
{
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    static const int DQ[6] = {1, 1, 0, -1, -1, 0}, DR[6] = {0, -1, -1, 0, 1, 1};
    int stamp = ++g_bfs_stamp, head = 0, tail = 0;
    g_bfs_seen[from] = stamp, g_bfs_depth[from] = 0, g_bfs_queue[tail++] = from;
    while (head < tail) {
        int h = g_bfs_queue[head++], d = g_bfs_depth[h], q, r;
        if (dist(h, to) <= 1)
            return 1;
        hex_cube(h, &q, &r);
        for (int k = 0; k < 6; k++) {
            int n = cube_hex(q + DQ[k], r + DR[k]);
            if (n < 0 || n >= HEX_COUNT || g_bfs_seen[n] == stamp)
                continue;
            g_bfs_seen[n] = stamp;
            if (hex_water(n) || d + 1 + dist(n, to) > limit)
                continue;
            g_bfs_depth[n] = d + 1, g_bfs_queue[tail++] = n;
        }
    }
    return 0;
}

static int crosses_river(int from, int to)
{
    if (from < 0 || from >= HEX_COUNT || to < 0 || to >= HEX_COUNT)
        return 0;
    int d = 0;
    if (line_water(from, to, NULL, &d) < NAVAL_RIVER)
        return 0;
    int key = (from << 16 | to) + 1, slot = (unsigned)(from * 31 + to) % RIVER_CACHE, cross = -1;
    AcquireSRWLockExclusive(&g_river_lock);
    __try {
        if (g_river[slot].key == key)
            cross = g_river[slot].cross;
        else {
            cross = !land_route(from, to, d * 3 / 2 + RIVER_DETOUR);
            g_river[slot].key = key, g_river[slot].cross = cross;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cross = 1;
    }
    ReleaseSRWLockExclusive(&g_river_lock);
    return cross;
}

/* 공격: 만든 부대 전력에 곱할 수상 배율(1 이하). 부대 칸 ~ 대상 성이 강 건넘(crosses_river)일 때만 */
static float naval_attack(uint8_t *unit)
{
    uint8_t *target = valid(g_atk_node) ? *(uint8_t **)(g_atk_node + 0x60) : NULL;
    float ua, ud, da, dd;
    if (!valid(target) || !valid(unit) ||
        !crosses_river(*(uint16_t *)(unit + 0x2A), CALL(ObjFn, FN_HEX)(target)))
        return 1;
    if (!force_naval(foothold_force(target), &da, &dd) || !unit_naval(unit, &ua, &ud))
        return 1;
    return naval_ratio(ua, ud, da, dd, NAVAL_ATK_MIN, 1);
}

/* 공격 계획(몫 나누기): 출진 거점 성 ~ 대상 성 직선 기준, 양쪽 세력의 수상 진형으로 */
static float naval_plan(uint8_t *fh, uint8_t *target, uint8_t *person)
{
    float ua, ud, da, dd;
    if (!g_v3 || !valid(target) || !valid(fh))
        return 1;
    __try {
        if (!crosses_river(CALL(ObjFn, FN_HEX)(fh), CALL(ObjFn, FN_HEX)(target)))
            return 1;
        if (!force_naval(foothold_force(target), &da, &dd) ||
            !force_naval(CALL(uint8_t * (__fastcall *)(void *), FN_PERSON_FORCE)(person), &ua, &ud))
            return 1;
        return naval_ratio(ua, ud, da, dd, NAVAL_ATK_MIN, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

/* 요격: 적 선두가 물 칸 위에 있고 선두 ~ 목표 성 직선의 앞쪽 ⌈거리/2⌉ + NAVAL_MARGIN칸이 모두 물이면
   출진 문턱에 곱할 수상 배율(선두의 수상 진형 / 목표 소유 세력의 수상 진형). 보내는 병력(E)에는 곱하지 않는다.
   빌드 47: 선두가 땅 위여도 목표까지 강을 건너야 하면(crosses_river, 맞은편 기슭에서 건너오려는 적) 같은 배율을 건다 */
static float naval_intercept(void *ctx)
{
    g_nv_water = 0, g_nv_cross = 0, g_nv_run = g_nv_dist = -1;
    if (!g_v3 || V102_STAGE < 3) /* 요격 수상 보정은 3단계(보류) */
        return 1;
    __try {
        uint8_t *lead = bb_obj(ctx, BT_LEAD), *target = bb_obj(ctx, BT_TARGET);
        if (!valid(lead) || !valid(target))
            return 1;
        int from = *(uint16_t *)(lead + 0x2A), run, d;
        g_nv_water = hex_water(from);
        line_water(from, CALL(ObjFn, FN_HEX)(target), &run, &d);
        g_nv_run = run, g_nv_dist = d;
        int afloat = g_nv_water && run >= (d + 1) / 2 + NAVAL_MARGIN;
        g_nv_cross = !afloat && crosses_river(from, CALL(ObjFn, FN_HEX)(target));
        float la, ld, da, dd;
        if ((!afloat && !g_nv_cross) || !unit_naval(lead, &la, &ld) || !force_naval(foothold_force(target), &da, &dd))
            return 1;
        return naval_ratio(la, ld, da, dd, NAVAL_MIN, NAVAL_MAX);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

/* 수상 진형 단계: 주가(11) 1, 몽충(12) 2, 누선(13) 3. 그 밖 0 */
static int naval_tier(int form) { return form >= 11 && form <= 13 ? form - 10 : 0; }

/* 빌드 48 수상 우위 요격(2026-11-03 사용자 결정): 목표 소유 세력(+0x15E)의 수상 진형이 선두(+0x1C)보다 NAVAL_ADV_TIER단계
   이상 높으면 1. 플레이어 세력은 원작 그대로. 빌드 49 라(2026-11-04): 선두가 물 위이거나 목표까지 강을 건너야 하고
   선두 ~ 목표 직선에 물 칸이 NAVAL_ADV_RUN 이상 남으면(빌드 48은 선두에서 이어지는 물만 셈: 건업 219.8.21 앞 물 1칸이라 놓침) */
static int naval_adv(void *ctx, int *ours, int *theirs)
{
    *ours = *theirs = 0;
    if (V102_STAGE < 2) /* 수상 우위 요격은 2단계부터(2026-11-05 사용자 결정으로 3단계에서 앞당김) */
        return 0;
    uint8_t *lead = bb_obj(ctx, BT_LEAD), *target = bb_obj(ctx, BT_TARGET);
    if (!g_v3 || !valid(lead) || !valid(target))
        return 0;
    int from = *(uint16_t *)(lead + 0x2A), th = CALL(ObjFn, FN_HEX)(target);
    int water = line_water(from, th, NULL, NULL);
    if (!hex_water(from) && !crosses_river(from, th))
        return 0;
    uint8_t *tf = foothold_force(target);
    if (water < NAVAL_ADV_RUN || !tf || CALL(ObjFn, FN_IS_PLAYER)(tf))
        return 0;
    int o = naval_tier(tf[0x15E]), t = naval_tier(lead[0x1C]);
    if (t < 1 || o < t + NAVAL_ADV_TIER)
        return 0;
    *ours = o, *theirs = t;
    return 1;
}

/* from → to 직선의 i번째 칸(0..n, line_water와 같은 큐브 보간) */
static int line_step(int from, int to, int i, int *n_out)
{
    int q0, r0, q1, r1;
    hex_cube(from, &q0, &r0);
    hex_cube(to, &q1, &r1);
    int dq = q1 - q0, dr = r1 - r0, ds = -dq - dr;
    int n = abs(dq) > abs(dr) ? abs(dq) : abs(dr);
    n = abs(ds) > n ? abs(ds) : n;
    if (n_out)
        *n_out = n;
    i = i < 0 ? 0 : i > n ? n : i;
    float t = n ? (float)i / (float)n : 0;
    float fq = (float)q0 + (float)dq * t + 1e-4f, fr = (float)r0 + (float)dr * t + 2e-4f, fs = -fq - fr;
    float rq = roundf(fq), rr = roundf(fr), rs = roundf(fs);
    float eq = fabsf(rq - fq), er = fabsf(rr - fr), es = fabsf(rs - fs);
    if (eq > er && eq > es)
        rq = -rr - rs;
    else if (er > es)
        rr = -rq - rs;
    return cube_hex((int)rq, (int)rr);
}

/* 칸 h의 지역 소유 세력(칸 +0x12 지역 → 지역 +0x78). 없으면 0 */
static int hex_region_owner(int h)
{
    uint8_t *tile = h >= 0 && h < HEX_COUNT ? root_slot(ROOT_TILES, h) : NULL;
    int rid = tile ? *(uint16_t *)(tile + 0x12) : 0;
    uint8_t *reg = rid >= 1 && rid <= REGION_MAX ? root_slot(ROOT_REGIONS, rid) : NULL;
    return reg ? reg[0x78] : 0;
}

/* 빌드 48 요격 진형(2026-11-03 사용자 결정): 출진 거점 fh에서 우리 봉시 기동, 적 선두는 지금 진형 기동으로 마주 오면
   만나는 칸을 거점 ~ 선두 직선 위 (거리 × 우리 기동 / 양쪽 합)으로, 적이 다가오지 않으면 선두 칸으로 본다.
   그 칸 앞뒤 MEET_SPAN칸 중 적대(동맹·연합 아님) 세력 지역이 있으면 1 */
static int meet_enemy_ground(void *ctx, uint8_t *fh, char *note, size_t nsz, int *meet_hex)
{
    note[0] = 0;
    if (meet_hex)
        *meet_hex = -1;
    uint8_t *lead = bb_obj(ctx, BT_LEAD);
    int fid = valid(fh) ? fh_owner(fh) : 0;
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (!valid(lead) || !valid(force))
        return 0;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int b = CALL(ObjFn, FN_HEX)(fh), h = *(uint16_t *)(lead + 0x2A), dest = h;
    int act = lead[0x37], kind = lead[0x38], id = *(uint16_t *)(lead + 0x3A);
    if ((act == 4 || act == 5) && (kind == 5 || kind == 6)) {
        uint8_t *o = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(game_root(), kind == 5 ? id : id + 0x33);
        if (valid(o))
            dest = CALL(ObjFn, FN_HEX)(o);
    } else if ((act == 1 || act == 8 || act == 14) && kind == 9 && id < HEX_COUNT)
        dest = id;
    int coming = dist(dest, b) < dist(h, b);
    uint8_t *bf = formation_by_id(F_BONGSI);
    float vu = valid(bf) ? (float)((int(__fastcall *)(void *, void *, int))RVA(FN_FORM_STAT))(force, bf, 1) : 0;
    float ve = (float)((int(__fastcall *)(void *, int, int))RVA(FN_UNIT_FORM_STAT))(lead, 0, 1);
    float t = coming && vu > 0 && ve > 0 ? vu / (vu + ve) : 1;
    int n = 0;
    line_step(b, h, 0, &n);
    int mi = (int)((float)n * t + 0.5f), hit = -1;
    for (int i = mi - MEET_SPAN; i <= mi + MEET_SPAN && hit < 0; i++) {
        if (i < 0 || i > n)
            continue;
        int g = hex_region_owner(line_step(b, h, i, NULL));
        if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g))
            hit = g;
    }
    if (meet_hex)
        *meet_hex = line_step(b, h, mi, NULL);
    _snprintf_s(note, nsz, _TRUNCATE, ", 만남 칸 %d(%d/%d칸%s)%s", line_step(b, h, mi, NULL), mi, n, coming ? "" : ", 적 머묾",
                hit > 0 ? " 적 지역이라 봉시 미룸" : "");
    return hit > 0;
}

/* 빌드 49 마(2026-11-04 사용자 결정): 만남 칸 h에서 원작 인접 효과 함수 0x2B07C0(칸, 세력, &합계)(실제 전투와 같음)로
   출진 거점 세력과 적 선두 세력의 계수를 구해 비(우리 / 적)를 ADJ_MIN~ADJ_MAX로 자른다. 못 구하면 1 */
static float adj_ratio_at(uint8_t *fh, uint8_t *lead, int h, float *ours, float *theirs)
{
    *ours = *theirs = 1;
    uint8_t *tile = h >= 0 && h < HEX_COUNT ? root_slot(ROOT_TILES, h) : NULL;
    int fid = valid(fh) ? fh_owner(fh) : 0, efid = valid(lead) ? CALL(ObjFn, FN_UNIT_FORCE)(lead) : 0, s1 = 0, s2 = 0;
    if (!tile || fid < 1 || fid > 51 || efid < 1 || efid > 51)
        return 1;
    float(__fastcall *adj)(void *, int, int *) = (float(__fastcall *)(void *, int, int *))RVA(FN_ADJ_EFFECT);
    float a = adj(tile, fid, &s1), b = adj(tile, efid, &s2);
    if (!(a > 0) || !(b > 0))
        return 1;
    *ours = a, *theirs = b;
    float r = a / b;
    return r < ADJ_MIN ? ADJ_MIN : r > ADJ_MAX ? ADJ_MAX : r;
}

static float adj_ratio(void *ctx, float *ours, float *theirs)
{
    *ours = *theirs = 1;
    uint8_t *fh = bb_obj(ctx, BT_FOOTHOLD), *lead = bb_obj(ctx, BT_LEAD);
    if (!g_v3 || !valid(fh) || !valid(lead))
        return 1;
    char note[96];
    int mh = -1;
    meet_enemy_ground(ctx, fh, note, sizeof note, &mh);
    return adj_ratio_at(fh, lead, mh, ours, theirs);
}

/* 위협 목록은 선두 하나만 담는 때가 많다(대부분 1부대). 적 선두 칸에서 ENEMY_NEAR칸 안의
   선두와 같은 세력 부대 수(선두 포함)와 견줘 큰 쪽. *foe = 그 부대들(또는 위협 목록) 전력 합 중 큰 쪽.
   E는 여러 거점이 함께 요격하면 다른 거점 몫을 뺀 값이라 진형 판단에 쓰지 않는다 */
static int enemy_count(void *ctx, int listed, float *foe, int *foe_sol)
{
    uint8_t *lead = g_v3 ? bb_obj(ctx, BT_LEAD) : NULL;
    *foe = 0;
    *foe_sol = 0;
    if (!valid(lead))
        return listed;
    uint8_t *threat[MAX_CAND];
    float lsum = 0, nsum = 0;
    int ln = list_items(bb_obj(ctx, BT_THREAT), threat, MAX_CAND), lsol = 0, nsol = 0;
    for (int i = 0; i < ln; i++)
        if (valid(threat[i])) {
            lsum += est_power(threat[i]);
            lsol += *(uint16_t *)(threat[i] + 0x16);
        }
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int f = CALL(ObjFn, FN_UNIT_FORCE)(lead), h = *(uint16_t *)(lead + 0x2A), n = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (valid(u) && CALL(ObjFn, FN_UNIT_FORCE)(u) == f && dist(h, *(uint16_t *)(u + 0x2A)) <= ENEMY_NEAR) {
            n++;
            nsum += est_power(u);
            nsol += *(uint16_t *)(u + 0x16);
        }
    }
    *foe = nsum > lsum ? nsum : lsum;
    *foe_sol = nsol > lsol ? nsol : lsol;
    return n > listed ? n : listed;
}

/* ---------- F. 그룹 10 관문 수비·열세 하한 ---------- */

static int g_enter_ok; /* Departure·ListAddArmy를 둘 다 감쌌을 때만 들여보내기 */

static int fh_vint(uint8_t *fh, int off) { return (*(int(__fastcall **)(void *))(*(uint8_t **)fh + off))(fh); }
static int gate_dur_arrive(uint8_t *gate, uint8_t *from, int *rate, int *turns); /* 빌드 63: 들여보낼 부대가 도착할 때의 예상 내구 */

/* 관문 사격이 병력으로 늘어나는 상한 (⌊4L/3⌋)²(0x2A8D40) */
static int fire_cap(int lead)
{
    int t = lead * 4 / 3;
    return t * t;
}

/* 무장이 가진 원거리 진형(안행, 또는 세력에서 원거리인 투석)으로 거점 fh에서 나갈 수 있으면 그 id, 아니면 0 */
static int ranged_form(uint8_t *person, uint8_t *fh)
{
    static const int RANGED[] = {3, 10};
    uint8_t *force = CALL(uint8_t * (__fastcall *)(void *), FN_PERSON_FORCE)(person);
    if (!valid(force))
        return 0;
    for (int i = 0; i < (int)_countof(RANGED); i++) {
        uint8_t *f = formation_by_id(RANGED[i]);
        if (!(*(uint32_t *)(person + 0x12C) >> RANGED[i] & 1) || !valid(f) ||
            *(uint16_t *)(f + 0x9A) > fh_vint(fh, 0x90) ||
            !((int(__fastcall *)(void *, void *))RVA(FN_RANGED))(force, f))
            continue;
        return RANGED[i];
    }
    return 0;
}

/* 세력 fid 부대 중 관문 gid로 들어가는 중(행동 3, 대상 종류 6)인 병사 합 */
static int gate_incoming(int fid, int gid)
{
    int sum = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (valid(u) && u[0x37] == 3 && u[0x38] == 6 && *(uint16_t *)(u + 0x3A) == gid - 0x33 &&
            CALL(ObjFn, FN_UNIT_FORCE)(u) == fid)
            sum += *(uint16_t *)(u + 0x16);
    }
    return sum;
}

/* 관문 포위: 관문 칸에서 GATE_NEAR칸 안에 동맹 아닌 세력(1~45) 부대가 있음. 이번 위협의 적 선두 거리와 상관없다
   (2026-11-01: 이미 포위된 관문에 다른 무리가 다가올 때 근접 지원이 관문 뒤에 주차하던 문제) */
static int gate_besieged(uint8_t *gate, int fid)
{
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (!valid(force))
        return 0;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int h = CALL(ObjFn, FN_HEX)(gate);
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0;
        if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && dist(h, *(uint16_t *)(u + 0x2A)) <= GATE_NEAR)
            return 1;
    }
    return 0;
}

/* 빌드 51(2026-11-05 사용자 결정): 적 선두가 우리 관문을 치는 중(행동 5, 대상 = 관문)이면 그 관문. 위험도 = 그 거점을 대상으로 한
   적 부대 병사 합 / 거점 병력. *dc 도시(target), *dg 관문 */
static uint8_t *attacked_gate(uint8_t *lead, uint8_t *target, int fid, float *dc, float *dg)
{
    if (!valid(lead) || !valid(target) || lead[0x37] != 5 || lead[0x38] != 6)
        return NULL;
    int gid = *(uint16_t *)(lead + 0x3A) + 0x33, cid = *(uint16_t *)(target + 0x4E);
    uint8_t *gate = gid >= 0x34 && gid <= 0x3D ? ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(game_root(), gid) : NULL;
    if (!valid(gate) || fh_owner(gate) != fid || gate == target)
        return NULL;
    int sc = 0, sg = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (!valid(u) || CALL(ObjFn, FN_UNIT_FORCE)(u) == fid || (u[0x37] != 4 && u[0x37] != 5))
            continue;
        int k = u[0x38], id = *(uint16_t *)(u + 0x3A);
        if (k == 5 && id == cid)
            sc += *(uint16_t *)(u + 0x16);
        else if (k == 6 && id + 0x33 == gid)
            sg += *(uint16_t *)(u + 0x16);
    }
    int gc = *(int *)(target + 0x3C), gg = *(int *)(gate + 0x3C);
    *dc = (float)sc / (float)(gc > 1000 ? gc : 1000);
    *dg = (float)sg / (float)(gg > 1000 ? gg : 1000);
    return gate;
}

static const char *siege_rules(uint8_t *fh, uint8_t *target, Member *m, int c, int *pk, int enemy, int e0, int avail, int dist,
                               int lead_hex, uint8_t **items, int n, int foe_sol)
{
    int k = *pk, tid = valid(target) ? *(uint16_t *)(target + 0x4E) : -1;
    int gate = tid >= 0x34 && tid <= 0x3D;
    int near_lead = gate && dist >= 0 && dist <= GATE_NEAR;
    int besieged = near_lead || (gate && gate_besieged(target, force_id(m[0].person)));
    /* 지원인데 적 선두가 출진 거점에서 관문보다 GATE_INSIDE칸 이상 가까우면 관문 안쪽(우리 쪽)에 들어온 적: 일반 요격 */
    if (besieged && fh != target && lead_hex != HEX_NONE) {
        int(__fastcall *hd)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
        int hf = CALL(ObjFn, FN_HEX)(fh);
        if (hd(hf, lead_hex) + GATE_INSIDE <= hd(hf, CALL(ObjFn, FN_HEX)(target))) {
            besieged = 0;
        }
    }
    if (besieged) {
        if (fh == target) {
            /* 관문 자신: 원거리 진형 무장 하나만. 남는 무장 중 통솔 (그 무장 − GATE_LEAD_GAP) 이상이 있고,
               관문 병력이 남는 최고 통솔의 사격 상한을 넘는 만큼만 데리고 나간다. 없으면 출진하지 않는다 */
            for (int j = 0; j < c; j++) {
                int rf = ranged_form(m[j].person, fh), rest = 0;
                if (!rf)
                    continue;
                for (int i = 0; i < n; i++)
                    if (items[i] != m[j].person && valid(items[i]) && ability(items[i], 0) > rest)
                        rest = ability(items[i], 0);
                int share = *(int *)(fh + 0x3C) - fire_cap(rest);
                share = share < avail ? share : avail;
                share = (share < m[j].cap ? share : m[j].cap) / STEP * STEP;
                if (rest < m[j].lead - GATE_LEAD_GAP || share < 1000)
                    continue;
                Member t = m[0];
                m[0] = m[j];
                m[j] = t;
                m[0].share = share;
                m[0].form = rf;
                *pk = 1;
                return "관문 원거리";
            }
            *pk = 0;
            return "관문 출진 안 함";
        }
        /* 다른 거점의 지원: 관문 병력이 사격 상한 밑이거나 근처 적 병사 합 밑이고 내구가 GATE_DUR_PCT% 이상이며 통째로 들어갈 자리가 있으면
           관문 안으로. 아니면 원거리 진형만, 그것도 없으면 그 무장은 나가지 않는다 */
        uint8_t *boss = CALL(uint8_t * (__fastcall *)(void *), FN_FH_LEADER)(target);
        int gar = *(int *)(target + 0x3C), room = fh_vint(target, 0xC8) - gar,
            dur = CALL(ObjFn, FN_FH_DUR)(target), dmax = fh_vint(target, 0x60);
        /* 빌드 63(2026-11-06 사용자 결정): 내구는 지금 값이 아니라 도착할 때의 예상 값으로 본다(빌드 62 판 백수관 220.1.11:
           지금 37%로 189·952 1만 4천 5백을 들였지만 도착 때 11%·7%, 함락으로 모두 잃음) */
        int rate = 0, turns = 0, dur_arr = dmax > 0 ? gate_dur_arrive(target, fh, &rate, &turns) : dur;
        int need_in = (gar < fire_cap(is_person(boss) ? ability(boss, 0) : 0) || gar < foe_sol) && dmax > 0;
        int low = g_enter_ok && !g_no_enter && need_in && dur_arr * 100 >= dmax * GATE_DUR_PCT;
        room -= gate_incoming(force_id(m[0].person), tid);
        for (int i = 0; i < k; i++) {
            if (low && m[i].share > 0 && m[i].share <= room) {
                m[i].enter = 1;
                room -= m[i].share;
                continue;
            }
            int rf = ranged_form(m[i].person, fh);
            if (rf)
                m[i].form = rf;
            else
                m[i].share = 0;
        }
        return "관문 지원";
    }
    /* 열세 하한: 이번 계획 전력 + 이미 같은 위협에 나간 아군(처음 E − 지금 E)이 처음 E의 WEAK_FLOOR_PCT% 미만이면
       무장마다 WEAK_FLOOR 이상, 못 채우면 그 무장은 나가지 않음 */
    if (!g_plan_adv && enemy > 0 && (group_power(m, k) * g_plan_adj + (float)(e0 - enemy)) * 100 < (float)e0 * WEAK_FLOOR_PCT) {
        int left = avail;
        for (int i = 0; i < k; i++) {
            if (m[i].share < WEAK_FLOOR)
                m[i].share = m[i].cap >= WEAK_FLOOR && left >= WEAK_FLOOR ? WEAK_FLOOR : 0;
            left -= m[i].share;
        }
        return "열세 하한";
    }
    /* 빌드 50(2026-11-05 사용자 결정): 수상 우위 요격도 무장마다 NAVAL_ADV_MIN 이상, 못 채우면 그 무장은 나가지 않음
       (빌드 49 판: 손권 시상 544 2,500명·유비 시상 662 500명이 나가 녹음) */
    if (g_plan_adv) {
        int left = avail, cut = 0;
        for (int i = 0; i < k; i++) {
            if (m[i].share > 0 && m[i].share < NAVAL_ADV_MIN) {
                m[i].share = m[i].cap >= NAVAL_ADV_MIN && left >= NAVAL_ADV_MIN ? NAVAL_ADV_MIN : 0;
                cut = 1;
            }
            left -= m[i].share;
        }
        if (cut) {
            return "수상 우위 최소 병력";
        }
    }
    return "";
}

/* 들여보낼 무장이면 Departure(요격 행동 7)를 "관문으로 들어가기"(행동 3, 대상 = 목표 거점)로 바꿔 부르고,
   바로 다음 ListAddArmy에서 그 부대를 요격 부대 목록에 넣지 않는다(트리 2가 명령을 다시 쓰지 않게) */
static __declspec(thread) int g_skip_add;
static void track_add(int person, int gate); /* 아래 추적 */

static int __fastcall depart_hook(uint8_t *node, void *ctx, void *a3)
{
    Member *mb = NULL;
    g_skip_add = 0;
    __try {
        if (g_plan.stage == 2 && *(int *)(node + 0x48) == 7) {
            mb = plan_member(bb_obj(ctx, BT_PERSON));
            if (mb && !mb->enter)
                mb = NULL;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mb = NULL;
    }
    if (!mb)
        return CALL(NodeFn, FN_DEPART)(node, ctx, a3);
    int act = *(int *)(node + 0x48);
    uint32_t var = *(uint32_t *)(node + 0x4C);
    *(int *)(node + 0x48) = 3;
    *(uint32_t *)(node + 0x4C) = BT_TARGET;
    int r = CALL(NodeFn, FN_DEPART)(node, ctx, a3);
    *(int *)(node + 0x48) = act;
    *(uint32_t *)(node + 0x4C) = var;
    g_skip_add = r == 1;
    uint8_t *target = bb_obj(ctx, BT_TARGET);
    if (r == 1 && valid(target)) {
        track_add(*(uint16_t *)(mb->person + 0x10), *(uint16_t *)(target + 0x4E));
    }
    return r;
}

/* 처음 E: 게임은 같은 위협에 부대가 나갈 때마다 E를 그 전력만큼 줄인다([1782]). 순마다(intercept_track) 비우는
   (목표 거점, 적 선두) 표에 그 순 처음 본 E를 둔다 */
#define E0_MAX 64
static struct {
    void *lead;
    int tid, turn, e0;
} g_e0[E0_MAX];
static SRWLOCK g_e0_lock = SRWLOCK_INIT;
static volatile LONG g_turn;

static int first_e(uint8_t *target, void *lead, int enemy)
{
    int tid = valid(target) ? *(uint16_t *)(target + 0x4E) : -1, turn = g_turn, e0 = enemy, slot = -1;
    AcquireSRWLockExclusive(&g_e0_lock);
    for (int i = 0; i < E0_MAX; i++) {
        if (g_e0[i].turn == turn && g_e0[i].tid == tid && g_e0[i].lead == lead) {
            slot = i;
            break;
        }
        if (slot < 0 && g_e0[i].turn != turn)
            slot = i; /* 지난 순 칸은 다시 쓴다 */
    }
    if (slot >= 0) {
        if (g_e0[slot].turn == turn && g_e0[slot].tid == tid && g_e0[slot].lead == lead && g_e0[slot].e0 > e0)
            e0 = g_e0[slot].e0;
        g_e0[slot].lead = lead, g_e0[slot].tid = tid, g_e0[slot].turn = turn, g_e0[slot].e0 = e0;
    }
    ReleaseSRWLockExclusive(&g_e0_lock);
    return e0;
}

/* 들여보낸 부대 추적: 무장 id·관문 id. 순마다 intercept_track이 부대 상태를 보고, 부대가 없어지면 뺀다 */
#define TRACK_MAX 32
static struct {
    int person, gate;
} g_track[TRACK_MAX];
static SRWLOCK g_track_lock = SRWLOCK_INIT;

static void track_add(int person, int gate)
{
    AcquireSRWLockExclusive(&g_track_lock);
    for (int i = 0; i < TRACK_MAX; i++)
        if (!g_track[i].person || g_track[i].person == person) {
            g_track[i].person = person;
            g_track[i].gate = gate;
            break;
        }
    ReleaseSRWLockExclusive(&g_track_lock);
}

static int tracked_gate(int person)
{
    int gid = 0;
    AcquireSRWLockShared(&g_track_lock);
    for (int i = 0; i < TRACK_MAX && !gid; i++)
        if (g_track[i].person == person)
            gid = g_track[i].gate;
    ReleaseSRWLockShared(&g_track_lock);
    return gid;
}

/* 빈 관문 태수: 무장 이동 명령. CCommandExecutionMoveNode 실행(0x2E180)과 같은 순서로 사양을 만들어 0x1F1270을 부른다 */
static void move_person(uint8_t *person, uint8_t *from, uint8_t *dest)
{
    typedef void(WINAPI * LockFn)(void *);
    struct {
        void *vt, *head, *tile, *dest;
    } spec = {(void *)RVA(VT_PERSON_LIST), NULL, NULL, NULL};
    CALL(PtrFn, FN_PLIST_INIT)(&spec);
    CALL(PtrFn, FN_PLIST_CLEAR)(&spec);
    if (*(int *)RVA(POOL_LOCK_ON))
        (*(LockFn *)RVA(IAT_LOCK))((void *)RVA(POOL_LOCK));
    void **slot = ((void **(__fastcall *)(void *, uint32_t, uint8_t))RVA(FN_POOL_ADD))(
        (void *)RVA(POOL_NODES), spec.head ? *(uint32_t *)spec.head : 0x14000, 1);
    if (slot)
        *slot = person;
    if (*(int *)RVA(POOL_LOCK_ON))
        (*(LockFn *)RVA(IAT_UNLOCK))((void *)RVA(POOL_LOCK));
    spec.tile = CALL(PtrFn, FN_FH_TILE)(from);
    spec.dest = dest;
    ((void(__fastcall *)(void *, int))RVA(FN_PERSON_MOVE))(&spec, 0);
    spec.vt = (void *)RVA(VT_PERSON_LIST);
    CALL(PtrFn, FN_PLIST_FREE)(&spec);
}

static int day_key(void);
static int fh_owner(uint8_t *fh);

/* 보낸 무장 추적. 순 처리 스레드에서만 쓴다 */
#define GOV_MAX 16
static struct {
    int gate, person, sent, arrived;
} g_gov[GOV_MAX];

static void gate_governor(void)
{
    uint8_t *root = game_root();
    int today = day_key();
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
    for (int i = 0; i < GOV_MAX; i++) {
        if (!g_gov[i].gate)
            continue;
        uint8_t *p = root_slot(ROOT_PERSONS, g_gov[i].person);
        int loc = valid(p) ? *(uint16_t *)(p + 0xE4) : -1;
        if (!g_gov[i].arrived) {
            if (loc == g_gov[i].gate) {
                g_gov[i].arrived = today;
            } else if (today - g_gov[i].sent > GOV_WAIT_DAYS) {
                g_gov[i].gate = 0;
            }
        } else if (loc != g_gov[i].gate) {
            g_gov[i].gate = 0;
        } else if (today - g_gov[i].arrived > GOV_STAY_DAYS) {
            g_gov[i].gate = 0;
        }
    }
    /* 거점마다 신분 1~4 무장 수(출발 거점의 마지막 무장은 보내지 않는다) */
    int here[0x3E] = {0};
    for (int id = 0; id <= PERSON_MAX; id++) {
        uint8_t *p = root_slot(ROOT_PERSONS, id);
        int loc = is_person(p) ? *(uint16_t *)(p + 0xE4) : -1;
        if (loc >= 1 && loc <= 0x3D && p[0xE6] >= 1 && p[0xE6] <= 4)
            here[loc]++;
    }
    for (int gid = 0x34; gid <= 0x3D; gid++) {
        uint8_t *gate = foothold(root, gid);
        int fid = valid(gate) ? fh_owner(gate) : 0, gar = valid(gate) ? *(int *)(gate + 0x3C) : 0, busy = 0;
        uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
        if (!valid(force) || gar <= 0 || CALL(ObjFn, FN_IS_PLAYER)(force) ||
            is_person(CALL(uint8_t * (__fastcall *)(void *), FN_FH_LEADER)(gate)))
            continue;
        for (int i = 0; i < GOV_MAX; i++)
            busy |= g_gov[i].gate == gid && !g_gov[i].arrived;
        if (busy)
            continue;
        int h = CALL(ObjFn, FN_HEX)(gate), foe = 0, foe_sol = 0;
        for (int i = 1; i <= UNIT_MAX; i++) {
            uint8_t *u = root_slot(ROOT_UNITS, i);
            int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0;
            if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && dist(h, *(uint16_t *)(u + 0x2A)) <= GOV_ENEMY_R)
                foe++, foe_sol += *(uint16_t *)(u + 0x16);
        }
        if (!foe)
            continue;
        int dur = CALL(ObjFn, FN_FH_DUR)(gate), dmax = fh_vint(gate, 0x60);
        if (dmax <= 0 || dur * 100 < dmax * GATE_DUR_PCT || (float)gar * 100 < (float)foe_sol * GOV_LOW_PCT) {
            continue;
        }
        uint8_t *best = NULL, *from = NULL;
        int best_lead = -1, best_d = 0;
        for (int id = 0; id <= PERSON_MAX; id++) {
            uint8_t *p = root_slot(ROOT_PERSONS, id);
            int loc = is_person(p) ? *(uint16_t *)(p + 0xE4) : -1;
            if (loc < 1 || loc > 0x3D || loc == gid || p[0xE6] != 4 || (*(uint16_t *)(p + 0x15E) & 1) || here[loc] < 2 ||
                force_id(p) != fid)
                continue;
            uint8_t *o = foothold(root, loc);
            int d = valid(o) && fh_owner(o) == fid ? dist(h, CALL(ObjFn, FN_HEX)(o)) : GOV_FROM_R + 1, lead = ability(p, 0);
            if (d <= GOV_FROM_R && lead > best_lead)
                best = p, from = o, best_lead = lead, best_d = d;
        }
        if (!best) {
            continue;
        }
        int pid = *(uint16_t *)(best + 0x10), fromid = *(uint16_t *)(from + 0x4E);
        move_person(best, from, gate);
        here[fromid]--;
        for (int i = 0; i < GOV_MAX; i++)
            if (!g_gov[i].gate) {
                g_gov[i].gate = gid, g_gov[i].person = pid, g_gov[i].sent = today, g_gov[i].arrived = 0;
                break;
            }
    }
}

/* ---------- H. 그룹 10 전선 보강 수송 (빌드 53, 2026-11-05 사용자 결정) ----------
   원작 수송 노드 CommandTransportDepature(0x32230)를 그대로 쓴다. 난이도 확률·군단 명령서·예비·부족 문턱·짝 표·5칸 적
   검사는 원작이고, 고르는 것(어디로·얼마나)만 바꾼다(빌드 52는 명령서 없이 순마다 따로 내서 도시가 많은 조조가 72만을 옮겼다).
   분류: 최전선 = 이웃(거점 +0x58, 자기 관문은 건너 봄)에 주요 적대 세력(도시 TR_MAJOR개 이상 또는 플레이어, 동맹·연합 아님)
   거점이 있는 자기 도시. 직전 도시 = 최전선과 이웃한 최전선 아닌 자기 도시. 그 밖은 후방.
   - 노드 앞: 도착 목록(노드 +0x3C, 세력 전선 목록 사본)에서 최전선이 아닌 도시(소세력 경계만)나 부족이 TR_FULL 미만인
     최전선 자리를 부족한 직전 도시로 잠시 바꾼다(최전선이 차 있어도 비어 있는 직전 도시로 수송이 나오게). 노드 뒤 되돌린다.
   - 부대를 만드는 0x200820 호출(0x3490E):
     · 후방 출발: 원작 대상(경로 위 첫 도시 중계)이 부족한 직전 도시나 직전 도시 없는 최전선이 아니면, 자기 도시를 따라
       TR_REAR_HOPS(2)단계 안의 가장 가까운 그런 도시로. 없으면 원작 중계 대상 그대로(빌드 69, 2026-11-07 사용자 결정: 최전선과
       직전 두 라인을 두텁게 하되 동선이 짧은 쪽이 유리하게. 빌드 65~67은 거리 제한이 없어 흑태자 판 동탁이 후방 수송 116번
       약 38만을 흑태자 앞에 바로 쌓았고 그중 3단계 이상이 약 19만, 빌드 68은 후방 바꾸기를 뺐다).
     · 직전 출발: 원작 대상이 부족한 이웃 최전선이 아니면 가장 부족한 이웃 최전선으로.
     · 보낼 곳이 없으면 만들지 않는다(명령서도 쓰지 않는다).
     · 양은 줄이기만 한다: 병사 ≤ 출발 × TR_STAGE_PCT%(직전)·TR_REAR_PCT%(후방), 출발에 TR_KEEP_MIN 남김, 받는 쪽 부족까지,
       TR_MIN_SOL 미만이면 0. 군량은 같은 비율·받는 쪽 군량 부족까지, 원작 문턱(1만) 미만이면 0. 금은 받는 도시 금이
       TR_GOLD_LOW 미만일 때만 TR_GOLD_SEND까지. 병사가 0이고 군량·금이 있으면 원작처럼 호위 1000.
     · 직전 도시로 가는 무장은 거기 남는다(사양 +0x34 = 0). 다음 원작 수송 때 그 무장이 최전선으로 잇는다.
   - 원작은 만든 뒤 묶음 +0x16에 원래 대상을 쓴다(0x349F0, r15). 노드가 끝나면 바꾼 부대의 묶음 대상을 다시 맞춘다.
   - 관문 출발·관문 대상 수송은 양 줄이기만 하고 대상은 그대로. 최전선끼리 수송(0x30390)은 손대지 않는다 */
#define TR_MAJOR 3
#define TR_STAGE_PCT 50
#define TR_REAR_PCT 75
#define TR_MIN_SOL 3000
#define TR_KEEP_MIN 5000 /* 출발 도시에 남길 최소 병사 */
#define TR_FULL 5000     /* 부족이 이 미만인 최전선은 원작도 수요 0(병사 부족 ≤ 5000) */
#define TR_MIN_FOOD 10000
#define TR_GOLD_LOW 5000
#define TR_GOLD_SEND 10000
#define TR_ENEMY_R 5
#define TR_ESCORT 1000
#define TR_REAR_HOPS 2 /* 빌드 69: 후방 출발이 대상을 바꿀 수 있는 자기 도시 단계 수 */
enum { TR_REAR, TR_STAGE, TR_FRONT };

static int near_foe_soldiers(uint8_t *fh, int fid, int r);

/* 거점 fh의 이웃 거점 번호(자기 관문은 그 너머 이웃으로) */
static int tr_neighbors(uint8_t *root, uint8_t *fh, int fid, int *out)
{
    uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
    int n = 0;
    for (int i = 0; i < 10; i++) {
        int id = fh[0x58 + i];
        uint8_t *o = id >= 1 && id <= 0x3D ? foothold(root, id) : NULL;
        if (!valid(o))
            continue;
        if (id > 0x33 && fh_owner(o) == fid) {
            for (int j = 0; j < 10; j++) {
                int id2 = o[0x58 + j];
                if (id2 >= 1 && id2 <= 0x3D && foothold(root, id2) != fh && n < 24)
                    out[n++] = id2;
            }
        } else if (n < 24)
            out[n++] = id;
    }
    return n;
}

/* 도시 dest로 가는 수송 부대(+0x10 == 2) 수와 병사·군량 */
static int tr_incoming(int dest, int *sol, int *food)
{
    int cnt = 0;
    *sol = 0, *food = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (!valid(u) || u[0x10] != 2 || u[0x38] != 5 || *(uint16_t *)(u + 0x3A) != dest)
            continue;
        cnt++, *sol += *(uint16_t *)(u + 0x16), *food += *(int *)(u + 0x60);
    }
    return cnt;
}

/* 같은 짝(양방향) 수송이 가는 중인가(원작 0x32230의 짝 표와 같은 뜻) */
static int tr_pair_busy(int from, int dest)
{
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (!valid(u) || u[0x10] != 2 || u[0x38] != 5)
            continue;
        uint8_t *p = root_slot(ROOT_PERSONS, *(uint16_t *)(u + 0x12));
        int home = is_person(p) ? *(uint16_t *)(p + 0xE2) : -1, t = *(uint16_t *)(u + 0x3A);
        if ((home == from && t == dest) || (home == dest && t == from))
            return 1;
    }
    return 0;
}

typedef struct {
    int fid;
    unsigned char mine[0x34], kind[0x34];
    int need[0x34];       /* 최전선: 상한 − 병력 − 오는 수송 병사 */
    int stage_need[0x34]; /* 직전: 맡은 이웃 최전선 부족 최대 − 오는 수송. 직전 없는 최전선: need */
    int nn[0x34], nb[0x34][24];
} TrMap;

/* 세력 fid의 도시 분류. 도시가 2개 미만이면 0 */
static int tr_classify(TrMap *m, int fid)
{
    uint8_t *root = game_root();
    uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    int cities[0x34] = {0};
    memset(m, 0, sizeof *m);
    m->fid = fid;
    if (!valid(force))
        return 0;
    for (int id = 1; id <= 0x33; id++) {
        uint8_t *o = foothold(root, id);
        int g = valid(o) ? fh_owner(o) : 0;
        if (g >= 1 && g <= 0x33)
            cities[g]++;
    }
    if (cities[fid] < 2)
        return 0;
    for (int id = 1; id <= 0x33; id++) {
        uint8_t *o = foothold(root, id);
        if (!valid(o) || fh_owner(o) != fid)
            continue;
        m->mine[id] = 1;
        m->nn[id] = tr_neighbors(root, o, fid, m->nb[id]);
        for (int k = 0; k < m->nn[id]; k++) {
            uint8_t *q = foothold(root, m->nb[id][k]);
            int g = valid(q) ? fh_owner(q) : 0;
            uint8_t *gf = g >= 1 && g <= 0x33 ? root_slot(ROOT_FORCES, g) : NULL;
            if (valid(gf) && g != fid && !ally_months(force, g) && (cities[g] >= TR_MAJOR || CALL(ObjFn, FN_IS_PLAYER)(gf)))
                m->kind[id] = TR_FRONT;
        }
    }
    for (int id = 1; id <= 0x33; id++)
        if (m->mine[id] && m->kind[id] != TR_FRONT)
            for (int k = 0; k < m->nn[id]; k++)
                if (m->nb[id][k] <= 0x33 && m->mine[m->nb[id][k]] && m->kind[m->nb[id][k]] == TR_FRONT)
                    m->kind[id] = TR_STAGE;
    for (int id = 1; id <= 0x33; id++) {
        uint8_t *o = foothold(root, id);
        if (!m->mine[id] || m->kind[id] != TR_FRONT)
            continue;
        int in_s, in_f;
        tr_incoming(id, &in_s, &in_f);
        m->need[id] = fh_vint(o, 0xC8) - *(int *)(o + 0x3C) - in_s;
    }
    for (int t = 1; t <= 0x33; t++) {
        if (!m->mine[t])
            continue;
        if (m->kind[t] == TR_STAGE) {
            for (int k = 0; k < m->nn[t]; k++) {
                int f = m->nb[t][k];
                if (f <= 0x33 && m->mine[f] && m->kind[f] == TR_FRONT && m->need[f] > m->stage_need[t])
                    m->stage_need[t] = m->need[f];
            }
            int in_s, in_f;
            tr_incoming(t, &in_s, &in_f);
            m->stage_need[t] -= in_s;
        } else if (m->kind[t] == TR_FRONT) {
            int staged = 0;
            for (int k = 0; k < m->nn[t]; k++)
                staged |= m->nb[t][k] <= 0x33 && m->mine[m->nb[t][k]] && m->kind[m->nb[t][k]] == TR_STAGE;
            m->stage_need[t] = staged ? 0 : m->need[t];
        }
    }
    return 1;
}

static __declspec(thread) TrMap g_trm;
static __declspec(thread) int g_tr_on;
#define TR_FIX_MAX 16
static __declspec(thread) struct {
    uint8_t *unit;
    int dst;
} g_tr_fix[TR_FIX_MAX];
static __declspec(thread) int g_tr_nfix;

/* 출발 sid(분류 sk)에서 보낼 대상. 원작 대상 odst가 맞으면 그대로. 없으면 0 */
static int tr_pick(int sid, int sk, int odst)
{
    TrMap *m = &g_trm;
    uint8_t *root = game_root();
    uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    if (sk == TR_STAGE) {
        int ok = odst >= 1 && odst <= 0x33 && m->mine[odst] && m->kind[odst] == TR_FRONT && m->need[odst] >= TR_MIN_SOL;
        for (int k = 0; ok && k < m->nn[sid]; k++)
            if (m->nb[sid][k] == odst)
                return odst;
        int best = 0;
        for (int k = 0; k < m->nn[sid]; k++) {
            int f = m->nb[sid][k];
            uint8_t *d = f <= 0x33 ? foothold(root, f) : NULL;
            if (f <= 0x33 && m->mine[f] && m->kind[f] == TR_FRONT && m->need[f] >= TR_MIN_SOL && (!best || m->need[f] > m->need[best]) &&
                !tr_pair_busy(sid, f) && !near_foe_soldiers(d, m->fid, TR_ENEMY_R))
                best = f;
        }
        return best;
    }
    if (odst >= 1 && odst <= 0x33 && m->mine[odst] && m->stage_need[odst] >= TR_MIN_SOL)
        return odst;
    /* 빌드 69: 자기 도시를 따라 sid에서 TR_REAR_HOPS단계 안의 도시만(이웃 = 거점 +0x58, 자기 관문은 건너 봄) */
    unsigned char hop[0x34];
    memset(hop, 0xFF, sizeof hop);
    int q[0x34], qh = 0, qt = 0;
    hop[sid] = 0, q[qt++] = sid;
    while (qh < qt) {
        int x = q[qh++];
        if (hop[x] >= TR_REAR_HOPS)
            continue;
        for (int k = 0; k < m->nn[x]; k++) {
            int y = m->nb[x][k];
            if (y >= 1 && y <= 0x33 && m->mine[y] && hop[y] == 0xFF)
                hop[y] = (unsigned char)(hop[x] + 1), q[qt++] = y;
        }
    }
    uint8_t *o = foothold(root, sid);
    int h = CALL(ObjFn, FN_HEX)(o), tgt = 0, td = 1 << 20;
    for (int t = 1; t <= 0x33; t++) {
        if (!m->mine[t] || t == sid || m->stage_need[t] < TR_MIN_SOL || hop[t] > TR_REAR_HOPS)
            continue;
        uint8_t *qf = foothold(root, t);
        int d = dist(h, CALL(ObjFn, FN_HEX)(qf));
        if (d < td && !tr_pair_busy(sid, t) && !near_foe_soldiers(qf, m->fid, TR_ENEMY_R))
            tgt = t, td = d;
    }
    return tgt ? tgt : odst; /* 빌드 69: 2단계 안에 없으면 원작 중계 대상 그대로 */
}

/* 0x32230 안 0x200820(사양, 0). 사양 [0] 무장 [1] 병사 [2] 진형 [3] 금 [4] 군량 [5..9] 중계 칸 [10] 행동 2/3 [11] 대상 종류
   5 도시 / 6 관문(id − 0x33) [12] 대상 [13] +0x34 무장 집으로 [14] +0x41 */
static uint64_t __fastcall tr_issue_hook(int *spec, int flag)
{
    UnitCreateFn orig = CALL(UnitCreateFn, FN_TR_ISSUE);
    if (!g_tr_on || !spec || !g_v3)
        return orig(spec, flag);
    int sid = 0, odst = 0, ndst = 0, sk = TR_REAR, o_sol = 0, o_food = 0, o_gold = 0, skip = 0;
    uint8_t *src = NULL, *dst = NULL;
    __try {
        uint8_t *root = game_root();
        uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
        uint8_t *p = spec[0] >= 0 && spec[0] <= PERSON_MAX ? root_slot(ROOT_PERSONS, spec[0]) : NULL;
        src = is_person(p) ? foothold_of(p) : NULL;
        sid = valid(src) ? *(uint16_t *)(src + 0x4E) : 0;
        odst = spec[11] == 6 ? spec[12] + 0x33 : spec[12];
        o_sol = spec[1], o_food = spec[4], o_gold = spec[3];
        if (sid < 1 || sid > 0x33 || !g_trm.mine[sid] || g_trm.kind[sid] == TR_FRONT)
            return orig(spec, flag); /* 관문 출발·분류 밖은 원작 그대로 */
        sk = g_trm.kind[sid];
        ndst = odst > 0x33 ? odst : tr_pick(sid, sk, odst); /* 관문 대상은 그대로. 후방 출발은 2단계 안에서만 바꿈(빌드 69) */
        dst = ndst ? foothold(root, ndst) : NULL;
        if (!ndst || !valid(dst)) {
            skip = 1;
        } else {
            int pct = sk == TR_STAGE ? TR_STAGE_PCT : TR_REAR_PCT;
            int sg = *(int *)(src + 0x3C), sf = *(int *)(src + 0x38), sm = *(int *)(src + 0x34);
            /* 후방 출발이 원작 중계 대상 그대로면 받는 쪽 부족으로 자르지 않는다(원작 병사가 이미 최종 대상 부족까지다) */
            int relay = sk == TR_REAR && ndst == odst && ndst <= 0x33 && g_trm.stage_need[ndst] < TR_MIN_SOL;
            int want = ndst > 0x33 || relay ? 1 << 30 : g_trm.kind[ndst] == TR_STAGE ? g_trm.stage_need[ndst] : g_trm.need[ndst];
            int sol = spec[1];
            if (sol > TR_ESCORT) {
                int cap = sg * pct / 100;
                cap = cap < sg - TR_KEEP_MIN ? cap : sg - TR_KEEP_MIN;
                cap = cap < want ? cap : want;
                sol = sol < cap ? sol : cap;
                if (sol < TR_MIN_SOL)
                    sol = 0;
            }
            int food = 0, gold = 0;
            if (ndst <= 0x33) {
                int in_s, in_f;
                tr_incoming(ndst, &in_s, &in_f);
                int fneed = fh_vint(dst, 0xC0) - *(int *)(dst + 0x38) - in_f;
                food = spec[4];
                food = food < sf * pct / 100 ? food : sf * pct / 100;
                food = food < fneed ? food : fneed;
                food = food >= TR_MIN_FOOD ? food : 0;
                if (*(int *)(dst + 0x34) < TR_GOLD_LOW) {
                    gold = spec[3];
                    gold = gold < sm * pct / 100 ? gold : sm * pct / 100;
                    gold = gold < TR_GOLD_SEND ? gold : TR_GOLD_SEND;
                    gold = gold >= 3000 ? gold : 0;
                }
            }
            if (!sol && (food || gold) && sg - TR_ESCORT >= TR_KEEP_MIN)
                sol = TR_ESCORT; /* 원작처럼 호위 */
            if (!sol)
                skip = 1;
            else {
                spec[1] = sol, spec[3] = gold, spec[4] = food;
                if (ndst != odst) {
                    spec[10] = 2, spec[11] = 5, spec[12] = ndst;
                    for (int i = 5; i <= 9; i++)
                        spec[i] = 0xBD10;
                }
                if (ndst <= 0x33 && g_trm.kind[ndst] == TR_STAGE)
                    spec[13] = 0; /* 무장이 직전 도시에 남아 다음 수송 때 최전선으로 */
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[수송] 대상·양 고르기 중 예외. 원작 그대로");
        return orig(spec, flag);
    }
    if (skip)
        return 0;
    uint64_t r = orig(spec, flag);
    __try {
        if (r && valid((void *)r)) {
            if (ndst != odst) {
                if (g_tr_nfix < TR_FIX_MAX)
                    g_tr_fix[g_tr_nfix].unit = (uint8_t *)r, g_tr_fix[g_tr_nfix].dst = ndst, g_tr_nfix++;
            }
            if (ndst <= 0x33) {
                if (g_trm.kind[ndst] == TR_FRONT)
                    g_trm.need[ndst] -= spec[1];
                g_trm.stage_need[ndst] -= spec[1];
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

/* 원작 수송 노드. 도착 목록 자리를 직전 도시로 잠시 바꾸고 tr_issue_hook이 대상·양을 고친다 */
static int __fastcall transport_node_hook(uint8_t *node, void *ctx, void *corps)
{
    NodeFn orig = CALL(NodeFn, FN_TRANSPORT);
    int fid = valid(corps) ? ((uint8_t *)corps)[0x10] : 0;
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (!g_v3 || !valid(force) || CALL(ObjFn, FN_IS_PLAYER)(force))
        return orig(node, ctx, corps);
    void *dl = NULL;
    uint8_t *items[MAX_CAND], *keep[MAX_CAND];
    int n = 0, swapped = 0;
    __try {
        if (!tr_classify(&g_trm, fid))
            return orig(node, ctx, corps);
        dl = bb_obj(ctx, *(uint32_t *)(node + 0x3C));
        n = dl ? list_items(dl, items, MAX_CAND) : 0;
        memcpy(keep, items, sizeof items[0] * (size_t)n);
        uint8_t *root = game_root();
        uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
        int used[0x34] = {0};
        for (int i = 0; i < n; i++)
            if (valid(items[i]) && *(uint16_t *)(items[i] + 0x4E) <= 0x33)
                used[*(uint16_t *)(items[i] + 0x4E)] = 1;
        for (int i = 0; i < n; i++) {
            int id = valid(items[i]) ? *(uint16_t *)(items[i] + 0x4E) : 0;
            if (id < 1 || id > 0x33 || !g_trm.mine[id] || (g_trm.kind[id] == TR_FRONT && g_trm.need[id] >= TR_FULL) ||
                (g_trm.kind[id] == TR_STAGE && g_trm.stage_need[id] >= TR_MIN_SOL))
                continue; /* 관문·부족한 최전선·부족한 직전 도시는 그대로 */
            int best = 0;
            for (int t = 1; t <= 0x33; t++) {
                if (used[t] || !g_trm.mine[t] || g_trm.kind[t] != TR_STAGE || g_trm.stage_need[t] < TR_MIN_SOL)
                    continue;
                uint8_t *q = foothold(root, t);
                int in_s, in_f;
                tr_incoming(t, &in_s, &in_f);
                if (fh_vint(q, 0xC8) - *(int *)(q + 0x3C) - in_s < TR_FULL)
                    continue; /* 직전 도시 자신이 차 있으면 원작 수요가 0 */
                if (!best || g_trm.stage_need[t] > g_trm.stage_need[best])
                    best = t;
            }
            if (!best)
                break;
            items[i] = foothold(root, best), used[best] = 1, swapped++;
        }
        if (swapped)
            list_rewrite(dl, items, n);
        g_tr_nfix = 0;
        g_tr_on = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (swapped)
            list_rewrite(dl, keep, n);
        g_tr_on = 0;
        plog("[수송] 도착 후보 바꾸기 중 예외. 원작 그대로");
        return orig(node, ctx, corps);
    }
    int r = orig(node, ctx, corps);
    g_tr_on = 0;
    __try {
        if (swapped)
            list_rewrite(dl, keep, n);
        for (int i = 0; i < g_tr_nfix; i++) {
            uint8_t *tr = valid(g_tr_fix[i].unit) ? CALL(uint8_t * (__fastcall *)(void *), FN_UNIT_TROOPS)(g_tr_fix[i].unit) : NULL;
            if (valid(tr) && *(uint16_t *)(tr + 0x14) == 0xC)
                *(uint16_t *)(tr + 0x16) = (uint16_t)g_tr_fix[i].dst;
        }
        g_tr_nfix = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

/* 빌드 50: 거점(도시·관문) 주인이 바뀐 날. 순마다 intercept_track이 갱신한다. 세션이 바뀌면(불러오기) 지우고,
   처음 본 주인은 "점령 아님"(날 0)으로 둔다 */
static volatile LONG g_fh_owner[0x3E], g_fh_taken[0x3E], g_fh_session;
/* 빌드 51 역습 창: 거점 병력을 순마다 COUNTER_SLOTS칸 고리에 둔다(10일 × 6 = 최근 60일) */
#define COUNTER_SLOTS 6
static volatile LONG g_fh_hist[0x3E][COUNTER_SLOTS], g_hist_pos;
static volatile LONG g_fh_dur[0x3E][COUNTER_SLOTS]; /* 빌드 63: 관문 내구, g_fh_hist와 같은 고리(0 = 기록 없음) */
static void fh_owner_track(void)
{
    uint8_t *root = game_root();
    int sess = turn_session(), today = day_key();
    if (InterlockedExchange(&g_fh_session, sess) != sess) {
        for (int id = 0; id < 0x3E; id++) {
            g_fh_owner[id] = 0, g_fh_taken[id] = 0;
            for (int k = 0; k < COUNTER_SLOTS; k++)
                g_fh_hist[id][k] = 0, g_fh_dur[id][k] = 0;
        }
        g_hist_pos = 0;
    }
    int pos = g_hist_pos;
    g_hist_pos = (pos + 1) % COUNTER_SLOTS;
    for (int id = 1; id <= 0x3D; id++) {
        uint8_t *o = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, id);
        g_fh_hist[id][pos] = valid(o) ? *(int *)(o + 0x3C) : 0;
        g_fh_dur[id][pos] = valid(o) && id >= 0x34 ? CALL(ObjFn, FN_FH_DUR)(o) + 1 : 0; /* +1: 내구 0도 기록 있음으로 */
        int g = valid(o) ? fh_owner(o) : 0;
        if (g <= 0)
            continue;
        if (g_fh_owner[id] && g_fh_owner[id] != g)
            g_fh_taken[id] = today;
        g_fh_owner[id] = g;
    }
}

/* 빌드 63: 관문 gate에 거점 from의 부대가 도착할 때의 예상 내구. 감소 빠르기 = 최근 GATE_DUR_TURNS순 안 가장 오래된 기록과
   지금의 차 / 순 수(늘었으면 0), 가는 순 = ⌈칸 거리 / GATE_MARCH_HEX⌉ */
static int gate_dur_arrive(uint8_t *gate, uint8_t *from, int *rate, int *turns)
{
    int dur = CALL(ObjFn, FN_FH_DUR)(gate), id = *(uint16_t *)(gate + 0x4E);
    int(__fastcall *hd)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int d = valid(from) ? hd(CALL(ObjFn, FN_HEX)(from), CALL(ObjFn, FN_HEX)(gate)) : 0;
    *rate = 0;
    *turns = (d + GATE_MARCH_HEX - 1) / GATE_MARCH_HEX;
    if (id < 0x34 || id > 0x3D)
        return dur;
    int last = (g_hist_pos + COUNTER_SLOTS - 1) % COUNTER_SLOTS;
    for (int k = GATE_DUR_TURNS; k >= 1; k--) {
        int v = g_fh_dur[id][(last - k + COUNTER_SLOTS * 2) % COUNTER_SLOTS] - 1;
        if (v >= 0) {
            *rate = v > dur ? (v - dur + k - 1) / k : 0;
            break;
        }
    }
    int a = dur - *rate * *turns;
    return a < 0 ? 0 : a;
}

/* 최근 60일(순 기록) 거점 병력 최고값 */
static int fh_recent_max(uint8_t *fh)
{
    int id = valid(fh) ? *(uint16_t *)(fh + 0x4E) : 0, mx = 0;
    for (int k = 0; id >= 1 && id <= 0x3D && k < COUNTER_SLOTS; k++)
        mx = g_fh_hist[id][k] > mx ? g_fh_hist[id][k] : mx;
    return mx;
}

/* 그 거점을 지금 주인이 점령한 지 며칠(이 세션에서 본 적 없으면 아주 큼) */
static int fh_taken_days(uint8_t *fh)
{
    int id = valid(fh) ? *(uint16_t *)(fh + 0x4E) : 0;
    if (id < 1 || id > 0x3D || !g_fh_taken[id] || g_fh_owner[id] != fh_owner(fh))
        return 1 << 20;
    return day_key() - g_fh_taken[id];
}

void intercept_track(void)
{
    InterlockedIncrement(&g_turn); /* 처음 E 표를 순마다 새로 */
    if (!g_v3)
        return;
    __try {
        fh_owner_track();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    AcquireSRWLockExclusive(&g_track_lock);
    __try {
        for (int t = 0; t < TRACK_MAX; t++) {
            int pid = g_track[t].person, gid = g_track[t].gate;
            if (!pid)
                continue;
            uint8_t *unit = NULL;
            for (int i = 1; i <= UNIT_MAX && !unit; i++) {
                uint8_t *u = root_slot(ROOT_UNITS, i);
                if (valid(u) && *(uint16_t *)(u + 0x12) == pid)
                    unit = u;
            }
            if (unit) {
                /* 대기(13)로 끊겼으면 관문으로 들어가기를 다시 건다(경로는 +0x48 = 0xBD10으로 다시 계산) */
                int again = unit[0x37] == 13 && *(uint8_t *)(unit + 0x140) == 0;
                if (again) {
                    ((void(__fastcall *)(void *, int))RVA(FN_SET_ACTION))(unit, 3);
                    ((void(__fastcall *)(void *, int, int))RVA(FN_SET_TARGET))(unit, 6, gid - 0x33);
                    *(uint16_t *)(unit + 0x48) = 0xBD10;
                }
                continue;
            }
            g_track[t].person = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[입성추적] 예외. 추적 목록 비움");
        memset(g_track, 0, sizeof g_track);
    }
    ReleaseSRWLockExclusive(&g_track_lock);
    __try {
        gate_governor();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int warned;
        if (!warned++)
            plog("[관문태수] 예외. 이번 순은 건너뜀");
        memset(g_gov, 0, sizeof g_gov);
    }
}

static int __fastcall addarmy_hook(uint8_t *node, void *ctx, void *a3)
{
    if (g_skip_add) {
        g_skip_add = 0;
        return 1;
    }
    return CALL(NodeFn, FN_ADDARMY)(node, ctx, a3);
}

static float __fastcall unit_power_hook(void *unit)
{
    float r = est_power(unit);
    if (g_v3 && g_atk_node) {
        __try {
            uint8_t *ret = (uint8_t *)_ReturnAddress();
            for (int i = 0; i < (int)_countof(ATTACK_POWER_CALLS); i++)
                if (ret == RVA(ATTACK_POWER_CALLS[i] + 5))
                    r *= naval_attack((uint8_t *)unit);
            /* 포화 통과 공격: 게임 끝 취소(합 < 처음 필요)가 필요의 SAT_END_PCT%에서 갈리게 */
            if (g_atk_sat && ret == RVA(SITE_ATK_END_POWER + 5))
                r *= 100.0f / SAT_END_PCT;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return r;
}

static __declspec(thread) int g_atk_res0; /* 공격 노드의 원래 예비(+0x88). 거점마다 위협 예비로 바꿨다가 끝나면 되돌린다 */

/* 빌드 50(2026-11-05 사용자 결정): 만든 부대 us[0..n)로 관문 gate를 함락하는 데 걸릴 날 수(못 하면 GATE_SIM_MAX + 1).
   거점을 칠 때 부대 공격은 공성(0x2B2440 ≈ 진형 공성 × 전력 × 0.05), 관문 방어 = 16 × 관문 전력 × 0.05 + 내구 × 0.2(0x2A9E50),
   통상 피해 = ((양쪽 병력)/300 + 300) × max(1, m²/2000)/1500, m = max(1, 1000 + 공격 − 방어)(0x16A1D0), 파성 =
   통솔 × 진형 파성/100 × (20000 + 병력)/10000(0x2AA2E0), 관문 전력은 분할 = 부대 수(0x2A8D40). 하루 한 번 싸운다고 보고,
   근접·공성 부대는 관문 옆 GATE_ADJ칸까지만 친다(안행은 2칸 거리). 내구 마지막 1은 옆 칸 부대만 깎는다(0x29E840).
   관문 병력 보충·우리 손실·시설 보너스는 넣지 않는다 */
static int gate_days(uint8_t *gate, uint8_t **us, int n, char *note, size_t nsz)
{
    uint8_t *boss = CALL(uint8_t * (__fastcall *)(void *), FN_FH_LEADER)(gate);
    int L = is_person(boss) ? ability(boss, 0) : 0, mor = gate[0x44] < 100 ? gate[0x44] : 100;
    float sg = (float)*(int *)(gate + 0x3C), dur = (float)CALL(ObjFn, FN_FH_DUR)(gate), cap = (float)fire_cap(L);
    float A[32], S[32], bsum = 0;
    int(__fastcall *ustat)(void *, int, int) = (int(__fastcall *)(void *, int, int))RVA(FN_UNIT_FORM_STAT);
    int adj_n = 0, m = 0, k = n < 32 ? n : 32;
    for (int i = 0; i < k; i++) {
        uint8_t *u = us[i], *pp = root_slot(ROOT_PERSONS, *(uint16_t *)(u + 0x12));
        int ranged = u[0x1B] == F_ANHAENG, lead = is_person(pp) ? ability(pp, 0) : 0;
        if (!ranged && adj_n >= GATE_ADJ)
            continue;
        adj_n += !ranged;
        /* 빌드 51: 전투 전력 p = 0x2A8D40(부대, NULL, 분할 1, 전투 1). 빌드 50은 0xB5EC0(= p × 병력 / 상수)을 써서 공격이
           몇 배로 부풀었다(빌드 50 판 관문 57: 관문 2,398명에 첫날 피해 4,099 → 1일) */
        float pw = ((float(__fastcall *)(void *, void *, int, int))RVA(FN_BATTLE_POWER))(u, NULL, 1, 1);
        S[m] = (float)*(uint16_t *)(u + 0x16);
        A[m++] = (float)ustat(u, 1, 3) * pw * 0.05f;
        float b = (float)lead * (float)ustat(u, 1, 4) / 100 * (20000 + S[m - 1]) / 10000;
        bsum += b > 1 ? b : 1;
    }
    int day = 0;
    float d0 = 0, b0 = bsum;
    for (day = 1; day <= GATE_SIM_MAX && m; day++) {
        float split = (float)m, pg = (sqrtf((sg < cap ? sg : cap) / split) + 2 * (float)L + 200) * 10 / (9 + split) * (200 + (float)mor) / 300;
        float dg = 16 * pg * 0.05f + dur * 0.2f, hit = 0;
        for (int i = 0; i < m; i++) {
            float mm = 1000 + A[i] - dg;
            mm = mm > 1 ? mm : 1;
            float q = mm * mm / 2000;
            q = q > 1 ? q : 1;
            hit += ((S[i] + sg) / 300 + 300) * q / 1500;
        }
        if (day == 1)
            d0 = hit;
        sg -= hit;
        dur -= bsum;
        if (dur < 1 && !adj_n)
            dur = 1;
        if (sg <= 0 || dur <= 0)
            break;
    }
    _snprintf_s(note, nsz, _TRUNCATE, "부대 %d(근접·공성 %d) 관문 병력 %d 내구 %d 대장 통솔 %d, 첫날 병력 피해 %.0f·내구 %.0f",
                m, adj_n, *(int *)(gate + 0x3C), CALL(ObjFn, FN_FH_DUR)(gate), L, d0, b0);
    return m ? day : GATE_SIM_MAX + 1;
}

static volatile LONG g_gate_cd[0x34][10]; /* 빌드 52: 세력·관문(id − 0x34)별 마지막 관문 공격 취소 날(함락 예상 > GATE_DAYS) */

/* 공격 노드 끝 재확인. 출진 판단은 첫 거점 정렬 때 참여 거점 목록 전체의 가용 병력으로 하므로, 장수를 내지 못하는
   거점(2026-11-02 판: 장사 공격에서 강릉)까지 세어 부풀 수 있다. 원작 끝 취소(0x30275)는 만든 부대 전력을 처음 필요
   전력과만 견준다. 그래서 노드가 성공(1)으로 끝나면 실제로 만든 부대 전력(원작 합과 같은 est_power × 수상 배율)을
   (필요 + 지원 예상)의 ATK_END_PCT%와 다시 견주고, 모자라면 원작처럼 묶음마다 0x9E900으로 취소하고 실패(2)를 돌려준다.
   2026-11-02: 판단(ATK_GO_PCT 70%)보다 낮게 60%. 판단 때 예상과 실제 생성 사이 오차(10% 안팎)로 취소되던 공격을 살린다 */
/* 빌드 52: 거점 fh(세력 fid) r칸 안 적대(동맹·연합 아님) 부대 병사 합 */
static int near_foe_soldiers(uint8_t *fh, int fid, int r)
{
    return near_foe_except(fh, fid, r, NULL, 0, NULL);
}

/* 빌드 71: near_foe_soldiers에서 skip 목록의 부대와 lead 세력의 lead ENEMY_NEAR칸 안 부대(지금 막으러 나가는 적 무리)를 뺀다 */
static int near_foe_except(uint8_t *fh, int fid, int r, uint8_t **skip, int ns, uint8_t *lead)
{
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (!valid(force))
        return 0;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int h = CALL(ObjFn, FN_HEX)(fh), sum = 0;
    int lf = valid(lead) ? CALL(ObjFn, FN_UNIT_FORCE)(lead) : 0, lh = valid(lead) ? *(uint16_t *)(lead + 0x2A) : 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0;
        if (g < 1 || g > 45 || g == fid || ally_months(force, g) || dist(h, *(uint16_t *)(u + 0x2A)) > r)
            continue;
        int skipped = lf && g == lf && dist(lh, *(uint16_t *)(u + 0x2A)) <= ENEMY_NEAR;
        for (int j = 0; j < ns && !skipped; j++)
            skipped = skip[j] == u;
        if (!skipped)
            sum += *(uint16_t *)(u + 0x16);
    }
    return sum;
}

static int atk_undo(uint8_t *node)
{
    uint8_t *groups[MAX_CAND], *target = *(uint8_t **)(node + 0x60);
    int n = list_items(node + ATK_RESULT, groups, MAX_CAND), units = 0, fid = 0;
    if (g_atk_want <= 0 || n < 1 || !valid(target))
        return 0;
    float made = 0;
    uint8_t *us[32];
    for (int g = 0; g < n; g++) {
        int id = valid(groups[g]) ? CALL(ObjFn, FN_TROOPS_ID)(groups[g]) : 0;
        for (int i = 1; i <= UNIT_MAX && id; i++) {
            uint8_t *u = root_slot(ROOT_UNITS, i);
            if (valid(u) && *(uint16_t *)(u + 0x58) == id) {
                if (units < 32)
                    us[units] = u;
                made += est_power(u) * naval_attack(u);
                fid = fid ? fid : CALL(ObjFn, FN_UNIT_FORCE)(u);
                units++;
            }
        }
    }
    /* 포화 통과는 원작 필요의 SAT_END_PCT%(게임 끝 취소도 같은 기준, unit_power_hook) */
    int pct = g_atk_sat ? SAT_END_PCT : g_atk_cross ? ATK_END_CROSS_PCT : ATK_END_PCT, keep = !units || made * 100 >= g_atk_want * pct;
    if (keep) {
        for (int i = 0; units && g_atk_sat && i < g_sat_n; i++) {
            int id = valid(g_sat_fh[i]) ? *(uint16_t *)(g_sat_fh[i] + 0x4E) : 0;
            if (id >= 1 && id <= 0x3D)
                g_sat_last[id] = day_key();
        }
        /* 빌드 50 관문: 함락 예상이 GATE_DAYS일을 넘으면 취소(빌드 49 판 B 관문 57: 근접 2부대가 석 달 녹음) */
        if (!units || *(uint16_t *)(target + 0x4E) <= 0x33)
            return 0;
        char gnote[200];
        int days = gate_days(target, us, units < 32 ? units : 32, gnote, sizeof gnote);
        if (days <= GATE_DAYS) {
            return 0;
        }
        for (int g = 0; g < n; g++)
            if (valid(groups[g]))
                CALL(ObjFn, FN_SORTIE_CANCEL)(groups[g]);
        if (fid >= 1 && fid <= 0x33)
            g_gate_cd[fid][*(uint16_t *)(target + 0x4E) - 0x34] = day_key(); /* 빌드 52: 같은 세력·관문은 GATE_CD_DAYS일 쉼 */
        char dtxt[16];
        _snprintf_s(dtxt, sizeof dtxt, _TRUNCATE, days > GATE_SIM_MAX ? "%d일 넘음" : "%d일", days > GATE_SIM_MAX ? GATE_SIM_MAX : days);
        return 1;
    }
    for (int g = 0; g < n; g++)
        if (valid(groups[g]))
            CALL(ObjFn, FN_SORTIE_CANCEL)(groups[g]);
    return 1;
}

static int __fastcall attack_node_hook(uint8_t *node, void *ctx, void *a3)
{
    uint8_t *prev = g_atk_node;
    int prev_res = g_atk_res0;
    g_atk_node = node;
    g_atk_res0 = *(int *)(node + 0x88);
    float prev_extra = g_atk_extra;
    g_atk_extra = -1;
    /* 출진 판단은 노드 실행(0x2F2B0)이 인자를 노드 필드에 채운 뒤라야 하므로 첫 거점 정렬(atk_threat_reserve)에서 한다 */
    int prev_go = g_atk_go;
    g_atk_go = -1;
    float prev_want = g_atk_want;
    g_atk_want = -1;
    int prev_bump = g_atk_bump, prev_sat = g_atk_sat, prev_capped = g_atk_capped, prev_cross = g_atk_cross;
    g_atk_bump = 0, g_atk_sat = 0, g_atk_capped = 0, g_atk_cross = 0;
    g_sg.fh = NULL;
    memset(&g_fc, 0, sizeof g_fc);
    /* 빌드 52(2026-11-05 사용자 결정): 모드 0xB(0x4ABE0)는 정렬이 없어 출진 판단·위협 예비가 걸리지 않는다. 원작 공격은 그대로
       두고, 참여 거점 THREAT_UNIT_R칸 안 적 부대 병사 합(가장 큰 거점 값)만 노드 예비(+0x88)로 올린다(0x4AECD·0x4AF7A가 읽음).
       적 거점 병력 몫은 넣지 않는다(빌드 51 판 1 유비 강릉 → 시상 습격이 그대로 나가게) */
    int mode_b = g_v3 && *(int *)(node + 0x4C) == 0xB, res_b = *(int *)(node + 0x88);
    if (mode_b) {
        __try {
            uint8_t *fhs[MAX_CAND];
            int nf = list_items(*(void **)(node + 0x78), fhs, MAX_CAND), worst = 0;
            for (int i = 0; i < nf; i++) {
                int u = valid(fhs[i]) ? near_foe_soldiers(fhs[i], fh_owner(fhs[i]), THREAT_UNIT_R) : 0;
                if (u > worst)
                    worst = u;
            }
            if (worst > res_b)
                *(int *)(node + 0x88) = worst;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            *(int *)(node + 0x88) = res_b;
        }
    }
    int r = CALL(NodeFn, FN_ATTACK)(node, ctx, a3);
    if (mode_b)
        *(int *)(node + 0x88) = res_b;
    atk_unbump();
    __try {
        if (g_v3 && V102_STAGE >= 3 && r == 1 && g_atk_go == 0 && atk_undo(node)) /* 끝 재확인·관문 함락 예상: 2단계에는 없음 */
            r = 2;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_atk_go = prev_go;
    g_atk_want = prev_want;
    g_atk_bump = prev_bump;
    g_atk_sat = prev_sat;
    g_atk_capped = prev_capped;
    g_atk_cross = prev_cross;
    g_atk_extra = prev_extra;
    *(int *)(node + 0x88) = g_atk_res0;
    g_atk_node = prev;
    g_atk_res0 = prev_res;
    return r;
}

/* 거점 owner 세력 번호(없으면 0) */
static int fh_owner(uint8_t *fh)
{
    uint8_t *f = foothold_force(fh);
    return f ? (*(int(__fastcall **)(void *))(*(uint8_t **)f + 0x60))(f) : 0;
}

/* 목표 주인과 그 동맹·연합(ally_months)의 거점 중 목표에서 SUPPORT_R칸 안의 (병력 − SUPPORT_KEEP) 합.
   목표 자신은 다 셈(게임 필요 전력은 성 전력이라 큰 수비 병력을 거의 반영하지 않는데, 그 병력은 요격으로 나온다),
   나머지는 × SUPPORT_PCT%. SUPPORT_R = 요격 경계 수집 25칸 + 공격 부대가 목표에 다가서는 거리.
   2026-11-02 다른 전선 할인(사용자 결정 1안 + 2안 약하게): 공격 세력 fid가 아닌 목표 주인의 적(공격 세력의 동맹도 아님)
   부대가 SUPPORT_BUSY_R칸 안에 있는 거점은 SUPPORT_BUSY_PCT%만 세고, 목표 주인의 공격 중 야전 병사(목표에서 SUPPORT_R칸 밖)가
   그 세력 거점 병력 합의 MAIN_AWAY_PCT%면 목표 말고 주변 지원을 MAIN_AWAY_CUT%까지(비례) 깎는다. why에 근거 */
static int atk_support(uint8_t *target, int fid, char *why, size_t wsz, int *field_out)
{
    int own = valid(target) ? fh_owner(target) : 0;
    uint8_t *force = own >= 1 && own <= 0x33 ? root_slot(ROOT_FORCES, own) : NULL;
    uint8_t *af = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (why && wsz)
        why[0] = 0;
    if (field_out)
        *field_out = 0;
    if (!valid(force))
        return 0;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int h = CALL(ObjFn, FN_HEX)(target), busy_hex[UNIT_MAX], nb = 0, away_sol = 0, gar_all = 0, sum_t = 0, nbusy = 0;
    float sum_n = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0, uh = g ? *(uint16_t *)(u + 0x2A) : 0;
        if (g == own && (u[0x37] == 4 || u[0x37] == 5) && dist(h, uh) > SUPPORT_R)
            away_sol += *(uint16_t *)(u + 0x16);
        else if (g >= 1 && g <= 45 && g != own && !ally_months(force, g) && g != fid && !(valid(af) && ally_months(af, g)))
            busy_hex[nb++] = uh;
    }
    uint8_t *root = game_root();
    for (int id = 1; id <= 0x3D; id++) {
        uint8_t *o = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, id);
        int g = valid(o) ? fh_owner(o) : 0;
        if (g == own)
            gar_all += *(int *)(o + 0x3C);
        if (g < 1 || g > 45 || (g != own && !ally_months(force, g)) || dist(h, CALL(ObjFn, FN_HEX)(o)) > SUPPORT_R)
            continue;
        int spare = *(int *)(o + 0x3C) - SUPPORT_KEEP;
        if (spare <= 0)
            continue;
        if (o == target) {
            sum_t += spare;
            continue;
        }
        int oh = CALL(ObjFn, FN_HEX)(o), busy = 0;
        for (int k = 0; k < nb && !busy; k++)
            busy = dist(oh, busy_hex[k]) <= SUPPORT_BUSY_R;
        nbusy += busy;
        sum_n += (float)spare * (busy ? SUPPORT_BUSY_PCT : SUPPORT_PCT) / 100;
    }
    float away = gar_all > 0 ? (float)away_sol * 100 / (float)gar_all : 0;
    float cut = away >= MAIN_AWAY_PCT ? MAIN_AWAY_CUT : away * MAIN_AWAY_CUT / MAIN_AWAY_PCT;
    /* 2026-11-03 목표 주변 적 야전 부대: 공격 세력과 그 동맹이 아닌 세력(목표 주인·제3세력)의 부대 병사를 FIELD_NEAR_R칸 안
       100%, FIELD_FAR_R칸 안 FIELD_FAR_PCT%로 더한다(빌드 43 판 유비 → 손권 강하: 조조 야전군 한가운데를 위험 없다고 봄) */
    /* 동맹·연합(남은 개월 = ally_months)이 ALLY_MIN_MONTHS 이상 남은 세력의 부대는 같은 거리 가중치로 적 야전 →
       주변 지원 순으로 뺀다(0 밑으로는 안 내림, 목표 성 몫은 그대로). 그보다 짧게 남은 동맹은 0(적도 아군도 아님) */
    int field = 0, ally = 0;
    if (valid(af))
        for (int i = 1; i <= UNIT_MAX; i++) {
            uint8_t *u = root_slot(ROOT_UNITS, i);
            int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0;
            if (g < 1 || g > 45 || g == fid)
                continue;
            int months = ally_months(af, g), du = dist(h, *(uint16_t *)(u + 0x2A)), s = *(uint16_t *)(u + 0x16);
            int wsol = du <= FIELD_NEAR_R ? s : du <= FIELD_FAR_R ? s * FIELD_FAR_PCT / 100 : 0;
            if (!months)
                field += wsol;
            else if (months >= ALLY_MIN_MONTHS)
                ally += wsol;
        }
    float neigh = sum_n * (100 - cut) / 100;
    int f_net = field > ally ? field - ally : 0, rest = ally > field ? ally - field : 0;
    neigh = neigh > (float)rest ? neigh - (float)rest : 0;
    if (why && wsz && (nbusy || cut >= 1))
        _snprintf_s(why, wsz, _TRUNCATE, ", 지원 할인: 다른 전선 %d곳, 주력 원정 %.0f%% → 주변 ×%.2f", nbusy, away,
                    (100 - cut) / 100);
    if (why && wsz && field)
        _snprintf_s(why + strlen(why), wsz - strlen(why), _TRUNCATE, ", 목표 주변 적 야전 %d명", field);
    if (why && wsz && ally)
        _snprintf_s(why + strlen(why), wsz - strlen(why), _TRUNCATE, ", 동맹 야전 %d명 뺌", ally);
    if (field_out)
        *field_out = f_net;
    return sum_t + (int)neigh + f_net;
}

/* 공격 모드 함수는 거점마다 정렬(0x44D13·0x46E02·0x4BCE5) 뒤 노드 +0x88(예비)을 다시 읽는다. 정렬 직후 그 거점 주변
   적(동맹 아닌 세력 1~45)의 THREAT_UNIT_R칸 안 부대 병사 합과 THREAT_FH_R칸 안 거점 병력 최대 × THREAT_FH_PCT% 중
   큰 쪽을 예비로 올린다. 위협이 있고 목표가 FAR_ATTACK칸보다 멀면 거점 병력 전부를 예비로 둬서 출진하지 않는다 */
typedef struct {
    int units, fhmax, d, too_far, res, wide; /* wide: 막 점령한 거점이라 반경 THREAT_FH_R_TAKEN */
} Threat;

/* 출발 거점 fh(세력 fid)의 위협 예비. res0 = 원래 예비. skip = 적 거점 최대에서 뺄 거점(포화 출진의 목표, 없으면 NULL) */
static void threat_of(uint8_t *fh, int fid, uint8_t *target, int res0, uint8_t *skip, Threat *t)
{
    uint8_t *force = root_slot(ROOT_FORCES, fid);
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int h = CALL(ObjFn, FN_HEX)(fh), units = 0, fhmax = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0;
        if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && dist(h, *(uint16_t *)(u + 0x2A)) <= THREAT_UNIT_R)
            units += *(uint16_t *)(u + 0x16);
    }
    uint8_t *root = game_root();
    /* 빌드 50: 점령한 지 TAKEN_WIDE_DAYS일 안이면 반경을 넓힌다(빌드 49 판 B 한중: 34칸 장안이 빠져 관문 공격) */
    int wide = fh_taken_days(fh) <= TAKEN_WIDE_DAYS, rad = wide ? THREAT_FH_R_TAKEN : THREAT_FH_R;
    for (int id = 1; id <= 0x3D; id++) {
        uint8_t *o = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, id);
        int g = valid(o) && o != fh && o != skip ? fh_owner(o) : 0;
        if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && dist(h, CALL(ObjFn, FN_HEX)(o)) <= rad &&
            *(int *)(o + 0x3C) > fhmax)
            fhmax = *(int *)(o + 0x3C);
    }
    int need = fhmax * THREAT_FH_PCT / 100;
    need = units > need ? units : need;
    t->units = units, t->fhmax = fhmax, t->d = dist(h, CALL(ObjFn, FN_HEX)(target)), t->wide = wide;
    t->too_far = need > 0 && t->d > FAR_ATTACK;
    t->res = t->too_far ? *(int *)(fh + 0x3C) : need > res0 ? need : res0;
}

/* 빌드 62: 지원 예상 몫의 출발 거점 예비 = max(THREAT_UNIT_R칸 안 적 부대 병사, THREAT_FH_R칸(막 점령이면 넓게) 안 적 거점 병력
   최대 × THREAT_FH_PCT%). 먼 목표 판단(too_far)은 쓰지 않는다 */
static int support_keep(uint8_t *fh, int fid, int *fhmax, int *units)
{
    Threat t;
    threat_of(fh, fid, fh, 0, NULL, &t);
    int k = t.fhmax * THREAT_FH_PCT / 100;
    *fhmax = t.fhmax, *units = t.units;
    return t.units > k ? t.units : k;
}

static void atk_threat_reserve(void *list)
{
    uint8_t *items[MAX_CAND], *node = g_atk_node;
    int n = list_items(list, items, MAX_CAND);
    if (!valid(node) || n < 1 || !valid(items[0]))
        return;
    uint8_t *fh = foothold_of(items[0]), *target = *(uint8_t **)(node + 0x60);
    int fid = force_id(items[0]);
    if (!valid(fh) || !valid(target) || fid < 1 || fid > 0x33 || !valid(root_slot(ROOT_FORCES, fid)))
        return;
    if (V102_STAGE < 3) {
        /* 1.0.2 재구성 2단계: 출진 판단·점령 정비·포화 출진·먼 목표 규칙 없음. 예비 = max(원래, THREAT_UNIT_R칸 안 적 부대 병사 합).
           적 거점 병력 몫은 넣지 않는다(마주 선 성이 차면 영구히 막혀 빌드 33 동결·빌드 49 B 손권 반격 막힘의 원인) */
        int gar = *(int *)(fh + 0x3C), units = near_foe_soldiers(fh, fid, THREAT_UNIT_R);
        /* 빌드 70(2026-11-07 사용자 결정): 노드 첫 거점에서 한 번, 가망 없는 공격이면 모든 참여 거점이 병사를 내지 않는다 */
        if (g_atk_go < 0)
            g_atk_go = atk_futile(node);
        if (g_atk_go == 1) {
            *(int *)(node + 0x88) = gar;
            return;
        }
        int res = units > g_atk_res0 ? units : g_atk_res0;
        *(int *)(node + 0x88) = res;
        return;
    }
    if (g_atk_go < 0) {
        *(int *)(node + 0x88) = g_atk_res0; /* 판단은 원래 예비 기준 */
        g_atk_go = atk_hopeless(node);
    }
    Threat t;
    threat_of(fh, fid, target, g_atk_res0, NULL, &t);
    int gar = *(int *)(fh + 0x3C);
    if (g_v3 && fh_taken_days(fh) < TAKEN_REST_DAYS) { /* 빌드 50 점령 정비: 이 거점은 병사를 내지 않음(atk_hopeless도 뺌) */
        *(int *)(node + 0x88) = gar;
        return;
    }
    if (g_atk_go == 1) { /* 하지 않기로 함: 이 노드의 모든 거점이 병사를 내지 않음 */
        *(int *)(node + 0x88) = gar;
        return;
    }
    if (g_atk_sat) { /* 포화 출진: 포화 거점만 g_sat_res(atk_hopeless)를 남기고 나간다 */
        int keep = gar;
        for (int i = 0; i < g_sat_n; i++)
            if (g_sat_fh[i] == fh)
                keep = g_sat_res[i];
        *(int *)(node + 0x88) = keep;
        return;
    }
    *(int *)(node + 0x88) = t.res;
}

/* 공격 출진 판단: 참여 거점마다 (병력 − 위협 예비) × 그 거점 대장의 병사당 전력을 더한 값이
   (게임 필요 전력 + 지원 예상 × 출발 거점 평균 병사당 전력)의 ATK_GO_PCT% 미만이면 1(하지 않음) */
/* 거점 fh(세력 fid)의 무장이 병사 left를 통솔 순으로 지휘 상한까지 나눠 가져갈 때의 부대 전력 합. 몫이 MIN_SHARE
   미만이면 건너뛴다(공격 B단계 3000 미만 건너뜀). 2026-11-02: 예전 (병력 × 거점 대장의 5000명당 전력)은 부대 전력이
   병사 수에 오목해서(√항이 (4L/3)²에서 끊김) 큰 부대일수록 부풀었다(빌드 34 판 동탁 9만 7천 → 실제 1만 4천) */
static int by_lead(const void *a, const void *b)
{
    return ability(*(uint8_t *const *)b, 0) - ability(*(uint8_t *const *)a, 0);
}

static float fh_power(uint8_t *fh, int fid, int left, uint8_t *target)
{
    uint8_t *c[MAX_CAND];
    int id = *(uint16_t *)(fh + 0x4E), n = 0;
    if (left < MIN_SHARE)
        return 0;
    for (int i = 0; i <= PERSON_MAX && n < MAX_CAND; i++) {
        uint8_t *p = root_slot(ROOT_PERSONS, i);
        if (is_person(p) && *(uint16_t *)(p + 0xE4) == id && p[0xE6] >= 1 && p[0xE6] <= 4 && force_id(p) == fid)
            c[n++] = p;
    }
    qsort(c, n, sizeof c[0], by_lead);
    float sum = 0;
    for (int k = 0; k < n && left >= MIN_SHARE; k++) {
        int s = CALL(ObjFn, FN_CAP)(c[k]);
        s = s < left ? s : left;
        if (s < MIN_SHARE)
            continue;
        sum += unit_power(c[k], s) * naval_plan(fh, target, c[k]);
        left -= s;
    }
    return sum;
}

static int sat_ready(uint8_t *fh)
{
    int id = *(uint16_t *)(fh + 0x4E);
    return id < 1 || id > 0x3D || !g_sat_last[id] || day_key() - g_sat_last[id] >= SAT_COOLDOWN;
}

/* 빌드 50: fh에서 SAT_LAND_R칸 안에 강을 건너지 않고 닿는 적 거점(도시·관문)이 있으면 그 번호, 없으면 0 */
static int land_enemy_near(uint8_t *fh)
{
    int fid = fh_owner(fh), h = CALL(ObjFn, FN_HEX)(fh);
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL, *root = game_root();
    if (!valid(force))
        return 0;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    for (int id = 1; id <= 0x3D; id++) {
        uint8_t *o = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, id);
        int g = valid(o) && o != fh ? fh_owner(o) : 0, oh = g ? CALL(ObjFn, FN_HEX)(o) : -1;
        if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && dist(h, oh) <= SAT_LAND_R && !crosses_river(h, oh))
            return id;
    }
    return 0;
}

/* 포화 거점: 병력이 상한(vfunc +0xC8)의 SAT_PCT% 이상, 강을 건너지 않거나 건너도 수군이 밀리지 않음(배율 1), 쉬는 중 아님.
   빌드 50(2026-11-05 사용자 결정): 강을 건너는 목표는 육로로 칠 수 있는 적 거점이 없을 때만(빌드 49 판 B 강릉 → 강하, 양양이 옆) */
static int sat_foothold(uint8_t *fh, uint8_t *target, uint8_t *boss)
{
    int gar = *(int *)(fh + 0x3C);
    if (!g_v3 || (float)gar * 100 < (float)fh_vint(fh, 0xC8) * SAT_PCT || !sat_ready(fh))
        return 0;
    if (!crosses_river(CALL(ObjFn, FN_HEX)(fh), CALL(ObjFn, FN_HEX)(target)))
        return 1;
    int land = land_enemy_near(fh);
    if (land) {
        return 0;
    }
    return is_person(boss) && naval_plan(fh, target, boss) >= 1;
}

static int atk_hopeless(uint8_t *node)
{
    uint8_t *target = *(uint8_t **)(node + 0x60), *fhs[MAX_CAND];
    int n = list_items(*(void **)(node + 0x78), fhs, MAX_CAND), need = *(int *)(node + 0x84), fid = 0;
    if (!valid(target) || n < 1 || need <= 0)
        return 0;
    float can = 0, sat_can = 0, ppw_sum = 0, wet = 1, cross_can = 0; /* wet: 참여 거점 중 가장 낮은 공격 수상 배율(강 건너 수군이 약하면 < 1) */
    int cnt = 0, sup = 0, own = fh_owner(target), pct = 100, cross = 0, ncross = 0, rested = 0; /* cross: 강 건넘 기준을 쓰는 공격 */
    char why[256];
    uint8_t *of = own >= 1 && own <= 0x33 ? root_slot(ROOT_FORCES, own) : NULL;
    if (valid(of) && CALL(ObjFn, FN_IS_PLAYER)(of)) { /* 원작도 궁극·상대 플레이어면 문턱을 낮추고 끝 취소를 하지 않는다 */
        int d = ((int(__fastcall *)(void *))RVA(FN_DIFFICULTY))(((void *(__fastcall *)(void))RVA(FN_SETTINGS))());
        pct = d >= 4 ? PLAYER_SUP_ULT : d == 3 ? PLAYER_SUP_HARD : 100;
    }
    g_sat_n = 0;
    for (int i = 0; i < n; i++) {
        uint8_t *fh = fhs[i], *boss = valid(fh) ? CALL(uint8_t * (__fastcall *)(void *), FN_FH_LEADER)(fh) : NULL;
        if (!is_person(boss))
            continue;
        fid = fid ? fid : force_id(boss);
        if (fid < 1 || fid > 0x33)
            return 0;
        int tid = *(uint16_t *)(target + 0x4E);
        if (g_v3 && tid >= 0x34 && tid <= 0x3D && g_gate_cd[fid][tid - 0x34] && day_key() - g_gate_cd[fid][tid - 0x34] < GATE_CD_DAYS) {
            /* 빌드 52: 함락 예상으로 취소한 관문은 GATE_CD_DAYS일 동안 다시 만들지 않는다(빌드 51 판 1 한중 → 관문 57 14번) */
            return 1;
        }
        int taken = fh_taken_days(fh);
        if (g_v3 && taken < TAKEN_REST_DAYS) { /* 빌드 50 점령 정비 */
            rested++;
            continue;
        }
        Threat t;
        threat_of(fh, fid, target, *(int *)(node + 0x88), NULL, &t);
        float ppw = unit_power(boss, REF_SOLDIERS) / REF_SOLDIERS;
        int avail = *(int *)(fh + 0x3C) - t.res;
        float pw = fh_power(fh, fid, avail, target); /* 무장별 수상 배율은 계획과 같다 */
        can += pw;
        ppw_sum += ppw, cnt++;
        float nv = naval_plan(fh, target, boss);
        wet = nv < wet ? nv : wet;
        if (g_v3 && crosses_river(CALL(ObjFn, FN_HEX)(fh), CALL(ObjFn, FN_HEX)(target)))
            cross_can += pw, ncross++;
        int gar = *(int *)(fh + 0x3C);
        if (g_sat_n < (int)_countof(g_sat_fh) && sat_foothold(fh, target, boss)) {
            /* 포화 출진 예비 = max(목표를 뺀 위협 예비, min(성 병력의 SAT_KEEP_PCT%, 보통 위협 예비)). 곧 적어도
               (100 − SAT_KEEP_PCT)%를 내고, 목표 말고 다른 적이 크면 그만큼은 남긴다(빌드 47. 빌드 46은 max(35%, 목표 뺀 예비)라
               보통 예비보다 많이 남겨 포화 전력이 보통 전력보다 작았다: 빌드 46 판 강릉 1만 9천 > 1만 5천) */
            Threat ts;
            threat_of(fh, fid, target, *(int *)(node + 0x88), target, &ts);
            int keep = gar * SAT_KEEP_PCT / 100;
            keep = keep < t.res ? keep : t.res;
            keep = keep > ts.res ? keep : ts.res;
            g_sat_fh[g_sat_n] = fh, g_sat_res[g_sat_n++] = keep;
            sat_can += fh_power(fh, fid, gar - keep, target);
        }
    }
    if (!cnt) {
        if (rested) { /* 참여 거점이 모두 점령 정비 중 */
            return 1;
        }
        return 0;
    }
    int field = 0;
    sup = atk_support(target, fid, why, sizeof why, &field);
    float spw = (float)pct / 100 * ppw_sum / (float)cnt; /* 지원 병사 → 전력 */
    float want = (float)need + (float)sup * spw;
    /* 2026-11-03 도강: 강을 건너는 공격은 판단 ATK_GO_CROSS_PCT%, 끝 재확인 ATK_END_CROSS_PCT%(전력 격차가 충분할 때만).
       빌드 46: 강을 건너는 참여 거점의 낼 전력이 CROSS_SHARE_PCT% 이상일 때만(빌드 45 판 양양 → 강릉이 강하·신야 때문에 90%) */
    cross = ncross && (can > 0 ? cross_can * 100 >= can * CROSS_SHARE_PCT : ncross * 100 >= cnt * CROSS_SHARE_PCT);
    int go_pct = cross ? ATK_GO_CROSS_PCT : ATK_GO_PCT;
    int go = can * 100 >= want * go_pct;
    /* 지원 상한: 지원 예상 전력이 원작 필요의 SUPPORT_CAP_PCT%를 넘으면 기준에서만 자른다(수비가 얇은데 주변만 두꺼운
       목표, 막 빼앗긴 도시). 원래 기준으로 통과하는 공격은 계획 병력·끝 재확인 모두 그대로다 */
    /* 상한은 성 병력 지원에만. 목표 주변 적 야전(field)은 늘 다 센다 */
    float garr = (float)(sup - field) * spw, gcap = (float)need * SUPPORT_CAP_PCT / 100;
    float want_cap = garr > gcap ? (float)need + gcap + (float)field * spw : want;
    /* 강을 건너는데 수군이 약하면(빌드 23 강릉 → 강하: 물 위에서 주변 거점 요격에 녹음) 상한을 걸지 않는다 */
    int no_cap = want_cap < want && wet < 1;
    if (no_cap)
        want_cap = want;
    int go_cap = !go && g_v3 && can * 100 >= want_cap * go_pct;
    /* 포화 출진: 지원 예상에 막혔어도 포화 거점만으로 원작 필요의 SAT_GO_PCT%를 내면 공격(빌드 47. 빌드 46은 (필요 + 지원
       예상)의 60%라 원작 필요가 트리 대상 방어의 약 3배인 목표에서 한 번도 통과하지 못함) */
    g_atk_sat = !go && !go_cap && g_v3 && g_sat_n && sat_can * 100 >= (float)need * SAT_GO_PCT;
    g_atk_want = g_atk_sat ? (float)need : go_cap ? want_cap : want;
    g_atk_capped = go_cap;
    g_atk_cross = cross;
    return !(go || go_cap || g_atk_sat);
}

/* 빌드 70(2026-11-07 사용자 결정): 2단계 가망 없는 공격 막기. 1.0.1 출진 판단(ATK_GO 70%·강 건넘 90%, 지원 상한·포화 출진·
   점령 정비·관문 쉼)을 뺀 1.0.2 재구성의 예외로, 성공 가망이 아주 낮은 공격만 막는다. 참여 거점마다 (병력 − 위협 예비(2단계와
   같은 max(원래, 15칸 적 부대))) 를 그 거점 무장이 통솔 순으로 나눠 낼 전력(무장별 수상 배율 포함)을 더하고, (필요 + 지원 예상
   × 거점 대장 평균 병사당 전력)의 ATK_FUTILE_PCT%(강을 건너는 참여 거점의 낼 전력이 CROSS_SHARE_PCT% 이상이면
   ATK_FUTILE_CROSS_PCT%) 미만이면 1. 근거: 빌드 64~69 판 비율 0.4 미만 노드는 거의 모두 실패·끝 취소(빌드 69 유비 강하 1명
   공격 0.21~0.39), 0.4~0.5에는 함락이 있었다(조조 여강 0.42~0.49). 빌드 64 유비 강릉 → 시상 도하 0.58은 강릉 2만 1천을 비웠다 */
static int atk_futile(uint8_t *node)
{
    uint8_t *target = *(uint8_t **)(node + 0x60), *fhs[MAX_CAND];
    int n = list_items(*(void **)(node + 0x78), fhs, MAX_CAND), need = *(int *)(node + 0x84), fid = 0;
    if (!g_v3 || !valid(target) || n < 1 || need <= 0)
        return 0;
    float can = 0, cross_can = 0, ppw_sum = 0;
    int cnt = 0, ncross = 0, own = fh_owner(target), pct = 100;
    uint8_t *of = own >= 1 && own <= 0x33 ? root_slot(ROOT_FORCES, own) : NULL;
    if (valid(of) && CALL(ObjFn, FN_IS_PLAYER)(of)) {
        int d = ((int(__fastcall *)(void *))RVA(FN_DIFFICULTY))(((void *(__fastcall *)(void))RVA(FN_SETTINGS))());
        pct = d >= 4 ? PLAYER_SUP_ULT : d == 3 ? PLAYER_SUP_HARD : 100;
    }
    int th = CALL(ObjFn, FN_HEX)(target);
    for (int i = 0; i < n; i++) {
        uint8_t *fh = fhs[i], *boss = valid(fh) ? CALL(uint8_t * (__fastcall *)(void *), FN_FH_LEADER)(fh) : NULL;
        if (!is_person(boss))
            continue;
        fid = fid ? fid : force_id(boss);
        if (fid < 1 || fid > 0x33)
            return 0;
        int gar = *(int *)(fh + 0x3C), units = near_foe_soldiers(fh, fid, THREAT_UNIT_R);
        int res = units > g_atk_res0 ? units : g_atk_res0;
        float pw = fh_power(fh, fid, gar - res, target);
        can += pw;
        ppw_sum += unit_power(boss, REF_SOLDIERS) / REF_SOLDIERS, cnt++;
        if (crosses_river(CALL(ObjFn, FN_HEX)(fh), th))
            cross_can += pw, ncross++;
    }
    if (!cnt)
        return 0;
    int sup = atk_support(target, fid, NULL, 0, NULL);
    float want = (float)need + (float)sup * (float)pct / 100 * ppw_sum / (float)cnt;
    int cross = ncross && (can > 0 ? cross_can * 100 >= can * CROSS_SHARE_PCT : ncross * 100 >= cnt * CROSS_SHARE_PCT);
    int go_pct = cross ? ATK_FUTILE_CROSS_PCT : ATK_FUTILE_PCT;
    if (can * 100 >= want * go_pct)
        return 0;
    return 1;
}

/* CArmySetRetreatNode(0x61120): 부대 변수(+0x38)의 부대에 대기 또는 귀환(0x2EA1B0)을 건다. 관문으로 들여보낸 부대면
   건너뛰고(성공), 명령이 바뀌어 있으면 관문 들어가기를 다시 건다 */
static int __fastcall retreat_hook(uint8_t *node, void *ctx, void *a3)
{
    __try {
        uint8_t *u = g_v3 ? bb_obj(ctx, *(uint32_t *)(node + 0x38)) : NULL;
        int gid = valid(u) ? tracked_gate(*(uint16_t *)(u + 0x12)) : 0;
        if (gid && *(uint8_t *)(u + 0x140) == 0) {
            if (u[0x37] != 3 || u[0x38] != 6 || *(uint16_t *)(u + 0x3A) != gid - 0x33) {
                ((void(__fastcall *)(void *, int))RVA(FN_SET_ACTION))(u, 3);
                ((void(__fastcall *)(void *, int, int))RVA(FN_SET_TARGET))(u, 6, gid - 0x33);
                *(uint16_t *)(u + 0x48) = 0xBD10;
            }
            return 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return CALL(NodeFn, FN_RETREAT)(node, ctx, a3);
}

/* ---------- G. 그룹 10 행군 대열 ---------- */

/* 원작은 공격 출진 거점마다 부대를 한 묶음(부대 +0x58)으로 묶지만 이동량은 부대마다 따로라, 기동이 낮은 공성 병기가
   뒤처지고 앞 부대가 먼저 들어가 보급이 끊긴다(행군 묶음). 같은 묶음·같은 공격 목표(행동 4 도시, 5 관문)로
   가는 컴퓨터 부대 중 가장 뒤처진 부대보다 목표에 MARCH_GAP칸 이상 가까운 부대는 이번 처리에서 움직이지 않는다.
   목표 MARCH_FREE칸 안, 적 부대 MARCH_ENEMY칸 안, 한 번에 MARCH_HOLD_DAYS일 넘게 기다린 경우는 원래대로 간다 */
static int g_wait_since[PERSON_MAX]; /* 대장 무장 id → 기다리기 시작한 날 키(0 = 안 기다림). 부대 처리는 주 루프 하나 */
static uint8_t g_wait_gaveup[PERSON_MAX];
static uint8_t g_wait_target[PERSON_MAX]; /* 기다림을 포기한 목표 거점 id. 포기는 목표가 바뀔 때까지 간다(빌드 47) */
static int g_esc_rear[PERSON_MAX], g_esc_day[PERSON_MAX]; /* 빌드 68 공성 병기 호위: 가장 뒤 공성 병기의 목표 거리 최소와 그날 */
/* 빌드 50: 묶음·목표별 첫 기다림 날. 그 뒤 MARCH_GROUP_DAYS일이 지나면 그 묶음은 그 목표로 더 기다리지 않는다
   (빌드 49 판 B 강릉 → 강하: 넷이 번갈아 기다려 40일에 9칸). 열린 주소 해시, 꽉 차면 기록 없이 원래대로 */
#define MARCH_GROUP_SLOTS 256
/* 빌드 62: 묶음 번호는 다시 쓰여서(빌드 61 판: 손권 묶음 25가 313일 전 조조 묶음 25·같은 목표 기록을 이어받아 바로 포기)
   열쇠에 세력을 넣고, MARCH_GROUP_GAP일 넘게 기다림이 없던 기록은 새로 시작한다 */
#define MARCH_GROUP_GAP 30
static struct {
    uint16_t grp;
    uint8_t target, force;
    int first, last;
} g_grp_wait[MARCH_GROUP_SLOTS];
static int grp_wait_first(int grp, int fhid, int fid, int today)
{
    for (int i = 0, k = (grp * 31 + fhid * 7 + fid) & (MARCH_GROUP_SLOTS - 1); i < MARCH_GROUP_SLOTS;
         i++, k = (k + 1) & (MARCH_GROUP_SLOTS - 1)) {
        int stale = today - g_grp_wait[k].last > MARCH_GROUP_GAP || today < g_grp_wait[k].last;
        if (g_grp_wait[k].grp == grp && g_grp_wait[k].target == fhid && g_grp_wait[k].force == fid && !stale) {
            g_grp_wait[k].last = today;
            return g_grp_wait[k].first;
        }
        if (!g_grp_wait[k].grp || stale) {
            g_grp_wait[k].grp = (uint16_t)grp, g_grp_wait[k].target = (uint8_t)fhid, g_grp_wait[k].force = (uint8_t)fid;
            g_grp_wait[k].first = g_grp_wait[k].last = today;
            return today;
        }
    }
    return today;
}

static int day_key(void)
{
    uint8_t *world = *(uint8_t **)(game_root() + ROOT_WORLD);
    return *(int16_t *)(world + WORLD_YMD) * 372 + (*(int8_t *)(world + WORLD_YMD + 2) - 1) * 31 +
           *(int8_t *)(world + WORLD_YMD + 3);
}

static int march_wait(uint8_t *u)
{
    int grp = *(uint16_t *)(u + 0x58), act = u[0x37], kind = u[0x38], id = *(uint16_t *)(u + 0x3A),
        pid = *(uint16_t *)(u + 0x12);
    if (!grp || (act != 4 && act != 5) || *(uint8_t *)(u + 0x140) || pid >= PERSON_MAX)
        return 0;
    int fhid = kind == 5 ? id : kind == 6 ? id + 0x33 : 0, fid = CALL(ObjFn, FN_UNIT_FORCE)(u);
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL, *root = game_root();
    uint8_t *target = fhid ? ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, fhid) : NULL;
    if (!valid(force) || CALL(ObjFn, FN_IS_PLAYER)(force) || !valid(target))
        return 0;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int h = *(uint16_t *)(u + 0x2A), th = CALL(ObjFn, FN_HEX)(target), mine = dist(h, th);
    int rear = mine, rear_pid = pid, n = 0, rear_hex = h;
    uint8_t *rear_u = NULL;
    if (mine > MARCH_FREE) {
        for (int i = 1; i <= UNIT_MAX; i++) {
            uint8_t *o = root_slot(ROOT_UNITS, i);
            if (o == u || !valid(o))
                continue;
            int oh = *(uint16_t *)(o + 0x2A);
            if (*(uint16_t *)(o + 0x58) == grp && o[0x37] == act && o[0x38] == kind && *(uint16_t *)(o + 0x3A) == id) {
                int d = dist(oh, th);
                n++;
                if (d > rear)
                    rear = d, rear_pid = *(uint16_t *)(o + 0x12), rear_u = o, rear_hex = oh;
                continue;
            }
            int g = CALL(ObjFn, FN_UNIT_FORCE)(o);
            if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && dist(h, oh) <= MARCH_ENEMY) {
                rear = mine, rear_u = NULL; /* 적이 붙었으면 원래대로 */
                break;
            }
        }
    }
    /* 빌드 46 판: 같은 묶음 두 부대가 번갈아 "가장 뒤"가 되어 간격이 좁혀질 때마다 포기가 지워지고 서로를 다시 기다렸다
       (조조 663·830, 건업 13~19칸에서 넉 달). 포기는 같은 목표인 동안 유지한다 */
    if (g_wait_target[pid] != fhid)
        g_wait_target[pid] = (uint8_t)fhid, g_wait_since[pid] = 0, g_wait_gaveup[pid] = 0;
    /* 빌드 50(2026-11-05 사용자 결정): 가장 뒤 부대가 나보다 느릴 때(공성 병기·느린 진형)만 기다린다. 같은 기동이면
       지형·길 때문에 뒤처진 것이라 기다려도 간격이 줄지 않는다(빌드 49 판 B 한중: 일반 부대끼리 한 달) */
    if (rear - mine >= MARCH_GAP && valid(rear_u)) {
        int(__fastcall *ustat)(void *, int, int) = (int(__fastcall *)(void *, int, int))RVA(FN_UNIT_FORM_STAT);
        if (ustat(rear_u, 0, 1) >= ustat(u, 0, 1))
            rear = mine;
    }
    if (rear - mine < MARCH_GAP) {
        g_wait_since[pid] = 0;
        return 0;
    }
    int today = day_key();
    if (g_wait_gaveup[pid])
        return 0;
    /* 빌드 68(2026-11-07 사용자 결정): 가장 뒤가 공성 병기(정란·충차·투석, 부대 +0x1B 육상 진형. 빌드 68·69는 +0x1C(수상 진형)를
       읽어 한 번도 걸리지 않았다, 빌드 70에서 고침: 0x2AF8B0 모드 1 = +0x1B, 모드 2 = +0x1C)이고 나는 아니면, 공성 병기가 목표에
       다가오는 동안은 묶음 날수·한 번 날수 한도 없이 기다린다(흑태자 판 194.12.21 강릉: 묶음 7이 21일째 목표 12칸 앞에서 풀려
       탱커가 먼저 붙고 충차 2021이 혼자 잘림). SIEGE_STALL_DAYS일 동안 가까워지지 않으면 포기. 목표 MARCH_FREE칸 안·적
       MARCH_ENEMY칸 안은 위와 같이 원래대로 */
    if (valid(rear_u) && is_siege(rear_u[0x1B]) && !is_siege(u[0x1B])) {
        if (!g_wait_since[pid] || rear < g_esc_rear[pid])
            g_esc_rear[pid] = rear, g_esc_day[pid] = today;
        if (today - g_esc_day[pid] > SIEGE_STALL_DAYS) {
            g_wait_gaveup[pid] = 1;
            return 0;
        }
        if (!g_wait_since[pid]) {
            g_wait_since[pid] = today;
        }
        return 1;
    }
    int first = grp_wait_first(grp, fhid, fid, today);
    if (today - first > MARCH_GROUP_DAYS) {
        g_wait_gaveup[pid] = 1;
        return 0;
    }
    if (!g_wait_since[pid]) {
        g_wait_since[pid] = today;
        return 1;
    }
    if (today - g_wait_since[pid] > MARCH_HOLD_DAYS) {
        g_wait_gaveup[pid] = 1;
        return 0;
    }
    return 1;
}

/* 빌드 48 상륙 재계산(2026-11-03 사용자 결정): 수상 우위로 나간 요격 무장(build_plan → nvt_add)은 부대 처리마다 본다.
   상대 선두가 물 칸을 벗어나면 그 부대 LAND_NEAR_R칸 안 아군 전력과 LAND_FOE_R칸 안 적 전력을 다시 재고, 아군이
   LAND_RETREAT_PCT% 미만이면 원작 퇴각 노드(0x61120)와 같은 방식으로 귀환(0x2EA1B0(부대, 1, 0, 0)), 0x2C9310이 참이면
   대기(행동 13, 대상 칸 +0x48). 한 번 재면 추적을 끝낸다 */
#define NAVAL_TRACK_MAX 64
#define LAND_NEAR_R 5
#define LAND_FOE_R 10
static struct {
    int pid, enemy, fid, day, th; /* th = 지킬 목표 칸: 상륙 = 선두가 땅 위이고 그 칸까지 강을 건널 일이 없음(빌드 49) */
} g_nvt[NAVAL_TRACK_MAX];
static volatile LONG g_nvt_n;
static SRWLOCK g_nvt_lock = SRWLOCK_INIT;

static void nvt_add(int pid, int enemy_pid, int fid, int th)
{
    int day = day_key(), slot = -1;
    AcquireSRWLockExclusive(&g_nvt_lock);
    for (int i = 0; i < NAVAL_TRACK_MAX; i++) {
        if (g_nvt[i].pid == pid) {
            slot = i;
            break;
        }
        if (slot < 0 && (!g_nvt[i].pid || day - g_nvt[i].day > NAVAL_TRACK_DAYS))
            slot = i;
    }
    if (slot >= 0) {
        if (!g_nvt[slot].pid)
            InterlockedIncrement(&g_nvt_n);
        g_nvt[slot].pid = pid, g_nvt[slot].enemy = enemy_pid, g_nvt[slot].fid = fid, g_nvt[slot].day = day, g_nvt[slot].th = th;
    }
    ReleaseSRWLockExclusive(&g_nvt_lock);
}

static void nvt_drop(int i)
{
    if (g_nvt[i].pid) {
        g_nvt[i].pid = 0;
        InterlockedDecrement(&g_nvt_n);
    }
}

/* 잠금 안에서는 칸 찾기·지우기만 한다(부대 표를 훑다 예외가 나도 잠금이 남지 않게) */
static int nvt_take(int pid, int drop, int *enemy, int *fid, int *day, int *th)
{
    int found = 0;
    AcquireSRWLockExclusive(&g_nvt_lock);
    for (int i = 0; i < NAVAL_TRACK_MAX && !found; i++)
        if (g_nvt[i].pid == pid) {
            found = 1;
            if (enemy)
                *enemy = g_nvt[i].enemy, *fid = g_nvt[i].fid, *day = g_nvt[i].day, *th = g_nvt[i].th;
            if (drop)
                nvt_drop(i);
        }
    ReleaseSRWLockExclusive(&g_nvt_lock);
    return found;
}

static void naval_landing(uint8_t *u)
{
    int pid = *(uint16_t *)(u + 0x12), epid = 0, fid = 0, day = 0, th = -1;
    if (!g_nvt_n || !pid || !nvt_take(pid, 0, &epid, &fid, &day, &th))
        return;
    if (*(uint8_t *)(u + 0x140) || u[0x37] != 7 || day_key() - day > NAVAL_TRACK_DAYS) {
        nvt_take(pid, 1, NULL, NULL, NULL, NULL);
        return;
    }
    uint8_t *lead = NULL;
    for (int i = 1; i <= UNIT_MAX && !lead; i++) {
        uint8_t *o = root_slot(ROOT_UNITS, i);
        if (valid(o) && *(uint16_t *)(o + 0x12) == epid)
            lead = o;
    }
    int lh = lead ? *(uint16_t *)(lead + 0x2A) : -1;
    if (lead && (hex_water(lh) || (th >= 0 && crosses_river(lh, th)))) /* 아직 물 위이거나 건너편 기슭 */
        return;
    nvt_take(pid, 1, NULL, NULL, NULL, NULL);
    if (!lead)
        return;
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (!valid(force))
        return;
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int h = *(uint16_t *)(u + 0x2A);
    float own = 0, foe = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *o = root_slot(ROOT_UNITS, i);
        int g = valid(o) ? CALL(ObjFn, FN_UNIT_FORCE)(o) : 0, d = g ? dist(h, *(uint16_t *)(o + 0x2A)) : 99;
        if (g == fid && d <= LAND_NEAR_R)
            own += est_power(o);
        else if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && d <= LAND_FOE_R)
            foe += est_power(o);
    }
    int back = own * 100 < foe * LAND_RETREAT_PCT;
    if (back) {
        if (((int(__fastcall *)(void *))RVA(FN_RETREAT_STAY))(u)) {
            ((void(__fastcall *)(void *, int))RVA(FN_SET_ACTION))(u, 13);
            ((void(__fastcall *)(void *, int, int))RVA(FN_SET_TARGET))(u, 9, *(uint16_t *)(u + 0x48));
        } else
            ((uint64_t(__fastcall *)(void *, uint64_t, uint64_t, uint64_t))RVA(FN_RETURN))(u, 1, 0, 0);
    }
}

static int __fastcall march_hook(uint8_t *u)
{
    int mv = CALL(ObjFn, FN_MOVE)(u);
    __try {
        if (g_v3 && V102_STAGE >= 2) /* 상륙 재계산은 수상 우위 요격과 함께 2단계부터 */
            naval_landing(u);
        if (mv > 0 && march_wait(u))
            return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return mv;
}

/* ---------- H. 그룹 10 지역 점령 출진 ---------- */

/* 지역 점령 출진(0x295B0)은 공격 노드를 거치지 않아 위협 예비·출진 판단이 걸리지 않았다(2026-11-02 판: 강릉 4만에서
   3만이 양양·강하 쪽 지역으로 나가 녹고 빈 강릉이 공격받음). 모드 함수가 거점마다 계획(0x3E5A0)한 직후 노드 +0x54(예비)를
   읽으므로, 그 호출을 감싸 거점마다 예비를 바꾼다.
   - 위협 예비: 공격과 같은 threat_of(목표 = 거점 자신이라 먼 목표 규칙은 없음).
   (빌드 30의 "적 도시 지원 예상 70%" 출진 판단은 뺐다(2026-11-02). 공성 기준이라 적과 맞닿은 지역 점령이 첫 순부터
    거의 다 막혀 모든 세력의 영토 확장이 멈췄고, 성 병력이 가장 많은 동탁이 가장 이득을 봤다. 목적지 위험은 따로 판단해야 한다)
   (0x43F80은 거점 금(vfunc +0x90) < 500이고 병력 > 1000이면 예비와 상관없이 1000을 낸다. 원작 그대로 둔다) */
typedef uint64_t(__fastcall *AreaPlanFn)(uint8_t *node, uint8_t *fh, void *a3, void *a4, void *a5);
static __declspec(thread) uint8_t *g_area_node;
static __declspec(thread) int g_area_res0 = -1; /* 이번 실행의 원래 예비(-1 = 아직 안 읽음) */

static void area_reserve(uint8_t *node, uint8_t *fh)
{
    int fid = fh_owner(fh);
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (!valid(force) || CALL(ObjFn, FN_IS_PLAYER)(force))
        return;
    if (g_area_res0 < 0)
        g_area_res0 = *(int *)(node + 0x54);
    Threat t;
    threat_of(fh, fid, fh, g_area_res0, NULL, &t);
    *(int *)(node + 0x54) = t.res;
}

static uint64_t __fastcall area_plan_hook(uint8_t *node, uint8_t *fh, void *a3, void *a4, void *a5)
{
    uint64_t r = CALL(AreaPlanFn, FN_AREA_PLAN)(node, fh, a3, a4, a5);
    __try {
        if (node == g_area_node && valid(fh))
            area_reserve(node, fh);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

static int __fastcall area_node_hook(uint8_t *node, void *ctx, void *a3)
{
    uint8_t *prev = g_area_node;
    int prev_res = g_area_res0;
    g_area_node = node, g_area_res0 = -1;
    int r = CALL(NodeFn, FN_AREA)(node, ctx, a3);
    if (g_area_res0 >= 0)
        *(int *)(node + 0x54) = g_area_res0;
    g_area_node = prev, g_area_res0 = prev_res;
    return r;
}

/* 빌드 46 D: 강을 건너는 지역 확보 출진(CCommandAreaTryGetDepartureNode 0x2A5A0). 원작은 출발 거점에 1만만 남기고 무장마다
   min(병력 − 1만, 지휘 상한)을 내보낸다. 목표 칸이 강 건너(crosses_river)면
   - 출발 거점에 위협 예비(threat_of, 원래 AREA_KEEP)를 남기도록 병사를 줄이고, 1000 미만이면 그 무장부터 내지 않는다.
   - 그 출진의 첫 부대에서 (병력 − 위협 예비) × 수상 배율(두 세력 수상 진형, NAVAL_ATK_MIN~1)이 목표 칸에서 가장 가까운
     적 거점 병력의 AREA_CROSS_PCT% + 목표 칸 주변 적 야전(FIELD_NEAR_R·FIELD_FAR_R 가중치)보다 작으면 출진하지 않는다.
   만든 부대가 없으면 노드는 남은 무장을 내지 않고 끝난다(0x2AC2A·0x2BBF6 → 정리 경로). 육로 지역 확보는 그대로다
   (빌드 45 판: 건업 5만 8천 중 4만 8천이 광릉 지역으로 건너가 1만 1천만 남은 사이 조조 7만 3천이 건업 지역으로 건너옴) */
#define AREA_KEEP 10000     /* 원작 지역 확보가 출발 거점에 남기는 병력 */
#define AREA_CROSS_PCT 70   /* 목표 쪽 적 거점 병력의 이 %(빌드 51, 2026-11-05 사용자 결정: 50 → 70. 빌드 48b~50 유비 강릉 → 양양 지역
                               다섯 번과 손권 건업 → 광릉 지역이 모두 그 거점 요격 3~5명에 녹음. 70%면 여섯 번 모두 안 나감) */
#define AREA_OWNER_R 30     /* 목표 칸에서 이 칸 안 가장 가까운 적 거점을 목표 쪽으로 본다 */
static __declspec(thread) uint8_t *g_tg_fh;
static __declspec(thread) int g_tg_day, g_tg_ok;

/* 칸 h 주변 적(세력 fid와 그 동맹·연합 아님) 야전 병사: FIELD_NEAR_R칸 안 100%, FIELD_FAR_R칸 안 FIELD_FAR_PCT% */
static int enemy_field(uint8_t *force, int fid, int h)
{
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    int field = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        int g = valid(u) ? CALL(ObjFn, FN_UNIT_FORCE)(u) : 0;
        if (g < 1 || g > 45 || g == fid || ally_months(force, g))
            continue;
        int du = dist(h, *(uint16_t *)(u + 0x2A)), s = *(uint16_t *)(u + 0x16);
        field += du <= FIELD_NEAR_R ? s : du <= FIELD_FAR_R ? s * FIELD_FAR_PCT / 100 : 0;
    }
    return field;
}

/* 빌드 48 지역 확보 세 경우(2026-11-03 사용자 결정: 막기보다 충분한 전력·부대 수로). 그 출진(거점·날)의 첫 부대에서 정한다.
   낼 수 있는 병력 = 강을 건너면 병력 − 위협 예비(병사도 그만큼 자름), 육로면 병력 − 원작 1만.
   기준 = 목표 칸에서 가장 가까운 적 거점 병력 × AREA_CROSS_PCT% + 목표 칸 주변 적 야전(강을 건너면 병력에 수상 배율).
   - 충분: 원작대로(점령 부대 0x2AC18 + 옆 칸 시설 건설 부대 0x2BBE4). 다만 건설 부대를 만들 때 이미 나간 점령 부대 병사가
     기준에 못 미치고 출발 거점이 포화(SAT_PCT%)면 건설 부대를 내지 않는다.
   - 부족 + 육로 + 출발 거점 포화: 점령 부대만 낸다. 건설 부대는 원작이 만든 뒤 행동 8·대상을 따로 써서(0x2BC90·0x2BCA8)
     합류시킬 수 없으므로 내지 않고 그 병력은 성에 남는다. 그래서 갈라지지 않는다.
   - 그 밖 부족: 이번에는 내지 않는다(성이 찰 때까지). 강을 건너는 출진은 포화여도 부족하면 내지 않는다 */
#define AREA_GO 1
#define AREA_MERGE 2
static __declspec(thread) int g_tg_occ, g_tg_req, g_tg_sat, g_tg_cross; /* 이번 출진: 만든 점령 부대 병사 합, 기준, 포화, 강 건넘 */
static __declspec(thread) float g_tg_r;
static __declspec(thread) int g_tg_conv_pid = -1, g_tg_occ_hex = -1; /* 노드 끝에서 점령 부대로 바꿀 건설 무장, 점령 칸 */
static __declspec(thread) uint8_t *g_tg_occ_unit;                  /* 이번 노드에서 만든 점령 부대(묶음 번호를 읽음) */

/* 1이면 사양대로(병사를 줄였을 수 있음) 만든다, 0이면 만들지 않는다. builder = 건설 부대(0x2BBE4) */
static int tryget_gate(int *spec, int builder)
{
    uint8_t *p = spec[0] >= 0 && spec[0] < PERSON_MAX ? root_slot(ROOT_PERSONS, spec[0]) : NULL;
    int fid = is_person(p) ? force_id(p) : 0, th = spec[15];
    uint8_t *force = fid >= 1 && fid <= 0x33 ? root_slot(ROOT_FORCES, fid) : NULL, *fh = valid(force) ? foothold_of(p) : NULL;
    if (!valid(fh) || spec[14] != 9 || th < 0 || th >= HEX_COUNT || CALL(ObjFn, FN_IS_PLAYER)(force))
        return 1;
    int h = CALL(ObjFn, FN_HEX)(fh), day = day_key(), gar = *(int *)(fh + 0x3C);
    int cross = g_tg_fh == fh && g_tg_day == day ? g_tg_cross : crosses_river(h, th), keep = AREA_KEEP;
    if (cross) {
        Threat t;
        threat_of(fh, fid, fh, AREA_KEEP, NULL, &t);
        keep = t.res;
    }
    int left = gar - keep;
    if (g_tg_fh != fh || g_tg_day != day) {
        int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
        uint8_t *root = game_root(), *best = NULL;
        int bd = AREA_OWNER_R + 1;
        for (int id = 1; id <= 0x3D; id++) {
            uint8_t *o = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, id);
            int g = valid(o) ? fh_owner(o) : 0, d;
            if (g >= 1 && g <= 45 && g != fid && !ally_months(force, g) && (d = dist(th, CALL(ObjFn, FN_HEX)(o))) < bd)
                best = o, bd = d;
        }
        float r = 1, ua, ud, da, dd;
        if (cross && best && force_naval(force, &ua, &ud) && force_naval(foothold_force(best), &da, &dd))
            r = naval_ratio(ua, ud, da, dd, NAVAL_ATK_MIN, 1);
        int field = enemy_field(force, fid, th), ogar = best ? *(int *)(best + 0x3C) : 0;
        int req = ogar * AREA_CROSS_PCT / 100 + field, sat = (float)gar * 100 >= (float)fh_vint(fh, 0xC8) * SAT_PCT;
        g_tg_fh = fh, g_tg_day = day, g_tg_occ = 0, g_tg_req = req, g_tg_r = r, g_tg_sat = sat, g_tg_cross = cross;
        g_tg_ok = left >= 1000 && (float)left * r >= (float)req ? AREA_GO : !cross && sat && left >= 1000 ? AREA_MERGE : 0;
    }
    if (!g_tg_ok)
        return 0;
    /* 모아야 하면 건설 부대도 원작대로 만들게 두고, 노드가 끝난 뒤(tryget_node_hook) 점령 묶음에 합쳐 점령 칸으로 보낸다
       (원작이 만든 직후 행동 8·대상·묶음 목적을 직접 쓰므로 그 뒤에 바꾼다). 점령 부대가 없으면 건설 부대를 내지 않는다 */
    if (builder && (g_tg_ok == AREA_MERGE || (!g_tg_cross && g_tg_sat && (float)g_tg_occ * g_tg_r < (float)g_tg_req))) {
        int conv = valid(g_tg_occ_unit) && g_tg_occ_hex >= 0;
        if (!conv)
            return 0;
        g_tg_conv_pid = spec[0];
    }
    if (g_tg_cross && spec[2] > left) {
        spec[2] = left;
    }
    return spec[2] >= 1000;
}

/* 빌드 48b(2026-11-04 사용자 결정): 지역 확보 노드가 끝난 뒤, 모으기로 한 건설 부대를 점령 묶음에 넣고 행동 1·점령 칸으로
   바꾼다. 경로 A가 둘째 부대를 첫 묶음에 합치는 순서(0x42A50 → 0x282540 해체 → 0x241C90 넣기 → 0x2EAAE0(1) → 0x2EAB40(9, 칸))와 같다.
   부대 시설 칸(+0x46)은 그대로 둔다(행동 1에서 쓰이는지는 미확인) */
static void tryget_convert(void)
{
    uint8_t *bu = NULL, *ou = g_tg_occ_unit;
    for (int i = 1; i <= UNIT_MAX && !bu; i++) {
        uint8_t *o = root_slot(ROOT_UNITS, i);
        if (valid(o) && *(uint16_t *)(o + 0x12) == g_tg_conv_pid)
            bu = o;
    }
    int gid = valid(ou) ? *(uint16_t *)(ou + 0x58) : 0;
    const char *why = !valid(bu) ? "건설 부대를 못 찾음" : !gid ? "점령 묶음 없음" : bu[0x37] != 8 ? "건설 부대 행동이 8이 아님" : NULL;
    int old = valid(bu) ? *(uint16_t *)(bu + 0x58) : 0;
    if (!why && old != gid) {
        uint8_t *grp = ((uint8_t * (__fastcall *)(void *))RVA(FN_UNIT_TROOPS))(bu);
        if (valid(grp))
            ((void(__fastcall *)(void *))RVA(FN_TROOPS_DISSOLVE))(grp);
        ((void(__fastcall *)(void *, int))RVA(FN_SET_TROOPS))(bu, gid);
    }
    if (!why) {
        ((void(__fastcall *)(void *, int))RVA(FN_SET_ACTION))(bu, 1);
        ((void(__fastcall *)(void *, int, int))RVA(FN_SET_TARGET))(bu, 9, g_tg_occ_hex);
    }
}

static int __fastcall tryget_node_hook(uint8_t *node, void *ctx, void *a3)
{
    int prev_pid = g_tg_conv_pid, prev_hex = g_tg_occ_hex;
    uint8_t *prev_unit = g_tg_occ_unit;
    g_tg_conv_pid = -1, g_tg_occ_hex = -1, g_tg_occ_unit = NULL;
    int r = CALL(NodeFn, FN_TRYGET)(node, ctx, a3);
    __try {
        if (g_v3 && g_tg_conv_pid >= 0)
            tryget_convert();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[지역] 지역 확보 건설 부대 바꾸기 예외(무장 %d)", g_tg_conv_pid);
    }
    g_tg_conv_pid = prev_pid, g_tg_occ_hex = prev_hex, g_tg_occ_unit = prev_unit;
    return r;
}

/* 지역 점령(0x27775)·지역 확보(0x2AC18 점령 부대·0x2BBE4 건설 부대) 출진 부대. 사양 [0] 무장 [2] 병사 [3] 육상 진형 [13] 행동
   [14] 대상 종류 [15] 대상. 지역 확보는 tryget_gate로 거른다(빌드 46 강 건넘, 빌드 48 육로 포함 세 경우) */
#define CALL_SITE() ((uint32_t)((uint8_t *)_ReturnAddress() - RVA(0)) - 5)

static uint64_t __fastcall area_create_hook(int *spec, int flag)
{
    uint32_t site = CALL_SITE();
    int tryget = site != SITE_AREA_CREATE, builder = site == SITE_TRYGET_CREATE_B, go = 1;
    if (g_v3 && tryget && spec) {
        __try {
            go = tryget_gate(spec, builder);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            go = 1;
        }
    }
    if (!go)
        return 0;
    uint64_t r = CALL(UnitCreateFn, FN_UNIT_CREATE)(spec, flag);
    if (g_v3 && tryget && !builder && r && spec) {
        g_tg_occ += spec[2];
        g_tg_occ_unit = (uint8_t *)r, g_tg_occ_hex = spec[15];
    }
    return r;
}

/* 공격 판단 [2043] "참여 거점 전력 합 > 문턱(대상 방어 × 210%/170% + …)": 포화 거점 참여·역습 창이면 문턱을 낮춘다 */
static char __fastcall cond_gt(void *ctx, void *node, const int *args, int argc)
{
    char r = CALL(CondFn, FN_COND_GT)(ctx, node, args, argc);
    if (!g_v3 || argc != 2 || (uint32_t)args[0] != BT_ATK_SUM || (uint32_t)args[1] != BT_ATK_BAR)
        return r;
    __try {
        uint8_t *target = bb_obj(ctx, BT_ATK_TARGET), *force = bb_obj(ctx, BT_MY_FORCE), *fhs[MAX_CAND];
        int n = list_items(bb_obj(ctx, BT_ATK_FHS), fhs, MAX_CAND), sum = 0, def = 0;
        int fid = valid(force) ? (*(int(__fastcall **)(void *))(*(uint8_t **)force + 0x60))(force) : 0;
        bb_int(ctx, BT_ATK_SUM, &sum);
        bb_int(ctx, BT_ATK_DEF, &def);
        /* 빌드 46 포화 출진: 참여 거점에 포화 거점(sat_foothold)이 있으면 문턱을 대상 방어 × SAT_TREE_PCT%로 낮춘다.
           상대가 플레이어면 원작 그대로(원작이 궁극에서 170%로 따로 낮춘다) */
        uint8_t *tf = valid(target) ? foothold_force(target) : NULL;
        int relief = 0;
        if (!r && def > 0 && (float)sum * 100 >= (float)def * SAT_TREE_PCT && fid >= 1 && fid <= 0x33 && !(tf && CALL(ObjFn, FN_IS_PLAYER)(tf)))
            for (int i = 0; i < n && !relief; i++)
                relief = valid(fhs[i]) && fh_owner(fhs[i]) == fid &&
                         sat_foothold(fhs[i], target, CALL(uint8_t * (__fastcall *)(void *), FN_FH_LEADER)(fhs[i]));
        if (relief) {
            r = 1;
        }
        /* 빌드 51 역습 창(2026-11-05 사용자 결정): 목표 병력이 최근 60일 최고의 COUNTER_DRAIN_PCT% 미만으로 빠졌으면(218년부터)
           문턱을 대상 방어 × COUNTER_TREE_PCT%로. 빌드 49 B 219.12.1: 조조 포화 공격을 막은 뒤 양양 25,744(참여 15,882 ≤ 31,517) */
        int tmax = valid(target) ? fh_recent_max(target) : 0;
        if (!r && def > 0 && valid(target) && day_key() >= 218 * 372 && (float)sum * 100 >= (float)def * COUNTER_TREE_PCT &&
            (float)*(int *)(target + 0x3C) * 100 < (float)tmax * COUNTER_DRAIN_PCT && fid >= 1 && fid <= 0x33 &&
            !(tf && CALL(ObjFn, FN_IS_PLAYER)(tf))) {
            r = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

/* ---------- I. 그룹 10 야전 묶음 재판단 (빌드 53, 2026-11-05 사용자 결정) ----------
   트리 2(묶음마다) 첫 노드 GetMyselfTroops(0x6D920, T2[3]) 뒤 묶음마다 하루 한 번. AI 세력(1~45, 플레이어 아님)만.
   구성원 중 잠금(+0x13C)·플레이어 조작(+0x140)이 있거나, 대장 부대가 병참 끊김(+0x26 비트 1 꺼짐)·사기 FIELD_MORALE 이하면 손대지 않는다.
   1) 전과 확대: 요격 묶음(목적 7 재집결, 또는 목적 1이고 대장이 쫓는 중(행동 7)이 아님)의 대장 FOLLOW_CLEAR_R칸 안 적 병사가 우리의
      1/3 미만이고 집이 위협받지 않으면, FOLLOW_R칸 안 강을 건너지 않는 가장 가까운 적 도시를 친다. 둘레(FIELD_GROUP_R칸) 우리 야전
      전력 ≥ (그 도시 출격 가능 전력(0xBAE30 모드 3, 원작 T2[208]과 같음) × FOLLOW_NEED_MULT + 지원 예상 병사 × 우리 병사당 전력)
      × FOLLOW_PCT%일 때. 쓰기: 목적 8·대상, 작업 목록 비움(0xA2450, 0x613A0이 적 지역에 들어서면 채움), 구성원 0x2EE010(u, 0, 1).
   2) 조우 교전: 공격 묶음(목적 2·8~0xB·0x16)의 대장 ENGAGE_R칸 안에 우리 거점을 노리는 적(경계 묶음 root+0x80BA8)이 있고
      둘레 우리 야전 전력 ≥ 그 적 전력 × ENGAGE_RATIO면 목적 1·+0x16 = 그 경계 묶음 번호(ExecuteIntercept가 구성원을 배정).
      원래 목적·대상을 g_rs에 두었다가, 요격이 끝나(목적 7) 또는 취소돼(0x11/0x12) 원래 대상이 아직 적이고 집이 위협받지 않으면
      RESUME_DAYS일 안에 원래 공격으로 되돌린다.
   3) 회군: 공격 묶음의 집(대장 +0xE2)을 노리는 경계 묶음 전력 합이 집 출격 가능 전력(0xBAE30 모드 8 플래그 1, 원작 [10274]과 같음)보다
      크고 대장이 목표에서 RECALL_FAR칸보다 멀면 목적 0x11/0x12(집)으로. 쓰기 순서는 0x6E190의 귀환 갈래와 같다 */
enum {
    VT_MYTROOPS = 0x134EBA0, FN_MYTROOPS = 0x6D920, SLOT_MYTROOPS = VT_MYTROOPS + 16 * 8, /* CGetMyselfTroopsNode */
    FN_TROOPS_OF = 0xB72F0,  /* (ctx, a3) → 묶음 객체 */
    FN_TROOPS_LEADER = 0x22FC40, /* (묶음) → 대장 부대 */
    FN_RELAY_CLEAR = 0xA33B0,    /* (묶음) +0x18 작업 목록 비움 */
    FN_UNIT_FLAG = 0x2F03D0,     /* (부대, k, v) k 0 → +0x41, 2 → +0x26 비트 3 */
    FN_UNIT_REPATH = 0x26E260, FN_UNIT_SPECIAL_BITS = 0xA2CF0,
    FN_FH_SORTIE = 0xBBEC0, /* (거점, 0, 모드, 플래그) → 출격 가능 전력 */
    FN_CAUTION_FORCE = 0x22E720,
    ROOT_CAUTION = 0x80BA8, CAUTION_MAX = 500,
};
#define FIELD_MORALE 30
#define FOLLOW_R 15
#define FOLLOW_CLEAR_R 8
#define FIELD_GROUP_R 6
#define FOLLOW_NEED_MULT 3
#define FOLLOW_PCT 70
#define ENGAGE_R 6
#define ENGAGE_RATIO 1.2f
#define RECALL_FAR 12
#define RESUME_DAYS 60
#define ENGAGE_CD 10 /* 원래 공격으로 되돌린 뒤 이 날 동안은 다시 조우 교전하지 않음 */
#define TROOPS_MAX 500

static int g_rj_day[TROOPS_MAX + 1];
static struct {
    int purpose, target, day, cd;
} g_rs[TROOPS_MAX + 1];

static int hostile_to(uint8_t *force, int fid, int g)
{
    return g >= 1 && g <= 45 && g != fid && !ally_months(force, g);
}

/* 칸 h에서 r칸 안 세력 fid(mine = 1) 또는 그 적(mine = 0) 일반 부대(+0x10 == 1)의 전력 합, 병사 합 */
static float area_power(int h, int fid, uint8_t *force, int r, int mine, int *sol)
{
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    float s = 0;
    *sol = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (!valid(u) || u[0x10] != 1)
            continue;
        int g = CALL(ObjFn, FN_UNIT_FORCE)(u);
        if (mine ? g != fid : !hostile_to(force, fid, g))
            continue;
        if (dist(h, *(uint16_t *)(u + 0x2A)) > r)
            continue;
        s += CALL(UnitPowerFn, FN_UNIT_POWER)(u);
        *sol += *(uint16_t *)(u + 0x16);
    }
    return s;
}

/* 묶음 tid의 구성원. 잠금·플레이어 조작 부대가 있으면 -1 */
static int troops_members(int tid, uint8_t **out, int max)
{
    int n = 0;
    for (int i = 1; i <= UNIT_MAX; i++) {
        uint8_t *u = root_slot(ROOT_UNITS, i);
        if (!valid(u) || *(uint16_t *)(u + 0x58) != tid)
            continue;
        if (*(uint32_t *)(u + 0x13C) || u[0x140])
            return -1;
        if (n < max)
            out[n++] = u;
    }
    return n;
}

static void troops_attack(uint8_t *tr, uint8_t **mem, int n, int purpose, int target)
{
    *(uint16_t *)(tr + 0x14) = (uint16_t)purpose, *(uint16_t *)(tr + 0x16) = (uint16_t)target;
    CALL(void(__fastcall *)(void *), FN_RELAY_CLEAR)(tr);
    for (int i = 0; i < n; i++)
        CALL(void(__fastcall *)(void *, int, int), FN_UNIT_FLAG)(mem[i], 0, 1);
}

static void troops_return(uint8_t *tr, uint8_t **mem, int n, int home)
{
    *(uint16_t *)(tr + 0x14) = (uint16_t)(home <= 0x33 ? 0x11 : 0x12), *(uint16_t *)(tr + 0x16) = (uint16_t)home;
    for (int i = 0; i < n; i++) {
        CALL(void(__fastcall *)(void *), FN_UNIT_REPATH)(mem[i]);
        CALL(void(__fastcall *)(void *, int, int), FN_UNIT_FLAG)(mem[i], 0, 1);
        CALL(void(__fastcall *)(void *, int, int), FN_UNIT_FLAG)(mem[i], 2, 0);
        CALL(void(__fastcall *)(void *), FN_UNIT_SPECIAL_BITS)(mem[i]);
    }
}

static int caution_live(uint8_t *c)
{
    return valid(c) && (*(int(__fastcall **)(void *))(*(uint8_t **)c + 0x18))(c) != 0;
}

/* 경계 묶음 c의 적 부대 전력 합, 칸 h에서 가장 가까운 적 부대 거리(*nearest) */
static float caution_power(uint8_t *c, int h, int *nearest)
{
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    uint8_t *us[64];
    int n = list_items(c + 0x28, us, 64);
    float e = 0;
    *nearest = 1 << 20;
    for (int i = 0; i < n; i++) {
        if (!valid(us[i]) || us[i][0x10] != 1)
            continue;
        e += CALL(UnitPowerFn, FN_UNIT_POWER)(us[i]);
        int d = dist(h, *(uint16_t *)(us[i] + 0x2A));
        *nearest = d < *nearest ? d : *nearest;
    }
    return e;
}

/* 집 home을 노리는 경계 묶음 전력 합 > 집 출격 가능 전력이면 1 */
static int home_threatened(uint8_t *force, int fid, int home, float *e_out, int *s_out)
{
    uint8_t *root = game_root(), *hf = ((uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD))(root, home);
    *e_out = 0, *s_out = 0;
    if (!valid(hf))
        return 0;
    float e = 0;
    for (int i = 1; i <= CAUTION_MAX; i++) {
        uint8_t *c = root_slot(ROOT_CAUTION, i);
        if (!caution_live(c) || *(uint16_t *)(c + 0x1A) != home || !hostile_to(force, fid, CALL(ObjFn, FN_CAUTION_FORCE)(c)))
            continue;
        int nd;
        e += caution_power(c, CALL(ObjFn, FN_HEX)(hf), &nd);
    }
    int s = ((int(__fastcall *)(void *, int, int, int))RVA(FN_FH_SORTIE))(hf, 0, 8, 1);
    *e_out = e, *s_out = s;
    return e > (float)s;
}

static void field_rejudge(uint8_t *tr)
{
    uint8_t *lead = CALL(uint8_t * (__fastcall *)(void *), FN_TROOPS_LEADER)(tr);
    if (!valid(lead) || lead[0x10] != 1)
        return;
    int tid = *(uint16_t *)(lead + 0x58), fid = CALL(ObjFn, FN_UNIT_FORCE)(lead), today = day_key();
    uint8_t *force = fid >= 1 && fid <= 45 ? root_slot(ROOT_FORCES, fid) : NULL;
    if (tid < 1 || tid > TROOPS_MAX || !valid(force) || CALL(ObjFn, FN_IS_PLAYER)(force) || g_rj_day[tid] == today)
        return;
    g_rj_day[tid] = today;
    int p = *(uint16_t *)(tr + 0x14), ptgt = *(uint16_t *)(tr + 0x16);
    int is_atk = p == 2 || (p >= 8 && p <= 0xB) || p == 0x16;
    if (g_rs[tid].day && (today - g_rs[tid].day > RESUME_DAYS || p == 0))
        g_rs[tid].day = 0;
    if (!is_atk && p != 1 && p != 7 && p != 0x11 && p != 0x12)
        return;
    uint8_t *mem[32];
    int n = troops_members(tid, mem, 32);
    if (n <= 0 || !(lead[0x26] & 2) || lead[0x1A] <= FIELD_MORALE)
        return;
    uint8_t *root = game_root();
    uint8_t *(__fastcall *foothold)(void *, int) = (uint8_t * (__fastcall *)(void *, int)) RVA(FN_FOOTHOLD);
    int(__fastcall *dist)(int, int) = (int(__fastcall *)(int, int))RVA(FN_HEX_DIST);
    uint8_t *lp = root_slot(ROOT_PERSONS, *(uint16_t *)(lead + 0x12));
    int h = *(uint16_t *)(lead + 0x2A), home = is_person(lp) ? *(uint16_t *)(lp + 0xE2) : 0;
    float e_home = 0;
    int s_home = 0, threat = home >= 1 && home <= 0x3D && home_threatened(force, fid, home, &e_home, &s_home);
    int my_sol;
    float mine = area_power(h, fid, force, FIELD_GROUP_R, 1, &my_sol);
    /* 원래 공격으로 되돌리기 */
    if (g_rs[tid].day && (p == 7 || p == 0x11 || p == 0x12)) {
        uint8_t *t = foothold(root, g_rs[tid].target);
        int ok = valid(t) && hostile_to(force, fid, fh_owner(t)) && !threat;
        if (ok) {
            troops_attack(tr, mem, n, g_rs[tid].purpose, g_rs[tid].target);
            g_rs[tid].cd = today;
        }
        g_rs[tid].day = 0;
        return;
    }
    if (p == 1 || p == 7) {
        /* 1) 전과 확대 */
        if (threat || (p == 1 && lead[0x37] == 7))
            return;
        if (p == 1) { /* 쫓던 경계 묶음에 아직 적 부대가 있으면 요격 중 */
            uint8_t *c = ptgt >= 1 && ptgt <= CAUTION_MAX ? root_slot(ROOT_CAUTION, ptgt) : NULL;
            int nd;
            if (caution_live(c) && caution_power(c, h, &nd) > 0)
                return;
        }
        int foe_sol;
        area_power(h, fid, force, FOLLOW_CLEAR_R, 0, &foe_sol);
        if (foe_sol * 3 >= my_sol)
            return;
        int best = 0, bd = 1 << 20;
        for (int id = 1; id <= 0x33; id++) {
            uint8_t *o = foothold(root, id);
            int oh = valid(o) ? CALL(ObjFn, FN_HEX)(o) : -1, d = oh >= 0 ? dist(h, oh) : 1 << 20;
            if (valid(o) && hostile_to(force, fid, fh_owner(o)) && d <= FOLLOW_R && d < bd && !crosses_river(h, oh))
                best = id, bd = d;
        }
        if (!best)
            return;
        uint8_t *t = foothold(root, best);
        int st = ((int(__fastcall *)(void *, int, int, int))RVA(FN_FH_SORTIE))(t, 0, 3, 0);
        int sup = atk_support(t, fid, NULL, 0, NULL);
        float ppw = my_sol > 0 ? mine / (float)my_sol : 0, want = ((float)st * FOLLOW_NEED_MULT + (float)sup * ppw) * FOLLOW_PCT / 100;
        int go = mine >= want && ppw > 0;
        if (go) {
            troops_attack(tr, mem, n, 8, best);
        }
        return;
    }
    if (!is_atk)
        return;
    /* 2) 조우 교전: 우리 거점을 노리는 경계 묶음이 대장 ENGAGE_R칸 안 */
    int bc = 0, bnear = 1 << 20;
    float be = 0;
    for (int i = 1; i <= CAUTION_MAX; i++) {
        uint8_t *c = root_slot(ROOT_CAUTION, i);
        if (!caution_live(c) || !hostile_to(force, fid, CALL(ObjFn, FN_CAUTION_FORCE)(c)))
            continue;
        uint8_t *aim = foothold(root, *(uint16_t *)(c + 0x1A));
        if (!valid(aim) || fh_owner(aim) != fid)
            continue;
        int nd;
        float e = caution_power(c, h, &nd);
        if (e > 0 && nd <= ENGAGE_R && nd < bnear)
            bc = i, bnear = nd, be = e;
    }
    if (bc && mine >= be * ENGAGE_RATIO && (!g_rs[tid].cd || today - g_rs[tid].cd >= ENGAGE_CD)) {
        if (!g_rs[tid].day)
            g_rs[tid].purpose = p, g_rs[tid].target = ptgt, g_rs[tid].day = today;
        *(uint16_t *)(tr + 0x14) = 1, *(uint16_t *)(tr + 0x16) = (uint16_t)bc;
        return;
    }
    /* 3) 회군 */
    uint8_t *t = foothold(root, ptgt);
    int dt = valid(t) ? dist(h, CALL(ObjFn, FN_HEX)(t)) : 0;
    if (threat && dt > RECALL_FAR) {
        troops_return(tr, mem, n, home);
    }
}

static int __fastcall mytroops_hook(uint8_t *node, void *ctx, void *a3)
{
    int r = CALL(NodeFn, FN_MYTROOPS)(node, ctx, a3);
    if (!g_v3)
        return r;
    __try {
        uint8_t *tr = ((uint8_t * (__fastcall *)(void *, void *)) RVA(FN_TROOPS_OF))(ctx, a3);
        if (valid(tr))
            field_rejudge(tr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int warned;
        if (!warned++)
            plog("[야전] 묶음 재판단 중 예외. 원작 그대로");
    }
    return r;
}

/* 그룹 10 전용 연결: 전력 추정 31곳, 임무 목록 8곳, 실행 노드 3개(공격 출진은 수상 보정용) */
static int v3_init(void)
{
    Patch p[_countof(UNIT_POWER_CALLS) + 2 + _countof(PLOT_LIST_CALLS)];
    int n = 0, want = (int)_countof(p);
    for (int i = 0; i < (int)_countof(UNIT_POWER_CALLS); i++)
        n += patch_call(&p[n], UNIT_POWER_CALLS[i], FN_UNIT_POWER, (void *)unit_power_hook);
    n += patch_call(&p[n], SITE_LIST_INSPECT, FN_LIST_INSPECT, (void *)inspect_list_hook);
    n += patch_call(&p[n], SITE_LIST_SEARCH, FN_LIST_SEARCH, (void *)search_list_hook);
    for (int i = 0; i < (int)_countof(PLOT_LIST_CALLS); i++)
        n += patch_call(&p[n], PLOT_LIST_CALLS[i], FN_LIST_PLOT, (void *)plot_list_hook);
    if (n != want || !patch_apply("AI 출진 전력·임무", p, n)) {
        plog("[AI 출진] 전력·임무 연결 실패(%d/%d)", n, want);
        return 0;
    }
    /* 1.0.2 재구성: 공격 노드 감싸기(출진 판단·끝 재확인·관문 함락 예상)는 1단계에서 걸지 않는다 */
    if (V102_STAGE >= 2 && !vtable_swap(SLOT_ATTACK, FN_ATTACK, (void *)attack_node_hook))
        plog("[AI 출진] 공격 출진 노드 연결 실패(공격 수상 보정 없음)");
    g_enter_ok = vtable_swap(SLOT_DEPART, FN_DEPART, (void *)depart_hook) &&
                 vtable_swap(SLOT_ADDARMY, FN_ADDARMY, (void *)addarmy_hook);
    if (!g_enter_ok)
        plog("[AI 출진] 출진·부대 목록 노드 연결 실패(관문 지원 들여보내기 없음)");
    if (g_enter_ok && !vtable_swap(SLOT_RETREAT, FN_RETREAT, (void *)retreat_hook))
        plog("[AI 출진] 퇴각 노드 연결 실패(들여보낸 부대가 귀환할 수 있음)");
    if (!vtable_swap(SLOT_DUAL, FN_DUAL, (void *)dual_hook))
        plog("[AI 출진] 이중파괴 노드 연결 실패");
    if (!vtable_swap(SLOT_SPYAWAY, FN_SPYAWAY, (void *)spyaway_hook))
        plog("[AI 출진] 첩보파견 노드 연결 실패");
    if (V102_STAGE < 2)
        return 1; /* 1단계: 아래(행군 대기·묶음·수송)는 걸지 않는다. 임무 노드(이중파괴·첩보파견)는 위로 옮김 */
    Patch mv;
    if (!patch_call(&mv, SITE_MOVE, FN_MOVE, (void *)march_hook) || !patch_apply("AI 출진 행군 대열", &mv, 1))
        plog("[AI 출진] 행군 대열 연결 실패");
    /* 빌드 65(2026-11-06 사용자 결정): 빌드 53 수송 감싸기를 2단계로 앞당김. 1.0.2 확인: 노드 0x32D90(vtable 0x1349878 슬롯 16),
       출발/도착 목록 노드 +0x38/+0x3C(함수 +0xA9), 부대 만들기 0x3546E → 0x201C20(앞 lea rcx,[rbp+0x190] / xor edx,edx),
       묶음 +0x16 쓰기 0x35550(호출 +0xE2), 사양 채우기 0x43C70(+0x28 행동, +0x2C 종류, +0x30 대상, +0x34, +0x38)이 1.0.1과 같은 배치 */
    Patch tp;
    if (!patch_call(&tp, SITE_TR_ISSUE, FN_TR_ISSUE, (void *)tr_issue_hook) || !vtable_swap(SLOT_TRANSPORT, FN_TRANSPORT, (void *)transport_node_hook))
        plog("[AI 출진] 수송 노드 연결 실패(원작 수송 그대로)");
    else if (!patch_apply("AI 출진 전선 보강 수송", &tp, 1)) {
        vtable_unswap(SLOT_TRANSPORT, (void *)transport_node_hook, FN_TRANSPORT);
        plog("[AI 출진] 수송 노드 연결 실패(적용)");
    }
    if (V102_STAGE < 3)
        return 1; /* 2단계: 묶음 재판단은 3단계(보류) */
    if (V102_REMOVED) { /* 1.0.2 재구성에서 뺀 묶음: 지역 점령 출진 규칙, 트리 공격 문턱(포화·역습 완화). 지점도 1.0.1 값 그대로다 */
    Patch ar[_countof(AREA_PLAN_CALLS) + 3];
    int an = 0;
    for (int i = 0; i < (int)_countof(AREA_PLAN_CALLS); i++)
        an += patch_call(&ar[an], AREA_PLAN_CALLS[i], FN_AREA_PLAN, (void *)area_plan_hook);
    an += patch_call(&ar[an], SITE_AREA_CREATE, FN_UNIT_CREATE, (void *)area_create_hook);
    an += patch_call(&ar[an], SITE_TRYGET_CREATE_A, FN_UNIT_CREATE, (void *)area_create_hook);
    an += patch_call(&ar[an], SITE_TRYGET_CREATE_B, FN_UNIT_CREATE, (void *)area_create_hook);
    if (an != (int)_countof(ar) || !vtable_swap(SLOT_AREA, FN_AREA, (void *)area_node_hook))
        plog("[AI 출진] 지역 점령 출진 연결 실패(%d/%d)", an, (int)_countof(ar));
    else if (!patch_apply("AI 출진 지역 점령 출진", ar, an)) {
        vtable_unswap(SLOT_AREA, (void *)area_node_hook, FN_AREA);
        plog("[AI 출진] 지역 점령 출진 연결 실패(적용)");
    } else if (!vtable_swap(SLOT_TRYGET, FN_TRYGET, (void *)tryget_node_hook))
        plog("[AI 출진] 지역 확보 노드 연결 실패(건설 부대를 점령 부대로 바꾸지 않음)");
    Patch gt;
    if (!patch_call(&gt, SITE_COND_GT, FN_COND_GT, (void *)cond_gt) || !patch_apply("AI 출진 공격 문턱", &gt, 1))
        plog("[AI 출진] 공격 문턱 연결 실패");
    }
    if (!vtable_swap(SLOT_MYTROOPS, FN_MYTROOPS, (void *)mytroops_hook))
        plog("[AI 출진] 묶음 노드 연결 실패(야전 묶음 재판단 없음)");
    return 1;
}

/* A단계 선봉 부대 생성: 사양(+0 무장, +8 병사, +0xC 진형)을 받아 둔다 */
static uint64_t __fastcall atk_van_hook(int *spec, int flag)
{
    if (spec && g_atk.stage == 1) {
        g_atk.van_id = spec[0];
        g_atk.van_soldiers = spec[2];
        g_atk.van_form = spec[3];
        /* 빌드 63(2026-11-06 사용자 결정): 선봉도 그룹 1번으로 계획(지휘 상한 비례). 노드 +0x84는 아직 선봉 전 값 */
        __try {
            uint8_t *p = g_v3 && spec[0] >= 0 && spec[0] < PERSON_MAX ? root_slot(ROOT_PERSONS, spec[0]) : NULL;
            uint8_t *fh = p ? foothold_of(p) : NULL, *node = g_atk_node;
            uint8_t *tg = valid(node) ? *(uint8_t **)(node + 0x60) : NULL;
            if (is_person(p) && valid(fh) && valid(tg) && spec[2] >= ATK_MIN_SHARE) {
                int avail = *(int *)(fh + 0x3C) - *(int *)(node + 0x88);
                atk_plan(node, tg, p, avail, spec);
                if (g_atk.stage != 2)
                    g_atk.stage = 0; /* 계획 안 씀: 이 거점은 원작대로(B단계에서 다시 세우지 않음) */
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_atk.stage = 0;
            g_atk.n = 0;
            plog("[공격] 선봉 계획 중 예외. 원래 동작으로");
        }
    }
    return atk_made_hook(spec, flag);
}

static int atk_init(void)
{
    static const uint32_t sorts[] = {SITE_ATK_SORT_16, SITE_ATK_SORT_9, SITE_ATK_SORT_8, SITE_ATK_SORT_A};
    static const uint32_t specs[] = {SITE_ATK_SPEC_16, SITE_ATK_SPEC_9, SITE_ATK_SPEC_8, SITE_ATK_SPEC_A};
    static const uint32_t tactics[] = {SITE_ATK_TACTICS_SPEC, SITE_ATK_TACTICS_16A, SITE_ATK_TACTICS_16B,
                                       SITE_ATK_TACTICS_9A,   SITE_ATK_TACTICS_9B,  SITE_ATK_TACTICS_8,
                                       SITE_ATK_TACTICS_B,    SITE_ATK_TACTICS_AA,  SITE_ATK_TACTICS_AB};
    static const uint32_t vans[] = {SITE_ATK_VAN_16, SITE_ATK_VAN_9, SITE_ATK_VAN_A};
    static const uint32_t mades[] = {SITE_ATK_MAKE_16, SITE_ATK_MAKE_9, SITE_ATK_MAKE_8, SITE_ATK_MAKE_B, SITE_ATK_MAKE_A};
    Patch p[_countof(sorts) + _countof(specs) + _countof(tactics) + _countof(vans) + _countof(mades)];
    int n = 0;
    for (int i = 0; i < (int)_countof(sorts); i++)
        if (patch_call(&p[n], sorts[i], FN_ATK_SORT, (void *)atk_sort_hook))
            n++;
    for (int i = 0; i < (int)_countof(specs); i++)
        if (patch_call(&p[n], specs[i], FN_ATK_SPEC, (void *)atk_spec_hook))
            n++;
    for (int i = 0; i < (int)_countof(tactics); i++)
        if (patch_call(&p[n], tactics[i], FN_ATK_TACTICS, (void *)atk_tactics_hook))
            n++;
    for (int i = 0; i < (int)_countof(vans); i++)
        if (patch_call(&p[n], vans[i], FN_UNIT_CREATE, (void *)atk_van_hook))
            n++;
    for (int i = 0; i < (int)_countof(mades); i++)
        if (patch_call(&p[n], mades[i], FN_UNIT_CREATE, (void *)atk_made_hook))
            n++;
    return n == (int)_countof(p) && patch_apply("공격 출진", p, n);
}

int feat_intercept_init(void)
{
    g_v3 = 1;
    feat_turn_init();
    static const struct {
        uint32_t slot, fn;
        void *hook;
    } swaps[] = {
        {SLOT_MULTI, FN_MULTI, (void *)multi_hook},
        {SLOT_DIVIDE, FN_DIVIDE, (void *)divide_hook},
        {SLOT_PLUSVAR, FN_PLUSVAR, (void *)plusvar_hook},
        {SLOT_NOINTERCEPT, FN_NOINTERCEPT, (void *)nointercept_hook},
        {SLOT_SORT, FN_SORT, (void *)sort_hook},
        {SLOT_SOLDIERS, FN_SOLDIERS, (void *)soldiers_hook},
        {SLOT_FORMEVAL, FN_FORMEVAL, (void *)formeval_hook},
        {SLOT_GETFORM, FN_GETFORM, (void *)getform_hook},
    };
    int i = 0;
    while (i < (int)_countof(swaps) && vtable_swap(swaps[i].slot, swaps[i].fn, swaps[i].hook))
        i++;
    Patch p[2];
    if (i == (int)_countof(swaps) && patch_call(&p[0], SITE_COND_DIST, FN_COND_DIST, (void *)cond_dist) &&
        patch_call(&p[1], SITE_COND_GTK, FN_COND_GTK, (void *)cond_gtk) && patch_apply("요격 조건", p, 2)) {
        if (V102_STAGE >= 2 && !atk_init())
            plog("[공격] 연결 실패. 요격만 적용");
        if (g_v3 && !v3_init())
            g_v3 = 0; /* 전력 보정·임무 없이 나머지(순위·선봉·최소 몫)만 */
        return 1;
    }
    plog("[요격] 연결 실패(노드 %d/%d)", i, (int)_countof(swaps));
    while (i-- > 0)
        vtable_unswap(swaps[i].slot, swaps[i].hook, swaps[i].fn);
    return 0;
}
