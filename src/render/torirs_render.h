#ifndef SRC_RENDER_TORIRS_RENDER_H
#define SRC_RENDER_TORIRS_RENDER_H

#include "toridraw.h"
#include "toridraw_font.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Soft3D contract: DRAW_MODEL is self-contained (project+sort+raster in the
 * consumer). Do not assume scene projection scratch survives across commands.
 * Future GPU backends may Project+Sort in the consumer or at END_3D; emitters
 * must not rely on Soft3D-only side effects for pick until a pick API exists.
 */

enum ToriRS_RenderCommandKind
{
    TORIRSRC_NONE = 0,

    /* --- STATE --- */
    TORIRSRC_BEGIN_3D,
    TORIRSRC_END_3D,
    TORIRSRC_BEGIN_2D,
    TORIRSRC_END_2D,
    TORIRSRC_CLEAR_RECT,
    TORIRSRC_FILL_RECT,

    /* --- RESOURCES (stubs for Soft3D when assets already live in scene) --- */
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

    /* --- DRAWING --- */
    TORIRSRC_DRAW_MODEL,
    TORIRSRC_DRAW_MODEL_WIDGET,
    TORIRSRC_SPRITE,
    TORIRSRC_FONT,
    TORIRSRC_LINE,

    /* --- BATCHING (3D) — reserved for GPU; Soft3D no-ops --- */
    TORIRSRC_BATCH3D_BEGIN,
    TORIRSRC_BATCH3D_MODEL_ADD,
    TORIRSRC_BATCH3D_ANIM_ADD,
    TORIRSRC_BATCH3D_END,
    TORIRSRC_BATCH3D_CLEAR,

    /* --- BATCHING (2D) --- */
    TORIRSRC_TEX_BEGIN,
    TORIRSRC_TEX_END,
    TORIRSRC_SPRITE_BEGIN,
    TORIRSRC_SPRITE_END,
    TORIRSRC_FONT_BEGIN,
    TORIRSRC_FONT_END,
};

struct ToriRS_RenderCommand_Begin3D
{
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position camera_position;
};

struct ToriRS_RenderCommand_Model
{
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position position;
    struct ToriDraw_Position world_position;
    int element_id;
    struct ToriDraw_Animation* animation;
    int anim_index;
    int anim_frame;
    bool dynamic;
};

struct ToriRS_RenderCommand_ModelWidget
{
    struct ToriDraw_ModelHandle model;
    int x;
    int y;
    int w;
    int h;
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
    int model_zoom;
    int model_xan;
    int model_yan;
    int model_zan;
    int model_x_offset;
    int model_y_offset;
    uint8_t model_orthog;
    uint8_t model_fixed_zoom;
};

struct ToriRS_RenderCommand_ModelLoad
{
    int element_id;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position world_position;
};

struct ToriRS_RenderCommand_AnimLoad
{
    int element_id;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position world_position;
};

struct ToriRS_RenderCommand_Batch
{
    int batch_id;
    int element_id;
    int pose_id;
    int anim_index;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position world_position;
};

struct ToriRS_RenderCommand_TexLoad
{
    int texture_id;
    struct ToriDraw_Texture* texture;
};

struct ToriRS_RenderCommand_SpriteLoad
{
    int element_id;
    struct ToriDraw_Sprite** sprites;
    int count;
};

struct ToriRS_RenderCommand_Sprite
{
    int scene_id;
    int atlas_index;
    int x;
    int y;
    int w;
    int h;
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
    int rotation;
    int outline;
    int graphic_shadow;
    int trans;
    uint8_t if3;
    uint8_t tiled;
    uint8_t flip_h;
    uint8_t flip_v;
};

struct ToriRS_RenderCommand_FontLoad
{
    int font_id;
    struct ToriDraw_Font* font;
};

struct ToriRS_RenderCommand_ClearRect
{
    int x;
    int y;
    int w;
    int h;
};

struct ToriRS_RenderCommand_FillRect
{
    int x;
    int y;
    int w;
    int h;
    int argb;
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
    int filled;
};

struct ToriRS_RenderCommand_Font
{
    int font_id;
    int x;
    int y;
    int w;
    int h;
    int color;
    int center;
    int y_align;
    int line_height;
    int shadowed;
    char const* text;
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
};

struct ToriRS_RenderCommand_Line
{
    int x;
    int y;
    int w;
    int h;
    int argb;
    int line_width;
    uint8_t line_direction;
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
};

struct ToriRS_RenderCommand
{
    enum ToriRS_RenderCommandKind kind;
    union
    {
        struct ToriRS_RenderCommand_Begin3D begin_3d;
        struct ToriRS_RenderCommand_Model model;
        struct ToriRS_RenderCommand_ModelWidget model_widget;
        struct ToriRS_RenderCommand_ModelLoad model_load;
        struct ToriRS_RenderCommand_AnimLoad anim_load;
        struct ToriRS_RenderCommand_Batch batch;
        struct ToriRS_RenderCommand_TexLoad tex_load;
        struct ToriRS_RenderCommand_SpriteLoad sprite_load;
        struct ToriRS_RenderCommand_FontLoad font_load;
        struct ToriRS_RenderCommand_Sprite sprite;
        struct ToriRS_RenderCommand_ClearRect clear_rect;
        struct ToriRS_RenderCommand_FillRect fill_rect;
        struct ToriRS_RenderCommand_Font font;
        struct ToriRS_RenderCommand_Line line;
    } u;
};

#endif
