#include "ui_loader.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "gamecache/toridraw_cachesprite.h"
#include "graphics/dashmap.h"
#include "osrs/revconfig/uitree.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/filelist.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"
#include "src/osrs/rscache/tables_dat/pix32.h"
#include "src/osrs/rscache/tables_dat/pix8.h"
#include "ui_resource_queue.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_LOADER_MAX_LAYOUT_ENTRIES 64
#define UI_LOADER_MAX_PENDING_ARCHIVES 16

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
    struct FileListDat* filelist;
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

    struct UIResourceQueue* queue;
    struct RevConfigBuffer* revconfig;
    enum UILoaderPhase phase;
    struct UILoaderPendingArchive pending_archives[UI_LOADER_MAX_PENDING_ARCHIVES];
    int pending_archive_count;
};

static void
ui_loader_free_pending_filelists(struct UILoaderState* state);

static bool
ui_loader_build(struct UILoaderState* state);

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

    state->phase = UI_LOADER_PHASE_BUILD;
    return state;
}

void
ui_loader_state_free(struct UILoaderState* state)
{
    if( !state )
        return;
    ui_loader_free_pending_filelists(state);
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
    ui_loader_free_pending_filelists(state);
    state->phase = UI_LOADER_PHASE_BUILD;
    state->pending_archive_count = 0;
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
    return state && state->phase == UI_LOADER_PHASE_DONE;
}

static uint32_t
load_kind(const char* str)
{
    if( strcmp(str, "sprite") == 0 )
        return 1;
    if( strcmp(str, "component") == 0 )
        return 2;
    if( strcmp(str, "layout") == 0 )
        return 3;
    return 0;
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

static bool
ui_loader_build(struct UILoaderState* state)
{
    struct UIResourceQueue* queue;
    struct RevConfigBuffer* revconfig;

    if( !state )
        return false;

    queue = state->queue;
    revconfig = state->revconfig;
    if( !queue || !revconfig )
        return false;

    state->layout_entry_count = 0;
    state->component_count = 0;

    uint32_t kind = 0;
    enum
    {
        KIND_NONE = 0,
        KIND_SPRITE = 1,
        KIND_COMPONENT = 2,
        KIND_LAYOUT = 3,
    };

    struct UIResourceQueueItem pending_sprite = { 0 };
    struct ComponentMapEntry pending_component = { 0 };
    struct LayoutItem pending_layout = { 0 };
    bool layout_has_entry = false;

    for( uint32_t fi = 0; fi < revconfig->field_count; fi++ )
    {
        struct RevConfigField* field = &revconfig->fields[fi];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            kind = load_kind(field->value);
            memset(&pending_sprite, 0, sizeof(pending_sprite));
            memset(&pending_component, 0, sizeof(pending_component));
            memset(&pending_layout, 0, sizeof(pending_layout));
            layout_has_entry = false;
            pending_sprite.kind = UIRES_KIND_SPRITE;
            pending_component.sprite_id = -1;
            pending_component.level_mask = 0xFu;
            break;

        case RCFIELD_ITEMNAME:
            if( kind == KIND_SPRITE )
                strncpy(pending_sprite.name, field->value, sizeof(pending_sprite.name) - 1);
            else if( kind == KIND_COMPONENT )
                strncpy(pending_component.name, field->value, sizeof(pending_component.name) - 1);
            break;

        case RCFIELD_ITEMDONE:
            if( kind == KIND_SPRITE )
            {
                pending_sprite.status = UIRES_PENDING;
                ui_resource_queue_push_sprite(queue, &pending_sprite);
            }
            else if( kind == KIND_COMPONENT )
            {
                struct ComponentMapEntry* ent =
                    dashmap_search(state->component_map, pending_component.name, DASHMAP_INSERT);
                if( ent )
                    *ent = pending_component;
                if( state->component_count < 128 )
                    state->components[state->component_count++] = pending_component;
            }
            else if( kind == KIND_LAYOUT )
            {
                if( layout_has_entry && state->layout_entry_count < UI_LOADER_MAX_LAYOUT_ENTRIES )
                {
                    state->layout_entries[state->layout_entry_count++] = pending_layout;
                }
            }
            kind = KIND_NONE;
            break;

        case RCFIELD_CACHE_TABLE:
            strncpy(pending_sprite.table, field->value, sizeof(pending_sprite.table) - 1);
            break;
        case RCFIELD_CACHE_ARCHIVE:
            strncpy(pending_sprite.archive, field->value, sizeof(pending_sprite.archive) - 1);
            break;
        case RCFIELD_CACHE_CONTAINER:
            strncpy(pending_sprite.container, field->value, sizeof(pending_sprite.container) - 1);
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            strncpy(
                pending_sprite.index_filename,
                field->value,
                sizeof(pending_sprite.index_filename) - 1);
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            strncpy(
                pending_sprite.data_filename,
                field->value,
                sizeof(pending_sprite.data_filename) - 1);
            break;
        case RCFIELD_CACHE_FORMAT:
            strncpy(pending_sprite.format, field->value, sizeof(pending_sprite.format) - 1);
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            pending_sprite.atlas_index = atoi(field->value);
            pending_sprite.atlas_use_count = false;
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            pending_sprite.atlas_count = atoi(field->value);
            pending_sprite.atlas_use_count = true;
            break;

        case RCFIELD_UICOMPONENT_TYPE:
            pending_component.type = component_type_from_string(field->value);
            break;
        case RCFIELD_UICOMPONENT_SPRITE:
            strncpy(
                pending_component.sprite_name,
                field->value,
                sizeof(pending_component.sprite_name) - 1);
            break;
        case RCFIELD_UICOMPONENT_WIDTH:
            pending_component.width = atoi(field->value);
            break;
        case RCFIELD_UICOMPONENT_HEIGHT:
            pending_component.height = atoi(field->value);
            break;
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            pending_component.level_mask = 0xFu;
            break;

        case RCFIELD_UILAYOUT_COMPONENT:
            if( layout_has_entry && state->layout_entry_count < UI_LOADER_MAX_LAYOUT_ENTRIES )
                state->layout_entries[state->layout_entry_count++] = pending_layout;
            memset(&pending_layout, 0, sizeof(pending_layout));
            strncpy(pending_layout.component, field->value, sizeof(pending_layout.component) - 1);
            layout_has_entry = true;
            break;
        case RCFIELD_UILAYOUT_X:
            pending_layout.x = atoi(field->value);
            break;
        case RCFIELD_UILAYOUT_Y:
            pending_layout.y = atoi(field->value);
            break;
        case RCFIELD_UILAYOUT_TOP:
            pending_layout.top = atoi(field->value);
            pending_layout.flags |= STATIC_UI_RELATIVE_FLAG_TOP;
            break;
        case RCFIELD_UILAYOUT_LEFT:
            pending_layout.left = atoi(field->value);
            pending_layout.flags |= STATIC_UI_RELATIVE_FLAG_LEFT;
            break;
        case RCFIELD_UILAYOUT_BOTTOM:
            pending_layout.bottom = atoi(field->value);
            pending_layout.flags |= STATIC_UI_RELATIVE_FLAG_BOTTOM;
            break;
        case RCFIELD_UILAYOUT_RIGHT:
            pending_layout.right = atoi(field->value);
            pending_layout.flags |= STATIC_UI_RELATIVE_FLAG_RIGHT;
            break;
        case RCFIELD_UILAYOUT_DIRTY:
            pending_layout.always_dirty = 1;
            break;
        case RCFIELD_UILAYOUT_NULL:
            if( layout_has_entry && state->layout_entry_count < UI_LOADER_MAX_LAYOUT_ENTRIES )
                state->layout_entries[state->layout_entry_count++] = pending_layout;
            memset(&pending_layout, 0, sizeof(pending_layout));
            layout_has_entry = false;
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

bool
ui_loader_submit(
    struct UILoaderState* state,
    struct UITree* tree,
    struct UIScene* scene)
{
    struct UIResourceQueue* queue;

    if( !state || !tree || !scene )
        return false;

    queue = state->queue;
    if( !queue )
        return false;

    tree->component_count = 0;
    tree->root_index = -1;

    for( int i = 0; i < queue->count; i++ )
    {
        struct UIResourceQueueItem* item = &queue->items[i];
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
    printf("ui_loader_submit: uitree has %u components\n", tree->component_count);
    return true;
}

static void
ui_loader_free_pending_filelists(struct UILoaderState* state)
{
    if( !state )
        return;

    for( int i = 0; i < state->pending_archive_count; i++ )
    {
        if( state->pending_archives[i].filelist )
        {
            filelist_dat_free(state->pending_archives[i].filelist);
            state->pending_archives[i].filelist = NULL;
        }
    }
}

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

        struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_cache_pix8_palette(pix8_palette);
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

        struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_cache_pix32(pix32);
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
    return toridraw_sprite_new_from_argb_owned(pixels, w, h);
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
    state->pending_archives[idx].filelist = NULL;
    return idx;
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
ui_loader_fill_pending_sprite_requests(struct UILoaderState* state)
{
    struct UIResourceQueue* rq = state->queue;
    if( !rq )
        return;

    for( int i = 0; i < rq->count; i++ )
    {
        struct UIResourceQueueItem* item = &rq->items[i];
        if( item->status != UIRES_PENDING )
            continue;

        if( item->source_archive_index < 0 ||
            item->source_archive_index >= state->pending_archive_count )
            continue;

        if( !state->pending_archives[item->source_archive_index].received )
            continue;

        struct FileListDat* filelist = state->pending_archives[item->source_archive_index].filelist;
        if( !filelist )
            continue;

        int start_atlas = item->atlas_use_count ? 0 : item->atlas_index;
        int count = item->atlas_use_count && item->atlas_count > 0 ? item->atlas_count : 1;
        struct ToriDraw_Sprite** sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
        if( !sprites )
        {
            item->status = UIRES_ERROR;
            item->error_code = -1;
            continue;
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
            printf("UI_Process: failed to load any sprites for %s\n", item->name);
            continue;
        }

        item->result_sprites = sprites;
        item->result_count = count;
        item->status = UIRES_RESOLVED;
        printf("UI_Process: resolved sprite %s (%d decoded)\n", item->name, loaded);
    }
}

static void
ui_loader_consume_resolved_cache_io(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !state || !io_queue )
        return;

    for( int i = 0; i < io_queue->count; i++ )
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

            pending->filelist =
                filelist_dat_new_from_cache_dat_archive((struct CacheDatArchive*)io_item->data);
            pending->received = true;
            cache_dat_archive_free((struct CacheDatArchive*)io_item->data);
            io_item->data = NULL;
            printf(
                "UI_Process: received cache archive table=%d archive=%d filelist=%p\n",
                pending->table_id,
                pending->archive_id,
                (void*)pending->filelist);
            break;
        }
    }
}

void
ui_loader_process(
    struct UILoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !state || !io_queue )
        return;

    switch( state->phase )
    {
    case UI_LOADER_PHASE_BUILD:
    {
        ui_loader_free_pending_filelists(state);
        state->pending_archive_count = 0;
        if( state->queue )
            ui_resource_queue_clear(state->queue);
        ui_loader_build(state);

        struct UIResourceQueue* rq = state->queue;
        for( int i = 0; i < rq->count; i++ )
        {
            struct UIResourceQueueItem* item = &rq->items[i];
            int table_id = 0;
            int archive_id = 0;
            if( !ui_loader_resolve_archive_ids(item->table, item->archive, &table_id, &archive_id) )
            {
                item->source_archive_index = -1;
                printf(
                    "UI_Process: unknown archive %s/%s for sprite %s\n",
                    item->table,
                    item->archive,
                    item->name);
                continue;
            }

            item->source_archive_index =
                ui_loader_pending_archive_index(state, table_id, archive_id);
        }

        for( int p = 0; p < state->pending_archive_count; p++ )
        {
            struct UILoaderPendingArchive* pending = &state->pending_archives[p];
            LibToriRS_IOQueuePush(io_queue, pending->table_id, pending->archive_id, 0);
            printf(
                "UI_Process: queued cache IO table=%d archive=%d\n",
                pending->table_id,
                pending->archive_id);
        }

        if( state->pending_archive_count == 0 )
            state->phase = UI_LOADER_PHASE_DONE;
        else
            state->phase = UI_LOADER_PHASE_WAIT;
    }
    break;

    case UI_LOADER_PHASE_WAIT:
        ui_loader_consume_resolved_cache_io(state, io_queue);
        ui_loader_fill_pending_sprite_requests(state);
        if( ui_loader_all_pending_archives_received(state) )
        {
            ui_loader_free_pending_filelists(state);
            state->phase = UI_LOADER_PHASE_DONE;
        }
        break;

    case UI_LOADER_PHASE_DONE:
    default:
        break;
    }
}
