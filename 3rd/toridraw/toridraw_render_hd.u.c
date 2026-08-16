/*
 * ToriDraw_RenderHD — routing an OB3 model's faces to the 48 textured kernels.
 *
 * See toridraw_render_hd.h for what the four routing decisions are. This file
 * is the dispatch: two tables of function pointers, because the plane family
 * and the three mapped families take different arguments (a plane walks
 * orthographic p/m/n; a mapping needs model-space vertices and the mapping
 * itself), and one per-face selector that fills a sampler and calls through.
 *
 * ## Fallbacks are visible, never silent
 *
 * Three things can go wrong per face, and each has a counter rather than a
 * disappearance:
 *
 *   - the material is not resident      -> flat colour, `fallback_no_texels`
 *   - the render type wants a mapping
 *     the model does not carry          -> plane kernel, `fallback_no_mapping`
 *   - the face is hidden or fully clear -> skipped, counted
 *
 * The stock raster skips a face whose texture has not streamed in, which is
 * right for a game (the face appears a frame later) and wrong for a viewer
 * (the model looks broken and nothing says why). Here the geometry always draws.
 */

#include "toridraw_render_hd.h"
#include <assert.h>

#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_texture_uv.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ dispatch */

/*
 * Every textured face goes through the mapped-kernel shape: uv solved per
 * vertex in model space, then interpolated perspective-correctly. The two plain
 * gates use the sampler-shaped scalar kernels rather than the faster SIMD ones,
 * because the HD path always has a sampler to honour — a material may ask for
 * clamp addressing, which the SIMD entry points cannot express.
 *
 * Render type 0 is NOT drawn by `texplane`. That kernel walks the P/M/N plane in
 * camera space and intersects eye rays with it, which is only right when the
 * face lies in that plane — OSRS content always does, HD content routinely
 * does not, and the result is a texture that slides across its face as the view
 * turns. `texpmn` projects each vertex onto the frame along the frame's normal
 * instead, as the HD reference does; texmap_common.h has the full account.
 *
 * `[gate][facealpha][modulate]` for the frame family; the same again behind
 * `[kind-1]` for cylinder, cube, sphere. The two differ only in the type of the
 * mapping argument, so the routing decision is made once and only the table
 * it lands in differs.
 */
typedef void (*hd_pmn_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int, int, const struct ToriDraw_TexPlaneFrame*,
    const struct ToriDraw_TexSampler*);

typedef void (*hd_mapped_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int, int, const struct ToriDraw_TexMapping*,
    const struct ToriDraw_TexSampler*);

#define HD_MAPPED_GATES(fam)                                                                       \
    { { { raster_##fam##_persp_texopaque_branching_lerp8_v3,                                       \
          raster_##fam##_persp_texopaque_modulate_branching_lerp8_v3 },                            \
        { raster_##fam##_persp_texopaque_facealpha_branching_lerp8_v3,                             \
          raster_##fam##_persp_texopaque_facealpha_modulate_branching_lerp8_v3 } },                \
      { { raster_##fam##_persp_textrans_branching_lerp8_v3,                                        \
          raster_##fam##_persp_textrans_modulate_branching_lerp8_v3 },                             \
        { raster_##fam##_persp_textrans_facealpha_branching_lerp8_v3,                              \
          raster_##fam##_persp_textrans_facealpha_modulate_branching_lerp8_v3 } },                 \
      { { raster_##fam##_persp_texalpha_branching_lerp8_v3,                                        \
          raster_##fam##_persp_texalpha_modulate_branching_lerp8_v3 },                             \
        { raster_##fam##_persp_texalpha_facealpha_branching_lerp8_v3,                              \
          raster_##fam##_persp_texalpha_facealpha_modulate_branching_lerp8_v3 } } }

static const hd_pmn_fn g_hd_pmn[3][2][2] = HD_MAPPED_GATES(texpmn);

static const hd_mapped_fn g_hd_mapped[3][3][2][2] = {
    HD_MAPPED_GATES(texcylinder),
    HD_MAPPED_GATES(texcube),
    HD_MAPPED_GATES(texsphere),
};
#undef HD_MAPPED_GATES

/*
 * The same matrix again, depth-tested: 48 twins, one per point, for
 * ToriDraw_RenderHDZBuffered.
 *
 * A second pair of tables rather than a flag on the first, because a depth twin
 * takes MORE ARGUMENTS than its plain sibling — three corner depth keys and the
 * buffer to test against — and there is no honest way to spell that as one
 * function pointer type. Keeping them apart also keeps the plain call sites
 * exactly as they were: nothing on the sorted path pays for a feature it does
 * not use, not even a null check.
 *
 * The two tables are laid out identically on purpose. `[gate][facealpha]
 * [modulate]` indexes both, so the routing decision is made once, in
 * hd_draw_face, and only the call it lands on differs.
 */
typedef void (*hd_pmn_zbuf_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int, int, const struct ToriDraw_TexPlaneFrame*,
    const struct ToriDraw_TexSampler*, float, float, float, torizdepth_t*);

typedef void (*hd_mapped_zbuf_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int, int, const struct ToriDraw_TexMapping*,
    const struct ToriDraw_TexSampler*, float, float, float, torizdepth_t*);

#define HD_MAPPED_ZBUF_GATES(fam)                                                                  \
    { { { raster_##fam##_persp_texopaque_zbuf_branching_lerp8_v3,                                  \
          raster_##fam##_persp_texopaque_modulate_zbuf_branching_lerp8_v3 },                       \
        { raster_##fam##_persp_texopaque_facealpha_zbuf_branching_lerp8_v3,                        \
          raster_##fam##_persp_texopaque_facealpha_modulate_zbuf_branching_lerp8_v3 } },           \
      { { raster_##fam##_persp_textrans_zbuf_branching_lerp8_v3,                                   \
          raster_##fam##_persp_textrans_modulate_zbuf_branching_lerp8_v3 },                        \
        { raster_##fam##_persp_textrans_facealpha_zbuf_branching_lerp8_v3,                         \
          raster_##fam##_persp_textrans_facealpha_modulate_zbuf_branching_lerp8_v3 } },            \
      { { raster_##fam##_persp_texalpha_zbuf_branching_lerp8_v3,                                   \
          raster_##fam##_persp_texalpha_modulate_zbuf_branching_lerp8_v3 },                        \
        { raster_##fam##_persp_texalpha_facealpha_zbuf_branching_lerp8_v3,                         \
          raster_##fam##_persp_texalpha_facealpha_modulate_zbuf_branching_lerp8_v3 } } }

static const hd_pmn_zbuf_fn g_hd_pmn_zbuf[3][2][2] = HD_MAPPED_ZBUF_GATES(texpmn);

static const hd_mapped_zbuf_fn g_hd_mapped_zbuf[3][3][2][2] = {
    HD_MAPPED_ZBUF_GATES(texcylinder),
    HD_MAPPED_ZBUF_GATES(texcube),
    HD_MAPPED_ZBUF_GATES(texsphere),
};
#undef HD_MAPPED_ZBUF_GATES

/* ------------------------------------------------------------ the tint */


static struct ToriDraw_HDTuning g_hd_tuning = TORIDRAW_HD_TUNING_DEFAULT_INIT;

void
ToriDraw_HDSetTuning(const struct ToriDraw_HDTuning* tuning)
{
    struct ToriDraw_HDTuning defaults = TORIDRAW_HD_TUNING_DEFAULT_INIT;
    g_hd_tuning = tuning ? *tuning : defaults;
}

void
ToriDraw_HDGetTuning(struct ToriDraw_HDTuning* out)
{
    if( out )
        *out = g_hd_tuning;
}

static void
hd_face_tint(int hsl16, int material_neutral, struct ToriDraw_TexSampler* sampler)
{
    /*
     * `tint_lightness` < 0 means "use the face's own authored lightness".
     * The default keeps it fixed at the midpoint — see the note on
     * TORIDRAW_HD_MODULATE_LIGHTNESS above for why the authored lightness is
     * normally excluded.
     */
    int lightness = g_hd_tuning.tint_lightness < 0 ? (hsl16 & 0x7F)
                                                   : (g_hd_tuning.tint_lightness & 0x7F);
    int keyed = (hsl16 & 0xFF80) | lightness;
    int rgb = g_hsl16_to_rgb_table[keyed & 0xFFFF];
    int scale = g_hd_tuning.tint_scale > 0 ? g_hd_tuning.tint_scale : 100;
    /*
     * The material's own average is the rule; an explicitly tuned neutral
     * OVERRIDES it, because a sweep that cannot force one global value cannot
     * measure what the per-texture rule is worth.
     */
    int neutral = g_hd_tuning.texture_neutral > 0
                      ? g_hd_tuning.texture_neutral
                      : (material_neutral > 0 ? material_neutral : 256);

    /*
     * The sampler multiplies by this and shifts down 8, so a tint of 256 is the
     * identity for a 255 texel. Dividing by `neutral` rather than 255 makes a
     * MID-GREY texel the identity instead — see TORIDRAW_HD_TEXTURE_NEUTRAL.
     */
    int tr = (rgb >> 16) & 0xFF, tg = (rgb >> 8) & 0xFF, tb = rgb & 0xFF;

    /* Blend the tint toward its own luminance — toward grey, not toward white,
     * so desaturating does not also brighten. */
    int sat = g_hd_tuning.tint_saturation;
    if( sat < 0 )
        sat = 0;
    if( sat < 100 )
    {
        int luma = (2126 * tr + 7152 * tg + 722 * tb) / 10000;
        tr = luma + (tr - luma) * sat / 100;
        tg = luma + (tg - luma) * sat / 100;
        tb = luma + (tb - luma) * sat / 100;
    }

    sampler->tint_r = (tr * 256 * scale) / (neutral * 100);
    sampler->tint_g = (tg * 256 * scale) / (neutral * 100);
    sampler->tint_b = (tb * 256 * scale) / (neutral * 100);
}

/* ---------------------------------------------------------- per model */

struct hd_ctx
{
    struct ToriDraw_Model* m;
    struct ToriDraw_ModelHD* hd;
    struct ToriDraw_Scene* scene;
    toripixel_t* pixel_buffer;
    int stride;
    int screen_width;
    int screen_height;
    int offset_x;
    int offset_y;
    int camera_cot16;
    const struct ToriDraw_HDMaterials* materials;
    struct ToriDraw_HDRenderStats* stats;

    /*
     * Depth state. `zbuffer` is NULL on the sorted path and nothing below reads
     * the rest; when it is set, every face of this model routes to the depth
     * twins and the buffer has already been reset for this model.
     */
    torizdepth_t* zbuffer;
    bool parallel;
    /** The model's camera-space centre depth, which the projection subtracted
     *  out of screen_vertices_z. See ToriDraw_ZbufTarget.model_mid_z. */
    int model_mid_z;
    /** For the untextured faces, which go through the shared SD depth family. */
    struct ToriDraw_ZbufTarget zbuf_target;
    struct ToriDraw_ZbufFaceSource zbuf_source;
};

/**
 * The depth key of one projected vertex.
 *
 * From screen z plus the model's mid-z, not from the orthographic scratch: the
 * projection only fills that scratch for models that carry textures, and an HD
 * model whose faces are all untextured would read whatever the last textured one
 * left there.
 */
static inline float
hd_vertex_key(const struct hd_ctx* ctx, int vertex)
{
    return toridraw_zdepth_key(
        ctx->scene->screen_vertices_z[vertex] + ctx->model_mid_z, ctx->parallel);
}

static const struct ToriDraw_HDMaterial*
hd_material(const struct hd_ctx* ctx, int texture_id)
{
    if( !ctx->materials || texture_id < 0 || texture_id >= ctx->materials->count )
        return NULL;
    const struct ToriDraw_HDMaterial* mat = &ctx->materials->items[texture_id];
    if( !mat->texels || (mat->width != 64 && mat->width != 128) )
        return NULL;
    return mat;
}

static void
hd_draw_face(struct hd_ctx* ctx, int face)
{
    struct ToriDraw_Model* m = ctx->m;
    struct ToriDraw_HDRenderStats* st = ctx->stats;

    int raw_type = m->face_infos ? m->face_infos[face] : 0;
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
    {
        if( st )
            st->skipped_hidden++;
        return;
    }

    /*
     * Per-corner colours exist only after a lighting pass. An unlit model is a
     * caller error, but dereferencing NULL to report it is worse than drawing
     * it flat — and this is the entry point a viewer hands arbitrary files to.
     */
    if( !m->face_colors_a || !m->face_colors_b || !m->face_colors_c )
    {
        if( st )
            st->skipped_hidden++;
        return;
    }
    int color_a = m->face_colors_a[face];
    int color_b = m->face_colors_b[face];
    int color_c = m->face_colors_c[face];
    if( color_c == TORIDRAWHSL16_HIDDEN )
    {
        if( st )
            st->skipped_hidden++;
        return;
    }

    int texture_id = m->face_textures ? m->face_textures[face] : -1;
    const struct ToriDraw_HDMaterial* mat = hd_material(ctx, texture_id);

    /* face_alphas is stored as transparency; the kernels want a source weight. */
    int raw_alpha = m->face_alphas ? (m->face_alphas[face] & 0xFF) : 0;
    int face_alpha = 0xFF - raw_alpha;

    /* ---- untextured, or a material we cannot sample: flat / gouraud ---- */
    if( texture_id < 0 || !mat )
    {
        if( texture_id >= 0 && st )
            st->fallback_no_texels++;

        if( face_alpha <= 1 )
        {
            if( st )
                st->skipped_alpha++;
            return;
        }

        bool const flat = (color_c == TORIDRAWHSL16_FLAT);
        if( st )
            st->drawn_untextured++;

        if( ctx->zbuffer )
        {
            /*
             * The SD depth family, shared rather than twinned: flat and gouraud
             * already have depth-tested kernels and they take the same face the
             * stock ones take. Only the textured half of the matrix needed new
             * variants. A flat face carries the TORIDRAWHSL16_FLAT selector in
             * colors_b/c rather than colours, so it passes colors_a three times.
             */
            ToriDraw_TriangleFaceZBuffered(
                &ctx->zbuf_target,
                &ctx->zbuf_source,
                face,
                flat ? TORIDRAW_ZBUF_MODE_FLAT : TORIDRAW_ZBUF_MODE_GOURAUD,
                color_a,
                flat ? color_a : color_b,
                flat ? color_a : color_c,
                face_alpha,
                TORIDRAW_ZBUF_TEX_OPAQUE,
                0,
                0,
                0,
                NULL,
                0,
                false,
                ctx->scene->near_clipped);
            return;
        }

        if( flat )
            ToriDraw_TriangleFaceFlat(
                ctx->pixel_buffer, face, m->face_indices_a, m->face_indices_b,
                m->face_indices_c, ctx->scene->screen_vertices_x,
                ctx->scene->screen_vertices_y, ctx->scene->screen_vertices_z,
                ctx->scene->orthographic_vertices_x, ctx->scene->orthographic_vertices_y,
                ctx->scene->orthographic_vertices_z, m->face_colors_a, m->face_alphas,
                ctx->scene->projection_near_plane_z, ctx->camera_cot16, ctx->offset_x,
                ctx->offset_y, ctx->stride, ctx->screen_width, ctx->screen_height,
                false, ctx->scene->near_clipped);
        else
            ToriDraw_TriangleFaceGouraud(
                ctx->pixel_buffer, face, m->face_indices_a, m->face_indices_b,
                m->face_indices_c, ctx->scene->screen_vertices_x,
                ctx->scene->screen_vertices_y, ctx->scene->screen_vertices_z,
                ctx->scene->orthographic_vertices_x, ctx->scene->orthographic_vertices_y,
                ctx->scene->orthographic_vertices_z, m->face_colors_a, m->face_colors_b,
                m->face_colors_c, m->face_alphas, ctx->scene->projection_near_plane_z,
                ctx->camera_cot16, ctx->offset_x, ctx->offset_y, ctx->stride,
                ctx->screen_width, ctx->screen_height, false, ctx->scene->near_clipped);
        return;
    }

    /* ------------------------------- textured: pick the four decisions ---- */

    int coord = m->face_texture_coords ? m->face_texture_coords[face] : -1;
    int render_type = 0;
    if( coord >= 0 && coord < m->textured_face_count && m->texture_render_types )
        render_type = m->texture_render_types[coord] & 0xFF;
    if( render_type > 3 )
        render_type = 0;

    int gate = mat->gate;
    if( gate < 0 || gate > 2 )
        gate = TORIDRAW_HD_GATE_OPAQUE;
    int use_facealpha = (face_alpha < 0xFF) ? 1 : 0;
    int use_modulate = mat->modulate ? 1 : 0;

    /*
     * The tint comes from the face's AUTHORED colour, not from colors_a.
     *
     * colors_a is the shade by this point — the lighting pass overwrites
     * colors_a/b/c of a textured face with plain 0..127 lightness. Feeding that
     * to the tint masks `hsl16 & 0xFF80`, which is 0 for every value under 128,
     * so every face tints with hue 0 saturation 0: a grey wash, i.e. no tint at
     * all with extra steps. `face_colors` is the flat authored HSL16 and is the
     * only place the hue still lives.
     */
    int tint_hsl = m->face_colors ? m->face_colors[face] : 0;

    struct ToriDraw_TexSampler sampler;
    ToriDraw_TexSamplerInit(&sampler, mat->texels, mat->width);
    sampler.clamp_s = mat->clamp_s;
    sampler.clamp_t = mat->clamp_t;
    sampler.face_alpha = face_alpha;
    if( use_modulate )
        hd_face_tint(tint_hsl, mat->texture_neutral, &sampler);

    if( st )
    {
        if( gate == TORIDRAW_HD_GATE_OPAQUE )
            st->gate_opaque++;
        else if( gate == TORIDRAW_HD_GATE_TRANS )
            st->gate_trans++;
        else
            st->gate_alpha++;
        st->with_facealpha += use_facealpha;
        st->with_modulate += use_modulate;
    }

    /* A textured face's shade is 0-127 lightness in colors_a/b/c, not hsl16. */
    int shade_a = color_a;
    int shade_b = (color_c == TORIDRAWHSL16_FLAT) ? color_a : color_b;
    int shade_c = (color_c == TORIDRAWHSL16_FLAT) ? color_a : color_c;

    int ia = m->face_indices_a[face];
    int ib = m->face_indices_b[face];
    int ic = m->face_indices_c[face];

    /*
     * Screen coordinates, centred.
     *
     * `screen_vertices_*` are relative to the projection origin, and the flat
     * and gouraud kernels take offset_x/offset_y and add it themselves. The
     * textured kernels have no such parameter — they rasterise the coordinates
     * they are handed — so the centring has to happen here.
     *
     * Missing it does not look like an offset bug. The untextured faces of the
     * same model draw in the right place while every textured one lands half a
     * screen up and left, so the model appears to lose its textured geometry
     * and a magnified fragment of it piles into the top-left corner.
     */
    int sx_a = ctx->scene->screen_vertices_x[ia] + ctx->offset_x;
    int sx_b = ctx->scene->screen_vertices_x[ib] + ctx->offset_x;
    int sx_c = ctx->scene->screen_vertices_x[ic] + ctx->offset_x;
    int sy_a = ctx->scene->screen_vertices_y[ia] + ctx->offset_y;
    int sy_b = ctx->scene->screen_vertices_y[ib] + ctx->offset_y;
    int sy_c = ctx->scene->screen_vertices_y[ic] + ctx->offset_y;

    /*
     * Model-space positions for the PROJECTION — the uv solve — as opposed to
     * the posed positions the projection to screen used above.
     *
     * These are the BIND POSE, never the animated vertices. The reference
     * computes a face's uv once, from the unanimated mesh, and keeps it while
     * animation moves the vertices; that is what makes a texture deform with
     * its face. Solving from the posed vertices instead recomputes the uv every
     * frame from geometry that has moved, and moved DIFFERENTLY from the thing
     * the uv is measured against: a P/M/N frame is a set of helper vertices
     * that need not be rigged with the faces that reference it, and a mapped
     * face's centre is a fixed bind-pose point. Either way the texture slides
     * across the face as the pose changes — the same "parallax" the frame
     * kernel fixed for a turning camera, brought back by a playing animation.
     *
     * A model that has never captured its originals is at its bind pose, so
     * its live vertices are the bind pose.
     */
    const vertexint_t* bx = m->original_vertices_x ? m->original_vertices_x : m->vertices_x;
    const vertexint_t* by = m->original_vertices_y ? m->original_vertices_y : m->vertices_y;
    const vertexint_t* bz = m->original_vertices_z ? m->original_vertices_z : m->vertices_z;

    const struct ToriDraw_TexMapping* mapping = NULL;
    if( render_type >= 1 && ctx->hd && ctx->hd->texture_mappings && coord >= 0 &&
        coord < m->textured_face_count )
        mapping = &ctx->hd->texture_mappings[coord];

    if( render_type >= 1 && !mapping )
    {
        /* The render type names a projection the model has no mapping for.
         * Drawing it through the frame kernel is wrong, but it is visible and
         * counted, which beats a hole in the mesh. */
        if( st )
            st->fallback_no_mapping++;
        render_type = 0;
    }

    if( render_type == 0 )
    {
        /* The frame projector: p/m/n are vertex indices, from the mapping entry
         * when there is one and from the face's own vertices otherwise. Read
         * from the bind-pose MODEL-space arrays, like the face's own vertices
         * below: the frame is a fixed uv basis, and only there does it stay put
         * as the camera moves and the animation plays. Handing the kernel the
         * camera-space positions instead is precisely the eye-ray plane walk
         * this family replaces. */
        int tp = ia, tm = ib, tn = ic;
        if( coord >= 0 && coord < m->textured_face_count && m->textured_p_coordinate )
        {
            tp = m->textured_p_coordinate[coord];
            tm = m->textured_m_coordinate[coord];
            tn = m->textured_n_coordinate[coord];
        }
        if( tp < 0 || tp >= m->vertex_count || tm < 0 || tm >= m->vertex_count ||
            tn < 0 || tn >= m->vertex_count )
        {
            if( st )
                st->skipped_hidden++;
            return;
        }

        if( st )
            st->drawn_plane++;

        struct ToriDraw_TexPlaneFrame frame = {
            bx[tp], by[tp], bz[tp],
            bx[tm], by[tm], bz[tm],
            bx[tn], by[tn], bz[tn],
        };

        if( ctx->zbuffer )
        {
            g_hd_pmn_zbuf[gate][use_facealpha][use_modulate](
                ctx->pixel_buffer, ctx->stride, ctx->screen_width, ctx->screen_height,
                sx_a, sx_b, sx_c,
                sy_a, sy_b, sy_c,
                ctx->scene->orthographic_vertices_z[ia], ctx->scene->orthographic_vertices_z[ib],
                ctx->scene->orthographic_vertices_z[ic],
                bx[ia], by[ia], bz[ia],
                bx[ib], by[ib], bz[ib],
                bx[ic], by[ic], bz[ic],
                shade_a, shade_b, shade_c, &frame, &sampler,
                hd_vertex_key(ctx, ia), hd_vertex_key(ctx, ib), hd_vertex_key(ctx, ic),
                ctx->zbuffer);
            return;
        }

        g_hd_pmn[gate][use_facealpha][use_modulate](
            ctx->pixel_buffer, ctx->stride, ctx->screen_width, ctx->screen_height,
            sx_a, sx_b, sx_c,
            sy_a, sy_b, sy_c,
            ctx->scene->orthographic_vertices_z[ia], ctx->scene->orthographic_vertices_z[ib],
            ctx->scene->orthographic_vertices_z[ic],
            bx[ia], by[ia], bz[ia],
            bx[ib], by[ib], bz[ib],
            bx[ic], by[ic], bz[ic],
            shade_a, shade_b, shade_c, &frame, &sampler);
        return;
    }

    /* A mapping projects from MODEL space — the bind pose, per the note above
     * — not the camera-space positions. */
    if( st )
    {
        if( render_type == 1 )
            st->drawn_cylinder++;
        else if( render_type == 2 )
            st->drawn_cube++;
        else
            st->drawn_sphere++;
    }

    if( ctx->zbuffer )
    {
        g_hd_mapped_zbuf[render_type - 1][gate][use_facealpha][use_modulate](
            ctx->pixel_buffer, ctx->stride, ctx->screen_width, ctx->screen_height,
            sx_a, sx_b, sx_c,
            sy_a, sy_b, sy_c,
            ctx->scene->orthographic_vertices_z[ia], ctx->scene->orthographic_vertices_z[ib],
            ctx->scene->orthographic_vertices_z[ic],
            bx[ia], by[ia], bz[ia],
            bx[ib], by[ib], bz[ib],
            bx[ic], by[ic], bz[ic],
            shade_a, shade_b, shade_c, mapping, &sampler,
            hd_vertex_key(ctx, ia), hd_vertex_key(ctx, ib), hd_vertex_key(ctx, ic),
            ctx->zbuffer);
        return;
    }

    g_hd_mapped[render_type - 1][gate][use_facealpha][use_modulate](
        ctx->pixel_buffer, ctx->stride, ctx->screen_width, ctx->screen_height,
        sx_a, sx_b, sx_c,
        sy_a, sy_b, sy_c,
        ctx->scene->orthographic_vertices_z[ia], ctx->scene->orthographic_vertices_z[ib],
        ctx->scene->orthographic_vertices_z[ic],
        bx[ia], by[ia], bz[ia],
        bx[ib], by[ib], bz[ib],
        bx[ic], by[ic], bz[ic],
        shade_a, shade_b, shade_c, mapping, &sampler);
}

/**
 * Everything both entry points need, and the clip rebasing they must agree on.
 *
 * Returns the rebased clip origin through `out_clip_left` / `out_clip_top`
 * because the depth path has to advance the z-buffer by the same amount it
 * advances the frame buffer: one index walks both, and a z-buffer that was not
 * rebased with the pixels tests each row against the wrong row.
 */
static void
hd_ctx_setup(
    struct hd_ctx* ctx,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    int* out_clip_left,
    int* out_clip_top)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->m = model_as_full(hnd);
    ctx->hd = ToriDraw_ModelAsHD(hnd);
    ctx->scene = scene;
    ctx->materials = materials;
    ctx->stats = out_stats;

    /* Same clip rebasing the stock raster does: left/top become 0 so the
     * existing x_start<0 clamps apply, and the projection origin is the centre
     * of the rebased clip rect, which is where the textured kernels anchor. */
    int clip_left = view_port->clip_left > 0 ? view_port->clip_left : 0;
    int clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
    int clip_right = view_port->clip_right > 0 ? view_port->clip_right : view_port->width;
    int clip_bottom = view_port->clip_bottom > 0 ? view_port->clip_bottom : view_port->height;
    if( clip_right < clip_left )
        clip_right = clip_left;
    if( clip_bottom < clip_top )
        clip_bottom = clip_top;

    ctx->stride = view_port->stride ? view_port->stride : view_port->width;
    ctx->screen_width = clip_right - clip_left;
    ctx->screen_height = clip_bottom - clip_top;
    ctx->offset_x = ctx->screen_width >> 1;
    ctx->offset_y = ctx->screen_height >> 1;
    ctx->camera_cot16 =
        toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048);
    ctx->pixel_buffer = pixel_buffer + clip_left + clip_top * ctx->stride;

    if( out_stats )
        out_stats->faces = ctx->m->face_count;

    *out_clip_left = clip_left;
    *out_clip_top = clip_top;
}

int
ToriDraw_RenderHD(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats)
{
    if( out_stats )
        memset(out_stats, 0, sizeof(*out_stats));

    if( !ToriDraw_ModelKindIsFull(hnd.kind) || !hnd.u.model.model )
        return TORIDRAW_CULL_ERROR;

    int cull = ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    ToriDraw_RenderModel2SortFaces(hnd, scene);

    struct hd_ctx ctx;
    int clip_left;
    int clip_top;
    hd_ctx_setup(
        &ctx, hnd, scene, view_port, camera, pixel_buffer, materials, out_stats, &clip_left,
        &clip_top);

    for( int i = 0; i < scene->tmp_face_order_count; i++ )
        hd_draw_face(&ctx, scene->tmp_face_order[i]);

    return TORIDRAW_CULL_VISIBLE;
}

/**
 * Is this face facing the camera?
 *
 * The unsorted entry points have to ask, because on the sorted path the face
 * SORT is what answers it — the depth bucketer drops a back-facing triangle
 * before it ever reaches a kernel. Skipping the sort therefore skips the cull
 * too, and a model drawn without it renders its own inside surfaces: with a
 * depth buffer they mostly lose, but "mostly" is exactly the wrong guarantee on
 * a model with interior geometry, and drawing them costs a full raster each.
 *
 * Same test, same sign convention (TORIDRAW_FLIP_WINDING included), and the same
 * exemption: a face with a vertex behind the near plane has no screen-space
 * winding yet, so the reference keeps it and lets the near-clip rebuild decide.
 */
static inline bool
hd_face_front_facing(const struct hd_ctx* ctx, int face)
{
    const struct ToriDraw_Model* m = ctx->m;
    const int* vx = ctx->scene->screen_vertices_x;
    const int* vy = ctx->scene->screen_vertices_y;

    int const a = m->face_indices_a[face];
    int const b = m->face_indices_b[face];
    int const c = m->face_indices_c[face];

    if( ctx->scene->near_clipped &&
        (vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED || vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
        return true;

    long long const dx1 = (long long)vx[a] - vx[b];
    long long const dy1 = (long long)vy[a] - vy[b];
    long long const dx2 = (long long)vx[c] - vx[b];
    long long const dy2 = (long long)vy[c] - vy[b];

    return toridraw_winding_front_facing(dx1 * dy2 - dy1 * dx2);
}

int
ToriDraw_RenderHDZBuffered(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats)
{
    if( out_stats )
        memset(out_stats, 0, sizeof(*out_stats));

    if( !ToriDraw_ModelKindIsFull(hnd.kind) || !hnd.u.model.model )
        return TORIDRAW_CULL_ERROR;

    int cull = ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    /* No ToriDraw_RenderModel2SortFaces. That is the whole difference at this
     * level: no depth buckets, no priority passes, no face order. */

    struct hd_ctx ctx;
    int clip_left;
    int clip_top;
    hd_ctx_setup(
        &ctx, hnd, scene, view_port, camera, pixel_buffer, materials, out_stats, &clip_left,
        &clip_top);

    /* Calling this entry point IS the opt-in, so the buffer is sized here rather
     * than gated on TORIDRAW_SCENE_MODEL_ZBUFFER or on a per-model flag. */
    int const rows = clip_top + ctx.screen_height;
    ToriDraw_SceneZBufferResize(scene, ctx.stride, rows);
    assert(ToriDraw_SceneHasZBuffer(scene, ctx.stride, rows));

    ctx.parallel = toridraw_proj_is_parallel(camera->proj_mode);
    ctx.model_mid_z = scene->projected_vertex.z;
    /* Rebased by the same amount as the frame buffer, so one offset walks both. */
    ctx.zbuffer = scene->zbuffer + clip_left + clip_top * ctx.stride;

    ctx.zbuf_target.pixel_buffer = ctx.pixel_buffer;
    ctx.zbuf_target.zbuffer = ctx.zbuffer;
    ctx.zbuf_target.stride = ctx.stride;
    ctx.zbuf_target.screen_width = ctx.screen_width;
    ctx.zbuf_target.screen_height = ctx.screen_height;
    ctx.zbuf_target.camera_cot16 = ctx.camera_cot16;
    ctx.zbuf_target.offset_x = ctx.offset_x;
    ctx.zbuf_target.offset_y = ctx.offset_y;
    ctx.zbuf_target.near_plane_z = scene->projection_near_plane_z;
    ctx.zbuf_target.model_mid_z = ctx.model_mid_z;
    ctx.zbuf_target.parallel = ctx.parallel;

    ctx.zbuf_source.face_indices_a = ctx.m->face_indices_a;
    ctx.zbuf_source.face_indices_b = ctx.m->face_indices_b;
    ctx.zbuf_source.face_indices_c = ctx.m->face_indices_c;
    ctx.zbuf_source.screen_vertices_x = scene->screen_vertices_x;
    ctx.zbuf_source.screen_vertices_y = scene->screen_vertices_y;
    ctx.zbuf_source.screen_vertices_z = scene->screen_vertices_z;
    ctx.zbuf_source.orthographic_vertices_x = scene->orthographic_vertices_x;
    ctx.zbuf_source.orthographic_vertices_y = scene->orthographic_vertices_y;
    ctx.zbuf_source.orthographic_vertices_z = scene->orthographic_vertices_z;

    /* Before the first face, not after the last: the reset is what confines the
     * depth test to this model, so it happens even if the model draws nothing. */
    toridraw_zbuf_reset(
        ctx.zbuffer,
        ctx.stride,
        ctx.screen_width,
        ctx.screen_height,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        ctx.m->vertex_count,
        ctx.offset_x,
        ctx.offset_y,
        scene->near_clipped,
        ctx.parallel);

    for( int face = 0; face < ctx.m->face_count; face++ )
    {
        if( !hd_face_front_facing(&ctx, face) )
            continue;
        hd_draw_face(&ctx, face);
    }

    return TORIDRAW_CULL_VISIBLE;
}

/* ------------------------------------------------- building the mappings */

bool
ToriDraw_ModelBuildTextureMappings(
    struct ToriDraw_ModelHD* hd,
    const int32_t* scale_x,
    const int32_t* scale_y,
    const int32_t* scale_z,
    const int8_t* rotation,
    const int8_t* direction,
    const int8_t* speed,
    const int8_t* trans_u,
    const int8_t* trans_v)
{
    if( !hd || hd->base.textured_face_count <= 0 )
        return false;

    int count = hd->base.textured_face_count;

    /*
     * The centre and basis come from the same routine the uv generator uses, so
     * a mapping built here and a uv generated offline cannot disagree.
     *
     * It takes `int` arrays, because it was written against RSCache_Model where
     * vertices and face indices really are `int`. ToriDraw_Model stores them as
     * int16_t (see the typedefs in toridraw_types.h), so they have to be WIDENED
     * rather than pointed at. Handing over the int16_t arrays directly compiles
     * silently — the toridraw unity is built with -w, so the pointer-type
     * mismatch is not reported — and reads each pair of int16 as one int, which
     * produces vertex indices in the millions and mapping centres to match.
     */
    int vc = hd->base.vertex_count;
    int fc = hd->base.face_count;
    int* vx = (int*)malloc((size_t)(vc > 0 ? vc : 1) * sizeof(int) * 3);
    int* fi = (int*)malloc((size_t)(fc > 0 ? fc : 1) * sizeof(int) * 3);
    assert(vx);
    assert(fi);
    int* vy = vx + vc;
    int* vz = vy + vc;
    int* fa = fi;
    int* fb = fi + fc;
    int* fcc = fb + fc;
    for( int i = 0; i < vc; i++ )
    {
        vx[i] = hd->base.vertices_x[i];
        vy[i] = hd->base.vertices_y[i];
        vz[i] = hd->base.vertices_z[i];
    }
    for( int i = 0; i < fc; i++ )
    {
        fa[i] = hd->base.face_indices_a[i];
        fb[i] = hd->base.face_indices_b[i];
        fcc[i] = hd->base.face_indices_c[i];
    }

    struct ToriDraw_TextureUvSource src;
    memset(&src, 0, sizeof(src));
    src.vertices_x = vx;
    src.vertices_y = vy;
    src.vertices_z = vz;
    src.vertex_count = vc;
    src.face_indices_a = fa;
    src.face_indices_b = fb;
    src.face_indices_c = fcc;
    src.face_textures = hd->base.face_textures;
    src.face_texture_coords = hd->base.face_texture_coords;
    src.face_count = fc;
    src.textured_face_count = count;
    /* Same width (int16_t vs uint16_t), and the basis casts back to int16_t
     * before use, so the reinterpret is value-preserving here. */
    src.textured_p = (const uint16_t*)hd->base.textured_p_coordinate;
    src.textured_m = (const uint16_t*)hd->base.textured_m_coordinate;
    src.textured_n = (const uint16_t*)hd->base.textured_n_coordinate;
    src.render_types = hd->base.texture_render_types;
    src.scale_x = scale_x;
    src.scale_y = scale_y;
    src.scale_z = scale_z;
    src.rotation = rotation;
    src.direction = direction;
    src.speed = speed;
    src.trans_u = trans_u;
    src.trans_v = trans_v;

    struct ToriDraw_TextureUvBasis* bases = (struct ToriDraw_TextureUvBasis*)calloc(
        (size_t)count, sizeof(*bases));
    if( !bases )
    {
        free(vx);
        free(fi);
        return false;
    }
    if( !ToriDraw_ComputeTextureUvBases(&src, bases) )
    {
        free(bases);
        free(vx);
        free(fi);
        return false;
    }

    struct ToriDraw_TexMapping* out =
        (struct ToriDraw_TexMapping*)calloc((size_t)count, sizeof(*out));
    if( !out )
    {
        free(bases);
        free(vx);
        free(fi);
        return false;
    }

    for( int i = 0; i < count; i++ )
    {
        int type = hd->base.texture_render_types ? (hd->base.texture_render_types[i] & 0xFF) : 0;
        if( type < 1 || type > 3 || !bases[i].valid )
            continue;

        out[i].centre_x = bases[i].centre_x;
        out[i].centre_y = bases[i].centre_y;
        out[i].centre_z = bases[i].centre_z;
        for( int k = 0; k < 9; k++ )
            out[i].matrix[k] = bases[i].matrix[k];

        out[i].direction = direction ? (direction[i] & 0xFF) : 0;
        out[i].speed = speed ? (float)speed[i] / 256.0f : 0.0f;

        if( type == 1 )
        {
            /*
             * The cylinder's post-atan2 u multiplier, and it must mirror the
             * three-branch rule the basis generator uses for the same field
             * (toridraw_texture_uv.c): only a POSITIVE scale_x becomes a u
             * wrap. Zero and negative both mean "no wrap" — a negative one is
             * spent on the matrix's x axis instead, and is already folded in
             * there by the time this runs.
             *
             * Dividing unconditionally made scale_x == 0 produce scale_z == 0,
             * and the kernel applies anything that is not exactly 1: `u *= 0`
             * collapses every vertex of the face onto one column of the
             * texture. That does not look like a scale bug, it looks like
             * banding, and the uv SPAN stays healthy because v still varies —
             * which is why a span check alone never caught it.
             */
            int sx = scale_x ? scale_x[i] : 0;
            out[i].scale_z = sx > 0 ? (float)sx / 1024.0f : 1.0f;
        }
        else if( type == 2 )
        {
            out[i].u_offset = trans_u ? (float)trans_u[i] / 256.0f : 0.0f;
            out[i].v_offset = trans_v ? (float)trans_v[i] / 256.0f : 0.0f;
            /* The face-selection test re-applies the raw scales on top of the
             * already-scaled basis; that double application is the reference's
             * and decides which cube face a triangle lands on. */
            out[i].axis_scale_x = (scale_x && scale_x[i]) ? (float)scale_x[i] / 64.0f : 0.0f;
            out[i].axis_scale_y = (scale_y && scale_y[i]) ? (float)scale_y[i] / 64.0f : 0.0f;
            out[i].axis_scale_z = (scale_z && scale_z[i]) ? (float)scale_z[i] / 64.0f : 0.0f;
        }
    }

    free(bases);
    free(vx);
    free(fi);
    free(hd->texture_mappings);
    hd->texture_mappings = out;
    return true;
}
