#ifndef RSCACHE_DATATYPES_DAT1_VERSION_LIST_H
#define RSCACHE_DATATYPES_DAT1_VERSION_LIST_H

#include <stdint.h>

struct RSCache_FileListDat;
struct RSCache_MapSquares;

/**
 * Contents of the dat1 version-list jagfile (config table archive 5).
 *
 * `map_squares` is owned by this struct when produced by
 * RSCache_Dat1VersionListNewFromJagfile (or when EncodeIntoJagfile rewrites
 * map_index from it). Callers that assign an external MapSquares must either
 * transfer ownership or clear the pointer before Free.
 */
struct RSCache_Dat1VersionList
{
    uint16_t* model_version;
    int model_version_count;
    uint16_t* anim_version;
    int anim_version_count;
    uint16_t* midi_version;
    int midi_version_count;
    uint16_t* map_version;
    int map_version_count;

    int32_t* model_crc;
    int model_crc_count;
    int32_t* anim_crc;
    int anim_crc_count;
    int32_t* midi_crc;
    int midi_crc_count;
    int32_t* map_crc;
    int map_crc_count;

    uint8_t* model_index;
    int model_index_count;
    uint16_t* anim_index;
    int anim_index_count;
    uint8_t* midi_index;
    int midi_index_count;

    /** Owned; encoded as "map_index" (7 bytes per square). */
    struct RSCache_MapSquares* map_squares;
};

struct RSCache_Dat1VersionList*
RSCache_Dat1VersionListNewFromJagfile(struct RSCache_FileListDat* jag);

/**
 * Replace/update the named version-list members inside `jag`. Existing members
 * are freed and rewritten; missing members are appended. Returns bytes that
 * would be produced by a subsequent FileListDatEncode of `jag`, or 0 on failure.
 * (The jag itself is updated in place; callers still encode and write it.)
 */
uint32_t
RSCache_Dat1VersionListEncodeIntoJagfile(
    const struct RSCache_Dat1VersionList* vl,
    struct RSCache_FileListDat* jag);

void
RSCache_Dat1VersionListFree(struct RSCache_Dat1VersionList* vl);

#endif
