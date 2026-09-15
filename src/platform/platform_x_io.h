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

/*
 * The local dat1 cache. Its remote alternative -- a LostCity server answering
 * the same reads over the 2004 on-demand protocol -- is enabled through
 * platform_x_io_ondemand.h rather than from here, for the same reason the JS5
 * attachment lives in platform_x_io_js5.h: App and TaskRunner see one IO
 * interface, and where the bytes come from is the executor's business.
 */
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

/**
 * @brief Where to ask for a stored file this disk does not have.
 *
 * The second leg of every stored-file read: the plugin manifest, the plugin
 * scripts it names, and each shipped plugin asset as a plugin asks for it.
 * TORIRS_IO_SERVER takes precedence, and an empty host says nothing rather
 * than turning the fallback off.
 */
void
PlatformX_IO_InitIoServer(
    struct PlatformX_IO* px,
    const char* host,
    int port);

/**
 * Name the cache this client reads from: the identity a boot manifest states,
 * plus the directory it lives in.
 *
 * A synchronous backend does not need telling — it was handed the open disk.
 * A remote one does: its requests are only meaningful against a particular
 * cache, and the server has to know which to open. Calling this on a local
 * backend is harmless and records nothing.
 */
void
PlatformX_IO_InitCacheId(
    struct PlatformX_IO* px,
    int epoch,
    int game,
    int revision,
    unsigned int quirks,
    const char* dir);

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
/**
 * @brief Is the read queued in `slot` still outstanding?
 *
 * Asked per task by the runner, so one task parked on a JS5 miss does not stop
 * the rest of the queue from being stepped. Always 0 for a purely synchronous
 * read: it was answered inside Process.
 */
int
PlatformX_IO_SlotPending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io,
    int slot);

int
PlatformX_IO_Pending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io);

/**
 * Can this backend still reach whatever answers its reads?
 *
 * Every browser lane can answer no — the wire one because every read crosses to
 * the IO server, the IndexedDB one because a read the local database misses
 * goes on to ask the same server for it. The desktop answers yes always, and
 * that is not a stub standing in for an unimplemented check: nothing sits
 * between it and its disk, so it has no server to be down, and a caller that
 * read "no server" as "server down" would switch off half the client on every
 * desktop build.
 *
 * This reports the TRANSPORT, not the file. A read that fails because the
 * server has no such path leaves this at yes — it proves the server is there —
 * which is exactly the distinction the plugin lane needs: a missing plugin
 * manifest is the ordinary case for a client with no scripts installed (see
 * task_plugin_io.c) and must never read as an outage.
 */
int
PlatformX_IO_ServerReachable(struct PlatformX_IO* px);

#endif
