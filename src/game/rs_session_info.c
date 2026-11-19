#include "game/rs_session_info.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
RS_SessionInfo_Reset(struct RS_SessionInfo* info)
{
    assert(info);
    memset(info, 0, sizeof(*info));
}

void
RS_SessionInfo_SetRebootTimer(
    struct RS_SessionInfo* info,
    int ticks,
    int cycles_per_tick)
{
    assert(info);
    info->reboot_timer = ticks * cycles_per_tick;
}

bool
RS_SessionInfo_StepRebootTimer(struct RS_SessionInfo* info)
{
    assert(info);
    if( info->reboot_timer > 1 )
    {
        info->reboot_timer--;
        return true;
    }
    return false;
}

char const*
RS_SessionInfo_RebootTimerText(
    struct RS_SessionInfo* info,
    int cycles_per_second)
{
    int seconds;
    int minutes;

    assert(info);
    if( info->reboot_timer == 0 )
        return NULL;

    seconds = info->reboot_timer / cycles_per_second;
    minutes = seconds / 60;
    seconds %= 60;
    snprintf(
        info->reboot_timer_text,
        sizeof(info->reboot_timer_text),
        "System update in: %d:%02d",
        minutes,
        seconds);
    return info->reboot_timer_text;
}

bool
RS_SessionInfo_SetMultiway(
    struct RS_SessionInfo* info,
    int multiway)
{
    assert(info);
    if( info->multiway == multiway )
        return false;
    info->multiway = multiway;
    return true;
}

int
RS_SessionInfo_Multiway(struct RS_SessionInfo const* info)
{
    assert(info);
    return info->multiway;
}
