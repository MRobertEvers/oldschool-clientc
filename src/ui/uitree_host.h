#ifndef SRC_UITREE_HOST_H
#define SRC_UITREE_HOST_H

#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

struct UIMinimenu;

enum UITreeHostRequestKind
{
    UITREE_HOST_IS_ACTIVE = 0,
    UITREE_HOST_APPLY_BUTTON_CLICK,
    UITREE_HOST_EVAL_TEXT_PLACEHOLDER,
    UITREE_HOST_GET_SELECTED_TAB,
    UITREE_HOST_SET_SELECTED_TAB,
    UITREE_HOST_GET_CAMERA_YAW,
    UITREE_HOST_GET_CROSS_ACTIVE,
    /** Returns current cross animation frame 0..7 (see UICross_AtlasFrame). */
    UITREE_HOST_GET_CROSS_ATLAS_FRAME,
    /** Writes the cross center (click point) to u.get_cross_position outs. */
    UITREE_HOST_GET_CROSS_POSITION,
    UITREE_HOST_GET_MINIMENU_VISIBLE,
    /** Writes the live minimenu model pointer to u.get_minimenu_state.out. */
    UITREE_HOST_GET_MINIMENU_STATE,
    /** Returns pixel width of u.measure_text.text in font_id, or 0. */
    UITREE_HOST_MEASURE_TEXT,
    UITREE_HOST_SCENE_SPRITE_HAS,
    UITREE_HOST_SCENE_FONT_HAS,
    UITREE_HOST_SCENE_MODEL_HAS,
    UITREE_HOST_GET_INV_SOURCE_SLOT,
    UITREE_HOST_SET_INV_SOURCE_SLOT,
    /** Returns scene_id of scrollbar sprite pack (frames 0=up/left, 1=down/right), or -1. */
    UITREE_HOST_GET_SCROLLBAR_SCENE,
    /**
     * Returns scene_id of a client-hardcoded sprite (u.static_sprite.slot is a
     * StaticSpriteSlot; ui stays leaf so it travels as an int), or -1.
     */
    UITREE_HOST_GET_STATIC_SPRITE_SCENE,
};

/*
 * Local copies of the engine StaticSpriteSlot values this module asks for, so
 * ui/ stays leaf. engine/static_sprites.c static-asserts they stay in sync.
 */
enum UITreeStaticSpriteSlot
{
    UITREE_STATIC_SPRITE_COMPASS = 0,
    UITREE_STATIC_SPRITE_CROSS = 10,
};

struct UIInvSlotData
{
    int obj_id;
    int obj_count;
    int scene_id;
    int atlas_index;
};

struct UITreeHostRequest
{
    enum UITreeHostRequestKind kind;
    union
    {
        struct
        {
            struct UITreeComponent const* component;
        } is_active;
        struct
        {
            struct UITreeComponent const* component;
        } apply_button_click;
        struct
        {
            struct UITreeComponent const* component;
            int script_idx;
        } eval_text_placeholder;
        struct
        {
            int tabno;
        } set_selected_tab;
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
        struct
        {
            int slot;
        } static_sprite;
        struct
        {
            int* out_x;
            int* out_y;
        } get_cross_position;
        struct
        {
            struct UIMinimenu const** out;
        } get_minimenu_state;
        struct
        {
            int font_id;
            char const* text;
        } measure_text;
    } u;
};

struct UITreeHost
{
    void* user;
    int (*request)(void* user, struct UITreeHostRequest* req);
};

void
UITree_HostInit(struct UITreeHost* host);

int
UITree_Host(struct UITreeHost const* host, struct UITreeHostRequest* req);

bool
UITree_ComponentVisibleHost(
    struct UITreeComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host);

bool
UITree_ComponentHitTestVisibleHost(
    struct UITreeComponent const* component,
    int hovered_component_id,
    struct UITreeHost const* host);

bool
UITree_ComponentIsActiveHost(
    struct UITreeHost const* host,
    struct UITreeComponent const* component);

bool
UITree_ComponentShouldEmit(
    struct UITreeComponent const* component,
    struct UITreeHost const* host);

int
UITree_ComponentSpriteRotation(
    struct UITreeComponent const* component,
    struct UITreeHost const* host);

#endif
