#include "plugin/torirs_plugin_ui.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* frame.* is closed.  A plugin may use these names, but cannot accidentally
 * coin a second public vocabulary item with a typo in it. */
static char const* const UI_CORE_NAMES[] = {
    "frame.viewport",
    "frame.minimap",
    "frame.minimap.housing",
    "frame.compass",
    "frame.chat",
    "frame.chat.buttons",
    "frame.chat.button.public",
    "frame.chat.button.private",
    "frame.chat.button.trade",
    "frame.chat.button.report",
    "frame.sidebar",
    "frame.sidebar.rail",
    "frame.sidebar.tab.0",
    "frame.sidebar.tab.1",
    "frame.sidebar.tab.2",
    "frame.sidebar.tab.3",
    "frame.sidebar.tab.4",
    "frame.sidebar.tab.5",
    "frame.sidebar.tab.6",
    "frame.sidebar.tab.7",
    "frame.sidebar.tab.8",
    "frame.sidebar.tab.9",
    "frame.sidebar.tab.10",
    "frame.sidebar.tab.11",
    "frame.sidebar.tab.12",
    "frame.sidebar.tab.13",
    "frame.modal",
    "frame.orbs",
    "frame.orb.hitpoints",
    "frame.orb.prayer",
    "frame.orb.run",
    "frame.orb.special",
    "frame.xp.drops",
};

struct UiAlias
{
    char const* old_name;
    char const* canonical;
};

/* Temporary migration vocabulary.  It is intentionally data, not scattered
 * special cases, so deleting aliases after the bundled migration is one edit. */
static struct UiAlias const UI_ALIASES[] = {
    { "viewport",         "frame.viewport"           },
    { "minimap",          "frame.minimap"            },
    { "minimap_edge",     "frame.minimap.housing"    },
    { "compass",          "frame.compass"            },
    { "chat",             "frame.chat"               },
    { "frame_chat",       "frame.chat"               },
    { "chat_buttons",     "frame.chat.buttons"       },
    { "report_button",    "frame.chat.button.report" },
    { "sidebar",          "frame.sidebar"            },
    { "frame_sidebar",    "frame.sidebar"            },
    { "main_modal",       "frame.modal"              },
    { "frame_main_modal", "frame.modal"              },
    { "orbs",             "frame.orbs"               },
    { "frame_orbs",       "frame.orbs"               },
    { "orb_hitpoints",    "frame.orb.hitpoints"      },
    { "orb_prayer",       "frame.orb.prayer"         },
    { "orb_run",          "frame.orb.run"            },
    { "orb_spec",         "frame.orb.special"        },
    { "xp_drops",         "frame.xp.drops"           },
};

static uint32_t
ui_revision_next(uint32_t revision)
{
    revision++;
    return revision ? revision : 1;
}

static uint32_t
ui_hash(char const* text)
{
    uint32_t hash = 2166136261u;

    assert(text);
    while( *text )
    {
        hash ^= (unsigned char)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static char const*
ui_alias_canonical(char const* name)
{
    assert(name);
    for( size_t i = 0; i < sizeof(UI_ALIASES) / sizeof(UI_ALIASES[0]); i++ )
        if( strcmp(name, UI_ALIASES[i].old_name) == 0 )
            return UI_ALIASES[i].canonical;
    return name;
}

static bool
ui_segment_valid(
    char const* begin,
    char const* end)
{
    assert(begin);
    assert(end);
    assert(end >= begin);

    if( begin == end || *begin == '-' || end[-1] == '-' )
        return false;
    for( char const* at = begin; at < end; at++ )
    {
        char const c = *at;
        if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' )
            continue;
        return false;
    }
    return true;
}

bool
ToriRS_UiRegistry_NameIsValid(char const* name)
{
    char const* segment;
    char const* at;
    size_t length;
    int segments = 1;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= TORIRS_UI_NAME_MAX )
        return false;

    segment = name;
    for( at = name;; at++ )
    {
        if( *at != '.' && *at != '\0' )
            continue;
        if( !ui_segment_valid(segment, at) )
            return false;
        if( *at == '\0' )
            break;
        segments++;
        segment = at + 1;
    }

    if( strncmp(name, "frame.", 6) == 0 )
        return segments >= 2;
    if( strncmp(name, "plugin.", 7) == 0 )
        return segments >= 3;
    return false;
}

static bool
ui_plugin_id_valid(char const* plugin)
{
    char const* end;

    assert(plugin);
    end = plugin + strlen(plugin);
    return (size_t)(end - plugin) < TORIRS_PLUGIN_NAME_MAX && ui_segment_valid(plugin, end);
}

static int
ui_find_index(
    struct ToriRS_UiRegistry const* registry,
    char const* canonical)
{
    uint32_t bucket;

    assert(registry);
    assert(canonical);
    bucket = ui_hash(canonical) & (TORIRS_UI_REGISTRY_HASH_SIZE - 1u);
    for( int probe = 0; probe < TORIRS_UI_REGISTRY_HASH_SIZE; probe++ )
    {
        uint16_t const entry = registry->_hash[bucket];
        if( entry == 0 )
            return -1;
        if( strcmp(registry->_nodes[entry - 1].name, canonical) == 0 )
            return entry - 1;
        bucket = (bucket + 1u) & (TORIRS_UI_REGISTRY_HASH_SIZE - 1u);
    }
    return -1;
}

static bool
ui_is_core_name(char const* name)
{
    assert(name);
    for( size_t i = 0; i < sizeof(UI_CORE_NAMES) / sizeof(UI_CORE_NAMES[0]); i++ )
        if( strcmp(name, UI_CORE_NAMES[i]) == 0 )
            return true;
    return false;
}

static struct ToriRS_UiNodeRef
ui_intern_unchecked(
    struct ToriRS_UiRegistry* registry,
    char const* canonical)
{
    struct ToriRS_UiNodeRef result = { 0 };
    uint32_t bucket;
    int index;

    assert(registry);
    assert(canonical);
    index = ui_find_index(registry, canonical);
    if( index >= 0 )
    {
        result.value = (uint32_t)index + 1u;
        return result;
    }
    if( registry->_node_count >= TORIRS_UI_REGISTRY_NODES_MAX )
        return result;

    index = registry->_node_count;
    bucket = ui_hash(canonical) & (TORIRS_UI_REGISTRY_HASH_SIZE - 1u);
    while( registry->_hash[bucket] != 0 )
        bucket = (bucket + 1u) & (TORIRS_UI_REGISTRY_HASH_SIZE - 1u);

    memset(&registry->_nodes[index], 0, sizeof(registry->_nodes[index]));
    (void)snprintf(registry->_nodes[index].name, TORIRS_UI_NAME_MAX, "%s", canonical);
    registry->_nodes[index].first_contribution = -1;
    registry->_nodes[index].dirty = true;
    registry->_hash[bucket] = (uint16_t)(index + 1);
    registry->_node_count++;
    result.value = (uint32_t)index + 1u;
    return result;
}

static void
ui_names_rollback(
    struct ToriRS_UiRegistry* registry,
    int node_count)
{
    assert(registry);
    assert(node_count >= 0);
    assert(node_count <= registry->_node_count);
    if( node_count == registry->_node_count )
        return;

    for( int i = node_count; i < registry->_node_count; i++ )
    {
        assert(!registry->_nodes[i].change_queued);
        assert(registry->_nodes[i].base_facets == 0);
        assert(registry->_nodes[i].first_contribution == -1);
        memset(&registry->_nodes[i], 0, sizeof(registry->_nodes[i]));
    }
    registry->_node_count = node_count;
    memset(registry->_hash, 0, sizeof(registry->_hash));
    for( int i = 0; i < registry->_node_count; i++ )
    {
        uint32_t bucket = ui_hash(registry->_nodes[i].name) & (TORIRS_UI_REGISTRY_HASH_SIZE - 1u);
        while( registry->_hash[bucket] != 0 )
            bucket = (bucket + 1u) & (TORIRS_UI_REGISTRY_HASH_SIZE - 1u);
        registry->_hash[bucket] = (uint16_t)(i + 1);
    }
}

void
ToriRS_UiRegistry_Init(struct ToriRS_UiRegistry* registry)
{
    assert(registry);
    _Static_assert(
        (TORIRS_UI_REGISTRY_HASH_SIZE & (TORIRS_UI_REGISTRY_HASH_SIZE - 1)) == 0,
        "named UI hash table size must be a power of two");
    _Static_assert(
        TORIRS_UI_REGISTRY_NODES_MAX < UINT16_MAX, "named UI node index must fit the hash table");
    _Static_assert(
        sizeof(UI_CORE_NAMES) / sizeof(UI_CORE_NAMES[0]) <= TORIRS_UI_REGISTRY_NODES_MAX,
        "core UI vocabulary must fit the registry");

    memset(registry, 0, sizeof(*registry));
    registry->_revision = 1;
    registry->_base_generation = 1;
    registry->_next_contribution_serial = 1;
    for( size_t i = 0; i < sizeof(UI_CORE_NAMES) / sizeof(UI_CORE_NAMES[0]); i++ )
    {
        int const count_before = registry->_node_count;
        struct ToriRS_UiNodeRef const ref = ui_intern_unchecked(registry, UI_CORE_NAMES[i]);
        assert(ref.value != 0);
        assert(registry->_node_count == count_before + 1);
        (void)count_before;
        (void)ref;
    }

#ifndef NDEBUG
    for( size_t i = 0; i < sizeof(UI_ALIASES) / sizeof(UI_ALIASES[0]); i++ )
    {
        assert(ui_is_core_name(UI_ALIASES[i].canonical));
        for( size_t j = i + 1; j < sizeof(UI_ALIASES) / sizeof(UI_ALIASES[0]); j++ )
            assert(strcmp(UI_ALIASES[i].old_name, UI_ALIASES[j].old_name) != 0);
    }
#endif
}

struct ToriRS_UiNodeRef
ToriRS_UiRegistry_Find(
    struct ToriRS_UiRegistry const* registry,
    char const* name)
{
    struct ToriRS_UiNodeRef result = { 0 };
    char const* canonical;
    int index;

    assert(registry);
    assert(name);
    canonical = ui_alias_canonical(name);
    if( !ToriRS_UiRegistry_NameIsValid(canonical) )
        return result;
    index = ui_find_index(registry, canonical);
    if( index >= 0 )
        result.value = (uint32_t)index + 1u;
    return result;
}

struct ToriRS_UiNodeRef
ToriRS_UiRegistry_Ref(
    struct ToriRS_UiRegistry* registry,
    char const* name)
{
    struct ToriRS_UiNodeRef result = { 0 };
    char const* canonical;

    assert(registry);
    assert(name);
    canonical = ui_alias_canonical(name);
    if( !ToriRS_UiRegistry_NameIsValid(canonical) )
        return result;
    if( strncmp(canonical, "frame.", 6) == 0 && !ui_is_core_name(canonical) )
        return result;
    return ui_intern_unchecked(registry, canonical);
}

static bool
ui_private_canonical(
    char const* plugin,
    char const* name,
    char* out,
    size_t out_size)
{
    char const* alias;
    int n;

    assert(plugin);
    assert(name);
    assert(out);
    assert(out_size > 0);
    if( !ui_plugin_id_valid(plugin) || !name[0] )
        return false;

    alias = ui_alias_canonical(name);
    if( alias != name || strncmp(name, "frame.", 6) == 0 )
        n = snprintf(out, out_size, "%s", alias);
    else if( strncmp(name, "plugin.", 7) == 0 )
    {
        size_t const plugin_length = strlen(plugin);
        size_t const name_length = strlen(name);
        if( name_length <= 7 + plugin_length )
            return false;
        if( strncmp(name + 7, plugin, plugin_length) != 0 || name[7 + plugin_length] != '.' )
            return false;
        n = snprintf(out, out_size, "%s", name);
    }
    else
        n = snprintf(out, out_size, "plugin.%s.%s", plugin, name);

    return n > 0 && (size_t)n < out_size && ToriRS_UiRegistry_NameIsValid(out);
}

struct ToriRS_UiNodeRef
ToriRS_UiRegistry_PrivateRef(
    struct ToriRS_UiRegistry* registry,
    char const* plugin,
    char const* name)
{
    struct ToriRS_UiNodeRef result = { 0 };
    char canonical[TORIRS_UI_NAME_MAX];

    assert(registry);
    assert(plugin);
    assert(name);
    if( !ui_private_canonical(plugin, name, canonical, sizeof(canonical)) )
        return result;
    return ToriRS_UiRegistry_Ref(registry, canonical);
}

char const*
ToriRS_UiRegistry_Name(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiNodeRef ref)
{
    assert(registry);
    if( ref.value == 0 || ref.value > (uint32_t)registry->_node_count )
        return NULL;
    return registry->_nodes[ref.value - 1u].name;
}

int
ToriRS_UiRegistry_NodeCount(struct ToriRS_UiRegistry const* registry)
{
    assert(registry);
    return registry->_node_count;
}

struct ToriRS_UiNodeRef
ToriRS_UiRegistry_NodeAt(
    struct ToriRS_UiRegistry const* registry,
    int index)
{
    struct ToriRS_UiNodeRef ref = { 0 };

    assert(registry);
    if( index >= 0 && index < registry->_node_count )
        ref.value = (uint32_t)index + 1u;
    return ref;
}

uint32_t
ToriRS_UiRegistry_Revision(struct ToriRS_UiRegistry const* registry)
{
    assert(registry);
    return registry->_revision;
}

uint32_t
ToriRS_UiRegistry_BaseGeneration(struct ToriRS_UiRegistry const* registry)
{
    assert(registry);
    return registry->_base_generation;
}

static bool
ui_facets_valid(uint32_t facets)
{
    return facets != 0 && (facets & ~TORIRS_UI_FACET_ALL) == 0;
}

static int
ui_facet_index(uint32_t facet)
{
    assert(
        facet == TORIRS_UI_FACET_BOUNDS || facet == TORIRS_UI_FACET_APPEARANCE ||
        facet == TORIRS_UI_FACET_ACTIONS);
    if( facet == TORIRS_UI_FACET_BOUNDS )
        return 0;
    if( facet == TORIRS_UI_FACET_APPEARANCE )
        return 1;
    return 2;
}

static bool
ui_copy_text(
    char* out,
    size_t out_size,
    char const* text)
{
    int n;

    assert(out);
    assert(out_size > 0);
    if( !text )
    {
        out[0] = '\0';
        return true;
    }
    n = snprintf(out, out_size, "%s", text);
    return n >= 0 && (size_t)n < out_size;
}

static bool
ui_rect_valid(struct ToriRS_Rect const* rect)
{
    int64_t right;
    int64_t bottom;

    assert(rect);
    if( rect->width < 0 || rect->height < 0 )
        return false;
    right = (int64_t)rect->x + rect->width;
    bottom = (int64_t)rect->y + rect->height;
    return right >= INT_MIN && right <= INT_MAX && bottom >= INT_MIN && bottom <= INT_MAX;
}

static bool
ui_node_field_available(
    struct ToriRS_UiNode const* value,
    size_t offset,
    size_t size)
{
    assert(value);
    /* A zero size is the compatibility spelling used by early in-tree
     * declarations. It promises only the original prefix. */
    return value->struct_size != 0 && value->struct_size >= offset + size;
}

#define UI_NODE_FIELD_AVAILABLE(value, field)                                             \
    ui_node_field_available(                                                              \
        (value), offsetof(struct ToriRS_UiNode, field), sizeof((value)->field))

static bool
ui_action_valid(char const* action)
{
    return action && action[0] && strlen(action) < TORIRS_UI_ACTION_MAX;
}

static bool
ui_value_shape_valid(
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    assert(value);
    if( value->struct_size != 0 && value->struct_size < TORIRS_UI_NODE_LEGACY_SIZE )
        return false;
    if( value->flags & ~(uint32_t)(TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED |
                                   TORIRS_UI_NODE_BLOCKS_FRAME | TORIRS_UI_NODE_BLOCKS_OVERLAY |
                                   TORIRS_UI_NODE_ACTIVE) )
        return false;
    if( (facets & TORIRS_UI_FACET_BOUNDS) != 0 )
    {
        if( !ui_rect_valid(&value->bounds) )
            return false;
        if( value->anchor < TORIRS_ANCHOR_TOP_LEFT || value->anchor > TORIRS_ANCHOR_BOTTOM_RIGHT )
            return false;
        if( value->paint_order < TORIRS_UI_PAINT_BEFORE_PARENT ||
            value->paint_order > TORIRS_UI_PAINT_AFTER_PARENT )
            return false;
        if( UI_NODE_FIELD_AVAILABLE(value, clip) &&
            (value->clip < TORIRS_UI_CLIP_NONE || value->clip > TORIRS_UI_CLIP_BOUNDS) )
            return false;
    }
    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 && value->label &&
        strlen(value->label) >= TORIRS_UI_LABEL_MAX )
        return false;
    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 && value->image.value < 0 )
        return false;
    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 &&
        UI_NODE_FIELD_AVAILABLE(value, state_image_mask) )
    {
        uint32_t const known_mask = (1u << TORIRS_UI_VISUAL_STATE_COUNT) - 1u;
        if( (value->state_image_mask & ~known_mask) != 0 )
            return false;
        if( value->state_image_mask != 0 && !UI_NODE_FIELD_AVAILABLE(value, state_images) )
            return false;
        if( UI_NODE_FIELD_AVAILABLE(value, state_images) )
            for( int i = 0; i < TORIRS_UI_VISUAL_STATE_COUNT; i++ )
                if( (value->state_image_mask & (1u << i)) != 0 &&
                    value->state_images[i].value < 0 )
                    return false;
    }
    if( (facets & TORIRS_UI_FACET_ACTIONS) != 0 && value->action &&
        !ui_action_valid(value->action) )
        return false;
    if( (facets & TORIRS_UI_FACET_ACTIONS) != 0 &&
        UI_NODE_FIELD_AVAILABLE(value, hit_rect_mode) )
    {
        if( value->hit_rect_mode < TORIRS_UI_HIT_RECT_BOUNDS ||
            value->hit_rect_mode > TORIRS_UI_HIT_RECT_CUSTOM )
            return false;
        if( value->hit_rect_mode == TORIRS_UI_HIT_RECT_CUSTOM &&
            (!UI_NODE_FIELD_AVAILABLE(value, hit_rect) || !ui_rect_valid(&value->hit_rect)) )
            return false;
    }
    if( (facets & TORIRS_UI_FACET_ACTIONS) != 0 &&
        UI_NODE_FIELD_AVAILABLE(value, action_count) )
    {
        if( value->action_count > TORIRS_UI_NAMED_ACTIONS_MAX )
            return false;
        if( value->action_count != 0 && !UI_NODE_FIELD_AVAILABLE(value, actions) )
            return false;
        for( uint32_t i = 0; i < value->action_count; i++ )
        {
            if( !ui_action_valid(value->actions[i]) )
                return false;
            for( uint32_t j = 0; j < i; j++ )
                if( strcmp(value->actions[i], value->actions[j]) == 0 )
                    return false;
        }
    }
    return true;
}

static void
ui_copy_nonname_value(
    struct ToriRS_UiStoredNode* out,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    assert(out);
    assert(value);
    memset(out, 0, sizeof(*out));
    for( int i = 0; i < TORIRS_UI_VISUAL_STATE_COUNT; i++ )
        out->state_images[i].value = 0;
    out->image.value = 0;
    if( (facets & TORIRS_UI_FACET_BOUNDS) != 0 )
    {
        out->bounds = value->bounds;
        out->anchor = value->anchor;
        out->paint_order = value->paint_order;
        out->clip = UI_NODE_FIELD_AVAILABLE(value, clip) ? value->clip : TORIRS_UI_CLIP_NONE;
        out->flags |= value->flags &
                      (TORIRS_UI_NODE_BLOCKS_FRAME | TORIRS_UI_NODE_BLOCKS_OVERLAY);
    }
    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 )
    {
        out->flags |= value->flags & (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ACTIVE);
        out->state_images[TORIRS_UI_VISUAL_IDLE] = value->image;
        if( UI_NODE_FIELD_AVAILABLE(value, state_image_mask) &&
            UI_NODE_FIELD_AVAILABLE(value, state_images) )
            for( int i = 0; i < TORIRS_UI_VISUAL_STATE_COUNT; i++ )
                if( (value->state_image_mask & (1u << i)) != 0 )
                    out->state_images[i] = value->state_images[i];
        out->image = out->state_images[TORIRS_UI_VISUAL_IDLE];
        (void)ui_copy_text(out->label, sizeof(out->label), value->label);
        out->label_x = UI_NODE_FIELD_AVAILABLE(value, label_x) ? value->label_x : 0;
        out->label_y = UI_NODE_FIELD_AVAILABLE(value, label_y) ? value->label_y : 0;
    }
    if( (facets & TORIRS_UI_FACET_ACTIONS) != 0 )
    {
        out->flags |= value->flags & TORIRS_UI_NODE_ENABLED;
        out->hit_rect_mode = TORIRS_UI_HIT_RECT_BOUNDS;
        if( UI_NODE_FIELD_AVAILABLE(value, hit_rect_mode) &&
            value->hit_rect_mode == TORIRS_UI_HIT_RECT_CUSTOM &&
            UI_NODE_FIELD_AVAILABLE(value, hit_rect) )
        {
            out->hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM;
            out->hit_rect = value->hit_rect;
        }
        if( UI_NODE_FIELD_AVAILABLE(value, action_count) && value->action_count != 0 &&
            UI_NODE_FIELD_AVAILABLE(value, actions) )
        {
            out->action_count = value->action_count;
            for( uint32_t i = 0; i < out->action_count; i++ )
                (void)ui_copy_text(
                    out->actions[i], sizeof(out->actions[i]), value->actions[i]);
        }
        else if( value->action )
        {
            out->action_count = 1;
            (void)ui_copy_text(out->actions[0], sizeof(out->actions[0]), value->action);
        }
        if( out->action_count != 0 )
            (void)ui_copy_text(out->action, sizeof(out->action), out->actions[0]);
    }
}

static void
ui_apply_facet(
    struct ToriRS_UiStoredNode* destination,
    struct ToriRS_UiStoredNode const* source,
    uint32_t facet)
{
    assert(destination);
    assert(source);
    if( facet == TORIRS_UI_FACET_BOUNDS )
    {
        destination->bounds = source->bounds;
        destination->parent = source->parent;
        destination->anchor = source->anchor;
        destination->paint_order = source->paint_order;
        destination->clip = source->clip;
        destination->flags &=
            ~(uint32_t)(TORIRS_UI_NODE_BLOCKS_FRAME | TORIRS_UI_NODE_BLOCKS_OVERLAY);
        destination->flags |=
            source->flags & (TORIRS_UI_NODE_BLOCKS_FRAME | TORIRS_UI_NODE_BLOCKS_OVERLAY);
    }
    else if( facet == TORIRS_UI_FACET_APPEARANCE )
    {
        destination->image = source->image;
        memcpy(destination->state_images, source->state_images, sizeof(destination->state_images));
        memcpy(destination->label, source->label, sizeof(destination->label));
        destination->label_x = source->label_x;
        destination->label_y = source->label_y;
        destination->flags &= ~(uint32_t)(TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ACTIVE);
        destination->flags |=
            source->flags & (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ACTIVE);
    }
    else
    {
        assert(facet == TORIRS_UI_FACET_ACTIONS);
        destination->hit_rect_mode = source->hit_rect_mode;
        destination->hit_rect = source->hit_rect;
        destination->action_count = source->action_count;
        memcpy(destination->actions, source->actions, sizeof(destination->actions));
        memcpy(destination->action, source->action, sizeof(destination->action));
        destination->flags &= ~(uint32_t)TORIRS_UI_NODE_ENABLED;
        destination->flags |= source->flags & TORIRS_UI_NODE_ENABLED;
    }
}

static void
ui_clear_facet(
    struct ToriRS_UiStoredNode* value,
    uint32_t facet)
{
    struct ToriRS_UiStoredNode empty;

    assert(value);
    memset(&empty, 0, sizeof(empty));
    ui_apply_facet(value, &empty, facet);
}

static bool
ui_copy_base_value(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef node,
    uint32_t facets,
    struct ToriRS_UiNode const* value,
    struct ToriRS_UiStoredNode* out)
{
    assert(registry);
    assert(value);
    assert(out);
    ui_copy_nonname_value(out, facets, value);
    if( (facets & TORIRS_UI_FACET_BOUNDS) != 0 && value->parent && value->parent[0] )
    {
        out->parent = ToriRS_UiRegistry_Ref(registry, value->parent);
        if( out->parent.value == 0 || out->parent.value == node.value )
            return false;
    }
    return true;
}

static bool
ui_copy_contribution_value(
    struct ToriRS_UiRegistry* registry,
    char const* plugin,
    struct ToriRS_UiNodeRef node,
    uint32_t facets,
    struct ToriRS_UiNode const* value,
    struct ToriRS_UiStoredNode* out)
{
    assert(registry);
    assert(plugin);
    assert(value);
    assert(out);
    ui_copy_nonname_value(out, facets, value);
    if( (facets & TORIRS_UI_FACET_BOUNDS) != 0 && value->parent && value->parent[0] )
    {
        out->parent = ToriRS_UiRegistry_PrivateRef(registry, plugin, value->parent);
        if( out->parent.value == 0 || out->parent.value == node.value )
            return false;
    }
    return true;
}

/* Follow every declared bounds-parent edge, not just today's winner.  This is
 * deliberately stronger than checking the current resolved tree: removing a
 * conflicting contribution later must not uncover a dormant cycle. */
static bool
ui_declared_reaches(
    struct ToriRS_UiRegistry const* registry,
    int from,
    int target,
    bool* visited)
{
    struct ToriRS_UiRegistryNode const* node;

    assert(registry);
    assert(from >= 0 && from < registry->_node_count);
    assert(target >= 0 && target < registry->_node_count);
    assert(visited);
    if( from == target )
        return true;
    if( visited[from] )
        return false;
    visited[from] = true;
    node = &registry->_nodes[from];

    if( (node->base_facets & TORIRS_UI_FACET_BOUNDS) != 0 && node->base.parent.value != 0 &&
        ui_declared_reaches(registry, (int)node->base.parent.value - 1, target, visited) )
        return true;
    for( int at = node->first_contribution; at >= 0;
         at = registry->_contributions[at].next_for_node )
    {
        struct ToriRS_UiRegistryContribution const* contribution = &registry->_contributions[at];
        assert(contribution->used);
        if( (contribution->facets & TORIRS_UI_FACET_BOUNDS) != 0 &&
            contribution->value.parent.value != 0 &&
            ui_declared_reaches(
                registry, (int)contribution->value.parent.value - 1, target, visited) )
            return true;
    }
    return false;
}

static bool
ui_parent_would_cycle(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeRef parent)
{
    bool visited[TORIRS_UI_REGISTRY_NODES_MAX] = { false };

    assert(registry);
    assert(node.value > 0 && node.value <= (uint32_t)registry->_node_count);
    if( parent.value == 0 )
        return false;
    assert(parent.value <= (uint32_t)registry->_node_count);
    return ui_declared_reaches(registry, (int)parent.value - 1, (int)node.value - 1, visited);
}

static void
ui_queue_change(
    struct ToriRS_UiRegistry* registry,
    int node,
    uint32_t facets)
{
    int tail;

    assert(registry);
    assert(node >= 0);
    assert(node < registry->_node_count);
    assert(ui_facets_valid(facets));
    registry->_nodes[node].dirty = true;
    registry->_nodes[node].changed_facets |= facets;
    if( registry->_nodes[node].change_queued )
        return;

    assert(registry->_change_count < TORIRS_UI_REGISTRY_NODES_MAX);
    tail = (registry->_change_head + registry->_change_count) % TORIRS_UI_REGISTRY_NODES_MAX;
    registry->_change_queue[tail] = (uint16_t)(node + 1);
    registry->_change_count++;
    registry->_nodes[node].change_queued = true;
}

static uint32_t
ui_node_contribution_facets(
    struct ToriRS_UiRegistry const* registry,
    int node_index)
{
    uint32_t facets = 0;

    assert(registry);
    assert(node_index >= 0 && node_index < registry->_node_count);
    for( int at = registry->_nodes[node_index].first_contribution; at >= 0;
         at = registry->_contributions[at].next_for_node )
    {
        assert(registry->_contributions[at].used);
        facets |= registry->_contributions[at].facets;
    }
    return facets;
}

void
ToriRS_UiRegistry_ClearBase(struct ToriRS_UiRegistry* registry)
{
    bool changed = false;

    assert(registry);
    for( int i = 0; i < registry->_node_count; i++ )
    {
        struct ToriRS_UiRegistryNode* node = &registry->_nodes[i];
        uint32_t old_facets;
        if( node->base_facets == 0 )
            continue;
        old_facets = node->base_facets;
        memset(&node->base, 0, sizeof(node->base));
        memset(node->base_provider, 0, sizeof(node->base_provider));
        node->base_facets = 0;
        ui_queue_change(registry, i, old_facets | ui_node_contribution_facets(registry, i));
        changed = true;
    }
    registry->_base_generation = ui_revision_next(registry->_base_generation);
    if( changed )
        registry->_revision = ui_revision_next(registry->_revision);
}

enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_AddBase(
    struct ToriRS_UiRegistry* registry,
    char const* provider,
    char const* name,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    struct ToriRS_UiStoredNode stored;
    struct ToriRS_UiNodeRef node_ref;
    struct ToriRS_UiRegistryNode* node;
    uint32_t old_base_facets;
    int names_before;

    assert(registry);
    assert(provider);
    assert(name);
    assert(value);
    names_before = registry->_node_count;
    if( !provider[0] || strlen(provider) >= TORIRS_PLUGIN_NAME_MAX || !ui_facets_valid(facets) ||
        !ui_value_shape_valid(facets, value) )
        return TORIRS_UI_REGISTRY_INVALID;
    node_ref = ToriRS_UiRegistry_Ref(registry, name);
    if( node_ref.value == 0 )
        return registry->_node_count >= TORIRS_UI_REGISTRY_NODES_MAX ? TORIRS_UI_REGISTRY_FULL
                                                                     : TORIRS_UI_REGISTRY_INVALID;
    node = &registry->_nodes[node_ref.value - 1u];
    old_base_facets = node->base_facets;
    if( (node->base_facets & facets) != 0 )
        return TORIRS_UI_REGISTRY_DUPLICATE;
    if( !ui_copy_base_value(registry, node_ref, facets, value, &stored) )
    {
        enum ToriRS_UiRegistryResult const result =
            registry->_node_count >= TORIRS_UI_REGISTRY_NODES_MAX ? TORIRS_UI_REGISTRY_FULL
                                                                  : TORIRS_UI_REGISTRY_INVALID;
        ui_names_rollback(registry, names_before);
        return result;
    }
    if( (facets & TORIRS_UI_FACET_BOUNDS) != 0 &&
        ui_parent_would_cycle(registry, node_ref, stored.parent) )
    {
        ui_names_rollback(registry, names_before);
        return TORIRS_UI_REGISTRY_INVALID;
    }

    for( uint32_t facet = TORIRS_UI_FACET_BOUNDS; facet <= TORIRS_UI_FACET_ACTIONS; facet <<= 1u )
    {
        int const index = ui_facet_index(facet);
        if( (facets & facet) == 0 )
            continue;
        ui_apply_facet(&node->base, &stored, facet);
        (void)snprintf(node->base_provider[index], TORIRS_PLUGIN_NAME_MAX, "%s", provider);
    }
    node->base_facets |= facets;
    if( old_base_facets == 0 )
        facets |= ui_node_contribution_facets(registry, (int)node_ref.value - 1);
    ui_queue_change(registry, (int)(node_ref.value - 1u), facets);
    registry->_revision = ui_revision_next(registry->_revision);
    return TORIRS_UI_REGISTRY_OK;
}

static bool
ui_base_canonical(
    char const* name,
    char* out)
{
    char const* canonical;
    int written;

    assert(name);
    assert(out);
    canonical = ui_alias_canonical(name);
    if( !ToriRS_UiRegistry_NameIsValid(canonical) )
        return false;
    if( strncmp(canonical, "frame.", 6) == 0 && !ui_is_core_name(canonical) )
        return false;
    written = snprintf(out, TORIRS_UI_NAME_MAX, "%s", canonical);
    return written > 0 && written < TORIRS_UI_NAME_MAX;
}

static struct ToriRS_UiNodeRef
ui_candidate_base_parent(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiBaseDeclaration const* declarations,
    int declaration_count,
    int node)
{
    struct ToriRS_UiNodeRef none = { 0 };

    assert(registry);
    assert(declaration_count >= 0);
    assert(node >= 0 && node < registry->_node_count);
    for( int i = 0; i < declaration_count; i++ )
    {
        struct ToriRS_UiNodeRef declared;
        char canonical[TORIRS_UI_NAME_MAX];
        bool valid;

        if( (declarations[i].facets & TORIRS_UI_FACET_BOUNDS) == 0 )
            continue;
        valid = ui_base_canonical(declarations[i].node, canonical);
        assert(valid);
        if( !valid )
            return none;
        declared = ToriRS_UiRegistry_Find(registry, canonical);
        assert(declared.value != 0);
        if( declared.value != (uint32_t)node + 1u )
            continue;
        if( !declarations[i].value.parent || !declarations[i].value.parent[0] )
            return none;
        valid = ui_base_canonical(declarations[i].value.parent, canonical);
        assert(valid);
        if( !valid )
            return none;
        return ToriRS_UiRegistry_Find(registry, canonical);
    }
    return none;
}

static bool
ui_candidate_graph_visit(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiBaseDeclaration const* declarations,
    int declaration_count,
    int node_index,
    unsigned char* state)
{
    struct ToriRS_UiNodeRef parent;
    struct ToriRS_UiRegistryNode const* node;

    assert(registry);
    assert(node_index >= 0 && node_index < registry->_node_count);
    assert(state);
    if( state[node_index] == 1 )
        return false;
    if( state[node_index] == 2 )
        return true;
    state[node_index] = 1;
    node = &registry->_nodes[node_index];

    parent = ui_candidate_base_parent(registry, declarations, declaration_count, node_index);
    if( parent.value != 0 &&
        !ui_candidate_graph_visit(
            registry, declarations, declaration_count, (int)parent.value - 1, state) )
        return false;
    for( int at = node->first_contribution; at >= 0;
         at = registry->_contributions[at].next_for_node )
    {
        struct ToriRS_UiRegistryContribution const* contribution = &registry->_contributions[at];
        assert(contribution->used);
        parent = contribution->value.parent;
        if( (contribution->facets & TORIRS_UI_FACET_BOUNDS) != 0 && parent.value != 0 &&
            !ui_candidate_graph_visit(
                registry, declarations, declaration_count, (int)parent.value - 1, state) )
            return false;
    }
    state[node_index] = 2;
    return true;
}

static bool
ui_candidate_graph_valid(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiBaseDeclaration const* declarations,
    int declaration_count)
{
    unsigned char state[TORIRS_UI_REGISTRY_NODES_MAX] = { 0 };

    assert(registry);
    assert(declaration_count >= 0);
    for( int i = 0; i < registry->_node_count; i++ )
        if( !ui_candidate_graph_visit(registry, declarations, declaration_count, i, state) )
            return false;
    return true;
}

enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_ReplaceBase(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiBaseDeclaration const* declarations,
    int declaration_count)
{
    uint32_t old_base_facets[TORIRS_UI_REGISTRY_NODES_MAX] = { 0 };
    int names_before;

    assert(registry);
    if( declaration_count < 0 || declaration_count > TORIRS_UI_BASE_DECLARATIONS_MAX ||
        (declaration_count > 0 && !declarations) )
        return TORIRS_UI_REGISTRY_INVALID;

    /* Validate the whole scratch declaration before interning or publishing
     * any of it.  Alias-equivalent names count as the same declaration. */
    for( int i = 0; i < declaration_count; i++ )
    {
        struct ToriRS_UiBaseDeclaration const* declaration = &declarations[i];
        char name[TORIRS_UI_NAME_MAX];
        char parent[TORIRS_UI_NAME_MAX];

        if( !declaration->provider || !declaration->provider[0] ||
            strlen(declaration->provider) >= TORIRS_PLUGIN_NAME_MAX || !declaration->node ||
            !ui_facets_valid(declaration->facets) ||
            !ui_value_shape_valid(declaration->facets, &declaration->value) ||
            !ui_base_canonical(declaration->node, name) )
            return TORIRS_UI_REGISTRY_INVALID;
        if( (declaration->facets & TORIRS_UI_FACET_BOUNDS) != 0 && declaration->value.parent &&
            declaration->value.parent[0] )
        {
            if( !ui_base_canonical(declaration->value.parent, parent) || strcmp(name, parent) == 0 )
                return TORIRS_UI_REGISTRY_INVALID;
        }
        for( int j = 0; j < i; j++ )
        {
            char prior[TORIRS_UI_NAME_MAX];
            bool const valid = ui_base_canonical(declarations[j].node, prior);
            assert(valid);
            if( !valid )
                return TORIRS_UI_REGISTRY_INVALID;
            if( strcmp(name, prior) == 0 && (declaration->facets & declarations[j].facets) != 0 )
                return TORIRS_UI_REGISTRY_DUPLICATE;
        }
    }

    names_before = registry->_node_count;
    for( int i = 0; i < declaration_count; i++ )
    {
        struct ToriRS_UiBaseDeclaration const* declaration = &declarations[i];
        struct ToriRS_UiNodeRef ref = ToriRS_UiRegistry_Ref(registry, declaration->node);
        if( ref.value == 0 )
        {
            ui_names_rollback(registry, names_before);
            return TORIRS_UI_REGISTRY_FULL;
        }
        if( (declaration->facets & TORIRS_UI_FACET_BOUNDS) != 0 && declaration->value.parent &&
            declaration->value.parent[0] )
        {
            ref = ToriRS_UiRegistry_Ref(registry, declaration->value.parent);
            if( ref.value == 0 )
            {
                ui_names_rollback(registry, names_before);
                return TORIRS_UI_REGISTRY_FULL;
            }
        }
    }
    if( !ui_candidate_graph_valid(registry, declarations, declaration_count) )
    {
        ui_names_rollback(registry, names_before);
        return TORIRS_UI_REGISTRY_INVALID;
    }
    for( int i = 0; i < declaration_count; i++ )
    {
        struct ToriRS_UiStoredNode stored;
        struct ToriRS_UiNodeRef const ref = ToriRS_UiRegistry_Ref(registry, declarations[i].node);
        if( !ui_copy_base_value(
                registry, ref, declarations[i].facets, &declarations[i].value, &stored) )
        {
            ui_names_rollback(registry, names_before);
            return TORIRS_UI_REGISTRY_INVALID;
        }
    }

    /* From here nothing can fail: the old base remains live until every
     * candidate invariant and every required intern slot has been proved. */
    for( int i = 0; i < registry->_node_count; i++ )
    {
        struct ToriRS_UiRegistryNode* node = &registry->_nodes[i];
        old_base_facets[i] = node->base_facets;
        if( node->base_facets == 0 )
            continue;
        memset(&node->base, 0, sizeof(node->base));
        memset(node->base_provider, 0, sizeof(node->base_provider));
        node->base_facets = 0;
    }
    for( int i = 0; i < declaration_count; i++ )
    {
        struct ToriRS_UiBaseDeclaration const* declaration = &declarations[i];
        struct ToriRS_UiStoredNode stored;
        struct ToriRS_UiNodeRef const ref = ToriRS_UiRegistry_Ref(registry, declaration->node);
        struct ToriRS_UiRegistryNode* node = &registry->_nodes[ref.value - 1u];
        bool copied;

        copied =
            ui_copy_base_value(registry, ref, declaration->facets, &declaration->value, &stored);
        assert(copied);
        (void)copied;
        for( uint32_t facet = TORIRS_UI_FACET_BOUNDS; facet <= TORIRS_UI_FACET_ACTIONS;
             facet <<= 1u )
        {
            int const facet_index = ui_facet_index(facet);
            if( (declaration->facets & facet) == 0 )
                continue;
            ui_apply_facet(&node->base, &stored, facet);
            (void)snprintf(
                node->base_provider[facet_index],
                TORIRS_PLUGIN_NAME_MAX,
                "%s",
                declaration->provider);
        }
        node->base_facets |= declaration->facets;
    }
    for( int i = 0; i < registry->_node_count; i++ )
    {
        struct ToriRS_UiRegistryNode const* node = &registry->_nodes[i];
        uint32_t changed_facets = old_base_facets[i] | node->base_facets;

        if( (old_base_facets[i] == 0) != (node->base_facets == 0) )
            changed_facets |= ui_node_contribution_facets(registry, i);
        if( changed_facets != 0 )
            ui_queue_change(registry, i, changed_facets);
    }
    registry->_base_generation = ui_revision_next(registry->_base_generation);
    registry->_revision = ui_revision_next(registry->_revision);
    return TORIRS_UI_REGISTRY_OK;
}

int
ToriRS_UiRegistry_RemoveBaseProvider(
    struct ToriRS_UiRegistry* registry,
    char const* provider)
{
    int removed = 0;
    bool changed = false;

    assert(registry);
    assert(provider);
    for( int i = 0; i < registry->_node_count; i++ )
    {
        struct ToriRS_UiRegistryNode* node = &registry->_nodes[i];
        uint32_t const old_base_facets = node->base_facets;
        uint32_t removed_facets = 0;
        for( uint32_t facet = TORIRS_UI_FACET_BOUNDS; facet <= TORIRS_UI_FACET_ACTIONS;
             facet <<= 1u )
        {
            int const index = ui_facet_index(facet);
            if( (node->base_facets & facet) == 0 ||
                strcmp(node->base_provider[index], provider) != 0 )
                continue;
            node->base_facets &= ~facet;
            node->base_provider[index][0] = '\0';
            ui_clear_facet(&node->base, facet);
            removed_facets |= facet;
            removed++;
            changed = true;
        }
        if( removed_facets != 0 )
        {
            if( old_base_facets != 0 && node->base_facets == 0 )
                removed_facets |= ui_node_contribution_facets(registry, i);
            ui_queue_change(registry, i, removed_facets);
        }
    }
    if( changed )
        registry->_revision = ui_revision_next(registry->_revision);
    return removed;
}

static bool
ui_contribution_mode_valid(int mode)
{
    return mode == TORIRS_UI_MODIFY || mode == TORIRS_UI_PROVIDE_IF_MISSING ||
           mode == TORIRS_UI_REPLACE_OR_PROVIDE;
}

static int
ui_contribution_slot(struct ToriRS_UiRegistry const* registry)
{
    assert(registry);
    for( int i = 0; i < TORIRS_UI_REGISTRY_CONTRIBUTIONS_MAX; i++ )
        if( !registry->_contributions[i].used )
            return i;
    return -1;
}

static struct ToriRS_UiRegistryContribution*
ui_contribution_by_ref(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiContributionRef ref);

static int
ui_contribution_find(
    struct ToriRS_UiRegistry const* registry,
    char const* plugin,
    int node)
{
    int at;

    assert(registry);
    assert(plugin);
    assert(node >= 0);
    at = registry->_nodes[node].first_contribution;
    while( at >= 0 )
    {
        struct ToriRS_UiRegistryContribution const* contribution = &registry->_contributions[at];
        assert(contribution->used);
        if( strcmp(contribution->plugin, plugin) == 0 )
            return at;
        at = contribution->next_for_node;
    }
    return -1;
}

enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_AddContribution(
    struct ToriRS_UiRegistry* registry,
    char const* plugin,
    struct ToriRS_UiContribution const* declaration,
    struct ToriRS_UiContributionRef* out_ref)
{
    struct ToriRS_UiStoredNode stored;
    struct ToriRS_UiNodeRef node_ref;
    struct ToriRS_UiRegistryContribution* contribution;
    int slot;
    int names_before;

    assert(registry);
    assert(plugin);
    assert(declaration);
    if( out_ref )
        out_ref->value = 0;
    if( !declaration->node || !ui_plugin_id_valid(plugin) ||
        (declaration->struct_size != 0 &&
         declaration->struct_size <
             offsetof(struct ToriRS_UiContribution, value) + TORIRS_UI_NODE_LEGACY_SIZE) ||
        !ui_contribution_mode_valid(declaration->mode) || !ui_facets_valid(declaration->facets) ||
        !ui_value_shape_valid(declaration->facets, &declaration->value) )
        return TORIRS_UI_REGISTRY_INVALID;

    /* Check capacity before interning either name, keeping failure atomic. */
    slot = ui_contribution_slot(registry);
    if( slot < 0 )
        return TORIRS_UI_REGISTRY_FULL;
    names_before = registry->_node_count;
    node_ref = ToriRS_UiRegistry_PrivateRef(registry, plugin, declaration->node);
    if( node_ref.value == 0 )
        return registry->_node_count >= TORIRS_UI_REGISTRY_NODES_MAX ? TORIRS_UI_REGISTRY_FULL
                                                                     : TORIRS_UI_REGISTRY_INVALID;
    if( ui_contribution_find(registry, plugin, (int)node_ref.value - 1) >= 0 )
        return TORIRS_UI_REGISTRY_DUPLICATE;
    if( !ui_copy_contribution_value(
            registry, plugin, node_ref, declaration->facets, &declaration->value, &stored) )
    {
        enum ToriRS_UiRegistryResult const result =
            registry->_node_count >= TORIRS_UI_REGISTRY_NODES_MAX ? TORIRS_UI_REGISTRY_FULL
                                                                  : TORIRS_UI_REGISTRY_INVALID;
        ui_names_rollback(registry, names_before);
        return result;
    }
    if( (declaration->facets & TORIRS_UI_FACET_BOUNDS) != 0 &&
        ui_parent_would_cycle(registry, node_ref, stored.parent) )
    {
        ui_names_rollback(registry, names_before);
        return TORIRS_UI_REGISTRY_INVALID;
    }

    contribution = &registry->_contributions[slot];
    memset(contribution, 0, sizeof(*contribution));
    contribution->used = true;
    contribution->serial = registry->_next_contribution_serial;
    registry->_next_contribution_serial = ui_revision_next(registry->_next_contribution_serial);
    contribution->node = (int)node_ref.value - 1;
    contribution->next_for_node = registry->_nodes[contribution->node].first_contribution;
    contribution->mode = declaration->mode;
    contribution->facets = declaration->facets;
    (void)snprintf(contribution->plugin, sizeof(contribution->plugin), "%s", plugin);
    contribution->value = stored;
    registry->_nodes[contribution->node].first_contribution = slot;
    registry->_contribution_count++;
    ui_queue_change(registry, contribution->node, contribution->facets);
    registry->_revision = ui_revision_next(registry->_revision);
    if( out_ref )
        out_ref->value = contribution->serial;
    return TORIRS_UI_REGISTRY_OK;
}

enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_UpdateContribution(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiContributionRef contribution_ref,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    struct ToriRS_UiRegistryContribution* contribution;
    struct ToriRS_UiStoredNode stored;
    struct ToriRS_UiStoredNode candidate;
    struct ToriRS_UiNodeRef node;
    int names_before;

    assert(registry);
    assert(value);
    contribution = ui_contribution_by_ref(registry, contribution_ref);
    if( !contribution || !ui_facets_valid(facets) || (facets & ~contribution->facets) != 0 ||
        !ui_value_shape_valid(facets, value) )
        return TORIRS_UI_REGISTRY_INVALID;

    names_before = registry->_node_count;
    node.value = (uint32_t)contribution->node + 1u;
    if( !ui_copy_contribution_value(
            registry, contribution->plugin, node, facets, value, &stored) )
    {
        enum ToriRS_UiRegistryResult const result =
            registry->_node_count >= TORIRS_UI_REGISTRY_NODES_MAX ? TORIRS_UI_REGISTRY_FULL
                                                                  : TORIRS_UI_REGISTRY_INVALID;
        ui_names_rollback(registry, names_before);
        return result;
    }
    if( (facets & TORIRS_UI_FACET_BOUNDS) != 0 &&
        ui_parent_would_cycle(registry, node, stored.parent) )
    {
        ui_names_rollback(registry, names_before);
        return TORIRS_UI_REGISTRY_INVALID;
    }

    candidate = contribution->value;
    for( uint32_t facet = TORIRS_UI_FACET_BOUNDS; facet <= TORIRS_UI_FACET_ACTIONS; facet <<= 1u )
        if( (facets & facet) != 0 )
            ui_apply_facet(&candidate, &stored, facet);
    if( memcmp(&candidate, &contribution->value, sizeof(candidate)) == 0 )
    {
        ui_names_rollback(registry, names_before);
        return TORIRS_UI_REGISTRY_OK;
    }

    contribution->value = candidate;
    ui_queue_change(registry, contribution->node, facets);
    registry->_revision = ui_revision_next(registry->_revision);
    return TORIRS_UI_REGISTRY_OK;
}

static bool
ui_contribution_eligible(
    struct ToriRS_UiRegistryContribution const* contribution,
    bool base_present)
{
    assert(contribution);
    if( contribution->mode == TORIRS_UI_MODIFY )
        return base_present;
    if( contribution->mode == TORIRS_UI_PROVIDE_IF_MISSING )
        return !base_present;
    assert(contribution->mode == TORIRS_UI_REPLACE_OR_PROVIDE);
    return true;
}

static char*
ui_resolved_provider(
    struct ToriRS_UiResolvedNode* resolved,
    uint32_t facet)
{
    assert(resolved);
    if( facet == TORIRS_UI_FACET_BOUNDS )
        return resolved->bounds_provider;
    if( facet == TORIRS_UI_FACET_APPEARANCE )
        return resolved->appearance_provider;
    assert(facet == TORIRS_UI_FACET_ACTIONS);
    return resolved->actions_provider;
}

static void
ui_resolve_node(
    struct ToriRS_UiRegistry* registry,
    int node_index)
{
    struct ToriRS_UiRegistryNode* node;
    bool base_present;

    assert(registry);
    assert(node_index >= 0);
    assert(node_index < registry->_node_count);
    node = &registry->_nodes[node_index];
    if( !node->dirty )
        return;

    memset(&node->resolved, 0, sizeof(node->resolved));
    node->resolved.ref.value = (uint32_t)node_index + 1u;
    node->resolved.available_facets = node->base_facets;
    node->resolved.value = node->base;
    node->resolved.revision = registry->_revision;
    for( uint32_t facet = TORIRS_UI_FACET_BOUNDS; facet <= TORIRS_UI_FACET_ACTIONS; facet <<= 1u )
    {
        int const index = ui_facet_index(facet);
        if( (node->base_facets & facet) != 0 )
            (void)snprintf(
                ui_resolved_provider(&node->resolved, facet),
                TORIRS_PLUGIN_NAME_MAX,
                "%s",
                node->base_provider[index]);
    }

    base_present = node->base_facets != 0;
    for( int at = node->first_contribution; at >= 0;
         at = registry->_contributions[at].next_for_node )
    {
        struct ToriRS_UiRegistryContribution* contribution = &registry->_contributions[at];
        assert(contribution->used);
        contribution->active_facets = 0;
        contribution->conflict_facets = 0;
        contribution->target_absent = contribution->mode == TORIRS_UI_MODIFY && !base_present;
    }

    for( uint32_t facet = TORIRS_UI_FACET_BOUNDS; facet <= TORIRS_UI_FACET_ACTIONS; facet <<= 1u )
    {
        struct ToriRS_UiRegistryContribution* winner = NULL;
        int contenders = 0;

        for( int at = node->first_contribution; at >= 0;
             at = registry->_contributions[at].next_for_node )
        {
            struct ToriRS_UiRegistryContribution* contribution = &registry->_contributions[at];
            if( (contribution->facets & facet) == 0 ||
                !ui_contribution_eligible(contribution, base_present) )
                continue;
            winner = contribution;
            contenders++;
        }

        if( contenders == 1 )
        {
            assert(winner);
            ui_apply_facet(&node->resolved.value, &winner->value, facet);
            node->resolved.available_facets |= facet;
            winner->active_facets |= facet;
            (void)snprintf(
                ui_resolved_provider(&node->resolved, facet),
                TORIRS_PLUGIN_NAME_MAX,
                "%s",
                winner->plugin);
        }
        else if( contenders > 1 )
        {
            node->resolved.conflict_facets |= facet;
            for( int at = node->first_contribution; at >= 0;
                 at = registry->_contributions[at].next_for_node )
            {
                struct ToriRS_UiRegistryContribution* contribution = &registry->_contributions[at];
                if( (contribution->facets & facet) != 0 &&
                    ui_contribution_eligible(contribution, base_present) )
                    contribution->conflict_facets |= facet;
            }
        }
    }
    if( (node->resolved.available_facets & TORIRS_UI_FACET_ACTIONS) != 0 &&
        node->resolved.value.hit_rect_mode == TORIRS_UI_HIT_RECT_BOUNDS )
        node->resolved.value.hit_rect = node->resolved.value.bounds;
    node->dirty = false;
}

bool
ToriRS_UiRegistry_Resolve(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef ref,
    struct ToriRS_UiResolvedNode* out)
{
    int node;

    assert(registry);
    assert(out);
    if( ref.value == 0 || ref.value > (uint32_t)registry->_node_count )
        return false;
    node = (int)ref.value - 1;
    ui_resolve_node(registry, node);
    registry->_nodes[node].resolved.revision = registry->_revision;
    *out = registry->_nodes[node].resolved;
    return out->available_facets != 0;
}

static struct ToriRS_UiRegistryContribution*
ui_contribution_by_ref(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiContributionRef ref)
{
    assert(registry);
    if( ref.value == 0 )
        return NULL;
    for( int i = 0; i < TORIRS_UI_REGISTRY_CONTRIBUTIONS_MAX; i++ )
        if( registry->_contributions[i].used && registry->_contributions[i].serial == ref.value )
            return &registry->_contributions[i];
    return NULL;
}

static void
ui_first_other_conflict(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiRegistryContribution const* subject,
    char* out)
{
    struct ToriRS_UiRegistryNode const* node;

    assert(registry);
    assert(subject);
    assert(out);
    out[0] = '\0';
    node = &registry->_nodes[subject->node];
    for( int at = node->first_contribution; at >= 0;
         at = registry->_contributions[at].next_for_node )
    {
        struct ToriRS_UiRegistryContribution const* candidate = &registry->_contributions[at];
        if( candidate == subject || (candidate->facets & subject->conflict_facets) == 0 ||
            !ui_contribution_eligible(candidate, node->base_facets != 0) )
            continue;
        if( !out[0] || strcmp(candidate->plugin, out) < 0 )
            (void)snprintf(out, TORIRS_PLUGIN_NAME_MAX, "%s", candidate->plugin);
    }
}

bool
ToriRS_UiRegistry_ContributionStatus(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiContributionRef ref,
    struct ToriRS_UiContributionStatus* out)
{
    struct ToriRS_UiRegistryContribution* contribution;

    assert(registry);
    assert(out);
    contribution = ui_contribution_by_ref(registry, ref);
    if( !contribution )
        return false;
    ui_resolve_node(registry, contribution->node);
    memset(out, 0, sizeof(*out));
    out->active_facets = contribution->active_facets;
    out->conflict_facets = contribution->conflict_facets;
    if( contribution->conflict_facets )
    {
        out->state = TORIRS_UI_CONTRIBUTION_CONFLICT;
        ui_first_other_conflict(registry, contribution, out->conflict_plugin);
    }
    else if( contribution->active_facets )
        out->state = TORIRS_UI_CONTRIBUTION_ACTIVE;
    else if( contribution->target_absent )
        out->state = TORIRS_UI_CONTRIBUTION_TARGET_ABSENT;
    else
        out->state = TORIRS_UI_CONTRIBUTION_INACTIVE;
    return true;
}

static int
ui_conflict_count_resolved(
    struct ToriRS_UiRegistry const* registry,
    int node_index,
    uint32_t facet)
{
    struct ToriRS_UiRegistryNode const* node = &registry->_nodes[node_index];
    int count = 0;

    if( (node->resolved.conflict_facets & facet) == 0 )
        return 0;
    for( int at = node->first_contribution; at >= 0;
         at = registry->_contributions[at].next_for_node )
        if( (registry->_contributions[at].conflict_facets & facet) != 0 )
            count++;
    return count;
}

int
ToriRS_UiRegistry_ConflictCount(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef ref,
    uint32_t facet)
{
    assert(registry);
    if( ref.value == 0 || ref.value > (uint32_t)registry->_node_count ||
        (facet != TORIRS_UI_FACET_BOUNDS && facet != TORIRS_UI_FACET_APPEARANCE &&
         facet != TORIRS_UI_FACET_ACTIONS) )
        return 0;
    ui_resolve_node(registry, (int)ref.value - 1);
    return ui_conflict_count_resolved(registry, (int)ref.value - 1, facet);
}

bool
ToriRS_UiRegistry_ConflictPluginAt(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef ref,
    uint32_t facet,
    int index,
    char* out_plugin,
    int out_size)
{
    struct ToriRS_UiRegistryNode const* node;
    char previous[TORIRS_PLUGIN_NAME_MAX] = { 0 };
    int count;

    assert(registry);
    assert(out_plugin);
    if( out_size <= 0 )
        return false;
    out_plugin[0] = '\0';
    if( index < 0 )
        return false;
    count = ToriRS_UiRegistry_ConflictCount(registry, ref, facet);
    if( index >= count )
        return false;
    node = &registry->_nodes[ref.value - 1u];

    /* Select the next lexical name on each pass.  Conflicts are tiny and
     * diagnostic-only; the hot Resolve path remains allocation- and sort-free. */
    for( int wanted = 0; wanted <= index; wanted++ )
    {
        char next[TORIRS_PLUGIN_NAME_MAX] = { 0 };
        for( int at = node->first_contribution; at >= 0;
             at = registry->_contributions[at].next_for_node )
        {
            struct ToriRS_UiRegistryContribution const* contribution =
                &registry->_contributions[at];
            if( (contribution->conflict_facets & facet) == 0 )
                continue;
            if( previous[0] && strcmp(contribution->plugin, previous) <= 0 )
                continue;
            if( !next[0] || strcmp(contribution->plugin, next) < 0 )
                (void)snprintf(next, sizeof(next), "%s", contribution->plugin);
        }
        if( !next[0] )
            return false;
        (void)snprintf(previous, sizeof(previous), "%s", next);
    }
    {
        int const written = snprintf(out_plugin, (size_t)out_size, "%s", previous);
        return written >= 0 && written < out_size;
    }
}

static bool
ui_unlink_contribution(
    struct ToriRS_UiRegistry* registry,
    int slot)
{
    struct ToriRS_UiRegistryContribution* contribution;
    int* link;

    assert(registry);
    assert(slot >= 0);
    assert(slot < TORIRS_UI_REGISTRY_CONTRIBUTIONS_MAX);
    contribution = &registry->_contributions[slot];
    if( !contribution->used )
        return false;
    link = &registry->_nodes[contribution->node].first_contribution;
    while( *link >= 0 && *link != slot )
        link = &registry->_contributions[*link].next_for_node;
    assert(*link == slot);
    *link = contribution->next_for_node;
    ui_queue_change(registry, contribution->node, contribution->facets);
    memset(contribution, 0, sizeof(*contribution));
    contribution->next_for_node = -1;
    registry->_contribution_count--;
    return true;
}

int
ToriRS_UiRegistry_RemovePlugin(
    struct ToriRS_UiRegistry* registry,
    char const* plugin)
{
    int contributions_removed = 0;
    int base_removed;

    assert(registry);
    assert(plugin);
    for( int i = 0; i < TORIRS_UI_REGISTRY_CONTRIBUTIONS_MAX; i++ )
        if( registry->_contributions[i].used &&
            strcmp(registry->_contributions[i].plugin, plugin) == 0 &&
            ui_unlink_contribution(registry, i) )
            contributions_removed++;
    base_removed = ToriRS_UiRegistry_RemoveBaseProvider(registry, plugin);
    /* RemoveBaseProvider already advanced the one teardown revision when it
     * found anything.  A contribution-only teardown still needs that commit. */
    if( contributions_removed > 0 && base_removed == 0 )
        registry->_revision = ui_revision_next(registry->_revision);
    return contributions_removed + base_removed;
}

int
ToriRS_UiRegistry_ChangeCount(struct ToriRS_UiRegistry const* registry)
{
    assert(registry);
    return registry->_change_count;
}

bool
ToriRS_UiRegistry_ChangeNext(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiChange* out)
{
    struct ToriRS_UiRegistryNode* node;
    uint16_t entry;

    assert(registry);
    assert(out);
    if( registry->_change_count == 0 )
        return false;

    entry = registry->_change_queue[registry->_change_head];
    registry->_change_head = (registry->_change_head + 1) % TORIRS_UI_REGISTRY_NODES_MAX;
    registry->_change_count--;
    assert(entry > 0 && entry <= registry->_node_count);
    node = &registry->_nodes[entry - 1];
    assert(node->change_queued);
    assert(node->changed_facets != 0);
    out->node.value = entry;
    out->facets = node->changed_facets;
    out->revision = registry->_revision;
    node->change_queued = false;
    node->changed_facets = 0;
    return true;
}
