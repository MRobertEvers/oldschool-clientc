#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_LOC_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_LOC_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"
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

/**
 * The fields a loc stream can state, for `RSCache_Dat2ConfigLoc.present`.
 *
 * Keyed by meaning, not opcode: where an era moved a field to another opcode
 * (model lists became 6/7 with int ids at rev 237, the map function is 60 in
 * dat1, 82 in OldSchool and 107 in RS2, the map scene is 102 in RS2), every
 * decoder maps whichever opcode it reads onto the one field and the encoder
 * writes the current codec's spelling. Distinct opcodes that set the same
 * struct value with a different meaning are distinct fields: 17 / 18 / 27 all
 * touch the walk and projectile flags, 77 and 92 both carry the transforms, and
 * 21 / 81 / 93 / 94 / 95 each select a contour mode.
 *
 * The opcodes the decoder consumes without storing (25, 42, 44, 45; RS2's 82,
 * 88, 90, 91, 97..99, 103..105 and 163..191; a pre-237 100 or 101; a pre-220
 * 91 or 96) have no field: the type stays lossy for them.
 */
enum RSCache_Dat2ConfigLocField
{
    RSCACHE_LOC_FIELD_MODELS = 0,            /* 1, rev 237+ 6; RS2 nested 1 and 5 */
    RSCACHE_LOC_FIELD_MODELS_FLAT,           /* 5, rev 237+ 7 */
    RSCACHE_LOC_FIELD_NAME,                  /* 2 */
    RSCACHE_LOC_FIELD_DESC,                  /* 3 */
    RSCACHE_LOC_FIELD_SIZE_X,                /* 14 */
    RSCACHE_LOC_FIELD_SIZE_Z,                /* 15 */
    RSCACHE_LOC_FIELD_UNSOLID,               /* 17: walk and projectiles pass */
    RSCACHE_LOC_FIELD_PROJECTILES_PASS,      /* 18 */
    RSCACHE_LOC_FIELD_INTERACTIVE,           /* 19 */
    RSCACHE_LOC_FIELD_CONTOUR_GROUND,        /* 21: contour type 1 */
    RSCACHE_LOC_FIELD_SHARELIGHT,            /* 22 */
    RSCACHE_LOC_FIELD_OCCLUDE,               /* 23 */
    RSCACHE_LOC_FIELD_ANIM,                  /* 24 */
    RSCACHE_LOC_FIELD_INTERACT_TYPE_1,       /* 27 */
    RSCACHE_LOC_FIELD_WALL_WIDTH,            /* 28 */
    RSCACHE_LOC_FIELD_AMBIENT,               /* 29 */
    RSCACHE_LOC_FIELD_OP1,                   /* 30 + n (RS2 also 150 + n, n < 5) */
    RSCACHE_LOC_FIELD_OP2,                   /* 31 */
    RSCACHE_LOC_FIELD_OP3,                   /* 32 */
    RSCACHE_LOC_FIELD_OP4,                   /* 33 */
    RSCACHE_LOC_FIELD_OP5,                   /* 34 */
    RSCACHE_LOC_FIELD_OP6,                   /* 35 */
    RSCACHE_LOC_FIELD_OP7,                   /* 36 */
    RSCACHE_LOC_FIELD_OP8,                   /* 37 */
    RSCACHE_LOC_FIELD_OP9,                   /* 38 */
    RSCACHE_LOC_FIELD_CONTRAST,              /* 39 */
    RSCACHE_LOC_FIELD_RECOLOURS,             /* 40 */
    RSCACHE_LOC_FIELD_RETEXTURES,            /* 41 */
    RSCACHE_LOC_FIELD_MAP_FUNCTION,          /* dat1 60, OldSchool 82, RS2 107 */
    RSCACHE_LOC_FIELD_CATEGORY,              /* 61 */
    RSCACHE_LOC_FIELD_MIRROR,                /* 62 */
    RSCACHE_LOC_FIELD_NO_SHADOW,             /* 64 */
    RSCACHE_LOC_FIELD_RESIZE_X,              /* 65 */
    RSCACHE_LOC_FIELD_RESIZE_HEIGHT,         /* 66 */
    RSCACHE_LOC_FIELD_RESIZE_Z,              /* 67 */
    RSCACHE_LOC_FIELD_MAP_SCENE,             /* 68; RS2 and pre-237 102 */
    RSCACHE_LOC_FIELD_FORCE_APPROACH,        /* 69 */
    RSCACHE_LOC_FIELD_OFFSET_X,              /* 70 */
    RSCACHE_LOC_FIELD_OFFSET_Y,              /* 71 */
    RSCACHE_LOC_FIELD_OFFSET_Z,              /* 72 */
    RSCACHE_LOC_FIELD_FORCE_DECOR,           /* 73 */
    RSCACHE_LOC_FIELD_BREAK_ROUTEFINDING,    /* 74 */
    RSCACHE_LOC_FIELD_RAISE_OBJECT,          /* 75 */
    RSCACHE_LOC_FIELD_MULTI,                 /* 77 */
    RSCACHE_LOC_FIELD_MULTI_DEFAULT,         /* 92: 77 plus a default loc */
    RSCACHE_LOC_FIELD_SOUND,                 /* 78 */
    RSCACHE_LOC_FIELD_SOUND_RANDOM,          /* 79 */
    RSCACHE_LOC_FIELD_CONTOUR_GROUND_HEIGHT, /* 81: contour type 2 */
    RSCACHE_LOC_FIELD_NO_RANDOM_ANIM_START,  /* 89 */
    RSCACHE_LOC_FIELD_DEFER_ANIM_CHANGE,     /* OldSchool 90 */
    RSCACHE_LOC_FIELD_SOUND_DISTANCE_FADE,   /* rev 220+ 91 */
    RSCACHE_LOC_FIELD_SOUND_FADE,            /* rev 220+ 93 */
    RSCACHE_LOC_FIELD_CONTOUR_TYPE_3,        /* pre-220 93 */
    RSCACHE_LOC_FIELD_UNKNOWN1,              /* OldSchool 94 */
    RSCACHE_LOC_FIELD_CONTOUR_TYPE_4,        /* RS2 94 */
    RSCACHE_LOC_FIELD_SOUND_VISIBILITY,      /* rev 220+ 95 */
    RSCACHE_LOC_FIELD_CONTOUR_TYPE_5,        /* pre-220 95 */
    RSCACHE_LOC_FIELD_RAISE,                 /* rev 220+ 96 */
    RSCACHE_LOC_FIELD_SUB_OPS,               /* rev 237+ 100 */
    RSCACHE_LOC_FIELD_COND_OPS,              /* rev 237+ 101 */
    RSCACHE_LOC_FIELD_COND_SUB_OPS,          /* rev 237+ 102 */
    RSCACHE_LOC_FIELD_RANDOM_ANIMS,          /* 106 */
    RSCACHE_LOC_FIELD_CAMPAIGNS,             /* 160 */
    RSCACHE_LOC_FIELD_PARAMS,                /* 249 */
    RSCACHE_LOC_FIELD_COUNT
};

struct RSCache_Dat2ConfigLoc
{
    /** Which `RSCache_Dat2ConfigLocField`s the stream stated. The encoder writes
     *  exactly these; the values below still hold the client defaults (and the
     *  derivations of `RSCache_Dat2ConfigLocFinish`) for the rest. */
    struct RSCache_Presence present;

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
    /**
     * The spelling of an action the stream stated as "hidden" (any case), which
     * `actions` holds as NULL exactly as the client does. Kept only so the
     * encoder can write the record back as it was: cache.osrs239 spells it
     * "hidden" 520 times and "Hidden" 128 times. NULL for every other slot.
     */
    char* hidden_actions[10];

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
    bool defer_anim_change;        /* opcode 90 on OSRS (payload-free) */
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
 * RS2-era opcode meanings that are independent of the model-list layout.  Rev 530
 * already has those meanings, while retaining the earlier flat opcode 1/5 model lists.
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
/**
 * Rev 643/727 nested model lists.  Opcode 1 is `u8 count`, then per entry
 * `u8 shape, u8 model_count, model_count x model-id`; opcode 5 carries two such
 * blocks.  This must not be inferred merely from the RS2 era: rev 530 uses the
 * flat `count x (u16 model, u8 shape)` / `count x u16 model` forms.
 */
#define RSCACHE_CONFIG_LOC_DECODE_RS2_NESTED_MODELS 128
/** Rev 530 alone: opcode 95 selects contour type 5 and has no payload. */
#define RSCACHE_CONFIG_LOC_DECODE_RS2_530 256

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
/**
 * Late pre-EoC RS2 locs retain the nested RS2 model-list shape, but every model,
 * animation and transform reference is a BigSmart.  Treating revision 727 as
 * the older u16 codec happens to decode small records and then loses alignment
 * as soon as a QBD-era model above 32767 appears.
 */
#define RSCACHE_CODEC_LOC_RS2_727 3
/** Rev 530 RS2 opcode semantics with the earlier flat opcode 1/5 model lists. */
#define RSCACHE_CODEC_LOC_RS2_530 4

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
 * It writes exactly the fields `loc->present` states (see
 * RSCache_Dat2ConfigLocField), never inferring one from its value: a field
 * stated with the default's value is written, an unstated one is not. A
 * caller building a record by hand states its fields with RSCache_PresenceSet.
 *
 * Fields that cannot be reproduced, so such records round-trip semantically but not
 * byte-exactly:
 *   - the opcodes the decoder consumes without storing (RS2's 25, 42, 44, 45,
 *     82, 88, 91, 97..99, 103..105, 163..191; a pre-220 91 or 96);
 *   - a field stated twice, of which the decoder keeps the last value;
 *   - RS2 actions 0..4 through 150+i, written back through 30+i; and RS2's
 *     nested model lists, written back in the flat OldSchool form;
 *   - an action "hidden" held as NULL with no `hidden_actions` spelling (a
 *     record built by hand), which this writes as "Hidden".
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
