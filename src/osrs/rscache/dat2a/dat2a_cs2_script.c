#include "dat2a_cs2_script.h"

#include <stdlib.h>
#include <string.h>

void
RSCache_CS2_ScriptInit(struct RSCache_CS2_Script* script)
{
    if( !script )
        return;
    memset(script, 0, sizeof(*script));
}

void
RSCache_CS2_ScriptFree(struct RSCache_CS2_Script* script)
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
