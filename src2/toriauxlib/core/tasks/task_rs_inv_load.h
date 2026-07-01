#ifndef TASK_RS_INV_LOAD_H
#define TASK_RS_INV_LOAD_H

#include "3rd/minipt.h"
#include "instance_revconfig_context.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "ui/uitree.h"

struct RSInvLoadCallbacks
{
    void* user;
    void (*on_slot)(void* user, int slot_index, int obj_id);
};

struct Task_RSInvLoad
{
    struct pt thread;
    enum ToriAuxLibCacheMode cache_mode;
    struct ToriAuxLibCache* cache;
    struct InstanceRevConfigContext* rc_ctx;
    struct RevConfigInvItem inv_item;
    struct RSInvLoadCallbacks callbacks;
    struct UIInventory inv;
    struct LibToriRS_IOBatch io_batch;
    int obj_ids[REVCONFIG_INV_MAX_ITEMS];
    int model_ids[REVCONFIG_INV_MAX_ITEMS];
    int model_count;
    int model_index;
    int slot_index;
    int pool_index;
};

struct Task_RSInvLoad*
Task_RSInvLoad_New(
    enum ToriAuxLibCacheMode cache_mode,
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks);

void
Task_RSInvLoad_Free(struct Task_RSInvLoad* task);

int
Task_RSInvLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx);

#endif
