#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_IDK_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_IDK_H

#include "../rsbuffer.h"
#include "dat2_configs.h"

#include <stdbool.h>

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
/** Set the type's non-zero defaults (body part and if-model ids are -1). */
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

/** Encode an identkit record. Returns bytes written, or 0 on failure. */
uint32_t
RSCache_Dat2ConfigIdkEncode(
    const struct RSCache_Dat2ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `RSCache_Dat2ConfigIdkEncode` will write. */
uint32_t
RSCache_Dat2ConfigIdkEncodeBound(const struct RSCache_Dat2ConfigIdk* idk);

#endif