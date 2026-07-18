#include "uitree_cmd_render.h"

#include "ui/uitree_emit.h"

#include "bmp.h"
#include "toridraw.h"
#include "toridraw_2d.h"
#include "toridraw_font.h"
#include "toridraw_light_model.h"
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

static uint32_t*
uitree_cmd_clamp_to_nominal(
    uint32_t const* src,
    int src_w,
    int src_h,
    int src_ox,
    int src_oy,
    int nominal_w,
    int nominal_h)
{
    uint32_t* dst;
    int y;
    int x;

    if( !src || nominal_w <= 0 || nominal_h <= 0 || src_w <= 0 || src_h <= 0 )
        return NULL;

    dst = calloc((size_t)nominal_w * (size_t)nominal_h, sizeof(uint32_t));
    if( !dst )
        return NULL;

    for( y = 0; y < src_h; y++ )
    {
        int dst_y = y + src_oy;
        if( dst_y < 0 || dst_y >= nominal_h )
            continue;
        for( x = 0; x < src_w; x++ )
        {
            int dst_x = x + src_ox;
            if( dst_x < 0 || dst_x >= nominal_w )
                continue;
            dst[dst_y * nominal_w + dst_x] = src[y * src_w + x];
        }
    }
    return dst;
}

static void
uitree_cmd_scale_pixel_alpha(
    uint32_t* buf,
    size_t count,
    int alpha)
{
    size_t i;

    if( !buf || alpha >= 255 )
        return;
    if( alpha < 0 )
        alpha = 0;

    for( i = 0; i < count; i++ )
    {
        uint32_t p = buf[i];
        int a = (int)((p >> 24) & 0xFF);
        a = (a * alpha) / 255;
        buf[i] = (p & 0x00FFFFFFu) | ((uint32_t)a << 24);
    }
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
    int nominal_w;
    int nominal_h;
    int sw;
    int sh;
    int ox;
    int oy;
    size_t pixel_count;
    uint32_t* spr_px;
    int alpha;
    int angle_2d;
    int pre_rot_sw;
    int pre_rot_sh;
    int pre_rot_ox;
    int pre_rot_oy;

    if( cmd->scene_id <= 0 )
        return;
    sprites = ToriDraw_SceneSpriteGet(scene, cmd->scene_id, &count);
    if( !sprites || count <= 0 )
        return;
    atlas = cmd->atlas_index;
    if( atlas < 0 || atlas >= count )
        atlas = 0;
    spr = sprites[atlas];
    if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
        return;

    vp = viewport_from_clip(&cmd->clip, stride);

    nominal_w = spr->width;
    nominal_h = spr->height;
    sw = nominal_w;
    sh = nominal_h;
    ox = spr->crop_x;
    oy = spr->crop_y;
    pixel_count = (size_t)sw * (size_t)sh;

    spr_px = malloc(pixel_count * sizeof(uint32_t));
    if( !spr_px )
        return;
    memcpy(spr_px, spr->pixels_argb, pixel_count * sizeof(uint32_t));

    if( cmd->outline > 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* outlined =
            ToriDraw_SpriteNewGraphicOutline(spr_px, sw, sh, cmd->outline, &sw2, &sh2);
        if( outlined )
        {
            free(spr_px);
            spr_px = outlined;
            sw = sw2;
            sh = sh2;
            ox -= cmd->outline;
            oy -= cmd->outline;
        }
    }

    if( cmd->graphic_shadow != 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* shadowed = ToriDraw_SpriteNewGraphicShadow(
            spr_px, sw, sh, cmd->graphic_shadow, &sw2, &sh2);
        if( shadowed )
        {
            free(spr_px);
            spr_px = shadowed;
            sw = sw2;
            sh = sh2;
        }
    }

    alpha = 255 - cmd->trans;
    if( alpha < 0 )
        alpha = 0;
    else if( alpha > 255 )
        alpha = 255;
    uitree_cmd_scale_pixel_alpha(spr_px, (size_t)sw * (size_t)sh, alpha);

    angle_2d = cmd->rotation;
    pre_rot_sw = sw;
    pre_rot_sh = sh;
    pre_rot_ox = ox;
    pre_rot_oy = oy;

    /* IF3: stretch to widget bounds. IF1: native size + crop offset. */
    if( cmd->if3 && !cmd->tiled )
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, 0);

        {
            uint32_t* clamped =
                uitree_cmd_clamp_to_nominal(spr_px, sw, sh, ox, oy, nominal_w, nominal_h);
            if( clamped )
            {
                free(spr_px);
                spr_px = clamped;
                sw = nominal_w;
                sh = nominal_h;
                ox = 0;
                oy = 0;
            }
        }

        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, 0, 0, angle_2d);

        {
            int draw_w = cmd->w > 0 ? cmd->w : sw;
            int draw_h = cmd->h > 0 ? cmd->h : sh;
            ToriDraw2D_BlitArgbScaled(
                &vp, cmd->x, cmd->y, draw_w, draw_h, spr_px, sw, sh, pixels);
        }
    }
    else
    {
        ToriDraw_SpriteTransformPixels(
            &spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, angle_2d);

        if( cmd->tiled )
        {
            ToriDraw2D_BlitArgbTiled(
                &vp,
                cmd->x,
                cmd->y,
                cmd->w,
                cmd->h,
                spr_px,
                sw,
                sh,
                cmd->x + ox,
                cmd->y + oy,
                pixels);
        }
        else
        {
            int draw_x = cmd->x + ox;
            int draw_y = cmd->y + oy;
            if( angle_2d != 0 )
            {
                int center_x = cmd->x + pre_rot_ox + pre_rot_sw / 2;
                int center_y = cmd->y + pre_rot_oy + pre_rot_sh / 2;
                draw_x = center_x - sw / 2;
                draw_y = center_y - sh / 2;
            }
            ToriDraw2D_BlitArgb(&vp, draw_x, draw_y, spr_px, sw, sh, pixels);
        }
    }

    free(spr_px);
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

    if( cmd->font_id < 0 || !cmd->text )
        return;
    font = ToriDraw_SceneFontGet(scene, cmd->font_id);
    if( !font )
        return;

    vp = viewport_from_clip(&cmd->clip, stride);
    (void)ToriDraw2D_DrawStringBox(
        font,
        &vp,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->text,
        cmd->color,
        cmd->text_center,
        cmd->text_y_align,
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

    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);

    (void)ToriDraw_RenderModelExtentsAtWidget(
        scene,
        hnd,
        cmd->model_zoom > 0 ? cmd->model_zoom : 2000,
        cmd->model_xan,
        cmd->model_yan,
        cmd->model_zan,
        cmd->model_x_offset,
        cmd->model_y_offset,
        0,
        cmd->model_orthog != 0,
        cmd->model_fixed_zoom != 0,
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
