#include "game/rs_player_ops.h"

#include <assert.h>
#include <stdio.h>

void
RS_PlayerOps_Set(
    struct RS_PlayerOps* ops,
    int index,
    char const* text,
    int primary)
{
    assert(ops);
    assert(index >= 0);
    assert(index < RS_PLAYER_OPS_SLOTS);
    assert(text);
    snprintf(ops->text[index], sizeof(ops->text[index]), "%s", text);
    ops->primary[index] = primary;
}

void
RS_PlayerOps_SetAttackOptions(
    struct RS_PlayerOps* ops,
    int player_attack_option,
    int npc_attack_option)
{
    assert(ops);
    ops->player_attack_option = player_attack_option;
    ops->npc_attack_option = npc_attack_option;
}

void
RS_PlayerOps_SetPlayerAttackOption(
    struct RS_PlayerOps* ops,
    int option)
{
    assert(ops);
    ops->player_attack_option = option;
}

void
RS_PlayerOps_SetNpcAttackOption(
    struct RS_PlayerOps* ops,
    int option)
{
    assert(ops);
    ops->npc_attack_option = option;
}
