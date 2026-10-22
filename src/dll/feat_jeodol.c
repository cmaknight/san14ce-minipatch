/* 그룹 2: 구심·악주 보유 부대 주위 아군 부대의 저돌 효과(효과 ID 1) 무효. 근거는 docs/02-traits.md.

게임은 저돌 효과를 "무장 효과 값 합" 0x2B2DA0(무장, 1)로 4곳에서 판정한다(부대 AI, 출진 명령설정 강제,
명령설정 선택지 2곳). 이 호출 4곳을 우리 함수로 돌려, 원래 값이 0이 아니고 그 무장이 대장인 부대가
구심·악주 대장 부대(자기 부대 제외) 반경 안에 있으면 0을 돌려준다.
범위 판정은 게임의 범위 개성(조건 9) 처리 0x27E1D0과 같은 재료를 쓴다:
칸 거리 0x2AA850, 반경 = 연계범위 [0x1A48FD8], 적대 판정 0x2CA280, 대상 제외 0x233440.
*/
#include "common.h"

enum {
    FN_EFFECT_SUM = 0x2B2DA0,  /* int (무장*, 효과 ID) */
    FN_VALID = 0x32F500,       /* int (객체*) */
    FN_TRAIT_SLOT = 0x2305D0,  /* int (무장*, 칸 0..8) → 개성 ID */
    FN_HEX_DIST = 0x2AA850,    /* int (칸, 칸) */
    FN_UNIT_FORCE = 0x22CC40,  /* int (부대*) */
    FN_HOSTILE = 0x2CA280,     /* int (세력, 세력) — 0이 아니면 적대 */
    FN_UNIT_SKIP = 0x233440,   /* int (부대*) — 0이 아니면 범위 효과 대상 아님 */
    RANGE_RVA = 0x1A48FD8,     /* 연계범위(기본 3) */
    ROOT_GENERALS = 0x158,     /* 무장 포인터 표 */
    ROOT_UNITS = 0x7E838,      /* 부대 포인터 표, 칸 1..500 */
    MAX_GENERAL = 0x1770,
    MAX_UNIT = 500,
    TRAIT_GUSIM = 21,
    TRAIT_AKJU = 23,
    EFFECT_JEODOL = 1,
    FN_SLOT_ACTIVE = 0x89E650, /* int (부대*, 칸) — 그 칸 개성 발동 중 */
    ROOT_TRAITS = 0x7DD18,
    SLOT_ARMY_LAYOUT_TICK = 0x1398A58 + 2 * 8, /* CArmyInfoLayout */
    FN_ARMY_LAYOUT_TICK = 0x1A7A40,
    FN_DOTS_REFRESH = 0x1AC030,  /* int (점 레이아웃, 부대) */
};

/* 저돌 판정 호출 지점(call 0x2B2DA0, edx = 1). 앞 바이트 문맥은 docs/02-traits.md */
static const uint32_t SITES[] = {0x6B50B, 0xA1D15, 0xA1DDF, 0x7FB203};
/* 발동개성 표시 판정 0x89E650 호출 지점 */
static const uint32_t ACTIVE_SITES[] = {0x1A4B72, 0x1AC1E0, 0x903492, 0x903FEA, 0x929152};

typedef int(__fastcall *EffectSumFn)(void *, int);
typedef int(__fastcall *ObjFn)(void *);
typedef int(__fastcall *SlotFn)(void *, int);
typedef int(__fastcall *IntIntFn)(int, int);
typedef int(__fastcall *UnitSlotFn)(void *, int);
typedef uint64_t(__fastcall *Fn4)(void *, void *, void *, void *);
typedef int(__fastcall *DotsFn)(void *, void *);

#define CALL(type, rva) ((type)RVA(rva))

static uint16_t general_id(const uint8_t *g) { return *(const uint16_t *)(g + 0x10); }
static uint16_t unit_leader(const uint8_t *u) { return *(const uint16_t *)(u + 0x12); }
static uint16_t unit_cell(const uint8_t *u) { return *(const uint16_t *)(u + 0x2A); }
static int unit_active(const uint8_t *u) { return u && u[0x10] != 0 && unit_leader(u) != 0; }

static uint8_t *unit_at(uint8_t *root, int i) { return *(uint8_t **)(root + ROOT_UNITS + (size_t)i * 8); }

static uint8_t *general_of(uint8_t *root, unsigned id)
{
    if (id == 0 || id > MAX_GENERAL)
        return NULL;
    return *(uint8_t **)(root + ROOT_GENERALS + (size_t)id * 8);
}

static int has_range_trait(uint8_t *g)
{
    if (!g || !CALL(ObjFn, FN_VALID)(g))
        return 0;
    for (int i = 0; i < 9; i++) {
        int t = CALL(SlotFn, FN_TRAIT_SLOT)(g, i);
        if (t == TRAIT_GUSIM || t == TRAIT_AKJU)
            return 1;
    }
    return 0;
}

/* 부대 target이 구심·악주 대장 부대(자기 제외) 반경 안이면 1 */
static int unit_nullified(uint8_t *root, uint8_t *target)
{
    if (!target || !unit_active(target) || CALL(ObjFn, FN_UNIT_SKIP)(target))
        return 0;
    int range = *(int *)RVA(RANGE_RVA);
    int tforce = CALL(ObjFn, FN_UNIT_FORCE)(target);
    for (int i = 1; i <= MAX_UNIT; i++) {
        uint8_t *h = unit_at(root, i);
        if (h == target || !unit_active(h))
            continue;
        if (CALL(IntIntFn, FN_HEX_DIST)(unit_cell(h), unit_cell(target)) > range)
            continue;
        if (!has_range_trait(general_of(root, unit_leader(h))))
            continue;
        if (CALL(IntIntFn, FN_HOSTILE)(CALL(ObjFn, FN_UNIT_FORCE)(h), tforce))
            continue;
        return 1;
    }
    return 0;
}

/* g가 대장인 부대가 무효 범위 안이면 1 */
static int jeodol_nullified(uint8_t *g)
{
    uint8_t *root = game_root();
    if (!root || !g)
        return 0;
    uint16_t gid = general_id(g);
    for (int i = 1; i <= MAX_UNIT; i++) {
        uint8_t *u = unit_at(root, i);
        if (unit_active(u) && unit_leader(u) == gid)
            return unit_nullified(root, u);
    }
    return 0;
}

static int __fastcall effect_sum_hook(void *general, int effect)
{
    int v = CALL(EffectSumFn, FN_EFFECT_SUM)(general, effect);
    if (v != 0 && effect == EFFECT_JEODOL && jeodol_nullified((uint8_t *)general))
        return 0;
    return v;
}

/* 발동개성 표시: 0x89E650(부대, 칸) = 그 칸 개성이 지금 발동 중인지(상세 창·지도 표시 5곳이 씀).
   저돌 효과(효과 ID 1)를 가진 개성이고 그 부대가 무효 범위 안이면 꺼짐으로 보인다(공군 등 다른 효과는 그대로 적용). */
static int __fastcall active_hook(void *unit, int slot)
{
    int r = CALL(UnitSlotFn, FN_SLOT_ACTIVE)(unit, slot);
    if (!r || !unit)
        return r;
    uint8_t *root = game_root();
    uint8_t *g = root ? general_of(root, unit_leader((uint8_t *)unit)) : NULL;
    if (!g || !CALL(ObjFn, FN_VALID)(g))
        return r;
    int t = CALL(SlotFn, FN_TRAIT_SLOT)(g, slot);
    if (t <= 0 || t > 0x162)
        return r;
    uint8_t *trait = *(uint8_t **)(root + ROOT_TRAITS + (size_t)t * 8);
    if (!trait || (trait[0xB6] != EFFECT_JEODOL && trait[0xBA] != EFFECT_JEODOL))
        return r;
    return unit_nullified(root, (uint8_t *)unit) ? 0 : r;
}

/* 부대 마크 옆 발동개성 점: 부대 표시 레이아웃(CArmyInfoLayout)의 매 프레임 함수(vtable 0x1398A58 슬롯 2, 0x1A7A40)는
   +0x248이 켜졌을 때만 "저장된 발동 비트와 새 결과를 비교해 바뀐 칸만 갱신"(0x1AC030(+0x260, 부대))을 부른다.
   게임은 저돌을 늘 발동으로 보므로 갱신 계기가 없다. 저돌 효과 부대에 한해 매 프레임 직접 부른다(바뀐 칸만 고치므로 반복해도 같다). */
static int unit_has_jeodol(uint8_t *root, uint8_t *unit)
{
    uint8_t *g = general_of(root, unit_leader(unit));
    if (!g || !CALL(ObjFn, FN_VALID)(g))
        return 0;
    for (int i = 0; i < 9; i++) {
        int t = CALL(SlotFn, FN_TRAIT_SLOT)(g, i);
        if (t <= 0 || t > 0x162)
            continue;
        uint8_t *trait = *(uint8_t **)(root + ROOT_TRAITS + (size_t)t * 8);
        if (trait && (trait[0xB6] == EFFECT_JEODOL || trait[0xBA] == EFFECT_JEODOL))
            return 1;
    }
    return 0;
}

/* 명령설정 적접근시(부대 +0x40: 1 무시, 2 공격). 게임은 출진 때 저돌 부대를 2로 고정한다(0xA1D00).
   저돌 효과가 무효가 되는 순간 1(무시)로, 다시 발동하는 순간 2(공격)로 바꾼다. 부대별 직전 상태를 기억한다. */
#define APPROACH_OFF 0x40
static struct { void *unit; uint16_t leader; int8_t nullified; } g_seen[512];

static void sync_approach(uint8_t *root, uint8_t *unit)
{
    int now = unit_nullified(root, unit);
    int i, free_i = -1;
    for (i = 0; i < 512; i++) {
        if (g_seen[i].unit == unit)
            break;
        if (!g_seen[i].unit && free_i < 0)
            free_i = i;
    }
    int before = 0; /* 처음 보면 게임이 고정한 "발동(공격)" 상태로 본다 */
    uint16_t leader = unit_leader(unit);
    if (i < 512 && g_seen[i].leader == leader)
        before = g_seen[i].nullified;
    else if (i < 512)
        g_seen[i].leader = leader; /* 같은 객체에 다른 부대가 새로 출진 */
    else if (free_i >= 0) {
        i = free_i;
        g_seen[i].unit = unit;
        g_seen[i].leader = leader;
    } else
        return;
    g_seen[i].nullified = (int8_t)now;
    if (now == before)
        return;
    unit[APPROACH_OFF] = now ? 1 : 2;
}

static uint64_t __fastcall army_layout_hook(uint8_t *layout, void *a, void *b, void *c)
{
    uint64_t r = CALL(Fn4, FN_ARMY_LAYOUT_TICK)(layout, a, b, c);
    uint8_t *root = game_root();
    uint8_t *unit = *(uint8_t **)(layout + 0x1F0);
    void *dots = *(void **)(layout + 0x260);
    if (root && unit && CALL(ObjFn, FN_VALID)(unit) && unit_has_jeodol(root, unit)) {
        if (dots)
            CALL(DotsFn, FN_DOTS_REFRESH)(dots, unit);
        sync_approach(root, unit);
    }
    return r;
}

int feat_jeodol_init(void)
{
    Patch p[sizeof SITES / sizeof SITES[0]];
    /* 각 지점 앞의 `mov edx, 1`(BA 01 00 00 00)까지 확인해 효과 ID가 1인 호출만 돌린다. */
    static const uint8_t mov_edx_1[] = {0xBA, 0x01, 0x00, 0x00, 0x00};
    static const int8_t edx_back[] = {-5, -5, -8, -12}; /* call 지점에서 mov edx,1 까지의 거리 */
    for (int i = 0; i < (int)(sizeof SITES / sizeof SITES[0]); i++) {
        if (!bytes_equal(SITES[i] + edx_back[i], mov_edx_1, sizeof mov_edx_1)) {
            plog("[저돌 무효] 지점 %06X 앞의 효과 ID 1 확인 실패. 적용하지 않음", SITES[i]);
            return 0;
        }
        if (!patch_call(&p[i], SITES[i], FN_EFFECT_SUM, (void *)effect_sum_hook)) {
            plog("[저돌 무효] 중계기 생성 실패");
            return 0;
        }
    }
    if (!patch_apply("저돌 무효", p, (int)(sizeof p / sizeof p[0])))
        return 0;
    Patch d[sizeof ACTIVE_SITES / sizeof ACTIVE_SITES[0]];
    for (int i = 0; i < (int)(sizeof ACTIVE_SITES / sizeof ACTIVE_SITES[0]); i++)
        if (!patch_call(&d[i], ACTIVE_SITES[i], FN_SLOT_ACTIVE, (void *)active_hook))
            return 1;
    patch_apply("저돌 무효 표시", d, (int)(sizeof d / sizeof d[0]));
    if (!vtable_swap(SLOT_ARMY_LAYOUT_TICK, FN_ARMY_LAYOUT_TICK, (void *)army_layout_hook))
        plog("[저돌 무효] 부대 마크 점 갱신 연결 실패");
    return 1;
}
