#ifndef OSRS_GAMENET_EXEC_H
#define OSRS_GAMENET_EXEC_H

/* gamenet_exec — inbound state-mutation layer.
 *
 * Dispatches a fully-parsed, revision-tagged packet to the appropriate
 * revision-specific exec function (gamenet_rev245_2_exec_dispatch_v1 etc.)
 * which updates GGame / World state accordingly.
 *
 * This is a thin revision switch; no byte-level work happens here.
 */

struct GGame;
struct RevPacket_LC245_2;

void
gamenet_exec(struct GGame* game, struct RevPacket_LC245_2* packet);

#endif /* OSRS_GAMENET_EXEC_H */
