#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_PARAM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_PARAM_H

#include "../rsbuffer.h"

struct RSCache_Dat2ConfigParam
{
    int id;
    char type;
    int default_int;
    long long default_long;
    int auto_disable;
    char* default_string;
    /** Bytes consumed. Equal to the record size for a fully understood record.
     *
     *  Added with the per-opcode split: this decoder used to skip opcodes it did
     *  not know and carry on, so it had no way to report that it had lost the
     *  thread. It now stops, and this is the signal — the same convention the
     *  rest of the library already follows. */
    int _consumed;
};

/**
 * Set the type's non-zero defaults on an already-zeroed record.
 *
 * Must run before any `DecodeOp` call: opcode 4 *clears* `auto_disable`, so its
 * default is 1 and a merely-zeroed record reads as off for every param.
 */
void
RSCache_Dat2ConfigParamInit(struct RSCache_Dat2ConfigParam* entry);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigParamDecodeOp(
    struct RSCache_Dat2ConfigParam* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

void
RSCache_Dat2ConfigParamDecodeInplace(
    struct RSCache_Dat2ConfigParam* entry,
    const void* data,
    int data_size);

/**
 * Encode a param record.
 *
 * Always writes the type through opcode 1. A record whose type arrived via
 * opcode 8 (numeric type id) therefore re-encodes to different bytes with the
 * same meaning — which opcode carried it is not retained by the decoder.
 */
uint32_t
RSCache_Dat2ConfigParamEncode(
    const struct RSCache_Dat2ConfigParam* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigParamFree(struct RSCache_Dat2ConfigParam* entry);

void
RSCache_Dat2ConfigParamFreeInplace(struct RSCache_Dat2ConfigParam* entry);

/** Bytes needed for `entry`, an upper bound. */
uint32_t
RSCache_Dat2ConfigParamEncodeBound(const struct RSCache_Dat2ConfigParam* entry);

#endif
