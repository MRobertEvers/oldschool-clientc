# Raid loop: resume here

For a FRESH session taking over the raid orchestrator (a long session's chat panel crashes the
editor: hand over every few hours; everything below is on disk). Worktree
`build/orchestrator/worktrees/raid`, branch `matthew-mbp-m4-raid-b1` in both repos, pushed.
The raid loop never touches the quest loop or v3. Read, in this order: this file,
`README.md`, the last section of `SEAM_LEDGER.md`, the last section of `ROOM_LEDGER.md`,
`MERGE_CHECKLIST.md`.

## How to run a pass

Cards under `tools/raid_gate/workflows/` (Workflow tool, `scriptPath`): `raid_seam` (args
`pass`, `reuse_triage`, `width`, `context`), `raid_author` (args `pass`, `raid`, `rooms`,
`mode`, `width`, `context`), `raid_spec`, `raid_rig`. State is under `build/<kind>_state/<pass>/`;
relaunch with the SAME pass name to resume, never `resumeFromRunId`. One pass at a time in the
worktree, `width` 1 (owner: one worker at a time), every command's output under about 4 KB.
Before a room launch run `python3 tools/raid_gate/pass_state.py build/author_state/<pass>` and
check it; a room to re-author must have its `<id>.author.json` / `.review.json` moved aside.

## State on 2026-10-04, after room launch 13

- Mode tested: Theatre of Blood, ENTRY, SOLO only. Normal, Hard and party play are untested.
- Seam15 LANDED (parent 53a8aa8e0, OSRS-Content 946c357a71): the super restore no longer heals
  Hitpoints, Verzik's tornado goes with her, `::give` takes the exact symbol, the Entry chest
  keeps its leftover bandages. Gates at the baseline (quest suite 115 green + deserttreasure,
  forgettabletale, regicide, troll RED; conformance 324/324; C selftest 11 baseline failures).
- ALL SIX Entry solo rooms KEPT (pass `matthew-mbp-m4-raid-b1-rooms-tob`, launch 13, ledger
  71dcbbcb8): tob_maiden f6d2c42e5 (63 rows), tob_bloat e96af7766 (55), tob_xarpus f417148e4
  (63), tob_sotetseg 812267e7f (83), tob_nylocas d87427d41 (76), tob_verzik d1b4ccf00 (144,
  deathless, every tornado row before her death). Coverage FULL on each.
- Seam16 LANDED (parent 2be1a3db5, ledger 0af2cc300, content a098da317e): Maiden's blackstorm
  always lands and is halved by prayer (sourced; my triage's premise was wrong, no change), Bloat's
  stomp needs line of sight and a hand tile rolled twice lands once, Verzik's P3 attacks roll
  accuracy from the cache record. Relay with the wiki's kit: 0 of 5 names survive (three die in
  Xarpus P2 to 51-63 acid splats, one at Vasilias, one in the Nylocas waves); tob_entry NOT
  authored. Rooms to re-author after seam18: tob_maiden (two new spec rows to measure), tob_bloat
  (hand_tiles row now 14-16 range), tob_verzik (256/14, dies in P3 without food); kept and green:
  tob_xarpus, tob_sotetseg, tob_nylocas. Their author/review state is moved aside as
  `*.launch13_seam16.*.bak`.
- Seam18 LANDED (parent 1746816fb, ledger 09ea62f64, content 6c7a65615a): the Saradomin brew
  raises Defence 20%+2 of base (owner confirmed; conformance 99 -> 120); br_potion.rs2's ToA
  supply brew still drains (CONTENT_BUGS, open); the relay recipe re-measured: 0 of 5 names
  survive, tob_entry NOT authored (the Nylocas uses up the supplies; next unproved lever is
  Ancient Magicks there). Quest suite at the baseline. ROOMS NOW MOVED: tob_maiden (two spec rows
  unmeasured), tob_bloat (hand_tiles range), tob_nylocas 91/24 (18 brew doses by tick 462,
  Vasilias never spawns), tob_verzik 256/14 (P3, no food); tob_xarpus, tob_sotetseg green.
- Seam17 MERGED into the raid branch at 41a93c0fc (three driven clients in one world; verified:
  _party_smoke green, cooks_assistant/druid byte-identical, conformance 341/341 = 177 verbs +
  164 seams). The raid17 worktree is removed; branch matthew-mbp-m4-raid-b1-seam17 stays pushed.
- RUNNING: seam pass `matthew-mbp-m4-raid-b1-seam19` (triage `SEAM_TRIAGE_2026-10-04g.md`):
  `::tobjoin` puts a member in the leader's room instance, `t.raid.enter` for a party, the
  member HUD orb bug, a Hard fixture with a Normal completion, the author card's `party` arg,
  `_party_smoke` at Bloat Normal. One pass at a time again (the parallel allowance was seam17 only).
- Joined relay scratch: `build/seam_state/matthew-mbp-m4-raid-b1-seam15/trj/joined.lua`; run
  names seed on their first 12 characters, case folded (jbase37).

## Next, in order

1. Seam19 lands. Then room launch 14 re-authors the four Entry rooms seam16 and seam18 moved (tob_maiden, tob_bloat, tob_nylocas, tob_verzik) (same room pass, move each room's
   `<id>.author.json` / `.review.json` aside, note it in sample.json's orchestrator_notes).
2. `tob_entry` from the joined relay if the five-name verdict allows, graded on
   `encounters/raidwide.tsv` (42 of its 75 rows have a raidwide.<id> step in the green ledger;
   raid_coverage.py grades only spec.<id> steps, so the author names its rows spec.raidwide.*
   or the checker learns raidwide.*); otherwise seam17 on the room the verdict names.
3. (running, see above) Seam19 on `SEAM_TRIAGE_2026-10-04g.md`: `::tobjoin` puts a member in
   the leader's room instance, `t.raid.enter` for a party, the member HUD orb bug, a Hard fixture
   with a Normal completion, the author card's `party` arg, `_party_smoke` at Bloat Normal.
4. Normal mode room pass with three players (`tob_<room>_normal`, pass `matthew-mbp-m4-raid-b1-rooms-tob-normal`, roles from the trio transcripts and Blert), then Hard (Hard needs a Normal completion on
   every account: tob_party.rs2 `%varp6826_tob_completions < 1` at the door).
5. The frame-count pass for D/E tick rows (`tools/raid_gate/frame_diff.py`).
6. Tombs of Amascut spec pass (corpus pinned), then Chambers of Xeric.

## Waiting on the owner

- The nine stray files a seam1 fixer wrote into the main checkout (restore was denied).
- The raid content worktree's old dirt under `selftest/quests/quest_cook/play` and
  `quest_druid/play` (restore was refused; never staged).
- Whether `docs/WAVES_ORCHESTRATOR.md` belongs on v3.

## Relaunching seam18 if it is not closed

If `build/seam_state/matthew-mbp-m4-raid-b1-seam18/close.json` does not exist, relaunch
`tools/raid_gate/workflows/raid_seam.workflow.js` with args `pass` `matthew-mbp-m4-raid-b1-seam18`,
`reuse_triage` `docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04f.md`, `width` 1, and a `context`
that says: relaunched by a fresh session (a seam with fix.<key>.json is DONE and its edits are
uncommitted in the tree; one with only a progress notebook resumes from it); only Entry solo is
tested; the six room tests are read-only; seam17 runs in worktrees/raid17 at the same time, never
touch it; every fixer reads its triage section and the RULES; the brew seam first, the relay seam
after it; the owner confirmed the brew fix on 2026-10-04; the editor rules (4 KB outputs, 8 KB
commands, no recursive grep over OSRS-Content, shell fallback when a hook times out); and the
closer's duties: re-run the six rooms (the brew fix is expected to move rooms: 'tob_<room> must be
re-authored: <what moved>', never revert a sourced fix), the quest suite (baseline 115 green +
deserttreasure, regicide, troll, forgettabletale RED; a quest red from brews is listed under 'The
Saradomin brew raises Defence' in SEAM_LEDGER.md and MERGE_CHECKLIST.md, not reverted), the
relay's five-name table and the brew-only run into SEAM_LEDGER.md as the tob_entry verdict (4 of
5), DRIVER_NOTES, spec rows, CONTENT_BUGS, conformance, commit by explicit path submodule first,
push the raid branch only.

## Relaunching seam17 if it is not closed

If `build/orchestrator/worktrees/raid17/build/seam_state/matthew-mbp-m4-raid-b1-seam17/close.json`
does not exist, relaunch `build/orchestrator/worktrees/raid17/tools/raid_gate/workflows/raid_seam.workflow.js`
with args `pass` `matthew-mbp-m4-raid-b1-seam17`, `branch` `matthew-mbp-m4-raid-b1-seam17`,
`worktree` `/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid17`,
`reuse_triage` `docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-04e.md`, `width` 1, and a `context`
that says: it runs ONLY in that worktree (seam16 runs in worktrees/raid at the same time; never
touch it or the main checkout); builds in its own src/build_questtest; no content edits; the
fixers in the triage's order; the editor rules (4 KB outputs, 8 KB commands, no recursive grep
over OSRS-Content, shell fallback when a hook times out); CLAUDE.md's assert/no-switch rules;
and the closer's duties: one-client quest runs byte-identical (cooks_assistant, druid against a
throwaway worktree at da183511b), the six rooms still pass, the three-client smoke run's three
tick logs agree on the server tick, check-drive-abi/check-pt-switch/test-plugin-lua, DRIVER_NOTES
'Three raiders in one run', README knobs, MERGE_CHECKLIST section 3, SEAM_LEDGER line, push the
seam17 branch only.
