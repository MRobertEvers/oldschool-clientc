#include "toriauxlibc_submit.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toriauxlib/c/toriauxlibc.h"

struct SubmitSequenceCtx
{
    struct ToriAuxLibC* c;
};

static void
submit_sequence_cb(
    int seq_id,
    struct RSCacheDat1A_ConfigSequence* sequence,
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
        ToriAuxLibC_FlotypeNewFromCacheConfigOverlay(flotype, flo_id);
    struct ToriAuxLibCore_Flotype* underlay_copy;
    if( !neutral )
        return;
    ToriAuxLibCore_FlotypeAdd(ToriAuxLibC_Core(ctx->c), flo_id, neutral);
    underlay_copy = submit_flotype_copy(neutral);
    if( underlay_copy )
        ToriAuxLibCore_UnderlayAdd(ToriAuxLibC_Core(ctx->c), flo_id, underlay_copy);
}

struct SubmitLocationCtx
{
    struct ToriAuxLibC* c;
};

static void
submit_location_cb(
    int loc_id,
    struct RSCacheDat2A_ConfigLocation* config_loc,
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
    struct RSCacheDat2A_MapTerrain* terrain = dat1_buildcache_map_terrain_get(dat1(c), map_id);
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
    struct RSCacheDat2A_MapLocs* locs = dat1_buildcache_map_scenery_get(dat1(c), map_id);
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
    struct RSCacheDat1A_AnimBaseFrames* abf = dat1_buildcache_animbaseframes_take(dat1(c), anim_id);
    if( !abf )
        return;

    struct ToriAuxLibCore_Animation* anim = ToriAuxLibC_AnimationNewFromCacheDatAnimbaseframes(abf);
    if( !anim )
        return;

    ToriAuxLibCore_AnimationAdd(ToriAuxLibC_Core(c), anim_id, anim);
}

void
ToriAuxLibC_SubmitModelFromDat1(
    struct ToriAuxLibC* c,
    int model_id)
{
    struct RSCacheDat2A_Model* model = dat1_buildcache_model_get(dat1(c), model_id);
    if( !model )
        return;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibC_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
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

void
ToriAuxLibC_SubmitSpriteFromDat1(
    struct ToriAuxLibC* c,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite)
{
    if( !c || !sprite )
        return;
    ToriAuxLibCore_SpriteAdd(ToriAuxLibC_Core(c), sprite_id, sprite);
}

void
ToriAuxLibC_SubmitFontFromDat1(
    struct ToriAuxLibC* c,
    int font_id,
    struct ToriAuxLibCore_Font* font)
{
    if( !c || !font )
        return;
    ToriAuxLibCore_FontAdd(ToriAuxLibC_Core(c), font_id, font);
}

void
ToriAuxLibC_SubmitAllComponentsFromDat1(struct ToriAuxLibC* c)
{
    if( !c )
        return;

    struct RSCacheDat1A_ConfigComponentList* interfaces = dat1_buildcache_get_interfaces(dat1(c));
    if( !interfaces || !interfaces->components )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibC_Core(c);
    for( int i = 0; i < interfaces->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* comp = interfaces->components[i];
        if( !comp )
            continue;

        struct ToriAuxLibCore_Component* neutral = ToriAuxLibC_ComponentNewFromCacheComponent(comp);
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
        ToriAuxLibC_SequenceNewFromCacheDat2Sequence(sequence, seq_id);
    if( !neutral )
        return;
    ToriAuxLibCore_SequenceAdd(ToriAuxLibC_Core(ctx->c), seq_id, neutral);
}

static void
submit_dat2_underlay_cb(
    int underlay_id,
    struct RSCacheDat2A_ConfigUnderlay* underlay,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibC_UnderlayNewFromCacheConfigUnderlay(underlay, underlay_id);
    if( !neutral )
        return;
    ToriAuxLibCore_UnderlayAdd(ToriAuxLibC_Core(ctx->c), underlay_id, neutral);
}

void
ToriAuxLibC_SubmitMapTerrainFromDat2(
    struct ToriAuxLibC* c,
    int map_id)
{
    struct RSCacheDat2A_MapTerrain* terrain = dat2_buildcache_map_terrain_get(dat2(c), map_id);
    if( !terrain )
        return;

    struct ToriAuxLibCore_MapTerrain* neutral =
        ToriAuxLibC_MapTerrainNewFromCacheMapTerrain(terrain);
    if( !neutral )
        return;

    ToriAuxLibCore_MapTerrainAdd(ToriAuxLibC_Core(c), map_id, neutral);
}

void
ToriAuxLibC_SubmitMapSceneryFromDat2(
    struct ToriAuxLibC* c,
    int map_id)
{
    struct RSCacheDat2A_MapLocs* locs = dat2_buildcache_map_scenery_get(dat2(c), map_id);
    if( !locs )
        return;

    struct ToriAuxLibCore_MapLocs* neutral = ToriAuxLibC_MapLocsNewFromCacheMapLocs(locs);
    if( !neutral )
        return;

    ToriAuxLibCore_MapSceneryAdd(ToriAuxLibC_Core(c), map_id, neutral);
}

void
ToriAuxLibC_SubmitModelFromDat2(
    struct ToriAuxLibC* c,
    int model_id)
{
    struct RSCacheDat2A_Model* model = dat2_buildcache_model_get(dat2(c), model_id);
    if( !model )
        return;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibC_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !gc_model )
        return;

    ToriAuxLibCore_ModelAdd(ToriAuxLibC_Core(c), model_id, gc_model);
}

void
ToriAuxLibC_SubmitAllSequencesFromDat2(struct ToriAuxLibC* c)
{
    struct SubmitSequenceCtx ctx = { .c = c };
    dat2_buildcache_foreach_sequence(dat2(c), submit_dat2_sequence_cb, &ctx);
}

void
ToriAuxLibC_SubmitAllFlotypesFromDat2(struct ToriAuxLibC* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat2_buildcache_foreach_flotype(dat2(c), submit_flotype_cb, &ctx);
}

void
ToriAuxLibC_SubmitAllUnderlaysFromDat2(struct ToriAuxLibC* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat2_buildcache_foreach_underlay(dat2(c), submit_dat2_underlay_cb, &ctx);
}

void
ToriAuxLibC_SubmitAllLocationsFromDat2(struct ToriAuxLibC* c)
{
    struct SubmitLocationCtx ctx = { .c = c };
    dat2_buildcache_foreach_config_loc(dat2(c), submit_location_cb, &ctx);
}
