#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_INV_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_INV_H

#include "../rsbuffer.h"

#include <stdbool.h>

/**
 * An inventory type (config group 5): how many slots a container has.
 *
 * One record per inventory id — the player's own inventory, the bank, each shop,
 * equipment. Anything that has to know a container's size reads this: an inventory
 * update, a drag bounds check, "is it full".
 *
 * The format is the whole of it: `opcode 2` carrying a u16, then the terminator. That
 * is not an assumption — every one of the 920 records in cache.osrs230 is exactly the
 * four bytes `02 <u16> 00`, and brute-forcing the operand width over 0-4 across five
 * caches leaves exactly one possibility. No record in the corpus carries any other
 * opcode, and none carries a size of zero.
 */
struct RSCache_Dat2ConfigInv
{
    int id;
    /** Slot count. 0 when the record named none, which no cache record does. */
    int size;
    struct RSCache_Params params;
    /** Bytes consumed. Equal to the record size for a fully understood record. */
    int _consumed;
};

/** Decode from a cursor, so back-to-back records in one buffer can be walked. */
void
RSCache_Dat2ConfigInvDecode(
    struct RSCache_Dat2ConfigInv* entry,
    struct RSCache_Buffer* buffer);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigInvDecodeOp(
    struct RSCache_Dat2ConfigInv* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Decode one dat2 record and record `_consumed`. Stops on an unknown opcode rather
 *  than guessing its width, which leaves `_consumed` short — the signal the
 *  round-trip harness asserts on. */
void
RSCache_Dat2ConfigInvDecodeInplace(
    struct RSCache_Dat2ConfigInv* entry,
    const void* data,
    int data_size);

void
RSCache_Dat2ConfigInvFreeInplace(struct RSCache_Dat2ConfigInv* entry);

/** Byte-exact on every record in the corpus. Returns bytes written, or 0. */
uint32_t
RSCache_Dat2ConfigInvEncode(
    const struct RSCache_Dat2ConfigInv* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigInvEncodeBound(const struct RSCache_Dat2ConfigInv* entry);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_INV_H
