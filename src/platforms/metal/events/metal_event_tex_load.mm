#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_res_tex_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    metal_event_texture_load(ctx, cmd);
}
