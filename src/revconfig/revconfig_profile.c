#include "revconfig_profile.h"

#include "revconfig_load.h"

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

    profile->camera.zoom_mode = REVCONFIG_CAMERA_ZOOM_CLAMPED;
    profile->camera.zoom_min = REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN;
    profile->camera.zoom_max = REVCONFIG_CAMERA_ZOOM_DEFAULT_MAX;
    profile->camera.zoom_height = REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT;
    profile->camera.controls =
        REVCONFIG_CAMERA_CONTROL_MMB | REVCONFIG_CAMERA_CONTROL_ARROW_KEYS;
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
}

static void
profile_merge_camera(
    struct RevConfigCameraItem* dst,
    struct RevConfigCameraItem const* src)
{
    assert(dst);
    assert(src);

    if( src->has_zoom )
    {
        dst->zoom_mode = src->zoom_mode;
        dst->zoom_height = src->zoom_height;
        dst->zoom_min = src->zoom_min;
        dst->zoom_max = src->zoom_max;
    }
    if( src->has_controls )
        dst->controls = src->controls;
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
RevConfigProfile_CameraClampHeight(
    struct RevConfigProfile const* profile,
    int height)
{
    assert(profile);
    if( height < profile->camera.zoom_min )
        height = profile->camera.zoom_min;
    if( height > profile->camera.zoom_max )
        height = profile->camera.zoom_max;
    return height;
}
