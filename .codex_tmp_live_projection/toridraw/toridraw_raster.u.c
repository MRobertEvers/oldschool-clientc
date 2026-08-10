#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TORIDRAW_PIXEL16
static const int g_empty_texture_texels[128 * 128] = { 0 };
#endif

enum DashModelRasterFlags
{
    RASTER_FLAG_GOURAUD_SMOOTH = 1 << 0,
    RASTER_FLAG_TEXTURE_AFFINE = 1 << 1,
};

enum FaceType
{
    FACE_TYPE_GOURAUD,
    FACE_TYPE_FLAT,
    FACE_TYPE_TEXTURED,
    FACE_TYPE_TEXTURED_FLAT_SHADE,
};

struct ToriDrawModelRasterContext
{
    uint32_t bench_flag;
    toripixel_t* pixel_buffer;
    int* face_infos;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    int num_faces;
    int* vertex_x;
    int* vertex_y;
    int* vertex_z;
    int* orthographic_vertex_x_nullable;
    int* orthographic_vertex_y_nullable;
    int* orthographic_vertex_z_nullable;
    int num_vertices;
    faceint_t* face_textures;
    faceint_t* face_texture_coords;
    int face_texture_coords_length;
    faceint_t* face_p_coordinate_nullable;
    faceint_t* face_m_coordinate_nullable;
    faceint_t* face_n_coordinate_nullable;
    int num_textured_faces;
    hsl16_t* colors_a;
    hsl16_t* colors_b;
    hsl16_t* colors_c;
    alphaint_t* face_alphas_nullable;
    int offset_x;
    int offset_y;
    int near_plane_z;
    int screen_width;
    int screen_height;
    int stride;
    int camera_cot16;
    struct ToriDraw_TextureMap* texture_map;
    int flags;
    bool allow_near_clip;
    /* Whether this model's projection could park TORIDRAW_SCREEN_X_NEAR_CLIPPED
     * in screen_vertices_x at all — scene->near_clipped, set by ToriDraw_Project.
     * The per-face sentinel tests below are gated on it, both to skip them
     * entirely for the common model and because the no-clip projection kernel
     * omits the -5001 nudge, so a genuine -5000 is possible when it is false. */
    bool near_clipped;

    /* Last texture resolved through texture_map, memoized. A model's faces are
     * drawn depth-bucketed but almost always share one or two texture ids, so
     * this turns the bounds check plus four dependent loads into one compare.
     * Lives for a single model's raster pass (the context is a stack local
     * rebuilt per model), so a texture swap between models is always seen. */
    int cache_texture_id;
    const int* cache_texels;
    int cache_texture_size;
    int cache_texture_opaque;
};

static inline void
ToriDraw_RasterModelFace(
    int face,
    struct ToriDrawModelRasterContext* ctx)
{
    assert(face >= 0 && face < ctx->num_faces);

    struct ToriDraw_Texture* texture = NULL;

    /* Render type is the raw byte (reference ModelData.light). 0=gouraud,
     * 1=flat, 2=hidden, 3=black/hidden; any other value is also hidden. */
    int raw_type = ctx->face_infos ? ctx->face_infos[face] : 0;
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
        return;
    enum FaceType type = (enum FaceType)raw_type;

    int color_a = ctx->colors_a[face];
    int color_b = ctx->colors_b[face];
    int color_c = ctx->colors_c[face];

    // Faces with color_c == -2 are not drawn. As far as I can tell, these faces are used for
    // texture PNM coordinates that do not coincide with a visible face.
    // /Users/matthewevers/Documents/git_repos/OS1/src/main/java/jagex3/dash3d/ModelUnlit.java
    // OS1 skips faces with -2.
    if( color_c == TORIDRAWHSL16_HIDDEN )
    {
        // TODO: How to organize this.
        // See here
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/rs/model/ModelData.ts
        // .light

        // and
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/mapviewer/webgl/buffer/SceneBuffer.ts
        // getModelFaces
        return;
        // color_c = 0;
    }

    int tp_vertex;
    int tm_vertex;
    int tn_vertex;

    int tp_x;
    int tp_y;
    int tp_z;
    int tm_x;
    int tm_y;
    int tm_z;
    int tn_x;
    int tn_y;
    int tn_z;

    int texture_id;
    int texture_face;
    int alpha = ctx->face_alphas_nullable ? (ctx->face_alphas_nullable[face]) : 0xFF;

    // TODO: See above comments. alpha overrides colors.
    // if( ctx->face_alphas_nullable && ctx->face_alphas_nullable[index] < 0 )
    // {
    //     return;
    // }

#ifndef TORIDRAW_PIXEL16
    const int* texels = g_empty_texture_texels;
    int texture_size = 0;
    int texture_opaque = true;
    if( ctx->face_textures != NULL )
        texture_id = ctx->face_textures[face];
    else
        texture_id = -1;

    if( texture_id != -1 )
    {
        if( texture_id == ctx->cache_texture_id )
        {
            texels = ctx->cache_texels;
            texture_size = ctx->cache_texture_size;
            texture_opaque = ctx->cache_texture_opaque;

            if( color_c == TORIDRAWHSL16_FLAT )
                goto textured_flat;
            else
                goto textured;
        }

        // gamma 0.8 is the default in os1
        texture = (texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY)
                      ? ToriDraw_TextureMapGet(ctx->texture_map, texture_id)
                      : NULL;
        // Texture not loaded (yet): skip the face. Textured faces store 0-127
        // lightness in colors_a/b/c, not HSL16, so a gouraud fallback would
        // draw garbage; the reference skips the face too.
        if( texture == NULL )
        {
            /* TORIRS_RASTER_TEX_DEBUG=1: tally skipped textured faces.
             * Resolve the env var once - getenv() walks environ with a strncmp
             * per entry, and this branch is the steady state for every face of
             * every model whose textures have not streamed in yet. */
            static int skip_tally[TORIDRAW_TEXTURE_ID_CAPACITY];
            static int skip_total = 0;
            static int debug_enabled = -1;
            if( debug_enabled < 0 )
                debug_enabled = getenv("TORIRS_RASTER_TEX_DEBUG") ? 1 : 0;

            if( debug_enabled && texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY )
            {
                skip_tally[texture_id]++;
                if( ++skip_total % 500 == 1 )
                    fprintf(
                        stderr,
                        "raster_tex_skip: total=%d id=%d (count=%d)\n",
                        skip_total,
                        texture_id,
                        skip_tally[texture_id]);
            }
            return;
        }

        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;

        ctx->cache_texture_id = texture_id;
        ctx->cache_texels = texels;
        ctx->cache_texture_size = texture_size;
        ctx->cache_texture_opaque = texture_opaque;

        if( color_c == TORIDRAWHSL16_FLAT )
            goto textured_flat;
        else
            goto textured;
    }
    else
#endif
    {
        // Alpha is a signed byte, but for non-textured
        // faces, we treat it as unsigned.
        // TORIDRAWHSL16_FLAT / TORIDRAWHSL16_HIDDEN are reserved. See lighting code.
        if( ctx->face_alphas_nullable )
            alpha = 0xFF - alpha;
        /* Reference getModelFaces: skip fully/near-fully transparent faces. */
        if( alpha <= 1 )
            return;

        if( color_c == TORIDRAWHSL16_FLAT )
        {
            type = FACE_TYPE_FLAT;
        }
        else
        {
            type = FACE_TYPE_GOURAUD;
        }
        switch( type )
        {
        case FACE_TYPE_GOURAUD:
            if( (ctx->flags & RASTER_FLAG_GOURAUD_SMOOTH) != 0 )
            {
                ToriDraw_TriangleFaceGouraudSmooth(
                    ctx->pixel_buffer,
                    face,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    ctx->face_alphas_nullable,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else
            {
                ToriDraw_TriangleFaceGouraud(
                    ctx->pixel_buffer,
                    face,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    ctx->face_alphas_nullable,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }

            break;
        case FACE_TYPE_FLAT:
            // Skip triangle if any vertex was clipped

            ToriDraw_TriangleFaceFlat(
                ctx->pixel_buffer,
                face,
                ctx->face_indices_a,
                ctx->face_indices_b,
                ctx->face_indices_c,
                ctx->vertex_x,
                ctx->vertex_y,
                ctx->vertex_z,
                ctx->orthographic_vertex_x_nullable,
                ctx->orthographic_vertex_y_nullable,
                ctx->orthographic_vertex_z_nullable,
                ctx->colors_a,
                ctx->face_alphas_nullable,
                ctx->near_plane_z,
                ctx->offset_x,
                ctx->offset_y,
                ctx->stride,
                ctx->screen_width,
                ctx->screen_height,
                ctx->allow_near_clip,
                ctx->near_clipped);

            break;
#ifndef TORIDRAW_PIXEL16
        case FACE_TYPE_TEXTURED:
        textured:;
            assert(ctx->orthographic_vertex_x_nullable != NULL);
            assert(ctx->orthographic_vertex_y_nullable != NULL);
            assert(ctx->orthographic_vertex_z_nullable != NULL);

            if( ctx->face_texture_coords && ctx->face_texture_coords[face] != -1 )
            {
                assert(ctx->face_p_coordinate_nullable != NULL);
                assert(ctx->face_m_coordinate_nullable != NULL);
                assert(ctx->face_n_coordinate_nullable != NULL);

                texture_face = ctx->face_texture_coords[face];

                tp_vertex = ctx->face_p_coordinate_nullable[texture_face];
                tm_vertex = ctx->face_m_coordinate_nullable[texture_face];
                tn_vertex = ctx->face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = ctx->face_indices_a[texture_face];
                tm_vertex = ctx->face_indices_b[texture_face];
                tn_vertex = ctx->face_indices_c[texture_face];
            }
            // texture_id = ctx->face_textures[index];
            // texture_face = ctx->face_infos[index] >> 2;
            // texture_face = ctx->face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < ctx->num_vertices);
            assert(tm_vertex < ctx->num_vertices);
            assert(tn_vertex < ctx->num_vertices);

            if( (ctx->flags & RASTER_FLAG_TEXTURE_AFFINE) != 0 )
            {
                ToriDraw_TriangleFaceTextureBlendAffineV3(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    texture_opaque,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else if( texture_opaque )
            {
                ToriDraw_TriangleFaceTextureBlendOpaque(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else
            {
                ToriDraw_TriangleFaceTextureBlendTransparent(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }

            break;
        case FACE_TYPE_TEXTURED_FLAT_SHADE:
        textured_flat:;
            assert(ctx->orthographic_vertex_x_nullable != NULL);
            assert(ctx->orthographic_vertex_y_nullable != NULL);
            assert(ctx->orthographic_vertex_z_nullable != NULL);

            if( ctx->face_texture_coords && ctx->face_texture_coords[face] != -1 )
            {
                texture_face = ctx->face_texture_coords[face];

                tp_vertex = ctx->face_p_coordinate_nullable[texture_face];
                tm_vertex = ctx->face_m_coordinate_nullable[texture_face];
                tn_vertex = ctx->face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = ctx->face_indices_a[texture_face];
                tm_vertex = ctx->face_indices_b[texture_face];
                tn_vertex = ctx->face_indices_c[texture_face];
            }
            // texture_id = ctx->face_textures[index];
            // texture_face = ctx->face_infos[index] >> 2;
            // texture_face = ctx->face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < ctx->num_vertices);
            assert(tm_vertex < ctx->num_vertices);
            assert(tn_vertex < ctx->num_vertices);

            if( (ctx->flags & RASTER_FLAG_TEXTURE_AFFINE) != 0 )
            {
                ToriDraw_TriangleFaceTextureFlatAffineV3(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    (int*)texels,
                    texture_size,
                    texture_opaque,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else if( texture_opaque )
            {
                ToriDraw_TriangleFaceTextureFlatOpaque(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else
            {
                ToriDraw_TriangleFaceTextureFlatTransparent(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }

            break;
#endif /* TORIDRAW_PIXEL16 */
        default:
            break;
        }
    }
}

static inline void
context_from_handle(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    bool smooth,
    struct ToriDrawModelRasterContext* ctx)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        ctx->num_faces = m->face_count;
        ctx->face_infos = m->face_infos;
        ctx->face_indices_a = m->face_indices_a;
        ctx->face_indices_b = m->face_indices_b;
        ctx->face_indices_c = m->face_indices_c;
        ctx->num_faces = m->face_count;
        ctx->vertex_x = scene->screen_vertices_x;
        ctx->vertex_y = scene->screen_vertices_y;
        ctx->vertex_z = scene->screen_vertices_z;
        ctx->orthographic_vertex_x_nullable = scene->orthographic_vertices_x;
        ctx->orthographic_vertex_y_nullable = scene->orthographic_vertices_y;
        ctx->orthographic_vertex_z_nullable = scene->orthographic_vertices_z;
        ctx->num_vertices = m->vertex_count;
        ctx->face_textures = m->face_textures;
        ctx->face_texture_coords = m->face_texture_coords;
        ctx->face_texture_coords_length = m->textured_face_count;
        ctx->face_p_coordinate_nullable = m->textured_p_coordinate;
        ctx->face_m_coordinate_nullable = m->textured_m_coordinate;
        ctx->face_n_coordinate_nullable = m->textured_n_coordinate;
        ctx->num_textured_faces = m->textured_face_count;
        ctx->colors_a = m->face_colors_a;
        ctx->colors_b = m->face_colors_b;
        ctx->colors_c = m->face_colors_c;
        ctx->face_alphas_nullable = m->face_alphas;
        /* Rebase clip so left/top become 0: existing x_start<0 / y<0 clamps apply.
         * Caller advances pixel_buffer by clip_left + clip_top*stride. */
        {
            int clip_left = view_port->clip_left;
            int clip_top = view_port->clip_top;
            int clip_right =
                view_port->clip_right > 0 ? view_port->clip_right : view_port->width;
            int clip_bottom =
                view_port->clip_bottom > 0 ? view_port->clip_bottom : view_port->height;
            if( clip_left < 0 )
                clip_left = 0;
            if( clip_top < 0 )
                clip_top = 0;
            if( clip_right < clip_left )
                clip_right = clip_left;
            if( clip_bottom < clip_top )
                clip_bottom = clip_top;
            ctx->screen_width = clip_right - clip_left;
            ctx->screen_height = clip_bottom - clip_top;
            /* Projection origin = center of the (rebased) clip rect. The
             * perspective texture kernels anchor their uv basis at
             * screen_width/2, screen_height/2 with no way to shift it, so the
             * geometry has to use that same origin or every textured face is
             * skewed by the difference. Full-surface viewports (clip == the
             * whole target) are unaffected: there the clip center already is
             * width/2, height/2. */
            ctx->offset_x = ctx->screen_width >> 1;
            ctx->offset_y = ctx->screen_height >> 1;
        }
        ctx->near_plane_z = camera->near_plane_z;
        ctx->stride = view_port->stride ? view_port->stride : view_port->width;
        ctx->camera_cot16 = toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048);
        ctx->texture_map = &ToriDraw_SceneTexState(scene)->texture_map;
        ctx->cache_texture_id = -1;
        ctx->cache_texels = NULL;
        ctx->cache_texture_size = 0;
        ctx->cache_texture_opaque = 0;
        ctx->flags = 0;
        if( smooth )
            ctx->flags |= RASTER_FLAG_GOURAUD_SMOOTH;
        if( false )
            ctx->flags |= RASTER_FLAG_TEXTURE_AFFINE;
        ctx->allow_near_clip = ToriDraw_ModelHasTextures(hnd);
        ctx->near_clipped = scene->near_clipped;
        break;
    }
    default:
        break;
    }
}

static inline void
ToriDraw_RasterWithFaceIndices(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    struct ToriDrawModelRasterContext ctx;
    context_from_handle(scene, hnd, view_port, camera, smooth, &ctx);
    {
        int clip_left = view_port->clip_left > 0 ? view_port->clip_left : 0;
        int clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
        int stride = ctx.stride;
        ctx.pixel_buffer = pixel_buffer + clip_left + clip_top * stride;
    }

    if( (ctx.num_faces == 9012 || ctx.num_faces == 8509) &&
        ctx.camera_cot16 >= 150000 )
    {
        static bool reported_sort_fidelity_qbd;
        static bool reported_sort_fidelity_td;
        bool* const reported_sort_fidelity =
            ctx.num_faces == 9012 ? &reported_sort_fidelity_qbd
                                  : &reported_sort_fidelity_td;
        if( !*reported_sort_fidelity )
        {
            unsigned char* accepted = calloc((size_t)ctx.num_faces, 1);
            unsigned char* ordered = calloc((size_t)ctx.num_faces, 1);
            int accepted_count = 0;
            int accepted_invalid = 0;
            int ordered_invalid = 0;
            int ordered_duplicates = 0;
            int accepted_absent = 0;
            int first_duplicate = -1;
            int first_absent = -1;

            if( accepted && ordered )
            {
                for( int depth = 0; depth < scene->depth_levels; depth++ )
                {
                    int const count = scene->tmp_depth_face_count[depth];
                    faceint_t const* const faces =
                        &scene->tmp_depth_faces[depth * scene->depth_stride];
                    for( int j = 0; j < count; j++ )
                    {
                        int const face = faces[j];
                        if( face < 0 || face >= ctx.num_faces )
                        {
                            accepted_invalid++;
                            continue;
                        }
                        if( !accepted[face] )
                        {
                            accepted[face] = 1;
                            accepted_count++;
                        }
                    }
                }

                for( int i = 0; i < scene->tmp_face_order_count; i++ )
                {
                    int const face = scene->tmp_face_order[i];
                    if( face < 0 || face >= ctx.num_faces )
                    {
                        ordered_invalid++;
                        continue;
                    }
                    if( ordered[face] )
                    {
                        ordered_duplicates++;
                        if( first_duplicate < 0 )
                            first_duplicate = face;
                    }
                    ordered[face] = 1;
                }

                for( int face = 0; face < ctx.num_faces; face++ )
                {
                    if( accepted[face] && !ordered[face] )
                    {
                        accepted_absent++;
                        if( first_absent < 0 )
                            first_absent = face;
                    }
                }

                fprintf(
                    stderr,
                    "sort_fidelity_probe: model=%p accepted=%d ordered=%d "
                    "accepted_invalid=%d ordered_invalid=%d duplicates=%d "
                    "accepted_absent=%d first_duplicate=%d first_absent=%d\n",
                    (void*)hnd.u.model.model, accepted_count,
                    scene->tmp_face_order_count, accepted_invalid, ordered_invalid,
                    ordered_duplicates, accepted_absent,
                    first_duplicate, first_absent);
                *reported_sort_fidelity = true;
            }
            free(accepted);
            free(ordered);
        }
    }

    for( int i = 0; i < scene->tmp_face_order_count; i++ )
    {
        int face = scene->tmp_face_order[i];
        static const int probe_x[] = { 170, 190, 210, 230, 250, 180, 220, 260,
                                       175, 190, 210, 230 };
        static const int probe_y[] = {  60,  80, 100, 120, 140, 140,  60, 100,
                                       160, 180, 210, 200 };
        int before[sizeof(probe_x) / sizeof(probe_x[0])];
        bool const probe =
            ctx.screen_width == 1158 && ctx.screen_height == 800 &&
            ctx.camera_cot16 >= 150000;

        if( probe )
        {
            for( unsigned p = 0; p < sizeof(probe_x) / sizeof(probe_x[0]); p++ )
                before[p] = ctx.pixel_buffer[probe_y[p] * ctx.stride + probe_x[p]];
        }

        {
            int const a = ctx.face_indices_a[face];
            int const b = ctx.face_indices_b[face];
            int const c = ctx.face_indices_c[face];
            int tp = a;
            int tm = b;
            int tn = c;
            int texture_face = -1;
            int const texture_id =
                ctx.face_textures ? ctx.face_textures[face] : -1;

            if( texture_id != -1 && ctx.face_texture_coords &&
                ctx.face_texture_coords[face] != -1 )
            {
                texture_face = ctx.face_texture_coords[face];
                if( texture_face >= 0 && texture_face < ctx.num_textured_faces )
                {
                    tp = ctx.face_p_coordinate_nullable[texture_face];
                    tm = ctx.face_m_coordinate_nullable[texture_face];
                    tn = ctx.face_n_coordinate_nullable[texture_face];
                }
            }

            g_texture_face_probe_model = hnd.u.model.model;
            g_texture_face_probe_model_faces = ctx.num_faces;
            g_texture_face_probe_face = face;
            g_texture_face_probe_type = ctx.face_infos ? ctx.face_infos[face] : 0;
            g_texture_face_probe_texture = texture_id;
            g_texture_face_probe_texture_face = texture_face;
            g_texture_face_probe_alpha =
                ctx.face_alphas_nullable ? ctx.face_alphas_nullable[face] : 255;
            g_texture_face_probe_screen[0] = ctx.vertex_x[a] + ctx.offset_x;
            g_texture_face_probe_screen[1] = ctx.vertex_y[a] + ctx.offset_y;
            g_texture_face_probe_screen[2] = ctx.vertex_x[b] + ctx.offset_x;
            g_texture_face_probe_screen[3] = ctx.vertex_y[b] + ctx.offset_y;
            g_texture_face_probe_screen[4] = ctx.vertex_x[c] + ctx.offset_x;
            g_texture_face_probe_screen[5] = ctx.vertex_y[c] + ctx.offset_y;
            g_texture_face_probe_shade[0] = ctx.colors_a[face];
            g_texture_face_probe_shade[1] = ctx.colors_b[face];
            g_texture_face_probe_shade[2] = ctx.colors_c[face];
            if( ctx.orthographic_vertex_x_nullable )
            {
                g_texture_face_probe_basis[0] = ctx.orthographic_vertex_x_nullable[tp];
                g_texture_face_probe_basis[1] = ctx.orthographic_vertex_y_nullable[tp];
                g_texture_face_probe_basis[2] = ctx.orthographic_vertex_z_nullable[tp];
                g_texture_face_probe_basis[3] = ctx.orthographic_vertex_x_nullable[tm];
                g_texture_face_probe_basis[4] = ctx.orthographic_vertex_y_nullable[tm];
                g_texture_face_probe_basis[5] = ctx.orthographic_vertex_z_nullable[tm];
                g_texture_face_probe_basis[6] = ctx.orthographic_vertex_x_nullable[tn];
                g_texture_face_probe_basis[7] = ctx.orthographic_vertex_y_nullable[tn];
                g_texture_face_probe_basis[8] = ctx.orthographic_vertex_z_nullable[tn];
            }
        }
        ToriDraw_RasterModelFace(face, &ctx);
        g_texture_face_probe_face = -1;
        if( probe )
        {
            for( unsigned p = 0; p < sizeof(probe_x) / sizeof(probe_x[0]); p++ )
            {
                int const after =
                    ctx.pixel_buffer[probe_y[p] * ctx.stride + probe_x[p]];
                if( after == before[p] )
                    continue;

                int const a = ctx.face_indices_a[face];
                int const b = ctx.face_indices_b[face];
                int const c = ctx.face_indices_c[face];
                int const raw_type = ctx.face_infos ? ctx.face_infos[face] : 0;
                int const texture_id =
                    ctx.face_textures ? ctx.face_textures[face] : -1;
                int texture_face = -1;
                int tp = a;
                int tm = b;
                int tn = c;
                if( texture_id != -1 && ctx.face_texture_coords &&
                    ctx.face_texture_coords[face] != -1 )
                {
                    texture_face = ctx.face_texture_coords[face];
                    if( texture_face >= 0 && texture_face < ctx.num_textured_faces )
                    {
                        tp = ctx.face_p_coordinate_nullable[texture_face];
                        tm = ctx.face_m_coordinate_nullable[texture_face];
                        tn = ctx.face_n_coordinate_nullable[texture_face];
                    }
                }

                fprintf(
                    stderr,
                    "face_probe: sample=%d,%d before=%06x after=%06x "
                    "model=%p faces=%d vertices=%d order=%d/%d face=%d "
                    "raw_type=%d texture=%d texture_face=%d alpha=%d "
                    "abc=%d,%d,%d screen=%d,%d,%d,%d,%d,%d z=%d,%d,%d "
                    "tpmt=%d,%d,%d basis=%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                    probe_x[p], probe_y[p], before[p] & 0xFFFFFF, after & 0xFFFFFF,
                    (void*)hnd.u.model.model, ctx.num_faces, ctx.num_vertices,
                    i, scene->tmp_face_order_count, face,
                    raw_type, texture_id, texture_face,
                    ctx.face_alphas_nullable ? ctx.face_alphas_nullable[face] : 255,
                    a, b, c,
                    ctx.vertex_x[a], ctx.vertex_y[a],
                    ctx.vertex_x[b], ctx.vertex_y[b],
                    ctx.vertex_x[c], ctx.vertex_y[c],
                    ctx.vertex_z[a], ctx.vertex_z[b], ctx.vertex_z[c],
                    tp, tm, tn,
                    ctx.orthographic_vertex_x_nullable ? ctx.orthographic_vertex_x_nullable[tp] : 0,
                    ctx.orthographic_vertex_y_nullable ? ctx.orthographic_vertex_y_nullable[tp] : 0,
                    ctx.orthographic_vertex_z_nullable ? ctx.orthographic_vertex_z_nullable[tp] : 0,
                    ctx.orthographic_vertex_x_nullable ? ctx.orthographic_vertex_x_nullable[tm] : 0,
                    ctx.orthographic_vertex_y_nullable ? ctx.orthographic_vertex_y_nullable[tm] : 0,
                    ctx.orthographic_vertex_z_nullable ? ctx.orthographic_vertex_z_nullable[tm] : 0,
                    ctx.orthographic_vertex_x_nullable ? ctx.orthographic_vertex_x_nullable[tn] : 0,
                    ctx.orthographic_vertex_y_nullable ? ctx.orthographic_vertex_y_nullable[tn] : 0,
                    ctx.orthographic_vertex_z_nullable ? ctx.orthographic_vertex_z_nullable[tn] : 0);
            }
        }
    }
}

static inline void
ToriDraw_Raster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    {
        return ToriDraw_RasterWithFaceIndices(
            scene, hnd, view_port, camera, pixel_buffer, smooth);
    }
    default:
        assert(false && "Invalid model handle kind");
        return;
    }
}
