#include "interfacex_host_io.h"

#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "platforms/platform_x/cachelib.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "platforms/platform_x_io_reactor.h"
#include "runescape/appearance.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/core_task_await.h"
#include "toriauxlib/core/tasks/task_clientscript_load.h"
#include "toriauxlib/core/tasks/task_dat2_config_entry_load.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_sprite.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct InterfaceX_GraphicSceneCacheEntry
{
    int graphic_id;
    int scene_id;
    struct InterfaceX_GraphicSceneCacheEntry* next;
};

struct InterfaceX_ObjIconCacheEntry
{
    int obj_id;
    int scene_id;
    struct InterfaceX_ObjIconCacheEntry* next;
};

struct InterfaceX_ObjIconFailedEntry
{
    int obj_id;
    int count;
    struct InterfaceX_ObjIconFailedEntry* next;
};

struct InterfaceX_HostIOCaches
{
    struct InterfaceX_GraphicSceneCacheEntry* graphic_scene_cache;
    struct InterfaceX_ObjIconCacheEntry* obj_icon_cache;
    struct InterfaceX_ObjIconFailedEntry* obj_icon_failed;
};

static struct InterfaceX_HostIOCaches s_host_io_caches;

static struct ToriDraw_Font*
hostio_font_new_from_core(struct ToriAuxLibCore_Font const* src);

static int
hostio_raster_sprite_to_scene(
    struct InterfaceX_HostIO* io,
    struct ToriAuxLibCore_Sprite* sprite,
    int* scene_id_out);

struct InterfaceX_TaskSpriteLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    int sprite_id;
};

struct InterfaceX_TaskFontLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    int font_id;
};

struct InterfaceX_TaskModelLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    int model_id;
};

struct InterfaceX_TaskInterfaceLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    int group_id;
    struct RSCacheDat2Disk_ReferenceTable* reference_table;
    struct RSCacheDat2Disk_Archive* iface_disk_archive;
    struct Dat2BuildCache_InterfaceArchive* iface_archive;
};

#define INTERFACEX_BATCH_MODEL_IO_CHUNK 64
#define INTERFACEX_BATCH_MODEL_MAX_IDS 512
#define INTERFACEX_BATCH_MODEL_MAX_NPC 64
#define INTERFACEX_BATCH_MODEL_MAX_OBJ 64
#define INTERFACEX_BATCH_MODEL_MAX_PLAYER 8

struct InterfaceX_TaskBatchModelLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    int* next_scene_id;

    struct InterfaceX_ModelLoadArg* args;
    int arg_count;
    struct InterfaceX_ModelLoadResult* results;

    int needed_model_ids[INTERFACEX_BATCH_MODEL_MAX_IDS];
    int needed_model_count;
    int prefetch_index;

    int npc_ids[INTERFACEX_BATCH_MODEL_MAX_NPC];
    int npc_count;
    int npc_index;

    int obj_ids[INTERFACEX_BATCH_MODEL_MAX_OBJ];
    int obj_count;

    int player_appearances[INTERFACEX_BATCH_MODEL_MAX_PLAYER][RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int player_count;
    int player_index;

    struct LibToriRS_IOBatch io_batch;

    struct Task_ToriAuxLibCache_NpcAdd* npc_child;
    struct Task_ToriAuxLibCache_PlayerAdd* player_child;
    struct Task_Dat2ConfigEntryLoad* obj_config_child;

    int scene_build_index;
    int batch_end;
    bool need_obj_load;
    int player_model_count;
    int player_model_ids[256];
    int decode_i;
    int init_i;
    int scene_id;
    struct ToriDraw_Model* td_model;
};

struct InterfaceX_BatchModelLoad
{
    struct InterfaceX_TaskBatchModelLoad* task;
    int arg_count;
};

static void
batch_model_load_destroy_runner(void* state)
{
    (void)state;
}

static const char*
hostio_config_kind_name(int config_kind)
{
    switch( config_kind )
    {
    case RSCacheDat2A_ConfigKind_Params:
        return "params";
    case RSCacheDat2A_ConfigKind_Enum:
        return "enum";
    case RSCacheDat2A_ConfigKind_Struct:
        return "struct";
    case RSCacheDat2A_ConfigKind_Object:
        return "object";
    default:
        return "unknown";
    }
}

static void
hostio_drain_io(struct InterfaceX_HostIO* io)
{
    while( LibToriCoreTaskRunner_HasLive(io->task_runner) )
    {
        InterfaceX_HostIO_Pump(io);
    }
}

void
InterfaceX_HostIO_Pump(struct InterfaceX_HostIO* io)
{
    if( !io )
        return;
    while( LibToriCoreTaskRunner_Run(io->task_runner) )
        LibToriPlatformX_IOReactorProcess(io->io_reactor, io->io_queue);
    LibToriPlatformX_IOReactorProcess(io->io_reactor, io->io_queue);
}

static void
hostio_run_task(
    struct InterfaceX_HostIO* io,
    void* task_state,
    int (*run_fn)(
        void*,
        struct LibToriRS_IOContext*),
    void (*destroy_fn)(void*))
{
    LibToriCoreTaskRunner_Add(io->task_runner, task_state, run_fn, destroy_fn);
    hostio_drain_io(io);
}

static void
hostio_sprite_load_destroy(void* state)
{
    InterfaceX_TaskSpriteLoad_Free(state);
}

static int
hostio_sprite_load_run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct InterfaceX_TaskSpriteLoad* task = task_state;
    struct Dat2BuildCache* bc = dat2(task->cache);

    PT_BEGIN(&task->thread);

    if( !task->cache || task->sprite_id < 0 )
    {
        fprintf(stderr, "hostio: sprite load skipped (invalid sprite_id=%d)\n", task->sprite_id);
        PT_EXIT(&task->thread);
    }

    if( ToriAuxLibCore_SpriteHas(ToriAuxLibCache_Core(task->cache), task->sprite_id) )
        PT_EXIT(&task->thread);

    if( dat2_buildcache_dynamic_sprite_has(bc, task->sprite_id) )
    {
        struct ToriAuxLibCore_Sprite* cached_sprite =
            dat2_buildcache_dynamic_sprite_get(bc, task->sprite_id);
        if( cached_sprite )
            ToriAuxLibCache_SubmitSprite(task->cache, task->sprite_id, cached_sprite);
        PT_EXIT(&task->thread);
    }

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Sprites);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchSprite(ctx, task->sprite_id));
    PT_YIELD(&task->thread);

    {
        struct RSCacheDat2Disk_Archive* archive =
            TAPIDat2_DecodeSpriteArchive(ctx, 0, task->sprite_id);
        if( !archive )
        {
            fprintf(
                stderr, "hostio: sprite archive decode failed for sprite_id=%d\n", task->sprite_id);
            PT_EXIT(&task->thread);
        }

        struct ToriAuxLibCore_Sprite* sprite =
            dat2_buildcache_sprite_decode_id_from_archive(archive, task->sprite_id);
        if( !sprite )
        {
            fprintf(stderr, "hostio: sprite decode failed for sprite_id=%d\n", task->sprite_id);
            PT_EXIT(&task->thread);
        }

        ToriAuxLibCache_SubmitSprite(task->cache, task->sprite_id, sprite);
    }

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
}

struct InterfaceX_TaskSpriteLoad*
InterfaceX_TaskSpriteLoad_New(
    struct ToriAuxLibCache* cache,
    int sprite_id)
{
    struct InterfaceX_TaskSpriteLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    task->sprite_id = sprite_id;
    return task;
}

void
InterfaceX_TaskSpriteLoad_Free(struct InterfaceX_TaskSpriteLoad* task)
{
    free(task);
}

int
InterfaceX_TaskSpriteLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    return hostio_sprite_load_run(task_state, ctx);
}

bool
InterfaceX_HostIO_FinalizeGraphicScene(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out)
{
    assert(io);
    assert(graphic_id >= 0);
    assert(scene_id_out);

    if( InterfaceX_HostIO_GraphicSceneId(io, graphic_id, scene_id_out) )
        return true;

    struct ToriAuxLibCore_Sprite* sprite = ToriAuxLibCore_SpriteGet(io->core, graphic_id);
    if( !sprite )
    {
        fprintf(stderr, "hostio: graphic sprite missing after IO for graphic_id=%d\n", graphic_id);
        return false;
    }

    int scene_id = -1;
    if( hostio_raster_sprite_to_scene(io, sprite, &scene_id) != 0 )
    {
        fprintf(stderr, "hostio: graphic scene raster failed for graphic_id=%d\n", graphic_id);
        return false;
    }

    struct InterfaceX_GraphicSceneCacheEntry* entry =
        calloc(1, sizeof(struct InterfaceX_GraphicSceneCacheEntry));
    if( entry )
    {
        entry->graphic_id = graphic_id;
        entry->scene_id = scene_id;
        entry->next = s_host_io_caches.graphic_scene_cache;
        s_host_io_caches.graphic_scene_cache = entry;
    }

    *scene_id_out = scene_id;
    return true;
}

static void
hostio_font_load_destroy(void* state)
{
    InterfaceX_TaskFontLoad_Free(state);
}

static int
hostio_font_load_run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct InterfaceX_TaskFontLoad* task = task_state;
    struct Dat2BuildCache* bc = dat2(task->cache);

    PT_BEGIN(&task->thread);

    if( !task->cache || task->font_id < 0 )
    {
        fprintf(stderr, "hostio: font load skipped (invalid font_id=%d)\n", task->font_id);
        PT_EXIT(&task->thread);
    }

    if( dat2_buildcache_font_has(bc, task->font_id) )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Fonts);
    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Sprites);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchFont(ctx, task->font_id));
    IO_REQUEST(ctx, 1, TAPIDat2_FetchSprite(ctx, task->font_id));
    PT_YIELD(&task->thread);

    {
        struct RSCacheDat2Disk_Archive* font_archive =
            TAPIDat2_DecodeFontArchive(ctx, 0, task->font_id);
        struct RSCacheDat2Disk_Archive* sprite_archive =
            TAPIDat2_DecodeSpriteArchive(ctx, 1, task->font_id);
        if( !font_archive || !sprite_archive )
        {
            fprintf(
                stderr,
                "hostio: font archive decode failed for font_id=%d (font=%s sprite=%s)\n",
                task->font_id,
                font_archive ? "ok" : "null",
                sprite_archive ? "ok" : "null");
        }
        else if( !dat2_buildcache_font_add_from_archives(
                     bc, task->font_id, font_archive, sprite_archive) )
        {
            fprintf(stderr, "hostio: font add failed for font_id=%d\n", task->font_id);
        }
        if( font_archive )
            RSCacheDat2Disk_ArchiveFree(font_archive);
        if( sprite_archive )
            RSCacheDat2Disk_ArchiveFree(sprite_archive);
    }

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
}

struct InterfaceX_TaskFontLoad*
InterfaceX_TaskFontLoad_New(
    struct ToriAuxLibCache* cache,
    int font_id)
{
    struct InterfaceX_TaskFontLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    task->font_id = font_id;
    return task;
}

void
InterfaceX_TaskFontLoad_Free(struct InterfaceX_TaskFontLoad* task)
{
    free(task);
}

int
InterfaceX_TaskFontLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    return hostio_font_load_run(task_state, ctx);
}

bool
InterfaceX_HostIO_FinalizeSceneFont(
    struct InterfaceX_HostIO* io,
    int font_id)
{
    assert(io);
    assert(font_id >= 0);

    if( InterfaceX_HostIO_SceneFontGet(io, font_id) )
        return true;

    ToriAuxLibCache_SubmitFontFromDat2(io->aux_cache, font_id);

    struct ToriAuxLibCore_Font* core_font = ToriAuxLibCore_FontGet(io->core, font_id);
    if( !core_font )
    {
        fprintf(stderr, "hostio: font missing after IO for font_id=%d\n", font_id);
        return false;
    }

    struct ToriDraw_Font* font = hostio_font_new_from_core(core_font);
    if( !font )
    {
        fprintf(stderr, "hostio: font raster failed for font_id=%d\n", font_id);
        return false;
    }

    ToriDraw_SceneFontAdd(io->scene, font_id, font);
    return true;
}

static void
hostio_model_load_destroy(void* state)
{
    InterfaceX_TaskModelLoad_Free(state);
}

static int
hostio_model_load_run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct InterfaceX_TaskModelLoad* task = task_state;
    struct Dat2BuildCache* bc = dat2(task->cache);

    PT_BEGIN(&task->thread);

    if( !task->cache || task->model_id < 0 )
    {
        fprintf(stderr, "hostio: model load skipped (invalid model_id=%d)\n", task->model_id);
        PT_EXIT(&task->thread);
    }

    if( dat2_buildcache_model_get(bc, task->model_id) )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Models);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, task->model_id));
    PT_YIELD(&task->thread);

    {
        struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, 0);
        if( model )
            dat2_buildcache_model_add(bc, task->model_id, model);
        else
            fprintf(stderr, "hostio: model decode failed for model_id=%d\n", task->model_id);
    }

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
}

struct InterfaceX_TaskModelLoad*
InterfaceX_TaskModelLoad_New(
    struct ToriAuxLibCache* cache,
    int model_id)
{
    struct InterfaceX_TaskModelLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    task->model_id = model_id;
    return task;
}

void
InterfaceX_TaskModelLoad_Free(struct InterfaceX_TaskModelLoad* task)
{
    free(task);
}

int
InterfaceX_TaskModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    return hostio_model_load_run(task_state, ctx);
}

static struct ToriDraw_Font*
hostio_font_new_from_core(struct ToriAuxLibCore_Font const* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    if( !font )
        return NULL;

    for( int i = 0; i < TORIAUXLIBCORE_FONT_GLYPH_COUNT; i++ )
    {
        font->glyph_width[i] = src->glyph_width[i];
        font->glyph_height[i] = src->glyph_height[i];
        font->offset_x[i] = src->offset_x[i];
        font->offset_y[i] = src->offset_y[i];
        font->advance[i] = src->advance[i];
        if( src->glyph_alpha[i] && src->glyph_width[i] > 0 && src->glyph_height[i] > 0 )
        {
            size_t len = (size_t)src->glyph_width[i] * (size_t)src->glyph_height[i];
            font->glyph_alpha[i] = malloc(len);
            if( !font->glyph_alpha[i] )
                goto fail;
            memcpy(font->glyph_alpha[i], src->glyph_alpha[i], len);
        }
    }
    font->advance[TORIDRAW_FONT_GLYPH_COUNT] = src->advance[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    memcpy(font->draw_width, src->draw_width, sizeof(font->draw_width));
    font->line_height = src->line_height;
    memcpy(font->charcodeset, src->charcodeset, sizeof(font->charcodeset));
    if( !ToriDraw_FontValidate(font) )
        goto fail;
    return font;

fail:
    ToriDraw_FontFree(font);
    return NULL;
}

static int
hostio_raster_sprite_to_scene(
    struct InterfaceX_HostIO* io,
    struct ToriAuxLibCore_Sprite* sprite,
    int* scene_id_out)
{
    assert(io);
    assert(sprite);
    assert(scene_id_out);
    assert(io->scene);
    assert(io->next_scene_id);

    if( sprite->frame_count <= 0 || !sprite->frames )
        return -1;

    struct ToriAuxLibCore_SpriteFrame const* frame = &sprite->frames[0];
    if( !frame->pixels_argb || frame->width <= 0 || frame->height <= 0 )
        return -1;

    uint32_t* argb = malloc((size_t)frame->width * (size_t)frame->height * sizeof(uint32_t));
    if( !argb )
        return -1;

    memcpy(
        argb, frame->pixels_argb, (size_t)frame->width * (size_t)frame->height * sizeof(uint32_t));

    struct ToriDraw_Sprite* spr =
        ToriDraw_SpriteNewFromArgbOwned(argb, frame->width, frame->height);
    if( !spr )
        return -1;

    spr->crop_x = frame->crop_x;
    spr->crop_y = frame->crop_y;

    int scene_id = (*io->next_scene_id)++;
    struct ToriDraw_Sprite** sprites = calloc(1, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
    {
        ToriDraw_SpriteFree(spr);
        return -1;
    }

    sprites[0] = spr;
    ToriDraw_SceneSpriteAdd(io->scene, scene_id, sprites, 1);
    *scene_id_out = scene_id;
    return 0;
}

bool
InterfaceX_HostIO_Init(
    struct InterfaceX_HostIO* io,
    struct ToriDraw_Scene* scene,
    int* next_scene_id,
    const char* cache_path)
{
    assert(io);
    assert(scene);
    assert(next_scene_id);
    assert(cache_path);

    memset(io, 0, sizeof(*io));
    io->scene = scene;
    io->next_scene_id = next_scene_id;

    io->cachelib = cachelib_new(CACHE_MODE_DAT2);
    if( !io->cachelib )
        goto fail;

    if( cachelib_platform_init(io->cachelib, cache_path) != 1 )
        goto fail;

    io->core = ToriAuxLibCore_New();
    if( !io->core )
        goto fail;

    io->aux_cache = ToriAuxLibCache_New(TORIAUXLIBCACHE_MODE_DAT2, io->core);
    if( !io->aux_cache )
        goto fail;

    io->io_queue = LibToriRS_IOQueueNew();
    if( !io->io_queue )
        goto fail;

    io->io_reactor = LibToriPlatformX_IOReactorNew(io->cachelib);
    if( !io->io_reactor )
        goto fail;

    io->task_runner = LibToriCoreTaskRunner_New(io->io_queue);
    if( !io->task_runner )
        goto fail;

    return true;

fail:
    InterfaceX_HostIO_Free(io);
    return false;
}

void
InterfaceX_HostIO_Free(struct InterfaceX_HostIO* io)
{
    if( !io )
        return;

    if( io->task_runner )
        LibToriCoreTaskRunner_Free(io->task_runner);
    if( io->io_reactor )
        LibToriPlatformX_IOReactorFree(io->io_reactor);
    if( io->io_queue )
        LibToriRS_IOQueueFree(io->io_queue);
    if( io->aux_cache )
        ToriAuxLibCache_Free(io->aux_cache);
    if( io->core )
        ToriAuxLibCore_Free(io->core);
    if( io->cachelib )
        cachelib_free(io->cachelib);

    memset(io, 0, sizeof(*io));
}

void
InterfaceX_HostIO_DrainTasks(struct InterfaceX_HostIO* io)
{
    if( !io )
        return;
    hostio_drain_io(io);
}

void
InterfaceX_HostIO_RunTask(
    struct InterfaceX_HostIO* io,
    void* task_state,
    int (*run_fn)(
        void*,
        struct LibToriRS_IOContext*),
    void (*destroy_fn)(void*))
{
    hostio_run_task(io, task_state, run_fn, destroy_fn);
}

void
InterfaceX_HostIO_QueueTask(
    struct InterfaceX_HostIO* io,
    void* task_state,
    int (*run_fn)(
        void*,
        struct LibToriRS_IOContext*),
    void (*destroy_fn)(void*))
{
    assert(io);
    LibToriCoreTaskRunner_Add(io->task_runner, task_state, run_fn, destroy_fn);
}

struct ToriAuxLibCache*
InterfaceX_HostIO_Cache(struct InterfaceX_HostIO* io)
{
    return io ? io->aux_cache : NULL;
}

struct ToriAuxLibCore*
InterfaceX_HostIO_Core(struct InterfaceX_HostIO* io)
{
    return io ? io->core : NULL;
}

static void
hostio_load_clientscript_sync(
    struct InterfaceX_HostIO* io,
    int script_id)
{
    if( !io || script_id < 0 )
        return;
    if( InterfaceX_HostIO_ClientScriptGet(io, script_id) )
        return;

    struct RSCacheDat2Disk* disk = cachelib_dat2_disk(io->cachelib);
    if( !disk )
        return;

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
        return;

    struct RSCacheDat2Disk_ReferenceTable* table = disk->tables[RSCacheDat2Disk_Table_Clientscript];
    if( table )
        RSCacheDat2Disk_ArchiveInitMetadataFromTable(table, archive);

    struct ToriAuxLibCore_ClientScript* cs = ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
        archive, script_id, ToriAuxLibCache_ClientscriptDecodeFlags(io->aux_cache));
    if( !cs )
        return;

    ToriAuxLibCache_SubmitClientScript(io->aux_cache, script_id, cs);
}

struct RSCacheDat2Disk*
InterfaceX_HostIO_Disk(struct InterfaceX_HostIO* io)
{
    return io && io->cachelib ? cachelib_dat2_disk(io->cachelib) : NULL;
}

static void
hostio_interface_load_destroy(void* state)
{
    InterfaceX_TaskInterfaceLoad_Free(state);
}

static void
hostio_interface_group_submit(
    struct InterfaceX_HostIO* io,
    int group_id)
{
    struct Dat2BuildCache* bc;
    struct Dat2BuildCache_InterfaceArchive* arch;
    int top_id;

    if( !io || !io->aux_cache || group_id < 0 )
        return;

    bc = dat2(io->aux_cache);
    arch = dat2_buildcache_interface_archive_get(bc, group_id);
    if( !arch )
        return;

    top_id = group_id << 16;
    if( ToriAuxLibCore_ComponentHas(io->core, top_id) )
        return;

    ToriAuxLibCache_SubmitComponentsFromDat2(io->aux_cache, arch);
}

void
InterfaceX_HostIO_InterfaceGroupSubmit(
    struct InterfaceX_HostIO* io,
    int group_id)
{
    hostio_interface_group_submit(io, group_id);
}

static int
hostio_interface_load_run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct InterfaceX_TaskInterfaceLoad* task = task_state;
    struct Dat2BuildCache* bc = dat2(task->cache);

    PT_BEGIN(&task->thread);

    if( !task->cache || task->group_id < 0 )
    {
        fprintf(
            stderr, "hostio: interface group load skipped (invalid group_id=%d)\n", task->group_id);
        PT_EXIT(&task->thread);
    }

    if( dat2_buildcache_interface_archive_has(bc, task->group_id) )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Interfaces);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchInterface(ctx, task->group_id));
    PT_YIELD(&task->thread);

    task->reference_table =
        dat2_buildcache_reference_table_get(bc, RSCacheDat2Disk_Table_Interfaces);
    if( !task->reference_table )
        PT_EXIT(&task->thread);

    task->iface_disk_archive = TAPIDat2_DecodeInterfaceArchive(ctx, 0, task->group_id);
    if( !task->iface_disk_archive )
    {
        fprintf(
            stderr, "hostio: interface archive decode failed for group_id=%d\n", task->group_id);
        PT_EXIT(&task->thread);
    }

    task->iface_archive = dat2_buildcache_component_decode_iface_archive_from_archive(
        task->reference_table, task->iface_disk_archive, task->group_id);
    task->iface_disk_archive = NULL;
    if( !task->iface_archive )
    {
        fprintf(
            stderr, "hostio: interface component decode failed for group_id=%d\n", task->group_id);
        PT_EXIT(&task->thread);
    }

    dat2_buildcache_interface_archive_add(bc, task->group_id, task->iface_archive);
    ToriAuxLibCache_SubmitComponentsFromDat2(task->cache, task->iface_archive);

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
}

struct InterfaceX_TaskInterfaceLoad*
InterfaceX_TaskInterfaceLoad_New(
    struct ToriAuxLibCache* cache,
    int group_id)
{
    struct InterfaceX_TaskInterfaceLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    task->group_id = group_id;
    return task;
}

void
InterfaceX_TaskInterfaceLoad_Free(struct InterfaceX_TaskInterfaceLoad* task)
{
    free(task);
}

int
InterfaceX_TaskInterfaceLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    return hostio_interface_load_run(task_state, ctx);
}

bool
InterfaceX_HostIO_LoadInterfaceGroup(
    struct InterfaceX_HostIO* io,
    int group_id)
{
    struct Dat2BuildCache* bc;
    struct InterfaceX_TaskInterfaceLoad* task;

    assert(io);
    if( group_id < 0 || !io->aux_cache )
        return false;

    bc = dat2(io->aux_cache);
    if( dat2_buildcache_interface_archive_has(bc, group_id) )
    {
        hostio_interface_group_submit(io, group_id);
        return true;
    }

    task = InterfaceX_TaskInterfaceLoad_New(io->aux_cache, group_id);
    if( !task )
    {
        fprintf(stderr, "hostio: interface group load alloc failed for group_id=%d\n", group_id);
        return false;
    }

    hostio_run_task(io, task, hostio_interface_load_run, hostio_interface_load_destroy);

    if( !dat2_buildcache_interface_archive_has(bc, group_id) )
    {
        fprintf(stderr, "hostio: interface group load failed for group_id=%d\n", group_id);
        return false;
    }

    hostio_interface_group_submit(io, group_id);
    return true;
}

bool
InterfaceX_HostIO_ConfigEntryReady(
    struct InterfaceX_HostIO* io,
    int config_kind,
    int id)
{
    struct Dat2BuildCache* bc;

    if( !io || id < 0 )
        return false;

    bc = dat2(io->aux_cache);
    if( !bc )
        return false;

    switch( config_kind )
    {
    case RSCacheDat2A_ConfigKind_Params:
        return dat2_buildcache_param_get(bc, id) != NULL;
    case RSCacheDat2A_ConfigKind_Enum:
        return dat2_buildcache_enum_get(bc, id) != NULL;
    case RSCacheDat2A_ConfigKind_Struct:
        return dat2_buildcache_struct_get(bc, id) != NULL;
    case RSCacheDat2A_ConfigKind_Object:
        return dat2_buildcache_object_get(bc, id) != NULL;
    default:
        return false;
    }
}

bool
InterfaceX_HostIO_LoadConfigEntries(
    struct InterfaceX_HostIO* io,
    int config_kind,
    const int* ids,
    int id_count)
{
    int* pending = NULL;
    int pending_count = 0;
    bool ok = true;

    assert(io);
    assert(ids);
    assert(id_count > 0);

    pending = malloc((size_t)id_count * sizeof(int));
    if( !pending )
    {
        fprintf(
            stderr,
            "hostio: config %s load alloc failed for %d ids\n",
            hostio_config_kind_name(config_kind),
            id_count);
        return false;
    }

    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        if( id < 0 || InterfaceX_HostIO_ConfigEntryReady(io, config_kind, id) )
            continue;

        bool duplicate = false;
        for( int j = 0; j < pending_count; j++ )
        {
            if( pending[j] == id )
            {
                duplicate = true;
                break;
            }
        }
        if( !duplicate )
            pending[pending_count++] = id;
    }

    if( pending_count > 0 )
    {
        struct Task_Dat2ConfigEntryLoad* task =
            Task_Dat2ConfigEntryLoad_New(io->aux_cache, config_kind, pending, pending_count);
        if( !task )
        {
            fprintf(
                stderr,
                "hostio: config %s load task alloc failed for %d ids\n",
                hostio_config_kind_name(config_kind),
                pending_count);
            ok = false;
        }
        else
        {
            hostio_run_task(
                io,
                task,
                Task_Dat2ConfigEntryLoad_Run,
                (void (*)(void*))Task_Dat2ConfigEntryLoad_Free);
        }
    }

    for( int i = 0; i < pending_count; i++ )
    {
        if( !InterfaceX_HostIO_ConfigEntryReady(io, config_kind, pending[i]) )
        {
            fprintf(
                stderr,
                "hostio: config %s id=%d failed to populate after IO\n",
                hostio_config_kind_name(config_kind),
                pending[i]);
            ok = false;
        }
    }

    free(pending);
    return ok;
}

bool
InterfaceX_HostIO_LoadConfigEntry(
    struct InterfaceX_HostIO* io,
    int config_kind,
    int id)
{
    if( id < 0 )
        return false;
    if( InterfaceX_HostIO_ConfigEntryReady(io, config_kind, id) )
        return true;
    return InterfaceX_HostIO_LoadConfigEntries(io, config_kind, &id, 1);
}

struct ToriAuxLibCore_ClientScript*
InterfaceX_HostIO_ClientScriptGet(
    struct InterfaceX_HostIO* io,
    int script_id)
{
    if( !io || script_id < 0 )
        return NULL;

    return ToriAuxLibCore_ClientScriptGet(io->core, script_id);
}

bool
InterfaceX_HostIO_LoadClientScript(
    struct InterfaceX_HostIO* io,
    int script_id)
{
    assert(io);
    assert(script_id >= 0);
    if( InterfaceX_HostIO_ClientScriptGet(io, script_id) )
        return true;

    struct Task_ClientScriptLoad* task = Task_ClientScriptLoad_New(io->aux_cache, &script_id, 1);
    if( !task )
    {
        fprintf(
            stderr, "hostio: clientscript load task alloc failed for script_id=%d\n", script_id);
        return false;
    }

    hostio_run_task(
        io, task, Task_ClientScriptLoad_Run, (void (*)(void*))Task_ClientScriptLoad_Free);

    hostio_load_clientscript_sync(io, script_id);

    if( !InterfaceX_HostIO_ClientScriptGet(io, script_id) )
    {
        fprintf(stderr, "hostio: clientscript id=%d failed to populate after IO\n", script_id);
        return false;
    }
    return true;
}

void
InterfaceX_HostIO_LoadClientScripts(
    struct InterfaceX_HostIO* io,
    const int* script_ids,
    int script_count)
{
    assert(io);
    assert(script_ids);
    assert(script_count > 0);

    int* pending = malloc((size_t)script_count * sizeof(int));
    assert(pending);

    int pending_count = 0;
    for( int i = 0; i < script_count; i++ )
    {
        int script_id = script_ids[i];
        if( script_id < 0 || InterfaceX_HostIO_ClientScriptGet(io, script_id) )
            continue;

        bool duplicate = false;
        for( int j = 0; j < pending_count; j++ )
        {
            if( pending[j] == script_id )
            {
                duplicate = true;
                break;
            }
        }
        if( !duplicate )
            pending[pending_count++] = script_id;
    }

    if( pending_count > 0 )
    {
        struct Task_ClientScriptLoad* task =
            Task_ClientScriptLoad_New(io->aux_cache, pending, pending_count);
        if( task )
        {
            hostio_run_task(
                io, task, Task_ClientScriptLoad_Run, (void (*)(void*))Task_ClientScriptLoad_Free);
        }
    }

    for( int i = 0; i < pending_count; i++ )
        hostio_load_clientscript_sync(io, pending[i]);

    free(pending);
}

bool
InterfaceX_HostIO_GraphicSceneId(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out)
{
    if( !io || graphic_id < 0 || !scene_id_out )
        return false;

    for( struct InterfaceX_GraphicSceneCacheEntry* it = s_host_io_caches.graphic_scene_cache; it;
         it = it->next )
    {
        if( it->graphic_id == graphic_id )
        {
            *scene_id_out = it->scene_id;
            return true;
        }
    }

    return false;
}

bool
InterfaceX_HostIO_LoadGraphicScene(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out)
{
    assert(io);
    assert(graphic_id >= 0);
    assert(scene_id_out);

    if( InterfaceX_HostIO_GraphicSceneId(io, graphic_id, scene_id_out) )
        return true;

    struct InterfaceX_TaskSpriteLoad* task =
        InterfaceX_TaskSpriteLoad_New(io->aux_cache, graphic_id);
    if( !task )
    {
        fprintf(stderr, "hostio: graphic scene load alloc failed for graphic_id=%d\n", graphic_id);
        return false;
    }
    hostio_run_task(io, task, hostio_sprite_load_run, hostio_sprite_load_destroy);

    return InterfaceX_HostIO_FinalizeGraphicScene(io, graphic_id, scene_id_out);
}

int
InterfaceX_HostIO_ObjIconInventoryModelId(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count)
{
    struct Dat2BuildCache* bc;
    struct RSCacheDat2A_ConfigObject* obj;

    if( !io || obj_id < 0 )
        return -1;

    bc = dat2(io->aux_cache);
    if( !bc )
        return -1;

    obj = dat2_buildcache_object_get(bc, obj_id);
    if( obj && count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < 10; i++ )
        {
            if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
                countobj_id = obj->count_obj[i];
        }
        if( countobj_id >= 0 )
            obj = dat2_buildcache_object_get(bc, countobj_id);
    }

    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static bool
hostio_ensure_obj_icon_deps(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count)
{
    int model_id = InterfaceX_HostIO_ObjIconInventoryModelId(io, obj_id, count);
    if( model_id > 0 )
    {
        if( !InterfaceX_HostIO_LoadModel(io, model_id) )
        {
            fprintf(
                stderr,
                "hostio: obj icon inventory model load failed obj_id=%d model_id=%d\n",
                obj_id,
                model_id);
            return false;
        }
    }
    return true;
}

bool
InterfaceX_HostIO_FinalizeObjIconScene(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out)
{
    assert(io);
    assert(obj_id >= 0);
    assert(scene_id_out);

    if( InterfaceX_HostIO_ObjIconSceneId(io, obj_id, count, scene_id_out) )
        return true;

    ToriAuxLibCache_EnsureObjtype(io->aux_cache, obj_id);

    struct ToriAuxLibCore_Sprite* sprite =
        dat2_buildcache_obj_icon_sprite(dat2(io->aux_cache), io->scene, obj_id, count);
    if( !sprite )
    {
        struct Dat2BuildCache* bc = dat2(io->aux_cache);
        struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(bc, obj_id);
        int inventory_model_id = obj ? obj->inventory_model_id : -1;
        fprintf(
            stderr,
            "hostio: obj icon sprite missing for obj_id=%d count=%d inventory_model_id=%d "
            "model_cached=%d\n",
            obj_id,
            count,
            inventory_model_id,
            inventory_model_id > 0 && dat2_buildcache_model_get(bc, inventory_model_id) != NULL);
        return false;
    }

    int scene_id = -1;
    if( hostio_raster_sprite_to_scene(io, sprite, &scene_id) != 0 )
    {
        fprintf(stderr, "hostio: obj icon scene raster failed for obj_id=%d\n", obj_id);
        return false;
    }

    struct InterfaceX_ObjIconCacheEntry* entry =
        calloc(1, sizeof(struct InterfaceX_ObjIconCacheEntry));
    if( entry )
    {
        entry->obj_id = obj_id;
        entry->scene_id = scene_id;
        entry->next = s_host_io_caches.obj_icon_cache;
        s_host_io_caches.obj_icon_cache = entry;
    }

    *scene_id_out = scene_id;
    return true;
}

struct ToriDraw_Font*
InterfaceX_HostIO_SceneFontGet(
    struct InterfaceX_HostIO* io,
    int font_id)
{
    if( !io || font_id < 0 || !io->scene )
        return NULL;
    return ToriDraw_SceneFontGet(io->scene, font_id);
}

bool
InterfaceX_HostIO_LoadSceneFont(
    struct InterfaceX_HostIO* io,
    int font_id)
{
    assert(io);
    assert(font_id >= 0);

    if( InterfaceX_HostIO_SceneFontGet(io, font_id) )
        return true;

    struct InterfaceX_TaskFontLoad* task = InterfaceX_TaskFontLoad_New(io->aux_cache, font_id);
    if( !task )
    {
        fprintf(stderr, "hostio: font load alloc failed for font_id=%d\n", font_id);
        return false;
    }
    hostio_run_task(io, task, hostio_font_load_run, hostio_font_load_destroy);

    return InterfaceX_HostIO_FinalizeSceneFont(io, font_id);
}

bool
InterfaceX_HostIO_ObjIconSceneId(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out)
{
    assert(io);
    assert(obj_id >= 0);
    assert(scene_id_out);

    (void)count;
    for( struct InterfaceX_ObjIconCacheEntry* it = s_host_io_caches.obj_icon_cache; it;
         it = it->next )
    {
        if( it->obj_id == obj_id )
        {
            *scene_id_out = it->scene_id;
            return true;
        }
    }

    return false;
}

bool
InterfaceX_HostIO_ObjIconLoadFailed(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count)
{
    (void)io;
    (void)count;

    if( obj_id < 0 )
        return false;

    for( struct InterfaceX_ObjIconFailedEntry* it = s_host_io_caches.obj_icon_failed; it;
         it = it->next )
    {
        if( it->obj_id == obj_id )
            return true;
    }

    return false;
}

void
InterfaceX_HostIO_MarkObjIconLoadFailed(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count)
{
    (void)io;

    if( obj_id < 0 )
        return;

    if( InterfaceX_HostIO_ObjIconLoadFailed(io, obj_id, count) )
        return;

    struct InterfaceX_ObjIconFailedEntry* entry =
        calloc(1, sizeof(struct InterfaceX_ObjIconFailedEntry));
    if( !entry )
        return;

    entry->obj_id = obj_id;
    entry->count = count > 0 ? count : 1;
    entry->next = s_host_io_caches.obj_icon_failed;
    s_host_io_caches.obj_icon_failed = entry;
}

bool
InterfaceX_HostIO_LoadObjIconScene(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out)
{
    assert(io);
    assert(obj_id >= 0);
    assert(scene_id_out);

    if( InterfaceX_HostIO_ObjIconSceneId(io, obj_id, count, scene_id_out) )
        return true;

    if( !InterfaceX_HostIO_LoadObjectConfig(io, obj_id) )
    {
        fprintf(stderr, "hostio: object config load failed for obj_id=%d\n", obj_id);
        return false;
    }
    (void)hostio_ensure_obj_icon_deps(io, obj_id, count);

    return InterfaceX_HostIO_FinalizeObjIconScene(io, obj_id, count, scene_id_out);
}

bool
InterfaceX_HostIO_LoadModel(
    struct InterfaceX_HostIO* io,
    int model_id)
{
    if( !io || model_id < 0 )
        return false;

    if( dat2_buildcache_model_get(dat2(io->aux_cache), model_id) )
        return true;

    struct InterfaceX_TaskModelLoad* task = InterfaceX_TaskModelLoad_New(io->aux_cache, model_id);
    if( !task )
    {
        fprintf(stderr, "hostio: model load alloc failed for model_id=%d\n", model_id);
        return false;
    }
    hostio_run_task(io, task, hostio_model_load_run, hostio_model_load_destroy);

    if( !dat2_buildcache_model_get(dat2(io->aux_cache), model_id) )
    {
        fprintf(stderr, "hostio: model id=%d failed to populate after IO\n", model_id);
        return false;
    }
    return true;
}

static void
hostio_load_npc_chathead_models(
    struct InterfaceX_HostIO* io,
    int npc_id)
{
    struct Dat2BuildCache* bc;
    struct RSCacheDat2A_ConfigNpctype* npc;
    int i;

    if( !io || npc_id < 0 )
        return;

    bc = dat2(io->aux_cache);
    npc = dat2_buildcache_npctype_get(bc, npc_id);
    if( !npc || !npc->chathead_models )
        return;

    for( i = 0; i < npc->chathead_models_count; i++ )
    {
        int model_id = npc->chathead_models[i];
        if( model_id >= 0 )
            InterfaceX_HostIO_LoadModel(io, model_id);
    }
}

bool
InterfaceX_HostIO_LoadNpctype(
    struct InterfaceX_HostIO* io,
    int npc_id)
{
    struct Dat2BuildCache* bc;

    assert(io);
    assert(npc_id >= 0);

    bc = dat2(io->aux_cache);
    if( !dat2_buildcache_npctype_get(bc, npc_id) )
    {
        struct Task_ToriAuxLibCache_NpcAdd* task =
            Task_ToriAuxLibCache_NpcAdd_New(io->aux_cache, npc_id);
        if( !task )
        {
            fprintf(stderr, "hostio: npctype load alloc failed for npc_id=%d\n", npc_id);
            return false;
        }

        hostio_run_task(
            io,
            task,
            Task_ToriAuxLibCache_NpcAdd_Run,
            (void (*)(void*))Task_ToriAuxLibCache_NpcAdd_Free);

        if( !dat2_buildcache_npctype_get(bc, npc_id) )
        {
            fprintf(stderr, "hostio: npctype id=%d failed to populate after IO\n", npc_id);
            return false;
        }
    }

    hostio_load_npc_chathead_models(io, npc_id);
    return true;
}

bool
InterfaceX_HostIO_LoadPlayerAppearance(
    struct InterfaceX_HostIO* io,
    const int appearance[12])
{
    struct Dat2BuildCache* bc;
    int model_ids[256];
    int model_count;
    int i;

    if( !io || !appearance )
        return false;

    bc = dat2(io->aux_cache);
    model_count = runescape_appearance_collect_model_ids_dat2(bc, appearance, model_ids, 256);
    if( model_count <= 0 )
    {
        struct Task_ToriAuxLibCache_PlayerAdd* task = Task_ToriAuxLibCache_PlayerAdd_New(
            io->aux_cache,
            appearance,
            RUNESCAPE_EXAMPLE_PLAYER_READYANIM,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM,
            RUNESCAPE_EXAMPLE_PLAYER_TURNANIM,
            RUNESCAPE_EXAMPLE_PLAYER_RUNANIM,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_B,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_R,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_L);
        if( !task )
        {
            fprintf(stderr, "hostio: player appearance load alloc failed\n");
            return false;
        }

        hostio_run_task(
            io,
            task,
            Task_ToriAuxLibCache_PlayerAdd_Run,
            (void (*)(void*))Task_ToriAuxLibCache_PlayerAdd_Free);

        model_count = runescape_appearance_collect_model_ids_dat2(bc, appearance, model_ids, 256);
    }

    for( i = 0; i < model_count; i++ )
    {
        if( !InterfaceX_HostIO_LoadModel(io, model_ids[i]) )
            return false;
    }

    return model_count > 0;
}

bool
InterfaceX_HostIO_PromoteObjtype(
    struct InterfaceX_HostIO* io,
    int obj_id)
{
    if( !io || obj_id < 0 )
        return false;

    InterfaceX_HostIO_LoadObjectConfig(io, obj_id);
    return ToriAuxLibCache_PromoteObjtype(io->aux_cache, obj_id);
}

bool
InterfaceX_HostIO_LoadObjectConfig(
    struct InterfaceX_HostIO* io,
    int obj_id)
{
    assert(io);
    assert(obj_id >= 0);

    if( dat2_buildcache_object_get(dat2(io->aux_cache), obj_id) )
        return true;

    return InterfaceX_HostIO_LoadConfigEntry(io, RSCacheDat2A_ConfigKind_Object, obj_id);
}

int
InterfaceX_HostIO_LoadModelScene(
    struct InterfaceX_HostIO* io,
    int model_id,
    int zoom,
    int xan,
    int yan,
    int width,
    int height)
{
    assert(io);
    assert(model_id >= 0);
    assert(io->scene);
    assert(io->next_scene_id);

    InterfaceX_HostIO_LoadModel(io, model_id);

    struct ToriAuxLibCore_Sprite* sprite = dat2_buildcache_widget_model_sprite(
        dat2(io->aux_cache), io->scene, model_id, zoom, xan, yan, width, height);
    if( !sprite )
        return -1;

    int scene_id = -1;
    if( hostio_raster_sprite_to_scene(io, sprite, &scene_id) != 0 )
        return -1;
    return scene_id;
}

static bool
batch_model_id_list_contains(
    int const* ids,
    int count,
    int model_id)
{
    for( int i = 0; i < count; i++ )
    {
        if( ids[i] == model_id )
            return true;
    }
    return false;
}

static void
batch_model_id_list_add(
    int* ids,
    int* count,
    int max_count,
    int model_id)
{
    if( model_id < 0 || *count >= max_count )
        return;
    if( batch_model_id_list_contains(ids, *count, model_id) )
        return;
    ids[(*count)++] = model_id;
}

static bool
batch_player_appearance_eq(
    int const* a,
    int const* b)
{
    return memcmp(a, b, (size_t)RUNESCAPE_APPEARANCE_SLOT_COUNT * sizeof(int)) == 0;
}

static void
batch_collect_deps_from_args(struct InterfaceX_TaskBatchModelLoad* task)
{
    assert(task);

    task->npc_count = 0;
    task->obj_count = 0;
    task->player_count = 0;

    for( int i = 0; i < task->arg_count; i++ )
    {
        struct InterfaceX_ModelLoadArg const* arg = &task->args[i];

        switch( arg->kind )
        {
        case INTERFACEX_MLOAD_PLAIN:
            break;
        case INTERFACEX_MLOAD_NPC:
            if( arg->u.npc_id >= 0 && task->npc_count < INTERFACEX_BATCH_MODEL_MAX_NPC )
                task->npc_ids[task->npc_count++] = arg->u.npc_id;
            break;
        case INTERFACEX_MLOAD_OBJ:
            if( arg->u.obj_id >= 0 && task->obj_count < INTERFACEX_BATCH_MODEL_MAX_OBJ )
                task->obj_ids[task->obj_count++] = arg->u.obj_id;
            break;
        case INTERFACEX_MLOAD_PLAYER:
        {
            bool found = false;
            for( int p = 0; p < task->player_count; p++ )
            {
                if( batch_player_appearance_eq(task->player_appearances[p], arg->u.appearance) )
                {
                    found = true;
                    break;
                }
            }
            if( !found && task->player_count < INTERFACEX_BATCH_MODEL_MAX_PLAYER )
            {
                memcpy(
                    task->player_appearances[task->player_count],
                    arg->u.appearance,
                    (size_t)RUNESCAPE_APPEARANCE_SLOT_COUNT * sizeof(int));
                task->player_count++;
            }
            break;
        }
        default:
            break;
        }
    }
}

static void
batch_collect_model_ids_after_configs(
    struct InterfaceX_TaskBatchModelLoad* task,
    struct Dat2BuildCache* bc)
{
    assert(task);
    assert(bc);

    for( int i = 0; i < task->arg_count; i++ )
    {
        struct InterfaceX_ModelLoadArg const* arg = &task->args[i];

        switch( arg->kind )
        {
        case INTERFACEX_MLOAD_PLAIN:
            batch_model_id_list_add(
                task->needed_model_ids,
                &task->needed_model_count,
                INTERFACEX_BATCH_MODEL_MAX_IDS,
                arg->u.model_id);
            break;
        case INTERFACEX_MLOAD_NPC:
        {
            struct RSCacheDat2A_ConfigNpctype* npc = dat2_buildcache_npctype_get(bc, arg->u.npc_id);
            if( !npc || !npc->chathead_models )
                break;
            for( int m = 0; m < npc->chathead_models_count; m++ )
            {
                batch_model_id_list_add(
                    task->needed_model_ids,
                    &task->needed_model_count,
                    INTERFACEX_BATCH_MODEL_MAX_IDS,
                    npc->chathead_models[m]);
            }
            break;
        }
        case INTERFACEX_MLOAD_OBJ:
        {
            struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(bc, arg->u.obj_id);
            if( obj && obj->inventory_model_id > 0 )
            {
                batch_model_id_list_add(
                    task->needed_model_ids,
                    &task->needed_model_count,
                    INTERFACEX_BATCH_MODEL_MAX_IDS,
                    obj->inventory_model_id);
            }
            break;
        }
        case INTERFACEX_MLOAD_PLAYER:
        {
            int model_ids[256];
            int model_count =
                runescape_appearance_collect_model_ids_dat2(bc, arg->u.appearance, model_ids, 256);
            for( int m = 0; m < model_count; m++ )
            {
                batch_model_id_list_add(
                    task->needed_model_ids,
                    &task->needed_model_count,
                    INTERFACEX_BATCH_MODEL_MAX_IDS,
                    model_ids[m]);
            }
            break;
        }
        default:
            break;
        }
    }
}

static struct ToriDraw_Model*
batch_build_td_model_for_arg(
    struct Dat2BuildCache* bc,
    struct InterfaceX_ModelLoadArg const* arg)
{
    struct RSCacheDat2A_Model* raw;
    struct RSCacheDat2A_Model* merged;
    struct RSCacheDat2A_Model* raw_models[16];
    int model_count;
    int i;

    if( !bc || !arg )
        return NULL;

    switch( arg->kind )
    {
    case INTERFACEX_MLOAD_PLAIN:
        if( arg->u.model_id < 0 )
            return NULL;
        raw = dat2_buildcache_model_get(bc, arg->u.model_id);
        if( !raw )
            return NULL;
        raw = RSCacheDat2A_ModelNewCopy(raw);
        break;

    case INTERFACEX_MLOAD_OBJ:
    {
        struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(bc, arg->u.obj_id);
        if( !obj || obj->inventory_model_id <= 0 )
            return NULL;
        raw = dat2_buildcache_model_get(bc, obj->inventory_model_id);
        if( !raw )
            return NULL;
        raw = RSCacheDat2A_ModelNewCopy(raw);
        break;
    }

    case INTERFACEX_MLOAD_NPC:
        if( arg->u.npc_id < 0 )
            return NULL;
        {
            struct RSCacheDat2A_ConfigNpctype* npc = dat2_buildcache_npctype_get(bc, arg->u.npc_id);
            if( !npc || !npc->chathead_models || npc->chathead_models_count <= 0 )
                return NULL;

            model_count = 0;
            for( i = 0; i < npc->chathead_models_count && model_count < 16; i++ )
            {
                int model_id = npc->chathead_models[i];
                if( model_id < 0 )
                    continue;
                raw_models[model_count] = dat2_buildcache_model_get(bc, model_id);
                if( raw_models[model_count] )
                    model_count++;
            }
            if( model_count <= 0 )
                return NULL;
            merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
            if( !merged )
                return NULL;
            raw = merged;
        }
        break;

    case INTERFACEX_MLOAD_PLAYER:
    {
        int model_ids[256];
        int id_count;

        id_count =
            runescape_appearance_collect_model_ids_dat2(bc, arg->u.appearance, model_ids, 256);
        if( id_count <= 0 )
            return NULL;

        model_count = 0;
        for( i = 0; i < id_count && model_count < 16; i++ )
        {
            raw_models[model_count] = dat2_buildcache_model_get(bc, model_ids[i]);
            if( raw_models[model_count] )
                model_count++;
        }
        if( model_count <= 0 )
            return NULL;

        merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
        if( !merged )
            return NULL;
        raw = merged;
        break;
    }

    default:
        return NULL;
    }

    if( !raw )
        return NULL;

    {
        struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(raw);
        RSCacheDat2A_ModelFree(raw);
        if( !td_model )
            return NULL;
        ToriDraw_ModelSetBoundsCylinder(td_model);
        return td_model;
    }
}

static int
batch_register_scene_model(
    struct InterfaceX_TaskBatchModelLoad* task,
    struct InterfaceX_ModelLoadArg const* arg,
    struct ToriDraw_Model* td_model)
{
    struct ToriDraw_ModelHandle hnd;

    assert(task);
    assert(arg);
    assert(td_model);
    assert(task->scene);
    assert(task->next_scene_id);

    if( arg->kind == INTERFACEX_MLOAD_PLAIN )
    {
        int model_id = arg->u.model_id;
        if( ToriDraw_SceneModelHas(task->scene, model_id) )
        {
            ToriDraw_ModelFree(td_model);
            return model_id;
        }

        hnd = (struct ToriDraw_ModelHandle){
            .kind = TORIDRAWMK_MODEL,
            .u.model.model = td_model,
        };
        ToriDraw_SceneModelAdd(task->scene, model_id, hnd);
        return model_id;
    }

    {
        int scene_id = (*task->next_scene_id)++;
        hnd = (struct ToriDraw_ModelHandle){
            .kind = TORIDRAWMK_MODEL,
            .u.model.model = td_model,
        };
        ToriDraw_SceneModelAdd(task->scene, scene_id, hnd);
        return scene_id;
    }
}

static void
batch_model_load_destroy(void* state)
{
    struct InterfaceX_TaskBatchModelLoad* task = state;
    if( !task )
        return;

    if( task->npc_child )
        Task_ToriAuxLibCache_NpcAdd_Free(task->npc_child);
    if( task->player_child )
        Task_ToriAuxLibCache_PlayerAdd_Free(task->player_child);
    if( task->obj_config_child )
        Task_Dat2ConfigEntryLoad_Free(task->obj_config_child);

    free(task->args);
    free(task->results);
    free(task);
}

static int
batch_model_load_run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct InterfaceX_TaskBatchModelLoad* task = task_state;
    struct Dat2BuildCache* bc;

    if( !task || !task->cache )
        return PT_EXITED;

    bc = dat2(task->cache);

    PT_BEGIN(&task->thread);

    if( !task->scene || !task->next_scene_id || task->arg_count <= 0 || !bc )
        PT_EXIT(&task->thread);

    for( task->init_i = 0; task->init_i < task->arg_count; task->init_i++ )
    {
        task->results[task->init_i].user_id = task->args[task->init_i].user_id;
        task->results[task->init_i].scene_id = -1;
    }

    task->needed_model_count = 0;
    batch_collect_deps_from_args(task);

    task->npc_index = 0;
    while( task->npc_index < task->npc_count )
    {
        if( dat2_buildcache_npctype_get(bc, task->npc_ids[task->npc_index]) )
        {
            task->npc_index++;
            continue;
        }

        if( task->npc_child )
            Task_ToriAuxLibCache_NpcAdd_Free(task->npc_child);
        task->npc_child =
            Task_ToriAuxLibCache_NpcAdd_New(task->cache, task->npc_ids[task->npc_index]);
        if( !task->npc_child )
        {
            fprintf(
                stderr,
                "hostio: batch model load npc alloc failed npc_id=%d\n",
                task->npc_ids[task->npc_index]);
            task->npc_index++;
            continue;
        }

        TASK_AWAIT(&task->thread, Task_ToriAuxLibCache_NpcAdd_Run(task->npc_child, ctx));
        Task_ToriAuxLibCache_NpcAdd_Free(task->npc_child);
        task->npc_child = NULL;
        task->npc_index++;
    }

    task->player_index = 0;
    while( task->player_index < task->player_count )
    {
        task->player_model_count = runescape_appearance_collect_model_ids_dat2(
            bc, task->player_appearances[task->player_index], task->player_model_ids, 256);
        if( task->player_model_count > 0 )
        {
            task->player_index++;
            continue;
        }

        if( task->player_child )
            Task_ToriAuxLibCache_PlayerAdd_Free(task->player_child);
        task->player_child = Task_ToriAuxLibCache_PlayerAdd_New(
            task->cache,
            task->player_appearances[task->player_index],
            RUNESCAPE_EXAMPLE_PLAYER_READYANIM,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM,
            RUNESCAPE_EXAMPLE_PLAYER_TURNANIM,
            RUNESCAPE_EXAMPLE_PLAYER_RUNANIM,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_B,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_R,
            RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_L);
        if( !task->player_child )
        {
            fprintf(stderr, "hostio: batch model load player alloc failed\n");
            task->player_index++;
            continue;
        }

        TASK_AWAIT(&task->thread, Task_ToriAuxLibCache_PlayerAdd_Run(task->player_child, ctx));
        Task_ToriAuxLibCache_PlayerAdd_Free(task->player_child);
        task->player_child = NULL;
        task->player_index++;
    }

    task->need_obj_load = false;
    if( task->obj_count > 0 )
    {
        for( task->init_i = 0; task->init_i < task->obj_count; task->init_i++ )
        {
            if( !dat2_buildcache_object_get(bc, task->obj_ids[task->init_i]) )
            {
                task->need_obj_load = true;
                break;
            }
        }

        if( task->need_obj_load )
        {
            if( task->obj_config_child )
                Task_Dat2ConfigEntryLoad_Free(task->obj_config_child);
            task->obj_config_child = Task_Dat2ConfigEntryLoad_New(
                task->cache, RSCacheDat2A_ConfigKind_Object, task->obj_ids, task->obj_count);
            if( task->obj_config_child )
            {
                TASK_AWAIT(
                    &task->thread, Task_Dat2ConfigEntryLoad_Run(task->obj_config_child, ctx));
                Task_Dat2ConfigEntryLoad_Free(task->obj_config_child);
                task->obj_config_child = NULL;
            }
        }
    }

    task->needed_model_count = 0;
    batch_collect_model_ids_after_configs(task, bc);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Models);

    task->prefetch_index = 0;
    while( task->prefetch_index < task->needed_model_count )
    {
        task->batch_end = task->prefetch_index + INTERFACEX_BATCH_MODEL_IO_CHUNK;
        if( task->batch_end > task->needed_model_count )
            task->batch_end = task->needed_model_count;

        LibToriRS_IOBatchReset(&task->io_batch);
        for( task->init_i = task->prefetch_index; task->init_i < task->batch_end; task->init_i++ )
        {
            if( dat2_buildcache_model_get(bc, task->needed_model_ids[task->init_i]) )
                continue;

            task->decode_i =
                LibToriRS_IOBatchAdd(&task->io_batch, task->needed_model_ids[task->init_i]);
            IO_REQUEST(
                ctx,
                task->decode_i,
                TAPIDat2_FetchModel(ctx, task->needed_model_ids[task->init_i]));
        }
        task->prefetch_index = task->batch_end;

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( task->decode_i = 0; task->decode_i < LibToriRS_IOBatchCount(&task->io_batch);
             task->decode_i++ )
        {
            int model_id = LibToriRS_IOBatchUser(&task->io_batch, task->decode_i);
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, task->decode_i);
            if( !model )
            {
                fprintf(stderr, "hostio: batch model decode failed model_id=%d\n", model_id);
                continue;
            }
            dat2_buildcache_model_add(bc, model_id, model);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    task->scene_build_index = 0;
    while( task->scene_build_index < task->arg_count )
    {
        struct InterfaceX_ModelLoadArg const* arg = &task->args[task->scene_build_index];
        struct InterfaceX_ModelLoadResult* result = &task->results[task->scene_build_index];

        task->scene_id = -1;
        if( arg->kind == INTERFACEX_MLOAD_PLAIN &&
            ToriDraw_SceneModelHas(task->scene, arg->u.model_id) )
        {
            task->scene_id = arg->u.model_id;
        }
        else
        {
            task->td_model = batch_build_td_model_for_arg(bc, arg);
            if( task->td_model )
                task->scene_id = batch_register_scene_model(task, arg, task->td_model);
            else
            {
                fprintf(
                    stderr,
                    "hostio: batch model scene build failed user_id=0x%x kind=%d\n",
                    arg->user_id,
                    (int)arg->kind);
            }
        }

        result->scene_id = task->scene_id;
        task->scene_build_index++;
    }

    PT_END(&task->thread);
}

struct InterfaceX_BatchModelLoad*
InterfaceX_BatchModelLoad_New(
    struct InterfaceX_HostIO* io,
    struct InterfaceX_ModelLoadArg const* args,
    int arg_count)
{
    struct InterfaceX_BatchModelLoad* batch;
    struct InterfaceX_TaskBatchModelLoad* task;

    if( !io || arg_count <= 0 || !args )
        return NULL;

    batch = calloc(1, sizeof(*batch));
    if( !batch )
        return NULL;

    task = calloc(1, sizeof(*task));
    if( !task )
    {
        free(batch);
        return NULL;
    }

    task->args = calloc((size_t)arg_count, sizeof(*task->args));
    task->results = calloc((size_t)arg_count, sizeof(*task->results));
    if( !task->args || !task->results )
    {
        free(task->args);
        free(task->results);
        free(task);
        free(batch);
        return NULL;
    }

    memcpy(task->args, args, (size_t)arg_count * sizeof(*task->args));
    PT_INIT(&task->thread);
    task->cache = io->aux_cache;
    task->scene = io->scene;
    task->next_scene_id = io->next_scene_id;
    task->arg_count = arg_count;

    batch->task = task;
    batch->arg_count = arg_count;
    return batch;
}

void
InterfaceX_BatchModelLoad_Destroy(struct InterfaceX_BatchModelLoad* batch)
{
    if( !batch )
        return;
    if( batch->task )
        batch_model_load_destroy(batch->task);
    free(batch);
}

void
InterfaceX_BatchModelLoad_Queue(
    struct InterfaceX_BatchModelLoad* batch,
    struct InterfaceX_HostIO* io)
{
    assert(batch);
    assert(io);
    assert(batch->task);

    InterfaceX_HostIO_QueueTask(
        io, batch->task, batch_model_load_run, batch_model_load_destroy_runner);
}

struct InterfaceX_ModelLoadResult const*
InterfaceX_BatchModelLoad_Results(
    struct InterfaceX_BatchModelLoad const* batch,
    int* count_out)
{
    if( count_out )
        *count_out = 0;
    if( !batch || !batch->task || !batch->task->results )
        return NULL;
    if( count_out )
        *count_out = batch->arg_count;
    return batch->task->results;
}

int
InterfaceX_BatchModelLoad_RunAwait(
    struct InterfaceX_BatchModelLoad* batch,
    struct LibToriRS_IOContext* ctx)
{
    if( !batch || !batch->task )
        return PT_EXITED;
    return batch_model_load_run(batch->task, ctx);
}
