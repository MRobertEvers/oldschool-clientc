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

#include "graphics/raster/texture/texmap_common.h"
#include "toridraw_raster_kernel_internal.h"
#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_texture_uv.h"

#include <stdlib.h>
#include <string.h>

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

#ifndef TORIDRAW_PIXEL16

#include "graphics/raster/texture/tex_sampler.h"

/* ------------------------------------------------------------ dispatch */

_Static_assert(
    (int)TORIDRAW_RASTER_TEXTURE_OPAQUE == (int)TORIDRAW_HD_GATE_OPAQUE,
    "HD opaque gate must index the public gate identically");
_Static_assert(
    (int)TORIDRAW_RASTER_TEXTURE_COLOR_KEY == (int)TORIDRAW_HD_GATE_TRANS,
    "HD colour-key gate must index the public gate identically");
_Static_assert(
    (int)TORIDRAW_RASTER_TEXTURE_TEXEL_ALPHA == (int)TORIDRAW_HD_GATE_ALPHA,
    "HD texel-alpha gate must index the public gate identically");

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

static int
hd_texture_neutral(int material_neutral)
{
    return g_hd_tuning.texture_neutral > 0
               ? g_hd_tuning.texture_neutral
               : (material_neutral > 0 ? material_neutral : 256);
}

static void
hd_face_tint(
    int hsl16,
    int material_neutral,
    struct ToriDraw_RasterTextureHD* texture)
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
    int neutral = hd_texture_neutral(material_neutral);
    texture->texture_neutral = neutral;

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

    texture->tint_r = (tr * 256 * scale) / (neutral * 100);
    texture->tint_g = (tg * 256 * scale) / (neutral * 100);
    texture->tint_b = (tb * 256 * scale) / (neutral * 100);
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

    /* Depth-only state is initialized before a Z kernel receives any face. */
    bool parallel;
    /** The model's camera-space centre depth, which the projection subtracted
     *  out of screen_vertices_z. See ToriDraw_ZbufTarget.model_mid_z. */
    int model_mid_z;
    /** For the untextured faces, which go through the shared SD depth family. */
    struct ToriDraw_ZbufTarget zbuf_target;
    struct ToriDraw_ZbufFaceSource zbuf_source;

    /* Public descriptors are reused for the whole pass / one face. */
    struct ToriDraw_RasterTarget target;
    struct ToriDraw_RasterFaceHD face;
    struct ToriDraw_RasterKernelHD kernel;
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
hd_draw_gouraud_z(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;

    (void)user_data;

    ToriDraw_TriangleFaceZBuffered(
        &ctx->zbuf_target,
        &ctx->zbuf_source,
        face->face_index,
        TORIDRAW_ZBUF_MODE_GOURAUD,
        face->shade[0],
        face->shade[1],
        face->shade[2],
        face->opacity,
        TORIDRAW_ZBUF_TEX_OPAQUE,
        0,
        0,
        0,
        NULL,
        0,
        false,
        ctx->scene->near_clipped);
}

static void
hd_draw_flat_z(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;

    (void)user_data;

    ToriDraw_TriangleFaceZBuffered(
        &ctx->zbuf_target,
        &ctx->zbuf_source,
        face->face_index,
        TORIDRAW_ZBUF_MODE_FLAT,
        face->shade[0],
        face->shade[1],
        face->shade[2],
        face->opacity,
        TORIDRAW_ZBUF_TEX_OPAQUE,
        0,
        0,
        0,
        NULL,
        0,
        false,
        ctx->scene->near_clipped);
}

#define TORIDRAW_HD_FLAT_FACE_ARGS(ctx, face)                                                    \
    (ctx)->pixel_buffer, (face)->face_index, (ctx)->m->face_indices_a,                           \
        (ctx)->m->face_indices_b, (ctx)->m->face_indices_c,                                     \
        (ctx)->scene->screen_vertices_x, (ctx)->scene->screen_vertices_y,                        \
        (ctx)->scene->screen_vertices_z, (ctx)->scene->orthographic_vertices_x,                  \
        (ctx)->scene->orthographic_vertices_y, (ctx)->scene->orthographic_vertices_z,            \
        (ctx)->m->face_colors_a, (ctx)->m->face_alphas,                                         \
        (ctx)->scene->projection_near_plane_z, (ctx)->camera_cot16, (ctx)->offset_x,             \
        (ctx)->offset_y, (ctx)->stride, (ctx)->screen_width, (ctx)->screen_height, false,        \
        (ctx)->scene->near_clipped

#define TORIDRAW_HD_GOURAUD_FACE_ARGS(ctx, face)                                                 \
    (ctx)->pixel_buffer, (face)->face_index, (ctx)->m->face_indices_a,                           \
        (ctx)->m->face_indices_b, (ctx)->m->face_indices_c,                                     \
        (ctx)->scene->screen_vertices_x, (ctx)->scene->screen_vertices_y,                        \
        (ctx)->scene->screen_vertices_z, (ctx)->scene->orthographic_vertices_x,                  \
        (ctx)->scene->orthographic_vertices_y, (ctx)->scene->orthographic_vertices_z,            \
        (ctx)->m->face_colors_a, (ctx)->m->face_colors_b, (ctx)->m->face_colors_c,               \
        (ctx)->m->face_alphas, (ctx)->scene->projection_near_plane_z,                            \
        (ctx)->camera_cot16, (ctx)->offset_x, (ctx)->offset_y, (ctx)->stride,                    \
        (ctx)->screen_width, (ctx)->screen_height, false, (ctx)->scene->near_clipped

static void
hd_branching_flat(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;
    (void)user_data;
    ToriDraw_TriangleFaceFlatBranching(TORIDRAW_HD_FLAT_FACE_ARGS(ctx, face));
}

static void
hd_scanline_flat(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;
    (void)user_data;
    ToriDraw_TriangleFaceFlatScanline(TORIDRAW_HD_FLAT_FACE_ARGS(ctx, face));
}

static void
hd_branching_gouraud(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;
    (void)user_data;
    ToriDraw_TriangleFaceGouraudBranching(TORIDRAW_HD_GOURAUD_FACE_ARGS(ctx, face));
}

static void
hd_scanline_gouraud(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;
    (void)user_data;
    ToriDraw_TriangleFaceGouraudScanline(TORIDRAW_HD_GOURAUD_FACE_ARGS(ctx, face));
}

static inline void
hd_sampler_from_face(
    struct ToriDraw_TexSampler* sampler,
    const struct ToriDraw_RasterFaceHD* face)
{
    const struct ToriDraw_RasterTextureHD* texture = &face->texture;

    ToriDraw_TexSamplerInit(sampler, texture->texels, texture->width);
    sampler->clamp_s = texture->clamp_s;
    sampler->clamp_t = texture->clamp_t;
    sampler->face_alpha = face->opacity;
    sampler->tint_r = texture->tint_r;
    sampler->tint_g = texture->tint_g;
    sampler->tint_b = texture->tint_b;
}

static void
hd_draw_plane_painter(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;
    const struct ToriDraw_RasterTextureHD* texture = &face->texture;
    int const ia = face->vertex[0];
    int const ib = face->vertex[1];
    int const ic = face->vertex[2];
    int const sx_a = target->screen_vertices_x[ia] + target->projection_center_x;
    int const sx_b = target->screen_vertices_x[ib] + target->projection_center_x;
    int const sx_c = target->screen_vertices_x[ic] + target->projection_center_x;
    int const sy_a = target->screen_vertices_y[ia] + target->projection_center_y;
    int const sy_b = target->screen_vertices_y[ib] + target->projection_center_y;
    int const sy_c = target->screen_vertices_y[ic] + target->projection_center_y;
    int const use_facealpha = face->opacity < 0xFF;
    int const use_modulate = texture->modulate;
    int const gate = (int)texture->gate;
    int const tp = texture->mapping.vertex_frame.p;
    int const tm = texture->mapping.vertex_frame.m;
    int const tn = texture->mapping.vertex_frame.n;
    const vertexint_t* bx = target->bind_vertices_x;
    const vertexint_t* by = target->bind_vertices_y;
    const vertexint_t* bz = target->bind_vertices_z;
    struct ToriDraw_TexPlaneFrame frame = {
        bx[tp], by[tp], bz[tp],
        bx[tm], by[tm], bz[tm],
        bx[tn], by[tn], bz[tn],
    };
    struct ToriDraw_TexSampler sampler;

    (void)user_data;
    hd_sampler_from_face(&sampler, face);
    g_hd_pmn[gate][use_facealpha][use_modulate](
        ctx->pixel_buffer,
        ctx->stride,
        ctx->screen_width,
        ctx->screen_height,
        sx_a,
        sx_b,
        sx_c,
        sy_a,
        sy_b,
        sy_c,
        target->orthographic_vertices_z[ia],
        target->orthographic_vertices_z[ib],
        target->orthographic_vertices_z[ic],
        bx[ia],
        by[ia],
        bz[ia],
        bx[ib],
        by[ib],
        bz[ib],
        bx[ic],
        by[ic],
        bz[ic],
        face->shade[0],
        face->shade[1],
        face->shade[2],
        &frame,
        &sampler);
}

static void
hd_draw_plane_z(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct hd_ctx* ctx = target->internal;
    const struct ToriDraw_RasterTextureHD* texture = &face->texture;
    int const ia = face->vertex[0];
    int const ib = face->vertex[1];
    int const ic = face->vertex[2];
    int const sx_a = target->screen_vertices_x[ia] + target->projection_center_x;
    int const sx_b = target->screen_vertices_x[ib] + target->projection_center_x;
    int const sx_c = target->screen_vertices_x[ic] + target->projection_center_x;
    int const sy_a = target->screen_vertices_y[ia] + target->projection_center_y;
    int const sy_b = target->screen_vertices_y[ib] + target->projection_center_y;
    int const sy_c = target->screen_vertices_y[ic] + target->projection_center_y;
    int const use_facealpha = face->opacity < 0xFF;
    int const use_modulate = texture->modulate;
    int const gate = (int)texture->gate;
    int const tp = texture->mapping.vertex_frame.p;
    int const tm = texture->mapping.vertex_frame.m;
    int const tn = texture->mapping.vertex_frame.n;
    const vertexint_t* bx = target->bind_vertices_x;
    const vertexint_t* by = target->bind_vertices_y;
    const vertexint_t* bz = target->bind_vertices_z;
    struct ToriDraw_TexPlaneFrame frame = {
        bx[tp], by[tp], bz[tp],
        bx[tm], by[tm], bz[tm],
        bx[tn], by[tn], bz[tn],
    };
    struct ToriDraw_TexSampler sampler;

    (void)user_data;
    hd_sampler_from_face(&sampler, face);
    g_hd_pmn_zbuf[gate][use_facealpha][use_modulate](
        ctx->pixel_buffer,
        ctx->stride,
        ctx->screen_width,
        ctx->screen_height,
        sx_a,
        sx_b,
        sx_c,
        sy_a,
        sy_b,
        sy_c,
        target->orthographic_vertices_z[ia],
        target->orthographic_vertices_z[ib],
        target->orthographic_vertices_z[ic],
        bx[ia],
        by[ia],
        bz[ia],
        bx[ib],
        by[ib],
        bz[ib],
        bx[ic],
        by[ic],
        bz[ic],
        face->shade[0],
        face->shade[1],
        face->shade[2],
        &frame,
        &sampler,
        hd_vertex_key(ctx, ia),
        hd_vertex_key(ctx, ib),
        hd_vertex_key(ctx, ic),
        target->zbuffer);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
hd_draw_mapped_painter(
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face,
    int mapped_kind)
{
    struct hd_ctx* ctx = target->internal;
    const struct ToriDraw_RasterTextureHD* texture = &face->texture;
    const struct ToriDraw_TexMapping* mapping = texture->mapping.hd_mapping;
    int const ia = face->vertex[0];
    int const ib = face->vertex[1];
    int const ic = face->vertex[2];
    int const sx_a = target->screen_vertices_x[ia] + target->projection_center_x;
    int const sx_b = target->screen_vertices_x[ib] + target->projection_center_x;
    int const sx_c = target->screen_vertices_x[ic] + target->projection_center_x;
    int const sy_a = target->screen_vertices_y[ia] + target->projection_center_y;
    int const sy_b = target->screen_vertices_y[ib] + target->projection_center_y;
    int const sy_c = target->screen_vertices_y[ic] + target->projection_center_y;
    int const use_facealpha = face->opacity < 0xFF;
    int const use_modulate = texture->modulate;
    int const gate = (int)texture->gate;
    const vertexint_t* bx = target->bind_vertices_x;
    const vertexint_t* by = target->bind_vertices_y;
    const vertexint_t* bz = target->bind_vertices_z;
    struct ToriDraw_TexSampler sampler;

    assert(mapped_kind >= 0 && mapped_kind < 3);
    assert(mapping);
    hd_sampler_from_face(&sampler, face);
    g_hd_mapped[mapped_kind][gate][use_facealpha][use_modulate](
        ctx->pixel_buffer,
        ctx->stride,
        ctx->screen_width,
        ctx->screen_height,
        sx_a,
        sx_b,
        sx_c,
        sy_a,
        sy_b,
        sy_c,
        target->orthographic_vertices_z[ia],
        target->orthographic_vertices_z[ib],
        target->orthographic_vertices_z[ic],
        bx[ia],
        by[ia],
        bz[ia],
        bx[ib],
        by[ib],
        bz[ib],
        bx[ic],
        by[ic],
        bz[ic],
        face->shade[0],
        face->shade[1],
        face->shade[2],
        mapping,
        &sampler);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
hd_draw_mapped_z(
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face,
    int mapped_kind)
{
    struct hd_ctx* ctx = target->internal;
    const struct ToriDraw_RasterTextureHD* texture = &face->texture;
    const struct ToriDraw_TexMapping* mapping = texture->mapping.hd_mapping;
    int const ia = face->vertex[0];
    int const ib = face->vertex[1];
    int const ic = face->vertex[2];
    int const sx_a = target->screen_vertices_x[ia] + target->projection_center_x;
    int const sx_b = target->screen_vertices_x[ib] + target->projection_center_x;
    int const sx_c = target->screen_vertices_x[ic] + target->projection_center_x;
    int const sy_a = target->screen_vertices_y[ia] + target->projection_center_y;
    int const sy_b = target->screen_vertices_y[ib] + target->projection_center_y;
    int const sy_c = target->screen_vertices_y[ic] + target->projection_center_y;
    int const use_facealpha = face->opacity < 0xFF;
    int const use_modulate = texture->modulate;
    int const gate = (int)texture->gate;
    const vertexint_t* bx = target->bind_vertices_x;
    const vertexint_t* by = target->bind_vertices_y;
    const vertexint_t* bz = target->bind_vertices_z;
    struct ToriDraw_TexSampler sampler;

    assert(mapped_kind >= 0 && mapped_kind < 3);
    assert(mapping);
    hd_sampler_from_face(&sampler, face);
    g_hd_mapped_zbuf[mapped_kind][gate][use_facealpha][use_modulate](
        ctx->pixel_buffer,
        ctx->stride,
        ctx->screen_width,
        ctx->screen_height,
        sx_a,
        sx_b,
        sx_c,
        sy_a,
        sy_b,
        sy_c,
        target->orthographic_vertices_z[ia],
        target->orthographic_vertices_z[ib],
        target->orthographic_vertices_z[ic],
        bx[ia],
        by[ia],
        bz[ia],
        bx[ib],
        by[ib],
        bz[ib],
        bx[ic],
        by[ic],
        bz[ic],
        face->shade[0],
        face->shade[1],
        face->shade[2],
        mapping,
        &sampler,
        hd_vertex_key(ctx, ia),
        hd_vertex_key(ctx, ib),
        hd_vertex_key(ctx, ic),
        target->zbuffer);
}

#define TORIDRAW_DEFINE_HD_MAPPED_CALLBACK(name, kind)                                         \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,              \
                     const struct ToriDraw_RasterFaceHD* face)                                 \
    {                                                                                          \
        (void)user_data;                                                                       \
        hd_draw_mapped_painter(target, face, kind);                                            \
    }

#define TORIDRAW_DEFINE_HD_MAPPED_Z_CALLBACK(name, kind)                                       \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,              \
                     const struct ToriDraw_RasterFaceHD* face)                                 \
    {                                                                                          \
        (void)user_data;                                                                       \
        hd_draw_mapped_z(target, face, kind);                                                  \
    }

TORIDRAW_DEFINE_HD_MAPPED_CALLBACK(hd_draw_cylinder, 0)
TORIDRAW_DEFINE_HD_MAPPED_CALLBACK(hd_draw_cube, 1)
TORIDRAW_DEFINE_HD_MAPPED_CALLBACK(hd_draw_sphere, 2)
TORIDRAW_DEFINE_HD_MAPPED_Z_CALLBACK(hd_draw_cylinder_z, 0)
TORIDRAW_DEFINE_HD_MAPPED_Z_CALLBACK(hd_draw_cube_z, 1)
TORIDRAW_DEFINE_HD_MAPPED_Z_CALLBACK(hd_draw_sphere_z, 2)

static const struct ToriDraw_RasterKernelHDVTable g_hd_branching_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_branching_gouraud,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_branching_flat,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_draw_plane_painter,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_draw_cylinder,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_draw_cube,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_draw_sphere,
    },
};

static const struct ToriDraw_RasterKernelHDVTable g_hd_scanline_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_scanline_gouraud,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_scanline_flat,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_draw_plane_painter,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_draw_cylinder,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_draw_cube,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_draw_sphere,
    },
};

static const struct ToriDraw_RasterKernelHDVTable g_hd_z_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_draw_gouraud_z,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_draw_flat_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_draw_plane_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_draw_cylinder_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_draw_cube_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_draw_sphere_z,
    },
};

static const struct ToriDraw_RasterKernelHD g_hd_branching_kernel = {
    .vtable = &g_hd_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelHD g_hd_scanline_kernel = {
    .vtable = &g_hd_scanline_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelHD g_hd_z_kernel = {
    .vtable = &g_hd_z_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetBranching(void)
{
    return &g_hd_branching_kernel;
}

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetScanline(void)
{
    return &g_hd_scanline_kernel;
}

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetZBuffered(void)
{
    return &g_hd_z_kernel;
}

static inline void
hd_dispatch_prepared_face(struct hd_ctx* ctx)
{
    ToriDraw_RasterKernelHDDispatch(&ctx->kernel, &ctx->target, &ctx->face);
}

#undef TORIDRAW_DEFINE_HD_MAPPED_CALLBACK
#undef TORIDRAW_DEFINE_HD_MAPPED_Z_CALLBACK

#undef TORIDRAW_HD_GOURAUD_FACE_ARGS
#undef TORIDRAW_HD_FLAT_FACE_ARGS

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

    int const ia = m->face_indices_a[face];
    int const ib = m->face_indices_b[face];
    int const ic = m->face_indices_c[face];
    if( ia < 0 || ia >= m->vertex_count || ib < 0 || ib >= m->vertex_count || ic < 0 ||
        ic >= m->vertex_count )
    {
        if( st )
            st->skipped_hidden++;
        return;
    }

    ctx->face.face_index = face;
    ctx->face.vertex[0] = ia;
    ctx->face.vertex[1] = ib;
    ctx->face.vertex[2] = ic;
    ctx->face.near_clipped =
        ctx->scene->near_clipped &&
        (ctx->scene->screen_vertices_x[ia] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         ctx->scene->screen_vertices_x[ib] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         ctx->scene->screen_vertices_x[ic] == TORIDRAW_SCREEN_X_NEAR_CLIPPED);

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

        ctx->face.face_class =
            flat ? TORIDRAW_RASTER_FACE_HD_FLAT : TORIDRAW_RASTER_FACE_HD_GOURAUD;
        ctx->face.shade[0] = color_a;
        ctx->face.shade[1] = flat ? color_a : color_b;
        ctx->face.shade[2] = flat ? color_a : color_c;
        ctx->face.opacity = face_alpha;
        hd_dispatch_prepared_face(ctx);
        return;
    }

    /* ------------------------------- textured: pick the four decisions ---- */

    int coord = m->face_texture_coords ? m->face_texture_coords[face] : -1;
    int raw_render_type = 0;
    if( coord >= 0 && coord < m->textured_face_count && m->texture_render_types )
        raw_render_type = m->texture_render_types[coord] & 0xFF;
    int render_type = raw_render_type;
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

    ctx->face.texture.texture_id = texture_id;
    ctx->face.texture.texels = mat->texels;
    ctx->face.texture.width = mat->width;
    ctx->face.texture.height = mat->width;
    ctx->face.texture.gate = (enum ToriDraw_RasterTextureGate)gate;
    ctx->face.texture.clamp_s = mat->clamp_s != 0;
    ctx->face.texture.clamp_t = mat->clamp_t != 0;
    ctx->face.texture.render_type = (unsigned int)raw_render_type;
    ctx->face.texture.modulate = use_modulate != 0;
    ctx->face.texture.tint_r = 256;
    ctx->face.texture.tint_g = 256;
    ctx->face.texture.tint_b = 256;
    ctx->face.texture.texture_neutral = hd_texture_neutral(mat->texture_neutral);
    if( use_modulate )
        hd_face_tint(tint_hsl, mat->texture_neutral, &ctx->face.texture);

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

    /* A textured face's shade is 0-127 lightness in colors_a/b/c, not HSL16. */
    bool const flat = color_c == TORIDRAWHSL16_FLAT;
    ctx->face.shade[0] = color_a;
    ctx->face.shade[1] = flat ? color_a : color_b;
    ctx->face.shade[2] = flat ? color_a : color_c;
    ctx->face.opacity = face_alpha;

    const struct ToriDraw_TexMapping* mapping = NULL;
    bool mapping_fallback = false;
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
        mapping_fallback = true;
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
        if( coord >= 0 && coord < m->textured_face_count && m->textured_p_coordinate &&
            m->textured_m_coordinate && m->textured_n_coordinate )
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

        ctx->face.face_class = TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE;
        ctx->face.texture.frame_fallback = mapping_fallback;
        ctx->face.texture.mapping.vertex_frame.p = tp;
        ctx->face.texture.mapping.vertex_frame.m = tm;
        ctx->face.texture.mapping.vertex_frame.n = tn;
        hd_dispatch_prepared_face(ctx);
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

    ctx->face.texture.frame_fallback = false;
    ctx->face.texture.mapping.hd_mapping = mapping;
    if( render_type == 1 )
        ctx->face.face_class = TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER;
    else if( render_type == 2 )
        ctx->face.face_class = TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE;
    else
        ctx->face.face_class = TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE;
    hd_dispatch_prepared_face(ctx);
}

/**
 * Everything both entry points need, including their shared clip rebasing.
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
    struct ToriDraw_HDRenderStats* out_stats)
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
    ctx->parallel = toridraw_proj_is_parallel(camera->proj_mode);
    ctx->model_mid_z = scene->projected_vertex.z;

    ctx->target.pixel_buffer = ctx->pixel_buffer;
    ctx->target.zbuffer = NULL;
    ctx->target.width = ctx->screen_width;
    ctx->target.height = ctx->screen_height;
    ctx->target.stride = ctx->stride;
    ctx->target.clip_origin_x = clip_left;
    ctx->target.clip_origin_y = clip_top;
    ctx->target.projection_center_x = ctx->offset_x;
    ctx->target.projection_center_y = ctx->offset_y;
    ctx->target.near_plane_z = scene->projection_near_plane_z;
    ctx->target.camera_cot16 = ctx->camera_cot16;
    ctx->target.model_mid_z = ctx->model_mid_z;
    ctx->target.parallel_projection = ctx->parallel;
    ctx->target.affine_textures = false;
    ctx->target.depth_test = false;
    /* Projection populates orthographic scratch for textured models.  Expose
     * that prepared-data guarantee to overrides even though the built-in HD
     * families retain their existing (non-rebuilding) near-clip behaviour. */
    ctx->target.near_clip_available =
        ToriDraw_ModelHasTextures(hnd) && scene->orthographic_vertices_x &&
        scene->orthographic_vertices_y && scene->orthographic_vertices_z;
    ctx->target.vertex_count = ctx->m->vertex_count;
    ctx->target.screen_vertices_x = scene->screen_vertices_x;
    ctx->target.screen_vertices_y = scene->screen_vertices_y;
    ctx->target.screen_vertices_z = scene->screen_vertices_z;
    ctx->target.orthographic_vertices_x = scene->orthographic_vertices_x;
    ctx->target.orthographic_vertices_y = scene->orthographic_vertices_y;
    ctx->target.orthographic_vertices_z = scene->orthographic_vertices_z;
    ctx->target.posed_vertices_x = ctx->m->vertices_x;
    ctx->target.posed_vertices_y = ctx->m->vertices_y;
    ctx->target.posed_vertices_z = ctx->m->vertices_z;
    ctx->target.bind_vertices_x =
        ctx->m->original_vertices_x ? ctx->m->original_vertices_x : ctx->m->vertices_x;
    ctx->target.bind_vertices_y =
        ctx->m->original_vertices_y ? ctx->m->original_vertices_y : ctx->m->vertices_y;
    ctx->target.bind_vertices_z =
        ctx->m->original_vertices_z ? ctx->m->original_vertices_z : ctx->m->vertices_z;
    ctx->target.internal = ctx;

    if( out_stats )
        out_stats->faces = ctx->m->face_count;
}

static const struct ToriDraw_RasterKernelHD*
hd_builtin_kernel(void)
{
    return ToriDraw_RasterGetScanline() ? ToriDraw_RasterKernelHDGetScanline()
                                        : ToriDraw_RasterKernelHDGetBranching();
}

/**
 * Is this face facing the camera?
 *
 * Model-order traversal has to ask, because on the sorted path the face sort
 * is what answers it: the depth bucketer drops a back-facing triangle before
 * it ever reaches a kernel. Skipping the sort therefore skips the cull too, and
 * a model drawn without it renders its own inside surfaces. With a depth buffer
 * they mostly lose, but "mostly" is exactly the wrong guarantee on a model with
 * interior geometry, and drawing them costs a full raster each.
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

static int
hd_render_begin(
    struct hd_ctx* ctx,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    int result;

    if( out_stats )
        memset(out_stats, 0, sizeof(*out_stats));

    assert(scene);
    assert(kernel);
    ToriDraw_RasterKernelHDAssertValid(kernel);
    if( !ToriDraw_ModelKindIsFull(hnd.kind) || !hnd.u.model.model )
        return TORIDRAW_CULL_ERROR;

    result = ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
    if( result != TORIDRAW_CULL_VISIBLE )
        return result;

    hd_ctx_setup(
        ctx, hnd, scene, view_port, camera, pixel_buffer, materials, out_stats);
    ctx->kernel = *kernel;
    return TORIDRAW_CULL_VISIBLE;
}

static void
hd_draw_faces_sorted(struct hd_ctx* ctx, struct ToriDraw_ModelHandle hnd)
{
    ToriDraw_RenderModel2SortFaces(hnd, ctx->scene);
    for( int i = 0; i < ctx->scene->tmp_face_order_count; i++ )
        hd_draw_face(ctx, ctx->scene->tmp_face_order[i]);
}

static void
hd_draw_faces_model_order(struct hd_ctx* ctx)
{
    for( int face = 0; face < ctx->m->face_count; face++ )
    {
        if( !hd_face_front_facing(ctx, face) )
            continue;
        hd_draw_face(ctx, face);
    }
}

static void
hd_enable_zbuffer(struct hd_ctx* ctx)
{
    struct ToriDraw_Scene* scene = ctx->scene;
    int const clip_left = ctx->target.clip_origin_x;
    int const clip_top = ctx->target.clip_origin_y;

    /* Calling this entry point IS the opt-in, so the buffer is sized here rather
     * than gated on TORIDRAW_SCENE_MODEL_ZBUFFER or on a per-model flag. */
    int const rows = clip_top + ctx->screen_height;
    if( !ToriDraw_SceneHasZBuffer(scene, ctx->stride, rows) )
    {
        bool const provisioned = ToriDraw_SceneZBufferResize(scene, ctx->stride, rows);

        assert(provisioned);
        (void)provisioned;
    }
    assert(ToriDraw_SceneHasZBuffer(scene, ctx->stride, rows));

    /* Rebased by the same amount as the frame buffer, so one offset walks both. */
    ctx->target.zbuffer = scene->zbuffer + clip_left + clip_top * ctx->stride;
    ctx->target.depth_test = true;

    ctx->zbuf_target.pixel_buffer = ctx->pixel_buffer;
    ctx->zbuf_target.zbuffer = ctx->target.zbuffer;
    ctx->zbuf_target.stride = ctx->stride;
    ctx->zbuf_target.screen_width = ctx->screen_width;
    ctx->zbuf_target.screen_height = ctx->screen_height;
    ctx->zbuf_target.camera_cot16 = ctx->camera_cot16;
    ctx->zbuf_target.offset_x = ctx->offset_x;
    ctx->zbuf_target.offset_y = ctx->offset_y;
    ctx->zbuf_target.near_plane_z = scene->projection_near_plane_z;
    ctx->zbuf_target.model_mid_z = ctx->model_mid_z;
    ctx->zbuf_target.parallel = ctx->parallel;

    ctx->zbuf_source.face_indices_a = ctx->m->face_indices_a;
    ctx->zbuf_source.face_indices_b = ctx->m->face_indices_b;
    ctx->zbuf_source.face_indices_c = ctx->m->face_indices_c;
    ctx->zbuf_source.screen_vertices_x = scene->screen_vertices_x;
    ctx->zbuf_source.screen_vertices_y = scene->screen_vertices_y;
    ctx->zbuf_source.screen_vertices_z = scene->screen_vertices_z;
    ctx->zbuf_source.orthographic_vertices_x = scene->orthographic_vertices_x;
    ctx->zbuf_source.orthographic_vertices_y = scene->orthographic_vertices_y;
    ctx->zbuf_source.orthographic_vertices_z = scene->orthographic_vertices_z;

    /* Before the first face, not after the last: the reset is what confines the
     * depth test to this model, so it happens even if the model draws nothing. */
    toridraw_zbuf_reset(
        ctx->target.zbuffer,
        ctx->stride,
        ctx->screen_width,
        ctx->screen_height,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        ctx->m->vertex_count,
        ctx->offset_x,
        ctx->offset_y,
        scene->near_clipped,
        ctx->parallel);
}

static int
hd_render_with_kernel_painter(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    struct hd_ctx ctx;
    int result;

    assert(kernel);
    assert((kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER) == 0);
    result = hd_render_begin(
        &ctx, hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
        kernel);
    if( result != TORIDRAW_CULL_VISIBLE )
        return result;

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        hd_draw_faces_sorted(&ctx, hnd);
    else
        hd_draw_faces_model_order(&ctx);
    return TORIDRAW_CULL_VISIBLE;
}

static int
hd_render_with_kernel_z(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    struct hd_ctx ctx;
    int result;

    assert(kernel);
    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);
    result = hd_render_begin(
        &ctx, hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
        kernel);
    if( result != TORIDRAW_CULL_VISIBLE )
        return result;
    hd_enable_zbuffer(&ctx);

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        hd_draw_faces_sorted(&ctx, hnd);
    else
        hd_draw_faces_model_order(&ctx);
    return TORIDRAW_CULL_VISIBLE;
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
    return hd_render_with_kernel_painter(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
        hd_builtin_kernel());
}

int
ToriDraw_RenderHDWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    assert(kernel);
    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER )
        return hd_render_with_kernel_z(
            hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
            kernel);
    return hd_render_with_kernel_painter(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats, kernel);
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
    return hd_render_with_kernel_z(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
        ToriDraw_RasterKernelHDGetZBuffered());
}

int
ToriDraw_RenderHDZBufferedWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    assert(kernel);
    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);
    return hd_render_with_kernel_z(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats, kernel);
}

#else /* TORIDRAW_PIXEL16 */

static void
hd_pixel16_unsupported_face(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    (void)user_data;
    (void)target;
    (void)face;
}

static const struct ToriDraw_RasterKernelHDVTable g_hd_pixel16_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_pixel16_unsupported_face,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_pixel16_unsupported_face,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_pixel16_unsupported_face,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_pixel16_unsupported_face,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_pixel16_unsupported_face,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_pixel16_unsupported_face,
    },
};

static const struct ToriDraw_RasterKernelHD g_hd_pixel16_branching_kernel = {
    .vtable = &g_hd_pixel16_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelHD g_hd_pixel16_scanline_kernel = {
    .vtable = &g_hd_pixel16_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelHD g_hd_pixel16_z_kernel = {
    .vtable = &g_hd_pixel16_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetBranching(void)
{
    return &g_hd_pixel16_branching_kernel;
}

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetScanline(void)
{
    return &g_hd_pixel16_scanline_kernel;
}

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetZBuffered(void)
{
    return &g_hd_pixel16_z_kernel;
}

static const struct ToriDraw_RasterKernelHD*
hd_pixel16_builtin_kernel(void)
{
    return ToriDraw_RasterGetScanline() ? ToriDraw_RasterKernelHDGetScanline()
                                        : ToriDraw_RasterKernelHDGetBranching();
}

static int
hd_pixel16_render_with_kernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    if( out_stats )
        memset(out_stats, 0, sizeof(*out_stats));
    assert(kernel);
    ToriDraw_RasterKernelHDAssertValid(kernel);
    (void)hnd;
    (void)scene;
    (void)position;
    (void)view_port;
    (void)camera;
    (void)pixel_buffer;
    (void)materials;
    return TORIDRAW_CULL_ERROR;
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
    return hd_pixel16_render_with_kernel(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
        hd_pixel16_builtin_kernel());
}

int
ToriDraw_RenderHDWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    return hd_pixel16_render_with_kernel(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats, kernel);
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
    return hd_pixel16_render_with_kernel(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats,
        ToriDraw_RasterKernelHDGetZBuffered());
}

int
ToriDraw_RenderHDZBufferedWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats,
    const struct ToriDraw_RasterKernelHD* kernel)
{
    assert(kernel);
    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);
    return hd_pixel16_render_with_kernel(
        hnd, scene, position, view_port, camera, pixel_buffer, materials, out_stats, kernel);
}

#endif /* TORIDRAW_PIXEL16 */

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
