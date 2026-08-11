#include "uitree_scene_bridge.h"

#include "engine/cache_provider.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "net/rev/packets/pkt_player_appearance.h" /* the slot vocabulary */
#include "engine/toridraw_font_from_torirs.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/toridraw_sprite_from_torirs.h"
#include "engine/torirs_debug_font_baked.h"
#include "engine/torirs_types.h"
#include "hmap.h"
#include "ui/uitree_debug_overlay.h"
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

/* #region agent log — texture publish trace, defined in app.c. */
int
app_tex_trace_enabled(void);
/* #endregion */

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

        if( texture_id < 0 || texture_id >= 2048 )
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
    bridge->obj_icon_outline_map = bridge_hmap_new_keyed(
        sizeof(int) * 2, sizeof(struct MapEntry_ObjIcon), BRIDGE_OBJ_ICON_MAP_CAP);
    bridge->obj_icon_plain_map = bridge_hmap_new_keyed(
        sizeof(int) * 2, sizeof(struct MapEntry_ObjIcon), BRIDGE_OBJ_ICON_MAP_CAP);
    bridge->obj_icon_border_map = bridge_hmap_new_keyed(
        sizeof(int) * 2, sizeof(struct MapEntry_ObjIcon), BRIDGE_OBJ_ICON_MAP_CAP);
    bridge->npc_head_map = bridge_hmap_new(sizeof(struct MapEntry_BridgeId), BRIDGE_MODEL_MAP_CAP);
    bridge->obj_model_map = bridge_hmap_new(sizeof(struct MapEntry_BridgeId), BRIDGE_MODEL_MAP_CAP);
    for( int slot = 0; slot < STATIC_SPRITE_COUNT; slot++ )
        bridge->static_sprite_scene[slot] = -1;
    bridge->player_scene_id = -1;
    bridge->local_player_scene_id = -1;
    bridge->player_head_scene_id = -1;
    assert(bridge->sprite_map && bridge->model_map && bridge->obj_icon_map &&
           bridge->obj_icon_outline_map && bridge->obj_icon_plain_map &&
           bridge->obj_icon_border_map && bridge->npc_head_map && bridge->obj_model_map);
}

void
UITreeSceneBridge_Free(struct UITreeSceneBridge* bridge)
{
    if( !bridge )
        return;
    bridge_hmap_free(bridge->sprite_map);
    bridge_hmap_free(bridge->model_map);
    bridge_hmap_free(bridge->obj_icon_map);
    bridge_hmap_free(bridge->obj_icon_outline_map);
    bridge_hmap_free(bridge->obj_icon_plain_map);
    bridge_hmap_free(bridge->obj_icon_border_map);
    bridge_hmap_free(bridge->npc_head_map);
    bridge_hmap_free(bridge->obj_model_map);
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
UITreeSceneBridge_EnsureDebugFont(
    struct UITreeSceneBridge* bridge,
    int font_slot)
{
    struct ToriDraw_Font const* baked;
    struct ToriDraw_Font* copy;
    int scene_id;

    assert(bridge);
    assert(bridge->scene);

    switch( font_slot )
    {
    case TORIDBG_FONT_SMALL:
        baked = ToriDbgFont_Small();
        scene_id = UITREE_SCENE_DEBUG_FONT_SMALL_ID;
        break;
    case TORIDBG_FONT_MENU:
        baked = ToriDbgFont_Menu();
        scene_id = UITREE_SCENE_DEBUG_FONT_MENU_ID;
        break;
    default:
        assert(0 && "unknown debug font slot");
        return -1;
    }

    if( ToriDraw_SceneFontHas(bridge->scene, scene_id) )
        return scene_id;

    /* Deep copy: the scene owns every font it holds and frees it at shutdown,
     * but `baked` is static and its glyph_alpha rows point into a const blob
     * (see torirs_debug_font_baked.h). Handing the scene the baked struct
     * would free .rdata. */
    copy = malloc(sizeof(*copy));
    if( !copy )
        return -1;
    memcpy(copy, baked, sizeof(*copy));
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        size_t bytes;
        copy->glyph_alpha[i] = NULL;
        if( !baked->glyph_alpha[i] )
            continue;
        bytes = (size_t)baked->glyph_width[i] * (size_t)baked->glyph_height[i];
        if( bytes == 0 )
            continue;
        copy->glyph_alpha[i] = malloc(bytes);
        if( !copy->glyph_alpha[i] )
        {
            ToriDraw_FontFree(copy);
            return -1;
        }
        memcpy(copy->glyph_alpha[i], baked->glyph_alpha[i], bytes);
    }

    ToriDraw_SceneFontAdd(bridge->scene, scene_id, copy);
    return scene_id;
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

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(bridge->provider, model);
    /* Interface archive models use the same asynchronously-published texture
     * map as world models.  Register their surviving SD material ids before
     * lighting so App's normal texture pump loads them; otherwise every
     * textured face is skipped and only untextured extremities remain. */
    ToriDraw_ModelNoteTextureWants(model);
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_LightModelScene(hnd, 0, 0);
    /* Rest-pose snapshot enables IF/CC_SETMODELANIM sequence playback on widgets. */
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_SceneModelAdd(bridge->scene, cache_model_id, hnd);
    bridge_map_put(bridge->model_map, cache_model_id, cache_model_id);
    return cache_model_id;
}

/* The design's seven body parts as a PLAYER_INFO appearance: each is an
 * identity-kit slot, and the compositor merges in slot order — which is design
 * part order, matching the reference's combineForAnim(models, count). */
static void
bridge_design_slots(
    int const kits[PLAYER_APPEARANCE_PARTS],
    int slots[APPEARANCE_SLOT_COUNT])
{
    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
        slots[i] = 0;
    for( int p = 0; p < PLAYER_APPEARANCE_PARTS; p++ )
        slots[p] = (kits && kits[p] >= 0) ? Appearance_PackKit(kits[p]) : 0;
}

int
UITreeSceneBridge_CollectPlayerDesignModelIds(
    struct UITreeSceneBridge* bridge,
    int const kits[PLAYER_APPEARANCE_PARTS],
    int gender,
    int* out_ids,
    int cap)
{
    int slots[12];
    assert(bridge && bridge->provider);
    bridge_design_slots(kits, slots);
    return PlayerModel_CollectAppearanceModelIds(
        bridge->provider, slots, gender, out_ids, cap);
}

int
UITreeSceneBridge_BuildPlayerDesignModel(
    struct UITreeSceneBridge* bridge,
    int const kits[PLAYER_APPEARANCE_PARTS],
    int const colours[PLAYER_APPEARANCE_COLORS],
    int gender)
{
    int slots[12];
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;

    assert(bridge && bridge->scene && bridge->provider);

    bridge_design_slots(kits, slots);
    merged = PlayerModel_BuildFromAppearance(bridge->provider, slots, colours, gender);
    if( !merged )
        return -1;

    /* PlayerModel_BuildFromAppearance already lights with the actor regime
     * (64, 850, -30, -50, -30). Re-lighting is safe — ApplyLighting always
     * reads the unlit face_colors — but unnecessary now that the builder
     * matches the design-preview constants. */
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;

    /* SceneModelAdd overwrites the slot without freeing what was there, so
     * drop the superseded composite (a design edit rebuilds every change). */
    {
        struct ToriDraw_ModelHandle old =
            ToriDraw_SceneModelGet(bridge->scene, UITREE_SCENE_PLAYER_MODEL_ID);
        if( old.kind == TORIDRAWMK_MODEL && old.u.model.model &&
            old.u.model.model != merged )
            ToriDraw_ModelFree(old.u.model.model);
    }

    ToriDraw_SceneModelAdd(bridge->scene, UITREE_SCENE_PLAYER_MODEL_ID, hnd);
    bridge->player_scene_id = UITREE_SCENE_PLAYER_MODEL_ID;
    return bridge->player_scene_id;
}

int
UITreeSceneBridge_EnsurePlayerModel(struct UITreeSceneBridge* bridge)
{
    struct PlayerAppearance app;

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

    return UITreeSceneBridge_BuildPlayerDesignModel(bridge, app.kits, app.colors, app.gender);
}

int
UITreeSceneBridge_BuildLocalPlayerModel(
    struct UITreeSceneBridge* bridge,
    int const slots[12],
    int const colours[5],
    int gender)
{
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;

    assert(bridge && bridge->scene && bridge->provider && slots);

    /* Identical compositor to the world entity's own model (worn objs merge in
     * slot order over the identity kits), so the figure on the widget and the
     * figure in the viewport cannot disagree. Both light with the actor regime. */
    merged = PlayerModel_BuildFromAppearance(bridge->provider, slots, colours, gender);
    if( !merged )
        return -1;

    /* SceneModelAdd overwrites the slot without freeing what was there, so drop
     * the superseded composite — this rebuilds on every appearance change. */
    {
        struct ToriDraw_ModelHandle old =
            ToriDraw_SceneModelGet(bridge->scene, UITREE_SCENE_LOCAL_PLAYER_MODEL_ID);
        if( old.kind == TORIDRAWMK_MODEL && old.u.model.model && old.u.model.model != merged )
            ToriDraw_ModelFree(old.u.model.model);
    }

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_SceneModelAdd(bridge->scene, UITREE_SCENE_LOCAL_PLAYER_MODEL_ID, hnd);
    bridge->local_player_scene_id = UITREE_SCENE_LOCAL_PLAYER_MODEL_ID;
    return bridge->local_player_scene_id;
}

int
UITreeSceneBridge_BuildInterfacePlayerModel(
    struct UITreeSceneBridge* bridge,
    int scene_id,
    int const slots[12],
    int const colours[5],
    int gender)
{
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_ModelHandle old;

    assert(bridge && bridge->scene && bridge->provider && slots);
    assert(scene_id >= UITREE_SCENE_IF_PLAYER_MODEL_BASE);
    merged = PlayerModel_BuildFromAppearance(bridge->provider, slots, colours, gender);
    if( !merged )
        return -1;

    old = ToriDraw_SceneModelGet(bridge->scene, scene_id);
    if( old.kind == TORIDRAWMK_MODEL && old.u.model.model && old.u.model.model != merged )
        ToriDraw_ModelFree(old.u.model.model);

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_SceneModelAdd(bridge->scene, scene_id, hnd);
    return scene_id;
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
    /* OSRS deob getHead (class195.method3617): recol then retex before light. */
    for( int r = 0; r < npc->retexture_count; r++ )
        ToriDraw_ModelRetexture(merged, npc->retextures_from[r], npc->retextures_to[r]);

    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(bridge->provider, merged);
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    /* Client-TS IfType.getTempModel lights every IF model (incl. NPC heads)
     * with the scene regime. xrsps ChatheadFactory uses actor + type offsets. */
    if( bridge->npc_light_uses_type_ambient_contrast )
        ToriDraw_LightModelActor(hnd, npc->contrast, npc->ambient);
    else
        ToriDraw_LightModelScene(hnd, 0, 0);

    scene_id = (int)(UITREE_SCENE_NPC_HEAD_BASE | (unsigned)npc_id);
    ToriDraw_SceneModelAdd(bridge->scene, scene_id, hnd);
    bridge_map_put(bridge->npc_head_map, npc_id, scene_id);
    return scene_id;
}

int
UITreeSceneBridge_EnsurePlayerHead(
    struct UITreeSceneBridge* bridge,
    int const slots[12],
    int const colors[5],
    int gender)
{
    struct ToriDraw_Model* merged;
    struct ToriDraw_ModelHandle hnd;

    assert(bridge && bridge->scene && bridge->provider);

    if( bridge->player_head_scene_id > 0 )
        return bridge->player_head_scene_id;
    if( ToriDraw_SceneModelHas(bridge->scene, UITREE_SCENE_PLAYER_HEAD_ID) )
    {
        bridge->player_head_scene_id = UITREE_SCENE_PLAYER_HEAD_ID;
        return bridge->player_head_scene_id;
    }

    if( !slots )
        return -1;

    /* Real appearance head (reference ClientPlayer.getHeadModel): the idk head
     * models of the head-bearing slots, design-recoloured. Returns NULL while
     * the head models are not yet resident — the caller retries next frame. */
    merged = PlayerHeadModel_BuildFromAppearance(bridge->provider, slots, colors, gender);
    if( !merged )
        return -1;

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    /* Client-TS getTempModel lights IF heads with the scene regime; xrsps
     * PlayerChatheadFactory uses absolute ambient 128 + actor dir/atten.
     * player_head_light_ambient is that absolute value when non-zero. */
    if( bridge->player_head_light_ambient != 0 )
    {
        struct ToriDraw_LightProfile const* p = ToriDraw_LightActorProfile();
        ToriDraw_LightModelParams(
            hnd,
            bridge->player_head_light_ambient,
            p->attenuation,
            p->src_x,
            p->src_y,
            p->src_z);
    }
    else
    {
        ToriDraw_LightModelScene(hnd, 0, 0);
    }
    ToriDraw_SceneModelAdd(bridge->scene, UITREE_SCENE_PLAYER_HEAD_ID, hnd);
    bridge->player_head_scene_id = UITREE_SCENE_PLAYER_HEAD_ID;
    return bridge->player_head_scene_id;
}

/* Count-variant resolution only (reference getSprite countobj loop). Returns
 * the objtype whose model should render for this (obj, count). Does NOT apply
 * the bank-note redirect — EnsureObjIcon composites the note itself so it can
 * draw the base item on top of the template paper. */
static struct ToriRS_Objtype*
bridge_resolve_count_variant(
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

/* Which post-process border the rasterized obj icon gets. NONE matches the
 * reference cert base sub-icon (outlineRgb == -1: no shadow); SHADOW is the
 * normal inventory icon (outlineRgb == 0: value-1 edge + drop shadow); WHITE is
 * the "Use"-selected highlight (outlineRgb > 0: value-1 edge + white ring, and
 * a 1.04x zoom so the ring stays inside the tile); BLACK is Soft3D's
 * SpriteNewGraphicOutline(1) on a plain raster (cc_setoutline(1), no shadow). */
enum BridgeObjIconOutline
{
    BRIDGE_ICON_OUTLINE_NONE = 0,
    BRIDGE_ICON_OUTLINE_SHADOW,
    BRIDGE_ICON_OUTLINE_WHITE,
    BRIDGE_ICON_OUTLINE_BLACK,
};

/* Rasterize one objtype's inventory model into a 36x32 obj-icon sprite at the
 * given zoom (reference getSprite model.objRender). Returns NULL when the
 * model is not resident; ownership of the sprite passes to the caller. */
static struct ToriDraw_Sprite*
bridge_rasterize_obj_icon(
    struct UITreeSceneBridge* bridge,
    struct ToriRS_Objtype* obj,
    int zoom,
    enum BridgeObjIconOutline outline)
{
    struct ToriRS_Model* rs_model;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_Sprite* sprite;
    int render_zoom;
    int i;

    if( !obj || obj->inventory_model_id <= 0 )
        return NULL;
    if( !CacheProvider_ModelHas(bridge->provider, obj->inventory_model_id) )
        return NULL;

    rs_model = CacheProvider_ModelGet(bridge->provider, obj->inventory_model_id);
    assert(rs_model != NULL && "ModelGet failed");

    bridge_publish_model_textures(bridge, rs_model);

    model = ToriDraw_ModelFromToriRS(rs_model);
    assert(model != NULL && "ModelFromToriRS failed");

    /* ObjModelLoader resizes before recolouring; 128 == 1.0. */
    if( obj->resize_x != 128 || obj->resize_y != 128 || obj->resize_z != 128 )
        ToriDraw_ModelScale(model, obj->resize_x, obj->resize_z, obj->resize_y);

    for( i = 0; i < obj->recolor_count; i++ )
        ToriDraw_ModelRecolor(model, obj->recolors_from[i], obj->recolors_to[i]);

    ToriDraw_ModelSetBoundsCylinder(model);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(bridge->provider, model);
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_LightModelScene(hnd, obj->contrast, obj->ambient);

    render_zoom = zoom > 0 ? zoom : 2000;
    /* Reference: outlineRgb > 0 renders at (zoom * 1.04)|0 so the white ring has
     * room. A larger zoom pushes the camera back → a slightly smaller icon. */
    if( outline == BRIDGE_ICON_OUTLINE_WHITE )
        render_zoom = render_zoom * 104 / 100;

    /* Reference icon raster is 36x32 with the 3D projection center at (16,16)
     * (ItemIconRenderer.OSRS_SPRITE_W/H); rasterizing 32x32 shifts every icon
     * ~2px and clips the right edge. The built-in postprocess bakes the SHADOW
     * variant; WHITE is applied below on the raw raster (the reference runs the
     * white pass in place of the shadow, not on top of it). */
    sprite = ToriDraw_SpriteNewFromObjIconRaster(
        bridge->scene,
        hnd,
        render_zoom,
        obj->xan2d,
        obj->yan2d,
        obj->zan2d,
        obj->offset_x2d,
        obj->offset_y2d,
        36,
        32,
        outline == BRIDGE_ICON_OUTLINE_SHADOW);
    ToriDraw_ModelFree(model);

    if( sprite && outline == BRIDGE_ICON_OUTLINE_WHITE )
        ToriDraw_SpritePostprocessObjIconOutlineColor(
            sprite->pixels_argb, sprite->width, sprite->height, 0xFFFFFFFFu);

    /* Match Soft3D's draw-time outline=1: in-place black neighbour dilate
     * (deob method9420). Bake once so dense grids skip per-frame outline. */
    if( sprite && outline == BRIDGE_ICON_OUTLINE_BLACK )
    {
        int ow = 0;
        int oh = 0;
        uint32_t* outlined = ToriDraw_SpriteNewGraphicOutline(
            sprite->pixels_argb, sprite->width, sprite->height, 1, &ow, &oh);
        ToriDraw_SpriteFree(sprite);
        sprite = NULL;
        if( !outlined )
            return NULL;
        sprite = ToriDraw_SpriteNewFromArgbOwned(outlined, ow, oh);
        if( !sprite )
        {
            free(outlined);
            return NULL;
        }
    }

    return sprite;
}

/* Overlay src's opaque pixels onto dst in place (reference Pix32.plotSprite:
 * non-zero source pixels are copied, zero/transparent skipped). Both sprites
 * must share dimensions or nothing is composited. */
static void
bridge_composite_over(
    struct ToriDraw_Sprite* dst,
    struct ToriDraw_Sprite const* src)
{
    int pixel_count;

    if( !dst || !src || !dst->pixels_argb || !src->pixels_argb )
        return;
    if( dst->width != src->width || dst->height != src->height )
        return;

    pixel_count = dst->width * dst->height;
    for( int idx = 0; idx < pixel_count; idx++ )
    {
        if( src->pixels_argb[idx] & 0x00FFFFFFu )
            dst->pixels_argb[idx] = src->pixels_argb[idx];
    }
}

/* Shared body for the plain (SHADOW) and "Use"-selected (WHITE) obj icons: the
 * two differ only in the border passed to the raster and which cache map holds
 * the result. */
static int
bridge_ensure_obj_icon(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count,
    enum BridgeObjIconOutline outline,
    struct HMap* map)
{
    struct MapEntry_ObjIcon* entry;
    int key[2];
    struct ToriRS_Objtype* obj;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** frames;
    int scene_id;

    assert(bridge);
    assert(bridge->scene);
    assert(bridge->provider);
    assert(map);

    if( obj_id <= 0 )
        return -1;
    if( count < 0 )
        count = 0;

    key[0] = obj_id;
    key[1] = count;
    entry = (struct MapEntry_ObjIcon*)hmap_search(map, key, HMAP_FIND);
    if( entry )
        return entry->scene_id;

    obj = bridge_resolve_count_variant(bridge->provider, obj_id, count);
    if( !obj )
        return -1;

    /*
     * A bank placeholder draws as the item it stands for, full stop.
     *
     * Unlike a note it has no template of its own to composite onto — the
     * reference's item-sprite builder takes the `placeholderId` branch, renders
     * that item's sprite and blits it at 0,0 with nothing underneath — so the
     * whole of the difference is resolving to the linked objtype here and
     * letting the ordinary path below run. The faded look is the interface's:
     * `bankmain_drawitem` sets `cc_settrans(120)` on a placeholder cell.
     */
    if( obj->inventory_model_id <= 0 && obj->placeholder_template >= 0 &&
        obj->placeholder_link > 0 )
    {
        struct ToriRS_Objtype* linked =
            CacheProvider_ObjtypeGet(bridge->provider, obj->placeholder_link);

        if( !linked )
            return -1;
        obj = linked;
    }

    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 )
    {
        /* Bank note (reference ObjType.genCert + getSprite cert branch): the
         * note's own model is the cert-template paper; the base item (cert_link)
         * icon is rendered 1.5x and composited on top. The reference returns
         * null (draws nothing) when the base item cannot render, so the note is
         * withheld rather than shown as blank paper until both are resident. The
         * reference applies the outline (white or shadow) to the template paper
         * only; the base item sub-icon always renders with no shadow. */
        struct ToriRS_Objtype* tmpl =
            CacheProvider_ObjtypeGet(bridge->provider, obj->cert_template);
        struct ToriRS_Objtype* base =
            obj->cert_link > 0
                ? CacheProvider_ObjtypeGet(bridge->provider, obj->cert_link)
                : NULL;
        struct ToriDraw_Sprite* base_sprite;

        if( !tmpl )
            return -1;
        base_sprite = base ? bridge_rasterize_obj_icon(
                                 bridge,
                                 base,
                                 (base->zoom2d > 0 ? base->zoom2d : 2000) * 3 / 2,
                                 BRIDGE_ICON_OUTLINE_NONE)
                           : NULL;
        if( !base_sprite )
            return -1;

        sprite = bridge_rasterize_obj_icon(
            bridge, tmpl, tmpl->zoom2d > 0 ? tmpl->zoom2d : 2000, outline);
        if( !sprite )
        {
            ToriDraw_SpriteFree(base_sprite);
            return -1;
        }
        bridge_composite_over(sprite, base_sprite);
        ToriDraw_SpriteFree(base_sprite);
    }
    else
    {
        if( obj->inventory_model_id <= 0 )
            return -1;
        sprite = bridge_rasterize_obj_icon(
            bridge, obj, obj->zoom2d > 0 ? obj->zoom2d : 2000, outline);
        if( !sprite )
            return -1;
    }

    frames = malloc(sizeof(*frames));
    if( !frames )
    {
        ToriDraw_SpriteFree(sprite);
        return -1;
    }
    frames[0] = sprite;

    scene_id = bridge->next_scene_id++;
    ToriDraw_SceneSpriteAdd(bridge->scene, scene_id, frames, 1);

    entry = (struct MapEntry_ObjIcon*)hmap_search(map, key, HMAP_INSERT);
    assert(entry);
    entry->obj_id = obj_id;
    entry->count = count;
    entry->scene_id = scene_id;
    return scene_id;
}

int
UITreeSceneBridge_EnsureObjModel(
    struct UITreeSceneBridge* bridge,
    int obj_id)
{
    struct ToriRS_Objtype* obj;
    struct ToriRS_Model* rs_model;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;
    int scene_id;

    assert(bridge && bridge->scene && bridge->provider);
    if( obj_id <= 0 )
        return -1;

    scene_id = bridge_map_get(bridge->obj_model_map, obj_id);
    if( scene_id > 0 )
        return scene_id;

    obj = CacheProvider_ObjtypeGet(bridge->provider, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    if( !CacheProvider_ModelHas(bridge->provider, obj->inventory_model_id) )
        return -1;
    rs_model = CacheProvider_ModelGet(bridge->provider, obj->inventory_model_id);
    if( !rs_model )
        return -1;

    bridge_publish_model_textures(bridge, rs_model);

    model = ToriDraw_ModelFromToriRS(rs_model);
    if( !model )
        return -1;

    /* ObjModelLoader resizes before recolouring; 128 == 1.0. */
    if( obj->resize_x != 128 || obj->resize_y != 128 || obj->resize_z != 128 )
        ToriDraw_ModelScale(model, obj->resize_x, obj->resize_z, obj->resize_y);

    for( int i = 0; i < obj->recolor_count; i++ )
        ToriDraw_ModelRecolor(model, obj->recolors_from[i], obj->recolors_to[i]);

    ToriDraw_ModelSetBoundsCylinder(model);
    ToriDraw_ModelCaptureOriginalVertices(model);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(bridge->provider, model);
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_LightModelScene(hnd, obj->contrast, obj->ambient);

    scene_id = (int)(UITREE_SCENE_OBJ_MODEL_BASE | (unsigned)obj_id);
    ToriDraw_SceneModelAdd(bridge->scene, scene_id, hnd);
    bridge_map_put(bridge->obj_model_map, obj_id, scene_id);
    return scene_id;
}

int
UITreeSceneBridge_EnsureObjIcon(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count)
{
    return bridge_ensure_obj_icon(
        bridge, obj_id, count, BRIDGE_ICON_OUTLINE_SHADOW, bridge->obj_icon_map);
}

int
UITreeSceneBridge_EnsureObjIconSelected(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count)
{
    return bridge_ensure_obj_icon(
        bridge, obj_id, count, BRIDGE_ICON_OUTLINE_WHITE, bridge->obj_icon_outline_map);
}

int
UITreeSceneBridge_EnsureObjIconPlain(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count)
{
    return bridge_ensure_obj_icon(
        bridge, obj_id, count, BRIDGE_ICON_OUTLINE_NONE, bridge->obj_icon_plain_map);
}

int
UITreeSceneBridge_EnsureObjIconBordered(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count)
{
    return bridge_ensure_obj_icon(
        bridge, obj_id, count, BRIDGE_ICON_OUTLINE_BLACK, bridge->obj_icon_border_map);
}

int
UITreeSceneBridge_TextureResident(
    struct UITreeSceneBridge const* bridge,
    int texture_id)
{
    struct ToriDraw_TextureState* tex_state;

    assert(bridge);
    if( texture_id < 0 || texture_id >= 2048 || !bridge->scene )
        return 0;
    tex_state = ToriDraw_SceneTexState(bridge->scene);
    return tex_state && tex_state->texture_map.textures[texture_id] ? 1 : 0;
}

int
UITreeSceneBridge_CollectMissingTextures(
    struct UITreeSceneBridge* bridge,
    int* out_ids,
    int max_ids)
{
    unsigned char seen[2048] = { 0 };
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
            if( texture_id < 0 || texture_id >= 2048 )
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

        if( texture_id < 0 || texture_id >= 2048 )
        {
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: publish id=%d -> rejected (out of range)\n", texture_id);
            continue;
        }

        rs = CacheProvider_TextureGet(bridge->provider, texture_id);
        if( !rs )
        {
            bridge->texture_failed[texture_id] = 1;
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: publish id=%d -> FAILED (no provider entry)\n", texture_id);
            continue;
        }

        texture = bridge_texture_from_torirs(rs);
        if( !texture )
        {
            bridge->texture_failed[texture_id] = 1;
            if( app_tex_trace_enabled() )
                fprintf(
                    stderr,
                    "tex_trace: publish id=%d -> FAILED (convert: texels=%p %dx%d)\n",
                    texture_id,
                    (void*)rs->texels,
                    rs->width,
                    rs->height);
            continue;
        }

        ToriDraw_SceneSetTexture(bridge->scene, texture_id, texture);
        if( app_tex_trace_enabled() )
            fprintf(
                stderr,
                "tex_trace: publish id=%d -> %s (%dx%d opaque=%d)\n",
                texture_id,
                UITreeSceneBridge_TextureResident(bridge, texture_id) ? "resident"
                                                                      : "SET BUT NOT RESIDENT",
                texture->width,
                texture->height,
                texture->opaque);
        published++;
    }
    return published;
}
