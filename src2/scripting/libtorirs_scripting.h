#ifndef LIBTORIRS_SCRIPTING_H
#define LIBTORIRS_SCRIPTING_H

#include <stdbool.h>

#define LIBTORIRS_SCRIPT_QUEUE_MAX_SIZE 128
#define LIBTORIRS_SCRIPT_NAME_MAX_SIZE 128
#define LIBTORIRS_SCRIPT_MAX_ARGS 8

struct LibToriRS_ScriptValue_Any
{
    void* value;
};

struct LibToriRS_ScriptValue_Int
{
    int value;
};

enum LibToriRS_ScriptValue_Kind
{
    LIBTORIRS_SCRIPT_VALUE_VOID = 0,
    LIBTORIRS_SCRIPT_VALUE_INT,
    LIBTORIRS_SCRIPT_VALUE_ANY,
};

struct LibToriRS_ScriptValue
{
    int kind;
    int magic;
    union
    {
        struct LibToriRS_ScriptValue_Int intval;
        struct LibToriRS_ScriptValue_Any anyval;
    } u;
};

struct LibToriRS_ScriptArgs
{
    struct LibToriRS_ScriptValue values[LIBTORIRS_SCRIPT_MAX_ARGS];
    int count;
};

struct LibToriRS_Script
{
    bool is_inline;
    char name[LIBTORIRS_SCRIPT_NAME_MAX_SIZE];
    struct LibToriRS_ScriptArgs args;
};

struct LibToriRS_ScriptQueue
{
    struct LibToriRS_ScriptValue values[LIBTORIRS_SCRIPT_QUEUE_MAX_SIZE];
    int head;
    int tail;
    int count;
};

struct LibToriRS_ScriptQueue*
LibToriRS_ScriptQueueNew(void);

void
LibToriRS_ScriptQueueFree(struct LibToriRS_ScriptQueue* queue);

struct LibToriRS_Script*
LibToriRS_ScriptQueueEmplace(
    struct LibToriRS_ScriptQueue* queue,
    const char* name);

struct LibToriRS_Script*
LibToriRS_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue);

bool
LibToriRS_ScriptQueueIsEmpty(struct LibToriRS_ScriptQueue* queue);

struct LibToriRS_ScriptArgs*
LibToriRS_ScriptGetArgs(struct LibToriRS_Script* script);

void
LibToriRS_ScriptArgsReset(struct LibToriRS_ScriptArgs* args);

#endif