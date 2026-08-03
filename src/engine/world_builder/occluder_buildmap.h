#ifndef OCCLUDER_BUILDMAP_H
#define OCCLUDER_BUILDMAP_H

/**
 * Build-time mark bitfield for planar occluders (Client-TS ClientBuild.mapo).
 *
 * Three bits per level, packed across 4 levels in a uint16_t:
 *   wall along X (west/east faces), wall along Z (north/south faces), flat floor.
 * Walls and roofs OR the all-levels composites so a single mark covers every
 * topLevel merge pass; floors OR the same way.
 *
 * Freed at the end of WorldBuilder_RebuildCenterzoneEnd after the greedy merge
 * has emitted SceneOccluders planes — same lifetime as shademap / decor_buildmap.
 */

#include <stdint.h>

struct SceneOccluders;
struct Heightmap;

/** wall-along-X (wall0) on every level: 0x001|0x008|0x040|0x200 */
#define OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS 0x249
/** wall-along-Z (wall1) on every level: 0x002|0x010|0x080|0x400 */
#define OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS 0x492
/** floor on every level: 0x004|0x020|0x100|0x800 */
#define OCCLUDER_MARK_FLOOR_ALL_LEVELS 0x924
/**
 * West arm of an L-wall in the reference (ClientBuild.ts:984). This is a Jagex
 * bug: 0x109 = wall0@L0 | floor@L2 | wall0@L2 — not wall0 on all levels.
 * Ported verbatim so our occlusion matches the reference's.
 */
#define OCCLUDER_MARK_WALL_L_WEST_ARM 0x109

struct OccluderBuildmap
{
    /**
     * Packed marks: one uint16_t per (level, x, z). The reference stores a
     * single Int32Array3d and ORs the all-levels composites into one cell;
     * we keep the same layout (level-major) so indexing matches the merge
     * loops that walk level independently of the packed bits.
     *
     * Actually the reference's mapo[level][x][z] holds the FULL 12-bit word
     * on every level (the OR writes the same composite into mapo[level]), so
     * the per-level index is just which physical level the loc sits on — the
     * bits themselves encode which levels the wall covers for the merge.
     */
    uint16_t* marks;
    /**
     * Opacity half of the flat-floor condition, recorded during the terrain
     * chunk pass (underlay/overlay + FloType.hide_underlay). Flatness is
     * applied later in world_build_scene_terrain once all corner heights are
     * known. 1 = candidate, 0 = not. Indexed level * size_x * size_z + ...
     */
    uint8_t* floor_opacity;
    int size_x;
    int size_z;
    int levels;
};

struct OccluderBuildmap*
occluder_buildmap_new(
    int size_x,
    int size_z,
    int levels);

void
occluder_buildmap_free(struct OccluderBuildmap* map);

void
occluder_buildmap_or_mark(
    struct OccluderBuildmap* map,
    int x,
    int z,
    int level,
    uint16_t bits);

void
occluder_buildmap_set_floor_opacity(
    struct OccluderBuildmap* map,
    int x,
    int z,
    int level,
    int opaque);

int
occluder_buildmap_floor_opacity(
    const struct OccluderBuildmap* map,
    int x,
    int z,
    int level);

/**
 * Apply the flatness half of the floor condition: if floor_opacity is set and
 * all four corner heights are equal, OR OCCLUDER_MARK_FLOOR_ALL_LEVELS.
 * Only valid for level > 0 (matching the reference).
 */
void
occluder_buildmap_mark_flat_floor(
    struct OccluderBuildmap* map,
    int x,
    int z,
    int level,
    int height_sw,
    int height_se,
    int height_ne,
    int height_nw);

/**
 * Greedy merge of marked tiles into SceneOccluder planes (Client-TS
 * ClientBuild.finishBuild second half). Clears consumed mark bits.
 * heightmap supplies ground Y; tile size is 128 scene units.
 */
void
occluder_buildmap_build_occluders(
    struct OccluderBuildmap* map,
    struct Heightmap* heightmap,
    struct SceneOccluders* out);

#endif
