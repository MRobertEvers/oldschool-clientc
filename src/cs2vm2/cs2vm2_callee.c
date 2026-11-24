/* Remembered gosub callees: see cs2vm2_callee.h. */
#include "cs2vm2_callee.h"

#include "cs2_opcode.h"
#include "mem/torirs_mem.h"

#include <string.h>

uint32_t g_cs2vm2_script_epoch;

static bool
callee_script_has_gosub(struct CS2VM2_Script const* script)
{
    for( int pc = 0; pc < script->op_count; pc++ )
    {
        if( script->opcodes[pc] == CS2_OP_GOSUB_WITH_PARAMS )
            return true;
    }
    return false;
}

void
CS2VM2_CalleePrepare(struct CS2VM2_Script* script)
{
    assert(script);
    assert(script->op_count > 0);
    assert(script->opcodes);

    if( !callee_script_has_gosub(script) )
    {
        /* Prepared again after its opcodes changed (a test): no gosub left. */
        ToriMem_Free(ToriMem_Script(), script->callees);
        script->callees = NULL;
        return;
    }
    if( !script->callees )
    {
        script->callees =
            ToriMem_Calloc(ToriMem_Script(), (size_t)script->op_count, sizeof(*script->callees));
        assert(script->callees);
    }
    else
        memset(script->callees, 0, (size_t)script->op_count * sizeof(*script->callees));
    script->callees_epoch = g_cs2vm2_script_epoch;
}

void
CS2VM2_CalleeRemember(
    struct CS2VM2_Script* script,
    int pc,
    struct CS2VM2_Script* callee)
{
    assert(script);
    assert(script->callees);
    assert(callee);
    assert(pc >= 0);
    assert(pc < script->op_count);
    assert(script->opcodes[pc] == CS2_OP_GOSUB_WITH_PARAMS);
    /* Written under an older epoch: every entry may name a freed script. */
    if( script->callees_epoch != g_cs2vm2_script_epoch )
    {
        memset(script->callees, 0, (size_t)script->op_count * sizeof(*script->callees));
        script->callees_epoch = g_cs2vm2_script_epoch;
    }
    script->callees[pc] = callee;
}

void
CS2VM2_CalleeScriptRetired(struct CS2VM2_Script* script)
{
    assert(script);
    ToriMem_Free(ToriMem_Script(), script->callees);
    script->callees = NULL;
    g_cs2vm2_script_epoch++;
}
