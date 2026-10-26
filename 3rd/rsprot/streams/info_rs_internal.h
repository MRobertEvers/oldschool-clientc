#ifndef RSPROT_STREAMS_INFO_RS_INTERNAL_H
#define RSPROT_STREAMS_INFO_RS_INTERNAL_H

/*
 * Shared by the rs-family versions; not part of info_rs.h. The sections here
 * read the same in every version -- only the new-npc record differs -- so the
 * versions are their own new-vis loop plus these.
 */

#include "info_rs.h"

#include "rsprot_info_internal.h"

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
    RsprotBuf* b,
    struct RsExtendedQueue* q,
    RsprotInfoList* out);

/** The npc extended-info blocks, from `b`'s (byte-aligned) read position. */
void
rs_npc_info_extended(
    struct RsExtendedQueue const* q,
    RsprotBuf* b,
    RsprotInfoList* out);

/** `len` bytes at the cursor into the list's bytes; NULL when the payload is
 *  shorter (the buffer is marked, and the caller stops). */
static inline uint8_t const*
rs_info_take_bytes(
    RsprotBuf* b,
    RsprotInfoList* out,
    int len)
{
    uint8_t const* p;

    if( len > rsprot_buf_readable(b) )
    {
        b->err |= RSPROT_BUF_ERR_UNDERFLOW;
        return NULL;
    }
    p = rsprot_info_blob(out, b->data + b->rpos, len, RSPROT_IP_DATA);
    b->rpos += len;
    return p;
}

/** A newline-ended string at the cursor, into the list's bytes. */
static inline char const*
rs_info_take_text(
    RsprotBuf* b,
    RsprotInfoList* out)
{
    int32_t n = 0;
    uint8_t const* s = rsprot_gstrnl(b, &n);

    return s ? rsprot_info_text(out, (char const*)s, n) : NULL;
}

/** Ends a read that ran out of payload: the list's last op says so. */
static inline void
rs_info_end(
    RsprotBuf* b,
    RsprotInfoList* out)
{
    if( b->err )
        rsprot_info_stop(b, out, RSPROT_STOP_ERROR, -1);
}

#endif
