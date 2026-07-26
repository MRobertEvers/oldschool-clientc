#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_SEQ_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_SEQ_H

#include <stdbool.h>

/*
 * Sequence (animation) config from the dat1 config jagfile's "seq.dat".
 * See SeqType.unpack in the LostCity JavaClient.
 *
 * frames[i] addresses one animation frame as
 *   (animbaseframes archive id << 16) | frame index within that archive,
 * where the archive comes from the ANIMATIONS table. iframes[i] is the same
 * encoding for the interleave ("i") frame, or -1.
 */
struct RSCache_Dat1ConfigSeq
{
    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    int* walkmerge;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
};

/** Whole "seq.dat" table: entries are variable-length and only sequentially
 * addressable, so they decode as one list. */
struct RSCache_Dat1ConfigSeqList
{
    struct RSCache_Dat1ConfigSeq* seqs;
    int seqs_count;
};

struct RSCache_Dat1ConfigSeqList*
RSCache_Dat1ConfigSeqListNewDecode(
    char* data,
    int data_size);

void
RSCache_Dat1ConfigSeqListFree(struct RSCache_Dat1ConfigSeqList* list);

/** Returns the number of bytes this entry consumed. */
int
RSCache_Dat1ConfigSeqDecodeInplace(
    struct RSCache_Dat1ConfigSeq* seq,
    char* data,
    int data_size);

void
RSCache_Dat1ConfigSeqFreeInplace(struct RSCache_Dat1ConfigSeq* seq);

uint32_t
RSCache_Dat1ConfigSeqEncodeBound(const struct RSCache_Dat1ConfigSeq* seq);

uint32_t
RSCache_Dat1ConfigSeqEncode(
    const struct RSCache_Dat1ConfigSeq* seq,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat1ConfigSeqListEncodeBound(const struct RSCache_Dat1ConfigSeqList* list);

uint32_t
RSCache_Dat1ConfigSeqListEncode(
    const struct RSCache_Dat1ConfigSeqList* list,
    uint8_t* out,
    uint32_t out_capacity);

#endif
