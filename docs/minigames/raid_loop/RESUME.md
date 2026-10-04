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
- Seam19 LANDED (parent 15c2ace2b, ledger 92e770eec, content 007a1c7c9b): `::tobjoinroom [mode]`
  puts a member in the leader's `::tobmode` instance (`::tobjoin` was taken by the C selftest);
  `::tobstate` ends ` party=N scale=K`; every raider's party orbs are current; `t.raid.enter`
  works for a party; fixture `tob_normal_done.ini` (a Normal completion, for the Hard door);
  raid_coverage.py party scope; the author card takes `party`; `_party_smoke` phase C at Bloat
  Normal reads hp_3 1500 = 2000 x 750/1000. Conformance 344/344. Open (CONTENT_BUGS seam19): the
  fight watchdog runs only for the barrier-crosser; room music reaches only the builder. Three
  clients are NOT tick-deterministic run to run (a member's typed command lands at its own
  frame-timed boundary; one run in three shifted by a tick).
- Room launch 14 (pass `matthew-mbp-m4-raid-b1-rooms-tob`, Entry solo; ledger 4fb49229b): KEPT
  tob_maiden 1dffd5230 (65/65 FULL, 33 of 33 blackstorms landed, prayed hits match 36.5+3.5c),
  tob_bloat abc53c18e (55/55, hand tiles 14-16 as Blert), tob_nylocas 116a1d938 (76/76, 41 brew
  doses + 11 restores, Vasilias killed); tob_xarpus f417148e4 and tob_sotetseg 812267e7f kept.
  tob_verzik: the retry (725620c00) passed its reviewer but the SAMPLER SENT IT BACK and reverted
  it (41f0cd7e2; ledger e11c5856d): its p3_auto_miss_entry row counted five prayed magic autos as
  unprayed (the test's pm/pg labels are swapped; Protect from Magic stayed on through tick 576),
  leaving 7 unprayed ranged autos under the row's minimum of 12; fix: decide unprayed per hit
  from t.prayer.read() on the landing tick for that hit's style (DRIVER_NOTES "Unprayed is the
  prayer you read"). RUNNING: the second retry (same args). The other five rooms are green.
- Joined relay scratch: `build/seam_state/matthew-mbp-m4-raid-b1-seam15/trj/joined.lua`; run
  names seed on their first 12 characters, case folded (jbase37).

## Next, in order

1. The tob_verzik retry lands (above). Then the NORMAL THREE-PLAYER ROOM PASS: card raid_author, pass
   `matthew-mbp-m4-raid-b1-rooms-tob-normal`, raid tob, the six rooms, mode normal, party 3, width 1
   (ids tob_<room>_normal; sources: Strategies wiki per-room sections, the six trio transcripts,
   Blert guides and data; roles only from the sources) (same room pass, move each room's
   `<id>.author.json` / `.review.json` aside, note it in sample.json's orchestrator_notes).
2. `tob_entry` from the joined relay if the five-name verdict allows, graded on
   `encounters/raidwide.tsv` (42 of its 75 rows have a raidwide.<id> step in the green ledger;
   raid_coverage.py grades only spec.<id> steps, so the author names its rows spec.raidwide.*
   or the checker learns raidwide.*); otherwise seam17 on the room the verdict names.
3. (landed) Seam19 on `SEAM_TRIAGE_2026-10-04g.md`: `::tobjoin` puts a member in
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

## Relaunching a pass

A background pass dies with the session that launched it. Every card resumes from its state
directory when relaunched with the SAME args: `build/author_state/<pass>/` for `raid_author`,
`build/seam_state/<pass>/` for `raid_seam` (`close.json` present = closed). Never use
resumeFromRunId. The context string for a room launch is in the "RUNNING" line above and the
state directory's sample.json orchestrator_notes; for a seam pass it is the triage file's RULES
plus the closer's duties (rooms re-run and 'must be re-authored' lines, quest suite at its
baseline with any sourced red listed in SEAM_LEDGER.md and MERGE_CHECKLIST.md, conformance,
DRIVER_NOTES, CONTENT_BUGS, commit by explicit path submodule first, push the raid branch only).
