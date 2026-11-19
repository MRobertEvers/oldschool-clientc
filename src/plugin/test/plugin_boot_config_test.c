/*
 * What this boot handed the plugin host (plugin/plugin_boot_config.h).
 *
 * The switch and the settings path read back as stated, a zeroed config has
 * user plugins off and persistence off, and the feature snapshot is taken by
 * the first capture only: a later capture, after a plugin has written over
 * the live table, leaves the boot's values standing.
 *
 * Run: make -C src test-plugin-boot-config
 */

#include "plugin/plugin_boot_config.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_switch_and_path(void)
{
    struct PluginBootConfig cfg;
    char const* path = "/tmp/plugins/prefs.ini";

    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT(PluginBootConfig_UserPlugins(&cfg) == 0, "zeroed: user plugins off");
    TEST_ASSERT(PluginBootConfig_PrefsPath(&cfg) == NULL, "zeroed: persistence off");
    TEST_ASSERT(!PluginBootConfig_FeaturesCaptured(&cfg), "zeroed: no snapshot");

    PluginBootConfig_SetUserPlugins(&cfg, 1);
    PluginBootConfig_SetPrefsPath(&cfg, path);
    TEST_ASSERT(PluginBootConfig_UserPlugins(&cfg) == 1, "user plugins on");
    TEST_ASSERT(PluginBootConfig_PrefsPath(&cfg) == path, "the path stated");

    PluginBootConfig_SetUserPlugins(&cfg, 0);
    PluginBootConfig_SetPrefsPath(&cfg, NULL);
    TEST_ASSERT(PluginBootConfig_UserPlugins(&cfg) == 0, "user plugins off again");
    TEST_ASSERT(PluginBootConfig_PrefsPath(&cfg) == NULL, "persistence off again");
}

static void
test_first_capture_wins(void)
{
    struct PluginBootConfig cfg;
    struct ToriRS_FeatureTable live;
    struct RevConfigCameraItem camera;

    memset(&cfg, 0, sizeof(cfg));
    memset(&live, 0, sizeof(live));
    memset(&camera, 0, sizeof(camera));
    live.painter_draw_distance = 25;
    live.player_head_light_ambient = 128;
    camera.rest = 600;
    camera.controls = 3;

    TEST_ASSERT(PluginBootConfig_CaptureFeatures(&cfg, &live, &camera), "the first capture takes it");
    TEST_ASSERT(PluginBootConfig_FeaturesCaptured(&cfg), "captured");

    /* A plugin writes over the live values; the boot's must not follow. */
    live.painter_draw_distance = 90;
    live.player_head_light_ambient = 0;
    camera.rest = 300;
    camera.controls = 0;
    TEST_ASSERT(!PluginBootConfig_CaptureFeatures(&cfg, &live, &camera), "a later capture is a no-op");

    TEST_ASSERT(
        PluginBootConfig_FeatureTable(&cfg)->painter_draw_distance == 25, "the boot's draw distance");
    TEST_ASSERT(
        PluginBootConfig_FeatureTable(&cfg)->player_head_light_ambient == 128, "the boot's head light");
    TEST_ASSERT(PluginBootConfig_FeatureCamera(&cfg)->rest == 600, "the boot's camera rest");
    TEST_ASSERT(PluginBootConfig_FeatureCamera(&cfg)->controls == 3, "the boot's camera controls");
}

int
main(void)
{
    test_switch_and_path();
    test_first_capture_wins();
    if( g_failures )
    {
        printf("plugin_boot_config_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_boot_config_test: ok\n");
    return 0;
}
