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
#include "filelist.h"

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

int
main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "cache254";
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* jag;

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
