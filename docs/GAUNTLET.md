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

Content lives under `OSRS-Content/.../server/scripts/minigames/minigame_gauntlet/`.

## Placement (engine vs content)

LostCity has no Gauntlet. Behaviour is content (`.rs2` + configs) plus two
engine surfaces:

| Surface | Role |
|---|---|
| `map_instance_*` | 14×14-zone instance (7×7 of 16-tile rooms). Scene window stays 13×13; instance storage raised to 16 (`MOCK230_MAPINSTANCE_ZONES`) |
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
4. **Boss — Hunllef**: style every 4; protect every 6 off-prayer hits; magic
   prayer-disable; tornadoes by HP band; floor pattern sets by HP thirds
   (`%gauntlet_hunllef_hp_phase`), faster warn→hit in later phases; phase-3
   patterns avoid door-adjacent offsets (wiki safe tiles).
5. **Leave / death / rewards**: unchanged (points / completion chest).

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

- Elite clue tertiary / incomplete adamant set reward rows
- Clan recolours / overlay wire for crystal gear
- Exact published tile coordinate tables (wiki shows images only; offsets here
  encode the documented set/speed/safe-door behaviour)
