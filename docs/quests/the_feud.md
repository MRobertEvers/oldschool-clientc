# The Feud (thefeud) -- content brief (wiki-sourced)

Status: pinned 2026-09-27 (seam pass seam21, `pin_wiki_briefs`). It
describes the port as of OSRS-Content 340c190aae plus the working tree on
that date. PARITY.tsv: `partial` at 340c190aae (parity2c).

The Feud is not in LostCity; neither LostCity tree has it. The port is built
from the OSRS wiki and Quest Helper's `thefeud/TheFeud.java`. The wiki
infobox gives the release date as **4 April 2005**. The parity2c note's
"Dec 2005" is wrong; `quest_thefeud.constant`'s "Apr 2005" is right.

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [The Feud](https://oldschool.runescape.wiki/w/The_Feud?oldid=15315438) | 15315438, 2026-08-21 | Requirements, Walkthrough, Rewards |
| [The Feud/Quick guide](https://oldschool.runescape.wiki/w/The_Feud/Quick_guide?oldid=14646459) | 14646459, 2024-04-30 | Checklist order |
| [Transcript:The Feud](https://oldschool.runescape.wiki/w/Transcript:The_Feud?oldid=15325427) | 15325427, 2026-08-30 | NPC lines, branches |
| [Tough Guy](https://oldschool.runescape.wiki/w/Tough_Guy?oldid=15292989) | 15292989, 2026-08-11 | Menaphite fight: level 75, 75 hitpoints, max hit 7, crush, speed 4, aggressive; "Taking too long to kill the Tough Guy will result in him despawning" |
| [Bandit champion](https://oldschool.runescape.wiki/w/Bandit_champion?oldid=15292988) | 15292988, 2026-08-11 | Bandit fight: level 70, 50 hitpoints, max hit 10, slash, speed 4, aggressive; drops a willow blackjack, or an adamant scimitar if you already carry one |
| [Menaphite Thug](https://oldschool.runescape.wiki/w/Menaphite_Thug?oldid=15267417) | 15267417, 2026-07-19 | The gang npcs you question and pickpocket |
| Quest Helper `thefeud/TheFeud.java` | quest-helper `5ea99d5ea9` (2026-07-25) | `steps.put` ladder (0..27), sub-varbits (FEUD_VAR_TALK_GANGS, FEUD_VAR_MENABOSS, FEUD_VAR_BANDITBOSS, FEUD_TALK_VILLAGER), rewards |

Cache: `configs/all.dbrow [quest_feud]` (id 77, startnpc `feud_ali_m`,
endstate 28, 1 QP, Thieving 30 with no boostable column, 15,000 Thieving
XP).

## 2. Requirements (wiki infobox)

- Thieving 30, not boostable, needed to start. The port checks it with
  `stat_base`.
- You must be able to kill a level 75 Tough Guy and a level 70 bandit
  champion. Both can be safespotted.
- Items: 501+ coins; gloves that work at the cactus (the wiki says Barrows
  gloves, ice gloves, vambraces and slayer gloves do not); a Kharidian
  headpiece and a fake beard from Ali Morrisane; 3 beers; a bucket; the snake
  charm and snake basket (a coin in the snake charmer's money pot).

## 3. Stages (`%feud_var`, a varbit on `main_feud_var`; Quest Helper `steps.put`)

The cache's own multinpc tables show just one native breakpoint, value 28
(completion: every `_multi` wrapper swaps to its `_postquest` npc at slot
29). The values between 0 and 28 are the port's own, one per walkthrough
beat (the header of `quest_thefeud.constant`). Quest Helper's keys are the
real game's and do NOT line up with them. A test binds the port's constants.

| Port value (constant) | QH key -> step | Guide step | Written by |
| --- | --- | --- | --- |
| 0 -> 1 `feud_accepted` | 0 startQuest | Ali Morrisane in Al Kharid: agree to find his nephew | `feud_alimorrisane.rs2 [label,feud_alimorrisane_offer]` |
| 1 -> 2 `feud_drunken_ali_done` | 1 goToPollnivneach, 2 findBeef | Buy 3 beers from Ali the Barman; use each on Drunken Ali | `feud_recruitment.rs2 [opnpcu,feud_drunken_ali]` |
| 2 -> 3 `feud_gangs_questioned` (`%feud_var_talk_gangs` 1/2 -> 3) | 3 talkToCamelman | Question a Menaphite thug and a bandit, in either order | `feud_recruitment.rs2 [opnpc1,feud_egyptian_doorman_multi]` / `[opnpc1,feud_arabian_guard2_multi]` |
| 3 -> 4 `feud_camels_bought` | 4 returnCamels | Ali the Camel Man: two camels for 500 coins, two receipts | `feud_recruitment.rs2 [label,feud_camel_offer]` |
| 4 -> 5 `feud_receipts_given` | 5 talkToAliTheOperator | Give one receipt to each gang | `[label,feud_give_receipt_menaphite]` / `[label,feud_give_receipt_bandit]` |
| 5 -> 6 `feud_operator_joined` | 6 pickpocketVillager | Ali the Operator: ask to join; pickpocket 3 villagers | `feud_recruitment.rs2 [label,feud_operator_recruit]` |
| 6 -> 7 `feud_pickpocket1_done` | 7, 8 pickPocketVillagerWithUrchin | 1st villager (the wiki: you always fail until you have spoken to the Operator) | `feud_recruitment.rs2 [opnpc3,feud_villager_multi_*]` (also `[label,feud_operator_task2]`) |
| 7 -> 8 `feud_pickpocket2_done` | 7, 8 | 2nd villager: pay a street urchin 10 coins to distract, pickpocket from behind | `feud_recruitment.rs2 [opnpc1,feud_street_urchin]`, `[opnpc3,feud_villager_multi_*]`; also `[label,feud_operator_task3]`, which gives `blackjack_oak` |
| 8 -> 9 `feud_pickpocket3_done` | 9-11 blackjackVillagerStep | 3rd villager: equip the oak blackjack; lure, knock out, pickpocket | `feud_recruitment.rs2 [opnpc3,feud_villager_multi_*]` |
| 9 -> 10 `feud_heist_briefed` | 12 talkToAliToGetSecondJob | The Operator gives the villa keys (the port ALSO hands over a desert disguise; the wiki has you buy the parts from Ali Morrisane or the Market seller) | `[label,feud_operator_heist_briefing]` |
| 10 (cactus scout) | 13 hideBehindCactus | Wear the desert disguise and gloves; Hide behind the cactus | `feud_heist.rs2 [oploc1,feud_cactus_row]` (gear-gated; writes nothing) |
| 10 -> 11 `feud_house_entered` | 13 -> 14 heist | Use the key on the villa door | `feud_heist.rs2 [oploc1,feud_closed_door_right]` |
| 11 -> 12 `feud_note_numbers` | 14 heist | Search the desk | `feud_heist.rs2 [oploc1,feud_mayors_desk]` |
| 11/12 -> 13 `feud_note_fib` | 14 heist | Search the bed for the Fibonacci note | `feud_heist.rs2 [oploc1,feud_mayors_bed]` |
| 13 -> 14 `feud_safe_opened` | 14 heist | Search the landscape picture; dial **1, 1, 2, 3, 5, 8** (numbers 1-9 clockwise); take the jewels (needs a free slot) | `feud_heist.rs2 [oploc1,feud_mayors_picture]` |
| 14 -> 16 `feud_traitor_briefed` | 15 returnTheJewels, 16 findTraitor | Give the jewels to the Operator (`%feud_given_jewels` = 1); he asks you to root out a traitor. `^feud_jewels_delivered` (15) is declared but never written | `[label,feud_operator_traitor_briefing]` |
| 16 -> 17 `feud_thug_questioned` | 16 findTraitor | A Menaphite thug names Traitorous Ali | `feud_recruitment.rs2 [label,feud_thug_questioning]` |
| 17 -> 18 `feud_barman_done` | 17 getSnake | Ali the Barman: the beer on the table is the traitor's | `feud_traitor.rs2 [opnpc1,feud_ali_the_barman]` |
| 18 -> 19 `feud_sauce_bought` | 18 camelDung | Ali the Kebab seller's red hot sauce | `feud_traitor.rs2 [opnpc1,feud_kebabman]` |
| 19 -> 20 `feud_dung_bucketed` | 18 camelDung | Sauce on the camel food trough; bucket the brown dung | `feud_traitor.rs2 [oplocu,feud_foodtrough2]` |
| 20 -> 21 `feud_snake_done` | 17 getSnake | Coins in the money pot (snake charm + basket); use the charm on a desert snake | `quest_ratcatchers/scripts/ratcatchers.rs2 [oplocu,feud_money_bowl]` (feud branch); `feud_traitor.rs2 [opnpcu,feud_desert_snake_outside]` |
| 21 -> 22 `feud_poison_made` | 22 poisonTheDrink | Ali the Hag takes the snake and the dung and makes the poison | `feud_traitor.rs2 [opnpc1,feud_hag]` |
| 22 -> 23 `feud_beer_poisoned` | 23 tellAliOperatorPoisoned | Use the poison on the traitor's beer table in the pub | `feud_traitor.rs2 [oplocu,feud_poison_beer_table]` |
| 23 -> 24 `feud_ready_confront` | 24 killThug | The Operator sends you to the Menaphite Leader | `[label,feud_operator_final_orders]` |
| 24 -> 25 `feud_menaphite_beaten` | 24 killThug -> 25 killChampion | Decline the Menaphite Leader; kill the Tough Guy | `feud_confrontation.rs2 [ai_queue3,feud_menap_toughguy]` |
| 25 -> 26 `feud_bandit_beaten` | 25 killChampion | The Bandit Leader summons the bandit champion; kill him | `feud_confrontation.rs2 [label,feud_bandit_leader_confront]` spawns it; `[ai_queue3,feud_bandit_toughguy]` |
| 26 -> 27 `feud_mayor_talked` | 26 spawnMayor | Ali the Mayor by the well tells you he smuggled the nephew out | `feud_confrontation.rs2 [label,feud_mayor_reveal]` |
| 27 -> 28 `feud_complete` | 27 finishQuest | Return to Ali Morrisane | `feud_alimorrisane.rs2 [label,feud_alimorrisane_finish]` |

Quest Helper puts getSnake (17) before camelDung (18). The port's dung
values (19, 20) come before its snake value (21). The port's order is the
Walkthrough's: the Hag asks for the snake first, and the dung second.

## 4. The fights

Wiki (Walkthrough "Defeating the gang leaders", Tough Guy, Bandit champion):

- **Tough Guy** (level 75, 75 hitpoints, max hit 7, crush, speed 4,
  aggressive). The Menaphite Leader summons him after you decline to join.
  He can be safespotted behind the table in the Menaphite tent. He despawns
  if the kill takes too long. He drops bones.
- **Bandit champion** (level 70, 50 hitpoints, max hit 10, slash, speed 4,
  aggressive). The Bandit Leader, north-west by the Rug Merchant, summons
  him. He can be safespotted behind the crate near the Rug Merchant. He
  drops a willow blackjack, or an adamant scimitar if a willow blackjack is
  already in the inventory.
- After EACH fight, the Walkthrough has you talk to a villager. After the
  Tough Guy, they send you to the Bandit Leader. After the champion, they
  send you to the Mayor. These are Quest Helper's `FEUD_TALK_VILLAGER` 1 and
  2 (`talkToAVillager`, `talkToAVillagerToSpawnMayor`).

Port: both `[opnpc2]` bindings end in `@player_combat_start` (trap 31). The
Menaphite Leader (`[label,feud_menap_leader_confront]`) and the Bandit
Leader (`[label,feud_bandit_leader_confront]`) each `npc_add` their fighter
for 50 ticks, and only when none is within 15 tiles. That 50-tick lifetime
is the port's form of the wiki's "despawns if it takes too long". `%feud_var_menaboss` / `%feud_var_banditboss` make
each kill count once. PARITY.tsv notes that neither fight was re-driven in
parity2c.

## 5. Rewards (wiki Rewards; Quest Helper `getItemRewards`)

- 1 quest point; 15,000 Thieving XP (the port: `stat_advance(thieving,
  150000)`); 500 coins.
- An oak blackjack. The port gives it during the quest, at the Operator's
  third task.
- A desert disguise, made during the quest. The port adds one at completion
  if you do not have one.
- A willow blackjack, which the wiki says drops from the Tough Guy. The
  Bandit champion page says the champion drops it.
- An adamant scimitar, dropped by the bandit champion.
- Access to the Rogue Trader miniquest. You can pickpocket villagers,
  bandits and Menaphite thugs in Pollnivneach.
- The port hands the willow blackjack and the adamant scimitar to you at
  Ali Morrisane (`feud_alimorrisane_finish`), following Quest Helper's
  reward list, not as drops.

## 6. Leftovers (PARITY.tsv `legs_left`, 340c190aae)

Verbatim:

> the safe's combination lock (interface 330 / clientscript 261) is
> narrated, not button-driven (a puzzle collapsed); hide-behind-cactus is
> real but not a persisted door gate (no varbit allocation path); graceful
> gloves not accepted at the cactus; the cowardly bandit and feud_npc_multi
> lookalike villagers absent; the Menaphite/bandit tough-guy fights were not
> re-driven this pass

The safe lock is being fixed at the same time this pass
(`feud_safe_combination_lock`, `feud_heist.rs2` + configs). The rows for
stages 11-14 above describe the file as it was before that change.

## 7. Found while pinning (NOT in PARITY.tsv; for the next parity pass)

- **The two villager talks are missing.** The Walkthrough has you talk to a
  villager after the Tough Guy (who sends you to the Bandit Leader) and
  after the champion (who sends you to the Mayor). Quest Helper tracks both
  with `FEUD_TALK_VILLAGER`. In the port, the Tough Guy's kill unlocks the
  Bandit Leader directly, and the champion's kill unlocks the Mayor
  directly. No script reads `feud_talk_villager`.
- **The drops became completion gifts.** The wiki has the willow blackjack
  and the adamant scimitar as fight drops. The port adds both at completion,
  and `[ai_queue3]` drops nothing quest-specific.
- **The disguise is handed over.** The Walkthrough has the player buy the
  Kharidian headpiece and fake beard (from Ali Morrisane or the Market
  seller) and combine them. The port's `[label,feud_operator_heist_briefing]`
  gives a finished `feud_desert_disguise` with the keys.
