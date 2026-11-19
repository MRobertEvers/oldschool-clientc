/*
 * The zone families as the headless client keeps them (scriptrun_zone.h).
 */

#include "scriptrun/scriptrun_zone.h"

#include "game/rs_server_tick.h"
#include "mem/torirs_mem.h"
#include "net/rev/revpacket.h"
#include "world/world.h"

#include <assert.h>
#include <string.h>

/* The headless client's cycle moves exactly this a tick, so the log needs no
 * slack for drift (rs_effect_log.h). */
enum
{
    ZONE_EFFECT_SLACK = 0,
};

void
ScriptrunZone_Init(
    struct ScriptrunZone* zone,
    struct RS_EventSink events)
{
    assert(zone);
    memset(zone, 0, sizeof(*zone));
    zone->world = World_New();
    assert(zone->world);
    zone->scene_size = 104;
    zone->world->_scene_size = zone->scene_size;
    zone->events = events;
}

void
ScriptrunZone_Free(struct ScriptrunZone* zone)
{
    if( !zone )
        return;
    World_Free(zone->world);
    for( int v = 0; v < SCRIPTRUN_ZONE_VIEWS; v++ )
        World_Free(zone->decks[v]);
    memset(zone, 0, sizeof(*zone));
}

void
ScriptrunZone_Rebuild(
    struct ScriptrunZone* zone,
    int base_x,
    int base_z,
    int scene_size)
{
    struct World_EntityPool* pool;

    assert(zone);
    assert(scene_size > 0);
    if( zone->have_base )
        World_ShiftEntities(zone->world, base_x - zone->base_x, base_z - zone->base_z);
    zone->have_base = 1;
    zone->base_x = base_x;
    zone->base_z = base_z;
    zone->scene_size = scene_size;
    zone->world->_scene_size = scene_size;
    zone->world->_base_tile_x = base_x;
    zone->world->_base_tile_z = base_z;
    /* App_WorldRebuildShift: World_ClearProjectilesAndSpotanims, and every
     * stack the shift put off the scene goes. */
    zone->scripted_count = 0;
    pool = &zone->world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; )
    {
        int const next = World_EntityPoolNext(pool, i);
        struct WorldEntity_ObjStack const* stack = World_EntityPoolGet(pool, i);

        if( stack && (stack->grid_position.x < 0 || stack->grid_position.z < 0 ||
                         stack->grid_position.x >= scene_size || stack->grid_position.z >= scene_size) )
            World_ObjStackDel(zone->world, i);
        i = next;
    }
}

void
ScriptrunZone_DropView(
    struct ScriptrunZone* zone,
    int view)
{
    assert(zone);
    assert(view > 0);
    assert(view < SCRIPTRUN_ZONE_VIEWS);
    World_Free(zone->decks[view]);
    zone->decks[view] = NULL;
}

void
ScriptrunZone_BeginTick(
    struct ScriptrunZone* zone,
    int tick,
    int now_cycle)
{
    int kept = 0;

    assert(zone);
    for( int i = 0; i < zone->loc_anim_count; i++ )
        if( tick - zone->loc_anims[i].tick <= RS_EFFECT_LOC_ANIM_LIFE_TICKS )
            zone->loc_anims[kept++] = zone->loc_anims[i];
    zone->loc_anim_count = kept;
    /* A scripted projectile's row goes when its flight is over
     * (World_CycleUpdateProjectiles: cycle past its end). */
    kept = 0;
    for( int i = 0; i < zone->scripted_count; i++ )
    {
        if( now_cycle > zone->scripted_end[i] )
            continue;
        zone->scripted_slot[kept] = zone->scripted_slot[i];
        zone->scripted_end[kept] = zone->scripted_end[i];
        kept++;
    }
    zone->scripted_count = kept;
}

/* ---- ground rows ---------------------------------------------------------- */

/* The world a view's rows live in: the root's, or the deck's own. */
static struct World*
zone_view_world(
    struct ScriptrunZone* zone,
    struct ScriptrunZoneAt const* at)
{
    struct World* world;

    if( at->view == 0 )
        return zone->world;
    assert(at->view > 0);
    assert(at->view < SCRIPTRUN_ZONE_VIEWS);
    world = zone->decks[at->view];
    if( !world )
    {
        world = World_New();
        assert(world);
        world->_scene_size = at->scene_size;
        world->_base_tile_x = at->origin_x;
        world->_base_tile_z = at->origin_z;
        zone->decks[at->view] = world;
    }
    return world;
}

static void
zone_stamp(
    struct ScriptrunZone* zone,
    int kind,
    int a,
    int b,
    int c,
    int d)
{
    if( zone->events.ring )
        RS_EventSink_Stamp(&zone->events, kind, a, b, c, d);
}

/* App_WorldObjStackAdd: every add is one new row; a count of 0 names the row
 * already there and adds none. The name and menu are the objtype's, which
 * this client does not decode: the row carries none, as the real client's
 * does while its objtype is not resident. */
static void
zone_obj_add(
    struct World* world,
    int x,
    int z,
    int level,
    int obj_id,
    int count)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    int idx;

    if( count <= 0 )
        return;
    idx = World_ObjStackAdd(world, -1, x, z, level, obj_id, count, "", none);
    /* The pool grows; a full one is a contract violation, never a drop. */
    assert(idx >= 0);
    (void)idx;
}

static void
zone_ground(
    struct ScriptrunZone* zone,
    struct ScriptrunZoneAt const* at,
    int name,
    void const* payload)
{
    struct World* world = zone_view_world(zone, at);
    int x, z, idx;

    if( name == PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS )
    {
        /* zone_full_reset: the zone's rows go (App_WorldObjStackClearTile). */
        for( int dz = 0; dz < 8; dz++ )
            for( int dx = 0; dx < 8; dx++ )
                while( (idx = World_ObjStackFind(
                            world, at->base_x + dx, at->base_z + dz, at->level, -1)) >= 0 )
                    World_ObjStackDel(world, idx);
        return;
    }
    if( name == PKT_NAME_OBJ_ADD )
    {
        struct PktObjAdd const* pkt = payload;

        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        /* DRIVE_STAMP: obj_added -- a=obj_id b=count c=tile_x d=tile_z. */
        zone_stamp(zone, RS_EVENT_OBJ_ADDED, pkt->obj_id, pkt->count, x, z);
        zone_obj_add(world, x, z, at->level, pkt->obj_id, pkt->count);
        return;
    }
    if( name == PKT_NAME_OBJ_DEL )
    {
        struct PktObjDel const* pkt = payload;

        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        /* DRIVE_STAMP: obj_removed -- a=obj_id c=tile_x d=tile_z. */
        zone_stamp(zone, RS_EVENT_OBJ_REMOVED, pkt->obj_id, 0, x, z);
        /* ONE row, the first in arrival order (App_WorldObjStackDel). */
        idx = World_ObjStackFind(world, x, z, at->level, pkt->obj_id);
        if( idx >= 0 )
            World_ObjStackDel(world, idx);
        return;
    }
    if( name == PKT_NAME_OBJ_COUNT )
    {
        struct PktObjCount const* pkt = payload;

        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        /* The first row of the id (App_WorldObjStackSetCount). */
        idx = World_ObjStackFind(world, x, z, at->level, pkt->obj_id);
        if( idx >= 0 )
            World_ObjStackSetCount(world, idx, pkt->new_count);
        return;
    }
    if( name == PKT_NAME_OBJ_REVEAL )
    {
        struct PktObjReveal const* pkt = payload;

        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        /* OBJ_ENABLED_OPS shares the name and addresses every viewer: the
         * first row of the id (App_WorldObjStackSetEnabledOps). */
        if( pkt->has_enabled_ops )
        {
            idx = World_ObjStackFind(world, x, z, at->level, pkt->obj_id);
            if( idx >= 0 )
                World_ObjStackSetEnabledOps(world, idx, pkt->enabled_ops);
            return;
        }
        /* A reveal targets one receiver; everyone else already sees it. */
        if( at->local_pid >= 0 && pkt->receiver != at->local_pid )
            return;
        zone_obj_add(world, x, z, at->level, pkt->obj_id, pkt->count);
        return;
    }
}

/* ---- effects --------------------------------------------------------------- */

static int
zone_scripted_in_flight(
    struct ScriptrunZone const* zone,
    int slot)
{
    for( int i = 0; i < zone->scripted_count; i++ )
        if( zone->scripted_slot[i] == slot )
            return 1;
    return 0;
}

static void
zone_effects(
    struct ScriptrunZone* zone,
    struct ScriptrunZoneAt const* at,
    int tick,
    int now,
    int name,
    void const* payload)
{
    int x, z, level;

    if( name == PKT_NAME_MAP_ANIM )
    {
        struct PktMapAnim const* pkt = payload;

        /* 65535 is the clear sentinel. */
        if( pkt->id == 65535 )
            return;
        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        /* A MAP_ANIM is filed only on a tile its view's scene holds
         * (worldview.method3333 before new class52); the rest go, unsaid. */
        if( x < 0 || z < 0 || x >= at->scene_size || z >= at->scene_size )
            return;
        RS_EffectLog_NoteMapAnim(&zone->effects, zone->effect_entries, now, ZONE_EFFECT_SLACK,
            at->origin_x + x, at->origin_z + z, at->level, pkt->id);
        return;
    }
    if( name == PKT_NAME_MAP_PROJANIM )
    {
        struct PktMapProjAnim const* pkt = payload;
        struct RS_EffectProjectile p;

        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        memset(&p, 0, sizeof(p));
        p.src_x = at->origin_x + x;
        p.src_z = at->origin_z + z;
        if( pkt->dst_abs )
        {
            /* Rev 239 states the landing point absolute; its plane is the
             * landing's. */
            p.dst_x = pkt->dst_abs_x;
            p.dst_z = pkt->dst_abs_z;
            level = pkt->dst_abs_level;
        }
        else
        {
            p.dst_x = p.src_x + pkt->dx_offset;
            p.dst_z = p.src_z + pkt->dz_offset;
            level = at->level;
        }
        p.level = level;
        p.spotanim = pkt->spotanim;
        p.target = pkt->target;
        p.start_delay = pkt->start_delay;
        p.end_delay = pkt->end_delay;
        RS_EffectLog_NoteProjectile(&zone->effects, zone->effect_entries, now, ZONE_EFFECT_SLACK, &p);
        return;
    }
    if( name == PKT_NAME_SCRIPTEDPROJ_ADD )
    {
        struct PktScriptedProjAdd const* pkt = payload;
        struct RS_EffectProjectile p;

        /* `if (var16 == null)`: a slot in flight keeps its projectile and the
         * packet does nothing else. */
        if( zone_scripted_in_flight(zone, pkt->slot) )
            return;
        assert(zone->scripted_count < SCRIPTRUN_ZONE_SCRIPTED);
        zone->scripted_slot[zone->scripted_count] = pkt->slot;
        zone->scripted_end[zone->scripted_count] = now + pkt->end_delay;
        zone->scripted_count++;
        x = at->base_x + ((pkt->pos >> 4) & 7);
        z = at->base_z + (pkt->pos & 7);
        memset(&p, 0, sizeof(p));
        p.src_x = at->origin_x + x;
        p.src_z = at->origin_z + z;
        p.dst_x = pkt->target_x;
        p.dst_z = pkt->target_z;
        p.level = pkt->target_level;
        p.spotanim = pkt->spotanim;
        p.target = pkt->target;
        p.start_delay = pkt->start_delay;
        p.end_delay = pkt->end_delay;
        RS_EffectLog_NoteProjectile(&zone->effects, zone->effect_entries, now, ZONE_EFFECT_SLACK, &p);
        return;
    }
    if( name == PKT_NAME_LOC_ANIM )
    {
        struct PktLocAnim const* pkt = payload;
        struct ScriptrunLocAnim* a;
        int const tx = (pkt->pos >> 4) & 7;
        int const tz = pkt->pos & 7;

        /* The driver lists the root's (App_DriveLocAnimNote is called only
         * when the cursor's world is the root). */
        if( at->view != 0 )
            return;
        assert(zone->loc_anim_count < RS_EFFECT_LOC_ANIM_CAP);
        a = &zone->loc_anims[zone->loc_anim_count++];
        a->x = at->origin_x + at->base_x + tx;
        a->z = at->origin_z + at->base_z + tz;
        a->level = at->level + at->loc_climb[tx][tz];
        a->shape = pkt->info >> 2;
        a->seq = pkt->seq_id;
        a->tick = tick;
        return;
    }
}

void
ScriptrunZone_Apply(
    struct ScriptrunZone* zone,
    struct ScriptrunZoneAt const* at,
    int tick,
    int now_cycle,
    int name,
    void const* payload)
{
    assert(zone);
    assert(at);
    if( name == PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS || name == PKT_NAME_OBJ_ADD ||
        name == PKT_NAME_OBJ_DEL || name == PKT_NAME_OBJ_COUNT || name == PKT_NAME_OBJ_REVEAL )
    {
        assert(payload || name == PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS);
        zone_ground(zone, at, name, payload);
        return;
    }
    if( name == PKT_NAME_MAP_ANIM || name == PKT_NAME_MAP_PROJANIM ||
        name == PKT_NAME_SCRIPTEDPROJ_ADD || name == PKT_NAME_LOC_ANIM )
    {
        assert(payload);
        zone_effects(zone, at, tick, now_cycle, name, payload);
    }
}

/* ---- loc change records ---------------------------------------------------- */

void
ScriptrunZone_LocChange(
    struct ScriptrunZone* zone,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int old_loc,
    int old_shape,
    int old_angle,
    int now_cycle,
    int op_flags,
    const char ops[5][32])
{
    assert(zone);
    assert(ops);
    zone->loc_changes_seen++;
    World_LocChangePushOps(zone->world, level, World_LocShapeToLayer(shape), x, z, old_loc,
        old_angle, old_shape, loc_id, angle, shape, now_cycle, -1, op_flags, ops);
}

void
ScriptrunZone_LocZoneReset(
    struct ScriptrunZone* zone,
    int x0,
    int z0,
    int level,
    uint8_t const climb[8][8])
{
    struct World* world;

    assert(zone);
    assert(climb);
    world = zone->world;
    for( int i = 0; i < world->loc_change_count; i++ )
    {
        struct World_LocChange* rec = &world->loc_changes[i];
        int const dx = rec->x - x0;
        int const dz = rec->z - z0;

        if( dx < 0 || dx >= 8 || dz < 0 || dz >= 8 || rec->level != level + climb[dx][dz] )
            continue;
        /* The map's loc stands again; the key keeps this one record. */
        if( rec->old_shape < 0 )
            rec->old_shape = rec->new_shape;
        rec->new_type = rec->old_type;
        rec->new_angle = rec->old_angle;
        rec->new_shape = rec->old_shape;
        rec->start_time = -1;
        rec->end_time = -1;
    }
}

/* ---- readers --------------------------------------------------------------- */

static int
zone_world_rows(
    struct World const* world,
    int origin_x,
    int origin_z,
    int view,
    struct ScriptrunObjRow* out,
    int n,
    int cap)
{
    struct World_EntityPool const* pool = &world->entities.obj_stack;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* s = World_EntityPoolGet(pool, i);

        if( !s )
            continue;
        assert(n < cap);
        (void)cap;
        out[n].x = origin_x + s->grid_position.x;
        out[n].z = origin_z + s->grid_position.z;
        out[n].level = s->grid_position.level;
        out[n].obj_id = s->obj_id;
        out[n].count = s->count;
        out[n].view = view;
        n++;
    }
    return n;
}

int
ScriptrunZone_Objs(
    struct ScriptrunZone const* zone,
    int const* deck_origin_x,
    int const* deck_origin_z,
    struct ScriptrunObjRow* out,
    int cap)
{
    int n;

    assert(zone);
    assert(out);
    n = zone_world_rows(zone->world, zone->base_x, zone->base_z, 0, out, 0, cap);
    for( int v = 1; v < SCRIPTRUN_ZONE_VIEWS; v++ )
    {
        if( !zone->decks[v] )
            continue;
        assert(deck_origin_x);
        assert(deck_origin_z);
        n = zone_world_rows(zone->decks[v], deck_origin_x[v], deck_origin_z[v], v, out, n, cap);
    }
    return n;
}

static int
zone_world_count(struct World const* world)
{
    struct World_EntityPool const* pool = &world->entities.obj_stack;
    int n = 0;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        n++;
    return n;
}

int
ScriptrunZone_ObjCount(struct ScriptrunZone const* zone)
{
    int n;

    assert(zone);
    n = zone_world_count(zone->world);
    for( int v = 1; v < SCRIPTRUN_ZONE_VIEWS; v++ )
        if( zone->decks[v] )
            n += zone_world_count(zone->decks[v]);
    return n;
}
