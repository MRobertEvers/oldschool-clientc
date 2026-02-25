#ifndef STATIC_UI_BUFFER_H
#define STATIC_UI_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "game.h"
#include "graphics/dash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame-dependent values computed once per frame and passed to resolve/execute. */
struct StaticUiFrameContext
{
    int chat_y;
    int bind_x;
    int bind_y;
    int bind_bottom_x;
    int bind_bottom_y;
};

/* Position expression: either a literal integer or a named expression (index into frame context). */
#define STATIC_UI_POS_LITERAL 0
#define STATIC_UI_POS_CHAT_Y 1
#define STATIC_UI_POS_BIND_X 2
#define STATIC_UI_POS_BIND_Y 3
#define STATIC_UI_POS_BIND_BOTTOM_X 4
#define STATIC_UI_POS_BIND_BOTTOM_Y 5
#define STATIC_UI_POS_CHAT_Y_M19 6
#define STATIC_UI_POS_EXPR_MAX 7

#define STATIC_UI_DRAW_TYPE_SPRITE 0
#define STATIC_UI_DRAW_TYPE_SPRITE_ROTATED 1

#define STATIC_UI_MAX_ID_LEN 64
#define STATIC_UI_MAX_HANDLER_NAME_LEN 32

/* State handler: given game and frame context, outputs whether to draw, which sprite, (x,y), and optional rotation (for compass).
 * rotation_cw is in r2pi2048 units (0-2047); only used when draw_type is SPRITE_ROTATED. */
typedef void (*StaticUiStateHandlerFn)(
    struct GGame* game,
    const struct StaticUiFrameContext* ctx,
    int* out_visible,
    struct DashSprite** out_sprite,
    int* out_x,
    int* out_y,
    int* out_rotation_cw);

struct StaticUiElementDescriptor
{
    char id[STATIC_UI_MAX_ID_LEN];
    /* Sprite lookup: key (offset into GGame) and optional array index; -1 = single sprite. */
    size_t sprite_offset;
    int sprite_index; /* -1 for non-array */
    /* Position: type of x/y (literal vs expr), value for literal. */
    int x_type;
    int x_value;
    int y_type;
    int y_value;
    /* Rotated blit params (compass): dest size and anchor in dest. */
    int rotated_dest_w;
    int rotated_dest_h;
    int rotated_dest_anchor_x;
    int rotated_dest_anchor_y;
    char state_handler_name[STATIC_UI_MAX_HANDLER_NAME_LEN];
    StaticUiStateHandlerFn state_handler;
    int draw_type;
};

struct StaticUiElementCommand
{
    const struct StaticUiElementDescriptor* descriptor;
};

struct StaticUiElementCommandBuffer
{
    struct StaticUiElementDescriptor* descriptors;
    int descriptor_count;
    struct StaticUiElementCommand* commands;
    int count;
    int capacity;
};

/* Callbacks for execute: renderer provides these so C code can blit without including platform-specific code. */
typedef void (*StaticUiBlitSpriteFn)(
    void* userdata,
    struct GGame* game,
    struct DashSprite* sprite,
    int x,
    int y);
typedef void (*StaticUiBlitRotatedFn)(
    void* userdata,
    struct GGame* game,
    struct DashSprite* sprite,
    int x,
    int y,
    int dest_w,
    int dest_h,
    int dest_anchor_x,
    int dest_anchor_y,
    int rotation_cw);

struct StaticUiRenderCallbacks
{
    void* userdata;
    StaticUiBlitSpriteFn blit_sprite;
    StaticUiBlitRotatedFn blit_rotated;
};

/* Load from INI and order file; returns NULL on failure. Buffer and descriptors are owned by the returned context (single allocation). */
struct StaticUiElementCommandBuffer*
static_ui_buffer_load(
    const char* ini_path,
    const char* order_path);

void
static_ui_buffer_free(struct StaticUiElementCommandBuffer* buffer);

/* Execute: for each command, resolve sprite and position (via descriptor or state handler), then call blit callbacks. */
void
static_ui_buffer_execute(
    struct StaticUiElementCommandBuffer* buffer,
    struct GGame* game,
    const struct StaticUiFrameContext* frame_ctx,
    const struct StaticUiRenderCallbacks* callbacks);

/* Resolve position from descriptor using frame context. */
int
static_ui_resolve_x(
    const struct StaticUiElementDescriptor* d,
    const struct StaticUiFrameContext* ctx);
int
static_ui_resolve_y(
    const struct StaticUiElementDescriptor* d,
    const struct StaticUiFrameContext* ctx);

#ifdef __cplusplus
}
#endif

#endif
