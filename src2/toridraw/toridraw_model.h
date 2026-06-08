#ifndef TORIDRAW_MODEL_H
#define TORIDRAW_MODEL_H

#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct ToriDraw_Normals*
toridraw_normals_new(
    int vertex_count,
    int face_count);

void
toridraw_normals_free(struct ToriDraw_Normals* normals);

void
toridraw_bones_free(struct ToriDraw_Bones* bones);

void
toridraw_model_alloc_normals(struct ToriDraw_Model* model);

void
toridraw_model_alloc_merged_normals(struct ToriDraw_Model* model);

void
toridraw_model_calculate_vertex_normals(struct ToriDraw_Model* model);

void
toridraw_model_free_normals(struct ToriDraw_Model* model);

void
toridraw_model_free(struct ToriDraw_Model* model);

static inline bool
toridraw_model_is_lightable(const struct ToriDraw_Model* model)
{
    return model && model->face_count > 0 && model->vertices_x && model->vertices_y &&
           model->vertices_z && model->face_colors_a && model->face_colors_b &&
           model->face_colors_c;
}

static inline struct ToriDraw_Model*
toridraw_model_as_full(struct ToriDraw_ModelHandle hnd)
{
    assert(hnd.kind == TORIDRAWMK_MODEL);
    return hnd.u.model.model;
}

static inline struct ToriDraw_BoundsCylinder*
toridraw_model_get_bounds_cylinder(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->bounds_cylinder;
    default:
        return NULL;
    }
}

static inline int
toridraw_model_get_face_priority(
    const uint8_t* packed,
    int index)
{
    uint8_t byte = packed[index >> 1];
    return (index & 1) ? (int)(byte >> 4) : (int)(byte & 0x0Fu);
}

static inline void
toridraw_texture_free(struct ToriDraw_Texture* texture)
{
    if( !texture )
        return;
    free(texture->texels);
    free(texture);
}

static inline void
toridraw_texturemap_set(
    struct ToriDraw_TextureMap* map,
    int id,
    struct ToriDraw_Texture* texture)
{
    if( !map || id < 0 || id >= 256 )
        return;
    if( map->textures[id] )
        toridraw_texture_free(map->textures[id]);
    map->textures[id] = texture;
    if( texture && id >= map->count )
        map->count = id + 1;
}

static inline struct ToriDraw_Texture*
toridraw_texturemap_get(
    const struct ToriDraw_TextureMap* map,
    int id)
{
    assert(map && id >= 0 && id < 256 && "Invalid texture ID");
    return map->textures[id];
}

void
toridraw_context_set_texture(
    struct ToriDraw_Context* ctx,
    int id,
    struct ToriDraw_Texture* texture);

int
toridraw_texture_average_hsl16(const struct ToriDraw_Texture* texture);

static inline bool
toridraw_model_has_textures(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->textured_face_count > 0;
    default:
        return false;
    }
}

static inline int
toridraw_model_get_face_count(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->face_count;
    default:
        return 0;
    }
}

static inline int
toridraw_model_get_vertex_count(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->vertex_count;
    default:
        return 0;
    }
}

static inline vertexint_t*
toridraw_model_get_vertices_x(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->vertices_x;
    default:
        return NULL;
    }
}

static inline vertexint_t*
toridraw_model_get_vertices_y(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->vertices_y;
    default:
        return NULL;
    }
}

static inline vertexint_t*
toridraw_model_get_vertices_z(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
        return hnd.u.model.model->vertices_z;
    default:
        return NULL;
    }
}

#endif
