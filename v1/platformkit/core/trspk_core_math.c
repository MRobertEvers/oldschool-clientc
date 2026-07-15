#include "trspk_math.h"

#include "render/libtorirs_render.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_types.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void
trspk_compute_pass_matrices(
    float view[16],
    float proj[16],
    const struct LibToriRS_RenderCommand_Begin3D* b3d,
    int fallback_w,
    int fallback_h)
{
    const struct ToriDraw_Position* cam_pos = &b3d->camera_position;
    const struct ToriDraw_Camera* cam = &b3d->camera;
    const struct ToriDraw_ViewPort* vp = &b3d->view_port;
    const int pass_w = vp->width > 0 ? vp->width : fallback_w;
    const int pass_h = vp->height > 0 ? vp->height : fallback_h;

    trspk_compute_view_matrix(
        view,
        -(float)cam_pos->x,
        -(float)cam_pos->y,
        -(float)cam_pos->z,
        ToriDraw_AngleToRadians(cam->pitch),
        ToriDraw_AngleToRadians(cam->yaw));
    trspk_compute_projection_matrix(
        proj, (90.0f * (float)M_PI) / 180.0f, (float)pass_w, (float)pass_h);
}
