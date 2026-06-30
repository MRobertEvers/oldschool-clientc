#include "t_ui_dat1_load.h"

#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "buildcache/dat1_buildcache.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "revconfig_ui_expand.h"
#include "toridraw/toridraw_map.h"

#define UI_DAT1_INV_MODEL_BATCH 32

static void
ui_dat1_resolve_panel_roots_merged(
    struct RevConfigUIBuildState* build,
    struct RSCacheDat1A_ConfigComponentList* ifaces)
{
    if( !build || !ifaces )
        return;

    for( int i = 0; i < ifaces->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* comp = ifaces->components[i];
        if( !comp || comp->type != COMPONENT_TYPE_INV || comp->width != 4 || comp->height != 7 )
            continue;
        if( comp->layer < 0 || comp->layer >= 1024 )
            continue;
        if( build->panel_root_id[149] < 0 )
            build->panel_root_id[149] = comp->layer;
    }

    for( int i = 0; i < build->component_count; i++ )
    {
        struct RevConfigUIComponentItem const* sidebar = &build->components[i];
        if( strcmp(sidebar->type, "sidebar") != 0 || sidebar->componentno < 0 ||
            sidebar->componentno >= 1024 )
            continue;
        if( build->panel_root_id[sidebar->componentno] >= 0 )
            continue;

        struct RSCacheDat1A_ConfigComponent* direct =
            ifaces->components_count > sidebar->componentno
                ? ifaces->components[sidebar->componentno]
                : NULL;
        if( !direct )
        {
            for( int j = 0; j < ifaces->components_count; j++ )
            {
                if( ifaces->components[j] && ifaces->components[j]->id == sidebar->componentno )
                {
                    direct = ifaces->components[j];
                    break;
                }
            }
        }
        if( direct && direct->type == COMPONENT_TYPE_LAYER && direct->children_count > 0 )
            build->panel_root_id[sidebar->componentno] = direct->id;
    }
}

static void
ui_dat1_work_push(
    struct Task_Dat1UILoad* task,
    struct UIDat1WorkItem const* item)
{
    if( !task || !item || task->work_count >= REVCONFIG_UI_MAX_WORK_ITEMS )
        return;
    int tail = (task->work_head + task->work_count) % REVCONFIG_UI_MAX_WORK_ITEMS;
    task->work_items[tail] = *item;
    task->work_count++;
}

static bool
ui_dat1_work_pop(
    struct Task_Dat1UILoad* task,
    struct UIDat1WorkItem* out)
{
    if( !task || task->work_count <= 0 )
        return false;
    if( out )
        *out = task->work_items[task->work_head];
    task->work_head = (task->work_head + 1) % REVCONFIG_UI_MAX_WORK_ITEMS;
    task->work_count--;
    return true;
}

static int
ui_dat1_inv_collect_model_ids(
    struct Task_Dat1UILoad* task,
    struct Dat1BuildCache* bc,
    int* out,
    int cap)
{
    if( !task || !task->inv_pool || !bc || !out || cap <= 0 )
        return 0;

    int count = 0;
    for( int inv_i = 0; inv_i < task->inv_pool->count; inv_i++ )
    {
        struct UIInventory* inv = &task->inv_pool->inventories[inv_i];
        for( int item_i = 0; item_i < inv->item_count; item_i++ )
        {
            int obj_id = inv->items[item_i].obj_id;
            struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(bc, obj_id);
            if( !obj || obj->model <= 0 )
                continue;
            if( dat1_buildcache_model_get(bc, obj->model) )
                continue;

            bool duplicate = false;
            for( int j = 0; j < count; j++ )
            {
                if( out[j] == obj->model )
                {
                    duplicate = true;
                    break;
                }
            }
            if( duplicate || count >= cap )
                continue;
            out[count++] = obj->model;
        }
    }
    return count;
}

static void
ui_dat1_load_inv_pool_icons(
    struct Task_Dat1UILoad* task,
    struct Dat1BuildCache* bc)
{
    if( !task || !task->inv_pool || !task->scene || !bc )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);

    for( int inv_i = 0; inv_i < task->inv_pool->count; inv_i++ )
    {
        struct UIInventory* inv = &task->inv_pool->inventories[inv_i];
        for( int item_i = 0; item_i < inv->item_count; item_i++ )
        {
            struct UIInventoryItem* item = &inv->items[item_i];
            if( item->scene_id >= 0 || item->obj_id <= 0 )
                continue;

            struct ToriDraw_Sprite* sprite =
                dat1_buildcache_obj_icon_sprite(bc, task->scene, item->obj_id, 1);
            if( !sprite )
                continue;

            int element_id = task->build.next_element_id++;
            struct ToriDraw_Sprite** row = malloc(sizeof(struct ToriDraw_Sprite*));
            if( !row )
            {
                ToriDraw_SpriteFree(sprite);
                continue;
            }
            row[0] = sprite;
            ToriDraw_SceneSpriteAdd(task->scene, element_id, row, 1);

            if( core )
            {
                struct ToriAuxLibCore_Sprite* gc_sprite =
                    calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
                if( gc_sprite )
                {
                    snprintf(
                        gc_sprite->name,
                        sizeof(gc_sprite->name),
                        "obj_icon_%d",
                        item->obj_id);
                    gc_sprite->sprites = row;
                    gc_sprite->count = 1;
                    ToriAuxLibCache_SubmitSpriteFromDat1(task->c, element_id, gc_sprite);
                }
            }

            item->scene_id = element_id;
            item->atlas_index = 0;
        }
    }
}

struct Task_Dat1UILoad*
Task_Dat1UILoad_New(
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game)
{
    struct Task_Dat1UILoad* task = calloc(1, sizeof(struct Task_Dat1UILoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->scene = scene;
    task->tree = tree;
    task->game = game;
    revconfig_ui_build_init(&task->build);
    return task;
}

void
Task_Dat1UILoad_Free(struct Task_Dat1UILoad* task)
{
    if( !task )
        return;
    revconfig_buffer_free(task->cache_fields);
    revconfig_buffer_free(task->ui_fields);
    revconfig_item_buffer_free(task->items);
    if( task->inv_pool )
        uitree_inv_pool_free(task->inv_pool);
    free(task);
}

static void
ui_dat1_log_tree_diagnostics(
    struct Task_Dat1UILoad const* task)
{
    if( !task || !task->tree )
        return;

    uint32_t rs_nodes = 0;
    uint32_t rs_inv_nodes = 0;
    uint32_t rs_gfx_nodes = 0;
    for( uint32_t i = 0; i < task->tree->component_count; i++ )
    {
        enum StaticUIComponentType ty = task->tree->components[i].type;
        if( ty == UIELEM_RS_GRAPHIC || ty == UIELEM_RS_INV || ty == UIELEM_RS_TEXT ||
            ty == UIELEM_RS_RECT || ty == UIELEM_RS_LINE || ty == UIELEM_RS_MODEL ||
            ty == UIELEM_RS_LAYER )
            rs_nodes++;
        if( ty == UIELEM_RS_INV )
            rs_inv_nodes++;
        if( ty == UIELEM_RS_GRAPHIC )
            rs_gfx_nodes++;
    }

    fprintf(
        stderr,
        "dat1 ui: tree nodes=%u rs_nodes=%u rs_inv=%u rs_gfx=%u\n",
        task->tree->component_count,
        rs_nodes,
        rs_inv_nodes,
        rs_gfx_nodes);

    if( task->build.panel_root_id[149] >= 0 )
    {
        fprintf(
            stderr,
            "dat1 ui: inventory panel root_layer=%d\n",
            task->build.panel_root_id[149]);
    }

    struct Dat1BuildCache* bc = dat1(task->c);
    if( bc )
    {
        struct RSCacheDat1A_ConfigComponentList* ifaces = dat1_buildcache_get_interfaces(bc);
        if( ifaces )
        {
            int const probe_ids[] = { 149, 593, 320 };
            for( size_t pi = 0; pi < sizeof(probe_ids) / sizeof(probe_ids[0]); pi++ )
            {
                int cid = probe_ids[pi];
                struct RSCacheDat1A_ConfigComponent* root = NULL;
                if( cid >= 0 && cid < ifaces->components_count )
                    root = ifaces->components[cid];
                if( !root )
                {
                    for( int j = 0; j < ifaces->components_count; j++ )
                    {
                        if( ifaces->components[j] && ifaces->components[j]->id == cid )
                        {
                            root = ifaces->components[j];
                            break;
                        }
                    }
                }
                if( !root )
                {
                    fprintf(stderr, "dat1 ui: iface root %d missing\n", cid);
                    continue;
                }
                int resolved = 0;
                if( root->children )
                {
                    for( int ci = 0; ci < root->children_count; ci++ )
                    {
                        int child_id = root->children[ci];
                        struct RSCacheDat1A_ConfigComponent* ch = NULL;
                        if( child_id >= 0 && child_id < ifaces->components_count )
                            ch = ifaces->components[child_id];
                        if( ch )
                            resolved++;
                    }
                }
                fprintf(
                    stderr,
                    "dat1 ui: iface root %d type=%d children=%d resolved=%d hide=%d\n",
                    cid,
                    root->type,
                    root->children_count,
                    resolved,
                    root->hide);
            }
        }
    }

    struct RevConfigUISpriteRef const* sideicons =
        revconfig_ui_build_find_ref(&task->build, "sideicons");
    if( sideicons )
    {
        int count = 0;
        struct ToriDraw_Sprite** sprites =
            task->scene ? ToriDraw_SceneSpriteGet(task->scene, sideicons->element_id, &count) : NULL;
        fprintf(
            stderr,
            "dat1 ui: sideicons element_id=%d atlas_count=%d scene_sprites=%d\n",
            sideicons->element_id,
            sideicons->cache_item.atlas_count,
            count);
        if( sprites && count > 0 && sprites[0] )
        {
            fprintf(
                stderr,
                "dat1 ui: sideicons[0] size=%dx%d\n",
                sprites[0]->width,
                sprites[0]->height);
        }
        if( sprites && count > 3 && sprites[3] && sprites[3]->pixels_argb )
        {
            int opaque = 0;
            int const sw = sprites[3]->width;
            int const sh = sprites[3]->height;
            for( int py = 0; py < sh; py++ )
            {
                for( int px = 0; px < sw; px++ )
                {
                    uint32_t pxv = sprites[3]->pixels_argb[px + py * sw];
                    if( pxv != 0 )
                        opaque++;
                }
            }
            fprintf(stderr, "dat1 ui: sideicons[3] opaque_pixels=%d/%d\n", opaque, sw * sh);
        }
    }

    for( uint32_t i = 0; i < task->tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &task->tree->components[i];
        if( c->type != UIELEM_BUILTIN_TAB_ICONS )
            continue;
        fprintf(
            stderr,
            "dat1 ui: tab_icon tabno=%d scene_id=%d atlas=%d size=%dx%d\n",
            c->u.tab_icon.tabno,
            c->u.tab_icon.scene_id,
            c->u.tab_icon.atlas_index,
            c->position.width,
            c->position.height);
    }

    if( task->inv_pool )
    {
        for( int inv_i = 0; inv_i < task->inv_pool->count; inv_i++ )
        {
            struct UIInventory const* inv = &task->inv_pool->inventories[inv_i];
            for( int item_i = 0; item_i < inv->item_count; item_i++ )
            {
                fprintf(
                    stderr,
                    "dat1 ui: inv[%s] slot=%d obj=%d scene_id=%d\n",
                    inv->name,
                    item_i,
                    inv->items[item_i].obj_id,
                    inv->items[item_i].scene_id);
            }
        }
    }
    else if( task->game && task->game->ui_inv_pool )
    {
        struct UIInventoryPool const* pool = task->game->ui_inv_pool;
        for( int inv_i = 0; inv_i < pool->count; inv_i++ )
        {
            struct UIInventory const* inv = &pool->inventories[inv_i];
            for( int item_i = 0; item_i < inv->item_count; item_i++ )
            {
                fprintf(
                    stderr,
                    "dat1 ui: inv[%s] slot=%d obj=%d scene_id=%d\n",
                    inv->name,
                    item_i,
                    inv->items[item_i].obj_id,
                    inv->items[item_i].scene_id);
            }
        }
    }
}

bool
Task_Dat1UILoad_IsReady(struct Task_Dat1UILoad* task)
{
    return task && task->ui_ready;
}

static void
ui_dat1_seed_work_queue(struct Task_Dat1UILoad* task)
{
    static char const* const font_names[UI_DAT1_FONT_COUNT] = { "p11", "p12", "b12", "q8" };

    for( int i = 0; i < UI_DAT1_FONT_COUNT; i++ )
    {
        struct UIDat1WorkItem item = { 0 };
        item.kind = UI_DAT1WORK_FONT;
        item.font_id = i;
        strncpy(item.font_name, font_names[i], sizeof(item.font_name) - 1);
        ui_dat1_work_push(task, &item);
    }

    for( int i = 0; i < task->build.sprite_ref_count; i++ )
    {
        struct UIDat1WorkItem item = { 0 };
        item.kind = UI_DAT1WORK_SPRITE;
        item.ref_index = i;
        ui_dat1_work_push(task, &item);
    }
}

static bool
ui_dat1_process_work_item(
    struct Task_Dat1UILoad* task,
    struct UIDat1WorkItem const* item)
{
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct Dat1BuildCache* bc = dat1(task->c);

    if( item->kind == UI_DAT1WORK_FONT )
    {
        if( ToriAuxLibCore_FontHas(core, item->font_id) )
            return true;

        struct ToriDraw_Font* font = dat1_buildcache_font_decode(bc, item->font_name);
        if( !font )
            return false;

        struct ToriAuxLibCore_Font* gc_font = calloc(1, sizeof(struct ToriAuxLibCore_Font));
        if( !gc_font )
        {
            ToriDraw_FontFree(font);
            return false;
        }
        strncpy(gc_font->name, item->font_name, sizeof(gc_font->name) - 1);
        gc_font->font = font;
        ToriAuxLibCache_SubmitFontFromDat1(task->c, item->font_id, gc_font);
        ToriDraw_SceneFontAdd(task->scene, item->font_id, font);
        return true;
    }

    if( item->kind == UI_DAT1WORK_SPRITE )
    {
        if( item->ref_index < 0 || item->ref_index >= task->build.sprite_ref_count )
            return true;

        struct RevConfigUISpriteRef const* ref = &task->build.sprite_refs[item->ref_index];
        if( ToriAuxLibCore_SpriteHas(core, ref->element_id) )
            return true;

        int count = 0;
        struct ToriDraw_Sprite** sprites = dat1_buildcache_sprite_decode(bc, &ref->cache_item, &count);
        if( !sprites || count <= 0 )
            return false;

        struct ToriAuxLibCore_Sprite* gc_sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
        if( !gc_sprite )
        {
            for( int j = 0; j < count; j++ )
                ToriDraw_SpriteFree(sprites[j]);
            free(sprites);
            return false;
        }
        strncpy(gc_sprite->name, ref->name, sizeof(gc_sprite->name) - 1);
        gc_sprite->sprites = sprites;
        gc_sprite->count = count;
        ToriAuxLibCache_SubmitSpriteFromDat1(task->c, ref->element_id, gc_sprite);
        ToriDraw_SceneSpriteAdd(task->scene, ref->element_id, sprites, count);
        return true;
    }

    return false;
}

int
Task_Dat1UILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1UILoad* task = task_state;
    struct Dat1BuildCache* bc = dat1(task->c);

    PT_BEGIN(&task->thread);

    task->cache_fields = revconfig_buffer_new(1024);
    task->ui_fields = revconfig_buffer_new(1024);
    task->items = revconfig_item_buffer_new(256);

    configio_fetch_revconfig(ctx, UI_DAT1_CACHE_INI);
    configio_fetch_revconfig(ctx, UI_DAT1_UI_INI);
    PT_YIELD(&task->thread);

    {
        void* data = NULL;
        int size = configio_decode_revconfig(ctx, &data);
        if( size > 0 && data )
            revconfig_load_fields_from_ini_bytes(data, (size_t)size, task->cache_fields);
        free(data);
    }
    {
        void* data = NULL;
        int size = configio_decode_revconfig(ctx, &data);
        if( size > 0 && data )
            revconfig_load_fields_from_ini_bytes(data, (size_t)size, task->ui_fields);
        free(data);
    }

    revconfig_items_build(task->cache_fields, task->items);
    revconfig_items_build(task->ui_fields, task->items);
    revconfig_ui_build_collect_items(&task->build, task->items);
    revconfig_ui_build_set_core(&task->build, ToriAuxLibCache_Core(task->c));

    task->inv_pool = uitree_inv_pool_new(8);
    revconfig_ui_build_set_expand_context(
        &task->build, task->c, bc, task->scene, task->inv_pool);
    if( task->inv_pool )
        revconfig_ui_build_populate_inv_pool(&task->build, task->items);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchMediaJagfile(ctx));
    IO_REQUEST(ctx, 1, TAPIDat1_FetchTitleFontsJagfile(ctx));
    IO_REQUEST(ctx, 2, TAPIDat1_FetchInterfacesJagfile(ctx));
    PT_YIELD(&task->thread);

    {
        struct RSCacheShared_FileListDat* media = TAPIDat1_DecodeMediaJagfile(ctx, 0);
        if( media )
            dat1_buildcache_set_media_2d_graphics_jagfile(bc, media);
        struct RSCacheShared_FileListDat* title = TAPIDat1_DecodeTitleFontsJagfile(ctx, 1);
        if( title )
            dat1_buildcache_set_title_fonts_jagfile(bc, title);
        struct RSCacheShared_FileListDat* interfaces_filelist = TAPIDat1_DecodeInterfacesJagfile(ctx, 2);
        if( interfaces_filelist )
        {
            int data_idx = RSCacheShared_FileListDatFindFileByName(interfaces_filelist, "data");
            if( data_idx >= 0 )
            {
                void* iface_data = interfaces_filelist->files[data_idx];
                int iface_size = interfaces_filelist->file_sizes[data_idx];
                struct RSCacheDat1A_ConfigComponentList* interfaces =
                    RSCacheDat1A_ConfigComponentListNewDecode(iface_data, iface_size);

                if( interfaces )
                {
                    dat1_buildcache_set_interfaces(bc, interfaces);
                    ToriAuxLibCache_SubmitAllComponentsFromDat1(task->c);
                    ui_dat1_resolve_panel_roots_merged(&task->build, interfaces);
                }
            }
            RSCacheShared_FileListDatFree(interfaces_filelist);
        }
    }
    LibToriRS_IOQueueClear(ctx->io);

    ui_dat1_seed_work_queue(task);

    while( task->work_count > 0 )
    {
        struct UIDat1WorkItem item;
        if( !ui_dat1_work_pop(task, &item) )
            break;
        if( !ui_dat1_process_work_item(task, &item) )
        {
            ui_dat1_work_push(task, &item);
            PT_YIELD(&task->thread);
            continue;
        }
    }

    if( ToriDraw_MapCount(bc->obj_hmap) == 0 )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        PT_YIELD(&task->thread);

        {
            struct RSCacheShared_FileListDat* config_jag = TAPIDat1_DecodeConfigJagfile(ctx, 0);
            if( config_jag )
                dat1_buildcache_set_fromconfigtable_config_jagfile(bc, config_jag);
        }
        LibToriRS_IOQueueClear(ctx->io);

        if( bc->fromconfigtable_config_jagfile )
            dat1_buildcache_objs_init_from_config_jagfile(bc);
    }

    task->inv_model_count = ui_dat1_inv_collect_model_ids(
        task, bc, task->inv_model_ids, UI_DAT1_INV_MODEL_MAX);

    for( task->inv_model_index = 0; task->inv_model_index < task->inv_model_count; )
    {
        LibToriRS_IOBatchReset(&task->inv_model_batch);
        int batch_end = task->inv_model_index + UI_DAT1_INV_MODEL_BATCH;
        if( batch_end > task->inv_model_count )
            batch_end = task->inv_model_count;

        for( ; task->inv_model_index < batch_end; task->inv_model_index++ )
        {
            int model_id = task->inv_model_ids[task->inv_model_index];
            if( !dat1_buildcache_model_get(bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->inv_model_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->inv_model_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->inv_model_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&task->inv_model_batch, i);
                dat1_buildcache_model_add(bc, model_id, model);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    ui_dat1_load_inv_pool_icons(task, bc);
    revconfig_ui_preload_interface_sprites(&task->build);

    if( task->tree )
        revconfig_ui_build_tree(&task->build, task->tree, "fixed");

    if( task->game && task->inv_pool )
    {
        GameRunescape_SetUIInvPool(task->game, task->inv_pool);
        task->inv_pool = NULL;
    }

    if( task->scene )
        ToriDraw_SceneSpritesReemitLoads(task->scene);

    if( task->game )
    {
        task->game->ui_sprites_synced = false;
        GameRunescape_SyncUISpritesFromScene(task->game);
        GameRunescape_SetUITreeReady(task->game, true);
    }

    ui_dat1_log_tree_diagnostics(task);

    dat1_buildcache_set_interfaces(bc, NULL);
    task->ui_ready = true;

    PT_END(&task->thread);
}
