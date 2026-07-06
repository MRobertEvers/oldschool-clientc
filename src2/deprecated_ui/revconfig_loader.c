#include "revconfig_loader.h"

#include "../buildcache/dat1_buildcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "../ioqueue/libtorirs_ioqueue.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "graphics/dashmap.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "semantic/dat1_element.h"
#include "semantic/dat1_interface.h"
#include "semantic/dat1_sprite.h"
#include "ui_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Types                                                               */
/* ================================================================== */

enum RevConfigLoadItemKind
{
    REVCONFIG_ITEM_SPRITE = 0,
    REVCONFIG_ITEM_INTERFACE = 1,
    REVCONFIG_ITEM_MINIMAP = 2,
    REVCONFIG_ITEM_WORLD = 3,
    REVCONFIG_ITEM_INV = 4,
};

struct RevConfigLoadItem
{
    enum RevConfigLoadItemKind kind;
    union
    {
        struct Dat1_Sprite sprite;
        struct Dat1_Interface interface_;
    } u;
};

#define REVCONFIG_LOADER_MAX_ITEMS 256

struct RevConfigLoaderState
{
    struct RevConfigLoadItem items[REVCONFIG_LOADER_MAX_ITEMS];
    int item_count;
    bool queue_built;

    struct Dat1BuildCache* buildcache;
    struct ToriAuxLibCore* gamecache;
    struct UIScene* scene;
    struct RevConfigBuffer* revconfig;

    /* sprite name -> Dat1_SpriteMapEntry hashmap */
    void* sprite_map_buffer;
    struct DashMap* sprite_map;

    bool done;
};

/* ================================================================== */
/* Sprite map                                                          */
/* ================================================================== */

#define SPRITE_MAP_CAPACITY 512

static bool
sprite_map_create(struct RevConfigLoaderState* state)
{
    state->sprite_map_buffer = malloc(SPRITE_MAP_CAPACITY * sizeof(struct Dat1_SpriteMapEntry));
    if( !state->sprite_map_buffer )
        return false;

    struct DashMapConfig cfg = {
        .buffer = state->sprite_map_buffer,
        .buffer_size = SPRITE_MAP_CAPACITY * sizeof(struct Dat1_SpriteMapEntry),
        .key_size = 64,
        .entry_size = sizeof(struct Dat1_SpriteMapEntry),
    };
    state->sprite_map = dashmap_new(&cfg, 0);
    return state->sprite_map != NULL;
}

static int
sprite_map_find(
    struct DashMap* sm,
    const char* name)
{
    struct Dat1_SpriteMapEntry* entry = dashmap_search(sm, name, DASHMAP_FIND);
    return entry ? entry->scene_id : -1;
}

/* ================================================================== */
/* Queue helpers                                                        */
/* ================================================================== */

static struct RevConfigLoadItem*
queue_push(struct RevConfigLoaderState* state)
{
    if( state->item_count >= REVCONFIG_LOADER_MAX_ITEMS )
        return NULL;
    return &state->items[state->item_count++];
}

/* ================================================================== */
/* Revconfig parsing -> build load queue (first pass)                  */
/* ================================================================== */

enum BuildKind
{
    BUILD_NONE = 0,
    BUILD_SPRITE = 1,
    BUILD_COMPONENT = 2,
    BUILD_LAYOUT = 3,
};

struct BuildCtx
{
    enum BuildKind kind;
    struct Dat1_Sprite pending_sprite;
};

static void
build_begin(
    struct BuildCtx* ctx,
    const char* type_value)
{
    memset(ctx, 0, sizeof(*ctx));
    if( strcmp(type_value, "sprite") == 0 )
    {
        ctx->kind = BUILD_SPRITE;
        dat1_sprite_init(&ctx->pending_sprite);
    }
    /* component and layout items are not queued - re-read during uitree_build */
}

static void
build_apply_cache_field(
    struct BuildCtx* ctx,
    const struct RevConfigField* field)
{
    if( ctx->kind != BUILD_SPRITE )
        return;
    struct Dat1_Sprite* s = &ctx->pending_sprite;
    switch( field->kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(s->table, field->value, sizeof(s->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(s->archive, field->value, sizeof(s->archive) - 1);
        break;
    case RCFIELD_CACHE_CONTAINER:
        strncpy(s->container, field->value, sizeof(s->container) - 1);
        break;
    case RCFIELD_CACHE_INDEX_FILENAME:
        strncpy(s->index_filename, field->value, sizeof(s->index_filename) - 1);
        break;
    case RCFIELD_CACHE_DATA_FILENAME:
        strncpy(s->data_filename, field->value, sizeof(s->data_filename) - 1);
        break;
    case RCFIELD_CACHE_FORMAT:
        strncpy(s->format, field->value, sizeof(s->format) - 1);
        break;
    case RCFIELD_CACHE_ATLAS_INDEX:
        s->atlas_index = atoi(field->value);
        s->atlas_use_count = false;
        break;
    case RCFIELD_CACHE_ATLAS_COUNT:
        s->atlas_count = atoi(field->value);
        s->atlas_use_count = true;
        break;
    default:
        break;
    }
}

static void
build_set_name(
    struct BuildCtx* ctx,
    const char* value)
{
    if( ctx->kind == BUILD_SPRITE )
        strncpy(ctx->pending_sprite.name, value, sizeof(ctx->pending_sprite.name) - 1);
}

static void
build_finish(
    struct RevConfigLoaderState* state,
    struct BuildCtx* ctx)
{
    if( ctx->kind == BUILD_SPRITE )
    {
        struct RevConfigLoadItem* item = queue_push(state);
        if( item )
        {
            item->kind = REVCONFIG_ITEM_SPRITE;
            item->u.sprite = ctx->pending_sprite;
        }
    }
    ctx->kind = BUILD_NONE;
}

static void
revconfig_build_queue(struct RevConfigLoaderState* state)
{
    struct RevConfigBuffer* rc = state->revconfig;
    if( !rc )
        return;

    struct BuildCtx ctx = { 0 };

    for( uint32_t fi = 0; fi < rc->field_count; fi++ )
    {
        const struct RevConfigField* field = &rc->fields[fi];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            build_begin(&ctx, field->value);
            break;
        case RCFIELD_ITEMNAME:
            build_set_name(&ctx, field->value);
            break;
        case RCFIELD_ITEMDONE:
            build_finish(state, &ctx);
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

        /* component/layout/uicomponent fields: not queued; handled in uitree_build */
        default:
            break;
        }
    }

    printf("revconfig_loader: built queue with %d sprite(s)\n", state->item_count);
    state->queue_built = true;
}

/* ================================================================== */
/* Request coalescing                                                   */
/* ================================================================== */

#define MAX_COALESCED_REQUESTS 64

struct CoalescedRequest
{
    int table_id;
    int archive_id;
    int flags;
};

static bool
coalesced_contains(
    const struct CoalescedRequest* reqs,
    int count,
    int table_id,
    int archive_id,
    int flags)
{
    for( int i = 0; i < count; i++ )
    {
        if( reqs[i].table_id == table_id && reqs[i].archive_id == archive_id &&
            reqs[i].flags == flags )
            return true;
    }
    return false;
}

static void
revconfig_collect_requests(
    struct RevConfigLoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    struct CoalescedRequest pending[MAX_COALESCED_REQUESTS];
    int pending_count = 0;

    for( int i = 0; i < state->item_count; i++ )
    {
        struct RevConfigLoadItem* item = &state->items[i];
        struct Dat1_ArchiveRequest* waiting;
        int waiting_count;

        if( item->kind == REVCONFIG_ITEM_SPRITE )
        {
            waiting = item->u.sprite.waiting;
            waiting_count = item->u.sprite.waiting_count;
        }
        else
        {
            waiting = item->u.interface_.waiting;
            waiting_count = item->u.interface_.waiting_count;
        }

        for( int j = 0; j < waiting_count; j++ )
        {
            struct Dat1_ArchiveRequest* req = &waiting[j];
            if( req->resolved )
                continue;
            if( coalesced_contains(
                    pending, pending_count, req->table_id, req->archive_id, req->flags) )
                continue;
            if( pending_count < MAX_COALESCED_REQUESTS )
            {
                pending[pending_count].table_id = req->table_id;
                pending[pending_count].archive_id = req->archive_id;
                pending[pending_count].flags = req->flags;
                pending_count++;
                LibToriRS_IOQueuePush(io_queue, req->table_id, req->archive_id, req->flags);
            }
        }
    }
}

/* ================================================================== */
/* IO routing: decode resolved IO -> dat1_buildcache, mark done        */
/* ================================================================== */

static void
mark_request_resolved(
    struct RevConfigLoaderState* state,
    int table_id,
    int archive_id,
    int flags)
{
    for( int i = 0; i < state->item_count; i++ )
    {
        struct RevConfigLoadItem* item = &state->items[i];
        struct Dat1_ArchiveRequest* waiting;
        int waiting_count;

        if( item->kind == REVCONFIG_ITEM_SPRITE )
        {
            waiting = item->u.sprite.waiting;
            waiting_count = item->u.sprite.waiting_count;
        }
        else
        {
            waiting = item->u.interface_.waiting;
            waiting_count = item->u.interface_.waiting_count;
        }

        for( int j = 0; j < waiting_count; j++ )
        {
            if( waiting[j].table_id == table_id && waiting[j].archive_id == archive_id &&
                waiting[j].flags == flags )
                waiting[j].resolved = true;
        }
    }
}

static bool
decode_interfaces_from_archive(
    struct RSCacheDat1Disk_Archive* archive,
    struct RSCacheDat1A_ConfigComponentList** out)
{
    struct RSCacheShared_FileListDat* filelist = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    assert(filelist && "Failed to create filelist from archive");

    int data_idx = RSCacheShared_FileListDatFindFileByName(filelist, "data");
    if( data_idx < 0 )
    {
        RSCacheShared_FileListDatFree(filelist);
        return false;
    }
    *out = RSCacheDat1A_ConfigComponentListNewDecode(
        filelist->files[data_idx], filelist->file_sizes[data_idx]);
    RSCacheShared_FileListDatFree(filelist);
    return *out != NULL;
}

static void
revconfig_route_resolved_io(
    struct RevConfigLoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !state || !io_queue || !state->buildcache )
        return;

    for( int i = io_queue->read_head; i < io_queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* io_item = &io_queue->items[i];
        if( io_item->kind != TORIRSIO_KIND_CACHE )
            continue;
        if( io_item->status != TORIRSIO_RESOLVED || !io_item->data )
            continue;

        int table_id = io_item->table_id;
        int archive_id = io_item->archive_id;
        int flags = io_item->flags;

        if( table_id == RSCacheDat1Disk_Table_Configs )
        {
            if( archive_id == RSCacheDat1A_ConfigKind_Media2dGraphics )
            {
                struct RSCacheShared_FileListDat* filelist =
                    RSCacheShared_FileListDatNewFromCacheDatArchive((struct RSCacheDat1Disk_Archive*)io_item->data);
                dat1_buildcache_set_media_2d_graphics_jagfile(state->buildcache, filelist);
                printf("revconfig_loader: decoded media jagfile\n");
                RSCacheDat1Disk_ArchiveFree((struct RSCacheDat1Disk_Archive*)io_item->data);
                io_item->data = NULL;
            }
            else if( archive_id == RSCacheDat1A_ConfigKind_Interfaces )
            {
                struct RSCacheDat1A_ConfigComponentList* ifaces = NULL;
                if( decode_interfaces_from_archive(
                        (struct RSCacheDat1Disk_Archive*)io_item->data, &ifaces) )
                {
                    dat1_buildcache_set_interfaces(state->buildcache, ifaces);
                    printf(
                        "revconfig_loader: decoded interfaces (%d components)\n",
                        ifaces ? ifaces->components_count : 0);
                }
                RSCacheDat1Disk_ArchiveFree((struct RSCacheDat1Disk_Archive*)io_item->data);
                io_item->data = NULL;
            }
            else if( archive_id == RSCacheDat1A_ConfigKind_TitleAndFonts )
            {
                struct RSCacheShared_FileListDat* filelist =
                    RSCacheShared_FileListDatNewFromCacheDatArchive((struct RSCacheDat1Disk_Archive*)io_item->data);
                if( filelist && state->scene )
                {
                    // ui_scene_load_fonts_from_title_archive(state->scene, filelist);
                    printf("revconfig_loader: loaded fonts from title archive\n");
                }
                if( filelist )
                    RSCacheShared_FileListDatFree(filelist);
                RSCacheDat1Disk_ArchiveFree((struct RSCacheDat1Disk_Archive*)io_item->data);
                io_item->data = NULL;
            }
        }
        else if( table_id == RSCacheDat1Disk_Table_Models )
        {
            /* Decode raw model into dat1_buildcache (not gamecache yet). */
            struct RSCacheDat2A_Model* model =
                RSCacheDat2A_ModelNewFromDatArchive((struct RSCacheDat1Disk_Archive*)io_item->data, archive_id);
            if( model )
            {
                dat1_buildcache_model_add(state->buildcache, archive_id, model);
                printf("revconfig_loader: decoded model %d into buildcache\n", archive_id);
            }
            RSCacheDat1Disk_ArchiveFree((struct RSCacheDat1Disk_Archive*)io_item->data);
            io_item->data = NULL;
        }

        mark_request_resolved(state, table_id, archive_id, flags);
    }

    io_queue->read_head = io_queue->count;
}

/* ================================================================== */
/* Step all queued elements                                             */
/* ================================================================== */

static void
revconfig_step_items(struct RevConfigLoaderState* state)
{
    for( int i = 0; i < state->item_count; i++ )
    {
        struct RevConfigLoadItem* item = &state->items[i];
        if( item->kind == REVCONFIG_ITEM_SPRITE )
        {
            struct Dat1_Sprite* s = &item->u.sprite;
            if( s->state != DAT1_STATE_DONE && s->state != DAT1_STATE_FAILED )
                dat1_sprite_step(s, state->buildcache, state->scene, state->sprite_map);
        }
        else
        {
            struct Dat1_Interface* iface = &item->u.interface_;
            if( iface->state != DAT1_STATE_DONE && iface->state != DAT1_STATE_FAILED )
                dat1_interface_step(
                    iface, state->buildcache, state->core, state->scene, state->sprite_map);
        }
    }
}

static bool
revconfig_all_done(const struct RevConfigLoaderState* state)
{
    for( int i = 0; i < state->item_count; i++ )
    {
        const struct RevConfigLoadItem* item = &state->items[i];
        enum Dat1_LoadState s =
            (item->kind == REVCONFIG_ITEM_SPRITE) ? item->u.sprite.state : item->u.interface_.state;
        if( s != DAT1_STATE_DONE && s != DAT1_STATE_FAILED )
            return false;
    }
    return true;
}

/* ================================================================== */
/* UITree build (bake) phase                                           */
/* ================================================================== */

/* Pure sprite_map lookup for the bake pass. */
static int
bake_sprite_find(
    struct DashMap* sprite_map,
    const char* name)
{
    return sprite_map_find(sprite_map, name);
}

static int
bake_sprite_find_bracket(
    struct DashMap* sprite_map,
    const char* sprite_ref,
    int* out_atlas_index)
{
    if( !sprite_ref || !sprite_ref[0] )
    {
        if( out_atlas_index )
            *out_atlas_index = 0;
        return -1;
    }
    char name[64];
    int atlas = 0;
    strncpy(name, sprite_ref, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    char* open = strchr(name, '[');
    char* close = strchr(name, ']');
    if( open && close && close > open )
    {
        *open = '\0';
        atlas = atoi(open + 1);
    }
    if( out_atlas_index )
        *out_atlas_index = atlas;
    return bake_sprite_find(sprite_map, name);
}

static int
bake_font_id(
    struct UIScene* scene,
    int font_idx)
{
    static const char* const font_names[] = { "p11", "p12", "b12", "q8" };
    int idx = (font_idx >= 0 && font_idx <= 3) ? font_idx : 1;
    return scene ? ui_scene_font_find_id(scene, font_names[idx]) : -1;
}

/* Forward declaration for recursion. */
static void
bake_rs_subtree(
    struct UITree* tree,
    struct DashMap* sprite_map,
    struct UIScene* scene,
    struct ToriAuxLibCore* core,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int32_t parent_idx,
    struct RSCacheDat1A_ConfigComponent* comp,
    int abs_x,
    int abs_y);

static void
bake_rs_subtree(
    struct UITree* tree,
    struct DashMap* sprite_map,
    struct UIScene* scene,
    struct ToriAuxLibCore* core,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int32_t parent_idx,
    struct RSCacheDat1A_ConfigComponent* comp,
    int abs_x,
    int abs_y)
{
    if( !comp || !ifaces )
        return;
    if( comp->hide && comp->type != COMPONENT_TYPE_LAYER )
        return;

    int sid, sid_a, fid;
    int bg_sid[UI_INV_SLOT_OFFSET_MAX];
    int bg_ai[UI_INV_SLOT_OFFSET_MAX];

    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
    {
        int32_t layer_idx = uitree_push_rs_layer(
            tree, parent_idx, comp->id, abs_x, abs_y, comp->width, comp->height);
        if( layer_idx < 0 || !comp->children )
            return;
        for( int ci = 0; ci < comp->children_count; ci++ )
        {
            int child_id = comp->children[ci];
            if( child_id < 0 || child_id >= ifaces->components_count )
                continue;
            struct RSCacheDat1A_ConfigComponent* child = ifaces->components[child_id];
            if( !child )
                continue;
            int cx = abs_x + (comp->childX ? comp->childX[ci] : 0) + child->x;
            int cy = abs_y + (comp->childY ? comp->childY[ci] : 0) + child->y;
            bake_rs_subtree(tree, sprite_map, scene, core, ifaces, layer_idx, child, cx, cy);
        }
        return;
    }

    case COMPONENT_TYPE_GRAPHIC:
        sid = bake_sprite_find(sprite_map, comp->graphic);
        sid_a = bake_sprite_find(sprite_map, comp->activeGraphic);
        if( sid < 0 && sid_a < 0 )
            return;
        uitree_push_rs_graphic(
            tree,
            parent_idx,
            comp->id,
            sid >= 0 ? sid : sid_a,
            0,
            (sid >= 0 && sid_a >= 0 && sid_a != sid) ? sid_a : -1,
            0,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
        return;

    case COMPONENT_TYPE_RECT:
        if( comp->fill )
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
        return;

    case COMPONENT_TYPE_TEXT:
    case COMPONENT_TYPE_INV_TEXT:
        fid = bake_font_id(scene, comp->font);
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
        return;

    case COMPONENT_TYPE_MODEL:
        if( comp->modelType == 1 && gamecache )
        {
            struct ToriDraw_ModelHandle hnd = ToriAuxLibCore_ModelGet(core, comp->model);
            if( hnd.kind == TORIDRAWMK_MODEL )
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
        return;

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
                const char* gname = comp->invSlotGraphic[si];
                if( gname && gname[0] )
                    bg_sid[si] = bake_sprite_find(sprite_map, gname);
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
        return;

    default:
        return;
    }
}

/* RSCacheDat2A_Component type enum for revconfig layout parsing (matches existing). */
enum BakeCompType
{
    BAKE_COMP_WORLD = 6,
    BAKE_COMP_SPRITE = 7,
};

struct BakeComponentEntry
{
    char name[64];
    int type; /* BakeCompType */
    char sprite_name[64];
    int sprite_id;
    int sprite_atlas_index;
    int width;
    int height;
    uint8_t level_mask;
};

struct BakeLayoutEntry
{
    char component[64];
    int x;
    int y;
    int flags;
    int top, right, bottom, left;
    uint8_t always_dirty;
};

#define BAKE_MAX_COMPONENTS 128
#define BAKE_MAX_LAYOUTS 64

struct BakePassCtx
{
    struct BakeComponentEntry components[BAKE_MAX_COMPONENTS];
    int component_count;
    struct BakeLayoutEntry layouts[BAKE_MAX_LAYOUTS];
    int layout_count;

    /* For layout accumulation: */
    struct BakeLayoutEntry pending_layout;
    bool layout_pending;
    char current_component[64];
    int current_type; /* BUILD_SPRITE / BUILD_COMPONENT / BUILD_LAYOUT */
    struct BakeComponentEntry pending_component;
};

static void
bake_pass_flush_layout(struct BakePassCtx* ctx)
{
    if( ctx->layout_pending && ctx->layout_count < BAKE_MAX_LAYOUTS )
        ctx->layouts[ctx->layout_count++] = ctx->pending_layout;
}

static void
bake_pass_apply_component_field(
    struct BakePassCtx* ctx,
    const struct RevConfigField* field)
{
    struct BakeComponentEntry* comp = &ctx->pending_component;
    switch( field->kind )
    {
    case RCFIELD_UICOMPONENT_TYPE:
        if( strcmp(field->value, "world") == 0 )
            comp->type = BAKE_COMP_WORLD;
        else
            comp->type = BAKE_COMP_SPRITE;
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
bake_pass_apply_layout_field(
    struct BakePassCtx* ctx,
    const struct RevConfigField* field)
{
    struct BakeLayoutEntry* le = &ctx->pending_layout;
    switch( field->kind )
    {
    case RCFIELD_UILAYOUT_COMPONENT:
        bake_pass_flush_layout(ctx);
        memset(le, 0, sizeof(*le));
        strncpy(le->component, field->value, sizeof(le->component) - 1);
        ctx->layout_pending = true;
        break;
    case RCFIELD_UILAYOUT_X:
        le->x = atoi(field->value);
        break;
    case RCFIELD_UILAYOUT_Y:
        le->y = atoi(field->value);
        break;
    case RCFIELD_UILAYOUT_TOP:
        le->top = atoi(field->value);
        le->flags |= STATIC_UI_RELATIVE_FLAG_TOP;
        break;
    case RCFIELD_UILAYOUT_LEFT:
        le->left = atoi(field->value);
        le->flags |= STATIC_UI_RELATIVE_FLAG_LEFT;
        break;
    case RCFIELD_UILAYOUT_BOTTOM:
        le->bottom = atoi(field->value);
        le->flags |= STATIC_UI_RELATIVE_FLAG_BOTTOM;
        break;
    case RCFIELD_UILAYOUT_RIGHT:
        le->right = atoi(field->value);
        le->flags |= STATIC_UI_RELATIVE_FLAG_RIGHT;
        break;
    case RCFIELD_UILAYOUT_DIRTY:
        le->always_dirty = 1;
        break;
    case RCFIELD_UILAYOUT_NULL:
        bake_pass_flush_layout(ctx);
        memset(le, 0, sizeof(*le));
        ctx->layout_pending = false;
        break;
    default:
        break;
    }
}

static void
bake_pass_item_done(
    struct BakePassCtx* ctx,
    struct DashMap* sprite_map)
{
    if( ctx->current_type == BUILD_COMPONENT )
    {
        /* Resolve sprite_id for sprite-type components. */
        struct BakeComponentEntry* comp = &ctx->pending_component;
        if( comp->type == BAKE_COMP_SPRITE && comp->sprite_name[0] )
        {
            int atlas = 0;
            comp->sprite_id = bake_sprite_find_bracket(sprite_map, comp->sprite_name, &atlas);
            comp->sprite_atlas_index = atlas;
        }
        if( ctx->component_count < BAKE_MAX_COMPONENTS )
            ctx->components[ctx->component_count++] = *comp;
    }
    ctx->current_type = BUILD_NONE;
}

static void
uitree_build_layouts(
    struct UITree* tree,
    struct BakePassCtx* ctx,
    struct DashMap* sprite_map)
{
    /* Build a name -> BakeComponentEntry mini-map for O(1) layout lookups. */
    for( int li = 0; li < ctx->layout_count; li++ )
    {
        struct BakeLayoutEntry* le = &ctx->layouts[li];
        /* Find component by name (linear, small count). */
        struct BakeComponentEntry* comp = NULL;
        for( int ci = 0; ci < ctx->component_count; ci++ )
        {
            if( strcmp(ctx->components[ci].name, le->component) == 0 )
            {
                comp = &ctx->components[ci];
                break;
            }
        }
        if( !comp )
        {
            printf("uitree_build: layout component '%s' not found\n", le->component);
            continue;
        }

        int32_t idx = -1;
        if( comp->type == BAKE_COMP_WORLD )
        {
            idx = uitree_push_world(
                tree, -1, le->x, le->y, comp->width, comp->height, comp->level_mask);
        }
        else if( comp->type == BAKE_COMP_SPRITE )
        {
            if( le->flags != 0 )
            {
                idx = uitree_push_sprite_relative(
                    tree,
                    -1,
                    comp->sprite_id,
                    comp->sprite_atlas_index,
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
                    comp->sprite_atlas_index,
                    le->x,
                    le->y,
                    comp->width,
                    comp->height);
            }
        }

        if( le->always_dirty && idx >= 0 )
            tree->components[idx].always_dirty = 1;
    }
    (void)sprite_map;
}

static void
uitree_build_rs_roots(
    struct UITree* tree,
    struct RevConfigLoaderState* state,
    struct RSCacheDat1A_ConfigComponentList* ifaces)
{
    if( !ifaces )
        return;

    for( int i = 0; i < state->item_count; i++ )
    {
        if( state->items[i].kind != REVCONFIG_ITEM_INTERFACE )
            continue;
        struct Dat1_Interface* iface = &state->items[i].u.interface_;
        if( iface->state != DAT1_STATE_DONE )
            continue;

        int cid = iface->component_id;
        if( cid < 0 || cid >= ifaces->components_count )
            continue;
        struct RSCacheDat1A_ConfigComponent* root = ifaces->components[cid];
        if( !root )
            continue;

        bake_rs_subtree(
            tree,
            state->sprite_map,
            state->scene,
            state->core,
            ifaces,
            -1,
            root,
            root->x,
            root->y);
        printf("uitree_build: baked RS component %d\n", cid);
    }
}

static void
uitree_build(
    struct UITree* tree,
    struct RevConfigLoaderState* state)
{
    tree->component_count = 0;
    tree->root_index = -1;

    struct RevConfigBuffer* rc = state->revconfig;
    if( !rc )
        return;

    struct BakePassCtx ctx = { 0 };
    ctx.current_type = BUILD_NONE;

    /* Single forward pass over the revconfig: reconstruct component/layout
     * definitions on the fly and emit uitree_push_* nodes. */
    for( uint32_t fi = 0; fi < rc->field_count; fi++ )
    {
        const struct RevConfigField* field = &rc->fields[fi];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            if( strcmp(field->value, "component") == 0 )
            {
                ctx.current_type = BUILD_COMPONENT;
                memset(&ctx.pending_component, 0, sizeof(ctx.pending_component));
                ctx.pending_component.sprite_id = -1;
                ctx.pending_component.level_mask = 0xFu;
                ctx.pending_component.type = BAKE_COMP_SPRITE;
            }
            else if( strcmp(field->value, "layout") == 0 )
            {
                ctx.current_type = BUILD_LAYOUT;
            }
            else
            {
                ctx.current_type = BUILD_NONE;
            }
            break;

        case RCFIELD_ITEMNAME:
            if( ctx.current_type == BUILD_COMPONENT )
                strncpy(
                    ctx.pending_component.name,
                    field->value,
                    sizeof(ctx.pending_component.name) - 1);
            break;

        case RCFIELD_ITEMDONE:
            bake_pass_item_done(&ctx, state->sprite_map);
            break;

        case RCFIELD_UICOMPONENT_TYPE:
        case RCFIELD_UICOMPONENT_SPRITE:
        case RCFIELD_UICOMPONENT_WIDTH:
        case RCFIELD_UICOMPONENT_HEIGHT:
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            if( ctx.current_type == BUILD_COMPONENT )
                bake_pass_apply_component_field(&ctx, field);
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
            if( ctx.current_type == BUILD_LAYOUT )
                bake_pass_apply_layout_field(&ctx, field);
            break;

        default:
            break;
        }
    }

    /* Flush final pending layout entry. */
    bake_pass_flush_layout(&ctx);

    /* Emit uitree nodes for layout items. */
    uitree_build_layouts(tree, &ctx, state->sprite_map);

    /* Bake RS interface roots. */
    struct RSCacheDat1A_ConfigComponentList* ifaces =
        state->buildcache ? dat1_buildcache_get_interfaces(state->buildcache) : NULL;
    uitree_build_rs_roots(tree, state, ifaces);

    printf("uitree_build: %u components\n", tree->component_count);
}

/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

struct RevConfigLoaderState*
revconfig_loader_state_new(void)
{
    struct RevConfigLoaderState* state = calloc(1, sizeof(*state));
    if( !state )
        return NULL;

    if( !sprite_map_create(state) )
    {
        revconfig_loader_state_free(state);
        return NULL;
    }

    state->revconfig = revconfig_buffer_new(4096);
    if( !state->revconfig )
    {
        revconfig_loader_state_free(state);
        return NULL;
    }

    return state;
}

void
revconfig_loader_state_free(struct RevConfigLoaderState* state)
{
    if( !state )
        return;
    if( state->sprite_map )
        dashmap_free(state->sprite_map);
    free(state->sprite_map_buffer);
    if( state->revconfig )
        revconfig_buffer_free(state->revconfig);
    free(state);
}

void
revconfig_loader_reset(struct RevConfigLoaderState* state)
{
    if( !state )
        return;
    revconfig_loader_revconfig_reset(state);
    memset(state->items, 0, sizeof(state->items));
    state->item_count = 0;
    state->queue_built = false;
    state->done = false;
    /* Clear sprite_map entries (re-init the dashmap). */
    if( state->sprite_map )
        dashmap_free(state->sprite_map);
    if( state->sprite_map_buffer )
    {
        struct DashMapConfig cfg = {
            .buffer = state->sprite_map_buffer,
            .buffer_size = SPRITE_MAP_CAPACITY * sizeof(struct Dat1_SpriteMapEntry),
            .key_size = 64,
            .entry_size = sizeof(struct Dat1_SpriteMapEntry),
        };
        state->sprite_map = dashmap_new(&cfg, 0);
    }
}

void
revconfig_loader_set_buildcache(
    struct RevConfigLoaderState* state,
    struct Dat1BuildCache* buildcache)
{
    if( state )
        state->buildcache = buildcache;
}

void
revconfig_loader_set_gamecache(
    struct RevConfigLoaderState* state,
    struct ToriAuxLibCore* gamecache)
{
    if( state )
        state->core = gamecache;
}

void
revconfig_loader_set_ui_scene(
    struct RevConfigLoaderState* state,
    struct UIScene* scene)
{
    if( state )
        state->scene = scene;
}

struct RevConfigBuffer*
revconfig_loader_revconfig(struct RevConfigLoaderState* state)
{
    return state ? state->revconfig : NULL;
}

void
revconfig_loader_revconfig_reset(struct RevConfigLoaderState* state)
{
    if( !state )
        return;
    if( state->revconfig )
        revconfig_buffer_free(state->revconfig);
    state->revconfig = revconfig_buffer_new(4096);
}

void
revconfig_loader_queue_rs_component(
    struct RevConfigLoaderState* state,
    int component_id)
{
    assert(state && component_id >= 0 && "Invalid arguments");
    struct RevConfigLoadItem* item = queue_push(state);
    if( !item )
    {
        printf("revconfig_loader: queue full, cannot add RS component %d\n", component_id);
        return;
    }
    item->kind = REVCONFIG_ITEM_INTERFACE;
    dat1_interface_init(&item->u.interface_, component_id);
    state->done = false;
    printf("revconfig_loader: queued RS component %d\n", component_id);
}

bool
revconfig_loader_ready(struct RevConfigLoaderState* state)
{
    return state && state->done;
}

void
revconfig_loader_process(
    struct RevConfigLoaderState* state,
    struct LibToriRS_IOQueue* io_queue)
{
    /* First call after revconfig is loaded: build the load queue. */
    if( !state->queue_built && state->revconfig )
        revconfig_build_queue(state);

    /* 1. Decode any resolved IO archives into dat1_buildcache and mark done. */
    revconfig_route_resolved_io(state, io_queue);

    /* 2. Advance each element's state machine. */
    revconfig_step_items(state);

    /* 3. Collect outstanding requests and push unique ones to the IO queue. */
    revconfig_collect_requests(state, io_queue);

    state->done = state->queue_built && revconfig_all_done(state);
}

bool
revconfig_loader_submit(
    struct RevConfigLoaderState* state,
    struct UITree* tree)
{
    if( !state || !tree )
        return false;
    uitree_build(tree, state);
    return true;
}
