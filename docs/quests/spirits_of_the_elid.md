# Spirits of the Elid (spiritsoftheelid) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). This brief
records the port and makes no content edits. PARITY.tsv: `done` at 340c190aae
(parity2c).

The wiki infobox dates the quest to **5 December 2005**. It is not in
LostCity. The port is built from the OSRS wiki and Quest Helper's
`spiritsoftheelid/SpiritsOfTheElid.java`, plus the cache's own dbrow and
varbit schema (see the header of `quest_spiritsoftheelid.constant`).

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Spirits of the Elid](https://oldschool.runescape.wiki/w/Spirits_of_the_Elid?oldid=15306897) | 15306897, 2026-08-19 | Requirements, Walkthrough, Rewards |
| [Spirits of the Elid/Quick guide](https://oldschool.runescape.wiki/w/Spirits_of_the_Elid/Quick_guide?oldid=15306724) | 15306724, 2026-08-19 | Checklist order |
| [Transcript:Spirits of the Elid](https://oldschool.runescape.wiki/w/Transcript:Spirits_of_the_Elid?oldid=15338971) | 15338971, 2026-09-10 | Awusah / Ghaslor / Shiratti / spirits / genie lines |
| [White golem](https://oldschool.runescape.wiki/w/White_golem?oldid=15338923) | 15338923, 2026-09-10 | Level 75, 80 hitpoints, max hit 4, crush, speed 4, aggressive; stab defence 1, every other defence 300 |
| [Grey golem](https://oldschool.runescape.wiki/w/Grey_golem?oldid=15338925) | 15338925, 2026-09-10 | Same, but slash defence 1 |
| [Black golem](https://oldschool.runescape.wiki/w/Black_golem?oldid=15338922) | 15338922, 2026-09-10 | Same, but crush defence 1 |
| Quest Helper `spiritsoftheelid/SpiritsOfTheElid.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` 0..55 -> complete 60 |

Cache: `configs/all.dbrow [quest_spiritsoftheelid]` (endstate 60, 2 QP,
requirements_boostable = 1). Progress is `%elidquest`.

## 2. Requirements (wiki infobox)

- Magic 33, Ranged 37, Mining 37 and Thieving 37, all boostable. The port's
  `[proc,elid_qualifies]` uses `stat()`.
- The ability to defeat three level 75 golems.
- Items: runes for Telekinetic Grab (a law and an air); a needle (a costume
  needle does not work) and 1-2 thread; crush, stab and slash weapons; a
  light source; a knife; a rope; a pickaxe; a Ranged or Magic attack. A
  bronze pickaxe, shortbow and bronze arrows spawn along the river.

## 3. Stages (`%elidquest`; Quest Helper `steps.put`)

The port uses Quest Helper's keys, and adds 5 for "started".

| Value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 -> 5 `elid_started` | 0 speakToAwusah | Awusah the Mayor, Nardah: the curse, no water | `elid_mayor.rs2 [label,elid_mayor_offer]` |
| 5 -> 10 `elid_ghaslor_done` | 10 speakToGhaslor | Ghaslor the Elder gives the ballad (keep it) | `elid_ghaslor.rs2 [label,elid_ghaslor_ballad]` |
| 10 -> 20 `elid_robes_key` | 20 getRobesAndKey | Shrine cupboards -> torn robe top + bottom; mend them with needle and thread; Telekinetic Grab the ancestral key | `elid_house.rs2 [proc,elid_search_cupboard]`; mending `[opheldu,elid_robetop_torn]` / `[opheldu,elid_robebottoms_torn]` (and `skill_crafting/.../leather.rs2`'s needle arms) |
| 20 -> 25 `elid_cave_entered` | 25 goUseKey | Rope on the root above the waterfall | `elid_dungeon.rs2 [oplocu,desert_water_cave_root]` |
| 25 -> 27 `elid_golems` | 27 clearChannels | Wear the Robe of Elidinis, carry the key and the ballad, open the door | `elid_dungeon.rs2 [oploc1,elid_underground_robe_door(_mirror)]` |
| 27 (`%elid_whitegolem`, `%elid_thievingchannel`) | 27 | South door: white golem (stab); disarm the spike trap (Thieving) | `[oploc1,elid_whitegolem_door]`, `[ai_queue3,elid_golem_white]`, `[oploc1,elid_water_channel_spiketrap]` |
| 27 (`%elid_greygolem`, `%elid_miningchannel`) | 27 | East door: grey golem (slash); mine the blocking rocks | `[oploc1,elid_greygolem_door]`, `[ai_queue3,elid_golem_grey]`, `[oploc1,elid_water_channel_blocked_rocks]` |
| 27 (`%elid_blackgolem`, `%elid_rangingchannel`) | 27 | North-east door: black golem (crush); hit the target across the water with Ranged or Magic | `[oploc1,elid_blackgolem_door]`, `[ai_queue3,elid_golem_black]`, `[opnpc2]/[apnpc2,elid_ranging_target(_multinpc)]` -> `[proc,elid_ranging_attack_gate]` |
| 27 -> 30 `elid_spirits_done` | 30 goSpeakToSpirits | Through the lake door (all three channels clear); talk to Nirrie, Tirrie and Hallak | `[oploc1,elid_underground_lake_door]`; `[opnpc1,elid_waterspirit*]` -> `[label,elid_spirits_talk]` |
| 30 -> 35 `elid_awusah_return` -> 40 `elid_shoes_phase` | 35 speakToAwusah2 -> 40 creviceSteps | Awusah: the statuette was thrown down the crevice; take his shoes by the door | `elid_mayor.rs2 [label,elid_mayor_reveal]` (writes both values) |
| 40 (sole) | 40 creviceSteps | Knife on the shoes -> sole | `elid_genie.rs2 [opheldu,elid_shoes]` -> `[proc,elid_cut_shoes]` |
| 40 (crevice) | 40 creviceSteps | Rope on the crevice west of town; a lit light source or "eaten alive by tiny bugs" | `elid_genie.rs2 [oploc1,elid_crevice_clickzone]`, `[proc,elid_has_light]` |
| 40 -> 50 `elid_genie_deal` | 50 talkToGenieAgain | The genie wants the mayor's sole | `elid_genie.rs2 [label,elid_genie_first_deal]` |
| 50 -> 55 `elid_statuette_phase` | 55 useStatuette | Give the sole; receive the statuette | `elid_genie.rs2 [label,elid_genie_take_sole]` |
| 55 -> 60 `elid_complete` | complete | Put the statuette on the plinth in the shrine | `elid_genie.rs2 [oplocu,elid_statuette_base]` -> `[proc,elid_quest_complete]` |

## 4. The fights: three golems

This quest has no single boss. Its three golem fights (Walkthrough "The
Golems"; the three golem pages) work like this:

- Each golem is level 75 with 80 hitpoints, max hit 4, crush, speed 4, and
  is aggressive. One golem appears when you try each door.
- Each has a defence of 1 against one style and 300 against everything else.
  White: stab. Grey: slash. Black: crush. The Walkthrough says each "can
  only be defeated by one type of attack style". A spear, hasta or bladed
  staff covers all three styles.
- If you open a door again before the golem's death animation ends, another
  golem spawns.

Port: every door `npc_add`s its golem for 50 ticks, and only while its
`%elid_*golem` bit is 0. Every `[opnpc2]` ends in `@player_combat_start`,
and every `[ai_queue3]` sets the bit. The style weakness is the cache npc's
defence table; there is no script gate. PARITY.tsv records that the golems
were "unchanged, not re-driven this pass". Proofs for the ranging channel
and the crevice: `parity_elid_ranging_final` 14/14, `parity_elid_light4`
7/7.

## 5. Rewards (wiki Rewards)

- 2 quest points.
- 8,000 Prayer, 1,000 Thieving and 1,000 Magic XP (the port:
  `stat_advance` 80000 / 10000 / 10000).
- Access to Nardah's fountain and to the Elidinis Statuette shrine. Praying
  there fills Hitpoints and temporarily boosts them, cures poison and venom,
  and restores prayer, run energy and special attack energy.
- The Robe of Elidinis.

## 6. Leftovers (PARITY.tsv `legs_left`, 340c190aae)

PARITY.tsv's `legs_left` is empty and the row is `done`. Its notes add:
"Golems: white stab, grey slash, black crush by defence (wiki), unchanged,
not re-driven this pass."

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

- **The robe door does not check for the ballad.** The Walkthrough says:
  "If you are not wearing the robes, or do not have the ballad, your
  character will ask what they could possibly have forgotten." The port's
  `[oploc1,elid_underground_robe_door]` checks the key and the worn robes
  only.
- **The spirits are gated by the stage, not the channels.**
  `[opnpc1,elid_waterspirit*]` needs only `%elidquest >= 27`. The lake door
  is what enforces "all three channels cleared".
- **The shrine statuette's prayer effect was not audited.** This pass did
  not check it (the post-quest reward above, and the Desert Diary task).
