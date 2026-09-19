/*
 * Drag to pan the world map.
 *
 * Three properties here are the kind that are only visible after a long drag
 * or a wrong click, which is exactly why they were worth getting out of the
 * frame loop and into a test.
 *
 *   - the pan is ANCHORED to where the view was when the drag was grabbed, not
 *     re-based every frame. Re-basing truncates the sub-tile remainder once
 *     per FRAME, so a slow drag of single-pixel steps converts each one to
 *     zero tiles and the map does not move at all, while the same distance
 *     covered in one jump moves normally.
 *
 *     Worth being exact about which variant is wrong, because one plausible
 *     reading is not: re-anchoring only on a frame that actually moved is
 *     equivalent to anchoring, since that frame's remainder is zero by
 *     construction. It is re-basing on EVERY frame that loses ground. Both
 *     were tried as mutations; only the second fails this test, which is how
 *     the distinction got written down.
 *   - a press that releases without ever panning is a CLICK, and a drag that
 *     moved is not also a click. The server decides what a map click means
 *     (the reference's ClickWorldMap), so reporting one that did not happen
 *     sends a teleport request nobody asked for.
 *   - the map's chrome sits INSIDE the surface box. The close button, the key
 *     panel, the search field and the zoom buttons are all inside it, so "the
 *     pointer is in the box" is not "the pointer is on the map". Getting this
 *     wrong meant closing the map also teleported the player to whatever tile
 *     the close button happened to be drawn over.
 *
 * Build and run:
 *   make -C src test-worldmap-drag
 */

#include "game/rs_worldmap_drag.h"

#include <stdio.h>
#include <string.h>

#define BOX_X 20
#define BOX_Y 30
#define BOX_W 500
#define BOX_H 400

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static struct UIWorldMapDrag
fresh_drag(void)
{
    struct UIWorldMapDrag drag;

    memset(&drag, 0, sizeof(drag));
    drag.box_x = BOX_X;
    drag.box_y = BOX_Y;
    drag.box_w = BOX_W;
    drag.box_h = BOX_H;
    return drag;
}

/* The pointer over bare map, with nothing else claiming the press. */
static struct UIWorldMapDragInput
pointer_at(
    int mouse_x,
    int mouse_y)
{
    struct UIWorldMapDragInput input;

    memset(&input, 0, sizeof(input));
    input.mouse_x = mouse_x;
    input.mouse_y = mouse_y;
    input.hover_component_id = -1;
    input.surface_live = 1;
    return input;
}

/*
 * A map at a zoom where ONE PIXEL IS A FRACTION OF A TILE, which is the whole
 * point: at zoom 100 the scale is 1024/256, so a single-pixel step converts to
 * 256/1024 = 0 tiles once truncated. That is what makes an accumulated pan
 * lose ground and an anchored one not -- and at the default zoom, where a
 * pixel is exactly one tile, the two are indistinguishable and this test
 * proves nothing. (It did not, until a mutation run said so.)
 */
static struct RS_WorldMapState
map_at(
    int display_x,
    int display_y)
{
    struct RS_WorldMapState map;

    memset(&map, 0, sizeof(map));
    RS_WorldMap_Init(&map);
    RS_WorldMap_SetZoomInstant(&map, 100);
    RS_WorldMap_SetDisplayPosition(&map, display_x, display_y);
    return map;
}

static void
display_of(
    struct RS_WorldMapState const* map,
    int* out_x,
    int* out_y)
{
    *out_x = 0;
    *out_y = 0;
    RS_WorldMap_DisplayPosition(map, out_x, out_y);
}

static void
test_the_pan_is_anchored_not_accumulated(void)
{
    struct UIWorldMapDrag drag = fresh_drag();
    struct RS_WorldMapState map = map_at(3200, 3200);
    struct UIWorldMapDragInput input;
    int display_x;
    int display_y;
    int scale_fp = RS_WorldMap_ZoomScaleFp(&map);

    if( scale_fp <= 0 )
        scale_fp = RS_WORLDMAP_ZOOM_SCALE_ONE;

    /* Grab in the middle of the surface. */
    input = pointer_at(200, 200);
    input.left_down = 1;
    input.left_held = 1;
    UIWorldMapDrag_Tick(&drag, &input, &map);
    CHECK(drag.active, "the press did not start a drag");
    CHECK(drag.origin_x == 3200 && drag.origin_y == 3200, "the anchor is %d,%d", drag.origin_x, drag.origin_y);

    CHECK(
        scale_fp > RS_WORLDMAP_ZOOM_SCALE_ONE,
        "the fixture's zoom gives scale_fp %d; at or below %d a pixel is a whole "
        "tile and this test cannot tell anchored from accumulated",
        scale_fp,
        RS_WORLDMAP_ZOOM_SCALE_ONE);

    /*
     * Drag one pixel at a time to a total of 7. Each single step converts to
     * ZERO tiles once truncated, so an ACCUMULATED pan never moves at all. An
     * anchored one computes the whole 7-pixel delta against the grab and moves
     * a tile.
     */
    for( int step = 1; step <= 7; step++ )
    {
        input = pointer_at(200 + step, 200);
        input.left_held = 1;
        UIWorldMapDrag_Tick(&drag, &input, &map);
    }
    display_of(&map, &display_x, &display_y);
    CHECK(
        display_x == 3200 - 7 * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp,
        "after seven single-pixel steps x is %d, want the whole-delta answer %d",
        display_x,
        3200 - 7 * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp);

    /* And going straight there in one step must agree exactly. */
    {
        struct UIWorldMapDrag jump_drag = fresh_drag();
        struct RS_WorldMapState jump_map = map_at(3200, 3200);
        int jump_x;
        int jump_y;

        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        UIWorldMapDrag_Tick(&jump_drag, &input, &jump_map);
        input = pointer_at(207, 200);
        input.left_held = 1;
        UIWorldMapDrag_Tick(&jump_drag, &input, &jump_map);
        display_of(&jump_map, &jump_x, &jump_y);
        CHECK(
            jump_x == display_x && jump_y == display_y,
            "one 7px step gave %d,%d but seven 1px steps gave %d,%d",
            jump_x,
            jump_y,
            display_x,
            display_y);
    }

    /* The map moves OPPOSITE the pointer in x and WITH it in y, because screen
     * y grows downward and map y northward. Getting either sign wrong makes
     * the map fly away from the cursor rather than follow it. */
    {
        struct UIWorldMapDrag sign_drag = fresh_drag();
        struct RS_WorldMapState sign_map = map_at(3200, 3200);
        int sign_x;
        int sign_y;

        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        UIWorldMapDrag_Tick(&sign_drag, &input, &sign_map);
        input = pointer_at(300, 300);
        input.left_held = 1;
        UIWorldMapDrag_Tick(&sign_drag, &input, &sign_map);
        display_of(&sign_map, &sign_x, &sign_y);
        CHECK(sign_x < 3200, "dragging right moved the view east (x %d)", sign_x);
        CHECK(sign_y > 3200, "dragging down moved the view south (y %d)", sign_y);
    }
}

static void
test_a_release_without_panning_is_a_click(void)
{
    struct UIWorldMapDrag drag = fresh_drag();
    struct RS_WorldMapState map = map_at(3200, 3200);
    struct UIWorldMapDragInput input;

    /* Press and release at the same point. */
    input = pointer_at(200, 200);
    input.left_down = 1;
    input.left_held = 1;
    CHECK(
        UIWorldMapDrag_Tick(&drag, &input, &map) == UI_WORLDMAP_DRAG_NONE,
        "the press itself reported something");

    input = pointer_at(200, 200);
    input.left_up = 1;
    CHECK(
        UIWorldMapDrag_Tick(&drag, &input, &map) == UI_WORLDMAP_DRAG_CLICKED,
        "a press released in place was not a click");
    CHECK(!drag.active, "the drag stayed active after release");

    /* A drag that moved the view is NOT also a click. */
    drag = fresh_drag();
    map = map_at(3200, 3200);
    input = pointer_at(200, 200);
    input.left_down = 1;
    input.left_held = 1;
    UIWorldMapDrag_Tick(&drag, &input, &map);
    input = pointer_at(400, 200);
    input.left_held = 1;
    CHECK(
        UIWorldMapDrag_Tick(&drag, &input, &map) == UI_WORLDMAP_DRAG_PANNED,
        "a 200px drag did not pan");
    input = pointer_at(400, 200);
    input.left_up = 1;
    CHECK(
        UIWorldMapDrag_Tick(&drag, &input, &map) == UI_WORLDMAP_DRAG_CLICKED ? 0 : 1,
        "a drag that panned was also reported as a click");

    /* The surface disappearing mid-drag drops it rather than clicking. */
    drag = fresh_drag();
    map = map_at(3200, 3200);
    input = pointer_at(200, 200);
    input.left_down = 1;
    input.left_held = 1;
    UIWorldMapDrag_Tick(&drag, &input, &map);
    input = pointer_at(200, 200);
    input.left_held = 1;
    input.surface_live = 0;
    CHECK(
        UIWorldMapDrag_Tick(&drag, &input, &map) == UI_WORLDMAP_DRAG_NONE,
        "a vanished surface produced a click");
    CHECK(!drag.active, "a vanished surface left the drag active");
}

static void
test_the_maps_own_chrome_keeps_its_clicks(void)
{
    struct RS_WorldMapState map = map_at(3200, 3200);
    struct UIWorldMapDragInput input;

    /* Over a component -- the close button, the zoom buttons, the key panel.
     * All of them are inside the surface box. */
    {
        struct UIWorldMapDrag drag = fresh_drag();

        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        input.hover_component_id = 12345;
        UIWorldMapDrag_Tick(&drag, &input, &map);
        CHECK(!drag.active, "a press on the map's chrome started a pan");
    }

    /* Something earlier in the frame already took the press. */
    {
        struct UIWorldMapDrag drag = fresh_drag();

        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        input.pointer_consumed = 1;
        UIWorldMapDrag_Tick(&drag, &input, &map);
        CHECK(!drag.active, "a consumed press started a pan");
    }

    /* The right-click menu is open over the map. */
    {
        struct UIWorldMapDrag drag = fresh_drag();

        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        input.minimenu_visible = 1;
        UIWorldMapDrag_Tick(&drag, &input, &map);
        CHECK(!drag.active, "a press under an open minimenu started a pan");
    }

    /* Outside the box, on each edge. The box is half-open: its first pixel is
     * inside and the one past its width is not. */
    {
        struct UIWorldMapDrag drag = fresh_drag();
        int const outside[][2] = {
            {BOX_X - 1, BOX_Y},
            {BOX_X, BOX_Y - 1},
            {BOX_X + BOX_W, BOX_Y},
            {BOX_X, BOX_Y + BOX_H},
        };

        for( size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++ )
        {
            drag = fresh_drag();
            input = pointer_at(outside[i][0], outside[i][1]);
            input.left_down = 1;
            input.left_held = 1;
            UIWorldMapDrag_Tick(&drag, &input, &map);
            CHECK(!drag.active, "a press at %d,%d (outside) started a pan", outside[i][0], outside[i][1]);
        }

        drag = fresh_drag();
        input = pointer_at(BOX_X, BOX_Y);
        input.left_down = 1;
        input.left_held = 1;
        UIWorldMapDrag_Tick(&drag, &input, &map);
        CHECK(drag.active, "a press on the box's own first pixel did not start a pan");

        drag = fresh_drag();
        input = pointer_at(BOX_X + BOX_W - 1, BOX_Y + BOX_H - 1);
        input.left_down = 1;
        input.left_held = 1;
        UIWorldMapDrag_Tick(&drag, &input, &map);
        CHECK(drag.active, "a press on the box's last pixel did not start a pan");
    }

    /* A surface the emit walk never sized. */
    {
        struct UIWorldMapDrag drag = fresh_drag();

        drag.box_w = 0;
        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        UIWorldMapDrag_Tick(&drag, &input, &map);
        CHECK(!drag.active, "an unsized surface started a pan");
    }

    /* No map state at all -- every frame before WORLDMAP_INIT. */
    {
        struct UIWorldMapDrag drag = fresh_drag();

        input = pointer_at(200, 200);
        input.left_down = 1;
        input.left_held = 1;
        CHECK(
            UIWorldMapDrag_Tick(&drag, &input, NULL) == UI_WORLDMAP_DRAG_NONE,
            "a pan started with no map state");
        CHECK(!drag.active, "a pan with no map state left the drag active");
    }
}

int
main(void)
{
    test_the_pan_is_anchored_not_accumulated();
    test_a_release_without_panning_is_a_click();
    test_the_maps_own_chrome_keeps_its_clicks();

    if( g_failures )
    {
        printf("rs_worldmap_drag_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_worldmap_drag_test: OK\n");
    return 0;
}
