#include "rs_gameproto_exec.h"

#include "app.h"
#include "inv/inv_manager.h"
#include "net/jbase37.h"
#include "net/net.h"
#include "net/rev/revpacket.h"
#include "rs_audio.h"
#include "rs_chat.h"
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
#include <string.h>

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
        struct InvSlot slot = { 0 };
        slot.obj_id = p->entries[i].obj_id;
        slot.obj_count = p->entries[i].count;
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

/* Zone sub-packet tile: base (scene-local, set by the FOLLOWS packets) +
 * packed nibbles. Level = the local player's plane. */
static void
zone_tile(
    struct App* app,
    int pos,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    *out_x = app->zone_base_x + ((pos >> 4) & 7);
    *out_z = app->zone_base_z + (pos & 7);
    *out_level = 0;
    if( app->esync.local_pid >= 0 || 1 )
    {
        int pid = app->esync.local_pid >= 0 ? app->esync.local_pid : 2047;
        if( RS_EntitySync_FindPlayer(&app->esync, pid, &world_idx, NULL) )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&app->world->entities.player, world_idx);
            if( player )
                *out_level = player->grid_position.level;
        }
    }
}

static void
exec_zone_sub_packet(
    struct RS_GameProtoCtx const* ctx,
    enum GameProtoPktName name,
    void const* payload)
{
    struct App* app = ctx->app;
    int tile_x, tile_z, level;

    if( !app || !app->world || !app->world->load_complete )
        return;

    switch( name )
    {
    case PKT_NAME_OBJ_ADD:
    {
        struct PktObjAdd const* pkt = payload;
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackAdd(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
        break;
    }
    case PKT_NAME_OBJ_DEL:
    {
        struct PktObjDel const* pkt = payload;
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackDel(app, tile_x, tile_z, level, pkt->obj_id);
        break;
    }
    case PKT_NAME_OBJ_COUNT:
    {
        struct PktObjCount const* pkt = payload;
        int idx;
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
        idx = World_ObjStackFind(app->world, tile_x, tile_z, level, pkt->obj_id);
        if( idx >= 0 )
            World_ObjStackSetCount(app->world, idx, pkt->new_count);
        break;
    }
    case PKT_NAME_OBJ_REVEAL:
    {
        /* Reveal targets one receiver; everyone else already sees it. */
        struct PktObjReveal const* pkt = payload;
        if( ctx->app->esync.local_pid >= 0 && pkt->receiver != ctx->app->esync.local_pid )
            break;
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldObjStackAdd(app, tile_x, tile_z, level, pkt->obj_id, pkt->count);
        break;
    }
    case PKT_NAME_LOC_DEL:
    {
        struct PktLocDel const* pkt = payload;
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
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
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
        App_WorldLocChange(
            app, tile_x, tile_z, level, pkt->loc_id, pkt->info >> 2, pkt->info & 0x3);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: LOC_ADD_CHANGE loc=%d shape=%d angle=%d at %d,%d\n",
                pkt->loc_id,
                pkt->info >> 2,
                pkt->info & 0x3,
                tile_x,
                tile_z);
        break;
    }
    case PKT_NAME_LOC_ANIM:
    {
        struct PktLocAnim const* pkt = payload;
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
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
            zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
            App_WorldSpotanimSpawn(app, tile_x, tile_z, level, pkt->id, pkt->height, pkt->delay);
        }
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
        zone_tile(app, pkt->pos, &tile_x, &tile_z, &level);
        src_tx = tile_x;
        src_tz = tile_z;
        dst_tx = tile_x + pkt->dx_offset;
        dst_tz = tile_z + pkt->dz_offset;
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
        /* LOC_MERGE (a drawing-order hint) remains a flagged follow-on. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "gameproto_exec: zone sub-packet %d stored (visual pending)\n", name);
        break;
    default:
        break;
    }
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
            /* The reactive half. Without it the skills tab paints once at
             * build time — against the zeroes the client starts with — and
             * nothing ever asks it to paint again, which is the same shape as
             * the inv-transmit gap that left server-driven inventories blank. */
            if( ctx->app )
                RS_CS2Host_NotifyStatChanged(&ctx->app->host, packet->_update_stat.stat);
            RS_PlayerStats_RecomputeCombatLevel(ctx->stats);
        }
        break;
    case PKT_NAME_UPDATE_RUNENERGY:
        ctx->stats->run_energy = packet->_update_run_energy.run_energy;
        break;
    case PKT_NAME_UPDATE_RUNWEIGHT:
        ctx->stats->run_weight = packet->_update_runweight.run_weight;
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
            App_IfTextSet(ctx->app, packet->_if_settext.component_id, packet->_if_settext.text);
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
        UITree_ApplyColour(
            ctx->tree,
            packet->_if_setcolour.component_id,
            rs15_to_rgb(packet->_if_setcolour.colour));
        break;
    case PKT_NAME_IF_SETMODEL:
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
        /* IF_OPENTOP names the gameframe root (TS setRootInterface). This client
         * already boots its gameframe (161) from the manifest and mounts every
         * server sub-interface INTO it, so the booted root is authoritative and
         * IF_OPENTOP is informational — we do NOT reboot on it. Rebooting here was
         * destructive: a stray/zero id (the live server sent 0) rebuilt the tree to
         * an empty root, wiping the live 161 gameframe and leaving a panel covering
         * the viewport. Log the mismatch for visibility; a real gameframe *switch*
         * (161<->548 fixed) would need proper sub-remount handling, not a raw reboot. */
        int top = packet->_if_opentop.interface_id;
        int boot = ctx->app ? ctx->app->boot_interface_id : -1;
        if( top != boot )
            fprintf(
                stderr,
                "if-opentop: server root=%d differs from booted root=%d; keeping booted root\n",
                top,
                boot);
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
    case PKT_NAME_RUNCLIENTSCRIPT:
        if( ctx->app )
            App_RunClientScript(ctx->app, &packet->_runclientscript);
        break;

    /* ---- chat ---- */
    case PKT_NAME_MESSAGE_GAME:
        if( ctx->chat )
            RS_Chat_AddMessage(ctx->chat, RS_CHAT_TYPE_GAME, NULL, packet->_message_game.text);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "message_game: %s\n", packet->_message_game.text);
        break;

    /* ---- world rebuild ---- */
    case PKT_NAME_REBUILD_NORMAL:
        /* Handled inside Task_GameProtoExec (world-load await + MAP_BUILD_
         * COMPLETE ack); reaching here means no app context. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: REBUILD_NORMAL zone=%d,%d (no app ctx)\n",
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
            /* Wire base is classic-scene local; store our-scene tiles. */
            ctx->app->zone_base_x =
                ctx->app->scene_off_x + packet->_update_zone_partial_follows.base_x;
            ctx->app->zone_base_z =
                ctx->app->scene_off_z + packet->_update_zone_partial_follows.base_z;
        }
        break;
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        if( ctx->app )
        {
            struct App* app = ctx->app;
            app->zone_base_x = app->scene_off_x + packet->_update_zone_full_follows.base_x;
            app->zone_base_z = app->scene_off_z + packet->_update_zone_full_follows.base_z;
            /* Full update: the zone's client-side obj stacks reset. */
            if( app->world )
            {
                for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
                    for( int dz = 0; dz < 8; dz++ )
                        for( int dx = 0; dx < 8; dx++ )
                        {
                            int idx;
                            while( (idx = World_ObjStackFind(
                                        app->world,
                                        app->zone_base_x + dx,
                                        app->zone_base_z + dz,
                                        level,
                                        -1)) >= 0 )
                                World_ObjStackDel(app->world, idx);
                        }
            }
        }
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        if( ctx->app )
        {
            struct PktUpdateZoneEnclosed const* enc = &packet->_update_zone_enclosed;
            /* Wire base is classic-scene local, same as the FOLLOWS variants
             * above — without scene_off every enclosed LOC/OBJ mutation lands
             * up to 63 tiles off the REBUILD-loaded scenery. */
            ctx->app->zone_base_x = ctx->app->scene_off_x + enc->base_x;
            ctx->app->zone_base_z = ctx->app->scene_off_z + enc->base_z;
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
            /* Wire coords are classic-scene local tiles. These only record
             * where the camera is headed; App_CinemaCamera walks it there. */
            app->cam_script.move_lx = packet->_cam_moveto.local_x;
            app->cam_script.move_lz = packet->_cam_moveto.local_z;
            app->cam_script.move_height = packet->_cam_moveto.height;
            app->cam_script.move_rate = packet->_cam_moveto.rate;
            app->cam_script.move_rate2 = packet->_cam_moveto.rate2;
            if( !app->cam_script.scripted )
            {
                /* First op of a cutscene: aim at where we already are, so the
                 * unset half of the pair does not swing the shot to tile 0. */
                app->cam_script.look_lx = packet->_cam_moveto.local_x;
                app->cam_script.look_lz = packet->_cam_moveto.local_z;
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
            app->cam_script.look_lx = packet->_cam_lookat.local_x;
            app->cam_script.look_lz = packet->_cam_lookat.local_z;
            app->cam_script.look_height = packet->_cam_lookat.height;
            app->cam_script.look_rate = packet->_cam_lookat.rate;
            app->cam_script.look_rate2 = packet->_cam_lookat.rate2;
            if( !app->cam_script.scripted )
            {
                app->cam_script.move_lx = app->world_camera_pos.x / 128 - app->scene_off_x;
                app->cam_script.move_lz = app->world_camera_pos.z / 128 - app->scene_off_z;
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
            ctx->app->need_redraw = 1;
        }
        break;
    case PKT_NAME_FRIENDLIST_LOADED:
        if( ctx->app )
            ctx->app->social.server_status = packet->_friendlist_loaded.status;
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
            RS_Chat_AddMessage(
                ctx->chat,
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
            ctx->app->need_redraw = 1;
        }
        break;

    /* ---- misc state ---- */
    case PKT_NAME_UPDATE_PID:
        if( ctx->app )
            ctx->app->esync.local_pid = packet->_update_pid.local_player_index;
        break;
    case PKT_NAME_RESET_CLIENT_VARCACHE:
        VarPManager_ResetAll(ctx->varps);
        break;
    case PKT_NAME_LAST_LOGIN_INFO:
        if( ctx->app )
        {
            ctx->app->welcome.last_ip = packet->_last_login_info.last_ip;
            ctx->app->welcome.days_since_login = packet->_last_login_info.days_since_login;
            ctx->app->welcome.days_since_recovery = packet->_last_login_info.days_since_recovery;
            ctx->app->welcome.unread_messages = packet->_last_login_info.unread_messages;
        }
        break;
    case PKT_NAME_UPDATE_REBOOT_TIMER:
        if( ctx->app )
            ctx->app->reboot_ticks = packet->_update_reboot_timer.ticks;
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
            RS_EntitySync_Clear(&app->esync, app->world);
            if( ctx->chat )
                RS_Chat_AddMessage(ctx->chat, RS_CHAT_TYPE_GAME, NULL, "You have been logged out.");
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
        if( ctx->app )
            ctx->app->multiway = packet->_set_multiway.multiway;
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
            ctx->app->slots.flash_tab = packet->_tut_flash.tab;
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
    case PKT_NAME_MIDI_SONG:
        if( ctx->app )
            RS_Audio_Song(&ctx->app->audio, packet->_midi_song.id);
        break;
    case PKT_NAME_MIDI_JINGLE:
        if( ctx->app )
            RS_Audio_Jingle(&ctx->app->audio, packet->_midi_jingle.id, packet->_midi_jingle.delay);
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
        if( ctx->app )
        {
            ctx->app->minimap_flag_x = -1;
            ctx->app->minimap_flag_z = -1;
        }
        break;
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        /* Reference clears the bound inventory's slots (linkObjType[i] = -1).
         * ApplyFull with count 0 empties every slot; a no-op if the container
         * was never transmitted. */
        if( ctx->invs )
            InvManager_ApplyFull(
                ctx->invs, packet->_update_inv_stop_transmit.component_id, NULL, NULL, 0);
        break;

    default:
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "gameproto_exec: unhandled packet %d\n", packet->packet_type);
        break;
    }
}
