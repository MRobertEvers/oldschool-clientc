#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_ENUM_H

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

void
RSCache_Dat2ConfigEnumFree(struct RSCache_Dat2ConfigEnum* entry);

void
RSCache_Dat2ConfigEnumFreeInplace(struct RSCache_Dat2ConfigEnum* entry);

#endif
