/*
 * The baked cull map's rebake memo (render/painter_cull_bake.h).
 *
 * Nothing baked: always due. After a bake, a viewport within 7 pixels on both
 * axes is not due, 8 or more on either axis is, in either direction. A half
 * remembered size (one axis 0) counts as nothing baked.
 *
 * Run: make -C src test-painter-cull-bake
 */

#include "render/painter_cull_bake.h"

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

int
main(void)
{
    struct PainterCullBake bake;

    memset(&bake, 0, sizeof(bake));
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 512, 334), "nothing baked: due");
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 0, 0), "nothing baked, any size: due");

    PainterCullBake_Note(&bake, 512, 334);
    TEST_ASSERT(bake.w == 512 && bake.h == 334, "the baked size is kept");
    TEST_ASSERT(!PainterCullBake_NeedsBake(&bake, 512, 334), "same size: not due");
    TEST_ASSERT(!PainterCullBake_NeedsBake(&bake, 519, 341), "+7 on both: not due");
    TEST_ASSERT(!PainterCullBake_NeedsBake(&bake, 505, 327), "-7 on both: not due");
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 520, 334), "+8 wide: due");
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 504, 334), "-8 wide: due");
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 512, 342), "+8 tall: due");
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 512, 326), "-8 tall: due");

    PainterCullBake_Note(&bake, 765, 503);
    TEST_ASSERT(!PainterCullBake_NeedsBake(&bake, 770, 500), "re-noted: measured from the new size");
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 512, 334), "re-noted: the old size is due");

    PainterCullBake_Note(&bake, 765, 0);
    TEST_ASSERT(PainterCullBake_NeedsBake(&bake, 765, 0), "one axis 0: counts as nothing baked");

    if( g_failures )
    {
        printf("painter_cull_bake_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("painter_cull_bake_test: ok\n");
    return 0;
}
