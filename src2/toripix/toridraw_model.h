#ifndef TORIDRAW_MODEL_H
#define TORIDRAW_MODEL_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

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

static inline struct ToriDraw_Texture*
toridraw_texturemap_get(
    const struct ToriDraw_TextureMap* map,
    int id)
{
    if( !map || id < 0 || id >= 256 )
        return NULL;
    return map->textures[id];
}

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
