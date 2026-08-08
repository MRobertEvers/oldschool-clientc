/* Sparse dat2 and raw-container primitives used by incremental JS5 storage. */
#include "rscache_test.h"

#include <rscache.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define test_mkdir(path) _mkdir(path)
#define test_rmdir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define test_mkdir(path) mkdir((path), 0700)
#define test_rmdir(path) rmdir(path)
#endif

static void
cleanup_directory(const char* directory)
{
    static const int indexes[] = { 2, 7, 255 };
    char path[1024];

    /* RSCache_Dat2DiskWriteArchive caches its .dat2 and .idxN handles keyed on
     * the directory path, and its contract (see dat2disk.c) is that a caller
     * deleting a cache file mid-run drops those handles first. Without this,
     * the next group recreates the same paths, the cached handles still refer
     * to the deleted inodes, and every write lands in a file nothing can read
     * back -- reported as a short read from the group that follows, three
     * checks later and nowhere near the cause. */
    RSCache_Dat2DiskWriteFlush();

    for( size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); ++i )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, indexes[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    (void)remove(path);
    (void)test_rmdir(directory);
}

static int
prepare_directory(const char* directory)
{
    cleanup_directory(directory);
    if( test_mkdir(directory) == 0 )
        return 0;
    return errno == EEXIST ? 0 : -1;
}

static long
file_size(const char* path)
{
    FILE* file = fopen(path, "rb");
    if( !file )
        return -1;
    if( fseek(file, 0, SEEK_END) != 0 )
    {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    fclose(file);
    return size;
}

static bool
make_reference_container(
    int table_version,
    int group_id,
    int group_version,
    uint32_t container_version,
    uint8_t** out,
    int* out_size)
{
    struct RSCache_ReferenceTable table = { 0 };
    int ids[1] = { group_id };
    table.format = 7;
    table.version = table_version;
    table.flags = RSCACHE_REFTABLE_FLAG_SIZES;
    table.id_count = 1;
    table.ids = ids;
    table.archive_count = group_id + 1;
    table.archives = calloc(
        (size_t)table.archive_count, sizeof(struct RSCache_ReferenceTableArchive));
    if( !table.archives )
        return false;

    for( int i = 0; i < table.archive_count; ++i )
        table.archives[i].index = -1;
    table.archives[group_id].index = group_id;
    table.archives[group_id].crc = 0x10203040;
    table.archives[group_id].compressed = 8;
    table.archives[group_id].uncompressed = 3;
    table.archives[group_id].version = group_version;
    table.archives[group_id].children.count = 1;
    table.archives[group_id].children.files =
        calloc(1, sizeof(struct RSCache_ReferenceTableArchiveFile));
    if( !table.archives[group_id].children.files )
    {
        free(table.archives);
        return false;
    }
    table.archives[group_id].children.files[0].id = 0;

    uint32_t encoded_bound = RSCache_ReferenceTableEncodeBound(&table);
    uint8_t* encoded = malloc(encoded_bound);
    uint32_t encoded_size = encoded
                                ? RSCache_ReferenceTableEncode(
                                      &table, encoded, encoded_bound)
                                : 0;
    free(table.archives[group_id].children.files);
    free(table.archives);
    if( encoded_size == 0 )
    {
        free(encoded);
        return false;
    }

    uint32_t container_bound =
        RSCache_ArchiveEncodeBound(encoded_size, RSCACHE_ARCHIVE_COMPRESSION_NONE);
    uint8_t* container = malloc((size_t)container_bound + 4u);
    uint32_t container_size = container
                                  ? RSCache_ArchiveEncode(
                                        container,
                                        container_bound,
                                        encoded,
                                        encoded_size,
                                        RSCACHE_ARCHIVE_COMPRESSION_NONE,
                                        NULL)
                                  : 0;
    free(encoded);
    if( container_size == 0 )
    {
        free(container);
        return false;
    }

    container[container_size] = (uint8_t)(container_version >> 24);
    container[container_size + 1u] = (uint8_t)(container_version >> 16);
    container[container_size + 2u] = (uint8_t)(container_version >> 8);
    container[container_size + 3u] = (uint8_t)container_version;
    *out = container;
    *out_size = (int)container_size + 4;
    return true;
}

static void
test_raw_container_validation(void)
{
    RSCACHE_TEST_GROUP("raw container framing and validation");

    static const uint8_t raw32[] = {
        0, 0, 0, 0, 3, 'a', 'b', 'c', 0x12, 0x34, 0x56, 0x78,
    };
    size_t container_size = 0;
    uint32_t crc = RSCache_Crc32Buffer(raw32, 8);

    RSCACHE_CHECK(RSCache_ArchiveRawContainerLength(
        raw32, sizeof(raw32), &container_size));
    RSCACHE_CHECK_EQ(container_size, 8);
    RSCACHE_CHECK(RSCache_ArchiveRawValidate(
        raw32, sizeof(raw32), crc, 0x12345678u, &container_size));
    RSCACHE_CHECK_EQ(container_size, 8);
    RSCACHE_CHECK(!RSCache_ArchiveRawValidate(
        raw32, sizeof(raw32), crc ^ 1u, 0x12345678u, NULL));
    RSCACHE_CHECK(!RSCache_ArchiveRawValidate(
        raw32, sizeof(raw32), crc, 0x12345679u, NULL));
    RSCACHE_CHECK(!RSCache_ArchiveRawValidate(raw32, 8, crc, 0x12345678u, NULL));

    static const uint8_t raw16[] = {
        0, 0, 0, 0, 3, 'a', 'b', 'c', 0x56, 0x78,
    };
    RSCACHE_CHECK(RSCache_ArchiveRawValidate(
        raw16, sizeof(raw16), crc, 0x12345678u, NULL));

    uint8_t trailing_garbage[sizeof(raw32) + 1u];
    memcpy(trailing_garbage, raw32, sizeof(raw32));
    trailing_garbage[sizeof(raw32)] = 0;
    RSCACHE_CHECK(!RSCache_ArchiveRawValidate(
        trailing_garbage, sizeof(trailing_garbage), crc, 0x12345678u, NULL));

    static const uint8_t compressed[] = {
        2, 0, 0, 0, 3, 0, 0, 0, 7, 1, 2, 3,
    };
    RSCACHE_CHECK(RSCache_ArchiveRawContainerLength(
        compressed, sizeof(compressed), &container_size));
    RSCACHE_CHECK_EQ(container_size, sizeof(compressed));

    static const uint8_t truncated[] = { 2, 0, 0, 0, 4, 0, 0, 0, 7, 1, 2, 3 };
    RSCACHE_CHECK(!RSCache_ArchiveRawContainerLength(
        truncated, sizeof(truncated), &container_size));
    static const uint8_t unknown[] = { 3, 0, 0, 0, 0 };
    RSCACHE_CHECK(!RSCache_ArchiveRawContainerLength(
        unknown, sizeof(unknown), &container_size));
    RSCACHE_CHECK(!RSCache_ArchiveRawContainerLength(raw32, sizeof(raw32), NULL));
}

static void
test_sparse_open_and_raw_read(const char* directory)
{
    RSCACHE_TEST_GROUP("non-truncating sparse open and exact raw read");

    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    static const uint8_t marker[] = { 0xde, 0xad, 0xbe, 0xef, 0x55 };
    FILE* seed = fopen(path, "wb");
    RSCACHE_CHECK(seed != NULL);
    if( !seed )
        return;
    RSCACHE_CHECK_EQ(fwrite(marker, 1, sizeof(marker), seed), sizeof(marker));
    fclose(seed);

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewSparseFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    RSCACHE_CHECK_EQ(file_size(path), sizeof(marker));
    if( !disk )
        return;

    uint8_t observed[sizeof(marker)] = { 0 };
    FILE* check = fopen(path, "rb");
    RSCACHE_CHECK(check != NULL);
    if( check )
    {
        RSCACHE_CHECK_EQ(fread(observed, 1, sizeof(observed), check), sizeof(observed));
        fclose(check);
        RSCACHE_CHECK_BYTES_EQ(observed, marker, sizeof(marker));
    }

    static const uint8_t raw[] = {
        0, 0, 0, 0, 3, 'j', 's', '5', 0x01, 0x23, 0x45, 0x67,
    };
    RSCACHE_CHECK_EQ(
        RSCache_Dat2DiskWriteArchive(directory, 7, 42, raw, (int)sizeof(raw)), 0);

    struct RSCache_Dat2DiskArchive* loaded =
        RSCache_Dat2DiskArchiveNewLoadRaw(disk, 7, 42);
    RSCACHE_CHECK(loaded != NULL);
    if( loaded )
    {
        RSCACHE_CHECK_EQ(loaded->table_id, 7);
        RSCACHE_CHECK_EQ(loaded->archive_id, 42);
        RSCACHE_CHECK_EQ(loaded->file_count, -1);
        RSCACHE_CHECK_EQ(loaded->data_size, sizeof(raw));
        RSCACHE_CHECK_BYTES_EQ(loaded->data, raw, sizeof(raw));
        RSCache_Dat2DiskArchiveFree(loaded);
    }

    loaded = RSCache_Dat2DiskArchiveNewLoad(disk, 7, 42);
    RSCACHE_CHECK(loaded != NULL);
    if( loaded )
    {
        RSCACHE_CHECK_EQ(loaded->file_count, 0);
        RSCACHE_CHECK_EQ(loaded->data_size, 3);
        RSCACHE_CHECK_BYTES_EQ(loaded->data, "js5", 3);
        RSCACHE_CHECK_EQ((uint32_t)loaded->revision, 0x01234567u);
        RSCache_Dat2DiskArchiveFree(loaded);
    }

    RSCache_Dat2DiskFree(disk);
}

static void
test_reference_install(const char* directory)
{
    RSCACHE_TEST_GROUP("raw reference-table install and replacement");

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewSparseFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    if( !disk )
        return;

    const int table_id = RSCACHE_DAT2_OSRS_TABLE_CONFIGS;
    uint8_t* first = NULL;
    int first_size = 0;
    RSCACHE_CHECK(make_reference_container(
        17, 9, 0x10203040, 0x55667788u, &first, &first_size));
    if( !first )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    RSCACHE_CHECK(RSCache_Dat2DiskInstallReferenceTableRaw(
        disk, table_id, first, first_size));
    RSCACHE_CHECK(disk->tables[table_id] != NULL);
    if( disk->tables[table_id] )
    {
        RSCACHE_CHECK_EQ(disk->tables[table_id]->version, 17);
        RSCACHE_CHECK_EQ(disk->tables[table_id]->id_count, 1);
        RSCACHE_CHECK_EQ(disk->tables[table_id]->ids[0], 9);
        RSCACHE_CHECK_EQ(
            (uint32_t)disk->tables[table_id]->archives[9].version, 0x10203040u);
    }

    char idx_path[1024];
    snprintf(idx_path, sizeof(idx_path), "%s/main_file_cache.idx%d", directory, table_id);
    RSCACHE_CHECK_EQ(file_size(idx_path), 0);

    struct RSCache_Dat2DiskArchive* raw = RSCache_Dat2DiskArchiveNewLoadRaw(
        disk, RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID, table_id);
    RSCACHE_CHECK(raw != NULL);
    if( raw )
    {
        RSCACHE_CHECK_EQ(raw->data_size, first_size);
        RSCACHE_CHECK_BYTES_EQ(raw->data, first, first_size);
        RSCache_Dat2DiskArchiveFree(raw);
    }

    static const uint8_t group[] = { 0, 0, 0, 0, 1, 0x7f, 0, 0, 0, 1 };
    RSCACHE_CHECK_EQ(
        RSCache_Dat2DiskWriteArchive(directory, table_id, 9, group, sizeof(group)), 0);
    long populated_idx_size = file_size(idx_path);
    RSCACHE_CHECK(populated_idx_size > 0);

    uint8_t* second = NULL;
    int second_size = 0;
    RSCACHE_CHECK(make_reference_container(
        18, 10, 0x50607080, 0x99aabbccu, &second, &second_size));
    RSCACHE_CHECK(second != NULL);
    if( second )
    {
        RSCACHE_CHECK(RSCache_Dat2DiskInstallReferenceTableRaw(
            disk, table_id, second, second_size));
        RSCACHE_CHECK_EQ(file_size(idx_path), populated_idx_size);
        RSCACHE_CHECK(disk->tables[table_id] != NULL);
        if( disk->tables[table_id] )
        {
            RSCACHE_CHECK_EQ(disk->tables[table_id]->version, 18);
            RSCACHE_CHECK_EQ(disk->tables[table_id]->ids[0], 10);
        }

        struct RSCache_ReferenceTable* installed = disk->tables[table_id];
        static const uint8_t invalid_reference[] = {
            0, 0, 0, 0, 1, 4, 0, 0, 0, 1,
        };
        RSCACHE_CHECK(!RSCache_Dat2DiskInstallReferenceTableRaw(
            disk,
            table_id,
            invalid_reference,
            (int)sizeof(invalid_reference)));
        RSCACHE_CHECK(disk->tables[table_id] == installed);
        RSCACHE_CHECK_EQ(disk->tables[table_id]->version, 18);
    }

    free(first);
    free(second);
    RSCache_Dat2DiskFree(disk);

    disk = RSCache_Dat2DiskNewFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    if( disk )
    {
        RSCACHE_CHECK(disk->tables[table_id] != NULL);
        if( disk->tables[table_id] )
        {
            RSCACHE_CHECK_EQ(disk->tables[table_id]->version, 18);
            RSCACHE_CHECK_EQ(disk->tables[table_id]->ids[0], 10);
        }
        RSCache_Dat2DiskFree(disk);
    }

    disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    if( disk )
    {
        static const uint8_t rejected_write[] = { 0, 0, 0, 0, 0 };
        RSCACHE_CHECK_EQ(disk->read_only, 1);
        raw = RSCache_Dat2DiskArchiveNewLoadRaw(
            disk, RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID, table_id);
        RSCACHE_CHECK(raw != NULL);
        RSCache_Dat2DiskArchiveFree(raw);
        RSCACHE_CHECK(!RSCache_Dat2DiskInstallReferenceTableRaw(
            disk, table_id, rejected_write, (int)sizeof(rejected_write)));
        RSCache_Dat2DiskFree(disk);
    }
}

int
main(void)
{
    printf("sparse cache tests\n");
    test_raw_container_validation();

    const char* directory = getenv("RSCACHE_TEST_TMPDIR");
#ifdef _WIN32
    char fallback[] = "rscache_test_sparse_tmp";
#else
    char fallback[] = "/tmp/rscache_test_sparse";
#endif
    if( !directory )
        directory = fallback;

    if( prepare_directory(directory) != 0 )
    {
        printf("   SKIP could not prepare %s\n", directory);
        return rscache_test_report("sparse");
    }

    test_sparse_open_and_raw_read(directory);
    cleanup_directory(directory);
    if( prepare_directory(directory) == 0 )
        test_reference_install(directory);
    cleanup_directory(directory);
    return rscache_test_report("sparse");
}
