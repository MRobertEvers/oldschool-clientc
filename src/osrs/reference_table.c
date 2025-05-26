#include "reference_table.h"

#include "buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// public static int getSmartInt(ByteBuffer buffer) {
// 	if (buffer.get(buffer.position()) < 0)
// 		return buffer.getInt() & 0x7fffffff;
// 	return buffer.getShort() & 0xFFFF;
// }
static int
get_smart_int(struct Buffer* buffer)
{
    assert(buffer->position < buffer->data_size);
    if( (buffer->data[buffer->position]) < 0 )
        return read_32(buffer) & 0x7fffffff;
    return read_16(buffer) & 0xFFFF;
}

#define FLAG_IDENTIFIERS 0x1
#define FLAG_WHIRLPOOL 0x2
#define FLAG_SIZES 0x4
#define FLAG_HASH 0x8

/**
 * @brief OpenRS calls this ReferenceTable. Runelite calls this "IndexData". See those classes
 * RuneLite/IndexData.java
 * OpenRS/ReferenceTable.java
 * @param buffer
 * @return struct ReferenceTable*
 */
struct ReferenceTable*
reference_table_new_decode(char* data, int data_size)
{
    struct ReferenceTable* table = malloc(sizeof(struct ReferenceTable));
    if( !table )
    {
        return NULL;
    }
    memset(table, 0, sizeof(struct ReferenceTable));

    struct Buffer buffer = { .data = data, .position = 0, .data_size = data_size };

    // Read header
    table->format = read_8(&buffer) & 0xFF;
    if( table->format < 5 || table->format > 7 )
    {
        free(table);
        return NULL;
    }

    if( table->format >= 6 )
    {
        table->version = read_32(&buffer);
    }

    table->flags = read_8(&buffer) & 0xFF;

    // Read the IDs
    int id_count;
    if( table->format >= 7 )
        id_count = get_smart_int(&buffer);
    else
        id_count = read_16(&buffer) & 0xFFFF;
    int* ids = malloc(id_count * sizeof(int));
    if( !ids )
    {
        free(table);
        return NULL;
    }

    table->ids = ids;
    table->id_count = id_count;

    int accumulator = 0;
    int max_id = -1;
    for( int i = 0; i < id_count; i++ )
    {
        int delta = table->format >= 7 ? get_smart_int(&buffer) : read_16(&buffer) & 0xFFFF;
        ids[i] = accumulator += delta;
        if( ids[i] > max_id )
        {
            max_id = ids[i];
        }
    }
    max_id++;

    // Allocate entries array
    table->archives = malloc(max_id * sizeof(struct ArchiveReference));
    if( !table->archives )
    {
        free(ids);
        free(table);
        return NULL;
    }
    memset(table->archives, 0, max_id * sizeof(struct ArchiveReference));
    // Set index to -1 to indicate it is a sparse array
    for( int i = 0; i < max_id; i++ )
        table->archives[i].index = -1;
    table->archive_count = max_id;

    // Initialize entries
    for( int i = 0; i < id_count; i++ )
    {
        table->archives[ids[i]].index = ids[i];
    }

    // Read identifiers if present
    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            table->archives[id].identifier = read_32(&buffer);
        }
    }

    // Read CRC32 checksums
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        table->archives[id].crc = read_32(&buffer);
    }

    // Read sizes if present
    if( (table->flags & FLAG_SIZES) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            table->archives[id].compressed = read_32(&buffer);
            table->archives[id].uncompressed = read_32(&buffer);
        }
    }

    // Read version numbers
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        table->archives[id].version = read_32(&buffer);
    }

    // Read child sizes
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        int child_count;
        if( table->format >= 7 )
            child_count = get_smart_int(&buffer);
        else
            child_count = read_16(&buffer) & 0xFFFF;
        table->archives[id].children.count = child_count;
        table->archives[id].children.files =
            malloc(child_count * sizeof(struct ArchiveFileReference));
        if( !table->archives[id].children.files )
        {
            // Cleanup on error
            for( int j = 0; j < i; j++ )
            {
                free(table->archives[ids[j]].children.files);
            }
            free(ids);
            free(table->archives);
            free(table);
            return NULL;
        }
    }

    // Read child IDs
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        accumulator = 0;
        for( int j = 0; j < table->archives[id].children.count; j++ )
        {
            int delta = table->format >= 7 ? get_smart_int(&buffer) : read_16(&buffer) & 0xFFFF;
            table->archives[id].children.files[j].id = accumulator += delta;
        }
    }

    // Read identifiers if present
    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        // for (int index = 0; index < validArchivesCount; ++index)
        // 	{
        // 		ArchiveData ad = archives[index];
        // 		int num = numberOfFiles[index];

        // 		for (int i = 0; i < num; ++i)
        // 		{
        // 			FileData fd = ad.files[i];
        // 			int name = stream.readInt();
        // 			fd.nameHash = name;
        // 		}
        // 	}
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            for( int j = 0; j < table->archives[id].children.count; j++ )
            {
                table->archives[id].children.files[j].name_hash = read_32(&buffer);
            }
        }
    }

    return table;
}

void
reference_table_free(struct ReferenceTable* table)
{
    if( table->ids )
        free(table->ids);
    if( table->archives )
        free(table->archives);
    free(table);
}