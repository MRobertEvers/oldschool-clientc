# Quest Helper coverage of the green tier 1 quest tests (2026-09-23)

This audit covers all 39 tier 1 rows marked green in `test/quests/QUEUE.tsv`. For each quest, the Quest Helper guide's `steps.put` ladder and its ConditionalStep leaves are compared with four things:
- the test's rows and setup cheats in `test/quests/<id>.lua`;
- its published ledger, now at `selftest/quests/quest_<id>/play/ledger.tsv`, moved from `selftest/quest_tests/<id>/` by OSRS-Content a91e740b15;
- the quest's `.rs2` under `OSRS-Content/osrs239-content/server/scripts/quests/<quest_dir>/`;
- for a teleport, the content that the teleport steps over.

Each guide step gets one class:
- **DRIVEN**: a test row does what the step says.
- **SKIP-CONTENT**: the port has no such leg. The `.rs2` line that advances past it is cited.
- **SKIP-CHEAT**: the content has the leg, but the test gets past it with a cheat.
- **SKIP-OTHER**: the content has the leg and the test leaves it out with no reason.

Counting rules:
- Items the guide lists as bring-alongs in `getItemRequirements()` and gives with `::give` are not skips.
- `::complete`/`::setvar` of a *prerequisite* quest is not a skip.
- Plain walking, stairs and ladders are merged into the step they lead to.
- A `goto_tile` (`::goto`) teleport that steps over a door, gate, passage or puzzle the guide lists, and that the content implements and gates, is SKIP-CHEAT.
- `guide steps` counts the guide's leaf action steps. For Mourning's End Part I and II it is the guide's own `getPanels()` list.

## Summary

Verdicts over the 39 rows:

| verdict | count |
|---|---|
| FULL | 11 |
| TEST GAP | 15 |
| CONTENT GAP | 6 |
| MIXED | 7 |

Totals: 644 guide steps, 443 driven, 140 missing from the content, 56 skipped by a cheat, 5 skipped with no reason. Mourning's End Part II alone accounts for 105 of the 140 content skips.

| quest | test | guide steps | driven | skip content | skip cheat | skip other | verdict | first gap |
|---|---|---|---|---|---|---|---|---|
| quest_atailoftwocats | atailoftwocats | 24 | 19 | 3 | 1 | 1 | MIXED | findBob (stage 15: operate the catspeak amulet (e)); the test talks to Unferth instead |
| quest_betweenarock | betweenarock | 23 | 22 | 1 | 0 | 0 | CONTENT GAP | useGoldBarOnDondakan (60) |
| quest_biohazard | biohazard | 28 | 24 | 0 | 4 | 0 | TEST GAP | enterBackyardOfHeadquarters (squeeze through mournerstewfence) bypassed by ::goto |
| quest_blackarmgang | blackarmgang | 14 | 9 | 3 | 2 | 0 | MIXED | startQuest (talk to Reldo / book / Reldo again: the port's tramp starts the Black Arm route with no Reldo gate) |
| quest_blackknight | blackknight | 8 | 4 | 0 | 4 | 0 | TEST GAP | enter fortress through the guarded door (bkfortressdoor1) wearing the bronze med helm + iron chainbody disguise |
| quest_cog | cog | 14 | 10 | 0 | 4 | 0 | TEST GAP | pushWall (push the secret wall in the tunnel to reach the blue cog's cell) |
| quest_cook | cooks_assistant | 9 | 1 | 0 | 8 | 0 | TEST GAP | getBucket (buy bucket at Lumbridge General Store) |
| quest_currentaffairs | currentaffairs | 20 | 16 | 4 | 0 | 0 | CONTENT GAP | doAudit (varp 25: answer Catherine's 8 audit questions matching form cr-4p) |
| quest_doric | doric | 2 | 2 | 0 | 0 | 0 | FULL | - |
| quest_druid | druid | 9 | 8 | 0 | 1 | 0 | TEST GAP | enterCauldronRoom (Prison door into the cauldron room, passed by ::goto teleport) |
| quest_drunkmonk | drunkmonk | 10 | 9 | 0 | 1 | 0 | TEST GAP | goUpLadder (climb the cellar ladder out of the blanket cave; test teleports with goto_tile) |
| quest_eadgar | eadgar | 27 | 24 | 0 | 2 | 1 | TEST GAP | get Berry's cell key + unlock Eadgar's cell (::setvar troll_freed_eadgar 1) |
| quest_elemental_workshop | elemental_workshop | 18 | 17 | 0 | 1 | 0 | TEST GAP | goDownStairs (climb down the spiral staircase behind the odd wall; test teleports with goto_tile/::goto) |
| quest_entertheabyss | entertheabyss | 7 | 7 | 0 | 0 | 0 | FULL | - |
| quest_fishingcompo | fishingcompo | 9 | 6 | 0 | 3 | 0 | TEST GAP | getGarlic (Seers' table garlic given by ::give) |
| quest_fluffs | fluffs | 9 | 9 | 0 | 0 | 0 | FULL | - |
| quest_golem | golem | 22 | 18 | 4 | 0 | 0 | CONTENT GAP | talkToElissa (no Elissa trigger in the pack) |
| quest_haunted | haunted | 15 | 10 | 0 | 5 | 0 | TEST GAP | pickupSpade (spade/compost/closet-key chain bypassed by ::goto into the closet) |
| quest_hero | hero | 22 | 18 | 1 | 3 | 0 | MIXED | reach deeper Taverley Dungeon (jailer/Velrak/dusty-key gate or pipe bypassed by ::goto to the lava spot) |
| quest_hetty | hetty | 4 | 3 | 0 | 1 | 0 | TEST GAP | killRat (rat's tail given by ::give) |
| quest_hunt | hunt | 15 | 14 | 0 | 1 | 0 | TEST GAP | travel to Karamja via Port Sarim seaman (replaced by ::goto teleport to Brimhaven) |
| quest_makinghistory | makinghistory | 12 | 12 | 0 | 0 | 0 | FULL | - |
| quest_misc | misc | 23 | 19 | 4 | 0 | 0 | CONTENT GAP | talkBrand1 (Brand courtship dialogue, varp 10 court ladder) |
| quest_mortton | mortton | 22 | 17 | 4 | 1 | 0 | MIXED | repairTemple (stage 50/55, ::mortton_repairtemple debugproc) |
| quest_mourningsendparti | mourningsendparti | 34 | 27 | 5 | 2 | 0 | MIXED | killMourner + pickUpLoot (fight a mourner in the Arandar pass; port gives the loot on one click, mend1_disguise.rs2:29-37) |
| quest_mourningsendpartii | mourningsendpartii | 110 | 5 | 105 | 0 | 0 | CONTENT GAP | stage 10-20 Mourner Caves/corpse/temple stairs/useChisel collapsed to mes() at mend2_shared.rs2:53-54; Temple of Light puzzles 1-6 + death altar collapsed to mes() at mend2_shared.rs2:97-111 |
| quest_murder | murder | 19 | 14 | 2 | 0 | 3 | MIXED | pick up the pungent pot (murderpot2) |
| quest_priest | priest | 7 | 7 | 0 | 0 | 0 | FULL | - |
| quest_prince | prince | 13 | 12 | 0 | 1 | 0 | TEST GAP | useKeyOnDoor (unlock prison door; ::goto into the cell) |
| quest_pryingtimes | pryingtimes | 10 | 8 | 2 | 0 | 0 | CONTENT GAP | deliverCargo (Port Sarim -> Pandemonium port task; content soft-skips it in dialogue) |
| quest_romeojuliet | romeojuliet | 7 | 7 | 0 | 0 | 0 | FULL | - |
| quest_rovingelves | rovingelves | 14 | 7 | 0 | 7 | 0 | TEST GAP | enterGlarialsTombstone (use Glarial's pebble on the tombstone; replaced by ::goto into the tomb) |
| quest_runemysteries | runemysteries | 6 | 6 | 0 | 0 | 0 | FULL | - |
| quest_scorpcatcher | scorpcatcher | 9 | 7 | 0 | 2 | 0 | TEST GAP | get dusty key + unlock deep Taverley gate (bypassed by ::goto) |
| quest_seaslug | seaslug | 18 | 18 | 0 | 0 | 0 | FULL | - |
| quest_sheep | sheep | 2 | 2 | 0 | 0 | 0 | FULL | - |
| quest_sheepherder | sheepherder | 12 | 12 | 0 | 0 | 0 | FULL | - |
| quest_squire | squire | 10 | 10 | 0 | 0 | 0 | FULL | - |
| quest_tearsofguthix | tearsofguthix | 7 | 3 | 2 | 2 | 0 | MIXED | enter Lumbridge Swamp caves (rope) / Juna's cave -- bypassed by ::tearsofguthix teleport |

## Why the gap was not caught

- **`docs/QUEST_AUTHORING.md` trap 16** and the reviewer card (`tools/quest_gate/author_batch.workflow.js`, review step 3) only ask whether the test cheats something the quest's **own .rs2** makes the player do. The sampler card asks the same thing. If the content pack left a leg out, there is nothing to cheat, so the review passes.
- No author, reviewer or sampler card compares the test against the guide's `steps.put` ladder. The guide is used only in these places:
  - the `new_quest.py` scaffold, which an author may throw away;
  - goto WorldPoints;
  - bring-along items (`canBeObtainedDuringQuest`);
  - the reward list.
- `gate.py` checks only the ledger's shape: that every row PASSes, the shots and the minimum row count.

## quest_mourningsendpartii: CONTENT GAP (worst case)

- The test is hand-written. Its header (lines 3-17) says it threw away the `new_quest.py` scaffold, which had 161 steps from `MourningsEndPartII.java`, because "this content pack does not implement that maze".
- The content has **no `[oploc*]` trigger anywhere** in `quest_mourningsendpartii/scripts/`. The whole quest is `mend2_shared.rs2`: five Arianwyn branches and one Essyllt branch.
- The collapse was chosen deliberately in `configs/mend2.constant:59-72` and `:115-146`, citing the content queue's rule that a puzzle with no precedent is collapsed to one deterministic action.

The guide's panels list 110 steps. The test drives 5 of them:
- `talkToArianwyn` (stage 0)
- `talkToEssyllt` (5)
- `bringCrystalToArianwyn` (20)
- `talkToArianwynAfterGivingCrystal` (40, as Arianwyn #3)
- `returnToArianwyn` (50)

The other 105 are SKIP-CONTENT:

1. **Stages 10/15/20, the `getCrystal` leg.** Covers `enterMournerHQ`, `enterMournerBasement`, `enterCave` (New Key), `searchCorpse`, `goUpStairsTemple`, `goUpSouthLadder`, `goToMiddleFromSouth`, `goUpFromMiddleToNorth` and `useChisel` on the dark crystal.
   - In the port, Essyllt sets `^mend2_essyllt_task` at `mend2_shared.rs2:142`.
   - The next Arianwyn talk narrates the whole leg with `mes()` at `:53-54` ("You make your way back through the Mourner Caves... You chisel a sample"), `inv_add`s the journal and sample at `:55-60`, and writes `^mend2_crystal_given` at `:72`.
   - The test only goto's Essyllt's tile, then Arianwyn's.
2. **Stage 30, `talkToElunedAfterGivingCrystal`.** Folded into Arianwyn's line at `:70`.
3. **Stage 40, `doAllPuzzles` (the Temple of Light).** Covers:
   - puzzles 1-6: 6 `pullDispenser`, 60 pillar/mirror steps, `climbWallSupport` ×2, `useRope`/`goDownRope`/`climbUpRope`, 5 chest searches;
   - `deathAltarPuzzle`: `turnKeyMirror` and `enterDeathAltarBarrier`;
   - `addCrystal`: `getDeathTalisman`, `enterDeathAltar`, `useCrystalOnAltar`, `leaveDeathAltar`, `turnPillarFromTemple`, `useCrystalOnCrystal`.

   In the port, all of this is five `mes()` lines at `mend2_shared.rs2:97-104`, behind item gates at `:76-96`. **`%mourning_quest_main = ^mend2_puzzle_done` is written at `mend2_shared.rs2:111`**, one Arianwyn click after `^mend2_crystal_given` (`:72`).

The test's Arianwyn #4 ("it's done", 40 -> 50) is a step the port invented; the guide has none.

The test uses no cheat to skip any leg: `::mend2` (`mend2_debug.rs2:5-9`) only sets not_started and teleports, and the `::give` lines are the guide's `getItemRequirements()` bring-alongs. The hole is entirely in the content, and review could not see it.

The ledger at `selftest/quests/quest_mourningsendpartii/play/ledger.tsv` has 40 PASS rows. Row 22 detail reads: "content line 'You make your way back into the Temple of Light and spend a long while turning its mirrored pillars...'".

## Per-quest detail (every non-FULL quest)

### quest_atailoftwocats atailoftwocats helper=atailoftwocats/ATailOfTwoCats.java

COUNTS guide_steps=24 driven=19 skipped_content=3 skipped_cheat=1 skipped_other=1 verdict=MIXED first_gap=findBob (stage 15: operate the catspeak amulet (e)); the test talks to Unferth instead

Paths: T = R/test/quests/atailoftwocats.lua, C = R/OSRS-Content/osrs239-content/server/scripts/quests/quest_atailoftwocats/scripts/twocats.rs2, QH = QH/.../atailoftwocats/ATailOfTwoCats.java.
Ledger: selftest/quest_tests/atailoftwocats/ledger.tsv has 89 rows, every one PASS (SUMMARY 89 PASS).
Bring-alongs given by ::give (legit, QH getItemRequirements QH:240): catspeak amulet (e) (T:36), 5 death runes (T:42), rake/dibber/4 potato seeds/logs/tinderbox/chocolate cake/bucket of milk/shears (T:59-66). Prereq cheats (not guide steps): ::complete quest_icthlarinslittlehelper (T:48), ::complete quest_gertrudescat (T:52). No desert shirt/robe or vial of water is given; the content never checks for them.

| guide varp | guide step (short) | test row(s) or cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Unferth, "I'll help you." | goto-unferth, talkToUnferth(+dialog), quest.stage.accepted | DRIVEN | T:104-109; C:36-42 |
| 5,10 | Talk to Hild (5 death runes) | goto-hild, talkToHild(+dialog), quest.stage.hild_done | DRIVEN | T:116-121; C:55-66 |
| 15 | findBob: operate catspeak amulet (e) to locate Bob | talkToUnferthFindBob (talk to Unferth instead) | SKIP-OTHER | T:132-137. The amulet op IS wired at stage 10: [opheld3,twocats_amuletofcatspeak] C:78-80 -> [label,twocats_find_bob_1] C:89-97. The test uses Unferth's own dispatch (C:29) to reach the same label. The comment at T:123-131 says no Bob interaction is wired, which is true of Bob but not of the amulet |
| 15 | talkToBob: talk to Bob the Cat | none | SKIP-CONTENT | find_bob_1 sets %twocats_quest=20 directly (C:97). There is no Bob npc/opnpc for this quest (T:125-129) |
| 20 | Talk to Gertrude, ask about Bob's parents | goto-gertrude, talkToGertrude(+dialog), quest.stage.gertrude_done | DRIVEN | T:155-160; gertrude.rs2:113-130 -> C:118-120 |
| 25,28 | Talk to Reldo (cat question, Robert the Strong) | goto-reldo, talkToReldo(+dialog), quest.stage.reldo_done | DRIVEN | T:171-176; reldo.rs2:26-27 -> C:140-147 (the port collapses the "Robert the Strong" sub-choice to one line) |
| 30 | findBobAgain: use the amulet (e) again | amulet.open2(+dialog), quest.stage.bob_found_again | DRIVEN | T:187-191; C:81-82, 153-155 |
| 30 | talkToBobAgain | none | SKIP-CONTENT | find_bob_2 sets %twocats_quest=35 directly (C:153-155) |
| 35 | Talk to the Sphinx in Sophanem | goto-sphinx, talkToSphinx(+dialog), quest.stage.chores_ready | DRIVEN | T:201-206; dragonslayer2.rs2:1497-1498 -> C:167-169 |
| 40 | Rake Unferth's patch | chore.rake(+dialog), chore.raked | DRIVEN | T:222-224; C:207-216 |
| 40 | Plant 4 potato seeds | chore.plant(+dialog), chore.planted | DRIVEN | T:229-231; C:218-243 |
| 40 | Make Unferth's bed | chore.bed(+dialog), chore.bed_made | DRIVEN | T:237-239; C:264-275 |
| 40 | Use logs on the fireplace | chore.logs(+dialog), chore.logs_placed | DRIVEN | T:246-248; C:293-301 |
| 40 | Use a tinderbox on the fireplace | chore.light(+dialog), chore.fire_lit | DRIVEN | T:250-252; C:303-314 |
| 40 | Use a chocolate cake on the table | chore.cake(+dialog), chore.cake_placed | DRIVEN | T:266-268; C:331-334 |
| 40 | Use a bucket of milk on the table | chore.milk(+dialog), chore.milk_placed | DRIVEN | T:269-271; C:335-338 |
| 40 | Use shears on Unferth | chore.shear(+dialog), chore.sheared | DRIVEN | T:278-280; C:363-383 |
| 40 | Wait 15-35 min for the potatoes to grow | garden.grow = t.cheat("::twocats_growpotatoes"), garden.grown | SKIP-CHEAT | T:289-290. The real growth is [softtimer,twocats_potato_grow] (C:242, 428-429). The debugproc (C:439-449) loops the same advance proc; it is a quest debugproc that skips the wait. The test then talks to Unferth at 40 -> 45 (T:295-299; C:455-462) |
| 45 | Report to Unferth | reportToUnferth(+dialog), quest.stage.reported | DRIVEN | T:304-306; C:468-470 |
| 50 | Talk to the Apothecary | goto-apothecary, talkToApothecary(+dialog), quest.stage.apothecary, reward.doctors_hat | DRIVEN | T:314-321; apothecary.rs2:11 -> C:484-492 |
| 55 | Talk to Unferth wearing hat + desert shirt/robe, no weapon | goto-unferth-cure, cureUnferth(+dialog), quest.stage.cured | DRIVEN (content weakened) | T:328-331. C:499-501 is unconditional: no hat, desert, weapon or vial check, so the disguise part of this leg does not exist in the port |
| 60 | findBobToFinish: use the amulet (e) once more | amulet.open3(+dialog), quest.stage.bob_found_last | DRIVEN | T:336-338; C:83-84, 507-509 |
| 60 | talkToBobToFinish | none | SKIP-CONTENT | find_bob_3 sets %twocats_quest=65 directly (C:507-509) |
| 65 | Talk to Unferth to complete | finishQuest(+dialog), quest.stage.complete, quest.varp_complete/scroll/points, reward.* | DRIVEN | T:348-350, 372-413; C:515-533 |

Summary: The quest runs end to end through real interactions, and all 89 ledger rows PASS. There are two kinds of gap. First, the content has no Bob the Cat: each of the three "talk to Bob" legs is collapsed into the amulet/locate label (C:97, 155, 509). Second, the test has two gaps of its own: (a) the potato wait is skipped with the ::twocats_growpotatoes debugproc, and (b) the first "find Bob" is triggered by talking to Unferth when the amulet's Open op (C:78-80) would do it. The port also drops the step-55 disguise requirement (C:499-501) and Reldo's "Robert the Strong" sub-choice.

### quest_betweenarock betweenarock helper=betweenarock/BetweenARock.java

COUNTS guide_steps=23 driven=22 skipped_content=1 skipped_cheat=0 skipped_other=0 verdict=CONTENT GAP first_gap=useGoldBarOnDondakan (60)

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver; test = R/test/quests/betweenarock.lua; content = R/OSRS-Content/osrs239-content/server/scripts/quests/quest_betweenarock/scripts/; guide = QH/.../betweenarock/BetweenARock.java.
Ledger: R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/betweenarock/ledger.tsv -- SUMMARY 123 PASS, pass=123 fail=0 (every row cited below is PASS).
Setup (lua:40-57): ::clearinv, ::setlevel attack/strength/defence/hitpoints 99, mining 40, smithing 50, ::complete quest_fishingcontest (prereq), ::give rune_scimitar, adamant_pickaxe, hammer, gold_bar 4, ammo_mould. All given items are guide bring-alongs (getItemRequirements, java:397-405: pickaxe, goldBars4, hammer, cannonMould, coins1000) or combat gear -- not skips. Travel (tunnels, ferrymen, boatman, Khorvak stairs) done by ::goto; guide lists them as sub-steps of the action they lead to (java:277, 283, 320, 336, 349, 359, 380) and content's travel.rs2 writes no quest state -- merged, not counted.

| guide varp value | guide step text (short) | test row(s) or cheat | class | evidence (file:line) |
|---|---|---|---|---|
| 0 | Talk to Dondakan (start) | talkToDondakan, talkToDondakan-dialog, quest.stage.told_of_rock (lua:91-101) | DRIVEN | dondakan.rs2:22-29 |
| 10 | Talk to Dwarven Engineer | talkToEngineer(-dialog), quest.stage.engineer_confirmed (lua:106-112) | DRIVEN | schematics.rs2:20-25 |
| 20 | Talk to Rolad | talkToRolad(-dialog), quest.stage.gathering_pages (lua:117-125) | DRIVEN | pages.rs2:21-27 |
| 30 | Mine low level rocks for a page (page 3) | mineRock, gotPage3 (lua:144-151) | DRIVEN | pages.rs2:117-130 |
| 30 | Kill scorpions for a page (page 1) | attackScorpion, killScorpion, gotPage1 (lua:153-162) | DRIVEN | pages.rs2:63-83 |
| 30 | Search the mine carts for a page (page 2) | searchCart, gotPage2, pagesCombined (lua:170-183) | DRIVEN | pages.rs2:87-99, combine :102-109 |
| 30 | Talk to Rolad again (hand in pages) | talkToRoladWithPages(-dialog), quest.stage.book_ready (lua:188-195) | DRIVEN | pages.rs2:30-39 |
| 40 | Read the entire dwarven lore book | readBook, readBook-dialog, quest.stage.returned_with_book (lua:206-212) | DRIVEN | pages.rs2:137-145 |
| 50 | Return to Dondakan with book + gold bar | talkToDondakanWithBook(-dialog), quest.stage.gold_bar_shown (lua:231-240) | DRIVEN | dondakan.rs2:36-46 |
| 60 | Use a gold bar on Dondakan | none -- the stage-50 talk already set it ("Show him the gold bar." is a narrated chat line) | SKIP-CONTENT | dondakan.rs2:43-46 sets %dwarfrock_gold_cannonball=1 and stage 60 inside the talk, bar not used/consumed; the opnpcu gold_bar handler (dondakan.rs2:135-138) exists only at stage 50 as an ALTERNATIVE to the talk, so the talk + use sequence the guide has cannot be done |
| 60 | Use gold bar on furnace -> gold cannonball | smeltCannonball, gotCannonball (lua:264-272) | DRIVEN | dondakan.rs2:170-182 |
| 60 | Use gold cannonball on Dondakan | useCannonballOnDondakan(-choice, -dialog) (lua:284-301) | DRIVEN | dondakan.rs2:140-162 |
| 60/70 | Talk to Dondakan after the shot | talkToDondakanAfterShot(-dialog) -> quest.stage.fired_into_rock; talkToDondakanForSchematic(-dialog) -> quest.stage.assembling_schematics (lua:304-319) | DRIVEN | dondakan.rs2:50-57, 66-71 |
| 80 | Read the last page of the book again (base schematic) | readBookAgain, gotBaseSchematic (lua:330-338) | DRIVEN | pages.rs2:148-150 |
| 80 | Talk to the Dwarven Engineer (schematic) | talkToEngineerForSchematic(-dialog), gotSchematic2 (lua:343-353) | DRIVEN | schematics.rs2:32-43 |
| 80 | Talk to Khorvak under White Wolf Mountain | talkToKhorvak(-dialog) choice "No, I've had enough...", gotSchematic3 (lua:359-370) | DRIVEN | schematics.rs2:90-98 |
| 80 | Assemble the schematics (puzzle) | assembleSchematic(-dialog-1/-2), gotAssembledSchematic (lua:377-404) | DRIVEN (note) | schematics.rs2:100-121 -- content replaces the rotate/slide PuzzleStep with a single opheld1; test drives what content offers |
| 80 | Use 3 gold bars on an anvil -> gold helmet | smithHelmet, helmetSynced (lua:424-429) | DRIVEN | schematics.rs2:126-152 |
| 80 (+90 substep talkToDondakanForEnd) | Prepare for a fight, return to Dondakan, get fired in | talkToDondakanForRealm-probe, equip.helmet, talkToDondakanForRealm(-dialog), quest.stage.in_the_realm, enterRealm-dialog (lua:439-471) | DRIVEN | dondakan.rs2:74-102 (one talk sets 90 and calls dwarfrock_enter_realm); java:380 makes ForEnd a sub-step |
| 90 | Mine 6 gold ores | mineGoldOre-1..11, gotGoldOre=6 (lua:495-510) | DRIVEN | realm.rs2:68 gates on 6 ore (generic mining) |
| 90 | TALK to the central wall of flame | approachFlame (click_loc op1; no -bypass row in ledger), approachFlame-dialog (lua:544-560) | DRIVEN | realm.rs2:54-83 |
| 90 | Kill the avatar | avatarPresent, attackAvatar-1, killAvatar (var.await_server 100), quest.stage.avatar_defeated (lua:584-632) | DRIVEN | realm.rs2:153-184 (ai_queue3 death sets 100) |
| 100 | Talk to Dondakan to finish | finishQuest(-dialog), quest.varp_complete, quest.scroll_title, quest.points, rewards (lua:645-663) | DRIVEN | dondakan.rs2:112-115, shared.rs2:50-55 |

Summary: every guide step is driven through the real client with no stage cheats; the only gap is content-side -- the port folds "use a gold bar on Dondakan" (stage 60) into the stage-50 talk (dondakan.rs2:43-46), so that item-use leg is never performed (the opnpcu gold_bar path exists only as an alternative to the talk). The schematic puzzle is also a content simplification (one opheld1 instead of the rotate/slide interface, schematics.rs2:100-121), counted as driven since the leg itself is performed.

### quest_biohazard biohazard helper=biohazard/Biohazard.java (+ GiveIngredientsToHelpersStep.java)

COUNTS guide_steps=28 driven=24 skipped_content=0 skipped_cheat=4 skipped_other=0 verdict=TEST GAP first_gap=enterBackyardOfHeadquarters (squeeze through mournerstewfence) bypassed by ::goto

Paths: T = R/test/quests/biohazard.lua; L = R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/biohazard/ledger.tsv (132 rows, SUMMARY 132 PASS, no non-PASS rows); S = R/OSRS-Content/osrs239-content/server/scripts.
Setup (T:72-85): ::clearinv, ::biohazardreset (reset debugproc, quest_biohazard.rs2:81-83), ::give gasmask 1 (guide getItemRequirements() = gasMask only, Biohazard.java:398-403 -> legit bring-along), ::complete quest_plaguecity (prerequisite), ::give rune_scimitar + ::setlevel hp/att/str/def (combat prep, not a quest leg). No ::setvar, no quest-stage debugproc, no ::kill; biohazard_pass_mourner (quest_biohazard.rs2:85-91) is NOT used.
Note: every t.player.goto_tile is a ::goto teleport (docs/QUEST_AUTHORING.md:161). Pure travel is merged with the action it leads to; the four rows below marked SKIP-CHEAT are guide action steps (operate loc / talk npc) whose content exists and that the test replaces with a ::goto teleport.

| guide varp | guide step (short) | test row(s) or cheat | class | evidence |
|---|---|---|---|---|
| 0 | talkToElena: talk to Elena | talkToElena, talkToElena-dialog (L5-7) T:126-141 | DRIVEN | elena.rs2:117 writes ^biohazard_started |
| 1 | talkToJerico | talkToJerico, -dialog (L9-11) T:146-157 | DRIVEN | jerico.rs2:8,48 |
| 2 | getBirdFeed: Jerico's cupboard | openJericoCupboard, searchJericoCupboard, -dialog, expectBirdfeed (L15-18) T:171-193 | DRIVEN | quest_biohazard_locs.rs2:20-47 |
| 2/3 | getPigeonCage: behind Jerico's house | pickupPigeons (L26) T:231-238 | DRIVEN | ground 'pigeons' areas/world/configs/m40_51.spawn:116-118 |
| 2 | investigateWatchtower (bird feed on tower) | investigateWatchtower (op1) + useBirdfeedOnWatchtower, birdfeed.consumed, stage.used_birdfeed (L13,22-24) T:164-165,198-218 | DRIVEN | [oplocu,biowatchtower_op] quest_biohazard_locs.rs2:58-66 |
| 3 | clickPigeonCage: open cage by tower | releasePigeons (inv_op pigeons 1), pigeoncage.returned, stage.released_pigeons (L28-30) T:246-253 | DRIVEN | [opheld1,pigeons] quest_biohazard_locs.rs2:69-79 |
| 4 | talkToOmartToEnterWestArdougne | talkToOmart, -dialog, stage.climbed_ladder (L32-34) T:260-270 | DRIVEN | [opnpc1,omart] quest_biohazard_locs.rs2:173-209 |
| 5 | enterBackyardOfHeadquarters: squeeze through fence | none -- goto-rottenApple ::goto 2549,3332,0 straight into the yard (L35) T:278 | SKIP-CHEAT (::goto) | [oploc1,mournerstewfence] general_use/scripts/fence.rs2:5-30 exists; no stage write |
| 5 | pickupRottenApple | takeRottenApple (L36) T:278-285 | DRIVEN | m39_52.spawn:56 ground obj |
| 5 | useRottenAppleOnCauldron | poisonCauldron, cauldron.poisonedSettle, stage.poisoned_stew (L40-42) T:299-317 | DRIVEN | [oplocu,mournercauldron_op] quest_biohazard_locs.rs2:103-121 |
| 6 | exitBackyardOfHeadquarters: fence back out | none -- goto-nurseHut ::goto 2518,3276,0 (L43) T:329 | SKIP-CHEAT (::goto) | fence.rs2:5-30 (reverse branch :15-24) |
| 6 | searchSarahsCupboard: doctor's gown | openNurseCupboard, searchNurseCupboard, expectDoctorGown, equipDoctorGown (L46-49) T:339-348 | DRIVEN | quest_biohazard_locs.rs2:123-142 |
| 6 | enterMournerHeadquarters: door while wearing gown | none -- test comment T:350-357 says click FAILed on a driver settle issue; goto-mournerstew2 ::goto 2551,3327,1 (L51) T:367 | SKIP-CHEAT (::goto) | gown-gated guard branch areas/area_ardougne_west/scripts/doors.rs2:19-24,76-95 ("In you go doc.") |
| 6 | goUpstairs + killMourner (for cage key) | talkToMournerstew2, -dialog, mournerstew2.attack, mournerstew2.dead, expectMournerKey (L52-58) T:366-421 | DRIVEN (real combat; L56 "dead after 28 tick(s), 4 re-engagement(s)"; stats boosted by ::setlevel) | [opnpc1,mournerstew2] mourner.rs2:47-78, [ai_queue3,mournerstew2] mourner.rs2:79 grants key |
| 6/7 | searchCrateForDistillator (in caged area) | goto-mournerCrate ::goto 2554,3327,1, searchMournerCrate, expectDistillator, stage.found_distillator (L59-62) T:427-435 | DRIVEN (caveat: cage gate never opened -- key obtained but never used; ::goto lands at the crate) | crate quest_biohazard_locs.rs2:249-281 (no key check); key gate [oplocu,mournerquaters_gate*] :144-171 unused |
| 7 | goBackDownstairs + talkToKilron: return over the wall | none -- goto-elenaDistillator ::goto 2592,3336,0 from West Ardougne (L63) T:446; header T:62-67 calls it "pure navigation" | SKIP-CHEAT (::goto) | [opnpc1,kilron] quest_biohazard_locs.rs2:225-241 (p_teleport ^biohazard_east_wall_dest) |
| 7 | talkToElenaWithDistillator | talkToElenaDistillator, dialog-1/2/3, expectVialsAndSample, stage.given_distillator (L64-71) T:446-496 | DRIVEN | elena.rs2:60-94 |
| 10 | talkToTheChemist (Rimmington) | talkToChemist, chemist-dialog, expectTouchPaper, stage.spoken_chemist (L73-76) T:503-529 | DRIVEN | chemist.rs2:19,84-97 |
| 12 | giveHopsBroline | talkToDrunk1, drunk1-dialog, drunk1.gaveCorrectVial (L78-80) T:534-554 | DRIVEN | errand_boys.rs2:10,57 |
| 12 | giveChancyHoney | talkToGambler1, -dialog, gambler1.gaveCorrectVial (L82-84) T:556-576 | DRIVEN | errand_boys.rs2:104,140 |
| 12 | giveVinciEthenea | talkToArtist1, -dialog, artist1.gaveCorrectVial (L86-88) T:578-596 | DRIVEN | errand_boys.rs2:209,232 |
| 12 | hopsVarrock: collect broline | talkToDrunk2, dialog-1/2, drunk2.returnedVial (L90-94) T:603-629 | DRIVEN | errand_boys.rs2:69 |
| 12 | chancyVarrock: collect honey | talkToGambler2 (L96 detail: "drive.op bypass after 2 failed on-screen presses"), dialog-1/2, gambler2.returnedVial (L97-100) T:631-678 | DRIVEN (op packet, not an on-screen click) | errand_boys.rs2:164 |
| 12 | vinciVarrock: collect ethenea | talkToArtist2 (L102 detail: "drive.op bypass"), artist2-dialog, artist2.returnedVial (L103-104) T:680-706 | DRIVEN (op packet) | errand_boys.rs2:268 |
| 12 | talkToAsyff (free priest set; buy = alternative, counted once) | talkToAsyff, asyff-dialog, expectPriestOutfit, equip x2 (L106-110) T:712-731 | DRIVEN | quest_eaglepeak/scripts/asyff.rs2:17 |
| 12 | talkToGuidor (priest set worn) | enterGuidorHouse (guidordoor), talkToGuidor, guidor-dialog, guidor.itemsConsumed, stage.found_secret (L114-119) T:742-790 | DRIVEN | guidors_wife.rs2:11; guidor.rs2:9,40 |
| 14 | returnToElenaAfterSampling | talkToElenaReport, -dialog, stage.reported_elena (L121-123) T:793-807 | DRIVEN | elena.rs2 [opnpc1,elena2]:10 |
| 15 | informTheKing (+ castle stairs, travel merged) | goto-kingLathas ::goto 2578,3293,1, talkToKingLathas, -dialog, expect_complete, skill.thievingReward (L125-132) T:817-851 | DRIVEN | area_ardougne_east/scripts/king_lathas.rs2:35 |

Summary: Every stage-writing leg is driven through real triggers and the ledger is 132/132 PASS with no stage cheats. The four gaps are all West-Ardougne navigation legs the content implements but the test teleports past with ::goto: the backyard fence in and out (fence.rs2:5), the gown-gated Mourner HQ door (doors.rs2:19-24,76-95; the test comment T:350-357 admits the click failed and was replaced), and the Kilron return crossing (quest_biohazard_locs.rs2:225-241). A fifth near-gap: the ::goto straight onto the crate tile also skips the locked cage gate, so the mourner key the fight earns is never used (quest_biohazard_locs.rs2:147-171).

### quest_blackarmgang blackarmgang helper=shieldofarrav/ShieldOfArravBlackArmGang.java

COUNTS guide_steps=14 driven=9 skipped_content=3 skipped_cheat=2 skipped_other=0 verdict=MIXED first_gap=startQuest (talk to Reldo / book / Reldo again: the port's tramp starts the Black Arm route with no Reldo gate)

Paths: QH = quest-helper/src/main/java/com/questhelper/helpers/quests/shieldofarrav/ShieldOfArravBlackArmGang.java; T = R/test/quests/blackarmgang.lua; Q = R/OSRS-Content/osrs239-content/server/scripts/quests/quest_blackarmgang/scripts/quest_blackarmgang.rs2; V = R/OSRS-Content/osrs239-content/server/scripts/areas/varrock/scripts/.
Ledger: R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/blackarmgang/ledger.tsv -- SUMMARY 42 PASS, fail=0 (every row cited below is PASS).
Setup (T:67-86): ::clearinv, ::setlevel hitpoints/defence/attack/strength 99 (combat gearing for the Weaponsmaster fight; not a quest leg).

| guide varp value | guide step text (short) | test row(s) or cheat | class | evidence (file:line) |
|---|---|---|---|---|
| 0 | startQuest: Talk to Reldo ("I'm in search of a quest") | none | SKIP-CONTENT | QH:75-78,138-140. The port's tramp starts the Black Arm route straight from not_started: V/tramp.rs2:50-51 (`%blackarmgang = ^blackarmgang_started`), with no Reldo/book gate. The Reldo leg exists only on %phoenixgang (V/reldo.rs2:138), and it neither gates nor advances %blackarmgang |
| 0 | searchBookcase: Search the bookcase for the book, then read it | none | SKIP-CONTENT | QH:141-142. [oploc1,questbookcase] exists (Q:6-17) but only on %phoenixgang; the Black Arm stage never reads it (tramp.rs2:50-51 above) |
| 0 | talkToReldoAgain: "Okay, I've read that book..." | none | SKIP-CONTENT | QH:143-145. Same as above: %phoenixgang only; not a gate on %blackarmgang |
| 1 | talkToCharlie: Talk to Charlie the Tramp (alleyway / let me join) | goto-talkToCharlie, talkToCharlie, talkToCharlie-dialog, quest.stage.started | DRIVEN | T:109-121; V/tramp.rs2:34-54; ledger rows 3-6 PASS |
| 1 | talkToKatrine: Talk to Katrine (Black Arm Gang / member / give me a try / Ok, no problem) | goto-talkToKatrine, talkToKatrine, talkToKatrine-dialog, quest.stage.spoken_katrine | DRIVEN | T:130-158; V/katrine.rs2:95; ledger rows 7-10 PASS |
| 2 | getWeaponStoreKey + goUpToWeaponStore: get the weapon store key from another player, go up the ladder | goto-weaponStore = t.player.goto_tile(3252,3384,1), which is a ::goto teleport (docs/QUEST_AUTHORING.md:161) | SKIP-CHEAT | T:172. The content locks the store: phoenixdoor2 (loc 2398, placed m50_52.jl2:570 = 3251,3386,0) refuses without the key (Q:49,56-61) and opens only on [oplocu,phoenixdoor2] with phoenixkey2 (Q:50-53). The key comes only from Straven, on the Phoenix side (V/straven.rs2:88,101). The test never holds the key and teleports past the door. QUEST_AUTHORING.md:274 sanctions a goto past a door the hand-in never reads, but by this audit's rule it is still a skipped leg |
| 2 | killWeaponsMaster: Kill the Weaponsmaster | attackWeaponsmaster, weaponsmasterDead, weaponsmaster.gone | DRIVEN | T:175-190 (a real player.attack fight; the 99 combat stats from setup are gearing only); ledger rows 12-14 PASS (dead after 17 ticks, 0 re-engagements) |
| 2 | pickupTwoCrossbows: Pick up TWO phoenix crossbows | takeCrossbow1, takeCrossbow2, crossbows.two | DRIVEN | T:199-216; Q:87-94 [opobj3,phoenix_crossbow]; ledger rows 15-17 PASS (0->1->2) |
| 2 | returnToKatrine (+goDownFromWeaponStore): Return to Katrine with the crossbows | goto-handInKatrine, handInKatrine, handInKatrine-dialog, quest.stage.joined | DRIVEN (the walk down is a goto teleport, navigation only) | T:221-229; V/katrine.rs2:113-119; ledger rows 18-21 PASS |
| 3 | getShieldFromCupboard (+goUpstairsInBase): Search the cupboard for half the shield | goto-cupboard, cupboard.search, cupboard.dismiss, shield2.taken | DRIVEN (stairs by goto teleport; the blackarmdoor would open anyway at joined, Q:96-100) | T:237-251; Q:104-114; ledger rows 22-25 PASS |
| 3 | talkToHaig (+goDownstairsInBase): Talk to Curator Haig with the shield half | goto-talkToHaig, talkToHaig, talkToHaig-dialog, curator.certificates | DRIVEN | T:258-288; V/curator.rs2:152-165; ledger rows 26-29 PASS |
| 3 | tradeCertificateHalf: Trade a certificate half with another player | stage.partnerHalf = `::give arravcertificate_lft 1` | SKIP-CHEAT | T:296. arravcertificate_lft is quest-obtained (curator_take_phoenix_half, Phoenix side), not a bring-along. The test gives it by cheat in place of the two-player trade |
| 3 | combineCertificate: Use the two halves together | combineCertificates, combineCertificates-dismiss, combine.certificate | DRIVEN | T:313-333; Q:122-138; ledger rows 31-33 PASS |
| 3 | talkToRoald: Talk to King Roald with the certificate | goto-talkToRoald, talkToRoald, talkToRoald-dialog, quest.stage.complete, reward.coins, expect_complete | DRIVEN | T:343-380; V/king_roald.rs2:75,93; Q:140-148; ledger rows 34-42 PASS |

Summary: The whole Black Arm chain from Charlie to King Roald is driven by real clicks. There are three kinds of gap:
- Content: the port starts the route at the tramp and never gates it on Quest Helper's Reldo/book opener (tramp.rs2:50-51).
- Test, the key: the weapon-store key is a two-player leg. The test never gets it and reaches the storeroom by ::goto teleport past the locked phoenixdoor2 (T:172), so neither the key hand-off nor the door unlock is exercised.
- Test, the certificate: the Phoenix partner's certificate half is supplied with `::give arravcertificate_lft 1` (T:296) in place of the trade.
Both test gaps stand in for a second player, which a single client cannot provide.

### quest_blackknight blackknight helper=blackknightfortress/BlackKnightFortress.java

COUNTS guide_steps=8 driven=4 skipped_content=0 skipped_cheat=4 skipped_other=0 verdict=TEST GAP first_gap=enter fortress through the guarded door (bkfortressdoor1) wearing the bronze med helm + iron chainbody disguise

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver, QH=/Users/matthewevers/Documents/git_repos/quest-helper
Guide = QH/src/main/java/com/questhelper/helpers/quests/blackknightfortress/BlackKnightFortress.java (BKF.java)
Test = R/test/quests/blackknight.lua ; Ledger = R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/blackknight/ledger.tsv (SUMMARY 37 PASS, fail=0 -- every row PASS, incl. quest.varp_complete client=4 server=4, quest.points 12->15, reward.coins 0->2500)
Content = R/OSRS-Content/osrs239-content/server/scripts/quests/quest_blackknight/scripts/quest_blackknight.rs2 (qbk.rs2); Sir Amik = R/OSRS-Content/osrs239-content/server/scripts/areas/falador/scripts/sir_amik_varze.rs2 (amik.rs2)

Counting: Falador-castle stair sub-steps are merged into the Amik talks; the six fortress ladders (climbUpLadder1..climbDownLadder6 and back) are merged into the leg they lead to (listen / cabbage); pushWall3 is a sub-step of goUpLadderToCabbageZone; watchCutscene is merged into useCabbageOnHole; recovery steps (exitBasement etc.) are not counted.

| guide varp value | guide step text (short) | test row(s) or cheat | class | evidence (file:line) |
|---|---|---|---|---|
| 0 | speakToAmik: "Speak to Sir Amik Varze" + choices "I seek a quest!" / "I laugh in the face of danger!" / "Yes." | goto-amik (goto_tile), amik.greet, amik.choose_seek_quest, amik.choose_laugh, amik.choose_start_yes, amik.accept_drain_close, dossier.granted, quest.stage.started (ledger rows 3-13 PASS) | DRIVEN | BKF.java:216-221; blackknight.lua:60-95; amik.rs2:74-78 (grant dossier, advance to started) |
| 1 | enterFortress: "Enter the Black Knights' Fortress" through bkfortressdoor1 (needs disguise worn) | none -- `t.player.goto_tile(3026,3509,0)` (a `::goto` teleport, docs/QUEST_AUTHORING.md:161) lands inside; helm/chainbody given but never equipped (lua:32-33, header lua:14-19 says the guarded door is never walked) | SKIP-CHEAT | BKF.java:228-230; content has the leg: qbk.rs2:10-17 (disguise gate -> guard refusal label qbk.rs2:166-179), disguise check qbk.rs2:74-87; bypass lua:98 |
| 1 | pushWall: "Push the wall to enter a secret room" (bksecretdoor 3016,3517,0) + ladders 1-6 to the listening room | none -- same goto_tile teleport into the listening room | SKIP-CHEAT | BKF.java:231-243; content: qbk.rs2:241-243 ([oploc1,bksecretdoor] walk-through); bypass lua:98 |
| 1 | listenAtGrill: "Listen at the grill" | grill.locate, listen.grill (drive.click_minimenu witchgrill op1, "Listen-at Grill"), grill.dialogue_drain, quest.stage.listened (ledger 15-18 PASS, spy=2) | DRIVEN | BKF.java:244-245; lua:100-168; qbk.rs2:289-300 |
| 2 | goUpLadderToCabbageZone: go through the east (banquet) door, choose "I don't care. I'm going in anyway.", climb the meeting ladder | none -- `t.player.goto_tile(3026,3510,1)` then goto-hole-exact teleport to 3031,3507,1 | SKIP-CHEAT | BKF.java:260-262; content: qbk.rs2:199-218 ([oploc1,bkfortressdoor2] guard warning, option 2 walks through + aggro); bypass lua:171,181 |
| 2 | pushWall2: "Push the wall to the south-east to enter the storage room" (bksecretdoor 3030,3510,1) | none -- teleport lands inside the cabbage-hole room | SKIP-CHEAT | BKF.java:263-264; content: qbk.rs2:241-243; bypass lua:171,181 |
| 2 | useCabbageOnHole: "USE the cabbage on the hole" + "Watch the cutscene" | hole.locate, goto-hole-exact, hole.lookup, use.cabbage_on_hole (player.use_on cabbage -> blackknighthole), hole.sabotage_drain, quest.stage.sabotaged, cabbage.consumed (ledger 20-26 PASS, spy=3) | DRIVEN | BKF.java:265-267,321-323; lua:173-198; qbk.rs2:245-279 (oplocu, inv_del cabbage, scene dialogue, advance to sabotaged :277) |
| 3 | returnToAmik: "Return to Sir Amik Varze ... to complete the quest" | goto-amik-return, amik.return_talk, amik.return_drain, handin.await_mesbox, handin.mesbox_text, handin.dismiss_mesbox, quest.varp_complete, quest.scroll_*, quest.points, quest.journal, reward.coins (ledger 27-37 PASS) | DRIVEN | BKF.java:273-275; lua:201-237; amik.rs2:102-107 (queue completion) -> qbk.rs2:313-325 |

Bring-alongs / setup (not skips): `::give bronze_med_helm 1`, `::give iron_chainbody 1`, `::give cabbage 1` are all Quest Helper getItemRequirements (BKF.java:363-369; lua:32-34) and the guide never has the player obtain them as a quest step. `::setvar qp 12` is the prerequisite QuestPointRequirement(12) (BKF.java:357; lua:35). `::blackknightrun` is the content's reset debugproc (qbk.rs2:339-356; sets %spy=0, no leg). No ::setvar of %spy, no ::complete.

Summary: The two %spy-writing actions (listen at the grill, cabbage on the hole) and both Sir Amik conversations are driven for real and the ledger is 37/37 PASS, but every fortress-access leg is bypassed by `goto_tile` (`::goto` teleport): the disguise-gated entrance door (qbk.rs2:10-17), both secret walls (qbk.rs2:241-243) and the banquet-hall door dialogue (qbk.rs2:199-218) are implemented in content yet never exercised, and the disguise items are given but never worn. The test header (lua:6-19) justifies this because those locs do not write %spy, but they are the quest's infiltration mechanic, so this is a TEST GAP. Side note: the QUEUE.tsv row still carries a stale REJECTED reviewer note describing failures the current ledger no longer shows.

### quest_cog cog helper=clocktower/ClockTower.java

COUNTS guide_steps=14 driven=10 skipped_content=0 skipped_cheat=4 skipped_other=0 verdict=TEST GAP first_gap=pushWall (push the secret wall in the tunnel to reach the blue cog's cell)

Paths: QH = quest-helper/.../quests/clocktower/ClockTower.java; T = test/quests/cog.lua; S = OSRS-Content/osrs239-content/server/scripts; Q = S/quests/quest_cog/scripts.
Guide: steps.put(0, talkToKojo); steps.put(1..4, doQuest); steps.put(5..7, goFinishQuest) (QH:355-392). The red/blue/black/white sub-ladders are at QH:357-378. Excluded from the count: syncStep (a journal re-sync, not an action) and every ladder/stair climb and plain door (travel only). getBucket/fillBucket (QH:230-233) is left out because the guide itself says it is optional and fills the bucketOfWater bring-along (QH:398-403). The test gives that bucket with `::give bucket_water 1` (T:51), which is legitimate.
Ledger: selftest/quest_tests/cog/ledger.tsv. 37/37 PASS, exit=0.
Travel in this test is `t.player.goto_tile`, which teleports (T:106 says "a goto_tile teleport"). Where the guide makes the route itself an action (a loc to operate or a puzzle), skipping that route by teleport is classed as a cheat.

| varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | talkToKojo: talk to Brother Kojo, choose "Yes." | goto-kojo-start, talkToKojo, talkToKojo-dialog, quest.stage.tasked_with_placing_cogs | DRIVEN | T:76-93; Q/brother_kojo.rs2:5,42 |
| 1 | pickUpRedCog (through the SE door) | goto-redcog (teleport), pickup.redcog | DRIVEN (the door is travel) | T:96-100; Q/cogs.rs2:20 [opobj3,redcog] |
| 1 | redCogOnRedSpindle | goto-red-spindle, place.redcog, quest.stage.three_remaining_cogs | DRIVEN | T:105-122; Q/quest_cog_spindles.rs2:12 |
| 2 | pushWall: follow the tunnel and push the wall at the end (SECRETDOOR2) | none; goto-bluecog teleports straight into the cell at 2574,9633 | SKIP-CHEAT (teleport past the leg) | T:125; S/general_use/scripts/door_walkthrough_fallback.rs2:100 [oploc1,secretdoor2] |
| 2 | pickUpBlueCog | pickup.bluecog | DRIVEN | T:126-129; Q/cogs.rs2:15 |
| 2 | blueCogOnBlueSpindle | goto-blue-spindle, place.bluecog, quest.stage.two_remaining_cogs | DRIVEN | T:131-139; Q/quest_cog_spindles.rs2:18 |
| 3 | pickupBlackCog: cool it with the bucket of water, then take it | pickup.blackcog (use_on bucket_water), pickup.blackcog.dismiss, pickup.blackcog.confirm | DRIVEN | T:142-160; Q/cogs.rs2:25-35 |
| 3 | blackCogOnBlackSpindle | goto-black-spindle, place.blackcog, quest.stage.one_remaining_cog | DRIVEN | T:162-178; Q/quest_cog_spindles.rs2:9 |
| 4 | pickUpRatPoison (NW of the dungeon, through the poordoor) | none | SKIP-CHEAT (teleport past the rat puzzle) | T:183; spawn S/areas/world/configs/m40_150.spawn:85 |
| 4 | pullFirstLever (CTLEVERA up) | none | SKIP-CHEAT (same) | T:183; Q/quest_cog_gates_and_levers.rs2:67 [oploc1,ctlevera] |
| 4 | ratPoisonFood: use the rat poison on the food trough | none | SKIP-CHEAT (same) | T:183; Q/quest_cog_food_trough.rs2:9-29 [oplocu,ctfoodtrough] sets ^quest_cog_rat_door_bit. The gate at Q/quest_cog_gates_and_levers.rs2:11-18 [oploc1,ctratgatec] opens only when that bit is set ("This door does not seem to be openable." otherwise). |
| 4 | pickUpWhiteCog (through the western gate) | goto-whitecog (teleports inside the cage, 2578,9655), pickup.whitecog | DRIVEN (the pickup itself; the gate is covered by the row above) | T:183-187; Q/cogs.rs2:10-12 |
| 4 | whiteCogOnWhiteSpindle (and climbWhiteLadder) | goto-white-spindle, place.whitecog, quest.stage.no_remaining_cogs | DRIVEN | T:189-197; Q/quest_cog_spindles.rs2:15 |
| 5 | finishQuest: talk to Kojo for the reward | goto-kojo-finish, talkToKojo-finish, talkToKojo-handin-dialog, quest.varp_complete, quest.scroll_title, quest.points, quest.journal, reward.coins | DRIVEN | T:202-220; Q/brother_kojo.rs2:77 |

Setup: `::clearinv` and `::give bucket_water 1` (T:50-51). bucket_water is the guide's own bring-along.

Summary: All four cogs are picked up and placed through the real client, and so are both Kojo talks. The test skips the two route puzzles that gate the blue and white cogs by teleporting past them with goto_tile. The blue cog's secret wall is not pushed. For the white cog, the rat poison is not picked up, the lever is not pulled, the trough is not poisoned and the rat gate is not opened. The content implements all of those legs. The file banner (T:14-20) argues the rat puzzle is "navigation-only" because ~can_pickup_cog (Q/cogs.rs2:71) never reads the rat-door bit. That is true of the pickup, but ctratgatec physically blocks the cage without the bit, so this is still a TEST GAP.

### quest_cook cooks_assistant helper=cooksassistant/CooksAssistant.java

COUNTS guide_steps=9 driven=1 skipped_content=0 skipped_cheat=8 skipped_other=0 verdict=TEST GAP first_gap=getBucket (buy bucket at Lumbridge General Store)

Paths: QH = quest-helper/src/main/java/com/questhelper/helpers/quests/cooksassistant/CooksAssistant.java; T = test/quests/cooks_assistant.lua; Q = OSRS-Content/osrs239-content/server/scripts/quests/quest_cook/scripts/quest_cook.rs2; W = .../scripts/general_use/scripts/windmills.rs2.
Guide: steps.put(0, doQuest) and steps.put(1, doQuest) (QH:213-214); doQuest/getFlour ConditionalSteps at QH:195-211. The ladder-climb steps (climbLadderOne/TwoUp, climbLadderThree/TwoDown, QH:155-171) are travel and are merged into fillHopper and collectFlour. Only coins (QH:126) are a real bring-along. Egg, milk and flour are what the quest asks you to go and get (getEgg/getFlour/milkCow in the ladder), so they are not bring-alongs.
Ledger: selftest/quest_tests/cooks_assistant/ledger.tsv. 25/25 PASS, exit=0.

| varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0/1 | getBucket: buy a bucket from the Lumbridge General Store | none; `::cookbmp_test_give_ingredients` hands over bucket_milk directly | SKIP-CHEAT | T:111; debugproc quest_cook_test_ingredients.rs2:11-20; the shop exists: areas/lumbridge/scripts/tutors.rs2:79 [opnpc3,generalshopkeeper1] |
| 0/1 | getPot: buy a pot from the General Store | same cheat (it gives pot_flour) | SKIP-CHEAT | T:111; tutors.rs2:79 |
| 0/1 | getEgg: pick up an egg at the farm north of Lumbridge | same cheat (it gives egg) | SKIP-CHEAT | T:111; egg ground spawns at areas/world/configs/m49_51.spawn:147-149 |
| 0/1 | getWheat: pick wheat north of Lumbridge | same cheat | SKIP-CHEAT | T:111; general_use/scripts/pickables.rs2:49 [oploc2,_pickable_wheat] |
| 0/1 | fillHopper (and climbing the mill ladder up): use grain on the hopper | same cheat | SKIP-CHEAT | T:111; W:106-112 [oplocu,hopper1] @hopper_fill |
| 0/1 | operateControls: operate the hopper controls | same cheat | SKIP-CHEAT | T:111; W:116 [oploc1,hopperlevers1] @hopper_operate |
| 0/1 | collectFlour (and climbing back down): use the pot on the flour bin | same cheat | SKIP-CHEAT | T:111; W:37,43 [oploc1/oplocu,millbase_flour] |
| 0/1 | milkCow: milk the dairy cow | same cheat | SKIP-CHEAT | T:111; general_use/scripts/dairy_cow.rs2:13,17 [oploc1/oplocu,fat_cow] |
| 0->1, 1->2 | finishQuest: talk to the Cook ("What's wrong?", "Yes"), then hand in egg, milk and flour | cooksassistant.greet, .choose_whats_wrong, .choose_help, .started (varp 1); .handin_talk, .handin_drain, .complete_message, .commit_continue, .commit_settle, quest.varp_complete (varp 2) | DRIVEN | T:68-103, T:139-191; Q:11 [opnpc1,cook], Q:59-64 sets ^cook_started, Q:146-156 commit |

Setup: `::cook` (T:46) resets the quest. Q:193 [debugproc,cook] sets %cookquest = not_started, clears the ingredients and teleports the player. It completes no leg, so it is not a skip.

Summary: The two Cook talks (start and hand-in) are driven through the real client. All eight gathering legs are replaced by the test-only debugproc `::cookbmp_test_give_ingredients` (T:111). The content has every one of those legs: the general store, the egg spawns, wheat, hopper, controls, flour bin and dairy cow. So this is a pure TEST GAP. The test never exercises the windmill chain or the cow. The file's banner justifies giving the items after accepting the quest, but the guide asks the player to obtain them.

### quest_currentaffairs currentaffairs helper=currentaffairs/CurrentAffairs.java

COUNTS guide_steps=20 driven=16 skipped_content=4 skipped_cheat=0 skipped_other=0 verdict=CONTENT GAP first_gap=doAudit (varp 25: answer Catherine's 8 audit questions matching form cr-4p)

Paths: QH=quest-helper/src/main/java/com/questhelper/helpers/quests/currentaffairs/CurrentAffairs.java; T=test/quests/currentaffairs.lua; C=OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/scripts/currentaffairs.rs2. Ledger (OSRS-Content/.../selftest/quest_tests/currentaffairs/ledger.tsv): 66/66 PASS, exit=0.

Setup (T:43-50): ::clearinv; ::currentaffairs (debugproc C:406-413 -- resets varbits + teleport to Arhein, performs no leg); ::give coins 50 (guide bring-along coinsRequirement QH:146,327 -- legit); ::setlevel sailing 22 / fishing 10 and ::complete quest_pandemonium (guide general requirements QH:315-317 -- legit, not legs). Charcoal is a guide bring-along too but the test obtains it in-quest from the cabinet.

| varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | startQuest: talk to Arhein, "What's with the duck?", "Yes." (QH:192) | startQuest, startQuest-dialog (T:78-87) | DRIVEN | C:132-142; ledger 4-6 |
| 5 | talkToCouncillor (QH:196) | talkToCouncillor, -dialog (T:90-96) | DRIVEN | C:221-227; ledger 8-10 |
| 10 | grabCharcoal from cabinet (QH:198) | getCharcoal (click_loc current_affairs_cabinet) (T:99-103) | DRIVEN | C:233-248; ledger 12-13 |
| 10 | fillFormCr4p: use charcoal on form, answer 8 Qs ("answers do not matter") (QH:199) | fillForm use_item_on_item (T:113-116) | DRIVEN (note: the 8-question IF is soft-skipped, all answers set to 1, C:16-25; guide says answers don't matter) | C:250-262; ledger 14-15 |
| 10 | handOverFormCr4p "Yes, I have it here." (QH:212) | handInForm, -dialog (T:119-124) | DRIVEN | C:202-207; ledger 17-18 |
| 10 | talkAfterFormHandedIn (QH:197) | confirmForm, -dialog (T:125-129) | DRIVEN | C:197-200; ledger 19-21 |
| 15 | talkToArhein "I need to find the Mayor of Catherby." (QH:215) | talkToArheinMayor, -dialog (T:132-138) | DRIVEN | C:126-131; ledger 23-25 |
| 20 | talkToHarry, buy kit for 50gp (QH:218; getNewFishbowl is recovery substep) | talkToHarry, -dialog, kit.received (T:141-156) | DRIVEN | C:337-356; harry.rs2:17; ledger 27-29 |
| 20 | fishInAquarium (QH:221) | catchMayor click_loc aquarium (T:159-162) | DRIVEN | C:358-378; ledger 30-31 |
| 20 | showArheinMayor "About the mayor..." (QH:225) | talkToArheinShowMayor, -dialog (T:165-171) | DRIVEN | C:116-121; ledger 33-35 |
| 25 | showCatherineMayor "Yes, I have it here." (QH:227; getNewMayor recovery) | talkToCouncillorAudit, -dialog (T:174-180) | DRIVEN | C:184-190; ledger 37-38 |
| 25 | doAudit: 8 audit questions matched to form answers (QH:233-241) | (none -- same dialogue as above jumps straight to "Audit complete") | SKIP-CONTENT | C:190-193 sets audit_start then %current_affairs=^ca_sign with mes("Soft-skip: audit questions.") C:191; header C:6-7 "Deferred: ... audit dialogue matching" |
| 30 | getForm7r45h (QH:243) | talkToCouncillorGetForm2, -dialog, form2.received (T:184-190) | DRIVEN | C:172-174; ledger 40-42 |
| 30 | signForm7r45h: use form on mayor (QH:244) | signForm use_item_on_item (T:193-196) | DRIVEN | C:281-305; ledger 43-44 |
| 30 | showCatherineForm (QH:245) | talkToCouncillorSigned, -dialog (T:199-204) | DRIVEN | C:157-162; ledger 45-47 |
| 35 | giveArheimNews "The by-law has been changed!" (QH:247; getDuck is recovery) | talkToArheinNews, -dialog, duck.received (T:207-216) | DRIVEN | C:91-100; ledger 49-52 |
| 40 | boardShip (QH:254) | (none) | SKIP-CONTENT | no board leg; opheld1 duck soft-skip C:382-387 mes("Soft-skip: board ship, sail, release duck, follow to shore.") |
| 40 | sailToStart + releaseDuck at island east of Catherby (QH:255-256) | chartCurrents inv_op duck op1 (T:224-229) fires the soft-skip | SKIP-CONTENT | C:384-385 sets %sailing_charting_current_duck_catherby_bay_complete=1 without sailing/release; ^ca_island_coord C-constant unused |
| 40 | followThatDuck + collectDuck (QH:258-260) | (none; chartCurrents.message checks the soft-skip text) | SKIP-CONTENT | C:392-399 has [opnpc1,sailing_charting_current_duck_stopped] but nothing spawns that npc (only defined in npc/configs/npc_anims.generated.npc:37342); completion flag comes from C:385 |
| 40 | showCurrentsArhein "I've charted the currents!" (QH:262) | talkToArheinComplete, -dialog, expect_complete (T:232-239) | DRIVEN | C:71-76 -> ~ca_quest_complete C:27-40; ledger 57-62 |

Summary: The test drives every leg the content implements, with no stage cheats; all gaps are content collapses. The Catherine audit (8 questions matched to form cr-4p answers) is narrated as a soft-skip inside the show-mayor dialogue (C:190-193), and the whole "Map the currents!" section -- board ship, sail to the island, release the duck, follow it, collect it -- is replaced by an opheld1 on the duck that sets the charted flag (C:382-387). Rewards also omit the 1400 Sailing XP (C:31-32 soft-skip mes).

### quest_druid druid helper=druidicritual/DruidicRitual.java

COUNTS guide_steps=9 driven=8 skipped_content=0 skipped_cheat=1 skipped_other=0 verdict=TEST GAP first_gap=enterCauldronRoom (Prison door into the cauldron room, passed by ::goto teleport)

Paths: QH = quest-helper/src/main/java/com/questhelper/helpers/quests/druidicritual/DruidicRitual.java; T = R/test/quests/druid.lua; L = R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/druid/ledger.tsv (SUMMARY pass=27 fail=0; every row PASS); C = R/OSRS-Content/osrs239-content/server/scripts.

Bring-alongs (getItemRequirements, QH:214-222): raw rat meat, raw bear meat, raw beef, raw chicken -- given by ::give at T:26-29. The ladder has no step for obtaining them (only tooltips QH:112-121), so these are legitimate bring-alongs, not skips.

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Kaqemeex, "I'm in search of a quest." / "Yes." (QH:141-143) | goto-talkToKaqemeex, talkToKaqemeex, talkToKaqemeex-dialog-1/-2, expect_stage-after-kaqemeex (T:48-70; L rows 2-6, varp=1) | DRIVEN | C/areas/area_taverly/scripts/kaqemeex.rs2:115 sets ^druid_started |
| 1 | Talk to Sanfew upstairs (goUpToSanfew stairs merged as travel) "I've been sent to help purify..." (QH:144-147) | goto-talkToSanfew (::goto to plane 1), talkToSanfew, talkToSanfew-dialog-1 incl. "Ok, I'll do that then." (T:73-85; L rows 7-9) | DRIVEN | C/areas/area_taverly/scripts/sanfew.rs2:63 sets ^druid_spoken_sanfew (no expect_stage row, but the cauldron gate C/quests/quest_druid/scripts/quest_druid.rs2:99 requires stage 2 and the dips succeeded, L rows 11-14) |
| 2 | Enter Taverley Dungeon (ladder, QH:149-153) + spam-click Prison door into cauldron room (QH:155-157) -- ladder merged as travel into the door step | goto-cauldron: t.player.goto_tile 2893,9831,0 = ::goto teleport from Sanfew's room straight inside the cauldron room (T:88; L row 10) | SKIP-CHEAT | Content has the leg: C/areas/taverly/dungeon/scripts/prison_doors.rs2:8-12 ([oploc1,cauldrondoor]/[oploc1,cauldrondoor_l]) -> :23-43 (suit of armour animates, then walk-through). Door does not gate the stage (no %druidquest test there), so the teleport only bypasses traversal/armour, not progression |
| 2 | Use rat meat on cauldron (QH:159-161) | useRatOnCauldron t.player.use_on raw_rat_meat -> cauldron_of_thunder (T:100; L row 14) | DRIVEN | quest_druid.rs2:120-123 [oplocu,cauldron_of_thunder] |
| 2 | Use beef on cauldron (QH:162-164) | useBeefOnCauldron (T:94; L row 12) | DRIVEN | quest_druid.rs2:124-125 |
| 2 | Use bear meat on cauldron (QH:165-167) | useBearOnCauldron (T:97; L row 13) | DRIVEN | quest_druid.rs2:126-127 |
| 2 | Use chicken on cauldron (QH:168-170) | useChickenOnCauldron (T:91; L row 11) | DRIVEN | quest_druid.rs2:128-129 |
| 2 | Bring enchanted meats to Sanfew upstairs (QH:175-179; stairs merged as travel) | goto-talkToSanfewWithMeat-upper (::goto), talkToSanfewWithMeat, -dialog, expect_stage-given_ingredients (T:103-113; L rows 15-18, varp=3) | DRIVEN | sanfew.rs2:95 sets ^druid_given_ingredients |
| 3 | Return to Kaqemeex to finish (QH:180) | goto-talkToKaqemeexToFinish, talkToKaqemeexToFinish, -dialog, expect_complete, reward.herblore (T:115-145; L rows 19-27, varp=4, +250 herblore) | DRIVEN | kaqemeex.rs2:123-134 -> queue(druid_quest_complete); quest_druid.rs2:18-33 sets ^druid_complete |

Summary: Every stage-changing leg (both Kaqemeex talks, both Sanfew talks, all four cauldron dips) is driven through real opnpc/oplocu interactions and the ledger is 27/27 PASS. The only gap is the Taverley Dungeon descent plus the Prison door (cauldrondoor) crossing, which the test replaces with a ::goto teleport into the cauldron room (T:88) even though the content implements the door and its animated-armour trap (prison_doors.rs2:8-43); it is not a progression gate, so the gap is traversal fidelity only. No setvar/::complete/debugproc cheats are used; the four raw meats are legitimate bring-alongs.

### quest_drunkmonk drunkmonk helper=monksfriend/MonksFriend.java

COUNTS guide_steps=10 driven=9 skipped_content=0 skipped_cheat=1 skipped_other=0 verdict=TEST GAP first_gap=goUpLadder (climb the cellar ladder out of the blanket cave; test teleports with goto_tile)

Ledger: OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/drunkmonk/ledger.tsv -- 42/42 PASS (SUMMARY pass=42 fail=0).
Setup (drunkmonk.lua:19-23): ::clearinv, ::give jug_water 1, ::give logs 1. Both are guide bring-alongs (MonksFriend.java:157-161 getItemRequirements = jugOfWater, log); legitimate.
Traversal: every "goto-*" row is t.player.goto_tile, which is a teleport (test/quests/_conformance.lua:134,266).

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | talkToOmad: talk to Brother Omad | talkToOmad + talkToOmad-dialog (choose "Why can't you sleep", "Can I help at all?"); quest.stage.spoken_to_omad | DRIVEN | MonksFriend.java:102,137; drunkmonk.lua:49-65; brother_omad.rs2:53 sets spoken_to_omad (and settimer blanket_ladder :54) |
| 10 | goDownLadder: go down ladder in the stone circle | goto-blanketLadder (teleport next to the spot), blanketLadder.spawned, goDownLadder (click_loc wildymirrorladdertop1 op1) | DRIVEN (approach by teleport, climb clicked) | MonksFriend.java:106,139; drunkmonk.lua:70-83; quest_drunkmonk.rs2:7-10 loc_add; lava_maze.rs2:57-63 oploc1 -> p_telejump(0_40_150_1_21) |
| 10 | grabBlanket: pick up Child's blanket | goto-blanketLocation, takeBlanket (click_obj childs_blanket, count 0->1), blanket.expect_has | DRIVEN | MonksFriend.java:107,142; drunkmonk.lua:88-100; m40_150.spawn:86 childs_blanket 2570 9604 |
| 10 | goUpLadder: go back up the ladder | goto-returnToOmadWithBlanket (goto_tile 2604,3209,0 teleports from the cave straight to Omad) | SKIP-CHEAT | MonksFriend.java:108,140 (ObjectID.LADDER_FROM_CELLAR @2561,9622); drunkmonk.lua:102-105 (comment claims no climb-up is bound); content DOES have the generic handler: ladders.loc:1435-1436 [ladder_from_cellar] category=climb_up_ladder, ladders.rs2:199 [oploc1,_climb_up_ladder] ~climb_ladder(1). Not stage-gating -- a travel leg, but the test's stated reason is wrong. (Loc name at 2561,9622 taken from QH's id; map not decoded.) |
| 10 | returnToOmadWithBlanket: bring blanket to Omad | returnToOmadWithBlanket + -dialog; quest.stage.retrieved_blanket; blanket.expect_consumed | DRIVEN | MonksFriend.java:109,141; drunkmonk.lua:107-133; brother_omad.rs2:57-67 (inv_total childs_blanket >=1 -> retrieved_blanket) |
| 20 | talkToOmadAgain: talk to Omad again | askAboutCedric + -dialog (choose "Who's Brother Cedric?", "Where should I look?"); quest.stage.looking_cedric | DRIVEN | MonksFriend.java:111,146; drunkmonk.lua:136-155; brother_omad.rs2:108 |
| 30 | talkToCedric: talk to Brother Cedric | goto-talkToCedric, talkToCedric + -dialog; quest.stage.finding_water | DRIVEN | MonksFriend.java:116,147; drunkmonk.lua:158-167; brother_cedric.rs2:40 |
| 40 | talkToCedricWithJug: give Cedric the jug of water | talkToCedricWithJug + -dialog (mesbox "You hand the monk a jug of water"); water.expect_consumed | DRIVEN | MonksFriend.java:118,148; drunkmonk.lua:171-199; brother_cedric.rs2:52 given_water |
| 50 | continueTalkingToCedric: talk to Cedric again (agree to fix cart) | same dialogue session: choose "Yes, I'd be happy to!"; quest.stage.fixing_cart | DRIVEN | MonksFriend.java:120,149; drunkmonk.lua:182-187; brother_cedric.rs2:67 fixing_cart |
| 60 | talkToCedricWithLog: bring Cedric logs | talkToCedricWithLog + -dialog ("You show Cedric some logs."); quest.stage.fixed_cart; logs.expect_consumed | DRIVEN | MonksFriend.java:124,150; drunkmonk.lua:203-223; brother_cedric.rs2:94 fixed_cart |
| 70 | finishQuest: return to Omad | goto-finishQuest, finishQuest + -dialog; quest.varp_complete (80), scroll, points, reward.woodcutting +2000, reward.lawrune +8 | DRIVEN | MonksFriend.java:126,151; drunkmonk.lua:234-257; brother_omad.rs2:139 @drunkmonk_party; ledger rows 37-42 PASS |

Summary: every quest-stage leg is played through real clicks and dialogue, and all 42 ledger rows pass. The only skipped leg is the climb back up out of the blanket cave. The test replaces it with a goto_tile teleport, saying no climb-up is bound (drunkmonk.lua:102-104). That reason is wrong: the content binds cellar ladders generically through category climb_up_ladder (ladders.rs2:199). The leg does not change the stage, so this is a small traversal gap rather than a skipped quest leg.

### quest_eadgar eadgar helper=eadgarsruse/EadgarsRuse.java

COUNTS guide_steps=27 driven=24 skipped_content=0 skipped_cheat=2 skipped_other=1 verdict=TEST GAP first_gap=get Berry's cell key + unlock Eadgar's cell (::setvar troll_freed_eadgar 1)

Paths: guide = QH/src/main/java/com/questhelper/helpers/quests/eadgarsruse/EadgarsRuse.java (EadgarsRuse.java); test = R/test/quests/eadgar.lua; ledger = R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/eadgar/ledger.tsv (SUMMARY line 120: pass=117 fail=0, every row PASS, including thistle.dried at line 84; the QUEUE note about a failing thistle.not_dried row is stale). Content: quest_eadgar/scripts/*.rs2, plus quest_troll/scripts/troll_eadgar.rs2 (Eadgar's state machine), quest_troll/scripts/quest_troll.rs2 (cell doors), areas/area_taverly/scripts/sanfew.rs2 (offer and turn-in), skill_cooking/scripts/cooking.rs2:39-40 (thistle drying).

Bring-alongs (getItemRequirements, EadgarsRuse.java:535-548): climbing boots or 12 coins, vodka, pineapple chunks, 2 logs, 10 grain, 5 raw chicken, tinderbox, pestle and mortar, ranarr potion (unf). The test ::gives all of these except the boots (eadgar.lua:35-42), which is legitimate. The dirty robe is quest-obtained and is talked out of Tegid for real. Walking steps are merged into the action they lead to (Tenzing boots purchase, stile, climbing rocks, secret entrance, stronghold stairs and doors, cave entrance and exit). The test's goto_tile is a teleport (test/quests/_conformance.lua:134, 266), so it never needs or equips climbing boots.

Other setup: ::setlevel herblore 31 (a quest requirement), ::setlevel firemaking 99 (makes the fire-lighting roll reliable; not a quest leg), ::complete quest_druidicritual (prerequisite quest). Troll Stronghold (also a prerequisite) is not completed. Only its troll_freed_eadgar flag is set (eadgar.lua:34).

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Sanfew upstairs; "Have you any more work...", accept (EadgarsRuse.java:359-365) | goto-sanfew-1, talkToSanfew-accept, talkToSanfew-accept-dialog (eadgar.lua:77-92); ledger l.6 PASS, quest.stage.started l.8 (varp 10) | DRIVEN | sanfew.rs2:161-173 (%eadgar_quest=^eadgar_started) |
| 10 (only if not freed) | Pickpocket or kill Berry for cell key 2 (EadgarsRuse.java:380-387, panel :101-103) | none. `::setvar troll_freed_eadgar 1` (eadgar.lua:34) | SKIP-CHEAT | content has the leg: troll_stronghold_camp_guard.rs2:25 (steal troll_key_eadgar from troll_prison_guard2), mountain_troll.rs2:35-36 (key drop while not freed); sanfew.rs2:157-160 refuses the quest until freed |
| 10 (only if not freed) | Unlock Eadgar's cell (EadgarsRuse.java:389) | none. Same ::setvar (eadgar.lua:34) | SKIP-CHEAT | quest_troll.rs2:237-241 ([oploc1]/[oplocu,troll_celldoor_eadgar] -> @troll_unlock_cell_2), :277-292 (%troll_freed_eadgar=^true, key deleted) |
| 10 | Travel to Eadgar's cave (boots, stile, rocks, secret door, stairs) and talk: "I need to find some goutweed" (EadgarsRuse.java:369-400) | goto-eadgar-1 (teleport), talkToEadgar-askGoutweed(+dialog) (eadgar.lua:99-107); ledger l.10 PASS, stage l.12 (15) | DRIVEN (travel merged) | troll_eadgar.rs2:68-69 -> :166-175 (%eadgar_quest=^eadgar_spoken_eadgar_first) |
| 15 | Talk to Burntmeat (EadgarsRuse.java:405) | goto-burntmeat-1, talkToBurntmeat-1(+dialog) (eadgar.lua:115-129); ledger l.14, stage l.16 (25) | DRIVEN | eadgar_troll_chief_cook.rs2:75-84 |
| 25 | Return to Eadgar (EadgarsRuse.java:413) | goto-eadgar-2, talkToEadgar-explainParrot(+dialog) (eadgar.lua:135-145); ledger l.18, stage l.20 (30) | DRIVEN | troll_eadgar.rs2:181-188 (^eadgar_needs_parrot) |
| 30 | Talk to Parroty Pete and ask Q2 "When did you add it?" and Q3 "What do you feed them?" (EadgarsRuse.java:416-425) | talkToPete + dialog, but it picks "It's very nice." (eadgar.lua:153-160); ledger l.22-23 | SKIP-OTHER | the content has both branches (eadgar_zoo_keeper_aviary.rs2:20, :33-39), but nothing is gated on them. alco-chunks (:42-50) and the hatch (:53-74) only check items and stage. The test talks to Pete but never asks the guide's questions |
| 30 | Use vodka on pineapple chunks (EadgarsRuse.java:427) | makeAlcoChunks (eadgar.lua:161-162); ledger l.24 | DRIVEN | eadgar_zoo_keeper_aviary.rs2:42-50 |
| 30 | Use alco-chunks on the aviary hatch (EadgarsRuse.java:431) | lookup.hatch, catchParrot (eadgar.lua:173-179); ledger l.25-26 | DRIVEN | eadgar_zoo_keeper_aviary.rs2:53-74 |
| 30 | Return the parrot to Eadgar (EadgarsRuse.java:434-437) | goto-eadgar-3, giveParrotToEadgar(+dialog) (use parrot on Eadgar; eadgar.lua:185-195); ledger l.28, stage l.30 (50) | DRIVEN | troll_eadgar.rs2:123-124 -> :190-200 |
| 50 | Use the parrot on the prison rack (EadgarsRuse.java:448) | goto-rack-1, hideParrot (eadgar.lua:197-201); ledger l.32, stage l.33 (60) | DRIVEN | eadgar_troll_chief_cook.rs2:165-176 |
| 60/70 | Talk to Tegid for a dirty robe ("Sanfew won't be happy...") (EadgarsRuse.java:453-454) | goto-tegid, talkToTegid-robe(+dialog), inv.await robe (eadgar.lua:221-231); ledger l.39 | DRIVEN | eadgar_druid_washing.rs2:7-35 |
| 60/70 | Bring Eadgar robe, logs, 5 chicken, 10 grain (EadgarsRuse.java:455-458) | goto-eadgar-4, talkToEadgar-explainItems (60->70); giveLogs, giveRobe, giveChicken1-5, giveGrain1-10 (eadgar.lua:206-290); ledger l.35-74, stages l.37 (70), l.76 (80) | DRIVEN | troll_eadgar.rs2:152/206-212 (->70), :82-122 deliveries, :137-140 (->80) |
| 80 | Pick a troll thistle (EadgarsRuse.java:463) | goto-thistle, pickThistle (eadgar.lua:296-299); ledger l.78 | DRIVEN | eadgar_troll_thistle.rs2:14-17 |
| 80 | Light a fire (EadgarsRuse.java:466) | goto-fire, lightFire, fire.lit (eadgar.lua:309-313); ledger l.80-81 | DRIVEN | firemaking (the player lights a real `fire` loc) |
| 80 | Use the thistle on the fire (own fire or the troll camp fire, one alternative; EadgarsRuse.java:468-474) | lookup.fire, dryThistle, thistle.dried (eadgar.lua:321-344); ledger l.82-84 | DRIVEN | cooking.rs2:39-40 -> eadgar_troll_thistle.rs2:60-63. The troll-camp-fire alternative is not wired, per the test's note at eadgar.lua:301-308 |
| 80 | Pestle and mortar on the dried thistle (EadgarsRuse.java:476) | grindThistle (eadgar.lua:351-353); ledger l.85 | DRIVEN | eadgar_troll_thistle.rs2:44-48 |
| 80 | Ground thistle on the ranarr (unf) (EadgarsRuse.java:478) | mixPotion (eadgar.lua:355-357); ledger l.86 | DRIVEN | eadgar_troll_thistle.rs2:53-57, :65-69 |
| 80 | Bring Eadgar the troll potion (EadgarsRuse.java:480-482) | goto-eadgar-6, givePotion(+dialog) (eadgar.lua:362-370); ledger l.88, stage l.90 (85) | DRIVEN | troll_eadgar.rs2:127-128 -> :218-227 |
| 85 | Get the parrot from the prison rack (EadgarsRuse.java:493) | goto-rack-2, fetchParrot(+dialog) (eadgar.lua:376-383); ledger l.92, stage l.94 (86) | DRIVEN | eadgar_troll_chief_cook.rs2:178-189 |
| 86 | Return to Eadgar with the parrot (EadgarsRuse.java:505) | goto-eadgar-7, makeFakeMan(+dialog) (eadgar.lua:389-399); ledger l.96, stage l.98 (87) | DRIVEN | troll_eadgar.rs2:131-132 -> :233-245 |
| 87 | Take the fake man to Burntmeat (EadgarsRuse.java:512) | goto-burntmeat-2, giveFakeMan(+dialog) (eadgar.lua:407-427); ledger l.100, stage l.102 (90) | DRIVEN | eadgar_troll_chief_cook.rs2:40, :57-62, :91 |
| 90 | Ask Burntmeat "So, where can I get some goutweed?" (EadgarsRuse.java:514-515) | the same giveFakeMan-dialog chooses it (eadgar.lua:419-423) | DRIVEN | eadgar_troll_chief_cook.rs2:100 (burntmeat_where_goutweed) |
| 90 | Search the kitchen drawers (EadgarsRuse.java:519-520) | goto-drawers, openDrawers, searchDrawers, inv.await key (eadgar.lua:439-443); ledger l.104-105 | DRIVEN | eadgar_troll_chief_cook.rs2:120-147 |
| 90 | Enter the storeroom with the key (EadgarsRuse.java:524) | goto-storeroomdoor, unlockStoreroom (eadgar.lua:445-447); ledger l.107, stage l.108 (100) | DRIVEN | eadgar_troll_chief_cook.rs2:149-159 |
| 100 | Search the goutweed crates while avoiding the guards (EadgarsRuse.java:526) | goto-crate, searchCrate (eadgar.lua:449-451); ledger l.110 | DRIVEN (note) | eadgar_troll_chief_cook.rs2:193-218. The port replaces the guard-avoidance stealth with a random(3) swipe roll and always grants goutweed |
| 100 | Return the goutweed to Sanfew (EadgarsRuse.java:528-531) | goto-sanfew-2, talkToSanfew-turnin(+dialog), quest.expect_complete, reward.herblore_xp (eadgar.lua:464-479); ledger l.113-119 (varp 110, +11000 herblore) | DRIVEN | sanfew.rs2:176-190 |

Summary: 24 of the 27 guide steps are driven through real clicks and dialogue, and the ledger passes all 117 rows. The main gap is freeing Eadgar from the Troll Stronghold prison (Berry's key and the cell door). The content implements it (quest_troll.rs2:237-292, troll_stronghold_camp_guard.rs2:25), and Sanfew gates the quest offer on it (sanfew.rs2:157), but the test sets `::setvar troll_freed_eadgar 1` instead (eadgar.lua:34). The guide shows this leg only when the player did not free Eadgar during Troll Stronghold, and the test completes no Troll Stronghold state other than this flag. Smaller gap: Parroty Pete is talked to, but the guide's two questions are never chosen. The content does not gate on them, so the omission is cheap, but the questions go unexercised. Content notes, not skips: the goutweed crate's guard stealth is simplified to a random swipe, the troll camp fire does not dry the thistle (the test uses a player-lit fire), and the whole climbing-boots route is bypassed because goto_tile teleports.

### quest_elemental_workshop elemental_workshop helper=elementalworkshopi/ElementalWorkshopI.java

COUNTS guide_steps=18 driven=17 skipped_content=0 skipped_cheat=1 skipped_other=0 verdict=TEST GAP first_gap=goDownStairs (climb down the spiral staircase behind the odd wall; test teleports with goto_tile/::goto)

Notes on the guide: ElementalWorkshopI is a ComplexStateQuestHelper with no steps.put() ladder. Its "values" are
varbit/zone conditions (ElementalWorkshopI.java:89-131); the ordered leaf list is the panel list at
ElementalWorkshopI.java:346-348 (18 steps). searchLeatherCrate/searchNeedleCrate are fixBellows sub-steps
(java:195) that only matter when the needle/leather bring-alongs are not carried; not counted.
Bring-alongs (java:298): knife, pickaxe, needle, thread, leather, hammer, 4 coal, all given in setup
(elemental_workshop.lua:32-38) and none is obtained inside the guide's ladder, so these are legitimate. The
rune scimitar and ::setlevel rows (lua:39-46) cover combatGear (java:304) and the 20 Mining/Smithing/Crafting
requirements (java:357-359); they are not quest steps.
The test file has NO t.blocked rows (the bookcase / key-making leg the QUEUE row's old rejection mentioned is
now driven). Ledger: selftest/quest_tests/elemental_workshop/ledger.tsv, SUMMARY 57 PASS, fail=0.

| guide condition | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| default (goReadBook) | searchBookcase: search marked bookcase, Seers' | goto-searchBookcase + searchBookcase + gotBook (lua:77-84) | DRIVEN | ledger rows 4-6 PASS; content rs2:10-42 grants the book |
| batteredBook | readBook: read the battered book | readBook + readBook.dismiss (lua:96-97); pickBookBackUp/gotBookBack (lua:100-107) pick the book up again after op1's shift-drop side effect | DRIVEN | ledger rows 7-10 PASS; shield_book.rs2:10-17 sets %elemental_workshop_book |
| hasReadBook & batteredBook | useKnifeOnBook: knife on the battered book | cutSpine + gotKey (lua:111-116) | DRIVEN | ledger row 11: "You make a small cut in the spine of the book." gained key + slashed book; shield_book.rs2:19-34 |
| batteredKey | openOddWall: open the odd wall N of bookcase | goto-openOddWall + openOddWall (lua:124-137) | DRIVEN (weak evidence) | ledger row 14 PASS on `ok map_flag`, tile 2709,3495 -> 2709,3495 (after walk_near stepped to 3494): the press landed and elem1_walk_wall ran (rs2:178-223), but nothing shows the player reached the stairwell zone (java:291), and the next row teleports anyway |
| inStairwell | goDownStairs: climb down the staircase (2711,3498) | none: goto-turnEastControl `t.player.goto_tile(2726, 9908, 0)` (lua:143) is a ::goto teleport (docs/QUEST_SERVER_CHEATS.md:92; QUEST_AUTHORING.md:161) | SKIP-CHEAT | content has the leg: [oploc1,elemental_workshop_spiralstairstop] rs2:225-227 teleports and sets %elemental_workshop_stairs = 1 (read only by the journal, elemental_workshop_journal.rs2:23), so no later gate notices it was skipped |
| inWorkshop | turnEastControl: east water control, north room | turnEastControl (lua:145) | DRIVEN | ledger row 16 PASS chat_message; rs2:48-61 (valve_1 multivarbit gate2, all.loc:29517) |
| turnedValve1 | turnWestControl: the other water control | turnWestControl (lua:150) | DRIVEN | ledger row 18 PASS; rs2:63 |
| turnedValve1+2 | pullLever: lever in north room | pullLever (lua:155) | DRIVEN | ledger row 19 PASS; rs2:65-81 sets %elemental_workshop_switch |
| solvedWater (needle, leather) | fixBellows: repair the bellows, east room | goto-fixBellows + fixBellows (lua:184-187) | DRIVEN | ledger row 25 PASS; rs2:83-99 |
| fixedBellow | pullBellowsLever: lever next to the bellows | foundAirLever + goto + pullBellowsLever (lua:188-195) | DRIVEN | ledger rows 26-28 PASS; rs2:101-114 sets bellows_switch |
| solvedWater+Air | getStoneBowl: search the NE boxes in the central room | foundBox1 + goto-getStoneBowl + getStoneBowl + gotBowl (lua:165-176) | DRIVEN | guide names box_4 (java:198) but this pack puts the bowl in box_1 (rs2:118-127); box_4 is the leather fallback (rs2:142-152); ledger rows 20-23 PASS |
| stoneBowl | useBowlOnLava: stone bowl on lava trough, south room | foundTrough + useBowlOnLava + gotFullBowl (lua:200-210) | DRIVEN | ledger rows 30-32 PASS; rs2:161-171 |
| lavaBowl | useLavaOnFurnace: lava bowl on furnace | foundFurnace + useLavaOnFurnace + gotEmptyBowl (lua:212-220) | DRIVEN | ledger rows 33-35 PASS; elem2_gather.rs2:44-48 -> rs2:232-242 sets %elemental_workshop_fire |
| solved all, inWorkshop | mineRock: mine an elemental rock, west room | goto-mineRock + mineRock + mineRock.dismiss (lua:225-228) | DRIVEN | ledger rows 36-38 PASS; elem2_gather.rs2:18-35 npc_add at :30 |
| earthNearby | killRock: kill the rock elemental | elementalPresent + attackElemental + killElemental (lua:237-243) | DRIVEN | ledger rows 39-41: real fight, "dead after 41 tick(s), 0 re-engagement(s)"; no ::kill |
| elementalOreNearby | pickUpOre: pick up the elemental ore | foundOre + takeOre + gotOre (lua:250-264) | DRIVEN | ledger rows 42-44 PASS; elemental_drops.rs2:105-110 |
| elementalOre | forgeBar: ore on the furnace | goto-forgeBar + forgeBar + gotBar (lua:269-277) | DRIVEN | ledger rows 45-47 PASS; rs2:244-270 |
| elementalBar | smithShield: bar on a workbench, central room | goto-smithShield + smithShield + expect_complete (lua:282-294) | DRIVEN | ledger rows 50-54 PASS; elem2_helm.rs2:62-69 -> rs2:272-296 completes the quest |

Summary: 17 of 18 guide steps are driven through the real client, the fight included. The one gap is the
staircase: the test reaches the workshop by a ::goto teleport (lua:143) rather than clicking
elemental_workshop_spiralstairstop (rs2:225-227), so %elemental_workshop_stairs is never set in the run (only
the journal reads it). A related weakness: the openOddWall row passes on `ok map_flag` with no proof that the
player went through the wall into the stairwell, because the next goto_tile would hide that failure.
Side finding (not a coverage gap): elem2_gather.rs2:37-41 comments that elemental_workshop_fire is "never set
anywhere in this tree". That comment is stale, because rs2:240 sets it and this run's useLavaOnFurnace row set it.

### quest_fishingcompo fishingcompo helper=fishingcontest/FishingContest.java

COUNTS guide_steps=9 driven=6 skipped_content=0 skipped_cheat=3 skipped_other=0 verdict=TEST GAP first_gap=getGarlic (Seers' table garlic given by ::give)

Paths: C = OSRS-Content/osrs239-content/server/scripts. Ledger: SUMMARY 48 PASS fail=0 (read at selftest/quest_tests/fishingcompo/ledger.tsv; OSRS-Content has since staged a move to selftest/quests/quest_fishingcompo/play/ledger.tsv, same SUMMARY).
Bring-alongs (FishingContest.java:287-290 getItemRequirements: coins, redVineWorm, garlic, spade, fishingRod). Coins (::give coins 10, fishingcompo.lua:45) and fishing rod (fishingcompo.lua:44) are treated as legitimate bring-alongs: the rod's Grandpa Jack leg (grandpaJack/runToJack/teleToHemenster, FishingContest.java:167-190) is a 5gp purchase alternative only reached when no rod is held, so not counted as a step. Garlic and the red vine worm are NOT: the guide ladder has its own steps to obtain them (getGarlic FishingContest.java:145-147; getWorms/goToMcGruborWood/goToRedVine :149-157, :200-202; both tooltips "This can be obtained during the quest", :112, :118), and the content implements both legs. The test's header claim (fishingcompo.lua:15-20: "neither kr_seers_table2 nor red_worm_junction has a single [oploc*] handler") is wrong for this pack: see rows below. ::setlevel fishing 10 (lua:46) is the guide's general requirement (FishingContest.java:282), legitimate.

| varp | guide step | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Vestri north of Catherby (stairs / Why not? / friend / let's be friends / how / Yes) | goto-dwarf-start, dwarf.greet, dwarf.choose_* x6, dwarf.drain_accept_close, quest.stage.started, dwarf.pass_granted (ledger 3-19 PASS) | DRIVEN | fishingcompo.lua:75-109; C/areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2:91,116-128 |
| 1 | Pick the garlic up on the table in Seers' Village | `::give garlic 1` | SKIP-CHEAT | fishingcompo.lua:43; content has the leg as a ground spawn: C/areas/world/configs/m42_54.spawn:45 `garlic 2714 3478 0` (the guide's table tile, FishingContest.java:145) |
| 1 | Gather 1 red vine worm in McGrubor's Woods (enter via loose railing, spade on red vines) | `::give red_vine_worm 1` | SKIP-CHEAT | fishingcompo.lua:42; content implements it: C/areas/area_seers/scripts/mcgrubors_wood.rs2:29-39 ([oploc1,mcgruborlooserailing]), :41-65 ([oploc1/oplocu,_red_vine] -> inv_add red_vine_worm), category note :7 |
| 1 | Enter Hemenster with your fishing pass (gate) | goto-bonzo-pay teleports past the gate (comment lua:113-115 "a straight teleport, no click_loc on the gate") | SKIP-CHEAT | fishingcompo.lua:116; content gate: C/quests/quest_fishingcompo/scripts/quest_fishingcompo_gate.rs2:44-58 ([oploc1,fishinggateclosedr] -> ~fishingcompo_gate_admit), :64-74 (pass check, sets %fishingcompo_passed=1) |
| 1 | Speak to Bonzo to enter the competition (5gp) | bonzo.greet, bonzo.choose_enter, quest.stage.in_comp, bonzo.fee_paid (ledger 21-26 PASS) | DRIVEN | fishingcompo.lua:117-134; C/areas/area_seers/scripts/hemenster/bonzo.rs2:86 |
| 2 | Put garlic in the pipes | garlicpipe.lookup, goto-garlicpipe, garlicpipe.stash (use_on), garlicpipe.stranger_reacts, quest.stage.garlic_comp (ledger 27-31 PASS) | DRIVEN | fishingcompo.lua:155-175; quest_fishingcompo_gate.rs2:4,14,31 |
| 3 | Catch the winning fish at the spot near the pipes | goto-fishspot, fish.carp, fish.carp_landed (ledger 32-34 PASS) | DRIVEN | fishingcompo.lua:187-191; C/quests/quest_fishingcompo/scripts/hemenster_fishing.rs2:8,47-49 |
| 3 | Speak to Bonzo with the fish | bonzo.howdoing, bonzo.accept_carp, quest.stage.won_comp, bonzo.trophy_granted (ledger 36-39 PASS) | DRIVEN | fishingcompo.lua:200-215; bonzo.rs2:105-107 |
| 4 | Bring Vestri the trophy | dwarf.trophy_handin, dwarf.won_dialogue, quest.varp_complete/scroll/points/journal, reward.fishing (ledger 42-48 PASS) | DRIVEN | fishingcompo.lua:230-254; mountain_dwarf.rs2:170-177 |

Summary: The dialogue/competition core (Vestri, Bonzo, garlic-in-pipe use_on, live carp catch, trophy hand-in) is fully driven. The gaps are the item-gathering legs the test justified as "not wired in this pack" -- but the pack does wire them: the Seers' garlic spawn (m42_54.spawn:45) and the McGrubor's railing + red-vine dig (mcgrubors_wood.rs2:29-65) exist, so both ::give lines are test cheats; the Hemenster gate's pass check is also bypassed by a goto teleport.

### quest_golem golem helper=thegolem/TheGolem.java

COUNTS guide_steps=22 driven=18 skipped_content=4 skipped_cheat=0 skipped_other=0 verdict=CONTENT GAP first_gap=talkToElissa (no Elissa trigger in the pack)

Paths: C = OSRS-Content/osrs239-content/server/scripts; G = C/quests/quest_golem/scripts/golem.rs2; P = C/quests/quest_golem/scripts/golem_portal.rs2.
Ledger: the brief's path selftest/quest_tests/golem/ledger.tsv no longer exists -- OSRS-Content has an in-progress (staged) move quest_tests -> selftest/quests/; the ledger is now C/selftest/quests/quest_golem/play/ledger.tsv: SUMMARY 86 PASS fail=0.
Bring-alongs (TheGolem.java getItemRequirements: 4 soft clay, vial, pestle and mortar, papyrus) -- ::give at golem.lua:23-26 legitimate. ::give hammer (lua:27) is a tool the port's throne-gem path needs (P:218-227), not a quest-obtained item. ::setlevel crafting 20 / thieving 25 (lua:28-29) are the quest's skill requirements. ::golem (lua:22) is a reset + teleport debugproc (G:136-140), no stage skip. Pure travel (enterRuinForFirstTime/enterRuin/enterRuinWithoutStatuette via ::goto over the maplink stairs, leaveThroneRoom, leaveRuin, goUpInMuseum) merged, not counted.

| varp | guide step | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to the Golem in Uzer | goto-golem-1, talkToGolem-offer(-dialog), quest.stage.offered (ledger 3-6 PASS) | DRIVEN | golem.lua:57-65; G:49-62 |
| 1 | Use 4 soft clay on the Golem | repairClay1..4 (+continue, clay-var), quest.stage.repaired (ledger 7-22 PASS) | DRIVEN | golem.lua:84-117; P:233-237,277-299 |
| 2 | Pick up the letter | goto-letter, pickUpLetter (ledger 26-27 PASS) | DRIVEN | golem.lua:136-145; C/areas/world/configs/m54_48.spawn:19 |
| 2 | Read the letter | readLetter (ledger 28 PASS) | DRIVEN | golem.lua:147; P:18-20 |
| 2 | Pick up the strange implement in the ruin (golem_golemkey) | prizeGems -- key obtained from the throne with a hammer instead of the ground item (ledger 65-69 PASS) | DRIVEN (alternate content path) | golem.lua:326-346; P:217-227; ground spawn also exists C/areas/world/configs/m42_76.spawn:17 |
| 2 | Talk to Elissa at the Digsite | not done | SKIP-CONTENT | Elissa is spawned (C/areas/world/configs/m52_53.spawn:22) but no [opnpc*,golem_elissa] trigger exists anywhere in C; the bookcase that follows is ungated (P:26-37) |
| 2 | Search the bookcase in the Exam Centre | goto-bookcase, searchBookcase(-continue/-sync) (ledger 46-49 PASS) | DRIVEN | golem.lua:204-211; P:26-37 |
| 2 | Read Varmen's notes | readNotes (ledger 50 PASS) | DRIVEN | golem.lua:212; P:22-24 |
| 2 | Talk to Curator Haig | not done | SKIP-CONTENT | C/areas/varrock/scripts/curator.rs2 has no golem branch in [opnpc1,curator] (:8); the pickpocket is gated only on %golem_a>=tasked (curator.rs2:65) |
| 3 | Pickpocket Curator Haig for the display cabinet key | goto-curator, pickpocketCurator(-continue/-sync) (ledger 40-43 PASS) | DRIVEN | golem.lua:180-187; curator.rs2:64-68 |
| 3 | Go upstairs in the museum and open the golem statue's display case | useKeyOnCase -- item-on-item stand-in (papyrus on key) (ledger 44-45 PASS) | SKIP-CONTENT (port collapses the display-case loc into [opheldu,golem_statuettekey]) | golem.lua:188-199; P:52-62 (no oploc on vm_timeline_terracotta_statue; header P:3 "museum case IF stub") |
| 3 | Use the statuette on the empty alcove | goto-ruin, world.statuette-alcove, placeStatuette, quest.stage.portal_open, placeStatuette-consumed (ledger 54-60 PASS) | DRIVEN | golem.lua:227-275; P:67-88 |
| 4 | Turn each of the four statuettes to face the door | not done | SKIP-CONTENT | P:3 "Soft: skip full statuette-rotation puzzle"; P:66,88 placing the statuette sets %golem_a=^golem_portal_open directly |
| 5 | Enter the portal | world.demon-door, enterPortal, enterPortal-arrived, enterPortal-seen (ledger 61-64 PASS) | DRIVEN | golem.lua:286-315; P:93-100 |
| 6 | Return and talk to the Golem | goto-golem-2, talkToGolem-demondead(-dialog), quest.stage.need_program (ledger 70-73 PASS) | DRIVEN | golem.lua:352-361; G:87-94 |
| 7 | Pick up black mushrooms | goto-mushroom, pickMushroom, pickMushroom-sync (ledger 31-33 PASS) | DRIVEN | golem.lua:157-163 (shared [oploc1,golem_black_mushrooms], P:11) |
| 7 | Grind the mushroom into a vial | grindMushroom(-continue/-sync) (ledger 34-36 PASS) | DRIVEN | golem.lua:164-168; P:120-132 |
| 7 | Steal a feather from the desert phoenix | goto-phoenix, stealFeather (ledger 29-30 PASS) | DRIVEN | golem.lua:151-152; P:39-48 |
| 7 | Use the phoenix feather on the ink | dipFeather(-continue/-sync) (ledger 37-39 PASS) | DRIVEN | golem.lua:171-175; P:138-141 |
| 7 | Use the phoenix quill on the papyrus | writeProgram(-continue/-sync) (ledger 51-53 PASS) | DRIVEN | golem.lua:216-220; P:147-154 |
| 7 | Use the strange implement on the Golem | insertKey, quest.stage.head_open (ledger 74-76 PASS) | DRIVEN | golem.lua:365-368; P:241-257 |
| 7 | Use the golem program on the Golem | handInProgram(-dialog), quest.varp_complete/scroll/points/journal, reward.crafting/thieving (ledger 78-86 PASS) | DRIVEN | golem.lua:379-391; P:259-270 |

Summary: Every leg the port implements is driven with no stage cheat; the four skips are all content collapses in this "soft" port -- no Elissa or Curator-talk trigger, the museum display case reduced to an item-on-item stub, and the four-statuette rotation puzzle replaced by a single placement that opens the portal (P:3,66-88). Order differs from the guide (program ingredients gathered at stage 3 rather than 7), which the content allows.

### quest_haunted haunted helper=ernestthechicken/ErnestTheChicken.java

COUNTS guide_steps=15 driven=10 skipped_content=0 skipped_cheat=5 skipped_other=0 verdict=TEST GAP first_gap=pickupSpade (spade/compost/closet-key chain bypassed by ::goto into the closet)

Paths: C = OSRS-Content/osrs239-content/server/scripts; Q = C/quests/quest_haunted/scripts/quest_haunted.rs2. Ledger: SUMMARY 36 PASS fail=0 (read at selftest/quest_tests/haunted/ledger.tsv; OSRS-Content has since staged a move to selftest/quests/quest_haunted/play/ledger.tsv, same SUMMARY).
No getItemRequirements in the helper; the test gives nothing (setup is ::clearinv + ::haunted, a reset debugproc, haunted.lua:54-55). t.player.goto_tile is the ::goto teleport cheat (script/plugins/quest_driver/pointer.lua:1798; docs/QUEST_SERVER_CHEATS.md:92). Pure travel (stairs, enterManorWithKey, goUpFromBasement, pullLeverToLeave, goToFirstFloor/SecondFloor) is merged; but ::goto used to pass a locked door or a puzzle is counted as a cheat. The file header (haunted.lua:9-27) openly calls the closet key chain and the six-lever maze "walking obstacles" and teleports past them; content implements both.

| varp | guide step | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Veronica outside Draynor Manor | goto-veronica, talk-veronica, veronica-accept, quest.stage.started (ledger 3-6 PASS) | DRIVEN | haunted.lua:80-96; C/areas/draynor/scripts/veronica.rs2:18 |
| 1 | Enter Draynor Manor's ground floor (scripted entrance door) | goto-manor-gate, open-manor-door (click_loc haunteddoorl) (ledger 7-8 PASS) | DRIVEN | haunted.lua:99-100; Q:10-24 ([oploc1,haunteddoorl] -> open_manor_entrance) |
| 1 | Pick up the fish food (first floor) | goto-fishfood, pickup.fish_food (ledger 15-16 PASS) | DRIVEN | haunted.lua:141-146; C/areas/world/configs/m48_52.spawn:43 |
| 1 | Pick up the poison (NW room) | goto-poison, pickup.poison (ledger 13-14 PASS) | DRIVEN | haunted.lua:134-139; m48_52.spawn:37 |
| 1 | Use the poison on the fish food | poison-fishfood, have.poisoned_fish_food (ledger 17-18 PASS) | DRIVEN | haunted.lua:150-153; Q:85-95 |
| 1 | Pick up the spade in the east room | not done -- the key it digs for is bypassed by ::goto (see tube row) | SKIP-CHEAT | haunted.lua:189 (::goto into the closet); content has it: m48_52.spawn:39 spade 3120 3359 |
| 1 | Search (dig) the compost heap for the key | not done | SKIP-CHEAT | haunted.lua:189; content: Q:29-40 ([oplocu,hauntedcompostheap] spade -> inv_add closet_key) |
| 1 | Use poisoned fish food on the fountain | goto-fountain, poison-fountain (use_on), poison-fountain-wait (ledger 19-21 PASS) | DRIVEN | haunted.lua:156-168; Q:101-111 |
| 1 | Search the fountain (pressure gauge) | fountain-gauge, fountain-gauge-drain, pickup.pressure_gauge (ledger 22-24 PASS) | DRIVEN | haunted.lua:175-182; Q:62-77 |
| 1 | Enter the small room with the key, pick up the rubber tube | goto-rubbertube teleports inside the locked closet; pickup.rubber_tube driven (ledger 25-26 PASS) | SKIP-CHEAT (partial: pickup driven, locked door bypassed) | haunted.lua:189-194; content lock: Q:44-60 ([oploc1/oplocu,closet_door] requires closet_key) |
| 1 | Search the bookcase to enter the secret room | not done | SKIP-CHEAT | haunted.lua:201 (::goto into the basement); content: Q:139-151,163-176 |
| 1 | Climb down the ladder + lever puzzle A-F in the basement | not done ("never touching closet_door or a single lever", lua:23) | SKIP-CHEAT | haunted.lua:201; content: Q:178-181 ([oploc1,puzzle_ladder_top]), Q:194-272 (lever triggers), Q:292+ ([proc,update_ernest_doors]) |
| 1 | Pick up the oil can in the west room | goto-oilcan, pickup.oil_can (ledger 27-28 PASS) | DRIVEN (reached by ::goto) | haunted.lua:201-206; C/areas/world/configs/m48_152.spawn:24 |
| 1 | Talk to Professor Oddenstein on the top floor | goto-oddenstein, talk-oddenstein, oddenstein-looking, quest.stage.spoken_to_oddenstein (ledger 9-12 PASS) | DRIVEN | haunted.lua:103-128; C/areas/draynor/scripts/professor_oddenstein.rs2:70 |
| 2 | Give Oddenstein the items | goto-oddenstein-handin, talk-oddenstein-handin, oddenstein-handin, quest.varp_complete/scroll/points/journal, reward.coins (ledger 29-36 PASS) | DRIVEN | haunted.lua:211-241 |

Summary: The item legs that end in a pickup or an item-on-loc are driven, but the two gating puzzles of this quest -- the spade/compost/closet-key chain for the rubber tube and the bookcase + six-lever basement maze for the oil can -- are skipped by ::goto teleports even though the content implements them (Q:29-60, Q:139-300). The test's own justification (lua:9-27) is that nothing the hand-in checks depends on them, which is exactly a test gap.

### quest_hero hero helper=heroesquest/HeroesQuest.java

COUNTS guide_steps=22 driven=18 skipped_content=1 skipped_cheat=3 skipped_other=0 verdict=MIXED first_gap=reach deeper Taverley Dungeon (jailer/Velrak/dusty-key gate or pipe bypassed by ::goto to the lava spot)

Paths: C = OSRS-Content/osrs239-content/server/scripts; H = C/quests/quest_hero/scripts; M = H/brimhaven_scarface_mansion.rs2. Ledger (moved, see note): C/selftest/quests/quest_hero/play/ledger.tsv SUMMARY 100 PASS fail=0.
Route: Phoenix Gang (the guide's non-Black-Arm branch, HeroesQuest.java:135-149).
Setup cheats that are legitimate: ::setvar qp/zanaris/dragonquest/arthur/phoenixgang and ::complete quest_druidicritual (hero.lua:57-62) are OTHER quests' completion (the quest's prerequisites); ::give phoenixkey2 (lua:63) is a Shield of Arrav item; ::setlevel and rune gear/sharks (lua:67-77) are levels/gear. Bring-alongs (HeroesQuest.java getItemRequirements: fishing rod, bait, harralander unf, pickaxe, [ranged/mage gear]) -- ::give at lua:80-85 legitimate; logs/tinderbox for the cooking fire are tools. iceGloves also appears in getItemRequirements but is quest-obtained (killIceQueen) and the test obtains it for real. Travel counted only where it is travel-with-purpose (a gated passage); stairs/ladders to Straven's base merged.

| varp | guide step | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Achietties outside the Heroes' Guild | goto-achietties, talkToAchietties(-dialog), quest.stage.started (ledger 8-11 PASS) | DRIVEN | hero.lua:129-141; C/areas/heroes_guild/scripts/achietties.rs2:55 |
| 1+ | Talk to Gerrant in Port Sarim for slime | goto-gerrant, talkToGerrant(-dialog), gerrant.slimeGranted (ledger 60-63 PASS) | DRIVEN | hero.lua:511-524; C/areas/port_sarim/scripts/gerrant.rs2:40 |
| 1+ | Combine harralander (unf) with the slime | mixBlamishOil, mixBlamishOil.oilMade (ledger 64-65 PASS) | DRIVEN | hero.lua:531-534; C/skill_herblore/configs/brewing/brew.dbrow (herblore_blamish_oil) |
| 1+ | Use the Blamish oil on the fishing rod | oilTheRod, oilTheRod.rodMade (ledger 66-67 PASS) | DRIVEN | hero.lua:536-539; H/oily_fishing_rod.rs2:10 |
| 1+ | Reach deeper Taverley Dungeon: squeeze through the pipe (70 Agility) or kill the Jailer for the jail key -> Velrak -> dusty key -> gate | goto-lavafish teleports straight to the lava spot | SKIP-CHEAT | hero.lua:547; content has every leg: C/drop_tables/scripts/jailer.rs2:13 (jail_key), C/areas/taverly/dungeon/scripts/jail_doors.rs2:8-30 ([oplocu,dungeonjail], [oplocu,deepdungeondoor] needs dusty_key), C/areas/taverly/dungeon/scripts/velrak_the_explorer.rs2:8-23, pipe C/skill_agility/configs/maplink_agility.dbrow:610 |
| 1+ | Fish a lava eel | goto-lavafish, fish.lavaeel, fish.lavaeel_caught (ledger 68-70 PASS) | DRIVEN (after ::goto) | hero.lua:547-551 |
| 1+ | Cook the lava eel | fire.goto, lightFire, fire.lit, lookup.fire, cookLavaEel(.cooked) (ledger 71-76 PASS) | DRIVEN | hero.lua:559-579 |
| 1+ | Talk to Straven | goto-straven, talkToStraven-1(-dialog), quest.stage.phoenix_gangmember_spoken (ledger 12-15 PASS) | DRIVEN | hero.lua:146-155; C/areas/varrock/scripts/straven.rs2:138 |
| 2 | Talk to Alfonse in Brimhaven | goto-alfonse, talkToAlfonse(-dialog), quest.stage.phoenix_talked_alfonse (ledger 16-19 PASS) | DRIVEN | hero.lua:160-170; C/areas/area_brimhaven/scripts/brimhaven_thin.rs2:44 |
| 3 | Talk to Charlie the Cook | openKitchenDoor, talkToCharlie(-dialog), quest.stage.phoenix_talked_charlie (ledger 20-23 PASS) | DRIVEN | hero.lua:177-200; H/brimhaven_restaurant.rs2:8; H/charlie_the_cook.rs2:34 |
| 4 | Push the wall into Pete's garden | openKitchenPanel (ledger 24 PASS) | DRIVEN | hero.lua:210; H/brimhaven_restaurant.rs2:16 |
| 4 | Get the misc key from your partner and use it on the NW door | not done; goto-throughSideDoor ::goto's onto the door tile | SKIP-CONTENT | hero.lua:223; port collapses the co-op leg: M:38-55 lets a Phoenix player at >=hero_phoenix_talked_charlie walk pete_sidedoor with no key (M:52-55); misc_key only via [oplocu,pete_sidedoor] M:60-63 |
| 4 | Kill Grip (partner lures him) | goto-grip, attackGrip, killGrip, quest.stage.phoenix_killed_grip (ledger 27-31 PASS) | DRIVEN | hero.lua:230-237; C/drop_tables/scripts/grip.rs2:19 |
| 5 | Get the candlestick (from partner) | pickUpGripKeys, unlockTreasureDoor, openChest-1/-2, lootCandlesticks, chest.heroquestUnclobbered (ledger 32-43 PASS) | DRIVEN (solo content substitute for the partner trade) | hero.lua:242-367; M:75-87 (soft single-player treasure door), M:93-118 |
| 5 | Bring the candlestick to Straven | goto-straven-2, talkToStraven-2(-dialog), quest.stage.phoenix_obtained_armband, straven.armbandGranted (ledger 44-48 PASS) | DRIVEN | hero.lua:373-384; straven.rs2:143 |
| 6 | Mine the rockslide on White Wolf Mountain and take the ladders to the Ice Queen | goto-icequeen teleports onto her tile (the pickaxe fallback lua:401-405 did not run: ledger 49 goto ok) | SKIP-CHEAT | hero.lua:400; content: C/areas/area_white_wolf_mountain/scripts/white_wolf_mountain.rs2:10-32 ([oploc1/oplocu,herorockslide] -> mine_ice_queen_lair_rockslide) |
| 6 | Kill the Ice Queen | killIceQueen.await_dead, player.aliveAfterIceQueen (ledger 50-51 PASS) | DRIVEN | hero.lua:408-438 |
| 6 | Pick up the ice gloves | iceGloves.visible, pickUpIceGloves, equipIceGloves (ledger 52-54 PASS) | DRIVEN | hero.lua:442-464; C/drop_tables/scripts/ice_queen.rs2:16 |
| 6 | Travel to Entrana with the monks (no weapons/armour) | goto-firebird teleports onto Entrana with rune gear worn (lua:470-473 notes the monk's check is skipped) | SKIP-CHEAT | hero.lua:475; content: C/areas/port_sarim/scripts/monk_of_entrana.rs2:16-17 ([opnpc1,shipmonk1_c] -> shipmonk_talk) |
| 6 | Kill the Entrana firebird | attackFirebird, killFirebird (ledger 56-57 PASS) | DRIVEN | hero.lua:476-478 |
| 6 | Pick up the fire feather | hotFeather.visible, pickUpHotFeather (ledger 58-59 PASS) | DRIVEN | hero.lua:483-504; C/drop_tables/scripts/entrana_firebird.rs2:13; H/fire_feather.rs2:15-20 |
| 6 | Bring Achietties the items | goto-achietties-handin, achietties.handIn(-dialog), quest.varp_complete/scroll/points/journal, reward.* (ledger 79-100 PASS) | DRIVEN | hero.lua:608-666; achietties.rs2:31; H/quest_hero.rs2:49-50 |

Summary: Every quest-state leg of the Phoenix route is driven, including both boss fights and the item crafting chain; the gaps are three gated-travel legs the test crosses by ::goto (deep Taverley's jailer/Velrak/dusty-key gate or pipe, the White Wolf Mountain rockslide, and the Entrana monk boat with its no-weapons rule) that the content implements, plus the co-op misc-key door that the port itself collapses for solo Phoenix players (M:52-55).

### quest_hetty hetty helper=witchspotion/WitchsPotion.java

COUNTS guide_steps=4 driven=3 skipped_content=0 skipped_cheat=1 skipped_other=0 verdict=TEST GAP first_gap=killRat (rat's tail given by ::give)

Bring-alongs (getItemRequirements, WitchsPotion.java:101-108 region): onion, burnt meat, eye of newt -- ::give at hetty.lua:10-12 is legitimate. Rat's tail is NOT a bring-along: it is the quest's own step killRat (WitchsPotion.java:79, steps.put(1) ConditionalStep default).

| varp | guide step | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Hetty in Rimmington ("I am in search of a quest." / "Yes") | goto-talkToWitch, talkToWitch, hetty.choose_quest, hetty.choose_darker, hetty.expect_stage_started (ledger rows 3-10 PASS) | DRIVEN | hetty.lua:35-59; content hetty.rs2:8-13,44-81 sets %hetty=^hetty_started |
| 1 | Kill a rat in the house to the west for a rat tail | `::give rats_tail 1` in setup | SKIP-CHEAT | hetty.lua:13; content implements the leg: drop_tables/scripts/rat.rs2:15,37-39 ([ai_queue3,rat_indoors] drops rats_tail while %hetty started) |
| 1 | Bring the ingredients to Hetty | returnToWitch, hetty.drain_complete_close, hetty.expect_stage_given (ledger 11-13 PASS) | DRIVEN | hetty.lua:63-69; hetty.rs2:21-30 |
| 2 | Drink from the cauldron | drinkPotion, completion_mesbox_text, completion_continue, quest.varp_complete/scroll/points/journal, reward.magic (ledger 15-22 PASS) | DRIVEN | hetty.lua:76-93; quest_hetty.rs2:6-17 |

Ledger: SUMMARY 22 PASS, fail=0 (read at selftest/quest_tests/hetty/ledger.tsv; during this audit OSRS-Content staged a move to selftest/quests/quest_hetty/play/ledger.tsv, same SUMMARY).

Summary: The only gap is the rat kill: the test hands itself rats_tail with ::give (hetty.lua:13) although the content implements the drop from rat_indoors (rat.rs2:37-39) and the guide makes it a quest step. Everything else is driven through real dialogue and the cauldron loc.

### quest_hunt hunt helper=piratestreasure/PiratesTreasure.java (+ RumSmugglingStep.java)

COUNTS guide_steps=15 driven=14 skipped_content=0 skipped_cheat=1 skipped_other=0 verdict=TEST GAP first_gap=travel to Karamja via Port Sarim seaman (replaced by ::goto teleport to Brimhaven)

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver, QH=quest-helper/.../helpers/quests/piratestreasure, C=R/OSRS-Content/osrs239-content/server/scripts.
Ledger: R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/hunt/ledger.tsv -- SUMMARY 71 PASS, pass=71 fail=0, 248 ticks, exit=0. The QUEUE note's suspect row gardener-dead is PASS in the ledger (ledger.tsv:60, "already gone before the wait ... hp 0/30 -> gone"); hunt.gardener_gone PASS (ledger.tsv:61).
Setup (hunt.lua:39-63): ::clearinv, ::give spade 2, ::give coins 100, ::setlevel attack 40, ::setlevel strength 40, ::give rune_scimitar 1. Spade and coins are guide bring-alongs (PiratesTreasure.java:161-165); the levels + scimitar are combat prerequisites for the gardener, not a quest leg. No ::setvar / ::complete / quest debugproc (huntrun, dig.rs2:62) / ::kill used.
Note: t.player.goto_tile is a teleport cheat (::goto, fallback ::tele) -- R/script/plugins/quest_driver/pointer.lua:1781-1795. Used for every travel.

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Redbeard Frank, "I'm in search of treasure." (PiratesTreasure.java:110-112) | goto-redbeard, talk-redbeard-start, redbeard-greeting, choose-treasure, redbeard-treasure-story, quest.stage.fetch_rum | DRIVEN | hunt.lua:85-107; C/quests/quest_hunt/scripts/redbeard_frank.rs2:70 sets hunt_fetch_rum; ledger.tsv:4-9 |
| 1 | Talk to a Seaman in Port Sarim to sail to Musa Point (RumSmugglingStep.java:175-177) | goto-bartender = ::goto 2797,3155,0 straight from Port Sarim to Brimhaven | SKIP-CHEAT | hunt.lua:111 (goto_tile -> pointer.lua:1784 ::goto); content has the leg: C/areas/port_sarim/scripts/sailors.rs2:14,24 ([opnpc1]/[opnpc3,seaman_lorris] -> karamja_sailor_talk/pay, p_telejump to Musa Point per :9) |
| 1 | Buy a Karamjan rum (guide: Zembo, RumSmugglingStep.java:179-181) | talk-bartender, buy-rum, hunt.rum_bought (alt source: Deadman's Chest bartender, 27 coins) | DRIVEN (alternative source) | hunt.lua:112-122; C/areas/area_brimhaven/scripts/deadmans_bartender.rs2:25-31; ledger.tsv:11-13. Zembo exists only as an npc config (configs/all.npc:435920), no shop script in C |
| 1 | Pick 10 bananas, talk to Luthas about employment (RumSmugglingStep.java:184-188) | goto-luthas, talk-luthas, luthas-employ; banana pick loop -> hunt.bananas_gathered | DRIVEN | hunt.lua:127-136,173-186; C/quests/quest_hunt/scripts/luthas.rs2:55 sets employed bit; ledger.tsv:14-16,18 |
| 1 | Put the Karamjan rum into the crate (RumSmugglingStep.java:190-191) | use_on(karamja_rum, bananacrate) loop -> hunt.stash_rum | DRIVEN | hunt.lua:146-161; banana_crate.rs2:48-57 (crate_rum=1); ledger.tsv:17 |
| 1 | Fill the crate with bananas (RumSmugglingStep.java:194-195) | goto-luthas-crate, use_on(banana, bananacrate) loop -> hunt.bananas_packed, check-crate-full, hunt.crate_full_msg | DRIVEN | hunt.lua:190-220; banana_crate.rs2:38-44; ledger.tsv:19-22 |
| 1 | Talk to Luthas, crate finished (RumSmugglingStep.java:197-198) | talk-luthas-payout, luthas-payout, hunt.luthas_paid_msg, hunt.luthas_payout_reopen, luthas-payout-decline | DRIVEN | hunt.lua:231-254; luthas.rs2:19-24 (crate_rum 1->2); ledger.tsv:23-27 |
| 1 | Pay the Customs Officer to sail to Port Sarim (RumSmugglingStep.java:200-207) | goto-customs (teleport to the dock, walk-merged), talk-customs, customs-pass, hunt.customs_paid_msg, hunt.customs_pay_reopen, customs-arrive | DRIVEN | hunt.lua:260-299; C/areas/karamja/scripts/customs_officer.rs2:73,97,112 (search, pay, p_telejump to Port Sarim); ledger.tsv:28-33 |
| 1 | Grab the white apron (guide: Fishing Shop, RumSmugglingStep.java:209-210) | goto-apron, click_obj(white_apron) -> hunt.take_apron (ground spawn by Wydin's, 3009,3204) | DRIVEN (alternative in-world copy) | hunt.lua:307-314; C/areas/world/configs/m47_50.spawn:50 (also :52-53 is the guide's copy); ledger.tsv:34-35 |
| 1 | Get a job with Wydin, wear apron, search the back-room crate (RumSmugglingStep.java:212-214) | goto-wydin, talk-wydin, wydin-job, wear-apron, wydindoor loop -> hunt.enter_back_room, open-grocery-crate, decline-crate-banana, hunt.rum_recovered | DRIVEN | hunt.lua:318-373; C/areas/port_sarim/scripts/wydin.rs2:58-67; C/quests/quest_hunt/scripts/food_store.rs2:4-8,21-28; ledger.tsv:36-43 |
| 1 | Bring the rum to Redbeard Frank (RumSmugglingStep.java:216-218) | goto-redbeard-rum, talk-redbeard-rum, redbeard-hand-rum, choose-go-get-it, hunt.redbeard_dialogue_closed, quest.stage.received_key | DRIVEN | hunt.lua:379-402; redbeard_frank.rs2:50-52; ledger.tsv:44-49 |
| 2 | Climb Blue Moon Inn stairs + use key on chest (PiratesTreasure.java:118-122) | goto-chest (::goto to 3219,3396,1 -- stairs not climbed; merged as walking), use-key-on-chest, hunt.message_taken | DRIVEN (stair climb teleported, walking-merged) | hunt.lua:407-418; C/quests/quest_hunt/scripts/pirate_message.rs2:7,17-18; ledger.tsv:50-52 |
| 2 | Read the Pirate message (PiratesTreasure.java:115) | read-message, read-message-dialog, quest.stage.read_note | DRIVEN | hunt.lua:421-426; pirate_message.rs2:20-22; ledger.tsv:53-55 |
| 3 | Dig in Falador Park cross; dig again after the gardener (PiratesTreasure.java:126) | goto-dig, dig-treasure (redirected), goto-dig-again, dig-treasure-again, quest.varp_complete / scroll / points / journal | DRIVEN | hunt.lua:429-512; C/general_use/scripts/spade.rs2:10 -> dig.rs2:4-16, :25-29; ledger.tsv:56-57,62-66 |
| 3 | Kill the Gardener (level 4) (PiratesTreasure.java:129,152) | hunt.equip_scimitar, attack-gardener, gardener-dead, hunt.gardener_gone | DRIVEN (setup ::setlevel 40/40 + rune scimitar are combat prereqs; ledger shows the gardener was already at 0 hp from auto-retaliate when the attack press landed) | hunt.lua:60-62,472-496; dig.rs2:42-44; ledger.tsv:58-61 |

Rewards (not guide steps): reward.pirate_casket, open-casket, reward.gold_ring/emerald/coins all PASS (ledger.tsv:68-73; dig.rs2:31-40).

Summary: The smuggling chain is driven for real end to end; the one skipped leg is the outbound sea voyage -- the test teleports from Port Sarim to Brimhaven with ::goto (hunt.lua:111) although the content wires the Port Sarim seamen (sailors.rs2:14,24). Rum and apron come from alternative in-world sources (Deadman's Chest bartender; the ground apron at 3009,3204) rather than the guide's Zembo/Fishing Shop -- Zembo has no shop script in the content pack, which is a minor content gap for the guide's route but not for the quest. The Blue Moon stair climb is also teleported but is a walking step, merged.

### quest_misc misc helper=throneofmiscellania/ThroneOfMiscellania.java

COUNTS guide_steps=23 driven=19 skipped_content=4 skipped_cheat=0 skipped_other=0 verdict=CONTENT GAP first_gap=talkBrand1 (Brand courtship dialogue, varp 10 court ladder)

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver, QH=quest-helper. J = QH/.../throneofmiscellania/ThroneOfMiscellania.java.
C = R/OSRS-Content/osrs239-content/server/scripts/quests/quest_misc/scripts. T = R/test/quests/misc.lua.
Ledger R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/misc/ledger.tsv: SUMMARY 81 rows, pass=81 fail=0 (every row cited below is PASS).
Route audited: Brand courtship (test chooses Brand, T:106; guide courtBrand J:109-118). Astrid alternative (J:98-106) counted once as the chosen branch.
Bring-alongs (J:469-481 getItemRequirements): iron bar, logs, ring, flowers, bow/cake, reputation item. Setup ::give coins/iron_bar/logs/gold_ring/cake/bronze_axe (T:66-71) are all bring-alongs -> not skips. Flowers are bought live (not given).
Prerequisite cheat: "::complete quest_heroes" (T:72) satisfies a general requirement (J:496), not a quest leg -> not counted.
Travel: travelToMisc (boat from Rellekka, J:319-320) and every spiral-stair ObjectStep merged into the action they lead to; test uses goto_tile teleports (T:100 etc.), which also bypass misc_door_guard.rs2 (T:9-15).

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Buy flowers from Flower Girl (getFlowers, J:321) | buyFlowers, buyFlowers-dialog, inv.gotFlowers (T:120-132; ledger 10-12) | DRIVEN | areas/area_miscellania/scripts/flower_girl.rs2:9,20,32 |
| 0 | Travel + go upstairs + talk to King Vargas, choose partner (talkToVargas, J:324-326) | goto-vargas1, talkVargas1(-dialog) chooses Brand (T:100-109; ledger 3-5) | DRIVEN | C/misc_king_vargas.rs2:58-69 |
| 10 (court) | Talk to Prince Brand a few times, 3 choices (talkBrand1, J:358-361) | talkBrandNeedFlowers(-dialog) (T:115-118) lands on the reminder "Did you bring me those flowers?" only | SKIP-CONTENT | Vargas's choice sets %misc_affection = s1_step0 at C/misc_king_vargas.rs2:68, so Brand's switch never hits not_started (C/misc_prince_brand.rs2:25-26); brand_talk1 (C/misc_prince_brand.rs2:43-52, the only writer of misc_s1_d1..d3) is unreachable. Test header T:17-29 acknowledges it. |
| 10 (court) | Use flowers on Brand (giveFlowersToBrand, J:376) | giveFlowersBrand, giveFlowersBrand.consumed (T:140-147; ledger 14-15) | DRIVEN | C/misc_prince_brand.rs2:94-102 |
| 10 (court) | Clap emote next to Brand (clapForBrand, J:380) | none (no emote performed) | SKIP-CONTENT | flowers opnpcu narrates the clap and sets %misc_s1_emote=1 itself: C/misc_prince_brand.rs2:98,101; no emote trigger in quest_misc (misc_princess_astrid.rs2:13-15 header: emotes narrated, no emote primitive) |
| 10 (court) | Talk to Brand more (talkBrand2, J:363) | talkBrand2(-dialog) (T:150-156; ledger 16-17) | DRIVEN | C/misc_prince_brand.rs2:27,57-65 (linear, no choices) |
| 10 (court) | Give Brand a cake (giveCakeToBrand, J:373) | giveCakeBrand, .consumed (T:159-163; ledger 18-19); cake = bring-along ::give T:70 | DRIVEN | C/misc_prince_brand.rs2:104-112 |
| 10 (court) | Talk to Brand more (talkBrand3, J:368) | talkBrand3(-dialog) (T:169-176; ledger 20-21) | DRIVEN | C/misc_prince_brand.rs2:29,70-81 |
| 10 (court) | Blow kiss emote next to Brand (blowKissToBrand, J:379) | none | SKIP-CONTENT | brand_talk3 narrates `mes("You blow Prince Brand a kiss.")` and sets %misc_s3_emote=1: C/misc_prince_brand.rs2:75,79 |
| 10 (court) | Use a ring on Brand (useRingOnBrand, J:381) | giveRingBrand(-dialog) (T:179-183; ledger 22-23); ring = bring-along ::give T:69 | DRIVEN | C/misc_prince_brand.rs2:113-123 (sets %misc_acceptedtorule=1 :122) |
| 10 | Talk to Queen Sigrid (talkToSigridDip1, J:392) | talkSigrid1(-dialog), quest.stage.talked_to_queen (T:201-207; ledger 28-31) | DRIVEN | C/misc_queen_sigrid.rs2:18,32-35 |
| 20 | Talk to King Vargas (talkToVargasDip1, J:397) | talkVargas3(-dialog), stage queen_requests_recognition (T:209-215; ledger 32-35) | DRIVEN | C/misc_king_vargas.rs2:47,93-96 |
| 30 | Talk to Queen Sigrid (talkToSigridDip2, J:403) | talkSigrid2(-dialog), stage need_bard_for_anthem (T:217-223; ledger 36-39) | DRIVEN | C/misc_queen_sigrid.rs2:20,40-43 |
| 40 | Talk to Prince Brand for the anthem (talkToBrandDip, J:408) | getAnthem(-dialog), stage prince_composed_anthem, inv.gotAwfulAnthem (T:226-236; ledger 40-44) | DRIVEN | C/misc_prince_brand.rs2:17-18,35-41 |
| 50 | Talk to Advisor Ghrim (talkToGhrimDip, J:412) | correctAnthem(-dialog), stage advisor_corrected_anthem, inv.gotGoodAnthem (T:238-248; ledger 45-49) | DRIVEN | C/misc_advisor_ghrim.rs2:35-36,48-55 |
| 60 | Return to Queen Sigrid with good anthem (talkToSigridDip3, J:418) | giveAnthemToSigrid(-dialog), stage queen_gave_treaty, inv.gotTreaty (T:250-259; ledger 50-54) | DRIVEN | C/misc_queen_sigrid.rs2:23,48-58 |
| 70 | Return to King Vargas with treaty (talkToVargasDip2, J:423) | giveTreatyToVargas(-dialog), stage gave_king_treaty (T:262-268; ledger 55-58) | DRIVEN | C/misc_king_vargas.rs2:52,110-114 |
| 80 | Talk to Derrik with iron bar (talkToDerrik, J:428) | forgeNib(-dialog), inv.gotNib (T:270-279; ledger 59-62); iron bar = bring-along ::give T:67 | DRIVEN | areas/area_miscellania/scripts/derrik.rs2:7,11 -> C/misc_smithy.rs2:18-24 |
| 80 | Use giant nib on logs (makePen, J:432) | makePen, inv.gotPen (T:283-286; ledger 63-64); logs = bring-along ::give T:68 | DRIVEN | C/misc_giant_nib.rs2:7-13 |
| 80 | Talk to Vargas with giant pen (giveVargasPen, J:435) | giveVargasPen(-dialog), stage king_signed_treaty (T:288-293; ledger 65-68) | DRIVEN | C/misc_king_vargas.rs2:53,116-124 |
| 90 | Reach 75% support: rake patches / mine coal / cut maples / fish (get75Support, J:462) | askGhrimForWork(-dialog) (T:305-310; ledger 69-71) - a Ghrim talk, not the resource activity | SKIP-CONTENT | C/misc_advisor_ghrim.rs2:57-67: narrated `mes("You spend time raking...")` :64 and `%misc_approval = ^MISC_APPROVAL_75_PERCENT` :66; header :12-22 says the Managing Miscellania loop has no writer |
| 90 | Talk to Vargas to finish (finishQuest, J:464) | talkVargasFinish(-dialog), quest.stage.complete, reward.coffers, scroll rows (T:316-381; ledger 72-81) | DRIVEN | C/misc_king_vargas.rs2:54,126-128,141-146 |

(Recovery step getAnotherAwfulAnthem J:409 not counted - alternate path. The test's talkVargas2 (T:193-198, stage talked_to_king) is an extra content-only visit: the guide goes straight from courting to Sigrid, the port gates %misc_quest=10 on a Vargas talk after acceptance, C/misc_king_vargas.rs2:85-87.)

Summary: every quest leg the content implements is driven by the test with no quest-leg cheats (only bring-along ::give and the Heroes' Quest prerequisite ::complete). The four gaps are all content collapses: the Brand first-conversation ladder is unreachable because Vargas pre-sets the affection step, the clap and blow-kiss emote legs are narrated by mes() inside other interactions, and the biggest one, the 75%-support Managing Miscellania resource grind, is replaced by a single Ghrim dialogue that writes %misc_approval directly (misc_advisor_ghrim.rs2:66).

### quest_mortton mortton helper=shadesofmortton/ShadesOfMortton.java

COUNTS guide_steps=22 driven=17 skipped_content=4 skipped_cheat=1 skipped_other=0 verdict=MIXED first_gap=repairTemple (stage 50/55, ::mortton_repairtemple debugproc)

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver; test=R/test/quests/mortton.lua; ledger=R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/mortton/ledger.tsv (104 rows, all PASS, "SUMMARY 104 PASS"); QS=R/OSRS-Content/osrs239-content/server/scripts/quests/quest_mortton/scripts; GM=R/OSRS-Content/osrs239-content/server/scripts/minigames/game_mortton/scripts; QH=ShadesOfMortton.java.

Setup (mortton.lua:37-94): ::give tarrominvial 5 / tinderbox / logs / ashes 5 / hammer / coins 5000 = QH getItemRequirements (QH:284) bring-alongs, legit; rune_scimitar + shark 5 = QH recommended combatGear/food (QH:290), legit; ::setlevel crafting 20 / herblore 15 = quest reqs (QH:318-320); firemaking 99 (req 5) and hp 99 / att/str/def 40 are over-gearing, not quest legs; ::complete quest_priestperil (prereq, QH:317) and quest_druidicritual (herblore unlock) are prerequisites, not legs; ::passive on 4 afflicted types + shadeshadow_level1 (mortton.lua:77-93) removes aggression only, the kills are still real combat (noted, not counted as a skip). No ::setvar of %morttonquest, no ::kill, no ::skipboss.

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | searchShelf: search shelf in south building | goto-searchShelf, searchShelf (click_loc op1), haveSerumBook, searchShelf-dismiss | DRIVEN | mortton.lua:180-192; QS/quest_mortton.rs2:1-8 |
| 0 | readDiary | readDiary (inv_op serum_book 1), readDiary-drain, quest.stage.mortton_read_diary | DRIVEN | mortton.lua:198-200; QS/serum_book.rs2:21-22 sets 5 |
| 5 | addAshes: ashes on tarromin (unf) -> Serum 207 | mixSerum207-1 (use_item_on_item), mixSerum207.stageAdvanced, quest.stage.mortton_made_serum | DRIVEN | mortton.lua:210-216; QS/quest_mortton.rs2:103-110 sets 10 |
| 10 | use207OnRazmire | goto-razmire, razmire.find, razmire.cure (use_on mort_serum3) | DRIVEN | mortton.lua:226-230; QS/razmire_keelgan.rs2:33-48 |
| 10 | talkToRazmire: "shadowy creatures" / "Yes, I'll dispatch" | razmire.acceptKillShades (chat.play choices), quest.stage.mortton_kill_shades | DRIVEN | mortton.lua:238-248; QS/razmire_keelgan.rs2:203 sets 15 |
| 15-35 | kill5Shades (+kill4..kill1 substeps): kill 5 Loar Shades, pick up remains | goto-shadehunt, shade.hunt (real t.player.attack + await_dead_engaged + click_obj shade_bones1 loop), player.aliveAfterShades, quest.stage.mortton_killed_5_shades, shade.remainsCollected | DRIVEN (note: shade type held ::passive, mortton.lua:93) | mortton.lua:277-354; GM/mortton_shades.rs2:34-35,47-60 |
| 40 | use207OnRazmireAgain | goto-razmire-2, mixSerum207-3, razmire.recure (ledger 29-30 PASS: afflicted branch ran) | DRIVEN | mortton.lua:364-369 |
| 40 | talkToRazmireAgain: hand in remains | razmire.giveRemains-dialogue, quest.stage.mortton_shades_to_razmire | DRIVEN | mortton.lua:380-390; QS/razmire_keelgan.rs2:99-111 sets 45 |
| 45 (text) | buy olive oil (general store) + 5 timber, 5 limestone, 25 swamp paste (builders' store) | razmire.shopOpen/buyTimber/buyLimestone/buySwamppaste/shopClose (+ -2/-3 trips); olive oil via Ulsquire's free grant (alternative source) ulsquire.askOil-dialogue, haveOliveOil | DRIVEN (swamp paste capped by pack space, topped up over 3 trips) | mortton.lua:422-444, 579-676, 749-771; QS/razmire_keelgan.rs2:14-20,288-289; QS/ulsquire_shauncy.rs2:154-158 |
| 45 | use207OnUlsquire | goto-ulsquire, ulsquire.find, ulsquire.cure (use_on) | DRIVEN | mortton.lua:450-454; QS/ulsquire_shauncy.rs2:7-21 |
| 45 | talkToUlsquire: show remains | ulsquire.giveRemains, quest.stage.mortton_shades_to_ulsquire | DRIVEN | mortton.lua:455-463; QS/ulsquire_shauncy.rs2:79-92 sets 47 |
| 47 | talkToUlsquireAgain: "What can you tell me about that temple?" | ulsquire.askTemple, ulsquire.askTemple-dialogue, quest.stage.mortton_ulsquire_temple | DRIVEN | mortton.lua:472-483; QS/ulsquire_shauncy.rs2:199-205 sets 50 |
| 50/55 | repairTemple: repair temple walls | ::mortton_repairtemple debugproc x3 (temple.repair-1/-2/-3), temple.wallsRepaired | SKIP-CHEAT (debugproc replaces the clicked [oploc1/3,_temple_wall] repair; real materials/xp/rolls are spent, only clicking/walking skipped) | mortton.lua:510, 682, 700; GM/flamtaer_temple.rs2:1-42 (real leg), 498-530 (debugproc), 93-94 sets 55, 310-314 sets 60 |
| 60 | lightAltar (tinderbox) | goto-temple-altar, temple.altarClick (click_loc op1), temple.altarLit | DRIVEN | mortton.lua:781-797; GM/flamtaer_temple.rs2:219-242 |
| 60 | repairTo20Sanctity | none | SKIP-CONTENT (only needed for Serum 208, which the port does not gate on; oil needs 10% only) | GM/flamtaer_temple.rs2:418, 443 |
| 60 | useOilOnFlame -> sacred oil | temple.altarFind, temple.sanctifyOil (use_on oliveoil3), quest.stage.mortton_created_sacred_oil | DRIVEN | mortton.lua:802-806; GM/flamtaer_temple.rs2:411-435 sets 65 |
| 60/65 | use207OnFlame -> Serum 208 | none | SKIP-CONTENT (leg exists but writes only a runtime bit, never the stage; nothing later requires it) | GM/flamtaer_temple.rs2:437-466 (sets ^player_made_perm_serum only) |
| 65 | useOilOnLog -> pyre logs | temple.makePyreLogs, havePyreLogs, quest.stage.mortton_created_pyre_logs | DRIVEN | mortton.lua:827-829; GM/mortton_pyre.rs2:54-70 sets 70 |
| 70/75 | burnCorpse: pyre logs on pyre, remains, light | goto-pyre, temple.pyreFind, temple.pyreAddLogs, quest.stage.mortton_logs_on_pyre, temple.pyreLogsPlaced, temple.pyreAddRemains, temple.shadeBonesAfterPyre, temple.pyreRemainsPlaced, temple.pyreLight, temple.pyreLit | DRIVEN | mortton.lua:842-898; GM/mortton_pyre.rs2:119-139 (75), 168, 228-262 (80) |
| 80 | use208OnRazmire | none | SKIP-CONTENT (port completes at stage 80 without it; [opnpcu] cat-109 branch exists but ungated) | QS/ulsquire_shauncy.rs2:112-116; QS/razmire_keelgan.rs2:49-63 |
| 80 | use208OnUlsquire | none | SKIP-CONTENT (same collapse) | QS/ulsquire_shauncy.rs2:112-116 (no ^ulsquire_perm_serum_used test); 22-38 (unused leg) |
| 80 | talkToUlsquireToFinish | goto-ulsquire-3, ulsquire.completePyre, ulsquire.completePyre-dialogue, quest.varp_complete, quest.scroll_title, quest.points, quest.journal, reward.herbloreXp, reward.craftingXp | DRIVEN | mortton.lua:929-964; QS/ulsquire_shauncy.rs2:112-116 -> QS/quest_mortton.rs2:66-71 |

Summary: The one test-side gap is the temple wall repair (stages 50-55), which is driven by the ::mortton_repairtemple debugproc (real materials spent, but no wall is ever clicked) instead of the self-re-arming [oploc1/3,_temple_wall] leg. The content-side gap is the whole Serum 208 chain (repair to 20% sanctity, Serum 207 on the flame, 208 on Razmire and Ulsquire): the port implements each action but Ulsquire's stage-80 branch (ulsquire_shauncy.rs2:112-116) completes the quest without checking the perm-serum bits, so the test skips the chain and still completes.

### quest_mourningsendparti mourningsendparti helper=mourningsendparti/MourningsEndPartI.java

COUNTS guide_steps=34 driven=27 skipped_content=5 skipped_cheat=2 skipped_other=0 verdict=MIXED first_gap=killMourner + pickUpLoot (fight a mourner in the Arandar pass; port gives the loot on one click, mend1_disguise.rs2:29-37)

Guide steps are the 34 in the MourningsEndPartI.java getPanels() lists (lines 535-570). Ladder: steps.put 0/1/2/3/4/5/6/7/8 (lines 96-178).

| guide varp value | guide step (short) | test row(s) or cheat | class | evidence |
|---|---|---|---|---|
| 0/1 | talkToIslwyn | talkToIslwyn, talkToIslwyn-dialog | DRIVEN | mourningsendparti.lua:145-150 |
| 2 | talkToArianwyn | talkToArianwyn, talkToArianwyn-dialog | DRIVEN | |
| 3 | killMourner (kill a mourner in the Arandar pass, 7 free slots) | killMourner = talk_to op2, then msg only | SKIP-CONTENT | mend1_disguise.rs2:6-9 ("combat/loot narrated on interaction rather than fought"), :29-37 (mes "You strike the mourner down" + inv_add of all seven items) |
| 3 | pickUpLoot (pick up the dropped items) | killMourner.loot (reads the backpack only) | SKIP-CONTENT | mend1_disguise.rs2:30-36 puts the loot straight into the backpack; nothing is dropped |
| 3 | searchLaundry, useSoapOnTop | searchLaundry, cleanTop | DRIVEN | mend1_disguise.rs2:40-79 |
| 3 | talkToOronwen (repair trousers) | talkToOronwen, talkToOronwen-dialog | DRIVEN | fur and silk are bring-alongs |
| 4 | enterMournerBase, enterBasement, talkToEssyllt | click_loc mournerstewdoor, click_loc trap door, talkToEssyllt | DRIVEN | test :300-325 |
| 5 | talkToGnome, useFeatherOnGnome, talkToGnomeWithItems, releaseGnome, giveGnomeItems, askAboutToads | gnomeCage.* rows | DRIVEN | feather, crunchies, logs and leather are bring-alongs |
| 5 | getToads (Feldip Hills: dye on bellows, then inflate swamp toads) | dyeBellows.* (use bellows on dye directly gives a bloated toad) | SKIP-CONTENT | mend1_sheep.rs2:1-7 ("the dye-bellows-inflate loop is collapsed to a single dye-on-bellows interaction per colour") |
| 5 | dyeSheep (load toad, equip device, fire at 4 sheep) | loadToad.*, equip.paintgun*, fireSheep.* | DRIVEN | |
| 5 | enterBaseAfterSheep, enterBasementAfterSheep | goto-talkToEssylltAfterSheep: goto_tile 2043,4631,0 straight into the basement | SKIP-CHEAT (2) | test :697. The doors exist and the test drove them earlier (mend1_disguise.rs2:117-136) |
| 5 | talkToEssylltAfterSheep | talkToEssylltAfterSheep | DRIVEN | |
| 6 | pickUpRottenApple (north-west of Mourner HQ) | none; Essyllt hands the apple over | SKIP-CONTENT | mend1_disguise.rs2:203-204 (inv_add rottenapples in dialogue); mend1_poison.rs2:11-13 |
| 6 | talkToElena | talkToElena-dialog | DRIVEN | |
| 6 | pickUpBarrel (from the orchard) | none; folded into the apple-pile click | SKIP-CONTENT | mend1_poison.rs2:47-50 ("collapses pickUpBarrel + useBarrelOnPile") |
| 6 | useBarrelOnPile, useApplesOnPress | fillBarrel, pressApples | DRIVEN | |
| 6 | getNaphtha (fractionalising still) | `::give regicide_barrel_naphtha` | bring-along (counted as driven) | QH coal20OrNaphtha is an OR requirement; mend1_poison.rs2:9-11 also soft-skips the still |
| 7 | useNaphthaOnBarrel, useSieveOnBarrel, cookNaphtha, usePowderOnFood1/2 | mixNaphtha, sieveMix, cookToxin, poisonStore1/2 | DRIVEN | |
| 7 | talkToEssylltAfterPoison | goto (teleport into basement again, :899), talkToEssylltAfterPoison | DRIVEN (the travel is teleported, not counted a second time) | |
| 8 | returnToArianwyn | talkToArianwynFinal | DRIVEN | |

Summary:
- **Content gaps.** The port leaves out the mourner fight and the loot pickup, the Feldip toad-inflating trip, and the rotten-apple and barrel pickups.
- **Cheats in the test.** After dyeing the sheep, the test teleports into the HQ basement with goto_tile instead of going back through the HQ door and trap door, which it had driven earlier.
- **Setup.** The `::complete` / `::setvar` lines for Plague City, Waterfall, Regicide, Roving Elves, Chompy Bird and Sheep Herder complete prerequisite quests, not this quest's own work.
- **Ledger.** 155/155 PASS.

### quest_murder murder helper=murdermystery/MurderMystery.java

COUNTS guide_steps=19 driven=14 skipped_content=2 skipped_cheat=0 skipped_other=3 verdict=MIXED first_gap=pick up the pungent pot (murderpot2)

Paths: R=3draster-quest-driver; test=R/test/quests/murder.lua; content=R/OSRS-Content/osrs239-content/server/scripts/quests/quest_murder/scripts/ (abbrev Q/); spawn=R/OSRS-Content/osrs239-content/server/scripts/areas/world/configs/m42_55.spawn.
Ledger R/OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/murder/ledger.tsv: 95 rows, all PASS (SUMMARY 95 PASS).
Setup: ::clearinv, ::give pot_empty 1 (murder.lua:46-47). Pot is the helper's only getItemRequirements item (MurderMystery.java:617-621) -> legitimate bring-along. No ::setvar/::complete/debugproc cheats.
Per-suspect alternatives (6 barrels, 6 silver items, 6 alibi locs) are counted once each. The test drives BOTH thread-colour candidates (bob/carol in the recorded run); content decides the match.

| varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to the Guard, accept ("Yes") | goto.guard, talk.guard, accept.quest, accept.close, quest.stage.started | DRIVEN | murder.lua:92-114; Q/murder_guard.rs2:14 |
| 1 | Search the window for a thread | window.inspect_click, window.mesbox, evidence.thread | DRIVEN | murder.lua:135-180; Q/quest_murder_window.rs2:60-81 (setbit thread :79) |
| 1 | Pick up the pungent pot in the east room | (none) | SKIP-OTHER | Content has it: spawn m42_55.spawn:47 murderpot2, take trigger Q/quest_murder_barrels.rs2:97-110. Not gating: guard's conclusive branch checks only poisonproof+thread+fingerprints (Q/murder_guard.rs2:75). Test never touches murderpot2. |
| 1 | Pick up the criminal's dagger | weapon.locate, goto.weapon.1, weapon.pickup, weapon.have_dust | DRIVEN | murder.lua:241-287 |
| 1 | Investigate sacks for 2 flypaper | goto.sacks, sack.search/drain/choose/close .1-.3, evidence.paper_count | DRIVEN | murder.lua:293-300; Q/quest_murder_window.rs2:37 |
| 1 | Search the suspect's barrel (silver item) | goto.suspect.room.X, goto.barrel.X, barrel.search.X, barrel.close.X, evidence.have_item.X | DRIVEN | murder.lua:360-364; Q/quest_murder_barrels.rs2:9-79 |
| 1 | Fill pot with flour (for the dagger) | (none needed) | SKIP-CONTENT | Dagger spawns already floured: m42_55.spawn:46 `murderweapondust`; flour-on-dagger trigger exists (Q/quest_murder_prints.rs2:19, :74-77) but is unreachable from the spawn. |
| 1 | Use pot of flour on the dagger | (none needed) | SKIP-CONTENT | same: m42_55.spawn:46 |
| 1 | Use flypaper on the dagger (unknown print) | weapon.fingerprint, evidence.fingerprint1 | DRIVEN | murder.lua:304-305; Q/quest_murder_prints.rs2:31 |
| 1 | Fill pot with flour (for silver item) (+1 flypaper, merged) | goto.flourbarrel.X, flour.get.X, flour.close.X, evidence.have_flour.X | DRIVEN | murder.lua:366-369; Q/quest_murder_window.rs2:26 |
| 1 | Use flour on suspect's silver item | item.flour.X, evidence.have_itemdust.X | DRIVEN | murder.lua:371-372; Q/quest_murder_prints.rs2:13-18 |
| 1 | Use flypaper on the floured item | item.paper.X, evidence.have_print.X | DRIVEN | murder.lua:374-375; Q/quest_murder_prints.rs2:25-30 |
| 1 | Compare suspect's print with killer's print | fingerprint.compare.X, fingerprint.close.X, evidence.fingerprint_match | DRIVEN | murder.lua:381-394; Q/quest_murder_prints.rs2:37, :146-150 (setbit fingerprints :148) |
| 1 | Talk to Gossip ("Who do you think was responsible?") | (none) | SKIP-OTHER | Content has the dialogue (Q/gossip.rs2:74-77) but writes no murder state; not gating (Q/murder_guard.rs2:75). Test omits it. |
| 1 | Talk to Poison Salesman about customers | goto.salesman, salesman.talk, salesman.drain_to_options, salesman.choose, salesman.close | DRIVEN | murder.lua:308-312; area_seers/scripts/poison_salesman.rs2:57-63 (sets ^poisonproof_spoken_salesman :61) |
| 1 | Talk to Poison Salesman about the pungent pot | (none) | SKIP-OTHER | Content has option 4 (poison_salesman.rs2:40-41, :70-76), only offered with murderpot2 in inventory; writes no state. Test never picked up the pot, so the option is never shown. |
| 1 | Talk to suspect ("Why'd you buy poison the other day?") | goto.suspect.X, suspect.talk.X, suspect.drain_to_options.X, suspect.choose_poison.X, suspect.close.X | DRIVEN | murder.lua:328-336; e.g. Q/bob.rs2:43-48 |
| 1 | Search the suspect's alibi thing (compost/hive/drain/web/fountain/crest) | goto.poisonloc.X, poisonloc.search.X, poisonloc.close.X | DRIVEN | murder.lua:337-343; Q/quest_murder_poisonproof.rs2:12-101 |
| 1 | Return to the guard: "I know who did it!" | goto.guard.accuse, guard.talk, guard.drain_to_options, guard.accuse, guard.conclusive_proof, quest.varp_complete, quest.scroll_title, quest.points, quest.journal, reward.crafting_xp, reward.coins | DRIVEN | murder.lua:405-432; Q/murder_guard.rs2:73-76, :193, :318 |

Summary: every gating evidence leg (thread, poison proof, fingerprints) is driven for real with no cheats. The two SKIP-CONTENT rows exist because the port spawns the dagger already floured (m42_55.spawn:46), which removes the flour-on-dagger leg. The three SKIP-OTHER rows are the pungent pot, Gossip and the salesman's pot dialogue. Content implements all three but none gates the guard's conclusive branch, and the test silently leaves them out.

### quest_prince prince helper=princealirescue/PrinceAliRescue.java

COUNTS guide_steps=13 driven=12 skipped_content=0 skipped_cheat=1 skipped_other=0 verdict=TEST GAP first_gap=useKeyOnDoor (unlock prison door; ::goto into the cell)

Paths: T=test/quests/prince.lua, L=OSRS-Content/.../selftest/quests/quest_prince/play/ledger.tsv (SUMMARY pass=53 fail=0), Q=OSRS-Content/.../quests/quest_prince/scripts/quest_prince.rs2, G=PrinceAliRescue.java, S=OSRS-Content/osrs239-content/server/scripts.
Bring-alongs (G:218-233 getItemRequirements) given in setup T:43-54: softclay, 3 wool, yellow dye, redberries, pot of flour, bucket of water, ashes, bronze bar, pink skirt, 3 beer, rope, 100 coins -- all legitimate (none is produced by a guide step; the guide's optional "buy rope from Ned"/"buy dye from Aggie" alternatives are not needed).

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Hassan (G:183) | hassan.talk, hassan.accept, quest.stage.started | DRIVEN | T:102-109; L:7-9 PASS; S/areas/alkharid/scripts/hassan.rs2:10 |
| 10 | Talk to Osman (G:185) | osman.talk, osman.instructions, quest.stage.spoken_osman | DRIVEN | T:116-138; L:11-13 PASS; S/areas/alkharid/scripts/osman.rs2:87 |
| 20 | Ned makes a wig from 3 wool (G:187) | ned.talk, ned.wig, ned.wig.have | DRIVEN | T:144-161; L:15-17 PASS; S/areas/draynor/scripts/ned.rs2:156 |
| 20 | Dye the wig with yellow dye (G:191) | wig.dye (use plainwig on yellowdye), wig.dyed | DRIVEN | T:165-168; L:18-19 PASS; Q:6-16 |
| 20 | Aggie mixes skin paste (G:192) | aggie.talk, aggie.paste, aggie.paste.have | DRIVEN | T:174-190; L:21-23 PASS; S/areas/draynor/scripts/aggie.rs2:193 |
| 20 | Keli key print with soft clay (G:195) | keli.talk, keli.keyprint, keli.keyprint.have | DRIVEN | T:198-226; L:25-27 PASS; S/areas/draynor/scripts/lady_keli.rs2:187 |
| 20 | Use key print on a furnace with bronze bar (G:201) | goto-furnace, furnace.locate, furnace.smelt (use_on keyprint -> dwarf_keldagrim_furnace), furnace.key.have | DRIVEN (Keldagrim furnace instead of QH's Lumbridge one; smelting.rs2 dispatches on the item only) | T:238-245; L:28-31 PASS; S/skill_smithing/scripts/smelting/smelting.rs2:83 -> Q:21-38 |
| 20 | Talk to Leela (G:202) | leela.talk, leela.prep, quest.stage.prep_finished | DRIVEN | T:253-257; L:33-35 PASS; S/areas/draynor/scripts/leela.rs2:37 |
| 30-33 | Give Joe three beers (G:206) | joe.talk, joe.beer, quest.stage.guard_drunk | DRIVEN | T:263-279; L:37-39 PASS; S/areas/draynor/scripts/joe.rs2:53 |
| 40 | Use rope on Keli (G:208) | keli.tie (use_on rope -> lady_keli), keli.tie.dismiss, quest.stage.tied_keli | DRIVEN | T:285-292; L:41-43 PASS; Q:41-58 |
| 50 | Use the key on the prison door (G:210) | NOT performed: goto-prince-cell = ::goto teleport straight into the cell | SKIP-CHEAT (::goto teleport past a content-implemented, quest-gated door) | T:298 (rationale T:31-36, T:295-297); content implements it at Q:61-71 ([oplocu,alidoor]: requires princeskey, %princequest > guard_drunk and Keli gone) |
| 50 | Talk to Prince Ali and free him (G:212) | prince.talk, prince.rescue, quest.stage.saved | DRIVEN | T:300-311; L:44-47 PASS; S/areas/draynor/scripts/prince_ali.rs2:19 |
| 100 | Return to Hassan (G:214) | hassan.return.talk, hassan.return.reward, quest.varp_complete, reward.coins | DRIVEN | T:318-331; L:49-55 PASS; Q:96-99 |

Summary: one gap -- the prison-door unlock ([oplocu,alidoor], Q:61-71, which gates on the key, the drunk-guard stage and Keli being gone) is bypassed with a ::goto teleport into the cell; the test argues the hand-in reads no door state, but the leg (and its gate) is never exercised. Everything else, including all quest-made items, is obtained through real clicks.

### quest_pryingtimes pryingtimes helper=pryingtimes/PryingTimes.java

COUNTS guide_steps=10 driven=8 skipped_content=2 skipped_cheat=0 skipped_other=0 verdict=CONTENT GAP first_gap=deliverCargo (Port Sarim -> Pandemonium port task; content soft-skips it in dialogue)

Paths: T=test/quests/pryingtimes.lua, L=OSRS-Content/.../selftest/quests/quest_pryingtimes/play/ledger.tsv (SUMMARY pass=40 fail=0), P=OSRS-Content/.../quests/quest_pryingtimes/scripts/pryingtimes.rs2, PL=.../quest_pryingtimes/scripts/pryingtimes_locs.rs2, G=PryingTimes.java.
Setup (T:69-74): ::clearinv; ::pryingtimes (quest debugproc P:151-181 -- sets PREREQUISITES %sailing_intro/%squire complete, resets %quest_pry to 0, grants the bring-alongs steel bar/redberry pie/hammer/sailing log = G:200-203 getItemRequirements, teleports to Steve); ::setlevel smithing 30 (prerequisite skill, P:111); ::spawn steve_beanie_1op (content places no Steve -- T:25-36; a content seam, not a guide step). None of these performs a quest leg.

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Steve Beanie to start (G:166) | startQuest + -dialog, quest.stage.deliver | DRIVEN | T:109-122; L:6-8 PASS; P:78-101 |
| 5 | Take and complete the Port Sarim -> Pandemonium courier port task (G:174 PortTaskStep) | none -- the test just picks "I delivered that cargo for you." | SKIP-CONTENT | P:4 header "Deferred: port-task cargo delivery"; P:59-65 "Soft stand-in for PortTaskStep" sets ^pry_let_steve on the dialogue choice alone |
| 10 | Let Steve know you delivered the looty (G:175) | letSteveKnow + -dialog, quest.stage.get_key | DRIVEN (same dialogue chains 5->10->15, P:64-65 then P:52-57) | T:130-140; L:9-11 PASS |
| 15/20 | Get the key (crowbar) from Thurgo with steel bar/hammer/pie (G:178) | goto-thurgo, getKey + -dialog, getKey.crowbar, quest.stage.give_key | DRIVEN | T:143-172; L:12-16 PASS; areas/port_sarim/scripts/thurgo.rs2:20 -> PL:69-82, PL:44-67 |
| 20 | Give the 'key' to Steve (G:182) | giveKey + -dialog, quest.stage.test_key | DRIVEN | T:176-184; L:18-20 PASS; P:36-46 |
| 25 | Sail to the crate north-west of Pandemonium (G:185 SailStep) | goto-seaCrate (::goto teleport) | SKIP-CONTENT (no sailing in the pack) | P:4 "Deferred: ... SailStep to sea crate"; PL:2 "Deferred: authentic sail-to-crate" |
| 25 | Open the sealed (floating) crate with the crowbar (G:184) | seaCrate.placed, testKey (click_loc sailing_charting_drink_crate), testKey.stout | DRIVEN | T:192-208; L:21-24 PASS; PL:84-106 |
| 25 | Drink the stout (G:186) | drinkStout (inv_op), drinkStout-dialog, drinkStout.consumed | DRIVEN | T:213-234; L:25-27 PASS; PL:108-129 (sets completion flag PL:113) |
| 25 | Tell Steve the key works (G:188) | goToSteve + -dialog, quest.stage.open_crate | DRIVEN | T:238-245; L:29-31 PASS; P:25-31 |
| 30 | Open Steve's crate behind the bar (G:190) | barCrate.placed, openCrate, quest.varp_complete + rewards | DRIVEN | T:258-297; L:33-42 PASS; PL:137-155 -> P:116-125 |

Not counted: "Kill the Drink Troll, or log out" (G:187) is optional (G:212-215 "can be ignored"; the helper never maps it to a varp); the test does not fight it; content spawns it (PL:125) but gates nothing on it.

Summary: the two sailing legs are absent from the content pack -- the courier port task is replaced by a dialogue choice (P:59-65) and the sail to the sea crate by a teleport -- so the test cannot drive them. Every leg the content implements is driven through real clicks; the ::pryingtimes debugproc only grants prerequisites and bring-alongs.

### quest_rovingelves rovingelves helper=rovingelves/RovingElves.java

COUNTS guide_steps=14 driven=7 skipped_content=0 skipped_cheat=7 skipped_other=0 verdict=TEST GAP first_gap=enterGlarialsTombstone (use Glarial's pebble on the tombstone; replaced by ::goto into the tomb)

Paths: T=test/quests/rovingelves.lua, L=OSRS-Content/.../selftest/quests/quest_rovingelves/play/ledger.tsv (SUMMARY pass=42 fail=0), RE=OSRS-Content/.../quests/quest_rovingelves/scripts, WL=OSRS-Content/.../quests/quest_waterfall/scripts/quest_waterfall_locs.rs2, G=RovingElves.java.
Setup (T:69-97): ::clearinv; ::give spade 1 (bring-along, G:202); ::give shark 15 (food, recommended G:208); ::setlevel attack/strength/defence/hitpoints/magic 99 (combat assist -- the fight itself is still real); ::setvar regicide_quest ^regicide_complete and ::complete quest_waterfall (prerequisite quests, not legs of this quest). Rope/pebble/key bring-alongs (G:202) are never given -- the legs that need them are skipped instead.

| guide varp | guide step (short) | test row(s) / cheat | class | evidence |
|---|---|---|---|---|
| 0/1 | Talk to Islwyn (G:164) | goto-islwyn1, talk.islwyn1 + -dialog, quest.stage.spoken_islwyn (journal) | DRIVEN | T:140-160; L:6-9 PASS; RE/rovingelves_islwyn.rs2:65 |
| 2 | Talk to Eluned (G:168) | walk.eluned1, talk.eluned1 + -dialog, quest.stage.spoken_eluned | DRIVEN | T:167-193; L:10-15 PASS; RE/rovingelves_eluned.rs2:59 |
| 3 | Enter Glarial's Tomb via the tombstone (pebble) (G:169) | goto-tomb (::goto 2528,9843) | SKIP-CHEAT (::goto teleport) | T:195-200; content: WL:143-160 [oplocu,glarials_tombstone_waterfall_quest] (enterable after Waterfall, WL:137-141) |
| 3 | Kill the Moss Guardian (G:175) | killGuardian.attack, killGuardian.await_dead (real combat loop, 5 rounds, 4 sharks eaten, confirmed by seed drop), player.aliveAfterGuardian | DRIVEN | T:208-297; L:18-20 PASS; RE/rovingelves_mossgiant.rs2:44-61 (sets ^obtained_old_seed at :61) |
| 3 | Pick up the consecration seed (G:177) | seedDrop.visible, pickUpSeed (click_obj), quest.stage.obtained_old_seed | DRIVEN | T:320-349 (click_obj T:332); L:22-24 PASS; drop at RE/rovingelves_mossgiant.rs2:60 |
| 3 | Return the seed to Eluned (G:179) | talk.eluned2 + -dialog, quest.stage.seed_enchanted | DRIVEN | T:355-378; L:25-30 PASS; RE/rovingelves_eluned.rs2:65-76 |
| 4 | Board the log raft (G:182) | skipped: goto-chalice (::goto 2603,9910) | SKIP-CHEAT (::goto teleport) | T:385; content WL:193-200 [oploc1,lograft_waterfall_quest] |
| 4 | Use rope on the rock (G:184) | skipped (same ::goto) | SKIP-CHEAT | T:385; content WL:249-255 [aplocu,crossing_rock_waterfall_quest] |
| 4 | Use rope on the dead tree (G:186) | skipped (same ::goto) | SKIP-CHEAT | T:385; content WL:289-297 [oplocu,overhanging_tree1_waterfall_quest] |
| 4 | Enter the falls (ledge door) (G:188) | skipped (same ::goto) | SKIP-CHEAT | T:385; content WL:299-305 [oploc1,waterfall_ledge_door] |
| 4 | Search the crate for a key (G:190) | skipped (same ::goto) | SKIP-CHEAT | T:385; content WL:397-405 [oploc1,baxtorian_crate_waterfall_quest] |
| 4 | Use key on the door to the chalice room (G:191) | skipped (same ::goto) | SKIP-CHEAT | T:385; content WL:418-430 [oplocu,baxtorian_door_2_waterfall_quest] |
| 4 | Plant the consecrated seed (G:193) | chalice.tileProbe, chalice.locProbe, plantSeed (inv_op seed op1), quest.stage.seed_planted | DRIVEN | T:385-450; L:31-35 PASS; RE/rovingelves_seed.rs2:9-27 |
| 5 | Return to Islwyn (G:195) | talk.islwyn2 + -dialog, quest.varp_complete, scroll, points, rewards | DRIVEN | T:463-525; L:37-44 PASS; RE/rovingelves_islwyn.rs2:71-97 |

Summary: every Roving Elves stage write is driven for real, including an actual Moss Guardian fight (with 99 combat stats and food), but all seven traversal legs -- the tombstone entry and the whole raft/rope/rope/ledge-door/crate-key/door route into the Chalice room -- are replaced by ::goto teleports even though quest_waterfall_locs.rs2 implements each of them. None of those legs writes %rovingelves_quest (T:1-16 argues they belong to Waterfall Quest), so the gap is traversal/mechanism coverage, not stage coverage; collapsed to "enter tomb" + "reach chalice room" it would be 2 skipped legs.

### quest_scorpcatcher scorpcatcher helper=scorpioncatcher/ScorpionCatcher.java

COUNTS guide_steps=9 driven=7 skipped_content=0 skipped_cheat=2 skipped_other=0 verdict=TEST GAP first_gap=get dusty key + unlock deep Taverley gate (bypassed by ::goto)

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver, T=R/test/quests/scorpcatcher.lua, C=R/OSRS-Content/osrs239-content/server/scripts. Ledger: 34/34 PASS (quest_tests/scorpcatcher/ledger.tsv).
Setup: ::clearinv, ::setlevel prayer 31 (T:16-17; the 31 Prayer is a general requirement, not a quest leg). Every travel row is t.player.goto_tile = a ::goto teleport (docs/QUEST_AUTHORING.md:161).

| guide varp | guide step (short) | test row(s) or cheat | class | evidence |
|---|---|---|---|---|
| 0 | Talk to Thormac, top of Sorcerer's Tower (ladders merged) | goto-thormac, talk.thormac1, talk.thormac1-dialog, quest.stage.started | DRIVEN | T:39-55; C/areas/area_seers/scripts/thormac.rs2 [opnpc1,thormac] |
| 1 | Talk to a Seer ("I need to locate some scorpions") | goto-seer1, talk.seer1, talk.seer1-dialog, quest.stage.first_hint | DRIVEN | T:58-72; C/areas/area_seers/scripts/seer.rs2:20-37 sets first_hint |
| 2/3 | Enter Taverley Dungeon; get the dusty key (jailer -> jail key -> Velrak, or 70 Agility pipe / 80 Agility floor) and unlock the deep-dungeon gate | none -- goto-scorpiona teleports straight into the scorpion room (2877,9796) | SKIP-CHEAT | T:76 (::goto via goto_tile). Content has the gate: C/areas/taverly/dungeon/scripts/jail_doors.rs2:17-24 (deepdungeondoor needs dusty_key), :29-32 ("This gate is locked."); velrak_the_explorer.rs2:8-24 gives dusty_key; drop_tables/scripts/jailer.rs2:13 drops jail_key. Test banner T:4-6 claims "no Taverley Dungeon traversal gates the scorpions" -- true of the npc trigger, false of the map. |
| 2/3 | Search the Old wall | search.oldwall | DRIVEN (out of order) | T:92; C/quests/quest_scorpcatcher/scripts/scorpcatcher_scorpions.rs2:13-20. Clicked AFTER the catch; the teleport already put the player inside the room the wall opens, so the search gated nothing. |
| 2/3 | Use cage on Taverley scorpion | catch.scorpiona, catch.scorpiona.inv | DRIVEN | T:81-88; scorpcatcher_scorpions.rs2:31-32, 45-58 |
| 2/3 | Enter Edgeville Monastery (ask Abbot to join the order, climb ladder) | none -- goto-scorpionc teleports to 3058,3488 plane 1 | SKIP-CHEAT | T:112. Content has the gate: C/areas/monastery/scripts/prayer_guild.rs2:14-26 (monasteryladder refuses while %prayer_guild<1), :28-41 (join-the-order choice sets %prayer_guild=1). |
| 2/3 | Use cage on Monastery scorpion | catch.scorpionc, catch.scorpionc.inv | DRIVEN | T:116-119; scorpcatcher_scorpions.rs2:37-38 |
| 2/3 | Enter Barbarian Outpost + use cage on Outpost scorpion (gate is walking; no barbariangate trigger exists in C) | goto-scorpionb, catch.scorpionb, catch.scorpionb.inv | DRIVEN | T:123-130; scorpcatcher_scorpions.rs2:34-35 |
| 3 | Return to Thormac with the full cage | goto-thormac2, talk.thormac2, talk.thormac2-dialog, quest.varp_complete | DRIVEN | T:143-152; thormac.rs2:20-25 queues scorpcatcher_quest_complete |

Extra (not a guide step): the port adds a second Seer visit that moves first_hint -> second_hint (seer.rs2:71-72); the test drives it (talk.seer2, T:96-108).

Summary: every quest-varp transition and all three catches are driven through real clicks, but the test teleports past two content-implemented access gates -- the dusty-key deep Taverley gate (jail_doors.rs2:17-32, with Velrak/jailer providing the key) and the Monastery's join-the-order ladder (prayer_guild.rs2:14-41). The Old wall search is clicked but after the teleport already placed the player behind it.

### quest_tearsofguthix tearsofguthix helper=tearsofguthix/TearsOfGuthix.java

COUNTS guide_steps=7 driven=3 skipped_content=2 skipped_cheat=2 skipped_other=0 verdict=MIXED first_gap=enter Lumbridge Swamp caves (rope) / Juna's cave -- bypassed by ::tearsofguthix teleport

Paths: R=/Users/matthewevers/Documents/git_repos/3draster-quest-driver, T=R/test/quests/tearsofguthix.lua, Q=R/OSRS-Content/osrs239-content/server/scripts/quests/quest_tearsofguthix/scripts, C=R/OSRS-Content/osrs239-content/server/scripts. Ledger: 16/16 PASS (quest_tests/tearsofguthix/ledger.tsv).
Setup: ::clearinv, ::tearsofguthix, ::setlevel firemaking 49 / crafting 20 / mining 20 (T:38-42). ::tearsofguthix (Q/tearsofguthix.rs2:183-189) sets %qp=43 (:184), resets the stage (:185), GRANTS tog_stone (:186, the quest-mined magic stone) and chisel (:187), and p_teleports to ^tog_juna_stand (:188). The helper's bring-alongs are rope, lit sapphire lantern, tinderbox, chisel and pickaxe, so the chisel is legitimate; tog_stone is not.

| guide varp | guide step (short) | test row(s) or cheat | class | evidence |
|---|---|---|---|---|
| 0 | Use rope on / enter the Lumbridge Swamp hole | none -- ::tearsofguthix teleport | SKIP-CONTENT | The ToG rope leg does not exist: C/quests/quest_anothersliceofham/scripts/slice_sergeants.rs2:62-67 is the only [oploc1,goblin_cave_entrance], and it ropes only under %slice_quest=^slice_infiltrate. Otherwise it just ~climb(-1)s, and there is no [oplocu,goblin_cave_entrance]. |
| 0 | Enter the cave (tog_cave_down) to Juna's room | none -- ::tearsofguthix teleport | SKIP-CONTENT | C/quests/quest_deathtothedorgeshuun/scripts/dttd_savezanik.rs2:37-40: the only [oploc1,tog_cave_down] refuses ("You have no reason to enter this tunnel.") unless %dttd_main=^dttd_zanik_saved. A ToG player cannot reach Juna without the debugproc teleport. |
| 0 | Talk to Juna ("Okay...") | tog.greet, tog.accept_dialog, quest.stage.need_bowl | DRIVEN (43 QP req forced by debugproc) | T:74-97; Q/tearsofguthix.rs2:66-79, :147 |
| 1 | Use lit sapphire lantern on a light creature to cross the chasm | none -- no lantern, stone pre-granted | SKIP-CHEAT | Content has it: Q/tearsofguthix_lantern.rs2:104-129 ([opnpcu,tog_light_creature*] -> p_teleport across the chasm). Lantern making: tearsofguthix_lantern.rs2:6-90. Test T:5-12 says it deliberately skips the "lantern-across-the-chasm mining minigame". |
| 1 | Mine a magic stone rock | none -- tog_stone from ::tearsofguthix | SKIP-CHEAT | Content has it: Q/tearsofguthix_lantern.rs2:131-151 (gated on need_bowl + Mining 20, inv_add tog_stone). Cheat: Q/tearsofguthix.rs2:186; T:39, T:68 (setup.have_stone). |
| 1 | Use chisel on the magic stone | tog.make_bowl, tog.bowl_crafted | DRIVEN | T:106-115; Q/tearsofguthix.rs2:150-160 |
| 1 | Talk to Juna with the bowl | tog.handin, tog.handin_dialog, quest.varp_complete, reward.crafting_xp(1000) | DRIVEN | T:128-151; Q/tearsofguthix.rs2:91-101 |

Summary: only the dialogue, the chisel craft and the hand-in are driven. The content does implement the chasm crossing and the stone mining (tearsofguthix_lantern.rs2:104-151), but the test skips both through the ::tearsofguthix debugproc's tog_stone grant and teleport. Even with the test fixed, the route in is a content gap: the swamp rope is only wired for Another Slice of Ham, and the only tog_cave_down trigger refuses anyone outside Death to the Dorgeshuun (dttd_savezanik.rs2:37-40), so Juna is unreachable without the teleport. The 43-QP requirement is also forced by the debugproc (tearsofguthix.rs2:184).
