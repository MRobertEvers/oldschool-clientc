#include "render/torirs_frame.h"

#include "painters/painters.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_scroll.h"
#include "world/world.h"

#include "toridraw_scene.h"

#include <assert.h>
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

static void
sprite_cmd(
    struct ToriRS_RenderCommand* out,
    int scene_id,
    int atlas,
    int x,
    int y,
    int w,
    int h,
    int rotation,
    struct UITreeEmitClip const* clip)
{
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
    out->u.sprite.rotation = rotation;
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
        out->u.sprite.rotation = desc->rotation;
        out->u.sprite.outline = desc->outline;
        out->u.sprite.graphic_shadow = desc->graphic_shadow;
        out->u.sprite.trans = desc->trans;
        out->u.sprite.if3 = desc->if3;
        out->u.sprite.tiled = desc->tiled;
        out->u.sprite.flip_h = desc->flip_h;
        out->u.sprite.flip_v = desc->flip_v;
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
        if( hnd.kind == TORIDRAWMK_NONE )
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
        return true;
    }

    case UITREE_EMIT_SCROLLBAR_V:
        return translate_scrollbar_v_step(desc, frame->scrollbar_step, out);

    case UITREE_EMIT_SCROLLBAR_H:
        return translate_scrollbar_h_step(desc, frame->scrollbar_step, out);

    case UITREE_EMIT_COMPASS:
        /* Native-size blit rotated by camera yaw (desc->rotation), like the
         * scrollbar arrows: chrome, never IF3-stretched. */
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
        out->u.sprite.rotation = desc->rotation;
        out->u.sprite.trans = desc->trans;
        out->u.sprite.if3 = 0;
        return true;

    case UITREE_EMIT_MINIMAP:
        /* Minimap terrain is rendered by the minimap module, not a 2D blit. */
        return false;

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

    memset(out, 0, sizeof(*out));
    out->width = canvas_w;
    out->height = canvas_h;
    out->stride = canvas_w;
    out->x_center = desc->x + desc->w / 2;
    out->y_center = desc->y + desc->h / 2;
    out->clip_left = desc->clip.x;
    out->clip_top = desc->clip.y;
    out->clip_right = desc->clip.x + desc->clip.w;
    out->clip_bottom = desc->clip.y + desc->clip.h;
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
        out->camera.fov_rpi2048 = 512;
        out->camera.near_plane_z = 50;
        out->camera.pitch = 128;
        out->camera.yaw = 0;
        out->camera.roll = 0;
    }
}

static bool
try_emit_world_draw_model(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out)
{
    assert(frame);
    assert(out);

    if( !frame->world || !frame->painters || !frame->scene )
        return false;

    while( frame->painters_index < frame->painters->command_count )
    {
        struct PaintersElementCommand* cmd =
            &frame->painters->commands[frame->painters_index++];
        int element_id = -1;
        struct ToriDraw_SceneElement* el;
        struct ToriDraw_Position rel;

        if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
            element_id = (int)cmd->_entity._bf_entity;
        else if( cmd->_bf_kind == PNTR_CMD_TERRAIN )
            element_id = World_TerrainElementAt(
                frame->world,
                (int)cmd->_terrain._bf_terrain_x,
                (int)cmd->_terrain._bf_terrain_z,
                (int)cmd->_terrain._bf_terrain_y);
        else
            continue;

        if( element_id < 0 || !ToriDraw_SceneElementIsLive(frame->scene, element_id) )
            continue;

        el = ToriDraw_SceneElementGet(frame->scene, element_id);
        if( !el || el->model.kind == TORIDRAWMK_NONE )
            continue;

        rel = el->world_position;
        rel.x -= frame->cam_x;
        rel.y -= frame->cam_y;
        rel.z -= frame->cam_z;

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
        if( cmd->_bf_kind == PNTR_CMD_TERRAIN )
        {
            out->u.model.pick_terrain = true;
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
    frame->pass = TORIRS_FRAME_PASS_NONE;
    frame->emit_index = 0;
    frame->painters_index = 0;
    frame->scrollbar_step = 0;
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
    frame->pass = TORIRS_FRAME_PASS_NONE;
    frame->emit_index = 0;
    frame->painters_index = 0;
    frame->scrollbar_step = 0;
    frame->in_world = false;
    frame->world_begun = false;
    frame->has_queued = false;
}
