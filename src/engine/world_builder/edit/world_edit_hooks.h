#ifndef WORLD_EDIT_HOOKS_H
#define WORLD_EDIT_HOOKS_H

/*
 * The EDIT layout's hooks into the shared bake, declared for world_builder.c's
 * translation unit only.
 *
 * world_builder.c includes the bake's fragments (world_terrain.u.c,
 * world_scenery.u.c, ...) and, at its end, edit/world_edit.u.c, which defines
 * these. Declaring them up front is what lets a fragment call into EDIT without
 * the bake depending on EDIT's order in the file, and without a separate object
 * every link line that names world_builder.o would have to learn about.
 *
 * Every call site is guarded by `builder->edit`, which is NULL under STATIC:
 * that one test is the whole cost of EDIT to the game's bake.
 */

#include "engine/torirs_types.h"

struct WorldBuilder;

/** Which shademap writer a captured shadow replays through. */
enum WorldEditShadeKind
{
    WORLD_EDIT_SHADE_WALL = 0,
    WORLD_EDIT_SHADE_WALL_CORNER = 1,
    WORLD_EDIT_SHADE_SIZED = 2,
};

/* A bake is starting; `zone_center_x` < 0 means the chunk-list form. */
static void
world_edit_on_bake_begin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    const int* chunks_xz,
    int chunk_count);

/* The bake ended with the build maps resident (keep_build_maps). */
static void
world_edit_on_bake_end(struct WorldBuilder* builder);

/* The bake is an instance (zone-copy) build: EDIT does not edit those. */
static void
world_edit_on_bake_instance(struct WorldBuilder* builder);

/* One floor the square terrain pass applied at a scene tile. */
static void
world_edit_note_bake_floor(
    struct WorldBuilder* builder,
    const struct ToriRS_MapFloor* floor,
    int scene_x,
    int scene_z,
    int level);

/* The square scenery pass reached the `index`th loc of square (mapx, mapz);
 * the captures that follow belong to it. */
static void
world_edit_bake_placement(
    struct WorldBuilder* builder,
    int mapx,
    int mapz,
    const struct ToriRS_MapLoc* map_loc);

/* What a placement does to the build maps, recorded while it is captured. */
static void
world_edit_capture_shade(
    struct WorldBuilder* builder,
    int kind,
    int x,
    int z,
    int level,
    int a,
    int b,
    int shade);

static void
world_edit_capture_wall_offset(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level,
    int offset);

static void
world_edit_capture_decor(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level,
    int element_id,
    int orientation,
    int displacement_kind);

static void
world_edit_capture_light(
    struct WorldBuilder* builder,
    int sharelight,
    int x,
    int z,
    int level,
    int element_id,
    int size_x,
    int size_z,
    int ambient,
    int contrast);

static void
world_edit_capture_element(
    struct WorldBuilder* builder,
    int element_id);

/* WorldBuilder_ApplyLocChange runs as the game's runtime loc change under EDIT
 * too: these hide the resident maps from it and give them back. */
struct WorldEditStash
{
    void* maps[8];
};

static void
world_edit_stash_maps(
    struct WorldBuilder* builder,
    struct WorldEditStash* stash);

static void
world_edit_unstash_maps(
    struct WorldBuilder* builder,
    const struct WorldEditStash* stash);

#endif
