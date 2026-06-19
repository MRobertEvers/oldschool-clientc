#include "toriauxlibc_submit.h"

#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"

struct SubmitSequenceCtx
{
    struct ToriAuxLibC* c;
};

static void
submit_sequence_cb(
    int seq_id,
    struct CacheDatSequence* sequence,
    void* user_data)
{
    struct SubmitSequenceCtx* ctx = user_data;
    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibC_SequenceNewFromCacheDatSequence(sequence, seq_id);
    if( !neutral )
        return;
    ToriAuxLibCore_SequenceAdd(ToriAuxLibC_Core(ctx->c), seq_id, neutral);
}

struct SubmitFlotypeCtx
{
    struct ToriAuxLibC* c;
};

static void
submit_flotype_cb(
    int flo_id,
    struct CacheConfigOverlay* flotype,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibC_FlotypeNewFromCacheConfigOverlay(flotype, flo_id);
    if( !neutral )
        return;
    ToriAuxLibCore_FlotypeAdd(ToriAuxLibC_Core(ctx->c), flo_id, neutral);
}

struct SubmitLocationCtx
{
    struct ToriAuxLibC* c;
};

static void
submit_location_cb(
    int loc_id,
    struct CacheConfigLocation* config_loc,
    void* user_data)
{
    struct SubmitLocationCtx* ctx = user_data;
    struct ToriAuxLibCore_Location* neutral =
        ToriAuxLibC_LocationNewFromCacheConfigLocation(config_loc);
    if( !neutral )
        return;
    ToriAuxLibCore_LocationAdd(ToriAuxLibC_Core(ctx->c), loc_id, neutral);
}

void
ToriAuxLibC_SubmitMapTerrainFromDat1(
    struct ToriAuxLibC* c,
    int map_id)
{
    struct CacheMapTerrain* terrain = dat1_buildcache_map_terrain_get(dat1(c), map_id);
    if( !terrain )
        return;

    struct ToriAuxLibCore_MapTerrain* neutral =
        ToriAuxLibC_MapTerrainNewFromCacheMapTerrain(terrain);
    if( !neutral )
        return;

    ToriAuxLibCore_MapTerrainAdd(ToriAuxLibC_Core(c), map_id, neutral);
}

void
ToriAuxLibC_SubmitMapSceneryFromDat1(
    struct ToriAuxLibC* c,
    int map_id)
{
    struct CacheMapLocs* locs = dat1_buildcache_map_scenery_get(dat1(c), map_id);
    if( !locs )
        return;

    struct ToriAuxLibCore_MapLocs* neutral = ToriAuxLibC_MapLocsNewFromCacheMapLocs(locs);
    if( !neutral )
        return;

    ToriAuxLibCore_MapSceneryAdd(ToriAuxLibC_Core(c), map_id, neutral);
}

void
ToriAuxLibC_SubmitAnimationFromDat1(
    struct ToriAuxLibC* c,
    int anim_id)
{
    struct CacheDatAnimBaseFrames* abf = dat1_buildcache_animbaseframes_get(dat1(c), anim_id);
    if( !abf )
        return;

    struct ToriAuxLibCore_Animation* anim =
        ToriAuxLibC_AnimationNewFromCacheDatAnimbaseframes(abf);
    if( !anim )
        return;

    ToriAuxLibCore_AnimationAdd(ToriAuxLibC_Core(c), anim_id, anim);
}

void
ToriAuxLibC_SubmitModelFromDat1(
    struct ToriAuxLibC* c,
    int model_id)
{
    struct CacheModel* model = dat1_buildcache_model_get(dat1(c), model_id);
    if( !model )
        return;

    struct CacheModel* copy = model_new_copy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibC_ModelNewFromCacheModel(copy);
    model_free(copy);
    if( !gc_model )
        return;

    ToriAuxLibCore_ModelAdd(ToriAuxLibC_Core(c), model_id, gc_model);
}

void
ToriAuxLibC_SubmitTexture(
    struct ToriAuxLibC* c,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture)
{
    ToriAuxLibCore_TextureAdd(ToriAuxLibC_Core(c), texture_id, texture);
}

void
ToriAuxLibC_SubmitAllSequencesFromDat1(struct ToriAuxLibC* c)
{
    struct SubmitSequenceCtx ctx = { .c = c };
    dat1_buildcache_foreach_sequence(dat1(c), submit_sequence_cb, &ctx);
}

void
ToriAuxLibC_SubmitAllFlotypesFromDat1(struct ToriAuxLibC* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat1_buildcache_foreach_flotype(dat1(c), submit_flotype_cb, &ctx);
}

void
ToriAuxLibC_SubmitAllLocationsFromDat1(struct ToriAuxLibC* c)
{
    struct SubmitLocationCtx ctx = { .c = c };
    dat1_buildcache_foreach_config_loc(dat1(c), submit_location_cb, &ctx);
}
