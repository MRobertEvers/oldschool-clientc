#ifndef SRC_PLUGIN_PLUGIN_BOOT_CONFIG_H
#define SRC_PLUGIN_PLUGIN_BOOT_CONFIG_H

/*
 * What this boot handed the plugin host: whether the USER plugins are on,
 * where plugin settings are written, and this boot's own feature values as
 * they stood before any plugin set one.
 *
 * The user plugins switch is TORIRS_PLUGINS, else `[ui:boot] plugins=`. Off,
 * the host carries only the nxt-* builtins and the boot task reads no script
 * manifest. A NULL settings path turns persistence off.
 *
 * The feature snapshot is what the Feature Flags plugin's "Revision default"
 * restores TO: the merge of the era table, the manifest's `[features:boot]`,
 * the env overrides and the revconfig, which nothing outside App_Init can
 * reconstruct once a plugin has written over it. It is taken once, on the
 * first call that asks for it, and every later capture is a no-op.
 *
 * This does not read the environment or the manifest, resolve a path, or
 * know when the feature table is final. The caller states the switch and the
 * path when it builds the host, and captures the snapshot at a moment the
 * table is resolved.
 */

#include "features/features.h"
#include "revconfig/revconfig.h"

#include <stdbool.h>

struct PluginBootConfig
{
    int user_plugins;
    char const* prefs_path;
    struct ToriRS_FeatureTable feature_boot;
    struct RevConfigCameraItem feature_boot_camera;
    bool feature_boot_valid;
};

/** The user plugins switch, nonzero for on. Stored as given. */
void
PluginBootConfig_SetUserPlugins(
    struct PluginBootConfig* cfg,
    int on);

int
PluginBootConfig_UserPlugins(struct PluginBootConfig const* cfg);

/** Where plugin settings are written; NULL turns persistence off. */
void
PluginBootConfig_SetPrefsPath(
    struct PluginBootConfig* cfg,
    char const* path);

char const*
PluginBootConfig_PrefsPath(struct PluginBootConfig const* cfg);

/**
 * Snapshot this boot's feature table and camera item, unless a snapshot was
 * already taken. Returns true when this call took it.
 */
bool
PluginBootConfig_CaptureFeatures(
    struct PluginBootConfig* cfg,
    struct ToriRS_FeatureTable const* table,
    struct RevConfigCameraItem const* camera);

bool
PluginBootConfig_FeaturesCaptured(struct PluginBootConfig const* cfg);

/** The snapshot's feature table. Must have been captured. */
struct ToriRS_FeatureTable*
PluginBootConfig_FeatureTable(struct PluginBootConfig* cfg);

/** The snapshot's camera item. Must have been captured. */
struct RevConfigCameraItem*
PluginBootConfig_FeatureCamera(struct PluginBootConfig* cfg);

#endif
