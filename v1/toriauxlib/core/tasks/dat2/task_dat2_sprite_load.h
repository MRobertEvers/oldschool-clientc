#ifndef TASK_DAT2_SPRITE_LOAD_H
#define TASK_DAT2_SPRITE_LOAD_H

#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "ioqueue/libtorirs_io.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"

struct LibToriRS_Task*
Task_Dat2SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item);

#endif
