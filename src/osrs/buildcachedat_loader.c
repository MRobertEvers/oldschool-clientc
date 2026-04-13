#include "buildcachedat_loader.h"

#include "datastruct/vec.h"
#include "graphics/dash.h"
#include "osrs/dash_utils.h"
#include "osrs/minimap.h"
#include "osrs/painters.h"
#include "osrs/rscache/archive.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_npc.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/texture.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/scene2.h"
#include "osrs/varp_varbit_manager.h"
#include "osrs/world.h"

#include <assert.h>

void
LibToriRS_WorldMinimapStaticRebuild(struct GGame* game);
#include <stdlib.h>
#include <string.h>

static int
int_cmp(const void* a, const void* b)
{
    int x = *(const int*)a;
    int y = *(const int*)b;
    if( x < y )
        return -1;
    if( x > y )
        return 1;
    return 0;
}

/** In-place unique on sorted [0..n); returns new length. */
static int
unique_sorted_int(int* arr, int n)
{
    if( n <= 0 )
        return 0;
    int w = 0;
    for( int r = 1; r < n; r++ )
    {
        if( arr[r] != arr[w] )
            arr[++w] = arr[r];
    }
    return w + 1;
}

static int
loader_uiscene_attach_single_sprite_owned(struct UIScene* ui, struct DashSprite* sprite)
{
    if( !ui || !sprite )
        return -1;
    int eid = uiscene_element_acquire(ui, -1);
    if( eid < 0 )
        return -1;
    struct UISceneElement* el = uiscene_element_at(ui, eid);
    if( !el )
        return -1;
    struct DashSprite** row = malloc(sizeof(struct DashSprite*));
    if( !row )
        return -1;
    row[0] = sprite;
    el->dash_sprites = row;
    el->dash_sprites_count = 1;
    el->dash_sprites_borrowed = false;
    return eid;
}

void
buildcachedat_loader_set_2d_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data)
{
    struct FileListDat* jagfile = filelist_dat_new_from_decode(data, data_size);
    buildcachedat_set_2d_media_jagfile(buildcachedat, jagfile);
}

void
buildcachedat_loader_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data)
{
    struct FileListDat* jagfile = filelist_dat_new_from_decode(data, data_size);
    buildcachedat_set_config_jagfile(buildcachedat, jagfile);
}

void
buildcachedat_loader_init_varp_varbit(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game)
{
    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(buildcachedat);
    if( config_jagfile )
        varp_varbit_load_from_config_jagfile(&game->varp_varbit, config_jagfile);
}

void
buildcachedat_loader_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data)
{
    struct FileListDat* jagfile = filelist_dat_new_from_decode(data, data_size);
    buildcachedat_set_versionlist_jagfile(buildcachedat, jagfile);
}

void
buildcachedat_loader_cache_map_terrain_mapid(
    struct BuildCacheDat* buildcachedat,
    int map_id,
    int data_size,
    void* data)
{
    int map_x = (map_id >> 16) & 0xFFFF;
    int map_z = map_id & 0xFFFF;

    struct CacheMapTerrain* terrain =
        map_terrain_new_from_decode_flags(data, data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);

    buildcachedat_add_map_terrain(buildcachedat, map_x, map_z, terrain);
}

void
buildcachedat_loader_cache_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data)
{
    int map_x = param_b >> 16;
    int map_z = param_b & 0xFFFF;

    struct CacheMapTerrain* terrain =
        map_terrain_new_from_decode_flags(data, data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);

    buildcachedat_add_map_terrain(buildcachedat, map_x, map_z, terrain);
}

void
buildcachedat_loader_cache_map_scenery_mapid(
    struct BuildCacheDat* buildcachedat,
    int map_id,
    int data_size,
    void* data)
{
    int mapx = map_id >> 16;
    int mapz = map_id & 0xFFFF;
    struct CacheMapLocs* locs = map_locs_new_from_decode(data, data_size);
    locs->_chunk_mapx = mapx;
    locs->_chunk_mapz = mapz;
    buildcachedat_add_scenery(buildcachedat, mapx, mapz, locs);
}

void
buildcachedat_loader_cache_map_scenery(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data)
{
    int mapx = param_b >> 16;
    int mapz = param_b & 0xFFFF;
    struct CacheMapLocs* locs = map_locs_new_from_decode(data, data_size);
    locs->_chunk_mapx = mapx;
    locs->_chunk_mapz = mapz;
    buildcachedat_add_scenery(buildcachedat, mapx, mapz, locs);
}

void
buildcachedat_loader_init_floortypes_from_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "flo.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSBuffer buffer = {
        .data = config_jagfile->files[data_file_idx],
        .size = config_jagfile->file_sizes[data_file_idx],
        .position = 0,
    };

    int count = g2(&buffer);
    for( int i = 0; i < count; i++ )
    {
        struct CacheConfigOverlay* flotype = malloc(sizeof(struct CacheConfigOverlay));
        memset(flotype, 0, sizeof(struct CacheConfigOverlay));
        flotype->_id = i;

        buffer.position += config_floortype_overlay_decode_inplace(
            flotype, buffer.data + buffer.position, buffer.size - buffer.position);

        buildcachedat_add_flotype(buildcachedat, i, flotype);
    }
}

void
buildcachedat_loader_init_scenery_configs_from_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.dat");
    int index_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.idx");

    assert(data_file_idx != -1 && "Data file must be found");
    assert(index_file_idx != -1 && "Index file must be found");

    struct FileListDatIndexed* filelist_indexed = filelist_dat_indexed_new_from_decode(
        config_jagfile->files[index_file_idx],
        config_jagfile->file_sizes[index_file_idx],
        config_jagfile->files[data_file_idx],
        config_jagfile->file_sizes[data_file_idx]);

    struct DashMapIter* iter = buildcachedat_iter_new_scenery(buildcachedat);
    struct CacheMapLocs* locs = NULL;
    while( (locs = buildcachedat_iter_next_scenery(iter)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheMapLoc* loc = &locs->locs[i];
            assert(loc->loc_id != -1 && "Loc id must be valid");
            assert(loc->loc_id < filelist_indexed->offset_count && "Loc id must be within range");

            if( buildcachedat_get_config_loc(buildcachedat, loc->loc_id) )
                continue;

            struct CacheConfigLocation* config_loc = malloc(sizeof(struct CacheConfigLocation));
            memset(config_loc, 0, sizeof(struct CacheConfigLocation));

            int offset = filelist_indexed->offsets[loc->loc_id];

            config_locs_decode_inplace(
                config_loc,
                filelist_indexed->data + offset,
                filelist_indexed->data_size - offset,
                CONFIG_LOC_DECODE_DAT);

            config_loc->_id = loc->loc_id;

            buildcachedat_add_config_loc(buildcachedat, loc->loc_id, config_loc);
        }
    }
    dashmap_iter_free(iter);
    filelist_dat_indexed_free(filelist_indexed);
}

int
buildcachedat_loader_get_all_scenery_locs(
    struct BuildCacheDat* buildcachedat,
    int** loc_ids_out,
    int** chunk_x_out,
    int** chunk_z_out)
{
    struct Vec* loc_ids = vec_new(sizeof(int), 512);
    struct Vec* chunk_x = vec_new(sizeof(int), 512);
    struct Vec* chunk_z = vec_new(sizeof(int), 512);

    struct DashMapIter* iter = buildcachedat_iter_new_scenery(buildcachedat);
    struct CacheMapLocs* locs = NULL;
    while( (locs = buildcachedat_iter_next_scenery(iter)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheMapLoc* loc = &locs->locs[i];
            vec_push(loc_ids, &loc->loc_id);
            vec_push(chunk_x, &locs->_chunk_mapx);
            vec_push(chunk_z, &locs->_chunk_mapz);
        }
    }
    dashmap_iter_free(iter);

    int count = vec_size(loc_ids);
    *loc_ids_out = malloc(count * sizeof(int));
    *chunk_x_out = malloc(count * sizeof(int));
    *chunk_z_out = malloc(count * sizeof(int));

    memcpy(*loc_ids_out, vec_data(loc_ids), count * sizeof(int));
    memcpy(*chunk_x_out, vec_data(chunk_x), count * sizeof(int));
    memcpy(*chunk_z_out, vec_data(chunk_z), count * sizeof(int));

    vec_free(loc_ids);
    vec_free(chunk_x);
    vec_free(chunk_z);

    return count;
}

int
buildcachedat_loader_get_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    int loc_id,
    int** model_ids_out)
{
    struct CacheConfigLocation* config_loc = buildcachedat_get_config_loc(buildcachedat, loc_id);
    if( !config_loc || !config_loc->models )
    {
        *model_ids_out = NULL;
        return 0;
    }

    struct Vec* model_ids = vec_new(sizeof(int), 16);

    int** model_id_sets = config_loc->models;
    int* lengths = config_loc->lengths;
    int* shapes = config_loc->shapes;
    int shapes_and_model_count = config_loc->shapes_and_model_count;

    if( !shapes )
    {
        int count = lengths[0];
        for( int i = 0; i < count; i++ )
        {
            int model_id = model_id_sets[0][i];
            if( model_id )
                vec_push(model_ids, &model_id);
        }
    }
    else
    {
        for( int i = 0; i < shapes_and_model_count; i++ )
        {
            int count_inner = lengths[i];
            for( int j = 0; j < count_inner; j++ )
            {
                int model_id = model_id_sets[i][j];
                if( model_id )
                    vec_push(model_ids, &model_id);
            }
        }
    }

    int count = vec_size(model_ids);
    *model_ids_out = malloc(count * sizeof(int));
    memcpy(*model_ids_out, vec_data(model_ids), count * sizeof(int));
    vec_free(model_ids);

    return count;
}

int
buildcachedat_loader_get_all_unique_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    int** model_ids_out)
{
    *model_ids_out = NULL;
    if( !buildcachedat )
        return 0;

    struct Vec* locs_vec = vec_new(sizeof(int), 512);
    struct DashMapIter* iter = buildcachedat_iter_new_scenery(buildcachedat);
    struct CacheMapLocs* locs = NULL;
    while( (locs = buildcachedat_iter_next_scenery(iter)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheMapLoc* loc = &locs->locs[i];
            vec_push(locs_vec, &loc->loc_id);
        }
    }
    dashmap_iter_free(iter);

    int nloc = vec_size(locs_vec);
    if( nloc == 0 )
    {
        vec_free(locs_vec);
        return 0;
    }

    int* loc_arr = malloc((size_t)nloc * sizeof(int));
    if( !loc_arr )
    {
        vec_free(locs_vec);
        return 0;
    }
    memcpy(loc_arr, vec_data(locs_vec), (size_t)nloc * sizeof(int));
    vec_free(locs_vec);

    qsort(loc_arr, (size_t)nloc, sizeof(int), int_cmp);
    int nunique_loc = unique_sorted_int(loc_arr, nloc);

    struct Vec* models = vec_new(sizeof(int), 1024);
    for( int li = 0; li < nunique_loc; li++ )
    {
        int* mids = NULL;
        int nm = buildcachedat_loader_get_scenery_model_ids(buildcachedat, loc_arr[li], &mids);
        for( int mi = 0; mi < nm; mi++ )
            vec_push(models, &mids[mi]);
        free(mids);
    }
    free(loc_arr);

    int nm_total = vec_size(models);
    if( nm_total == 0 )
    {
        vec_free(models);
        return 0;
    }

    int* marr = malloc((size_t)nm_total * sizeof(int));
    if( !marr )
    {
        vec_free(models);
        return 0;
    }
    memcpy(marr, vec_data(models), (size_t)nm_total * sizeof(int));
    vec_free(models);

    qsort(marr, (size_t)nm_total, sizeof(int), int_cmp);
    int nunique_m = unique_sorted_int(marr, nm_total);

    if( nunique_m < nm_total )
    {
        int* shrink = realloc(marr, (size_t)nunique_m * sizeof(int));
        if( shrink )
            marr = shrink;
    }
    *model_ids_out = marr;
    return nunique_m;
}

void
buildcachedat_loader_cache_model(
    struct BuildCacheDat* buildcachedat,
    int model_id,
    int data_size,
    void* data)
{
    struct CacheModel* model = model_new_decode(data, data_size);
    buildcachedat_add_model(buildcachedat, model_id, model);
}

void
buildcachedat_loader_cache_textures(
    struct BuildCacheDat* buildcachedat,
    struct Scene2* scene2,
    int data_size,
    void* data)
{
    assert(scene2 != NULL && "Textures must load into Scene2");

    struct FileListDat* filelist = filelist_dat_new_from_decode(data, data_size);

    // Hardcoded to 50 in the deob. Not sure why.
    for( int i = 0; i < 50; i++ )
    {
        struct CacheDatTexture* texture = cache_dat_texture_new_from_filelist_dat(filelist, i, 0);

        int animation_direction = TEXANIM_DIRECTION_NONE;
        int animation_speed = 0;

        // In old revisions (e.g. 245.2) the animated textures were hardcoded.
        if( i == 17 || i == 24 )
        {
            animation_direction = TEXANIM_DIRECTION_V_DOWN;
            animation_speed = 2;
        }

        struct DashTexture* dash_texture =
            texture_new_from_texture_sprite(texture, animation_direction, animation_speed);
        assert(dash_texture != NULL);

        scene2_texture_add(scene2, i, dash_texture);
        buildcachedat_add_texture_ref(buildcachedat, i);
        cache_dat_texture_free(texture);
    }

    filelist_dat_free(filelist);
}

void
buildcachedat_loader_init_sequences_from_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "seq.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSBuffer buffer = { .data = config_jagfile->files[data_file_idx],
                               .size = config_jagfile->file_sizes[data_file_idx],
                               .position = 0 };

    int count = g2(&buffer);
    buildcachedat_reserve_hmap(buildcachedat->sequences_hmap, (size_t)count);
    for( int i = 0; i < count; i++ )
    {
        struct CacheDatSequence* sequence = malloc(sizeof(struct CacheDatSequence));
        memset(sequence, 0, sizeof(struct CacheDatSequence));

        buffer.position += config_dat_sequence_decode_inplace(
            sequence, buffer.data + buffer.position, buffer.size - buffer.position);

        buildcachedat_add_sequence(buildcachedat, i, sequence);
    }
}

int
buildcachedat_loader_get_animbaseframes_count_from_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* versionlist_jagfile = buildcachedat_versionlist_jagfile(buildcachedat);
    assert(versionlist_jagfile != NULL && "Versionlist jagfile must be loaded");

    int index_file_idx = filelist_dat_find_file_by_name(versionlist_jagfile, "anim_index");
    if( index_file_idx == -1 )
        return 264; // Fallback default

    return 264; // Hardcoded for now
}

void
buildcachedat_loader_cache_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    int data_size,
    void* data)
{
    struct CacheDatAnimBaseFrames* animbaseframes =
        cache_dat_animbaseframes_new_decode(data, data_size);

    buildcachedat_add_animbaseframes(buildcachedat, animbaseframes_id, animbaseframes);

    for( int i = 0; i < animbaseframes->frame_count; i++ )
    {
        struct CacheAnimframe* animframe = &animbaseframes->frames[i];
        buildcachedat_add_animframe_ref(
            buildcachedat, animframe->id, animbaseframes_id, i);
    }
}

static struct DashSprite*
load_sprite_pix8(
    struct FileListDat* filelist,
    const char* filename,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix8Palette* pix8_palette = NULL;
    struct DashSprite* sprite = NULL;
    int data_file_idx = filelist_dat_find_file_by_name(filelist, filename);
    if( data_file_idx == -1 )
    {
        return NULL;
    }

    pix8_palette = cache_dat_pix8_palette_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);

    sprite = dashsprite_new_from_cache_pix8_palette(pix8_palette);
    cache_dat_pix8_palette_free(pix8_palette);

    return sprite;
}

static struct DashSprite*
load_sprite_pix32(
    struct FileListDat* filelist,
    const char* filename,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix32* pix32 = NULL;
    struct DashSprite* sprite = NULL;
    int data_file_idx = filelist_dat_find_file_by_name(filelist, filename);
    if( data_file_idx == -1 )
    {
        return NULL;
    }

    pix32 = cache_dat_pix32_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix32 )
    {
        return NULL;
    }
    sprite = dashsprite_new_from_cache_pix32(pix32);
    cache_dat_pix32_free(pix32);
    return sprite;
}

void
buildcachedat_loader_cache_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data)
{
    // struct FileListDat* filelist = filelist_dat_new_from_decode(data, data_size);

    // int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    // assert(index_file_idx != -1 && "Failed to find index.dat in filelist");

    // game->sprite_invback = load_sprite_pix8(filelist, "invback.dat", index_file_idx, 0);
    // game->sprite_chatback = load_sprite_pix8(filelist, "chatback.dat", index_file_idx, 0);
    // game->sprite_mapback = load_sprite_pix8(filelist, "mapback.dat", index_file_idx, 0);
    // game->sprite_backbase1 = load_sprite_pix8(filelist, "backbase1.dat", index_file_idx, 0);
    // game->sprite_backbase2 = load_sprite_pix8(filelist, "backbase2.dat", index_file_idx, 0);
    // game->sprite_backhmid1 = load_sprite_pix8(filelist, "backhmid1.dat", index_file_idx, 0);

    // for( int i = 0; i < 13; i++ )
    // {
    //     game->sprite_sideicons[i] = load_sprite_pix8(filelist, "sideicons.dat", index_file_idx,
    //     i);
    // }

    // game->sprite_compass = load_sprite_pix32(filelist, "compass.dat", index_file_idx, 0);
    // game->sprite_mapedge = load_sprite_pix32(filelist, "mapedge.dat", index_file_idx, 0);

    // for( int i = 0; i < 50; i++ )
    // {
    //     game->sprite_mapscene[i] = load_sprite_pix8(filelist, "mapscene.dat", index_file_idx, i);
    // }

    // for( int i = 0; i < 50; i++ )
    // {
    //     game->sprite_mapfunction[i] =
    //         load_sprite_pix32(filelist, "mapfunction.dat", index_file_idx, i);
    // }

    // for( int i = 0; i < 20; i++ )
    // {
    //     game->sprite_hitmarks[i] = load_sprite_pix32(filelist, "hitmarks.dat", index_file_idx,
    //     i); game->sprite_headicons[i] = load_sprite_pix32(filelist, "headicons.dat",
    //     index_file_idx, i);
    // }

    // game->sprite_mapmarker0 = load_sprite_pix32(filelist, "mapmarker.dat", index_file_idx, 0);
    // game->sprite_mapmarker1 = load_sprite_pix32(filelist, "mapmarker.dat", index_file_idx, 1);

    // for( int i = 0; i < 8; i++ )
    // {
    //     game->sprite_cross[i] = load_sprite_pix32(filelist, "cross.dat", index_file_idx, i);
    // }

    // game->sprite_mapdot0 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 0);
    // game->sprite_mapdot1 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 1);
    // game->sprite_mapdot2 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 2);
    // game->sprite_mapdot3 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 3);

    // game->sprite_scrollbar0 = load_sprite_pix8(filelist, "scrollbar.dat", index_file_idx, 0);
    // game->sprite_scrollbar1 = load_sprite_pix8(filelist, "scrollbar.dat", index_file_idx, 1);

    // game->sprite_redstone1 = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    // game->sprite_redstone2 = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    // game->sprite_redstone3 = load_sprite_pix8(filelist, "redstone3.dat", index_file_idx, 0);
    // game->sprite_redstone1h = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    // game->sprite_redstone2h = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    // game->sprite_redstone1v = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    // game->sprite_redstone2v = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    // game->sprite_redstone3v = load_sprite_pix8(filelist, "redstone3.dat", index_file_idx, 0);
    // game->sprite_redstone1hv = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    // game->sprite_redstone2hv = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    // /* Client.ts: h = flipHorizontally, v = flipVertically, hv = both */
    // if( game->sprite_redstone1h )
    //     dashsprite_flip_horizontal(game->sprite_redstone1h);
    // if( game->sprite_redstone2h )
    //     dashsprite_flip_horizontal(game->sprite_redstone2h);
    // if( game->sprite_redstone1v )
    //     dashsprite_flip_vertical(game->sprite_redstone1v);
    // if( game->sprite_redstone2v )
    //     dashsprite_flip_vertical(game->sprite_redstone2v);
    // if( game->sprite_redstone3v )
    //     dashsprite_flip_vertical(game->sprite_redstone3v);
    // if( game->sprite_redstone1hv )
    // {
    //     dashsprite_flip_horizontal(game->sprite_redstone1hv);
    //     dashsprite_flip_vertical(game->sprite_redstone1hv);
    // }
    // if( game->sprite_redstone2hv )
    // {
    //     dashsprite_flip_horizontal(game->sprite_redstone2hv);
    //     dashsprite_flip_vertical(game->sprite_redstone2hv);
    // }

    // for( int i = 0; i < 2; i++ )
    // {
    //     game->sprite_modicons[i] = load_sprite_pix8(filelist, "mod_icons.dat", index_file_idx,
    //     i);
    // }

    // game->sprite_backleft1 = load_sprite_pix32(filelist, "backleft1.dat", index_file_idx, 0);
    // game->sprite_backleft2 = load_sprite_pix8(filelist, "backleft2.dat", index_file_idx, 0);
    // game->sprite_backright1 = load_sprite_pix8(filelist, "backright1.dat", index_file_idx, 0);
    // game->sprite_backright2 = load_sprite_pix8(filelist, "backright2.dat", index_file_idx, 0);
    // game->sprite_backtop1 = load_sprite_pix8(filelist, "backtop1.dat", index_file_idx, 0);
    // game->sprite_backvmid1 = load_sprite_pix8(filelist, "backvmid1.dat", index_file_idx, 0);
    // game->sprite_backvmid2 = load_sprite_pix8(filelist, "backvmid2.dat", index_file_idx, 0);
    // game->sprite_backvmid3 = load_sprite_pix8(filelist, "backvmid3.dat", index_file_idx, 0);
    // game->sprite_backhmid2 = load_sprite_pix8(filelist, "backhmid2.dat", index_file_idx, 0);

    // game->media_filelist = filelist;
}

/** Try to load a single component sprite by name (e.g. "miscgraphics,0") from media filelist.
 */
static void
load_one_component_sprite(
    struct BuildCacheDat* buildcachedat,
    struct UIScene* ui_scene,
    struct FileListDat* filelist,
    int index_file_idx,
    const char* sprite_name)
{
    char filename_buf[256];
    int sprite_idx = 0;
    if( !sprite_name || !sprite_name[0] || !ui_scene )
        return;
    if( buildcachedat_get_component_sprite_element_id(buildcachedat, sprite_name) >= 0 )
        return; /* already loaded */
    if( sscanf(sprite_name, "%255[^,],%d", filename_buf, &sprite_idx) != 2 )
    {
        snprintf(filename_buf, sizeof(filename_buf), "%s", sprite_name);
        sprite_idx = 0;
    }
    size_t len = strlen(filename_buf);
    if( len + 5 > sizeof(filename_buf) )
        return;
    if( len < 4 || strcmp(filename_buf + len - 4, ".dat") != 0 )
    {
        memcpy(filename_buf + len, ".dat", 4);
        filename_buf[len + 4] = '\0';
    }
    struct DashSprite* sprite =
        load_sprite_pix32(filelist, filename_buf, index_file_idx, sprite_idx);
    if( !sprite )
        sprite = load_sprite_pix8(filelist, filename_buf, index_file_idx, sprite_idx);
    if( sprite )
    {
        int eid = loader_uiscene_attach_single_sprite_owned(ui_scene, sprite);
        if( eid >= 0 )
            buildcachedat_add_component_sprite_ref(buildcachedat, sprite_name, eid);
        else
            dashsprite_free(sprite);
    }
}

void
buildcachedat_loader_load_component_sprites_from_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game)
{
    struct FileListDat* filelist = buildcachedat->cfg_media_jagfile;
    if( !filelist || !game || !game->ui_scene )
        return;
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    if( index_file_idx == -1 )
        return;

    struct DashMapIter* iter = buildcachedat_component_iter_new(buildcachedat);
    int id;
    struct CacheDatConfigComponent* component;
    while( (component = buildcachedat_component_iter_next(iter, &id)) != NULL )
    {
        load_one_component_sprite(
            buildcachedat, game->ui_scene, filelist, index_file_idx, component->graphic);
        load_one_component_sprite(
            buildcachedat, game->ui_scene, filelist, index_file_idx, component->activeGraphic);
        if( component->invSlotGraphic )
        {
            for( int i = 0; i < 20; i++ )
            {
                if( component->invSlotGraphic[i] )
                    load_one_component_sprite(
                        buildcachedat,
                        game->ui_scene,
                        filelist,
                        index_file_idx,
                        component->invSlotGraphic[i]);
            }
        }
    }
    dashmap_iter_free(iter);
}

void
buildcachedat_loader_cache_title(
    struct BuildCacheDat* buildcachedat,
    struct UIScene* ui_scene,
    int data_size,
    void* data)
{
    assert(ui_scene != NULL && "Fonts load into UIScene");

    struct DashPixFont* font = NULL;
    struct FileListDat* filelist = filelist_dat_new_from_decode(data, data_size);

    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    assert(index_file_idx != -1 && "Failed to find index.dat in filelist");

    int data_file_idx = filelist_dat_find_file_by_name(filelist, "b12.dat");
    assert(data_file_idx != -1 && "Failed to find b12.dat in filelist");
    struct CacheDatPixfont* pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);

    font = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    buildcachedat_add_font_ref(buildcachedat, "b12", uiscene_font_add(ui_scene, "b12", font));
    cache_dat_pixfont_free(pixfont);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "p12.dat");
    assert(data_file_idx != -1);
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    font = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    buildcachedat_add_font_ref(buildcachedat, "p12", uiscene_font_add(ui_scene, "p12", font));
    cache_dat_pixfont_free(pixfont);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "p11.dat");
    assert(data_file_idx != -1);
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    font = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    buildcachedat_add_font_ref(buildcachedat, "p11", uiscene_font_add(ui_scene, "p11", font));
    cache_dat_pixfont_free(pixfont);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "q8.dat");
    assert(data_file_idx != -1);
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    font = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    buildcachedat_add_font_ref(buildcachedat, "q8", uiscene_font_add(ui_scene, "q8", font));
    cache_dat_pixfont_free(pixfont);

    filelist_dat_free(filelist);
}

void
buildcachedat_loader_init_idkits_from_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* filelist = buildcachedat_config_jagfile(buildcachedat);

    int data_file_idx = filelist_dat_find_file_by_name(filelist, "idk.dat");
    assert(data_file_idx != -1 && "Failed to find idk.dat in filelist");

    struct CacheDatConfigIdkList* idk_list = cache_dat_config_idk_list_new_decode(
        filelist->files[data_file_idx], filelist->file_sizes[data_file_idx]);

    buildcachedat_reserve_hmap(buildcachedat->idk_hmap, (size_t)idk_list->idks_count);

    for( int i = 0; i < idk_list->idks_count; i++ )
    {
        buildcachedat_add_idk(buildcachedat, i, idk_list->idks[i]);
    }
    free(idk_list->idks);
    free(idk_list);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "npc.dat");
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "npc.idx");

    assert(data_file_idx != -1 && index_file_idx != -1);

    struct FileListDatIndexed* npc_fi = filelist_dat_indexed_new_from_decode(
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx]);

    buildcachedat_reserve_hmap(buildcachedat->npc_hmap, (size_t)npc_fi->offset_count);

    for( int i = 0; i < npc_fi->offset_count; i++ )
    {
        struct RSBuffer npc_buf;
        rsbuf_init(
            &npc_buf,
            npc_fi->data + npc_fi->offsets[i],
            npc_fi->data_size - npc_fi->offsets[i]);
        struct CacheDatConfigNpc* npc = cache_dat_config_npc_decode_one(&npc_buf);
        assert(npc != NULL && "Failed to decode npc");
        buildcachedat_add_npc(buildcachedat, i, npc);
    }
    filelist_dat_indexed_free(npc_fi);
}

void
buildcachedat_loader_init_objects_from_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* filelist = buildcachedat_config_jagfile(buildcachedat);

    int data_file_idx = filelist_dat_find_file_by_name(filelist, "obj.dat");
    assert(data_file_idx != -1 && "Failed to find obj.dat in filelist");
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "obj.idx");
    assert(index_file_idx != -1 && "Failed to find obj.idx in filelist");

    struct FileListDatIndexed* fi = filelist_dat_indexed_new_from_decode(
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx]);

    buildcachedat_reserve_hmap(buildcachedat->obj_hmap, (size_t)fi->offset_count);

    for( int i = 0; i < fi->offset_count; i++ )
    {
        struct CacheDatConfigObj* obj = cache_dat_config_obj_decode_one(
            fi->data + fi->offsets[i], fi->data_size - fi->offsets[i]);
        assert(obj != NULL && "Failed to decode obj");
        buildcachedat_add_obj(buildcachedat, i, obj);
    }

    filelist_dat_indexed_free(fi);
}

void
buildcachedat_loader_load_interfaces(
    struct BuildCacheDat* buildcachedat,
    void* archive_data,
    int archive_data_size)
{
    if( !archive_data || archive_data_size == 0 )
    {
        printf("buildcachedat_loader_load_interfaces: No data provided\n");
        return;
    }

    // Create a temporary archive structure to extract the filelist
    struct CacheDatArchive archive;
    archive.data = archive_data;
    archive.data_size = archive_data_size;

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(&archive);
    if( !filelist )
    {
        printf("buildcachedat_loader_load_interfaces: Failed to create filelist from archive\n");
        return;
    }

    // Find the "data" file in the archive
    int idx = -1;
    int name_hash = archive_name_hash_dat("data");
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
        {
            idx = i;
            break;
        }
    }

    if( idx == -1 )
    {
        printf("buildcachedat_loader_load_interfaces: 'data' file not found in archive\n");
        filelist_dat_free(filelist);
        return;
    }

    char* file_data = filelist->files[idx];
    int file_data_size = filelist->file_sizes[idx];

    // Decode the component list from the "data" file
    struct CacheDatConfigComponentList* component_list =
        cache_dat_config_component_list_new_decode(file_data, file_data_size);

    if( !component_list )
    {
        printf("buildcachedat_loader_load_interfaces: Failed to decode component list\n");
        filelist_dat_free(filelist);
        return;
    }

    printf(
        "buildcachedat_loader_load_interfaces: Loaded %d components\n",
        component_list->components_count);

    for( int i = 0; i < component_list->components_count; i++ )
    {
        struct CacheDatConfigComponent* component = component_list->components[i];
        if( component )
        {
            buildcachedat_add_component(buildcachedat, component->id, component);
        }
    }

    // Don't free components, they're owned by buildcachedat now
    free(component_list->components);
    free(component_list);
    filelist_dat_free(filelist);
}

// Update all the dash loading and API's to use DashVertexInt* instead of int* for vertices of
// models. Loading from the cache
void
buildcachedat_loader_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z)
{
    /* varp/varbit loaded in init_varp_varbit (called right after config in load_scene_dat) */
    if( game->world )
        world_free(game->world);

    game->world = world_new(buildcachedat, game->scene2);

    buildcachedat_clear_jagfiles(buildcachedat);

    world_buildcachedat_rebuild_centerzone(game->world, map_sw_x * 8 + 12, map_sw_z * 8 + 12, 104);

    LibToriRS_WorldMinimapStaticRebuild(game);

    buildcachedat_clear(buildcachedat);
}

void
buildcachedat_loader_finalize_scene_centerzone(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int zonex,
    int zonez,
    int size)
{
    if( game->world )
        world_free(game->world);

    game->world = world_new(buildcachedat, game->scene2);

    buildcachedat_clear_jagfiles(buildcachedat);

    world_buildcachedat_rebuild_centerzone(game->world, zonex, zonez, size);

    LibToriRS_WorldMinimapStaticRebuild(game);

    buildcachedat_clear(buildcachedat);
}

void
buildcachedat_loader_prepare_scene_centerzone(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game)
{
    if( game->world )
        world_free(game->world);

    game->world = world_new(buildcachedat, game->scene2);
    /* Jagfile lifecycle is managed by the caller (Lua).  The config jagfile must
     * remain valid through the chunk loop so per-chunk init_scenery_configs calls
     * can decode loc configs.  Do NOT clear jagfiles here. */
}