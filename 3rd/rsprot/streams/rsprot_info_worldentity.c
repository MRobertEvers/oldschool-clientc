/*
 * WORLDENTITY_INFO, V3-V7 -- the two loops every version runs.
 *
 * RSProx's world-entity clients (GenericWorldEntityInfoClient 227-232, then
 * each revision's WorldEntityInfoClient) have two section shapes across these
 * prots: V3-V5 read bare update entries and stop the spawn records 10 bytes
 * short; V6 and V7 end every idle and move entry in extended info and read
 * spawns to the last byte. What moves inside them -- a spawn record's fields,
 * their order and transforms, and the extended-info payload -- is generated
 * per layout into worldentity_info_v<N>.c, which also fixes which loop its
 * readers run.
 *
 * Stateless: an update entry addresses the active world's entity list by
 * position, and its deltas apply to whatever the host holds there.
 */
#include "rsprot_info.h"
#include "rsprot_info_internal.h"

#include <assert.h>
#include <string.h>

static int32_t
component(RsprotBuf *b, int code)
{
    switch (code & 3) {
    case 1: return rsprot_g1s(b);
    case 2: return rsprot_g2s_12(b);
    case 3: return (int32_t)rsprot_g4_1234(b);
    default: return 0;
    }
}

void
rsprot_info_we_fine(RsprotBuf *b, int32_t *x, int32_t *y, int32_t *z, int32_t *angle)
{
    int codes = rsprot_g1s(b);

    *x = component(b, codes);
    *y = component(b, codes >> 2);
    *z = component(b, codes >> 4);
    *angle = component(b, codes >> 6);
}

/* An update entry's opcode: 0 leaves, 1 stays, 2 moves, 3 teleports (RSProx
 * reads any other value as a move, and so does this). Returns the op written,
 * or NULL for a removal. */
static RsprotInfoOp *
entry(RsprotBuf *b, RsprotInfoList *out, int who)
{
    int opcode = rsprot_g1(b);
    RsprotInfoOp *op;

    if (opcode == 0) {
        rsprot_info_emit(out, RSPROT_OP_WE_REMOVE)->who = who;
        return NULL;
    }
    op = rsprot_info_emit(out, opcode == 1 ? RSPROT_OP_WE_IDLE : RSPROT_OP_WE_MOVE);
    op->who = who;
    if (opcode == 1)
        return op;
    op->we_move.teleport = opcode == 3;
    rsprot_info_we_fine(b, &op->we_move.dx, &op->we_move.dy, &op->we_move.dz,
                        &op->we_move.dangle);
    return op;
}

/* A spawn the record could not finish is retracted, as a block is. */
static void
spawn_one(RsprotBuf *b, RsprotInfoList *out, RsprotWeSpawnRead spawn)
{
    int before = out->count;
    int index = rsprot_g2_12(b);
    RsprotInfoOp *op = rsprot_info_emit(out, RSPROT_OP_WE_ADD);

    op->who = index;
    spawn(b, &op->we_add);
    if (b->err)
        out->count = before;
}

int
rsprot_info_worldentity_plain_read(
    RsprotWeSpawnRead spawn, const uint8_t *data, int32_t len, RsprotInfoList *out)
{
    RsprotBuf b;
    int count;

    assert(spawn);
    assert(out);
    rsprot_info_list_begin(out);
    rsprot_buf_wrap_read(&b, data, len);

    count = rsprot_g1(&b);
    rsprot_info_emit_value(out, RSPROT_OP_WE_COUNT, (uint64_t)count);
    for (int i = 0; i < count && !b.err; i++)
        entry(&b, out, i);
    while (!b.err && rsprot_buf_readable(&b) >= 10)
        spawn_one(&b, out, spawn);
    return rsprot_info_finish(&b, out, len);
}

int
rsprot_info_worldentity_extended_read(
    RsprotWeSpawnRead spawn, RsprotWeExtRead ext, const uint8_t *data, int32_t len,
    RsprotInfoList *out)
{
    RsprotBuf b;
    int count;

    assert(spawn);
    assert(ext);
    assert(out);
    rsprot_info_list_begin(out);
    rsprot_buf_wrap_read(&b, data, len);

    count = rsprot_g1(&b);
    rsprot_info_emit_value(out, RSPROT_OP_WE_COUNT, (uint64_t)count);
    for (int i = 0; i < count && !b.err; i++) {
        RsprotInfoOp *op = entry(&b, out, i);

        if (op)
            ext(&b, &op->we_move.ext);
    }
    while (!b.err && rsprot_buf_readable(&b) > 0)
        spawn_one(&b, out, spawn);
    return rsprot_info_finish(&b, out, len);
}
