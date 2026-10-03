# Meat and Greet: what the ladder cannot tell you

Source: OSRS wiki oldids 15355341 (quest), 15263407 (transcript). No LostCity.

Where things stand
- Emelio 1754,3074 stands inside a fenced yard; its door is shut. Lucas, Renata and
  Vincens stand OUTSIDE the fence (1749-1751,3072), so a walk from Emelio needs the door.
- Emelio, Lucas, Renata, Vincens are multinpc shells (mag_emelio, mag_lucas ...). The
  server finds a trigger by the spawned shell, so scripts bind the shell, not _1op/_vis.
  The connoisseurs only exist while the quest is at 6..14.
- Spice Merchant 1685,3101. Alba 1587,3126. Lelia 1819,9484 in the lobby, 38 tiles
  south of the colosseum stairs (arrival 1805,9522); walkable.
- The Wolf Den mouth (1496,3131) is only reachable from the EAST (1498,3131): the
  blocked cliff band runs west of it, so 1496,3128 is a dead end.

Gates and how they open
- Den: Alba must have asked (meat 2). Then the loc builds a private copy of m23_148; the
  exit loc is added at 1488,9502,1. Exit asks first, Quick-exit does not.
- Spice box: dialogue opens interface 888 (number_pad), digits 2546, submit arrow.
  A wrong code keeps the pad open. Close leaves the box locked.
- Arena: only through Lelia ("I think so."). A private copy of m28_48; no exit loc
  inside, so leave by teleport or death. Stage 18 needs the kebab; stage 20 does not.
- Colosseum entrance (twilightspromise.rs2:355) puts stages 14..24 in the lobby.

Dialogue options that matter
- Start: "Yes." Spice: "I'm here about a missing delivery." (first visit).
- Emelio ratio menu: adjust each ingredient, then "I think that's perfect!" ->
  "Sounds good to me." Right answer is meat 4, salad 2, spice 1, sauce 3.
  A wrong recipe still hands the kebab; the connoisseur says "Eww!" and Emelio
  re-opens the menu ("They didn't like it.").
- Stage 20 Lelia uses the transcript's "I think so."; Quest Helper writes "Okay,
  I'm ready." there. Unresolved.

Fights
- Dire Wolf Alpha: 100 hp, stab max 12, aggro range 8. In melee reach it may call a
  ranged pup (max 3, accurate); pups vanish when it dies. Drops wolf bones.
- Minotaur: 240 hp, crush max 14, speed 5 (4 below half, 3 below a quarter).
  Melee x3 then "Moo!" and a magic swing one tick later (x2 below half). The wiki
  says it is weak to slash. A 99/99/99 character in rune with a spear and 25 sharks
  won both (alpha 53 ticks, minotaur 135-179 ticks, 7 sharks).
- Protect prayers are untested through the driver (no prayer verb).

Different from the guide
- Colosseum arrival and the arena death respawn in the lobby (wiki trivia) are
  not ported; a death uses the ordinary respawn.
- "Let's trade." and Emelio's Trade open nothing yet: the Fortis Spice Stall and
  Emelio's Kebab Shop stock files are not generated (tools/gen_shop_scripts.py).
