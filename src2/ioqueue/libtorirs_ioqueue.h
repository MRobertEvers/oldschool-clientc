#ifndef LIBTORIRS_IOQUEUE_H
#define LIBTORIRS_IOQUEUE_H

#include <stdbool.h>

#define LIBTORIRS_IOQUEUE_MAX_SIZE 128
#define LIBTORIRS_IOQUEUE_PATH_MAX 256

enum LibToriRS_IOQueueItem_Status
{
    TORIRSIO_PENDING = 0,
    TORIRSIO_RESOLVED,
    TORIRSIO_ERROR,
};

enum LibToriRS_IOResourceKind
{
    TORIRSIO_KIND_CACHE = 0,
    TORIRSIO_KIND_CONFIG_FILE,
};

struct LibToriRS_IOQueueItem
{
    enum LibToriRS_IOResourceKind kind;
    int table_id;
    int archive_id;
    int flags;
    char path[LIBTORIRS_IOQUEUE_PATH_MAX];

    enum LibToriRS_IOQueueItem_Status status;
    int error_code;
    void* data;
    int data_size;
};

struct LibToriRS_IOQueue
{
    struct LibToriRS_IOQueueItem items[LIBTORIRS_IOQUEUE_MAX_SIZE];
    int count;
    int read_head;
};

struct LibToriRS_IOQueue*
LibToriRS_IOQueueNew(void);

void
LibToriRS_IOQueueFree(struct LibToriRS_IOQueue* queue);

void
LibToriRS_IOQueueClear(struct LibToriRS_IOQueue* queue);

void
LibToriRS_IOQueuePush(
    struct LibToriRS_IOQueue* queue,
    int table_id,
    int archive_id,
    int flags);

bool
LibToriRS_IOQueuePushConfigFile(
    struct LibToriRS_IOQueue* queue,
    const char* path);

bool
LibToriRS_IOQueueIsEmpty(struct LibToriRS_IOQueue* queue);

bool
LibToriRS_IOQueuePopWrite(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* in);

bool
LibToriRS_IOQueuePopRead(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* out);

struct LibToriRS_IOQueueItem*
LibToriRS_IOQueuePopReadPtr(struct LibToriRS_IOQueue* queue);

#endif
