Ratcatchers notes (driven 2026-09-30, source = wiki + Quest Helper; LostCity has no quest).

Stage numbers: the port uses its own %ratcatch_var values (ratcatchers.constant), not the guide's
steps.put keys. 60 = Apothecary done, 62 = Jack has seen the cat antipoison, 65 = King Rat dead.

Hooknosed Jack's warehouse (3265-3275, 3375-3388):
- The ladder (3268,3379,0) is INSIDE the ground floor. Jack stands at 3268,3401. Walk to 3272,3382 and
  click the door fai_varrock_poor_door (3272,3380) first; the ladder is unreachable from outside.
- Never ::goto 3268,3380,1: that square is walled off from the loft. Climb the ladder (lands 3269,3379,1).
- King Rat wall: vc_blank_walldecor (3270,3379,1), reachable from the loft. Use the cat ITEM on it, then
  Yes, then "Be careful in there, cat!" or "Don't hold back!".
- Fight (ratcatchers.rs2:393-470, timer ratcatch_kingrat_round, a round every 3 ticks): cat 5 health,
  King Rat 10, cat hits 0-2, rat hits 0-1. A fish used on the WALL heals 2 (not on the cat).
  "Be careful" retreats at 1 health, so feed a fish and send the cat in again. "Don't hold back"
  fights to the death and the cat item is deleted. Leaving the loft ends the fight.
- Jack must be talked to once after the Apothecary with the antipoison in the pack (stage 60 -> 62).
- Rat poison: Jack mixes it for vial + kwuarm + red spiders' eggs, or bring poison itself. Poison
  cheese by using rat poison on 4 cheese; leave one at each of the 4 holes (oplocu).

Pollnivneach: the money pot feud_money_bowl; Ali speaks by name through ~ratcatch_charmer_say
(ratcatchers.rs2:697), because a loc trigger has no active npc. 101 coins, or 51 with
ring_of_charos_unlocked worn (wiki: the Charos ring cuts the price).

Differences from the guide:
- Sewer rats: the guide's pitrat_sarim_def has no sewer spawn; the generic rat in the sewer box is
  pressed with op 2 (Attack; the cache rat has no op 1) at ratcatchers.rs2:93-133. Each catch needs
  the mesbox dismissed and deletes the rat; pick rats with t.npc.tiles(z 9855-9919), 8 needed.
  Bring 70 hitpoints: zombies and giant rats kill a level-3 account. The cat is the inventory item.
- Mansion rats: the cache party rat has only Examine and server .npc ops never reach the client menu,
  so trellis climb places 6 private stationary vc_rat (op 2) on the spawn tiles (ratcatchers.rs2:206-263).
  Stand 3 tiles east or 1 west of each (2832,5098 and 2863,5101 are reachable only from one
  side); re-read the slot after walking, it changes. Caught state = vc_raton_off1..6 varbits.
- Mansion guards are static spawns; no sight/patrol rule is ported (see legs_left).
- The snake charm tune plays on widget 282; play it near 3010-3028, 3224-3240 outside the pits.
