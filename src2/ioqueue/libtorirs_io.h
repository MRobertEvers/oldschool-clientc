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
    int run_id;

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
    int run_counter;
};

struct LibToriRS_IOContext
{
    struct LibToriRS_IOQueue* io;
};

#endif