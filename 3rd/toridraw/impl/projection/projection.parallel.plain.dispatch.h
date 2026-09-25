#ifndef TORIDRAW_GRAPHICS_PROJECTION_ORTHO_H
#define TORIDRAW_GRAPHICS_PROJECTION_ORTHO_H

/*
 * The parallel-projection kernels' ISA lanes: what a lane provides, and how
 * the four kernels ask for it.
 *
 * projection_ortho.u.c explains what a parallel projection is and why there
 * are four kernels. This header is about the other axis of the same table: each
 * of those four has a vector body written once per instruction set, and WHICH
 * body a build compiles is a property of the machine, not of the kernel. They
 * used to be the same function -- one `project_vertices_array_ortho_fused_*`
 * with a NEON block, an AVX2 block and an SSE block stacked inside it ahead of
 * the scalar tail, four times over, so twelve vector loops lived in four
 * functions and the tail that follows each was written under the last `#endif`.
 *
 *   projection_ortho.u.c        the four kernels, their scalar tails, the
 *                              6DOF family that has no vector body at all,
 *                              and the lane selection
 *   projection_ortho.neon.u.c   AArch64: four lanes, vmovl_s16 loads
 *   projection_ortho.avx.u.c    AVX2: eight lanes, vpmulld
 *   projection_ortho.sse2.u.c   SSE2: four lanes, mullo_epi32_sse from
 *                              sse2_41compat.h, and an and/andnot/or blend
 *                              because pblendvb is SSE4.1
 *   projection_ortho.none.u.c   everywhere else -- wasm and the scalar builds.
 *                              All four hooks decline and the tail loop IS the
 *                              kernel, which is the arrangement the reference
 *                              results are defined by.
 *
 * THE HOOK CONTRACT. Each lane defines all four of these, under these exact
 * names, which is what lets projection_ortho.u.c carry no preprocessor:
 *
 *   int toridraw_ortho_lane_fused(...)
 *   int toridraw_ortho_lane_fused_clip(...)
 *   int toridraw_ortho_lane_fused_notex(...)
 *   int toridraw_ortho_lane_fused_notex_clip(...)
 *
 * Each projects a whole number of LEADING vertices -- always vertex 0 upward,
 * never a hole -- and returns how many it wrote. The caller finishes
 * [return, num_vertices) with the scalar body, so a lane may consume any
 * multiple of its vector width it likes, and a lane with no kernel returns 0
 * and the caller's loop covers everything. There is no third answer: a lane
 * either wrote exactly the first N vertices of every output array it was
 * handed, or it wrote nothing.
 */

#include "graphics/dash_restrict.h"
#include "graphics/dash_vertexint.h"
#include "impl/projection/projection.scalar_reference.h"

/*
 * The camera and the model's placement, resolved once per call.
 *
 * The four kernels each turn `model_yaw`, `camera_yaw` and `camera_pitch` into
 * six table reads before anything vectorizes, and every lane body then wants
 * those six plus the scene offset, the zoom and the near plane. Passing them as
 * one block is what keeps the hook signatures to their arrays and a count; the
 * struct is a compile-time fiction in an inlined lane, so nothing is loaded
 * through it at run time.
 */
struct ToriDraw_OrthoFusedCamera
{
    int cos_model_yaw;
    int sin_model_yaw;
    int cos_camera_yaw;
    int sin_camera_yaw;
    int cos_camera_pitch;
    int sin_camera_pitch;
    int scene_x;
    int scene_y;
    int scene_z;
    int camera_zoom16;
    int model_mid_z;
    /* Read by the two *_clip hooks only. The noclip pair is handed a zero and
     * never looks: with no divide there is no singularity to guard against,
     * which is the whole reason the noclip kernels exist. */
    int near_plane_z;
};

/* A lane with no kernel for the camera-space (textured) families. */
#define TORIDRAW_ORTHO_LANE_DECLINE(cam, ox, oy, oz, sx, sy, sz, vx, vy, vz, num_vertices)         \
    ((void)(cam),                                                                                  \
     (void)(ox),                                                                                   \
     (void)(oy),                                                                                   \
     (void)(oz),                                                                                   \
     (void)(sx),                                                                                   \
     (void)(sy),                                                                                   \
     (void)(sz),                                                                                   \
     (void)(vx),                                                                                   \
     (void)(vy),                                                                                   \
     (void)(vz),                                                                                   \
     (void)(num_vertices),                                                                         \
     0)

/* A lane with no kernel for the screen-only (untextured) families. */
#define TORIDRAW_ORTHO_LANE_DECLINE_NOTEX(cam, sx, sy, sz, vx, vy, vz, num_vertices)               \
    ((void)(cam),                                                                                  \
     (void)(sx),                                                                                   \
     (void)(sy),                                                                                   \
     (void)(sz),                                                                                   \
     (void)(vx),                                                                                   \
     (void)(vy),                                                                                   \
     (void)(vz),                                                                                   \
     (void)(num_vertices),                                                                         \
     0)

#endif /* TORIDRAW_GRAPHICS_PROJECTION_ORTHO_H */
