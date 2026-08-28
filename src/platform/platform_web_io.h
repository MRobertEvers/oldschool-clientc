#ifndef PLATFORM_WEB_IO_H
#define PLATFORM_WEB_IO_H

#include <asyncio.h>

/*
 * The browser's platform IO executor.
 *
 * Every function here is DEFINED IN JAVASCRIPT -- platform/platform_web_io.js,
 * linked with --js-library so these symbols resolve to it. There is no C
 * implementation and no C shim forwarding into one; a shim would be a second
 * executor inside the seam, reading the queue twice, in two languages, with two
 * chances to disagree about what an item says.
 *
 * This header exists so C callers have declarations to compile against. It is
 * reached through platform/platform_io.h, which is what app.c and task_runner.h
 * actually include -- they name no platform.
 *
 * Nothing PlatformX is present in this build. platform_x_io.c is the desktop's
 * and is not compiled here, so a call that reached for it is a link error
 * rather than a name that quietly resolves to the wrong executor.
 *
 * ## The handle
 *
 * `struct PlatformWeb_IO` is declared and never defined, and never
 * dereferenced: the executor keeps its state on the JavaScript side, so what
 * crosses is an identifier. An incomplete type is how that is said in C.
 *
 * ## Answers arrive later
 *
 * A browser cannot read anything synchronously without freezing the tab, so
 * Process starts work and returns, and Pending reports what has not been
 * answered yet. That is the queue's own outstanding-item contract -- the same
 * one a JS5 miss uses on the desktop -- so nothing above the queue can tell
 * which executor it is talking to. No ASYNCIFY is involved, and none is
 * permitted on this lane (platform_check.mk): nothing in C is ever suspended,
 * because C hands the work over and asks later whether it is done.
 */

struct PlatformWeb_IO;
struct RSCache_Dat2Disk;
struct RSCache_Dat1Disk;

struct PlatformWeb_IO*
PlatformWeb_IO_New(void);

void
PlatformWeb_IO_Free(struct PlatformWeb_IO* px);

/* A browser opens no cache directory: the executor reads the record database
 * the cache producers fill. Both accept the call and record nothing, so the
 * client's boot reads the same on every platform. */
void
PlatformWeb_IO_InitDat2Disk(struct PlatformWeb_IO* px, struct RSCache_Dat2Disk* disk);

void
PlatformWeb_IO_InitDat1Disk(struct PlatformWeb_IO* px, struct RSCache_Dat1Disk* disk);

void
PlatformWeb_IO_InitConfigPath(struct PlatformWeb_IO* px, const char* config_path);

void
PlatformWeb_IO_InitScriptPath(struct PlatformWeb_IO* px, const char* script_path);

/*
 * Where to ask for a stored file this executor does not have.
 *
 * Accepted and recorded nowhere. The page is served BY the file server, and
 * the host IO reads every stored file relative to that same boot URL
 * (web/torirs_host.js), so a browser already knows the answer this call
 * carries and cannot be pointed somewhere else by a manifest. It exists so the
 * client's boot reads the same on every platform.
 */
void
PlatformWeb_IO_InitIoServer(struct PlatformWeb_IO* px, const char* host, int port);

void
PlatformWeb_IO_InitCacheId(
    struct PlatformWeb_IO* px,
    int epoch,
    int game,
    int revision,
    unsigned int quirks,
    const char* dir);

/**
 * Answer one item immediately, or refuse.
 *
 * Always refuses here, and that is the honest reply rather than a stub: every
 * answer on this platform arrives after an await, so there is never one to hand
 * back inside the call. Process is the only way in.
 */
int
PlatformWeb_IO_LoadItem(struct PlatformWeb_IO* px, struct ToriRS_IOItem* item);

int
PlatformWeb_IO_Process(struct PlatformWeb_IO* px, struct ToriRS_IO* io);

int
PlatformWeb_IO_Pending(struct PlatformWeb_IO* px, struct ToriRS_IO* io);

/**
 * Can the executor still reach whatever answers its reads?
 *
 * False only when a request went unanswered. A server that says "no such file"
 * is a server that is THERE, and clears this exactly as bytes would -- the
 * distinction the plugin lane needs, since a missing manifest is the ordinary
 * case for a client with no scripts installed.
 */
int
PlatformWeb_IO_ServerReachable(struct PlatformWeb_IO* px);

/* --- the frame loop's host-facing calls ---------------------------------- */
/*
 * Not part of the queue seam -- none of them takes a queue or an item -- so
 * they keep their own names and are not mapped in platform_io.h. main()'s loop
 * calls them directly, under TORIRS_PLATFORM_WEB.
 */

/** Give the asynchronous side a turn: drive the cache producer. */
void
PlatformWeb_Pump(void);

/**
 * How many reads are outstanding across every queue.
 *
 * PACING, not correctness: while reads are in flight the loop runs from the
 * event loop instead of requestAnimationFrame, because a WebSocket delivers
 * between turns of the event loop and a display-rate loop would cap delivery at
 * one round trip per frame.
 */
int
PlatformWeb_PendingTotal(void);

/**
 * May a read block the frame that issued it?
 *
 * Kept because the loop asks; the answer here is always no, and this platform
 * has no synchronous read to suppress.
 */
void
PlatformWeb_SetBlockingReads(int allowed);

#endif
