/*
 * Where an overlay primitive goes.
 *
 * Every push in the client arrives at one function, and the draw window that
 * happens to be open decides which list it lands in. Get that wrong and a
 * plugin's panel drawing is painted across the game world in panel
 * coordinates, or a health bar is drawn above the interfaces instead of under
 * them. Both look like a rendering bug in something else.
 */

#include "render/overlay_stage.h"

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

static struct UITreeEntityOverlay
a_rect(int x)
{
    struct UITreeEntityOverlay item;
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_RECT;
    item.x = x;
    item.y = 7;
    item.w = 20;
    item.h = 3;
    item.color = 0xFF00FF00u;
    return item;
}

static void
test_a_fresh_stage_holds_nothing(void)
{
    struct OverlayStage stage;

    memset(&stage, 0x61, sizeof(stage));
    OverlayStage_Reset(&stage);
    TEST_ASSERT(
        OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 0, "the world list starts empty");
    TEST_ASSERT(
        OverlayStage_Count(&stage, OVERLAY_SURFACE_CANVAS) == 0, "so does the canvas list");
    TEST_ASSERT(!stage.batch_started, "and no batch is open");
    TEST_ASSERT(!stage.canvas_prepared, "and the canvas has not been prepared");
}

static void
test_the_world_is_where_a_push_goes_by_default(void)
{
    struct OverlayStage stage;
    struct UITreeEntityOverlay const item = a_rect(11);

    /* Every built-in overlay -- health bars, hitsplats, overhead chat, the
     * editor's marks -- is built with no draw window open at all, so the
     * default is not a fallback, it is the common case. */
    OverlayStage_Reset(&stage);
    TEST_ASSERT(OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item), "a world push is kept");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 1, "in the world list");
    TEST_ASSERT(
        OverlayStage_Count(&stage, OVERLAY_SURFACE_CANVAS) == 0, "and not in the canvas list");
    TEST_ASSERT(
        OverlayStage_Items(&stage, OVERLAY_SURFACE_WORLD)[0].x == 11, "with what was pushed");
}

static void
test_an_open_canvas_window_takes_the_push(void)
{
    struct OverlayStage stage;
    struct UITreeEntityOverlay const item = a_rect(22);

    OverlayStage_Reset(&stage);
    TEST_ASSERT(OverlayStage_Push(&stage, OVERLAY_SURFACE_CANVAS, &item), "a canvas push is kept");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_CANVAS) == 1, "in the canvas list");
    /* The two are cut to different boxes, and the clip travels on the desc
     * rather than the item -- so a canvas primitive in the world list is drawn
     * clipped to the scene, under the interfaces it was meant to sit above. */
    TEST_ASSERT(
        OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 0, "and not in the world list");
    TEST_ASSERT(
        OverlayStage_Items(&stage, OVERLAY_SURFACE_CANVAS)[0].x == 22, "with what was pushed");
}

static void
test_a_panel_push_lands_in_neither(void)
{
    struct OverlayStage stage;
    struct UITreeEntityOverlay const item = a_rect(33);

    /* The isolation boundary. A panel's drawing is staged elsewhere, and a
     * push that arrives here with a panel window open is one the panel failed
     * to stage. Falling through to the world list would scribble the panel's
     * contents across the scene, positioned in panel coordinates -- which
     * nobody would read as "the panel did not open". */
    OverlayStage_Reset(&stage);
    TEST_ASSERT(!OverlayStage_Push(&stage, OVERLAY_SURFACE_PANEL, &item), "a panel push is refused");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 0, "and reaches the world");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_CANVAS) == 0, "nor the canvas");
    /* And it reports none of its own, rather than some number that happens to
     * be the world's -- a draw verb asks this to report its own cost, and an
     * empty stage cannot tell "none" from "the world's none". So fill the
     * world list first. */
    OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);
    OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);
    OverlayStage_Push(&stage, OVERLAY_SURFACE_CANVAS, &item);
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 2, "the world list is not empty");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_PANEL) == 0, "and the panel holds none of it");
    TEST_ASSERT(OverlayStage_Items(&stage, OVERLAY_SURFACE_PANEL) == NULL, "and offers none");
}

static void
test_the_lists_are_reset_independently(void)
{
    struct OverlayStage stage;
    struct UITreeEntityOverlay const item = a_rect(1);

    /* Two host requests fill them, at different points in the frame. Resetting
     * both from either one would empty a list that has already been built. */
    OverlayStage_Reset(&stage);
    OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);
    OverlayStage_Push(&stage, OVERLAY_SURFACE_CANVAS, &item);

    OverlayStage_ResetWorld(&stage);
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 0, "the world list emptied");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_CANVAS) == 1, "the canvas list did not");

    OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);
    OverlayStage_ResetCanvas(&stage);
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_CANVAS) == 0, "the canvas list emptied");
    TEST_ASSERT(OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == 1, "the world list did not");
}

static void
test_a_frame_reset_closes_the_batch(void)
{
    struct OverlayStage stage;

    OverlayStage_Reset(&stage);
    stage.batch_started = true;
    stage.canvas_prepared = true;
    /* Left open across a frame boundary, the next frame's first BEGIN would
     * take itself for a repeat and reuse a dispatch that belongs to the frame
     * before it -- the canvas would be a frame stale, permanently. */
    OverlayStage_Reset(&stage);
    TEST_ASSERT(!stage.batch_started, "a new frame opens no batch");
    TEST_ASSERT(!stage.canvas_prepared, "and has prepared no canvas");
}

static void
test_each_list_fills_and_stops(void)
{
    struct OverlayStage stage;
    struct UITreeEntityOverlay const item = a_rect(5);
    int i;

    OverlayStage_Reset(&stage);
    for( i = 0; i < OVERLAY_STAGE_WORLD_MAX; i++ )
        TEST_ASSERT(
            OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item), "the world list fills");
    /* Refused, not wrapped: a wrapped push would overwrite the primitives
     * already staged this frame, so a busy scene would lose what it drew
     * first rather than what it drew last. */
    TEST_ASSERT(
        !OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item), "one past full is refused");
    TEST_ASSERT(
        OverlayStage_Count(&stage, OVERLAY_SURFACE_WORLD) == OVERLAY_STAGE_WORLD_MAX,
        "and the list does not grow");
    /* A full world list does not close the canvas one. */
    TEST_ASSERT(
        OverlayStage_Push(&stage, OVERLAY_SURFACE_CANVAS, &item), "the canvas list is unaffected");

    OverlayStage_Reset(&stage);
    for( i = 0; i < OVERLAY_STAGE_CANVAS_MAX; i++ )
        TEST_ASSERT(
            OverlayStage_Push(&stage, OVERLAY_SURFACE_CANVAS, &item), "the canvas list fills");
    TEST_ASSERT(
        !OverlayStage_Push(&stage, OVERLAY_SURFACE_CANVAS, &item), "one past full is refused");
    TEST_ASSERT(
        OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item), "the world list is unaffected");
}

static void
test_the_world_list_has_room_for_polygon_runs(void)
{
    /* A filled polygon is a begin / point... / end RUN, so one highlighted
     * entity costs a dozen entries rather than one. At the canvas list's size
     * the runs starved the outlines that followed them: the buffer filled and
     * every later push was dropped, which on screen reads as a broken outline
     * rather than as a full buffer. Nothing in the canvas list is per-entity,
     * so it needs no such headroom. */
    TEST_ASSERT(
        OVERLAY_STAGE_WORLD_MAX > OVERLAY_STAGE_CANVAS_MAX,
        "the per-entity list is the larger of the two");
}

static void
test_pushes_keep_their_order(void)
{
    struct OverlayStage stage;
    int i;

    /* A polygon is a bracketed run and everything in the list draws in the
     * order it was pushed, so a list that reordered would put an outline's
     * points outside its own begin/end and paint the shape as a scatter of
     * dots. */
    OverlayStage_Reset(&stage);
    for( i = 0; i < 5; i++ )
    {
        struct UITreeEntityOverlay const item = a_rect(i);
        OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);
    }
    for( i = 0; i < 5; i++ )
        TEST_ASSERT(
            OverlayStage_Items(&stage, OVERLAY_SURFACE_WORLD)[i].x == i,
            "every primitive is where it was pushed");
}

static void
test_a_push_copies_the_primitive(void)
{
    struct OverlayStage stage;
    struct UITreeEntityOverlay item = a_rect(9);

    /* The caller builds each primitive in a local and reuses it. A stage that
     * kept the pointer would end the frame with every entry describing the
     * last thing anyone drew. */
    OverlayStage_Reset(&stage);
    snprintf(item.text, sizeof(item.text), "%s", "first");
    OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);
    item.x = 99;
    snprintf(item.text, sizeof(item.text), "%s", "second");
    OverlayStage_Push(&stage, OVERLAY_SURFACE_WORLD, &item);

    TEST_ASSERT(
        OverlayStage_Items(&stage, OVERLAY_SURFACE_WORLD)[0].x == 9, "the first kept its own x");
    TEST_ASSERT(
        strcmp(OverlayStage_Items(&stage, OVERLAY_SURFACE_WORLD)[0].text, "first") == 0,
        "and its own text");
    TEST_ASSERT(
        OverlayStage_Items(&stage, OVERLAY_SURFACE_WORLD)[1].x == 99, "the second kept its own");
}

int
main(void)
{
    test_a_fresh_stage_holds_nothing();
    test_the_world_is_where_a_push_goes_by_default();
    test_an_open_canvas_window_takes_the_push();
    test_a_panel_push_lands_in_neither();
    test_the_lists_are_reset_independently();
    test_a_frame_reset_closes_the_batch();
    test_each_list_fills_and_stops();
    test_the_world_list_has_room_for_polygon_runs();
    test_pushes_keep_their_order();
    test_a_push_copies_the_primitive();

    if( g_failures )
    {
        printf("overlay_stage: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("overlay_stage: all checks passed\n");
    return 0;
}
