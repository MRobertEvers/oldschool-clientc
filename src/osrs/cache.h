#ifndef OSRS_CACHE_H_
#define OSRS_CACHE_H_

#include "reference_table.h"

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

bool cache_is_valid_table_id(int table_id);

struct Cache
{
    char const* directory;

    struct ReferenceTable* tables[CACHE_TABLE_COUNT];

    char* _dat2;
    int _dat2_size;
};

struct Cache* cache_new_from_directory(char const* directory);
void cache_free(struct Cache* cache);

struct CacheArchive
{
    char* data;
    int data_size;

    int archive_id;
    int table_id;
    int revision;

    int file_count;
};

struct CacheArchive* cache_archive_new_reference_table_load(struct Cache* cache, int table_id);
struct CacheArchive* cache_archive_new_load(struct Cache* cache, int table_id, int archive_id);
void cache_archive_free(struct CacheArchive* archive);

#endif