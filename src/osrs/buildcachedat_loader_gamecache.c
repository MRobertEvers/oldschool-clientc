#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/gamecache/gamecache_store.h"

#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"

#include <stdlib.h>
#include <string.h>

static int*
dup_ints_or_null(const int* src, int n)
{
    if( n <= 0 || !src )
        return NULL;
    int* out = malloc((size_t)n * sizeof(int));
    if( !out )
        return NULL;
    memcpy(out, src, (size_t)n * sizeof(int));
    return out;
}

static struct GameCacheFloorType*
dup_flotype_overlay(const struct CacheConfigOverlay* s)
{
    struct GameCacheFloorType* d = malloc(sizeof(struct GameCacheFloorType));
    if( !d )
        return NULL;
    memcpy(d, s, sizeof(struct GameCacheFloorType));
    d->flotype_name = s->flotype_name ? strdup(s->flotype_name) : NULL;
    return d;
}

static void
ensure_flotype_from_bcd(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int flotype_id)
{
    if( gamecache_get_flotype(gc, flotype_id) )
        return;
    struct CacheConfigOverlay* src = buildcachedat_get_flotype(bcd, flotype_id);
    if( !src )
        return;
    struct GameCacheFloorType* d = dup_flotype_overlay(src);
    if( d )
        gamecache_store_put_flotype(gc, flotype_id, d);
}

static void
submit_terrain_floortypes_for_chunk(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    struct CacheMapTerrain* map_terrain)
{
    int tx, tz, lv;
    for( tx = 0; tx < MAP_TERRAIN_X; tx++ )
    {
        for( tz = 0; tz < MAP_TERRAIN_Z; tz++ )
        {
            for( lv = 0; lv < MAP_TERRAIN_LEVELS; lv++ )
            {
                struct CacheMapFloor* tile =
                    &map_terrain->tiles_xyz[MAP_TILE_COORD(tx, tz, lv)];
                if( tile->underlay_id > 0 )
                    ensure_flotype_from_bcd(gc, bcd, tile->underlay_id - 1);
                int overlay_id = tile->overlay_id - 1;
                if( overlay_id != -1 )
                    ensure_flotype_from_bcd(gc, bcd, overlay_id);
            }
        }
    }
}

static struct GameCacheSequence*
dup_sequence(const struct CacheDatSequence* s)
{
    struct GameCacheSequence* d = calloc(1, sizeof(struct GameCacheSequence));
    if( !d )
        return NULL;
    d->frame_count = s->frame_count;
    d->loops = s->loops;
    d->stretches = s->stretches;
    d->priority = s->priority;
    d->replaceheldleft = s->replaceheldleft;
    d->replaceheldright = s->replaceheldright;
    d->maxloops = s->maxloops;
    d->preanim_move = s->preanim_move;
    d->postanim_move = s->postanim_move;
    d->duplicate_behavior = s->duplicate_behavior;

    int n = s->frame_count;
    if( n > 0 && s->frames )
    {
        d->frames = dup_ints_or_null(s->frames, n);
        d->iframes = dup_ints_or_null(s->iframes, n);
        d->delay = dup_ints_or_null(s->delay, n);
        d->walkmerge = dup_ints_or_null(s->walkmerge, n);
    }
    return d;
}

static struct GameCacheAnimBase*
dup_anim_base(const struct CacheAnimBase* b)
{
    if( !b )
        return NULL;
    struct GameCacheAnimBase* out = calloc(1, sizeof(struct GameCacheAnimBase));
    if( !out )
        return NULL;
    out->length = b->length;
    if( b->length <= 0 )
        return out;
    out->types = malloc((size_t)b->length * sizeof(uint8_t));
    memcpy(out->types, b->types, (size_t)b->length * sizeof(uint8_t));
    out->label_counts = malloc((size_t)b->length * sizeof(uint16_t));
    memcpy(out->label_counts, b->label_counts, (size_t)b->length * sizeof(uint16_t));
    out->labels = calloc((size_t)b->length, sizeof(uint8_t*));
    for( int i = 0; i < b->length; i++ )
    {
        int c = b->label_counts[i];
        if( c > 0 && b->labels[i] )
        {
            out->labels[i] = malloc((size_t)c * sizeof(uint8_t));
            memcpy(out->labels[i], b->labels[i], (size_t)c * sizeof(uint8_t));
        }
    }
    return out;
}

static struct GameCacheAnimframe*
dup_animframe(const struct CacheAnimframe* s)
{
    struct GameCacheAnimframe* d = calloc(1, sizeof(struct GameCacheAnimframe));
    if( !d )
        return NULL;
    d->id = s->id;
    d->base = dup_anim_base(s->base);
    d->length = s->length;
    d->delay = s->delay;
    if( s->length > 0 && s->groups )
    {
        d->groups = malloc((size_t)s->length * sizeof(int16_t));
        d->x = malloc((size_t)s->length * sizeof(int16_t));
        d->y = malloc((size_t)s->length * sizeof(int16_t));
        d->z = malloc((size_t)s->length * sizeof(int16_t));
        memcpy(d->groups, s->groups, (size_t)s->length * sizeof(int16_t));
        memcpy(d->x, s->x, (size_t)s->length * sizeof(int16_t));
        memcpy(d->y, s->y, (size_t)s->length * sizeof(int16_t));
        memcpy(d->z, s->z, (size_t)s->length * sizeof(int16_t));
    }
    return d;
}

static struct GameCacheObj*
dup_obj(const struct CacheDatConfigObj* s)
{
    struct GameCacheObj* d = calloc(1, sizeof(struct GameCacheObj));
    if( !d )
        return NULL;
    memcpy(d, s, sizeof(struct GameCacheObj));
    d->name = s->name ? strdup(s->name) : NULL;
    d->desc = s->desc ? strdup(s->desc) : NULL;
    d->recol_s = dup_ints_or_null(s->recol_s, s->recol_count);
    d->recol_d = dup_ints_or_null(s->recol_d, s->recol_count);
    d->countobj = dup_ints_or_null(s->countobj, s->countobj_count);
    d->countco = dup_ints_or_null(s->countco, s->countobj_count);
    for( int i = 0; i < 5; i++ )
    {
        d->op[i] = s->op[i] ? strdup(s->op[i]) : NULL;
        d->iop[i] = s->iop[i] ? strdup(s->iop[i]) : NULL;
    }
    return d;
}

static struct GameCacheIdk*
dup_idk(const struct CacheDatConfigIdk* s)
{
    struct GameCacheIdk* d = malloc(sizeof(struct GameCacheIdk));
    if( !d )
        return NULL;
    memcpy(d, s, sizeof(struct GameCacheIdk));
    d->models = NULL;
    if( s->models_count > 0 && s->models )
    {
        d->models = malloc((size_t)s->models_count * sizeof(int));
        memcpy(d->models, s->models, (size_t)s->models_count * sizeof(int));
    }
    return d;
}

static struct GameCacheNpc*
dup_npc(const struct CacheDatConfigNpc* s)
{
    struct GameCacheNpc* d = calloc(1, sizeof(struct GameCacheNpc));
    if( !d )
        return NULL;
    memcpy(d, s, sizeof(struct GameCacheNpc));
    d->name = s->name ? strdup(s->name) : NULL;
    d->desc = s->desc ? strdup(s->desc) : NULL;
    d->models = dup_ints_or_null(s->models, s->models_count);
    d->models_count = s->models_count;
    d->heads = dup_ints_or_null(s->heads, s->heads_count);
    d->heads_count = s->heads_count;
    d->recol_s = dup_ints_or_null(s->recol_s, s->recol_count);
    d->recol_d = dup_ints_or_null(s->recol_d, s->recol_count);
    for( int i = 0; i < 5; i++ )
        d->op[i] = s->op[i] ? strdup(s->op[i]) : NULL;
    return d;
}

static void
ensure_model_from_bcd(struct GameCache* gc, struct BuildCacheDat* bcd, int model_id)
{
    if( model_id <= 0 || gamecache_get_model(gc, model_id) )
        return;
    struct CacheModel* src = buildcachedat_get_model(bcd, model_id);
    if( !src )
        return;
    struct CacheModel* copy = model_new_copy(src);
    if( !copy )
        return;
    gamecache_store_put_model(gc, model_id, (struct GameCacheModel*)(void*)copy);
}

static void
submit_models_for_loc_config(struct GameCache* gc, struct BuildCacheDat* bcd, struct CacheConfigLocation* loc)
{
    int n = loc->shapes_and_model_count;
    if( n <= 0 || !loc->lengths || !loc->models )
        return;
    for( int i = 0; i < n; i++ )
    {
        int len = loc->lengths[i];
        if( len <= 0 || !loc->models[i] )
            continue;
        for( int j = 0; j < len; j++ )
        {
            int mid = loc->models[i][j];
            if( mid != 0 )
                ensure_model_from_bcd(gc, bcd, mid);
        }
    }
}

void
buildcachedat_loader_gamecache_submit_terrain(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    struct CacheMapTerrain* src = buildcachedat_get_map_terrain(bcd, mapx, mapz);
    if( !src )
        return;
    struct GameCacheTerrain* t = malloc(sizeof(struct GameCacheTerrain));
    if( !t )
        return;
    memcpy(t, src, sizeof(struct GameCacheTerrain));
    gamecache_store_put_terrain(gc, mapx, mapz, t);
    submit_terrain_floortypes_for_chunk(gc, bcd, src);
}

void
buildcachedat_loader_gamecache_submit_scenery(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    struct CacheMapLocs* src = buildcachedat_get_scenery(bcd, mapx, mapz);
    if( !src || src->locs_count <= 0 )
        return;

    struct GameCacheLocList* out = malloc(sizeof(struct GameCacheLocList));
    if( !out )
        return;
    out->_chunk_mapx = src->_chunk_mapx;
    out->_chunk_mapz = src->_chunk_mapz;
    out->locs_count = src->locs_count;
    out->locs = malloc((size_t)src->locs_count * sizeof(struct GameCacheLoc));
    memcpy(out->locs, src->locs, (size_t)src->locs_count * sizeof(struct GameCacheLoc));

    for( int i = 0; i < src->locs_count; i++ )
    {
        int lid = src->locs[i].loc_id;
        if( gamecache_get_config_loc(gc, lid) )
            continue;
        struct CacheConfigLocation* cl = buildcachedat_get_config_loc(bcd, lid);
        if( !cl )
            continue;
        struct CacheConfigLocation* dup = config_locs_dup(cl);
        if( dup )
        {
            gamecache_store_put_config_loc(gc, lid, (struct GameCacheLocConfig*)(void*)dup);
            submit_models_for_loc_config(gc, bcd, cl);
        }
    }

    gamecache_store_put_scenery(gc, mapx, mapz, out);
}

void
buildcachedat_loader_gamecache_submit_for_rebuild_centerzone_chunk(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    buildcachedat_loader_gamecache_submit_terrain(gc, bcd, mapx, mapz);
    buildcachedat_loader_gamecache_submit_scenery(gc, bcd, mapx, mapz);

    int* mids = NULL;
    int n = buildcachedat_loader_scenery_config_get_model_ids_mapchunk(bcd, mapx, mapz, &mids);
    for( int i = 0; i < n; i++ )
        ensure_model_from_bcd(gc, bcd, mids[i]);
    free(mids);
}

void
buildcachedat_loader_gamecache_submit_sequences(struct GameCache* gc, struct BuildCacheDat* bcd)
{
    struct DashMapIter* it = buildcachedat_iter_new_sequences(bcd);
    if( !it )
        return;
    int sid;
    struct CacheDatSequence* seq;
    while( (seq = buildcachedat_iter_next_sequence_with_id(it, &sid)) != NULL )
    {
        if( gamecache_get_sequence(gc, sid) )
            continue;
        struct GameCacheSequence* d = dup_sequence(seq);
        if( d )
            gamecache_store_put_sequence(gc, sid, d);

        for( int fi = 0; fi < seq->frame_count; fi++ )
        {
            int frame_id = seq->frames[fi];
            if( gamecache_get_animframe(gc, frame_id) )
                continue;
            struct CacheAnimframe* af = buildcachedat_get_animframe(bcd, frame_id);
            if( !af )
                continue;
            struct GameCacheAnimframe* afd = dup_animframe(af);
            if( afd )
                gamecache_store_put_animframe(gc, frame_id, afd);
        }
    }
    dashmap_iter_free(it);
}

void
buildcachedat_loader_gamecache_submit_scene_build_data(struct GameCache* gc, struct BuildCacheDat* bcd)
{
    struct DashMapIter* it = buildcachedat_iter_new_objs(bcd);
    if( it )
    {
        int oid;
        struct CacheDatConfigObj* o;
        while( (o = buildcachedat_iter_next_obj_with_id(it, &oid)) != NULL )
        {
            if( !gamecache_get_obj(gc, oid) )
            {
                struct GameCacheObj* d = dup_obj(o);
                if( d )
                    gamecache_store_put_obj(gc, oid, d);
            }
        }
        dashmap_iter_free(it);
    }

    it = buildcachedat_iter_new_idks(bcd);
    if( it )
    {
        int iid;
        struct CacheDatConfigIdk* ik;
        while( (ik = buildcachedat_iter_next_idk_with_id(it, &iid)) != NULL )
        {
            if( !gamecache_get_idk(gc, iid) )
            {
                struct GameCacheIdk* d = dup_idk(ik);
                if( d )
                    gamecache_store_put_idk(gc, iid, d);
            }
        }
        dashmap_iter_free(it);
    }

    it = buildcachedat_iter_new_npcs(bcd);
    if( it )
    {
        int nid;
        struct CacheDatConfigNpc* n;
        while( (n = buildcachedat_iter_next_npc_with_id(it, &nid)) != NULL )
        {
            if( !gamecache_get_npc(gc, nid) )
            {
                struct GameCacheNpc* d = dup_npc(n);
                if( d )
                    gamecache_store_put_npc(gc, nid, d);
            }
        }
        dashmap_iter_free(it);
    }
}

void
buildcachedat_loader_gamecache_submit_models(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    const int* model_ids,
    int model_ids_count)
{
    for( int i = 0; i < model_ids_count; i++ )
        ensure_model_from_bcd(gc, bcd, model_ids[i]);
}
