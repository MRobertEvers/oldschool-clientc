#include "ev_player.h"

#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_types.h"

/* The idk/obj/spotanim record loaders used to live here as statics. They moved
 * out when the obj and loc model builds needed the same two of them — see
 * ev_config.h for why a second copy of that layout branch would be silent. */
#include "ev_config.h"

#include "rscache.h"
#include "toridraw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- model helpers ------------------------------------------------------- */

/* The client's two-step conversion, never a third copy of it — see the header
 * comment in ev_build.c for what a hand-rolled one cost. */
static struct ToriDraw_Model*
ev_model_from_rs(struct RSCache_Model* rs)
{
    struct ToriRS_Model* mid;
    struct ToriDraw_Model* model;

    assert(rs);
    /* ToriRS_ModelFromRSCache moves the arrays out; `rs` is a shell afterwards. */
    mid = ToriRS_ModelFromRSCache(rs);
    RSCache_ModelFree(rs);
    if( !mid )
        return NULL;
    model = ToriDraw_ModelFromToriRS(mid);
    ToriRS_ModelFree(mid);
    return model;
}

static struct ToriDraw_Model*
ev_model_load(struct Tool_Dat2Cache* c, int model_id)
{
    if( model_id < 0 )
        return NULL;
    return ev_model_from_rs(tool_dat2_model_load(c, model_id));
}

/* A raw model record read off disk. The porter writes these out byte for byte,
 * so the cache decoder reads them unchanged. */
static struct ToriDraw_Model*
ev_model_load_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    long len;
    uint8_t* bytes;
    struct RSCache_Model* rs;

    if( !f )
    {
        fprintf(stderr, "ev_player: cannot open model file %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if( len <= 0 )
    {
        fclose(f);
        return NULL;
    }
    bytes = malloc((size_t)len);
    assert(bytes);
    if( fread(bytes, 1, (size_t)len, f) != (size_t)len )
    {
        fclose(f);
        free(bytes);
        fprintf(stderr, "ev_player: short read on %s\n", path);
        return NULL;
    }
    fclose(f);
    rs = RSCache_ModelNewDecode(bytes, (int)len);
    free(bytes);
    if( !rs )
    {
        fprintf(stderr, "ev_player: %s did not decode as a model record\n", path);
        return NULL;
    }
    return ev_model_from_rs(rs);
}

/* ---- player -------------------------------------------------------------- */

void
ev_player_spec_init(struct EV_PlayerSpec* spec)
{
    assert(spec);
    memset(spec, 0, sizeof(*spec));
    for( int i = 0; i < EV_PLAYER_PARTS; i++ )
        spec->kits[i] = -1;
    for( int i = 0; i < EV_PLAYER_MAX_WORN; i++ )
        spec->worn[i] = -1;
}

/*
 * The first selectable identity kit for each body part.
 *
 * PlayerAppearance_ResolveDefaultMale's rule, and its reason: every hairstyle,
 * beard and jaw in the cache is an identity kit too, so "all of them" is a few
 * hundred heads drawn on top of each other. One per part is a body.
 */
static void
ev_resolve_default_kits(struct Tool_Dat2Cache* c, int kits[EV_PLAYER_PARTS])
{
    int found = 0;
    for( int i = 0; i < EV_PLAYER_PARTS; i++ )
        if( kits[i] >= 0 )
            found++;

    for( int id = 0; id < EV_PLAYER_IDK_SCAN_MAX && found < EV_PLAYER_PARTS; id++ )
    {
        struct RSCache_Dat2ConfigIdk* idk = ev_idk_load(c, id);
        if( !idk )
            continue;
        if( !idk->is_not_selectable && idk->body_part_id >= 0 &&
            idk->body_part_id < EV_PLAYER_PARTS && kits[idk->body_part_id] < 0 )
        {
            kits[idk->body_part_id] = id;
            found++;
        }
        RSCache_Dat2ConfigIdkFree(idk);
    }
}

/* Reference ObjType.getWearModelNoCheck: three wear models per gender. */
static void
ev_obj_wear_models(const struct RSCache_Dat2ConfigObj* obj, int gender, int out[3])
{
    if( gender )
    {
        out[0] = obj->female_model_0;
        out[1] = obj->female_model_1;
        out[2] = obj->female_model_2;
    }
    else
    {
        out[0] = obj->male_model_0;
        out[1] = obj->male_model_1;
        out[2] = obj->male_model_2;
    }
}

static void
ev_map_add(
    struct EV_PlayerPartMap* map,
    int source_id,
    int is_obj,
    int model_id,
    const struct ToriDraw_Model* part,
    int vertex_first,
    int face_first)
{
    struct EV_PlayerPart* p;
    /* The part map is an optional out-parameter — most callers only want the
     * merged model — so absent in, absent out. NOT a contract violation. */
    if( !map )
        return;
    assert(part);
    if( map->count >= EV_PLAYER_MAX_PARTS )
        return;
    p = &map->parts[map->count++];
    p->source_id = source_id;
    p->is_obj = is_obj;
    p->model_id = model_id;
    p->vertex_first = vertex_first;
    p->vertex_count = part->vertex_count;
    p->face_first = face_first;
    p->face_count = part->face_count;
}

struct ToriDraw_Model*
ev_build_player_model(
    struct Tool_Dat2Cache* c,
    const struct EV_PlayerSpec* spec,
    struct EV_PlayerPartMap* out_map)
{
    struct ToriDraw_Model* parts[EV_PLAYER_MAX_PARTS];
    struct ToriDraw_Model* merged;
    int kits[EV_PLAYER_PARTS];
    int part_count = 0;
    int vertex_off = 0;
    int face_off = 0;

    assert(c && spec);
    if( out_map )
        memset(out_map, 0, sizeof(*out_map));

    for( int i = 0; i < EV_PLAYER_PARTS; i++ )
        kits[i] = spec->kits[i];
    ev_resolve_default_kits(c, kits);

    /* Body first, then equipment — the appearance-slot order the client walks,
     * and the order the part map's vertex ranges are stated in. */
    for( int part = 0; part < EV_PLAYER_PARTS; part++ )
    {
        struct RSCache_Dat2ConfigIdk* idk;
        if( kits[part] < 0 )
            continue;
        idk = ev_idk_load(c, kits[part]);
        if( !idk )
            continue;
        for( int m = 0; m < idk->model_ids_count && part_count < EV_PLAYER_MAX_PARTS; m++ )
        {
            struct ToriDraw_Model* model = ev_model_load(c, idk->model_ids[m]);
            if( !model )
                continue;
            /* Kit recolours before the merge — reference IdkType
             * getModelNoCheck, and the same reason the npc build recolours
             * before lighting: lighting bakes colour into per-corner shades. */
            for( int r = 0; r < idk->recolor_count; r++ )
                ToriDraw_ModelRecolor(model, idk->recolors_from[r], idk->recolors_to[r]);
            ev_map_add(out_map, kits[part], 0, idk->model_ids[m], model, vertex_off, face_off);
            vertex_off += model->vertex_count;
            face_off += model->face_count;
            parts[part_count++] = model;
        }
        RSCache_Dat2ConfigIdkFree(idk);
    }

    for( int w = 0; w < spec->worn_count && w < EV_PLAYER_MAX_WORN; w++ )
    {
        struct RSCache_Dat2ConfigObj* obj;
        int wear[3];
        if( spec->worn[w] < 0 )
            continue;
        obj = ev_obj_load(c, spec->worn[w]);
        if( !obj )
        {
            fprintf(stderr, "ev_player: obj %d did not load\n", spec->worn[w]);
            continue;
        }
        ev_obj_wear_models(obj, spec->gender, wear);
        for( int m = 0; m < 3 && part_count < EV_PLAYER_MAX_PARTS; m++ )
        {
            struct ToriDraw_Model* model = ev_model_load(c, wear[m]);
            if( !model )
                continue;
            for( int r = 0; r < obj->recolor_count; r++ )
                ToriDraw_ModelRecolor(model, obj->recolors_from[r], obj->recolors_to[r]);
            ev_map_add(out_map, spec->worn[w], 1, wear[m], model, vertex_off, face_off);
            vertex_off += model->vertex_count;
            face_off += model->face_count;
            parts[part_count++] = model;
        }
        RSCache_Dat2ConfigObjFree(obj);
    }

    if( part_count == 0 )
        return NULL;

    merged = ToriDraw_ModelNewMerge(parts, part_count);
    for( int p = 0; p < part_count; p++ )
        ToriDraw_ModelFree(parts[p]);
    if( !merged )
        return NULL;

    /* Design colours are left at palette identity; see the header. */

    /* Same fallback ev_build_npc_model makes, for the same reason: this viewer
     * ships geometry with no material table, and the raster skips a textured
     * face it cannot resolve, so a textured model renders as nothing at all. */
    if( merged->face_textures )
        for( int face = 0; face < merged->face_count; face++ )
            merged->face_textures[face] = (faceint_t)-1;

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = merged;
        /* Players light at contrast 0 / ambient 0 — PlayerModel_BuildFrom-
         * Appearance's own call. */
        ToriDraw_LightModelActor(hnd, 0, 0);
    }
    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);
    return merged;
}

/* ---- spotanim ------------------------------------------------------------ */

int
ev_spotanim_model_id(
    struct Tool_Dat2Cache* c,
    int spotanim_id)
{
    struct RSCache_Dat2ConfigSpotanim* spot = ev_spotanim_load(c, spotanim_id);
    int id = spot ? spot->model : -1;
    RSCache_Dat2ConfigSpotanimFree(spot);
    return id;
}

struct ToriDraw_Model*
ev_build_spotanim_model(
    struct Tool_Dat2Cache* c,
    int spotanim_id,
    const char* model_file_override,
    int angle_override,
    int* out_seq_id)
{
    struct RSCache_Dat2ConfigSpotanim* spot;
    struct ToriDraw_Model* model;

    assert(c);
    if( out_seq_id )
        *out_seq_id = -1;

    spot = ev_spotanim_load(c, spotanim_id);
    if( !spot )
        return NULL;
    if( out_seq_id )
        *out_seq_id = spot->anim;

    model = model_file_override ? ev_model_load_file(model_file_override)
                                : ev_model_load(c, spot->model);
    if( !model )
    {
        RSCache_Dat2ConfigSpotanimFree(spot);
        return NULL;
    }

    /* app_world_build_spotanim_model, in its order. The recolour loop's guard
     * on recol_s[0] is the reference's, not a shortcut. */
    if( spot->recol_s[0] != 0 )
        for( int i = 0; i < RSCACHE_SPOTANIM_COLOUR_SLOTS; i++ )
            ToriDraw_ModelRecolor(model, spot->recol_s[i], spot->recol_d[i]);
    for( int i = 0; i < RSCACHE_SPOTANIM_COLOUR_SLOTS; i++ )
        if( spot->retex_s[i] != 0 )
            ToriDraw_ModelRetexture(model, spot->retex_s[i], spot->retex_d[i]);

    if( spot->resizeh != 128 || spot->resizev != 128 )
        ToriDraw_ModelScale(model, spot->resizeh, spot->resizeh, spot->resizev);
    /* Before lighting, where the client does it: the lighting bake reads
     * vertex positions, so orienting afterwards would light the mesh in one
     * pose and draw it in another. */
    if( angle_override >= 0 )
        ToriDraw_ModelOrient(model, angle_override & 3);
    else if( spot->angle != 0 )
        ToriDraw_ModelOrient(model, spot->angle / 90);

    if( model->face_textures )
        for( int face = 0; face < model->face_count; face++ )
            model->face_textures[face] = (faceint_t)-1;

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_LightModelActor(hnd, spot->contrast, spot->ambient);
    }
    ToriDraw_ModelSetBoundsCylinder(model);
    ToriDraw_ModelCaptureOriginalVertices(model);

    RSCache_Dat2ConfigSpotanimFree(spot);
    return model;
}

/* The body+graphic merge lives in ev_render.c — see the header. */
