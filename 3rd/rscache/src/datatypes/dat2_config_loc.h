#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_LOC_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_LOC_H

#include "../rsbuffer.h"
#include "../rscache_profile.h"
#include "dat2_configs.h"

#include <stdbool.h>

enum RSCache_Dat2LocShape
{
    RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE = 0,
    RSCACHE_LOC_SHAPE_WALL_TRI_CORNER = 1,
    RSCACHE_LOC_SHAPE_WALL_TWO_SIDES = 2,
    RSCACHE_LOC_SHAPE_WALL_RECT_CORNER = 3,

    // Inside decor is not moved by wall offset.
    RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE = 4,
    // Outside decor is moved by wall offset.
    RSCACHE_LOC_SHAPE_WALL_DECOR_OUTSIDE = 5,
    RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE = 6,
    RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE = 7,
    RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE = 8,

    RSCACHE_LOC_SHAPE_WALL_DIAGONAL = 9,

    RSCACHE_LOC_SHAPE_SCENERY = 10,
    RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL = 11,

    RSCACHE_LOC_SHAPE_ROOF_SLOPED = 12,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER = 13,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_INNER_CORNER = 14,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER = 15,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER = 16,
    RSCACHE_LOC_SHAPE_ROOF_FLAT = 17,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG = 18,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER = 19,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER = 20,
    RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER = 21,

    RSCACHE_LOC_SHAPE_FLOOR_DECORATION = 22,
};

enum RSCache_Dat2LocParamType
{
    RSCACHE_LOC_PARAM_TYPE_INT = 0,
    RSCACHE_LOC_PARAM_TYPE_STRING = 1,
};

struct RSCache_Dat2ConfigLoc
{
    // Added after loading.
    int _id;

    /**
     * Sometimes multiple models are specified in a single loc config,
     * and the shape_select field of the map loc selects which one to use.
     * E.g. Walls will have multiple angles.
     */
    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;

    // Null terminated strings
    char* name;
    char* desc;

    int size_x;
    int size_z;

    // Block walk can be 0, 1, or 2.
    // 2 is the default.
    // For ground-decor locs, 2 is NOT blocked walk.
    // For other locs, != 0 is blocked walk.
    // The '2' special case basically changes the default for ground-decor locs.
    int blocks_walk;
    int blocks_projectiles;

    // Both x and z.
    // LostCity/2004Scape/Pazaz-Gang call this wallwidth.
    int wall_width;

    // If this is true, then we have to do mouse interaction checks
    // The player can right click on the loc etc.
    int is_interactive;

    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;

    // This is merge_normals in rs map viewer
    // If this is true, normals of locs that share a point in space
    // are merged into a single normal.
    int sharelight;

    int occlude;

    // Animation
    int seq_id;

    // Lighting
    int ambient;
    int contrast;

    // Menu operations - null terminated strings
    char* actions[10];

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    // Map function id is the sprite that appears on the world map.
    int map_function_id;
    int mirrored;
    int shadowed;

    int resize_x;
    int resize_height;
    int resize_z;

    int map_scene_id;
    int offset_x;
    int offset_y;
    int offset_z;

    int obstructs_ground;
    int break_routefinding;

    // If true, items on the same tile as are raised from the
    // ground height. For example, to appear as if they're sitting
    // on a table.
    int support_items;

    int transform_varbit;
    int transform_varp;
    // These are the ids of other locs.
    // The transform varbit or transform varp are required to know which of these
    // locs to use in place of this loc.
    int* transforms;
    int transform_count;

    int ambient_sound_id;
    int ambient_sound_distance;
    int ambient_sound_retain;

    int ambient_sound_ticks_min;
    int ambient_sound_ticks_max;

    int* ambient_sound_ids;
    int ambient_sound_id_count;

    bool seq_random_start;

    int* random_seq_ids;
    int* random_seq_delays;
    int random_seq_id_count;

    int* campaign_ids;
    int campaign_id_count;

    struct RSCache_Params params;

    /** Bytes consumed by the last decode (diagnostic: exact-consumption
     * scans compare this against the file size to detect misalignment). */
    int _consumed;
};

#define RSCACHE_CONFIG_LOC_DECODE_DAT2 0
#define RSCACHE_CONFIG_LOC_DECODE_DAT 1
#define RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS 2
/** Kronos client: opcode 78/79 omit ambient_sound_retain G1 after distance. */
#define RSCACHE_CONFIG_LOC_DECODE_KRONOS 4
/** OSRS rev >= 220 payloads: opcode 93 is sound fades (not contour), 95/96
 * carry a byte, etc. (xrsps LocType.ts cacheInfo.revision >= 220 branches). */
#define RSCACHE_CONFIG_LOC_DECODE_OSRS_220 8

/** Era payload flags for this cache. The canonical entry point: it is the only
 *  thing that can set RSCACHE_CONFIG_LOC_DECODE_KRONOS, since that quirk is a
 *  client-build difference no revision comparison implies. */
int
RSCache_Dat2ConfigLocFlags(const struct RSCache* cache);

/** Era payload flags from a loc config group's js5 archive revision alone.
 *  Equivalent to RSCache_Dat2ConfigLocFlags on a profile that knows nothing but
 *  that revision — so it cannot yield the KRONOS or DAT flags. */
int
RSCache_Dat2ConfigLocFlagsForRevision(int revision);

/** revision is the js5 archive revision of the loc config group; it selects
 * era payload flags (mirrors RSCache_Dat2ConfigNpcNewDecode). */
struct RSCache_Dat2ConfigLoc*
RSCache_Dat2ConfigLocNewDecode(
    int revision,
    char* buffer,
    int buffer_size);

/**
 * Decode with the flags a cache profile selects.
 *
 * Prefer this to the `revision` form. A bare archive revision cannot express two
 * things the profile can: the container (a dat1 record has a different string
 * terminator and narrower ids) and a client quirk such as RSCACHE_QUIRK_KRONOS,
 * which no revision number implies because it is a build difference.
 */
struct RSCache_Dat2ConfigLoc*
RSCache_Dat2ConfigLocNewDecodeProfile(
    const struct RSCache* cache,
    char* buffer,
    int buffer_size);
/**
 * Encode a loc record for this cache.
 *
 * The era flags change the *encoding*, not just the decoding: the string
 * terminator follows the container, model ids widen with
 * LARGE_MODEL_IDS, contour opcodes 93 and 95 are unavailable once the >= 220
 * payloads apply (they mean sound fades there), and the Kronos quirk drops the
 * ambient-sound retain byte. Encoding with the wrong profile produces a record the
 * target client misreads.
 *
 * Fields that cannot be reproduced, so such records round-trip semantically but not
 * byte-exactly:
 *   - roughly 25 opcodes the decoder consumes without storing (25, 44, 45, 61, 69,
 *     88/90/91/96..105, 163..191, and the boolean-flag block);
 *   - actions 0..4, which the decoder accepts through either opcode 30+i or 150+i
 *     and stores in the same slots — this writes the 30-range;
 *   - map_function_id (opcodes 60, 82, 107) and map_scene_id (68, 102), likewise
 *     collapsed to the lowest opcode of each group;
 *   - an action of literal "hidden", normalised to NULL by the decoder;
 *   - opcode 95's payload pre-220, which the decoder discards.
 */
uint32_t
RSCache_Dat2ConfigLocEncode(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigLoc* loc,
    uint8_t* out,
    uint32_t out_capacity);

/** As RSCache_Dat2ConfigLocEncode, for callers holding raw
 *  RSCACHE_CONFIG_LOC_DECODE_* flags rather than a profile. */
uint32_t
RSCache_Dat2ConfigLocEncodeFlags(
    const struct RSCache_Dat2ConfigLoc* loc,
    int flags,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigLocFree(struct RSCache_Dat2ConfigLoc* loc);
void
RSCache_Dat2ConfigLocFreeInplace(struct RSCache_Dat2ConfigLoc* loc);

void
RSCache_Dat2ConfigLocDecodeInplace(
    struct RSCache_Dat2ConfigLoc* loc,
    char* buffer,
    int buffer_size,
    int flags);

#endif
