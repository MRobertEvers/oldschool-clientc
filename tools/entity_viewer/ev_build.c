#include "ev_build.h"

/* The obj and loc records, which asset_access.h has no loader for. */
#include "ev_config.h"

#include "engine/toridraw_animation_from_rscache.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_types.h"

#include "toridraw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * "Can the client be given pixels for this texture id?"
 *
 * Defaults to no, which is the pre-texture behaviour: every textured face falls
 * back to its flat colour. The server installs a predicate that answers out of
 * the cache's baked texture set.
 */
static int (*g_texture_available_fn)(int id, void* user) = NULL;
static void* g_texture_available_user = NULL;

void
ev_build_set_texture_available(int (*fn)(int, void*), void* user)
{
    g_texture_available_fn = fn;
    g_texture_available_user = user;
}

static int
ev_build_texture_available(int id)
{
    if( id < 0 )
        return 0;
    if( !g_texture_available_fn )
        return 0;
    return g_texture_available_fn(id, g_texture_available_user);
}


/*
 * The per-textured-face mapping parameters, accumulated across a merge.
 *
 * ToriDraw_ModelMerge concatenates textured faces in part order, so these are
 * built the same way: part 0's parameters, then part 1's, and so on. Any other
 * order would pair a face with another face's projection — which does not fail,
 * it draws the wrong texture orientation and looks like a bad decode.
 *
 * They cannot be read back off the merged ToriDraw_Model because they are not
 * on it: only the RSCache_Model carries them, and it is freed per part.
 */
struct EV_TexParams
{
    int32_t* scale_x;
    int32_t* scale_y;
    int32_t* scale_z;
    int8_t* rotation;
    int8_t* direction;
    int8_t* speed;
    int8_t* trans_u;
    int8_t* trans_v;
    int count;
    int capacity;
    /** Cleared when a part turns out to carry no mapping data, so a partial
     *  set is never handed to the mapping builder. */
    bool valid;
};

static void
tex_params_free(struct EV_TexParams* p)
{
    if( !p )
        return;
    free(p->scale_x);
    free(p->scale_y);
    free(p->scale_z);
    free(p->rotation);
    free(p->direction);
    free(p->speed);
    free(p->trans_u);
    free(p->trans_v);
    memset(p, 0, sizeof(*p));
}

static bool
tex_params_reserve(struct EV_TexParams* p, int want)
{
    if( want <= p->capacity )
        return true;
    int cap = p->capacity ? p->capacity : 32;
    while( cap < want )
        cap *= 2;

#define EV_GROW(field, type)                                                   \
    do                                                                         \
    {                                                                          \
        type* grown = (type*)realloc(p->field, (size_t)cap * sizeof(type));    \
        assert(grown);                                                         \
        p->field = grown;                                                      \
    } while( 0 )

    EV_GROW(scale_x, int32_t);
    EV_GROW(scale_y, int32_t);
    EV_GROW(scale_z, int32_t);
    EV_GROW(rotation, int8_t);
    EV_GROW(direction, int8_t);
    EV_GROW(speed, int8_t);
    EV_GROW(trans_u, int8_t);
    EV_GROW(trans_v, int8_t);
#undef EV_GROW

    p->capacity = cap;
    return true;
}

/**
 * Append one part's mapping parameters.
 *
 * A part with textured faces but no parameter arrays still has to contribute
 * `n` entries, or every later part's parameters shift onto the wrong faces.
 * Zeros are the right filler: they are what a plain plane-projected face reads
 * as, and plane projection ignores all of these.
 */
static void
tex_params_append(struct EV_TexParams* p, const struct RSCache_Model* rs)
{
    int n = rs ? rs->textured_face_count : 0;
    if( n <= 0 )
        return;
    if( !tex_params_reserve(p, p->count + n) )
    {
        p->valid = false;
        return;
    }

    for( int i = 0; i < n; i++ )
    {
        int at = p->count + i;
        p->scale_x[at] = rs->texture_scale_x ? rs->texture_scale_x[i] : 0;
        p->scale_y[at] = rs->texture_scale_y ? rs->texture_scale_y[i] : 0;
        p->scale_z[at] = rs->texture_scale_z ? rs->texture_scale_z[i] : 0;
        p->rotation[at] = rs->texture_rotation ? rs->texture_rotation[i] : 0;
        p->direction[at] = rs->texture_direction ? rs->texture_direction[i] : 0;
        p->speed[at] = rs->texture_speed ? rs->texture_speed[i] : 0;
        p->trans_u[at] = rs->texture_trans_u ? rs->texture_trans_u[i] : 0;
        p->trans_v[at] = rs->texture_trans_v ? rs->texture_trans_v[i] : 0;
    }
    p->count += n;

    /* Anything beyond plane projection needs real parameters; a model that has
     * such faces and no arrays cannot be mapped and must not pretend. */
    if( rs->texture_render_types && !rs->texture_scale_x )
        for( int i = 0; i < n; i++ )
            if( rs->texture_render_types[i] != 0 )
            {
                p->valid = false;
                break;
            }
}

/* ---- RSCache_Model -> ToriDraw_Model ------------------------------------ */

/*
 * There is no conversion here on purpose.
 *
 * This file used to hand-roll RSCache_Model -> ToriDraw_Model, and it got the
 * details wrong in ways that only showed up as rendering artefacts: face
 * priority is one byte per face in the cache and two 4-bit fields per byte in
 * the renderer, so copying it verbatim gave every face some other face's
 * priority, the painter's sort ran on nonsense and models drew with holes in
 * them. Nothing about that was visible in the code — the copy looked right and
 * even carried a comment saying it was.
 *
 * The client already owns this conversion in two steps that the whole game
 * renders through, so the viewer uses them:
 *
 *   ToriRS_ModelFromRSCache   src/engine/torirs_model_from_rscache.c
 *   ToriDraw_ModelFromToriRS  src/engine/toridraw_model_from_torirs.c
 *
 * They also carry the parts this file never had: the animaya skin, the
 * bounds cylinder, and the PnM texture invariant assert.
 */

/*
 * `ToriDraw_ModelDropNonSdTextures` is the one function in that pair the viewer
 * does not call, and it is the only reason a CacheProvider is named at all.
 * The viewer has no material table or texture pixels; ev_build_npc_model strips
 * texture selectors before lighting so those faces fall back to their source
 * colours instead of disappearing in the raster's missing-texture path.
 */
bool
CacheProvider_TextureIsSd(
    struct CacheProvider* provider,
    int texture_id)
{
    (void)provider;
    (void)texture_id;
    return true;
}

/* ---- a list of model ids -> one merged model ----------------------------- */

/*
 * Every subject in this file is "a list of model ids that make one mesh": an
 * npc's models, a loc's models for one shape, an obj's two chathead halves. The
 * load, the two-step conversion and the merge are the same for all three, and
 * they have to run in list order or `out_params` pairs each mapping with
 * another face's projection (see EV_TexParams).
 *
 * `subject`/`subject_id` only name the thing in the debug output — the merge is
 * the same work whatever asked for it.
 *
 * Ids below zero are skipped: an obj's absent wear slot and a loc's empty model
 * entry are both -1. Returns NULL when nothing decoded.
 */
static struct ToriDraw_Model*
merge_model_ids(
    struct Tool_Dat2Cache* c,
    const char* subject,
    int subject_id,
    const int* model_ids,
    int model_count,
    struct EV_TexParams* out_params)
{
    assert(c);
    assert(subject);
    if( model_count <= 0 )
        return NULL;
    assert(model_ids);

    struct ToriDraw_Model** parts = calloc((size_t)model_count, sizeof(*parts));
    assert(parts);

    int part_count = 0;
    for( int i = 0; i < model_count; i++ )
    {
        if( model_ids[i] < 0 )
            continue;
        struct RSCache_Model* rs = tool_dat2_model_load(c, model_ids[i]);
        if( !rs )
            continue;

        /* Before the conversion: ToriRS_ModelFromRSCache moves the arrays out
         * and leaves `rs` hollow, and the mapping parameters go with them. */
        if( out_params )
            tex_params_append(out_params, rs);

        /* ToriRS_ModelFromRSCache *moves* the arrays out and leaves `rs`
         * hollow, so the free below releases a shell, not the geometry. */
        struct ToriRS_Model* mid = ToriRS_ModelFromRSCache(rs);
        RSCache_ModelFree(rs);
        if( !mid )
            continue;

        struct ToriDraw_Model* part = ToriDraw_ModelFromToriRS(mid);
        ToriRS_ModelFree(mid);
        if( part )
        {
            /* What each part brings to the merge's priority fold. A part with
             * neither a per-face array nor a model_priority contributes faces
             * at priority 0, which draws them behind everything. */
            if( getenv("EV_PART_DEBUG") )
                fprintf(
                    stderr,
                    "    %s %d part model=%d faces=%d face_priorities=%s model_priority=%d\n",
                    subject,
                    subject_id,
                    model_ids[i],
                    part->face_count,
                    part->face_priorities ? "yes" : "no",
                    part->model_priority);
            /* EV_ONLY_PART=<index> keeps a single part of a merged subject, so
             * a shape that looks wrong can be attributed to the model that
             * actually contains it rather than guessed at from the composite. */
            const char* only = getenv("EV_ONLY_PART");
            if( only && out_params )
                out_params->valid = false; /* dropping a part desyncs the order */
            if( only && atoi(only) != i )
                ToriDraw_ModelFree(part);
            else
                parts[part_count++] = part;
        }
    }

    struct ToriDraw_Model* merged = NULL;
    if( part_count == 1 )
        merged = parts[0];
    else if( part_count > 1 )
    {
        merged = ToriDraw_ModelMerge(parts, part_count);

        /*
         * Check the merge's index arithmetic against a plain concatenation.
         *
         * A merged model's face must point at vertices belonging to the part it
         * came from, shifted by that part's vertex offset. Getting it wrong
         * produces geometry that is *somewhere*, so it renders — a ghost copy of
         * a part at the wrong place, which is what a shield appearing twice
         * looks like.
         */
        if( merged && getenv("EV_CHECK_MERGE") )
        {
            int voff = 0;
            int foff = 0;
            int bad = 0;
            for( int pi = 0; pi < part_count; pi++ )
            {
                for( int f = 0; f < parts[pi]->face_count; f++ )
                {
                    int d = foff + f;
                    if( merged->face_indices_a[d] != parts[pi]->face_indices_a[f] + voff ||
                        merged->face_indices_b[d] != parts[pi]->face_indices_b[f] + voff ||
                        merged->face_indices_c[d] != parts[pi]->face_indices_c[f] + voff )
                        bad++;
                }
                for( int v = 0; v < parts[pi]->vertex_count; v++ )
                    if( merged->vertices_x[voff + v] != parts[pi]->vertices_x[v] ||
                        merged->vertices_y[voff + v] != parts[pi]->vertices_y[v] ||
                        merged->vertices_z[voff + v] != parts[pi]->vertices_z[v] )
                        bad++;
                voff += parts[pi]->vertex_count;
                foff += parts[pi]->face_count;
            }
            fprintf(
                stderr,
                "  merge check: %d parts, %d vertices, %d faces, %d mismatch(es)\n",
                part_count, voff, foff, bad);
        }
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    free(parts);
    return merged;
}

/**
 * Drop every texture id the browser will not have pixels for.
 *
 * Its missing-texture rule skips a textured face *entirely*, which made
 * texture-driven backports such as the Summoning models bake successfully and
 * then render as a blank canvas. The reference client falls back to the face
 * colour instead, so that is what a stripped id gets — and it has to happen
 * before lighting bakes the per-corner colours.
 *
 * The predicate is the server's because only it knows what it can ship. It was
 * unconditional until textures were loadable at all, and leaving it that way
 * would mean every texture in the cache stayed invisible for the same reason
 * the untextured fallback existed.
 */
static void
strip_unavailable_textures(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->face_textures )
        return;
    for( int face = 0; face < model->face_count; face++ )
        if( !ev_build_texture_available(model->face_textures[face]) )
            model->face_textures[face] = (faceint_t)-1;
}

/**
 * Promote a built model to the HD variant, or decide it does not need one.
 *
 * Takes ownership of `model` either way: on the NULL paths it is freed, and
 * ToriDraw_ModelHDFromModel consumes it on the other.
 *
 * Only worth an HD model when a face actually needs one. Plane projection is
 * what the classic raster already does, so promoting a plane-only model buys
 * nothing and costs a second pointer per model — the exact bloat the HD variant
 * exists to avoid. A model whose mapped faces have no usable parameters is also
 * refused, because a mapping built from filler draws a confident wrong answer.
 */
static struct ToriDraw_ModelHD*
finish_hd(
    struct ToriDraw_Model* model,
    struct EV_TexParams* params,
    const char* subject,
    int subject_id)
{
    assert(params);
    assert(subject);
    if( !model )
    {
        tex_params_free(params);
        return NULL;
    }

    int needs_mapping = 0;
    if( model->texture_render_types )
        for( int i = 0; i < model->textured_face_count; i++ )
            if( model->texture_render_types[i] != 0 )
            {
                needs_mapping = 1;
                break;
            }

    if( !needs_mapping || !params->valid || params->count != model->textured_face_count )
    {
        if( needs_mapping && getenv("EV_HD_DEBUG") )
            fprintf(
                stderr,
                "  %s %d: mapped faces but no usable parameters (valid=%d, %d of %d)\n",
                subject,
                subject_id,
                params->valid,
                params->count,
                model->textured_face_count);
        tex_params_free(params);
        ToriDraw_ModelFree(model);
        return NULL;
    }

    /* Consumes `model` — the arrays move into the HD shell by value, so there
     * is nothing left to free on this side whatever happens. */
    struct ToriDraw_ModelHD* hd = ToriDraw_ModelHDFromModel(model);
    assert(hd);

    /* The mappings are derived from the bind pose, so this has to happen before
     * anything animates the model. */
    ToriDraw_ModelBuildTextureMappings(
        hd,
        params->scale_x,
        params->scale_y,
        params->scale_z,
        params->rotation,
        params->direction,
        params->speed,
        params->trans_u,
        params->trans_v);
    tex_params_free(params);
    return hd;
}

/* ---- npc ----------------------------------------------------------------- */

/* The shared body. `out_params` is NULL for the plain build. */
static struct ToriDraw_Model*
build_npc_model(
    struct Tool_Dat2Cache* c,
    int npc_id,
    struct EV_TexParams* out_params)
{
    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(c, npc_id);
    if( !npc )
        return NULL;

    struct ToriDraw_Model* merged =
        merge_model_ids(c, "npc", npc_id, npc->models, npc->models_count, out_params);
    if( !merged )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    /*
     * From here the order is the client's, step for step — app.c's npc model
     * build. It is an order, not a list: recolour has to precede lighting
     * because lighting bakes face colours into per-vertex shaded colours and a
     * swap afterwards is a no-op, and the scale has to precede the bounds
     * cylinder because the cylinder is measured off the vertices.
     */
    for( int i = 0; i < npc->recolor_count; i++ )
        ToriDraw_ModelRecolor(merged, npc->recolor_to_find[i], npc->recolor_to_replace[i]);
    for( int i = 0; i < npc->retexture_count; i++ )
        ToriDraw_ModelRetexture(merged, npc->retexture_to_find[i], npc->retexture_to_replace[i]);

    strip_unavailable_textures(merged);

    /* Npc opcodes 97/98. Missing here until now, so every npc with a scale of
     * its own — a giant, a small pet — rendered at the model's raw size. */
    if( npc->width_scale != 128 || npc->height_scale != 128 )
        ToriDraw_ModelScale(merged, npc->width_scale, npc->width_scale, npc->height_scale);

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_LightModelActor(hnd, npc->contrast, npc->ambient);

    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);

    RSCache_Dat2ConfigNpcFree(npc);
    return merged;
}

struct ToriDraw_Model*
ev_build_npc_model(
    struct Tool_Dat2Cache* c,
    int npc_id)
{
    return build_npc_model(c, npc_id, NULL);
}

struct ToriDraw_ModelHD*
ev_build_npc_model_hd(
    struct Tool_Dat2Cache* c,
    int npc_id)
{
    struct EV_TexParams params;
    memset(&params, 0, sizeof(params));
    params.valid = true;

    return finish_hd(build_npc_model(c, npc_id, &params), &params, "npc", npc_id);
}

/* ---- obj ----------------------------------------------------------------- */

const char*
ev_obj_model_variant_name(enum EV_ObjModelVariant variant)
{
    static const char* const NAMES[EV_OBJ_MODEL_VARIANT_COUNT] = {
        "item", "male", "female", "male head", "female head"
    };
    assert(variant >= 0);
    assert(variant < EV_OBJ_MODEL_VARIANT_COUNT);
    return NAMES[variant];
}

/**
 * The model ids one variant names, written into `out` (at least 3 entries).
 *
 * Returns how many slots were filled, INCLUDING the ones that are -1 — an
 * absent wear slot still occupies its place in the list, and merge_model_ids
 * skips it. Returns 0 when the variant names nothing at all.
 */
static int
obj_variant_model_ids(
    const struct RSCache_Dat2ConfigObj* obj,
    enum EV_ObjModelVariant variant,
    int out[3])
{
    assert(obj);
    switch( variant )
    {
    case EV_OBJ_MODEL_ITEM:
        out[0] = obj->inventory_model_id;
        return 1;
    case EV_OBJ_MODEL_MALE:
        out[0] = obj->male_model_0;
        out[1] = obj->male_model_1;
        out[2] = obj->male_model_2;
        return 3;
    case EV_OBJ_MODEL_FEMALE:
        out[0] = obj->female_model_0;
        out[1] = obj->female_model_1;
        out[2] = obj->female_model_2;
        return 3;
    case EV_OBJ_MODEL_MALE_HEAD:
        out[0] = obj->male_head_model;
        out[1] = obj->male_head_model_2;
        return 2;
    case EV_OBJ_MODEL_FEMALE_HEAD:
        out[0] = obj->female_head_model;
        out[1] = obj->female_head_model_2;
        return 2;
    default:
        break;
    }
    assert(0 && "unhandled obj model variant");
    return 0;
}

static struct ToriDraw_Model*
build_obj_model(
    struct Tool_Dat2Cache* c,
    int obj_id,
    enum EV_ObjModelVariant variant,
    struct EV_TexParams* out_params)
{
    struct RSCache_Dat2ConfigObj* obj = ev_obj_load(c, obj_id);
    if( !obj )
        return NULL;

    int ids[3] = { -1, -1, -1 };
    int count = obj_variant_model_ids(obj, variant, ids);
    struct ToriDraw_Model* merged = merge_model_ids(c, "obj", obj_id, ids, count, out_params);
    if( !merged )
    {
        RSCache_Dat2ConfigObjFree(obj);
        return NULL;
    }

    /*
     * The client's order, from bridge_rasterize_obj_icon (the inventory icon)
     * and ev_build_player_model (the worn models): resize, then recolour, then
     * lighting.
     *
     * The resize is the ICON build's. The world-drop path (app_world_spawn_obj_
     * stack) passes 128/128 and so ignores opcodes 110-112 — a difference of a
     * scale between the same model on the ground and in the inventory, and the
     * icon is the one anybody means by "the item's model".
     */
    if( obj->resize_x != 128 || obj->resize_y != 128 || obj->resize_z != 128 )
        ToriDraw_ModelScale(merged, obj->resize_x, obj->resize_z, obj->resize_y);

    for( int i = 0; i < obj->recolor_count; i++ )
        ToriDraw_ModelRecolor(merged, obj->recolors_from[i], obj->recolors_to[i]);

    /*
     * Retextures (opcode 41) are applied here and NOT by this client: its
     * ToriRS_Objtype carries no retexture pairs, so an obj that swaps a texture
     * draws with the source model's in the game and with the record's here.
     * That disagreement is worth seeing rather than reproducing — the viewer's
     * job includes saying what the record holds.
     */
    for( int i = 0; i < obj->retexture_count; i++ )
        ToriDraw_ModelRetexture(merged, obj->retextures_from[i], obj->retextures_to[i]);

    strip_unavailable_textures(merged);

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    /*
     * The icon and the ground drop both light as scene geometry with the
     * record's own ambient/contrast. A worn model is never lit alone in the
     * client — it is merged into the body first and the whole player lights at
     * 0/0 (PlayerModel_BuildFromAppearance) — so that is what one gets here.
     */
    if( variant == EV_OBJ_MODEL_ITEM )
        ToriDraw_LightModelScene(hnd, obj->contrast, obj->ambient);
    else
        ToriDraw_LightModelActor(hnd, 0, 0);

    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);

    RSCache_Dat2ConfigObjFree(obj);
    return merged;
}

struct ToriDraw_Model*
ev_build_obj_model(
    struct Tool_Dat2Cache* c,
    int obj_id,
    enum EV_ObjModelVariant variant)
{
    return build_obj_model(c, obj_id, variant, NULL);
}

struct ToriDraw_ModelHD*
ev_build_obj_model_hd(
    struct Tool_Dat2Cache* c,
    int obj_id,
    enum EV_ObjModelVariant variant)
{
    struct EV_TexParams params;
    memset(&params, 0, sizeof(params));
    params.valid = true;

    return finish_hd(build_obj_model(c, obj_id, variant, &params), &params, "obj", obj_id);
}

/* ---- loc ----------------------------------------------------------------- */

/*
 * Loc recolour endpoints at or below this are texture ids, not HSL colours.
 *
 * The reference stores a textured face's texture id in the same faceColour
 * field a recolour pass remaps, so the two operations are one pass partitioned
 * purely by value range: texture ids occupy 0..50, HSL colours are always
 * above 50. This C decoder splits the texture id out into face_textures, so the
 * partition has to be made explicit — world_scenery.u.c's apply_transforms
 * makes the same one, and without it scenery loses its recolour-driven texture.
 */
#define EV_LOC_RECOLOUR_TEXTURE_MAX 50

/**
 * The model ids a loc lists for one shape.
 *
 * `shape` of -1 means "the first group the record carries", which is the only
 * sensible default for a viewer: a loc's shapes are a wall, its corner and its
 * diagonal, and there is no shape that every loc has.
 *
 * A record encoded with opcode 5 is single-model: the decoder sets a count of 1
 * and leaves `shapes` NULL on purpose, because shape selection does not apply
 * to it (ToriRS_LocationFromRSCacheDat2 says the same). Returns 0 when the loc
 * does not carry the shape asked for — NOT another shape's mesh, because a wall
 * drawn in answer to "show me the corner" looks like a correct answer.
 */
static int
loc_shape_model_ids(
    const struct RSCache_Dat2ConfigLoc* loc,
    int shape,
    int* out,
    int out_cap)
{
    int count = 0;

    assert(loc);
    assert(out);
    if( !loc->models || loc->shapes_and_model_count <= 0 || !loc->lengths )
        return 0;

    if( !loc->shapes )
    {
        int n = loc->lengths[0];
        for( int i = 0; i < n && count < out_cap; i++ )
            out[count++] = loc->models[0][i];
        return count;
    }

    for( int g = 0; g < loc->shapes_and_model_count; g++ )
    {
        if( shape >= 0 && loc->shapes[g] != shape )
            continue;
        for( int i = 0; i < loc->lengths[g] && count < out_cap; i++ )
            out[count++] = loc->models[g][i];
        /* -1 takes the first group only; a named shape takes every group that
         * carries it, which is what scenery_load_model does. */
        if( shape < 0 )
            break;
    }
    return count;
}

static struct ToriDraw_Model*
build_loc_model(
    struct Tool_Dat2Cache* c,
    int loc_id,
    int shape,
    struct EV_TexParams* out_params)
{
    /* The scene builder's own ceiling on how many models make one loc. */
    int ids[10];
    struct RSCache_Dat2ConfigLoc* loc = ev_loc_load(c, loc_id);
    if( !loc )
        return NULL;

    int count = loc_shape_model_ids(loc, shape, ids, (int)(sizeof(ids) / sizeof(ids[0])));
    struct ToriDraw_Model* merged = merge_model_ids(c, "loc", loc_id, ids, count, out_params);
    if( !merged )
    {
        RSCache_Dat2ConfigLocFree(loc);
        return NULL;
    }

    /*
     * world_scenery.u.c's apply_transforms, in its order: recolour and
     * retexture, mirror, orient, resize, translate. The orient step is absent
     * because nothing here is placed on a tile — the viewer orbits the model in
     * the record's own frame. `mirrored` is NOT absent: at orientation 0 the
     * scene builder's `loc->mirrored != (orientation > 3)` is exactly
     * `loc->mirrored`, and dropping it would draw a mirrored loc handed.
     */
    for( int i = 0; i < loc->recolor_count; i++ )
    {
        int from = loc->recolors_from[i];
        int to = loc->recolors_to[i];
        if( from <= EV_LOC_RECOLOUR_TEXTURE_MAX && to <= EV_LOC_RECOLOUR_TEXTURE_MAX )
            ToriDraw_ModelRetexture(merged, from, to);
        else
            ToriDraw_ModelRecolor(merged, from, to);
    }
    for( int i = 0; i < loc->retexture_count; i++ )
        ToriDraw_ModelRetexture(merged, loc->retextures_from[i], loc->retextures_to[i]);

    if( loc->mirrored )
        ToriDraw_ModelMirror(merged);
    if( loc->resize_x != 128 || loc->resize_height != 128 || loc->resize_z != 128 )
        ToriDraw_ModelScale(merged, loc->resize_x, loc->resize_z, loc->resize_height);
    if( loc->offset_x != 0 || loc->offset_y != 0 || loc->offset_z != 0 )
        ToriDraw_ModelTranslate(merged, loc->offset_x, loc->offset_y, loc->offset_z);

    strip_unavailable_textures(merged);

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    /*
     * Scene lighting, and the per-loc default at that.
     *
     * A `sharelight` loc is lit in the game by a whole-scene pass that merges
     * the normals of every loc sharing a point in space, which needs the
     * neighbours this viewer does not have. The scene builder already falls
     * back to the per-loc light for a runtime spawn, for the same reason, so
     * that is the honest answer here too — a sharelight loc drawn alone is
     * slightly harder-edged than the same loc in a built scene.
     */
    ToriDraw_LightModelScene(hnd, loc->contrast, loc->ambient);

    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);

    RSCache_Dat2ConfigLocFree(loc);
    return merged;
}

struct ToriDraw_Model*
ev_build_loc_model(
    struct Tool_Dat2Cache* c,
    int loc_id,
    int shape)
{
    return build_loc_model(c, loc_id, shape, NULL);
}

struct ToriDraw_ModelHD*
ev_build_loc_model_hd(
    struct Tool_Dat2Cache* c,
    int loc_id,
    int shape)
{
    struct EV_TexParams params;
    memset(&params, 0, sizeof(params));
    params.valid = true;

    return finish_hd(build_loc_model(c, loc_id, shape, &params), &params, "loc", loc_id);
}

/* ---- sequence -> animation ---------------------------------------------- */

/*
 * Assemble a sequence's animation.
 *
 * The framemap is the rig, the frames are the poses in sequence order, and
 * `ToriDraw_AnimationFromRSCache` is the client's own assembler for exactly
 * that. This used to build the ToriDraw_Animation field by field here, which
 * meant a second implementation of the frame/base copy — the same duplication
 * that got face priority wrong on the model side.
 */
struct ToriDraw_Animation*
ev_build_seq_anim(
    struct Tool_Dat2Cache* c,
    int seq_id,
    int* out_framemap_id)
{
    if( out_framemap_id )
        *out_framemap_id = -1;

    struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(c, seq_id);
    if( !seq )
        return NULL;

    /*
     * Skeletal (Animaya) first: these sequences carry no frame list at all, so
     * the classic path below would see an empty sequence and give up. Every
     * modern OldSchool npc animates this way.
     *
     * The result is still a ToriDraw_Animation — `skeletal` set, `base`/`frames`
     * NULL — so frame stepping is identical and only the pose call branches.
     * `frame_count` is the config's play range when it states one, which is how
     * one baked palette serves several sequences that slice it differently.
     */
    if( seq->frame_count <= 0 && seq->anim_maya_id > 0 )
    {
        struct RSCache_Dat2AnimMaya* maya = tool_dat2_animaya_load(c, seq->anim_maya_id);
        struct ToriDraw_Animation* anim = NULL;

        if( maya && maya->base_id >= 0 )
        {
            if( out_framemap_id )
                *out_framemap_id = maya->base_id;

            struct RSCache_Dat2SkeletalBase* base =
                tool_dat2_skeletal_base_load(c, maya->base_id);
            struct ToriDraw_SkeletalAnim* skeletal =
                ToriDraw_SkeletalAnimFromRSCache(seq_id, maya, base);

            if( skeletal )
            {
                anim = calloc(1, sizeof(*anim));
                assert(anim);
                int play = skeletal->frame_count;
                if( seq->anim_maya_end > seq->anim_maya_start )
                {
                    play = seq->anim_maya_end - seq->anim_maya_start;
                    if( play > skeletal->frame_count )
                        play = skeletal->frame_count;
                }
                anim->skeletal = skeletal;
                anim->frame_count = play > 0 ? play : 1;
                anim->replaceheldleft = -1;
                anim->replaceheldright = -1;
            }
            RSCache_Dat2SkeletalBaseFree(base);
        }

        RSCache_Dat2AnimMayaFree(maya);
        RSCache_Dat2ConfigSequenceFree(seq);
        return anim;
    }

    if( !seq->frame_ids || seq->frame_count <= 0 )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }

    int framemap_id = tool_dat2_seq_framemap_id(c, seq, 0);
    struct RSCache_Dat2Framemap* fm =
        framemap_id >= 0 ? tool_dat2_framemap_load(c, framemap_id) : NULL;
    if( !fm )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }
    if( out_framemap_id )
        *out_framemap_id = framemap_id;

    struct RSCache_Dat2Frame** frames =
        calloc((size_t)seq->frame_count, sizeof(*frames));
    int* delays = calloc((size_t)seq->frame_count, sizeof(*delays));
    struct ToriDraw_Animation* anim = NULL;

    assert(frames);
    assert(delays);
    /*
     * Every frame slot is filled, including the ones that fail to decode.
     * Dropping a frame would shorten the sequence, which moves every later
     * frame's timing and shifts what `frame_step` loops back to — a
     * sequence that plays slightly wrong is harder to notice than one
     * frame that holds.
     */
    int decoded = 0;
    for( int i = 0; i < seq->frame_count; i++ )
    {
        frames[i] = tool_dat2_frame_load(c, fm, seq->frame_ids[i]);
        delays[i] = seq->frame_lengths ? seq->frame_lengths[i] : 1;
        if( frames[i] )
            decoded++;
    }

    if( decoded > 0 )
        anim = ToriDraw_AnimationFromRSCache(
            fm,
            (struct RSCache_Dat2Frame const* const*)frames,
            delays,
            seq->frame_count,
            seq->frame_step);

    for( int i = 0; i < seq->frame_count; i++ )
        RSCache_Dat2FrameFree(frames[i]);

    free(frames);
    free(delays);
    RSCache_Dat2FramemapFree(fm);
    RSCache_Dat2ConfigSequenceFree(seq);
    return anim;
}
