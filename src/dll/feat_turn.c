/* 그룹 10 순 처리 훅.

순 끝 단계 39(0x42F4B0)가 부르는 순 처리(0x305390, 거점마다 수입·모병·개발·훈련) 호출 0x431824를 감싸, 처리 뒤에
intercept_track(feat_intercept.c)을 부른다. 관문으로 들여보낸 부대의 들어가기 다시 걸기, 빈 관문 태수 보내기, 거점 주인
바뀐 날 갱신이 여기서 돈다.

세션: 게임 날짜(월드 +0x34: u16 년, +2 월, +3 일 1·11·21)가 뒤로 가거나 DATE_STEP_MAX일 넘게 건너뛰거나 월드가 바뀌면
불러오기·새 게임으로 보고 세션 번호를 올린다. 거점 주인 기록은 세션이 바뀌면 지운다(turn_session).
*/
#include "common.h"

enum {
    SITE_TURN = 0x431824, FN_TURN = 0x305390,
    ROOT_WORLD = 0x871E8,
};
#define WORLD_YMD 0x34
#define DATE_STEP_MAX 40 /* 이보다 크게 앞으로 건너뛰면 다른 세이브 */

typedef uint64_t(__fastcall *TurnFn)(void *, void *, void *, void *);

static SRWLOCK g_lock = SRWLOCK_INIT;
static int g_on, g_session, g_seen, g_last;
static void *g_world;

static uint8_t *world_of(uint8_t *root) { return root ? *(uint8_t **)(root + ROOT_WORLD) : NULL; }

static int date_key(uint8_t *world)
{
    int y = *(int16_t *)(world + WORLD_YMD), m = *(int8_t *)(world + WORLD_YMD + 2), x = *(int8_t *)(world + WORLD_YMD + 3);
    return y * 372 + (m - 1) * 31 + x;
}

/* 날짜가 끊겼으면 새 세션. 세션 번호를 돌려준다 */
static int check_session(uint8_t *world)
{
    int key = date_key(world);
    AcquireSRWLockExclusive(&g_lock);
    if (!g_seen || world != g_world || key < g_last || key > g_last + DATE_STEP_MAX)
        g_session++;
    g_seen = 1;
    g_world = world;
    g_last = key;
    int s = g_session;
    ReleaseSRWLockExclusive(&g_lock);
    return s;
}

int turn_session(void)
{
    if (!g_on)
        return 0;
    int s = 0;
    __try {
        uint8_t *world = world_of(game_root());
        s = world ? check_session(world) : g_session;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s = g_session;
    }
    return s;
}

static uint64_t __fastcall turn_hook(void *a, void *b, void *c, void *d)
{
    uint64_t r = ((TurnFn)RVA(FN_TURN))(a, b, c, d);
    __try {
        uint8_t *world = world_of(game_root());
        if (world) {
            check_session(world);
            intercept_track();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int warned;
        if (!warned++)
            plog("[AI 출진] 순 처리 중 예외. 이번 순은 건너뜀");
    }
    return r;
}

int feat_turn_init(void)
{
    Patch p;
    if (!patch_call(&p, SITE_TURN, FN_TURN, (void *)turn_hook) || !patch_apply("AI 출진 순 처리", &p, 1))
        return 0;
    g_on = 1;
    return 1;
}
