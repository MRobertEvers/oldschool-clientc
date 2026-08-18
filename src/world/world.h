#ifndef WORLD_H
#define WORLD_H

#include "world_entity.h"

#include "engine/world_builder/collision_map.h"
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/minimap.h"
#include "painters/painters.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;

/* Sized for a rebuild's bulk despawn (projectiles + spotanims + out-of-scene
 * stacks) plus a frame of NPC/player churn. Overflow is a programming error —
 * silently dropping EntityRemoved orphans DYNAMIC scene elements across the
 * next ClearPool(STATIC) and starves the element free list. */
#define WORLD_MAX_EVENTS 8192
#define WORLD_SCENERY_PICK_MAX 4096
#define WORLD_LOC_CHANGE_MAX 256

#define WORLD_MAP_TERRAIN_X 64
#define WORLD_MAP_TERRAIN_Z 64
#define WORLD_MAP_TERRAIN_LEVELS 4

enum WorldEventKind
{
    WorldEventKind_EntityRemoved = 0,
    /* A free-standing map spotanim's start delay has just elapsed and it is
     * drawing for the first time. Its sequence must begin HERE, not at spawn:
     * the element exists from spawn so the app can build its model, but a
     * splash timed to a projectile's arrival spends its whole delay invisible,
     * and an animation stepped through that delay is already finished — or
     * cleared — by the time anyone can see it. See App_WorldDrainEntityRemoved
     * and app_world_spawn_spotanim_now for the two halves. */
    WorldEventKind_SpotanimStarted = 1,
};

struct World_Event
{
    enum WorldEventKind kind;
    int element_id;
};

struct World_SceneryPick
{
    int element_id;
    int loc_id;
    int scenery_index;
};

typedef int (*World_HeightFn)(
    void* userdata,
    int world_x,
    int world_z,
    int level);

/*
 * Sequence-config timing source for entity animation stepping. Keeps the
 * World free of renderer/cache types: the app implements the getters over
 * the scene animation registry (ToriDraw_Animation carries the seq meta).
 * Getters return sane defaults for unknown/unloaded seq ids:
 *   frame_count 0, frame_duration 1, frame_step 0, max_loops 99,
 *   priority 5, duplicate_behavior -1, preanim_move 0, postanim_move 0.
 */
struct World_SeqSource
{
    void* userdata;
    int (*frame_count)(void* userdata, int seq_id);
    int (*frame_duration)(void* userdata, int seq_id, int frame);
    int (*frame_step)(void* userdata, int seq_id);
    int (*max_loops)(void* userdata, int seq_id);
    int (*priority)(void* userdata, int seq_id);
    int (*duplicate_behavior)(void* userdata, int seq_id);
    int (*preanim_move)(void* userdata, int seq_id);
    int (*postanim_move)(void* userdata, int seq_id);
    /* Non-zero when the seq stretches the model along its motion (reference
     * SeqType.stretches). Drives the entity's forward draw-padding so a
     * stretching primary action registers with the painter over the tile ahead
     * and does not draw in front of a wall it should sit behind. */
    int (*stretches)(void* userdata, int seq_id);
    /* Resolve a spotanim (SpotType) id to its animation seq id (-1 if none or
     * not yet resident). Lets the world step an entity's attached-graphic frame
     * from the spot's own seq without pulling in cache/config types. */
    int (*spotanim_seq)(void* userdata, int spotanim_id);
};

#define WORLD_MAPFUNC_MAX 1000

struct World_MapFunctionIcon
{
    int x;
    int z;
    int level;
    int func;
};

/** Loc "mapscene" minimap icon (reference LocType.mapscene, drawn by
 * Client.ts drawDetail): a Pix8 sprite baked into the minimap image at a loc's
 * footprint. Sourced from wall + centrepiece/roof locs whose config carries a
 * mapscene index (trees, rocks, altars, …); gathered at scene build and plotted
 * per baked level in app_rebuild_world_map. `width`/`length` are the loc's raw
 * config footprint (size_x/size_z, NOT orientation-swapped — drawDetail centers
 * with the raw values). */
/*
 * A loc in the built scene that emits ambient sound.
 *
 * Gathered at scene build the same way mapscene icons are, for the same reason:
 * the builder is the only place that has both the placed loc and its config, and
 * re-deriving either afterwards would mean walking the map again. The audio
 * layer turns each of these into a looping voice whose volume and pan follow the
 * camera, and drops them all when the scene is rebuilt.
 */
struct World_AreaSound
{
    /** Scene-space south-west tile of the loc's footprint. */
    int x;
    int z;
    /** Footprint in tiles, with the loc's orientation already applied. The
     *  reference measures distance to the *box*, not to a point, so a
     *  four-tile waterfall is equally loud along its whole length. */
    int size_x;
    int size_z;
    int level;
    /** The loc's config id, so a scene rebuild can recognise an emitter it is
     *  already playing instead of restarting it. */
    int loc_id;
    /** The continuous sound, or -1 when this loc declares none. Independent of
     *  the random set below: a loc may declare both, and 33 in osrs239 do. */
    int sound_id;
    /** Random alternatives, borrowed from the loc config (which outlives the
     *  scene: configs are cached by the provider, not by the world). */
    const int* sound_ids;
    int sound_id_count;
    /** Ticks between plays for the random set. */
    int ticks_min;
    int ticks_max;
    /** Tiles at which it is inaudible. */
    int distance;
    /** Tiles within which it is at full volume; the falloff runs from here out
     *  to `distance`. Loc opcode 78/79's fourth field. */
    int inner;
};

struct World_MapSceneIcon
{
    int x;
    int z;
    int level;
    int mapscene;
    int width;
    int length;
};

/** Client-side temporary loc change (Client-TS LocChange / deob class99).
 * Coordinates are scene-local; rebuilt maps drop entries that leave [0,104). */
struct World_LocChange
{
    int level;
    int layer;
    int x;
    int z;
    int old_type;
    int old_angle;
    int old_shape;
    int new_type;
    int new_angle;
    int new_shape;
    int start_time;
    int end_time;
};

struct World
{
    int _base_tile_x;
    int _base_tile_z;
    int _chunk_sw_x;
    int _chunk_sw_z;
    int _chunk_ne_x;
    int _chunk_ne_z;
    int _offset_x;
    int _offset_z;
    int _scene_size;

    bool load_complete;
    /** Generation counter, bumped every time a rebuild finishes
     * (World_SetLoadComplete(true)). Load *completion* is no longer detected by
     * polling this (Task_WorldLoad runs App_WorldLoadFinish at its tail); it
     * survives only as a cheap "the scene changed" edge for debug one-shots and
     * staleness checks that would otherwise re-run every frame. */
    unsigned load_seq;

    /** Bumped whenever a runtime loc change edits the minimap wall bits (door
     * opened/closed): app_world_map_poll rebakes the baked map sprite when this
     * moves, so the red door line follows the world state. */
    unsigned minimap_seq;

    /** Client cycle counter (reference loopCycle): advanced by World_Cycle,
     * stamps exact-move windows / spotanim delays / hitmark expiry. */
    int cycle;

    struct World_SeqSource seq_source;

    struct World_EntityList entities;

    struct World_Event events[WORLD_MAX_EVENTS];
    int event_count;

    struct World_SceneryPick scenery_picks[WORLD_SCENERY_PICK_MAX];
    int scenery_pick_count;

    World_HeightFn height_fn;
    void* height_userdata;

    struct Heightmap* heightmap;
    struct CollisionMap* collision_maps[COLLISION_LEVELS];
    struct Minimap* minimap;
    struct Painter* painter;
    struct PaintersCullMap* cullmap;
    /** Shared ToriDraw scene; set by App after World_New so dynamic painter
     * registration can read model bounds for occlusion heights. */
    struct ToriDraw_Scene* scene;

    /** Raw land "settings" byte per scene tile (reference mapl): 0x1 block,
     * 0x2 link-below (bridge), 0x4 remove-roof, 0x8 vis-below. Persisted
     * from the builder flag map so the per-frame roof check can raycast
     * camera->player (Client-TS roofCheck). scene_size^2 per level. */
    uint8_t* tile_flags;

    /** Per-tile ground-stack raise height (Client-TS Model.objRaise / LocType
     * raiseobject). Written at loc place when raiseobject==1 as max(-vy);
     * App_WorldObjStackAdd subtracts it from ground Y so items sit on tables.
     * scene_size^2 per level; 0 = flat ground. Survives after the builder's
     * decor_buildmap is freed. */
    int16_t* obj_raise;

    /** Per-tile "last frame a stationary 1x1 entity claimed this square"
     *  stamp (reference Client.tileLastOccupiedCycle) — scene_size^2, compared
     *  against `scene_cycle`. The dynamic-registration pass draws at most one
     *  tile-centred 1x1 entity per tile per frame; the first to claim a tile
     *  (in add order: local player, alwaysontop NPC, other players, normal
     *  NPCs) wins and the rest are skipped, preventing stacked models from
     *  z-fighting. NULL until the scene is allocated. */
    int* tile_last_occupied_cycle;
    /** Monotonic frame counter (reference Client.sceneCycle): bumped once per
     *  dynamic-registration pass so the stamp above never needs clearing. */
    int scene_cycle;
    /** Server pid of the local player (esync.local_pid), mirrored here so the
     *  render-cycle dynamic pass can register the local player first — the
     *  reference draws `addPlayers(true)` (self) ahead of every other entity,
     *  and self is never dedup-skipped. -1 = offline / not yet bound. */
    int local_pid;

    /** Loc "mapfunction" minimap icons gathered at scene build (reference
     * activeMapFunctionX/Z/Count, Client.ts minimapBuildBuffer): scene tile
     * after the random-walk spread + func (dat1 atlas frame / dat2 mapelement
     * id). */
    struct World_MapFunctionIcon mapfuncs[WORLD_MAPFUNC_MAX];
    int mapfunc_count;

    /** Loc "mapscene" minimap icons gathered at scene build (reference
     * drawDetail's mapscene plot, Client.ts). Grown dynamically: a densely
     * wooded scene has far more of these than mapfunctions, so a fixed cap
     * would silently drop icons. Buffer persists across rebuilds; count is
     * reset in World_ResetSceneAlloc. */
    struct World_MapSceneIcon* mapscenes;
    int mapscene_count;
    int mapscene_capacity;

    /** Ambient-sound emitters gathered at scene build. Same lifetime rules as
     *  mapscenes: buffer persists across rebuilds, count resets. */
    struct World_AreaSound* area_sounds;
    int area_sound_count;
    int area_sound_capacity;
    /** Bumped on every rebuild so the audio layer can tell "same list" from
     *  "new list that happens to be the same length". */
    int area_sound_generation;

    /** Client-side temporary loc-change list (Client-TS locChanges /
     * deob world.field1353). Pushed by App_WorldLocChange; shifted on
     * REBUILD_NORMAL and unlinked when out of the 104 window. Not re-applied
     * after a rebuild — the server re-sends zone state. */
    struct World_LocChange loc_changes[WORLD_LOC_CHANGE_MAX];
    int loc_change_count;
};

/** Padded mover painter footprint: the tile span (pos±padding)>>7 covers,
 * (Client-TS World.addDynamic). Padding is 60 for size-1 movers/projectiles,
 * 60+(size-1)*64 for larger NPCs. */
struct World_PainterFootprint
{
    int sx;
    int sz;
    int size_x;
    int size_z;
};

/** Compute the padded tile span an entity registers over. Returns false (and
 * leaves *out untouched) when any tile of the span falls outside [0,scene_size)
 * — the reference World.setSprite rejects such a sprite wholesale rather than
 * clamping it, so the caller must skip registration (never draw it). Clamping
 * instead would store an out-of-bounds element sx and crash the painter's tile
 * lookup on the next frame.
 *
 * When `forward_padding` is non-zero the span is extended one tile along `yaw`
 * (reference World.addDynamic forwardPadding, driven by
 * ClientEntity.needsForwardDrawPadding): a stretching primary animation reaches
 * into the tile ahead, so it must register there or the painter draws it in
 * front of a wall it should sit behind. `yaw` is a 0..2047 orientation and is
 * ignored when `forward_padding` is 0. */
bool
World_EntityPainterFootprint(
    int pos_x,
    int pos_z,
    int draw_padding,
    int yaw,
    int forward_padding,
    int scene_size,
    struct World_PainterFootprint* out);

/** tile_flags lookup, 0 for out-of-range/unloaded. */
int
World_TileFlagGet(
    struct World const* world,
    int x,
    int z,
    int level);

/**
 * Bridge columns: the three level spaces a loc lives in, and how to travel
 * between them.
 *
 * A bridge deck is authored one plane above the plane it is walked from
 * (LINK_BELOW on the column's cache level 1). Three different consumers
 * disagree about which plane that means, and each one is right:
 *
 *  - the **cache** level is what the map stream says and what the world keeps —
 *    scenery pool, collision stamps, heightmap reads. Client-TS ClientBuild
 *    places locs on the raw level and lets `World.pushDown` sort the drawing.
 *  - the **paint** level is where the push-down parked that tile
 *    (WorldBuilder_RebuildCenterzoneEnd): cache 1 -> paint 0, and the underside,
 *    cache 0, is parked at paint 3.
 *  - the **wire** level is the plane the server names, which is the plane the
 *    player walks — LostCity `GameMap.loadLocations` shifts once at read time
 *    and every zone event, click and `loc_find` uses that.
 *
 * Non-bridge columns collapse all three, which is why getting this wrong is
 * invisible until a bridge deck carries a loc something mutates.
 */
int
World_LocPaintLevel(
    struct World const* world,
    int x,
    int z,
    int cache_level);

/** Wire (walked) level -> cache level, the trip a zone loc packet has to make
 *  before it can name the loc the map placed. Mirrors app_world_height's own
 *  +1 for heights (Client.ts getAvH), including its `level < 3` guard. */
int
World_LocCacheLevel(
    struct World const* world,
    int x,
    int z,
    int wire_level);

/** Ground-stack raise height for a scene tile (0 if none / out of range). */
int
World_ObjRaiseGet(
    struct World const* world,
    int x,
    int z,
    int level);

/** Keep the max raise height on a tile (Client-TS setObj takes max objRaise). */
void
World_ObjRaiseSetMax(
    struct World* world,
    int x,
    int z,
    int level,
    int raise);

static inline int
World_MapTileCoord(
    int x,
    int z,
    int level)
{
    return x + z * WORLD_MAP_TERRAIN_X + level * (WORLD_MAP_TERRAIN_X * WORLD_MAP_TERRAIN_Z);
}

static inline int
World_ToSceneX(
    struct World* world,
    int mapx,
    int chunk_x)
{
    return (chunk_x - world->_offset_x) + (mapx - world->_chunk_sw_x) * WORLD_MAP_TERRAIN_X;
}

static inline int
World_ToSceneZ(
    struct World* world,
    int mapz,
    int chunk_z)
{
    return (chunk_z - world->_offset_z) + (mapz - world->_chunk_sw_z) * WORLD_MAP_TERRAIN_Z;
}

static inline int
World_TerrainTileIdx(
    struct World* world,
    int x,
    int z,
    int level)
{
    return x + z * world->_scene_size + level * world->_scene_size * world->_scene_size;
}

struct World*
World_New(void);

void
World_Free(struct World* world);

/** Bind the shared ToriDraw scene (for occlusion model-height lookups). */
void
World_SetScene(
    struct World* world,
    struct ToriDraw_Scene* scene);

/** Append a loc mapscene minimap icon (reference drawDetail mapscene gather),
 * growing world->mapscenes as needed. Silently no-ops on alloc failure. */
void
World_AddMapSceneIcon(
    struct World* world,
    int x,
    int z,
    int level,
    int mapscene,
    int width,
    int length);

/** Append an ambient-sound emitter (copied), growing world->area_sounds as
 *  needed. Silently no-ops on alloc failure. Takes the whole record rather than
 *  a parameter list because the emitter grew a footprint, an inner radius and a
 *  loc id, and a nine-int call is where transposed arguments live. */
void
World_AddAreaSound(
    struct World* world,
    const struct World_AreaSound* source);

/**
 * Drop every emitter whose south-west tile is (x, z) on `level`.
 *
 * For a runtime loc change: a door that opens, a machine that is switched off.
 * Without this the emitter list is only ever rebuilt by a full scene build, so a
 * loc that stops existing keeps humming until the player walks far enough to
 * force a rebuild. Bumps `area_sound_generation` when it removes anything,
 * which is what makes the audio layer re-resolve its live voices.
 *
 * Returns the number removed.
 */
int
World_RemoveAreaSoundAt(
    struct World* world,
    int x,
    int z,
    int level);

void
World_SetHeightFn(
    struct World* world,
    World_HeightFn fn,
    void* userdata);

void
World_ResetScene(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
World_ResetSceneChunkList(
    struct World* world,
    const int* chunks_xz,
    int count);

void
World_SetLoadComplete(
    struct World* world,
    bool complete);

void
World_TerrainSet(
    struct World* world,
    int element_id,
    int x,
    int z,
    int level);

void
World_TerrainReset(struct World* world);

int
World_TerrainElementAt(
    struct World* world,
    int x,
    int z,
    int level);

int
World_PlayerSpawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    struct WorldEntityFacet_IdleAnimations idle_animations);

void
World_PlayerDespawn(
    struct World* world,
    int idx);

int
World_NpcSpawn(
    struct World* world,
    int element_id,
    int npc_id,
    int level,
    int scene_x,
    int scene_z,
    int size,
    struct WorldEntityFacet_IdleAnimations idle_animations);

void
World_NpcDespawn(
    struct World* world,
    int idx);

/** `target` is the wire target-entity id (see WorldEntity_Projectile.target):
 * non-zero makes the arc home on that entity's live position every cycle,
 * WORLD_PROJECTILE_TARGET_NONE pins it to dst_x/dst_z. */
int
World_ProjectileSpawn(
    struct World* world,
    int element_id,
    int level,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int h1,
    int end_height,
    int t1,
    int t2,
    int angle,
    int startpos,
    int target);

void
World_ProjectileDespawn(
    struct World* world,
    int idx);

int
World_SpotanimSpawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    int y,
    int orientation,
    int idle_delay,
    int lifetime);

void
World_SpotanimDespawn(
    struct World* world,
    int idx);

int
World_SceneryRegister(
    struct World* world,
    int element_id,
    int loc_id,
    int scene_x,
    int scene_z,
    int level,
    int size_x,
    int size_z,
    int shape,
    int angle,
    int force_approach,
    char const* name,
    char const actions[5][32],
    int interactive);

struct WorldEntity_Scenery*
World_SceneryGetByElementId(
    struct World* world,
    int element_id);

/** Pool walk by the server's own slot number — the id space faceEntity uses.
 * Returns NULL for an unsynced/unknown slot. */
struct WorldEntity_NPC*
World_NpcGetByServerSlot(
    struct World* world,
    int server_slot);

struct WorldEntity_Player*
World_PlayerGetByServerPid(
    struct World* world,
    int server_pid);

/** Pool walk by painter element id. Returns NULL when no player owns it. */
struct WorldEntity_Player*
World_PlayerGetByElementId(
    struct World* world,
    int element_id);

/** Pool walk; out_index (optional) receives the npc pool index for the
 * face/path mutators. */
struct WorldEntity_NPC*
World_NpcGetByElementId(
    struct World* world,
    int element_id,
    int* out_index);

void
World_ClearSceneryPicks(struct World* world);

void
World_RegisterSceneryPick(
    struct World* world,
    int element_id,
    int loc_id);

void
World_PlayerPathPushStep(
    struct World* world,
    int idx,
    int step_type,
    int direction);

void
World_NpcPathPushStep(
    struct World* world,
    int idx,
    int step_type,
    int direction);

void
World_PlayerPathJump(
    struct World* world,
    int idx,
    bool force_teleport,
    int x,
    int z);

void
World_PlayerPathJumpCollisionAware(
    struct World* world,
    int idx,
    struct CollisionMap* collision,
    bool force_teleport,
    int x,
    int z,
    int step_type);

void
World_NpcPathJump(
    struct World* world,
    int idx,
    bool force_teleport,
    int x,
    int z);

/** Lock an entity's facing onto another (reference faceEntity): the server
 * slot, npc slots as-is and player slots + WORLD_FACING_PLAYER_BASE;
 * WORLD_FACING_ENTITY_NONE clears. Applied every cycle by World_Cycle. */
void
World_PlayerFaceEntity(
    struct World* world,
    int idx,
    int entity_id);

void
World_NpcFaceEntity(
    struct World* world,
    int idx,
    int entity_id);

void
World_PlayerFaceEntityDetailed(
    struct World* world,
    int idx,
    int entity_id,
    int fallback_angle,
    bool instant);

void
World_NpcFaceEntityDetailed(
    struct World* world,
    int idx,
    int entity_id,
    int fallback_angle,
    bool instant);

/** Select the revision-239 facing movement policy and clear any older facing
 * kind. These are used only by the modern generic Face block; classic entity
 * and square masks remain independent. */
void
World_PlayerBeginModernFacing(
    struct World* world,
    int idx,
    int movement_mode);

void
World_NpcBeginModernFacing(
    struct World* world,
    int idx,
    int movement_mode);

/** One-shot face-coord (reference faceSquareX/Z). Coordinates are the raw
 * wire form — absolute half-tiles, (tile << 1) + 1 — because 0,0 is the
 * reference's "none" sentinel and converting to scene tiles first would make
 * scene tile 0 unreachable. Consumed and cleared by the next cycle. */
void
World_PlayerFaceCoord(
    struct World* world,
    int idx,
    int square_x,
    int square_z);

void
World_NpcFaceCoord(
    struct World* world,
    int idx,
    int square_x,
    int square_z);

void
World_PlayerFaceAngle(
    struct World* world,
    int idx,
    int angle,
    bool instant);

void
World_NpcFaceAngle(
    struct World* world,
    int idx,
    int angle,
    bool instant);

void
World_PlayerSetAnimation(
    struct World* world,
    int idx,
    int animation_id,
    int animation_type);

void
World_NpcSetAnimation(
    struct World* world,
    int idx,
    int animation_id,
    int animation_type);

#define WORLD_ANIMATION_TYPE_PRIMARY 0
#define WORLD_ANIMATION_TYPE_SECONDARY 1

void
World_SetSeqSource(
    struct World* world,
    struct World_SeqSource const* source);

/* Server-driven primary (transient) animation with reference semantics:
 * same-seq RestartMode RESET zeroes frame/cycle/loop, RESETLOOP zeroes the
 * loop counter; otherwise the new seq applies only when its priority >= the
 * playing seq's. Records preanim_route_length. */
void
World_PlayerSetPrimaryAnimation(
    struct World* world,
    int idx,
    int seq_id,
    int delay);

void
World_NpcSetPrimaryAnimation(
    struct World* world,
    int idx,
    int seq_id,
    int delay);

/* Reference EXACTMOVE: scene-local tiles, cycle deltas from now. */
void
World_PlayerSetExactMove(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing);

void
World_PlayerSetExactMoveDetailed(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing,
    bool facing_is_yaw);

void
World_NpcSetExactMove(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing);

void
World_NpcSetExactMoveDetailed(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing,
    bool facing_is_yaw);

void
World_PlayerSetSpotanim(
    struct World* world,
    int idx,
    int spotanim_id,
    int height,
    int cycle_delay);

void
World_NpcSetSpotanim(
    struct World* world,
    int idx,
    int spotanim_id,
    int height,
    int cycle_delay);

/* CHANGE_TYPE (transmog): caller passes the new size + anim set (with the
 * reference walkanim_l/r swap already applied). */
void
World_NpcSetType(
    struct World* world,
    int idx,
    int npc_id,
    int size,
    struct WorldEntityFacet_IdleAnimations const* idle);

void
World_PlayerSetAppearance(
    struct World* world,
    int idx,
    int const slots[12],
    int const identkit[12],
    int const colors[5],
    struct WorldEntityFacet_IdleAnimations const* idle,
    char const* name,
    int combat_level,
    int gender);

void
World_PlayerAddHitmark(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health);

void
World_PlayerAddHitmarkTimed(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health,
    int delay,
    int slot_limit,
    int duration,
    int slot_policy);

void
World_NpcAddHitmark(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health);

void
World_NpcAddHitmarkTimed(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health,
    int delay,
    int slot_limit,
    int duration,
    int slot_policy);

void
World_PlayerSetHealthbar(struct World* world, int idx, int fill, int width);

void
World_NpcSetHealthbar(struct World* world, int idx, int fill, int width);

void
World_PlayerClearHealthbar(struct World* world, int idx);

void
World_NpcClearHealthbar(struct World* world, int idx);

/** Set an entity's overhead chat text (reference chatMessage/Colour/Effect,
 * chatTimer reset to 150). colour/effect select the render style; pass 0/0 for
 * plain forced-chat "say" lines. A NULL or empty message clears the overhead. */
void
World_PlayerSetChat(
    struct World* world,
    int idx,
    char const* message,
    int colour,
    int effect);

void
World_NpcSetChat(
    struct World* world,
    int idx,
    char const* message,
    int colour,
    int effect);

/* ---- zone-packet world mutations ---- */

/** Ground item stack add: takes ownership of an already-created scene
 * element. One stack entity per (tile, obj) pair; re-adding refreshes the
 * count. Returns the pool index or -1. */
int
World_ObjStackAdd(
    struct World* world,
    int element_id,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count,
    char const* name,
    char const actions[5][32]);

/** Find a stack by tile + obj id (obj_id -1 = any). Returns pool idx or -1. */
int
World_ObjStackFind(
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int obj_id);

/** Pool walk by scene element id (the world pick classifier's entry point). */
struct WorldEntity_ObjStack*
World_ObjStackGetByElementId(
    struct World* world,
    int element_id);

/** Remove a stack (emits EntityRemoved for its element). */
void
World_ObjStackDel(
    struct World* world,
    int idx);

/** Update a stack's count in place (same obj -> same model). */
void
World_ObjStackSetCount(
    struct World* world,
    int idx,
    int count);

/** Map a loc shape (0-22) to its layer (0=WALL, 1=WALL_DECOR, 2=GROUND,
 * 3=GROUND_DECOR), matching Client-TS LOC_SHAPE_TO_LAYER. Returns -1 for an
 * out-of-range shape. */
int
World_LocShapeToLayer(int shape);

/** Find a scenery entity by tile and loc layer. `loc_shape` is the zone
 * packet's shape (info >> 2); its layer is matched so a door (WALL) never
 * mutates a centrepiece/floor-decor sharing the tile. Pass loc_shape < 0 to
 * match the first loc on the tile regardless of layer. Returns the scenery pool
 * index or -1. */
int
World_SceneryFindAt(
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape);

/** LOC_DEL: remove the scenery entity + its scene element (event emitted). */
void
World_SceneryRemove(
    struct World* world,
    int idx);

/** REBUILD_NORMAL relocation (Client-TS rebuild handler): the scene base
 * moved by (dx, dz) tiles; shift every player/npc/objstack's scene-local
 * coords by the negation (routes, grid, fine draw positions, exact-move).
 * Entities landing outside the scene are parked on tile 255 — still tracked
 * (the server addresses info lists by position) but skipped by the painter
 * bounds checks until the server removes them. Loc-change records shift and
 * are dropped when out of the scene window. */
void
World_ShiftEntities(
    struct World* world,
    int dx,
    int dz);

/** Record a temporary loc change (Client-TS locChanges.push). old_* may be
 * -1 when unknown; end_time of -1 means "permanent until replaced" (not
 * countdown-ticked). end_time > 0 is a countdown (locChangeDoQueue): hide/apply
 * new_type when start_time reaches 0, restore old_type when end_time reaches 0. */
void
World_LocChangePush(
    struct World* world,
    int level,
    int layer,
    int x,
    int z,
    int old_type,
    int old_angle,
    int old_shape,
    int new_type,
    int new_angle,
    int new_shape,
    int start_time,
    int end_time);

/** Clear every temporary loc-change record (scene rebuild). */
void
World_LocChangesClear(struct World* world);

/**
 * Advance countdown LocChanges (Client-TS locChangeDoQueue). Entries with
 * end_time < 0 are permanent records and are skipped. For each elapsed cycle,
 * decrements timers and invokes `apply` when start hits 0 (new_type) or end
 * hits 0 (old_type, then removes the entry).
 */
void
World_LocChangesTick(
    struct World* world,
    int cycles_elapsed,
    void (*apply)(void* user, int level, int x, int z, int loc_id, int shape, int angle),
    void* user);

/** Despawn every projectile + spotanim (reference mapBuild clears both when
 * the new scene lands; their trajectories are scene-local). Emits
 * EntityRemoved events for their elements. */
void
World_ClearProjectilesAndSpotanims(struct World* world);

int
World_EventsCount(struct World* world);

const struct World_Event*
World_EventsPeek(
    struct World* world,
    int i);

void
World_EventsClear(struct World* world);

void
World_Cycle(
    struct World* world,
    int cycles_elapsed);

/**
 * Move every actor along its route by `cycles` worth of travel, where one
 * cycle is 20ms of client time -- rev-239 client.method1894/method3611.
 *
 * Call once per rendered FRAME with the real elapsed time, fractional:
 * `(now_ms - last_frame_ms) / 20.0f`. World_Cycle only picks facings and
 * animations; this is what actually moves anything, and keeping it off the
 * 20ms grid is what stops a walk quantising into a lurch.
 */
void
World_MoversAdvance(
    struct World* world,
    float cycles);

/* Shared with world_cycle.c */
void
World_EmitEvent(
    struct World* world,
    enum WorldEventKind kind,
    int element_id);

void
World_ProjectileSetTarget(
    struct World* world,
    struct WorldEntity_Projectile* p,
    int cycle);

void
World_ProjectileMove(
    struct WorldEntity_Projectile* p,
    int delta);

#endif
