#ifndef SRC_UITREE_BUILD_H
#define SRC_UITREE_BUILD_H

#include "uitree.h"

#include <stdint.h>

/** Flat POD view of a cache component for building — no engine headers. */
enum UIBuildComponentType
{
    UIBUILD_LAYER = 0,
    UIBUILD_INV,
    UIBUILD_RECT,
    UIBUILD_TEXT,
    UIBUILD_GRAPHIC,
    UIBUILD_MODEL,
    UIBUILD_INV_TEXT,
    UIBUILD_LINE,
};

struct UIBuildComponent
{
    int id;
    enum UIBuildComponentType type;
    int parent_id;
    int base_x;
    int base_y;
    int base_width;
    int base_height;
    int scroll_height;
    int scroll_width;
    uint8_t if3;
    int8_t x_mode;
    int8_t y_mode;
    int8_t width_mode;
    int8_t height_mode;
    int aspect_w;
    int aspect_h;
    int graphic;
    int graphic_active;
    int outline;
    int graphic_shadow;
    uint8_t horizontal_flip;
    uint8_t vertical_flip;
    uint8_t tiled;
    uint8_t graphic_hitbox_only;
    int transparency;
    int color;
    int filled;
    int font_id;
    int text_h_align;
    int text_v_align;
    int text_line_height;
    int shadowed;
    char const* text;
    char const* text_active;
    int model_id;
    int model_zoom;
    int model_xan;
    int model_yan;
    int model_zan;
    int model_x_offset;
    int model_y_offset;
    uint8_t model_orthog;
    uint8_t model_fixed_zoom;
    int inv_cols;
    int inv_rows;
    int margin_x;
    int margin_y;
    int inv_slot_offset_x[UI_INV_SLOT_OFFSET_MAX];
    int inv_slot_offset_y[UI_INV_SLOT_OFFSET_MAX];
    int inv_slot_graphic_id[UI_INV_SLOT_OFFSET_MAX];
    int line_width;
    uint8_t line_horizontal;
    uint8_t hide;
    int button_type;
    int client_code;
    int32_t click_mask;
    int over_layer_id;
    int over_color;
    int active_color;
    int active_over_color;
    uint8_t drag_dead_zone;
    uint8_t drag_dead_time;
};

struct UITreeBuildSource
{
    int count;
    struct UIBuildComponent const* (*get_component)(void* ud, int index);
    int (*get_parent_id)(void* ud, int index);
    int (*resolve_sprite)(void* ud, int graphic_id);
    int (*resolve_font)(void* ud, int font_id);
    void* ud;
};

void
UITree_FillPositionFromBuild(
    struct UITreeElemPosition* pos,
    struct UIBuildComponent const* comp);

int32_t
UITree_PushBuildComponent(
    struct UITree* tree,
    int32_t parent_index,
    struct UIBuildComponent const* comp,
    int (*resolve_sprite)(void*, int),
    int (*resolve_font)(void*, int),
    void* resolve_ud);

int
UITree_BuildFromSource(
    struct UITree* tree,
    struct UITreeBuildSource const* src);

#endif
