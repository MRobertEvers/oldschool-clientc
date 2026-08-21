#ifndef WORLD_BUILDER_TEST_HARNESS_H
#define WORLD_BUILDER_TEST_HARNESS_H

#include <stdio.h>

extern int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Unit tests (no disk). */
void test_painters_smoke(void);
void test_painters_tile_order(void);
void test_minimap_push_down(void);
void test_builder_lifecycle(void);
void test_prerotate_placement(void);

/* Cache-backed render integration test (skips if cache dir missing). */
void test_world_builder_cache_render(void);

/* Rebuild stutter bench (WB_BENCH=1 runs this instead of the tests). */
void test_world_builder_bench(void);

#endif
