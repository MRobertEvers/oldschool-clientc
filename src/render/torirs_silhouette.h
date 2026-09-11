#ifndef SRC_RENDER_TORIRS_SILHOUETTE_H
#define SRC_RENDER_TORIRS_SILHOUETTE_H

#include <stdbool.h>
#include <stdint.h>

/* A mesh coverage mask, in canvas pixels. Triangle coverage, including holes
 * and disconnected parts, is retained until the contour is built. Foreground
 * surfaces attenuate that completed contour; an occluder's cut through a mesh
 * must not become a new highlighted edge. */
struct ToriRS_Silhouette
{
    int x, y, width, height;
    float* depth;
    int* order;
    uint8_t* coverage;
    uint8_t* alpha;
};

struct ToriRS_SilhouetteVertex
{
    float x, y;
    /* Larger is nearer: reciprocal camera z for perspective, -z for parallel. */
    float depth;
    float u, v, q;
};

struct ToriRS_SilhouetteTexture
{
    const uint32_t* pixels;
    int width, height;
    bool color_key;
    bool texel_alpha;
};

void ToriRS_SilhouetteInit(struct ToriRS_Silhouette* mask,
                         int x, int y, int width, int height);
void ToriRS_SilhouetteFree(struct ToriRS_Silhouette* mask);

/* The caller clips whole faces at the near plane before triangulating them.
 * Non-textured triangles pass textured=false; texture is otherwise required.
 * For a subject, coverage/depth/order are collected. For a foreground model,
 * the completed alpha mask is attenuated according to the active renderer's
 * painter order or depth testing. */
void ToriRS_SilhouetteTriangle(
    struct ToriRS_Silhouette* mask,
    const struct ToriRS_SilhouetteVertex vertices[3],
    int opacity, bool textured, const struct ToriRS_SilhouetteTexture* texture,
    bool subject, int draw_order, bool depth_test);

/* Width zero means fill only. The outline is outside the actual mesh coverage;
 * it does not consume the fill, close concavities, or trace internal faces. */
void ToriRS_SilhouetteStyle(struct ToriRS_Silhouette* mask,
                          int fill_alpha, int outline_width);

#endif
