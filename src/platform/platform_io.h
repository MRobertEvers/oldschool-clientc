#ifndef PLATFORM_IO_H
#define PLATFORM_IO_H

/*
 * The IO queue's platform seam.
 *
 * The architecture is [game -> IO Queue] :> [platform IO executor]. This header
 * is the ":>" -- what the game may call, with no opinion about who answers.
 *
 * There are exactly two executors and they share no code:
 *
 *   PlatformX_*    the desktop. A filesystem and sockets, answering
 *                  synchronously (platform/platform_x_io.c).
 *
 *   PlatformWeb_*  the browser. IndexedDB and fetch, answering after an await,
 *                  and written in JavaScript because that is the language with
 *                  the async facilities -- platform/platform_web_io.js, linked
 *                  with --js-library so its functions ARE these symbols.
 *
 * THERE IS NO PlatformX IN THE WEB BUILD. Not a renamed one, not a shim
 * forwarding to one: platform_x_io.c is not compiled for the web at all, and
 * every name it defines is absent from that module. The two implementations
 * carry different prefixes precisely so that a symbol from the wrong one is a
 * link error rather than something that silently resolves.
 *
 * Callers use the Platform_IO_* names below and get whichever executor their
 * platform builds. Nothing above this line names a platform -- app.c and
 * task_runner.h queue work and ask what is outstanding; where the bytes come
 * from is settled here, once.
 */

#if defined(TORIRS_PLATFORM_WEB)
#include "platform/platform_web_io.h"

#define Platform_IO_New PlatformWeb_IO_New
#define Platform_IO_Free PlatformWeb_IO_Free
#define Platform_IO_InitDat2Disk PlatformWeb_IO_InitDat2Disk
#define Platform_IO_InitDat1Disk PlatformWeb_IO_InitDat1Disk
#define Platform_IO_InitConfigPath PlatformWeb_IO_InitConfigPath
#define Platform_IO_InitScriptPath PlatformWeb_IO_InitScriptPath
#define Platform_IO_InitCacheId PlatformWeb_IO_InitCacheId
#define Platform_IO_LoadItem PlatformWeb_IO_LoadItem
#define Platform_IO_Process PlatformWeb_IO_Process
#define Platform_IO_Pending PlatformWeb_IO_Pending
#define Platform_IO_ServerReachable PlatformWeb_IO_ServerReachable

/* The opaque handle. A browser's executor keeps its state on the JavaScript
 * side, so this is an identifier rather than a struct -- which is why nothing
 * anywhere dereferences one. */
#define Platform_IO struct PlatformWeb_IO

#else
#include "platform/platform_x_io.h"

#define Platform_IO_New PlatformX_IO_New
#define Platform_IO_Free PlatformX_IO_Free
#define Platform_IO_InitDat2Disk PlatformX_IO_InitDat2Disk
#define Platform_IO_InitDat1Disk PlatformX_IO_InitDat1Disk
#define Platform_IO_InitConfigPath PlatformX_IO_InitConfigPath
#define Platform_IO_InitScriptPath PlatformX_IO_InitScriptPath
#define Platform_IO_InitCacheId PlatformX_IO_InitCacheId
#define Platform_IO_LoadItem PlatformX_IO_LoadItem
#define Platform_IO_Process PlatformX_IO_Process
#define Platform_IO_Pending PlatformX_IO_Pending
#define Platform_IO_ServerReachable PlatformX_IO_ServerReachable

#define Platform_IO struct PlatformX_IO

#endif

#endif
