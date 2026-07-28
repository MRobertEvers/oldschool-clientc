#ifndef PLATFORM_X_IO_H
#define PLATFORM_X_IO_H

#include <asyncio.h>
#include <rscache.h>

struct PlatformX_IO*
PlatformX_IO_New(void);

void
PlatformX_IO_InitDat2Disk(
    struct PlatformX_IO* px,
    struct RSCache_Dat2Disk* disk);

void
PlatformX_IO_InitDat1Disk(
    struct PlatformX_IO* px,
    struct RSCache_Dat1Disk* disk);

void
PlatformX_IO_InitConfigPath(
    struct PlatformX_IO* px,
    const char* config_path);

void
PlatformX_IO_InitScriptPath(
    struct PlatformX_IO* px,
    const char* script_path);

void
PlatformX_IO_Free(struct PlatformX_IO* px);

int
PlatformX_IO_LoadItem(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item);

int
PlatformX_IO_Process(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io);

/**
 * Number of requests issued for `io` that have not been answered yet.
 *
 * The synchronous backends always return 0: Process satisfies every item
 * before it returns, so by the time anyone could ask, nothing is outstanding.
 * The web backend only *starts* its reads, so this is what tells the scheduler
 * not to resume a task that is still waiting — resuming it would run the code
 * after its PT_YIELD against an empty slot and report a decode failure.
 *
 * Per-io, not global: the app runs two task pipelines over one platform pump,
 * and one of them being blocked must not stall the other.
 */
int
PlatformX_IO_Pending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io);

#endif
