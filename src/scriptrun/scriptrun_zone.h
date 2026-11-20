#ifndef SRC_SCRIPTRUN_SCRIPTRUN_ZONE_H
#define SRC_SCRIPTRUN_SCRIPTRUN_ZONE_H

/*
 * The zone families as the headless client keeps them: ground items, the
 * effect log (projectiles and map graphics) and the loc animations, applied
 * by the REAL CLIENT's rules onto the real client's records.
 *
 * WHAT IS SHARED. The records are the real client's: ground rows are World
 * obj stacks (world/world.h World_ObjStack*), written with the calls and the
 * targets App_WorldObjStackAdd / _Del / _SetCount / _SetEnabledOps /
 * _ClearTile make (app/app_world_rebuild.c); the effect log is
 * game/rs_effect_log.h, the one push and decode both clients run. What the
 * real client does on top is presentation (models, plugin notices, the
 * overlay, redraws) and is left out, as is every parallel task those
 * families raise: each only attaches a body (a ground stack's model, a
 * projectile's or a map graphic's spotanim), so none changes game state and
 * none is raised here (scriptrun_core.h, the list of kinds).
 *
 * THE RULES, each the real client's (game/rs_gameproto_exec.c
 * exec_zone_sub_packet_at and its *_specific translations):
 *   - OBJ_ADD is one new row, always; a count of 0 names the row already on
 *     the tile (rev 239's ObjEnabledOps as a reveal) and adds none. Rows sit
 *     in pool order, arrival order: OBJ_DEL and OBJ_COUNT take the first
 *     match from the head, the oldest. OBJ_REVEAL adds for its receiver only;
 *     OBJ_ENABLED_OPS sets the first row's ops. UPDATE_ZONE_FULL_FOLLOWS
 *     clears the zone's rows. A rebuild shifts every row with the scene and
 *     drops those that leave it (App_WorldRebuildShift). A deck's rows live
 *     in that deck's own world.
 *   - OBJ_ADDED / OBJ_REMOVED are stamped at the packet, scene tiles, as the
 *     real client's DRIVE_STAMP sites do.
 *   - MAP_ANIM: a tile outside the view's scene is dropped (the reference's
 *     worldview.method3333 gate); MAP_ANIM_SPECIFIC goes through it.
 *   - MAP_PROJANIM / PROJANIM_SPECIFIC / SCRIPTEDPROJ_ADD are logged in the
 *     view's absolute frame; a SCRIPTEDPROJ_ADD for a slot whose projectile
 *     is still in flight does nothing (rev-241 deob `if (var16 == null)`).
 *   - LOC_ANIM / LOC_ANIM_SPECIFIC are listed only for the root view, at the
 *     loc's cache level (zone_loc_tile_at's bridge climb), for
 *     RS_EFFECT_LOC_ANIM_LIFE_TICKS server ticks.
 *
 * THE PARALLEL TASKS, AND WHAT THIS CLIENT DOES WITH EACH. The ONE list of
 * every kind the real client raises on its parallel queue (game/rs_pending.h
 * ranges, app/app_tasks_*.c). A kind whose apply writes game state is an
 * App-free core in src/game that this client runs too, on its own parallel
 * queue (scriptrun_tasks.h), with assets answered at once; a kind that only
 * draws has nothing to apply here and is not raised.
 *
 *   ground: GROUND_STACK_MODEL, GROUND_WIDGET_ICON -- a model, an icon; the
 *     row is written at the packet on both clients. Not raised.
 *   effects: PROJECTILE_BODY, MAP_ANIM_BODY -- a body; the row is logged at
 *     the packet. (A map graphic whose body fails goes; none fails here.)
 *     Not raised.
 *   locs: LOC_CHANGE (scenery, collision, records) and LOC_MERGE (a timed
 *     hide and restore) write game state: RUN (game/rs_loc_tasks.h).
 *     ZONE_HELD waits for an unbuilt view: not needed, a view's scene is
 *     built at its packet. LOC_SEQ_BIND -- a sequence. Not raised.
 *   entities: NPC_BODY, SEQ_BIND -- a body, a sequence; the entity apply is
 *     shared and synchronous (game/rs_entity_apply.h). Not raised.
 *   interface assets (IF_MODEL, INV_ICON), the mounts' visual half, the
 *     scripts' visual ops -- models and icons. Not raised.
 *
 * App-free: nothing here names the client's App, a scene, a model or a cache.
 */

#include "game/rs_effect_log.h"
#include "game/rs_event_ring.h"

#include <stdint.h>

struct World;

enum
{
    /* World entity view slots (WORLDVIEW_MAX), slot 0 the root. */
    SCRIPTRUN_ZONE_VIEWS = 64,
    /* Scripted projectile slots tracked at once (the rev-241 slot field is
     * wire data; more live slots than this is an assert). */
    SCRIPTRUN_ZONE_SCRIPTED = 256,
};

/* A LOC_ANIM as its packet said it, the real client's App_DriveLocAnim with
 * the server tick in place of the cycle: the sequence a placed loc started.
 * Absolute tiles, the loc's cache level. */
struct ScriptrunLocAnim
{
    int x, z, level;
    int shape;
    int seq;
    int tick; /* the server tick that sent it */
};

/* Where a zone sub-packet applies: what the real client's struct ZoneAt holds
 * (the header's base and plane, the view), with what this client cannot look
 * up in a scene: the view's frame and size, and each zone tile's loc climb. */
struct ScriptrunZoneAt
{
    /* 0 the root, else a deck's view slot. */
    int view;
    /* The view's absolute frame: the root's accepted scene base, a deck's
     * staging base. Absolute = origin + scene tile. */
    int origin_x, origin_z;
    /* The zone's south-west tile, scene tiles of the view, and the walked
     * plane the header named. */
    int base_x, base_z;
    int level;
    /* The view's scene size: a MAP_ANIM outside [0, size) is dropped. */
    int scene_size;
    /* The local player's server index (OBJ_REVEAL's receiver), -1 unknown. */
    int local_pid;
    /* 1 where a loc on zone tile [x][z] sits a cache level above the walked
     * plane (a bridge column, World_LocCacheLevel). */
    uint8_t loc_climb[8][8];
};

struct ScriptrunZone
{
    /* The root's ground rows, in its scene tiles (owned). Its scene base is
     * `base_x`/`base_z`; `have_base` 0 before the first rebuild. */
    struct World* world;
    int have_base;
    int base_x, base_z;
    int scene_size;
    /* A deck's ground rows, made when its first row comes (owned). */
    struct World* decks[SCRIPTRUN_ZONE_VIEWS];

    /* THE EFFECT LOG (game/rs_effect_log.h). */
    struct RS_EventRing effects;
    struct RS_Event effect_entries[RS_EFFECT_LOG_CAP];

    /* LOC_ANIMs of the last RS_EFFECT_LOC_ANIM_LIFE_TICKS ticks, oldest
     * first. */
    struct ScriptrunLocAnim loc_anims[RS_EFFECT_LOC_ANIM_CAP];
    int loc_anim_count;

    /* The scripted projectile slots in flight: slot and its end cycle. */
    int scripted_slot[SCRIPTRUN_ZONE_SCRIPTED];
    int scripted_end[SCRIPTRUN_ZONE_SCRIPTED];
    int scripted_count;

    /* Loc changes recorded since the zone was made (a count for reports). */
    int loc_changes_seen;

    /* Where OBJ_ADDED / OBJ_REMOVED go (the owner's ring); unbound (ring
     * NULL): not stamped. */
    struct RS_EventSink events;
};

/** Initialise `zone` in place. `events` is the ring the obj events go to
 *  (an unbound sink stamps nothing). */
void
ScriptrunZone_Init(
    struct ScriptrunZone* zone,
    struct RS_EventSink events);

/** Release the worlds. */
void
ScriptrunZone_Free(struct ScriptrunZone* zone);

/**
 * The root scene was rebuilt around absolute base (`base_x`, `base_z`),
 * `scene_size` tiles: every root row moves with it and those that leave it
 * go, the scripted projectile slots empty (World_ClearProjectilesAndSpotanims).
 * The real client's App_WorldRebuildShift. Not called for a rebuild the real
 * client skips (the same centre again, its scene complete).
 */
void
ScriptrunZone_Rebuild(
    struct ScriptrunZone* zone,
    int base_x,
    int base_z,
    int scene_size);

/** The world view `view`'s rows and loc change records live in: the root's
 *  (`view` 0), or the deck's own, made at its staging base (`origin_x`,
 *  `origin_z`) and `scene_size` when it has none yet. */
struct World*
ScriptrunZone_ViewWorld(
    struct ScriptrunZone* zone,
    int view,
    int origin_x,
    int origin_z,
    int scene_size);

/** A deck view went (despawned, or its deck rebuilt): its rows go. */
void
ScriptrunZone_DropView(
    struct ScriptrunZone* zone,
    int view);

/** A new server tick: loc animations past their life and scripted slots
 *  whose flight is over go. `now_cycle` is tick * the cycles per tick. */
void
ScriptrunZone_BeginTick(
    struct ScriptrunZone* zone,
    int tick,
    int now_cycle);

/**
 * One zone sub-packet of these families (`name` a PKT_NAME_*, `payload` its
 * struct) at `at`, as exec_zone_sub_packet_at applies it. Other names are
 * ignored. `now_cycle` stamps the log; `tick` the loc animations.
 */
void
ScriptrunZone_Apply(
    struct ScriptrunZone* zone,
    struct ScriptrunZoneAt const* at,
    int tick,
    int now_cycle,
    int name,
    void const* payload);

/* ---- readers ------------------------------------------------------------- */

/* A ground row, absolute tile. */
struct ScriptrunObjRow
{
    int x, z, level;
    int obj_id;
    int count;
    int view;
};

/** Every ground row, root first then each deck, each in pool order (arrival
 *  order). `origin_x`/`origin_z` give each deck view's staging base (index
 *  by view; the root's is the zone's own). At most `cap`; returns how many. */
int
ScriptrunZone_Objs(
    struct ScriptrunZone const* zone,
    int const* deck_origin_x,
    int const* deck_origin_z,
    struct ScriptrunObjRow* out,
    int cap);

/** How many ground rows there are in all. */
int
ScriptrunZone_ObjCount(struct ScriptrunZone const* zone);

#endif /* SRC_SCRIPTRUN_SCRIPTRUN_ZONE_H */
