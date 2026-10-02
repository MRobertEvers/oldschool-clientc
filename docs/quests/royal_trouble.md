# Royal Trouble -- wiki-pinned brief

Source: https://oldschool.runescape.wiki/w/Royal_Trouble (walkthrough, rewards,
changes), https://oldschool.runescape.wiki/w/Transcript:Royal_Trouble (every
dialogue line), and the item/scenery pages (Burnt diary, Lift manual, Rocks,
Crevice, Tunnel, Giant Sea Snake, Heavy box, Scroll, Letter), read 2026-10-02.
Guide ladder: quest-helper `helpers/quests/royaltrouble/RoyalTrouble.java`.
Royal Trouble (22 May 2006) post-dates LostCity, so no LostCity script exists:
the port is built from this brief and the guide.

## Requirements and rewards

- Throne of Miscellania complete (so Heroes' Quest and The Fremennik Trials);
  Agility 40 and Slayer 40, both boostable. The Agility 40 is real gameplay: the
  three crevices and the tunnel need it (1 xp on the way deeper, none coming back).
- Rewards: 1 quest point, 20,000 coins (Sigrid), 5,000 Agility, Slayer and
  Hitpoints xp, hire Bosun Zarah as a crewmate (80 Sailing), enhanced Managing
  Miscellania rewards, dock at Etceteria (65 Sailing).
- Kill: Giant Sea Snake, level 149, 100 hitpoints, melee close / ranged from a
  distance, poison 9. Needs 40 Slayer to damage. Cannot be fought after the quest.

## Varbits (Quest Helper values are the spec)

| varbit | values |
| --- | --- |
| ROYAL_QUEST (varb2140) | 0 start, 10 Ghrim said yes, 20 Brand/Astrid scene done, 30 complete |
| ROYAL_MISC (varb2141) | 10 Vargas gave the task, 20 both islands reported, 30 Ghrim, 40 sailor, 50 scroll, 60 down the ladder, 80 Donal, 110 Armod, 120 snake dead |
| ROYAL_ETC (varb2142) | 10 Sigrid, 20 reported back, 40 box handed in |
| ROYAL_*_VILLAGERS_ABOUTTHEFTS | 1 once a citizen of that island has been asked |
| ROYAL_LIFTSTAGE | 1 pulley, 2 longer pulley, 3 pulley 2, 4 rope, 5 beam on platform, 6 engine placed, 7 repaired (5 coal), 9 rode up |
| ROYAL_COALINENGINE | 0..5 lumps, any time before or after the engine is placed |
| ROYAL_MISC_USEDMININGPROP, ROPELOC | prop wedged in the crevice; rope on the rock |
| ROYAL_MISC_NUMBEROFCHAPTERS | 0..5 diary pages found, in path order |

## Walkthrough (what the player does)

1. Ghrim ("Has anything been happening...", "Yes.") -> Brand or Astrid (the war
   scene, a wiki cutscene) -> King Vargas ("Right away") -> ask one citizen of
   Miscellania (Gunnhild by the herbs, Leif, Frodi, Magnus) -> Sigrid in
   Etceteria -> ask one citizen (Helga, Haming, Matilda, Ashild, Moldof, Arnor)
   -> (Sigrid again) -> Vargas -> Ghrim -> the Miscellania sailor -> Vargas for the
   scroll.
2. The dungeon ladder south of the castle: the guard takes the scroll. Donal in
   the pub gives the mining prop. A bronze pickaxe sticks in the rock by the
   dwarves. Use the prop on the crevice in the north-west corner, squeeze through.
3. The lift room: crates give an engine's worth of parts -- 3 beams, 3 pulley
   beams, 2 ropes (+ the engine on the floor, the manual on the table). Pulley
   beam on the scaffold; beam on pulley beam = long, + beam = longer, longer on
   the scaffold; a third pulley beam; a rope; a beam on the platform; the engine
   on the engine platform; 5 coal (own, or mined from the four rocks) in the
   engine; Use-Lift with a spare rope.
4. Upstairs: take the plank, tunnel on the east wall (beams will not fit through),
   rope on the rock, swing over the water. Five fire remains give the five pages
   of Armod's burnt diary; the slippery rocks (8 damage a go) are crossed with
   the plank; steam vents and falling rocks hurt by Hitpoints level. Read the
   diary, squeeze through the crevice at the end.
5. Armod and the four others confess (a wiki cutscene opens it). Through the next
   crevice: kill the Giant Sea Snake, take the heavy box (stays 5 minutes), back
   out, the guard's rope home (near the hole in south-east Etceteria; the guard
   returns a lost box).
6. Sigrid takes the box: 20,000 coins and a letter. Vargas takes the letter:
   quest complete.

## OSRS changes that apply

- 9 Nov 2022 (Diversity & Inclusion): the player may romance Brand or Astrid
  regardless of gender or choose to be a best friend instead; the war scene and
  Armod's diary entry still use the old "daughter/son" text. The port reads
  %varb14607_misc_partner_multivar (1 Brand, 0 Astrid) and has no friend flag.
- 4 Apr 2013: diary page retrieval bug fixed (pages come in path order).

## Cutscenes (owner spec pending: docs/quests/cutscenes/)

Two wiki cutscenes: the Brand/Astrid throne-room scene and the kids around their
fire when the player squeezes through the crevice. Their camera sequences are not
ported here; the dialogue pages play where the player stands.
