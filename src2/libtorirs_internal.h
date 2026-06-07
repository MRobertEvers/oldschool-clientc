#ifndef LIBTORIRS_INTERNAL_H
#define LIBTORIRS_INTERNAL_H

#include "buildcache/dat1_buildcache.h"
#include "core/tasks/core_task.h"
#include "gamecache/gamecache.h"
#include "games/game_handle.h"
#include "games/model_viewer.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "scripting/libtorirs_scripting.h"
#include "world/world.h"
#include "world/world_scene_events.h"

#include <stdbool.h>
#include <stdint.h>

#define LIBTORIRS_MAX_TASKS 64

struct LibToriRS_Task
{
    struct CoreTask* task;
    int last_res;
    int next;
    int prev;
};

struct LibToriRS_Instance
{
    bool running;
    uint64_t last_frame_ms;
    uint64_t input_accumulator_ms;
    struct LibToriRS_IOQueue* io_queue;
    struct LibToriRS_ScriptQueue* script_queue;
    struct LibToriRS_Input* input;

    struct LibToriRS_Task tasks[LIBTORIRS_MAX_TASKS];
    int task_free_head;
    int task_live_head;

    struct GameCacheL* gamecache_l;

    struct GameModelViewer* model_viewer;
    struct GameHandle model_viewer_handle;
};

#endif