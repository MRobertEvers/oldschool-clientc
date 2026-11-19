#ifndef SRC_GAME_ENTITY_INFO_SCRATCH_H
#define SRC_GAME_ENTITY_INFO_SCRATCH_H

/*
 * The op lists the PLAYER_INFO and NPC_INFO decoders write into, kept from one
 * packet to the next.
 *
 * Each decoder fills an ENTITY_INFO_SCRATCH_OPS_MAX list: the ops and the byte
 * arena their strings and blobs live in, ~460KB together. Both packets arrive
 * every server tick, so each decoder has one list that it borrows for a packet
 * and hands back when the task that read it is freed, instead of allocating
 * and freeing one per packet. The list is allocated on its first borrow.
 *
 * A list already lent out is not lent twice: a second borrow while the first
 * is out gets a list of its own, freed on its release. The packet pipeline
 * settles one task before it runs the next, so that does not happen in
 * practice; the fallback keeps it from being a correctness assumption.
 *
 * This does not decode anything and does not know which packet a list is for.
 * The caller picks the decoder's slot, hands back exactly the list it
 * borrowed, and frees the kept lists at teardown.
 */

#include <stdint.h>

struct RsprotInfoOp;
struct RsprotInfoList;

enum
{
    /** Ops in one borrowed list. */
    ENTITY_INFO_SCRATCH_OPS_MAX = 4096,
    /** The list's strings and blobs: no more than the payload (a var-short)
     *  and a NUL per op. */
    ENTITY_INFO_SCRATCH_BYTES_MAX = 65536 + ENTITY_INFO_SCRATCH_OPS_MAX,
};

/** One decoder's kept list. */
struct EntityInfoScratchSlot
{
    /** NULL until the first borrow. */
    struct RsprotInfoOp* ops;
    uint8_t* bytes;
    /** The kept list is lent out. */
    int busy;
};

struct EntityInfoScratch
{
    struct EntityInfoScratchSlot npc;
    struct EntityInfoScratchSlot player;
};

/**
 * Point `list` at the slot's kept list, allocating it on the first borrow, and
 * mark it lent; if it is already lent, at a fresh allocation instead.
 */
void
EntityInfoScratch_Borrow(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list);

/**
 * Hand `list` back to the slot it was borrowed from: the kept list is marked
 * free again, a fallback list is freed. A NULL list, or one that never
 * borrowed (its ops NULL), is left alone. `list->ops` is NULL afterwards.
 */
void
EntityInfoScratch_Release(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list);

/** Free both kept lists and forget them. Safe on NULL and on scratch that
 *  never lent. */
void
EntityInfoScratch_Free(struct EntityInfoScratch* scratch);

#endif /* SRC_GAME_ENTITY_INFO_SCRATCH_H */
