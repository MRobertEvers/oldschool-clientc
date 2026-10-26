/*
 * The rs-family entry points: each revision's PLAYER_INFO and NPC_INFO, and
 * the constants its version reads (info_rs.h).
 */
#include "info_rs.h"

/* Client-TS getNpcPosNewVis: gBit(14) slot, terminator 16383; gBit(11) type. */
static NpcInfoRs const k_npc_info_rs245 = { .slot_bits = 14, .type_bits = 11 };
static NpcInfoRs const k_npc_info_rs254 = { .slot_bits = 14, .type_bits = 11 };
static NpcInfoRs const k_npc_info_rs289 = { .slot_bits = 14, .type_bits = 11 };

int
player_info_rs245(uint8_t const* data, int len, RsprotInfoList* out)
{
    return player_info_rs_v1(data, len, out);
}

int
player_info_rs254(uint8_t const* data, int len, RsprotInfoList* out)
{
    return player_info_rs_v1(data, len, out);
}

int
player_info_rs289(uint8_t const* data, int len, RsprotInfoList* out)
{
    return player_info_rs_v1(data, len, out);
}

int
npc_info_rs245(uint8_t const* data, int len, RsprotInfoList* out)
{
    return npc_info_rs_v1(&k_npc_info_rs245, data, len, out);
}

int
npc_info_rs254(uint8_t const* data, int len, RsprotInfoList* out)
{
    return npc_info_rs_v1(&k_npc_info_rs254, data, len, out);
}

int
npc_info_rs289(uint8_t const* data, int len, RsprotInfoList* out)
{
    return npc_info_rs_v2(&k_npc_info_rs289, data, len, out);
}
