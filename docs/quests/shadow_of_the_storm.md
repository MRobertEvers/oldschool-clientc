# Shadow of the Storm (shadowstorm) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). This brief
documents the port only; no content was edited. PARITY.tsv: `partial` at
340c190aae (parity2c).

The wiki infobox dates Shadow of the Storm to **14 November 2005**. LostCity
does not have it (`shadowstorm_ritual.rs2`: "LostCity: none"). The port is
built from the OSRS wiki and Quest Helper's
`shadowofthestorm/ShadowOfTheStorm.java` + `IncantationStep.java`.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Shadow of the Storm](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm?oldid=15354765) | 15354765, 2026-09-23 | Requirements, Walkthrough (Evil Dave's black-clothing rule, the kilns, both rituals, the fight), Rewards |
| [Shadow of the Storm/Quick guide](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm/Quick_guide?oldid=15212986) | 15212986, 2026-05-19 | Checklist order |
| [Transcript:Shadow of the Storm](https://oldschool.runescape.wiki/w/Transcript:Shadow_of_the_Storm?oldid=15263330) | 15263330, 2026-07-14 | Reen / Badden / Evil Dave / Denath / Matthew / golem lines; "You fool! Do it again, and get it right!" |
| [Agrith Naar](https://oldschool.runescape.wiki/w/Agrith_Naar?oldid=15350581) | 15350581, 2026-09-18 | The boss: level 100, 95 hitpoints, max hit 10 crush / 10 magic, speed 4, demon, aggressive |
| Quest Helper `shadowofthestorm/ShadowOfTheStorm.java`, `IncantationStep.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` 0..124 -> complete 125 |

Cache: `configs/all.dbrow [quest_shadowofthestorm]`. Progress is
`%agrith_quest`, carried by `agrith_quest_varp` (`shadowstorm.varp`).

## 2. Requirements (wiki infobox)

- Crafting 30 (boostable).
- The Golem and Demon Slayer completed.
- The ability to defeat a level 100 demon.
- Items: Silverlight (Father Reen gives one if you have none); the strange
  implement (found during the quest); three pieces of black clothing (the
  wiki lists what Evil Dave does and does not accept; the dyed Silverlight
  does NOT count); black dye (black mushroom + vial + pestle and mortar); a
  silver bar; a way into the desert.

## 3. Stages (`%agrith_quest`; Quest Helper `steps.put`)

The port's constants equal Quest Helper's keys. The port never writes 80,
100 or 110 (see the rows).

| Value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 -> 10 `sots_see_badden` | 0 talkToReen | Father Reen, south of the Al Kharid bank (gives an undyed Silverlight if you have none) | `shadowstorm.rs2 [label,sots_reen_talk]` |
| 10 -> 20 `sots_infiltrate` | 10 talkToBadden | Father Badden at Uzer: ask every option | `shadowstorm.rs2 [label,sots_badden_talk]` |
| 20 (dye) | 20 infiltrateCult | Black mushroom -> black dye; dye Silverlight; wear 3 black items | `shadowstorm_dye.rs2` |
| 20 -> 30 `sots_denath` | 20 -> 30 goTalkToDenath | Evil Dave lets you through the portal; Denath tells you the incantation | `shadowstorm_ritual.rs2 [opnpc1,agrith_dave_at_portal]` |
| 30 -> 40 `sots_sigil_tasks` | 40 completeSubTasks | Jennifer gives the demonic sigil mould | `shadowstorm_dye.rs2 [opnpc1,agrith_jennifer_sigil]` |
| 40 -> 50 `sots_matthew` | 50 completeSubTasks | Matthew: find Josef's book | `shadowstorm_ritual.rs2 [label,sots_matthew_talk]` |
| 50 -> 60 `sots_golem_ask` | 60 completeSubTasks | The clay golem: Denath killed Josef, the book is in a kiln (rolls `%agrith_kiln` 0..3) | `quest_golem/scripts/golem.rs2 [label,golem_talk]` |
| 60 -> 70 `sots_ritual` | 70 startRitual | Search the 4 kilns around Uzer; the rolled one holds the demonic tome | `shadowstorm_ritual.rs2 [oploc1,agrith_kiln_1..4]` -> `[label,sots_kiln_search]` |
| 70 (sigil) | 70 startRitual | Silver bar at a furnace with the mould -> demonic sigil (allowed from 40) | `skill_crafting/scripts/jewellery/jewellery_if.rs2` |
| 70 -> 90 `sots_ritual_done` | 80 performRitual -> 90 | Hand Matthew the book; the FIRST ritual (you chant Denath's order; Denath walks into the portal); pick up his sigil | `shadowstorm_ritual.rs2 [label,sots_matthew_talk]` (narrated, section 7) |
| 90 (recruit) | 90, 100 prepareForSecondRitual | Chase Evil Dave / Eric / Tanya (Tanya's sigil); Dave returns and gives Eric's sigil; Badden and Reen each take a sigil; strange implement on the golem, then talk | `%agrith_convinced_dave`, `%agrith_badden_uzer`, `%agrith_reen_uzer`, `%agrith_convinced_golem` (`shadowstorm.rs2`, `quest_golem/scripts/golem_portal.rs2`) |
| 90 -> 120 `sots_fight` | 110 summonAgrith -> 120 defeatAgrith | Matthew gathers everyone; chant the BOOK's order; Agrith-Naar appears | `shadowstorm_ritual.rs2 [label,sots_matthew_talk]` (5-way choice, answer 2) |
| 120 -> 124 `sots_unequip` | 124 unequipDarklight | Final blow with Silverlight worn; it becomes Darklight | `shadowstorm_ritual.rs2 [ai_queue3,agrith_naar]` |
| 124 -> 125 `sots_complete` | complete | Automatic on the kill; the fallback is unequipping Darklight | `[proc,sots_try_complete]` (from `[ai_queue3,agrith_naar]`; `player/scripts/equip.rs2 [proc,unequip]`) |

## 4. The boss fight: Agrith-Naar

Wiki (Agrith Naar; Walkthrough "The Final Fight"):

- Level 100, 95 hitpoints. Max hit 10 crush and 10 magic. Speed 4. Demon,
  aggressive. He stays in one place for the whole fight.
- He uses melee by default. He switches to Fire Blast if you turn on Protect
  from Melee or walk out of range. If you are out of melee range AND using
  Protect from Magic, he casts Telekinetic Grab and pulls you next to him.
- Any weapon damages him. **Silverlight must be equipped for the final blow,
  or his health is restored to 12.** For a pure, any manually cast combat
  spell works as long as Silverlight is worn.
- You can flinch him with melee from behind the north-west torch. A
  Dwarf multicannon does not work ("Some kind of demonic magic prevents the
  cannon from functioning.").
- After the kill, Silverlight becomes Darklight. If the final chat is
  interrupted, unequipping Darklight triggers it.

Port: `[opnpc2,agrith_naar]` is `@player_combat_start`. The throne spawn
lasts 500 ticks. In `[ai_opplayer2,agrith_naar]`, 1 in 3 is a Telekinetic
Grab (`[queue,sots_agrith_tk_pull]` teleports you onto his tile). Otherwise
he uses Fire Blast if Protect from Melee is on or 1 in 4 comes up (max
`^sots_agrith_fireblast_max` = 12), and melee the rest of the time. The kill
gate checks for `agrith_silverlight_dyed` or `darklight` worn; if neither
is worn it calls `npc_statheal(hitpoints, 12)`.

DRIVEN in parity2c: `parity_shadowstorm_kill` 13/13 took the bar 30->0 in
14 ticks with the dyed Silverlight, reaching stage 125, scroll + 1 QP;
`parity_shadowstorm_sigil` 21/21.

## 5. Rewards (wiki Rewards)

- 1 quest point.
- **10,000 XP** in any combat skill other than Prayer (a lamp choice).
- Silverlight becomes Darklight. It has a special attack and is stronger
  against demons.
- Six cut gems (2 sapphires, 2 rubies, 2 emeralds), but only "if you use a
  hammer and chisel on the demon's throne and had not yet removed them
  during The Golem".
- Agrith-Naar becomes available in the Nightmare Zone.

## 6. Leftovers (PARITY.tsv `legs_left`, 340c190aae)

Verbatim:

> the incantation's word order is fixed, not per-account random; the
> circle-tile standing requirement is not enforced; the reward lamp is a
> fixed Hitpoints advance, not the wiki's chosen combat skill (tree-wide lamp
> gap)

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

- **The lamp pays one tenth.** FIXED seam22: `^sots_agrith_lamp_xp = 100000`. `stat_advance` takes XP x10: Rum Deal's
  70000 is 7,000 XP and The Feud's 150000 is 15,000 XP. So
  `stat_advance(hitpoints, ^sots_agrith_lamp_xp)` with
  `^sots_agrith_lamp_xp = 10000` gives **1,000** Hitpoints XP. The wiki's
  reward is 10,000.
- **The first ritual is narrated.** FIXED seam22: 70 -> 80 in
  `[label,sots_matthew_talk]` (book read and handed back, dyed Silverlight
  shown) -> 90 on the sigil's Chant on 2718,4902,2 in Denath's order; Tanya's
  sigil drop is still missing. Before the fix: at 70 -> 90, `sots_matthew_talk` plays
  four `~mesbox` lines ("Denath leads the chant ... bolts for the portal")
  and drops one sigil at the throne. In the Walkthrough, the player chants
  Denath's order with their own sigil. Tanya's sigil and Eric's sigil (from
  Evil Dave) come from the chase through the portal. By the kit's rules, a
  stage that a narrating `mes()` advances is a gap.
- **The fight AI does not follow the wiki's switch.** The port rolls
  Telekinetic Grab 1 in 3 whatever your range or prayer. The wiki uses it
  only against a player who is out of melee range with Protect from Magic
  on. The port's Fire Blast also fires 1 in 4 at melee range with no melee
  prayer. Its Fire Blast max is 12; the wiki's magic max hit is 10.
- **The throne gems are handed over.** The port gives the six gems at
  completion if `%golem_throne_gems` is 0. The wiki gives them only for a
  hammer and chisel used on the throne. That action already exists at
  `quest_golem/scripts/golem_portal.rs2 [oplocu,golem_throne_withgems]`.
