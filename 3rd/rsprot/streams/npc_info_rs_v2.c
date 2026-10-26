/*
 * NPC_INFO, rs family, version 2: build 289. Version 1 with a jump flag in the
 * new-npc record between (dx, dz) and the extended-info bit (webclient
 * Client.getNpcPosNewVis, branch 289) -- the teleport-vs-walk distinction the
 * tracked section always had, extended to an npc appearing for the first time.
 */
#include "info_rs_internal.h"

#include <assert.h>
#include <stdbool.h>

int
npc_info_rs_v2(
    NpcInfoRs const* k,
    uint8_t const* data,
    int length,
    RsprotInfoList* out)
{
    struct RsExtendedQueue queue;
    struct RsExtendedQueue* q = &queue;
    RsprotBuf b;
    int slot_bits;
    int type_bits;
    int terminator;
    int new_idx;

    assert(k);
    assert(data);
    assert(out);
    assert(k->slot_bits > 0);
    assert(k->slot_bits <= RS_NPC_INFO_BITS_MAX);
    assert(k->type_bits > 0);
    assert(k->type_bits <= RS_NPC_INFO_BITS_MAX);
    slot_bits = k->slot_bits;
    type_bits = k->type_bits;
    terminator = (1 << slot_bits) - 1;

    rsprot_info_list_begin(out);
    q->count = 0;
    rsprot_buf_wrap_read(&b, data, length);
    rsprot_buf_bit_begin(&b);
    new_idx = rs_npc_info_old_vis(&b, q, out);

    /* New NPCs: slot + type + 5 + 5 + jump(1) + 1 bits per record, terminated by an
     * all-ones slot. Both widths come from the revision (Client-TS
     * getNpcPosNewVis reads gBit(14) with terminator 16383, NOT the 13-bit /
     * 8191 layout of older revisions), while the 21-bit loop guard is the
     * reference's own fixed margin and stays put: it is what decides whether
     * the terminator is consumed before the byte-aligned extended-info
     * section, so changing it would shift that alignment. */
    while( b.bit_rpos + 21 < length * 8 )
    {
        int npc_slot = rsprot_gbits(&b, slot_bits);
        if( npc_slot == terminator )
            break;

        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_NEW;
            op->value = (uint64_t)npc_slot;
        }

        int npc_type = rsprot_gbits(&b, type_bits);
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_NPCTYPE;
            op->value = (uint64_t)npc_type;
        }

        int dx = rsprot_gbits(&b, 5);
        if( dx > 15 )
            dx -= 32;
        int dz = rsprot_gbits(&b, 5);
        if( dz > 15 )
            dz -= 32;
        int jump = rsprot_gbits(&b, 1);
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_DELTA_XZ;
            op->delta_xz.dx = (int16_t)dx;
            op->delta_xz.dz = (int16_t)dz;
            op->delta_xz.jump = jump != 0;
        }

        if( rsprot_gbits(&b, 1) )
            rs_info_queue(q, new_idx);
        new_idx++;
    }

    rsprot_buf_bit_end(&b);
    rs_npc_info_extended(q, &b, out);
    rs_info_end(&b, out);
    return out->count;
}
