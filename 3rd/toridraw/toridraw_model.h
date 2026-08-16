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
    assert(src);
    assert(count > 0);
    void* dst = malloc(count * elem_size);
    assert(dst);
    memcpy(dst, src, count * elem_size);
    return dst;
}

/* Whether an optional array (face_alphas, face_textures, ...) is present is
 * decided HERE, once, rather than inside the copy: a model carries a face_count
 * for arrays it does not have. Absent stays absent; present is copied, and a
 * failed copy asserts. */
#define TORIDRAW_MODEL_COPY(model, field, src, count)                                               \
    ((model)->field = ((src) && (count) > 0)                                                        \
                          ? (typeof((model)->field))ToriDraw_BufCopy(                               \
                                (src), (size_t)(count), sizeof(*(model)->field))                    \
                          : NULL)

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
    assert(model);
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

/* Walkmerge blend (reference Model.maskAnimate): `walkmerge` is the ascending
 * 9999999-terminated list of transform groups the SECONDARY frame drives; the
 * PRIMARY frame drives every other group. ORIGIN ops apply in both passes.
 * Falls back to a plain primary apply when walkmerge/secondary are absent.
 * Both frames must share `base` (same rig). */
void
ToriDraw_ModelAnimateFrameMasked(
    struct ToriDraw_Model* model,
    const struct ToriDraw_AnimBase* base,
    const struct ToriDraw_AnimFrame* primary,
    const struct ToriDraw_AnimFrame* secondary,
    const int* walkmerge);

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

/**
 * True for both model kinds that carry a full ToriDraw_Model.
 *
 * The scene element system is deliberately NOT updated to accept
 * TORIDRAWMK_MODEL_HD: an HD model is not placed in a world scene today, and
 * widening those tests would claim support that has not been built. This
 * predicate marks the paths that genuinely handle both.
 */
static inline int
ToriDraw_ModelKindIsFull(enum ToriDraw_ModelKind kind)
{
    return kind == TORIDRAWMK_MODEL || kind == TORIDRAWMK_MODEL_HD;
}

static inline struct ToriDraw_Model*
ToriDraw_ModelAsFull(struct ToriDraw_ModelHandle hnd)
{
    /* An HD model's `base` is the first member and is a plain model, so both
     * kinds answer this the same way and nothing downstream has to care. */
    assert(hnd.kind == TORIDRAWMK_MODEL || hnd.kind == TORIDRAWMK_MODEL_HD);
    return hnd.u.model.model;
}

/**
 * The HD tail, or NULL when this handle is not an HD model.
 *
 * Returning NULL rather than asserting is deliberate: the HD render path is
 * meant to accept any model and fall back to the plain kernels for one that
 * carries no mapping, so "not HD" is an ordinary answer and not a caller error.
 */
static inline struct ToriDraw_ModelHD*
ToriDraw_ModelAsHD(struct ToriDraw_ModelHandle hnd)
{
    if( hnd.kind != TORIDRAWMK_MODEL_HD )
        return NULL;
    return (struct ToriDraw_ModelHD*)hnd.u.model.model;
}

/**
 * Release an HD model: its mapping tail, then the base model's arrays.
 *
 * The base is embedded by value, so this frees what ToriDraw_ModelFree frees
 * plus the tail. Calling ToriDraw_ModelFree on an HD model instead leaks the
 * mappings, which is why this exists as its own entry point.
 */
/**
 * Promote a plain model to the HD variant, taking ownership.
 *
 * The base is embedded by value, so the arrays move across untouched and only
 * the original shell is released — `model` is invalid on return whether this
 * succeeds or not. The mappings are still NULL; build them with
 * ToriDraw_ModelBuildTextureMappings while the model is in its bind pose.
 */
struct ToriDraw_ModelHD*
ToriDraw_ModelHDFromModel(struct ToriDraw_Model* model);

void
ToriDraw_ModelHDFree(struct ToriDraw_ModelHD* hd);

/** Wrap an HD model as a handle. */
static inline struct ToriDraw_ModelHandle
ToriDraw_ModelHandleFromHD(struct ToriDraw_ModelHD* hd)
{
    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL_HD;
    hnd.u.model.model = hd ? &hd->base : NULL;
    return hnd;
}

static inline struct ToriDraw_BoundsCylinder*
ToriDraw_ModelGetBoundsCylinder(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
        if( !hnd.u.model.model )
            return NULL;
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
    assert(map);
    if( id < 0 || id >= TORIDRAW_TEXTURE_ID_CAPACITY )
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
    assert(map && id >= 0 && id < TORIDRAW_TEXTURE_ID_CAPACITY && "Invalid texture ID");
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
    case TORIDRAWMK_MODEL_HD:
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
    case TORIDRAWMK_MODEL_HD:
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
    case TORIDRAWMK_MODEL_HD:
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
    case TORIDRAWMK_MODEL_HD:
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
    case TORIDRAWMK_MODEL_HD:
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
    case TORIDRAWMK_MODEL_HD:
        return hnd.u.model.model->vertices_z;
    default:
        return NULL;
    }
}

#endif
