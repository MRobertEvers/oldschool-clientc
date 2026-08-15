#include "rs_soundscape.h"
#include <assert.h>

#include <stdlib.h>
#include <string.h>

void
RS_Soundscapes_Init(struct RS_Soundscapes* table)
{
    assert(table);
    memset(table, 0, sizeof(*table));
}

void
RS_Soundscapes_Free(struct RS_Soundscapes* table)
{
    if( !table )
        return;
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
}

bool
RS_Soundscapes_SetEntries(
    struct RS_Soundscapes* table,
    struct RS_Soundscape* entries,
    int count)
{
    if( count <= 0 )
        return false;
    assert(table);
    assert(entries);
    free(table->entries);
    table->entries = entries;
    table->count = count;
    return true;
}

const struct RS_Soundscape*
RS_Soundscapes_Get(
    const struct RS_Soundscapes* table,
    int id)
{
    assert(table);
    if( !table->entries || id < 0 || id >= table->count )
        return NULL;
    if( !table->entries[id].present )
        return NULL;
    return &table->entries[id];
}
