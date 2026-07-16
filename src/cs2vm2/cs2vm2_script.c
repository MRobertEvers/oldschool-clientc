#include "cs2vm2_script.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
CS2VM2_ScriptInit(struct CS2VM2_Script* script)
{
    assert(script);
    memset(script, 0, sizeof(*script));
}

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script)
{
    if( !script )
        return;

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
