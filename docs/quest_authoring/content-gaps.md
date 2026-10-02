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
