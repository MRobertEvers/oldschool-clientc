#ifndef REFERENCE_TABLE_H
#define REFERENCE_TABLE_H

#include <stdbool.h>

struct ArchiveFileReference
{
    int name_hash;
    int id;
};

struct ArchiveReference
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
        struct ArchiveFileReference* files;
        int count;
    } children;
};

struct ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    // archives is a sparse array. ids is the list of indices that are valid.
    int* ids;

    // Each entry refers to an archive.
    // The 'children' of the archives are "files" in the archive.
    struct ArchiveReference* archives;
    int archive_count;
};

struct ReferenceTable* reference_table_new_decode(char* data, int data_size);
void reference_table_free(struct ReferenceTable* table);

#endif