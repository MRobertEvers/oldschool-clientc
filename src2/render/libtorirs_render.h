#ifndef LIBTORIRS_RENDER_H
#define LIBTORIRS_RENDER_H

#include "toridraw/toridraw.h"

#include <stdbool.h>
#include <stdint.h>

#define LIBTORIRS_RENDER_QUEUE_MAX_SIZE 1024

enum LibToriRS_RenderCommandKind
{
    TORIRSRC_NONE = 0,

    // --- STATE ---
    TORIRSRC_BEGIN_3D,
    TORIRSRC_END_3D,
    TORIRSRC_BEGIN_2D,
    TORIRSRC_END_2D,
    TORIRSRC_CLEAR_RECT,

    // --- RESOURCES (Assets) ---
    TORIRSRC_MODEL_LOAD,
    TORIRSRC_MODEL_UNLOAD,
    TORIRSRC_ANIM_LOAD,
    TORIRSRC_ANIM_UNLOAD,
    TORIRSRC_TEX_LOAD,
    TORIRSRC_TEX_UNLOAD,
    TORIRSRC_SPRITE_LOAD,
    TORIRSRC_SPRITE_UNLOAD,
    TORIRSRC_FONT_LOAD,
    TORIRSRC_FONT_UNLOAD,

    // --- DRAWING ---
    TORIRSRC_MODEL,
    TORIRSRC_SPRITE,
    TORIRSRC_FONT,

    // --- BATCHING (3D) ---
    TORIRSRC_BATCH3D_BEGIN,
    TORIRSRC_BATCH3D_MODEL_ADD,
    TORIRSRC_BATCH3D_ANIM_ADD,
    TORIRSRC_BATCH3D_END,
    TORIRSRC_BATCH3D_CLEAR,

    // --- BATCHING (2D) ---
    TORIRSRC_TEX_BEGIN,
    TORIRSRC_TEX_END,
    TORIRSRC_SPRITE_BEGIN,
    TORIRSRC_SPRITE_END,
    TORIRSRC_FONT_BEGIN,
    TORIRSRC_FONT_END,
};

struct LibToriRS_RenderCommand_Begin3D
{
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position camera_position;
};

struct LibToriRS_RenderCommand_Model
{
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position position;
};

struct LibToriRS_RenderCommand
{
    enum LibToriRS_RenderCommandKind kind;
    union
    {
        struct LibToriRS_RenderCommand_Begin3D begin_3d;
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
LibToriRS_RenderQueue_PushCommandBegin3D(
    struct LibToriRS_RenderQueue* render_queue,
    const struct ToriDraw_ViewPort* view_port,
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_Position* camera_position);

void
LibToriRS_RenderQueue_PushCommandModelDraw(
    struct LibToriRS_RenderQueue* render_queue,
    struct ToriDraw_ModelHandle model,
    const struct ToriDraw_Position* position);

void
LibToriRS_RenderQueue_PushCommandEnd3D(struct LibToriRS_RenderQueue* render_queue);

#endif
