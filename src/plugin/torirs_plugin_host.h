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
#define TORIRS_PLUGIN_CONFIG_MAX 24
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

    int (*key_held)(void* user, int keycode);
    int (*hover_tile)(void* user, int* out_tile_x, int* out_tile_z, int* out_level);

    int (*project)(
        void* user,
        int fine_x,
        int fine_z,
        int height_above_ground,
        int* out_x,
        int* out_y);

    /* Drawing. Each returns the number of overlay items it pushed, so the host
     * can hold a plugin to its per-frame budget. */
    int (*draw_tile)(
        void* user,
        int tile_x,
        int tile_z,
        int level,
        uint32_t rgb,
        int fill_alpha);
    int (*draw_hull)(void* user, int element_id, uint32_t rgb, int fill_alpha);
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
bool PluginHost_IsEnabled(struct ToriRS_PluginHost const* host, int plugin_index);
int PluginHost_Count(struct ToriRS_PluginHost const* host);
char const* PluginHost_Name(struct ToriRS_PluginHost const* host, int plugin_index);
/** Last error text for a plugin, or NULL. Shown in the settings panel. */
char const* PluginHost_Error(struct ToriRS_PluginHost const* host, int plugin_index);
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

#endif /* TORIRS_PLUGIN_HOST_H */
