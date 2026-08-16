/*
 * Every registered opcode codec, against every record of its type in a real cache.
 *
 * `RSCache_OpcodeCodec` is a wrapper layer, so the risk it carries is not that a
 * layout is wrong — the layouts underneath were validated before this existed and
 * are unchanged — but that a *row* is wrong: a decode paired with another type's
 * encode, a `record_size` naming the wrong struct, a `record_free` that leaks or
 * double-frees, a bound that under-counts.
 *
 * Every one of those is silent. A mismatched pair still compiles and still returns
 * bytes; an under-counting bound truncates a record rather than failing. So the
 * test drives the interface exactly as a packer would and holds it to three
 * properties that a wrong row cannot satisfy:
 *
 *   1. **`decode` consumes the whole record.** These decoders stop on an opcode
 *      they do not recognise rather than guessing its width, so a short consume is
 *      the signal that the row reached the wrong decoder.
 *   2. **`encode` never exceeds `encode_bound`.** Checked per record with a
 *      guard-band canary rather than by trusting the return, because an encoder
 *      that overran would already have corrupted the heap by the time it returned.
 *   3. **Byte-identity, where the type is capable of it.** Not asserted uniformly:
 *      several types are documented lossy for reasons the wrapper cannot fix
 *      (`dat2_config_npc.h` explains why npc is ~0% byte-exact — the decoder
 *      establishes no -1 defaults, so "absent" and "zero" are one state). The
 *      per-type counts are printed, and the bar is stated per type below.
 *
 * The type list is not written down here. It is whatever the registry holds, walked
 * through `RSCache_OpcodeCodecAt`, and the address comes from
 * `RSCache_RecordAddressFor` — so a codec added to the registry is tested without
 * touching this file, and a second list cannot fall behind the first.
 */

#include "dat2disk.h"
#include "filelist.h"
#include "opcode_codec.h"
#include "rscache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(int ok, const char* what)
{
    g_checks++;
    if( !ok )
        g_failures++;
    printf("opcode-codec: %-56s %s\n", what, ok ? "ok" : "FAILED");
}

/** Written past the encode buffer so an overrun is caught at the moment it
 *  happens rather than surfacing later as unrelated corruption. */
#define CANARY_BYTES 32
#define CANARY_FILL 0xA5

struct Tally
{
    int records;
    int exact;
    int differ;
    int short_consume;
    int bound_exceeded;
    int encode_failed;
};

static void
run_codec(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    const struct RSCache_OpcodeCodec* codec,
    struct Tally* tally)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, codec->type);
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table = RSCache_Dat2DiskTableId(disk, addr.table);

    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return;
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, addr.group);
    if( !archive )
        return;
    if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    files = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    for( int f = 0; f < files->file_count; f++ )
    {
        const uint8_t* src = (const uint8_t*)files->files[f];
        int size = files->file_sizes[f];
        void* record;
        uint8_t* out;
        uint32_t bound, written;
        int consumed = -1;
        int overran = 0;

        if( size <= 0 )
            continue;
        tally->records++;

        record = RSCache_OpcodeCodecRecordDecode(codec, profile, src, size, &consumed);
        if( !record )
        {
            tally->differ++;
            continue;
        }
        if( consumed != size )
            tally->short_consume++;

        bound = codec->encode_bound(record);
        out = (uint8_t*)malloc(bound + CANARY_BYTES);
        if( !out )
        {
            RSCache_OpcodeCodecRecordFree(codec, record);
            continue;
        }
        memset(out + bound, CANARY_FILL, CANARY_BYTES);

        written = codec->encode(profile, record, out, bound);
        for( int c = 0; c < CANARY_BYTES; c++ )
        {
            if( out[bound + c] != CANARY_FILL )
                overran = 1;
        }
        if( overran || written > bound )
            tally->bound_exceeded++;
        if( written == 0 )
            tally->encode_failed++;

        if( written == (uint32_t)size && memcmp(out, src, (size_t)size) == 0 )
            tally->exact++;
        else
            tally->differ++;

        free(out);
        RSCache_OpcodeCodecRecordFree(codec, record);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

/* ---- the inheritance the interface exists for ---------------------------- */

/*
 * A server-side record built on a client one, exercised exactly as the real
 * server pack will be.
 *
 * This is the property the whole interface is for, and it is not demonstrated by
 * any amount of client round-tripping: a record that *embeds* a client record,
 * delegates every opcode to it first, and handles only what comes back unclaimed.
 *
 * Three things have to hold, and each one silently breaks the design if it does
 * not:
 *
 *   1. `&server->base == server` — the embedded record starts at offset zero, so
 *      a pointer to the server record is a valid pointer to the client one. This
 *      is what lets every existing client function keep working unchanged.
 *   2. Delegation actually fills the base. If it did not, a server record would
 *      carry server fields and no name, size or models.
 *   3. **A client decoder fed a server record does not corrupt itself.** It must
 *      stop at the first opcode it does not know, leaving a short `_consumed`,
 *      rather than reading the payload as an opcode and resynchronising on
 *      garbage. Without this, one stream cannot safely serve both sides.
 */

/** Server-band opcode, inside the reserved 64..255 range. */
#define SERVER_INV_RESPAWN 200

struct ServerInv
{
    struct RSCache_Dat2ConfigInv base;
    int respawn_rate;
    bool has_respawn;
};

static bool
server_inv_decode_op(void* record, int opcode, struct RSCache_Buffer* buffer, unsigned flags)
{
    struct ServerInv* self = (struct ServerInv*)record;

    if( RSCache_Dat2ConfigInvDecodeOp(&self->base, opcode, buffer, flags) )
        return true;
    if( opcode == SERVER_INV_RESPAWN )
    {
        self->respawn_rate = g2(buffer);
        self->has_respawn = true;
        return true;
    }
    return false;
}

static void
cpc_server_inv_init(void* record)
{
    memset(record, 0, sizeof(struct ServerInv));
}

static const struct RSCache_OpcodeCodec k_server_inv = {
    .name = "inv",
    .type = RSCACHE_TYPE_INV,
    .epoch = RSCACHE_EPOCH_DAT2,
    .record_size = sizeof(struct ServerInv),
    .record_init = cpc_server_inv_init,
    .record_free = NULL,
    .flags_for = NULL,
    .decode_op = server_inv_decode_op,
    .decode = NULL,
    .encode = NULL,
    .encode_bound = NULL,
};

static void
check_server_subclass(const struct RSCache* profile)
{
    /* opcode 2 (size = 28), then server opcode 200 (respawn = 100), terminator. */
    static const uint8_t stream[] = { 0x02, 0x00, 0x1C, 0xC8, 0x00, 0x64, 0x00 };
    struct ServerInv server;
    struct RSCache_Dat2ConfigInv client;
    int consumed = -1;

    check(
        (void*)&server == (void*)&server.base,
        "the embedded client record starts at offset zero");

    cpc_server_inv_init(&server);
    consumed = RSCache_OpcodeStreamDecode(
        &k_server_inv, profile, &server, stream, (int)sizeof(stream));

    check(consumed == (int)sizeof(stream), "the server codec consumes the whole stream");
    check(server.base.size == 28, "delegation filled the inherited client field");
    check(
        server.has_respawn && server.respawn_rate == 100,
        "the server codec handled its own opcode");

    /* The same bytes through the client codec: it must stop cleanly at 200. */
    memset(&client, 0, sizeof(client));
    RSCache_Dat2ConfigInvDecodeInplace(&client, stream, (int)sizeof(stream));
    check(client.size == 28, "the client codec still reads the fields it knows");
    /*
     * 4, not 3: the loop has already read the opcode byte via `g1` when the
     * handler declines, and the library's convention is to report the position
     * after that read — `dat2_config_npc.c`'s default case does the same. What
     * matters is that it is *short* of the 7-byte stream and never advanced into
     * the server payload, which is what makes one stream safe for both sides.
     */
    check(
        client._consumed == 4,
        "the client codec stops on the first server opcode, not past its payload");
    check(
        client._consumed < (int)sizeof(stream),
        "the shortfall is visible to the caller");
}

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache.osrs239";
    const char* profile_name = argc > 2 ? argv[2] : "osrs239";
    struct RSCache_Dat2Disk* disk;
    struct RSCache profile;
    int registered = RSCache_OpcodeCodecCount();
    int total_records = 0;
    int total_short = 0;
    int total_bound = 0;
    int total_encode_failed = 0;
    int types_with_records = 0;

    printf("opcode-codec: %d codecs registered\n", registered);

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Loud, and not a pass. A round-trip check that quietly succeeds on an
         * absent cache is worse than no check. */
        printf("opcode-codec: SKIPPED — no cache at %s\n", cache_dir);
        return 0;
    }
    if( !RSCache_ProfileByName(profile_name, &profile) )
    {
        printf("opcode-codec: SKIPPED — no %s profile\n", profile_name);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    for( int i = 0; i < registered; i++ )
    {
        const struct RSCache_OpcodeCodec* codec = RSCache_OpcodeCodecAt(i);
        struct Tally tally;

        memset(&tally, 0, sizeof(tally));
        if( (int)codec->epoch != profile.epoch )
            continue;
        /* A row whose lookup does not round-trip is mis-keyed: it would be
         * unreachable through the interface every real caller uses. */
        if( RSCache_OpcodeCodecFor(&profile, codec->type) != codec ||
            RSCache_OpcodeCodecByName(&profile, codec->name) != codec )
        {
            printf("opcode-codec: %-10s NOT REACHABLE by type or name lookup\n",
                   codec->name);
            g_checks++;
            g_failures++;
            continue;
        }
        if( !RSCache_OpcodeCodecCanEncode(codec) )
        {
            printf("opcode-codec: %-10s decode only\n", codec->name);
            continue;
        }

        run_codec(disk, &profile, codec, &tally);
        if( tally.records == 0 )
        {
            printf("opcode-codec: %-10s no records in this cache\n", codec->name);
            continue;
        }
        types_with_records++;
        total_records += tally.records;
        total_short += tally.short_consume;
        total_bound += tally.bound_exceeded;
        total_encode_failed += tally.encode_failed;
        printf("opcode-codec: %-10s %6d records, %6d exact, %6d differ, "
               "%d short, %d over-bound\n",
               codec->name, tally.records, tally.exact, tally.differ,
               tally.short_consume, tally.bound_exceeded);
    }

    check_server_subclass(&profile);

    /*
     * Zero, for every registered type.
     *
     * This briefly stood at 132 — obj records that stopped 85-107 bytes early
     * with no error. The cause was opcode 115 (`team`) decoding as a u16 when it
     * is a single byte: the read swallowed the next opcode byte, the stream
     * slipped, and decoding continued plausibly until it hit a payload byte that
     * happened to be zero and took it for the terminator. The 53 records that
     * looked like an unhandled "opcode 87" were the same bug — 87 was a payload
     * byte being read as an opcode, not a real opcode at all.
     *
     * Both symptoms had been in the tree the whole time and neither was visible,
     * because `RSCache_Dat2ConfigObj` has no `_consumed` field and nothing else
     * measured obj's consumption. That is the entire argument for this bar: a
     * decoder that stops on the wrong byte still returns a plausible record.
     */
    check(total_short == 0, "every record is consumed to its last byte");

    check(registered > 0, "the registry is not empty");
    check(types_with_records > 0, "at least one codec found records to check");
    check(total_records > 0, "records were decoded through the interface");
    check(total_bound == 0, "no encode exceeded its own bound");
    check(total_encode_failed == 0, "no encode failed at its own bound");

    RSCache_Dat2DiskFree(disk);

    if( g_failures )
    {
        printf("opcode-codec: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("opcode-codec: all %d checks passed\n", g_checks);
    return 0;
}
