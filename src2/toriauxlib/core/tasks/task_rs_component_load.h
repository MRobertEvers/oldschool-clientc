#ifndef TASK_RS_COMPONENT_LOAD_H
#define TASK_RS_COMPONENT_LOAD_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "toriauxlib/c/toriauxlibcache.h"

#include <stdbool.h>

struct InstanceRevConfigContext;
struct ToriDraw_Scene;
struct LibToriRS_IOContext;

#define RS_COMPONENT_STACK_MAX 1024
#define TASK_RS_COMPONENT_PREFETCH_BATCH 64

struct RSComponentLoadCallbacks
{
    void* user;
    void (*on_component)(
        void* user,
        int component_id);
};

struct Task_RSComponentLoad
{
    struct pt thread;
    enum ToriAuxLibCacheMode cache_mode;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    struct InstanceRevConfigContext* rc_ctx;
    int root_component_id;
    char owner_component[64];
    struct RSComponentLoadCallbacks callbacks;

    int components_walked;
    int stack[RS_COMPONENT_STACK_MAX];
    int stack_x[RS_COMPONENT_STACK_MAX];
    int stack_y[RS_COMPONENT_STACK_MAX];
    int stack_parent_id[RS_COMPONENT_STACK_MAX];
    int stack_count;

    struct LibToriRS_IOBatch io_batch;
    int* needed_sprite_ids;
    int needed_sprite_count;
    int prefetch_chunk_index;
};

struct Task_RSComponentLoad*
Task_RSComponentLoad_New(
    enum ToriAuxLibCacheMode cache_mode,
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks);

void
Task_RSComponentLoad_Free(struct Task_RSComponentLoad* task);

int
Task_RSComponentLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
