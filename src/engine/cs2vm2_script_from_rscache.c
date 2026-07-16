#include "cs2vm2_script_from_rscache.h"

#include <assert.h>
#include <string.h>

bool
CS2VM2_ScriptFromRSCache(
    struct RSCache_CS2_Script* src,
    struct CS2VM2_Script* dst)
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

    RSCache_CS2_ScriptInit(src);
    return true;
}
