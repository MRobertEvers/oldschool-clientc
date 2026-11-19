#ifndef SRC_GAME_RS_PLAYER_OPS_H
#define SRC_GAME_RS_PLAYER_OPS_H

/*
 * What the menu may offer on another player, and how the two Attack options
 * rank an Attack row.
 *
 * The five player ops are the server's: SET_PLAYER_OP names slot 1..5 (stored
 * at 0..4), its text, and whether the op is primary -- a non-primary op is
 * deprioritised. An empty text is no op. A text longer than the slot is cut to
 * fit.
 *
 * The Attack options (enum RS_AttackOption) are derived state, not varp
 * storage: they are assigned from varp clientcode 18 / 22 and at a session's
 * boot value, and nothing else changes them. A VARP_RESET leaves them alone.
 *
 * This does not know which revision models the Attack options as a setting,
 * what a varp holds, or which slot numbers the wire may legally send. The
 * caller decides whether a varp may assign an option, converts its value,
 * checks the slot, and picks the boot value.
 */

#define RS_PLAYER_OPS_SLOTS 5
#define RS_PLAYER_OPS_TEXT_MAX 40

struct RS_PlayerOps
{
    /** SET_PLAYER_OP rows for the player context menu (slot 1..5 -> [0..4]). */
    char text[RS_PLAYER_OPS_SLOTS][RS_PLAYER_OPS_TEXT_MAX];
    int primary[RS_PLAYER_OPS_SLOTS];
    /** Controls-settings "Attack" options (enum RS_AttackOption; reference
     *  client.playerAttackOption / npcAttackOption). */
    int player_attack_option;
    int npc_attack_option;
};

/** SET_PLAYER_OP for `index` 0..4 (the wire's slot minus one). `text` is
 *  copied, cut to RS_PLAYER_OPS_TEXT_MAX - 1 bytes. */
void
RS_PlayerOps_Set(
    struct RS_PlayerOps* ops,
    int index,
    char const* text,
    int primary);

/** Both Attack options at once, player first: a session's boot value. */
void
RS_PlayerOps_SetAttackOptions(
    struct RS_PlayerOps* ops,
    int player_attack_option,
    int npc_attack_option);

/** One Attack option, as varp clientcode 18 (player) / 22 (npc) assigns it. */
void
RS_PlayerOps_SetPlayerAttackOption(
    struct RS_PlayerOps* ops,
    int option);
void
RS_PlayerOps_SetNpcAttackOption(
    struct RS_PlayerOps* ops,
    int option);

#endif /* SRC_GAME_RS_PLAYER_OPS_H */
