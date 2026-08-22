#include "task_static_sprites_load.h"

#include "engine/cache_provider.h"
#include "engine/static_sprites.h"
#include "engine/uitree_scene_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_StaticSpritesLoad
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    struct UITreeSceneBridge* bridge;
    int slot;
};

static int
Task_StaticSpritesLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_StaticSpritesLoad* self = (struct Task_StaticSpritesLoad*)base;

    assert(self->provider);
    assert(self->bridge);
    (void)io;

    PT_BEGIN(&self->pt);

    /*
     * Pure bind pass: Task_UIBuilderAssetsLoad has already decoded every
     * `[sprite:…]` the profile declared and registered it under its section
     * name, so all this does is point the host's slots at the ones it draws
     * itself. Nothing here names an archive — a slot the profile did not
     * declare simply stays unbound, and the overlay that wanted it draws
     * nothing.
     *
     * Locals never survive a yield; there are none to survive here, and the
     * loop body no longer yields at all.
     */
    for( self->slot = 0; self->slot < STATIC_SPRITE_COUNT; self->slot++ )
    {
        char const* name = StaticSprite_SlotName((enum StaticSpriteSlot)self->slot);
        int sprite_id;
        int scene_id;

        if( UITreeSceneBridge_StaticSpriteSceneId(
                self->bridge, (enum StaticSpriteSlot)self->slot) > 0 )
            continue;

        sprite_id = CacheProvider_SpriteIdByName(self->provider, name);
        if( sprite_id < 0 || !CacheProvider_SpriteHas(self->provider, sprite_id) )
        {
            /* Era-absent or undeclared: leave the slot unbound. */
            if( getenv("TORIRS_STATIC_SPRITE_DEBUG") )
                fprintf(stderr, "static_sprite: '%s' unresolved\n", name);
            continue;
        }

        scene_id = UITreeSceneBridge_EnsureStaticSprite(
            self->bridge, (enum StaticSpriteSlot)self->slot, sprite_id);
        if( getenv("TORIRS_STATIC_SPRITE_DEBUG") )
            fprintf(
                stderr, "static_sprite: '%s' sprite=%d scene=%d\n", name, sprite_id, scene_id);
    }

    PT_END(&self->pt);
}

static void
Task_StaticSpritesLoad_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_StaticSpritesLoad_VTable = {
    .run = Task_StaticSpritesLoad_Run,
    .free = Task_StaticSpritesLoad_Free,
};

struct ToriRS_Task*
CreateTask_StaticSpritesLoad(
    struct CacheProvider* provider,
    struct UITreeSceneBridge* bridge)
{
    struct Task_StaticSpritesLoad* task;

    assert(provider);
    assert(bridge);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_StaticSpritesLoad_VTable;
    strncpy(task->task.name, "StaticSpritesLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->bridge = bridge;
    PT_INIT(&task->pt);
    return &task->task;
}
