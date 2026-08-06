#ifndef UITREE_SCENE_BRIDGE_H
#define UITREE_SCENE_BRIDGE_H

#include "engine/static_sprites.h"

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
    /** (obj_id, count) → scene_id for the white-outlined "Use"-selected variant */
    struct HMap* obj_icon_outline_map;
    /** (obj_id, count) → scene_id for the plain (no baked shadow) variant —
     * reference outlineRgb == -1; used when a component applies runtime
     * cc_setoutline / cc_setgraphicshadow on top of a SETOBJECT icon. */
    struct HMap* obj_icon_plain_map;
    /** (obj_id, count) → scene_id for cc_setoutline(1) without graphic_shadow:
     * plain raster + black border baked once (Soft3D SpriteNewGraphicOutline
     * equivalent). Collection-log / bank cell grids use this so Soft3D does
     * not re-outline every unique icon every frame. */
    struct HMap* obj_icon_border_map;

    /**
     * Client-hardcoded sprites (compass, hitmarks, cross, scrollbar arrows, …)
     * keyed by StaticSpriteSlot; -1 until loaded. These have no owning tree
     * node, so emit/draw fetch them through the host.
     */
    int static_sprite_scene[STATIC_SPRITE_COUNT];

    /** Composited default player avatar model; -1 until first built. */
    int player_scene_id;

    /** Composited LIVE local-player model (real PLAYER_INFO appearance, worn
     * equipment included); -1 until first built. Distinct from player_scene_id,
     * which is the offline default avatar the design preview poses. */
    int local_player_scene_id;

    /** Composited local-player chathead model; -1 until first built. */
    int player_head_scene_id;

    /** npc_id → composited chathead scene model id (reference NpcType.getHead). */
    struct HMap* npc_head_map;

    /** obj_id → lit interface model scene id (reference IfType.getModel type 4,
     * set by IF_SETOBJECT — e.g. the combat-tab weapon model). */
    struct HMap* obj_model_map;

    /** Texture ids whose load already failed; never re-requested. */
    unsigned char texture_failed[2048];

    /** Effective lighting behaviour after era + `[render:light]` merge.
     *  Copied from App at init so the bridge can light NPC/player heads
     *  without an App* in hand. */
    int npc_light_uses_type_ambient_contrast;
    int player_head_light_ambient;
};

/* Reserved scene model id for the composited player avatar (out of cache range). */
#define UITREE_SCENE_PLAYER_MODEL_ID 0x40000000

/* Reserved scene sprite id for the baked world map the minimap blits from
 * (out of the bridge's next_scene_id range, which counts up from 1). */
#define UITREE_SCENE_WORLD_MAP_SPRITE_ID 0x40000001

/* Reserved scene sprite id for the world map's flash marker. Synthesised, not
 * decoded from the cache — see app_worldmap_flash_marker_scene. */
#define UITREE_SCENE_WORLD_MAP_FLASH_SPRITE_ID 0x40000004

/* Reserved scene sprite id for the world map overview pane (clientCode 1401):
 * the current area's compositetexture PNG, replaced on area switch. */
#define UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID 0x40000005

/* Reserved scene model id for the composited local-player chathead. */
#define UITREE_SCENE_PLAYER_HEAD_ID 0x40000002

/* Reserved scene model id for the composited LIVE local-player body model
 * (clientCode 328 widgets). Separate from UITREE_SCENE_PLAYER_MODEL_ID so a
 * rebuild from the real appearance never clobbers the design preview's. */
#define UITREE_SCENE_LOCAL_PLAYER_MODEL_ID 0x40000003

/* Base of the reserved scene-model id range for composited NPC chatheads
 * (id = base | npc_id); distinct from cache model ids and the avatars above. */
#define UITREE_SCENE_NPC_HEAD_BASE 0x50000000

/** Obj interface models (IF_SETOBJECT: lit + recoloured inventory model bound
 * to a MODEL widget, e.g. the combat-tab weapon): scene ids are base | obj_id. */
#define UITREE_SCENE_OBJ_MODEL_BASE 0x58000000

/* Reserved scene font ids for the two baked debug-overlay faces. Scene font ids
 * are cache font ids everywhere else (see EnsureFont), so these sit out of that
 * range alongside the reserved model/sprite ids above. */
#define UITREE_SCENE_DEBUG_FONT_SMALL_ID 0x40000006
#define UITREE_SCENE_DEBUG_FONT_MENU_ID 0x40000007

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

/** Reverse lookup: cache graphic id for a sprite scene_id, or -1 (debug dumps). */
int
UITreeSceneBridge_SpriteCacheIdForScene(
    struct UITreeSceneBridge const* bridge,
    int scene_id);

/**
 * Bind a client-hardcoded sprite from an already-loaded cache archive into its
 * slot (memoized). Returns scene_id or -1.
 */
int
UITreeSceneBridge_EnsureStaticSprite(
    struct UITreeSceneBridge* bridge,
    enum StaticSpriteSlot slot,
    int cache_graphic_id);

/** Scene id bound to a static sprite slot, or -1. */
int
UITreeSceneBridge_StaticSpriteSceneId(
    struct UITreeSceneBridge const* bridge,
    enum StaticSpriteSlot slot);

/** Scrollbar chrome (frames 0/1 = arrows) — STATIC_SPRITE_SCROLLBAR shorthand. */
int
UITreeSceneBridge_EnsureScrollbar(
    struct UITreeSceneBridge* bridge,
    int cache_graphic_id);

int
UITreeSceneBridge_ScrollbarSceneId(struct UITreeSceneBridge const* bridge);

/** Ensure font in scene keyed by cache_font_id. Returns font scene id or -1. */
int
UITreeSceneBridge_EnsureFont(
    struct UITreeSceneBridge* bridge,
    int cache_font_id);

/**
 * Register one of the two baked debug faces (enum ToriDbgFontSlot) in the scene
 * and return its reserved scene font id.
 *
 * Needs no cache and no provider: the faces are compiled in. They are also
 * `static`, and ToriDraw_SceneGraphShutdown frees every font it holds, so this
 * registers a deep copy (~4 KB of glyph alpha per face) rather than the baked
 * struct itself.
 */
int
UITreeSceneBridge_EnsureDebugFont(
    struct UITreeSceneBridge* bridge,
    int font_slot);

/** Ensure model in scene. Returns scene model id or -1. */
int
UITreeSceneBridge_EnsureModel(
    struct UITreeSceneBridge* bridge,
    int cache_model_id);

/**
 * Composite the default player avatar (IdentityKit body parts merged + recolored)
 * and register it in the scene. Requires the appearance idks + models already in
 * the provider (see CreateTask_PlayerAppearanceLoad). Built once, then cached.
 * Returns the scene model id or -1 if the kits/models are unavailable.
 */
int
UITreeSceneBridge_EnsurePlayerModel(struct UITreeSceneBridge* bridge);

/**
 * (Re)build the player-design composite from an explicit kit/colour/gender set
 * and register it under the same scene id as EnsurePlayerModel, replacing (and
 * freeing) whatever composite was there. This is the design screen's rebuild
 * path — the reference rebuilds on every idkDesignRedraw.
 *
 * `kits` is one IdentityKit id per design part (-1 = none) and `colours` is one
 * palette index per design colour. Models that are not resident are skipped, so
 * gate on UITreeSceneBridge_CollectPlayerDesignModelIds first (reference
 * IdkType.checkModel) or the composite comes out missing limbs.
 * Returns the scene model id, or -1 when nothing composited.
 */
int
UITreeSceneBridge_BuildPlayerDesignModel(
    struct UITreeSceneBridge* bridge,
    int const kits[7],
    int const colours[5],
    int gender);

/**
 * (Re)build the LIVE local-player model from their real PLAYER_INFO appearance
 * — the same slots/colours/gender the world entity is built from, so worn
 * equipment is on it — and register it under
 * UITREE_SCENE_LOCAL_PLAYER_MODEL_ID, freeing whatever composite was there.
 *
 * This is what a clientCode 328 model widget (the equipment-stats figure)
 * draws. Unlike the design preview it is not cached: the caller rebuilds it
 * whenever the appearance changes, which is what makes it live.
 *
 * `slots` is the 12-entry appearance array (0x100+kit / 0x200+obj, 0 = empty).
 * Non-resident models are skipped, so a build attempted before the wear models
 * have loaded comes out incomplete — retry until it looks right, or gate on the
 * same loads the world entity does.
 * Returns the scene model id, or -1 when nothing composited.
 */
int
UITreeSceneBridge_BuildLocalPlayerModel(
    struct UITreeSceneBridge* bridge,
    int const slots[12],
    int const colours[5],
    int gender);

/**
 * List the cache model ids a design kit set references, so the caller can await
 * the loads before rebuilding. Returns the count written (capped at cap).
 */
int
UITreeSceneBridge_CollectPlayerDesignModelIds(
    struct UITreeSceneBridge* bridge,
    int const kits[7],
    int gender,
    int* out_ids,
    int cap);

/**
 * Composite an NPC's chathead (NpcType.heads merged + npc recolours) and register
 * it in the scene, keyed/memoized by npc_id. Requires the npctype + its head
 * models already in the provider. Returns the scene model id or -1.
 * Reference: NpcType.getHead (Client-TS) / npc_head_model (v0 entity_scenebuild).
 */
int
UITreeSceneBridge_EnsureNpcHead(
    struct UITreeSceneBridge* bridge,
    int npc_id);

/**
 * Composite the local player's chathead from their real PLAYER_INFO appearance
 * (identity-kit head parts merged + design-recoloured) and register it in the
 * scene. Requires the appearance's idk head models already resident. Cached
 * after the first successful build. Returns the scene model id or -1 (nothing
 * resolved yet — the caller retries). Reference: ClientPlayer.getHeadModel.
 */
int
UITreeSceneBridge_EnsurePlayerHead(
    struct UITreeSceneBridge* bridge,
    int const slots[12],
    int const colors[5],
    int gender);

/**
 * Register an obj's lit + recoloured inventory model as a scene MODEL
 * (reference IfType.getModel type 4 / ObjType 3D interface model, driven by
 * IF_SETOBJECT — e.g. the combat-tab weapon). Requires objtype + inventory
 * model already in CacheProvider. Returns scene model id or -1.
 */
int
UITreeSceneBridge_EnsureObjModel(
    struct UITreeSceneBridge* bridge,
    int obj_id);

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

/**
 * Rasterize the white-outlined variant of an obj icon (reference
 * ObjType.getSprite with outlineRgb = 0xFFFFFF) for the item armed for "Use".
 * Cached separately from the plain icon; requires the same model residency, so
 * it only succeeds once EnsureObjIcon would. Returns scene sprite id or -1.
 */
int
UITreeSceneBridge_EnsureObjIconSelected(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count);

/**
 * Rasterize the plain (no baked drop-shadow) variant of an obj icon (reference
 * ObjType.getSprite with outlineRgb = -1). Used when the component's
 * cc_setoutline / cc_setgraphicshadow will apply the post-process at draw time
 * — stacking that pass on a SHADOW-baked icon doubles the shadow. Returns
 * scene sprite id or -1.
 */
int
UITreeSceneBridge_EnsureObjIconPlain(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count);

/**
 * Rasterize a plain icon with Soft3D-equivalent black border (cc_setoutline(1)
 * and no graphic_shadow) baked into the pixels. Prefer this over draw-time
 * SpriteNewGraphicOutline for dense item grids. Returns scene sprite id or -1.
 */
int
UITreeSceneBridge_EnsureObjIconBordered(
    struct UITreeSceneBridge* bridge,
    int obj_id,
    int count);

/**
 * Collect texture ids referenced by scene model faces that are not yet in the
 * scene texture map (and not marked failed). Returns the number written to
 * out_ids. The host loads these via CreateTask_TextureLoad, then calls
 * UITreeSceneBridge_PublishTextures; while missing, the raster skips the face
 * (reference parity: textures pop in once loaded).
 *
 * O(every live element × its faces) — a whole world's geometry. The host does
 * not need it per tick: model construction reports the ids it wants (see
 * ToriDraw_ModelTextureWantsTake). Kept for one-shot audits of a built scene.
 */
int
UITreeSceneBridge_CollectMissingTextures(
    struct UITreeSceneBridge* bridge,
    int* out_ids,
    int max_ids);

/** Is this texture id already in the scene's texture map? */
int
UITreeSceneBridge_TextureResident(
    struct UITreeSceneBridge const* bridge,
    int texture_id);

/**
 * Upload provider textures into the scene texture map for each id in ids;
 * ids the provider still lacks are marked failed so they are never
 * re-requested. Returns the number uploaded.
 */
int
UITreeSceneBridge_PublishTextures(
    struct UITreeSceneBridge* bridge,
    const int* ids,
    int id_count);

#endif
