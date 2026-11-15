#include "torirsserver/wire/osrs242/osrs242_player_info.h"

#include "torirsserver/wire/core/wire_facing.h"

#include <assert.h>
#include "rsprot_buf.h"

/*
 * PLAYER_INFO's extended-info tail at revision 242.
 *
 * The bit sections ahead of it are 241's (RSProt player_info version 6 covers
 * 240..242), so wire_player_info.c still writes them. The tail is
 * transcribed from the reader it must satisfy, RSProt's player_info osrs242
 * (3rd/rsprot/spec/streams/player_info.ini, from RSProx 8c514346's
 * PlayerInfoClient): new flag bits, continuation bits, block order and nearly
 * every transform, and the appearance blob forwards.
 *
 * Flags: FACING 0x1, SPOTANIMS 0x8, SEQUENCE 0x10, APPEARANCE 0x40,
 * TEMP_MOVE_SPEED 0x100, CHAT 0x800, EXACT_MOVE 0x2000, TINTING 0x8000,
 * TRANSPARENCY 0x10000, HEADBARS 0x80000, HITMARKS 0x100000; continuation bits
 * 0x4 (second byte) and 0x1000 (third byte).
 *
 * Block order: EXACT_MOVE, NAME_EXTRAS, TINTING, FREEZE, PLAYER_RESET,
 * MOVE_SPEED, SEQUENCE, HITMARK, SPOTANIMS, HEADBAR, APPEARANCE, TRANSPARENCY,
 * CHAT, SAY, FACING, TEMP_MOVE_SPEED -- of which this server writes the ones it
 * has state for.
 */
enum
{
    EXT242_FACE = 0x1,
    EXT242_NEXT_BYTE_1 = 0x4,
    EXT242_SPOTANIM = 0x8,
    EXT242_SEQUENCE = 0x10,
    EXT242_APPEARANCE = 0x40,
    EXT242_TEMP_MOVE_SPEED = 0x100,
    EXT242_CHAT = 0x800,
    EXT242_NEXT_BYTE_2 = 0x1000,
    EXT242_EXACT_MOVE = 0x2000,
    EXT242_TINTING = 0x8000,
    EXT242_TRANSPARENCY = 0x10000,
    EXT242_HEADBARS = 0x80000,
    EXT242_HITMARKS = 0x100000,
};

static void
osrs242_player_info_extended(
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
        flag |= EXT242_HITMARKS;
    if( has_headbar )
        flag |= EXT242_HEADBARS;
    if( has_face )
        flag |= EXT242_FACE;
    if( has_seq )
        flag |= EXT242_SEQUENCE;
    if( has_chat )
        flag |= EXT242_CHAT;
    if( has_spotanim )
        flag |= EXT242_SPOTANIM;
    if( has_temp_move_speed )
        flag |= EXT242_TEMP_MOVE_SPEED;
    if( has_exact_move )
        flag |= EXT242_EXACT_MOVE;
    if( has_tint )
        flag |= EXT242_TINTING;
    if( has_transparency )
        flag |= EXT242_TRANSPARENCY;
    if( has_appearance )
        flag |= EXT242_APPEARANCE;
    if( flag == 0 )
        return;

    /* The third-byte bit lives in the second byte, so it is set first. */
    if( flag & 0xffff0000u )
        flag |= EXT242_NEXT_BYTE_2;
    if( flag & 0xffffff00u )
        flag |= EXT242_NEXT_BYTE_1;
    rsprot_p1(buf, (int32_t)(flag & 0xff));
    if( flag & EXT242_NEXT_BYTE_1 )
        rsprot_p1(buf, (int32_t)((flag >> 8) & 0xff));
    if( flag & EXT242_NEXT_BYTE_2 )
        rsprot_p1(buf, (int32_t)((flag >> 16) & 0xff));

    /* exact_move: g1s dx1, g1sAlt3 dz1, g1sAlt3 dx2, g1sAlt3 dz2, g2Alt1
     * delay1, g2Alt2 delay2, g2Alt3 direction. */
    if( has_exact_move )
    {
        rsprot_p1(buf, ext->exact_start_x);
        rsprot_p1_sub128(buf, ext->exact_start_z);
        rsprot_p1_sub128(buf, ext->exact_end_x);
        rsprot_p1_sub128(buf, ext->exact_end_z);
        rsprot_p2_21(buf, ext->exact_start_cycle);
        rsprot_p2_12_add128(buf, ext->exact_end_cycle);
        rsprot_p2_21_add128(buf, ext->exact_facing);
    }
    /* tinting: g2Alt1 start, g2Alt3 end, g1sAlt1 hue, g1sAlt1 saturation,
     * g1sAlt3 lightness, g1Alt2 weight. */
    if( has_tint )
    {
        rsprot_p2_21(buf, ext->tint_start_cycle & 0xffff);
        rsprot_p2_21_add128(buf, ext->tint_end_cycle & 0xffff);
        rsprot_p1_add128(buf, ext->tint_hue & 0xff);
        rsprot_p1_add128(buf, ext->tint_saturation & 0xff);
        rsprot_p1_sub128(buf, ext->tint_lightness & 0xff);
        rsprot_p1_neg(buf, ext->tint_weight & 0xff);
    }
    /* sequence: g2 id, g1Alt3 delay. */
    if( has_seq )
    {
        rsprot_p2_12(buf, ext->seq_id < 0 ? 65535 : ext->seq_id);
        rsprot_p1_sub128(buf, ext->seq_delay);
    }
    /* hitmark: g1 count, then four smarts per hit. */
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
    /* spotanims: g1 count; per entry g1Alt1 slot, g2Alt3 id, g4Alt3
     * (height << 16 | delay), g1 loop. */
    if( has_spotanim )
    {
        assert(ext->spotanim_count > 0);
        assert(ext->spotanim_count <= WIRE_PLAYER_SPOTANIM_MAX);
        rsprot_p1(buf, ext->spotanim_count);
        for( int i = 0; i < ext->spotanim_count; i++ )
        {
            struct WirePlayerSpotanim const* spot = &ext->spotanims[i];

            rsprot_p1_add128(buf, spot->slot);
            rsprot_p2_21_add128(buf, spot->id < 0 ? 65535 : spot->id);
            rsprot_p4_2143(buf, spot->height_delay);
            rsprot_p1(buf, 0); /* loop: this server's spot anims play once */
        }
    }
    /* headbar: g1Alt2 count; smart type, smart duration, smart start delay,
     * g1Alt3 start fill, and g1Alt2 end fill when the bar has a duration. */
    if( has_headbar )
    {
        rsprot_p1_neg(buf, 1 + ext->headbar_extra_count);
        rsprot_psmart1or2(buf, ext->headbar_type);
        rsprot_psmart1or2(buf, ext->headbar_duration);
        if( ext->headbar_duration != WIRE_HEADBAR_REMOVE )
        {
            rsprot_psmart1or2(buf, ext->headbar_start_delay);
            rsprot_p1_sub128(buf, ext->headbar_start_fill);
            if( ext->headbar_duration > 0 )
                rsprot_p1_neg(buf, ext->headbar_end_fill);
        }
        for( int i = 0; i < ext->headbar_extra_count; i++ )
        {
            rsprot_psmart1or2(buf, ext->headbar_extra[i].type);
            rsprot_psmart1or2(buf, 0);
            rsprot_psmart1or2(buf, ext->headbar_extra[i].start_delay);
            rsprot_p1_sub128(buf, ext->headbar_extra[i].start_fill);
        }
    }
    /* appearance: g1Alt2 length (`-n`), then the bytes forwards (gdata; 241
     * reversed them). */
    if( has_appearance )
    {
        rsprot_p1_neg(buf, appearance_len & 0xff);
        for( int i = 0; i < appearance_len; i++ )
            rsprot_p1(buf, appearance[i]);
    }
    /* transparency: g2sAlt3 start, g2s end (each added to the client cycle),
     * g1sAlt1 from, g1sAlt3 to, g1Alt1 use-start. */
    if( has_transparency )
    {
        rsprot_p2_21_add128(buf, ext->transparency_start_cycle & 0xffff);
        rsprot_p2_12(buf, ext->transparency_end_cycle & 0xffff);
        rsprot_p1_add128(buf, ext->transparency_from & 0xff);
        rsprot_p1_sub128(buf, ext->transparency_to & 0xff);
        rsprot_p1_add128(buf, ext->transparency_use_start ? 1 : 0);
    }
    /* chat: g2 colour/effects, g1Alt2 mod icon, g1 autotyper, g1Alt2 length,
     * then the huffman bytes reversed, each +128. */
    if( has_chat )
    {
        rsprot_p2_12(buf, ext->chat_colour_effect);
        rsprot_p1_neg(buf, ext->chat_type);
        rsprot_p1(buf, 0); /* auto-typed */
        rsprot_p1_neg(buf, ext->chat_len);
        for( int i = ext->chat_len - 1; i >= 0; i-- )
            rsprot_p1(buf, (ext->chat_data[i] + 128) & 0xff);
    }
    /* facing: g1Alt3 header, then Face's shared payload. */
    if( has_face )
    {
        int header = ext->face.walk_mode | ((int)ext->face.kind << 3) |
                     (ext->face.instant ? 0x40 : 0);

        rsprot_p1_sub128(buf, header);
        wire_face_write_payload(buf, &ext->face);
    }
    /* temporary move speed: g1sAlt3. */
    if( has_temp_move_speed )
        rsprot_p1_sub128(buf, ext->temp_move_speed);
}

void
osrs242_player_info_write_world(
    struct RsprotBuf* buf,
    struct WirePlayerInfoState* state,
    const struct WirePlayerUpdate* updates,
    int count)
{
    const struct WirePlayerUpdate* extended[WIRE_PLAYER_SLOTS];
    int n = wire_player_info_write_sections(buf, state, updates, count, extended);

    for( int i = 0; i < n; ++i )
        osrs242_player_info_extended(
            buf, extended[i]->appearance, extended[i]->appearance_len, extended[i]->ext);
}
