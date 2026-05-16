#ifndef LIBTORIRS_IOQUEUE_H
#define LIBTORIRS_IOQUEUE_H

#include <stdbool.h>

#define LIBTORIRS_IOQUEUE_MAX_SIZE 128

struct LibToriRS_IOQueueItem
{
    int table_id;
    int archive_id;
    int flags;

    bool is_resolved;
    void* data;
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
LibToriRS_IOQueueIsEmpty(struct LibToriRS_IOQueue* queue);

bool
LibToriRS_IOQueuePopWrite(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* in);

bool
LibToriRS_IOQueuePopRead(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* out);

#endif