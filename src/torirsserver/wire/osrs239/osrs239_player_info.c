#include "torirsserver/wire/osrs239/osrs239_player_info.h"

#include "torirsserver/wire/core/wire_facing.h"

#include <assert.h>
#include "rsprot_buf.h"

/*
 * PLAYER_INFO's extended-info tail at revision 239. The GPI bit sections in
 * front of it are wire/core/wire_player_info.c's, shared with 241 and 242.
 *
 * Transcribed against RSProt's own reference DECODER
 * (osrs-239-desktop/src/test/.../info/PlayerInfoClient.kt) rather than its
 * encoder: the decoder is what a client does.
 */

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
    EXTINFO_TINTING = 0x200,
    /* "another flag byte follows" -- the player's are 0x8 and 0x800, where an
     * npc's are 0x40 and 0x800. Only the second one is shared. */
    EXTINFO_NEXT_BYTE_1 = 0x8,
    EXTINFO_NEXT_BYTE_2 = 0x800,
};

static void
osrs239_player_info_extended(struct RsprotBuf* buf, const uint8_t* appearance,
                             int appearance_len, const struct WirePlayerExt* ext)
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
    int const has_tint = ext && ext->has_tint;
    int const has_extended =
        has_appearance || has_hit || has_face || has_seq || has_chat || has_spotanim ||
        has_temp_move_speed || has_exact_move || has_headbar || has_tint;

    /* Rev 239 has no TRANSPARENCY block; the encoder sets it at 241 only. */
    assert(!ext || !ext->has_transparency);

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
        if( has_tint )
            flag |= EXTINFO_TINTING;
        if( has_appearance )
            flag |= EXTINFO_APPEARANCE;

        if( flag & 0xffffff00u )
            flag |= EXTINFO_NEXT_BYTE_1;
        if( flag & 0xffff0000u )
            flag |= EXTINFO_NEXT_BYTE_2;

        rsprot_p1(buf, (int32_t)(flag & 0xff));
        if( flag & EXTINFO_NEXT_BYTE_1 )
            rsprot_p1(buf, (int32_t)((flag >> 8) & 0xff));
        if( flag & EXTINFO_NEXT_BYTE_2 )
            rsprot_p1(buf, (int32_t)((flag >> 16) & 0xff));

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

            rsprot_p1_sub128(buf, 1 + extra);
            rsprot_psmart1or2(buf, ext->hit_type);
            rsprot_psmart1or2(buf, ext->hit_value);
            rsprot_psmart1or2(buf, ext->hit_delay);
            /*
             * The fourth smart is not an optional cap. Actor.method3560 only
             * allocates a drawable hitmark when this value is positive, and
             * revision 239's actor keeps four concurrent hitmark slots.
             * Sending zero still posts RuneLite's HitsplatApplied callback but
             * deliberately inserts nothing into the render list.
             */
            rsprot_psmart1or2(buf, slots);
            /* The rest of the tick's splats, in the same four-field shape. A
             * player hit by two things at once is two hitmarks, not one — the
             * count above is what says so. */
            for( int i = 0; i < extra; i++ )
            {
                rsprot_psmart1or2(buf, ext->hit_extra[i].type);
                rsprot_psmart1or2(buf, ext->hit_extra[i].value);
                rsprot_psmart1or2(buf, ext->hit_extra[i].delay);
                rsprot_psmart1or2(buf, slots);
            }
        }
        if( has_face )
        {
            /* PlayerFacingEncoder: p1Alt2 header, then Face's shared payload.
             * It is read after hitmarks/reset and before sequence. */
            wire_face_write_player(buf, &ext->face);
        }
        if( has_spotanim )
        {
            assert(ext->spotanim_count > 0);
            assert(ext->spotanim_count <= WIRE_PLAYER_SPOTANIM_MAX);
            rsprot_p1(buf, ext->spotanim_count);
            for( int i = 0; i < ext->spotanim_count; i++ )
            {
                struct WirePlayerSpotanim const* spot = &ext->spotanims[i];

                rsprot_p1_neg(buf, spot->slot);
                rsprot_p2_12_add128(buf, spot->id < 0 ? 65535 : spot->id);
                rsprot_p4_1234(buf, spot->height_delay);
            }
        }
        if( has_chat )
        {
            rsprot_p2_12_add128(buf, ext->chat_colour_effect);
            rsprot_p1_neg(buf, ext->chat_type);
            rsprot_p1_add128(buf, 0); /* auto-typed */
            rsprot_p1_neg(buf, ext->chat_len);
            for( int i = ext->chat_len - 1; i >= 0; i-- )
                rsprot_p1(buf, ext->chat_data[i]);
        }
        if( has_tint )
        {
            /*
             * PlayerTintingEncoder: p2Alt1 start, p2 end, p1Alt2 hue,
             * p1 saturation, p1Alt3 lightness, p1Alt3 weight -- after CHAT
             * (and TRANSPARENCY, never sent) and before HEADBARS in
             * PlayerAvatarExtendedInfoDesktopWriter's order. The deob reads
             * the same six (class109.method3804, flag 0x200) and adds the
             * client cycle to both cycles.
             */
            rsprot_p2_21(buf, ext->tint_start_cycle & 0xffff);
            rsprot_p2_12(buf, ext->tint_end_cycle & 0xffff);
            rsprot_p1_neg(buf, ext->tint_hue & 0xff);
            rsprot_p1(buf, ext->tint_saturation & 0xff);
            rsprot_p1_sub128(buf, ext->tint_lightness & 0xff);
            rsprot_p1_sub128(buf, ext->tint_weight & 0xff);
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
            rsprot_p1_add128(buf, 1 + ext->headbar_extra_count);
            rsprot_psmart1or2(buf, ext->headbar_type);
            rsprot_psmart1or2(buf, ext->headbar_duration);
            if( ext->headbar_duration != WIRE_HEADBAR_REMOVE )
            {
                rsprot_psmart1or2(buf, ext->headbar_start_delay);
                rsprot_p1_add128(buf, ext->headbar_start_fill);
                if( ext->headbar_duration > 0 )
                    rsprot_p1_neg(buf, ext->headbar_end_fill);
            }
            /* A bar per further splat, at the hitpoints after it (RSProx). */
            for( int i = 0; i < ext->headbar_extra_count; i++ )
            {
                rsprot_psmart1or2(buf, ext->headbar_extra[i].type);
                rsprot_psmart1or2(buf, 0);
                rsprot_psmart1or2(buf, ext->headbar_extra[i].start_delay);
                rsprot_p1_add128(buf, ext->headbar_extra[i].start_fill);
            }
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
            rsprot_p2_12_add128(buf, ext->seq_id < 0 ? 65535 : ext->seq_id);
            rsprot_p1(buf, ext->seq_delay);
        }
        if( has_temp_move_speed )
        {
            /* PlayerTempMoveSpeedEncoder: raw signed p1. The golden client
             * reads this with class617.method13129(), not one of the Alt byte
             * transforms. class174 value 2 makes locomotion run for this
             * staged step without changing the player's persistent default. */
            rsprot_p1(buf, ext->temp_move_speed);
        }
        if( has_appearance )
        {
            rsprot_p1(buf, (128 - appearance_len) & 0xff);
            for( int i = 0; i < appearance_len; i++ )
                rsprot_p1(buf, (appearance[i] + 128) & 0xff);
        }
        if( has_exact_move )
        {
            rsprot_p1(buf, ext->exact_start_x);
            rsprot_p1_add128(buf, ext->exact_start_z);
            rsprot_p1_neg(buf, ext->exact_end_x);
            rsprot_p1_sub128(buf, ext->exact_end_z);
            rsprot_p2_12(buf, ext->exact_start_cycle);
            rsprot_p2_21(buf, ext->exact_end_cycle);
            rsprot_p2_21_add128(buf, ext->exact_facing);
        }
    }
}

void
osrs239_player_info_write(
    struct RsprotBuf* buf,
    int local_index,
    enum WirePlayerMovement movement,
    int32_t movement_value,
    int low_res_inactive,
    const uint8_t* appearance,
    int appearance_len,
    const struct WirePlayerExt* ext)
{
    /* Rev 239 has no TRANSPARENCY block; the encoder sets it at 241 only. */
    assert(!ext || !ext->has_transparency);
    wire_player_info_write_local(
        buf, local_index, movement, movement_value, low_res_inactive,
        wire_player_info_has_extended(appearance, appearance_len, ext));
    osrs239_player_info_extended(buf, appearance, appearance_len, ext);
}

void
osrs239_player_info_write_world(
    struct RsprotBuf* buf,
    struct WirePlayerInfoState* state,
    const struct WirePlayerUpdate* updates,
    int count)
{
    const struct WirePlayerUpdate* extended[WIRE_PLAYER_SLOTS];
    int n = wire_player_info_write_sections(buf, state, updates, count, extended);

    for( int i = 0; i < n; ++i )
        osrs239_player_info_extended(
            buf, extended[i]->appearance, extended[i]->appearance_len, extended[i]->ext);
}
