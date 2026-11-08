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

#include "editor_spawn.h"

#include <stdbool.h>
#include <stdint.h>

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

/* ---- the spawn preview --------------------------------------------------------
 *
 * Every spawn of the open squares standing in the world, npc bodies held still
 * in their ready pose and ground items on their tiles: the Spawns panel's View
 * toggles. Real scene entities, like the spawn ghost, which is what makes them
 * lit, sized and pickable like the npc they preview; marked (editor_preview)
 * so the menu offers "Select spawn" in place of the npc's own ops.
 *
 * The table keeps the rows wanted and what each became. Reconciling it against
 * a new set of rows leaves an unchanged row's entity standing, so an edit to
 * one row does not reload every body in the square. Nothing here touches a
 * world: the caller lands the pending items and removes what it is handed.
 */
#define MAP_EDITOR_SPAWN_PREVIEW_MAX 1024

/** Not looked up yet (the id); no such record is -2. */
#define MAP_EDITOR_SPAWN_PREVIEW_ID_UNKNOWN (-1)
#define MAP_EDITOR_SPAWN_PREVIEW_ID_MISSING (-2)

/** One spawn the preview should show. */
struct MapEditorSpawnPreviewRow
{
    struct Editor_SpawnRow row;
    /** 1 the edited file holds it, 0 the generated one. */
    int edited;
    /** A generated row the edited file hides: drawn translucent. */
    int suppressed;
};

struct MapEditorSpawnPreviewItem
{
    struct MapEditorSpawnPreviewRow want;
    uint32_t hash;
    /** The npc / obj id, or MAP_EDITOR_SPAWN_PREVIEW_ID_*. */
    int id;
    /** The world pool index it landed as; -1 while pending. */
    int entity;
    /** Its scene element once landed; -1 while pending. */
    int element_id;
    /** Footprint side in tiles, 1 until landed. */
    int size;
    /** An obj's translucency has been written (suppressed rows). */
    bool faded;
    /** Reconcile's scratch: a wanted row already claimed this item. */
    bool claimed;
};

struct MapEditorSpawnPreview
{
    struct MapEditorSpawnPreviewItem items[MAP_EDITOR_SPAWN_PREVIEW_MAX];
    int count;
    /** World.load_seq the entities landed in. */
    unsigned load_seq;
    /** The rows' serial the table was last reconciled against. */
    uint32_t rows_serial;
    /** Where the next frame's landing pass starts. */
    int cursor;
};

/** FNV-1a over everything that names the row. */
uint32_t
MapEditorSpawnPreview_RowHash(struct MapEditorSpawnPreviewRow const* row);

/**
 * Make the table hold exactly `rows`: an item whose row is still wanted keeps
 * its entity, one that is not goes into `out_removed` (when it had landed),
 * and each new row becomes a pending item. Returns how many were written to
 * `out_removed`, which must hold MAP_EDITOR_SPAWN_PREVIEW_MAX.
 */
int
MapEditorSpawnPreview_Reconcile(
    struct MapEditorSpawnPreview* preview,
    struct MapEditorSpawnPreviewRow const* rows,
    int count,
    struct MapEditorSpawnPreviewItem* out_removed);

/** The world was rebuilt and took every entity with it: all pending again. */
void
MapEditorSpawnPreview_WorldLost(struct MapEditorSpawnPreview* preview);

/** The item a scene element is, or NULL. */
struct MapEditorSpawnPreviewItem*
MapEditorSpawnPreview_FindElement(
    struct MapEditorSpawnPreview* preview,
    int element_id);

/* ---- the multi-piece ghost (workbench 6.3) ------------------------------------
 *
 * A set of footprints the next action would land: an armed STAMP's pieces at
 * the hovered tile (turned as the stamp is), or a gizmo DRAG's destination
 * for every selected loc. Each piece is a footprint on the ground, TAKEN when
 * its tile+shape is held by a loc the action does not carry along -- a stamp
 * replaces that loc, a moved loc swaps with it -- which the overlay draws
 * orange before the release.
 *
 * Drawn, not placed: unlike the one-loc ghost nothing enters the scene, so
 * there is no occupant to remember and taking it down is forgetting it. It is
 * rebuilt only when what it was built from moves (the key below); a quiet
 * frame keeps it as it is.
 */
#define MAP_EDITOR_PIECE_GHOST_MAX 256

enum MapEditorPieceGhostSource
{
    MAP_EDITOR_PIECE_GHOST_NONE = 0,
    /** An armed stamp at the hovered tile. */
    MAP_EDITOR_PIECE_GHOST_STAMP,
    /** The gizmo drag's destinations. */
    MAP_EDITOR_PIECE_GHOST_DRAG,
};

struct MapEditorPieceGhostPiece
{
    /** SW scene tile, level, footprint. */
    int scene_x;
    int scene_z;
    int level;
    int size_x;
    int size_z;
    /** The landing key holds a loc the action does not carry: orange. */
    bool taken;
};

struct MapEditorPieceGhost
{
    enum MapEditorPieceGhostSource source;
    /** What it was built from: an origin tile and level, a turn (or a drag
     *  offset packed by the caller), and the caller's serial (the document's
     *  revision, the selection's). */
    int key_x;
    int key_z;
    int key_level;
    int key_turn;
    unsigned key_serial;
    int count;
    int taken_count;
    /** Pieces past MAP_EDITOR_PIECE_GHOST_MAX: not drawn, but counted. */
    int dropped;
    struct MapEditorPieceGhostPiece pieces[MAP_EDITOR_PIECE_GHOST_MAX];
};

/** Nothing shown. */
void
MapEditorPieceGhost_Clear(struct MapEditorPieceGhost* ghost);

/** Whether the ghost is built from exactly this (and not NONE). */
bool
MapEditorPieceGhost_Matches(
    struct MapEditorPieceGhost const* ghost,
    enum MapEditorPieceGhostSource source,
    int key_x,
    int key_z,
    int key_level,
    int key_turn,
    unsigned key_serial);

/** Start again from `source` (not NONE) and its key; no pieces yet. */
void
MapEditorPieceGhost_Begin(
    struct MapEditorPieceGhost* ghost,
    enum MapEditorPieceGhostSource source,
    int key_x,
    int key_z,
    int key_level,
    int key_turn,
    unsigned key_serial);

/** One footprint (sizes below 1 assert). False when the ghost is full: the
 *  piece is counted in `dropped` instead. */
bool
MapEditorPieceGhost_Add(
    struct MapEditorPieceGhost* ghost,
    struct MapEditorPieceGhostPiece const* piece);

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
    /** The multi-piece ghost (a stamp, a gizmo drag). */
    struct MapEditorPieceGhost pieces;
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

/* ---- the wander preview ----------------------------------------------------
 *
 * How far an npc roams from where it spawns, drawn on the ground: the
 * WANDER square (every tile within `wanderrange` of the spawn, the npc's own
 * footprint grown by that much on each side -- the server's roam picks a
 * destination within wanderrange of the spawn coord on both axes) and the
 * LEASH square (`maxrange`, how far it follows before it gives up and walks
 * home) as a fainter outline. Values are the record's server fields; a
 * record that states none gets the engine's built-in 5 and 7
 * (torirs_server_content.c npc_def_builtin).
 *
 * Shown for the spawn ghost under the pointer, for every selected npc spawn
 * in the Spawns panel, and at a pending Move's destination. The rectangles
 * are ABSOLUTE world tiles, inclusive; the App turns them into scene tiles
 * and draws them (app_overlay.c).
 */

#define MAP_EDITOR_WANDER_DEFAULT 5
#define MAP_EDITOR_LEASH_DEFAULT 7
/** A range past this is clamped for drawing (a square is 2r+1 tiles). */
#define MAP_EDITOR_WANDER_RANGE_MAX 64
/** Rectangles one frame draws at most: 64 selected spawns, each with its
 *  wander and leash squares, plus a move destination's and a ghost's. */
#define MAP_EDITOR_WANDER_RECTS_MAX 200

enum MapEditorWanderKind
{
    /** The tiles the npc roams over: translucent fill and an outline. */
    MAP_EDITOR_WANDER_AREA = 0,
    /** The leash: an outline only, fainter. */
    MAP_EDITOR_WANDER_LEASH = 1,
    /** Where a pending move puts the spawn itself (its footprint). */
    MAP_EDITOR_WANDER_FOOTPRINT = 2,
};

struct MapEditorWanderRect
{
    enum MapEditorWanderKind kind;
    int x0;
    int z0;
    int x1;
    int z1;
    int level;
};

/**
 * The squares of one npc standing at (x, z, level) with a `size` footprint:
 * its wander area (`wanderrange` < 0 means the default 5; 0 draws no area,
 * the npc does not roam) and its leash (`maxrange` < 0: the default 7; a
 * leash no larger than the wander area is not drawn). Writes at most `max`
 * and returns the count.
 */
int
MapEditorWander_Rects(
    int x,
    int z,
    int level,
    int size,
    int wanderrange,
    int maxrange,
    struct MapEditorWanderRect* out,
    int max);

/** Tiles inside a rectangle (what a test counts). */
int
MapEditorWander_RectTiles(struct MapEditorWanderRect const* rect);

#endif /* SRC_EDITOR_MAP_EDITOR_GHOST_H */
