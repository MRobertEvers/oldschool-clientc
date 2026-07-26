#include "trspk_math.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    int pass_h)
{
    /* cam_* is the camera's world position. trspk_compute_view_matrix applies
     * the -camera translation itself; do not pre-negate here. */
    trspk_compute_view_matrix(view, cam_x, cam_y, cam_z, pitch_rad, yaw_rad);
    trspk_compute_projection_matrix(
        proj, (90.0f * (float)M_PI) / 180.0f, (float)pass_w, (float)pass_h);
}
