#include "engine/player_appearance.h"

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * OSRS PlayerComposition body-color recolors (hair, torso, legs, feet, skin).
 * For the default appearance every colors[] index is 0, and palette entry 0 is
 * the identity of the source color, so only the one non-identity secondary
 * recolor below has any visible effect. Kept explicit for future full support.
 */
static const int PLAYER_BODY_RECOLOR_FROM_2[PLAYER_APPEARANCE_COLORS] = {
    -10304, 9104, -1, -1, -1
};
static const int PLAYER_BODY_RECOLOR_TO_2_INDEX0[PLAYER_APPEARANCE_COLORS] = {
    6554, 9104, 0, 0, 0
};

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

    /* Discover the first selectable kit for each body part, loading idk configs
     * on demand and stopping as soon as all parts are found. */
    for( self->scan_i = 0;
         self->scan_i < PLAYER_IDK_SCAN_MAX && self->found < PLAYER_APPEARANCE_PARTS;
         self->scan_i++ )
    {
        if( !CacheProvider_IdkHas(self->provider, self->scan_i) )
            TASK_AWAITSELF_IF(CreateTask_IdkLoad(self->provider, self->scan_i));
        player_try_record_kit(self, self->scan_i);
    }

    /* Load every model referenced by the chosen kits. */
    for( self->model_part = 0; self->model_part < PLAYER_APPEARANCE_PARTS; self->model_part++ )
    {
        for( self->model_i = 0;
             self->model_i < player_kit_model_count(self->provider, self->kits[self->model_part]);
             self->model_i++ )
        {
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(
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

/* Expose the default secondary body recolor pairs for the compositor. */
int
PlayerAppearance_DefaultBodyRecolorCount(void)
{
    return PLAYER_APPEARANCE_COLORS;
}

int
PlayerAppearance_DefaultBodyRecolorFrom(int c)
{
    if( c < 0 || c >= PLAYER_APPEARANCE_COLORS )
        return -1;
    return PLAYER_BODY_RECOLOR_FROM_2[c];
}

int
PlayerAppearance_DefaultBodyRecolorTo(int c)
{
    if( c < 0 || c >= PLAYER_APPEARANCE_COLORS )
        return 0;
    return PLAYER_BODY_RECOLOR_TO_2_INDEX0[c];
}
