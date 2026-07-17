#include "uitree_builder.h"
#include "uitree_builder_manifest.h"

#include "engine/cache_provider.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_UIBuilderAssetsLoad
{
    struct ToriRS_Task task;
    struct pt pt;

    struct UITreeBuilder* builder;
    struct UIBuilderManifest const* manifest;

    int i;
    int* unique_objs;
    int unique_obj_count;
    int unique_obj_cap;
};

static int
unique_obj_add(
    struct Task_UIBuilderAssetsLoad* self,
    int obj_id)
{
    assert(self);
    if( obj_id <= 0 )
        return 0;
    for( int i = 0; i < self->unique_obj_count; i++ )
    {
        if( self->unique_objs[i] == obj_id )
            return 0;
    }
    if( self->unique_obj_count >= self->unique_obj_cap )
    {
        int n = self->unique_obj_cap == 0 ? 16 : self->unique_obj_cap * 2;
        int* grown = realloc(self->unique_objs, (size_t)n * sizeof(int));
        assert(grown);
        self->unique_objs = grown;
        self->unique_obj_cap = n;
    }
    self->unique_objs[self->unique_obj_count++] = obj_id;
    return 1;
}

static void
collect_unique_objs(struct Task_UIBuilderAssetsLoad* self)
{
    assert(self && self->manifest);
    for( int i = 0; i < self->manifest->inv_count; i++ )
    {
        struct UIBuilderInvSeed const* seed = &self->manifest->invs[i];
        for( int j = 0; j < seed->item_count; j++ )
            unique_obj_add(self, seed->obj_ids[j]);
    }
}

static int
Task_UIBuilderAssetsLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_UIBuilderAssetsLoad* self = (struct Task_UIBuilderAssetsLoad*)base;
    assert(self->builder);
    assert(self->manifest);
    assert(self->builder->provider);

    PT_BEGIN(&self->pt);

    /* Sprites — register names first so bake can resolve even if a leaf load fails. */
    for( self->i = 0; self->i < self->manifest->sprite_count; self->i++ )
    {
        struct UIBuilderSpriteReq const* req = &self->manifest->sprites[self->i];
        UITreeBuilder_RegisterSprite(
            self->builder,
            req->name,
            req->archive_id,
            req->atlas_index,
            req->atlas_count);
    }
    for( self->i = 0; self->i < self->manifest->sprite_count; self->i++ )
    {
        struct UIBuilderSpriteReq const* req = &self->manifest->sprites[self->i];
        if( req->archive_id >= 0 )
            TASK_AWAITSELF_IF(CreateTask_SpriteLoad(self->builder->provider, req->archive_id));
    }

    /* Fonts */
    for( self->i = 0; self->i < self->manifest->font_count; self->i++ )
    {
        struct UIBuilderFontReq const* req = &self->manifest->fonts[self->i];
        UITreeBuilder_RegisterFont(
            self->builder, req->name, req->archive_id, req->cache_font_id);
    }
    for( self->i = 0; self->i < self->manifest->font_count; self->i++ )
    {
        struct UIBuilderFontReq const* req = &self->manifest->fonts[self->i];
        if( req->archive_id >= 0 )
            TASK_AWAITSELF_IF(CreateTask_FontLoad(self->builder->provider, req->archive_id));
    }

    /* Unique inv objs */
    collect_unique_objs(self);
    for( self->i = 0; self->i < self->unique_obj_count; self->i++ )
    {
        TASK_AWAITSELF_IF(
            CreateTask_ObjLoad(self->builder->provider, self->unique_objs[self->i]));
    }

    /* Components / packs */
    for( self->i = 0; self->i < self->manifest->component_count; self->i++ )
    {
        struct UIBuilderComponentReq const* req = &self->manifest->components[self->i];
        TASK_AWAITSELF_IF(
            CreateTask_ComponentLoad(self->builder->provider, req->packed_id));
    }

    PT_END(&self->pt);
}

static void
Task_UIBuilderAssetsLoad_Free(struct ToriRS_Task* base)
{
    struct Task_UIBuilderAssetsLoad* self = (struct Task_UIBuilderAssetsLoad*)base;
    free(self->unique_objs);
    free(self);
}

static struct ToriRS_TaskVTable Task_UIBuilderAssetsLoad_VTable = {
    .run = Task_UIBuilderAssetsLoad_Run,
    .free = Task_UIBuilderAssetsLoad_Free,
};

struct ToriRS_Task*
CreateTask_UIBuilderAssetsLoad(
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest)
{
    assert(builder);
    assert(manifest);

    struct Task_UIBuilderAssetsLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_UIBuilderAssetsLoad_VTable;
    strncpy(task->task.name, "UIBuilderAssetsLoad", sizeof(task->task.name) - 1);
    task->builder = builder;
    task->manifest = manifest;
    PT_INIT(&task->pt);
    return &task->task;
}
