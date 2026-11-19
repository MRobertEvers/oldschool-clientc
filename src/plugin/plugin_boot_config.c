#include "plugin/plugin_boot_config.h"

#include <assert.h>

void
PluginBootConfig_SetUserPlugins(
    struct PluginBootConfig* cfg,
    int on)
{
    assert(cfg);
    cfg->user_plugins = on;
}

int
PluginBootConfig_UserPlugins(struct PluginBootConfig const* cfg)
{
    assert(cfg);
    return cfg->user_plugins;
}

void
PluginBootConfig_SetPrefsPath(
    struct PluginBootConfig* cfg,
    char const* path)
{
    assert(cfg);
    cfg->prefs_path = path;
}

char const*
PluginBootConfig_PrefsPath(struct PluginBootConfig const* cfg)
{
    assert(cfg);
    return cfg->prefs_path;
}

bool
PluginBootConfig_CaptureFeatures(
    struct PluginBootConfig* cfg,
    struct ToriRS_FeatureTable const* table,
    struct RevConfigCameraItem const* camera)
{
    assert(cfg);
    assert(table);
    assert(camera);

    if( cfg->feature_boot_valid )
        return false;
    cfg->feature_boot = *table;
    cfg->feature_boot_camera = *camera;
    cfg->feature_boot_valid = true;
    return true;
}

bool
PluginBootConfig_FeaturesCaptured(struct PluginBootConfig const* cfg)
{
    assert(cfg);
    return cfg->feature_boot_valid;
}

struct ToriRS_FeatureTable*
PluginBootConfig_FeatureTable(struct PluginBootConfig* cfg)
{
    assert(cfg);
    assert(cfg->feature_boot_valid);
    return &cfg->feature_boot;
}

struct RevConfigCameraItem*
PluginBootConfig_FeatureCamera(struct PluginBootConfig* cfg)
{
    assert(cfg);
    assert(cfg->feature_boot_valid);
    return &cfg->feature_boot_camera;
}
