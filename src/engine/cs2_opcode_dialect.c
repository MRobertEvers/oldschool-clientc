#include "cs2_opcode_dialect.h"

#include "cs2vm2/cs2_opcode.h"

#include <assert.h>

struct CS2VM2_Dialect const*
CS2_DialectForCache(const struct RSCache* cache)
{
    assert(cache);
    if( RSCache_IsRs2Dat2(cache) )
        return &CS2VM2_DIALECT_RS2_634;
    if( cache->game == RSCACHE_GAME_OLDSCHOOL && cache->revision != RSCACHE_REVISION_UNKNOWN )
    {
        if( cache->revision <= 229 )
            return &CS2VM2_DIALECT_OSRS_229;
        if( cache->revision == 230 )
            return &CS2VM2_DIALECT_OSRS_230;
    }
    return &CS2VM2_DIALECT_OSRS;
}

uint16_t
CS2_OpcodeTranslate(
    struct CS2VM2_Dialect const* dialect,
    uint16_t wire_opcode)
{
    assert(dialect);
    for( int i = 0; i < dialect->remap_count; i++ )
        if( dialect->remap[i].wire == wire_opcode )
            return dialect->remap[i].canonical;
    return wire_opcode;
}
