#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_SEQ_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_SEQ_H

#include "../rsbuffer.h"

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

/**
 * Bring a zeroed allocation to this type's defaults.
 *
 * Seven fields are non-zero and each is a value the type also uses: `loops` -1,
 * `priority` 5 (the gate `playAnimation` compares against, so 0 would let any
 * anim interrupt any other), `maxloops` 99, and the held/move ids at -1 where 0
 * is obj 0 and move-style 0. The encoder writes an opcode only where the field
 * differs from these, so a wrong init silently changes which opcodes are emitted.
 */
void
RSCache_Dat1ConfigSeqInit(struct RSCache_Dat1ConfigSeq* seq);

/**
 * Handle one opcode, advancing `buffer` past its payload.
 *
 * False means "not mine", which ends the stream — the width of an unknown
 * opcode cannot be guessed. See `opcode_codec.h`.
 */
bool
RSCache_Dat1ConfigSeqDecodeOp(
    struct RSCache_Dat1ConfigSeq* seq,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

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
