#include "game/rs_npc_body_lands.h"

#include <assert.h>

int
RS_NpcBodyLands_InFlight(
    struct RS_NpcBodyLands const* lands,
    int base_npc_id,
    int npc_id)
{
    assert(lands);
    for( int i = 0; i < lands->count; i++ )
        if( lands->base[i] == base_npc_id && lands->type[i] == npc_id )
            return 1;
    return 0;
}

int
RS_NpcBodyLands_Register(
    struct RS_NpcBodyLands* lands,
    int base_npc_id,
    int npc_id)
{
    assert(lands);
    if( lands->count >= RS_NPC_BODY_LANDS_MAX )
        return 0;
    lands->base[lands->count] = base_npc_id;
    lands->type[lands->count] = npc_id;
    lands->count++;
    return 1;
}

void
RS_NpcBodyLands_Forget(
    struct RS_NpcBodyLands* lands,
    int base_npc_id,
    int npc_id)
{
    assert(lands);
    for( int i = 0; i < lands->count; i++ )
    {
        if( lands->base[i] != base_npc_id || lands->type[i] != npc_id )
            continue;
        lands->count--;
        lands->base[i] = lands->base[lands->count];
        lands->type[i] = lands->type[lands->count];
        return;
    }
}
