/* san14patch 프록시 DLL 공통 선언. 구조는 docs/00-overview.md. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

/* 지원 빌드: SAN14CE_KO.exe 1.0.1 (PE timestamp 0x6A983C25) */
#define GAME_TIMESTAMP 0x6A983C25u
#define GAME_IMAGE_SIZE 0x29FF000u

extern uint8_t *g_base; /* 게임 이미지 베이스 */
#define RVA(x) (g_base + (x))

/* log.c */
void log_open(void);
void plog(const char *fmt, ...);

/* config.c: san14patch.ini [groups] */
typedef struct {
    int trait_balance;  /* 그룹 2 */
    int expert_scaling; /* 그룹 1 */
    int auto_search;    /* 그룹 3 */
    int trait_tooltip;  /* 그룹 4 */
    int fog_of_war;     /* 그룹 100(임시) */
    int log_plus;       /* 그룹 6 */
    int post_busy;      /* 그룹 7 */
    int train_cost;     /* 그룹 8 */
    int even_develop;   /* 그룹 9 */
} Config;
extern Config g_cfg;
void config_load(void);
void game_dir(wchar_t *out, size_t n);

/* patch.c: 원래 바이트를 확인한 뒤 일괄 적용. 하나라도 어긋나면 그 기능은 전부 포기한다. */
typedef struct {
    uint32_t rva;
    uint8_t len;
    uint8_t orig[16];
    uint8_t repl[16];
} Patch;
int bytes_equal(uint32_t rva, const void *want, size_t n);
int patch_apply(const char *feature, Patch *p, int n);
/* 게임 이미지 ±2 GB 안에 `jmp [rip]; dq target` 중계기를 만들고 주소를 돌려준다. */
uint8_t *relay_new(void *target);
void *near_alloc(size_t n, DWORD prot);
/* call rel32(E8) 지점 site_rva를 target으로 돌리는 Patch를 채운다. 원래 호출 대상 RVA를 *orig_callee에. */
int patch_call(Patch *p, uint32_t site_rva, uint32_t expect_callee_rva, void *target);
/* .rdata vtable 슬롯 교체. 원래 값이 expect_rva가 아니면 실패. */
int vtable_swap(uint32_t slot_rva, uint32_t expect_rva, void *target);
/* vtable_swap 되돌리기: 현재 값이 ours일 때만 원래 함수로 */
void vtable_unswap(uint32_t slot_rva, void *ours, uint32_t orig_rva);

/* 사용자 설정: %LOCALAPPDATA%\san14patch\settings.ini (모든 세이브 공통) */
int settings_get_int(const wchar_t *section, const wchar_t *key, int def);
void settings_set_int(const wchar_t *section, const wchar_t *key, int value);

/* 게임 구조(docs/00-overview.md) */
#define ROOT_PTR_RVA 0x277DF18u
static inline uint8_t *game_root(void) { return *(uint8_t **)RVA(ROOT_PTR_RVA); }
/* 세이브에 저장되는 미사용 칸: 월드([root+0x871E8]) +0x17AC, 4바이트. 게임은 새 게임 초기화(0x325B80)에서
   0으로 채우고 직렬화(0x337A90)만 한다. 바이트별로 나눠 쓴다(0이면 기본값). */
enum { WX_AUTO_ON = 0, WX_BASE_SELF = 1, WX_BASE_OTHER = 2 };
static inline uint8_t *world_extra(void)
{
    uint8_t *root = game_root();
    uint8_t *world = root ? *(uint8_t **)(root + 0x871E8) : NULL;
    return world ? world + 0x17AC : NULL;
}

/* 기능 초기화. 실패해도 게임은 그대로 진행한다. */
int feat_jeodol_init(void);
int feat_autosearch_init(void);
int feat_tooltip_init(void);
int feat_expert_init(void);
int feat_post_init(void);
int feat_train_init(void);
int feat_logs_init(void);
int feat_logwnd_init(void);
int feat_develop_init(void);
/* 공용 말풍선(feat_tooltip.c). key가 같으면 글자를 다시 넣지 않는다. */
void tooltip_show(const void *key, const wchar_t *text);
void tooltip_hide(const void *key);
void *ui_hovered(void);
int ui_mouse_in(void *w);
int feat_autosearch_ui_init(void);
int feat_autosearch_enabled(void);
