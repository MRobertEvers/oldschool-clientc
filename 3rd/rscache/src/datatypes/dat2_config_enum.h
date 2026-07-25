#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H

#include <stdint.h>

struct RSCache_Dat2ConfigEnum
{
    int id;
    int output_is_string; /* bool as int */
    int default_int;
    char* default_string;
    int* keys;
    int* int_values;
    char** string_values;
    int count;
};

void
RSCache_Dat2ConfigEnumDecodeInplace(
    struct RSCache_Dat2ConfigEnum* entry,
    const void* data,
    int data_size);

/**
 * Encode an enum record.
 *
 * Not byte-exact for records carrying opcodes 1, 7 or 8 — the decoder consumes
 * those without keeping them, so the information is already gone. Semantically
 * exact in all cases.
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

#endif
