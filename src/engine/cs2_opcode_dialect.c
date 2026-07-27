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
        /* STRUCT_PARAM: 634 wire 4500, canonical 6516. Same stack shape — pop
         * (struct, param), push int or string by the ParamType — so the existing
         * handler is correct once the id lines up. */
        if( wire_opcode == 4500 )
            return (uint16_t)CS2_OP_STRUCT_PARAM;
    }
    return wire_opcode;
}
