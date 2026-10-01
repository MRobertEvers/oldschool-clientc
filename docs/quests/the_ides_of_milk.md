# The Ides of Milk -- wiki-pinned brief

Source: no LostCity or 2009scape implementation (2025 quest). Constructed
from the OSRS wiki and the Quest Helper guide.

- https://oldschool.runescape.wiki/w/The_Ides_of_Milk (Walkthrough, Rewards) --
  read 2026-09-27, no oldid pinned by the tool (WebFetch summary, not a raw
  wikitext diff -- re-check against a pinned revision if this page is cited
  again for exact wording).
- https://oldschool.runescape.wiki/w/The_Ides_of_Milk/Quick_guide -- read
  2026-09-27.
- https://oldschool.runescape.wiki/w/Cowbell_amulet -- read 2026-09-27.
- quest-helper `theidesofmilk.TheIdesOfMilk` (`steps.put` 0..21 -> complete 22).

## Walkthrough (quick guide order)

1. Speak to Cassius by the Lumbridge pond, south of the windmill (3171,3277,0
   -- a fresh WorldPoint AND the quest's own `^iom_cassius_coord` agree).
   Accept ("Yes.").
2. Speak to Gillie Groats in the cow field (3254,3274,0). Choose the topic
   about cow productivity.
3. Speak to Seth Groats at his farmhouse (3223,3293,0), near the chicken
   coop northwest of the cow field.
4. Search the shelves on the southern wall of Seth's farmhouse for "The
   groats principles" (`cowquest_seth_shelf`).
5. Return the book to Cassius; drink the milk sample he gives you (near
   him); talk to him again; take the second sample to Duke Horacio.
6. Speak to Duke Horacio upstairs in Lumbridge Castle (3210-3212,3220,1).
7. Return to Gillie; drink the second sample near her.
8. Open the gate northeast of Gillie (3262,3294,0) and defeat the Bull
   (level 30), named Brutus. Mechanics below.
9. Speak to Gillie about the bull, then Cassius to finish. Claim the
   cowbell amulet and magic lamp from Gillie.

## The Bull fight (Brutus)

Quoted from the Walkthrough ("Grabbing the Bull by Its Horns") and the Quick
guide, 2026-09-27:

- Ordinary melee maxes 3 damage, and "cannot kill the player during the
  quest, as they will always hit for 0 if the player is at 1 Hitpoint."
- "After every 1-6 melee hits" Brutus does one of two specials. Both
  "ignore protection prayers" and can hit "up to 19" -- roughly 30-40% of a
  low-level character's hp -- and are "avoided entirely by moving out of the
  way before Brutus moves":
  - **Snort** ("he will then slam the ground and hit a number of tiles
    around the player"): "a 3x1 row if adjacent, or an L-shape if diagonal".
    Dodge by "stepping two tiles to either side or one tile backwards."
  - **Growl**: "Brutus charges toward the player", "fully avoidable by
    stepping two tiles to either side."

## Rewards

1 quest point; access to Brutus as a repeatable cow boss; a cowbell amulet
and a magic lamp (1,000 xp in a combat skill, including Prayer) from Gillie.

## Cowbell amulet (post-quest reward, not this quest's own completion)

Worn: speeds dairy cow milking from 7 ticks to 6. Charged with air runes
(up to 1,000 charges): teleports to the Lumbridge cow field (blocked past
Wilderness level 20; a nearby cow/calf says "Moo?"/"M-Moo?" on arrival).
"Ring" (a right-click option) hastens Brutus's respawn from 36 ticks to 13.

## Port notes (parity2c, 2026-09-27)

- Brutus's fight is real content: idesofmilk_locs.rs2's
  `[ai_opplayer2,cowboss]` rolls a real special after 1-6 swings (matching
  the quote above), telegraphs it (`npc_say`), and resolves it a tick later
  against the player's LIVE tile with real damage that ignores prayer. The
  hit-zone geometry (a 3-wide row/L for Snort, a 3-wide lane for Growl) is
  this pass's own reading of the prose above, not lifted from a decompile --
  disclosed in the code's own comments.
- `~cowquest_try_safe_death` (idesofmilk.rs2, hooked from player/death.rs2)
  is what makes Brutus's ordinary swing non-lethal during the quest, per the
  first bullet above; his specials are exempted and can genuinely kill.
- Cassius (`cowboss_farmer`) has no spawn row anywhere in the tree (Gillie
  and Seth do), so a live player cannot start the quest. The parity2c worker
  wrote one at 3171,3277,0 (the quest's own `^iom_cassius_coord` and Quest
  Helper's WorldPoint agree); the closer PARKED it at
  `build/parity_state/parity2c/parked/idesofmilk.spawn`: one more npc
  reshuffles the world RNG and the C selftest's "npcs roam inside their
  radius" stanza then catches a giant frog one tile outside its box. The
  cause is the engine's wander roll routing through
  `ToriRSServer_WorldNpcWalkTo`'s naive approach path (LostCity `Npc.wander`
  queues the rolled tile itself); the LostCity-faithful fix keeps the
  selftest at HEAD's set but turns sheepherder's herding BLOCKED (A/B:
  67/67 on the HEAD engine). Land the row with that engine fix and a
  re-tuned herd, in one engine pass.
- DONE in seam25: the magic lamp (`cowboss_reward_lamp`) Rubs through A Tail
  of Two Cats' shared xpreward picker (twocats.rs2, kind
  `^twocats_lampkind_cowboss`): 1,000 XP to one of the seven combat skills
  including Prayer, no level floor (wiki Magic_lamp_(The_Ides_of_Milk)).
- Deferred, out of scope for the quest's own completion (a post-quest reward
  feature involving the charge/teleport/travel-network systems): the cowbell
  amulet's charges, teleport and Ring-to-hasten-respawn.
- Deferred: full Cassius/Gillie refuse trees beyond the one "No." at the
  first meeting.
