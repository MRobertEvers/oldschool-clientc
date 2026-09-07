#include "mock239_playerinfo.h"

#include <rsareabuf.h>
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

/* Extended-info flags. Only APPEARANCE is written here; the flag byte grows to
 * two bytes at 0x8 and three at 0x800, which is why 0x20 fits in one. */
enum
{
    EXTINFO_APPEARANCE = 0x20,
    EXTINFO_FACE = 0x1,
    EXTINFO_SEQUENCE = 0x40,
    EXTINFO_CHAT = 0x100,
    EXTINFO_TEMP_MOVE_SPEED = 0x1000,
    EXTINFO_EXACT_MOVE = 0x4000,
    EXTINFO_SPOTANIM = 0x20000,
    EXTINFO_HITMARKS = 0x40000,
    EXTINFO_HEADBARS = 0x10000,
    /* "another flag byte follows" -- the player's are 0x8 and 0x800, where an
     * npc's are 0x40 and 0x800. Only the second one is shared. */
    EXTINFO_NEXT_BYTE_1 = 0x8,
    EXTINFO_NEXT_BYTE_2 = 0x800,
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

/*
 * pSmart1or2 is `rsab_psmart`. This file used to carry a private copy — one of
 * five in src/net (docs/BUFFER_ACCESSOR_AUDIT.md §1.1), byte-identical in range
 * and silently truncating outside it where the library latches an overflow.
 */
#define ext_psmart1or2 rsab_psmart

static void
write_player_extended(struct RSAreaBuf* buf, const uint8_t* appearance,
                      int appearance_len, const struct Mock239PlayerExt* ext)
{
    int const has_appearance = appearance && appearance_len > 0;
    int const has_hit = ext && ext->has_hit;
    int const has_headbar = ext && ext->has_headbar;
    int const has_face = ext && ext->has_face;
    int const has_seq = ext && ext->has_seq;
    int const has_chat = ext && ext->has_chat;
    int const has_spotanim = ext && ext->has_spotanim;
    int const has_temp_move_speed = ext && ext->has_temp_move_speed;
    int const has_exact_move = ext && ext->has_exact_move;
    int const has_extended =
        has_appearance || has_hit || has_face || has_seq || has_chat || has_spotanim ||
        has_temp_move_speed || has_exact_move || has_headbar;

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
        /*
         * The flag, then the blocks in the WRITER's order -- HITMARKS first,
         * then SEQUENCE, TEMP_MOVE_SPEED, and APPEARANCE. That is not ascending
         * flag order and not the order they are listed in anywhere; it is
         * PlayerAvatarExtendedInfoDesktopWriter's, and the client replays the
         * same sequence with nothing on the wire separating the blocks.
         *
         * The player flag space is its own, DIFFERENT from the npc one: here
         * SEQUENCE is 0x40 and HITMARKS 0x40000, where an npc's are 0x80 and
         * 0x80000. The continuation bits differ too -- a player's second byte
         * is announced by 0x8 and an npc's by 0x40.
         */
        uint32_t flag = 0;

        if( has_hit )
            flag |= EXTINFO_HITMARKS;
        if( has_headbar )
            flag |= EXTINFO_HEADBARS;
        if( has_face )
            flag |= EXTINFO_FACE;
        if( has_seq )
            flag |= EXTINFO_SEQUENCE;
        if( has_chat )
            flag |= EXTINFO_CHAT;
        if( has_spotanim )
            flag |= EXTINFO_SPOTANIM;
        if( has_temp_move_speed )
            flag |= EXTINFO_TEMP_MOVE_SPEED;
        if( has_exact_move )
            flag |= EXTINFO_EXACT_MOVE;
        if( has_appearance )
            flag |= EXTINFO_APPEARANCE;

        if( flag & 0xffffff00u )
            flag |= EXTINFO_NEXT_BYTE_1;
        if( flag & 0xffff0000u )
            flag |= EXTINFO_NEXT_BYTE_2;

        rsab_p1(buf, (int32_t)(flag & 0xff));
        if( flag & EXTINFO_NEXT_BYTE_1 )
            rsab_p1(buf, (int32_t)((flag >> 8) & 0xff));
        if( flag & EXTINFO_NEXT_BYTE_2 )
            rsab_p1(buf, (int32_t)((flag >> 16) & 0xff));

        if( has_hit )
        {
            /*
             * PlayerHitEncoder -- and it is NOT the npc encoder with a
             * different flag. The four fields per hit are the same, but the
             * COUNT is p1Alt3 (`128 - n`) where NpcHitmarkEncoder writes
             * p1Alt1 (`n + 128`).
             *
             * Writing the npc form here sends 129 for one hit, which the
             * client reads back as `128 - 129 = -1 & 0xff` = 255. It then reads
             * 255 hitsplats out of a packet that holds one, runs off the end,
             * and drops the connection. The symptom is the client logging out
             * at the instant a hit lands -- with no hitsplat, because the block
             * that would have drawn it is what killed the stream.
             */
            int const slots = ext->hit_slots > 0 ? ext->hit_slots : 4;
            int extra = ext->hit_extra_count;

            if( extra < 0 )
                extra = 0;
            if( extra > (int)(sizeof(ext->hit_extra) / sizeof(ext->hit_extra[0])) )
                extra = (int)(sizeof(ext->hit_extra) / sizeof(ext->hit_extra[0]));

            rsab_p1_alt3(buf, 1 + extra);
            ext_psmart1or2(buf, ext->hit_type);
            ext_psmart1or2(buf, ext->hit_value);
            ext_psmart1or2(buf, ext->hit_delay);
            /*
             * The fourth smart is not an optional cap. Actor.method3560 only
             * allocates a drawable hitmark when this value is positive, and
             * revision 239's actor keeps four concurrent hitmark slots.
             * Sending zero still posts RuneLite's HitsplatApplied callback but
             * deliberately inserts nothing into the render list.
             */
            ext_psmart1or2(buf, slots);
            /* The rest of the tick's splats, in the same four-field shape. A
             * player hit by two things at once is two hitmarks, not one — the
             * count above is what says so. */
            for( int i = 0; i < extra; i++ )
            {
                ext_psmart1or2(buf, ext->hit_extra[i].type);
                ext_psmart1or2(buf, ext->hit_extra[i].value);
                ext_psmart1or2(buf, ext->hit_delay);
                ext_psmart1or2(buf, slots);
            }
        }
        if( has_face )
        {
            /* PlayerFacingEncoder: p1Alt2 header, then Face's shared payload.
             * It is read after hitmarks/reset and before sequence. */
            mock239_face_write_player(buf, &ext->face);
        }
        if( has_spotanim )
        {
            rsab_p1(buf, 1);
            rsab_p1_alt2(buf, ext->spotanim_slot);
            rsab_p2_alt2(buf, ext->spotanim_id < 0 ? 65535 : ext->spotanim_id);
            rsab_p4(buf, ext->spotanim_height_delay);
        }
        if( has_chat )
        {
            rsab_p2_alt2(buf, ext->chat_colour_effect);
            rsab_p1_alt2(buf, ext->chat_type);
            rsab_p1_alt1(buf, 0); /* auto-typed */
            rsab_p1_alt2(buf, ext->chat_len);
            for( int i = ext->chat_len - 1; i >= 0; i-- )
                rsab_p1(buf, ext->chat_data[i]);
        }
        if( has_headbar )
        {
            /* PlayerHeadbarEncoder: p1Alt1 count, smart type/duration/delay,
             * then start fill p1Alt1 and target fill p1Alt2.
             *
             * Count and target fill are NOT the npc encoder's transforms --
             * that one writes them alt2 and alt3. The two blocks carry the
             * same six fields and disagree on two of the three byte
             * transforms; see the matching decoder.
             *
             * Which way round is not a detail: the pair was inverted here and
             * in the npc block until 2026-08-21, and the npc half killed a
             * real client's NPC_INFO the first time anything took a hit (see
             * put_npc_extended_v5). The player half is the same defect one
             * packet over -- a count of 1 written alt2 reads back as 127.
             */
            rsab_p1_alt1(buf, 1);
            ext_psmart1or2(buf, ext->headbar_type);
            ext_psmart1or2(buf, ext->headbar_duration);
            ext_psmart1or2(buf, ext->headbar_start_delay);
            rsab_p1_alt1(buf, ext->headbar_start_fill);
            if( ext->headbar_duration > 0 )
                rsab_p1_alt2(buf, ext->headbar_end_fill);
        }
        if( has_seq )
        {
            /*
             * PlayerSequenceEncoder: p2Alt2 id, p1 delay -- both orders differ
             * from NpcSequenceEncoder's p2 id, p1Alt2 delay. Same two fields,
             * same three bytes, and not one of them written the same way.
             *
             * p2Alt2 is [v>>8, v+128], so a plain p2 here shifts the id's low
             * byte by 128: the player plays a real animation, just the wrong
             * one. That is the whole failure -- nothing malformed, nothing
             * logged, a defend that looks like something else.
             */
            rsab_p2_alt2(buf, ext->seq_id < 0 ? 65535 : ext->seq_id);
            rsab_p1(buf, ext->seq_delay);
        }
        if( has_temp_move_speed )
        {
            /* PlayerTempMoveSpeedEncoder: raw signed p1. The golden client
             * reads this with class617.method13129(), not one of the Alt byte
             * transforms. class174 value 2 makes locomotion run for this
             * staged step without changing the player's persistent default. */
            rsab_p1(buf, ext->temp_move_speed);
        }
        if( has_appearance )
        {
            rsab_p1(buf, (128 - appearance_len) & 0xff);
            for( int i = 0; i < appearance_len; i++ )
                rsab_p1(buf, (appearance[i] + 128) & 0xff);
        }
        if( has_exact_move )
        {
            rsab_p1(buf, ext->exact_start_x);
            rsab_p1_alt1(buf, ext->exact_start_z);
            rsab_p1_alt2(buf, ext->exact_end_x);
            rsab_p1_alt3(buf, ext->exact_end_z);
            rsab_p2(buf, ext->exact_start_cycle);
            rsab_p2_alt1(buf, ext->exact_end_cycle);
            rsab_p2_alt3(buf, ext->exact_facing);
        }
    }
}

void
mock239_playerinfo_write(
    struct RSAreaBuf* buf,
    int local_index,
    enum Mock239PlayerMovement movement,
    int32_t movement_value,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len,
    const struct Mock239PlayerExt* ext)
{
    int const has_appearance = appearance && appearance_len > 0;
    int const has_hit = ext && ext->has_hit;
    int const has_headbar = ext && ext->has_headbar;
    int const has_face = ext && ext->has_face;
    int const has_seq = ext && ext->has_seq;
    int const has_chat = ext && ext->has_chat;
    int const has_spotanim = ext && ext->has_spotanim;
    int const has_temp_move_speed = ext && ext->has_temp_move_speed;
    int const has_exact_move = ext && ext->has_exact_move;
    int const has_extended =
        has_appearance || has_hit || has_face || has_seq || has_chat || has_spotanim ||
        has_temp_move_speed || has_exact_move || has_headbar;

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
    if( movement == MOCK239_PLAYER_NOMOVE && !has_extended )
    {
        rsab_pbit(buf, 1, 0);
        write_stationary(buf, 0);
        rsab_bytes(buf);
    }
    else
    {
        rsab_pbit(buf, 1, 1); /* not skipped */
        rsab_pbit(buf, 1, has_extended ? 1 : 0);
        switch( movement )
        {
        case MOCK239_PLAYER_NOMOVE:
            /* NOMOVE is legal for the local index only when extended info
             * follows.  The no-extended case took the skip branch above. */
            rsab_pbit(buf, 2, HIRES_OP_NOMOVE);
            break;
        case MOCK239_PLAYER_WALK:
            rsab_pbit(buf, 2, HIRES_OP_WALK);
            rsab_pbit(buf, 3, movement_value & 0x7);
            break;
        case MOCK239_PLAYER_RUN:
            rsab_pbit(buf, 2, HIRES_OP_RUN);
            rsab_pbit(buf, 4, movement_value & 0xf);
            break;
        case MOCK239_PLAYER_TELEPORT:
        default:
            /* Far form. `movement_value` is a delta against the coordinate
             * seeded by REBUILD_LOGIN / the preceding PLAYER_INFO. */
            rsab_pbit(buf, 2, HIRES_OP_TELEPORT);
            rsab_pbit(buf, 1, 1);
            rsab_pbit(buf, 30, movement_value);
            break;
        }
        rsab_bytes(buf);
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

    write_player_extended(buf, appearance, appearance_len, ext);
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

int
mock239_npcinfo_tail_needs_sentinel(
    size_t bit_position,
    size_t extended_bytes)
{
    size_t const padding_bits = (8u - (bit_position & 7u)) & 7u;

    return padding_bits + extended_bytes * 8u >= 28u;
}

static uint32_t
player_region(int32_t coord)
{
    uint32_t value = (uint32_t)coord;
    return ((value >> 28) & 3u) << 16 | ((value >> 27) & 1u) << 8 |
           ((value >> 13) & 1u);
}

void
mock239_playerinfo_state_init(struct Mock239PlayerInfoState* state,
                             int local_index, int32_t coord)
{
    assert(state);
    assert(local_index > 0);
    assert(local_index < MOCK239_PLAYER_SLOTS);
    memset(state, 0, sizeof(*state));
    state->initialized = 1;
    state->local_index = local_index;
    state->high[local_index] = 1;
    state->coord[local_index] = coord;
}

static int
player_update_extended(const struct Mock239PlayerUpdate* update)
{
    if( !update ) return 0;
    if( update->appearance && update->appearance_len > 0 ) return 1;
    const struct Mock239PlayerExt* e = update->ext;
    return e && (e->has_face || e->has_hit || e->has_headbar || e->has_seq ||
                 e->has_chat || e->has_spotanim || e->has_temp_move_speed ||
                 e->has_exact_move);
}

static int
player_update_needed(const struct Mock239PlayerInfoState* state, int index,
                     const struct Mock239PlayerUpdate* update)
{
    assert(state);
    if( state->high[index] )
        return !update || !update->visible ||
               update->movement != MOCK239_PLAYER_NOMOVE || player_update_extended(update);
    return update && (update->visible || player_region(update->coord) != state->region[index]);
}

static void
write_region_delta(struct RSAreaBuf* buf, uint32_t old_region, uint32_t new_region)
{
    assert(buf);
    uint32_t level = ((new_region >> 16) - (old_region >> 16)) & 3;
    uint32_t x = ((new_region >> 8) - (old_region >> 8)) & 255;
    uint32_t z = (new_region - old_region) & 255;
    rsab_pbit(buf, 2, 3);
    rsab_pbit(buf, 18, (int32_t)((level << 16) | (x << 8) | z));
}

void
mock239_playerinfo_write_world(struct RSAreaBuf* buf,
                              struct Mock239PlayerInfoState* state,
                              const struct Mock239PlayerUpdate* updates, int count)
{
    const struct Mock239PlayerUpdate* slots[MOCK239_PLAYER_SLOTS] = {0};
    const struct Mock239PlayerUpdate* extended[MOCK239_PLAYER_SLOTS];
    uint8_t old_high[MOCK239_PLAYER_SLOTS];
    uint8_t next_inactive[MOCK239_PLAYER_SLOTS] = {0};
    int extended_count = 0;
    assert(buf);
    assert(state);
    assert(state->initialized);
    assert(updates);
    assert(count > 0);
    assert(count < MOCK239_PLAYER_SLOTS);
    for( int i = 0; i < count; ++i )
    {
        int index = updates[i].index;
        assert(index > 0);
        assert(index < MOCK239_PLAYER_SLOTS);
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
        rsab_bits(buf);
        for( int index = 1; index < MOCK239_PLAYER_SLOTS; ++index )
        {
            if( old_high[index] != high || state->inactive[index] != inactive ) continue;
            const struct Mock239PlayerUpdate* update = slots[index];
            if( !player_update_needed(state, index, update) )
            {
                int further = 0, last = index;
                next_inactive[index] = 1;
                for( int next = index + 1; next < MOCK239_PLAYER_SLOTS; ++next )
                {
                    if( old_high[next] != high || state->inactive[next] != inactive ) continue;
                    if( player_update_needed(state, next, slots[next]) ) break;
                    next_inactive[next] = 1;
                    further++;
                    last = next;
                }
                rsab_pbit(buf, 1, 0);
                write_stationary(buf, further);
                index = last;
                continue;
            }
            rsab_pbit(buf, 1, 1);
            if( high && (!update || !update->visible) )
            {
                assert(index != state->local_index);
                uint32_t region = player_region(state->coord[index]);
                uint32_t wanted = update ? player_region(update->coord) : region;
                rsab_pbit(buf, 1, 0);
                rsab_pbit(buf, 2, HIRES_OP_NOMOVE);
                rsab_pbit(buf, 1, region != wanted);
                if( region != wanted ) write_region_delta(buf, region, wanted);
                state->high[index] = 0;
                state->region[index] = wanted;
                continue;
            }
            assert(update);
            int has_extended = player_update_extended(update);
            if( !high )
            {
                uint32_t wanted = player_region(update->coord);
                if( !update->visible )
                {
                    write_region_delta(buf, state->region[index], wanted);
                    state->region[index] = wanted;
                    continue;
                }
                rsab_pbit(buf, 2, 0); /* low-to-high promotion */
                rsab_pbit(buf, 1, wanted != state->region[index]);
                if( wanted != state->region[index] )
                    write_region_delta(buf, state->region[index], wanted);
                rsab_pbit(buf, 13, ((uint32_t)update->coord >> 14) & 8191);
                rsab_pbit(buf, 13, (uint32_t)update->coord & 8191);
                rsab_pbit(buf, 1, has_extended);
                state->high[index] = 1;
                state->region[index] = wanted;
                next_inactive[index] = 1;
            }
            else
            {
                rsab_pbit(buf, 1, has_extended);
                rsab_pbit(buf, 2, update->movement);
                switch( update->movement )
                {
                case MOCK239_PLAYER_NOMOVE: assert(has_extended); break;
                case MOCK239_PLAYER_WALK: rsab_pbit(buf, 3, update->movement_value & 7); break;
                case MOCK239_PLAYER_RUN: rsab_pbit(buf, 4, update->movement_value & 15); break;
                case MOCK239_PLAYER_TELEPORT:
                    rsab_pbit(buf, 1, 1);
                    rsab_pbit(buf, 30, update->movement_value);
                    break;
                }
            }
            state->coord[index] = update->coord;
            if( has_extended ) extended[extended_count++] = update;
        }
        rsab_bytes(buf);
    }
    memcpy(state->inactive, next_inactive, sizeof(next_inactive));
    for( int i = 0; i < extended_count; ++i )
    {
        const struct Mock239PlayerUpdate* update = extended[i];
        write_player_extended(buf, update->appearance, update->appearance_len, update->ext);
    }
}
