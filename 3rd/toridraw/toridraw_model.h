#ifndef TORIDRAW_MODEL_H
#define TORIDRAW_MODEL_H

#include "toridraw_animation.h"
#include "toridraw_types.h"
#include "toridraw_shared_model.h"

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

/**
 * The arrays that describe a model's FACES, as opposed to where its corners sit
 * or what colour they came out.
 *
 * Every one of these is a function of the loc config and the rotation alone: a
 * face names vertex slots, not positions, so contouring a placement to the
 * ground or relighting it from a neighbour moves the vertices underneath
 * without renumbering anything here. That is what makes the set loanable
 * between placements; see ToriDraw_Model::shared_faces.
 *
 * Kept as a list so the free path, the steal and the adopt cannot drift out of
 * step -- a field added to the model and forgotten here would be freed twice or
 * leaked, and neither shows up near its cause. It is TWO lists only because one
 * of the arrays is not loanable to every loc; see below.
 */
/**
 * The face arrays that are safe to LEND, which is all of them but one.
 *
 * face_infos is the exception and is deliberately absent: it is the array
 * World.shareLight writes, hiding the seam faces where two placements of a
 * sharelight loc meet. Lending it hid one segment's seam at every placement of
 * the loc -- a run of identical walls with the same faces missing, which reads
 * on screen as a wall you can see straight through from one side only.
 *
 * Both reference clients draw the line in the same place. The deob's
 * `Model(sharelight, 0, proto, hillskew)` and Client-TS's
 * `Model.hillSkewCopy(model, hillskew, sharelight)` give a sharelight placement
 * its OWN faceRenderType -- allocating and zeroing one even when the prototype
 * had none, precisely so the removal always has somewhere to write -- while
 * every other face array keeps pointing at the prototype. Neither client
 * disables the face removal; they make it safe by not sharing what it writes.
 *
 * Kept out of the loan unconditionally rather than per-loc, so the type says
 * the rule and no flag has to agree with it. It costs an int per face on a
 * borrowing placement that a per-loc test would have saved on the contoured
 * ones, and buys a shape in which the bug cannot be expressed.
 */
#define TORIDRAW_SHARED_FACE_FIELDS(X)                                                             \
    X(face_indices_a)                                                                              \
    X(face_indices_b)                                                                              \
    X(face_indices_c)                                                                              \
    X(face_colors)                                                                                 \
    X(face_textures)                                                                               \
    X(face_alphas)                                                                                 \
    X(face_priorities)                                                                             \
    X(textured_p_coordinate)                                                                       \
    X(textured_m_coordinate)                                                                       \
    X(textured_n_coordinate)                                                                       \
    X(texture_render_types)                                                                        \
    X(face_texture_coords)

/** Every face array a model has: the lendable set plus the one that is always
 *  its own. The free path walks this; the loan walks the set above. */
#define TORIDRAW_MODEL_FACE_FIELDS(X)                                                              \
    TORIDRAW_SHARED_FACE_FIELDS(X)                                                                 \
    X(face_infos)

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

/**
 * Free the model and its arrays -- or, for one on loan from a shared-model
 * store, drop this holder and leave the geometry to the others.
 */
void
ToriDraw_ModelFree(struct ToriDraw_Model* model);

/**
 * Abort if `model` is on loan from a shared-model store, for the in-place
 * mutators.
 *
 * A borrowed model is written through exactly one door
 * (ToriDraw_SceneElementModelForWrite, which takes a private copy first). Any
 * other writer is a bug that would otherwise show up as every placement of one
 * loc moving, fading or animating together, which is a long way from its
 * cause.
 */
/** Free every array a fully-owned model holds, leaving the shell. Exposed for
 *  the two shared types, whose `base` is embedded rather than allocated. */
void
ToriDraw_ModelFree_arrays(struct ToriDraw_Model* m);

/** Release a lent-faces placement: its own arrays, then its share of the loan. */
void
ToriDraw_ModelLentFacesFree(struct ToriDraw_ModelLentFaces* lent);

/**
 * Release whatever this handle addresses, by its kind.
 *
 * The one free that callers holding a handle should use: an owned model frees
 * outright, a shared one drops a holder, a lent-faces one drops its arrays and
 * its share of the loan. Getting that dispatch wrong used to be possible --
 * every regime was the same type and ToriDraw_ModelFree guessed from two
 * nullable fields.
 */
static inline void
ToriDraw_ModelHandleFree(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_NONE:
        return;
    case TORIDRAWMK_MODEL_SHARED:
        ToriDraw_SharedModelRelease(hnd.u.shared);
        return;
    case TORIDRAWMK_MODEL_LENT_FACES:
        ToriDraw_ModelLentFacesFree(hnd.u.lent);
        return;
    default:
        ToriDraw_ModelFree(hnd.u.model.model);
        return;
    }
}

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
/** The long-standing spelling of ToriDraw_ModelRead, kept because two dozen
 *  render paths use it and every one of them only reads. */
static inline const struct ToriDraw_Model*
ToriDraw_ModelAsFull(struct ToriDraw_ModelHandle hnd)
{
    return ToriDraw_ModelRead(hnd);
}

/**
 * The model, writable, because this handle says the caller owns all of it.
 *
 * The ONLY way to get a non-const `struct ToriDraw_Model*` out of a handle, and
 * it refuses every kind that does not own its geometry outright. That is what
 * makes the bare type mean something: hold one and it is yours.
 */
static inline struct ToriDraw_Model*
ToriDraw_ModelWrite(struct ToriDraw_ModelHandle hnd)
{
    assert(
        (hnd.kind == TORIDRAWMK_MODEL || hnd.kind == TORIDRAWMK_MODEL_HD) &&
        "write to geometry this handle does not own");
    return hnd.u.model.model;
}

/**
 * The private half of a lent-faces placement: its vertices, its per-corner
 * colours, its face_infos.
 *
 * Returns the whole base because C cannot hand back "every member but twelve",
 * so this is a promise the caller keeps rather than one the compiler enforces:
 * do not touch TORIDRAW_SHARED_FACE_FIELDS through it. What it does enforce is
 * the kind -- a whole-shared model has no private half and does not come back
 * from here at all.
 *
 * This is what contouring, the End-batch lighting and the World.shareLight seam
 * hide are entitled to.
 */
static inline struct ToriDraw_Model*
ToriDraw_ModelLentFacesPrivate(struct ToriDraw_ModelHandle hnd)
{
    assert(hnd.kind == TORIDRAWMK_MODEL_LENT_FACES);
    return &hnd.u.lent->base;
}

/** A handle onto a model that owns itself. */
static inline struct ToriDraw_ModelHandle
ToriDraw_ModelHandleOwned(struct ToriDraw_Model* model)
{
    struct ToriDraw_ModelHandle hnd = { 0 };

    assert(model);
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    return hnd;
}

/** A handle onto a model the scene shares whole. */
static inline struct ToriDraw_ModelHandle
ToriDraw_ModelHandleShared(struct ToriDraw_SharedModel* shared)
{
    struct ToriDraw_ModelHandle hnd = { 0 };

    assert(shared);
    hnd.kind = TORIDRAWMK_MODEL_SHARED;
    hnd.u.shared = shared;
    return hnd;
}

/** A handle onto a placement that borrows its face arrays. */
static inline struct ToriDraw_ModelHandle
ToriDraw_ModelHandleLentFaces(struct ToriDraw_ModelLentFaces* lent)
{
    struct ToriDraw_ModelHandle hnd = { 0 };

    assert(lent);
    hnd.kind = TORIDRAWMK_MODEL_LENT_FACES;
    hnd.u.lent = lent;
    return hnd;
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
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        if( !hnd.u.model.model || !hnd.u.model.model->has_bounds_cylinder )
            return NULL;
        return &hnd.u.model.model->bounds_cylinder;
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
    if( !texture->borrowed_texels )
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

/** Ints ToriDraw_TextureMapAnimate's scratch must hold: the largest texture
 *  it will rotate, and 128x128 is the largest the cache carries. */
#define TORIDRAW_TEXTURE_ANIM_SCRATCH_INTS (128 * 128)

/**
 * Advance every scrolling texture in the map by `cycles`.
 *
 * `scratch` is the caller's, and must hold TORIDRAW_TEXTURE_ANIM_SCRATCH_INTS
 * ints -- 64 KB. It used to be a function-local static, which meant every
 * client that linked ToriDraw reserved that 64 KB whether or not it had a
 * single scrolling texture, and an embedded client that never animates one
 * paid a quarter of a 250 KB budget for a buffer it could not reach. A texture
 * larger than the scratch is skipped rather than truncated.
 */
void
ToriDraw_TextureMapAnimate(
    struct ToriDraw_TextureMap* map,
    int cycles,
    int* scratch,
    int scratch_ints);

static inline bool
ToriDraw_ModelHasTextures(struct ToriDraw_ModelHandle hnd)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
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
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
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
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
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
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
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
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
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
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        return hnd.u.model.model->vertices_z;
    default:
        return NULL;
    }
}

#endif
