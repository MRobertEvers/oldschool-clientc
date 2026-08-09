/*
 * Ambient soundscapes (dat2 config kind 15) — decode, round trip, and the era
 * gate.
 *
 * This format has no reference implementation to compare against and no
 * checksum, so the check that means anything is byte-exact re-encoding of every
 * record in a real cache: a field read in the wrong order, or a ×20 applied
 * twice, cannot survive it.
 *
 * The synthetic cases exist for the two things a corpus of eight records cannot
 * exercise on its own — the reference's caps (8 sets, 48 ids) and the
 * stop-on-unknown-opcode rule.
 */

#include "rscache.h"
#include "rscache_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_ROOT
#define CACHE_ROOT "."
#endif

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* Record 1 of cache.osrs239, transcribed byte for byte. Four random sets, no
 * continuous loop, no fades -- the shape the format is really for. */
static const uint8_t k_record1[] = {
    0x02, 0x01, 0x5e, 0x02, 0x8a, 0x04, 0x2d, 0x46, 0x2d, 0x47, 0x2d, 0x48, 0x2d, 0x49,
    0x02, 0x01, 0x5e, 0x06, 0x40, 0x02, 0x2d, 0x4f, 0x09, 0x6b,
    0x02, 0x00, 0xfa, 0x03, 0xe8, 0x05, 0x2d, 0x4a, 0x2d, 0x4b, 0x09, 0x6b, 0x09, 0x6b, 0x09, 0x6b,
    0x02, 0x00, 0xfa, 0x03, 0x20, 0x09, 0x2d, 0x4c, 0x2d, 0x4d, 0x2d, 0x4e, 0x09, 0x6b, 0x09, 0x6b,
    0x09, 0x6b, 0x09, 0x6b, 0x09, 0x6b, 0x09, 0x6b,
    0x00
};

static void
test_known_record(void)
{
    struct RSCache_Dat2ConfigSoundscape s;

    printf("-- cache.osrs239 record 1\n");
    RSCache_Dat2ConfigSoundscapeDecodeInplace(&s, k_record1, (int)sizeof(k_record1));

    CHECK(s._consumed == (int)sizeof(k_record1), "consumed the whole record");
    CHECK(s.loop_count == 0, "no continuous loop");
    CHECK(s.set_count == 4, "four random sets");
    if( s.set_count == 4 )
    {
        /* 0x015e = 350 ticks = 7000 ms; 0x028a = 650 = 13000 ms. */
        CHECK(s.sets[0].min_ms == 7000, "set 0 min is ticks x 20");
        CHECK(s.sets[0].max_ms == 13000, "set 0 max is ticks x 20");
        CHECK(s.sets[0].id_count == 4, "set 0 has four ids");
        CHECK(s.sets[0].ids && s.sets[0].ids[0] == 11590, "set 0 first id");
        CHECK(s.sets[0].ids && s.sets[0].ids[3] == 11593, "set 0 last id");

        /* The weighting idiom: 2411 repeated to bias the uniform pick. */
        CHECK(s.sets[2].id_count == 5, "set 2 has five ids");
        CHECK(s.sets[3].id_count == 9, "set 3 has nine ids");
        if( s.sets[3].ids )
        {
            int filler = 0;
            for( int i = 0; i < s.sets[3].id_count; i++ )
                if( s.sets[3].ids[i] == 2411 )
                    filler++;
            CHECK(filler == 6, "set 3 repeats the filler id six times");
        }
    }
    RSCache_Dat2ConfigSoundscapeFreeInplace(&s);
}

static void
test_caps_consume_before_dropping(void)
{
    /*
     * A set whose id count exceeds the reference's 48 is *read* and then
     * dropped. Skipping the payload instead would leave the cursor inside it and
     * turn every following byte into a garbage opcode -- so what this really
     * asserts is that the record after the oversized set still decodes.
     */
    uint8_t rec[8 + 49 * 2 + 8];
    struct RSCache_Dat2ConfigSoundscape s;
    int n = 0;

    printf("-- an over-long set is consumed, then dropped\n");
    rec[n++] = 2;               /* opcode 2 */
    rec[n++] = 0; rec[n++] = 5; /* min 5 ticks */
    rec[n++] = 0; rec[n++] = 9; /* max 9 ticks */
    rec[n++] = 49;              /* one past the cap */
    for( int i = 0; i < 49; i++ )
    {
        rec[n++] = 0;
        rec[n++] = (uint8_t)i;
    }
    rec[n++] = 1;                 /* opcode 1: a continuous loop */
    rec[n++] = 1;                 /* one id */
    rec[n++] = 0x2d; rec[n++] = 0x5b; /* 11611 */
    rec[n++] = 0;                 /* terminator */

    RSCache_Dat2ConfigSoundscapeDecodeInplace(&s, rec, n);
    CHECK(s._consumed == n, "consumed the whole record despite the dropped set");
    CHECK(s.set_count == 0, "the over-long set was dropped");
    CHECK(s.loop_count == 1, "the opcode after it still decoded");
    CHECK(s.loop_ids && s.loop_ids[0] == 11611, "and decoded correctly");
    RSCache_Dat2ConfigSoundscapeFreeInplace(&s);
}

static void
test_unknown_opcode_stops(void)
{
    uint8_t rec[] = { 1, 1, 0x00, 0x05, 99, 0, 0, 0, 0 };
    struct RSCache_Dat2ConfigSoundscape s;

    printf("-- an unknown opcode stops the decode\n");
    RSCache_Dat2ConfigSoundscapeDecodeInplace(&s, rec, (int)sizeof(rec));
    CHECK(s._consumed == 0, "a short decode reports itself");
    CHECK(s.loop_count == 1, "fields before the unknown opcode are kept");
    RSCache_Dat2ConfigSoundscapeFreeInplace(&s);
}

/** Decode + re-encode every record in a cache's group 15. */
static int
sweep_cache(const char* cache_dir, const char* rev_name)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int records = 0;
    int exact = 0;

    if( !RSCache_ProfileByName(rev_name, &profile) )
    {
        printf("  SKIP: no profile %s\n", rev_name);
        return 0;
    }
    disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    if( !disk )
    {
        printf("  SKIP: no cache at %s\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_SOUNDSCAPE);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) ||
        archive->file_count <= 0 )
    {
        /* Not a failure. Group 15 is an OldSchool 231+ addition and its absence
         * is the fact the era gate rests on. */
        printf("  %s: no group 15 (expected before OldSchool 231)\n", cache_dir);
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    for( int i = 0; files && i < files->file_count; i++ )
    {
        struct RSCache_Dat2ConfigSoundscape s;
        uint8_t* re;
        uint32_t bound;
        uint32_t wrote;

        if( files->file_sizes[i] <= 0 )
            continue;
        records++;
        RSCache_Dat2ConfigSoundscapeDecodeInplace(&s, files->files[i], files->file_sizes[i]);
        if( s._consumed != files->file_sizes[i] )
        {
            printf(
                "  FAIL: record %d consumed %d of %d\n",
                archive->file_ids ? archive->file_ids[i] : i,
                s._consumed,
                files->file_sizes[i]);
            g_fail++;
            RSCache_Dat2ConfigSoundscapeFreeInplace(&s);
            continue;
        }

        bound = RSCache_Dat2ConfigSoundscapeEncodeBound(&s);
        re = (uint8_t*)malloc(bound);
        wrote = re ? RSCache_Dat2ConfigSoundscapeEncode(&s, re, bound) : 0;
        if( re && wrote == (uint32_t)files->file_sizes[i] &&
            memcmp(re, files->files[i], wrote) == 0 )
        {
            exact++;
        }
        else
        {
            /* The offset is the whole diagnostic: a length mismatch is a
             * missing opcode, and an equal-length mismatch at offset N names
             * the field. Printing only "not exact" would mean re-deriving it by
             * hand every time. */
            uint32_t at = 0;
            while( re && at < wrote && at < (uint32_t)files->file_sizes[i] &&
                   re[at] == (uint8_t)files->files[i][at] )
                at++;
            printf(
                "  FAIL: record %d re-encoded %u bytes of %d, first diff at %u"
                " (got %02x want %02x)\n",
                archive->file_ids ? archive->file_ids[i] : i,
                wrote,
                files->file_sizes[i],
                at,
                re && at < wrote ? re[at] : 0,
                at < (uint32_t)files->file_sizes[i] ? (uint8_t)files->files[i][at] : 0);
        }
        free(re);
        RSCache_Dat2ConfigSoundscapeFreeInplace(&s);
    }
    g_checks++;
    if( records != exact )
        g_fail++;
    printf("  %s: %d/%d records byte-exact\n", cache_dir, exact, records);

    if( files )
        RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return records;
}

int
main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : CACHE_ROOT;
    char path[1024];

    test_known_record();
    test_caps_consume_before_dropping();
    test_unknown_opcode_stops();

    printf("-- whole-cache round trip\n");
    snprintf(path, sizeof(path), "%s/cache.osrs239", root);
    sweep_cache(path, "osrs239");
    snprintf(path, sizeof(path), "%s/cache.osrs230", root);
    sweep_cache(path, "osrs230");

    if( g_fail )
    {
        printf("test_soundscape: %d/%d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("test_soundscape: %d checks passed\n", g_checks);
    return 0;
}
