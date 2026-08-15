#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_OBJ_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_OBJ_H

#include "../rsbuffer.h"
#include "../rscache_profile.h"
#include "dat2_configs.h"
#include "dat2_entity_ops.h"

#include <stdbool.h>

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

    /** Ground menu ops (opcodes 30-34). */
    char* actions[5];
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
 * Omits fields still at their decode default and writes opcodes in ascending
 * order, which matches how the packer emits them.
 *
 * Three fields cannot be reproduced, so records using them round-trip
 * semantically but not byte-exactly:
 *   - opcode 9, a string the decoder discards;
 *   - an action of the literal "Hidden", which the decoder normalises to NULL and
 *     is therefore indistinguishable from an absent action;
 *   - a name of the literal "null", indistinguishable from an absent opcode 2.
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
