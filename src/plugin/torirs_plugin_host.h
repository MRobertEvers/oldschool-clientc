#ifndef TORIRS_PLUGIN_HOST_H
#define TORIRS_PLUGIN_HOST_H

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

#include "plugin/torirs_plugin.h"

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
/** Resident assets, across every plugin. */
#define TORIRS_PLUGIN_ASSETS_MAX 32
/** Resident IMAGES, across every plugin. Each holds a decoded ARGB sprite in
 *  the scene, so the ceiling is what keeps a plugin from filling the scene's
 *  sprite table with art nothing draws. */
#define TORIRS_PLUGIN_IMAGES_MAX 64
/** Longest screenshot destination a plugin may name, including separators. */
#define TORIRS_PLUGIN_SCREENSHOT_DIR_MAX 192

struct ToriRS_PluginHost;

/* ------------------------------------------------------------------------ */
/* The engine seam. app.c implements every one of these.                     */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginEngine
{
    /** struct App*. */
    void* user;

    int (*world_cycle)(void* user);
    uint64_t (*frame_ms)(void* user);

    int (*local_player)(void* user, struct ToriRS_PluginPlayerSnap* out);
    int (*npc_next)(void* user, int iter, struct ToriRS_PluginNpcSnap* out);
    int (*npc_by_slot)(void* user, int server_slot, struct ToriRS_PluginNpcSnap* out);
    int (*player_next)(void* user, int iter, struct ToriRS_PluginPlayerSnap* out);
    int (*obj_next)(void* user, int iter, struct ToriRS_PluginObjSnap* out);
    int (*loc_next)(void* user, int iter, struct ToriRS_PluginLocSnap* out);
    int (*highlight_next)(void* user, int iter, struct ToriRS_PluginHighlightItem* out);

    /** One game line into the chatbox. @see ToriRS_PluginApi::notify. */
    void (*notify)(void* user, char const* text);

    int (*key_held)(void* user, int keycode);
    int (*hover_tile)(void* user, int* out_tile_x, int* out_tile_z, int* out_level);
    int (*hover_entity)(void* user, struct ToriRS_PluginHoverEntity* out);
    int (*element_height)(void* user, int element_id);

    /** The pointer, in canvas coords. @see ToriRS_PluginApi::mouse_pos. */
    int (*mouse_pos)(void* user, int* out_x, int* out_y);
    /** The minimap's box this frame. @see ToriRS_PluginApi::minimap_rect. */
    int (*minimap_rect)(void* user, int* out_x, int* out_y, int* out_w, int* out_h);
    /** One skill's boosted and base level. @see ToriRS_PluginApi::stat. */
    int (*stat)(void* user, int skill, int* out_current, int* out_base);
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
    char const* (*skill_name)(void* user, int skill);
    /** Run energy, 0..100. */
    int (*run_energy)(void* user);

    /** The client's live var state, read-only. @see ToriRS_PluginApi::varbit. */
    int (*varbit)(void* user, int varbit_id);
    int (*varp)(void* user, int varp_id);

    /** The boot profile's `[<kind>:<name>] id=` table. @see ToriRS_PluginApi::cache_id. */
    int (*cache_id)(void* user, char const* kind, char const* name);

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
    void (*draw_select_canvas)(void* user, int canvas);

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
    int (*image_read)(void* user, int slot, uint32_t* out, int max);
    /** Drop a published image. Idempotent. */
    void (*image_release)(void* user, int slot);
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
    int (*if_click)(void* user, int component_id, int op);
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
    int (*draw_hull)(void* user, int element_id, uint32_t rgb, int fill_alpha, int shape);
    int (*draw_line)(void* user, int x0, int y0, int x1, int y1, uint32_t rgb);
    int (*draw_text)(void* user, int x, int y, char const* text, uint32_t rgb);
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
    int (*menu_add)(void* user, void* cursor, char const* text, int action_id);

    /* Assets. The engine owns the paths and the IO queue; the host owns the
     * resident bytes and who is allowed to see them, which is why `plugin` is
     * a name and not an index -- it is a directory component. */

    /** Queue a read. The engine calls PluginHost_AssetDeliver when it lands,
     *  with the bytes or with NULL. Returns 1 when the read was queued. */
    int (*asset_read)(void* user, char const* plugin, char const* name);
    /** Queue a write. `data` is COPIED by the engine: the host's resident copy
     *  is replaced the moment asset_save returns and cannot be borrowed for
     *  the lifetime of an async write. */
    int (*asset_write)(void* user, char const* plugin, char const* name, void const* data, int size);

    /** Record a deferred frame capture. `dir` is NULL or "" for the plugin's
     *  own saved-asset directory. Returns 1 when the request was accepted; the
     *  engine takes it at the end of the frame and writes the PNG itself. */
    int (*screenshot)(void* user, char const* plugin, char const* dir, char const* name);

    /* World objects. Handles are the engine's; the host records who owns each
     * one so a stopped plugin's objects leave the scene with it. */

    int (*object_create)(void* user);
    void (*object_destroy)(void* user, int object);
    void (*object_set_model)(void* user, int object, int source, int id);
    void (*object_recolor)(void* user, int object, int hsl_from, int hsl_to);
    void (*object_clear_recolors)(void* user, int object);
    void (*object_set_anim)(void* user, int object, int seq_id, int loop);
    void (*object_set_light)(void* user, int object, int ambient, int contrast);
    void (*object_set_position)(
        void* user,
        int object,
        int tile_x,
        int tile_z,
        int level,
        int height,
        int yaw);
    void (*object_set_active)(void* user, int object, int active);
    int (*object_ready)(void* user, int object);

    int (*hsl_from_rgb)(void* user, uint32_t rgb);
    uint32_t (*hsl_to_rgb)(void* user, int hsl);
};

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/** Allocates and registers every statically linked plugin. `engine` is copied.
 *  Plugins are not started here -- PluginHost_Start does that, once the config
 *  store has been loaded. */
struct ToriRS_PluginHost* PluginHost_New(struct ToriRS_PluginEngine const* engine);

/** Fires EV_STOP on everything still running, in reverse start order. */
void PluginHost_Free(struct ToriRS_PluginHost* host);

/** Register one plugin. `def` must outlive the host (it is held by pointer,
 *  the way every static plugin def is a file-scope constant). Returns the
 *  plugin index, or -1 when the table is full. */
int PluginHost_Register(struct ToriRS_PluginHost* host, struct ToriRS_PluginDef const* def);

/** Runs `init` and fires EV_START for every enabled plugin that is not yet
 *  running. Idempotent, so a late-registered plugin (a script that finished
 *  loading) is started by calling this again. */
void PluginHost_Start(struct ToriRS_PluginHost* host);

/** Enable/disable at runtime: the settings-panel checkbox, and how a faulting
 *  script is taken out of the frame. Fires EV_START/EV_STOP and drops the
 *  plugin's subscriptions, menu routes and draw budget. */
void PluginHost_SetEnabled(struct ToriRS_PluginHost* host, int plugin_index, bool enabled);

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
void PluginHost_Reload(struct ToriRS_PluginHost* host, int plugin_index);
bool PluginHost_IsEnabled(struct ToriRS_PluginHost const* host, int plugin_index);
int PluginHost_Count(struct ToriRS_PluginHost const* host);
/** The plugin's identity: the ini section, the manifest entry, the key
 *  PluginHost_IndexOf answers to. Never what a person is shown -- use
 *  PluginHost_Title for that. */
char const* PluginHost_Name(struct ToriRS_PluginHost const* host, int plugin_index);
/**
 * The plugin's human name: "Entity Highlighter", for the roster and the page
 * header.
 *
 * Never empty and never NULL: a def that declared no title gets one derived
 * from its name, so no caller has to carry a fallback of its own and no panel
 * can end up printing a slug. It is a LABEL -- nothing may key off it, and it
 * may change between two runs of the same plugin.
 */
char const* PluginHost_Title(struct ToriRS_PluginHost const* host, int plugin_index);
/** Last error text for a plugin, or NULL. Shown in the settings panel. */
char const* PluginHost_Error(struct ToriRS_PluginHost const* host, int plugin_index);
/** Does this plugin exist to host others? @see ToriRS_PluginDef::adapter --
 *  the settings roster is the only caller, and it reads it to decide whether
 *  the row is worth showing. */
bool PluginHost_IsAdapter(struct ToriRS_PluginHost const* host, int plugin_index);
/** Is this a builtin, whose switch lives in the cache's All Settings panel?
 *  @see ToriRS_PluginDef::hidden. The roster is the only caller. */
bool PluginHost_IsHidden(struct ToriRS_PluginHost const* host, int plugin_index);
void PluginHost_SetError(struct ToriRS_PluginHost* host, int plugin_index, char const* text);
int PluginHost_IndexOf(struct ToriRS_PluginHost const* host, char const* name);

/** The api table handed to plugins. Adapters need it to forward calls. */
struct ToriRS_PluginApi const* PluginHost_Api(struct ToriRS_PluginHost const* host);
/** The ctx for one plugin, for an adapter that drives a plugin directly. */
struct ToriRS_PluginCtx* PluginHost_Ctx(struct ToriRS_PluginHost* host, int plugin_index);
/** The plugin index behind a ctx. A language adapter registers one plugin per
 *  script but shares one `init` function between them, so this is how that
 *  init works out which script it was handed. */
int PluginHost_CtxIndex(struct ToriRS_PluginCtx const* ctx);

/* ------------------------------------------------------------------------ */
/* Seam entry points. Each is a no-op when nothing subscribed.               */
/* ------------------------------------------------------------------------ */

void PluginHost_FrameStart(struct ToriRS_PluginHost* host, uint64_t now_ms);
void PluginHost_LogicTick(struct ToriRS_PluginHost* host, int logic_cycle);
void PluginHost_ServerTick(struct ToriRS_PluginHost* host, int world_cycle);
void PluginHost_WorldLoaded(struct ToriRS_PluginHost* host, int base_tile_x, int base_tile_z);

void PluginHost_NpcSpawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginNpcSnap const* npc);
void PluginHost_NpcRetype(struct ToriRS_PluginHost* host, struct ToriRS_PluginNpcSnap const* npc);
void PluginHost_NpcDespawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginNpcSnap const* npc);

/**
 * An All Settings row was used: the panel's apply hub just named `setting_id`.
 *
 * `value` is -1 for the rows whose hub carries none -- every toggle, and the
 * buttons this exists for.
 */
void PluginHost_Setting(struct ToriRS_PluginHost* host, int setting_id, int value);

/** A chat line landed. `sender` may be NULL for a system message. */
void PluginHost_ChatMessage(
    struct ToriRS_PluginHost* host,
    int type,
    char const* sender,
    char const* text);

/** A recognised notable moment. `kind` is the recogniser's stable name and
 *  must not be NULL; `subject` and `text` may be. */
void PluginHost_GameEvent(
    struct ToriRS_PluginHost* host,
    char const* kind,
    char const* subject,
    int value,
    char const* text);

void PluginHost_ObjSpawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginObjSnap const* obj);
void PluginHost_ObjCount(struct ToriRS_PluginHost* host, struct ToriRS_PluginObjSnap const* obj);
void PluginHost_ObjDespawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginObjSnap const* obj);

/** An engine-side asset read finished. `data` is HANDED OVER on success (the
 *  host frees it); NULL means the read failed, and the plugin is told so. Both
 *  outcomes raise EV_ASSET, so a plugin never has to time a load out. */
void PluginHost_AssetDeliver(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* asset_name,
    void* data,
    int size);

/** Returns 1 when a plugin asked for the packet to be dropped. */
int PluginHost_PacketIn(struct ToriRS_PluginHost* host, int packet_name, int size);
/** Returns 1 when a plugin vetoed the send. Called BEFORE the builder runs, so
 *  a veto never advances the outbound ISAAC stream. `builder_expr` is the
 *  stringified call; the host trims it to the leading identifier. */
int PluginHost_PacketOutVeto(struct ToriRS_PluginHost* host, char const* builder_expr);

/** Returns 1 when a plugin consumed the key. */
int PluginHost_Key(struct ToriRS_PluginHost* host, int key, int ch, bool down);

/** Opens the draw window, dispatches EV_DRAW_WORLD, closes it. */
void PluginHost_DrawWorld(struct ToriRS_PluginHost* host);

/** The same, for EV_DRAW_CANVAS: a different surface token, a different
 *  overlay list, and the canvas rather than the world viewport as the clip. */
void PluginHost_DrawCanvas(struct ToriRS_PluginHost* host, int width, int height);

/**
 * Deliver a canvas hit region's use, raising EV_CANVAS_CLICK on the plugin
 * that declared it and on no other.
 *
 * `plugin_index` is what the engine recorded beside the region, so a plugin
 * can never be handed another's click even if the two overlap.
 */
void PluginHost_CanvasClick(
    struct ToriRS_PluginHost* host, int plugin_index, uint32_t tag, int op, int x, int y);

/** Dispatches EV_MENU_BUILD. `cursor` is handed to engine->menu_add. */
void PluginHost_MenuBuild(
    struct ToriRS_PluginHost* host,
    void* cursor,
    struct ToriRS_PluginEvMenuBuild* menu,
    bool hover_pass);

/** True when `action` is a live plugin-owned menu action. */
bool PluginHost_OwnsMenuAction(struct ToriRS_PluginHost const* host, int action);

/** Dispatches EV_MENU_SELECT. Returns 1 when the engine's own dispatch should
 *  be suppressed -- always so for a plugin-owned row, and for a native row a
 *  subscriber consumed. */
int PluginHost_MenuSelect(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginMenuRow const* row,
    int click_x,
    int click_y);

/* ------------------------------------------------------------------------ */
/* Config store                                                              */
/* ------------------------------------------------------------------------ */

/** Apply one parsed ini entry. Unknown plugins and keys are remembered so a
 *  script that has not finished loading yet still gets its saved values, and
 *  so a round-trip never drops a section it did not understand. */
void PluginHost_ConfigApply(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* key,
    char const* value);

/** Decode a whole plugin_prefs.ini image. */
void PluginHost_ConfigDecode(struct ToriRS_PluginHost* host, void const* data, int size);

/** Encode the store. Keys at their declared default are omitted, matching
 *  RS_Prefs. Caller frees *out_data. Returns 1 on success. */
int PluginHost_ConfigEncode(
    struct ToriRS_PluginHost const* host,
    void** out_data,
    int* out_size);

/** True when anything has changed since the last encode. */
bool PluginHost_ConfigDirty(struct ToriRS_PluginHost const* host);
void PluginHost_ConfigClearDirty(struct ToriRS_PluginHost* host);

/* Direct config access, for the settings panel and for adapters. */
char const* PluginHost_ConfigGet(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    char const* key);
void PluginHost_ConfigSet(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* key,
    char const* value);
int PluginHost_ConfigCount(struct ToriRS_PluginHost const* host, int plugin_index);
struct ToriRS_PluginConfigItem const* PluginHost_ConfigItem(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    int item_index);

/* ---- the plugin window ---------------------------------------------------
 *
 * The host owns the MODEL of the shared window -- which plugin claimed a tab,
 * what controls are on it, what they say -- and owns nothing about how it is
 * presented. Whoever draws it (the settings panel, and through it whichever
 * chrome executor is bound) reads this registry and mirrors it.
 *
 * Kept here rather than in the panel because a plugin's controls have to
 * outlive any particular presentation of them: the window can be closed,
 * reopened, moved from the canvas to an OS window or a browser tab, and the
 * plugin must not have to rebuild its tab for any of that.
 */

/** Window controls across ALL plugins. A shared budget on top of the
 *  per-plugin TORIRS_PLUGIN_WIDGETS_MAX, so sixteen greedy plugins cannot
 *  between them exhaust a fixed-size host. */
#define TORIRS_PLUGIN_WIN_WIDGETS_MAX 256

/** One control on a plugin's tab, as the host holds it. */
struct ToriRS_PluginWinWidget
{
    /** enum ToriRS_PluginWidgetKind. */
    int kind;
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char label[64];
    char text[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    int checked;
    int selected;
    /** "a|b|c" for a dropdown; empty otherwise. */
    char choices[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
};

/** Has this plugin claimed a tab? */
bool PluginHost_WinHasTab(struct ToriRS_PluginHost const* host, int plugin_index);
/** Its tab's title; "" when it has none. Never NULL. */
char const* PluginHost_WinTabTitle(struct ToriRS_PluginHost const* host, int plugin_index);

int PluginHost_WinWidgetCount(struct ToriRS_PluginHost const* host, int plugin_index);
struct ToriRS_PluginWinWidget const* PluginHost_WinWidgetAt(
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
void PluginHost_WinBuild(struct ToriRS_PluginHost* host, int plugin_index);

/**
 * Deliver a control's use to the plugin that owns it, updating the host's copy
 * of the control first so a plugin reading its own tab back sees the new value.
 *
 * @param action enum ToriRS_PluginUiAction.
 * @return 1 when the widget was found and the event dispatched.
 */
int PluginHost_WinDispatch(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* widget_id,
    int action,
    int value,
    char const* text);

/** Drop a plugin's tab and every control on it. Used by disable and reload. */
void PluginHost_WinClearPlugin(struct ToriRS_PluginHost* host, int plugin_index);

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
int PluginHost_WinRevision(struct ToriRS_PluginHost const* host);

#endif /* TORIRS_PLUGIN_HOST_H */
