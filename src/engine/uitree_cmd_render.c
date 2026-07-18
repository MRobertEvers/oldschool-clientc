#include "uitree_cmd_render.h"

#include "ui/uitree_emit.h"

#include "bmp.h"
#include "toridraw.h"
#include "toridraw_2d.h"
#include "toridraw_font.h"
#include "toridraw_model_sprite.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UITREE_CMD_BG 0xFF202428

static struct ToriDraw_ViewPort
viewport_from_clip(
    struct UITreeEmitClip const* clip,
    int stride)
{
    struct ToriDraw_ViewPort vp;
    memset(&vp, 0, sizeof(vp));
    vp.stride = stride;
    vp.clip_left = clip->x;
    vp.clip_top = clip->y;
    vp.clip_right = clip->x + clip->w;
    vp.clip_bottom = clip->y + clip->h;
    return vp;
}

static void
render_sprite(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmd,
    int* pixels,
    int stride)
{
    struct ToriDraw_Sprite** sprites;
    int count = 0;
    struct ToriDraw_Sprite* spr;
    struct ToriDraw_ViewPort vp;
    int atlas;

    if( cmd->scene_id <= 0 )
        return;
    sprites = ToriDraw_SceneSpriteGet(scene, cmd->scene_id, &count);
    if( !sprites || count <= 0 )
        return;
    atlas = cmd->atlas_index;
    if( atlas < 0 || atlas >= count )
        atlas = 0;
    spr = sprites[atlas];
    if( !spr || !spr->pixels_argb )
        return;

    vp = viewport_from_clip(&cmd->clip, stride);
    if( cmd->w > 0 && cmd->h > 0 )
        ToriDraw2D_BlitArgbScaled(
            &vp,
            cmd->x,
            cmd->y,
            cmd->w,
            cmd->h,
            spr->pixels_argb,
            spr->width,
            spr->height,
            pixels);
    else
        ToriDraw2D_BlitArgb(
            &vp, cmd->x, cmd->y, spr->pixels_argb, spr->width, spr->height, pixels);
}

static void
render_text(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmd,
    int* pixels,
    int stride)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;
    int x_align;

    if( cmd->font_id < 0 || !cmd->text )
        return;
    font = ToriDraw_SceneFontGet(scene, cmd->font_id);
    if( !font )
        return;

    vp = viewport_from_clip(&cmd->clip, stride);
    x_align = cmd->text_center ? 1 : 0;
    (void)ToriDraw2D_DrawStringBox(
        font,
        &vp,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->text,
        cmd->color,
        x_align,
        0,
        cmd->text_line_height,
        cmd->text_shadowed != 0,
        pixels);
}

static void
render_rect(
    struct UITreeEmitDesc const* cmd,
    int* pixels,
    int stride)
{
    struct ToriDraw_ViewPort vp = viewport_from_clip(&cmd->clip, stride);
    int x0 = cmd->x;
    int y0 = cmd->y;
    int x1 = cmd->x + cmd->w;
    int y1 = cmd->y + cmd->h;
    if( cmd->filled )
        ToriDraw2D_FillRect(&vp, x0, y0, x1, y1, cmd->color, pixels);
    else
        ToriDraw2D_DrawRectOutline(&vp, x0, y0, x1, y1, cmd->color, pixels);
}

static void
render_line(
    struct UITreeEmitDesc const* cmd,
    int* pixels,
    int stride)
{
    struct ToriDraw_ViewPort vp = viewport_from_clip(&cmd->clip, stride);
    ToriDraw2D_DrawLine(
        &vp, cmd->x, cmd->y, cmd->x + cmd->w, cmd->y + cmd->h, 1, cmd->color, pixels);
}

static void
render_model(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmd,
    int* pixels,
    int stride,
    int canvas_w,
    int canvas_h)
{
    struct ToriDraw_ModelHandle hnd;
    int draw_x = 0;
    int draw_y = 0;
    int out_w = 0;
    int out_h = 0;

    if( cmd->model_id < 0 )
        return;
    hnd = ToriDraw_SceneModelGet(scene, cmd->model_id);
    if( hnd.kind == TORIDRAWMK_NONE )
        return;

    (void)ToriDraw_RenderModelExtentsAtWidget(
        scene,
        hnd,
        cmd->model_zoom > 0 ? cmd->model_zoom : 100,
        cmd->model_xan,
        cmd->model_yan,
        0,
        0,
        0,
        cmd->y + (cmd->h > 0 ? cmd->h / 2 : 0),
        false,
        false,
        (toripixel_t*)pixels,
        stride,
        canvas_w,
        canvas_h,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->clip.x,
        cmd->clip.y,
        cmd->clip.x + cmd->clip.w,
        cmd->clip.y + cmd->clip.h,
        &draw_x,
        &draw_y,
        &out_w,
        &out_h);
}

static void
render_cmd(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmd,
    int* pixels,
    int stride,
    int canvas_w,
    int canvas_h)
{
    switch( cmd->kind )
    {
    case UITREE_EMIT_SPRITE:
        render_sprite(scene, cmd, pixels, stride);
        break;
    case UITREE_EMIT_TEXT:
        render_text(scene, cmd, pixels, stride);
        break;
    case UITREE_EMIT_RECT:
        render_rect(cmd, pixels, stride);
        break;
    case UITREE_EMIT_LINE:
        render_line(cmd, pixels, stride);
        break;
    case UITREE_EMIT_MODEL:
        render_model(scene, cmd, pixels, stride, canvas_w, canvas_h);
        break;
    case UITREE_EMIT_CC_OBJ:
    case UITREE_EMIT_INV_SLOT:
        if( cmd->scene_id > 0 )
            render_sprite(scene, cmd, pixels, stride);
        break;
    default:
        break;
    }
}

int
UITreeCmd_WriteBmp(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    char const* path,
    int width,
    int height)
{
    int* pixels;
    int i;
    size_t n;

    assert(scene);
    assert(path);
    assert(width > 0 && height > 0);

    n = (size_t)width * (size_t)height;
    pixels = calloc(n, sizeof(int));
    if( !pixels )
        return -1;

    for( i = 0; i < (int)n; i++ )
        pixels[i] = UITREE_CMD_BG;

    for( i = 0; i < cmd_count; i++ )
        render_cmd(scene, &cmds[i], pixels, width, width, height);

    bmp_write_file(path, pixels, width, height);
    free(pixels);
    return 0;
}
