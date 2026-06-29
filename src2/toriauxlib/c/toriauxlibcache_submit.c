#include "toriauxlibcache_submit.h"

#include <stdio.h>

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_config_idk.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_idk.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_skeletalbase.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"

struct SubmitSequenceCtx
{
    struct ToriAuxLibCache* c;
};

static void
submit_sequence_cb(
    int seq_id,
    struct RSCacheDat1A_ConfigSequence* sequence,
    void* user_data)
{
    struct SubmitSequenceCtx* ctx = user_data;
    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibCache_SequenceNewFromCacheDatSequence(sequence, seq_id);
    if( !neutral )
        return;
    ToriAuxLibCore_SequenceAdd(ToriAuxLibCache_Core(ctx->c), seq_id, neutral);
}

struct SubmitFlotypeCtx
{
    struct ToriAuxLibCache* c;
};

static struct ToriAuxLibCore_Flotype*
submit_flotype_copy(const struct ToriAuxLibCore_Flotype* src)
{
    struct ToriAuxLibCore_Flotype* copy;
    if( !src )
        return NULL;
    copy = malloc(sizeof(struct ToriAuxLibCore_Flotype));
    if( !copy )
        return NULL;
    *copy = *src;
    return copy;
}

static void
submit_flotype_cb(
    int flo_id,
    struct RSCacheDat2A_ConfigOverlay* flotype,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibCache_FlotypeNewFromCacheConfigOverlay(flotype, flo_id);
    struct ToriAuxLibCore_Flotype* underlay_copy;
    if( !neutral )
        return;
    ToriAuxLibCore_FlotypeAdd(ToriAuxLibCache_Core(ctx->c), flo_id, neutral);
    underlay_copy = submit_flotype_copy(neutral);
    if( underlay_copy )
        ToriAuxLibCore_UnderlayAdd(ToriAuxLibCache_Core(ctx->c), flo_id, underlay_copy);
}

struct SubmitLocationCtx
{
    struct ToriAuxLibCache* c;
};

static void
submit_location_cb(
    int loc_id,
    struct RSCacheDat2A_ConfigLocation* config_loc,
    void* user_data)
{
    struct SubmitLocationCtx* ctx = user_data;
    struct ToriAuxLibCore_Location* neutral =
        ToriAuxLibCache_LocationNewFromCacheConfigLocation(config_loc);
    if( !neutral )
        return;
    ToriAuxLibCore_LocationAdd(ToriAuxLibCache_Core(ctx->c), loc_id, neutral);
}

void
ToriAuxLibCache_SubmitMapTerrainFromDat1(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapTerrain* terrain = dat1_buildcache_map_terrain_get(dat1(c), map_id);
    if( !terrain )
        return;

    struct ToriAuxLibCore_MapTerrain* neutral =
        ToriAuxLibCache_MapTerrainNewFromCacheMapTerrain(terrain);
    if( !neutral )
        return;

    ToriAuxLibCore_MapTerrainAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitMapSceneryFromDat1(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapLocs* locs = dat1_buildcache_map_scenery_get(dat1(c), map_id);
    if( !locs )
        return;

    struct ToriAuxLibCore_MapLocs* neutral = ToriAuxLibCache_MapLocsNewFromCacheMapLocs(locs);
    if( !neutral )
        return;

    ToriAuxLibCore_MapSceneryAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitAnimationFromDat1(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct RSCacheDat1A_AnimBaseFrames* abf = dat1_buildcache_animbaseframes_take(dat1(c), anim_id);
    if( !abf )
        return;

    struct ToriAuxLibCore_Animation* anim = ToriAuxLibCache_AnimationNewFromCacheDatAnimbaseframes(abf);
    if( !anim )
        return;

    ToriAuxLibCore_AnimationAdd(ToriAuxLibCache_Core(c), anim_id, anim);
}

void
ToriAuxLibCache_SubmitModelFromDat1(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct RSCacheDat2A_Model* model = dat1_buildcache_model_get(dat1(c), model_id);
    if( !model )
        return;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !gc_model )
        return;

    ToriAuxLibCore_ModelAdd(ToriAuxLibCache_Core(c), model_id, gc_model);
}

static void
submit_recolor_raw(
    struct RSCacheDat2A_Model* model,
    int color_src,
    int color_dst)
{
    if( !model || !model->face_colors )
        return;

    for( int f = 0; f < model->face_count; f++ )
    {
        if( model->face_colors[f] == (uint16_t)color_src )
            model->face_colors[f] = (uint16_t)color_dst;
    }
}

void
ToriAuxLibCache_SubmitIdkModelFromDat1(
    struct ToriAuxLibCache* c,
    int idk_id)
{
    struct RSCacheDat1A_ConfigIdk* idk = dat1_buildcache_idk_get(dat1(c), idk_id);
    if( !idk || !idk->models || idk->models_count <= 0 )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->models_count && model_count < 12; i++ )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), idk->models[i]);
        if( !raw )
            continue;
        raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_IdkModelAdd(ToriAuxLibCache_Core(c), idk_id, gc_model);
}

void
ToriAuxLibCache_SubmitObjModelFromDat1(
    struct ToriAuxLibCache* c,
    int obj_id)
{
    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(dat1(c), obj_id);
    if( !obj )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;

    if( obj->manwear != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), obj->manwear);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->manwear2 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), obj->manwear2);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->manwear3 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), obj->manwear3);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    for( int i = 0; i < obj->recol_count; i++ )
        submit_recolor_raw(merged, obj->recol_s[i], obj->recol_d[i]);

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_ObjModelAdd(ToriAuxLibCache_Core(c), obj_id, gc_model);
}

void
ToriAuxLibCache_SubmitIdkModelFromDat2(
    struct ToriAuxLibCache* c,
    int idk_id)
{
    struct RSCacheDat2A_ConfigIdk* idk = dat2_buildcache_identkit_get(dat2(c), idk_id);
    if( !idk || !idk->model_ids || idk->model_ids_count <= 0 )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->model_ids_count && model_count < 12; i++ )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), idk->model_ids[i]);
        if( !raw )
            continue;
        raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_IdkModelAdd(ToriAuxLibCache_Core(c), idk_id, gc_model);
}

void
ToriAuxLibCache_SubmitObjModelFromDat2(
    struct ToriAuxLibCache* c,
    int obj_id)
{
    struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(dat2(c), obj_id);
    if( !obj )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;

    if( obj->male_model_0 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), obj->male_model_0);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->male_model_1 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), obj->male_model_1);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->male_model_2 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), obj->male_model_2);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    for( int i = 0; i < obj->recolor_count; i++ )
        submit_recolor_raw(merged, obj->recolors_from[i], obj->recolors_to[i]);

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_ObjModelAdd(ToriAuxLibCache_Core(c), obj_id, gc_model);
}

void
ToriAuxLibCache_SubmitTexture(
    struct ToriAuxLibCache* c,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture)
{
    ToriAuxLibCore_TextureAdd(ToriAuxLibCache_Core(c), texture_id, texture);
}

void
ToriAuxLibCache_SubmitAllSequencesFromDat1(struct ToriAuxLibCache* c)
{
    struct SubmitSequenceCtx ctx = { .c = c };
    dat1_buildcache_foreach_sequence(dat1(c), submit_sequence_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllFlotypesFromDat1(struct ToriAuxLibCache* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat1_buildcache_foreach_flotype(dat1(c), submit_flotype_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllLocationsFromDat1(struct ToriAuxLibCache* c)
{
    struct SubmitLocationCtx ctx = { .c = c };
    dat1_buildcache_foreach_config_loc(dat1(c), submit_location_cb, &ctx);
}

void
ToriAuxLibCache_SubmitSpriteFromDat1(
    struct ToriAuxLibCache* c,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite)
{
    if( !c || !sprite )
        return;
    ToriAuxLibCore_SpriteAdd(ToriAuxLibCache_Core(c), sprite_id, sprite);
}

void
ToriAuxLibCache_SubmitFontFromDat1(
    struct ToriAuxLibCache* c,
    int font_id,
    struct ToriAuxLibCore_Font* font)
{
    if( !c || !font )
        return;
    ToriAuxLibCore_FontAdd(ToriAuxLibCache_Core(c), font_id, font);
}

void
ToriAuxLibCache_SubmitAllComponentsFromDat1(struct ToriAuxLibCache* c)
{
    ToriAuxLibCache_SubmitComponentsFromDat1(c, NULL, 0);
}

void
ToriAuxLibCache_SubmitComponentsFromDat1(
    struct ToriAuxLibCache* c,
    const bool* needed,
    int needed_count)
{
    if( !c )
        return;

    struct RSCacheDat1A_ConfigComponentList* interfaces = dat1_buildcache_get_interfaces(dat1(c));
    if( !interfaces || !interfaces->components )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);
    for( int i = 0; i < interfaces->components_count; i++ )
    {
        if( needed )
        {
            if( i >= needed_count || !needed[i] )
                continue;
        }

        struct RSCacheDat1A_ConfigComponent* comp = interfaces->components[i];
        if( !comp )
            continue;

        struct ToriAuxLibCore_Component* neutral = ToriAuxLibCache_ComponentNewFromCacheComponent(comp);
        if( !neutral )
            continue;

        int component_id = comp->id >= 0 ? comp->id : i;
        ToriAuxLibCore_ComponentAdd(core, component_id, neutral);
    }
}

static void
submit_dat2_sequence_cb(
    int seq_id,
    struct RSCacheDat2A_ConfigSequence* sequence,
    void* user_data)
{
    struct SubmitSequenceCtx* ctx = user_data;
    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibCache_SequenceNewFromCacheDat2Sequence(sequence, seq_id);
    if( !neutral )
        return;
    ToriAuxLibCore_SequenceAdd(ToriAuxLibCache_Core(ctx->c), seq_id, neutral);
}

static void
submit_dat2_flotype_cb(
    int flo_id,
    struct RSCacheDat2A_ConfigOverlay* flotype,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibCache_FlotypeNewFromCacheConfigOverlay(flotype, flo_id);
    if( !neutral )
        return;
    ToriAuxLibCore_FlotypeAdd(ToriAuxLibCache_Core(ctx->c), flo_id, neutral);
}

static void
submit_dat2_underlay_cb(
    int underlay_id,
    struct RSCacheDat2A_ConfigUnderlay* underlay,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibCache_UnderlayNewFromCacheConfigUnderlay(underlay, underlay_id);
    if( !neutral )
        return;
    ToriAuxLibCore_UnderlayAdd(ToriAuxLibCache_Core(ctx->c), underlay_id, neutral);
}

void
ToriAuxLibCache_SubmitMapTerrainFromDat2(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapTerrain* terrain = dat2_buildcache_map_terrain_get(dat2(c), map_id);
    if( !terrain )
        return;

    struct ToriAuxLibCore_MapTerrain* neutral =
        ToriAuxLibCache_MapTerrainNewFromCacheMapTerrain(terrain);
    if( !neutral )
        return;

    ToriAuxLibCore_MapTerrainAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitMapSceneryFromDat2(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapLocs* locs = dat2_buildcache_map_scenery_get(dat2(c), map_id);
    if( !locs )
        return;

    struct ToriAuxLibCore_MapLocs* neutral = ToriAuxLibCache_MapLocsNewFromCacheMapLocs(locs);
    if( !neutral )
        return;

    ToriAuxLibCore_MapSceneryAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitModelFromDat2(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct RSCacheDat2A_Model* model = dat2_buildcache_model_get(dat2(c), model_id);
    if( !model )
        return;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !gc_model )
        return;

    ToriAuxLibCore_ModelAdd(ToriAuxLibCache_Core(c), model_id, gc_model);
}

void
ToriAuxLibCache_SubmitAllSequencesFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitSequenceCtx ctx = { .c = c };
    dat2_buildcache_foreach_sequence(dat2(c), submit_dat2_sequence_cb, &ctx);
}

void
ToriAuxLibCache_SubmitSequenceFromDat2(
    struct ToriAuxLibCache* c,
    int seq_id)
{
    struct RSCacheDat2A_ConfigSequence* sequence = dat2_buildcache_sequence_get(dat2(c), seq_id);
    if( !sequence )
        return;

    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibCache_SequenceNewFromCacheDat2Sequence(sequence, seq_id);
    if( !neutral )
        return;

    ToriAuxLibCore_SequenceAdd(ToriAuxLibCache_Core(c), seq_id, neutral);
}

void
ToriAuxLibCache_SubmitAllFlotypesFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat2_buildcache_foreach_flotype(dat2(c), submit_dat2_flotype_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllUnderlaysFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat2_buildcache_foreach_underlay(dat2(c), submit_dat2_underlay_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllLocationsFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitLocationCtx ctx = { .c = c };
    dat2_buildcache_foreach_config_loc(dat2(c), submit_location_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAnimationFromDat2(
    struct ToriAuxLibCache* c,
    int archive_id)
{
    if( !c || archive_id < 0 )
        return;

    struct Dat2BuildCache_FramesArchive* fa =
        dat2_buildcache_frames_take(dat2(c), archive_id);
    if( !fa )
        return;

    struct ToriAuxLibCore_Animation* anim =
        ToriAuxLibCache_AnimationNewFromDat2FramesArchive(fa, archive_id);
    Dat2BuildCache_FramesArchiveFree(fa);

    if( !anim )
        return;

    ToriAuxLibCore_AnimationAdd(ToriAuxLibCache_Core(c), archive_id, anim);
}

void
ToriAuxLibCache_SubmitSkeletalFromDat2(
    struct ToriAuxLibCache* c,
    int anim_maya_id)
{
    if( !c || anim_maya_id < 0 )
        return;

    struct RSCacheDat2A_AnimMaya* maya =
        dat2_buildcache_skeletal_take(dat2(c), anim_maya_id);
    if( !maya )
    {
        fprintf(stderr,
                "ToriAuxLibCache_SubmitSkeletalFromDat2: anim_maya_id=%d not in "
                "buildcache (load failed earlier)\n",
                anim_maya_id);
        return;
    }

    /* Load the bind-pose SkeletalBase from idx1 using base_id */
    struct RSCacheDat2A_SkeletalBase* skelbase =
        RSCacheDat2A_SkeletalBaseNewFromCache(ToriAuxLibCache_Dat2Disk(c), maya->base_id);

    if( !skelbase )
    {
        fprintf(stderr,
                "ToriAuxLibCache_SubmitSkeletalFromDat2: SkeletalBase load failed "
                "(anim_maya_id=%d base_id=%d)\n",
                anim_maya_id, maya->base_id);
        RSCacheDat2A_AnimMayaFree(maya);
        return;
    }

    /* Bake per-frame per-bone skinning matrix palette */
    int frame_count = 0, bone_count = 0;
    float* palette = RSCacheDat2A_SkeletalBaseBakePalette(
        maya, skelbase, &frame_count, &bone_count);

    RSCacheDat2A_SkeletalBaseFree(skelbase);
    RSCacheDat2A_AnimMayaFree(maya);

    if( !palette )
    {
        fprintf(stderr,
                "ToriAuxLibCache_SubmitSkeletalFromDat2: BakePalette failed "
                "(anim_maya_id=%d)\n",
                anim_maya_id);
        return;
    }

    struct ToriAuxLibCore_SkeletalAnim* skeletal =
        ToriAuxLibCache_SkeletalAnimNewFromBakedPalette(
            anim_maya_id, palette, frame_count, bone_count);

    if( !skeletal )
    {
        fprintf(stderr,
                "ToriAuxLibCache_SubmitSkeletalFromDat2: SkeletalAnimNew failed "
                "(anim_maya_id=%d frame_count=%d bone_count=%d)\n",
                anim_maya_id, frame_count, bone_count);
        return;
    }

    ToriAuxLibCore_SkeletalAnimAdd(ToriAuxLibCache_Core(c), anim_maya_id, skeletal);
}
