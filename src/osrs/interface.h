#ifndef INTERFACE_H
#define INTERFACE_H

#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/rscache/tables_dat/config_component.h"

#include <stdbool.h>
#include <stdint.h>

void
interface_draw_component(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_layer(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_rect(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_text(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_graphic(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride);

void
interface_draw_component_inv(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
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

// Find scrollable layer whose scrollbar contains (mouse_x, mouse_y). Returns component id or -1.
// Fills out_scrollbar_y, out_height, out_scroll_height for the hit layer when found.
int
interface_find_scrollbar_at(
    struct GGame* game,
    struct CacheDatConfigComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y,
    int* out_scrollbar_y,
    int* out_height,
    int* out_scroll_height);

void
interface_handle_scrollbar_click(
    struct GGame* game,
    int component_id,
    int scrollbar_y,
    int height,
    int scroll_height,
    int click_y);

// Get default (left-click) action for an inventory slot from menu logic (Client.ts order + sort)
int
interface_get_inv_default_action(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int obj_id,
    int slot);

// Check if a mouse click hits an inventory item, returns slot number or -1
int
interface_check_inv_click(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int mouse_x,
    int mouse_y);

// Handle inventory button click (sends packet to server)
void
interface_handle_inv_button(
    struct GGame* game,
    int action,
    int obj_id,
    int slot,
    int component_id);

#endif
