#include "mock239_playerinfo.h"

#include <rsareabuf.h>

/*
 * Transcribed against RSProt's own reference DECODER
 * (osrs-239-desktop/src/test/.../info/PlayerInfoClient.kt) rather than its
 * encoder. That is deliberate: the decoder is what a client does, it is one
 * file instead of seven thousand lines of production encoder, and a wire format
 * is easier to get right by reading what consumes it.
 */

/* Skip-run widths. `readStationary` picks by a 2-bit type. */
enum
{
    STATIONARY_NONE = 0, /* skip 0 more */
    STATIONARY_5BIT = 1,
    STATIONARY_8BIT = 2,
    STATIONARY_11BIT = 3,
};

/* High-resolution update opcodes, after the 1-bit extended-info flag. */
enum
{
    HIRES_OP_NOMOVE = 0, /* stayed put, or (without extended info) dropped to low res */
    HIRES_OP_WALK = 1,   /* 3-bit direction */
    HIRES_OP_RUN = 2,    /* 4-bit direction */
    HIRES_OP_TELEPORT = 3, /* 1-bit near/far, then 12 or 30 bits */
};

/* Extended-info flags. Only APPEARANCE is written here; the flag byte grows to
 * two bytes at 0x8 and three at 0x800, which is why 0x20 fits in one. */
enum
{
    EXTINFO_APPEARANCE = 0x20,
};

/**
 * Write a skip run covering `count` further players.
 *
 * The narrowest width that fits, because this runs once per gap and the widest
 * form costs 13 bits where the narrowest costs 3. `count` is players AFTER this
 * one, so a lone skip is count 0.
 */
static void
write_stationary(struct RSAreaBuf* buf, int count)
{
    if( count == 0 )
    {
        rsab_pbit(buf, 2, STATIONARY_NONE);
    }
    else if( count < 32 )
    {
        rsab_pbit(buf, 2, STATIONARY_5BIT);
        rsab_pbit(buf, 5, count);
    }
    else if( count < 256 )
    {
        rsab_pbit(buf, 2, STATIONARY_8BIT);
        rsab_pbit(buf, 8, count);
    }
    else
    {
        rsab_pbit(buf, 2, STATIONARY_11BIT);
        rsab_pbit(buf, 11, count);
    }
}

void
mock239_playerinfo_write_init(
    struct RSAreaBuf* buf,
    int local_index,
    int32_t coord)
{
    rsab_bits(buf);
    rsab_pbit(buf, 30, coord);

    /*
     * Every OTHER slot, in index order, as a low-resolution position.
     *
     * The client walks 1..2047 and skips its own index, so the entries are
     * positional: writing 2048 of them, or writing them in any other order,
     * shifts every subsequent player's rough position by one slot. There is no
     * length prefix to disagree with — the count is structural.
     *
     * Zero is a legitimate value here (it means "somewhere at 0,0"), and it is
     * what this server says about players it has never heard of. The client
     * only uses it to place a player it later promotes to high resolution, and
     * a promotion restates the position in full.
     */
    for( int idx = 1; idx < MOCK239_PLAYER_SLOTS; idx++ )
    {
        if( idx == local_index )
            continue;
        rsab_pbit(buf, 18, 0);
    }
    rsab_bytes(buf);
}

void
mock239_playerinfo_write(
    struct RSAreaBuf* buf,
    int local_index,
    int32_t coord_delta,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len)
{
    int const has_extended = appearance && appearance_len > 0;

    /*
     * Unused while the local player is the only high-resolution entry: the
     * section-1 loop is one iteration and does not need to name whose. It stays
     * in the signature because the moment a second player is tracked, every
     * write in that section becomes index-ordered and the caller must already
     * be passing it.
     */
    (void)local_index;

    /*
     * Section 1 — high resolution, active this cycle.
     *
     * The local player is the only high-resolution entry this server tracks, so
     * this section is exactly one update.
     */
    rsab_bits(buf);
    rsab_pbit(buf, 1, 1); /* not skipped */
    rsab_pbit(buf, 1, has_extended ? 1 : 0);
    /*
     * Always the far teleport form, carrying a delta that is usually zero.
     *
     * The alternative for a stationary player is NOMOVE, which is one bit
     * cheaper and carries a trap: NOMOVE without the extended-info bit means
     * "drop to low resolution" for a normal player, and for the LOCAL index the
     * client throws outright. A zero delta says the same thing with no state to
     * get wrong.
     *
     * The near form (12 bits) would be smaller again but is a signed delta
     * against what the client last heard, so it needs the same tracking with a
     * narrower range and a worse failure.
     */
    rsab_pbit(buf, 2, HIRES_OP_TELEPORT);
    rsab_pbit(buf, 1, 1);
    rsab_pbit(buf, 30, coord_delta);
    rsab_bytes(buf);

    /*
     * Section 2 — high resolution, inactive this cycle. Always empty: the only
     * high-resolution player is written above and is never skipped, so its
     * cycle bit never sets.
     *
     * Note that an empty bit section emits ZERO bytes — entering bit mode and
     * leaving it round the same byte cursor to itself. The four sections are
     * not four markers on the wire, which is worth knowing before hunting for
     * separators that do not exist.
     */
    rsab_bits(buf);
    rsab_bytes(buf);

    /*
     * Sections 3 and 4 — low resolution, inactive then active.
     *
     * The untracked crowd goes in exactly one of them, and which one changes
     * after the first tick. See `low_res_inactive` in the header: the client
     * sets a cycle bit on every player it skips and shifts it down each tick,
     * reading section 3 for players whose bit is set. So the run starts in
     * section 4 and moves to section 3 from the second tick onward.
     *
     * The list is indices 1..2047 (2047 of them) minus the local player, so
     * 2046 entries. One skip bit covers the first; the run then covers the
     * other 2045, because a run counts the players AFTER the one it follows.
     */
    {
        int const low_res_count = MOCK239_PLAYER_SLOTS - 1 - 1;

        rsab_bits(buf);
        if( low_res_inactive )
        {
            rsab_pbit(buf, 1, 0);
            write_stationary(buf, low_res_count - 1);
        }
        rsab_bytes(buf);

        rsab_bits(buf);
        if( !low_res_inactive )
        {
            rsab_pbit(buf, 1, 0);
            write_stationary(buf, low_res_count - 1);
        }
        rsab_bytes(buf);
    }

    /*
     * Extended info, byte-aligned, in the order the indices were flagged.
     *
     * The appearance block is length-prefixed with p1Alt3 (`128 - len`) and its
     * body is written with pdataAlt2 (each byte + 128). Both are obfuscation
     * rather than structure, and both are load-bearing: a plain length and a
     * plain body produce a block the client reads as a different length of
     * different data.
     */
    if( has_extended )
    {
        rsab_p1(buf, EXTINFO_APPEARANCE);
        rsab_p1(buf, (128 - appearance_len) & 0xff);
        for( int i = 0; i < appearance_len; i++ )
            rsab_p1(buf, (appearance[i] + 128) & 0xff);
    }
}

void
mock239_npcinfo_write_empty(struct RSAreaBuf* buf)
{
    rsab_bits(buf);
    rsab_pbit(buf, 8, 0); /* no high-resolution npcs */
    /*
     * NO 0xFFFF TERMINATOR HERE, and that is the whole subtlety of the empty
     * packet.
     *
     * The client's low-resolution loop only attempts to read an index when at
     * least 16 + 12 bits remain; below that it stops. With no npcs and no
     * extended info there is nothing after the count, so it stops immediately
     * and consumes ONE byte. Writing the terminator anyway makes the packet
     * three bytes that the client reads one of, and it reports the difference
     * as `RuntimeException: 1,3` and drops the connection — which presents as
     * the client returning to the login screen a second after logging in.
     *
     * The terminator is required once something follows: with additions or
     * extended-info bytes in the buffer the loop has enough bits to keep going
     * and would read them as npc indices. So it belongs in the populated
     * writer, not here.
     */
    rsab_bytes(buf);
}
