#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_BUF_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_BUF_H

/*
 * A growable one-way byte queue, contiguous with a read offset.
 *
 * Same shape as ToriRSServerPipe and for the same reason: in-process, client
 * and server share one thread, so a queue that blocks is a deadlock and a
 * queue that drops corrupts the stream — growing is the only behaviour with
 * no failure mode. It is a separate type rather than a reuse because the
 * game server's pipe lives in torirs_server_transport.c, whose other half
 * (the socket transport) drags the whole ws layer into any binary that links
 * it; ToriRSMapEd must link into a client that carries no game server.
 *
 * The Peek/Consume pair is what the frame parser wants: a frame's payload is
 * contiguous at `data + head`, so it can be decoded in place and consumed,
 * with no copy per frame.
 */

#include <stdint.h>

struct ToriRSMapEdBuf
{
    uint8_t* data;
    int cap;
    /** Read offset into `data`; bytes before it are consumed. */
    int head;
    /** Write offset. Live bytes are [head, tail). */
    int tail;
    int closed;
};

/** Append. Returns `len`, or -1 once closed. */
int
ToriRSMapEd_BufWrite(
    struct ToriRSMapEdBuf* buf,
    const uint8_t* src,
    int len);

/** Take up to `max`. Returns the count, 0 when empty, or -1 when the writer
 *  closed *and* nothing is left — a reader drains before it sees the end. */
int
ToriRSMapEd_BufRead(
    struct ToriRSMapEdBuf* buf,
    uint8_t* dst,
    int max);

/** Live bytes. */
int
ToriRSMapEd_BufAvailable(const struct ToriRSMapEdBuf* buf);

/** The live bytes, in place. Valid until the next write or consume. */
const uint8_t*
ToriRSMapEd_BufPeek(const struct ToriRSMapEdBuf* buf);

/** Drop `count` bytes off the front, as if read. */
void
ToriRSMapEd_BufConsume(
    struct ToriRSMapEdBuf* buf,
    int count);

void
ToriRSMapEd_BufClose(struct ToriRSMapEdBuf* buf);

void
ToriRSMapEd_BufFree(struct ToriRSMapEdBuf* buf);

#endif
