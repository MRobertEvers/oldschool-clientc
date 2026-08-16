#ifndef PAINTERS_CULL_PROJECT_H
#define PAINTERS_CULL_PROJECT_H

#include "painters.h"

struct ToriDrawTrigFns;

/** PaintersProjectFn backed by toridraw project_fast_trig (model origin/yaw fixed at 0).
 * user must be a non-NULL const struct ToriDrawTrigFns*. */
void
painters_cull_project(
    struct PaintersProjectedVertex* out,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_cot16,
    int near_clip,
    int screen_width,
    int screen_height,
    void* user);

/** Convenience: bake with toridraw projection + trig callbacks.
 *  camera_cot16 is the resolved projection multiplier (toridraw_proj_cot16). */
struct PaintersCullMap*
painters_cullmap_build_toridraw(
    int radius,
    int near_clip_z,
    int screen_width,
    int screen_height,
    int camera_cot16,
    const struct ToriDrawTrigFns* trig);

#endif /* PAINTERS_CULL_PROJECT_H */
