#ifndef RSCACHE_REFERENCE_TABLE_H
#define RSCACHE_REFERENCE_TABLE_H

#include <stdbool.h>

struct RSCache_ReferenceTableArchiveFile
{
    int name_hash;
    int id;
};

struct RSCache_ReferenceTableArchive
{
    int index;
    int identifier;
    int crc;
    int hash;
    unsigned char whirlpool[64];
    int compressed;
    int uncompressed;
    int version;
    struct
    {
        struct RSCache_ReferenceTableArchiveFile* files;
        int count;
    } children;
};

struct RSCache_ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    int* ids;
    struct RSCache_ReferenceTableArchive* archives;
    int archive_count;
};

struct RSCache_ReferenceTable*
RSCache_ReferenceTableNewDecode(
    char* data,
    int data_size);

void
RSCache_ReferenceTableFree(struct RSCache_ReferenceTable* table);

#endif
