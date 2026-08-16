#include "osrs/datatypes/appearances.h"
#include "osrs/texture.h"
#include "toriauxlib/core/tasks/component_load_types.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "toriauxlib/core/tasks/toriauxlib_tasks.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_scene.h"
#include "revconfig/revconfig.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
interfacex_stub_task_run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    (void)base;
    (void)ctx;
    return PT_EXITED;
}

static void
interfacex_stub_task_free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_interfacex_stub_task_vtable = {
    .run_fn = interfacex_stub_task_run,
    .free_fn = interfacex_stub_task_free,
};

static struct LibToriRS_Task*
interfacex_stub_task_new(void)
{
    struct LibToriRS_Task* task = calloc(1, sizeof(*task));
    if( task )
        task->vtable = &g_interfacex_stub_task_vtable;
    return task;
}

struct LibToriRS_Task*
Task_Dat1ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks)
{
    (void)cache;
    (void)scene;
    (void)rc_ctx;
    (void)root_component_id;
    (void)callbacks;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat1InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    (void)cache;
    (void)rc_ctx;
    (void)inv_item;
    (void)callbacks;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat1SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item)
{
    (void)rc_ctx;
    (void)cache;
    (void)item;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat1FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item)
{
    (void)rc_ctx;
    (void)cache;
    (void)item;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat2ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks)
{
    (void)cache;
    (void)scene;
    (void)rc_ctx;
    (void)root_component_id;
    (void)callbacks;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat2InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    (void)cache;
    (void)rc_ctx;
    (void)inv_item;
    (void)callbacks;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat2SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item)
{
    (void)rc_ctx;
    (void)cache;
    (void)item;
    return interfacex_stub_task_new();
}

struct LibToriRS_Task*
Task_Dat2FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item)
{
    (void)rc_ctx;
    (void)cache;
    (void)item;
    return interfacex_stub_task_new();
}

void
appearances_decode(
    struct AppearanceOp* op,
    uint16_t* appearances,
    int slot)
{
    (void)appearances;
    (void)slot;
    if( op )
    {
        op->kind = APPEARANCE_KIND_NONE;
        op->id = 0;
    }
}

int
cs1vm_script_length(int const* script)
{
    (void)script;
    return 0;
}

struct ToriDraw_Texture*
texture_new_toridraw_from_texture_sprite(
    struct RSCacheDat1A_ConfigTexture* texture,
    int animation_direction,
    int animation_speed,
    bool upscale_to_128,
    bool half_to_64)
{
    (void)texture;
    (void)animation_direction;
    (void)animation_speed;
    (void)upscale_to_128;
    (void)half_to_64;
    return NULL;
}

struct ToriDraw_Texture*
texture_new_toridraw_from_definition_packs(
    struct RSCacheDat2A_Texture* texture_definition,
    struct RSCacheDat2A_SpritePack** packs)
{
    (void)texture_definition;
    (void)packs;
    return NULL;
}

int
ui_minimenu_debug_enabled(void)
{
    return 0;
}

void
ui_minimenu_debug_log(char const* fmt, ...)
{
    (void)fmt;
}
