#include "buildcachedat.h"

#include "graphics/dash.h"

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};

struct FlotypeEntry
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct ConfigLocEntry
{
    int id;
    struct CacheConfigLocation* config_loc;
};

struct AnimframeEntry
{
    int id;
    struct CacheAnimframe* animframe;
};

struct AnimbaseframesEntry
{
    int id;
    struct CacheDatAnimBaseFrames* animbaseframes;
};

struct SceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct TextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct SequenceEntry
{
    int id;
    struct CacheDatSequence* sequence;
};

struct IdkEntry
{
    int id;
    struct CacheDatConfigIdk* idk;
};

struct ObjEntry
{
    int id;
    struct CacheDatConfigObj* obj;
};

struct IdkModelEntry
{
    int id;
    struct CacheModel* model;
};

struct ObjModelEntry
{
    int id;
    struct CacheModel* model;
};

struct NpcEntry
{
    int id;
    struct CacheDatConfigNpc* npc;
};

struct NpcModelEntry
{
    int id;
    struct CacheModel* model;
};

struct BuildCacheDat*
buildcachedat_new(void)
{
    struct BuildCacheDat* buildcachedat = malloc(sizeof(struct BuildCacheDat));
    memset(buildcachedat, 0, sizeof(struct BuildCacheDat));

    struct DashMapConfig config = { 0 };

    int buffer_size = 1024 * sizeof(struct TextureEntry) * 4;
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct TextureEntry),
    };
    buildcachedat->textures_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FlotypeEntry),
    };
    buildcachedat->flotype_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SceneryEntry),
    };
    buildcachedat->scenery_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ModelEntry),
    };
    buildcachedat->models_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ConfigLocEntry),
    };
    buildcachedat->config_loc_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size * 8),
        .buffer_size = buffer_size * 8,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct AnimframeEntry),
    };
    buildcachedat->animframes_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct AnimbaseframesEntry),
    };
    buildcachedat->animbaseframes_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SequenceEntry),
    };
    buildcachedat->sequences_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct IdkEntry),
    };
    buildcachedat->idk_hmap = dashmap_new(&config, 0);

    buffer_size = 4096 * 8 * sizeof(struct ObjEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ObjEntry),
    };
    buildcachedat->obj_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct IdkModelEntry),
    };
    buildcachedat->idk_models_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ObjModelEntry),
    };
    buildcachedat->obj_models_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapTerrainEntry),
    };
    buildcachedat->map_terrains_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct NpcEntry),
    };
    buildcachedat->npc_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct NpcModelEntry),
    };
    buildcachedat->npc_models_hmap = dashmap_new(&config, 0);

    return buildcachedat;
}

void
buildcachedat_free(struct BuildCacheDat* buildcachedat)
{
    // TODO: Free the files.
    dashmap_free(buildcachedat->textures_hmap);
    dashmap_free(buildcachedat->flotype_hmap);
    dashmap_free(buildcachedat->scenery_hmap);
    dashmap_free(buildcachedat->models_hmap);
    dashmap_free(buildcachedat->config_loc_hmap);
    dashmap_free(buildcachedat->animframes_hmap);
    dashmap_free(buildcachedat->animbaseframes_hmap);
    dashmap_free(buildcachedat->sequences_hmap);
    free(buildcachedat);
}

void
buildcachedat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* config_jagfile)
{
    buildcachedat->cfg_config_jagfile = config_jagfile;
}

struct FileListDat*
buildcachedat_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    return buildcachedat->cfg_config_jagfile;
}

void
buildcachedat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* versionlist_jagfile)
{
    buildcachedat->cfg_versionlist_jagfile = versionlist_jagfile;
}

struct FileListDat*
buildcachedat_versionlist_jagfile(struct BuildCacheDat* buildcachedat)
{
    return buildcachedat->cfg_versionlist_jagfile;
}

void
buildcachedat_add_flotype(
    struct BuildCacheDat* buildcachedat,
    int flotype_id,
    struct CacheConfigOverlay* flotype)
{
    struct FlotypeEntry* flotype_entry = (struct FlotypeEntry*)dashmap_search(
        buildcachedat->flotype_hmap, &flotype_id, DASHMAP_INSERT);
    assert(flotype_entry && "Flotype must be inserted into hmap");
    flotype_entry->id = flotype_id;
    flotype_entry->flotype = flotype;
}

struct CacheConfigOverlay*
buildcachedat_get_flotype(
    struct BuildCacheDat* buildcachedat,
    int flotype_id)
{
    struct FlotypeEntry* flotype_entry = (struct FlotypeEntry*)dashmap_search(
        buildcachedat->flotype_hmap, &flotype_id, DASHMAP_FIND);
    if( !flotype_entry )
        return NULL;
    return flotype_entry->flotype;
}

void
buildcachedat_add_texture(
    struct BuildCacheDat* buildcachedat,
    int texture_id,
    struct DashTexture* texture)
{
    struct TextureEntry* texture_entry = (struct TextureEntry*)dashmap_search(
        buildcachedat->textures_hmap, &texture_id, DASHMAP_INSERT);
    assert(texture_entry && "Texture must be inserted into hmap");
    texture_entry->id = texture_id;
    texture_entry->texture = texture;
}

struct DashTexture*
buildcachedat_get_texture(
    struct BuildCacheDat* buildcachedat,
    int texture_id)
{
    struct TextureEntry* texture_entry = (struct TextureEntry*)dashmap_search(
        buildcachedat->textures_hmap, &texture_id, DASHMAP_FIND);
    if( !texture_entry )
        return NULL;
    return texture_entry->texture;
}

void
buildcachedat_add_scenery(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    struct CacheMapLocs* locs)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* scenery_entry =
        (struct SceneryEntry*)dashmap_search(buildcachedat->scenery_hmap, &mapxz, DASHMAP_INSERT);
    assert(scenery_entry && "Scenery must be inserted into hmap");
    scenery_entry->id = mapxz;
    scenery_entry->mapx = mapx;
    scenery_entry->mapz = mapz;
    scenery_entry->locs = locs;
}

struct CacheMapLocs*
buildcachedat_get_scenery(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* scenery_entry =
        (struct SceneryEntry*)dashmap_search(buildcachedat->scenery_hmap, &mapxz, DASHMAP_FIND);
    if( !scenery_entry )
        return NULL;
    return scenery_entry->locs;
}

struct CacheMapLocs*
buildcachedat_iter_next_scenery(struct DashMapIter* iter)
{
    struct SceneryEntry* scenery_entry = (struct SceneryEntry*)dashmap_iter_next(iter);
    if( !scenery_entry )
        return NULL;
    return scenery_entry->locs;
}

struct DashMapIter*
buildcachedat_iter_new_scenery(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->scenery_hmap);
}

void
buildcachedat_add_model(
    struct BuildCacheDat* buildcachedat,
    int model_id,
    struct CacheModel* model)
{
    struct ModelEntry* model_entry =
        (struct ModelEntry*)dashmap_search(buildcachedat->models_hmap, &model_id, DASHMAP_INSERT);
    assert(model_entry && "Model must be inserted into hmap");
    model_entry->id = model_id;
    model_entry->model = model;
}

struct CacheModel*
buildcachedat_get_model(
    struct BuildCacheDat* buildcachedat,
    int model_id)
{
    struct ModelEntry* model_entry =
        (struct ModelEntry*)dashmap_search(buildcachedat->models_hmap, &model_id, DASHMAP_FIND);
    if( !model_entry )
        return NULL;
    return model_entry->model;
}

void
buildcachedat_add_idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id,
    struct CacheModel* model)
{
    struct IdkModelEntry* idk_entry = (struct IdkModelEntry*)dashmap_search(
        buildcachedat->idk_models_hmap, &idk_id, DASHMAP_INSERT);
    assert(idk_entry && "Idk must be inserted into hmap");
    idk_entry->id = idk_id;
    idk_entry->model = model;
}

struct CacheModel*
buildcachedat_get_idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id)
{
    struct IdkModelEntry* idk_entry = (struct IdkModelEntry*)dashmap_search(
        buildcachedat->idk_models_hmap, &idk_id, DASHMAP_FIND);
    if( !idk_entry )
        return NULL;
    return idk_entry->model;
}

void
buildcachedat_add_obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id,
    struct CacheModel* model)
{
    struct ObjModelEntry* obj_entry = (struct ObjModelEntry*)dashmap_search(
        buildcachedat->obj_models_hmap, &obj_id, DASHMAP_INSERT);
    assert(obj_entry && "Obj must be inserted into hmap");
    obj_entry->id = obj_id;
    obj_entry->model = model;
}

struct CacheModel*
buildcachedat_get_obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id)
{
    struct ObjModelEntry* obj_entry = (struct ObjModelEntry*)dashmap_search(
        buildcachedat->obj_models_hmap, &obj_id, DASHMAP_FIND);
    if( !obj_entry )
        return NULL;
    return obj_entry->model;
}

void
buildcachedat_add_npc(
    struct BuildCacheDat* buildcachedat,
    int npc_id,
    struct CacheDatConfigNpc* npc)
{
    struct NpcEntry* npc_entry =
        (struct NpcEntry*)dashmap_search(buildcachedat->npc_hmap, &npc_id, DASHMAP_INSERT);
    assert(npc_entry && "Npc must be inserted into hmap");
    npc_entry->id = npc_id;
    npc_entry->npc = npc;
}

struct CacheDatConfigNpc*
buildcachedat_get_npc(
    struct BuildCacheDat* buildcachedat,
    int npc_id)
{
    struct NpcEntry* npc_entry =
        (struct NpcEntry*)dashmap_search(buildcachedat->npc_hmap, &npc_id, DASHMAP_FIND);
    if( !npc_entry )
        return NULL;
    return npc_entry->npc;
}

struct DashMapIter*
buildcachedat_iter_new_npcs(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->npc_hmap);
}

struct CacheDatConfigNpc*
buildcachedat_iter_next_npc(struct DashMapIter* iter)
{
    struct NpcEntry* npc_entry = (struct NpcEntry*)dashmap_iter_next(iter);
    if( !npc_entry )
        return NULL;
    return npc_entry->npc;
}

void
buildcachedat_add_npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_id,
    struct CacheModel* model)
{
    struct NpcModelEntry* npc_model_entry = (struct NpcModelEntry*)dashmap_search(
        buildcachedat->npc_models_hmap, &npc_id, DASHMAP_INSERT);
    assert(npc_model_entry && "Npc model must be inserted into hmap");
    npc_model_entry->id = npc_id;
    npc_model_entry->model = model;
}

struct CacheModel*
buildcachedat_get_npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_id)
{
    struct NpcModelEntry* npc_model_entry = (struct NpcModelEntry*)dashmap_search(
        buildcachedat->npc_models_hmap, &npc_id, DASHMAP_FIND);
    if( !npc_model_entry )
        return NULL;
    return npc_model_entry->model;
}

// TODO: Needs to only be done by loc id and not shape select.
void
buildcachedat_add_config_loc(
    struct BuildCacheDat* buildcachedat,
    int config_loc_id,
    struct CacheConfigLocation* config_loc)
{
    struct ConfigLocEntry* config_loc_entry = (struct ConfigLocEntry*)dashmap_search(
        buildcachedat->config_loc_hmap, &config_loc_id, DASHMAP_INSERT);
    assert(config_loc_entry && "Config loc must be inserted into hmap");
    config_loc_entry->id = config_loc_id;
    config_loc_entry->config_loc = config_loc;
}

struct CacheConfigLocation*
buildcachedat_iter_next_config_loc(struct DashMapIter* iter)
{
    struct ConfigLocEntry* config_loc_entry = (struct ConfigLocEntry*)dashmap_iter_next(iter);
    if( !config_loc_entry )
        return NULL;
    return config_loc_entry->config_loc;
}

struct CacheConfigLocation*
buildcachedat_get_config_loc(
    struct BuildCacheDat* buildcachedat,
    int config_loc_id)
{
    struct ConfigLocEntry* config_loc_entry = (struct ConfigLocEntry*)dashmap_search(
        buildcachedat->config_loc_hmap, &config_loc_id, DASHMAP_FIND);
    if( !config_loc_entry )
        return NULL;
    return config_loc_entry->config_loc;
}

struct DashMapIter*
buildcachedat_iter_new_config_locs(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->config_loc_hmap);
}

void
buildcachedat_add_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id,
    struct CacheAnimframe* animframe)
{
    struct AnimframeEntry* animframe_entry = (struct AnimframeEntry*)dashmap_search(
        buildcachedat->animframes_hmap, &animframe_id, DASHMAP_INSERT);
    assert(animframe_entry && "Animframe must be inserted into hmap");
    animframe_entry->id = animframe_id;
    animframe_entry->animframe = animframe;
}

struct CacheAnimframe*
buildcachedat_get_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id)
{
    struct AnimframeEntry* animframe_entry = (struct AnimframeEntry*)dashmap_search(
        buildcachedat->animframes_hmap, &animframe_id, DASHMAP_FIND);
    if( !animframe_entry )
        return NULL;
    return animframe_entry->animframe;
}

void
buildcachedat_add_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    struct CacheDatAnimBaseFrames* animbaseframes)
{
    struct AnimbaseframesEntry* animbaseframes_entry = (struct AnimbaseframesEntry*)dashmap_search(
        buildcachedat->animbaseframes_hmap, &animbaseframes_id, DASHMAP_INSERT);
    assert(animbaseframes_entry && "Animbaseframes must be inserted into hmap");
    animbaseframes_entry->id = animbaseframes_id;
    animbaseframes_entry->animbaseframes = animbaseframes;
}

void
buildcachedat_add_sequence(
    struct BuildCacheDat* buildcachedat,
    int sequence_id,
    struct CacheDatSequence* sequence)
{
    struct SequenceEntry* sequence_entry = (struct SequenceEntry*)dashmap_search(
        buildcachedat->sequences_hmap, &sequence_id, DASHMAP_INSERT);
    assert(sequence_entry && "Sequence must be inserted into hmap");
    sequence_entry->id = sequence_id;
    sequence_entry->sequence = sequence;
}

struct CacheDatSequence*
buildcachedat_get_sequence(
    struct BuildCacheDat* buildcachedat,
    int sequence_id)
{
    struct SequenceEntry* sequence_entry = (struct SequenceEntry*)dashmap_search(
        buildcachedat->sequences_hmap, &sequence_id, DASHMAP_FIND);
    if( !sequence_entry )
        return NULL;
    return sequence_entry->sequence;
}

void
buildcachedat_add_idk(
    struct BuildCacheDat* buildcachedat,
    int idk_id,
    struct CacheDatConfigIdk* idk)
{
    struct IdkEntry* idk_entry =
        (struct IdkEntry*)dashmap_search(buildcachedat->idk_hmap, &idk_id, DASHMAP_INSERT);
    assert(idk_entry && "Idk must be inserted into hmap");
    idk_entry->id = idk_id;
    idk_entry->idk = idk;
}

struct CacheDatConfigIdk*
buildcachedat_get_idk(
    struct BuildCacheDat* buildcachedat,
    int idk_id)
{
    struct IdkEntry* idk_entry =
        (struct IdkEntry*)dashmap_search(buildcachedat->idk_hmap, &idk_id, DASHMAP_FIND);
    if( !idk_entry )
        return NULL;
    return idk_entry->idk;
}

struct CacheDatConfigIdk*
buildcachedat_iter_next_idk(struct DashMapIter* iter)
{
    struct IdkEntry* idk_entry = (struct IdkEntry*)dashmap_iter_next(iter);
    if( !idk_entry )
        return NULL;
    return idk_entry->idk;
}

struct DashMapIter*
buildcachedat_iter_new_idks(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->idk_hmap);
}

void
buildcachedat_add_obj(
    struct BuildCacheDat* buildcachedat,
    int obj_id,
    struct CacheDatConfigObj* obj)
{
    struct ObjEntry* obj_entry =
        (struct ObjEntry*)dashmap_search(buildcachedat->obj_hmap, &obj_id, DASHMAP_INSERT);
    assert(obj_entry && "Obj must be inserted into hmap");
    obj_entry->id = obj_id;
    obj_entry->obj = obj;
}

struct CacheDatConfigObj*
buildcachedat_get_obj(
    struct BuildCacheDat* buildcachedat,
    int obj_id)
{
    struct ObjEntry* obj_entry =
        (struct ObjEntry*)dashmap_search(buildcachedat->obj_hmap, &obj_id, DASHMAP_FIND);
    if( !obj_entry )
        return NULL;
    return obj_entry->obj;
}

struct CacheDatConfigObj*
buildcachedat_iter_next_obj(struct DashMapIter* iter)
{
    struct ObjEntry* obj_entry = (struct ObjEntry*)dashmap_iter_next(iter);
    if( !obj_entry )
        return NULL;
    return obj_entry->obj;
}

struct DashMapIter*
buildcachedat_iter_new_objs(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->obj_hmap);
}

void
buildcachedat_add_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* map_terrain_entry = (struct MapTerrainEntry*)dashmap_search(
        buildcachedat->map_terrains_hmap, &mapxz, DASHMAP_INSERT);
    assert(map_terrain_entry && "Map terrain must be inserted into hmap");
    map_terrain_entry->id = mapxz;
    map_terrain_entry->mapx = mapx;
    map_terrain_entry->mapz = mapz;
    map_terrain_entry->map_terrain = map_terrain;
}

struct CacheMapTerrain*
buildcachedat_get_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* map_terrain_entry = (struct MapTerrainEntry*)dashmap_search(
        buildcachedat->map_terrains_hmap, &mapxz, DASHMAP_FIND);
    if( !map_terrain_entry )
        return NULL;
    return map_terrain_entry->map_terrain;
}