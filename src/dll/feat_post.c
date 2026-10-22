/* 그룹 7: 이동·임무 중 장수 관직 임명. 근거는 docs/07-post.md.

관직 화면(CStrategyPostState)은 행동 중 판정 0x232240으로 무장을 고를 수 없게 하고(0x72D385),
보유자가 행동 중인 관직을 잠근다(0x72D9DB). 이 두 호출만 감싸 이동·임무 중이면 허용한다.
출진 중(소재 +0xE4가 부대 0x3E~0x231, 수송대 포함)과 사라짐(행동 종류 0x10)은 원래대로 막는다.
확정 경로 0x73F790 → 0x1F1750은 행동 중인지 다시 보지 않는다. 다른 명령과 AI 관직 경로는 그대로다.
*/
#include "common.h"

enum {
    FN_BUSY = 0x232240,
    SITE_PICK = 0x72D385, SITE_LOCK = 0x72D9DB,
    FN_ACTION = 0x22AA70, FN_VALID = 0x32F500,
    LOC_UNIT_MIN = 0x3E, LOC_UNIT_MAX = 0x231,
    ACT_DISAPPEAR = 0x10,
};

typedef int(__fastcall *PersonFn)(void *);
#define CALL(type, rva) ((type)RVA(rva))

static int __fastcall busy_hook(uint8_t *p)
{
    int r = CALL(PersonFn, FN_BUSY)(p);
    if (!r)
        return 0;
    __try {
        unsigned loc = *(uint16_t *)(p + 0xE4);
        if (loc >= LOC_UNIT_MIN && loc <= LOC_UNIT_MAX)
            return 1;
        uint8_t *act = ((uint8_t * (__fastcall *)(void *)) RVA(FN_ACTION))(p);
        if (act && CALL(PersonFn, FN_VALID)(act)) {
            typedef int(__fastcall * KindFn)(void *);
            if ((*(KindFn **)act)[0x98 / 8](act) == ACT_DISAPPEAR)
                return 1;
        }
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return r;
    }
}

int feat_post_init(void)
{
    Patch p[2];
    int ok = patch_call(&p[0], SITE_PICK, FN_BUSY, (void *)busy_hook) &&
             patch_call(&p[1], SITE_LOCK, FN_BUSY, (void *)busy_hook) && patch_apply("관직 임명 완화", p, 2);
    if (!ok)
        plog("[관직] 이동·임무 중 임명 연결 실패");
    return ok;
}
