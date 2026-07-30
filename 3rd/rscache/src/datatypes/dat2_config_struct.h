#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_STRUCT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_STRUCT_H

#include "../rsbuffer.h"

struct RSCache_Dat2ConfigStruct
{
    int id;
    struct RSCache_Params params;
};

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigStructDecodeOp(
    struct RSCache_Dat2ConfigStruct* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

void
RSCache_Dat2ConfigStructDecodeInplace(
    struct RSCache_Dat2ConfigStruct* entry,
    const void* data,
    int data_size);

/** Encode a struct record — opcode 249 param map, then the terminator. Returns
 *  bytes written, or 0 on failure. */
uint32_t
RSCache_Dat2ConfigStructEncode(
    const struct RSCache_Dat2ConfigStruct* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigStructFree(struct RSCache_Dat2ConfigStruct* entry);

void
RSCache_Dat2ConfigStructFreeInplace(struct RSCache_Dat2ConfigStruct* entry);

/** Bytes needed for `entry`, an upper bound. */
uint32_t
RSCache_Dat2ConfigStructEncodeBound(const struct RSCache_Dat2ConfigStruct* entry);

#endif
