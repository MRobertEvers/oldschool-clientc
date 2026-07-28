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
 * It also serves build-web/ as static files, so `io_server --manifest X` is the
 * whole thing you need to run to open the client in a browser.
 *
 *   POST /io          an IOWire request batch -> an IOWire response batch
 *   GET  /boot/<path>  a manifest or RevConfig INI, under --boot-root
 *   GET  /...          a file under --root (default build-web/), "/" -> index.html
 *
 * Usage:
 *   src/build/io_server --manifest manifest_rs254.ini [--port 8088]
 *   src/build/io_server --rev lc254 cache.rs254_zuk
 */

#include "http_server.h"

#include "bootmanifest/bootmanifest.h"
#include "platform/io_wire.h"
#include "platform/platform_x_io.h"

#include <assert.h>
#include <rscache.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IO_SERVER_DEFAULT_PORT 8088
#define IO_SERVER_DEFAULT_ROOT "build-web"

/*
 * One open cache.
 *
 * Each carries its own PlatformX_IO, which is what makes the per-cache
 * decompressed-archive LRU inside it correct rather than a hazard: a group
 * archive cached for one cache must never answer a read against another.
 */
struct CacheSlot
{
    char dir[IOWIRE_CACHE_DIR_MAX];
    struct RSCache profile;
    struct PlatformX_IO* px;
    struct RSCache_Dat1Disk* dat1_disk;
    struct RSCache_Dat2Disk* dat2_disk;
    char describe[192];
    int failed_open; /* remember a refusal so it is reported once, not per read */
};

#define IO_SERVER_MAX_CACHES 8

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
    char root[512];
    /* Where GET /boot/<path> reads from: the tree the manifests live in, and
     * the root the caches themselves are resolved against. */
    char boot_root[512];
    char config_dir[256];
    char script_dir[256];
    int verbose;

    long served;
    long failed;
    long bytes_out;
};

/* Resolve a client-named cache directory under the server's root. The name
 * arrives from another process, so it is input: no absolute paths, no
 * traversal, no escaping the tree the server was pointed at. */
static int
cache_dir_is_safe(char const* dir)
{
    if( !dir || !dir[0] )
        return 0;
    if( dir[0] == '/' )
        return 0;
    if( strstr(dir, "..") )
        return 0;
    return 1;
}

static struct CacheSlot*
io_server_cache_for(
    struct IoServer* server,
    struct IOWireCache const* want)
{
    struct CacheSlot* slot = NULL;
    char path[1024];
    char quirks[32];

    for( int i = 0; i < server->cache_count; i++ )
    {
        if( strcmp(server->caches[i].dir, want->dir) == 0 )
            return server->caches[i].failed_open ? NULL : &server->caches[i];
    }

    if( !cache_dir_is_safe(want->dir) )
    {
        fprintf(stderr, "io_server: refusing cache directory '%s'\n", want->dir);
        return NULL;
    }
    if( server->cache_count >= IO_SERVER_MAX_CACHES )
    {
        fprintf(stderr, "io_server: no room for another cache (%d open)\n", server->cache_count);
        return NULL;
    }

    slot = &server->caches[server->cache_count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->dir, sizeof(slot->dir), "%s", want->dir);
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
        slot->dir,
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
    struct HttpResponse* res)
{
    FILE* file;
    long size;
    void* data;

    file = fopen(full, "rb");
    if( !file )
    {
        fprintf(stderr, "http: 404 %s\n", full);
        res->status = 404;
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
    if( !data || (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size) )
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
    serve_file(server, full, res);
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
    serve_file(server, full, res);
}

static void
io_server_handler(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct IoServer* server = (struct IoServer*)user;

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
    if( strcmp(req->method, "GET") == 0 )
    {
        if( strncmp(req->path, "/boot/", 6) == 0 )
        {
            handle_boot_file(server, req, res);
            return;
        }
        if( strcmp(req->path, "/stats") == 0 )
        {
            char* text = malloc(256);
            int len;
            if( !text )
            {
                res->status = 500;
                return;
            }
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
    res->status = 404;
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
        "  --manifest   read cache identity + dir from the same boot manifest the\n"
        "               native client uses (recommended: the two cannot disagree)\n"
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
            snprintf(preopen.dir, sizeof(preopen.dir), "%s", manifest.cache_dir);
            have_preopen = 1;
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

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf(
        "io_server: http://localhost:%d/  (serving %s; caches open on demand)\n",
        port,
        server.root);
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
    return 0;
}
