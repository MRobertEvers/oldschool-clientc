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

    /** `Mock230Server.players` pids standing in this zone.
     *
     *  Players were the one kind of entity the ZoneMap did not know about, and
     *  the omission was invisible because PLAYER_INFO scanned the whole pool
     *  instead — which is affordable at MOCK230_PLAYER_MAX 8 and answers the
     *  wrong question at any size: "who is within 15 tiles" rather than "who is
     *  in a zone this client holds". See struct Mock230PlayerArea. */
    int* players;
    int player_count, player_capacity;

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

static void
list_add(
    int** list,
    int* count,
    int* capacity,
    int value);

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
        free(zone->players);
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
    enum Mock230ZoneKind kind)
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
            if( kind == MOCK230_ZONE_KIND_NPC )
                list_del(zone->npcs, &zone->npc_count, slot);
            else if( kind == MOCK230_ZONE_KIND_OBJ )
                list_del(zone->objs, &zone->obj_count, slot);
            else
                list_del(zone->players, &zone->player_count, slot);
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
    if( kind == MOCK230_ZONE_KIND_NPC )
        list_add(&zone->npcs, &zone->npc_count, &zone->npc_capacity, slot);
    else if( kind == MOCK230_ZONE_KIND_OBJ )
        list_add(&zone->objs, &zone->obj_count, &zone->obj_capacity, slot);
    else
        list_add(&zone->players, &zone->player_count, &zone->player_capacity, slot);
}

void
mock230_zone_npc_refile(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];

    refile(srv, &npc->zone_filed, slot, npc->active, npc->x, npc->z, npc->level,
           MOCK230_ZONE_KIND_NPC);
}

void
mock230_zone_obj_refile(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];

    refile(srv, &obj->zone_index, slot, obj->active, obj->x, obj->z, obj->level,
           MOCK230_ZONE_KIND_OBJ);
}

/*
 * A player's own memory of which zone it is filed under.
 *
 * `player->zone_filed`, NOT `player->zone_index`. The two look interchangeable
 * and are opposites: `zone_index` is the *window* latch, deliberately reset to
 * -1 by `mock230_zone_player_reset` on every scene rebuild so the active window
 * is recomputed. Filing off a field that is cleared behind your back would take
 * the player out of the map's membership lists on every rebuild and never put
 * them back, so nobody would be able to see anybody after walking 88 tiles.
 */
void
mock230_zone_player_refile(
    struct Mock230Server* srv,
    int pid)
{
    struct Mock230Player* other = &srv->players[pid];

    refile(srv, &other->zone_filed, pid, other->active && other->world != NULL, other->x,
           other->z, other->level, MOCK230_ZONE_KIND_PLAYER);
}

void
mock230_zone_sync_players(struct Mock230Server* srv)
{
    for( int pid = 0; pid < srv->player_count; pid++ )
        mock230_zone_player_refile(srv, pid);
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
    event.receiver_pid = obj->receiver_pid;
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
/* ------------------------------------------------------------------ */
/* The player's zone map                                               */
/* ------------------------------------------------------------------ */

/*
 * A client's zone map is a subscription, and it holds no entities.
 *
 * See `struct Mock230PlayerZoneMap` for why there are two maps at all. The
 * division of labour here:
 *
 *   the list  is maintained. It changes only when the player crosses a zone
 *             boundary, and then only at its edges — 7 columns of zones leave,
 *             7 enter, 42 are untouched — so keeping it exact is a set
 *             difference and costs nothing.
 *   the entities are not. What is standing inside those zones changes every
 *             tick, so the packet builders walk the list against the world map
 *             at the moment they need to know.
 *
 * Materialising the entities too, and pushing membership changes from the world
 * map into each subscriber, was built and reverted. It is faster in principle
 * and fails silently in practice: a push that never arrives leaves a client
 * missing an npc standing in front of it, with nothing anywhere to say so. The
 * walk is a few hundred hash probes per tick at MOCK230_PLAYER_MAX clients and
 * cannot be stale, because it asks the authority every time.
 */

/** This client's entry for `index`, or NULL if it does not hold that zone. */
struct Mock230PlayerZone*
mock230_playerzonemap_find(
    struct Mock230Player* player,
    int index)
{
    for( int i = 0; i < player->zonemap.count; i++ )
    {
        if( player->zonemap.zones[i].index == index )
            return &player->zonemap.zones[i];
    }
    return NULL;
}

/** Is `index` in the 7x7 window centred on (centre_x, centre_z), clipped to the
 *  build area? The one place the window's shape is written down. */
static int
window_holds(
    const struct Mock230Server* srv,
    int centre_x,
    int centre_z,
    int index)
{
    int zone_x = index & 0x7ff;
    int zone_z = (index >> 11) & 0x7ff;

    if( zone_x < centre_x - MOCK230_ZONE_VIEW_RADIUS ||
        zone_x > centre_x + MOCK230_ZONE_VIEW_RADIUS )
        return 0;
    if( zone_z < centre_z - MOCK230_ZONE_VIEW_RADIUS ||
        zone_z > centre_z + MOCK230_ZONE_VIEW_RADIUS )
        return 0;
    /* Clipped to the build area, which is what a tile box is not and why
     * NPC_INFO used to name npcs outside the region the client has a scene
     * for. */
    if( zone_x < srv->zone_x - MOCK230_ZONE_BUILD_RADIUS ||
        zone_x > srv->zone_x + MOCK230_ZONE_BUILD_RADIUS )
        return 0;
    if( zone_z < srv->zone_z - MOCK230_ZONE_BUILD_RADIUS ||
        zone_z > srv->zone_z + MOCK230_ZONE_BUILD_RADIUS )
        return 0;
    return 1;
}

void
mock230_playerzonemap_clear(struct Mock230Player* player)
{
    player->zonemap.count = 0;
    player->zone_index = -1;
}

/*
 * Move the window to wherever the player is now, touching only what changed.
 *
 * Phase 8. Does nothing at all when the player has not changed zone and the
 * build area has not moved under them, which is the common case — a player
 * walking within one zone costs three compares.
 *
 * A zone that leaves takes its `loaded` flag with it because the flag lives on
 * the entry. Re-entering therefore re-states the zone in full, which is what
 * the flush already does for anything it does not hold.
 */
void
mock230_playerzonemap_move(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    int here;
    int centre_x;
    int centre_z;
    int wanted[MOCK230_ZONE_ACTIVE_MAX];
    int wanted_count = 0;
    struct Mock230PlayerZone kept[MOCK230_ZONE_ACTIVE_MAX];
    int kept_count = 0;

    if( !srv )
        return;
    here = mock230_zone_index(player->x, player->z, player->level);
    if( player->zone_index == here && player->zonemap.count > 0 &&
        player->zonemap.built_zone_x == srv->zone_x &&
        player->zonemap.built_zone_z == srv->zone_z )
        return;

    centre_x = player->x >> 3;
    centre_z = player->z >> 3;
    for( int x = centre_x - MOCK230_ZONE_VIEW_RADIUS; x <= centre_x + MOCK230_ZONE_VIEW_RADIUS;
         x++ )
    {
        for( int z = centre_z - MOCK230_ZONE_VIEW_RADIUS;
             z <= centre_z + MOCK230_ZONE_VIEW_RADIUS; z++ )
        {
            for( int level = 0; level < MOCK230_ZONE_LEVELS; level++ )
            {
                int index = mock230_zone_index(x << 3, z << 3, level);

                if( !window_holds(srv, centre_x, centre_z, index) )
                    continue;
                if( wanted_count < MOCK230_ZONE_ACTIVE_MAX )
                    wanted[wanted_count++] = index;
            }
        }
    }

    /*
     * Rebuild the table in the new window's order, carrying `loaded` across for
     * the zones that survive.
     *
     * Written as a rebuild-into-`kept` rather than an in-place compaction
     * because the two have to happen together: a survivor keeps its flag, a
     * departure loses it, and an arrival starts without one. Doing it in place
     * means deleting by swapping the tail down, which reorders the table under
     * the loop that is reading it — the class of bug that produced the parallel
     * `loaded[]` array's disagreements in the first place.
     */
    for( int i = 0; i < wanted_count; i++ )
    {
        struct Mock230PlayerZone* held = mock230_playerzonemap_find(player, wanted[i]);

        kept[kept_count].index = wanted[i];
        kept[kept_count].loaded = held ? held->loaded : 0;
        kept_count++;
    }
    memcpy(player->zonemap.zones, kept, sizeof(kept[0]) * (size_t)kept_count);
    player->zonemap.count = kept_count;
    player->zone_index = here;
    player->zonemap.built_zone_x = srv->zone_x;
    player->zonemap.built_zone_z = srv->zone_z;
}

/* The gap to a npc's FOOTPRINT, per axis. See mock230_zone.h — the header
 * carries why view range must not be measured off the origin corner. */
void
mock230_npc_view_deltas(
    const struct Mock230Npc* npc,
    const struct Mock230Player* player,
    int* out_dx,
    int* out_dz)
{
    int size = npc->size > 0 ? npc->size : 1;
    int dx = 0;
    int dz = 0;

    if( npc->x > player->x )
        dx = npc->x - player->x;
    else if( player->x > npc->x + size - 1 )
        dx = player->x - (npc->x + size - 1);
    if( npc->z > player->z )
        dz = npc->z - player->z;
    else if( player->z > npc->z + size - 1 )
        dz = player->z - (npc->z + size - 1);

    *out_dx = dx;
    *out_dz = dz;
}

/*
 * Who is standing in this client's zones, right now.
 *
 * Walked at the moment a packet needs it rather than kept up to date, which is
 * what makes it impossible for the answer to be stale.
 *
 * The plane and the view radius are applied HERE, per entity — not per zone and
 * not by the caller. Per zone is not enough: a zone straddling the edge of the
 * radius contributes everything in it, up to 7 tiles past the range, and `out`
 * is bounded, so a dense fringe can fill it with entities that will be
 * discarded and crowd out ones that are actually beside the player. That is
 * unreachable while a world holds 63 npcs and ordinary at 23,139.
 */
static int
area_entities(
    struct Mock230Player* player,
    int radius,
    int want_players,
    int* out,
    int max)
{
    struct Mock230Server* srv = player->world;
    int count = 0;
    /*
     * The zone box below rejects whole zones on the ORIGIN tile, which is the
     * only coordinate a zone files an npc under — so it has to allow for a
     * footprint reaching back into range from an origin outside it, or the
     * per-entity test never gets asked about the npc that needed it. Players
     * are 1x1 and pay nothing.
     */
    int zone_pad = want_players ? 0 : MOCK230_NPC_SIZE_MAX - 1;

    if( !srv )
        return 0;
    for( int i = 0; i < player->zonemap.count && count < max; i++ )
    {
        int index = player->zonemap.zones[i].index;
        int zone_x = (index & 0x7ff) << 3;
        int zone_z = ((index >> 11) & 0x7ff) << 3;
        /* A zone describes its own plane rather than borrowing the player's.
         * The subscription spans all four — a loc change one storey up still
         * has to be addressable — and the entity streams span one. */
        int zone_level = (index >> 22) & 3;
        struct Mock230Zone* zone;

        if( zone_level != player->level )
            continue;
        /* The zone box first, as a cheap reject for the 40-odd zones that
         * cannot contain anything in range. */
        if( zone_x > player->x + radius + zone_pad ||
            zone_x + MOCK230_ZONE_TILES - 1 < player->x - radius - zone_pad )
            continue;
        if( zone_z > player->z + radius + zone_pad ||
            zone_z + MOCK230_ZONE_TILES - 1 < player->z - radius - zone_pad )
            continue;
        zone = zone_by_index(srv, index);
        if( !zone )
            continue;
        if( want_players )
        {
            for( int n = 0; n < zone->player_count && count < max; n++ )
            {
                struct Mock230Player* other = &srv->players[zone->players[n]];

                if( other->x < player->x - radius || other->x > player->x + radius )
                    continue;
                if( other->z < player->z - radius || other->z > player->z + radius )
                    continue;
                out[count++] = zone->players[n];
            }
        }
        else
        {
            for( int n = 0; n < zone->npc_count && count < max; n++ )
            {
                struct Mock230Npc* npc = &srv->npcs[zone->npcs[n]];
                int dx;
                int dz;

                /* To the footprint, not to the origin corner — see
                 * mock230_npc_view_deltas. */
                mock230_npc_view_deltas(npc, player, &dx, &dz);
                if( dx > radius || dz > radius )
                    continue;
                out[count++] = zone->npcs[n];
            }
        }
    }
    return count;
}

int
mock230_playerzonemap_npcs(
    struct Mock230Player* player,
    int radius,
    int* out,
    int max)
{
    return area_entities(player, radius, 0, out, max);
}

int
mock230_playerzonemap_players(
    struct Mock230Player* player,
    int radius,
    int* out,
    int max)
{
    return area_entities(player, radius, 1, out, max);
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

        if( !obj->active || !mock230_world_ground_visible_to(srv, zone->objs[i], player->pid) )
            continue;
        memset(&event, 0, sizeof(event));
        event.kind = MOCK230_ZONE_EV_OBJ_ADD;
        event.receiver_pid = obj->receiver_pid;
        event.pos = zone_pos(obj->x, obj->z);
        event.id = obj->obj_id;
        event.count = obj->count;
        /* Rev-239 ground-object prots have no top-level opcode: they must be
         * carried as an ordinal-prefixed PARTIAL_ENCLOSED record even during
         * the initial zone-state replay.  Sending the classic standalone form
         * resolves OBJ_ADD to -1 and silently loses persistent floor loot. */
        if( mock230_zone_sub_standalone(srv->wire, event.kind) )
            mock230_send_zone_sub(player, &event);
        else
        {
            uint8_t one[256];
            int written = mock230_encode_zone_sub(srv->wire, one, (int)sizeof(one), &event);
            if( written > 0 )
                mock230_send_zone_enclosed(player, obj->x >> 3, obj->z >> 3,
                                            obj->level, one, written);
        }
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

/*
 * Does this everyone-event name an npc, and so have to be encoded per client?
 *
 * A projectile that homes on an npc carries that npc's index, and an npc's index
 * is PRIVATE to each observer (Mock230PlayerSlotMap): the world slot the event
 * holds means a different npc — or none — on every stream it reaches. So the
 * event is seen by everyone but cannot be *written* once for everyone, and it
 * has to leave the shared blob even though its receiver is -1.
 *
 * Player targets are the other half of the same field (`-pid - 1`) and stay in
 * the shared blob: player ids are absolute, the same number on every stream.
 */
static int
zone_event_names_npc(const struct Mock230ZoneEvent* event)
{
    return event->kind == MOCK230_ZONE_EV_PROJANIM && event->target > 0;
}

/*
 * The projectile's target index as ONE client names it.
 *
 * `target` is the wire's encoding of "whom": `slot + 1` for an npc, `-pid - 1`
 * for a player, 0 for nobody. Only the npc half is per-client. An npc this
 * client is not tracking becomes 0 rather than a guess: with no target the
 * client flies the arc to the destination tile the packet already carries —
 * which is where the target stood at the cast — whereas a stale index homes the
 * shot onto whichever npc happens to answer to that name.
 */
static int
projanim_target_for_client(
    const struct Mock230Player* player,
    int target)
{
    int client_slot;

    if( target <= 0 )
        return target;
    client_slot = mock230_slotmap_client(player, target - 1);
    if( client_slot < 0 )
        return 0;
    return client_slot + 1;
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
        /* Written per client below instead — see zone_event_names_npc. */
        if( zone_event_names_npc(&zone->events[i]) )
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

    /*
     * The window first, in case anything moved the player since phase 8 — a
     * mid-tick teleport, an instance built underneath them. Idempotent when it
     * has not.
     *
     * "Drop what fell out of the window" used to be a block of its own here,
     * reconciling a `loaded[]` array against a `zones[]` array by hand. It is
     * gone: `loaded` is a field on the zone entry now, so a zone leaving the
     * window takes its flag with it and there is nothing left to keep in step.
     * The client keeps drawing those zones — they are still inside its 104x104
     * scene — it simply stops being told about them, and is caught up in full
     * if it comes back.
     */
    mock230_playerzonemap_move(player);

    for( int i = 0; i < player->zonemap.count; i++ )
    {
        struct Mock230PlayerZone* entry = &player->zonemap.zones[i];
        int index = entry->index;
        struct Mock230Zone* zone = zone_by_index(srv, index);
        int zone_x = index & 0x7ff;
        int zone_z = (index >> 11) & 0x7ff;
        /* Bits 22-23 of the key (mock230_zone_index). Recovering it is what
         * lets a zone describe its own plane instead of borrowing the
         * player's. */
        int zone_level = (index >> 22) & 3;
        int loaded = entry->loaded;

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
            entry->loaded = 1;
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
            const struct Mock230ZoneEvent* event = &zone->events[e];
            struct Mock230ZoneEvent local;

            if( event->receiver_pid >= 0 )
            {
                if( event->receiver_pid != player->pid )
                    continue;
            }
            else if( zone_event_names_npc(event) )
            {
                /* Everyone sees it; only the npc it names is spelled
                 * differently on each stream. */
                local = *event;
                local.target = projanim_target_for_client(player, event->target);
                event = &local;
            }
            else
            {
                continue; /* already in the shared blob */
            }

            if( mock230_zone_sub_standalone(srv->wire, event->kind) )
            {
                mock230_send_zone_header(player, zone_x, zone_z, zone_level, 0);
                mock230_send_zone_sub(player, event);
                continue;
            }
            {
                uint8_t one[256];
                int written = mock230_encode_zone_sub(srv->wire, one, (int)sizeof(one), event);

                if( written > 0 )
                    mock230_send_zone_enclosed(player, zone_x, zone_z, zone_level, one, written);
            }
        }
    }
}

void
mock230_zone_player_reset(struct Mock230Player* player)
{
    /* Through mock230_playerzonemap_clear so the subscription, the loaded set and the
     * `zone_index` latch are dropped together — a reset that cleared the zones
     * and left the latch would rebuild nothing. */
    mock230_playerzonemap_clear(player);
}
