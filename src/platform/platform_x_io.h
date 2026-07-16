#ifndef PLATFORM_X_IO_H
#define PLATFORM_X_IO_H

struct RSCache_Disk;
struct ToriRS_IO;
struct ToriRS_IOItem;
struct PlatformX_IO;

struct PlatformX_IO*
PlatformX_IO_New(
    struct RSCache_Disk* disk,
    const char* config_dir,
    const char* script_dir);

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
