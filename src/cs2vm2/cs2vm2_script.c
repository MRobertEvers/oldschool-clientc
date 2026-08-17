#include "cs2vm2_script.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void
CS2VM2_ScriptInit(struct CS2VM2_Script* script)
{
    assert(script);
    memset(script, 0, sizeof(*script));
}

bool
CS2VM2_ScriptAllocSwitches(
    struct CS2VM2_Script* script,
    int count)
{
    assert(script);
    assert(!script->switch_tables);

    script->switch_table_count = 0;
    if( count <= 0 )
        return true;

    script->switch_tables = calloc((size_t)count, sizeof(*script->switch_tables));
    assert(script->switch_tables);
    script->switch_table_count = count;
    return true;
}

bool
CS2VM2_ScriptAllocSwitchCases(
    struct CS2VM2_Script* script,
    int table_index,
    int case_count)
{
    struct CS2VM2_ScriptSwitch* table;

    assert(script);
    assert(table_index >= 0 && table_index < script->switch_table_count);

    table = &script->switch_tables[table_index];
    table->case_count = 0;
    if( case_count <= 0 )
        return true;

    table->cases = calloc((size_t)case_count, sizeof(*table->cases));
    assert(table->cases);
    table->case_count = case_count;
    return true;
}

static int
cs2vm2_switch_case_cmp(
    void const* a,
    void const* b)
{
    struct CS2VM2_ScriptSwitchCase const* ca = (struct CS2VM2_ScriptSwitchCase const*)a;
    struct CS2VM2_ScriptSwitchCase const* cb = (struct CS2VM2_ScriptSwitchCase const*)b;
    /* Not (ca->key - cb->key): keys span the whole int range and the subtraction
     * overflows, which sorts a table with both large-positive and
     * large-negative keys into the wrong order. */
    if( ca->key < cb->key )
        return -1;
    if( ca->key > cb->key )
        return 1;
    return 0;
}

void
CS2VM2_ScriptSortSwitches(struct CS2VM2_Script* script)
{
    assert(script);

    for( int i = 0; i < script->switch_table_count; i++ )
    {
        struct CS2VM2_ScriptSwitch* table = &script->switch_tables[i];
        if( table->case_count > 1 )
        {
            assert(table->cases);
            qsort(
                table->cases, (size_t)table->case_count, sizeof(*table->cases),
                cs2vm2_switch_case_cmp);
        }
        table->sorted = 1;
    }
}

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script)
{
    if( !script )
        return;

    if( script->switch_tables )
    {
        for( int i = 0; i < script->switch_table_count; i++ )
            free(script->switch_tables[i].cases);
        free(script->switch_tables);
    }
    free(script->callee_cache);
    free(script->signature);
    free(script->opcodes);
    free(script->int_operands);
    if( script->string_operands )
    {
        for( int i = 0; i < script->op_count; i++ )
            free(script->string_operands[i]);
        free(script->string_operands);
    }
    memset(script, 0, sizeof(*script));
}

bool
CS2VM2_ScriptCopy(
    const struct CS2VM2_Script* src,
    struct CS2VM2_Script* dst)
{
    int i;

    assert(src);
    assert(dst);

    if( src->op_count <= 0 )
        return false;

    CS2VM2_ScriptInit(dst);
    dst->script_id = src->script_id;
    dst->rs2_dialect = src->rs2_dialect;
    dst->local_int_count = src->local_int_count;
    dst->local_string_count = src->local_string_count;
    dst->local_long_count = src->local_long_count;
    dst->int_argument_count = src->int_argument_count;
    dst->string_argument_count = src->string_argument_count;
    dst->long_argument_count = src->long_argument_count;
    dst->int_stack_depth = src->int_stack_depth;
    dst->str_stack_depth = src->str_stack_depth;
    dst->op_count = src->op_count;
    if( !CS2VM2_ScriptAllocSwitches(dst, src->switch_table_count) )
        return false;
    for( i = 0; i < src->switch_table_count; i++ )
    {
        if( !CS2VM2_ScriptAllocSwitchCases(dst, i, src->switch_tables[i].case_count) )
            return false;
        memcpy(
            dst->switch_tables[i].cases,
            src->switch_tables[i].cases,
            (size_t)src->switch_tables[i].case_count * sizeof(*dst->switch_tables[i].cases));
        /* The cases came over in whatever order the source had them, so the
         * source's answer to "is this searchable" is the copy's answer too. */
        dst->switch_tables[i].sorted = src->switch_tables[i].sorted;
    }

    if( src->signature )
    {
        dst->signature = strdup(src->signature);
        assert(dst->signature);
    }

    /* The copy starts with no resolved callees: the cache is keyed by this
     * script's own pcs and is rebuilt on demand, so sharing or copying it would
     * only duplicate state that costs one host call to recover. */
    dst->callee_cache = NULL;

    dst->opcodes = malloc((size_t)src->op_count * sizeof(*dst->opcodes));
    assert(dst->opcodes);
    memcpy(dst->opcodes, src->opcodes, (size_t)src->op_count * sizeof(*dst->opcodes));

    dst->int_operands = malloc((size_t)src->op_count * sizeof(*dst->int_operands));
    assert(dst->int_operands);
    memcpy(dst->int_operands, src->int_operands, (size_t)src->op_count * sizeof(*dst->int_operands));

    dst->string_operands = calloc((size_t)src->op_count, sizeof(*dst->string_operands));
    assert(dst->string_operands);
    for( i = 0; i < src->op_count; i++ )
    {
        if( !src->string_operands[i] )
            continue;
        dst->string_operands[i] = strdup(src->string_operands[i]);
        assert(dst->string_operands[i]);
    }

    return true;
}
