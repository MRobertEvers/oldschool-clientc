/*
 * PLAYER_INFO's rev-241 extended-info tail, written by the server
 * (mock241_playerinfo.c) and read back by the client's own 241 reader
 * (net/rev/osrs241/osrs241_entity_info.c).
 *
 * The two blocks the Fractured Archive's doors need are checked field by
 * field against the deob's reader (src_osrs241_rl1_13_3):
 *
 *   FACE, angle kind   class64.method1691, kind 2: one smart, the angle;
 *                      the header's bit 6 is `instant`.
 *   TRANSPARENCY       class81, flag 0x10000: g2sAlt3 start, g2sAlt3 end,
 *                      g1sAlt1 from, g1sAlt1 to, g1Alt3 use-start
 *                      (class643.method13958 / method13948,
 *                      Statics.method13899) -> class17.method328.
 *
 * TRANSPARENCY sits after EXACT_MOVE and its flag needs the third flag byte,
 * so a writer that put it anywhere else, or forgot the continuation bit, reads
 * back as garbage in the blocks around it: the exact move rides along to prove
 * the order.
 */
#include "mem/torirs_mem.h"
#include "torirsserver/mock239_playerinfo.h"
#include "streams/rsprot_info.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <string.h>

static int g_failures;

void osrs241_playerinfo_set_local(int local_index);
void osrs241_playerinfo_init(uint8_t const* data, int len);
int osrs241_player_info_read(uint8_t const* data, int len, RsprotInfoList* out);

/* The readers write a list; these hand the test its ops, with the strings
 * in one arena the next read reuses. */
static uint8_t g_info_bytes[1 << 16];

static int
osrs241_player_info_read_ops(uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    RsprotInfoList list;

    rsprot_info_list_init(&list, ops, cap, g_info_bytes, (int)sizeof(g_info_bytes));
    return osrs241_player_info_read(data, len, &list);
}


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

/* One tick of the local player with `ext`, through the 241 reader. */
static int
roundtrip(struct Mock239PlayerInfoState* server, const struct Mock239PlayerExt* ext,
          struct RsprotInfoOp* ops, int cap)
{
    uint8_t bytes[4096];
    struct RSAreaBuf buf;
    struct Mock239PlayerUpdate update = {
        .index = LOCAL_INDEX,
        .visible = 1,
        .coord = gpi_coord(0, 3200, 3200),
        .ext = ext,
    };

    rsab_wrap(&buf, bytes, sizeof(bytes));
    mock239_playerinfo_write_world(&buf, server, &update, 1);
    CHECK(rsab_ok(&buf), "the packet stays within its buffer");
    return osrs241_player_info_read_ops(bytes, (int)rsab_len(&buf), ops, cap);
}

static struct RsprotInfoOp const*
find(struct RsprotInfoOp const* ops, int n, RsprotInfoOpKind kind)
{
    for( int i = 0; i < n; i++ )
        if( ops[i].kind == kind )
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
    int n;

    rsab_wrap(&init, init_bytes, sizeof(init_bytes));
    mock239_playerinfo_write_init(&init, LOCAL_INDEX, gpi_coord(0, 3200, 3200));
    osrs241_playerinfo_set_local(LOCAL_INDEX);
    osrs241_playerinfo_init(init_bytes, (int)rsab_len(&init));
    mock239_playerinfo_state_init(&server, LOCAL_INDEX, gpi_coord(0, 3200, 3200));
    server.wire_revision = 241;

    fprintf(stderr, "mock241-playerinfo: the Ocellus door's fade out, with an exact move\n");
    {
        struct Mock239PlayerExt ext;
        struct RsprotInfoOp const* t;
        struct RsprotInfoOp const* x;

        memset(&ext, 0, sizeof(ext));
        ext.has_exact_move = 1;
        ext.exact_start_x = -2;
        ext.exact_start_z = 3;
        ext.exact_end_x = 4;
        ext.exact_end_z = -5;
        ext.exact_start_cycle = 7;
        ext.exact_end_cycle = 11;
        ext.exact_facing = 1280;
        /* RSProx 4308 [1453]: start=30, end=60, 0 -> 255, usestart false. */
        ext.has_transparency = 1;
        ext.transparency_start_cycle = 30;
        ext.transparency_end_cycle = 60;
        ext.transparency_from = 0;
        ext.transparency_to = 255;
        ext.transparency_use_start = 0;
        n = roundtrip(&server, &ext, ops, 64);
        t = find(ops, n, RSPROT_OP_TRANSPARENCY);
        x = find(ops, n, RSPROT_OP_EXACT_MOVE);
        CHECK(t && (int16_t)t->transparency.start_cycle_delta == 30 && (int16_t)t->transparency.end_cycle_delta == 60 &&
                  (uint8_t)t->transparency.from == 0 && (uint8_t)t->transparency.to == 255 &&
                  !t->transparency.use_start,
              "transparency start/end/from/to/use-start survive (%d %d %d %d %d)",
              t ? t->transparency.start_cycle_delta : -99, t ? t->transparency.end_cycle_delta : -99,
              t ? (uint8_t)t->transparency.from : -1, t ? (uint8_t)t->transparency.to : -1,
              t ? t->transparency.use_start : -1);
        CHECK(x && x->exact_move.start_x == -2 && x->exact_move.end_z == -5 &&
                  x->exact_move.facing == 1280,
              "the exact move before it still reads (the blocks are in the reader's order)");
    }

    fprintf(stderr, "mock241-playerinfo: the fade in on arrival, and \"gone already\"\n");
    {
        struct Mock239PlayerExt ext;
        struct RsprotInfoOp const* t;

        memset(&ext, 0, sizeof(ext));
        ext.has_transparency = 1;
        ext.transparency_start_cycle = 0;
        ext.transparency_end_cycle = 30;
        ext.transparency_from = 255;
        ext.transparency_to = 0;
        ext.transparency_use_start = 1;
        n = roundtrip(&server, &ext, ops, 64);
        t = find(ops, n, RSPROT_OP_TRANSPARENCY);
        CHECK(t && (int16_t)t->transparency.start_cycle_delta == 0 && (int16_t)t->transparency.end_cycle_delta == 30 &&
                  (uint8_t)t->transparency.from == 255 && (uint8_t)t->transparency.to == 0 &&
                  t->transparency.use_start,
              "a transparency block alone (third flag byte) reads back");

        ext.transparency_start_cycle = -1;
        ext.transparency_end_cycle = -1;
        n = roundtrip(&server, &ext, ops, 64);
        t = find(ops, n, RSPROT_OP_TRANSPARENCY);
        CHECK(t && (int16_t)t->transparency.start_cycle_delta == -1 && (int16_t)t->transparency.end_cycle_delta == -1,
              "the cycles are signed: (-1, -1) survives");
    }

    fprintf(stderr, "mock241-playerinfo: a landing's face angle\n");
    {
        struct Mock239PlayerExt ext;
        struct RsprotInfoOp const* f;

        memset(&ext, 0, sizeof(ext));
        ext.has_face = 1;
        mock239_face_init(&ext.face);
        ext.face.kind = MOCK239_FACE_ANGLE;
        ext.face.angle = 1536;
        n = roundtrip(&server, &ext, ops, 64);
        f = find(ops, n, RSPROT_OP_FACE_ANGLE);
        CHECK(f && f->face_angle.angle == 1536 && !f->face_angle.instant && f->face_angle.modern,
              "angle 1536, not instant (`[facing] angle=1536`)");

        ext.face.angle = 0;
        ext.face.instant = 1;
        n = roundtrip(&server, &ext, ops, 64);
        f = find(ops, n, RSPROT_OP_FACE_ANGLE);
        CHECK(f && f->face_angle.angle == 0 && f->face_angle.instant,
              "angle 0 with instant (`[facing] angle=0, instant=true`)");
    }

    if( g_failures )
    {
        fprintf(stderr, "mock241-playerinfo: %d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "mock241-playerinfo: all checks passed\n");
    return 0;
}
