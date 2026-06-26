#ifndef PAINTER_BENCH_REAL_H
#define PAINTER_BENCH_REAL_H

#include <stdint.h>

/* Benchmark seeded procedural scenes. Returns 1 if bucket slower overall. */
int painter_bench_seeded(uint32_t start, uint32_t count, int iters);

#ifdef FUZZ_WITH_CACHE
const char* painter_bench_find_cache_dir(void);

/* Benchmark dat2 cache world sweep (timing only). Returns 1 if bucket slower. */
int painter_bench_cache(const char* cache_dir, int iters);

/* Fuzz cache scene: optional bench + optional superset diff check.
 * Returns 0 pass, 1 fail, -1 load error. */
int painter_fuzz_cache_scene(const char* cache_dir, int do_bench, int bench_iters, int diff_check);
#endif /* FUZZ_WITH_CACHE */

#endif /* PAINTER_BENCH_REAL_H */
