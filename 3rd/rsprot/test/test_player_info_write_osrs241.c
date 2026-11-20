/*
 * PLAYER_INFO's rev-241 extended-info tail, written by the server
 * (player_info_write_osrs241.c) and read back by the client's own 241 reader
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
#include "streams/player_info_write.h"
#include "streams/rsprot_info.h"
#include "streams/npc_info.h"
#include "streams/player_info.h"

#include "rsprot_buf.h"

#include <stdio.h>
#include <string.h>

/* The client's reader state: one player table, as the client's executor holds
 * it (net/proto/proto_info.c wraps the same three calls). */
static RsprotPlayerInfo g_pi;

static int g_failures;

/* The client's 241 readers: the revision's own stream entry points over the
 * player table g_pi, as the client's executor binds them. */
static void
osrs241_playerinfo_set_local(int local_index)
{
    rsprot_player_info_reset(&g_pi, local_index);
}

static void
osrs241_playerinfo_init(uint8_t const* data, int len)
{
    rsprot_player_info_init(&g_pi, data, len);
}

static int
osrs241_player_info_read(uint8_t const* data, int len, RsprotInfoList* out)
{
    return player_info_osrs241(&g_pi, data, len, out);
}

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
roundtrip(struct RsprotPlayerInfoWriter* server, const struct RsprotPlayerExt* ext,
          struct RsprotInfoOp* ops, int cap)
{
    uint8_t bytes[4096];
    struct RsprotBuf buf;
    struct RsprotPlayerUpdate update = {
        .index = LOCAL_INDEX,
        .visible = 1,
        .coord = gpi_coord(0, 3200, 3200),
        .ext = ext,
    };

    rsprot_buf_wrap(&buf, bytes, sizeof(bytes));
    player_info_osrs241_write(&buf, server, &update, 1);
    CHECK(rsprot_buf_ok(&buf), "the packet stays within its buffer");
    return osrs241_player_info_read_ops(bytes, (int)buf.wpos, ops, cap);
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
    struct RsprotPlayerInfoWriter server;
    struct RsprotInfoOp ops[64];
    /* 30 + 2047 * 18 bits = 4608 bytes; a 4096-byte array here was read
     * 512 bytes past its end for as long as the buffer let a cursor run on. */
    uint8_t init_bytes[4608];
    struct RsprotBuf init;
    int n;

    rsprot_buf_wrap(&init, init_bytes, sizeof(init_bytes));
    rsprot_player_info_write_init(&init, LOCAL_INDEX, gpi_coord(0, 3200, 3200));
    osrs241_playerinfo_set_local(LOCAL_INDEX);
    CHECK(rsprot_buf_ok(&init) && init.wpos == 4608, "the init block fits its buffer (%d bytes)",
          (int)init.wpos);
    osrs241_playerinfo_init(init_bytes, (int)init.wpos);
    rsprot_player_info_writer_init(&server, LOCAL_INDEX, gpi_coord(0, 3200, 3200));

    fprintf(stderr, "osrs241-player-info: the Ocellus door's fade out, with an exact move\n");
    {
        struct RsprotPlayerExt ext;
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

    fprintf(stderr, "osrs241-player-info: the fade in on arrival, and \"gone already\"\n");
    {
        struct RsprotPlayerExt ext;
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

    fprintf(stderr, "osrs241-player-info: a landing's face angle\n");
    {
        struct RsprotPlayerExt ext;
        struct RsprotInfoOp const* f;

        memset(&ext, 0, sizeof(ext));
        ext.has_face = 1;
        rsprot_face_init(&ext.face);
        ext.face.kind = RSPROT_FACE_KIND_ANGLE;
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

    /* The login's first update (RSProx index 4378 [0]): move speed, facing,
     * name extras, player reset -- here with a transparency between, so the
     * reset's place after it is read back too. */
    fprintf(stderr, "osrs241-player-info: the login's name extras and reset\n");
    {
        struct RsprotPlayerExt ext;
        struct RsprotInfoOp const* f;

        memset(&ext, 0, sizeof(ext));
        ext.has_move_speed = 1;
        ext.move_speed = 2;
        ext.has_face = 1;
        rsprot_face_init(&ext.face);
        ext.face.kind = RSPROT_FACE_KIND_ANGLE;
        ext.face.angle = 0;
        ext.has_name_extras = 1;
        ext.name_before = "<col=ff0000>";
        ext.name_after = "</col>";
        ext.name_after_level = " (skill 9)";
        ext.has_transparency = 1;
        ext.transparency_start_cycle = 5;
        ext.transparency_end_cycle = 9;
        ext.transparency_to = 200;
        ext.has_player_reset = 1;
        ext.player_reset = 7;
        n = roundtrip(&server, &ext, ops, 64);
        f = find(ops, n, RSPROT_OP_NAME_EXTRAS);
        CHECK(f && strcmp(f->name_extras.before_name, "<col=ff0000>") == 0 &&
                  strcmp(f->name_extras.after_name, "</col>") == 0 &&
                  strcmp(f->name_extras.after_combat_level, " (skill 9)") == 0,
              "name extras (%s|%s|%s)", f ? f->name_extras.before_name : "-",
              f ? f->name_extras.after_name : "-", f ? f->name_extras.after_combat_level : "-");
        f = find(ops, n, RSPROT_OP_TRANSPARENCY);
        CHECK(f && (uint8_t)f->transparency.to == 200, "transparency before the reset");
        f = find(ops, n, RSPROT_OP_RESET);
        CHECK(f && f->value == 7, "player reset (%d)", f ? (int)f->value : -1);
        f = find(ops, n, RSPROT_OP_MOVE_SPEED);
        CHECK(f && f->value == 2, "move speed beside them");
        /* The reset puts the player's traversal back to walk BEFORE the
         * record's blocks (deob 241 class81.method2126: flag 0x8 first, the
         * move speed's field806 after), so the run the same record sends is
         * what the slot keeps, though 241 writes the reset after it. */
        CHECK(g_pi.move_speed[LOCAL_INDEX] == RSPROT_TRAVERSAL_RUN,
              "the slot keeps the record's run past its reset (%d)", (int)g_pi.move_speed[LOCAL_INDEX]);
    }

    if( g_failures )
    {
        fprintf(stderr, "osrs241-player-info: %d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "osrs241-player-info: all checks passed\n");
    return 0;
}
