#include "ringbuf.h"

#include <stddef.h>
#include <string.h>

struct RingBuf
{
    unsigned char* buffer;
    size_t capacity;
    size_t head; /* next position to read */
    size_t tail; /* next position to write */
    bool owns_buffer;
};

/*-----------------------------------------------------------
 * Constructor/Destructor
 *----------------------------------------------------------*/

struct RingBuf*
ringbuf_new(size_t capacity)
{
    if( capacity == 0 )
        return NULL;

    struct RingBuf* rb = (struct RingBuf*)malloc(sizeof(struct RingBuf));
    if( !rb )
        return NULL;

    rb->buffer = (unsigned char*)malloc(capacity);
    if( !rb->buffer )
    {
        free(rb);
        return NULL;
    }

    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->owns_buffer = true;
    return rb;
}

void
ringbuf_free(struct RingBuf* rb)
{
    if( !rb )
        return;
    if( rb->owns_buffer && rb->buffer )
        free(rb->buffer);
    free(rb);
}

/*-----------------------------------------------------------
 * Init with external buffer
 *----------------------------------------------------------*/

int
ringbuf_init(struct RingBuf* rb, void* buffer, size_t capacity)
{
    if( !rb || !buffer || capacity == 0 )
        return RINGBUF_BADARG;

    rb->buffer = (unsigned char*)buffer;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->owns_buffer = false;
    return RINGBUF_OK;
}

void
ringbuf_deinit(struct RingBuf* rb)
{
    if( !rb )
        return;
    rb->buffer = NULL;
    rb->capacity = 0;
    rb->head = 0;
    rb->tail = 0;
    rb->owns_buffer = false;
}

/*-----------------------------------------------------------
 * Capacity and state
 *----------------------------------------------------------*/

size_t
ringbuf_capacity(const struct RingBuf* rb)
{
    return rb ? rb->capacity : 0;
}

size_t
ringbuf_used(const struct RingBuf* rb)
{
    if( !rb )
        return 0;
    return (rb->tail - rb->head + rb->capacity) % rb->capacity;
}

size_t
ringbuf_avail(const struct RingBuf* rb)
{
    if( !rb )
        return 0;
    /* One slot reserved to distinguish full from empty */
    return rb->capacity - ringbuf_used(rb) - 1;
}

bool
ringbuf_empty(const struct RingBuf* rb)
{
    return rb && rb->head == rb->tail;
}

bool
ringbuf_full(const struct RingBuf* rb)
{
    if( !rb )
        return false;
    return (rb->tail + 1) % rb->capacity == rb->head;
}

/*-----------------------------------------------------------
 * Write
 *----------------------------------------------------------*/

int
ringbuf_write(struct RingBuf* rb, const void* data, size_t len)
{
    if( !rb || !data )
        return RINGBUF_BADARG;
    if( len == 0 )
        return RINGBUF_OK;
    if( len > ringbuf_avail(rb) )
        return RINGBUF_FULL;

    const unsigned char* p = (const unsigned char*)data;
    size_t n = rb->capacity - rb->tail;
    if( len <= n )
    {
        memcpy(rb->buffer + rb->tail, p, len);
    }
    else
    {
        memcpy(rb->buffer + rb->tail, p, n);
        memcpy(rb->buffer, p + n, len - n);
    }
    rb->tail = (rb->tail + len) % rb->capacity;
    return RINGBUF_OK;
}

int
ringbuf_putc(struct RingBuf* rb, unsigned char c)
{
    if( !rb )
        return RINGBUF_BADARG;
    if( ringbuf_full(rb) )
        return RINGBUF_FULL;
    rb->buffer[rb->tail] = c;
    rb->tail = (rb->tail + 1) % rb->capacity;
    return RINGBUF_OK;
}

/*-----------------------------------------------------------
 * Putback
 *----------------------------------------------------------*/

int
ringbuf_putback(struct RingBuf* rb, const void* data, size_t len)
{
    if( !rb || !data )
        return RINGBUF_BADARG;
    if( len == 0 )
        return RINGBUF_OK;
    if( len > ringbuf_avail(rb) )
        return RINGBUF_FULL;

    size_t new_head = (rb->head - len + rb->capacity) % rb->capacity;
    const unsigned char* p = (const unsigned char*)data;
    size_t n = rb->capacity - new_head;
    if( len <= n )
    {
        memcpy(rb->buffer + new_head, p, len);
    }
    else
    {
        memcpy(rb->buffer + new_head, p, n);
        memcpy(rb->buffer, p + n, len - n);
    }
    rb->head = new_head;
    return RINGBUF_OK;
}

/*-----------------------------------------------------------
 * Read
 *----------------------------------------------------------*/

size_t
ringbuf_read(struct RingBuf* rb, void* dst, size_t len)
{
    if( !rb || !dst || len == 0 )
        return 0;

    size_t used = ringbuf_used(rb);
    if( len > used )
        len = used;
    if( len == 0 )
        return 0;

    unsigned char* p = (unsigned char*)dst;
    size_t n = rb->capacity - rb->head;
    if( len <= n )
    {
        memcpy(p, rb->buffer + rb->head, len);
    }
    else
    {
        memcpy(p, rb->buffer + rb->head, n);
        memcpy(p + n, rb->buffer, len - n);
    }
    rb->head = (rb->head + len) % rb->capacity;
    return len;
}

int
ringbuf_getc(struct RingBuf* rb, unsigned char* out)
{
    if( !rb || !out )
        return RINGBUF_BADARG;
    if( ringbuf_empty(rb) )
        return RINGBUF_EMPTY;
    *out = rb->buffer[rb->head];
    rb->head = (rb->head + 1) % rb->capacity;
    return RINGBUF_OK;
}

/*-----------------------------------------------------------
 * Peek
 *----------------------------------------------------------*/

size_t
ringbuf_peek(struct RingBuf* rb, void* dst, size_t len)
{
    return ringbuf_peek_at(rb, 0, dst, len);
}

size_t
ringbuf_peek_at(const struct RingBuf* rb, size_t offset, void* dst, size_t len)
{
    if( !rb || !dst || len == 0 )
        return 0;

    size_t used = ringbuf_used(rb);
    if( offset >= used )
        return 0;
    if( len > used - offset )
        len = used - offset;

    unsigned char* p = (unsigned char*)dst;
    size_t start = (rb->head + offset) % rb->capacity;
    size_t n = rb->capacity - start;
    if( len <= n )
    {
        memcpy(p, rb->buffer + start, len);
    }
    else
    {
        memcpy(p, rb->buffer + start, n);
        memcpy(p + n, rb->buffer, len - n);
    }
    return len;
}

/*-----------------------------------------------------------
 * Skip
 *----------------------------------------------------------*/

size_t
ringbuf_skip(struct RingBuf* rb, size_t len)
{
    if( !rb )
        return 0;
    size_t used = ringbuf_used(rb);
    if( len > used )
        len = used;
    rb->head = (rb->head + len) % rb->capacity;
    return len;
}

/*-----------------------------------------------------------
 * Clear
 *----------------------------------------------------------*/

void
ringbuf_clear(struct RingBuf* rb)
{
    if( rb )
    {
        rb->head = 0;
        rb->tail = 0;
    }
}
