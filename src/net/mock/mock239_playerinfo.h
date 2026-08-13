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
 * It does not yet add, move or remove other players.  The local player's
 * high-resolution walk, run and teleport forms are implemented; the
 * low-to-high transition for other slots is not.
 */

#include <stddef.h>
#include <stdint.h>

#include "mock239_facing.h"

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
 * `movement` selects NOMOVE, WALK, RUN or TELEPORT. `movement_value` is the
 * matching direction or coordinate delta. A TELEPORT value is a DELTA against
 * the position the client already holds, not an absolute coordinate — packed
 * (dLevel << 28) | (dX << 14) | dZ with each field masked to its width. The
 * client adds it:
 *
 *     curX = (curX + deltaX) and 16383
 *
 * so passing an absolute coord moves the player by that much every tick. The
 * symptom is a world that builds correctly and then goes black a few seconds
 * later, as the player walks off the loaded scene — nothing errors, because
 * every packet is well-formed.
 *
 * A stationary player uses NOMOVE; when it has no extended info the writer
 * expresses that as a stationary skip, which is the client codec's canonical
 * form.
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
 * NULL sends no appearance, which is correct for every tick after the first.
 *
 * `ext` carries the rest of the local player's extended info, or NULL. See
 * the struct.
 */
struct Mock239PlayerExt
{
    /** Revision-239 FACE block.  This is the generic entity/loc/reset model,
     * not either of the legacy player-mask fields. */
    int has_face;
    struct Mock239Face face;
    /** A hitsplat landing this tick: the hitmark TYPE from content, and the
     *  damage. Zero `has_hit` when nothing was hit.
     *
     *  These name the FIRST splat of the tick. A player can take several at
     *  once — in the Inferno a whole wave lands together — and the block is a
     *  list, so `hit_extra` carries the rest. A caller that fills only these
     *  four (`hit_extra_count` left at zero) sends exactly one hitmark, which
     *  is what every caller did before the list existed. */
    int has_hit;
    int hit_type;
    int hit_value;
    int hit_delay;
    int hit_slots;
    /** Splats two and onward, same fields, same tick. */
    struct
    {
        int type;
        int value;
    } hit_extra[3];
    int hit_extra_count;
    /** The actor HEADBARS block is independent from a hitmark. */
    int has_headbar;
    int headbar_type;
    int headbar_duration;
    int headbar_start_delay;
    int headbar_start_fill;
    int headbar_end_fill;
    /** Public-chat bytes are already wordpacked; the v5 wire reverses them. */
    int has_chat;
    int chat_colour_effect;
    int chat_type;
    uint8_t const* chat_data;
    int chat_len;
    int has_spotanim;
    int spotanim_slot;
    int spotanim_id;
    int spotanim_height_delay;
    /** A sequence to play: `seq_id` < 0 cancels whatever is running. */
    int has_seq;
    int seq_id;
    int seq_delay;
    /** One-step traversal override. Revision 239 separates the RUN position
     * opcode from the locomotion speed used to animate that step: value 2 is
     * run, value 1 is the default walk. Without this block a player advances
     * two tiles per update while visibly playing the walk sequence. */
    int has_temp_move_speed;
    int temp_move_speed;
    int has_exact_move;
    int exact_start_x;
    int exact_start_z;
    int exact_end_x;
    int exact_end_z;
    int exact_start_cycle;
    int exact_end_cycle;
    int exact_facing;
};

/** The rev-239 high-resolution movement opcode.  `movement_value` below is a
 * 3-bit direction for WALK, a 4-bit direction for RUN, and a packed 30-bit
 * coordinate delta for TELEPORT. */
enum Mock239PlayerMovement
{
    MOCK239_PLAYER_NOMOVE = 0,
    MOCK239_PLAYER_WALK = 1,
    MOCK239_PLAYER_RUN = 2,
    MOCK239_PLAYER_TELEPORT = 3,
};

void
mock239_playerinfo_write(
    struct RSAreaBuf* buf,
    int local_index,
    enum Mock239PlayerMovement movement,
    int32_t movement_value,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len,
    const struct Mock239PlayerExt* ext);

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
mock239_npcinfo_write_empty(struct RSAreaBuf* buf);

/** Mirror the client's 28-bit low-resolution guard at the boundary
 * between the last add record and byte-aligned extended info. */
int
mock239_npcinfo_tail_needs_sentinel(
    size_t bit_position,
    size_t extended_bytes);

#endif
