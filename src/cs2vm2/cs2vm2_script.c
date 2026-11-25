#include "mem/torirs_mem.h"
#include "cs2vm2_script.h"
#include "cs2_opcode.h"
#include "cs2vm2_jit.h"
#include "cs2vm2_callee.h"
#include "cs2vm2_str.h"

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

    script->switch_tables = ToriMem_Calloc(ToriMem_Script(), (size_t)count, sizeof(*script->switch_tables));
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

    table->cases = ToriMem_Calloc(ToriMem_Script(), (size_t)case_count, sizeof(*table->cases));
    assert(table->cases);
    table->case_count = case_count;
    return true;
}

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script)
{
    if( !script )
        return;

    /* Any caller that remembered this script as its callee must ask again. */
    CS2VM2_CalleeScriptRetired(script);
    /* First, while the constant strings its debug canary re-hashes exist. */
    CS2VM2_JitFree(script);
    if( script->switch_tables )
    {
        for( int i = 0; i < script->switch_table_count; i++ )
            ToriMem_Free(ToriMem_Script(), script->switch_tables[i].cases);
        ToriMem_Free(ToriMem_Script(), script->switch_tables);
    }
    ToriMem_Free(ToriMem_Script(), script->signature);
    ToriMem_Free(ToriMem_Script(), script->opcodes);
    ToriMem_Free(ToriMem_Script(), script->undefined_wires);
    ToriMem_Free(ToriMem_Script(), script->int_operands);
    ToriMem_Free(ToriMem_Script(), script->long_operands);
    if( script->string_operands )
    {
        for( int i = 0; i < script->op_count; i++ )
            ToriMem_Free(ToriMem_Script(), script->string_operands[i]);
        ToriMem_Free(ToriMem_Script(), script->string_operands);
    }
    if( script->string_constants )
    {
        for( int i = 0; i < script->op_count; i++ )
            ToriMem_Free(ToriMem_Script(), script->string_constants[i]);
        ToriMem_Free(ToriMem_Script(), script->string_constants);
    }
    ToriMem_Free(ToriMem_Script(), script->prepared_ops);
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
    dst->dialect = src->dialect;
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
    }

    if( src->signature )
    {
        dst->signature = ToriMem_Strdup(ToriMem_Script(), src->signature);
        assert(dst->signature);
    }

    dst->opcodes = ToriMem_Alloc(ToriMem_Script(), (size_t)src->op_count * sizeof(*dst->opcodes));
    assert(dst->opcodes);
    memcpy(dst->opcodes, src->opcodes, (size_t)src->op_count * sizeof(*dst->opcodes));

    if( src->undefined_wire_count > 0 )
    {
        size_t const bytes = (size_t)src->undefined_wire_count * sizeof(*dst->undefined_wires);
        dst->undefined_wires = ToriMem_Alloc(ToriMem_Script(), bytes);
        assert(dst->undefined_wires);
        memcpy(dst->undefined_wires, src->undefined_wires, bytes);
        dst->undefined_wire_count = src->undefined_wire_count;
    }

    dst->int_operands = ToriMem_Alloc(ToriMem_Script(), (size_t)src->op_count * sizeof(*dst->int_operands));
    assert(dst->int_operands);
    memcpy(dst->int_operands, src->int_operands, (size_t)src->op_count * sizeof(*dst->int_operands));

    if( src->long_operands )
    {
        dst->long_operands = ToriMem_Alloc(ToriMem_Script(), (size_t)src->op_count * sizeof(*dst->long_operands));
        assert(dst->long_operands);
        memcpy(dst->long_operands, src->long_operands, (size_t)src->op_count * sizeof(*dst->long_operands));
    }

    if( src->string_operands )
    {
        dst->string_operands =
            ToriMem_Calloc(ToriMem_Script(), (size_t)src->op_count, sizeof(*dst->string_operands));
        assert(dst->string_operands);
        for( i = 0; i < src->op_count; i++ )
        {
            if( !src->string_operands[i] )
                continue;
            dst->string_operands[i] = ToriMem_Strdup(ToriMem_Script(), src->string_operands[i]);
            assert(dst->string_operands[i]);
        }
    }
    CS2VM2_ScriptPrepare(dst);

    if( src->jit )
        CS2VM2_JitCopy(src, dst);

    return true;
}

/* A string constant: one allocation, freed with the script. */
static struct CS2VM2_Str*
script_constant_new(char const* text)
{
    size_t const len = strlen(text);
    struct CS2VM2_Str* s;

    assert(len <= UINT32_MAX);
    s = ToriMem_Alloc(ToriMem_Script(), sizeof(*s) + len + 1u);
    assert(s);
    s->len = (uint32_t)len;
    s->cap = (uint32_t)len;
    memcpy(s->text, text, len + 1u);
    return s;
}

static struct CS2VM2_Str**
script_constants_from(
    int op_count,
    char* const* text)
{
    struct CS2VM2_Str** constants =
        ToriMem_Calloc(ToriMem_Script(), (size_t)op_count, sizeof(*constants));

    assert(constants);
    for( int i = 0; i < op_count; i++ )
        if( text[i] )
            constants[i] = script_constant_new(text[i]);
    return constants;
}

void
CS2VM2_ScriptEnsureConstants(struct CS2VM2_Script* script)
{
    assert(script);
    if( script->string_constants || !script->string_operands )
        return;
    assert(script->op_count > 0);
    script->string_constants = script_constants_from(script->op_count, script->string_operands);
}

void
CS2VM2_ScriptPrepare(struct CS2VM2_Script* script)
{
    assert(script);
    assert(script->op_count > 0);
    assert(script->opcodes);
    /* Every opcode is a canonical instruction a script can hold: what every
     * switch over them relies on to have no default (cs2vm2_dialect.h). A
     * loaded script is translated by its dialect's table, which produces
     * nothing else; a script built by hand is written in them. */
    for( int pc = 0; pc < script->op_count; pc++ )
        assert(script->opcodes[pc] < CS2_OP_PLAIN_COUNT);

    CS2VM2_ScriptEnsureConstants(script);
    /* A script with no string operand still gets its row of NULLs: the loop
     * reads constants[pc] for every handler instruction without a test. */
    if( !script->string_constants )
    {
        script->string_constants = ToriMem_Calloc(
            ToriMem_Script(), (size_t)script->op_count, sizeof(*script->string_constants));
        assert(script->string_constants);
    }

    /* Written again on every call, so a script whose opcodes were changed by
     * hand (a test) is prepared again by calling this again. */
    if( !script->prepared_ops )
    {
        script->prepared_ops =
            ToriMem_Calloc(ToriMem_Script(), (size_t)script->op_count + 1u, sizeof(*script->prepared_ops));
        assert(script->prepared_ops);
    }
    CS2VM2_ScriptBuildRunOps(script, script->prepared_ops);
    if( !script->jit )
        script->run_ops = script->prepared_ops;
    /* A table for the gosubs' callees, empty: the first call at each asks. */
    CS2VM2_CalleePrepare(script);
}
