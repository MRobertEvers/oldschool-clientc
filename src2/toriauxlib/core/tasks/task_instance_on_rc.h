#ifndef TASK_INSTANCE_ON_RC_H
#define TASK_INSTANCE_ON_RC_H

#include "instance_revconfig_context.h"
#include "ioqueue/libtorirs_io.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"

struct Task_InstanceOnRCUIComponent
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigUIComponentItem item;
};

struct Task_InstanceOnRCUILayout
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct RevConfigUILayoutItem item;
};

struct Task_InstanceOnRCInv
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigInvItem item;
};

struct LibToriRS_Task*
Task_InstanceOnRCUIComponent_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigUIComponentItem const* item);

struct LibToriRS_Task*
Task_InstanceOnRCUILayout_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigUILayoutItem const* item);

struct LibToriRS_Task*
Task_InstanceOnRCInv_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigInvItem const* item);

#endif
