#ifndef SRC_NET_MOCK_MOCK239_PLAYERINFO_H
#define SRC_NET_MOCK_MOCK239_PLAYERINFO_H

/*
 * PLAYER_INFO v5 — the revision-239 player stream.
 *
 * This is a different CODEC from the classic bitstream next door in
 * mock230_encode.c, not a different field order, which is why it is its own
 * file and why the wire adapter refuses PLAYER_INFO at 239 until this is
 * wired in. Without it no OldSchool client reaches the world: the stream is
 * how a client learns that it exists.
 *
 * ## The shape
 *
 * Two things are sent, and they are not the same thing.
 *
 * **The init block** rides inside REBUILD_LOGIN, once, and seeds the client's
 * whole 2048-slot table:
 *
 *     30 bits   the local player's absolute coord (level<<28 | x<<14 | z)
 *     18 bits   x 2047, one per other index: level<<16 | (x>>13)<<8 | (z>>13)
 *
 * Those 18-bit entries are *low-resolution* positions — map-square granularity,
 * which is all the client needs to know roughly where an unseen player is. The
 * local index is skipped, so the count is 2047 and not 2048.
 *
 * **The per-tick packet** is FOUR bit sections, each byte-aligned at both ends,
 * followed by the extended-info blocks:
 *
 *     1. high resolution, players active this cycle
 *     2. high resolution, players inactive this cycle
 *     3. low resolution, inactive
 *     4. low resolution, active
 *
 * The two-pass split per resolution is the client's own loop structure
 * (`unmodifiedFlags` carries a cycle bit), and a server that merges them writes
 * a stream the client reads in the wrong order. Every section is independently
 * byte-aligned, so they cannot be concatenated as one bit run.
 *
 * ## Skip runs
 *
 * Within a section a player is either written (`1` then its update) or skipped
 * (`0` then a run length). The run says how many players AFTER this one to
 * skip, in one of four widths:
 *
 *     type 0   0 more        type 1   5 bits      type 2   8 bits    type 3   11 bits
 *
 * 11 bits is what makes section 4 cheap: 2047 untracked players cost one bit,
 * two bits and eleven bits, not 2047 bits.
 *
 * ## What this file does and does not do
 *
 * It puts THE LOCAL PLAYER in high resolution with an appearance block, and
 * holds every other index in low resolution. That is the milestone that makes a
 * client render itself and its scene.
 *
 * It does not yet add, move or remove other players — the high-resolution
 * movement opcodes (walk, run, teleport, the 12- and 30-bit coord forms) and
 * the low-to-high transition are read by the client and written by nobody here.
 * The bit-level helpers below are shaped so that adding them is filling in
 * branches rather than restructuring, and `mock239_playerinfo_write` states
 * where.
 */

#include <stdint.h>

struct RSAreaBuf;

/** Slots in the player table; index 0 is unused, so the init block carries
 *  2047 low-resolution entries when the local player is one of them. */
#define MOCK239_PLAYER_SLOTS 2048

/**
 * The init block that rides inside REBUILD_LOGIN.
 *
 * `local_index` is the slot the client will treat as itself, and `coord` is
 * packed (level << 28) | (x << 14) | z. Writes bits and leaves the buffer
 * byte-aligned.
 */
void
mock239_playerinfo_write_init(
    struct RSAreaBuf* buf,
    int local_index,
    int32_t coord);

/**
 * One tick of PLAYER_INFO carrying only the local player.
 *
 * `teleported` selects how the position is stated: a teleport writes the
 * absolute 30-bit coord, otherwise the player is reported stationary. Both
 * forms carry the extended-info bit when `appearance` is non-NULL.
 *
 * `low_res_inactive` selects WHICH low-resolution section the untracked players
 * are skipped in, and it is not cosmetic. The client carries a per-player cycle
 * bit (`unmodifiedFlags`) that it sets on anyone it skipped and shifts down
 * each tick, and it reads section 3 for players whose bit is set and section 4
 * for those whose is not. So on the first tick after the init block every slot
 * is "active" and the run goes in section 4; on every tick after that the same
 * slots have been skipped once and the run must move to section 3.
 *
 * Writing it in the wrong section does not desynchronise immediately -- tick 1
 * is correct either way -- it desynchronises on tick 2, which reads as the
 * client working briefly and then dropping back to the login screen.
 *
 * `appearance` / `appearance_len` are the raw appearance block — the same bytes
 * the classic stream sends — which this wraps in the v5 extended-info framing.
 * NULL sends no extended info, which is correct for every tick after the first.
 */
void
mock239_playerinfo_write(
    struct RSAreaBuf* buf,
    int local_index,
    int32_t coord,
    int teleported,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len);

/**
 * One tick of NPC_INFO v5 carrying no npcs.
 *
 * A far simpler codec than PLAYER_INFO: ONE bit section, not four.
 *
 *     8 bits    how many high-resolution npcs follow
 *     ...       one update per high-resolution npc
 *     16 bits   an index per npc entering view, terminated by 0xFFFF
 *
 * The index is 16 bits at this revision — the classic stream's is 14 — which is
 * the same id-space widening that moved the npc type field, and a decoder built
 * for the narrow form reads two npcs where there is one.
 *
 * The 0xFFFF terminator is not optional whenever extended info follows. The
 * client's low-resolution loop keeps consuming 16-bit indices while the bit
 * reader has bits left, and the bit reader spans the rest of the packet — so
 * without it the extended-info bytes are read as npc indices.
 *
 * This writes the empty case only: no high-resolution npcs, no additions. That
 * is what the server needs to be able to SEND the packet at all rather than
 * refuse it; populating it is the next step and is a smaller job than
 * PLAYER_INFO was.
 */
void
mock239_npcinfo_write_empty(struct RSAreaBuf* buf);

#endif
