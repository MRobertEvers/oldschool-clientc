#include "uitree_emit.h"

#include "uitree_layout.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool
UITree_EmitFill(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeComponent const* component,
    int32_t node_index,
    struct UITreeEmitDesc* out)
{
    assert(tree);
    assert(component);
    assert(out);

    memset(out, 0, sizeof(*out));

    if( host )
    {
        if( !UITree_ComponentShouldEmit(component, host) )
            return false;
    }

    int x = 0, y = 0, w = 0, h = 0;
    UITree_LayoutGetBounds(&component->position, &x, &y, &w, &h);

    out->node_index = node_index;
    out->component_id = component->component_id;
    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;

    switch( component->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_RS_GRAPHIC:
        out->kind = UITREE_EMIT_SPRITE;
        if( component->type == UIELEM_BUILTIN_SPRITE )
        {
            out->scene_id = component->u.sprite.scene_id;
            out->atlas_index = component->u.sprite.atlas_index;
        }
        else
        {
            out->scene_id = component->u.rs_graphic.scene_id;
            out->atlas_index = component->u.rs_graphic.atlas_index;
            if( component->u.rs_graphic.graphic_hitbox_only )
                return false;
        }
        if( out->scene_id <= 0 )
            return false;
        out->rotation = UITree_ComponentSpriteRotation(component, host);
        return true;

    case UIELEM_RS_TEXT:
        if( !component->u.rs_text.text || component->u.rs_text.text[0] == '\0' )
            return false;
        out->kind = UITREE_EMIT_TEXT;
        out->text = component->u.rs_text.text;
        out->font_id = component->u.rs_text.font_id;
        out->color = component->u.rs_text.color;
        out->text_center = component->u.rs_text.center;
        out->text_shadowed = component->u.rs_text.shadowed;
        out->text_line_height = component->u.rs_text.line_height;
        return true;

    case UIELEM_RS_RECT:
        out->kind = UITREE_EMIT_RECT;
        out->color = component->u.rs_rect.color;
        out->filled = component->u.rs_rect.filled;
        return true;

    case UIELEM_RS_LINE:
        out->kind = UITREE_EMIT_LINE;
        out->color = component->u.rs_line.color;
        return true;

    case UIELEM_RS_MODEL:
        if( component->u.rs_model.gamecache_model_id < 0 )
            return false;
        out->kind = UITREE_EMIT_MODEL;
        out->model_id = component->u.rs_model.gamecache_model_id;
        out->model_zoom = component->u.rs_model.zoom;
        out->model_xan = component->u.rs_model.xan;
        out->model_yan = component->u.rs_model.yan;
        return true;

    case UIELEM_CC_OBJ:
        if( component->u.cc_obj.obj_id <= 0 )
            return false;
        out->kind = UITREE_EMIT_CC_OBJ;
        out->obj_id = component->u.cc_obj.obj_id;
        out->obj_count = component->u.cc_obj.obj_count;
        out->scene_id = component->u.cc_obj.scene_id;
        out->atlas_index = component->u.cc_obj.atlas_index;
        return true;

    case UIELEM_INV_SLOT:
        out->kind = UITREE_EMIT_INV_SLOT;
        out->inv_source_id = component->u.inv_slot.inv_source_id;
        out->inv_slot = component->u.inv_slot.slot;
        out->scene_id = component->item_scene_id;
        out->atlas_index = component->item_atlas_index;
        out->obj_id = component->item_id;
        out->obj_count = component->item_count;
        return true;

    case UIELEM_BUILTIN_WORLD:
        out->kind = UITREE_EMIT_WORLD;
        return true;

    case UIELEM_BUILTIN_MINIMAP:
        out->kind = UITREE_EMIT_MINIMAP;
        out->scene_id = component->u.minimap.scene_id;
        return true;

    case UIELEM_BUILTIN_COMPASS:
        out->kind = UITREE_EMIT_COMPASS;
        out->scene_id = component->u.sprite.scene_id;
        out->atlas_index = component->u.sprite.atlas_index;
        out->rotation = UITree_ComponentSpriteRotation(component, host);
        return true;

    case UIELEM_RS_LAYER:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
    case UIELEM_BUILTIN_CHAT_BUTTON:
    case UIELEM_BUILTIN_REDSTONE_TAB:
    case UIELEM_BUILTIN_TAB_ICONS:
    case UIELEM_BUILTIN_CROSS:
    case UIELEM_BUILTIN_MINIMENU:
    case UIELEM_INV_GRID:
    case UIELEM_RS_INV_TEXT:
        return false;
    }

    return false;
}

void
UITree_EmitBufferInit(struct UITreeEmitBuffer* buf)
{
    assert(buf);
    memset(buf, 0, sizeof(*buf));
}

void
UITree_EmitBufferFree(struct UITreeEmitBuffer* buf)
{
    if( !buf )
        return;
    free(buf->cmds);
    memset(buf, 0, sizeof(*buf));
}

static void
emit_buffer_append(
    struct UITreeEmitBuffer* buf,
    struct UITreeEmitDesc const* desc)
{
    assert(buf && desc);
    if( buf->count >= buf->cap )
    {
        int n = buf->cap == 0 ? 64 : buf->cap * 2;
        struct UITreeEmitDesc* grown =
            realloc(buf->cmds, (size_t)n * sizeof(struct UITreeEmitDesc));
        assert(grown);
        buf->cmds = grown;
        buf->cap = n;
    }
    buf->cmds[buf->count++] = *desc;
}

static int
clip_intersect(
    struct UITreeEmitClip* dst,
    struct UITreeEmitClip const* a,
    int x,
    int y,
    int w,
    int h)
{
    int x0 = a->x;
    int y0 = a->y;
    int x1 = a->x + a->w;
    int y1 = a->y + a->h;
    int bx0 = x;
    int by0 = y;
    int bx1 = x + w;
    int by1 = y + h;
    if( bx0 > x0 )
        x0 = bx0;
    if( by0 > y0 )
        y0 = by0;
    if( bx1 < x1 )
        x1 = bx1;
    if( by1 < y1 )
        y1 = by1;
    if( x0 >= x1 || y0 >= y1 )
    {
        dst->x = dst->y = dst->w = dst->h = 0;
        return 0;
    }
    dst->x = x0;
    dst->y = y0;
    dst->w = x1 - x0;
    dst->h = y1 - y0;
    return 1;
}

static int
component_is_layer_clip(struct UITreeComponent const* c)
{
    if( !c )
        return 0;
    switch( c->type )
    {
    case UIELEM_RS_LAYER:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
    case UIELEM_INV_GRID:
        return 1;
    default:
        return 0;
    }
}

static int
emit_kind_is_text(enum UITreeEmitKind kind)
{
    return kind == UITREE_EMIT_TEXT;
}

static void
emit_walk_node(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip,
    int text_pass)
{
    struct UITreeComponent const* c;
    struct UITreeEmitDesc desc;
    struct UITreeEmitClip layer_clip;
    struct UITreeEmitClip const* child_clip;
    int x = 0, y = 0, w = 0, h = 0;
    int32_t child;

    assert(tree && out && parent_clip);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;

    c = &tree->components[idx];
    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);

    child_clip = parent_clip;
    if( component_is_layer_clip(c) && w > 0 && h > 0 )
    {
        if( clip_intersect(&layer_clip, parent_clip, x, y, w, h) )
            child_clip = &layer_clip;
        /* If the layer itself is outside the canvas, still walk children with the
         * parent clip — IF3 roots can sit off-origin while children are in view. */
    }

    if( UITree_EmitFill(tree, host, c, idx, &desc) )
    {
        int is_text = emit_kind_is_text(desc.kind);
        if( (text_pass && is_text) || (!text_pass && !is_text) )
        {
            if( desc.kind != UITREE_EMIT_WORLD && desc.kind != UITREE_EMIT_MINIMAP &&
                desc.kind != UITREE_EMIT_COMPASS )
            {
                desc.clip = *parent_clip;
                emit_buffer_append(out, &desc);
            }
        }
    }

    for( child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
        emit_walk_node(tree, host, out, child, child_clip, text_pass);
}

static void
emit_walk_pass(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int text_pass,
    int canvas_w,
    int canvas_h)
{
    struct UITreeEmitClip root_clip;
    int32_t root;

    assert(tree && out);
    root_clip.x = 0;
    root_clip.y = 0;
    root_clip.w = canvas_w;
    root_clip.h = canvas_h;

    for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
        emit_walk_node(tree, host, out, root, &root_clip, text_pass);
}

void
UITree_EmitWalk(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out)
{
    assert(tree);
    assert(out);
    emit_walk_pass(tree, host, out, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    emit_walk_pass(tree, host, out, 1, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}
