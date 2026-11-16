# Make-X batch timing (2026-11-16)

Every production loop behind interface 270 `skillmulti`, checked against RSProx
after cooking was found running a batch too fast (OSRS-Content 3b01bbb1f1).

## Method

- Sessions: every session that opened `skillmulti` (137), the newest 15
  normal-world sessions per production seq, and sessions found by each skill's
  private game message. Leagues / Grid Master sessions are kept apart: their
  relics boost smelting, smithing, fletching and leather to 1-2 ticks.
- The index stores every player's seqs, so a seq hit is usually someone else at
  the GE. Only the recorder's own `update_stat_v2` and messages count.
- Per batch: the `resume_pausebutton com=skillmulti` tick (the pick), then the
  ticks of that skill's xp drops. Click ticks jitter by one.
- No recording: the wiki recipe infobox `ticks =` field (`?action=raw`),
  labelled unverified.
- `p_delay(n)` costs n+1 ticks.

## What the recordings show

| action | session | pick -> first | gap | n |
|---|---|---|---|---|
| cooking (fire/range) | 4503, 4079, 4461, 4500 | 2-3 (from panel open) | 3 then 4 | 250+ gaps |
| smithing (anvil) | 3449 | 3 | 5 | 9 |
| weaving, bolt of linen | 3449 | 3 (6 of 7; one 4) | 3 | 43/43 |
| spinning, linen yarn | 3449 (rev 235, Seers' wheel) | 2-3 | 2, seq `human_spinningwheel_60` | 93/93 |
| gem cutting | 4494 (+16 unboosted Leagues) | 1 | 2 | 16 of 20 first at 1 |
| herblore, finished | 4076, 4187, 4392, 4079 | 1 | 2 | 97 |
| herblore, unfinished | 3123 | 0-1 | 1 | 45 |
| ogre arrow shafts | 4503 | 3 | 3 | 1 |
| ogre headless / wolfbone tips | 4503 | 1-2 | 2 | 4 |
| construction (sailing parts) | 3443, 3449 | 3-4 | 4 | 43 |

Weaving, spinning and construction time the first item from the pick, a full
cycle later. Cooking times it from the panel opening. Gems and herblore resolve
the first one tick after the pick.

The 3449 spin panel reads "Linen yarn|Bow string" [6099], and the wheel plays
`loc_anim spinningwheel (466)` with synth 2590 on each item.

## Fixed (2026-11-16)

| file | was | now | evidence |
|---|---|---|---|
| weaving.rs2 | item on the pick tick, then 3 | first at +3, then 3 | 3449 |
| spinning.rs2 | item on the pick tick, then 3, seq _90 | first at +3 / 3 (_90); linen +2 / 2 (_60) | 3449 (linen); wiki 3 for wool/bow string, unverified |
| spinning.rs2 | Bow string / Linen yarn | Linen yarn / Bow string | 3449 [6099] |
| uncut_gem.rs2 | item on the pick tick, then 2 | first at +1, then 2 | 4494 |
| ogre_arrows.rs2 (shafts) | 2 ticks | 3 ticks | 4503 + wiki |
| wine.rs2 | 2 ticks | 3 ticks | wiki only, unverified |

## Open (no recording; wiki or analogy only)

| action | ours | wiki | note |
|---|---|---|---|
| leather, snakeskin, fabric armour, gold/silver jewellery, pottery wheel, glassblowing, dough, amethyst, bow stringing | first item on the pick tick | - | probably a full cycle early, as weaving was |
| bow stringing | 3 | 2 | ours slower |
| amethyst cutting | 3 | 2 | ours slower |
| bread/pastry dough | 3 | 1 | ours slower |
| battlestaves | one per use, instant, no seq | 2, make-X | cache has `human_battlestaff_crafting` (7531) |
| furnace smelting | 4 | bronze/gold 5, iron/steel 4 | |
| pottery firing | 6 | 7 | |
| wooden shields | p_delay(7) = 8 | 7 | the comment says 7 |
| spinning | no loc_anim | - | 3449 animates the wheel on each item |

These are already right: cooking, anvil smithing, herblore, log cutting (3),
headless arrows and arrowtips (2), amulet stringing (2, index 4185).
