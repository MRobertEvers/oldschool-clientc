/*
 * Which painter command an in-scene tile marker is drawn after.
 *
 * The stage keeps a marker's primitives while one draw_tile call has it open,
 * and once a frame matches them against the terrain commands of the paint.
 * The answer has three ways to be wrong and each looks like something else on
 * screen: the FIRST command of a tile instead of the last puts the marker
 * under a bridge deck; an unstable sort breaks a polygon run apart; and a tile
 * the paint never drew has to sort last and be counted, not dropped.
 *
 * Run: make -C src test-tile-mark-stage
 */

#include "render/tile_mark_stage.h"

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

/* Big enough that a stack copy is a bad idea. */
static struct TileMarkStage g_stage;

static struct UITreeEntityOverlay
a_point(int x)
{
    struct UITreeEntityOverlay item;
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_RECT;
    item.x = x;
    item.y = 3;
    item.w = 1;
    item.h = 1;
    item.clip_x = -99;
    item.clip_y = -99;
    item.clip_w = -99;
    item.clip_h = -99;
    return item;
}

static void
push_marker(struct TileMarkStage* stage, int key, int first_x, int primitives)
{
    TileMarkStage_Begin(stage, key, 4, 5, 600, 400);
    for( int i = 0; i < primitives; i++ )
    {
        struct UITreeEntityOverlay const item = a_point(first_x + i);
        TileMarkStage_Push(stage, &item);
    }
    TileMarkStage_End(stage);
}

static void
test_a_reset_stage_is_empty_and_closed(void)
{
    memset(&g_stage, 0x5A, sizeof(g_stage));
    TileMarkStage_Reset(&g_stage);
    TEST_ASSERT(TileMarkStage_Count(&g_stage) == 0, "no marks after a reset");
    TEST_ASSERT(!TileMarkStage_IsOpen(&g_stage), "and the stage is closed");
    TEST_ASSERT(g_stage.overflow == 0, "and nothing has overflowed");
    TEST_ASSERT(g_stage.unplaced == 0, "and nothing is unplaced");
    TEST_ASSERT(g_stage.clip_w == 0, "and the clip is cleared");
}

static void
test_keys_are_distinct_per_tile_and_level(void)
{
    int const a = TileMarkStage_Key(10, 20, 0);
    int const b = TileMarkStage_Key(10, 20, 1);
    int const c = TileMarkStage_Key(20, 10, 0);
    int const d = TileMarkStage_Key(10, 21, 0);

    TEST_ASSERT(a != b, "the level is part of the key");
    TEST_ASSERT(a != c, "x and z are not interchangeable");
    TEST_ASSERT(a != d, "neighbouring tiles differ");
    TEST_ASSERT(
        TileMarkStage_Key(511, 511, 3) == ((511 * 512) + 511) * 4 + 3,
        "the last tile and level pack exactly");
    TEST_ASSERT(TileMarkStage_Key(0, 0, 0) == 0, "the first packs to zero");
}

static void
test_a_push_carries_the_stage_clip_and_tile(void)
{
    struct UITreeEntityOverlay const item = a_point(42);
    struct ToriRS_WorldTileMark const* marks;
    int const key = TileMarkStage_Key(3, 4, 1);

    TileMarkStage_Reset(&g_stage);
    TileMarkStage_Begin(&g_stage, key, 11, 12, 13, 14);
    TEST_ASSERT(TileMarkStage_IsOpen(&g_stage), "begin opens the stage");
    TEST_ASSERT(TileMarkStage_Push(&g_stage, &item), "an open stage keeps a push");
    TileMarkStage_End(&g_stage);
    TEST_ASSERT(!TileMarkStage_IsOpen(&g_stage), "end closes it");

    marks = TileMarkStage_Marks(&g_stage);
    TEST_ASSERT(TileMarkStage_Count(&g_stage) == 1, "one mark kept");
    TEST_ASSERT(marks[0].item.x == 42, "with what was pushed");
    TEST_ASSERT(marks[0].item.clip_x == 11, "clipped to the stage's viewport, x");
    TEST_ASSERT(marks[0].item.clip_y == 12, "y");
    TEST_ASSERT(marks[0].item.clip_w == 13, "w");
    TEST_ASSERT(marks[0].item.clip_h == 14, "h");
    TEST_ASSERT(
        marks[0].after_command == TILE_MARK_STAGE_UNPLACED, "and unplaced until a placement");
    TEST_ASSERT(g_stage.keys[0] == key, "tagged with the open tile");
}

static void
test_a_full_stage_refuses_and_says_so(void)
{
    struct UITreeEntityOverlay const item = a_point(1);
    int kept = 0;

    TileMarkStage_Reset(&g_stage);
    TileMarkStage_Begin(&g_stage, 0, 0, 0, 1, 1);
    for( int i = 0; i < TILE_MARK_STAGE_MAX; i++ )
        kept += TileMarkStage_Push(&g_stage, &item) ? 1 : 0;
    TEST_ASSERT(kept == TILE_MARK_STAGE_MAX, "the stage holds its whole budget");
    TEST_ASSERT(g_stage.overflow == 0, "without overflowing");
    TEST_ASSERT(!TileMarkStage_Push(&g_stage, &item), "one more is refused");
    TileMarkStage_End(&g_stage);
    TEST_ASSERT(g_stage.overflow == 1, "and the refusal is remembered");
    TEST_ASSERT(TileMarkStage_Count(&g_stage) == TILE_MARK_STAGE_MAX, "not wrapped");
}

static void
test_placement_takes_the_last_command_of_a_tile(void)
{
    struct TileMarkPlacement placement;
    int const tile = TileMarkStage_Key(7, 8, 0);
    int const other = TileMarkStage_Key(7, 8, 1);
    struct ToriRS_WorldTileMark const* marks;

    TileMarkStage_Reset(&g_stage);
    push_marker(&g_stage, tile, 100, 3);
    push_marker(&g_stage, tile, 200, 2);

    TEST_ASSERT(
        TileMarkStage_PlaceBegin(&g_stage, &placement) == 1, "two markers on one tile are one tile");
    /* A bridge: the ground below, the deck at another level, then this tile's
     * own ground again later in the paint. */
    TileMarkPlacement_Saw(&placement, 5, tile);
    TileMarkPlacement_Saw(&placement, 9, other);
    TileMarkPlacement_Saw(&placement, 17, tile);
    TileMarkStage_PlaceFinish(&g_stage, &placement);

    marks = TileMarkStage_Marks(&g_stage);
    for( int i = 0; i < TileMarkStage_Count(&g_stage); i++ )
        TEST_ASSERT(marks[i].after_command == 17, "every primitive follows the LAST command");
    TEST_ASSERT(g_stage.unplaced == 0, "nothing is unplaced");
}

static void
test_placement_sorts_stably_and_counts_the_unplaced(void)
{
    struct TileMarkPlacement placement;
    int const late = TileMarkStage_Key(1, 1, 0);
    int const gone = TileMarkStage_Key(2, 2, 0);
    int const early = TileMarkStage_Key(3, 3, 0);
    struct ToriRS_WorldTileMark const* marks;
    int expect_x[] = { 300, 301, 302, 100, 101, 102, 200, 201 };

    TileMarkStage_Reset(&g_stage);
    push_marker(&g_stage, late, 100, 3);
    push_marker(&g_stage, gone, 200, 2);
    push_marker(&g_stage, early, 300, 3);

    TEST_ASSERT(TileMarkStage_PlaceBegin(&g_stage, &placement) == 3, "three distinct tiles");
    TileMarkPlacement_Saw(&placement, 4, early);
    TileMarkPlacement_Saw(&placement, 6, TileMarkStage_Key(9, 9, 0));
    TileMarkPlacement_Saw(&placement, 12, late);
    TileMarkStage_PlaceFinish(&g_stage, &placement);

    marks = TileMarkStage_Marks(&g_stage);
    TEST_ASSERT(TileMarkStage_Count(&g_stage) == 8, "nothing is dropped");
    for( int i = 0; i < 8; i++ )
        TEST_ASSERT(marks[i].item.x == expect_x[i], "sorted by command, each run kept in order");
    TEST_ASSERT(marks[0].after_command == 4, "the early tile first");
    TEST_ASSERT(marks[3].after_command == 12, "then the late one");
    TEST_ASSERT(marks[6].after_command == TILE_MARK_STAGE_UNPLACED, "the undrawn tile last");
    TEST_ASSERT(g_stage.keys[0] == early, "the keys travel with their marks");
    TEST_ASSERT(g_stage.keys[7] == gone, "to the end");
    TEST_ASSERT(g_stage.unplaced == 2, "and its primitives are counted");
}

static void
test_an_empty_stage_has_nothing_to_place(void)
{
    struct TileMarkPlacement placement;

    TileMarkStage_Reset(&g_stage);
    TEST_ASSERT(
        TileMarkStage_PlaceBegin(&g_stage, &placement) == 0, "no marks means no paint walk");
}

int
main(void)
{
    test_a_reset_stage_is_empty_and_closed();
    test_keys_are_distinct_per_tile_and_level();
    test_a_push_carries_the_stage_clip_and_tile();
    test_a_full_stage_refuses_and_says_so();
    test_placement_takes_the_last_command_of_a_tile();
    test_placement_sorts_stably_and_counts_the_unplaced();
    test_an_empty_stage_has_nothing_to_place();

    if( g_failures )
    {
        printf("tile_mark_stage: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("tile_mark_stage: all checks passed\n");
    return 0;
}
