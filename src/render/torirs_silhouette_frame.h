#ifndef SRC_RENDER_TORIRS_SILHOUETTE_FRAME_H
#define SRC_RENDER_TORIRS_SILHOUETTE_FRAME_H

#include "render/torirs_silhouette.h"

struct ToriRS_Frame;
struct ToriDraw_RasterTarget;
struct ToriDraw_RasterFaceSD;

/* Returns NULL for a live scene with no drawable coverage. The caller owns the
 * returned mask and frees it with SilhouetteFree followed by free. */
struct ToriRS_Silhouette* ToriRS_SilhouetteBuildFrame(
    struct ToriRS_Frame* frame, int element_id, int fill_alpha, int outline_width,
    bool always_on_top, int clip_x, int clip_y, int clip_w, int clip_h);

/* Narrow normalized-face seam, also exercised by the renderer fixture. */
void ToriRS_SilhouetteDrawFace(
    struct ToriRS_Silhouette* mask,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face,
    bool subject, int draw_order, bool depth_test);

#endif
