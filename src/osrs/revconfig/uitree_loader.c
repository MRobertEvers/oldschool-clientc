#include "uitree_loader.h"

#include "graphics/dashmap.h"
#include "osrs/chat.h"
#include "osrs/game.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/uitree.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal (full) definition of UITreeLoader ─────────────────────────────── */

#define MAX_LAYOUT_ENTRIES 128

enum SpriteLoad_AtlasMode
{
    SPRITELOAD_ATLAS_MODE_INDEX,
    SPRITELOAD_ATLAS_MODE_COUNT,
};

enum LoadKind
{
    LOAD_KIND_NONE,
    LOAD_KIND_SPRITE,
    LOAD_KIND_COMPONENT,
    LOAD_KIND_LAYOUT,
    LOAD_KIND_INV
};

struct SpriteLoad
{
    char name[64];
    char table[64];
    char archive[64];
    char container[64];
    char index_filename[64];
    char data_filename[64];
    char format[16];

    enum SpriteLoad_AtlasMode atlas_mode;
    int atlas_index;
    int atlas_count;

    char transforms[5][32];
    int transform_count;

    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct ComponentLoad
{
    char name[64];
    char type[64];
    char sprite[64];
    char sprite_active[64];
    int def_x;
    int def_y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int tabno;
    int componentno;
    char inv[64];
    char paint_levels[64];
    char font[64];
    struct ChatUILayout chat_geom;
    uint16_t chat_geom_mask;
    char minimenu_region_viewport[64];
    char minimenu_region_sidebar[64];
    char minimenu_region_chat[64];
    char minimenu_place_viewport_max[64];
    char minimenu_place_sidebar_max[64];
    char minimenu_place_chat_max[64];
    int crosshair_hotspot_offset;
    uint8_t crosshair_hotspot_offset_set;
};

struct LayoutItem
{
    char component[64];
    int x;
    int y;
    int flags;
    int top;
    int right;
    int bottom;
    int left;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    uint8_t always_dirty;
};

struct LayoutLoad
{
    char name[64];
    struct LayoutItem entries[MAX_LAYOUT_ENTRIES];
    int entry_count;
};

struct InvLoad
{
    char name[64];
    int item_ids[UI_INVENTORY_MAX_ITEMS];
    int item_count;
};

struct CurrentLoad
{
    enum LoadKind kind;
    union
    {
        struct SpriteLoad sprite;
        struct ComponentLoad component;
        struct LayoutLoad layout;
        struct InvLoad inv;
    } u;
};

struct UITreeLoader
{
    enum UITreeLoaderStatus status;
    struct UITreeLoaderAssetRequest pending_assets[UITREE_LOADER_MAX_PENDING_ASSETS];
    int pending_asset_count;

    /* Borrowed references — not owned, must outlive the loader. */
    struct UITree* ui_ref;

    /* Iteration state. */
    struct CurrentLoad load; /* accumulator for the current INI item */
    int field_index;
};

/* ── Allocation ─────────────────────────────────────────────────────────────── */

struct UITreeLoader*
uitree_loader_new(struct UITree* ui_ref)
{
    struct UITreeLoader* loader = calloc(1, sizeof(*loader));
    if( !loader )
        return NULL;

    loader->status = UITREE_LOADER_RUNNING;
    loader->ui_ref = ui_ref;
    loader->field_index = 0;

    /* Reset the UITree to empty before starting. */
    if( ui_ref )
    {
        ui_ref->component_count = 0;
        ui_ref->root_index = -1;
    }

    return loader;
}

static uint32_t
uitree_loader_parse_item_kind(const char* str)
{
    if( !str )
        return LOAD_KIND_NONE;
    if( strcmp(str, "sprite") == 0 )
        return LOAD_KIND_SPRITE;
    if( strcmp(str, "component") == 0 )
        return LOAD_KIND_COMPONENT;
    if( strcmp(str, "layout") == 0 )
        return LOAD_KIND_LAYOUT;
    if( strcmp(str, "inv") == 0 )
        return LOAD_KIND_INV;
    return LOAD_KIND_NONE;
}

/* ── RevConfig field handlers (uitree_loader_step) ─────────────────────────── */

static int
sprite_placeholder_atlas_index(struct UITreeLoader* loader)
{
    struct SpriteLoad* s = &loader->load.u.sprite;
    if( s->atlas_mode == SPRITELOAD_ATLAS_MODE_COUNT )
        return 0;
    return s->atlas_index;
}

/* Reserve next pending slot (cleared, kind + default bindings). Returns index or -1 if full.
 * Call uitree_loader_pending_commit() after filling `u` and any UITree work between. */
static int
uitree_loader_pending_prepare(
    struct UITreeLoader* loader,
    enum UITreeLoaderAssetKind kind)
{
    if( loader->pending_asset_count >= UITREE_LOADER_MAX_PENDING_ASSETS )
        return -1;
    int slot = loader->pending_asset_count;
    struct UITreeLoaderAssetRequest* req = &loader->pending_assets[slot];
    memset(req, 0, sizeof(*req));
    req->kind = kind;
    req->resolved = false;
    return slot;
}

static void
uitree_loader_pending_commit(struct UITreeLoader* loader)
{
    loader->pending_asset_count++;
}

static enum UITreeLoaderStatus
push_pending_model(
    struct UITreeLoader* loader,
    int model_id)
{
    int slot = uitree_loader_pending_prepare(loader, UITREE_ASSET_MODEL);
    if( slot < 0 )
        return UITREE_LOADER_ERROR;
    loader->pending_assets[slot].u.model.model_id = model_id;
    uitree_loader_pending_commit(loader);
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
push_pending_font(
    struct UITreeLoader* loader,
    const char* logical_name)
{
    if( !logical_name )
        return UITREE_LOADER_ERROR;
    int slot = uitree_loader_pending_prepare(loader, UITREE_ASSET_FONT);
    if( slot < 0 )
        return UITREE_LOADER_ERROR;
    strncpy(
        loader->pending_assets[slot].u.font.name,
        logical_name,
        sizeof(loader->pending_assets[slot].u.font.name) - 1);
    loader->pending_assets[slot].u.font.name[sizeof(loader->pending_assets[slot].u.font.name) - 1] =
        '\0';
    uitree_loader_pending_commit(loader);
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
push_pending_rs_component(
    struct UITreeLoader* loader,
    int component_id)
{
    if( component_id < 0 )
        return UITREE_LOADER_ERROR;
    int slot = uitree_loader_pending_prepare(loader, UITREE_ASSET_RS_COMPONENT);
    if( slot < 0 )
        return UITREE_LOADER_ERROR;
    struct UITreeLoaderAssetRequest* req = &loader->pending_assets[slot];
    req->u.rs_component.component_id = component_id;
    uitree_loader_pending_commit(loader);
    return UITREE_LOADER_RUNNING;
}

static void
uitree_loader_pending_set_sprite_name(
    struct UITreeLoaderAssetRequest* req,
    const char* logical_name)
{
    strncpy(req->u.sprite.name, logical_name, sizeof(req->u.sprite.name) - 1);
    req->u.sprite.name[sizeof(req->u.sprite.name) - 1] = '\0';
}

static enum UITreeLoaderStatus
on_sprite_done(struct UITreeLoader* loader)
{
    if( loader->load.u.sprite.name[0] == '\0' )
        return UITREE_LOADER_ERROR;

    int slot = uitree_loader_pending_prepare(loader, UITREE_ASSET_SPRITE);
    if( slot < 0 )
        return UITREE_LOADER_ERROR;
    uitree_loader_pending_set_sprite_name(
        &loader->pending_assets[slot], loader->load.u.sprite.name);

    int atlas = sprite_placeholder_atlas_index(loader);
    int32_t uitree_idx = -1;
    if( loader->ui_ref )
    {
        uitree_idx = uitree_push_sprite_xy(loader->ui_ref, -1, -1, atlas, 0, 0, 1, 1);
    }
    (void)uitree_idx;

    uitree_loader_pending_commit(loader);
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_layer_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    struct UITreeLoaderAssetRequest_RSComponent* component = &req->u.rs_component;

    int32_t lid = uitree_push_rs_layer(
        loader->ui_ref,
        0,
        component->resolve.id,
        component->resolve.scroll,
        component->resolve.hide ? 1 : 0,
        component->resolve.x,
        component->resolve.y,
        component->resolve.width,
        component->resolve.height);

    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_inv_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_rect_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_text_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_graphic_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_model_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_inv_text_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    return UITREE_LOADER_RUNNING;
}

static enum UITreeLoaderStatus
on_rs_component_done(
    struct UITreeLoader* loader,
    struct UITreeLoaderAssetRequest* req)
{
    switch( req->u.rs_component.resolve.type )
    {
    case UITREE_LOADER_RS_COMPONENT_TYPE_LAYER:
        return on_rs_component_layer_done(loader, req);
        break;
    case UITREE_LOADER_RS_COMPONENT_TYPE_INV:
        return on_rs_component_inv_done(loader, req);
    case UITREE_LOADER_RS_COMPONENT_TYPE_RECT:
        return on_rs_component_rect_done(loader, req);
    case UITREE_LOADER_RS_COMPONENT_TYPE_TEXT:
        return on_rs_component_text_done(loader, req);
    case UITREE_LOADER_RS_COMPONENT_TYPE_GRAPHIC:
        return on_rs_component_graphic_done(loader, req);
    case UITREE_LOADER_RS_COMPONENT_TYPE_MODEL:
        return on_rs_component_model_done(loader, req);
    case UITREE_LOADER_RS_COMPONENT_TYPE_INV_TEXT:
        return on_rs_component_inv_text_done(loader, req);
    default:
    {
        assert(0 && "Invalid RS component type");
    }
    }

    return UITREE_LOADER_ERROR;
}

static enum UITreeLoaderStatus
on_layout_done(struct UITreeLoader* loader)
{
    return UITREE_LOADER_NEEDS_ASSET;
}

static enum UITreeLoaderStatus
on_inv_done(struct UITreeLoader* loader)
{
    return UITREE_LOADER_NEEDS_ASSET;
}

static void
on_rcfield_itemtype(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    uint32_t k = uitree_loader_parse_item_kind(field->value);

    loader->load.kind = (enum LoadKind)k;
    if( loader->load.kind == LOAD_KIND_INV )
        memset(&loader->load.u.inv, 0, sizeof(loader->load.u.inv));
}

static void
on_rcfield_itemname(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(loader->load.u.sprite.name, field->value, sizeof(loader->load.u.sprite.name) - 1);
        break;
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.name, field->value, sizeof(loader->load.u.component.name) - 1);
        break;
    case LOAD_KIND_LAYOUT:
        strncpy(loader->load.u.layout.name, field->value, sizeof(loader->load.u.layout.name) - 1);
        break;
    case LOAD_KIND_INV:
        strncpy(loader->load.u.inv.name, field->value, sizeof(loader->load.u.inv.name) - 1);
        break;
    default:
        break;
    }
}

static enum UITreeLoaderStatus
on_rcfield_itemdone(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    (void)field;
    enum UITreeLoaderStatus status = UITREE_LOADER_RUNNING;
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        status = on_sprite_done(loader);
        break;
    case LOAD_KIND_COMPONENT:
        status = on_component_done(loader);
        break;
    case LOAD_KIND_LAYOUT:
        status = on_layout_done(loader);
        break;
    case LOAD_KIND_INV:
        status = on_inv_done(loader);
        break;
    default:
        status = UITREE_LOADER_ERROR;
        break;
    }

    if( status == UITREE_LOADER_ERROR )
    {
        loader->load.kind = LOAD_KIND_NONE;
        memset(&loader->load, 0, sizeof(loader->load));
        loader->status = UITREE_LOADER_ERROR;
        return UITREE_LOADER_ERROR;
    }

    if( status == UITREE_LOADER_NEEDS_ASSET )
    {
        loader->load.kind = LOAD_KIND_NONE;
        memset(&loader->load, 0, sizeof(loader->load));
        loader->status = UITREE_LOADER_NEEDS_ASSET;
        return UITREE_LOADER_NEEDS_ASSET;
    }

    loader->load.kind = LOAD_KIND_NONE;
    memset(&loader->load, 0, sizeof(loader->load));

    loader->status = UITREE_LOADER_RUNNING;
    return UITREE_LOADER_RUNNING;
}

static void
on_rcfield_cache_table(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(loader->load.u.sprite.table, field->value, sizeof(loader->load.u.sprite.table) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_archive(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.archive, field->value, sizeof(loader->load.u.sprite.archive) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_container(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.container,
            field->value,
            sizeof(loader->load.u.sprite.container) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_index_filename(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.index_filename,
            field->value,
            sizeof(loader->load.u.sprite.index_filename) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_data_filename(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.data_filename,
            field->value,
            sizeof(loader->load.u.sprite.data_filename) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_format(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.format, field->value, sizeof(loader->load.u.sprite.format) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_atlas_index(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_INDEX;
        loader->load.u.sprite.atlas_index = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_atlas_count(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_COUNT;
        loader->load.u.sprite.atlas_count = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_transform(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        if( loader->load.u.sprite.transform_count < 5 )
        {
            strncpy(
                loader->load.u.sprite.transforms[loader->load.u.sprite.transform_count],
                field->value,
                sizeof(loader->load.u.sprite.transforms[0]) - 1);
            loader->load.u.sprite.transform_count++;
        }
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_crop_x(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_x = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_crop_y(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_y = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_crop_width(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_width = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_cache_crop_height(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_height = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_type(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.type, field->value, sizeof(loader->load.u.component.type) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_sprite(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.sprite,
            field->value,
            sizeof(loader->load.u.component.sprite) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_sprite_active(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.sprite_active,
            field->value,
            sizeof(loader->load.u.component.sprite_active) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_width(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.width = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_height(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.height = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_anchor_x(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.anchor_x = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_anchor_y(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.anchor_y = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_tabno(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.tabno = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_componentno(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.componentno = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_inv(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.inv, field->value, sizeof(loader->load.u.component.inv) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_paint_levels(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.paint_levels,
            field->value,
            sizeof(loader->load.u.component.paint_levels) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_font(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.font, field->value, sizeof(loader->load.u.component.font) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chatback_screen_x(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.chatback_screen_x = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_X;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chatback_screen_y(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.chatback_screen_y = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_Y;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_clip_w(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.clip_w = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_W;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_clip_h(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.clip_h = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_H;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_text_x_local(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.text_x_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_TEXT_X_LOCAL;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_scrollbar_x_local(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.scrollbar_x_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_SCROLLBAR_X_LOCAL;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_separator_y_local(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.separator_y_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_Y_LOCAL;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_separator_w(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.separator_w = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_W;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_line_h(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.line_h = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_LINE_H;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_chat_input_line_y_local(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.input_line_y_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_minimenu_region_viewport(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_region_viewport,
            field->value,
            sizeof(loader->load.u.component.minimenu_region_viewport) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_minimenu_region_sidebar(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_region_sidebar,
            field->value,
            sizeof(loader->load.u.component.minimenu_region_sidebar) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_minimenu_region_chat(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_region_chat,
            field->value,
            sizeof(loader->load.u.component.minimenu_region_chat) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_minimenu_place_viewport_max(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_place_viewport_max,
            field->value,
            sizeof(loader->load.u.component.minimenu_place_viewport_max) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_minimenu_place_sidebar_max(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_place_sidebar_max,
            field->value,
            sizeof(loader->load.u.component.minimenu_place_sidebar_max) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_minimenu_place_chat_max(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_place_chat_max,
            field->value,
            sizeof(loader->load.u.component.minimenu_place_chat_max) - 1);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uicomponent_crosshair_hotspot_offset(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.crosshair_hotspot_offset = atoi(field->value);
        loader->load.u.component.crosshair_hotspot_offset_set = 1;
        break;
    default:
        break;
    }
}

static void
on_rcfield_inv_item(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_INV:
        if( loader->load.u.inv.item_count < UI_INVENTORY_MAX_ITEMS )
            loader->load.u.inv.item_ids[loader->load.u.inv.item_count++] = atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_component(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count >= MAX_LAYOUT_ENTRIES )
        {
            fprintf(
                stderr,
                "uitree_loader: layout exceeds MAX_LAYOUT_ENTRIES (%d);"
                " ignoring extra entries\n",
                MAX_LAYOUT_ENTRIES);
            break;
        }
        strncpy(
            loader->load.u.layout.entries[loader->load.u.layout.entry_count].component,
            field->value,
            sizeof(loader->load.u.layout.entries[0].component) - 1);
        loader->load.u.layout.entry_count++;
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_x(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.def_x = atoi(field->value);
        break;
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].x =
                atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_y(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.def_y = atoi(field->value);
        break;
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].y =
                atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_width(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].width =
                atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_height(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].height =
                atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_anchor_x(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].anchor_x =
                atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_anchor_y(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].anchor_y =
                atoi(field->value);
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_top(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_TOP;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].top =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_left(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_LEFT;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].left =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_bottom(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_BOTTOM;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].bottom =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_right(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_RIGHT;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].right =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
}

static void
on_rcfield_uilayout_dirty(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            const char* v = field->value;
            int truthy = (strcmp(v, "true") == 0) || (strcmp(v, "1") == 0);
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].always_dirty =
                truthy ? 1u : 0u;
        }
        break;
    default:
        break;
    }
}

static void
on_rcfield_default(
    struct UITreeLoader* loader,
    struct RevConfigField* field)
{
    (void)field;
}

enum UITreeLoaderStatus
uitree_loader_send_revconfig(
    struct UITreeLoader* loader,
    struct RevConfigBuffer* revconfig)
{
    if( !loader || !revconfig )
        return UITREE_LOADER_ERROR;
    if( loader->status == UITREE_LOADER_DONE || loader->status == UITREE_LOADER_ERROR )
        return loader->status;

    struct RevConfigField* field = NULL;
    enum UITreeLoaderStatus status = UITREE_LOADER_RUNNING;
    for( int i = loader->field_index; i < revconfig->field_count; i++ )
    {
        field = &revconfig->fields[i];

        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            on_rcfield_itemtype(loader, field);
            break;
        case RCFIELD_ITEMNAME:
            on_rcfield_itemname(loader, field);
            break;
        case RCFIELD_ITEMDONE:
        {
            status = on_rcfield_itemdone(loader, field);
            if( status != UITREE_LOADER_RUNNING )
            {
                loader->field_index = i + 1;
                return status;
            }
        }
        break;

        case RCFIELD_CACHE_TABLE:
            on_rcfield_cache_table(loader, field);
            break;
        case RCFIELD_CACHE_ARCHIVE:
            on_rcfield_cache_archive(loader, field);
            break;
        case RCFIELD_CACHE_CONTAINER:
            on_rcfield_cache_container(loader, field);
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            on_rcfield_cache_index_filename(loader, field);
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            on_rcfield_cache_data_filename(loader, field);
            break;
        case RCFIELD_CACHE_FORMAT:
            on_rcfield_cache_format(loader, field);
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            on_rcfield_cache_atlas_index(loader, field);
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            on_rcfield_cache_atlas_count(loader, field);
            break;
        case RCFIELD_CACHE_TRANSFORM:
            on_rcfield_cache_transform(loader, field);
            break;
        case RCFIELD_CACHE_CROP_X:
            on_rcfield_cache_crop_x(loader, field);
            break;
        case RCFIELD_CACHE_CROP_Y:
            on_rcfield_cache_crop_y(loader, field);
            break;
        case RCFIELD_CACHE_CROP_WIDTH:
            on_rcfield_cache_crop_width(loader, field);
            break;
        case RCFIELD_CACHE_CROP_HEIGHT:
            on_rcfield_cache_crop_height(loader, field);
            break;

        case RCFIELD_UICOMPONENT_TYPE:
            on_rcfield_uicomponent_type(loader, field);
            break;
        case RCFIELD_UICOMPONENT_SPRITE:
            on_rcfield_uicomponent_sprite(loader, field);
            break;
        case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
            on_rcfield_uicomponent_sprite_active(loader, field);
            break;
        case RCFIELD_UICOMPONENT_WIDTH:
            on_rcfield_uicomponent_width(loader, field);
            break;
        case RCFIELD_UICOMPONENT_HEIGHT:
            on_rcfield_uicomponent_height(loader, field);
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_X:
            on_rcfield_uicomponent_anchor_x(loader, field);
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_Y:
            on_rcfield_uicomponent_anchor_y(loader, field);
            break;
        case RCFIELD_UICOMPONENT_TABNO:
            on_rcfield_uicomponent_tabno(loader, field);
            break;
        case RCFIELD_UICOMPONENT_COMPONENTNO:
            on_rcfield_uicomponent_componentno(loader, field);
            break;
        case RCFIELD_UICOMPONENT_INV:
            on_rcfield_uicomponent_inv(loader, field);
            break;
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            on_rcfield_uicomponent_paint_levels(loader, field);
            break;
        case RCFIELD_UICOMPONENT_FONT:
            on_rcfield_uicomponent_font(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_X:
            on_rcfield_uicomponent_chatback_screen_x(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_Y:
            on_rcfield_uicomponent_chatback_screen_y(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_W:
            on_rcfield_uicomponent_chat_clip_w(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_H:
            on_rcfield_uicomponent_chat_clip_h(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_TEXT_X_LOCAL:
            on_rcfield_uicomponent_chat_text_x_local(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_SCROLLBAR_X_LOCAL:
            on_rcfield_uicomponent_chat_scrollbar_x_local(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_Y_LOCAL:
            on_rcfield_uicomponent_chat_separator_y_local(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_W:
            on_rcfield_uicomponent_chat_separator_w(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_LINE_H:
            on_rcfield_uicomponent_chat_line_h(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CHAT_INPUT_LINE_Y_LOCAL:
            on_rcfield_uicomponent_chat_input_line_y_local(loader, field);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_VIEWPORT:
            on_rcfield_uicomponent_minimenu_region_viewport(loader, field);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_SIDEBAR:
            on_rcfield_uicomponent_minimenu_region_sidebar(loader, field);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_CHAT:
            on_rcfield_uicomponent_minimenu_region_chat(loader, field);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_VIEWPORT_MAX:
            on_rcfield_uicomponent_minimenu_place_viewport_max(loader, field);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_SIDEBAR_MAX:
            on_rcfield_uicomponent_minimenu_place_sidebar_max(loader, field);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_CHAT_MAX:
            on_rcfield_uicomponent_minimenu_place_chat_max(loader, field);
            break;
        case RCFIELD_UICOMPONENT_CROSSHAIR_HOTSPOT_OFFSET:
            on_rcfield_uicomponent_crosshair_hotspot_offset(loader, field);
            break;

        case RCFIELD_INV_ITEM:
            on_rcfield_inv_item(loader, field);
            break;

        case RCFIELD_UILAYOUT_COMPONENT:
            on_rcfield_uilayout_component(loader, field);
            break;
        case RCFIELD_UILAYOUT_X:
            on_rcfield_uilayout_x(loader, field);
            break;
        case RCFIELD_UILAYOUT_Y:
            on_rcfield_uilayout_y(loader, field);
            break;
        case RCFIELD_UILAYOUT_WIDTH:
            on_rcfield_uilayout_width(loader, field);
            break;
        case RCFIELD_UILAYOUT_HEIGHT:
            on_rcfield_uilayout_height(loader, field);
            break;
        case RCFIELD_UILAYOUT_ANCHOR_X:
            on_rcfield_uilayout_anchor_x(loader, field);
            break;
        case RCFIELD_UILAYOUT_ANCHOR_Y:
            on_rcfield_uilayout_anchor_y(loader, field);
            break;
        case RCFIELD_UILAYOUT_TOP:
            on_rcfield_uilayout_top(loader, field);
            break;
        case RCFIELD_UILAYOUT_LEFT:
            on_rcfield_uilayout_left(loader, field);
            break;
        case RCFIELD_UILAYOUT_BOTTOM:
            on_rcfield_uilayout_bottom(loader, field);
            break;
        case RCFIELD_UILAYOUT_RIGHT:
            on_rcfield_uilayout_right(loader, field);
            break;
        case RCFIELD_UILAYOUT_DIRTY:
            on_rcfield_uilayout_dirty(loader, field);
            break;

        default:
            on_rcfield_default(loader, field);
            break;
        }
    }

    loader->field_index = revconfig->field_count;

    if( loader->pending_asset_count > 0 )
    {
        loader->status = UITREE_LOADER_NEEDS_ASSET;
        return UITREE_LOADER_NEEDS_ASSET;
    }

    loader->status = UITREE_LOADER_DONE;
    return UITREE_LOADER_DONE;
}

enum UITreeLoaderStatus
uitree_loader_send_rs_component(
    struct UITreeLoader* loader,
    int component_id)
{
    return push_pending_rs_component(loader, component_id);
}

enum UITreeLoaderStatus
uitree_loader_send(struct UITreeLoader* loader)
{
    assert(loader);
    if( loader->pending_asset_count == 0 )
        return UITREE_LOADER_DONE;

    struct UITree* ui = loader->ui_ref;

    for( int i = 0; i < loader->pending_asset_count; i++ )
    {
        struct UITreeLoaderAssetRequest* req = &loader->pending_assets[i];

        switch( req->kind )
        {
        case UITREE_ASSET_NONE:
            break;

        case UITREE_ASSET_SPRITE:
            break;

        case UITREE_ASSET_RS_COMPONENT:
            on_rs_component_done(loader, req);
            break;

        case UITREE_ASSET_MODEL:
            break;

        case UITREE_ASSET_FONT:
            break;

        default:
            loader->status = UITREE_LOADER_ERROR;
            return UITREE_LOADER_ERROR;
        }
    }

    loader->status = UITREE_LOADER_RUNNING;
    return UITREE_LOADER_RUNNING;
}

/* ── Accessors ──────────────────────────────────────────────────────────────── */

int
uitree_loader_pending_asset_count(const struct UITreeLoader* loader)
{
    if( !loader )
        return 0;
    return loader->pending_asset_count;
}

const struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets(const struct UITreeLoader* loader)
{
    if( !loader )
        return NULL;
    return loader->pending_assets;
}

struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets_mut(struct UITreeLoader* loader)
{
    if( !loader )
        return NULL;
    return loader->pending_assets;
}

/* ── Cleanup ────────────────────────────────────────────────────────────────── */

void
uitree_loader_free(struct UITreeLoader* loader)
{
    if( !loader )
        return;
    free(loader);
}
