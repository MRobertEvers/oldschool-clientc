/* The session's OSRS player table. See proto_info.h. */
#include "proto_info.h"

#include <assert.h>
#include <string.h>

static RsprotPlayerInfo g_pi;

int
proto_info_player_read(
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
proto_info_npc_read(
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
proto_info_set_local(int local_index)
{
    rsprot_player_info_reset(&g_pi, local_index);
}

void
proto_info_init(
    uint8_t const* data,
    int len)
{
    rsprot_player_info_init(&g_pi, data, len);
}

int
proto_info_local_coord(int* abs_x, int* abs_z, int* level)
{
    int idx = g_pi.local_index;

    assert(abs_x);
    assert(abs_z);
    assert(level);
    if( !g_pi.seeded || idx <= 0 || idx >= RSPROT_INFO_PLAYER_SLOTS )
        return 0;
    *abs_x = (g_pi.coord[idx] >> 14) & 0x3fff;
    *abs_z = g_pi.coord[idx] & 0x3fff;
    *level = (g_pi.coord[idx] >> 28) & 0x3;
    return 1;
}

size_t
proto_info_state_size(void)
{
    return sizeof(g_pi);
}

void
proto_info_state_save(void* out)
{
    assert(out);
    memcpy(out, &g_pi, sizeof(g_pi));
}

void
proto_info_state_load(void const* in)
{
    assert(in);
    memcpy(&g_pi, in, sizeof(g_pi));
}
