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
 * [gate][facealpha][modulate]. The plane family's twelve.
 *
 * The two plain gates use the sampler-shaped scalar kernels rather than the
 * faster SIMD ones, because the HD path always has a sampler to honour — a
 * material may ask for clamp addressing, which the SIMD entry points cannot
 * express. A caller that knows its material is repeat-addressed and needs the
 * speed can still call the SIMD kernel directly.
 */
typedef void (*hd_plane_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, const struct ToriDraw_TexSampler*);

static const hd_plane_fn g_hd_plane[3][2][2] = {
    /* texopaque */
    { { raster_texplane_persp_texopaque_branching_lerp8_v3,
        raster_texplane_persp_texopaque_modulate_branching_lerp8_v3 },
      { raster_texplane_persp_texopaque_facealpha_branching_lerp8_v3,
        raster_texplane_persp_texopaque_facealpha_modulate_branching_lerp8_v3 } },
    /* textrans */
    { { raster_texplane_persp_textrans_branching_lerp8_v3,
        raster_texplane_persp_textrans_modulate_branching_lerp8_v3 },
      { raster_texplane_persp_textrans_facealpha_branching_lerp8_v3,
        raster_texplane_persp_textrans_facealpha_modulate_branching_lerp8_v3 } },
    /* texalpha */
    { { raster_texplane_persp_texalpha_branching_lerp8_v3,
        raster_texplane_persp_texalpha_modulate_branching_lerp8_v3 },
      { raster_texplane_persp_texalpha_facealpha_branching_lerp8_v3,
        raster_texplane_persp_texalpha_facealpha_modulate_branching_lerp8_v3 } },
};

/* [kind-1][gate][facealpha][modulate] — cylinder, cube, sphere. */
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

static const hd_mapped_fn g_hd_mapped[3][3][2][2] = {
    HD_MAPPED_GATES(texcylinder),
    HD_MAPPED_GATES(texcube),
    HD_MAPPED_GATES(texsphere),
};
#undef HD_MAPPED_GATES

/* ------------------------------------------------------------ the tint */

/*
 * The modulate tint, from the face's own colour.
 *
 * Hue and saturation only — the authored lightness is NOT folded in. For a
 * textured face the lighting pass has already overwritten colors_a/b/c with
 * plain lightness, and that lightness arrives at the kernel as the shade; using
 * it here as well counts it twice and collapses every lightness-0 face to black.
 * That exact bug is on record (docs/rs2012_materials_backport/
 * HISTORICAL_alpha_kernels.md §2: material 2164 vanished). So the hsl16 is
 * rebuilt at the midpoint of the lightness range, where the palette gives the
 * pure hue, and only the chroma survives.
 */
#define TORIDRAW_HD_MODULATE_LIGHTNESS 64

static void
hd_face_tint(int hsl16, struct ToriDraw_TexSampler* sampler)
{
    int keyed = (hsl16 & 0xFF80) | TORIDRAW_HD_MODULATE_LIGHTNESS;
    int rgb = g_hsl16_to_rgb_table[keyed & 0xFFFF];

    /* 0..256, so a white tint is the exact identity: (c * 256) >> 8 == c. */
    sampler->tint_r = (((rgb >> 16) & 0xFF) * 256) / 255;
    sampler->tint_g = (((rgb >> 8) & 0xFF) * 256) / 255;
    sampler->tint_b = ((rgb & 0xFF) * 256) / 255;
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
};

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

    struct ToriDraw_TexSampler sampler;
    ToriDraw_TexSamplerInit(&sampler, mat->texels, mat->width);
    sampler.clamp_s = mat->clamp_s;
    sampler.clamp_t = mat->clamp_t;
    sampler.face_alpha = face_alpha;
    if( use_modulate )
        hd_face_tint(color_a, &sampler);

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

    const struct ToriDraw_TexMapping* mapping = NULL;
    if( render_type >= 1 && ctx->hd && ctx->hd->texture_mappings && coord >= 0 &&
        coord < m->textured_face_count )
        mapping = &ctx->hd->texture_mappings[coord];

    if( render_type >= 1 && !mapping )
    {
        /* The render type names a projection the model has no mapping for.
         * Drawing it through the plane kernel is wrong, but it is visible and
         * counted, which beats a hole in the mesh. */
        if( st )
            st->fallback_no_mapping++;
        render_type = 0;
    }

    if( render_type == 0 )
    {
        /* The plane projector: p/m/n are vertex indices, from the mapping entry
         * when there is one and from the face's own vertices otherwise. */
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

        g_hd_plane[gate][use_facealpha][use_modulate](
            ctx->pixel_buffer, ctx->stride, ctx->screen_width, ctx->screen_height,
            ctx->camera_cot16,
            ctx->scene->screen_vertices_x[ia], ctx->scene->screen_vertices_x[ib],
            ctx->scene->screen_vertices_x[ic],
            ctx->scene->screen_vertices_y[ia], ctx->scene->screen_vertices_y[ib],
            ctx->scene->screen_vertices_y[ic],
            ctx->scene->orthographic_vertices_x[tp], ctx->scene->orthographic_vertices_x[tm],
            ctx->scene->orthographic_vertices_x[tn],
            ctx->scene->orthographic_vertices_y[tp], ctx->scene->orthographic_vertices_y[tm],
            ctx->scene->orthographic_vertices_y[tn],
            ctx->scene->orthographic_vertices_z[tp], ctx->scene->orthographic_vertices_z[tm],
            ctx->scene->orthographic_vertices_z[tn],
            shade_a, shade_b, shade_c, &sampler);
        return;
    }

    /* A mapping projects from MODEL space, so it needs the model's own
     * vertices, not the camera-space ones the plane path walks. */
    if( st )
    {
        if( render_type == 1 )
            st->drawn_cylinder++;
        else if( render_type == 2 )
            st->drawn_cube++;
        else
            st->drawn_sphere++;
    }

    g_hd_mapped[render_type - 1][gate][use_facealpha][use_modulate](
        ctx->pixel_buffer, ctx->stride, ctx->screen_width, ctx->screen_height,
        ctx->scene->screen_vertices_x[ia], ctx->scene->screen_vertices_x[ib],
        ctx->scene->screen_vertices_x[ic],
        ctx->scene->screen_vertices_y[ia], ctx->scene->screen_vertices_y[ib],
        ctx->scene->screen_vertices_y[ic],
        ctx->scene->orthographic_vertices_z[ia], ctx->scene->orthographic_vertices_z[ib],
        ctx->scene->orthographic_vertices_z[ic],
        m->vertices_x[ia], m->vertices_y[ia], m->vertices_z[ia],
        m->vertices_x[ib], m->vertices_y[ib], m->vertices_z[ib],
        m->vertices_x[ic], m->vertices_y[ic], m->vertices_z[ic],
        shade_a, shade_b, shade_c, mapping, &sampler);
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
    memset(&ctx, 0, sizeof(ctx));
    ctx.m = model_as_full(hnd);
    ctx.hd = ToriDraw_ModelAsHD(hnd);
    ctx.scene = scene;
    ctx.materials = materials;
    ctx.stats = out_stats;

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

    ctx.stride = view_port->stride ? view_port->stride : view_port->width;
    ctx.screen_width = clip_right - clip_left;
    ctx.screen_height = clip_bottom - clip_top;
    ctx.offset_x = ctx.screen_width >> 1;
    ctx.offset_y = ctx.screen_height >> 1;
    ctx.camera_cot16 =
        toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048);
    ctx.pixel_buffer = pixel_buffer + clip_left + clip_top * ctx.stride;

    if( out_stats )
        out_stats->faces = ctx.m->face_count;

    for( int i = 0; i < scene->tmp_face_order_count; i++ )
        hd_draw_face(&ctx, scene->tmp_face_order[i]);

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
    assert(fi);
    if( !vx )
    {
        free(vx);
        free(fi);
        return false;
    }
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
            /* The cylinder's u wrap width, which its seam fixup folds against. */
            out[i].scale_z = scale_x ? (float)scale_x[i] / 1024.0f : 0.0f;
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
