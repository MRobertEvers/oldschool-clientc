/*
 * PLAYER_INFO's rev-242 extended-info tail, written by the server
 * (mock242_playerinfo.c) and read back by the client's 242 reader (RSProt's
 * player_info_osrs242, from RSProx 8c514346's PlayerInfoClient).
 *
 * One update carries every block the server writes, so a block in the wrong
 * place, a wrong continuation bit or a wrong transform reads back as garbage
 * in that block or the ones after it. Each value is chosen so a transform
 * slip (+128, negation, byte order) changes it.
 */
#include "mem/torirs_mem.h"
#include "torirsserver/mock239_playerinfo.h"
#include "streams/rsprot_info.h"
#include "net/proto/proto_info.h"
#include "streams/npc_info.h"
#include "streams/player_info.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <string.h>

static int g_failures;
static uint8_t g_info_bytes[1 << 16];

#define CHECK(cond, ...)                                                                   \
    do                                                                                     \
    {                                                                                      \
        fprintf(stderr, (cond) ? "  ok   " : "  FAIL ");                                   \
        fprintf(stderr, __VA_ARGS__);                                                      \
        fprintf(stderr, "\n");                                                             \
        if( !(cond) )                                                                      \
            g_failures++;                                                                  \
    } while( 0 )

#define LOCAL_INDEX 42

static int32_t
gpi_coord(int level, int x, int z)
{
    return (int32_t)(((level & 3) << 28) | ((x & 16383) << 14) | (z & 16383));
}

static int
roundtrip(struct Mock239PlayerInfoState* server, const struct Mock239PlayerExt* ext,
          uint8_t const* appearance, int appearance_len, struct RsprotInfoOp* ops, int cap)
{
    uint8_t bytes[4096];
    struct RSAreaBuf buf;
    RsprotInfoList list;
    struct Mock239PlayerUpdate update = {
        .index = LOCAL_INDEX,
        .visible = 1,
        .coord = gpi_coord(0, 3200, 3200),
        .ext = ext,
        .appearance = appearance,
        .appearance_len = appearance_len,
    };

    rsab_wrap(&buf, bytes, sizeof(bytes));
    mock239_playerinfo_write_world(&buf, server, &update, 1);
    CHECK(rsab_ok(&buf), "the packet stays within its buffer");
    rsprot_info_list_init(&list, ops, cap, g_info_bytes, (int)sizeof(g_info_bytes));
    return proto_info_player_read(player_info_osrs242, bytes, (int)rsab_len(&buf), &list);
}

static struct RsprotInfoOp const*
find_nth(struct RsprotInfoOp const* ops, int n, RsprotInfoOpKind kind, int nth)
{
    for( int i = 0; i < n; i++ )
        if( ops[i].kind == kind && nth-- == 0 )
            return &ops[i];
    return NULL;
}

int
main(void)
{
    struct Mock239PlayerInfoState server;
    struct RsprotInfoOp ops[64];
    uint8_t init_bytes[4096];
    struct RSAreaBuf init;
    struct Mock239PlayerExt ext;
    static uint8_t const appearance[] = { 0x00, 0x01, 0x83, 0xff, 0x10, 0x7f };
    static uint8_t const chat[] = { 0x11, 0x92, 0x03, 0xf4 };
    struct RsprotInfoOp const* op;
    int n;

    rsab_wrap(&init, init_bytes, sizeof(init_bytes));
    mock239_playerinfo_write_init(&init, LOCAL_INDEX, gpi_coord(0, 3200, 3200));
    proto_info_set_local(LOCAL_INDEX);
    proto_info_init(init_bytes, (int)rsab_len(&init));
    mock239_playerinfo_state_init(&server, LOCAL_INDEX, gpi_coord(0, 3200, 3200));
    server.wire_revision = 242;

    memset(&ext, 0, sizeof(ext));
    ext.has_exact_move = 1;
    ext.exact_start_x = -2;
    ext.exact_start_z = 3;
    ext.exact_end_x = 4;
    ext.exact_end_z = -5;
    ext.exact_start_cycle = 7;
    ext.exact_end_cycle = 300;
    ext.exact_facing = 1280;
    ext.has_tint = 1;
    ext.tint_start_cycle = 2;
    ext.tint_end_cycle = 400;
    ext.tint_hue = 10;
    ext.tint_saturation = -20;
    ext.tint_lightness = 30;
    ext.tint_weight = 99;
    ext.has_seq = 1;
    ext.seq_id = 1234;
    ext.seq_delay = 5;
    ext.has_hit = 1;
    ext.hit_type = 1;
    ext.hit_value = 17;
    ext.hit_delay = 3;
    ext.hit_extra_count = 1;
    ext.hit_extra[0].type = 2;
    ext.hit_extra[0].value = 300;
    ext.hit_extra[0].delay = 9;
    ext.has_spotanim = 1;
    ext.spotanim_count = 1;
    ext.spotanims[0].slot = 3;
    ext.spotanims[0].id = 1500;
    ext.spotanims[0].height_delay = (92 << 16) | 25;
    ext.has_headbar = 1;
    ext.headbar_type = 0;
    ext.headbar_duration = 40;
    ext.headbar_start_delay = 6;
    ext.headbar_start_fill = 20;
    ext.headbar_end_fill = 200;
    ext.headbar_extra_count = 1;
    ext.headbar_extra[0].type = 0;
    ext.headbar_extra[0].start_delay = 9;
    ext.headbar_extra[0].start_fill = 150;
    ext.has_transparency = 1;
    ext.transparency_start_cycle = 30;
    ext.transparency_end_cycle = -1;
    ext.transparency_from = 0;
    ext.transparency_to = 255;
    ext.transparency_use_start = 1;
    ext.has_chat = 1;
    ext.chat_colour_effect = 0x0201;
    ext.chat_type = 2;
    ext.chat_data = chat;
    ext.chat_len = (int)sizeof(chat);
    ext.has_face = 1;
    mock239_face_init(&ext.face);
    ext.face.kind = MOCK239_FACE_ANGLE;
    ext.face.angle = 1536;
    ext.has_temp_move_speed = 1;
    ext.temp_move_speed = 2;

    fprintf(stderr, "mock242-playerinfo: every block the server writes, in one update\n");
    n = roundtrip(&server, &ext, appearance, (int)sizeof(appearance), ops, 64);

    /* The first delay (the server's start cycle) arrives as end_cycle_delta:
     * the world names the end of the glide onto the start tile `move_end`
     * (world_cycle.c, World_UpdateExactMove). */
    op = find_nth(ops, n, RSPROT_OP_EXACT_MOVE, 0);
    CHECK(op && op->exact_move.start_x == -2 && op->exact_move.start_z == 3 &&
              op->exact_move.end_x == 4 && op->exact_move.end_z == -5 &&
              op->exact_move.end_cycle_delta == 7 && op->exact_move.start_cycle_delta == 300 &&
              op->exact_move.facing == 1280,
          "exact move (%d %d %d %d %d %d %d)", op ? op->exact_move.start_x : 0,
          op ? op->exact_move.start_z : 0, op ? op->exact_move.end_x : 0,
          op ? op->exact_move.end_z : 0, op ? op->exact_move.start_cycle_delta : 0,
          op ? op->exact_move.end_cycle_delta : 0, op ? op->exact_move.facing : 0);
    op = find_nth(ops, n, RSPROT_OP_TINT, 0);
    CHECK(op && op->tint.start_cycle_delta == 2 && op->tint.end_cycle_delta == 400 &&
              (int8_t)op->tint.hue == 10 && (int8_t)op->tint.saturation == -20 &&
              (int8_t)op->tint.lightness == 30 && (uint8_t)op->tint.weight == 99,
          "tinting (%d %d %d %d)", op ? op->tint.hue : 0, op ? op->tint.saturation : 0,
          op ? op->tint.lightness : 0, op ? op->tint.weight : 0);
    op = find_nth(ops, n, RSPROT_OP_SEQUENCE, 0);
    CHECK(op && op->sequence.sequence_id == 1234 && op->sequence.delay == 5, "sequence");
    op = find_nth(ops, n, RSPROT_OP_DAMAGE, 0);
    CHECK(op && op->damage.damage_type == 1 && op->damage.damage == 17 && op->damage.delay == 3,
          "first hitmark");
    op = find_nth(ops, n, RSPROT_OP_DAMAGE, 1);
    CHECK(op && op->damage.damage_type == 2 && op->damage.damage == 300 && op->damage.delay == 9,
          "second hitmark");
    op = find_nth(ops, n, RSPROT_OP_SPOTANIM, 0);
    CHECK(op && op->spotanim.slot == 3 && op->spotanim.spotanim_id == 1500 &&
              op->spotanim.height_delay == ((92 << 16) | 25) && !op->spotanim.loop,
          "spot anim");
    op = find_nth(ops, n, RSPROT_OP_HEADBAR, 0);
    CHECK(op && op->headbar.type == 0 && op->headbar.duration == 40 &&
              op->headbar.start_delay == 6 && op->headbar.start_fill == 20 &&
              op->headbar.end_fill == 200,
          "first headbar");
    op = find_nth(ops, n, RSPROT_OP_HEADBAR, 1);
    CHECK(op && op->headbar.start_delay == 9 && op->headbar.start_fill == 150, "second headbar");
    op = find_nth(ops, n, RSPROT_OP_APPEARANCE, 0);
    CHECK(op && op->appearance.len == (int)sizeof(appearance) &&
              memcmp(op->appearance.appearance, appearance, sizeof(appearance)) == 0,
          "appearance bytes in order");
    op = find_nth(ops, n, RSPROT_OP_TRANSPARENCY, 0);
    CHECK(op && (int16_t)op->transparency.start_cycle_delta == 30 &&
              (int16_t)op->transparency.end_cycle_delta == -1 &&
              (uint8_t)op->transparency.from == 0 && (uint8_t)op->transparency.to == 255 &&
              op->transparency.use_start,
          "transparency");
    op = find_nth(ops, n, RSPROT_OP_CHAT, 0);
    CHECK(op && op->chat.colour_effect == 0x0201 && op->chat.type == 2 &&
              op->chat.length == sizeof(chat) && memcmp(op->chat.data, chat, sizeof(chat)) == 0,
          "chat");
    op = find_nth(ops, n, RSPROT_OP_FACE_ANGLE, 0);
    CHECK(op && op->face_angle.angle == 1536 && !op->face_angle.instant, "face angle");
    op = find_nth(ops, n, RSPROT_OP_TEMP_MOVE_SPEED, 0);
    CHECK(op && op->value == 2, "temporary move speed");

    if( g_failures )
    {
        fprintf(stderr, "mock242-playerinfo: %d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "mock242-playerinfo: all checks passed\n");
    return 0;
}
