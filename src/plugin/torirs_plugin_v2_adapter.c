#include "plugin/torirs_plugin_v2_adapter.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define V2_PANEL_CHOICES_MAX 192

enum V2ResourceKind
{
    V2_RESOURCE_IMAGE = 0,
    V2_RESOURCE_MODEL,
    V2_RESOURCE_MESH,
    V2_RESOURCE_INSTANCE,
};

#define V2_RESOURCE_SLOT_MASK ((1u << TORIRS_PLUGIN_V2_RESOURCE_SLOT_BITS) - 1u)
#define V2_RESOURCE_KIND_MASK ((1u << TORIRS_PLUGIN_V2_RESOURCE_KIND_BITS) - 1u)
#define V2_RESOURCE_NAMESPACE_MASK                                                     \
    ((1u << TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_BITS) - 1u)

_Static_assert(
    TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX <= V2_RESOURCE_SLOT_MASK,
    "image tokens fit encoded slot");
_Static_assert(
    TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX <= V2_RESOURCE_SLOT_MASK,
    "model tokens fit encoded slot");
_Static_assert(
    TORIRS_PLUGIN_V2_MESH_TOKENS_MAX <= V2_RESOURCE_SLOT_MASK,
    "mesh tokens fit encoded slot");
_Static_assert(
    TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX <= V2_RESOURCE_SLOT_MASK,
    "scene-instance tokens fit encoded slot");
_Static_assert(V2_RESOURCE_INSTANCE <= V2_RESOURCE_KIND_MASK, "resource kinds fit token");

static int
v2_resource_encode(
    int slot,
    uint32_t incarnation,
    enum V2ResourceKind kind,
    uint32_t resource_namespace)
{
    uint32_t const value =
        (incarnation << TORIRS_PLUGIN_V2_RESOURCE_INCAR_SHIFT) |
        (resource_namespace << TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_SHIFT) |
        ((uint32_t)kind << TORIRS_PLUGIN_V2_RESOURCE_SLOT_BITS) |
        (uint32_t)(slot + 1);

    assert(slot >= 0 && slot < (int)V2_RESOURCE_SLOT_MASK);
    assert(incarnation <= TORIRS_PLUGIN_V2_RESOURCE_INCAR_MAX);
    assert(resource_namespace <= V2_RESOURCE_NAMESPACE_MASK);
    assert(value > 0 && value <= INT_MAX);
    return (int)value;
}

/* Allocation is deliberately off the retained hot path.  Resolution below
 * is O(1); acquiring scans one small, fixed table so it can both preserve the
 * token for a repeated request and reuse a retired legacy handle safely. */
static int
v2_resource_acquire(
    struct ToriRS_PluginV2ResourceToken* entries,
    int count,
    enum V2ResourceKind kind,
    uint32_t resource_namespace,
    int legacy)
{
    int free_slot = -1;

    assert(entries);
    if( legacy < 0 )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_PluginV2ResourceToken* entry = &entries[i];
        if( entry->active && entry->legacy == legacy )
            return v2_resource_encode(i, entry->incarnation, kind, resource_namespace);
        if( !entry->active && !entry->retired && free_slot < 0 )
            free_slot = i;
    }
    if( free_slot < 0 )
        return 0;
    entries[free_slot].legacy = legacy;
    entries[free_slot].active = true;
    return v2_resource_encode(
        free_slot, entries[free_slot].incarnation, kind, resource_namespace);
}

static int
v2_resource_resolve(
    struct ToriRS_PluginV2ResourceToken const* entries,
    int count,
    enum V2ResourceKind kind,
    uint32_t resource_namespace,
    int value)
{
    struct ToriRS_PluginV2ResourceToken const* entry;
    uint32_t encoded;
    uint32_t incarnation;
    uint32_t encoded_kind;
    uint32_t encoded_namespace;
    int slot;

    assert(entries);
    if( value <= 0 )
        return -1;
    encoded = (uint32_t)value;
    slot = (int)(encoded & V2_RESOURCE_SLOT_MASK) - 1;
    encoded_kind =
        (encoded >> TORIRS_PLUGIN_V2_RESOURCE_SLOT_BITS) & V2_RESOURCE_KIND_MASK;
    encoded_namespace =
        (encoded >> TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_SHIFT) &
        V2_RESOURCE_NAMESPACE_MASK;
    incarnation = encoded >> TORIRS_PLUGIN_V2_RESOURCE_INCAR_SHIFT;
    if( slot < 0 || slot >= count || encoded_kind != (uint32_t)kind ||
        encoded_namespace != resource_namespace )
        return -1;
    entry = &entries[slot];
    if( !entry->active || entry->retired || entry->incarnation != incarnation )
        return -1;
    return entry->legacy;
}

static void
v2_resource_invalidate(
    struct ToriRS_PluginV2ResourceToken* entries,
    int count,
    enum V2ResourceKind kind,
    uint32_t resource_namespace,
    int value)
{
    uint32_t const encoded = value > 0 ? (uint32_t)value : 0;
    int const slot = (int)(encoded & V2_RESOURCE_SLOT_MASK) - 1;

    assert(entries);
    if( v2_resource_resolve(entries, count, kind, resource_namespace, value) < 0 )
        return;
    assert(slot >= 0 && slot < count);
    entries[slot].active = false;
    entries[slot].legacy = -1;
    if( entries[slot].incarnation == TORIRS_PLUGIN_V2_RESOURCE_INCAR_MAX )
        entries[slot].retired = true;
    else
        entries[slot].incarnation++;
}

static bool
v2_output_copy(
    void* output,
    uint32_t capacity,
    void const* snapshot,
    size_t snapshot_size)
{
    assert(output);
    assert(snapshot);
    if( capacity < sizeof(uint32_t) )
        return false;
    memcpy(output, snapshot, capacity < snapshot_size ? capacity : snapshot_size);
    return true;
}

int
ToriRS_PluginV2Adapter_ImageUnbox(
    struct ToriRS_PluginV2Adapter const* adapter,
    struct ToriRS_ImageRef image)
{
    assert(adapter);
    return v2_resource_resolve(
        adapter->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        adapter->hooks.resource_namespace,
        image.value);
}

int
ToriRS_PluginV2Adapter_ModelUnbox(
    struct ToriRS_PluginV2Adapter const* adapter,
    struct ToriRS_ModelRef model)
{
    assert(adapter);
    return v2_resource_resolve(
        adapter->model_tokens,
        TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
        V2_RESOURCE_MODEL,
        adapter->hooks.resource_namespace,
        model.value);
}

_Static_assert(
    (int)TORIRS_AREA_PLATFORM_SAFE == (int)TORIRS_PLUGIN_AREA_PLATFORM_SAFE,
    "platform area");
_Static_assert(
    (int)TORIRS_AREA_FRAME_BUILD == (int)TORIRS_PLUGIN_AREA_FRAME_BUILD,
    "frame-build area");
_Static_assert(
    (int)TORIRS_AREA_OVERLAY_SAFE == (int)TORIRS_PLUGIN_AREA_OVERLAY_SAFE,
    "overlay area");
_Static_assert(
    (int)TORIRS_AREA_RAW_VIEWPORT == (int)TORIRS_PLUGIN_AREA_RAW_VIEWPORT,
    "viewport area");
_Static_assert(
    (int)TORIRS_ANCHOR_TOP_LEFT == (int)TORIRS_PLACEMENT_ANCHOR_TOP_LEFT,
    "top-left anchor");
_Static_assert(
    (int)TORIRS_ANCHOR_BOTTOM_RIGHT == (int)TORIRS_PLACEMENT_ANCHOR_BOTTOM_RIGHT,
    "bottom-right anchor");
_Static_assert(
    (int)TORIRS_EDGE_TOP == (int)TORIRS_PLUGIN_PLACEMENT_EDGE_TOP,
    "top edge");
_Static_assert(
    (int)TORIRS_EDGE_RIGHT == (int)TORIRS_PLUGIN_PLACEMENT_EDGE_RIGHT,
    "right edge");
_Static_assert(
    (int)TORIRS_EDGE_BOTTOM == (int)TORIRS_PLUGIN_PLACEMENT_EDGE_BOTTOM,
    "bottom edge");
_Static_assert(
    (int)TORIRS_EDGE_LEFT == (int)TORIRS_PLUGIN_PLACEMENT_EDGE_LEFT,
    "left edge");
_Static_assert(
    (int)TORIRS_FRAME_STATUS_NATIVE == (int)TORIRS_PLUGIN_FRAME_NATIVE,
    "native frame status");
_Static_assert(
    (int)TORIRS_FRAME_STATUS_ACTIVE == (int)TORIRS_PLUGIN_FRAME_ACTIVE,
    "active frame status");
_Static_assert(
    (int)TORIRS_FRAME_STATUS_LOADING == (int)TORIRS_PLUGIN_FRAME_LOADING,
    "loading frame status");
_Static_assert(
    (int)TORIRS_FRAME_STATUS_FALLBACK == (int)TORIRS_PLUGIN_FRAME_FALLBACK,
    "fallback frame status");

static struct ToriRS_PluginV2Adapter*
v2_adapter(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter;

    assert(api);
    adapter = api->instance;
    assert(adapter);
    assert(&adapter->api == api);
    assert(adapter->legacy);
    assert(adapter->context);
    return adapter;
}

static void
v2_core_log(
    struct ToriRS_ApiV2* api,
    char const* format,
    ...)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    char message[1024];
    va_list args;

    assert(format);
    if( !adapter->legacy->log )
        return;
    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    adapter->legacy->log(adapter->context, "%s", message);
}

static void
v2_core_notify(
    struct ToriRS_ApiV2* api,
    char const* text)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(text);
    if( adapter->legacy->notify )
        adapter->legacy->notify(adapter->context, text);
}

static int
v2_core_screen(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->screen ? adapter->legacy->screen(adapter->context)
                                   : TORIRS_PLUGIN_SCREEN_BOOT;
}

static uint64_t
v2_core_frame_ms(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->frame_ms ? adapter->legacy->frame_ms(adapter->context) : 0;
}

static uint64_t
v2_core_frame_work_us(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->frame_work_us ? adapter->legacy->frame_work_us(adapter->context) : 0;
}

static bool
v2_core_lane(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginLane* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->lane && adapter->legacy->lane(adapter->context, out);
}

static bool
v2_core_capability(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(name);
    return adapter->hooks.capability &&
           adapter->hooks.capability(adapter->hooks.user, adapter->context, name);
}

static bool
v2_config_has(
    struct ToriRS_ApiV2* api,
    char const* key)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(key);
    return adapter->legacy->cfg_has && adapter->legacy->cfg_has(adapter->context, key);
}

static bool
v2_config_get_bool(
    struct ToriRS_ApiV2* api,
    char const* key,
    bool* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(key);
    assert(out);
    if( !adapter->legacy->cfg_has || !adapter->legacy->cfg_bool ||
        !adapter->legacy->cfg_has(adapter->context, key) )
        return false;
    *out = adapter->legacy->cfg_bool(adapter->context, key) != 0;
    return true;
}

static bool
v2_config_get_int(
    struct ToriRS_ApiV2* api,
    char const* key,
    int* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(key);
    assert(out);
    if( !adapter->legacy->cfg_has || !adapter->legacy->cfg_int ||
        !adapter->legacy->cfg_has(adapter->context, key) )
        return false;
    *out = adapter->legacy->cfg_int(adapter->context, key);
    return true;
}

static bool
v2_config_get_color(
    struct ToriRS_ApiV2* api,
    char const* key,
    uint32_t* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(key);
    assert(out);
    if( !adapter->legacy->cfg_has || !adapter->legacy->cfg_color ||
        !adapter->legacy->cfg_has(adapter->context, key) )
        return false;
    *out = adapter->legacy->cfg_color(adapter->context, key);
    return true;
}

static bool
v2_config_get_string(
    struct ToriRS_ApiV2* api,
    char const* key,
    char const** out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(key);
    assert(out);
    if( !adapter->legacy->cfg_has || !adapter->legacy->cfg_str ||
        !adapter->legacy->cfg_has(adapter->context, key) )
        return false;
    *out = adapter->legacy->cfg_str(adapter->context, key);
    return true;
}

static enum ToriRS_Result
v2_config_set(
    struct ToriRS_ApiV2* api,
    char const* key,
    char const* value)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(key);
    assert(value);
    if( !adapter->legacy->cfg_set )
        return TORIRS_RESULT_UNSUPPORTED;
    adapter->legacy->cfg_set(adapter->context, key, value);
    return TORIRS_RESULT_OK;
}

static bool
v2_world_local_player(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginPlayerSnap* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->local_player && adapter->legacy->local_player(adapter->context, out);
}

static int
v2_world_npc_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_PluginNpcSnap* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->npc_next ? adapter->legacy->npc_next(adapter->context, iterator, out)
                                     : -1;
}

static bool
v2_world_npc_by_slot(
    struct ToriRS_ApiV2* api,
    int slot,
    struct ToriRS_PluginNpcSnap* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->npc_by_slot &&
           adapter->legacy->npc_by_slot(adapter->context, slot, out);
}

static int
v2_world_player_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_PluginPlayerSnap* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->player_next
               ? adapter->legacy->player_next(adapter->context, iterator, out)
               : -1;
}

static int
v2_world_item_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_PluginObjSnap* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->obj_next ? adapter->legacy->obj_next(adapter->context, iterator, out)
                                     : -1;
}

static int
v2_world_scenery_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_PluginLocSnap* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->loc_next ? adapter->legacy->loc_next(adapter->context, iterator, out)
                                     : -1;
}

static bool
v2_input_key_held(
    struct ToriRS_ApiV2* api,
    int key)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->key_held && adapter->legacy->key_held(adapter->context, key);
}

static bool
v2_input_pointer(
    struct ToriRS_ApiV2* api,
    int* out_x,
    int* out_y)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out_x);
    assert(out_y);
    return adapter->legacy->mouse_pos && adapter->legacy->mouse_pos(adapter->context, out_x, out_y);
}

static bool
v2_input_hover_tile(
    struct ToriRS_ApiV2* api,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out_x);
    assert(out_z);
    assert(out_level);
    return adapter->legacy->hover_tile &&
           adapter->legacy->hover_tile(adapter->context, out_x, out_z, out_level);
}

static bool
v2_input_hover_entity(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginHoverEntity* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->legacy->hover_entity && adapter->legacy->hover_entity(adapter->context, out);
}

static void
v2_input_text_input(
    struct ToriRS_ApiV2* api,
    bool enabled)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    if( adapter->legacy->text_input )
        adapter->legacy->text_input(adapter->context, enabled ? 1 : 0);
}

static struct ToriRS_UiNodeRef
v2_ui_ref(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_UiNodeRef missing = { 0 };

    assert(name);
    return adapter->hooks.ui_ref
               ? adapter->hooks.ui_ref(adapter->hooks.user, adapter->context, name)
               : missing;
}

static bool
v2_ui_info(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeInfo* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out);
    return adapter->hooks.ui_info &&
           adapter->hooks.ui_info(adapter->hooks.user, adapter->context, node, out);
}

static bool
v2_ui_invoke(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(action);
    return adapter->hooks.ui_invoke &&
           adapter->hooks.ui_invoke(adapter->hooks.user, adapter->context, node, action);
}

static bool
v2_ui_contribution_info(
    struct ToriRS_ApiV2* api,
    char const* node,
    uint32_t facets,
    struct ToriRS_UiContributionInfo* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(node);
    assert(out);
    return adapter->hooks.ui_contribution_info &&
           adapter->hooks.ui_contribution_info(
               adapter->hooks.user, adapter->context, node, facets, out);
}

static bool
v2_ui_node_images_valid(
    struct ToriRS_PluginV2Adapter const* adapter,
    struct ToriRS_UiNode const* value)
{
    size_t const size = value->struct_size ? value->struct_size : TORIRS_UI_NODE_LEGACY_SIZE;

    assert(adapter);
    assert(value);
    if( size >= offsetof(struct ToriRS_UiNode, image) + sizeof(value->image) &&
        value->image.value != 0 && ToriRS_PluginV2Adapter_ImageUnbox(adapter, value->image) < 0 )
        return false;
    if( size < offsetof(struct ToriRS_UiNode, state_image_mask) +
                   sizeof(value->state_image_mask) ||
        value->state_image_mask == 0 )
        return true;
    if( size < offsetof(struct ToriRS_UiNode, state_images) + sizeof(value->state_images) )
        return false;
    for( int state = 0; state < TORIRS_UI_VISUAL_STATE_COUNT; state++ )
        if( (value->state_image_mask & (1u << state)) != 0 &&
            value->state_images[state].value != 0 &&
            ToriRS_PluginV2Adapter_ImageUnbox(adapter, value->state_images[state]) < 0 )
            return false;
    return true;
}

static enum ToriRS_Result
v2_ui_update(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(value);
    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 &&
        !v2_ui_node_images_valid(adapter, value) )
        return TORIRS_RESULT_INVALID;
    return adapter->hooks.ui_update
               ? adapter->hooks.ui_update(
                     adapter->hooks.user, adapter->context, node, facets, value)
               : TORIRS_RESULT_UNSUPPORTED;
}

static bool
v2_area_valid(int area)
{
    return area >= TORIRS_AREA_PLATFORM_SAFE && area <= TORIRS_AREA_RAW_VIEWPORT;
}

static bool
v2_area_decode(
    struct ToriRS_PlacementAreaRef ref,
    int* out_area)
{
    int const area = (int)ref.value - 1;

    assert(out_area);
    if( ref.value == 0 || !v2_area_valid(area) )
        return false;
    *out_area = area;
    return true;
}

static void
v2_rect_from_legacy(
    struct ToriRS_Rect* out,
    struct ToriRS_PlacementRect const* legacy)
{
    assert(out);
    assert(legacy);
    out->x = legacy->x;
    out->y = legacy->y;
    out->width = legacy->w;
    out->height = legacy->h;
}

static void
v2_rect_to_legacy(
    struct ToriRS_PlacementRect* out,
    struct ToriRS_Rect const* rect)
{
    assert(out);
    assert(rect);
    out->x = rect->x;
    out->y = rect->y;
    out->w = rect->width;
    out->h = rect->height;
}

static uint32_t
v2_placement_revision(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->placement_revision
               ? adapter->legacy->placement_revision(adapter->context)
               : 0;
}

static struct ToriRS_PlacementAreaRef
v2_placement_area(
    struct ToriRS_ApiV2* api,
    int area)
{
    struct ToriRS_PlacementAreaRef ref = { 0 };

    (void)v2_adapter(api);
    if( v2_area_valid(area) )
        ref.value = (uint32_t)area + 1u;
    return ref;
}

static bool
v2_placement_primary(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PlacementAreaRef area_ref,
    struct ToriRS_Rect* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PlacementRect largest;
    uint64_t largest_area = 0;
    bool found = false;
    int area;
    int iterator = -1;

    assert(out);
    if( !v2_area_decode(area_ref, &area) || !adapter->legacy->placement_rect_next )
        return false;
    for( ;; )
    {
        struct ToriRS_PlacementRect rect;
        uint64_t rect_area;
        iterator = adapter->legacy->placement_rect_next(adapter->context, area, iterator, &rect);
        if( iterator < 0 )
            break;
        if( rect.w <= 0 || rect.h <= 0 )
            continue;
        rect_area = (uint64_t)rect.w * (uint64_t)rect.h;
        if( !found || rect_area > largest_area )
        {
            largest = rect;
            largest_area = rect_area;
            found = true;
        }
    }
    if( found )
        v2_rect_from_legacy(out, &largest);
    return found;
}

static bool
v2_placement_place(
    struct ToriRS_ApiV2* api,
    int area,
    int anchor,
    int width,
    int height,
    int margin,
    struct ToriRS_Rect* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PlacementRect placed;

    assert(out);
    if( !v2_area_valid(area) || anchor < TORIRS_ANCHOR_TOP_LEFT ||
        anchor > TORIRS_ANCHOR_BOTTOM_RIGHT || width <= 0 || height <= 0 || margin < 0 ||
        !adapter->legacy->placement_place )
        return false;
    if( !adapter->legacy->placement_place(
            adapter->context, area, anchor, width, height, margin, &placed) )
        return false;
    v2_rect_from_legacy(out, &placed);
    return true;
}

static int
v2_placement_rect_next(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PlacementAreaRef area_ref,
    int iterator,
    struct ToriRS_Rect* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PlacementRect rect;
    int area;
    int next;

    assert(out);
    if( !v2_area_decode(area_ref, &area) || !adapter->legacy->placement_rect_next )
        return -1;
    next = adapter->legacy->placement_rect_next(adapter->context, area, iterator, &rect);
    if( next >= 0 )
        v2_rect_from_legacy(out, &rect);
    return next;
}

static bool
v2_placement_contains(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PlacementAreaRef area_ref,
    struct ToriRS_Rect rect)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PlacementRect legacy_rect;
    int area;

    if( !v2_area_decode(area_ref, &area) || rect.width < 0 || rect.height < 0 ||
        !adapter->legacy->placement_contains )
        return false;
    v2_rect_to_legacy(&legacy_rect, &rect);
    return adapter->legacy->placement_contains(adapter->context, area, &legacy_rect) != 0;
}

static bool
v2_reservation_name_valid(char const* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= TORIRS_PLACEMENT_RESERVATION_MAX )
        return false;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' )
            continue;
        return false;
    }
    return true;
}

static enum ToriRS_PlacementReserveResult
v2_placement_reserve(
    struct ToriRS_ApiV2* api,
    char const* name,
    int area,
    int edge,
    int pixels)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(name);
    if( !v2_reservation_name_valid(name) || area != TORIRS_AREA_OVERLAY_SAFE ||
        edge < TORIRS_EDGE_TOP || edge > TORIRS_EDGE_LEFT || pixels < 0 )
        return TORIRS_RESERVE_INVALID;
    if( !adapter->legacy->placement_reserve )
        return TORIRS_RESERVE_INVALID;
    return adapter->legacy->placement_reserve(adapter->context, name, area, edge, pixels)
               ? TORIRS_RESERVE_OK
               : TORIRS_RESERVE_BUDGET;
}

static bool
v2_placement_reservation_rect(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_Rect* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PlacementRect legacy;

    assert(name);
    assert(out);
    if( !v2_reservation_name_valid(name) || !adapter->legacy->placement_reservation_rect ||
        !adapter->legacy->placement_reservation_rect(adapter->context, name, &legacy) )
        return false;
    v2_rect_from_legacy(out, &legacy);
    return true;
}

static int
v2_canvas_from_legacy(int canvas)
{
    if( canvas == TORIRS_PLUGIN_CANVAS_FIXED )
        return TORIRS_FRAME_CANVAS_FIXED;
    if( canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW )
        return TORIRS_FRAME_CANVAS_WINDOW;
    return -1;
}

static int
v2_frame_offer_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_FrameOfferInfo* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PluginFrameInfo legacy;
    struct ToriRS_FrameOfferInfo snapshot;
    uint32_t const capacity = out ? out->struct_size : 0;
    int next;

    assert(out);
    if( capacity < TORIRS_FRAME_OFFER_INFO_REQUIRED_SIZE )
        return -1;
    if( !adapter->legacy->frame_offer_next )
        return -1;
    next = adapter->legacy->frame_offer_next(adapter->context, iterator, &legacy);
    if( next < 0 )
        return -1;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    (void)snprintf(snapshot.id, sizeof(snapshot.id), "%s", legacy.id);
    (void)snprintf(snapshot.title, sizeof(snapshot.title), "%s", legacy.title);
    (void)snprintf(snapshot.provider, sizeof(snapshot.provider), "%s", legacy.provider);
    snapshot.canvas = v2_canvas_from_legacy(legacy.canvas);
    if( snapshot.canvas == TORIRS_FRAME_CANVAS_FIXED )
    {
        snapshot.width = legacy.width;
        snapshot.height = legacy.height;
    }
    else if( snapshot.canvas == TORIRS_FRAME_CANVAS_WINDOW )
    {
        snapshot.min_width = legacy.width;
        snapshot.min_height = legacy.height;
    }
    snapshot.available = legacy.available != 0;
    (void)v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
    return next;
}

static void
v2_frame_selection(
    struct ToriRS_ApiV2* api,
    struct ToriRS_FrameSelection* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    struct ToriRS_PluginFrameSelection legacy;
    struct ToriRS_FrameSelection snapshot;
    uint32_t const capacity = out ? out->struct_size : 0;

    assert(out);
    if( capacity < TORIRS_FRAME_SELECTION_REQUIRED_SIZE )
        return;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    if( !adapter->legacy->frame_selection )
    {
        snapshot.status = TORIRS_FRAME_STATUS_NATIVE;
        (void)v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
        return;
    }
    memset(&legacy, 0, sizeof(legacy));
    adapter->legacy->frame_selection(adapter->context, &legacy);
    (void)snprintf(
        snapshot.requested_id, sizeof(snapshot.requested_id), "%s", legacy.requested);
    (void)snprintf(snapshot.active_id, sizeof(snapshot.active_id), "%s", legacy.active);
    snapshot.status = legacy.status;
    (void)snprintf(snapshot.reason, sizeof(snapshot.reason), "%s", legacy.reason);
    snapshot.revision = legacy.revision;
    (void)v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
}

static enum ToriRS_Result
v2_frame_select(
    struct ToriRS_ApiV2* api,
    char const* id)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(id);
    if( !adapter->legacy->frame_select )
        return TORIRS_RESULT_UNSUPPORTED;
    return adapter->legacy->frame_select(adapter->context, id) ? TORIRS_RESULT_OK
                                                               : TORIRS_RESULT_INVALID;
}

static void
v2_frame_invalidate(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    if( adapter->legacy->frame_invalidate )
        adapter->legacy->frame_invalidate(adapter->context);
}

static bool
v2_draw_project(
    struct ToriRS_ApiV2* api,
    int fine_x,
    int fine_z,
    int height,
    int* out_x,
    int* out_y)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out_x);
    assert(out_y);
    return adapter->legacy->project &&
           adapter->legacy->project(adapter->context, fine_x, fine_z, height, out_x, out_y);
}

static int
v2_draw_element_height(
    struct ToriRS_ApiV2* api,
    int element_id)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->element_height
               ? adapter->legacy->element_height(adapter->context, element_id)
               : 0;
}

static int
v2_draw_hsl_from_rgb(
    struct ToriRS_ApiV2* api,
    uint32_t rgb)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->hsl_from_rgb ? adapter->legacy->hsl_from_rgb(adapter->context, rgb) : 0;
}

static uint32_t
v2_draw_hsl_to_rgb(
    struct ToriRS_ApiV2* api,
    int hsl)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->hsl_to_rgb ? adapter->legacy->hsl_to_rgb(adapter->context, hsl) : 0;
}

static bool
v2_asset_name_valid(char const* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= TORIRS_PLUGIN_ASSET_NAME_MAX || strstr(name, "..") )
        return false;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-' )
            continue;
        return false;
    }
    return true;
}

static enum ToriRS_AssetState
v2_asset_state_checked(enum ToriRS_AssetState state)
{
    return state >= TORIRS_ASSET_PENDING && state <= TORIRS_ASSET_ERROR
               ? state
               : TORIRS_ASSET_ERROR;
}

static enum ToriRS_AssetState
v2_assets_request(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int size = 0;

    assert(name);
    if( !v2_asset_name_valid(name) )
        return TORIRS_ASSET_INVALID;
    if( adapter->hooks.asset_request )
        return v2_asset_state_checked(
            adapter->hooks.asset_request(
                adapter->hooks.user, adapter->context, name));
    if( !adapter->legacy->asset_load || !adapter->legacy->asset_data )
        return TORIRS_ASSET_ERROR;
    if( adapter->legacy->asset_data(adapter->context, name, &size) )
        return TORIRS_ASSET_READY;
    return adapter->legacy->asset_load(adapter->context, name) ? TORIRS_ASSET_READY
                                                               : TORIRS_ASSET_PENDING;
}

static bool
v2_assets_bytes(
    struct ToriRS_ApiV2* api,
    char const* name,
    void const** out_data,
    size_t* out_size)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    void const* data;
    int size = 0;

    assert(name);
    assert(out_data);
    assert(out_size);
    if( !v2_asset_name_valid(name) || !adapter->legacy->asset_data )
        return false;
    if( adapter->hooks.asset_request &&
        v2_asset_state_checked(adapter->hooks.asset_request(
            adapter->hooks.user, adapter->context, name)) != TORIRS_ASSET_READY )
        return false;
    data = adapter->legacy->asset_data(adapter->context, name, &size);
    if( size < 0 )
        return false;
    if( !data && !adapter->hooks.asset_request )
    {
        if( size != 0 || !adapter->legacy->asset_load ||
            !adapter->legacy->asset_load(adapter->context, name) )
            return false;
    }
    *out_data = data;
    *out_size = (size_t)size;
    return true;
}

static enum ToriRS_Result
v2_assets_save(
    struct ToriRS_ApiV2* api,
    char const* name,
    void const* data,
    size_t size)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(name);
    assert(data || size == 0);
    if( !v2_asset_name_valid(name) || size > INT_MAX )
        return TORIRS_RESULT_INVALID;
    if( !adapter->legacy->asset_save )
        return TORIRS_RESULT_UNSUPPORTED;
    return adapter->legacy->asset_save(adapter->context, name, data, (int)size)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_ERROR;
}

static void
v2_assets_release(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(name);
    if( adapter->legacy->asset_release )
        adapter->legacy->asset_release(adapter->context, name);
}

static enum ToriRS_AssetState
v2_assets_image(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_ImageRef* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int width = 0;
    int height = 0;
    int image = -1;
    enum ToriRS_AssetState state;

    assert(name);
    assert(out);
    out->value = 0;
    if( !v2_asset_name_valid(name) )
        return TORIRS_ASSET_INVALID;
    if( adapter->hooks.image_request )
    {
        state = v2_asset_state_checked(adapter->hooks.image_request(
            adapter->hooks.user, adapter->context, name, &image));
        if( (state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY) && image >= 0 )
            out->value = v2_resource_acquire(
                adapter->image_tokens,
                TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
                V2_RESOURCE_IMAGE,
                adapter->hooks.resource_namespace,
                image);
        else if( state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY )
            return TORIRS_ASSET_ERROR;
        if( (state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY) &&
            out->value == 0 )
            return TORIRS_ASSET_BUDGET;
        return state;
    }
    if( !adapter->legacy->image_load )
        return TORIRS_ASSET_ERROR;
    image = adapter->legacy->image_load(adapter->context, name);
    if( image < 0 )
        return TORIRS_ASSET_BUDGET;
    out->value = v2_resource_acquire(
        adapter->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        adapter->hooks.resource_namespace,
        image);
    if( out->value == 0 )
        return TORIRS_ASSET_BUDGET;
    if( adapter->legacy->image_size &&
        adapter->legacy->image_size(adapter->context, image, &width, &height) )
        return TORIRS_ASSET_READY;
    return TORIRS_ASSET_PENDING;
}

static bool
v2_assets_image_size(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ImageRef image,
    int* out_width,
    int* out_height)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(out_width);
    assert(out_height);
    int const legacy_image = ToriRS_PluginV2Adapter_ImageUnbox(adapter, image);
    if( legacy_image < 0 || !adapter->legacy->image_size )
        return false;
    return adapter->legacy->image_size(adapter->context, legacy_image, out_width, out_height) != 0;
}

static void
v2_assets_image_release(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ImageRef image)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int const legacy_image = ToriRS_PluginV2Adapter_ImageUnbox(adapter, image);
    if( legacy_image < 0 )
        return;
    if( adapter->hooks.image_release )
        adapter->hooks.image_release(adapter->hooks.user, adapter->context, image);
    else if( adapter->legacy->image_release )
        adapter->legacy->image_release(adapter->context, legacy_image);
    v2_resource_invalidate(
        adapter->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        adapter->hooks.resource_namespace,
        image.value);
}

static enum ToriRS_AssetState
v2_assets_model(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_ModelRef* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int model = -1;
    enum ToriRS_AssetState state;

    assert(name);
    assert(out);
    out->value = 0;
    if( !v2_asset_name_valid(name) )
        return TORIRS_ASSET_INVALID;
    if( adapter->hooks.model_request )
    {
        state = v2_asset_state_checked(adapter->hooks.model_request(
            adapter->hooks.user, adapter->context, name, &model));
        if( (state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY) && model >= 0 )
            out->value = v2_resource_acquire(
                adapter->model_tokens,
                TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
                V2_RESOURCE_MODEL,
                adapter->hooks.resource_namespace,
                model);
        else if( state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY )
            return TORIRS_ASSET_ERROR;
        if( (state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY) &&
            out->value == 0 )
            return TORIRS_ASSET_BUDGET;
        return state;
    }
    if( !adapter->legacy->model_load )
        return TORIRS_ASSET_ERROR;
    model = adapter->legacy->model_load(adapter->context, name);
    if( model < 0 )
        return TORIRS_ASSET_BUDGET;
    out->value = v2_resource_acquire(
        adapter->model_tokens,
        TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
        V2_RESOURCE_MODEL,
        adapter->hooks.resource_namespace,
        model);
    if( out->value == 0 )
        return TORIRS_ASSET_BUDGET;
    /* The old API has no model-ready query.  Resident source bytes are the
     * strongest state it can prove; otherwise the handle is still pending. */
    if( adapter->legacy->asset_data && adapter->legacy->asset_data(adapter->context, name, NULL) )
        return TORIRS_ASSET_READY;
    return TORIRS_ASSET_PENDING;
}

static void
v2_assets_model_release(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ModelRef model)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    if( ToriRS_PluginV2Adapter_ModelUnbox(adapter, model) < 0 )
        return;
    if( adapter->hooks.model_release )
        adapter->hooks.model_release(adapter->hooks.user, adapter->context, model);
    v2_resource_invalidate(
        adapter->model_tokens,
        TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
        V2_RESOURCE_MODEL,
        adapter->hooks.resource_namespace,
        model.value);
}

static enum ToriRS_Result
v2_assets_screenshot(
    struct ToriRS_ApiV2* api,
    char const* destination,
    char const* name,
    char* out_path,
    size_t out_path_size)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(name);
    assert(out_path);
    if( out_path_size == 0 || out_path_size > INT_MAX || !v2_asset_name_valid(name) )
        return TORIRS_RESULT_INVALID;
    if( !adapter->legacy->screenshot )
        return TORIRS_RESULT_UNSUPPORTED;
    return adapter->legacy->screenshot(
               adapter->context, destination, name, out_path, (int)out_path_size)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_ERROR;
}

static enum ToriRS_Result
v2_scene_mesh_create(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MeshRef* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int mesh;

    assert(out);
    out->value = 0;
    if( !adapter->legacy->mesh_create )
        return TORIRS_RESULT_UNSUPPORTED;
    mesh = adapter->legacy->mesh_create(adapter->context);
    if( mesh < 0 )
        return TORIRS_RESULT_BUDGET;
    out->value = v2_resource_acquire(
        adapter->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        adapter->hooks.resource_namespace,
        mesh);
    if( out->value == 0 )
    {
        if( adapter->legacy->mesh_destroy )
            adapter->legacy->mesh_destroy(adapter->context, mesh);
        return TORIRS_RESULT_BUDGET;
    }
    return TORIRS_RESULT_OK;
}

static void
v2_scene_mesh_destroy(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MeshRef mesh)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int const legacy_mesh = v2_resource_resolve(
        adapter->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        adapter->hooks.resource_namespace,
        mesh.value);
    if( legacy_mesh < 0 )
        return;
    if( adapter->legacy->mesh_destroy )
        adapter->legacy->mesh_destroy(adapter->context, legacy_mesh);
    v2_resource_invalidate(
        adapter->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        adapter->hooks.resource_namespace,
        mesh.value);
}

static enum ToriRS_Result
v2_scene_mesh_vertex(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MeshRef mesh,
    int x,
    int y,
    int z)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    int const legacy_mesh = v2_resource_resolve(
        adapter->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        adapter->hooks.resource_namespace,
        mesh.value);
    if( legacy_mesh < 0 )
        return TORIRS_RESULT_INVALID;
    if( !adapter->legacy->mesh_vertex )
        return TORIRS_RESULT_UNSUPPORTED;
    return adapter->legacy->mesh_vertex(adapter->context, legacy_mesh, x, y, z) >= 0
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_BUDGET;
}

static enum ToriRS_Result
v2_scene_mesh_face(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MeshRef mesh,
    int a,
    int b,
    int c,
    int hsl,
    int alpha)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    int const legacy_mesh = v2_resource_resolve(
        adapter->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        adapter->hooks.resource_namespace,
        mesh.value);
    if( legacy_mesh < 0 || a < 0 || b < 0 || c < 0 || alpha < 0 ||
        alpha > TORIRS_PLUGIN_MESH_ALPHA_MAX )
        return TORIRS_RESULT_INVALID;
    if( !adapter->legacy->mesh_face )
        return TORIRS_RESULT_UNSUPPORTED;
    return adapter->legacy->mesh_face(adapter->context, legacy_mesh, a, b, c, hsl, alpha) >= 0
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_BUDGET;
}

static enum ToriRS_Result
v2_scene_instance_create(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int instance;

    assert(out);
    out->value = 0;
    if( !adapter->legacy->object_create )
        return TORIRS_RESULT_UNSUPPORTED;
    instance = adapter->legacy->object_create(adapter->context);
    if( instance < 0 )
        return TORIRS_RESULT_BUDGET;
    out->value = v2_resource_acquire(
        adapter->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        adapter->hooks.resource_namespace,
        instance);
    if( out->value == 0 )
    {
        if( adapter->legacy->object_destroy )
            adapter->legacy->object_destroy(adapter->context, instance);
        return TORIRS_RESULT_BUDGET;
    }
    return TORIRS_RESULT_OK;
}

static void
v2_scene_instance_destroy(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int const legacy_instance = v2_resource_resolve(
        adapter->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        adapter->hooks.resource_namespace,
        instance.value);
    if( legacy_instance < 0 )
        return;
    if( adapter->legacy->object_destroy )
        adapter->legacy->object_destroy(adapter->context, legacy_instance);
    v2_resource_invalidate(
        adapter->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        adapter->hooks.resource_namespace,
        instance.value);
}

static enum ToriRS_Result
v2_scene_instance_model(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    struct ToriRS_ModelRef model)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    int const legacy_instance = v2_resource_resolve(
        adapter->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        adapter->hooks.resource_namespace,
        instance.value);
    int const legacy_model = ToriRS_PluginV2Adapter_ModelUnbox(adapter, model);
    if( legacy_instance < 0 || legacy_model < 0 )
        return TORIRS_RESULT_INVALID;
    if( !adapter->legacy->object_set_model )
        return TORIRS_RESULT_UNSUPPORTED;
    adapter->legacy->object_set_model(
        adapter->context, legacy_instance, TORIRS_PLUGIN_MODEL_ASSET, legacy_model);
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
v2_scene_instance_position(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    int const legacy_instance = v2_resource_resolve(
        adapter->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        adapter->hooks.resource_namespace,
        instance.value);
    if( legacy_instance < 0 || level < 0 || yaw < 0 || yaw > 2047 )
        return TORIRS_RESULT_INVALID;
    if( !adapter->legacy->object_set_position )
        return TORIRS_RESULT_UNSUPPORTED;
    adapter->legacy->object_set_position(
        adapter->context, legacy_instance, tile_x, tile_z, level, height, yaw);
    return TORIRS_RESULT_OK;
}

static void
v2_scene_instance_active(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    bool active)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int const legacy_instance = v2_resource_resolve(
        adapter->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        adapter->hooks.resource_namespace,
        instance.value);
    if( legacy_instance >= 0 && adapter->legacy->object_set_active )
        adapter->legacy->object_set_active(adapter->context, legacy_instance, active ? 1 : 0);
}

static enum ToriRS_Result
v2_panel_request(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginPanelDesc const* description)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);

    assert(description);
    if( !adapter->legacy->panel_request )
        return TORIRS_RESULT_UNSUPPORTED;
    return adapter->legacy->panel_request(adapter->context, description) ? TORIRS_RESULT_OK
                                                                         : TORIRS_RESULT_ERROR;
}

static void
v2_panel_invalidate(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    if( adapter->legacy->panel_clear )
        adapter->legacy->panel_clear(adapter->context);
}

static void
v2_panel_attention(
    struct ToriRS_ApiV2* api,
    bool wanted)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    if( adapter->legacy->panel_set_attention )
        (void)adapter->legacy->panel_set_attention(adapter->context, wanted);
}

static int
v2_cache_frame_root(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->frame_root ? adapter->legacy->frame_root(adapter->context) : -1;
}

static int
v2_cache_varbit(
    struct ToriRS_ApiV2* api,
    int id)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->varbit ? adapter->legacy->varbit(adapter->context, id) : 0;
}

static int
v2_cache_varp(
    struct ToriRS_ApiV2* api,
    int id)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    return adapter->legacy->varp ? adapter->legacy->varp(adapter->context, id) : 0;
}

static bool
v2_cache_component_rect(
    struct ToriRS_ApiV2* api,
    int component_id,
    struct ToriRS_Rect* out)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    int x;
    int y;
    int width;
    int height;

    assert(out);
    if( !adapter->legacy->component_rect ||
        !adapter->legacy->component_rect(adapter->context, component_id, &x, &y, &width, &height) )
        return false;
    *out = (struct ToriRS_Rect){ x, y, width, height };
    return true;
}

static bool
v2_cache_invoke(
    struct ToriRS_ApiV2* api,
    int component_id,
    int op)
{
    struct ToriRS_PluginV2Adapter* adapter = v2_adapter(api);
    if( component_id < 0 || op < 0 || op > 10 || !adapter->legacy->if_click )
        return false;
    return adapter->legacy->if_click(adapter->context, component_id, op) != 0;
}

static struct ToriRS_PluginV2DrawScope*
v2_draw_scope(struct ToriRS_DrawBuilder* draw)
{
    struct ToriRS_PluginV2DrawScope* scope;

    assert(draw);
    scope = draw->implementation;
    assert(scope);
    assert(scope->active);
    assert(scope->adapter);
    return scope;
}

static int
v2_alpha(int alpha)
{
    if( alpha < 0 )
        return 0;
    if( alpha > 255 )
        return 255;
    return alpha;
}

static bool
v2_clip_rect(
    struct ToriRS_PluginV2DrawScope const* scope,
    struct ToriRS_Rect* rect)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    assert(scope);
    assert(rect);
    if( rect->width <= 0 || rect->height <= 0 )
        return false;
    if( !scope->clip_active )
        return true;
    left = rect->x > scope->clip.x ? rect->x : scope->clip.x;
    top = rect->y > scope->clip.y ? rect->y : scope->clip.y;
    right = (int64_t)rect->x + rect->width;
    if( right > (int64_t)scope->clip.x + scope->clip.width )
        right = (int64_t)scope->clip.x + scope->clip.width;
    bottom = (int64_t)rect->y + rect->height;
    if( bottom > (int64_t)scope->clip.y + scope->clip.height )
        bottom = (int64_t)scope->clip.y + scope->clip.height;
    if( right <= left || bottom <= top )
        return false;
    *rect = (struct ToriRS_Rect){ (int)left, (int)top, (int)(right - left), (int)(bottom - top) };
    return true;
}

static bool
v2_clip_line(
    struct ToriRS_PluginV2DrawScope const* scope,
    int* x0,
    int* y0,
    int* x1,
    int* y1)
{
    double t0 = 0.0;
    double t1 = 1.0;
    double const dx = (double)*x1 - *x0;
    double const dy = (double)*y1 - *y0;
    double const p[4] = { -dx, dx, -dy, dy };
    double const q[4] = {
        (double)*x0 - scope->clip.x,
        (double)scope->clip.x + scope->clip.width - *x0,
        (double)*y0 - scope->clip.y,
        (double)scope->clip.y + scope->clip.height - *y0,
    };
    int const original_x = *x0;
    int const original_y = *y0;

    assert(scope);
    if( !scope->clip_active )
        return true;
    if( scope->clip.width <= 0 || scope->clip.height <= 0 )
        return false;
    for( int i = 0; i < 4; i++ )
    {
        if( p[i] == 0.0 )
        {
            if( q[i] < 0.0 )
                return false;
            continue;
        }
        double const ratio = q[i] / p[i];
        if( p[i] < 0.0 )
        {
            if( ratio > t1 )
                return false;
            if( ratio > t0 )
                t0 = ratio;
        }
        else
        {
            if( ratio < t0 )
                return false;
            if( ratio < t1 )
                t1 = ratio;
        }
    }
    *x0 = original_x + (int)(dx * t0);
    *y0 = original_y + (int)(dy * t0);
    *x1 = original_x + (int)(dx * t1);
    *y1 = original_y + (int)(dy * t1);
    return true;
}

static void
v2_builder_rect(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_Rect rect,
    uint32_t rgb,
    int alpha)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;

    if( v2_clip_rect(scope, &rect) && legacy->draw_rect )
        legacy->draw_rect(
            scope->adapter->context,
            scope->legacy_surface,
            rect.x,
            rect.y,
            rect.width,
            rect.height,
            rgb,
            v2_alpha(alpha));
}

static void
v2_builder_line(
    struct ToriRS_DrawBuilder* draw,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t rgb,
    int alpha)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;

    /* v1 has no partially-transparent line.  Fully transparent is still a
     * no-op; every visible alpha uses its exact geometry and colour. */
    if( v2_alpha(alpha) > 0 && v2_clip_line(scope, &x0, &y0, &x1, &y1) &&
        legacy->draw_line )
        legacy->draw_line(scope->adapter->context, scope->legacy_surface, x0, y0, x1, y1, rgb);
}

static void
v2_builder_text(
    struct ToriRS_DrawBuilder* draw,
    int x,
    int y,
    char const* text,
    uint32_t rgb)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;

    assert(text);
    if( scope->clip_active &&
        (x < scope->clip.x || y < scope->clip.y ||
         x >= scope->clip.x + scope->clip.width || y >= scope->clip.y + scope->clip.height) )
        return;
    if( legacy->draw_text )
        legacy->draw_text(scope->adapter->context, scope->legacy_surface, x, y, text, rgb);
}

static void
v2_builder_image(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    int alpha)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;
    int const opaque_alpha = v2_alpha(alpha);
    int const legacy_image = ToriRS_PluginV2Adapter_ImageUnbox(scope->adapter, image);

    if( legacy_image < 0 || opaque_alpha == 0 || !legacy->draw_image )
        return;
    legacy->draw_image(
        scope->adapter->context,
        scope->legacy_surface,
        legacy_image,
        x,
        y,
        scope->clip_active ? scope->clip.x : 0,
        scope->clip_active ? scope->clip.y : 0,
        scope->clip_active ? scope->clip.width : 0,
        scope->clip_active ? scope->clip.height : 0,
        255 - opaque_alpha);
}

static enum ToriRS_Result
v2_builder_world_tile(
    struct ToriRS_DrawBuilder* draw,
    int tile_x,
    int tile_z,
    int level,
    uint32_t fill_rgb,
    uint32_t outline_rgb,
    int alpha)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;

    if( !legacy->draw_tile )
        return TORIRS_RESULT_UNSUPPORTED;
    legacy->draw_tile(
        scope->adapter->context,
        scope->legacy_surface,
        tile_x,
        tile_z,
        level,
        outline_rgb,
        fill_rgb,
        v2_alpha(alpha));
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
v2_builder_world_hull(
    struct ToriRS_DrawBuilder* draw,
    int element_id,
    uint32_t rgb,
    int alpha,
    int shape)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;

    if( shape != TORIRS_PLUGIN_HULL_BOUNDS && shape != TORIRS_PLUGIN_HULL_MESH )
        return TORIRS_RESULT_INVALID;
    if( !legacy->draw_hull )
        return TORIRS_RESULT_UNSUPPORTED;
    legacy->draw_hull(
        scope->adapter->context, scope->legacy_surface, element_id, rgb, v2_alpha(alpha), shape);
    return TORIRS_RESULT_OK;
}

static uint32_t
v2_action_tag(char const* action)
{
    uint32_t hash = 2166136261u;

    assert(action);
    while( *action )
    {
        hash ^= (unsigned char)*action++;
        hash *= 16777619u;
    }
    hash &= 0x7fffffffu;
    return hash ? hash : 1;
}

static enum ToriRS_Result
v2_builder_action_region(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_Rect rect,
    char const* action)
{
    struct ToriRS_PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_PluginApi const* legacy = scope->adapter->legacy;
    char const* operations[1];

    assert(action);
    if( !action[0] || strlen(action) >= TORIRS_UI_ACTION_MAX || !v2_clip_rect(scope, &rect) )
        return TORIRS_RESULT_INVALID;
    if( !legacy->hit_region )
        return TORIRS_RESULT_UNSUPPORTED;
    operations[0] = action;
    return legacy->hit_region(
               scope->adapter->context,
               scope->legacy_surface,
               rect.x,
               rect.y,
               rect.width,
               rect.height,
               operations,
               1,
               v2_action_tag(action))
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_BUDGET;
}

void
ToriRS_PluginV2Adapter_DrawBegin(
    struct ToriRS_PluginV2Adapter* adapter,
    void* legacy_surface,
    struct ToriRS_PluginV2DrawScope* scope,
    struct ToriRS_DrawBuilder* out)
{
    assert(adapter);
    assert(legacy_surface);
    assert(scope);
    assert(out);
    memset(scope, 0, sizeof(*scope));
    scope->adapter = adapter;
    scope->legacy_surface = legacy_surface;
    scope->active = true;
    *out = (struct ToriRS_DrawBuilder){
        .struct_size = sizeof(*out),
        .implementation = scope,
        .rect = v2_builder_rect,
        .line = v2_builder_line,
        .text = v2_builder_text,
        .image = v2_builder_image,
        .world_tile = v2_builder_world_tile,
        .world_hull = v2_builder_world_hull,
        .action_region = v2_builder_action_region,
    };
}

void
ToriRS_PluginV2Adapter_DrawEnd(
    struct ToriRS_PluginV2DrawScope* scope,
    struct ToriRS_DrawBuilder* builder)
{
    assert(scope);
    assert(builder);
    assert(scope->active);
    assert(builder->implementation == scope);
    scope->active = false;
    builder->implementation = NULL;
}

void
ToriRS_PluginV2Adapter_DrawClip(
    struct ToriRS_PluginV2DrawScope* scope,
    struct ToriRS_Rect clip)
{
    assert(scope);
    assert(scope->active);
    scope->clip_active = clip.width > 0 && clip.height > 0;
    scope->clip = clip;
}

static struct ToriRS_PluginV2FrameScope*
v2_frame_scope(struct ToriRS_FrameBuilder* frame)
{
    struct ToriRS_PluginV2FrameScope* scope;

    assert(frame);
    scope = frame->implementation;
    assert(scope);
    assert(scope->active);
    assert(scope->adapter);
    return scope;
}

static int
v2_surface_to_legacy(int surface)
{
    static int const SURFACE[TORIRS_SURFACE_COUNT] = {
        TORIRS_PLUGIN_SLOT_VIEWPORT,
        TORIRS_PLUGIN_SLOT_MINIMAP,
        TORIRS_PLUGIN_SLOT_SIDEBAR,
        TORIRS_PLUGIN_SLOT_CHAT,
        TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,
        TORIRS_PLUGIN_SLOT_MAIN_MODAL,
        TORIRS_PLUGIN_SLOT_COMPASS,
        TORIRS_PLUGIN_SLOT_ORBS,
    };

    if( surface < 0 || surface >= TORIRS_SURFACE_COUNT )
        return -1;
    return SURFACE[surface];
}

static bool
v2_frame_rect_valid(struct ToriRS_Rect const* rect)
{
    int64_t right;
    int64_t bottom;

    assert(rect);
    if( rect->width <= 0 || rect->height <= 0 )
        return false;
    right = (int64_t)rect->x + rect->width;
    bottom = (int64_t)rect->y + rect->height;
    return right >= INT_MIN && right <= INT_MAX && bottom >= INT_MIN && bottom <= INT_MAX;
}

static bool
v2_frame_image(
    struct ToriRS_PluginV2FrameScope* scope,
    struct ToriRS_ImageRef ref,
    int* out_legacy)
{
    int legacy = -1;

    assert(scope);
    assert(out_legacy);
    if( ref.value != 0 )
    {
        legacy = ToriRS_PluginV2Adapter_ImageUnbox(scope->adapter, ref);
        if( legacy < 0 )
        {
            scope->invalid = true;
            return false;
        }
        for( int i = 0; i < scope->image_ref_count; i++ )
            if( scope->image_refs[i].value == ref.value )
            {
                *out_legacy = legacy;
                return true;
            }
        if( scope->image_ref_count >= TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX )
        {
            scope->invalid = true;
            return false;
        }
        scope->image_refs[scope->image_ref_count++] = ref;
    }
    *out_legacy = legacy;
    return true;
}

static bool
v2_frame_ui_node_images(
    struct ToriRS_PluginV2FrameScope* scope,
    struct ToriRS_UiNode const* node)
{
    size_t const size = node->struct_size ? node->struct_size : TORIRS_UI_NODE_LEGACY_SIZE;
    int legacy;

    assert(scope);
    assert(node);
    if( !v2_ui_node_images_valid(scope->adapter, node) )
        return false;
    if( size >= offsetof(struct ToriRS_UiNode, image) + sizeof(node->image) &&
        node->image.value != 0 && !v2_frame_image(scope, node->image, &legacy) )
        return false;
    if( size >= offsetof(struct ToriRS_UiNode, state_images) + sizeof(node->state_images) )
        for( int state = 0; state < TORIRS_UI_VISUAL_STATE_COUNT; state++ )
            if( (node->state_image_mask & (1u << state)) != 0 &&
                node->state_images[state].value != 0 &&
                !v2_frame_image(scope, node->state_images[state], &legacy) )
                return false;
    return true;
}

static void
v2_builder_surface(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    struct ToriRS_Rect rect)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const legacy_surface = v2_surface_to_legacy(surface);

    if( legacy_surface < 0 || !v2_frame_rect_valid(&rect) ||
        (scope->surface_mask & (1u << surface)) != 0 )
    {
        scope->invalid = true;
        return;
    }
    scope->surface_mask |= 1u << surface;
    if( scope->adapter->legacy->layout_slot )
        (void)scope->adapter->legacy->layout_slot(
            scope->adapter->context, legacy_surface, rect.x, rect.y, rect.width, rect.height);
}

static void
v2_builder_surface_member(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    int member,
    struct ToriRS_Rect rect)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const legacy_surface = v2_surface_to_legacy(surface);

    if( legacy_surface < 0 || member < 0 || !v2_frame_rect_valid(&rect) ||
        scope->member_count >= (int)(sizeof(scope->members) / sizeof(scope->members[0])) )
    {
        scope->invalid = true;
        return;
    }
    for( int i = 0; i < scope->member_count; i++ )
        if( scope->members[i].surface == surface && scope->members[i].member == member )
        {
            scope->invalid = true;
            return;
        }
    scope->members[scope->member_count].surface = surface;
    scope->members[scope->member_count].member = member;
    scope->member_count++;
    if( scope->adapter->legacy->layout_slot_at )
        (void)scope->adapter->legacy->layout_slot_at(
            scope->adapter->context,
            legacy_surface,
            member,
            rect.x,
            rect.y,
            rect.width,
            rect.height);
}

static void
v2_builder_skin(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    struct ToriRS_FrameSkin const* skin)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const legacy_surface = v2_surface_to_legacy(surface);
    int image = -1;
    int mask = -1;

    if( !skin || legacy_surface < 0 ||
        (skin->struct_size != 0 && skin->struct_size < sizeof(*skin)) )
    {
        scope->invalid = true;
        return;
    }
    if( !v2_frame_image(scope, skin->image, &image) ||
        !v2_frame_image(scope, skin->mask, &mask) )
        return;
    if( scope->adapter->legacy->layout_slot_skin )
        (void)scope->adapter->legacy->layout_slot_skin(
            scope->adapter->context, legacy_surface, image, mask);
}

static void
v2_builder_surface_overlay(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    struct ToriRS_FrameSurfaceOverlay const* overlay)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const legacy_surface = v2_surface_to_legacy(surface);
    int image = -1;

    if( !overlay || legacy_surface < 0 ||
        (scope->surface_mask & (1u << surface)) == 0 ||
        (overlay->struct_size != 0 && overlay->struct_size < sizeof(*overlay)) ||
        overlay->image.value == 0 || overlay->alpha < 0 || overlay->alpha > 255 )
    {
        scope->invalid = true;
        return;
    }
    if( !v2_frame_image(scope, overlay->image, &image) )
        return;
    if( scope->adapter->legacy->layout_slot_overlay )
        (void)scope->adapter->legacy->layout_slot_overlay(
            scope->adapter->context,
            legacy_surface,
            image,
            overlay->x,
            overlay->y,
            255 - overlay->alpha);
}

static void
v2_builder_ui_node(
    struct ToriRS_FrameBuilder* frame,
    char const* name,
    struct ToriRS_UiNode const* node)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);

    assert(name);
    assert(node);
    if( !v2_frame_ui_node_images(scope, node) )
    {
        scope->invalid = true;
        return;
    }
    if( scope->adapter->hooks.frame_ui_node )
        scope->adapter->hooks.frame_ui_node(
            scope->adapter->hooks.user, scope->adapter->context, name, node);
}

static void
v2_builder_scrollbar(
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameScrollbar const* skin)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);
    struct ToriRS_ImageRef refs[6];
    int images[6];
    int top;
    int middle;
    int bottom;

    if( !skin ||
        (skin->struct_size != 0 && skin->struct_size < TORIRS_FRAME_SCROLLBAR_LEGACY_SIZE) )
    {
        scope->invalid = true;
        return;
    }
    refs[0] = skin->track;
    refs[1] = skin->thumb;
    refs[2] = skin->thumb;
    refs[3] = skin->thumb;
    refs[4] = skin->up;
    refs[5] = skin->down;
    if( skin->struct_size >= sizeof(*skin) && skin->split_thumb )
    {
        refs[1] = skin->thumb_top;
        refs[2] = skin->thumb_middle;
        refs[3] = skin->thumb_bottom;
    }
    for( int i = 0; i < 6; i++ )
        if( !v2_frame_image(scope, refs[i], &images[i]) )
            return;
    top = images[1];
    middle = images[2];
    bottom = images[3];
    if( scope->adapter->legacy->layout_scrollbar )
        (void)scope->adapter->legacy->layout_scrollbar(
            scope->adapter->context,
            images[0],
            top,
            middle,
            bottom,
            images[4],
            images[5]);
}

static void
v2_builder_reason(
    struct ToriRS_FrameBuilder* frame,
    char const* reason)
{
    struct ToriRS_PluginV2FrameScope* scope = v2_frame_scope(frame);

    assert(reason);
    (void)snprintf(scope->reason, sizeof(scope->reason), "%s", reason);
    if( scope->adapter->hooks.frame_reason )
        scope->adapter->hooks.frame_reason(
            scope->adapter->hooks.user, scope->adapter->context, scope->reason);
}

void
ToriRS_PluginV2Adapter_FrameBegin(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginV2FrameScope* scope,
    struct ToriRS_FrameBuilder* out)
{
    assert(adapter);
    assert(scope);
    assert(out);
    memset(scope, 0, sizeof(*scope));
    scope->adapter = adapter;
    scope->active = true;
    *out = (struct ToriRS_FrameBuilder){
        .struct_size = sizeof(*out),
        .implementation = scope,
        .surface = v2_builder_surface,
        .surface_member = v2_builder_surface_member,
        .skin = v2_builder_skin,
        .surface_overlay = v2_builder_surface_overlay,
        .ui_node = v2_builder_ui_node,
        .scrollbar = v2_builder_scrollbar,
        .reason = v2_builder_reason,
    };
}

void
ToriRS_PluginV2Adapter_FrameEnd(
    struct ToriRS_PluginV2FrameScope* scope,
    struct ToriRS_FrameBuilder* builder)
{
    assert(scope);
    assert(builder);
    assert(scope->active);
    assert(builder->implementation == scope);
    scope->active = false;
    builder->implementation = NULL;
}

char const*
ToriRS_PluginV2Adapter_FrameReason(struct ToriRS_PluginV2FrameScope const* scope)
{
    assert(scope);
    return scope->reason;
}

bool
ToriRS_PluginV2Adapter_FrameValid(struct ToriRS_PluginV2FrameScope const* scope)
{
    assert(scope);
    if( scope->invalid || (scope->surface_mask & (1u << TORIRS_SURFACE_VIEWPORT)) == 0 )
        return false;
    for( int i = 0; i < scope->image_ref_count; i++ )
        if( ToriRS_PluginV2Adapter_ImageUnbox(scope->adapter, scope->image_refs[i]) < 0 )
            return false;
    return true;
}

static struct ToriRS_PluginV2PanelScope*
v2_panel_scope(struct ToriRS_PanelBuilder* panel)
{
    struct ToriRS_PluginV2PanelScope* scope;

    assert(panel);
    scope = panel->implementation;
    assert(scope);
    assert(scope->active);
    assert(scope->adapter);
    return scope;
}

static void
v2_panel_generated_id(
    struct ToriRS_PluginV2PanelScope* scope,
    char const* kind,
    char* out,
    size_t out_size)
{
    assert(scope);
    assert(kind);
    assert(out);
    assert(out_size > 0);
    (void)snprintf(out, out_size, "_v2_%s_%d", kind, scope->generated_id++);
}

static void
v2_builder_heading(
    struct ToriRS_PanelBuilder* panel,
    char const* text)
{
    struct ToriRS_PluginV2PanelScope* scope = v2_panel_scope(panel);
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

    assert(text);
    v2_panel_generated_id(scope, "heading", id, sizeof(id));
    if( scope->adapter->legacy->panel_widget )
        (void)scope->adapter->legacy->panel_widget(
            scope->adapter->context, TORIRS_PLUGIN_W_SECTION, id, text);
}

static void
v2_builder_paragraph(
    struct ToriRS_PanelBuilder* panel,
    char const* text)
{
    struct ToriRS_PluginV2PanelScope* scope = v2_panel_scope(panel);
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

    assert(text);
    v2_panel_generated_id(scope, "paragraph", id, sizeof(id));
    if( scope->adapter->legacy->panel_widget )
        (void)scope->adapter->legacy->panel_widget(
            scope->adapter->context, TORIRS_PLUGIN_W_PARAGRAPH, id, text);
}

static void
v2_builder_toggle(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    bool value)
{
    struct ToriRS_PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    if( scope->adapter->legacy->panel_widget &&
        scope->adapter->legacy->panel_widget(
            scope->adapter->context, TORIRS_PLUGIN_W_TOGGLE, id, label) &&
        scope->adapter->legacy->panel_set_value )
        (void)scope->adapter->legacy->panel_set_value(scope->adapter->context, id, value ? 1 : 0);
}

static bool
v2_panel_options_valid(
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    if( option_count < 0 || option_count > 128 || (option_count > 0 && !options) )
        return false;
    for( int i = 0; i < option_count; i++ )
    {
        if( options[i].struct_size < TORIRS_SELECT_OPTION_REQUIRED_SIZE || !options[i].value ||
            !options[i].value[0] || !options[i].label )
            return false;
        for( int j = 0; j < i; j++ )
            if( strcmp(options[i].value, options[j].value) == 0 )
                return false;
    }
    return true;
}

static bool
v2_panel_legacy_choices(
    struct ToriRS_SelectOption const* options,
    int option_count,
    char* out,
    size_t out_size)
{
    size_t used = 0;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !v2_panel_options_valid(options, option_count) )
        return false;
    for( int i = 0; i < option_count; i++ )
    {
        char const* label = options[i].label ? options[i].label : "";
        size_t const length = strlen(label);
        if( strchr(label, '|') || used + length + (i ? 1u : 0u) >= out_size )
            return false;
        if( i )
            out[used++] = '|';
        memcpy(out + used, label, length);
        used += length;
        out[used] = '\0';
    }
    return true;
}

static void
v2_builder_select(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    struct ToriRS_PluginV2PanelScope* scope = v2_panel_scope(panel);
    struct ToriRS_PluginV2Adapter* adapter = scope->adapter;
    char choices[V2_PANEL_CHOICES_MAX];
    int selected = -1;

    assert(id);
    assert(value);
    if( !v2_panel_options_valid(options, option_count) )
        return;
    if( adapter->hooks.panel_select )
    {
        adapter->hooks.panel_select(
            adapter->hooks.user, adapter->context, id, label, value, options, option_count);
        return;
    }
    if( !adapter->legacy->panel_widget )
        return;
    for( int i = 0; i < option_count; i++ )
        if( options[i].value && strcmp(options[i].value, value) == 0 )
            selected = i;
    if( !v2_panel_legacy_choices(options, option_count, choices, sizeof(choices)) )
        return;
    if( adapter->legacy->panel_widget(adapter->context, TORIRS_PLUGIN_W_DROPDOWN, id, label) &&
        adapter->legacy->panel_set_options )
        (void)adapter->legacy->panel_set_options(adapter->context, id, choices, selected);
}

static void
v2_builder_button(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    bool enabled)
{
    struct ToriRS_PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    if( scope->adapter->legacy->panel_widget &&
        scope->adapter->legacy->panel_widget(
            scope->adapter->context, TORIRS_PLUGIN_W_BUTTON, id, label) &&
        scope->adapter->legacy->panel_set_value )
        (void)scope->adapter->legacy->panel_set_value(scope->adapter->context, id, enabled ? 1 : 0);
}

static void
v2_builder_custom(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    int preferred_height)
{
    struct ToriRS_PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    if( scope->adapter->legacy->panel_widget &&
        scope->adapter->legacy->panel_widget(
            scope->adapter->context, TORIRS_PLUGIN_W_CUSTOM, id, NULL) &&
        scope->adapter->legacy->panel_set_height )
        (void)scope->adapter->legacy->panel_set_height(
            scope->adapter->context, id, preferred_height);
}

void
ToriRS_PluginV2Adapter_PanelBegin(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* out)
{
    assert(adapter);
    assert(scope);
    assert(out);
    memset(scope, 0, sizeof(*scope));
    scope->adapter = adapter;
    scope->active = true;
    *out = (struct ToriRS_PanelBuilder){
        .struct_size = sizeof(*out),
        .implementation = scope,
        .heading = v2_builder_heading,
        .paragraph = v2_builder_paragraph,
        .toggle = v2_builder_toggle,
        .select = v2_builder_select,
        .button = v2_builder_button,
        .custom = v2_builder_custom,
    };
}

void
ToriRS_PluginV2Adapter_PanelEnd(
    struct ToriRS_PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* builder)
{
    assert(scope);
    assert(builder);
    assert(scope->active);
    assert(builder->implementation == scope);
    scope->active = false;
    builder->implementation = NULL;
}

bool
ToriRS_PluginV2Adapter_Init(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginApi const* legacy,
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginV2AdapterHooks const* hooks)
{
    size_t hook_size = 0;

    assert(adapter);
    assert(legacy);
    assert(context);
    if( legacy->abi_version != TORIRS_PLUGIN_ABI )
        return false;
    if( hooks )
    {
        size_t const minimum =
            offsetof(struct ToriRS_PluginV2AdapterHooks, user) + sizeof(hooks->user);
        if( hooks->struct_size < minimum )
            return false;
        hook_size = hooks->struct_size < sizeof(*hooks) ? hooks->struct_size : sizeof(*hooks);
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->legacy = legacy;
    adapter->context = context;
    if( hooks )
        memcpy(&adapter->hooks, hooks, hook_size);
    if( adapter->hooks.resource_namespace > V2_RESOURCE_NAMESPACE_MASK )
    {
        memset(adapter, 0, sizeof(*adapter));
        return false;
    }

    adapter->api = (struct ToriRS_ApiV2){
        .struct_size = sizeof(adapter->api),
        .major_version = TORIRS_PLUGIN_API_V2_MAJOR,
        .minor_version = TORIRS_PLUGIN_API_V2_MINOR,
        .instance = adapter,
        .core = {
            .struct_size = sizeof(adapter->api.core),
            .log = v2_core_log,
            .notify = v2_core_notify,
            .screen = v2_core_screen,
            .frame_ms = v2_core_frame_ms,
            .frame_work_us = v2_core_frame_work_us,
            .lane = v2_core_lane,
            .capability = v2_core_capability,
        },
        .config = {
            .struct_size = sizeof(adapter->api.config),
            .has = v2_config_has,
            .get_bool = v2_config_get_bool,
            .get_int = v2_config_get_int,
            .get_color = v2_config_get_color,
            .get_string = v2_config_get_string,
            .set = v2_config_set,
        },
        .world = {
            .struct_size = sizeof(adapter->api.world),
            .local_player = v2_world_local_player,
            .npc_next = v2_world_npc_next,
            .npc_by_slot = v2_world_npc_by_slot,
            .player_next = v2_world_player_next,
            .item_next = v2_world_item_next,
            .scenery_next = v2_world_scenery_next,
        },
        .input = {
            .struct_size = sizeof(adapter->api.input),
            .key_held = v2_input_key_held,
            .pointer = v2_input_pointer,
            .hover_tile = v2_input_hover_tile,
            .hover_entity = v2_input_hover_entity,
            .text_input = v2_input_text_input,
        },
        .ui = {
            .struct_size = sizeof(adapter->api.ui),
            .ref = v2_ui_ref,
            .info = v2_ui_info,
            .invoke = v2_ui_invoke,
            .contribution_info = v2_ui_contribution_info,
            .update = v2_ui_update,
        },
        .placement = {
            .struct_size = sizeof(adapter->api.placement),
            .revision = v2_placement_revision,
            .area = v2_placement_area,
            .primary = v2_placement_primary,
            .place = v2_placement_place,
            .rect_next = v2_placement_rect_next,
            .contains = v2_placement_contains,
            .reserve = v2_placement_reserve,
            .reservation_rect = v2_placement_reservation_rect,
        },
        .frame = {
            .struct_size = sizeof(adapter->api.frame),
            .offer_next = v2_frame_offer_next,
            .selection = v2_frame_selection,
            .select = v2_frame_select,
            .invalidate = v2_frame_invalidate,
        },
        .draw = {
            .struct_size = sizeof(adapter->api.draw),
            .project = v2_draw_project,
            .element_height = v2_draw_element_height,
            .hsl_from_rgb = v2_draw_hsl_from_rgb,
            .hsl_to_rgb = v2_draw_hsl_to_rgb,
        },
        .assets = {
            .struct_size = sizeof(adapter->api.assets),
            .request = v2_assets_request,
            .bytes = v2_assets_bytes,
            .save = v2_assets_save,
            .release = v2_assets_release,
            .image = v2_assets_image,
            .image_size = v2_assets_image_size,
            .image_release = v2_assets_image_release,
            .model = v2_assets_model,
            .model_release = v2_assets_model_release,
            .screenshot = v2_assets_screenshot,
        },
        .scene = {
            .struct_size = sizeof(adapter->api.scene),
            .mesh_create = v2_scene_mesh_create,
            .mesh_destroy = v2_scene_mesh_destroy,
            .mesh_vertex = v2_scene_mesh_vertex,
            .mesh_face = v2_scene_mesh_face,
            .instance_create = v2_scene_instance_create,
            .instance_destroy = v2_scene_instance_destroy,
            .instance_model = v2_scene_instance_model,
            .instance_position = v2_scene_instance_position,
            .instance_active = v2_scene_instance_active,
        },
        .panel = {
            .struct_size = sizeof(adapter->api.panel),
            .request = v2_panel_request,
            .invalidate = v2_panel_invalidate,
            .attention = v2_panel_attention,
        },
        .cache = {
            .struct_size = sizeof(adapter->api.cache),
            .frame_root = v2_cache_frame_root,
            .varbit = v2_cache_varbit,
            .varp = v2_cache_varp,
            .component_rect = v2_cache_component_rect,
            .invoke = v2_cache_invoke,
        },
    };
    return true;
}

struct ToriRS_ApiV2*
ToriRS_PluginV2Adapter_Api(struct ToriRS_PluginV2Adapter* adapter)
{
    assert(adapter);
    return &adapter->api;
}
