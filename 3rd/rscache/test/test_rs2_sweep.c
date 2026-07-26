/*
 * cache.643 (RS2) coverage sweep — a *diagnostic*, not an assertion suite.
 *
 * The round-trip suite (test_roundtrip.c) scans config *groups* in table 2, which is
 * the OldSchool layout. 643 promotes loc/npc/obj/seq/spotanim into their own sharded
 * tables (see RSCache_RecordAddressFor), so those types are invisible to it. This
 * walks the sharded tables directly and reports, per type:
 *
 *   - records seen
 *   - records whose decode landed exactly on their terminator (exact consumption)
 *   - a histogram of the opcode each short decode stopped on
 *
 * The opcode histogram is the useful part. A decoder that stops on opcode 229 has
 * not met a real opcode 229 — it has desynced earlier and is reading a payload byte
 * as an opcode. The *distribution* names the culprit: a single wrong field width
 * shows up as one hot opcode with a long tail of noise behind it.
 *
 *   make -C 3rd/rscache build/test_rs2_sweep && 3rd/rscache/build/test_rs2_sweep
 */
#include "rscache_test.h"

#include "dat2disk.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_obj.h"
#include "filelist.h"
#include "reference_table.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RSCACHE_TEST_REPO_ROOT
#define RSCACHE_TEST_REPO_ROOT "../.."
#endif

struct sweep
{
    int records;
    int consumed_exact;
    int consumed_short;
    /* Opcode the decode stopped on, for records that did not consume exactly. */
    int stop_opcode[256];
};

static void
sweep_report(
    const char* label,
    const char* cache,
    const struct sweep* s)
{
    if( s->records == 0 )
    {
        printf("  %-10s %-14s (absent)\n", label, cache);
        return;
    }
    printf(
        "  %-10s %-14s %6d records  exact %6d (%5.1f%%)  short %5d\n",
        label,
        cache,
        s->records,
        s->consumed_exact,
        100.0 * s->consumed_exact / s->records,
        s->consumed_short);

    /* Rank the stop opcodes so the dominant one is first — that is the one worth
     * chasing; the rest are usually its downstream noise. */
    for( int rank = 0; rank < 12; rank++ )
    {
        int best = -1;
        for( int op = 0; op < 256; op++ )
            if( s->stop_opcode[op] > 0 && (best < 0 || s->stop_opcode[op] > s->stop_opcode[best]) )
                best = op;
        if( best < 0 )
            break;
        printf("      stopped on opcode %3d : %d\n", best, s->stop_opcode[best]);
        ((struct sweep*)s)->stop_opcode[best] = -s->stop_opcode[best];
    }
    for( int op = 0; op < 256; op++ )
        if( s->stop_opcode[op] < 0 )
            ((struct sweep*)s)->stop_opcode[op] = -s->stop_opcode[op];
}

/*
 * The opcode a short decode stopped on.
 *
 * `_consumed` is the position *after* the byte the decoder gave up on — the opcode
 * has already been read when the default arm fires — so the opcode is at
 * `consumed - 1`, not at `consumed`. Reading it at `consumed` reports the first
 * *payload* byte instead and makes the histogram meaningless.
 */
static void
note_stop(
    struct sweep* s,
    const uint8_t* data,
    int size,
    int consumed)
{
    s->consumed_short++;
    if( consumed > 0 && consumed <= size )
        s->stop_opcode[data[consumed - 1]]++;
    else
        s->stop_opcode[0]++;
}

typedef void (*sweep_visitor)(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s);

static void
visit_loc(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s)
{
    struct RSCache_Dat2ConfigLoc loc;
    memset(&loc, 0, sizeof(loc));
    RSCache_Dat2ConfigLocDecodeInplace(
        &loc, (char*)data, size, RSCache_Dat2ConfigLocFlags(profile));
    s->records++;
    if( loc._consumed == size )
        s->consumed_exact++;
    else
        note_stop(s, data, size, loc._consumed);
    RSCache_Dat2ConfigLocFreeInplace(&loc);
}

static void
visit_obj(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s)
{
    /* obj has no profile overload and no _consumed field, so this can only report
     * "the decoder returned something" — enough to tell a hard failure from a
     * silent misread, not enough to prove alignment. */
    (void)profile;
    struct RSCache_Dat2ConfigObj* obj = RSCache_Dat2ConfigObjNewDecode((char*)data, size);
    s->records++;
    if( !obj )
        note_stop(s, data, size, -1);
    else
        s->consumed_exact++;
    RSCache_Dat2ConfigObjFree(obj);
}

static void
visit_npc(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s)
{
    struct RSCache_Dat2ConfigNpc* npc =
        RSCache_Dat2ConfigNpcNewDecodeProfile(profile, (char*)data, size);
    s->records++;
    if( !npc )
    {
        note_stop(s, data, size, -1);
        return;
    }
    if( npc->_consumed == size )
        s->consumed_exact++;
    else
        note_stop(s, data, size, npc->_consumed);
    RSCache_Dat2ConfigNpcFree(npc);
}

/*
 * Walk every group of a sharded table.
 *
 * The reference table names which groups exist; loading blind would trip on the
 * sparse ones. Records are decoded straight from the file list — the id itself is
 * not needed here, only the bytes.
 */
static void
sweep_table(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    enum RSCache_Type type,
    sweep_visitor visit,
    struct sweep* s)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, type);
    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    if( table_id < 0 )
        return;

    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
        return;
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
        return;

    for( int i = 0; i < table->id_count; i++ )
    {
        int group = table->ids[i];
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, group);
        if( !archive )
            continue;
        RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( files )
        {
            for( int f = 0; f < files->file_count; f++ )
            {
                if( files->file_sizes[f] <= 0 )
                    continue;
                visit(profile, (const uint8_t*)files->files[f], files->file_sizes[f], s);
            }
            RSCache_FileListFree(files);
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }

    RSCache_ReferenceTableFree(table);
}

int
main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : RSCACHE_TEST_REPO_ROOT;

    static const char* CACHES[] = { "cache.643", "cache.rs643" };

    struct
    {
        const char* label;
        enum RSCache_Type type;
        sweep_visitor visit;
    } CASES[] = {
        { "loc", RSCACHE_TYPE_LOC, visit_loc },
        { "obj", RSCACHE_TYPE_OBJ, visit_obj },
        { "npc", RSCACHE_TYPE_NPC, visit_npc },
    };

    printf("rs2 sweep (root %s)\n", root);

    for( size_t c = 0; c < sizeof(CACHES) / sizeof(CACHES[0]); c++ )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", root, CACHES[c]);
        struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
        if( !disk )
        {
            printf("  (no cache at %s)\n", path);
            continue;
        }

        struct RSCache profile;
        if( !RSCache_ProfileByName("643", &profile) )
            profile = RSCache_ProfileZero();
        RSCache_Dat2DiskSetEpoch(disk, profile.epoch);

        for( size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++ )
        {
            struct sweep s;
            memset(&s, 0, sizeof(s));
            sweep_table(disk, &profile, CASES[i].type, CASES[i].visit, &s);
            sweep_report(CASES[i].label, CACHES[c], &s);
        }

        RSCache_Dat2DiskFree(disk);
    }

    return 0;
}
