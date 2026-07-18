#include "uitree_scene_bridge.h"

#include "engine/cache_provider.h"
#include "engine/toridraw_font_from_torirs.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/toridraw_sprite_from_torirs.h"
#include "engine/torirs_types.h"

#include "hmap.h"
#include "toridraw_font.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_SPRITE_MAP_CAP 4096
#define BRIDGE_MODEL_MAP_CAP 4096

struct MapEntry_BridgeId
{
    int cache_id;
    int scene_id;
};

static size_t
bridge_hmap_buffer_bytes(
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
bridge_hmap_new(
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = bridge_hmap_buffer_bytes(entry_size, capacity);
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
bridge_hmap_free(struct HMap* map)
{
    if( !map )
        return;
    free(hmap_free(map));
}

void
UITreeSceneBridge_Init(
    struct UITreeSceneBridge* bridge,
    struct ToriDraw_Scene* scene,
    struct CacheProvider* provider)
{
    assert(bridge);
    assert(scene);
    assert(provider);
    memset(bridge, 0, sizeof(*bridge));
    bridge->scene = scene;
    bridge->provider = provider;
    bridge->next_scene_id = 1;
    bridge->sprite_map = bridge_hmap_new(sizeof(struct MapEntry_BridgeId), BRIDGE_SPRITE_MAP_CAP);
    bridge->model_map = bridge_hmap_new(sizeof(struct MapEntry_BridgeId), BRIDGE_MODEL_MAP_CAP);
    assert(bridge->sprite_map && bridge->model_map);
}

void
UITreeSceneBridge_Free(struct UITreeSceneBridge* bridge)
{
    if( !bridge )
        return;
    bridge_hmap_free(bridge->sprite_map);
    bridge_hmap_free(bridge->model_map);
    memset(bridge, 0, sizeof(*bridge));
}

static int
bridge_map_get(
    struct HMap* map,
    int cache_id)
{
    struct MapEntry_BridgeId* entry;
    if( !map || cache_id < 0 )
        return -1;
    entry = (struct MapEntry_BridgeId*)hmap_search(map, &cache_id, HMAP_FIND);
    if( !entry )
        return -1;
    return entry->scene_id;
}

static void
bridge_map_put(
    struct HMap* map,
    int cache_id,
    int scene_id)
{
    struct MapEntry_BridgeId* entry;
    assert(map);
    entry = (struct MapEntry_BridgeId*)hmap_search(map, &cache_id, HMAP_INSERT);
    assert(entry);
    entry->cache_id = cache_id;
    entry->scene_id = scene_id;
}

int
UITreeSceneBridge_EnsureSprite(
    struct UITreeSceneBridge* bridge,
    int cache_graphic_id)
{
    int scene_id;
    struct ToriRS_Sprite* rs;
    struct ToriDraw_Sprite** sprites;
    int count = 0;

    assert(bridge);
    assert(bridge->scene);
    assert(bridge->provider);

    if( cache_graphic_id <= 0 )
        return -1;

    scene_id = bridge_map_get(bridge->sprite_map, cache_graphic_id);
    if( scene_id > 0 )
        return scene_id;

    if( !CacheProvider_SpriteHas(bridge->provider, cache_graphic_id) )
        return -1;

    rs = CacheProvider_SpriteGet(bridge->provider, cache_graphic_id);
    if( !rs )
        return -1;

    sprites = ToriDraw_SpritesFromToriRS(rs, &count);
    if( !sprites || count <= 0 )
        return -1;

    scene_id = bridge->next_scene_id++;
    ToriDraw_SceneSpriteAdd(bridge->scene, scene_id, sprites, count);
    bridge_map_put(bridge->sprite_map, cache_graphic_id, scene_id);
    return scene_id;
}

int
UITreeSceneBridge_EnsureFont(
    struct UITreeSceneBridge* bridge,
    int cache_font_id)
{
    struct ToriRS_Font* rs;
    struct ToriDraw_Font* font;

    assert(bridge);
    assert(bridge->scene);
    assert(bridge->provider);

    if( cache_font_id < 0 )
        return -1;

    if( ToriDraw_SceneFontHas(bridge->scene, cache_font_id) )
        return cache_font_id;

    if( !CacheProvider_FontHas(bridge->provider, cache_font_id) )
        return -1;

    rs = CacheProvider_FontGet(bridge->provider, cache_font_id);
    if( !rs )
        return -1;

    font = ToriDraw_FontFromToriRS(rs);
    if( !font )
        return -1;

    ToriDraw_SceneFontAdd(bridge->scene, cache_font_id, font);
    return cache_font_id;
}

int
UITreeSceneBridge_EnsureModel(
    struct UITreeSceneBridge* bridge,
    int cache_model_id)
{
    int scene_id;
    struct ToriRS_Model* rs;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;

    assert(bridge);
    assert(bridge->scene);
    assert(bridge->provider);

    if( cache_model_id < 0 )
        return -1;

    scene_id = bridge_map_get(bridge->model_map, cache_model_id);
    if( scene_id > 0 )
        return scene_id;

    if( ToriDraw_SceneModelHas(bridge->scene, cache_model_id) )
    {
        bridge_map_put(bridge->model_map, cache_model_id, cache_model_id);
        return cache_model_id;
    }

    if( !CacheProvider_ModelHas(bridge->provider, cache_model_id) )
        return -1;

    rs = CacheProvider_ModelGet(bridge->provider, cache_model_id);
    if( !rs )
        return -1;

    model = ToriDraw_ModelFromToriRS(rs);
    if( !model )
        return -1;

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_SceneModelAdd(bridge->scene, cache_model_id, hnd);
    bridge_map_put(bridge->model_map, cache_model_id, cache_model_id);
    return cache_model_id;
}
