/*
 * NPC_INFO, revisions 227-241 (NPC_INFO_SMALL / LARGE, V5 and V6).
 *
 * RSProx's NpcInfoClient.processHighResolution has one wire shape at every
 * revision; processLowResolution -- the entering-view record -- reorders its
 * seven fields every revision, and LARGE differs from SMALL in the record's
 * dx/dz width (decodeDelta: 8 bits instead of 6). So the record is code each
 * npc layout generates (npc_info_small_v5.c, ...), and this is the rest of the
 * stream, which every one of those readers runs with its own record reader and
 * tables.
 *
 * Stateless, like the reader it was ported from
 * (src/net/rev/osrs241/osrs241_entity_info.c): positions are the list both
 * ends rebuild during the packet, and the host applies the op list to its own.
 */
#include "rsprot_info.h"
#include "rsprot_info_internal.h"

#include <assert.h>
#include <string.h>

static void
step(RsprotInfoOut *out, int direction, int speed)
{
    RsprotInfoOp *op = rsprot_info_emit(out, RSPROT_IO_NPC_STEP, -1);

    op->step.direction = direction;
    op->step.speed = speed;
}

int
rsprot_info_npc_read(
    const RsprotInfoSide *side, RsprotNpcRecordRead record, const uint8_t *data, int32_t len,
    RsprotNpcTypeLengths lengths, void *ctx, RsprotInfoOp *ops, int cap, int *complete)
{
    static int extended[512];
    RsprotInfoOut out;
    int extended_count = 0;
    int list_index = 0;
    int count;
    int ok;
    RsprotBuf b;

    assert(side);
    assert(record);
    assert(ops);
    assert(complete);
    *complete = 0;
    memset(&out, 0, sizeof(out));
    out.ops = ops;
    out.cap = cap;

    rsprot_buf_wrap_read(&b, data, len);
    rsprot_buf_bit_begin(&b);

    count = rsprot_gbits(&b, 8);
    rsprot_info_emit(&out, RSPROT_IO_NPC_COUNT, -1)->count = count;
    for (int old = 0; old < count && !b.err; old++) {
        int type;

        if (rsprot_gbits(&b, 1) == 0) {
            rsprot_info_emit(&out, RSPROT_IO_NPC_KEEP, old)->moved = 0;
            list_index++;
            continue;
        }
        type = rsprot_gbits(&b, 2);
        if (type == 3) {
            rsprot_info_emit(&out, RSPROT_IO_NPC_REMOVE, old);
            continue;
        }
        rsprot_info_emit(&out, RSPROT_IO_NPC_KEEP, old)->moved = 1;
        if (type == 0) {
            /* Stood still with extended info: the bit is implied. */
            extended[extended_count++] = list_index;
        } else if (type == 1) {
            step(&out, rsprot_gbits(&b, 3), 1);
            if (rsprot_gbits(&b, 1))
                extended[extended_count++] = list_index;
        } else {
            if (rsprot_gbits(&b, 1) == 1) {
                step(&out, rsprot_gbits(&b, 3), 2);
                step(&out, rsprot_gbits(&b, 3), 2);
            } else {
                step(&out, rsprot_gbits(&b, 3), 0);
            }
            if (rsprot_gbits(&b, 1))
                extended[extended_count++] = list_index;
        }
        list_index++;
    }

    /* Entering-view records: a 16-bit slot (0xFFFF ends), then the record. The
     * reference stops when fewer than 16 + 12 bits remain, terminator or not. */
    while (!b.err && rsprot_buf_readable_bits(&b) >= 16 + 12) {
        int slot = rsprot_gbits(&b, 16);
        RsprotNpcRecord r;
        RsprotInfoOp *op;

        if (slot == 0xFFFF)
            break;
        memset(&r, 0, sizeof(r));
        record(&b, &r);
        if (b.err)
            break;
        op = rsprot_info_emit(&out, RSPROT_IO_NPC_ADD, list_index);
        op->add.slot = slot;
        op->add.type = r.type;
        op->add.dx = r.dx;
        op->add.dz = r.dz;
        op->add.jump = r.jump;
        op->add.facing = r.angle;
        op->add.has_spawn_cycle = r.has_spawn_cycle;
        op->add.spawn_cycle = r.spawn_cycle;
        if (r.extended && extended_count < (int)(sizeof(extended) / sizeof(extended[0])))
            extended[extended_count++] = list_index;
        list_index++;
    }
    rsprot_buf_bit_end(&b);
    ok = !b.err && rsprot_info_tail(&b, side, extended, extended_count, &out, lengths, ctx);
    *complete = ok && !b.err && b.rpos == len && !out.overflow;
    return out.count;
}
