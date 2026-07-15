#ifndef TASK_DAT2_FONT_LOAD_H
#define TASK_DAT2_FONT_LOAD_H

#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "ioqueue/libtorirs_io.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"

struct LibToriRS_Task*
Task_Dat2FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item);

#endif
