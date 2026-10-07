# Waves seam pass 4 triage (2026-10-04): land the two held fixes, and two faults that block batch B

Pass: `matthew-mbp-m4-waves-b1-seam4`. Written by the waves orchestrator. Four seams, one
fixer at a time, in this order.

**The owner's decisions (2026-10-03, night), which this pass carries out:**
"Eat delay should follow osrs rules." and "Prayer does not regenerate. That was never a
thing." Seam pass 3 proved both fixes and held them as patches because they turn five green
quest tests red. They land now. The five tests are EXPECTED red after this pass and are
the quest loop's to rework (it has been told, with the rows): `troll` (row 31),
`contact` (row 214), `regicide` (row 205) from the eat delay; `legends` (row 494) and `mm`
(row 133) from prayer. The closer does NOT hold these two seams as patches for those five.
Any OTHER quest that was green and turns red is still held and reported, as the card says.

Sources: OSRS behaviour first. The wiki outranks the cache where they disagree; the 2004
source is never cited (`docs/WAVES_ORCHESTRATOR.md` section 12). Every fixer measures
before and after with the tick log and quotes the rows.

## content: eat_delay_land

Units: every unit whose fight eats or drinks; the full run.

Files: `the files named in docs/minigames/waves_loop/patches/matthew-mbp-m4-waves-b1-seam3.eat_delay_port.content.patch and .parent.patch (list them with git apply --stat)`, `docs/minigames/waves_loop/FORKED_FROM.md`, `docs/minigames/waves_loop/MERGE_CHECKLIST.md`

Summary. Apply seam pass 3's held eat delay port: `git -C <repo> apply --3way <patch>` for
the content patch in `OSRS-Content` and the parent patch in the worktree (if a hunk does
not apply because the tree moved, resolve it by reading both sides; never drop a hunk
silently). Restore its held conformance rows from
`build/seam_state/matthew-mbp-m4-waves-b1-seam3/held/` as your snippet. Read seam pass 3's
report and notebook first (`fix.eat_delay_port.json`, `fix.eat_delay_port.progress.md`
there): the proof scratches, the eight pinned wiki pages, the three varps renumbered to
7223-7225. Re-run its before and after proof on this tree and quote the rows: a wizard's
spell through an eat lands on its normal tick; the swing after an eat; an Attack pressed
during a drink swings; food plus a potion on one tick is allowed. Then the eat paths the
port does not cover, which the fixer listed: `minigame_gauntlet/scripts/gauntlet_craft.rs2`
(paddlefish) is brought to the same rule here (add it to your files and say so);
`minigame_toa/scripts/toa_supplies.rs2` is the raid loop's content and `kebab.rs2:20` is a
quest branch: leave both, list them as open.

Evidence. `CONTENT_BUGS.md` ENG-16, ENG-17, ENG-18; seam pass 3's close report
(`build/seam_state/matthew-mbp-m4-waves-b1-seam3/close.progress.md`).

## content: prayer_land

Units: every unit (prayer is held through every wave); inferno_blob_and_splits.

Files: `the files named in docs/minigames/waves_loop/patches/matthew-mbp-m4-waves-b1-seam3.prayer_regen_and_drain.content.patch`, `OSRS-Content/osrs239-content/server/scripts/player/death.rs2`, `the prayer config file that states each prayer's drain effect (find it from skill_prayer/; name it in your report)`

Summary. Apply seam pass 3's held prayer patch the same way (you run after
`eat_delay_land`, whose patch also touches `death.rs2`: make the smallest edit there).
What the patch does: a drained prayer no longer restores over time (our `stat_restore`
timer was doing that; it is not an OSRS rule), no drain is charged on the activation tick,
and the drain counter is kept when prayers go off. Then the three rows seam pass 3 left
open, each settled from the pinned wiki Prayer page and fixed: (1) ENG-39: dying zeroes the
drain counter (`death.rs2`, the wiki's "or dying"); (2) a prayer lit while another already
drains must not be charged on its own activation tick (seam pass 3 measured 264 against
the wiki's 228 for that case); (3) ENG-31: Chivalry drains at twice the wiki's rate (24
against 12): correct the drain effect to the wiki's table and check every other prayer's
drain effect against the same table while you are there, listing each difference with the
wiki line. Restore the held conformance row. Measure before and after: a 20-tick hold, 20
one-tick flicks, two prayers held, a prayer lit over another, Chivalry for 60 ticks, a
death with the counter part full.

Evidence. `CONTENT_BUGS.md` ENG-10, ENG-11, ENG-31, ENG-39; `fix.prayer_regen_and_drain.json`
of seam pass 3.

## content: inferno_jad_wave_spawn

Units: inferno_single_jad, inferno_triple_jad, inferno_zuk_sets_and_healers.

Files: `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/scripts/inferno_pillars.rs2`

Summary. ENG-20 (and ENG-21, the same shape): wave 67 never spawns its Jad because
`~inferno_pillars_collapse_all` aborts at `inferno_pillars.rs2:150` (a nested
`npc_findnext` / `npc_huntall` inside an open find). The repeating wave timer used to
retry it and hide the fault; with the timer fixed the wave is empty. Rewrite the collapse
so it does not nest finds (collect, then act), keeping what it does to the pillars exactly
as the content intends. Source for what should happen at wave 67: the pinned wiki Inferno
page and the wave table in `docs/minigames/inferno/SOURCES.md` (the pillars are gone from
the Jad waves on: quote the sentence). Prove by entering at 66, clearing it with real
attacks, and showing wave 67 spawn one Jad; then enter at 67 and 68 directly and show the
Jad counts 1 and 3.

Evidence. `CONTENT_BUGS.md` ENG-20, ENG-21; `fix.inferno_wave_respawn.json` of seam pass 3.

## engine: retaliate_no

Units: inferno_nibblers_and_pillars; every npc whose record says it does not retaliate.

Files: `src/torirsserver/torirs_server_scripts.c`, `src/torirsserver/torirs_server_world.c`, `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/configs/inferno.npc`

Summary. ENG-25, ENG-26, ENG-27 (ENG-4's first item, not fixed in seam pass 3 because the
files were outside that seam): the default retaliation ignores an npc record's
`retaliate=no`, so a nibbler attacked by the player turns on the player. Sources: the
cache field itself, and the pinned wiki Jal-Nib page (line 50 as seam pass 3 cites it:
nibblers attack the pillars and ignore the player while a pillar stands; quote it and what
it says happens once the pillars are gone). Read seam pass 3's notebook for where the
default retaliation is decided (`fix.shared_combat_rules.progress.md`). Fix the general
rule in the engine files, set the nibbler's record as the wiki and the cache say, and keep
the tick log's hooks as they are. C is proved with a PRIVATE client (`PLATFORM_OBJ_BASE`,
`PLATFORM_TARGET`, `QUEST_BINARY`); the closer builds the shared one. Prove: wave 1,
attack a nibbler, and show from `t.npc.pack` and the tick log that it keeps its pillar and
never swings at the player; an ordinary npc with no such field still retaliates; two green
quests that fight still pass.

Evidence. `CONTENT_BUGS.md` ENG-4, ENG-25, ENG-26, ENG-27; `fix.shared_combat_rules.json`.
