#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H

#include <stdint.h>

struct RSCache_Dat2ConfigEnum
{
    int id;
    int output_is_string; /* bool as int */
    int default_int;
    int64_t default_long;
    char* default_string;
    int* keys;
    int* int_values;
    int64_t* long_values;
    char** string_values;
    int count;
    /** Bytes consumed. Equal to the record size for a fully understood record.
     *
     *  Added with the stop-on-unknown fix: this decoder skipped opcodes it did
     *  not know, so it had no way to report that it had lost the thread. */
    int _consumed;
};

void
RSCache_Dat2ConfigEnumDecodeInplace(
    struct RSCache_Dat2ConfigEnum* entry,
    const void* data,
    int data_size);

/**
 * Encode an enum record.
 *
 * Not byte-exact for records carrying opcode 1 — the decoder consumes it without
 * keeping the key type, so that information is already gone. Semantically exact
 * otherwise (including long maps via opcodes 7/8).
 */
uint32_t
RSCache_Dat2ConfigEnumEncode(
    const struct RSCache_Dat2ConfigEnum* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigEnumFree(struct RSCache_Dat2ConfigEnum* entry);

void
RSCache_Dat2ConfigEnumFreeInplace(struct RSCache_Dat2ConfigEnum* entry);

/** An upper bound on what `RSCache_Dat2ConfigEnumEncode` will write. */
uint32_t
RSCache_Dat2ConfigEnumEncodeBound(const struct RSCache_Dat2ConfigEnum* entry);

#endif
