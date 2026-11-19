#include "ui/dev_overlay.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>

struct DevOverlay*
DevOverlay_Create(void)
{
    /* UI: the developer chrome and its panels' state; DevOverlay_Free
     * releases it. */
    struct DevOverlay* overlay = ToriMem_Calloc(ToriMem_Ui(), 1, sizeof(*overlay));

    FrameTimeRing_Reset(&overlay->frame_times);
    return overlay;
}

void
DevOverlay_Free(struct DevOverlay* overlay)
{
    if( !overlay )
        return;
    ToriMem_Free(ToriMem_Ui(), overlay);
}

void
DevOverlay_FrameTimeText(
    uint32_t mean_us,
    char* out,
    size_t out_size)
{
    assert(out);
    assert(out_size > 0);
    /* Two decimals: the samples are microseconds, and rounding a 3.4 ms
     * frame to "3 ms" throws away the part that moves. */
    if( mean_us > 0 )
        snprintf(
            out,
            out_size,
            "%u.%02u ms",
            (unsigned)(mean_us / 1000u),
            (unsigned)((mean_us % 1000u) / 10u));
    else
        snprintf(out, out_size, "--");
}
