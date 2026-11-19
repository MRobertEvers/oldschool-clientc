#include "game/rs_client_phase.h"

#include <assert.h>

void
RS_ClientPhase_Reset(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->app_state = APP_STATE_BOOTING;
    phase->screen = APP_SCREEN_BOOT;
    phase->hold_last_frame = 0;
}

void
RS_ClientPhase_BakeStarted(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->app_state = APP_STATE_BOOTING;
    phase->hold_last_frame = 0;
}

void
RS_ClientPhase_BakeSettled(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->app_state = APP_STATE_READY;
    phase->hold_last_frame = 0;
}

void
RS_ClientPhase_LogoutHold(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->hold_last_frame = 1;
}

void
RS_ClientPhase_BootBake(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->screen = APP_SCREEN_BOOT;
}

void
RS_ClientPhase_ShowTitle(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->screen = APP_SCREEN_TITLE;
}

void
RS_ClientPhase_LoginSubmitted(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->screen = APP_SCREEN_CONNECTING;
}

void
RS_ClientPhase_GameRooted(struct RS_ClientPhase* phase)
{
    assert(phase);
    phase->screen = APP_SCREEN_GAME;
}

bool
RS_ClientPhase_Ready(struct RS_ClientPhase const* phase)
{
    assert(phase);
    return phase->app_state == APP_STATE_READY;
}

bool
RS_ClientPhase_Booting(struct RS_ClientPhase const* phase)
{
    assert(phase);
    return phase->app_state == APP_STATE_BOOTING;
}

int
RS_ClientPhase_Screen(struct RS_ClientPhase const* phase)
{
    assert(phase);
    return phase->screen;
}

bool
RS_ClientPhase_OnTitle(struct RS_ClientPhase const* phase)
{
    assert(phase);
    return phase->screen == APP_SCREEN_TITLE || phase->screen == APP_SCREEN_CONNECTING;
}

bool
RS_ClientPhase_HoldsLastFrame(struct RS_ClientPhase const* phase)
{
    assert(phase);
    return phase->app_state == APP_STATE_BOOTING && phase->hold_last_frame;
}
