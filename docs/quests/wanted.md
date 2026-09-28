# Wanted! (wanted) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). This brief
records the port; it changes no content. PARITY.tsv: `partial` at
340c190aae (parity2c).

The wiki infobox dates Wanted! to **17 October 2005**. It is not in LostCity
or 2009scape. The port is built from the OSRS wiki and Quest Helper's
`wanted/Wanted.java`.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Wanted!](https://oldschool.runescape.wiki/w/Wanted!?oldid=15355767) | 15355767, 2026-09-25 | Requirements, Walkthrough (every hunt stop and Solus's reactions), Rewards |
| [Wanted!/Quick guide](https://oldschool.runescape.wiki/w/Wanted!/Quick_guide?oldid=14917147) | 14917147, 2025-06-08 | Checklist order |
| [Transcript:Wanted!](https://oldschool.runescape.wiki/w/Transcript:Wanted!?oldid=15330436) | 15330436, 2026-09-03 | Tiffy / Amik / Savant / Daquarius / Mage of Zamorak / Solus lines |
| [Solus Dellagar](https://oldschool.runescape.wiki/w/Solus_Dellagar?oldid=15204754) | 15204754, 2026-05-02 | The boss: no combat level, 40 hitpoints, max hit 4, melee, attack speed 3, aggressive |
| [Commorb](https://oldschool.runescape.wiki/w/Commorb?oldid=15324611) | 15324611, 2026-08-29 | Scan / Contact |
| Quest Helper `wanted/Wanted.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` 0..10 -> complete 11; `setupZones()` hunt zones |

Cache: `configs/all.dbrow [quest_wanted]` (id 92, endstate 11, 1 QP,
requirement_questpoints 32). The dbrow's `requirement_quests` decodes to the
wrong quests (see `wanted.constant`); the gate follows Quest Helper instead.

## 2. Requirements (wiki infobox)

- 32 quest points, needed to start.
- Quests completed: Recruitment Drive (which needs Black Knights' Fortress
  and Druidic Ritual), The Lost Tribe (Rune Mysteries, Goblin Diplomacy),
  Priest in Peril, and Enter the Abyss. The port's
  `[proc,wanted_meets_requirements]` checks all five and `%qp >= 32`.
- The ability to defeat a level 32 Black Knight (Wanted!).
- 10,000 coins, OR a law rune + an enchanted gem + molten glass (the
  Commorb).
- 20 un-noted rune or pure essence. The wiki says "you'll get them back":
  they return as the final clue, 20 noted essence.
- A light source for Dorgesh-Kaan and the Lumbridge Swamp Caves.

## 3. Stages (`%wanted_main`, on basevar `quest_wanted`; Quest Helper `steps.put`)

The port's values match Quest Helper's keys. Quest Helper maps
keys 1 and 2 to the same step as 0 (talkToSirTiffy1); the port never writes
1 or 2.

| Value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 -> 3 `wanted_amik_first` | 0-2 talkToSirTiffy1 | Sir Tiffy Cashien, Falador Park: the clerk's error, the loophole | `wanted_tiffy_amik.rs2 [proc,wanted_tiffy_talk]` |
| 3 -> 4 `wanted_tiffy_second` | 3 goDoAmikP1 | Sir Amik Varze (White Knights' Castle, 2nd floor): DECLINE the squire offer | `[proc,wanted_amik_talk]` (both answers advance; accepting also sets `%wanted_joke_option`) |
| 4 -> 5 `wanted_amik_second` | 4 talkToSirTiffy2 | Tiffy: a crisis has arisen | `[proc,wanted_tiffy_talk]` |
| 5 -> 6 `wanted_tiffy_third` | 5 goTalkToSirAmik2 | Amik: Solus Dellagar is back; accept the mission | `[proc,wanted_amik_talk]` |
| 6 -> 7 `wanted_get_commorb` | 6 talkToSirTiffy3 | Tiffy offers the Commorb | `[proc,wanted_tiffy_talk]` |
| 7 -> 8 `wanted_investigation` | 7 goGetCommorb | Buy it (10,000 coins) or have it made (law rune + enchanted gem + molten glass) | `[proc,wanted_tiffy_talk]` |
| 8 (`%wanted_commorb_intel` 1) | 8 investigation | Commorb Contact: Savant sends you to the Black Knights' Base and the Mage of Zamorak | `wanted_commorb.rs2 [opheld2,wanted_crystal_ball]` |
| 8 (`%wanted_daquarius_hint` 1) | 8 investigation | Lord Daquarius (Taverley Dungeon, SW room) tells you nothing | `wanted_daquarius.rs2 [opnpc1,lord_daquarius]` |
| 8 (`%wanted_daquarius_hint` 1 -> 2) | 8 investigation | Kill a level 32 Black Knight AFTER talking to Daquarius (the wiki: in his room, without leaving, conversation not interrupted) | `drop_tables/scripts/black_knight.rs2 [label,black_knight_drops]` (any `black_knight` / `aggressive_black_knight` kill anywhere counts; see section 7) |
| 8 (`%wanted_lord_d_exposition` 1) | 8 investigation | Daquarius gives in: Solus is somewhere with fur that is "not from a bear" | `[opnpc1,lord_daquarius]` |
| 8 (`%wanted_zammy_mage_hint` 1) -> 9 `wanted_hunt` | 8 -> 9 goHuntForSolus | Mage of Zamorak, Varrock Zamorakian chapel: 20 un-noted essence for the tip "east" -> Canifis | `wanted_mage.rs2 [proc,wanted_zammy_mage_talk]` (spliced into `quest_templeoftheeye/scripts/templeoftheeye.rs2`'s `[opnpc1]`) |
| 9 (`%wanted_mission1..19`) | 9 goHuntForSolus | Commorb Scan at 7 stops: Canifis, random, Champions' Guild, random, Dorgesh-Kaan mine, random, Rune essence mine | `wanted_hunt.rs2 [proc,wanted_scan_commorb]` via `[opheld1,wanted_crystal_ball]` |
| 9 -> 10 `wanted_final_battle` | 10 goTalkToSirAmikAfterFinalBattle | Kill Solus at the essence mine | `wanted_hunt.rs2 [ai_queue3,wanted_solus_attackable]` |
| 10 (hat) | 10 | The port gives Solus's hat through Commorb Contact | `wanted_commorb.rs2 [opheld2,wanted_crystal_ball]` |
| 10 -> 11 `wanted_complete` | complete | Hand the hat to Sir Amik | `[proc,wanted_amik_talk]` -> `wanted_shared.rs2 [proc,wanted_quest_complete]` |

## 4. The hunt and the boss fight

Hunt order, from the Walkthrough "Chasing Solus": seven stops. Canifis is
first. The Champions' Guild is always 3rd, the Dorgesh-Kaan mine 5th and
the Rune essence mine 7th. Stops 2, 4 and 6 are random, drawn from the item
table (Rellekka, Musa Point, Wizards' Tower, Ardougne Market, Castle Wars,
the Grand Tree, Slayer Tower, Brimhaven pub, Ali Morrisane's stall,
Lumbridge Swamp Caves, Goblin Village, Dragon Inn, McGrubor's Wood,
Draynor/Diango, the Shrine of Scorpius). At each stop Solus leaves an item
that names the next place.

- 3rd, Champions' Guild (blue cape): Solus casts Smoke Barrage. It
  disorients you; no damage, no poison.
- 5th, Dorgesh-Kaan mine (bone spear; light source; follow Kazgar): a
  "hostage" woman appears who is Solus. She sucker-punches you and leaves.
- 2nd stop: Solus teleports you to Camelot.
- 4th stop: a strong Flames of Zamorak, damage as a percentage of current
  Hitpoints, typeless. It never kills: Savant teleports you to the White
  Knights' Castle with all items.
- 6th stop: he summons a level 32 Black Knight (Wanted!). You can run
  instead of killing it.

The boss: **Solus Dellagar** at the Rune essence mine. Savant blocks the
teleports and summons 15 rangers; Solus kills them with Ice Barrage. Then
you fight him. Wiki stats: no combat level, 40 hitpoints, max hit 4,
"weak, but fairly rapid melee", attack speed 3, aggressive. You get his hat
after the fight (Savant replaces a lost one). You return to the teleport
spot.

Port: the scan at stop 7 `npc_add`s `wanted_solus_attackable` at
`^wanted_essence_mine_coord` (2909,4833, parity2c) for 100 ticks.
`[opnpc2,wanted_solus_attackable]` ends in `@player_combat_start`.
`[ai_queue3]` writes stage 10. Proof `parity2c_wanted4` 43/43: Solus's bar
went 30->0 in 12 ticks.

## 5. Rewards (wiki Rewards)

- 1 quest point.
- 5,000 Slayer XP. The port calls `stat_advance(slayer, 50000)`.
- Access to the White Knights' armoury (white equipment).

## 6. Leftovers (PARITY.tsv `legs_left`, 340c190aae)

Verbatim:

> the Champions' Guild stop narrates a Black Knight decoy where the wiki
> has Solus's Smoke Barrage, and the Dorgesh-Kaan stop narrates a magic bolt
> where the wiki has the 'hostage' woman who is Solus and sucker-punches the
> player -- both mesbox narration of the wrong event; every pool stop is a
> single scan-in-zone beat (Lumbridge Swamp Caves' rope/light/spiny-helmet
> descent absent); the pre-hunt Tiffy/Amik/Daquarius/Zamorak-mage chain not
> re-proved end to end

`wanted_hunt_stops` (`wanted_hunt.rs2` + configs) is being fixed at the
same time this pass. The hunt rows above describe that file as it was
before the fix.

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

- **Any Black Knight counts, anywhere.** `drop_tables/scripts/black_knight.rs2`
  writes `%wanted_daquarius_hint` = 2 on any `black_knight` or
  `aggressive_black_knight` kill once Daquarius has been talked to (hint 1).
  The Walkthrough: "The Black Knight kill won't count if you kill it before
  talking to Lord Daquarius, if you leave the room after the kill, or if you
  get interrupted during the ensuing conversation." The port enforces the
  first condition but not the room or the interruption.
- **The hunt gives no clue items.** The Walkthrough's item table says each
  stop leaves an item (blue cape, bone spear, 20 noted essence, and the
  random-stop items). At pinning time, no `inv_add` exists in
  `wanted_hunt.rs2`.
- **The essence is kept.** The Mage of Zamorak `inv_del`s the 20 essence.
  The wiki says you get them back as the final clue.
- **The hat comes from Contact, not the kill.** The Walkthrough says
  "kill him, and you'll get his hat". The port gives it only through
  Commorb Contact at stage 10. The wiki names that as the path for
  replacing a LOST hat.
- **Accepting the squire offer does not loop.** The Walkthrough says
  accepting Amik's squire offer loops forever on Asgarnian ale until Tiffy
  undoes it. The port advances either way.
