/*
 * The live flattened hull: the deob's own rendering (class467.method10419).
 *
 * Nothing is built here. The deck is bound exactly as a full-detail hull is,
 * and the frame transform carries the deob's numbers -- Y scale 0.01, a
 * pre-scale offset of -1200 and the config's flat HSL -- which the frame emitter
 * applies to a copy of every model it draws inside the view
 * (src/render/torirs_frame_flat.u.h).
 *
 * @see wev_flat.h
 */
#include "render/wev_flat.h"

#include "painters/painters.h"
#include "render/torirs_frame.h"
#include "world/wev.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>

static void*
live_create(void)
{
    return NULL;
}

static void
live_destroy(
    void* state,
    struct ToriDraw_Scene* scene)
{
    (void)state;
    (void)scene;
}

static void
live_bind_painter(
    void* state,
    struct ToriDraw_Scene* scene,
    struct WorldviewRegistry* worldviews,
    struct Wev const* wev,
    struct Painter* parent_painter,
    int camera_sx,
    int camera_sz,
    bool debug)
{
    struct Worldview* view;

    (void)state;
    (void)scene;
    (void)debug;
    assert(worldviews);
    assert(wev);
    assert(parent_painter);
    assert(WorldviewRegistry_IsLive(worldviews, wev->id));

    view = WorldviewRegistry_Get(worldviews, wev->id);
    assert(view->world);
    assert(view->world->painter);
    painter_set_world_entity_view(
        parent_painter, wev->id, view->world->painter, camera_sx, camera_sz, 0);
}

static void
live_bind_frame(
    void* state,
    struct Wev const* wev,
    struct ToriRS_FrameViewXform* xf)
{
    (void)state;
    assert(wev);
    assert(wev->config);
    assert(xf);
    xf->flatten_scale = 0.01f;
    xf->flatten_y_offset = -1200;
    xf->flat_hsl = wev->config->flat_hsl;
    xf->flat = true;
}

static int
live_claim_elements(
    void* state,
    int* out_element_ids,
    int max)
{
    (void)state;
    (void)out_element_ids;
    (void)max;
    assert(out_element_ids);
    assert(max >= 0);
    return 0;
}

static void
live_release_view(
    void* state,
    struct ToriDraw_Scene* scene,
    int view_id)
{
    (void)state;
    (void)scene;
    (void)view_id;
}

struct WevFlatOps const g_wev_flat_live = {
    .name = "live",
    .create = live_create,
    .destroy = live_destroy,
    .bind_painter = live_bind_painter,
    .bind_frame = live_bind_frame,
    .claim_elements = live_claim_elements,
    .release_view = live_release_view,
};
