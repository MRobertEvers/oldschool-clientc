#ifndef TASK_INSTANCE_REVCONFIG_LOAD_H
#define TASK_INSTANCE_REVCONFIG_LOAD_H

#include "libtori_core_task.h"
#include "3rd/minipt.h"
#include "instance_revconfig_context.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "revconfig/revconfig.h"
#include "task_instance_on_rc.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "ui/uitree.h"

#include <stdbool.h>

struct ToriDraw_Scene;
struct GameRunescape;

#define INSTANCE_RC_MAX_CONFIG_FILES 8

struct Task_InstanceRevConfigLoad
{
    struct pt thread;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    struct UITree* tree;
    struct GameRunescape* game;
    char const* config_files[INSTANCE_RC_MAX_CONFIG_FILES];
    int config_file_count;
    char const* layout_group;

    struct RevConfigItemBuffer* items;
    int item_index;
    struct InstanceRevConfigContext rc_ctx;

    union
    {
        struct Task_InstanceOnRCCacheSprite on_cache_sprite;
        struct Task_InstanceOnRCCacheFont on_cache_font;
        struct Task_InstanceOnRCUIComponent on_uicomponent;
        struct Task_InstanceOnRCUILayout on_uilayout;
        struct Task_InstanceOnRCInv on_inv;
    } handler;

    LibToriCoreTaskFunction sub_run;
    void* sub_state;

    int config_fetch_index;
    bool ui_ready;
};

struct Task_InstanceRevConfigLoad*
Task_InstanceRevConfigLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game,
    char const* const* config_files,
    int config_file_count,
    char const* layout_group);

void
Task_InstanceRevConfigLoad_Free(struct Task_InstanceRevConfigLoad* task);

bool
Task_InstanceRevConfigLoad_IsReady(struct Task_InstanceRevConfigLoad* task);

int
Task_InstanceRevConfigLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx);

#endif
