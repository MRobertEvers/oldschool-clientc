# Ratcatchers wiki brief (parity3f closer, 2026-10-01)

Source: no LostCity quest (neither LostCity_Content2 nor LostCity_Server has a
`quest_ratcatchers`). The port is constructed from the OSRS wiki and the Quest
Helper ladder (`helpers/quests/ratcatchers/RatCatchers.java`).

| Reference | Use |
| --- | --- |
| https://oldschool.runescape.wiki/w/Ratcatchers | walkthrough, King Rat fight, snake charmer, rewards |
| quest-helper `RatCatchers.java` | step ladder (steps 60/62/65/70/75), coins101, fish requirement |

## King Rat (Jack's warehouse wall) -- what content models
- Use the cat on the hole in the wall (not the rat holes); two options:
  "Be careful!" -- the cat retreats at 1 hitpoint and the fight can be
  restarted; "Don't hold back!" -- it fights until it or the rat dies (the
  cat can be lost).
- King Rat: 10 hitpoints, maximum hit 1. Fish are used ON THE HOLE IN THE WALL,
  not on the cat, to heal it.
- The cat's own strength/HP by type and maturity is not modelled (one profile:
  5 health, hits 0-2); see the PARITY row.

## Snake charmer (Pollnivneach)
- Ali the Snake Charmer charges 100 coins for the music scroll and the snake
  charm (Quest Helper asks for 101); a worn Ring of Charos(a) lowers it to 50
  and skips the walking dialogue. Content: 101 / 51 (Quest Helper's +1 kept).

## Rewards
2 Quest points; 4,500 Thieving experience; a rat pole; overgrown cats can be
trained into wily and lazy cats and named; access to the Rat Pits and the
Minigame Teleport to the rat pits (Port Sarim, Ardougne, Varrock, Keldagrim).

Driving notes: docs/quests/ladders/ratcatchers.notes.md.
