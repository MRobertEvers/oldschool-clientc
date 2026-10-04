# Does a hit cancel a script paused in p_delay? No. (2026-10-04)

Question (owner): Legends' seed planting "wasted a seed" when a jungle wolf was attacking;
is that real behaviour? An Opus investigator read both engines and ran 22 plantings with
a wolf present. Verdict: being hit does NOT cancel the planting. Every lost seed was the
Herblore roll failing (`stat_random(herblore, 40, 243)`, legends_yommi.rs2:30: 132/256 at
level 45, 244/256 at 99), which shows "You planted the seed incorrectly ... withers and
dies" and never shows a baby tree. No engine fix is needed. The b58 legends fixer misread
a failed roll as "the wolf's hit cancelled the planting"; that run's ledger and shot were
overwritten, so its own loss is attributed by elimination, not by a row.

## Engines
- LostCity: p_delay marks the player delayed and suspends the script
  (engine/src/engine/script/handlers/PlayerOps.ts:376-380); it resumes before the queues
  (World.ts:693-697); clicks while delayed are dropped (World.ts:620-623); queues wait on
  canAccess() (Player.ts:821-831); a hit's if_close drops only dialogue waits
  (Player.ts:775-778); auto-retaliate is a queued script held behind canAccess().
- Ours: same timing (torirs_server_scripts.c:10117-10127); resume before queues
  (torirs_server_world.c:15182-15185, scripts.c:1428-1443); queues gated the same way
  (scripts.c:938-947); damage never touches the parked script
  (torirs_server_combat.c:1837-1850). A parked script is dropped only by its own finish
  or abort, a dialogue close (dialogue-type waits only), logout or teardown.

## Probes (build/orchestrator/probe_delay/probe{1,2,3}.lua, ledgers build/quest_gate/probe_delay_{1,2,3})
- Herblore 99, wolf fighting, then plant: seeds 8 -> 7, baby, baby, SAPLING with a hit on
  the sapling tick. Same with auto-retaliate off, with a walk click inside the delay, and
  with an attack click inside the delay.
- Herblore 45: 5 of 8 withered with the mesbox and no baby tree; 3 grew, two of them
  taking hits during the baby phase.
- Totals: 22 plantings with a wolf present, 14 saplings, 8 seeds lost, all to the roll,
  0 cancels.

## Follow-ups
- legends.lua: keep the retry with the next seed (the real need); the "kill the wolf
  first" step may stay for the hp margin but the comment must say the roll, not the hit.
- Conformance row worth adding (`pdelay.survives_hit`): plant with a wolf attacking at
  Herblore 99, PASS only if baby -> sapling, seeds down by one, the "needs to be watered"
  line, and hp fell inside the window.
- Two divergences noted, neither the cause: ours accepts walk clicks and world ops while
  delayed (documented, world.c:12540-12575); our npc melee hit has no if_close
  (combat_stats.rs2:917) where LostCity's does (npc_combat_melee.rs2:36, which LostCity
  itself marks uncertain).
