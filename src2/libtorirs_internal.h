#ifndef LIBTORIRS_INTERNAL_H
#define LIBTORIRS_INTERNAL_H

#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache.h"
#include "games/model_viewer.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "scripting/libtorirs_scripting.h"

#include <stdbool.h>
#include <stdint.h>

struct LibToriRS_Instance
{
    bool running;
    uint64_t last_frame_ms;
    uint64_t input_accumulator_ms;
    struct LibToriRS_IOQueue* io_queue;
    struct LibToriRS_ScriptQueue* script_queue;
    struct LibToriRS_Input* input;
    struct LibToriRS_RenderQueue* render_queue;

    struct Dat1BuildCache* dat1_buildcache;
    struct GameCache* gamecache;

    struct GameModelViewer* model_viewer;
};

#endif