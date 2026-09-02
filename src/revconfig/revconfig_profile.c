#include "revconfig_profile.h"

#include "revconfig_load.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <string.h>

void
RevConfigProfile_Init(struct RevConfigProfile* profile)
{
    assert(profile);
    memset(profile, 0, sizeof(*profile));

    /* Not stated, for the three names and the two permissive extensions.
     * painter_draw_distance's own sentinel is the 0 memset already left. */
    profile->features.ground_click_unbounded = -1;
    profile->features.ground_click_offmap = -1;
    profile->features.clienttype = -1;
    profile->features.on_mobile = -1;

    /* Nothing stated: the client builds no plugin launcher. The memset already
     * left both names empty; the numbers need their own sentinel because 0 is a
     * real child, a real position and (for a size) a real-looking one. */
    profile->chrome.plugin_button_parent = -1;
    profile->chrome.plugin_button_x = -1;
    profile->chrome.plugin_button_y = -1;
    profile->chrome.plugin_button_w = -1;
    profile->chrome.plugin_button_h = -1;
    profile->chrome.plugin_button_align = REVCONFIG_CHROME_ALIGN_NONE;
    profile->chrome.plugin_button_margin = -1;

    /* One default per key, spelled the same way the INI spells it. The has_
     * flags stay 0: nothing has been STATED yet, which is what lets the band
     * ends stay derived from a rest a later source may still move. */
    profile->camera.rest = REVCONFIG_CAMERA_REST_DEFAULT;
    profile->camera.zoom_closest = REVCONFIG_CAMERA_ZOOM_CLOSEST_DEFAULT;
    profile->camera.zoom_furthest = REVCONFIG_CAMERA_ZOOM_FURTHEST_DEFAULT;
    profile->camera.wheel_step = REVCONFIG_CAMERA_WHEEL_STEP_DEFAULT;
    profile->camera.distance_scale = REVCONFIG_CAMERA_DISTANCE_SCALE_DEFAULT;
    profile->camera.viewport_zoom = REVCONFIG_CAMERA_VIEWPORT_ZOOM_DEFAULT;
    profile->camera.pitch_distance = REVCONFIG_CAMERA_PITCH_DISTANCE_DEFAULT;
    profile->camera.pitch_flattest = REVCONFIG_CAMERA_PITCH_FLATTEST_DEFAULT;
    profile->camera.pitch_steepest = REVCONFIG_CAMERA_PITCH_STEEPEST_DEFAULT;
    profile->camera.controls =
        REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS;
    /* Not an INI key -- the player's switch. @see enum RevConfigCameraWheel. */
    profile->camera.wheel = REVCONFIG_CAMERA_WHEEL_LIVE;
}

static void
profile_merge_features(
    struct RevConfigFeaturesItem* dst,
    struct RevConfigFeaturesItem const* src)
{
    assert(dst);
    assert(src);

    if( src->era[0] )
        memcpy(dst->era, src->era, sizeof(dst->era));
    if( src->ground_click_nearest[0] )
        memcpy(
            dst->ground_click_nearest,
            src->ground_click_nearest,
            sizeof(dst->ground_click_nearest));
    if( src->mover[0] )
        memcpy(dst->mover, src->mover, sizeof(dst->mover));
    if( src->ground_click_unbounded >= 0 )
        dst->ground_click_unbounded = src->ground_click_unbounded;
    if( src->ground_click_offmap >= 0 )
        dst->ground_click_offmap = src->ground_click_offmap;
    if( src->painter_draw_distance > 0 )
        dst->painter_draw_distance = src->painter_draw_distance;
    if( src->clienttype >= 0 )
        dst->clienttype = src->clienttype;
    if( src->on_mobile >= 0 )
        dst->on_mobile = src->on_mobile;
}

/*
 * The band ends nobody stated, from the rest that survived the merge.
 *
 * Run after every merge rather than at parse time, because the rest a
 * derivation should follow is the FINAL one: a lane whose ui.ini says
 * `rest=600` and whose boot manifest says `rest=900` wants the default band
 * around 900, and deriving at parse time would have handed it the band around
 * 600 and then let the rest walk out from under it.
 *
 * An end that a section did state is left exactly as stated, which is the
 * whole point of the has_ flags: `zoom_closest=60` on a phone must not lose
 * its floor because some later source restated the rest.
 */
static void
profile_camera_resolve_band(struct RevConfigCameraItem* camera)
{
    int closest;
    int furthest;

    assert(camera);
    assert(camera->rest > 0);

    revconfig_camera_default_band(camera->rest, &closest, &furthest);
    if( !camera->has_zoom_closest )
        camera->zoom_closest = closest;
    if( !camera->has_zoom_furthest )
        camera->zoom_furthest = furthest;

    /* An inverted band is not a camera anybody meant: app_world_camera_zooms
     * reads `closest < furthest` as "this revision zooms at all", so the two
     * keys crossing would take the wheel away rather than narrow it. Report
     * and keep the wider of the two, since a stated end is a deliberate one
     * and the derived partner is not. */
    if( camera->zoom_closest >= camera->zoom_furthest )
    {
        TORIRS_ERR("revconfig: [camera] zoom_closest=%d is not nearer than "
            "zoom_furthest=%d; widening to keep the band\n",
            camera->zoom_closest,
            camera->zoom_furthest);
        if( camera->has_zoom_closest )
            camera->zoom_furthest = camera->zoom_closest + 1;
        else
            camera->zoom_closest = camera->zoom_furthest - 1;
    }
}

/*
 * The pitch range, same rule as the band: crossed ends are a camera nobody
 * meant. Every consumer clamps `flattest <= pitch <= steepest`, so a crossed
 * pair would pin the camera at one angle with no way out of it -- and the
 * terrain clamp, which drives pitch UP toward steepest, would fight the drag
 * driving it down.
 */
static void
profile_camera_resolve_pitch(struct RevConfigCameraItem* camera)
{
    assert(camera);

    if( camera->pitch_flattest <= camera->pitch_steepest )
        return;
    TORIRS_ERR("revconfig: [camera] pitch_flattest=%d is not flatter than "
        "pitch_steepest=%d; widening to keep the range\n",
        camera->pitch_flattest,
        camera->pitch_steepest);
    /* A stated end is deliberate and its partner may only be a default, so the
     * default is the one that moves. */
    if( camera->has_pitch_flattest )
        camera->pitch_steepest = camera->pitch_flattest;
    else
        camera->pitch_flattest = camera->pitch_steepest;
}

/*
 * One `if` per key, and every one of them the same shape.
 *
 * There is no compound case left: while `zoom=` stated the rest, both band
 * ends and the camera model together, this function had to special-case the
 * order they were applied in and `zoom_closest=` had to reach past it to a
 * single end. Each key now merges on its own has_ flag, so a later source
 * restating one of them cannot disturb the other five.
 *
 * `wheel` is absent on purpose: no INI states it, so no merge carries it.
 * A profile carrying it would put the player's switch back under revision
 * control by the back door. @see enum RevConfigCameraWheel.
 */
static void
profile_merge_camera(
    struct RevConfigCameraItem* dst,
    struct RevConfigCameraItem const* src)
{
    assert(dst);
    assert(src);

    if( src->has_rest )
    {
        dst->rest = src->rest;
        dst->has_rest = 1;
    }
    if( src->has_zoom_closest )
    {
        dst->zoom_closest = src->zoom_closest;
        dst->has_zoom_closest = 1;
    }
    if( src->has_zoom_furthest )
    {
        dst->zoom_furthest = src->zoom_furthest;
        dst->has_zoom_furthest = 1;
    }
    if( src->has_wheel_step )
    {
        dst->wheel_step = src->wheel_step;
        dst->has_wheel_step = 1;
    }
    if( src->has_distance_scale )
    {
        dst->distance_scale = src->distance_scale;
        dst->has_distance_scale = 1;
    }
    if( src->has_viewport_zoom )
    {
        dst->viewport_zoom = src->viewport_zoom;
        dst->has_viewport_zoom = 1;
    }
    if( src->has_pitch_distance )
    {
        dst->pitch_distance = src->pitch_distance;
        dst->has_pitch_distance = 1;
    }
    if( src->has_pitch_flattest )
    {
        dst->pitch_flattest = src->pitch_flattest;
        dst->has_pitch_flattest = 1;
    }
    if( src->has_pitch_steepest )
    {
        dst->pitch_steepest = src->pitch_steepest;
        dst->has_pitch_steepest = 1;
    }
    if( src->has_controls )
    {
        dst->controls = src->controls;
        dst->has_controls = 1;
    }
    profile_camera_resolve_band(dst);
    profile_camera_resolve_pitch(dst);
}

static void
profile_merge_chrome(
    struct RevConfigChromeItem* dst,
    struct RevConfigChromeItem const* src)
{
    assert(dst);
    assert(src);

    /* Per key, like the two above: a later source moving the button one slot
     * down must not take the interface, the geometry and the layout script with
     * it. An unstated key is an empty string or a -1, which is exactly what the
     * item carries for a key its section did not spell. */
    if( src->plugin_iface[0] )
        memcpy(dst->plugin_iface, src->plugin_iface, sizeof(dst->plugin_iface));
    if( src->plugin_button_op[0] )
        memcpy(dst->plugin_button_op, src->plugin_button_op, sizeof(dst->plugin_button_op));
    if( src->plugin_button_parent >= 0 )
        dst->plugin_button_parent = src->plugin_button_parent;
    if( src->plugin_button_x >= 0 )
        dst->plugin_button_x = src->plugin_button_x;
    if( src->plugin_button_y >= 0 )
        dst->plugin_button_y = src->plugin_button_y;
    if( src->plugin_button_w >= 0 )
        dst->plugin_button_w = src->plugin_button_w;
    if( src->plugin_button_h >= 0 )
        dst->plugin_button_h = src->plugin_button_h;
    if( src->plugin_button_anchor[0] )
        memcpy(
            dst->plugin_button_anchor,
            src->plugin_button_anchor,
            sizeof(dst->plugin_button_anchor));
    if( src->plugin_button_align != REVCONFIG_CHROME_ALIGN_NONE )
        dst->plugin_button_align = src->plugin_button_align;
    if( src->plugin_button_margin >= 0 )
        dst->plugin_button_margin = src->plugin_button_margin;
}

void
RevConfigProfile_AddItems(
    struct RevConfigProfile* profile,
    struct RevConfigItemBuffer const* items)
{
    assert(profile);
    assert(items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_FEATURES )
            profile_merge_features(&profile->features, &item->u.features);
        else if( item->kind == RCITEM_CAMERA )
            profile_merge_camera(&profile->camera, &item->u.camera);
        else if( item->kind == RCITEM_CHROME )
            profile_merge_chrome(&profile->chrome, &item->u.chrome);
    }
}

/** Parse one source into `profile`. `prefix` NULL/"" is the unprefixed dialect. */
static void
profile_load_one(
    struct RevConfigProfile* profile,
    char const* path,
    char const* prefix)
{
    struct RevConfigBuffer* fields;
    struct RevConfigItemBuffer* items;

    assert(profile);
    if( !path || path[0] == '\0' )
        return;

    fields = revconfig_buffer_new(256);
    assert(fields);
    items = revconfig_item_buffer_new(64);
    assert(items);

    revconfig_load_fields_from_ini_prefixed(path, prefix, fields);
    revconfig_items_build(fields, items);
    RevConfigProfile_AddItems(profile, items);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

void
RevConfigProfile_LoadSources(
    struct RevConfigProfile* profile,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini)
{
    assert(profile);
    /* Same order as RevConfigRefs_LoadSources: shared files first, the boot
     * manifest's own inline sections last. */
    profile_load_one(profile, ui_ini, NULL);
    profile_load_one(profile, cache_ini, NULL);
    profile_load_one(profile, inline_ini, "revconfig");
}

int
RevConfigProfile_CameraClampZoom(
    struct RevConfigProfile const* profile,
    int zoom)
{
    assert(profile);
    if( zoom < profile->camera.zoom_closest )
        zoom = profile->camera.zoom_closest;
    if( zoom > profile->camera.zoom_furthest )
        zoom = profile->camera.zoom_furthest;
    return zoom;
}
