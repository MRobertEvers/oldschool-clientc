#ifndef SRC_EDITOR_MAP_EDITOR_GHOST_H
#define SRC_EDITOR_MAP_EDITOR_GHOST_H

/*
 * The map editor's hover ghost: the translucent preview of the loc that a
 * click would place, standing on the tile under the cursor.
 *
 * The ghost is a real scene placement, not a drawing. That is what makes it
 * look right -- it is lit, shaped and posed by the same builder the committed
 * loc will be -- and it is also the whole difficulty, because placing it
 * EVICTS whatever already held that tile's slot in that layer. A ghost that
 * forgets what it displaced reads as a deletion: hover over a door and the
 * door is gone, hover away and it does not come back.
 *
 * So the ghost remembers its occupant, and the two ways it can end do
 * different things with that memory:
 *
 *   - hovering AWAY restores the occupant. The tile was only ever borrowed.
 *   - COMMITTING forgets it. The click chose to overwrite, and restoring
 *     would resurrect the old loc on top of the one just placed.
 *
 * Nothing here touches a scene. The caller is handed what to place and what to
 * put back, which is what lets the bookkeeping be tested without a world.
 */

#include <stdbool.h>

/** One placement: which loc, where, and in what pose. */
struct MapEditorGhostSpec
{
    int loc_id;
    int scene_x;
    int scene_z;
    int level;
    int shape;
    int angle;
};

/**
 * The SPAWN ghost: the npc or obj a Place click with an npc / obj catalog pick
 * would add, standing translucent on the hovered tile. A sibling of the loc
 * ghost rather than a kind of it, because nothing is displaced: actors and
 * ground items share tiles, so there is no occupant to remember, only the
 * entity the ghost itself became.
 *
 * Placing one is asynchronous (the npc's config and body, the obj's model
 * load first), so a ghost is PENDING until the caller lands it with the pool
 * index the world handed back. Taking it down hands that index back so the
 * caller can despawn exactly what it spawned -- and nothing, when it never
 * landed.
 */
enum MapEditorSpawnKind
{
    MAP_EDITOR_SPAWN_NPC = 1,
    MAP_EDITOR_SPAWN_OBJ = 2,
};

struct MapEditorSpawnSpec
{
    enum MapEditorSpawnKind kind;
    int id;
    int scene_x;
    int scene_z;
    int level;
};

struct MapEditorSpawnGhost
{
    bool active;
    struct MapEditorSpawnSpec spec;
    /** The world pool index it landed as (npc pool, obj-stack pool); -1 while
     *  pending. */
    int entity;
    /** The world it landed in (World.load_seq): a rebuild since freed it. */
    unsigned load_seq;
    /** Footprint side in tiles (the npc type's size, 1 for an obj), known
     *  once landed. */
    int size;
    bool faded;
};

/**
 * Where the editor's pointer is this frame, as the ghosts and the overlay
 * read it (the hover tile, or TORIRS_EDITOR_HOVER's in a headless shot), and
 * the level the next edit writes. Recomputed once per frame by the ghost
 * update; `valid` 0 when the pointer is off the world or on a panel.
 */
struct MapEditorHover
{
    bool valid;
    int scene_x;
    int scene_z;
    int level;
};

struct MapEditorGhost
{
    bool active;
    /** The fade has been written onto the placed element's own model. Separate
     *  from `active` because the placement is asynchronous: the ghost exists
     *  for some frames before there is an element to fade. */
    bool alpha_done;
    struct MapEditorGhostSpec spec;
    /** Whoever held this tile's slot before the ghost took it, and whether
     *  there was anyone. */
    bool displaced_valid;
    struct MapEditorGhostSpec displaced;

    /* Siblings the loc functions below never touch (their Reset clears the
     * loc ghost alone): the App holds every editor preview in this one
     * field. */
    struct MapEditorSpawnGhost spawn;
    struct MapEditorHover hover;
};

/** Clear everything: the loc ghost, the spawn ghost, the hover. */
void
MapEditorGhost_Reset(struct MapEditorGhost* ghost);

/**
 * Whether a ghost standing where `want` describes would already be this one.
 *
 * Any change of tile, loc or pose is a remove and an add; the same spec is
 * nothing to do but the fade. False when no ghost is up.
 */
bool
MapEditorGhost_Matches(
    struct MapEditorGhost const* ghost,
    struct MapEditorGhostSpec const* want);

/**
 * Take the ghost down, and say what has to be put back.
 *
 * Returns false when there was no ghost. `out_restore` is filled and
 * `*out_has_restore` set when the ghost had evicted an occupant; the caller
 * places the ghost's own spec as a deletion first and then that.
 */
bool
MapEditorGhost_Remove(
    struct MapEditorGhost* ghost,
    struct MapEditorGhostSpec* out_removed,
    bool* out_has_restore,
    struct MapEditorGhostSpec* out_restore);

/**
 * Forget the ghost WITHOUT restoring anything -- what a commit click does.
 *
 * The real placement has just replaced the ghost's element on the same tile
 * and layer, so removing would delete the loc that was placed, and restoring
 * the displaced occupant would put the old one back on top of it. Forgetting
 * both is what makes the commit stick.
 *
 * The occupant is cleared as well as the ghost, and that is belt and braces:
 * nothing reads it once the ghost is down. It is cleared anyway because the
 * alternative is a struct carrying a loc id that is no longer true, and the
 * next reader of this type should not have to work out that it is unreachable.
 */
void
MapEditorGhost_Commit(struct MapEditorGhost* ghost);

/**
 * Put a ghost up. `displaced` is whoever held the slot, or NULL for nobody --
 * read BEFORE the placement is queued, because the capture has to see the
 * pre-ghost scene.
 */
void
MapEditorGhost_Place(
    struct MapEditorGhost* ghost,
    struct MapEditorGhostSpec const* spec,
    struct MapEditorGhostSpec const* displaced);

/** The fade has landed; do not write it again. */
void
MapEditorGhost_NoteFaded(struct MapEditorGhost* ghost);

/** True while a ghost is up and its fade has not been written yet. */
bool
MapEditorGhost_NeedsFade(struct MapEditorGhost const* ghost);

/* ---- the spawn ghost ----------------------------------------------------- */

/** Whether a spawn ghost standing as `want` describes is this one (pending or
 *  landed). False when none is up. */
bool
MapEditorSpawnGhost_Matches(
    struct MapEditorSpawnGhost const* ghost,
    struct MapEditorSpawnSpec const* want);

/** Put a spawn ghost up, pending: the caller places the entity and lands it. */
void
MapEditorSpawnGhost_Place(
    struct MapEditorSpawnGhost* ghost,
    struct MapEditorSpawnSpec const* spec);

/** The placement produced pool index `entity` (>= 0) in world `load_seq`,
 *  `size` tiles a side. */
void
MapEditorSpawnGhost_Land(
    struct MapEditorSpawnGhost* ghost,
    int entity,
    unsigned load_seq,
    int size);

/** True while up and not landed: the caller keeps trying to place it. */
bool
MapEditorSpawnGhost_Pending(struct MapEditorSpawnGhost const* ghost);

/**
 * Take the spawn ghost down. Returns false when none was up. `*out_entity`
 * is the pool index to despawn, or -1 when it never landed or landed in a
 * world `load_seq` no longer is (a rebuild already freed it); `out_spec` says
 * what and where.
 */
bool
MapEditorSpawnGhost_Remove(
    struct MapEditorSpawnGhost* ghost,
    unsigned load_seq,
    struct MapEditorSpawnSpec* out_spec,
    int* out_entity);

#endif /* SRC_EDITOR_MAP_EDITOR_GHOST_H */
