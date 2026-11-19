#include "net/session_end.h"

#include <assert.h>
#include <string.h>

void
SessionEnd_Reset(struct SessionEnd* end)
{
    assert(end);
    memset(end, 0, sizeof(*end));
}

void
SessionEnd_RequestLogout(struct SessionEnd* end)
{
    assert(end);
    end->logout_requested = 1;
}

enum SessionEndAction
SessionEnd_Step(
    struct SessionEnd* end,
    bool session_up)
{
    assert(end);

    if( !end->logout_requested )
        return SESSION_END_IDLE;
    end->logout_requested = 0;

    /* Nobody to wait for: the request IS the ending. */
    if( !session_up )
        return SESSION_END_LOGOUT;

    /* Already waiting: the wait stays armed as it is. */
    if( end->logout_wait_cycles > 0 )
        return SESSION_END_IDLE;

    end->logout_wait_cycles = SESSION_END_LOGOUT_WAIT_CYCLES;
    return SESSION_END_WAIT_ARMED;
}

bool
SessionEnd_LogoutPending(struct SessionEnd const* end)
{
    assert(end);
    return end->logout_wait_cycles > 0 || end->logout_requested;
}

void
SessionEnd_Hop(
    struct SessionEnd* end,
    int world)
{
    assert(end);
    end->hop_pending = 1;
    end->hop_world = world;
}

bool
SessionEnd_TakeHop(
    struct SessionEnd* end,
    int* out_world)
{
    assert(end);
    assert(out_world);
    if( !end->hop_pending )
        return false;
    end->hop_pending = 0;
    *out_world = end->hop_world;
    return true;
}
