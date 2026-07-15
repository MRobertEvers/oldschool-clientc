#ifndef TASK_DAT2_CS2_RUN_H
#define TASK_DAT2_CS2_RUN_H

#include "ioqueue/libtorirs_io.h"

struct GameRunescape;
struct CS2VMX;
struct GameRunescapeCS2Host;

/**
 * Cooperative CS2 script runner: runs CS2VMX_RunScript, and on YIELD awaits
 * the matching dat2 load task (clientscript / component / config / model).
 * Never reads disk itself — only enqueues IO via child tasks / IO_REQUEST.
 */
struct LibToriRS_Task*
Task_Dat2CS2Run_New(
    struct GameRunescape* game,
    struct CS2VMX* vm,
    struct GameRunescapeCS2Host* host,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* str_args,
    int str_arg_count);

#endif
