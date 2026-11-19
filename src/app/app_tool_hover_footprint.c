/*
 * The hover footprint outline: a side-app (sideapp/side_app.h).
 *
 * TORIRS_HOVER_FOOTPRINT=1: outline the hovered loc's footprint tiles in red.
 *
 * The painter orders scenery by its FOOTPRINT (size_x x size_z from the loc
 * config, orientation-swapped), while the model draws wherever its vertices
 * land — and nothing on screen says which tiles the painter believed the loc
 * covered. When a model overhangs its footprint, terrain on the overhung
 * tiles legitimately draws later and paints over it, which reads as "the
 * painter is broken" while every ordering rule is being honoured. This makes
 * the footprint visible so model-vs-footprint mismatches are a hover, not an
 * afternoon (the multiloc trap of loc-placement-debug fame).
 *
 * Each footprint tile is outlined at terrain height through the same
 * projector the health bars use, so the outline hugs the contour.
 *
 * The env var picks the mode AND the starting state, so an existing
 * `TORIRS_HOVER_FOOTPRINT=1` run is unchanged and a key bound to
 * `hover_footprint` (CTRL held) merely gains the ability to turn it off.
 * Unset means off but armed at mode 1, which is what the key turns on. A
 * negative or unparsable value is off with nothing to restore. The toggle
 * restores the configured mode instead of a literal 1, so a run started with
 * TORIRS_HOVER_FOOTPRINT=<loc id> keeps outlining that id after an off/on
 * rather than silently downgrading to "the hovered loc". The seed and the
 * toggle are editor/hover_footprint.h's.
 *
 * The key is delivered at the loc editor's point of the frame (its tick,
 * before the BOOTING early-out: after the loc editor's toggle and before the
 * map editor's), with the loc editor's chat suppression: not while a text
 * input has focus, and not a key the map editor claimed this frame. While it
 * is on it asks the pick to see every loc (inspects_scenery), and it draws in
 * the world overlay pass after the bars, splats and chat and before the map
 * editor's selection (app_build_entity_overlays).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "app/app_internal.h"
#include "editor/hover_footprint.h"

struct AppToolHoverFootprint
{
    /** The live mode and the mode the toggle restores. */
    struct HoverFootprint footprint;
};

/* Private to this unit, declared up front so definition order is free. */
static void
app_tool_hover_footprint_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);
static void
app_tool_hover_footprint_world_overlay(
    void* self,
    struct SideAppHost* host);
static int
app_tool_hover_footprint_inspects_scenery(void const* self);
static void
app_tool_hover_footprint_free(void* self);

enum
{
    APP_TOOL_HOVER_FOOTPRINT_TOGGLE
};

static struct SideAppAction const k_app_tool_hover_footprint_actions[] = {
    [APP_TOOL_HOVER_FOOTPRINT_TOGGLE] = { "hover_footprint", SIDE_APP_PHASE_LOC_EDITOR, SIDE_APP_GATE_CTRL,
                                          SIDE_APP_KEY_PRESS, 0 },
};

static struct SideAppHooks const k_app_tool_hover_footprint_hooks = {
    .key = app_tool_hover_footprint_key,
    .free = app_tool_hover_footprint_free,
    .world_overlay = app_tool_hover_footprint_world_overlay,
    .inspects_scenery = app_tool_hover_footprint_inspects_scenery,
};

void
app_tool_hover_footprint_register(struct App* app)
{
    struct AppToolHoverFootprint* tool;

    assert(app);
    assert(app->side_apps);
    tool = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*tool));
    {
        char const* env = getenv("TORIRS_HOVER_FOOTPRINT");
        int mode = (env && env[0]) ? (int)strtol(env, NULL, 0) : 0;
        HoverFootprint_Seed(&tool->footprint, mode);
    }
    SideApps_Register(
        app->side_apps,
        "hover_footprint",
        tool,
        &k_app_tool_hover_footprint_hooks,
        k_app_tool_hover_footprint_actions,
        (int)(sizeof(k_app_tool_hover_footprint_actions) / sizeof(k_app_tool_hover_footprint_actions[0])));
}

/* The toggle key: off, or back on at the mode it restores. */
static void
app_tool_hover_footprint_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct AppToolHoverFootprint* tool = self;
    struct App* app = app_from_side_app_host(host);
    int footprint;

    (void)action;
    (void)args;
    assert(tool);
    assert(action == APP_TOOL_HOVER_FOOTPRINT_TOGGLE);
    assert(args);
    footprint = HoverFootprint_Toggle(&tool->footprint);
    RedrawRequest_Raise(&app->redraw_request);
    TORIRS_LOG("hover_footprint: %d\n", footprint);
}

/* While the outline is on, the pick sees the locs it drops by default: walls,
 * fences, gravel and ground decor are most of what a footprint question is
 * about. */
static int
app_tool_hover_footprint_inspects_scenery(void const* self)
{
    struct AppToolHoverFootprint const* tool = self;

    assert(tool);
    return tool->footprint.live != 0;
}

static void
app_tool_hover_footprint_world_overlay(
    void* self,
    struct SideAppHost* host)
{
    struct AppToolHoverFootprint* tool = self;
    struct App* app = app_from_side_app_host(host);
    /* 0 = off; 1 = the hovered loc; >1 = every instance of that LOC ID.
     * The id form exists for headless runs: TORIRS_SIM_HOVER parks the mouse
     * before the frame loop, so an exit screenshot has no hover to read.
     *
     * The live mode is the tool's state rather than a static resolved once,
     * because the hover_footprint hotkey turns it on and off during a
     * session. The env var still chooses WHICH mode, and the toggle restores
     * it -- see HoverFootprint::mode. */
    int mode;

    assert(tool);
    mode = tool->footprint.live;

    if( !mode || !app->world )
        return;

    if( mode == 1 )
    {
        /*
         * The pickset is this frame's under-mouse set, nearest hits first (the
         * same order the minimenu consumes), so the first loc or npc in it is
         * the one the cursor is actually on.
         *
         * Npcs are outlined as well as locs because an editor is placing both
         * against each other, and "what am I about to act on" is the same
         * question for either. The model outline is the same call for both —
         * they are both scene elements — which is why this does not need to
         * know what kind of entity it found beyond where to read the id.
         */
        struct World_Picked const* hit = NULL;
        for( int i = 0; i < app->world_views.frame->world_pickset.count && !hit; i++ )
        {
            enum World_PickType const type = app->world_views.frame->world_pickset.items[i].type;
            if( type == WORLD_PICK_SCENERY || type == WORLD_PICK_NPC )
                hit = &app->world_views.frame->world_pickset.items[i];
        }
        if( !hit )
            return;

        /* Model silhouette first. It is the outline that reads as "this thing
         * is selected"; the ground footprint below says which TILES it owns,
         * which is what an editor needs when placing something beside it. */
        app_overlay_outline_element_model(app, hit->element_id, APP_OUTLINE_COLOR_HOVER);

        if( hit->type == WORLD_PICK_SCENERY )
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, hit->element_id);
            if( scenery )
                app_overlay_outline_scenery(app, scenery);
        }
        return;
    }

    struct World_EntityPool* pool = &app->world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, i);
        if( scenery && scenery->loc_id == mode )
        {
            /* Both marks, the same pair the hover path draws. The two modes
             * showing different things would make the by-id form useless for
             * checking the hover form — which is what it is for, since a
             * headless run has no cursor to hover with. */
            app_overlay_outline_element_model(app, scenery->element_id, APP_OUTLINE_COLOR_HOVER);
            app_overlay_outline_scenery(app, scenery);
        }
    }
}

static void
app_tool_hover_footprint_free(void* self)
{
    ToriMem_Free(ToriMem_Process(), self);
}
