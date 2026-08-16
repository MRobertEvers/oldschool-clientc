/*
 * One perspective texture scanline, generated once per point of the capability
 * matrix. The includer defines:
 *
 *   TS2_SPAN_FN   scanline entry point name
 *   TS2_GATE      0 = every texel drawn      (texopaque)
 *                 1 = RGB 0 is a colour key  (textrans)
 *                 2 = the texel's own alpha  (texalpha)
 *   TS2_FACEALPHA 1 = additionally weight by the face's alpha
 *   TS2_MODULATE  1 = additionally tint by the face's colour
 *
 * The uv walk is draw_texture_scanline_*_blend_branching_lerp8_v3_ordered from
 * tex.span.scalar.u.c, unchanged: same span clipping, same carried-forward block
 * endpoints, same linear-fit test, same fallback to a per-pixel exact block.
 * Keeping it identical is the point — it is what lets a face change gate without
 * its texture mapping moving, and it is what the coverage test pins.
 *
 * What differs is only the per-pixel composite, and it is written once in
 * TS2_EMIT below rather than duplicated into the block and remainder paths.
 *
 * These stay scalar. The plain gates call into the per-ISA SIMD spans
 * (tex.span.{scalar,sse2,sse41,avx,neon}.u.c) because they only ever write the
 * frame buffer; every variant here either reads the destination back or does
 * per-pixel work the SIMD chain is not shaped for. The `scanline` family reached
 * the same conclusion for its own facealpha variants.
 */

/* ---------------------------------------------------------- per pixel */

/*
 * Compose one texel into `dst_ref`.
 *
 * Order is load-bearing: the shade multiplies the texel (SD's `texel x
 * lightness` contract), the tint then multiplies that result, and only the
 * *alpha* decides how much of the destination survives. Tinting after shading
 * rather than before is what keeps a lightness-0 face from collapsing to black —
 * the authored lightness already arrived as `shade`, so a tint carrying it too
 * would count it twice.
 */
#define TS2_EMIT(dst_ref, texel_expr, shade)                                                       \
    do                                                                                             \
    {                                                                                              \
        int ts2_texel_ = (texel_expr);                                                             \
        int ts2_rgb_ = ts2_texel_ & 0x00FFFFFF;                                                    \
        int ts2_skip_ = 0;                                                                         \
        int ts2_alpha_ = 0xFF;                                                                     \
                                                                                                   \
        (void)ts2_skip_;                                                                           \
        (void)ts2_alpha_;                                                                          \
                                                                                                   \
        TS2_GATE_APPLY(ts2_texel_, ts2_rgb_, ts2_skip_, ts2_alpha_);                               \
                                                                                                   \
        if( !ts2_skip_ )                                                                           \
        {                                                                                          \
            int ts2_out_ = TS2_SHADE_AND_MODULATE(ts2_rgb_, (shade));                              \
            TS2_FACEALPHA_APPLY(ts2_alpha_);                                                       \
            TS2_STORE(dst_ref, ts2_out_, ts2_alpha_);                                              \
        }                                                                                          \
    } while( 0 )

#if TS2_GATE == 0
/* Every texel drawn, fully opaque. */
#define TS2_GATE_APPLY(texel, rgb, skip, alpha) ((void)0)
#elif TS2_GATE == 1
/* Colour key: RGB 0 is a hole. Tested on RGB, not on the whole word, so a
 * texture that happens to carry a zero alpha byte on a coloured texel is not
 * mistaken for a hole. */
#define TS2_GATE_APPLY(texel, rgb, skip, alpha)                                                    \
    do                                                                                             \
    {                                                                                              \
        if( (rgb) == 0 )                                                                           \
            (skip) = 1;                                                                            \
    } while( 0 )
#else
/* The texel's own alpha. A fully transparent texel is skipped outright rather
 * than blended at 0, so it matches the colour key's "left exactly alone" and
 * costs nothing on the large transparent regions these frames have. */
#define TS2_GATE_APPLY(texel, rgb, skip, alpha)                                                    \
    do                                                                                             \
    {                                                                                              \
        (alpha) = ((unsigned)(texel) >> 24) & 0xFF;                                                \
        if( (alpha) == 0 )                                                                         \
            (skip) = 1;                                                                            \
    } while( 0 )
#endif

/*
 * Shade, and tint when this variant modulates — as one operation.
 *
 * Not two: shading down to 8 bits and then multiplying back up rounds twice and
 * amplifies the first rounding, which streaks a smooth gradient. See
 * tex_sampler_shade_tint.
 */
#if TS2_MODULATE
#define TS2_SHADE_AND_MODULATE(rgb, shade) tex_sampler_shade_tint(sampler, (rgb), (shade))
#else
#define TS2_SHADE_AND_MODULATE(rgb, shade) shade_blend((rgb), (shade))
#endif

#if TS2_FACEALPHA
#define TS2_FACEALPHA_APPLY(alpha)                                                                 \
    do                                                                                             \
    {                                                                                              \
        (alpha) = tex_sampler_mul255((alpha), sampler->face_alpha);                                \
    } while( 0 )
#else
#define TS2_FACEALPHA_APPLY(alpha) ((void)0)
#endif

#if TS2_GATE == 2 || TS2_FACEALPHA
/* An alpha of exactly 0xFF is a plain store, not a blend: alpha_blend rounds a
 * 0xFF channel down to 0xFE, so routing an opaque pixel through it would dim
 * every fully-opaque texel by one count per channel. */
#define TS2_STORE(dst_ref, rgb, alpha)                                                             \
    do                                                                                             \
    {                                                                                              \
        if( (alpha) >= 0xFF )                                                                      \
            (dst_ref) = (rgb);                                                                     \
        else                                                                                       \
            (dst_ref) = alpha_blend((alpha), (dst_ref), (rgb));                                    \
    } while( 0 )
#else
#define TS2_STORE(dst_ref, rgb, alpha)                                                             \
    do                                                                                             \
    {                                                                                              \
        (void)(alpha);                                                                             \
        (dst_ref) = (rgb);                                                                         \
    } while( 0 )
#endif

/** Eight pixels along a fitted uv line. */
static inline void
TS2_CAT(TS2_SPAN_FN, _block8)(
    int* RESTRICT pixel_buffer,
    int offset,
    const struct ToriDraw_TexSampler* RESTRICT sampler,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int shade)
{
    for( int i = 0; i < 8; i++ )
    {
        TS2_EMIT(
            pixel_buffer[offset + i],
            sampler->texels[tex_sampler_index(sampler, u_scan, v_scan)],
            shade);
        u_scan += step_u;
        v_scan += step_v;
    }
}

/**
 * The per-pixel path: an exact divide per pixel, the mapping the reference
 * rasterizer uses for every pixel. Only blocks that fail tex_span_lerp8_v_fits
 * reach it, so this is cold.
 */
static inline void
TS2_CAT(TS2_SPAN_FN, _exact)(
    int* RESTRICT pixel_buffer,
    int offset,
    const struct ToriDraw_TexSampler* RESTRICT sampler,
    int count,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade)
{
    for( int i = 0; i < count; i++ )
    {
        int w = cw >> sampler->shift;
        if( w != 0 )
        {
            /*
             * The exact path indexes by row directly, so it applies the address
             * mode to the raw quotients rather than to a pre-shifted scan.
             *
             * u is CLAMPED here even in repeat mode, and that is not an
             * oversight — it is what tex_span_exact_block does, so it is what
             * `repeat` has always meant on this path. The 8-pixel path masks u
             * instead. The two disagree in the reference and the disagreement is
             * load-bearing: matching it is what makes a repeat-addressed variant
             * bit-identical to its plain twin, which is the anchor every other
             * check in the matrix test chains off. Diverging here showed up as
             * ~26% of covered pixels sampling the neighbouring checker square.
             */
            int u_raw = au / w;
            int v_raw = bv / w;
            int u = clamp(u_raw, 0, sampler->width - 1);
            int v = sampler->clamp_t ? clamp(v_raw, 0, sampler->width - 1)
                                     : (v_raw & (sampler->width - 1));
            TS2_EMIT(
                pixel_buffer[offset + i],
                sampler->texels[u + (v << sampler->shift)],
                shade);
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
    }
}

/* ---------------------------------------------------------------- span */

static inline void
TS2_SPAN_FN(
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
    const struct ToriDraw_TexSampler* RESTRICT sampler)
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

    int texture_shift = sampler->shift;
    int texture_width = sampler->width;

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    /* Block k's end uv is block k+1's start uv — carried forward rather than
     * divided for twice. The carried u is the CLAMPED one, matching the endpoint
     * this block actually used. */
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

                TS2_CAT(TS2_SPAN_FN, _block8)(
                    pixel_buffer,
                    offset,
                    sampler,
                    cur_u << texture_shift,
                    tex_span_v_scan_start(cur_v, texture_width, texture_shift),
                    s_u,
                    s_v,
                    shade8bit_ish8 >> 8);
            }
            else
            {
                TS2_CAT(TS2_SPAN_FN, _exact)(
                    pixel_buffer,
                    offset,
                    sampler,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_dx,
                    step_bv_dx,
                    step_cw_dx,
                    shade8bit_ish8 >> 8);
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
            TS2_CAT(TS2_SPAN_FN, _exact)(
                pixel_buffer,
                offset,
                sampler,
                remaining,
                au,
                bv,
                cw,
                step_au_dx,
                step_bv_dx,
                step_cw_dx,
                shade);
            return;
        }

        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        int u_scan = cur_u << texture_shift;
        int v_scan = tex_span_v_scan_start(cur_v, texture_width, texture_shift);

        for( int i = 0; i < remaining; i++ )
        {
            TS2_EMIT(
                pixel_buffer[offset + i],
                sampler->texels[tex_sampler_index(sampler, u_scan, v_scan)],
                shade);
            u_scan += s_u;
            v_scan += s_v;
        }
    }
}

#undef TS2_EMIT
#undef TS2_GATE_APPLY
#undef TS2_SHADE_AND_MODULATE
#undef TS2_FACEALPHA_APPLY
#undef TS2_STORE

#undef TS2_SPAN_FN
#undef TS2_GATE
#undef TS2_FACEALPHA
#undef TS2_MODULATE
