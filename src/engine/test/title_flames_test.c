/*
 * The title screen's fire.
 *
 * Deterministic by construction -- a private seeded LCG, never rand() -- so
 * these are real assertions rather than eyeballing. What is worth pinning is
 * not a particular pixel but the properties that make it look like fire:
 * it burns upward, it stays inside its column, it is hottest at the base, and
 * it is the same fire twice.
 */

#include "engine/title_flames.h"
#include "engine/torirs_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define COL_W TORIRS_FLAME_W
#define COL_H 265
/* Client-TS draws the fire nine rows down its 265-tall column. */
#define FLAME_ROW 9

static uint32_t*
flat_column(uint32_t colour)
{
    uint32_t* px = malloc((size_t)COL_W * COL_H * sizeof(*px));
    if( !px )
        return NULL;
    for( int i = 0; i < COL_W * COL_H; i++ )
        px[i] = colour;
    return px;
}

static void
run(
    struct TitleFlames* flames,
    int steps)
{
    for( int i = 0; i < steps; i++ )
        TitleFlames_Advance(flames, 35);
}

static int
count_lit(
    struct TitleFlames const* flames,
    enum TitleFlameSide side,
    uint32_t background,
    int y0,
    int y1)
{
    uint32_t const* px = TitleFlames_Pixels(flames, side);
    int lit = 0;
    for( int y = y0; y < y1; y++ )
        for( int x = 0; x < COL_W; x++ )
            if( (px[y * COL_W + x] & 0x00FFFFFFu) != (background & 0x00FFFFFFu) )
                lit++;
    return lit;
}

static void
init_flames(
    struct TitleFlames* flames,
    uint32_t background)
{
    uint32_t* left = flat_column(background);
    uint32_t* right = flat_column(background);
    uint32_t const* pair[TORIRS_FLAME_SIDES];

    pair[TORIRS_FLAME_LEFT] = left;
    pair[TORIRS_FLAME_RIGHT] = right;
    TitleFlames_Init(flames, pair, COL_W, COL_H, NULL);
    {
        /* Client-TS's own placement: the left brazier leans out past the
         * column's edge, the right one starts 24 in and is narrower. A
         * profile states these; a test that skips them gets no fire, which
         * is the contract test_unplaced_side_is_absent pins below. */
        struct TitleFlameGeometry const left_geom = { -22, -1, COL_W, FLAME_ROW };
        struct TitleFlameGeometry const right_geom = { 24, 1, 103, FLAME_ROW };
        TitleFlames_SetGeometry(flames, TORIRS_FLAME_LEFT, &left_geom);
        TitleFlames_SetGeometry(flames, TORIRS_FLAME_RIGHT, &right_geom);
    }
    free(left);
    free(right);
}

/* Nothing has burned yet, so the column is exactly the backdrop. */
static void
test_starts_as_the_backdrop(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF102030u;

    init_flames(&flames, bg);
    TEST_ASSERT(
        count_lit(&flames, TORIRS_FLAME_LEFT, bg, 0, COL_H) == 0,
        "an unstepped flame is just the backdrop");
    TitleFlames_Free(&flames);
}

static void
test_burns_and_rises(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;
    int low;
    int high;

    init_flames(&flames, bg);
    run(&flames, 40);

    low = count_lit(&flames, TORIRS_FLAME_LEFT, bg, TORIRS_FLAME_H / 2, TORIRS_FLAME_H - 1);
    high = count_lit(&flames, TORIRS_FLAME_LEFT, bg, 1, TORIRS_FLAME_H / 4);

    TEST_ASSERT(low > 0, "the fire lights its lower half");
    /* Hottest at the base: a fire that is denser at the top is a fire running
     * upside down, which is exactly what an inverted row shift produces. */
    TEST_ASSERT(low > high, "the fire is denser at its base than at its top");
    TitleFlames_Free(&flames);
}

/* The column must stay in its own 128px strip: it is blitted over a backdrop
 * the rest of the screen owns, and a stray pixel lands on the title art. */
static void
test_stays_in_its_column(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;
    uint32_t const* px;
    int outside = 0;

    init_flames(&flames, bg);
    run(&flames, 40);

    px = TitleFlames_Pixels(&flames, TORIRS_FLAME_LEFT);
    /*
     * Rows the fire never reaches stay pristine.
     *
     * The band is the geometry's, not the heat field's: the fire is drawn
     * FLAME_ROW rows down the column, so it ends below the heat field's own
     * height and the rows above it are the ones that must stay clean. This
     * is the whole reason the column is taller than the simulation -- the
     * flame's base belongs in the brazier bowl, not on the column's edge.
     */
    for( int y = 0; y < FLAME_ROW + 1; y++ )
        for( int x = 0; x < COL_W; x++ )
            if( (px[y * COL_W + x] & 0x00FFFFFFu) != 0u )
                outside++;
    for( int y = FLAME_ROW + TORIRS_FLAME_H - 1; y < COL_H; y++ )
        for( int x = 0; x < COL_W; x++ )
            if( (px[y * COL_W + x] & 0x00FFFFFFu) != 0u )
                outside++;
    TEST_ASSERT(outside == 0, "the fire stays inside the rows it simulates");
    TitleFlames_Free(&flames);
}

/* Same seed, same fire. This is what makes every assertion above meaningful,
 * and it is why the simulation carries its own RNG instead of calling rand(). */
static void
test_is_reproducible(void)
{
    struct TitleFlames a;
    struct TitleFlames b;
    uint32_t const bg = 0xFF000000u;

    init_flames(&a, bg);
    init_flames(&b, bg);
    run(&a, 25);
    run(&b, 25);

    TEST_ASSERT(
        memcmp(
            TitleFlames_Pixels(&a, TORIRS_FLAME_LEFT),
            TitleFlames_Pixels(&b, TORIRS_FLAME_LEFT),
            (size_t)COL_W * COL_H * sizeof(uint32_t)) == 0,
        "two runs of the same fire agree pixel for pixel");

    TitleFlames_Free(&a);
    TitleFlames_Free(&b);
}

/*
 * A brazier the profile never placed draws no fire at all.
 *
 * The alternative -- falling back to 0,0 -- puts a fire in the middle of a
 * wall on any revision that forgets the key, which is the sort of thing
 * that reads as a rendering bug rather than a missing declaration.
 */
static void
test_unplaced_side_is_absent(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;
    struct TitleFlameGeometry const geom = { -22, -1, COL_W, FLAME_ROW };
    uint32_t const* pair[TORIRS_FLAME_SIDES];
    uint32_t* col[TORIRS_FLAME_SIDES];

    for( int s = 0; s < TORIRS_FLAME_SIDES; s++ )
    {
        col[s] = malloc((size_t)COL_W * COL_H * sizeof(uint32_t));
        TEST_ASSERT(col[s] != NULL, "column alloc");
        for( int i = 0; i < COL_W * COL_H; i++ )
            col[s][i] = bg;
        pair[s] = col[s];
    }
    TitleFlames_Init(&flames, pair, COL_W, COL_H, NULL);
    /* Only the left side is placed. */
    TitleFlames_SetGeometry(&flames, TORIRS_FLAME_LEFT, &geom);
    run(&flames, 40);

    TEST_ASSERT(
        count_lit(&flames, TORIRS_FLAME_LEFT, bg, 0, COL_H) > 0,
        "the placed brazier burns");
    TEST_ASSERT(
        count_lit(&flames, TORIRS_FLAME_RIGHT, bg, 0, COL_H) == 0,
        "the unplaced brazier is absent, not misplaced");

    for( int s = 0; s < TORIRS_FLAME_SIDES; s++ )
        free(col[s]);
    TitleFlames_Free(&flames);
}

/* The two braziers must not be the same image side by side. */
static void
test_sides_differ(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;

    init_flames(&flames, bg);
    run(&flames, 40);
    TEST_ASSERT(
        memcmp(
            TitleFlames_Pixels(&flames, TORIRS_FLAME_LEFT),
            TitleFlames_Pixels(&flames, TORIRS_FLAME_RIGHT),
            (size_t)COL_W * COL_H * sizeof(uint32_t)) != 0,
        "the two braziers are not the same picture");
    TitleFlames_Free(&flames);
}

/* Advance is fixed-step: a call carrying less than one step's worth of time
 * must not move the fire, or its speed would follow the frame rate. */
static void
test_fixed_step(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;

    init_flames(&flames, bg);
    TEST_ASSERT(TitleFlames_Advance(&flames, 5) == 0, "a partial step does not advance");
    TEST_ASSERT(TitleFlames_Advance(&flames, 5) == 0, "nor does a second");
    TEST_ASSERT(TitleFlames_Advance(&flames, 30) == 1, "the accumulated time does");
    TitleFlames_Free(&flames);
}

/* A profile states its own colours; the fire must actually wear them. */
static void
test_palette_is_configurable(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;
    static int const green[TORIRS_FLAME_PALETTES][TORIRS_FLAME_PALETTE_STOPS] = {
        { 0x000000, 0x00FF00, 0x00FF00, 0x00FF00, 0x00FF00 },
        { 0x000000, 0x00FF00, 0x00FF00, 0x00FF00, 0x00FF00 },
        { 0x000000, 0x00FF00, 0x00FF00, 0x00FF00, 0x00FF00 },
    };
    uint32_t const* px;
    int red_seen = 0;

    init_flames(&flames, bg);
    TitleFlames_SetPalettes(&flames, green);
    run(&flames, 40);

    px = TitleFlames_Pixels(&flames, TORIRS_FLAME_LEFT);
    for( int i = 0; i < COL_W * COL_H; i++ )
        if( ((px[i] >> 16) & 0xFF) > 8 )
            red_seen++;
    TEST_ASSERT(red_seen == 0, "an all-green palette burns green");
    TitleFlames_Free(&flames);
}

/*
 * The two eras smooth the fire differently, and it is visible.
 *
 * Client-TS averages the four neighbours and excludes the centre. On a
 * lattice that leaves the two checkerboard sublattices reading only each
 * other, so they drift apart and the fire carries a permanent dither. The
 * deob's box blur includes the centre and mixes them.
 *
 * Measured as the mean absolute difference between ADJACENT pixels over the
 * one between pixels TWO apart. Above 1 means neighbours disagree more than
 * near-neighbours do, which is a checkerboard and nothing else; below 1 is
 * an ordinary smooth field. This is the number that told the difference
 * between 'the reference looks like this' and 'we picked the wrong blur'.
 */
static double
grain_ratio(struct TitleFlames* flames)
{
    double adjacent = 0.0;
    double apart = 0.0;
    int n = 0;

    for( int y = 180; y < 250; y++ )
    {
        for( int x = 20; x < 106; x++ )
        {
            int at = y * TORIRS_FLAME_W + x;
            adjacent += abs(flames->heat[at] - flames->heat[at + 1]);
            apart += abs(flames->heat[at] - flames->heat[at + 2]);
            n++;
        }
    }
    TEST_ASSERT(n > 0 && apart > 0.0, "the sample band carries some fire");
    return apart > 0.0 ? adjacent / apart : 0.0;
}

static void
test_blur_kinds_differ(void)
{
    struct TitleFlames flames;
    uint32_t const bg = 0xFF000000u;
    double neighbour4;
    double box;

    init_flames(&flames, bg);
    run(&flames, 40);
    neighbour4 = grain_ratio(&flames);
    TitleFlames_Free(&flames);

    init_flames(&flames, bg);
    TitleFlames_SetBlur(&flames, TORIRS_FLAME_BLUR_BOX);
    run(&flames, 40);
    box = grain_ratio(&flames);
    TitleFlames_Free(&flames);

    /* The 2004 value is not a defect to be driven down: it was matched to
     * within 0.03 of an independent transcription of Client-TS, so a change
     * here means the simulation drifted from its reference. */
    TEST_ASSERT(neighbour4 > 1.4, "the four-neighbour blur leaves the reference's dither");
    TEST_ASSERT(box < 1.0, "the box blur mixes the sublattices and removes it");
    TEST_ASSERT(box < neighbour4, "and is the smoother of the two");
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    test_starts_as_the_backdrop();
    test_burns_and_rises();
    test_stays_in_its_column();
    test_is_reproducible();
    test_sides_differ();
    test_unplaced_side_is_absent();
    test_blur_kinds_differ();
    test_fixed_step();
    test_palette_is_configurable();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("title_flames_test: ok (%d checks)\n", g_checks);
    return 0;
}
