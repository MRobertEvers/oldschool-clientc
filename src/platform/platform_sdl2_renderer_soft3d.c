#include "platform/platform_sdl2_renderer_soft3d.h"

#include "render/torirs_frame.h"

#include "toridraw.h"
#include "toridraw_2d.h"
#include "toridraw_font.h"
#include "toridraw_model_sprite.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * Emitted scissor -> raster clip rect, intersected with the pixel buffer.
 *
 * The clip an emitter produces is whatever the layout says (a RevConfig node
 * box, an IF3 parent's box); nothing guarantees it lies inside the canvas —
 * chrome anchored to a screen edge routinely extends past it. Every draw kind
 * funnels through here, so this is the one place that can promise a command
 * cannot write outside the buffer.
 */
static struct ToriDraw_ViewPort
viewport_from_scissor(
    struct ToriRS_Soft3D const* soft,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h)
{
    struct ToriDraw_ViewPort vp;
    int right = scissor_x + scissor_w;
    int bottom = scissor_y + scissor_h;

    memset(&vp, 0, sizeof(vp));
    vp.stride = soft->stride;
    vp.clip_left = scissor_x < 0 ? 0 : scissor_x;
    vp.clip_top = scissor_y < 0 ? 0 : scissor_y;
    vp.clip_right = right > soft->width ? soft->width : right;
    vp.clip_bottom = bottom > soft->height ? soft->height : bottom;
    /* An entirely off-canvas box collapses to empty rather than inverting. */
    if( vp.clip_right < vp.clip_left )
        vp.clip_right = vp.clip_left;
    if( vp.clip_bottom < vp.clip_top )
        vp.clip_bottom = vp.clip_top;
    return vp;
}

static uint32_t*
soft3d_clamp_to_nominal(
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
soft3d_scale_pixel_alpha(
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
soft3d_draw_sprite(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Sprite const* cmd)
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

    assert(soft);
    assert(cmd);
    assert(soft->scene);

    if( cmd->scene_id <= 0 )
        return;
    sprites = ToriDraw_SceneSpriteGet(soft->scene, cmd->scene_id, &count);
    if( !sprites || count <= 0 )
        return;
    atlas = cmd->atlas_index;
    if( atlas < 0 || atlas >= count )
        atlas = 0;
    spr = sprites[atlas];
    if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
        return;

    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);

    /* Chrome rotated by camera yaw (compass, minimap, scrollbar arrows): inverse-map
     * the destination box through the anchor pair instead of growing a pixel buffer.
     * Units here are 0..2047, not the IF3 spriteAngle scale used further down. */
    if( cmd->rotated )
    {
        struct ToriDraw_Sprite* mask_spr = NULL;
        if( cmd->mask_scene_id > 0 )
        {
            int mask_count = 0;
            struct ToriDraw_Sprite** mask_sprites =
                ToriDraw_SceneSpriteGet(soft->scene, cmd->mask_scene_id, &mask_count);
            if( mask_sprites && cmd->mask_atlas_index >= 0 &&
                cmd->mask_atlas_index < mask_count )
                mask_spr = mask_sprites[cmd->mask_atlas_index];
        }
        if( mask_spr && mask_spr->pixels_argb )
            ToriDraw2D_BlitSpriteRotatedMaskedEx(
                spr,
                mask_spr,
                cmd->mask_keep_opaque,
                &vp,
                cmd->x,
                cmd->y,
                cmd->w > 0 ? cmd->w : spr->width,
                cmd->h > 0 ? cmd->h : spr->height,
                cmd->dst_anchor_x,
                cmd->dst_anchor_y,
                cmd->src_anchor_x,
                cmd->src_anchor_y,
                cmd->rotation_r2pi2048,
                soft->pixels);
        else
            ToriDraw2D_BlitSpriteRotatedEx(
                spr,
                &vp,
                cmd->x,
                cmd->y,
                cmd->w > 0 ? cmd->w : spr->width,
                cmd->h > 0 ? cmd->h : spr->height,
                cmd->dst_anchor_x,
                cmd->dst_anchor_y,
                cmd->src_anchor_x,
                cmd->src_anchor_y,
                cmd->rotation_r2pi2048,
                soft->pixels);
        return;
    }

    nominal_w = spr->width;
    nominal_h = spr->height;
    sw = nominal_w;
    sh = nominal_h;
    ox = spr->crop_x;
    oy = spr->crop_y;
    pixel_count = (size_t)sw * (size_t)sh;

    /*
     * Fast path: no command below mutates pixels, so blit the scene's cached
     * sprite directly instead of cloning it.
     *
     * The general path allocates and copies the whole image up to four times
     * per sprite per frame (clone, flip, clamp-to-nominal, rotate). At rev230
     * gameframe sprite counts that clone traffic — and the calloc inside
     * soft3d_clamp_to_nominal in particular — was a top frame-time cost, and
     * every one of those copies is the identity when the sprite is drawn
     * plain, which is the overwhelmingly common case.
     */
    if( cmd->outline <= 0 && cmd->graphic_shadow == 0 && cmd->trans <= 0 &&
        !cmd->flip_h && !cmd->flip_v && cmd->sprite_angle_r2pi65536 == 0 )
    {
        if( cmd->tiled )
        {
            ToriDraw2D_BlitArgbTiled(
                &vp,
                cmd->x,
                cmd->y,
                cmd->w,
                cmd->h,
                spr->pixels_argb,
                sw,
                sh,
                cmd->x + ox,
                cmd->y + oy,
                soft->pixels);
            return;
        }
        if( !cmd->if3 )
        {
            ToriDraw2D_BlitArgb(
                &vp, cmd->x + ox, cmd->y + oy, spr->pixels_argb, sw, sh, soft->pixels);
            return;
        }
        /* if3 scales the *nominal* box, so a crop offset is only skippable when
         * the sprite already sits at that box's origin — otherwise the offset
         * would have to scale with it and the general path has to run. */
        if( ox == 0 && oy == 0 )
        {
            int draw_w = cmd->w > 0 ? cmd->w : sw;
            int draw_h = cmd->h > 0 ? cmd->h : sh;
            ToriDraw2D_BlitArgbScaled(
                &vp,
                cmd->x,
                cmd->y,
                draw_w,
                draw_h,
                spr->pixels_argb,
                sw,
                sh,
                soft->pixels);
            return;
        }
    }

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
    soft3d_scale_pixel_alpha(spr_px, (size_t)sw * (size_t)sh, alpha);

    angle_2d = cmd->sprite_angle_r2pi65536;
    pre_rot_sw = sw;
    pre_rot_sh = sh;
    pre_rot_ox = ox;
    pre_rot_oy = oy;

    if( cmd->if3 && !cmd->tiled )
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, 0);

        /* Identity when the sprite already fills its nominal box at the origin;
         * skipping it drops a full-image calloc+copy from the general path too
         * (a flipped or translucent sprite still usually needs no clamp). */
        if( ox != 0 || oy != 0 || sw != nominal_w || sh != nominal_h )
        {
            uint32_t* clamped =
                soft3d_clamp_to_nominal(spr_px, sw, sh, ox, oy, nominal_w, nominal_h);
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
                &vp, cmd->x, cmd->y, draw_w, draw_h, spr_px, sw, sh, soft->pixels);
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
                soft->pixels);
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
            ToriDraw2D_BlitArgb(&vp, draw_x, draw_y, spr_px, sw, sh, soft->pixels);
        }
    }

    free(spr_px);
}

static void
soft3d_draw_font(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Font const* cmd)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;

    assert(soft);
    assert(cmd);
    if( cmd->font_id < 0 || !cmd->text )
        return;
    font = ToriDraw_SceneFontGet(soft->scene, cmd->font_id);
    if( !font )
        return;

    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    if( cmd->baseline )
    {
        /* Baseline text (world overlays like hitsplats): y is the text bottom,
         * matching reference PixFont.drawString/centreString. Box alignment
         * (y_align, w/h) does not apply. */
        (void)ToriDraw2D_DrawString(
            font,
            &vp,
            cmd->x,
            cmd->y,
            cmd->text,
            cmd->color,
            cmd->center != 0,
            cmd->shadowed != 0,
            soft->pixels);
        return;
    }
    (void)ToriDraw2D_DrawStringBox(
        font,
        &vp,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->text,
        cmd->color,
        cmd->center,
        cmd->y_align,
        cmd->line_height,
        cmd->shadowed != 0,
        soft->pixels);
}

static void
soft3d_draw_fill_rect(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_FillRect const* cmd)
{
    struct ToriDraw_ViewPort vp;
    int x0;
    int y0;
    int x1;
    int y1;

    assert(soft);
    assert(cmd);
    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    x0 = cmd->x;
    y0 = cmd->y;
    x1 = cmd->x + cmd->w;
    y1 = cmd->y + cmd->h;
    if( cmd->filled )
        ToriDraw2D_FillRect(&vp, x0, y0, x1, y1, cmd->argb, soft->pixels);
    else
        ToriDraw2D_DrawRectOutline(&vp, x0, y0, x1, y1, cmd->argb, soft->pixels);
}

static void
soft3d_draw_clear_rect(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_ClearRect const* cmd)
{
    struct ToriDraw_ViewPort vp;
    int x0;
    int y0;
    int x1;
    int y1;

    assert(soft);
    assert(cmd);
    vp = viewport_from_scissor(soft, 0, 0, soft->width, soft->height);
    x0 = cmd->x;
    y0 = cmd->y;
    x1 = cmd->x + cmd->w;
    y1 = cmd->y + cmd->h;
    ToriDraw2D_FillRect(&vp, x0, y0, x1, y1, TORIRS_SOFT3D_BG, soft->pixels);
}

static void
soft3d_draw_line(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Line const* cmd)
{
    struct ToriDraw_ViewPort vp;
    int thickness;
    int x1;
    int y1;
    int x2;
    int y2;

    assert(soft);
    assert(cmd);
    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    thickness = cmd->line_width > 0 ? cmd->line_width : 1;

    if( cmd->line_direction )
    {
        x1 = cmd->x;
        y1 = cmd->y + cmd->h;
        x2 = cmd->x + cmd->w;
        y2 = cmd->y;
    }
    else
    {
        x1 = cmd->x;
        y1 = cmd->y;
        x2 = cmd->x + cmd->w;
        y2 = cmd->y + cmd->h;
    }

    ToriDraw2D_DrawLine(&vp, x1, y1, x2, y2, thickness, cmd->argb, soft->pixels);
}

static void
soft3d_draw_model_widget(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_ModelWidget const* cmd)
{
    int draw_x = 0;
    int draw_y = 0;
    int out_w = 0;
    int out_h = 0;

    assert(soft);
    assert(cmd);
    if( cmd->model.kind == TORIDRAWMK_NONE )
        return;

    (void)ToriDraw_RenderModelExtentsAtWidget(
        soft->scene,
        cmd->model,
        cmd->model_zoom > 0 ? cmd->model_zoom : 2000,
        cmd->model_xan,
        cmd->model_yan,
        cmd->model_zan,
        cmd->model_x_offset,
        cmd->model_y_offset,
        0,
        cmd->model_orthog != 0,
        cmd->model_fixed_zoom != 0,
        (toripixel_t*)soft->pixels,
        soft->stride,
        soft->width,
        soft->height,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->scissor_x,
        cmd->scissor_y,
        cmd->scissor_x + cmd->scissor_w,
        cmd->scissor_y + cmd->scissor_h,
        &draw_x,
        &draw_y,
        &out_w,
        &out_h);
}

static void
soft3d_draw_model(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Model const* cmd)
{
    struct ToriDraw_Position position;

    assert(soft);
    assert(cmd);
    if( !soft->has_3d )
        return;
    if( cmd->model.kind == TORIDRAWMK_NONE )
        return;

    if( cmd->animation && cmd->element_id >= 0 )
        ToriDraw_SceneElementApplyAnimation(
            soft->scene, cmd->element_id, cmd->anim_index == 0, cmd->anim_frame);

    position = cmd->position;
    if( ToriDraw_RenderModel1Project(
            cmd->model, soft->scene, &position, &soft->view_port_3d, &soft->camera_3d) !=
        TORIDRAW_CULL_VISIBLE )
        return;

    /* Hittest before the face sort: the scene scratch holds this model's
     * projection only until the next model projects, and a model whose faces
     * all sort away must still pick. */
    if( soft->pick_enabled && cmd->pickable && cmd->element_id >= 0 &&
        (cmd->pick_aabb ? ToriDraw_ProjectedModelContainsAabb(
                              soft->scene, soft->pick_mouse_x, soft->pick_mouse_y)
                        : ToriDraw_ProjectedModelContainsPoint(
                              soft->scene, cmd->model, &soft->view_port_3d, soft->pick_mouse_x,
                              soft->pick_mouse_y)) )
        ToriRS_PickHitsAdd(
            &soft->pick_hits,
            cmd->element_id,
            cmd->pick_terrain,
            cmd->pick_tile_x,
            cmd->pick_tile_z,
            cmd->pick_tile_level);

    if( ToriDraw_RenderModel2SortFaces(cmd->model, soft->scene) <= 0 )
        return;
    ToriDraw_RenderModel3Raster(
        soft->scene, &soft->view_port_3d, &soft->camera_3d, soft->pixels, false);
}

void
ToriRS_Soft3D_Init(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    int* pixels,
    int width,
    int height)
{
    assert(soft);
    assert(scene);
    assert(pixels);
    assert(width > 0 && height > 0);
    memset(soft, 0, sizeof(*soft));
    soft->scene = scene;
    soft->pixels = pixels;
    soft->width = width;
    soft->height = height;
    soft->stride = width;
}

void
ToriRS_Soft3D_SetPick(
    struct ToriRS_Soft3D* soft,
    int mouse_x,
    int mouse_y)
{
    assert(soft);
    soft->pick_enabled = true;
    soft->pick_mouse_x = mouse_x;
    soft->pick_mouse_y = mouse_y;
    ToriRS_PickHitsReset(&soft->pick_hits);
}

void
ToriRS_Soft3D_Execute(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd)
{
    assert(soft);
    assert(cmd);

    switch( cmd->kind )
    {
    case TORIRSRC_BEGIN_3D:
        soft->has_3d = true;
        soft->view_port_3d = cmd->u.begin_3d.view_port;
        soft->camera_3d = cmd->u.begin_3d.camera;
        if( soft->view_port_3d.stride <= 0 )
            soft->view_port_3d.stride = soft->stride;
        break;

    case TORIRSRC_END_3D:
        soft->has_3d = false;
        break;

    case TORIRSRC_BEGIN_2D:
    case TORIRSRC_END_2D:
        break;

    case TORIRSRC_CLEAR_RECT:
        soft3d_draw_clear_rect(soft, &cmd->u.clear_rect);
        break;

    case TORIRSRC_FILL_RECT:
        soft3d_draw_fill_rect(soft, &cmd->u.fill_rect);
        break;

    case TORIRSRC_DRAW_MODEL:
        soft3d_draw_model(soft, &cmd->u.model);
        break;

    case TORIRSRC_DRAW_MODEL_WIDGET:
        soft3d_draw_model_widget(soft, &cmd->u.model_widget);
        break;

    case TORIRSRC_SPRITE:
        soft3d_draw_sprite(soft, &cmd->u.sprite);
        break;

    case TORIRSRC_FONT:
        soft3d_draw_font(soft, &cmd->u.font);
        break;

    case TORIRSRC_LINE:
        soft3d_draw_line(soft, &cmd->u.line);
        break;

    case TORIRSRC_MODEL_LOAD:
    case TORIRSRC_MODEL_UNLOAD:
    case TORIRSRC_ANIM_LOAD:
    case TORIRSRC_ANIM_UNLOAD:
    case TORIRSRC_TEX_LOAD:
    case TORIRSRC_TEX_UNLOAD:
    case TORIRSRC_SPRITE_LOAD:
    case TORIRSRC_SPRITE_UNLOAD:
    case TORIRSRC_FONT_LOAD:
    case TORIRSRC_FONT_UNLOAD:
    case TORIRSRC_BATCH3D_BEGIN:
    case TORIRSRC_BATCH3D_MODEL_ADD:
    case TORIRSRC_BATCH3D_ANIM_ADD:
    case TORIRSRC_BATCH3D_END:
    case TORIRSRC_BATCH3D_CLEAR:
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
    case TORIRSRC_NONE:
        break;
    }
}

void
ToriRS_Soft3D_RenderFrame(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand cmd;
    int i;
    size_t n;

    assert(soft);
    assert(frame);
    assert(soft->pixels);
    assert(soft->width > 0 && soft->height > 0);

    n = (size_t)soft->width * (size_t)soft->height;
    for( i = 0; i < (int)n; i++ )
        soft->pixels[i] = TORIRS_SOFT3D_BG;

    soft->has_3d = false;
    ToriRS_FrameBegin(frame);
    while( ToriRS_FrameNextCommand(frame, &cmd) )
        ToriRS_Soft3D_Execute(soft, &cmd);
    ToriRS_FrameEnd(frame);
}
