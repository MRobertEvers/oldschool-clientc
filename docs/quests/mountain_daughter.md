# Mountain Daughter (mountaindaughter) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). This brief
documents the port only; it changes no content. PARITY.tsv: `done` at
7e91c06fe3 (parity2b).

The wiki infobox dates Mountain Daughter to **7 March 2005**. The parity2b
note's "2015" is wrong. LostCity does not have the quest. The port is built
from the OSRS wiki and Quest Helper's `mountaindaughter/MountainDaughter.java`.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Mountain Daughter](https://oldschool.runescape.wiki/w/Mountain_Daughter?oldid=15292291) | 15292291, 2026-08-10 | Requirements, Walkthrough, Rewards |
| [Mountain Daughter/Quick guide](https://oldschool.runescape.wiki/w/Mountain_Daughter/Quick_guide?oldid=15288828) | 15288828, 2026-08-06 | Checklist order |
| [Transcript:Mountain Daughter](https://oldschool.runescape.wiki/w/Transcript:Mountain_Daughter?oldid=15263308) | 15263308, 2026-07-14 | Hamal / Asleif / Jokul / Svidi / Brundt / Kendal / Ragnar lines |
| [The Kendal](https://oldschool.runescape.wiki/w/The_Kendal?oldid=15199460) | 15199460, 2026-04-28 | The boss: level 70, 50 hitpoints, max hit 9, crush, attack speed 4, aggressive |
| Quest Helper `mountaindaughter/MountainDaughter.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` 0..60 -> complete 70 |

Cache: `configs/all.dbrow [quest_mountaindaughter]` (endstate 70, 2 QP).
Progress is `%mdaughter_quest_var`. The sub-tasks are
`%mdaughter_relations_var`, `%mdaughter_food_var` and
`%mdaughter_burial_state`.

## 2. Requirements (wiki infobox)

- Agility 20, boostable. The footnote says the first entry into the camp
  needs 10.
- The ability to defeat a level 70 monster. It can be safespotted.
- Items: rope, pickaxe, axe, plank; a staff or pole (the pole is found
  during the quest; the Dramen staff does not work); gloves (not slayer,
  ranger, moonclan, lunar, vambraces, mystic or random-event gloves).

## 3. Stages (`%mdaughter_quest_var`; Quest Helper `steps.put`)

The port's values match Quest Helper's keys.

| Value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 (entry) | 0 enteringTheCamp | Rope on the boulder above the guard, then climb down into the camp | `mountaindaughter_camp.rs2 [oplocu,mdaughter_cliff_boulder]` |
| 0 -> 10 `mdq_started` | 0 -> 10 | Hamal the Chieftain: "Why is everyone so hostile?"; after this the rockslide can be climbed | `[label,mdq_hamal_offer]`; rockslide `[oploc1,mdaughter_rockslide]` (ungated in the port, section 7) |
| 10 (route to the pool) | 10 speakToSpirit | Mud from the roots on the tall tree; climb it; pole or staff on the rock clump; plank on the flat stone | `[oploc1,mdaughter_roots_1]` / `[oploc1,mdaughter_swampbubbles1]`, `[oplocu/oploc3,mdaughter_lake_tree]`, `[oplocu,mdaughter_polerocks]`, `[oplocu,mdaughter_flatstone1/2]` |
| 10 -> 20 `mdq_spirit_heard` | 10 -> 20 | Listen-to the Shining pool: Asleif asks for peace with Rellekka and a food source | `mountaindaughter_spirit.rs2 [oploc1,mdaughter_sulphar_gas]` -> `[label,mdq_spirit_first_listen]` |
| 20 (relations 10..60) | 20 helpTheCamp | Hamal about Rellekka (10); Svidi in the forest (30, "Can't I persuade you to go in there somehow?"); Brundt wants the ancient rock; pickaxe on the ancient rock (40); Brundt's guarantee (50); guarantee to Svidi (60). The port inserts Jokul as relations step 20, between Hamal and Svidi (section 7) | `[label,mdq_hamal_topic_diplomacy_start]`, `[label,mdq_jokul_talk]`, `[label,mdq_svidi_talk]`, `[proc,mdq_brundt_dialogue]`, `[oplocu,mdaughter_ancient_rock]`, `[label,mdq_svidi_guarantee]` |
| 20 (food 10..20) | 20 helpTheCamp | Jokul: the white pearl. Wear gloves, pick the fruit on White Wolf Mountain, eat it to get the seed, give the seed to Hamal | `[label,mdq_hamal_topic_food]`, `[oploc3,mdaughter_white_pearl_bush]`, `[opheld3,mdaughter_white_pearl_fruit]` |
| 20 -> 30 `mdq_ready_for_kendal` | 30 talkKendal | Back to the pool: Asleif asks you to find what happened to her (needs relations done and food 20) | `mountaindaughter_spirit.rs2 [label,mdq_spirit_check_progress]` |
| 30 -> 40 `mdq_kendal_found` | 40 killKendalStep | Chop the dead trees; enter the cave; talk to the Kendal ("It's just me, no one special" / "You mean a sacrifice?"; unmask him; threaten him) | `mountaindaughter_kendal.rs2 [oploc1,mdaughter_caveentrance]`, `[opnpc1,mdaughter_multi_bear]` -> `[label,mdq_kendal_intro]` |
| 40 -> 50 `mdq_kendal_killed` | 50 returnTheCorpse | Kill the Kendal; take the Bearhead and Asleif's corpse | `mountaindaughter_kendal.rs2 [ai_queue3,mdaughter_bearman_fighter]` |
| 50 -> 60 `mdq_corpse_given` | 60 buryCorpse | Show Hamal the corpse | `mountaindaughter_camp.rs2 [label,mdq_hamal_corpse]` |
| 60 (burial) | 60 | Ragnar gives Asleif's necklace; bury the corpse on the pool island (`%mdaughter_burial_state` 1); collect 5 muddy rocks | `mountaindaughter_burial.rs2 [label,mdq_ragnar_necklace]`, `[opheld3,mdaughter_daughter_corpse]` |
| 60 -> 70 `mdq_complete` | complete | Use 5 muddy rocks on the burial mound (cairn) | `mountaindaughter_burial.rs2 [oplocu,mdaughter_burialmound]` -> `[proc,mdq_quest_complete]` |

## 4. The boss fight: the Kendal

Wiki (The Kendal; Walkthrough "The Kendal"):

- Level 70, 50 hitpoints, max hit 9. Crush, attack speed 4, aggressive.
- Protect from Melee blocks all of his damage. With Ranged or Magic you can
  safespot him around the skeletons.
- Threaten him and he attacks. When he dies, a Bearhead mask appears in your
  inventory, and Asleif's corpse can be taken from the south-east corner of
  the pillars.
- A cannon works. If you are safespotting, "after attacking the boss,
  another one will spawn".

Port: `[proc,mdq_spawn_kendal]` `npc_add`s `mdaughter_bearman_fighter` at
`^mdq_kendal_coord` with lifetime `^mdq_kendal_lifetime` (32000), owner set,
mode `applayer2`, and runs a `[timer,mdq_kendal_monitor]`.
`[opnpc2]`/`[apnpc2]` jump to `@player_combat_start(_ap)` behind the
`~mdq_attack_kendal` gate (seam20 trap 31). `[ai_queue3]` writes 50 and
`obj_add_private`s bones, the Bearhead (`~mdq_give_bearhead`: straight into
the inventory, or dropped if the inventory is full) and the corpse. DRIVEN
in parity2b (`parity_mountaindaughter` 14/14): the attack bar went 30->29,
the `await_dead_engaged` kill was corroborated by a zero bar,
`mdaughter_quest_var` read 50 ten ticks after the corpse appeared, and the
bearhead landed from the death handler.

## 5. Rewards (wiki Rewards)

- 2 quest points.
- 2,000 Prayer XP and 1,000 Attack XP. The port uses `stat_advance` 20000 /
  10000.
- The Bearhead.
- You can cross into the Mountain Camp over the rockslide.
- The Kendal is available in the Nightmare Zone.

## 6. Leftovers (PARITY.tsv `legs_left`, 7e91c06fe3)

PARITY.tsv's `legs_left` is empty and the row is `done`. Its notes say the
lake raft, the diplomacy chain, the pearl and the cairn "matched the guide
leg by leg".

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

- **The dead trees are one click.** The Walkthrough says to chop, with an
  axe, each dead tree that blocks the path to the cave ("stand to the west
  side of the first tree when chopping it down to walk over the stump").
  The port's `[oploc1,mdaughter_caveentrance]` checks
  `~woodcutting_axe_checker` and teleports you straight into the cave, so
  there are no tree locs to chop.
- **The corpse drops where the Kendal dies.** The wiki puts Asleif's corpse
  in the south-east corner of the pillars. The port `obj_add_private`s it on
  the Kendal's death tile.
- **Jokul gates the peace talks.** In the Walkthrough, Hamal sends you to
  Svidi about Rellekka, and Jokul belongs to the food thread ("talk to
  Jokul ... White pearl"). In the port, `[opnpc1,mdaughter_jokul]` answers
  only at `%mdaughter_relations_var = 10`, and Jokul himself writes
  relations 20 (`^mdq_relations_jokul_done`) and sends you to Svidi. That
  makes Jokul a step in the diplomacy chain.
- **The rockslide is open from the start.** The Walkthrough says the
  rockslide can be climbed only after Hamal lets you look for his daughter
  ("From this point on, you can enter the camp by climbing over the
  rockslide"). The port's `[oploc1,mdaughter_rockslide]` teleports you into
  the camp at any stage.
