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

/** TORIRS_PLUGIN_PREFS overrides; empty disables persistence entirely. */
char const* PluginPrefs_Path(void);
/** TORIRS_PLUGIN_MANIFEST overrides; empty loads no scripts. */
char const* PluginManifest_Path(void);

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

/** Encode the config store now and write it. */
struct ToriRS_Task* CreateTask_PluginSave(
    struct ToriRS_PluginHost const* host,
    char const* path);

#endif /* TASK_PLUGIN_IO_H */
