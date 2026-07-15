#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

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
    int camera_fov;
    struct ToriDraw_TextureMap* texture_map;
    int flags;
    bool allow_near_clip;
};

static inline void
ToriDraw_RasterModelFace(
    int face,
    struct ToriDrawModelRasterContext* ctx)
{
    assert(face >= 0 && face < ctx->num_faces);

    struct ToriDraw_Texture* texture = NULL;

    // TODO: FaceTYpe is wrong, type 2 is hidden, 3 is black, 0 is gouraud, 1 is flat.
    enum FaceType type = ctx->face_infos ? (ctx->face_infos[face] & 0x3) : FACE_TYPE_GOURAUD;
    if( type == 2 )
    {
        return;
    }
    assert(type >= 0 && type <= 3);

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
        // gamma 0.8 is the default in os1
        texture = ToriDraw_TextureMapGet(ctx->texture_map, texture_id);
        assert(texture != NULL);

        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;

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
                    ctx->allow_near_clip);
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
                    ctx->allow_near_clip);
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
                ctx->allow_near_clip);

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
                    ctx->camera_fov,
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
                    ctx->allow_near_clip);
            }
            else if( texture_opaque )
            {
                ToriDraw_TriangleFaceTextureBlendOpaque(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_fov,
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
                    ctx->allow_near_clip);
            }
            else
            {
                ToriDraw_TriangleFaceTextureBlendTransparent(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_fov,
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
                    ctx->allow_near_clip);
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
                    ctx->camera_fov,
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
                    ctx->allow_near_clip);
            }
            else if( texture_opaque )
            {
                ToriDraw_TriangleFaceTextureFlatOpaque(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_fov,
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
                    ctx->allow_near_clip);
            }
            else
            {
                ToriDraw_TriangleFaceTextureFlatTransparent(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_fov,
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
                    ctx->allow_near_clip);
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
            ctx->offset_x = (view_port->width >> 1) - clip_left;
            ctx->offset_y = (view_port->height >> 1) - clip_top;
            ctx->screen_width = clip_right - clip_left;
            ctx->screen_height = clip_bottom - clip_top;
        }
        ctx->near_plane_z = camera->near_plane_z;
        ctx->stride = view_port->stride ? view_port->stride : view_port->width;
        ctx->camera_fov = camera->fov_rpi2048;
        ctx->texture_map = &ToriDraw_SceneTexState(scene)->texture_map;
        ctx->flags = 0;
        if( smooth )
            ctx->flags |= RASTER_FLAG_GOURAUD_SMOOTH;
        if( false )
            ctx->flags |= RASTER_FLAG_TEXTURE_AFFINE;
        ctx->allow_near_clip = ToriDraw_ModelHasTextures(hnd);
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

    for( int i = 0; i < scene->tmp_face_order_count; i++ )
    {
        int face = scene->tmp_face_order[i];
        ToriDraw_RasterModelFace(face, &ctx);
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
