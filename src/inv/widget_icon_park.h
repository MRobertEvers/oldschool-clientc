#ifndef SRC_INV_WIDGET_ICON_PARK_H
#define SRC_INV_WIDGET_ICON_PARK_H

/*
 * Widget obj-icon requests a clientscript made, held until a logic tick
 * releases them a bounded burst at a time.
 *
 * Keyed by component, newest wins: a script that rewrites one cell a dozen
 * times leaves one request, naming what the cell wants now. A request the
 * script made is due at once; one put back by a land whose parts were still
 * missing is due after its wait. A release takes the due requests from the
 * head, up to the burst, and the rest keep their places in line.
 *
 * Nothing here reads a clock, a cell or a task runner. The caller passes the
 * time and the burst, fills in each request's obj, count and wait, and queues
 * the loads for what a release hands back.
 */

#include <stdbool.h>
#include <stdint.h>

/* One cell's request. */
struct WidgetIconRequest
{
    int com_id;
    int obj_id;
    int count;
    /* A request the script made is due at once (0). One put back by a land
     * that found its parts still missing is due after `wait_ms`, which the
     * next miss doubles (InvIconRetry_NextWait, inv/inv_icon_retry.h). */
    uint64_t due_ms;
    uint32_t wait_ms;
};

struct WidgetIconPark
{
    struct WidgetIconRequest* icons;
    int count;
    int cap;
};

/** An empty park, on the process heap. */
struct WidgetIconPark*
WidgetIconPark_New(void);

/** Free the park and its requests. NULL is a no-op. */
void
WidgetIconPark_Free(struct WidgetIconPark* park);

/** The cell's request, made if it had none; `*found` says which. A new row
 *  has only its component set, and the caller fills in everything else. */
struct WidgetIconRequest*
WidgetIconPark_Row(
    struct WidgetIconPark* park,
    int component_id,
    bool* found);

int
WidgetIconPark_Count(struct WidgetIconPark const* park);

/**
 * Take up to `max` requests due at `now_ms`, from the head, into `out` (room
 * for `max`), and close the line up behind them. Returns how many were taken.
 */
int
WidgetIconPark_TakeDue(
    struct WidgetIconPark* park,
    uint64_t now_ms,
    int max,
    struct WidgetIconRequest* out);

#endif
