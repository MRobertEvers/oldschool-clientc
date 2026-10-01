# Regicide wiki brief (parity3f, 2026-09-30)

Source: no LostCity quest exists (LostCity_Content2 and LostCity_Server have no
`quest_regicide`; the old header comments claiming a LostCity port were wrong).
The port is constructed from the OSRS wiki and the Quest Helper ladder.

| Reference | Use |
| --- | --- |
| https://oldschool.runescape.wiki/w/Regicide | requirements, walkthrough, rewards |
| https://oldschool.runescape.wiki/w/Transcript:Regicide | every dialogue (all NPC lines in content follow it) |
| https://oldschool.runescape.wiki/w/Transcript:Iorwerth%27s_message, Transcript:King%27s_message | the two scrolls |
| quest-helper `helpers/quests/regicide/Regicide.java` | step ladder, varp numbering 0..14 |

## Requirements
Underground Pass (Biohazard, Plague City), Crafting 10 to start, Agility 56
(boostable) for the dense forest, a level 110 Tyras guard to kill (safespot ok).

## Stage ladder (varp regicide_quest)
0/1 talk to King Lathas (messenger optional) -> 2 go through the pass and the
Well of Voyage; Idris scene -> 3 scouts spoken -> 4 Lord Iorwerth -> 5 tracker
wants proof (crystal pendant) -> 6 pendant shown, "Follow" the tracks -> 7
tracks found -> 8 tracker explains the dense forest; first forest summons a
Tyras guard -> 9 a Tyras guard killed (the private one, or the camp-entrance
one) -> 10 Tyras camp entered (General Hining; two barrels, tar, sulphur may be
gathered now) -> 11 Iorwerth gives the Big Book o' Bangs and answers the
ingredient questions -> bomb made (limestone on a furnace in gloves, pestle on
quicklime with a pot, pestle on sulphur, Chemist "Your quest", barrel of coal
tar on the fractionalising still, ground quicklime and ground sulphur into the
naphtha, strip of cloth fuse) -> cooked rabbit to the catapult guard, bomb on
the catapult with a tinderbox -> 12 Tyras dead -> 13 Iorwerth hands the message
-> 14 Arianwyn near Ardougne Castle opens it -> 15 hand it to Lathas.

## Rewards
3 quest points, 13,750 Agility XP, 15,000 coins, Tirannwn and Arandar access,
dragon halberd, Iorwerth camp and Zul-Andra teleports, BJS fairy ring.
The crystal pendant is removed when talking to Iorwerth after use and at the end.

## Known unsourced
Elena (regicide_elena_chat varbit) is told about the book before the Chemist per
the walkthrough; the transcript has no Elena lines, so none are written.
