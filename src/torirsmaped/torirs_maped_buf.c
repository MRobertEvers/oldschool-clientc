#include "torirs_maped_buf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int
ToriRSMapEd_BufAvailable(const struct ToriRSMapEdBuf* buf)
{
    assert(buf);
    return buf->tail - buf->head;
}

const uint8_t*
ToriRSMapEd_BufPeek(const struct ToriRSMapEdBuf* buf)
{
    assert(buf);
    return buf->data + buf->head;
}

void
ToriRSMapEd_BufConsume(
    struct ToriRSMapEdBuf* buf,
    int count)
{
    assert(buf);
    assert(count >= 0);
    assert(count <= buf->tail - buf->head);

    buf->head += count;
    if( buf->head == buf->tail )
    {
        buf->head = 0;
        buf->tail = 0;
    }
}

void
ToriRSMapEd_BufClose(struct ToriRSMapEdBuf* buf)
{
    if( !buf )
        return;
    buf->closed = 1;
}

void
ToriRSMapEd_BufFree(struct ToriRSMapEdBuf* buf)
{
    if( !buf )
        return;

    free(buf->data);
    buf->data = NULL;
    buf->cap = 0;
    buf->head = 0;
    buf->tail = 0;
}

int
ToriRSMapEd_BufWrite(
    struct ToriRSMapEdBuf* buf,
    const uint8_t* src,
    int len)
{
    int live;

    assert(buf);
    assert(src || len == 0);
    assert(len >= 0);

    if( buf->closed )
        return -1;
    if( len == 0 )
        return 0;

    /* Compact before growing: a long session writes and drains continuously,
     * so `head` marches forward and the tail hits `cap` while most of the
     * buffer is dead space. */
    live = buf->tail - buf->head;
    if( buf->head > 0 && buf->tail + len > buf->cap )
    {
        memmove(buf->data, buf->data + buf->head, (size_t)live);
        buf->head = 0;
        buf->tail = live;
    }
    if( buf->tail + len > buf->cap )
    {
        int want = buf->tail + len;
        int cap = buf->cap > 0 ? buf->cap : 4096;
        while( cap < want )
            cap *= 2;
        buf->data = realloc(buf->data, (size_t)cap);
        assert(buf->data);
        buf->cap = cap;
    }

    memcpy(buf->data + buf->tail, src, (size_t)len);
    buf->tail += len;
    return len;
}

int
ToriRSMapEd_BufRead(
    struct ToriRSMapEdBuf* buf,
    uint8_t* dst,
    int max)
{
    int live;

    assert(buf);
    assert(dst || max == 0);
    assert(max >= 0);

    live = buf->tail - buf->head;
    if( live == 0 )
        return buf->closed ? -1 : 0;

    if( live > max )
        live = max;
    memcpy(dst, buf->data + buf->head, (size_t)live);
    ToriRSMapEd_BufConsume(buf, live);
    return live;
}
