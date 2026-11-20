/*
 * The real client's half of a server packet.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 *
 * For each packet, in this order: the model (RS_ServerState_Apply, the one
 * copy of the rules both clients run), then what only this client does with
 * it -- tree pokes, CS2 transmits and hooks, redraws, client scripts, audio,
 * and the parallel tasks and asset wants for what must not hold the packet
 * pump. The families whose record still lives in the scene or the tree
 * (zones, locs, ground items, projectiles, interface mounts, world entities)
 * are applied here whole. The arms that write only a tree are the game
 * half's (game/rs_gameproto_exec.c, RS_GameProto_PresentTree), called from
 * here; a client with no App runs that half alone.
 *
 * The App is required (fa5680f77): every field of the context is asserted once
 * on entry (app_exec_ctx) and no arm tests one. app->world, ->scene and
 * ->provider being NULL are real states and keep their guards. Every arm of
 * the packet switch (App_GameProto_Exec) and of the zone sub-packet switch
 * (exec_zone_sub_packet_at) is one call to its own static exec_ function.
 */

#include "app/app_internal.h"
#include "app/app_gameproto_exec.h"
#include "torirs_env.h"

#include "inv/inv_manager.h"
#include "net/jbase37.h"
#include "net/login/login_osrs.h"
#include "net/net.h"
#include "net/rev/revpacket.h"
#include "net/torirs_control.h"
#include "app/app_replay_control.h"
#include "game/rs_attack_option.h"
#include "game/rs_audio.h"
#include "game/rs_chat.h"
#include "game/rs_clan.h"
#include "game/rs_friends_chat.h"
#include "game/rs_clientcode.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_cs2_host.h"
#include "game/rs_entity_sync.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_player_stats.h"
#include "game/rs_server_state.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "game/task_entity_assets.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* One packet's context inside this file: the model's records, the tree the
 * tree-only arms write, the App, and what the model made of the packet. Every
 * field is required: app_exec_ctx asserts each once, and no arm tests one. */
struct AppGameProtoCtx
{
    struct RS_ServerState const* state;
    struct UITree* tree;
    struct App* app;
    /** RS_ServerState_Apply's answer for this packet (PASS for a zone op
     *  replayed from a hold or a keyframe: its model already ran). */
    enum RS_ServerStateApply modelled;
};

/* The context every entry of this file runs a packet under. */
static struct AppGameProtoCtx
app_exec_ctx(
    struct App* app,
    struct RS_ServerState const* state)
{
    struct AppGameProtoCtx ctx;

    assert(app);
    assert(state);
    assert(app->tree);
    ctx.state = state;
    ctx.tree = app->tree;
    ctx.app = app;
    ctx.modelled = RS_SERVER_STATE_PASS;
    return ctx;
}

/*
 * Put a line in the chatbox.
 *
 * Through the CS2 host, because a message is two things and
 * both have to happen: it goes in the store, and the chat-transmit channel
 * has to say so, or the cache's `[proc,rebuildchatbox]` never runs and the
 * line sits in a store nothing draws. The host also owns the client clock the
 * node is stamped with.
 */
static void
exec_chat_add(
    struct AppGameProtoCtx const* ctx,
    int type,
    char const* name,
    char const* sender,
    char const* text)
{
    assert(ctx);
    assert(ctx->state->chat);
    RS_CS2Host_ChatAdd(&ctx->app->host, type, name, sender, text);
}

/* The presentation half of a chat line the model already stored
 * (RS_ServerState_Apply): the chat-transmit channel has to say so, or the
 * cache's `[proc,rebuildchatbox]` never runs and the line sits in a store
 * nothing draws. */
static void
exec_chat_presented(struct AppGameProtoCtx const* ctx)
{
    assert(ctx);
    RS_CS2Host_NotifyChatAdded(&ctx->app->host);
}

/*
 * One camera packet, recorded for the READ side (render/cam_script.h,
 * CamScript_Record). The draw path never reads this; the quest driver does
 * (t.world.camera, t.cutscene.await), so a cutscene a quest scripts can be
 * asserted rather than only survived (seam32 cutscene_verb_and_camera_read).
 *
 * `world_x`/`world_z` are WORLD tiles, resolved by the caller against the
 * scene the packet arrived in: a later rebuild moves the scene base, and a
 * scene-local tile read back after it would name the wrong place.
 */
static void
exec_cam_script_record(
    struct App* app,
    enum CamScriptOp kind,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2)
{
    assert(app);
    CamScript_Record(
        &app->cam_script,
        kind,
        world_x,
        world_z,
        height,
        rate,
        rate2,
        app->world ? app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES : 0);
}

/* The local player's plane is the fallback for classic zone headers which do
 * not carry a plane. Revision 239 does carry one and must not use this path:
 * its zone stream includes level-1 scenery while the player remains on level 0. */
static int
zone_player_level(struct App* app)
{
    int world_idx;
    int pid = app->esync.local_pid >= 0 ? app->esync.local_pid : 2047;
    if( RS_EntitySync_FindPlayer(&app->esync, pid, &world_idx, NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&app->world->entities.player, world_idx);
        if( player )
            return player->grid_position.level;
    }
    return 0;
}

/* A zone header arrived: the base and plane later sub-packets read. */
static void
zone_set_base(
    struct App* app,
    int base_x,
    int base_z,
    int level)
{
    app->zone_base.base_x = base_x;
    app->zone_base.base_z = base_z;
    app->zone_base.level = level;
}

static int
zone_header_level(struct App* app, int header_level)
{
    if( header_level >= 0 )
        return header_level;
    return zone_player_level(app);
}

/* TRIGGER_ONDIALOGABORT is a broadcast over the currently active interface
 * tree. Closed packs remain allocated for reuse, so hidden ancestry is the
 * boundary between active listeners and stale registrations. */
static void
exec_trigger_on_dialog_abort(struct AppGameProtoCtx const* ctx)
{
    struct App* app;

    assert(ctx);
    assert(ctx->app);
    assert(ctx->tree);
    app = ctx->app;
    /* The reference runs the listeners inline, at this packet (class415
     * method9509 -> Statics.method6590, runScript at once): after every
     * RUNCLIENTSCRIPT the tick sent before it, before every packet after it.
     * So the held scripts go first and the listeners follow them on THIS
     * pipeline, where the next packet waits for both; on the parallel queue
     * they ran whenever it was next stepped, behind later packets (U9). */
    App_FlushPendingClientScripts(app, &app->packet_runner);
    UITREE_SCAN_METER(ctx->tree);
    for( uint32_t i = 0; i < ctx->tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &ctx->tree->components[i];
        struct UITreeRuntimeScriptHook hook;
        if( node->freed || node->component_id < 0 )
            continue;
        hook = UITree_Hooks(node)->on_dialog_abort;
        if( hook.script_id <= 0 )
            continue;
        if( UITree_ComponentOrAncestorHidden(ctx->tree, node->component_id) )
            continue;
        RS_CS2_DispatchHook(&app->host, &app->packet_runner, node->component_id, &hook);
    }
    RedrawRequest_Raise(&app->redraw_request);
}

/* Where a zone sub-packet applies: base + packed nibbles, at `level`, in the
 * world view `view`. All of it is captured by the caller — live packets use
 * the app's current state, replayed ones the state as of arrival (a queued
 * packet outlives the SET_ACTIVE_WORLD that addressed it; the tick fence
 * resets the live cursor). */
struct ZoneAt
{
    int base_x;
    int base_z;
    int level;
    int view;
};

/*
 * The south-west tile, in absolute coordinates of the view's own frame, of the
 * scene a packet for `view` addresses: what an absolute coord the packet
 * carries is measured against. The root's is the accepted REBUILD's scene
 * (also while that scene is still being built, so a packet held for it is
 * placed in the scene it was sent for, not the one being torn down); a deck's
 * is its own world's.
 */
static void
zone_view_origin(
    struct App* app,
    int view,
    int* out_x,
    int* out_z)
{
    struct World const* world;

    assert(app);
    assert(out_x);
    assert(out_z);
    if( view == WORLDVIEW_ROOT )
    {
        *out_x = RS_RebuildRegion_SceneBaseX(&app->rebuild_region);
        *out_z = RS_RebuildRegion_SceneBaseZ(&app->rebuild_region);
        return;
    }
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    assert(world);
    *out_x = world->_base_tile_x;
    *out_z = world->_base_tile_z;
}

static void
zone_tile_at(
    struct ZoneAt const* at,
    int pos,
    int* out_x,
    int* out_z,
    int* out_level)
{
    *out_x = at->base_x + ((pos >> 4) & 7);
    *out_z = at->base_z + (pos & 7);
    *out_level = at->level;
}

/*
 * Same tile, but the level a *loc* packet means.
 *
 * A zone names the plane the player walks. On a bridge column that is one below
 * the plane the map authored the deck's locs on, and the scene keeps them where
 * the map put them (the push-down moves the painter tile, not the loc). So a
 * LOC_ADD_CHANGE arriving for a deck has to climb back to the cache level or it
 * cannot find the loc it is replacing — it spawns a second one on the plane
 * below instead, which is two chests inside each other in the QBD reward room.
 * Entities do not need this: their levels are walked levels all the way to the
 * painter, and app_world_height makes the same climb for their heights.
 */
static void
zone_loc_tile_at(
    struct App* app,
    struct ZoneAt const* at,
    int pos,
    int* out_x,
    int* out_z,
    int* out_level)
{
    zone_tile_at(at, pos, out_x, out_z, out_level);
    *out_level = World_LocCacheLevel(
        App_ActiveWorldview(app)->world, *out_x, *out_z, *out_level);
}

/* Copy one zone sub-packet payload into a PktZoneSubPacket by name. Returns 0
 * for a name that is not a zone sub-packet (nothing to queue). */
static int
zone_sub_packet_copy(
    struct PktZoneSubPacket* dst,
    enum GameProtoPktName name,
    void const* payload)
{
    dst->name = name;
    switch( name )
    {
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        /* The reset is the header itself: its zone and plane are the hold's
         * base and level. */
        return 1;
    case PKT_NAME_OBJ_ADD:
        dst->_obj_add = *(struct PktObjAdd const*)payload;
        return 1;
    case PKT_NAME_OBJ_DEL:
        dst->_obj_del = *(struct PktObjDel const*)payload;
        return 1;
    case PKT_NAME_OBJ_COUNT:
        dst->_obj_count = *(struct PktObjCount const*)payload;
        return 1;
    case PKT_NAME_OBJ_REVEAL:
        dst->_obj_reveal = *(struct PktObjReveal const*)payload;
        return 1;
    case PKT_NAME_LOC_DEL:
        dst->_loc_del = *(struct PktLocDel const*)payload;
        return 1;
    case PKT_NAME_LOC_ADD_CHANGE:
        dst->_loc_add_change = *(struct PktLocAddChange const*)payload;
        return 1;
    case PKT_NAME_LOC_ANIM:
        dst->_loc_anim = *(struct PktLocAnim const*)payload;
        return 1;
    case PKT_NAME_LOC_MERGE:
        dst->_loc_merge = *(struct PktLocMerge const*)payload;
        return 1;
    case PKT_NAME_MAP_ANIM:
        dst->_map_anim = *(struct PktMapAnim const*)payload;
        return 1;
    case PKT_NAME_SOUND_AREA:
        dst->_sound_area = *(struct PktSoundArea const*)payload;
        return 1;
    case PKT_NAME_MAP_PROJANIM:
        dst->_map_projanim = *(struct PktMapProjAnim const*)payload;
        return 1;
    case PKT_NAME_SCRIPTEDPROJ_ADD:
        dst->_scriptedproj_add = *(struct PktScriptedProjAdd const*)payload;
        return 1;
    case PKT_NAME_SCRIPTEDPROJ_CHANGE:
        dst->_scriptedproj_change = *(struct PktScriptedProjChange const*)payload;
        return 1;
    case PKT_NAME_OBJ_CUSTOMISE:
        dst->_obj_customise = *(struct PktObjCustomise const*)payload;
        return 1;
    case PKT_NAME_OBJ_UNCUSTOMISE:
        dst->_obj_uncustomise = *(struct PktObjUncustomise const*)payload;
        return 1;
    default:
        return 0;
    }
}

static void
exec_zone_sub_packet_at(
    struct AppGameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload,
    struct ZoneAt const* at);

/* Is view `view` built, with nothing held for it ahead of a new packet? */
static int
zone_view_ready(
    struct App* app,
    int view)
{
    struct World const* world;

    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return 0;
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    return world && world->load_complete && !App_ZonePacketsHeld(app, view);
}

/*
 * A zone sub-packet for a view whose scene is not built cannot be dropped:
 * the reference's scene build is synchronous inside its packet loop, so
 * there every zone update processes after the rebuild it follows (a boat's
 * zones behind its REBUILD_WORLDENTITY, the Inferno's flank walls two ticks
 * after REBUILD_REGION, docs/ORANGE_WEDGE.md §17). It is held for THAT view,
 * with the zone base, plane and view as of NOW -- a later header moves the
 * base, the tick fence resets the cursor -- and applies once the view is
 * built, in arrival order (App_ZonePacketHold, app/app_tasks_locs.c). A
 * packet for a built view with packets still held for it is held behind
 * them, so arrival order holds within the view.
 */
static void
zone_exec_or_hold(
    struct AppGameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload,
    struct ZoneAt const* at)
{
    struct App* app = ctx->app;
    struct PktZoneSubPacket pkt;

    assert(at);
    if( zone_view_ready(app, at->view) )
    {
        exec_zone_sub_packet_at(ctx, name, payload, at);
        return;
    }
    memset(&pkt, 0, sizeof(pkt));
    if( !zone_sub_packet_copy(&pkt, name, payload) )
        return;
    App_ZonePacketHold(app, &pkt, at->base_x, at->base_z, at->level, at->view);
}

void
App_GameProto_ApplyHeldZoneOp(
    struct App* app,
    struct RS_ServerState const* state,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level,
    int view)
{
    struct ZoneAt at = { base_x, base_z, level, view };
    struct AppGameProtoCtx ctx = app_exec_ctx(app, state);

    assert(pkt);
    exec_zone_sub_packet_at(&ctx, pkt->name, &pkt->_loc_add_change, &at);
}

/* One loc-change record's key: the reference's locChanges identity
 * (Client-TS locChangeCreate matches level, x, z and layer). */
static int
zone_loc_change_same_key(
    struct World_LocChange const* a,
    struct World_LocChange const* b)
{
    assert(a);
    assert(b);
    return a->level == b->level && a->layer == b->layer && a->x == b->x && a->z == b->z;
}

static int
zone_loc_change_in_zone(
    struct World const* world,
    struct World_LocChange const* rec,
    int base_x,
    int base_z,
    int zone_level)
{
    assert(world);
    assert(rec);
    if( rec->x < base_x || rec->x >= base_x + 8 || rec->z < base_z || rec->z >= base_z + 8 )
        return 0;
    /* Records keep the level the loc was applied at, which is the CACHE level
     * (zone_loc_tile_at's bridge climb); the header names the walked plane. */
    return rec->level == World_LocCacheLevel(world, rec->x, rec->z, zone_level);
}

/*
 * UPDATE_ZONE_FULL_FOLLOWS resets the client's memory of the zone's LOCS as
 * well as its obj stacks: every loc this client changed inside the 8x8, on the
 * header's plane, goes back to what the map put there, and the state the
 * server writes behind the header re-applies whatever still differs.
 *
 * Reference: Client-TS Client.ts, UPDATE_ZONE_FULL_FOLLOWS (~7463), sets
 * `endTime = 0` on every locChanges entry in the zone on minusedlevel, and the
 * next locChangeDoQueue puts that entry's oldType/oldAngle/oldShape back and
 * unlinks it. The server half is the zone catch-up (Engine-TS
 * src/engine/zone/Zone.ts writeFullFollows: "this completely resets the client
 * zones to default ... the original locs"; here torirs_server_zone.c
 * write_state): it describes only what differs from the map, so a loc that
 * changed AND CHANGED BACK while this client was not being
 * told about the zone is described by nothing at all, and without the reset
 * the client keeps whatever it last drew. That was the Miscellania castle gate
 * (2510,3860): opened, its 500-tick revert fired while the player was ~40 tiles
 * away -- outside the 7x7 zone window, same scene -- and on the way back the
 * client still had the closed leaf deleted; the state named only the open
 * leaf's tile, so the doorway drew neither leaf while the server's door was
 * shut and every walk stalled against it. A revert landing on the tick the
 * player changes plane is the same hole (the new plane's zones are
 * FULL_FOLLOWSed and that tick's events skipped): the level-1 landing door
 * 2506,3851.
 *
 * Our list differs from the reference's in the one way that matters here: the
 * reference keys locChanges by (level, x, z, layer) and keeps the FIRST
 * change's old loc, while World_LocChangePush appends a record per applied
 * change that remembers what stood there just before it. So the map's own loc
 * is the OLDEST record's old_* for the key, and the restore is taken from it.
 *
 * A change still PENDING in the zone (its models loading) never reached the
 * scene or the records: it is cancelled here, so it cannot land after the
 * reset and the state behind it (App_LocChangesCancelZone). The restore goes
 * through App_WorldLocChange, a change like any other: it waits for the map
 * loc's models, and a LOC_* the state behind this header sends for the same
 * tile and layer replaces it. Its apply pushes one more record (old = what it
 * replaced, new = the map's loc). So that record is never read as "the map's loc" by the next
 * reset, the key keeps ONE record ahead of it, rewritten to old = new = the
 * map's loc; the rest of the key's records are dropped.
 *
 * A world still loading is being rebuilt from the cache, so its zone already
 * IS the map's: the records are dropped and nothing is queued.
 */
static void
zone_full_reset_locs(
    struct App* app,
    struct ZoneAt const* at)
{
    struct World* world;
    unsigned char drop[WORLD_LOC_CHANGE_MAX];
    int count;
    int write = 0;

    assert(app);
    assert(app->world);
    assert(at);
    /* The view the header addressed is the cursor (exec_zone_sub_packet_at,
     * App_GameProto_ReplayClearScene). */
    assert(WorldviewRegistry_Active(&app->worldviews) == at->view);
    world = App_ActiveWorldview(app)->world;
    if( world->load_complete )
        (void)App_LocChangesCancelZone(app, at->view, at->base_x, at->base_z, at->level);
    count = world->loc_change_count;
    memset(drop, 0, sizeof(drop));
    for( int i = 0; i < count; i++ )
    {
        struct World_LocChange* rec = &world->loc_changes[i];
        struct World_LocChange const* latest = rec;
        int first = 1;

        if( !zone_loc_change_in_zone(world, rec, at->base_x, at->base_z, at->level) )
            continue;
        for( int j = 0; j < i; j++ )
        {
            if( zone_loc_change_same_key(&world->loc_changes[j], rec) )
            {
                first = 0;
                break;
            }
        }
        if( !first || !world->load_complete )
        {
            drop[i] = 1;
            continue;
        }
        /* Where no loc stood, World_LocChangePush records old_shape -1; the
         * delete still needs a shape, and only for its LAYER, which every
         * record of the key shares -- so the change's own shape stands in,
         * and is kept on the record for the next reset. */
        int shape = rec->old_shape >= 0 ? rec->old_shape : rec->new_shape;

        for( int k = i + 1; k < count; k++ )
            if( zone_loc_change_same_key(&world->loc_changes[k], rec) )
                latest = &world->loc_changes[k];
        if( !(rec->old_type < 0 && latest->new_type < 0) &&
            !(rec->old_type == latest->new_type && rec->old_angle == latest->new_angle &&
              shape == latest->new_shape) )
        {
            /* The driver reads the restore from the pending change, as it
             * reads a LOC_DEL or LOC_ADD_CHANGE (App_DriveLocChangesPending):
             * it lands behind the map loc's models, and scriptrun
             * (ScriptrunCore_ZoneResetLocs) reverts on this packet's tick. */
            App_WorldLocChange(app, rec->x, rec->z, rec->level, rec->old_type, shape, rec->old_angle);
        }
        rec->old_shape = shape;
        rec->new_type = rec->old_type;
        rec->new_angle = rec->old_angle;
        rec->new_shape = shape;
        rec->start_time = -1;
        rec->end_time = -1;
    }
    for( int i = 0; i < count; i++ )
    {
        if( drop[i] )
            continue;
        if( write != i )
            world->loc_changes[write] = world->loc_changes[i];
        write++;
    }
    world->loc_change_count = write;
}

/* The zone base and plane the latest header named, in the view the cursor
 * addresses: where a live zone sub-packet applies. */
static struct ZoneAt
zone_at_now(struct App* app)
{
    struct ZoneAt at;

    at.base_x = app->zone_base.base_x;
    at.base_z = app->zone_base.base_z;
    at.level = app->zone_base.level;
    at.view = WorldviewRegistry_Active(&app->worldviews);
    return at;
}

static void
exec_zone_sub_packet(
    struct AppGameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload)
{
    struct App* app = ctx->app;
    struct ZoneAt at;

    /* `app->world` doubles as "the view registry has its root": no world
     * yet is a real state, and nothing registered one to resolve. */
    assert(app);
    if( !app->world )
        return;
    at = zone_at_now(app);
    zone_exec_or_hold(ctx, name, payload, &at);
}

void
App_GameProto_ReplayZoneOp(
    struct App* app,
    struct RS_ServerState const* state,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level)
{
    struct AppGameProtoCtx ctx = app_exec_ctx(app, state);
    struct ZoneAt at;

    assert(app->world);
    assert(pkt);
    at.base_x = base_x;
    at.base_z = base_z;
    at.level = level;
    at.view = WORLDVIEW_ROOT;
    zone_exec_or_hold(&ctx, pkt->name, &pkt->_loc_add_change, &at);
}

/* UPDATE_ZONE_FULL_FOLLOWS's reset of one zone: its loc changes back to the
 * map's (pending ones cancelled) and its ground items gone. */
static void
zone_full_reset(
    struct App* app,
    struct ZoneAt const* at)
{
    zone_full_reset_locs(app, at);
    for( int dz = 0; dz < 8; dz++ )
        for( int dx = 0; dx < 8; dx++ )
            App_WorldObjStackClearTile(app, at->base_x + dx, at->base_z + dz, at->level);
}

void
App_GameProto_ReplayClearScene(struct App* app)
{
    /* The zones holding a loc change or a ground item, as a bitmap over the
     * scene's 8x8 zones: a reset of each of the 13x13x4 is the same answer
     * and a quadratic walk of the change list per zone. */
    enum
    {
        ZONES = 32
    };
    unsigned char marked[ZONES][ZONES];
    struct World* world;
    struct World_EntityPool const* pool;

    assert(app);
    assert(app->world);
    assert(WorldviewRegistry_Active(&app->worldviews) == WORLDVIEW_ROOT);
    world = app->world;
    /* A change still loading would land on the scene the reset leaves. */
    App_LocChangesCancelAll(app);
    memset(marked, 0, sizeof(marked));
    for( int i = 0; i < world->loc_change_count; i++ )
    {
        int const zx = world->loc_changes[i].x >> 3;
        int const zz = world->loc_changes[i].z >> 3;

        if( zx >= 0 && zx < ZONES && zz >= 0 && zz < ZONES )
            marked[zx][zz] = 1;
    }
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i >= 0; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* stack = World_EntityPoolAt(pool, i);
        int const zx = stack->grid_position.x >> 3;
        int const zz = stack->grid_position.z >> 3;

        if( zx >= 0 && zx < ZONES && zz >= 0 && zz < ZONES )
            marked[zx][zz] = 1;
    }
    for( int zx = 0; zx < ZONES; zx++ )
    {
        for( int zz = 0; zz < ZONES; zz++ )
        {
            if( !marked[zx][zz] )
                continue;
            for( int level = 0; level < 4; level++ )
            {
                /* UPDATE_ZONE_FULL_FOLLOWS's arm, for this zone and plane. */
                struct ZoneAt at = { zx * 8, zz * 8, level, WORLDVIEW_ROOT };

                zone_full_reset(app, &at);
            }
        }
    }
}

/*
 * MAP_ANIM_SPECIFIC: a MAP_ANIM for this player only, addressed by a
 * build-area tile with no zone header, on the player's own plane (deob
 * client.java prot 90: `new class52(worldview, id, worldview.field1400, ...)`
 * -- the same constructor MAP_ANIM reaches). Played through the MAP_ANIM arm
 * with the zone the tile is in as its base, so the scene-bounds rule, the
 * spawn and the drive note are MAP_ANIM's own; queued like any zone packet
 * while the world is still loading.
 */
static void
exec_map_anim_specific(
    struct AppGameProtoCtx const* ctx,
    struct PktMapAnimSpecific const* pkt)
{
    struct App* app = ctx->app;
    struct PktMapAnim sub;
    struct ZoneAt at;

    assert(pkt);
    /* Same root-registered sentinel as exec_zone_sub_packet. */
    assert(app);
    if( !app->world )
        return;
    memset(&sub, 0, sizeof(sub));
    sub.pos = ((pkt->x & 7) << 4) | (pkt->z & 7);
    sub.id = pkt->id;
    sub.height = pkt->height;
    sub.delay = pkt->delay;
    at.base_x = pkt->x & ~7;
    at.base_z = pkt->z & ~7;
    at.level = zone_player_level(app);
    at.view = WorldviewRegistry_Active(&app->worldviews);
    zone_exec_or_hold(ctx, PKT_NAME_MAP_ANIM, &sub, &at);
}

/*
 * LOC_ANIM_SPECIFIC: LOC_ANIM for this player only, addressed like
 * MAP_ANIM_SPECIFIC by a build-area tile on the player's plane (deob client.java
 * class259.field3139 -> Statics.method5969, the method LOC_ANIM reaches).
 */
static void
exec_loc_anim_specific(
    struct AppGameProtoCtx const* ctx,
    struct PktLocAnimSpecific const* pkt)
{
    struct App* app = ctx->app;
    struct PktLocAnim sub;
    struct ZoneAt at;

    assert(pkt);
    assert(app);
    if( !app->world )
        return;
    memset(&sub, 0, sizeof(sub));
    sub.pos = ((pkt->x & 7) << 4) | (pkt->z & 7);
    sub.info = pkt->info;
    sub.seq_id = pkt->seq_id;
    at.base_x = pkt->x & ~7;
    at.base_z = pkt->z & ~7;
    at.level = zone_player_level(app);
    at.view = WorldviewRegistry_Active(&app->worldviews);
    zone_exec_or_hold(ctx, PKT_NAME_LOC_ANIM, &sub, &at);
}

/*
 * PROJANIM_SPECIFIC_V4: a projectile for this player only (deob client.java
 * class259.field3164 -> new class31, as MAP_PROJANIM). Both ends are absolute,
 * so it runs through the MAP_PROJANIM arm with the source's zone as its base and
 * the landing tile as its absolute coord.
 */
static void
exec_projanim_specific(
    struct AppGameProtoCtx const* ctx,
    struct PktProjAnimSpecific const* pkt)
{
    struct App* app = ctx->app;
    struct PktMapProjAnim sub;
    struct ZoneAt at;
    int local_x;
    int local_z;

    assert(pkt);
    assert(app);
    if( !app->world || pkt->spotanim == 65535 )
        return;
    /* Against the scene the server addressed, which is the REBUILD's while
     * the world is still building it: the root world's own base is the old
     * scene's until then, and the packet is held for the new one. */
    zone_view_origin(app, WORLDVIEW_ROOT, &local_x, &local_z);
    local_x = pkt->x - local_x;
    local_z = pkt->z - local_z;
    /* Wire data, as exec_obj_specific's. */
    if( local_x < 0 || local_z < 0 || local_x >= app->world->_scene_size ||
        local_z >= app->world->_scene_size )
    {
        TORIRS_LOG("exec: projanim specific at %d,%d is outside the scene\n", pkt->x, pkt->z);
        return;
    }
    memset(&sub, 0, sizeof(sub));
    sub.pos = ((local_x & 7) << 4) | (local_z & 7);
    sub.target = pkt->target;
    sub.source = pkt->source;
    sub.spotanim = pkt->spotanim;
    sub.src_height = pkt->src_height;
    sub.dst_height = pkt->dst_height;
    sub.start_delay = pkt->start_delay;
    sub.end_delay = pkt->end_delay;
    sub.peak = pkt->peak;
    sub.arc = pkt->arc;
    sub.dst_abs = 1;
    sub.dst_abs_level = pkt->dst_level;
    sub.dst_abs_x = pkt->dst_x;
    sub.dst_abs_z = pkt->dst_z;
    at.base_x = local_x & ~7;
    at.base_z = local_z & ~7;
    at.level = pkt->level;
    at.view = WORLDVIEW_ROOT;
    zone_exec_or_hold(ctx, PKT_NAME_MAP_PROJANIM, &sub, &at);
}

/*
 * OBJ_CUSTOMISE_SPECIFIC(_V2) / OBJ_UNCUSTOMISE_SPECIFIC: the zone forms at an
 * absolute tile, queued the same way while the world loads (deob client.java
 * class259.field3188, field3224, field3106).
 */
static void
exec_obj_customise_specific(
    struct AppGameProtoCtx const* ctx,
    enum GameProtoPktName name,
    struct PktObjCustomiseSpecific const* pkt)
{
    struct App* app = ctx->app;
    union
    {
        struct PktObjCustomise add;
        struct PktObjUncustomise del;
    } sub;
    enum GameProtoPktName sub_name;
    struct ZoneAt at;
    int local_x;
    int local_z;
    int pos;

    assert(pkt);
    assert(app);
    if( !app->world )
        return;
    /* Against the scene the server addressed (exec_projanim_specific). */
    zone_view_origin(app, WORLDVIEW_ROOT, &local_x, &local_z);
    local_x = pkt->x - local_x;
    local_z = pkt->z - local_z;
    /* Wire data, as exec_obj_specific's. */
    if( local_x < 0 || local_z < 0 || local_x >= app->world->_scene_size ||
        local_z >= app->world->_scene_size )
    {
        TORIRS_LOG("exec: obj customise at %d,%d is outside the scene\n", pkt->x, pkt->z);
        return;
    }
    pos = ((local_x & 7) << 4) | (local_z & 7);
    memset(&sub, 0, sizeof(sub));
    if( name == PKT_NAME_OBJ_CUSTOMISE_SPECIFIC )
    {
        sub_name = PKT_NAME_OBJ_CUSTOMISE;
        sub.add.pos = pos;
        sub.add.obj_id = pkt->obj_id;
        sub.add.count = pkt->count;
        sub.add.fields = pkt->fields;
    }
    else
    {
        assert(name == PKT_NAME_OBJ_UNCUSTOMISE_SPECIFIC);
        sub_name = PKT_NAME_OBJ_UNCUSTOMISE;
        sub.del.pos = pos;
        sub.del.obj_id = pkt->obj_id;
        sub.del.count = pkt->count;
    }
    at.base_x = local_x & ~7;
    at.base_z = local_z & ~7;
    at.level = pkt->level;
    at.view = WORLDVIEW_ROOT;
    zone_exec_or_hold(ctx, sub_name, &sub, &at);
}

/*
 * OBJ_ADD_SPECIFIC / OBJ_DEL_SPECIFIC / OBJ_COUNT_SPECIFIC: a ground obj for
 * this player only, at an ABSOLUTE tile with no zone header. Played through
 * the zone OBJ arms with the zone the tile is in as their base, like
 * MAP_ANIM_SPECIFIC, so a stack the server owns per player stacks, counts and
 * despawns exactly as a public one does; queued while the world is loading.
 */
static void
exec_obj_specific(
    struct AppGameProtoCtx const* ctx,
    enum GameProtoPktName name,
    struct PktObjSpecific const* pkt)
{
    struct App* app = ctx->app;
    union
    {
        struct PktObjAdd add;
        struct PktObjDel del;
        struct PktObjCount count;
        struct PktObjReveal reveal;
    } sub;
    enum GameProtoPktName sub_name;
    struct ZoneAt at;
    int local_x;
    int local_z;
    int pos;

    assert(pkt);
    assert(app);
    if( !app->world )
        return;
    /* Against the scene the server addressed (exec_projanim_specific): the
     * accepted REBUILD's base, which during a rebuild is the new scene's while
     * app->world's is still the old one (F3's pattern; it converted against
     * app->world here, so a stack sent mid-rebuild landed one scene off). */
    zone_view_origin(app, WORLDVIEW_ROOT, &local_x, &local_z);
    local_x = pkt->x - local_x;
    local_z = pkt->z - local_z;
    /* Wire data: a stack outside the scene is a lagging or confused server,
     * refused rather than written past the map. */
    if( local_x < 0 || local_z < 0 || local_x >= app->world->_scene_size ||
        local_z >= app->world->_scene_size )
    {
        TORIRS_LOG("exec: obj specific at %d,%d is outside the scene\n", pkt->x, pkt->z);
        return;
    }
    pos = ((local_x & 7) << 4) | (local_z & 7);
    memset(&sub, 0, sizeof(sub));
    if( name == PKT_NAME_OBJ_ADD_SPECIFIC )
    {
        sub_name = PKT_NAME_OBJ_ADD;
        sub.add.pos = pos;
        sub.add.obj_id = pkt->obj_id;
        sub.add.count = pkt->count;
        sub.add.time_until_public = pkt->time_until_public;
        sub.add.time_until_despawn = pkt->time_until_despawn;
        sub.add.ownership_type = pkt->ownership_type;
        sub.add.never_becomes_public = pkt->never_becomes_public;
    }
    else if( name == PKT_NAME_OBJ_DEL_SPECIFIC )
    {
        sub_name = PKT_NAME_OBJ_DEL;
        sub.del.pos = pos;
        sub.del.obj_id = pkt->obj_id;
    }
    else if( name == PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC )
    {
        sub_name = PKT_NAME_OBJ_REVEAL;
        sub.reveal.pos = pos;
        sub.reveal.obj_id = pkt->obj_id;
        sub.reveal.receiver = -1;
        sub.reveal.has_enabled_ops = 1;
        sub.reveal.enabled_ops = pkt->op_flags;
    }
    else
    {
        assert(name == PKT_NAME_OBJ_COUNT_SPECIFIC);
        sub_name = PKT_NAME_OBJ_COUNT;
        sub.count.pos = pos;
        sub.count.obj_id = pkt->obj_id;
        sub.count.old_count = pkt->old_count;
        sub.count.new_count = pkt->count;
    }
    at.base_x = local_x & ~7;
    at.base_z = local_z & ~7;
    at.level = pkt->level;
    at.view = WORLDVIEW_ROOT;
    zone_exec_or_hold(ctx, sub_name, &sub, &at);
}

/*
 * CLEAR_ENTITIES: every npc and world entity of the active world goes, and the
 * active world is the root again; players stay, their own stream moves them
 * (RSProx ClearEntities). The npc list is the POSITIONAL one NPC_INFO
 * addresses, so it empties with them, or the next update would read its
 * records against npcs that are gone.
 */
static void
exec_clear_entities(struct App* app)
{
    int const view = WorldviewRegistry_Active(&app->worldviews);

    while( Wevs_ViewListCount(&app->wevs, view) > 0 )
        App_WevDespawn(app, Wevs_ViewListAt(&app->wevs, view, 0)->id);
    if( view == WORLDVIEW_ROOT )
    {
        for( int i = 0; i < app->esync.active_npc_count; i++ )
            RS_EntitySync_RemoveNpc(&app->esync, app->world, app->esync.active_npcs[i]);
        app->esync.active_npc_count = 0;
    }
    else
        RS_EntitySync_ClearViewNpcs(&app->esync, app->world, view);
    WorldviewRegistry_ResetActive(&app->worldviews);
    RedrawRequest_Raise(&app->redraw_request);
}

static void
exec_zone_full_follows(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at)
{
    zone_full_reset(ctx->app, at);
}

static void
exec_zone_obj_add(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktObjAdd const* pkt)
{
    /* DRIVE_STAMP: obj_added -- a=obj_id b=count c=tile_x d=tile_z.
     * drop's corroboration: the item appearing at the player's tile. */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    int idx;
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_DriveEvent(app, DRIVE_EVENT_OBJ_ADDED, pkt->obj_id, pkt->count, tile_x, tile_z);
    idx = App_WorldObjStackAdd(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
    /* -1 only for a count-0 add naming no stack on the tile: a row is
     * made whatever the cache holds (its model may follow). */
    if( idx >= 0 )
        App_WorldObjStackSetOwnership(
            app,
            idx,
            pkt->time_until_public,
            pkt->time_until_despawn,
            pkt->ownership_type,
            pkt->never_becomes_public);
}

static void
exec_zone_obj_del(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktObjDel const* pkt)
{
    /* DRIVE_STAMP: obj_removed -- a=obj_id c=tile_x d=tile_z. click_obj
     * learns the stack vanished from this; no polling. */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_DriveEvent(app, DRIVE_EVENT_OBJ_REMOVED, pkt->obj_id, 0, tile_x, tile_z);
    App_WorldObjStackDel(app, tile_x, tile_z, level, pkt->obj_id);
}

static void
exec_zone_obj_count(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktObjCount const* pkt)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_WorldObjStackSetCount(app, tile_x, tile_z, level, pkt->obj_id, pkt->new_count);
}

static void
exec_zone_obj_reveal(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktObjReveal const* pkt)
{
    /* Reveal targets one receiver; everyone else already sees it. */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    /* OBJ_ENABLED_OPS shares this name and addresses every viewer. */
    if( pkt->has_enabled_ops )
    {
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackSetEnabledOps(app, tile_x, tile_z, level, pkt->obj_id, pkt->enabled_ops);
        return;
    }
    if( ctx->app->esync.local_pid >= 0 && pkt->receiver != ctx->app->esync.local_pid )
        return;
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_WorldObjStackAdd(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
}

static void
exec_zone_loc_del(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktLocDel const* pkt)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
    /* Remove the loc in this shape's layer (scene + collision). shape =
     * info >> 2 keys the layer so a door (WALL) removal only hits the WALL
     * loc, not a centrepiece/floor-decor sharing the tile. loc_id = -1 = no
     * replacement. A loc change like an add (app/app_tasks_locs.c): it
     * replaces an add still loading for the tile and layer, and loads
     * nothing itself. */
    App_WorldLocChange(app, tile_x, tile_z, level, -1, pkt->info >> 2, pkt->info & 0x3);
}

static void
exec_zone_loc_add_change(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktLocAddChange const* pkt)
{
    /* Replace the loc in this layer: remove the stale loc and spawn the new
     * one (scene + collision), matching Client-TS locChangeUnchecked. Async:
     * the new loc's models are usually absent from the static build's
     * preload, so the change applies once they're resident (reference
     * changeLocAvailable gate in locChangeDoQueue). */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
    /* The op mask and replacement labels ride along: they describe THIS
     * placement, so they cannot be recovered from the loc id later. */
    App_WorldLocChangeOps(
        app, tile_x, tile_z, level, pkt->loc_id, pkt->info >> 2, pkt->info & 0x3,
        pkt->op_flags, pkt->ops);
    if( torirs_env_net_debug() )
        TORIRS_LOG("gameproto_exec: LOC_ADD_CHANGE loc=%d shape=%d angle=%d at %d,%d,l%d\n",
            pkt->loc_id,
            pkt->info >> 2,
            pkt->info & 0x3,
            tile_x,
            tile_z,
            level);
}

static void
exec_zone_loc_anim(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktLocAnim const* pkt)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
    App_WorldSceneryAnim(app, tile_x, tile_z, level, pkt->info >> 2, pkt->seq_id);
    /* The driver reads the animation from the packet, not from the
     * scenery that waits on the loc's models (App_DriveLocAnimNote). */
    if( App_ActiveWorldview(app)->world == app->world )
        App_DriveLocAnimNote(app, tile_x, tile_z, level, pkt->info >> 2, pkt->seq_id);
}

static void
exec_zone_map_anim(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktMapAnim const* pkt)
{
    /* Free-standing graphical effect at a tile (reference MAP_ANIM ->
     * new MapSpotAnim). id 65535 is the clear sentinel; ignore it. */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    if( pkt->id != 65535 )
    {
        /* The view the packet names (the cursor as of arrival), its scene
         * and its frame: a deck's graphic is bounded by the deck. */
        struct World const* world = App_ActiveWorldview(app)->world;
        int origin_x;
        int origin_z;

        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        assert(world);
        /* The reference files a MAP_ANIM only on a tile its scene holds
         * (worldview.method3333 before new class52), and drops the rest
         * without a trace. Same here, so a graphic the real client cannot
         * show is not one this client shows either. */
        if( tile_x < 0 || tile_z < 0 || tile_x >= world->_scene_size ||
            tile_z >= world->_scene_size )
            return;
        /* The row exists now; its body lands later. */
        (void)App_WorldSpotanimSpawn(
            app, tile_x, tile_z, level, pkt->id, pkt->height, pkt->delay);
        zone_view_origin(app, at->view, &origin_x, &origin_z);
        App_DriveMapAnimNote(app, origin_x + tile_x, origin_z + tile_z, level, pkt->id);
    }
}

static void
exec_zone_sound_area(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktSoundArea const* pkt)
{
    /*
     * A one-shot sound at a tile, with its own audible radius. The level is
     * decoded but not used to gate it: the reference range-checks in x/z
     * only, and a forge on the floor below is meant to be heard.
     */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    /* 65535 is the wire's -1: Jagex's `sound_area id=-1` for an npc with
     * no sound of its own. The reference queues it and then finds no
     * synth 65535 to load; nothing is heard either way, so it stops here
     * rather than asking the cache for a sound that does not exist. */
    if( pkt->id == 65535 )
        return;
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_PlaySoundAt(
        app, pkt->id, pkt->loops, pkt->delay, tile_x, tile_z, pkt->radius, pkt->inner);
}

static void
exec_zone_map_projanim(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktMapProjAnim const* pkt)
{
    /* Projectile spawn from a spotanim config (reference MAP_PROJANIM ->
     * new ClientProj). The base tile is `pos` within the zone; the
     * destination is offset by dx/dz (the target's cast-time position, which
     * is also the initial aim point). peak/arc are Client-TS angle/startpos.
     * pkt->target names the entity the arc then homes on each cycle. */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    int src_tx, src_tz, dst_tx, dst_tz, src_level;
    /* Absolute coords are the named view's: the root's scene base, or a
     * deck's own frame. */
    int origin_x;
    int origin_z;
    zone_view_origin(app, at->view, &origin_x, &origin_z);
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    src_tx = tile_x;
    src_tz = tile_z;
    /* The zone's plane is the SOURCE's; rev-239 states the landing plane
     * separately in the absolute coord, and paints the projectile on the
     * source's (method4319 adds it on field374). */
    src_level = level;
    if( pkt->dst_abs )
    {
        /* Revision 239 states the landing point as an absolute CoordGrid;
         * everything below the parse works in scene-local tiles, and the
         * scene origin is the zone the last rebuild centred on, minus the
         * six zones of margin the client keeps on each side. Applied as
         * offsets, an absolute coord puts the arc a few tiles from the
         * scene's south-west corner and the shot flies off the map. */
        dst_tx = pkt->dst_abs_x - origin_x;
        dst_tz = pkt->dst_abs_z - origin_z;
        level = pkt->dst_abs_level;
    }
    else
    {
        dst_tx = tile_x + pkt->dx_offset;
        dst_tz = tile_z + pkt->dz_offset;
    }
    App_DriveProjectileNote(
            app,
            origin_x + src_tx,
            origin_z + src_tz,
            origin_x + dst_tx,
            origin_z + dst_tz,
            level,
            pkt->spotanim,
            pkt->target,
            pkt->start_delay,
            pkt->end_delay);
    /* The projectile exists and flies from now; its body lands later. */
    (void)App_WorldProjectileSpawn(
        app,
        src_tx,
        src_tz,
        src_level,
        dst_tx,
        dst_tz,
        level,
        pkt->spotanim,
        pkt->src_height,
        pkt->dst_height,
        pkt->start_delay,
        pkt->end_delay,
        pkt->peak,
        pkt->arc,
        pkt->target,
        pkt->source);
}

static void
exec_zone_obj_customise(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktObjCustomise const* pkt)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    struct WorldEntity_ObjCustomise custom = {
        .model = pkt->fields.model,
        .tint = pkt->fields.tint,
        .recol_count = pkt->fields.recol_count,
        .recol_index = pkt->fields.recol_index,
        .recol_value = pkt->fields.recol_value,
        .retex_count = pkt->fields.retex_count,
        .retex_index = pkt->fields.retex_index,
        .retex_value = pkt->fields.retex_value,
    };

    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_WorldObjStackCustomise(app, tile_x, tile_z, level, pkt->obj_id, pkt->count, &custom);
}

static void
exec_zone_obj_uncustomise(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktObjUncustomise const* pkt)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;

    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    App_WorldObjStackUncustomise(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
}

static void
exec_zone_scriptedproj_change(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktScriptedProjChange const* pkt)
{
    /* Deob class278.field3434 -> class56.method1481; an absent slot is
     * ignored there too. */
    struct App* app = ctx->app;
    struct World* world = App_ActiveWorldview(app)->world;
    int const idx = World_ProjectileFindScriptedSlot(world, pkt->slot);

    if( idx < 0 )
        return;
    /* The projectile exists from its ADD on, whether or not its body has
     * landed, so a CHANGE right behind the ADD finds it. */
    if( pkt->target_x != -1 )
    {
        int origin_x;
        int origin_z;

        zone_view_origin(app, at->view, &origin_x, &origin_z);
        World_ProjectileScriptedRetarget(
            world,
            idx,
            (pkt->target_x - origin_x) * 128 + 64,
            (pkt->target_z - origin_z) * 128 + 64,
            pkt->target_level,
            pkt->target_offset_x,
            pkt->target_offset_z,
            pkt->target_height,
            pkt->target);
    }
    if( pkt->freeze_cycles != -1 )
        World_ProjectileScriptedFreeze(
            world, idx, pkt->freeze_cycles, pkt->delete_on_freeze_end != 0);
}

static void
exec_zone_scriptedproj_add(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktScriptedProjAdd const* pkt)
{
    /* Rev-241 scripted projectile (deob class278.field3437 -> new class56):
     * the source is the zone tile, the target an absolute coord taken to
     * scene tiles against the rebuild origin as MAP_PROJANIM's is. */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    int origin_x;
    int origin_z;
    struct WorldEntity_ScriptedProjSpec sp;

    zone_view_origin(app, at->view, &origin_x, &origin_z);
    zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
    /* `if (var16 == null)` (rev-241 deob Statics.java ~48257): a slot in
     * use keeps its projectile and the packet does nothing else. The
     * projectile exists from its ADD on, so an ADD right behind another
     * for the same slot sees it, loaded or not. */
    if( World_ProjectileFindScriptedSlot(App_ActiveWorldview(app)->world, pkt->slot) >= 0 )
        return;
    App_DriveProjectileNote(
        app,
        origin_x + tile_x,
        origin_z + tile_z,
        pkt->target_x,
        pkt->target_z,
        pkt->target_level,
        pkt->spotanim,
        pkt->target,
        pkt->start_delay,
        pkt->end_delay);
    sp = (struct WorldEntity_ScriptedProjSpec){
        .src_x = tile_x,
        .src_z = tile_z,
        .src_level = level,
        .src_offset_x = pkt->source_offset_x,
        .src_offset_z = pkt->source_offset_z,
        .src_height = pkt->source_height,
        .source = pkt->source,
        .dst_x = pkt->target_x - origin_x,
        .dst_z = pkt->target_z - origin_z,
        .dst_level = pkt->target_level,
        .dst_offset_x = pkt->target_offset_x,
        .dst_offset_z = pkt->target_offset_z,
        .dst_height = pkt->target_height,
        .target = pkt->target,
        .spotanim_id = pkt->spotanim,
        .start_delay = pkt->start_delay,
        .end_delay = pkt->end_delay,
        .curve = { pkt->curve_h, pkt->curve_a, pkt->curve_t },
        .slot = pkt->slot,
    };
    (void)App_WorldScriptedProjSpawn(app, &sp);
}

static void
exec_zone_loc_merge(
    struct AppGameProtoCtx const* ctx,
    struct ZoneAt const* at,
    struct PktLocMerge const* pkt)
{
    /* P_LOCMERGE: hide the loc for [start,end) and mark the player so the
     * loc model rides with them (Client-TS zonePacket P_LOCMERGE). */
    struct App* app = ctx->app;
    int tile_x, tile_z, level;
    int pid;
    zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
    pid = pkt->pid;
    App_WorldLocMerge(
        app,
        tile_x,
        tile_z,
        level,
        pkt->loc_id,
        pkt->info >> 2,
        pkt->info & 0x3,
        pkt->start,
        pkt->end,
        pid);
}

static void
exec_zone_sub_packet_at(
    struct AppGameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload,
    struct ZoneAt const* at)
{
    struct App* app = ctx->app;
    int prev_view = WorldviewRegistry_Active(&app->worldviews);

    /* Apply under the cursor AS OF ARRIVAL. A replayed pending packet was
     * queued behind a SET_ACTIVE_WORLD the tick fence has since reset, so the
     * live cursor is the wrong view for it; the applicators below (and the
     * spawn tasks they enqueue, which capture the cursor) all resolve their
     * (world, builder) through App_ActiveWorldview. Restored on the way out —
     * for a live packet this round-trips the same value. */
    WorldviewRegistry_SetActive(&app->worldviews, at->view);

    switch( name )
    {
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        exec_zone_full_follows(ctx, at);
        break;
    case PKT_NAME_OBJ_ADD:
        exec_zone_obj_add(ctx, at, payload);
        break;
    case PKT_NAME_OBJ_DEL:
        exec_zone_obj_del(ctx, at, payload);
        break;
    case PKT_NAME_OBJ_COUNT:
        exec_zone_obj_count(ctx, at, payload);
        break;
    case PKT_NAME_OBJ_REVEAL:
        exec_zone_obj_reveal(ctx, at, payload);
        break;
    case PKT_NAME_LOC_DEL:
        exec_zone_loc_del(ctx, at, payload);
        break;
    case PKT_NAME_LOC_ADD_CHANGE:
        exec_zone_loc_add_change(ctx, at, payload);
        break;
    case PKT_NAME_LOC_ANIM:
        exec_zone_loc_anim(ctx, at, payload);
        break;
    case PKT_NAME_MAP_ANIM:
        exec_zone_map_anim(ctx, at, payload);
        break;
    case PKT_NAME_SOUND_AREA:
        exec_zone_sound_area(ctx, at, payload);
        break;
    case PKT_NAME_MAP_PROJANIM:
        exec_zone_map_projanim(ctx, at, payload);
        break;
    case PKT_NAME_OBJ_CUSTOMISE:
        exec_zone_obj_customise(ctx, at, payload);
        break;
    case PKT_NAME_OBJ_UNCUSTOMISE:
        exec_zone_obj_uncustomise(ctx, at, payload);
        break;
    case PKT_NAME_SCRIPTEDPROJ_CHANGE:
        exec_zone_scriptedproj_change(ctx, at, payload);
        break;
    case PKT_NAME_SCRIPTEDPROJ_ADD:
        exec_zone_scriptedproj_add(ctx, at, payload);
        break;
    case PKT_NAME_LOC_MERGE:
        exec_zone_loc_merge(ctx, at, payload);
        break;
    default:
        break;
    }

    WorldviewRegistry_SetActive(&app->worldviews, prev_view);
}

/*
 * Open the welcome screen (reference tcpIn LAST_LOGIN_INFO).
 *
 * The reference picks WHICH interface by scanning every loaded IfType for a
 * component whose clientCode is 650 or 655 and taking that component's layer.
 * It can: its interface archive is one blob, decoded whole at startup. This
 * client loads interfaces as per-id packs and has nothing to scan, so the two
 * answers that scan would give are declared in the profile instead
 * (`[iface:welcome_screen]` / `[iface:welcome_screen_notice]`).
 *
 * What stays here is the CHOICE between them, because that is a fact about the
 * packet rather than about the cache: 201 is the "nothing to say about
 * recovery questions" sentinel, so anything else -- or a members-on-a-free-
 * world warning -- means there is a paragraph to show, and the paragraph rows
 * only exist on the second screen.
 */
static void
exec_open_welcome_screen(struct App* app)
{
    char const* iface_name;
    int iface_id;

    assert(app);

    /* No address means the server is not offering a welcome screen at all.
     * The reference gates on the same field, and it is also what keeps a
     * zero-filled App from opening one before any packet has arrived. */
    if( app->session_info.welcome.last_ip == 0 )
        return;
    /* Never in front of something already open. LAST_LOGIN_INFO is a login
     * packet by convention, not by rule, and the reference makes the same
     * check rather than trusting that. */
    if( app->slots.main_modal_id != -1 )
        return;

    iface_name = (app->session_info.welcome.days_since_recovery != RS_CC_RECOVERY_DAYS_SILENT ||
                  app->session_info.welcome.member_warning == 1)
                     ? "welcome_screen_notice"
                     : "welcome_screen";
    iface_id = RevConfigRefs_Get(&app->revconfig_refs, "iface", iface_name);
    /* -1 = this profile declares no such interface, which is the honest answer
     * for a revision whose cache has no welcome screen. Not a fallback to the
     * other name: the two hold different rows, and opening the wrong one shows
     * a paragraph about recovery questions that the server never sent. */
    if( iface_id < 0 )
        return;

    /* Clears a side modal or chat dialogue the way the reference's closeModal()
     * does. It does NOT send CLOSE_MODAL: the reference's send is unconditional
     * only because closeModal() is one function, and we have already
     * established there is no viewport modal for the server to hear about. */
    RS_UISlots_CloseModal(app);
    RS_UISlots_OpenMain(app, iface_id);
}

/*
 * WORLDENTITY_INFO (SAILING_PLAN C1, deob Statics.method977): the packet is
 * per view — its `count` ops walk the ACTIVE view's server-ordered entity
 * list in list order, so op i addresses list slot i, not entity id i. The
 * count exceeding the list is the server and client disagreeing about how
 * many boats exist, which is a protocol violation, not a state to walk past
 * — the deob throws a RuntimeException on it, so the assert is faithful. A
 * count SHORT of the list is the opposite: the tail is being despawned.
 *
 * Despawns are collected before being swept: removing a slot renumbers the
 * indices the remaining ops address.
 */
static void
exec_worldentity_info(
    struct App* app,
    struct PktWorldEntityInfo const* pkt)
{
    int view;
    int despawn_ids[WORLDVIEW_MAX];
    int despawn_count = 0;

    assert(app);
    assert(pkt);
    view = WorldviewRegistry_Active(&app->worldviews);
    /* The count is WIRE data: a count past the view's actual list is a
     * malformed or misaddressed packet (parse cannot bound it against client
     * state), and indexing the list with it walks past live entries — refuse
     * the packet, don't assert (NDEBUG turns the assert into the OOB walk
     * itself). */
    if( pkt->count > Wevs_ViewListCount(&app->wevs, view) )
    {
        TORIRS_LOG(
            "exec: WORLDENTITY_INFO refused, count %d > list %d (view %d)\n",
            pkt->count,
            Wevs_ViewListCount(&app->wevs, view),
            view);
        return;
    }

    /*
     * A count SHORTER than the list is the server truncating it: every entity
     * from `count` on is gone (deob Statics.method977's leading
     * `for (i = count; i < list.size(); i++) remove`). Without this a boat
     * that sails out of range never despawns — it keeps its view, its scene
     * elements and its slot in the registry for the rest of the session.
     *
     * Collect first, despawn after: Wevs_Despawn compacts the list, so
     * removing while indexing it walks past live entries.
     */
    for( int i = pkt->count; i < Wevs_ViewListCount(&app->wevs, view); i++ )
        despawn_ids[despawn_count++] = Wevs_ViewListAt(&app->wevs, view, i)->id;
    for( int i = 0; i < despawn_count; i++ )
        App_WevDespawn(app, despawn_ids[i]);
    despawn_count = 0;

    for( int i = 0; i < pkt->count; i++ )
    {
        struct PktWevUpdate const* up = &pkt->updates[i];
        struct Wev* wev = Wevs_ViewListAt(&app->wevs, view, i);

        if( up->op == PKT_WEV_OP_DESPAWN )
        {
            despawn_ids[despawn_count++] = wev->id;
            continue;
        }
        if( up->op == PKT_WEV_OP_ENQUEUE || up->op == PKT_WEV_OP_SNAP )
            Wev_ApplyMove(
                wev,
                up->dx,
                up->dy,
                up->dz,
                up->dangle,
                up->op == PKT_WEV_OP_SNAP,
                app->wevs.clock);
        /* Only a bit-0x2 payload replaces the op mask; the flags byte on its
         * own says nothing about which right-click ops are enabled, and
         * writing it here would blank every op on any flagless update. */
        if( up->has_op_mask )
            wev->op_mask = (unsigned)up->op_mask;
        if( up->has_seq )
        {
            wev->seq_id = up->seq_id;
            wev->seq_delay = up->seq_delay;
            /* One animaya frame per 20 ms cycle; the delay holds frame 0
             * that many cycles away (deob field5699). */
            wev->seq_start_cycle = app->wevs.clock + up->seq_delay;
        }
    }

    for( int i = 0; i < despawn_count; i++ )
        App_WevDespawn(app, despawn_ids[i]);

    /*
     * The trailer's position is fine units from the PARENT view's build-area
     * origin (scene-local, as the deob keeps it: a rebuild shifts every
     * class467 by the base delta, client.java's method10438(-dx << 7, ...)).
     * A Wev's transform is absolute root-world fine units, which a rebuild
     * leaves alone for the same reason -- so the origin is added once, here,
     * against the base the parent has when the packet lands. Moves are deltas
     * and need nothing.
     */
    struct World const* parent = WorldviewRegistry_Get(&app->worldviews, view)->world;
    int const origin_fx = parent->_base_tile_x * 128;
    int const origin_fz = parent->_base_tile_z * 128;

    for( int i = 0; i < pkt->spawn_count; i++ )
    {
        struct PktWevSpawn const* sp = &pkt->spawns[i];
        struct Wev* wev;
        int live;

        /* `sp->id` is the server's world-entity id, not a view slot. A spawn
         * naming one that is already live replaces it (the deob despawns
         * before re-adding; a repeat-spawn that skipped this would leave two
         * views answering to one id). */
        live = WorldviewRegistry_SlotForWire(&app->worldviews, sp->id);
        if( live > WORLDVIEW_ROOT )
            App_WevDespawn(app, live);
        wev = App_WevSpawn(
            app,
            sp->id,
            sp->config_id,
            sp->size_x_tiles,
            sp->size_z_tiles,
            sp->priority_group,
            origin_fx + sp->x,
            origin_fz + sp->z,
            sp->angle,
            (unsigned)sp->op_mask);
        if( !wev )
            continue; /* refused (bad config/size) — App_WevSpawn logged it */

        if( sp->has_seq )
        {
            wev->seq_id = sp->seq_id;
            wev->seq_delay = sp->seq_delay;
            wev->seq_start_cycle = app->wevs.clock + sp->seq_delay;
        }
    }
    RedrawRequest_Raise(&app->redraw_request);
}

/* ---- friends chat, clans --------------------------------------------------- */

static void
exec_friends_chat(
    struct RS_CS2Host* host,
    int packet_type,
    struct PktRawPayload const* payload)
{
    assert(host);
    assert(payload);
    bool const full = packet_type == PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL;
    bool const decoded =
        full ? RS_FriendsChat_ApplyFull(
                   &host->friends_chat, payload->data, payload->length, host->local_player_name)
             : RS_FriendsChat_ApplySingleUser(
                   &host->friends_chat, payload->data, payload->length, host->local_player_name);
    if( !decoded )
        TORIRS_ERR(
            "gameproto_exec: %s did not decode (%d bytes)\n",
            full ? "UPDATE_FRIENDCHAT_CHANNEL_FULL" : "UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER",
            payload->length);
    /* Both bump onclantransmit whatever the payload held, and the reference
     * re-sorts the list the next frame. */
    RS_FriendsChat_Sort(&host->friends_chat, host->map_world);
    host->clan_transmit_dirty = 1;
}

static int
exec_varclan_type_of(
    void* user,
    int var_id)
{
    return RS_CS2Host_VarClanType((struct RS_CS2Host const*)user, var_id);
}

static void
exec_clan_request_full(
    struct RS_CS2Host* host,
    int kind,
    int clan_type)
{
    struct RS_CS2SocialSend send;
    memset(&send, 0, sizeof(send));
    send.kind = kind;
    send.values[0] = clan_type;
    RS_CS2Host_SendPush(host, &send);
}

static void
exec_clan(
    struct RS_CS2Host* host,
    int packet_type,
    struct PktRawPayload const* payload)
{
    assert(host);
    assert(payload);
    int request_full = INT32_MIN;
    bool decoded = true;
    char const* name = "";

    switch( packet_type )
    {
    case PKT_NAME_VARCLAN_ENABLE:
        RS_ClanStore_VarClanEnable(&host->clan);
        return;
    case PKT_NAME_VARCLAN_DISABLE:
        RS_ClanStore_VarClanDisable(&host->clan);
        return;
    case PKT_NAME_VARCLAN:
        name = "VARCLAN";
        decoded = RS_ClanStore_ApplyVarClan(
            &host->clan, payload->data, payload->length, exec_varclan_type_of, host);
        break;
    case PKT_NAME_CLANCHANNEL_FULL:
        name = "CLANCHANNEL_FULL";
        decoded = RS_ClanStore_ApplyChannelFull(&host->clan, payload->data, payload->length);
        host->clan_channel_transmit_dirty = 1;
        break;
    case PKT_NAME_CLANCHANNEL_DELTA:
        name = "CLANCHANNEL_DELTA";
        decoded = RS_ClanStore_ApplyChannelDelta(
            &host->clan, payload->data, payload->length, &request_full);
        host->clan_channel_transmit_dirty = 1;
        if( request_full != INT32_MIN )
            exec_clan_request_full(host, RS_CS2_SOCIAL_SEND_CLANCHANNEL_FULL_REQUEST, request_full);
        break;
    case PKT_NAME_CLANSETTINGS_FULL:
        name = "CLANSETTINGS_FULL";
        decoded = RS_ClanStore_ApplySettingsFull(&host->clan, payload->data, payload->length);
        host->clan_settings_transmit_dirty = 1;
        break;
    case PKT_NAME_CLANSETTINGS_DELTA:
        name = "CLANSETTINGS_DELTA";
        decoded = RS_ClanStore_ApplySettingsDelta(
            &host->clan, payload->data, payload->length, &request_full);
        host->clan_settings_transmit_dirty = 1;
        if( request_full != INT32_MIN )
            exec_clan_request_full(
                host, RS_CS2_SOCIAL_SEND_CLANSETTINGS_FULL_REQUEST, request_full);
        break;
    default:
        assert(!"exec_clan: not a clan packet");
        return;
    }
    if( !decoded )
        TORIRS_ERR("gameproto_exec: %s did not decode (%d bytes)\n", name, payload->length);
}

static void
exec_message_clanchannel(
    struct AppGameProtoCtx const* ctx,
    bool system,
    struct PktMessageClanChannel const* message)
{
    assert(ctx);
    assert(ctx->app);
    assert(message);
    struct App* app = ctx->app;
    struct RS_ClanStore const* clan = &app->host.clan;
    struct RS_ClanChannel const* channel =
        message->clan_type >= 0
            ? (message->clan_type < RS_CLAN_AFFINED_SLOTS ? clan->affined_channel[message->clan_type]
                                                          : NULL)
            : clan->listened_channel;
    /* A real "not ready" case, dropped on purpose: a line for a clan channel
     * this client has not been sent (or has left) has nowhere to go. The
     * reference drops it the same way (deob client.java, class243.field3061:
     * `var74 == null` marks it seen-and-skipped). */
    if( !channel )
        return;

    int64_t const key = ((int64_t)message->world << 32) + message->counter;
    if( RS_Social_MessageSeen(&app->social, key) )
        return;

    if( !system )
    {
        int ignorable;
        RS_Chat_CrownImage(message->crown, &ignorable);
        if( ignorable && RS_Social_IsIgnored(&app->social, message->sender) )
            return;
    }
    RS_Social_MessageRemember(&app->social, key);

    char const* const text = message->text ? message->text : "";
    if( system )
    {
        /* The reference stores the clan's name where a player line keeps its
         * channel prefix; this store keeps that in `sender`. */
        int const type = message->clan_type >= 0 ? RS_CHAT_TYPE_CLAN_MESSAGE : RS_CHAT_TYPE_CLAN_GUEST_MESSAGE;

        exec_chat_add(ctx, type, "", channel->name, text);
        App_NotifyChatMessage(app, type, channel->name, text);
        return;
    }
    char escaped[RS_CHAT_TEXT_LEN];
    char name[RS_CHAT_SENDER_LEN];
    RS_ServerState_EscapeTags(text, escaped, sizeof(escaped));
    RS_Chat_CrownedName(message->crown, message->sender, name, sizeof(name));
    {
        int const type = message->clan_type >= 0 ? RS_CHAT_TYPE_CLAN_CHAT : RS_CHAT_TYPE_CLAN_GUEST_CHAT;

        exec_chat_add(ctx, type, name, channel->name, escaped);
        App_NotifyChatMessage(app, type, message->sender, text);
    }
}

/* deob class630.method13772: clientscript 85 hears a group arrive or leave. */
static void
exec_group_notify(void* user, int def_id, int index, int64_t uid)
{
    struct App* app = user;
    struct PktRunClientScript request;

    memset(&request, 0, sizeof(request));
    request.script_id = 85;
    request.argc = 3;
    request.intv[0] = def_id;
    request.intv[1] = index;
    request.long_mask = 1u << 2;
    request.longv[2] = uid;
    App_RunClientScript(app, &request);
}

/*
 * CACHE_INVALIDATE, the live reload's control frame (net/torirs_control.h): it
 * rebuilt these archives. What is resident and can be dropped without pulling
 * it from under live state is dropped (CacheProvider_InvalidateArchive):
 * interface packs (3), the loc, npc and obj records (2/6, 2/9, 2/10) and the
 * map squares (5), whose IO-layer copies go through the provider's archive
 * hook. Placed scenery needs nothing more: the server owes a REBUILD after a
 * square or a loc-record reload, sent after this frame on the same connection,
 * and the rebuilt scene re-reads the squares and every placed loc's record.
 */
static void
exec_control_cache_invalidate(
    struct AppGameProtoCtx const* ctx,
    struct TorirsControlFrame const* frame)
{
    int index;
    int count;
    int evicted = 0;
    int deferred = 0;

    if( !ctx->app->provider )
        return;
    if( !torirs_control_invalidate_decode(frame, &index, &count) )
    {
        TORIRS_ERR("control: CACHE_INVALIDATE of %d bytes does not decode -- dropped\n",
                   frame->length);
        return;
    }
    for( int i = 0; i < count; i++ )
    {
        enum CacheProviderInvalidate done = CacheProvider_InvalidateArchive(
            ctx->app->provider, index, torirs_control_invalidate_archive(frame, i));

        evicted += done == CACHE_PROVIDER_INVALIDATE_EVICTED;
        deferred += done == CACHE_PROVIDER_INVALIDATE_DEFERRED;
    }
    /* The REBUILD the server owes after a square or loc-record reload names
     * the zone the client already holds, and the same-zone early-out
     * (task_gameproto_exec.c, deob method3310) would skip it: force it, so the
     * scene is built again from the rebuilt squares and the re-decoded loc
     * records. */
    if( index == 5 || (index == 2 && evicted > 0) )
        RS_RebuildRegion_Force(&ctx->app->rebuild_region);
    if( getenv("TORIRS_CACHE_INVALIDATE_DEBUG") )
        TORIRS_REPORT("cache invalidate: index %d, %d archive(s): %d dropped, %d until the next "
                      "login\n",
                      index, count, evicted, deferred);
    else
        TORIRS_LOG("cache invalidate: index %d, %d archive(s): %d dropped, %d until the next "
                   "login\n",
                   index, count, evicted, deferred);
}

/* TORIRS_TRACE_NATIVE_UI, read once: the two packets that ask arrive by the
 * thousand on a replay seek, and getenv walks the environment each time. */
static int
exec_trace_native_ui(void)
{
    static int on = -1;

    if( on < 0 )
        on = getenv("TORIRS_TRACE_NATIVE_UI") != NULL;
    return on;
}

static void
exec_control(
    struct AppGameProtoCtx const* ctx,
    struct TorirsControlFrame const* frame)
{
    if( frame->type == TORIRS_CONTROL_CACHE_INVALIDATE )
    {
        exec_control_cache_invalidate(ctx, frame);
        return;
    }
    if( torirs_control_type_is_replay(frame->type) )
    {
        App_ReplayControlExec(ctx->app, frame);
        return;
    }
    TORIRS_ERR("control: unknown frame type %d (%d bytes) -- dropped\n", frame->type, frame->length);
}

static void
exec_varp_small(
    struct AppGameProtoCtx const* ctx,
    struct PktVarpSmall const* p)
{
    (void)ctx;
    if( exec_trace_native_ui() )
        TORIRS_REPORT("NATIVE_PACKET VARP_SMALL variable=%d value=%d\n",
            p->variable, p->value);
}

static void
exec_update_stat(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateStat const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
    {
        struct RS_PlayerStats const* stats = ctx->state->stats;

        if( exec_trace_native_ui() )
            TORIRS_REPORT("NATIVE_PACKET UPDATE_STAT applied stat=%d level=%d xp=%d\n",
                p->stat, stats->current_level[p->stat],
                p->xp);
        /* A level-up is read from the BASE level SetXp just derived, not
         * from the boosted one on the wire: a potion raises that and a
         * drain lowers it, and neither is an advance. */
        App_NotifyStatLevel(
            ctx->app,
            p->stat,
            stats->base_level[p->stat]);
        /* The reactive half. Without it the skills tab paints once at
         * build time — against the zeroes the client starts with — and
         * nothing ever asks it to paint again, which is the same shape as
         * the inv-transmit gap that left server-driven inventories blank. */
        RS_CS2Host_NotifyStatChanged(&ctx->app->host, p->stat);
    }
}

/* The reactive half, same shape as UPDATE_STAT above: the orb's CS2 binds
 * IF_SETONMISCTRANSMIT, so without a notify it paints its build-time value
 * and is only ever refreshed by an unrelated interface event. Gated on the
 * value actually changing (the model answers UNCHANGED otherwise) because
 * the dispatch walks every component — and the server already change-gates
 * these packets, so an unchanged value arriving at all is unusual. */
static void
exec_run_misc_changed(struct AppGameProtoCtx const* ctx)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
        RS_CS2Host_NotifyMiscChanged(&ctx->app->host);
}

/* Containers arrive long after the interface that paints them was built,
 * so the CS2 paint script has to be told to run again. Obj icons upload
 * lazily when the bound inv view next renders (same path as the
 * RevConfig-seeded containers). inv_packet is the model's stamp. */
static void
exec_update_inv_full(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateInvFull const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
    {
        int const container = RS_ServerState_InvContainerId(p->inv_id, p->component_id);

        RS_CS2Host_NotifyInvChanged(
            &ctx->app->host, p->inv_id > 0 ? container & 0x7fff : container);
    }
}

static void
exec_update_inv_partial(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateInvPartial const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED && p->count > 0 )
    {
        int const container = RS_ServerState_InvContainerId(p->inv_id, p->component_id);

        RS_CS2Host_NotifyInvChanged(
            &ctx->app->host, p->inv_id > 0 ? container & 0x7fff : container);
    }
}

/* deob client.java (class243.field2979): the inventory is unlinked and its
 * id goes onto the same changed-inventory ring UPDATE_INV_FULL feeds
 * (field733.method9432), so the inventory's transmit hooks run and redraw
 * the emptied slots. */
static void
exec_update_inv_stop_transmit(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateInvStopTransmit const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
    {
        int const container = RS_ServerState_InvContainerId(p->inv_id, p->component_id);

        RS_CS2Host_NotifyInvChanged(
            &ctx->app->host, p->inv_id > 0 ? container & 0x7fff : container);
    }
}

/* The store write is the model's; these put the value on the node when
 * the node is mounted (App_If*Show), and the mount puts it there again. */
static void
exec_if_settext(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetText const* p)
{
    App_IfTextShow(ctx->app, p->component_id, p->text);
    /* A quest completion is painted onto the scroll and never said
     * aloud (content: questscroll.rs2), so this is the only place the
     * client can see one happen. */
    App_NotifyInterfaceText(ctx->app, p->text);
}

static void
exec_if_setevents(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetEvents const* p)
{
    /* Persisting, like IF_SETHIDE: the server enables a component's events
     * before the interface holding it has finished mounting. */
    App_IfEventsShow(
        ctx->app,
        p->component_id,
        p->from,
        p->to,
        p->events);
}

static void
exec_if_sethide(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetHide const* p)
{
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
        TORIRS_REPORT("NATIVE_PACKET IF_SETHIDE received com=%d hide=%d\n",
            p->component_id, p->hide);
    App_IfHideShow(ctx->app, p->component_id, p->hide);
}

static void
exec_if_setcolour(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetColour const* p)
{
    /* Persisting, like IF_SETTEXT and IF_SETHIDE above, and for the same
     * reason: a colour written in the tick its interface is mounted has no
     * node to land on yet. This applied straight to the tree, so it was the
     * one setter of the family that could be silently dropped — and it was,
     * every time, for an overlay the server opens and colours together. */
    App_IfColourShow(
        ctx->app,
        p->component_id,
        RS_ServerState_Rs15ToRgb(p->colour));
}

static void
exec_if_setmodel(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetModel const* p)
{
    /* A wire model id addresses the cache, not the renderer's scene. The
     * App owns the asynchronous cache-load/upload bridge and persistence
     * across an interface remount. */
    App_SetInterfaceModel(
        ctx->app,
        p->component_id,
        p->model_id);
}

static void
exec_if_setobject(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetObject const* p)
{
    /* Reference IfType.getModel type 4: the obj's 3D interface model (the
     * server drives the combat-tab weapon display with this on equip). */
    App_SetInterfaceObjModel(
        ctx->app,
        p->component_id,
        p->obj_id,
        p->zoom);
}

static void
exec_if_setnpchead(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetNpcHead const* p)
{
    /* Reference IfType.getModel type 2: composite the npc chathead and bind
     * it. Needs the App (async model loads via the exec runner). */
    App_SetInterfaceNpcHead(
        ctx->app, p->component_id, p->npc_id);
}

static void
exec_if_setplayerhead(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetPlayerHead const* p)
{
    /* Reference IfType.getModel type 3: local-player chathead. */
    App_SetInterfacePlayerHead(ctx->app, p->component_id);
}

static void
exec_if_setanim(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetAnim const* p)
{
    /* Reference modelAnim: the chathead's talk/idle sequence. Persist via the
     * App (survives the chat interface mounting after the packet). */
    App_SetInterfaceModelAnim(
        ctx->app, p->component_id, p->anim_id);
}

static void
exec_if_setnpchead_active(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetNpcHeadActive const* p)
{
    if( ctx->app->world )
    {
        int world_idx;
        if( RS_EntitySync_FindNpc(
                &ctx->app->esync,
                p->index,
                &world_idx,
                NULL) )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&ctx->app->world->entities.npc, world_idx);
            if( npc )
                App_SetInterfaceNpcHead(
                    ctx->app,
                    p->component_id,
                    npc->npc_id);
        }
    }
}

static void
exec_if_setplayermodel_basecolour(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetPlayerModel const* p)
{
    App_SetInterfacePlayerModelBaseColour(
        ctx->app,
        p->component_id,
        p->index,
        p->value);
}

static void
exec_if_setplayermodel_bodytype(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetPlayerModel const* p)
{
    App_SetInterfacePlayerModelBodyType(
        ctx->app,
        p->component_id,
        p->value);
}

static void
exec_if_setplayermodel_obj(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetPlayerModel const* p)
{
    App_SetInterfacePlayerModelObj(
        ctx->app,
        p->component_id,
        p->value);
}

static void
exec_if_setplayermodel_self(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetPlayerModel const* p)
{
    App_SetInterfacePlayerModelSelf(
        ctx->app,
        p->component_id,
        p->value);
}

static void
exec_if_openmain(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenMain const* p)
{
    RS_UISlots_OpenMain(ctx->app, p->component_id);
}

static void
exec_if_openside(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenSide const* p)
{
    RS_UISlots_OpenSide(ctx->app, p->component_id);
}

static void
exec_if_openmain_side(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenMainSide const* p)
{
    RS_UISlots_OpenMainSide(
        ctx->app,
        p->main_component_id,
        p->side_component_id);
}

static void
exec_if_openchat(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenChat const* p)
{
    RS_UISlots_OpenChat(ctx->app, p->component_id);
}

static void
exec_if_close(struct AppGameProtoCtx const* ctx)
{
    RS_UISlots_CloseModal(ctx->app);
}

static void
exec_if_settab(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetTab const* p)
{
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
        TORIRS_REPORT("NATIVE_PACKET IF_SETTAB received tab=%d com=%d\n",
            p->tab_id, p->component_id);
    RS_UISlots_SetTab(ctx->app, p->tab_id, p->component_id);
}

static void
exec_if_opentop(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenTop const* p)
{
    /* Switch the gameframe root when the server asks for a different top
     * (Display Fixed/Classic/Modern). Reject non-positive ids — a live
     * server once sent 0 and wiped the tree. Same-id is a no-op. */
    int top = p->interface_id;
    int cur = RS_BootState_InterfaceId(&ctx->app->boot);
    if( top <= 0 )
        return;
    if( top == cur )
        return;
    TORIRS_LOG("if-opentop: switching root %d -> %d\n", cur, top);
    /*
     * The IF_SETEVENTS arming survives the switch.
     *
     * The reference's open-top (rev 239 class415.method9502, rev 241
     * class420.method9711 + method9723) unloads the old root's widgets and
     * loads the new one; it never touches the flags table. Only IF_RESYNC
     * replaces that table, and a closed group loses its own entries
     * (RS_ServerState_SlotGroupLeaves). The subs IF_MOVESUB carries into the new top
     * keep their masks: Jagex arms the backpack once at login and the
     * welcome screen's Play then opens the gameframe top without arming it
     * again (RSProx index 4378: IF_SETEVENTS 149:0 at line 644, IF_OPENTOP
     * at 782, an op on a backpack item at 4258). Clearing here left every
     * backpack slot without its target bits, so no item offered "Use".
     */
    App_OpenRootInterface(ctx->app, top);
}

static void
exec_if_opensub(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenSub const* p)
{
    if( torirs_env_net_debug() )
        TORIRS_LOG("if-opensub: iface=%d target=0x%08x (%d<<16|%d) type=%d\n",
            p->interface_id,
            (unsigned)p->target_uid,
            (p->target_uid >> 16) & 0xffff,
            p->target_uid & 0xffff,
            p->type);
    App_OpenSubInterface(
        ctx->app,
        p->target_uid,
        p->interface_id,
        p->type);
}

static void
exec_if_closesub(
    struct AppGameProtoCtx const* ctx,
    struct PktIfCloseSub const* p)
{
    App_CloseSubInterface(ctx->app, p->target_uid);
}

static void
exec_if_movesub(
    struct AppGameProtoCtx const* ctx,
    struct PktIfMoveSub const* p)
{
    /* Wire data: a component uid is (group << 16) | child, never negative.
     * One that is names no slot either side of the move, so the packet is
     * refused here, in one place, with a line. */
    if( p->source_uid < 0 || p->dest_uid < 0 )
    {
        TORIRS_ERR("gameproto_exec: IF_MOVESUB 0x%08x -> 0x%08x names no component, refused\n",
            (unsigned)p->source_uid,
            (unsigned)p->dest_uid);
        return;
    }
    App_MoveSubInterface(
        ctx->app, p->source_uid, p->dest_uid);
}

static void
exec_if_resync_v2(
    struct AppGameProtoCtx const* ctx,
    struct PktIfResync const* sync)
{
    struct App* app = ctx->app;
    int root_changed = sync->root_interface_id > 0 &&
                       sync->root_interface_id != RS_BootState_InterfaceId(&app->boot);
    struct UITreeInterfaceParent live[UITREE_INTERFACE_PARENT_MAX];
    unsigned char* resync_reachable;
    int live_count = 0;

    if( root_changed )
        App_OpenRootInterface(app, sync->root_interface_id);
    resync_reachable = ToriMem_Calloc(
        ToriMem_Process(), (size_t)(sync->mount_count > 0 ? sync->mount_count : 1), 1);
    assert(resync_reachable);

    /*
     * RECONCILE against what is already mounted; do not close and
     * reopen the lot.
     *
     * The snapshot is the server's mount registry, so a slot it
     * restates with the same group and type is a slot the client
     * already has right — and closing it is destructive, not neutral.
     * A close reclaims the whole widget group (App_CloseSubInterface),
     * which throws away everything the CS2 scripts built on top of the
     * cache pack: hidden/shown layers, dynamic children, the lot. The
     * reopen re-bakes from the pack and only re-runs that pack's OWN
     * onload, so anything configured from outside it stays lost.
     *
     * Measured on the Display panel's layout switch, which is the only
     * thing that sends this packet: `ToriRSServer_GameframeOpentop`
     * remounts the frame and then commits the same registry here. The
     * toplevel's onload had already picked the popout strip's
     * collapsed variant (`popout` 728: layer 4 shown, layer 7 hidden)
     * and created its three panel buttons as dynamic children of
     * 728:6; the blanket close threw all of it away and the reopen
     * came back at the cache defaults — a black strip with no buttons.
     *
     * Closing what the snapshot does NOT restate is still this
     * packet's job, and that is what removes the stale mounts an
     * interrupted layout switch leaves behind.
     */
    live_count = ctx->tree->interface_parent_count;
    if( live_count > UITREE_INTERFACE_PARENT_MAX )
        live_count = UITREE_INTERFACE_PARENT_MAX;
    memcpy(live, ctx->tree->interface_parents, (size_t)live_count * sizeof(live[0]));
    for( int i = 0; i < live_count; i++ )
    {
        int keep = 0;
        for( int j = 0; !keep && j < sync->mount_count; j++ )
            keep = sync->mounts[j].target_uid == live[i].container_uid &&
                   sync->mounts[j].interface_id == live[i].group_id &&
                   sync->mounts[j].type == live[i].type;
        if( !keep )
            App_CloseSubInterface(app, live[i].container_uid);
    }
    App_IfEventsClear(app);
    /* The snapshot is one table, and it can state a mount in a slot
     * the live top does not have: the server keeps what a root switch
     * did not move (the phone's hotkey column while the world map's
     * top is up). Those are kept, not opened -- their slot is not in
     * this tree -- and a kept record the snapshot does not restate is
     * gone. Reachable = under the root, or inside a reachable group. */
    {
        int const root = sync->root_interface_id > 0 ? sync->root_interface_id
                                                     : RS_BootState_InterfaceId(&app->boot);
        int grew = 1;

        while( grew )
        {
            grew = 0;
            for( int i = 0; i < sync->mount_count; i++ )
            {
                int const slot_group = (int)((uint32_t)sync->mounts[i].target_uid >> 16);
                int found = slot_group == root;

                if( resync_reachable[i] )
                    continue;
                for( int j = 0; !found && j < sync->mount_count; j++ )
                    found = resync_reachable[j] && sync->mounts[j].interface_id == slot_group;
                if( found )
                {
                    resync_reachable[i] = 1;
                    grew = 1;
                }
            }
        }
        App_KeptMountsClear(app);
        for( int i = 0; i < sync->mount_count; i++ )
            if( !resync_reachable[i] && sync->mounts[i].interface_id > 0 )
                App_KeptMountAdd(
                    app,
                    sync->mounts[i].target_uid,
                    sync->mounts[i].interface_id,
                    sync->mounts[i].type);
    }
    for( int i = 0; i < sync->mount_count; i++ )
    {
        int mounted = 0;
        for( int j = 0; !mounted && j < live_count; j++ )
            mounted = live[j].container_uid == sync->mounts[i].target_uid &&
                      live[j].group_id == sync->mounts[i].interface_id &&
                      live[j].type == sync->mounts[i].type;
        /* U7: a root change has already queued the kept mounts of
         * the new top (App_RootMountsReturn), and the live snapshot
         * it emptied cannot say so. A second mount task for the same
         * group would remount it and drop its arming. */
        if( !mounted )
            mounted = App_SlotMountQueued(
                app,
                sync->mounts[i].target_uid,
                sync->mounts[i].interface_id,
                sync->mounts[i].type);
        if( mounted || !resync_reachable[i] )
            continue;
        App_OpenSubInterface(
            app,
            sync->mounts[i].target_uid,
            sync->mounts[i].interface_id,
            sync->mounts[i].type);
    }
    for( int i = 0; i < sync->event_count; i++ )
    {
        uint32_t mask = sync->events[i].events1 |
            ((sync->events[i].events2 & UINT32_C(0x3ff)) << 1);
        App_IfEventsSet(
            app,
            sync->events[i].component_id,
            sync->events[i].from,
            sync->events[i].to,
            (int)mask);
    }
    ToriMem_Free(ToriMem_Process(), resync_reachable);
}

static void
exec_runclientscript(
    struct AppGameProtoCtx const* ctx,
    struct RevPacket* packet)
{
    App_RunClientScript(ctx->app, packet->_runclientscript);
}

static void
exec_message_game(
    struct AppGameProtoCtx const* ctx,
    struct PktMessageGame const* p)
{
    exec_chat_presented(ctx);
    /* Independent of the chatbox: a headless run has no chat model, and a
     * plugin watching for a boss kill has to work there too. */
    App_NotifyChatMessage(
        ctx->app,
        p->type,
        p->name,
        p->text);
}

static void
exec_rebuild(
    struct AppGameProtoCtx const* ctx,
    struct PktMapRebuild const* p)
{
    (void)ctx;
    /* Handled inside Task_GameProtoExec (world-load await + MAP_BUILD_
     * COMPLETE ack); reaching here means the packet was executed directly,
     * not through that task. */
    if( torirs_env_net_debug() )
        TORIRS_LOG("gameproto_exec: %s zone=%d,%d (not through the exec task)\n",
            p->zones ? "REBUILD_REGION" : "REBUILD_NORMAL",
            p->zonex,
            p->zonez);
}

static void
exec_entity_info_dropped(
    struct AppGameProtoCtx const* ctx,
    struct RevPacket* packet)
{
    (void)ctx;
    /* Consumed by Task_GameProtoExec's awaited entity-info tasks once a
     * scene is loaded (WorldLoadState_Active). Reaching here is the one
     * real "not ready" case: no REBUILD has built a scene this session
     * yet, and every coordinate in the stream is relative to the scene
     * base the rebuild sets, so there is nothing to place against. The
     * server sends REBUILD before the first info packet of a session, so
     * this is a stray packet, and it is dropped on purpose. */
    if( torirs_env_net_debug() )
        TORIRS_LOG("gameproto_exec: entity info packet %d dropped (no active world)\n",
            packet->packet_type);
}

static void
exec_update_zone_partial_follows(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateZonePartialFollows const* p)
{
    /* Wire base is classic-scene local; with a (zone-6)*8 scene base
     * that is our-scene too. */
    zone_set_base(
        ctx->app,
        p->base_x,
        p->base_z,
        zone_header_level(ctx->app, p->level));
}

static void
exec_update_zone_full_follows(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateZoneFullFollows const* p)
{
    struct App* app = ctx->app;
    zone_set_base(
        app,
        p->base_x,
        p->base_z,
        zone_header_level(app, p->level));
    /* Full update: the zone's client-side locs and obj stacks reset --
     * in arrival order with the packets around it: held behind any
     * packet still held for the view, or for its scene to be built. */
    if( app->world )
    {
        struct ZoneAt at = zone_at_now(app);

        zone_exec_or_hold(ctx, PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, NULL, &at);
    }
}

static void
exec_update_zone_partial_enclosed(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateZoneEnclosed const* enc)
{
    /* Wire base is classic-scene local; with a (zone-6)*8 scene base
     * that is our-scene too. */
    zone_set_base(
        ctx->app,
        enc->base_x,
        enc->base_z,
        zone_header_level(ctx->app, enc->level));
    for( int i = 0; i < enc->count; i++ )
        exec_zone_sub_packet(ctx, enc->entries[i].name, &enc->entries[i]._loc_add_change);
}

/* The seq and spotanim are the model's; the held models and the
 * spotanim's asset are the client's to fetch. */
static void
exec_anim_specific(
    struct AppGameProtoCtx const* ctx,
    struct PktAnimSpecific const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED && p->id >= 0 )
        EntityAssets_WantPlayerHeldForPid(
            ctx->app, RS_ServerState_LocalPid(ctx->state->esync), p->id);
}

static void
exec_entity_spotanim_specific(
    struct AppGameProtoCtx const* ctx,
    struct PktEntitySpotanimSpecific const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED &&
        p->id >= 0 )
        App_EntitySpotanimPrefetch(ctx->app, p->id);
}

static void
exec_cam_moveto(
    struct AppGameProtoCtx const* ctx,
    struct PktCamMoveTo const* p)
{
    struct App* app = ctx->app;
    /* Classic wire coords are scene-local tiles; the V2 packet's are
     * absolute world tiles (packet->absolute), converted here against
     * the scene the shot plays in. These only record where the camera
     * is headed; App_CinemaCamera walks it there. */
    int cam_lx = p->local_x;
    int cam_lz = p->local_z;
    if( p->absolute && app->world )
    {
        cam_lx -= app->world->_base_tile_x;
        cam_lz -= app->world->_base_tile_z;
    }
    /* The look half is left as it is (deob client.java, class243.field3026):
     * after a CAM_RESET its rates are 0, so the angles hold. */
    if( CamScript_MoveTo(
            &app->cam_script,
            cam_lx,
            cam_lz,
            p->height,
            p->rate,
            p->rate2) )
        App_CinemaCameraSnapPosition(app);
    exec_cam_script_record(
        app,
        CAM_SCRIPT_OP_MOVETO,
        cam_lx + (app->world ? app->world->_base_tile_x : 0),
        cam_lz + (app->world ? app->world->_base_tile_z : 0),
        p->height,
        p->rate,
        p->rate2);
    RedrawRequest_Raise(&app->redraw_request);
}

static void
exec_cam_lookat(
    struct AppGameProtoCtx const* ctx,
    struct PktCamLookAt const* p)
{
    struct App* app = ctx->app;
    /* Same absolute-vs-scene-local split as CAM_MOVETO above. */
    int cam_lx = p->local_x;
    int cam_lz = p->local_z;
    if( p->absolute && app->world )
    {
        cam_lx -= app->world->_base_tile_x;
        cam_lz -= app->world->_base_tile_z;
    }
    /* The move half is left as it is (deob client.java, class243.field3073):
     * after a CAM_RESET its rates are 0, so the eye holds where it was
     * -- Wanted!'s scans turn to Solus without moving (RSProx 4464
     * [2833]). */
    if( CamScript_LookAt(
            &app->cam_script,
            cam_lx,
            cam_lz,
            p->height,
            p->rate,
            p->rate2) )
        App_CinemaCameraSnapAngle(app);
    exec_cam_script_record(
        app,
        CAM_SCRIPT_OP_LOOKAT,
        cam_lx + (app->world ? app->world->_base_tile_x : 0),
        cam_lz + (app->world ? app->world->_base_tile_z : 0),
        p->height,
        p->rate,
        p->rate2);
    RedrawRequest_Raise(&app->redraw_request);
}

static void
exec_cam_moveto_cycles(
    struct AppGameProtoCtx const* ctx,
    struct PktCamMoveToCycles const* p)
{
    if( ctx->app->world )
    {
        struct App* app = ctx->app;
        /* World tiles from v2 on; v0/v1 (222-236) send build-area tiles. */
        int cam_lx = p->x -
                     (p->absolute ? app->world->_base_tile_x : 0);
        int cam_lz = p->z -
                     (p->absolute ? app->world->_base_tile_z : 0);
        App_CinemaCameraMoveCycles(
            app,
            cam_lx,
            cam_lz,
            p->height,
            p->cycles,
            p->ignore_terrain,
            p->easing,
            p->height_relative);
        exec_cam_script_record(
            app,
            CAM_SCRIPT_OP_MOVETO_CYCLES,
            cam_lx + app->world->_base_tile_x,
            cam_lz + app->world->_base_tile_z,
            p->height,
            p->cycles,
            p->easing);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

/* deob client.java class259.field3190: signed deltas from where the camera
 * is. Recorded as yaw, pitch, cycles in height, rate, rate2. */
static void
exec_cam_rotateby(
    struct AppGameProtoCtx const* ctx,
    struct PktCamRotate const* p)
{
    if( ctx->app->world )
    {
        App_CinemaCameraRotateBy(
            ctx->app,
            p->yaw,
            p->pitch,
            p->cycles,
            p->easing);
        exec_cam_script_record(
            ctx->app,
            CAM_SCRIPT_OP_ROTATEBY,
            -1,
            -1,
            p->yaw,
            p->pitch,
            p->cycles);
        RedrawRequest_Raise(&ctx->app->redraw_request);
    }
}

/* deob client.java class259.field3187. */
static void
exec_cam_rotateto(
    struct AppGameProtoCtx const* ctx,
    struct PktCamRotate const* p)
{
    if( ctx->app->world )
    {
        App_CinemaCameraRotateTo(
            ctx->app,
            p->yaw,
            p->pitch,
            p->cycles,
            p->easing);
        exec_cam_script_record(
            ctx->app,
            CAM_SCRIPT_OP_ROTATETO,
            -1,
            -1,
            p->yaw,
            p->pitch,
            p->cycles);
        RedrawRequest_Raise(&ctx->app->redraw_request);
    }
}

static void
exec_cam_lookat_cycles(
    struct AppGameProtoCtx const* ctx,
    struct PktCamLookAtCycles const* p)
{
    if( ctx->app->world )
    {
        struct App* app = ctx->app;
        struct PktCamLookAtCycles const* l = p;

        CamScript_LookCycles(
            &app->cam_script,
            l->x - app->world->_base_tile_x,
            l->z - app->world->_base_tile_z,
            l->height,
            l->height_relative,
            app->world->cycle,
            l->cycles,
            l->easing);
        exec_cam_script_record(
            app, CAM_SCRIPT_OP_LOOKAT_CYCLES, l->x, l->z, l->height, l->cycles, l->easing);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

static void
exec_cam_moveto_arc(
    struct AppGameProtoCtx const* ctx,
    struct PktCamMoveToArc const* p)
{
    if( ctx->app->world )
    {
        struct App* app = ctx->app;
        struct PktCamMoveToArc const* a = p;
        int base_x = a->absolute ? app->world->_base_tile_x : 0;
        int base_z = a->absolute ? app->world->_base_tile_z : 0;

        App_CinemaCameraMoveArc(
            app,
            a->x - base_x,
            a->z - base_z,
            a->via_x - base_x,
            a->via_z - base_z,
            a->height,
            a->cycles,
            a->ignore_terrain,
            a->easing,
            a->height_relative);
        exec_cam_script_record(
            app,
            CAM_SCRIPT_OP_MOVETO_ARC,
            a->x - base_x + app->world->_base_tile_x,
            a->z - base_z + app->world->_base_tile_z,
            a->height,
            a->cycles,
            a->easing);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

static void
exec_cam_rotateto_coordinate(
    struct AppGameProtoCtx const* ctx,
    struct PktCamRotateToCoordinate const* p)
{
    if( ctx->app->world )
    {
        struct App* app = ctx->app;
        int cam_lx = p->x -
                     (p->absolute ? app->world->_base_tile_x : 0);
        int cam_lz = p->z -
                     (p->absolute ? app->world->_base_tile_z : 0);
        App_CinemaCameraRotateCycles(
            app,
            cam_lx,
            cam_lz,
            p->height,
            p->cycles,
            p->easing,
            p->height_relative);
        exec_cam_script_record(
            app,
            CAM_SCRIPT_OP_ROTATETO_COORDINATE,
            cam_lx + app->world->_base_tile_x,
            cam_lz + app->world->_base_tile_z,
            p->height,
            p->cycles,
            p->easing);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

static void
exec_cam_shake(
    struct AppGameProtoCtx const* ctx,
    struct PktCamShake const* p)
{
    /* The wire order is axis, spread, amplitude, rate — the packet's
     * field names predate knowing which was which. */
    int axis = p->axis;
    if( axis >= 0 && axis < CAM_SCRIPT_SHAKE_AXES )
        CamScript_Shake(
            &ctx->app->cam_script,
            axis,
            p->amplitude,
            p->frequency,
            p->speed);
    /* Recorded whatever the axis: an out-of-range shake is still a
     * packet the server sent, and the serial counts every one. */
    exec_cam_script_record(
        ctx->app,
        CAM_SCRIPT_OP_SHAKE,
        -1,
        -1,
        p->frequency,
        axis,
        p->speed);
}

static void
exec_cam_reset(struct AppGameProtoCtx const* ctx)
{
    struct App* app = ctx->app;
    /* Deob client.java, class243.field2937: both targets, heights and
     * rates to 0, not just the flag. A stale move rate would carry the
     * last scene's eye into the next look-at-only scene. */
    CamScript_Reset(&app->cam_script);
    exec_cam_script_record(app, CAM_SCRIPT_OP_RESET, -1, -1, 0, 0, 0);
    RedrawRequest_Raise(&app->redraw_request);
}

static void
exec_cam_smoothreset(
    struct AppGameProtoCtx const* ctx,
    struct PktCamSmoothReset const* p)
{
    struct App* app = ctx->app;
    /* Deob client.java, class243.field2963 `&& field904`: only a
     * script that is up can glide back. The targets and rates stay;
     * app_world_camera_follow walks the eye home with them, and
     * ending it is CamScript_GlideEnd's. Every CAM_MOVETO or
     * CAM_LOOKAT after this cancels the glide (field698 = false). */
    if( CamScript_SmoothReset(&app->cam_script) )
        RedrawRequest_Raise(&app->redraw_request);
    exec_cam_script_record(
        app,
        CAM_SCRIPT_OP_SMOOTHRESET,
        -1,
        -1,
        0,
        p->move_constant_speed,
        p->move_proportional_speed);
}

/* deob client.java class243.field2935 (V3) / field3045 (V4): field2521 is
 * the type, field774 the index, and the camera's focus is that entity
 * (client.method1700, Statics.method8560) until a login puts the local
 * player back. app_world_camera_follow reads it. */
static void
exec_cam_target(
    struct AppGameProtoCtx const* ctx,
    struct PktCamTarget const* p)
{
    if( CamScript_SetTarget(&ctx->app->cam_script, p->type, p->index) )
        RedrawRequest_Raise(&ctx->app->redraw_request);
}

/* The friends panel's rows are cc_created from the store by the client's
 * own script 125, so nothing repaints them but the friend-transmit
 * channel. Filling the store without this notify leaves the panel showing
 * whatever it drew at mount. Script 681 reads the chat filter modes back
 * through chat_getfilter_private before it will send a private message,
 * so a filter change reaches the CS2 side the same way. */
static void
exec_friend_changed(struct AppGameProtoCtx const* ctx)
{
    RS_CS2Host_NotifyFriendChanged(&ctx->app->host);
    RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_message_private(
    struct AppGameProtoCtx const* ctx,
    struct PktMessagePrivate const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
    {
        char name[16];

        exec_chat_presented(ctx);
        base37tostr((uint64_t)p->from, name, sizeof(name));
        App_NotifyChatMessage(
            ctx->app,
            p->staff_mod ? RS_CHAT_TYPE_PRIVATE_FROM_MOD
                                               : RS_CHAT_TYPE_PRIVATE_FROM,
            name,
            p->text);
    }
}

/* Plugins hear every chat line the store takes, as they hear MESSAGE_GAME
 * and MESSAGE_PRIVATE: RuneLite's ChatMessage fires for every line the
 * client adds, whichever channel. */
static void
exec_message_private_echo(
    struct AppGameProtoCtx const* ctx,
    struct PktMessagePrivateEcho const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
    {
        exec_chat_presented(ctx);
        App_NotifyChatMessage(
            ctx->app,
            RS_CHAT_TYPE_PRIVATE_TO,
            p->recipient,
            p->text ? p->text : "");
    }
}

static void
exec_message_friendchannel(
    struct AppGameProtoCtx const* ctx,
    struct PktMessageFriendChannel const* p)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
    {
        exec_chat_presented(ctx);
        App_NotifyChatMessage(
            ctx->app,
            RS_CHAT_TYPE_FRIENDS_CHAT,
            p->sender,
            p->text ? p->text : "");
    }
}

static void
exec_last_login_info(struct AppGameProtoCtx const* ctx)
{
    /* The rows are clientCode components RS_ClientCode_Tick refreshes off
     * the session record the model wrote; this packet also OPENS the
     * screen they sit on, which is the only thing that ever does. */
    exec_open_welcome_screen(ctx->app);
    RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_logout(struct AppGameProtoCtx const* ctx)
{
    struct App* app = ctx->app;
    /* The server ending the session and the player asking to are the
     * same ending: socket closed, world forgotten, login screen back
     * up. The chat line goes in first because App_Logout is what takes
     * the chatbox away -- it is the only feedback a profile with no
     * title screen to return to ever gets. */
    exec_chat_add(ctx, RS_CHAT_TYPE_GAME, NULL, NULL, "You have been logged out.");
    App_Logout(app);
}

static void
exec_set_multiway(struct AppGameProtoCtx const* ctx)
{
    if( ctx->modelled == RS_SERVER_STATE_APPLIED )
        RedrawRequest_Raise(&ctx->app->redraw_request);
}

/* deob client.java (class243.field3065): `field758 = g1 == 1`, the flag
 * class112.method3876/3877 read for the camera's pitch limits. */
static void
exec_cam_unlock(
    struct AppGameProtoCtx const* ctx,
    struct PktCamUnlock const* p)
{
    if( CamScript_SetUnlock(&ctx->app->cam_script, p->unlock) )
        RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_minimap_toggle(
    struct AppGameProtoCtx const* ctx,
    struct PktMinimapToggle const* p)
{
    if( getenv("TORIRS_FRAME_ROLE_AUDIT") )
        TORIRS_REPORT("frame_native: MINIMAP_TOGGLE state=%d\n", p->state);
    if( MinimapView_SetToggleState(&ctx->app->minimap, p->state) )
        RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_send_ping(
    struct AppGameProtoCtx const* ctx,
    struct PktSendPing const* p)
{
    App_SendPingReply(ctx->app, p->value1, p->value2);
}

/* deob client.java field1008: OBJUNLOCK_RESET forgets the bitset. */
static void
exec_objunlock_reset(struct AppGameProtoCtx const* ctx)
{
    ctx->app->host.client.objunlock_present = false;
}

/* class561.method12548: each entry names its word by delta from the last.
 * The deob throws on a word past the end; here it is dropped with a line. */
static void
exec_objunlock_update(
    struct AppGameProtoCtx const* ctx,
    struct PktObjUnlock const* p)
{
    struct RS_CS2ClientState* client = &ctx->app->host.client;
    int word = 0;

    if( !client->objunlock_present )
    {
        memset(client->objunlock_words, 0, sizeof(client->objunlock_words));
        client->objunlock_present = true;
    }
    for( int i = 0; i < p->count; i++ )
    {
        word += p->entries[i].delta;
        if( word < 0 || word >= RS_CS2_OBJUNLOCK_WORDS )
        {
            TORIRS_LOG("exec: objunlock word %d out of range\n", word);
            break;
        }
        client->objunlock_words[word] = (uint64_t)p->entries[i].bits;
    }
}

/* deob client.java class259.field3155 -> class186.method6134. Through the
 * host's slot, so main.c opens it on the platform as CS2's openurl does. */
static void
exec_url_open(
    struct AppGameProtoCtx const* ctx,
    struct PktText const* p)
{
    snprintf(ctx->app->host.client.pending_open_url,
             sizeof(ctx->app->host.client.pending_open_url),
             "%s",
             p->text);
}

static void
exec_logout_withreason(
    struct AppGameProtoCtx const* ctx,
    struct PktLogoutWithReason const* p)
{
    struct App* app = ctx->app;
    char key[48];
    char const* line[3];

    exec_chat_add(ctx, RS_CHAT_TYPE_GAME, NULL, NULL, "You have been logged out.");
    App_Logout(app);
    /* What the login screen says, by reason (deob Statics.method7871:
     * 1 kicked, 2 the servers updating) -- the profile's words. */
    for( int i = 0; i < 3; i++ )
    {
        snprintf(key, sizeof(key), "logout_reason_%d_line%d",
                 p->reason, i + 1);
        line[i] = RS_LoginReplies_String(&app->login_replies, key);
    }
    RS_Title_SetMessages(&app->title, line[0], line[1], line[2]);
    RS_Title_SetScreen(&app->title, RS_TITLE_INFO);
}

static void
exec_zbuf(
    struct AppGameProtoCtx const* ctx,
    struct PktZBuf const* p)
{
    RenderMode_SetServerZbuf(&ctx->app->render_mode, p->enabled);
    RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_cam_mode(
    struct AppGameProtoCtx const* ctx,
    struct PktIntValue const* p)
{
    CamScript_SetMode(&ctx->app->cam_script, p->value);
    RedrawRequest_Raise(&ctx->app->redraw_request);
}

/* deob client.java class259.field3148 -> Statics.method9192. */
static void
exec_oculus_sync(
    struct AppGameProtoCtx const* ctx,
    struct PktIntValue const* p)
{
    CamScript_OculusSync(&ctx->app->cam_script, p->value);
}

static void
exec_cam_skybox(
    struct AppGameProtoCtx const* ctx,
    struct PktIntValue const* p)
{
    App_SetSkybox(ctx->app, p->value);
}

static void
exec_set_heatmap_enabled(
    struct AppGameProtoCtx const* ctx,
    struct PktIntValue const* p)
{
    RS_WorldMapSurface_SetHeatmap(&ctx->app->worldmap, p->value != 0);
    RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_group_var(
    struct AppGameProtoCtx const* ctx,
    struct PktGroupVar const* p)
{
    for( int i = 0; i < p->count; i++ )
        RS_GroupStore_ApplyVar(
            &ctx->app->host.groups,
            p->updates[i].index,
            p->updates[i].packed,
            p->updates[i].int_value,
            p->updates[i].long_value,
            p->updates[i].string_value);
}

static void
exec_group_full(
    struct AppGameProtoCtx const* ctx,
    struct PktRawPayload const* p)
{
    /* A player in no group gets the packet EMPTY (both rev-241 recordings
     * of 2026-10-22/28 open with one): nothing to apply, and no bytes for
     * RS_GroupStore_ApplyFull to be handed. */
    if( ctx->app->provider && p->length > 0 )
        RS_GroupStore_ApplyFull(
            &ctx->app->host.groups,
            &ctx->app->provider->group_types,
            p->data,
            p->length,
            exec_group_notify,
            ctx->app);
    /* The group-ironman shared storage (deob class603.method13001). Its
     * readers are the group storage clientscript commands, which this
     * client does not implement; the payload is decoded and kept no
     * further until they are. */
}

static void
exec_reset_anims(struct AppGameProtoCtx const* ctx)
{
    if( ctx->app->scene )
    {
        int slot_count = ToriDraw_SceneElementSlotCount(ctx->app->scene);
        for( int element_id = 0; element_id < slot_count; element_id++ )
        {
            struct ToriDraw_SceneElement* el;
            if( !ToriDraw_SceneElementIsLive(ctx->app->scene, element_id) )
                continue;
            el = ToriDraw_SceneElementGet(ctx->app->scene, element_id);
            if( el )
            {
                el->anim_frame = 0;
                el->anim_cycle = 0;
            }
        }
        RedrawRequest_Raise(&ctx->app->redraw_request);
    }
}

static void
exec_tut_flash(
    struct AppGameProtoCtx const* ctx,
    struct PktTutFlash const* p)
{
    ctx->app->slots.flash_tab = p->tab;
    /* Asked to flash the tab the player is already looking at, the
     * reference moves them OFF it -- an open panel covers its own icon,
     * so a blink nobody can see is no instruction at all. 3 is the
     * inventory, which is where it sends you unless that IS the tab. */
    if( p->tab >= 0 &&
        p->tab == ctx->app->slots.side_tab )
    {
        ctx->app->slots.side_tab = p->tab == 3 ? 1 : 3;
        RedrawRequest_Raise(&ctx->app->redraw_request);
    }
}

static void
exec_tut_open(
    struct AppGameProtoCtx const* ctx,
    struct PktTutOpen const* p)
{
    RS_UISlots_OpenTut(ctx->app, p->component_id);
}

static void
exec_synth_sound(
    struct AppGameProtoCtx const* ctx,
    struct PktSynthSound const* p)
{
    App_PlaySound(
        ctx->app,
        p->id,
        p->loops,
        p->delay);
}

static void
exec_midi_song_withsecondary(
    struct AppGameProtoCtx const* ctx,
    struct PktMidiSongWithSecondary const* p)
{
    /*
     * Same as MIDI_SONG, plus a variant of the same arrangement held ready
     * for a later MIDI_SWAP. Tracks always loop.
     */
    App_PlaySongWithSecondary(
        ctx->app,
        p->primary_id,
        p->secondary_id,
        p->fade_out_ms,
        p->fade_in_ms);
}

static void
exec_midi_swap(
    struct AppGameProtoCtx const* ctx,
    struct PktMidiSwap const* p)
{
    App_SwapSong(
        ctx->app,
        p->fade_out_ms,
        p->fade_in_ms);
}

static void
exec_midi_song(
    struct AppGameProtoCtx const* ctx,
    struct PktMidiSong const* p)
{
    /*
     * The V2 form the modern revisions send carries a fade envelope; the
     * older one does not and decodes to zeroes, which reads correctly as
     * "switch immediately". A track always loops -- it is background music,
     * and the server only ever replaces it.
     */
    App_PlaySong(
        ctx->app,
        p->id,
        true,
        p->fade_out_ms,
        p->fade_in_ms);
}

static void
exec_midi_jingle(
    struct AppGameProtoCtx const* ctx,
    struct PktMidiJingle const* p)
{
    App_PlayJingle(ctx->app, p->id, p->delay);
}

static void
exec_midi_song_stop(
    struct AppGameProtoCtx const* ctx,
    struct PktMidiSongStop const* p)
{
    App_StopSong(ctx->app, p->fade_out_ms);
}

static void
exec_ambientsound_start(
    struct AppGameProtoCtx const* ctx,
    struct PktAmbientSoundStart const* p)
{
    App_SetAmbientSound(
        ctx->app,
        p->id,
        p->fade_ms);
}

static void
exec_ambientsound_stop(
    struct AppGameProtoCtx const* ctx,
    struct PktAmbientSoundStop const* p)
{
    App_SetAmbientSound(ctx->app, -1, p->fade_ms);
}

static void
exec_if_openoverlay(
    struct AppGameProtoCtx const* ctx,
    struct PktIfOpenOverlay const* p)
{
    RS_UISlots_OpenOverlay(ctx->app, p->component_id);
}

static void
exec_if_settab_active(
    struct AppGameProtoCtx const* ctx,
    struct PktIfSetTabActive const* p)
{
    RS_UISlots_SetSideTab(ctx->app, p->tab_id);
}

static void
exec_unset_map_flag(
    struct AppGameProtoCtx const* ctx,
    struct PktSetMapFlag const* p)
{
    /* DRIVE_STAMP: map_flag -- a=-1 when cleared, else a=tile_x b=tile_z.
     * BOTH branches: walk_to, talk_to and click_loc all read the clear as
     * "the route finished or was abandoned". */
    /* Wire tiles are classic-scene local; with a (zone-6)*8 base that is
     * our-scene too. */
    if( p->clear )
    {
        ctx->app->minimap.flag_tile_x = -1;
        ctx->app->minimap.flag_tile_z = -1;
    }
    else if( p->absolute )
    {
        /* SetMapFlagV2 carries an absolute coord; the minimap draws in
         * scene-local tiles. Converted against the root world's base:
         * the origin the camera packets use and the one the flag is
         * read back against (app_tick.c, the CS2 destination coord).
         * The deob converts against the view whose rectangle holds the
         * coord (client.java class243.field3077: field748.method1423,
         * class100.method3310); for every coord in the root scene that
         * is the root's base. A flag on a deck's staging rectangle has
         * no minimap of its own here and lands off this scene. */
        ctx->app->minimap.flag_tile_x =
            p->x - (ctx->app->world ? ctx->app->world->_base_tile_x : 0);
        ctx->app->minimap.flag_tile_z =
            p->z - (ctx->app->world ? ctx->app->world->_base_tile_z : 0);
    }
    else
    {
        ctx->app->minimap.flag_tile_x = p->x;
        ctx->app->minimap.flag_tile_z = p->z;
    }
    /* Both branches land here: a clear leaves flag_tile_{x,z} at the
     * -1 the first branch set, so one stamp of the settled value
     * covers "a=-1 when cleared, else a=tile_x b=tile_z" without
     * repeating the call per branch. */
    App_DriveEvent(
        ctx->app,
        DRIVE_EVENT_MAP_FLAG,
        ctx->app->minimap.flag_tile_x,
        ctx->app->minimap.flag_tile_z,
        0,
        0);
    RedrawRequest_Raise(&ctx->app->redraw_request);
}

static void
exec_server_tick_end(struct AppGameProtoCtx const* ctx)
{
    /* The fence. Every packet of this tick has been applied, so the scripts
     * the tick pushed can now repaint against the state the server meant
     * them to see — see `pending_clientscripts` in app.h. */
    /* The model has fenced the tick, returned the active-world cursor to
     * the root and stamped server_tick (RS_ServerState_Apply). */
    /* Onto the packet pipeline (behind this fence task), not the
     * asset runner: the pipeline is a strict FIFO, so the tick's
     * scripts run one after another in wire order, as the reference
     * runs them inline. On the asset runner a script parked on a
     * sprite load let the next one overtake it: the House
     * Advertisement board's "list complete" call (poh_board_addline
     * row 200) ran before the row call had counted its row, and drew
     * "No houses are currently listed." over the house it listed. */
    App_FlushPendingClientScripts(ctx->app, &ctx->app->packet_runner);
    /* The 600ms cadence a plugin actually wants: every packet of this
     * server tick is now in world state, so a snapshot read here
     * agrees with what the server believes. Anything sampled mid-tick
     * is reading a half-applied world. */
    ctx->app->drive_events.batch_open = 0;
    PluginHost_ServerTick(
        ctx->app->plugins, ctx->app->world ? ctx->app->world->cycle : 0);
}

static void
exec_update_tradingpost(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateTradingPost const* p)
{
    RS_CS2Host_ApplyTradingPost(
        &ctx->app->host, p, ctx->app->host.client.now_ms);
    ctx->app->host.active_offers_transmit_dirty = 1;
}

static void
exec_update_stockmarket_slot(
    struct AppGameProtoCtx const* ctx,
    struct PktUpdateStockmarketSlot const* p)
{
    if( p->slot < RS_CS2_STOCKMARKET_SLOTS )
    {
        struct RS_CS2StockmarketOffer* offer = &ctx->app->host.stockmarket[p->slot];
        offer->status = p->status;
        offer->obj = p->obj;
        offer->price = p->price;
        offer->count = p->count;
        offer->completed_count = p->completed_count;
        offer->completed_gold = p->completed_gold;
    }
    else
        TORIRS_ERR("gameproto_exec: UPDATE_STOCKMARKET_SLOT slot %d out of range\n", p->slot);
    ctx->app->host.stock_transmit_dirty = 1;
}

void
App_GameProto_Exec(
    struct App* app,
    struct RS_ServerState const* state,
    struct RevPacket* packet)
{
    struct AppGameProtoCtx ctx_ = app_exec_ctx(app, state);
    struct AppGameProtoCtx const* ctx = &ctx_;

    assert(packet);

    if( packet->_control )
    {
        exec_control(ctx, packet->_control);
        return;
    }

    /* The model half first (game/rs_server_state.c): every record the packet
     * writes is written before anything below presents it. What follows for a
     * packet the model applied is that packet's presentation only. */
    ctx_.modelled = RS_ServerState_Apply(ctx->state, packet);

    switch( packet->packet_type )
    {
    /* ---- varps (model only; varp_changed is the model's stamp) ---- */
    case PKT_NAME_VARP_SMALL:
        exec_varp_small(ctx, &packet->_varp_small);
        break;
    case PKT_NAME_VARP_LARGE:
    case PKT_NAME_VARP_LONG:
    case PKT_NAME_VARP_SYNC:
    case PKT_NAME_VARP_RESET:
        break;

    /* ---- player stats ---- */
    case PKT_NAME_UPDATE_STAT:
        exec_update_stat(ctx, &packet->_update_stat);
        break;
    case PKT_NAME_UPDATE_RUNENERGY:
    case PKT_NAME_UPDATE_RUNWEIGHT:
        exec_run_misc_changed(ctx);
        break;

    /* ---- inventories ---- */
    case PKT_NAME_UPDATE_INV_FULL:
        exec_update_inv_full(ctx, &packet->_update_inv_full);
        break;
    case PKT_NAME_UPDATE_INV_PARTIAL:
        exec_update_inv_partial(ctx, &packet->_update_inv_partial);
        break;
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        exec_update_inv_stop_transmit(ctx, &packet->_update_inv_stop_transmit);
        break;

    /* ---- interface component mutations ---- */
    case PKT_NAME_IF_SETTEXT:
        exec_if_settext(ctx, &packet->_if_settext);
        break;
    case PKT_NAME_IF_SETEVENTS:
        exec_if_setevents(ctx, &packet->_if_setevents);
        break;
    case PKT_NAME_IF_SETHIDE:
        exec_if_sethide(ctx, &packet->_if_sethide);
        break;
    case PKT_NAME_IF_SETCOLOUR:
        exec_if_setcolour(ctx, &packet->_if_setcolour);
        break;
    case PKT_NAME_IF_SETMODEL:
        exec_if_setmodel(ctx, &packet->_if_setmodel);
        break;
    case PKT_NAME_IF_SETOBJECT:
        exec_if_setobject(ctx, &packet->_if_setobject);
        break;
    case PKT_NAME_IF_SETNPCHEAD:
        exec_if_setnpchead(ctx, &packet->_if_setnpchead);
        break;
    case PKT_NAME_IF_SETPLAYERHEAD:
        exec_if_setplayerhead(ctx, &packet->_if_setplayerhead);
        break;
    case PKT_NAME_IF_SETANIM:
        exec_if_setanim(ctx, &packet->_if_setanim);
        break;
    /* Written to the tree alone, the same in a client with no App
     * (game/rs_gameproto_exec.c): they rely on the mount task having been
     * queued ahead of them. */
    case PKT_NAME_IF_SETSCROLLPOS:
    case PKT_NAME_IF_SETPOSITION:
    case PKT_NAME_IF_SETROTATESPEED:
    case PKT_NAME_IF_SETANGLE:
    case PKT_NAME_IF_CLEARINV:
        RS_GameProto_PresentTree(ctx->tree, packet);
        break;
    case PKT_NAME_IF_SETNPCHEAD_ACTIVE:
        exec_if_setnpchead_active(ctx, &packet->_if_setnpchead_active);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR:
        exec_if_setplayermodel_basecolour(ctx, &packet->_if_setplayermodel);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE:
        exec_if_setplayermodel_bodytype(ctx, &packet->_if_setplayermodel);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_OBJ:
        exec_if_setplayermodel_obj(ctx, &packet->_if_setplayermodel);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_SELF:
        exec_if_setplayermodel_self(ctx, &packet->_if_setplayermodel);
        break;

    /* ---- interface slots (need the App / RS_UISlots) ---- */
    case PKT_NAME_IF_OPENMAIN:
        exec_if_openmain(ctx, &packet->_if_openmain);
        break;
    case PKT_NAME_IF_OPENSIDE:
        exec_if_openside(ctx, &packet->_if_openside);
        break;
    case PKT_NAME_IF_OPENMAIN_SIDE:
        exec_if_openmain_side(ctx, &packet->_if_openmain_side);
        break;
    case PKT_NAME_IF_OPENCHAT:
        exec_if_openchat(ctx, &packet->_if_openchat);
        break;
    case PKT_NAME_IF_CLOSE:
        exec_if_close(ctx);
        break;
    case PKT_NAME_IF_SETTAB:
        exec_if_settab(ctx, &packet->_if_settab);
        break;

    /* ---- modern gameframe interfaces (rev-230 openTop/openSub) ---- */
    case PKT_NAME_IF_OPENTOP:
        exec_if_opentop(ctx, &packet->_if_opentop);
        break;
    case PKT_NAME_IF_OPENSUB:
        exec_if_opensub(ctx, &packet->_if_opensub);
        break;
    case PKT_NAME_IF_CLOSESUB:
        exec_if_closesub(ctx, &packet->_if_closesub);
        break;
    case PKT_NAME_IF_MOVESUB:
        exec_if_movesub(ctx, &packet->_if_movesub);
        break;
    case PKT_NAME_IF_RESYNC_V2:
        exec_if_resync_v2(ctx, &packet->_if_resync);
        break;
    case PKT_NAME_RUNCLIENTSCRIPT:
        exec_runclientscript(ctx, packet);
        break;
    case PKT_NAME_TRIGGER_ONDIALOGABORT:
        exec_trigger_on_dialog_abort(ctx);
        break;

    /* ---- chat ---- */
    case PKT_NAME_MESSAGE_GAME:
        exec_message_game(ctx, &packet->_message_game);
        break;

    /* ---- world rebuild ---- */
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
        exec_rebuild(ctx, &packet->_map_rebuild);
        break;
    case PKT_NAME_PLAYER_INFO:
    case PKT_NAME_NPC_INFO:
    case PKT_NAME_NPC_INFO_LARGE:
        exec_entity_info_dropped(ctx, packet);
        break;

    /* ---- zone packets (world mutations) ---- */
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
        exec_update_zone_partial_follows(ctx, &packet->_update_zone_partial_follows);
        break;
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        exec_update_zone_full_follows(ctx, &packet->_update_zone_full_follows);
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        exec_update_zone_partial_enclosed(ctx, &packet->_update_zone_enclosed);
        break;
    case PKT_NAME_OBJ_ADD:
        exec_zone_sub_packet(ctx, PKT_NAME_OBJ_ADD, &packet->_obj_add);
        break;
    case PKT_NAME_OBJ_DEL:
        exec_zone_sub_packet(ctx, PKT_NAME_OBJ_DEL, &packet->_obj_del);
        break;
    case PKT_NAME_OBJ_COUNT:
        exec_zone_sub_packet(ctx, PKT_NAME_OBJ_COUNT, &packet->_obj_count);
        break;
    case PKT_NAME_OBJ_REVEAL:
        exec_zone_sub_packet(ctx, PKT_NAME_OBJ_REVEAL, &packet->_obj_reveal);
        break;
    case PKT_NAME_LOC_DEL:
        exec_zone_sub_packet(ctx, PKT_NAME_LOC_DEL, &packet->_loc_del);
        break;
    case PKT_NAME_LOC_ADD_CHANGE:
        exec_zone_sub_packet(ctx, PKT_NAME_LOC_ADD_CHANGE, &packet->_loc_add_change);
        break;
    case PKT_NAME_LOC_ANIM:
        exec_zone_sub_packet(ctx, PKT_NAME_LOC_ANIM, &packet->_loc_anim);
        break;
    case PKT_NAME_MAP_ANIM:
        exec_zone_sub_packet(ctx, PKT_NAME_MAP_ANIM, &packet->_map_anim);
        break;
    case PKT_NAME_MAP_ANIM_SPECIFIC:
        exec_map_anim_specific(ctx, &packet->_map_anim_specific);
        break;
    case PKT_NAME_SCRIPTEDPROJ_CHANGE:
        exec_zone_sub_packet(ctx, PKT_NAME_SCRIPTEDPROJ_CHANGE, &packet->_scriptedproj_change);
        break;
    case PKT_NAME_OBJ_CUSTOMISE:
        exec_zone_sub_packet(ctx, PKT_NAME_OBJ_CUSTOMISE, &packet->_obj_customise);
        break;
    case PKT_NAME_OBJ_UNCUSTOMISE:
        exec_zone_sub_packet(ctx, PKT_NAME_OBJ_UNCUSTOMISE, &packet->_obj_uncustomise);
        break;
    case PKT_NAME_OBJ_CUSTOMISE_SPECIFIC:
    case PKT_NAME_OBJ_UNCUSTOMISE_SPECIFIC:
        exec_obj_customise_specific(ctx, packet->packet_type, &packet->_obj_customise_specific);
        break;
    case PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC:
        exec_obj_specific(ctx, packet->packet_type, &packet->_obj_specific);
        break;
    case PKT_NAME_LOC_ANIM_SPECIFIC:
        exec_loc_anim_specific(ctx, &packet->_loc_anim_specific);
        break;
    case PKT_NAME_PROJANIM_SPECIFIC:
        exec_projanim_specific(ctx, &packet->_projanim_specific);
        break;
    case PKT_NAME_ANIM_SPECIFIC:
        exec_anim_specific(ctx, &packet->_anim_specific);
        break;
    case PKT_NAME_NPC_ANIM_SPECIFIC:
        break;
    case PKT_NAME_NPC_SPOTANIM_SPECIFIC:
    case PKT_NAME_PLAYER_SPOTANIM_SPECIFIC:
        exec_entity_spotanim_specific(ctx, &packet->_entity_spotanim_specific);
        break;
    case PKT_NAME_SOUND_AREA:
        exec_zone_sub_packet(ctx, PKT_NAME_SOUND_AREA, &packet->_sound_area);
        break;
    case PKT_NAME_MAP_PROJANIM:
        exec_zone_sub_packet(ctx, PKT_NAME_MAP_PROJANIM, &packet->_map_projanim);
        break;
    case PKT_NAME_SCRIPTEDPROJ_ADD:
        exec_zone_sub_packet(ctx, PKT_NAME_SCRIPTEDPROJ_ADD, &packet->_scriptedproj_add);
        break;
    case PKT_NAME_LOC_MERGE:
        exec_zone_sub_packet(ctx, PKT_NAME_LOC_MERGE, &packet->_loc_merge);
        break;

    /* ---- camera ---- */
    case PKT_NAME_CAM_MOVETO:
        exec_cam_moveto(ctx, &packet->_cam_moveto);
        break;
    case PKT_NAME_CAM_LOOKAT:
        exec_cam_lookat(ctx, &packet->_cam_lookat);
        break;
    case PKT_NAME_CAM_MOVETO_CYCLES:
        exec_cam_moveto_cycles(ctx, &packet->_cam_moveto_cycles);
        break;
    case PKT_NAME_CAM_ROTATEBY:
        exec_cam_rotateby(ctx, &packet->_cam_rotate);
        break;
    case PKT_NAME_CAM_ROTATETO:
        exec_cam_rotateto(ctx, &packet->_cam_rotate);
        break;
    case PKT_NAME_CAM_LOOKAT_CYCLES:
        exec_cam_lookat_cycles(ctx, &packet->_cam_lookat_cycles);
        break;
    case PKT_NAME_CAM_MOVETO_ARC:
        exec_cam_moveto_arc(ctx, &packet->_cam_moveto_arc);
        break;
    case PKT_NAME_CAM_ROTATETO_COORDINATE:
        exec_cam_rotateto_coordinate(ctx, &packet->_cam_rotateto_coordinate);
        break;
    case PKT_NAME_CAM_SHAKE:
        exec_cam_shake(ctx, &packet->_cam_shake);
        break;
    case PKT_NAME_CAM_RESET:
        exec_cam_reset(ctx);
        break;
    case PKT_NAME_CAM_SMOOTHRESET:
        exec_cam_smoothreset(ctx, &packet->_cam_smoothreset);
        break;
    case PKT_NAME_CAM_TARGET:
        exec_cam_target(ctx, &packet->_cam_target);
        break;

    /* ---- social ---- */
    case PKT_NAME_UPDATE_FRIENDLIST:
    case PKT_NAME_UPDATE_IGNORELIST:
    case PKT_NAME_FRIENDLIST_LOADED:
    case PKT_NAME_CHAT_FILTER_SETTINGS:
        exec_friend_changed(ctx);
        break;
    case PKT_NAME_MESSAGE_PRIVATE:
        exec_message_private(ctx, &packet->_message_private);
        break;
    case PKT_NAME_MESSAGE_PRIVATE_ECHO:
        exec_message_private_echo(ctx, &packet->_message_private_echo);
        break;
    case PKT_NAME_MESSAGE_FRIENDCHANNEL:
        exec_message_friendchannel(ctx, &packet->_message_friendchannel);
        break;

    /* ---- misc state ---- */
    case PKT_NAME_UPDATE_PID:
        break;
    case PKT_NAME_LAST_LOGIN_INFO:
        exec_last_login_info(ctx);
        break;
    case PKT_NAME_UPDATE_REBOOT_TIMER:
    case PKT_NAME_P_COUNTDIALOG:
        break;
    case PKT_NAME_LOGOUT:
        exec_logout(ctx);
        break;
    case PKT_NAME_SET_PLAYER_OP:
        break;
    case PKT_NAME_SET_MULTIWAY:
        exec_set_multiway(ctx);
        break;
    case PKT_NAME_CAM_UNLOCK:
        exec_cam_unlock(ctx, &packet->_cam_unlock);
        break;
    case PKT_NAME_MINIMAP_TOGGLE:
        exec_minimap_toggle(ctx, &packet->_minimap_toggle);
        break;
    /* ---- the prots a replay used to drop ---- */
    case PKT_NAME_SEND_PING:
        exec_send_ping(ctx, &packet->_send_ping);
        break;
    case PKT_NAME_UNKNOWN_STRING:
        break;
    case PKT_NAME_OBJUNLOCK_RESET:
        exec_objunlock_reset(ctx);
        break;
    case PKT_NAME_OBJUNLOCK_UPDATE:
        exec_objunlock_update(ctx, &packet->_objunlock_update);
        break;
    /* The deob reads and discards both. */
    case PKT_NAME_ACCOUNT_FLAGS:
    case PKT_NAME_HISCORE_REPLY:
        break;
    case PKT_NAME_SET_WEBSITE_TOKEN:
        break;
    case PKT_NAME_URL_OPEN:
        exec_url_open(ctx, &packet->_text);
        break;
    case PKT_NAME_SITE_SETTINGS:
        break;
    case PKT_NAME_REFLECTION_CHECKER:
        /* The reference answers by reflecting over its own JVM classes. There
         * is nothing here to reflect over, so no answer is sent: a fabricated
         * one would claim classes this client does not have. */
        break;
    case PKT_NAME_PACKET_GROUP_START:
        /* The reference holds the frame until the group's bytes are all in, so
         * the group applies together (client.java, var3.field1554). This
         * client applies a whole read's packets before it draws, so a group
         * that arrived together already applies together. */
        break;
    case PKT_NAME_LOGOUT_WITHREASON:
        exec_logout_withreason(ctx, &packet->_logout_withreason);
        break;
    case PKT_NAME_LOGOUT_TRANSFER:
    case PKT_NAME_SET_INTERACTION_MODE:
    case PKT_NAME_RESET_INTERACTION_MODE:
    case PKT_NAME_NPC_HEADICON_SPECIFIC:
        break;
    case PKT_NAME_OBJ_ADD_SPECIFIC:
    case PKT_NAME_OBJ_DEL_SPECIFIC:
    case PKT_NAME_OBJ_COUNT_SPECIFIC:
        exec_obj_specific(ctx, packet->packet_type, &packet->_obj_specific);
        break;
    case PKT_NAME_CLEAR_ENTITIES:
        exec_clear_entities(ctx->app);
        break;
    case PKT_NAME_ZBUF:
        exec_zbuf(ctx, &packet->_zbuf);
        break;
    case PKT_NAME_CAM_MODE:
        exec_cam_mode(ctx, &packet->_int_value);
        break;
    case PKT_NAME_OCULUS_SYNC:
        exec_oculus_sync(ctx, &packet->_int_value);
        break;
    case PKT_NAME_CAM_SKYBOX:
        exec_cam_skybox(ctx, &packet->_int_value);
        break;
    case PKT_NAME_SET_HEATMAP_ENABLED:
        exec_set_heatmap_enabled(ctx, &packet->_int_value);
        break;
    case PKT_NAME_UPDATE_UID192:
        break;
    case PKT_NAME_GROUP_VAR:
        exec_group_var(ctx, &packet->_group_var);
        break;
    case PKT_NAME_GROUP_FULL:
        exec_group_full(ctx, &packet->_raw_payload);
        break;
    case PKT_NAME_HIDENPCOPS:
    case PKT_NAME_HIDELOCOPS:
    case PKT_NAME_HIDEOBJOPS:
    case PKT_NAME_HINT_ARROW:
        break;
    case PKT_NAME_RESET_ANIMS:
        exec_reset_anims(ctx);
        break;
    case PKT_NAME_ENABLE_TRACKING:
    case PKT_NAME_FINISH_TRACKING:
        break;

    /* ---- tutorial ---- */
    case PKT_NAME_TUT_FLASH:
        exec_tut_flash(ctx, &packet->_tut_flash);
        break;
    case PKT_NAME_TUT_OPEN:
        exec_tut_open(ctx, &packet->_tut_open);
        break;

    /* ---- audio ---- */
    case PKT_NAME_SYNTH_SOUND:
        exec_synth_sound(ctx, &packet->_synth_sound);
        break;
    case PKT_NAME_MIDI_SONG_WITHSECONDARY:
        exec_midi_song_withsecondary(ctx, &packet->_midi_song_with_secondary);
        break;
    case PKT_NAME_MIDI_SWAP:
        exec_midi_swap(ctx, &packet->_midi_swap);
        break;
    case PKT_NAME_MIDI_SONG:
        exec_midi_song(ctx, &packet->_midi_song);
        break;
    case PKT_NAME_MIDI_JINGLE:
        exec_midi_jingle(ctx, &packet->_midi_jingle);
        break;
    case PKT_NAME_MIDI_SONG_STOP:
        exec_midi_song_stop(ctx, &packet->_midi_song_stop);
        break;
    case PKT_NAME_AMBIENTSOUND_START:
        exec_ambientsound_start(ctx, &packet->_ambientsound_start);
        break;
    case PKT_NAME_AMBIENTSOUND_STOP:
        exec_ambientsound_stop(ctx, &packet->_ambientsound_stop);
        break;

    /* ---- remaining interface packets ---- */
    case PKT_NAME_IF_OPENOVERLAY:
        exec_if_openoverlay(ctx, &packet->_if_openoverlay);
        break;
    case PKT_NAME_IF_SETTAB_ACTIVE:
        exec_if_settab_active(ctx, &packet->_if_settab_active);
        break;
    case PKT_NAME_UNSET_MAP_FLAG:
        exec_unset_map_flag(ctx, &packet->_set_map_flag);
        break;
    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
    case PKT_NAME_SET_ACTIVE_WORLD:
        break;
    case PKT_NAME_WORLDENTITY_INFO:
        exec_worldentity_info(ctx->app, &packet->_worldentity_info);
        break;
    case PKT_NAME_SERVER_TICK_END:
        exec_server_tick_end(ctx);
        break;

    /* ---- friends chat, clans, Grand Exchange ---- */
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL:
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER:
        exec_friends_chat(&ctx->app->host, packet->packet_type, &packet->_raw_payload);
        break;
    case PKT_NAME_VARCLAN:
    case PKT_NAME_VARCLAN_ENABLE:
    case PKT_NAME_VARCLAN_DISABLE:
    case PKT_NAME_CLANCHANNEL_FULL:
    case PKT_NAME_CLANCHANNEL_DELTA:
    case PKT_NAME_CLANSETTINGS_FULL:
    case PKT_NAME_CLANSETTINGS_DELTA:
        exec_clan(&ctx->app->host, packet->packet_type, &packet->_raw_payload);
        break;
    case PKT_NAME_MESSAGE_CLANCHANNEL:
    case PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM:
        exec_message_clanchannel(
            ctx, packet->packet_type == PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM,
            &packet->_message_clanchannel);
        break;
    case PKT_NAME_UPDATE_TRADINGPOST:
        exec_update_tradingpost(ctx, &packet->_update_tradingpost);
        break;
    case PKT_NAME_UPDATE_STOCKMARKET_SLOT:
        exec_update_stockmarket_slot(ctx, &packet->_update_stockmarket_slot);
        break;

    default:
        if( torirs_env_net_debug() )
            TORIRS_LOG("gameproto_exec: unhandled packet %d\n", packet->packet_type);
        break;
    }
}
