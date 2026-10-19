/*
 * The player TINT's colour rule against numbers worked by hand from the
 * rev-239 deob (class158.method5310 and Statics.method3506), not from the
 * port: world/entity_tint.h. Run: make -C src test-entity-tint.
 */
#include "world/entity_tint.h"

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

static int
hsl(int h, int s, int l)
{
    return h << 10 | s << 7 | l;
}

int
main(void)
{
    /* The recorded Apmeken corruption: (0, 600, 0, 6, 24, 112) at cycle 1000. */
    struct WorldEntityTint tint = { 1000, 1600, 0, 6, 24, 112 };
    struct WorldEntityTint heavy = { 0, 10, 0, 0, 0, (int8_t)200 };
    struct WorldEntityTint off = { 0, 10, -1, -1, -1, 0 };

    CHECK(!WorldEntityTint_Active(&tint, 999), "not before its start cycle");
    CHECK(WorldEntityTint_Active(&tint, 1000), "on its start cycle");
    CHECK(WorldEntityTint_Active(&tint, 1599), "on its last cycle");
    CHECK(!WorldEntityTint_Active(&tint, 1600), "not on its end cycle (half-open)");
    CHECK(!WorldEntityTint_Active(&off, 5), "weight 0 (the clear) draws nothing");
    /* method5142 tests the weight as a signed byte: 200 is -56. */
    CHECK(!WorldEntityTint_Active(&heavy, 5), "a weight of 128..255 draws nothing");

    /*
     * h 40 s 3 l 100 toward (0, 6, 24) by 112/128, Java int arithmetic:
     *   h: 40 + ((0 - 40) * 112 >> 7)   = 40 + (-4480 >> 7) = 40 - 35 = 5
     *   s:  3 + ((6 - 3) * 112 >> 7)    =  3 + (336 >> 7)   =  3 + 2  = 5
     *   l: 100 + ((24 - 100) * 112 >> 7) = 100 + (-8512 >> 7) = 100 - 67 = 33
     *      (an arithmetic shift floors: -66.5 -> -67)
     */
    {
        int got = WorldEntityTint_Hsl16(hsl(40, 3, 100), 0, 6, 24, 112);
        CHECK(got == hsl(5, 5, 33), "the recorded tint moves h40 s3 l100 to h5 s5 l33 (got h%d s%d l%d)",
              got >> 10 & 63, got >> 7 & 7, got & 127);
    }
    /* A -1 channel is left exactly where it was. */
    {
        int got = WorldEntityTint_Hsl16(hsl(40, 3, 100), -1, -1, 24, 112);
        CHECK((got >> 10 & 63) == 40 && (got >> 7 & 7) == 3 && (got & 127) == 33,
              "hue and saturation -1 are untouched, lightness still moves");
    }
    /* Weight 0 is the identity, whatever the targets. */
    CHECK(WorldEntityTint_Hsl16(hsl(17, 2, 64), 0, 6, 24, 0) == hsl(17, 2, 64),
          "weight 0 changes nothing");
    /* Weight 127 lands one step short of the target, never past it. */
    {
        int got = WorldEntityTint_Hsl16(hsl(63, 7, 127), 0, 0, 0, 127);
        CHECK(got == hsl(0, 0, 0) || ((got >> 10 & 63) <= 1 && (got & 127) <= 1),
              "weight 127 goes (almost) all the way: h%d s%d l%d", got >> 10 & 63, got >> 7 & 7,
              got & 127);
    }

    if( g_failures )
    {
        fprintf(stderr, "entity_tint: %d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "entity_tint: all tests passed\n");
    return 0;
}
