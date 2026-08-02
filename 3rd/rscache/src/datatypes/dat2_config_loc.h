#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_LOC_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_LOC_H

#include "../rsbuffer.h"
#include "../rscache_profile.h"
#include "dat2_configs.h"
#include "dat2_entity_ops.h"

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
    /**
     * Opcode 61: the record's category, the same id space `dat2_config_npc.c`
     * decodes at opcode 18 and `dat2_config_obj.c` at 94. 0 is "no category
     * stated" and is the decoder's default, exactly as it is for the other two.
     *
     * It was `g2(buffer); // Skip unsigned short` until 2026-08-02. That it is a
     * *category* rather than some other u16 was not taken on faith: the ids
     * group semantically (684 is 58 records every one of which is a bank booth,
     * 237 is 11 bank chests) and they share a space with the other two domains
     * (the max across npc/obj/loc in cache.osrs239 is 2504/2506/2474, and 9 ids
     * carry both npc and loc members). `tools/loc_category_probe` prints the
     * histogram that argument is made from.
     */
    int category;
    int mirrored;
    int shadowed;

    int resize_x;
    int resize_height;
    int resize_z;

    int map_scene_id;
    /**
     * Opcode 69: the sides this loc may NOT be approached from, as a 4-bit
     * DirectionFlag mask (1 N, 2 E, 4 S, 8 W) in the loc's *unrotated* frame.
     * Consumed by the client's pathfinder approach test (Client-TS
     * CollisionMap.testLoc), which rotates it by the placed angle first:
     *   angle != 0 -> ((fa << angle) & 0xf) + (fa >> (4 - angle))
     * Default 0 = approachable from every side.
     */
    int force_approach;
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

    /* --- OSRS >= 220 sound / raise fields (opcodes 91, 93, 95, 96) --- */
    int sound_distance_fade_curve; /* opcode 91 */
    int sound_fade_in_curve;       /* opcode 93 */
    int sound_fade_in_duration;    /* opcode 93; default 300 */
    int sound_fade_out_curve;      /* opcode 93 */
    int sound_fade_out_duration;   /* opcode 93; default 300 */
    bool unknown1;                 /* opcode 94 on OSRS (payload-free) */
    int sound_visibility;          /* opcode 95; default 2 */
    int raise;                     /* opcode 96 */

    /** Rev 237+: sub-ops / conditional ops. Plain ops still live in `actions`. */
    struct RSCache_EntityOps entity_ops;

    struct RSCache_Params params;

    /** Bytes consumed by the last decode (diagnostic: exact-consumption
     * scans compare this against the file size to detect misalignment). */
    int _consumed;
    /**
     * How many action opcodes (30..38) the decode saw, including any spelled
     * "hidden".
     *
     * Not derivable from `actions[]`: a "hidden" action is counted and then
     * stored as NULL, so counting non-NULL slots undercounts and would leave
     * those locs non-interactive. Loop state that the post-decode fixup needs,
     * so it lives in the record rather than in the decoder's stack frame.
     */
    int _actions_seen;
};

#define RSCACHE_CONFIG_LOC_DECODE_DAT2 0
#define RSCACHE_CONFIG_LOC_DECODE_DAT 1
#define RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS 2
/** Kronos client: opcode 78/79 omit ambient_sound_retain G1 after distance. */
#define RSCACHE_CONFIG_LOC_DECODE_KRONOS 4
/** OSRS rev >= 220 payloads: opcode 93 is sound fades (not contour), 95/96
 * carry a byte, etc. (xrsps LocType.ts cacheInfo.revision >= 220 branches). */
#define RSCACHE_CONFIG_LOC_DECODE_OSRS_220 8
/**
 * RS2 branch (the 643 era): opcodes 1 and 5 carry a *nested* model list.
 *
 * OSRS opcode 1 is `u8 count` then `count x (u16 model, u8 shape)`. RS2 inverts the
 * nesting: `u8 count` then per entry `u8 shape, u8 model_count, model_count x u16`, so one
 * shape owns a list of models rather than each model naming its shape. Opcode 5 is two of
 * those blocks back to back where OSRS has a bare model list.
 *
 * This is the difference that matters, not the handful of extra opcodes: reading an RS2
 * record with the OSRS shape desynchronises inside the very first opcode, and every
 * "unimplemented opcode" reported after that is a data byte being read as an opcode. Only
 * 651 of 2048 records in cache.rs643 survived, and those were the ones whose opcode 1
 * happened to be absent or degenerate.
 *
 * Per void's ObjectDecoder.kt (`skip`, called once for opcode 1 and twice for opcode 5).
 */
#define RSCACHE_CONFIG_LOC_DECODE_RS2 16
/** Rev 237+: opcodes 6/7 int model arrays (g4 instead of g2). */
#define RSCACHE_CONFIG_LOC_DECODE_REV237_INT_MODEL_IDS 32
/** Rev 237+: opcodes 100/101/102 EntityOps (102 is map_scene_id without this). */
#define RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS 64

/* --- codec versions -------------------------------------------------------- */
/*
 * Loc has two *structural* codecs, not one flag-driven codec.
 *
 * The distinction this library draws: field-level differences (a byte appears, a width
 * grows) are absorbed by RSCache_Dat2ConfigLocFlags; a different *stream shape* gets its
 * own codec version. RS2 is the latter — opcodes 1 and 5 invert the model-list nesting, so
 * the same opcode number means a different structure rather than a wider field.
 *
 * A revision module pins the codec explicitly (see rev_dat2_rs643.c); the derivation below
 * is only the fallback for a cache nobody declared.
 */
#define RSCACHE_CODEC_LOC_OSRS 1
#define RSCACHE_CODEC_LOC_RS2 2

/** Which loc codec this cache uses. */
int
RSCache_Dat2ConfigLocCodecVersion(const struct RSCache* cache);

/** Era payload flags for this cache. The canonical entry point: it is the only
 *  thing that can set RSCACHE_CONFIG_LOC_DECODE_KRONOS, since that quirk is a
 *  client-build difference no revision comparison implies. Prefer this to any
 *  archive-revision-only form. */
int
RSCache_Dat2ConfigLocFlags(const struct RSCache* cache);

/**
 * Decode with the flags a cache profile selects.
 *
 * Prefer this to any archive-revision-only form. A bare archive revision cannot
 * express two things the profile can: the container (a dat1 record has a different
 * string terminator and narrower ids) and a client quirk such as RSCACHE_QUIRK_KRONOS,
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
 *   - roughly 25 opcodes the decoder consumes without storing (25, 44, 45, 69,
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

/** An upper bound on what `RSCache_Dat2ConfigLocEncode` will write. */
uint32_t
RSCache_Dat2ConfigLocEncodeBound(const struct RSCache_Dat2ConfigLoc* loc);

void
RSCache_Dat2ConfigLocFree(struct RSCache_Dat2ConfigLoc* loc);
void
RSCache_Dat2ConfigLocFreeInplace(struct RSCache_Dat2ConfigLoc* loc);

/** Set the type's non-zero defaults on a record. Must run before any DecodeOp. */
void
RSCache_Dat2ConfigLocInit(struct RSCache_Dat2ConfigLoc* loc);

/**
 * Fix up the record once its stream is exhausted.
 *
 * `blocks_walk`/`blocks_projectiles` collapse when `break_routefinding` is set,
 * and `is_interactive` is derived from the models, shapes and action count. None
 * of it is knowable opcode by opcode.
 */
void
RSCache_Dat2ConfigLocFinish(struct RSCache_Dat2ConfigLoc* loc, unsigned flags);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point a server-side loc record delegates through. See
 * `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigLocDecodeOp(
    struct RSCache_Dat2ConfigLoc* loc,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

void
RSCache_Dat2ConfigLocDecodeInplace(
    struct RSCache_Dat2ConfigLoc* loc,
    char* buffer,
    int buffer_size,
    int flags);

#endif
