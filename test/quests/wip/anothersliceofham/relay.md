## Guide 1.1-1.5: the route into Dorgesh-Kaan is clickable end to end (seam matthew-mbp-m4-b55-seam1 anothersliceofham_route_into_dorgeshkaan)

- **No content was broken. The test skipped the route, and the setup lacked The Lost Tribe.** The round-1/2
  file set up `::complete quest_losttribe`, then teleported past the route with `goto-enterCity`
  (3317,9601) and `goto-talkToUrtag` (2729,5365,1). The second goto lands INSIDE Ur-tag's council room, past
  its door. Everything the guide names is driven by The Lost Tribe's content: the hole is
  `lost_tribe_cellar_wall`'s multiloc on `varb532_lost_tribe_quest` (all.loc, values 4..12 =
  `lost_tribe_cavewall_hole_walldecor`, bound at `quest_losttribe/scripts/losttribe.rs2:245`). Kazgar is
  `lost_tribe_guide`'s multinpc (values 9..11 `_2ops`, 12 `_3ops`, bound at
  `losttribe_finish.rs2:102`). The city door refuses below `^lt_complete` (`lotg_intro.rs2:67`).
  helper_coverage's "no trigger on lost_tribe_cellar_wall" only meant that no row was named
  `climbThroughHole`. The trigger sits on the multiloc child.
- **Setup MUST keep `::complete quest_losttribe`, before `quest_deathtothedorgeshuun`.** Quest Helper lists
  only DTTD/Giant Dwarf/Dig Site, and `::complete` writes only the named quest's own var. Without the Lost
  Tribe line, varb532=0: the hole, Kazgar and the city door are all absent from the client
  (`build/quest_gate/b55s1_route_noLT`: `climbThroughHole` FAIL `no loc 6905
  (lost_tribe_cavewall_hole_walldecor) in the client's entity pool`). The scaffold now emits that line:
  `tools/quest_gate/new_quest.py` has `ROUTE_PREREQS`.
- **The click sequence.** Copy it as written. It was measured 30/0 in
  `build/quest_gate/b55s1_slice_copy`, a copy of the committed file with only these rows changed
  (script `build/seam_state/matthew-mbp-m4-b55-seam1/scratch/b55s1_slice_copy.lua`). Replace the committed
  `goto-enterCity` .. `enterCity.here` block with steps 3-5 and the `goto-talkToUrtag` row with step 7:
  1. `goto-goDownIntoBasement` `goto_tile 3209,3216,0`. This is plain travel to the trapdoor in the castle
     kitchen.
  2. `goDownIntoBasement`: `click_loc "qip_cook_trapdoor_open" 1`. Lands at 3210,9616,0.
  3. `climbThroughHole` (guide 1.4): `click_loc "lost_tribe_cavewall_hole_walldecor" 1`. The click_loc
     walks across the cellar to the hole and resolves `-> lost_tribe_cellar_wall (base)`; "You squeeze
     through the hole." Then `t.ticks(3)`, and the tile reads 3221,9618,0.
  4. `talkToKazgar` (guide 1.3): `talk_to "lost_tribe_guide" 1`. The base symbol was proved at varb532=11
     (`_2ops`, b55s1_kazgar_base11) and at 12 (`_3ops`, b55s1_kazgar_base). `"lost_tribe_guide_2ops"` also
     works today. Then `chat.play { "npc:Hello friend", "options", "choose:Can you show me the way to the
     mines?", "player:Can you show me the way to the mines?", "npc:Certainly" }` and `t.ticks(4)`. The
     player lands beside Mistag at 3319,9615,0 (shot 014).
  5. `enterCity` (guide 1.2): `click_loc "cave_goblin_city_doorr" 1`. No goto is needed: the door is in
     view from Mistag's tile. "You head through into the city of Dorgesh-Kaan." Then `t.ticks(4)`, and the
     tile reads 2704,5365,0.
  6. `labDoor` `click_loc "dorgesh_inner_door_closed" 1`, then `climbToF1City` `click_loc
     "dorgesh_1stairs_posh" 1`. The tile reads 2720,5361,1. These are unchanged.
  7. `urtagDoor`: `click_loc "dorgesh_inner_door_posh_closed" 1 { at = { 2733, 5363 } }`, then
     `t.ticks(2)`. Ur-tag (2730,5365,1) stands in a walled room whose only entrance is this door
     (`maps/m42_83.jl2`; a generic `next_loc_stage` door, `doors/configs/doors.loc:1420`). Without it,
     `talk_to dorgesh_urtaq` answers `I can't reach that!` (b55s1_route_a row 15).
  8. `talkToUrtag` `talk_to "dorgesh_urtaq" 1` and the committed 11-entry `chat.play`. Then
     `quest.stage.urtag_done` = 1.
- **helper_coverage on that copy:** climbThroughHole moved from CONTENT_GAP to DRIVEN, and talkToKazgar is
  now DRIVEN by its own row. The copy stops next at `dig1.click`: the artefact hotspot menu shows only
  Examine. That block belongs to the stages_0_to_4 seam.
- **Open, not fixed here (content, `quest_deathtothedorgeshuun/scripts/dttd_shared.rs2:65`):**
  `dttd_quest_complete` writes `varb532_lost_tribe_quest = 13`, but the cache maps 13 to -1 for Kazgar,
  Mistag and the cellar wall (multinpc14/multiloc14). So after a REAL completion of Death to the Dorgeshuun,
  the hole, Kazgar and Mistag vanish (`build/quest_gate/b55s1_route_dttd13`: climbThroughHole `no loc
  6905`). The value the content's own comment means ("native cache multinpc13", the `_3ops`
  Talk-to/Mines/Watermill Kazgar) is **12**. The wiki agrees: Kazgar, revid 15196282, lists "Talk-to,
  Mines, Watermill" after Death to the Dorgeshuun. At 12 the whole route above passes 18/0
  (`b55s1_route_dttd12`). The test does not hit this, because `::complete` leaves varb532 at 11. If the
  DTTD arm of `quest_cheat.rs2` is later made to write 532=12 as well, the rows above still pass unchanged.

## Guide 1.6-1.13 and stages 2-5: digs, cleaning, Tegdak, railway exit, Scribe, Oldak, generals (seam matthew-mbp-m4-b55-seam1 anothersliceofham_stages_0_to_4_drivable)

- **What changed in the content (OSRS-Content f97d2dcd59).**
  - `slice_tegdak.rs2`: the six artefacts are dug by USING THE TROWEL on them, `[oplocu,slice_artifact_hotspot_0N]`
    (the cache gives the Artefact/Hole locs and the specimen table no op at all; quick guide oldid 14458352
    #Excavation "Dig up artefacts from the ground with the trowel", "Use artefacts on the specimen table";
    Quest Helper dig1..6 are trowel-highlighted ObjectSteps). The dead `[oploc1,...]` bindings and the
    click-the-table-cleans-everything `[oploc1,slice_table_01]` are gone.
  - Hotspot N now gives `slice_artifact_N_dirty` and writes `varb355(0+N)_slice_artifact_N` (the hotspot's own
    cache multivarbit). Before, hotspot_02 wrote artifact_5's varbit (so hotspot_05 turned into a Hole), 04 wrote
    2 and 05 wrote 4.
  - Using anything but the trowel on an artefact: mesbox "You need a trowel to excavate this site."; a dug site:
    mes "You've already excavated this site."
  - `slice_generals.rs2`: same talk on either general; the line before the move into the tower instance is now
    "The goblins scatter into the buildings, and you take cover behind a hut." The move itself stays (the
    H.A.M.-bush is an instance: article oldid 15292360, transcript oldid 15263379 "The goblin generals").
- **Measured:** `build/quest_gate/b55s1_slice_b` 146/0 to the scroll = the route section above + the rows
  below + the parity fight tail (script `build/seam_state/matthew-mbp-m4-b55-seam1/scratch/b55s1_slice_b.lua`).
  `helper_coverage.py anothersliceofham --lua <that> --ledger <its ledger>` reads **VERDICT FULL** (DRIVEN=28 TRAVEL=1).
  `lint_quest` is clean. `gate.py` has one finding, inherited from the parity tail: `special.toggle` has an empty detail.
  Write the toggle's reading into that row (for example `t.check("special.toggle", t.ui.invoke(w, 0) == "ok" and "ok", "armed varp301=...")`).
- **The rows, after `quest.stage.artefacts` / `trowel.have` / `brush.have` (copy them, then rename to taste):**
  1. Tegdak's first talk (`talkToTegdak`, from 2512,5562,0): no options, three pages:
     `npc:Ah, you must be the one Ur-tag sent!`, `player:How do I go about that?`, `npc:Take this trowel`.
     The trowel and the brush land in the pack, then stage 2. Use `chat.play`, not `chat.drain`, because drain's
     detail is the bare word `none`.
  2. `dig1`..`dig6`: for each one, `t.player.walk_to(x, z, 30)`, then
     `t.exec("digN", t.player.use_on, "trowel", t.player.by_symbol("loc", "slice_artifact_hotspot_0N"))`,
     then `t.inv.await("slice_artifact_N_dirty", 1, 5)` and `t.var.await("varb355<N>_slice_artifact_N", 1, 5)`.
     The varbits are varb3551..varb3556. Walk tiles and hotspots:
     dig1 2513,5561 -> hotspot_01 (2513,5563); dig2 2512,5559 -> 02 (2511,5561); dig3 2512,5551 -> 03 (2513,5550);
     dig4 2512,5548 -> 04 (2511,5547); dig5 2512,5545 -> 05 (2512,5544); dig6 2512,5540 -> 06 (2513,5539).
     Message: "You carefully excavate the site with your trowel, and unearth a dirt-caked artefact."
     A dug site shows a Hole.
  3. `cleanArtefacts`: walk_to 2512,5558. For n = 1..6, `t.player.use_on("slice_artifact_n_dirty",
     t.player.by_symbol("loc", "slice_table_01"))`, then `t.inv.await("slice_artifact_n_clean", 1, 5)`.
     Message: "You carefully brush the dirt from the artefact, revealing the detail beneath."
     The specimen brush must be in the pack.
  4. `showTegdakArtefacts`: `talk_to "slice_goblin_archaeologist" 1`. The six "You hand Tegdak the ..." lines are
     chat messages, not pages. Then two pages, `npc:Remarkable -- all six pieces` and
     `npc:This is well beyond my expertise`, then "Zanik joins you." Stage 3 (`scribe`) follows, with
     `varb3557_slice_zanik_at_dig` = 0 and `slice_zanik_follower` present within 6 tiles.
  5. **talkToZanikRailway is NOT a talk.** Zanik follows automatically after the sixth hand-in (quick guide 14458352
     "Zanik will automatically follow you after"; transcript 15263379 "Zanik starts following the player").
     Quest Helper shows that step only while she is not following, so it is the recovery fallback. Prove it with a
     row such as `zanikFollowing` (`t.npc.await_present("slice_zanik_follower", 6, 5)`). The grader's present
     DRIVEN for it only rides on a row whose name contains "zanik".
  6. `leaveRailway`: `goto_tile 2521,5605,0`, the tunnel's north end and plain travel. Then
     `click_loc "slice_underground_wall_exit_goblin" 1`. "You step through the doorway, back toward Dorgesh-Kaan."
     The player lands at 2695,5277,1.
  7. `talkToScribe`: `goto_tile 2714,5369,1`, then `talk_to "dorgesh_male_scribe" 1`. Pages:
     `player:We found this mace during the dig`, `npc:Our written language`, `npc:Could anyone read it?` (Zanik),
     `npc:Perhaps the surface goblins would`. Stage 4 (`oldak`) follows. The Scribe answers this only while
     varb3557 = 0 (Zanik following).
  8. `goDownToF0City`: `goto_tile 2720,5362,1`, then `click_loc "dorgesh_1stairs_posh_top" 1`. The player lands at
     2720,5358,0.
  9. `talkToOldak`: `goto_tile 2705,5363,0`, then `talk_to "dorgesh_oldak_there" 1`. That is the talkable Oldak
     the login hook spawns at 2704,5365,0. Quest Helper's `LOTG_OLDAK_CUTSCENE` is the same npc id that the old
     helper called `NpcID.OLDAK`, renamed later. The type has no op, and Land of the Goblins' helper uses
     `DORGESH_OLDAK_THERE` for him. The grader reads it DRIVEN by your `talkToOldak` row. Pages:
     `player:The Scribe sent us`, `npc:The Goblin Village?`, `npc:Oldak's calculations aren't always exact`.
     Two chat messages follow, then the player is teleported to 2957,3512,0 and stage 5 (`generals`) is set.
  10. `talkToGenerals` (no goto, you are beside them): `talk_to "general_wartface_green" 1`. Pages:
      `player:Our Scribe couldn't read`, `npc:By the ancestors`, `npc:It cannot be.` (Bentnoze),
      `player:A Chosen Commander?`, `npc:The Zanik who carries`, `npc:H.A.M.! They must have followed`
      (Bentnoze), `npc:Head for the tower to the south`. Stage 6 (`ham_rangers`) follows, and the player is moved
      to 2443,5421,0 in the tower instance (map m38_84). Continue with the parity tail's `goUpLadder`:
      `click_loc "slice_goblin_ladder_bottom" 1` -> 2447,5417,2.
- **Generals at stages 5 and 7 DO advance the quest:** b55s1_slice_b `quest.stage.ham_rangers` after
  talkToGenerals, and `quest.stage.sergeant` after talkToGeneralsAgain. The old "never reads
  %varb3550_slice_quest" was a grader artefact. helper_coverage's `script_body` stops at the next `[` line,
  so the first of two stacked trigger headers had an empty body. The headers are split now.
- **Inventory:** the committed setup (14 slots) plus the trowel, the brush and six artefacts fits in 28.
  Do not add setup items without counting.

## Guide killHamMageAndArcher -> talkToGeneralsAgain: the content moves you to the generals now (seam matthew-mbp-m4-b55-seam2 anothersliceofham_rangers_return_never_fires)

- **What was broken (content).** The return after the two H.A.M. rangers ran INSIDE the dying npc's
  `[ai_queue3,...]`. Its `~mesbox` parked that npc script on the player, the stage write landed, but the
  `p_teleport` never moved him off the tower (round 6: `goto-generals2` detail `from 2447,5416,2`), and
  `~npc_default_death` resumed with no active npc (`npc_findhero with no active npc ... slice_hammage.rs2:108`).
- **Fixed in content (OSRS-Content 2ca4e77a52):** `slice_hammage.rs2` -- each death handler does
  only the npc's half (dead flag, "You have defeated the H.A.M. ..." line, `~npc_default_death`) and, once
  both flags are set at stage 6, `queue(slice_ham_rangers_return)`. The queued PLAYER script shows the mesbox,
  `p_teleport(^slice_generals_coord)` (2957,3512,0) and writes stage 7. `slice_ham_combat_login` queues the same
  return when both rangers are dead at stage 6 (a logout during the mesbox). Same shape fixed for Sigmund
  (`slice_sigmund.rs2`): the death handler writes the flag and stage 10 and queues his parting line, which
  now actually shows (round 6's `defeatSigmund.chat` read `none`).
- **Why a teleport and not a climb down (source).** In the game the second kill starts the kidnap cutscene in
  the village and it ends with you at the generals: wiki Another_Slice_of_H.A.M. oldid 15292360 walkthrough
  ("When you have done this, there will be another short cutscene where Zanik is kidnapped by Sigmund. The
  goblin generals will tell you..."), Transcript oldid 15263379 "Upon defeating the two H.A.M. members"
  (Sigmund, Zanik and both generals in one scene); Quest Helper AnotherSliceOfHam.java (master 4f8bb22ff2)
  goes from `killHamMageAndArcher` (2447,5417,2) straight to `talkToGeneralsAgain` at WorldPoint(2957,3512,0),
  with no travel step. This port has no kidnap cutscene, so the mesbox narrates it and the teleport is the
  scene's end move. The quick guide (oldid 14458352) says nothing about the way back.
- **The tower's own ladder down (OPEN, not this seam).** The cache map does place `slice_goblin_ladder_top`
  (Climb-down) at 2442,5417,2 (`maps/m38_84.jl2:1753`), but the port lands you at 2447,5417,2 on the far side
  of the cover crates: `click_loc "slice_goblin_ladder_top" 1` answers `reach_failed: I can't reach that!`
  (`build/quest_gate/b55s2_slice_ladder`, shot 122). The game lands you at the ladder with the crates as cover.
  That is a parity leg (landing tile, Climb-down, Hide-behind) for the content queue. It does not block the
  guide: no guide step climbs down.
- **What the round-7 author changes** (proved 148/0 in `build/quest_gate/b55s2_slice_a`, mage first, and
  `build/quest_gate/b55s2_slice_b`, archer first; scripts under
  `build/seam_state/matthew-mbp-m4-b55-seam2/scratch/`):
  1. Drop `goto-generals2` (round6_rejected.lua:201). The content moves you.
  2. Replace `killHam-box`'s `chat.drain` (detail `none`) with
     `t.exec("killHam-box", t.chat.play, { "mesbox:With both ambushers down" })`, then `t.ticks(2)`, then
     `quest.stage.generals_again`, then the tile row:
     `do local tr, th = t.world.tile(); t.check("killHam.returned", tr == "ok" and th.level == 0 and math.abs(th.x - 2957) <= 2 and math.abs(th.z - 3512) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2957,3512,0, no goto)") or tostring(th)) end`
     Measured: `killHam-box` `1 page(s): mesbox:With both ambushers down, you`, `killHam.returned`
     `2957,3512,0`, then `talkToGeneralsAgain` with no goto, in both kill orders.
  3. Replace `defeatSigmund.chat`'s `chat.drain` with `t.exec("defeatSigmund.chat", t.chat.play, { "npc:Someday, somehow" })`.
     Measured `1 page(s): npc:Someday, somehow, I will have`.
  4. The three pure reads the sampler named cannot fail; make each a comparison:
     - `zanik.at_dig` (round6_rejected.lua:144, after the sixth hand-in): `t.check("zanik.at_dig",
       t.var.expect("varb3557_slice_zanik_at_dig", 0))` -- 0 = following (`^slice_zanik_following`), 1 = waiting
       at the dig; b55s2_slice_a read 0 there.
     - `special.armed`: `t.check("special.armed", t.var.expect("varp301_sa_attack", 1))`.
     - `special.energy`: read it into a local and compare: `local _, e = t.var.varp("varp300_sa_energy");
       t.check("special.energy", (e or 0) >= 1000, "special energy " .. tostring(e) .. " (the ancient mace special costs 100%)")`.
  5. Optional, proves the logout path: after both kills, `t.check("relog.during_box", t.session.relog())` while the
     mesbox is up; the login queues the return again (`build/quest_gate/b55s2_slice_c` 109/0: logged out from
     2447,5416,2 at stage 6, mesbox re-shown, stage 7, 2957,3512,0).
  6. Lint (closer, seam2): `lint_quest.py` (rule 1450bf4f2, after round 6) refuses the committed
     `anothersliceofham.lua` lines 47, 52, 56, 59, 81 (`t.check(..., t.world.tile(...))` cannot fail) and
     round4_rejected.lua's twins. Every tile row the round-7 file carries must compare `h.x`/`h.z`/`h.level`
     the way `killHam.returned` above does. Closer re-run on the committed pack: `b55s2_close_b` (archer first)
     148/0, same `killHam-box` / `killHam.returned 2957,3512,0` / `defeatSigmund.chat` rows.
