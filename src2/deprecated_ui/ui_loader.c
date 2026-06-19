#include "ui_loader.h"

#include "../buildcache/dat1_buildcache.h"
#include "../ioqueue/libtorirs_ioqueue.h"
#include "gamecache/gamecache.h"
#include "gamecache/toridraw_cachemodel.h"
#include "gamecache/toridraw_cachesprite.h"
#include "graphics/dashmap.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "ui_resource_queue.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_LOADER_MAX_LAYOUT_ENTRIES 64
#define UI_LOADER_MAX_PENDING_ARCHIVES 32
#define UI_LOADER_MAX_PENDING_MODELS 32
#define UI_LOADER_MAX_WORK_ITEMS 32

/* -------------------------------------------------------------------------- */
/* Internal types                                                             */
/* -------------------------------------------------------------------------- */

struct SpriteMapEntry
{
    char name[64];
    int scene_id;
    int atlas_index;
};

struct ComponentMapEntry
{
    char name[64];
    enum StaticUIComponentType type;
    char sprite_name[64];
    int sprite_id;
    int sprite_index;
    int width;
    int height;
    uint8_t level_mask;
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
    uint8_t always_dirty;
};

struct UILoaderPendingArchive
{
    int table_id;
    int archive_id;
    bool received;
    bool io_dispatched;
};

struct UILoaderPendingModel
{
    int model_id;
    bool received;
    bool io_dispatched;
};

enum UILoaderWorkKind
{
    UILOADER_WORK_PROCESS_REVCONFIG = 0,
    UILOADER_WORK_PROCESS_RESOURCE_QUEUE,
};

struct UILoaderState
{
    struct LayoutItem layout_entries[UI_LOADER_MAX_LAYOUT_ENTRIES];
    int layout_entry_count;

    struct ComponentMapEntry components[128];
    int component_count;

    void* sprite_map_buffer;
    void* component_map_buffer;
    struct DashMap* sprite_map;
    struct DashMap* component_map;

    struct Dat1BuildCache* buildcache;
    struct GameCache* gamecache;
    struct UIScene* ui_scene;

    struct UIResourceQueue* queue;
    struct RevConfigBuffer* revconfig;
    enum UILoaderWorkKind work_items[UI_LOADER_MAX_WORK_ITEMS];
    int work_head;
    int work_count;
    bool done;
    struct UILoaderPendingArchive pending_archives[UI_LOADER_MAX_PENDING_ARCHIVES];
    int pending_archive_count;
    struct UILoaderPendingModel pending_models[UI_LOADER_MAX_PENDING_MODELS];
    int pending_model_count;
    bool need_fonts;
    bool fonts_received;
    bool fonts_io_dispatched;
};

enum UILoaderItemKind
{
    UILOADER_ITEM_NONE = 0,
    UILOADER_ITEM_SPRITE,
    UILOADER_ITEM_COMPONENT,
    UILOADER_ITEM_LAYOUT,
};

struct UILoaderBuildCtx
{
    enum UILoaderItemKind kind;
    struct UIResourceQueueItem pending_sprite;
    struct ComponentMapEntry pending_component;
    struct LayoutItem pending_layout;
    bool layout_has_entry;
};

/* -------------------------------------------------------------------------- */
/* Work queue helpers                                                         */
/* -------------------------------------------------------------------------- */

static void
ui_loader_work_clear(struct UILoaderState* state)
{
    state->work_head = 0;
    state->work_count = 0;
}

static bool
ui_loader_work_push(
    struct UILoaderState* state,
    enum UILoaderWorkKind kind)
{
    if( state->work_count >= UI_LOADER_MAX_WORK_ITEMS )
        return false;
    state->work_items[state->work_count++] = kind;
    return true;
}

static bool
ui_loader_work_pop(
    struct UILoaderState* state,
    enum UILoaderWorkKind* out_kind)
{
    if( state->work_head >= state->work_count )
        return false;
    *out_kind = state->work_items[state->work_head++];
    return true;
}

static bool
ui_loader_work_empty(struct UILoaderState* state)
{
    return state->work_head >= state->work_count;
}

static bool
ui_loader_work_contains(
    struct UILoaderState* state,
    enum UILoaderWorkKind kind)
{
    for( int i = state->work_head; i < state->work_count; i++ )
    {
        if( state->work_items[i] == kind )
            return true;
    }
    return false;
}

static bool
ui_loader_item_is_terminal(const struct UIResourceQueueItem* item)
{
    return item->state == UIRES_STATE_DONE || item->state == UIRES_STATE_FAILED;
}

static bool
ui_loader_resource_queue_has_incomplete(struct UILoaderState* state)
{
    struct UIResourceQueue* rq = state->queue;
    if( !rq )
        return false;

    for( int i = 0; i < rq->count; i++ )
    {
        if( !ui_loader_item_is_terminal(&rq->items[i]) )
            return true;
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* String / parse helpers                                                     */
/* -------------------------------------------------------------------------- */

static enum UILoaderItemKind
load_kind(const char* str)
{
    if( strcmp(str, "sprite") == 0 )
        return UILOADER_ITEM_SPRITE;
    if( strcmp(str, "component") == 0 )
        return UILOADER_ITEM_COMPONENT;
    if( strcmp(str, "layout") == 0 )
        return UILOADER_ITEM_LAYOUT;
    return UILOADER_ITEM_NONE;
}

static enum StaticUIComponentType
component_type_from_string(const char* str)
{
    if( strcmp(str, "world") == 0 )
        return UIELEM_BUILTIN_WORLD;
    if( strcmp(str, "sprite") == 0 )
        return UIELEM_BUILTIN_SPRITE;
    return UIELEM_BUILTIN_SPRITE;
}

static void
parse_sprite_bracket(
    const char* sprite_ref,
    char* name_out,
    int* atlas_index_out)
{
    strncpy(name_out, sprite_ref, 63);
    name_out[63] = '\0';
    *atlas_index_out = 0;

    char* open = strchr(name_out, '[');
    char* close = strchr(name_out, ']');
    if( open && close && close > open )
    {
        *open = '\0';
        *atlas_index_out = atoi(open + 1);
    }
}

/* -------------------------------------------------------------------------- */
/* Build phase helpers                                                        */
/* -------------------------------------------------------------------------- */

static void
build_layout_flush(
    struct UILoaderState* state,
    struct UILoaderBuildCtx* ctx)
{
    if( ctx->layout_has_entry && state->layout_entry_count < UI_LOADER_MAX_LAYOUT_ENTRIES )
        state->layout_entries[state->layout_entry_count++] = ctx->pending_layout;
}

static void
build_begin_item(
    struct UILoaderBuildCtx* ctx,
    const char* type_value)
{
    ctx->kind = load_kind(type_value);
    memset(&ctx->pending_sprite, 0, sizeof(ctx->pending_sprite));
    memset(&ctx->pending_component, 0, sizeof(ctx->pending_component));
    memset(&ctx->pending_layout, 0, sizeof(ctx->pending_layout));
    ctx->layout_has_entry = false;
    ctx->pending_sprite.kind = UIRES_KIND_SPRITE;
    ctx->pending_component.sprite_id = -1;
    ctx->pending_component.level_mask = 0xFu;
}

static void
build_set_item_name(
    struct UILoaderBuildCtx* ctx,
    const char* value)
{
    if( ctx->kind == UILOADER_ITEM_SPRITE )
        strncpy(ctx->pending_sprite.name, value, sizeof(ctx->pending_sprite.name) - 1);
    else if( ctx->kind == UILOADER_ITEM_COMPONENT )
        strncpy(ctx->pending_component.name, value, sizeof(ctx->pending_component.name) - 1);
}

static void
build_finish_item(
    struct UILoaderState* state,
    struct UILoaderBuildCtx* ctx)
{
    struct UIResourceQueue* queue = state->queue;

    if( ctx->kind == UILOADER_ITEM_SPRITE )
    {
        ctx->pending_sprite.status = UIRES_PENDING;
        ui_resource_queue_push_sprite(queue, &ctx->pending_sprite);
    }
    else if( ctx->kind == UILOADER_ITEM_COMPONENT )
    {
        struct ComponentMapEntry* ent =
            dashmap_search(state->component_map, ctx->pending_component.name, DASHMAP_INSERT);
        if( ent )
            *ent = ctx->pending_component;
        if( state->component_count < 128 )
            state->components[state->component_count++] = ctx->pending_component;
    }
    else if( ctx->kind == UILOADER_ITEM_LAYOUT )
        build_layout_flush(state, ctx);

    ctx->kind = UILOADER_ITEM_NONE;
}

static void
build_apply_cache_field(
    struct UILoaderBuildCtx* ctx,
    const struct RevConfigField* field)
{
    struct UIResourceQueueItem* sprite = &ctx->pending_sprite;

    switch( field->kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(sprite->table, field->value, sizeof(sprite->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(sprite->archive, field->value, sizeof(sprite->archive) - 1);
        break;
    case RCFIELD_CACHE_CONTAINER:
        strncpy(sprite->container, field->value, sizeof(sprite->container) - 1);
        break;
    case RCFIELD_CACHE_INDEX_FILENAME:
        strncpy(sprite->index_filename, field->value, sizeof(sprite->index_filename) - 1);
        break;
    case RCFIELD_CACHE_DATA_FILENAME:
        strncpy(sprite->data_filename, field->value, sizeof(sprite->data_filename) - 1);
        break;
    case RCFIELD_CACHE_FORMAT:
        strncpy(sprite->format, field->value, sizeof(sprite->format) - 1);
        break;
    case RCFIELD_CACHE_ATLAS_INDEX:
        sprite->atlas_index = atoi(field->value);
        sprite->atlas_use_count = false;
        break;
    case RCFIELD_CACHE_ATLAS_COUNT:
        sprite->atlas_count = atoi(field->value);
        sprite->atlas_use_count = true;
        break;
    default:
        break;
    }
}

static void
build_apply_component_field(
    struct UILoaderBuildCtx* ctx,
    const struct RevConfigField* field)
{
    struct ComponentMapEntry* comp = &ctx->pending_component;

    switch( field->kind )
    {
    case RCFIELD_UICOMPONENT_TYPE:
        comp->type = component_type_from_string(field->value);
        break;
    case RCFIELD_UICOMPONENT_SPRITE:
        strncpy(comp->sprite_name, field->value, sizeof(comp->sprite_name) - 1);
        break;
    case RCFIELD_UICOMPONENT_WIDTH:
        comp->width = atoi(field->value);
        break;
    case RCFIELD_UICOMPONENT_HEIGHT:
        comp->height = atoi(field->value);
        break;
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        comp->level_mask = 0xFu;
        break;
    default:
        break;
    }
}

static void
build_apply_layout_field(
    struct UILoaderState* state,
    struct UILoaderBuildCtx* ctx,
    const struct RevConfigField* field)
{
    struct LayoutItem* layout = &ctx->pending_layout;

    switch( field->kind )
    {
    case RCFIELD_UILAYOUT_COMPONENT:
        build_layout_flush(state, ctx);
        memset(layout, 0, sizeof(*layout));
        strncpy(layout->component, field->value, sizeof(layout->component) - 1);
        ctx->layout_has_entry = true;
        break;
    case RCFIELD_UILAYOUT_X:
        layout->x = atoi(field->value);
        break;
    case RCFIELD_UILAYOUT_Y:
        layout->y = atoi(field->value);
        break;
    case RCFIELD_UILAYOUT_TOP:
        layout->top = atoi(field->value);
        layout->flags |= STATIC_UI_RELATIVE_FLAG_TOP;
        break;
    case RCFIELD_UILAYOUT_LEFT:
        layout->left = atoi(field->value);
        layout->flags |= STATIC_UI_RELATIVE_FLAG_LEFT;
        break;
    case RCFIELD_UILAYOUT_BOTTOM:
        layout->bottom = atoi(field->value);
        layout->flags |= STATIC_UI_RELATIVE_FLAG_BOTTOM;
        break;
    case RCFIELD_UILAYOUT_RIGHT:
        layout->right = atoi(field->value);
        layout->flags |= STATIC_UI_RELATIVE_FLAG_RIGHT;
        break;
    case RCFIELD_UILAYOUT_DIRTY:
        layout->always_dirty = 1;
        break;
    case RCFIELD_UILAYOUT_NULL:
        build_layout_flush(state, ctx);
        memset(layout, 0, sizeof(*layout));
        ctx->layout_has_entry = false;
        break;
    default:
        break;
    }
}

static bool
ui_loader_build(struct UILoaderState* state)
{
    struct UIResourceQueue* queue;
    struct RevConfigBuffer* revconfig;

    queue = state->queue;
    revconfig = state->revconfig;

    state->layout_entry_count = 0;
    state->component_count = 0;

    struct UILoaderBuildCtx ctx = { 0 };

    for( uint32_t fi = 0; fi < revconfig->field_count; fi++ )
    {
        const struct RevConfigField* field = &revconfig->fields[fi];

        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            build_begin_item(&ctx, field->value);
            break;
        case RCFIELD_ITEMNAME:
            build_set_item_name(&ctx, field->value);
            break;
        case RCFIELD_ITEMDONE:
            build_finish_item(state, &ctx);
            break;

        case RCFIELD_CACHE_TABLE:
        case RCFIELD_CACHE_ARCHIVE:
        case RCFIELD_CACHE_CONTAINER:
        case RCFIELD_CACHE_INDEX_FILENAME:
        case RCFIELD_CACHE_DATA_FILENAME:
        case RCFIELD_CACHE_FORMAT:
        case RCFIELD_CACHE_ATLAS_INDEX:
        case RCFIELD_CACHE_ATLAS_COUNT:
            build_apply_cache_field(&ctx, field);
            break;

        case RCFIELD_UICOMPONENT_TYPE:
        case RCFIELD_UICOMPONENT_SPRITE:
        case RCFIELD_UICOMPONENT_WIDTH:
        case RCFIELD_UICOMPONENT_HEIGHT:
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            build_apply_component_field(&ctx, field);
            break;

        case RCFIELD_UILAYOUT_COMPONENT:
        case RCFIELD_UILAYOUT_X:
        case RCFIELD_UILAYOUT_Y:
        case RCFIELD_UILAYOUT_TOP:
        case RCFIELD_UILAYOUT_LEFT:
        case RCFIELD_UILAYOUT_BOTTOM:
        case RCFIELD_UILAYOUT_RIGHT:
        case RCFIELD_UILAYOUT_DIRTY:
        case RCFIELD_UILAYOUT_NULL:
            build_apply_layout_field(state, &ctx, field);
            break;

        default:
            break;
        }
    }

    printf(
        "ui_loader_build: %d sprite requests, %d layout entries\n",
        queue->count,
        state->layout_entry_count);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Archive / sprite decode helpers                                            */
/* -------------------------------------------------------------------------- */

static struct ToriDraw_Sprite*
ui_loader_decode_sprite(
    struct FileListDat* filelist,
    const struct UIResourceQueueItem* item,
    int atlas_index)
{
    if( !filelist || !item )
        return NULL;

    int index_file_idx = filelist_dat_find_file_by_name(filelist, item->index_filename);
    int data_file_idx = filelist_dat_find_file_by_name(filelist, item->data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    if( strcmp(item->format, "pix8") == 0 )
    {
        struct CacheDatPix8Palette* pix8_palette = cache_dat_pix8_palette_new(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix8_palette )
            return NULL;

        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix8Palette(pix8_palette);
        cache_dat_pix8_palette_free(pix8_palette);
        return sprite;
    }

    if( strcmp(item->format, "pix32") == 0 )
    {
        struct CacheDatPix32* pix32 = cache_dat_pix32_new(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix32 )
            return NULL;

        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix32(pix32);
        cache_dat_pix32_free(pix32);
        return sprite;
    }

    return NULL;
}

static struct ToriDraw_Sprite*
ui_loader_placeholder_sprite(
    int width,
    int height)
{
    int w = width > 0 ? width : 32;
    int h = height > 0 ? height : 32;
    uint32_t* pixels = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if( !pixels )
        return NULL;
    for( int i = 0; i < w * h; i++ )
        pixels[i] = 0xFFFF00FFu;
    return ToriDraw_SpriteNewFromArgbOwned(pixels, w, h);
}

static struct ToriDraw_Sprite*
ui_loader_decode_media_sprite_ref(
    struct FileListDat* filelist,
    const char* sprite_ref)
{
    char filename_buf[256];
    int sprite_idx = 0;

    if( !filelist || !sprite_ref || !sprite_ref[0] )
        return NULL;

    if( sscanf(sprite_ref, "%255[^,],%d", filename_buf, &sprite_idx) != 2 )
    {
        strncpy(filename_buf, sprite_ref, sizeof(filename_buf) - 1);
        filename_buf[sizeof(filename_buf) - 1] = '\0';
        sprite_idx = 0;
    }

    size_t len = strlen(filename_buf);
    if( len + 5 > sizeof(filename_buf) )
        return NULL;
    if( len < 4 || strcmp(filename_buf + len - 4, ".dat") != 0 )
    {
        memcpy(filename_buf + len, ".dat", 4);
        filename_buf[len + 4] = '\0';
    }

    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    int data_file_idx = filelist_dat_find_file_by_name(filelist, filename_buf);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    struct CacheDatPix32* pix32 = cache_dat_pix32_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( pix32 )
    {
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix32(pix32);
        cache_dat_pix32_free(pix32);
        if( sprite )
            return sprite;
    }

    struct CacheDatPix8Palette* pix8_palette = cache_dat_pix8_palette_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix8_palette )
        return NULL;

    struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix8Palette(pix8_palette);
    cache_dat_pix8_palette_free(pix8_palette);
    return sprite;
}

static bool
ui_loader_decode_interfaces_from_archive(
    struct CacheDatArchive* archive,
    struct CacheDatConfigComponentList** out_list)
{
    struct FileListDat* filelist;
    int data_idx;

    if( !archive || !out_list )
        return false;

    filelist = filelist_dat_new_from_cache_dat_archive(archive);
    if( !filelist )
        return false;

    data_idx = filelist_dat_find_file_by_name(filelist, "data");
    if( data_idx < 0 )
    {
        filelist_dat_free(filelist);
        return false;
    }

    *out_list = cache_dat_config_component_list_new_decode(
        filelist->files[data_idx], filelist->file_sizes[data_idx]);
    filelist_dat_free(filelist);
    return *out_list != NULL;
}

static struct CacheDatConfigComponent*
ui_loader_get_interface_component(
    struct CacheDatConfigComponentList* interfaces,
    int component_id)
{
    if( !interfaces || component_id < 0 || component_id >= interfaces->components_count )
        return NULL;
    return interfaces->components[component_id];
}

static int
ui_loader_acquire_rs_sprite(
    struct UILoaderState* state,
    struct UIScene* scene,
    struct FileListDat* media,
    const char* sprite_ref)
{
    struct SpriteMapEntry* sm;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** row;
    int scene_id;

    if( !state || !scene || !media || !sprite_ref || !sprite_ref[0] )
        return -1;

    sm = dashmap_search(state->sprite_map, sprite_ref, DASHMAP_FIND);
    if( sm )
        return sm->scene_id;

    sprite = ui_loader_decode_media_sprite_ref(media, sprite_ref);
    if( !sprite )
        return -1;

    row = malloc(sizeof(struct ToriDraw_Sprite*));
    if( !row )
    {
        ToriDraw_SpriteFree(sprite);
        return -1;
    }
    row[0] = sprite;

    scene_id = ui_scene_element_acquire_with_sprites(scene, row, 1, false, sprite_ref);
    if( scene_id < 0 )
    {
        ToriDraw_SpriteFree(sprite);
        free(row);
        return -1;
    }

    sm = dashmap_search(state->sprite_map, sprite_ref, DASHMAP_INSERT);
    if( sm )
    {
        strncpy(sm->name, sprite_ref, sizeof(sm->name) - 1);
        sm->name[sizeof(sm->name) - 1] = '\0';
        sm->scene_id = scene_id;
        sm->atlas_index = 0;
    }

    return scene_id;
}

static int
ui_loader_ensure_font_id(
    struct UIScene* scene,
    int font_idx)
{
    static const char* const font_names[] = { "p11", "p12", "b12", "q8" };
    int fidx = font_idx;
    if( fidx < 0 || fidx > 3 )
        fidx = 1;
    if( !scene )
        return -1;
    return ui_scene_font_find_id(scene, font_names[fidx]);
}

static void
ui_loader_bake_rs_subtree(
    struct UILoaderState* state,
    struct UITree* tree,
    struct UIScene* scene,
    struct FileListDat* media,
    struct CacheDatConfigComponentList* interfaces,
    int32_t parent_idx,
    struct CacheDatConfigComponent* comp,
    int abs_x,
    int abs_y)
{
    int32_t layer_idx;
    int sid;
    int sid_a;
    int fid;
    int bg_sid[UI_INV_SLOT_OFFSET_MAX];
    int bg_ai[UI_INV_SLOT_OFFSET_MAX];

    if( !state || !tree || !scene || !media || !comp || !interfaces )
        return;

    if( comp->hide && comp->type != COMPONENT_TYPE_LAYER )
        return;

    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
        layer_idx = uitree_push_rs_layer(
            tree, parent_idx, comp->id, abs_x, abs_y, comp->width, comp->height);
        if( layer_idx < 0 || !comp->children )
            return;

        for( int i = 0; i < comp->children_count; i++ )
        {
            struct CacheDatConfigComponent* child =
                ui_loader_get_interface_component(interfaces, comp->children[i]);
            int cx;
            int cy;

            if( !child )
                continue;

            cx = abs_x + (comp->childX ? comp->childX[i] : 0) + child->x;
            cy = abs_y + (comp->childY ? comp->childY[i] : 0) + child->y;
            ui_loader_bake_rs_subtree(
                state, tree, scene, media, interfaces, layer_idx, child, cx, cy);
        }
        break;

    case COMPONENT_TYPE_GRAPHIC:
        sid = ui_loader_acquire_rs_sprite(state, scene, media, comp->graphic);
        sid_a = ui_loader_acquire_rs_sprite(state, scene, media, comp->activeGraphic);
        if( sid < 0 && sid_a < 0 )
            return;

        uitree_push_rs_graphic(
            tree,
            parent_idx,
            comp->id,
            sid >= 0 ? sid : sid_a,
            0,
            sid >= 0 && sid_a >= 0 && sid_a != sid ? sid_a : -1,
            0,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
        break;

    case COMPONENT_TYPE_RECT:
        if( comp->fill )
        {
            uitree_push_rs_rect(
                tree,
                parent_idx,
                comp->id,
                comp->colour,
                comp->fill ? 1 : 0,
                abs_x,
                abs_y,
                comp->width,
                comp->height);
        }
        break;

    case COMPONENT_TYPE_TEXT:
        fid = ui_loader_ensure_font_id(scene, comp->font);
        uitree_push_rs_text(
            tree,
            parent_idx,
            comp->id,
            fid,
            comp->colour,
            comp->center ? 1 : 0,
            comp->shadowed ? 1 : 0,
            comp->text,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
        break;

    case COMPONENT_TYPE_INV_TEXT:
        fid = ui_loader_ensure_font_id(scene, comp->font);
        uitree_push_rs_text(
            tree,
            parent_idx,
            comp->id,
            fid,
            comp->colour,
            comp->center ? 1 : 0,
            comp->shadowed ? 1 : 0,
            comp->text,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
        break;

    case COMPONENT_TYPE_MODEL:
        if( comp->modelType == 1 && state->gamecache )
        {
            struct ToriDraw_ModelHandle hnd =
                gamecache_model_get(state->gamecache, comp->model);
            if( hnd.kind == TORIDRAWMK_MODEL )
            {
                uitree_push_rs_model(
                    tree,
                    parent_idx,
                    comp->id,
                    comp->model,
                    comp->zoom,
                    comp->xan,
                    comp->yan,
                    abs_x,
                    abs_y,
                    comp->width,
                    comp->height);
            }
        }
        break;

    case COMPONENT_TYPE_INV:
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            bg_sid[si] = -1;
            bg_ai[si] = 0;
        }
        if( comp->invSlotGraphic )
        {
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                char const* gname = comp->invSlotGraphic[si];
                if( !gname || gname[0] == '\0' )
                    continue;
                bg_sid[si] = ui_loader_acquire_rs_sprite(state, scene, media, gname);
                bg_ai[si] = 0;
            }
        }
        uitree_push_rs_inv(
            tree,
            parent_idx,
            comp->id,
            -1,
            comp->width,
            comp->height,
            comp->marginX,
            comp->marginY,
            comp->invSlotOffsetX,
            comp->invSlotOffsetY,
            bg_sid,
            bg_ai,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
        break;

    default:
        break;
    }
}

static int
ui_loader_pending_model_index(
    struct UILoaderState* state,
    int model_id)
{
    for( int i = 0; i < state->pending_model_count; i++ )
    {
        if( state->pending_models[i].model_id == model_id )
            return i;
    }

    if( state->pending_model_count >= UI_LOADER_MAX_PENDING_MODELS )
        return -1;

    int idx = state->pending_model_count++;
    state->pending_models[idx].model_id = model_id;
    state->pending_models[idx].received = false;
    state->pending_models[idx].io_dispatched = false;
    return idx;
}

static void
ui_loader_collect_rs_deps_from_component(
    struct UILoaderState* state,
    struct CacheDatConfigComponentList* interfaces,
    struct CacheDatConfigComponent* comp)
{
    if( !state || !comp || !interfaces )
        return;

    if( comp->type == COMPONENT_TYPE_TEXT || comp->type == COMPONENT_TYPE_INV_TEXT )
        state->need_fonts = true;

    if( comp->type == COMPONENT_TYPE_MODEL && comp->modelType == 1 && comp->model > 0 )
    {
        if( !state->gamecache )
            return;
        struct ToriDraw_ModelHandle hnd = gamecache_model_get(state->gamecache, comp->model);
        if( hnd.kind != TORIDRAWMK_MODEL )
            ui_loader_pending_model_index(state, comp->model);
    }

    if( comp->type == COMPONENT_TYPE_LAYER && comp->children )
    {
        for( int i = 0; i < comp->children_count; i++ )
        {
            struct CacheDatConfigComponent* child =
                ui_loader_get_interface_component(interfaces, comp->children[i]);
            if( child )
                ui_loader_collect_rs_deps_from_component(state, interfaces, child);
        }
    }
}

static bool
ui_loader_queue_pending_models(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !state || !io_queue )
        return false;

    for( int i = 0; i < state->pending_model_count; i++ )
    {
        struct UILoaderPendingModel* pending = &state->pending_models[i];
        if( pending->received || pending->io_dispatched )
            continue;

        if( state->gamecache )
        {
            struct ToriDraw_ModelHandle hnd =
                gamecache_model_get(state->gamecache, pending->model_id);
            if( hnd.kind == TORIDRAWMK_MODEL )
            {
                pending->received = true;
                continue;
            }
        }

        LibToriRS_IOQueuePush(io_queue, CACHE_DAT_MODELS, pending->model_id, 0);
        pending->io_dispatched = true;
        printf(
            "UI_Process: queued model IO id=%d\n",
            pending->model_id);
    }
    return true;
}

static bool
ui_loader_all_pending_models_received(struct UILoaderState* state)
{
    if( state->pending_model_count <= 0 )
        return true;

    for( int i = 0; i < state->pending_model_count; i++ )
    {
        if( !state->pending_models[i].received )
            return false;
    }
    return true;
}

static bool
ui_loader_fonts_ready(struct UILoaderState* state)
{
    if( !state || !state->need_fonts )
        return true;
    return state->fonts_received;
}

static void
ui_loader_bake_rs_roots(
    struct UILoaderState* state,
    struct UITree* tree,
    struct UIScene* scene)
{
    struct UIResourceQueue* queue;
    struct Dat1BuildCache* buildcache;
    struct CacheDatConfigComponentList* interfaces;
    struct FileListDat* media;

    if( !state || !tree || !scene )
        return;

    queue = state->queue;
    buildcache = state->buildcache;
    if( !queue || !buildcache )
        return;

    interfaces = dat1_buildcache_get_interfaces(buildcache);
    media = dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    if( !interfaces || !media )
        return;

    for( int i = 0; i < queue->count; i++ )
    {
        struct UIResourceQueueItem* item = &queue->items[i];
        struct CacheDatConfigComponent* root;

        if( item->kind != UIRES_KIND_RS_COMPONENT || item->status != UIRES_RESOLVED )
            continue;

        root = ui_loader_get_interface_component(interfaces, item->component_id);
        if( !root )
        {
            printf(
                "ui_loader_submit: RS component %d not found in interfaces\n", item->component_id);
            continue;
        }

        ui_loader_bake_rs_subtree(
            state, tree, scene, media, interfaces, -1, root, root->x, root->y);
        printf("ui_loader_submit: baked RS component %d\n", item->component_id);
    }
}

static int
ui_loader_pending_archive_index(
    struct UILoaderState* state,
    int table_id,
    int archive_id)
{
    for( int i = 0; i < state->pending_archive_count; i++ )
    {
        if( state->pending_archives[i].table_id == table_id &&
            state->pending_archives[i].archive_id == archive_id )
            return i;
    }

    if( state->pending_archive_count >= UI_LOADER_MAX_PENDING_ARCHIVES )
        return -1;

    int idx = state->pending_archive_count++;
    state->pending_archives[idx].table_id = table_id;
    state->pending_archives[idx].archive_id = archive_id;
    state->pending_archives[idx].received = false;
    state->pending_archives[idx].io_dispatched = false;
    return idx;
}

static bool
ui_loader_request_config_archive(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue,
    int archive_id)
{
    int table_id = CACHE_DAT_CONFIGS;
    int idx = ui_loader_pending_archive_index(state, table_id, archive_id);
    struct UILoaderPendingArchive* pending;

    if( idx < 0 )
        return false;

    pending = &state->pending_archives[idx];
    if( !pending->io_dispatched )
    {
        LibToriRS_IOQueuePush(io_queue, table_id, archive_id, 0);
        pending->io_dispatched = true;
        printf("UI_Process: queued cache IO table=%d archive=%d\n", table_id, archive_id);
    }
    return true;
}

static bool
ui_loader_resolve_archive_ids(
    const char* table,
    const char* archive,
    int* out_table_id,
    int* out_archive_id)
{
    if( !table || !archive || !out_table_id || !out_archive_id )
        return false;

    if( strcmp(table, "configs") == 0 )
    {
        *out_table_id = CACHE_DAT_CONFIGS;
        if( strcmp(archive, "media") == 0 )
        {
            *out_archive_id = CONFIG_DAT_MEDIA_2D_GRAPHICS;
            return true;
        }
    }
    return false;
}

static bool
ui_loader_all_pending_archives_received(struct UILoaderState* state)
{
    if( state->pending_archive_count <= 0 )
        return true;

    for( int i = 0; i < state->pending_archive_count; i++ )
    {
        if( !state->pending_archives[i].received )
            return false;
    }
    return true;
}

static void
ui_loader_decode_item(
    struct UILoaderState* state,
    struct UIResourceQueueItem* item)
{
    struct Dat1BuildCache* buildcache = state ? state->buildcache : NULL;
    struct FileListDat* filelist;

    filelist = dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    if( !filelist )
    {
        item->status = UIRES_ERROR;
        item->error_code = -4;
        item->state = UIRES_STATE_FAILED;
        return;
    }

    int start_atlas = item->atlas_use_count ? 0 : item->atlas_index;
    int count = item->atlas_use_count && item->atlas_count > 0 ? item->atlas_count : 1;
    struct ToriDraw_Sprite** sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
    {
        item->status = UIRES_ERROR;
        item->error_code = -1;
        item->state = UIRES_STATE_FAILED;
        return;
    }

    int loaded = 0;
    for( int j = 0; j < count; j++ )
    {
        int atlas_index = start_atlas + j;
        sprites[j] = ui_loader_decode_sprite(filelist, item, atlas_index);
        if( !sprites[j] )
        {
            sprites[j] = ui_loader_placeholder_sprite(32, 32);
            if( sprites[j] )
                printf(
                    "UI_Process: decode failed for %s atlas %d, using placeholder\n",
                    item->name,
                    atlas_index);
        }
        if( sprites[j] )
            loaded++;
    }

    if( loaded <= 0 )
    {
        free(sprites);
        item->status = UIRES_ERROR;
        item->error_code = -2;
        item->state = UIRES_STATE_FAILED;
        printf("UI_Process: failed to load any sprites for %s\n", item->name);
        return;
    }

    item->result_sprites = sprites;
    item->result_count = count;
    item->status = UIRES_RESOLVED;
    item->state = UIRES_STATE_DONE;
    printf("UI_Process: resolved sprite %s (%d decoded)\n", item->name, loaded);
}

static void
ui_loader_step_sprite(
    struct UILoaderState* state,
    struct UIResourceQueueItem* item,
    struct LibToriRS_IOQueue* io_queue)
{
    struct Dat1BuildCache* buildcache = state ? state->buildcache : NULL;

    switch( item->state )
    {
    case UIRES_STATE_NEED_ARCHIVE:
    {
        int table_id = 0;
        int archive_id = 0;
        if( !ui_loader_resolve_archive_ids(item->table, item->archive, &table_id, &archive_id) )
        {
            item->source_archive_index = -1;
            item->status = UIRES_ERROR;
            item->error_code = -5;
            item->state = UIRES_STATE_FAILED;
            printf(
                "UI_Process: unknown archive %s/%s for sprite %s\n",
                item->table,
                item->archive,
                item->name);
            return;
        }

        int idx = ui_loader_pending_archive_index(state, table_id, archive_id);
        if( idx < 0 )
        {
            item->status = UIRES_ERROR;
            item->error_code = -6;
            item->state = UIRES_STATE_FAILED;
            return;
        }

        item->source_archive_index = idx;
        struct UILoaderPendingArchive* pending = &state->pending_archives[idx];

        if( dat1_buildcache_get_media_2d_graphics_jagfile(buildcache) )
        {
            pending->received = true;
            item->state = UIRES_STATE_DECODE;
            return;
        }

        if( !pending->io_dispatched )
        {
            LibToriRS_IOQueuePush(io_queue, pending->table_id, pending->archive_id, 0);
            pending->io_dispatched = true;
            printf(
                "UI_Process: queued cache IO table=%d archive=%d\n",
                pending->table_id,
                pending->archive_id);
        }
        item->state = UIRES_STATE_WAIT_ARCHIVE;
        return;
    }

    case UIRES_STATE_WAIT_ARCHIVE:
        if( item->source_archive_index < 0 ||
            item->source_archive_index >= state->pending_archive_count )
            return;

        if( !state->pending_archives[item->source_archive_index].received )
            return;

        item->state = UIRES_STATE_DECODE;
        /* fall through */

    case UIRES_STATE_DECODE:
        ui_loader_decode_item(state, item);
        break;

    default:
        break;
    }
}

static void
ui_loader_step_rs_component(
    struct UILoaderState* state,
    struct UIResourceQueueItem* item,
    struct LibToriRS_IOQueue* io_queue)
{
    struct Dat1BuildCache* buildcache = state ? state->buildcache : NULL;
    struct CacheDatConfigComponentList* interfaces;
    struct CacheDatConfigComponent* comp;

    if( item->state == UIRES_STATE_DONE || item->state == UIRES_STATE_FAILED )
        return;

    if( !buildcache )
    {
        item->status = UIRES_ERROR;
        item->error_code = -9;
        item->state = UIRES_STATE_FAILED;
        return;
    }

    switch( item->state )
    {
    case UIRES_STATE_NEED_ARCHIVE:
        if( !dat1_buildcache_get_interfaces(buildcache) )
        {
            if( !ui_loader_request_config_archive(state, io_queue, CONFIG_DAT_INTERFACES) )
            {
                item->status = UIRES_ERROR;
                item->error_code = -6;
                item->state = UIRES_STATE_FAILED;
                return;
            }
        }

        if( !dat1_buildcache_get_media_2d_graphics_jagfile(buildcache) )
        {
            if( !ui_loader_request_config_archive(state, io_queue, CONFIG_DAT_MEDIA_2D_GRAPHICS) )
            {
                item->status = UIRES_ERROR;
                item->error_code = -6;
                item->state = UIRES_STATE_FAILED;
                return;
            }
        }

        item->state = UIRES_STATE_WAIT_ARCHIVE;
        return;

    case UIRES_STATE_WAIT_ARCHIVE:
        if( !dat1_buildcache_get_interfaces(buildcache) )
            return;
        if( !dat1_buildcache_get_media_2d_graphics_jagfile(buildcache) )
            return;
        item->state = UIRES_STATE_NEED_DEPS;
        /* fall through */

    case UIRES_STATE_NEED_DEPS:
        interfaces = dat1_buildcache_get_interfaces(buildcache);
        comp = ui_loader_get_interface_component(interfaces, item->component_id);
        if( !comp )
        {
            item->status = UIRES_ERROR;
            item->error_code = -7;
            item->state = UIRES_STATE_FAILED;
            printf("UI_Process: RS component %d not found in interfaces\n", item->component_id);
            return;
        }

        ui_loader_collect_rs_deps_from_component(state, interfaces, comp);

        if( state->need_fonts && state->ui_scene &&
            ui_scene_font_find_id(state->ui_scene, "p11") >= 0 )
            state->fonts_received = true;

        if( state->need_fonts && !state->fonts_received && !state->fonts_io_dispatched )
        {
            if( !ui_loader_request_config_archive(state, io_queue, CONFIG_DAT_TITLE_AND_FONTS) )
            {
                item->status = UIRES_ERROR;
                item->error_code = -6;
                item->state = UIRES_STATE_FAILED;
                return;
            }
            state->fonts_io_dispatched = true;
        }

        if( !ui_loader_queue_pending_models(state, io_queue) )
        {
            item->status = UIRES_ERROR;
            item->error_code = -6;
            item->state = UIRES_STATE_FAILED;
            return;
        }

        if( state->pending_model_count > 0 || (state->need_fonts && !state->fonts_received) )
        {
            item->state = UIRES_STATE_WAIT_DEPS;
            return;
        }

        item->status = UIRES_RESOLVED;
        item->state = UIRES_STATE_DONE;
        printf("UI_Process: resolved RS component %d (no extra deps)\n", item->component_id);
        return;

    case UIRES_STATE_WAIT_DEPS:
        if( !ui_loader_all_pending_models_received(state) )
            return;
        if( !ui_loader_fonts_ready(state) )
            return;

        item->status = UIRES_RESOLVED;
        item->state = UIRES_STATE_DONE;
        printf("UI_Process: resolved RS component %d\n", item->component_id);
        break;

    default:
        break;
    }
}

static void
ui_loader_step_item(
    struct UILoaderState* state,
    struct UIResourceQueueItem* item,
    struct LibToriRS_IOQueue* io_queue)
{
    if( ui_loader_item_is_terminal(item) )
        return;

    switch( item->kind )
    {
    case UIRES_KIND_SPRITE:
        ui_loader_step_sprite(state, item, io_queue);
        break;
    case UIRES_KIND_RS_COMPONENT:
        ui_loader_step_rs_component(state, item, io_queue);
        break;
    default:
        item->status = UIRES_ERROR;
        item->error_code = -8;
        item->state = UIRES_STATE_FAILED;
        break;
    }
}

static void
ui_loader_process_revconfig(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    struct UIResourceQueue* rq = state->queue;
    if( !rq )
        return;

    state->pending_archive_count = 0;
    state->pending_model_count = 0;
    state->need_fonts = false;
    state->fonts_received = false;
    state->fonts_io_dispatched = false;
    ui_resource_queue_clear(rq);
    ui_loader_build(state);

    for( int i = 0; i < rq->count; i++ )
        ui_loader_step_item(state, &rq->items[i], io_queue);
}

static void
ui_loader_process_resource_queue(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    struct UIResourceQueue* rq = state->queue;
    if( !rq )
        return;

    for( int i = 0; i < rq->count; i++ )
        ui_loader_step_item(state, &rq->items[i], io_queue);
}

static void
ui_loader_consume_resolved_cache_io(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    struct Dat1BuildCache* buildcache = state ? state->buildcache : NULL;

    if( !state || !io_queue || !buildcache )
        return;

    for( int i = io_queue->read_head; i < io_queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* io_item = &io_queue->items[i];
        if( io_item->kind != TORIRSIO_KIND_CACHE )
            continue;
        if( io_item->status != TORIRSIO_RESOLVED || !io_item->data )
            continue;

        for( int p = 0; p < state->pending_archive_count; p++ )
        {
            struct UILoaderPendingArchive* pending = &state->pending_archives[p];
            if( pending->received )
                continue;
            if( pending->table_id != io_item->table_id ||
                pending->archive_id != io_item->archive_id )
                continue;

            if( pending->table_id == CACHE_DAT_CONFIGS &&
                pending->archive_id == CONFIG_DAT_MEDIA_2D_GRAPHICS )
            {
                struct FileListDat* filelist =
                    filelist_dat_new_from_cache_dat_archive((struct CacheDatArchive*)io_item->data);
                dat1_buildcache_set_media_2d_graphics_jagfile(buildcache, filelist);
                printf("UI_Process: received media archive filelist=%p\n", (void*)filelist);
            }
            else if(
                pending->table_id == CACHE_DAT_CONFIGS &&
                pending->archive_id == CONFIG_DAT_INTERFACES )
            {
                struct CacheDatConfigComponentList* interfaces = NULL;
                if( ui_loader_decode_interfaces_from_archive(
                        (struct CacheDatArchive*)io_item->data, &interfaces) )
                {
                    dat1_buildcache_set_interfaces(buildcache, interfaces);
                    printf(
                        "UI_Process: received interfaces archive (%d components)\n",
                        interfaces ? interfaces->components_count : 0);
                }
                else
                    printf("UI_Process: failed to decode interfaces archive\n");
            }
            else if(
                pending->table_id == CACHE_DAT_CONFIGS &&
                pending->archive_id == CONFIG_DAT_TITLE_AND_FONTS )
            {
                struct FileListDat* filelist =
                    filelist_dat_new_from_cache_dat_archive((struct CacheDatArchive*)io_item->data);
                if( filelist && state->ui_scene )
                {
                    ui_scene_load_fonts_from_title_archive(state->ui_scene, filelist);
                    printf("UI_Process: loaded fonts from title archive\n");
                }
                if( filelist )
                    filelist_dat_free(filelist);
                state->fonts_received = true;
            }

            pending->received = true;
            cache_dat_archive_free((struct CacheDatArchive*)io_item->data);
            io_item->data = NULL;
            printf(
                "UI_Process: received cache archive table=%d archive=%d\n",
                pending->table_id,
                pending->archive_id);
            break;
        }

        if( io_item->kind != TORIRSIO_KIND_CACHE )
            continue;
        if( io_item->status != TORIRSIO_RESOLVED || !io_item->data )
            continue;
        if( io_item->table_id != CACHE_DAT_MODELS )
            continue;

        for( int p = 0; p < state->pending_model_count; p++ )
        {
            struct UILoaderPendingModel* pending = &state->pending_models[p];
            if( pending->received )
                continue;
            if( pending->model_id != io_item->archive_id )
                continue;

            if( state->gamecache && state->buildcache )
            {
                struct CacheModel* model =
                    model_new_from_dat_archive((struct CacheDatArchive*)io_item->data, pending->model_id);
                if( model )
                {
                    struct CacheModel* copy = model_new_copy(model);
                    model_free(model);
                    if( copy )
                    {
                        struct ToriDraw_Model* td = ToriDraw_ModelNewFromCacheModel(copy);
                        model_free(copy);
                        if( td )
                        {
                            struct ToriDraw_ModelHandle hnd = {
                                .kind = TORIDRAWMK_MODEL,
                                .u.model.model = td,
                            };
                            gamecache_model_add(state->gamecache, pending->model_id, hnd);
                            printf(
                                "UI_Process: loaded UI model %d into gamecache\n",
                                pending->model_id);
                        }
                    }
                }
            }

            pending->received = true;
            cache_dat_archive_free((struct CacheDatArchive*)io_item->data);
            io_item->data = NULL;
            printf("UI_Process: received model archive id=%d\n", pending->model_id);
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Work-queue process loop                                                    */
/* -------------------------------------------------------------------------- */

void
ui_loader_process(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    enum UILoaderWorkKind work_kind;

    if( !state || !io_queue || !state->buildcache )
        return;

    ui_loader_consume_resolved_cache_io(state, io_queue);
    io_queue->read_head = io_queue->count;

    if( ui_loader_work_empty(state) && ui_loader_resource_queue_has_incomplete(state) )
    {
        if( !ui_loader_work_contains(state, UILOADER_WORK_PROCESS_RESOURCE_QUEUE) )
            ui_loader_work_push(state, UILOADER_WORK_PROCESS_RESOURCE_QUEUE);
    }

    while( ui_loader_work_pop(state, &work_kind) )
    {
        switch( work_kind )
        {
        case UILOADER_WORK_PROCESS_REVCONFIG:
            ui_loader_process_revconfig(state, io_queue);
            break;
        case UILOADER_WORK_PROCESS_RESOURCE_QUEUE:
            ui_loader_process_resource_queue(state, io_queue);
            break;
        }
    }

    state->done = ui_loader_work_empty(state) && !ui_loader_resource_queue_has_incomplete(state) &&
                  ui_loader_all_pending_archives_received(state) &&
                  ui_loader_all_pending_models_received(state) && ui_loader_fonts_ready(state);
}

/* -------------------------------------------------------------------------- */
/* Lifecycle and accessors                                                    */
/* -------------------------------------------------------------------------- */

struct UILoaderState*
ui_loader_state_new(void)
{
    struct UILoaderState* state = calloc(1, sizeof(struct UILoaderState));
    if( !state )
        return NULL;

    state->sprite_map_buffer = malloc(512 * sizeof(struct SpriteMapEntry));
    state->component_map_buffer = malloc(512 * sizeof(struct ComponentMapEntry));
    if( !state->sprite_map_buffer || !state->component_map_buffer )
    {
        ui_loader_state_free(state);
        return NULL;
    }

    struct DashMapConfig sprite_cfg = {
        .buffer = state->sprite_map_buffer,
        .buffer_size = 512 * sizeof(struct SpriteMapEntry),
        .key_size = 64,
        .entry_size = sizeof(struct SpriteMapEntry),
    };
    struct DashMapConfig component_cfg = {
        .buffer = state->component_map_buffer,
        .buffer_size = 512 * sizeof(struct ComponentMapEntry),
        .key_size = 64,
        .entry_size = sizeof(struct ComponentMapEntry),
    };

    state->sprite_map = dashmap_new(&sprite_cfg, 0);
    state->component_map = dashmap_new(&component_cfg, 0);
    if( !state->sprite_map || !state->component_map )
    {
        ui_loader_state_free(state);
        return NULL;
    }

    state->queue = ui_resource_queue_new();
    state->revconfig = revconfig_buffer_new(4096);
    if( !state->queue || !state->revconfig )
    {
        ui_loader_state_free(state);
        return NULL;
    }

    ui_loader_work_clear(state);
    ui_loader_work_push(state, UILOADER_WORK_PROCESS_REVCONFIG);
    state->done = false;
    return state;
}

void
ui_loader_state_free(struct UILoaderState* state)
{
    if( !state )
        return;
    if( state->sprite_map )
        dashmap_free(state->sprite_map);
    if( state->component_map )
        dashmap_free(state->component_map);
    free(state->sprite_map_buffer);
    free(state->component_map_buffer);
    if( state->queue )
        ui_resource_queue_free(state->queue);
    if( state->revconfig )
        revconfig_buffer_free(state->revconfig);
    free(state);
}

void
ui_loader_reset(struct UILoaderState* state)
{
    if( !state )
        return;

    ui_loader_revconfig_reset(state);
    if( state->queue )
        ui_resource_queue_clear(state->queue);
    state->pending_archive_count = 0;
    state->pending_model_count = 0;
    state->need_fonts = false;
    state->fonts_received = false;
    state->fonts_io_dispatched = false;
    ui_loader_work_clear(state);
    ui_loader_work_push(state, UILOADER_WORK_PROCESS_REVCONFIG);
    state->done = false;
}

struct RevConfigBuffer*
ui_loader_revconfig(struct UILoaderState* state)
{
    return state ? state->revconfig : NULL;
}

void
ui_loader_revconfig_reset(struct UILoaderState* state)
{
    if( !state )
        return;
    if( state->revconfig )
        revconfig_buffer_free(state->revconfig);
    state->revconfig = revconfig_buffer_new(4096);
}

bool
ui_loader_ready(struct UILoaderState* state)
{
    return state && state->done;
}

void
ui_loader_set_buildcache(
    struct UILoaderState* state,
    struct Dat1BuildCache* buildcache)
{
    if( !state )
        return;
    state->buildcache = buildcache;
}

void
ui_loader_set_gamecache(
    struct UILoaderState* state,
    struct GameCache* gamecache)
{
    if( !state )
        return;
    state->gamecache = gamecache;
}

void
ui_loader_set_ui_scene(
    struct UILoaderState* state,
    struct UIScene* scene)
{
    if( !state )
        return;
    state->ui_scene = scene;
}

void
ui_loader_queue_rs_component(
    struct UILoaderState* state,
    int component_id)
{
    if( !state || !state->queue )
        return;

    if( !ui_resource_queue_push_rs_component(state->queue, component_id) )
    {
        printf("ui_loader_queue_rs_component: failed to queue component %d\n", component_id);
        return;
    }

    state->pending_model_count = 0;
    state->done = false;
    if( !ui_loader_work_contains(state, UILOADER_WORK_PROCESS_RESOURCE_QUEUE) )
        ui_loader_work_push(state, UILOADER_WORK_PROCESS_RESOURCE_QUEUE);
}

/* -------------------------------------------------------------------------- */
/* Submit phase                                                               */
/* -------------------------------------------------------------------------- */

static void
apply_layout(
    struct UILoaderState* state,
    struct UITree* tree)
{
    for( int i = 0; i < state->layout_entry_count; i++ )
    {
        struct LayoutItem* le = &state->layout_entries[i];
        struct ComponentMapEntry* comp =
            dashmap_search(state->component_map, le->component, DASHMAP_FIND);
        if( !comp )
        {
            printf("ui_loader: layout component not found: %s\n", le->component);
            continue;
        }

        int32_t idx = -1;
        switch( comp->type )
        {
        case UIELEM_BUILTIN_WORLD:
            idx = uitree_push_world(
                tree, -1, le->x, le->y, comp->width, comp->height, comp->level_mask);
            break;
        case UIELEM_BUILTIN_SPRITE:
            if( le->flags != 0 )
            {
                idx = uitree_push_sprite_relative(
                    tree,
                    -1,
                    comp->sprite_id,
                    comp->sprite_index,
                    le->flags,
                    le->top,
                    le->right,
                    le->bottom,
                    le->left,
                    comp->width,
                    comp->height);
            }
            else
            {
                idx = uitree_push_sprite_xy(
                    tree,
                    -1,
                    comp->sprite_id,
                    comp->sprite_index,
                    le->x,
                    le->y,
                    comp->width,
                    comp->height);
            }
            break;
        default:
            break;
        }

        if( le->always_dirty && idx >= 0 )
            tree->components[idx].always_dirty = 1;
    }
}

bool
ui_loader_submit(
    struct UILoaderState* state,
    struct UITree* tree,
    struct UIScene* scene)
{
    struct UIResourceQueue* queue;

    queue = state->queue;

    tree->component_count = 0;
    tree->root_index = -1;

    for( int i = 0; i < queue->count; i++ )
    {
        struct UIResourceQueueItem* item = &queue->items[i];
        if( item->kind != UIRES_KIND_SPRITE )
            continue;
        if( item->status != UIRES_RESOLVED || !item->result_sprites || item->result_count <= 0 )
        {
            printf("ui_loader_submit: skip unresolved sprite %s\n", item->name);
            continue;
        }

        int element_id = ui_scene_element_acquire_with_sprites(
            scene, item->result_sprites, item->result_count, false, item->name);
        if( element_id < 0 )
            continue;

        item->result_sprites = NULL;
        item->result_count = 0;

        struct SpriteMapEntry* sm = dashmap_search(state->sprite_map, item->name, DASHMAP_INSERT);
        if( sm )
        {
            strncpy(sm->name, item->name, sizeof(sm->name) - 1);
            sm->scene_id = element_id;
            sm->atlas_index = item->atlas_index;
        }
    }

    for( int i = 0; i < state->component_count; i++ )
    {
        struct ComponentMapEntry* comp = &state->components[i];
        if( comp->type != UIELEM_BUILTIN_SPRITE || comp->sprite_name[0] == '\0' )
            continue;

        char sprite_key[64];
        int atlas_index = 0;
        parse_sprite_bracket(comp->sprite_name, sprite_key, &atlas_index);

        struct SpriteMapEntry* sm = dashmap_search(state->sprite_map, sprite_key, DASHMAP_FIND);
        if( sm )
        {
            comp->sprite_id = sm->scene_id;
            comp->sprite_index = atlas_index;

            struct ComponentMapEntry* ent =
                dashmap_search(state->component_map, comp->name, DASHMAP_FIND);
            if( ent )
            {
                ent->sprite_id = comp->sprite_id;
                ent->sprite_index = comp->sprite_index;
            }
        }
        else
            printf(
                "ui_loader_submit: sprite not resolved for component %s (%s)\n",
                comp->name,
                sprite_key);
    }

    apply_layout(state, tree);
    ui_loader_bake_rs_roots(state, tree, scene);
    printf("ui_loader_submit: uitree has %u components\n", tree->component_count);
    return true;
}
