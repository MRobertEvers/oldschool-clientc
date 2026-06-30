#ifndef TASK_INSTANCE_ON_RC_H
#define TASK_INSTANCE_ON_RC_H

#include "instance_revconfig_context.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "revconfig/revconfig.h"

struct Task_InstanceOnRCCacheSprite
{
    struct pt thread;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigCacheItem item;
    int element_id;
};

struct Task_InstanceOnRCUIComponent
{
    struct pt thread;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigUIComponentItem item;
    struct Task_RSComponentLoad* rs_load;
};

struct Task_InstanceOnRCUILayout
{
    struct pt thread;
    struct InstanceRevConfigContext* rc_ctx;
    struct RevConfigUILayoutItem item;
};

struct Task_InstanceOnRCInv
{
    struct pt thread;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigInvItem item;
    struct Task_RSInvLoad* inv_load;
};

void
Task_InstanceOnRCCacheSprite_Init(
    struct Task_InstanceOnRCCacheSprite* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item);

int
Task_InstanceOnRCCacheSprite_Run(void* task_state, struct LibToriRS_IOContext* ctx);

void
Task_InstanceOnRCUIComponent_Init(
    struct Task_InstanceOnRCUIComponent* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigUIComponentItem const* item);

int
Task_InstanceOnRCUIComponent_Run(void* task_state, struct LibToriRS_IOContext* ctx);

void
Task_InstanceOnRCUILayout_Init(
    struct Task_InstanceOnRCUILayout* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigUILayoutItem const* item);

int
Task_InstanceOnRCUILayout_Run(void* task_state, struct LibToriRS_IOContext* ctx);

void
Task_InstanceOnRCInv_Init(
    struct Task_InstanceOnRCInv* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigInvItem const* item);

int
Task_InstanceOnRCInv_Run(void* task_state, struct LibToriRS_IOContext* ctx);

#endif
