#include "interfacex_host_io.h"

#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "platforms/platform_x/cachelib.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "platforms/platform_x_io_reactor.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/task_clientscript_load.h"
#include "toriauxlib/core/tasks/task_dat2_config_entry_load.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_font.h"
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

struct InterfaceX_HostIOCaches
{
    struct InterfaceX_GraphicSceneCacheEntry* graphic_scene_cache;
    struct InterfaceX_ObjIconCacheEntry* obj_icon_cache;
};

static struct InterfaceX_HostIOCaches s_host_io_caches;

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

static void
hostio_drain_io(struct InterfaceX_HostIO* io)
{
    while( core_task_runner_has_live(io->task_runner) )
    {
        while( core_task_runner_run(io->task_runner) )
            LibToriPlatformX_IOReactorProcess(io->io_reactor, io->io_queue);
        LibToriPlatformX_IOReactorProcess(io->io_reactor, io->io_queue);
    }
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
    core_task_runner_add(io->task_runner, task_state, run_fn, destroy_fn);
    hostio_drain_io(io);
}

static void
hostio_sprite_load_destroy(void* state)
{
    free(state);
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
        PT_EXIT(&task->thread);

    if( dat2_buildcache_dynamic_sprite_has(bc, task->sprite_id) ||
        ToriAuxLibCore_SpriteHas(ToriAuxLibCache_Core(task->cache), task->sprite_id) )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Sprites);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchSprite(ctx, task->sprite_id));
    PT_YIELD(&task->thread);

    {
        struct RSCacheDat2Disk_Archive* archive =
            TAPIDat2_DecodeSpriteArchive(ctx, 0, task->sprite_id);
        if( !archive )
            PT_EXIT(&task->thread);

        struct ToriAuxLibCore_Sprite* sprite =
            dat2_buildcache_sprite_decode_id_from_archive(archive, task->sprite_id);
        if( !sprite )
            PT_EXIT(&task->thread);

        ToriAuxLibCache_SubmitSprite(task->cache, task->sprite_id, sprite);
    }

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
}

static void
hostio_font_load_destroy(void* state)
{
    free(state);
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
        PT_EXIT(&task->thread);

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
        if( font_archive && sprite_archive )
            dat2_buildcache_font_add_from_archives(bc, task->font_id, font_archive, sprite_archive);
        if( font_archive )
            RSCacheDat2Disk_ArchiveFree(font_archive);
        if( sprite_archive )
            RSCacheDat2Disk_ArchiveFree(sprite_archive);
    }

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
}

static void
hostio_model_load_destroy(void* state)
{
    free(state);
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
        PT_EXIT(&task->thread);

    if( dat2_buildcache_model_get(bc, task->model_id) )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->thread, bc, RSCacheDat2Disk_Table_Models);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, task->model_id));
    PT_YIELD(&task->thread);

    {
        struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, 0);
        if( model )
            dat2_buildcache_model_add(bc, task->model_id, model);
    }

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->thread);
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

    io->task_runner = core_task_runner_new(io->io_queue);
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
        core_task_runner_free(io->task_runner);
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

void
InterfaceX_HostIO_LoadConfigEntries(
    struct InterfaceX_HostIO* io,
    int config_kind,
    const int* ids,
    int id_count)
{
    int* pending = NULL;
    int pending_count = 0;

    assert(io);
    assert(ids);
    assert(id_count > 0);

    pending = malloc((size_t)id_count * sizeof(int));
    if( !pending )
        return;

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
        if( task )
        {
            hostio_run_task(
                io,
                task,
                Task_Dat2ConfigEntryLoad_Run,
                (void (*)(void*))Task_Dat2ConfigEntryLoad_Free);
        }
    }

    free(pending);
}

void
InterfaceX_HostIO_LoadConfigEntry(
    struct InterfaceX_HostIO* io,
    int config_kind,
    int id)
{
    InterfaceX_HostIO_LoadConfigEntries(io, config_kind, &id, 1);
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

void
InterfaceX_HostIO_LoadClientScript(
    struct InterfaceX_HostIO* io,
    int script_id)
{
    assert(io);
    assert(script_id >= 0);
    if( InterfaceX_HostIO_ClientScriptGet(io, script_id) )
        return;

    struct Task_ClientScriptLoad* task = Task_ClientScriptLoad_New(io->aux_cache, &script_id, 1);
    if( !task )
        return;

    hostio_run_task(
        io, task, Task_ClientScriptLoad_Run, (void (*)(void*))Task_ClientScriptLoad_Free);

    hostio_load_clientscript_sync(io, script_id);
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

void
InterfaceX_HostIO_LoadGraphicScene(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out)
{
    assert(io);
    assert(graphic_id >= 0);
    assert(scene_id_out);

    if( InterfaceX_HostIO_GraphicSceneId(io, graphic_id, scene_id_out) )
        return;

    struct InterfaceX_TaskSpriteLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return;
    PT_INIT(&task->thread);
    task->cache = io->aux_cache;
    task->sprite_id = graphic_id;
    hostio_run_task(io, task, hostio_sprite_load_run, hostio_sprite_load_destroy);

    struct ToriAuxLibCore_Sprite* sprite = ToriAuxLibCore_SpriteGet(io->core, graphic_id);
    if( !sprite )
        return;

    int scene_id = -1;
    if( hostio_raster_sprite_to_scene(io, sprite, &scene_id) != 0 )
        return;

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

void
InterfaceX_HostIO_LoadSceneFont(
    struct InterfaceX_HostIO* io,
    int font_id)
{
    assert(io);
    assert(font_id >= 0);

    if( InterfaceX_HostIO_SceneFontGet(io, font_id) )
        return;

    struct InterfaceX_TaskFontLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return;
    PT_INIT(&task->thread);
    task->cache = io->aux_cache;
    task->font_id = font_id;
    hostio_run_task(io, task, hostio_font_load_run, hostio_font_load_destroy);

    ToriAuxLibCache_SubmitFontFromDat2(io->aux_cache, font_id);

    struct ToriAuxLibCore_Font* core_font = ToriAuxLibCore_FontGet(io->core, font_id);
    if( !core_font )
        return;

    struct ToriDraw_Font* font = hostio_font_new_from_core(core_font);
    if( font )
        ToriDraw_SceneFontAdd(io->scene, font_id, font);
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

void
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
        return;

    InterfaceX_HostIO_LoadObjectConfig(io, obj_id);
    ToriAuxLibCache_EnsureObjtype(io->aux_cache, obj_id);

    struct ToriAuxLibCore_Sprite* sprite =
        dat2_buildcache_obj_icon_sprite(dat2(io->aux_cache), io->scene, obj_id, count);
    if( !sprite )
        return;

    int scene_id = -1;
    if( hostio_raster_sprite_to_scene(io, sprite, &scene_id) != 0 )
        return;

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
}

void
InterfaceX_HostIO_LoadModel(
    struct InterfaceX_HostIO* io,
    int model_id)
{
    if( !io || model_id < 0 )
        return;

    if( dat2_buildcache_model_get(dat2(io->aux_cache), model_id) )
        return;

    struct InterfaceX_TaskModelLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return;
    PT_INIT(&task->thread);
    task->cache = io->aux_cache;
    task->model_id = model_id;
    hostio_run_task(io, task, hostio_model_load_run, hostio_model_load_destroy);
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

void
InterfaceX_HostIO_LoadObjectConfig(
    struct InterfaceX_HostIO* io,
    int obj_id)
{
    assert(io);
    assert(obj_id >= 0);

    if( dat2_buildcache_object_get(dat2(io->aux_cache), obj_id) )
        return;

    InterfaceX_HostIO_LoadConfigEntry(io, RSCacheDat2A_ConfigKind_Object, obj_id);
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
