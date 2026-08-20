# The Gauntlet / Corrupted Gauntlet / Crystal equipment

> Written 2026-08-04; updated for procedural rooms, `inv_setvar`, floor
> pattern sets, diary gate, and crystal tool drain.
> Behaviour authority: [OSRS wiki — The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet),
> [Reward Chest (The Gauntlet)](https://oldschool.runescape.wiki/w/Reward_Chest_(The_Gauntlet)),
> [Crystalline Hunllef](https://oldschool.runescape.wiki/w/Crystalline_Hunllef),
> [Corrupted Hunllef](https://oldschool.runescape.wiki/w/Corrupted_Hunllef),
> [Tornado (The Gauntlet)](https://oldschool.runescape.wiki/w/Tornado_(The_Gauntlet)),
> [Crystal equipment](https://oldschool.runescape.wiki/w/Crystal_equipment) /
> [Crystal singing](https://oldschool.runescape.wiki/w/Crystal_singing) /
> [Ilfeen](https://oldschool.runescape.wiki/w/Ilfeen).
> Map squares and loc/obj names measured from the osrs239 tree (jl2 / configs).
> LostCity: **none** (Song of the Elves / post-254). Kronos: no Java minigame
> (RuneLite plugin only). Re-measure coords rather than trusting this prose.
>
> **Secondary reference (not authoritative):** Near-Reality's excluded Gauntlet
> plugin, `plugins/excluded/gauntlet`, package
> `com.near_reality.game.content.gauntlet` (Andys1814, 2022-01). Used for the
> numbers the wiki shows only as images or prose — the tornado clock, the floor
> pattern geometry, the trample roll — and for nothing else. Where it disagrees
> with the wiki (its max hits are deliberately nerfed for its own combat
> system) the wiki wins; where it has an outright bug it is not copied, and the
> one that matters is noted under "Boss encounter" below.

Content lives under `OSRS-Content/.../server/scripts/minigames/minigame_gauntlet/`.

## Placement (engine vs content)

LostCity has no Gauntlet. Behaviour is content (`.rs2` + configs) plus two
engine surfaces:

| Surface | Role |
|---|---|
| `map_instance_*` | 14×14-zone instance (7×7 of 16-tile rooms). Scene window stays 13×13; instance storage raised to 16 (`TORIRSSERVER_MAPINSTANCE_ZONES`) |
| `inv_setvar` / `inv_getvar` (EXTRA **11016** / **11017**) | Per-slot ints keyed by obj — LC commented signatures, never wired there. Crystal charges use the item obj as key |

## Map squares (measured)

| Role | Square | Notes |
|---|---|---|
| Prif portal | `m50_95` loc `gauntlet_lobby_entrance` @ local `29,34` | SotE-gated → lobby |
| Lobby hub | `m47_95` plane 1 | Shared. Land `24,43` |
| Room library (normal) | `m29_88` | 4×4 of 16-tile rooms on plane 1 (door pitch measured in jl2) |
| Room library (corrupted) | `m30_88` | Same layout, `_hm` locs |

**Procedural 7×7:** `~gauntlet_build_instance` allocates 14×14 zones, pins start
room (3,2) and boss room (3,3) from template cells (3,2)/(3,3), shuffles the
other 47 rooms from the 4×4 library with random quarter-turns (2×2 zone remap).
Start/boss land tiles stay `55,40` / `55,55` (local offsets inside those rooms).

## Session flow (wiki)

1. **SotE gate**: portal / enter require `%sote ≥ ^sote_complete`.
2. **Portal** → lobby; **Enter** stores gear in `gauntlet_holding`, builds the
   instance, start kit, prep timer **1000 / 750** ticks.
3. **Rooms**: procedural assembly; monsters at remappable room centres; sceptre
   Lights `prif_gauntlet_door_wall_unlit_*`.
4. **Boss — Hunllef**: see below.
5. **Leave / death / rewards**: unchanged (points / completion chest).

## Boss encounter

Everything below is measured from the 12×12 walkable arena, whose south-west
corner is `^gauntlet_arena_lx/lz` — `boss_room * room_tiles + 2`, the same
bound the reference's barrier plugin enforces. Not from the Hunllef's own tile:
the boss moves, the arena does not.

| Mechanic | Behaviour |
|---|---|
| Attack style | Ranged↔magic every 4 attacks (`hunllef_attack_transition_*`). Both projectile attacks animate with `hunllef_attack_ranged` (seq 8419) — the Hunllef has one projectile animation, not two |
| Protection | Swaps to the style of the 6th off-prayer hit; hits of the protected style are zeroed and do **not** count |
| Prayer disable | 15 in 101 of magic attacks (`^gauntlet_prayer_disable_pct`), distinct travel projectile, resolves on impact with `crystal_hunllef_prayer_impact` |
| Trample | Player inside the 5×5 footprint: `hunllef_attack_melee` (8420), 8..`^gauntlet_stomp_*_max`, "You're trampled beneath the …" |
| Tornadoes | A **clock**, not a dice roll: `^gauntlet_tornado_cooldown` = 56 ticks, counted down on the boss tick and reloaded on entry and after each wave. Count = HP third, +1 corrupted. Summon animates `hunllef_attack_special` (8418) — that seq is the tornado summon and belongs to nothing else. Each spawns on a random arena tile (re-rolled off the player), lives 15 ticks, hunts, and bursts `crystal_hunllef_crystals_hit` on contact |
| Damaging floor | Pattern every `^gauntlet_floor_cycle` (30) ticks: 5 ticks warning form, 6 damaging, then **dark** for the rest of the cycle. A pattern is 1–5 axis-aligned rectangles over the arena, not loose tiles |

Pattern sets are chosen by HP third (`%gauntlet_hunllef_hp_phase`):

| Phase | Set | Shapes |
|---|---|---|
| 1 | EASY, 12 | four 6×6 corner blocks, four 6×6 shifted inward, four single 4×12 / 12×4 bands |
| 2 | EASY + MEDIUM, 14 | the above, plus both 4×12 bands together and both 12×4 bands together (96 of 144 tiles lit) |
| 3 | HARD, 4 | four 3×3 corners + 4×4 centre; a two-deep outer ring; four 4×4 inset; four 4×4 flush |

The reference builds its phase-2 set by appending MEDIUM onto the *shared
static* EASY list, so its phase-2 set grows by two more copies every time any
fight reaches phase two. That is a reference bug and is **not** reproduced —
`~gauntlet_floor_rect_easy` / `~gauntlet_floor_rect_hard` are pure functions of
`(phase, index)`.

`loc_add` aborts the calling script when handed a tile outside the built scene,
so a single bad rectangle offset does not misdraw a tile — it kills the fight,
and only on the roll that picks that pattern in that phase. `::gauntletrun`
walks every rectangle of every pattern of every phase for that reason, and is
run by `ToriRSServer --selftest`.

A whole pattern is armed on one tick, and the engine's loc-revert table is a
fixed array: `TORIRSSERVER_LOC_REVERT_MAX` was raised from 128 to 512 because the
paired-band pattern alone is 96 entries, and an overflow there does not fail
loudly — it warns on stderr and leaves the tile permanently damaging.

## Crystal equipment (outside)

| Surface | Behaviour |
|---|---|
| Singing bowl / Reese / Conwenna | Wiki costs; new items get `inv_setvar(..., start_charges)` |
| Ilfeen | Coin enchant; **hard Western Provinces Diary** required for halberd (`%western_diary_hard_complete`) |
| Charges | Per-item via `inv_setvar`/`inv_getvar` (key = item). Armour drain per worn piece on hit taken; weapon on non-zero hit dealt; tools on woodcut/mine/fish success |
| Save | `inv_var` / `worn_var` / `bank_var` / `container_var.*` sections |

Legacy `%crystal_*_charges` varps remain for old saves but are no longer written
by singing / drain paths.

## Debugprocs

- `::gauntletrun` — encounter assertions: the shared combat invariant that
  preparing a player hit leaves the intended npc active, the arena origin
  against the boss room, the floor cycle adding up, and every floor rectangle
  inside the arena. Wired into `ToriRSServer --selftest`
- `::~gauntlet` / `::~gauntlethm` — force SotE complete + lobby
- `::~gauntletreward` — flag reward chest (normal completion)
- `::~crystalsing` — shards/seeds/tools + tele near Prif bowl

`::~crystal_set` deliberately does **not** live in Gauntlet. It is owned once by
`skill_combat/scripts/player/crystal_set.rs2`; a former duplicate here made the
effective command depend on compile order. The compiler now rejects duplicate
debug commands. See [`CRYSTAL_SET_COMMAND.md`](CRYSTAL_SET_COMMAND.md) for the
client-side `cry` prefix collision, pristine-cache escape, and all permanent
guards.

## Still deferred

- **Death sequence.** The reference plays `hunllef_death_part_a`, then on the
  next tick transforms to `crystal_hunllef_death` / `_hm` (npc 9024 / 9038) with
  `hunllef_death_part_b` before completing. Those records and seqs are in the
  cache and unused; `[ai_queue3]` here still ends the fight without them
- **Post-attack pauses.** The reference returns `attackSpeed + 4` after a
  tornado wave and `+ 1` on the style-switch attack. Neither is reproduced: the
  only lever available is `npc_delay`, and this file already documents why it
  cannot be used here — it invalidates the npc for its whole turn including the
  QUEUE drain, which is where the player's own hits on the boss arrive, so
  player hitsplats bunch and land up to five ticks late
- Elite clue tertiary / incomplete adamant set reward rows
- Clan recolours / overlay wire for crystal gear
- Exact published tile coordinate tables (wiki shows images only; offsets here
  encode the documented set/speed/safe-door behaviour)
