#ifndef UITREE_SCENE_BRIDGE_H
#define UITREE_SCENE_BRIDGE_H

#include "engine/static_sprites.h"

#include <stdint.h>

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

    /** Device pixels per chrome pixel for the baked overlay faces: 1, 2 or 3.
     *  Init leaves 1; @see UITreeSceneBridge_SetChromeScale. */
    int chrome_scale;

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

    /*
     * The human ready animation, from the profile's `[seq:human_readyanim]`.
     *
     * It lives beside the player composite because that is what it poses, and
     * because both paths that build a clientCode 327/328 preview -- the boot
     * bake and the runtime interface mount -- hold a bridge and neither holds
     * the other's manifest. -1 until the App states it, and -1 leaves the
     * preview at its bind pose rather than posing it with another cache's seq.
     */
    int player_idle_seq;

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

/* Reserved scene font ids for the baked debug-overlay faces at 1x. Scene font
 * ids are cache font ids everywhere else (see EnsureFont), so these sit out of
 * that range alongside the reserved model/sprite ids above. */
#define UITREE_SCENE_DEBUG_FONT_SMALL_ID 0x40000006
#define UITREE_SCENE_DEBUG_FONT_MENU_ID 0x40000007
#define UITREE_SCENE_DEBUG_FONT_BODY_ID 0x40000008

/**
 * The same three faces at scale 2 and 3, one block of slots per scale.
 *
 * A separate block rather than an id arithmetic that runs on from the 1x ids:
 * the next id after BODY is already taken (the chrome skin, below), and a
 * scaled face silently landing on the skin's entry is the kind of collision
 * that shows up as a missing panel background three subsystems away. Scale 1
 * keeps its historic ids so nothing that already resolved one has to change.
 */
#define UITREE_SCENE_DEBUG_FONT_SCALED_BASE 0x40001000
/** @param slot enum ToriRSChromeFontSlot. @param scale >= 2. */
#define UITREE_SCENE_DEBUG_FONT_SCALED_ID(slot, scale)                                        \
    (UITREE_SCENE_DEBUG_FONT_SCALED_BASE + ((scale) - 2) * 16 + (slot))
/** The baked chrome skin: one scene entry, one atlas index per skin slot. */
#define UITREE_SCENE_CHROME_SKIN_ID 0x40000009
/** The editor catalog's model preview: one slot, re-rendered per pick. */
#define UITREE_SCENE_EDITOR_PREVIEW_ID 0x4000000A

/**
 * Base of the reserved scene-sprite range for PLUGIN IMAGES: art a plugin
 * shipped as its own asset file and decoded at runtime (scene id = base +
 * slot, one slot per resident image).
 *
 * A range rather than one multi-frame entry like the chrome skin's, because
 * these arrive one at a time and at sizes nothing knows in advance: a skin is
 * baked as a set and can be uploaded as a set, while a plugin loads an asset,
 * gets an answer some frames later, and loads another. Re-uploading a growing
 * atlas on each arrival would rebuild every sprite already in the scene.
 */
#define UITREE_SCENE_PLUGIN_IMAGE_BASE 0x48000000

/**
 * Slots in the plugin-image range: one per resident image across every plugin.
 *
 * It has to be at least the plugin host's TORIRS_PLUGIN_IMAGES_MAX, and it is
 * stated here rather than there because the SCENE is what runs out -- an image
 * the host handed a slot to and the bridge refused is an image that decodes
 * fine and draws nothing, reported as "would not decode". The two are held
 * together by a static assert in plugin/torirs_plugin_bridge.u.c, which is the
 * one file that sees both numbers. The range above it starts 0x08000000
 * higher, so there is no ceiling here worth economising against.
 */
#define UITREE_SCENE_PLUGIN_IMAGE_SLOTS 192

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
 * Register one of the two baked debug faces (enum ToriRSChromeFontSlot) in the scene
 * and return its reserved scene font id.
 *
 * Needs no cache and no provider: the faces are compiled in. They are also
 * `static`, and ToriDraw_SceneGraphShutdown frees every font it holds, so this
 * registers a deep copy (~4 KB of glyph alpha per face) rather than the baked
 * struct itself.
 */
/**
 * Upload the baked chrome skin (src/engine/torirs_chrome_skin_baked.h) as one
 * multi-frame scene sprite and return its scene id, or -1 when the build has
 * no skin baked in. Idempotent. Registers deep copies, for the same reason
 * EnsureDebugFont does: the scene frees what it holds and these are .rdata.
 */
int
UITreeSceneBridge_EnsureChromeSkin(struct UITreeSceneBridge* bridge);

/**
 * Publish one plugin image at `slot` and return its scene id.
 *
 * `argb` is COPIED: the scene frees every sprite it holds, and the caller's
 * buffer is a decoded asset it goes on to free itself. A slot already holding
 * an image is replaced, which is what lets a plugin re-save an asset and see
 * the new pixels without a restart.
 *
 * @return the scene id, or -1 when the slot or the geometry is out of range.
 */
int
UITreeSceneBridge_PublishPluginImage(
    struct UITreeSceneBridge* bridge,
    int slot,
    int width,
    int height,
    uint32_t const* argb);

/**
 * Copy a published plugin image's pixels back into `out`, which holds `max`.
 *
 * The scene is where the pixels live -- the publish above deep-copies into it
 * and the caller's buffer is gone -- so this is the only place a plugin's own
 * art can be read back to compose something new out of.
 *
 * @return how many pixels were copied, or 0 when the slot holds nothing or the
 * buffer is too small for the whole image. Never a partial copy: half an image
 * is a torn picture, not a smaller one.
 */
int
UITreeSceneBridge_ReadPluginImage(
    struct UITreeSceneBridge* bridge,
    int slot,
    uint32_t* out,
    int max);

/** Drop a published plugin image, freeing its scene entry. */
void
UITreeSceneBridge_ReleasePluginImage(struct UITreeSceneBridge* bridge, int slot);

/**
 * Which chrome scale the debug-overlay faces resolve at: 1, 2 or 3.
 *
 * Held here because this is where slot becomes scene font id, and the whole
 * requirement is that the size the chrome LAID OUT with is the size the
 * renderer DRAWS with. One value, read by every resolve, is what makes the two
 * unable to disagree.
 */
void
UITreeSceneBridge_SetChromeScale(struct UITreeSceneBridge* bridge, int scale);

int
UITreeSceneBridge_ChromeScale(struct UITreeSceneBridge const* bridge);

int
UITreeSceneBridge_EnsureDebugFont(
    struct UITreeSceneBridge* bridge,
    int font_slot);

/**
 * The same baked faces, pinned at 1x whatever the chrome scale is.
 *
 * For text that lands in INTERFACE pixels rather than chrome pixels -- a
 * `[component:] font=chrome:<slot>`, which the gameframe lays out at its own
 * scale and the shell then scales again. EnsureDebugFont resolves at the
 * chrome's scale on purpose (the overlay measures and paints at the display's
 * density), and asking it for a component's face is the giant-text bug: a 2x
 * face drawn into 1x interface coordinates, doubled a second time on the way
 * to the window.
 *
 * @return the scene font id, or -1 for an unknown slot.
 */
int
UITreeSceneBridge_EnsureDebugFont1x(
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
 * `slots` is the 12-entry canonical appearance array (pkt_player_appearance.h:
 * empty, kit or obj).
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

/** Build or replace an independently-owned interface player composition at
 *  `scene_id`. Unlike BuildLocalPlayerModel this never shares a reserved model
 *  with another widget, so incremental server setters cannot mutate siblings. */
int
UITreeSceneBridge_BuildInterfacePlayerModel(
    struct UITreeSceneBridge* bridge,
    int scene_id,
    int const slots[12],
    int const colours[5],
    int gender);

/* Per-widget composition models live outside cache ids and the other reserved
 * composites. App assigns monotonically increasing ids from this base. */
#define UITREE_SCENE_IF_PLAYER_MODEL_BASE 0x60000000

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
