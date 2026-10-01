# Rum Deal (rumdeal) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). This brief
documents the port; it changes no content. PARITY.tsv row: `partial` at
340c190aae (parity2c).

LostCity has no Rum Deal. Neither `LostCity_Content2/scripts/quests` nor
`LostCity_Server` has a `quest_rumdeal` or any rum quest directory (parity2c
checked this with a repo-wide find). So the port is built from the OSRS wiki
and Quest Helper's `rumdeal/RumDeal.java` + `SlugSteps.java`.
The wiki infobox dates the release to **31 October 2005**. The
"Jan 2005" in the parity2c notes and the batch brief is wrong.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Rum Deal](https://oldschool.runescape.wiki/w/Rum_Deal?oldid=15315444) | 15315444, 2026-08-21 | Requirements, Walkthrough, Rewards |
| [Rum Deal/Quick guide](https://oldschool.runescape.wiki/w/Rum_Deal/Quick_guide?oldid=15019725) | 15019725, 2025-11-08 | Checklist order, dialogue options (`Yes!` / `Of course, I fear no demon!` / `Nonsense! Keep the money!`) |
| [Transcript:Rum Deal](https://oldschool.runescape.wiki/w/Transcript:Rum_Deal?oldid=15263329) | 15263329, 2026-07-14 | Every Pete / Braindeath / Davey / Donnie line, the basement cupboard, Zombie swab intimidation, the Rabid Jack reveal |
| [Evil spirit](https://oldschool.runescape.wiki/w/Evil_spirit?oldid=15199641) | 15199641, 2026-04-28 | The boss: level 150, 90 hitpoints, max hit 28, crush melee only, attack speed 4, aggressive, safespottable |
| [Fever spider](https://oldschool.runescape.wiki/w/Fever_spider?oldid=15275482) | 15275482, 2026-07-25 | Level 49, 40 hitpoints, Slayer 42; without slayer gloves a hit does 12.5% of the player's Hitpoints level and causes disease; 11 spawn tiles in the brewery basement |
| Quest Helper `rumdeal/RumDeal.java` + `SlugSteps.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` 0..18 ladder, rewards (2 QP, 7,000 Fishing/Prayer/Farming XP) |

Cache: `configs/all.dbrow [quest_rumdeal]` (id 95, endstate 19, 2 QP,
startnpc `deal_pete`, the five stat requirements).

## 2. Requirements (wiki infobox)

- Quests: Zogre Flesh Eaters (which needs Big Chompy Bird Hunting and Jungle
  Potion -> Druidic Ritual) and Priest in Peril.
- Crafting 42, Farming 40, Prayer 47, Fishing 50 are boostable. Slayer 42 is
  not boostable and is needed to start. A footnote says the Fishing
  requirement is bypassed by getting 5 karamthulhu from zombie pirates.
- 47 available prayer points when Davey blesses the wrench. The blessing
  drains none of them (Walkthrough: "Note that this does not drain any prayer
  points").
- The port checks all of these in `deal_shared.rs2 [proc,deal_meets_requirements]`.

## 3. Stages (`%deal_quest`, a plain varp; Quest Helper `steps.put`)

The real game's varp values are Quest Helper's keys 0..18, and 19 is
complete. The port writes **its own values** (see the header of
`configs/rumdeal.constant`). The port merges keys that Quest Helper maps to
the same step object and keeps only the dbrow's endstate 19. So a test binds
the port's constants, not Quest Helper's numbers.

| Port value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 `deal_not_started` | 0, 1 talkToPete | Talk to Pirate Pete on the Ectofuntus dock, accept, refuse the money | -- |
| 0 -> 1 `deal_started` | 2 startOff | Knocked out and rowed to Braindeath Island; talk to Captain Braindeath | `deal_pete.rs2 [opnpc1,deal_pete]` |
| 1 -> 2 `deal_growing_blindweed` | 3, 4 growBlindweed | Braindeath gives the blindweed seed | `deal_braindeath.rs2 [opnpc1,deal_captian_braindeath]` |
| 2 (`%deal_farming` 3 raked, 4 planted, 5 grown) | 3, 4 growBlindweed | Rake the SE patch, plant the seed (Farming 40), wait about 5 minutes, pick | `deal_farming.rs2`: rake/plant `[oplocu,deal_blindweed_*]`, `settimer(deal_blindweed_grow, 500)`, `[timer,deal_blindweed_grow]` |
| 2 -> 3 `deal_deliver_blindweed` | 5 bringPlant | Pick the grown blindweed | `deal_farming.rs2 [oploc1,deal_blindweed_fullygrown]` |
| 3 -> 4 `deal_hopper_blindweed` | 6 addPlant | Braindeath says to put it in the hopper | `deal_braindeath.rs2` |
| 4 -> 5 `deal_told_get_water` | 7 talkAfterPlant | Use blindweed on the hopper (top floor, NW) | `deal_water_hopper.rs2 [oplocu,deal_hopper]` |
| 5 -> 6 `deal_get_water` | 8 getWater | Braindeath hands a bucket if you have none; gate by 50% Luke, stagnant water on the volcano | `deal_braindeath.rs2`; gate `[oploc1,deal_gate_closed]`, fill `[oplocu,deal_stagnant]` |
| 6 -> 7 `deal_told_get_sluglings` | 9 putWater | Pour the stagnant water into the hopper | `deal_water_hopper.rs2 [oplocu,deal_hopper]` |
| 7 -> 8 `deal_get_sluglings` | 10 startSlug | Braindeath gives the tangled fishbowl and net | `deal_braindeath.rs2` |
| 8 (`%deal_barrel` 0..5) | 11 getSlugsSteps | Fish 5 sluglings, put each in the pressure barrel | `deal_sluglings.rs2 [opnpc1,deal_squid]`, `[oplocu,deal_pressure]` |
| 8 -> 9 `deal_told_kill_spirit` | 12 startSpirit | Pull the lever at 5 (the lever loc flips at `%deal_barrel` 5) | `deal_sluglings.rs2 [oploc1,deal_multi_lever]` |
| 9 -> 10 `deal_kill_spirit` | 13 killSpiritSteps | Braindeath gives the wrench | `deal_braindeath.rs2` |
| 10 (`%deal_multi_hopper` 1) | 13 killSpiritSteps | Davey blesses the wrench (47 prayer points); use the holy wrench on the brewing control; the Evil spirit appears | `deal_combat.rs2 [opnpc1,deal_davey]`, `[oplocu,deal_multicontrol]` -> `[proc,deal_spawn_evilspirit]` |
| 10 -> 11 `deal_told_kill_spider` | 14 spiderStepsStart | Kill the Evil spirit | `deal_combat.rs2 [ai_queue3,deal_evil_spirit]` (also `%deal_multi_hopper` = 2) |
| 11 -> 12 `deal_kill_spider` | 15 spiderSteps | Braindeath asks for a fever spider carcass. Finish the dialogue first, or the carcass does not drop | `deal_braindeath.rs2` |
| 12 (carcass drop) | 15 spiderSteps | Kill a fever spider in the basement; it drops `deal_spider_body` | `deal_combat.rs2 [ai_queue3,deal_fever_spiders1]` (drops only at stage 12) |
| 12 -> 13 `deal_told_get_swill` | 16 makeBrewForDonnieStart | Use the carcass on the hopper | `deal_water_hopper.rs2 [oplocu,deal_hopper]` |
| 13 -> 14 `deal_get_swill` | 17 giveBrewToDonnie | Braindeath; fill a bucket at the output tap | `deal_braindeath.rs2`; tap `[oplocu,deal_brewvat_tap]` |
| 14 -> 15 `deal_return_to_finish` | 18 finishQuest | Give the unsanitary swill to Captain Donnie | `deal_donnie.rs2 [opnpc1,deal_captian_donnie]` |
| 15 -> 19 `deal_complete` | complete | Return to Braindeath | `deal_braindeath.rs2` -> `deal_shared.rs2 [proc,deal_quest_complete]` |

## 4. The boss fight: the Evil spirit

Wiki rules (Evil spirit, Walkthrough "Evil spirits"):

- It appears "as soon as you hit the controls" with the holy wrench, and it
  attacks you (aggressive).
- Level 150, 90 hitpoints, max hit 28. It uses crush melee only, attack
  speed 4. Examine: "The pun was intended."
- Protect from Melee negates its attacks. It can be safespotted with Ranged,
  Magic or a halberd, using crates, barrels or brewers. Changing floors
  drops its aggression.
- When you attack it, the player says "The power of Guthix compels you!"
  (Trivia).

Port: `[proc,deal_spawn_evilspirit]` spawns `deal_evil_spirit` at
`^deal_multicontrol_coord` (1_33_79_32_45) for 1000 ticks. It will not
spawn a second one if one is already within 5 tiles.
`[opnpc2,deal_evil_spirit]` ends in `@player_combat_start` (seam20, trap
31). `[ai_queue3,deal_evil_spirit]` writes stage 11 only when
`npc_findhero` finds the killer. It was DRIVEN for real in parity2c
(`build/parity_state/parity2c/rumdeal_scripts/parity_rumdeal.lua`, run
`parity_rumdeal4`). The rows: `spirit.attack` hp 30/30 hitsplat 0,
`spirit.dead` "slot 252 dead after 16 tick(s) ... ZERO BAR", then
`spirit.told_spider` `deal_quest = 11`. The 30/30 is the health bar's own
scale, not the npc's hitpoints.

Fever spider (a mob, not the boss): level 49, 40 hitpoints. Without slayer
gloves its hit does 12.5% of Hitpoints and causes disease, even through
Protect from Melee. It is safespottable north-west of the ladder (Quick
guide). The port has eleven `deal_fever_spiders1` spawns in `m33_79.spawn`.

## 5. Rewards (wiki Rewards; Quest Helper agrees)

- 2 quest points.
- 7,000 Fishing XP, 7,000 Prayer XP, 7,000 Farming XP. The port calls
  `stat_advance(..., 70000)`: internal units are XP x10.
- The holy wrench. While it is in the inventory, a prayer potion restores
  more prayer points.
- Access to Braindeath Island.
- The port's scroll lines come from `~quest_complete_rewards(quest_rumdeal,
  "7000 Fishing XP|7000 Prayer XP|7000 Farming XP|Holy wrench|Access to
  Braindeath Island", coins)`.

## 6. Leftovers (PARITY.tsv `legs_left`, 340c190aae)

Transcribed verbatim:

> fever spiders' disease (stat drain unless Slayer gloves are worn, per the
> wiki) is not modelled -- the basement spiders are plain melee mobs; the
> fever-spider kill itself is not driven (11 copies stacked in m33_79.spawn
> trip driver seam 15)

The seam 15 half is being fixed separately this pass
(`attack_press_and_watch_same_slot`).

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

These came from comparing the pinned Transcript and Walkthrough with the
port's scripts. They are recorded here only; no content was changed.

- **The basement cupboard is inert.** The Walkthrough and Quick guide say to
  take the rake and seed dibber from the cupboard (Transcript "Searching the
  basement cupboard for farming supplies"). Both `deal_broomcupboard` and
  `deal_broomcupboard_open` are declared in `all.loc.compack`, but no script
  handles them. A player who arrives without a rake or dibber has no in-game
  source for them on the island.
- **Zombie swab "Intimidate" is absent.** The Walkthrough says to right-click
  "Intimidate" so the swabs stop attacking; the Transcript has "Intimidating
  the Zombie swabs", swabs 1-5. No script in the tree names a swab or an
  intimidate op.
- **No Rabid Jack reveal.** In the Transcript, Donnie says "Rabid Jack would
  have my hide if I told ye it were him that sent me!", and Braindeath
  answers "Rabid Jack!". The port's Donnie and Braindeath finish lines are
  paraphrases with no Rabid Jack. The port's constant header says its
  dialogue is paraphrased throughout; the Transcript is now pinned for
  whoever ports it verbatim.
- The karamthulhu substitute for a slugling (a rare catch, and the Fishing
  bypass footnote) is deferred in `rumdeal.constant`: this tree has no
  rare-roll precedent.
