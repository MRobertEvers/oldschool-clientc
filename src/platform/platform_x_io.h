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
PlatformX_IO_Free(struct PlatformX_IO* io);

int
PlatformX_IO_LoadItem(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item);

int
PlatformX_IO_Process(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io);

#endif
