#ifndef LIBTORIRS_IO_H
#define LIBTORIRS_IO_H

#include <assert.h>
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

struct IOSlot
{
    int id;
};

struct LibToriRS_IOQueueItem
{
    enum LibToriRS_IOKind kind;
    enum LibToriRS_IOStat status;
    int run_id;
    int slot_id;
    bool consumed;

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
    int current_slot;
};

struct LibToriRS_IOContext
{
    struct LibToriRS_IOQueue* io;
};

#define LIBTORIRS_IOBATCH_MAX LIBTORIRS_IOQUEUE_MAX_SIZE

struct LibToriRS_IOBatch
{
    int count;
    int user[LIBTORIRS_IOBATCH_MAX];
};

static inline void
LibToriRS_IOBatchReset(struct LibToriRS_IOBatch* batch)
{
    batch->count = 0;
}

static inline int
LibToriRS_IOBatchAdd(struct LibToriRS_IOBatch* batch, int user_value)
{
    assert(batch->count < LIBTORIRS_IOBATCH_MAX);
    int slot = batch->count;
    batch->user[slot] = user_value;
    batch->count++;
    return slot;
}

static inline int
LibToriRS_IOBatchCount(const struct LibToriRS_IOBatch* batch)
{
    return batch->count;
}

static inline int
LibToriRS_IOBatchUser(const struct LibToriRS_IOBatch* batch, int slot)
{
    assert(slot >= 0 && slot < batch->count);
    return batch->user[slot];
}

static inline bool
LibToriRS_IOBatchEmpty(const struct LibToriRS_IOBatch* batch)
{
    return batch->count == 0;
}

#define IO_REQUEST(ctx, id, expr) \
    do { (ctx)->io->current_slot = (id); (expr); (ctx)->io->current_slot = -1; } while (0)

#endif