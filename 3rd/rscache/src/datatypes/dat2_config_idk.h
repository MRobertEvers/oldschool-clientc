#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_IDK_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_IDK_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"
#include "../rscache_profile.h"
#include "dat2_configs.h"

#include <stdbool.h>

/**
 * The fields an identkit stream can state, for `RSCache_Dat2ConfigIdk.present`.
 *
 * Keyed by meaning: the model list is opcode 2 (u16 ids) or 5 (u32 ids), and
 * if-model slot i is opcode 60+i (u16) or 70+i (u32); each pair decodes to one
 * field. The ten if-model slots are ten opcodes, so ten fields.
 */
enum RSCache_Dat2ConfigIdkField
{
    RSCACHE_IDK_FIELD_BODY_PART = 0,  /* 1 */
    RSCACHE_IDK_FIELD_MODELS,         /* 2 / 5 */
    RSCACHE_IDK_FIELD_NOT_SELECTABLE, /* 3 */
    RSCACHE_IDK_FIELD_RECOLOURS,      /* 40 */
    RSCACHE_IDK_FIELD_RETEXTURES,     /* 41 */
    /** Slot i is RSCACHE_IDK_FIELD_IF_MODEL_FIRST + i, i in 0..9. */
    RSCACHE_IDK_FIELD_IF_MODEL_FIRST, /* 60+i / 70+i */
    RSCACHE_IDK_FIELD_COUNT = RSCACHE_IDK_FIELD_IF_MODEL_FIRST + 10,
};

/**
 * Encode flag: write the model list and the if-models in their u32 forms
 * (opcodes 5 and 70+i) always, as the cache does from OldSchool rev 237 -- every
 * one of cache.osrs239's 307 identkits uses them for ids that fit a u16, where
 * cache.osrs230 uses 2 and 60+i. Without it the narrow form is written whenever
 * the id fits, which is the older caches' shape.
 */
#define RSCACHE_CONFIG_IDK_ENCODE_INT_MODEL_IDS 1

/** Encode flags for this cache. */
unsigned
RSCache_Dat2ConfigIdkFlags(const struct RSCache* cache);

struct RSCache_Dat2ConfigIdk
{
    int _id;

    int body_part_id;
    int* model_ids;
    int model_ids_count;

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    bool is_not_selectable;

    int if_model_ids[10];
    /** Which fields the stream stated (RSCACHE_IDK_FIELD_*). */
    struct RSCache_Presence present;
    /** Bytes consumed. Equal to the record size for a fully understood record.
     *
     *  Added with the per-opcode split: this decoder had no `default` case, so an
     *  unknown opcode was skipped silently and there was no way to detect that it
     *  had lost the thread. */
    int _consumed;
};

struct RSCache_Dat2ConfigIdk*
RSCache_Dat2ConfigIdkNewDecode(
    char* buffer,
    int buffer_size);
/** Zero the record, set the type's non-zero defaults (body part and if-model ids
 *  are -1), and state nothing. */
void
RSCache_Dat2ConfigIdkInit(struct RSCache_Dat2ConfigIdk* idk);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 * See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigIdkDecodeOp(
    struct RSCache_Dat2ConfigIdk* idk,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

void
RSCache_Dat2ConfigIdkFree(struct RSCache_Dat2ConfigIdk* idk);

void
RSCache_Dat2ConfigIdkDecodeInplace(
    struct RSCache_Dat2ConfigIdk* idk,
    char* buffer,
    int buffer_size);

/**
 * Encode an identkit record: exactly the fields `present` states, in the cache's
 * own order (3, 1, 40, 41, the if-models, then the model list). Returns bytes
 * written. `Encode` uses the narrow forms where an id fits (flags 0); `EncodeFlags`
 * and `EncodeProfile` take the era's.
 */
uint32_t
RSCache_Dat2ConfigIdkEncode(
    const struct RSCache_Dat2ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigIdkEncodeFlags(
    const struct RSCache_Dat2ConfigIdk* idk,
    unsigned flags,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigIdkEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `RSCache_Dat2ConfigIdkEncode` will write. */
uint32_t
RSCache_Dat2ConfigIdkEncodeBound(const struct RSCache_Dat2ConfigIdk* idk);

#endif