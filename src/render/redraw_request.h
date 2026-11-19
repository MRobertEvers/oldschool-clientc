#ifndef SRC_RENDER_REDRAW_REQUEST_H
#define SRC_RENDER_REDRAW_REQUEST_H

/*
 * Whether the next frame has to build and present a new picture.
 *
 * It is a request, raised from anywhere and consumed in one place. Any unit
 * that changes something the picture shows raises it; raising it again before
 * it is consumed changes nothing, so a frame that saw ten changes still draws
 * once. The frame loop reads it to decide whether to run the emit walk, and
 * clears it once that frame is committed -- or keeps it set when the frame it
 * drew must be followed by another.
 *
 * Nothing here knows what changed, what a frame draws, or when a frame runs.
 * The caller raises it, asks whether it is pending, and clears or takes it at
 * the point its frame loop has decided the request was served.
 */

#include <assert.h>
#include <stdbool.h>

struct RedrawRequest
{
    /** Non-zero while a raised request has not been cleared or taken. */
    int pending;
};

/** Ask for the next frame to draw. */
static inline void
RedrawRequest_Raise(struct RedrawRequest* req)
{
    assert(req);
    req->pending = 1;
}

/** A request is waiting. Reading it does not consume it. */
static inline bool
RedrawRequest_Pending(const struct RedrawRequest* req)
{
    assert(req);
    return req->pending != 0;
}

/** Drop the request: the frame that served it is done. */
static inline void
RedrawRequest_Clear(struct RedrawRequest* req)
{
    assert(req);
    req->pending = 0;
}

/** Whether a request was waiting, and drop it. */
static inline bool
RedrawRequest_Take(struct RedrawRequest* req)
{
    bool was;

    assert(req);
    was = req->pending != 0;
    req->pending = 0;
    return was;
}

#endif
