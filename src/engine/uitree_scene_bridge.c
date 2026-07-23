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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_SPRITE_MAP_CAP 4096
#define BRIDGE_MODEL_MAP_CAP 4096
#define BRIDGE_OBJ_ICON_MAP_CAP 4096

static struct ToriDraw_Texture*
bridge_texture_from_torirs(const struct ToriRS_Texture* rs);

/* Upload provider textures referenced by rs_model's faces into the scene
 * texture map; the raster reads only the scene map. Ids the provider lacks
 * are left alone (raster skips those faces). */
static void
bridge_publish_model_textures(
    struct UITreeSceneBridge* bridge,
    const struct ToriRS_Model* rs_model)
{
    struct ToriDraw_TextureState* tex_state;

    if( !rs_model || !rs_model->face_textures )
        return;
    tex_state = ToriDraw_SceneTexState(bridge->scene);
    if( !tex_state )
        return;

    for( int f = 0; f < rs_model->face_count; f++ )
    {
        int texture_id = (int)rs_model->face_textures[f];
        struct ToriRS_Texture* rs;
        struct ToriDraw_Texture* texture;

        if( texture_id < 0 || texture_id >= 256 )
            continue;
        if( tex_state->texture_map.textures[texture_id] )
            continue;

        rs = CacheProvider_TextureGet(bridge->provider, texture_id);
        if( !rs )
            continue;
        texture = bridge_texture_from_torirs(rs);
        if( !texture )
            continue;
        ToriDraw_SceneSetTexture(bridge->scene, texture_id, texture);
    }
}

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
    bridge->npc_head_map = bridge_hmap_new(sizeof(struct MapEntry_BridgeId), BRIDGE_MODEL_MAP_CAP);
    for( int slot = 0; slot < STATIC_SPRITE_COUNT; slot++ )
        bridge->static_sprite_scene[slot] = -1;
    bridge->player_scene_id = -1;
    bridge->player_head_scene_id = -1;
    assert(bridge->sprite_map && bridge->model_map && bridge->obj_icon_map && bridge->npc_head_map);
}

void
UITreeSceneBridge_Free(struct UITreeSceneBridge* bridge)
{
    if( !bridge )
        return;
    bridge_hmap_free(bridge->sprite_map);
    bridge_hmap_free(bridge->model_map);
    bridge_hmap_free(bridge->obj_icon_map);
    bridge_hmap_free(bridge->npc_head_map);
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
UITreeSceneBridge_SpriteCacheIdForScene(
    struct UITreeSceneBridge const* bridge,
    int scene_id)
{
    struct HMapIter* iter;
    struct MapEntry_BridgeId* entry;
    int cache_id = -1;

    if( !bridge || !bridge->sprite_map || scene_id < 0 )
        return -1;

    iter = hmap_iter_new(bridge->sprite_map);
    while( (entry = (struct MapEntry_BridgeId*)hmap_iter_next(iter)) )
    {
        if( entry->scene_id == scene_id )
        {
            cache_id = entry->cache_id;
            break;
        }
    }
    hmap_iter_free(iter);
    return cache_id;
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
UITreeSceneBridge_EnsureStaticSprite(
    struct UITreeSceneBridge* bridge,
    enum StaticSpriteSlot slot,
    int cache_graphic_id)
{
    int scene_id;

    assert(bridge);
    assert(slot >= 0 && slot < STATIC_SPRITE_COUNT);
    if( bridge->static_sprite_scene[slot] > 0 )
        return bridge->static_sprite_scene[slot];

    scene_id = UITreeSceneBridge_EnsureSprite(bridge, cache_graphic_id);
    if( scene_id > 0 )
        bridge->static_sprite_scene[slot] = scene_id;
    return scene_id;
}

int
UITreeSceneBridge_StaticSpriteSceneId(
    struct UITreeSceneBridge const* bridge,
    enum StaticSpriteSlot slot)
{
    assert(bridge);
    assert(slot >= 0 && slot < STATIC_SPRITE_COUNT);
    return bridge->static_sprite_scene[slot];
}

int
UITreeSceneBridge_EnsureScrollbar(
    struct UITreeSceneBridge* bridge,
    int cache_graphic_id)
{
    return UITreeSceneBridge_EnsureStaticSprite(
        bridge, STATIC_SPRITE_SCROLLBAR, cache_graphic_id);
}

int
UITreeSceneBridge_ScrollbarSceneId(struct UITreeSceneBridge const* bridge)
{
    return UITreeSceneBridge_StaticSpriteSceneId(bridge, STATIC_SPRITE_SCROLLBAR);
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

    {
        int resolved = PlayerAppearance_ResolveDefaultMale(bridge->provider, &app);
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(stderr, "EnsurePlayerModel: resolved=%d kits=[%d,%d,%d,%d,%d,%d,%d]\n",
                resolved, app.kits[0], app.kits[1], app.kits[2], app.kits[3],
                app.kits[4], app.kits[5], app.kits[6]);
        if( resolved <= 0 )
            return -1;
    }

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

#define BRIDGE_NPC_HEAD_PARTS_MAX 16

int
UITreeSceneBridge_EnsureNpcHead(
    struct UITreeSceneBridge* bridge,
    int npc_id)
{
    struct ToriRS_Npctype* npc;
    struct ToriDraw_Model* parts[BRIDGE_NPC_HEAD_PARTS_MAX];
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;
    int part_count = 0;
    int scene_id;

    assert(bridge && bridge->scene && bridge->provider);
    if( npc_id < 0 )
        return -1;

    scene_id = bridge_map_get(bridge->npc_head_map, npc_id);
    if( scene_id > 0 )
        return scene_id;

    if( !CacheProvider_NpctypeHas(bridge->provider, npc_id) )
        return -1;
    npc = CacheProvider_NpctypeGet(bridge->provider, npc_id);
    if( !npc || npc->heads_count <= 0 || !npc->heads )
        return -1;

    /* Merge the head models (reference NpcType.getHead / v0 npc_head_model). */
    for( int i = 0; i < npc->heads_count; i++ )
    {
        struct ToriRS_Model* rs;
        struct ToriDraw_Model* model;
        int mid = npc->heads[i];
        if( mid < 0 || !CacheProvider_ModelHas(bridge->provider, mid) )
            continue;
        rs = CacheProvider_ModelGet(bridge->provider, mid);
        if( !rs )
            continue;
        model = ToriDraw_ModelFromToriRS(rs);
        if( !model )
            continue;
        if( part_count < BRIDGE_NPC_HEAD_PARTS_MAX )
            parts[part_count++] = model;
        else
            ToriDraw_ModelFree(model);
    }
    if( part_count == 0 )
        return -1;

    merged = ToriDraw_ModelNewMerge(parts, part_count);
    for( int i = 0; i < part_count; i++ )
        ToriDraw_ModelFree(parts[i]);
    if( !merged )
        return -1;

    for( int r = 0; r < npc->recolor_count; r++ )
        ToriDraw_ModelRecolor(merged, npc->recolors_from[r], npc->recolors_to[r]);

    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);

    scene_id = (int)(UITREE_SCENE_NPC_HEAD_BASE | (unsigned)npc_id);
    ToriDraw_SceneModelAdd(bridge->scene, scene_id, hnd);
    bridge_map_put(bridge->npc_head_map, npc_id, scene_id);
    return scene_id;
}

int
UITreeSceneBridge_EnsurePlayerHead(struct UITreeSceneBridge* bridge)
{
    struct PlayerAppearance app;
    struct ToriDraw_Model* parts[BRIDGE_PLAYER_PART_MODELS_MAX];
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;
    int part_count = 0;
    int p;

    assert(bridge && bridge->scene && bridge->provider);

    if( bridge->player_head_scene_id > 0 )
        return bridge->player_head_scene_id;
    if( ToriDraw_SceneModelHas(bridge->scene, UITREE_SCENE_PLAYER_HEAD_ID) )
    {
        bridge->player_head_scene_id = UITREE_SCENE_PLAYER_HEAD_ID;
        return bridge->player_head_scene_id;
    }

    if( PlayerAppearance_ResolveDefaultMale(bridge->provider, &app) <= 0 )
        return -1;

    /* Compose the head parts from each IdentityKit's heads[] (reference
     * ClientPlayer.getHeadModel: idk head models per appearance slot). */
    for( p = 0; p < PLAYER_APPEARANCE_PARTS; p++ )
    {
        struct ToriRS_Idk* idk;
        int h;
        if( app.kits[p] < 0 )
            continue;
        idk = CacheProvider_IdkGet(bridge->provider, app.kits[p]);
        if( !idk )
            continue;
        for( h = 0; h < 10; h++ )
        {
            struct ToriRS_Model* rs;
            struct ToriDraw_Model* model;
            int mid = idk->heads[h];
            int r;
            /* 0 and -1 both mean "no head part" for an idk (v0 idk_head_model). */
            if( mid <= 0 || !CacheProvider_ModelHas(bridge->provider, mid) )
                continue;
            rs = CacheProvider_ModelGet(bridge->provider, mid);
            if( !rs )
                continue;
            model = ToriDraw_ModelFromToriRS(rs);
            if( !model )
                continue;
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

    merged = ToriDraw_ModelNewMerge(parts, part_count);
    for( p = 0; p < part_count; p++ )
        ToriDraw_ModelFree(parts[p]);
    if( !merged )
        return -1;

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
    ToriDraw_ModelCaptureOriginalVertices(merged);

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);
    ToriDraw_SceneModelAdd(bridge->scene, UITREE_SCENE_PLAYER_HEAD_ID, hnd);
    bridge->player_head_scene_id = UITREE_SCENE_PLAYER_HEAD_ID;
    return bridge->player_head_scene_id;
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

    /* Bank note: the icon is the cert template's model + recolours (reference
     * ObjType.genCert copies template.model/recol into the note), not the base
     * item's. Redirect so EnsureObjIcon rasterizes the template objtype. */
    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 )
    {
        struct ToriRS_Objtype* tmpl = CacheProvider_ObjtypeGet(provider, obj->cert_template);
        if( tmpl )
            obj = tmpl;
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

    bridge_publish_model_textures(bridge, rs_model);

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

int
UITreeSceneBridge_CollectMissingTextures(
    struct UITreeSceneBridge* bridge,
    int* out_ids,
    int max_ids)
{
    unsigned char seen[256] = { 0 };
    struct ToriDraw_TextureState* tex_state;
    int count = 0;
    int slot_count;

    assert(bridge);
    assert(bridge->scene);
    assert(out_ids);

    tex_state = ToriDraw_SceneTexState(bridge->scene);
    if( !tex_state )
        return 0;

    slot_count = ToriDraw_SceneElementSlotCount(bridge->scene);
    for( int element_id = 0; element_id < slot_count; element_id++ )
    {
        struct ToriDraw_SceneElement* el;
        struct ToriDraw_Model* model;

        if( !ToriDraw_SceneElementIsLive(bridge->scene, element_id) )
            continue;
        el = ToriDraw_SceneElementGet(bridge->scene, element_id);
        if( !el || el->model.kind != TORIDRAWMK_MODEL || !el->model.u.model.model )
            continue;
        model = el->model.u.model.model;
        if( !model->face_textures )
            continue;
        for( int f = 0; f < model->face_count && count < max_ids; f++ )
        {
            int texture_id = (int)model->face_textures[f];
            if( texture_id < 0 || texture_id >= 256 )
                continue;
            if( seen[texture_id] || bridge->texture_failed[texture_id] )
                continue;
            seen[texture_id] = 1;
            if( tex_state->texture_map.textures[texture_id] )
                continue;
            out_ids[count++] = texture_id;
        }
    }
    return count;
}

static struct ToriDraw_Texture*
bridge_texture_from_torirs(const struct ToriRS_Texture* rs)
{
    struct ToriDraw_Texture* texture;
    size_t texel_bytes;

    if( !rs || !rs->texels || rs->width <= 0 || rs->height <= 0 )
        return NULL;

    texture = calloc(1, sizeof(*texture));
    if( !texture )
        return NULL;

    texel_bytes = (size_t)rs->width * (size_t)rs->height * sizeof(int);
    texture->texels = malloc(texel_bytes);
    if( !texture->texels )
    {
        free(texture);
        return NULL;
    }
    memcpy(texture->texels, rs->texels, texel_bytes);
    texture->width = rs->width;
    texture->height = rs->height;
    texture->opaque = rs->opaque;
    texture->animation_direction = rs->animation_direction;
    texture->animation_speed = rs->animation_speed;
    return texture;
}

int
UITreeSceneBridge_PublishTextures(
    struct UITreeSceneBridge* bridge,
    const int* ids,
    int id_count)
{
    int published = 0;

    assert(bridge);
    assert(bridge->scene);
    assert(bridge->provider);

    for( int i = 0; i < id_count; i++ )
    {
        int texture_id = ids[i];
        struct ToriRS_Texture* rs;
        struct ToriDraw_Texture* texture;

        if( texture_id < 0 || texture_id >= 256 )
            continue;

        rs = CacheProvider_TextureGet(bridge->provider, texture_id);
        if( !rs )
        {
            bridge->texture_failed[texture_id] = 1;
            continue;
        }

        texture = bridge_texture_from_torirs(rs);
        if( !texture )
        {
            bridge->texture_failed[texture_id] = 1;
            continue;
        }

        ToriDraw_SceneSetTexture(bridge->scene, texture_id, texture);
        published++;
    }
    return published;
}
