#ifndef TORIDRAWX_TASKS_H
#define TORIDRAWX_TASKS_H

#include "../libtorirs.h"
#include "toridrawx.h"

struct Task_ToriDrawX_ModelLoad;

struct Task_ToriDrawX_ModelLoad*
Task_ToriDrawX_ModelLoad_New(
    struct ToriDrawX* tdx,
    int model_id);

void
Task_ToriDrawX_ModelLoad_Free(struct Task_ToriDrawX_ModelLoad* task);

int
Task_ToriDrawX_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriDrawX_TexturesLoad;

struct Task_ToriDrawX_TexturesLoad*
Task_ToriDrawX_TexturesLoad_New(struct ToriDrawX* tdx);

void
Task_ToriDrawX_TexturesLoad_Free(struct Task_ToriDrawX_TexturesLoad* task);

int
Task_ToriDrawX_TexturesLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriDrawX_AnimationsLoad;

struct Task_ToriDrawX_AnimationsLoad*
Task_ToriDrawX_AnimationsLoad_New(struct ToriDrawX* tdx);

void
Task_ToriDrawX_AnimationsLoad_Free(struct Task_ToriDrawX_AnimationsLoad* task);

int
Task_ToriDrawX_AnimationsLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
