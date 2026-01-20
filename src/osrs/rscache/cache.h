#ifndef OSRS_CACHE_H_
#define OSRS_CACHE_H_

#include "osrs/rscache/reference_table.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum CacheTable
{
    CACHE_ANIMATIONS = 0,
    CACHE_SKELETONS = 1,
    CACHE_CONFIGS = 2,
    CACHE_INTERFACES = 3,
    CACHE_SOUNDEFFECTS = 4,
    CACHE_MAPS = 5,
    CACHE_MUSIC_TRACKS = 6,
    CACHE_MODELS = 7,
    CACHE_SPRITES = 8,
    CACHE_TEXTURES = 9,
    CACHE_BINARY = 10,
    CACHE_MUSIC_JINGLES = 11,
    CACHE_CLIENTSCRIPT = 12,
    CACHE_FONTS = 13,
    CACHE_MUSIC_SAMPLES = 14,
    CACHE_MUSIC_PATCHES = 15,
    CACHE_WORLDMAP_GEOGRAPHY = 18,
    CACHE_WORLDMAP = 19,
    CACHE_WORLDMAP_GROUND = 20,
    CACHE_DBTABLEINDEX = 21,
    CACHE_ANIMAYAS = 22,
    CACHE_GAMEVALS = 24,
    CACHE_TABLE_COUNT
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
cache_is_valid_table_id(int table_id);

enum CacheMode
{
    CACHE_MODE_LOCAL_ONLY = 0,
    CACHE_MODE_INET = 1,
};

struct Cache
{
    char const* directory;

    struct ReferenceTable* tables[CACHE_TABLE_COUNT];

    FILE* _dat2_file;

    enum CacheMode mode;
    void* _inet_nullable;
};

struct Cache*
cache_new_from_directory(char const* directory);
struct Cache*
cache_new_inet(
    char const* directory,
    char const* ip,
    int port);
struct Cache*
cache_new_uninitialized(void);
void
cache_free(struct Cache* cache);

struct CacheArchive
{
    char* data;
    int data_size;

    int archive_id;
    int table_id;
    int revision;

    int file_count;
};

struct CacheArchive*
cache_archive_new_reference_table_load(
    struct Cache* cache,
    int table_id);

struct CacheArchive*
cache_archive_new_load(
    struct Cache* cache,
    int table_id,
    int archive_id);
struct CacheArchive*
cache_archive_new_load_decrypted(
    struct Cache* cache,
    int table_id,
    int archive_id,
    uint32_t* xtea_key_nullable);
struct CacheArchive*
cache_archive_new_load_uninitialized_metadata(
    struct Cache* cache,
    int table_id,
    int archive_id);

void
cache_archive_init_metadata(
    struct Cache* cache,
    struct CacheArchive* archive);
void
cache_archive_free(struct CacheArchive* archive);

uint32_t*
cache_archive_xtea_key(
    struct Cache* cache,
    int table_id,
    int archive_id);

#ifdef __cplusplus
}
#endif
#endif