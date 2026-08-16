#include "render/torirs_frame.h"

#include "painters/painters.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_scroll.h"
#include "world/world.h"

#include "graphics/projection.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
frame_queue(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand const* cmd)
{
    assert(frame);
    assert(cmd);
    assert(!frame->has_queued);
    frame->queued = *cmd;
    frame->has_queued = true;
}

static bool
frame_take_queued(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(out);
    if( !frame->has_queued )
        return false;
    *out = frame->queued;
    frame->has_queued = false;
    return true;
}

/** Translate ToriDraw resource events into the retained renderer command
 * stream.  Static models and all of their animation poses must be baked before
 * DRAW; texture loads must also reach a retained VBO after asynchronous cache
 * loading.  Soft3D safely ignores the resource-only commands. */
static bool
frame_translate_scene_event(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Event const* ev,
    struct ToriRS_RenderCommand* out)
{
    struct ToriDraw_SceneElement* element;
    assert(ev);
    assert(out);
    memset(out, 0, sizeof(*out));
    switch( ev->kind )
    {
    case TORIDRAW_EVENT_MODEL_LOAD:
        if( !scene || !ToriDraw_SceneElementIsLive(scene, ev->element_id) )
            return false;
        element = ToriDraw_SceneElementGet(scene, ev->element_id);
        /* Dynamic entities are rebuilt in the per-frame TRSPK group.  Their
         * MODEL_LOAD event is emitted just before the creator marks them
         * dynamic, so baking that snapshot would only leak persistent arena
         * space at its pre-position origin. */
        if( !element || element->dynamic || element->model.kind == TORIDRAWMK_NONE )
            return false;
        out->kind = TORIRSRC_MODEL_LOAD;
        out->u.model_load.element_id = ev->element_id;
        out->u.model_load.model = element->model;
        out->u.model_load.world_position = element->world_position;
        return true;
    case TORIDRAW_EVENT_MODEL_UNLOAD:
        out->kind = TORIRSRC_MODEL_UNLOAD;
        out->u.model_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_ANIM_LOAD:
        if( !scene || !ToriDraw_SceneElementIsLive(scene, ev->element_id) )
            return false;
        element = ToriDraw_SceneElementGet(scene, ev->element_id);
        if( !element || element->dynamic || element->model.kind == TORIDRAWMK_NONE )
            return false;
        out->kind = TORIRSRC_ANIM_LOAD;
        out->u.anim_load.element_id = ev->element_id;
        out->u.anim_load.anim_index = ev->anim_index;
        out->u.anim_load.animation =
            ev->anim_index == 0 ? element->animation : element->secondary_animation;
        if( !out->u.anim_load.animation )
            return false;
        out->u.anim_load.model = element->model;
        out->u.anim_load.world_position = element->world_position;
        return true;
    case TORIDRAW_EVENT_ANIM_UNLOAD:
        out->kind = TORIRSRC_ANIM_UNLOAD;
        out->u.anim_load.element_id = ev->element_id;
        out->u.anim_load.anim_index = ev->anim_index;
        return true;
    case TORIDRAW_EVENT_TEX_LOAD:
        out->kind = TORIRSRC_TEX_LOAD;
        out->u.tex_load.texture_id = ev->texture_id;
        out->u.tex_load.texture =
            scene && ToriDraw_SceneTexState(scene)
                ? ToriDraw_TextureMapGet(
                      &ToriDraw_SceneTexState(scene)->texture_map,
                      ev->texture_id)
                : NULL;
        if( !out->u.tex_load.texture )
            return false;
        return true;
    case TORIDRAW_EVENT_TEX_UNLOAD:
        out->kind = TORIRSRC_TEX_UNLOAD;
        out->u.tex_load.texture_id = ev->texture_id;
        return true;
    case TORIDRAW_EVENT_BATCH_BEGIN:
        out->kind = TORIRSRC_BATCH3D_BEGIN;
        out->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_BATCH_MODEL_ADD:
        /* SceneBatchAddElement emits a NONE placeholder before the real model
         * is assigned.  Do not resolve that sentinel to the element's later
         * live handle and bake the same geometry twice. */
        if( ev->model.kind == TORIDRAWMK_NONE )
            return false;
        if( !scene || !ToriDraw_SceneElementIsLive(scene, ev->element_id) )
            return false;
        element = ToriDraw_SceneElementGet(scene, ev->element_id);
        if( !element || element->dynamic || element->model.kind == TORIDRAWMK_NONE )
            return false;
        out->kind = TORIRSRC_BATCH3D_MODEL_ADD;
        out->u.batch.batch_id = ev->batch_id;
        out->u.batch.element_id = ev->element_id;
        out->u.batch.pose_id = ev->pose_id;
        out->u.batch.model = element->model;
        out->u.batch.world_position = element->world_position;
        return true;
    case TORIDRAW_EVENT_BATCH_ANIM_ADD:
        if( !scene || !ToriDraw_SceneElementIsLive(scene, ev->element_id) )
            return false;
        out->kind = TORIRSRC_BATCH3D_ANIM_ADD;
        out->u.batch.batch_id = ev->batch_id;
        out->u.batch.element_id = ev->element_id;
        out->u.batch.pose_id = ev->pose_id;
        out->u.batch.anim_index = ev->anim_index;
        out->u.batch.model = ev->model;
        out->u.batch.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_BATCH_END:
        out->kind = TORIRSRC_BATCH3D_END;
        out->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_BATCH_CLEAR:
        out->kind = TORIRSRC_BATCH3D_CLEAR;
        out->u.batch.batch_id = ev->batch_id;
        out->u.batch.clear_all = ev->batch_id == TORIDRAW_SCENE_INVALID_BATCH_ID;
        return true;
    case TORIDRAW_EVENT_SCENE_RESET:
        out->kind = TORIRSRC_BATCH3D_CLEAR;
        out->u.batch.batch_id = TORIDRAW_SCENE_INVALID_BATCH_ID;
        out->u.batch.clear_all = true;
        return true;
    default:
        return false;
    }
}

static bool
frame_take_scene_event(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    struct ToriDraw_EventQueue* eq;
    assert(frame);
    assert(out);
    assert(frame->scene);
    eq = ToriDraw_SceneEvents(frame->scene);
    if( !eq )
        return false;
    while( frame->event_index < eq->count )
    {
        struct ToriDraw_Event const* ev = &eq->events[frame->event_index++];
        if( frame_translate_scene_event(frame->scene, ev, out) )
            return true;
    }
    return false;
}

static void
frame_emit_begin_2d(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->kind = TORIRSRC_BEGIN_2D;
    frame->pass = TORIRS_FRAME_PASS_2D;
}

static void
frame_emit_end_2d(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->kind = TORIRSRC_END_2D;
    frame->pass = TORIRS_FRAME_PASS_NONE;
}

static void
frame_emit_end_3d(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->kind = TORIRSRC_END_3D;
    frame->pass = TORIRS_FRAME_PASS_NONE;
    frame->in_world = false;
    frame->world_begun = false;
}

static int
emit_color_argb(
    int color,
    int trans)
{
    int alpha;
    if( trans < 0 )
        trans = 0;
    else if( trans > 255 )
        trans = 255;
    alpha = 255 - trans;
    return (alpha << 24) | (color & 0xFFFFFF);
}

static void
fill_rect_cmd(
    struct ToriRS_RenderCommand* out,
    int x,
    int y,
    int w,
    int h,
    int argb,
    struct UITreeEmitClip const* clip)
{
    memset(out, 0, sizeof(*out));
    out->kind = TORIRSRC_FILL_RECT;
    out->u.fill_rect.x = x;
    out->u.fill_rect.y = y;
    out->u.fill_rect.w = w;
    out->u.fill_rect.h = h;
    out->u.fill_rect.argb = argb;
    out->u.fill_rect.scissor_x = clip->x;
    out->u.fill_rect.scissor_y = clip->y;
    out->u.fill_rect.scissor_w = clip->w;
    out->u.fill_rect.scissor_h = clip->h;
    out->u.fill_rect.filled = 1;
}

/* Centre of a scene sprite's cropped sub-rect (the pivot BlitSpriteRotatedEx
 * samples around). Leaves the outs untouched when the sprite is not resident. */
static void
sprite_src_center(
    struct ToriRS_Frame* frame,
    int scene_id,
    int atlas_index,
    int* out_cx,
    int* out_cy)
{
    int count = 0;
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Sprite* sprite;

    assert(frame);
    assert(frame->scene);

    sprites = ToriDraw_SceneSpriteGet(frame->scene, scene_id, &count);
    if( !sprites || atlas_index < 0 || atlas_index >= count )
        return;
    sprite = sprites[atlas_index];
    if( !sprite )
        return;
    *out_cx = (sprite->crop_width > 0 ? sprite->crop_width : sprite->width) / 2;
    *out_cy = (sprite->crop_height > 0 ? sprite->crop_height : sprite->height) / 2;
}

/* Canvas size of a scene sprite (crop dims when trimmed, else pixel dims).
 * Leaves the outs untouched when the sprite is not resident. */
static void
sprite_canvas_dims(
    struct ToriRS_Frame* frame,
    int scene_id,
    int atlas_index,
    int* out_w,
    int* out_h)
{
    int count = 0;
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Sprite* sprite;

    assert(frame);
    assert(frame->scene);

    sprites = ToriDraw_SceneSpriteGet(frame->scene, scene_id, &count);
    if( !sprites || atlas_index < 0 || atlas_index >= count )
        return;
    sprite = sprites[atlas_index];
    if( !sprite )
        return;
    *out_w = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
    *out_h = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
}

/* Arm the inverse-mapped rotated blit. Rotation is in camera-yaw units (2048 =
 * full turn); zero leaves the command on the plain blit path. */
static void
sprite_set_rotated(
    struct ToriRS_RenderCommand_Sprite* sprite,
    int rotation_r2pi2048,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y)
{
    if( rotation_r2pi2048 == 0 )
        return;
    sprite->rotated = 1;
    sprite->rotation_r2pi2048 = rotation_r2pi2048;
    sprite->dst_anchor_x = dst_anchor_x;
    sprite->dst_anchor_y = dst_anchor_y;
    sprite->src_anchor_x = src_anchor_x;
    sprite->src_anchor_y = src_anchor_y;
}

static void
sprite_cmd(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out,
    int scene_id,
    int atlas,
    int x,
    int y,
    int w,
    int h,
    int rotation_r2pi2048,
    struct UITreeEmitClip const* clip)
{
    int src_cx = w / 2;
    int src_cy = h / 2;

    memset(out, 0, sizeof(*out));
    out->kind = TORIRSRC_SPRITE;
    out->u.sprite.scene_id = scene_id;
    out->u.sprite.atlas_index = atlas;
    out->u.sprite.x = x;
    out->u.sprite.y = y;
    out->u.sprite.w = w;
    out->u.sprite.h = h;
    out->u.sprite.scissor_x = clip->x;
    out->u.sprite.scissor_y = clip->y;
    out->u.sprite.scissor_w = clip->w;
    out->u.sprite.scissor_h = clip->h;
    /* Chrome pivots on the box centre; the source pivot is the sprite's own
     * centre, which need not match the box (arrows are drawn at native size). */
    if( rotation_r2pi2048 != 0 )
        sprite_src_center(frame, scene_id, atlas, &src_cx, &src_cy);
    sprite_set_rotated(&out->u.sprite, rotation_r2pi2048, w / 2, h / 2, src_cx, src_cy);
    out->u.sprite.if3 = 0;
}

static bool
vertical_scrollbar_grip(
    struct UITreeEmitDesc const* desc,
    int vh,
    int* grip_y,
    int* grip_size,
    int* track_h)
{
    int scroll_height = desc->scroll_content;
    int scroll_y = desc->scroll_off_y;
    int range;

    *track_h = vh - 32;
    if( *track_h <= 0 || scroll_height <= 0 )
        return false;
    *grip_size = (*track_h * vh) / scroll_height;
    if( *grip_size < 8 )
        *grip_size = 8;
    if( *grip_size > *track_h )
        *grip_size = *track_h;
    range = scroll_height - vh;
    *grip_y = range > 0 ? ((*track_h - *grip_size) * scroll_y) / range : 0;
    return true;
}

static bool
horizontal_scrollbar_grip(
    struct UITreeEmitDesc const* desc,
    int sw,
    int* grip_x,
    int* grip_size,
    int* track_w)
{
    int scroll_width = desc->scroll_content;
    int scroll_x = desc->scroll_off_x;
    int range;

    *track_w = sw - 32;
    if( *track_w <= 0 || scroll_width <= 0 )
        return false;
    *grip_size = (*track_w * sw) / scroll_width;
    if( *grip_size < 8 )
        *grip_size = 8;
    if( *grip_size > *track_w )
        *grip_size = *track_w;
    range = scroll_width - sw;
    *grip_x = range > 0 ? ((*track_w - *grip_size) * scroll_x) / range : 0;
    return true;
}

static bool
translate_scrollbar_v_step(
    struct ToriRS_Frame* frame,
    struct UITreeEmitDesc const* desc,
    int step,
    struct ToriRS_RenderCommand* out)
{
    int sb_x = desc->x;
    int ly = desc->y;
    int vh = desc->h;
    int grip_y = 0;
    int grip_size = 0;
    int track_h = 0;
    int grip_y0;
    struct UITreeEmitClip const* clip = &desc->clip;

    switch( step )
    {
    case 0:
        if( desc->scene_id > 0 )
            sprite_cmd(
                frame,
                out,
                desc->scene_id,
                0,
                sb_x,
                ly,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                0,
                clip);
        else
            fill_rect_cmd(
                out,
                sb_x,
                ly,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_ARGB,
                clip);
        return true;
    case 1:
        if( desc->scene_id > 0 )
            sprite_cmd(
                frame,
                out,
                desc->scene_id,
                1,
                sb_x,
                ly + vh - UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                0,
                clip);
        else
            fill_rect_cmd(
                out,
                sb_x,
                ly + vh - UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_ARGB,
                clip);
        return true;
    case 2:
        if( !vertical_scrollbar_grip(desc, vh, &grip_y, &grip_size, &track_h) )
            return false;
        fill_rect_cmd(
            out,
            sb_x,
            ly + UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_THICKNESS,
            track_h,
            UITREE_SCROLLBAR_TRACK_ARGB,
            clip);
        return true;
    case 3:
        if( !vertical_scrollbar_grip(desc, vh, &grip_y, &grip_size, &track_h) )
            return false;
        fill_rect_cmd(
            out,
            sb_x,
            ly + UITREE_SCROLLBAR_THICKNESS + grip_y,
            UITREE_SCROLLBAR_THICKNESS,
            grip_size,
            UITREE_SCROLLBAR_GRIP_ARGB,
            clip);
        return true;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        if( !vertical_scrollbar_grip(desc, vh, &grip_y, &grip_size, &track_h) )
            return false;
        grip_y0 = ly + UITREE_SCROLLBAR_THICKNESS + grip_y;
        switch( step )
        {
        case 4:
            fill_rect_cmd(out, sb_x, grip_y0, 2, grip_size, UITREE_SCROLLBAR_GRIP_HI_ARGB, clip);
            return true;
        case 5:
            fill_rect_cmd(
                out,
                sb_x,
                grip_y0,
                UITREE_SCROLLBAR_THICKNESS,
                2,
                UITREE_SCROLLBAR_GRIP_HI_ARGB,
                clip);
            return true;
        case 6:
            fill_rect_cmd(
                out,
                sb_x + UITREE_SCROLLBAR_THICKNESS - 1,
                grip_y0,
                1,
                grip_size,
                UITREE_SCROLLBAR_GRIP_LO_ARGB,
                clip);
            return true;
        case 7:
            if( grip_size > 1 )
            {
                fill_rect_cmd(
                    out,
                    sb_x + UITREE_SCROLLBAR_THICKNESS - 2,
                    grip_y0 + 1,
                    1,
                    grip_size - 1,
                    UITREE_SCROLLBAR_GRIP_LO_ARGB,
                    clip);
            }
            else
                return false;
            return true;
        case 8:
            fill_rect_cmd(
                out,
                sb_x,
                grip_y0 + grip_size - 1,
                UITREE_SCROLLBAR_THICKNESS,
                1,
                UITREE_SCROLLBAR_GRIP_LO_ARGB,
                clip);
            return true;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return false;
}

static bool
translate_scrollbar_h_step(
    struct ToriRS_Frame* frame,
    struct UITreeEmitDesc const* desc,
    int step,
    struct ToriRS_RenderCommand* out)
{
    int lx = desc->x;
    int sb_y = desc->y;
    int sw = desc->w;
    int grip_x = 0;
    int grip_size = 0;
    int track_w = 0;
    int grip_x0;
    int const h_arrow_rotation = 512;
    struct UITreeEmitClip const* clip = &desc->clip;

    switch( step )
    {
    case 0:
        if( desc->scene_id > 0 )
            sprite_cmd(
                frame,
                out,
                desc->scene_id,
                0,
                lx,
                sb_y,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                h_arrow_rotation,
                clip);
        else
            fill_rect_cmd(
                out,
                lx,
                sb_y,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_ARGB,
                clip);
        return true;
    case 1:
        if( desc->scene_id > 0 )
            sprite_cmd(
                frame,
                out,
                desc->scene_id,
                1,
                lx + sw - UITREE_SCROLLBAR_THICKNESS,
                sb_y,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                h_arrow_rotation,
                clip);
        else
            fill_rect_cmd(
                out,
                lx + sw - UITREE_SCROLLBAR_THICKNESS,
                sb_y,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_ARGB,
                clip);
        return true;
    case 2:
        if( !horizontal_scrollbar_grip(desc, sw, &grip_x, &grip_size, &track_w) )
            return false;
        fill_rect_cmd(
            out,
            lx + UITREE_SCROLLBAR_THICKNESS,
            sb_y,
            track_w,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_TRACK_ARGB,
            clip);
        return true;
    case 3:
        if( !horizontal_scrollbar_grip(desc, sw, &grip_x, &grip_size, &track_w) )
            return false;
        fill_rect_cmd(
            out,
            lx + UITREE_SCROLLBAR_THICKNESS + grip_x,
            sb_y,
            grip_size,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_GRIP_ARGB,
            clip);
        return true;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        if( !horizontal_scrollbar_grip(desc, sw, &grip_x, &grip_size, &track_w) )
            return false;
        grip_x0 = lx + UITREE_SCROLLBAR_THICKNESS + grip_x;
        switch( step )
        {
        case 4:
            fill_rect_cmd(out, grip_x0, sb_y, 2, UITREE_SCROLLBAR_THICKNESS, UITREE_SCROLLBAR_GRIP_HI_ARGB, clip);
            return true;
        case 5:
            fill_rect_cmd(out, grip_x0, sb_y, grip_size, 2, UITREE_SCROLLBAR_GRIP_HI_ARGB, clip);
            return true;
        case 6:
            fill_rect_cmd(
                out,
                grip_x0 + grip_size - 1,
                sb_y,
                1,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_LO_ARGB,
                clip);
            return true;
        case 7:
            if( grip_size > 1 )
            {
                fill_rect_cmd(
                    out,
                    grip_x0 + 1,
                    sb_y + UITREE_SCROLLBAR_THICKNESS - 2,
                    grip_size - 1,
                    1,
                    UITREE_SCROLLBAR_GRIP_LO_ARGB,
                    clip);
            }
            else
                return false;
            return true;
        case 8:
            fill_rect_cmd(
                out,
                grip_x0,
                sb_y + UITREE_SCROLLBAR_THICKNESS - 1,
                grip_size,
                1,
                UITREE_SCROLLBAR_GRIP_LO_ARGB,
                clip);
            return true;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return false;
}

static bool
translate_ui_cmd(
    struct ToriRS_Frame* frame,
    struct UITreeEmitDesc const* desc,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(desc);
    assert(out);
    assert(frame->scene);

    memset(out, 0, sizeof(*out));

    switch( desc->kind )
    {
    case UITREE_EMIT_SPRITE:
    case UITREE_EMIT_CC_OBJ:
        if( desc->scene_id <= 0 )
            return false;
        out->kind = TORIRSRC_SPRITE;
        out->u.sprite.scene_id = desc->scene_id;
        out->u.sprite.atlas_index = desc->atlas_index;
        out->u.sprite.x = desc->x;
        out->u.sprite.y = desc->y;
        out->u.sprite.w = desc->w;
        out->u.sprite.h = desc->h;
        out->u.sprite.scissor_x = desc->clip.x;
        out->u.sprite.scissor_y = desc->clip.y;
        out->u.sprite.scissor_w = desc->clip.w;
        out->u.sprite.scissor_h = desc->clip.h;
        out->u.sprite.outline = desc->outline;
        out->u.sprite.graphic_shadow = desc->graphic_shadow;
        out->u.sprite.trans = desc->trans;
        out->u.sprite.if3 = desc->if3;
        out->u.sprite.tiled = desc->tiled;
        out->u.sprite.flip_h = desc->flip_h;
        out->u.sprite.flip_v = desc->flip_v;
        out->u.sprite.sprite_angle_r2pi65536 = desc->sprite_angle_r2pi65536;
        return true;

    case UITREE_EMIT_TEXT:
        if( desc->font_id < 0 || !desc->text )
            return false;
        out->kind = TORIRSRC_FONT;
        out->u.font.font_id = desc->font_id;
        out->u.font.x = desc->x;
        out->u.font.y = desc->y;
        out->u.font.w = desc->w;
        out->u.font.h = desc->h;
        out->u.font.color = desc->color;
        out->u.font.center = desc->text_center;
        out->u.font.y_align = desc->text_y_align;
        out->u.font.line_height = desc->text_line_height;
        out->u.font.shadowed = desc->text_shadowed;
        out->u.font.baseline = desc->text_baseline;
        /* CS1 %N expansion supersedes the raw cache text when present. */
        out->u.font.text = desc->text_formatted[0] ? desc->text_formatted : desc->text;
        out->u.font.scissor_x = desc->clip.x;
        out->u.font.scissor_y = desc->clip.y;
        out->u.font.scissor_w = desc->clip.w;
        out->u.font.scissor_h = desc->clip.h;
        return true;

    case UITREE_EMIT_RECT:
        out->kind = TORIRSRC_FILL_RECT;
        out->u.fill_rect.x = desc->x;
        out->u.fill_rect.y = desc->y;
        out->u.fill_rect.w = desc->w;
        out->u.fill_rect.h = desc->h;
        out->u.fill_rect.argb = emit_color_argb(desc->color, desc->trans);
        out->u.fill_rect.scissor_x = desc->clip.x;
        out->u.fill_rect.scissor_y = desc->clip.y;
        out->u.fill_rect.scissor_w = desc->clip.w;
        out->u.fill_rect.scissor_h = desc->clip.h;
        out->u.fill_rect.filled = desc->filled;
        return true;

    case UITREE_EMIT_LINE:
        out->kind = TORIRSRC_LINE;
        out->u.line.x = desc->x;
        out->u.line.y = desc->y;
        out->u.line.w = desc->w;
        out->u.line.h = desc->h;
        out->u.line.argb = emit_color_argb(desc->color, desc->trans);
        out->u.line.line_width = desc->line_width;
        out->u.line.line_direction = desc->line_direction;
        out->u.line.scissor_x = desc->clip.x;
        out->u.line.scissor_y = desc->clip.y;
        out->u.line.scissor_w = desc->clip.w;
        out->u.line.scissor_h = desc->clip.h;
        return true;

    case UITREE_EMIT_MODEL:
    {
        struct ToriDraw_ModelHandle hnd;
        if( desc->model_id < 0 )
            return false;
        hnd = ToriDraw_SceneModelGet(frame->scene, desc->model_id);
        if( hnd.kind != TORIDRAWMK_MODEL || !hnd.u.model.model )
            return false;
        out->kind = TORIRSRC_DRAW_MODEL_WIDGET;
        out->u.model_widget.model = hnd;
        out->u.model_widget.x = desc->x;
        out->u.model_widget.y = desc->y;
        out->u.model_widget.w = desc->w;
        out->u.model_widget.h = desc->h;
        out->u.model_widget.scissor_x = desc->clip.x;
        out->u.model_widget.scissor_y = desc->clip.y;
        out->u.model_widget.scissor_w = desc->clip.w;
        out->u.model_widget.scissor_h = desc->clip.h;
        out->u.model_widget.model_zoom = desc->model_zoom;
        out->u.model_widget.model_xan = desc->model_xan;
        out->u.model_widget.model_yan = desc->model_yan;
        out->u.model_widget.model_zan = desc->model_zan;
        out->u.model_widget.model_x_offset = desc->model_x_offset;
        out->u.model_widget.model_y_offset = desc->model_y_offset;
        out->u.model_widget.model_orthog = desc->model_orthog;
        out->u.model_widget.model_fixed_zoom = desc->model_fixed_zoom;
        /*
         * An obj on a MODEL widget is composed the way its icon is, and half
         * the composition is this: the icon rasteriser translates by
         * `-bounds->min_y / 2` so the model sits centred rather than hanging
         * off its origin. Widget models pass 0 here because their own record
         * places them; an obj has no such record, and without the term the
         * make-menu drew every item high and left of its cell.
         */
        if( desc->model_obj_composed )
        {
            struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);

            if( bounds )
                out->u.model_widget.model_center_y = -bounds->min_y / 2;
        }
        return true;
    }

    case UITREE_EMIT_SCROLLBAR_V:
        return translate_scrollbar_v_step(frame, desc, frame->scrollbar_step, out);

    case UITREE_EMIT_SCROLLBAR_H:
        return translate_scrollbar_h_step(frame, desc, frame->scrollbar_step, out);

    case UITREE_EMIT_COMPASS:
    {
        /* Native-size blit rotated by camera yaw: chrome, never IF3-stretched.
         * Both pivots are centres, so the needle spins in place. Matching the
         * reference (widgets-gl drawTextureRotatedMasked): when the pack's
         * placeholder graphic is resident it is the inverted circular mask,
         * and the draw box takes the mask's size centred in the node box. */
        int box_x = desc->x;
        int box_y = desc->y;
        int box_w = desc->w;
        int box_h = desc->h;
        int src_cx = desc->w / 2;
        int src_cy = desc->h / 2;
        int mask_scene_id = 0;
        if( desc->scene_id <= 0 )
            return false;
        sprite_src_center(frame, desc->scene_id, desc->atlas_index, &src_cx, &src_cy);
        if( desc->mask_scene_id > 0 )
        {
            int mask_w = 0;
            int mask_h = 0;
            sprite_canvas_dims(
                frame, desc->mask_scene_id, desc->mask_atlas_index, &mask_w, &mask_h);
            if( mask_w > 0 && mask_h > 0 )
            {
                box_x = desc->x + (desc->w - mask_w) / 2;
                box_y = desc->y + (desc->h - mask_h) / 2;
                box_w = mask_w;
                box_h = mask_h;
                mask_scene_id = desc->mask_scene_id;
            }
        }
        out->kind = TORIRSRC_SPRITE;
        out->u.sprite.scene_id = desc->scene_id;
        out->u.sprite.atlas_index = desc->atlas_index;
        out->u.sprite.x = box_x;
        out->u.sprite.y = box_y;
        out->u.sprite.w = box_w;
        out->u.sprite.h = box_h;
        out->u.sprite.scissor_x = desc->clip.x;
        out->u.sprite.scissor_y = desc->clip.y;
        out->u.sprite.scissor_w = desc->clip.w;
        out->u.sprite.scissor_h = desc->clip.h;
        out->u.sprite.trans = desc->trans;
        out->u.sprite.if3 = 0;
        out->u.sprite.mask_scene_id = mask_scene_id;
        out->u.sprite.mask_atlas_index = desc->mask_atlas_index;
        out->u.sprite.mask_keep_opaque = desc->mask_keep_opaque;
        /* The compass is a disc in a square box, so the corners fall outside
         * the mask and the blit never writes them. */
        out->u.sprite.mask_needs_clear = 1;
        /* Rotation 0 (camera facing north) still needs the anchored blit:
         * the plain path would draw the padded canvas top-left-anchored. */
        out->u.sprite.rotated = 1;
        out->u.sprite.rotation_r2pi2048 = desc->rotation_r2pi2048;
        out->u.sprite.dst_anchor_x = box_w / 2;
        out->u.sprite.dst_anchor_y = box_h / 2;
        out->u.sprite.src_anchor_x = src_cx;
        out->u.sprite.src_anchor_y = src_cy;
        return true;
    }

    case UITREE_EMIT_MINIMAP:
        /* Steps past 0 draw the host-computed dot overlay (reference
         * minimapDraw: obj/NPC/player dots, flag, white local square) on top
         * of the map blit, clipped to the same box. */
        if( frame->scrollbar_step > 0 )
        {
            struct UITreeMinimapDot const* dot;
            int box_cx = desc->x + desc->w / 2;
            int box_cy = desc->y + desc->h / 2;
            int box_right = desc->x + desc->w;
            int box_bottom = desc->y + desc->h;
            int clip_right = desc->clip.x + desc->clip.w;
            int clip_bottom = desc->clip.y + desc->clip.h;
            int left = desc->x > desc->clip.x ? desc->x : desc->clip.x;
            int top = desc->y > desc->clip.y ? desc->y : desc->clip.y;
            int right = box_right < clip_right ? box_right : clip_right;
            int bottom = box_bottom < clip_bottom ? box_bottom : clip_bottom;

            if( !desc->minimap_dots || frame->scrollbar_step > desc->minimap_dot_count )
                return false;
            dot = &desc->minimap_dots[frame->scrollbar_step - 1];
            if( dot->scene_id > 0 )
            {
                out->kind = TORIRSRC_SPRITE;
                out->u.sprite.scene_id = dot->scene_id;
                out->u.sprite.atlas_index = dot->atlas_index;
                out->u.sprite.x = box_cx + dot->dx;
                out->u.sprite.y = box_cy + dot->dy;
                out->u.sprite.w = dot->w;
                out->u.sprite.h = dot->h;
                out->u.sprite.scissor_x = left;
                out->u.sprite.scissor_y = top;
                out->u.sprite.scissor_w = right > left ? right - left : 0;
                out->u.sprite.scissor_h = bottom > top ? bottom - top : 0;
                out->u.sprite.if3 = 0;
            }
            else
            {
                out->kind = TORIRSRC_FILL_RECT;
                out->u.fill_rect.x = box_cx + dot->dx;
                out->u.fill_rect.y = box_cy + dot->dy;
                out->u.fill_rect.w = dot->w;
                out->u.fill_rect.h = dot->h;
                out->u.fill_rect.argb = dot->color;
                out->u.fill_rect.scissor_x = left;
                out->u.fill_rect.scissor_y = top;
                out->u.fill_rect.scissor_w = right > left ? right - left : 0;
                out->u.fill_rect.scissor_h = bottom > top ? bottom - top : 0;
                out->u.fill_rect.filled = 1;
            }
            return true;
        }
        /* The baked world map is far larger than the widget: the source pivot
         * follows the camera position while the destination pivot stays at the
         * box centre, so the map scrolls and spins under a fixed frame. */
        if( desc->scene_id <= 0 )
            return false;
        out->kind = TORIRSRC_SPRITE;
        out->u.sprite.scene_id = desc->scene_id;
        out->u.sprite.atlas_index = 0;
        out->u.sprite.x = desc->x;
        out->u.sprite.y = desc->y;
        out->u.sprite.w = desc->w;
        out->u.sprite.h = desc->h;
        /* Clip to the widget box intersected with the node's own clip rect:
         * the map always over-fills its frame, so the box is what bounds it —
         * but a box that sticks out past its parent (or the canvas) must not
         * paint there, which the plain box alone would allow. */
        {
            int box_right = desc->x + desc->w;
            int box_bottom = desc->y + desc->h;
            int clip_right = desc->clip.x + desc->clip.w;
            int clip_bottom = desc->clip.y + desc->clip.h;
            int left = desc->x > desc->clip.x ? desc->x : desc->clip.x;
            int top = desc->y > desc->clip.y ? desc->y : desc->clip.y;
            int right = box_right < clip_right ? box_right : clip_right;
            int bottom = box_bottom < clip_bottom ? box_bottom : clip_bottom;

            out->u.sprite.scissor_x = left;
            out->u.sprite.scissor_y = top;
            out->u.sprite.scissor_w = right > left ? right - left : 0;
            out->u.sprite.scissor_h = bottom > top ? bottom - top : 0;
        }
        out->u.sprite.if3 = 0;
        /* The pack's mask placeholder clips the over-filled map to its round
         * window (inverted: map shows where the mask is transparent). */
        out->u.sprite.mask_scene_id = desc->mask_scene_id;
        out->u.sprite.mask_atlas_index = desc->mask_atlas_index;
        out->u.sprite.mask_keep_opaque = desc->mask_keep_opaque;
        /* Same round window as the compass: the map over-fills its box and the
         * mask clips it to a circle, so the corners are never written. */
        out->u.sprite.mask_needs_clear = 1;
        /* Rotation 0 (camera facing north) still needs the anchored blit. */
        out->u.sprite.rotated = 1;
        out->u.sprite.rotation_r2pi2048 = desc->rotation_r2pi2048;
        out->u.sprite.dst_anchor_x = desc->w / 2;
        out->u.sprite.dst_anchor_y = desc->h / 2;
        out->u.sprite.src_anchor_x = desc->src_anchor_x;
        out->u.sprite.src_anchor_y = desc->src_anchor_y;
        return true;

    case UITREE_EMIT_WORLDMAP:
    {
        /* Step 0 paints the surface background (what shows where a region is
         * missing or still loading); steps 1..N blit the baked regions, each
         * 1:1 because the host baked them at the view's pixels-per-tile. */
        int step = frame->scrollbar_step;
        if( step == 0 )
        {
            out->kind = TORIRSRC_FILL_RECT;
            out->u.fill_rect.x = desc->x;
            out->u.fill_rect.y = desc->y;
            out->u.fill_rect.w = desc->w;
            out->u.fill_rect.h = desc->h;
            out->u.fill_rect.argb = emit_color_argb(desc->color, 0);
            out->u.fill_rect.scissor_x = desc->clip.x;
            out->u.fill_rect.scissor_y = desc->clip.y;
            out->u.fill_rect.scissor_w = desc->clip.w;
            out->u.fill_rect.scissor_h = desc->clip.h;
            out->u.fill_rect.filled = 1;
            return true;
        }
        if( !desc->worldmap_tiles || step - 1 >= desc->worldmap_tile_count )
            return false;
        {
            struct UITreeWorldMapTile const* tile = &desc->worldmap_tiles[step - 1];
            if( tile->scene_id <= 0 )
                return false;
            out->kind = TORIRSRC_SPRITE;
            out->u.sprite.scene_id = tile->scene_id;
            out->u.sprite.atlas_index = tile->atlas_index;
            out->u.sprite.x = tile->x;
            out->u.sprite.y = tile->y;
            out->u.sprite.w = tile->w;
            out->u.sprite.h = tile->h;
            /* if3 is the renderers' "scale to the destination box" flag, in both
             * the software and GL paths. */
            out->u.sprite.if3 = (uint8_t)(tile->scaled ? 1 : 0);
            out->u.sprite.scissor_x = desc->clip.x;
            out->u.sprite.scissor_y = desc->clip.y;
            out->u.sprite.scissor_w = desc->clip.w;
            out->u.sprite.scissor_h = desc->clip.h;
            return true;
        }
    }

    case UITREE_EMIT_ENTITY_OVERLAY:
    {
        /* One primitive per multi-step (reference drawEntities emits a health
         * bar as two rects and a hitsplat as sprite + shadow text + text).
         * Coordinates are absolute screen px; the desc box is the world
         * viewport, which is also the clip — the reference draws these with
         * Pix2D clipped to the scene viewport. */
        struct UITreeEntityOverlay const* item;
        if( !desc->entity_overlays || frame->scrollbar_step >= desc->entity_overlay_count )
            return false;
        item = &desc->entity_overlays[frame->scrollbar_step];

        switch( item->kind )
        {
        case UITREE_ENTITY_OVERLAY_SPRITE:
            if( item->scene_id <= 0 )
                return false;
            out->kind = TORIRSRC_SPRITE;
            out->u.sprite.scene_id = item->scene_id;
            out->u.sprite.atlas_index = item->atlas_index;
            out->u.sprite.x = item->x;
            out->u.sprite.y = item->y;
            out->u.sprite.w = item->w;
            out->u.sprite.h = item->h;
            out->u.sprite.scissor_x = desc->clip.x;
            out->u.sprite.scissor_y = desc->clip.y;
            out->u.sprite.scissor_w = desc->clip.w;
            out->u.sprite.scissor_h = desc->clip.h;
            out->u.sprite.if3 = 0;
            return true;
        case UITREE_ENTITY_OVERLAY_TEXT:
            /* >= 0: dat1 p11 is cache font id 0, and scene font ids are the
             * cache ids (see app_hitsplat_font_scene_id). */
            if( item->font_id < 0 || item->text[0] == '\0' )
                return false;
            out->kind = TORIRSRC_FONT;
            out->u.font.font_id = item->font_id;
            out->u.font.text = item->text;
            out->u.font.x = item->x;
            out->u.font.y = item->y;
            out->u.font.w = item->w;
            out->u.font.h = item->h;
            out->u.font.color = (int)item->color;
            out->u.font.center = 1;
            /* Reference draws hitsplat numbers with `p11.centreString`, whose
             * y is the text baseline/bottom (drawString does `y -= height2d`),
             * not a widget-box top. Without this the number lands ~1 line
             * height too low and drifts off the hitmark sprite. */
            out->u.font.baseline = 1;
            out->u.font.scissor_x = desc->clip.x;
            out->u.font.scissor_y = desc->clip.y;
            out->u.font.scissor_w = desc->clip.w;
            out->u.font.scissor_h = desc->clip.h;
            return true;
        case UITREE_ENTITY_OVERLAY_LINE:
            out->kind = TORIRSRC_LINE;
            out->u.line.x = item->x;
            out->u.line.y = item->y;
            out->u.line.w = item->w;
            out->u.line.h = item->h;
            out->u.line.argb = item->color;
            out->u.line.line_width = item->line_width > 0 ? item->line_width : 1;
            out->u.line.line_direction = item->line_direction;
            out->u.line.scissor_x = desc->clip.x;
            out->u.line.scissor_y = desc->clip.y;
            out->u.line.scissor_w = desc->clip.w;
            out->u.line.scissor_h = desc->clip.h;
            return true;
        case UITREE_ENTITY_OVERLAY_RECT:
        default:
            out->kind = TORIRSRC_FILL_RECT;
            out->u.fill_rect.x = item->x;
            out->u.fill_rect.y = item->y;
            out->u.fill_rect.w = item->w;
            out->u.fill_rect.h = item->h;
            out->u.fill_rect.argb = item->color;
            out->u.fill_rect.filled = 1;
            out->u.fill_rect.scissor_x = desc->clip.x;
            out->u.fill_rect.scissor_y = desc->clip.y;
            out->u.fill_rect.scissor_w = desc->clip.w;
            out->u.fill_rect.scissor_h = desc->clip.h;
            return true;
        }
    }

    case UITREE_EMIT_DEBUG_OVERLAY:
    {
        /* One display-list primitive per multi-step. The prims are already in
         * absolute screen pixels and carry their own scissor box, so this is a
         * straight field copy — no layout, no measurement, no allocation. */
        struct ToriDbgPrim const* prim;
        int font_id;

        if( !desc->debug_prims || frame->scrollbar_step >= desc->debug_prim_count )
            return false;
        prim = &desc->debug_prims[frame->scrollbar_step];

        if( prim->kind == TORIDBG_PRIM_TEXT )
        {
            if( !prim->text || prim->text[0] == '\0' )
                return false;
            if( prim->font_slot < 0 || prim->font_slot >= TORIDBG_FONT_SLOT_COUNT )
                return false;
            font_id = desc->debug_font_id[prim->font_slot];
            /* >= 0: scene font ids are cache ids, and 0 is a real one. A
             * negative id means the host never registered that baked face. */
            if( font_id < 0 )
                return false;
            out->kind = TORIRSRC_FONT;
            out->u.font.font_id = font_id;
            out->u.font.text = prim->text;
            out->u.font.x = prim->x;
            out->u.font.y = prim->y;
            out->u.font.color = (int)prim->color;
            out->u.font.shadowed = prim->shadowed;
            /* The overlay lays out from the baked advance tables and places a
             * baseline, matching ToriDraw2D_DrawString's `y -= line_height`. */
            out->u.font.baseline = prim->baseline;
            out->u.font.scissor_x = prim->clip.x;
            out->u.font.scissor_y = prim->clip.y;
            out->u.font.scissor_w = prim->clip.w;
            out->u.font.scissor_h = prim->clip.h;
            return true;
        }

        /* RECT. filled == 0 reaches ToriDraw2D_DrawRectOutline, which is what
         * makes a bordered background one primitive instead of four. */
        out->kind = TORIRSRC_FILL_RECT;
        out->u.fill_rect.x = prim->x;
        out->u.fill_rect.y = prim->y;
        out->u.fill_rect.w = prim->w;
        out->u.fill_rect.h = prim->h;
        /* Prims carry 0xRRGGBB, the same convention the UITree colour fields
         * use; the alpha byte is this layer's to supply. ToriDraw2D_FillRect
         * early-outs on alpha 0, so a raw copy here draws nothing at all. The
         * overlay is developer chrome and never translucent, hence trans 0. */
        out->u.fill_rect.argb = emit_color_argb((int)prim->color, 0);
        out->u.fill_rect.filled = prim->filled;
        out->u.fill_rect.scissor_x = prim->clip.x;
        out->u.fill_rect.scissor_y = prim->clip.y;
        out->u.fill_rect.scissor_w = prim->clip.w;
        out->u.fill_rect.scissor_h = prim->clip.h;
        return true;
    }

    case UITREE_EMIT_WORLD:
    case UITREE_EMIT_NONE:
        return false;
    }

    return false;
}

void
ToriRS_Frame_BuildWorldViewPort(
    struct UITreeEmitDesc const* desc,
    int canvas_w,
    int canvas_h,
    struct ToriDraw_ViewPort* out)
{
    assert(desc);
    assert(out);

    /* width/height are the *world box*, not the canvas (v1
     * GameRunescape_UpdateWorldViewport). The raster centers the projection at
     * width/2 while the perspective texture kernel anchors its uv basis at the
     * center of the clip rect; feeding the canvas size here moved the geometry
     * center to the canvas center and left the texture basis on the box center,
     * skewing every textured face by that difference. Only `stride` stays the
     * canvas width — the pixel buffer is still the full frame. */
    memset(out, 0, sizeof(*out));
    out->width = desc->w > 0 ? desc->w : canvas_w;
    out->height = desc->h > 0 ? desc->h : canvas_h;
    out->stride = canvas_w;
    out->x_center = desc->x + desc->w / 2;
    out->y_center = desc->y + desc->h / 2;
    /* Clip to the world box intersected with the node's inherited clip rect.
     * The emit walk hands every desc its *parent's* clip, which for the
     * screen-anchored world node is the whole canvas — a 2D draw is bounded by
     * its own w/h anyway, but 3D geometry fills whatever the scissor allows, so
     * the box has to be folded in here or the scene paints over the chrome (and
     * the texture kernel's clip-center basis drifts off x_center/y_center). */
    {
        int box_right = desc->x + out->width;
        int box_bottom = desc->y + out->height;
        int clip_right = desc->clip.x + desc->clip.w;
        int clip_bottom = desc->clip.y + desc->clip.h;

        out->clip_left = desc->x > desc->clip.x ? desc->x : desc->clip.x;
        out->clip_top = desc->y > desc->clip.y ? desc->y : desc->clip.y;
        out->clip_right = box_right < clip_right ? box_right : clip_right;
        out->clip_bottom = box_bottom < clip_bottom ? box_bottom : clip_bottom;
        /* The 3D pass sets its viewport straight from this desc, bypassing the
         * raster backend's per-command canvas clamp — a box anchored past a
         * screen edge has to be bounded here or it writes outside the buffer. */
        if( out->clip_left < 0 )
            out->clip_left = 0;
        if( out->clip_top < 0 )
            out->clip_top = 0;
        if( out->clip_right > canvas_w )
            out->clip_right = canvas_w;
        if( out->clip_bottom > canvas_h )
            out->clip_bottom = canvas_h;
        if( out->clip_right < out->clip_left )
            out->clip_right = out->clip_left;
        if( out->clip_bottom < out->clip_top )
            out->clip_bottom = out->clip_top;
    }
    /* TORIRS_VP_DEBUG=1: the raster/texture origin is the clip-rect center;
     * it must land on x_center/y_center or textured faces skew. */
    if( getenv("TORIRS_VP_DEBUG") )
        fprintf(
            stderr,
            "world_vp: desc=(%d,%d %dx%d) clip=(%d,%d %dx%d) canvas=%dx%d "
            "-> w=%d h=%d center=(%d,%d) raster_origin=(%d,%d)\n",
            desc->x, desc->y, desc->w, desc->h,
            desc->clip.x, desc->clip.y, desc->clip.w, desc->clip.h,
            canvas_w, canvas_h,
            out->width, out->height, out->x_center, out->y_center,
            out->clip_left + ((out->clip_right - out->clip_left) >> 1),
            out->clip_top + ((out->clip_bottom - out->clip_top) >> 1));
}

static void
build_begin_3d_from_world_emit(
    struct ToriRS_Frame* frame,
    struct UITreeEmitDesc const* desc,
    struct ToriRS_RenderCommand_Begin3D* out)
{
    assert(frame);
    assert(desc);
    assert(out);

    memset(out, 0, sizeof(*out));
    ToriRS_Frame_BuildWorldViewPort(desc, frame->canvas_w, frame->canvas_h, &out->view_port);

    if( frame->has_world_camera )
    {
        out->camera = frame->world_camera;
        out->camera_position.x = frame->cam_x;
        out->camera_position.y = frame->cam_y;
        out->camera_position.z = frame->cam_z;
    }
    else
    {
        out->camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
        out->camera.proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
        out->camera.near_plane_z = 50;
        out->camera.pitch = 128;
        out->camera.yaw = 0;
        out->camera.roll = 0;
    }
}

/*
 * TORIRS_EMIT_LOC=<loc_id>: trace the position of every draw command emitted
 * for that loc.
 *
 * This is the last place a loc's position is anything a human can read. After
 * this it is a camera-relative delta fed to the projection kernel, so a loc
 * that draws on the wrong tile and a loc that is *placed* on the wrong tile
 * look identical downstream. Printing the element's world position, the
 * camera, the delta the renderer receives, and the tile that world position
 * implies separates the two: `tile` disagreeing with `slot` means the build
 * placed it wrong, while both agreeing on a loc that visibly draws elsewhere
 * means the geometry (extent below) or the projection is responsible.
 *
 * Capped so a 200-frame run does not emit 200 identical lines per element.
 */
#define EMIT_LOC_DEBUG_MAX 12

static void
emit_loc_debug(
    struct ToriRS_Frame* frame,
    struct ToriDraw_SceneElement const* el,
    struct ToriDraw_Position const* rel,
    int element_id)
{
    static int budget = -1;
    static int want_loc = -1;
    struct WorldEntity_Scenery* scenery;

    if( budget < 0 )
    {
        char const* env = getenv("TORIRS_EMIT_LOC");
        want_loc = env ? (int)strtol(env, NULL, 0) : -1;
        budget = env ? EMIT_LOC_DEBUG_MAX : 0;
    }
    if( budget == 0 || want_loc < 0 )
        return;

    /* Negative-result cache. The lookup below is a linear walk of the scenery
     * pool, and it runs per element command per frame — thousands x thousands,
     * which drops the client to single-digit fps and makes the flag unusable
     * in the live session it exists for. An element's loc never changes, so a
     * miss stays a miss until the scene is rebuilt; keyed on load_seq so a
     * rebuild that recycles element ids cannot serve a stale answer. */
    {
        static int miss_key[4096];
        static unsigned miss_seq = (unsigned)-1;
        int const slot_idx = element_id & 4095;
        if( miss_seq != frame->world->load_seq )
        {
            memset(miss_key, -1, sizeof(miss_key));
            miss_seq = frame->world->load_seq;
        }
        if( miss_key[slot_idx] == element_id )
            return;

        scenery = World_SceneryGetByElementId(frame->world, element_id);
        if( !scenery || scenery->loc_id != want_loc )
        {
            miss_key[slot_idx] = element_id;
            return;
        }
    }

    /* One line per element until the loc actually moves. Without this a live
     * session spends the whole budget re-printing an unchanging position every
     * frame, and the interesting event — a position that CHANGES, e.g. across
     * a scene rebuild — scrolls past unnoticed. The camera is deliberately not
     * part of the key: it moves constantly and is not what is being watched. */
    {
#define EMIT_LOC_SEEN_MAX 64
        static int seen_element[EMIT_LOC_SEEN_MAX];
        static int seen_x[EMIT_LOC_SEEN_MAX];
        static int seen_z[EMIT_LOC_SEEN_MAX];
        static int seen_yaw[EMIT_LOC_SEEN_MAX];
        static int seen_count;
        for( int i = 0; i < seen_count; i++ )
        {
            if( seen_element[i] != element_id )
                continue;
            if( seen_x[i] == el->world_position.x && seen_z[i] == el->world_position.z &&
                seen_yaw[i] == el->world_position.yaw )
                return; /* unchanged since the last line for this element */
            seen_x[i] = el->world_position.x;
            seen_z[i] = el->world_position.z;
            seen_yaw[i] = el->world_position.yaw;
            goto emit;
        }
        /* Table full: stay silent rather than reprint an untracked element
         * every frame, which is what drowns the trace when a loc has more
         * instances on screen than the table holds. */
        if( seen_count >= EMIT_LOC_SEEN_MAX )
            return;
        seen_element[seen_count] = element_id;
        seen_x[seen_count] = el->world_position.x;
        seen_z[seen_count] = el->world_position.z;
        seen_yaw[seen_count] = el->world_position.yaw;
        seen_count++;
    }
emit:
    budget--;

    fprintf(
        stderr,
        "emit_loc %d el=%d: world=(%d,%d,%d) yaw=%d cam=(%d,%d,%d) rel=(%d,%d,%d) "
        "tile=(%d,%d) slot=(%d,%d) lvl=%d\n",
        want_loc,
        element_id,
        el->world_position.x,
        el->world_position.y,
        el->world_position.z,
        el->world_position.yaw,
        frame->cam_x,
        frame->cam_y,
        frame->cam_z,
        rel->x,
        rel->y,
        rel->z,
        /* Anchor tile, NOT world_position >> 7. Placement centres a model over
         * its draw footprint, so a 2x2 loc anchored on tile 13 sits at fine
         * 1792 whose raw tile is 14 — printing that reads as an off-by-one
         * against `slot` on every even-footprint loc in the scene. Back the
         * footprint out so `tile` and `slot` are directly comparable and only
         * differ when something is actually wrong. */
        (el->world_position.x - 64 * scenery->debug.draw_size_x) >> 7,
        (el->world_position.z - 64 * scenery->debug.draw_size_z) >> 7,
        scenery->grid_position.x,
        scenery->grid_position.z,
        scenery->grid_position.level);

    /* The geometry the projection will actually receive, in model-local units.
     * Reported here rather than from the build-time capture because anything
     * that rewrote vertices after the build (animation frames, contour ground)
     * is already applied by now. */
    if( el->model.kind == TORIDRAWMK_MODEL && el->model.u.model.model )
    {
        struct ToriDraw_Model const* m = el->model.u.model.model;
        if( m->vertex_count > 0 )
        {
            int xmin = m->vertices_x[0], xmax = m->vertices_x[0];
            int zmin = m->vertices_z[0], zmax = m->vertices_z[0];
            for( int v = 1; v < m->vertex_count; v++ )
            {
                if( m->vertices_x[v] < xmin ) xmin = m->vertices_x[v];
                if( m->vertices_x[v] > xmax ) xmax = m->vertices_x[v];
                if( m->vertices_z[v] < zmin ) zmin = m->vertices_z[v];
                if( m->vertices_z[v] > zmax ) zmax = m->vertices_z[v];
            }
            fprintf(
                stderr,
                "          extent x[%d..%d] z[%d..%d] -> world x[%d..%d] z[%d..%d] "
                "tiles x[%d..%d] z[%d..%d] (pre-yaw)\n",
                xmin, xmax, zmin, zmax,
                el->world_position.x + xmin,
                el->world_position.x + xmax,
                el->world_position.z + zmin,
                el->world_position.z + zmax,
                (el->world_position.x + xmin) >> 7,
                (el->world_position.x + xmax) >> 7,
                (el->world_position.z + zmin) >> 7,
                (el->world_position.z + zmax) >> 7);
        }
    }
}

/* Painter-command stepping (see torirs_frame.h). -2 = env not read yet. */
static int g_paint_limit = -2;
static int g_paint_limit_step = -2;
static long g_paint_limit_step_at = 0; /* TORIRS_PAINT_LIMIT_STEP_AT */
static long g_paint_limit_frame = 0;

static void
paint_limit_init(void)
{
    if( g_paint_limit == -2 )
    {
        char const* env = getenv("TORIRS_PAINT_LIMIT");
        g_paint_limit = (env && env[0]) ? (int)strtol(env, NULL, 0) : -1;
        if( g_paint_limit < -1 )
            g_paint_limit = -1;
    }
    if( g_paint_limit_step == -2 )
    {
        char const* env = getenv("TORIRS_PAINT_LIMIT_STEP");
        g_paint_limit_step = (env && env[0]) ? (int)strtol(env, NULL, 0) : 0;
        env = getenv("TORIRS_PAINT_LIMIT_STEP_AT");
        g_paint_limit_step_at = (env && env[0]) ? strtol(env, NULL, 0) : 0;
    }
}

void
ToriRS_Frame_PaintLimitSet(int limit)
{
    paint_limit_init();
    g_paint_limit = limit < 0 ? -1 : limit;
}

int
ToriRS_Frame_PaintLimitGet(void)
{
    paint_limit_init();
    return g_paint_limit;
}

/*
 * TORIRS_ONLY_LOC=<id>[,<id>...] — draw ONLY scenery whose loc id is listed,
 * and no terrain at all. Everything else in the world pass is dropped, so the
 * named locs stand alone against the cleared frame.
 *
 * A screenshot of a wall inside a cave cannot answer "is this one loc or three,
 * and are they moving together" — the neighbouring cliff, the floor and the
 * lava all read as part of the same rock, and an occluding loc in front hides
 * exactly the seam in question. Isolating the locs makes the seam the only
 * thing on screen. Built for the Inferno seal (30337 / 30338 / 30336 and their
 * state2 ids), where three separately-animated pieces have to look like one
 * wall face going down.
 *
 * Debug-only and off unless the variable is set: parsed once, then a linear
 * scan of at most ONLY_LOC_MAX ids per emitted element.
 */
#define ONLY_LOC_MAX 16
static int g_only_loc[ONLY_LOC_MAX];
static int g_only_loc_count = -1;

static void
only_loc_init(void)
{
    char const* env;
    char const* p;

    if( g_only_loc_count >= 0 )
        return;
    g_only_loc_count = 0;
    env = getenv("TORIRS_ONLY_LOC");
    if( !env )
        return;
    for( p = env; *p && g_only_loc_count < ONLY_LOC_MAX; )
    {
        char* end;
        long v = strtol(p, &end, 0);
        if( end == p )
            break;
        g_only_loc[g_only_loc_count++] = (int)v;
        p = (*end == ',') ? end + 1 : end;
    }
    fprintf(stderr, "only_loc: %d id(s), terrain suppressed\n", g_only_loc_count);
}

static bool
frame_only_loc_allows(
    struct ToriRS_Frame* frame,
    int cmd_kind,
    int element_id)
{
    struct WorldEntity_Scenery* sc;

    only_loc_init();
    if( g_only_loc_count <= 0 )
        return true;
    if( cmd_kind != PNTR_CMD_ELEMENT )
        return false; /* terrain and pick-only commands */
    sc = World_SceneryGetByElementId(frame->world, element_id);
    if( !sc )
        return false;
    for( int i = 0; i < g_only_loc_count; i++ )
        if( sc->loc_id == g_only_loc[i] )
            return true;
    return false;
}

static bool
try_emit_world_draw_model(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    int paint_limit;

    assert(frame);
    assert(out);

    if( !frame->world || !frame->painters || !frame->scene )
        return false;

    /* Truncate the stream at the cap: commands past it simply do not exist
     * this frame, so the raster shows the scene as of paint N. The index
     * counts every consumed command, drawn or dropped, so a cap position
     * names the same command on every frame of the same scene. */
    paint_limit = ToriRS_Frame_PaintLimitGet();

    while( frame->painters_index < frame->painters->command_count )
    {
        if( paint_limit >= 0 && frame->painters_index >= paint_limit )
            break;
        struct PaintersElementCommand* cmd =
            &frame->painters->commands[frame->painters_index++];
        int element_id = -1;
        struct ToriDraw_SceneElement* el;
        struct ToriDraw_Position rel;

        if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
            element_id = (int)cmd->_entity._bf_entity;
        else if( cmd->_bf_kind == PNTR_CMD_TERRAIN ||
                 cmd->_bf_kind == PNTR_CMD_TERRAIN_PICK_ONLY )
            element_id = World_TerrainElementAt(
                frame->world,
                (int)cmd->_terrain._bf_terrain_x,
                (int)cmd->_terrain._bf_terrain_z,
                (int)cmd->_terrain._bf_terrain_y);
        else
            continue;

        /* TORIRS_FRAME_DEBUG: a painted command that never becomes a draw. The
         * painter emitting a command is not the same as geometry reaching the
         * raster — a dead element id or a model-less element drops out here
         * silently, which looks identical to "the painter found nothing". */
        if( element_id < 0 || !ToriDraw_SceneElementIsLive(frame->scene, element_id) )
        {
            if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
                frame->dbg_drop_not_live++;
            continue;
        }

        el = ToriDraw_SceneElementGet(frame->scene, element_id);
        if( !el || el->model.kind == TORIDRAWMK_NONE )
        {
            if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
                frame->dbg_drop_no_model++;
            continue;
        }

        if( !frame_only_loc_allows(frame, cmd->_bf_kind, element_id) )
            continue;

        if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
            frame->dbg_emit_element++;
        else
            frame->dbg_emit_terrain++;

        rel = el->world_position;
        rel.x -= frame->cam_x;
        rel.y -= frame->cam_y;
        rel.z -= frame->cam_z;

        if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
            emit_loc_debug(frame, el, &rel, element_id);

        /* TORIRS_DRAW_ORDER=N: dump frame N's whole emit sequence — the
         * painter's back-to-front order — with each element resolved to a loc
         * id and footprint and each terrain command to its tile.
         *
         * The only tool that answers "what draws before what", which is the
         * question every draw-order bug actually poses and the one a screenshot
         * cannot settle. It found the recycled-scene-element defect (the same
         * loc emitted at two different depths, 30340 at order 482 and again at
         * 1341 in one frame) and it is what rules a draw-order explanation OUT,
         * which is just as useful: pair it with TORIRS_EMIT_LOC's model-extent
         * line to tell "drawn in the wrong order" from "the model overhangs its
         * footprint" from "the terrain is where it says it is". */
        {
            static long want = -2; static long seen = -1; static int in_f = 0;
            static int once = 0; static long last_i = -1; static int seq = 0;
            if( want == -2 ) { const char* e = getenv("TORIRS_DRAW_ORDER"); want = e ? strtol(e,NULL,0) : -1; }
            if( want >= 0 )
            {
                if( (long)frame->painters_index <= last_i )
                { seen++; in_f = (seen >= want) && !once; if( in_f ) once = 1; seq = 0; }
                last_i = (long)frame->painters_index;
                if( in_f )
                {
                    struct WorldEntity_Scenery* sc =
                        cmd->_bf_kind == PNTR_CMD_ELEMENT
                            ? World_SceneryGetByElementId(frame->world, element_id) : NULL;
                    /* cmd= is the painters_index of this command — the unit
                     * TORIRS_PAINT_LIMIT caps at, so a cap of cmd+1 draws up
                     * to and including this line. */
                    if( cmd->_bf_kind == PNTR_CMD_TERRAIN ||
                        cmd->_bf_kind == PNTR_CMD_TERRAIN_PICK_ONLY )
                        fprintf(stderr, "order %4d cmd=%4d TERRAIN%s tile=%d,%d L%d\n", seq++,
                                frame->painters_index - 1,
                                cmd->_bf_kind == PNTR_CMD_TERRAIN_PICK_ONLY ? "(pick)" : "",
                                (int)cmd->_terrain._bf_terrain_x, (int)cmd->_terrain._bf_terrain_z,
                                (int)cmd->_terrain._bf_terrain_y);
                    else if( sc )
                        fprintf(stderr,
                                "order %4d cmd=%4d LOC loc=%d slot=%d,%d L%d size=%dx%d\n", seq++,
                                frame->painters_index - 1,
                                sc->loc_id, sc->grid_position.x, sc->grid_position.z,
                                sc->grid_position.level,
                                sc->debug.draw_size_x, sc->debug.draw_size_z);
                    else
                    {
                        /* Everything that is not baked scenery: npcs, players,
                         * projectiles, spotanims. A draw-order question about a
                         * loc almost always has an npc on the other side of it,
                         * and "loc=-1" cannot answer which npc. */
                        struct WorldEntity_NPC* npc =
                            World_NpcGetByElementId(frame->world, element_id, NULL);
                        if( npc )
                            fprintf(stderr,
                                    "order %4d cmd=%4d NPC npc=%d tile=%d,%d L%d size=%d "
                                    "draw=%d,%d\n",
                                    seq++, frame->painters_index - 1, npc->npc_id,
                                    npc->grid_position.x, npc->grid_position.z,
                                    npc->grid_position.level, npc->size,
                                    (int)npc->draw_position.x, (int)npc->draw_position.z);
                        else
                            fprintf(stderr, "order %4d cmd=%4d ELEM element=%d at=%d,%d,%d\n",
                                    seq++, frame->painters_index - 1, element_id,
                                    el->world_position.x, el->world_position.y,
                                    el->world_position.z);
                    }
                }
            }
        }

        memset(out, 0, sizeof(*out));
        out->kind = TORIRSRC_DRAW_MODEL;
        out->u.model.model = el->model;
        out->u.model.position = rel;
        out->u.model.world_position = el->world_position;
        out->u.model.element_id = element_id;
        out->u.model.animation = el->animation;
        out->u.model.anim_frame = el->anim_frame;
        out->u.model.dynamic = el->dynamic;
        out->u.model.pickable = true;
        out->u.model.pick_aabb = el->pick_aabb;
        if( cmd->_bf_kind == PNTR_CMD_TERRAIN ||
            cmd->_bf_kind == PNTR_CMD_TERRAIN_PICK_ONLY )
        {
            out->u.model.pick_terrain = true;
            out->u.model.pick_only = (cmd->_bf_kind == PNTR_CMD_TERRAIN_PICK_ONLY);
            out->u.model.pick_tile_x = (int)cmd->_terrain._bf_terrain_x;
            out->u.model.pick_tile_z = (int)cmd->_terrain._bf_terrain_z;
            out->u.model.pick_tile_level = (int)cmd->_terrain._bf_terrain_y;
        }
        else
        {
            out->u.model.pick_tile_x = -1;
            out->u.model.pick_tile_z = -1;
            out->u.model.pick_tile_level = -1;
        }
        return true;
    }

    return false;
}

void
ToriRS_FrameInit(struct ToriRS_Frame* frame)
{
    assert(frame);
    memset(frame, 0, sizeof(*frame));
}

void
ToriRS_FrameSetEmit(
    struct ToriRS_Frame* frame,
    struct UITreeEmitDesc const* cmds,
    int count)
{
    assert(frame);
    if( count > 0 )
        assert(cmds);
    frame->emit_cmds = cmds;
    frame->emit_count = count;
}

void
ToriRS_FrameSetEmitBuffer(
    struct ToriRS_Frame* frame,
    struct UITreeEmitBuffer const* buf)
{
    assert(frame);
    assert(buf);
    ToriRS_FrameSetEmit(frame, buf->cmds, buf->count);
}

void
ToriRS_FrameSetScene(
    struct ToriRS_Frame* frame,
    struct ToriDraw_Scene* scene)
{
    assert(frame);
    assert(scene);
    frame->scene = scene;
}

void
ToriRS_FrameSetCanvas(
    struct ToriRS_Frame* frame,
    int width,
    int height)
{
    assert(frame);
    assert(width > 0 && height > 0);
    frame->canvas_w = width;
    frame->canvas_h = height;
}

void
ToriRS_FrameSetWorld(
    struct ToriRS_Frame* frame,
    struct World* world,
    struct PaintersBuffer* painters,
    struct ToriDraw_Camera const* camera,
    int cam_x,
    int cam_y,
    int cam_z)
{
    assert(frame);
    frame->world = world;
    frame->painters = painters;
    frame->cam_x = cam_x;
    frame->cam_y = cam_y;
    frame->cam_z = cam_z;
    if( camera )
    {
        frame->world_camera = *camera;
        frame->has_world_camera = true;
    }
    else
    {
        memset(&frame->world_camera, 0, sizeof(frame->world_camera));
        frame->has_world_camera = false;
    }
}

void
ToriRS_FrameBegin(struct ToriRS_Frame* frame)
{
    assert(frame);
    assert(frame->scene);
    assert(frame->canvas_w > 0 && frame->canvas_h > 0);
    /* TORIRS_PAINT_LIMIT_STEP: advance the cap once per frame (from frame
     * TORIRS_PAINT_LIMIT_STEP_AT on, so a login/cinematic prefix does not
     * consume the sweep), so a TORIRS_BMP_SERIES run flips through the paint
     * order. */
    paint_limit_init();
    g_paint_limit_frame++;
    if( g_paint_limit >= 0 && g_paint_limit_step > 0 &&
        g_paint_limit_frame > g_paint_limit_step_at )
        g_paint_limit += g_paint_limit_step;
    frame->pass = TORIRS_FRAME_PASS_NONE;
    frame->emit_index = 0;
    frame->painters_index = 0;
    frame->scrollbar_step = 0;
    frame->event_index = 0;
    frame->in_world = false;
    frame->world_begun = false;
    frame->has_queued = false;
    memset(&frame->queued, 0, sizeof(frame->queued));
    memset(&frame->pending_begin_3d, 0, sizeof(frame->pending_begin_3d));
}

bool
ToriRS_FrameNextCommand(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(out);
    assert(frame->scene);

    if( frame_take_queued(frame, out) )
        return true;

    /* Drain ordered scene resource events before any DRAW_MODEL so retained
     * model/pose buffers and asynchronous textures are ready for the pass. */
    if( frame_take_scene_event(frame, out) )
        return true;

again:
    /* Finish / drain world 3D pass. */
    if( frame->in_world )
    {
        if( !frame->world_begun )
        {
            memset(out, 0, sizeof(*out));
            out->kind = TORIRSRC_BEGIN_3D;
            out->u.begin_3d = frame->pending_begin_3d;
            frame->pass = TORIRS_FRAME_PASS_3D;
            frame->world_begun = true;
            return true;
        }

        if( try_emit_world_draw_model(frame, out) )
            return true;

        frame_emit_end_3d(frame, out);
        return true;
    }

    while( frame->emit_index < frame->emit_count )
    {
        struct UITreeEmitDesc const* desc = &frame->emit_cmds[frame->emit_index];
        int is_scrollbar = desc->kind == UITREE_EMIT_SCROLLBAR_V ||
                           desc->kind == UITREE_EMIT_SCROLLBAR_H;
        int sb_steps = desc->kind == UITREE_EMIT_SCROLLBAR_V
                           ? UITREE_SCROLLBAR_V_DRAW_STEPS
                           : UITREE_SCROLLBAR_H_DRAW_STEPS;
        /* Minimap descs also multi-step: step 0 = map blit, 1..N = overlay
         * dots (same scrollbar_step counter — only one desc steps at a time). */
        if( desc->kind == UITREE_EMIT_MINIMAP && desc->minimap_dot_count > 0 )
        {
            is_scrollbar = 1;
            sb_steps = 1 + desc->minimap_dot_count;
        }
        /* Entity overlays step one primitive at a time (step 0 is already a
         * primitive here, unlike MINIMAP where step 0 is the map blit). */
        if( desc->kind == UITREE_EMIT_ENTITY_OVERLAY && desc->entity_overlay_count > 0 )
        {
            is_scrollbar = 1;
            sb_steps = desc->entity_overlay_count;
        }
        /* World map: step 0 is the background, then one blit per baked region. */
        if( desc->kind == UITREE_EMIT_WORLDMAP )
        {
            is_scrollbar = 1;
            sb_steps = 1 + desc->worldmap_tile_count;
        }
        /* Debug overlay: one step per display-list primitive. Stepping the
         * host's array in place is the reason the overlay's whole per-frame
         * cost is a pointer copy — an emit desc per prim would mean copying
         * ~300 bytes each, every frame, for a list that rarely changes. */
        if( desc->kind == UITREE_EMIT_DEBUG_OVERLAY && desc->debug_prim_count > 0 )
        {
            is_scrollbar = 1;
            sb_steps = desc->debug_prim_count;
        }

        if( desc->kind == UITREE_EMIT_WORLD )
        {
            frame->emit_index++;
            frame->scrollbar_step = 0;
            build_begin_3d_from_world_emit(frame, desc, &frame->pending_begin_3d);
            frame->in_world = true;
            frame->world_begun = false;
            frame->painters_index = 0;

            if( frame->pass == TORIRS_FRAME_PASS_2D )
            {
                frame_emit_end_2d(frame, out);
                return true;
            }
            goto again;
        }

        {
            struct ToriRS_RenderCommand draw;
            if( !translate_ui_cmd(frame, desc, &draw) )
            {
                if( is_scrollbar )
                {
                    frame->scrollbar_step++;
                    if( frame->scrollbar_step >= sb_steps )
                    {
                        frame->emit_index++;
                        frame->scrollbar_step = 0;
                    }
                    continue;
                }
                frame->emit_index++;
                frame->scrollbar_step = 0;
                continue;
            }

            if( is_scrollbar )
            {
                frame->scrollbar_step++;
                if( frame->scrollbar_step >= sb_steps )
                {
                    frame->emit_index++;
                    frame->scrollbar_step = 0;
                }
            }
            else
            {
                frame->emit_index++;
                frame->scrollbar_step = 0;
            }

            if( frame->pass != TORIRS_FRAME_PASS_2D )
            {
                frame_queue(frame, &draw);
                frame_emit_begin_2d(frame, out);
                return true;
            }

            *out = draw;
            return true;
        }
    }

    if( frame->pass == TORIRS_FRAME_PASS_2D )
    {
        frame_emit_end_2d(frame, out);
        return true;
    }
    if( frame->pass == TORIRS_FRAME_PASS_3D )
    {
        frame_emit_end_3d(frame, out);
        return true;
    }

    return false;
}

void
ToriRS_FrameEnd(struct ToriRS_Frame* frame)
{
    assert(frame);
    if( frame->scene )
        ToriDraw_SceneFrameEnd(frame->scene);
    frame->pass = TORIRS_FRAME_PASS_NONE;
    frame->emit_index = 0;
    frame->painters_index = 0;
    frame->scrollbar_step = 0;
    frame->event_index = 0;
    frame->in_world = false;
    frame->world_begun = false;
    frame->has_queued = false;
}
