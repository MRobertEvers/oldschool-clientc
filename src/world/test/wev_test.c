/*
 * wev_test — SAILING_PLAN C1 gate: the WORLDENTITY_INFO delta reader against
 * hand-computed byte sequences, the class458 interpolator against
 * hand-computed segments (including the 2047→1 wraparound arc), the footprint
 * corner bake, and the WevConfig decode against archive 72 of the real
 * cache.osrs239 (skipped with a loud message when the cache is absent — the
 * synthetic decode checks still run).
 *
 *   make -C src test-wev            (runs from src/, cache at ../cache.osrs239)
 */
#include "world/wev.h"

#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"

/* rscache (synchronous disk read — same pattern as engine/test/seal_rig_scan.c) */
#include "dat2disk.h"
#include "datatypes/dat2_configs.h"
#include "filelist.h"
#include "revisions/revisions.h"
#include "rscache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* assert() with the sentence attached (same shape as rs_gameproto_exec_test):
 * aborts on the first failure, because a half-run gate proves nothing. */
#define TEST_WEV_ASSERT(cond, msg)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

/* ------------------------------------------------------------------ */
/* WORLDENTITY_INFO parse: the 2-bit-per-axis delta reader              */
/* ------------------------------------------------------------------ */

static void
test_parse_deltas(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    struct RevPacket p;

    /* Op 2 (enqueue), mask 0x61 = dx:i8 | dz:i16<<4 | dangle:i8<<6.
     * dx = 5, dz = 0xFED4 = -300, dangle = 0xF8 = -8; updateFlags 0x1 with
     * seq 0x0102 = 258, delay 7. Hand-laid, read back exactly. */
    uint8_t move[] = { 0x01, 0x02, 0x61, 0x05, 0xFE, 0xD4, 0xF8, 0x01, 0x01, 0x02, 0x07 };

    memset(&p, 0, sizeof(p));
    TEST_WEV_ASSERT(
        rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, move, (int)sizeof(move), &p),
        "enqueue-op packet parses");
    TEST_WEV_ASSERT(p._worldentity_info.count == 1, "one update");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].op == PKT_WEV_OP_ENQUEUE, "op is enqueue");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].dx == 5, "dx i8 positive");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].dy == 0, "dy code 0 reads nothing");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].dz == -300, "dz i16 sign-extends");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].dangle == -8, "dangle i8 sign-extends");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].update_flags == 0x1, "updateFlags kept");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].has_seq == 1, "bit 0x1 announces seq");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].seq_id == 258, "seq id u16 be");
    TEST_WEV_ASSERT(p._worldentity_info.updates[0].seq_delay == 7, "seq delay u8");
    TEST_WEV_ASSERT(p._worldentity_info.spawn_count == 0, "no spawn trailer");

    /* A byte short must fail as a whole, not apply a half-read update. */
    memset(&p, 0, sizeof(p));
    TEST_WEV_ASSERT(
        !rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, move, (int)sizeof(move) - 1, &p),
        "truncated enqueue packet rejected");

    /* A trailing byte is a malformed spawn trailer, also a whole-packet drop. */
    {
        uint8_t padded[sizeof(move) + 1];

        memcpy(padded, move, sizeof(move));
        padded[sizeof(move)] = 0x00;
        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            !rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, padded, (int)sizeof(padded), &p),
            "trailing garbage rejected");
    }

    /* Op 3 (snap) with an i32 dx (code 3): 0x00010000 = 65536. */
    {
        uint8_t snap[] = { 0x01, 0x03, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00 };

        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, snap, (int)sizeof(snap), &p),
            "snap-op packet parses");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].op == PKT_WEV_OP_SNAP, "op is snap");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].dx == 65536, "dx i32 be");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].dz == 0, "dz code 0");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].has_seq == 0, "flags 0 carries no seq");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].seq_id == -1, "no-seq default is -1");
        TEST_WEV_ASSERT(
            p._worldentity_info.updates[0].has_op_mask == 0, "flags 0 carries no op mask");
        TEST_WEV_ASSERT(
            p._worldentity_info.updates[0].op_mask == PKT_WEV_OP_MASK_ALL,
            "absent op mask defaults to all five");
    }

    /* Despawn (0) reads NOTHING past its op byte — not even a flags byte
     * (deob Statics.method977 guards the whole record on `op != 0`). Reading
     * one here would eat the next record's op and desync the walk. Op 1 is
     * flags-only; the 65535 seq id is the wire's "clear", surfaced as -1. */
    {
        uint8_t mixed[] = { 0x02, 0x00, 0x01, 0x01, 0xFF, 0xFF, 0x00 };

        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, mixed, (int)sizeof(mixed), &p),
            "despawn + flags-only packet parses");
        TEST_WEV_ASSERT(p._worldentity_info.count == 2, "two updates");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].op == PKT_WEV_OP_DESPAWN, "op 0");
        TEST_WEV_ASSERT(
            p._worldentity_info.updates[0].update_flags == 0, "despawn reads no flags byte");
        TEST_WEV_ASSERT(p._worldentity_info.updates[1].op == PKT_WEV_OP_FLAGS, "op 1");
        TEST_WEV_ASSERT(p._worldentity_info.updates[1].has_seq == 1, "seq payload present");
        TEST_WEV_ASSERT(p._worldentity_info.updates[1].seq_id == -1, "65535 clears to -1");
    }

    /* updateFlags bit 0x2 is the 5-bit op-enabled mask (deob field5694) read
     * through (128 - b) & 0xFF, and it precedes the bit-0x1 seq payload.
     * b = 0x61 (97) -> 128 - 97 = 31, every op enabled. */
    {
        uint8_t opmask[] = { 0x01, 0x01, 0x03, 0x61, 0x01, 0x02, 0x07 };

        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, opmask, (int)sizeof(opmask), &p),
            "flags 0x3 packet parses");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].has_op_mask == 1, "bit 0x2 announces mask");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].op_mask == 31, "(128 - 97) & 0xFF");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].has_seq == 1, "bit 0x1 still read after");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].seq_id == 258, "seq id follows the mask");
        TEST_WEV_ASSERT(p._worldentity_info.updates[0].seq_delay == 7, "seq delay last");
    }

    /* An op past 3 and a count past the registry are protocol violations the
     * parse layer rejects whole. */
    {
        uint8_t bad_op[] = { 0x01, 0x04, 0x00 };
        uint8_t bad_count[] = { 0x10 };

        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            !rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, bad_op, (int)sizeof(bad_op), &p),
            "op 4 rejected");
        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            !rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, bad_count, (int)sizeof(bad_count), &p),
            "count 16 rejected");
    }

    printf("ok - WORLDENTITY_INFO delta reader (i8/i16/i32, signs, seq, bounds)\n");
}

static void
test_parse_spawn_trailer(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    struct RevPacket p;

    /*
     * count 0, then two trailer records in the deob's field order (id g2be,
     * updateFlags u8, sizeByte through (-b) & 0xFF, ownerTypeIndex through
     * (b - 128) & 0xFF, configId as a SIGNED LITTLE-ENDIAN u16, the absolute
     * transform, and only then the updateFlags payload):
     *
     *   id 3, flags 0, size byte 0xDF → 0x21 (2x8 by 1x8 tiles), priority
     *     byte 0x81 → 1, config 05 00 → 5, mask 0x22 (dx:i16, dz:i16),
     *     x 0x1900 = 6400, z 0x3200 = 12800; no flag payload;
     *   id 4, flags 0x1, size byte 0xEF → 0x11, priority byte 0x82 → 2,
     *     config 01 00 → 1, mask 0xC0 (dangle:i32), dangle 0x00000802 = 2050
     *     → wraps to 2, then the trailing seq 13428 delay 5.
     */
    uint8_t body[] = {
        0x00,                               /* count */
        0x00, 0x03, 0x00, 0xDF, 0x81,       /* id, flags, size, priority */
        0x05, 0x00,                         /* config (le) */
        0x22, 0x19, 0x00, 0x32, 0x00,       /* mask, x, z */
        0x00, 0x04, 0x01, 0xEF, 0x82,       /* id, flags, size, priority */
        0x01, 0x00,                         /* config (le) */
        0xC0, 0x00, 0x00, 0x08, 0x02,       /* mask, dangle */
        0x34, 0x74, 0x05,                   /* flag payload: seq, delay */
    };

    memset(&p, 0, sizeof(p));
    TEST_WEV_ASSERT(
        rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, body, (int)sizeof(body), &p),
        "spawn trailer parses");
    TEST_WEV_ASSERT(p._worldentity_info.count == 0, "no updates");
    TEST_WEV_ASSERT(p._worldentity_info.spawn_count == 2, "two spawns");

    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].id == 3, "spawn 0 id");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].update_flags == 0, "spawn 0 flags");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].size_x_tiles == 16, "high nibble x8 tiles");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].size_z_tiles == 8, "low nibble x8 tiles");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].priority_group == 1, "spawn 0 priority");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].config_id == 5, "spawn 0 config");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].x == 6400, "spawn 0 x from i16 delta");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].z == 12800, "spawn 0 z from i16 delta");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[0].angle == 0, "spawn 0 angle default 0");

    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].id == 4, "spawn 1 id");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].has_seq == 1, "spawn 1 seq present");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].seq_id == 13428, "spawn 1 seq id");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].seq_delay == 5, "spawn 1 seq delay");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].size_x_tiles == 8, "spawn 1 size x");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].size_z_tiles == 8, "spawn 1 size z");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].priority_group == 2, "spawn 1 priority");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].config_id == 1, "spawn 1 config");
    TEST_WEV_ASSERT(p._worldentity_info.spawns[1].angle == 2, "spawn angle wraps & 0x7FF");
    TEST_WEV_ASSERT(
        p._worldentity_info.spawns[1].op_mask == PKT_WEV_OP_MASK_ALL,
        "flags 0x1 leaves the op mask at its default");

    /* Truncating the last delta byte must reject the whole packet. */
    memset(&p, 0, sizeof(p));
    TEST_WEV_ASSERT(
        !rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, body, (int)sizeof(body) - 1, &p),
        "truncated spawn trailer rejected");

    /* Id 0 (the root) can never spawn as a world entity. */
    {
        uint8_t bad_id[] = { 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x01, 0x00 };

        memset(&p, 0, sizeof(p));
        TEST_WEV_ASSERT(
            !rev->parse(rev, PKT_NAME_WORLDENTITY_INFO, bad_id, (int)sizeof(bad_id), &p),
            "spawn id 0 rejected");
    }

    printf("ok - WORLDENTITY_INFO spawn trailer (sizes, absolute transform, seq)\n");
}

/* ------------------------------------------------------------------ */
/* Interpolator (class458)                                             */
/* ------------------------------------------------------------------ */

static void
test_interpolator(void)
{
    struct WevConfig cfg;
    struct Wevs wevs;
    struct Wev* w;

    WevConfig_Init(&cfg, 1);
    Wevs_Init(&wevs);

    /*
     * Linear x: 0 → 1280, armed at cycle 0 and landing at cycle 30.
     *
     * The window is [arm - 1, enqueue + 30] — 31 cycles wide, not 30 (deob
     * class458.method10184 sets its start a cycle in the past). So the very
     * first step is already 1/31 of the way along, and cycle 15 is 16/31, not
     * a half. These are the deob's numbers; a clean 0 / 640 would mean the
     * window had silently been re-derived.
     */
    w = Wevs_Spawn(&wevs, 1, WORLDVIEW_ROOT, &cfg, 1, 0, 0, 0, 0, 0);
    Wev_ApplyMove(w, 1280, 0, 0, 0, false, 0.0);
    Wev_Interpolate(w, 0.0);
    TEST_WEV_ASSERT(w->x == 1280 * 1 / 31, "first step is 1/31 in, not 0");
    Wev_Interpolate(w, 15.0);
    TEST_WEV_ASSERT(w->x == 1280 * 16 / 31, "cycle 15 is 16/31 of the window");
    Wev_Interpolate(w, 30.0);
    TEST_WEV_ASSERT(w->x == 1280, "segment lands exactly on its target");
    TEST_WEV_ASSERT(w->queue_count == 0, "landed segment drops the pending count");
    /* Nothing pending: rest on slot 0 however late the clock runs. */
    Wev_Interpolate(w, 500.0);
    TEST_WEV_ASSERT(w->x == 1280, "drained queue rests on the current target");

    /* Snap chains off the current target and teleports, no segment. */
    Wev_ApplyMove(w, 100, 0, 50, 10, true, 40.0);
    TEST_WEV_ASSERT(w->x == 1380, "snap x immediate");
    TEST_WEV_ASSERT(w->z == 50, "snap z immediate");
    TEST_WEV_ASSERT(w->angle == 10, "snap angle immediate");
    TEST_WEV_ASSERT(w->queue_count == 0, "snap leaves no segment");
    /* A delta after a snap chains off where the snap put it. */
    Wev_ApplyMove(w, 20, 0, 0, 0, false, 41.0);
    TEST_WEV_ASSERT(w->queue[0].x == 1400, "snap target is the next delta's base");

    /* Wraparound 2047 → 1: d = (1-2047) & 0x7FF = 2, so the arc crosses 0
     * instead of sweeping 2046 units backwards. Halfway through a 31-cycle
     * window (16/31 of an arc of 2) is one unit, i.e. exactly 0. */
    w = Wevs_Spawn(&wevs, 2, WORLDVIEW_ROOT, &cfg, 1, 0, 0, 2047, 0, 0);
    Wev_ApplyMove(w, 0, 0, 0, 2, false, 0.0);
    TEST_WEV_ASSERT(w->queue[0].angle == 1, "target = (2047+2) & 0x7FF");
    Wev_Interpolate(w, 0.0);
    Wev_Interpolate(w, 15.0);
    TEST_WEV_ASSERT(w->angle == 0, "midpoint of 2047→1 is 0, through the wrap");
    Wev_Interpolate(w, 30.0);
    TEST_WEV_ASSERT(w->angle == 1, "wraparound arc lands on 1");

    /* Reverse arc 1 → 2047: d = 2046 > 1024 → -2, crossing 0 downwards. */
    w = Wevs_Spawn(&wevs, 3, WORLDVIEW_ROOT, &cfg, 1, 0, 0, 1, 0, 0);
    Wev_ApplyMove(w, 0, 0, 0, -2, false, 0.0);
    TEST_WEV_ASSERT(w->queue[0].angle == 2047, "target = (1-2) & 0x7FF");
    Wev_Interpolate(w, 0.0);
    Wev_Interpolate(w, 15.0);
    TEST_WEV_ASSERT(w->angle == 0, "midpoint of 1→2047 is 0, through the wrap");
    Wev_Interpolate(w, 30.0);
    TEST_WEV_ASSERT(w->angle == 2047, "reverse arc lands on 2047");

    /*
     * Two segments. Deltas chain off the NEWEST queued target, and the arm
     * SNAPSHOTS its target: a segment already in flight finishes on the
     * target it was armed with, and only the next arm sees the newer one
     * (deob class458.method10184 copies the transform, it does not alias it).
     */
    w = Wevs_Spawn(&wevs, 4, WORLDVIEW_ROOT, &cfg, 1, 0, 0, 0, 0, 0);
    Wev_ApplyMove(w, 128, 0, 0, 0, false, 0.0);
    Wev_Interpolate(w, 0.0); /* arms toward 128 over [-1, 30] */
    Wev_ApplyMove(w, 128, 0, 0, 0, false, 30.0);
    TEST_WEV_ASSERT(w->queue_count == 2, "two pending segments");
    TEST_WEV_ASSERT(w->queue[0].x == 256, "second delta chains off the first target");
    TEST_WEV_ASSERT(w->queue[1].x == 128, "the rotate pushed the first target down a slot");
    Wev_Interpolate(w, 30.0);
    TEST_WEV_ASSERT(w->x == 128, "the armed segment lands on the target it was armed with");
    TEST_WEV_ASSERT(w->queue_count == 1, "one segment paid off");
    /* Re-armed at 45 over [44, 30 + 30]: half of that window is cycle 52. */
    Wev_Interpolate(w, 45.0);
    Wev_Interpolate(w, 52.0);
    TEST_WEV_ASSERT(w->x == 192, "second segment at half of its own window");
    Wev_Interpolate(w, 60.0);
    TEST_WEV_ASSERT(w->x == 256, "second segment lands");
    TEST_WEV_ASSERT(w->queue_count == 0, "queue drained");

    /*
     * A tenth pending segment is dropped, not asserted (deob
     * class467.method10446 caps field5691 at 9). The count saturates and the
     * oldest entry falls off the ring, but slot 0 — the destination — always
     * survives, so the boat still ends up where the server put it.
     */
    w = Wevs_Spawn(&wevs, 5, WORLDVIEW_ROOT, &cfg, 1, 0, 0, 0, 0, 0);
    for( int i = 0; i < 12; i++ )
        Wev_ApplyMove(w, 128, 0, 0, 0, false, (double)(i * 30));
    TEST_WEV_ASSERT(w->queue_count == WEV_TARGET_PENDING_MAX, "pending saturates at nine");
    TEST_WEV_ASSERT(w->queue[0].x == 128 * 12, "every delta still chained; nothing was lost");

    /*
     * ...and the backlog does not replay the stale path at 30 cycles a
     * segment. Every remaining segment is armed against the same slot-0
     * target with a window that expired long ago, so nine frames drain nine
     * segments and the hull is already at the destination on the first.
     */
    Wev_Interpolate(w, 400.0);
    TEST_WEV_ASSERT(w->x == 128 * 12, "an expired window lands immediately");
    TEST_WEV_ASSERT(w->queue_count == WEV_TARGET_PENDING_MAX - 1, "one drained per frame");
    for( int i = 1; i < WEV_TARGET_PENDING_MAX; i++ )
        Wev_Interpolate(w, 400.0 + (double)i);
    TEST_WEV_ASSERT(w->queue_count == 0, "backlog drained");
    TEST_WEV_ASSERT(w->x == 128 * 12, "and never left the destination");

    printf("ok - interpolator (linear, wraparound both ways, snap, backlog cap, drain)\n");
}

/* Encodes all four arguments into the returned height so the assertions below
 * pin which view each hull was sampled against, not just that the hook ran. */
static int
test_height_fn(
    void* userdata,
    int view_id,
    int world_x,
    int world_z,
    int level)
{
    (void)userdata;
    (void)world_z;
    return world_x / 10 + level * 1000 + view_id * 100000;
}

static void
test_frame_driver(void)
{
    struct WevConfig cfg;
    struct Wevs wevs;
    struct Wev* boat;
    struct Wev* nested;

    WevConfig_Init(&cfg, 1);
    cfg.plane = 1;
    Wevs_Init(&wevs);

    /* A boat under the root and a nested entity under the boat's view: the
     * worklist must reach both, and each height comes from the hook. */
    boat = Wevs_Spawn(&wevs, 5, WORLDVIEW_ROOT, &cfg, 1, 1000, 0, 0, 0, 0);
    nested = Wevs_Spawn(&wevs, 6, 5, &cfg, 1, 2000, 0, 0, 0, 0);
    Wev_ApplyMove(boat, 300, 0, 0, 0, false, 0.0);

    Wevs_Frame(&wevs, 15.0, test_height_fn, NULL);
    TEST_WEV_ASSERT(wevs.clock == 15.0, "clock advances by frame_cycles");
    /* Armed on this very frame, so the window is [14, 30] and one cycle of it
     * has passed: 1000 + 300/16 = 1018. */
    TEST_WEV_ASSERT(boat->x == 1018, "driver interpolates the boat");
    TEST_WEV_ASSERT(boat->y == 1101, "boat height sampled against the ROOT view");
    TEST_WEV_ASSERT(nested->x == 2000, "nested entity reached through the worklist");
    TEST_WEV_ASSERT(nested->y == 501200, "nested height sampled against its carrier's view");

    boat->y = -12345;
    Wevs_Frame(&wevs, 15.0, test_height_fn, NULL);
    TEST_WEV_ASSERT(wevs.clock == 30.0, "clock keeps accumulating");
    TEST_WEV_ASSERT(boat->x == 1300, "second frame lands the segment");
    TEST_WEV_ASSERT(boat->y == 1130, "height re-sampled every frame, never interpolated");

    printf("ok - per-frame driver (worklist, clock, terrain hook)\n");
}

/* ------------------------------------------------------------------ */
/* Config decode + corner bake                                         */
/* ------------------------------------------------------------------ */

static void
test_config_decode_synthetic(void)
{
    struct WevConfig cfg;

    /* One record exercising every field family. */
    uint8_t rec[] = {
        2,  1,                            /* plane */
        4,  0xFF, 0xC0,                   /* pivot_x = -64 */
        6,  0x02, 0x00,                   /* bounds_w = 512 */
        7,  0x01, 0x00,                   /* bounds_h = 256 */
        12, 'S', 'h', 'i', 'p', 0,        /* name */
        14,                               /* parameterless flag */
        15, 'B', 'o', 'a', 'r', 'd', 0,   /* ops[0] */
        20, 0x09, 0x87,                   /* category = 2439 */
        25, 0x34, 0x74,                   /* anim = 13428 */
        0,
    };

    TEST_WEV_ASSERT(
        WevConfig_Decode(&cfg, 7, rec, (int)sizeof(rec)), "synthetic record decodes clean");
    TEST_WEV_ASSERT(cfg.id == 7, "id kept");
    TEST_WEV_ASSERT(cfg.plane == 1, "plane");
    TEST_WEV_ASSERT(cfg.pivot_x == -64, "pivot_x signed");
    TEST_WEV_ASSERT(cfg.bounds_w == 512, "bounds_w");
    TEST_WEV_ASSERT(cfg.bounds_h == 256, "bounds_h");
    TEST_WEV_ASSERT(cfg.name && strcmp(cfg.name, "Ship") == 0, "name string");
    TEST_WEV_ASSERT(cfg.flag14, "op 14 is a flag, not a string");
    TEST_WEV_ASSERT(cfg.ops[0] && strcmp(cfg.ops[0], "Board") == 0, "op 15 is ops[0]");
    TEST_WEV_ASSERT(cfg.category == 2439, "category");
    TEST_WEV_ASSERT(cfg.anim_id == 13428, "anim id");
    TEST_WEV_ASSERT(cfg.click_mode == 2, "click mode defaults to 2");
    TEST_WEV_ASSERT(cfg.flat_hsl == WEV_FLAT_HSL_DEFAULT, "flat HSL default 39188");

    /* Corner bake off those bounds: margin[0]=256 → half-extents 512 x 384.
     * Bucket 0 is the unrotated box; bucket 4 (512 units = 90°) swaps the
     * axes. Corner 2 is (+x,+z) pre-rotation. */
    TEST_WEV_ASSERT(cfg.corner_x[0][0][0] == -512, "bucket 0 corner 0 x");
    TEST_WEV_ASSERT(cfg.corner_z[0][0][0] == -384, "bucket 0 corner 0 z");
    TEST_WEV_ASSERT(cfg.corner_x[0][0][2] == 512, "bucket 0 corner 2 x");
    TEST_WEV_ASSERT(cfg.corner_z[0][0][2] == 384, "bucket 0 corner 2 z");
    TEST_WEV_ASSERT(cfg.corner_x[0][4][2] == -384, "90-degree bucket rotates x");
    TEST_WEV_ASSERT(cfg.corner_z[0][4][2] == 512, "90-degree bucket rotates z");
    /* Largest margin inflates, never shrinks. */
    TEST_WEV_ASSERT(cfg.corner_x[2][0][2] == 512 + (362 - 256), "margin 2 wider");
    WevConfig_FreeContents(&cfg);

    /* An unknown opcode stops the decode and reports where — and clears the
     * id, so WevConfigTable_Has reports the record absent instead of handing
     * out a half-decoded boat built from default dimensions. */
    {
        uint8_t bad[] = { 2, 1, 99, 0x12, 0x34, 0 };

        TEST_WEV_ASSERT(
            !WevConfig_Decode(&cfg, 8, bad, (int)sizeof(bad)), "unknown opcode fails the decode");
        TEST_WEV_ASSERT(cfg._consumed == 2, "consumed stops before the unknown opcode");
        TEST_WEV_ASSERT(cfg.id == -1, "a failed decode reads back as absent");
        WevConfig_FreeContents(&cfg);
    }

    /* Same for a record that runs off the end without its opcode-0 terminator. */
    {
        uint8_t unterminated[] = { 2, 1, 6, 0x02, 0x00 };

        TEST_WEV_ASSERT(
            !WevConfig_Decode(&cfg, 9, unterminated, (int)sizeof(unterminated)),
            "unterminated record fails the decode");
        TEST_WEV_ASSERT(cfg.id == -1, "unterminated record reads back as absent");
        WevConfig_FreeContents(&cfg);
    }

    printf("ok - WevConfig synthetic decode + corner bake\n");
}

static void
test_config_decode_cache(char const* cache_dir)
{
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int clean = 0;
    int pinned = 0;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(
            stderr,
            "\n*** SKIP: no dat2 cache at %s — the archive-72 WevConfig decode was NOT "
            "verified against real data. Run from src/ with ../cache.osrs239 present. ***\n\n",
            cache_dir);
        return;
    }
    /* Same identity step as app.c / seal_rig_scan: without it the disk answers
     * ABSENT for every logical table. */
    {
        struct RSCache profile =
            RSCache_ProfileForIdentity(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239, 0u);
        RSCache_Dat2DiskSetProfile(disk, &profile);
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(
        disk,
        RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
        RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY);
    if( !archive )
    {
        fprintf(
            stderr,
            "\n*** SKIP: cache at %s has no config group 72 (pre-sailing cache?) — the "
            "WevConfig decode was NOT verified against real data. ***\n\n",
            cache_dir);
        RSCache_Dat2DiskFree(disk);
        return;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    TEST_WEV_ASSERT(files != NULL, "group 72 splits into files");
    TEST_WEV_ASSERT(archive->file_count == 14, "cache.osrs239 carries 14 world entities");

    for( int i = 0; i < files->file_count; i++ )
    {
        struct WevConfig cfg;
        int id = archive->file_ids ? archive->file_ids[i] : i;

        if( !files->files[i] || files->file_sizes[i] <= 0 )
            continue;
        TEST_WEV_ASSERT(
            WevConfig_Decode(
                &cfg, id, (uint8_t const*)files->files[i], files->file_sizes[i]),
            "every archive-72 record decodes clean (full consumption)");
        clean++;

        /* Pins hand-verified against the raw bytes (probe_archive72). */
        if( id == 4 )
        {
            TEST_WEV_ASSERT(cfg.plane == 0, "wev 4 plane");
            TEST_WEV_ASSERT(cfg.bounds_off_x == 512, "wev 4 off x");
            TEST_WEV_ASSERT(cfg.bounds_off_z == 512, "wev 4 off z");
            TEST_WEV_ASSERT(cfg.click_mode == 1, "wev 4 explicit click mode");
            pinned++;
        }
        else if( id == 5 )
        {
            TEST_WEV_ASSERT(cfg.name && strcmp(cfg.name, "Ship") == 0, "wev 5 name");
            TEST_WEV_ASSERT(cfg.flag14, "wev 5 flag14");
            TEST_WEV_ASSERT(cfg.plane == 1, "wev 5 plane");
            TEST_WEV_ASSERT(cfg.pivot_x == -64, "wev 5 pivot x (signed u16)");
            TEST_WEV_ASSERT(cfg.pivot_z == 512, "wev 5 pivot z");
            TEST_WEV_ASSERT(cfg.bounds_off_x == 384, "wev 5 off x");
            TEST_WEV_ASSERT(cfg.bounds_off_z == 1152, "wev 5 off z");
            TEST_WEV_ASSERT(cfg.anim_id == 13428, "wev 5 anim");
            TEST_WEV_ASSERT(cfg.op26 == 7291, "wev 5 op26");
            pinned++;
        }
        else if( id == 9 )
        {
            TEST_WEV_ASSERT(cfg.name && strcmp(cfg.name, "The Zenith") == 0, "wev 9 name");
            TEST_WEV_ASSERT(cfg.ops[0] && strcmp(cfg.ops[0], "Board") == 0, "wev 9 Board op");
            TEST_WEV_ASSERT(cfg.op24 == 1, "wev 9 op24");
            TEST_WEV_ASSERT(cfg.op26 == 7292, "wev 9 op26");
            TEST_WEV_ASSERT(cfg.pivot_z == 192, "wev 9 pivot z");
            pinned++;
        }
        else if( id == 13 )
        {
            TEST_WEV_ASSERT(
                cfg.name && strcmp(cfg.name, "The Bark of the Bight") == 0, "wev 13 name");
            TEST_WEV_ASSERT(cfg.bounds_h == -256, "wev 13 negative bounds (0xff00)");
            TEST_WEV_ASSERT(cfg.ops[1] && strcmp(cfg.ops[1], "Attack") == 0, "wev 13 Attack op");
            TEST_WEV_ASSERT(cfg.category == 2439, "wev 13 category");
            TEST_WEV_ASSERT(cfg.op26 == 7290, "wev 13 op26");
            pinned++;
        }
        WevConfig_FreeContents(&cfg);
    }
    TEST_WEV_ASSERT(clean == 14, "all 14 records decoded");
    TEST_WEV_ASSERT(pinned == 4, "all 4 pinned records were present");

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    printf("ok - archive-72 WevConfig decode against %s (14 records, 4 pinned)\n", cache_dir);
}

/**
 * The pseudo-loc footprint the painter takes its extent from. The property that
 * matters is that it is a function of HEADING, not a constant: a hull pinned to
 * a fixed extent under-covers at some angles and scenery beside it then sorts
 * in front.
 */
static void
test_footprint_tiles(void)
{
    struct WevConfig cfg;
    struct Wev wev;
    /* bounds 512 x 256, offsets 0; margin[0] = 256 => half-extents 512 x 384. */
    uint8_t rec[] = {
        2, 0,             /* plane */
        6, 0x02, 0x00,    /* bounds_w = 512 */
        7, 0x01, 0x00,    /* bounds_h = 256 */
        0,
    };
    int mx;
    int mz;
    int sx;
    int sz;

    WevConfig_Init(&cfg, -1);
    TEST_WEV_ASSERT(
        WevConfig_Decode(&cfg, 3, rec, (int)sizeof(rec)), "footprint fixture decodes");

    memset(&wev, 0, sizeof(wev));
    wev.config = &cfg;

    /* Unrotated: x spans [-512,512] -> tiles -4..4, z spans [-384,384] -> -3..3. */
    wev.angle = 0;
    Wev_FootprintTiles(&wev, 0, &mx, &mz, &sx, &sz);
    TEST_WEV_ASSERT(mx == -4 && sx == 9, "heading 0: 9 tiles across the long axis");
    TEST_WEV_ASSERT(mz == -3 && sz == 7, "heading 0: 7 tiles across the short axis");

    /* 90 degrees (angle 512, bucket 4) swaps them. A fixed footprint cannot be
     * right at both headings, which is the whole point of recomputing it. */
    wev.angle = 512;
    Wev_FootprintTiles(&wev, 0, &mx, &mz, &sx, &sz);
    TEST_WEV_ASSERT(sx == 7 && sz == 9, "heading 90: the axes swap");

    /* Off-axis is strictly wider than either: the diagonal reaches further. */
    {
        int dx;
        int dz;

        wev.angle = 256;
        Wev_FootprintTiles(&wev, 0, &mx, &mz, &dx, &dz);
        TEST_WEV_ASSERT(dx > 9, "heading 45 is wider in x than either axis-aligned pose");
        TEST_WEV_ASSERT(dz > 9, "heading 45 is wider in z than either axis-aligned pose");
    }

    /* Every heading keeps the entity's own tile inside the box - the painter
     * descends on the tile the boat is standing on. */
    for( int a = 0; a < 2048; a += 64 )
    {
        wev.angle = a;
        Wev_FootprintTiles(&wev, 0, &mx, &mz, &sx, &sz);
        TEST_WEV_ASSERT(mx <= 0 && 0 < mx + sx, "the entity tile is inside the box in x");
        TEST_WEV_ASSERT(mz <= 0 && 0 < mz + sz, "the entity tile is inside the box in z");
    }

    /* Translating by whole tiles shifts the box and leaves its extent alone. */
    wev.angle = 0;
    wev.x = 128 * 5;
    wev.z = 128 * 2;
    Wev_FootprintTiles(&wev, 0, &mx, &mz, &sx, &sz);
    TEST_WEV_ASSERT(mx == -4 + 5 && sx == 9, "a whole-tile move shifts x, not the extent");
    TEST_WEV_ASSERT(mz == -3 + 2 && sz == 7, "a whole-tile move shifts z, not the extent");

    /* Negative absolute coordinates floor. >>7 gives -7 here; a /128 would
     * truncate to -6 and mis-seed the box by a whole tile. */
    wev.x = -128 * 3;
    wev.z = 0;
    Wev_FootprintTiles(&wev, 0, &mx, &mz, &sx, &sz);
    TEST_WEV_ASSERT(mx == -7 && sx == 9, "a negative origin floors instead of truncating");

    /* A wider margin never shrinks the box. */
    {
        int wide_sx;
        int wide_sz;

        wev.x = 0;
        wev.angle = 0;
        Wev_FootprintTiles(&wev, 0, &mx, &mz, &sx, &sz);
        Wev_FootprintTiles(&wev, WEV_FOOTPRINT_MARGINS - 1, &mx, &mz, &wide_sx, &wide_sz);
        TEST_WEV_ASSERT(wide_sx >= sx && wide_sz >= sz, "a wider margin never shrinks the box");
    }

    WevConfig_FreeContents(&cfg);
    printf("ok - rotated footprint tiles (heading, floor, margin ordering)\n");
}

int
main(int argc, char** argv)
{
    char const* cache_dir = argc > 1 ? argv[1] : "../cache.osrs239";

    test_parse_deltas();
    test_parse_spawn_trailer();
    test_interpolator();
    test_frame_driver();
    test_config_decode_synthetic();
    test_footprint_tiles();
    test_config_decode_cache(cache_dir);

    printf("wev_test: all passed\n");
    return 0;
}
