#include "rs_gameproto_exec.h"

#include "app.h"
#include "inv/inv_manager.h"
#include "net/jbase37.h"
#include "net/net.h"
#include "net/rev/revpacket.h"
#include "rs_attack_option.h"
#include "rs_audio.h"
#include "rs_chat.h"
#include "rs_clientcode.h"
#include "rs_cs2_dispatch.h"
#include "rs_cs2_host.h"
#include "rs_entity_sync.h"
#include "rs_player_stats.h"
#include "rs_social.h"
#include "rs_ui_slots.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Put a line in the chatbox.
 *
 * Through the CS2 host when there is one, because a message is two things and
 * both have to happen: it goes in the store, and the chat-transmit channel
 * has to say so, or the cache's `[proc,rebuildchatbox]` never runs and the
 * line sits in a store nothing draws. The host also owns the client clock the
 * node is stamped with.
 *
 * The app-less form is the unit tests and the headless packet harness, which
 * have a store and no host; they get the store write and a zero clock, which
 * is all a run with no chatbox can use.
 */
static void
exec_chat_add(
    struct RS_GameProtoCtx const* ctx,
    int type,
    char const* name,
    char const* sender,
    char const* text)
{
    assert(ctx);
    if( !ctx->chat )
        return;
    if( ctx->app )
        RS_CS2Host_ChatAdd(&ctx->app->host, type, name, sender, text);
    else
        RS_Chat_AddMessage(ctx->chat, type, name, sender, text, 0);
}

/* 15-bit RS colour (r<<10|g<<5|b, 5 bits each) to RGB888. */
static int
rs15_to_rgb(int c)
{
    int r = (c >> 10) & 0x1f;
    int g = (c >> 5) & 0x1f;
    int b = c & 0x1f;
    return ((r << 3) << 16) | ((g << 3) << 8) | (b << 3);
}

/* Container key for an inv packet: the revision's own inventory id when it
 * carries one (rev 230+ addresses the container and the bound component
 * separately, and CS2 reads containers by inventory id), else the component id
 * the older revisions use as the container key. Zero counts as absent, not as
 * container 0: RevPacket is zero-initialised before parsing, so a revision (or
 * a hand-built test packet) that never touches inv_id must keep the legacy
 * component-id keying. */
static int
exec_inv_container_id(
    int inv_id,
    int component_id)
{
    return inv_id > 0 ? inv_id : component_id;
}

static void
exec_if_clearinv_node(
    struct UITree* tree,
    int32_t idx)
{
    struct UITreeComponent* c;

    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    c = &tree->components[idx];
    if( c->freed )
        return;
    c->item_id = 0;
    c->item_count = 0;
    c->item_scene_id = -1;
    c->item_atlas_index = 0;
    if( c->type == UIELEM_CC_OBJ )
    {
        c->u.cc_obj.obj_id = 0;
        c->u.cc_obj.obj_count = 0;
        c->u.cc_obj.scene_id = -1;
        c->u.cc_obj.atlas_index = 0;
    }
    UITree_MarkNodeDirty(tree, idx);
    for( int32_t child = c->first_child; child >= 0; )
    {
        int32_t next = tree->components[child].next_sibling;
        exec_if_clearinv_node(tree, child);
        child = next;
    }
}

static void
exec_update_inv_full(
    struct RS_GameProtoCtx const* ctx,
    struct PktUpdateInvFull const* p)
{
    /* Obj icons upload lazily when the bound inv view next renders (same path
     * as the RevConfig-seeded containers). */
    int container = exec_inv_container_id(p->inv_id, p->component_id);
    if( InvManager_EnsureContainer(ctx->invs, container, p->size, "server-inv") < 0 )
        return;
    InvManager_ApplyFull(ctx->invs, container, p->obj_ids, p->obj_counts, p->size);
    /* Containers arrive long after the interface that paints them was built,
     * so the CS2 paint script has to be told to run again. */
    if( ctx->app )
        RS_CS2Host_NotifyInvChanged(&ctx->app->host, container);
    if( getenv("TORIRS_INV_DEBUG") )
    {
        fprintf(
            stderr,
            "inv-full: container=%d (com 0x%08x) size=%d\n",
            container,
            (unsigned)p->component_id,
            p->size);
        for( int i = 0; i < p->size; i++ )
            if( p->obj_ids[i] > 0 )
                fprintf(stderr, "  slot %2d obj=%d x%d\n", i, p->obj_ids[i], p->obj_counts[i]);
    }
}

static void
exec_update_inv_partial(
    struct RS_GameProtoCtx const* ctx,
    struct PktUpdateInvPartial const* p)
{
    int src;
    int container = exec_inv_container_id(p->inv_id, p->component_id);
    if( InvManager_EnsureContainer(ctx->invs, container, 0, "server-inv") < 0 )
        return;
    src = InvManager_ContainerForSource(ctx->invs, container);
    if( src < 0 )
        return;
    if( getenv("TORIRS_INV_DEBUG") )
        fprintf(
            stderr,
            "inv-partial: container=%d (com 0x%08x) slots=%d\n",
            container,
            (unsigned)p->component_id,
            p->count);
    for( int i = 0; i < p->count; i++ )
    {
        /* The packet carries an item, not an icon: say so, rather than
         * letting the zero-initialiser pass 0 off as a scene id. Zero is
         * never allocated (the bridge counts up from 1), but it IS >= 0, so
         * emit drew "sprite 0" — nothing — with the stack count over it, and
         * it is not INV_MANAGER_NO_SCENE_ID, so the icon reconcile never
         * looked at the slot again. Every destination of an item that moved
         * between containers arrives on this path. */
        struct InvSlot slot = { 0 };
        slot.obj_id = p->entries[i].obj_id;
        slot.obj_count = p->entries[i].count;
        slot.scene_id = INV_MANAGER_NO_SCENE_ID;
        InvManager_SetSlot(ctx->invs, src, p->entries[i].slot, &slot);
        if( getenv("TORIRS_INV_DEBUG") )
            fprintf(
                stderr,
                "  slot %2d obj=%d x%d\n",
                p->entries[i].slot,
                slot.obj_id,
                slot.obj_count);
    }
    if( ctx->app && p->count > 0 )
        RS_CS2Host_NotifyInvChanged(&ctx->app->host, container);
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
exec_trigger_on_dialog_abort(struct RS_GameProtoCtx const* ctx)
{
    struct App* app;

    assert(ctx);
    if( !ctx->app || !ctx->tree )
        return;
    app = ctx->app;
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
        RS_CS2_DispatchHook(&app->host, &app->runner, node->component_id, &hook);
    }
    app->need_redraw = 1;
}

/* Where a zone sub-packet applies: base + packed nibbles, at `level`. The
 * base/level pair is captured by the caller — live packets use the app's
 * current state, replayed ones the state as of arrival. */
struct ZoneAt
{
    int base_x;
    int base_z;
    int level;
};

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
    struct App const* app,
    struct ZoneAt const* at,
    int pos,
    int* out_x,
    int* out_z,
    int* out_level)
{
    zone_tile_at(at, pos, out_x, out_z, out_level);
    *out_level = World_LocCacheLevel(app->world, *out_x, *out_z, *out_level);
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
    default:
        return 0;
    }
}

static void
exec_zone_sub_packet_at(
    struct RS_GameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload,
    struct ZoneAt const* at);

/*
 * A zone sub-packet arriving while the world is still async-loading cannot be
 * dropped: the reference's scene build is synchronous inside its packet loop,
 * so there every zone update processes after the rebuild it follows. Queue it
 * with the zone base and header plane as of NOW, then replay once the load
 * completes.
 */
static void
zone_pending_push(
    struct App* app,
    enum GameProtoPktName name,
    void const* payload)
{
    struct AppPendingZonePkt* entry;
    int cap = (int)(sizeof(app->pending_zone) / sizeof(app->pending_zone[0]));

    if( app->pending_zone_count >= cap )
    {
        fprintf(stderr, "gameproto_exec: pending-zone queue full, dropping pkt %d\n", (int)name);
        return;
    }
    entry = &app->pending_zone[app->pending_zone_count];
    if( !zone_sub_packet_copy(&entry->pkt, name, payload) )
        return;
    entry->base_x = app->zone_base_x;
    entry->base_z = app->zone_base_z;
    entry->level = app->zone_level;
    app->pending_zone_count++;
}

void
RS_GameProto_FlushPendingZone(struct RS_GameProtoCtx const* ctx)
{
    struct App* app = ctx->app;
    int i;

    if( !app || !app->world || !app->world->load_complete )
        return;
    for( i = 0; i < app->pending_zone_count; i++ )
    {
        struct AppPendingZonePkt const* entry = &app->pending_zone[i];
        struct ZoneAt at = { entry->base_x, entry->base_z, entry->level };
        exec_zone_sub_packet_at(ctx, entry->pkt.name, &entry->pkt._loc_add_change, &at);
    }
    app->pending_zone_count = 0;
}

static void
exec_zone_sub_packet(
    struct RS_GameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload)
{
    struct App* app = ctx->app;
    struct ZoneAt at;

    if( !app || !app->world )
        return;
    if( !app->world->load_complete )
    {
        zone_pending_push(app, name, payload);
        return;
    }
    /* Keep arrival order: anything still queued from the load window applies
     * before this live packet. */
    if( app->pending_zone_count > 0 )
        RS_GameProto_FlushPendingZone(ctx);

    at.base_x = app->zone_base_x;
    at.base_z = app->zone_base_z;
    at.level = app->zone_level;
    exec_zone_sub_packet_at(ctx, name, payload, &at);
}

static void
exec_zone_sub_packet_at(
    struct RS_GameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload,
    struct ZoneAt const* at)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;

    switch( name )
    {
    case PKT_NAME_OBJ_ADD:
    {
        struct PktObjAdd const* pkt = payload;
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackAdd(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
        break;
    }
    case PKT_NAME_OBJ_DEL:
    {
        struct PktObjDel const* pkt = payload;
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackDel(app, tile_x, tile_z, level, pkt->obj_id);
        break;
    }
    case PKT_NAME_OBJ_COUNT:
    {
        struct PktObjCount const* pkt = payload;
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackSetCount(app, tile_x, tile_z, level, pkt->obj_id, pkt->new_count);
        break;
    }
    case PKT_NAME_OBJ_REVEAL:
    {
        /* Reveal targets one receiver; everyone else already sees it. */
        struct PktObjReveal const* pkt = payload;
        if( ctx->app->esync.local_pid >= 0 && pkt->receiver != ctx->app->esync.local_pid )
            break;
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackAdd(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
        break;
    }
    case PKT_NAME_LOC_DEL:
    {
        struct PktLocDel const* pkt = payload;
        zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
        /* Remove the loc in this shape's layer (scene + collision). shape =
         * info >> 2 keys the layer so a door (WALL) removal only hits the WALL
         * loc, not a centrepiece/floor-decor sharing the tile. loc_id = -1 = no
         * replacement. Routed through the async loc-change task so it stays
         * ordered with in-flight LOC_ADD_CHANGE loads on the same tile. */
        App_WorldLocChange(app, tile_x, tile_z, level, -1, pkt->info >> 2, pkt->info & 0x3);
        break;
    }
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        /* Replace the loc in this layer: remove the stale loc and spawn the new
         * one (scene + collision), matching Client-TS locChangeUnchecked. Async:
         * the new loc's models are usually absent from the static build's
         * preload, so the change applies once they're resident (reference
         * changeLocAvailable gate in locChangeDoQueue). */
        struct PktLocAddChange const* pkt = payload;
        zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
        /* The op mask and replacement labels ride along: they describe THIS
         * placement, so they cannot be recovered from the loc id later. */
        App_WorldLocChangeOps(
            app, tile_x, tile_z, level, pkt->loc_id, pkt->info >> 2, pkt->info & 0x3,
            pkt->op_flags, pkt->ops);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: LOC_ADD_CHANGE loc=%d shape=%d angle=%d at %d,%d,l%d\n",
                pkt->loc_id,
                pkt->info >> 2,
                pkt->info & 0x3,
                tile_x,
                tile_z,
                level);
        break;
    }
    case PKT_NAME_LOC_ANIM:
    {
        struct PktLocAnim const* pkt = payload;
        zone_loc_tile_at(app, at, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldSceneryAnim(app, tile_x, tile_z, level, pkt->info >> 2, pkt->seq_id);
        break;
    }
    case PKT_NAME_MAP_ANIM:
    {
        /* Free-standing graphical effect at a tile (reference MAP_ANIM ->
         * new MapSpotAnim). id 65535 is the clear sentinel; ignore it. */
        struct PktMapAnim const* pkt = payload;
        if( pkt->id != 65535 )
        {
            zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
            App_WorldSpotanimSpawn(app, tile_x, tile_z, level, pkt->id, pkt->height, pkt->delay);
        }
        break;
    }
    case PKT_NAME_SOUND_AREA:
    {
        /*
         * A one-shot sound at a tile, with its own audible radius. The level is
         * decoded but not used to gate it: the reference range-checks in x/z
         * only, and a forge on the floor below is meant to be heard.
         */
        struct PktSoundArea const* pkt = payload;
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        App_PlaySoundAt(
            app, pkt->id, pkt->loops, pkt->delay, tile_x, tile_z, pkt->radius, pkt->inner);
        break;
    }
    case PKT_NAME_MAP_PROJANIM:
    {
        /* Projectile spawn from a spotanim config (reference MAP_PROJANIM ->
         * new ClientProj). The base tile is `pos` within the zone; the
         * destination is offset by dx/dz (the target's cast-time position, which
         * is also the initial aim point). peak/arc are Client-TS angle/startpos.
         * pkt->target names the entity the arc then homes on each cycle. */
        struct PktMapProjAnim const* pkt = payload;
        int src_tx, src_tz, dst_tx, dst_tz;
        zone_tile_at(at, pkt->pos, &tile_x, &tile_z, &level);
        src_tx = tile_x;
        src_tz = tile_z;
        if( pkt->dst_abs )
        {
            /* Revision 239 states the landing point as an absolute CoordGrid;
             * everything below the parse works in scene-local tiles, and the
             * scene origin is the zone the last rebuild centred on, minus the
             * six zones of margin the client keeps on each side. Applied as
             * offsets, an absolute coord puts the arc a few tiles from the
             * scene's south-west corner and the shot flies off the map. */
            int origin_x = (ctx->app->rebuild_zone_x - 6) * 8;
            int origin_z = (ctx->app->rebuild_zone_z - 6) * 8;

            dst_tx = pkt->dst_abs_x - origin_x;
            dst_tz = pkt->dst_abs_z - origin_z;
            level = pkt->dst_abs_level;
        }
        else
        {
            dst_tx = tile_x + pkt->dx_offset;
            dst_tz = tile_z + pkt->dz_offset;
        }
        App_WorldProjectileSpawn(
            app,
            src_tx,
            src_tz,
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
            pkt->target);
        break;
    }
    case PKT_NAME_LOC_MERGE:
    {
        /* P_LOCMERGE: hide the loc for [start,end) and mark the player so the
         * loc model rides with them (Client-TS zonePacket P_LOCMERGE). */
        struct PktLocMerge const* pkt = payload;
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
        break;
    }
    default:
        break;
    }
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
    if( app->welcome.last_ip == 0 )
        return;
    /* Never in front of something already open. LAST_LOGIN_INFO is a login
     * packet by convention, not by rule, and the reference makes the same
     * check rather than trusting that. */
    if( app->slots.main_modal_id != -1 )
        return;

    iface_name = (app->welcome.days_since_recovery != RS_CC_RECOVERY_DAYS_SILENT ||
                  app->welcome.member_warning == 1)
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

void
RS_GameProto_Exec(
    struct RS_GameProtoCtx const* ctx,
    struct RevPacket* packet)
{
    assert(ctx && packet);

    switch( packet->packet_type )
    {
    /* ---- varps ---- */
    case PKT_NAME_VARP_SMALL:
        VarPManager_ApplySmall(ctx->varps, packet->_varp_small.variable, packet->_varp_small.value);
        break;
    case PKT_NAME_VARP_LARGE:
        VarPManager_ApplyLarge(ctx->varps, packet->_varp_large.variable, packet->_varp_large.value);
        break;

    /* ---- player stats ---- */
    case PKT_NAME_UPDATE_STAT:
        if( packet->_update_stat.stat >= 0 &&
            packet->_update_stat.stat < RS_PLAYER_STATS_SKILL_COUNT )
        {
            RS_PlayerStats_SetXp(ctx->stats, packet->_update_stat.stat, packet->_update_stat.xp);
            ctx->stats->current_level[packet->_update_stat.stat] = packet->_update_stat.level;
            /* A level-up is read from the BASE level SetXp just derived, not
             * from the boosted one on the wire: a potion raises that and a
             * drain lowers it, and neither is an advance. */
            if( ctx->app )
                App_NotifyStatLevel(
                    ctx->app,
                    packet->_update_stat.stat,
                    ctx->stats->base_level[packet->_update_stat.stat]);
            /* The reactive half. Without it the skills tab paints once at
             * build time — against the zeroes the client starts with — and
             * nothing ever asks it to paint again, which is the same shape as
             * the inv-transmit gap that left server-driven inventories blank. */
            if( ctx->app )
                RS_CS2Host_NotifyStatChanged(&ctx->app->host, packet->_update_stat.stat);
            RS_PlayerStats_RecomputeCombatLevel(ctx->stats);
        }
        break;
    /* The reactive half, same shape as UPDATE_STAT above: the orb's CS2 binds
     * IF_SETONMISCTRANSMIT, so without a notify it paints its build-time value
     * and is only ever refreshed by an unrelated interface event. Gated on the
     * value actually changing because the dispatch walks every component —
     * and the server already change-gates these packets, so an unchanged value
     * arriving at all is unusual. */
    case PKT_NAME_UPDATE_RUNENERGY:
        if( ctx->stats->run_energy != packet->_update_run_energy.run_energy )
        {
            ctx->stats->run_energy = packet->_update_run_energy.run_energy;
            if( ctx->app )
                RS_CS2Host_NotifyMiscChanged(&ctx->app->host);
        }
        break;
    case PKT_NAME_UPDATE_RUNWEIGHT:
        if( ctx->stats->run_weight != packet->_update_runweight.run_weight )
        {
            ctx->stats->run_weight = packet->_update_runweight.run_weight;
            if( ctx->app )
                RS_CS2Host_NotifyMiscChanged(&ctx->app->host);
        }
        break;

    /* ---- inventories ---- */
    case PKT_NAME_UPDATE_INV_FULL:
        exec_update_inv_full(ctx, &packet->_update_inv_full);
        break;
    case PKT_NAME_UPDATE_INV_PARTIAL:
        exec_update_inv_partial(ctx, &packet->_update_inv_partial);
        break;

    /* ---- interface component mutations ---- */
    case PKT_NAME_IF_SETTEXT:
        /* Persist through the app store when available: journal/bonus texts
         * arrive before their interface mounts (tests exec without an app). */
        if( ctx->app )
        {
            App_IfTextSet(ctx->app, packet->_if_settext.component_id, packet->_if_settext.text);
            /* A quest completion is painted onto the scroll and never said
             * aloud (content: questscroll.rs2), so this is the only place the
             * client can see one happen. */
            App_NotifyInterfaceText(ctx->app, packet->_if_settext.text);
        }
        else
            UITree_ApplyText(ctx->tree, packet->_if_settext.component_id, packet->_if_settext.text);
        break;
    case PKT_NAME_IF_SETEVENTS:
        /* Persisting, like IF_SETHIDE: the server enables a component's events
         * before the interface holding it has finished mounting. */
        if( ctx->app )
            App_IfEventsSet(
                ctx->app,
                packet->_if_setevents.component_id,
                packet->_if_setevents.from,
                packet->_if_setevents.to,
                packet->_if_setevents.events);
        break;
    case PKT_NAME_IF_SETHIDE:
        /* Persisting setter, not a one-shot apply: IF_SETHIDE routinely lands
         * before the interface it targets has finished mounting (the mount is
         * an async task), and the reference keeps `hide` on IfType.list where
         * it survives. */
        if( ctx->app )
            App_IfHideSet(ctx->app, packet->_if_sethide.component_id, packet->_if_sethide.hide);
        else
            UITree_ApplyHide(
                ctx->tree, packet->_if_sethide.component_id, packet->_if_sethide.hide);
        break;
    case PKT_NAME_IF_SETCOLOUR:
        /* Persisting, like IF_SETTEXT and IF_SETHIDE above, and for the same
         * reason: a colour written in the tick its interface is mounted has no
         * node to land on yet. This applied straight to the tree, so it was the
         * one setter of the family that could be silently dropped — and it was,
         * every time, for an overlay the server opens and colours together. */
        if( ctx->app )
            App_IfColourSet(
                ctx->app,
                packet->_if_setcolour.component_id,
                rs15_to_rgb(packet->_if_setcolour.colour));
        else
            UITree_ApplyColour(
                ctx->tree,
                packet->_if_setcolour.component_id,
                rs15_to_rgb(packet->_if_setcolour.colour));
        break;
    case PKT_NAME_IF_SETMODEL:
        /* A wire model id addresses the cache, not the renderer's scene. The
         * App owns the asynchronous cache-load/upload bridge and persistence
         * across an interface remount. */
        if( ctx->app )
            App_SetInterfaceModel(
                ctx->app,
                packet->_if_setmodel.component_id,
                packet->_if_setmodel.model_id);
        else
            UITree_ApplyModel(
                ctx->tree, packet->_if_setmodel.component_id, packet->_if_setmodel.model_id);
        break;
    case PKT_NAME_IF_SETOBJECT:
        /* Reference IfType.getModel type 4: the obj's 3D interface model (the
         * server drives the combat-tab weapon display with this on equip). */
        if( ctx->app )
            App_SetInterfaceObjModel(
                ctx->app,
                packet->_if_setobject.component_id,
                packet->_if_setobject.obj_id,
                packet->_if_setobject.zoom);
        break;
    case PKT_NAME_IF_SETNPCHEAD:
        /* Reference IfType.getModel type 2: composite the npc chathead and bind
         * it. Needs the App (async model loads via the exec runner). */
        if( ctx->app )
            App_SetInterfaceNpcHead(
                ctx->app, packet->_if_setnpchead.component_id, packet->_if_setnpchead.npc_id);
        break;
    case PKT_NAME_IF_SETPLAYERHEAD:
        /* Reference IfType.getModel type 3: local-player chathead. */
        if( ctx->app )
            App_SetInterfacePlayerHead(ctx->app, packet->_if_setplayerhead.component_id);
        break;
    case PKT_NAME_IF_SETANIM:
        /* Reference modelAnim: the chathead's talk/idle sequence. Persist via the
         * App (survives the chat interface mounting after the packet); fall back
         * to a direct field set for tests with no App. */
        if( ctx->app )
            App_SetInterfaceModelAnim(
                ctx->app, packet->_if_setanim.component_id, packet->_if_setanim.anim_id);
        else
            UITree_ApplyModelAnim(
                ctx->tree, packet->_if_setanim.component_id, packet->_if_setanim.anim_id);
        break;
    case PKT_NAME_IF_SETSCROLLPOS:
        UITree_ApplyScrollPos(
            ctx->tree, packet->_if_setscrollpos.component_id, 0, packet->_if_setscrollpos.pos);
        break;
    case PKT_NAME_IF_SETPOSITION:
        UITree_ApplyPosition(
            ctx->tree,
            packet->_if_setposition.component_id,
            packet->_if_setposition.x,
            packet->_if_setposition.z);
        break;
    case PKT_NAME_IF_SETROTATESPEED:
        UITree_ApplyModelRotateSpeed(
            ctx->tree,
            packet->_if_setrotatespeed.component_id,
            packet->_if_setrotatespeed.x_speed,
            packet->_if_setrotatespeed.y_speed);
        break;
    case PKT_NAME_IF_SETANGLE:
        UITree_ApplyModelAngle(
            ctx->tree,
            packet->_if_setangle.component_id,
            packet->_if_setangle.angle_x,
            packet->_if_setangle.angle_y,
            packet->_if_setangle.zoom);
        break;
    case PKT_NAME_IF_CLEARINV:
        exec_if_clearinv_node(
            ctx->tree,
            UITree_FindByComponentId(ctx->tree, packet->_if_clearinv.component_id));
        break;
    case PKT_NAME_IF_SETNPCHEAD_ACTIVE:
        if( ctx->app && ctx->app->world )
        {
            int world_idx;
            if( RS_EntitySync_FindNpc(
                    &ctx->app->esync,
                    packet->_if_setnpchead_active.index,
                    &world_idx,
                    NULL) )
            {
                struct WorldEntity_NPC* npc =
                    World_EntityPoolGet(&ctx->app->world->entities.npc, world_idx);
                if( npc )
                    App_SetInterfaceNpcHead(
                        ctx->app,
                        packet->_if_setnpchead_active.component_id,
                        npc->npc_id);
            }
        }
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR:
        if( ctx->app )
            App_SetInterfacePlayerModelBaseColour(
                ctx->app,
                packet->_if_setplayermodel.component_id,
                packet->_if_setplayermodel.index,
                packet->_if_setplayermodel.value);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE:
        if( ctx->app )
            App_SetInterfacePlayerModelBodyType(
                ctx->app,
                packet->_if_setplayermodel.component_id,
                packet->_if_setplayermodel.value);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_OBJ:
        if( ctx->app )
            App_SetInterfacePlayerModelObj(
                ctx->app,
                packet->_if_setplayermodel.component_id,
                packet->_if_setplayermodel.value);
        break;
    case PKT_NAME_IF_SETPLAYERMODEL_SELF:
        if( ctx->app )
            App_SetInterfacePlayerModelSelf(
                ctx->app,
                packet->_if_setplayermodel.component_id,
                packet->_if_setplayermodel.value);
        break;

    /* ---- interface slots (need the App / RS_UISlots) ---- */
    case PKT_NAME_IF_OPENMAIN:
        if( ctx->app )
            RS_UISlots_OpenMain(ctx->app, packet->_if_openmain.component_id);
        break;
    case PKT_NAME_IF_OPENSIDE:
        if( ctx->app )
            RS_UISlots_OpenSide(ctx->app, packet->_if_openside.component_id);
        break;
    case PKT_NAME_IF_OPENMAIN_SIDE:
        if( ctx->app )
            RS_UISlots_OpenMainSide(
                ctx->app,
                packet->_if_openmain_side.main_component_id,
                packet->_if_openmain_side.side_component_id);
        break;
    case PKT_NAME_IF_OPENCHAT:
        if( ctx->app )
            RS_UISlots_OpenChat(ctx->app, packet->_if_openchat.component_id);
        break;
    case PKT_NAME_IF_CLOSE:
        if( ctx->app )
            RS_UISlots_CloseModal(ctx->app);
        break;
    case PKT_NAME_IF_SETTAB:
        if( ctx->app )
            RS_UISlots_SetTab(ctx->app, packet->_if_settab.tab_id, packet->_if_settab.component_id);
        break;

    /* ---- modern gameframe interfaces (rev-230 openTop/openSub) ---- */
    case PKT_NAME_IF_OPENTOP:
    {
        /* Switch the gameframe root when the server asks for a different top
         * (Display Fixed/Classic/Modern). Reject non-positive ids — a live
         * server once sent 0 and wiped the tree. Same-id is a no-op. */
        int top = packet->_if_opentop.interface_id;
        int cur = ctx->app ? ctx->app->boot_interface_id : -1;
        if( !ctx->app || top <= 0 )
            break;
        if( top == cur )
            break;
        fprintf(stderr, "if-opentop: switching root %d -> %d\n", cur, top);
        /*
         * The arming goes with the old root, and that is not tidiness.
         *
         * A real client rebuilds its whole widget state on an IF_OPENTOP and
         * the events map goes with it: measured against RuneLite, everything
         * armed before a second open answered no click afterwards, with
         * `pkt.log.arm` reporting `(component default)` for every cell.
         *
         * Keeping it here made this client MORE forgiving than the one the
         * server has to satisfy, which is the worst way to differ: a server
         * that opens a top twice and arms in between works perfectly in-house
         * and is dead in the real client. Dropping it is what made that bug
         * visible from this side too.
         */
        App_IfEventsClear(ctx->app);
        App_OpenRootInterface(ctx->app, top);
        break;
    }
    case PKT_NAME_IF_OPENSUB:
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "if-opensub: iface=%d target=0x%08x (%d<<16|%d) type=%d\n",
                packet->_if_opensub.interface_id,
                (unsigned)packet->_if_opensub.target_uid,
                (packet->_if_opensub.target_uid >> 16) & 0xffff,
                packet->_if_opensub.target_uid & 0xffff,
                packet->_if_opensub.type);
        if( ctx->app )
            App_OpenSubInterface(
                ctx->app,
                packet->_if_opensub.target_uid,
                packet->_if_opensub.interface_id,
                packet->_if_opensub.type);
        break;
    case PKT_NAME_IF_CLOSESUB:
        if( ctx->app )
            App_CloseSubInterface(ctx->app, packet->_if_closesub.target_uid);
        break;
    case PKT_NAME_IF_MOVESUB:
        if( ctx->app )
            App_MoveSubInterface(
                ctx->app, packet->_if_movesub.source_uid, packet->_if_movesub.dest_uid);
        break;
    case PKT_NAME_IF_RESYNC_V2:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            struct PktIfResync const* sync = &packet->_if_resync;
            int root_changed = sync->root_interface_id > 0 &&
                               sync->root_interface_id != app->boot_interface_id;
            struct UITreeInterfaceParent live[UITREE_INTERFACE_PARENT_MAX];
            int live_count = 0;

            if( root_changed )
                App_OpenRootInterface(app, sync->root_interface_id);

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
            if( app->tree )
            {
                live_count = app->tree->interface_parent_count;
                if( live_count > UITREE_INTERFACE_PARENT_MAX )
                    live_count = UITREE_INTERFACE_PARENT_MAX;
                memcpy(
                    live,
                    app->tree->interface_parents,
                    (size_t)live_count * sizeof(live[0]));
            }
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
            for( int i = 0; i < sync->mount_count; i++ )
            {
                int mounted = 0;
                for( int j = 0; !mounted && j < live_count; j++ )
                    mounted = live[j].container_uid == sync->mounts[i].target_uid &&
                              live[j].group_id == sync->mounts[i].interface_id &&
                              live[j].type == sync->mounts[i].type;
                if( mounted )
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
        }
        break;
    case PKT_NAME_RUNCLIENTSCRIPT:
        if( ctx->app )
            App_RunClientScript(ctx->app, packet->_runclientscript);
        break;
    case PKT_NAME_TRIGGER_ONDIALOGABORT:
        exec_trigger_on_dialog_abort(ctx);
        break;

    /* ---- chat ---- */
    case PKT_NAME_MESSAGE_GAME:
        /* The TYPE is the server's, not a constant: it picks the filter tab,
         * the colour and the prefix the chatbox script draws the line with. */
        exec_chat_add(
            ctx,
            packet->_message_game.type,
            packet->_message_game.name,
            packet->_message_game.name,
            packet->_message_game.text);
        /* Independent of the chatbox: a headless run has no chat model, and a
         * plugin watching for a boss kill has to work there too. */
        if( ctx->app )
            App_NotifyChatMessage(
                ctx->app,
                packet->_message_game.type,
                packet->_message_game.name,
                packet->_message_game.text);
        /*
         * Printed by default, and that is a deliberate inversion.
         *
         * This used to be gated on TORIRS_NET_DEBUG — the whole packet
         * firehose — which made a script's own `mes` output invisible to
         * anyone who had not already guessed that variable. A headless run
         * paints no chatbox, so stderr is the only channel content has, and
         * `mes` is how a debugproc reports what it found. Gating the one line
         * a test harness exists to read behind a general-purpose trace switch
         * cost this session three misdiagnoses: a probe that ran correctly and
         * printed nothing reads exactly like a script that aborted.
         *
         * The volume does not justify a gate — login sends five of these and a
         * busy tick a handful — so the switch is an opt-OUT for a caller that
         * genuinely wants silence.
         */
        {
            char const* quiet = getenv("TORIRS_MES");

            if( !quiet || strcmp(quiet, "0") != 0 )
                fprintf(stderr, "message_game: %s\n", packet->_message_game.text);
        }
        break;

    /* ---- world rebuild ---- */
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
        /* Handled inside Task_GameProtoExec (world-load await + MAP_BUILD_
         * COMPLETE ack); reaching here means no app context. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: %s zone=%d,%d (no app ctx)\n",
                packet->_map_rebuild.zones ? "REBUILD_REGION" : "REBUILD_NORMAL",
                packet->_map_rebuild.zonex,
                packet->_map_rebuild.zonez);
        break;
    case PKT_NAME_PLAYER_INFO:
    case PKT_NAME_NPC_INFO:
        /* Normally consumed by Task_GameProtoExec's awaited entity-info
         * tasks; reaching here means no world is active (packet dropped). */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: entity info packet %d dropped (no active world)\n",
                packet->packet_type);
        break;

    /* ---- zone packets (world mutations) ---- */
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
        if( ctx->app )
        {
            /* Wire base is classic-scene local; with a (zone-6)*8 scene base
             * that is our-scene too. */
            ctx->app->zone_base_x = packet->_update_zone_partial_follows.base_x;
            ctx->app->zone_base_z = packet->_update_zone_partial_follows.base_z;
            ctx->app->zone_level = zone_header_level(
                ctx->app, packet->_update_zone_partial_follows.level);
        }
        break;
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            app->zone_base_x = packet->_update_zone_full_follows.base_x;
            app->zone_base_z = packet->_update_zone_full_follows.base_z;
            app->zone_level = zone_header_level(app, packet->_update_zone_full_follows.level);
            /* Full update: the zone's client-side obj stacks reset. */
            if( app->world )
            {
                for( int dz = 0; dz < 8; dz++ )
                    for( int dx = 0; dx < 8; dx++ )
                        App_WorldObjStackClearTile(
                            app, app->zone_base_x + dx, app->zone_base_z + dz, app->zone_level);
            }
        }
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        if( ctx->app )
        {
            struct PktUpdateZoneEnclosed const* enc = &packet->_update_zone_enclosed;
            /* Wire base is classic-scene local; with a (zone-6)*8 scene base
             * that is our-scene too. */
            ctx->app->zone_base_x = enc->base_x;
            ctx->app->zone_base_z = enc->base_z;
            ctx->app->zone_level = zone_header_level(ctx->app, enc->level);
            for( int i = 0; i < enc->count; i++ )
                exec_zone_sub_packet(ctx, enc->entries[i].name, &enc->entries[i]._loc_add_change);
        }
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
    case PKT_NAME_SOUND_AREA:
        exec_zone_sub_packet(ctx, PKT_NAME_SOUND_AREA, &packet->_sound_area);
        break;
    case PKT_NAME_MAP_PROJANIM:
        exec_zone_sub_packet(ctx, PKT_NAME_MAP_PROJANIM, &packet->_map_projanim);
        break;
    case PKT_NAME_LOC_MERGE:
        exec_zone_sub_packet(ctx, PKT_NAME_LOC_MERGE, &packet->_loc_merge);
        break;

    /* ---- camera ---- */
    case PKT_NAME_CAM_MOVETO:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            /* Classic wire coords are scene-local tiles; the V2 packet's are
             * absolute world tiles (packet->absolute), converted here against
             * the scene the shot plays in. These only record where the camera
             * is headed; App_CinemaCamera walks it there. */
            int cam_lx = packet->_cam_moveto.local_x;
            int cam_lz = packet->_cam_moveto.local_z;
            if( packet->_cam_moveto.absolute && app->world )
            {
                cam_lx -= app->world->_base_tile_x;
                cam_lz -= app->world->_base_tile_z;
            }
            app->cam_script.move_lx = cam_lx;
            app->cam_script.move_lz = cam_lz;
            app->cam_script.move_height = packet->_cam_moveto.height;
            app->cam_script.move_rate = packet->_cam_moveto.rate;
            app->cam_script.move_rate2 = packet->_cam_moveto.rate2;
            if( !app->cam_script.scripted )
            {
                /* First op of a cutscene: aim at where we already are, so the
                 * unset half of the pair does not swing the shot to tile 0. */
                app->cam_script.look_lx = cam_lx;
                app->cam_script.look_lz = cam_lz;
            }
            app->cam_script.scripted = 1;
            if( packet->_cam_moveto.rate2 >= 100 )
                App_CinemaCameraSnapPosition(app);
            app->need_redraw = 1;
        }
        break;
    case PKT_NAME_CAM_LOOKAT:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            /* Same absolute-vs-scene-local split as CAM_MOVETO above. */
            int cam_lx = packet->_cam_lookat.local_x;
            int cam_lz = packet->_cam_lookat.local_z;
            if( packet->_cam_lookat.absolute && app->world )
            {
                cam_lx -= app->world->_base_tile_x;
                cam_lz -= app->world->_base_tile_z;
            }
            app->cam_script.look_lx = cam_lx;
            app->cam_script.look_lz = cam_lz;
            app->cam_script.look_height = packet->_cam_lookat.height;
            app->cam_script.look_rate = packet->_cam_lookat.rate;
            app->cam_script.look_rate2 = packet->_cam_lookat.rate2;
            if( !app->cam_script.scripted )
            {
                app->cam_script.move_lx = app->world_camera_pos.x / 128;
                app->cam_script.move_lz = app->world_camera_pos.z / 128;
            }
            app->cam_script.scripted = 1;
            if( packet->_cam_lookat.rate2 >= 100 )
                App_CinemaCameraSnapAngle(app);
            app->need_redraw = 1;
        }
        break;
    case PKT_NAME_CAM_SHAKE:
        if( ctx->app )
        {
            /* The wire order is axis, spread, amplitude, rate — the packet's
             * field names predate knowing which was which. */
            int axis = packet->_cam_shake.axis;
            if( axis >= 0 && axis < 5 )
            {
                struct App* app = ctx->app;
                app->cam_script.shake[axis] = 1;
                app->cam_script.shake_jitter[axis] = packet->_cam_shake.amplitude;
                app->cam_script.shake_amplitude[axis] = packet->_cam_shake.frequency;
                app->cam_script.shake_speed[axis] = packet->_cam_shake.speed;
                app->cam_script.shake_cycle[axis] = 0;
            }
        }
        break;
    case PKT_NAME_CAM_RESET:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            app->cam_script.scripted = 0;
            for( int i = 0; i < 5; i++ )
                app->cam_script.shake[i] = 0;
            app->need_redraw = 1;
        }
        break;

    /* ---- social ---- */
    case PKT_NAME_UPDATE_FRIENDLIST:
        if( ctx->app )
        {
            char name[16];
            struct RS_Social* social = &ctx->app->social;
            int found = 0;
            social->server_status = 2;
            if( !packet->_update_friendlist.present )
            {
                RS_CS2Host_NotifyFriendChanged(&ctx->app->host);
                ctx->app->need_redraw = 1;
                break;
            }
            base37tostr((uint64_t)packet->_update_friendlist.name37, name, sizeof(name));
            for( int i = 0; i < social->friend_count; i++ )
            {
                if( strcmp(social->friend_name[i], name) == 0 )
                {
                    social->friend_world[i] = packet->_update_friendlist.world;
                    found = 1;
                    break;
                }
            }
            if( !found )
                RS_Social_AddFriend(social, name, packet->_update_friendlist.world);
            /* The friends panel's rows are cc_created from this store by the
             * client's own script 125, so nothing repaints them but the
             * friend-transmit channel. Filling the store without this notify
             * leaves the panel showing whatever it drew at mount. */
            RS_CS2Host_NotifyFriendChanged(&ctx->app->host);
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_UPDATE_IGNORELIST:
        if( ctx->app )
        {
            struct RS_Social* social = &ctx->app->social;
            social->ignore_count = 0;
            for( int i = 0; i < packet->_update_ignorelist.count; i++ )
            {
                char name[16];
                base37tostr((uint64_t)packet->_update_ignorelist.names37[i], name, sizeof(name));
                RS_Social_AddIgnore(social, name);
            }
            RS_CS2Host_NotifyFriendChanged(&ctx->app->host);
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_FRIENDLIST_LOADED:
        if( ctx->app )
        {
            /* This is what ends the panel's "Loading friends list" state:
             * RS_Social_FriendCount answers -1 until the status reads 2. */
            ctx->app->social.server_status = packet->_friendlist_loaded.status;
            RS_CS2Host_NotifyFriendChanged(&ctx->app->host);
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_MESSAGE_PRIVATE:
        if( ctx->app && ctx->chat )
        {
            /* Dedupe by message id (reference messageIds ring). */
            struct App* app = ctx->app;
            char name[16];
            int dup = 0;
            for( int i = 0; i < 100; i++ )
                if( app->pm_message_ids[i] == packet->_message_private.message_id )
                {
                    dup = 1;
                    break;
                }
            if( dup )
                break;
            app->pm_message_ids[app->pm_message_head] = packet->_message_private.message_id;
            app->pm_message_head = (app->pm_message_head + 1) % 100;
            base37tostr((uint64_t)packet->_message_private.from, name, sizeof(name));
            exec_chat_add(
                ctx,
                packet->_message_private.staff_mod ? RS_CHAT_TYPE_PRIVATE_FROM_MOD
                                                   : RS_CHAT_TYPE_PRIVATE_FROM,
                name,
                name,
                packet->_message_private.text);
            if( ctx->app )
                App_NotifyChatMessage(
                    ctx->app,
                    packet->_message_private.staff_mod ? RS_CHAT_TYPE_PRIVATE_FROM_MOD
                                                       : RS_CHAT_TYPE_PRIVATE_FROM,
                    name,
                    packet->_message_private.text);
        }
        break;
    case PKT_NAME_CHAT_FILTER_SETTINGS:
        if( ctx->app )
        {
            ctx->app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC] =
                packet->_chat_filter_settings.chat_public_mode;
            ctx->app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE] =
                packet->_chat_filter_settings.chat_private_mode;
            ctx->app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_TRADE] =
                packet->_chat_filter_settings.chat_trade_mode;
            /* Script 681 reads these back through chat_getfilter_private before
             * it will send a private message, so a change has to reach the CS2
             * side the same way a friend-list change does. */
            RS_CS2Host_NotifyFriendChanged(&ctx->app->host);
            ctx->app->need_redraw = 1;
        }
        break;

    /* ---- misc state ---- */
    case PKT_NAME_UPDATE_PID:
        if( ctx->app )
            ctx->app->esync.local_pid = packet->_update_pid.local_player_index;
        break;
    case PKT_NAME_VARP_SYNC:
        /* Restore the client-visible copy from the server-authoritative set
         * (rev-230 client.java VARP_SYNC / LostCity Client.ts:7390). Distinct
         * from VARP_RESET, which zeroes both. */
        VarPManager_ApplySync(ctx->varps);
        break;
    case PKT_NAME_VARP_RESET:
        VarPManager_ResetAll(ctx->varps);
        break;
    case PKT_NAME_LAST_LOGIN_INFO:
        if( ctx->app )
        {
            ctx->app->welcome.last_ip = packet->_last_login_info.last_ip;
            ctx->app->welcome.days_since_login = packet->_last_login_info.days_since_login;
            ctx->app->welcome.days_since_recovery = packet->_last_login_info.days_since_recovery;
            ctx->app->welcome.unread_messages = packet->_last_login_info.unread_messages;
            ctx->app->welcome.member_warning = packet->_last_login_info.member_warning;
            /* The rows themselves are clientCode components RS_ClientCode_Tick
             * refreshes off this struct; this packet also OPENS the screen they
             * sit on, which is the only thing that ever does. */
            exec_open_welcome_screen(ctx->app);
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_UPDATE_REBOOT_TIMER:
        /* The wire carries SERVER ticks; the countdown runs on the 20ms client
         * cycle, so it is converted once here rather than at every read
         * (reference `rebootTimer = g2() * 30`, where 30 is the same ratio
         * spelled as a literal). */
        if( ctx->app )
            ctx->app->reboot_timer =
                packet->_update_reboot_timer.ticks * APP_SERVER_TICK_LOGIC_CYCLES;
        break;
    case PKT_NAME_P_COUNTDIALOG:
        if( ctx->chat )
        {
            ctx->chat->dialog_input_open = 1;
            ctx->chat->dialog_input[0] = '\0';
        }
        break;
    case PKT_NAME_LOGOUT:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            /* Shared with the connection-lost path in app.c: a session that
             * ends is a session that ends, however it was told to. */
            App_NetSessionReset(app);
            exec_chat_add(ctx, RS_CHAT_TYPE_GAME, NULL, NULL, "You have been logged out.");
            if( app->net )
                ToriRS_Network_Logout(app->net);
            app->need_redraw = 1;
        }
        break;
    case PKT_NAME_SET_PLAYER_OP:
        if( ctx->app && packet->_set_player_op.slot >= 1 && packet->_set_player_op.slot <= 5 )
        {
            int slot = packet->_set_player_op.slot - 1;
            snprintf(
                ctx->app->player_ops[slot],
                sizeof(ctx->app->player_ops[slot]),
                "%s",
                packet->_set_player_op.text ? packet->_set_player_op.text : "");
            ctx->app->player_ops_primary[slot] = packet->_set_player_op.primary;
        }
        break;
    case PKT_NAME_SET_MULTIWAY:
        if( ctx->app && ctx->app->multiway != packet->_set_multiway.multiway )
        {
            ctx->app->multiway = packet->_set_multiway.multiway;
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_MINIMAP_TOGGLE:
        if( ctx->app && ctx->app->minimap_state != packet->_minimap_toggle.state )
        {
            ctx->app->minimap_state = packet->_minimap_toggle.state;
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_HINT_ARROW:
        if( ctx->app )
        {
            ctx->app->hint_arrow.type =
                packet->_hint_arrow.type == 255 ? 0 : packet->_hint_arrow.type;
            ctx->app->hint_arrow.target = packet->_hint_arrow.id;
            ctx->app->hint_arrow.tile_z = packet->_hint_arrow.z;
            ctx->app->hint_arrow.height = packet->_hint_arrow.height;
        }
        break;
    case PKT_NAME_RESET_ANIMS:
        if( ctx->app && ctx->app->scene )
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
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_ENABLE_TRACKING:
        if( ctx->app )
            ctx->app->tracking_enabled = 1;
        break;
    case PKT_NAME_FINISH_TRACKING:
        if( ctx->app )
            ctx->app->tracking_enabled = 0;
        break;

    /* ---- tutorial ---- */
    case PKT_NAME_TUT_FLASH:
        if( ctx->app )
        {
            ctx->app->slots.flash_tab = packet->_tut_flash.tab;
            /* Asked to flash the tab the player is already looking at, the
             * reference moves them OFF it -- an open panel covers its own icon,
             * so a blink nobody can see is no instruction at all. 3 is the
             * inventory, which is where it sends you unless that IS the tab. */
            if( packet->_tut_flash.tab >= 0 &&
                packet->_tut_flash.tab == ctx->app->slots.side_tab )
            {
                ctx->app->slots.side_tab = packet->_tut_flash.tab == 3 ? 1 : 3;
                ctx->app->need_redraw = 1;
            }
        }
        break;
    case PKT_NAME_TUT_OPEN:
        if( ctx->app )
            RS_UISlots_OpenTut(ctx->app, packet->_tut_open.component_id);
        break;

    /* ---- audio ---- */
    case PKT_NAME_SYNTH_SOUND:
        if( ctx->app )
            App_PlaySound(
                ctx->app,
                packet->_synth_sound.id,
                packet->_synth_sound.loops,
                packet->_synth_sound.delay);
        break;
    case PKT_NAME_MIDI_SONG_WITHSECONDARY:
        /*
         * Same as MIDI_SONG, plus a variant of the same arrangement held ready
         * for a later MIDI_SWAP. Tracks always loop.
         */
        if( ctx->app )
            App_PlaySongWithSecondary(
                ctx->app,
                packet->_midi_song_with_secondary.primary_id,
                packet->_midi_song_with_secondary.secondary_id,
                packet->_midi_song_with_secondary.fade_out_ms,
                packet->_midi_song_with_secondary.fade_in_ms);
        break;
    case PKT_NAME_MIDI_SWAP:
        if( ctx->app )
            App_SwapSong(
                ctx->app,
                packet->_midi_swap.fade_out_ms,
                packet->_midi_swap.fade_in_ms);
        break;
    case PKT_NAME_MIDI_SONG:
        /*
         * The V2 form the modern revisions send carries a fade envelope; the
         * older one does not and decodes to zeroes, which reads correctly as
         * "switch immediately". A track always loops -- it is background music,
         * and the server only ever replaces it.
         */
        if( ctx->app )
            App_PlaySong(
                ctx->app,
                packet->_midi_song.id,
                true,
                packet->_midi_song.fade_out_ms,
                packet->_midi_song.fade_in_ms);
        break;
    case PKT_NAME_MIDI_JINGLE:
        if( ctx->app )
            App_PlayJingle(ctx->app, packet->_midi_jingle.id, packet->_midi_jingle.delay);
        break;
    case PKT_NAME_MIDI_SONG_STOP:
        if( ctx->app )
            App_StopSong(ctx->app, packet->_midi_song_stop.fade_out_ms);
        break;
    case PKT_NAME_AMBIENTSOUND_START:
        if( ctx->app )
            App_SetAmbientSound(
                ctx->app,
                packet->_ambientsound_start.id,
                packet->_ambientsound_start.fade_ms);
        break;
    case PKT_NAME_AMBIENTSOUND_STOP:
        if( ctx->app )
            App_SetAmbientSound(ctx->app, -1, packet->_ambientsound_stop.fade_ms);
        break;

    /* ---- remaining interface packets ---- */
    case PKT_NAME_IF_OPENOVERLAY:
        if( ctx->app )
            RS_UISlots_OpenOverlay(ctx->app, packet->_if_openoverlay.component_id);
        break;
    case PKT_NAME_IF_SETTAB_ACTIVE:
        if( ctx->app )
            RS_UISlots_SetSideTab(ctx->app, packet->_if_settab_active.tab_id);
        break;
    case PKT_NAME_UNSET_MAP_FLAG:
        /* Wire tiles are classic-scene local; with a (zone-6)*8 base that is
         * our-scene too. */
        if( ctx->app )
        {
            if( packet->_set_map_flag.clear )
            {
                ctx->app->minimap_flag_x = -1;
                ctx->app->minimap_flag_z = -1;
            }
            else if( packet->_set_map_flag.absolute )
            {
                /* SetMapFlagV2 carries an absolute coord; the minimap draws in
                 * scene-local tiles. Same origin the projectile decode uses. */
                ctx->app->minimap_flag_x =
                    packet->_set_map_flag.x - (ctx->app->rebuild_zone_x - 6) * 8;
                ctx->app->minimap_flag_z =
                    packet->_set_map_flag.z - (ctx->app->rebuild_zone_z - 6) * 8;
            }
            else
            {
                ctx->app->minimap_flag_x = packet->_set_map_flag.x;
                ctx->app->minimap_flag_z = packet->_set_map_flag.z;
            }
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        /* Reference clears the bound inventory's slots (linkObjType[i] = -1).
         * ApplyFull with count 0 empties every slot; a no-op if the container
         * was never transmitted. */
        if( ctx->invs )
            InvManager_ApplyFull(
                ctx->invs,
                exec_inv_container_id(
                    packet->_update_inv_stop_transmit.inv_id,
                    packet->_update_inv_stop_transmit.component_id),
                NULL,
                NULL,
                0);
        break;
    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
        if( ctx->app )
        {
            ctx->app->npc_update_origin_x = packet->_set_npc_update_origin.x;
            ctx->app->npc_update_origin_z = packet->_set_npc_update_origin.z;
            ctx->app->npc_update_origin_valid = 1;
        }
        break;
    case PKT_NAME_SERVER_TICK_END:
        /* The fence. Every packet of this tick has been applied, so the scripts
         * the tick pushed can now repaint against the state the server meant
         * them to see — see `pending_clientscripts` in app.h. */
        if( ctx->app )
        {
            ctx->app->server_tick_fence_seen = 1;
            ctx->app->server_tick_open = 0;
            App_FlushPendingClientScripts(ctx->app);
            /* The 600ms cadence a plugin actually wants: every packet of this
             * server tick is now in world state, so a snapshot read here
             * agrees with what the server believes. Anything sampled mid-tick
             * is reading a half-applied world. */
            PluginHost_ServerTick(
                ctx->app->plugins, ctx->app->world ? ctx->app->world->cycle : 0);
        }
        break;

    default:
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "gameproto_exec: unhandled packet %d\n", packet->packet_type);
        break;
    }
}
