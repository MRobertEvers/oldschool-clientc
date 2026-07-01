#ifndef UITREE_HOST_H
#define UITREE_HOST_H

#include "uitree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ToriDraw_Scene;

/**
 * Callback table for retained UI tree: the only way the UI module reaches VM,
 * game state, and scene assets. Pixel data stays in ToriDraw_Scene.
 */
struct UITreeHost
{
    void* user;

    /* VM / CS1 */
    bool (*is_active)(void* user, struct StaticUIComponent const* component);
    void (*apply_button_click)(void* user, struct StaticUIComponent const* component);
    int (*eval_text_placeholder)(
        void* user,
        struct StaticUIComponent const* component,
        int script_idx);

    /* Game state */
    int (*get_selected_tab)(void* user);
    void (*set_selected_tab)(void* user, int tabno);
    int (*get_camera_yaw)(void* user);
    void (*get_minimap_anchor)(
        void* user,
        int* out_src_anchor_x,
        int* out_src_anchor_y);
    int (*get_world_map_size)(void* user, int* out_w, int* out_h);

    bool (*get_cross_active)(void* user);
    void (*get_cross_position)(void* user, int* out_x, int* out_y);
    int (*get_cross_atlas_frame)(void* user);
    bool (*get_minimenu_visible)(void* user);
    void (*get_minimenu_layout)(
        void* user,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);
    int (*get_minimenu_hovered_option)(void* user);

    /* Scene assets (existence only; lookup by id) */
    bool (*scene_sprite_has)(void* user, int scene_id);
    bool (*scene_font_has)(void* user, int font_id);
    bool (*scene_model_has)(void* user, int model_id);

    /* Inventory slot for RS inv grids */
    int (*get_inv_slot_obj_id)(void* user, int inv_index, int slot);
    int (*get_inv_slot_scene_id)(void* user, int inv_index, int slot);
    int (*get_inv_slot_atlas_index)(void* user, int inv_index, int slot);
};

void
uitree_host_init(struct UITreeHost* host);

bool
uitree_component_visible_host(
    struct StaticUIComponent const* component,
    int hovered_component_id,
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
    int hovered_component_id,
    struct UITreeHost const* host,
    int base_color);

bool
uitree_component_is_active_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component);

int
uitree_component_text_color_host(
    struct StaticUIComponent const* component,
    int hovered_component_id,
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
