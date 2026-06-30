#ifndef REVCONFIG_UI_LOAD_H
#define REVCONFIG_UI_LOAD_H

/* Legacy include: forwards to Task_ToriAuxLibCache_UILoad facade. */
#include "toriauxlib/core/tasks/task_ui_load.h"

typedef struct Task_ToriAuxLibCache_UILoad Task_RevConfigUILoad;

#define Task_RevConfigUILoad_New Task_ToriAuxLibCache_UILoad_New
#define Task_RevConfigUILoad_Free Task_ToriAuxLibCache_UILoad_Free
#define Task_RevConfigUILoad_IsReady Task_ToriAuxLibCache_UILoad_IsReady
#define Task_RevConfigUILoad_Run Task_ToriAuxLibCache_UILoad_Run

#endif
