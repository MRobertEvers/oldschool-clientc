#ifndef TASK_UI_DAT2_LOAD_H
#define TASK_UI_DAT2_LOAD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "core/tapi/tapi_config.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "ui/uitree.h"
#include "toriauxlib/c/revconfig_ui_build.h"
#include "core/tapi/tapi_dat2.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_scene.h"

#include <stdbool.h>

struct GameRunescape;

#define UI_DAT2_CACHE_INI "rev_245_2/rev_kronos_ui_cache.ini"
#define UI_DAT2_UI_INI "rev_245_2/rev_kronos_ui.ini"
#define UI_DAT2_LAYOUT_GROUP_FIXED "fixed"
#define UI_DAT2_LAYOUT_GROUP_RESIZABLE_BOX "resizable_box"
#define UI_DAT2_LAYOUT_GROUP_RESIZABLE_BOTTOM "resizable_bottom"
#define UI_VARBIT_SIDE_PANELS 4607
#define UI_DAT2_FONT_COUNT 4
#define UI_DAT2_MAX_WORK_RETRIES 64

enum UIDat2WorkKind
{
    UI_DAT2WORK_FONT = 0,
    UI_DAT2WORK_SPRITE,
};

struct UIDat2WorkItem
{
    enum UIDat2WorkKind kind;
    int ref_index;
    int font_id;
    int numeric_id;
    int retry_count;
};

struct Task_Dat2UILoad
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
    struct UIDat2WorkItem work_items[REVCONFIG_UI_MAX_WORK_ITEMS];
    int work_head;
    int work_count;
    char layout_group[32];
    bool is_resized;
    bool fonts_skipped;
    bool ui_ready;
};

struct Task_Dat2UILoad*
Task_Dat2UILoad_New(
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game);

void
Task_Dat2UILoad_Free(struct Task_Dat2UILoad* task);

void
Task_Dat2UILoad_SetResized(struct Task_Dat2UILoad* task, bool is_resized);

void
Task_Dat2UILoad_SetLayoutGroup(struct Task_Dat2UILoad* task, char const* layout_group);

bool
Task_Dat2UILoad_IsReady(struct Task_Dat2UILoad* task);

int
Task_Dat2UILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
