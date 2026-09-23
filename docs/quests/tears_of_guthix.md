# Tears of Guthix — wiki brief

Pinned 2026-09-23 for the tearsofguthix content-parity pass
(`docs/QUEST_HELPER_COVERAGE_2026-09-23.md`). Source: live
https://oldschool.runescape.wiki/w/Tears_of_Guthix (Requirements,
Walkthrough, Rewards sections), cross-checked against the Quest Helper guide
(`quest-helper/.../tearsofguthix/TearsOfGuthix.java`).

## Requirements

- 43 Quest points.
- Firemaking 49 (boostable), Crafting 20 (boostable), Mining 20 (boostable).
- Items: a lit sapphire lantern (bullseye lantern + cut sapphire + tinderbox
  + lamp oil to make one), a chisel, a pickaxe, a rope (only needed the
  first time the Lumbridge Swamp Caves entrance is used at all — a
  once-per-account unlock, not once-per-quest).

## Walkthrough

Two routes into the chasm:

- **Option A (no prior Lost Tribe completion).** Enter the Lumbridge Swamp
  Caves through the hole in the swamp (tie a rope to it the first time).
  Inside: avoid wall beasts by stopping one square away from them, take the
  south path to avoid cave crawlers, then cross an agility obstacle to reach
  the Chasm of Tears / Juna's chamber.
- **Option B (Lost Tribe already completed).** A faster route opens through
  the Lumbridge Castle cellar — no wall beasts or agility obstacle. First
  visit still needs a pickaxe to clear rocks.

At Juna: tell her stories of your adventures (a `p_choice3`-style dialogue,
"Okay..." accepts). She explains the chasm and that a bowl is needed to
collect the tears, and that the bowl must be made from stone mined on the
south side of the chasm.

Crossing the chasm: climb the rocks, then use a lit sapphire lantern on
("attract") a light-creature; it carries the player across.

Making the bowl: mine a magic stone rock (pickaxe) on the south side, then
use a chisel on it to make a stone bowl. Return across (attract another
light-creature or backtrack) and hand the bowl to Juna.

Juna keeps the bowl for future visits and lets the player collect the Tears
of Guthix (a separate, repeatable, once-weekly minigame) from then on.

## Rewards

- 1 Quest point.
- 1,000 Crafting experience.
- Access to the Tears of Guthix minigame (weekly).

## Port notes (2026-09-23 parity pass)

- The port's `quest_tearsofguthix` (`tearsofguthix.rs2`,
  `tearsofguthix_lantern.rs2`) already implements Juna's dialogue, the
  chasm-crossing light-creature attract, the stone mine and the chisel→bowl
  craft, and the hand-in/completion/reward. Option A's entrance (rope +
  `tog_cave_down`) had NO real route in before this pass — see
  `build/parity_state/parity1/tearsofguthix.parity.json` for the fix.
- Option B (the Lost Tribe / `lost_tribe_hole_2` cellar route) is DEATH TO
  THE DORGESHUUN's own leg (`dttd_savezanik.rs2`), out of this quest's
  directory and this pass's scope; not touched.
- The wall beasts / cave crawlers / agility obstacle hazards along Option A
  are not modelled (no `[oploc*]`/`[opnpc*]` for them found anywhere in this
  pack) — the port's entrance is a straight walk once the rope is tied,
  matching the pack's own established "soft" idiom for OSRS geography this
  content does not otherwise render (see the chasm-crossing and
  climbing-rock procs in `tearsofguthix_lantern.rs2`, which are the same
  shape). Left as a legs_left item, not fixed in this pass.
