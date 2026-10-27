#include "plugin/plugins/nxt_activities.h"
#include "plugin/plugins/nxt_cox_helper.h"
#include "plugin/plugins/nxt_cox_layout.h"
#include "plugin/plugins/plugin_draw.h"
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * All Settings > Activities, row 118: "Chambers of Xeric helper".
 *
 * ---- why this is native ----
 *
 * The cache's raid overlay (interface 513, raids_overlay) is filled by
 * raids_overlay_content (4663), and that script's first act is
 *
 *     if (~torirs_duel_notice = 1 & %varb12347_cox_helper_disabled = 0) {
 *         return;
 *     }
 *
 * -- "an enhanced client, with the helper switched on" (the proc's name is
 * this tree's misnomer; it is clienttype enhanced / 5 / 10, or 7 / 8). This
 * client is clienttype 10, so with the row ticked the cache draws NOTHING and
 * expects the client to. This file is that half: what the C++ client draws.
 *
 * ---- what it draws ----
 *
 * The C++ client's helper is not in any build this tree has decompiled (the
 * wiki marks it "Mobile and Steam clients only"; the rev 216 Mac client and
 * the current Mac build both lack its every string). Its one published
 * picture -- the wiki's Chambers_of_Xeric_helper.png, 140x182 -- is the
 * reference, and every number below is measured off it:
 *
 *   - the plate is graphic_297 (`tradebacking`), tiled from the box's own
 *     corner, at alpha 155: all nine of its colours over black round to the
 *     picture's exactly at 155/255;
 *   - two 1px frames, 0x383023 outside and 0x5a5245 inside -- script1192's,
 *     the CS2 box's own -- opaque;
 *   - p11 (fontmetrics_494), 0xff981f with a black shadow, for every word:
 *     each word's ink width in the picture is p11's;
 *   - 140 wide, 5 in from both sides; one row per room every 12 px, the kind
 *     on the left and "<name> <letter>" right-aligned; then the two point rows
 *     2 px lower, and the time row 2 px lower again beside an 11x11 arrow.
 *
 * The room list comes from the instance's rebuild templates and nothing else
 * (nxt_cox_layout.h); the points and the time from the four vars
 * raids_overlay_databox_1511 reads. The time is counted HERE: the cache
 * advances %varb6386_raids_timer itself from raids_overlay_timer (2289), but
 * that timer is installed by the very script that returned above, so on this
 * client nothing would ever move it.
 */

/** raids_overlay's `data` child, where the CS2 box would have been. */
#define NXT_COX_DATA_CHILD 3
/** %varb5425_raids_client_progress at which the clock stops (the chest). */
#define NXT_COX_PROGRESS_DONE 5

#define NXT_COX_CAPABILITY "varbit:" NXT_VARBIT_COX_HELPER
#define NXT_COX_FEATURE "Chambers of Xeric helper"


extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_COX_HELPER;

struct NxtCoxState
{
    struct Porcelain* porcelain;
    bool available;
    /** raids_overlay:3, or -1 on a profile that does not name the iface. */
    int data_component;

    struct NxtCoxLayout layout;

    /**
     * The clock, in server ticks. `clock_seen` is the var's last value: a
     * change is the server's word (a start, a rejoin) and is taken as it
     * stands, and between changes the clock advances once a tick, as 2289
     * would have advanced the var.
     */
    int clock;
    int clock_seen;

    bool collapsed;
    /** Where the arrow was last drawn, canvas pixels; `described` is the box
     *  the control was last described at, so a move re-describes it. */
    struct ToriRS_Rect arrow;
    bool arrow_drawn;
    struct ToriRS_Rect described;
    bool described_drawn;
    bool described_collapsed;

    /** The last reason the box was not drawn, for TORIRS_COX_HELPER_TRACE. */
    char const* why_not;
    int trace;
    int traced_findings;

    struct PluginDraw_Atlas text;
    struct ToriRS_ImageRef plate_ref;
    uint32_t* plate_px;
    int plate_w;
    int plate_h;
    struct ToriRS_ImageRef arrow_ref;
    uint32_t* arrow_px;
    int arrow_w;
    int arrow_h;
};

/* ---- reading the instance -------------------------------------------- */

/** TORIRS_COX_HELPER_TRACE: the list as the box would print it. */
static void
nxt_cox_trace_rooms(struct NxtCoxState const* state)
{
    int order[NXT_COX_HELPER_MAX_ROOMS];
    int n;

    assert(state);
    if( !state->trace )
        return;
    n = NxtCoxLayout_Order(&state->layout, order, NXT_COX_HELPER_MAX_ROOMS);
    fprintf(stderr, "cox-helper: rooms %d:", n);
    for( int i = 0; i < n; i++ )
    {
        struct NxtCoxCell const* c = &state->layout.cells[order[i]];
        struct NxtCoxRoomKind const* kind = NxtCox_RoomKind(c->kind);
        fprintf(stderr, " %s%s %c@%d,%d,%d", i ? "| " : "", kind->name, NxtCox_VariantLetter(c->variant),
            c->level, c->cell_x, c->cell_z);
    }
    fprintf(stderr, "\n");
}

/** The room kind a zone of the scene was copied from, or -1. */
static int
nxt_cox_zone_kind(
    struct ToriRS_Api* api,
    int level,
    int zone_x,
    int zone_z,
    struct ToriRS_InstanceZone* zone)
{
    if( !api->world.instance_zone(api, level, zone_x, zone_z, zone) )
        return -1;
    return NxtCox_RoomKindOf(zone->template_tile_x, zone->template_tile_z, zone->template_level);
}

/** Forget the remembered raid when the scene's raid start is another one.
 *  Returns true when it did. */
static bool
nxt_cox_forget_other_raid(
    struct ToriRS_Api* api,
    struct NxtCoxState* state,
    int base_tile_x,
    int base_tile_z)
{
    for( int level = 0; level < NXT_COX_MAX_LEVELS; level++ )
    {
        for( int dx = 0; dx < 13; dx++ )
        {
            for( int dz = 0; dz < 13; dz++ )
            {
                struct ToriRS_InstanceZone zone;
                int const zone_x = (base_tile_x >> 3) + dx;
                int const zone_z = (base_tile_z >> 3) + dz;
                int const kind = nxt_cox_zone_kind(api, level, zone_x, zone_z, &zone);

                if( kind < 0 || !NxtCox_RoomIsRaidStart(kind) )
                    continue;
                if( !NxtCoxLayout_IsAnotherRaid(&state->layout, level, zone_x >> 2, zone_z >> 2) )
                    return false;
                NxtCoxLayout_Reset(&state->layout);
                return true;
            }
        }
    }
    return false;
}

/*
 * Every zone of the new scene, asked what it was copied from. Sixteen zones
 * make a room and all sixteen say the same; Note keeps one.
 *
 * A scene with no raid room in it at all is not a raid, and a scene whose
 * raid start is not the remembered one is another raid (a reroll, RSProx
 * 4392: six raids in a row): either way whatever was remembered belonged to
 * the last one and goes.
 */
static void
nxt_cox_scan(struct ToriRS_Api* api, struct NxtCoxState* state, int base_tile_x, int base_tile_z)
{
    int found = 0;
    bool changed;

    assert(api);
    assert(state);
    changed = nxt_cox_forget_other_raid(api, state, base_tile_x, base_tile_z);
    for( int level = 0; level < NXT_COX_MAX_LEVELS; level++ )
    {
        for( int dx = 0; dx < 13; dx++ )
        {
            for( int dz = 0; dz < 13; dz++ )
            {
                struct ToriRS_InstanceZone zone;
                int const zone_x = (base_tile_x >> 3) + dx;
                int const zone_z = (base_tile_z >> 3) + dz;
                int const kind = nxt_cox_zone_kind(api, level, zone_x, zone_z, &zone);

                if( kind < 0 )
                    continue;
                found++;
                /* A room is 32 tiles, four zones, and an instance starts on a
                 * map square: the room's cell is the zone's over four. */
                changed |= NxtCoxLayout_Note(
                    &state->layout,
                    level,
                    zone_x >> 2,
                    zone_z >> 2,
                    kind,
                    NxtCox_RoomVariantOf(zone.template_tile_x),
                    zone.rotation);
            }
        }
    }
    if( found == 0 )
    {
        changed = state->layout.count > 0;
        NxtCoxLayout_Reset(&state->layout);
        state->collapsed = false;
    }
    if( changed )
        nxt_cox_trace_rooms(state);
}

static void
nxt_cox_world_loaded(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_WorldLoadedEvent const* event)
{
    struct NxtCoxState* state = state_ptr;

    assert(api);
    assert(state);
    assert(event);
    if( !state->available )
        return;
    nxt_cox_scan(api, state, event->base_tile_x, event->base_tile_z);
    Porcelain_Invalidate(state->porcelain);
}

/* ---- the tick: clock and floor ---------------------------------------- */

static void
nxt_cox_sample(struct ToriRS_Api* api, void* state_ptr, uint64_t elapsed_ms)
{
    struct NxtCoxState* state = state_ptr;
    struct ToriRS_PlayerSnapshot me;
    int var;

    assert(api);
    assert(state);
    (void)elapsed_ms;

    /*
     * raids_overlay_timer, ported: once a tick while the raid is not done,
     * and only once the clock has been started (`%varb6386 > 0` is
     * raids_overlay_databox's own condition for installing the timer).
     */
    var = Porcelain_SettingValue(state->porcelain, NXT_VARBIT_RAIDS_TIMER, 0);
    if( var != state->clock_seen )
    {
        state->clock_seen = var;
        state->clock = var;
    }
    else if(
        state->clock > 0 &&
        Porcelain_SettingValue(state->porcelain, NXT_VARBIT_RAIDS_PROGRESS, 0) < NXT_COX_PROGRESS_DONE )
    {
        state->clock++;
    }

    /* The floor the player stands on, which is what orders the floors. */
    if( state->layout.count > 0 && api->world.local_player(api, &me) )
    {
        for( int i = 0; i < state->layout.count; i++ )
        {
            struct NxtCoxCell const* c = &state->layout.cells[i];
            if( c->level == me.level && c->cell_x == (me.true_x >> 5) && c->cell_z == (me.true_z >> 5) )
            {
                int const before = state->layout.visited_count;
                NxtCoxLayout_Visit(&state->layout, me.level);
                if( state->layout.visited_count != before )
                    nxt_cox_trace_rooms(state);
                break;
            }
        }
    }
}

static void
nxt_cox_tick(struct ToriRS_Api* api, void* state_ptr, struct ToriRS_TickEvent const* event)
{
    struct NxtCoxState* state = state_ptr;

    assert(api);
    assert(state);
    (void)api;
    (void)event;
    if( !state->available )
        return;
    Porcelain_Tick(state->porcelain, PORCELAIN_SERVER_TICK);
}

/* ---- the draw ---------------------------------------------------------- */

static void
nxt_cox_view(struct NxtCoxState const* state, struct NxtCoxHelperView* view)
{
    int order[NXT_COX_HELPER_MAX_ROOMS];
    int n;

    assert(state);
    assert(view);
    /* Zeroed whole: the view is the derived image's key, padding included. */
    memset(view, 0, sizeof(*view));
    view->collapsed = state->collapsed;
    if( !state->collapsed )
    {
        n = NxtCoxLayout_Order(&state->layout, order, NXT_COX_HELPER_MAX_ROOMS);
        for( int i = 0; i < n; i++ )
        {
            struct NxtCoxCell const* c = &state->layout.cells[order[i]];
            struct NxtCoxRoomKind const* kind = NxtCox_RoomKind(c->kind);
            snprintf(view->rooms[i].kind, sizeof(view->rooms[i].kind), "%s", kind->category);
            snprintf(view->rooms[i].name, sizeof(view->rooms[i].name), "%s", kind->name);
            view->rooms[i].letter = NxtCox_VariantLetter(c->variant);
        }
        view->room_count = n;
    }
    NxtCoxHelper_FormatScore(
        view->total,
        sizeof(view->total),
        Porcelain_SettingValue(state->porcelain, NXT_VARBIT_RAIDS_PARTYSCORE, 0));
    NxtCoxHelper_FormatScore(
        view->yours,
        sizeof(view->yours),
        Porcelain_SettingValue(state->porcelain, "varp:" NXT_VARP_RAIDS_PLAYERSCORE, 0));
    NxtCoxHelper_FormatTime(view->time, sizeof(view->time), state->clock);
}

static bool
nxt_cox_art_ready(struct ToriRS_Api* api, struct NxtCoxState* state)
{
    int ready = 1;

    assert(api);
    assert(state);
    /* All three asked for on the same frame: an image slot is handed out in
     * request order (@see item_stats.c, is_load_art). */
    ready &= PluginDraw_AtlasLoad(api, &state->text, "text") ? 1 : 0;
    ready &= PluginDraw_ImageLoad(
                 api, "plate.png", &state->plate_ref, &state->plate_px, &state->plate_w,
                 &state->plate_h)
                 ? 1
                 : 0;
    /* Wanted, not required: a box without its arrow still reads. */
    (void)PluginDraw_ImageLoad(
        api, "arrow.png", &state->arrow_ref, &state->arrow_px, &state->arrow_w, &state->arrow_h);
    return ready != 0;
}

static bool
nxt_cox_paint(struct ToriRS_Api* api, void* user, uint32_t* argb, int w, int h)
{
    struct NxtCoxState* state = user;
    struct NxtCoxHelperView view;
    struct NxtCoxHelperArt art;

    assert(api);
    assert(state);
    (void)api;
    nxt_cox_view(state, &view);
    art.text = &state->text;
    art.plate_px = state->plate_px;
    art.plate_w = state->plate_w;
    art.plate_h = state->plate_h;
    art.arrow_px = state->arrow_px;
    art.arrow_w = state->arrow_w;
    art.arrow_h = state->arrow_h;
    NxtCoxHelper_Paint(&view, &art, argb, w, h);
    return true;
}

/*
 * TORIRS_COX_HELPER_TRACE=1: say why the box is not up, once per change of
 * reason. A hidden builtin that draws nothing looks exactly like a setting
 * nobody switched on; this is the one line that tells the two apart.
 */
static void
nxt_cox_why(struct NxtCoxState* state, char const* why)
{
    assert(state);
    if( state->why_not == why )
        return;
    state->why_not = why;
    if( state->trace )
        fprintf(stderr, "cox-helper: %s\n", why ? why : "drawn");
}

/** Is the helper up: the setting ticked and the raid overlay mounted. */
static bool
nxt_cox_where(struct ToriRS_Api* api, struct NxtCoxState* state, struct ToriRS_Rect* out)
{
    assert(api);
    assert(state);
    assert(out);
    if( !state->available )
    {
        nxt_cox_why(state, "unavailable: no helper setting in this profile");
        return false;
    }
    if( state->data_component < 0 )
    {
        nxt_cox_why(state, "unavailable: no raids_overlay in this profile");
        return false;
    }
    if( !Porcelain_Setting(state->porcelain, NXT_VARBIT_COX_HELPER, PORCELAIN_SETTING_INVERTED) )
    {
        nxt_cox_why(state, "off: the setting is unticked (the cache draws its own box)");
        return false;
    }
    if( !api->cache.component_rect(api, state->data_component, out) )
    {
        nxt_cox_why(state, "hidden: raids_overlay is not mounted");
        return false;
    }
    return true;
}

static void
nxt_cox_draw_canvas(struct ToriRS_Api* api, void* state_ptr, struct ToriRS_Graphics* draw)
{
    struct NxtCoxState* state = state_ptr;
    struct NxtCoxHelperView view;
    struct ToriRS_Rect at;
    struct ToriRS_ImageRef image;
    enum PorcelainDerivedState derived;
    int h;

    assert(api);
    assert(state);
    assert(draw);
    state->arrow_drawn = false;
    if( !nxt_cox_where(api, state, &at) )
        return;
    if( !nxt_cox_art_ready(api, state) )
    {
        nxt_cox_why(state, "waiting: art not loaded");
        return;
    }

    nxt_cox_view(state, &view);
    h = NxtCoxHelper_Height(&view);
    image = Porcelain_Derived(
        state->porcelain,
        "cox_helper.png",
        &view,
        sizeof(view),
        NxtCoxHelper_Width(),
        h,
        nxt_cox_paint,
        state,
        &derived);
    if( derived != PORCELAIN_DERIVED_READY )
    {
        nxt_cox_why(state, derived == PORCELAIN_DERIVED_FAILED ? "failed: the paint" : "waiting: the paint");
        return;
    }
    draw->image(draw, image, at.x, at.y, 255);
    nxt_cox_why(state, NULL);

    NxtCoxHelper_ArrowRect(&view, &state->arrow);
    state->arrow.x += at.x;
    state->arrow.y += at.y;
    state->arrow_drawn = true;
}

/* ---- the arrow -------------------------------------------------------- */

/*
 * The arrow is the box's one control. The C++ client's LUI takes the click on
 * its own widget before the game's menu does; here that is an owned control
 * over the arrow -- a plugin MENU ROW would sort below "Walk here" (the
 * minimenu ranks every plugin action under the lane's own), so a left click
 * would walk. It wears the 1x1 blank, because a control takes its size
 * through its picture (@see mobile_gameframe.c's tap blockers).
 *
 * The op's wording is not published; these two say what it does.
 */
static void
nxt_cox_toggle(struct ToriRS_Api* api, void* user, char const* key)
{
    struct NxtCoxState* state = user;

    assert(api);
    assert(state);
    assert(key);
    (void)api;
    (void)key;
    state->collapsed = !state->collapsed;
    if( state->trace )
        fprintf(stderr, "cox-helper: %s\n", state->collapsed ? "collapsed" : "expanded");
}

static void
nxt_cox_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct NxtCoxState* state = user;
    struct PorcelainItem item;

    assert(describe);
    assert(state);
    state->described = state->arrow;
    state->described_drawn = state->arrow_drawn;
    if( !state->arrow_drawn )
        return;
    memset(&item, 0, sizeof(item));
    item.key = "arrow";
    item.image = "blank.png";
    item.w = state->arrow.width;
    item.h = state->arrow.height;
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.place.dx = state->arrow.x;
    item.place.dy = state->arrow.y;
    /* Over the viewport, which is also what gives a canvas control its
     * parent: the frame root is walked up to from an element the plugin
     * watches, and without one the control defers, silently, forever. */
    item.place.depth = PORCELAIN_EL(VIEWPORT);
    item.op_label = state->collapsed ? "Expand" : "Collapse";
    item.on_op = nxt_cox_toggle;
    item.user = state;
    item.hit = true;
    item.enabled = true;
    describe->control(describe, &item);
    if( state->trace )
        fprintf(stderr, "cox-helper: arrow control at %d,%d %dx%d (%s)\n", item.place.dx, item.place.dy,
            item.w, item.h, item.op_label);
}

static void
nxt_cox_frame(struct ToriRS_Api* api, void* state_ptr, struct ToriRS_FrameEvent const* event)
{
    struct NxtCoxState* state = state_ptr;

    assert(api);
    assert(state);
    (void)event;
    if( !state->available )
        return;
    if( state->described_drawn != state->arrow_drawn ||
        (state->arrow_drawn && memcmp(&state->described, &state->arrow, sizeof(state->arrow)) != 0) ||
        state->collapsed != state->described_collapsed )
    {
        state->described_collapsed = state->collapsed;
        Porcelain_Invalidate(state->porcelain);
    }
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
    if( state->trace )
    {
        struct PorcelainFinding found[8];
        int const n = Porcelain_Findings(state->porcelain, found, 8);
        if( n != state->traced_findings )
        {
            for( int i = state->traced_findings; i < n && i < 8; i++ )
                fprintf(stderr, "cox-helper: finding %s result=%d %s\n", found[i].verb ? found[i].verb : "?",
                    found[i].result, found[i].detail ? found[i].detail : "");
            state->traced_findings = n;
        }
    }
}

/* ---- lifecycle -------------------------------------------------------- */

static void
nxt_cox_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtCoxState* state = state_ptr;
    int iface = -1;
    int base_x = 0;
    int base_z = 0;

    assert(api);
    assert(state);
    memset(state, 0, sizeof(*state));
    state->data_component = -1;
    state->trace = getenv("TORIRS_COX_HELPER_TRACE") != NULL;
    state->why_not = "";
    NxtCoxLayout_Reset(&state->layout);
    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_NXT_COX_HELPER, state);
    assert(state->porcelain);

    if( !Porcelain_Has(state->porcelain, NXT_COX_CAPABILITY) )
        Porcelain_ExpectUnsupported(state->porcelain, NXT_COX_FEATURE,
            "this revision declares no Chambers of Xeric helper setting");
    state->available = Porcelain_Require(state->porcelain, NXT_COX_CAPABILITY, NXT_COX_FEATURE);
    if( !state->available )
        return;
    if( api->cache.named_id(api, "iface", NXT_IFACE_RAIDS_OVERLAY, &iface) && iface >= 0 )
        state->data_component = (iface << 16) | NXT_COX_DATA_CHILD;
    Porcelain_EveryServerTick(state->porcelain, nxt_cox_sample, state);
    Porcelain_Describe(state->porcelain, nxt_cox_describe, state);

    /* Enabled inside a raid: the scene is already there. */
    if( api->world.scene_origin(api, &base_x, &base_z) )
        nxt_cox_scan(api, state, base_x, base_z);
}

static void
nxt_cox_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct NxtCoxState* state = state_ptr;

    assert(api);
    assert(state);
    PluginDraw_AtlasFree(api, &state->text);
    PluginDraw_ImageFree(api, &state->plate_px, &state->plate_ref);
    PluginDraw_ImageFree(api, &state->arrow_px, &state->arrow_ref);
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_COX_HELPER = {
    .struct_size = sizeof(TORIRS_PLUGIN_NXT_COX_HELPER),
    .id = "nxt-cox-helper",
    .title = "Chambers of Xeric helper (All Settings)",
    .version = "1.0.0",
    .state_size = sizeof(struct NxtCoxState),
    .config = NULL,
    .flags = TORIRS_PLUGIN_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = nxt_cox_start,
        .on_stop = nxt_cox_stop,
        .on_server_tick = nxt_cox_tick,
        .on_world_loaded = nxt_cox_world_loaded,
        .on_frame_start = nxt_cox_frame,
        .on_draw_canvas = nxt_cox_draw_canvas,
    },
};
