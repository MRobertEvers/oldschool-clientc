#include "render/overlay_stage.h"

#include <assert.h>
#include <string.h>

void
OverlayStage_Reset(struct OverlayStage* stage)
{
    assert(stage);
    /* The counts, not the arrays: between them they are most of a megabyte,
     * and rewriting that every frame would cost more than everything in it.
     * Nothing reads past a count. */
    stage->world_count = 0;
    stage->canvas_count = 0;
    stage->batch_started = false;
    stage->canvas_prepared = false;
}

void
OverlayStage_ResetWorld(struct OverlayStage* stage)
{
    assert(stage);
    stage->world_count = 0;
}

void
OverlayStage_ResetCanvas(struct OverlayStage* stage)
{
    assert(stage);
    stage->canvas_count = 0;
}

bool
OverlayStage_Push(
    struct OverlayStage* stage,
    enum OverlaySurface surface,
    struct UITreeEntityOverlay const* item)
{
    assert(stage);
    assert(item);

    if( surface == OVERLAY_SURFACE_CANVAS )
    {
        if( stage->canvas_count >= OVERLAY_STAGE_CANVAS_MAX )
            return false;
        stage->canvas[stage->canvas_count++] = *item;
        return true;
    }
    /* A panel's drawing is staged elsewhere, and this is the boundary that
     * says so. Falling through to the world list would paint a panel's
     * contents across the scene in panel coordinates. */
    if( surface == OVERLAY_SURFACE_PANEL )
        return false;

    if( stage->world_count >= OVERLAY_STAGE_WORLD_MAX )
        return false;
    stage->world[stage->world_count++] = *item;
    return true;
}

int
OverlayStage_Count(struct OverlayStage const* stage, enum OverlaySurface surface)
{
    assert(stage);
    switch( surface )
    {
    case OVERLAY_SURFACE_CANVAS: return stage->canvas_count;
    /* None of a panel's drawing is here, so this stage has none of it to
     * report -- not "some number that happens to be the world's". */
    case OVERLAY_SURFACE_PANEL: return 0;
    default: return stage->world_count;
    }
}

struct UITreeEntityOverlay const*
OverlayStage_Items(struct OverlayStage const* stage, enum OverlaySurface surface)
{
    assert(stage);
    switch( surface )
    {
    case OVERLAY_SURFACE_CANVAS: return stage->canvas;
    case OVERLAY_SURFACE_PANEL: return NULL;
    default: return stage->world;
    }
}
