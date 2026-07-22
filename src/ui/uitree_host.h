#ifndef SRC_UITREE_HOST_H
#define SRC_UITREE_HOST_H

#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

struct UIMinimenu;
struct UIHoverText;
struct UIChatView;

/** Minimap overlay dot (reference minimapDrawDot output), host-computed and
 * already rotated: sprite top-left goes at (box_center_x + dx,
 * box_center_y + dy), drawn w*h. scene_id <= 0 draws a filled rect of
 * `color` instead (the local-player white square). */
struct UITreeMinimapDot
{
    int dx;
    int dy;
    int w;
    int h;
    int scene_id;
    int atlas_index;
    uint32_t color;
};

/** One screen-space primitive of the entity overlay pass (reference
 * drawEntities' health bars + hitmarks, Client.ts:4897-4932). The host
 * projects the entity, applies the reference's per-slot nudges and hands the
 * draw layer flat, already-positioned primitives — ui/ stays leaf and knows
 * nothing about entities or the camera. */
enum UITreeEntityOverlayKind
{
    UITREE_ENTITY_OVERLAY_RECT = 0,
    UITREE_ENTITY_OVERLAY_SPRITE,
    UITREE_ENTITY_OVERLAY_TEXT,
};

#define UITREE_ENTITY_OVERLAY_TEXT_LEN 8

struct UITreeEntityOverlay
{
    int kind;
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
    int scene_id;
    int atlas_index;
    int font_id;
    /** TEXT: centred on x, baseline at y (reference centreString). */
    char text[UITREE_ENTITY_OVERLAY_TEXT_LEN];
};

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
    /** Writes the live mouseover-text model to u.get_hovertext_state.out. */
    UITREE_HOST_GET_HOVERTEXT_STATE,
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
    /**
     * Returns scene_id of the baked world map sprite (-1 while no world is
     * loaded) and writes the camera's pivot inside it to u.get_minimap_state.
     */
    UITREE_HOST_GET_MINIMAP_STATE,
    /**
     * Writes a pointer to the host-computed minimap overlay dots (valid only
     * for the current frame) to u.get_minimap_dots.out_dots; returns the
     * count (0 = no overlay).
     */
    UITREE_HOST_GET_MINIMAP_DOTS,
    /**
     * Writes the host-owned entity overlay array (health bars + hitsplats,
     * same-frame lifetime) to u.get_entity_overlays.out_items; returns the
     * item count.
     */
    UITREE_HOST_GET_ENTITY_OVERLAYS,
    /**
     * Returns nonzero when u.tab_enabled.tabno has an interface assigned
     * (reference sideOverlayId[n] != -1) — gates tab icon draw + tab clicks.
     */
    UITREE_HOST_GET_TAB_ENABLED,
    /** Returns the current mode (0..3) of u.chat_filter.filter
     *  (enum UITreeChatButtonFilter). */
    UITREE_HOST_GET_CHAT_FILTER_MODE,
    /** Advance u.chat_filter.filter to its next mode (user click). */
    UITREE_HOST_CYCLE_CHAT_FILTER_MODE,
    /** Writes the flattened chat draw model to u.get_chat_state.out. */
    UITREE_HOST_GET_CHAT_STATE,
    /**
     * Writes an obj's display name to u.get_obj_name.out (cap bytes) and its
     * stackable flag to *out_stackable. Returns 1 when the obj is known.
     */
    UITREE_HOST_GET_OBJ_NAME,
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
            int tabno;
        } tab_enabled;
        struct
        {
            int filter; /* enum UITreeChatButtonFilter */
        } chat_filter;
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
            int* out_src_anchor_x;
            int* out_src_anchor_y;
        } get_minimap_state;
        struct
        {
            struct UITreeMinimapDot const** out_dots;
        } get_minimap_dots;
        struct
        {
            struct UITreeEntityOverlay const** out_items;
            /** World viewport box the overlays must be clipped to (reference
             * draws them with Pix2D clipped to the scene viewport). */
            int* out_clip_x;
            int* out_clip_y;
            int* out_clip_w;
            int* out_clip_h;
        } get_entity_overlays;
        struct
        {
            struct UIMinimenu const** out;
        } get_minimenu_state;
        struct
        {
            struct UIHoverText const** out;
        } get_hovertext_state;
        struct
        {
            struct UIChatView const** out;
        } get_chat_state;
        struct
        {
            int obj_id;
            char* out;
            int cap;
            int* out_stackable;
        } get_obj_name;
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
