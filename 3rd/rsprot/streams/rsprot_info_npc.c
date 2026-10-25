/*
 * NPC_INFO, revisions 227-241 (NPC_INFO_SMALL / LARGE, V5 and V6).
 *
 * RSProx's NpcInfoClient.processHighResolution has one wire shape at every
 * revision; processLowResolution -- the entering-view record -- reorders its
 * seven fields every revision, and LARGE differs from SMALL in the record's
 * dx/dz width (decodeDelta: 8 bits instead of 6). So the record is code each
 * revision generates (npc_info_osrs<N>.c), and this is the rest of the bit
 * stream, which every version reader (npc_info_v5.c, npc_info_v6.c) runs.
 *
 * Stateless, like the reader it was ported from
 * (src/net/rev/osrs241/osrs241_entity_info.c): positions are the list both
 * ends rebuild during the packet, and the host applies the op list to its own.
 */
#include "rsprot_info.h"
#include "rsprot_info_internal.h"

#include <assert.h>
#include <string.h>

/* speed 0 crawl, 1 walk, 2 run */
static void
step(RsprotInfoList *out, int direction, int speed)
{
    rsprot_info_emit_value(out,
                           speed == 1   ? RSPROT_OP_WALKDIR
                           : speed == 2 ? RSPROT_OP_RUNDIR
                                        : RSPROT_OP_CRAWLDIR,
                           (uint64_t)direction);
}

/* A newcomer: its slot, type and offset, its spawn facing
 * (NpcInfoClient.NPC_TURN_ANGLES), and the cycle it spawned on. */
static void
added(RsprotInfoList *out, int slot, const RsprotNpcRecord *rec)
{
    static const uint16_t k_spawn_facing[8] = { 768, 1024, 1280, 512, 1536, 256, 0, 1792 };
    RsprotInfoOp *op;

    rsprot_info_emit_value(out, RSPROT_OP_ADD_NEW, (uint64_t)slot);
    rsprot_info_emit_value(out, RSPROT_OP_NPCTYPE, (uint64_t)rec->type);
    op = rsprot_info_emit(out, RSPROT_OP_DELTA_XZ);
    op->delta_xz.dx = (int16_t)rec->dx;
    op->delta_xz.dz = (int16_t)rec->dz;
    op->delta_xz.jump = rec->jump != 0;
    op = rsprot_info_emit(out, RSPROT_OP_FACE_ANGLE);
    op->face_angle.angle = k_spawn_facing[rec->angle & 7];
    op->face_angle.instant = true;
    op->face_angle.spawn = true;
    if (rec->has_spawn_cycle)
        rsprot_info_emit_value(out, RSPROT_OP_SPAWN_CYCLE, rec->spawn_cycle);
}

int32_t
rsprot_info_npc_delta(RsprotBuf *b, int bits)
{
    int32_t v = rsprot_gbits(b, bits);

    return v >= 1 << (bits - 1) ? v - (1 << bits) : v;
}

int
rsprot_info_npc_sections(
    RsprotBuf *b, int delta_bits, RsprotNpcRecordRead record, RsprotInfoList *out,
    int extended[RSPROT_INFO_NPC_EXTENDED_MAX])
{
    int extended_count = 0;
    int list_index = 0;
    int count;

    assert(b);
    assert(record);
    assert(out);
    assert(extended);
    rsprot_buf_bit_begin(b);

    count = rsprot_gbits(b, 8);
    rsprot_info_emit_value(out, RSPROT_OP_COUNT_RESET, (uint64_t)count);
    for (int old = 0; old < count && !b->err; old++) {
        int type;

        if (rsprot_gbits(b, 1) == 0) {
            rsprot_info_emit_value(out, RSPROT_OP_ADD_OLD, (uint64_t)old);
            rsprot_info_emit_value(out, RSPROT_OP_INFO, 0);
            list_index++;
            continue;
        }
        type = rsprot_gbits(b, 2);
        if (type == 3) {
            rsprot_info_emit_value(out, RSPROT_OP_CLEAR_OLD, (uint64_t)old);
            continue;
        }
        rsprot_info_emit_value(out, RSPROT_OP_ADD_OLD, (uint64_t)old);
        rsprot_info_emit_value(out, RSPROT_OP_INFO, 1);
        if (type == 0) {
            /* Stood still with extended info: the bit is implied. */
            extended[extended_count++] = list_index;
        } else if (type == 1) {
            step(out, rsprot_gbits(b, 3), 1);
            if (rsprot_gbits(b, 1))
                extended[extended_count++] = list_index;
        } else {
            if (rsprot_gbits(b, 1) == 1) {
                step(out, rsprot_gbits(b, 3), 2);
                step(out, rsprot_gbits(b, 3), 2);
            } else {
                step(out, rsprot_gbits(b, 3), 0);
            }
            if (rsprot_gbits(b, 1))
                extended[extended_count++] = list_index;
        }
        list_index++;
    }

    /* Entering-view records: a 16-bit slot (0xFFFF ends), then the record. The
     * reference stops when fewer than 16 + 12 bits remain, terminator or not. */
    while (!b->err && rsprot_buf_readable_bits(b) >= 16 + 12) {
        int slot = rsprot_gbits(b, 16);
        RsprotNpcRecord rec;

        if (slot == 0xFFFF)
            break;
        memset(&rec, 0, sizeof(rec));
        record(b, delta_bits, &rec);
        if (b->err)
            break;
        added(out, slot, &rec);
        if (rec.extended && extended_count < RSPROT_INFO_NPC_EXTENDED_MAX)
            extended[extended_count++] = list_index;
        list_index++;
    }
    rsprot_buf_bit_end(b);
    return extended_count;
}
