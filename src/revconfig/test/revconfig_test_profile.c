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
            profile.camera.wheel == REVCONFIG_CAMERA_WHEEL_LIVE, "default zoom clamped");
        TEST_ASSERT(
            profile.camera.zoom_closest == REVCONFIG_CAMERA_ZOOM_CLOSEST_DEFAULT, "default zoom min");
        TEST_ASSERT(
            profile.camera.zoom_furthest == REVCONFIG_CAMERA_ZOOM_FURTHEST_DEFAULT, "default zoom max");
        TEST_ASSERT(
            profile.camera.controls ==
                (REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS),
            "default controls both");
        TEST_ASSERT(
            profile.camera.wheel_step == REVCONFIG_CAMERA_WHEEL_STEP_DEFAULT,
            "default wheel step");
        TEST_ASSERT(
            profile.camera.distance_scale == REVCONFIG_CAMERA_DISTANCE_SCALE_DEFAULT,
            "default distance scale");
        TEST_ASSERT(
            profile.camera.pitch_distance == REVCONFIG_CAMERA_PITCH_DISTANCE_DEFAULT,
            "default pitch distance");
        TEST_ASSERT(
            profile.camera.pitch_flattest == REVCONFIG_CAMERA_PITCH_FLATTEST_DEFAULT,
            "default pitch flattest");
        TEST_ASSERT(
            profile.camera.pitch_steepest == REVCONFIG_CAMERA_PITCH_STEEPEST_DEFAULT,
            "default pitch steepest");
    }

    /*
     * The pitch keys: the third zoom lever and the range it acts over.
     *
     * `pitch_distance=` is what the follow distance multiplies the pitch by,
     * and the reference's 3 is now a default rather than a literal in app.c --
     * where it sat beside four copies of the 128..383 range and a fifth in
     * 256ths. All of it is one statement here.
     */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[camera]\n"
            "pitch_distance=2\n"
            "pitch_flattest=160\n"
            "pitch_steepest=400\n");
        TEST_ASSERT(profile.camera.pitch_distance == 2, "pitch distance stated");
        TEST_ASSERT(profile.camera.pitch_flattest == 160, "pitch flattest stated");
        TEST_ASSERT(profile.camera.pitch_steepest == 400, "pitch steepest stated");

        /* 0 is a real answer -- a camera that does not pull back as it tips. */
        merge_ini(&profile, "[camera]\npitch_distance=0\n");
        TEST_ASSERT(profile.camera.pitch_distance == 0, "a flat pitch term is allowed");

        /* Past a quarter turn the eye is placed under the anchor. */
        merge_ini(&profile, "[camera]\npitch_steepest=900\n");
        TEST_ASSERT(profile.camera.pitch_steepest == 400, "past a quarter turn is refused");
        merge_ini(&profile, "[camera]\npitch_distance=40\n");
        TEST_ASSERT(profile.camera.pitch_distance == 0, "an absurd coefficient is refused");

        /* Crossed ends would pin the camera at one angle, with the terrain
         * clamp driving it up and the drag driving it down. */
        merge_ini(&profile, "[camera]\npitch_flattest=300\npitch_steepest=200\n");
        TEST_ASSERT(
            profile.camera.pitch_flattest <= profile.camera.pitch_steepest,
            "a crossed pitch range is widened");

        /* And it is independent of the zoom keys, like every other key here. */
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\npitch_distance=2\n");
        TEST_ASSERT(profile.camera.rest == REVCONFIG_CAMERA_REST_DEFAULT, "rest untouched");
        TEST_ASSERT(
            profile.camera.pitch_flattest == REVCONFIG_CAMERA_PITCH_FLATTEST_DEFAULT,
            "the range is not implied by the coefficient");
    }

    /*
     * Every key states ONE number, and each is an expression.
     *
     * This is the whole point of the section's shape: `zoom=fixed:<h>` /
     * `zoom=clamped:[a,b]` stated the rest, both band ends and the camera
     * model together, so no profile could restate one without restating the
     * other three. Six keys, six statements.
     */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[camera]\n"
            "rest=0x200\n"
            "zoom_closest=0x100\n"
            "zoom_furthest=8 * 100\n"
            "wheel_step=25\n"
            "distance_scale=70\n"
            "viewport_zoom=no\n");
        TEST_ASSERT(profile.camera.rest == 0x200, "rest expression");
        TEST_ASSERT(profile.camera.zoom_closest == 0x100, "closest expression");
        TEST_ASSERT(profile.camera.zoom_furthest == 800, "furthest expression");
        TEST_ASSERT(profile.camera.wheel_step == 25, "wheel step");
        TEST_ASSERT(profile.camera.distance_scale == 70, "distance scale");
        TEST_ASSERT(profile.camera.viewport_zoom == 0, "viewport zoom off");
        /* The player's switch is not a key and no section may reach it. */
        TEST_ASSERT(profile.camera.wheel == REVCONFIG_CAMERA_WHEEL_LIVE, "wheel is not an INI key");

        /* A leftover comma is somebody writing the old `clamped:[a,b]` into a
         * key that takes one number. Refused, not read as its prefix. */
        merge_ini(&profile, "[camera]\nzoom_closest=240,2160\n");
        TEST_ASSERT(profile.camera.zoom_closest == 0x100, "a band in one key is refused");
    }

    /* A band end left unstated is derived from the rest that SURVIVED the
     * merge, not from the one in scope when it was parsed -- and a stated end
     * keeps its number when a later source moves the rest under it. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\nrest=600\n");
        TEST_ASSERT(profile.camera.zoom_closest == 240, "derived closest at 600");
        TEST_ASSERT(profile.camera.zoom_furthest == 2160, "derived furthest at 600");
        merge_ini(&profile, "[camera]\nrest=1000\n");
        TEST_ASSERT(profile.camera.zoom_closest == 400, "derived band follows the new rest");
        TEST_ASSERT(profile.camera.zoom_furthest == 3600, "and its far end too");
        merge_ini(&profile, "[camera]\nzoom_closest=60\n");
        merge_ini(&profile, "[camera]\nrest=600\n");
        TEST_ASSERT(profile.camera.zoom_closest == 60, "a STATED end is not re-derived");
        TEST_ASSERT(profile.camera.zoom_furthest == 2160, "its unstated partner still is");
    }

    /* distance_scale= is the device's dolly and is independent of the band:
     * stating it must not disturb where the camera rests or how far the wheel
     * may travel. Out-of-range is refused rather than clamped -- a percentage
     * nobody meant would arrive as a black frame or a camera inside the
     * player's head. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile,
            "[camera]\nzoom_closest=300\nzoom_furthest=1200\ndistance_scale=70\n");
        TEST_ASSERT(profile.camera.distance_scale == 70, "distance scale stated");
        TEST_ASSERT(profile.camera.zoom_closest == 300, "band untouched by the scale");
        TEST_ASSERT(profile.camera.rest == 600, "rest untouched by the scale");
        merge_ini(&profile, "[camera]\nwheel_step=25\n");
        TEST_ASSERT(profile.camera.distance_scale == 70, "scale survives another key");
        merge_ini(&profile, "[camera]\ndistance_scale=5\n");
        TEST_ASSERT(profile.camera.distance_scale == 70, "below the range is refused");
        merge_ini(&profile, "[camera]\ndistance_scale=1000\n");
        TEST_ASSERT(profile.camera.distance_scale == 70, "above the range is refused");
        merge_ini(&profile, "[camera]\ndistance_scale=130\n");
        TEST_ASSERT(profile.camera.distance_scale == 130, "scale overridden");
    }

    /* wheel_step= is its own key: a section that states only it keeps the
     * default band, and a later source can override it alone. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile,
            "[camera]\nzoom_closest=300\nzoom_furthest=1200\nwheel_step=25\n");
        TEST_ASSERT(profile.camera.wheel_step == 25, "wheel step stated");
        TEST_ASSERT(profile.camera.zoom_closest == 300, "band stated beside it");
        merge_ini(&profile, "[camera]\nwheel_step=100\n");
        TEST_ASSERT(profile.camera.wheel_step == 100, "wheel step overridden");
        TEST_ASSERT(profile.camera.zoom_closest == 300, "band survives wheel step override");
        TEST_ASSERT(profile.camera.zoom_furthest == 1200, "band far end survives");
        merge_ini(&profile, "[camera]\nzoom_closest=240\nzoom_furthest=2160\n");
        TEST_ASSERT(profile.camera.wheel_step == 100, "wheel step survives a band override");
    }

    /* The 2004 camera, which is now three ordinary keys instead of one
     * spelling: it rests at 600, takes neither viewport term, and orbits on
     * the arrow keys. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[camera]\n"
            "rest=600\n"
            "viewport_zoom=no\n"
            "controls=arrow_keys\n");
        /* The wheel stays live -- it is this client's gesture, not the
         * revision's. Losing it was the old combined key's doing, and there is
         * no longer a spelling that could take it away. */
        TEST_ASSERT(
            profile.camera.wheel == REVCONFIG_CAMERA_WHEEL_LIVE, "wheel still live");
        TEST_ASSERT(profile.camera.viewport_zoom == 0, "no viewport term");
        TEST_ASSERT(profile.camera.rest == 600, "rest stated");
        TEST_ASSERT(
            profile.camera.controls == REVCONFIG_CAMERA_CONTROL_ARROW_KEYS, "arrow keys only");
        /* A band comes with it, so switching the wheel on in the settings has
         * somewhere to go. 40%..360% of the stated 600. */
        TEST_ASSERT(profile.camera.zoom_closest == 240, "derived band floor");
        TEST_ASSERT(profile.camera.zoom_furthest == 2160, "derived band top");
        TEST_ASSERT(
            RevConfigProfile_CameraClampZoom(&profile, 2000) == 2000, "inside the band");
        TEST_ASSERT(
            RevConfigProfile_CameraClampZoom(&profile, 100) == 240, "clamped to the floor");
    }

    /* Later source wins PER KEY: overriding controls must not put the zoom back
     * to its default. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile,
            "[camera]\nrest=600\nviewport_zoom=no\ncontrols=arrow_keys\n");
        merge_ini(&profile, "[camera]\ncontrols=mmb,arrow_keys\n");
        /* Every other key survives a later source restating only `controls=`.
         * `wheel` is not among them: it is the player's, and no source states
         * it at all. */
        TEST_ASSERT(profile.camera.viewport_zoom == 0, "camera model survives");
        TEST_ASSERT(profile.camera.rest == 600, "rest survives");
        TEST_ASSERT(profile.camera.zoom_closest == 240, "band survives");
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

    /* No `[chrome]` block is a revision the client builds no launcher on:
     * nothing to mount the button in, and no geometry invented for one. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        TEST_ASSERT(profile.chrome.plugin_iface[0] == '\0', "chrome iface unstated");
        TEST_ASSERT(profile.chrome.plugin_button_parent == -1, "button parent unstated");
        TEST_ASSERT(profile.chrome.plugin_button_x == -1, "button x unstated");
        TEST_ASSERT(profile.chrome.plugin_button_y == -1, "button y unstated");
        TEST_ASSERT(profile.chrome.plugin_button_w == -1, "button width unstated");
        TEST_ASSERT(profile.chrome.plugin_button_h == -1, "button height unstated");
        TEST_ASSERT(profile.chrome.plugin_button_op[0] == '\0', "button op unstated");
        TEST_ASSERT(profile.chrome.plugin_button_anchor[0] == '\0', "anchor unstated");
        TEST_ASSERT(
            profile.chrome.plugin_button_align == REVCONFIG_CHROME_ALIGN_NONE,
            "align unstated");
        TEST_ASSERT(profile.chrome.plugin_button_margin == -1, "margin unstated");
    }

    /* The rev-239 mount, whole -- and the interface root is child 0, so a real
     * zero has to survive a merge that keys off "unstated". */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[chrome]\n"
            "plugin_button_iface=logout\n"
            "plugin_button_parent=0\n"
            "plugin_button_x=23\n"
            "plugin_button_y=205\n"
            "plugin_button_w=144\n"
            "plugin_button_h=36\n"
            "plugin_button_op=Manage Plugins\n");
        TEST_ASSERT(strcmp(profile.chrome.plugin_iface, "logout") == 0, "chrome iface stated");
        TEST_ASSERT(profile.chrome.plugin_button_parent == 0, "child zero stated");
        TEST_ASSERT(profile.chrome.plugin_button_x == 23, "button x stated");
        TEST_ASSERT(profile.chrome.plugin_button_y == 205, "button y stated");
        TEST_ASSERT(profile.chrome.plugin_button_w == 144, "button width stated");
        TEST_ASSERT(profile.chrome.plugin_button_h == 36, "button height stated");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_button_op, "Manage Plugins") == 0,
            "button op stated");

        /* Per KEY, like the camera: a manifest nudging the plate down the tab
         * must not take the interface or the rest of the box with it. */
        merge_ini(&profile, "[chrome]\nplugin_button_y=180\n");
        TEST_ASSERT(profile.chrome.plugin_button_y == 180, "y overridden");
        TEST_ASSERT(strcmp(profile.chrome.plugin_iface, "logout") == 0, "iface survives");
        TEST_ASSERT(profile.chrome.plugin_button_w == 144, "width survives");
    }

    /* The anchored mount: a role to cut the plate from, and the one thing the
     * role cannot say -- which end of the panel it goes. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[chrome]\n"
            "plugin_button_iface=logout\n"
            "plugin_button_parent=0\n"
            "plugin_button_anchor=logout_button\n"
            "plugin_button_align=top\n"
            "plugin_button_margin=15\n");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_button_anchor, "logout_button") == 0,
            "anchor stated");
        TEST_ASSERT(
            profile.chrome.plugin_button_align == REVCONFIG_CHROME_ALIGN_TOP,
            "align stated");
        TEST_ASSERT(profile.chrome.plugin_button_margin == 15, "margin stated");
        /* A margin of zero is a plate flush with the panel's edge, which is a
         * placement and not an omission. */
        merge_ini(&profile, "[chrome]\nplugin_button_align=bottom\nplugin_button_margin=0\n");
        TEST_ASSERT(
            profile.chrome.plugin_button_align == REVCONFIG_CHROME_ALIGN_BOTTOM,
            "align overridden");
        TEST_ASSERT(profile.chrome.plugin_button_margin == 0, "flush margin stated");
        TEST_ASSERT(
            strcmp(profile.chrome.plugin_button_anchor, "logout_button") == 0,
            "anchor survives");

        /* An edge this client has no meaning for is reported and left alone,
         * for the same reason an unparseable number is: guessing puts the
         * plate over the panel's own controls. */
        merge_ini(&profile, "[chrome]\nplugin_button_align=middle\n");
        TEST_ASSERT(
            profile.chrome.plugin_button_align == REVCONFIG_CHROME_ALIGN_BOTTOM,
            "unknown edge ignored");
    }

    /* A geometry of nothing is an invisible button, not a smaller one: it is
     * reported and left unstated rather than applied as atoi()'s zero. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(
            &profile,
            "[chrome]\n"
            "plugin_button_w=0\n"
            "plugin_button_h=none\n"
            "plugin_button_parent=-2\n");
        TEST_ASSERT(profile.chrome.plugin_button_w == -1, "zero width ignored");
        TEST_ASSERT(profile.chrome.plugin_button_h == -1, "unparseable height ignored");
        TEST_ASSERT(profile.chrome.plugin_button_parent == -1, "negative parent ignored");
    }

    /* A malformed value is reported and ignored, not applied as zero: resting
     * the eye at the player's feet is not a better answer than the default.
     * The old `zoom=` spelling is refused the same way -- named in the loader
     * so it says what to write instead, rather than booting a silent default. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\nrest=\n");
        merge_ini(&profile, "[camera]\nzoom_closest=none\n");
        merge_ini(&profile, "[camera]\nviewport_zoom=sometimes\n");
        merge_ini(&profile, "[camera]\nzoom=clamped:[2160,240]\n");
        merge_ini(&profile, "[camera]\ncontrols=mmb,trackball\n");
        TEST_ASSERT(profile.camera.rest == REVCONFIG_CAMERA_REST_DEFAULT, "bad rest ignored");
        TEST_ASSERT(
            profile.camera.zoom_closest == REVCONFIG_CAMERA_ZOOM_CLOSEST_DEFAULT,
            "bad band end kept");
        TEST_ASSERT(profile.camera.viewport_zoom == 1, "bad viewport_zoom ignored");
        TEST_ASSERT(
            profile.camera.wheel == REVCONFIG_CAMERA_WHEEL_LIVE, "the wheel is untouched");
        TEST_ASSERT(
            profile.camera.controls ==
                (REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS),
            "bad controls ignored whole");
    }

    /* Crossed band ends are widened rather than left inverted:
     * app_world_camera_zooms reads `closest < furthest` as "this camera
     * zooms", so a crossed pair would take the wheel away entirely. */
    {
        struct RevConfigProfile profile;
        RevConfigProfile_Init(&profile);
        merge_ini(&profile, "[camera]\nzoom_closest=900\nzoom_furthest=400\n");
        TEST_ASSERT(
            profile.camera.zoom_closest < profile.camera.zoom_furthest,
            "a crossed band still zooms");
        TEST_ASSERT(profile.camera.zoom_closest == 900, "the stated near end is kept");
    }
}
