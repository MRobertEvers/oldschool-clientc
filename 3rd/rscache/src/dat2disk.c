#include "dat2disk.h"

#include "archive.h"
#include "reference_table.h"
#include "rscache_profile.h"
#include "xtea_config.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 520
#define INDEX_ENTRY_SIZE 6

static int
dat2disk_seek_read(
    FILE* file,
    uint64_t offset)
{
    if( !file )
        return -1;
#ifdef _WIN32
    if( offset > INT64_MAX )
        return -1;
    return _fseeki64(file, (__int64)offset, SEEK_SET);
#else
    if( offset > (uint64_t)LONG_MAX )
        return -1;
    return fseek(file, (long)offset, SEEK_SET);
#endif
}

static void
read_sector_header_small(
    struct RSCache_Dat2DiskSectorHeader* header,
    uint8_t* data)
{
    header->archive_id = ((data[0] & 0xFF) << 8) | (data[1] & 0xFF);
    header->part_no = ((data[2] & 0xFF) << 8) | (data[3] & 0xFF);
    header->next_sector_no = ((data[4] & 0xFF) << 16) | ((data[5] & 0xFF) << 8) | (data[6] & 0xFF);
    header->index_id = data[7] & 0xFF;
}

static void
write_sector_header_small(
    struct RSCache_Dat2DiskSectorHeader* header,
    uint8_t* data)
{
    // Small archive format (8 byte header)
    // Archive (2 bytes), CurrentPart (2 bytes), NextSector (3 bytes), Table (1 byte)
    data[0] = (header->archive_id >> 8) & 0xFF;
    data[1] = header->archive_id & 0xFF;
    data[2] = (header->part_no >> 8) & 0xFF;
    data[3] = header->part_no & 0xFF;
    data[4] = (header->next_sector_no >> 16) & 0xFF;
    data[5] = (header->next_sector_no >> 8) & 0xFF;
    data[6] = header->next_sector_no & 0xFF;
    data[7] = header->index_id & 0xFF;
}

static void
read_sector_header_large(
    struct RSCache_Dat2DiskSectorHeader* header,
    uint8_t* data)
{
    header->archive_id = ((data[0] & 0xFF) << 24) | ((data[1] & 0xFF) << 16) |
                         ((data[2] & 0xFF) << 8) | (data[3] & 0xFF);
    header->part_no = ((data[4] & 0xFF) << 8) + (data[5] & 0xFF);
    header->next_sector_no = ((data[6] & 0xFF) << 16) | ((data[7] & 0xFF) << 8) | (data[8] & 0xFF);
    header->index_id = data[9] & 0xFF;
}

static void
write_sector_header_large(
    struct RSCache_Dat2DiskSectorHeader* header,
    uint8_t* data)
{
    // Large archive format (10 byte header)
    // Archive (4 bytes), CurrentPart (2 bytes), NextSector (3 bytes), Table (1 byte)
    data[0] = (header->archive_id >> 24) & 0xFF;
    data[1] = (header->archive_id >> 16) & 0xFF;
    data[2] = (header->archive_id >> 8) & 0xFF;
    data[3] = header->archive_id & 0xFF;
    data[4] = (header->part_no >> 8) & 0xFF;
    data[5] = header->part_no & 0xFF;
    data[6] = (header->next_sector_no >> 16) & 0xFF;
    data[7] = (header->next_sector_no >> 8) & 0xFF;
    data[8] = header->next_sector_no & 0xFF;
    data[9] = header->index_id & 0xFF;
}

static int
header_size_for_archive(int archive_id)
{
    return archive_id > 0xFFFF ? 10 : 8;
}

static void
read_sector_header(
    struct RSCache_Dat2DiskSectorHeader* header,
    int archive_id,
    uint8_t* data,
    int data_size)
{
    if( archive_id > 0xFFFF )
    {
        assert(data_size >= 10);
        read_sector_header_large(header, data);
    }
    else
    {
        assert(data_size >= 8);
        read_sector_header_small(header, data);
    }
}

static void
write_sector_header(
    struct RSCache_Dat2DiskSectorHeader* header,
    uint8_t* data,
    int data_size)
{
    if( header->archive_id > 0xFFFF )
    {
        assert(data_size >= 10);
        write_sector_header_large(header, data);
    }
    else
    {
        assert(data_size >= 8);
        write_sector_header_small(header, data);
    }
}

int
RSCache_Dat2DiskDat2FileReadArchive(
    FILE* dat2_file,
    // This is a sanity check. I.e. if you run off the end of the index's dat2 file,
    // this will catch an error.
    int idx_file_id,
    int archive_id,
    int sector,
    int length,
    struct RSCache_Dat2DiskArchive* archive)
{
    uint8_t read_buffer[SECTOR_SIZE];
    int data_block_size;

    int header_size = header_size_for_archive(archive_id);
    struct RSCache_Dat2DiskSectorHeader header = { 0 };
    header.archive_id = archive_id;
    header.index_id = idx_file_id;
    header.part_no = 0;
    header.next_sector_no = 0;

    char* out = NULL;

    if( sector <= 0L )
    {
        printf("bad read, dat length %d, requested sector %d", length, sector);
        goto error;
    }

    int out_len = 0;
    out = malloc((size_t)length);
    if( !out )
        goto error;
    memset(out, 0, length);

    for( int part = 0, read_bytes_count = 0; length > read_bytes_count;
         sector = header.next_sector_no )
    {
        if( sector == 0 )
        {
            printf("Unexpected end of file\n");
            goto error;
        }

        if( dat2disk_seek_read(
                dat2_file, (uint64_t)(uint32_t)sector * (uint64_t)SECTOR_SIZE) != 0 )
            goto error;

        data_block_size = length - read_bytes_count;

        // Archives are stored on disk in a linked list of sectors.
        // [Part 0] -> [Part 1] -> [Part 2] -> ...
        //
        // sectors need not be contiguous on disk.
        // e.g.
        // DISK
        // 1: Archive X, Part 5
        // 2: Archive Y, Part 0
        // 3: Archive Y, Part 1
        // 4: Archive X, Part 6
        //
        //
        // Each archive has an id.
        // Each archive id in the header MUST match the archive id.
        //
        // Each archive belongs to a table (MODELS, ANIMS, etc.).
        // The table is stored in the "Table" field.
        // This MUST be the same for all parts of the archive.
        //
        //  Archives >= 16
        //
        //  0                   1                   2                   3
        //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |                            Archive                            |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |          CurrentPart          |           NextSector          |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |               |     Table     |                               |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
        // |                            Payload (510 bytes)                |
        // ...                           (510 bytes)                     ...
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //
        //
        //  Archives < 16
        //
        //  0                   1                   2                   3
        //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //  |            Archive            |          CurrentPart          |
        //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //  |                   NextSector                  |     Table     |
        //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //  |                                                               |
        //  +                            Payload                            +
        //  |                                                               |
        //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

        if( data_block_size > SECTOR_SIZE - header_size )
            data_block_size = SECTOR_SIZE - header_size;

        int bytes_read = fread(read_buffer, 1, header_size + data_block_size, dat2_file);
        if( bytes_read < header_size + data_block_size )
        {
            printf("short read when reading file data for %d/%d\n", archive_id, header.index_id);
            goto error;
        }

        read_sector_header(&header, archive_id, read_buffer, bytes_read);

        // Safety check that we are still reading the correct archive.
        if( archive_id != header.archive_id || header.part_no != part ||
            idx_file_id != header.index_id )
        {
            printf(
                "data mismatch %d != %d, %d != %d, %d != %d\n",
                archive_id,
                header.archive_id,
                part,
                header.part_no,
                idx_file_id,
                header.index_id);
            goto error;
        }

        if( header.next_sector_no < 0 )
        {
            printf("invalid next sector");
            goto error;
        }

        memcpy(out + out_len, read_buffer + header_size, data_block_size);
        out_len += data_block_size;

        read_bytes_count += data_block_size;

        ++part;
    }

    archive->data = out;
    archive->data_size = out_len;
    archive->archive_id = archive_id;
    return 0;

error:
    if( out )
        free(out);
    return -1;
}

int
RSCache_Dat2DiskDatFileReadArchive(
    FILE* dat_file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct RSCache_Dat2DiskArchive* archive)
{
    // DAT file indexes are named 0 offset, but accessed as 1 offset.
    // e.g. index_id 0 is the first archive, but accessed as 1.
    return RSCache_Dat2DiskDat2FileReadArchive(
        dat_file, index_id + 1, archive_id, start_sector, length_bytes, archive);
}

int
RSCache_Dat2DiskDat2FileAppendArchive(
    FILE* file,
    int index_id,
    int archive_id,
    uint8_t* data,
    int data_size)
{
    uint8_t sector_data[SECTOR_SIZE];

    // Fill file to next SECTOR_SIZE boundary
    fseek(file, 0, SEEK_END);
    long data_file_size = ftell(file);

    int padding = 0;

    if( data_file_size % SECTOR_SIZE != 0 )
    {
        padding = SECTOR_SIZE - (data_file_size % SECTOR_SIZE);
        uint8_t zeros[SECTOR_SIZE] = { 0 };
        if( fwrite(zeros, 1, (size_t)padding, file) != (size_t)padding )
        {
            printf("failed to write padding\n");
            assert(false);
            return -1;
        }
    }

    int sector_no = (data_file_size + padding) / SECTOR_SIZE;

    // Determine header size and payload size based on archive_id
    int header_size = header_size_for_archive(archive_id);
    int payload_size = SECTOR_SIZE - header_size;
    int bytes_written = 0;

    struct RSCache_Dat2DiskSectorHeader header = { 0 };
    header.archive_id = archive_id;
    header.index_id = index_id;
    header.part_no = 0;
    header.next_sector_no = sector_no + 1;

    fseek(file, sector_no * SECTOR_SIZE, SEEK_SET);
    while( bytes_written < data_size )
    {
        memset(sector_data, 0, SECTOR_SIZE);

        // Calculate how many bytes to write in this sector
        int bytes_to_write = data_size - bytes_written;
        if( bytes_to_write > payload_size )
            bytes_to_write = payload_size;

        // Write header based on archive_id
        write_sector_header(&header, sector_data, header_size);

        // Copy payload data
        memcpy(sector_data + header_size, data + bytes_written, bytes_to_write);

        // Write the sector
        int written_bytes = fwrite(sector_data, 1, SECTOR_SIZE, file);
        if( written_bytes != SECTOR_SIZE )
        {
            printf("failed to write sector\n");
            assert(false);
            return -1;
        }

        bytes_written += bytes_to_write;
        header.part_no++;
        header.next_sector_no++;
    }

    return sector_no;
}

/*
 * Open-handle cache for the write path.
 *
 * This function used to open and close BOTH the .dat2 and the .idxN on every
 * call. A pack writes one archive per record and one per asset — six figures of
 * them — so that was ~4 opens and closes per archive, and on Windows an open is
 * not cheap: path resolution, a security check, and the on-access virus scan.
 *
 * Archives are written table by table, so caching one handle of each hits
 * essentially every time. What is NOT cached is correctness-relevant: every
 * write is still followed by fflush, so the bytes reach the OS and a separate
 * FILE* opened on the same path — which is how the reference-table loader reads
 * — sees them. Only the open/close disappears.
 *
 * Callers that delete or move a cache file mid-run must call
 * RSCache_Dat2DiskWriteFlush first, or the handle will point at a file that is
 * no longer the one on disk (and on Windows the delete will simply fail).
 */
static struct
{
    char directory[1024];
    FILE* file;
} g_dat2_write;

static struct
{
    char directory[1024];
    int table_id;
    FILE* file;
} g_idx_write;

void
RSCache_Dat2DiskWriteFlush(void)
{
    if( g_dat2_write.file )
    {
        fclose(g_dat2_write.file);
        g_dat2_write.file = NULL;
        g_dat2_write.directory[0] = '\0';
    }
    if( g_idx_write.file )
    {
        fclose(g_idx_write.file);
        g_idx_write.file = NULL;
        g_idx_write.directory[0] = '\0';
        g_idx_write.table_id = -1;
    }
}

/* "r+b" then fall back to "w+b": opening an existing cache for update must not
 * truncate it, but a fresh one has to be created. */
static FILE*
dat2disk_open_rw(const char* path)
{
    FILE* f = fopen(path, "r+b");

    if( !f )
        f = fopen(path, "w+b");
    return f;
}

static FILE*
dat2disk_dat2_handle(const char* directory)
{
    char path[1024];

    if( g_dat2_write.file && strcmp(g_dat2_write.directory, directory) == 0 )
        return g_dat2_write.file;

    if( g_dat2_write.file )
    {
        fclose(g_dat2_write.file);
        g_dat2_write.file = NULL;
    }

    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    g_dat2_write.file = dat2disk_open_rw(path);
    if( !g_dat2_write.file )
        return NULL;
    snprintf(g_dat2_write.directory, sizeof(g_dat2_write.directory), "%s", directory);
    return g_dat2_write.file;
}

static FILE*
dat2disk_idx_handle(const char* directory, int table_id)
{
    char path[1024];

    if( g_idx_write.file && g_idx_write.table_id == table_id &&
        strcmp(g_idx_write.directory, directory) == 0 )
        return g_idx_write.file;

    if( g_idx_write.file )
    {
        fclose(g_idx_write.file);
        g_idx_write.file = NULL;
    }

    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, table_id);
    g_idx_write.file = dat2disk_open_rw(path);
    if( !g_idx_write.file )
        return NULL;
    snprintf(g_idx_write.directory, sizeof(g_idx_write.directory), "%s", directory);
    g_idx_write.table_id = table_id;
    return g_idx_write.file;
}

int
RSCache_Dat2DiskWriteArchive(
    const char* directory,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size)
{
    if( !directory || !data || data_size <= 0 || archive_id < 0 )
        return -1;

    FILE* dat2_file = dat2disk_dat2_handle(directory);
    if( !dat2_file )
        return -1;

    /* Reserve sector 0 on a fresh file. The index reader treats sector 0 as
     * "absent", so an archive placed there would be invisible. */
    fseek(dat2_file, 0, SEEK_END);
    if( ftell(dat2_file) == 0 )
    {
        uint8_t reserved[SECTOR_SIZE] = { 0 };
        if( fwrite(reserved, 1, sizeof(reserved), dat2_file) != sizeof(reserved) )
        {
            RSCache_Dat2DiskWriteFlush();
            return -1;
        }
    }

    int sector = RSCache_Dat2DiskDat2FileAppendArchive(
        dat2_file, table_id, archive_id, (uint8_t*)data, data_size);
    /* Flush rather than close: the bytes have to be visible to a reader opening
     * this path separately, but the handle is worth keeping. */
    fflush(dat2_file);

    if( sector <= 0 )
        return -1;

    FILE* index_file = dat2disk_idx_handle(directory, table_id);
    if( !index_file )
        return -1;

    struct RSCache_Dat2DiskIndexRecord record = {
        .idx_file_id = table_id,
        .archive_idx = archive_id,
        .sector = sector,
        .length = data_size,
    };

    int result = RSCache_Dat2DiskIndexFileWriteRecord(index_file, archive_id, &record);
    fflush(index_file);
    return result == 0 ? 0 : -1;
}

int
RSCache_Dat2DiskWriteArchiveTo(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size)
{
    if( !disk || disk->read_only || !disk->store.put )
        return -1;
    return disk->store.put(disk->store.user, table_id, archive_id, data, data_size);
}

int
RSCache_Dat2DiskIndexFileReadRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_Dat2DiskIndexRecord* record)
{
    char data[INDEX_ENTRY_SIZE] = { 0 };
    int ret = 0;

    if( entry_idx < 0 )
        return -1;
    ret = dat2disk_seek_read(
        file, (uint64_t)(uint32_t)entry_idx * (uint64_t)INDEX_ENTRY_SIZE);
    if( ret != 0 )
    {
        ret = ferror(file);
        printf("failed to seek index record err: %d\n", ret);
        return -1;
    }

    // ret = ftell(file);
    // printf("current file pos: %d\n", ret);

    ret = fread(data, INDEX_ENTRY_SIZE, 1, file);
    if( ret != 1 )
    {
        /* A short/missing idx record is the ordinary sparse-cache miss. Do not
         * print once per group while JS5 scans an empty table. */
        return -1;
    }

    // 	int length = ((buffer[0] & 0xFF) << 16) | ((buffer[1] & 0xFF) << 8) | (buffer[2] & 0xFF);
    // int sector = ((buffer[3] & 0xFF) << 16) | ((buffer[4] & 0xFF) << 8) | (buffer[5] & 0xFF);

    // Convert Java:
    // int length = ((buffer[0] & 0xFF) << 16) | ((buffer[1] & 0xFF) << 8) | (buffer[2] & 0xFF);
    // int sector = ((buffer[3] & 0xFF) << 16) | ((buffer[4] & 0xFF) << 8) | (buffer[5] & 0xFF);

    // Read 3 bytes for length and 3 bytes for sector
    // Need to mask with 0xFF to handle sign extension when converting to int
    int length = ((data[0] & 0xFF) << 16) | ((data[1] & 0xFF) << 8) | (data[2] & 0xFF);

    int sector = ((data[3] & 0xFF) << 16) | ((data[4] & 0xFF) << 8) | (data[5] & 0xFF);

    if( length <= 0 || sector <= 0 )
        return -1;

    record->length = length;
    record->sector = sector;
    record->archive_idx = entry_idx;
    record->idx_file_id = -1;

    return 0;
}

int
RSCache_Dat2DiskIndexFileWriteRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_Dat2DiskIndexRecord* record)
{
    // If the desired write offset is past EOF, fill the gap with zeros.
    char data[INDEX_ENTRY_SIZE] = { 0 };

    long offset = entry_idx * INDEX_ENTRY_SIZE;
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);

    if( offset > file_size )
    {
        // Need to fill gap with zeros
        long gap = offset - file_size;
        if( gap > 0 )
        {
            // Seek to end again just to be sure
            fseek(file, 0, SEEK_END);
            // Write zeros
            while( gap > 0 )
            {
                size_t to_write = gap > INDEX_ENTRY_SIZE ? INDEX_ENTRY_SIZE : gap;
                fwrite(data, 1, to_write, file);
                gap -= to_write;
            }
        }
    }

    data[0] = (record->length >> 16) & 0xFF;
    data[1] = (record->length >> 8) & 0xFF;
    data[2] = record->length & 0xFF;
    data[3] = (record->sector >> 16) & 0xFF;
    data[4] = (record->sector >> 8) & 0xFF;
    data[5] = record->sector & 0xFF;

    // seek to end
    fseek(file, entry_idx * INDEX_ENTRY_SIZE, SEEK_SET);
    int written_bytes = fwrite(data, INDEX_ENTRY_SIZE, 1, file);
    if( written_bytes != 1 )
    {
        printf("failed to write index record\n");
        assert(false);
        return -1;
    }
    return 0;
}

static FILE*
dat2disk_fopen_index(
    char const* directory,
    int table_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, table_id);
    return fopen(path, "rb");
}

/* Slot in `disk->index_files` for a table id, or -1 for an id that has no index
 * file at all.
 *
 * The reference table is the reason this is not just the table id. Reads of
 * idx255 go through exactly the same path as reads of idx0..idx36, but 255 is
 * outside the range RSCache_Dat2DiskIsValidTableId admits — that predicate
 * bounds the *reference-table array*, which is indexed by the table a reference
 * table describes, and 255 is the file they are all stored in rather than one of
 * the things described. So it gets the slot past the end. */
static int
dat2disk_index_slot(int table_id)
{
    if( table_id == RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID )
        return RSCACHE_DAT2_DISK_TABLE_CAPACITY;
    if( RSCache_Dat2DiskIsValidTableId(table_id) )
        return table_id;
    return -1;
}

/* The disk's read handle on idxN, opened on first use. NULL means "no such
 * table", which is an ordinary answer here and not cached: a table can be
 * created later by commit_table, and a disk that answered "absent" once must
 * not keep saying so. */
static FILE*
dat2disk_disk_index_handle(struct RSCache_Dat2Disk* disk, int table_id)
{
    assert(disk);
    assert(disk->directory);

    int slot = dat2disk_index_slot(table_id);
    if( slot < 0 )
        return NULL;
    if( !disk->index_files[slot] )
        disk->index_files[slot] = dat2disk_fopen_index(disk->directory, table_id);
    return disk->index_files[slot];
}

/* Drop one cached read handle. Called where this disk changes a table under
 * itself, so the next read reopens rather than serving what stdio buffered
 * before the write. `table_id < 0` drops every slot. */
static void
dat2disk_disk_index_forget(struct RSCache_Dat2Disk* disk, int table_id)
{
    assert(disk);

    if( table_id >= 0 )
    {
        int slot = dat2disk_index_slot(table_id);
        if( slot >= 0 && disk->index_files[slot] )
        {
            fclose(disk->index_files[slot]);
            disk->index_files[slot] = NULL;
        }
        return;
    }

    for( int i = 0; i <= RSCACHE_DAT2_DISK_TABLE_CAPACITY; ++i )
    {
        if( disk->index_files[i] )
        {
            fclose(disk->index_files[i]);
            disk->index_files[i] = NULL;
        }
    }
}

static int
dat2disk_read_index(
    struct RSCache_Dat2DiskIndexRecord* record,
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int entry_idx)
{
    assert(record);
    assert(disk);

    FILE* index_file = dat2disk_disk_index_handle(disk, table_id);
    if( !index_file )
        return -1;

    if( RSCache_Dat2DiskIndexFileReadRecord(index_file, entry_idx, record) != 0 )
        return -1;

    record->idx_file_id = table_id;
    return 0;
}

/* --- the dat2-file backing ------------------------------------------------
 *
 * The sector-chained container, expressed as one implementation of
 * RSCache_Dat2Store rather than as the thing the disk *is*. Everything it needs
 * is already on the disk (directory, dat2 handle, read-only flag), so `user` is
 * the disk itself and there is no separate allocation to own or free.
 */

static int
dat2_file_store_get(
    void* user,
    int table_id,
    int archive_id,
    uint8_t** data,
    int* size)
{
    struct RSCache_Dat2Disk* disk = (struct RSCache_Dat2Disk*)user;
    struct RSCache_Dat2DiskIndexRecord index_record = { 0 };
    struct RSCache_Dat2DiskArchive archive;

    if( !disk || !disk->directory || !disk->dat2_file )
        return -1;
    /* An idx entry that is not there is the ordinary "no such archive", which
     * is 0 and not an error: an incremental cache is mostly holes. */
    if( dat2disk_read_index(&index_record, disk, table_id, archive_id) != 0 )
        return 0;

    memset(&archive, 0, sizeof(archive));
    if( RSCache_Dat2DiskDat2FileReadArchive(
            disk->dat2_file,
            index_record.idx_file_id,
            index_record.archive_idx,
            index_record.sector,
            index_record.length,
            &archive) != 0 )
    {
        free(archive.data);
        return -1;
    }
    /* A record the index points at but whose chain yielded nothing is a
     * corrupt cache, not an absent archive. */
    if( !archive.data || archive.data_size <= 0 )
    {
        free(archive.data);
        return -1;
    }

    *data = (uint8_t*)archive.data;
    *size = archive.data_size;
    return 1;
}

static int
dat2_file_store_put(
    void* user,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int size)
{
    struct RSCache_Dat2Disk* disk = (struct RSCache_Dat2Disk*)user;
    int rc;

    if( !disk || disk->read_only || !disk->directory )
        return -1;
    rc = RSCache_Dat2DiskWriteArchive(disk->directory, table_id, archive_id, data, size);
    /* The write went through its own stream (dat2disk_idx_handle's rw cache), so
     * this disk's read handle on the same idxN may be holding bytes from before
     * it. Drop it whether or not the write succeeded — a partial write leaves the
     * same staleness. */
    dat2disk_disk_index_forget(disk, table_id);
    return rc;
}

static int
dat2_file_store_has_table(
    void* user,
    int table_id)
{
    struct RSCache_Dat2Disk* disk = (struct RSCache_Dat2Disk*)user;

    if( !disk || !disk->directory )
        return 0;
    /* Through the cache: presence is the same question the read path asks, and a
     * table that exists is about to be read from anyway. A missing table is not
     * cached, so a later commit_table still becomes visible here. */
    return dat2disk_disk_index_handle(disk, table_id) != NULL;
}

/*
 * Bring the side structures a reference-table write leaves stale back in line.
 *
 * Two of them, both artifacts of the file layout. The zero-byte idxN is the
 * presence sentinel has_table reads, and a table written without one is
 * invisible to the next open. The dat2 reopen is because the write went through
 * its own FILE stream, so this disk's reader may be holding an input buffer or
 * end-of-file state from before the append.
 *
 * The cached idxN read handle is dropped for the same reason as the dat2 reopen,
 * and additionally because this may be the call that creates idxN: a handle
 * opened before it existed would be NULL, and a handle opened on the file this
 * touches would predate the reference-table write.
 */
static int
dat2_file_store_commit_table(
    void* user,
    int table_id)
{
    struct RSCache_Dat2Disk* disk = (struct RSCache_Dat2Disk*)user;
    char path[1024];
    int path_size;
    FILE* sentinel;
    FILE* refreshed;

    if( !disk || !disk->directory )
        return -1;

    dat2disk_disk_index_forget(disk, table_id);

    path_size =
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", disk->directory, table_id);
    if( path_size < 0 || (size_t)path_size >= sizeof(path) )
        return -1;
    /* Append mode creates a missing file without destroying groups already
     * cached in table N. */
    sentinel = fopen(path, "ab");
    if( !sentinel )
        return -1;
    if( fclose(sentinel) != 0 )
        return -1;

    path_size = snprintf(path, sizeof(path), "%s/main_file_cache.dat2", disk->directory);
    if( path_size < 0 || (size_t)path_size >= sizeof(path) )
        return -1;
    refreshed = fopen(path, "rb+");
    if( !refreshed )
        return -1;
    if( disk->dat2_file )
        fclose(disk->dat2_file);
    disk->dat2_file = refreshed;
    return 0;
}

struct RSCache_Dat2Store
RSCache_Dat2DiskFileStore(struct RSCache_Dat2Disk* disk)
{
    struct RSCache_Dat2Store store;

    memset(&store, 0, sizeof(store));
    store.user = disk;
    store.get = dat2_file_store_get;
    /* No put on a read-only disk: refusing at the vtable is what makes the
     * promise NewReadOnlyFromDirectory advertises structural rather than a
     * flag every writer has to remember to test. */
    store.put = (disk && disk->read_only) ? NULL : dat2_file_store_put;
    store.has_table = dat2_file_store_has_table;
    store.commit_table = dat2_file_store_commit_table;
    return store;
}

bool
RSCache_Dat2DiskIsValidTableId(int table_id)
{
    /*
     * Any table the container can address.
     *
     * This used to be an allow-list of the *named* OSRS enum values, which silently made
     * whole tables unreachable rather than merely absent: `init_reference_tables` skips a
     * rejected id before attempting the load, so nothing is even logged. Tables 16 and 17
     * — where the RS2 branch keeps locs and enums — decode perfectly well once past this
     * check, so the list was the only thing hiding them.
     *
     * A table that genuinely is not in a cache already returns NULL from the reference
     * table load, which callers handle, so gating on a name bought nothing and cost a
     * generation of support.
     */
    return table_id >= 0 && table_id < RSCACHE_DAT2_DISK_TABLE_CAPACITY;
}

/*
 * Two tables, one per branch, indexed by logical table.
 *
 * Written as full tables rather than "OSRS ids with a few RS2 overrides" on purpose.
 * An override list makes the shared 0..15 block an inherited default, which is exactly
 * the coupling the separate enums exist to remove: the day a branch renumbers one of
 * those, the override list is silently wrong for it while still compiling.
 */
static const int DAT2_TABLE_IDS_OSRS[RSCACHE_DAT2_TABLE_COUNT] = {
    [RSCACHE_DAT2_TABLE_ANIMATIONS] = RSCACHE_DAT2_OSRS_TABLE_ANIMATIONS,
    [RSCACHE_DAT2_TABLE_SKELETONS] = RSCACHE_DAT2_OSRS_TABLE_SKELETONS,
    [RSCACHE_DAT2_TABLE_CONFIGS] = RSCACHE_DAT2_OSRS_TABLE_CONFIGS,
    [RSCACHE_DAT2_TABLE_INTERFACES] = RSCACHE_DAT2_OSRS_TABLE_INTERFACES,
    [RSCACHE_DAT2_TABLE_SOUND_EFFECTS] = RSCACHE_DAT2_OSRS_TABLE_SOUND_EFFECTS,
    [RSCACHE_DAT2_TABLE_MAPS] = RSCACHE_DAT2_OSRS_TABLE_MAPS,
    [RSCACHE_DAT2_TABLE_MUSIC_TRACKS] = RSCACHE_DAT2_OSRS_TABLE_MUSIC_TRACKS,
    [RSCACHE_DAT2_TABLE_MODELS] = RSCACHE_DAT2_OSRS_TABLE_MODELS,
    [RSCACHE_DAT2_TABLE_SPRITES] = RSCACHE_DAT2_OSRS_TABLE_SPRITES,
    [RSCACHE_DAT2_TABLE_TEXTURES] = RSCACHE_DAT2_OSRS_TABLE_TEXTURES,
    [RSCACHE_DAT2_TABLE_BINARY] = RSCACHE_DAT2_OSRS_TABLE_BINARY,
    [RSCACHE_DAT2_TABLE_MUSIC_JINGLES] = RSCACHE_DAT2_OSRS_TABLE_MUSIC_JINGLES,
    [RSCACHE_DAT2_TABLE_CLIENTSCRIPT] = RSCACHE_DAT2_OSRS_TABLE_CLIENTSCRIPT,
    [RSCACHE_DAT2_TABLE_FONTS] = RSCACHE_DAT2_OSRS_TABLE_FONTS,
    [RSCACHE_DAT2_TABLE_MUSIC_SAMPLES] = RSCACHE_DAT2_OSRS_TABLE_MUSIC_SAMPLES,
    [RSCACHE_DAT2_TABLE_MUSIC_PATCHES] = RSCACHE_DAT2_OSRS_TABLE_MUSIC_PATCHES,
    [RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY] = RSCACHE_DAT2_OSRS_TABLE_WORLDMAP_GEOGRAPHY,
    [RSCACHE_DAT2_TABLE_WORLDMAP] = RSCACHE_DAT2_OSRS_TABLE_WORLDMAP,
    [RSCACHE_DAT2_TABLE_WORLDMAP_GROUND] = RSCACHE_DAT2_OSRS_TABLE_WORLDMAP_GROUND,
    [RSCACHE_DAT2_TABLE_DBTABLE_INDEX] = RSCACHE_DAT2_OSRS_TABLE_DBTABLE_INDEX,
    [RSCACHE_DAT2_TABLE_ANIMAYAS] = RSCACHE_DAT2_OSRS_TABLE_ANIMAYAS,
    [RSCACHE_DAT2_TABLE_GAMEVALS] = RSCACHE_DAT2_OSRS_TABLE_GAMEVALS,
    /* The RS2-only types live in the config table here, addressed by config kind —
     * RSCache_RecordAddressFor is what resolves those, not this. */
    [RSCACHE_DAT2_TABLE_LOC] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_ENUM] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_NPC] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_OBJ] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_SEQ] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_SPOTANIM] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_VARBIT] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_MATERIALS] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_PARTICLES] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_DEFAULTS] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
};

static const int DAT2_TABLE_IDS_RS2[RSCACHE_DAT2_TABLE_COUNT] = {
    [RSCACHE_DAT2_TABLE_ANIMATIONS] = RSCACHE_DAT2_RS2_TABLE_ANIMATIONS,
    [RSCACHE_DAT2_TABLE_SKELETONS] = RSCACHE_DAT2_RS2_TABLE_SKELETONS,
    [RSCACHE_DAT2_TABLE_CONFIGS] = RSCACHE_DAT2_RS2_TABLE_CONFIGS,
    [RSCACHE_DAT2_TABLE_INTERFACES] = RSCACHE_DAT2_RS2_TABLE_INTERFACES,
    [RSCACHE_DAT2_TABLE_SOUND_EFFECTS] = RSCACHE_DAT2_RS2_TABLE_SOUND_EFFECTS,
    [RSCACHE_DAT2_TABLE_MAPS] = RSCACHE_DAT2_RS2_TABLE_MAPS,
    [RSCACHE_DAT2_TABLE_MUSIC_TRACKS] = RSCACHE_DAT2_RS2_TABLE_MUSIC_TRACKS,
    [RSCACHE_DAT2_TABLE_MODELS] = RSCACHE_DAT2_RS2_TABLE_MODELS,
    [RSCACHE_DAT2_TABLE_SPRITES] = RSCACHE_DAT2_RS2_TABLE_SPRITES,
    [RSCACHE_DAT2_TABLE_TEXTURES] = RSCACHE_DAT2_RS2_TABLE_TEXTURES,
    [RSCACHE_DAT2_TABLE_BINARY] = RSCACHE_DAT2_RS2_TABLE_BINARY,
    [RSCACHE_DAT2_TABLE_MUSIC_JINGLES] = RSCACHE_DAT2_RS2_TABLE_MUSIC_JINGLES,
    [RSCACHE_DAT2_TABLE_CLIENTSCRIPT] = RSCACHE_DAT2_RS2_TABLE_CLIENTSCRIPT,
    [RSCACHE_DAT2_TABLE_FONTS] = RSCACHE_DAT2_RS2_TABLE_FONTS,
    [RSCACHE_DAT2_TABLE_MUSIC_SAMPLES] = RSCACHE_DAT2_RS2_TABLE_MUSIC_SAMPLES,
    [RSCACHE_DAT2_TABLE_MUSIC_PATCHES] = RSCACHE_DAT2_RS2_TABLE_MUSIC_PATCHES,
    /* 18..22 are npc/obj/seq/spotanim/varbit in this branch, so none of the OldSchool
     * tables that share those ids exist here. Reading them anyway is what this table
     * prevents: it decodes real archives as the wrong type rather than failing. */
    [RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_WORLDMAP] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_WORLDMAP_GROUND] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_DBTABLE_INDEX] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_ANIMAYAS] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_GAMEVALS] = RSCACHE_DAT2_DISK_TABLE_ABSENT,
    [RSCACHE_DAT2_TABLE_LOC] = RSCACHE_DAT2_RS2_TABLE_LOC,
    [RSCACHE_DAT2_TABLE_ENUM] = RSCACHE_DAT2_RS2_TABLE_ENUM,
    [RSCACHE_DAT2_TABLE_NPC] = RSCACHE_DAT2_RS2_TABLE_NPC,
    [RSCACHE_DAT2_TABLE_OBJ] = RSCACHE_DAT2_RS2_TABLE_OBJ,
    [RSCACHE_DAT2_TABLE_SEQ] = RSCACHE_DAT2_RS2_TABLE_SEQ,
    [RSCACHE_DAT2_TABLE_SPOTANIM] = RSCACHE_DAT2_RS2_TABLE_SPOTANIM,
    [RSCACHE_DAT2_TABLE_VARBIT] = RSCACHE_DAT2_RS2_TABLE_VARBIT,
    [RSCACHE_DAT2_TABLE_MATERIALS] = RSCACHE_DAT2_RS2_TABLE_MATERIALS,
    [RSCACHE_DAT2_TABLE_PARTICLES] = RSCACHE_DAT2_RS2_TABLE_PARTICLES,
    [RSCACHE_DAT2_TABLE_DEFAULTS] = RSCACHE_DAT2_RS2_TABLE_DEFAULTS,
};

int
RSCache_Dat2DiskTableForGame(
    int game,
    enum RSCache_Dat2Table table)
{
    if( table < 0 || table >= RSCACHE_DAT2_TABLE_COUNT )
        return RSCACHE_DAT2_DISK_TABLE_ABSENT;

    switch( game )
    {
    case RSCACHE_GAME_RS2:
        return DAT2_TABLE_IDS_RS2[table];
    case RSCACHE_GAME_OLDSCHOOL:
        return DAT2_TABLE_IDS_OSRS[table];
    default:
        /* Unset / dat1 has no dat2 tables. */
        return RSCACHE_DAT2_DISK_TABLE_ABSENT;
    }
}

void
RSCache_Dat2DiskSetProfile(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile)
{
    assert(disk);
    assert(profile);
    assert(RSCache_ProfileIsIdentified(profile));
    disk->profile = *profile;
    disk->profile_set = 1;
}

const struct RSCache*
RSCache_Dat2DiskProfile(const struct RSCache_Dat2Disk* disk)
{
    if( !disk || !disk->profile_set )
        return NULL;
    return &disk->profile;
}

int
RSCache_Dat2DiskTableId(
    const struct RSCache_Dat2Disk* disk,
    enum RSCache_Dat2Table table)
{
    int game = disk && disk->profile_set ? disk->profile.game : RSCACHE_GAME_UNSET;
    return RSCache_Dat2DiskTableForGame(game, table);
}

/*
 * Which tables exist is a property of the CACHE, not of the library's table list.
 *
 * The id range is era-dependent and overlapping — 19 is OldSchool's worldmap and RS2's objs, 26
 * is RS2's materials and nothing at all in OldSchool — so neither a name allow-list (which
 * hides tables we have not named: D18/D21) nor a bare range walk (which probes tables the cache
 * never had, and reports each as a failure) is right.
 *
 * The cache answers it directly: a table is present iff its `.idxN` file is. Absence is then
 * silent, because a cache not shipping a table is ordinary — an OldSchool dump has no reason to
 * carry RS2's 23..34. A table whose index *does* exist but whose reference table will not load
 * is still reported, because that one is a real problem.
 */
static bool
dat2disk_table_present(
    struct RSCache_Dat2Disk* disk,
    int table_id)
{
    if( !disk || !disk->store.has_table )
        return false;
    return disk->store.has_table(disk->store.user, table_id) != 0;
}

static void
init_reference_tables(struct RSCache_Dat2Disk* disk)
{
    for( int i = 0; i < RSCACHE_DAT2_DISK_TABLE_CAPACITY; ++i )
    {
        if( !RSCache_Dat2DiskIsValidTableId(i) )
            continue;
        if( !dat2disk_table_present(disk, i) )
            continue;

        /* Asked once here whether the open is eager or lazy: the store answers
         * it by looking for an idxN file, and a lazy disk that re-asked per
         * lookup would stat its way through every miss. */
        disk->tables_present |= (uint64_t)1 << i;
        if( disk->lazy_tables )
            continue;

        struct RSCache_Dat2DiskArchive* table_archive =
            RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, i);
        if( !table_archive )
        {
            printf("Failed to load referencetable %d\n", i);
            continue;
        }

        disk->tables[i] =
            RSCache_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
        RSCache_Dat2DiskArchiveFree(table_archive);
    }
}

static struct RSCache_ReferenceTable*
dat2disk_ensure_reference_table_loaded(
    struct RSCache_Dat2Disk* disk,
    int table_id)
{
    if( disk->tables[table_id] )
        return disk->tables[table_id];

    /*
     * A lazy disk recorded at open which tables the cache ships. Without that
     * test every lookup of a table this branch does not carry — and an
     * OldSchool dump does not carry RS2's 23..34 — would reach the loader,
     * fail, and say so again on the next lookup. An eager disk has a zero
     * bitmap and skips the test, since a NULL entry there already means the
     * open tried and could not.
     */
    if( disk->lazy_tables &&
        (disk->tables_present & ((uint64_t)1 << table_id)) == 0 )
        return NULL;

    struct RSCache_Dat2DiskArchive* table_archive =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !table_archive )
    {
        printf("Failed to load reference table %d\n", table_id);
        return NULL;
    }

    disk->tables[table_id] =
        RSCache_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCache_Dat2DiskArchiveFree(table_archive);
    return disk->tables[table_id];
}

struct RSCache_ReferenceTable*
RSCache_Dat2DiskReferenceTable(
    struct RSCache_Dat2Disk* disk,
    int table_id)
{
    assert(disk);

    /* Callers hold this straight out of RSCache_Dat2DiskTableId, where it
     * means the branch has no such table. That is an answer, not a bad
     * argument. */
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return NULL;

    assert(table_id >= 0);
    assert(table_id < RSCACHE_DAT2_DISK_TABLE_CAPACITY);
    return dat2disk_ensure_reference_table_loaded(disk, table_id);
}

static struct RSCache_Dat2Disk*
dat2disk_new_from_directory(
    const char* directory,
    const char* dat2_mode,
    int read_only,
    int lazy_tables)
{
    if( !directory || !dat2_mode )
        return NULL;

    struct RSCache_Dat2Disk* disk =
        (struct RSCache_Dat2Disk*)malloc(sizeof(struct RSCache_Dat2Disk));
    if( !disk )
        return NULL;

    memset(disk, 0, sizeof(struct RSCache_Dat2Disk));
    disk->profile = RSCache_ProfileZero();
    disk->profile_set = 0;
    disk->directory = strdup(directory);
    if( !disk->directory )
    {
        free(disk);
        return NULL;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    disk->dat2_file = fopen(path, dat2_mode);
    if( !disk->dat2_file )
    {
        free(disk->directory);
        free(disk);
        return NULL;
    }

    disk->read_only = read_only;
    disk->lazy_tables = lazy_tables;
    /* After read_only, which decides whether the vtable offers a put at all. */
    disk->store = RSCache_Dat2DiskFileStore(disk);
    init_reference_tables(disk);
    return disk;
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromDirectory(char const* directory)
{
    return dat2disk_new_from_directory(directory, "rb+", 0, 0);
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromDirectoryLazyTables(char const* directory)
{
    return dat2disk_new_from_directory(directory, "rb+", 0, 1);
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewReadOnlyFromDirectory(const char* directory)
{
    return dat2disk_new_from_directory(directory, "rb", 1, 0);
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewSparseFromDirectory(const char* directory)
{
    if( !directory )
        return NULL;

    char path[1024];
    int path_size = snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    if( path_size < 0 || (size_t)path_size >= sizeof(path) )
        return NULL;

    /* Append mode has the one property this constructor promises: it creates a
     * missing file and can never truncate an existing one. NewFromDirectory
     * reopens it in update mode before any sector reads occur. */
    FILE* file = fopen(path, "ab");
    if( !file )
        return NULL;
    if( fclose(file) != 0 )
        return NULL;

    return RSCache_Dat2DiskNewFromDirectory(directory);
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromStore(
    const char* label,
    const struct RSCache_Dat2Store* store)
{
    if( !store || !store->get )
        return NULL;

    struct RSCache_Dat2Disk* disk =
        (struct RSCache_Dat2Disk*)malloc(sizeof(struct RSCache_Dat2Disk));
    if( !disk )
        return NULL;

    memset(disk, 0, sizeof(struct RSCache_Dat2Disk));
    disk->profile = RSCache_ProfileZero();
    disk->profile_set = 0;
    disk->store = *store;
    disk->directory = strdup(label ? label : "store");
    if( !disk->directory )
    {
        free(disk);
        return NULL;
    }

    /* dat2_file stays NULL. It belongs to the file store, and a disk holding
     * both would have two sources of truth for the same archive. */
    init_reference_tables(disk);
    return disk;
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewUninitialized(void)
{
    struct RSCache_Dat2Disk* disk =
        (struct RSCache_Dat2Disk*)malloc(sizeof(struct RSCache_Dat2Disk));
    if( !disk )
        return NULL;
    memset(disk, 0, sizeof(struct RSCache_Dat2Disk));
    disk->profile = RSCache_ProfileZero();
    disk->profile_set = 0;
    return disk;
}

void
RSCache_Dat2DiskFree(struct RSCache_Dat2Disk* disk)
{
    if( !disk )
        return;

    if( disk->store.destroy )
        disk->store.destroy(disk->store.user);
    /* The file store's handle. Closed after destroy so a store that wanted a
     * final flush through it still could. */
    if( disk->dat2_file )
        fclose(disk->dat2_file);
    dat2disk_disk_index_forget(disk, -1);

    for( int i = 0; i < RSCACHE_DAT2_DISK_TABLE_CAPACITY; ++i )
    {
        if( disk->tables[i] )
            RSCache_ReferenceTableFree(disk->tables[i]);
    }

    free(disk->directory);
    free(disk);
}

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoadRaw(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id)
{
    uint8_t* data = NULL;
    int size = 0;

    if( !disk || !disk->store.get || table_id < 0 || table_id > 255 || archive_id < 0 )
        return NULL;
    if( disk->store.get(disk->store.user, table_id, archive_id, &data, &size) != 1 )
        return NULL;
    if( !data || size <= 0 )
    {
        free(data);
        return NULL;
    }

    struct RSCache_Dat2DiskArchive* archive =
        (struct RSCache_Dat2DiskArchive*)malloc(sizeof(struct RSCache_Dat2DiskArchive));
    if( !archive )
    {
        free(data);
        return NULL;
    }
    memset(archive, 0, sizeof(struct RSCache_Dat2DiskArchive));
    archive->data = (char*)data;
    archive->data_size = size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->file_count = -1;
    return archive;
}

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewReferenceTableLoad(
    struct RSCache_Dat2Disk* disk,
    int table_id)
{
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoadRaw(
        disk, RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID, table_id);
    if( !archive )
        return NULL;

    if( !RSCache_ArchiveDecryptDecompress(archive, NULL) )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    /* Preserve the historical meaning of this helper: this is the reference
     * metadata *for* table N, rather than an ordinary archive owned by 255. */
    archive->table_id = table_id;
    return archive;
}

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoad(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id)
{
    return RSCache_Dat2DiskArchiveNewLoadDecrypted(disk, table_id, archive_id, NULL);
}

bool
RSCache_Dat2DiskArchiveInitMetadataFromTable(
    struct RSCache_ReferenceTable* table,
    struct RSCache_Dat2DiskArchive* archive)
{
    assert(table != NULL);
    assert(archive != NULL);

    /* Hand-patched caches (e.g. cache.kronos) carry idx records for archives
     * the reference table doesn't list — past its max id or in an id gap
     * (index == -1). Report failure instead of indexing out of bounds. */
    if( archive->archive_id < 0 || archive->archive_id >= table->archive_count ||
        table->archives[archive->archive_id].index < 0 )
        return false;
    struct RSCache_ReferenceTableArchive* archive_reference = &table->archives[archive->archive_id];
    archive->revision = archive_reference->version;
    archive->file_count = archive_reference->children.count;

    free(archive->file_ids);
    archive->file_ids = NULL;
    if( archive->file_count > 0 )
    {
        archive->file_ids = malloc((size_t)archive->file_count * sizeof(int));
        assert(archive->file_ids);
        for( int i = 0; i < archive->file_count; i++ )
            archive->file_ids[i] = archive_reference->children.files[i].id;
    }
    return true;
}

bool
RSCache_Dat2DiskArchiveInitMetadata(
    struct RSCache_Dat2Disk* disk,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct RSCache_ReferenceTable* table =
        dat2disk_ensure_reference_table_loaded(disk, archive->table_id);
    if( !table )
    {
        printf("Failed to load reference table for table %d\n", archive->table_id);
        return false;
    }

    return RSCache_Dat2DiskArchiveInitMetadataFromTable(table, archive);
}

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoadDecrypted(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id,
    uint32_t* xtea_key_nullable)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoadRaw(disk, table_id, archive_id);
    if( !archive )
        return NULL;

    if( !RSCache_ArchiveDecryptDecompress(archive, xtea_key_nullable) )
    {
        printf("Failed to decompress dat2 archive for table %d\n", table_id);
        goto error;
    }

    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->file_count = 0;
    return archive;

error:
    RSCache_Dat2DiskArchiveFree(archive);
    return NULL;
}

bool
RSCache_Dat2DiskInstallReferenceTableRaw(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    const uint8_t* data,
    int data_size)
{
    if( !disk || disk->read_only || !disk->store.put ||
        !RSCache_Dat2DiskIsValidTableId(table_id) || !data || data_size <= 0 )
        return false;

    size_t container_size;
    if( !RSCache_ArchiveRawContainerLength(data, (size_t)data_size, &container_size) )
        return false;
    size_t trailer_size = (size_t)data_size - container_size;
    if( trailer_size != 0u && trailer_size != 2u && trailer_size != 4u )
        return false;

    struct RSCache_Dat2DiskArchive* archive =
        (struct RSCache_Dat2DiskArchive*)calloc(1, sizeof(*archive));
    if( !archive )
        return false;
    archive->data = malloc((size_t)data_size);
    if( !archive->data )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return false;
    }
    memcpy(archive->data, data, (size_t)data_size);
    archive->data_size = data_size;

    if( !RSCache_ArchiveDecryptDecompress(archive, NULL) )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return false;
    }

    struct RSCache_ReferenceTable* replacement =
        RSCache_ReferenceTableNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !replacement )
        return false;

    if( RSCache_Dat2DiskWriteArchiveTo(
            disk, RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID, table_id, data, data_size) != 0 )
    {
        RSCache_ReferenceTableFree(replacement);
        return false;
    }

    /* Whatever side structures this backing keeps beside the record — for the
     * dat2 files, the idxN presence sentinel and a reader whose buffered state
     * predates the append. A keyed store has none and does not implement it. */
    if( disk->store.commit_table &&
        disk->store.commit_table(disk->store.user, table_id) != 0 )
    {
        RSCache_ReferenceTableFree(replacement);
        return false;
    }

    /* Published last, so a disk whose table pointer says "installed" is one
     * where the bytes really are in the store. */
    struct RSCache_ReferenceTable* previous_table = disk->tables[table_id];
    disk->tables[table_id] = replacement;
    RSCache_ReferenceTableFree(previous_table);
    return true;
}

uint32_t*
RSCache_Dat2DiskArchiveXteaKey(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id)
{
    (void)disk;
    return (uint32_t*)RSCache_XteaConfigFindKey(table_id, archive_id);
}

void
RSCache_Dat2DiskArchiveFree(struct RSCache_Dat2DiskArchive* archive)
{
    if( !archive )
        return;

    if( archive->data )
        free(archive->data);

    free(archive->file_ids);
    free(archive);
}
