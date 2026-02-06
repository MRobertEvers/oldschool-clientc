#include "buildcachedat_loader.h"

#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_npc.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/filelist.h"
#include "osrs/dashlib.h"
#include "osrs/dash_utils.h"
#include "osrs/texture.h"
#include "osrs/scenebuilder.h"
#include "osrs/minimap.h"
#include "osrs/painters.h"
#include "datastruct/vec.h"
#include "graphics/dash.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

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
buildcachedat_loader_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data)
{
    struct FileListDat* jagfile = filelist_dat_new_from_decode(data, data_size);
    buildcachedat_set_versionlist_jagfile(buildcachedat, jagfile);
}

void
buildcachedat_loader_cache_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data)
{
    int map_x = (param_b >> 16) & 0xFFFF;
    int map_z = param_b & 0xFFFF;
    
    struct CacheMapTerrain* terrain = map_terrain_new_from_decode_flags(
        data, data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);
    
    buildcachedat_add_map_terrain(buildcachedat, map_x, map_z, terrain);
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
buildcachedat_loader_load_floortype(struct BuildCacheDat* buildcachedat)
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
buildcachedat_loader_load_scenery_configs(struct BuildCacheDat* buildcachedat)
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
            struct CacheConfigLocation* config_loc = malloc(sizeof(struct CacheConfigLocation));
            memset(config_loc, 0, sizeof(struct CacheConfigLocation));
            
            struct CacheMapLoc* loc = &locs->locs[i];
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
buildcachedat_loader_load_textures(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data)
{
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
        
        buildcachedat_add_texture(buildcachedat, i, dash_texture);
        dash3d_add_texture(game->sys_dash, i, dash_texture);
    }
    
    filelist_dat_free(filelist);
}

void
buildcachedat_loader_load_sequences(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");
    
    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "seq.dat");
    assert(data_file_idx != -1 && "Data file must be found");
    
    struct RSBuffer buffer = {
        .data = config_jagfile->files[data_file_idx],
        .size = config_jagfile->file_sizes[data_file_idx],
        .position = 0
    };
    
    int count = g2(&buffer);
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
buildcachedat_loader_get_animbaseframes_count(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* versionlist_jagfile =
        buildcachedat_versionlist_jagfile(buildcachedat);
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
        buildcachedat_add_animframe(buildcachedat, animframe->id, animframe);
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
buildcachedat_loader_load_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data)
{
    struct FileListDat* filelist = filelist_dat_new_from_decode(data, data_size);
    
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    assert(index_file_idx != -1 && "Failed to find index.dat in filelist");
    
    game->sprite_invback = load_sprite_pix8(filelist, "invback.dat", index_file_idx, 0);
    game->sprite_chatback = load_sprite_pix8(filelist, "chatback.dat", index_file_idx, 0);
    game->sprite_mapback = load_sprite_pix8(filelist, "mapback.dat", index_file_idx, 0);
    game->sprite_backbase1 = load_sprite_pix8(filelist, "backbase1.dat", index_file_idx, 0);
    game->sprite_backbase2 = load_sprite_pix8(filelist, "backbase2.dat", index_file_idx, 0);
    game->sprite_backhmid1 = load_sprite_pix8(filelist, "backhmid1.dat", index_file_idx, 0);
    
    for( int i = 0; i < 13; i++ )
    {
        game->sprite_sideicons[i] = load_sprite_pix8(filelist, "sideicons.dat", index_file_idx, i);
    }
    
    game->sprite_compass = load_sprite_pix32(filelist, "compass.dat", index_file_idx, 0);
    game->sprite_mapedge = load_sprite_pix32(filelist, "mapedge.dat", index_file_idx, 0);
    
    for( int i = 0; i < 50; i++ )
    {
        game->sprite_mapscene[i] = load_sprite_pix8(filelist, "mapscene.dat", index_file_idx, i);
    }
    
    for( int i = 0; i < 50; i++ )
    {
        game->sprite_mapfunction[i] = load_sprite_pix32(filelist, "mapfunction.dat", index_file_idx, i);
    }
    
    for( int i = 0; i < 20; i++ )
    {
        game->sprite_hitmarks[i] = load_sprite_pix32(filelist, "hitmarks.dat", index_file_idx, i);
        game->sprite_headicons[i] = load_sprite_pix32(filelist, "headicons.dat", index_file_idx, i);
    }
    
    game->sprite_mapmarker0 = load_sprite_pix32(filelist, "mapmarker.dat", index_file_idx, 0);
    game->sprite_mapmarker1 = load_sprite_pix32(filelist, "mapmarker.dat", index_file_idx, 1);
    
    for( int i = 0; i < 8; i++ )
    {
        game->sprite_cross[i] = load_sprite_pix32(filelist, "cross.dat", index_file_idx, i);
    }
    
    game->sprite_mapdot0 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 0);
    game->sprite_mapdot1 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 1);
    game->sprite_mapdot2 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 2);
    game->sprite_mapdot3 = load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 3);
    
    game->sprite_scrollbar0 = load_sprite_pix8(filelist, "scrollbar.dat", index_file_idx, 0);
    game->sprite_scrollbar1 = load_sprite_pix8(filelist, "scrollbar.dat", index_file_idx, 1);
    
    game->sprite_redstone1 = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    game->sprite_redstone2 = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    game->sprite_redstone3 = load_sprite_pix8(filelist, "redstone3.dat", index_file_idx, 0);
    game->sprite_redstone1h = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    game->sprite_redstone2h = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    game->sprite_redstone1v = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    game->sprite_redstone2v = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    game->sprite_redstone3v = load_sprite_pix8(filelist, "redstone3.dat", index_file_idx, 0);
    game->sprite_redstone1hv = load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);
    game->sprite_redstone2hv = load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);
    
    for( int i = 0; i < 2; i++ )
    {
        game->sprite_modicons[i] = load_sprite_pix8(filelist, "mod_icons.dat", index_file_idx, i);
    }
    
    game->sprite_backleft1 = load_sprite_pix32(filelist, "backleft1.dat", index_file_idx, 0);
    game->sprite_backleft2 = load_sprite_pix8(filelist, "backleft2.dat", index_file_idx, 0);
    game->sprite_backright1 = load_sprite_pix8(filelist, "backright1.dat", index_file_idx, 0);
    game->sprite_backright2 = load_sprite_pix8(filelist, "backright2.dat", index_file_idx, 0);
    game->sprite_backtop1 = load_sprite_pix8(filelist, "backtop1.dat", index_file_idx, 0);
    game->sprite_backvmid1 = load_sprite_pix8(filelist, "backvmid1.dat", index_file_idx, 0);
    game->sprite_backvmid2 = load_sprite_pix8(filelist, "backvmid2.dat", index_file_idx, 0);
    game->sprite_backvmid3 = load_sprite_pix8(filelist, "backvmid3.dat", index_file_idx, 0);
    game->sprite_backhmid2 = load_sprite_pix8(filelist, "backhmid2.dat", index_file_idx, 0);
    
    filelist_dat_free(filelist);
}

void
buildcachedat_loader_load_title(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data)
{
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
    game->pixfont_b12 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);
    
    data_file_idx = filelist_dat_find_file_by_name(filelist, "p12.dat");
    assert(data_file_idx != -1);
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    game->pixfont_p12 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);
    
    data_file_idx = filelist_dat_find_file_by_name(filelist, "p11.dat");
    assert(data_file_idx != -1);
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    game->pixfont_p11 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);
    
    data_file_idx = filelist_dat_find_file_by_name(filelist, "q8.dat");
    assert(data_file_idx != -1);
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    game->pixfont_q8 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);
    
    filelist_dat_free(filelist);
}

void
buildcachedat_loader_load_idkits(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* filelist = buildcachedat_config_jagfile(buildcachedat);
    
    int data_file_idx = filelist_dat_find_file_by_name(filelist, "idk.dat");
    assert(data_file_idx != -1 && "Failed to find idk.dat in filelist");
    
    struct CacheDatConfigIdkList* idk_list = cache_dat_config_idk_list_new_decode(
        filelist->files[data_file_idx], filelist->file_sizes[data_file_idx]);
    
    for( int i = 0; i < idk_list->idks_count; i++ )
    {
        buildcachedat_add_idk(buildcachedat, i, idk_list->idks[i]);
    }
    
    data_file_idx = filelist_dat_find_file_by_name(filelist, "npc.dat");
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "npc.idx");
    
    assert(data_file_idx != -1 && index_file_idx != -1);
    
    struct CacheDatConfigNpcList* npc_list = cache_dat_config_npc_list_new_decode(
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx]);
    
    for( int i = 0; i < npc_list->npcs_count; i++ )
    {
        buildcachedat_add_npc(buildcachedat, i, npc_list->npcs[i]);
    }
}

void
buildcachedat_loader_load_objects(struct BuildCacheDat* buildcachedat)
{
    struct FileListDat* filelist = buildcachedat_config_jagfile(buildcachedat);
    
    int data_file_idx = filelist_dat_find_file_by_name(filelist, "obj.dat");
    assert(data_file_idx != -1 && "Failed to find obj.dat in filelist");
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "obj.idx");
    assert(index_file_idx != -1 && "Failed to find obj.idx in filelist");
    
    struct CacheDatConfigObjList* obj_list = cache_dat_config_obj_list_new_decode(
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx]);
    
    for( int i = 0; i < obj_list->objs_count; i++ )
    {
        buildcachedat_add_obj(buildcachedat, i, obj_list->objs[i]);
    }
}

void
buildcachedat_loader_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z)
{
    // Initialize painter and minimap if not already done
    if( !game->sys_painter )
    {
        game->sys_painter = painter_new(104, 104, MAP_TERRAIN_LEVELS);
        game->sys_painter_buffer = painter_buffer_new();
    }
    
    if( !game->sys_minimap )
    {
        game->sys_minimap = minimap_new(
            map_sw_x * 64, map_sw_z * 64, 
            (map_ne_x + 1) * 64, (map_ne_z + 1) * 64, 
            MAP_TERRAIN_LEVELS);
    }
    
    // Initialize scene builder if not already done
    if( !game->scenebuilder )
    {
        game->scenebuilder = scenebuilder_new_painter(game->sys_painter, game->sys_minimap);
    }
    
    // Build the final scene from cached data
    game->scene = scenebuilder_load_from_buildcachedat(
        game->scenebuilder,
        map_sw_x * 64,
        map_sw_z * 64,
        map_ne_x * 64,
        map_ne_z * 64,
        104,
        104,
        buildcachedat);
}