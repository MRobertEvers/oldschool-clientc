#include "world/wev_aboard_trace.h"

#include <assert.h>

bool
WevAboardTrace_Changed(
    struct WevAboardTrace* trace,
    int view_id)
{
    assert(trace);
    if( trace->traced_view == view_id )
        return false;
    trace->traced_view = view_id;
    return true;
}
