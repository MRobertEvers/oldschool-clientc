#ifndef LIBTORIRS_RENDER_H
#define LIBTORIRS_RENDER_H

#include "toripix/toridraw.h"

#include <stdbool.h>
#include <stdint.h>

#define LIBTORIRS_RENDER_QUEUE_MAX_SIZE 1024

enum LibToriRS_RenderCommandKind
{
    TORIRS_RENDER_COMMAND_NONE = 0,

    // --- STATE ---
    TORIRS_RENDER_COMMAND_BEGIN_3D,
    TORIRS_RENDER_COMMAND_END_3D,
    TORIRS_RENDER_COMMAND_BEGIN_2D,
    TORIRS_RENDER_COMMAND_END_2D,
    TORIRS_RENDER_COMMAND_CLEAR_RECT,

    // --- RESOURCES (Assets) ---
    TORIRS_RENDER_COMMAND_MODEL_LOAD,
    TORIRS_RENDER_COMMAND_MODEL_UNLOAD,
    TORIRS_RENDER_COMMAND_ANIM_LOAD,
    TORIRS_RENDER_COMMAND_ANIM_UNLOAD,
    TORIRS_RENDER_COMMAND_TEX_LOAD,
    TORIRS_RENDER_COMMAND_TEX_UNLOAD,
    TORIRS_RENDER_COMMAND_SPRITE_LOAD,
    TORIRS_RENDER_COMMAND_SPRITE_UNLOAD,
    TORIRS_RENDER_COMMAND_FONT_LOAD,
    TORIRS_RENDER_COMMAND_FONT_UNLOAD,

    // --- DRAWING ---
    TORIRS_RENDER_COMMAND_MODEL,
    TORIRS_RENDER_COMMAND_SPRITE,
    TORIRS_RENDER_COMMAND_FONT,

    // --- BATCHING (3D) ---
    TORIRS_RENDER_COMMAND_BATCH3D_BEGIN,
    TORIRS_RENDER_COMMAND_BATCH3D_MODEL_ADD,
    TORIRS_RENDER_COMMAND_BATCH3D_ANIM_ADD,
    TORIRS_RENDER_COMMAND_BATCH3D_END,
    TORIRS_RENDER_COMMAND_BATCH3D_CLEAR,

    // --- BATCHING (2D) ---
    TORIRS_RENDER_COMMAND_TEX_BEGIN,
    TORIRS_RENDER_COMMAND_TEX_END,
    TORIRS_RENDER_COMMAND_SPRITE_BEGIN,
    TORIRS_RENDER_COMMAND_SPRITE_END,
    TORIRS_RENDER_COMMAND_FONT_BEGIN,
    TORIRS_RENDER_COMMAND_FONT_END,
};

struct LibToriRS_RenderCommand_Model
{
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position position;
    struct ToriDraw_ViewPort* view_port_ref;
    struct ToriDraw_Camera* camera_ref;
};

struct LibToriRS_RenderCommand
{
    enum LibToriRS_RenderCommandKind kind;
    union
    {
        struct LibToriRS_RenderCommand_Model model;
    } u;
};

struct LibToriRS_RenderQueue
{
    struct LibToriRS_RenderCommand commands[LIBTORIRS_RENDER_QUEUE_MAX_SIZE];
    int count;
};

struct LibToriRS_RenderQueue*
LibToriRS_RenderQueue_New(void);

void
LibToriRS_RenderQueue_Free(struct LibToriRS_RenderQueue* render_queue);

void
LibToriRS_RenderQueue_Clear(struct LibToriRS_RenderQueue* render_queue);

bool
LibToriRS_RenderQueue_IsEmpty(struct LibToriRS_RenderQueue* render_queue);

void
LibToriRS_RenderQueue_PushCommandModelDraw(
    struct LibToriRS_RenderQueue* render_queue,
    struct ToriDraw_ModelHandle model,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);

#endif