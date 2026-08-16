#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_BAS_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_BAS_H

#include <stdint.h>

struct RSCache;

/** Early RS2 BAS sequence references are u16 with 65535 as the sentinel. */
#define RSCACHE_CODEC_BAS_RS2_U16 1
/** The 727 client reads BAS sequence references as BigSmart values. */
#define RSCACHE_CODEC_BAS_RS2_727 2

int
RSCache_Dat2ConfigBasCodecVersion(const struct RSCache* cache);

/**
 * Base Animation Set (BAS)
 * BasType / render-animation config (RS2 config group 32).
 *
 * Sourced from rs-map-viewer's BasType.ts. An NPC's opcode 127 (`basTypeId`)
 * redirects idle/walk sequences through this type instead of opcodes 13/14 —
 * Steel titan (7343) is the motivating case: standing/walking stay 0 and the
 * real seqs live on bas 1318.
 *
 * Only the fields the client reads for NPC motion are stored. Other opcodes are
 * consumed so the stream stays aligned.
 */
struct RSCache_Dat2ConfigBas
{
    int idle_seq_id;
    int walk_seq_id;
    int walk_back_seq_id;
    int walk_left_seq_id;
    int walk_right_seq_id;
    int _consumed;
};

struct RSCache_Dat2ConfigBas*
RSCache_Dat2ConfigBasNewDecode(
    char* data,
    int data_size);

struct RSCache_Dat2ConfigBas*
RSCache_Dat2ConfigBasNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int data_size);

/** Encode the stored motion fields (opcodes 1, 40–42) plus terminator.
 *  Returns bytes written, or 0 on failure. */
uint32_t
RSCache_Dat2ConfigBasEncode(
    const struct RSCache_Dat2ConfigBas* bas,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigBasEncodeBound(const struct RSCache_Dat2ConfigBas* bas);

void
RSCache_Dat2ConfigBasFree(struct RSCache_Dat2ConfigBas* bas);

#endif
