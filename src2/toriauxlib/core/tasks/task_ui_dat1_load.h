#ifndef TASK_UI_DAT1_LOAD_H
#define TASK_UI_DAT1_LOAD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "core/tapi/tapi_config.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/c/revconfig_ui_build.h"
#include "toriauxlib/c/revconfig_ui_expand.h"
#include "games/runescape.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_DAT1_CACHE_INI "rev_245_2/rev_245_2_dat1_cache.ini"
#define UI_DAT1_UI_INI "rev_245_2/rev_245_2_dat1_ui.ini"
#define UI_DAT1_FONT_COUNT 4
#define UI_DAT1_INV_MODEL_MAX 32

enum UIDat1WorkKind
{
    UI_DAT1WORK_FONT = 0,
    UI_DAT1WORK_SPRITE,
};

struct UIDat1WorkItem
{
    enum UIDat1WorkKind kind;
    int ref_index;
    int font_id;
    char font_name[16];
};

struct Task_Dat1UILoad
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    struct ToriDraw_Scene* scene;
    struct RevConfigBuffer* cache_fields;
    struct RevConfigBuffer* ui_fields;
    struct RevConfigItemBuffer* items;
    struct RevConfigUIBuildState build;
    struct UITree* tree;
    struct GameRunescape* game;
    struct UIInventoryPool* inv_pool;
    int inv_model_ids[UI_DAT1_INV_MODEL_MAX];
    int inv_model_count;
    int inv_model_index;
    struct LibToriRS_IOBatch inv_model_batch;
    struct UIDat1WorkItem work_items[REVCONFIG_UI_MAX_WORK_ITEMS];
    int work_head;
    int work_count;
    bool ui_ready;
};

struct Task_Dat1UILoad*
Task_Dat1UILoad_New(
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game);

void
Task_Dat1UILoad_Free(struct Task_Dat1UILoad* task);

bool
Task_Dat1UILoad_IsReady(struct Task_Dat1UILoad* task);

int
Task_Dat1UILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
