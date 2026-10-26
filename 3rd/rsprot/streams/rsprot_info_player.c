/*
 * PLAYER_INFO, revisions 227-241 -- the stateful half.
 *
 * The four bit sections and the movement encodings are RSProx's
 * PlayerInfoClient.decodeBitCodes / getHighResolutionPlayerPosition /
 * getLowResolutionPlayerPosition, which have one shape at every revision this
 * library carries (tools/rsprox_gen_info.py would not have to touch this file
 * for a new revision that kept them). The port follows the reader this project
 * verified against recordings at 239 and 241
 * (src/net/rev/osrs241/osrs241_entity_info.c). The extended-info tail after
 * the sections is each version's (player_info_v<N>.c), and both write the op
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
    memset(pi->move_speed, RSPROT_TRAVERSAL_WALK, sizeof(pi->move_speed));
}

/*
 * The GPI init block: 30 bits of absolute coordinate for the local player, then
 * 18 bits of rough position (level << 16 | x << 8 | z) for every other slot in
 * index order. The local slot is skipped, so the count is structural.
 */
int
rsprot_player_info_gpi(int local_index, const uint8_t *data, int32_t len, int32_t *coord,
                       int32_t positions[RSPROT_INFO_PLAYER_SLOTS])
{
    RsprotBuf b;

    assert(coord);
    assert(positions);
    memset(positions, 0, sizeof(int32_t) * RSPROT_INFO_PLAYER_SLOTS);
    *coord = 0;
    if (local_index <= 0 || local_index >= RSPROT_INFO_PLAYER_SLOTS || !data || len <= 0)
        return 0;
    rsprot_buf_wrap_read(&b, data, len);
    rsprot_buf_bit_begin(&b);
    *coord = rsprot_gbits(&b, 30);
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++)
        if (idx != local_index)
            positions[idx] = rsprot_gbits(&b, 18);
    rsprot_buf_bit_end(&b);
    return !b.err;
}

void
rsprot_player_info_seed(RsprotPlayerInfo *pi, int32_t coord,
                        const int32_t positions[RSPROT_INFO_PLAYER_SLOTS])
{
    int local;

    assert(pi);
    assert(positions);
    local = pi->local_index;
    memset(pi, 0, sizeof(*pi));
    pi->local_index = local;
    pi->seeded = 1;
    memset(pi->move_speed, RSPROT_TRAVERSAL_WALK, sizeof(pi->move_speed));
    if (local <= 0 || local >= RSPROT_INFO_PLAYER_SLOTS)
        return;
    pi->coord[local] = coord;
    pi->high_res[local] = 1;
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++) {
        int32_t packed = positions[idx];

        if (idx == local)
            continue;
        pi->low_res_pos[idx] =
            (int32_t)((((packed >> 8) & 0xff) << 14) + (packed & 0xff) + ((packed >> 16) << 28));
    }
}

void
rsprot_player_info_init(RsprotPlayerInfo *pi, const uint8_t *data, int32_t len)
{
    static _Thread_local int32_t positions[RSPROT_INFO_PLAYER_SLOTS];
    int32_t coord;

    assert(pi);
    if (!rsprot_player_info_gpi(pi->local_index, data, len, &coord, positions)) {
        /* Seeded with nothing known, as the reference is by an empty block. */
        int local = pi->local_index;

        memset(pi, 0, sizeof(*pi));
        pi->local_index = local;
        pi->seeded = 1;
        memset(pi->move_speed, RSPROT_TRAVERSAL_WALK, sizeof(pi->move_speed));
        return;
    }
    rsprot_player_info_seed(pi, coord, positions);
}

typedef struct Reader
{
    RsprotPlayerInfo *pi;
    RsprotPlayerPacket *pp;
    RsprotInfoList *out;
    uint8_t cycle_high[RSPROT_INFO_PLAYER_SLOTS];
    int *extended;
    int extended_count;
} Reader;

static void
queue_extended(Reader *r, int idx)
{
    if (r->extended_count < RSPROT_INFO_PLAYER_SLOTS)
        r->extended[r->extended_count++] = idx;
}

void
rsprot_info_player_target(RsprotInfoList *out, const RsprotPlayerInfo *pi, int idx)
{
    if (idx == pi->local_index)
        rsprot_info_emit(out, RSPROT_OP_SET_LOCAL_PLAYER);
    else
        rsprot_info_emit_value(out, RSPROT_OP_ADD_NEW, (uint64_t)idx);
}

/* The slot moved: its target, then its new absolute position. Entering view
 * (how 0) and teleporting (3) re-place; walking (1) and running (2) step. The
 * traversal is the slot's persistent one until a MOVE_SPEED / TEMP_MOVE_SPEED
 * block amends this op (movement_op). */
static void
moved(Reader *r, int idx, int how)
{
    int32_t coord = r->pi->coord[idx];
    RsprotInfoXZLevel *m;

    rsprot_info_player_target(r->out, r->pi, idx);
    r->pp->movement_op[idx] = (int16_t)r->out->count;
    m = &rsprot_info_emit(r->out, RSPROT_OP_ABS_XZLEVEL)->xz_level;
    m->x = (int16_t)((coord >> 14) & 0x3fff);
    m->z = (int16_t)(coord & 0x3fff);
    m->level = (uint8_t)((coord >> 28) & 0x3);
    m->jump = how == 0 || how == 3;
    m->has_move_speed = true;
    m->move_speed = r->pi->move_speed[idx];
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
        pi->update_type[idx] = RSPROT_PLAYER_LOW_RESOLUTION_TO_HIGH_RESOLUTION;
        moved(r, idx, 0);
        if (extended)
            queue_extended(r, idx);
        return 1;
    }
    pi->update_type[idx] = RSPROT_PLAYER_LOW_RESOLUTION_MOVEMENT;
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
            pi->update_type[idx] = RSPROT_PLAYER_HIGH_RESOLUTION_IDLE;
            rsprot_info_player_target(r->out, pi, idx);
            return;
        }
        /* Opcode 0 with nothing to say drops the slot to low resolution --
         * which the local player cannot do; RSProx throws. Reaching it means
         * the table's local index is not the server's (a world hop the
         * recording did not log the new index of). */
        if (idx == pi->local_index) {
            b->err |= RSPROT_BUF_ERR_RANGE;
            return;
        }
        pi->low_res_pos[idx] = (int32_t)((level << 28) | (z >> 13) | ((x >> 13) << 14));
        pi->high_res[idx] = 0;
        /* A low-resolution update that follows reclassifies the slot. */
        pi->update_type[idx] = RSPROT_PLAYER_HIGH_RESOLUTION_TO_LOW_RESOLUTION;
        pi->move_speed[idx] = RSPROT_TRAVERSAL_WALK;
        rsprot_info_emit_value(r->out, RSPROT_OP_REMOVE_PID, (uint64_t)idx);
        if (rsprot_gbits(b, 1) != 0)
            low_res(r, b, idx);
        return;
    }

    pi->update_type[idx] = RSPROT_PLAYER_HIGH_RESOLUTION_MOVEMENT;
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
    uint8_t idle = low ? RSPROT_PLAYER_LOW_RESOLUTION_IDLE : RSPROT_PLAYER_HIGH_RESOLUTION_IDLE;

    rsprot_buf_bit_begin(b);
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS && !b->err; idx++) {
        int inactive = (pi->flags[idx] & CUR_CYCLE_INACTIVE) != 0;

        if ((r->cycle_high[idx] != 0) == low || inactive != want_inactive)
            continue;
        if (skipped > 0) {
            --skipped;
            pi->update_type[idx] = idle;
            pi->flags[idx] |= NEXT_CYCLE_INACTIVE;
            continue;
        }
        if (rsprot_gbits(b, 1) == 0) {
            skipped = read_stationary(b);
            pi->update_type[idx] = idle;
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
rsprot_info_player_sections(
    RsprotPlayerPacket *pp, RsprotBuf *b, RsprotInfoList *out,
    int extended[RSPROT_INFO_PLAYER_SLOTS])
{
    Reader r;
    RsprotPlayerInfo *pi;

    assert(pp);
    assert(pp->pi);
    assert(b);
    assert(out);
    assert(extended);
    pi = pp->pi;
    memset(&r, 0, sizeof(r));
    memset(pi->update_type, RSPROT_PLAYER_LOW_RESOLUTION_IDLE, sizeof(pi->update_type));
    r.pi = pi;
    r.pp = pp;
    for (int i = 0; i < RSPROT_INFO_PLAYER_SLOTS; i++)
        pp->movement_op[i] = -1;
    r.out = out;
    r.extended = extended;
    memcpy(r.cycle_high, pi->high_res, sizeof(r.cycle_high));

    /* High-res active, high-res inactive, low-res inactive, low-res active. */
    section(&r, b, 0, 0);
    section(&r, b, 1, 0);
    section(&r, b, 1, 1);
    section(&r, b, 0, 1);
    for (int idx = 1; idx < RSPROT_INFO_PLAYER_SLOTS; idx++)
        pi->flags[idx] = (uint8_t)(pi->flags[idx] >> 1);
    return r.extended_count;
}
