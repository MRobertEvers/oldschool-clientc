# The Fremennik Isles -- wiki-pinned brief

Source: https://oldschool.runescape.wiki/w/The_Fremennik_Isles (walkthrough, rewards;
oldid 15314025) and https://oldschool.runescape.wiki/w/Transcript:The_Fremennik_Isles
(every dialogue line; oldid 15314068), read 2026-10-02. Guide ladder: quest-helper
`helpers/quests/thefremennikisles/TheFremennikIsles.java` + `KillTrolls.java`.
The quest (6 Feb 2007) post-dates LostCity: no LostCity script exists, so the port is
built from this brief and the guide. Cutscenes of the original (ferry rides, the cave
entrance, the Troll King's approach, the fade around the performance) are specced
separately: `docs/quests/cutscenes/TRIAGE.tsv` rows 1-3.

## Requirements and rewards

- The Fremennik Trials complete (the only start requirement). Construction 20
  (boostable) is needed only to repair the bridges; Agility 40 is only for the optional
  Central Fremennik Isles mine. The cache dbrow lists both as stats, the wiki does not
  gate the start on them: the port gates the start on the Trials alone and the repair on
  Construction 20, the wiki's form.
- Kills: 10 frenzied ice trolls (level 74-82, any kind) in a multicombat cave and the
  Ice Troll King (level 122, melee, Protect from Magic).
- Rewards: 1 quest point; 5,000 Construction, 5,000 Crafting, 10,000 Woodcutting xp;
  two choices of 10,000 xp in Attack/Strength/Defence/Hitpoints (the same skill may be
  chosen twice); Helm of Neitiznot (Defence 55; 50,000 coins to buy another from
  Mawnis); access to arctic pines, the north-east runite island (Thakkrad repairs the
  third bridge) and the Jatizso mine; jester outfit retrievable from the chest; the
  Contraband Yak Produce shop if the 5,000 window tax is refunded to Vanligga; about
  20,000 coins during the quest.

## Stage ladder (varbit `fris_quest`; the guide's steps.put keys)

| value | state | step |
| --- | --- | --- |
| 0 | not started | talkToMord |
| 5 | accepted from Mord | talkToGjuki (cat intervenes) |
| 10 | the king asks for raw tuna | talkToGjuki with tuna |
| 20 | Hrafn fed | continueTalkingToGjuki |
| 30 | ore needed (6 mithril / 7 coal / 8 tin by Mining level 55+/2-54/1; noted ok) | bringOreToGjuki |
| 40 | ore handed in (10,000 coins) | talkToGjukiAfterOre |
| 50 | spy mission: outfit from the chest, Slug's password "Free stuff please." | getJesterOutfit / talkToSlug |
| 55 | Slug briefed (outfit worn, nothing in hands) | goSpyOnMawnis / performForMawnis |
| 60 | performed: report to Slug (3 questions, 2,500 coins) | tellSlugReport1 |
| 90 | gain Mawnis's trust (outfit off) | talkToMawnis |
| 100 | eight ropes (shown and kept; 1,000 coins) | talkToMawnisWithLogs |
| 110 | eight split logs (shown and kept; 1,500 coins) | talkToMawnisWithLogs |
| 130 | logs delivered | talkToMawnisAfterItems |
| 140 | repair the two bridges (4 rope + 4 split logs + knife each, Construction 20) | repairBridge1/2 |
| 150 | both repaired: report to Mawnis (1,500 coins, the raid plan) | talkToMawnisAfterRepair |
| 160 | go to Jatizso; Gjuki makes the player tax collector (tax bag) | talkToGjukiToReport |
| 200 | window tax 1000/window: Hring or Raum 8000, Skuli 6000, Flosi or Keepa 5000, Vanligga 5000 | collectFrom* |
| 210 | beard tax 1000 each: Raum, Hring, Skuli, Flosi, Keepa (bag reads 29,000) | collectFrom*Again |
| 230 | spy again | talkToSlugToSpyAgain |
| 235 | briefed again | goSpyOnMawnisAgain |
| 240 | performed again: report (Etceteria / potions / "I have been helping") | reportBackToSlugAgain |
| 260 | Gjuki rants, hands over the royal decree | talkToGjukiAfterSpy2 |
| 270 | deliver the decree (outfit off) | talkToMawnisWithDecree |
| 275 | decree read | talkToMawnisAfterDecree |
| 280 | yak-hide armour: 3 hides cured by Thakkrad (5 gp each), needle + thread, Crafting 46 body / 43 legs | getYakArmour |
| 290 | Neitiznot shield (2 arctic pine logs, bronze nail, hammer, rope; stump, Woodcutting 56) and the oaths | makeShield |
| 300 | eastern cave trapdoor (north-east of the northern isle) | enterCave |
| 310 | in the cave: kill 10 trolls (varbit `fris_task` counts down), Bork's supplies once each | killTrolls |
| 320 | Ice Troll King dead: Decapitate the corpse | decapitateKing |
| 325 | head in hand: take it to Mawnis | finishQuest |
| 330/331 | helm given; two experience choices | finishQuestGivenHead |
| 340 | complete | dbrow endstate |

## The pieces the guide hides

- Jester performance: four-piece outfit worn, weapon and shield slots empty. Mawnis
  shouts Talk / Dance / Juggle / Skip / "Pie in your face" / Jig / (Bow) one at a
  time and the player presses the matching button on the `frisd_jestertask` panel;
  Fridleif and Thakkrad hold the council conversation overhead (round one: two days,
  seventeen, two bridges; round two: Etceteria, potions, the Champion helped).
  "Failing or doing nothing": "Useless fool! Return when your routine is more polished!"
- Tax: the merchant asks "how much?"; the player counts the building's windows and
  types the amount (too low "Can't you count?", too high "I'm not paying you that
  much!"); the money goes into the tax bag (empty/light/normal/hefty/bulging), never to
  the player. Vanligga's third option pays her tax for the player (5,000 coins).
- Bridges: `frisb_bridge_3_s/n` (west), `_4_s/n` (centre) have op1 Walk-across and op2
  Repair; `_5` is the runite-island bridge. Messages: "The bridge is broken. I should
  speak to Thakkrad about repairing these." / "You need a Construction level of 20 to
  repair the bridge." / "You will need a knife..." / "You need four split logs..." /
  "You need four lengths of rope..." / "I have already repaired this bridge; I can cross it."
- Supplies: Bork gives 8-10 tuna, 2-4 strength potion (4), 2-4 prayer potion (3), each
  request once; a short pack gets what fits and no more.
- Ice Troll King: after ten kills the bridge opens; he hits first. Protect from Magic,
  not Melee (Protect from Melee makes him knock the player back). Corpse and head can
  be fetched again after the quest without refighting.
