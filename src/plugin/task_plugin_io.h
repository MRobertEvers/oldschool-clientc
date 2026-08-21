#ifndef TASK_PLUGIN_IO_H
#define TASK_PLUGIN_IO_H

struct ToriRS_Task;
struct ToriRS_PluginHost;

/* Beside preferences.ini, and written the same way. */
#define PLUGIN_PREFS_DEFAULT_PATH "plugin_prefs.ini"
/* Resolved under the script dir by the IO layer, like every other script. */
#define PLUGIN_MANIFEST_DEFAULT_PATH "plugins/plugins.ini"

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

/** Encode the config store now and write it. */
struct ToriRS_Task* CreateTask_PluginSave(
    struct ToriRS_PluginHost const* host,
    char const* path);

#endif /* TASK_PLUGIN_IO_H */
