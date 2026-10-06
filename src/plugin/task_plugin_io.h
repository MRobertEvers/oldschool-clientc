#ifndef TASK_PLUGIN_IO_H
#define TASK_PLUGIN_IO_H

struct ToriRS_Task;
struct ToriRS_PluginHost;

/* Beside preferences.ini, and written the same way. */
#define PLUGIN_PREFS_DEFAULT_PATH "plugin_prefs.ini"
/* Resolved under the script dir by the IO layer, like every other script. */
#define PLUGIN_MANIFEST_DEFAULT_PATH "plugins/plugins.ini"

/*
 * Plugin assets live in two places, and a read tries them in this order.
 *
 * SAVED sits beside plugin_prefs.ini, is read and written as a client FILE,
 * and is where a plugin's own asset_save lands.
 *
 * SHIPPED sits under the script directory beside the scripts themselves, is
 * read as a SCRIPT item, and is where a plugin's author puts the table or the
 * data file the plugin cannot work without. It is not writable -- on the
 * browser lane the script directory is served, not stored.
 *
 * Preferring the saved copy on read is what lets a plugin ship a default and
 * later replace it under the same name.
 */
#define PLUGIN_ASSET_SHIPPED_DIR "plugins/assets"
#define PLUGIN_ASSET_SAVED_DIR "plugin_assets"

/* The scripts manifest the Scripts tab asks for (script/plugins/script_runner.lua
 * through api.drive.tests): resolved under the script dir by the IO layer like
 * the plugin manifest, `[test:<id>]` sections. Written by the launcher
 * (tools/raid_gate/prepare_scripts.py, the osrs239-scripts profile's
 * [derived:tests] block). */
#define TESTS_MANIFEST_DEFAULT_PATH "tests/tests.ini"

/** TORIRS_PLUGIN_PREFS overrides; empty disables persistence entirely. */
char const* PluginPrefs_Path(void);
/** TORIRS_PLUGIN_MANIFEST overrides; empty loads no scripts. */
char const* PluginManifest_Path(void);

/** TORIRS_TESTS_MANIFEST overrides (a script-dir-relative path, the
 *  TORIRS_PLUGIN_MANIFEST convention); never NULL. */
char const* TestsManifest_Path(void);

/** Read the manifest, register each script, decode saved settings, then start
 *  every enabled plugin. One task because the order between those steps is
 *  load-bearing -- see the file comment. */
struct ToriRS_Task* CreateTask_PluginBoot(
    struct ToriRS_PluginHost* host,
    char const* manifest_path,
    char const* prefs_path);

/** Read one plugin asset: the saved copy if there is one, else the shipped
 *  one. Calls PluginHost_AssetDeliver either way -- with the bytes, or with
 *  NULL when neither exists. `saved_path` is the fully composed client-file
 *  path (see App's asset seam); the shipped path is derived from the plugin
 *  and asset names. */
struct ToriRS_Task* CreateTask_PluginAssetRead(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* asset_name,
    char const* saved_path);

/** Write one plugin asset to its saved path. `data` is COPIED. */
struct ToriRS_Task* CreateTask_PluginAssetWrite(
    char const* saved_path,
    void const* data,
    int size);

/**
 * Where a script read lands. `data` is the file's bytes (OWNED by the callee,
 * which frees them) or NULL when there is no such script; `size` is 0 then.
 * `serial` is the caller's own, handed back so a reply to a superseded request
 * can be told from the current one.
 */
typedef void (*PluginScriptReadDeliver)(
    void* user,
    int serial,
    char const* path,
    void* data,
    int size);

/** Read one SCRIPT item -- a path under the script dir, exactly as the plugin
 *  manifest and the plugin sources are read -- and hand it to `deliver`.
 *  Every call is a fresh read of the file: nothing is cached, which is what
 *  makes a test source edited between two Plays the one the second Play runs
 *  (the Scripts tab, api.drive.play). */
struct ToriRS_Task* CreateTask_PluginScriptRead(
    char const* path,
    int serial,
    PluginScriptReadDeliver deliver,
    void* user);

/**
 * One request to the launch service of this client's embedded IO server (raid
 * seam37; src/platform/launch_sessions.h): a SCRIPT item at `launch/<verb>`
 * lending `body` (`size` bytes of key=value lines, COPIED here) to the
 * executor (platform_x_io.c answer_launch_item). `deliver` gets the answer
 * text (OWNED by the callee) -- `ok ...`, `error: ...` or `unsupported: ...`
 * -- or NULL when the executor had no answer at all (an executor that does
 * not serve launch items: the caller says "unsupported").
 */
struct ToriRS_Task* CreateTask_PluginLaunch(
    char const* verb,
    char const* body,
    int size,
    int serial,
    PluginScriptReadDeliver deliver,
    void* user);

/** Encode the config store now and write it. */
struct ToriRS_Task* CreateTask_PluginSave(
    struct ToriRS_PluginHost const* host,
    char const* path);

#endif /* TASK_PLUGIN_IO_H */
