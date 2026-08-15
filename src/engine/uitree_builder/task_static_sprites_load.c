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

/* Resolve the slot's archive to a provider sprite id, or -1. Named RevConfig
 * loads (dat1 chrome INIs) already registered most of these. */
static int
static_sprite_lookup(
    struct CacheProvider* provider,
    struct StaticSpriteDef const* def)
{
    int sprite_id = CacheProvider_SpriteIdByName(provider, def->name);
    if( sprite_id >= 0 && CacheProvider_SpriteHas(provider, sprite_id) )
        return sprite_id;
    if( def->dat2_name )
    {
        sprite_id = CacheProvider_SpriteIdByName(provider, def->dat2_name);
        if( sprite_id >= 0 && CacheProvider_SpriteHas(provider, sprite_id) )
            return sprite_id;
    }
    return -1;
}

static int
Task_StaticSpritesLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_StaticSpritesLoad* self = (struct Task_StaticSpritesLoad*)base;

    assert(self->provider);
    assert(self->bridge);

    PT_BEGIN(&self->pt);

    /* Locals never survive a yield: everything is re-derived from self->slot. */
    for( self->slot = 0; self->slot < STATIC_SPRITE_COUNT; self->slot++ )
    {
        if( UITreeSceneBridge_StaticSpriteSceneId(
                self->bridge, (enum StaticSpriteSlot)self->slot) > 0 )
            continue;

        /* Dat2: resolve the sprites-table archive by name. No-op on dat1
         * (vtable slot unset -> creator returns NULL). */
        if( static_sprite_lookup(
                self->provider, StaticSprite_Def((enum StaticSpriteSlot)self->slot)) < 0 &&
            StaticSprite_Def((enum StaticSpriteSlot)self->slot)->dat2_name )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_SpriteLoadByName(
                self->provider,
                StaticSprite_Def((enum StaticSpriteSlot)self->slot)->dat2_name));
        }

        /* Dat1: decode the named media-jagfile sprite. No-op on dat2. */
        if( static_sprite_lookup(
                self->provider, StaticSprite_Def((enum StaticSpriteSlot)self->slot)) < 0 &&
            StaticSprite_Def((enum StaticSpriteSlot)self->slot)->dat1_name )
        {
            struct StaticSpriteDef const* def =
                StaticSprite_Def((enum StaticSpriteSlot)self->slot);
            char data_filename[80];
            struct CacheProviderSpriteSource src;

            snprintf(data_filename, sizeof(data_filename), "%s.dat", def->dat1_name);
            memset(&src, 0, sizeof(src));
            src.name = def->name;
            src.format = def->dat1_format;
            src.data_filename = data_filename;
            src.index_filename = "index.dat";
            src.atlas_index = 0;
            src.atlas_count = def->dat1_atlas_count;
            PT_TASK_AWAITSELF_IF(CreateTask_SpriteLoadFromSource(self->provider, &src));
        }

        {
            struct StaticSpriteDef const* def =
                StaticSprite_Def((enum StaticSpriteSlot)self->slot);
            int sprite_id = static_sprite_lookup(self->provider, def);
            int scene_id;
            if( sprite_id < 0 )
            {
                /* Era-absent or missing archive: leave the slot unbound. */
                if( getenv("TORIRS_STATIC_SPRITE_DEBUG") )
                    fprintf(stderr, "static_sprite: '%s' unresolved\n", def->name);
                continue;
            }
            scene_id = UITreeSceneBridge_EnsureStaticSprite(
                self->bridge, (enum StaticSpriteSlot)self->slot, sprite_id);
            if( getenv("TORIRS_STATIC_SPRITE_DEBUG") )
                fprintf(
                    stderr, "static_sprite: '%s' sprite=%d scene=%d\n", def->name, sprite_id,
                    scene_id);
        }
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
    if( !bridge )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_StaticSpritesLoad_VTable;
    strncpy(task->task.name, "StaticSpritesLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->bridge = bridge;
    PT_INIT(&task->pt);
    return &task->task;
}
