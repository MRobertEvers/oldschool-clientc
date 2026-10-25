/* The session's OSRS player table. See rsprot_entity_state.h. */
#include "rsprot_entity_state.h"

#include <assert.h>
#include <string.h>

static RsprotPlayerInfo g_pi;

int
rsprot_entity_player_read(
    RsprotPlayerInfoReadFn read,
    uint8_t const* data,
    int len,
    RsprotInfoList* out)
{
    assert(read);
    assert(out);
    return read(&g_pi, data, len, out);
}

int
rsprot_entity_npc_read(
    RsprotNpcInfoReadFn read,
    uint8_t const* data,
    int len,
    RsprotInfoList* out)
{
    assert(read);
    assert(out);
    return read(data, len, NULL, NULL, out);
}

void
rsprot_entity_set_local(int local_index)
{
    rsprot_player_info_reset(&g_pi, local_index);
}

void
rsprot_entity_init(
    uint8_t const* data,
    int len)
{
    rsprot_player_info_init(&g_pi, data, len);
}

size_t
rsprot_entity_state_size(void)
{
    return sizeof(g_pi);
}

void
rsprot_entity_state_save(void* out)
{
    assert(out);
    memcpy(out, &g_pi, sizeof(g_pi));
}

void
rsprot_entity_state_load(void const* in)
{
    assert(in);
    memcpy(&g_pi, in, sizeof(g_pi));
}
