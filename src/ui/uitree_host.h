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

/* Long enough for a full overhead chat line (reference chatMessage); hitsplat
 * numbers use only the first few bytes. */
#define UITREE_ENTITY_OVERLAY_TEXT_LEN 100

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

/* One blit on the world map surface: a baked map region, or a map element icon
 * over it. Both are positioned by the host in absolute screen pixels — regions
 * are baked at exactly the view's pixels-per-tile, so nothing scales here — the
 * same division of labour as UITreeEntityOverlay: the host projects, the draw
 * layer draws, and ui/ knows nothing about map coordinates. */
struct UITreeWorldMapTile
{
    int scene_id;
    int atlas_index;
    int x;
    int y;
    int w;
    int h;
    /** Stretch the sprite to w x h rather than blitting it at its own size.
     *  Region tiles set this so a zoom change can keep drawing the bake it
     *  already has, scaled, until the new-zoom bake replaces it. */
    int scaled;
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
     * Writes the host-owned world map tile array (the baked regions covering
     * the map surface this frame, same-frame lifetime) to
     * u.get_worldmap_tiles.out_items; returns the item count. The host also
     * reports the surface's background colour, which shows wherever the area
     * has no region or its tiles have not loaded yet.
     */
    UITREE_HOST_GET_WORLDMAP_TILES,
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
    /**
     * Inventory slot press/drag ghost (reference objDrag* visual): writes the
     * cell's identity and the current mouse delta (already deadzoned
     * host-side) to u.get_inv_drag outs. Returns 1 while the slot should
     * ghost — emit renders that cell alone offset by (dx,dy) at trans 128.
     * IF1/CS1 ghosts from arm time; IF3 ghosts only once the press promotes
     * past the deadzone + dead time (a plain click must not flicker).
     *
     * An item cell comes in two shapes and the identity differs between them.
     * A TYPE_INV grid cell is (inv source id, slot), because the grid is one
     * component holding many slots. A CS2 `cc_create`d cell is its own
     * component, so `out_component_id` is what names it and the source/slot
     * pair is meaningless. Both are reported; a caller matches on whichever it
     * is.
     */
    UITREE_HOST_GET_INV_DRAG,
    /**
     * Scene font id for the inventory stack-count number (reference draws it
     * with the client's p11), or -1 while the font is still loading.
     */
    UITREE_HOST_GET_INV_COUNT_FONT,
    /**
     * Selected-item white outline (reference TYPE_INV draw: outline = 0xFFFFFF
     * when useMode && objSelectedSlot == slot && objSelectedComId == child.id).
     * Given the grid's component id + slot + the slot's obj id/count, returns
     * the scene id of the white-outlined icon variant when that slot is the
     * armed "Use" selection, else 0 — emit swaps it in for the plain icon.
     */
    UITREE_HOST_GET_INV_SELECT_ICON,
    /**
     * Which cell is armed for "Use" (reference useMode / objSelectedComId /
     * objSelectedSlot). Returns 1 while a selection is live and writes the
     * (component, slot) pair naming it; returns 0 otherwise.
     *
     * The pair is what the protocol addresses a cell by — a CS2 `cc_create`d
     * cell reports its static PARENT's uid plus its index within it, never the
     * runtime child's own id — so a caller matches a node the same two ways
     * GET_INV_DRAG's caller does. The grid path does not need this: it already
     * knows the (component, slot) of every slot it draws and can ask
     * GET_INV_SELECT_ICON directly. A dynamic cell does not know its own slot's
     * addressing without walking to its parent, which is what this saves.
     */
    UITREE_HOST_GET_INV_SELECTION,
    /**
     * Plain (no baked drop-shadow) obj icon for a SETOBJECT cell that also has
     * cc_setoutline / cc_setgraphicshadow. Returns the scene sprite id, or -1
     * while the model is still loading. Inventory cells leave outline/shadow at
     * 0 and keep the SHADOW-baked item_scene_id instead.
     */
    UITREE_HOST_GET_OBJ_ICON_PLAIN,
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
            int* out_source_id;
            int* out_slot;
            int* out_dx;
            int* out_dy;
            /** Component id of the armed cell — the identity a CS2-created
             *  cell has and a grid slot does not. */
            int* out_component_id;
        } get_inv_drag;
        struct
        {
            int com_id;
            int slot;
            int obj_id;
            int count;
        } get_inv_select_icon;
        struct
        {
            int* out_component_id;
            int* out_slot;
        } get_inv_selection;
        struct
        {
            int obj_id;
            int count;
        } get_obj_icon_plain;
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
            struct UITreeWorldMapTile const** out_items;
            /** Widget box, so the host can decide which regions are visible. */
            int box_x;
            int box_y;
            int box_w;
            int box_h;
            int* out_background_rgb;
        } get_worldmap_tiles;
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
