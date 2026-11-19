#include "render/render_mode.h"

#include <assert.h>

void
RenderMode_SetWorld(
    struct RenderMode* rm,
    enum ToriRS_WorldRenderMode mode)
{
    assert(rm);
    rm->world = mode;
}

enum ToriRS_WorldRenderMode
RenderMode_World(struct RenderMode const* rm)
{
    assert(rm);
    return rm->world;
}

void
RenderMode_SetRendererAnimatesTextures(
    struct RenderMode* rm,
    bool animates)
{
    assert(rm);
    rm->renderer_animates_textures = animates;
}

bool
RenderMode_RendererAnimatesTextures(struct RenderMode const* rm)
{
    assert(rm);
    return rm->renderer_animates_textures;
}

void
RenderMode_SetSoftwareRsZBuffer(
    struct RenderMode* rm,
    bool rszbuffer)
{
    assert(rm);
    rm->software_rszbuffer = rszbuffer;
}

void
RenderMode_SetServerZbuf(
    struct RenderMode* rm,
    int enabled)
{
    assert(rm);
    rm->server_zbuf = enabled;
}

void
RenderMode_ForgetServerZbuf(struct RenderMode* rm)
{
    assert(rm);
    rm->server_zbuf = -1;
}

bool
RenderMode_SoftwareDepth(struct RenderMode const* rm)
{
    assert(rm);
    return rm->world == TORIRS_WORLD_DEPTH;
}

bool
RenderMode_SoftwareRsZBuffer(struct RenderMode const* rm)
{
    bool rszbuffer;

    assert(rm);
    rszbuffer = rm->server_zbuf >= 0 ? rm->server_zbuf != 0 : rm->software_rszbuffer;
    return rszbuffer && rm->world == TORIRS_WORLD_PAINTER;
}
