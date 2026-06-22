#ifndef RSCACHE_RSCACHEDAT2DISK_REFERENCETABLE_H
#define RSCACHE_RSCACHEDAT2DISK_REFERENCETABLE_H

#include <stdbool.h>

struct RSCacheDat2Disk_ArchiveFileReference
{
    int name_hash;
    int id;
};

struct RSCacheDat2Disk_ArchiveReference
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
        struct RSCacheDat2Disk_ArchiveFileReference* files;
        int count;
    } children;
};

struct RSCacheDat2Disk_ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    // archives is a sparse array. ids is the list of indices that are valid.
    int* ids;

    // Each entry refers to an archive.
    // The 'children' of the archives are "files" in the archive.
    struct RSCacheDat2Disk_ArchiveReference* archives;
    int archive_count;
};

struct RSCacheDat2Disk_ReferenceTable* RSCacheDat2Disk_ReferenceTableNewDecode(char* data, int data_size);
void RSCacheDat2Disk_ReferenceTableFree(struct RSCacheDat2Disk_ReferenceTable* table);

#endif