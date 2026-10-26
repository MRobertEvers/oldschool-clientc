#ifndef RSPROT_TRACE_H
#define RSPROT_TRACE_H

/*
 * Writing a trace (rsprot_exec.h, RsprotTrace): one entry per value read, in
 * wire order, under the reader's name for it. The codec executor
 * (src/net/exec/pkt_exec.c) and the info-stream block readers
 * (streams/rsprot_info_blocks.c) write the same entries, so a host reads both
 * with one parser.
 *
 * Every entry starts u8 tag, u8 name length, the name; then the tag's body
 * (RSPROT_TRACE_PRIM ... RSPROT_TRACE_BITS). An entry that does not fit sets
 * `overflow` and is dropped, as is every entry after it.
 */

#include "rsprot_exec.h"

#include <string.h>

static inline int
rsprot_trace_head(RsprotTrace *t, int tag, const char *name, int32_t body)
{
    size_t n = name ? strlen(name) : 0;

    if (n > 255)
        n = 255;
    if (t->overflow || t->cap - t->len < (int32_t)(2 + n) + body) {
        t->overflow = 1;
        return 0;
    }
    t->out[t->len++] = (uint8_t)tag;
    t->out[t->len++] = (uint8_t)n;
    memcpy(t->out + t->len, name, n);
    t->len += (int32_t)n;
    return 1;
}

static inline void
rsprot_trace_be(RsprotTrace *t, uint64_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; i--)
        t->out[t->len++] = (uint8_t)(v >> (8 * i));
}

/** A primitive's value; `prim` is its RsprotPrim, or 0 where the reader has none. */
static inline void
rsprot_trace_prim(RsprotTrace *t, int prim, int32_t v, const char *name)
{
    if (!t || !rsprot_trace_head(t, RSPROT_TRACE_PRIM, name, 5))
        return;
    t->out[t->len++] = (uint8_t)prim;
    rsprot_trace_be(t, (uint32_t)v, 4);
}

static inline void
rsprot_trace_long(RsprotTrace *t, int64_t v, const char *name)
{
    if (!t || !rsprot_trace_head(t, RSPROT_TRACE_LONG, name, 8))
        return;
    rsprot_trace_be(t, (uint64_t)v, 8);
}

/** A string (`tag` RSPROT_TRACE_STR, `sub` its RsprotStrKind; `len` -1 is null)
 *  or a blob (RSPROT_TRACE_DATA, `sub` -1). */
static inline void
rsprot_trace_bytes(RsprotTrace *t, int tag, int sub, const uint8_t *data, int32_t len,
                   const char *name)
{
    int32_t n = len < 0 ? 0 : len;

    if (!t || !rsprot_trace_head(t, tag, name, (sub >= 0) + 4 + n))
        return;
    if (sub >= 0)
        t->out[t->len++] = (uint8_t)sub;
    rsprot_trace_be(t, (uint32_t)len, 4);
    if (n > 0)
        memcpy(t->out + t->len, data, (size_t)n);
    t->len += n;
}

static inline void
rsprot_trace_bits(RsprotTrace *t, int width, int32_t v, const char *name)
{
    if (!t || !rsprot_trace_head(t, RSPROT_TRACE_BITS, name, 5))
        return;
    t->out[t->len++] = (uint8_t)width;
    rsprot_trace_be(t, (uint32_t)v, 4);
}

#endif
