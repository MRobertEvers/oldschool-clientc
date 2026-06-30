#ifndef TORIDRAW_MODEL_H
#define TORIDRAW_MODEL_H

#include "toridraw_animation.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline void*
ToriDraw_BufCopy(
    const void* src,
    size_t count,
    size_t elem_size)
{
    if( !src || count == 0 )
        return NULL;
    void* dst = malloc(count * elem_size);
    if( dst )
        memcpy(dst, src, count * elem_size);
    return dst;
}

#define TORIDRAW_MODEL_COPY(model, field, src, count)                                               \
    ((model)->field = (typeof((model)->field))ToriDraw_BufCopy(                                     \
        (src), (size_t)(count), sizeof(*(model)->field)))

#define TORIDRAW_MODEL_MOVE(model, field, src)                                                     \
    do                                                                                             \
    {                                                                                              \
        (model)->field = (src);                                                                    \
        (src) = NULL;                                                                              \
    } while( 0 )

static inline struct ToriDraw_Model*
ToriDraw_ModelNew(
    int vertex_count,
    int face_count,
    uint8_t flags)
{
    struct ToriDraw_Model* model = calloc(1, sizeof(struct ToriDraw_Model));
    if( !model )
        return NULL;
    model->flags = flags;
    model->vertex_count = vertex_count;
    model->face_count = face_count;
    return model;
}

struct ToriDraw_Normals*
ToriDraw_NormalsNew(
    int vertex_count,
    int face_count);

void
ToriDraw_NormalsFree(struct ToriDraw_Normals* normals);

void
ToriDraw_BonesFree(struct ToriDraw_Bones* bones);

struct ToriDraw_Bones*
ToriDraw_BonesCopy(const struct ToriDraw_Bones* src);

void
ToriDraw_ModelAllocNormals(struct ToriDraw_Model* model);

void
ToriDraw_ModelAllocMergedNormals(struct ToriDraw_Model* model);

void
ToriDraw_ModelCalculateVertexNormals(struct ToriDraw_Model* model);

void
ToriDraw_ModelFreeNormals(struct ToriDraw_Model* model);

void
ToriDraw_ModelFree(struct ToriDraw_Model* model);

void
ToriDraw_ModelAssertPnmTextureInvariant(struct ToriDraw_Model const* model);

void
ToriDraw_ModelCaptureOriginalVertices(struct ToriDraw_Model* model);

void
ToriDraw_ModelAnimateReset(struct ToriDraw_Model* model);

void
ToriDraw_ModelAnimateFrame(
    struct ToriDraw_Model* model,
    const struct ToriDraw_AnimBase* base,
    const struct ToriDraw_AnimFrame* frame);

struct ToriDraw_SkeletalAnim;

void
ToriDraw_ModelAnimateSkeletal(
    struct ToriDraw_Model* model,
    const struct ToriDraw_SkeletalAnim* skeletal,
    int frame_index);

static inline bool
ToriDraw_ModelIsLightable(const struct ToriDraw_Model* model)
{
    return model && model->face_count > 0 && model->vertices_x && model->vertices_y &&
           model->vertices_z && model->face_colors_a && model->face_colors_b &&
           model->face_colors_c;
}

static inline struct ToriDraw_Model*
ToriDraw_ModelAsFull(struct ToriDraw_ModelHandle hnd)
{
    assert(hnd.kind == TORIDRAWMK_MODEL);
    return hnd.u.model.model;
}

static inline struct ToriDraw_BoundsCylinder*
ToriDraw_ModelGetBoundsCylinder(struct ToriDraw_ModelHandle hnd)
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
ToriDraw_ModelGetFacePriority(
    const uint8_t* packed,
    int index)
{
    uint8_t byte = packed[index >> 1];
    return (index & 1) ? (int)(byte >> 4) : (int)(byte & 0x0Fu);
}

static inline void
ToriDraw_TextureFree(struct ToriDraw_Texture* texture)
{
    if( !texture )
        return;
    free(texture->texels);
    free(texture);
}

static inline void
ToriDraw_TextureMapSet(
    struct ToriDraw_TextureMap* map,
    int id,
    struct ToriDraw_Texture* texture)
{
    if( !map || id < 0 || id >= 256 )
        return;
    if( map->textures[id] )
        ToriDraw_TextureFree(map->textures[id]);
    map->textures[id] = texture;
    if( texture && id >= map->count )
        map->count = id + 1;
}

static inline struct ToriDraw_Texture*
ToriDraw_TextureMapGet(
    const struct ToriDraw_TextureMap* map,
    int id)
{
    assert(map && id >= 0 && id < 256 && "Invalid texture ID");
    return map->textures[id];
}

int
ToriDraw_TextureAverageHsl16(const struct ToriDraw_Texture* texture);

void
ToriDraw_TextureAnimate(
    struct ToriDraw_Texture* tex,
    int cycles,
    int* scratch);

void
ToriDraw_TextureMapAnimate(
    struct ToriDraw_TextureMap* map,
    int cycles);

static inline bool
ToriDraw_ModelHasTextures(struct ToriDraw_ModelHandle hnd)
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
ToriDraw_ModelGetFaceCount(struct ToriDraw_ModelHandle hnd)
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
ToriDraw_ModelGetVertexCount(struct ToriDraw_ModelHandle hnd)
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
ToriDraw_ModelGetVerticesX(struct ToriDraw_ModelHandle hnd)
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
ToriDraw_ModelGetVerticesY(struct ToriDraw_ModelHandle hnd)
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
ToriDraw_ModelGetVerticesZ(struct ToriDraw_ModelHandle hnd)
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
