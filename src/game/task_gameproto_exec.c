#include "task_gameproto_exec.h"

#include "app.h"
#include "net/rev/gameproto_parse.h"
#include "rs_gameproto_exec.h"
#include "task_exec_entity_info.h"
#include "engine/world_builder/task_world_load.h"
#include "world/world.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_GameProtoExec
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    struct RevPacket packet; /* owned; heap fields freed on task free */

    /* Map squares to prefetch: the 3x3 under the scene for REBUILD_NORMAL, the
     * distinct source squares the descriptors name for REBUILD_REGION (which is
     * why this is larger than the 9 pairs the normal path needs). */
    int chunks[32 * 2];
    int chunk_count;
    /* Scene base (absolute tiles) before the load — the entity-shift delta
     * source (Client-TS mapBuildPrevBaseX/Z / deob method3310 var3/var4). */
    int prev_base_x;
    int prev_base_z;
    int had_world;
    int zone_x;
    int zone_z;
    /* Obj-load cursors (ground item models must be cached before exec). */
    int zone_i;
    int pending_obj_id;
};

/* The view a REBUILD packet addresses: its own world_area prefix (0 = root),
 * not the SET_ACTIVE_WORLD cursor — the deob reads the id off the rebuild
 * itself. Resolved fresh at every use because locals do not survive the
 * protothread await; Get() asserts the id names a live view, the same loud
 * stop as the deob's unknown-world-entity throw. */
static struct Worldview*
rebuild_view(struct Task_GameProtoExec* self)
{
    return WorldviewRegistry_Get(&self->app->worldviews, self->packet._map_rebuild.world_area);
}

/* Obj id referenced by a zone entry that spawns a ground item, or -1. */
static int
zone_entry_obj_id(struct PktZoneSubPacket const* entry)
{
    if( entry->name == PKT_NAME_OBJ_ADD )
        return entry->_obj_add.obj_id;
    if( entry->name == PKT_NAME_OBJ_REVEAL )
        return entry->_obj_reveal.obj_id;
    return -1;
}

/* Inventory-model load for the pending obj (config already loaded). */
static struct ToriRS_Task*
obj_ground_model_task(struct Task_GameProtoExec* self)
{
    struct ToriRS_Objtype* obj =
        CacheProvider_ObjtypeGet(self->app->provider, self->pending_obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return NULL;
    return CreateTask_ModelLoad(self->app->provider, obj->inventory_model_id);
}

void
rebuild_square_rect(
    int zone_x,
    int zone_z,
    int* mx0,
    int* mz0,
    int* mx1,
    int* mz1)
{
    int sw_tile_x = (zone_x - 6) * 8;
    int sw_tile_z = (zone_z - 6) * 8;

    assert(mx0 && mz0 && mx1 && mz1);
    *mx0 = sw_tile_x >> 6;
    *mz0 = sw_tile_z >> 6;
    *mx1 = (sw_tile_x + 103) >> 6;
    *mz1 = (sw_tile_z + 103) >> 6;
}

/* REBUILD_NORMAL zone -> map-square list covering the 104x104 scene window.
 * Rect is at most 3x3 by construction; hitting the cap is a programming error. */
static void
rebuild_compute_chunks(struct Task_GameProtoExec* self)
{
    int mx0, mz0, mx1, mz1;

    rebuild_square_rect(
        self->packet._map_rebuild.zonex, self->packet._map_rebuild.zonez, &mx0, &mz0, &mx1,
        &mz1);

    self->chunk_count = 0;
    for( int mx = mx0; mx <= mx1; mx++ )
        for( int mz = mz0; mz <= mz1; mz++ )
        {
            assert(self->chunk_count < 9 && "REBUILD_NORMAL square rect exceeds 3x3");
            self->chunks[self->chunk_count * 2] = mx;
            self->chunks[self->chunk_count * 2 + 1] = mz;
            self->chunk_count++;
        }
}

/*
 * REBUILD_REGION descriptors -> the distinct *source* map squares to prefetch.
 *
 * Not the squares under the scene: those are the instance's own reserved
 * coordinates, which by construction have no archives at all. The load has to
 * fetch what the descriptors point at instead, and duplicates matter — a house
 * is dozens of zones out of one or two squares, so the same square appears over
 * and over.
 *
 * Squares past the cap are dropped rather than asserted. An instance is allowed
 * to name more than a scene-sized rebuild ever would, and the failure mode of
 * dropping one is a missing zone, not a corrupt scene.
 */
static void
rebuild_region_compute_chunks(struct Task_GameProtoExec* self)
{
    const int32_t* zones = self->packet._map_rebuild.zones;
    int cap = (int)(sizeof(self->chunks) / sizeof(self->chunks[0])) / 2;

    self->chunk_count = 0;
    for( int i = 0; i < PKT_MAP_REBUILD_ZONES; i++ )
    {
        int mx;
        int mz;
        int seen = 0;

        if( zones[i] == 0 )
            continue;
        mx = ((zones[i] >> 14) & 0x3ff) >> 3;
        mz = ((zones[i] >> 3) & 0x7ff) >> 3;
        for( int c = 0; c < self->chunk_count && !seen; c++ )
            if( self->chunks[c * 2] == mx && self->chunks[c * 2 + 1] == mz )
                seen = 1;
        if( seen || self->chunk_count >= cap )
            continue;
        self->chunks[self->chunk_count * 2] = mx;
        self->chunks[self->chunk_count * 2 + 1] = mz;
        self->chunk_count++;
    }
}

static int
Task_GameProtoExec_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_GameProtoExec* self = (struct Task_GameProtoExec*)base;
    struct App* app = self->app;
    (void)io;

    PT_BEGIN(&self->pt);

    /* Entity command streams decode + apply inside their own awaited tasks
     * (spawn/appearance/seq cache loads). Everything else applies
     * synchronously; handlers that mutate interfaces enqueue slot-mount
     * tasks BEHIND this one on the same serial queue, so ordering with later
     * packets holds. */
    if( self->packet.packet_type == PKT_NAME_REBUILD_NORMAL ||
        self->packet.packet_type == PKT_NAME_REBUILD_REGION )
    {
        /* Client-TS / deob method3310: same-zone early-out when a world is
         * already active. No ack on skip (Client.ts:2289 acks from mapBuild).
         * REBUILD_REGION forces past it — see App_WorldRebuildBegin.
         *
         * So does the first rebuild of a re-established session, and for a
         * reason the early-out cannot see: a reconnect usually lands on the
         * same zone it left, which makes this look like a redundant rebuild
         * when it is the opposite. Skipping it sends no MAP_BUILD_COMPLETE,
         * and at this revision the server holds the entire login burst behind
         * that acknowledgement — so the session re-establishes, receives one
         * REBUILD_NORMAL, and then silence. */
        self->zone_x = self->packet._map_rebuild.zonex;
        self->zone_z = self->packet._map_rebuild.zonez;
        /* The tail below (App_WorldRebuildBegin's root flags, the entity
         * shift, camera/minimap, App_WorldLoadFinish) acts on the ROOT scene:
         * a boat's rebuild arrives as REBUILD_WORLDENTITY (plan C2) and never
         * through here. rebuild_view() already asserts the id is live; this
         * pins that at C0/C1 only the root can be addressed by these two
         * packets. */
        assert(self->packet._map_rebuild.world_area == WORLDVIEW_ROOT);
        {
            int force = self->packet.packet_type == PKT_NAME_REBUILD_REGION ||
                        app->net_force_rebuild;

            app->net_force_rebuild = 0;
            if( !App_WorldRebuildBegin(app, self->zone_x, self->zone_z, force) )
                PT_EXIT(&self->pt);
        }

        if( self->packet._map_rebuild.zones )
            rebuild_region_compute_chunks(self);
        else
            rebuild_compute_chunks(self);
        /* Entities carry scene-local coords relative to the old base;
         * capture it so they can be shifted onto the new one (Client-TS
         * shifts by mapBuildBaseX - mapBuildPrevBaseX). (world, builder)
         * resolve through rebuild_view() at every use rather than into a
         * local: locals do not survive the protothread await below. */
        self->had_world =
            app->world_active && app->world && rebuild_view(self)->world->load_complete;
        self->prev_base_x = self->had_world ? rebuild_view(self)->world->_base_tile_x : 0;
        self->prev_base_z = self->had_world ? rebuild_view(self)->world->_base_tile_z : 0;
        /* on_done is NULL: we await the load and run the tail ourselves
         * below, so the entity shift can land between the scene swap and
         * App_WorldLoadFinish (which the shift must precede). */
        PT_TASK_AWAITSELF_IF(CreateTask_WorldLoad(
            app->provider, rebuild_view(self)->builder, self->chunks, self->chunk_count,
            self->zone_x, self->zone_z, self->packet._map_rebuild.zones, NULL, NULL));
        /* The load's final step swapped the scene synchronously (same
         * task drain — no frame renders in between): relocate the kept
         * entities before any later packet or frame reads them, then run
         * the post-load wiring (height fn, minimap bake, MAP_BUILD_COMPLETE). */
        if( self->had_world && rebuild_view(self)->world->load_complete )
            App_WorldRebuildShift(
                app,
                rebuild_view(self)->world->_base_tile_x - self->prev_base_x,
                rebuild_view(self)->world->_base_tile_z - self->prev_base_z);
        App_WorldLoadFinish(app);
    }
    else if( self->packet.packet_type == PKT_NAME_OBJ_ADD ||
             self->packet.packet_type == PKT_NAME_OBJ_REVEAL )
    {
        /* Ground item model must be cached before the exec spawns it. */
        self->pending_obj_id = self->packet.packet_type == PKT_NAME_OBJ_ADD
                                   ? self->packet._obj_add.obj_id
                                   : self->packet._obj_reveal.obj_id;
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->pending_obj_id));
        PT_TASK_AWAITSELF_IF(obj_ground_model_task(self));
        {
            struct RS_GameProtoCtx ctx = {
                .tree = app->tree,
                .invs = &app->invs,
                .varps = &app->varps,
                .stats = &app->stats,
                .chat = &app->chat,
                .app = app,
            };
            RS_GameProto_Exec(&ctx, &self->packet);
            app->need_redraw = 1;
        }
    }
    else if( self->packet.packet_type == PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED )
    {
        for( self->zone_i = 0; self->zone_i < self->packet._update_zone_enclosed.count;
             self->zone_i++ )
        {
            self->pending_obj_id =
                zone_entry_obj_id(&self->packet._update_zone_enclosed.entries[self->zone_i]);
            if( self->pending_obj_id < 0 )
                continue;
            PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->pending_obj_id));
            PT_TASK_AWAITSELF_IF(obj_ground_model_task(self));
        }
        {
            struct RS_GameProtoCtx ctx = {
                .tree = app->tree,
                .invs = &app->invs,
                .varps = &app->varps,
                .stats = &app->stats,
                .chat = &app->chat,
                .app = app,
            };
            RS_GameProto_Exec(&ctx, &self->packet);
            app->need_redraw = 1;
        }
    }
    else if( self->packet.packet_type == PKT_NAME_PLAYER_INFO && app->world_active )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ExecPlayerInfo(
            app, self->packet._player_info.data, self->packet._player_info.length));
    }
    else if( self->packet.packet_type == PKT_NAME_NPC_INFO && app->world_active )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ExecNpcInfo(
            app, self->packet._npc_info.data, self->packet._npc_info.length));
    }
    else
    {
        struct RS_GameProtoCtx ctx = {
            .tree = app->tree,
            .invs = &app->invs,
            .varps = &app->varps,
            .stats = &app->stats,
            .chat = &app->chat,
            .app = app,
        };
        RS_GameProto_Exec(&ctx, &self->packet);
        app->need_redraw = 1;
    }

    PT_END(&self->pt);
}

static void
Task_GameProtoExec_Free(struct ToriRS_Task* base)
{
    struct Task_GameProtoExec* self = (struct Task_GameProtoExec*)base;
    gameproto_free(&self->packet);
    free(self);
}

static struct ToriRS_TaskVTable Task_GameProtoExec_VTable = {
    .run = Task_GameProtoExec_Run,
    .free = Task_GameProtoExec_Free,
};

struct ToriRS_Task*
CreateTask_GameProtoExec(
    struct App* app,
    struct RevPacket* packet)
{
    struct Task_GameProtoExec* task;

    assert(app && packet);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_GameProtoExec_VTable;
    strncpy(task->task.name, "GameProtoExec", sizeof(task->task.name) - 1);
    task->app = app;
    task->packet = *packet; /* ownership of heap fields moves here */
    PT_INIT(&task->pt);
    return &task->task;
}
