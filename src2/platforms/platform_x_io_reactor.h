#ifndef PLATFORM_X_IO_REACTOR_H
#define PLATFORM_X_IO_REACTOR_H

#include "../ioqueue/libtorirs_ioqueue.h"

struct CacheLib;
struct LibToriPlatformX_IOReactor;

struct LibToriPlatformX_IOReactor*
LibToriPlatformX_IOReactorNew(struct CacheLib* cache);

void
LibToriPlatformX_IOReactorFree(struct LibToriPlatformX_IOReactor* reactor);

int
LibToriPlatformX_IOReactorLoadItem(
    struct LibToriPlatformX_IOReactor* reactor,
    struct LibToriRS_IOQueueItem* item);

int
LibToriPlatformX_IOReactorProcess(
    struct LibToriPlatformX_IOReactor* reactor,
    struct LibToriRS_IOQueue* queue);

#endif
