# The Gauntlet / Corrupted Gauntlet / Crystal equipment

> Written 2026-08-04; updated for loot/leave/death/crystal finish and deferred
> boss/NPC/SotE follow-through.
> Behaviour authority: [OSRS wiki — The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet),
> [Reward Chest (The Gauntlet)](https://oldschool.runescape.wiki/w/Reward_Chest_(The_Gauntlet)),
> [Crystalline Hunllef](https://oldschool.runescape.wiki/w/Crystalline_Hunllef),
> [Tornado (The Gauntlet)](https://oldschool.runescape.wiki/w/Tornado_(The_Gauntlet)),
> [Crystal equipment](https://oldschool.runescape.wiki/w/Crystal_equipment) /
> [Crystal singing](https://oldschool.runescape.wiki/w/Crystal_singing) /
> [Ilfeen](https://oldschool.runescape.wiki/w/Ilfeen).
> Map squares and loc/obj names measured from the osrs239 tree (jl2 / configs).
> LostCity: **none** (Song of the Elves / post-254). Kronos: no Java minigame
> (RuneLite plugin only). Re-measure coords rather than trusting this prose.

Content lives under `OSRS-Content/.../server/scripts/minigames/minigame_gauntlet/`.

## Placement (engine vs content)

LostCity has no Gauntlet. All behaviour is content: `.rs2` + configs. Instance
isolation uses the existing `map_instance_*` surface (`map_instances.md`). No
new Server VM opcodes. **`inv_setvar` / `inv_getvar` still absent** (same gap
as ethereum) — crystal charges stay player-scoped `%crystal_*_charges`.

## Map squares (measured)

| Role | Square | Notes |
|---|---|---|
| Prif portal | `m50_95` loc `gauntlet_lobby_entrance` @ local `29,34` (world ~3229,6114; wiki Map 3228,6116) | SotE-gated → lobby |
| Lobby hub | `m47_95` plane 1 | Shared (not instanced). Land `24,43`. Entrance multilocs `gauntlet_entrance` @ `24,48`. Reward chest `gauntlet_chest` @ `19,43` |
| Normal challenge template | `m29_88` | Copied via `~map_instance_from_square`. Facilities on **plane 1** |
| Corrupted template | `m30_88` | Same layout, `_hm` locs/objs |

Start room (local, plane 1): tools ~`55,39`, singing bowl `51,44`, range `59,34`, sink `61,36`, exit platform `60,44`. Boss room centre ~`55,55`.

## Session flow (wiki)

1. **SotE gate**: portal / enter require `%sote ≥ ^sote_complete`.
2. **Portal** → lobby (gear not stored yet).
3. **Enter challenge** (normal always; corrupted after `%total_completed_gauntlet ≥ 1`):
   - Move worn+inv into `gauntlet_holding`; instance template; start kit + prep timer **1000 / 750** ticks.
4. **Rooms**: static template (true procedural 7×7 not engine-supported). Monsters placed across room grid; **sceptre Lights** `prif_gauntlet_door_wall_unlit_*` → lit nodes.
5. **Gather / craft**: wiki yields and Jul 2026 shard costs; points for incomplete loot.
6. **Boss — Crystalline / Corrupted Hunllef**:
   - Starts **ranged**; switches attack style every **4** attacks (stomp excluded; tornado summon counts).
   - **Protection prayer** starts random; every **6** off-prayer player hits switches protect to that style (damage blocked when matching).
   - Magic attacks ~1/5 **disable all prayers** (`~prayer_deactivate_all`).
   - On-prayer max hits by armour tier sum: normal **12/10/8/6**, corrupted **16/13/10/8**.
   - **Tornadoes** (`crystal_hunllef_crystals`): HP bands 1/2/3 (corrupted +1); chase 20 ticks; damage 10–20 scaled by armour tiers.
   - **Floor**: warning → hit locs pulse in boss room; standing on hit deals 10–20; pulse speeds up by phase.
7. **Leave / death loot**: platform = none; barrier/escape-in-boss/death = points table; kill = completion chest.
8. **Reward chest**: shards + 2/3 main rolls + Mod Lenny uniques; incomplete/junk by points.

## Crystal equipment (outside)

| Surface | Behaviour |
|---|---|
| `prif_singing_bowl` | Player sing — wiki levels/shards/XP |
| Reese (`prif_singer1`) / Conwenna (`prif_singer2`) | Talk unlocks `%prif_learnt_crystal_singing` + Sing; **+60** armour/tools, **+20** weapons, **+50** enhanced, key **+5** |
| Ilfeen (`ilfeen_prif`) | Coin-enchant weapon seed → bow/shield/halberd; declining costs via `%ilfeen_enchant_count` (wiki table). Hard WP diary forhalberd deferred |
| Charges | `%crystal_armour/tool/weapon_charges` — armour drain on hit taken; weapon drain on non-zero hit dealt. Tool skilling drain deferred with crystal axes in woodcut |

## Debugprocs

- `::gauntlet` / `::gauntlethm` — force SotE complete + lobby
- `::gauntletreward` — flag reward chest (normal completion)
- `::crystalsing` — shards/seeds/tools + tele near Prif bowl

## Still deferred / opcode gaps

- True procedural 7×7 room generation (needs chunk assembly beyond static templates)
- Exact floor pattern sets / elite clue tertiary / incomplete adamant set rows
- Hard Western Provinces Diary gate for Ilfeenhalberd
- Crystal tool charge drain on woodcut/mine/fish; clan recolours; overlay wire
- **`inv_setvar` / `inv_getvar`** per-item charges (engine gap — report, do not invent)
