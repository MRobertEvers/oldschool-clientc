#ifndef TORIDRAW_RSZBUF_TEXTURE_PLANES_H
#define TORIDRAW_RSZBUF_TEXTURE_PLANES_H

/*
 * The three homogeneous texture planes of a rev-241 textured triangle
 * [the setup half of class162.method5170 / method5171].
 *
 * u, v and w are planes over the screen whose ratios u/w and v/w are the
 * texture coordinate; each is a base at the projection centre plus a step per
 * screen column and per screen row. They are solved from the face's texture
 * frame -- the camera-space p (origin), m (u end) and n (v end) corners -- and
 * kept in 18.14 fixed point. The perspective span fits a line through eight
 * pixels and so wants its x step eight times over; the linear span (terrain)
 * steps per pixel. That is the only difference between the two setups.
 */

#include "graphics/int_wrap.h"

#include <stdint.h>

/** Camera-space texture frame: [0] p, [1] m, [2] n. */
struct ToriDraw_RsZbufTextureFrame
{
    int x[3];
    int y[3];
    int z[3];
};

struct ToriDraw_RsZbufTexturePlane
{
    int base;
    int step_x;
    int step_y;
};

struct ToriDraw_RsZbufTexturePlanes
{
    struct ToriDraw_RsZbufTexturePlane u;
    struct ToriDraw_RsZbufTexturePlane v;
    struct ToriDraw_RsZbufTexturePlane w;
};

/** Java's `(int) ((long) (product) << shift / scale)`: the product wraps as an
 *  int first, the shift and divide are 64-bit, the result is narrowed. */
static inline int
rszbuf_plane_step(int product, int shift, int projection_scale)
{
    int64_t const widened = (int64_t)((uint64_t)(int64_t)product << shift);
    return (int)(uint32_t)(uint64_t)(widened / projection_scale);
}

static inline void
rszbuf_texture_planes_solve(
    struct ToriDraw_RsZbufTexturePlanes* out,
    const struct ToriDraw_RsZbufTextureFrame* frame,
    int projection_scale,
    int x_step_shift)
{
    int const px = frame->x[0];
    int const py = frame->y[0];
    int const pz = frame->z[0];
    /* p - m, and n - p: the frame's two axes, the first one reversed. */
    int const mx = toridraw_wrap_sub(px, frame->x[1]);
    int const my = toridraw_wrap_sub(py, frame->y[1]);
    int const mz = toridraw_wrap_sub(pz, frame->z[1]);
    int const nx = toridraw_wrap_sub(frame->x[2], px);
    int const ny = toridraw_wrap_sub(frame->y[2], py);
    int const nz = toridraw_wrap_sub(frame->z[2], pz);

#define RSZBUF_CROSS(a, b, c, d) toridraw_wrap_sub(toridraw_wrap_mul((a), (b)), toridraw_wrap_mul((c), (d)))
    out->u.base = toridraw_wrap_shl(RSZBUF_CROSS(py, nx, px, ny), 14);
    out->u.step_x = rszbuf_plane_step(RSZBUF_CROSS(pz, ny, py, nz), 14 + x_step_shift, projection_scale);
    out->u.step_y = rszbuf_plane_step(RSZBUF_CROSS(px, nz, pz, nx), 14, projection_scale);

    out->v.base = toridraw_wrap_shl(RSZBUF_CROSS(py, mx, px, my), 14);
    out->v.step_x = rszbuf_plane_step(RSZBUF_CROSS(pz, my, py, mz), 14 + x_step_shift, projection_scale);
    out->v.step_y = rszbuf_plane_step(RSZBUF_CROSS(px, mz, pz, mx), 14, projection_scale);

    out->w.base = toridraw_wrap_shl(RSZBUF_CROSS(my, nx, mx, ny), 14);
    out->w.step_x = rszbuf_plane_step(RSZBUF_CROSS(mz, ny, my, nz), 14 + x_step_shift, projection_scale);
    out->w.step_y = rszbuf_plane_step(RSZBUF_CROSS(mx, nz, mz, nx), 14, projection_scale);
#undef RSZBUF_CROSS
}

/** A plane's value at screen row `y` (relative to the centre), column 0. */
static inline int
rszbuf_plane_at_row(const struct ToriDraw_RsZbufTexturePlane* plane, int y)
{
    return toridraw_wrap_add(toridraw_wrap_mul(plane->step_y, y), plane->base);
}

#endif
