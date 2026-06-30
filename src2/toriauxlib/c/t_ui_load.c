#include "t_ui_load.h"

#include "toriauxlib/c/toriauxlibcache.h"

void
Task_ToriAuxLibCache_UILoad_SetResized(struct Task_ToriAuxLibCache_UILoad* task, bool is_resized)
{
    if( task && task->task_dat2 )
        Task_Dat2UILoad_SetResized(task->task_dat2, is_resized);
}

void
Task_ToriAuxLibCache_UILoad_SetLayoutGroup(
    struct Task_ToriAuxLibCache_UILoad* task,
    char const* layout_group)
{
    if( task && task->task_dat2 )
        Task_Dat2UILoad_SetLayoutGroup(task->task_dat2, layout_group);
}

struct Task_ToriAuxLibCache_UILoad*
Task_ToriAuxLibCache_UILoad_New(
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game)
{
    struct Task_ToriAuxLibCache_UILoad* task = calloc(1, sizeof(struct Task_ToriAuxLibCache_UILoad));
    if( !task )
        return NULL;
    task->c = c;
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT1 )
        task->task_dat1 = Task_Dat1UILoad_New(c, scene, tree, game);
    else if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT2 )
        task->task_dat2 = Task_Dat2UILoad_New(c, scene, tree, game);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibCache_UILoad_Free(struct Task_ToriAuxLibCache_UILoad* task)
{
    if( !task )
        return;
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT1 && task->task_dat1 )
        Task_Dat1UILoad_Free(task->task_dat1);
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT2 && task->task_dat2 )
        Task_Dat2UILoad_Free(task->task_dat2);
    free(task);
}

bool
Task_ToriAuxLibCache_UILoad_IsReady(struct Task_ToriAuxLibCache_UILoad* task)
{
    if( !task )
        return false;
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1UILoad_IsReady(task->task_dat1);
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2UILoad_IsReady(task->task_dat2);
    return false;
}

int
Task_ToriAuxLibCache_UILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibCache_UILoad* task = (struct Task_ToriAuxLibCache_UILoad*)task_state;
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1UILoad_Run(task->task_dat1, ctx);
    if( ToriAuxLibCache_Mode(task->c) == TORIAUXLIBCACHE_MODE_DAT2 )
        return Task_Dat2UILoad_Run(task->task_dat2, ctx);
    return PT_ENDED;
}
