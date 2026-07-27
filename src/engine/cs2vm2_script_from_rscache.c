#include "cs2vm2_script_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
cs2vm2_script_translate_opcodes(
    struct CS2VM2_Script* dst,
    enum CS2_OpcodeDialect dialect)
{
    assert(dst);
    assert(dst->opcodes || dst->op_count == 0);

    for( int i = 0; i < dst->op_count; i++ )
        dst->opcodes[i] = CS2_OpcodeTranslate(dialect, dst->opcodes[i]);
}

static bool
cs2vm2_script_copy_heap_fields(
    const struct RSCache_CS2_Script* src,
    struct CS2VM2_Script* dst)
{
    int i;

    assert(src);
    assert(dst);

    if( src->signature )
    {
        dst->signature = strdup(src->signature);
        assert(dst->signature);
    }

    if( src->op_count > 0 )
    {
        dst->opcodes = malloc((size_t)src->op_count * sizeof(*dst->opcodes));
        assert(dst->opcodes);
        memcpy(dst->opcodes, src->opcodes, (size_t)src->op_count * sizeof(*dst->opcodes));

        dst->int_operands = malloc((size_t)src->op_count * sizeof(*dst->int_operands));
        assert(dst->int_operands);
        memcpy(
            dst->int_operands,
            src->int_operands,
            (size_t)src->op_count * sizeof(*dst->int_operands));

        dst->string_operands = calloc((size_t)src->op_count, sizeof(*dst->string_operands));
        assert(dst->string_operands);
        for( i = 0; i < src->op_count; i++ )
        {
            if( !src->string_operands[i] )
                continue;
            dst->string_operands[i] = strdup(src->string_operands[i]);
            assert(dst->string_operands[i]);
        }
    }

    return true;
}

bool
CS2VM2_ScriptFromRSCache(
    struct RSCache_CS2_Script* src,
    struct CS2VM2_Script* dst,
    enum CS2_OpcodeDialect dialect)
{
    assert(src);
    assert(dst);

    if( src->op_count <= 0 )
        return false;

    CS2VM2_ScriptInit(dst);
    dst->script_id = src->script_id;
    dst->signature = src->signature;
    dst->local_int_count = src->local_int_count;
    dst->local_string_count = src->local_string_count;
    dst->local_long_count = src->local_long_count;
    dst->int_argument_count = src->int_argument_count;
    dst->string_argument_count = src->string_argument_count;
    dst->long_argument_count = src->long_argument_count;
    dst->int_stack_depth = 0;
    dst->str_stack_depth = 0;
    dst->op_count = src->op_count;
    dst->opcodes = src->opcodes;
    dst->int_operands = src->int_operands;
    dst->string_operands = src->string_operands;
    dst->switch_table_count = src->switch_table_count;
    memcpy(dst->switch_tables, src->switch_tables, sizeof(dst->switch_tables));

    /* Ownership transferred; rewrite wire opcodes in place under the chosen dialect. */
    cs2vm2_script_translate_opcodes(dst, dialect);

    RSCache_CS2_ScriptInit(src);
    return true;
}

bool
CS2VM2_ScriptCopyFromRSCache(
    const struct RSCache_CS2_Script* src,
    struct CS2VM2_Script* dst,
    enum CS2_OpcodeDialect dialect)
{
    assert(src);
    assert(dst);

    if( src->op_count <= 0 )
        return false;

    CS2VM2_ScriptInit(dst);
    dst->script_id = src->script_id;
    dst->rs2_dialect = dialect == CS2_OPCODE_DIALECT_RS2_DAT2;
    dst->signature = NULL;
    dst->local_int_count = src->local_int_count;
    dst->local_string_count = src->local_string_count;
    dst->local_long_count = src->local_long_count;
    dst->int_argument_count = src->int_argument_count;
    dst->string_argument_count = src->string_argument_count;
    dst->long_argument_count = src->long_argument_count;
    dst->int_stack_depth = 0;
    dst->str_stack_depth = 0;
    dst->op_count = src->op_count;
    dst->opcodes = NULL;
    dst->int_operands = NULL;
    dst->string_operands = NULL;
    dst->switch_table_count = src->switch_table_count;
    memcpy(dst->switch_tables, src->switch_tables, sizeof(dst->switch_tables));

    if( !cs2vm2_script_copy_heap_fields(src, dst) )
        return false;

    cs2vm2_script_translate_opcodes(dst, dialect);
    return true;
}
