#include "uitree_loader.h"

#include "graphics/dashmap.h"
#include "osrs/game.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_load.h"
#include "uitree_load_private.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal (full) definition of UITreeLoader ─────────────────────────────── */

struct UITreeLoader
{
    enum UITreeLoaderStatus status;
    struct UITreeLoaderAssetRequest pending_asset;

    /* Borrowed references — not owned, must outlive the loader. */
    struct UITree* ui;
    struct RevConfigBuffer* revconfig_buffer;

    /* Iteration state. */
    uint32_t field_index;    /* next field to process */
    struct CurrentLoad load; /* accumulator for the current INI item */

    /* Hashmaps — owned by loader, freed in uitree_loader_free(). */
    struct DashMap* sprite_hmap;
    struct DashMap* component_hmap;
    void* sprite_hmap_buffer;
    void* component_hmap_buffer;
};

/* ── Allocation ─────────────────────────────────────────────────────────────── */

struct UITreeLoader*
uitree_loader_new(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer)
{
    struct UITreeLoader* loader = calloc(1, sizeof(*loader));
    if( !loader )
        return NULL;

    loader->status = UITREE_LOADER_RUNNING;
    loader->ui = ui;
    loader->ui_scene = ui_scene;
    loader->inv_pool = inv_pool;
    loader->game = game;
    loader->revconfig_buffer = revconfig_buffer;
    loader->field_index = 0;

    /* Reset the UITree to empty before starting. */
    if( ui )
    {
        ui->component_count = 0;
        ui->root_index = -1;
    }

    /* Allocate hashmaps. */
    loader->sprite_hmap_buffer = malloc(1024 * sizeof(struct SpriteEntry));
    if( !loader->sprite_hmap_buffer )
    {
        free(loader);
        return NULL;
    }

    loader->component_hmap_buffer = malloc(1024 * sizeof(struct ComponentEntry));
    if( !loader->component_hmap_buffer )
    {
        free(loader->sprite_hmap_buffer);
        free(loader);
        return NULL;
    }

    struct DashMapConfig sprite_cfg = {
        .buffer = loader->sprite_hmap_buffer,
        .buffer_size = 1024 * sizeof(struct SpriteEntry),
        .key_size = 64,
        .entry_size = sizeof(struct SpriteEntry),
    };
    loader->sprite_hmap = dashmap_new(&sprite_cfg, 0);

    struct DashMapConfig component_cfg = {
        .buffer = loader->component_hmap_buffer,
        .buffer_size = 1024 * sizeof(struct ComponentEntry),
        .key_size = 64,
        .entry_size = sizeof(struct ComponentEntry),
    };
    loader->component_hmap = dashmap_new(&component_cfg, 0);

    if( !loader->sprite_hmap || !loader->component_hmap )
    {
        dashmap_free(loader->sprite_hmap);
        dashmap_free(loader->component_hmap);
        free(loader->sprite_hmap_buffer);
        free(loader->component_hmap_buffer);
        free(loader);
        return NULL;
    }

    return loader;
}

/* ── Step ───────────────────────────────────────────────────────────────────── */

enum UITreeLoaderStatus
uitree_loader_step(struct UITreeLoader* loader)
{
    if( !loader )
        return UITREE_LOADER_ERROR;
    if( loader->status == UITREE_LOADER_DONE || loader->status == UITREE_LOADER_ERROR )
        return loader->status;

    struct RevConfigBuffer* buf = loader->revconfig_buffer;

    while( loader->field_index < buf->field_count )
    {
        struct RevConfigField* field = &buf->fields[loader->field_index];

        switch( field->kind )
        {
        /* ── Kind tag: start of a new item ─────────────────────────────── */
        case RCFIELD_ITEMTYPE:
        {
            uint32_t k = uitree_impl_load_kind(field->value);
            loader->load.kind = (enum LoadKind)k;
            if( loader->load.kind == LOAD_KIND_INV )
                memset(&loader->load._inv, 0, sizeof(loader->load._inv));
            loader->field_index++;
        }
        break;

        /* ── Item name ──────────────────────────────────────────────────── */
        case RCFIELD_ITEMNAME:
            uitree_impl_on_itemname(&loader->load, field->value);
            loader->field_index++;
            break;

        /* ── Item complete: attempt to load it ──────────────────────────── */
        case RCFIELD_ITEMDONE:
        {
            struct UITreeLoaderAssetRequest req;
            memset(&req, 0, sizeof(req));
            req.kind = UITREE_ASSET_NONE;

            int rc = uitree_impl_load_item(
                &loader->load,
                loader->sprite_hmap,
                loader->component_hmap,
                loader->ui,
                loader->ui_scene,
                loader->inv_pool,
                loader->game,
                &req);

            if( rc != 0 )
            {
                /* An asset is missing; pause here so the caller can supply it.
                 * field_index is NOT advanced — the next call to step() will
                 * retry this same RCFIELD_ITEMDONE entry. */
                loader->pending_asset = req;
                loader->status = UITREE_LOADER_NEEDS_ASSET;
                return UITREE_LOADER_NEEDS_ASSET;
            }

            /* Item loaded successfully; reset accumulator and advance. */
            loader->load.kind = LOAD_KIND_NONE;
            memset(&loader->load, 0, sizeof(loader->load));
            loader->field_index++;

            /* Return RUNNING after each successful item so the caller can
             * interleave other work if desired. */
            loader->status = UITREE_LOADER_RUNNING;
            return UITREE_LOADER_RUNNING;
        }

        /* ── Sprite fields ──────────────────────────────────────────────── */
        case RCFIELD_CACHE_TABLE:
            strncpy(
                loader->load._sprite.table, field->value, sizeof(loader->load._sprite.table) - 1);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_ARCHIVE:
            strncpy(
                loader->load._sprite.archive,
                field->value,
                sizeof(loader->load._sprite.archive) - 1);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_CONTAINER:
            strncpy(
                loader->load._sprite.container,
                field->value,
                sizeof(loader->load._sprite.container) - 1);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            strncpy(
                loader->load._sprite.index_filename,
                field->value,
                sizeof(loader->load._sprite.index_filename) - 1);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            strncpy(
                loader->load._sprite.data_filename,
                field->value,
                sizeof(loader->load._sprite.data_filename) - 1);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_FORMAT:
            strncpy(
                loader->load._sprite.format, field->value, sizeof(loader->load._sprite.format) - 1);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            loader->load._sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_INDEX;
            loader->load._sprite.atlas_index = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            loader->load._sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_COUNT;
            loader->load._sprite.atlas_count = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_TRANSFORM:
            if( loader->load._sprite.transform_count < 5 )
            {
                strncpy(
                    loader->load._sprite.transforms[loader->load._sprite.transform_count],
                    field->value,
                    sizeof(loader->load._sprite.transforms[0]) - 1);
                loader->load._sprite.transform_count++;
            }
            loader->field_index++;
            break;
        case RCFIELD_CACHE_CROP_X:
            loader->load._sprite.crop_x = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_CROP_Y:
            loader->load._sprite.crop_y = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_CROP_WIDTH:
            loader->load._sprite.crop_width = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_CACHE_CROP_HEIGHT:
            loader->load._sprite.crop_height = atoi(field->value);
            loader->field_index++;
            break;

        /* ── Component fields ───────────────────────────────────────────── */
        case RCFIELD_UICOMPONENT_TYPE:
            strncpy(
                loader->load._component.type,
                field->value,
                sizeof(loader->load._component.type) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_SPRITE:
            strncpy(
                loader->load._component.sprite,
                field->value,
                sizeof(loader->load._component.sprite) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
            strncpy(
                loader->load._component.sprite_active,
                field->value,
                sizeof(loader->load._component.sprite_active) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_WIDTH:
            loader->load._component.width = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_HEIGHT:
            loader->load._component.height = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_X:
            loader->load._component.anchor_x = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_Y:
            loader->load._component.anchor_y = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_TABNO:
            loader->load._component.tabno = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_COMPONENTNO:
            loader->load._component.componentno = atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_INV:
            strncpy(
                loader->load._component.inv, field->value, sizeof(loader->load._component.inv) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            strncpy(
                loader->load._component.paint_levels,
                field->value,
                sizeof(loader->load._component.paint_levels) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_FONT:
            strncpy(
                loader->load._component.font,
                field->value,
                sizeof(loader->load._component.font) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_X:
            loader->load._component.chat_geom.chatback_screen_x = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_X;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_Y:
            loader->load._component.chat_geom.chatback_screen_y = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_Y;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_W:
            loader->load._component.chat_geom.clip_w = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_W;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_H:
            loader->load._component.chat_geom.clip_h = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_H;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_TEXT_X_LOCAL:
            loader->load._component.chat_geom.text_x_local = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_TEXT_X_LOCAL;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_SCROLLBAR_X_LOCAL:
            loader->load._component.chat_geom.scrollbar_x_local = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_SCROLLBAR_X_LOCAL;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_Y_LOCAL:
            loader->load._component.chat_geom.separator_y_local = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_Y_LOCAL;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_W:
            loader->load._component.chat_geom.separator_w = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_W;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_LINE_H:
            loader->load._component.chat_geom.line_h = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_LINE_H;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CHAT_INPUT_LINE_Y_LOCAL:
            loader->load._component.chat_geom.input_line_y_local = atoi(field->value);
            loader->load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL;
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_VIEWPORT:
            strncpy(
                loader->load._component.minimenu_region_viewport,
                field->value,
                sizeof(loader->load._component.minimenu_region_viewport) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_SIDEBAR:
            strncpy(
                loader->load._component.minimenu_region_sidebar,
                field->value,
                sizeof(loader->load._component.minimenu_region_sidebar) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_CHAT:
            strncpy(
                loader->load._component.minimenu_region_chat,
                field->value,
                sizeof(loader->load._component.minimenu_region_chat) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_VIEWPORT_MAX:
            strncpy(
                loader->load._component.minimenu_place_viewport_max,
                field->value,
                sizeof(loader->load._component.minimenu_place_viewport_max) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_SIDEBAR_MAX:
            strncpy(
                loader->load._component.minimenu_place_sidebar_max,
                field->value,
                sizeof(loader->load._component.minimenu_place_sidebar_max) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_CHAT_MAX:
            strncpy(
                loader->load._component.minimenu_place_chat_max,
                field->value,
                sizeof(loader->load._component.minimenu_place_chat_max) - 1);
            loader->field_index++;
            break;
        case RCFIELD_UICOMPONENT_CROSSHAIR_HOTSPOT_OFFSET:
            loader->load._component.crosshair_hotspot_offset = atoi(field->value);
            loader->load._component.crosshair_hotspot_offset_set = 1;
            loader->field_index++;
            break;

        /* ── Inventory item field ───────────────────────────────────────── */
        case RCFIELD_INV_ITEM:
            if( loader->load._inv.item_count < UI_INVENTORY_MAX_ITEMS )
                loader->load._inv.item_ids[loader->load._inv.item_count++] = atoi(field->value);
            loader->field_index++;
            break;

        /* ── Layout fields ──────────────────────────────────────────────── */
        case RCFIELD_UILAYOUT_COMPONENT:
        {
            if( loader->load._layout.entry_count >= MAX_LAYOUT_ENTRIES )
            {
                fprintf(
                    stderr,
                    "uitree_loader: layout exceeds MAX_LAYOUT_ENTRIES (%d);"
                    " ignoring extra entries\n",
                    MAX_LAYOUT_ENTRIES);
                loader->field_index++;
                break;
            }
            strncpy(
                loader->load._layout.entries[loader->load._layout.entry_count].component,
                field->value,
                sizeof(loader->load._layout.entries[0].component) - 1);
            loader->load._layout.entry_count++;
            loader->field_index++;
        }
        break;
        case RCFIELD_UILAYOUT_X:
        {
            if( loader->load.kind == LOAD_KIND_COMPONENT )
            {
                loader->load._component.def_x = atoi(field->value);
            }
            else if( loader->load._layout.entry_count > 0 )
            {
                loader->load._layout.entries[loader->load._layout.entry_count - 1].x =
                    atoi(field->value);
            }
            loader->field_index++;
        }
        break;
        case RCFIELD_UILAYOUT_Y:
        {
            if( loader->load.kind == LOAD_KIND_COMPONENT )
            {
                loader->load._component.def_y = atoi(field->value);
            }
            else if( loader->load._layout.entry_count > 0 )
            {
                loader->load._layout.entries[loader->load._layout.entry_count - 1].y =
                    atoi(field->value);
            }
            loader->field_index++;
        }
        break;
        case RCFIELD_UILAYOUT_WIDTH:
            if( loader->load._layout.entry_count > 0 )
                loader->load._layout.entries[loader->load._layout.entry_count - 1].width =
                    atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_HEIGHT:
            if( loader->load._layout.entry_count > 0 )
                loader->load._layout.entries[loader->load._layout.entry_count - 1].height =
                    atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_ANCHOR_X:
            if( loader->load._layout.entry_count > 0 )
                loader->load._layout.entries[loader->load._layout.entry_count - 1].anchor_x =
                    atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_ANCHOR_Y:
            if( loader->load._layout.entry_count > 0 )
                loader->load._layout.entries[loader->load._layout.entry_count - 1].anchor_y =
                    atoi(field->value);
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_TOP:
            if( loader->load._layout.entry_count > 0 )
            {
                loader->load._layout.entries[loader->load._layout.entry_count - 1].flags =
                    STATIC_UI_RELATIVE_FLAG_TOP;
                loader->load._layout.entries[loader->load._layout.entry_count - 1].top =
                    atoi(field->value);
            }
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_LEFT:
            if( loader->load._layout.entry_count > 0 )
            {
                loader->load._layout.entries[loader->load._layout.entry_count - 1].flags =
                    STATIC_UI_RELATIVE_FLAG_LEFT;
                loader->load._layout.entries[loader->load._layout.entry_count - 1].left =
                    atoi(field->value);
            }
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_BOTTOM:
            if( loader->load._layout.entry_count > 0 )
            {
                loader->load._layout.entries[loader->load._layout.entry_count - 1].flags =
                    STATIC_UI_RELATIVE_FLAG_BOTTOM;
                loader->load._layout.entries[loader->load._layout.entry_count - 1].bottom =
                    atoi(field->value);
            }
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_RIGHT:
            if( loader->load._layout.entry_count > 0 )
            {
                loader->load._layout.entries[loader->load._layout.entry_count - 1].flags =
                    STATIC_UI_RELATIVE_FLAG_RIGHT;
                loader->load._layout.entries[loader->load._layout.entry_count - 1].right =
                    atoi(field->value);
            }
            loader->field_index++;
            break;
        case RCFIELD_UILAYOUT_DIRTY:
            if( loader->load._layout.entry_count > 0 )
            {
                const char* v = field->value;
                int truthy = (strcmp(v, "true") == 0) || (strcmp(v, "1") == 0);
                loader->load._layout.entries[loader->load._layout.entry_count - 1].always_dirty =
                    truthy ? 1u : 0u;
            }
            loader->field_index++;
            break;

        default:
            loader->field_index++;
            break;
        }
    }

    /* All fields consumed — finalize. */
    uitree_impl_resolve_game_uiscene_sprite_ids(loader->game, loader->ui_scene);

    if( loader->ui && uitree_validate_sidebar_tab_layout(loader->ui) != 0 )
        fprintf(stderr, "uitree_loader: sidebar tab layout invalid\n");

    if( loader->ui )
        uitree_print_nodes(loader->ui);

    loader->status = UITREE_LOADER_DONE;
    return UITREE_LOADER_DONE;
}

/* ── Accessors ──────────────────────────────────────────────────────────────── */

struct UITreeLoaderAssetRequest
uitree_loader_pending_asset(const struct UITreeLoader* loader)
{
    struct UITreeLoaderAssetRequest empty;
    memset(&empty, 0, sizeof(empty));
    if( !loader )
        return empty;
    return loader->pending_asset;
}

/* ── Cleanup ────────────────────────────────────────────────────────────────── */

void
uitree_loader_free(struct UITreeLoader* loader)
{
    if( !loader )
        return;
    dashmap_free(loader->sprite_hmap);
    dashmap_free(loader->component_hmap);
    free(loader->sprite_hmap_buffer);
    free(loader->component_hmap_buffer);
    free(loader);
}
