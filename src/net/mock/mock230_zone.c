/*
 * The ZoneMap: per-zone entity lists, a buffered event log, and the replay.
 *
 * Read mock230_zone.h first — it carries the design and the reference it was
 * ported from. This file is the mechanism.
 *
 * Three things happen here, in tick order:
 *
 *   phase 8   membership is reconciled (which npcs and objs are in which zone)
 *             and this tick's events are already sitting in each zone's buffer,
 *             put there by whoever mutated the world.
 *   phase 10  every client is walked over its 7x7 window of active zones. A
 *             zone it does not hold yet gets FULL_FOLLOWS and the zone's whole
 *             state; a zone it holds gets the tick's events.
 *   phase 11  the event buffers and the shared byte blobs are dropped. The
 *             state — loc records, obj and npc membership — is not.
 *
 * The shared blob is built on first use rather than in phase 8, which is the
 * one deliberate deviation from the reference's `computeShared()`. It is the
 * same bytes; building it lazily just means an event queued between the zone
 * phase and the client-out phase cannot be silently dropped on the floor.
 */

#include "mock230_zone.h"

#include "mock230.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Structures                                                          */
/* ------------------------------------------------------------------ */

struct Mock230Zone
{
    int index;
    /** Zone units, not tiles. */
    int x, z, level;

    /** Locs that are not what the map square says. The durable half. */
    struct Mock230ZoneLoc* locs;
    int loc_count, loc_capacity;

    /** `Mock230Server.ground` slots standing in this zone. */
    int* objs;
    int obj_count, obj_capacity;

    /** `Mock230Server.npcs` slots standing in this zone. */
    int* npcs;
    int npc_count, npc_capacity;

    /** This tick's events, cleared in phase 11. */
    struct Mock230ZoneEvent* events;
    int event_count, event_capacity;
    /** Already on the map's dirty list this tick. */
    int listed;

    /** The encoded ENCLOSED events, shared by every client in the zone. Built
     *  on first use; `shared_tick` is the tick it was built for. */
    uint8_t* shared;
    int shared_len, shared_capacity;
    int shared_tick;
};

/*
 * Open addressing, linear probing, power-of-two capacity.
 *
 * Zones are never deleted — a zone that has been touched once is cheap to keep
 * and the alternative is tombstones — so the probe never has to step over a
 * hole, which is the only reason this is eight lines rather than forty.
 */
struct Mock230ZoneMap
{
    struct Mock230Zone** slots;
    int capacity;
    int count;

    /** Zones with events this tick. */
    struct Mock230Zone** dirty;
    int dirty_count, dirty_capacity;
};

/* ------------------------------------------------------------------ */
/* Growth                                                              */
/* ------------------------------------------------------------------ */

static void*
grow(
    void* base,
    int* capacity,
    int needed,
    size_t item)
{
    int want = *capacity ? *capacity : 8;
    void* grown;

    if( needed <= *capacity )
        return base;
    while( want < needed )
        want *= 2;
    grown = realloc(base, (size_t)want * item);
    if( !grown )
        return base; /* the caller re-checks capacity and gives up quietly */
    *capacity = want;
    return grown;
}

/* ------------------------------------------------------------------ */
/* The map                                                             */
/* ------------------------------------------------------------------ */

static int
probe(
    struct Mock230Zone* const* slots,
    int capacity,
    int index)
{
    /* The key is already a packed grid coordinate, so its low bits vary with
     * every step in x and its high bits with level — multiplying by a large odd
     * constant is what keeps a column of zones from landing in one run. */
    unsigned int hash = (unsigned int)index * 2654435761u;
    int i = (int)(hash & (unsigned int)(capacity - 1));

    while( slots[i] && slots[i]->index != index )
        i = (i + 1) & (capacity - 1);
    return i;
}

static int
map_rehash(
    struct Mock230ZoneMap* map,
    int capacity)
{
    struct Mock230Zone** slots = calloc((size_t)capacity, sizeof(*slots));

    if( !slots )
        return 0;
    for( int i = 0; i < map->capacity; i++ )
    {
        if( !map->slots[i] )
            continue;
        slots[probe(slots, capacity, map->slots[i]->index)] = map->slots[i];
    }
    free(map->slots);
    map->slots = slots;
    map->capacity = capacity;
    return 1;
}

static struct Mock230ZoneMap*
map_of(
    struct Mock230Server* srv,
    int create)
{
    if( !srv->zone_map && create )
    {
        srv->zone_map = calloc(1, sizeof(*srv->zone_map));
        if( srv->zone_map && !map_rehash(srv->zone_map, 256) )
        {
            free(srv->zone_map);
            srv->zone_map = NULL;
        }
    }
    return srv->zone_map;
}

/** The zone containing this tile, created if nothing has happened there yet. */
static struct Mock230Zone*
zone_at(
    struct Mock230Server* srv,
    int x,
    int z,
    int level)
{
    struct Mock230ZoneMap* map = map_of(srv, 1);
    struct Mock230Zone* zone;
    int index;
    int i;

    if( !map )
        return NULL;
    index = mock230_zone_index(x, z, level);
    i = probe(map->slots, map->capacity, index);
    if( map->slots[i] )
        return map->slots[i];

    zone = calloc(1, sizeof(*zone));
    if( !zone )
        return NULL;
    zone->index = index;
    zone->x = x >> 3;
    zone->z = z >> 3;
    zone->level = level & 3;
    zone->shared_tick = -1;

    map->slots[i] = zone;
    map->count++;
    /* Half load factor: a linear probe degrades sharply past that, and the
     * whole table is a few hundred pointers. */
    if( map->count * 2 >= map->capacity )
        map_rehash(map, map->capacity * 2);
    return zone;
}

/** The zone containing this tile, or NULL when nothing has ever happened
 *  there — which is the common case and must not allocate. */
static struct Mock230Zone*
zone_find(
    struct Mock230Server* srv,
    int x,
    int z,
    int level)
{
    struct Mock230ZoneMap* map = map_of(srv, 0);

    if( !map )
        return NULL;
    return map->slots[probe(map->slots, map->capacity, mock230_zone_index(x, z, level))];
}

static struct Mock230Zone*
zone_by_index(
    struct Mock230Server* srv,
    int index)
{
    struct Mock230ZoneMap* map = map_of(srv, 0);

    if( !map )
        return NULL;
    return map->slots[probe(map->slots, map->capacity, index)];
}

void
mock230_zone_free(struct Mock230Server* srv)
{
    struct Mock230ZoneMap* map = srv->zone_map;

    if( !map )
        return;
    for( int i = 0; i < map->capacity; i++ )
    {
        struct Mock230Zone* zone = map->slots[i];

        if( !zone )
            continue;
        free(zone->locs);
        free(zone->objs);
        free(zone->npcs);
        free(zone->events);
        free(zone->shared);
        free(zone);
    }
    free(map->slots);
    free(map->dirty);
    free(map);
    srv->zone_map = NULL;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static int
zone_pos(
    int x,
    int z)
{
    return ((x & 7) << 4) | (z & 7);
}

static void
queue_event(
    struct Mock230Server* srv,
    struct Mock230Zone* zone,
    const struct Mock230ZoneEvent* event)
{
    struct Mock230ZoneMap* map = map_of(srv, 1);

    if( !zone || !map )
        return;

    zone->events = grow(zone->events, &zone->event_capacity, zone->event_count + 1,
                        sizeof(*zone->events));
    if( zone->event_count >= zone->event_capacity )
        return;
    zone->events[zone->event_count++] = *event;
    if( srv->verbose &&
        (event->kind == MOCK230_ZONE_EV_LOC_ADD_CHANGE ||
         event->kind == MOCK230_ZONE_EV_LOC_DEL ||
         event->kind == MOCK230_ZONE_EV_LOC_ANIM) )
    {
        const char* kind = event->kind == MOCK230_ZONE_EV_LOC_ADD_CHANGE
                               ? "LOC_ADD_CHANGE"
                           : event->kind == MOCK230_ZONE_EV_LOC_DEL ? "LOC_DEL"
                                                                    : "LOC_ANIM";
        int x = (zone->x << 3) + ((event->pos >> 4) & 7);
        int z = (zone->z << 3) + (event->pos & 7);

        fprintf(stderr,
                "mock230: zone event tick=%d %s x=%d z=%d level=%d id=%d "
                "shape=%d angle=%d\n",
                srv->tick, kind, x, z, zone->level, event->id, event->shape,
                event->angle);
    }
    /* Any new event invalidates a blob already built this tick. */
    zone->shared_tick = -1;

    if( zone->listed )
        return;
    map->dirty = grow(map->dirty, &map->dirty_capacity, map->dirty_count + 1,
                      sizeof(*map->dirty));
    if( map->dirty_count >= map->dirty_capacity )
        return;
    map->dirty[map->dirty_count++] = zone;
    zone->listed = 1;
}

void
mock230_zone_reset(struct Mock230Server* srv)
{
    struct Mock230ZoneMap* map = map_of(srv, 0);

    if( !map )
        return;
    for( int i = 0; i < map->dirty_count; i++ )
    {
        map->dirty[i]->event_count = 0;
        map->dirty[i]->shared_len = 0;
        map->dirty[i]->shared_tick = -1;
        map->dirty[i]->listed = 0;
    }
    map->dirty_count = 0;
}

void
mock230_zone_map_stats(
    struct Mock230Server const* srv,
    int* out_count,
    int* out_capacity)
{
    struct Mock230ZoneMap* map;

    assert(out_count);
    assert(out_capacity);
    *out_count = 0;
    *out_capacity = 0;
    if( !srv || !srv->zone_map )
        return;
    map = srv->zone_map;
    *out_count = map->count;
    *out_capacity = map->capacity;
}

/* ------------------------------------------------------------------ */
/* Loc records                                                         */
/* ------------------------------------------------------------------ */

static struct Mock230ZoneLoc*
loc_in(
    struct Mock230Zone* zone,
    int x,
    int z,
    int shape)
{
    for( int i = 0; i < zone->loc_count; i++ )
    {
        struct Mock230ZoneLoc* loc = &zone->locs[i];

        if( loc->x == x && loc->z == z && loc->shape == shape )
            return loc;
    }
    return NULL;
}

struct Mock230ZoneLoc*
mock230_zone_loc_find(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape)
{
    struct Mock230Zone* zone = zone_find(srv, x, z, level);

    return zone ? loc_in(zone, x, z, shape) : NULL;
}

struct Mock230ZoneLoc*
mock230_zone_loc_find_id(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int loc_id)
{
    struct Mock230Zone* zone = zone_find(srv, x, z, level);

    if( !zone )
        return NULL;
    for( int i = 0; i < zone->loc_count; i++ )
    {
        struct Mock230ZoneLoc* loc = &zone->locs[i];

        /* `loc_id < 0` records a deletion — the tile *had* this loc and no
         * longer does, which is exactly what a find must not answer with. */
        if( loc->x == x && loc->z == z && loc->loc_id == loc_id && loc->loc_id >= 0 )
            return loc;
    }
    return NULL;
}

void
mock230_zone_loc_changed(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    int base_loc_id,
    int base_angle,
    int over_base)
{
    struct Mock230Zone* zone = zone_at(srv, x, z, level);
    struct Mock230ZoneLoc* loc;
    struct Mock230ZoneEvent event;

    if( !zone )
        return;

    loc = loc_in(zone, x, z, shape);
    if( !loc )
    {
        zone->locs = grow(zone->locs, &zone->loc_capacity, zone->loc_count + 1,
                          sizeof(*zone->locs));
        if( zone->loc_count >= zone->loc_capacity )
            return;
        loc = &zone->locs[zone->loc_count++];
        memset(loc, 0, sizeof(*loc));
        loc->x = x;
        loc->z = z;
        loc->level = level;
        loc->shape = shape;
        /* The base is only ever read on creation: by the time a second change
         * lands, the scene is already holding the *first* change and asking it
         * what the map square says would answer with our own edit. */
        loc->base_loc_id = base_loc_id;
        loc->base_angle = base_angle;
    }
    loc->loc_id = loc_id;
    loc->angle = angle;
    loc->over_base = over_base;

    memset(&event, 0, sizeof(event));
    event.receiver_pid = -1;
    event.pos = zone_pos(x, z);
    event.shape = shape;
    event.angle = angle;
    if( loc_id < 0 )
    {
        event.kind = MOCK230_ZONE_EV_LOC_DEL;
    }
    else
    {
        event.kind = MOCK230_ZONE_EV_LOC_ADD_CHANGE;
        event.id = loc_id;
    }
    queue_event(srv, zone, &event);

    /*
     * Back to what the cache says: retire the record.
     *
     * The event above still goes out — the clients were told about the change
     * and have to be told it is over — but there is nothing left to replay, and
     * keeping the record would make a tree that is felled and regrows every
     * thirty seconds accumulate one entry per cycle for the life of the world.
     */
    if( loc->loc_id == loc->base_loc_id && loc->angle == loc->base_angle )
    {
        int i = (int)(loc - zone->locs);

        zone->locs[i] = zone->locs[--zone->loc_count];
    }
}

void
mock230_zone_loc_anim(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int seq_id)
{
    struct Mock230Zone* zone;
    struct Mock230ZoneEvent event;

    assert(srv);
    zone = zone_at(srv, x, z, level);
    if( !zone )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_LOC_ANIM;
    event.receiver_pid = -1;
    event.pos = zone_pos(x, z);
    event.shape = shape;
    event.angle = angle;
    event.id = seq_id;
    queue_event(srv, zone, &event);
}

void
mock230_zone_projanim(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int dst_x,
    int dst_z,
    int target,
    int spotanim,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc)
{
    struct Mock230Zone* zone;
    struct Mock230ZoneEvent event;

    assert(srv);
    zone = zone_at(srv, x, z, level);
    if( !zone )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_PROJANIM;
    event.receiver_pid = -1;
    event.pos = zone_pos(x, z);
    event.id = spotanim;
    /*
     * The destination travels as an offset from the source because that is what
     * the wire carries — one signed byte each. A shot from further than 127
     * tiles away cannot be described, and nothing can produce one: the source is
     * the caster and the target is something it can see.
     */
    event.dx_offset = dst_x - x;
    event.dz_offset = dst_z - z;
    event.dst_x = dst_x;
    event.dst_z = dst_z;
    event.dst_level = level;
    event.target = target;
    event.src_height = src_height;
    event.dst_height = dst_height;
    event.start_delay = start_delay;
    event.end_delay = end_delay;
    event.peak = peak;
    event.arc = arc;
    queue_event(srv, zone, &event);
}

void
mock230_zone_mapanim(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int spotanim,
    int height,
    int delay)
{
    struct Mock230Zone* zone;
    struct Mock230ZoneEvent event;

    assert(srv);
    zone = zone_at(srv, x, z, level);
    if( !zone )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_MAPANIM;
    event.receiver_pid = -1;
    event.pos = zone_pos(x, z);
    event.id = spotanim;
    event.src_height = height;
    event.start_delay = delay;
    queue_event(srv, zone, &event);
}

void
mock230_zone_loc_merge(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int start_cycle,
    int end_cycle,
    int player_pid,
    int east,
    int south,
    int west,
    int north)
{
    struct Mock230Zone* zone;
    struct Mock230ZoneEvent event;

    assert(srv);
    zone = zone_at(srv, x, z, level);
    if( !zone )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_LOC_MERGE;
    event.receiver_pid = -1;
    event.pos = zone_pos(x, z);
    event.shape = shape;
    event.angle = angle;
    event.id = loc_id;
    event.start_cycle = start_cycle;
    event.end_cycle = end_cycle;
    event.player_pid = player_pid;
    event.east = east;
    event.south = south;
    event.west = west;
    event.north = north;
    queue_event(srv, zone, &event);
}

void
mock230_zone_locs_foreach(
    struct Mock230Server* srv,
    void (*fn)(struct Mock230ZoneLoc* loc, void* user),
    void* user)
{
    struct Mock230ZoneMap* map = map_of(srv, 0);

    if( !map )
        return;
    for( int i = 0; i < map->capacity; i++ )
    {
        struct Mock230Zone* zone = map->slots[i];

        if( !zone )
            continue;
        for( int j = 0; j < zone->loc_count; j++ )
            fn(&zone->locs[j], user);
    }
}

void
mock230_zone_locs_clear_rect(
    struct Mock230Server* srv,
    int x,
    int z,
    int width,
    int height)
{
    struct Mock230ZoneMap* map = map_of(srv, 0);
    int min_zone_x;
    int min_zone_z;
    int max_zone_x;
    int max_zone_z;

    if( !map || width <= 0 || height <= 0 )
        return;
    min_zone_x = x >> 3;
    min_zone_z = z >> 3;
    max_zone_x = (x + width - 1) >> 3;
    max_zone_z = (z + height - 1) >> 3;

    for( int i = 0; i < map->capacity; i++ )
    {
        struct Mock230Zone* zone = map->slots[i];

        if( !zone || zone->x < min_zone_x || zone->x > max_zone_x ||
            zone->z < min_zone_z || zone->z > max_zone_z )
            continue;
        zone->loc_count = 0;
        /* A release can happen in phase 5, before phase 10 has encoded the
         * events. Do not let a fresh tenant receive its predecessor's final
         * LOC_DEL/LOC_ANIM burst. Other event kinds are left alone because
         * their ground-object/entity storage has a different owner. */
        for( int j = 0; j < zone->event_count; )
        {
            int kind = zone->events[j].kind;

            if( kind == MOCK230_ZONE_EV_LOC_ADD_CHANGE || kind == MOCK230_ZONE_EV_LOC_DEL ||
                kind == MOCK230_ZONE_EV_LOC_ANIM || kind == MOCK230_ZONE_EV_LOC_MERGE )
            {
                memmove(&zone->events[j], &zone->events[j + 1],
                        (size_t)(zone->event_count - j - 1) * sizeof(*zone->events));
                zone->event_count--;
                continue;
            }
            j++;
        }
        zone->shared_len = 0;
        zone->shared_tick = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Membership                                                          */
/* ------------------------------------------------------------------ */

static void
list_add(
    int** list,
    int* count,
    int* capacity,
    int value)
{
    *list = grow(*list, capacity, *count + 1, sizeof(**list));
    if( *count >= *capacity )
        return;
    (*list)[(*count)++] = value;
}

static void
list_del(
    int* list,
    int* count,
    int value)
{
    for( int i = 0; i < *count; i++ )
    {
        if( list[i] != value )
            continue;
        list[i] = list[--(*count)];
        return;
    }
}

/*
 * File `slot` under the zone containing (x, z, level), taking it out of
 * whichever zone `*filed` says it was in.
 *
 * `*filed` is the entity's own memory of where it is, held as **the packed zone
 * index plus one** so that 0 means "nowhere". That is not a style choice: an
 * npc slot and a ground slot are both born from a `memset`, and zone (0, 0, 0)
 * is a perfectly real index — with a plain -1 sentinel every entity in the
 * world would start out believing it stood in the south-west corner of the map,
 * and the first sync would try to unfile it from there.
 *
 * `x`/`z`/`level` are ignored when `present` is 0, which is how a despawn is
 * spelled: leave the old zone and file nowhere.
 */
static void
refile(
    struct Mock230Server* srv,
    int* filed,
    int slot,
    int present,
    int x,
    int z,
    int level,
    int is_npc)
{
    int want = present ? mock230_zone_index(x, z, level) + 1 : 0;
    struct Mock230Zone* zone;

    if( *filed == want )
        return;
    if( *filed > 0 )
    {
        zone = zone_by_index(srv, *filed - 1);
        if( zone )
        {
            if( is_npc )
                list_del(zone->npcs, &zone->npc_count, slot);
            else
                list_del(zone->objs, &zone->obj_count, slot);
        }
    }
    *filed = want;
    if( want == 0 )
        return;
    zone = zone_at(srv, x, z, level);
    if( !zone )
    {
        *filed = 0;
        return;
    }
    if( is_npc )
        list_add(&zone->npcs, &zone->npc_count, &zone->npc_capacity, slot);
    else
        list_add(&zone->objs, &zone->obj_count, &zone->obj_capacity, slot);
}

void
mock230_zone_npc_refile(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];

    refile(srv, &npc->zone_index, slot, npc->active, npc->x, npc->z, npc->level, 1);
}

void
mock230_zone_obj_refile(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];

    refile(srv, &obj->zone_index, slot, obj->active, obj->x, obj->z, obj->level, 0);
}

void
mock230_zone_sync_npcs(struct Mock230Server* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
        mock230_zone_npc_refile(srv, slot);
}

void
mock230_zone_sync_objs(struct Mock230Server* srv)
{
    for( int slot = 0; slot < MOCK230_GROUND_MAX; slot++ )
        mock230_zone_obj_refile(srv, slot);
}

int
mock230_zone_npcs_near(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int radius,
    int* out,
    int max)
{
    struct Mock230ZoneMap* map = map_of(srv, 0);
    int min_x = (x - radius) >> 3;
    int min_z = (z - radius) >> 3;
    int count = 0;

    if( !map )
        return 0;
    if( min_x < 0 )
        min_x = 0;
    if( min_z < 0 )
        min_z = 0;
    for( int zx = min_x; zx <= (x + radius) >> 3; zx++ )
    {
        for( int zz = min_z; zz <= (z + radius) >> 3; zz++ )
        {
            struct Mock230Zone* zone = zone_by_index(
                srv, mock230_zone_index(zx << 3, zz << 3, level));

            if( !zone )
                continue;
            for( int i = 0; i < zone->npc_count && count < max; i++ )
                out[count++] = zone->npcs[i];
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Obj events                                                          */
/* ------------------------------------------------------------------ */

static void
obj_event(
    struct Mock230Server* srv,
    int slot,
    int kind,
    int old_count,
    int new_count)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];
    struct Mock230Zone* zone = zone_at(srv, obj->x, obj->z, obj->level);
    struct Mock230ZoneEvent event;

    if( !zone )
        return;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    /* No obj on this floor belongs to anyone in particular yet; the reference's
     * per-receiver loot is what this field is here for. */
    event.receiver_pid = -1;
    event.pos = zone_pos(obj->x, obj->z);
    event.id = obj->obj_id;
    event.count = new_count;
    event.old_count = old_count;
    /* Only a *drop* expires; a map-square spawn stays until it is taken, which
     * is what `despawn_tick < 0` means. Clamped at zero because a tick that has
     * already passed is not a negative lifetime. */
    if( obj->despawn_tick >= 0 )
        event.despawn_ticks = obj->despawn_tick > srv->tick ? obj->despawn_tick - srv->tick : 0;
    queue_event(srv, zone, &event);
}

void
mock230_zone_obj_added(
    struct Mock230Server* srv,
    int slot)
{
    obj_event(srv, slot, MOCK230_ZONE_EV_OBJ_ADD, 0, srv->ground[slot].count);
}

void
mock230_zone_obj_removed(
    struct Mock230Server* srv,
    int slot)
{
    obj_event(srv, slot, MOCK230_ZONE_EV_OBJ_DEL, 0, 0);
}

void
mock230_zone_obj_counted(
    struct Mock230Server* srv,
    int slot,
    int old_count,
    int new_count)
{
    obj_event(srv, slot, MOCK230_ZONE_EV_OBJ_COUNT, old_count, new_count);
}

/* ------------------------------------------------------------------ */
/* The per-player flush                                                */
/* ------------------------------------------------------------------ */

/*
 * The 7x7x4 window of zones this client is kept current on, clipped to the
 * build area — `BuildArea.rebuildZones` in the reference.
 *
 * Recomputed only when the player changes zone, which is what the reference
 * does and what keeps a stationary player's client-out phase down to a set
 * membership test per zone.
 *
 * ALL FOUR PLANES, not just the player's. The window used to be built with
 * `player->level`, which meant a loc change one storey up was not merely
 * mis-addressed — the zone holding it was not in the set at all, so nothing
 * ever considered flushing it. The client's scene holds every plane of the
 * loaded region (that is how an upstairs is drawn), so the server has to be
 * willing to talk about every plane of it.
 *
 * What that cost: content could only mutate a loc on a plane by standing the
 * player on it. The Inferno's prison walls did exactly that — teleport to
 * plane 1, spawn, teleport back — and the player saw themselves jump.
 *
 * The set is four times larger and the per-tick work is not: the flush skips a
 * zone with no events in one test, and empty planes are the common case.
 */
static void
rebuild_active(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    int centre_x = player->x >> 3;
    int centre_z = player->z >> 3;
    int left = srv->zone_x - MOCK230_ZONE_BUILD_RADIUS;
    int right = srv->zone_x + MOCK230_ZONE_BUILD_RADIUS;
    int bottom = srv->zone_z - MOCK230_ZONE_BUILD_RADIUS;
    int top = srv->zone_z + MOCK230_ZONE_BUILD_RADIUS;

    player->active_zone_count = 0;
    for( int x = centre_x - MOCK230_ZONE_VIEW_RADIUS; x <= centre_x + MOCK230_ZONE_VIEW_RADIUS;
         x++ )
    {
        for( int z = centre_z - MOCK230_ZONE_VIEW_RADIUS;
             z <= centre_z + MOCK230_ZONE_VIEW_RADIUS; z++ )
        {
            if( x < left || x > right || z < bottom || z > top )
                continue;
            for( int level = 0; level < MOCK230_ZONE_LEVELS; level++ )
            {
                if( player->active_zone_count >= MOCK230_ZONE_ACTIVE_MAX )
                    continue;
                player->active_zones[player->active_zone_count++] =
                    mock230_zone_index(x << 3, z << 3, level);
            }
        }
    }
}

static int
holds(
    const int* set,
    int count,
    int index)
{
    for( int i = 0; i < count; i++ )
        if( set[i] == index )
            return 1;
    return 0;
}

/**
 * The zone's whole current state, for a client that has just been handed a
 * FULL_FOLLOWS.
 *
 * This is the *whole* state, including anything that changed this tick: the
 * loc records are written at the moment of the mutation and obj membership is
 * reconciled in phase 8, so by the time this runs the state already accounts
 * for every event still sitting in the buffer. Which is exactly why the caller
 * skips that buffer for a zone it has just sent state for — see there.
 */
static void
write_state(
    struct Mock230Player* player,
    struct Mock230Zone* zone)
{
    struct Mock230Server* srv = player->world;

    for( int i = 0; i < zone->obj_count; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[zone->objs[i]];
        struct Mock230ZoneEvent event;

        if( !obj->active )
            continue;
        memset(&event, 0, sizeof(event));
        event.kind = MOCK230_ZONE_EV_OBJ_ADD;
        event.receiver_pid = -1;
        event.pos = zone_pos(obj->x, obj->z);
        event.id = obj->obj_id;
        event.count = obj->count;
        mock230_send_zone_sub(player, &event);
    }
    for( int i = 0; i < zone->loc_count; i++ )
    {
        struct Mock230ZoneLoc* loc = &zone->locs[i];
        struct Mock230ZoneEvent event;

        memset(&event, 0, sizeof(event));
        event.receiver_pid = -1;
        event.pos = zone_pos(loc->x, loc->z);
        event.shape = loc->shape;
        event.angle = loc->angle;
        if( loc->loc_id < 0 )
        {
            event.kind = MOCK230_ZONE_EV_LOC_DEL;
        }
        else
        {
            event.kind = MOCK230_ZONE_EV_LOC_ADD_CHANGE;
            event.id = loc->loc_id;
        }
        mock230_send_zone_sub(player, &event);
    }
}

/** Encode the zone's everyone-events once, for however many clients are in it. */
static void
build_shared(
    struct Mock230Server* srv,
    struct Mock230Zone* zone)
{
    if( zone->shared_tick == srv->tick )
        return;
    zone->shared_len = 0;
    zone->shared_tick = srv->tick;
    for( int i = 0; i < zone->event_count; i++ )
    {
        int written;

        if( zone->events[i].receiver_pid >= 0 )
            continue;
        /* Revision 239's MAP_PROJANIM_V2 is 24 payload bytes plus its enclosed
         * ordinal. The classic record fitted in 16, which made a fresh zone's
         * first projectile overflow and disappear; a reused buffer could mask
         * the defect after some unrelated events had already grown it. */
        zone->shared = grow(zone->shared, &zone->shared_capacity, zone->shared_len + 32,
                            sizeof(*zone->shared));
        if( zone->shared_len + 32 > zone->shared_capacity )
            return;
        written = mock230_encode_zone_sub(srv->wire, zone->shared + zone->shared_len,
                                          zone->shared_capacity - zone->shared_len,
                                          &zone->events[i]);
        zone->shared_len += written;
    }
}

void
mock230_zone_update_player(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    int here = mock230_zone_index(player->x, player->z, player->level);
    int kept[MOCK230_ZONE_ACTIVE_MAX];
    int kept_count = 0;

    if( player->zone_index != here || player->active_zone_count == 0 )
    {
        rebuild_active(player);
        player->zone_index = here;
    }

    /* Drop what fell out of the window. The client keeps drawing those zones —
     * they are still inside its 104x104 scene — it simply stops being told
     * about them, and is caught up in full if it comes back. */
    for( int i = 0; i < player->loaded_zone_count; i++ )
        if( holds(player->active_zones, player->active_zone_count, player->loaded_zones[i]) )
            kept[kept_count++] = player->loaded_zones[i];
    memcpy(player->loaded_zones, kept, sizeof(int) * (size_t)kept_count);
    player->loaded_zone_count = kept_count;

    for( int i = 0; i < player->active_zone_count; i++ )
    {
        int index = player->active_zones[i];
        struct Mock230Zone* zone = zone_by_index(srv, index);
        int zone_x = index & 0x7ff;
        int zone_z = (index >> 11) & 0x7ff;
        /* Bits 22-23 of the key (mock230_zone_index). Recovering it is what
         * lets a zone describe its own plane instead of borrowing the
         * player's. */
        int zone_level = (index >> 22) & 3;
        int loaded = holds(player->loaded_zones, player->loaded_zone_count, index);

        if( !loaded )
        {
            /*
             * FULL_FOLLOWS resets the client's memory of the zone, so the state
             * that follows it is exact rather than added on top of whatever was
             * there. Sent even for a zone with nothing in it: the client may be
             * holding objs there from before a rebuild, and "nothing to say" is
             * not the same as "nothing to undo".
             *
             * Then *skip this tick's events for this client*. The state written
             * above is already current — it includes whatever those events
             * describe — so sending both is how a ground obj arrives twice and
             * the client draws two stacks on one tile. It did, and the first
             * attempt at guarding it compared each entity's change tick against
             * the current one, which is subtly not the same test: events queued
             * before any tick ran (the world's own spawns, placed at tick 0 with
             * no player to flush them to) are still in the buffer on tick 1, and
             * their stamps no longer match.
             *
             * The one thing this gives up is a *receiver-scoped* event landing
             * on the same tick a client loads the zone. Nothing produces one
             * yet; when something does — the reference's per-killer loot — the
             * state written above has to learn about receivers too, and this is
             * the line that changes with it.
             */
            /*
             * The unconditional FULL is for the plane the player is ON, and
             * only that one.
             *
             * FULL_FOLLOWS resets the client's memory of a zone, and sending it
             * for an empty zone is deliberate *there*: the client may be
             * holding objs from before a rebuild, and "nothing to say" is not
             * "nothing to undo". That reasoning is about the plane the player
             * just arrived on. It does not extend to the other three, and
             * extending it was a mistake with teeth — the window is 7x7x4, so
             * a client entering an instance got 196 of these instead of 49,
             * 147 of them resetting planes it had nothing on. The arena
             * rendered blank.
             *
             * A zone that exists on another plane still gets its FULL and its
             * state; one that does not is simply marked loaded, and any later
             * event in it flushes through the normal path below.
             */
            if( zone || zone_level == player->level )
            {
                mock230_send_zone_header(player, zone_x, zone_z, zone_level, 1);
                if( zone )
                    write_state(player, zone);
            }
            if( player->loaded_zone_count < MOCK230_ZONE_ACTIVE_MAX )
                player->loaded_zones[player->loaded_zone_count++] = index;
            continue;
        }

        if( !zone || zone->event_count == 0 )
            continue;

        build_shared(srv, zone);
        if( zone->shared_len > 0 )
            mock230_send_zone_enclosed(player, zone_x, zone_z, zone_level, zone->shared,
                                       zone->shared_len);

        /*
         * Whatever is addressed to one client goes out on its own -- as a
         * top-level packet where the revision has one, and otherwise as a
         * PARTIAL_ENCLOSED carrying a single event.
         *
         * Revision 239 has no top-level prot for the obj family or for
         * MAP_PROJANIM_V2 at all; they exist only inside the enclosed blob.
         * Sending them the 230 way resolves to opcode -1 and drops them, and
         * the events this loop carries are precisely the ones scoped to one
         * player -- loot only its killer may see. Nothing logs it and the
         * shared events in the same zone keep arriving, so the symptom is one
         * player's ground items missing rather than anything looking broken.
         *
         * A one-event blob is not a workaround: RSProt's own encoder says
         * player-specific zone prots "cannot be grouped together and must be
         * sent separately, as they also are in OldSchool RuneScape".
         */
        for( int e = 0; e < zone->event_count; e++ )
        {
            if( zone->events[e].receiver_pid != player->pid )
                continue;
            if( mock230_zone_sub_standalone(srv->wire, zone->events[e].kind) )
            {
                mock230_send_zone_header(player, zone_x, zone_z, zone_level, 0);
                mock230_send_zone_sub(player, &zone->events[e]);
                continue;
            }
            {
                uint8_t one[256];
                int written = mock230_encode_zone_sub(srv->wire, one, (int)sizeof(one),
                                                     &zone->events[e]);

                if( written > 0 )
                    mock230_send_zone_enclosed(player, zone_x, zone_z, zone_level, one, written);
            }
        }
    }
}

void
mock230_zone_player_reset(struct Mock230Player* player)
{
    player->loaded_zone_count = 0;
    player->active_zone_count = 0;
    player->zone_index = -1;
}
