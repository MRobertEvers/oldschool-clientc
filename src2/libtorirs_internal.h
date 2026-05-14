#ifndef LIBTORIRS_INTERNAL_H
#define LIBTORIRS_INTERNAL_H

#include "scripting/libtorirs_scripting.h"

struct LibToriRS_Instance
{
    struct LibToriRS_ScriptQueue* script_queue;
};

#endif