#include "uitree_scene_bridge.h"

#include "engine/cache_provider.h"
#include "engine/player_appearance.h"
#include "engine/toridraw_font_from_torirs.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/toridraw_sprite_from_torirs.h"
#include "engine/torirs_types.h"
#include "hmap.h"
#include "toridraw_font.h"
#include "toridraw_light_model.h"
#include "toridraw_model.h"
#include "toridraw_model_sprite.h"
#include "toridraw_model_transform.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_SPRITE_MAP_CAP 4096
#define BRIDGE_MODEL_MAP_CAP 4096
#define BRIDGE_OBJ_ICON_MAP_CAP 4096

struct MapEntry_BridgeId
{
    int cache_id;
    int scene_id;
};

struct MapEntry_ObjIcon
{
    int obj_id;
    int count;
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
bridge_hmap_new_keyed(
    size_t key_size,
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = bridge_hmap_buffer_bytes(entry_size, capacity);
    void* buffer = malloc(buffer_size);
    assert(buffer);
    struct HashConfig config = {
        .key_size = key_size,
        .entry_size = entry_size,
        .buffer = buffer,
        .buffer_size = buffer_size,
        .capacity = capacity,
    };
    struct HMap* map = hmap_new(&config, 0);
    assert(map);
    return map;
}

static struct HMap*
bridge_hmap_new(
    size_t entry_size,
    size_t capacity)
{
    return bridge_hmap_new_keyed(sizeof(int), entry_size, capacity);
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
    bridge->obj_icon_map = bridge_hmap_new_keyed(
        sizeof(int) * 2, sizeof(struct MapEntry_ObjIcon), BRIDGE_OBJ_ICON_MAP_CAP);
    bridge->scrollbar_scene_id = -1;
    bridge->player_scene_id = -1;
    assert(bridge->sprite_map && bridge->model_map && bridge->obj_icon_map);
}

void
UITreeSceneBridge_Free(struct UITreeSceneBridge* bridge)
{
    if( !bridge )
        return;
    bridge_hmap_free(bridge->sprite_map);
    bridge_hmap_free(bridge->model_map);
    bridge_hmap_free(bridge->obj_icon_map);
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
UITreeSceneBridge_EnsureScrollbar(
    struct UITreeSceneBridge* bridge,
    int cache_graphic_id)
{
    int scene_id;

    assert(bridge);
    if( bridge->scrollbar_scene_id > 0 )
        return bridge->scrollbar_scene_id;

    scene_id = UITreeSceneBridge_EnsureSprite(bridge, cache_graphic_id);
    if( scene_id > 0 )
        bridge->scrollbar_scene_id = scene_id;
    return scene_id;
}

int
UITreeSceneBridge_ScrollbarSceneId(struct UITreeSceneBridge const* bridge)
{
    assert(bridge);
    return bridge->scrollbar_scene_id;
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
    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);
    /* Rest-pose snapshot enables IF/CC_SETMODELANIM sequence playback on widgets. */
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_SceneModelAdd(bridge->scene, cache_model_id, hnd);
    bridge_map_put(bridge->model_map, cache_model_id, cache_model_id);
    return cache_model_id;
}

#define BRIDGE_PLAYER_PART_MODELS_MAX (PLAYER_APPEARANCE_PARTS * 8)

int
UITreeSceneBridge_EnsurePlayerModel(struct UITreeSceneBridge* bridge)
{
    struct PlayerAppearance app;
    struct ToriDraw_Model* parts[BRIDGE_PLAYER_PART_MODELS_MAX];
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;
    int part_count = 0;
    int p;

    assert(bridge && bridge->scene && bridge->provider);

    if( bridge->player_scene_id > 0 )
        return bridge->player_scene_id;
    if( ToriDraw_SceneModelHas(bridge->scene, UITREE_SCENE_PLAYER_MODEL_ID) )
    {
        bridge->player_scene_id = UITREE_SCENE_PLAYER_MODEL_ID;
        return bridge->player_scene_id;
    }

    if( PlayerAppearance_ResolveDefaultMale(bridge->provider, &app) <= 0 )
        return -1;

    /* Compose body parts 0..6: one merged, kit-recolored model per IdentityKit. */
    for( p = 0; p < PLAYER_APPEARANCE_PARTS; p++ )
    {
        struct ToriRS_Idk* idk;
        int m;
        if( app.kits[p] < 0 )
            continue;
        idk = CacheProvider_IdkGet(bridge->provider, app.kits[p]);
        if( !idk )
            continue;
        for( m = 0; m < idk->model_ids_count; m++ )
        {
            struct ToriRS_Model* rs;
            struct ToriDraw_Model* model;
            int mid = idk->model_ids[m];
            int r;
            if( mid < 0 || !CacheProvider_ModelHas(bridge->provider, mid) )
                continue;
            rs = CacheProvider_ModelGet(bridge->provider, mid);
            if( !rs )
                continue;
            model = ToriDraw_ModelFromToriRS(rs);
            if( !model )
                continue;
            /* Kit recolors applied per part before merge (mirrors PlayerComposition). */
            for( r = 0; r < 10; r++ )
            {
                if( idk->recolors_from[r] != 0 || idk->recolors_to[r] != 0 )
                    ToriDraw_ModelRecolor(model, idk->recolors_from[r], idk->recolors_to[r]);
            }
            if( part_count < BRIDGE_PLAYER_PART_MODELS_MAX )
                parts[part_count++] = model;
            else
                ToriDraw_ModelFree(model);
        }
    }

    if( part_count == 0 )
        return -1;

    /* ModelNewMerge copies geometry, so the part models must be freed after. */
    merged = ToriDraw_ModelNewMerge(parts, part_count);
    for( p = 0; p < part_count; p++ )
        ToriDraw_ModelFree(parts[p]);
    if( !merged )
        return -1;

    /* Default-appearance body recolors (colors all 0 → mostly identity). */
    {
        int c;
        for( c = 0; c < PlayerAppearance_DefaultBodyRecolorCount(); c++ )
        {
            int from = PlayerAppearance_DefaultBodyRecolorFrom(c);
            if( from != -1 )
                ToriDraw_ModelRecolor(merged, from, PlayerAppearance_DefaultBodyRecolorTo(c));
        }
    }

    ToriDraw_ModelSetBoundsCylinder(merged);
    /* Snapshot the rest pose so sequence frames can be applied/reset each tick. */
    ToriDraw_ModelCaptureOriginalVertices(merged);

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);
    ToriDraw_SceneModelAdd(bridge->scene, UITREE_SCENE_PLAYER_MODEL_ID, hnd);
    bridge->player_scene_id = UITREE_SCENE_PLAYER_MODEL_ID;
    return bridge->player_scene_id;
}

static struct ToriRS_Objtype*
bridge_resolve_obj_for_icon(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    int i;
    int countobj_id = -1;

    if( !provider || obj_id <= 0 )
        return NULL;
    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    if( !obj )
        return NULL;

    if( count > 1 )
    {
        for( i = 0; i < 10; i++ )
        {
            if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
                countobj_id = obj->count_obj[i];
        }
        if( countobj_id > 0 )
        {
            struct ToriRS_Objtype* variant = CacheProvider_ObjtypeGet(provider, countobj_id);
            if( variant )
                obj = variant;
        }
    }
    return obj;
}

int
UITreeSceneBridge_EnsureObjIcon(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count)
{
    struct MapEntry_ObjIcon* entry;
    int key[2];
    struct ToriRS_Objtype* obj;
    struct ToriRS_Model* rs_model;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** frames;
    int scene_id;
    int zoom;
    int i;

    assert(bridge);
    assert(bridge->scene);
    assert(bridge->provider);

    if( obj_id <= 0 )
        return -1;
    if( count < 0 )
        count = 0;

    key[0] = obj_id;
    key[1] = count;
    entry = (struct MapEntry_ObjIcon*)hmap_search(bridge->obj_icon_map, key, HMAP_FIND);
    if( entry )
        return entry->scene_id;

    obj = bridge_resolve_obj_for_icon(bridge->provider, obj_id, count);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    if( !CacheProvider_ModelHas(bridge->provider, obj->inventory_model_id) )
        return -1;

    rs_model = CacheProvider_ModelGet(bridge->provider, obj->inventory_model_id);
    assert(rs_model != NULL && "ModelGet failed");

    model = ToriDraw_ModelFromToriRS(rs_model);
    assert(model != NULL && "ModelFromToriRS failed");

    for( i = 0; i < obj->recolor_count; i++ )
        ToriDraw_ModelRecolor(model, obj->recolors_from[i], obj->recolors_to[i]);

    ToriDraw_ModelSetBoundsCylinder(model);

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_LightModelDefault(hnd, obj->contrast, obj->ambient);

    zoom = obj->zoom2d > 0 ? obj->zoom2d : 2000;
    /* Reference icon raster is 36x32 with the 3D projection center at (16,16)
     * (ItemIconRenderer.OSRS_SPRITE_W/H); rasterizing 32x32 shifts every icon
     * ~2px and clips the right edge. */
    sprite = ToriDraw_SpriteNewFromObjIconRaster(
        bridge->scene,
        hnd,
        zoom,
        obj->xan2d,
        obj->yan2d,
        obj->zan2d,
        obj->offset_x2d,
        obj->offset_y2d,
        36,
        32,
        true);
    ToriDraw_ModelFree(model);
    if( !sprite )
        return -1;

    frames = malloc(sizeof(*frames));
    if( !frames )
    {
        ToriDraw_SpriteFree(sprite);
        return -1;
    }
    frames[0] = sprite;

    scene_id = bridge->next_scene_id++;
    ToriDraw_SceneSpriteAdd(bridge->scene, scene_id, frames, 1);

    entry = (struct MapEntry_ObjIcon*)hmap_search(bridge->obj_icon_map, key, HMAP_INSERT);
    assert(entry);
    entry->obj_id = obj_id;
    entry->count = count;
    entry->scene_id = scene_id;
    return scene_id;
}
