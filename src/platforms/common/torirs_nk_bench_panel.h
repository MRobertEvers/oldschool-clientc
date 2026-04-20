#ifndef TORIRS_NK_BENCH_PANEL_H
#define TORIRS_NK_BENCH_PANEL_H

struct nk_context;
struct GGame;

#ifdef __cplusplus
extern "C" {
#endif

/** Optional extra Nuklear window from the bench_sdl2 client (NULL = no-op). */
void
torirs_nk_bench_panel_register(void (*draw)(struct nk_context* nk, struct GGame* game));

void
torirs_nk_bench_panel_unregister(void);

#ifdef __cplusplus
}
#endif

#endif /* TORIRS_NK_BENCH_PANEL_H */
