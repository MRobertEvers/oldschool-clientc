#include "task_rs_inv_load.h"

#include "ui/uitree.h"

#include <stdlib.h>
#include <string.h>

struct Task_RSInvLoad*
Task_RSInvLoad_New(
    enum ToriAuxLibCacheMode cache_mode,
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    struct Task_RSInvLoad* task = calloc(1, sizeof(struct Task_RSInvLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache_mode = cache_mode;
    task->cache = cache;
    task->rc_ctx = rc_ctx;
    if( inv_item )
        task->inv_item = *inv_item;
    if( callbacks )
        task->callbacks = *callbacks;
    return task;
}

void
Task_RSInvLoad_Free(struct Task_RSInvLoad* task)
{
    free(task);
}

int
Task_RSInvLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx)
{
    struct Task_RSInvLoad* task = task_state;
    (void)ctx;

    PT_BEGIN(&task->thread);

    if( task->rc_ctx && task->rc_ctx->inv_pool && task->inv_item.name[0] != '\0' )
    {
        struct UIInventory inv;
        memset(&inv, 0, sizeof(inv));
        strncpy(inv.name, task->inv_item.name, sizeof(inv.name) - 1);
        inv.item_count = task->inv_item.item_count;
        if( inv.item_count > UI_INVENTORY_MAX_ITEMS )
            inv.item_count = UI_INVENTORY_MAX_ITEMS;

        for( int i = 0; i < inv.item_count; i++ )
        {
            inv.items[i].obj_id = atoi(task->inv_item.items[i]);
            inv.items[i].scene_id = -1;
            inv.items[i].atlas_index = 0;
        }

        int pool_index = uitree_inv_pool_append(task->rc_ctx->inv_pool, &inv);
        for( int i = 0; i < inv.item_count; i++ )
        {
            if( task->callbacks.on_slot )
                task->callbacks.on_slot(task->callbacks.user, pool_index, inv.items[i].obj_id);
        }
    }

    PT_END(&task->thread);
}
