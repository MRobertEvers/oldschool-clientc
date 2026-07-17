#include "uitree_emit.h"

#include "uitree_layout.h"

#include <assert.h>
#include <stdbool.h>
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
