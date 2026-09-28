# Zogre Flesh Eaters (zogreflesheaters) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). This brief
documents the port; it changes no content. PARITY.tsv: `partial` at
340c190aae (parity2c).

The wiki infobox dates the quest to **17 May 2005**. Neither LostCity tree
has it. The port's first slice took its dialogue policy from 2009scape
(`zogreflesheaters.rs2` header: "Policy: 2009scape GrishDialogue /
OgreGuardDialogue / ZogreFleshEatersListeners"; several `.varp` carriers are
2009scape attributes). parity2c then checked every leg against the OSRS wiki
and Quest Helper's `zogreflesheaters/ZogreFleshEaters.java`. Those two are
the authority here.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters?oldid=15328252) | 15328252, 2026-09-02 | Requirements, Walkthrough, Rewards |
| [Zogre Flesh Eaters/Quick guide](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters/Quick_guide?oldid=15297480) | 15297480, 2026-08-13 | Checklist order |
| [Transcript:Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Transcript:Zogre_Flesh_Eaters?oldid=15301503) | 15301503, 2026-08-14 | Grish / guard / Zavistic / Sithik / bartender lines |
| [Slash Bash](https://oldschool.runescape.wiki/w/Slash_Bash?oldid=15355748) | 15355748, 2026-09-25 | The boss: level 111, 100 hitpoints, max hit 13, ranged + crush, speed 6, undead, aggressive; damage rules; 5-minute despawn |
| [Relicym's balm](https://oldschool.runescape.wiki/w/Relicym%27s_balm?oldid=15223391) | 15223391, 2026-06-01 | The disease cure the quest unlocks |
| Quest Helper `zogreflesheaters/ZogreFleshEaters.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` ladder (0, 2, 3, 4, 5, 6, 8, 10, 12; endstate 14) |

Cache: `configs/all.dbrow [quest_zogreflesheaters]` (endstate 14).

## 2. Requirements (wiki infobox)

- Quests: Big Chompy Bird Hunting; Jungle Potion (which needs Druidic
  Ritual).
- Smithing 4 and Herblore 8 are boostable. Ranged 30 is not.
- You must be able to defeat a level 111 enemy.
- 5 free inventory slots at the start: three cooked chompy and two super
  restore potions from Grish.
- The port's `[proc,zfe_has_requirements]` checks `%junglepotion`,
  `%chompybird >= 65`, Ranged 30, Smithing 4 and Herblore 8.

## 3. Stages (`%zogre`, on the `zombie_ogre` carrier; Quest Helper `steps.put`)

The port's values are its own. Only 0 and 14 match the real game. Quest
Helper's keys are shown next to them for cross-reference; a test binds the
port's constants (`zogreflesheaters.constant`).

| Port value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 -> 1 `zfe_investigate` | 0 talkToGrish | Grish at Jiggig: ask about the zogres, offer help; he gives 3 cooked chompy + 2 super restores | `zogreflesheaters.rs2 [opnpc1,zogre_ogre_shaman]` |
| 1 -> 2 `zfe_crypt` | 2 talkToGuard | The ogre guard crushes the barricade (`%thzfe_blocking_barricade` = 1) | `zogreflesheaters.rs2 [opnpc1,zogre_ogre_guard]` |
| 2 (item-tracked) | 3 explore | Jiggig dungeon: lectern -> torn page; skeleton -> zombie -> ruined backpack (tankard, rotten food, knife); coffin: search, knife, open, search -> black prism | `[oploc1,zogre_lecturn]`, `[oploc1,zogre_brentle_skeleton]` / `[ai_queue3,zogre_human_brentle_vahn]`, `[opheld1,zogre_brentle_vahn_backpack]`, `[oploc1/oplocu,zogre_coffin_special]` (`zogreflesheaters.rs2`) |
| 2 -> 3 `zfe_zavistic` | 3 explore | Tankard on the Dragon Inn bartender; show the prism + page to Zavistic Rarve (or ring the bell below Magic 66) | `zogre_finish.rs2 [proc,zfe_zavistic_talk]` via `[opnpc1,zogre_human_zavistic_rarve]` / `[oploc1,zogre_outdoor_bell]`; bartender `[opnpcu,dragon_bartender]` |
| 3 -> 4 `zfe_sithik` | 3 explore | Sithik in the guest house (upstairs, west room): ask to look around | `zogre_finish.rs2 [proc,zfe_sithik_man_talk]` (`[oploc1,ogre_bedman_loc]`) |
| 4 (item-tracked) | 3 explore | Drawers / cupboard / wardrobe -> portraiture book, necromancy book, papyrus + charcoal, H.A.M. book; papyrus on Sithik -> portrait ("honesty" = good); good portrait signed by the bartender | `[oploc1,sithiks_drawers/cupboard/wardrobe]`, `[oplocu,ogre_bedman_loc]`, `[opnpcu,dragon_bartender]` |
| 4 -> 5 `zfe_potion` | 4 poisonSith | Show all evidence to Zavistic; he takes it and gives the strange potion | `zogre_finish.rs2 [proc,zfe_zavistic_talk]` |
| 5 -> 6 `zfe_potion_tea` | 5 poisonSith | Potion on the cup of tea on Sithik's dresser | `zogre_finish.rs2 [opobju,zogre_cup_of_tea_sithix]` |
| 6 -> 7 `zfe_sithik_ogre` | 6 askSithQuestions | Go down the ladder and back up; Sithik is an ogre (`%thzfe_sithik_transformed` = 1) | `zogre_finish.rs2 [oploc1,ladder]` (guarded on `loc_coord = ^zfe_sithik_ladder`; every other ladder falls through to `~climb_ladder(1)`, seam25) |
| 7 (answers) | 8 askAboutDiseaseAndOgres | Ask all three questions: undead ogres -> `%thzfe_makebrutalarrow`, disease -> `%thzfe_makecuredisease` | `zogre_finish.rs2 [proc,zfe_sithik_ogre_talk]` |
| 7 -> 8 `zfe_grish_key` | 8 -> 10 | Tell Grish; he gives the ogre gate key (`zogre_tomb_artefact_key`); "There must be an easier way to kill these zogres!" -> `%thzfe_makecompozogrebow` | `zogreflesheaters.rs2 [opnpc1,zogre_ogre_shaman]` |
| 8 (fight) | 10 goKillBash | Two locked doors, stairs down, search the stand -> Slash Bash | `zogre_finish.rs2 [oploc1,ogre_cavedoorr/l]` -> `[proc,zfe_tomb_door]`; `[oploc1,zogre_stand]` |
| 8 -> 9 `zfe_slash_bash` | 12 returnRelic | Kill Slash Bash; take the ogre artefact | `zogre_finish.rs2 [ai_queue3,zogre_slash_bash]` |
| 9 -> 14 `zfe_complete` | complete | Give the artefact to Grish | `zogreflesheaters.rs2 [opnpc1,zogre_ogre_shaman]` -> `[queue,zfe_quest_complete]` |

## 4. The boss fight: Slash Bash

Wiki (Slash Bash; Walkthrough "Preparing for Slash Bash" and "Relocating the
ceremonial grounds"):

- Level 111, 100 hitpoints, max hit 13. He attacks with ranged and crush,
  attack speed 6. Undead, aggressive.
- Searching the stand spawns him. You can safespot him by standing east of
  the stand BEFORE you search it; there he uses only ranged, and Protect from
  Missiles negates all his damage.
- Incoming damage: brutal arrows from the comp ogre bow do full damage.
  Crumble Undead does half damage (maximum hit 7). Everything else does 25%.
- His attacks inflict disease (a random stat, not Hitpoints or Prayer,
  drained by up to 15), even through protection prayers. It takes 5 doses of
  Relicym's balm or 2 of Sanfew serum to cure. His attacks can also drain
  Prayer when you pray against their style.
- If you leave or take longer than five minutes, he disappears ("The huge
  Slash Bash grows weary of your feeble attempts at combat and awaits a true
  challenge"). Search the stand again to respawn him.
- He drops the ogre artefact and 3 ourg bones. If you die, get another gate
  key from Grish. Grish takes the key when you hand the artefact in.

Port: `[oploc1,zogre_stand]` at stage 8 does `npc_add(^zfe_slash_bash_spawn,
zogre_slash_bash, 500)`. 500 ticks = 5 minutes, then `npc_setmode(opplayer2)`.
Damage goes through `skill_combat/scripts/player/player_hit_npc_prepare.rs2`
-> `[proc,zfe_slash_bash_prepare_hit]`: comp bow (`zogre_bow`) + brutal
arrows do full damage, Crumble Undead (`%zfe_crumble_undead_cast`) does half
capped at 7, and everything else is divided by 4. Proof `parity_zogreflesheaters5`
21/21: melee 30->27 hitsplat 1, comp bow + brutal 30->21 hitsplat 3.

## 5. Rewards (wiki Rewards)

- 1 quest point.
- 2,000 Fletching, 2,000 Ranged and 2,000 Herblore XP (the port:
  `stat_advance(..., 20000)`).
- 3 ourg bones and 2 zogre bones.
- You can make Relicym's balm, fletch comp ogre bows and brutal arrows, and
  wear inoculation bracelets.
- Giving Uglug Nar a 2-, 3- or 4-dose Relicym's balm opens
  `~ Uglug's stuffsies ~`.
- Bonus: sell the black prism to Zavistic (2,000 coins) or Yanni Salika
  (5,000 coins).
- The port's scroll: `"2000 Fletching XP|2000 Ranged XP|2000 Herblore XP|3
  ourg bones|2 zogre bones|Make Relicym's balm|Fletch comp ogre bows"`.

## 6. Leftovers (PARITY.tsv `legs_left`, 340c190aae)

Verbatim:

> Crumble Undead's half-damage cap-7 branch on Slash Bash is not driven live
> (cast never landed, driver seam 15; STALE since seam25: s25sithik2/4 landed
> 67-68 re-casts, 30/30 -> 3/30, but not a kill inside his 500-tick stay); Slash Bash's disease attack unwired;
> barricade smash animation; Sithik bad-portrait branch; transformed Sithik
> keeps the human chathead; Relicym's balm has no quest gate

`relicym_balm_gate` (skill_herblore + this quest's scripts) and seam 15
(`attack_press_and_watch_same_slot`) are both being fixed at the same time
this pass.

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

- **FIXED in seam25** (`[ai_queue3,zogre_slash_bash]` now `obj_add`s it at `npc_coord`; the test picks it up). Was: **The artefact goes straight to the backpack.** The wiki has Slash Bash
  DROP the ogre artefact, and Quest Helper has a `pickUpOgreArtefact` step
  gated on `ogreRelicNearby`. The port's `[ai_queue3,zogre_slash_bash]`
  `inv_add`s it and prints "You take an ogre artefact.". The 3 ourg bones
  that the wiki says he drops are added at completion instead.
- **Uglug Nar's balm hand-in was not checked.** `shop/jiggig` has the store
  (`uglugs_stuffsies`); this pass did not verify whether the one-time balm
  hand-in (1,000 / 650 / 300 / 100 coins by dose) gates it.
- **Grish does not take the key.** The wiki warns: "After talking to Grish,
  he will take your key and you will be unable to return to the boss room".
  Nothing in the port `inv_del`s `zogre_tomb_artefact_key`.
- **No inoculation bracelet gate.** No script in the tree references
  `inoculation` except Sailing's station.
