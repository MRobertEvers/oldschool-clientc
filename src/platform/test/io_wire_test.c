/*
 * The IO wire has to be transparent: an item that crossed it must be
 * indistinguishable from one the native backend filled in locally, or the web
 * build decodes subtly different archives from the desktop build and every
 * downstream difference gets blamed on something else.
 *
 * So this test does not check the encoding against a golden blob. It loads the
 * same request twice — once directly, once through encode/decode on both sides
 * — and compares the two items field for field and byte for byte. Anything the
 * codec drops or reshapes shows up as a mismatch on the archive it dropped it
 * from.
 *
 *   make -C src test-io-wire [DAT1_CACHE=../cache254] [DAT2_CACHE=../cache.osrs230]
 */

#include "platform/io_wire.h"
#include "platform/platform_x_io.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while( 0 )

/*
 * One request, both ways.
 *
 * `spec` is loaded directly into `direct`, and separately encoded as a request,
 * decoded (as the server would), loaded, encoded as a response, decoded and
 * applied (as the browser would) into `round`. The two are then compared.
 */
static void
check_round_trip(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem const* spec,
    char const* label)
{
    struct ToriRS_IOItem direct = *spec;
    struct ToriRS_IOItem server_item;
    struct ToriRS_IOItem round;
    struct IOWireBuf request;
    struct IOWireBuf response;
    struct IOWireReader reader;
    struct IOWireRequest wire_req;
    struct IOWireResponse wire_resp;
    struct IOWireCache cache_out;
    /* A batch says which cache it is about; the server opens that one. Both
     * the identity and the directory have to survive the round trip, or a
     * client booting one generation gets answers from another's cache. */
    struct IOWireCache const cache_in = {
        .epoch = 1, .game = 2, .revision = 254, .quirks = 0x5u, .dir = "cache.rs254_zuk"
    };
    int count;

    PlatformX_IO_LoadItem(px, &direct);

    /* --- client -> server ------------------------------------------------ */
    IOWireBuf_Init(&request);
    IOWire_BatchBegin(&request, &cache_in);
    IOWire_WriteRequest(&request, 4242, spec);
    CHECK(!request.error, "%s: request encode ran out of memory", label);

    IOWireReader_Init(&reader, request.data, request.len);
    count = IOWire_BatchRead(&reader, &cache_out);
    CHECK(cache_out.epoch == cache_in.epoch, "%s: cache epoch %d, expected %d", label,
          cache_out.epoch, cache_in.epoch);
    CHECK(cache_out.game == cache_in.game, "%s: cache game %d", label, cache_out.game);
    CHECK(cache_out.revision == cache_in.revision, "%s: cache revision %d", label,
          cache_out.revision);
    CHECK(cache_out.quirks == cache_in.quirks, "%s: cache quirks %u", label,
          (unsigned)cache_out.quirks);
    CHECK(strcmp(cache_out.dir, cache_in.dir) == 0, "%s: cache dir '%s', expected '%s'",
          label, cache_out.dir, cache_in.dir);
    CHECK(count == 1, "%s: request batch count %d, expected 1", label, count);
    CHECK(IOWire_ReadRequest(&reader, &wire_req) == 0, "%s: request decode failed", label);
    CHECK(wire_req.req_id == 4242, "%s: req_id %u, expected 4242", label, wire_req.req_id);
    CHECK(reader.off == reader.len, "%s: request record left %d bytes unread", label,
          reader.len - reader.off);

    IOWire_RequestToItem(&wire_req, &server_item);
    CHECK(server_item.kind == spec->kind, "%s: kind %d, expected %d", label,
          (int)server_item.kind, (int)spec->kind);
    if( spec->kind == TORIRS_IOK_CACHE )
    {
        CHECK(server_item.u.cache.table_id == spec->u.cache.table_id,
              "%s: table %d, expected %d", label, server_item.u.cache.table_id,
              spec->u.cache.table_id);
        CHECK(server_item.u.cache.archive_id == spec->u.cache.archive_id,
              "%s: archive %d, expected %d", label, server_item.u.cache.archive_id,
              spec->u.cache.archive_id);
        CHECK(server_item.u.cache.flags == spec->u.cache.flags, "%s: flags %d, expected %d",
              label, server_item.u.cache.flags, spec->u.cache.flags);
    }
    if( spec->kind == TORIRS_IOK_CONFIG_FILE )
        CHECK(strcmp(server_item.u.config_file.path, spec->u.config_file.path) == 0,
              "%s: path '%s', expected '%s'", label, server_item.u.config_file.path,
              spec->u.config_file.path);

    /* --- server -> client ------------------------------------------------ */
    PlatformX_IO_LoadItem(px, &server_item);

    IOWireBuf_Init(&response);
    IOWire_BatchBegin(&response, NULL);
    IOWire_WriteResponse(&response, wire_req.req_id, &server_item);
    CHECK(!response.error, "%s: response encode ran out of memory", label);

    IOWireReader_Init(&reader, response.data, response.len);
    count = IOWire_BatchRead(&reader, NULL);
    CHECK(count == 1, "%s: response batch count %d, expected 1", label, count);
    CHECK(IOWire_ReadResponse(&reader, &wire_resp) == 0, "%s: response decode failed", label);
    CHECK(reader.off == reader.len, "%s: response record left %d bytes unread", label,
          reader.len - reader.off);
    CHECK(wire_resp.req_id == 4242, "%s: response req_id %u", label, wire_resp.req_id);

    memset(&round, 0, sizeof(round));
    round.kind = spec->kind;
    round.u = spec->u;
    IOWire_ApplyResponse(&wire_resp, &round);

    /* --- the actual claim ------------------------------------------------ */
    CHECK(
        (direct.error_code == 0) == (round.error_code == 0),
        "%s: direct error %d vs round-trip error %d", label, direct.error_code,
        round.error_code);

    if( direct.error_code == 0 && round.error_code == 0 )
    {
        if( spec->kind == TORIRS_IOK_CACHE &&
            spec->u.cache.flags != TORIRS_IO_CACHE_DAT2 )
        {
            struct RSCache_Dat1DiskArchive* a = direct.data;
            struct RSCache_Dat1DiskArchive* b = round.data;
            CHECK(a && b, "%s: missing dat1 archive", label);
            if( a && b )
            {
                CHECK(a->table_id == b->table_id, "%s: table_id %d vs %d", label, a->table_id,
                      b->table_id);
                CHECK(a->archive_id == b->archive_id, "%s: archive_id %d vs %d", label,
                      a->archive_id, b->archive_id);
                CHECK(a->revision == b->revision, "%s: revision %d vs %d", label, a->revision,
                      b->revision);
                CHECK(a->file_count == b->file_count, "%s: file_count %d vs %d", label,
                      a->file_count, b->file_count);
                CHECK(a->format == b->format, "%s: format %d vs %d", label, (int)a->format,
                      (int)b->format);
                CHECK(a->data_size == b->data_size, "%s: data_size %d vs %d", label,
                      a->data_size, b->data_size);
                if( a->data_size == b->data_size && a->data_size > 0 )
                    CHECK(memcmp(a->data, b->data, (size_t)a->data_size) == 0,
                          "%s: %d payload bytes differ", label, a->data_size);
            }
        }
        else if( spec->kind == TORIRS_IOK_CACHE )
        {
            struct RSCache_Dat2DiskArchive* a = direct.data;
            struct RSCache_Dat2DiskArchive* b = round.data;
            CHECK(a && b, "%s: missing dat2 archive", label);
            if( a && b )
            {
                CHECK(a->table_id == b->table_id, "%s: table_id %d vs %d", label, a->table_id,
                      b->table_id);
                CHECK(a->archive_id == b->archive_id, "%s: archive_id %d vs %d", label,
                      a->archive_id, b->archive_id);
                CHECK(a->revision == b->revision, "%s: revision %d vs %d", label, a->revision,
                      b->revision);
                CHECK(a->file_count == b->file_count, "%s: file_count %d vs %d", label,
                      a->file_count, b->file_count);
                CHECK(a->data_size == b->data_size, "%s: data_size %d vs %d", label,
                      a->data_size, b->data_size);
                if( a->data_size == b->data_size && a->data_size > 0 )
                    CHECK(memcmp(a->data, b->data, (size_t)a->data_size) == 0,
                          "%s: %d payload bytes differ", label, a->data_size);
                /* file_ids is what maps a group member to its offset; losing it
                 * silently turns every group read into member 0. */
                CHECK((a->file_ids != NULL) == (b->file_ids != NULL),
                      "%s: file_ids present %d vs %d", label, a->file_ids != NULL,
                      b->file_ids != NULL);
                if( a->file_ids && b->file_ids && a->file_count == b->file_count )
                    CHECK(memcmp(a->file_ids, b->file_ids,
                                 (size_t)a->file_count * sizeof(int)) == 0,
                          "%s: %d file ids differ", label, a->file_count);
            }
        }
        else if( spec->kind == TORIRS_IOK_CONFIG_FILE || spec->kind == TORIRS_IOK_SCRIPT )
        {
            CHECK(direct.data_size == round.data_size, "%s: size %d vs %d", label,
                  direct.data_size, round.data_size);
            if( direct.data_size == round.data_size && direct.data_size > 0 )
                CHECK(memcmp(direct.data, round.data, (size_t)direct.data_size) == 0,
                      "%s: file bytes differ", label);
        }
    }

    printf(
        "  %-34s %s (%d bytes on the wire)\n",
        label,
        direct.error_code == 0 ? "ok" : "absent (both sides agree)",
        response.len);

    IOWire_FreeLoadedItem(&direct);
    IOWire_FreeLoadedItem(&server_item);
    /* `round` holds what the client built; free it the same way. */
    IOWire_FreeLoadedItem(&round);
    IOWireBuf_Free(&request);
    IOWireBuf_Free(&response);
}

static struct ToriRS_IOItem
cache_item(
    int table_id,
    int archive_id,
    int flags)
{
    struct ToriRS_IOItem item;
    memset(&item, 0, sizeof(item));
    item.kind = TORIRS_IOK_CACHE;
    item.u.cache.table_id = table_id;
    item.u.cache.archive_id = archive_id;
    item.u.cache.flags = flags;
    return item;
}

/* A malformed batch must be refused, not walked off the end of. */
static void
check_malformed(void)
{
    struct IOWireReader reader;
    struct IOWireResponse resp;
    uint8_t good[64];
    struct IOWireBuf buf;
    struct ToriRS_IOItem item = cache_item(1, 0, TORIRS_IO_CACHE_DAT1);

    IOWireReader_Init(&reader, "nope", 4);
    CHECK(IOWire_BatchRead(&reader, NULL) < 0, "bad magic accepted");

    IOWireReader_Init(&reader, "TRIO\x09\x00\x00\x00\x01\x00\x00\x00", 12);
    CHECK(IOWire_BatchRead(&reader, NULL) < 0, "wrong version accepted");

    IOWireBuf_Init(&buf);
    IOWire_BatchBegin(&buf, NULL);
    IOWire_WriteResponse(&buf, 7, &item);
    CHECK(buf.len > 12, "response record not written");
    memcpy(good, buf.data, (size_t)(buf.len < 64 ? buf.len : 64));

    /* Truncated one byte short of complete: the reader must say so rather than
     * hand back a record with a blob pointer past the end of the buffer. */
    IOWireReader_Init(&reader, buf.data, buf.len - 1);
    CHECK(IOWire_BatchRead(&reader, NULL) == 1, "truncated batch header rejected too early");
    CHECK(IOWire_ReadResponse(&reader, &resp) != 0, "truncated record accepted");
    IOWireBuf_Free(&buf);
}

/* A settings save always replaces an existing file after first launch. This
 * specifically exercises the Windows path, where CRT rename() refuses to
 * replace the destination and used to make every later preferences save fail. */
static void
check_file_replace(struct PlatformX_IO* px)
{
    char const* path = "build/io_replace_test.ini";
    char const replacement[] = "version=2\n";
    char actual[sizeof(replacement)] = { 0 };
    struct ToriRS_IOItem item;
    FILE* file;

    file = fopen(path, "wb");
    CHECK(file != NULL, "file replace: could not create initial file");
    if( !file )
        return;
    fwrite("version=1\n", 1, 10, file);
    fclose(file);

    memset(&item, 0, sizeof(item));
    item.kind = TORIRS_IOK_FILE_WRITE;
    snprintf(item.u.file.path, sizeof(item.u.file.path), "%s", path);
    item.data = (void*)replacement;
    item.data_size = (int)(sizeof(replacement) - 1);
    CHECK(PlatformX_IO_LoadItem(px, &item) == 0, "file replace: platform write failed");
    CHECK(item.error_code == 0, "file replace: error code %d", item.error_code);

    file = fopen(path, "rb");
    CHECK(file != NULL, "file replace: replacement is missing");
    if( file )
    {
        size_t got = fread(actual, 1, sizeof(actual) - 1, file);
        fclose(file);
        CHECK(got == sizeof(replacement) - 1, "file replace: got %d bytes", (int)got);
        CHECK(strcmp(actual, replacement) == 0, "file replace: contents are '%s'", actual);
    }
    remove(path);
}

int
main(
    int argc,
    char** argv)
{
    char const* dat1_dir = argc > 1 ? argv[1] : "../cache254";
    char const* dat2_dir = argc > 2 && argv[2][0] ? argv[2] : NULL;
    char const* dat2_rev = argc > 3 && argv[3][0] ? argv[3] : "osrs230";
    struct PlatformX_IO* px;
    struct RSCache_Dat1Disk* dat1;

    printf("io_wire: framing\n");
    check_malformed();

    px = PlatformX_IO_New();
    assert(px);

    printf("io_wire: client file replacement\n");
    check_file_replace(px);

    dat1 = RSCache_Dat1DiskNewFromDirectory(dat1_dir);
    if( !dat1 )
    {
        fprintf(stderr, "io_wire_test: no dat1 cache at %s\n", dat1_dir);
        return 1;
    }
    PlatformX_IO_InitDat1Disk(px, dat1);
    PlatformX_IO_InitConfigPath(px, ".");
    PlatformX_IO_InitScriptPath(px, ".");

    printf("io_wire: dat1 archives (%s)\n", dat1_dir);
    {
        struct ToriRS_IOItem item;

        /* Archive 2 is the `config` jag archive; 0 is unused in idx0. */
        item = cache_item(RSCACHE_DAT1_DISK_TABLE_CONFIGS, 2, TORIRS_IO_CACHE_DAT1);
        check_round_trip(px, &item, "dat1 config archive");

        item = cache_item(RSCACHE_DAT1_DISK_TABLE_ANIMATIONS, 0, TORIRS_IO_CACHE_DAT1);
        check_round_trip(px, &item, "dat1 animations");

        item = cache_item(RSCACHE_DAT1_DISK_TABLE_MODELS, 0, TORIRS_IO_CACHE_DAT1);
        check_round_trip(px, &item, "dat1 model 0");

        /* Map squares resolve through the versionlist on the server side; the
         * client only ever sends the square id, so this is the request shape
         * whose meaning is furthest from its wire form. */
        item = cache_item(
            RSCACHE_DAT1_DISK_TABLE_MAPS, RSCache_MapSquareId(50, 50),
            TORIRS_IO_CACHE_DAT1_MAP_TERRAIN);
        check_round_trip(px, &item, "dat1 map terrain 50,50");
        item = cache_item(
            RSCACHE_DAT1_DISK_TABLE_MAPS, RSCache_MapSquareId(50, 50),
            TORIRS_IO_CACHE_DAT1_MAP_SCENERY);
        check_round_trip(px, &item, "dat1 map scenery 50,50");
        /* A square this cache does not ship: legitimate at the edge of the
         * built world, and the wire must carry the refusal rather than an
         * empty archive that decodes into blank terrain. */
        item = cache_item(
            RSCACHE_DAT1_DISK_TABLE_MAPS, RSCache_MapSquareId(30, 30),
            TORIRS_IO_CACHE_DAT1_MAP_TERRAIN);
        check_round_trip(px, &item, "dat1 map terrain absent");

        /* A table this cache refuses: both sides must agree it failed. */
        item = cache_item(RSCACHE_DAT1_DISK_TABLE_SOUNDS, 0, TORIRS_IO_CACHE_DAT1);
        check_round_trip(px, &item, "dat1 refused table");
    }

    printf("io_wire: file reads\n");
    {
        struct ToriRS_IOItem item;
        FILE* file = fopen("build/io_wire_test_fixture.bin", "wb");
        if( file )
        {
            for( int i = 0; i < 1000; i++ )
                fputc(i & 0xFF, file);
            fclose(file);
        }
        memset(&item, 0, sizeof(item));
        item.kind = TORIRS_IOK_CONFIG_FILE;
        snprintf(item.u.config_file.path, sizeof(item.u.config_file.path),
                 "build/io_wire_test_fixture.bin");
        check_round_trip(px, &item, "config file");

        memset(&item, 0, sizeof(item));
        item.kind = TORIRS_IOK_CONFIG_FILE;
        snprintf(item.u.config_file.path, sizeof(item.u.config_file.path),
                 "build/definitely_absent.bin");
        check_round_trip(px, &item, "config file absent");
    }

    if( dat2_dir )
    {
        struct RSCache_Dat2Disk* dat2 = RSCache_Dat2DiskNewFromDirectory(dat2_dir);
        struct RSCache profile;
        if( !dat2 )
        {
            fprintf(stderr, "io_wire_test: no dat2 cache at %s\n", dat2_dir);
            return 1;
        }
        /* Without the identity every logical table resolves to ABSENT, and the
         * round trip would "pass" by agreeing that nothing exists. */
        if( !RSCache_ProfileByName(dat2_rev, &profile) )
        {
            fprintf(stderr, "io_wire_test: unknown rev '%s'\n", dat2_rev);
            return 1;
        }
        RSCache_Dat2DiskSetProfile(dat2, &profile);
        PlatformX_IO_InitDat2Disk(px, dat2);
        printf("io_wire: dat2 archives (%s, rev %s)\n", dat2_dir, dat2_rev);
        {
            struct ToriRS_IOItem item;
            /* A group archive: this is the one response shape carrying an aux
             * array (file_ids), and losing it turns every member read into
             * member 0. */
            item = cache_item(RSCACHE_DAT2_TABLE_CONFIGS, 10, TORIRS_IO_CACHE_DAT2);
            check_round_trip(px, &item, "dat2 config group 10");
            item = cache_item(RSCACHE_DAT2_TABLE_INTERFACES, 161, TORIRS_IO_CACHE_DAT2);
            check_round_trip(px, &item, "dat2 interface 161");
            item = cache_item(RSCACHE_DAT2_TABLE_MODELS, 0, TORIRS_IO_CACHE_DAT2);
            check_round_trip(px, &item, "dat2 model 0");
        }
        RSCache_Dat2DiskFree(dat2);
    }

    PlatformX_IO_Free(px);
    RSCache_Dat1DiskFree(dat1);

    printf("io_wire: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
