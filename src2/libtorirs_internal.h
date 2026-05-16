#ifndef LIBTORIRS_INTERNAL_H
#define LIBTORIRS_INTERNAL_H

#include "ioqueue/libtorirs_ioqueue.h"
#include "scripting/libtorirs_scripting.h"

#include <stdbool.h>

struct LibToriRS_Instance
{
    bool running;
    struct LibToriRS_IOQueue* io_queue;
    struct LibToriRS_ScriptQueue* script_queue;
    struct LibToriRS_Input* input;
};

#endif