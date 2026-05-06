#ifndef OSRS_GAMENET_PROCESS_H
#define OSRS_GAMENET_PROCESS_H

/* gamenet_process — drain the pending parsed-packet queue each game cycle.
 *
 * If the revision has packets queued (parsed inbound but not yet executed),
 * push a SCRIPT_PKT_DISPATCH event onto the script queue so Lua can process
 * them this frame.
 *
 * Supersedes the old gameproto_process.{c,h} files (now deleted).
 */

struct GGame;

void
gamenet_process(struct GGame* game);

#endif /* OSRS_GAMENET_PROCESS_H */
