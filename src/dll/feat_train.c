/* 그룹 8: 훈련비 비례 차감. 근거는 docs/08-traincost.md.

훈련 0x300E50은 비용 0x266850(도시, 담당)을 한 번 구해 금 검사(0x300ED1)와 차감(0x3010EE)·지출 누계(0x3010F9)에
그대로 쓴다. 대상(도시와 관할 관문)마다 상승량 0x268EA0이 하나라도 1 이상이면 전액을 낸다.
비용 호출을 감싸 "원래 비용 × 실제 상승 합 ÷ 계산 상승 합"(올림)을 돌려준다. 훈련을 적용하기 전에 불리므로
대상마다 상승식 0x268F70과 최대사기 0x22EA10으로 미리 계산한다(적용은 하지 않는다).
- 0x300EBA: 훈련 안의 비용(금 검사·차감·누계가 이 값을 쓴다).
- 0x268F10: 대상별 상승량 0x268EA0 안의 금 검사.
- 0x2DC763: 예상 지출(0x2DC440이 시험 모드 0x300E50(도시, 1) 뒤에 더한다).
훈련 담당 임명 화면의 비용 표시(0x780AE7)는 순당 최대 비용으로 그대로 둔다.
계산 상승이 없으면(모두 최대사기 등) 0이다. 원본은 이때 훈련하지 않고 예상 지출에만 전액을 넣었다.
*/
#include "common.h"

enum {
    FN_COST = 0x266850, FN_GAIN = 0x268F70, FN_MAX_MORALE = 0x22EA10, FN_CITY_OF = 0x22A7B0, FN_VALID = 0x32F500,
    SITE_TRAIN = 0x300EBA, SITE_GAIN_GOLD = 0x268F10, SITE_FORECAST = 0x2DC763,
    ROOT_GATES = 0xA0, ROOT_FORCES = 0xDF30,
    /* 게임 공용 목록 노드 풀(0x22CFD0이 ROOT+0xA0을 이렇게 따라간다): 노드 = {데이터, 다음} */
    POOL_READY = 0x27D10E8, POOL_NODES = 0x27D10F0, POOL_COUNT = 0x27D1120,
};

typedef int(__fastcall *CostFn)(void *, void *);
typedef int(__fastcall *ObjFn)(void *);
#define CALL(type, rva) ((type)RVA(rva))

/* 이민족(세력 종류 46~50)은 적용 0x2238F0이 사기를 바꾸지 않는다 */
static int no_morale(uint8_t *t)
{
    uint8_t *root = game_root();
    int fid = (*(ObjFn **)t)[0xB0 / 8](t);
    uint8_t *force = *(uint8_t **)(root + ROOT_FORCES + (size_t)(fid > 0x33 ? 0 : fid) * 8);
    if (!force)
        return 0;
    unsigned kind = (unsigned)((*(ObjFn **)force)[0x60 / 8](force) - 0x2E);
    return kind <= 4;
}

/* 대상 하나: 0x268EA0과 같은 조건으로 계산 상승, 0x2238F0과 같은 자르기로 실제 상승 */
static void add_target(uint8_t *t, void *officer, int64_t *calc, int64_t *act)
{
    int morale = t[0x44], max = CALL(ObjFn, FN_MAX_MORALE)(t);
    if (morale >= max || !*(int *)(t + 0x3C))
        return;
    int g = CALL(CostFn, FN_GAIN)(t, officer);
    if (g <= 0)
        return;
    *calc += g;
    if (!no_morale(t))
        *act += g < max - morale ? g : max - morale;
}

static int __fastcall cost_hook(uint8_t *city, uint8_t *officer)
{
    int base = CALL(CostFn, FN_COST)(city, officer);
    __try {
        if (!city || !officer || !CALL(ObjFn, FN_VALID)(officer) || !*(uint16_t *)(city + 0xCC))
            return base;
        int64_t calc = 0, act = 0;
        add_target(city, officer, &calc, &act);
        uint8_t *root = game_root();
        uint32_t *head = *(uint32_t **)(root + ROOT_GATES);
        uint8_t **pool = *(uint8_t ***)RVA(POOL_NODES);
        if (head && *(void **)RVA(POOL_READY) && pool && *head < *(uint32_t *)RVA(POOL_COUNT)) {
            int guard = 0;
            for (uint8_t *n = pool[*head]; n && guard < 4096; n = *(uint8_t **)(n + 8), guard++) {
                uint8_t *gate = *(uint8_t **)n;
                if (gate && ((uint8_t * (__fastcall *)(void *)) RVA(FN_CITY_OF))(gate) == city)
                    add_target(gate, officer, &calc, &act);
            }
        }
        if (!calc)
            return 0;
        return (int)((base * act + calc - 1) / calc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return base;
    }
}

int feat_train_init(void)
{
    Patch p[3];
    int ok = patch_call(&p[0], SITE_TRAIN, FN_COST, (void *)cost_hook) &&
             patch_call(&p[1], SITE_GAIN_GOLD, FN_COST, (void *)cost_hook) &&
             patch_call(&p[2], SITE_FORECAST, FN_COST, (void *)cost_hook) && patch_apply("훈련비 비례", p, 3);
    if (!ok)
        plog("[훈련비] 비례 차감 연결 실패");
    return ok;
}
