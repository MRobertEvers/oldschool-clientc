/*
 * Dat2 cache edit API: seed a minimal cache, PutFile, Commit, reopen and verify.
 */
#include "rscache_test.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
prepare_directory(const char* directory)
{
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf '%s' && mkdir -p '%s'", directory, directory);
    return system(command);
}

static void
cleanup_directory(const char* directory)
{
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf '%s'", directory);
    (void)system(command);
}

static bool
write_seed_cache(const char* directory)
{
    int table_id = RSCACHE_DAT2_OSRS_TABLE_CONFIGS;
    int archive_id = 9;

    static const uint8_t file0[] = "seed-file-zero";
    struct RSCache_FileList group = { 0 };
    char* files[1];
    int sizes[1];
    files[0] = (char*)file0;
    sizes[0] = (int)(sizeof(file0) - 1);
    group.files = files;
    group.file_sizes = sizes;
    group.file_count = 1;

    uint32_t group_bound = RSCache_FileListEncodeBound(&group);
    uint8_t* group_bytes = malloc(group_bound ? group_bound : 1);
    if( !group_bytes )
        return false;
    uint32_t group_size = RSCache_FileListEncode(&group, group_bytes, group_bound);
    if( group_size == 0 )
    {
        free(group_bytes);
        return false;
    }

    uint32_t bound =
        RSCache_ArchiveEncodeBound(group_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    uint8_t* container = malloc(bound);
    if( !container )
    {
        free(group_bytes);
        return false;
    }
    uint32_t container_size = RSCache_ArchiveEncode(
        container,
        bound,
        group_bytes,
        group_size,
        RSCACHE_ARCHIVE_COMPRESSION_GZIP,
        NULL);
    free(group_bytes);
    if( container_size == 0 )
    {
        free(container);
        return false;
    }

    if( RSCache_Dat2DiskWriteArchive(
            directory, table_id, archive_id, container, (int)container_size) != 0 )
    {
        free(container);
        return false;
    }

    int crc = (int)RSCache_Crc32Buffer(container, container_size);
    int compressed = (int)container_size;
    int uncompressed = (int)group_size;
    free(container);

    struct RSCache_ReferenceTable table = { 0 };
    int ids[] = { archive_id };
    table.format = 7;
    table.version = 1;
    table.flags = RSCACHE_REFTABLE_FLAG_SIZES;
    table.id_count = 1;
    table.ids = ids;
    table.archive_count = archive_id + 1;
    table.archives =
        calloc((size_t)table.archive_count, sizeof(struct RSCache_ReferenceTableArchive));
    if( !table.archives )
        return false;
    for( int i = 0; i < table.archive_count; i++ )
        RSCache_ReferenceTableSetHasArchive(&table, i, false);

    RSCache_ReferenceTableSetHasArchive(&table, archive_id, true);
    table.archives[archive_id].crc = crc;
    RSCache_ReferenceTableCompressed(&table, archive_id) = compressed;
    RSCache_ReferenceTableUncompressed(&table, archive_id) = uncompressed;
    table.archives[archive_id].version = 1;
    table.archives[archive_id].children.count = 1;
    table.archives[archive_id].children.files =
        calloc(1, sizeof(struct RSCache_ReferenceTableArchiveFile));
    if( !table.archives[archive_id].children.files )
    {
        free(table.archives);
        return false;
    }
    table.archives[archive_id].children.files[0].id = 0;

    uint32_t rbound = RSCache_ReferenceTableEncodeBound(&table);
    uint8_t* raw = malloc(rbound);
    if( !raw )
    {
        free(table.archives[archive_id].children.files);
        free(table.archives);
        return false;
    }
    uint32_t raw_size = RSCache_ReferenceTableEncode(&table, raw, rbound);
    if( raw_size == 0 )
    {
        free(raw);
        free(table.archives[archive_id].children.files);
        free(table.archives);
        return false;
    }

    uint32_t cbound = RSCache_ArchiveEncodeBound(raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    uint8_t* ref_container = malloc(cbound);
    if( !ref_container )
    {
        free(raw);
        free(table.archives[archive_id].children.files);
        free(table.archives);
        return false;
    }
    uint32_t ref_size = RSCache_ArchiveEncode(
        ref_container, cbound, raw, raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP, NULL);
    free(raw);
    free(table.archives[archive_id].children.files);
    free(table.archives);
    if( ref_size == 0 )
    {
        free(ref_container);
        return false;
    }

    int rc = RSCache_Dat2DiskWriteArchive(
        directory,
        RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID,
        table_id,
        ref_container,
        (int)ref_size);
    free(ref_container);
    return rc == 0;
}

static void
test_put_file_replace_and_append(void)
{
    RSCACHE_TEST_GROUP("dat2 edit put file");

    const char* directory = getenv("RSCACHE_TEST_TMPDIR");
    char fallback[] = "/tmp/rscache_test_cache_edit";
    if( !directory )
        directory = fallback;

    if( prepare_directory(directory) != 0 )
    {
        printf("   SKIP could not prepare %s\n", directory);
        return;
    }

    RSCACHE_CHECK(write_seed_cache(directory));

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    if( !disk )
    {
        cleanup_directory(directory);
        return;
    }

    struct RSCache_Dat2Edit* edit = RSCache_Dat2EditNew(disk);
    RSCACHE_CHECK(edit != NULL);
    if( !edit )
    {
        RSCache_Dat2DiskFree(disk);
        cleanup_directory(directory);
        return;
    }

    static const uint8_t replaced[] = "replaced-file-zero";
    static const uint8_t appended[] = "brand-new-file-one";
    int table_id = RSCACHE_DAT2_OSRS_TABLE_CONFIGS;

    RSCACHE_CHECK(RSCache_Dat2EditPutFile(
        edit, table_id, 9, 0, replaced, (uint32_t)(sizeof(replaced) - 1)));
    RSCACHE_CHECK(RSCache_Dat2EditPutFile(
        edit, table_id, 9, 1, appended, (uint32_t)(sizeof(appended) - 1)));
    RSCACHE_CHECK(RSCache_Dat2EditCommit(edit, directory));

    RSCache_Dat2EditFree(edit);
    RSCache_Dat2DiskFree(disk);

    disk = RSCache_Dat2DiskNewFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    if( disk )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, 9);
        RSCACHE_CHECK(archive != NULL);
        if( archive )
        {
            RSCACHE_CHECK(RSCache_Dat2DiskArchiveInitMetadata(disk, archive));
            RSCACHE_CHECK_EQ(archive->file_count, 2);
            if( archive->file_count == 2 )
            {
                RSCACHE_CHECK_EQ(archive->file_ids[0], 0);
                RSCACHE_CHECK_EQ(archive->file_ids[1], 1);
            }

            struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            RSCACHE_CHECK(files != NULL);
            if( files )
            {
                RSCACHE_CHECK_EQ(files->file_count, 2);
                RSCACHE_CHECK_EQ(files->file_sizes[0], (int)(sizeof(replaced) - 1));
                RSCACHE_CHECK_EQ(files->file_sizes[1], (int)(sizeof(appended) - 1));
                if( files->file_sizes[0] == (int)(sizeof(replaced) - 1) )
                    RSCACHE_CHECK_BYTES_EQ(
                        files->files[0], replaced, (int)(sizeof(replaced) - 1));
                if( files->file_sizes[1] == (int)(sizeof(appended) - 1) )
                    RSCACHE_CHECK_BYTES_EQ(
                        files->files[1], appended, (int)(sizeof(appended) - 1));
                RSCache_FileListFree(files);
            }

            struct RSCache_ReferenceTable* loaded = disk->tables[table_id];
            RSCACHE_CHECK(loaded != NULL);
            if( loaded )
            {
                RSCACHE_CHECK_EQ(loaded->id_count, 1);
                RSCACHE_CHECK_EQ(loaded->archives[9].children.count, 2);
                RSCACHE_CHECK(loaded->archives[9].version > 1);
            }

            RSCache_Dat2DiskArchiveFree(archive);
        }
        RSCache_Dat2DiskFree(disk);
    }

    cleanup_directory(directory);
}

int
main(void)
{
    printf("cache edit tests\n");
    test_put_file_replace_and_append();
    return rscache_test_report("cache_edit");
}
