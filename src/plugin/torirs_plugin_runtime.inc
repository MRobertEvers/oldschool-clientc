#include "plugin/torirs_plugin_runtime.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
_Static_assert(
    TORIRS_PLUGIN_V2_FRAME_SKIN_SURFACES_MAX == 2,
    "frame dependency formula matches minimap and compass skin policy");
_Static_assert(
    TORIRS_PLUGIN_V2_FRAME_SCROLLBAR_REFS_MAX == 6,
    "frame dependency formula matches the six-piece engine_slot scrollbar");
_Static_assert(
    TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX <= TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
    "one legal frame dependency set fits the runtime image-token table");

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
 * token for a repeated request and reuse a retired engine_slot handle safely. */
static int
v2_resource_acquire(
    struct PluginV2ResourceToken* entries,
    int count,
    enum V2ResourceKind kind,
    uint32_t resource_namespace,
    int engine_slot)
{
    int free_slot = -1;

    assert(entries);
    if( engine_slot < 0 )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        struct PluginV2ResourceToken* entry = &entries[i];
        if( entry->active && entry->slot == engine_slot )
            return v2_resource_encode(i, entry->incarnation, kind, resource_namespace);
        if( !entry->active && !entry->retired && free_slot < 0 )
            free_slot = i;
    }
    if( free_slot < 0 )
        return 0;
    /* Incarnation zero is reserved so even plugin namespace zero's first
     * token can never be mistaken for the historical engine_slot-slot + 1 box. */
    if( entries[free_slot].incarnation == 0 )
        entries[free_slot].incarnation = 1;
    entries[free_slot].slot = engine_slot;
    entries[free_slot].source_revision = 0;
    entries[free_slot].active = true;
    return v2_resource_encode(
        free_slot, entries[free_slot].incarnation, kind, resource_namespace);
}

/** Acquire an evictable host resource whose slot can change identity. */
static int
v2_resource_acquire_versioned(
    struct PluginV2ResourceToken* entries,
    int count,
    enum V2ResourceKind kind,
    uint32_t resource_namespace,
    int engine_slot,
    uint64_t source_revision)
{
    int free_slot = -1;

    assert(entries);
    if( engine_slot < 0 || source_revision == 0 )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        struct PluginV2ResourceToken* entry = &entries[i];
        if( entry->active && entry->slot == engine_slot )
        {
            if( entry->source_revision == source_revision )
                return v2_resource_encode(
                    i, entry->incarnation, kind, resource_namespace);
            entry->active = false;
            entry->slot = -1;
            entry->source_revision = 0;
            if( entry->incarnation == TORIRS_PLUGIN_V2_RESOURCE_INCAR_MAX )
                entry->retired = true;
            else
                entry->incarnation++;
        }
        if( !entry->active && !entry->retired && free_slot < 0 )
            free_slot = i;
    }
    if( free_slot < 0 )
        return 0;
    if( entries[free_slot].incarnation == 0 )
        entries[free_slot].incarnation = 1;
    entries[free_slot].slot = engine_slot;
    entries[free_slot].source_revision = source_revision;
    entries[free_slot].active = true;
    return v2_resource_encode(
        free_slot, entries[free_slot].incarnation, kind, resource_namespace);
}

static int
v2_resource_resolve(
    struct PluginV2ResourceToken const* entries,
    int count,
    enum V2ResourceKind kind,
    uint32_t resource_namespace,
    int value)
{
    struct PluginV2ResourceToken const* entry;
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
    return entry->slot;
}

static void
v2_resource_invalidate(
    struct PluginV2ResourceToken* entries,
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
    entries[slot].slot = -1;
    entries[slot].source_revision = 0;
    if( entries[slot].incarnation == TORIRS_PLUGIN_V2_RESOURCE_INCAR_MAX )
        entries[slot].retired = true;
    else
        entries[slot].incarnation++;
}

static void
v2_resource_reset(
    struct PluginV2ResourceToken* entries,
    int count)
{
    assert(entries);
    for( int i = 0; i < count; i++ )
    {
        struct PluginV2ResourceToken* entry = &entries[i];
        if( !entry->active )
            continue;
        entry->active = false;
        entry->slot = -1;
        entry->source_revision = 0;
        if( entry->incarnation == TORIRS_PLUGIN_V2_RESOURCE_INCAR_MAX )
            entry->retired = true;
        else
            entry->incarnation++;
    }
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
plugin_v2_runtime_ImageUnbox(
    struct PluginV2Runtime const* runtime,
    struct ToriRS_ImageRef image)
{
    assert(runtime);
    return v2_resource_resolve(
        runtime->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        ((uint32_t)runtime->context->index),
        image.value);
}

int
plugin_v2_runtime_ModelUnbox(
    struct PluginV2Runtime const* runtime,
    struct ToriRS_ModelRef model)
{
    assert(runtime);
    return v2_resource_resolve(
        runtime->model_tokens,
        TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
        V2_RESOURCE_MODEL,
        ((uint32_t)runtime->context->index),
        model.value);
}


static struct PluginV2Runtime*
v2_runtime(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime;

    assert(api);
    runtime = api->instance;
    assert(runtime);
    assert(&runtime->api == api);
    assert(runtime->context);
    return runtime;
}

static int v2_surface_slot(int surface);
static bool v2_panel_options_valid(
    struct ToriRS_SelectOption const* options,
    int option_count);

static void
v2_core_log(
    struct ToriRS_ApiV2* api,
    char const* format,
    ...)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    char message[1024];
    va_list args;

    assert(format);
    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    api_log(runtime->context, "%s", message);
}

static void
v2_core_notify(
    struct ToriRS_ApiV2* api,
    char const* text)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(text);
    api_notify(runtime->context, text);
}

static int
v2_core_screen(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_screen(runtime->context);
}

static uint64_t
v2_core_frame_ms(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_frame_ms(runtime->context);
}

static uint64_t
v2_core_frame_work_us(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_frame_work_us(runtime->context);
}

static bool
v2_core_lane(
    struct ToriRS_ApiV2* api,
    struct ToriRS_LaneInfo* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_lane(runtime->context, out);
}

static bool
v2_core_capability(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(name);
    return plugin_v2_capability(
        runtime->context->host, runtime->context, name);
}

static char const*
v2_core_plugin_id(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return runtime->context->name;
}

static bool
v2_config_has(
    struct ToriRS_ApiV2* api,
    char const* key)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(key);
    return api_cfg_has(runtime->context, key);
}

static bool
v2_config_get_bool(
    struct ToriRS_ApiV2* api,
    char const* key,
    bool* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(key);
    assert(out);
    if( false ||
        !api_cfg_has(runtime->context, key) )
        return false;
    *out = api_cfg_bool(runtime->context, key) != 0;
    return true;
}

static bool
v2_config_get_int(
    struct ToriRS_ApiV2* api,
    char const* key,
    int* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(key);
    assert(out);
    if( false ||
        !api_cfg_has(runtime->context, key) )
        return false;
    *out = api_cfg_int(runtime->context, key);
    return true;
}

static bool
v2_config_get_color(
    struct ToriRS_ApiV2* api,
    char const* key,
    uint32_t* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(key);
    assert(out);
    if( false ||
        !api_cfg_has(runtime->context, key) )
        return false;
    *out = api_cfg_color(runtime->context, key);
    return true;
}

static bool
v2_config_get_string(
    struct ToriRS_ApiV2* api,
    char const* key,
    char const** out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(key);
    assert(out);
    if( false ||
        !api_cfg_has(runtime->context, key) )
        return false;
    *out = api_cfg_str(runtime->context, key);
    return true;
}

static enum ToriRS_Result
v2_config_set(
    struct ToriRS_ApiV2* api,
    char const* key,
    char const* value)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(key);
    assert(value);
    api_cfg_set(runtime->context, key, value);
    return TORIRS_RESULT_OK;
}

static bool
v2_world_local_player(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PlayerSnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_local_player(runtime->context, out);
}

static int
v2_world_npc_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_NpcSnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_npc_next(runtime->context, iterator, out);
}

static bool
v2_world_npc_by_slot(
    struct ToriRS_ApiV2* api,
    int slot,
    struct ToriRS_NpcSnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_npc_by_slot(runtime->context, slot, out);
}

static int
v2_world_player_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_PlayerSnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_player_next(runtime->context, iterator, out);
}

static int
v2_world_item_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_GroundItemSnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_obj_next(runtime->context, iterator, out);
}

static int
v2_world_scenery_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_ScenerySnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_loc_next(runtime->context, iterator, out);
}

static bool
v2_input_key_held(
    struct ToriRS_ApiV2* api,
    int key)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_key_held(runtime->context, key);
}

static bool
v2_input_pointer(
    struct ToriRS_ApiV2* api,
    int* out_x,
    int* out_y)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out_x);
    assert(out_y);
    return api_mouse_pos(runtime->context, out_x, out_y);
}

static bool
v2_input_hover_tile(
    struct ToriRS_ApiV2* api,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out_x);
    assert(out_z);
    assert(out_level);
    return api_hover_tile(runtime->context, out_x, out_z, out_level);
}

static bool
v2_input_hover_entity(
    struct ToriRS_ApiV2* api,
    struct ToriRS_HoverTarget* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return api_hover_entity(runtime->context, out);
}

static void
v2_input_text_input(
    struct ToriRS_ApiV2* api,
    bool enabled)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
        api_text_input(runtime->context, enabled ? 1 : 0);
}

static void
v2_input_chat_focus(
    struct ToriRS_ApiV2* api,
    bool focused)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
        api_chat_focus(runtime->context, focused ? 1 : 0);
}

static struct ToriRS_UiNodeRef
v2_ui_ref(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(name);
    return plugin_v2_ui_ref(
        runtime->context->host, runtime->context, name);
}

static bool
v2_ui_info(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeInfo* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out);
    return plugin_v2_ui_info(
        runtime->context->host, runtime->context, node, out);
}

static bool
v2_ui_invoke(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(action);
    return plugin_v2_ui_invoke(
        runtime->context->host, runtime->context, node, action);
}

static bool
v2_ui_contribution_info(
    struct ToriRS_ApiV2* api,
    char const* node,
    uint32_t facets,
    struct ToriRS_UiContributionInfo* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(node);
    assert(out);
    return plugin_v2_ui_contribution_info_hook(
        runtime->context->host, runtime->context, node, facets, out);
}

static bool
v2_ui_node_images_valid(
    struct PluginV2Runtime const* runtime,
    struct ToriRS_UiNode const* value)
{
    size_t const size = value->struct_size ? value->struct_size : TORIRS_UI_NODE_LEGACY_SIZE;

    assert(runtime);
    assert(value);
    if( size >= offsetof(struct ToriRS_UiNode, image) + sizeof(value->image) &&
        value->image.value != 0 && plugin_v2_runtime_ImageUnbox(runtime, value->image) < 0 )
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
            plugin_v2_runtime_ImageUnbox(runtime, value->state_images[state]) < 0 )
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(value);
    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 &&
        !v2_ui_node_images_valid(runtime, value) )
        return TORIRS_RESULT_INVALID;
    return plugin_v2_ui_update_hook(
        runtime->context->host, runtime->context, node, facets, value);
}

static bool
v2_ui_menu_add(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MenuBuildEvent* menu,
    char const* text,
    uint32_t action_id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(menu);
    assert(text);
    return api_menu_add(
               runtime->context, menu, text, action_id) != 0;
}

static enum ToriRS_Result
v2_ui_set_enabled(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    bool enabled)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return plugin_v2_ui_set_enabled_hook(
        runtime->context->host, runtime->context, node, enabled);
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
v2_rect_from_internal(
    struct ToriRS_Rect* out,
    struct ToriRS_PlacementRect const* source)
{
    assert(out);
    assert(source);
    out->x = source->x;
    out->y = source->y;
    out->width = source->w;
    out->height = source->h;
}

static void
v2_rect_to_internal(
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
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_placement_revision(runtime->context)
               : 0;
}

static struct ToriRS_PlacementAreaRef
v2_placement_area(
    struct ToriRS_ApiV2* api,
    int area)
{
    struct ToriRS_PlacementAreaRef ref = { 0 };

    (void)v2_runtime(api);
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
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_PlacementRect largest;
    uint64_t largest_area = 0;
    bool found = false;
    int area;
    int iterator = -1;

    assert(out);
    if( !v2_area_decode(area_ref, &area) )
        return false;
    for( ;; )
    {
        struct ToriRS_PlacementRect rect;
        uint64_t rect_area;
        iterator = api_placement_rect_next(runtime->context, area, iterator, &rect);
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
        v2_rect_from_internal(out, &largest);
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
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_PlacementRect placed;

    assert(out);
    if( !v2_area_valid(area) || anchor < TORIRS_ANCHOR_TOP_LEFT ||
        anchor > TORIRS_ANCHOR_BOTTOM_RIGHT || width <= 0 || height <= 0 || margin < 0 )
        return false;
    if( !api_placement_place(
            runtime->context, area, anchor, width, height, margin, &placed) )
        return false;
    v2_rect_from_internal(out, &placed);
    return true;
}

static int
v2_placement_rect_next(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PlacementAreaRef area_ref,
    int iterator,
    struct ToriRS_Rect* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_PlacementRect rect;
    int area;
    int next;

    assert(out);
    if( !v2_area_decode(area_ref, &area) )
        return -1;
    next = api_placement_rect_next(runtime->context, area, iterator, &rect);
    if( next >= 0 )
        v2_rect_from_internal(out, &rect);
    return next;
}

static bool
v2_placement_contains(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PlacementAreaRef area_ref,
    struct ToriRS_Rect rect)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_PlacementRect internal_rect;
    int area;

    if( !v2_area_decode(area_ref, &area) || rect.width < 0 || rect.height < 0 )
        return false;
    v2_rect_to_internal(&internal_rect, &rect);
    return api_placement_contains(runtime->context, area, &internal_rect) != 0;
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(name);
    if( !v2_reservation_name_valid(name) || area != TORIRS_AREA_OVERLAY_SAFE ||
        edge < TORIRS_EDGE_TOP || edge > TORIRS_EDGE_LEFT || pixels < 0 )
        return TORIRS_RESERVE_INVALID;
    return api_placement_reserve(runtime->context, name, area, edge, pixels)
               ? TORIRS_RESERVE_OK
               : TORIRS_RESERVE_BUDGET;
}

static bool
v2_placement_reservation_rect(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_Rect* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_PlacementRect source;

    assert(name);
    assert(out);
    if( !v2_reservation_name_valid(name) ||
        !api_placement_reservation_rect(runtime->context, name, &source) )
        return false;
    v2_rect_from_internal(out, &source);
    return true;
}

static int
v2_frame_offer_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_FrameOfferInfo* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_FrameOfferInfo snapshot;
    uint32_t const capacity = out ? out->struct_size : 0;
    int next;

    assert(out);
    if( capacity < TORIRS_FRAME_OFFER_INFO_REQUIRED_SIZE )
        return -1;
    next = api_frame_offer_next(runtime->context, iterator, &snapshot);
    if( next < 0 )
        return -1;
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    (void)v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
    return next;
}

static void
v2_frame_selection(
    struct ToriRS_ApiV2* api,
    struct ToriRS_FrameSelection* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_FrameSelection snapshot;
    uint32_t const capacity = out ? out->struct_size : 0;

    assert(out);
    if( capacity < TORIRS_FRAME_SELECTION_REQUIRED_SIZE )
        return;
    api_frame_selection(runtime->context, &snapshot);
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    (void)v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
}

static enum ToriRS_Result
v2_frame_select(
    struct ToriRS_ApiV2* api,
    char const* id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(id);
    return api_frame_select(runtime->context, id) ? TORIRS_RESULT_OK
                                                               : TORIRS_RESULT_INVALID;
}

static void
v2_frame_invalidate(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
        api_frame_invalidate(runtime->context);
}

static bool
v2_frame_surface_native_size(
    struct ToriRS_ApiV2* api,
    int surface,
    int* out_width,
    int* out_height)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const surface_token = v2_surface_slot(surface);

    if( surface_token < 0 )
        return false;
    return api_slot_native_size(
               runtime->context, surface_token, out_width, out_height) != 0;
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out_x);
    assert(out_y);
    return api_project(runtime->context, fine_x, fine_z, height, out_x, out_y);
}

static int
v2_draw_element_height(
    struct ToriRS_ApiV2* api,
    int element_id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_element_height(runtime->context, element_id)
               : 0;
}

static int
v2_draw_hsl_from_rgb(
    struct ToriRS_ApiV2* api,
    uint32_t rgb)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_hsl_from_rgb(runtime->context, rgb);
}

static uint32_t
v2_draw_hsl_to_rgb(
    struct ToriRS_ApiV2* api,
    int hsl)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_hsl_to_rgb(runtime->context, hsl);
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
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(name);
    if( !v2_asset_name_valid(name) )
        return TORIRS_ASSET_INVALID;
    return v2_asset_state_checked(plugin_v2_asset_request(
        runtime->context->host, runtime->context, name));
}

static bool
v2_assets_bytes(
    struct ToriRS_ApiV2* api,
    char const* name,
    void const** out_data,
    size_t* out_size)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    void const* data;
    int size = 0;

    assert(name);
    assert(out_data);
    assert(out_size);
    if( !v2_asset_name_valid(name) )
        return false;
    if( v2_asset_state_checked(plugin_v2_asset_request(
            runtime->context->host, runtime->context, name)) != TORIRS_ASSET_READY )
        return false;
    data = api_asset_data(runtime->context, name, &size);
    if( size < 0 )
        return false;
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(name);
    assert(data || size == 0);
    if( !v2_asset_name_valid(name) || size > INT_MAX )
        return TORIRS_RESULT_INVALID;
    return api_asset_save(runtime->context, name, data, (int)size)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_ERROR;
}

static void
v2_assets_release(
    struct ToriRS_ApiV2* api,
    char const* name)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(name);
        api_asset_release(runtime->context, name);
}

static enum ToriRS_AssetState
v2_assets_image(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_ImageRef* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int width = 0;
    int height = 0;
    int image = -1;
    enum ToriRS_AssetState state;

    assert(name);
    assert(out);
    out->value = 0;
    if( !v2_asset_name_valid(name) )
        return TORIRS_ASSET_INVALID;
    state = v2_asset_state_checked(plugin_v2_image_request(
        runtime->context->host, runtime->context, name, &image));
    if( state != TORIRS_ASSET_PENDING && state != TORIRS_ASSET_READY )
        return state;
    if( image < 0 )
        return TORIRS_ASSET_ERROR;
    out->value = v2_resource_acquire(
        runtime->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        ((uint32_t)runtime->context->index),
        image);
    if( out->value == 0 )
        return TORIRS_ASSET_BUDGET;
    (void)width;
    (void)height;
    return state;
}

static bool
v2_assets_image_size(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ImageRef image,
    int* out_width,
    int* out_height)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(out_width);
    assert(out_height);
    int const image_slot = plugin_v2_runtime_ImageUnbox(runtime, image);
    if( image_slot < 0 )
        return false;
    return api_image_size(runtime->context, image_slot, out_width, out_height) != 0;
}

static void
v2_assets_image_release(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ImageRef image)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const image_slot = plugin_v2_runtime_ImageUnbox(runtime, image);
    if( image_slot < 0 )
        return;
    plugin_v2_image_release(
        runtime->context->host, runtime->context, image);
    v2_resource_invalidate(
        runtime->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        ((uint32_t)runtime->context->index),
        image.value);
}

static enum ToriRS_AssetState
v2_assets_model(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_ModelRef* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int model = -1;
    enum ToriRS_AssetState state;

    assert(name);
    assert(out);
    out->value = 0;
    if( !v2_asset_name_valid(name) )
        return TORIRS_ASSET_INVALID;
    state = v2_asset_state_checked(plugin_v2_model_request(
        runtime->context->host, runtime->context, name, &model));
    if( state != TORIRS_ASSET_PENDING && state != TORIRS_ASSET_READY )
        return state;
    if( model < 0 )
        return TORIRS_ASSET_ERROR;
    out->value = v2_resource_acquire(
        runtime->model_tokens,
        TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
        V2_RESOURCE_MODEL,
        ((uint32_t)runtime->context->index),
        model);
    if( out->value == 0 )
        return TORIRS_ASSET_BUDGET;
    return state;
}

static void
v2_assets_model_release(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ModelRef model)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    if( plugin_v2_runtime_ModelUnbox(runtime, model) < 0 )
        return;
    plugin_v2_model_release(
        runtime->context->host, runtime->context, model);
    v2_resource_invalidate(
        runtime->model_tokens,
        TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX,
        V2_RESOURCE_MODEL,
        ((uint32_t)runtime->context->index),
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(name);
    assert(out_path);
    if( out_path_size == 0 || out_path_size > INT_MAX || !v2_asset_name_valid(name) )
        return TORIRS_RESULT_INVALID;
    return api_screenshot(
               runtime->context, destination, name, out_path, (int)out_path_size)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_ERROR;
}

static bool
v2_assets_image_pixels(
    struct ToriRS_ApiV2* api,
    struct ToriRS_ImageRef image,
    uint32_t* out_argb,
    size_t capacity,
    size_t* out_count)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const image_slot = plugin_v2_runtime_ImageUnbox(runtime, image);
    int width = 0;
    int height = 0;
    size_t required;
    int copied;

    assert(out_count);
    *out_count = 0;
    if( image_slot < 0 || !out_argb ||
        !api_image_size(runtime->context, image_slot, &width, &height) ||
        width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height )
        return false;
    required = (size_t)width * (size_t)height;
    if( required > capacity || required > INT_MAX )
        return false;
    copied = api_image_pixels(
        runtime->context, image_slot, out_argb, (int)capacity);
    if( copied < 0 || (size_t)copied != required )
        return false;
    *out_count = required;
    return true;
}

static enum ToriRS_AssetState
v2_assets_image_compose(
    struct ToriRS_ApiV2* api,
    char const* name,
    int width,
    int height,
    uint32_t const* argb,
    struct ToriRS_ImageRef* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int image;

    assert(name);
    assert(out);
    out->value = 0;
    if( !v2_asset_name_valid(name) || !argb || width <= 0 || height <= 0 )
        return TORIRS_ASSET_INVALID;
    image = api_image_compose(
        runtime->context, name, width, height, argb);
    if( image < 0 )
        return TORIRS_ASSET_BUDGET;
    out->value = v2_resource_acquire(
        runtime->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        ((uint32_t)runtime->context->index),
        image);
    return out->value ? TORIRS_ASSET_READY : TORIRS_ASSET_BUDGET;
}

static enum ToriRS_Result
v2_scene_mesh_create(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MeshRef* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int mesh;

    assert(out);
    out->value = 0;
    mesh = api_mesh_create(runtime->context);
    if( mesh < 0 )
        return TORIRS_RESULT_BUDGET;
    out->value = v2_resource_acquire(
        runtime->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        ((uint32_t)runtime->context->index),
        mesh);
    if( out->value == 0 )
    {
            api_mesh_destroy(runtime->context, mesh);
        return TORIRS_RESULT_BUDGET;
    }
    return TORIRS_RESULT_OK;
}

static void
v2_scene_mesh_destroy(
    struct ToriRS_ApiV2* api,
    struct ToriRS_MeshRef mesh)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const mesh_slot = v2_resource_resolve(
        runtime->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        ((uint32_t)runtime->context->index),
        mesh.value);
    if( mesh_slot < 0 )
        return;
        api_mesh_destroy(runtime->context, mesh_slot);
    v2_resource_invalidate(
        runtime->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        ((uint32_t)runtime->context->index),
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    int const mesh_slot = v2_resource_resolve(
        runtime->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        ((uint32_t)runtime->context->index),
        mesh.value);
    if( mesh_slot < 0 )
        return TORIRS_RESULT_INVALID;
    return api_mesh_vertex(runtime->context, mesh_slot, x, y, z) >= 0
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    int const mesh_slot = v2_resource_resolve(
        runtime->mesh_tokens,
        TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH,
        ((uint32_t)runtime->context->index),
        mesh.value);
    if( mesh_slot < 0 || a < 0 || b < 0 || c < 0 || alpha < 0 ||
        alpha > TORIRS_PLUGIN_MESH_ALPHA_MAX )
        return TORIRS_RESULT_INVALID;
    return api_mesh_face(runtime->context, mesh_slot, a, b, c, hsl, alpha) >= 0
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_BUDGET;
}

static enum ToriRS_Result
v2_scene_instance_create(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int instance;

    assert(out);
    out->value = 0;
    instance = api_object_create(runtime->context);
    if( instance < 0 )
        return TORIRS_RESULT_BUDGET;
    out->value = v2_resource_acquire(
        runtime->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        ((uint32_t)runtime->context->index),
        instance);
    if( out->value == 0 )
    {
            api_object_destroy(runtime->context, instance);
        return TORIRS_RESULT_BUDGET;
    }
    return TORIRS_RESULT_OK;
}

static void
v2_scene_instance_destroy(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        ((uint32_t)runtime->context->index),
        instance.value);
    if( instance_slot < 0 )
        return;
        api_object_destroy(runtime->context, instance_slot);
    v2_resource_invalidate(
        runtime->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        ((uint32_t)runtime->context->index),
        instance.value);
}

static enum ToriRS_Result
v2_scene_instance_model(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    struct ToriRS_ModelRef model)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        ((uint32_t)runtime->context->index),
        instance.value);
    int const model_slot = plugin_v2_runtime_ModelUnbox(runtime, model);
    if( instance_slot < 0 || model_slot < 0 )
        return TORIRS_RESULT_INVALID;
    api_object_set_model(
        runtime->context, instance_slot, TORIRS_PLUGIN_MODEL_ASSET, model_slot);
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
    struct PluginV2Runtime* runtime = v2_runtime(api);

    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        ((uint32_t)runtime->context->index),
        instance.value);
    if( instance_slot < 0 || level < 0 || yaw < 0 || yaw > 2047 )
        return TORIRS_RESULT_INVALID;
    api_object_set_position(
        runtime->context, instance_slot, tile_x, tile_z, level, height, yaw);
    return TORIRS_RESULT_OK;
}

static void
v2_scene_instance_active(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    bool active)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens,
        TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE,
        ((uint32_t)runtime->context->index),
        instance.value);
    if( instance_slot >= 0 )
        api_object_set_active(runtime->context, instance_slot, active ? 1 : 0);
}

static enum ToriRS_Result
v2_scene_instance_mesh(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    struct ToriRS_MeshRef mesh)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    int const mesh_slot = v2_resource_resolve(
        runtime->mesh_tokens, TORIRS_PLUGIN_V2_MESH_TOKENS_MAX,
        V2_RESOURCE_MESH, ((uint32_t)runtime->context->index), mesh.value);

    if( instance_slot < 0 || mesh_slot < 0 )
        return TORIRS_RESULT_INVALID;
    api_object_set_model(
        runtime->context, instance_slot, TORIRS_PLUGIN_MODEL_MESH, mesh_slot);
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
v2_scene_instance_cache_model(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    int kind,
    int id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    int source;

    if( instance_slot < 0 || id < 0 ||
        (kind != TORIRS_SCENE_MODEL_CACHE && kind != TORIRS_SCENE_MODEL_SPOTANIM) )
        return TORIRS_RESULT_INVALID;
    source = kind == TORIRS_SCENE_MODEL_CACHE ? TORIRS_PLUGIN_MODEL_CACHE
                                              : TORIRS_PLUGIN_MODEL_SPOTANIM;
    api_object_set_model(runtime->context, instance_slot, source, id);
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
v2_scene_instance_recolor(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    int from_hsl,
    int to_hsl)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    if( instance_slot < 0 )
        return TORIRS_RESULT_INVALID;
    api_object_recolor(runtime->context, instance_slot, from_hsl, to_hsl);
    return TORIRS_RESULT_OK;
}

static void
v2_scene_instance_clear_recolors(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    if( instance_slot >= 0 )
        api_object_clear_recolors(runtime->context, instance_slot);
}

static enum ToriRS_Result
v2_scene_instance_animation(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    int sequence_id,
    bool loop)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    if( instance_slot < 0 || sequence_id < -1 )
        return TORIRS_RESULT_INVALID;
    api_object_set_anim(
        runtime->context, instance_slot, sequence_id, loop ? 1 : 0);
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
v2_scene_instance_light(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance,
    int ambient,
    int contrast)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    if( instance_slot < 0 )
        return TORIRS_RESULT_INVALID;
    api_object_set_light(
        runtime->context, instance_slot, ambient, contrast);
    return TORIRS_RESULT_OK;
}

static bool
v2_scene_instance_ready(
    struct ToriRS_ApiV2* api,
    struct ToriRS_SceneInstanceRef instance)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int const instance_slot = v2_resource_resolve(
        runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX,
        V2_RESOURCE_INSTANCE, ((uint32_t)runtime->context->index), instance.value);
    return instance_slot >= 0 && api_object_ready(runtime->context, instance_slot) != 0;
}

static enum ToriRS_Result
v2_panel_request(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PanelDescriptor const* description)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);

    assert(description);
    return api_panel_request(runtime->context, description) ? TORIRS_RESULT_OK
                                                                         : TORIRS_RESULT_ERROR;
}

static void
v2_panel_invalidate(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
        api_panel_clear(runtime->context);
}

static void
v2_panel_attention(
    struct ToriRS_ApiV2* api,
    bool wanted)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
        (void)api_panel_set_attention(runtime->context, wanted);
}

static enum ToriRS_Result
v2_panel_set_text(
    struct ToriRS_ApiV2* api,
    char const* id,
    char const* text)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(id);
    assert(text);
    return api_panel_set_text(runtime->context, id, text)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_NOT_FOUND;
}

static enum ToriRS_Result
v2_panel_set_value(
    struct ToriRS_ApiV2* api,
    char const* id,
    int value)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(id);
    return api_panel_set_value(runtime->context, id, value)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_NOT_FOUND;
}

static enum ToriRS_Result
v2_panel_set_height(
    struct ToriRS_ApiV2* api,
    char const* id,
    int preferred_height)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(id);
    return api_panel_set_height(
               runtime->context, id, preferred_height)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_NOT_FOUND;
}

static enum ToriRS_Result
v2_panel_set_options(
    struct ToriRS_ApiV2* api,
    char const* id,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(id);
    assert(value);
    if( !v2_panel_options_valid(options, option_count) )
        return TORIRS_RESULT_INVALID;
    return plugin_v2_panel_set_options(
        runtime->context->host,
        runtime->context,
        id,
        value,
        options,
        option_count);
}

static void
v2_panel_redraw(
    struct ToriRS_ApiV2* api,
    char const* id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(id);
        api_panel_invalidate(runtime->context, id);
}

static int
v2_cache_frame_root(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_frame_root(runtime->context);
}

static int
v2_cache_varbit(
    struct ToriRS_ApiV2* api,
    int id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_varbit(runtime->context, id);
}

static int
v2_cache_varp(
    struct ToriRS_ApiV2* api,
    int id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_varp(runtime->context, id);
}

static bool
v2_cache_component_rect(
    struct ToriRS_ApiV2* api,
    int component_id,
    struct ToriRS_Rect* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int x;
    int y;
    int width;
    int height;

    assert(out);
    if( false ||
        !api_component_rect(runtime->context, component_id, &x, &y, &width, &height) )
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
    struct PluginV2Runtime* runtime = v2_runtime(api);
    if( component_id < 0 || op < 0 || op > 10 )
        return false;
    return api_if_click(runtime->context, component_id, op) != 0;
}

static bool
v2_cache_named_id(
    struct ToriRS_ApiV2* api,
    char const* kind,
    char const* name,
    int* out_id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int id;

    assert(kind);
    assert(name);
    assert(out_id);
    id = api_cache_id(runtime->context, kind, name);
    if( id < 0 )
        return false;
    *out_id = id;
    return true;
}

static int
v2_cache_tab_active(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_tab_active(runtime->context)
               : -1;
}

static bool
v2_cache_tab_enabled(struct ToriRS_ApiV2* api, int tab)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_tab_enabled(runtime->context, tab) != 0;
}

static bool
v2_cache_tab_select(struct ToriRS_ApiV2* api, int tab)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_tab_select(runtime->context, tab);
}

static bool
v2_client_display_get(
    struct ToriRS_ApiV2* api,
    int setting,
    int* out_value,
    int* out_min,
    int* out_max)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_display_setting(
               runtime->context, setting, out_value, out_min, out_max) != 0;
}

static enum ToriRS_Result
v2_client_display_set(
    struct ToriRS_ApiV2* api,
    int setting,
    int value)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_display_setting_set(runtime->context, setting, value)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_NOT_FOUND;
}

static int
v2_client_feature_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_FeatureInfo* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(out);
    return 1
               ? api_feature_next(runtime->context, iterator, out)
               : -1;
}

static bool
v2_client_feature_get(
    struct ToriRS_ApiV2* api,
    char const* key,
    int* out_value)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int value;
    assert(key);
    assert(out_value);
    value = api_feature_get(runtime->context, key);
    if( value == TORIRS_PLUGIN_FEATURE_UNSET )
        return false;
    *out_value = value;
    return true;
}

static enum ToriRS_Result
v2_client_feature_set(
    struct ToriRS_ApiV2* api,
    char const* key,
    int value)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(key);
    return api_feature_set(runtime->context, key, value)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_INVALID;
}

static int
v2_client_world_cycle(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_world_cycle(runtime->context)
               : 0;
}

static bool
v2_client_datestamp(
    struct ToriRS_ApiV2* api,
    char* out,
    size_t out_size)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(out);
    if( out_size == 0 || out_size > INT_MAX )
        return false;
    return api_datestamp(
               runtime->context, out, (int)out_size) != 0;
}

static uint32_t
v2_client_setting_color(
    struct ToriRS_ApiV2* api,
    int varp_id,
    uint32_t fallback)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_setting_color(runtime->context, varp_id, fallback)
               : fallback;
}

static size_t
v2_client_memory_bytes(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return plugin_v2_memory_bytes(
        runtime->context->host, runtime->context);
}

static void
v2_client_disable_self(
    struct ToriRS_ApiV2* api,
    char const* reason)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(reason);
        api_disable_self(runtime->context, reason);
}

static bool
v2_game_skill(
    struct ToriRS_ApiV2* api,
    int index,
    struct ToriRS_SkillSnapshot* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    struct ToriRS_SkillSnapshot snapshot;
    char const* name;
    uint32_t capacity;

    assert(out);
    capacity = out->struct_size;
    if( capacity < TORIRS_SKILL_SNAPSHOT_REQUIRED_SIZE || index < 0 )
        return false;
    name = api_skill_name(runtime->context, index);
    if( !name )
        return false;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    snapshot.index = index;
    (void)snprintf(snapshot.name, sizeof(snapshot.name), "%s", name);
    if( !api_stat(
            runtime->context, index, &snapshot.current_level, &snapshot.base_level) ||
        !api_stat_xp(
            runtime->context,
            index,
            &snapshot.xp,
            &snapshot.level_xp,
            &snapshot.next_level_xp) )
        return false;
    return v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
}

static int
v2_game_run_energy(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_run_energy(runtime->context)
               : 0;
}

static int
v2_game_inventory_size(struct ToriRS_ApiV2* api, int inventory)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return 1
               ? api_inv_size(runtime->context, inventory)
               : 0;
}

static bool
v2_game_inventory_slot(
    struct ToriRS_ApiV2* api,
    int inventory,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return api_inv_slot(
               runtime->context, inventory, slot, out_obj_id, out_count) != 0;
}

static bool
v2_game_item_info(
    struct ToriRS_ApiV2* api,
    int obj_id,
    struct ToriRS_ItemInfo* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(out);
    return api_obj_info(runtime->context, obj_id, out) != 0;
}

static enum ToriRS_AssetState
v2_game_item_image(
    struct ToriRS_ApiV2* api,
    int obj_id,
    int count,
    int style,
    struct ToriRS_ImageRef* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    int image;
    uint64_t revision = 0;
    enum ToriRS_AssetState state;

    assert(out);
    out->value = 0;
    state = v2_asset_state_checked(plugin_v2_item_image(
        runtime->context->host,
        runtime->context,
        obj_id,
        count,
        style,
        &image,
        &revision));
    if( state != TORIRS_ASSET_PENDING && state != TORIRS_ASSET_READY )
        return state;
    if( image < 0 || revision == 0 )
        return state == TORIRS_ASSET_PENDING ? state : TORIRS_ASSET_ERROR;
    out->value = v2_resource_acquire_versioned(
        runtime->image_tokens,
        TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX,
        V2_RESOURCE_IMAGE,
        ((uint32_t)runtime->context->index),
        image,
        revision);
    return out->value ? state : TORIRS_ASSET_BUDGET;
}

static int
v2_game_highlight_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_HighlightItem* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(out);
    return 1
               ? api_highlight_next(runtime->context, iterator, out)
               : -1;
}

static int
v2_game_loot_source_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_LootSource* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(out);
    return 1
               ? api_loot_source_next(runtime->context, iterator, out)
               : -1;
}

static int
v2_game_loot_row_next(
    struct ToriRS_ApiV2* api,
    int source_id,
    int iterator,
    struct ToriRS_LootRow* out)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(out);
    return 1
               ? api_loot_row_next(
                     runtime->context, source_id, iterator, out)
               : -1;
}

static char const*
v2_game_entity_part(
    struct ToriRS_ApiV2* api,
    int kind,
    int a,
    int b,
    int c,
    int d,
    char* buffer,
    size_t capacity)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    if( !buffer || capacity == 0 || capacity > INT_MAX )
        return NULL;
    return api_entity_part(
        runtime->context, kind, a, b, c, d, buffer, (int)capacity);
}

static enum ToriRS_Result
v2_game_entity_look(
    struct ToriRS_ApiV2* api,
    char const* part,
    struct ToriRS_EntityAppearance const* look)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(part);
    assert(look);
    return api_entity_look(runtime->context, part, look)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_NOT_FOUND;
}

static enum ToriRS_Result
v2_game_entity_ops(
    struct ToriRS_ApiV2* api,
    char const* part,
    int mode,
    char const* const* operations,
    int operation_count,
    uint32_t action_id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    assert(part);
    return api_entity_ops(
               runtime->context,
               part,
               mode,
               operations,
               operation_count,
               action_id)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_NOT_FOUND;
}

static uint64_t
v2_game_loot_revision(struct ToriRS_ApiV2* api)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return plugin_v2_loot_revision(
        runtime->context->host, runtime->context);
}

static bool
v2_game_loot_source_clear(
    struct ToriRS_ApiV2* api,
    int source_id)
{
    struct PluginV2Runtime* runtime = v2_runtime(api);
    return source_id >= 0 && plugin_v2_loot_source_clear(
        runtime->context->host, runtime->context, source_id);
}

static struct PluginV2DrawScope*
v2_draw_scope(struct ToriRS_DrawBuilder* draw)
{
    struct PluginV2DrawScope* scope;

    assert(draw);
    scope = draw->implementation;
    assert(scope);
    assert(scope->active);
    assert(scope->runtime);
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
    struct PluginV2DrawScope const* scope,
    struct ToriRS_Rect* rect)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    assert(scope);
    assert(rect);
    if( (int64_t)rect->x + scope->origin_x < INT_MIN ||
        (int64_t)rect->x + scope->origin_x > INT_MAX ||
        (int64_t)rect->y + scope->origin_y < INT_MIN ||
        (int64_t)rect->y + scope->origin_y > INT_MAX )
        return false;
    rect->x += scope->origin_x;
    rect->y += scope->origin_y;
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
    struct PluginV2DrawScope const* scope,
    int* x0,
    int* y0,
    int* x1,
    int* y1)
{
    double t0 = 0.0;
    double t1 = 1.0;
    double dx;
    double dy;
    double p[4];
    double q[4];
    int original_x;
    int original_y;

    assert(scope);
    if( (int64_t)*x0 + scope->origin_x < INT_MIN ||
        (int64_t)*x0 + scope->origin_x > INT_MAX ||
        (int64_t)*x1 + scope->origin_x < INT_MIN ||
        (int64_t)*x1 + scope->origin_x > INT_MAX ||
        (int64_t)*y0 + scope->origin_y < INT_MIN ||
        (int64_t)*y0 + scope->origin_y > INT_MAX ||
        (int64_t)*y1 + scope->origin_y < INT_MIN ||
        (int64_t)*y1 + scope->origin_y > INT_MAX )
        return false;
    *x0 += scope->origin_x;
    *x1 += scope->origin_x;
    *y0 += scope->origin_y;
    *y1 += scope->origin_y;
    dx = (double)*x1 - *x0;
    dy = (double)*y1 - *y0;
    p[0] = -dx;
    p[1] = dx;
    p[2] = -dy;
    p[3] = dy;
    q[0] = (double)*x0 - scope->clip.x;
    q[1] = (double)scope->clip.x + scope->clip.width - *x0;
    q[2] = (double)*y0 - scope->clip.y;
    q[3] = (double)scope->clip.y + scope->clip.height - *y0;
    original_x = *x0;
    original_y = *y0;
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
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);

    if( v2_clip_rect(scope, &rect) )
        api_draw_rect(
            scope->runtime->context,
            scope->surface_token,
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
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);

    /* v1 has no partially-transparent line.  Fully transparent is still a
     * no-op; every visible alpha uses its exact geometry and colour. */
    if( v2_alpha(alpha) > 0 && v2_clip_line(scope, &x0, &y0, &x1, &y1) )
        api_draw_line(scope->runtime->context, scope->surface_token, x0, y0, x1, y1, rgb);
}

static void
v2_builder_text(
    struct ToriRS_DrawBuilder* draw,
    int x,
    int y,
    char const* text,
    uint32_t rgb)
{
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);

    assert(text);
    if( (int64_t)x + scope->origin_x < INT_MIN ||
        (int64_t)x + scope->origin_x > INT_MAX ||
        (int64_t)y + scope->origin_y < INT_MIN ||
        (int64_t)y + scope->origin_y > INT_MAX )
        return;
    x += scope->origin_x;
    y += scope->origin_y;
    if( scope->clip_active &&
        (x < scope->clip.x || y < scope->clip.y ||
         x >= scope->clip.x + scope->clip.width || y >= scope->clip.y + scope->clip.height) )
        return;
        api_draw_text(scope->runtime->context, scope->surface_token, x, y, text, rgb);
}

static void
v2_builder_image(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    int alpha)
{
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);
    int const opaque_alpha = v2_alpha(alpha);
    int const image_slot = plugin_v2_runtime_ImageUnbox(scope->runtime, image);

    if( image_slot < 0 || opaque_alpha == 0 ||
        (int64_t)x + scope->origin_x < INT_MIN ||
        (int64_t)x + scope->origin_x > INT_MAX ||
        (int64_t)y + scope->origin_y < INT_MIN ||
        (int64_t)y + scope->origin_y > INT_MAX )
        return;
    x += scope->origin_x;
    y += scope->origin_y;
    api_draw_image(
        scope->runtime->context,
        scope->surface_token,
        image_slot,
        x,
        y,
        scope->clip_active ? scope->clip.x : 0,
        scope->clip_active ? scope->clip.y : 0,
        scope->clip_active ? scope->clip.width : 0,
        scope->clip_active ? scope->clip.height : 0,
        255 - opaque_alpha);
}

static void
v2_builder_image_clip(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    struct ToriRS_Rect clip,
    int alpha)
{
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);
    int const opaque_alpha = v2_alpha(alpha);
    int const image_slot = plugin_v2_runtime_ImageUnbox(scope->runtime, image);

    if( image_slot < 0 || opaque_alpha == 0 ||
        (int64_t)x + scope->origin_x < INT_MIN ||
        (int64_t)x + scope->origin_x > INT_MAX ||
        (int64_t)y + scope->origin_y < INT_MIN ||
        (int64_t)y + scope->origin_y > INT_MAX || !v2_clip_rect(scope, &clip) )
        return;
    x += scope->origin_x;
    y += scope->origin_y;
    api_draw_image(
        scope->runtime->context,
        scope->surface_token,
        image_slot,
        x,
        y,
        clip.x,
        clip.y,
        clip.width,
        clip.height,
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
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);
    api_draw_tile(
        scope->runtime->context,
        scope->surface_token,
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
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);

    if( shape != TORIRS_PLUGIN_HULL_BOUNDS && shape != TORIRS_PLUGIN_HULL_MESH )
        return TORIRS_RESULT_INVALID;
    api_draw_hull(
        scope->runtime->context, scope->surface_token, element_id, rgb, v2_alpha(alpha), shape);
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
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);
    char const* operations[1];

    assert(action);
    if( !action[0] || strlen(action) >= TORIRS_UI_ACTION_MAX || !v2_clip_rect(scope, &rect) )
        return TORIRS_RESULT_INVALID;
    operations[0] = action;
    return api_hit_region(
               scope->runtime->context,
               scope->surface_token,
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

static enum ToriRS_Result
v2_builder_action_region_id(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_Rect rect,
    char const* action,
    uint32_t action_id)
{
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);
    char const* operations[1];

    assert(action);
    if( !action_id || (action_id & 0x80000000u) != 0 || !action[0] ||
        strlen(action) >= TORIRS_UI_ACTION_MAX || !v2_clip_rect(scope, &rect) )
        return TORIRS_RESULT_INVALID;
    operations[0] = action;
    return api_hit_region(
               scope->runtime->context,
               scope->surface_token,
               rect.x,
               rect.y,
               rect.width,
               rect.height,
               operations,
               1,
               action_id)
               ? TORIRS_RESULT_OK
               : TORIRS_RESULT_BUDGET;
}

static bool
v2_builder_context(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_DrawContext* out)
{
    struct PluginV2DrawScope* scope = v2_draw_scope(draw);
    struct ToriRS_DrawContext snapshot;
    uint32_t capacity;

    assert(out);
    capacity = out->struct_size;
    if( !scope->context_valid || capacity < sizeof(uint32_t) )
        return false;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    snapshot.bounds = scope->local_bounds;
    snapshot.clip = scope->local_clip;
    return v2_output_copy(out, capacity, &snapshot, sizeof(snapshot));
}

void
plugin_v2_runtime_DrawBegin(
    struct PluginV2Runtime* runtime,
    void* surface_token,
    struct PluginV2DrawScope* scope,
    struct ToriRS_DrawBuilder* out)
{
    assert(runtime);
    assert(surface_token);
    assert(scope);
    assert(out);
    memset(scope, 0, sizeof(*scope));
    scope->runtime = runtime;
    scope->surface_token = surface_token;
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
        .image_clip = v2_builder_image_clip,
        .action_region_id = v2_builder_action_region_id,
        .context = v2_builder_context,
    };
}

void
plugin_v2_runtime_DrawEnd(
    struct PluginV2DrawScope* scope,
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
plugin_v2_runtime_DrawClip(
    struct PluginV2DrawScope* scope,
    struct ToriRS_Rect clip)
{
    assert(scope);
    assert(scope->active);
    scope->clip_active = clip.width > 0 && clip.height > 0;
    scope->clip = clip;
}

void
plugin_v2_runtime_DrawRegion(
    struct PluginV2DrawScope* scope,
    struct ToriRS_Rect region)
{
    assert(scope);
    assert(scope->active);
    scope->context_valid = region.width > 0 && region.height > 0;
    if( !scope->context_valid )
        return;
    scope->origin_x = region.x;
    scope->origin_y = region.y;
    scope->local_bounds = (struct ToriRS_Rect){ 0, 0, region.width, region.height };
    scope->local_clip = scope->local_bounds;
    scope->clip_active = true;
    scope->clip = region;
}

static struct PluginV2FrameScope*
v2_frame_scope(struct ToriRS_FrameBuilder* frame)
{
    struct PluginV2FrameScope* scope;

    assert(frame);
    scope = frame->implementation;
    assert(scope);
    assert(scope->active);
    assert(scope->runtime);
    return scope;
}

static int
v2_surface_slot(int surface)
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
    struct PluginV2FrameScope* scope,
    struct ToriRS_ImageRef ref,
    int* out_slot)
{
    int image_slot = -1;

    assert(scope);
    assert(out_slot);
    if( ref.value != 0 )
    {
        image_slot = plugin_v2_runtime_ImageUnbox(scope->runtime, ref);
        if( image_slot < 0 )
        {
            scope->invalid = true;
            return false;
        }
        for( int i = 0; i < scope->image_ref_count; i++ )
            if( scope->image_refs[i].value == ref.value )
            {
                *out_slot = image_slot;
                return true;
            }
        if( scope->image_ref_count >= TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX )
        {
            scope->invalid = true;
            return false;
        }
        scope->image_refs[scope->image_ref_count++] = ref;
    }
    *out_slot = image_slot;
    return true;
}

static bool
v2_frame_ui_node_images(
    struct PluginV2FrameScope* scope,
    struct ToriRS_UiNode const* node)
{
    size_t const size = node->struct_size ? node->struct_size : TORIRS_UI_NODE_LEGACY_SIZE;
    int image_slot;

    assert(scope);
    assert(node);
    if( !v2_ui_node_images_valid(scope->runtime, node) )
        return false;
    if( size >= offsetof(struct ToriRS_UiNode, image) + sizeof(node->image) &&
        node->image.value != 0 && !v2_frame_image(scope, node->image, &image_slot) )
        return false;
    if( size >= offsetof(struct ToriRS_UiNode, state_images) + sizeof(node->state_images) )
        for( int state = 0; state < TORIRS_UI_VISUAL_STATE_COUNT; state++ )
            if( (node->state_image_mask & (1u << state)) != 0 &&
                node->state_images[state].value != 0 &&
                !v2_frame_image(scope, node->state_images[state], &image_slot) )
                return false;
    return true;
}

static void
v2_builder_surface(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    struct ToriRS_Rect rect)
{
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const surface_token = v2_surface_slot(surface);

    if( surface_token < 0 || !v2_frame_rect_valid(&rect) ||
        (scope->surface_mask & (1u << surface)) != 0 )
    {
        scope->invalid = true;
        return;
    }
    scope->surface_mask |= 1u << surface;
        (void)api_layout_slot(
            scope->runtime->context, surface_token, rect.x, rect.y, rect.width, rect.height);
}

static void
v2_builder_surface_member(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    int member,
    struct ToriRS_Rect rect)
{
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const surface_token = v2_surface_slot(surface);

    if( surface_token < 0 || member < 0 || !v2_frame_rect_valid(&rect) ||
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
        (void)api_layout_slot_at(
            scope->runtime->context,
            surface_token,
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
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const surface_token = v2_surface_slot(surface);
    int image = -1;
    int mask = -1;

    if( !skin || surface_token < 0 ||
        (skin->struct_size != 0 && skin->struct_size < sizeof(*skin)) )
    {
        scope->invalid = true;
        return;
    }
    if( !v2_frame_image(scope, skin->image, &image) ||
        !v2_frame_image(scope, skin->mask, &mask) )
        return;
        (void)api_layout_slot_skin(
            scope->runtime->context, surface_token, image, mask);
}

static void
v2_builder_surface_overlay(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    struct ToriRS_FrameSurfaceOverlay const* overlay)
{
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);
    int const surface_token = v2_surface_slot(surface);
    int image = -1;

    if( !overlay || surface_token < 0 ||
        (scope->surface_mask & (1u << surface)) == 0 ||
        (overlay->struct_size != 0 && overlay->struct_size < sizeof(*overlay)) ||
        overlay->image.value == 0 || overlay->alpha < 0 || overlay->alpha > 255 )
    {
        scope->invalid = true;
        return;
    }
    if( !v2_frame_image(scope, overlay->image, &image) )
        return;
        (void)api_layout_slot_overlay(
            scope->runtime->context,
            surface_token,
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
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);

    assert(name);
    assert(node);
    if( !v2_frame_ui_node_images(scope, node) )
    {
        scope->invalid = true;
        return;
    }
    plugin_v2_frame_ui_node(
        scope->runtime->context->host,
        scope->runtime->context,
        name,
        node);
}

static void
v2_builder_scrollbar(
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameScrollbar const* skin)
{
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);
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
        (void)api_layout_scrollbar(
            scope->runtime->context,
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
    struct PluginV2FrameScope* scope = v2_frame_scope(frame);

    assert(reason);
    (void)snprintf(scope->reason, sizeof(scope->reason), "%s", reason);
}

void
plugin_v2_runtime_FrameBegin(
    struct PluginV2Runtime* runtime,
    struct PluginV2FrameScope* scope,
    struct ToriRS_FrameBuilder* out)
{
    assert(runtime);
    assert(scope);
    assert(out);
    memset(scope, 0, sizeof(*scope));
    scope->runtime = runtime;
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
plugin_v2_runtime_FrameEnd(
    struct PluginV2FrameScope* scope,
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
plugin_v2_runtime_FrameReason(struct PluginV2FrameScope const* scope)
{
    assert(scope);
    return scope->reason;
}

bool
plugin_v2_runtime_FrameValid(struct PluginV2FrameScope const* scope)
{
    assert(scope);
    if( scope->invalid || (scope->surface_mask & (1u << TORIRS_SURFACE_VIEWPORT)) == 0 )
        return false;
    for( int i = 0; i < scope->image_ref_count; i++ )
        if( plugin_v2_runtime_ImageUnbox(scope->runtime, scope->image_refs[i]) < 0 )
            return false;
    return true;
}

static struct PluginV2PanelScope*
v2_panel_scope(struct ToriRS_PanelBuilder* panel)
{
    struct PluginV2PanelScope* scope;

    assert(panel);
    scope = panel->implementation;
    assert(scope);
    assert(scope->active);
    assert(scope->runtime);
    return scope;
}

static void
v2_panel_generated_id(
    struct PluginV2PanelScope* scope,
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
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

    assert(text);
    v2_panel_generated_id(scope, "heading", id, sizeof(id));
        (void)api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_SECTION, id, text);
}

static void
v2_builder_paragraph(
    struct ToriRS_PanelBuilder* panel,
    char const* text)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

    assert(text);
    v2_panel_generated_id(scope, "paragraph", id, sizeof(id));
        (void)api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_PARAGRAPH, id, text);
}

static void
v2_builder_toggle(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    bool value)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    if( api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_TOGGLE, id, label) )
        (void)api_panel_set_value(scope->runtime->context, id, value ? 1 : 0);
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

static void
v2_builder_select(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);
    struct PluginV2Runtime* runtime = scope->runtime;
    assert(id);
    assert(value);
    if( !v2_panel_options_valid(options, option_count) )
        return;
    plugin_v2_panel_select(
        runtime->context->host,
        runtime->context,
        id,
        label,
        value,
        options,
        option_count);
}

static void
v2_builder_button(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    bool enabled)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    if( api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_BUTTON, id, label) )
        (void)api_panel_set_value(scope->runtime->context, id, enabled ? 1 : 0);
}

static void
v2_builder_custom(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    int preferred_height)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    if( api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_CUSTOM, id, NULL) )
        (void)api_panel_set_height(
            scope->runtime->context, id, preferred_height);
}

static void
v2_builder_label(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* text)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    assert(text);
    if( api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_LABEL, id, NULL) )
        (void)api_panel_set_text(
            scope->runtime->context, id, text);
}

static void
v2_builder_key_value(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    char const* value)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);

    assert(id);
    assert(label);
    assert(value);
    if( api_panel_widget(
            scope->runtime->context, TORIRS_PLUGIN_W_KEY_VALUE, id, label) )
        (void)api_panel_set_text(
            scope->runtime->context, id, value);
}

static enum ToriRS_Result
v2_builder_node(
    struct ToriRS_PanelBuilder* panel,
    struct ToriRS_PanelNode const* node)
{
    struct PluginV2PanelScope* scope = v2_panel_scope(panel);
    int kind = -1;

    assert(node);
    if( node->struct_size < TORIRS_PANEL_NODE_REQUIRED_SIZE || !node->id ||
        !node->id[0] || node->kind < TORIRS_PANEL_HEADING ||
        node->kind > TORIRS_PANEL_CUSTOM )
        return TORIRS_RESULT_INVALID;
    switch( node->kind )
    {
    case TORIRS_PANEL_HEADING: kind = TORIRS_PLUGIN_W_SECTION; break;
    case TORIRS_PANEL_PARAGRAPH: kind = TORIRS_PLUGIN_W_PARAGRAPH; break;
    case TORIRS_PANEL_LABEL: kind = TORIRS_PLUGIN_W_LABEL; break;
    case TORIRS_PANEL_KEY_VALUE: kind = TORIRS_PLUGIN_W_KEY_VALUE; break;
    case TORIRS_PANEL_TOGGLE: kind = TORIRS_PLUGIN_W_TOGGLE; break;
    case TORIRS_PANEL_INPUT: kind = TORIRS_PLUGIN_W_INPUT; break;
    case TORIRS_PANEL_TEXTAREA: kind = TORIRS_PLUGIN_W_TEXTAREA; break;
    case TORIRS_PANEL_SELECT: kind = TORIRS_PLUGIN_W_DROPDOWN; break;
    case TORIRS_PANEL_BUTTON: kind = TORIRS_PLUGIN_W_BUTTON; break;
    case TORIRS_PANEL_SEPARATOR: kind = TORIRS_PLUGIN_W_SEPARATOR; break;
    case TORIRS_PANEL_PROGRESS: kind = TORIRS_PLUGIN_W_PROGRESS; break;
    case TORIRS_PANEL_ERROR: kind = TORIRS_PLUGIN_W_ERROR; break;
    case TORIRS_PANEL_LIST_ROW: kind = TORIRS_PLUGIN_W_LIST_ROW; break;
    case TORIRS_PANEL_CUSTOM: kind = TORIRS_PLUGIN_W_CUSTOM; break;
    default: return TORIRS_RESULT_INVALID;
    }
    if( node->kind == TORIRS_PANEL_SELECT )
    {
        if( !node->text || !v2_panel_options_valid(node->options, node->option_count) )
            return TORIRS_RESULT_INVALID;
        plugin_v2_panel_select(
            scope->runtime->context->host,
            scope->runtime->context,
            node->id,
            node->label,
            node->text,
            node->options,
            node->option_count);
        return TORIRS_RESULT_OK;
    }
    if( false ||
        !api_panel_widget(scope->runtime->context, kind, node->id, node->label) )
        return TORIRS_RESULT_BUDGET;
    {
        if( node->text )
            (void)api_panel_set_text(
                scope->runtime->context, node->id, node->text);
        if( (node->kind == TORIRS_PANEL_TOGGLE ||
             node->kind == TORIRS_PANEL_PROGRESS ||
             node->kind == TORIRS_PANEL_LIST_ROW ||
             node->kind == TORIRS_PANEL_BUTTON) )
            (void)api_panel_set_value(
                scope->runtime->context, node->id, node->value);
        if( (node->kind == TORIRS_PANEL_CUSTOM ||
             node->kind == TORIRS_PANEL_TEXTAREA) )
            (void)api_panel_set_height(
                scope->runtime->context, node->id, node->preferred_height);
    }
    return TORIRS_RESULT_OK;
}

void
plugin_v2_runtime_PanelBegin(
    struct PluginV2Runtime* runtime,
    struct PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* out)
{
    assert(runtime);
    assert(scope);
    assert(out);
    memset(scope, 0, sizeof(*scope));
    scope->runtime = runtime;
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
        .label = v2_builder_label,
        .key_value = v2_builder_key_value,
        .node = v2_builder_node,
    };
}

void
plugin_v2_runtime_PanelEnd(
    struct PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* builder)
{
    assert(scope);
    assert(builder);
    assert(scope->active);
    assert(builder->implementation == scope);
    scope->active = false;
    builder->implementation = NULL;
}

static bool
v2_runtime_init(
    struct PluginV2Runtime* runtime,
    struct ToriRS_PluginCtx* context,
    bool preserve_resources)
{
    assert(runtime);
    assert(context);

    if( preserve_resources )
    {
        memset(&runtime->api, 0, sizeof(runtime->api));
        memset(&runtime->client_api, 0, sizeof(runtime->client_api));
        memset(&runtime->game_api, 0, sizeof(runtime->game_api));
        runtime->context = NULL;
    }
    else
        memset(runtime, 0, sizeof(*runtime));
    runtime->context = context;
    if( ((uint32_t)runtime->context->index) > V2_RESOURCE_NAMESPACE_MASK )
    {
        memset(&runtime->api, 0, sizeof(runtime->api));
        memset(&runtime->client_api, 0, sizeof(runtime->client_api));
        memset(&runtime->game_api, 0, sizeof(runtime->game_api));
        runtime->context = NULL;
        return false;
    }

    runtime->client_api = (struct ToriRS_ClientApiV2){
        .struct_size = sizeof(runtime->client_api),
        .display_get = v2_client_display_get,
        .display_set = v2_client_display_set,
        .feature_next = v2_client_feature_next,
        .feature_get = v2_client_feature_get,
        .feature_set = v2_client_feature_set,
        .world_cycle = v2_client_world_cycle,
        .datestamp = v2_client_datestamp,
        .setting_color = v2_client_setting_color,
        .memory_bytes = v2_client_memory_bytes,
        .disable_self = v2_client_disable_self,
    };
    runtime->game_api = (struct ToriRS_GameApiV2){
        .struct_size = sizeof(runtime->game_api),
        .skill = v2_game_skill,
        .run_energy = v2_game_run_energy,
        .inventory_size = v2_game_inventory_size,
        .inventory_slot = v2_game_inventory_slot,
        .item_info = v2_game_item_info,
        .item_image = v2_game_item_image,
        .highlight_next = v2_game_highlight_next,
        .loot_source_next = v2_game_loot_source_next,
        .loot_row_next = v2_game_loot_row_next,
        .entity_part = v2_game_entity_part,
        .entity_look = v2_game_entity_look,
        .entity_ops = v2_game_entity_ops,
        .loot_revision = v2_game_loot_revision,
        .loot_source_clear = v2_game_loot_source_clear,
    };

    runtime->api = (struct ToriRS_ApiV2){
        .struct_size = sizeof(runtime->api),
        .major_version = TORIRS_PLUGIN_API_V2_MAJOR,
        .minor_version = TORIRS_PLUGIN_API_V2_MINOR,
        .instance = runtime,
        .core = {
            .struct_size = sizeof(runtime->api.core),
            .log = v2_core_log,
            .notify = v2_core_notify,
            .screen = v2_core_screen,
            .frame_ms = v2_core_frame_ms,
            .frame_work_us = v2_core_frame_work_us,
            .lane = v2_core_lane,
            .capability = v2_core_capability,
            .plugin_id = v2_core_plugin_id,
        },
        .config = {
            .struct_size = sizeof(runtime->api.config),
            .has = v2_config_has,
            .get_bool = v2_config_get_bool,
            .get_int = v2_config_get_int,
            .get_color = v2_config_get_color,
            .get_string = v2_config_get_string,
            .set = v2_config_set,
        },
        .world = {
            .struct_size = sizeof(runtime->api.world),
            .local_player = v2_world_local_player,
            .npc_next = v2_world_npc_next,
            .npc_by_slot = v2_world_npc_by_slot,
            .player_next = v2_world_player_next,
            .item_next = v2_world_item_next,
            .scenery_next = v2_world_scenery_next,
        },
        .input = {
            .struct_size = sizeof(runtime->api.input),
            .key_held = v2_input_key_held,
            .pointer = v2_input_pointer,
            .hover_tile = v2_input_hover_tile,
            .hover_entity = v2_input_hover_entity,
            .text_input = v2_input_text_input,
            .chat_focus = v2_input_chat_focus,
        },
        .ui = {
            .struct_size = sizeof(runtime->api.ui),
            .ref = v2_ui_ref,
            .info = v2_ui_info,
            .invoke = v2_ui_invoke,
            .contribution_info = v2_ui_contribution_info,
            .update = v2_ui_update,
            .menu_add = v2_ui_menu_add,
            .set_enabled = v2_ui_set_enabled,
        },
        .placement = {
            .struct_size = sizeof(runtime->api.placement),
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
            .struct_size = sizeof(runtime->api.frame),
            .offer_next = v2_frame_offer_next,
            .selection = v2_frame_selection,
            .select = v2_frame_select,
            .invalidate = v2_frame_invalidate,
            .surface_native_size = v2_frame_surface_native_size,
        },
        .draw = {
            .struct_size = sizeof(runtime->api.draw),
            .project = v2_draw_project,
            .element_height = v2_draw_element_height,
            .hsl_from_rgb = v2_draw_hsl_from_rgb,
            .hsl_to_rgb = v2_draw_hsl_to_rgb,
        },
        .assets = {
            .struct_size = sizeof(runtime->api.assets),
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
            .image_pixels = v2_assets_image_pixels,
            .image_compose = v2_assets_image_compose,
        },
        .scene = {
            .struct_size = sizeof(runtime->api.scene),
            .mesh_create = v2_scene_mesh_create,
            .mesh_destroy = v2_scene_mesh_destroy,
            .mesh_vertex = v2_scene_mesh_vertex,
            .mesh_face = v2_scene_mesh_face,
            .instance_create = v2_scene_instance_create,
            .instance_destroy = v2_scene_instance_destroy,
            .instance_model = v2_scene_instance_model,
            .instance_position = v2_scene_instance_position,
            .instance_active = v2_scene_instance_active,
            .instance_mesh = v2_scene_instance_mesh,
            .instance_cache_model = v2_scene_instance_cache_model,
            .instance_recolor = v2_scene_instance_recolor,
            .instance_clear_recolors = v2_scene_instance_clear_recolors,
            .instance_animation = v2_scene_instance_animation,
            .instance_light = v2_scene_instance_light,
            .instance_ready = v2_scene_instance_ready,
        },
        .panel = {
            .struct_size = sizeof(runtime->api.panel),
            .request = v2_panel_request,
            .invalidate = v2_panel_invalidate,
            .attention = v2_panel_attention,
            .set_text = v2_panel_set_text,
            .set_value = v2_panel_set_value,
            .set_height = v2_panel_set_height,
            .set_options = v2_panel_set_options,
            .redraw = v2_panel_redraw,
        },
        .cache = {
            .struct_size = sizeof(runtime->api.cache),
            .frame_root = v2_cache_frame_root,
            .varbit = v2_cache_varbit,
            .varp = v2_cache_varp,
            .component_rect = v2_cache_component_rect,
            .invoke = v2_cache_invoke,
            .named_id = v2_cache_named_id,
            .tab_active = v2_cache_tab_active,
            .tab_enabled = v2_cache_tab_enabled,
            .tab_select = v2_cache_tab_select,
        },
        .client = &runtime->client_api,
        .game = &runtime->game_api,
    };
    return true;
}

bool
plugin_v2_runtime_Init(
    struct PluginV2Runtime* runtime,
    struct ToriRS_PluginCtx* context)
{
    return v2_runtime_init(runtime, context, false);
}

bool
plugin_v2_runtime_Reinit(
    struct PluginV2Runtime* runtime,
    struct ToriRS_PluginCtx* context)
{
    return v2_runtime_init(runtime, context, true);
}

void
plugin_v2_runtime_Reset(struct PluginV2Runtime* runtime)
{
    assert(runtime);
    v2_resource_reset(runtime->image_tokens, TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX);
    v2_resource_reset(runtime->model_tokens, TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX);
    v2_resource_reset(runtime->mesh_tokens, TORIRS_PLUGIN_V2_MESH_TOKENS_MAX);
    v2_resource_reset(runtime->instance_tokens, TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX);
    memset(&runtime->api, 0, sizeof(runtime->api));
    memset(&runtime->client_api, 0, sizeof(runtime->client_api));
    memset(&runtime->game_api, 0, sizeof(runtime->game_api));
    runtime->context = NULL;
}

struct ToriRS_ApiV2*
plugin_v2_runtime_Api(struct PluginV2Runtime* runtime)
{
    assert(runtime);
    return &runtime->api;
}
