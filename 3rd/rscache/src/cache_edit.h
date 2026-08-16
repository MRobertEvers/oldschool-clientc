#ifndef RSCACHE_CACHE_EDIT_H
#define RSCACHE_CACHE_EDIT_H

#include "dat1disk.h"
#include "dat2disk.h"

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Dat2Edit;

struct RSCache_Dat2Edit* RSCache_Dat2EditNew(struct RSCache_Dat2Disk* disk);
void RSCache_Dat2EditFree(struct RSCache_Dat2Edit* edit);

bool RSCache_Dat2EditPutFile(struct RSCache_Dat2Edit* edit, int table_id, int archive_id,
                             int file_id, const uint8_t* data, uint32_t size);

bool RSCache_Dat2EditPutArchive(struct RSCache_Dat2Edit* edit, int table_id, int archive_id,
                                const uint8_t* data, uint32_t size);

bool RSCache_Dat2EditCommit(struct RSCache_Dat2Edit* edit, const char* directory);

struct RSCache_Dat1Edit;

struct RSCache_Dat1Edit* RSCache_Dat1EditNew(struct RSCache_Dat1Disk* disk);
void RSCache_Dat1EditFree(struct RSCache_Dat1Edit* edit);

/** Replace/append one NPC record (already encoded) in the configs jagfile. */
bool RSCache_Dat1EditPutNpc(
    struct RSCache_Dat1Edit* edit,
    int npc_id,
    const uint8_t* record,
    uint32_t size);

/** Replace one sequence record inside seq.dat (already encoded). */
bool RSCache_Dat1EditPutSeq(
    struct RSCache_Dat1Edit* edit,
    int seq_id,
    const uint8_t* record,
    uint32_t size);

/** Replace the entire seq.dat payload. */
bool RSCache_Dat1EditPutSeqList(
    struct RSCache_Dat1Edit* edit,
    const uint8_t* seq_dat,
    uint32_t size);

/**
 * Write a model archive. If `already_gzipped` is false, the payload is gzip
 * compressed before being stored in the models table.
 */
bool RSCache_Dat1EditPutModel(
    struct RSCache_Dat1Edit* edit,
    int model_id,
    const uint8_t* gzip_or_raw,
    uint32_t size,
    bool already_gzipped);

/**
 * Write an animation archive. If `already_gzipped` is false, the payload is gzip
 * compressed before being stored in the animations table.
 */
bool RSCache_Dat1EditPutAnimArchive(
    struct RSCache_Dat1Edit* edit,
    int archive_id,
    const uint8_t* decompressed_or_gzip,
    uint32_t size,
    bool already_gzipped);

/**
 * Apply pending puts: reload configs/version-list jagfiles, splice npc/seq,
 * gzip+write models/anims, bump version-list versions/crcs/index lengths, and
 * rewrite table 0 archives 2 and 5.
 */
bool RSCache_Dat1EditCommit(struct RSCache_Dat1Edit* edit, const char* directory);

#endif
