/*
 * The web build's disk.
 *
 * A browser tab cannot open a 170MB cache directory, and would not want to: the
 * client reads a few thousand archives out of it over a session and never most
 * of them. So this process keeps the cache open and answers reads one at a
 * time, running the *same* PlatformX_IO_LoadItem the native client runs. That
 * is what makes the two builds agree: there is no second implementation of
 * "what does this archive request mean", only one, on this side of the socket.
 *
 * It also serves build-web/ as static files, so `io_server` on its own is the
 * whole thing you need to run to open the client in a browser.
 *
 *   POST /io          an IOWire request batch -> an IOWire response batch
 *   GET  /boot/<path>  a manifest or RevConfig INI, under --boot-root
 *   GET  /cache/dat1/<table>/<archive>[?flags=N][&manifest=<path>]
 *                     one raw dat1 container, proxied off a LostCity server
 *   GET  /status      what this process is serving, as a page
 *   GET  /stats       the same counters, one line, for scripts
 *   GET  /...          a file under --root (default build-web/), "/" -> index.html
 *
 * ## The client names its world, not the command line
 *
 * Nothing about which world this serves is settled at startup. A batch names
 * the cache it is about, and a container fetch names the manifest it is booting
 * -- the same manifest path the client fetched through GET /boot/<path> moments
 * earlier, so there is one spelling of "which world" and no way for the two
 * ends to disagree about it. Caches, on-demand connections and parsed manifests
 * are all opened on first use and kept.
 *
 * So one process serves every world at once, and `--manifest` is only a
 * preopen: it moves "that server is not running" from a browser tab to the
 * command line that asked for it. It used to be the only way to reach an
 * on-demand world at all, which meant a second world meant a second process on
 * a second port.
 *
 * ## Why the dat1 proxy is here and not in the page
 *
 * A dat1 cache lives on the LostCity server, and a browser cannot read it.
 * Half of it is the 2004 on-demand protocol on the game port -- raw TCP,
 * which a page has no way to speak -- and the other half is eight jag
 * archives served over HTTP with no Access-Control-Allow-Origin, so a page
 * on this server's origin gets an opaque response it cannot read a byte of.
 * Neither is a gap in the page; both are what that server is.
 *
 * So this process holds the on-demand client and the page asks it. What
 * crosses is the container exactly as the server serves it, undecoded: the
 * browser stores raw containers for dat2 already, and one shape for both is
 * what keeps a single decode step at the far end.
 *
 * Usage:
 *   src/build/io_server [--port 8088]
 *   src/build/io_server --manifest manifests/manifest_rs254lc.ini [--port 8088]
 *   src/build/io_server --rev lc254 cache.rs254_zuk
 */

#include "http_server.h"

#include "bootmanifest/bootmanifest.h"
#include "asyncio.h"
#include "platform/io_wire.h"
#include "platform/platform_x_io.h"
#include "platform/platform_x_io_ondemand.h"

#include <assert.h>
#include <rscache.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define IO_SERVER_DEFAULT_PORT 8088
#define IO_SERVER_DEFAULT_ROOT "build-web"

/*
 * One open cache.
 *
 * Each carries its own PlatformX_IO, which is what makes the per-cache
 * decompressed-archive LRU inside it correct rather than a hazard: a group
 * archive cached for one cache must never answer a read against another.
 */
/* Long enough for a full-length cache directory plus the profile tail, so a
 * slot's log line never comes out cut in half. */
#define IO_SERVER_DESCRIBE_MAX (IOWIRE_CACHE_DIR_MAX + 128)

struct CacheSlot
{
    char dir[IOWIRE_CACHE_DIR_MAX];
    struct RSCache profile;
    struct PlatformX_IO* px;
    struct RSCache_Dat1Disk* dat1_disk;
    struct RSCache_Dat2Disk* dat2_disk;
    char describe[IO_SERVER_DESCRIBE_MAX];
    int failed_open; /* remember a refusal so it is reported once, not per read */
};

#define IO_SERVER_MAX_CACHES 8

/*
 * One LostCity server this process proxies.
 *
 * Keyed on the resolved endpoint rather than on the manifest that named it:
 * two manifests pointing at the same server are the same wire, and opening a
 * second connection for the second manifest would spend a slot — and a socket
 * the server moves into file-pipe state — on a server already held.
 */
struct OnDemandSource
{
    char host[128];
    int port;
    int ws_port;
    struct PlatformX_IO* px;
    char describe[IO_SERVER_DESCRIBE_MAX];
    int failed_open; /* remember a refusal so it is reported once, not per read */
};

#define IO_SERVER_MAX_ONDEMAND 4

/* A manifest path as the client spelled it, and the endpoint it resolved to.
 * `source` is an index into IoServer::ondemand, or -1 for a manifest that
 * parsed but names no on-demand cache — a negative answer worth caching, since
 * otherwise every read against a disk world re-parses the INI to be told so. */
struct ManifestBinding
{
    char path[HTTP_MAX_PATH];
    int source;
};

#define IO_SERVER_MAX_MANIFESTS 8

struct IoServer
{
    /*
     * Caches are opened on demand, because which one a client wants is the
     * client's business: the page's command line is its query string, so the
     * manifest can change without restarting anything. Every batch names its
     * cache (see IOWireCache), and the first request for one opens it.
     */
    struct CacheSlot caches[IO_SERVER_MAX_CACHES];
    int cache_count;

    /*
     * The LostCity proxies.
     *
     * There used to be one, settled at startup from --manifest, on the reading
     * that an on-demand cache IS the server named in [net:boot] and so there
     * could only be one per process. The first half is true; the second did not
     * follow from it. A disk cache is keyed on its directory and opened when a
     * batch names it, and the same is available here — the key is the endpoint,
     * and what names it is the client's manifest.
     *
     * So this is a table on the same terms as `caches`, and --manifest is now
     * only a preopen. What that buys is the thing the file header claims for
     * the disk side: the manifest can change without restarting anything, which
     * for an ondemand world it previously could not.
     */
    struct OnDemandSource ondemand[IO_SERVER_MAX_ONDEMAND];
    int ondemand_count;

    /*
     * Resolved manifest path -> endpoint, so a manifest is parsed once rather
     * than on every container fetch. A dat1 read is already a blocking round
     * trip; adding an INI parse in front of each one would be the larger half.
     */
    struct ManifestBinding manifests[IO_SERVER_MAX_MANIFESTS];
    int manifest_count;

    char root[512];
    /* Where GET /boot/<path> reads from: the tree the manifests live in, and
     * the root the caches themselves are resolved against. */
    char boot_root[512];
    char config_dir[256];
    char script_dir[256];
    int verbose;

    /* Only GET /status reads these. The banner already printed the port, but a
     * page reached through a hostname cannot see stdout. */
    int port;
    time_t started;

    long served;
    long failed;
    long bytes_out;
};

/* Resolve a client-named cache directory under the server's root. The name
 * arrives from another process, so it is input: no absolute paths, and nothing
 * that escapes the tree the server was pointed at.
 *
 * A `..` SEGMENT is not itself the danger, and refusing every one of them
 * refuses a legitimate cache: manifests live in manifests/ and name their
 * cache relative to themselves, so the honest spelling of the ordinary rev-239
 * cache is `manifests/../cache.osrs239`. What must be refused is a path that
 * LEAVES the root — a property of the resolved path, not of any segment in it.
 * So resolve first and judge after: a `..` with nothing left to pop is exactly
 * that escape, and is the one this rejects.
 *
 * Writes the resolved directory to `out`. Returns 0 (and leaves `out` empty)
 * for anything refused. */
static int
cache_dir_normalize(char const* dir, char* out, size_t cap)
{
    char const* cursor = dir;
    size_t used = 0;

    assert(dir);
    assert(out);
    assert(cap > 0);

    out[0] = '\0';
    if( !dir[0] )
        return 0;
    if( dir[0] == '/' )
        return 0;

    while( *cursor )
    {
        char const* end = strchr(cursor, '/');
        size_t len = end ? (size_t)(end - cursor) : strlen(cursor);

        if( len == 0 || (len == 1 && cursor[0] == '.') )
        {
            /* "" and "." resolve to the directory already held. */
        }
        else if( len == 2 && cursor[0] == '.' && cursor[1] == '.' )
        {
            char* last;
            if( used == 0 )
                return 0; /* nothing left to pop: this leaves the root */
            last = strrchr(out, '/');
            if( last )
                *last = '\0';
            else
                out[0] = '\0';
            used = strlen(out);
        }
        else
        {
            if( used + len + 2 > cap )
                return 0;
            if( used )
                out[used++] = '/';
            memcpy(out + used, cursor, len);
            used += len;
            out[used] = '\0';
        }

        if( !end )
            break;
        cursor = end + 1;
    }

    return out[0] != '\0';
}

static struct CacheSlot*
io_server_cache_for(
    struct IoServer* server,
    struct IOWireCache const* want)
{
    struct CacheSlot* slot = NULL;
    char path[1024];
    char quirks[32];
    char dir[IOWIRE_CACHE_DIR_MAX];

    assert(server);
    assert(want);

    if( !cache_dir_normalize(want->dir, dir, sizeof(dir)) )
    {
        fprintf(stderr, "io_server: refusing cache directory '%s'\n", want->dir);
        return NULL;
    }

    /* Keyed on the RESOLVED directory, not the spelling that arrived. The page
     * names its cache relative to its own manifest and the --manifest preopen
     * names it relative to the boot root, so one cache reaches here spelled two
     * ways; matching on the raw string would open it twice and spend the
     * second slot on a cache already held. */
    for( int i = 0; i < server->cache_count; i++ )
    {
        if( strcmp(server->caches[i].dir, dir) == 0 )
            return server->caches[i].failed_open ? NULL : &server->caches[i];
    }

    if( server->cache_count >= IO_SERVER_MAX_CACHES )
    {
        fprintf(stderr, "io_server: no room for another cache (%d open)\n", server->cache_count);
        return NULL;
    }

    slot = &server->caches[server->cache_count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->dir, sizeof(slot->dir), "%s", dir);
    slot->profile = RSCache_ProfileForIdentity(
        (enum RSCache_Game)want->game,
        (enum RSCache_Epoch)want->epoch,
        want->revision,
        want->quirks);

    RSCache_QuirksName(slot->profile.quirks, quirks, (int)sizeof(quirks));
    snprintf(
        slot->describe,
        sizeof(slot->describe),
        "%s (%s %s rev %d quirks %s)",
        dir,
        RSCache_EpochName(slot->profile.epoch),
        RSCache_GameName(slot->profile.game),
        slot->profile.revision,
        quirks);

    snprintf(path, sizeof(path), "%s/%s", server->boot_root, slot->dir);

    slot->px = PlatformX_IO_New();
    if( !slot->px )
    {
        slot->failed_open = 1;
        return NULL;
    }

    if( slot->profile.epoch == RSCACHE_EPOCH_DAT1 )
    {
        slot->dat1_disk = RSCache_Dat1DiskNewFromDirectory(path);
        if( !slot->dat1_disk )
        {
            fprintf(stderr, "io_server: no dat1 cache at %s\n", path);
            slot->failed_open = 1;
            return NULL;
        }
        PlatformX_IO_InitDat1Disk(slot->px, slot->dat1_disk);
    }
    else
    {
        slot->dat2_disk = RSCache_Dat2DiskNewFromDirectory(path);
        if( !slot->dat2_disk )
        {
            fprintf(stderr, "io_server: no dat2 cache at %s\n", path);
            slot->failed_open = 1;
            return NULL;
        }
        /* Table ids and the map XTEA gate are properties of the identity, not
         * of the container; without this every logical table is ABSENT. */
        RSCache_Dat2DiskSetProfile(slot->dat2_disk, &slot->profile);
        if( RSCache_MapLocsEncrypted(&slot->profile) )
        {
            char xtea_path[1152];
            snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", path);
            /* rscache keeps XTEA keys in one global table, so two open caches
             * that both need keys share it. Fine for the caches we ship (only
             * one era is keyed at a time); worth knowing before adding a
             * second keyed cache to a single session. */
            if( RSCache_XteaConfigLoadKeys(xtea_path) <= 0 )
                fprintf(stderr, "io_server: no xtea keys at %s (world maps may fail)\n",
                        xtea_path);
        }
        PlatformX_IO_InitDat2Disk(slot->px, slot->dat2_disk);
    }

    PlatformX_IO_InitConfigPath(slot->px, server->config_dir);
    PlatformX_IO_InitScriptPath(slot->px, server->script_dir);
    printf("io_server: opened %s\n", slot->describe);
    fflush(stdout);
    return slot;
}

/* -------------------------------------------------------------- on demand */

/* Defined with the static-file routes, which is where the rule it enforces
 * belongs; a manifest path is subject to the same one. */
static int
sanitize_path(char const* path, char* out, int out_size);

/*
 * One named query parameter, or 0 when absent.
 *
 * Matched at a parameter boundary rather than with strstr, which is what the
 * flags= read above does and would find `oldflags=` just as happily. Not
 * percent-decoded, for the same reason sanitize_path does not decode a path:
 * nothing this server is asked for needs it, and a decoder is a second place
 * for "what does this name mean" to be answered differently.
 */
static int
query_param(
    char const* tail,
    char const* name,
    char* out,
    int cap)
{
    char const* cursor = strchr(tail, '?');
    int name_len = (int)strlen(name);

    assert(tail);
    assert(name);
    assert(out);
    assert(cap > 0);

    out[0] = '\0';
    if( !cursor )
        return 0;
    cursor++;

    while( *cursor )
    {
        char const* value_end = strchr(cursor, '&');
        if( !value_end )
            value_end = cursor + strlen(cursor);

        if( (int)(value_end - cursor) > name_len + 1 &&
            strncmp(cursor, name, (size_t)name_len) == 0 &&
            cursor[name_len] == '=' )
        {
            int len = (int)(value_end - cursor) - name_len - 1;
            if( len >= cap )
                return 0;
            memcpy(out, cursor + name_len + 1, (size_t)len);
            out[len] = '\0';
            return 1;
        }
        cursor = *value_end ? value_end + 1 : value_end;
    }
    return 0;
}

/*
 * Get, or open, the wire to one LostCity server.
 *
 * Opening is where a dead server is discovered, and `failed_open` is what keeps
 * that from being rediscovered per read: the slot is kept, marked, and refused
 * from then on. Not retried, because the retry that matters is the operator
 * starting the server, and that is a restart of nothing — the next process to
 * ask gets a fresh slot.
 */
static struct OnDemandSource*
io_server_ondemand_for(
    struct IoServer* server,
    char const* host,
    int port,
    int ws_port)
{
    struct OnDemandSource* source;
    int enabled;

    assert(server);
    assert(host);

    for( int i = 0; i < server->ondemand_count; i++ )
    {
        if( strcmp(server->ondemand[i].host, host) == 0 &&
            server->ondemand[i].port == port &&
            server->ondemand[i].ws_port == ws_port )
            return server->ondemand[i].failed_open ? NULL : &server->ondemand[i];
    }

    if( server->ondemand_count >= IO_SERVER_MAX_ONDEMAND )
    {
        fprintf(stderr, "io_server: no room for another on-demand source (%d open)\n",
                server->ondemand_count);
        return NULL;
    }

    source = &server->ondemand[server->ondemand_count++];
    memset(source, 0, sizeof(*source));
    snprintf(source->host, sizeof(source->host), "%s", host);
    source->port = port;
    source->ws_port = ws_port;
    snprintf(source->describe, sizeof(source->describe),
        "%s:%d (dat1 on demand, web port %d)", host, port, ws_port);

    source->px = PlatformX_IO_New();
    assert(source->px);
    /* No hydration directory here on purpose: io_server PROXIES this cache to
     * a browser, which keeps its own copy in IndexedDB. A second one on the
     * server's disk would be a copy nobody reads. */
    enabled = PlatformXIO_Dat1OnDemandEnable(source->px, host, port, ws_port, NULL);
    if( enabled != 0 )
    {
        fprintf(stderr,
            "io_server: %s is not serving a cache (game port %d, web port %d)\n",
            host, port, ws_port);
        source->failed_open = 1;
        return NULL;
    }
    printf("io_server: proxying %s\n", source->describe);
    fflush(stdout);
    return source;
}

/*
 * The client's manifest, resolved to the server it names.
 *
 * The path is read the same way GET /boot/<path> reads one — under --boot-root,
 * sanitized — because it IS the same file, fetched by the same client moments
 * earlier. Anything else would mean two spellings of "which manifest" and a way
 * for them to disagree.
 */
static struct OnDemandSource*
io_server_ondemand_for_manifest(
    struct IoServer* server,
    char const* manifest_path)
{
    static struct BootManifest manifest; /* ~KBs; not worth a stack frame */
    struct ManifestBinding* binding = NULL;
    struct OnDemandSource* source;
    char rel[HTTP_MAX_PATH];
    char full[HTTP_MAX_PATH + 512];
    char with_slash[HTTP_MAX_PATH];

    assert(server);
    assert(manifest_path);

    for( int i = 0; i < server->manifest_count; i++ )
    {
        if( strcmp(server->manifests[i].path, manifest_path) == 0 )
        {
            if( server->manifests[i].source < 0 )
                return NULL;
            return &server->ondemand[server->manifests[i].source];
        }
    }

    /* sanitize_path wants a rooted path, and it is what decides whether this
     * one may be opened at all — so the leading slash is added rather than the
     * check being skipped for a spelling that arrived without one. */
    snprintf(with_slash, sizeof(with_slash), "%s%s",
             manifest_path[0] == '/' ? "" : "/", manifest_path);
    if( sanitize_path(with_slash, rel, (int)sizeof(rel)) != 0 || rel[0] != '/' )
    {
        fprintf(stderr, "io_server: refusing manifest path '%s'\n", manifest_path);
        return NULL;
    }
    snprintf(full, sizeof(full), "%s%s", server->boot_root, rel);

    if( BootManifest_LoadFile(&manifest, full) != 0 )
    {
        fprintf(stderr, "io_server: cannot read manifest %s\n", full);
        return NULL;
    }

    if( server->manifest_count < IO_SERVER_MAX_MANIFESTS )
    {
        binding = &server->manifests[server->manifest_count++];
        snprintf(binding->path, sizeof(binding->path), "%s", manifest_path);
        binding->source = -1;
    }

    if( !manifest.cache_on_demand )
    {
        /* A disk world. Its reads come through POST /io, which names the cache
         * directly; there is nothing to proxy. Remembered as -1 so the next
         * read does not re-parse the file to reach the same answer. */
        return NULL;
    }

    source = io_server_ondemand_for(
        server,
        manifest.host[0] ? manifest.host : "localhost",
        manifest.port > 0 ? manifest.port : 43594,
        manifest.ws_port > 0 ? manifest.ws_port : 80);
    if( source && binding )
        binding->source = (int)(source - server->ondemand);
    return source;
}

/* ------------------------------------------------------------------ cache */

/*
 * The reference table is the one kind the client does NOT want in the form
 * PlatformX_IO_LoadItem produces. Locally that call decodes the table into a
 * pointer graph, which does not cross a socket; the client links the same
 * decoder, so what travels is the archive it decodes from. Everything else
 * goes through LoadItem untouched.
 */
static void
load_reference_table_raw(
    struct CacheSlot* slot,
    struct ToriRS_IOItem* item)
{
    int logical = item->u.reference_table.table_id;
    int table_id;
    struct RSCache_Dat2DiskArchive* archive;

    if( logical < 0 || logical >= RSCACHE_DAT2_TABLE_COUNT )
    {
        item->error_code = -1;
        return;
    }
    table_id = RSCache_Dat2DiskTableId(slot->dat2_disk, (enum RSCache_Dat2Table)logical);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        item->error_code = -1;
        return;
    }

    archive = RSCache_Dat2DiskArchiveNewReferenceTableLoad(slot->dat2_disk, table_id);
    if( !archive )
    {
        item->error_code = -1;
        return;
    }

    /* Hand the payload over and let the (now data-less) archive go. */
    item->data = archive->data;
    item->data_size = archive->data_size;
    item->error_code = 0;
    archive->data = NULL;
    RSCache_Dat2DiskArchiveFree(archive);
}

/*
 * Does this slot hold the kind of cache the request needs?
 *
 * What a request asks for is input — it arrived from another process — not an
 * invariant. PlatformX_IO_LoadItem is entitled to assert that the disk it needs
 * is open, because in the native client App_Init opened it two calls earlier;
 * here the client is a browser tab, and an assert would take the whole server
 * down mid-session. It did once: a dat1 cache asked for a dat2 archive aborted
 * the server, and every later request in that tab failed at the transport with
 * nothing saying why. Now that a batch names its cache the right one gets
 * opened, and this is the backstop for a request that still does not fit the
 * cache it named.
 */
static int
slot_can_serve(
    struct CacheSlot* slot,
    struct ToriRS_IOItem const* item)
{
    int wants_dat1;

    if( item->kind == TORIRS_IOK_REFERENCE_TABLE )
        return slot->dat2_disk != NULL;
    if( item->kind != TORIRS_IOK_CACHE )
        return 1;

    wants_dat1 = item->u.cache.flags == TORIRS_IO_CACHE_DAT1 ||
                 item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN ||
                 item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY;

    return wants_dat1 ? (slot->dat1_disk != NULL) : (slot->dat2_disk != NULL);
}

static void
io_server_load(
    struct CacheSlot* slot,
    struct ToriRS_IOItem* item)
{
    item->data = NULL;
    item->data_size = 0;
    item->error_code = 0;

    if( !slot || !slot_can_serve(slot, item) )
    {
        item->error_code = -1;
        return;
    }

    /*
     * Client-owned files are refused outright.
     *
     * They are a browser's own device settings, served by that client against
     * its own filesystem and never encoded onto the wire — so one arriving here
     * is a request this server has no business honouring. Refusing by kind
     * rather than trusting that nothing sends one keeps a remote "write this
     * path" from ever becoming reachable: this process would run it with its
     * own privileges, against a path a client chose.
     */
    if( item->kind == TORIRS_IOK_FILE_READ || item->kind == TORIRS_IOK_FILE_WRITE )
    {
        fprintf(stderr, "io: refusing a client-file request; those stay on the client\n");
        item->error_code = -1;
        return;
    }

    if( item->kind == TORIRS_IOK_REFERENCE_TABLE )
        load_reference_table_raw(slot, item);
    else
        PlatformX_IO_LoadItem(slot->px, item);
}

static void
handle_io_batch(
    struct IoServer* server,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct IOWireReader reader;
    struct IOWireBuf out;
    struct IOWireCache wanted;
    struct CacheSlot* slot;
    int count;

    IOWireReader_Init(&reader, req->body, req->body_len);
    count = IOWire_BatchRead(&reader, &wanted);
    if( count < 0 )
    {
        res->status = 400;
        snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
        res->body = (void*)"malformed io batch\n";
        res->body_len = 19;
        res->owns_body = 0;
        return;
    }

    /* Every record in a batch is about the cache the batch named, so it is
     * resolved once — and a client naming one this server cannot open gets
     * failed items rather than a dropped connection. */
    slot = io_server_cache_for(server, &wanted);

    IOWireBuf_Init(&out);
    IOWire_BatchBegin(&out, NULL);

    for( int i = 0; i < count; i++ )
    {
        struct IOWireRequest wire_req;
        struct ToriRS_IOItem item;

        if( IOWire_ReadRequest(&reader, &wire_req) != 0 )
        {
            fprintf(stderr, "io: truncated request %d/%d\n", i, count);
            break;
        }

        IOWire_RequestToItem(&wire_req, &item);
        io_server_load(slot, &item);

        if( server->verbose || item.error_code != 0 )
        {
            char what[128];
            IOWire_DescribeItem(&item, what, (int)sizeof(what));
            fprintf(
                stderr,
                "io: %-26s %-30s %s (%d bytes)\n",
                slot ? slot->dir : wanted.dir,
                what,
                item.error_code == 0 ? "ok" : "FAILED",
                item.data_size);
        }
        if( item.error_code == 0 )
            server->served++;
        else
            server->failed++;

        IOWire_WriteResponse(&out, wire_req.req_id, &item);
        IOWire_FreeLoadedItem(&item);
    }

    if( out.error )
    {
        IOWireBuf_Free(&out);
        res->status = 500;
        snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
        res->body = (void*)"out of memory\n";
        res->body_len = 14;
        res->owns_body = 0;
        return;
    }

    server->bytes_out += out.len;
    res->status = 200;
    snprintf(res->content_type, sizeof(res->content_type), "application/octet-stream");
    res->body = out.data; /* handed to the server, freed after the write */
    res->body_len = out.len;
    res->owns_body = 1;
}

/* ------------------------------------------------------------ static files */

/* Reject anything that could escape the root. Query strings and fragments are
 * dropped; "%20" and friends are not decoded because no file we serve has one. */
static int
sanitize_path(
    char const* path,
    char* out,
    int out_size)
{
    int len = 0;

    if( path[0] != '/' )
        return -1;
    if( strstr(path, "..") )
        return -1;

    for( int i = 0; path[i] && path[i] != '?' && path[i] != '#'; i++ )
    {
        if( len >= out_size - 1 )
            return -1;
        out[len++] = path[i];
    }
    out[len] = '\0';
    if( len == 0 || out[len - 1] == '/' )
    {
        if( len + 10 >= out_size )
            return -1;
        snprintf(out + len, (size_t)(out_size - len), "index.html");
    }
    return 0;
}

static void
serve_file(
    struct IoServer* server,
    char const* full,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    FILE* file;
    long size;
    void* data;
    struct stat info;

    /*
     * A validator first, so a client that already has this file can be told so
     * instead of being sent it again.
     *
     * mtime and size together, which is what every static server uses and is
     * exactly as strong as the question being asked: "is the copy I fetched
     * still the file that is there?". It is not a content hash and does not
     * claim to be — a file rewritten within the same second at the same length
     * would be missed, which for a manifest someone is editing by hand is a
     * rounding error against re-reading it on every boot.
     */
    if( stat(full, &info) == 0 )
    {
        snprintf(
            res->etag,
            sizeof(res->etag),
            "\"%lld-%lld\"",
            (long long)info.st_mtime,
            (long long)info.st_size);
        if( HttpRequest_MatchesETag(req, res->etag) )
        {
            res->status = 304;
            res->body = NULL;
            res->body_len = 0;
            if( server->verbose )
                fprintf(stderr, "http: 304 %s (unchanged)\n", full);
            return;
        }
    }

    file = fopen(full, "rb");
    if( !file )
    {
        fprintf(stderr, "http: 404 %s\n", full);
        res->status = 404;
        res->etag[0] = '\0';
        return;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        res->status = 500;
        return;
    }
    data = malloc((size_t)size ? (size_t)size : 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        res->status = 500;
        return;
    }
    fclose(file);

    res->status = 200;
    snprintf(res->content_type, sizeof(res->content_type), "%s",
             HttpServer_ContentTypeForPath(full));
    res->body = data;
    res->body_len = (int)size;
    res->owns_body = 1;
    if( server->verbose )
        fprintf(stderr, "http: 200 %s (%ld bytes)\n", full, size);
}

static void
handle_static(
    struct IoServer* server,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char rel[HTTP_MAX_PATH];
    char full[HTTP_MAX_PATH + 512];

    if( sanitize_path(req->path, rel, (int)sizeof(rel)) != 0 )
    {
        res->status = 400;
        return;
    }
    snprintf(full, sizeof(full), "%s%s", server->root, rel);
    serve_file(server, full, req, res);

    /*
     * A bare 404 at the root is the least useful answer this server can give:
     * it is what someone sees when they point a browser at the host to check
     * that the server is up, and build-web/ has not been built. Send them to
     * the status page, which reports the missing client as its first line —
     * so the redirect informs rather than hides.
     *
     * Only the root. A 404 for some other path is a real 404 and must stay
     * one; redirecting every miss would turn a mistyped asset into a page
     * that loads, which is how a broken build looks like a working one.
     */
    if( res->status == 404 && strcmp(rel, "/index.html") == 0 )
    {
        res->status = 302;
        snprintf(res->location, sizeof(res->location), "/status");
    }
}

/*
 * GET /boot/<path> — a file the client reads with fopen rather than through the
 * IO queue: a boot manifest, a RevConfig INI.
 *
 * These used to be baked into the module with --preload-file, which made the
 * page's command line a lie: the manifest is chosen by the query string, so a
 * manifest that was not linked in could be named but not opened. Serving them
 * means any manifest works against any build, and a new one needs no relink.
 *
 * Rooted at --boot-root (default the working directory, i.e. the repo root)
 * rather than at --root: these are source files, not build output.
 */
static void
handle_boot_file(
    struct IoServer* server,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char rel[HTTP_MAX_PATH];
    char full[HTTP_MAX_PATH + 512];

    if( sanitize_path(req->path + 5, rel, (int)sizeof(rel)) != 0 || rel[0] != '/' )
    {
        res->status = 400;
        return;
    }
    snprintf(full, sizeof(full), "%s%s", server->boot_root, rel);
    serve_file(server, full, req, res);
}

/*
 * GET /cache/dat1/<table>/<archive>[?flags=N] -- one raw dat1 container.
 *
 * The page's dat1 producer, on the other side of this route, is a fetch and
 * nothing more: every decision about what a read MEANS is made here, by the
 * same code the native client runs. `flags` is the item's cache flags, and
 * it is on the wire because a map read names a SQUARE -- resolving it to an
 * archive needs the server's versionlist, which is on this side.
 *
 * 404 is an answer, not a failure: a built world has holes at its edges, and
 * the client already treats an absent archive as one. 503 is the outage --
 * no on-demand source configured at all -- and says so separately so a page
 * cannot read "this world has no such map" out of "you did not point me at
 * a server".
 */
static void
handle_ondemand_container(
    struct IoServer* server,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char const* cursor = req->path + 12;
    char* end = NULL;
    long table_id;
    long archive_id;
    long flags = TORIRS_IO_CACHE_DAT1;
    char const* query;
    char manifest_path[HTTP_MAX_PATH];
    struct OnDemandSource* source;
    uint8_t* bytes;
    int format = 0;
    int size = 0;

    assert(server);
    assert(req);
    assert(res);

    table_id = strtol(cursor, &end, 10);
    if( end == cursor || *end != '/' )
    {
        res->status = 400;
        return;
    }
    cursor = end + 1;
    archive_id = strtol(cursor, &end, 10);
    if( end == cursor )
    {
        res->status = 400;
        return;
    }
    if( table_id < 0 || archive_id < 0 )
    {
        res->status = 400;
        return;
    }

    query = strstr(end, "flags=");
    if( query )
        flags = strtol(query + 6, NULL, 10);

    /*
     * Which server to ask, named by the client's own manifest.
     *
     * The fallback is what keeps a client that names none working: with a
     * single source open — which is what --manifest leaves behind — there is
     * no ambiguity to resolve. With several, there is, and guessing among them
     * would serve one world's containers to another; that is the 503.
     */
    if( query_param(end, "manifest", manifest_path, (int)sizeof(manifest_path)) )
    {
        source = io_server_ondemand_for_manifest(server, manifest_path);
    }
    else if( server->ondemand_count == 1 )
    {
        source = server->ondemand[0].failed_open ? NULL : &server->ondemand[0];
    }
    else
    {
        source = NULL;
    }

    if( !source )
    {
        res->status = 503;
        return;
    }

    bytes = PlatformXIO_Dat1OnDemandContainerFetch(
        source->px, (int)table_id, (int)archive_id, (int)flags, &format, &size);
    if( !bytes || size <= 0 )
    {
        free(bytes);
        server->failed++;
        res->status = 404;
        return;
    }

    /* The format is NOT sent. It is a function of the table -- the jag
     * archives are DAT_MULTIFILE and everything else is DAT -- so the far
     * side derives it from the read it already made, and there is no header
     * for the two ends to disagree about. Read here only to keep the
     * out-parameter honest. */
    (void)format;

    server->served++;
    server->bytes_out += size;
    res->status = 200;
    res->body = bytes;
    res->body_len = size;
    res->owns_body = 1;
}

/* --------------------------------------------------------------- /status */

/*
 * Cache directories reach this process from a client — a batch names the cache
 * it is about — so every one of them is text someone else chose, and it is
 * printed back into a page. Escaped on the way out rather than validated on the
 * way in: cache_dir_normalize's job is to keep a read inside the tree, not to
 * decide which bytes are safe in HTML, and the two questions have different
 * right answers.
 */
static void
html_escape(
    char const* text,
    char* out,
    int cap)
{
    int len = 0;

    assert(text);
    assert(out);
    assert(cap > 0);

    for( int i = 0; text[i]; i++ )
    {
        char const* entity = NULL;

        switch( text[i] )
        {
        case '&': entity = "&amp;"; break;
        case '<': entity = "&lt;"; break;
        case '>': entity = "&gt;"; break;
        case '"': entity = "&quot;"; break;
        default: break;
        }

        if( entity )
        {
            int entity_len = (int)strlen(entity);
            if( len + entity_len >= cap )
                break;
            memcpy(out + len, entity, (size_t)entity_len);
            len += entity_len;
            continue;
        }
        if( len + 1 >= cap )
            break;
        out[len++] = text[i];
    }
    out[len] = '\0';
}

/* Escaping can grow a string sixfold ("&quot;"), and every string that reaches
 * the page goes through it, so a scratch buffer is sized against the longest
 * one the server holds rather than against any one call site. */
#define IO_SERVER_ESCAPED_MAX ((IO_SERVER_DESCRIBE_MAX * 6) + 1)

/*
 * GET /status — what this process is, in a browser.
 *
 * /stats answers the same question in one line for a script; this is the one
 * for a person who typed the host into a URL bar and needs to know whether they
 * reached the server at all, which cache it holds, and whether the LostCity
 * proxy came up. Self-contained markup on purpose: it has to render when
 * build-web/ is missing, which is exactly the case that sends someone here.
 */
static void
handle_status(
    struct IoServer* server,
    struct HttpResponse* res)
{
    /*
     * Every table on this page is bounded by its own MAX, and every cell is
     * bounded by the escape buffer, so the worst page has a size and one
     * allocation covers it — the writer never has to grow. A row is charged
     * one escaped string plus its markup and any unescaped describe.
     */
    int const row = IO_SERVER_ESCAPED_MAX + IO_SERVER_DESCRIBE_MAX + 512;
    int const cap = 16384 +
                    (IO_SERVER_MAX_CACHES * row) +
                    (IO_SERVER_MAX_ONDEMAND * row) +
                    (IO_SERVER_MAX_MANIFESTS * row);
    char* page = malloc((size_t)cap);
    char escaped[IO_SERVER_ESCAPED_MAX];
    long uptime;
    int len = 0;

    assert(server);
    assert(page);

    uptime = (long)(time(NULL) - server->started);

    len += snprintf(
        page + len, (size_t)(cap - len),
        "<!doctype html>\n"
        "<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        /* A status page nobody has to reload by hand. Five seconds is short
         * enough to watch a cache open and long enough to be free. */
        "<meta http-equiv=\"refresh\" content=\"5\">\n"
        "<title>io_server status</title>\n"
        "<style>\n"
        "body{font:14px/1.5 ui-monospace,Consolas,monospace;margin:0;padding:2rem;\n"
        "     background:#14161a;color:#d8dee9}\n"
        "h1{font-size:1.25rem;margin:0 0 .25rem}\n"
        "h2{font-size:.95rem;margin:2rem 0 .5rem;color:#88c0d0;\n"
        "   text-transform:uppercase;letter-spacing:.08em}\n"
        "table{border-collapse:collapse;width:100%%;max-width:70rem}\n"
        "td,th{text-align:left;padding:.35rem .75rem .35rem 0;\n"
        "      border-bottom:1px solid #2b303b;vertical-align:top}\n"
        "th{color:#7b8394;font-weight:normal;white-space:nowrap}\n"
        ".up{color:#a3be8c}.down{color:#bf616a}.muted{color:#7b8394}\n"
        "code{color:#ebcb8b}\n"
        "</style></head><body>\n"
        "<h1>io_server <span class=\"up\">up</span></h1>\n"
        "<p class=\"muted\">port %d &middot; %ld s uptime &middot; refreshes every 5 s</p>\n",
        server->port,
        uptime);

    /*
     * The reason someone lands here by redirect rather than by typing /status,
     * so it goes first. stat rather than a flag set at startup: --root can be
     * built while the server runs, and a page that still says "no client" once
     * there is one would send someone looking for a fault that is fixed.
     */
    {
        char index_path[HTTP_MAX_PATH + 512];
        struct stat info;

        snprintf(index_path, sizeof(index_path), "%s/index.html", server->root);
        if( stat(index_path, &info) != 0 )
        {
            html_escape(server->root, escaped, (int)sizeof(escaped));
            len += snprintf(
                page + len, (size_t)(cap - len),
                "<p class=\"down\">No <code>index.html</code> under "
                "<code>%s</code> — the web client is not built, so "
                "<code>GET /</code> lands here. Build it, or point "
                "<code>--root</code> at a tree that has one.</p>\n",
                escaped);
        }
    }

    len += snprintf(
        page + len, (size_t)(cap - len),
        "<h2>Requests</h2><table>\n"
        "<tr><th>served</th><td>%ld</td></tr>\n"
        "<tr><th>failed</th><td>%ld</td></tr>\n"
        "<tr><th>bytes out</th><td>%ld (%.1f MiB)</td></tr>\n"
        "</table>\n",
        server->served,
        server->failed,
        server->bytes_out,
        (double)server->bytes_out / (1024.0 * 1024.0));

    html_escape(server->root, escaped, (int)sizeof(escaped));
    len += snprintf(
        page + len, (size_t)(cap - len),
        "<h2>Paths</h2><table>\n"
        "<tr><th>static root</th><td><code>%s</code></td></tr>\n",
        escaped);
    html_escape(server->boot_root, escaped, (int)sizeof(escaped));
    len += snprintf(
        page + len, (size_t)(cap - len),
        "<tr><th>boot root</th><td><code>%s</code></td></tr>\n",
        escaped);
    html_escape(server->config_dir, escaped, (int)sizeof(escaped));
    len += snprintf(
        page + len, (size_t)(cap - len),
        "<tr><th>config</th><td><code>%s</code></td></tr>\n",
        escaped);
    html_escape(server->script_dir, escaped, (int)sizeof(escaped));
    len += snprintf(
        page + len, (size_t)(cap - len),
        "<tr><th>script</th><td><code>%s</code></td></tr>\n"
        "</table>\n",
        escaped);

    len += snprintf(
        page + len, (size_t)(cap - len),
        "<h2>Caches (%d of %d open)</h2>\n",
        server->cache_count,
        IO_SERVER_MAX_CACHES);
    if( server->cache_count == 0 )
    {
        /* Not an error: caches open on the first batch that names one, so an
         * empty table is the normal state of a server nobody has asked yet. */
        len += snprintf(
            page + len, (size_t)(cap - len),
            "<p class=\"muted\">None yet — a cache opens on the first request "
            "that names it.</p>\n");
    }
    else
    {
        len += snprintf(
            page + len, (size_t)(cap - len),
            "<table><tr><th>state</th><th>identity</th></tr>\n");
        for( int i = 0; i < server->cache_count; i++ )
        {
            html_escape(server->caches[i].describe, escaped, (int)sizeof(escaped));
            len += snprintf(
                page + len, (size_t)(cap - len),
                "<tr><td class=\"%s\">%s</td><td><code>%s</code></td></tr>\n",
                server->caches[i].failed_open ? "down" : "up",
                server->caches[i].failed_open ? "failed" : "open",
                escaped);
        }
        len += snprintf(page + len, (size_t)(cap - len), "</table>\n");
    }

    len += snprintf(
        page + len, (size_t)(cap - len),
        "<h2>LostCity proxies (%d of %d)</h2>\n",
        server->ondemand_count,
        IO_SERVER_MAX_ONDEMAND);
    if( server->ondemand_count == 0 )
    {
        len += snprintf(
            page + len, (size_t)(cap - len),
            "<p class=\"muted\">None yet — one opens when a client asks for "
            "<code>/cache/dat1/…?manifest=&lt;path&gt;</code> with a manifest "
            "whose boot cache is <code>source=ondemand</code>.</p>\n");
    }
    else
    {
        len += snprintf(
            page + len, (size_t)(cap - len),
            "<table><tr><th>state</th><th>server</th></tr>\n");
        for( int i = 0; i < server->ondemand_count; i++ )
        {
            html_escape(server->ondemand[i].describe, escaped, (int)sizeof(escaped));
            len += snprintf(
                page + len, (size_t)(cap - len),
                "<tr><td class=\"%s\">%s</td><td><code>%s</code></td></tr>\n",
                server->ondemand[i].failed_open ? "down" : "up",
                server->ondemand[i].failed_open ? "unreachable" : "connected",
                escaped);
        }
        len += snprintf(page + len, (size_t)(cap - len), "</table>\n");
    }

    len += snprintf(
        page + len, (size_t)(cap - len),
        "<h2>Manifests resolved (%d of %d)</h2>\n",
        server->manifest_count,
        IO_SERVER_MAX_MANIFESTS);
    if( server->manifest_count == 0 )
    {
        len += snprintf(
            page + len, (size_t)(cap - len),
            "<p class=\"muted\">None — no client has named one yet.</p>\n");
    }
    else
    {
        len += snprintf(
            page + len, (size_t)(cap - len),
            "<table><tr><th>manifest</th><th>reads from</th></tr>\n");
        for( int i = 0; i < server->manifest_count; i++ )
        {
            int bound = server->manifests[i].source;
            html_escape(server->manifests[i].path, escaped, (int)sizeof(escaped));
            len += snprintf(
                page + len, (size_t)(cap - len),
                "<tr><td><code>%s</code></td><td>%s</td></tr>\n",
                escaped,
                bound >= 0 ? server->ondemand[bound].describe
                           : "its own cache directory (disk world)");
        }
        len += snprintf(page + len, (size_t)(cap - len), "</table>\n");
    }

    len += snprintf(
        page + len, (size_t)(cap - len),
        "<h2>Endpoints</h2><table>\n"
        "<tr><th>GET /</th><td>the client, from the static root</td></tr>\n"
        "<tr><th>GET /status</th><td>this page</td></tr>\n"
        "<tr><th>GET /stats</th><td>the same counters, one line, for scripts</td></tr>\n"
        "<tr><th>GET /boot/&lt;path&gt;</th><td>a manifest or RevConfig INI</td></tr>\n"
        "<tr><th>GET /cache/dat1/&lt;table&gt;/&lt;archive&gt;</th>"
        "<td>one raw container off the LostCity server</td></tr>\n"
        "<tr><th>POST /io</th><td>an IOWire request batch</td></tr>\n"
        "</table>\n"
        "</body></html>\n");

    res->status = 200;
    snprintf(res->content_type, sizeof(res->content_type), "text/html; charset=utf-8");
    res->body = page;
    res->body_len = len < cap ? len : cap - 1;
    res->owns_body = 1;
}

/*
 * Is this request a person looking at a page, or a program reading bytes?
 *
 * The two want opposite things from a failure. A browser wants a page saying
 * what went wrong; the client's fetch wants the status code and nothing it has
 * to parse around. Accept is exactly that question asked by the request itself,
 * so there is no need to sniff User-Agent or guess from the path: a browser
 * lists `text/html` on a navigation and asks for anything on a fetch.
 */
static int
wants_html(struct HttpRequest const* req)
{
    int len = 0;
    char const* accept;

    assert(req);

    accept = HttpRequest_Header(req, "Accept", &len);
    if( !accept )
        return 0;
    for( int i = 0; i + 9 <= len; i++ )
    {
        if( memcmp(accept + i, "text/html", 9) == 0 )
            return 1;
    }
    return 0;
}

/*
 * The body for a failure a browser is about to render.
 *
 * Every route here reports failure by setting a status and returning, which is
 * right — a handler should not each carry its own idea of what an error looks
 * like. So the page is put on at the end, in one place, for whatever status
 * came back. A program still gets the terse body it got before; only a
 * navigation gets markup.
 */
static void
render_error_page(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    static struct
    {
        int status;
        char const* what;
    } const reasons[] = {
        { 400, "The server could not parse that request." },
        { 404, "No such path on this server." },
        { 405, "That method is not allowed on this path. This server answers "
               "GET, HEAD and OPTIONS everywhere, and POST at /io." },
        { 500, "The server failed while answering that." },
        { 503, "This server has no LostCity connection, so cache reads that "
               "need one cannot be answered." },
    };
    char const* what = "The server could not answer that.";
    char escaped_path[(HTTP_MAX_PATH * 6) + 1];
    char* body;
    int const cap = (int)sizeof(escaped_path) + 1024;

    assert(req);
    assert(res);

    for( size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++ )
    {
        if( reasons[i].status == res->status )
        {
            what = reasons[i].what;
            break;
        }
    }

    /* The path is echoed back into the page, and it is the most attacker-
     * controlled string this process handles — it arrived in the request line. */
    html_escape(req->path, escaped_path, (int)sizeof(escaped_path));

    body = malloc((size_t)cap);
    assert(body);
    res->body_len = snprintf(
        body, (size_t)cap,
        "<!doctype html>\n"
        "<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        "<title>%d — io_server</title>\n"
        "<style>body{font:14px/1.6 ui-monospace,Consolas,monospace;margin:0;\n"
        "padding:3rem 2rem;background:#14161a;color:#d8dee9}\n"
        "h1{font-size:2.5rem;margin:0;color:#bf616a}\n"
        "p{max-width:40rem}code{color:#ebcb8b}a{color:#88c0d0}\n"
        "</style></head><body>\n"
        "<h1>%d</h1>\n"
        "<p>%s</p>\n"
        "<p class=\"muted\"><code>%s %s</code></p>\n"
        "<p><a href=\"/status\">/status</a> — what this server is serving.</p>\n"
        "</body></html>\n",
        res->status,
        res->status,
        what,
        req->method,
        escaped_path);
    snprintf(res->content_type, sizeof(res->content_type), "text/html; charset=utf-8");
    res->body = body;
    res->owns_body = 1;
}

static void
io_server_route(
    struct IoServer* server,
    struct HttpRequest const* req,
    struct HttpResponse* res);

static void
io_server_handler(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct IoServer* server = (struct IoServer*)user;

    io_server_route(server, req, res);

    /*
     * A HEAD is a GET whose body is dropped: routing it as one is what makes
     * every path answer it, rather than the dispatcher's fallthrough turning a
     * link check of a file that exists into a 404. The Content-Length then
     * goes out as 0 rather than the entity's length — permitted, and the
     * alternative is every handler learning to size a body it will not send.
     */
    if( strcmp(req->method, "HEAD") == 0 )
    {
        if( res->owns_body )
            free(res->body);
        res->body = NULL;
        res->body_len = 0;
        res->owns_body = 0;
        return;
    }

    if( res->status >= 400 && !res->body && wants_html(req) )
        render_error_page(req, res);
}

static void
io_server_route(
    struct IoServer* server,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    if( strcmp(req->method, "OPTIONS") == 0 )
    {
        res->status = 204;
        res->body = NULL;
        res->body_len = 0;
        return;
    }
    if( strcmp(req->method, "POST") == 0 && strncmp(req->path, "/io", 3) == 0 )
    {
        handle_io_batch(server, req, res);
        return;
    }
    if( strcmp(req->method, "GET") == 0 || strcmp(req->method, "HEAD") == 0 )
    {
        /*
         * Every browser asks for this on every navigation, and nothing here
         * has one to give. Answering 204 rather than 404 keeps a request the
         * user did not make out of the failure count and out of the log, where
         * it reads as a fault in whatever they were actually loading. If the
         * static root does have a favicon, the normal path below serves it.
         */
        if( strcmp(req->path, "/favicon.ico") == 0 )
        {
            char full[HTTP_MAX_PATH + 512];
            struct stat info;

            snprintf(full, sizeof(full), "%s/favicon.ico", server->root);
            if( stat(full, &info) != 0 )
            {
                res->status = 204;
                res->body = NULL;
                res->body_len = 0;
                return;
            }
        }
        if( strncmp(req->path, "/boot/", 6) == 0 )
        {
            handle_boot_file(server, req, res);
            return;
        }
        if( strncmp(req->path, "/cache/dat1/", 12) == 0 )
        {
            handle_ondemand_container(server, req, res);
            return;
        }
        if( strcmp(req->path, "/status") == 0 )
        {
            handle_status(server, res);
            return;
        }
        if( strcmp(req->path, "/stats") == 0 )
        {
            char* text = malloc(256);
            int len;
            assert(text);
            /* Which caches are open, so a page can say what it is talking to.
             * The first is the one --manifest preopened, if any. */
            len = snprintf(
                text, 256, "serving=%s served=%ld failed=%ld bytes_out=%ld\n",
                server->cache_count ? server->caches[0].describe : "(none yet)",
                server->served, server->failed, server->bytes_out);
            res->status = 200;
            snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
            res->body = text;
            res->body_len = len;
            res->owns_body = 1;
            return;
        }
        handle_static(server, req, res);
        return;
    }
    /*
     * The method, not the path, is what this server cannot answer — a PUT to a
     * path that exists is still refused. 405 says which of the two is wrong;
     * the 404 this used to send sent someone looking for a missing route.
     */
    res->status = 405;
}

/* -------------------------------------------------------------------- main */

static void
on_signal(int sig)
{
    (void)sig;
    HttpServer_RequestStop();
}

static void
usage(char const* argv0)
{
    fprintf(
        stderr,
        "usage: %s [--manifest <boot.ini> | --rev <name> <cache_dir>]\n"
        "          [--port N] [--root DIR] [--boot-root DIR] [--config DIR]\n"
        "          [--script DIR] [-v]\n"
        "\n"
        "  --manifest   PREOPEN one world's cache (and its LostCity wire) at\n"
        "               startup, so an unreachable server is named here rather\n"
        "               than in a browser tab. Optional: a client names its own\n"
        "               manifest per request, so this serves every world without\n"
        "               it\n"
        "  --rev        named cache profile, with the cache dir as a positional\n"
        "  --root       directory served over GET (default %s)\n"
        "  --boot-root  directory served over GET /boot/<path> — the manifests and\n"
        "               RevConfig INIs the client opens by name (default .)\n"
        "  --port       listen port (default %d)\n",
        argv0,
        IO_SERVER_DEFAULT_ROOT,
        IO_SERVER_DEFAULT_PORT);
}

int
main(
    int argc,
    char** argv)
{
    static struct IoServer server;
    static struct BootManifest manifest;
    struct IOWireCache preopen;
    char const* rev_name = NULL;
    int port = IO_SERVER_DEFAULT_PORT;
    int have_preopen = 0;
    int want_ondemand = 0;

    memset(&server, 0, sizeof(server));
    memset(&preopen, 0, sizeof(preopen));
    snprintf(server.root, sizeof(server.root), "%s", IO_SERVER_DEFAULT_ROOT);
    snprintf(server.boot_root, sizeof(server.boot_root), ".");
    snprintf(server.config_dir, sizeof(server.config_dir), "config");
    snprintf(server.script_dir, sizeof(server.script_dir), "script");

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--manifest") == 0 && i + 1 < argc )
        {
            if( BootManifest_LoadFile(&manifest, argv[++i]) != 0 )
                return 1;
            preopen.epoch = manifest.cache_epoch;
            preopen.game = manifest.cache_game;
            preopen.revision = manifest.cache_revision;
            preopen.quirks = manifest.cache_quirks;
            if( snprintf(preopen.dir, sizeof(preopen.dir), "%s", manifest.cache_dir) >=
                (int)sizeof(preopen.dir) )
            {
                fprintf(
                    stderr,
                    "io_server: cache directory too long: %s\n",
                    manifest.cache_dir);
                return 1;
            }
            /* source=ondemand names no directory, and opening one would be
             * opening a cache this boot said it does not have. The wire to
             * the server replaces it; see want_ondemand below. */
            want_ondemand = manifest.cache_on_demand;
            have_preopen = !want_ondemand && preopen.dir[0] != 0;
            continue;
        }
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
        {
            rev_name = argv[++i];
            continue;
        }
        if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
        {
            port = atoi(argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--root") == 0 && i + 1 < argc )
        {
            snprintf(server.root, sizeof(server.root), "%s", argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--boot-root") == 0 && i + 1 < argc )
        {
            snprintf(server.boot_root, sizeof(server.boot_root), "%s", argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--config") == 0 && i + 1 < argc )
        {
            snprintf(server.config_dir, sizeof(server.config_dir), "%s", argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--script") == 0 && i + 1 < argc )
        {
            snprintf(server.script_dir, sizeof(server.script_dir), "%s", argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0 )
        {
            server.verbose = 1;
            continue;
        }
        if( argv[i][0] != '-' && !preopen.dir[0] )
        {
            snprintf(preopen.dir, sizeof(preopen.dir), "%s", argv[i]);
            continue;
        }
        usage(argv[0]);
        return 1;
    }

    if( rev_name )
    {
        struct RSCache named;
        if( !RSCache_ProfileByName(rev_name, &named) )
        {
            fprintf(stderr, "io_server: unknown rev \'%s\'\n", rev_name);
            return 1;
        }
        preopen.epoch = named.epoch;
        preopen.game = named.game;
        preopen.revision = named.revision;
        preopen.quirks = named.quirks;
        have_preopen = preopen.dir[0] != 0;
    }

    /*
     * Opening a cache up front is optional now: every batch names the cache it
     * is about, so the server can answer a client it knew nothing about when it
     * started. Passing --manifest is still worth it — a missing cache then
     * fails here, at startup, instead of in a browser tab later.
     */
    if( have_preopen && !io_server_cache_for(&server, &preopen) )
    {
        fprintf(stderr, "io_server: could not open %s\n", preopen.dir);
        return 1;
    }

    /*
     * --manifest named an on-demand world, so open its wire now rather than on
     * the first read. Still worth doing for the same reason the cache preopen
     * is: a server that is not started yet is named here, at the command line
     * that asked for it, rather than in a browser tab later.
     *
     * No longer a requirement, though, which is the change. A client names its
     * own manifest per request now, so a process launched with no --manifest at
     * all serves any world a client asks for — and one launched WITH it can
     * still serve others alongside. Failing to reach this one is therefore not
     * fatal any more: it is one unreachable source out of however many this
     * process will be asked for, and it is reported as that.
     */
    if( want_ondemand )
    {
        if( !io_server_ondemand_for(
                &server,
                manifest.host[0] ? manifest.host : "localhost",
                manifest.port > 0 ? manifest.port : 43594,
                manifest.ws_port > 0 ? manifest.ws_port : 80) )
        {
            fprintf(stderr,
                "io_server: --manifest names source=ondemand but that server is "
                "not serving a cache; continuing, since a client can name "
                "another manifest\n");
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    server.port = port;
    server.started = time(NULL);

    printf(
        "io_server: http://localhost:%d/  (serving %s; caches open on demand)\n"
        "io_server: http://localhost:%d/status  (status page)\n",
        port,
        server.root,
        port);
    fflush(stdout);

    if( HttpServer_Run(port, io_server_handler, &server) != 0 )
        return 1;

    printf("io_server: served=%ld failed=%ld bytes=%ld\n",
           server.served, server.failed, server.bytes_out);
    for( int i = 0; i < server.cache_count; i++ )
    {
        PlatformX_IO_Free(server.caches[i].px);
        RSCache_Dat1DiskFree(server.caches[i].dat1_disk);
        RSCache_Dat2DiskFree(server.caches[i].dat2_disk);
    }
    for( int i = 0; i < server.ondemand_count; i++ )
        PlatformX_IO_Free(server.ondemand[i].px);
    return 0;
}
