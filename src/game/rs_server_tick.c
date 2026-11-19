#include "game/rs_server_tick.h"

#include <assert.h>
#include <string.h>

void
RS_ServerTick_Reset(struct RS_ServerTick* tick)
{
    assert(tick);
    memset(tick, 0, sizeof(*tick));
}

bool
RS_ServerTick_NotePacket(
    struct RS_ServerTick* tick,
    bool plain_varp,
    uint64_t cycle)
{
    bool opened = false;

    assert(tick);
    if( tick->open && !plain_varp )
        tick->open_nonvar = 1;
    if( !tick->open )
    {
        tick->open_nonvar = !plain_varp;
        tick->open_cycle = (int)cycle;
        opened = true;
    }
    tick->open = 1;
    return opened;
}

void
RS_ServerTick_Fence(struct RS_ServerTick* tick)
{
    assert(tick);
    tick->fence_seen = 1;
    tick->open = 0;
    tick->serial++;
}

void
RS_ServerTick_EndUnfenced(struct RS_ServerTick* tick)
{
    assert(tick);
    tick->serial++;
}

void
RS_ServerTick_Close(struct RS_ServerTick* tick)
{
    assert(tick);
    tick->open = 0;
}

bool
RS_ServerTick_Overdue(
    struct RS_ServerTick const* tick,
    uint64_t cycle,
    uint64_t max_cycles)
{
    assert(tick);
    return tick->open && cycle - (uint64_t)(int64_t)tick->open_cycle >= max_cycles;
}

bool
RS_ServerTick_Suspend(struct RS_ServerTick* tick)
{
    bool was_open;

    assert(tick);
    was_open = tick->open != 0;
    tick->open = 0;
    return was_open;
}

void
RS_ServerTick_Resume(
    struct RS_ServerTick* tick,
    uint64_t cycle)
{
    assert(tick);
    tick->open = 1;
    tick->open_cycle = (int)cycle;
}

bool
RS_ServerTick_IsOpen(struct RS_ServerTick const* tick)
{
    assert(tick);
    return tick->open != 0;
}

bool
RS_ServerTick_OpenNonVar(struct RS_ServerTick const* tick)
{
    assert(tick);
    return tick->open_nonvar != 0;
}

bool
RS_ServerTick_FenceSeen(struct RS_ServerTick const* tick)
{
    assert(tick);
    return tick->fence_seen != 0;
}

unsigned
RS_ServerTick_Serial(struct RS_ServerTick const* tick)
{
    assert(tick);
    return tick->serial;
}
