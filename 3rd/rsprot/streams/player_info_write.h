#ifndef RSPROT_STREAM_PLAYER_INFO_WRITE_H
#define RSPROT_STREAM_PLAYER_INFO_WRITE_H

/*
 * PLAYER_INFO, the writer side: what a server sends, beside the reader in
 * player_info.h that a client runs on the same bytes.
 *
 * The GPI bit sections here are shared by every OSRS revision written so far
 * (239..242). The extended-info blocks behind them differ by revision and are
 * each revision's own: player_info_write_osrs239.c, player_info_write_osrs241.c,
 * player_info_write_osrs242.c. Each revision's `player_info_osrs<N>_write`
 * calls `rsprot_player_info_write_sections` here and then writes its own tail.
 *
 * The stream was first written for revision 239, so the history below speaks
 * of it as "the revision-239 player stream". It moved here from
 * src/torirsserver/wire/ (2026-11-15), where torirs_server_encode.c drives it;
 * "the server" below is that caller.
 *
 * This is a different CODEC from the classic bitstream torirs_server_encode.c
 * writes for revision 230, not a different field order. Without it no
 * OldSchool client reaches the world: the stream is how a client learns that
 * it exists.
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
 * The world writer retains each observer's high/low membership and cycle bits,
 * promotes visible players, writes walk/run/teleport and extended updates, and
 * retires players that leave view. The local-only writer remains available for
 * focused codec fixtures.
 */

#include "face_write.h"
#include "rsprot_info.h" /* RSPROT_INFO_PLAYER_SLOTS */

#include <stddef.h>
#include <stdint.h>

struct RsprotBuf;

/** Graphics one PLAYER_INFO spotanim block carries (the server's per-tick
 *  slot list, TORIRSSERVER_NPC_SPOTANIM_MAX). */
#define RSPROT_PLAYER_SPOTANIM_MAX 8

/**
 * The init block that rides inside REBUILD_LOGIN.
 *
 * `local_index` is the slot the client will treat as itself, and `coord` is
 * packed (level << 28) | (x << 14) | z. Writes bits and leaves the buffer
 * byte-aligned.
 */
void
rsprot_player_info_write_init(
    struct RsprotBuf* buf,
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

/* A HEADBARS entry's end time that removes the bar (RSProx decodeHeadbars:
 * `removed=true`, no start delay or fills after it). */
#define RSPROT_HEADBAR_REMOVE 0x7fff

struct RsprotPlayerExt
{
    /** Revision-239 FACE block.  This is the generic entity/loc/reset model,
     * not either of the legacy player-mask fields. */
    int has_face;
    struct RsprotFace face;
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
        int delay;
    } hit_extra[3];
    int hit_extra_count;
    /** The actor HEADBARS block is independent from a hitmark. A duration
     *  of RSPROT_HEADBAR_REMOVE removes the bar: nothing follows it. */
    int has_headbar;
    int headbar_type;
    int headbar_duration;
    int headbar_start_delay;
    int headbar_start_fill;
    int headbar_end_fill;
    /** One more HEADBARS entry per further splat (rev 241: one bar per splat,
     *  filled to the hitpoints after it); duration 0, so no end fill. */
    struct
    {
        int type;
        int start_delay;
        int start_fill;
    } headbar_extra[3];
    int headbar_extra_count;
    /** Public-chat bytes are already Huffman-coded (net/chat_huffman.h); the v5
     *  wire reverses them. */
    int has_chat;
    int chat_colour_effect;
    int chat_type;
    uint8_t const* chat_data;
    int chat_len;
    /** PlayerSpotAnimEncoder's list: one graphic per numbered slot, each
     *  replacing only its own slot's on the client. */
    int has_spotanim;
    int spotanim_count;
    struct RsprotPlayerSpotanim
    {
        int slot;
        int id;
        /** height << 16 | delay, as the wire carries it. */
        int height_delay;
    } spotanims[RSPROT_PLAYER_SPOTANIM_MAX];
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
    /** TINTING (player flag 0x200): cycles relative to the client's arrival
     * cycle, then hue/saturation/lightness bytes (255 = leave the channel)
     * and the weight out of 128. */
    int has_tint;
    int tint_start_cycle;
    int tint_end_cycle;
    int tint_hue;
    int tint_saturation;
    int tint_lightness;
    int tint_weight;
    /** TRANSPARENCY (rev 241 only, player flag 0x10000; deob class81): signed
     * cycles relative to arrival, the alphas (0 opaque .. 255 gone), and
     * whether the fade starts from `from` (class17.method328). The 239
     * writer has no block for it, so it is never set there. */
    int has_transparency;
    int transparency_start_cycle;
    int transparency_end_cycle;
    int transparency_from;
    int transparency_to;
    int transparency_use_start;
};

/** The rev-239 high-resolution movement opcode.  `movement_value` below is a
 * 3-bit direction for WALK, a 4-bit direction for RUN, and a packed 30-bit
 * coordinate delta for TELEPORT. */
enum RsprotGpiMove
{
    RSPROT_GPI_NOMOVE = 0,
    RSPROT_GPI_WALK = 1,
    RSPROT_GPI_RUN = 2,
    RSPROT_GPI_TELEPORT = 3,
};

void
rsprot_player_info_write_local(
    struct RsprotBuf* buf,
    int local_index,
    enum RsprotGpiMove movement,
    int32_t movement_value,
    int low_res_inactive,
    int has_extended);

/** 1 when this player has an extended-info block to write this tick: an
 *  appearance, or any of `ext`'s blocks. Decides the GPI's extended bit, so
 *  every revision's tail must write exactly the players this says yes to. */
int
rsprot_player_info_has_extended(
    const uint8_t* appearance,
    int appearance_len,
    const struct RsprotPlayerExt* ext);

/** Per-observer GPI state. Membership and cycle flags describe the last
 * packet, not the current world; reset only with the login/reconnect init. */
struct RsprotPlayerInfoWriter
{
    int initialized;
    int local_index;
    uint8_t high[RSPROT_INFO_PLAYER_SLOTS];
    uint8_t inactive[RSPROT_INFO_PLAYER_SLOTS];
    int32_t coord[RSPROT_INFO_PLAYER_SLOTS];
    uint32_t region[RSPROT_INFO_PLAYER_SLOTS];
};

struct RsprotPlayerUpdate
{
    int index;
    int visible;
    int32_t coord;
    enum RsprotGpiMove movement;
    int32_t movement_value;
    const uint8_t* appearance;
    int appearance_len;
    const struct RsprotPlayerExt* ext;
};

void
rsprot_player_info_writer_init(
    struct RsprotPlayerInfoWriter* state,
    int local_index,
    int32_t coord);
/**
 * The four GPI bit sections for `updates`, against `state`. Fills `extended`
 * with the players whose extended-info blocks must follow, in the order the
 * client reads them, and returns how many. The caller -- a revision's
 * `player_info_osrs<N>_write` -- writes those blocks in its own layout.
 */
int
rsprot_player_info_write_sections(
    struct RsprotBuf* buf,
    struct RsprotPlayerInfoWriter* state,
    const struct RsprotPlayerUpdate* updates,
    int count,
    const struct RsprotPlayerUpdate* extended[RSPROT_INFO_PLAYER_SLOTS]);


/* ------------------------------------------------------------------ */
/* Per revision: player_info_<rev>_write is the writer beside          */
/* player_info_<rev> (player_info.h), the reader of the same stream.   */
/* ------------------------------------------------------------------ */

/* ---- revision 239: the GPI sections, then 239's extended-info tail ---- */

/** One tick carrying only the local player (see rsprot_player_info_write_local). */
void
player_info_osrs239_write_local(
    struct RsprotBuf* buf,
    int local_index,
    enum RsprotGpiMove movement,
    int32_t movement_value,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len,
    const struct RsprotPlayerExt* ext);

/** One tick of the whole world, against the observer's GPI `state`. */
void
player_info_osrs239_write(
    struct RsprotBuf* buf,
    struct RsprotPlayerInfoWriter* state,
    const struct RsprotPlayerUpdate* updates,
    int count);

/* ---- revision 241: the GPI sections, then 241's extended-info tail ---- */

/** One tick of the whole world, against the observer's GPI `state`. */
void
player_info_osrs241_write(
    struct RsprotBuf* buf,
    struct RsprotPlayerInfoWriter* state,
    const struct RsprotPlayerUpdate* updates,
    int count);

/* ---- revision 242: the GPI sections, then 242's extended-info tail ---- */

/** One tick of the whole world, against the observer's GPI `state`. */
void
player_info_osrs242_write(
    struct RsprotBuf* buf,
    struct RsprotPlayerInfoWriter* state,
    const struct RsprotPlayerUpdate* updates,
    int count);

/* Rev 239 writes one overhead-icon archive index in an appearance block;
 * generic server/content state uses the older bitmask vocabulary.
 *
 * 31 bits, not 8. The eight came from the classic wire field being one byte,
 * but this is the index side of the conversion, not the mask side — and
 * `headicons_prayer` already has 24 frames in the base cache and 30 with the
 * Ancient Curses lane's own icons appended. At 8 the six curse overheads
 * (24..29) were unreachable: the mask carried the bit, this loop never looked
 * at it, and the appearance went out as 255 "no icon". */
static inline int
rsprot_appearance_headicon_index(int mask)
{
    for( int icon = 0; icon < 31; icon++ )
        if( mask & (1 << icon) )
            return icon;
    return -1;
}

#endif
