#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_OBJ_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_OBJ_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"
#include "../rscache_profile.h"
#include "dat2_configs.h"
#include "dat2_entity_ops.h"

#include <stdbool.h>

/**
 * The fields an obj stream can state, for `RSCache_Dat2ConfigObj.present`.
 *
 * Keyed by meaning, not opcode: a worn model is opcode 23 or, from rev 237, the
 * int-id 45, and both set MANWEAR; the RS2 codecs number several of these
 * differently again. Families of separate opcodes (ops 30-34, interface ops
 * 35-39, the stack variants 100-109) are one field per opcode, and opcode 43
 * (a sub-op list) is one field per ground op it names, because each is stated
 * independently. Opcodes a decoder consumes without storing (9, the RS2 extras)
 * have no field: the type stays lossy for them.
 */
enum RSCache_Dat2ConfigObjField
{
    RSCACHE_OBJ_FIELD_MODEL = 0,          /* 1, 44 */
    RSCACHE_OBJ_FIELD_NAME,               /* 2 */
    RSCACHE_OBJ_FIELD_DESC,               /* 3 */
    RSCACHE_OBJ_FIELD_ZOOM2D,             /* 4 */
    RSCACHE_OBJ_FIELD_XAN2D,              /* 5 */
    RSCACHE_OBJ_FIELD_YAN2D,              /* 6 */
    RSCACHE_OBJ_FIELD_XOF2D,              /* 7 */
    RSCACHE_OBJ_FIELD_YOF2D,              /* 8 */
    RSCACHE_OBJ_FIELD_STACKABLE,          /* 11 (=1), 160 (=2); RS2 0xA5 (=0) */
    RSCACHE_OBJ_FIELD_COST,               /* 12 */
    RSCACHE_OBJ_FIELD_WEARPOS,            /* 13 */
    RSCACHE_OBJ_FIELD_WEARPOS2,           /* 14 */
    RSCACHE_OBJ_FIELD_UNTRADEABLE,        /* 15, rev 238+ */
    RSCACHE_OBJ_FIELD_MEMBERS,            /* 16 */
    RSCACHE_OBJ_FIELD_MANWEAR,            /* 23, 45: model + offset */
    RSCACHE_OBJ_FIELD_MANWEAR2,           /* 24, 46 */
    RSCACHE_OBJ_FIELD_WOMANWEAR,          /* 25, 48: model + offset */
    RSCACHE_OBJ_FIELD_WOMANWEAR2,         /* 26, 49 */
    RSCACHE_OBJ_FIELD_WEARPOS3,           /* 27 */
    RSCACHE_OBJ_FIELD_OP1,                /* 30 .. OP5 = 34, consecutive */
    RSCACHE_OBJ_FIELD_OP2,
    RSCACHE_OBJ_FIELD_OP3,
    RSCACHE_OBJ_FIELD_OP4,
    RSCACHE_OBJ_FIELD_OP5,
    RSCACHE_OBJ_FIELD_IOP1,               /* 35 .. IOP5 = 39, consecutive */
    RSCACHE_OBJ_FIELD_IOP2,
    RSCACHE_OBJ_FIELD_IOP3,
    RSCACHE_OBJ_FIELD_IOP4,
    RSCACHE_OBJ_FIELD_IOP5,
    RSCACHE_OBJ_FIELD_RECOL,              /* 40 */
    RSCACHE_OBJ_FIELD_RETEX,              /* 41 */
    RSCACHE_OBJ_FIELD_SHIFT_CLICK_DROP,   /* 42 (RS2: 134 / 0x86) */
    RSCACHE_OBJ_FIELD_SUBOP1,             /* 43 naming ground op 1 .. SUBOP5, consecutive */
    RSCACHE_OBJ_FIELD_SUBOP2,
    RSCACHE_OBJ_FIELD_SUBOP3,
    RSCACHE_OBJ_FIELD_SUBOP4,
    RSCACHE_OBJ_FIELD_SUBOP5,
    RSCACHE_OBJ_FIELD_MANWEAR3,           /* 78, 47 */
    RSCACHE_OBJ_FIELD_WOMANWEAR3,         /* 79, 50 */
    RSCACHE_OBJ_FIELD_MANHEAD,            /* 90, 51 */
    RSCACHE_OBJ_FIELD_WOMANHEAD,          /* 91, 53 */
    RSCACHE_OBJ_FIELD_MANHEAD2,           /* 92, 52 */
    RSCACHE_OBJ_FIELD_WOMANHEAD2,         /* 93, 54 */
    RSCACHE_OBJ_FIELD_GE_TRADEABLE,       /* 65 */
    RSCACHE_OBJ_FIELD_WEIGHT,             /* 75 */
    RSCACHE_OBJ_FIELD_CATEGORY,           /* 94 */
    RSCACHE_OBJ_FIELD_ZAN2D,              /* 95 */
    RSCACHE_OBJ_FIELD_CERTLINK,           /* 97 */
    RSCACHE_OBJ_FIELD_CERTTEMPLATE,       /* 98 */
    RSCACHE_OBJ_FIELD_COUNTOBJ1,          /* 100 .. COUNTOBJ10 = 109, consecutive */
    RSCACHE_OBJ_FIELD_COUNTOBJ2,
    RSCACHE_OBJ_FIELD_COUNTOBJ3,
    RSCACHE_OBJ_FIELD_COUNTOBJ4,
    RSCACHE_OBJ_FIELD_COUNTOBJ5,
    RSCACHE_OBJ_FIELD_COUNTOBJ6,
    RSCACHE_OBJ_FIELD_COUNTOBJ7,
    RSCACHE_OBJ_FIELD_COUNTOBJ8,
    RSCACHE_OBJ_FIELD_COUNTOBJ9,
    RSCACHE_OBJ_FIELD_COUNTOBJ10,
    RSCACHE_OBJ_FIELD_RESIZEX,            /* 110 */
    RSCACHE_OBJ_FIELD_RESIZEY,            /* 111 */
    RSCACHE_OBJ_FIELD_RESIZEZ,            /* 112 */
    RSCACHE_OBJ_FIELD_AMBIENT,            /* 113 */
    RSCACHE_OBJ_FIELD_CONTRAST,           /* 114 */
    RSCACHE_OBJ_FIELD_TEAM,               /* 115 */
    RSCACHE_OBJ_FIELD_BOUGHTLINK,         /* 139 */
    RSCACHE_OBJ_FIELD_BOUGHTTEMPLATE,     /* 140 */
    RSCACHE_OBJ_FIELD_PLACEHOLDERLINK,    /* 148 */
    RSCACHE_OBJ_FIELD_PLACEHOLDERTEMPLATE,/* 149 */
    RSCACHE_OBJ_FIELD_ENTITY_SUB_OPS,     /* 200, rev 237+ */
    RSCACHE_OBJ_FIELD_ENTITY_COND_OPS,    /* 201, rev 237+ */
    RSCACHE_OBJ_FIELD_ENTITY_COND_SUB_OPS,/* 202, rev 237+ */
    RSCACHE_OBJ_FIELD_PARAMS,             /* 249 */
    /* RS2 only: decoded and stored, but the OldSchool stream cannot carry them. */
    RSCACHE_OBJ_FIELD_RS2_ITEM_TYPE,      /* 96 */
    RSCACHE_OBJ_FIELD_RS2_LEND,           /* 121 */
    RSCACHE_OBJ_FIELD_RS2_LEND_TEMPLATE,  /* 122 */
    RSCACHE_OBJ_FIELD_COUNT
};

struct RSCache_Dat2ConfigObj
{
    int _id;
    /** Bytes consumed by the last decode, including opcode 0 on success. */
    int _consumed;

    // Null terminated strings
    char* name;
    char* examine;

    int resize_x;
    int resize_y;
    int resize_z;

    int xan2d;
    int yan2d;
    int zan2d;

    // Alchemy value
    int cost;
    /**
     * Whether the item can be traded between players. Defaults true; opcode 15
     * (rev 238+) clears it. Distinct from ge_tradeable.
     */
    bool tradeable;
    /** Grand Exchange tradeable. Opcode 65. Pre-238 this was the only "tradeable" flag. */
    bool ge_tradeable;

    int stacking_behaviour;
    // Also the ground model id.
    int inventory_model_id;
    int wearpos_1;
    int wearpos_2;
    int wearpos_3;

    bool is_members;

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    int zoom2d;
    int offset_x2d;
    int offset_y2d;

    int ambient;
    int contrast;

    // ??
    int count_co[10];
    int count_obj[10];

    /** Ground menu ops (opcodes 30-34). An op the stream spells "hidden" (any
     *  case) reads NULL here, as the client treats it; its spelling is kept in
     *  `hidden_actions` so the record can be written back as it was. */
    char* actions[5];
    /** The literal of a hidden ground op, NULL unless `actions[i]` was hidden. */
    char* hidden_actions[5];
    char** sub_actions[5];
    char* if_actions[5];

    /** Rev 237+: ground EntityOps sub/cond forms (opcodes 200-202). */
    struct RSCache_EntityOps entity_ops;

    int male_model_0;
    int male_model_1;
    int male_model_2;
    int male_offset;
    int male_head_model;
    int male_head_model_2;

    int female_model_0;
    int female_model_1;
    int female_model_2;
    int female_offset;
    int female_head_model;
    int female_head_model_2;

    int category;
    int noted_id;
    int noted_template;
    int team;
    int weight;
    int shift_click_drop_index;
    int bought_id;
    int bought_template_id;

    /** RS2 rev-530 opcode 96 and lending links (121/122). */
    int item_type;
    int lend_id;
    int lend_template_id;

    int placeholder_id;
    int placeholder_template_id;

    struct RSCache_Params params;

    /** Which fields the stream stated (RSCACHE_OBJ_FIELD_*). Values above still
     *  carry the client defaults for the rest. */
    struct RSCache_Presence present;
};

/** Rev 237+: opcodes 200/201/202 EntityOps on groundOps. */
#define RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS 1
/** Rev 237+: opcodes 44-54 int model ids. */
#define RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS 2
/** Rev 238+: opcode 15 tradeable=false; opcode 65 is ge_tradeable. */
#define RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE 4
/** Rev 239+: opcode 160 stackable = 2. */
#define RSCACHE_CONFIG_OBJ_DECODE_REV239_STACKABLE2 8

/**
 * The RS2 build-670+ branch (rev 727 and its neighbours).
 *
 * Every model field became **varuint** — two bytes, or four when the top bit of
 * the first is set — so an inventory model or a worn model reads at the right
 * length only by accident and the rest of the record lands wrong. Opcodes 0x17
 * and 0x19 also dropped the trailing type byte at build 502, 0x2A and 0x2B were
 * reused for other data, and around thirty opcodes have no counterpart in the
 * 643 table.
 *
 * The same opcode number meaning a different structure is what makes this a
 * codec version rather than a flag on the existing body — the rule stated in
 * dat2_config_loc.h.
 */
#define RSCACHE_CONFIG_OBJ_DECODE_RS2_BUILD670 16
/** RS2 rev 530 exact item opcode stream. */
#define RSCACHE_CONFIG_OBJ_DECODE_RS2_530 32

/**
 * The RS2 rev-634 item stream (rev 634 and 643).
 *
 * The 530 table plus opcodes 18, 132 and 134, transcribed from the rev-634
 * client's own decoder. See obj_decode_op_rs2_634 for the reading and for why
 * the build-670 body is not it.
 */
#define RSCACHE_CONFIG_OBJ_DECODE_RS2_634 64

/*
 * Codec versions. A field that merely got wider is absorbed by
 * RSCache_Dat2ConfigObjFlags; a different stream shape gets a version here, and
 * a revision module pins it (see rev_dat2_rs727.c).
 */
#define RSCACHE_CODEC_OBJ_DEFAULT 1
/** RS2 build 670+: varuint model ids and the later opcode set. */
#define RSCACHE_CODEC_OBJ_RS2_BUILD670 2
/** RS2 rev 530: bare 23/25 model ids plus opcodes 96 and 121-130. */
#define RSCACHE_CODEC_OBJ_RS2_530 3
/** RS2 rev 634/643: the 530 table plus opcodes 18, 132 and 134. */
#define RSCACHE_CODEC_OBJ_RS2_634 4

/** Which obj codec this cache uses. */
int
RSCache_Dat2ConfigObjCodecVersion(const struct RSCache* cache);

int
RSCache_Dat2ConfigObjFlags(const struct RSCache* cache);

/**
 * Encode an obj (item) record.
 *
 * Writes exactly the fields set in `present`, whatever their values, in a fixed
 * opcode order. A caller that builds a record by hand states its fields with
 * RSCache_PresenceSet. Model ids use the int forms (44-54) whenever the codec
 * has them (rev 237+), which is how every rev-237+ record is packed.
 *
 * Not reproduced byte-exactly: opcode 9 (a string the decoder discards), an
 * opcode stated twice (only the last value is kept), and the stream's own
 * opcode order.
 */
uint32_t
RSCache_Dat2ConfigObjEncode(
    const struct RSCache_Dat2ConfigObj* object,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigObjEncodeFlags(
    const struct RSCache_Dat2ConfigObj* object,
    int flags,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigObjEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigObj* object,
    uint8_t* out,
    uint32_t out_capacity);

struct RSCache_Dat2ConfigObj*
RSCache_Dat2ConfigObjNewDecode(
    char* buffer,
    int buffer_size);

struct RSCache_Dat2ConfigObj*
RSCache_Dat2ConfigObjNewDecodeProfile(
    const struct RSCache* cache,
    char* buffer,
    int buffer_size);

/**
 * Set the type's defaults on a record. Must run before any DecodeOp.
 *
 * Allocates the default name, so a record it has touched must be released even
 * if nothing was decoded into it.
 */
void
RSCache_Dat2ConfigObjInit(struct RSCache_Dat2ConfigObj* object);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point a server-side obj record delegates through. See
 * `opcode_codec.h`. `flags` is not optional: opcodes 160 and 200-202 exist only
 * from rev 237/239 and are refused below that.
 */
bool
RSCache_Dat2ConfigObjDecodeOp(
    struct RSCache_Dat2ConfigObj* object,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Release what the record owns, leaving the struct to the caller. Split out of
 *  `Free` so a caller holding the record by value can release it without the
 *  double free `Free` would cause. */
void
RSCache_Dat2ConfigObjFreeInplace(struct RSCache_Dat2ConfigObj* object);

/** An upper bound on what `RSCache_Dat2ConfigObjEncodeProfile` will write. */
uint32_t
RSCache_Dat2ConfigObjEncodeBound(const struct RSCache_Dat2ConfigObj* object);

void
RSCache_Dat2ConfigObjFree(struct RSCache_Dat2ConfigObj* object);

void
RSCache_Dat2ConfigObjDecodeInplace(
    struct RSCache_Dat2ConfigObj* object,
    char* buffer,
    int buffer_size);

void
RSCache_Dat2ConfigObjDecodeInplaceFlags(
    struct RSCache_Dat2ConfigObj* object,
    char* buffer,
    int buffer_size,
    int flags);

#endif
