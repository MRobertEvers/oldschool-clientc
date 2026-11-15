#include "torirsserver/wire/osrs241/osrs241_player_info.h"

#include "torirsserver/wire/core/wire_facing.h"

#include <assert.h>
#include "rsprot_buf.h"

/*
 * PLAYER_INFO's extended-info tail at revision 241.
 *
 * The bit sections ahead of it are unchanged from 239, so wire_player_info.c
 * still writes them; only this tail moved. It is transcribed from the reader it
 * must satisfy -- RSProx's osrs-241 PlayerInfoClient.decodeExtendedInfoBlocks
 * -- and mirrors the client's own 241 reader (src/net/rev/osrs241/
 * osrs241_entity_info.c, player_extended) block for block.
 *
 * Flags (RSProx 241): APPEARANCE 0x2, SPOTANIM 0x4, SAY 0x10, FACE 0x20,
 * SEQUENCE 0x40, CHAT 0x400, EXACT_MOVE 0x800, TINTING 0x1000,
 * TEMP_MOVE_SPEED 0x4000, TRANSPARENCY 0x10000, HITMARKS 0x20000, HEADBARS
 * 0x80000; continuation bits 0x80 (second byte) and 0x2000 (third byte).
 *
 * Block order (RSProx 241): SAY, MOVE_SPEED, FACE, NAME_EXTRAS, SPOTANIM,
 * HEADBARS, APPEARANCE, TEMP_MOVE_SPEED, CHAT, HITMARKS, TINTING, SEQUENCE,
 * EXACT_MOVE, TRANSPARENCY, RESET, FREEZE -- of which this server writes the
 * ones it has state for.
 */
enum
{
    EXT241_APPEARANCE = 0x2,
    EXT241_SPOTANIM = 0x4,
    EXT241_FACE = 0x20,
    EXT241_SEQUENCE = 0x40,
    EXT241_NEXT_BYTE_1 = 0x80,
    EXT241_CHAT = 0x400,
    EXT241_EXACT_MOVE = 0x800,
    EXT241_TINTING = 0x1000,
    EXT241_NEXT_BYTE_2 = 0x2000,
    EXT241_TEMP_MOVE_SPEED = 0x4000,
    EXT241_TRANSPARENCY = 0x10000,
    EXT241_HITMARKS = 0x20000,
    EXT241_HEADBARS = 0x80000,
};

static void
osrs241_player_info_extended(
    struct RsprotBuf* buf,
    const uint8_t* appearance,
    int appearance_len,
    const struct WirePlayerExt* ext)
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
    int const has_transparency = ext && ext->has_transparency;
    uint32_t flag = 0;

    assert(buf);
    if( has_hit )
        flag |= EXT241_HITMARKS;
    if( has_headbar )
        flag |= EXT241_HEADBARS;
    if( has_face )
        flag |= EXT241_FACE;
    if( has_seq )
        flag |= EXT241_SEQUENCE;
    if( has_chat )
        flag |= EXT241_CHAT;
    if( has_spotanim )
        flag |= EXT241_SPOTANIM;
    if( has_temp_move_speed )
        flag |= EXT241_TEMP_MOVE_SPEED;
    if( has_exact_move )
        flag |= EXT241_EXACT_MOVE;
    if( has_tint )
        flag |= EXT241_TINTING;
    if( has_transparency )
        flag |= EXT241_TRANSPARENCY;
    if( has_appearance )
        flag |= EXT241_APPEARANCE;
    if( flag == 0 )
        return;

    /* The third-byte bit lives in the second byte, so it is set first. */
    if( flag & 0xffff0000u )
        flag |= EXT241_NEXT_BYTE_2;
    if( flag & 0xffffff00u )
        flag |= EXT241_NEXT_BYTE_1;
    rsprot_p1(buf, (int32_t)(flag & 0xff));
    if( flag & EXT241_NEXT_BYTE_1 )
        rsprot_p1(buf, (int32_t)((flag >> 8) & 0xff));
    if( flag & EXT241_NEXT_BYTE_2 )
        rsprot_p1(buf, (int32_t)((flag >> 16) & 0xff));

    /* decodeFacing: g1Alt3 header (239: g1Alt2), then Face's shared payload. */
    if( has_face )
    {
        int header = ext->face.walk_mode | ((int)ext->face.kind << 3) |
                     (ext->face.instant ? 0x40 : 0);

        rsprot_p1_sub128(buf, header);
        wire_face_write_payload(buf, &ext->face);
    }
    /* decodeSpotanims: g1Alt2 count; per entry g1Alt2 slot, g2Alt2 id,
     * g4 (height << 16 | delay), g1 loop. */
    if( has_spotanim )
    {
        assert(ext->spotanim_count > 0);
        assert(ext->spotanim_count <= WIRE_PLAYER_SPOTANIM_MAX);
        rsprot_p1_neg(buf, ext->spotanim_count);
        for( int i = 0; i < ext->spotanim_count; i++ )
        {
            struct WirePlayerSpotanim const* spot = &ext->spotanims[i];

            rsprot_p1_neg(buf, spot->slot);
            rsprot_p2_12_add128(buf, spot->id < 0 ? 65535 : spot->id);
            rsprot_p4_1234(buf, spot->height_delay);
            rsprot_p1(buf, 0); /* loop: this server's spot anims play once */
        }
    }
    /* decodeHeadbar: g1 count; smart type, smart duration, smart start delay,
     * g1Alt3 start fill, and g1Alt1 end fill when the bar has a duration. */
    if( has_headbar )
    {
        rsprot_p1(buf, 1 + ext->headbar_extra_count);
        rsprot_psmart1or2(buf, ext->headbar_type);
        rsprot_psmart1or2(buf, ext->headbar_duration);
        if( ext->headbar_duration != WIRE_HEADBAR_REMOVE )
        {
            rsprot_psmart1or2(buf, ext->headbar_start_delay);
            rsprot_p1_sub128(buf, ext->headbar_start_fill);
            if( ext->headbar_duration > 0 )
                rsprot_p1_add128(buf, ext->headbar_end_fill);
        }
        /* A bar per further splat (rev 241 only; the 239 writer sends one). */
        for( int i = 0; i < ext->headbar_extra_count; i++ )
        {
            rsprot_psmart1or2(buf, ext->headbar_extra[i].type);
            rsprot_psmart1or2(buf, 0);
            rsprot_psmart1or2(buf, ext->headbar_extra[i].start_delay);
            rsprot_p1_sub128(buf, ext->headbar_extra[i].start_fill);
        }
    }
    /* APPEARANCE: g1Alt2 length (`-n`), then the bytes REVERSED (gdataAlt1). */
    if( has_appearance )
    {
        rsprot_p1_neg(buf, appearance_len & 0xff);
        for( int i = appearance_len - 1; i >= 0; i-- )
            rsprot_p1(buf, appearance[i]);
    }
    /* decodeTemporaryMoveSpeed: g1sAlt1. */
    if( has_temp_move_speed )
        rsprot_p1_add128(buf, ext->temp_move_speed);
    /* decodeChat: g2Alt1 colour/effects, g1Alt1 mod icon, g1Alt2 autotyper,
     * g1Alt1 length, then the huffman bytes reversed (gdataAlt1). */
    if( has_chat )
    {
        rsprot_p2_21(buf, ext->chat_colour_effect);
        rsprot_p1_add128(buf, ext->chat_type);
        rsprot_p1_neg(buf, 0); /* auto-typed */
        rsprot_p1_add128(buf, ext->chat_len);
        for( int i = ext->chat_len - 1; i >= 0; i-- )
            rsprot_p1(buf, ext->chat_data[i]);
    }
    /* decodeHitmark: g1 count (239: g1Alt3), then four smarts per hit. */
    if( has_hit )
    {
        int const slots = ext->hit_slots > 0 ? ext->hit_slots : 4;
        int extra = ext->hit_extra_count;

        if( extra < 0 )
            extra = 0;
        if( extra > (int)(sizeof(ext->hit_extra) / sizeof(ext->hit_extra[0])) )
            extra = (int)(sizeof(ext->hit_extra) / sizeof(ext->hit_extra[0]));
        rsprot_p1(buf, 1 + extra);
        rsprot_psmart1or2(buf, ext->hit_type);
        rsprot_psmart1or2(buf, ext->hit_value);
        rsprot_psmart1or2(buf, ext->hit_delay);
        rsprot_psmart1or2(buf, slots);
        for( int i = 0; i < extra; i++ )
        {
            rsprot_psmart1or2(buf, ext->hit_extra[i].type);
            rsprot_psmart1or2(buf, ext->hit_extra[i].value);
            rsprot_psmart1or2(buf, ext->hit_extra[i].delay);
            rsprot_psmart1or2(buf, slots);
        }
    }
    /* decodeTinting: g2 start, g2Alt1 end, g1sAlt2 hue, g1s saturation,
     * g1sAlt2 lightness, g1Alt3 weight. */
    if( has_tint )
    {
        rsprot_p2_12(buf, ext->tint_start_cycle & 0xffff);
        rsprot_p2_21(buf, ext->tint_end_cycle & 0xffff);
        rsprot_p1_neg(buf, ext->tint_hue & 0xff);
        rsprot_p1(buf, ext->tint_saturation & 0xff);
        rsprot_p1_neg(buf, ext->tint_lightness & 0xff);
        rsprot_p1_sub128(buf, ext->tint_weight & 0xff);
    }
    /* decodeSequence: g2Alt2 id, g1Alt1 delay. */
    if( has_seq )
    {
        rsprot_p2_12_add128(buf, ext->seq_id < 0 ? 65535 : ext->seq_id);
        rsprot_p1_add128(buf, ext->seq_delay);
    }
    /* decodeExactMove: g1sAlt1 dx1, g1s dz1, g1sAlt1 dx2, g1sAlt3 dz2,
     * g2Alt1 delay1, g2Alt2 delay2, g2Alt2 direction. */
    if( has_exact_move )
    {
        rsprot_p1_add128(buf, ext->exact_start_x);
        rsprot_p1(buf, ext->exact_start_z);
        rsprot_p1_add128(buf, ext->exact_end_x);
        rsprot_p1_sub128(buf, ext->exact_end_z);
        rsprot_p2_21(buf, ext->exact_start_cycle);
        rsprot_p2_12_add128(buf, ext->exact_end_cycle);
        rsprot_p2_12_add128(buf, ext->exact_facing);
    }
    /* decodeTransparency (deob class81, flag 0x10000): g2sAlt3 start and
     * g2sAlt3 end (each added to the client cycle), g1sAlt1 from, g1sAlt1 to,
     * g1Alt3 use-start (class643.method13958 / method13948,
     * Statics.method13899), handed to class17.method328. */
    if( has_transparency )
    {
        rsprot_p2_21_add128(buf, ext->transparency_start_cycle & 0xffff);
        rsprot_p2_21_add128(buf, ext->transparency_end_cycle & 0xffff);
        rsprot_p1_add128(buf, ext->transparency_from & 0xff);
        rsprot_p1_add128(buf, ext->transparency_to & 0xff);
        rsprot_p1_sub128(buf, ext->transparency_use_start ? 1 : 0);
    }
}

void
osrs241_player_info_write_world(
    struct RsprotBuf* buf,
    struct WirePlayerInfoState* state,
    const struct WirePlayerUpdate* updates,
    int count)
{
    const struct WirePlayerUpdate* extended[WIRE_PLAYER_SLOTS];
    int n = wire_player_info_write_sections(buf, state, updates, count, extended);

    for( int i = 0; i < n; ++i )
        osrs241_player_info_extended(
            buf, extended[i]->appearance, extended[i]->appearance_len, extended[i]->ext);
}
