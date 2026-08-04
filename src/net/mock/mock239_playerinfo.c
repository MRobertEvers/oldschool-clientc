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
    int32_t coord,
    int teleported,
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
    if( teleported )
    {
        rsab_pbit(buf, 2, HIRES_OP_TELEPORT);
        /*
         * The far form. The near form is a 12-bit delta against the client's
         * previous coord, which is smaller but requires the server to know what
         * the client last heard — state this server does not keep per session,
         * and getting it wrong silently displaces the player rather than
         * failing.
         */
        rsab_pbit(buf, 1, 1);
        rsab_pbit(buf, 30, coord);
    }
    else
    {
        /*
         * NOMOVE with the extended-info bit set is "still here, and here is an
         * update". NOMOVE *without* it means something else entirely for a
         * non-local player — the client drops them to low resolution — and for
         * the local index the client throws. So this branch is only safe while
         * `has_extended` is true, and the caller that sends no appearance
         * should be sending a teleport or a movement instead.
         */
        rsab_pbit(buf, 2, HIRES_OP_NOMOVE);
    }
    rsab_bytes(buf);

    /*
     * Sections 2 and 3 — high resolution inactive, low resolution inactive.
     *
     * Both empty: the only high-resolution player was written above, a player
     * cannot be in both halves of one cycle, and nothing has dropped out of low
     * resolution.
     *
     * They are written for symmetry with the client's loop, NOT because the
     * wire needs them: an empty bit section emits zero bytes, since entering
     * bit mode and leaving it round the same byte cursor to itself. Deleting
     * these two lines produces an identical packet — a mutation test confirmed
     * it, which is worth stating here because the obvious reading ("four
     * sections, so four markers on the wire") is wrong and would send someone
     * hunting for separators that do not exist.
     */
    rsab_bits(buf);
    rsab_bytes(buf);
    rsab_bits(buf);
    rsab_bytes(buf);

    /*
     * Section 4 — low resolution, active: every index this server does not
     * track, skipped in one run.
     *
     * The list is indices 1..2047 (2047 of them) minus the local player, so
     * 2046 entries. One skip bit covers the first; the run then covers the
     * other 2045, because a run counts the players AFTER the one it follows.
     *
     * Neither off-by-one truncates the packet. Too small and the client's loop
     * finds a bit run where it expected the end; too large and it exits with
     * `skipped != 0`. Both throw inside the client, which is the good case —
     * the bad case would be a stream that decodes into the wrong players.
     */
    rsab_bits(buf);
    {
        int const low_res_count = MOCK239_PLAYER_SLOTS - 1 - 1;
        rsab_pbit(buf, 1, 0);
        write_stationary(buf, low_res_count - 1);
    }
    rsab_bytes(buf);

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
    rsab_pbit(buf, 8, 0);       /* no high-resolution npcs */
    rsab_pbit(buf, 16, 0xffff); /* end of the low-resolution additions */
    rsab_bytes(buf);
}
