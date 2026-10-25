/*
 * PLAYER_INFO, revisions 227-241 -- the stateful half.
 *
 * The four bit sections and the movement encodings are RSProx's
 * PlayerInfoClient.decodeBitCodes / getHighResolutionPlayerPosition /
 * getLowResolutionPlayerPosition, which have one shape at every revision this
 * library carries (tools/rsprox_gen_info.py would not have to touch this file
 * for a new revision that kept them). The port follows the reader this project
 * verified against recordings at 239 and 241
 * (src/net/rev/osrs241/osrs241_entity_info.c); the extended-info tail after the
 * sections is rsprot_info_blocks.c, driven by the layout's tables -- which
 * player_info.c (generated) passes in, one reader per layout. Both write the op
 * list (rsprot_info.h).
 */
#include "rsprot_info.h"
#include "rsprot_info_internal.h"

#include <assert.h>
#include <string.h>

#define CUR_CYCLE_INACTIVE 0x1
#define NEXT_CYCLE_INACTIVE 0x2

/* The 8 walk and 16 run directions, as (dx, dz). */
static const int8_t k_walk_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int8_t k_walk_dz[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
static const int8_t k_run_dx[16] = { -2, -1, 0, 1, 2, -2, 2, -2, 2, -2, 2, -2, -1, 0, 1, 2 };
static const int8_t k_run_dz[16] = { -2, -2, -2, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 2, 2, 2 };

void
rsprot_player_info_reset(RsprotPlayerInfo *pi, int local_index)
{
    assert(pi);
    memset(pi, 0, sizeof(*pi));
    pi->local_index = local_index;
}

/*
 * The GPI init block: 30 bits of absolute coordinate for the local player, then
 * 18 bits of rough position (level << 16 | x << 8 | z) for every other slot in
 * index order. The local slot is skipped, so the count is structural.
 */
void
rsprot_player_info_init(RsprotPlayerInfo *pi, const uint8_t *data, int32_t len)
{
    RsprotBuf b;
    int local;

    assert(pi);
    local = pi->local_index;
    memset(pi, 0, sizeof(*pi));
    pi->local_index = local;
    pi->seeded = 1;
    if (local <= 0 || local >= RSPROT_INFO_PLAYER_SLOTS || !data || len <= 0)
        return;

    rsprot_buf_wrap_read(&b, data, len);
    rsprot_buf_bit_begin(&b);
    pi->coord[local] = rsprot_gbits(&b, 30);
    pi->high_res[local] = 1;
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++) {
        int packed;

        if (idx == local)
            continue;
        packed = rsprot_gbits(&b, 18);
        pi->low_res_pos[idx] =
            (int32_t)((((packed >> 8) & 0xff) << 14) + (packed & 0xff) + ((packed >> 16) << 28));
    }
    rsprot_buf_bit_end(&b);
}

typedef struct Reader
{
    RsprotPlayerInfo *pi;
    RsprotInfoOut out;
    uint8_t cycle_high[RSPROT_INFO_PLAYER_SLOTS];
    int extended[RSPROT_INFO_PLAYER_SLOTS];
    int extended_count;
} Reader;

static void
queue_extended(Reader *r, int idx)
{
    if (r->extended_count < RSPROT_INFO_PLAYER_SLOTS)
        r->extended[r->extended_count++] = idx;
}

static void
moved(Reader *r, int idx, int how)
{
    RsprotInfoOp *op = rsprot_info_emit(&r->out, RSPROT_IO_PLAYER_MOVE, idx);

    op->move.coord = r->pi->coord[idx];
    op->move.how = how;
}

static int
read_stationary(RsprotBuf *b)
{
    switch (rsprot_gbits(b, 2)) {
    case 0: return 0;
    case 1: return rsprot_gbits(b, 5);
    case 2: return rsprot_gbits(b, 8);
    default: return rsprot_gbits(b, 11);
    }
}

/* getLowResolutionPlayerPosition. Returns 1 when the slot entered high
 * resolution (which is also what sets its next-cycle bit). */
static int
low_res(Reader *r, RsprotBuf *b, int idx)
{
    RsprotPlayerInfo *pi = r->pi;
    int opcode = rsprot_gbits(b, 2);
    int32_t rough = pi->low_res_pos[idx];

    if (opcode == 0) {
        int fine_x;
        int fine_z;
        int extended;
        int level;

        /* A promotion may be preceded by one more low-resolution update. */
        if (rsprot_gbits(b, 1) != 0)
            low_res(r, b, idx);
        rough = pi->low_res_pos[idx];
        fine_x = rsprot_gbits(b, 13);
        fine_z = rsprot_gbits(b, 13);
        extended = rsprot_gbits(b, 1);
        level = (rough >> 28) & 0x3;
        pi->coord[idx] =
            (int32_t)((level << 28) | (((((rough >> 14) & 0xff) << 13) + fine_x) << 14) |
                      ((((rough & 0xff) << 13) + fine_z) & 0x3fff));
        pi->high_res[idx] = 1;
        moved(r, idx, 0);
        if (extended)
            queue_extended(r, idx);
        return 1;
    }
    if (opcode == 1) {
        int level_delta = rsprot_gbits(b, 2);

        pi->low_res_pos[idx] =
            (int32_t)(((((rough >> 28) + level_delta) & 3) << 28) + (rough & 0x0fffffff));
        return 0;
    }
    if (opcode == 2) {
        int packed = rsprot_gbits(b, 5);
        int level = ((rough >> 28) + (packed >> 3)) & 3;
        int x = ((rough >> 14) & 0xff) + k_walk_dx[packed & 7];
        int z = (rough & 0xff) + k_walk_dz[packed & 7];

        pi->low_res_pos[idx] = (int32_t)((x << 14) + z + (level << 28));
        return 0;
    }
    {
        int packed = rsprot_gbits(b, 18);
        int level = ((rough >> 28) + (packed >> 16)) & 3;
        int x = (((packed >> 8) & 0xff) + (rough >> 14)) & 0xff;
        int z = ((packed & 0xff) + rough) & 0xff;

        pi->low_res_pos[idx] = (int32_t)((x << 14) + z + (level << 28));
        return 0;
    }
}

/* getHighResolutionPlayerPosition. */
static void
high_res(Reader *r, RsprotBuf *b, int idx)
{
    RsprotPlayerInfo *pi = r->pi;
    int extended = rsprot_gbits(b, 1);
    int opcode;
    int32_t coord = pi->coord[idx];
    int level = (coord >> 28) & 0x3;
    int x = (coord >> 14) & 0x3fff;
    int z = coord & 0x3fff;

    if (extended)
        queue_extended(r, idx);
    opcode = rsprot_gbits(b, 2);

    if (opcode == 0) {
        if (extended) {
            rsprot_info_emit(&r->out, RSPROT_IO_PLAYER_STILL, idx);
            return;
        }
        /* Opcode 0 with nothing to say drops the slot to low resolution --
         * which the local player cannot do; the reference throws. */
        if (idx == pi->local_index)
            return;
        pi->low_res_pos[idx] = (int32_t)((level << 28) | (z >> 13) | ((x >> 13) << 14));
        pi->high_res[idx] = 0;
        rsprot_info_emit(&r->out, RSPROT_IO_PLAYER_REMOVE, idx);
        if (rsprot_gbits(b, 1) != 0)
            low_res(r, b, idx);
        return;
    }

    if (opcode == 1) {
        int move = rsprot_gbits(b, 3);

        x += k_walk_dx[move];
        z += k_walk_dz[move];
    } else if (opcode == 2) {
        int move = rsprot_gbits(b, 4);

        x += k_run_dx[move];
        z += k_run_dz[move];
    } else if (rsprot_gbits(b, 1) == 0) {
        /* Near teleport: 12 bits of signed delta. */
        int packed = rsprot_gbits(b, 12);
        int dx = (packed >> 5) & 31;
        int dz = packed & 31;

        if (dx > 15)
            dx -= 32;
        if (dz > 15)
            dz -= 32;
        x += dx;
        z += dz;
        level = (level + (packed >> 10)) & 0x3;
    } else {
        /* Far teleport: 30 bits, still a delta. */
        int packed = rsprot_gbits(b, 30);

        x = (x + ((packed >> 14) & 0x3fff)) & 0x3fff;
        z = (z + (packed & 0x3fff)) & 0x3fff;
        level = (level + ((packed >> 28) & 0x3)) & 0x3;
    }
    pi->coord[idx] = (int32_t)((level << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff));
    moved(r, idx, opcode);
}

/* One of the four sections; see decodeBitCodes. `want_inactive` is the cycle
 * bit a slot must carry, `low` which resolution is walked. */
static void
section(Reader *r, RsprotBuf *b, int want_inactive, int low)
{
    RsprotPlayerInfo *pi = r->pi;
    int skipped = 0;

    rsprot_buf_bit_begin(b);
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS && !b->err; idx++) {
        int inactive = (pi->flags[idx] & CUR_CYCLE_INACTIVE) != 0;

        if ((r->cycle_high[idx] != 0) == low || inactive != want_inactive)
            continue;
        if (skipped > 0) {
            --skipped;
            pi->flags[idx] |= NEXT_CYCLE_INACTIVE;
            continue;
        }
        if (rsprot_gbits(b, 1) == 0) {
            skipped = read_stationary(b);
            pi->flags[idx] |= NEXT_CYCLE_INACTIVE;
            continue;
        }
        if (low) {
            if (low_res(r, b, idx))
                pi->flags[idx] |= NEXT_CYCLE_INACTIVE;
        } else {
            high_res(r, b, idx);
        }
    }
    if (skipped != 0)
        b->err |= RSPROT_BUF_ERR_RANGE; /* RSProx: RuntimeException */
    rsprot_buf_bit_end(b);
}

int
rsprot_info_player_read(
    const RsprotInfoSide *side, RsprotPlayerInfo *pi, const uint8_t *data, int32_t len,
    RsprotInfoOp *ops, int cap, int *complete)
{
    static Reader r;
    RsprotBuf b;
    int ok;

    assert(side);
    assert(pi);
    assert(ops);
    assert(complete);
    *complete = 0;
    if (!pi->seeded)
        return 0;

    memset(&r, 0, sizeof(r));
    r.pi = pi;
    r.out.ops = ops;
    r.out.cap = cap;
    memcpy(r.cycle_high, pi->high_res, sizeof(r.cycle_high));

    rsprot_buf_wrap_read(&b, data, len);
    /* High-res active, high-res inactive, low-res inactive, low-res active. */
    section(&r, &b, 0, 0);
    section(&r, &b, 1, 0);
    section(&r, &b, 1, 1);
    section(&r, &b, 0, 1);
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++)
        pi->flags[idx] = (uint8_t)(pi->flags[idx] >> 1);
    ok = !b.err &&
         rsprot_info_tail(&b, side, r.extended, r.extended_count, &r.out, NULL, NULL);
    *complete = ok && !b.err && b.rpos == len && !r.out.overflow;
    return r.out.count;
}
