/*
 * TitleFlames — the title screen's fire.
 *
 * Two things are under test and the build file is half of it: `make
 * test-title-flames` links this against engine/title_flames.c and nothing else,
 * no $(OBJS) and no $(LDFLAGS). The module having no dependencies is a property
 * someone can quietly break, and the link failing is how they find out.
 *
 * The rest is what a decorative effect can actually be held to. Not "does it
 * look right" — that is a picture, and pictures do not belong in a test — but
 * the things that are wrong in a way nobody would notice for months: a palette
 * that steps instead of interpolating, a fire that stops burning, a render that
 * walks off its panel, a seed that does not reproduce.
 */

#include "engine/title_flames.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        if( !(cond) )                                                          \
        {                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            g_failures++;                                                      \
        }                                                                      \
    } while( 0 )

static uint32_t g_panel[TITLE_FLAMES_PANEL_PIXELS];
static uint32_t g_other[TITLE_FLAMES_PANEL_PIXELS];

static long
lit_pixels(const uint32_t* panel)
{
    long lit = 0;

    for( int i = 0; i < TITLE_FLAMES_PANEL_PIXELS; i++ )
    {
        if( (panel[i] & 0xFFFFFFu) != 0 )
            lit++;
    }
    return lit;
}

/*
 * The stops land on the segment boundaries and the space between them is
 * actually walked.
 *
 * The boundary check alone would pass an implementation that wrote the five
 * stops and left the other 251 entries black, which is why the midpoints are
 * checked too: 0x800000 is halfway from black to red, 0xff8000 halfway from red
 * to yellow. A palette that steps rather than ramps gives a fire made of four
 * flat colours, and it still burns, and it still looks vaguely like fire.
 */
static void
test_palette_expansion(void)
{
    struct TitleFlames* flames = TitleFlames_New(TitleFlames_ClassicRamps, 12345);
    const uint32_t* palette = TitleFlames_Palette(flames);

    CHECK(palette[0] == 0x000000u, "stop 0 is %06x, want 000000", palette[0]);
    CHECK(palette[64] == 0xff0000u, "stop 1 is %06x, want ff0000", palette[64]);
    CHECK(palette[128] == 0xffff00u, "stop 2 is %06x, want ffff00", palette[128]);
    CHECK(palette[192] == 0xffffffu, "stop 3 is %06x, want ffffff", palette[192]);
    CHECK(palette[255] == 0xffffffu, "last entry is %06x, want ffffff", palette[255]);

    CHECK(palette[32] == 0x800000u, "black->red midpoint is %06x, want 800000", palette[32]);
    CHECK(palette[96] == 0xff8000u, "red->yellow midpoint is %06x, want ff8000", palette[96]);

    TitleFlames_Free(flames);
}

/** It burns, on both sides, and the two sides are not the same picture. */
static void
test_it_burns(void)
{
    struct TitleFlames* flames = TitleFlames_New(TitleFlames_ClassicRamps, 4242);
    long left, right;

    for( int frame = 0; frame < 40; frame++ )
        TitleFlames_Frame(flames);

    memset(g_panel, 0, sizeof(g_panel));
    TitleFlames_RenderLeft(flames, g_panel);
    left = lit_pixels(g_panel);

    memset(g_other, 0, sizeof(g_other));
    TitleFlames_RenderRight(flames, g_other);
    right = lit_pixels(g_other);

    CHECK(left > 1000, "left panel lit only %ld pixels", left);
    CHECK(right > 1000, "right panel lit only %ld pixels", right);
    /* Same heat buffer, opposite shear: identical output would mean the shear
     * is not being applied and the panels are just two copies. */
    CHECK(memcmp(g_panel, g_other, sizeof(g_panel)) != 0,
          "left and right panels are identical");

    TitleFlames_Free(flames);
}

/*
 * The fire starts at row 9 and never writes above it.
 *
 * Both renders walk a shear that can be negative, so the destination cursor
 * moves backwards on some rows. Nine clear rows above the fire is the margin
 * that absorbs it, and this is what says the cursor stays inside it.
 */
static void
test_stays_inside_the_panel(void)
{
    struct TitleFlames* flames = TitleFlames_New(TitleFlames_ClassicRamps, 777);

    /* Long enough for the shear to reach both extremes. */
    for( int frame = 0; frame < 200; frame++ )
        TitleFlames_Frame(flames);

    memset(g_panel, 0, sizeof(g_panel));
    TitleFlames_RenderLeft(flames, g_panel);
    for( int i = 0; i < 9 * TITLE_FLAMES_PANEL_WIDTH; i++ )
    {
        if( (g_panel[i] & 0xFFFFFFu) != 0 )
        {
            CHECK(0, "left render wrote above row 9, at index %d", i);
            break;
        }
    }

    memset(g_panel, 0, sizeof(g_panel));
    TitleFlames_RenderRight(flames, g_panel);
    for( int i = 0; i < 9 * TITLE_FLAMES_PANEL_WIDTH; i++ )
    {
        if( (g_panel[i] & 0xFFFFFFu) != 0 )
        {
            CHECK(0, "right render wrote above row 9, at index %d", i);
            break;
        }
    }

    TitleFlames_Free(flames);
}

/** The composite blends RGB and leaves the destination's alpha byte alone. */
static void
test_destination_alpha_survives(void)
{
    struct TitleFlames* flames = TitleFlames_New(TitleFlames_ClassicRamps, 31337);

    for( int frame = 0; frame < 20; frame++ )
        TitleFlames_Frame(flames);

    for( int i = 0; i < TITLE_FLAMES_PANEL_PIXELS; i++ )
        g_panel[i] = 0xFF000000u;
    TitleFlames_RenderLeft(flames, g_panel);
    for( int i = 0; i < TITLE_FLAMES_PANEL_PIXELS; i++ )
    {
        if( (g_panel[i] & 0xFF000000u) != 0xFF000000u )
        {
            CHECK(0, "alpha clobbered at index %d (pixel %08x)", i, g_panel[i]);
            break;
        }
    }

    TitleFlames_Free(flames);
}

/*
 * The seed is the whole of the randomness.
 *
 * Two instances on one seed must agree frame for frame, and two on different
 * seeds must not. The first half is what makes a frame reproducible at all; the
 * second is what says the seed is actually reaching the generator, which a
 * constant-seeded implementation would also pass the first half of.
 */
static void
test_seed_determines_everything(void)
{
    struct TitleFlames* a = TitleFlames_New(TitleFlames_ClassicRamps, 999);
    struct TitleFlames* b = TitleFlames_New(TitleFlames_ClassicRamps, 999);
    struct TitleFlames* c = TitleFlames_New(TitleFlames_ClassicRamps, 1000);

    for( int frame = 0; frame < 10; frame++ )
    {
        TitleFlames_Frame(a);
        TitleFlames_Frame(b);
        TitleFlames_Frame(c);
    }

    memset(g_panel, 0, sizeof(g_panel));
    memset(g_other, 0, sizeof(g_other));
    TitleFlames_RenderLeft(a, g_panel);
    TitleFlames_RenderLeft(b, g_other);
    CHECK(memcmp(g_panel, g_other, sizeof(g_panel)) == 0, "same seed diverged");

    memset(g_other, 0, sizeof(g_other));
    TitleFlames_RenderLeft(c, g_other);
    CHECK(memcmp(g_panel, g_other, sizeof(g_panel)) != 0,
          "different seeds produced identical fire");

    TitleFlames_Free(a);
    TitleFlames_Free(b);
    TitleFlames_Free(c);
}

/*
 * A mask carves the cooling map, so registering one changes the fire.
 *
 * It is not asserted *where* the rune shows up: the mask is punched into a
 * cooling map that is then read at a moving offset, so which pixels brighten is
 * a property of the whole simulation and not of the mask's coordinates. That it
 * reaches the fire at all is the part worth pinning.
 */
static void
test_masks_reach_the_fire(void)
{
    static uint8_t solid[32 * 32];
    struct TitleFlames_Mask mask;
    struct TitleFlames* plain = TitleFlames_New(TitleFlames_ClassicRamps, 2024);
    struct TitleFlames* runed = TitleFlames_New(TitleFlames_ClassicRamps, 2024);

    memset(solid, 1, sizeof(solid));
    mask.pixels = solid;
    mask.width = 32;
    mask.height = 32;
    mask.origin_x = 48;
    mask.origin_y = 160;
    TitleFlames_SetMasks(runed, &mask, 1);

    /* Long enough for the cooling map to be regenerated at least once, which is
     * the only moment a mask is consulted. */
    for( int frame = 0; frame < 200; frame++ )
    {
        TitleFlames_Frame(plain);
        TitleFlames_Frame(runed);
    }

    memset(g_panel, 0, sizeof(g_panel));
    memset(g_other, 0, sizeof(g_other));
    TitleFlames_RenderLeft(plain, g_panel);
    TitleFlames_RenderLeft(runed, g_other);
    CHECK(memcmp(g_panel, g_other, sizeof(g_panel)) != 0,
          "registering a mask did not change the fire");

    TitleFlames_Free(plain);
    TitleFlames_Free(runed);
}

int
main(void)
{
    test_palette_expansion();
    test_it_burns();
    test_stays_inside_the_panel();
    test_destination_alpha_survives();
    test_seed_determines_everything();
    test_masks_reach_the_fire();

    /* A deallocator taking NULL is an idiom, not a tolerated failure. */
    TitleFlames_Free(NULL);

    if( g_failures )
        printf("title_flames: %d failure(s)\n", g_failures);
    else
        printf("title_flames: all checks passed\n");
    return g_failures != 0;
}
