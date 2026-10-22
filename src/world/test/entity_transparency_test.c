/*
 * The npc TRANSPARENCY fade against numbers worked by hand from the rev-239
 * deob (class657.method14216 / method14218, Model.method4912), not from the
 * port: world/entity_transparency.h. Run: make -C src test-entity-transparency.
 */
#include "world/entity_transparency.h"

#include <stdio.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            fprintf(stderr, "  ok   ");                                                            \
        else                                                                                       \
        {                                                                                          \
            fprintf(stderr, "  FAIL ");                                                            \
            g_failures++;                                                                          \
        }                                                                                          \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fprintf(stderr, "\n");                                                                     \
    } while( 0 )

int
main(void)
{
    {
        struct WorldEntityTransparency none = { 0 };

        CHECK(WorldEntityTransparency_Alpha(&none, 1000) == 0, "no block yet: opaque");
    }
    {
        /* Ocellus out: (30, 45, 0, 255, use) landing on cycle 1000. */
        struct WorldEntityTransparency t = { 0 };

        WorldEntityTransparency_Set(&t, 1000, 1030, 1045, 0, 255, 1);
        CHECK(WorldEntityTransparency_Alpha(&t, 1029) == 0, "before the start: what it showed");
        CHECK(WorldEntityTransparency_Alpha(&t, 1030) == 0, "on the start: from");
        /* (255 - 0) * 5/15 + 0 = 85 */
        CHECK(WorldEntityTransparency_Alpha(&t, 1035) == 85, "a third through: 85 (got %d)",
              WorldEntityTransparency_Alpha(&t, 1035));
        CHECK(WorldEntityTransparency_Alpha(&t, 1045) == 255, "on the end: to");
        CHECK(WorldEntityTransparency_Alpha(&t, 5000) == 255, "and after it");

        /* Ocellus re-added unseen: (-1, -1, 0, 255) on cycle 1120. */
        WorldEntityTransparency_Set(&t, 1120, 1119, 1119, 0, 255, 1);
        CHECK(WorldEntityTransparency_Alpha(&t, 1120) == 255, "(-1, -1): gone at once");

        /* In: (0, 20, 255, 0) on cycle 1150. */
        WorldEntityTransparency_Set(&t, 1150, 1150, 1170, 255, 0, 1);
        CHECK(WorldEntityTransparency_Alpha(&t, 1150) == 255, "fade in starts from 255");
        /* (0 - 255) * 10/20 + 255 = 127.5 -> 127 */
        CHECK(WorldEntityTransparency_Alpha(&t, 1160) == 127, "half way: 127 (got %d)",
              WorldEntityTransparency_Alpha(&t, 1160));
        CHECK(WorldEntityTransparency_Alpha(&t, 1170) == 0, "on the end: opaque");
    }
    {
        /* use_start 0 with a start still ahead: the fade begins from the
         * alpha last shown, not from the block's `from`. */
        struct WorldEntityTransparency t = { 0 };

        WorldEntityTransparency_Set(&t, 0, 0, 10, 0, 200, 1);
        (void)WorldEntityTransparency_Alpha(&t, 10);
        WorldEntityTransparency_Set(&t, 20, 30, 40, 0, 0, 0);
        CHECK(WorldEntityTransparency_Alpha(&t, 30) == 200, "starts from the shown 200 (got %d)",
              WorldEntityTransparency_Alpha(&t, 30));
        /* use_start 0 but the start already passed: from the block. */
        WorldEntityTransparency_Set(&t, 50, 40, 60, 100, 0, 0);
        CHECK(WorldEntityTransparency_Alpha(&t, 40) == 100, "a passed start uses `from` (got %d)",
              WorldEntityTransparency_Alpha(&t, 40));
    }
    /* Model.method4912: a + m * (253 - a) >> 8, faces at 253+ untouched. */
    CHECK(WorldEntityTransparency_Face(0, 0) == 0, "alpha 0 leaves a face alone");
    CHECK(WorldEntityTransparency_Face(0, 128) == 126, "opaque face at 128: 126 (got %d)",
          WorldEntityTransparency_Face(0, 128));
    CHECK(WorldEntityTransparency_Face(100, 128) == 176, "face 100 at 128: 176 (got %d)",
          WorldEntityTransparency_Face(100, 128));
    CHECK(WorldEntityTransparency_Face(254, 128) == 254, "a face at 254 is left");

    if( g_failures )
    {
        fprintf(stderr, "entity_transparency: %d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "entity_transparency: all tests passed\n");
    return 0;
}
