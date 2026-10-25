/*
 * PLAYER_INFO / NPC_INFO at revision 241: 3rd/rsprot/streams' readers
 * (player_info_osrs241 -> player_info_v6, npc_info_small_v6_osrs241 ->
 * npc_info_small_v6) over the session's table (net/rev/rsprot_entity_state.c).
 *
 * The names stay because the login, the REBUILD and the hosts that decode
 * several streams (scriptrun) state the table through them.
 */

#include "net/rev/rsprot_entity_state.h"
#include "streams/rsprot_info.h"

#include <stddef.h>
#include <stdint.h>

void
osrs241_playerinfo_set_local(int local_index)
{
    rsprot_entity_set_local(local_index);
}

size_t
osrs241_playerinfo_state_size(void)
{
    return rsprot_entity_state_size();
}

void
osrs241_playerinfo_state_save(void* out)
{
    rsprot_entity_state_save(out);
}

void
osrs241_playerinfo_state_load(void const* in)
{
    rsprot_entity_state_load(in);
}

void
osrs241_playerinfo_init(
    uint8_t const* data,
    int len)
{
    rsprot_entity_init(data, len);
}

int
osrs241_player_info_read(
    uint8_t const* data,
    int len,
    RsprotInfoList* out)
{
    return rsprot_entity_player_read(player_info_osrs241, data, len, out);
}

int
osrs241_npc_info_read(
    uint8_t const* data,
    int len,
    RsprotInfoList* out)
{
    return rsprot_entity_npc_read(npc_info_small_v6_osrs241, data, len, out);
}

int
osrs241_npc_info_read_large(
    uint8_t const* data,
    int len,
    RsprotInfoList* out)
{
    return rsprot_entity_npc_read(npc_info_large_v6_osrs241, data, len, out);
}
