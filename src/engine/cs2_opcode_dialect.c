#include "cs2_opcode_dialect.h"

#include "cs2vm2/cs2_opcode.h"

#include <assert.h>

enum CS2_OpcodeDialect
CS2_OpcodeDialectForCache(const struct RSCache* cache)
{
    assert(cache);
    if( RSCache_IsRs2Dat2(cache) )
        return CS2_OPCODE_DIALECT_RS2_DAT2;
    return CS2_OPCODE_DIALECT_CANONICAL;
}

uint16_t
CS2_OpcodeTranslate(
    enum CS2_OpcodeDialect dialect,
    uint16_t wire_opcode)
{
    if( dialect == CS2_OPCODE_DIALECT_RS2_DAT2 )
    {
        /* 634 SWITCH lives at wire 51; canonical moved it to 60 and reused 51
         * for GET_VARC_LONG. Translate so the VM never sees the collision. */
        if( wire_opcode == 51 )
            return (uint16_t)CS2_OP_SWITCH;
    }
    return wire_opcode;
}
