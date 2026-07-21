#include "engine/dat1/dat1_buildcache.h"

#include "engine/dat1/dat1_tasks.h"
#include "engine/torirs_sprite_from_rscache.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdlib.h>

struct MapEntry_CacheModel
{
    int id;
    struct RSCache_Model* model;
};

struct MapEntry_Terrain
{
    int id;
    struct RSCache_MapTerrain* terrain;
};

struct MapEntry_Scenery
{
    int id;
    struct RSCache_MapLocs* locs;
};

struct MapEntry_ConfigObj
{
    int id;
    struct RSCache_Dat1ConfigObj* obj;
};

struct MapEntry_ConfigNpc
{
    int id;
    struct RSCache_Dat1ConfigNpc* npc;
};

struct MapEntry_ConfigIdk
{
    int id;
    struct RSCache_Dat1ConfigIdk* idk;
};

struct MapEntry_AnimBaseFrames
{
    int id;
    struct RSCache_Dat1AnimBaseFrames* animbaseframes;
};

#define DAT1_MODEL_MAP_CAPACITY 8192
#define DAT1_MAP_REGION_CAPACITY 512
#define DAT1_CONFIG_MAP_CAPACITY 4096

static size_t
dat1_hmap_buffer_bytes(
    size_t entry_size,
    size_t capacity)
{
    const size_t align = 16;
    size_t entry_offset = align;
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = (raw_stride + align - 1) & ~(align - 1);
    return align + stride * capacity;
}

static struct HMap*
dat1_hmap_new(
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = dat1_hmap_buffer_bytes(entry_size, capacity);
    void* buffer = malloc(buffer_size);
    assert(buffer);

    struct HashConfig config = {
        .key_size = sizeof(int),
        .entry_size = entry_size,
        .buffer = buffer,
        .buffer_size = buffer_size,
        .capacity = capacity,
    };
    struct HMap* map = hmap_new(&config, 0);
    assert(map);
    return map;
}

static void
dat1_hmap_free(struct HMap* map)
{
    if( !map )
        return;
    free(hmap_free(map));
}

static void
dat1_hmap_maybe_grow(struct HMap** map_out)
{
    struct HMap* map;
    size_t new_capacity;
    size_t buffer_size;
    void* new_buffer;
    void* old_buffer;

    assert(map_out);
    map = *map_out;
    assert(map);

    if( map->size * 4 <= map->capacity * 3 )
        return;

    new_capacity = map->capacity * 2;
    buffer_size = dat1_hmap_buffer_bytes(map->entry_size, new_capacity);
    new_buffer = malloc(buffer_size);
    assert(new_buffer);

    if( hmap_resize(*map_out, new_buffer, buffer_size, new_capacity, &old_buffer) != HMAP_OK )
    {
        free(new_buffer);
        return;
    }

    free(old_buffer);
}

static void
dat1_hmap_prepare_insert(struct HMap** map_out)
{
    assert(map_out);
    assert(*map_out);
    if( (*map_out)->capacity > 0 && (*map_out)->size * 4 >= (*map_out)->capacity * 3 )
        dat1_hmap_maybe_grow(map_out);
}

static void
dat1_jagfile_set(
    struct RSCache_FileListDat** slot,
    struct RSCache_FileListDat* jagfile)
{
    assert(slot);
    if( *slot )
        RSCache_FileListDatFree(*slot);
    *slot = jagfile;
}

static void
dat1_jagfile_clear(struct RSCache_FileListDat** slot)
{
    assert(slot);
    if( *slot )
        RSCache_FileListDatFree(*slot);
    *slot = NULL;
}

static struct CacheProviderVTable dat1_vtable = {
    .Task_ModelLoad = CreateTask_Dat1ModelLoad,
    .Task_ComponentPackLoad = CreateTask_Dat1ComponentPackLoad,
    .Task_ClientScriptLoad = CreateTask_Dat1ClientScriptLoad,
    .Task_ObjLoad = CreateTask_Dat1ObjLoad,
    .Task_NpcLoad = CreateTask_Dat1NpcLoad,
    .Task_IdkLoad = CreateTask_Dat1IdkLoad,
    .Task_MapTerrainLoad = CreateTask_Dat1MapTerrainLoad,
    .Task_MapSceneryLoad = CreateTask_Dat1MapSceneryLoad,
    .Task_LocLoad = CreateTask_Dat1LocLoad,
    .Task_FlotypeLoad = CreateTask_Dat1FlotypeLoad,
    .Task_UnderlayLoad = CreateTask_Dat1UnderlayLoad,
    .Task_TextureLoad = CreateTask_Dat1TextureLoad,
    .Task_SequenceLoad = CreateTask_Dat1SequenceLoad,
    .Task_SpriteLoad = CreateTask_Dat1SpriteLoad,
    .Task_SpriteLoadByName = CreateTask_Dat1SpriteLoadByName,
    .Task_SpriteLoadFromSource = CreateTask_Dat1SpriteLoadFromSource,
    .Task_FontLoad = CreateTask_Dat1FontLoad,
    .Task_FontLoadByName = CreateTask_Dat1FontLoadByName,
    .Task_EnumLoad = NULL,
    .Task_StructLoad = NULL,
    .Task_ComponentLoad = CreateTask_Dat1ComponentLoad,
};

struct Dat1BuildCache*
dat1_buildcache_new(void)
{
    struct Dat1BuildCache* dat1_buildcache = calloc(1, sizeof(*dat1_buildcache));
    assert(dat1_buildcache);

    dat1_buildcache->next_sprite_id = DAT1_SPRITE_ID_BASE;
    dat1_buildcache->base.vtable = &dat1_vtable;
    CacheProvider_InitEngineCaches(&dat1_buildcache->base);
    dat1_buildcache->models_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_CacheModel), DAT1_MODEL_MAP_CAPACITY);
    dat1_buildcache->obj_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_ConfigObj), DAT1_CONFIG_MAP_CAPACITY);
    dat1_buildcache->npc_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_ConfigNpc), DAT1_CONFIG_MAP_CAPACITY);
    dat1_buildcache->idk_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_ConfigIdk), DAT1_CONFIG_MAP_CAPACITY);
    dat1_buildcache->map_terrain_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_Terrain), DAT1_MAP_REGION_CAPACITY);
    dat1_buildcache->map_scenery_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_Scenery), DAT1_MAP_REGION_CAPACITY);
    dat1_buildcache->animbaseframes_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_AnimBaseFrames), DAT1_CONFIG_MAP_CAPACITY);

    return dat1_buildcache;
}

static void
dat1_hmap_reset(
    struct HMap** map_out,
    size_t entry_size,
    size_t capacity)
{
    assert(map_out);
    dat1_hmap_free(*map_out);
    *map_out = dat1_hmap_new(entry_size, capacity);
}

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);

    dat1_jagfile_clear(&dat1_buildcache->config_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->versionlist_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->media_2d_graphics_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->title_fonts_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->textures_jagfile);
    if( dat1_buildcache->loc_index )
    {
        RSCache_FileListDatIndexedFree(dat1_buildcache->loc_index);
        dat1_buildcache->loc_index = NULL;
    }
    if( dat1_buildcache->interfaces_list )
    {
        RSCache_Dat1ConfigComponentListFree(dat1_buildcache->interfaces_list);
        dat1_buildcache->interfaces_list = NULL;
    }

    dat1_buildcache_models_cleanup(dat1_buildcache);
    dat1_buildcache_objs_cleanup(dat1_buildcache);
    dat1_buildcache_npcs_cleanup(dat1_buildcache);
    dat1_buildcache_idks_cleanup(dat1_buildcache);
    dat1_buildcache_map_terrain_cleanup(dat1_buildcache);
    dat1_buildcache_map_scenery_cleanup(dat1_buildcache);
    dat1_buildcache_animbaseframes_cleanup(dat1_buildcache);
    if( dat1_buildcache->seq_list )
    {
        RSCache_Dat1ConfigSeqListFree(dat1_buildcache->seq_list);
        dat1_buildcache->seq_list = NULL;
    }

    dat1_hmap_free(dat1_buildcache->animbaseframes_hmap);
    dat1_hmap_free(dat1_buildcache->models_hmap);
    dat1_hmap_free(dat1_buildcache->obj_hmap);
    dat1_hmap_free(dat1_buildcache->npc_hmap);
    dat1_hmap_free(dat1_buildcache->idk_hmap);
    dat1_hmap_free(dat1_buildcache->map_terrain_hmap);
    dat1_hmap_free(dat1_buildcache->map_scenery_hmap);
    CacheProvider_FreeEngineCaches(&dat1_buildcache->base);

    free(dat1_buildcache);
}

struct CacheProvider*
dat1_buildcache_as_provider(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return &dat1_buildcache->base;
}

void
dat1_buildcache_set_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* config_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->config_jagfile, config_jagfile);
}

void
dat1_buildcache_clear_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->config_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->config_jagfile;
}

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* versionlist_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->versionlist_jagfile, versionlist_jagfile);
}

void
dat1_buildcache_clear_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->versionlist_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->versionlist_jagfile;
}

void
dat1_buildcache_set_textures_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* textures_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->textures_jagfile, textures_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_textures_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->textures_jagfile;
}

struct RSCache_FileListDatIndexed*
dat1_buildcache_get_loc_index(struct Dat1BuildCache* dat1_buildcache)
{
    struct RSCache_FileListDat* config_jagfile;
    int data_idx;
    int index_idx;

    assert(dat1_buildcache);
    if( dat1_buildcache->loc_index )
        return dat1_buildcache->loc_index;

    config_jagfile = dat1_buildcache->config_jagfile;
    if( !config_jagfile )
        return NULL;

    data_idx = RSCache_FileListDatFindFileByName(config_jagfile, "loc.dat");
    index_idx = RSCache_FileListDatFindFileByName(config_jagfile, "loc.idx");
    if( data_idx < 0 || index_idx < 0 )
        return NULL;

    dat1_buildcache->loc_index = RSCache_FileListDatIndexedNewFromDecode(
        config_jagfile->files[index_idx],
        config_jagfile->file_sizes[index_idx],
        config_jagfile->files[data_idx],
        config_jagfile->file_sizes[data_idx]);
    return dat1_buildcache->loc_index;
}

struct RSCache_Dat1ConfigSeqList*
dat1_buildcache_get_seq_list(struct Dat1BuildCache* dat1_buildcache)
{
    struct RSCache_FileListDat* config_jagfile;
    int data_idx;

    assert(dat1_buildcache);
    if( dat1_buildcache->seq_list )
        return dat1_buildcache->seq_list;

    config_jagfile = dat1_buildcache->config_jagfile;
    if( !config_jagfile )
        return NULL;

    data_idx = RSCache_FileListDatFindFileByName(config_jagfile, "seq.dat");
    if( data_idx < 0 )
        return NULL;

    dat1_buildcache->seq_list = RSCache_Dat1ConfigSeqListNewDecode(
        config_jagfile->files[data_idx], config_jagfile->file_sizes[data_idx]);
    return dat1_buildcache->seq_list;
}

void
dat1_buildcache_animbaseframes_add(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id,
    struct RSCache_Dat1AnimBaseFrames* animbaseframes)
{
    struct MapEntry_AnimBaseFrames* entry;

    assert(dat1_buildcache);
    assert(animbaseframes);

    dat1_hmap_prepare_insert(&dat1_buildcache->animbaseframes_hmap);
    entry = (struct MapEntry_AnimBaseFrames*)hmap_search(
        dat1_buildcache->animbaseframes_hmap, &animbaseframes_id, HMAP_INSERT);
    assert(entry && "AnimBaseFrames must be inserted into hmap");

    if( entry->animbaseframes )
        RSCache_Dat1AnimBaseFramesFree(entry->animbaseframes);
    entry->id = animbaseframes_id;
    entry->animbaseframes = animbaseframes;
}

struct RSCache_Dat1AnimBaseFrames*
dat1_buildcache_animbaseframes_get(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id)
{
    struct MapEntry_AnimBaseFrames* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_AnimBaseFrames*)hmap_search(
        dat1_buildcache->animbaseframes_hmap, &animbaseframes_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->animbaseframes;
}

void
dat1_buildcache_animbaseframes_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_AnimBaseFrames* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->animbaseframes_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->animbaseframes_hmap);
    while( (entry = (struct MapEntry_AnimBaseFrames*)hmap_iter_next(iter)) )
    {
        if( entry->animbaseframes )
            RSCache_Dat1AnimBaseFramesFree(entry->animbaseframes);
        entry->animbaseframes = NULL;
    }
    hmap_iter_free(iter);
    dat1_hmap_reset(
        &dat1_buildcache->animbaseframes_hmap,
        sizeof(struct MapEntry_AnimBaseFrames),
        DAT1_CONFIG_MAP_CAPACITY);
}

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* media_2d_graphics_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->media_2d_graphics_jagfile, media_2d_graphics_jagfile);
}

void
dat1_buildcache_clear_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->media_2d_graphics_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->media_2d_graphics_jagfile;
}

void
dat1_buildcache_set_title_fonts_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* title_fonts_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->title_fonts_jagfile, title_fonts_jagfile);
}

void
dat1_buildcache_clear_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->title_fonts_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->title_fonts_jagfile;
}

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct RSCache_Model* model)
{
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);
    assert(model);

    dat1_hmap_prepare_insert(&dat1_buildcache->models_hmap);
    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat1_buildcache->models_hmap, &model_id, HMAP_INSERT);
    assert(entry && "Model must be inserted into hmap");

    entry->id = model_id;
    entry->model = model;
}

struct RSCache_Model*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat1_buildcache->models_hmap, &model_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
dat1_buildcache_model_remove(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat1_buildcache->models_hmap, &model_id, HMAP_REMOVE);
    if( !entry || !entry->model )
        return;

    RSCache_ModelFree(entry->model);
    entry->model = NULL;
}

void
dat1_buildcache_models_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->models_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->models_hmap);
    while( (entry = (struct MapEntry_CacheModel*)hmap_iter_next(iter)) )
    {
        if( entry->model )
            RSCache_ModelFree(entry->model);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->models_hmap,
        sizeof(struct MapEntry_CacheModel),
        DAT1_MODEL_MAP_CAPACITY);
}

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCache_MapTerrain* terrain)
{
    struct MapEntry_Terrain* entry;

    assert(dat1_buildcache);
    assert(terrain);

    dat1_hmap_prepare_insert(&dat1_buildcache->map_terrain_hmap);
    entry = (struct MapEntry_Terrain*)hmap_search(
        dat1_buildcache->map_terrain_hmap, &map_id, HMAP_INSERT);
    assert(entry && "Terrain must be inserted into hmap");

    entry->id = map_id;
    entry->terrain = terrain;
}

struct RSCache_MapTerrain*
dat1_buildcache_map_terrain_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    struct MapEntry_Terrain* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_Terrain*)hmap_search(
        dat1_buildcache->map_terrain_hmap, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
dat1_buildcache_map_terrain_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    return dat1_buildcache_map_terrain_get(dat1_buildcache, map_id) != NULL;
}

void
dat1_buildcache_map_terrain_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Terrain* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->map_terrain_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->map_terrain_hmap);
    while( (entry = (struct MapEntry_Terrain*)hmap_iter_next(iter)) )
    {
        if( entry->terrain )
            RSCache_MapTerrainFree(entry->terrain);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->map_terrain_hmap,
        sizeof(struct MapEntry_Terrain),
        DAT1_MAP_REGION_CAPACITY);
}

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCache_MapLocs* locs)
{
    struct MapEntry_Scenery* entry;

    assert(dat1_buildcache);
    assert(locs);

    dat1_hmap_prepare_insert(&dat1_buildcache->map_scenery_hmap);
    entry = (struct MapEntry_Scenery*)hmap_search(
        dat1_buildcache->map_scenery_hmap, &map_id, HMAP_INSERT);
    assert(entry && "Scenery must be inserted into hmap");

    entry->id = map_id;
    entry->locs = locs;
}

struct RSCache_MapLocs*
dat1_buildcache_map_scenery_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    struct MapEntry_Scenery* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_Scenery*)hmap_search(
        dat1_buildcache->map_scenery_hmap, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
dat1_buildcache_map_scenery_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    return dat1_buildcache_map_scenery_get(dat1_buildcache, map_id) != NULL;
}

void
dat1_buildcache_map_scenery_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Scenery* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->map_scenery_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->map_scenery_hmap);
    while( (entry = (struct MapEntry_Scenery*)hmap_iter_next(iter)) )
    {
        if( entry->locs )
            RSCache_MapLocsFree(entry->locs);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->map_scenery_hmap,
        sizeof(struct MapEntry_Scenery),
        DAT1_MAP_REGION_CAPACITY);
}

void
dat1_buildcache_prune(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);

    dat1_buildcache_models_cleanup(dat1_buildcache);
    dat1_buildcache_objs_cleanup(dat1_buildcache);
    dat1_buildcache_npcs_cleanup(dat1_buildcache);
    dat1_buildcache_idks_cleanup(dat1_buildcache);
    dat1_buildcache_map_terrain_cleanup(dat1_buildcache);
    dat1_buildcache_map_scenery_cleanup(dat1_buildcache);
}

void
dat1_buildcache_obj_add(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id,
    struct RSCache_Dat1ConfigObj* obj)
{
    struct MapEntry_ConfigObj* entry;

    assert(dat1_buildcache);
    assert(obj);

    dat1_hmap_prepare_insert(&dat1_buildcache->obj_hmap);
    entry = (struct MapEntry_ConfigObj*)hmap_search(
        dat1_buildcache->obj_hmap, &obj_id, HMAP_INSERT);
    assert(entry && "Obj must be inserted into hmap");

    if( entry->obj )
        RSCache_Dat1ConfigObjFree(entry->obj);

    entry->id = obj_id;
    entry->obj = obj;
}

struct RSCache_Dat1ConfigObj*
dat1_buildcache_obj_get(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id)
{
    struct MapEntry_ConfigObj* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_ConfigObj*)hmap_search(
        dat1_buildcache->obj_hmap, &obj_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->obj;
}

bool
dat1_buildcache_obj_has(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id)
{
    return dat1_buildcache_obj_get(dat1_buildcache, obj_id) != NULL;
}

void
dat1_buildcache_objs_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigObj* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->obj_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->obj_hmap);
    while( (entry = (struct MapEntry_ConfigObj*)hmap_iter_next(iter)) )
    {
        if( entry->obj )
            RSCache_Dat1ConfigObjFree(entry->obj);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->obj_hmap, sizeof(struct MapEntry_ConfigObj), DAT1_CONFIG_MAP_CAPACITY);
}

struct RSCache_Dat1ConfigObj*
dat1_buildcache_obj_load_from_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id)
{
    struct RSCache_FileListDat* config_jagfile;
    struct RSCache_Dat1ConfigObj* existing;
    struct RSCache_FileListDatIndexed* filelist_indexed;
    struct RSCache_Dat1ConfigObj* obj;
    int data_file_idx;
    int index_file_idx;

    assert(dat1_buildcache);

    if( obj_id < 0 )
        return NULL;

    existing = dat1_buildcache_obj_get(dat1_buildcache, obj_id);
    if( existing )
        return existing;

    config_jagfile = dat1_buildcache->config_jagfile;
    if( !config_jagfile )
        return NULL;

    data_file_idx = RSCache_FileListDatFindFileByName(config_jagfile, "obj.dat");
    index_file_idx = RSCache_FileListDatFindFileByName(config_jagfile, "obj.idx");
    if( data_file_idx == -1 || index_file_idx == -1 )
        return NULL;

    filelist_indexed = RSCache_FileListDatIndexedNewFromDecode(
        config_jagfile->files[index_file_idx],
        config_jagfile->file_sizes[index_file_idx],
        config_jagfile->files[data_file_idx],
        config_jagfile->file_sizes[data_file_idx]);
    if( !filelist_indexed || obj_id >= filelist_indexed->offset_count )
    {
        RSCache_FileListDatIndexedFree(filelist_indexed);
        return NULL;
    }

    obj = RSCache_Dat1ConfigObjDecodeOne(
        filelist_indexed->data + filelist_indexed->offsets[obj_id],
        filelist_indexed->data_size - filelist_indexed->offsets[obj_id]);
    if( obj )
        dat1_buildcache_obj_add(dat1_buildcache, obj_id, obj);

    RSCache_FileListDatIndexedFree(filelist_indexed);
    return obj;
}

void
dat1_buildcache_npc_add(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id,
    struct RSCache_Dat1ConfigNpc* npc)
{
    struct MapEntry_ConfigNpc* entry;

    assert(dat1_buildcache);
    assert(npc);

    dat1_hmap_prepare_insert(&dat1_buildcache->npc_hmap);
    entry = (struct MapEntry_ConfigNpc*)hmap_search(
        dat1_buildcache->npc_hmap, &npc_id, HMAP_INSERT);
    assert(entry && "Npc must be inserted into hmap");

    if( entry->npc )
        RSCache_Dat1ConfigNpcFree(entry->npc);

    entry->id = npc_id;
    entry->npc = npc;
}

struct RSCache_Dat1ConfigNpc*
dat1_buildcache_npc_get(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id)
{
    struct MapEntry_ConfigNpc* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_ConfigNpc*)hmap_search(
        dat1_buildcache->npc_hmap, &npc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->npc;
}

bool
dat1_buildcache_npc_has(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id)
{
    return dat1_buildcache_npc_get(dat1_buildcache, npc_id) != NULL;
}

void
dat1_buildcache_npcs_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigNpc* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->npc_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->npc_hmap);
    while( (entry = (struct MapEntry_ConfigNpc*)hmap_iter_next(iter)) )
    {
        if( entry->npc )
            RSCache_Dat1ConfigNpcFree(entry->npc);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->npc_hmap, sizeof(struct MapEntry_ConfigNpc), DAT1_CONFIG_MAP_CAPACITY);
}

struct RSCache_Dat1ConfigNpc*
dat1_buildcache_npc_load_from_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id)
{
    struct RSCache_FileListDat* config_jagfile;
    struct RSCache_Dat1ConfigNpc* existing;
    struct RSCache_FileListDatIndexed* filelist_indexed;
    struct RSCache_Buffer buffer;
    struct RSCache_Dat1ConfigNpc* npc;
    int data_file_idx;
    int index_file_idx;

    assert(dat1_buildcache);

    if( npc_id < 0 )
        return NULL;

    existing = dat1_buildcache_npc_get(dat1_buildcache, npc_id);
    if( existing )
        return existing;

    config_jagfile = dat1_buildcache->config_jagfile;
    if( !config_jagfile )
        return NULL;

    data_file_idx = RSCache_FileListDatFindFileByName(config_jagfile, "npc.dat");
    index_file_idx = RSCache_FileListDatFindFileByName(config_jagfile, "npc.idx");
    if( data_file_idx == -1 || index_file_idx == -1 )
        return NULL;

    filelist_indexed = RSCache_FileListDatIndexedNewFromDecode(
        config_jagfile->files[index_file_idx],
        config_jagfile->file_sizes[index_file_idx],
        config_jagfile->files[data_file_idx],
        config_jagfile->file_sizes[data_file_idx]);
    if( !filelist_indexed || npc_id >= filelist_indexed->offset_count )
    {
        RSCache_FileListDatIndexedFree(filelist_indexed);
        return NULL;
    }

    RSCache_BufferInit(
        &buffer,
        (uint8_t*)(filelist_indexed->data + filelist_indexed->offsets[npc_id]),
        (uint32_t)(filelist_indexed->data_size - filelist_indexed->offsets[npc_id]));

    npc = RSCache_Dat1ConfigNpcDecodeOne(&buffer);
    if( npc )
        dat1_buildcache_npc_add(dat1_buildcache, npc_id, npc);

    RSCache_FileListDatIndexedFree(filelist_indexed);
    return npc;
}

void
dat1_buildcache_idk_add(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id,
    struct RSCache_Dat1ConfigIdk* idk)
{
    struct MapEntry_ConfigIdk* entry;

    assert(dat1_buildcache);
    assert(idk);

    dat1_hmap_prepare_insert(&dat1_buildcache->idk_hmap);
    entry = (struct MapEntry_ConfigIdk*)hmap_search(
        dat1_buildcache->idk_hmap, &idk_id, HMAP_INSERT);
    assert(entry && "Idk must be inserted into hmap");

    if( entry->idk )
        RSCache_Dat1ConfigIdkFree(entry->idk);

    entry->id = idk_id;
    entry->idk = idk;
}

struct RSCache_Dat1ConfigIdk*
dat1_buildcache_idk_get(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id)
{
    struct MapEntry_ConfigIdk* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_ConfigIdk*)hmap_search(
        dat1_buildcache->idk_hmap, &idk_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->idk;
}

bool
dat1_buildcache_idk_has(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id)
{
    return dat1_buildcache_idk_get(dat1_buildcache, idk_id) != NULL;
}

void
dat1_buildcache_idks_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigIdk* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->idk_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->idk_hmap);
    while( (entry = (struct MapEntry_ConfigIdk*)hmap_iter_next(iter)) )
    {
        if( entry->idk )
            RSCache_Dat1ConfigIdkFree(entry->idk);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->idk_hmap, sizeof(struct MapEntry_ConfigIdk), DAT1_CONFIG_MAP_CAPACITY);
}

void
dat1_buildcache_set_interfaces_list(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_Dat1ConfigComponentList* interfaces_list)
{
    assert(dat1_buildcache);
    if( dat1_buildcache->interfaces_list )
        RSCache_Dat1ConfigComponentListFree(dat1_buildcache->interfaces_list);
    dat1_buildcache->interfaces_list = interfaces_list;
}

struct RSCache_Dat1ConfigComponentList*
dat1_buildcache_get_interfaces_list(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->interfaces_list;
}

int
dat1_buildcache_sprite_id_alloc(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->next_sprite_id++;
}

int
dat1_buildcache_sprite_ref_acquire(
    struct Dat1BuildCache* dat1_buildcache,
    char const* ref)
{
    struct ToriRS_Sprite* sprite;
    int sprite_id;

    assert(dat1_buildcache);
    if( !ref || ref[0] == '\0' )
        return -1;

    sprite_id = CacheProvider_SpriteIdByName(&dat1_buildcache->base, ref);
    if( sprite_id >= 0 && CacheProvider_SpriteHas(&dat1_buildcache->base, sprite_id) )
        return sprite_id;

    if( !dat1_buildcache->media_2d_graphics_jagfile )
        return -1;

    sprite = ToriRS_SpriteFromDat1Ref(dat1_buildcache->media_2d_graphics_jagfile, ref);
    if( !sprite )
        return -1;

    sprite_id = dat1_buildcache_sprite_id_alloc(dat1_buildcache);
    CacheProvider_SpriteAdd(&dat1_buildcache->base, sprite_id, sprite);
    CacheProvider_SpriteNameMapPut(&dat1_buildcache->base, ref, sprite_id);
    return sprite_id;
}

void
dat1_buildcache_idks_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    struct RSCache_FileListDat* config_jagfile;
    struct RSCache_Dat1ConfigIdkList* idk_list;
    int data_file_idx;

    assert(dat1_buildcache);

    config_jagfile = dat1_buildcache->config_jagfile;
    assert(config_jagfile);

    data_file_idx = RSCache_FileListDatFindFileByName(config_jagfile, "idk.dat");
    assert(data_file_idx != -1);

    idk_list = RSCache_Dat1ConfigIdkListNewDecode(
        config_jagfile->files[data_file_idx], config_jagfile->file_sizes[data_file_idx]);
    assert(idk_list);

    for( int i = 0; i < idk_list->idks_count; i++ )
        dat1_buildcache_idk_add(dat1_buildcache, i, idk_list->idks[i]);

    free(idk_list->idks);
    free(idk_list);
}
