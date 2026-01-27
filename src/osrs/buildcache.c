#include "buildcache.h"

#include "graphics/dash.h"
#include "osrs/rscache/tables/maps.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapTerrainEntry
{
    int mapxz;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};

struct MapSceneryEntry
{
    int mapxz;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct ConfigOverlayEntry
{
    int id;
    struct CacheConfigOverlay* config_overlay;
};

struct ConfigUnderlayEntry
{
    int id;
    struct CacheConfigUnderlay* config_underlay;
};

struct ConfigSequenceEntry
{
    int id;
    struct CacheConfigSequence* config_sequence;
};

struct ConfigLocationEntry
{
    int id;
    struct CacheConfigLocation* config_location;
};

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct SpritepackEntry
{
    int id;
    struct CacheSpritePack* spritepack;
};

struct TextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct FrameBlobEntry
{
    int id;
    struct CacheFrameBlob* frame_blob;
};

struct FrameAnimEntry
{
    int id;
    struct CacheFrame* frame;
};

struct FramemapEntry
{
    int id;
    struct CacheFramemap* framemap;
};

struct BuildCache*
buildcache_new(void)
{
    struct BuildCache* buildcache = malloc(sizeof(struct BuildCache));
    memset(buildcache, 0, sizeof(struct BuildCache));

    struct DashMapConfig config = { 0 };

    size_t buffer_size = 1024 * 64;
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapTerrainEntry),
    };
    buildcache->map_terrain_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapSceneryEntry),
    };
    buildcache->map_scenery_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ConfigOverlayEntry),
    };
    buildcache->config_overlay_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ConfigUnderlayEntry),
    };
    buildcache->config_underlay_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ConfigSequenceEntry),
    };
    buildcache->config_sequence_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ModelEntry),
    };
    buildcache->models_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SpritepackEntry),
    };
    buildcache->spritepacks_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct TextureEntry),
    };
    buildcache->textures_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FrameBlobEntry),
    };
    buildcache->frame_blob_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FrameAnimEntry),
    };
    buildcache->frame_anim_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FramemapEntry),
    };
    buildcache->framemaps_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ConfigLocationEntry),
    };
    buildcache->config_location_hmap = dashmap_new(&config, 0);

    return buildcache;
}

void
buildcache_free(struct BuildCache* buildcache)
{
    dashmap_free(buildcache->map_terrain_hmap);
    dashmap_free(buildcache->map_scenery_hmap);
    dashmap_free(buildcache->config_overlay_hmap);
    dashmap_free(buildcache->config_underlay_hmap);
    dashmap_free(buildcache->config_sequence_hmap);
    dashmap_free(buildcache->config_location_hmap);
    dashmap_free(buildcache->models_hmap);
    dashmap_free(buildcache->spritepacks_hmap);
    dashmap_free(buildcache->textures_hmap);
    dashmap_free(buildcache->frame_blob_hmap);
    dashmap_free(buildcache->frame_anim_hmap);
    dashmap_free(buildcache->framemaps_hmap);
    free(buildcache);
}

void
buildcache_add_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* entry = (struct MapTerrainEntry*)dashmap_search(
        buildcache->map_terrain_hmap, &mapxz, DASHMAP_INSERT);
    assert(entry && "Map terrain must be inserted into hmap");
    entry->mapxz = mapxz;
    entry->mapx = mapx;
    entry->mapz = mapz;
    entry->map_terrain = map_terrain;
}

struct CacheMapTerrain*
buildcache_get_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* entry =
        (struct MapTerrainEntry*)dashmap_search(buildcache->map_terrain_hmap, &mapxz, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->map_terrain;
}

void
buildcache_add_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    struct CacheMapLocs* locs)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapSceneryEntry* entry = (struct MapSceneryEntry*)dashmap_search(
        buildcache->map_scenery_hmap, &mapxz, DASHMAP_INSERT);
    assert(entry && "Map scenery must be inserted into hmap");
    entry->mapxz = mapxz;
    entry->mapx = mapx;
    entry->mapz = mapz;
    entry->locs = locs;
}

struct CacheMapLocs*
buildcache_get_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapSceneryEntry* entry =
        (struct MapSceneryEntry*)dashmap_search(buildcache->map_scenery_hmap, &mapxz, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

struct DashMapIter*
buildcache_iter_new_map_scenery(struct BuildCache* buildcache)
{
    return dashmap_iter_new(buildcache->map_scenery_hmap);
}

struct CacheMapLocs*
buildcache_iter_next_map_scenery(
    struct DashMapIter* iter,
    int* mapx,
    int* mapz)
{
    struct MapSceneryEntry* entry = (struct MapSceneryEntry*)dashmap_iter_next(iter);
    if( !entry )
        return NULL;
    *mapx = entry->mapx;
    *mapz = entry->mapz;
    return entry->locs;
}

void
buildcache_iter_free_map_scenery(struct DashMapIter* iter)
{
    dashmap_iter_free(iter);
}

struct DashMapIter*
buildcache_iter_new_models(struct BuildCache* buildcache)
{
    return dashmap_iter_new(buildcache->models_hmap);
}

struct CacheModel*
buildcache_iter_next_models(
    struct DashMapIter* iter,
    int* model_id)
{
    struct ModelEntry* entry = (struct ModelEntry*)dashmap_iter_next(iter);
    if( !entry )
        return NULL;
    *model_id = entry->id;
    return entry->model;
}

void
buildcache_iter_free_models(struct DashMapIter* iter)
{
    dashmap_iter_free(iter);
}

void
buildcache_add_config_location(
    struct BuildCache* buildcache,
    int config_location_id,
    struct CacheConfigLocation* config_location)
{
    struct ConfigLocationEntry* entry = (struct ConfigLocationEntry*)dashmap_search(
        buildcache->config_location_hmap, &config_location_id, DASHMAP_INSERT);
    assert(entry && "Config location must be inserted into hmap");
    entry->id = config_location_id;
    entry->config_location = config_location;
}

struct CacheConfigLocation*
buildcache_get_config_location(
    struct BuildCache* buildcache,
    int config_location_id)
{
    struct ConfigLocationEntry* entry = (struct ConfigLocationEntry*)dashmap_search(
        buildcache->config_location_hmap, &config_location_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_location;
}

void
buildcache_add_config_overlay(
    struct BuildCache* buildcache,
    int config_overlay_id,
    struct CacheConfigOverlay* config_overlay)
{
    struct ConfigOverlayEntry* entry = (struct ConfigOverlayEntry*)dashmap_search(
        buildcache->config_overlay_hmap, &config_overlay_id, DASHMAP_INSERT);
    assert(entry && "Config overlay must be inserted into hmap");
    entry->id = config_overlay_id;
    entry->config_overlay = config_overlay;
}

struct CacheConfigOverlay*
buildcache_get_config_overlay(
    struct BuildCache* buildcache,
    int config_overlay_id)
{
    struct ConfigOverlayEntry* entry = (struct ConfigOverlayEntry*)dashmap_search(
        buildcache->config_overlay_hmap, &config_overlay_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_overlay;
}

struct DashMapIter*
buildcache_iter_new_config_overlay(struct BuildCache* buildcache)
{
    return dashmap_iter_new(buildcache->config_overlay_hmap);
}

struct CacheConfigOverlay*
buildcache_iter_next_config_overlay(struct DashMapIter* iter)
{
    struct ConfigOverlayEntry* entry = (struct ConfigOverlayEntry*)dashmap_iter_next(iter);
    if( !entry )
        return NULL;
    return entry->config_overlay;
}

void
buildcache_iter_free_config_overlay(struct DashMapIter* iter)
{
    dashmap_iter_free(iter);
}

void
buildcache_add_config_underlay(
    struct BuildCache* buildcache,
    int config_underlay_id,
    struct CacheConfigUnderlay* config_underlay)
{
    struct ConfigUnderlayEntry* entry = (struct ConfigUnderlayEntry*)dashmap_search(
        buildcache->config_underlay_hmap, &config_underlay_id, DASHMAP_INSERT);
    assert(entry && "Config underlay must be inserted into hmap");
    entry->id = config_underlay_id;
    entry->config_underlay = config_underlay;
}

struct CacheConfigUnderlay*
buildcache_get_config_underlay(
    struct BuildCache* buildcache,
    int config_underlay_id)
{
    struct ConfigUnderlayEntry* entry = (struct ConfigUnderlayEntry*)dashmap_search(
        buildcache->config_underlay_hmap, &config_underlay_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_underlay;
}

void
buildcache_add_config_sequence(
    struct BuildCache* buildcache,
    int config_sequence_id,
    struct CacheConfigSequence* config_sequence)
{
    struct ConfigSequenceEntry* entry = (struct ConfigSequenceEntry*)dashmap_search(
        buildcache->config_sequence_hmap, &config_sequence_id, DASHMAP_INSERT);
    assert(entry && "Config sequence must be inserted into hmap");
    entry->id = config_sequence_id;
    entry->config_sequence = config_sequence;
}

struct CacheConfigSequence*
buildcache_get_config_sequence(
    struct BuildCache* buildcache,
    int config_sequence_id)
{
    struct ConfigSequenceEntry* entry = (struct ConfigSequenceEntry*)dashmap_search(
        buildcache->config_sequence_hmap, &config_sequence_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->config_sequence;
}

void
buildcache_add_model(
    struct BuildCache* buildcache,
    int model_id,
    struct CacheModel* model)
{
    struct ModelEntry* entry =
        (struct ModelEntry*)dashmap_search(buildcache->models_hmap, &model_id, DASHMAP_INSERT);
    assert(entry && "Model must be inserted into hmap");
    entry->id = model_id;
    entry->model = model;
}

struct CacheModel*
buildcache_get_model(
    struct BuildCache* buildcache,
    int model_id)
{
    struct ModelEntry* entry =
        (struct ModelEntry*)dashmap_search(buildcache->models_hmap, &model_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
buildcache_add_spritepack(
    struct BuildCache* buildcache,
    int spritepack_id,
    struct CacheSpritePack* spritepack)
{
    struct SpritepackEntry* entry = (struct SpritepackEntry*)dashmap_search(
        buildcache->spritepacks_hmap, &spritepack_id, DASHMAP_INSERT);
    assert(entry && "Spritepack must be inserted into hmap");
    entry->id = spritepack_id;
    entry->spritepack = spritepack;
}

struct CacheSpritePack*
buildcache_get_spritepack(
    struct BuildCache* buildcache,
    int spritepack_id)
{
    struct SpritepackEntry* entry = (struct SpritepackEntry*)dashmap_search(
        buildcache->spritepacks_hmap, &spritepack_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->spritepack;
}

void
buildcache_add_texture(
    struct BuildCache* buildcache,
    int texture_id,
    struct DashTexture* texture)
{
    struct TextureEntry* entry = (struct TextureEntry*)dashmap_search(
        buildcache->textures_hmap, &texture_id, DASHMAP_INSERT);
    assert(entry && "Texture must be inserted into hmap");
    entry->id = texture_id;
    entry->texture = texture;
}

struct DashTexture*
buildcache_get_texture(
    struct BuildCache* buildcache,
    int texture_id)
{
    struct TextureEntry* entry =
        (struct TextureEntry*)dashmap_search(buildcache->textures_hmap, &texture_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->texture;
}

void
buildcache_add_frame_blob(
    struct BuildCache* buildcache,
    int frame_blob_id,
    struct CacheFrameBlob* frame_blob)
{
    struct FrameBlobEntry* entry = (struct FrameBlobEntry*)dashmap_search(
        buildcache->frame_blob_hmap, &frame_blob_id, DASHMAP_INSERT);
    assert(entry && "Frame blob must be inserted into hmap");
    entry->id = frame_blob_id;
    entry->frame_blob = frame_blob;
}

struct CacheFrameBlob*
buildcache_get_frame_blob(
    struct BuildCache* buildcache,
    int frame_blob_id)
{
    struct FrameBlobEntry* entry = (struct FrameBlobEntry*)dashmap_search(
        buildcache->frame_blob_hmap, &frame_blob_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->frame_blob;
}

void
buildcache_add_frame_anim(
    struct BuildCache* buildcache,
    int frame_anim_id,
    struct CacheFrame* frame)
{
    struct FrameAnimEntry* entry = (struct FrameAnimEntry*)dashmap_search(
        buildcache->frame_anim_hmap, &frame_anim_id, DASHMAP_INSERT);
    assert(entry && "Frame anim must be inserted into hmap");
    entry->id = frame_anim_id;
    entry->frame = frame;
}

struct CacheFrame*
buildcache_get_frame_anim(
    struct BuildCache* buildcache,
    int frame_anim_id)
{
    struct FrameAnimEntry* entry = (struct FrameAnimEntry*)dashmap_search(
        buildcache->frame_anim_hmap, &frame_anim_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->frame;
}

void
buildcache_add_framemap(
    struct BuildCache* buildcache,
    int framemap_id,
    struct CacheFramemap* framemap)
{
    struct FramemapEntry* entry = (struct FramemapEntry*)dashmap_search(
        buildcache->framemaps_hmap, &framemap_id, DASHMAP_INSERT);
    assert(entry && "Framemap must be inserted into hmap");
    entry->id = framemap_id;
    entry->framemap = framemap;
}

struct CacheFramemap*
buildcache_get_framemap(
    struct BuildCache* buildcache,
    int framemap_id)
{
    struct FramemapEntry* entry = (struct FramemapEntry*)dashmap_search(
        buildcache->framemaps_hmap, &framemap_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->framemap;
}