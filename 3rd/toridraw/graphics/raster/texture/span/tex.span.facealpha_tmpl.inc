/*
 * One perspective texture scanline with per-face alpha. Included once per texel
 * gate by tex.span.facealpha.u.c, which defines:
 *
 *   TSFA_SPAN_FN      scanline entry point name
 *   TSFA_SPAN_BLOCK8  the 8-pixel kernel for this gate
 *   TSFA_SPAN_TRANS   1 = texel 0 is transparent (passed on to the exact block)
 *
 * The body is draw_texture_scanline_{opaque,transparent}_blend_branching_lerp8_v3_ordered
 * from tex.span.scalar.u.c with the destination read folded in: same span
 * clipping, same carried-forward block endpoints, same fit test, same fallback
 * to a per-pixel exact block. Keeping the walk identical is the point - it is
 * what lets a face change gate without its texture mapping moving.
 */

static inline void
TSFA_SPAN_FN(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width,
    int face_alpha)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    /* Block k's end uv is block k+1's start uv - carry it forward instead of
     * dividing for it twice. The carried u is the CLAMPED one, matching the
     * endpoint this block actually used. */
    int cur_u = 0;
    int cur_v = 0;
    int have_cur = 0;

    while( blocks-- )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                float inv_w = 1.0f / (float)w;
                cur_u = tex_span_u_quotient(au, inv_w, texture_width);
                cur_v = tex_span_v_quotient(bv, w, inv_w);
            }

            int w_n = (cw + step_cw8) >> texture_shift;
            int nxt_u;
            int nxt_v;
            if( w_n != 0 )
            {
                float inv_w_n = 1.0f / (float)w_n;
                nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
                nxt_v = tex_span_v_quotient(bv + step_bv8, w_n, inv_w_n);
            }
            else
            {
                nxt_u = cur_u;
                nxt_v = cur_v;
            }

            /* w_n == 0 leaves nxt_* fabricated from cur_*, so the fit test sees
             * a flat gradient and believes it. The block has no far endpoint;
             * draw it per pixel rather than smearing one row across eight. */
            if( w_n != 0 && tex_span_lerp8_v_fits(cur_v, nxt_v, texture_width) )
            {
                int s_u = (nxt_u - cur_u) << (texture_shift - 3);
                int s_v = (nxt_v - cur_v) << (texture_shift - 3);

                TSFA_SPAN_BLOCK8(
                    (uint32_t*)&pixel_buffer[offset],
                    (const uint32_t*)texels,
                    cur_u << texture_shift,
                    tex_span_v_scan_start(cur_v, texture_width, texture_shift),
                    s_u,
                    s_v,
                    texture_shift,
                    u_mask,
                    v_mask,
                    shade8bit_ish8 >> 8,
                    face_alpha);
            }
            else
            {
                tex_span_exact_block_facealpha(
                    pixel_buffer,
                    offset,
                    texels,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_dx,
                    step_bv_dx,
                    step_cw_dx,
                    shade8bit_ish8 >> 8,
                    texture_width,
                    texture_shift,
                    TSFA_SPAN_TRANS,
                    face_alpha);
            }

            cur_u = nxt_u;
            cur_v = nxt_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }
        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        int w = cw >> texture_shift;
        if( !have_cur )
        {
            float inv_w = 1.0f / (float)w;
            cur_u = tex_span_u_quotient(au, inv_w, texture_width);
            cur_v = tex_span_v_quotient(bv, w, inv_w);
        }

        int w_n = (cw + step_cw8) >> texture_shift;
        int nxt_u = cur_u;
        int nxt_v = cur_v;
        if( w_n != 0 )
        {
            float inv_w_n = 1.0f / (float)w_n;
            nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
            nxt_v = tex_span_v_quotient(bv + step_bv8, w_n, inv_w_n);
        }

        int shade = shade8bit_ish8 >> 8;

        if( w_n == 0 || !tex_span_lerp8_v_fits(cur_v, nxt_v, texture_width) )
        {
            tex_span_exact_block_facealpha(
                pixel_buffer,
                offset,
                texels,
                remaining,
                au,
                bv,
                cw,
                step_au_dx,
                step_bv_dx,
                step_cw_dx,
                shade,
                texture_width,
                texture_shift,
                TSFA_SPAN_TRANS,
                face_alpha);
            return;
        }

        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        int u_scan = cur_u << texture_shift;
        int v_scan = tex_span_v_scan_start(cur_v, texture_width, texture_shift);

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            int t = texels[u + v];
#if TSFA_SPAN_TRANS
            if( t != 0 )
#endif
                pixel_buffer[offset] =
                    alpha_blend(face_alpha, pixel_buffer[offset], shade_blend(t, shade));
            offset++;
            u_scan += s_u;
            v_scan += s_v;
        }
    }
}

#undef TSFA_SPAN_FN
#undef TSFA_SPAN_BLOCK8
#undef TSFA_SPAN_TRANS
