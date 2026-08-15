#include "engine/player_appearance.h"

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
PlayerAppearance_ResolveDefaultMale(
    struct CacheProvider* provider,
    struct PlayerAppearance* out)
{
    int found = 0;
    assert(provider && out);

    out->gender = 0;
    for( int i = 0; i < PLAYER_APPEARANCE_PARTS; i++ )
        out->kits[i] = -1;
    for( int i = 0; i < PLAYER_APPEARANCE_COLORS; i++ )
        out->colors[i] = 0;

    for( int id = 0; id < PLAYER_IDK_SCAN_MAX && found < PLAYER_APPEARANCE_PARTS; id++ )
    {
        struct ToriRS_Idk* idk;
        int part;
        if( !CacheProvider_IdkHas(provider, id) )
            continue;
        idk = CacheProvider_IdkGet(provider, id);
        if( !idk || idk->not_selectable )
            continue;
        part = idk->body_part_id;
        if( part >= 0 && part < PLAYER_APPEARANCE_PARTS && out->kits[part] < 0 )
        {
            out->kits[part] = id;
            found++;
        }
    }
    return found;
}

/* --- async prefetch of the default appearance's idk configs + models --- */

struct Task_PlayerAppearanceLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct CacheProvider* provider;
    int scan_i;
    int found;
    int kits[PLAYER_APPEARANCE_PARTS];
    int model_part;
    int model_i;
};

static void
player_try_record_kit(struct Task_PlayerAppearanceLoad* self, int id)
{
    struct ToriRS_Idk* idk;
    int part;
    if( !CacheProvider_IdkHas(self->provider, id) )
        return;
    idk = CacheProvider_IdkGet(self->provider, id);
    if( !idk || idk->not_selectable )
        return;
    part = idk->body_part_id;
    if( part >= 0 && part < PLAYER_APPEARANCE_PARTS && self->kits[part] < 0 )
    {
        self->kits[part] = id;
        self->found++;
    }
}

/* Sync helpers so the load loops keep no idk pointer live across a yield. */
static int
player_kit_model_count(struct CacheProvider* provider, int kit_id)
{
    struct ToriRS_Idk* idk;
    if( kit_id < 0 )
        return 0;
    idk = CacheProvider_IdkGet(provider, kit_id);
    return idk ? idk->model_ids_count : 0;
}

static int
player_kit_model_id(struct CacheProvider* provider, int kit_id, int i)
{
    struct ToriRS_Idk* idk;
    if( kit_id < 0 )
        return -1;
    idk = CacheProvider_IdkGet(provider, kit_id);
    if( !idk || i < 0 || i >= idk->model_ids_count )
        return -1;
    return idk->model_ids[i];
}

static int
Task_PlayerAppearanceLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_PlayerAppearanceLoad* self = (struct Task_PlayerAppearanceLoad*)base;
    assert(self->provider);

    PT_BEGIN(&self->pt);

    /* Load every identity kit and record the first selectable one per body
     * part. The whole table is needed, not just the seven defaults: the design
     * screen's arrows step kit ids one at a time over the full IdkType table
     * (reference clientButton CC_CHANGE_*), so a kit that is not loaded is a
     * kit the player cannot cycle onto.
     *
     * Kit ids are contiguous from 0, so the first id that fails to load is the
     * end of the table — that miss is the terminator, and costs one "Failed to
     * load idk" line. */
    for( self->scan_i = 0; self->scan_i < PLAYER_IDK_SCAN_MAX; self->scan_i++ )
    {
        if( !CacheProvider_IdkHas(self->provider, self->scan_i) )
            PT_TASK_AWAITSELF_IF(CreateTask_IdkLoad(self->provider, self->scan_i));
        if( !CacheProvider_IdkHas(self->provider, self->scan_i) )
            break;
        player_try_record_kit(self, self->scan_i);
    }

    if( getenv("TORIRS_ANIM_DEBUG") )
        fprintf(
            stderr,
            "PlayerAppearanceLoad: scanned=%d found=%d kits=[%d,%d,%d,%d,%d,%d,%d]\n",
            self->scan_i,
            self->found,
            self->kits[0],
            self->kits[1],
            self->kits[2],
            self->kits[3],
            self->kits[4],
            self->kits[5],
            self->kits[6]);

    /* Load every model referenced by the chosen kits. */
    for( self->model_part = 0; self->model_part < PLAYER_APPEARANCE_PARTS; self->model_part++ )
    {
        for( self->model_i = 0;
             self->model_i < player_kit_model_count(self->provider, self->kits[self->model_part]);
             self->model_i++ )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(
                self->provider,
                player_kit_model_id(self->provider, self->kits[self->model_part], self->model_i)));
        }
    }

    PT_END(&self->pt);
}

static void
Task_PlayerAppearanceLoad_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_PlayerAppearanceLoad_VTable = {
    .run = Task_PlayerAppearanceLoad_Run,
    .free = Task_PlayerAppearanceLoad_Free,
};

struct ToriRS_Task*
CreateTask_PlayerAppearanceLoad(struct CacheProvider* provider)
{
    struct Task_PlayerAppearanceLoad* task;
    assert(provider);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PlayerAppearanceLoad_VTable;
    strncpy(task->task.name, "PlayerAppearanceLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    for( int i = 0; i < PLAYER_APPEARANCE_PARTS; i++ )
        task->kits[i] = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
