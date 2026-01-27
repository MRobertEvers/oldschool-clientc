#ifndef RINGBUF_H
#define RINGBUF_H

/**
 * Byte-oriented ring buffer. One slot is reserved to distinguish full from
 * empty, so the maximum storable bytes is capacity - 1.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define RINGBUF_OK      0
#define RINGBUF_NOMEM   1
#define RINGBUF_BADARG  2
#define RINGBUF_FULL    3
#define RINGBUF_EMPTY   4

struct RingBuf;

/* Constructor/Destructor */
struct RingBuf* ringbuf_new(size_t capacity);
void ringbuf_free(struct RingBuf* rb);

/* Initialize with caller-provided buffer (no allocation) */
int ringbuf_init(struct RingBuf* rb, void* buffer, size_t capacity);
void ringbuf_deinit(struct RingBuf* rb);

/* Capacity and state */
size_t ringbuf_capacity(const struct RingBuf* rb);
size_t ringbuf_used(const struct RingBuf* rb);
size_t ringbuf_avail(const struct RingBuf* rb);
bool ringbuf_empty(const struct RingBuf* rb);
bool ringbuf_full(const struct RingBuf* rb);

/* Write (add bytes to buffer) */
int ringbuf_write(struct RingBuf* rb, const void* data, size_t len);
int ringbuf_putc(struct RingBuf* rb, unsigned char c);

/* Putback (return bytes to the front of the read head; they will be read next) */
int ringbuf_putback(struct RingBuf* rb, const void* data, size_t len);

/* Read (remove bytes from buffer, copy to dst) */
size_t ringbuf_read(struct RingBuf* rb, void* dst, size_t len);
int ringbuf_getc(struct RingBuf* rb, unsigned char* out);

/* Peek (view bytes without removing) */
size_t ringbuf_peek(struct RingBuf* rb, void* dst, size_t len);
size_t ringbuf_peek_at(const struct RingBuf* rb, size_t offset, void* dst, size_t len);

/* Discard bytes from the front without copying */
size_t ringbuf_skip(struct RingBuf* rb, size_t len);

/* Clear all data */
void ringbuf_clear(struct RingBuf* rb);

#endif
