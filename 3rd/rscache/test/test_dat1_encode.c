/*
 * dat1 config encoders, against every record in a real dat1 cache.
 *
 * Four dat1 types shipped decode-only — obj, idk, spotanim and component — so a
 * dat1 tree could be read and never written back. This measures the encoders as
 * they land, one type at a time, to the same bar the dat2 side uses: **byte
 * identity against the source archive**, not a semantic round trip.
 *
 * The bar matters more here than usual because of how dat1 configs are laid out.
 * A dat2 record is its own FileList member with its own length, so a decoder that
 * misjudges a field is contained. A dat1 config archive is *one concatenated
 * stream* — `<count:u16>` then every record back to back, each ending in opcode 0
 * — so a single wrong width shifts every following record. Byte identity over the
 * whole stream is the only check that catches that; comparing structs would pass
 * happily on a stream that no client could read.
 *
 * Reaching the records at all takes three steps, none of which the dat2 harness
 * needs: open the dat1 disk, load table 0 archive 2 (`config.jag`), and pull the
 * `<type>.dat` member out of the jagfile by name.
 */

#include "dat1disk.h"
#include "datatypes/dat1_config_idk.h"
#include "datatypes/dat1_config_obj.h"
#include "datatypes/dat1_config_spotanim.h"
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
    printf("dat1-encode: %-56s %s\n", what, ok ? "ok" : "FAILED");
}

/**
 * Re-encode a whole idk archive and compare it to the source bytes.
 *
 * Whole-archive rather than per-record because the archive *is* the unit: the
 * count header and the record boundaries are only correct together.
 */
static void
run_idk(struct RSCache_FileListDat* jag)
{
    int dat = RSCache_FileListDatFindFileByName(jag, "idk.dat");
    struct RSCache_Dat1ConfigIdkList* list;
    const uint8_t* src;
    int size;
    uint8_t* out;
    uint32_t cap, written;
    int exact = 0, differ = 0;

    if( dat < 0 )
    {
        printf("dat1-encode: idk       no idk.dat in this cache\n");
        return;
    }
    src = (const uint8_t*)jag->files[dat];
    size = jag->file_sizes[dat];
    list = RSCache_Dat1ConfigIdkListNewDecode(jag->files[dat], size);
    if( !list )
    {
        check(0, "the idk archive decodes");
        return;
    }

    /* Generous: the header plus every record's own bound. */
    cap = 8u;
    for( int i = 0; i < list->idks_count; i++ )
        cap += RSCache_Dat1ConfigIdkEncodeBound(list->idks[i]);
    out = (uint8_t*)malloc(cap);
    if( !out )
    {
        check(0, "encode buffer allocates");
        return;
    }

    /* `<count:u16>` then the records back to back, exactly as the decoder reads
     * them. Written by hand rather than through a list encoder because the count
     * header is the only part that is not per record. */
    out[0] = (uint8_t)((list->idks_count >> 8) & 0xFF);
    out[1] = (uint8_t)(list->idks_count & 0xFF);
    written = 2;
    for( int i = 0; i < list->idks_count; i++ )
    {
        uint32_t one =
            RSCache_Dat1ConfigIdkEncode(list->idks[i], out + written, cap - written);

        if( one == 0 )
        {
            differ++;
            break;
        }
        written += one;
    }

    if( written == (uint32_t)size && memcmp(out, src, (size_t)size) == 0 )
        exact = 1;
    else if( written == (uint32_t)size )
    {
        for( int b = 0; b < size; b++ )
        {
            if( out[b] != src[b] )
            {
                printf("dat1-encode:   first diff at byte %d: src %02x, out %02x\n", b,
                       src[b], out[b]);
                printf("dat1-encode:   src:");
                for( int k = b - 4 < 0 ? 0 : b - 4; k < b + 8 && k < size; k++ )
                    printf(" %02x", src[k]);
                printf("\ndat1-encode:   out:");
                for( int k = b - 4 < 0 ? 0 : b - 4; k < b + 8 && k < size; k++ )
                    printf(" %02x", out[k]);
                printf("\n");
                break;
            }
        }
    }

    printf(
        "dat1-encode: idk       %d records, %u bytes written of %d source%s\n",
        list->idks_count, written, size, exact ? " — byte identical" : "");
    check(list->idks_count > 0, "the cache has idk records to check");
    check(exact, "the idk archive re-encodes to its own bytes");

    free(out);
}

/*
 * obj differs from idk in how its archive is laid out: the records live in
 * `obj.dat` but their *boundaries* come from `obj.idx`, a parallel table of
 * per-record lengths. So this re-encodes record by record and compares each
 * against its own slice, which also localises a failure to one id.
 */
static void
run_obj(struct RSCache_FileListDat* jag)
{
    int idx = RSCache_FileListDatFindFileByName(jag, "obj.idx");
    int dat = RSCache_FileListDatFindFileByName(jag, "obj.dat");
    struct RSCache_Dat1ConfigObjList* list;
    const uint8_t* idxp;
    const uint8_t* datp;
    int exact = 0, differ = 0, first_bad = -1;
    int differ_with_dup = 0;
    uint32_t offset = 2; /* past the u16 count */

    if( idx < 0 || dat < 0 )
    {
        printf("dat1-encode: obj       no obj.idx/obj.dat in this cache\n");
        return;
    }
    idxp = (const uint8_t*)jag->files[idx];
    datp = (const uint8_t*)jag->files[dat];
    list = RSCache_Dat1ConfigObjListNewDecode(
        jag->files[idx], jag->file_sizes[idx], jag->files[dat], jag->file_sizes[dat]);
    if( !list )
    {
        check(0, "the obj archive decodes");
        return;
    }

    for( int i = 0; i < list->objs_count; i++ )
    {
        int len = (idxp[2 + i * 2] << 8) | idxp[2 + i * 2 + 1];
        uint32_t bound = RSCache_Dat1ConfigObjEncodeBound(list->objs[i]);
        uint8_t* out = (uint8_t*)malloc(bound);
        uint32_t written;
        int dup = 0;

        if( !out )
            break;
        written = RSCache_Dat1ConfigObjEncode(list->objs[i], out, bound);
        if( written == (uint32_t)len && memcmp(out, datp + offset, (size_t)len) == 0 )
            exact++;
        else
        {
            if( differ == 0 )
            {
                first_bad = i;
                printf("dat1-encode:   obj %d: src(%d):", i, len);
                for( int b = 0; b < len && b < 32; b++ )
                    printf(" %02x", datp[offset + b]);
                printf("\ndat1-encode:   obj %d: out(%u):", i, written);
                for( int b = 0; b < (int)written && b < 32; b++ )
                    printf(" %02x", out[b]);
                printf("\n");
            }
            differ++;
            /*
             * Was this record's shortfall caused by a repeated opcode? The struct
             * holds one value per field, so a record that states an opcode twice
             * keeps only the last and the earlier value is gone. That is a
             * property of the source data, not of this encoder — but it is only a
             * fair excuse if it accounts for *every* differing record, so it is
             * counted rather than asserted.
             */
            for( int a = 0; a < list->objs[i]->opcode_count && !dup; a++ )
                for( int b2 = a + 1; b2 < list->objs[i]->opcode_count; b2++ )
                    if( list->objs[i]->opcodes[a] == list->objs[i]->opcodes[b2] )
                    {
                        dup = 1;
                        break;
                    }
            if( dup )
                differ_with_dup++;
        }
        offset += (uint32_t)len;
        free(out);
    }

    printf("dat1-encode: obj       %d records, %d exact, %d differ\n", list->objs_count,
           exact, differ);
    if( differ )
        printf("dat1-encode:   first differing obj id %d\n", first_bad);
    if( differ )
        printf("dat1-encode:   of those, %d state an opcode more than once\n",
               differ_with_dup);
    check(list->objs_count > 0, "the cache has obj records to check");
    /*
     * Not `differ == 0`. Five records in cache254 state an opcode twice — obj 25
     * carries opcode 5 as both 0x0154 and 0x0194 — and a struct with one field per
     * opcode cannot hold both. The bar is that a repeated opcode explains *every*
     * difference; anything else is an encoder defect.
     */
    check(
        differ == differ_with_dup,
        "every differing obj record is one that repeats an opcode");
}

/*
 * spotanim is laid out like idk: `<count:u16>` then the records back to back in
 * one stream, so the whole archive is the unit of comparison.
 */
static void
run_spotanim(struct RSCache_FileListDat* jag)
{
    int dat = RSCache_FileListDatFindFileByName(jag, "spotanim.dat");
    struct RSCache_Dat1ConfigSpotanimList* list;
    const uint8_t* src;
    int size;
    uint8_t* out;
    uint32_t cap, written = 2;
    int exact = 0;

    if( dat < 0 )
    {
        printf("dat1-encode: spotanim  no spotanim.dat in this cache\n");
        return;
    }
    src = (const uint8_t*)jag->files[dat];
    size = jag->file_sizes[dat];
    list = RSCache_Dat1ConfigSpotanimListNewDecode(jag->files[dat], size);
    if( !list )
    {
        check(0, "the spotanim archive decodes");
        return;
    }

    cap = 8u;
    for( int i = 0; i < list->spotanims_count; i++ )
        cap += RSCache_Dat1ConfigSpotanimEncodeBound(&list->spotanims[i]);
    out = (uint8_t*)malloc(cap);
    if( !out )
        return;

    out[0] = (uint8_t)((list->spotanims_count >> 8) & 0xFF);
    out[1] = (uint8_t)(list->spotanims_count & 0xFF);
    for( int i = 0; i < list->spotanims_count; i++ )
    {
        uint32_t one = RSCache_Dat1ConfigSpotanimEncode(
            &list->spotanims[i], out + written, cap - written);

        if( one == 0 )
            break;
        written += one;
    }
    if( written == (uint32_t)size && memcmp(out, src, (size_t)size) == 0 )
        exact = 1;

    printf("dat1-encode: spotanim  %d records, %u bytes written of %d source%s\n",
           list->spotanims_count, written, size, exact ? " — byte identical" : "");
    check(list->spotanims_count > 0, "the cache has spotanim records to check");
    check(exact, "the spotanim archive re-encodes to its own bytes");
    free(out);
}

/* ---- the same records, through the opcode-codec registry ------------------ */

/*
 * `test_opcode_codec` walks the registry, but it can only reach dat2 rows: it
 * addresses records through `RSCache_RecordAddressFor` and a `.dat2` disk, and a
 * dat1 config record has neither a table nor a group. So the dat1 rows would sit
 * in the table registered and never once driven, which is the state this file
 * exists to prevent for the encoders themselves.
 *
 * What a wrong row looks like here is the same as there and just as silent: a
 * decode paired with another type's encode still returns bytes, a `record_size`
 * naming the wrong struct still allocates, a `record_init` that zeroes instead of
 * seeding still decodes without error. Three properties catch all of them, and
 * none of them needs a fidelity number written down:
 *
 *   1. **Exact consumption, record by record and archive-wide.** dat1 configs are
 *      one concatenated stream, so a decode that misjudges one record shifts
 *      every later one; requiring the walk to land exactly on the archive's last
 *      byte is the strongest single check available.
 *   2. **No encode exceeds its own bound**, checked with a guard-band canary
 *      rather than by trusting the return value.
 *   3. **The interface reproduces what the type's own entry point reproduces.**
 *      Asserted only where the direct path above already proved the archive
 *      byte-reproducible — a shortfall there means the interface lost something
 *      the direct path keeps, which is exactly what a zeroing `record_init`
 *      would cause.
 */

#define CANARY_BYTES 32
#define CANARY_FILL 0xA5

struct RegTally
{
    int records;
    int exact;
    int differ;
    /**
     * Records that re-encode to the same *length* but not the same bytes.
     *
     * The column `EXCEPTIONS.md` B3 exists for: high same-len with low exact
     * means the source stated its opcodes in an order the encoder does not
     * reproduce, which loses nothing; low both means the decoder dropped a
     * field. Without it "0 exact" cannot be told apart from "0 kept".
     */
    int same_len;
    int bound_exceeded;
    int encode_failed;
    /** True when the walk landed exactly on the archive's last byte. */
    int consumed_all;
};

static const struct RSCache_OpcodeCodec*
reachable_codec(const struct RSCache* profile, const char* name, enum RSCache_Type type)
{
    const struct RSCache_OpcodeCodec* by_name = RSCache_OpcodeCodecByName(profile, name);
    char what[96];

    snprintf(what, sizeof(what), "dat1 `%s` is reachable by name and by type", name);
    /* Both lookups, not one: a row keyed to the wrong `type` is still reachable
     * by name, and every real caller uses the type lookup. */
    check(by_name != NULL && by_name == RSCache_OpcodeCodecFor(profile, type), what);
    return by_name;
}

/**
 * Re-encode one record through the codec and tally it against its source bytes.
 *
 * `src`/`len` is the record's own slice, so this is byte identity per record
 * rather than per archive.
 */
static void
tally_encode(
    const struct RSCache_OpcodeCodec* codec,
    const struct RSCache* profile,
    const void* record,
    const uint8_t* src,
    int len,
    struct RegTally* tally)
{
    uint32_t bound = codec->encode_bound(record);
    uint8_t* out = (uint8_t*)malloc(bound + CANARY_BYTES);
    uint32_t written;
    int overran = 0;

    if( !out )
        return;
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
    if( written == (uint32_t)len && memcmp(out, src, (size_t)len) == 0 )
        tally->exact++;
    else
    {
        tally->differ++;
        if( written == (uint32_t)len )
            tally->same_len++;
    }
    free(out);
}

/**
 * Walk a concatenated dat1 archive through the registry.
 *
 * The record boundaries come from the decoder's own consumption — there is no
 * index — which is what makes property (1) above worth checking: if any record
 * is misjudged, the walk cannot land on the archive's end.
 */
static void
run_registry_stream(
    const struct RSCache* profile,
    struct RSCache_FileListDat* jag,
    const char* name,
    enum RSCache_Type type,
    const char* member,
    int header_bytes,
    struct RegTally* tally)
{
    const struct RSCache_OpcodeCodec* codec;
    const uint8_t* src;
    int size;
    int offset = header_bytes;

    int file = RSCache_FileListDatFindFileByName(jag, member);
    if( file < 0 )
    {
        printf("dat1-registry: %-10s no %s in this cache\n", name, member);
        return;
    }
    codec = reachable_codec(profile, name, type);
    if( !codec )
        return;

    src = (const uint8_t*)jag->files[file];
    size = jag->file_sizes[file];

    while( offset < size )
    {
        void* record;
        int consumed = -1;

        record = RSCache_OpcodeCodecRecordDecode(
            codec, profile, src + offset, size - offset, &consumed);
        if( !record || consumed <= 0 )
        {
            if( record )
                RSCache_OpcodeCodecRecordFree(codec, record);
            break;
        }
        tally->records++;
        if( RSCache_OpcodeCodecCanEncode(codec) )
            tally_encode(codec, profile, record, src + offset, consumed, tally);
        RSCache_OpcodeCodecRecordFree(codec, record);
        offset += consumed;
    }

    tally->consumed_all = (offset == size);
}

/**
 * Walk an indexed dat1 archive (`<type>.idx` + `<type>.dat`) through the registry.
 *
 * Here the boundaries are stated by the index, so consumption is checked against
 * the length the index gives rather than being the thing that finds them.
 */
static void
run_registry_indexed(
    const struct RSCache* profile,
    struct RSCache_FileListDat* jag,
    const char* name,
    enum RSCache_Type type,
    const char* idx_member,
    const char* dat_member,
    struct RegTally* tally)
{
    const struct RSCache_OpcodeCodec* codec;
    const uint8_t* idxp;
    const uint8_t* datp;
    int idx = RSCache_FileListDatFindFileByName(jag, idx_member);
    int dat = RSCache_FileListDatFindFileByName(jag, dat_member);
    int count;
    uint32_t offset = 2; /* past the u16 count */

    if( idx < 0 || dat < 0 )
    {
        printf("dat1-registry: %-10s no %s/%s in this cache\n", name, idx_member, dat_member);
        return;
    }
    codec = reachable_codec(profile, name, type);
    if( !codec )
        return;

    idxp = (const uint8_t*)jag->files[idx];
    datp = (const uint8_t*)jag->files[dat];
    count = (idxp[0] << 8) | idxp[1];

    tally->consumed_all = 1;
    for( int i = 0; i < count; i++ )
    {
        int len = (idxp[2 + i * 2] << 8) | idxp[2 + i * 2 + 1];
        void* record;
        int consumed = -1;

        record = RSCache_OpcodeCodecRecordDecode(codec, profile, datp + offset, len, &consumed);
        if( !record )
        {
            tally->consumed_all = 0;
            break;
        }
        tally->records++;
        /* Short of the index's own length means the decoder stopped on an
         * opcode it did not know — the signal, not an error code. */
        if( consumed != len )
            tally->consumed_all = 0;
        if( RSCache_OpcodeCodecCanEncode(codec) )
            tally_encode(codec, profile, record, datp + offset, len, tally);
        RSCache_OpcodeCodecRecordFree(codec, record);
        offset += (uint32_t)len;
    }
}

static void
report(const char* name, const struct RegTally* tally, int require_all_exact)
{
    char what[96];

    printf("dat1-registry: %-10s %5d records, %5d exact, %5d differ (%d same-len)\n", name,
           tally->records, tally->exact, tally->differ, tally->same_len);

    snprintf(what, sizeof(what), "dat1 `%s`: the cache has records to check", name);
    check(tally->records > 0, what);
    snprintf(what, sizeof(what), "dat1 `%s`: every record is consumed to its last byte", name);
    check(tally->consumed_all, what);
    snprintf(what, sizeof(what), "dat1 `%s`: no encode exceeded its own bound", name);
    check(tally->bound_exceeded == 0, what);
    snprintf(what, sizeof(what), "dat1 `%s`: no encode failed at its own bound", name);
    check(tally->encode_failed == 0, what);
    if( require_all_exact )
    {
        /*
         * Only for the types the direct path above already proves reproducible
         * archive-wide. Where it holds it is the check that a zeroing
         * `record_init` would fail: the encoders omit any field equal to its
         * default, so seeding the wrong default changes which opcodes are
         * written without changing anything else about the record.
         */
        snprintf(what, sizeof(what),
                 "dat1 `%s`: the interface re-encodes every record to its own bytes", name);
        check(tally->exact == tally->records, what);
    }
}

static void
run_registry(struct RSCache_FileListDat* jag, struct RSCache_FileListDat* interfaces)
{
    struct RSCache profile;
    struct RegTally tally;

    if( !RSCache_ProfileByName("lc254", &profile) )
    {
        printf("dat1-registry: SKIPPED — no lc254 profile\n");
        return;
    }

#define RUN_STREAM(NAME, TYPE, MEMBER, JAG, ALL_EXACT)                                             \
    do                                                                                             \
    {                                                                                              \
        memset(&tally, 0, sizeof(tally));                                                          \
        run_registry_stream(&profile, (JAG), NAME, TYPE, MEMBER, 2, &tally);                       \
        if( tally.records )                                                                        \
            report(NAME, &tally, ALL_EXACT);                                                       \
    } while( 0 )

    /* idk and spotanim are byte-reproducible archive-wide on the direct path
     * above, so the interface is held to the same bar. seq is not: its encoder
     * writes ascending and states only what differs from a default, while the
     * source archive's order is its own — the `same-len` versus `exact` split
     * `EXCEPTIONS.md` B3 describes. Its numbers are printed, not asserted. */
    RUN_STREAM("idk", RSCACHE_TYPE_IDK, "idk.dat", jag, 1);
    RUN_STREAM("spotanim", RSCACHE_TYPE_SPOTANIM, "spotanim.dat", jag, 1);
    RUN_STREAM("seq", RSCACHE_TYPE_SEQUENCE, "seq.dat", jag, 0);

    memset(&tally, 0, sizeof(tally));
    run_registry_indexed(&profile, jag, "obj", RSCACHE_TYPE_OBJ, "obj.idx", "obj.dat", &tally);
    if( tally.records )
        report("obj", &tally, 0);

    memset(&tally, 0, sizeof(tally));
    run_registry_indexed(&profile, jag, "npc", RSCACHE_TYPE_NPC, "npc.idx", "npc.dat", &tally);
    if( tally.records )
        report("npc", &tally, 0);

    /*
     * component lives in its own jagfile and has no encoder, so `report`'s
     * encode bars are vacuous for it — what is being checked is that the
     * whole-record `decode` override consumes the archive exactly, which is the
     * only bar a decode-only row can be held to.
     */
    if( interfaces )
    {
        memset(&tally, 0, sizeof(tally));
        run_registry_stream(&profile, interfaces, "component", RSCACHE_TYPE_COMPONENT, "data", 2,
                            &tally);
        if( tally.records )
        {
            const struct RSCache_OpcodeCodec* codec =
                RSCache_OpcodeCodecByName(&profile, "component");

            report("component", &tally, 0);
            check(
                !RSCache_OpcodeCodecCanEncode(codec),
                "dat1 `component` reports itself decode-only rather than half-encoding");
        }
    }

#undef RUN_STREAM
}

int
main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "cache254";
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* jag;
    struct RSCache_Dat1DiskArchive* ifaces_archive = NULL;
    struct RSCache_FileListDat* ifaces = NULL;

    disk = RSCache_Dat1DiskNewFromDirectory(dir);
    if( !disk )
    {
        /* Loud, and not a pass — the rule the rest of this suite follows. */
        printf("dat1-encode: SKIPPED — no dat1 cache at %s\n", dir);
        return 0;
    }
    archive = RSCache_Dat1DiskArchiveNewLoad(disk, 0, 2);
    if( !archive )
    {
        printf("dat1-encode: SKIPPED — %s has no config archive\n", dir);
        RSCache_Dat1DiskFree(disk);
        return 0;
    }
    jag = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
    if( !jag )
    {
        printf("dat1-encode: SKIPPED — config.jag did not decode\n");
        RSCache_Dat1DiskArchiveFree(archive);
        RSCache_Dat1DiskFree(disk);
        return 0;
    }

    run_idk(jag);
    run_obj(jag);
    run_spotanim(jag);

    /* The interfaces jagfile is a separate archive (table 0, archive 3) and its
     * components live in one member called "data". Absent is not a failure —
     * `run_registry` skips what it cannot find, as the rest of this file does. */
    ifaces_archive = RSCache_Dat1DiskArchiveNewLoad(disk, 0, RSCACHE_DAT1_CONFIG_INTERFACES);
    if( ifaces_archive )
        ifaces = RSCache_FileListDatNewFromDecode(
            ifaces_archive->data, ifaces_archive->data_size);
    run_registry(jag, ifaces);

    if( ifaces )
        RSCache_FileListDatFree(ifaces);
    if( ifaces_archive )
        RSCache_Dat1DiskArchiveFree(ifaces_archive);
    RSCache_FileListDatFree(jag);
    RSCache_Dat1DiskArchiveFree(archive);
    RSCache_Dat1DiskFree(disk);

    if( g_failures )
    {
        printf("dat1-encode: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("dat1-encode: all %d checks passed\n", g_checks);
    return 0;
}
