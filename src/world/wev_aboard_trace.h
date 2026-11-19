#ifndef SRC_WORLD_WEV_ABOARD_TRACE_H
#define SRC_WORLD_WEV_ABOARD_TRACE_H

/*
 * The TORIRS_WEV_DEBUG trace of which world view the local player is aboard:
 * the view the last trace line named, so the per-tick routing pass prints
 * only when the answer changes.
 *
 * A zeroed record has traced WORLDVIEW_ROOT (0), which is also the answer the
 * first routing pass computes for a player aboard nothing, so a client that
 * boards nothing never prints.
 *
 * This decides whether a view id is a change worth a line, and remembers it
 * when it is.
 *
 * Nothing here reads the environment, the players or the views, or prints.
 * The caller decides whether tracing is on, computes the view, and writes the
 * line.
 */

#include <stdbool.h>

struct WevAboardTrace
{
    /** The view id the last trace line named; WORLDVIEW_ROOT when zeroed. */
    int traced_view;
};

/** Whether `view_id` differs from the view last traced; when it does, it is
 *  remembered as traced. */
bool
WevAboardTrace_Changed(
    struct WevAboardTrace* trace,
    int view_id);

#endif
