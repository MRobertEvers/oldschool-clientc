#include "engine/world_builder/world_load_state.h"

#include <assert.h>

void
WorldLoadState_BeginLocal(struct WorldLoadState* state)
{
    assert(state);
    state->attempted = 1;
    state->inflight = 1;
}

void
WorldLoadState_BeginRebuild(
    struct WorldLoadState* state,
    int background)
{
    assert(state);
    state->attempted = 1;
    if( !background )
        state->inflight = 1;
    state->server_driven = 1;
}

void
WorldLoadState_BeginRepeat(struct WorldLoadState* state)
{
    assert(state);
    state->server_driven = 1;
}

void
WorldLoadState_Stall(struct WorldLoadState* state)
{
    assert(state);
    state->inflight = 1;
}

void
WorldLoadState_Land(struct WorldLoadState* state)
{
    assert(state);
    state->inflight = 0;
}

void
WorldLoadState_Complete(struct WorldLoadState* state)
{
    assert(state);
    state->active = 1;
}

void
WorldLoadState_Acknowledged(struct WorldLoadState* state)
{
    assert(state);
    state->server_driven = 0;
}

void
WorldLoadState_Fail(struct WorldLoadState* state)
{
    assert(state);
    state->server_driven = 0;
}

void
WorldLoadState_Cancel(
    struct WorldLoadState* state,
    int job_idle)
{
    assert(state);
    if( job_idle )
        state->inflight = 0;
    state->server_driven = 0;
}

void
WorldLoadState_Attempt(struct WorldLoadState* state)
{
    assert(state);
    state->attempted = 1;
}

void
WorldLoadState_ForgetAttempt(struct WorldLoadState* state)
{
    assert(state);
    state->attempted = 0;
}

int
WorldLoadState_Active(struct WorldLoadState const* state)
{
    assert(state);
    return state->active;
}

int
WorldLoadState_Attempted(struct WorldLoadState const* state)
{
    assert(state);
    return state->attempted;
}

int
WorldLoadState_Inflight(struct WorldLoadState const* state)
{
    assert(state);
    return state->inflight;
}

int
WorldLoadState_ServerDriven(struct WorldLoadState const* state)
{
    assert(state);
    return state->server_driven;
}

int
WorldLoadState_ServerLoadBlanking(struct WorldLoadState const* state)
{
    assert(state);
    return state->server_driven && state->inflight;
}
