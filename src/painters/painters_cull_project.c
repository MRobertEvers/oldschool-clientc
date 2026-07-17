#include "painters_cull_project.h"

#include <assert.h>

/* projection.u.c is include-guarded (PROJECTION_U_C). */
/* clang-format off */
#include "graphics/projection.u.c"
/* clang-format on */

void
painters_cull_project(
    struct PaintersProjectedVertex* out,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int fov,
    int near_clip,
    int screen_width,
    int screen_height,
    void* user)
{
    const struct ToriDrawTrigFns* trig = (const struct ToriDrawTrigFns*)user;
    assert(trig && trig->sin && trig->cos && trig->tan);

    struct ProjectedVertex pv;
    project_fast_trig(
        &pv,
        0,
        0,
        0,
        0,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw,
        fov,
        near_clip,
        screen_width,
        screen_height,
        trig);
    out->x = pv.x;
    out->y = pv.y;
    out->z = pv.z;
    out->clipped = pv.clipped;
}

struct PaintersCullMap*
painters_cullmap_build_toridraw(
    int radius,
    int near_clip_z,
    int screen_width,
    int screen_height,
    const struct ToriDrawTrigFns* trig)
{
    assert(trig && trig->sin && trig->cos && trig->tan);
    return painters_cullmap_build(
        radius,
        near_clip_z,
        screen_width,
        screen_height,
        painters_cull_project,
        (void*)trig,
        (PaintersSinFn)trig->sin);
}
