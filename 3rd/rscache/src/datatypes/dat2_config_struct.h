#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_STRUCT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_STRUCT_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"

/**
 * The fields a struct stream can state, for `RSCache_Dat2ConfigStruct.present`.
 * A struct is nothing but its param map, so there is one.
 */
enum RSCache_Dat2ConfigStructField
{
    RSCACHE_STRUCT_FIELD_PARAMS = 0, /* 249 */
    RSCACHE_STRUCT_FIELD_COUNT
};

struct RSCache_Dat2ConfigStruct
{
    /** Which `RSCache_Dat2ConfigStructField`s the stream stated. The encoder
     *  writes exactly these: a map stated with no entries is opcode 249 with
     *  count 0, and a record that states nothing is the bare terminator. */
    struct RSCache_Presence present;
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

/** Encode a struct record — opcode 249 param map when `present` states it, then
 *  the terminator. Returns bytes written. */
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
