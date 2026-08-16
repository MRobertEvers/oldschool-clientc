#ifndef TEXMAP_COMMON_H
#define TEXMAP_COMMON_H

#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>

/*
 * Shared setup for the mapped texture families — `texcylinder`, `texcube` and
 * `texsphere`.
 *
 * ## What separates them from `texplane`
 *
 * A `texplane` face names three vertices whose *positions* are the texture
 * plane, so the rasterizer can walk that plane directly and never needs a uv at
 * all. A mapped face names no such thing: it carries a projection about the face
 * group's centre, and its uv has to be computed. Cylinder and sphere are
 * non-linear in the vertex (arctangent, arcsine), so there is no plane to walk
 * even in principle.
 *
 * ## Why the projection happens in the kernel, and from WHICH positions
 *
 * It could be a pre-pass writing a uv array, and for a model drawn many times
 * that would be cheaper. Doing it in triangle setup touches exactly the three
 * vertices being drawn and keeps each family self-contained — the projection
 * and its seam fixup are the thing that makes `texsphere` a different kernel
 * from `texcube` rather than a different name for it.
 *
 * The positions it takes are the BIND POSE, not the animated vertices. The
 * reference solves a face's uv once from the unanimated mesh and keeps it while
 * animation moves the vertices — that is what makes a texture deform with its
 * face. Solving from posed vertices against a fixed mapping centre (or a P/M/N
 * frame that is not rigged with the faces that use it) recomputes a different uv
 * every frame, and the texture slides across the face as the animation plays.
 * The caller (ToriDraw_RenderHD) reads `original_vertices_*` for these
 * arguments; a model that never captured originals is at its bind pose.
 *
 * ## Angles
 *
 * Through the arctangent table in shared_tables.h, never libm, so the result is
 * identical on every platform. See the note there.
 */

/** The mapping a face group carries. Model space throughout. */
struct ToriDraw_TexMapping
{
    /** Midpoint of the bounding box of every vertex in the face group. */
    int centre_x;
    int centre_y;
    int centre_z;
    /** Model space -> mapping space; rotation about the stored axis, then the
     *  stored rotation, then the per-axis scales. Already scaled. */
    float matrix[9];
    /** 0-3 scroll direction, applied after projection. */
    int direction;
    /** Translation along the projection's second axis, in tiles. */
    float speed;
    /** Cube only, in tiles. */
    float u_offset;
    float v_offset;
    /** Cylinder only: the u wrap width, which the seam fixup folds against. */
    float scale_z;
    /*
     * Cube only. The face-selection test divides the triangle normal by the
     * per-axis scales *again*, on top of the already-scaled matrix. That double
     * application is the reference's, and which cube face a triangle lands on
     * depends on it, so it is carried rather than folded away. Each entry is the
     * raw stored scale over 64.
     */
    float axis_scale_x;
    float axis_scale_y;
    float axis_scale_z;
};

/** The scroll direction, shared by all three projections. */
static inline void
toridraw_texmap_direction(int direction, float* u, float* v)
{
    if( direction == 1 )
    {
        float t = *u;
        *u = -*v;
        *v = t;
    }
    else if( direction == 2 )
    {
        *u = -*u;
        *v = -*v;
    }
    else if( direction == 3 )
    {
        float t = *u;
        *u = *v;
        *v = -t;
    }
}

/**
 * Unwrap a triangle's uv across the projection's seam.
 *
 * `atan2` has a branch cut, so a triangle straddling it gets values like 0.98,
 * 0.99, 0.02. Nothing about those is wrong per vertex, but the rasteriser
 * interpolates between them the LONG way round and smears the entire texture
 * across the face. On a rock surface that reads as streaking, and only on the
 * handful of faces that happen to cross the seam — 53 of the rs643 TzTok-Jad's
 * 380 cylinder faces, scattered over the model.
 *
 * `wrap` is the width the coordinate repeats over: the cylinder's u multiplier
 * for type 1, and 1.0 for the sphere. Direction 1 and 3 swap u and v, so the
 * wrapped axis moves with them.
 *
 * Shared with ToriDraw_ComputeTextureUv rather than duplicated: the generator
 * has always done this, the kernels never did, and one copy is what stops them
 * disagreeing again.
 */
static inline void
toridraw_texmap_unwrap_seam(int direction, float wrap, float* u, float* v)
{
    float* axis = (direction & 1) ? v : u;
    float half = wrap / 2.0f;

    if( wrap <= 0.0f )
        return;

    if( axis[1] - axis[0] > half )
        axis[1] -= wrap;
    else if( axis[0] - axis[1] > half )
        axis[1] += wrap;

    if( axis[2] - axis[0] > half )
        axis[2] -= wrap;
    else if( axis[0] - axis[2] > half )
        axis[2] += wrap;
}

/** Centre and rotate a model-space vertex into mapping space. */
static inline void
toridraw_texmap_to_mapping_space(
    const struct ToriDraw_TexMapping* m,
    int vx,
    int vy,
    int vz,
    float* a,
    float* b,
    float* c)
{
    float dx = (float)(vx - m->centre_x);
    float dy = (float)(vy - m->centre_y);
    float dz = (float)(vz - m->centre_z);

    *a = dx * m->matrix[0] + dy * m->matrix[1] + dz * m->matrix[2];
    *b = dx * m->matrix[3] + dy * m->matrix[4] + dz * m->matrix[5];
    *c = dx * m->matrix[6] + dy * m->matrix[7] + dz * m->matrix[8];
}

/** Type 1 — cylindrical: angle about the mapping axis, height along it. */
static inline void
toridraw_texmap_project_cylinder(
    const struct ToriDraw_TexMapping* m,
    int vx,
    int vy,
    int vz,
    float* out_u,
    float* out_v)
{
    float a, b, c;
    toridraw_texmap_to_mapping_space(m, vx, vy, vz, &a, &b, &c);

    /* (turns16 + 32768) / 65536 is exactly atan2/2pi + 0.5, with no pi. */
    float u = (float)(ToriDraw_Atan2Turns16(a, c) + 32768) * (1.0f / 65536.0f);
    if( m->scale_z != 1.0f )
        u *= m->scale_z;
    float v = b + 0.5f + m->speed;

    toridraw_texmap_direction(m->direction, &u, &v);
    *out_u = u;
    *out_v = v;
}

/**
 * Which cube face a triangle projects onto, from its scaled normal. The
 * dominant axis wins; ties fall through to x, matching the reference's strict
 * `>` comparisons.
 */
static inline int
toridraw_texmap_cube_axis(float x, float y, float z)
{
    float ax = x < 0.0f ? -x : x;
    float ay = y < 0.0f ? -y : y;
    float az = z < 0.0f ? -z : z;

    if( ay > ax && ay > az )
        return y > 0.0f ? 0 : 1;
    if( az > ax && az > ay )
        return z > 0.0f ? 2 : 3;
    return x > 0.0f ? 4 : 5;
}

/** Type 2 — cube: pick a face from the triangle normal, then project flat. */
static inline void
toridraw_texmap_project_cube(
    const struct ToriDraw_TexMapping* m,
    int axis,
    int vx,
    int vy,
    int vz,
    float* out_u,
    float* out_v)
{
    float a, b, c;
    toridraw_texmap_to_mapping_space(m, vx, vy, vz, &a, &b, &c);

    float u;
    float v;
    if( axis == 0 )
    {
        u = a + m->speed + 0.5f;
        v = -c + m->v_offset + 0.5f;
    }
    else if( axis == 1 )
    {
        u = a + m->speed + 0.5f;
        v = c + m->v_offset + 0.5f;
    }
    else if( axis == 2 )
    {
        u = -a + m->speed + 0.5f;
        v = -b + m->u_offset + 0.5f;
    }
    else if( axis == 3 )
    {
        u = a + m->speed + 0.5f;
        v = -b + m->u_offset + 0.5f;
    }
    else if( axis == 4 )
    {
        u = c + m->v_offset + 0.5f;
        v = -b + m->u_offset + 0.5f;
    }
    else
    {
        u = -c + m->v_offset + 0.5f;
        v = -b + m->u_offset + 0.5f;
    }

    toridraw_texmap_direction(m->direction, &u, &v);
    *out_u = u;
    *out_v = v;
}

/** Type 3 — spherical: longitude and latitude about the mapping centre. */
static inline void
toridraw_texmap_project_sphere(
    const struct ToriDraw_TexMapping* m,
    int vx,
    int vy,
    int vz,
    float* out_u,
    float* out_v)
{
    float a, b, c;
    toridraw_texmap_to_mapping_space(m, vx, vy, vz, &a, &b, &c);

    float u = (float)(ToriDraw_Atan2Turns16(a, c) + 32768) * (1.0f / 65536.0f);
    /*
     * asin(b / |v|) is exactly atan2(b, hypot(a, c)). Taking the second form
     * skips the division and the full-length square root, and removes the 0/0
     * the first produces for a vertex sitting exactly at the mapping centre —
     * the reference gets NaN there, which poisons a whole triangle rather than
     * one texel. atan2(0, 0) is 0, the equator, which is the sensible answer for
     * a point with no direction.
     *
     * asin/pi is twice asin/2pi, hence the doubled turns.
     */
    float v = (float)(2 * ToriDraw_Atan2Turns16(b, sqrtf(a * a + c * c)) + 32768) *
                  (1.0f / 65536.0f) +
              m->speed;

    toridraw_texmap_direction(m->direction, &u, &v);
    *out_u = u;
    *out_v = v;
}

/*
 * Type 0 — the P/M/N plane projector, and why it lives in THIS family.
 *
 * A type-0 face names three vertices: uv is (0,0) at P, (1,0) at M and (0,1)
 * at N. The stock `texplane` kernel — the SD reference's — walks that plane in
 * camera space and intersects each pixel's EYE RAY with it. That is exact when
 * the face lies in the P/M/N plane, which OSRS content always arranges: P, M, N
 * are the face's own vertices, or coplanar with them.
 *
 * HD (OB3) content does not arrange it. There P/M/N is a shared texture FRAME
 * that many faces reference — that is how one small tile repeats across a
 * whole body part — and it is routinely off the face's plane by several edge
 * lengths. RS727 TzTok-Jad: 2320 of 2320 type-0 faces use a frame that is not
 * their own vertices, 1589 of them more than one edge length off-plane, the
 * worst 97 edge lengths. Intersecting eye rays with a plane that is not the
 * face's turns the texture into a slide projector sitting at the camera: as the
 * view turns, the projection slides across the face while the face itself stays
 * put. It reads as parallax — a texture that "swims" — and it is loudest on
 * densely tiled faces, because that is where the frame is smallest and furthest
 * from the face. The tell in the plane walker is `w` changing SIGN inside one
 * face: the frame's plane passing through the eye.
 *
 * The HD reference never walks the plane. It projects each face vertex onto the
 * P/M/N plane along that plane's NORMAL — a fixed, view-independent uv per
 * vertex — and interpolates it perspective-correctly, exactly as the mapped
 * families do for their own projections. So type 0 belongs here, with the frame
 * as its "mapping". Where the face IS coplanar with P/M/N the two agree, so
 * nothing is lost for content that kept the SD invariant.
 *
 * The solve is the reference's, kept operation-for-operation, and it is the ONE
 * copy: ToriDraw_ComputeTextureUv's generator calls this too, so the kernel and
 * the direct generator cannot drift apart. u is the component of (A - P) along
 * U within the plane, read off with V x n where n = U x V — dotting with a
 * vector normal to both V and n discards the off-plane component and the V
 * component in one step. Same for v with n x U.
 */
struct ToriDraw_TexPlaneFrame
{
    int px;
    int py;
    int pz;
    int mx;
    int my;
    int mz;
    int nx;
    int ny;
    int nz;
};

static inline void
toridraw_texmap_project_plane(
    const struct ToriDraw_TexPlaneFrame* f,
    int vx0,
    int vy0,
    int vz0,
    int vx1,
    int vy1,
    int vz1,
    int vx2,
    int vy2,
    int vz2,
    float* u,
    float* v)
{
    float ox = (float)f->px;
    float oy = (float)f->py;
    float oz = (float)f->pz;

    float mu_x = (float)f->mx - ox;
    float mu_y = (float)f->my - oy;
    float mu_z = (float)f->mz - oz;
    float nv_x = (float)f->nx - ox;
    float nv_y = (float)f->ny - oy;
    float nv_z = (float)f->nz - oz;

    float d0x = (float)vx0 - ox;
    float d0y = (float)vy0 - oy;
    float d0z = (float)vz0 - oz;
    float d1x = (float)vx1 - ox;
    float d1y = (float)vy1 - oy;
    float d1z = (float)vz1 - oz;
    float d2x = (float)vx2 - ox;
    float d2y = (float)vy2 - oy;
    float d2z = (float)vz2 - oz;

    float nx = mu_y * nv_z - mu_z * nv_y;
    float ny = mu_z * nv_x - mu_x * nv_z;
    float nz = mu_x * nv_y - mu_y * nv_x;

    float ax = nv_y * nz - nv_z * ny;
    float ay = nv_z * nx - nv_x * nz;
    float az = nv_x * ny - nv_y * nx;
    float det_u = ax * mu_x + ay * mu_y + az * mu_z;
    /*
     * A degenerate frame — p, m and n collinear, or two of them the same vertex
     * — makes this determinant zero. The reference divides anyway and gets
     * Infinity, which JS carries harmlessly into a Float32Array; here it would
     * reach a rasterizer, and a non-finite uv poisons an entire triangle rather
     * than one texel. Measured at 467 of 6,395,265 type-0 faces across
     * cache.rs727_preeoc, so it is rare and real. Leaving uv at zero collapses
     * the face to one texel, which is what a frame with no extent means.
     */
    if( det_u == 0.0f )
    {
        u[0] = u[1] = u[2] = 0.0f;
        v[0] = v[1] = v[2] = 0.0f;
        return;
    }
    float inv = 1.0f / det_u;

    u[0] = (ax * d0x + ay * d0y + az * d0z) * inv;
    u[1] = (ax * d1x + ay * d1y + az * d1z) * inv;
    u[2] = (ax * d2x + ay * d2y + az * d2z) * inv;

    ax = mu_y * nz - mu_z * ny;
    ay = mu_z * nx - mu_x * nz;
    az = mu_x * ny - mu_y * nx;
    float det_v = ax * nv_x + ay * nv_y + az * nv_z;
    if( det_v == 0.0f )
    {
        u[0] = u[1] = u[2] = 0.0f;
        v[0] = v[1] = v[2] = 0.0f;
        return;
    }
    inv = 1.0f / det_v;

    v[0] = (ax * d0x + ay * d0y + az * d0z) * inv;
    v[1] = (ax * d1x + ay * d1y + az * d1z) * inv;
    v[2] = (ax * d2x + ay * d2y + az * d2z) * inv;

    /* The reference's snap-to-1 pass. A frame whose u span lands within a hair
     * of exactly one tile is snapped so adjacent faces share an edge exactly;
     * the asymmetric ordering is the reference's. */
    if( u[1] - u[0] > 0.99f && u[1] - u[0] < 1.1f )
        u[1] = 1.0f;
    if( u[2] - u[1] > 0.99f && u[2] - u[1] < 1.1f )
        u[2] = 1.0f;
    if( u[0] - u[2] > 0.99f && u[0] - u[2] < 1.1f )
        u[0] = 1.0f;
    if( u[0] - u[1] > 0.99f && u[0] - u[1] < 1.1f )
        u[0] = 1.0f;
    if( u[1] - u[2] > 0.99f && u[1] - u[2] < 1.1f )
        u[1] = 1.0f;
    if( u[2] - u[0] > 0.99f && u[2] - u[0] < 1.1f )
        u[2] = 1.0f;
}

/*
 * The perspective uv planes.
 *
 * The span kernels consume `au`, `bv`, `cw` stepped linearly in screen space and
 * compute `u = au / (cw >> shift)` per eight pixels. That IS perspective-correct
 * interpolation — u/w over 1/w — so the mapped families need no new span: they
 * only need those three accumulators built from explicit uv instead of from a
 * plane's normals.
 *
 * u/z, v/z and 1/z are linear in screen space (that is what makes perspective
 * correction work), so each is a plane through the three vertices, solved by
 * Cramer below.
 *
 * ## The normalisation, which is the whole difficulty
 *
 * ToriDraw_TexturePlanePrepare32 cannot be reused: its `base` comes from
 * projecting a 3D normal's z, not from an arbitrary screen-space plane. So the
 * scale is chosen here, and it is chosen from the values the span will actually
 * see — the plane is linear, so its extremes over the drawn region are at the
 * corners of the triangle's clipped screen bounding box. Taking the max over
 * those corners and scaling it to a fixed headroom bounds every intermediate the
 * span can form, including the eight-pixel lookahead it does past a short tail.
 */
struct ToriDraw_TexUvPlanes
{
    int au;
    int bv;
    int cw;
    int step_au_dx;
    int step_bv_dx;
    int step_cw_dx;
    int step_au_dy;
    int step_bv_dy;
    int step_cw_dy;
};

/** Headroom for the span's own arithmetic on top of the extremes we bound. */
#define TORIDRAW_TEXUV_HEADROOM_BITS 29

/**
 * Build the planes, evaluated at (screen_width/2, y_top) — the origin the span
 * rebases from with `x0 - (screen_width >> 1)`.
 *
 * `z` is camera-space depth and must be positive; a caller that has not near
 * clipped will be rejected rather than divided by zero. Returns false when the
 * triangle is degenerate or no safe scale exists, in which case nothing is drawn.
 */
static inline bool
toridraw_texmap_uv_planes(
    struct ToriDraw_TexUvPlanes* out,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    const float* u,
    const float* v,
    const int* z,
    int screen_width,
    int screen_height,
    int texture_width,
    int texture_shift,
    int y_top)
{
    float area = (float)(x1 - x0) * (float)(y2 - y0) - (float)(x2 - x0) * (float)(y1 - y0);
    if( area == 0.0f )
        return false;
    if( z[0] <= 0 || z[1] <= 0 || z[2] <= 0 )
        return false;

    /* Per-vertex u/z, v/z, 1/z. u is scaled into texels here, once, so the span
     * needs no knowledge of the texture size beyond its shift. */
    float w[3];
    float uu[3];
    float vv[3];
    for( int i = 0; i < 3; i++ )
    {
        w[i] = 1.0f / (float)z[i];
        uu[i] = u[i] * (float)texture_width * w[i];
        vv[i] = v[i] * (float)texture_width * w[i];
    }

    float inv_area = 1.0f / area;
    float fx = (float)x0;
    float fy = (float)y0;

#define TEXUV_PLANE(f, gx, gy)                                                                     \
    do                                                                                             \
    {                                                                                              \
        (gx) = (((f)[1] - (f)[0]) * (float)(y2 - y0) - ((f)[2] - (f)[0]) * (float)(y1 - y0)) *     \
               inv_area;                                                                           \
        (gy) = (((f)[2] - (f)[0]) * (float)(x1 - x0) - ((f)[1] - (f)[0]) * (float)(x2 - x0)) *     \
               inv_area;                                                                           \
    } while( 0 )

    float gu_x, gu_y, gv_x, gv_y, gw_x, gw_y;
    TEXUV_PLANE(uu, gu_x, gu_y);
    TEXUV_PLANE(vv, gv_x, gv_y);
    TEXUV_PLANE(w, gw_x, gw_y);
#undef TEXUV_PLANE

    /* Values at the span's origin. */
    float ox = (float)(screen_width >> 1);
    float oy = (float)y_top;
    float base_u = uu[0] + gu_x * (ox - fx) + gu_y * (oy - fy);
    float base_v = vv[0] + gv_x * (ox - fx) + gv_y * (oy - fy);
    float base_w = w[0] + gw_x * (ox - fx) + gw_y * (oy - fy);

    /*
     * Bound every value the span can form. The drawn region is inside the
     * triangle's screen bbox clipped to the viewport, plus the eight-pixel
     * lookahead; evaluating each plane at that box's four corners bounds it,
     * because a plane's extremes over a box are at its corners.
     */
    int bx0 = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int bx1 = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int by0 = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int by1 = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if( bx0 < 0 )
        bx0 = 0;
    if( by0 < 0 )
        by0 = 0;
    if( bx1 > screen_width )
        bx1 = screen_width;
    if( by1 > screen_height )
        by1 = screen_height;
    bx1 += 8;

    float worst = 1.0f;
    for( int cx = 0; cx < 2; cx++ )
    {
        for( int cy = 0; cy < 2; cy++ )
        {
            float px = (float)(cx ? bx1 : bx0) - ox;
            float py = (float)(cy ? by1 : by0) - oy;

            float su = base_u + gu_x * px + gu_y * py;
            float sv = base_v + gv_x * px + gv_y * py;
            float sw = base_w + gw_x * px + gw_y * py;

            su = su < 0.0f ? -su : su;
            sv = sv < 0.0f ? -sv : sv;
            sw = sw < 0.0f ? -sw : sw;
            /* cw carries an extra `<< shift` on top of w. */
            sw = sw * (float)(1 << texture_shift);

            if( su > worst )
                worst = su;
            if( sv > worst )
                worst = sv;
            if( sw > worst )
                worst = sw;
        }
    }

    float scale = (float)(1 << TORIDRAW_TEXUV_HEADROOM_BITS) / worst;
    if( !(scale > 0.0f) )
        return false;

    float shifted = scale * (float)(1 << texture_shift);

    out->au = (int)(base_u * scale);
    out->bv = (int)(base_v * scale);
    out->cw = (int)(base_w * shifted);
    out->step_au_dx = (int)(gu_x * scale);
    out->step_bv_dx = (int)(gv_x * scale);
    out->step_cw_dx = (int)(gw_x * shifted);
    out->step_au_dy = (int)(gu_y * scale);
    out->step_bv_dy = (int)(gv_y * scale);
    out->step_cw_dy = (int)(gw_y * shifted);

    /*
     * `w = cw >> shift` must stay non-zero, or the span skips the block. Scaling
     * is common to all three planes, so a face whose uv spans a huge number of
     * tiles pushes au up and w down until w rounds away. That is a real,
     * measured population (the cache has faces with uv spans in the thousands),
     * and drawing them at reduced precision beats dropping them, so this only
     * rejects the case where w has collapsed entirely.
     */
    if( out->cw == 0 && out->step_cw_dx == 0 && out->step_cw_dy == 0 )
        return false;

    return true;
}

#endif
