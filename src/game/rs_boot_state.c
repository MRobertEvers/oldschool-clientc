#include "game/rs_boot_state.h"

#include <assert.h>
#include <string.h>

void
RS_BootState_Reset(struct RS_BootState* boot)
{
    assert(boot);
    memset(boot, 0, sizeof(*boot));
}

void
RS_BootState_BakeStarted(
    struct RS_BootState* boot,
    int interface_id,
    bool quiet)
{
    assert(boot);
    boot->interface_id = interface_id;
    if( !quiet )
        boot->progress = 0;
}

void
RS_BootState_SetProgress(
    struct RS_BootState* boot,
    int percent)
{
    assert(boot);
    assert(percent >= 0);
    boot->progress = percent;
}

int
RS_BootState_Progress(struct RS_BootState const* boot)
{
    assert(boot);
    return boot->progress;
}

int
RS_BootState_InterfaceId(struct RS_BootState const* boot)
{
    assert(boot);
    return boot->interface_id;
}

bool
RS_BootState_ConfigReady(struct RS_BootState const* boot)
{
    assert(boot);
    return boot->config_ready != 0;
}

void
RS_BootState_MarkConfigReady(struct RS_BootState* boot)
{
    assert(boot);
    boot->config_ready = 1;
}
