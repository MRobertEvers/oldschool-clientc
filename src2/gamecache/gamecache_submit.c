#include "gamecache_submit.h"

#include "buildcache/dat1_buildcache.h"
#include "gamecache.h"
#include "gamecache_types.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"

struct SubmitSequenceCtx
{
    struct GameCache* gamecache;
};

static void
submit_sequence_cb(
    int seq_id,
    struct CacheDatSequence* sequence,
    void* user_data)
{
    struct SubmitSequenceCtx* ctx = user_data;
    struct GameCache_Sequence* neutral =
        gamecache_sequence_new_from_cache_dat_sequence(sequence, seq_id);
    if( !neutral )
        return;
    gamecache_sequence_add(ctx->gamecache, seq_id, neutral);
}

struct SubmitFlotypeCtx
{
    struct GameCache* gamecache;
};

static void
submit_flotype_cb(
    int flo_id,
    struct CacheConfigOverlay* flotype,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct GameCache_Flotype* neutral =
        gamecache_flotype_new_from_cache_config_overlay(flotype, flo_id);
    if( !neutral )
        return;
    gamecache_flotype_add(ctx->gamecache, flo_id, neutral);
}

struct SubmitLocationCtx
{
    struct GameCache* gamecache;
};

static void
submit_location_cb(
    int loc_id,
    struct CacheConfigLocation* config_loc,
    void* user_data)
{
    struct SubmitLocationCtx* ctx = user_data;
    struct GameCache_Location* neutral =
        gamecache_location_new_from_cache_config_location(config_loc);
    if( !neutral )
        return;
    gamecache_location_add(ctx->gamecache, loc_id, neutral);
}

void
gamecache_submit_map_terrain_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int map_id)
{
    struct CacheMapTerrain* terrain = dat1_buildcache_map_terrain_get(dat1, map_id);
    if( !terrain )
        return;

    struct GameCache_MapTerrain* neutral =
        gamecache_map_terrain_new_from_cache_map_terrain(terrain);
    if( !neutral )
        return;

    gamecache_map_terrain_add(gamecache, map_id, neutral);
}

void
gamecache_submit_map_scenery_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int map_id)
{
    struct CacheMapLocs* locs = dat1_buildcache_map_scenery_get(dat1, map_id);
    if( !locs )
        return;

    struct GameCache_MapLocs* neutral = gamecache_map_locs_new_from_cache_map_locs(locs);
    if( !neutral )
        return;

    gamecache_map_scenery_add(gamecache, map_id, neutral);
}

void
gamecache_submit_animation_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int anim_id)
{
    struct CacheDatAnimBaseFrames* abf = dat1_buildcache_animbaseframes_get(dat1, anim_id);
    if( !abf )
        return;

    struct GameCache_Animation* anim =
        gamecache_animation_new_from_cache_dat_animbaseframes(abf);
    if( !anim )
        return;

    gamecache_animation_add(gamecache, anim_id, anim);
}

void
gamecache_submit_model_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int model_id)
{
    struct CacheModel* model = dat1_buildcache_model_get(dat1, model_id);
    if( !model )
        return;

    struct CacheModel* copy = model_new_copy(model);
    if( !copy )
        return;

    struct GameCache_Model* gc_model = gamecache_model_new_from_cache_model(copy);
    model_free(copy);
    if( !gc_model )
        return;

    gamecache_model_add(gamecache, model_id, gc_model);
}

void
gamecache_submit_texture(
    struct GameCache* gamecache,
    int texture_id,
    struct GameCache_Texture* texture)
{
    gamecache_texture_add(gamecache, texture_id, texture);
}

void
gamecache_submit_all_sequences_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1)
{
    struct SubmitSequenceCtx ctx = { .gamecache = gamecache };
    dat1_buildcache_foreach_sequence(dat1, submit_sequence_cb, &ctx);
}

void
gamecache_submit_all_flotypes_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1)
{
    struct SubmitFlotypeCtx ctx = { .gamecache = gamecache };
    dat1_buildcache_foreach_flotype(dat1, submit_flotype_cb, &ctx);
}

void
gamecache_submit_all_locations_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1)
{
    struct SubmitLocationCtx ctx = { .gamecache = gamecache };
    dat1_buildcache_foreach_config_loc(dat1, submit_location_cb, &ctx);
}
