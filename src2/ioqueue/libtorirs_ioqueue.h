#ifndef LIBTORIRS_IOQUEUE_H
#define LIBTORIRS_IOQUEUE_H

#include "libtorirs_io.h"

#include <stdbool.h>

struct LibToriRS_IOQueue*
LibToriRS_IOQueueNew(void);

void
LibToriRS_IOQueueFree(struct LibToriRS_IOQueue* queue);

void
LibToriRS_IOQueueClear(struct LibToriRS_IOQueue* queue);

int
LibToriRS_IOQueueBeginRun(struct LibToriRS_IOQueue* queue);

bool
LibToriRS_IOQueueRunComplete(
    struct LibToriRS_IOQueue* queue,
    int run_id);

void
LibToriRS_IOQueuePushCache(
    struct LibToriRS_IOQueue* queue,
    int table_id,
    int archive_id,
    int flags);

bool
LibToriRS_IOQueuePushConfigFile(
    struct LibToriRS_IOQueue* queue,
    const char* path);

bool
LibToriRS_IOQueuePushScript(
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
