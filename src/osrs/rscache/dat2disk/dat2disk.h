#ifndef RSCACHE_RSCACHEDAT2DISK_H
#define RSCACHE_RSCACHEDAT2DISK_H

#include "dat2disk_reference_table.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RSCacheDat2Disk_Table
{
    RSCacheDat2Disk_Table_Animations = 0,
    RSCacheDat2Disk_Table_Skeletons = 1,
    RSCacheDat2Disk_Table_Configs = 2,
    RSCacheDat2Disk_Table_Interfaces = 3,
    RSCacheDat2Disk_Table_SoundEffects = 4,
    RSCacheDat2Disk_Table_Maps = 5,
    RSCacheDat2Disk_Table_MusicTracks = 6,
    RSCacheDat2Disk_Table_Models = 7,
    RSCacheDat2Disk_Table_Sprites = 8,
    RSCacheDat2Disk_Table_Textures = 9,
    RSCacheDat2Disk_Table_Binary = 10,
    RSCacheDat2Disk_Table_MusicJingles = 11,
    RSCacheDat2Disk_Table_Clientscript = 12,
    RSCacheDat2Disk_Table_Fonts = 13,
    RSCacheDat2Disk_Table_MusicSamples = 14,
    RSCacheDat2Disk_Table_MusicPatches = 15,
    RSCacheDat2Disk_Table_WorldmapGeography = 18,
    RSCacheDat2Disk_Table_Worldmap = 19,
    RSCacheDat2Disk_Table_WorldmapGround = 20,
    RSCacheDat2Disk_Table_DbtableIndex = 21,
    RSCacheDat2Disk_Table_Animayas = 22,
    RSCacheDat2Disk_Table_Gamevals = 24,
    RSCacheDat2Disk_Table_Count
};

/**
 * There are many tables in the Dat2 cache format.
 *
 * Table 255 serves as a special table.
 * Table 255 contains archive metadata for the archives stored in
 * the other tables.
 * The metadata includes, file names, file ids, and CRC information.
 * The archive ids in table 255 correspond to the table ids of the other tables.
 * For example, table 2 corresponds to archive 2 of table 255.
 * The archives in table 255 are a binary blob that is parsed into a list of archive metadata.
 * So the metadata for the archives of table 2 are stored in a list
 * of archive metadata in archive 2 of table 255.
 * You can find archive metadata by searching through the list of archive metadata and looking for
 * the archive id that matches the archive id of the archive in table 2.
 *
 * Otherwise, Table 255 behaves like the other tables.
 *
 * Each table stores Archives. Archives can be a single blob,
 * or a multi-file archive. Archives can be compressed or uncompressed.
 *
 * The compression mode of the archive is stored WITH the archive.
 * All other information about the archive is stored in the table 255.
 *
 * Archives that contain multiple files are stored in a multi-file format, called a "FileList" here.
 * Note: For "dat", LostCity calls this a "JagFile", we call it a "FileListDat", which is slightly
 * different from "FileList".
 */

bool
RSCacheDat2Disk_IsValidTableId(int table_id);

enum RSCacheDat2Disk_Mode
{
    RSCacheDat2Disk_Mode_LocalOnly = 0,
    RSCacheDat2Disk_Mode_Inet = 1,
};

struct RSCacheDat2Disk
{
    char const* directory;

    struct RSCacheDat2Disk_ReferenceTable* tables[RSCacheDat2Disk_Table_Count];

    FILE* _dat2_file;

    enum RSCacheDat2Disk_Mode mode;
    void* _inet_nullable;
};

struct RSCacheDat2Disk*
RSCacheDat2Disk_NewFromDirectory(char const* directory);
struct RSCacheDat2Disk*
RSCacheDat2Disk_NewInet(
    char const* directory,
    char const* ip,
    int port);
struct RSCacheDat2Disk*
RSCacheDat2Disk_NewUninitialized(void);
void
RSCacheDat2Disk_Free(struct RSCacheDat2Disk* cache);

struct RSCacheDat2Disk_Archive
{
    char* data;
    int data_size;

    int archive_id;
    int table_id;
    int revision;

    int file_count;
};

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewReferenceTableLoad(
    struct RSCacheDat2Disk* cache,
    int table_id);

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewLoad(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id);
struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewLoadDecrypted(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id,
    uint32_t* xtea_key_nullable);
struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewLoadUninitializedMetadata(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id);

void
RSCacheDat2Disk_ArchiveInitMetadata(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive);
void
RSCacheDat2Disk_ArchiveFree(struct RSCacheDat2Disk_Archive* archive);

uint32_t*
RSCacheDat2Disk_ArchiveXteaKey(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id);

#ifdef __cplusplus
}
#endif
#endif