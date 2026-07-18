#ifndef UITREE_SCENE_BRIDGE_H
#define UITREE_SCENE_BRIDGE_H

struct ToriDraw_Scene;
struct CacheProvider;
struct HMap;

/**
 * Uploads CacheProvider ToriRS assets into a ToriDraw_Scene and returns scene
 * element ids for UITree nodes (integers only on the tree).
 */
struct UITreeSceneBridge
{
    struct ToriDraw_Scene* scene;
    struct CacheProvider* provider;
    int next_scene_id;

    /** cache_graphic_id → scene_id */
    struct HMap* sprite_map;
    /** cache_model_id → scene_id (usually same as cache id) */
    struct HMap* model_map;
    /** (obj_id, count) → scene_id for rasterized inventory icons */
    struct HMap* obj_icon_map;

    /** IF1 scrollbar arrow pack (atlas 0=up/left, 1=down/right); -1 if unloaded. */
    int scrollbar_scene_id;
};

void
UITreeSceneBridge_Init(
    struct UITreeSceneBridge* bridge,
    struct ToriDraw_Scene* scene,
    struct CacheProvider* provider);

void
UITreeSceneBridge_Free(struct UITreeSceneBridge* bridge);

/** Ensure sprite in scene. Returns scene_id or -1. */
int
UITreeSceneBridge_EnsureSprite(
    struct UITreeSceneBridge* bridge,
    int cache_graphic_id);

/**
 * Bind IF1 scrollbar chrome from an already-loaded cache sprite archive
 * (frames 0/1 = arrows). Returns scene_id or -1.
 */
int
UITreeSceneBridge_EnsureScrollbar(
    struct UITreeSceneBridge* bridge,
    int cache_graphic_id);

/** Scene id of scrollbar arrow pack, or -1. */
int
UITreeSceneBridge_ScrollbarSceneId(struct UITreeSceneBridge const* bridge);

/** Ensure font in scene keyed by cache_font_id. Returns font scene id or -1. */
int
UITreeSceneBridge_EnsureFont(
    struct UITreeSceneBridge* bridge,
    int cache_font_id);

/** Ensure model in scene. Returns scene model id or -1. */
int
UITreeSceneBridge_EnsureModel(
    struct UITreeSceneBridge* bridge,
    int cache_model_id);

/**
 * Rasterize an inventory/obj icon into the scene (32x32).
 * Requires objtype + inventory model already in CacheProvider.
 * Returns scene sprite id or -1.
 */
int
UITreeSceneBridge_EnsureObjIcon(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count);

#endif
