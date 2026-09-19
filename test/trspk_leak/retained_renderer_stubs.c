#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The retained-resource commands under test never enter the compatibility
 * 2D rasterizer or frame iterator.  Stubbing those independent consumers
 * keeps this test CPU-only while linking the real D3D9 command dispatcher. */
struct ToriRS_Soft3D*
ToriRS_Soft3D_New(void)
{
    struct ToriRS_Soft3D* soft = calloc(1, sizeof(*soft));
    assert(soft);
    return soft;
}

void
ToriRS_Soft3D_Free(struct ToriRS_Soft3D* soft)
{
    free(soft);
}

void
ToriRS_Soft3D_Init(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    int* pixels,
    int width,
    int height)
{
    memset(soft, 0, sizeof(*soft));
    soft->scene = scene;
    soft->pixels = pixels;
    soft->width = width;
    soft->height = height;
    soft->stride = width;
}

void
ToriRS_Soft3D_Execute(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* command)
{
    (void)soft;
    (void)command;
}

void
ToriRS_PickHitsReset(struct ToriRS_PickHits* hits)
{
    if( hits )
        hits->count = 0;
}

void
ToriRS_PickHitsAdd(
    struct ToriRS_PickHits* hits,
    int element_id,
    bool is_terrain,
    int tile_x,
    int tile_z,
    int tile_level)
{
    struct ToriRS_PickHit* hit;
    if( !hits || hits->count >= TORIRS_PICK_HITS_MAX )
        return;
    hit = &hits->items[hits->count++];
    hit->element_id = element_id;
    hit->is_terrain = is_terrain;
    hit->tile_x = tile_x;
    hit->tile_z = tile_z;
    hit->tile_level = tile_level;
}

void
ToriRS_FrameBegin(struct ToriRS_Frame* frame)
{
    (void)frame;
}

bool
ToriRS_FrameNextCommand(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    (void)frame;
    (void)out;
    return false;
}

void
ToriRS_FrameEnd(struct ToriRS_Frame* frame)
{
    (void)frame;
}
