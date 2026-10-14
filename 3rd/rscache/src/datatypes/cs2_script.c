#include "cs2_script.h"
#include "../rscache_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
RSCache_CS2_ScriptInit(struct RSCache_CS2_Script* script)
{
    assert(script);
    memset(script, 0, sizeof(*script));
}

int
RSCache_CS2_ScriptAllocSwitches(struct RSCache_CS2_Script* script, int count)
{
    assert(script);
    if( count <= 0 )
    {
        script->switch_table_count = 0;
        return 1;
    }
    script->switch_tables =
        (struct RSCache_CS2_ScriptSwitch*)RSCACHE_CALLOC((size_t)count, sizeof(*script->switch_tables));
    if( !script->switch_tables )
        return 0;
    script->switch_table_count = count;
    return 1;
}

int
RSCache_CS2_ScriptAllocSwitchCases(
    struct RSCache_CS2_Script* script,
    int table_index,
    int case_count)
{
    assert(script);
    if( table_index < 0 || table_index >= script->switch_table_count )
        return 0;
    struct RSCache_CS2_ScriptSwitch* table = &script->switch_tables[table_index];
    if( case_count <= 0 )
    {
        table->case_count = 0;
        return 1;
    }
    table->cases =
        (struct RSCache_CS2_ScriptSwitchCase*)RSCACHE_CALLOC((size_t)case_count, sizeof(*table->cases));
    if( !table->cases )
        return 0;
    table->case_count = case_count;
    return 1;
}

void
RSCache_CS2_ScriptFree(struct RSCache_CS2_Script* script)
{
    if( !script )
        return;
    RSCACHE_FREE(script->signature);
    RSCACHE_FREE(script->opcodes);
    RSCACHE_FREE(script->int_operands);
    RSCACHE_FREE(script->long_operands);
    if( script->string_operands )
    {
        for( int i = 0; i < script->op_count; i++ )
            RSCACHE_FREE(script->string_operands[i]);
        RSCACHE_FREE(script->string_operands);
    }
    if( script->switch_tables )
    {
        for( int i = 0; i < script->switch_table_count; i++ )
            RSCACHE_FREE(script->switch_tables[i].cases);
        RSCACHE_FREE(script->switch_tables);
    }
    memset(script, 0, sizeof(*script));
}
