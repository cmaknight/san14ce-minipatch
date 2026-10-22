/* 그룹 9: AI·위임 군단 내정 균등 개발. 근거는 docs/09-develop.md.

중점(개발 항목) 규칙: 1000 미만 항목 중 Lv이 가장 낮은 것. Lv이 같으면 중시 항목, 그다음 상업 → 농업 → 병영.
중시 항목은 군단 +0x1A(3 상, 2 농, 4 병), 없으면 군단장 성향 word +0x170/+0x172/+0x174 중 0보다 큰 최대값.

순 끝: 명령서 재계산 0x257990 호출(0x426B14, 단계 55)을 감싸, 담당관이 있는 AI·위임 지역의 개발 항목(+0x72)을
   우리 규칙으로 직접 바꾼다. 담당관은 그대로이고 명령서는 쓰지 않는다. 담당관을 유지하고 개발 항목만 바꾸는 것은
   게임에서도 명령서가 들지 않는다(2026-10-22 사용자 확인). 실행 함수 0x1ED290도 +0x72를 그냥 쓰고, 담당관이
   바뀔 때만 0x2DFF10 정리를 한다.
   대상: 플레이어 직할 군단(0x32E8D0)이 아닌 군단의 지역 중 도시 세력이 소유하고(0x22CC00) 담당관이 있는 곳.
AI가 새로 임명할 때 정한 개발 항목도 다음 순 끝에 같은 규칙으로 맞춰진다.

담당관 교체(2026-10-22 C안): 재계산 직후(명령서가 새로 생긴 때) 군단마다 최대 1곳, 명령서 1개로 담당관을 바꾼다.
   후보: 같은 도시 소속(+0xE2), 신분 1~4, 행동 중(0x232240)·특수 무장(0x233110) 아님, 다른 지역을 맡지 않음,
   그 도시의 모병·훈련 담당(+0xCA/+0xCC) 아님(실행 함수가 그 역할을 정리해 버린다).
   능력: 개발 항목에 맞는 능력(상업 지력 2, 농업 정치 3, 병영 무력 1, 0x22FCE0 관직 보너스 포함). 지금 담당관은 겸임
   감소(겸임 2곳 64%, 3곳 이상 44%, docs/09-develop.md)를 반영한다. 맡은 지역 수는 지역 담당관(+0x36)을 직접 센다.
   무장 +0x1E8은 임명 직후 갱신되지 않아(같은 무장이 순마다 다른 지역에 새로 임명됨, 2026-10-22 dll.log) 쓰지 않는다. 후보가 SWAP_GAIN 이상 높을 때만, 군단에서 이득이 가장 큰
   지역 하나를 바꾼다. 실행은 CDomesticOfficerSetNode와 같은 일괄 처리(0x25040 → 0x1F1A40 → 0x1ED290 → 정리)이고,
   명령서가 [0x1A483DC]개 이상일 때만 한다.

지나간 시도(2026-10-22 게임 확인, dll.log 진단):
- 순 끝 남은 명령서로만 전환: AI·위임 군단은 매 순 명령서를 다 써서(재계산 전 0, 후 10~11) 한 번도 바뀌지 않았다.
- AI 노드 안에서 전환(C안): 중점 선택 노드 0x7A1DB(→ 0xB1AA0)는 담당관이 없는 지역에서만 불렸다(순마다 66~140회,
  담당 있음 0회). 이 노드 훅은 새 임명 때 항목만 미리 맞추는 정도라 순 끝 전환과 겹쳐 뺐다.
*/
#include "common.h"
#include <string.h>

enum {
    SITE_RECOUNT = 0x426B14, FN_RECOUNT = 0x257990,
    FN_VALID = 0x32F500, FN_DIRECT_CORPS = 0x32E8D0, FN_REGION_FORCE = 0x22CC00,
    FN_LV_COMMERCE = 0x22E2B0, FN_LV_FARM = 0x22C760, FN_LV_BARRACKS = 0x231170,
    ROOT_PERSONS = 0x158, ROOT_NO_PERSON = 0x73A50, ROOT_REGIONS = 0x6CAF0, ROOT_CITIES = 0xDD38,
    ROOT_WORLD = 0x871E8,
    REGION_MAX = 0x1F4, DEV_MAX = 1000,
    FN_ABILITY = 0x22FCE0, FN_BUSY = 0x232240, FN_SPECIAL = 0x233110,
    FN_BATCH_INIT = 0x25040, FN_BATCH_ADD = 0x1F1A40, FN_BATCH_RUN = 0x1ED290,
    FN_BATCH_FREE_PART = 0x9E00, FN_BATCH_FREE = 0x424D60, VT_BATCH_BASE = 0x1343680,
    TOKEN_COST = 0x1A483DC, PERSON_MAX = 0x1770, CORPS_MAX = 0x33, SWAP_GAIN = 10,
};

typedef int(__fastcall *ObjFn)(void *);
typedef void *(__fastcall *PtrFn)(void *);
#define CALL(type, rva) ((type)RVA(rva))

static int valid(void *o) { return o && CALL(ObjFn, FN_VALID)(o); }

static uint8_t *person(unsigned id)
{
    uint8_t *root = game_root();
    return *(uint8_t **)(root + (id <= 0x1770 ? ROOT_PERSONS + (size_t)id * 8 : ROOT_NO_PERSON));
}

static int has_officer(uint8_t *region) { return valid(person(*(uint16_t *)(region + 0x36))); }

/* 중시 항목 1~3(상·농·병), 없으면 0 */
static int emphasis(uint8_t *corps)
{
    switch (corps[0x1A]) {
    case 3: return 1;
    case 2: return 2;
    case 4: return 3;
    }
    uint8_t *leader = person(*(uint16_t *)(corps + 0x12));
    if (!valid(leader))
        return 0;
    int best = 0, bv = 0;
    for (int k = 0; k < 3; k++) {
        int v = *(int16_t *)(leader + 0x170 + k * 2);
        if (v > bv)
            bv = v, best = k + 1;
    }
    return best;
}

static void levels(uint8_t *region, int lv[4])
{
    lv[1] = CALL(ObjFn, FN_LV_COMMERCE)(region);
    lv[2] = CALL(ObjFn, FN_LV_FARM)(region);
    lv[3] = CALL(ObjFn, FN_LV_BARRACKS)(region);
}

/* 우리 규칙의 중점(1~3). 모두 1000이면 0 */
static int choose(uint8_t *corps, uint8_t *region, const int lv[4])
{
    int e = emphasis(corps), order[4] = {e, 1, 2, 3}, best = 0;
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        if (!k || *(uint16_t *)(region + 0x3C + (k - 1) * 2) >= DEV_MAX)
            continue;
        if (!best || lv[k] < lv[best])
            best = k;
    }
    return best;
}

/* ---------- 순 끝 개발 항목 전환 ---------- */

static void rebalance(void)
{
    uint8_t *root = game_root();
    void *world = root ? *(void **)(root + ROOT_WORLD) : NULL;
    if (!world)
        return;
    void *direct = CALL(PtrFn, FN_DIRECT_CORPS)(world);
    for (int i = 1; i <= REGION_MAX; i++) {
        uint8_t *region = *(uint8_t **)(root + ROOT_REGIONS + (size_t)i * 8);
        if (!valid(region) || region[0x35] > 0x33)
            continue;
        uint8_t *city = *(uint8_t **)(root + ROOT_CITIES + (size_t)region[0x35] * 8);
        if (!valid(city))
            continue;
        uint8_t *corps = (*(PtrFn **)city)[0x80 / 8](city);
        if (!corps || corps == direct || !valid(corps))
            continue;
        if (CALL(ObjFn, FN_REGION_FORCE)(region) != (*(ObjFn **)city)[0xB0 / 8](city) || !has_officer(region))
            continue;
        int lv[4];
        levels(region, lv);
        int target = choose(corps, region, lv), cur = region[0x72];
        if (!target || target == cur)
            continue;
        region[0x72] = (uint8_t)target;
    }
}

/* ---------- 재계산 직후 담당관 교체 ---------- */
static const int FOCUS_STAT[4] = {0, 2, 3, 1}; /* 상업 지력, 농업 정치, 병영 무력 */

static int ability(uint8_t *p, int focus)
{
    return ((int(__fastcall *)(void *, int, int))RVA(FN_ABILITY))(p, FOCUS_STAT[focus], 0xFFF);
}

static uint8_t g_posts[PERSON_MAX + 1]; /* 무장별 맡은 지역 수(지역 +0x36을 센 것) */

static void count_posts(uint8_t *root)
{
    memset(g_posts, 0, sizeof g_posts);
    for (int i = 1; i <= REGION_MAX; i++) {
        uint8_t *region = *(uint8_t **)(root + ROOT_REGIONS + (size_t)i * 8);
        unsigned id = valid(region) ? *(uint16_t *)(region + 0x36) : 0;
        if (id && id <= PERSON_MAX && g_posts[id] < 255)
            g_posts[id]++;
    }
}

/* 겸임 감소를 반영한 실효 능력(docs/09-develop.md: 겸임 0~1곳 100%, 2곳 64%, 3곳 이상 44%) */
static int effective(unsigned id, int focus)
{
    int n = id <= PERSON_MAX ? g_posts[id] : 0, a = ability(person(id), focus);
    return n <= 1 ? a : n == 2 ? a * 64 / 100 : a * 44 / 100;
}

static int free_candidate(uint8_t *p, uint8_t *city)
{
    unsigned st = p[0xE6], id = *(uint16_t *)(p + 0x10);
    if (st < 1 || st > 4 || id > PERSON_MAX || g_posts[id])
        return 0;
    if (id == *(uint16_t *)(city + 0xCA) || id == *(uint16_t *)(city + 0xCC))
        return 0;
    if (((int(__fastcall *)(void *, int))RVA(FN_SPECIAL))(p, 0) || CALL(ObjFn, FN_BUSY)(p))
        return 0;
    return 1;
}

static int assign(uint8_t *region, unsigned person_id, int focus)
{
    __declspec(align(16)) uint8_t batch[0x80];
    memset(batch, 0, sizeof batch);
    typedef void(__fastcall * B1)(void *);
    typedef void(__fastcall * B4)(void *, void *, int, int);
    CALL(B1, FN_BATCH_INIT)(batch);
    CALL(B4, FN_BATCH_ADD)(batch, region, (int)person_id, focus);
    CALL(B1, FN_BATCH_RUN)(batch);
    CALL(B1, FN_BATCH_FREE_PART)(batch + 0x20);
    CALL(B1, FN_BATCH_FREE_PART)(batch + 0x10);
    *(void **)batch = RVA(VT_BATCH_BASE);
    CALL(B1, FN_BATCH_FREE)(batch);
    return *(uint16_t *)(region + 0x36) == person_id;
}

typedef struct {
    uint8_t *region;
    unsigned person;
    int gain;
} Swap;


static void reassign(void)
{
    uint8_t *root = game_root();
    void *world = root ? *(void **)(root + ROOT_WORLD) : NULL;
    if (!world)
        return;
    void *direct = CALL(PtrFn, FN_DIRECT_CORPS)(world);
    int cost = *(int *)RVA(TOKEN_COST);
    count_posts(root);
    Swap best[CORPS_MAX + 1];
    uint8_t *best_corps[CORPS_MAX + 1];
    memset(best, 0, sizeof best);
    memset(best_corps, 0, sizeof best_corps);
    for (int i = 1; i <= REGION_MAX; i++) {
        uint8_t *region = *(uint8_t **)(root + ROOT_REGIONS + (size_t)i * 8);
        if (!valid(region) || region[0x35] > 0x33)
            continue;
        unsigned cid = region[0x35];
        uint8_t *city = *(uint8_t **)(root + ROOT_CITIES + (size_t)cid * 8);
        if (!valid(city))
            continue;
        uint8_t *corps = (*(PtrFn **)city)[0x80 / 8](city);
        if (!corps || corps == direct || !valid(corps) || corps[0x14] < cost)
            continue;
        if (CALL(ObjFn, FN_REGION_FORCE)(region) != (*(ObjFn **)city)[0xB0 / 8](city) || !has_officer(region))
            continue;
        int focus = region[0x72];
        if (focus < 1 || focus > 3 || *(uint16_t *)(region + 0x3C + (focus - 1) * 2) >= DEV_MAX)
            continue;
        int cur = effective(*(uint16_t *)(region + 0x36), focus);
        /* 같은 도시 소속의 빈 무장 중 가장 나은 사람 */
        unsigned pick = 0;
        int cand = cur + SWAP_GAIN - 1;
        for (unsigned id = 1; id <= PERSON_MAX; id++) {
            uint8_t *p = *(uint8_t **)(root + ROOT_PERSONS + (size_t)id * 8);
            if (!p || *(uint16_t *)(p + 0xE2) != cid || !valid(p) || !free_candidate(p, city))
                continue;
            int a = ability(p, focus);
            if (a > cand)
                cand = a, pick = id;
        }
        if (!pick)
            continue;
        int k = -1;
        for (int c = 0; c <= CORPS_MAX && k < 0; c++)
            if (best_corps[c] == corps || !best_corps[c])
                k = c;
        if (k < 0)
            continue;
        best_corps[k] = corps;
        if (cand - cur > best[k].gain) {
            best[k].region = region;
            best[k].person = pick;
            best[k].gain = cand - cur;
        }
    }
    for (int k = 0; k <= CORPS_MAX && best_corps[k]; k++) {
        Swap *w = &best[k];
        if (!w->region || best_corps[k][0x14] < cost)
            continue;
        if (assign(w->region, w->person, w->region[0x72]) && w->person <= PERSON_MAX)
            g_posts[w->person]++;
    }
}

static void __fastcall recount_hook(void)
{
    __try {
        rebalance();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[내정] 순 끝 전환 중 예외. 이번 순은 건너뜀");
    }
    ((void(__fastcall *)(void))RVA(FN_RECOUNT))();
    __try {
        reassign();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        plog("[내정] 담당관 교체 중 예외. 이번 순은 건너뜀");
    }
}

int feat_develop_init(void)
{
    Patch p;
    int ok = patch_call(&p, SITE_RECOUNT, FN_RECOUNT, (void *)recount_hook) && patch_apply("내정 균등 개발", &p, 1);
    if (!ok)
        plog("[내정] 균등 개발 연결 실패");
    return ok;
}
