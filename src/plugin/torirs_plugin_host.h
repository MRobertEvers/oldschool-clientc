#ifndef TORIRS_PLUGIN_HOST_H
#define TORIRS_PLUGIN_HOST_H

/** The object's own appearance: the host-only third placement. Numerically
 *  what ui/uitree_host.h calls UITREE_ROLE_PLACE_SELF; the two headers do not
 *  include each other, so the number is stated on both sides. */
#define PLUGIN_ANCHOR_PLACE_SELF 2

/*
 * The host: the bus, the api implementation, the config store, and the entry
 * points the engine calls at each seam.
 *
 * The engine is reached through a vtable it fills in (ToriRS_PluginEngine)
 * rather than by including app.h, for three reasons: app.c's overlay and
 * projection helpers stay static, the host compiles and tests standalone
 * against a fake engine, and the direction of the dependency matches every
 * other injection seam in this tree (UITreeHost, EditorHost, World_SeqSource).
 */

#include "plugin/torirs_plugin_types.h"
#include "plugin/torirs_plugin_host_types.h"
#include "plugin/torirs_plugin_ui.h"
#include "plugin/torirs_plugin_v2.h"

#include <stdbool.h>
#include <stdint.h>

#define TORIRS_PLUGIN_MAX 32
/**
 * Config slots one plugin's store may hold: its whole declared schema, PLUS
 * any key an ini carried that the schema does not claim (a setting renamed
 * between versions, or a section whose plugin has not loaded yet -- see
 * PluginConfigSlot.schema_index). So this is deliberately LARGER than the
 * schema ceiling a plugin is held to, and the difference is the headroom for
 * those strays.
 *
 * The schema half of that pair is PLUGIN_LUA_MAX_CONFIG (32) for a script;
 * PluginHost_Register refuses any def, C or Lua, whose schema does not fit
 * here. Sized against the plugin this tree ports from -- RuneLite's Ground
 * Items carries 33 settings -- rather than against the plugins that exist
 * today, since it was the smaller of these two numbers that a real port hit
 * first.
 *
 * The cost is fixed and paid whether or not a plugin uses it:
 * sizeof(struct PluginConfigSlot) is 260, so the store is 40 * 260 = ~10 KB
 * per plugin and ~333 KB across TORIRS_PLUGIN_MAX.
 */
#define TORIRS_PLUGIN_CONFIG_MAX 40
#define TORIRS_PLUGIN_CONFIG_VALUE_MAX 192
#define TORIRS_PLUGIN_SUBS_MAX 32
/* Rows one menu build may carry routes for. Bounded by the menu itself. */
#define TORIRS_PLUGIN_MENU_ROUTES_MAX 24
/* Overlay items one plugin may push per frame, before the host clips it and
 * says so. The pool it draws from is shared with health bars and chat. */
#define TORIRS_PLUGIN_DRAW_BUDGET 512
/** World objects one plugin may hold at once. A loot beam per lit tile is the
 *  shape this is sized for; past it object_create refuses and says so. */
#define TORIRS_PLUGIN_OBJECT_BUDGET 64
/** Authored meshes one plugin may hold at once. Far smaller than the object
 *  budget on purpose: a mesh is a SHAPE and the objects standing on it are the
 *  copies, so a plugin needs one per distinct thing it draws (a beam per
 *  style, a marker, a plinth) and not one per place it draws it. */
#define TORIRS_PLUGIN_MESH_BUDGET 8
/**
 * Resident assets, across every plugin.
 *
 * Raised from 32 with the image ceiling below, and for the same reason: an
 * image is READ through the asset sandbox, so every picture a plugin ships
 * occupies a slot here as well as one there, and a gameframe is seventy
 * pictures. At 32 the layout plugin's tab icons loaded as handles with no
 * pixels behind them -- a frame drawn with half its art missing and one line
 * on stderr to say why.
 *
 * The cost is honest and known: the PNG bytes of an image stay resident after
 * the decode that only needed them once, so ~70 KB of this table is bytes
 * nothing will read again. Dropping them would mean the host deciding that a
 * file loaded as an image is not also wanted as bytes, which is not something
 * it can know -- a plugin may legitimately hold both. Slots are cheap; the
 * guess would not be.
 */
#define TORIRS_PLUGIN_ASSETS_MAX 128
/** Resident shipped MODELS, across every plugin. Each holds decoded geometry
 *  the host keeps for as long as the plugin runs, so the ceiling is what stops
 *  a plugin from loading a folder of art nothing stands on. */
#define TORIRS_PLUGIN_MODELS_MAX 32
/**
 * Resident IMAGES, across every plugin. Each holds a decoded ARGB sprite in
 * the scene, so the ceiling is what keeps a plugin from filling the scene's
 * sprite table with art nothing draws.
 *
 * Raised from 64 when the gameframe layouts landed. 64 was sized against the
 * plugins that existed -- an orb set is ten pictures and an xp drop is two --
 * and a whole GAMEFRAME is a different order of thing: two families of stone
 * surround, twenty-seven tab icons and nine mirrored redstones is seventy-odd
 * on its own, and it has to share the table with whatever else is switched on.
 * At ~40 bytes a slot the whole table is under 8 KB either way, so the number
 * is bounded by what is reasonable to draw rather than by what it costs.
 */
#define TORIRS_PLUGIN_IMAGES_MAX 192
/**
 * Item icons the host keeps rasterised, across every plugin.
 *
 * A CACHE and not a budget: nothing refuses past this, the least recently
 * asked-for entry is dropped instead. @see ToriRS_PluginApi::obj_image.
 *
 * Sized against what a page of them costs rather than against a plugin's
 * appetite, because the appetite is unbounded -- a loot tracker's history
 * names every item it has ever seen, and a bank's is the whole game. Each
 * entry occupies one of the IMAGES_MAX slots above and about 4.6 KB of scene
 * sprite (36x32 ARGB), so 48 is a quarter of the image table and ~220 KB, and
 * it comfortably holds every distinct icon on one visible page with room for
 * the one being scrolled towards.
 */
#define TORIRS_PLUGIN_OBJ_ICONS_MAX 48
/** Live named placement reservations across every plugin. */
#define TORIRS_PLUGIN_RESERVES_MAX 32
/** Longest screenshot destination a plugin may name, including separators. */
#define TORIRS_PLUGIN_SCREENSHOT_DIR_MAX 192

struct ToriRS_PluginHost;

/** Engine draw_select_canvas mode for a panel-local custom target. The
 *  application prepares that target before PluginHost_PanelDraw. */
#define TORIRS_PLUGIN_ENGINE_DRAW_PANEL 3

/* ------------------------------------------------------------------------ */
/* The engine seam. app.c implements every one of these.                     */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginEngine
{
    /** struct App*. */
    void* user;

    /** enum AppScreen, as a TORIRS_SCREEN_* value.
     *  @see ToriRS_PluginApi::screen. */
    int (*screen)(void* user);

    /** Canvas minus what the OS is covering (the soft keyboard).
     *  @see ToriRS_PluginApi::safe_os. */
    int (*safe_os)(
        void* user,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);
    /**
     * Exact platform-safe fragments. Pass -1 to start; returns the fragment
     * index or -1 when finished. Optional: the host adapts safe_os as one
     * fragment when absent.
     */
    int (*platform_safe_next)(
        void* user,
        int iter,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);

    int (*world_cycle)(void* user);
    uint64_t (*frame_ms)(void* user);
    uint64_t (*frame_work_us)(void* user);
    /** Named runtime/platform capability. Unknown names return zero. Optional
     * for focused harnesses; absence means no advertised capabilities. */
    int (*capability)(void* user, char const* name);
    size_t (*memory_bytes)(void* user);

    int (*local_player)(
        void* user,
        struct ToriRS_PlayerSnapshot* out);
    int (*npc_next)(
        void* user,
        int iter,
        struct ToriRS_NpcSnapshot* out);
    int (*npc_by_slot)(
        void* user,
        int server_slot,
        struct ToriRS_NpcSnapshot* out);
    int (*player_next)(
        void* user,
        int iter,
        struct ToriRS_PlayerSnapshot* out);
    int (*obj_next)(
        void* user,
        int iter,
        struct ToriRS_GroundItemSnapshot* out);
    int (*loc_next)(
        void* user,
        int iter,
        struct ToriRS_ScenerySnapshot* out);
    int (*highlight_next)(
        void* user,
        int iter,
        struct ToriRS_HighlightItem* out);

    /** One game line into the chatbox. @see ToriRS_PluginApi::notify. */
    void (*notify)(
        void* user,
        char const* text);

    int (*key_held)(
        void* user,
        int keycode);
    int (*hover_tile)(
        void* user,
        int* out_tile_x,
        int* out_tile_z,
        int* out_level);
    int (*hover_entity)(
        void* user,
        struct ToriRS_HoverTarget* out);
    int (*element_height)(
        void* user,
        int element_id);

    /** The pointer, in canvas coords. @see ToriRS_PluginApi::mouse_pos. */
    int (*mouse_pos)(
        void* user,
        int* out_x,
        int* out_y);
    /**
     * One PLACEABLE region's box, plus CANVAS. @see
     * ToriRS_PluginApi::slot_rect.
     *
     * The two SAFE regions are deliberately not here: SAFE_GAMECHROME is
     * derived from these plus the host's own reservation table, and the host
     * is the only thing that holds both; SAFE_LANECHROME is CANVAS minus the
     * `lane_chrome_<n>` roles, and is derived beside it so there is one
     * subtraction rule rather than two.
     */
    int (*slot_rect)(
        void* user,
        int slot,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);
    /**
     * One MEMBER of a placeable region. @see
     * ToriRS_PluginApi::slot_member_rect.
     *
     * The read half of layout_slot's `member`, so the two use one numbering.
     */
    int (*slot_member_rect)(
        void* user,
        int slot,
        int member,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);
    /**
     * The size the LANE authored for a placeable region's surface. @see
     * ToriRS_PluginApi::slot_native_size.
     *
     * The engine's to answer because it is a fact about the tree and about
     * nothing else: no reservation, no claim and no plugin state is in it.
     */
    int (*slot_native_size)(
        void* user,
        int slot,
        int* out_w,
        int* out_h);
    /** One component's box, by id. @see ToriRS_PluginApi::component_rect. */
    int (*component_rect)(
        void* user,
        int component_id,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);

    /* The semantic roles. @see ToriRS_PluginApi::role_rect and friends.
     *
     * Whole verbs and not a name->id resolver the host then feeds to the id
     * verbs, because a role may name a node that HAS no id -- and because the
     * answer has to be taken and used in one step: a role bound to a
     * script-built component is only true until that subtree is next rebuilt.
     *
     * `safe_gamechrome` and `safe_lanechrome` are the exceptions the engine
     * cannot answer, for the same reason it cannot answer their slots: both are
     * derived, one from the reservation table and one from the canvas, and both
     * are derived in the host. It intercepts those two names before they get
     * here. The `lane_chrome_<n>` roles the second is built from are ordinary
     * profile roles and DO come here. */

    /** Where a role's element is. @see ToriRS_PluginApi::role_rect. */
    int (*role_rect)(
        void* user,
        char const* role,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);
    /** Whether it is on screen. @see ToriRS_PluginApi::role_visible. */
    int (*role_visible)(
        void* user,
        char const* role);
    /** Press it. @see ToriRS_PluginApi::role_click. */
    int (*role_click)(
        void* user,
        char const* role,
        int op);
    /** Its component id right now, or -1. @see ToriRS_PluginApi::role_id. */
    int (*role_id)(
        void* user,
        char const* role);
    /**
     * Does `role` name a FRAME SLOT member on this revision, and which?
     *
     * The reverse of role_rect, and the one question the chrome tier cannot
     * answer without the engine. An arranger declares a part's art against
     * (slot, member) because that is what it just placed; a dresser asks for
     * it by role name because a name is the only address that survives
     * changing lanes. Something has to know that `report_button` IS
     * `slot(chat_buttons, report)` here, and the only thing that does is the
     * profile's own role table.
     *
     * `out_member` is -1 for a role naming a whole region.
     *
     * @return 1 when the role resolves to a slot, 0 for a cache component, a
     * role this revision has not declared, or no tree.
     */
    int (*role_slot)(
        void* user,
        char const* role,
        int* out_slot,
        int* out_member);
    /** Reconcile one persistent owner-scoped replacement declaration. */
    int (*role_replace)(
        void* user,
        int plugin,
        char const* role,
        int enabled);
    /** Suppress only a live role node's native paint and/or own input. Its
     * children remain live. Idempotent and re-resolved across tree rebuilds. */
    int (*role_suppress_facets)(
        void* user,
        char const* role,
        int paint,
        int input);
    /** Select/reset the role anchor for the open canvas subscriber. A NULL
     * role resets it; a non-NULL empty role selects active-invalid/drop state;
     * `replace` says the caller owns the replacement claim. */
    /** `place` is enum ToriRS_LegacyAnchorPlace, or PLUGIN_ANCHOR_PLACE_SELF
     *  for the replacement owner's own appearance -- the object itself, which
     *  everything BEFORE paints under and everything AFTER paints over. */
    int (*role_anchor)(
        void* user,
        int plugin,
        char const* role,
        int replace,
        int place);

    /* The resolved gameframe. The host selects one published offer; the
     * engine owns what activation does -- suppressing native chrome, pinning
     * the canvas, and moving the live surfaces. */

    /** The claim changed (or was restated). `owned` is 1 while a plugin holds
     *  the frame; `canvas` is enum ToriRS_EngineFrameCanvas and `fixed_w/h`
     *  the pinned canvas, 0 when it follows the window. */
    void (*layout_set)(
        void* user,
        int owned,
        int canvas,
        int fixed_w,
        int fixed_h);
    /** Empty the slot table, before an EV_LAYOUT declaration. */
    void (*layout_begin)(void* user);
    /** Apply what the declaration left behind, and hide every surface it did
     *  not place. */
    void (*layout_end)(void* user);
    /** Place one slot, or one MEMBER of it when `member` is not -1.
     *  @see ToriRS_PluginApi::layout_slot_at; the return is the same "does
     *  this frame have one" answer. */
    int (*layout_slot)(
        void* user,
        int slot,
        int member,
        int x,
        int y,
        int w,
        int h);
    /** Pure presence query used while a replacement frame is staged. Unlike
     * layout_slot, this must not mutate engine declaration state. Optional;
     * the host falls back to the resolved slot rectangle queries. */
    int (*layout_slot_exists)(
        void* user,
        int slot,
        int member);
    /** Skin one slot: the picture it draws from and the alpha cut-out it is
     *  clipped to, as plugin image slots (art -1 keeps native; mask -1 clears).
     *  @see ToriRS_PluginApi::layout_slot_skin. */
    int (*layout_slot_skin)(
        void* user,
        int slot,
        int art,
        int mask);
    /** Paint one plugin image immediately after a slot's primary subtree.
     *  The image is a plugin slot and may still be pending; the engine maps
     *  its stable handle to the stable scene id.
     *  @see ToriRS_PluginApi::layout_slot_overlay. */
    int (*layout_slot_overlay)(
        void* user,
        int slot,
        int image,
        int x,
        int y,
        int trans);
    /** Six plugin image slots in UITreeScrollbarSkinPiece order, or NULL to
     *  clear. @see ToriRS_PluginApi::layout_scrollbar. */
    int (*layout_scrollbar)(
        void* user,
        int const* images,
        int count);
    /** The selected sidebar tab, or -1. @see ToriRS_PluginApi::tab_active. */
    int (*tab_active)(void* user);
    /** Flip to that tab. @see ToriRS_PluginApi::tab_select. */
    int (*tab_select)(
        void* user,
        int tabno);
    /** Nonzero when that tab has an interface mounted behind it.
     *  @see ToriRS_PluginApi::tab_enabled. */
    int (*tab_enabled)(
        void* user,
        int tabno);
    /** One skill's boosted and base level. @see ToriRS_PluginApi::stat. */
    int (*stat)(
        void* user,
        int skill,
        int* out_current,
        int* out_base);
    /** One skill's xp and the thresholds either side of it.
     *  @see ToriRS_PluginApi::stat_xp. */
    int (*stat_xp)(
        void* user,
        int skill,
        int* out_xp,
        int* out_level_xp,
        int* out_next_xp);
    /** The skill's name, or NULL past the stat table.
     *  @see ToriRS_PluginApi::skill_name. */
    char const* (*skill_name)(
        void* user,
        int skill);
    /** Run energy, 0..100. */
    int (*run_energy)(void* user);

    /**
     * The client-only feature flags. @see ToriRS_PluginApi::feature_next.
     *
     * The ENGINE decides what is on this list, and that is the whole of the
     * rule keeping a server-agreed flag away from a plugin: there is no key
     * for one, so no plugin can name it and none can be added by a plugin
     * shipping its own idea of the list.
     */
    int (*feature_next)(
        void* user,
        int iter,
        struct ToriRS_FeatureInfo* out);
    int (*feature_get)(
        void* user,
        char const* key);
    int (*feature_set)(
        void* user,
        char const* key,
        int value);
    /** One client-wide display preference and its range, and the setter for
     *  it. @see ToriRS_PluginApi::display_setting. */
    int (*display_setting)(
        void* user,
        int setting,
        int* out_value,
        int* out_min,
        int* out_max);
    int (*display_setting_set)(
        void* user,
        int setting,
        int value);

    /**
     * The device-local requested gameframe. Returns 1 when the preference was
     * explicitly present, 0 for a fresh/legacy file (and still writes `auto`).
     * `out_migration_version` lets the host perform the one-time conversion
     * from the two legacy plugin enable switches.
     *
     * Optional for focused harnesses; an absent callback means `auto` without
     * persistence.
     */
    int (*frame_preference)(
        void* user,
        char* out,
        int out_size,
        int* out_migration_version);
    int (*frame_preference_set)(
        void* user,
        char const* id,
        int migration_version);

    /** The client's live var state, read-only. @see ToriRS_PluginApi::varbit. */
    int (*varbit)(
        void* user,
        int varbit_id);
    int (*varp)(
        void* user,
        int varp_id);

    /** The boot profile's `[<kind>:<name>] id=` table. @see ToriRS_PluginApi::cache_id. */
    int (*cache_id)(
        void* user,
        char const* kind,
        char const* name);
    /** What `[cache:boot]` stated. OPTIONAL -- a NULL here answers every
     *  plugin UNKNOWN, which is the one answer nothing decides on.
     *  @see ToriRS_PluginApi::lane. */
    int (*lane)(
        void* user,
        struct ToriRS_LaneInfo* out);
    /** The live gameframe's root interface group, or -1 on a revconfig frame.
     *  Optional like `lane`: a harness with no notion of a cache gameframe
     *  leaves it NULL and every plugin reads -1. */
    int (*frame_root)(void* user);

    /** One objtype, resident-only. @see ToriRS_PluginApi::obj_info. */
    int (*obj_info)(
        void* user,
        int obj_id,
        struct ToriRS_ItemInfo* out);
    /** One container slot. `inv` is enum ToriRS_InventoryKind.
     *  @see ToriRS_PluginApi::inv_slot. */
    int (*inv_slot)(
        void* user,
        int inv,
        int slot,
        int* out_obj_id,
        int* out_count);
    /** That container's slot count. @see ToriRS_PluginApi::inv_size. */
    int (*inv_size)(
        void* user,
        int inv);

    int (*project)(
        void* user,
        int fine_x,
        int fine_z,
        int height_above_ground,
        int* out_x,
        int* out_y);

    /* Drawing. Each returns the number of overlay items it pushed, so the host
     * can hold a plugin to its per-frame budget. */

    /**
     * Which list the draw calls below append to: 0 the world overlay, 1 the
     * canvas overlay. Set by the host around each draw dispatch and never by a
     * plugin.
     *
     * A mode rather than a second set of draw entry points, because a rect is
     * a rect: only the list it lands in and the clip that list carries differ,
     * and duplicating five builders to say so would leave five chances for the
     * two to drift apart.
     */
    void (*draw_select_canvas)(
        void* user,
        int canvas);

    /**
     * Decode `data` as an image and publish it at `slot`, returning 1 on
     * success. The engine owns the decode and the scene entry; the host owns
     * which plugin may see which slot.
     */
    int (*image_publish)(
        void* user,
        int slot,
        void const* data,
        int size,
        int* out_w,
        int* out_h);
    /**
     * Publish `w`x`h` ARGB pixels at `slot`, returning 1 on success.
     *
     * image_publish's sibling with the decode taken out, because the caller
     * already has pixels. @see ToriRS_PluginApi::image_compose.
     */
    int (*image_publish_argb)(
        void* user,
        int slot,
        int w,
        int h,
        uint32_t const* argb);
    /**
     * Copy a published image's pixels back into `out`, which holds `max` of
     * them. @return how many were copied, 0 if it will not fit or the slot
     * holds nothing. @see ToriRS_PluginApi::image_pixels.
     */
    int (*image_read)(
        void* user,
        int slot,
        uint32_t* out,
        int max);
    /** Drop a published image. Idempotent. */
    void (*image_release)(
        void* user,
        int slot);

    /**
     * Rasterise the client's inventory icon for `obj_id` at `count` and
     * publish it at plugin image `slot`.
     *
     * `style` is enum ToriRS_ItemIconStyle. Returns 1 when the slot now
     * holds the icon, 0 when the objtype or its inventory model is not
     * resident yet -- which is an ordinary state and not a failure, so the
     * host answers -1 and the plugin asks again.
     *
     * The engine end owns the build because an icon is a scene render: the
     * interface emitter already asks the scene bridge for exactly these three
     * variants and the bridge already caches them, so this reuses that work
     * and copies the finished pixels into the plugin's slot.
     * @see ToriRS_PluginApi::obj_image.
     */
    /** Walk the client's recorded loot. Iterator protocol of npc_next.
     *  @see ToriRS_PluginApi::loot_source_next. */
    int (*loot_source_next)(
        void* user,
        int iter,
        struct ToriRS_LootSource* out);
    int (*loot_row_next)(
        void* user,
        int source_id,
        int iter,
        struct ToriRS_LootRow* out);
    uint64_t (*loot_revision)(void* user);
    int (*loot_source_clear)(void* user, int source_id);

    int (*obj_image)(
        void* user,
        int slot,
        int obj_id,
        int count,
        int style,
        int* out_w,
        int* out_h);
    /**
     * Record a hit region for the plugin currently drawing.
     * @see ToriRS_PluginApi::hit_region. Returns 1 when it was kept.
     */
    int (*hit_region)(
        void* user,
        int plugin,
        int x,
        int y,
        int w,
        int h,
        char const* const* ops,
        int op_count,
        uint32_t tag);
    /** Press an interface button. @see ToriRS_PluginApi::if_click. */
    int (*if_click)(
        void* user,
        int component_id,
        int op);
    /** @see ToriRS_PluginApi::text_input. */
    void (*text_input)(
        void* user,
        int on);
    /** @see ToriRS_PluginApi::chat_focus. */
    void (*chat_focus)(
        void* user,
        int on);
    /** Blit a published image. @see ToriRS_PluginApi::draw_image. */
    int (*draw_image)(
        void* user,
        int slot,
        int x,
        int y,
        int w,
        int h,
        int clip_x,
        int clip_y,
        int clip_w,
        int clip_h,
        int trans);
    int (*draw_tile)(
        void* user,
        int tile_x,
        int tile_z,
        int level,
        uint32_t rgb,
        uint32_t fill_rgb,
        int fill_alpha);
    int (*draw_hull)(
        void* user,
        int element_id,
        uint32_t rgb,
        int fill_alpha,
        int shape);
    int (*draw_line)(
        void* user,
        int x0,
        int y0,
        int x1,
        int y1,
        uint32_t rgb);
    int (*draw_text)(
        void* user,
        int x,
        int y,
        char const* text,
        uint32_t rgb);
    int (*draw_rect)(
        void* user,
        int x,
        int y,
        int w,
        int h,
        uint32_t rgb,
        int fill_alpha);

    /** Append a row carrying `action_id` to the menu build in progress.
     *  Returns 1 on success, 0 when the menu is full. */
    int (*menu_add)(
        void* user,
        void* cursor,
        char const* text,
        int action_id);
    /**
     * Remove one row from the menu being built, by the index it had in the
     * EV_MENU_BUILD payload. Rows above it shift down, so a caller dropping
     * several walks from the highest index to the lowest.
     *
     * For the entity tier: a HITBOX holder that REPLACES the game's rows for
     * an npc needs the game's rows gone, and appending cannot say that.
     * @see ToriRS_PluginApi::entity_ops.
     */
    int (*menu_drop)(
        void* user,
        void* cursor,
        int index);

    /* Assets. The engine owns the paths and the IO queue; the host owns the
     * resident bytes and who is allowed to see them, which is why `plugin` is
     * a name and not an index -- it is a directory component. */

    /** Queue a read. The engine calls PluginHost_AssetDeliver when it lands,
     *  with the bytes or with NULL. Returns 1 when the read was queued. */
    int (*asset_read)(
        void* user,
        char const* plugin,
        char const* name);
    /** Queue a write. `data` is COPIED by the engine: the host's resident copy
     *  is replaced the moment asset_save returns and cannot be borrowed for
     *  the lifetime of an async write. */
    int (*asset_write)(
        void* user,
        char const* plugin,
        char const* name,
        void const* data,
        int size);

    /** Record a deferred frame capture. `dir` is NULL or "" for the plugin's
     *  own saved-asset directory. Returns 1 when the request was accepted; the
     *  engine takes it at the end of the frame and writes the PNG itself.
     *  `out_path` is filled with the file the capture will land in -- the
     *  engine owns the folders, so it is the only side that can say -- and is
     *  left empty when there is nowhere to write it. */
    int (*screenshot)(
        void* user,
        char const* plugin,
        char const* dir,
        char const* name,
        char* out_path,
        int out_path_size);

    /* World objects. Handles are the engine's; the host records who owns each
     * one so a stopped plugin's objects leave the scene with it. */

    /* Authored meshes. Storage is the engine's, for the same reason objects'
     * is: only it can turn one into a drawable model. */

    /** Decode `data` as a model and hold it at slot `model`. Returns 0 when
     *  the bytes are not a model this client reads -- which is an answer, not
     *  an error: the plugin named the file. */
    int (*model_publish)(
        void* user,
        int model,
        void const* data,
        int size);
    void (*model_release)(
        void* user,
        int model);

    int (*mesh_create)(void* user);
    void (*mesh_destroy)(
        void* user,
        int mesh);
    void (*mesh_clear)(
        void* user,
        int mesh);
    int (*mesh_vertex)(
        void* user,
        int mesh,
        int x,
        int y,
        int z);
    int (*mesh_face)(
        void* user,
        int mesh,
        int a,
        int b,
        int c,
        int hsl,
        int alpha);

    int (*object_create)(void* user);
    void (*object_destroy)(
        void* user,
        int object);
    void (*object_set_model)(
        void* user,
        int object,
        int source,
        int id);
    void (*object_recolor)(
        void* user,
        int object,
        int hsl_from,
        int hsl_to);
    void (*object_clear_recolors)(
        void* user,
        int object);
    void (*object_set_anim)(
        void* user,
        int object,
        int seq_id,
        int loop);
    void (*object_set_light)(
        void* user,
        int object,
        int ambient,
        int contrast);
    void (*object_set_position)(
        void* user,
        int object,
        int tile_x,
        int tile_z,
        int level,
        int height,
        int yaw);
    void (*object_set_active)(
        void* user,
        int object,
        int active);
    int (*object_ready)(
        void* user,
        int object);

    int (*hsl_from_rgb)(
        void* user,
        uint32_t rgb);
    uint32_t (*hsl_to_rgb)(
        void* user,
        int hsl);
};

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/** Allocates and registers every statically linked plugin. `engine` is copied.
 *  Plugins are not started here -- PluginHost_Start does that, once the config
 *  store has been loaded. */
struct ToriRS_PluginHost*
PluginHost_New(struct ToriRS_PluginEngine const* engine);

/** Calls on_stop for everything still running, in reverse start order. */
void
PluginHost_Free(struct ToriRS_PluginHost* host);

/**
 * Register one callback-table v2 plugin. The definition and everything it
 * references must outlive the host. Per-instance state is host-owned, zeroed
 * before each start, and released after on_stop.
 */
int
PluginHost_RegisterV2(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginDefV2 const* def);

/** Calls on_start for each newly enabled plugin. Idempotent, so a dynamically
 * registered script can call this after registration. */
void
PluginHost_Start(struct ToriRS_PluginHost* host);

/** Enable or disable at runtime, including deterministic retained teardown. */
void
PluginHost_SetEnabled(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    bool enabled);

/**
 * Stop a plugin, rebuild it from its source, and start it again.
 *
 * What "Save" in the plugin window does after it writes the settings, and the
 * reason it does: a plugin reads its config in on_start and caches what it
 * found, so writing a key underneath a running plugin leaves it running on the
 * old value with the panel showing the new one.
 *
 * Saved config values SURVIVE -- that is the whole point -- while keys the
 * reloaded source newly declares arrive with their defaults. Subscriptions,
 * world objects, resident assets and the window tab are all released and
 * rebuilt, so nothing from the previous run outlives it.
 *
 * A disabled plugin is left alone: it is already torn down, and restarting it
 * here would switch it on behind the user's back.
 */
void
PluginHost_Reload(
    struct ToriRS_PluginHost* host,
    int plugin_index);

/** Internal language-adapter hook for rebuilding source between teardown and
 * restart. It is not part of the public plugin API. */
void
PluginHost_SetReloadHandler(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    void (*handler)(struct ToriRS_PluginHost*, int, void*),
    void* user);
/**
 * Is this plugin switched on RIGHT NOW -- the user's switch, minus any lane
 * that refused it. What the roster's checkbox and the boot line both want.
 *
 * Deliberately not the saved state. A plugin that called `disable_self` is
 * off, and reporting it on because the ini still says so would draw a ticked
 * box over a plugin that is torn down. The saved line is the config encoder's
 * business and nobody else's. @see ToriRS_PluginApi::disable_self.
 */
bool
PluginHost_IsEnabled(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
int
PluginHost_Count(struct ToriRS_PluginHost const* host);
/** The plugin's identity: the ini section, the manifest entry, the key
 *  PluginHost_IndexOf answers to. Never what a person is shown -- use
 *  PluginHost_Title for that. */
char const*
PluginHost_Name(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/**
 * The plugin's human name: "Entity Highlighter", for the roster and the page
 * header.
 *
 * Never empty and never NULL: a def that declared no title gets one derived
 * from its name, so no caller has to carry a fallback of its own and no panel
 * can end up printing a slug. It is a LABEL -- nothing may key off it, and it
 * may change between two runs of the same plugin.
 */
char const*
PluginHost_Title(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/** Last error text for a plugin, or NULL. Shown in the settings panel. */
char const*
PluginHost_Error(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/** Does this plugin exist to host others? @see ToriRS_PluginDef::adapter --
 *  the settings roster is the only caller, and it reads it to decide whether
 *  the row is worth showing. */
bool
PluginHost_IsRuntimeHost(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/** Is this a builtin, whose switch lives in the cache's All Settings panel?
 *  @see ToriRS_PluginDef::hidden. The roster is the only caller. */
bool
PluginHost_IsHidden(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/** Is this the plugin that cannot be switched off, and sorts to the top of the
 *  roster? @see ToriRS_PluginDef::essential. */
bool
PluginHost_IsEssential(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
void
PluginHost_SetError(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* text);
int
PluginHost_IndexOf(
    struct ToriRS_PluginHost const* host,
    char const* name);

/* Canonical named UI, used by the v2 adapter and diagnostics. */
struct ToriRS_UiNodeRef
PluginHost_UiRef(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* name);
bool
PluginHost_UiInfo(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeInfo* out);
bool
PluginHost_UiInvoke(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    char const* action);
int
PluginHost_UiChangeNext(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiChange* out);
int
PluginHost_UiPresentationCount(struct ToriRS_PluginHost const* host);
uint32_t
PluginHost_UiPresentationRebuilds(struct ToriRS_PluginHost const* host);
/** Instrumentation for retained presenter cost-model regressions. */
uint32_t
PluginHost_UiPresentationChangeVisits(struct ToriRS_PluginHost const* host);
uint32_t
PluginHost_UiPresentationRegistryVisits(struct ToriRS_PluginHost const* host);
uint32_t
PluginHost_UiPresentationRoleProbeVisits(struct ToriRS_PluginHost const* host);

/* ------------------------------------------------------------------------ */
/* Seam entry points. Each is a no-op when nothing subscribed.               */
/* ------------------------------------------------------------------------ */

/** `drawn_frames` is the client's cumulative rendered-frame count; see
 *  ToriRS_FrameEvent. */
void
PluginHost_FrameStart(
    struct ToriRS_PluginHost* host,
    uint64_t now_ms,
    uint64_t drawn_frames);
void
PluginHost_LogicTick(
    struct ToriRS_PluginHost* host,
    int logic_cycle);
void
PluginHost_ServerTick(
    struct ToriRS_PluginHost* host,
    int world_cycle);
void
PluginHost_WorldLoaded(
    struct ToriRS_PluginHost* host,
    int base_tile_x,
    int base_tile_z);

void
PluginHost_NpcSpawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_NpcSnapshot const* npc);
void
PluginHost_NpcRetype(
    struct ToriRS_PluginHost* host,
    struct ToriRS_NpcSnapshot const* npc);
void
PluginHost_NpcDespawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_NpcSnapshot const* npc);

/** A chat line landed. `sender` may be NULL for a system message. */
void
PluginHost_ChatMessage(
    struct ToriRS_PluginHost* host,
    int type,
    char const* sender,
    char const* text);

/** A recognised notable moment. `kind` is the recogniser's stable name and
 *  must not be NULL; `subject` and `text` may be. */
void
PluginHost_GameEvent(
    struct ToriRS_PluginHost* host,
    char const* kind,
    char const* subject,
    int value,
    char const* text);

void
PluginHost_ObjSpawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_GroundItemSnapshot const* obj);
void
PluginHost_ObjCount(
    struct ToriRS_PluginHost* host,
    struct ToriRS_GroundItemSnapshot const* obj);
void
PluginHost_ObjDespawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_GroundItemSnapshot const* obj);

/** An engine-side asset read finished. `data` is HANDED OVER on success (the
 *  host frees it); NULL means the read failed, and the plugin is told so. Both
 *  outcomes raise EV_ASSET, so a plugin never has to time a load out. */
void
PluginHost_AssetDeliver(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* asset_name,
    void* data,
    int size);

/** Returns 1 when a plugin consumed the key. */
int
PluginHost_Key(
    struct ToriRS_PluginHost* host,
    int key,
    int ch,
    bool down);

/** Opens the draw window, dispatches EV_DRAW_WORLD, closes it. */
void
PluginHost_DrawWorld(struct ToriRS_PluginHost* host);

/** The same, for EV_DRAW_CANVAS: a different surface token, a different
 *  overlay list, and the canvas rather than the world viewport as the clip. */
void
PluginHost_DrawCanvas(
    struct ToriRS_PluginHost* host,
    int width,
    int height);

/** Re-resolve all standing role replacement claims. Called before interaction
 * so a reclaimed/rebuilt target can never retain or inherit suppression. */
void
PluginHost_ReconcileRoleReplacements(struct ToriRS_PluginHost* host);

/** The same again, for EV_DRAW_FRAME -- the chrome surface, under the
 *  interfaces. Raised for the frame's owner alone, and not at all when nobody
 *  holds it, so a client with no layout plugin pays one branch. */
void
PluginHost_DrawFrame(
    struct ToriRS_PluginHost* host,
    int width,
    int height);

/**
 * The chrome pass: reconcile the suppressions, then re-ask every claimant
 * whose declaration went stale. @see ToriRS_PluginApi::chrome_claim.
 *
 * Call once a frame, AFTER PluginHost_Layout. That order is the whole of the
 * promise between the two tiers -- the frame's arranger has already stated
 * where everything is, so every box a dresser reads is this pass's rather than
 * the last one's.
 *
 * Per frame rather than per layout because a borrowed image lands off the IO
 * queue with no layout in flight, and a dresser that skipped a pass waiting
 * for pixels has to be asked again once they arrive.
 */
void
PluginHost_ChromeTick(
    struct ToriRS_PluginHost* host,
    int width,
    int height);

/**
 * A requested offer has finished its non-layout preparation and needs one
 * safe candidate-build attempt. This is independent of committed ownership:
 * native or the previous plugin frame remains live until that attempt commits.
 */
bool
PluginHost_FrameNeedsLayout(struct ToriRS_PluginHost const* host);

/**
 * Ask the requested candidate (or committed provider on relayout) to declare
 * against a canvas of `width` x `height`.
 *
 * The engine calls this at the three moments the last declaration stopped
 * being true: the canvas resized, the gameframe was rebuilt, and a selection
 * or explicit invalidation requested another candidate. A no-op with no
 * plugin candidate or committed frame.
 *
 * The two dimensions are ignored for a FIXED offer, which reads its own pinned
 * size back -- see the body.
 */
void
PluginHost_Layout(
    struct ToriRS_PluginHost* host,
    int width,
    int height);

/**
 * Announce that a layout input may have moved.
 *
 * Called by the client when something it owns moved a region -- a resize, a
 * gameframe rebuild -- and by the host itself when a plugin reserves. Legacy
 * EV_LAYOUT_CHANGED remains the broad compatibility notification. V2
 * on_placement_changed is raised separately, only after the canonical area
 * sets and assigned reservation boxes differ from their last complete state.
 * Callback-side changes are coalesced into a later, non-recursive transaction.
 */
void
PluginHost_LayoutChanged(struct ToriRS_PluginHost* host);

/**
 * Deliver a canvas hit region's use, raising EV_CANVAS_CLICK on the plugin
 * that declared it and on no other.
 *
 * `plugin_index` is what the engine recorded beside the region, so a plugin
 * can never be handed another's click even if the two overlap.
 */
void
PluginHost_CanvasClick(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    uint32_t tag,
    int op,
    int x,
    int y);

/** Dispatches EV_MENU_BUILD. `cursor` is handed to engine->menu_add. */
void
PluginHost_MenuBuild(
    struct ToriRS_PluginHost* host,
    void* cursor,
    struct ToriRS_MenuBuildEvent* menu,
    bool hover_pass);

/** True when `action` is a live plugin-owned menu action. */
bool
PluginHost_OwnsMenuAction(
    struct ToriRS_PluginHost const* host,
    int action);

/** Dispatches EV_MENU_SELECT. Returns 1 when the engine's own dispatch should
 *  be suppressed -- always so for a plugin-owned row, and for a native row a
 *  subscriber consumed. */
int
PluginHost_MenuSelect(
    struct ToriRS_PluginHost* host,
    struct ToriRS_MenuRow const* row,
    int click_x,
    int click_y);

/* ------------------------------------------------------------------------ */
/* Config store                                                              */
/* ------------------------------------------------------------------------ */

/** Apply one parsed ini entry. Unknown plugins and keys are remembered so a
 *  script that has not finished loading yet still gets its saved values, and
 *  so a round-trip never drops a section it did not understand. */
void
PluginHost_ConfigApply(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* key,
    char const* value);

/** Decode a whole plugin_prefs.ini image. */
void
PluginHost_ConfigDecode(
    struct ToriRS_PluginHost* host,
    void const* data,
    int size);

/** Encode the store. Keys at their declared default are omitted, matching
 *  RS_Prefs. Caller frees *out_data. Returns 1 on success. */
int
PluginHost_ConfigEncode(
    struct ToriRS_PluginHost const* host,
    void** out_data,
    int* out_size);

/** True when anything has changed since the last encode. */
bool
PluginHost_ConfigDirty(struct ToriRS_PluginHost const* host);
void
PluginHost_ConfigClearDirty(struct ToriRS_PluginHost* host);

/* Direct config access, for the settings panel and for adapters. */
char const*
PluginHost_ConfigGet(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    char const* key);
void
PluginHost_ConfigSet(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* key,
    char const* value);
int
PluginHost_ConfigCount(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
struct ToriRS_ConfigItem const*
PluginHost_ConfigItem(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    int item_index);

/* ---- temporary V1 plugin-window compatibility ----------------------------
 *
 * The host owns this legacy MODEL -- which plugin registered a tab,
 * what controls are on it, what they say -- and owns nothing about how it is
 * presented. Whoever draws it (the settings panel, and through it whichever
 * WEB/BROWSER executor is bound) reads this registry and mirrors it.
 *
 * Kept here rather than in the panel because a plugin's controls have to
 * outlive any particular presentation of them: the window can be closed,
 * reopened or moved between the internal canvas and shared browser page, and the
 * plugin must not have to rebuild its tab for any of that.
 */

/** Window controls across ALL plugins. A shared budget on top of the
 *  per-plugin TORIRS_PLUGIN_WIDGETS_MAX, so sixteen greedy plugins cannot
 *  between them exhaust a fixed-size host. */
#define TORIRS_PLUGIN_WIN_WIDGETS_MAX 256

/** Structured select rows retained for the one active semantic page. */
#define TORIRS_PLUGIN_SELECT_OPTIONS_MAX 128
#define TORIRS_PLUGIN_SELECT_VALUE_MAX TORIRS_PLUGIN_CONFIG_VALUE_MAX
#define TORIRS_PLUGIN_SELECT_LABEL_MAX TORIRS_PLUGIN_CONFIG_VALUE_MAX
#define TORIRS_PLUGIN_SELECT_DETAIL_MAX TORIRS_PLUGIN_CONFIG_VALUE_MAX

struct ToriRS_PluginSelectOption
{
    char value[TORIRS_PLUGIN_SELECT_VALUE_MAX];
    char label[TORIRS_PLUGIN_SELECT_LABEL_MAX];
    char detail[TORIRS_PLUGIN_SELECT_DETAIL_MAX];
    bool enabled;
};

/** One control on a plugin's tab, as the host holds it. */
struct ToriRS_PanelWidget
{
    /** enum ToriRS_PanelWidgetKind. */
    int kind;
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char label[64];
    char text[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    int checked;
    int selected;
    /** "a|b|c" for a dropdown; empty otherwise. */
    char choices[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    /**
     * Lossless V2 select state. `select_options` points into host-owned copied
     * storage and remains valid for this panel selection generation. Legacy
     * dropdowns leave `structured_select` false and continue using choices.
     */
    bool structured_select;
    struct ToriRS_PluginSelectOption* select_options;
    int select_option_count;
    char selected_value[TORIRS_PLUGIN_SELECT_VALUE_MAX];
    /** Generic result state for the ABI-21 semantic kinds. Legacy checkbox
     *  and dropdown adapters keep this in step with checked/selected. */
    int value;
    /** CUSTOM only: preferred logical content height. */
    int preferred_height;
    /** Never reused within one host lifetime. Lets a queued intent distinguish
     *  a removed node from a later declaration with the same string id. */
    uint32_t serial;
};

/** Has this plugin claimed a tab? */
bool
PluginHost_WinHasTab(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/** Its tab's title; "" when it has none. Never NULL. */
char const*
PluginHost_WinTabTitle(
    struct ToriRS_PluginHost const* host,
    int plugin_index);

int
PluginHost_WinWidgetCount(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
struct ToriRS_PanelWidget const*
PluginHost_WinWidgetAt(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    int widget_index);

/**
 * Ask a plugin to declare its controls, if its tab is empty.
 *
 * Raises EV_UI_BUILD. Called by whoever presents the window when it opens, and
 * by the host itself after a reload -- the plugin declares its tab in one
 * place and never has to know which of those happened.
 */
void
PluginHost_WinBuild(
    struct ToriRS_PluginHost* host,
    int plugin_index);

/**
 * Deliver a control's use to the plugin that owns it, updating the host's copy
 * of the control first so a plugin reading its own tab back sees the new value.
 *
 * @param action enum ToriRS_PanelActionKind.
 * @return 1 when the widget was found and the event dispatched.
 */
int
PluginHost_WinDispatch(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* widget_id,
    int action,
    int value,
    char const* text);

/** Drop a plugin's tab and every control on it. Used by disable and reload. */
void
PluginHost_WinClearPlugin(
    struct ToriRS_PluginHost* host,
    int plugin_index);

/**
 * Bumped whenever the registry's SHAPE changes -- a tab claimed or dropped, a
 * control added or removed. A presentation compares it against what it last
 * built and rebuilds only when it differs, which is what keeps a window that
 * nothing has touched from being torn down and reassembled every frame.
 *
 * Value changes (a checkbox toggled, a field edited) do NOT bump it: those are
 * mirrored onto the existing controls, which is the whole reason the chrome
 * has compare-then-set mutators.
 */
int
PluginHost_WinRevision(struct ToriRS_PluginHost const* host);

/* ---- the shared application plugin panel --------------------------------
 *
 * This is the platform-neutral authority boundary. PluginHost owns inert rail
 * registrations, exactly one active plugin model, and the generation checks
 * around work entering that model. A platform/application shell owns where
 * the rail and page are placed and drives these entry points from user
 * selection and queued presenter intents.
 */

/** Rail metadata. Entries exist only for running plugins which called
 *  panel_request from EV_START. */
bool
PluginHost_PanelHasPage(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
char const*
PluginHost_PanelTitle(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
char const*
PluginHost_PanelIconAsset(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
int
PluginHost_PanelPreferredWidth(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
/** Whether the entry has asked to be noticed. A flag, never a caption -- the
 *  rail is a column of icons. @see ToriRS_PluginApi::panel_set_attention. */
bool
PluginHost_PanelWantsAttention(
    struct ToriRS_PluginHost const* host,
    int plugin_index);

/** Revisioned, host-owned icon pixels for the registered rail entry. The
 * accessor never crosses plugin namespaces: `plugin_index` must own both the
 * registration and its automatically loaded image. Source icons are capped
 * at 64x64; 0 means pending, missing, malformed, or intentionally absent and
 * presenters use the baked wrench fallback. */
uint32_t
PluginHost_PanelIconRevision(
    struct ToriRS_PluginHost const* host,
    int plugin_index);
int
PluginHost_PanelIconPixels(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    uint32_t* out_argb,
    int max_pixels,
    int* out_width,
    int* out_height);

/** Changes whenever an entry is added, removed, renamed, rebadged, or changes
 *  attention state. Presenters use it to avoid rebuilding an idle rail. */
uint32_t
PluginHost_PanelRegistryRevision(struct ToriRS_PluginHost const* host);

/** The only plugin whose page is mounted, or -1. */
int
PluginHost_PanelActive(struct ToriRS_PluginHost const* host);
/** Most recently selected registered entry. Unlike PanelActive this survives
 *  an ordinary collapse, so the same icon can expand again. */
int
PluginHost_PanelLastSelected(struct ToriRS_PluginHost const* host);
/** Nonzero and advanced for every active-selection transition. */
uint32_t
PluginHost_PanelSelectionGeneration(struct ToriRS_PluginHost const* host);

/**
 * Handle a rail selection and synchronously build one registered page.
 * Selecting the mounted entry collapses it; selecting while collapsed expands
 * it; selecting another entry replaces it. Returns 1 when the action was
 * accepted and 0 for an absent/stopped entry.
 */
int
PluginHost_PanelSelect(
    struct ToriRS_PluginHost* host,
    int plugin_index);
/**
 * PanelSelect naming which FACE to mount. `view` is enum
 * ToriRS_PanelView; PanelSelect is this with VIEW_PAGE.
 *
 * Asking for the face already mounted collapses, exactly as reselecting the
 * entry does. Asking for the OTHER face of the mounted plugin is a
 * REPLACEMENT and advances the selection generation, because the two faces are
 * two page models and nothing may survive between them.
 */
int
PluginHost_PanelSelectView(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    int view);
/** Which face the mounted page is showing, VIEW_PAGE when nothing is. */
int
PluginHost_PanelView(struct ToriRS_PluginHost const* host);
/** Collapse the page, notifying the old plugin that it became invisible and
 *  retaining PanelLastSelected. Returns 1 when a page was closed. */
int
PluginHost_PanelClose(struct ToriRS_PluginHost* host);

/** Build the active page if it was explicitly cleared. A presenter resync
 *  must read the retained model instead; it does not call this. */
int
PluginHost_PanelEnsureBuilt(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation);

/** Active model reads are generation checked so a sync cannot accidentally
 *  finish against a page which replaced the one it started reading. */
int
PluginHost_PanelWidgetCount(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation);
struct ToriRS_PanelWidget const*
PluginHost_PanelWidgetAt(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation,
    int widget_index);

/** Exact retained properties changed on one mounted panel node. */
enum ToriRS_PluginPanelChangeFlags
{
    TORIRS_PLUGIN_PANEL_CHANGE_TEXT = 1u << 0,
    TORIRS_PLUGIN_PANEL_CHANGE_VALUE = 1u << 1,
    TORIRS_PLUGIN_PANEL_CHANGE_HEIGHT = 1u << 2,
    TORIRS_PLUGIN_PANEL_CHANGE_OPTIONS = 1u << 3,
};

/**
 * One coalesced active-page mutation. `widget_index` is stable for the current
 * selection generation and `widget_serial` fences a structural redeclaration.
 * Values stay in the authoritative widget and are read with PanelWidgetAt.
 */
struct ToriRS_PluginPanelChange
{
    int widget_index;
    uint32_t widget_serial;
    uint32_t flags;
    uint32_t model_revision;
};

/**
 * Pop one exact retained mutation for the active page.
 *
 * Returns 1 for a row, 0 when caught up, and -1 when the generation is stale or
 * a structural declaration requires the caller to rebuild the page. Repeated
 * setters for one node coalesce through a direct per-slot index; this never
 * searches the pending queue.
 */
int
PluginHost_PanelChangeNext(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    struct ToriRS_PluginPanelChange* out);

/** A full page build/recovery consumed all earlier row mutations. */
void
PluginHost_PanelChangesAcknowledge(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation);

/** Instrumentation: exact journal rows popped since host creation. */
uint32_t
PluginHost_PanelChangeVisits(struct ToriRS_PluginHost const* host);
/** Changes on every active model mutation, including result-state changes. */
uint32_t
PluginHost_PanelModelRevision(struct ToriRS_PluginHost const* host);

/** Publish neutral layout facts for the current selection. Returns 0 for a
 *  stale generation or invalid allocation. */
int
PluginHost_PanelLayout(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    int width,
    int height,
    int scale_milli,
    int size_class,
    bool visible,
    bool game_visible);

/** Deliver a copied result-state intent. Both generation and serial must name
 *  the active node; duplicate/stale presenter work therefore cannot reach a
 *  newly selected or redeclared page. */
int
PluginHost_PanelDispatch(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial,
    uint64_t intent_sequence,
    char const* widget_id,
    int action,
    int value,
    char const* text,
    int x,
    int y);

/** Whether a selected custom node is dirty, followed by its scoped draw pass.
 *  The caller prepares `surface` as a panel-local target before dispatch and
 *  restores its renderer afterwards. Draw returns 0 for hidden, clean, stale,
 *  or non-custom nodes. */
bool
PluginHost_PanelNeedsDraw(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation,
    uint32_t widget_serial);
/** Mark an active custom node dirty because presenter geometry changed. */
int
PluginHost_PanelInvalidate(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial);
int
PluginHost_PanelDraw(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial,
    void* surface,
    int x,
    int y,
    int width,
    int height);

#endif /* TORIRS_PLUGIN_HOST_H */
