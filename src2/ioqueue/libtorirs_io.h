#ifndef LIBTORIRS_IO_H
#define LIBTORIRS_IO_H

#include <stdbool.h>
#include <string.h>

#define LIBTORIRS_IOQUEUE_MAX_SIZE 128
#define LIBTORIRS_IOQUEUE_PATH_MAX 256

enum LibToriRS_IOStat
{
    TORIRSIO_STAT_YIELD = 0,
    TORIRSIO_STAT_DONE,
};

enum LibToriRS_IOKind
{
    TORIRSIO_KIND_CACHE = 0,
    TORIRSIO_KIND_CONFIG_FILE,
    TORIRSIO_KIND_SCRIPT,
};

struct LibToriRS_IOQueueItem_Cache
{
    int epoch;
    int table_id;
    int archive_id;
    int flags;
};

struct LibToriRS_IOQueueItem_ConfigFile
{
    char path[LIBTORIRS_IOQUEUE_PATH_MAX];
};

struct LibToriRS_IOQueueItem_Script
{
    char path[LIBTORIRS_IOQUEUE_PATH_MAX];
};

struct LibToriRS_IOQueueItem
{
    enum LibToriRS_IOKind kind;
    enum LibToriRS_IOStat status;

    int error_code;
    void* data;
    int data_size;
    union
    {
        struct LibToriRS_IOQueueItem_Cache cache;
        struct LibToriRS_IOQueueItem_ConfigFile config_file;
        struct LibToriRS_IOQueueItem_Script script;
    } u;
};

struct LibToriRS_IOQueue
{
    struct LibToriRS_IOQueueItem items[LIBTORIRS_IOQUEUE_MAX_SIZE];
    int count;
    int read_head;
};

struct LibToriRS_IOContext
{
    struct LibToriRS_IOQueue* io;
};

typedef struct
{
    unsigned short lc;
} pt_state_t;

typedef enum
{
    PT_YIELDED = 0,
    PT_FINISHED,
} PT_Status;

// clang-format off

// --- 1. State Declaration Macros ---
// Creates a struct named `[name]_state_t` with a hidden `_pt` field.
#define PT_STATE_BEGIN(name) typedef struct { pt_state_t _pt;
#define PT_STATE_CHILD(child_name, var_name) child_name##_state_t var_name;
#define PT_STATE_END(name) } name##_state_t;

// --- 2. Initialization Macro ---
// Zeroing the struct sets `lc = 0` for the parent AND all nested children automatically.
#define PT_STATE_INIT(state_ptr) memset((state_ptr), 0, sizeof(*(state_ptr)))

// --- 3. Thread Definition Macro ---
// Forces a standard signature and exposes `state` and `ctx` to the local scope.
#define PT_THREAD(name) PT_Status name(name##_state_t* state, struct LibToriRS_IOContext* ctx)

// Resolves to the raw type (useful for pointers, sizeof, or casts)
#define PT_STATE_T(name) name##_state_t

// Declares an uninitialized variable of the task's state type
#define PT_DECLARE(name, var_name) name##_state_t var_name

// Declares AND zero-initializes a root task state in one step
#define PT_SPAWN(name, var_name) \
    name##_state_t var_name; \
    PT_STATE_INIT(&(var_name))

#define PT_BEGIN() switch (state->_pt.lc) { case 0:

#define PT_YIELD() \
    do { \
        state->_pt.lc = __LINE__; \
        return PT_YIELDED; \
        case __LINE__:; \
    } while (0)

#define PT_AWAIT(child_func_call) \
    do { \
        state->_pt.lc = __LINE__; \
        case __LINE__: \
        if ((child_func_call) == PT_YIELDED) { \
            return PT_YIELDED; \
        } \
    } while (0)

#define PT_END() } state->_pt.lc = 0; return PT_FINISHED;
// clang-format on

#endif