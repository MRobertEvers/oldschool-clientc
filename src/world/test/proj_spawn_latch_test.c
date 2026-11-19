/*
 * The developer projectile hotkey's two presses.
 *
 * With no world and no App: the first press marks the source; a press on
 * another tile launches from it and clears it; a press on the source's x and
 * z clears it whatever the level; and the out parameters are written only
 * for a launch.
 *
 * Run: make -C src test-proj-spawn-latch
 */

#include "world/proj_spawn_latch.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static struct ProjSpawnLatch
fresh(void)
{
    struct ProjSpawnLatch latch;

    memset(&latch, 0x5a, sizeof(latch));
    ProjSpawnLatch_Reset(&latch);
    return latch;
}

static void
test_first_press_marks_second_launches(void)
{
    struct ProjSpawnLatch latch = fresh();
    int x = -7;
    int z = -7;
    int level = -7;

    printf("TEST: the first press marks the source, the second launches from it\n");

    TEST_ASSERT(latch.src_x == -1 && latch.src_z == -1, "a reset marks no source");
    TEST_ASSERT(latch.src_level == 0, "and level 0");
    TEST_ASSERT(
        ProjSpawnLatch_Press(&latch, 10, 20, 1, &x, &z, &level) == PROJ_SPAWN_LATCH_MARKED,
        "the first press marks");
    TEST_ASSERT(x == -7 && z == -7 && level == -7, "marking hands nothing back");
    TEST_ASSERT(
        ProjSpawnLatch_Press(&latch, 12, 20, 0, &x, &z, &level) == PROJ_SPAWN_LATCH_LAUNCH,
        "a press on another tile launches");
    TEST_ASSERT(x == 10 && z == 20 && level == 1, "from the marked source and its level");
    TEST_ASSERT(latch.src_x == -1 && latch.src_z == -1, "the launch cleared the source");
    TEST_ASSERT(latch.src_level == 1, "the cleared source keeps its level");
    TEST_ASSERT(
        ProjSpawnLatch_Press(&latch, 12, 20, 0, &x, &z, &level) == PROJ_SPAWN_LATCH_MARKED,
        "the next press marks afresh");
}

static void
test_pressing_the_source_clears_it(void)
{
    struct ProjSpawnLatch latch = fresh();
    int x = -7;
    int z = -7;
    int level = -7;

    printf("TEST: a press on the source's x and z clears it, whatever the level\n");

    (void)ProjSpawnLatch_Press(&latch, 3, 4, 0, &x, &z, &level);
    TEST_ASSERT(
        ProjSpawnLatch_Press(&latch, 3, 4, 2, &x, &z, &level) == PROJ_SPAWN_LATCH_CLEARED,
        "the same x and z on another level clears");
    TEST_ASSERT(x == -7 && z == -7 && level == -7, "clearing hands nothing back");
    TEST_ASSERT(latch.src_x == -1 && latch.src_z == -1, "no source now");
    TEST_ASSERT(
        ProjSpawnLatch_Press(&latch, 3, 4, 2, &x, &z, &level) == PROJ_SPAWN_LATCH_MARKED,
        "so the next press marks");
}

int
main(void)
{
    test_first_press_marks_second_launches();
    test_pressing_the_source_clears_it();

    if( g_failures )
    {
        printf("proj_spawn_latch: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("proj_spawn_latch: all checks passed\n");
    return 0;
}
