#ifndef TASK_DAT2_INV_LOAD_H
#define TASK_DAT2_INV_LOAD_H

#include "toriauxlib/core/tasks/component_load_types.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "ioqueue/libtorirs_io.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"

struct LibToriRS_Task*
Task_Dat2InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks);

#endif
