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

### parity1b pass (2026-09-23) — the light-creature spawn

- "Crossing the chasm: climb the rocks, then use a lit sapphire lantern on
  (attract) a light-creature" was real, already-implemented content
  (`tearsofguthix_lantern.rs2`'s `[opnpc1/opnpcu,tog_light_creature_op]` →
  `~tog_attract_light_creature`) with nothing to click it on: no
  `tog_light_creature*` npc was ever placed on the live map
  (`areas/world/configs/m50_148.spawn` lists only `tog_juna_dummy`), so a
  player who reached the chasm through parity1's own rope/`tog_cave_down`
  fix (rather than `::toglantern`'s debug `npc_add`) still had nothing to
  attract. Fixed: a lit sapphire lantern used ON the climbing rocks at
  either bank (`[oplocu,tog_climbing_rocks_down]` /
  `[oplocu,tog_climbing_rocks_up]`, new `[proc,tog_summon_light_creature]`)
  summons a real, player-owned `tog_light_creature_op` beside the player —
  the same `npc_find`/`npc_add`+`npc_setowner` idiom
  `[proc,tog_bind_speaker]` already uses for Juna's own chathead, so it is
  never visible to anyone else. Proven end-to-end through the real client,
  both directions of the crossing, then mine → chisel → bowl → hand-in →
  quest complete (`build/parity_state/parity1b/tearsofguthix.parity.json`).
- Found along the way, NOT fixed this pass (pre-existing, upstream of the
  spawn, flagged in open_issues): the quest's own reverse-direction
  `[opheldu,tog_sapphire_lantern_unlit]` (armed lantern, clicked tinderbox)
  does not actually light the lantern in this harness —
  `[opheldu,tinderbox]` (`skill_firemaking/scripts/firemaking.rs2:25`), the
  shared "light X" dispatch table every other lightable item merges into
  (see that file's own Olaf comment), has no case for
  `tog_sapphire_lantern_unlit` and answers its dm_default first when armed
  the OTHER way. A real player using "tinderbox → lantern" (the more common
  real-game order) would hit this. Worth a follow-up pass to
  `firemaking.rs2`, not `tearsofguthix_lantern.rs2`.
- Closer follow-up (same pass): `firemaking.rs2`'s `[opheldu,tinderbox]`
  now carries a `tog_sapphire_lantern_unlit` case that jumps to
  `@tog_light_sapphire_lantern`, so both click orders light the lantern.
  Proved: `build/quest_gate/close_proof_1b/ledger.tsv` rows 1-4
  (`light.tinderbox_on_lantern`, `light.lantern_on_tinderbox`, both
  "You light the sapphire lantern.").

### parity1c pass (2026-09-23) — Option A hazards (wall beasts / cave crawlers / agility obstacle)

This pass's own work item, correcting parity1/parity1b's "no `[oploc*]`/
`[opnpc*]` for these hazards exists anywhere in the pack" — that was wrong;
the roster npcs and the agility loc are all real, cache-extracted content.
What was actually missing/broken differs per hazard:

- **Wall beasts — FIXED.** `swamp_wallbeast` ("Hole in the wall") is a real
  roster npc already spawned at five real cache-extracted positions
  (`areas/world/configs/m49_149.spawn`, `m50_149.spawn`; source: dennisdev/
  rs-map-viewer, same as every other `.spawn` row), squarely on the Option A
  route between the rope entrance and `tog_cave_down`. But it carried ZERO
  combat capability anywhere (no `combat_stats.generated.npc` entry, no
  `huntmode`) — a player could already walk straight through every one with
  no hazard at all. Fixed with a hand-authored combat overlay,
  `quest_tearsofguthix/configs/quest_tearsofguthix.npc`, using the SAME
  idiom this pack already uses for this exact gap class
  (`quest_zanaris.npc`'s `tree_spirit`/`zombie_entranan`, `quest_ikov.npc`'s
  `ikov_lucien2`): the cache's own `stat1-4` fields already sitting on this
  npc (attack=30, defence=16, strength=30, hitpoints=105), which match the
  OSRS Wiki's "Wall beast" page exactly (combat level 49 — this npc's own
  `vislevel`/`swamp_wallbeast_combat` vislevel — and 105 hitpoints;
  https://oldschool.runescape.wiki/w/Wall_beast, read 2026-09-23), plus
  `huntmode=aggressive`, `huntrange=1` (the wiki's own "stop one square
  away... run past it" avoidance line, taken as the range spec), Crush
  damage type and 4-tick attack speed (also wiki-sourced). No scripting
  beyond the stat block — the engine's generic aggressive-npc AI
  (`torirs_server_combat.c`) does the rest, the same mechanism that already
  drives cave crawlers. Proven through the real client, both the damage
  (adjacent → hitpoints fall) and the avoidance (≥3 tiles → hitpoints hold)
  — `build/parity_state/parity1c/scratch/tearsofguthix_hazards.lua`, rows
  `wallbeast.grabbed_and_damaged` / `wallbeast.safe_at_distance`. The
  overhead name stays "Hole in the wall" rather than swapping to "Wall
  beast" on aggro — this engine has no `npc_settype`/runtime-retype opcode
  (checked, none registered) and no passive-proximity disguise-swap idiom
  exists anywhere in this pack to build one from safely; flagged, not
  hidden.
- **Cave crawlers — ALREADY REAL, nothing to fix.** `slayer_cave_crawler_1`
  (and `_3`/`_4`) are already spawned at real cache positions in
  `m49_149.spawn`, and already fully combat-capable
  (`combat_stats.generated.npc`: `huntmode=aggressive`, `huntrange=5`,
  hitpoints=22) — the same generic engine mechanism as the wall beast fix
  above, requiring no quest-side content at all. Proven live and hostile:
  `tearsofguthix_hazards.lua` row `crawler.engaged_when_close`.
- **Agility obstacle — investigated, NOT fixed, a real pre-existing gap in
  shared content.** `swamp_cave_steppingstone_a`/`_b` and the
  `maplink_agility` table row for it
  (`skill_agility/configs/maplink_agility.dbrow`
  `maplink_agility_0_50_149_4_36`, 30 Agility XP) are both real and already
  wired — but the obstacle cannot actually be clicked by any player. No
  quest anywhere in this pack has ever driven one through the real client
  before (checked: no `test/quests/*.lua` references any maplink_agility
  loc). `~maplink_agility` (`skill_agility/scripts/maplink_agility.rs2`)
  keys its `db_find(maplink_agility:src, coord)` on the PLAYER'S OWN
  coordinate at click time — the player must stand exactly on the row's src
  tile (world 3204,9572, decoded from local(4,36) in region 50_149) — but
  that tile is 2 squares from the loc's own tile (3206,9572, confirmed by
  `world.loc_near`, `match=exact`), outside the generic engine
  interaction-reach check's adjacency requirement
  (`cannot_reach_message`, `torirs_server_world.c`). Proved the conflict
  both ways through the real client: pressing from the src tile times out
  on the reach check every retry; pressing from a tile adjacent enough to
  satisfy reach lands the click but answers content's own `dm_default`
  ("Nothing interesting happens") because that tile is not the row's src —
  `tearsofguthix_hazards.lua` rows `obstacle.press_from_src` /
  `obstacle.press_from_adjacent`. The likely real fix (`blockrange=0` on
  the loc, matching its own working cache-sibling `brew_stepping_stone`,
  which already carries `blockrange=0`) lives in `configs/all.loc` — a
  machine-unpacked cache dump ("Unpacked by cachepack... lossy", its own
  header says, not meant for hand edits) OUTSIDE `quest_tearsofguthix`'s
  directory, and this engine's loc loader does not merge a quest-owned
  `.loc` override onto an already-cache-defined symbol (`ContentLoc` returns
  the FIRST `[symbol]` block found walking the tree, and `configs/` sorts
  before `server/` at the content root, so a quest file's override for an
  `all.loc` symbol is silently dead — confirmed this is why `quest_mm.loc`'s
  own working `next_loc_stage` override pattern does not generalise here:
  that symbol has no base `all.loc` entry to lose the race against). Bigger
  and riskier than this pass's own directory-scoped budget; flagged for
  whoever owns `configs/all.loc` / the shared reach-check system next.
