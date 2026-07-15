#ifndef TASK_DAT2_COMPONENT_LOAD_H
#define TASK_DAT2_COMPONENT_LOAD_H

#include "toriauxlib/core/tasks/component_load_types.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "ioqueue/libtorirs_io.h"
#include "toriauxlib/cache/toriauxlibcache.h"

struct ToriDraw_Scene;

struct LibToriRS_Task*
Task_Dat2ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks);

#endif
