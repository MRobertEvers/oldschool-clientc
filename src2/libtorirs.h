#ifndef LIBTORIRS_H
#define LIBTORIRS_H

#include "input/libtorirs_input.h"
#include "render/libtorirs_render.h"
#include "world/world_scene_events.h"

#include <stdbool.h>
#include <stdint.h>

struct LibToriRS_ScriptQueue;
struct LibToriRS_Instance;
struct LibToriRS_CommandQueue;

struct LibToriRS_Instance*
LibToriRS_InstanceNew(void);

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

struct LibToriRS_RenderQueue*
LibToriRS_GetRenderQueue(struct LibToriRS_Instance* instance);

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
LibToriRS_GameStep(struct LibToriRS_Instance* instance);

bool
LibToriRS_IsRunning(struct LibToriRS_Instance* instance);

#endif