#ifndef INTERFACE_H
#define INTERFACE_H

#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/rscache/tables_dat/config_component.h"

#include <stdbool.h>
#include <stdint.h>

struct GInput;

/** Sidebar overlay rect — matches revconfig sidebar roots (e.g. rev_245_2_ui.ini) & Client.ts. */
#define IFACE_SIDEBAR_X0 553
#define IFACE_SIDEBAR_Y0 205
#define IFACE_SIDEBAR_X1 743
#define IFACE_SIDEBAR_Y1 466

static inline bool
iface_point_in_sidebar_overlay(int mx, int my)
{
    return mx > IFACE_SIDEBAR_X0 && my > IFACE_SIDEBAR_Y0 && mx < IFACE_SIDEBAR_X1 &&
           my < IFACE_SIDEBAR_Y1;
}

/** Main viewport, sidebar, or chat regions where inventory minimenu options exist (Client.ts). */
static inline bool
interface_point_in_ui_inv_primary_region(int mx, int my)
{
    if( iface_point_in_sidebar_overlay(mx, my) )
        return true;
    if( mx > 4 && my > 4 && mx < 516 && my < 338 )
        return true;
    if( mx >= 4 && my >= 357 && mx < 516 && my < 503 )
        return true;
    return false;
}

void
interface_draw_component(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int scroll_y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_layer(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int scroll_y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_rect(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_text(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_graphic(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_inv(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_model(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_scrollbar(
    struct GGame* game,
    int x,
    int y,
    int scroll_pos,
    int scroll_height,
    int height,
    int* pixel_buffer,
    int stride);

// Find topmost component id that has (overlayer >= 0 || overColour != 0) and contains (mouse_x, mouse_y).
// Used by interface_update_region_hover_ids per Client.ts addComponentOptions.
int
interface_find_hovered_interface_id(
    struct GGame* game,
    struct GameCacheComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y);

// Find scrollable layer whose scrollbar contains (mouse_x, mouse_y). Returns component id or -1.
// Fills out_scrollbar_y, out_height, out_scroll_height for the hit layer when found.
int
interface_find_scrollbar_at(
    struct GGame* game,
    struct GameCacheComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y,
    int* out_scrollbar_y,
    int* out_height,
    int* out_scroll_height);

// Up/down arrow: scroll by step pixels. step = rate * game_cycles when holding.
void
interface_handle_scrollbar_arrow_step(
    struct GGame* game,
    int component_id,
    int max_scroll,
    int up_not_down,
    int step);

// Single click: scroll by SCROLLBAR_ARROW_DELTA (4 px).
void
interface_handle_scrollbar_arrow(
    struct GGame* game,
    int component_id,
    int max_scroll,
    int up_not_down);

void
interface_handle_scrollbar_click(
    struct GGame* game,
    int component_id,
    int scrollbar_y,
    int height,
    int scroll_height,
    int click_y);

/* Continuous grip/track drag: recomputes scrollPos from the current mouse_y position,
 * centering the grip on the cursor.  Mirrors Client.ts doScrollbar track branch (10543-10558).
 * layer_y = absolute Y of the layer in screen space, height = layer height, scroll_height =
 * total scrollable height (> height).  Call every frame while LMB is held over region 2/3. */
void
interface_handle_scrollbar_drag(
    struct GGame* game,
    int component_id,
    int layer_y,
    int height,
    int scroll_height,
    int mouse_y);

// Run component script and return result. Uses ClientScriptVM for opcodes 5,7,13,14.
int
interface_get_if_var(
    struct GGame* game,
    struct GameCacheComponent* component,
    int script_id);

// Return whether component passes script comparator check (Client.ts getIfActive).
bool
interface_get_if_active(
    struct GGame* game,
    struct GameCacheComponent* component);

/** Expand %1..%5 using component client scripts (Client.ts drawInterface + inf). */
void
interface_expand_if_text_placeholders(
    struct GGame* game,
    struct GameCacheComponent* component,
    const char* src,
    char* out,
    int out_cap);

/** Once per frame: set iface over_* ids from Client.ts region rectangles + interface_find_hovered. */
void
interface_update_region_hover_ids(struct GGame* game);

struct UITreeOptionSet;

/** Sidebar cache inv minimenu when UITree does not supply rows: Client.ts addComponentOptions uses
 * sideModalId after IF_OPENSIDE, else sideOverlayId[sideTab] from IF_SETTAB — here
 * sidebar_interface_id / tab_interface_id[selected_tab]. Hit-test: iface_point_in_sidebar_overlay. */
bool
interface_sidebar_overlay_try_fill_uitree_options(
    struct GGame* game,
    int pick_mx,
    int pick_my,
    struct UITreeOptionSet* os,
    int* out_inv_component_id,
    int* out_inv_slot);

/** Top option line for UI (non-world) hover tooltip from hovered component cache fields.
 * Returns 1 if `out` was filled, 0 if no tooltip. */
int
interface_hover_tooltip_line(struct GGame* game, char* out, int out_cap);

/** True if component_id matches any region overlay hover id (Client.ts TEXT/RECT hovered). */
bool
interface_component_is_overlay_hovered(struct GGame* game, int component_id);

// Get default (left-click) action for an inventory slot from menu logic (Client.ts order + sort)
int
interface_get_inv_default_action(
    struct GGame* game,
    struct GameCacheComponent* component,
    int obj_id,
    int slot);

enum InterfaceButtonAction
{
    IF_BUTTON_ACTION_IF_BUTTON = 0,
    IF_BUTTON_ACTION_CLOSE_MODAL = 1,
    IF_BUTTON_ACTION_RESUME_PAUSEBUTTON = 2,
};

/** Client.ts doAction: when TOGGLE/SELECT button clicked, optimistically update varp
 * so getIfActive reflects new state before server responds. */
void
interface_apply_button_click_varp_optimistic(
    struct GGame* game,
    int component_id);

// Find clickable button at (mouse_x, mouse_y). Returns 1 if found, 0 else.
// Matches buttonType (OK/TOGGLE/SELECT/CLOSE/CONTINUE) or clientCode > 0.
// Sets *out_component_id, *out_client_code, *out_button_action, and menu params.
int
interface_find_button_click_at(
    struct GGame* game,
    struct GameCacheComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y,
    int* out_component_id,
    int* out_client_code,
    int* out_button_action,
    int* out_menu_param_a,
    int* out_menu_param_b,
    int* out_menu_param_c);

// Check if a mouse click hits an inventory item, returns slot number or -1
int
interface_check_inv_click(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int mouse_x,
    int mouse_y);

/** Client.ts drawInterface: dx/dy from grab with ±5 deadzone; inv_drag_cycles < 5 → (0,0). */
void
interface_inv_drag_delta(struct GGame* game, int* out_dx, int* out_dy);

/** Client.ts objDragArea: LMB down on swappable inv default row → drag; release completes. */
void
interface_inv_try_drag_mouse_down(struct GGame* game, int mx, int my);

void
interface_inv_drag_tick(struct GGame* game);

void
interface_inv_try_drag_mouse_up(struct GGame* game, struct GInput* input);

/** After selecting magic sidebar tab (tab id 6): if `TORI_MAGIC_TAB_IF_BUTTON_COMP` is a
 * positive integer, send PKTOUT IF_BUTTON with that component id so server scripts can run
 * `inv_transmit` (or equivalent) for spellbook TYPE_INV panels. World-specific. */
void
interface_magic_tab_request_inv_transmit_if_configured(struct GGame* game);

#endif
