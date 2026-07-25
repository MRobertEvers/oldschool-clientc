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
};

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

#endif
