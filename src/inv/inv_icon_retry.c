#include "inv/inv_icon_retry.h"

#include <assert.h>

uint32_t
InvIconRetry_NextWait(uint32_t wait_ms)
{
    if( wait_ms == 0 )
        return INV_ICON_RETRY_FIRST_MS;
    return wait_ms * 2u > INV_ICON_RETRY_MAX_MS ? INV_ICON_RETRY_MAX_MS : wait_ms * 2u;
}

bool
InvIconRetry_Due(
    struct InvIconRetry const* retry,
    uint64_t now_ms)
{
    assert(retry);
    return !retry->inflight && now_ms >= retry->next_pass_ms;
}

void
InvIconRetry_Begin(struct InvIconRetry* retry)
{
    assert(retry);
    retry->inflight = 1;
}

void
InvIconRetry_NotePass(
    struct InvIconRetry* retry,
    int resolved,
    uint64_t now_ms)
{
    assert(retry);
    if( resolved > 0 )
    {
        retry->wait_ms = 0;
        retry->next_pass_ms = 0;
    }
    else
    {
        retry->wait_ms = InvIconRetry_NextWait(retry->wait_ms);
        retry->next_pass_ms = now_ms + retry->wait_ms;
    }
}

void
InvIconRetry_End(struct InvIconRetry* retry)
{
    assert(retry);
    retry->inflight = 0;
}

void
InvIconRetry_Lift(struct InvIconRetry* retry)
{
    assert(retry);
    retry->next_pass_ms = 0;
}

bool
InvIconRetry_InFlight(struct InvIconRetry const* retry)
{
    assert(retry);
    return retry->inflight != 0;
}
