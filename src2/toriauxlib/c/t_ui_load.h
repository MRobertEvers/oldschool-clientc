#ifndef T_UI_LOAD_H
#define T_UI_LOAD_H

#include "t_ui_dat1_load.h"
#include "t_ui_dat2_load.h"
#include "games/runescape.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "ui/uitree.h"
#include "toridraw/toridraw_scene.h"

#include <stdbool.h>

struct Task_ToriAuxLibCache_UILoad
{
    struct ToriAuxLibCache* c;
    struct Task_Dat1UILoad* task_dat1;
    struct Task_Dat2UILoad* task_dat2;
};

struct Task_ToriAuxLibCache_UILoad*
Task_ToriAuxLibCache_UILoad_New(
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game);

void
Task_ToriAuxLibCache_UILoad_Free(struct Task_ToriAuxLibCache_UILoad* task);

void
Task_ToriAuxLibCache_UILoad_SetResized(struct Task_ToriAuxLibCache_UILoad* task, bool is_resized);

void
Task_ToriAuxLibCache_UILoad_SetLayoutGroup(
    struct Task_ToriAuxLibCache_UILoad* task,
    char const* layout_group);

bool
Task_ToriAuxLibCache_UILoad_IsReady(struct Task_ToriAuxLibCache_UILoad* task);

int
Task_ToriAuxLibCache_UILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
