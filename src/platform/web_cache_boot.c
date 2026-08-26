/*
 * The browser's JS5 metadata barrier.
 *
 * ## Why this file exists at all
 *
 * On a desktop host the barrier is four lines: attach a JS5 primer, spin on
 * Tick until the reference tables are in, carry on into App_Init. That spin is
 * exactly what a browser cannot do. There is no ASYNCIFY on this lane (see
 * platform_check.mk), so a loop that never returns to the JavaScript event loop
 * never lets a WebSocket deliver a byte — the wait would be for data the wait
 * itself is preventing.
 *
 * The barrier also cannot simply be dropped and done later. App_Init decodes
 * reference tables itself, and a decode is not a tolerant reader: JS5 has to
 * have installed them before anything opens the cache. (docs/JS5_SERVER.md
 * calls this out — the ordering is the difference between a torn reference
 * table being repaired and it bricking the cache.)
 *
 * So the loop is inverted. Instead of C driving the pump and yielding to the
 * page, the page drives the pump and C keeps the state:
 *
 *     preRun:  addRunDependency('torirs-js5')
 *              _torirs_web_cache_prime_begin(manifest, host, port)
 *              setTimeout loop: _torirs_web_cache_prime_step()
 *                 -> 0 keep going, 1 ready, -1 failed
 *              removeRunDependency('torirs-js5')  ->  main() starts
 *
 * emscripten's run dependencies already mean "do not call main() yet", so the
 * barrier is expressed in the mechanism the runtime has for it, and main()
 * itself stays the same shape it has on every other host.
 *
 * ## What survives the prime
 *
 * The Js5 client and its disk are torn down when the prime finishes; the record
 * store is not. That is the same division the desktop build has (main.c frees
 * the sparse disk and leaves the files), and it is what lets App_Init open its
 * own disk over the same records a moment later.
 */

#include "platform/web_cache_boot.h"
#include <assert.h>

#include "app.h"
#include "bootmanifest/bootmanifest.h"
#include "executor_config.h"
#include "js5/js5.h"
#include "platform/dat2_web_store.h"
#include "platform/platform_sdl2.h"
#include "platform/platform_x_io_js5_cache.h"

#include <dat2disk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

struct WebCacheBoot
{
    struct Dat2WebStore* store;
    struct RSCache_Dat2Disk* disk;
    struct PlatformXIOJs5Cache* prime;

    char cache_dir[512];
    char js5_host[TORIRS_EXECUTOR_JS5_HOST_MAX];
    int js5_port;
    int js5_revision;

    int begun;
    int status; /* 0 running, 1 ready, -1 failed */

    /* The primer is torn down the moment it finishes, so its last progress is
     * copied out first. Without this the page's failure report reads zero bytes
     * received and concludes there is no server — the opposite diagnosis from
     * the one the numbers support. */
    struct Js5Progress final_progress;
};

static struct WebCacheBoot g_boot;

/* The store outlives the primer and is what App_Init opens its disk over. */
struct Dat2WebStore*
WebCacheBoot_Store(void)
{
    return g_boot.store;
}

const char*
WebCacheBoot_CacheDir(void)
{
    return g_boot.cache_dir[0] ? g_boot.cache_dir : NULL;
}

int
WebCacheBoot_Ready(void)
{
    return g_boot.status == 1;
}

const char*
WebCacheBoot_Js5Host(void)
{
    return g_boot.js5_host;
}

int
WebCacheBoot_Js5Port(void)
{
    return g_boot.js5_port;
}

int
WebCacheBoot_Js5Revision(void)
{
    return g_boot.js5_revision;
}

static void
web_cache_boot_release_primer(void)
{
    PlatformXIOJs5Cache_Free(g_boot.prime);
    g_boot.prime = NULL;
    RSCache_Dat2DiskFree(g_boot.disk);
    g_boot.disk = NULL;
}

/*
 * Read just enough of the manifest to know which cache this is.
 *
 * Deliberately the same BootManifest reader main() uses rather than a second
 * parse on the JavaScript side: the host would otherwise have to know what
 * `[cache:boot] dir=` means, and the two readers would disagree the first time
 * a manifest used a form only one of them handled.
 */
static int
web_cache_boot_read_manifest(const char* manifest_path)
{
    static struct BootManifest manifest;
    struct AppConfig cfg;
    struct ToriRS_ExecutorConfig executor;

    memset(&cfg, 0, sizeof(cfg));
    ToriRS_ExecutorConfig_Init(&executor);
    /* LoadFile resets the manifest itself, including the "not stated" sentinels
     * a zeroed struct would get wrong. */
    if( BootManifest_LoadFile(&manifest, manifest_path) != 0 )
    {
        fprintf(stderr, "web js5: cannot read manifest %s\n", manifest_path);
        return -1;
    }
    BootManifest_ApplyToConfig(&manifest, &cfg);
    BootManifest_ApplyToExecutorConfig(&manifest, &executor);

    if( !cfg.cache_dir || !cfg.cache_dir[0] )
    {
        fprintf(stderr, "web js5: manifest %s names no cache directory\n", manifest_path);
        return -1;
    }
    snprintf(g_boot.cache_dir, sizeof(g_boot.cache_dir), "%s", cfg.cache_dir);

    /*
     * The JS5 revision has to match what the server announces or the handshake
     * is answered with status 6. It comes from the cache identity for the same
     * reason the desktop build takes it from there: a manifest states one
     * generation, and a client asking for another is a configuration mistake
     * rather than something to negotiate.
     */
    g_boot.js5_revision = executor.js5_revision_explicit && executor.js5_revision > 0
                              ? executor.js5_revision
                              : cfg.cache_revision;
    if( g_boot.js5_revision <= 0 )
    {
        fprintf(stderr, "web js5: manifest %s states no cache revision\n", manifest_path);
        return -1;
    }
    return 0;
}

/*
 * Which cache the manifest names — the page's first question, and the reason
 * this is a separate call from the prime.
 *
 * The host has to hydrate the record store for a specific cache before any C
 * code can read it, and it cannot know which one without reading the manifest.
 * Answering here rather than teaching the page what `[cache:boot] dir=` means
 * keeps one manifest reader in the process; two of them would agree until the
 * first manifest used a form only one had learned.
 *
 * Returns NULL when the manifest is unreadable or names no cache.
 */
EMSCRIPTEN_KEEPALIVE const char*
torirs_web_cache_key(const char* manifest_path)
{
    if( g_boot.cache_dir[0] )
        return g_boot.cache_dir;
    if( !manifest_path || !manifest_path[0] )
    {
        fprintf(stderr, "web js5: no manifest to read a cache name from\n");
        return NULL;
    }
    if( web_cache_boot_read_manifest(manifest_path) != 0 )
        return NULL;
    return g_boot.cache_dir;
}

EMSCRIPTEN_KEEPALIVE int
torirs_web_cache_prime_begin(
    const char* js5_host,
    int js5_port)
{
    struct Js5Config js5;
    struct RSCache_Dat2Store ops;

    if( g_boot.begun )
        return g_boot.status;
    g_boot.begun = 1;
    g_boot.status = -1;

    if( !g_boot.cache_dir[0] )
    {
        fprintf(stderr, "web js5: torirs_web_cache_key was never called\n");
        return -1;
    }

    snprintf(
        g_boot.js5_host,
        sizeof(g_boot.js5_host),
        "%s",
        js5_host && js5_host[0] ? js5_host : "localhost");
    g_boot.js5_port = js5_port > 0 && js5_port <= 65535 ? js5_port : 43594;

    g_boot.store = Dat2WebStore_New(g_boot.cache_dir);
    if( !g_boot.store )
    {
        fprintf(
            stderr,
            "web js5: no record store for %s — the page harness did not open one\n",
            g_boot.cache_dir);
        return -1;
    }

    ops = Dat2WebStore_Ops(g_boot.store);
    g_boot.disk = RSCache_Dat2DiskNewFromStore(g_boot.cache_dir, &ops);
    if( !g_boot.disk )
    {
        fprintf(stderr, "web js5: cannot open a dat2 facade over the record store\n");
        return -1;
    }

    Js5ConfigInit(&js5);
    js5.host = g_boot.js5_host;
    js5.primary_port = (uint16_t)g_boot.js5_port;
    js5.fallback_port = 0; /* a browser has one endpoint; 443 is not a second one */
    js5.revision = (uint32_t)g_boot.js5_revision;
    /* Reference tables only. A background fill started here would race the
     * barrier it is holding up, and pull the whole cache before the client has
     * drawn a frame. */
    js5.background_fill = false;

    g_boot.prime = PlatformXIOJs5Cache_New(g_boot.disk, &js5);
    if( !g_boot.prime )
    {
        fprintf(stderr, "web js5: cannot attach the reference-table primer\n");
        return -1;
    }

    fprintf(
        stderr,
        "web js5: priming %s (rev %d) from ws://%s:%d\n",
        g_boot.cache_dir,
        g_boot.js5_revision,
        g_boot.js5_host,
        g_boot.js5_port);
    g_boot.status = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int
torirs_web_cache_prime_step(void)
{
    struct Js5Progress progress;

    if( g_boot.status != 0 )
        return g_boot.status;

    if( PlatformXIOJs5Cache_Tick(g_boot.prime, PlatformSDL2_Ticks64()) < 0 )
        g_boot.status = -1;
    else if( PlatformXIOJs5Cache_MetadataReady(g_boot.prime) )
        g_boot.status = 1;
    else
        return 0;

    PlatformXIOJs5Cache_GetProgress(g_boot.prime, &progress);
    g_boot.final_progress = progress;
    if( g_boot.status < 0 )
        fprintf(
            stderr,
            "web js5: reference-table prime failed (error=%d state=%d status=%u port=%u)\n",
            (int)progress.last_error,
            (int)progress.state,
            (unsigned)progress.handshake_status,
            (unsigned)progress.current_port);
    else
        /* Reported here as well as after App_Init because this is the pass that
         * actually downloads on a cold cache; the second one is a local CRC
         * check and would make a first boot look free. */
        fprintf(
            stderr,
            "web js5: reference tables primed (%u references, %llu network bytes)\n",
            (unsigned)progress.references_ready,
            (unsigned long long)progress.bytes_received);

    /*
     * The client is KEPT, not released.
     *
     * It was torn down here when the disk it filled was opened again by
     * App_Init a moment later and platform_x_io.c drove a second JS5 client of
     * its own. Nothing does that now: the platform executor is JavaScript, it
     * does not attach producers, and this is the only JS5 client in the
     * process. Freeing it would leave the cache with no way to grow, so every
     * group not already resident would be a permanent miss.
     *
     * Only the failure path lets go, because a client that could not reach its
     * server will not answer a demand either.
     */
    if( g_boot.status < 0 )
        web_cache_boot_release_primer();
    return g_boot.status;
}

/* --- the on-demand producer ---------------------------------------------- */
/*
 * How a group that is not in the database gets there.
 *
 * The executor reads the record database and nothing else -- archives have
 * their own transport, and this is it. On a miss the host asks here, drives
 * Tick until Status answers, and reads the database again; what JS5 downloads
 * lands there on the way through (Js5RscacheStorage -> the disk -> the store).
 *
 * Three calls rather than one blocking one, because the JavaScript side is
 * where the waiting belongs: a WebSocket delivers between turns of the event
 * loop, so anything that waited in C would be waiting for a message that cannot
 * arrive until it returns.
 */
EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_Js5Request(int table, int archive)
{
    if( !g_boot.prime )
        return -1;
    if( PlatformXIOJs5Cache_GroupReady(g_boot.prime, table, archive) )
        return 1;
    switch( PlatformXIOJs5Cache_RequestGroup(g_boot.prime, table, archive) )
    {
    case JS5_REQUEST_ALREADY_READY:
        return 1;
    case JS5_REQUEST_ERROR:
        return -1;
    default:
        return 0;
    }
}

/** Give the producer a turn. Driven by the page; see the comment above. */
EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_Js5Tick(void)
{
    if( !g_boot.prime )
        return -1;
    return PlatformXIOJs5Cache_Tick(g_boot.prime, PlatformSDL2_Ticks64());
}

/** 1 resident, 0 still coming, -1 the producer gave up on this group. */
EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_Js5Status(int table, int archive)
{
    if( !g_boot.prime )
        return -1;
    if( PlatformXIOJs5Cache_GroupReady(g_boot.prime, table, archive) )
        return 1;
    if( PlatformXIOJs5Cache_GroupFailed(g_boot.prime, table, archive) )
        return -1;
    return 0;
}

/* --- the frame loop's host-facing calls ---------------------------------- */
/*
 * These used to live in the wire backend and in platform_x_io.c, one copy
 * each. Neither file is built for a browser any more -- the executor is
 * JavaScript and platform_x_io.c is the desktop's -- but their contract was
 * never about where bytes come from: "give the asynchronous side a turn", and
 * "may a read block the frame". So they belong with the thing that IS
 * asynchronous here, which is the producer above.
 *
 * The pacing one matters: while JS5 has reads in flight the loop must run from
 * the event loop rather than from requestAnimationFrame, because a WebSocket
 * delivers between turns of the event loop and a display-rate loop would cap
 * the download at one round trip per frame.
 */
void
PlatformWeb_Pump(void)
{
    if( g_boot.prime )
        PlatformXIOJs5Cache_Tick(g_boot.prime, PlatformSDL2_Ticks64());
}

/*
 * Nothing to answer. There is no synchronous read anywhere on this platform to
 * suppress: every answer arrives after an await, which is the whole design.
 */
void
PlatformWeb_SetBlockingReads(int allowed)
{
    (void)allowed;
}

/*
 * How far along the prime is, for the page's status line: references installed,
 * network bytes, and the transport state — enough to tell "still downloading"
 * from "cannot reach the JS5 server", which otherwise look identical from
 * outside.
 */
EMSCRIPTEN_KEEPALIVE void
torirs_web_cache_prime_stats(int* out)
{
    struct Js5Progress progress;

    assert(out);
    memset(out, 0, 5 * sizeof(int));
    /* Live while the primer exists, the retained snapshot afterwards — which is
     * the case the page's failure report reads. */
    if( g_boot.prime )
        PlatformXIOJs5Cache_GetProgress(g_boot.prime, &progress);
    else
        progress = g_boot.final_progress;
    out[0] = g_boot.status;
    out[1] = (int)progress.references_ready;
    out[2] = (int)progress.bytes_received;
    out[3] = (int)progress.state;
    out[4] = (int)progress.last_error;
}
