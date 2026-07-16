#include "reference_table.h"

#include "rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IDENTIFIERS 0x1
#define FLAG_WHIRLPOOL 0x2
#define FLAG_SIZES 0x4
#define FLAG_HASH 0x8

struct RSCache_ReferenceTable*
RSCache_ReferenceTableNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_ReferenceTable* table = malloc(sizeof(struct RSCache_ReferenceTable));
    if( !table )
        return NULL;
    memset(table, 0, sizeof(struct RSCache_ReferenceTable));

    struct RSCache_Buffer buffer = { .data = (uint8_t*)data, .position = 0, .size = (uint32_t)data_size };

    table->format = g1(&buffer);
    if( table->format < 5 || table->format > 7 )
    {
        free(table);
        return NULL;
    }

    if( table->format >= 6 )
        table->version = g4(&buffer);

    table->flags = g1(&buffer);

    int id_count;
    if( table->format >= 7 )
        id_count = gusmart(&buffer);
    else
        id_count = g2(&buffer);

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
        int delta = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
        ids[i] = accumulator += delta;
        if( ids[i] > max_id )
            max_id = ids[i];
    }
    max_id++;

    table->archives = malloc(max_id * sizeof(struct RSCache_ReferenceTableArchive));
    if( !table->archives )
    {
        free(ids);
        free(table);
        return NULL;
    }
    memset(table->archives, 0, max_id * sizeof(struct RSCache_ReferenceTableArchive));
    for( int i = 0; i < max_id; i++ )
        table->archives[i].index = -1;
    table->archive_count = max_id;

    for( int i = 0; i < id_count; i++ )
        table->archives[ids[i]].index = ids[i];

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
            table->archives[ids[i]].identifier = g4(&buffer);
    }

    for( int i = 0; i < id_count; i++ )
        table->archives[ids[i]].crc = g4(&buffer);

    if( (table->flags & FLAG_SIZES) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            table->archives[id].compressed = g4(&buffer);
            table->archives[id].uncompressed = g4(&buffer);
        }
    }

    for( int i = 0; i < id_count; i++ )
        table->archives[ids[i]].version = g4(&buffer);

    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        int child_count = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
        table->archives[id].children.count = child_count;
        table->archives[id].children.files =
            malloc(child_count * sizeof(struct RSCache_ReferenceTableArchiveFile));
        if( !table->archives[id].children.files )
        {
            for( int j = 0; j < i; j++ )
                free(table->archives[ids[j]].children.files);
            free(ids);
            free(table->archives);
            free(table);
            return NULL;
        }
    }

    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        accumulator = 0;
        for( int j = 0; j < table->archives[id].children.count; j++ )
        {
            int delta = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
            table->archives[id].children.files[j].id = accumulator += delta;
        }
    }

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            for( int j = 0; j < table->archives[id].children.count; j++ )
                table->archives[id].children.files[j].name_hash = g4(&buffer);
        }
    }

    return table;
}

void
RSCache_ReferenceTableFree(struct RSCache_ReferenceTable* table)
{
    if( !table )
        return;

    if( table->ids )
        free(table->ids);

    for( int i = 0; i < table->archive_count; i++ )
    {
        if( table->archives[i].children.files )
            free(table->archives[i].children.files);
    }

    if( table->archives )
        free(table->archives);

    free(table);
}
