#include "disk.h"

#include "rsbuf.h"

#include <assert.h>
#include <string.h>

#define SECTOR_SIZE 520

struct SectorHeader
{
    int part_no;
    int next_sector_no;
    int index_id;
    int archive_id;
};

static void
read_sector_header_small(struct SectorHeader* header, uint8_t* data)
{
    header->archive_id = ((data[0] & 0xFF) << 8) | (data[1] & 0xFF);
    header->part_no = ((data[2] & 0xFF) << 8) | (data[3] & 0xFF);
    header->next_sector_no = ((data[4] & 0xFF) << 16) | ((data[5] & 0xFF) << 8) | (data[6] & 0xFF);
    header->index_id = data[7] & 0xFF;
}

static void
write_sector_header_small(struct SectorHeader* header, uint8_t* data)
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
read_sector_header_large(struct SectorHeader* header, uint8_t* data)
{
    header->archive_id = ((data[0] & 0xFF) << 24) | ((data[1] & 0xFF) << 16) |
                         ((data[2] & 0xFF) << 8) | (data[3] & 0xFF);
    header->part_no = ((data[4] & 0xFF) << 8) + (data[5] & 0xFF);
    header->next_sector_no = ((data[6] & 0xFF) << 16) | ((data[7] & 0xFF) << 8) | (data[8] & 0xFF);
    header->index_id = data[9] & 0xFF;
}

static void
write_sector_header_large(struct SectorHeader* header, uint8_t* data)
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
read_sector_header(struct SectorHeader* header, int archive_id, uint8_t* data, int data_size)
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
write_sector_header(struct SectorHeader* header, uint8_t* data, int data_size)
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
disk_dat2file_read_archive(
    FILE* dat2_file,
    struct Dat2Archive* archive,
    // This is a sanity check. I.e. if you run off the end of the index's dat2 file,
    // this will catch an error.
    int idx_file_id,
    int archive_id,
    int sector,
    int length)
{
    char read_buffer[SECTOR_SIZE];
    int read_buffer_len = 0;
    int data_block_size;

    int header_size = header_size_for_archive(archive_id);
    struct SectorHeader header = { 0 };
    header.archive_id = archive_id;
    header.index_id = idx_file_id;
    header.part_no = 0;
    header.next_sector_no = sector;

    char* out = NULL;

    if( sector <= 0L )
    {
        printf("bad read, dat length %d, requested sector %d", length, sector);
        goto error;
    }

    int out_len = 0;
    out = malloc(length);
    memset(out, 0, length);

    for( int part = 0, read_bytes_count = 0; length > read_bytes_count;
         sector = header.next_sector_no )
    {
        if( sector == 0 )
        {
            printf("Unexpected end of file\n");
            goto error;
        }

        fseek(dat2_file, sector * SECTOR_SIZE, SEEK_SET);

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

        read_buffer_len = bytes_read;

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
disk_dat2file_append_archive(FILE* file, int index_id, int archive_id, uint8_t* data, int data_size)
{
    uint8_t sector_data[SECTOR_SIZE];

    // Fill file to next SECTOR_SIZE boundary
    long data_file_size = ftell(file);
    if( data_file_size < 0 )
    {
        printf("ftell failed\n");
        return -1;
    }

    int padding = 0;
    int sector_no = data_file_size / SECTOR_SIZE;

    if( data_file_size % SECTOR_SIZE != 0 )
    {
        sector_no++;
        padding = SECTOR_SIZE - (data_file_size % SECTOR_SIZE);
        uint8_t zeros[SECTOR_SIZE] = { 0 };
        if( fwrite(zeros, 1, padding, file) != padding )
        {
            printf("failed to write padding\n");
            return -1;
        }
    }

    // Determine header size and payload size based on archive_id
    int header_size = header_size_for_archive(archive_id);
    int payload_size = SECTOR_SIZE - header_size;
    int bytes_written = 0;

    struct SectorHeader header = { 0 };
    header.archive_id = archive_id;
    header.index_id = index_id;
    header.part_no = 0;
    header.next_sector_no = sector_no;

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
        if( fwrite(sector_data, 1, SECTOR_SIZE, file) != SECTOR_SIZE )
        {
            printf("failed to write sector\n");
            return -1;
        }

        bytes_written += bytes_to_write;
        header.part_no++;
        header.next_sector_no++;
    }

    return 0;
}