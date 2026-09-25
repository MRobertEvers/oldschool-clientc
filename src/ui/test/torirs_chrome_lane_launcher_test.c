/*
 * The plugin launcher on a lane's own stone column: where it lands, and the
 * picture it composes.
 *
 * The numbers are the OSRS239 mobile toplevel's own. `side_left_chat` stacks
 * three 58x40 stones at y 0, 39 and 78 -- they overlap by the pixel their art
 * shares -- and the third of them, `chatting_button`, is the anchor. Its
 * backing child is 58x40 and its glyph child 33x36 at (12, 1), the cache's own
 * `cc_setposition(0, -1, centre, centre)`.
 *
 * Run: make -C src test-chrome-lane-launcher
 */

#include "ui/torirs_chrome_lane_launcher.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

enum
{
    STONE_W = 58,
    STONE_H = 40,
    GLYPH_W = 33,
    GLYPH_H = 36,
    GLYPH_X = 12,
    GLYPH_Y = 1
};

static void
test_pitch(void)
{
    /* The anchor at 78 with the toggle stone above it at 39. */
    CHECK(ToriRSLaneLauncher_Pitch(78, 39, STONE_H) == 39, "the column's pitch is the stone gap");
    /* One stone and no other: its own height is the best answer there is. */
    CHECK(ToriRSLaneLauncher_Pitch(0, -1, STONE_H) == STONE_H, "a lone stone pitches by height");
    /* A sibling at or below the anchor is not the stone above it. */
    CHECK(
        ToriRSLaneLauncher_Pitch(39, 78, STONE_H) == STONE_H,
        "a stone below the anchor is not its predecessor");
}

static void
test_y_follows_the_last_shown_stone(void)
{
    /* All three stones up: the column ends at 118 and the launcher is the
     * fourth, at 117. */
    CHECK(
        ToriRSLaneLauncher_Y(118, STONE_H, 39) == 117,
        "the launcher is one pitch below the anchor");
    /* The chatbox put away: the anchor is hidden, the column ends at 79, and
     * the launcher takes the place the anchor had rather than floating in the
     * gap it left. */
    CHECK(
        ToriRSLaneLauncher_Y(79, STONE_H, 39) == 78,
        "a hidden anchor hands its own place to the launcher");
}

/** A backing whose every pixel names its own position, so a copy that slid or
 *  scaled is visible in the answer rather than merely plausible. */
static void
fill_backing(uint32_t* backing, int width, int height)
{
    for( int row = 0; row < height; row++ )
        for( int column = 0; column < width; column++ )
            backing[(row * width) + column] =
                0xFF000000u | ((uint32_t)row << 8) | (uint32_t)column;
}

static void
test_compose_keeps_the_stone(void)
{
    uint32_t backing[STONE_W * STONE_H];
    uint32_t glyph[GLYPH_W * GLYPH_H];
    uint32_t out[STONE_W * STONE_H];
    int opaque_glyph = 1;

    fill_backing(backing, STONE_W, STONE_H);
    /* A fully transparent glyph: nothing of it may reach the picture. */
    memset(glyph, 0, sizeof(glyph));
    ToriRSLaneLauncher_Compose(
        backing, STONE_W, STONE_H, glyph, GLYPH_W, GLYPH_H, STONE_W, STONE_H, GLYPH_X, GLYPH_Y,
        GLYPH_W, GLYPH_H, out);
    CHECK(memcmp(out, backing, sizeof(backing)) == 0, "a clear glyph leaves the stone untouched");

    /* An opaque glyph fills exactly its box and nothing outside it. */
    for( int index = 0; index < GLYPH_W * GLYPH_H; index++ )
        glyph[index] = 0xFFFF0000u;
    ToriRSLaneLauncher_Compose(
        backing, STONE_W, STONE_H, glyph, GLYPH_W, GLYPH_H, STONE_W, STONE_H, GLYPH_X, GLYPH_Y,
        GLYPH_W, GLYPH_H, out);
    for( int row = 0; row < STONE_H; row++ )
        for( int column = 0; column < STONE_W; column++ )
        {
            int const inside = row >= GLYPH_Y && row < GLYPH_Y + GLYPH_H && column >= GLYPH_X &&
                               column < GLYPH_X + GLYPH_W;
            uint32_t const pixel = out[(row * STONE_W) + column];
            if( inside && pixel != 0xFFFF0000u )
                opaque_glyph = 0;
            if( !inside && pixel != backing[(row * STONE_W) + column] )
                opaque_glyph = 0;
        }
    CHECK(opaque_glyph, "the glyph fills the anchor's glyph box and only that");
}

static void
test_compose_blends_a_soft_edge(void)
{
    uint32_t backing[STONE_W * STONE_H];
    uint32_t glyph[GLYPH_W * GLYPH_H];
    uint32_t out[STONE_W * STONE_H];
    uint32_t centre;

    for( int index = 0; index < STONE_W * STONE_H; index++ )
        backing[index] = 0xFF000000u;
    for( int index = 0; index < GLYPH_W * GLYPH_H; index++ )
        glyph[index] = 0x80FFFFFFu;
    ToriRSLaneLauncher_Compose(
        backing, STONE_W, STONE_H, glyph, GLYPH_W, GLYPH_H, STONE_W, STONE_H, GLYPH_X, GLYPH_Y,
        GLYPH_W, GLYPH_H, out);
    centre = out[((GLYPH_Y + (GLYPH_H / 2)) * STONE_W) + GLYPH_X + (GLYPH_W / 2)];
    CHECK((centre >> 24) == 0xFFu, "the composed pixel is opaque over an opaque stone");
    CHECK(((centre >> 16) & 0xFFu) > 0x70u, "half-alpha white lifts the black stone");
    CHECK(((centre >> 16) & 0xFFu) < 0x90u, "and lifts it only halfway");
}

/** A glyph box that hangs over the edge is clipped, not written past. */
static void
test_compose_clips_an_overhanging_glyph(void)
{
    uint32_t backing[STONE_W * STONE_H];
    uint32_t glyph[GLYPH_W * GLYPH_H];
    uint32_t out[(STONE_W * STONE_H) + 1];
    uint32_t const canary = 0xDEADBEEFu;

    fill_backing(backing, STONE_W, STONE_H);
    for( int index = 0; index < GLYPH_W * GLYPH_H; index++ )
        glyph[index] = 0xFF00FF00u;
    out[STONE_W * STONE_H] = canary;
    ToriRSLaneLauncher_Compose(
        backing, STONE_W, STONE_H, glyph, GLYPH_W, GLYPH_H, STONE_W, STONE_H, STONE_W - 4,
        STONE_H - 4, GLYPH_W, GLYPH_H, out);
    CHECK(out[STONE_W * STONE_H] == canary, "an overhanging glyph writes nothing past the end");
    CHECK(
        out[((STONE_H - 1) * STONE_W) + STONE_W - 1] == 0xFF00FF00u,
        "the part of it that is inside still draws");
}

/** A backing at another size is scaled to the anchor's box. */
static void
test_compose_scales_the_backing(void)
{
    uint32_t backing[(STONE_W / 2) * (STONE_H / 2)];
    uint32_t glyph[4];
    uint32_t out[STONE_W * STONE_H];

    fill_backing(backing, STONE_W / 2, STONE_H / 2);
    memset(glyph, 0, sizeof(glyph));
    ToriRSLaneLauncher_Compose(
        backing, STONE_W / 2, STONE_H / 2, glyph, 2, 2, STONE_W, STONE_H, 0, 0, 2, 2, out);
    CHECK(out[0] == backing[0], "the top-left corner survives the scale");
    CHECK(
        out[((STONE_H - 1) * STONE_W) + STONE_W - 1] ==
            backing[(((STONE_H / 2) - 1) * (STONE_W / 2)) + (STONE_W / 2) - 1],
        "and so does the bottom-right one");
}

int
main(void)
{
    test_pitch();
    test_y_follows_the_last_shown_stone();
    test_compose_keeps_the_stone();
    test_compose_blends_a_soft_edge();
    test_compose_clips_an_overhanging_glyph();
    test_compose_scales_the_backing();
    if( g_failures )
    {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All lane launcher tests passed.\n");
    return 0;
}
