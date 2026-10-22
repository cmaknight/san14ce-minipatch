/* 그룹 3: 턴 종료 시 자동 탐색. 근거는 docs/03-autosearch.md.

게임의 "탐색 → 자동탐색 → 결정"과 같은 처리를 턴 종료 확정 직후에 한다.
- 조건: 자동탐색 화면(2단계)에서 마지막으로 고른 값. 게임이 월드 데이터에 저장한다(세이브에 포함).
- 후보: 게임의 자동탐색 후보 계산 0x713E60 (화면이 쓰는 것과 같은 함수, 같은 정렬).
- 실행: 명령서/비용만큼 상위 후보에 대해 "결정"(0x732290)과 같은 순서로 0x6DCE80 → 0x1F2B80.
턴 종료 지점은 0x44D041 `call 0x451100`(ecx = 0, 턴 진행 직전). 켜짐 여부는 feat_autosearch_enabled().
*/
#include "common.h"
#include <string.h>

enum {
    SITE_TURN_END = 0x44D041,
    FN_TURN_END = 0x451100,      /* void (int flag) */
    FN_WORLD_DISTRICT = 0x32E8D0,/* CDistrictData* (CWorldData*) — 플레이어 군단 */
    FN_PLAYER_CTRL = 0x233960,   /* int (district) — 플레이어 조작 군단 여부 */
    FN_DISTRICT_FORCE = 0x40190, /* CForceData* (district) */
    FN_FORCE_CITY_COUNT = 0x22A9D0, /* int (force) */
    FN_MAP_CTOR = 0x1676E0,      /* std::map<CCityData*, int> 생성자(this) */
    FN_MAP_DTOR = 0x167E20,      /* 소멸자(this) */
    FN_LIST_INIT = 0x21E2B0,     /* ptr_list 초기화(this) */
    FN_LIST_DTOR = 0x424D60,     /* ptr_list 소멸(this) */
    FN_CANDIDATES = 0x713E60,    /* (state*, ptr_list*, 실행무장, 귀환, 중시) */
    FN_SEARCH_AREA = 0x6DCE80,   /* CAreaData* (district, person, city) */
    FN_SEARCH_EXEC = 0x1F2B80,   /* (cmd*, isPlayer) */
    FN_ALLOC = 0x3F6E80,         /* 게임 operator new */
    FN_VALID = 0x32F500,
    VT_PTR_LIST = 0x13436B0,
    VT_USER_STRATEGY = 0x13D63A0,/* CUserStrategyState: 전략 맵 명령 대기 */
    STATE_COUNT = 0x1B620C0,
    STATE_ARRAY = 0x1B620D0,
    STATE_BUSY = 0x1B620E0,
    LIST_NODES = 0x27D10F0,      /* 노드 표 포인터 */
    LIST_NODE_COUNT = 0x27D1120,
    PERSON_CITY = 0x277E278,     /* 무장 id → 목표 도시 표(6001칸). 자동탐색 화면이 열릴 때 할당(0x725469), 후보 계산이 채움 */
    PERSON_CITY_SIZE = 0xBB88,
    FN_GAME_FREE = 0x3F6E90,
    SEARCH_COST = 0x1A4856C,     /* 탐색 명령서 비용 */
    WORLD_OFF = 0x871E8,
};

typedef void(__fastcall *TurnEndFn)(int);
typedef void *(__fastcall *PtrFn)(void *);
typedef int(__fastcall *IntFn)(void *);
typedef void(__fastcall *VoidFn)(void *);
typedef void(__fastcall *CandFn)(void *, void *, int, int, int);
typedef void *(__fastcall *AreaFn)(void *, void *, void *);
typedef void(__fastcall *ExecFn)(void *, int);
typedef void *(__fastcall *AllocFn)(size_t);

#define CALL(type, rva) ((type)RVA(rva))


static int on_strategy_map(void)
{
    uint32_t n = *(uint32_t *)RVA(STATE_COUNT);
    void **arr = *(void ***)RVA(STATE_ARRAY);
    if (n == 0 || n > 0x40 || !arr || *(uint64_t *)RVA(STATE_BUSY) != 0)
        return 0;
    return *(uint8_t **)arr[n - 1] == RVA(VT_USER_STRATEGY);
}

/* 화면이 쓰는 라디오 번호로 바꾼다(0x724B80과 같은 변환) */
static void saved_conditions(uint8_t *world, int *who, int *days, int *prefer)
{
    *who = *(int *)(world + 0x17B0);
    if (*who < 0 || *who > 3)
        *who = 0;
    switch (*(int *)(world + 0x17B8)) {
    case 0: *days = 2; break; /* 제한 없음 */
    case 2: *days = 1; break; /* 20일 */
    default: *days = 0; break; /* 10일 */
    }
    *prefer = *(int *)(world + 0x17B4) == 2 ? 1 : 0; /* 능력 우선 / 군사추천 우선 */
}

static void **list_first(void *list)
{
    uint32_t *h = *(uint32_t **)((uint8_t *)list + 8);
    void ***table = *(void ****)RVA(LIST_NODES);
    if (!h || !table || *h >= *(uint32_t *)RVA(LIST_NODE_COUNT))
        return NULL;
    return table[*h];
}

static void run_auto_search(void)
{
    uint8_t *root = game_root();
    uint8_t *world = root ? *(uint8_t **)(root + WORLD_OFF) : NULL;
    if (!world)
        return;
    uint8_t *district = CALL(PtrFn, FN_WORLD_DISTRICT)(world);
    if (!district || !CALL(IntFn, FN_VALID)(district) || !CALL(IntFn, FN_PLAYER_CTRL)(district))
        return;
    int cost = *(int *)RVA(SEARCH_COST);
    int sheets = district[0x14];
    if (cost <= 0 || sheets < cost)
        return;
    void *force = CALL(PtrFn, FN_DISTRICT_FORCE)(district);
    int ncity = force ? CALL(IntFn, FN_FORCE_CITY_COUNT)(force) : 0;
    if (ncity <= 0 || ncity > 0x40)
        return;

    /* 도시별 이동일수 캐시(빈 std::map n개). 후보 계산이 필요할 때 채운다. */
    uint8_t *maps = (uint8_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)ncity * 16);
    uint8_t *state = (uint8_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x500);
    if (!maps || !state) {
        if (maps) HeapFree(GetProcessHeap(), 0, maps);
        if (state) HeapFree(GetProcessHeap(), 0, state);
        return;
    }
    for (int i = 0; i < ncity; i++)
        CALL(PtrFn, FN_MAP_CTOR)(maps + i * 16);
    *(uint8_t **)(state + 0x470) = district;
    *(uint8_t **)(state + 0x478) = maps;

    uint8_t list[16];
    *(uint8_t **)list = RVA(VT_PTR_LIST);
    *(void **)(list + 8) = NULL;
    CALL(VoidFn, FN_LIST_INIT)(list);

    /* 화면 밖에서는 목표 도시 표가 없다. 화면과 같은 크기로 만들고 끝나면 해제한다. */
    void ***pcity_slot = (void ***)RVA(PERSON_CITY);
    int own_pcity = 0;
    if (!*pcity_slot) {
        void **t = (void **)CALL(AllocFn, FN_ALLOC)(PERSON_CITY_SIZE);
        if (!t)
            goto cleanup_list;
        memset(t, 0, PERSON_CITY_SIZE);
        *pcity_slot = t;
        own_pcity = 1;
    }

    int who, days, prefer;
    saved_conditions(world, &who, &days, &prefer);
    CALL(CandFn, FN_CANDIDATES)(state, list, who, days, prefer);

    int limit = sheets / cost, tried = 0;
    void **pcity = *pcity_slot;
    for (void **node = list_first(list); node && tried < limit; node = (void **)node[1]) {
        uint8_t *person = (uint8_t *)node[0];
        tried++;
        if (!person || !CALL(IntFn, FN_VALID)(person))
            continue;
        uint16_t gid = *(uint16_t *)(person + 0x10);
        void *cmd[2] = {NULL, person};
        cmd[0] = CALL(AreaFn, FN_SEARCH_AREA)(district, person, pcity ? pcity[gid] : NULL);
        CALL(ExecFn, FN_SEARCH_EXEC)(cmd, CALL(IntFn, FN_PLAYER_CTRL)(district));
        if (district[0x14] < cost)
            break;
    }

    /* 탐색 화면을 닫을 때(0x720730)와 같이 후보 표시를 지운다 */
    for (void **node = list_first(list); node; node = (void **)node[1]) {
        uint8_t *person = (uint8_t *)node[0];
        if (person) {
            *(uint16_t *)(person + 0x15E) &= 0xFFBF;
            *(int *)(person + 0x1CC) = 0;
        }
    }
    if (own_pcity) {
        CALL(VoidFn, FN_GAME_FREE)(*pcity_slot);
        *pcity_slot = NULL;
    }
cleanup_list:
    CALL(VoidFn, FN_LIST_DTOR)(list);
    for (int i = 0; i < ncity; i++)
        CALL(VoidFn, FN_MAP_DTOR)(maps + i * 16);
    HeapFree(GetProcessHeap(), 0, maps);
    HeapFree(GetProcessHeap(), 0, state);
}

static int report(EXCEPTION_POINTERS *e)
{
    uint8_t *a = (uint8_t *)e->ExceptionRecord->ExceptionAddress;
    plog("[자동 탐색] 예외 %08X @%p (게임 RVA %06llX). 이번 턴은 건너뜀", e->ExceptionRecord->ExceptionCode, a,
         (unsigned long long)(a - g_base));
    return EXCEPTION_EXECUTE_HANDLER;
}

static void __fastcall turn_end_hook(int flag)
{
    if (flag == 0 && feat_autosearch_enabled() && on_strategy_map()) {
        __try {
            run_auto_search();
        } __except (report(GetExceptionInformation())) {
        }
    }
    CALL(TurnEndFn, FN_TURN_END)(flag);
}

int feat_autosearch_init(void)
{
    static const uint8_t xor_ecx[] = {0x33, 0xC9};
    if (!bytes_equal(SITE_TURN_END - 2, xor_ecx, sizeof xor_ecx)) {
        plog("[자동 탐색] 턴 종료 지점 문맥 불일치. 적용하지 않음");
        return 0;
    }
    Patch p;
    if (!patch_call(&p, SITE_TURN_END, FN_TURN_END, (void *)turn_end_hook))
        return 0;
    if (!patch_apply("자동 탐색", &p, 1))
        return 0;
    feat_autosearch_ui_init();
    return 1;
}
