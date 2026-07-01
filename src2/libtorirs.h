#ifndef LIBTORIRS_H
#define LIBTORIRS_H

#include "toriauxlib/core/tasks/core_task.h"
#include "input/libtorirs_input.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw_scene.h"

#include <stdbool.h>
#include <stdint.h>

struct LibToriRS_ScriptQueue;
struct LibToriRS_Instance;
struct LibToriRS_CommandQueue;
struct ToriAuxLib;

struct LibToriRS_Instance*
LibToriRS_InstanceNew(void);

struct LibToriRS_Instance*
LibToriRS_InstanceNewWithCacheMode(int cache_mode);

void
LibToriRS_InstanceSetClientKronos(
    struct LibToriRS_Instance* instance,
    bool kronos);

bool
LibToriRS_InstanceIsKronos(struct LibToriRS_Instance* instance);

void
LibToriRS_InstanceFree(struct LibToriRS_Instance* instance);

void
LibToriRS_InitTime(
    struct LibToriRS_Instance* instance,
    uint64_t time);

void
LibToriRS_InitGame_ModelViewer(struct LibToriRS_Instance* instance);

struct LibToriRS_ScriptQueue*
LibToriRS_GetScriptQueue(struct LibToriRS_Instance* instance);

struct LibToriRS_IOQueue*
LibToriRS_GetIOQueue(struct LibToriRS_Instance* instance);

void
LibToriRS_ProcessCommandQueue(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    uint64_t time);

void
LibToriRS_ProcessInput(struct LibToriRS_Instance* instance);

void
LibToriRS_TickInput(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    uint64_t time_ms);

void
LibToriRS_SetCpuAnimation(
    struct LibToriRS_Instance* instance,
    bool enabled);

void
LibToriRS_FrameBegin(struct LibToriRS_Instance* instance);

uint64_t
LibToriRS_GetAnimationClock(struct LibToriRS_Instance* instance);

bool
LibToriRS_FrameNextCommand(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command);

void
LibToriRS_FrameEnd(struct LibToriRS_Instance* instance);

bool
LibToriRS_IsRunning(struct LibToriRS_Instance* instance);

struct ToriDraw_Scene*
LibToriRS_GetCurrentToriDrawScene(struct LibToriRS_Instance* instance);

struct ToriAuxLib*
LibToriRS_GetToriAuxLib(struct LibToriRS_Instance* instance);

/* Caller must pass a destroy callback that frees task_state (may be NULL). */
void
LibToriRS_TasksAdd(
    struct LibToriRS_Instance* instance,
    void* task_state,
    CoreTaskFunction task_function,
    CoreTaskDestructor destroy);

bool
LibToriRS_TasksRun(struct LibToriRS_Instance* instance);

bool
LibToriRS_TasksHasLive(struct LibToriRS_Instance* instance);

#endif
