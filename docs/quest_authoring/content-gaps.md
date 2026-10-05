# Content gaps reviewers named (section 8)

Declare a content gap with a `-- GUIDE-GAP:` marker citing the `.rs2` line (trap 32); never drive
around one.

## Content gaps reviewers named in sonnet-b31

*Origin: section 8 ("Gaps reported by authors").*

CONTENT GAPS REVIEWERS NAMED IN sonnet-b31 (declare them, never drive around them): The Feud's
`buyDisguiseGear`/`createDisguise` (`feud_recruitment.rs2:218` hands over a FINISHED
`feud_desert_disguise` at the heist briefing; no headpiece-plus-beard combine exists),
`blackjackVillager` (Lure/Knock-out narrated, `feud_recruitment.rs2:292`) and `givenDungToHag`
(`feud_traitor.rs2:166`). Shadow of the Storm's Evil Dave clothing check, exit portal, recruit legs,
golem implement and second ritual were ported in seam26 (`shadowstorm_ritual.rs2`,
`golem_portal.rs2`).

## Ported in seam27, and the One Small Favour gaps named before it

*Origin: section 8 ("Gaps reported by authors").*

PORTED IN seam27 (The Feud's disguise combine, blackjack lesson and three Hag talks; One Small
Favour's eight landing lights, three-step vane, Horvik's bars/medicine/pigeon cages, Tassie's clay
and Aggie's stage; Mountain Daughter's lake crossings and dead trees; Ghosts Ahoy's stepping stones
and Energy Barrier), so the gaps below are history.

### Content gaps reviewers named (One Small Favour)

CONTENT GAPS REVIEWERS NAMED (One Small Favour, `quest_onesmallfavour/scripts/`; declare them, never
drive around them): `fixAllLamps` narrated by a mesbox that sets the eight light vars itself
(`onesmallfavour_relay.rs2:333`); `useHammerOnVane`/`searchVaneAgain` collapsed into one hammer
mesbox (`onesmallfavour_puzzles.rs2:114`); `talkToAggie` (`onesmallfavour_relay.rs2:20`); Tassie
hands an empty pot with no soft clay (`onesmallfavour_relay.rs2:52-59`); Horvik never takes the
pigeon cages and hands chicken cages for the salts instead
(`areas/varrock/scripts/horvik.rs2:20-33`, `talkToHorvikFinal`), and pigeons spawn only three at a
time.

## Dragon Slayer: the guild master's questions and the Oracle's magic door (sonnet-b36; FIXED seam31)

*Origin: the Dragon Slayer author and reviewer (batch sonnet-b36).*

- `guild_master.rs2:4` has no quest questions after stage 0. `returnToGuildmaster` (the guide asks
  him where the map pieces are) can only reach "What is this place?". Grade it as a GUIDE-GAP that
  cites the line, not as a PASS on a generic talk.
- `magic_door.rs2` has only `oploc1`. The door takes all four items (silk, lobster pot, unfired
  bowl, wizard's mind bomb) in one click, so the guide's four use-on steps (`useSilkOnDoor`,
  `usePotOnDoor`, `useUnfiredBowlOnDoor`, `useMindBombOnDoor`) are four GUIDE-GAPs.
  `helper_coverage` stays at CONTENT_GAP=4 until the door gets one `[oplocu]` per item.

FIXED seam31 (both bullets; sources Quest Helper DragonSlayer.java, 2009scape
GuildmasterDialogue.java / DSMagicDoorPlugin.java and the OSRS wiki brief
`docs/quests/dragon_slayer_i.md` s5/s8 -- LostCity has neither):

- The Guildmaster at stage 1 asks "Have you gone to talk to Oziach yet?". From stage 2 until the
  quest is done he offers "What is this place?" / "About my quest to kill the dragon...". Drive it as
  `chat.play{"npc:Greetings!", "options", "choose:About my quest to kill the dragon...",
  "player:About my quest"}`, then `chat.drain{stop_at="options"}` + `chat.choose` per topic. The
  route topic opens a second menu (Melzar / Thalzar / Lozar); Melzar's answer hands over a maze key
  only when you own none. Each topic raises its knowledge flag, never lowers it.
- The magic door takes one item per use, in any order: `t.player.use_on` each of `silk`,
  `lobster_pot`, `bowl_unfired`, `wizards_mind_bomb` ("You put <item> into the opening in the
  door."). The fourth opens it ("The door opens...", `%dragon_oracle` = 3) and walks you through.
  A plain Open answers "The door is locked." until then. Server varp `%dragon_door_items` holds the
  four bits.

## Rewards that are scroll text only, a finale cutscene not built, a start npc with no spawn (sonnet-b42)

*Origin: the sonnet-b42 authors and reviewers (deserttreasure, gardenoftranquility, giantdwarf).*

- DESERT TREASURE'S RING AND SIGNET ARE ONLY SCROLL TEXT. `deserttreasure.rs2:1957` lists "Ancient
  signet from Eblis" and "Ring of visibility" in `~quest_complete_rewards`, but nothing in
  `quest_deserttreasure` calls `inv_add` for either. A `t.inv.await` on them times out; a reward row
  that reads only the scroll line is not an item reward. Report it as a content gap; assert the
  items the script does give.
- GARDEN OF TRANQUILLITY'S FINALE HAS NO CUTSCENE. `garden_statues.rs2:15-16` says the real game's
  Roald garden tour and Falador guard distraction are a cutscene that "is not built here". The quest
  completes inside King Roald's throne-room dialogue (`talkToRoald`), so there is no `cutscene:`
  row to write and `cutscene_row_required` does not apply.
- THE GIANT DWARF'S PRE-QUEST BOATMAN HAS NO SPAWN ROW. `dwarf_city_boatman_mines_prequest` is
  placed nowhere; the only placement is `dwarf_city_boatman_mines` (`areas/world/configs/m44_158.spawn`).
  `gdwarf_start.rs2:11-14` sends that npc's op1 to the pre-quest proc while
  `%giantdwarf_quest = ^gdwarf_not_started`, so talk to `dwarf_city_boatman_mines` to start.

## Desert Treasure: Malak has no "How can I kill Dessous?" option; Damis's true form under the wanderers' claim (sonnet-b46)

*Origin: author batch sonnet-b46 (deserttreasure leg 3).*

- MALAK HAS NO "HOW CAN I KILL DESSOUS?" ROW. Quest Helper's `askAboutKillingDessous` chooses that
  option, but `[opnpc1,fourdiamonds_vampire_lord]` (`deserttreasure.rs2:444`) opens no menu once
  `%dt_blood_stage >= ^dt_blood_agreed`. The repeat talk says "Why are you still here? I notice
  Dessous still lives." and repeats the silver-bar instructions (`deserttreasure.rs2:489-490`).
  Drive the step as that repeat talk and say in a comment that the option does not exist; a
  `choose:How can I kill Dessous?` entry only times out.
- DAMIS'S TRUE FORM REFUSED EVERY ATTACK. In the Shadow Dungeon, each Attack on `fd_damis_tougher`
  answered "I'm already under attack." while the dungeon's aggressive wanderers held the single-way
  claim. `::passive` on the four wanderer types (`sword_skeleton_3`, `sword_skeleton_3b`,
  `shadow_dog_wild`, `small_bat`) let the swing land (gaps-combat: `::passive <npc_symbol>`). Never
  make Damis passive: he must still fight back and die for real.

## The Lost Tribe: the maze trap puts the candle lantern out and nothing relights it (matthew-mbp-m4-b47)

The Lost Tribe's floor trap drops the player into the Lumbridge Swamp Caves and turns
`candle_lantern_lit` into `candle_lantern_unlit` (`losttribe_tunnels.rs2`). `[opheldu,tinderbox]`
(`skill_firemaking/scripts/firemaking.rs2:25-55`) has cases for lit arrows, logs, jogre bones, the
black candle, the sapphire lantern and Olaf's planks, but none for a candle lantern. A tinderbox on
the unlit lantern answers "Nothing interesting happens." The tunnels never test for a light either,
so a test that falls in walks on with the lantern out. Stay on the marked path; report a relight
only if a quest needs one.

## `no_row <npc>` for a world npc the guide names: no spawn row at all (Zembo, matthew-mbp-m4-b49-seam1)

Symptom: `t.npc.nearest("zembo", 15)` answers `no_row zembo (id 13655, searched N npc(s) within 15)`
at the guide's WorldPoint, and grep finds the symbol in no `.spawn` file. The generated squares
(`areas/world/configs/m*.spawn`, `gen_spawns.py`, do-not-hand-edit) dropped him. The fix is content
ported from its source, kept in a quest-local `.spawn` the generator cannot clear (the
`quest_prince.spawn` precedent): Tai Bwo Wannai Trio's Zembo is `quest_tbwt/configs/tbwt_zembo.spawn`
(2925,3143, LostCity m45_49.jm2), `tbwt_zembo.inv` `[boozeshop]` and `tbwt_zembo.rs2` (LostCity
`zambo.rs2`). Drive him with `talk_to` + `choose:Yes please.` + `t.shop.attach("boozeshop")`, or
`t.shop.open("zembo", 3, "boozeshop")`; rum is 30 coins (conformance row
`seam.zembo_boozeshop_sells_rum`).

## Tai Bwo Wannai Trio: loading the karambwan vessel takes ONE karambwanji (FIXED b49-seam2)

Symptom before the fix: after `fillVessel` the backpack had no raw karambwanji left, so a second
load or `makeKarambwanjiPaste` found none. `[proc,tbwt_load_karambwan_vessel]` cleared the
karambwanji's slot with `inv_delslot`, and this cache's `tbwt_raw_karambwanji` is `stackable=1`, so
one load ate the stack. LostCity's code is the same, but its karambwanji is not stackable
(`fishing.obj`), so there the slot was one fish. The OSRS wiki ("Raw karambwanji", oldid 15184350)
loads one. The proc now does `inv_del(inv, tbwt_raw_karambwanji, 1)`: 5 -> 4 -> 3 in both use
orders (conformance row `seam.tbwt_vessel_loads_one_karambwanji`). Leftover karambwanji stay in the
pack, so a test no longer has to net again before each load.

## Enlightened Journey: no willow branch source in the pack; check a hand-in's source before you `::give` it (matthew-mbp-m4-b50; FIXED matthew-mbp-m4-b50-seam1)

**FIXED (matthew-mbp-m4-b50-seam1).** Every hand-in now has an in-game source, and the round-1 file
with each mid-run `::give` replaced by a driven gather ran 221/221 to `quest.varp_complete`
(`build/quest_gate/s1_ej_copy`). New content in `skill_farming`: willow saplings and Auguste's
sapling (`zep_plantpot_willow_sapling`) grow in tree patches (`farming_trees` rows
`farming_tree_willow*`; wiki "Willow tree (Farming)" oldid 15287151), and secateurs used on a grown,
health-checked willow cut its branches, one per 5 minutes up to 6 (`farming_tree.rs2`; wiki "Willow
branch" oldid 15184331; conformance `seam.willow_tree_grows_branches`). Fill on an empty sack makes
Potatoes(10) (`farming_sacks.rs2`; wiki "Empty sack" oldid 15183845; conformance
`seam.vegetable_sack_fill`). Empty sacks are Sarah's (`farming_shopkeeper_1`), redberries Wydin's,
the dyes Aggie's, silk the Al Kharid silk trader's, the bowl a spawn up the ladder in Auguste's
house. Every route, tile and op: `test/quests/wip/enlightenedjourney/relay.md`. Not built: the
sack's Remove-one/Empty ops, paying a gardener to protect the tree, stump regrowth.

The finding as the b50 reviewer wrote it:

*Origin: the b50 reviewer rejected enlightenedjourney for `::give`s of items the guide has you get.*

Nothing in the content pack hands out `willow_branch`. A grep finds it only in the quest's own
`ej_crafting.rs2` and `enlightenedjourney.constant` and in `skill_crafting` weaving. No farming
harvest gives branches. So QH's "Get 12 willow branches" (`talkToAugusteWithBranches`) has no
source to drive. A `::give willow_branch 12` grades as a cheat. The file must stop at `t.blocked`
with a `content_bug` that names the missing source. The ladder notes say the same thing
(`docs/quests/ladders/enlightenedjourney.notes.md:15`).

The quest's other hand-ins, the potato sacks, red and yellow dye, silk and the bowl, were
`::give`n too. Each is the same finding: the guide has you obtain them. Silk has a source in the
world (`areas/alkharid/scripts/silk_trader.rs2`). The b50 reviewer did not trace the dye, sack,
potato or bowl sources. Grep the content for the `inv_add` before you write that leg. Do not
assume an item has no source.


## A quest loc absent from `maps/*.jl2` may be absent from the real cache too (Eagles' Peak Pedestal, matthew-mbp-m4-b50-seam1)

*Origin: the b50 parity closer proposed a map row for the net-trap Pedestal (loc 19980) in
`m30_76.jl2` and deleting the runtime stand-up.*

The real game's own rev 239 cache places no Pedestal either: `cachepack unpack --cache
cache.osrs239 --rev osrs239 --assets=maps` gives an `m30_76.jl2` byte-identical to OSRS-Content's,
and no map square places locs 19980-19984. So the game stands it up server-side, and the port's
`eaglepeak_bronze_ensure_pedestal` (a `loc_add` guarded by `loc_find` on all three forms) is the
faithful mechanism; `bronze_room.rs2` now says so. Check the pristine cache before proposing a map
row. Quest runs and JS5 read the pristine `cache.osrs239` (run.py `write_manifest`), never the
`make torirsserver-cache` bake (`cache.osrs239.baked`), so a map row would be inert in a run anyway.

## The Queen of Thieves: an uncooked stew cannot be cooked, so the stew is bought (matthew-mbp-m4-b53)

O'Reilly wants a bowl of stew (Quest Helper `talkToOReilly`, item "Stew"). Making one gets as far
as `uncooked_stew` (`skill_cooking/scripts/cooking_inv/scripts/stew.rs2`). But
`skill_cooking/configs/cooking_generic.dbrow` has no row for it, so a range answers "You can't cook
that." (`cooking.rs2` `[proc,attempt_cook]`, the `db_find(cooking_generic:uncooked, ...)` miss).
The stew is a bring-along, so buying it is allowed: the Shayzien barman (`shayzien_barman`, op 3,
shop `shayzien_pub`, The Cloak and Stagger, 1550,3560) sells one for 20 coins. The only setup
`::give` is the coins. Once a `cooking_generic` row exists, cook the stew instead.

## Only one of three `npc_add` ambush trolls appears (Swan Song, matthew-mbp-m4-b54; FIXED OSRS-Content f2902a94dd)

FIXED by seam pass matthew-mbp-m4-b54-seam1: the offsets were written as `movecoord(coord, 2, 1, 0)`
and `(-1, 2, 0)`, and `movecoord`'s middle argument is the LEVEL (LostCity ServerOps.ts:103-107),
so trolls 2 and 3 stood on levels 1 and 2. Neither `npc_add` nor the map was the cause. See
seam-facts: Seam pass matthew-mbp-m4-b54-seam1 (a); the rows are in `test/quests/wip/swansong/relay.md`.
The original finding:


`swansong_colony.rs2` `[proc,ssq_spawn_entrance_ambush]` (lines 119-123) calls `npc_add` three
times for `swan_troll_ambush`: on `^ssq_entrance_ambush_coord`, at +2,+1 and at -1,+2. The client
shows one troll (shots 030 and 032, and `t.npc.tiles` lists one copy), so `%varb2107` never
reaches 3 and stage 50 cannot be reached. The test stops at `t.blocked` with `content_bug`. Not
yet known: whether `npc_add` drops the two offset spawns or their tiles are blocked. Check that on
the server first (the spawn tiles in the map's collision, and a server-side npc count after the
proc). Do not work around it in the test.

## Swan Song's entrance ambush: 3 trolls where the wiki has 8, and they despawn 50 ticks after entry (matthew-mbp-m4-b54; despawn FIXED OSRS-Content 1ef7c1e7b9)

*The 50-tick despawn and the one-shot flag are FIXED (seam-facts: Seam pass matthew-mbp-m4-b54-seam3 (c)): the trolls stay until killed, and a re-entry puts back the ones still owed. The 3-of-8 gap below stands.*

This is a parity gap; the test does not need to work around it. The wiki says that inside
the Colony grounds "you will be attacked by eight (8) level 79 Sea trolls" (Swan_Song revid
15359363, "Battle at the Colony"). The port spawns three. `^ssq_trolls_needed` is 3, and the
counter `%varb2107_swansong_trolls` has only 2 bits, so it cannot count to 8 without a wider
varbit. Assert the port's 3 (`varb2107` reads 1, 2, 3, then stage 50). Note the gap in the
test header; do not invent five more kills. Two things follow from the port's version:

- `[proc,ssq_spawn_entrance_ambush]` calls `npc_add(..., 50)`, so each troll despawns 50 ticks
  after it spawns. `varb2111_swansong_ambush=1` stops a second spawn, so a fight that runs past
  about 50 ticks soft-locks stage 40. Kill the three without pausing; the measured run took
  about 34 ticks (`test/quests/wip/swansong/relay.md`).
- Malignius later wants 7 bones. With 3 ambush trolls, the rest have to come from later
  trolls. Only one later troll exists (the one the first fishing cast wakes), so the trolls
  leave 4 bones; see the next section for the other 3.

## Swan Song: Franklin gives no hammer, and the trolls leave 4 of Malignius's 7 bones (matthew-mbp-m4-b54; hammer FIXED OSRS-Content 1ef7c1e7b9)

*The hammer half is FIXED (seam-facts: Seam pass matthew-mbp-m4-b54-seam3 (d)): the hole no longer asks for one, and Franklin hands his over when you talk to him after lighting the firebox. Drop the general-store leg. The bones paragraph below stands.*

*Origin: the matthew-mbp-m4-b54 round-4 review.*

Quest Helper's hammer tooltip says "Franklin will give you one" (SwanSong.java:188). In the port
nobody gives a hammer: no Swan Song script has an `inv_add` for one. The colony hole also refuses
entry without it ("You'll want a hammer before heading in", `swansong_colony.rs2:109`), before you
can reach Franklin. Quest Helper lists the hammer on `enterColony` too. So the hammer is bought in
game, not given in setup: the Lumbridge general store (`generalshopkeeper1`, op 3, shop
`generalshop1`) sells one for 1 coin. Setup gives only the coins.

The bones tooltip says to pick them up from the sea trolls (SwanSong.java:154). The port has four
sea trolls before Malignius: the three ambush trolls and the one the first fishing cast wakes. Each
drops one `bones`, so three more are needed. Malignius asks for "the normal sort you get from
people and small monsters", so kill three chickens (Lumbridge farm, `chicken_brown`) and pick up
their bones. Do not `::give` bones.

## Content gaps reviewers named in matthew-mbp-m4-b55

*Origin: the matthew-mbp-m4-b55 reviews. Each line is a gap in the port, not in the test. Write it
in the row and in doc_gaps, and never `::give`/`::setvar` around it.*

- **The Hand in the Sand.** Rarve's sandpit cutscene is not ported. Entrana has no boat crossing and
  no check that bans weapons, so Mazion is reached by `goto_tile` (parity notes). (The Port Sarim
  monk's weapon search is FIXED: seam-facts, Seam pass matthew-mbp-m4-b59-seam1 (j).)
- **Meat and Greet.** Emelio's "Trade" and the Spice Merchant's "Let's trade" open nothing, because
  the shop stock files are not generated (PARITY.tsv row, `wiki_shop_owners.csv` regen).
- **The Eyes of Glouphrie.** Brimstail hands out discs only while the stage is below
  `^eyeglo_machine_ready` 35 (`eyeglo_quest.rs2:121-123`). He gives three random discs from an
  18-disc pool (`random(18)`, `eyeglo_quest.rs2:403`), and none while you carry six or more. After
  the front-panel unlock he has no disc line. So hold every disc the control panel needs BEFORE you
  unlock it, using the exchanger to swap leftovers. Check the wiki before you report this as a bug.
- **The Ascent of Arceuus.** Guide step `talkToArceuus` names Lord Trobin, but the content sends
  you to Asteros (`[opnpc1,asteros_arceuus_vis]`, `ascentofarceuus.rs2:220`). Trobin talks only
  at stage 13. The Favour reward and the Graceful recolour interface are not authored
  (`aoa_leftover_*`). The Karuulm elevator lands at 1311,10188, not at the guide's 1312,10211
  (Kaal's footprint).
- **Another Slice of H.A.M.** (FIXED b55-seam1, OSRS-Content f97d2dcd59: the digs are the trowel USED
  on the artefact and the cleaning an artefact used on the table; the route clicks end to end with
  `::complete quest_losttribe`, whose trigger is on the hole's multiloc child -> seam-facts: Seam
  pass matthew-mbp-m4-b55-seam1 (a)-(c).) `slice_artifact_hotspot_0N_1` (`configs/all.loc:256904`, "Artefact")
  has no op1, so the menu offers only Examine. `[oploc1,slice_artifact_hotspot_0N]`
  (`slice_tegdak.rs2:103-143`) never fires, and there is no `[oplocu]` for the trowel. Stage 2 to 3
  cannot be driven (content_bug), and the stage 3-11 tail is still to be driven after the fix.
  `[oploc1,cave_goblin_city_doorr]` refuses until Lost Tribe is complete
  (`lotg_intro.rs2:67-71`), so setup needs `::complete quest_losttribe`, which the scaffold
  leaves out. Nothing has a trigger on `lost_tribe_cellar_wall`. Guide steps 1.3 (Kazgar) and 1.4
  (`climbThroughHole`) are therefore travelled with `goto_tile` and graded as content gaps.

## A goto right after a fight hides a return teleport that never fired (H.A.M. watchtower, matthew-mbp-m4-b55 round 6; FIXED matthew-mbp-m4-b55-seam2)

*Symptom: the stage advances after a kill and a mesbox says you head back, but the next row's
`goto_tile` detail reads `from <the fight's tile>`. The content's own teleport did not run.*

- **Another Slice of H.A.M.** When both H.A.M. rangers die, `slice_ham_rangers_check`
  (`slice_hammage.rs2:110-115`) runs inside `[ai_queue3,slice_ham_archer]`. It shows "With both
  ambushers down, you make your way back to report to the Generals." and sets stage 7, but
  `p_teleport(^slice_generals_coord)` leaves the player on the tower top at 2447,5416,2. client.log
  then prints `npc_findhero with no active npc` at `[proc,npc_default_death]` from
  `slice_hammage.rs2:108`. The tower has no ladder down, so a real player is stranded there. The
  committed test hid this with `goto-generals2`. The fix belongs in content: `queue()` a player
  script for the mesbox, the teleport and the stage write, as seam-facts says for a `p_delay` in
  an npc script (seam pass 21 (b), seam 37 (d)).
- **For authors:** after a fight whose content should move you, read `t.world.tile()` and compare it
  with the destination before any goto. A goto there is only travel when the content never meant
  to move you.

**FIXED (matthew-mbp-m4-b55-seam2, OSRS-Content 2ca4e77a52; seam-facts: Seam pass matthew-mbp-m4-b55-seam2).** `slice_hammage.rs2`: the death handlers do the npc's half only
(flag, line, `~npc_default_death` with the npc still active) and `queue(slice_ham_rangers_return)` once
both are dead at stage 6; that player script shows the mesbox, teleports to 2957,3512,0 and writes
stage 7, and `slice_ham_combat_login` queues it again after a logout during the mesbox. The teleport
is sourced: the second kill starts the kidnap cutscene, which ends at the generals (wiki
Another_Slice_of_H.A.M. oldid 15292360; Transcript oldid 15263379 "Upon defeating the two H.A.M.
members"; Quest Helper `talkToGeneralsAgain` at 2957,3512,0 straight after `killHamMageAndArcher`).
Sigmund's death handler (`slice_sigmund.rs2`) had the same shape and now queues his parting line.
Measured: `b55s2_slice_a` (mage first) and `b55s2_slice_b` (archer first) 148/0, `killHam.returned`
`2957,3512,0` with no goto, no `npc_findhero with no active npc` in client.log.

- **The shape to grep for in any quest:** a `[ai_queue<n>,...]` that binds `npc_findhero` and then
  reaches `~mesbox`/`~chatnpc*`/`~chatplayer`/`p_delay` BEFORE `~npc_default_death`.
  `tools/check_npc_script_player_suspend.py` does not catch it (it flags only a suspend with no
  player bound). Queue the player's half; write any stage the kill decides in the npc's half or
  in the queued script, never after a page.
