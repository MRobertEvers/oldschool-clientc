#include "toridraw_model.h"

#include "osrs/palette.h"
#include "toridraw_lighting.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_Normals*
toridraw_normals_new(
    int vertex_count,
    int face_count)
{
    struct ToriDraw_Normals* normals = malloc(sizeof(struct ToriDraw_Normals));
    memset(normals, 0, sizeof(struct ToriDraw_Normals));
    normals->vertex_normals = malloc(sizeof(struct ToriDraw_Normal) * (size_t)vertex_count);
    memset(normals->vertex_normals, 0, sizeof(struct ToriDraw_Normal) * (size_t)vertex_count);
    normals->vertex_normals_count = vertex_count;
    if( face_count > 0 )
    {
        normals->face_normals = malloc(sizeof(struct ToriDraw_Normal) * (size_t)face_count);
        memset(normals->face_normals, 0, sizeof(struct ToriDraw_Normal) * (size_t)face_count);
        normals->face_normals_count = face_count;
    }
    return normals;
}

void
toridraw_normals_free(struct ToriDraw_Normals* normals)
{
    if( !normals )
        return;
    free(normals->vertex_normals);
    free(normals->face_normals);
    free(normals);
}

void
toridraw_bones_free(struct ToriDraw_Bones* bones)
{
    if( !bones )
        return;
    if( bones->bones )
    {
        for( int i = 0; i < bones->bones_count; i++ )
            free(bones->bones[i]);
        free(bones->bones);
    }
    free(bones->bones_sizes);
    free(bones);
}

static void
toridraw_model_free_arrays(struct ToriDraw_Model* m)
{
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    free(m->face_colors_a);
    free(m->face_colors_b);
    free(m->face_colors_c);
    free(m->face_indices_a);
    free(m->face_indices_b);
    free(m->face_indices_c);
    free(m->face_textures);
    free(m->original_vertices_x);
    free(m->original_vertices_y);
    free(m->original_vertices_z);
    free(m->face_alphas);
    free(m->original_face_alphas);
    free(m->face_infos);
    free(m->face_priorities);
    free(m->face_colors);
    free(m->textured_p_coordinate);
    free(m->textured_m_coordinate);
    free(m->textured_n_coordinate);
    free(m->face_texture_coords);
    toridraw_normals_free(m->normals);
    toridraw_normals_free(m->merged_normals);
    toridraw_bones_free(m->vertex_bones);
    toridraw_bones_free(m->face_bones);
    free(m->bounds_cylinder);
}

void
toridraw_model_free(struct ToriDraw_Model* model)
{
    if( !model )
        return;
    toridraw_model_free_arrays(model);
    free(model);
}

static int
toridraw_model_needs_face_normals(struct ToriDraw_Model* model)
{
    const int* fi = model->face_infos;
    if( !fi )
        return 0;
    int fc = model->face_count;
    for( int i = 0; i < fc; i++ )
    {
        if( (fi[i] & 0x3) == 1 )
            return 1;
    }
    return 0;
}

void
toridraw_model_alloc_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->normals )
        return;
    int face_n = toridraw_model_needs_face_normals(model) ? model->face_count : 0;
    model->normals = toridraw_normals_new(model->vertex_count, face_n);
}

void
toridraw_model_alloc_merged_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->merged_normals )
        return;
    model->merged_normals = toridraw_normals_new(model->vertex_count, 0);
}

void
toridraw_model_calculate_vertex_normals(struct ToriDraw_Model* model)
{
    assert(model);
    toridraw_model_alloc_normals(model);
    struct ToriDraw_Normals* nm = model->normals;
    assert(nm);

    int vc = model->vertex_count;
    int fc = model->face_count;
    toridraw_calculate_vertex_normals(
        nm->vertex_normals,
        nm->face_normals,
        vc,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        fc);

    struct ToriDraw_Normals* mm = model->merged_normals;
    if( mm && mm->vertex_normals )
        memcpy(mm->vertex_normals, nm->vertex_normals, sizeof(struct ToriDraw_Normal) * (size_t)vc);
}

void
toridraw_model_free_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->normals )
        return;
    toridraw_normals_free(model->normals);
    toridraw_normals_free(model->merged_normals);
    model->normals = NULL;
    model->merged_normals = NULL;
}

void
toridraw_context_set_texture(
    struct ToriDraw_Context* ctx,
    int id,
    struct ToriDraw_Texture* texture)
{
    if( !ctx || id < 0 || id >= 256 )
        return;

    struct ToriDraw_TextureMap* map = &ctx->texture_map;
    struct ToriDraw_Texture* const old = map->textures[id];

    if( old == texture )
        return;

    if( old )
    {
        toridraw_eventqueue_push(&ctx->events, TORIDRAW_EVENT_TEX_UNLOAD, id, NULL);
        toridraw_texture_free(old);
        map->textures[id] = NULL;
    }

    if( texture )
    {
        map->textures[id] = texture;
        toridraw_eventqueue_push(&ctx->events, TORIDRAW_EVENT_TEX_LOAD, id, texture);
        if( id >= map->count )
            map->count = id + 1;
    }
    else if( id == map->count - 1 )
    {
        while( map->count > 0 && !map->textures[map->count - 1] )
            map->count--;
    }
}

int
toridraw_texture_average_hsl16(const struct ToriDraw_Texture* texture)
{
    if( !texture || !texture->texels || texture->width <= 0 || texture->height <= 0 )
        return 0;

    int red = 0;
    int green = 0;
    int blue = 0;
    int colour_count = texture->width * texture->height;
    for( int i = 0; i < colour_count; i++ )
    {
        int pixel = texture->texels[i];
        red += (pixel >> 16) & 0xff;
        green += (pixel >> 8) & 0xff;
        blue += pixel & 0xff;
    }

    int average_rgb =
        ((red / colour_count) << 16) + ((green / colour_count) << 8) + (blue / colour_count);
    return palette_rgb_to_hsl16(average_rgb);
}
