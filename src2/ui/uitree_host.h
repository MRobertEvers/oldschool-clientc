#ifndef UITREE_HOST_H
#define UITREE_HOST_H

#include "ui_inv_data_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ToriDraw_Scene;

enum UITreeHostRequestKind
{
    UITREE_HOST_IS_ACTIVE,
    UITREE_HOST_APPLY_BUTTON_CLICK,
    UITREE_HOST_EVAL_TEXT_PLACEHOLDER,
    UITREE_HOST_GET_SELECTED_TAB,
    UITREE_HOST_SET_SELECTED_TAB,
    UITREE_HOST_GET_CAMERA_YAW,
    UITREE_HOST_GET_MINIMAP_ANCHOR,
    UITREE_HOST_GET_WORLD_MAP_SIZE,
    UITREE_HOST_GET_CROSS_ACTIVE,
    UITREE_HOST_GET_CROSS_POSITION,
    UITREE_HOST_GET_CROSS_ATLAS_FRAME,
    UITREE_HOST_GET_MINIMENU_VISIBLE,
    UITREE_HOST_GET_MINIMENU_LAYOUT,
    UITREE_HOST_GET_MINIMENU_HOVERED_OPTION,
    UITREE_HOST_SCENE_SPRITE_HAS,
    UITREE_HOST_SCENE_FONT_HAS,
    UITREE_HOST_SCENE_MODEL_HAS,
    UITREE_HOST_GET_INV_SOURCE_SLOT,
    UITREE_HOST_SET_INV_SOURCE_SLOT,
};

struct UITreeHostRequest
{
    enum UITreeHostRequestKind kind;
    union
    {
        struct
        {
            struct StaticUIComponent const* component;
        } is_active;
        struct
        {
            struct StaticUIComponent const* component;
        } apply_button_click;
        struct
        {
            struct StaticUIComponent const* component;
            int script_idx;
        } eval_text_placeholder;
        struct
        {
            int tabno;
        } set_selected_tab;
        struct
        {
            int* out_x;
            int* out_y;
        } get_minimap_anchor;
        struct
        {
            int* out_w;
            int* out_h;
        } get_world_map_size;
        struct
        {
            int* out_x;
            int* out_y;
        } get_cross_position;
        struct
        {
            int* out_x;
            int* out_y;
            int* out_w;
            int* out_h;
        } get_minimenu_layout;
        struct
        {
            int scene_id;
        } scene_sprite_has;
        struct
        {
            int font_id;
        } scene_font_has;
        struct
        {
            int model_id;
        } scene_model_has;
        struct
        {
            int source_id;
            int slot;
            struct UIInvSlotData* out;
        } get_inv_source_slot;
        struct
        {
            int source_id;
            int slot;
            struct UIInvSlotData const* data;
        } set_inv_source_slot;
    } u;
};

/**
 * Host bridge for retained UI tree: the only way the UI module reaches VM,
 * game state, and scene assets. Pixel data stays in ToriDraw_Scene.
 */
struct UITreeHost
{
    void* user;
    int (*request)(void* user, struct UITreeHostRequest* req);
};

void
uitree_host_init(struct UITreeHost* host);

int
uitree_host(struct UITreeHost const* host, struct UITreeHostRequest* req);

bool
uitree_component_visible_host(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host);

bool
uitree_component_hit_test_visible_host(
    struct StaticUIComponent const* component,
    int hovered_component_id,
    struct UITreeHost const* host);

bool
uitree_component_is_clickable_host(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host);

int
uitree_component_rect_color_host(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host,
    int base_color);

bool
uitree_component_is_active_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component);

int
uitree_component_text_color_host(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host,
    int base_color);

char const*
uitree_component_text_source_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component);

void
uitree_behavior_handle_click_host(
    struct UITreeHost* host,
    struct UITree const* tree,
    int32_t clicked_index);

char const*
uitree_expand_text_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component,
    char* scratch,
    size_t scratch_size);

bool
uitree_component_should_emit(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host);

int
uitree_component_sprite_rotation(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host);

#endif
