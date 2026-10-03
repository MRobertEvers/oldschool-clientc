# Meat and Greet (wiki-pinned brief)

Pinned 2026-10-03 for parity b55. Not a LostCity quest. Quest dir
`OSRS-Content/osrs239-content/server/scripts/quests/quest_meatandgreet/`, progress
var `varb11182_mag` (carrier `varp4399_mag_primary`), complete = 26.

## Sources (wiki oldids, fetched through api.php)

- Meat_and_Greet 15355341 (walkthrough, rewards, fight notes)
- Transcript:Meat_and_Greet 15263407 (every npc line; marked incomplete for the
  Lelia dialogue between eating the kebab and being told to market it)
- Dire_Wolf_Alpha 15208071, Dire_Wolf 15208070 ("Meat and Greet" version, id 13813),
  Minotaur_(Meat_and_Greet) 15200556
- Quest Helper `helpers/quests/meatandgreet/MeatAndGreet.java` (stage ladder 0..24 and
  the progress-var comments)

## Stage ladder

| stage | meaning | writer |
|---|---|---|
| 0/2 | start offer; "Yes." writes 2 and, at the end of the speech, 4 | Emelio |
| 4 | spice (varb11183) 1->2 merchant, 3 code, 4 sent; meat (varb11184) 1->2 Alba, 3 alpha dead, 4 Alba told; the second finisher writes 6 | Spice Merchant, Alba |
| 6 | "the critical step: the recipe" writes 8, portions all 1 | Emelio |
| 8 | ratio menu (varb11185-11188), test kebab (varb11189), taste (varb11190); good taste writes 10 | Emelio, connoisseurs |
| 10, 12 | "Marketing!" writes 12, two Test kebabs write 14 | Emelio |
| 14 | Lelia eats a kebab (16), pitch (18) | Lelia |
| 18, 20 | "I think so." enters the arena; the first entry eats the second kebab (20) | Lelia |
| 22 | Minotaur dead | Minotaur death |
| 24 | Lelia's send-off | Lelia |
| 26 | Emelio: 8,000 Cooking XP, 1 quest point, Emelio's Kebab Shop | Emelio |

Recipe: meat 4, salad 2, spice 1, sauce 3 (Vincens: sauce = spice + salad; Renata: spice <
salad; Lucas: meat = 2 x salad). Spice box code 2546 (interface 888).

## Combat (stats agree with the cache's stat1..6)

- Dire Wolf Alpha: level 113, 100 hp, 120/110/80, stab, speed 4, max 12, calls ranged pups.
- Dire Wolf pup: level 72, 10 hp, 75/75/70, ranged 20 (+500), speed 4, max 3.
- Minotaur: level 193, 240 hp, 140/110/100, magic 100, speed 5, max 14; melee x3 then
  "Moo!" magic (x2 below 50%), "Moooooo!" and one tick faster at 50% and 25%.
