# Waves seam pass 5 triage (2026-10-04): the Inferno's content, waves 1 to 66 and the systems

Pass: `matthew-mbp-m4-waves-b1-seam5`. Written by the waves orchestrator after the spec
pass's batches A and B (twelve tables under `docs/minigames/inferno/encounters/`, about
570 rows; `docs/minigames/waves_loop/SPEC_LEDGER.md`). Six seams, one fixer at a time, in
this order. Jad, Zuk, the glyph and the final-wave adds are seam pass 6, after batch C.

**How a content seam works here.** The spec table is the target, not the current script.
For your file: (1) list every row of `docs/minigames/waves_loop/CONTENT_BUGS.md` whose
`where` names it and whose `fixed in` is empty, and every row of the unit tables named
below where our measured value differs from `spec_value` (the tables' `measured_by` and
the batch reports under `build/spec_state/matthew-mbp-m4-waves-b1-spec-inferno-{a,b}/`
say what was measured). (2) For each, re-read the SOURCE line the row cites before
touching the script (a rate is not an odds ratio; an animation's length is not server
ticks; a client HUD reading is not a server value). (3) Fix rows at grade A, B or C.
A grade D row is fixed only when the source is the wiki (owner, 2026-10-03: the wiki is
extremely likely to be correct and outranks the cache); a grade E row is NOT changed: its
constant keeps its `[Mn]` tag. (4) Prove each fix by PLAYING the wave with real attacks,
prayer and food (bring-alongs in setup only), quoting tick-log rows before and after as a
whole distribution. (5) Write the commit that fixed it into the row's `fixed in` column
(the closer fills the sha) and note every row you did NOT fix and why.
Presentation: an animation, graphic, projectile or sound is placed only from a source
(the cache's own binding, `docs/minigames/inferno/RIG_ANIMATIONS.md` at tier bound,
rig+name or rig+sound, a plugin or Blert id, `docs/INFERNO_SOUNDS.md` rows a source
STATES); never invent a visual and never change a mechanic to fit one.
Sources: OSRS behaviour first; the 2004 source is never cited
(`docs/WAVES_ORCHESTRATOR.md` section 12). After any content edit:
`make -C <worktree>/src torirsserver-scripts > <log> 2>&1`. C is proved with a private
client; the closer builds the shared one. `MI` below is
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno`.

## content: inferno_waves_file

Units: wave_table; every wave unit.

Files: `MI/scripts/inferno_waves.rs2`, `MI/configs/inferno.constant`

Summary. Tables: `wave_table.tsv`. Rows: WT-1 to WT-5 (and WT-1-bat, WT-1-melee,
WT-1-ranger: no spawn tile of ours matches Blert's observed tiles), ENG-38 (the delay
between a clear and the next spawn: ours 8, Blert's OBSERVED events 6 in 901 of 909),
ENG-37 ("Wave: N" printed twice at entry; the second print is in this file, the first in
`inferno.rs2`: remove the duplicate here), BLOBLET-OFFSET. The spawn positions and the
rule that assigns them are a grade B table from Blert (`sources/blert/SAMPLE_SUMMARY.md`,
`sources/blert_api/`): rebuild our spawn tiles and assignment from it, per wave, and prove
with five entries at different waves that every npc spawns on a tile the recordings show
for that wave. In `inferno.constant` you own only the wave, spawn and delay constants;
other seams of this pass edit other blocks of it after you.

Evidence. `CONTENT_BUGS.md` WT-*, ENG-37, ENG-38; `wave_table.tsv`.

## content: inferno_monsters_file

Units: nibblers_and_pillars, bat, blob_and_splits, melee, ranger.

Files: `MI/scripts/inferno_ai.rs2`, `MI/configs/inferno.npc`, `MI/configs/inferno.constant`

Summary. Tables: `nibblers_and_pillars.tsv`, `bat.tsv`, `blob_and_splits.tsv`, `melee.tsv`,
`ranger.tsv`. Rows: NIB-CAD, NIB-TARGET, NIB-FIRST, NIB-ACC, ENG-14 (a nibbler picks ONE
pillar and stays on it); BAT-RUN, BAT-DRAIN, BAT-CAD, ENG-12 and ENG-12-b (the bat's hit
flight), ENG-59 (a bat killing an unprayed 99-hitpoint player in 40 ticks: compare its
accuracy and max hit with the table first); BLOB-SCAN (the blob reads the prayer one tick
before it attacks: the T-1 rule, the blob flick rests on it), BLOB-NOPRAY, BLOB-MELEE,
BLOB-FIRST, BLOBLET-IDLE, BLOB-RANGE; MELEE-DIG, MELEE-DIG-DELAY, MELEE-DIG-LAND;
RANGER-FIRST, RANGER-MELEE-CHANCE, RANGER-MELEE-MAX, RANGER-RANGE; RIG-7, RIG-8 (Jal-Xil's
default attack animation is its close-range one; Blert's `InfernoNpc.java` names the
auto's id); the hitpoints, levels and sizes in `inferno.npc` against the cache dump
(`sources/cache_npc.txt`). One monster at a time, each proved in a wave that carries it.
In `inferno.constant` you own only the monsters' blocks.

Evidence. `CONTENT_BUGS.md` NIB-*, BAT-*, BLOB-*, BLOBLET-*, MELEE-*, RANGER-*, ENG-12,
ENG-14, ENG-59, RIG-7, RIG-8; the five tables.

## content: inferno_pillars_file

Units: nibblers_and_pillars; every wave that uses a pillar safespot.

Files: `MI/scripts/inferno_pillars.rs2`, `MI/configs/inferno.npc`, `MI/configs/inferno.varp`

Summary. Tables: `nibblers_and_pillars.tsv`. Rows: INF-AV-002, INF-AV-003, ENG-13 (and
ENG-13-bat, ENG-13-ranger, ENG-13-46-mager: no monster's attack is blocked by a pillar),
ENG-45 (the pillar npcs wander), ENG-48 and ENG-48-b (pillars return at wave 68), ENG-55
and ENG-55-b, NIB-RADIUS, NIB-PILLAR-1HP (leave ours at the wiki's 2-4). The cache ships
the pillars as LOCS: `inferno_safespot1..3` (30353-30355), 3x3, `blockwalk=1`, with four
damage states (30284-30287) selected by varbits 5655-5657 over 0-63, 64-127, 128-191,
192-255 (`docs/minigames/inferno/AV_INVENTORY.md`, `sources/cache_locs.txt`); plugins key
on those loc ids; the dying pillar is npc 7710 with seq 7561. Ours are npc 7709 only,
invisible and non-blocking. Build the pillars as the cache has them: the loc placed in
the instance so that it blocks movement AND line of sight (prove with `t.world.los` from
behind each pillar to three spawn tiles: false; and by standing behind a pillar while a
ranger or mager is alive: no hit lands, from the tick log), its damage state driven by the
varbit from the pillar's hitpoints, the attackable npc kept only as far as nibblers need a
target and pinned to its tile, the collapse playing the dying pillar's record and
sequence, its damage and radius as the table's rows say. If placing a blocking loc in an
instance needs engine work, stop at the exact gap and report it as an engine finding with
the line; do not fake blocking in script.

Evidence. `CONTENT_BUGS.md` INF-AV-002, INF-AV-003, ENG-13*, ENG-45, ENG-48*, ENG-55*,
NIB-RADIUS; seam pass 2's `fix.los_and_pack.json`.

## content: inferno_mager_file

Units: mager_resurrection; bat, melee (their revived records).

Files: `MI/scripts/inferno_zek.rs2`

Summary. Table: `mager_resurrection.tsv`. Rows: MAGER-MELEE-CHANCE, MAGER-MELEE-MAX,
MAGER-RANGE, MAGER-REVIVE-GAP, MAGER-REVIVE-NIBBLER, MAGER-REVIVE-SPAWN, MAGER-REVIVE-TILE,
MAGER-REVIVE-TRIGGER, BAT-REVIVE, MELEE-REVIVE-HP: what the mager may revive, when, where,
with how many hitpoints, and its own attack rule. Batch B killed no mager and saw no
revive chain, so measure first: enter a mager wave, kill another monster, and show the
revive in the tick log (who, the tick gap, the tile, the hitpoints), then fix the rows the
table grades A to C (or D from the wiki), then kill the mager itself.

Evidence. `CONTENT_BUGS.md` MAGER-*, BAT-REVIVE, MELEE-REVIVE-HP; `mager_resurrection.tsv`.

## content: inferno_entry_pause_death_file

Units: entry_and_cape, pause_and_logout, death_and_failure_reward, practice_mode,
completion_reward_and_pet.

Files: `MI/scripts/inferno.rs2`, `MI/scripts/inferno_practice_fee.rs2`, `MI/configs/inferno_practice_fee.constant`, `MI/configs/inferno.constant`, `OSRS-Content/osrs239-content/server/scripts/interface_logout/scripts/logout.rs2`

Summary. Tables: `entry_and_cape.tsv`, `pause_and_logout.tsv`,
`death_and_failure_reward.tsv`, `practice_mode.tsv`, `completion_reward_and_pet.tsv`.
(1) ENTRY BY CLICK: ENG-6, INF-AV-001 (write the entrance's varbit to the value the
cache's multiloc binds to the op: 2, not 1), ENG-7 (the walk goes to a copy the client
does not draw), ENTRY-1 to ENTRY-4, ENG-37's first print. The fire cape sacrifice and
Jump-in must be reachable by clicking the entrance and TzHaar-Ket-Keh, and a REAL (not
practice) run must start that way. (2) PAUSE: ENG-8, PL-1 to PL-5: the logout button
inside a run asks, then logs the player out when the wave ends, and logging in resumes
paused before the next wave, as the pinned wiki Inferno page states (quote it); seam pass
2's `t.wave.pause / resume` then work by click. ENG-29 (the client's 5-second logout
fallback ends a logout the server refused) is the engine's, not yours: leave it, it is in
the shared seam. (3) DEATH: DEATH-1 to DEATH-4: what is kept, where the player lands, the
Tokkul for a failed run by wave (the table's rows; no pinned Tokkul-by-wave table exists,
so that row stays as graded). (4) PRACTICE: PRACTICE-1 to PRACTICE-3: no pinned source
states practice rules; change nothing there that is grade E, and keep `::inferno` as the
practice entry the driver uses. (5) REWARD: REWARD-1 to REWARD-5, INF-AV-008, INF-AV-009:
the cape, the pet roll and the sacrifice rule, the Zuk kill count varp 1585 (it also
drives TzHaar-Ket-Keh's Exchange op and the hiscores), the combat achievement bits and the
collection log bit. The kill itself is in `inferno_zuk.rs2` (seam pass 6): write the
reward proc here so that pass only has to call it, and prove it with the content's own
kill path under the readouts batch A used, saying plainly that the kill was not fought.
In `inferno.constant` you own only the entry, exit, pause and reward blocks.

Evidence. `CONTENT_BUGS.md` ENG-6, ENG-7, ENG-8, ENG-37, ENTRY-*, PL-*, DEATH-*,
PRACTICE-*, REWARD-*, INF-AV-001, INF-AV-008, INF-AV-009; the five tables.

## engine: shared_followups

Units: every unit (death, eating, logout); the tree's own checks.

Files: `OSRS-Content/osrs239-content/server/scripts/player/death.rs2`, `tools/check_gauntlet_contract.py`, `src/app/app_logout.c or whichever client file holds the 5-second logout fallback (name it)`

Summary. (1) ENG-60: prayer is not restored on death (quest test `mm`'s end save 20 of 52
after a death and respawn, `build/probe_state/v3merge/mm/`). Find whether the death
restore skips prayer or seam pass 4's prayer change made it skip; pin the wiki Death page
and quote what is restored; fix; prove with a real death (a staged low-hitpoint fight, not
`::die`) and the stats read after the respawn. (2) ENG-56: `tools/check_gauntlet_contract.py`
lines 190-194 pin the Gauntlet's old eat lines, so `make -C src check-gauntlet-contract`
is red since seam pass 4 changed the paddlefish to OSRS eating rules: update the pin to
the new lines and show the check green. (3) ENG-58: the conformance row
`seam.eat_does_not_hold_queued_hit` also passes on the old content: write a sharper row as
your snippet (a projectile hit queued before the eat must land on its own tick; seam pass
3's wizard scratch shows how) and say which old-content behaviour it now fails on, proved
in a throwaway worktree with the old consumption files (CLAUDE.md: never mutate this
tree). (4) ENG-29: the client's 5-second logout fallback ends a logout the server refused
(the 16-tick combat rule): find it, and make the client wait for the server's answer as
the reference client does (quote the reference client's rule from the repo's reference
trees, read-only); if no reference states it, report UNSOURCED and change nothing.

Evidence. `CONTENT_BUGS.md` ENG-29, ENG-56, ENG-58, ENG-60.
