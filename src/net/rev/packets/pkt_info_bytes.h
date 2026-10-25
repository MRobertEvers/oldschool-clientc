#ifndef SRC_NET_REV_PACKETS_PKT_INFO_BYTES_H
#define SRC_NET_REV_PACKETS_PKT_INFO_BYTES_H

/*
 * For the client's own PLAYER_INFO / NPC_INFO readers (the classic RS2 streams,
 * lc289): copy a blob or a string from an RSCache_Buffer cursor into the list's
 * byte arena (3rd/rsprot/streams/rsprot_info.h), where the ops point at it.
 */

#include "mem/torimem_rscache.h"
#include "mem/torirs_mem.h"
#include "streams/rsprot_info.h"

#include <assert.h>
#include <rsbuffer.h>
#include <string.h>

/* `len` bytes at the cursor, into the list's bytes. */
static inline uint8_t const*
pkt_info_take_bytes(
    struct RSCache_Buffer* buf,
    RsprotInfoList* out,
    int len)
{
    uint8_t const* p;

    assert(buf->position + (uint32_t)len <= buf->size);
    p = rsprot_info_blob(out, buf->data + buf->position, len, RSPROT_IP_DATA);
    buf->position += (uint32_t)len;
    return p;
}

/* A newline-ended string at the cursor, into the list's bytes. */
static inline char const*
pkt_info_take_text(
    struct RSCache_Buffer* buf,
    RsprotInfoList* out)
{
    char* raw = gstringnewline(buf);
    char const* text;

    if( !raw )
        return NULL;
    /* RSCache allocated it; the list's copy is the one the op keeps. */
    raw = ToriMem_TakeStrFromRSCache(ToriMem_Net(), raw);
    text = rsprot_info_text(out, raw, (int32_t)strlen(raw));
    ToriMem_Free(ToriMem_Net(), raw);
    return text;
}

#endif
