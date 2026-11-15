#include "player_info_write.h"

#include "../src/rsprot_buf.h"
#include <assert.h>
#include <string.h>

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

/**
 * Write a skip run covering `count` further players.
 *
 * The narrowest width that fits, because this runs once per gap and the widest
 * form costs 13 bits where the narrowest costs 3. `count` is players AFTER this
 * one, so a lone skip is count 0.
 */
static void
gpi_write_stationary(struct RsprotBuf* buf, int count)
{
    if( count == 0 )
    {
        rsprot_pbits(buf, 2, STATIONARY_NONE);
    }
    else if( count < 32 )
    {
        rsprot_pbits(buf, 2, STATIONARY_5BIT);
        rsprot_pbits(buf, 5, count);
    }
    else if( count < 256 )
    {
        rsprot_pbits(buf, 2, STATIONARY_8BIT);
        rsprot_pbits(buf, 8, count);
    }
    else
    {
        rsprot_pbits(buf, 2, STATIONARY_11BIT);
        rsprot_pbits(buf, 11, count);
    }
}

void
rsprot_player_info_write_init(
    struct RsprotBuf* buf,
    int local_index,
    int32_t coord)
{
    rsprot_buf_bit_begin(buf);
    rsprot_pbits(buf, 30, coord);

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
    for( int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++ )
    {
        if( idx == local_index )
            continue;
        rsprot_pbits(buf, 18, 0);
    }
    rsprot_buf_bit_end(buf);
}

void
rsprot_player_info_write_local(
    struct RsprotBuf* buf,
    int local_index,
    enum RsprotGpiMove movement,
    int32_t movement_value,
    int low_res_inactive,
    int has_extended)
{
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
    rsprot_buf_bit_begin(buf);
    /*
     * A player who has not moved and has nothing to say is SKIPPED, not
     * written.
     *
     * This is the shape the client is built around: `active = 0` plus a
     * zero-length stationary run says "nothing about this one this tick". The
     * obvious alternative -- writing a teleport whose delta happens to be zero
     * -- is well-formed and decodes cleanly, and it still breaks the client,
     * because a teleport means the player JUMPED and the client re-centres its
     * scene on one. Sending that every tick re-centres every tick: the world
     * builds, draws once, and then goes black.
     *
     * (NOMOVE, opcode 0, is the other way to say "still here", but only with
     * the extended-info bit set -- without it the client throws outright for
     * the local index. Skipping needs no such care.)
     */
    if( movement == RSPROT_GPI_NOMOVE && !has_extended )
    {
        rsprot_pbits(buf, 1, 0);
        gpi_write_stationary(buf, 0);
        rsprot_buf_bit_end(buf);
    }
    else
    {
        rsprot_pbits(buf, 1, 1); /* not skipped */
        rsprot_pbits(buf, 1, has_extended ? 1 : 0);
        switch( movement )
        {
        case RSPROT_GPI_NOMOVE:
            /* NOMOVE is legal for the local index only when extended info
             * follows.  The no-extended case took the skip branch above. */
            rsprot_pbits(buf, 2, HIRES_OP_NOMOVE);
            break;
        case RSPROT_GPI_WALK:
            rsprot_pbits(buf, 2, HIRES_OP_WALK);
            rsprot_pbits(buf, 3, movement_value & 0x7);
            break;
        case RSPROT_GPI_RUN:
            rsprot_pbits(buf, 2, HIRES_OP_RUN);
            rsprot_pbits(buf, 4, movement_value & 0xf);
            break;
        case RSPROT_GPI_TELEPORT:
        default:
            /* Far form. `movement_value` is a delta against the coordinate
             * seeded by REBUILD_LOGIN / the preceding PLAYER_INFO. */
            rsprot_pbits(buf, 2, HIRES_OP_TELEPORT);
            rsprot_pbits(buf, 1, 1);
            rsprot_pbits(buf, 30, movement_value);
            break;
        }
        rsprot_buf_bit_end(buf);
    }

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
    rsprot_buf_bit_begin(buf);
    rsprot_buf_bit_end(buf);

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
        int const low_res_count = RSPROT_INFO_PLAYER_SLOTS - 1 - 1;

        rsprot_buf_bit_begin(buf);
        if( low_res_inactive )
        {
            rsprot_pbits(buf, 1, 0);
            gpi_write_stationary(buf, low_res_count - 1);
        }
        rsprot_buf_bit_end(buf);

        rsprot_buf_bit_begin(buf);
        if( !low_res_inactive )
        {
            rsprot_pbits(buf, 1, 0);
            gpi_write_stationary(buf, low_res_count - 1);
        }
        rsprot_buf_bit_end(buf);
    }
}

static uint32_t
gpi_player_region(int32_t coord)
{
    uint32_t value = (uint32_t)coord;
    return ((value >> 28) & 3u) << 16 | ((value >> 27) & 1u) << 8 |
           ((value >> 13) & 1u);
}

void
rsprot_player_info_writer_init(struct RsprotPlayerInfoWriter* state,
                             int local_index, int32_t coord)
{
    assert(state);
    assert(local_index > 0);
    assert(local_index < RSPROT_INFO_PLAYER_SLOTS);
    memset(state, 0, sizeof(*state));
    state->initialized = 1;
    state->local_index = local_index;
    state->high[local_index] = 1;
    state->coord[local_index] = coord;
}

int
rsprot_player_info_has_extended(
    const uint8_t* appearance,
    int appearance_len,
    const struct RsprotPlayerExt* ext)
{
    if( appearance && appearance_len > 0 )
        return 1;
    return ext && (ext->has_face || ext->has_hit || ext->has_headbar || ext->has_seq ||
                   ext->has_chat || ext->has_spotanim || ext->has_temp_move_speed ||
                   ext->has_exact_move || ext->has_tint || ext->has_transparency);
}

static int
gpi_update_extended(const struct RsprotPlayerUpdate* update)
{
    if( !update ) return 0;
    return rsprot_player_info_has_extended(update->appearance, update->appearance_len, update->ext);
}

static int
gpi_update_needed(const struct RsprotPlayerInfoWriter* state, int index,
                     const struct RsprotPlayerUpdate* update)
{
    assert(state);
    if( state->high[index] )
        return !update || !update->visible ||
               update->movement != RSPROT_GPI_NOMOVE || gpi_update_extended(update);
    return update && (update->visible || gpi_player_region(update->coord) != state->region[index]);
}

static void
gpi_write_region_delta(struct RsprotBuf* buf, uint32_t old_region, uint32_t new_region)
{
    assert(buf);
    uint32_t level = ((new_region >> 16) - (old_region >> 16)) & 3;
    uint32_t x = ((new_region >> 8) - (old_region >> 8)) & 255;
    uint32_t z = (new_region - old_region) & 255;
    rsprot_pbits(buf, 2, 3);
    rsprot_pbits(buf, 18, (int32_t)((level << 16) | (x << 8) | z));
}

int
rsprot_player_info_write_sections(struct RsprotBuf* buf,
                                struct RsprotPlayerInfoWriter* state,
                                const struct RsprotPlayerUpdate* updates, int count,
                                const struct RsprotPlayerUpdate* extended[RSPROT_INFO_PLAYER_SLOTS])
{
    const struct RsprotPlayerUpdate* slots[RSPROT_INFO_PLAYER_SLOTS] = {0};
    uint8_t old_high[RSPROT_INFO_PLAYER_SLOTS];
    uint8_t next_inactive[RSPROT_INFO_PLAYER_SLOTS] = {0};
    int extended_count = 0;
    assert(buf);
    assert(state);
    assert(state->initialized);
    assert(updates);
    assert(extended);
    assert(count > 0);
    assert(count < RSPROT_INFO_PLAYER_SLOTS);
    for( int i = 0; i < count; ++i )
    {
        int index = updates[i].index;
        assert(index > 0);
        assert(index < RSPROT_INFO_PLAYER_SLOTS);
        assert(!slots[index]);
        slots[index] = &updates[i];
    }
    assert(slots[state->local_index]);
    assert(slots[state->local_index]->visible);
    memcpy(old_high, state->high, sizeof(old_high));

    /* RSProt239 PlayerInfoClient keeps its index lists until all four passes
     * finish. A removal must not be emitted again in this packet's low pass. */
    for( int pass = 0; pass < 4; ++pass )
    {
        int high = pass < 2;
        int inactive = pass == 1 || pass == 2;
        rsprot_buf_bit_begin(buf);
        for( int index = 1; index < RSPROT_INFO_PLAYER_SLOTS; ++index )
        {
            if( old_high[index] != high || state->inactive[index] != inactive ) continue;
            const struct RsprotPlayerUpdate* update = slots[index];
            if( !gpi_update_needed(state, index, update) )
            {
                int further = 0, last = index;
                next_inactive[index] = 1;
                for( int next = index + 1; next < RSPROT_INFO_PLAYER_SLOTS; ++next )
                {
                    if( old_high[next] != high || state->inactive[next] != inactive ) continue;
                    if( gpi_update_needed(state, next, slots[next]) ) break;
                    next_inactive[next] = 1;
                    further++;
                    last = next;
                }
                rsprot_pbits(buf, 1, 0);
                gpi_write_stationary(buf, further);
                index = last;
                continue;
            }
            rsprot_pbits(buf, 1, 1);
            if( high && (!update || !update->visible) )
            {
                assert(index != state->local_index);
                uint32_t region = gpi_player_region(state->coord[index]);
                uint32_t wanted = update ? gpi_player_region(update->coord) : region;
                rsprot_pbits(buf, 1, 0);
                rsprot_pbits(buf, 2, HIRES_OP_NOMOVE);
                rsprot_pbits(buf, 1, region != wanted);
                if( region != wanted ) gpi_write_region_delta(buf, region, wanted);
                state->high[index] = 0;
                state->region[index] = wanted;
                continue;
            }
            assert(update);
            int has_extended = gpi_update_extended(update);
            if( !high )
            {
                uint32_t wanted = gpi_player_region(update->coord);
                if( !update->visible )
                {
                    gpi_write_region_delta(buf, state->region[index], wanted);
                    state->region[index] = wanted;
                    continue;
                }
                rsprot_pbits(buf, 2, 0); /* low-to-high promotion */
                rsprot_pbits(buf, 1, wanted != state->region[index]);
                if( wanted != state->region[index] )
                    gpi_write_region_delta(buf, state->region[index], wanted);
                rsprot_pbits(buf, 13, ((uint32_t)update->coord >> 14) & 8191);
                rsprot_pbits(buf, 13, (uint32_t)update->coord & 8191);
                rsprot_pbits(buf, 1, has_extended);
                state->high[index] = 1;
                state->region[index] = wanted;
                next_inactive[index] = 1;
            }
            else
            {
                rsprot_pbits(buf, 1, has_extended);
                rsprot_pbits(buf, 2, update->movement);
                switch( update->movement )
                {
                case RSPROT_GPI_NOMOVE: assert(has_extended); break;
                case RSPROT_GPI_WALK: rsprot_pbits(buf, 3, update->movement_value & 7); break;
                case RSPROT_GPI_RUN: rsprot_pbits(buf, 4, update->movement_value & 15); break;
                case RSPROT_GPI_TELEPORT:
                    rsprot_pbits(buf, 1, 1);
                    rsprot_pbits(buf, 30, update->movement_value);
                    break;
                }
            }
            state->coord[index] = update->coord;
            if( has_extended ) extended[extended_count++] = update;
        }
        rsprot_buf_bit_end(buf);
    }
    memcpy(state->inactive, next_inactive, sizeof(next_inactive));
    return extended_count;
}
