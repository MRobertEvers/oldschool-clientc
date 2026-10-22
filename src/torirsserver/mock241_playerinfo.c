#include "mock241_playerinfo.h"

#include "mock239_facing.h"

#include <assert.h>
#include <rsareabuf.h>

/*
 * PLAYER_INFO's extended-info tail at revision 241.
 *
 * The bit sections ahead of it are unchanged from 239, so mock239_playerinfo.c
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

void
mock241_write_player_extended(
    struct RSAreaBuf* buf,
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
    rsab_p1(buf, (int32_t)(flag & 0xff));
    if( flag & EXT241_NEXT_BYTE_1 )
        rsab_p1(buf, (int32_t)((flag >> 8) & 0xff));
    if( flag & EXT241_NEXT_BYTE_2 )
        rsab_p1(buf, (int32_t)((flag >> 16) & 0xff));

    /* decodeFacing: g1Alt3 header (239: g1Alt2), then Face's shared payload. */
    if( has_face )
    {
        int header = ext->face.walk_mode | ((int)ext->face.kind << 3) |
                     (ext->face.instant ? 0x40 : 0);

        rsab_p1_alt3(buf, header);
        mock239_face_write_payload(buf, &ext->face);
    }
    /* decodeSpotanims: g1Alt2 count; per entry g1Alt2 slot, g2Alt2 id,
     * g4 (height << 16 | delay), g1 loop. */
    if( has_spotanim )
    {
        assert(ext->spotanim_count > 0);
        assert(ext->spotanim_count <= MOCK239_PLAYER_SPOTANIM_MAX);
        rsab_p1_alt2(buf, ext->spotanim_count);
        for( int i = 0; i < ext->spotanim_count; i++ )
        {
            struct Mock239PlayerSpotanim const* spot = &ext->spotanims[i];

            rsab_p1_alt2(buf, spot->slot);
            rsab_p2_alt2(buf, spot->id < 0 ? 65535 : spot->id);
            rsab_p4(buf, spot->height_delay);
            rsab_p1(buf, 0); /* loop: this server's spot anims play once */
        }
    }
    /* decodeHeadbar: g1 count; smart type, smart duration, smart start delay,
     * g1Alt3 start fill, and g1Alt1 end fill when the bar has a duration. */
    if( has_headbar )
    {
        rsab_p1(buf, 1);
        rsab_psmart(buf, ext->headbar_type);
        rsab_psmart(buf, ext->headbar_duration);
        rsab_psmart(buf, ext->headbar_start_delay);
        rsab_p1_alt3(buf, ext->headbar_start_fill);
        if( ext->headbar_duration > 0 )
            rsab_p1_alt1(buf, ext->headbar_end_fill);
    }
    /* APPEARANCE: g1Alt2 length (`-n`), then the bytes REVERSED (gdataAlt1). */
    if( has_appearance )
    {
        rsab_p1_alt2(buf, appearance_len & 0xff);
        for( int i = appearance_len - 1; i >= 0; i-- )
            rsab_p1(buf, appearance[i]);
    }
    /* decodeTemporaryMoveSpeed: g1sAlt1. */
    if( has_temp_move_speed )
        rsab_p1_alt1(buf, ext->temp_move_speed);
    /* decodeChat: g2Alt1 colour/effects, g1Alt1 mod icon, g1Alt2 autotyper,
     * g1Alt1 length, then the huffman bytes reversed (gdataAlt1). */
    if( has_chat )
    {
        rsab_p2_alt1(buf, ext->chat_colour_effect);
        rsab_p1_alt1(buf, ext->chat_type);
        rsab_p1_alt2(buf, 0); /* auto-typed */
        rsab_p1_alt1(buf, ext->chat_len);
        for( int i = ext->chat_len - 1; i >= 0; i-- )
            rsab_p1(buf, ext->chat_data[i]);
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
        rsab_p1(buf, 1 + extra);
        rsab_psmart(buf, ext->hit_type);
        rsab_psmart(buf, ext->hit_value);
        rsab_psmart(buf, ext->hit_delay);
        rsab_psmart(buf, slots);
        for( int i = 0; i < extra; i++ )
        {
            rsab_psmart(buf, ext->hit_extra[i].type);
            rsab_psmart(buf, ext->hit_extra[i].value);
            rsab_psmart(buf, ext->hit_extra[i].delay);
            rsab_psmart(buf, slots);
        }
    }
    /* decodeTinting: g2 start, g2Alt1 end, g1sAlt2 hue, g1s saturation,
     * g1sAlt2 lightness, g1Alt3 weight. */
    if( has_tint )
    {
        rsab_p2(buf, ext->tint_start_cycle & 0xffff);
        rsab_p2_alt1(buf, ext->tint_end_cycle & 0xffff);
        rsab_p1_alt2(buf, ext->tint_hue & 0xff);
        rsab_p1(buf, ext->tint_saturation & 0xff);
        rsab_p1_alt2(buf, ext->tint_lightness & 0xff);
        rsab_p1_alt3(buf, ext->tint_weight & 0xff);
    }
    /* decodeSequence: g2Alt2 id, g1Alt1 delay. */
    if( has_seq )
    {
        rsab_p2_alt2(buf, ext->seq_id < 0 ? 65535 : ext->seq_id);
        rsab_p1_alt1(buf, ext->seq_delay);
    }
    /* decodeExactMove: g1sAlt1 dx1, g1s dz1, g1sAlt1 dx2, g1sAlt3 dz2,
     * g2Alt1 delay1, g2Alt2 delay2, g2Alt2 direction. */
    if( has_exact_move )
    {
        rsab_p1_alt1(buf, ext->exact_start_x);
        rsab_p1(buf, ext->exact_start_z);
        rsab_p1_alt1(buf, ext->exact_end_x);
        rsab_p1_alt3(buf, ext->exact_end_z);
        rsab_p2_alt1(buf, ext->exact_start_cycle);
        rsab_p2_alt2(buf, ext->exact_end_cycle);
        rsab_p2_alt2(buf, ext->exact_facing);
    }
    /* decodeTransparency (deob class81, flag 0x10000): g2sAlt3 start and
     * g2sAlt3 end (each added to the client cycle), g1sAlt1 from, g1sAlt1 to,
     * g1Alt3 use-start (class643.method13958 / method13948,
     * Statics.method13899), handed to class17.method328. */
    if( has_transparency )
    {
        rsab_p2_alt3(buf, ext->transparency_start_cycle & 0xffff);
        rsab_p2_alt3(buf, ext->transparency_end_cycle & 0xffff);
        rsab_p1_alt1(buf, ext->transparency_from & 0xff);
        rsab_p1_alt1(buf, ext->transparency_to & 0xff);
        rsab_p1_alt3(buf, ext->transparency_use_start ? 1 : 0);
    }
}
