#ifndef SRC_NET_REV_RS_INFO_RS_INTERNAL_H
#define SRC_NET_REV_RS_INFO_RS_INTERNAL_H

/*
 * Shared by the RS2-family versions; not part of info_rs.h. The sections here
 * read the same in every version -- only the new-npc record differs -- so the
 * versions are their own new-vis loop plus these.
 */

#include "info_rs.h"

#include "net/bitbuffer.h"

/** One packet's entries owed an extended-info block, by list position, in
 *  wire order. On the version's stack: the streams carry no state. */
struct RsExtendedQueue
{
    uint16_t idx[2048];
    int count;
};

/** The next op, zeroed. How many ops a packet produces is chosen by the
 *  SERVER, so running out of room is a hostile or malformed input, not a
 *  caller bug: past the list's end the op is the list's scratch op and the
 *  list is marked overflowed (rsprot_info_emit). */
static inline RsprotInfoOp*
rs_info_op(RsprotInfoList* out)
{
    return rsprot_info_emit(out, RSPROT_OP_NONE);
}

static inline void
rs_info_queue(
    struct RsExtendedQueue* q,
    int idx)
{
    if( q->count < (int)(sizeof(q->idx) / sizeof(q->idx[0])) )
        q->idx[q->count++] = (uint16_t)idx;
}

/** The tracked-npc section: count, then each npc's movement. Returns the list
 *  position the new-npc records start at. */
int
rs_npc_info_old_vis(
    struct Net_BitBuffer* buf,
    struct RsExtendedQueue* q,
    RsprotInfoList* out);

/** The npc extended-info blocks, byte-aligned from `byte_pos`. */
void
rs_npc_info_extended(
    struct RsExtendedQueue const* q,
    uint8_t const* data,
    int len,
    int byte_pos,
    RsprotInfoList* out);

#endif
