/*
 * Textured triangle template for the `scanline` family.
 *
 * Included once per variant. The includer defines:
 *
 *   SLT_FN            triangle entry point name
 *   SLT_SPAN_FN       span kernel name (must be distinct per variant)
 *   SLT_PERSP         1 = perspective (one divide pair per 8-pixel block)
 *                     0 = affine      (one divide pair per span)
 *   SLT_TRANS         1 = texel 0 is transparent
 *   SLT_SHADE_INTERP  1 = per-vertex shade plane, 0 = constant face shade
 *   SLT_FACE_ALPHA    1 = blend the shaded texel into the destination
 *
 * Every instantiation takes the same argument list so callers can dispatch
 * without reshaping arguments; parameters a variant does not use are voided.
 *
 * All instantiations share the front-end contract in scanline_common.h: one
 * y-sort, y-clipping folded into the edge accumulators, and the horizontal
 * clip decided once per trapezoid segment. Per-triangle hoisting that the
 * `branching` kernels repeat per scanline - texture shift, v mask, the
 * screen-centre uv bias - happens once here.
 */

/* ------------------------------------------------------------ per-pixel */

#if SLT_FACE_ALPHA
#define SLT_STORE(dst_ref, texel, shade)                                                           \
    do                                                                                             \
    {                                                                                              \
        (dst_ref) = alpha_blend(face_alpha, (dst_ref), shade_blend((texel), (shade)));             \
    } while( 0 )
#else
#define SLT_STORE(dst_ref, texel, shade)                                                           \
    do                                                                                             \
    {                                                                                              \
        (dst_ref) = shade_blend((texel), (shade));                                                 \
    } while( 0 )
#endif

#if SLT_TRANS
#define SLT_EMIT(dst_ref, texel, shade)                                                            \
    do                                                                                             \
    {                                                                                              \
        int slt_texel_ = (texel);                                                                  \
        if( slt_texel_ != 0 )                                                                      \
            SLT_STORE(dst_ref, slt_texel_, shade);                                                 \
    } while( 0 )
#else
#define SLT_EMIT(dst_ref, texel, shade) SLT_STORE(dst_ref, texel, shade)
#endif

/*
 * Eight pixels. Without face alpha this is the existing per-ISA SIMD kernel
 * (NEON / AVX2 / SSE4.1 / SSE2 / scalar) from raster/texture/span/tex.span.u.c;
 * the face-alpha variants need a read-modify-write and stay scalar.
 */
#if SLT_FACE_ALPHA
#define SLT_BLOCK8(pb, off, tex, u_scan, v_scan, su, sv, shift, vmask, shade)                      \
    do                                                                                             \
    {                                                                                              \
        int slt_u_ = (u_scan);                                                                     \
        int slt_v_ = (v_scan);                                                                     \
        int slt_off_ = (off);                                                                      \
        for( int slt_i_ = 0; slt_i_ < 8; slt_i_++ )                                                \
        {                                                                                          \
            SLT_EMIT((pb)[slt_off_], (tex)[(slt_u_ >> (shift)) + (slt_v_ & (vmask))], (shade));    \
            slt_u_ += (su);                                                                        \
            slt_v_ += (sv);                                                                        \
            slt_off_ += 1;                                                                         \
        }                                                                                          \
    } while( 0 )
#elif SLT_TRANS
#define SLT_BLOCK8(pb, off, tex, u_scan, v_scan, su, sv, shift, vmask, shade)                      \
    raster_linear_transparent_blend_lerp8(                                                         \
        (uint32_t*)(pb), (off), (const uint32_t*)(tex), (u_scan), (v_scan), (su), (sv), (shift),   \
        (shade))
#else
#define SLT_BLOCK8(pb, off, tex, u_scan, v_scan, su, sv, shift, vmask, shade)                      \
    raster_linear_opaque_blend_lerp8(                                                              \
        (uint32_t*)(pb), (off), (const uint32_t*)(tex), (u_scan), (v_scan), (su), (sv), (shift),   \
        (shade))
#endif

/* ----------------------------------------------------------------- span */

/**
 * One already-clipped pixel run.
 *
 * `au` / `bv` / `cw` and `shade8bit_ish8` arrive rebased to the run's first
 * pixel, so no screen-centre correction happens here.
 */
static void
SLT_SPAN_FN(
    int* RESTRICT pixel_buffer,
    int offset,
    int count,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    const int* RESTRICT texels,
    int texture_shift,
    int u_mask,
    int v_mask,
    int texture_width,
    int face_alpha)
{
#if !SLT_FACE_ALPHA
    (void)face_alpha;
#endif
#if !SLT_SHADE_INTERP
    (void)step_shade8bit_dx_ish8;
#endif

    int blocks = count >> 3;
    int remaining = count & 0x7;

#if SLT_PERSP
    /* Perspective: one reciprocal pair per eight pixels, linear in between. */
    int step_au8 = step_au_dx * 8;
    int step_bv8 = step_bv_dx * 8;
    int step_cw8 = step_cw_dx * 8;
#if SLT_SHADE_INTERP
    int step_shade8 = step_shade8bit_dx_ish8 * 8;
#endif

    while( blocks-- > 0 )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            int curr_u = au / w;
            int curr_v = bv / w;
            curr_u = clamp(curr_u, 0, texture_width - 1);

            int next_u = curr_u;
            int next_v = curr_v;
            int w_next = (cw + step_cw8) >> texture_shift;
            if( w_next != 0 )
            {
                next_u = (au + step_au8) / w_next;
                next_v = (bv + step_bv8) / w_next;
                next_u = clamp(next_u, 0, texture_width - 1);
            }

            int step_scale = 1 << (texture_shift - 3);
            int step_u = (next_u - curr_u) * step_scale;
            int step_v = (next_v - curr_v) * step_scale;

            SLT_BLOCK8(
                pixel_buffer,
                offset,
                texels,
                curr_u * (1 << texture_shift),
                (curr_v & u_mask) * (1 << texture_shift),
                step_u,
                step_v,
                texture_shift,
                v_mask,
                shade8bit_ish8 >> 8);
        }

        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
#if SLT_SHADE_INTERP
        shade8bit_ish8 += step_shade8;
#endif
    }

    if( remaining > 0 )
    {
        int w = cw >> texture_shift;
        if( w == 0 )
            return;

        int curr_u = au / w;
        int curr_v = bv / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);

        int next_u = curr_u;
        int next_v = curr_v;
        int w_next = (cw + step_cw8) >> texture_shift;
        if( w_next != 0 )
        {
            next_u = (au + step_au8) / w_next;
            next_v = (bv + step_bv8) / w_next;
            next_u = clamp(next_u, 0, texture_width - 1);
        }

        int step_scale = 1 << (texture_shift - 3);
        int step_u = (next_u - curr_u) * step_scale;
        int step_v = (next_v - curr_v) * step_scale;

        int u_scan = curr_u * (1 << texture_shift);
        int v_scan = (curr_v & u_mask) * (1 << texture_shift);
        int shade = shade8bit_ish8 >> 8;

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            SLT_EMIT(pixel_buffer[offset], texels[u + v], shade);
            u_scan += step_u;
            v_scan += step_v;
            offset += 1;
        }
    }
#else
    /* Affine: one reciprocal pair for the whole run, linear across it. */
    int curr_u = 0;
    int curr_v = 0;
    int w = cw >> texture_shift;
    if( w != 0 )
    {
        curr_u = au / w;
        curr_v = bv / w;
    }
    curr_u = clamp(curr_u, 0, texture_width - 1);

    int end_u = 0;
    int end_v = 0;
    int w_end = toridraw_add_mul32(cw, step_cw_dx, count) >> texture_shift;
    if( w_end != 0 )
    {
        end_u = toridraw_add_mul32(au, step_au_dx, count) / w_end;
        end_v = toridraw_add_mul32(bv, step_bv_dx, count) / w_end;
    }
    end_u = clamp(end_u, 0, texture_width - 1);

    int u_scan = curr_u * (1 << texture_shift);
    int v_scan = (curr_v & u_mask) * (1 << texture_shift);
    int step_u = (end_u - curr_u) * (1 << texture_shift) / count;
    int step_v = ((end_v - curr_v) * (1 << texture_shift)) / count;

#if SLT_SHADE_INTERP
    int step_shade8 = step_shade8bit_dx_ish8 * 8;
#endif

    while( blocks-- > 0 )
    {
        SLT_BLOCK8(
            pixel_buffer,
            offset,
            texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            v_mask,
            shade8bit_ish8 >> 8);

        u_scan += step_u * 8;
        v_scan += step_v * 8;
        offset += 8;
#if SLT_SHADE_INTERP
        shade8bit_ish8 += step_shade8;
#endif
    }

    if( remaining > 0 )
    {
        int shade = shade8bit_ish8 >> 8;
        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            SLT_EMIT(pixel_buffer[offset], texels[u + v], shade);
            u_scan += step_u;
            v_scan += step_v;
            offset += 1;
        }
    }
#endif
}

/* ------------------------------------------------------------- triangle */

static void
SLT_FN(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int face_alpha,
    int* RESTRICT texels,
    int texture_width)
{
#if !SLT_SHADE_INTERP
    (void)shade7bit_b;
    (void)shade7bit_c;
#endif
#if !SLT_FACE_ALPHA
    (void)face_alpha;
#endif

    struct ScanlineTri tri;
    struct ScanlineEdges edges;

    if( !scanline_tri_build(&tri, x0, x1, x2, y0, y1, y2) )
        return;
    if( !scanline_edges_build(&edges, &tri, screen_height) )
        return;

    /* The uv basis lives on the texture triangle, which the y-sort never
     * touches, so these coefficients are computed from the arguments as given. */
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    int uv_xhat = vU_z * vV_y - vU_y * vV_z;
    int uv_yhat = vU_x * vV_z - vU_z * vV_x;
    int uv_zhat = vU_y * vV_x - vU_x * vV_y;

    int ov_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int ov_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int ov_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int uo_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int uo_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;
    int uo_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    struct ToriDraw_TexturePlane32 texture_plane = {
        .term = {
            { ov_xhat, ov_yhat, ov_zhat, 0 },
            { uo_xhat, uo_yhat, uo_zhat, 0 },
            { uv_xhat, uv_yhat, uv_zhat, 0 },
        },
    };
    if( !ToriDraw_TexturePlanePrepare32(
            &texture_plane, screen_width, screen_height, camera_cot16) )
        return;
    ov_xhat = texture_plane.term[0].x;
    ov_yhat = texture_plane.term[0].y;
    uo_xhat = texture_plane.term[1].x;
    uo_yhat = texture_plane.term[1].y;
    uv_xhat = texture_plane.term[2].x;
    uv_yhat = texture_plane.term[2].y;

    /* Hoisted out of the row loop; the `branching` kernels redo these per
     * scanline. */
    int texture_shift = (texture_width & 0x80) ? 7 : 6;
    int v_mask = (texture_shift == 7) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    /* Rows and spans stay centered so a large x coefficient is multiplied
     * once, without a pair of overflowing terms that would later cancel. */
    int half_width = screen_width >> 1;
    int half_height = screen_height >> 1;

    int au_base = texture_plane.term[0].base;
    int bv_base = texture_plane.term[1].base;
    int cw_base = texture_plane.term[2].base;

#if SLT_SHADE_INTERP
    /* Shade is a plane over the screen triangle, invariant under the y-sort:
     * derive it from the unsorted vertices, recover the signed area's sign
     * from the permutation parity, and take the base from the sorted apex. */
    int sarea = tri.parity ? -tri.sarea : tri.sarea;

    int dx_ab = x1 - x0;
    int dy_ab = y1 - y0;
    int dx_ac = x2 - x0;
    int dy_ac = y2 - y0;

    int d_shade_ab = shade7bit_b - shade7bit_a;
    int d_shade_ac = shade7bit_c - shade7bit_a;

    int shade_xhat_ish8 = ((d_shade_ab * dy_ac - d_shade_ac * dy_ab) << 9) / sarea;
    int shade_yhat_ish8 = ((d_shade_ac * dx_ab - d_shade_ab * dx_ac) << 9) / sarea;

    int shades[3];
    shades[0] = shade7bit_a;
    shades[1] = shade7bit_b;
    shades[2] = shade7bit_c;

    int shade_base_ish8 =
        (shades[tri.perm[0]] << 9) - shade_xhat_ish8 * tri.xa + shade_xhat_ish8;
#else
    int shade_xhat_ish8 = 0;
    int shade_yhat_ish8 = 0;
    int shade_base_ish8 = (shade7bit_a << 1) << 8;
#endif

    for( int seg = 0; seg < 2; seg++ )
    {
        int rows = edges.rows[seg];
        if( rows <= 0 )
            continue;

        int l_ish16 = edges.l_ish16[seg];
        int l_step = edges.l_step[seg];
        int r_ish16 = edges.r_ish16[seg];
        int r_step = edges.r_step[seg];

        int row = edges.y0[seg];
        int offset = row * stride;

        int dy = row - half_height;
        int au = au_base + ov_yhat * dy;
        int bv = bv_base + uo_yhat * dy;
        int cw = cw_base + uv_yhat * dy;

#if SLT_SHADE_INTERP
        int shade_ish8 = shade_base_ish8 + shade_yhat_ish8 * (row - tri.ya);
#else
        int shade_ish8 = shade_base_ish8;
#endif

        int no_hclip =
            scanline_segment_no_hclip(l_ish16, l_step, r_ish16, r_step, rows, screen_width);

        while( rows-- > 0 )
        {
            int x_start;
            int count;

            if( no_hclip )
            {
                x_start = l_ish16 >> 16;
                count = (r_ish16 >> 16) - x_start;
            }
            else
            {
                count = scanline_row_clip(l_ish16, r_ish16, screen_width, &x_start);
            }

            if( count > 0 )
            {
                int const centered_x = x_start - half_width;
                SLT_SPAN_FN(
                    pixel_buffer,
                    offset + x_start,
                    count,
                    au + ov_xhat * centered_x,
                    bv + uo_xhat * centered_x,
                    cw + uv_xhat * centered_x,
                    ov_xhat,
                    uo_xhat,
                    uv_xhat,
                    shade_ish8 + shade_xhat_ish8 * x_start,
                    shade_xhat_ish8,
                    texels,
                    texture_shift,
                    u_mask,
                    v_mask,
                    texture_width,
                    face_alpha);
            }

            l_ish16 += l_step;
            r_ish16 += r_step;
            au += ov_yhat;
            bv += uo_yhat;
            cw += uv_yhat;
#if SLT_SHADE_INTERP
            shade_ish8 += shade_yhat_ish8;
#endif
            offset += stride;
        }
    }
}

#undef SLT_STORE
#undef SLT_EMIT
#undef SLT_BLOCK8

#undef SLT_FN
#undef SLT_SPAN_FN
#undef SLT_PERSP
#undef SLT_TRANS
#undef SLT_SHADE_INTERP
#undef SLT_FACE_ALPHA
