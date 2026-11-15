#ifndef RSPROT_STREAM_NPC_INFO_WRITE_H
#define RSPROT_STREAM_NPC_INFO_WRITE_H

/* NPC_INFO v5, the writer side: what a server needs beside the reader in
 * npc_info.h. */

#include <stddef.h>

struct RsprotBuf;

/**
 * One tick of NPC_INFO v5 carrying no npcs.
 *
 * A far simpler codec than PLAYER_INFO: ONE bit section, not four.
 *
 *     8 bits    how many high-resolution npcs follow
 *     ...       one update per high-resolution npc
 *     16 bits   a client-local instance index per npc, terminated by 0xFFFF
 *     ...       each add record finishes with a 14-bit initial NPC type
 *
 * The index is not the NPC's cache/config id. A type above 0x3FFF is sent as a
 * valid 14-bit initial type plus the add record's update flag; the queued
 * update then uses mask 0x1 and a transformed unsigned 16-bit (p2Alt3)
 * replacement type in the same packet.
 *
 * The 0xFFFF terminator is required only when the padding plus extended-info
 * tail leaves at least 28 readable bits. That is this codec's exact
 * `16 + 12` low-resolution-record guard. Below that threshold the client exits
 * before reading an index, so writing a sentinel makes it consume 0xffff as the
 * first extended mask instead.
 *
 * This writes the empty case only: no high-resolution npcs, no additions. That
 * is what the server needs to be able to SEND the packet at all rather than
 * refuse it; populating it is the next step and is a smaller job than
 * PLAYER_INFO was.
 */
void
rsprot_npc_info_write_empty(struct RsprotBuf* buf);

/** Mirror the client's 28-bit low-resolution guard at the boundary
 * between the last add record and byte-aligned extended info. */
int
rsprot_npc_info_tail_needs_sentinel(
    size_t bit_position,
    size_t extended_bytes);

#endif
