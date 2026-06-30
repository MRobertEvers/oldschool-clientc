#ifndef TASK_TORIAUXLIBTD_H
#define TASK_TORIAUXLIBTD_H

#include "../../../libtorirs.h"
#include "toriauxlib/td/toriauxlibtd.h"

struct Task_ToriAuxLibTD_ModelLoad;

struct Task_ToriAuxLibTD_ModelLoad*
Task_ToriAuxLibTD_ModelLoad_New(
    struct ToriAuxLibTD* tdx,
    int model_id);

void
Task_ToriAuxLibTD_ModelLoad_Free(struct Task_ToriAuxLibTD_ModelLoad* task);

int
Task_ToriAuxLibTD_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibTD_TexturesLoad;

struct Task_ToriAuxLibTD_TexturesLoad*
Task_ToriAuxLibTD_TexturesLoad_New(struct ToriAuxLibTD* tdx);

void
Task_ToriAuxLibTD_TexturesLoad_Free(struct Task_ToriAuxLibTD_TexturesLoad* task);

int
Task_ToriAuxLibTD_TexturesLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_ToriAuxLibTD_AnimationsLoad;

struct Task_ToriAuxLibTD_AnimationsLoad*
Task_ToriAuxLibTD_AnimationsLoad_New(struct ToriAuxLibTD* tdx);

void
Task_ToriAuxLibTD_AnimationsLoad_Free(struct Task_ToriAuxLibTD_AnimationsLoad* task);

int
Task_ToriAuxLibTD_AnimationsLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
