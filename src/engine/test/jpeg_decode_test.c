/*
 * JPEG decode, against the real title backgrounds in the caches this tree
 * ships with.
 *
 * There is no synthetic fixture here on purpose. The thing worth proving is not
 * that stb decodes JPEG -- it does -- but that the bytes the RS caches actually
 * store decode at all: the dat1 title member has a corrupted SOI that every
 * reference client patches before decoding, and finding that out from a blank
 * title screen later is the failure this test exists to prevent.
 *
 * A cache that is not checked out is skipped, not failed.
 */

#include "engine/jpeg_decode.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Tests run from src/; the caches sit beside it at the repo root. */
static void
cache_path(
    char* out,
    size_t out_size,
    char const* dir)
{
    snprintf(out, out_size, "../%s", dir);
}

/* A decoded background is a photograph: it must be opaque, and it must not be
 * one flat colour (which is what a decode that "succeeded" into an empty buffer
 * looks like). */
static void
check_plausible_image(
    uint32_t const* pixels,
    int width,
    int height,
    char const* what)
{
    size_t count = (size_t)width * (size_t)height;
    uint32_t first = pixels[0];
    int all_same = 1;
    int all_opaque = 1;

    for( size_t i = 0; i < count; i++ )
    {
        if( pixels[i] != first )
            all_same = 0;
        if( (pixels[i] & 0xFF000000u) != 0xFF000000u )
            all_opaque = 0;
    }

    CHECK(all_opaque, what);
    CHECK(!all_same, what);
}

static void
test_dat1_title(void)
{
    char path[512];
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* jagfile;
    int member;
    int width = 0;
    int height = 0;
    uint32_t* pixels = NULL;

    cache_path(path, sizeof(path), "cache254.lostcity");
    disk = RSCache_Dat1DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP cache254.lostcity: not present\n");
        return;
    }

    archive = RSCache_Dat1DiskArchiveNewLoad(
        disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS);
    if( !archive )
    {
        printf("   SKIP cache254.lostcity: no title archive\n");
        RSCache_Dat1DiskFree(disk);
        return;
    }

    jagfile = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
    CHECK(jagfile != NULL, "dat1 title jagfile decodes");
    if( jagfile )
    {
        member = RSCache_FileListDatFindFileByName(jagfile, "title.dat");
        CHECK(member >= 0, "dat1 title.dat is present");
        if( member >= 0 )
        {
            CHECK(
                JpegDecode_ArgbRsCache(
                    jagfile->files[member], jagfile->file_sizes[member], &width, &height, &pixels),
                "dat1 title.dat decodes");
            if( pixels )
            {
                printf("   cache254.lostcity title.dat: %dx%d\n", width, height);
                /* Half a title screen: the reference blits this at x=0 and its
                 * mirror at x=382 to fill 765x503 (Client-TS loadTitleBackground),
                 * so the two halves overlap by a column and the composite is
                 * exactly the screen. Pinned because the composite's tile
                 * offsets are only correct for these dimensions. */
                CHECK(width == 383, "dat1 title is the 383-wide half-backdrop");
                CHECK(height == 503, "dat1 title is 503 tall");
                check_plausible_image(pixels, width, height, "dat1 title pixels are a photograph");
                free(pixels);
                pixels = NULL;
            }
        }
        RSCache_FileListDatFree(jagfile);
    }

    RSCache_Dat1DiskArchiveFree(archive);
    RSCache_Dat1DiskFree(disk);
}

/* The modern lane keeps its title background in the binary table, addressed by
 * name hash rather than by a jagfile member name. */
static int
dat2_archive_by_name(
    struct RSCache_ReferenceTable* table,
    char const* name)
{
    char buf[64];
    int name_hash;

    snprintf(buf, sizeof(buf), "%s", name);
    name_hash = RSCache_ArchiveNameHashDat2(buf);
    for( int i = 0; i < table->archive_count; i++ )
    {
        if( RSCache_ReferenceTableIdentifier(table, i) == name_hash )
            return i;
    }
    return -1;
}

static void
test_dat2_title(void)
{
    char path[512];
    struct RSCache_Dat2Disk* disk;
    struct RSCache_ReferenceTable* table;
    struct RSCache_Dat2DiskArchive* archive;
    int archive_id;
    int width = 0;
    int height = 0;
    uint32_t* pixels = NULL;

    cache_path(path, sizeof(path), "cache.osrs239");
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP cache.osrs239: not present\n");
        return;
    }

    table = RSCache_Dat2DiskReferenceTable(disk, RSCACHE_DAT2_TABLE_BINARY);
    if( !table )
    {
        printf("   SKIP cache.osrs239: no binary table\n");
        RSCache_Dat2DiskFree(disk);
        return;
    }

    archive_id = dat2_archive_by_name(table, "title.jpg");
    CHECK(archive_id >= 0, "dat2 title.jpg is in the binary table");
    if( archive_id >= 0 )
    {
        archive = RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_TABLE_BINARY, archive_id);
        CHECK(archive != NULL, "dat2 title.jpg archive loads");
        if( archive )
        {
            CHECK(
                JpegDecode_ArgbRsCache(
                    archive->data, archive->data_size, &width, &height, &pixels),
                "dat2 title.jpg decodes");
            if( pixels )
            {
                printf("   cache.osrs239 title.jpg: %dx%d\n", width, height);
                /* Same half-and-mirror composition as the old lane: the deob
                 * blits it at titleX and its mirror at titleX+382. */
                CHECK(width > 0 && width <= 765, "dat2 title is a half-screen backdrop");
                CHECK(height >= 500, "dat2 title covers the screen height");
                check_plausible_image(pixels, width, height, "dat2 title pixels are a photograph");
                free(pixels);
                pixels = NULL;
            }
            RSCache_Dat2DiskArchiveFree(archive);
        }
    }

    RSCache_Dat2DiskFree(disk);
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    test_dat1_title();
    test_dat2_title();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("jpeg_decode_test: ok (%d checks)\n", g_checks);
    return 0;
}
