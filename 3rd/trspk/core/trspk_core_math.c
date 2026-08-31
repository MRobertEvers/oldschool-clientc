#include "trspk_math.h"

#include "impl/projection/projection.scalar_reference.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Depth range for the parallel projection, in world units either side of the
 * camera. Generous on purpose: under parallel projection depth is only an
 * ordering, and a range too small silently clips the far half of a map.
 */
#define TRSPK_PARALLEL_DEPTH_RANGE 65536.0f

void
trspk_compute_pass_matrices(
    float view[16],
    float proj[16],
    float cam_x,
    float cam_y,
    float cam_z,
    float pitch_rad,
    float yaw_rad,
    int pass_w,
    int pass_h,
    int projection_mode,
    int projection_scale,
    int fov_rpi2048,
    int parallel_zoom16)
{
    /* cam_* is the camera's world position. trspk_compute_view_matrix applies
     * the -camera translation itself; do not pre-negate here. */
    trspk_compute_view_matrix(view, cam_x, cam_y, cam_z, pitch_rad, yaw_rad);

    if( toridraw_projection_is_parallel(projection_mode) )
    {
        float const zoom =
            (float)(parallel_zoom16 > 0 ? parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT) /
            (float)TORIDRAW_ORTHO_ZOOM_UNIT;
        trspk_compute_projection_matrix_parallel(
            proj, zoom, (float)pass_w, (float)pass_h, TRSPK_PARALLEL_DEPTH_RANGE);
        return;
    }

    /*
     * The same resolution the rasterizer's kernels do, through the same header.
     * cot16 is a 16.16 multiplier on UNIT_SCALE, so the linear scale the
     * projection formula wants is cot16 >> TORIDRAW_PROJECTION_COT16_SHIFT — kept in
     * float here rather than shifted, because a scale is not always an integer
     * and rounding it would reintroduce the error this call exists to remove.
     */
    {
        int const cot16 = toridraw_projection_cot16(projection_mode, projection_scale, fov_rpi2048);
        float const scale = (float)cot16 / (float)(1 << TORIDRAW_PROJECTION_COT16_SHIFT);
        trspk_compute_projection_matrix(proj, scale, (float)pass_w, (float)pass_h);
    }
}
