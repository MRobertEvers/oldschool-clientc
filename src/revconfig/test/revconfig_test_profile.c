#include "revconfig_profile.h"
#include "test_harness.h"

#include <string.h>

/** Build the items one INI body describes and merge them into `profile`. */
static void
merge_ini(
    struct RevConfigProfile* profile,
    char const* ini)
{
    struct RevConfigBuffer* fields = revconfig_buffer_new(64);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);

    TEST_ASSERT(fields && items, "alloc");
    revconfig_load_fields_from_ini_bytes((const uint8_t*)ini, (uint32_t)strlen(ini), fields);
    revconfig_items_build(fields, items);
    RevConfigProfile_AddItems(profile, items);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

void
test_profile(void)
{
    printf("TEST: profile\n");

    /* A profile that states nothing is the camera this tree shipped with, and
     * no feature stated at all. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        TEST_ASSERT(profile.features.era[0] == '\0', "era unstated");
        TEST_ASSERT(profile.features.ground_click_unbounded == -1, "unbounded unstated");
        TEST_ASSERT(profile.features.ground_click_offmap == -1, "offmap unstated");
        TEST_ASSERT(
            profile.camera.zoom_mode == REVCONFIG_CAMERA_ZOOM_CLAMPED, "default zoom clamped");
        TEST_ASSERT(
            profile.camera.zoom_min == REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN, "default zoom min");
        TEST_ASSERT(
            profile.camera.zoom_max == REVCONFIG_CAMERA_ZOOM_DEFAULT_MAX, "default zoom max");
        TEST_ASSERT(
            profile.camera.controls ==
                (REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS),
            "default controls both");
        TEST_ASSERT(
            profile.camera.wheel_step == REVCONFIG_CAMERA_WHEEL_STEP_DEFAULT,
            "default wheel step");
    }

    /* wheel_step= is its own key: a section that states only it keeps the
     * default band, and a later source can override it alone. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\nzoom=clamped:[300,1200]\nwheel_step=25\n");
        TEST_ASSERT(profile.camera.wheel_step == 25, "wheel step stated");
        TEST_ASSERT(profile.camera.zoom_min == 300, "band stated beside it");
        merge_ini(&profile, "[camera]\nwheel_step=100\n");
        TEST_ASSERT(profile.camera.wheel_step == 100, "wheel step overridden");
        TEST_ASSERT(profile.camera.zoom_min == 300, "band survives wheel step override");
        TEST_ASSERT(profile.camera.zoom_max == 1200, "band max survives");
        merge_ini(&profile, "[camera]\nzoom=clamped:[240,2160]\n");
        TEST_ASSERT(profile.camera.wheel_step == 100, "wheel step survives zoom override");
    }

    /* The 2004 camera: pinned eye height, arrow keys only. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[camera]\n"
            "zoom=fixed:600\n"
            "controls=arrow_keys\n");
        TEST_ASSERT(profile.camera.zoom_mode == REVCONFIG_CAMERA_ZOOM_FIXED, "fixed zoom");
        TEST_ASSERT(profile.camera.zoom_height == 600, "fixed height");
        TEST_ASSERT(
            profile.camera.controls == REVCONFIG_CAMERA_CONTROL_ARROW_KEYS, "arrow keys only");
        TEST_ASSERT(RevConfigProfile_CameraClampHeight(&profile, 2000) == 600, "fixed clamps");
        TEST_ASSERT(RevConfigProfile_CameraClampHeight(&profile, 100) == 600, "fixed clamps low");
    }

    /* Later source wins PER KEY: overriding controls must not put the zoom back
     * to its default. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\nzoom=fixed:600\ncontrols=arrow_keys\n");
        merge_ini(&profile, "[camera]\ncontrols=mmb,arrow_keys\n");
        TEST_ASSERT(profile.camera.zoom_mode == REVCONFIG_CAMERA_ZOOM_FIXED, "zoom survives");
        TEST_ASSERT(profile.camera.zoom_height == 600, "zoom height survives");
        TEST_ASSERT(
            profile.camera.controls ==
                (REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS),
            "controls overridden");
    }

    /* Same rule for [features], and an unstated key stays unstated. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[features]\nera=osrs\nmover=frame\npainter_draw_distance=32\n");
        merge_ini(&profile, "[features]\nera=server_routed\n");
        TEST_ASSERT(strcmp(profile.features.era, "server_routed") == 0, "era overridden");
        TEST_ASSERT(strcmp(profile.features.mover, "frame") == 0, "mover survives");
        TEST_ASSERT(profile.features.painter_draw_distance == 32, "draw distance survives");
        TEST_ASSERT(
            profile.features.ground_click_nearest[0] == '\0', "nearest still unstated");
    }

    /* No `[chrome]` block is a revision with no strip: nothing to mount the
     * plugin button in, and no geometry invented for one. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        TEST_ASSERT(profile.chrome.plugin_iface[0] == '\0', "chrome iface unstated");
        TEST_ASSERT(profile.chrome.plugin_button_parent == -1, "button parent unstated");
        TEST_ASSERT(profile.chrome.plugin_panel_parent == -1, "panel parent unstated");
        TEST_ASSERT(profile.chrome.plugin_button_slot == -1, "button slot unstated");
        TEST_ASSERT(profile.chrome.plugin_button_size == -1, "button size unstated");
        TEST_ASSERT(profile.chrome.plugin_button_pitch == -1, "button pitch unstated");
        TEST_ASSERT(profile.chrome.plugin_layout_script[0] == '\0', "layout script unstated");
        TEST_ASSERT(profile.chrome.plugin_button_op[0] == '\0', "button op unstated");
    }

    /* The rev-239 mount, whole -- and slot 0 is a real slot, so it has to
     * survive a merge that keys off "unstated". */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[chrome]\n"
            "plugin_button_iface=plugin_popout\n"
            "plugin_button_parent=6\n"
            "plugin_panel_parent=9\n"
            "plugin_button_slot=0\n"
            "plugin_button_size=30\n"
            "plugin_button_pitch=36\n"
            "plugin_button_op=Show Plugin Settings\n"
            "plugin_layout_script=plugin_popout_layout\n");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_iface, "plugin_popout") == 0, "chrome iface stated");
        TEST_ASSERT(profile.chrome.plugin_button_parent == 6, "button parent stated");
        TEST_ASSERT(profile.chrome.plugin_panel_parent == 9, "panel parent stated");
        TEST_ASSERT(profile.chrome.plugin_button_slot == 0, "slot zero stated");
        TEST_ASSERT(profile.chrome.plugin_button_size == 30, "button size stated");
        TEST_ASSERT(profile.chrome.plugin_button_pitch == 36, "button pitch stated");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_button_op, "Show Plugin Settings") == 0,
            "button op stated");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_layout_script, "plugin_popout_layout") == 0,
            "layout script stated");

        /* Per KEY, like the camera: a manifest moving the button down the
         * column must not take the interface or the geometry with it. */
        merge_ini(&profile, "[chrome]\nplugin_button_slot=3\n");
        TEST_ASSERT(profile.chrome.plugin_button_slot == 3, "slot overridden");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_iface, "plugin_popout") == 0, "iface survives");
        TEST_ASSERT(profile.chrome.plugin_button_pitch == 36, "pitch survives");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_layout_script, "plugin_popout_layout") == 0,
            "layout script survives");
    }

    /* A geometry of nothing is an invisible button, not a smaller one: it is
     * reported and left unstated rather than applied as atoi()'s zero. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[chrome]\n"
            "plugin_button_size=0\n"
            "plugin_button_pitch=none\n"
            "plugin_button_parent=-2\n");
        TEST_ASSERT(profile.chrome.plugin_button_size == -1, "zero size ignored");
        TEST_ASSERT(profile.chrome.plugin_button_pitch == -1, "unparseable pitch ignored");
        TEST_ASSERT(profile.chrome.plugin_button_parent == -1, "negative parent ignored");
    }

    /* A malformed zoom is reported and ignored, not applied as zero: pinning
     * the eye at the player's feet is not a better answer than the default. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\nzoom=clamped:[2160,240]\n");
        merge_ini(&profile, "[camera]\nzoom=fixed:\n");
        merge_ini(&profile, "[camera]\ncontrols=mmb,trackball\n");
        TEST_ASSERT(
            profile.camera.zoom_mode == REVCONFIG_CAMERA_ZOOM_CLAMPED, "bad zoom ignored");
        TEST_ASSERT(
            profile.camera.zoom_min == REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN, "bad zoom min kept");
        TEST_ASSERT(
            profile.camera.controls ==
                (REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS),
            "bad controls ignored whole");
    }
}
