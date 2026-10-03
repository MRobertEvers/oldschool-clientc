# Waves seam pass 3 triage (2026-10-03): what blocks a measured fight

Pass: `matthew-mbp-m4-waves-b1-seam3`. Written by the waves orchestrator from the rows
seam passes 1 and 2 wrote in `docs/minigames/waves_loop/CONTENT_BUGS.md` (ENG-1 to ENG-19).
This pass takes the faults that make a fought measurement wrong or impossible, so the
Inferno spec pass can fight: the wave that re-spawns, the eat that holds hits, the shared
combat rules, prayer regeneration and drain, and the client's npc pool after a leave.
The Inferno's own presentation and geometry faults (the entrance ENG-6 and ENG-7, pause by
logout ENG-8, the pillars ENG-13, the nibblers' target ENG-14, the bat's flight ENG-12)
wait for the spec tables: they are content seams with sourced values, after the spec pass.

Rules for every seam here:

- **Measure first, on this branch's HEAD, with the tick log**, and quote the rows. Then the
  source line. Then the fix. Then the same measurement again. A fix with no source line
  for what the real game does is not made; the seam is reported UNSOURCED.
- **Which source.** This server plays the rev-239 game, and neither wave minigame existed in 2004
  (owner, 2026-10-03): the 2004 source (LostCity) is NOT a source in this loop. Do not cite
  it, do not port from it, and do not treat a difference from it as a defect. A rule is
  settled from a Jagex newspost, the cache, the pinned modern wiki mechanics page, plugin
  code or a recording. Rows in `CONTENT_BUGS.md` that cite 2004 code are to be re-sourced
  by you from those, or reported UNSOURCED and left unchanged.
- **The fixers run one at a time** and each rebuilds the script pack after its own content
  edit (`make -C <worktree>/src torirsserver-scripts > <log> 2>&1`). C changes are proved
  with a PRIVATE client (`PLATFORM_OBJ_BASE`, `PLATFORM_TARGET`, `QUEST_BINARY`); the closer
  builds the shared one.
- **These are tree-wide changes.** Each fixer runs `cooks_assistant`, `druid` and two quest
  tests that fight and eat (pick two green ones from `test/quests/QUEUE.tsv` whose file
  uses `opts.eat`), before and after, and reports any row that moved. The closer's full
  suite decides; a green quest turned red is kept as a patch, not committed.
- New var ids go in `docs/minigames/waves_loop/MERGE_CHECKLIST.md` (names carry the id).
- Copies from the raid branch's CONTENT repo are pinned to content commit `7936c59bf9`
  (content merge base with `v3`: `315ffdff00`), verbatim where `v3` has not changed the
  file since the base, three-way merged where it has, each a row in `FORKED_FROM.md`.
  Never cherry-pick: the raid's content commits carry raid work.

## content: inferno_wave_respawn

Units: every Inferno wave unit (no wave can be cleared while it re-spawns).

Files: `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/scripts/inferno_waves.rs2`

Summary. ENG-5: a wave's set spawns again every 8 ticks because the `inferno_wave_tick`
softtimer (`inferno_waves.rs2:154`) is not cleared when the wave begins
(`~inferno_begin_wave`, `:64`). Measured three times in seam pass 2 (npc_spawn rows at
ticks 13, 21, 29, 37, 45). Sources: the wave table spawns each wave once
(`docs/minigames/inferno/SOURCES.md`, the wave table rows and Blert's per-wave spawn
events in `sources/blert/SAMPLE_SUMMARY.md`); a softtimer in OUR engine repeats until cleared (quote our engine's line). Fix the timer's lifetime so a wave spawns once, the next wave
starts only after the last npc of the wave is dead, and the delay between a clear and the
next spawn is whatever the content already intends (do NOT retune it: read it, measure it,
report it with the constant's name, and leave its value for the spec pass to grade).
Check the same fault in the Jad, triple Jad and Zuk entries (`inferno_jad.rs2`,
`inferno_zuk.rs2` are NOT yours: if they share the fault, report the lines).
Prove by PLAYING: `t.wave.enter("inferno", 1)`, kill wave 1's nibblers with real attacks
(bring-alongs in setup only), and show from the tick log one spawn set for wave 1, the
clear tick, one spawn set for wave 2, and `t.wave.state()` reading wave 2; then leave.

Evidence. `CONTENT_BUGS.md` ENG-5; `build/seam_state/matthew-mbp-m4-waves-b1-seam2/fix.wave_enter_state_pause.json`.

## content: eat_delay_port

Units: every unit whose fight eats or drinks (all of them from the bat on); the full run.

Files: `OSRS-Content: every path that content commit 7936c59bf9 changed outside minigames/minigame_tob, minigame_toa and minigame_cox (list them with git show --stat --name-only 7936c59bf9; the consumption scripts, consume_shared.rs2, consume_delay.varp, food.rs2, pack/varp.alloc)`, `src/torirsserver/torirs_server_world_selftest.c`, `docs/minigames/waves_loop/FORKED_FROM.md`, `docs/minigames/waves_loop/MERGE_CHECKLIST.md`

Summary. ENG-16, ENG-17, ENG-18 (and ENG-4's third item), measured in seam pass 2: an eat's
`p_delay(2)` holds a queued PROJECTILE hit until the delay ends (cast 30, hit 33 instead of
31); an eat costs 0, 1 or 2 ticks of the next swing depending on where in the swing it
lands; a drink ends in `p_stopaction` and wipes an Attack pressed during it. Sources: the modern rule on the pinned wiki. Pin the Food page (its delay section), the Potion page, Tick eating and Combo eating with `tools/toa_fetch_wiki.py` into `docs/minigames/inferno/sources/wiki/` and quote the sentences that state: what an eat delays (the player's next attack, by how many ticks), whether a potion delays an attack, what combines in one tick, and whether an incoming hit lands on its own tick while the player eats (tick eating rests on that). The existing rows cite 2004 code: that is not a source; re-source each from these pages. A behaviour no modern source states is left as it is and reported UNSOURCED.
The raid loop ported this on the owner's decision as content commit `7936c59bf9`: bring that port over as the opening rules say, then check every behaviour it changes against the modern sources above yourself: a part of the port that rests only on 2004 code and that no modern source supports is left out and listed. Restore the selftest stanza seam pass 1 left out
(`torirs_server_world_selftest.c`, the raid tip's eat-delay loop at 19625-19652 of parent
commit `94f55b306`) and write the two conformance rows it left out
(`seam.eat_does_not_hold_queued_hit`, `seam.eat_delay_clocks`) as your snippet.
`pack/varp.alloc` is changed on both sides: three-way merge, and every new varp id goes in
the merge checklist. Prove with seam pass 2's own scratch (`scratch` files of
`supplies_by_dose` under `build/seam_state/matthew-mbp-m4-waves-b1-seam2/`): a wizard's
spell hits on its normal tick through an eat; the swing after an eat is 3 ticks late; an
Attack pressed during a drink survives; `t.player.drink`'s `then_attack` still works.

Evidence. `CONTENT_BUGS.md` ENG-4, ENG-16, ENG-17, ENG-18; `docs/WAVES_ORCHESTRATOR.md`
section 5 (the four engine findings) and section 10 lesson 14.

## engine: shared_combat_rules

Units: inferno_nibblers_and_pillars (a nibbler must not turn on the player), every unit
fought with magic, inferno_pause_and_logout.

Files: `src/torirsserver/torirs_server_combat.c`, `OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/combat.rs2`, `OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/player_magic.rs2`, `OSRS-Content/osrs239-content/server/scripts/interface_logout/scripts/logout.rs2`

Summary. Three rules, each measured before it is touched.
(1) ENG-4 item 1: the default retaliation ignores an npc record's `retaliate=no`. Measure
on an npc whose cache record says so (find one in `configs/all.npc` with a capped grep;
`docs/minigames/inferno/sources/cache_npc.txt` says which Inferno records carry it): attack
it and read from the tick log whether it swings back. The raid loop fixed its bosses
through content (content commit `93707f5d60`, read it as reference with
`git -C <worktree>/OSRS-Content show --stat 93707f5d60`); the general rule belongs where
the default retaliation is decided. Source: the cache field itself, and a modern page or plugin that states the npc does not fight back (quote it).
(2) ENG-4 item 2: a spell cast never sets the magic damage type, so a style check treats
it as the weapon's style. Measure with a staff in hand: cast at an npc and read the hit's
type in the tick log's `hit_npc` row. The raid's fix is in `player_magic.rs2` at
`7936c59bf9` (28 lines): take that hunk as the opening rules allow, after checking it against the pinned wiki's statement of what a spell's damage type is (quote it).
(3) ENG-9: no combat logout delay: seam pass 2's run logged out on the tick it was last
hit. Source: pin the wiki's Logout page and quote its combat rule (how long after being attacked a logout is refused, and the message); the window's length comes from that page, not from 2004 code.
Prove: logout pressed within the window is refused with the game's message; pressed after
it, it logs out.
The tick log's recording hooks in `torirs_server_combat.c` stay exactly as they are.

Evidence. `CONTENT_BUGS.md` ENG-4, ENG-9; seam pass 1's `left_out` list
(`build/seam_state/matthew-mbp-m4-waves-b1-seam1/fix.driver_port.json`).

## content: prayer_regen_and_drain

Units: every unit (prayer is held through every wave); inferno_blob_and_splits (the flick).

Files: `OSRS-Content/osrs239-content/server/scripts/player/scripts/stat_restore.rs2`, `OSRS-Content/osrs239-content/server/scripts/skill_prayer/scripts/prayer.rs2`

Summary. (1) ENG-10: prayer points regenerate (91 to 93 over 220 ticks with nothing lit)
because `[timer,stat_restore]` skips only hitpoints. Source: the wiki's Prayer page (pin it; quote the sentence that says prayer points do not regenerate on their own). Fix.
(2) ENG-11: our drain adds the effect every tick and zeroes the counter when the last prayer goes off. The row calls that a defect because 2004 code differs; that is not a reason. Pin the wiki's Prayer page and its
"Prayer drain mechanics" section (drain effect per prayer, drain resistance from prayer
bonus, when the counter is checked, what switching off does to the counter) and settle
each difference from its sentences: change ours only where the modern rule says ours is
wrong, and quote it. Measure before and after with seam pass 2's prayer scratch: a 20-tick
hold, 20 one-tick flicks, a hold of two prayers, and a hold with prayer bonus worn; report
points drained against the figure the wiki's formula gives for each. A row that no source
settles stays as it is and is written as open.

Evidence. `CONTENT_BUGS.md` ENG-10, ENG-11; `fix.prayer_flick.json` of seam pass 2.

## engine: npc_pool_after_leave

Units: inferno_pause_and_logout, inferno_death_and_failure_reward, every test that enters
twice; any instanced content.

Files: `src/game/task_exec_entity_info.c`, `src/app/app_world_spawn.c`, `src/app/app_world_rebuild.c`, `src/world/world.c`

Summary. ENG-19, unsettled: after a practice run leaves by the Cave exit, the client's npc
pool still holds the arena's npcs (`t.wave.resume` read "pool 20: harpie x5, nibbler x15"
with the run inactive), so the conformance rows for waves had to run last. First find out
which side is wrong: does the server still list those npcs for the player after the
teleport out of the instance (the tick log and the server's own npc list for the player),
or does the client keep slots the server dropped (the npc info packet's removal path on a
teleport or a map rebuild)? Reproduce it outside the Inferno too (walk or teleport away
from any group of npcs: do their slots leave the pool?). Fix only the side that is wrong,
in the one file that holds the fault (name it; the other listed files are yours only if
the fault is there), with the reference client's behaviour as the source (the deob client
under the repo's reference trees: read-only; quote the removal rule). If the fault is the
Inferno content not deleting its npcs on leave (`inferno.rs2:190`), that is content: report
it as an engine finding with the line and change nothing.
Prove: enter, leave, and read an empty arena pool; enter again and read exactly one wave's
npcs; two green quests with npcs on screen still pass.

Evidence. `CONTENT_BUGS.md` ENG-19; seam pass 2's closer notes
(`build/seam_state/matthew-mbp-m4-waves-b1-seam2/close.progress.md`).
