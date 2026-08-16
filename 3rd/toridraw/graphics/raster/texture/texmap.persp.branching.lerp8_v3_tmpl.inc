/*
 * Mapped textured triangle, `branching` walk. Included once per variant by the
 * `texpmn`, `texcylinder`, `texcube` and `texsphere` families. The includer
 * defines:
 *
 *   TMAP_FN    triangle entry point name
 *   TMAP_SPAN  scanline kernel from span/tex.span.gates.u.c
 *   TMAP_KIND  0 = P/M/N plane frame, 1 = cylinder, 2 = cube, 3 = sphere
 *   TMAP_ZBUF  1 = depth-tested variant (optional; absent means 0)
 *
 * The four families share this walker and differ only in TMAP_KIND, which
 * selects the projection and its seam fixup. They are separate kernels rather
 * than one kernel with a runtime mode because the projection is the thing that
 * distinguishes them: a per-pixel branch on a per-triangle property is waste,
 * and a name that says `texsphere` should not be able to draw a cube.
 *
 * Kind 0 takes a `struct ToriDraw_TexPlaneFrame` (the P/M/N model-space
 * positions) where the other three take a `struct ToriDraw_TexMapping`; see
 * TMAP_MAP_T. Why type 0 is drawn here and not by `texplane` is the note above
 * toridraw_texmap_project_plane in texmap_common.h.
 *
 * ## Why there is no ordered helper
 *
 * `texplane` splits into a six-way y-permutation dispatch plus an ordered
 * walker, because its uv basis is anchored to vertices the permutation must not
 * disturb. Here the attributes are *planes* through the three vertices, and a
 * plane is invariant under permutation — so the planes are solved once from the
 * unsorted triangle and only the screen positions are sorted. That is the same
 * observation the `scanline` family rests on.
 *
 * ## What the caller supplies
 *
 * Screen and camera-space positions from the POSED projection, and BIND-POSE
 * model-space positions for the uv solve — for the face's own three vertices
 * and, for kind 0, the P/M/N frame alike. The mapping is defined about a
 * bind-pose centre and the frame is a set of helper vertices that need not be
 * rigged with the faces that use it, so only the bind pose gives a uv that
 * stays put while an animation plays. See texmap_common.h.
 *
 * ## The depth variant
 *
 * A TMAP_ZBUF variant takes the three corners' depth KEYS (graphics/zdepth.h)
 * and a z-buffer, and changes nothing else. The keys do NOT ride the y sort
 * below, for the same reason the shade plane's coefficients do not: a plane is
 * invariant under permutation, so it is solved once from the unsorted triangle.
 *
 * The keys are supplied rather than derived from z0/z1/z2 here, even though
 * those are the camera depths the key is a function of, because the FUNCTION
 * differs by projection mode — a reciprocal under perspective, a negated scale
 * under a parallel camera — and the kernel has no way to know which one it is
 * drawing. The caller does.
 */

#include "graphics/int_wrap.h"

#ifndef TMAP_ZBUF
#define TMAP_ZBUF 0
#endif

/* What the "mapping" argument is: a P/M/N frame for kind 0, a face-group
 * mapping for the rest. Same slot in the signature either way, so the render
 * dispatch tables line up. */
#if TMAP_KIND == 0
#define TMAP_MAP_T struct ToriDraw_TexPlaneFrame
#else
#define TMAP_MAP_T struct ToriDraw_TexMapping
#endif

#if TMAP_ZBUF
#define TMAP_ZKEY_PARAMS , float key_a, float key_b, float key_c, torizdepth_t* RESTRICT zbuffer
#define TMAP_ZROW_ARGS , zbuffer, key_row, depth.step_dx
#define TMAP_ZADVANCE() (key_row += depth.step_dy)
#else
#define TMAP_ZKEY_PARAMS
#define TMAP_ZROW_ARGS
#define TMAP_ZADVANCE() ((void)0)
#endif

static inline void
TMAP_FN(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    /* Camera-space depth, for the perspective divide. */
    int z0,
    int z1,
    int z2,
    /* Model-space positions, for the projection. */
    int mx0,
    int my0,
    int mz0,
    int mx1,
    int my1,
    int mz1,
    int mx2,
    int my2,
    int mz2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    const TMAP_MAP_T* RESTRICT mapping,
    const struct ToriDraw_TexSampler* RESTRICT sampler TMAP_ZKEY_PARAMS)
{
    /* ---------------------------------------------------- 1. projection */

    float u[3];
    float v[3];

#if TMAP_KIND == 0
    /* Fixed per-vertex uv in the frame; nothing here depends on the camera.
     * No seam fixup: a plane projection has no seam. */
    toridraw_texmap_project_plane(
        mapping, mx0, my0, mz0, mx1, my1, mz1, mx2, my2, mz2, u, v);
#elif TMAP_KIND == 1
    toridraw_texmap_project_cylinder(mapping, mx0, my0, mz0, &u[0], &v[0]);
    toridraw_texmap_project_cylinder(mapping, mx1, my1, mz1, &u[1], &v[1]);
    toridraw_texmap_project_cylinder(mapping, mx2, my2, mz2, &u[2], &v[2]);
    toridraw_texmap_unwrap_seam(
        mapping->direction, mapping->scale_z > 0.0f ? mapping->scale_z : 1.0f, u, v);

    /*
     * Seam fixup. A vertex that wrapped the cylinder the long way round is
     * pulled back so the triangle spans the short arc; without it a face
     * straddling the seam smears the whole texture across itself. The axis the
     * fixup applies to follows the scroll direction, because direction 1 and 3
     * swap u and v.
     */
    {
        float half = mapping->scale_z / 2.0f;
        float* axis = (mapping->direction & 1) ? v : u;
        if( axis[1] - axis[0] > half )
            axis[1] -= mapping->scale_z;
        else if( axis[0] - axis[1] > half )
            axis[1] += mapping->scale_z;
        if( axis[2] - axis[0] > half )
            axis[2] -= mapping->scale_z;
        else if( axis[0] - axis[2] > half )
            axis[2] += mapping->scale_z;
    }
#elif TMAP_KIND == 2
    {
        /*
         * Cube face selection, from the triangle's own normal in mapping space.
         * It is per triangle, not per vertex, which is what keeps a cube
         * projection affine over a face and is why this family needs no seam
         * fixup: a triangle lands wholly on one face of the cube.
         */
        float d1x = (float)(mx1 - mx0);
        float d1y = (float)(my1 - my0);
        float d1z = (float)(mz1 - mz0);
        float d2x = (float)(mx2 - mx0);
        float d2y = (float)(my2 - my0);
        float d2z = (float)(mz2 - mz0);

        float nx = d1y * d2z - d2y * d1z;
        float ny = d1z * d2x - d2z * d1x;
        float nz = d1x * d2y - d2x * d1y;

        float ax = (nx * mapping->matrix[0] + ny * mapping->matrix[1] +
                    nz * mapping->matrix[2]) *
                   mapping->axis_scale_x;
        float ay = (nx * mapping->matrix[3] + ny * mapping->matrix[4] +
                    nz * mapping->matrix[5]) *
                   mapping->axis_scale_y;
        float az = (nx * mapping->matrix[6] + ny * mapping->matrix[7] +
                    nz * mapping->matrix[8]) *
                   mapping->axis_scale_z;

        int axis = toridraw_texmap_cube_axis(ax, ay, az);

        toridraw_texmap_project_cube(mapping, axis, mx0, my0, mz0, &u[0], &v[0]);
        toridraw_texmap_project_cube(mapping, axis, mx1, my1, mz1, &u[1], &v[1]);
        toridraw_texmap_project_cube(mapping, axis, mx2, my2, mz2, &u[2], &v[2]);
    }
#else
    toridraw_texmap_project_sphere(mapping, mx0, my0, mz0, &u[0], &v[0]);
    toridraw_texmap_project_sphere(mapping, mx1, my1, mz1, &u[1], &v[1]);
    toridraw_texmap_project_sphere(mapping, mx2, my2, mz2, &u[2], &v[2]);
    /* The sphere's longitude wraps once over the full turn. */
    toridraw_texmap_unwrap_seam(mapping->direction, 1.0f, u, v);

    /*
     * The sphere's seam fixup folds against a whole turn rather than against a
     * scale. Note the second test is `> 0` where its three siblings use `> 0.5`
     * — that asymmetry is the reference's, it decides which side of the seam a
     * vertex lands on, and "fixing" it would silently move geometry the
     * reference does not move.
     */
    {
        float* axis = (mapping->direction & 1) ? v : u;
        if( axis[1] - axis[0] > 0.5f )
            axis[1] -= 1.0f;
        else if( axis[0] - axis[1] > 0.0f )
            axis[1] += 1.0f;
        if( axis[2] - axis[0] > 0.5f )
            axis[2] -= 1.0f;
        else if( axis[0] - axis[2] > 0.5f )
            axis[2] += 1.0f;
    }
#endif

    /* ------------------------------------------------------- 2. y sort */

    int sx[3] = { x0, x1, x2 };
    int sy[3] = { y0, y1, y2 };
    int sh[3] = { shade7bit_a, shade7bit_b, shade7bit_c };

    /* Three compare-swaps sort three elements. The attribute planes below are
     * solved from the UNSORTED triangle, so nothing else has to move. */
#define TMAP_SWAP(i, j)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if( sy[j] < sy[i] )                                                                        \
        {                                                                                          \
            int t = sy[i];                                                                         \
            sy[i] = sy[j];                                                                         \
            sy[j] = t;                                                                             \
            t = sx[i];                                                                             \
            sx[i] = sx[j];                                                                         \
            sx[j] = t;                                                                             \
            t = sh[i];                                                                             \
            sh[i] = sh[j];                                                                         \
            sh[j] = t;                                                                             \
        }                                                                                          \
    } while( 0 )

    TMAP_SWAP(0, 1);
    TMAP_SWAP(1, 2);
    TMAP_SWAP(0, 1);
#undef TMAP_SWAP

    if( sy[2] < 0 || sy[0] > screen_height )
        return;
    if( sy[2] - sy[0] == 0 )
        return;

    /* ------------------------------------------------- 3. shade + edges */

    int dy_AC = sy[2] - sy[0];
    int dy_AB = sy[1] - sy[0];
    int dx_AC = sx[2] - sx[0];
    int dx_AB = sx[1] - sx[0];

    int sarea = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea == 0 )
        return;

#if TMAP_ZBUF
    /* From the UNSORTED corners, like every other plane here. */
    struct ToriDraw_ZbufPlane depth;
    if( !toridraw_zbuf_plane_solve(
            &depth, x0, y0, key_a, x1, y1, key_b, x2, y2, key_c) )
        return;
#endif

    int dblend_ab = sh[1] - sh[0];
    int dblend_ac = sh[2] - sh[0];

    int shade8bit_yhat_ish8 = ((dx_AC * dblend_ab - dx_AB * dblend_ac) << 9) / sarea;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend_ac - dy_AC * dblend_ab) << 9) / sarea;
    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    int shade8bit_edge_ish8 = toridraw_wrap_add(
        toridraw_wrap_sub(sh[0] << 9, toridraw_wrap_mul(shade8bit_xhat_ish8, sx[0])),
        shade8bit_xhat_ish8);

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;
    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( sy[2] != sy[1] )
        step_edge_x_BC_ish16 = ((sx[2] - sx[1]) << 16) / (sy[2] - sy[1]);

    int edge_x_AC_ish16 = sx[0] << 16;
    int edge_x_AB_ish16 = sx[0] << 16;
    int edge_x_BC_ish16 = sx[1] << 16;

    int ry0 = sy[0];
    int ry1 = sy[1];
    int ry2 = sy[2];

    if( ry0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * ry0;
        /* Only pre-step A->B if that edge is walked; a near-horizontal AB times
         * a distant y overflows the 16.16 product for a discarded result. Full
         * argument in graphics/raster/zbuffer/zbuf.screen.u.c. */
        if( ry1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * ry0;
        shade8bit_edge_ish8 = toridraw_wrap_sub(
            shade8bit_edge_ish8, toridraw_wrap_mul(shade8bit_yhat_ish8, ry0));
        ry0 = 0;
    }
    if( ry1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * ry1;
        ry1 = 0;
    }
    if( ry1 > screen_height )
    {
        ry1 = screen_height;
        ry2 = screen_height;
    }
    else if( ry2 > screen_height )
    {
        ry2 = screen_height;
    }

    /* ------------------------------------------------------ 4. uv planes */

    struct ToriDraw_TexUvPlanes uv;
    int zs[3] = { z0, z1, z2 };
    if( !toridraw_texmap_uv_planes(
            &uv,
            x0, x1, x2,
            y0, y1, y2,
            u, v, zs,
            screen_width, screen_height,
            sampler->width, sampler->shift,
            ry0) )
        return;

    int au = uv.au;
    int bv = uv.bv;
    int cw = uv.cw;
    int offset = ry0 * stride;

#if TMAP_ZBUF
    float key_row = toridraw_zbuf_plane_row(&depth, ry0);
#endif

    /* ---------------------------------------------------------- 5. walk */

#define TMAP_ROW(left, right)                                                                      \
    TMAP_SPAN(                                                                                     \
        pixel_buffer,                                                                              \
        screen_width,                                                                              \
        (left),                                                                                    \
        (right),                                                                                   \
        offset,                                                                                    \
        au,                                                                                        \
        bv,                                                                                        \
        cw,                                                                                        \
        uv.step_au_dx,                                                                             \
        uv.step_bv_dx,                                                                             \
        uv.step_cw_dx,                                                                             \
        shade8bit_edge_ish8,                                                                       \
        shade8bit_xhat_ish8,                                                                       \
        sampler TMAP_ZROW_ARGS)

#define TMAP_ADVANCE()                                                                             \
    do                                                                                             \
    {                                                                                              \
        au += uv.step_au_dy;                                                                       \
        bv += uv.step_bv_dy;                                                                       \
        cw += uv.step_cw_dy;                                                                       \
        shade8bit_edge_ish8 = toridraw_wrap_add(shade8bit_edge_ish8, shade8bit_yhat_ish8);        \
        offset += stride;                                                                          \
        TMAP_ZADVANCE();                                                                           \
    } while( 0 )

    int seg1 = ry1 - ry0;
    int seg2 = ry2 - ry1;
    if( seg1 < 0 )
        seg1 = 0;
    if( seg2 < 0 )
        seg2 = 0;

    /* Which edge is on the left is decided once, from the signed area, rather
     * than per row — the sorted triangle cannot change winding mid-walk. */
    if( (ry0 == ry1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (ry0 != ry1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        while( seg1-- > 0 )
        {
            TMAP_ROW(edge_x_AB_ish16, edge_x_AC_ish16);
            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            TMAP_ADVANCE();
        }
        while( seg2-- > 0 )
        {
            TMAP_ROW(edge_x_BC_ish16, edge_x_AC_ish16);
            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            TMAP_ADVANCE();
        }
    }
    else
    {
        while( seg1-- > 0 )
        {
            TMAP_ROW(edge_x_AC_ish16, edge_x_AB_ish16);
            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            TMAP_ADVANCE();
        }
        while( seg2-- > 0 )
        {
            TMAP_ROW(edge_x_AC_ish16, edge_x_BC_ish16);
            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            TMAP_ADVANCE();
        }
    }

#undef TMAP_ROW
#undef TMAP_ADVANCE
}

#undef TMAP_ZKEY_PARAMS
#undef TMAP_ZROW_ARGS
#undef TMAP_ZADVANCE
#undef TMAP_MAP_T

#undef TMAP_FN
#undef TMAP_SPAN
#undef TMAP_KIND
#undef TMAP_ZBUF
