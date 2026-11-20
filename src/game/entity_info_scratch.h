#ifndef SRC_GAME_ENTITY_INFO_SCRATCH_H
#define SRC_GAME_ENTITY_INFO_SCRATCH_H

/*
 * The op lists the PLAYER_INFO and NPC_INFO decoders write into, kept from one
 * packet to the next, and the read that fills one: both clients' (the real
 * client's entity-info tasks and the headless client's) go through it.
 *
 * Each decoder fills a list: the ops and the byte arena their strings and
 * blobs live in. Both packets arrive every server tick, so each decoder has
 * one list that it borrows for a packet and hands back when the task that read
 * it is freed, instead of allocating and freeing one per packet. The list is
 * allocated on its first borrow, ENTITY_INFO_SCRATCH_OPS_MAX ops and
 * ENTITY_INFO_SCRATCH_BYTES_MAX bytes.
 *
 * A LIST IS NEVER APPLIED INCOMPLETE. Legal server data can need more than
 * those: a tick in which every one of the 255 players an observer tracks
 * enters view with its blocks (an appearance, hits, headbars, graphics, a
 * sequence) is several thousand ops, the 1,792 untracked slots can each carry
 * a low-resolution step on top, and the byte arena holds a BODY_CUSTOM's
 * two-byte models as four-byte ints. A reader that runs out sets the list's
 * `overflow` and carries on so its stream state stays in step; applying that
 * list would silently drop every entity past the cut. So EntityInfoScratch_Read
 * sizes the list from the packet: on an overflow it doubles the list, puts
 * the reader's stream state back to where it was before the packet, and reads
 * the packet again, until it fits. A grown kept list stays grown.
 *
 * A list already lent out is not lent twice: a second borrow while the first
 * is out gets a list of its own, freed on its release. The packet pipeline
 * settles one task before it runs the next, so that does not happen in
 * practice; the fallback keeps it from being a correctness assumption.
 *
 * This does not know which packet a list is for. The caller picks the
 * decoder's slot, hands back exactly the list it borrowed, and frees the kept
 * lists at teardown.
 */

#include <stddef.h>
#include <stdint.h>

struct RsprotInfoOp;
struct RsprotInfoList;
struct ToriMem_Group;

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
    /** The kept list's size, once allocated: the defaults, or what a packet
     *  grew it to. */
    int ops_cap;
    int bytes_cap;
    /** The kept list is lent out. */
    int busy;
    /** Where the lists are allocated; NULL is ToriMem_Session(). */
    struct ToriMem_Group* group;
    /** A stateful reader's state as it was before the packet being read
     *  (EntityInfoScratch_Read), allocated on the first such read. */
    void* state;
    size_t state_size;
};

/**
 * A decoder: the revision's reader, and for a stream with state across
 * packets (the OSRS player table) how to save and restore it -- all three, or
 * all NULL for a stream with none (GameProtoRevTable's player_info_state_*).
 */
struct EntityInfoReader
{
    int (*read)(
        uint8_t const* data,
        int len,
        struct RsprotInfoList* out);
    size_t (*state_size)(void);
    void (*state_save)(void* out);
    void (*state_load)(void const* in);
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

/**
 * Read one packet into `list` (borrowed from `slot`) with `reader`, growing
 * the list until the whole packet fits (see the top of this file). Returns
 * the reader's op count. `list->ops` may move: read it after this returns.
 */
int
EntityInfoScratch_Read(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list,
    struct EntityInfoReader const* reader,
    uint8_t const* data,
    int len);

/** Free both kept lists and forget them. Safe on NULL and on scratch that
 *  never lent. */
void
EntityInfoScratch_Free(struct EntityInfoScratch* scratch);

#endif /* SRC_GAME_ENTITY_INFO_SCRATCH_H */
