#ifndef SRC_UITREE_HOST_H
#define SRC_UITREE_HOST_H

#include "uitree.h"
/* Leaf header: `static const` advance tables plus POD structs, no includes of
 * its own. It is here so the emit desc can carry a typed display-list pointer. */
#include "uitree_debug_overlay.h"

#include <stdbool.h>
#include <stdint.h>

struct UIMinimenu;
struct UIHoverText;
struct UIChatView;

/**
 * Coarse external inputs read by UITree host requests while building an emit
 * list. These are deliberately domains, not per-node subscriptions: a full
 * emit records the union of the domains it actually read, then the retention
 * gate compares only those epochs on the next frame.
 *
 * The host owns the epochs. Code which changes an authoritative input calls
 * `UITree_HostInputsChanged`; callers never need to know which nodes happened
 * to consume it. A later partial-emission implementation can reuse the same
 * vocabulary with a stamp per retained span.
 */
enum UITreeHostInputDomain
{
    /** CS1/varp/skill state, selected tabs, chat, and server widget state. */
    UITREE_HOST_INPUT_CLIENT_STATE = 0,
    /** Camera yaw/pivot and camera-derived map projection. */
    UITREE_HOST_INPUT_CAMERA,
    /** Pointer feedback: hover text, menus, crosshair, and drag offsets. */
    UITREE_HOST_INPUT_POINTER,
    /** Inventory contents, selection, and inventory-derived CS1 state. */
    UITREE_HOST_INPUT_INVENTORY,
    /** Asynchronously available sprites, fonts, models, and obj metadata. */
    UITREE_HOST_INPUT_ASSETS,
    /** World/session presentation such as minimap, multiway, and world map. */
    UITREE_HOST_INPUT_WORLD,
    /** Clock/cycle-driven presentation such as flashes and cross animation. */
    UITREE_HOST_INPUT_ANIMATION,
    /** Host-built entity, plugin, and developer overlay display lists. */
    UITREE_HOST_INPUT_OVERLAYS,
    UITREE_HOST_INPUT_DOMAIN_COUNT,
};

typedef uint32_t UITreeHostInputMask;

#define UITREE_HOST_INPUT_BIT(domain) ((UITreeHostInputMask)1u << (domain))
#define UITREE_HOST_INPUT_ALL                                                                    \
    ((UITreeHostInputMask)((1u << UITREE_HOST_INPUT_DOMAIN_COUNT) - 1u))

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
    /** Sprite-content rotation in 2048-per-turn units, pivoted at the icon
     * centre (a sailing hull's minimap icon turns with its yaw — deob
     * client.method2412). 0 = plain blit. */
    int rotate;
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
    /** A diagonal of the (x,y,w,h) box: direction 0 runs top-left to
     *  bottom-right, 1 bottom-left to top-right (TORIRSRC_LINE's contract).
     *  Any projected world segment fits by picking box + direction. */
    UITREE_ENTITY_OVERLAY_LINE,
    /* Convex polygon, as a begin / point... / end run.
     *
     * Bracketed rather than one item carrying an array so that each item is
     * still ONE render command: the emit walk produces one command per step,
     * and this keeps a variable-length primitive from needing a sub-step
     * counter threaded through the walk and every backend. `color` and `trans`
     * ride on the BEGIN; the POINTs carry only x and y. */
    UITREE_ENTITY_OVERLAY_POLY_BEGIN,
    UITREE_ENTITY_OVERLAY_POLY_POINT,
    UITREE_ENTITY_OVERLAY_POLY_END,
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
    /** SPRITE only: 0 = opaque, 255 = invisible (ToriRS_RenderCommand_Sprite's
     *  own sense). The health bar fades out with it. */
    int trans;
    /** Optional extra clip, intersected with the world viewport. A zero `w` or
     *  `h` means "no extra clip", which is what every primitive but the health
     *  bar's filled half wants -- that one is a full-width sprite drawn cut off
     *  at the current fill, exactly as the reference clips it. */
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    /** LINE only: which diagonal of the box, and its thickness (0 = 1px). */
    uint8_t line_direction;
    uint8_t line_width;
    /** TEXT: centred on x, baseline at y (reference centreString). */
    char text[UITREE_ENTITY_OVERLAY_TEXT_LEN];
};

/** One role-local canvas list, already grouped by exact target incarnation.
 * `replace` selects the pruned target's tombstone; `place` selects the exact
 * before/native-self/after boundary for a live target. */
/* Where a role overlay group paints relative to its target. These values are
 * the internal UITree side of the host's named-UI boundary contract. */
#define UITREE_ROLE_PLACE_AFTER 0
#define UITREE_ROLE_PLACE_BEFORE 1
/** The target's own appearance, when a plugin provides it: paints between the
 *  BEFORE and AFTER groups, where the native subtree would have. */
#define UITREE_ROLE_PLACE_SELF 2

struct UITreeRoleOverlayGroup
{
    int32_t node_index;
    uint32_t node_incarnation;
    uint8_t replace;
    uint8_t place;
    struct UITreeEntityOverlay const* items;
    int item_count;
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
    /**
     * The touch marker's whole state in one ask: whether it is running, where,
     * and which atlas frame. One request rather than the cross's three because
     * this one is answered from a single struct and splitting it would let the
     * position and the frame come from different ticks.
     *
     * @return non-zero when a marker is running; the outs are untouched
     * otherwise. @see ui/uitree_ink.h.
     */
    UITREE_HOST_GET_INKWELL,
    /** Scene id of the uploaded inkwell atlas, or <=0 when it has none. */
    UITREE_HOST_GET_INKWELL_SCENE,
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
     * Nonzero when the server has taken the minimap away (MINIMAP_TOGGLE).
     *
     * Deliberately not folded into GET_MINIMAP_STATE's return, which already
     * answers "-1 = no baked map yet": that is the map not being READY, this
     * is the map being WITHHELD, and a caller that cannot tell them apart
     * will eventually treat one as the other.
     */
    UITREE_HOST_GET_MINIMAP_HIDDEN,
    /**
     * Nonzero when the player is in a multi-combat zone (SET_MULTIWAY) and the
     * indicator should draw. The sprite and its place are the widget's, from
     * revconfig; only the answer to "now?" is the host's.
     */
    UITREE_HOST_GET_MULTIWAY,
    /**
     * System-update countdown (UPDATE_REBOOT_TIMER). Returns nonzero when an
     * update is pending and writes the formatted line -- a pointer with
     * frame lifetime, like the hovertext model -- to
     * u.get_reboot_timer.out_text.
     */
    UITREE_HOST_GET_REBOOT_TIMER,
    /**
     * Which pre-game screen is showing, as an RS_TitleScreen value, or -1 when
     * the client is not on the title screen at all.
     *
     * The title tree carries every screen's widgets at once and the app hides
     * the groups that are not current, so this is what a widget belonging to
     * one screen asks before drawing on another's.
     */
    UITREE_HOST_GET_TITLE_SCREEN,
    /**
     * One credential line, already composed: prefix, the value (masked if the
     * widget asked), and the caret when this field has focus and the blink is
     * in its visible half. Written to u.get_title_field.out_text with frame
     * lifetime, like the hovertext and reboot-timer strings.
     *
     * Composed by the host rather than the widget because the caret's phase is
     * the client's clock and the mask is a property of the value, but the
     * widget hands over the spelling of both -- see UITreeLoginInputConfig.
     * Returns nonzero when there is a line to draw.
     */
    UITREE_HOST_GET_TITLE_FIELD,
    /**
     * One of the three login message lines (u.get_title_message.index, 0-2),
     * written to out_text with frame lifetime. Returns nonzero when that line
     * is non-empty, so a two-line reply on a three-line layout draws two.
     */
    UITREE_HOST_GET_TITLE_MESSAGE,
    /**
     * The loading bar's state: percent 0-100 and the status line. Returns
     * nonzero while a bar should show -- there is no bar once the title screen
     * is idle, and drawing an empty one is not the same thing.
     */
    UITREE_HOST_GET_TITLE_PROGRESS,
    /**
     * A login_button was clicked; u.title_action.action is its resolved
     * RS_TitleAction. A command, not a question: it contributes nothing to
     * what the frame draws, and the host bumps its own epochs for whatever the
     * action changed.
     */
    UITREE_HOST_TITLE_ACTION,
    /**
     * Is one of the login form's checkboxes on? u.get_title_toggle.toggle is
     * the RS_TitleToggle to ask about; returns nonzero for on.
     *
     * A question, unlike TITLE_ACTION above: it decides which of the widget's
     * two sprites this frame draws. The state is the host's because it is
     * device state that outlives the tree, not a property of the node.
     */
    UITREE_HOST_GET_TITLE_TOGGLE,
    /**
     * The scene sprite one brazier's fire is currently in
     * (u.get_title_flames.side selects which), written to out_scene_id.
     *
     * The simulation is the host's -- it owns the clock the fire burns on --
     * and it hands over a sprite id rather than pixels so the widget draws it
     * exactly like any other sprite. Returns nonzero while there is a fire.
     */
    UITREE_HOST_GET_TITLE_FLAMES,
    /**
     * Writes a pointer to the host-computed minimap overlay dots (valid only
     * for the current frame) to u.get_minimap_dots.out_dots; returns the
     * count (0 = no overlay).
     */
    UITREE_HOST_GET_MINIMAP_DOTS,
    /** Begin one full/retained overlay-source refresh. Hosts use this to clear
     * per-overlay-frame side state (notably plugin click regions) independently
     * of whether a world exists and therefore a FRAME list is requested. */
    UITREE_HOST_BEGIN_OVERLAYS,
    /**
     * Writes the host-owned entity overlay array (health bars + hitsplats,
     * same-frame lifetime) to u.get_entity_overlays.out_items; returns the
     * item count.
     */
    UITREE_HOST_GET_ENTITY_OVERLAYS,
    /**
     * The PLUGIN CANVAS overlay: the same item vocabulary as the entity
     * overlays above, drawn in canvas space instead of world space. Writes the
     * host-owned array (same-frame lifetime) to u.get_entity_overlays.out_items
     * and the clip to the same outs; returns the item count.
     *
     * A second list rather than a flag on the first, because the two are
     * clipped and LAYERED differently and a plugin has to be able to ask for
     * either. An entity overlay is hoisted to just above the 3D world and cut
     * to the world viewport, which is what makes a tile marker sit under the
     * inventory the way the reference draws it; an orb beside the minimap is
     * chrome, and in a fixed gameframe the minimap is not inside the world
     * viewport at all -- a marker drawn there through the world list is
     * clipped away entirely.
     */
    UITREE_HOST_GET_CANVAS_OVERLAYS,
    /** Prepare the canvas dispatch and return its role-local groups. The host
     * writes whether role.anchor was requested even when every target missed,
     * which fences retained emit from silently promoting those draws global. */
    UITREE_HOST_GET_ROLE_OVERLAY_GROUPS,
    /** Publish the exact parent clip used for a role-local group so its hit
     * regions obey the same local clipping on the next interaction frame. */
    UITREE_HOST_SET_ROLE_OVERLAY_CLIP,
    /** The plugin FRAME overlay: a layout plugin's own chrome, cut to the
     *  canvas like GET_CANVAS_OVERLAYS and emitted in a different place --
     *  over the 3D scene, under the interfaces. @see FrameOffer.draw. */
    UITREE_HOST_GET_FRAME_OVERLAYS,
    /**
     * Writes the host-owned world map tile array (the baked regions covering
     * the map surface this frame, same-frame lifetime) to
     * u.get_worldmap_tiles.out_items; returns the item count. The host also
     * reports the surface's background colour, which shows wherever the area
     * has no region or its tiles have not loaded yet.
     */
    UITREE_HOST_GET_WORLDMAP_TILES,
    /**
     * Overview pane (clientCode 1401): writes one scaled tile (the current
     * area's compositetexture) to u.get_worldmap_overview.out_items and the
     * area background colour. Returns the item count (0 or 1).
     */
    UITREE_HOST_GET_WORLDMAP_OVERVIEW,
    /**
     * Returns nonzero when u.tab_enabled.tabno has an interface assigned
     * (reference sideOverlayId[n] != -1) — gates tab icon draw + tab clicks.
     */
    UITREE_HOST_GET_TAB_ENABLED,
    /**
     * Returns nonzero when u.tab_enabled.tabno is the tab the server asked to
     * FLASH and the blink is currently in its dark half — i.e. "hide this
     * icon on this frame".
     *
     * Separate from GET_TAB_ENABLED rather than folded into it, though the
     * reference writes the two as one expression
     * (`sideIcon[n] !== -1 && (tutFlashIcon !== n || loopCycle % 20 < 10)`).
     * They answer different questions: one is whether the tab HAS a panel, the
     * other is where a blink is in its cycle this frame, and a single "enabled"
     * that silently means both is the kind of answer that later gets reused
     * for the wrong one.
     */
    UITREE_HOST_GET_TAB_FLASH_HIDDEN,
    /** Returns the current mode (0..3) of u.chat_filter.filter
     *  (enum UITreeChatButtonFilter). */
    UITREE_HOST_GET_CHAT_FILTER_MODE,
    /** Advance u.chat_filter.filter to its next mode (user click). */
    UITREE_HOST_CYCLE_CHAT_FILTER_MODE,
    /** Writes the flattened chat draw model to u.get_chat_state.out. */
    UITREE_HOST_GET_CHAT_STATE,
    /**
     * Writes an obj's display name to u.get_obj_name.out (cap bytes), its
     * stackable flag to *out_stackable and whether it is a bank *placeholder*
     * (the obj record carries a placeholder template) to *out_placeholder.
     * Returns 1 when the obj is known.
     */
    UITREE_HOST_GET_OBJ_NAME,
    /**
     * Inventory slot press/drag ghost (reference objDrag* visual): writes the
     * cell's identity and the current mouse delta (already deadzoned
     * host-side) to u.get_inv_drag outs. Returns 1 while the slot should
     * ghost — emit renders that cell alone offset by (dx,dy) at trans 128.
     * The ghost starts at the press that armed the cell, not at the promotion:
     * the deadzone and dead time zero (dx,dy), never the fade.
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
    /**
     * Plain icon with cc_setoutline(1) black border baked in (no graphic_shadow).
     * Prefer over Soft3D draw-time outline for dense item grids.
     */
    UITREE_HOST_GET_OBJ_ICON_BORDERED,
    /**
     * Writes the debug overlay's display list to u.get_debug_overlay.out_prims
     * and returns the primitive count (0 = no overlay, which is the normal
     * case — the pass then costs one host call and nothing else). The array is
     * host-owned and lives as long as the ToriRSChrome, so unlike the other
     * host-owned arrays here it is not same-frame-only; it is still only read
     * during the frame it was fetched for.
     */
    UITREE_HOST_GET_DEBUG_OVERLAY,
    /**
     * Server IF_SETEVENTS mask for u.get_if_events.com_id, including a dynamic
     * child's inheritance of its parent's armed sub range. Hit-test uses this
     * so choice-menu rows (plain TEXT, no cache clickmask) are clickable once
     * the server arms `chatmenu:options`.
     */
    UITREE_HOST_GET_IF_EVENTS,
    UITREE_HOST_REQUEST_COUNT,
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
            int obj_id;
            int count;
        } get_obj_icon_bordered;
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
            /* In: the component's configured artwork, so the host does not
             * have to know what a profile said. -1 for any of them means
             * "unstated", and the host substitutes its default. */
            int style;
            int walk_color;
            int interact_color;
            int* out_x;
            int* out_y;
            int* out_atlas_index;
        } get_inkwell;
        struct
        {
            int* out_src_anchor_x;
            int* out_src_anchor_y;
        } get_minimap_state;
        struct
        {
            char const** out_text;
        } get_reboot_timer;
        struct
        {
            /** The widget's own config, so the host can compose the line the
             *  way this field asked: its prefix, its mask, its caret spelling
             *  and its blink period. */
            struct UITreeLoginInputConfig const* config;
            /** Nonzero out when this is the focused field, so the widget can
             *  draw focus without asking a second question. */
            int* out_focused;
            char const** out_text;
        } get_title_field;
        struct
        {
            int index;
            char const** out_text;
        } get_title_message;
        struct
        {
            int* out_percent;
            char const** out_text;
        } get_title_progress;
        struct
        {
            /** Resolved RS_TitleAction. */
            int action;
        } title_action;
        struct
        {
            /** enum RS_TitleToggle. */
            int toggle;
        } get_title_toggle;
        struct
        {
            /** enum TitleFlameSide. */
            int side;
            /* The node's own placement of the fire inside its column,
             * carried across because the host owns the simulation but the
             * profile owns where each era's flame leans. */
            int bias;
            int sway;
            int run;
            int row;
            int blur;
            int* out_scene_id;
        } get_title_flames;
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
            struct UITreeWorldMapTile const** out_items;
            int box_x;
            int box_y;
            int box_w;
            int box_h;
            int* out_background_rgb;
        } get_worldmap_overview;
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
            struct UITreeRoleOverlayGroup const** out_groups;
            int* out_anchor_seen;
        } get_role_overlay_groups;
        struct
        {
            int32_t node_index;
            uint32_t node_incarnation;
            int replace;
            int clip_x;
            int clip_y;
            int clip_w;
            int clip_h;
            int place;
        } set_role_overlay_clip;
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
            int* out_placeholder;
        } get_obj_name;
        struct
        {
            int font_id;
            char const* text;
        } measure_text;
        struct
        {
            int com_id;
        } get_if_events;
        struct
        {
            struct ToriRSChromePrim const** out_prims;
        } get_debug_overlay;
    } u;
};

struct UITreeHost
{
    void* user;
    int (*request)(void* user, struct UITreeHostRequest* req);
    /** Monotonic versions of the external input domains above. */
    uint64_t input_epoch[UITREE_HOST_INPUT_DOMAIN_COUNT];
    /** Last semantic signature published by each authoritative source.  These
     * let source owners compare before invalidating without duplicating epoch
     * bookkeeping or keeping a parallel snapshot beside every UITreeHost. */
    uint64_t input_signature[UITREE_HOST_INPUT_DOMAIN_COUNT];
    UITreeHostInputMask input_signature_valid;
    /** Optional read observer. UITree_EmitWalk sets this only on a shallow
     * copy of the host, so ordinary host calls pay one predictable null test. */
    UITreeHostInputMask* observed_input_mask;
};

/** Snapshot of the host inputs consumed by one completed emit walk. */
struct UITreeHostInputStamp
{
    struct UITreeHost const* source;
    UITreeHostInputMask dependencies;
    uint64_t epoch[UITREE_HOST_INPUT_DOMAIN_COUNT];
};

void
UITree_HostInit(struct UITreeHost* host);

int
UITree_Host(struct UITreeHost const* host, struct UITreeHostRequest* req);

/** Map a request to the external inputs which can change its answer.
 * Invalid/unrecognised kinds return UITREE_HOST_INPUT_ALL, preserving
 * correctness when a request is added before its precise classification. */
UITreeHostInputMask
UITree_HostRequestInputMask(enum UITreeHostRequestKind kind);

/** Advance the authoritative versions for the supplied input domains. */
void
UITree_HostInputsChanged(struct UITreeHost* host, UITreeHostInputMask changed);

/** Publish one domain's current semantic signature.  The first publication
 * and every changed signature advance that domain's epoch; an unchanged
 * signature is a no-op.  Returns true when the epoch advanced.
 *
 * This is a convenience for sources whose state is assembled from several
 * ordinary fields at a frame publication fence.  Event-driven sources may
 * continue to call `UITree_HostInputsChanged` directly. */
bool
UITree_HostPublishInputSignature(
    struct UITreeHost* host,
    enum UITreeHostInputDomain domain,
    uint64_t signature);

/** Capture the selected input domains for retained-output validation. */
void
UITree_HostInputStampCapture(
    struct UITreeHost const* host,
    UITreeHostInputMask dependencies,
    struct UITreeHostInputStamp* out);

/** True only while every input version recorded in `stamp` is current. */
bool
UITree_HostInputStampIsCurrent(
    struct UITreeHostInputStamp const* stamp,
    struct UITreeHost const* host);

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
