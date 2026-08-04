# The Gauntlet / Corrupted Gauntlet / Crystal equipment

> Written 2026-08-04; updated for loot/leave/death/crystal finish.
> Behaviour authority: [OSRS wiki — The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet),
> [Reward Chest (The Gauntlet)](https://oldschool.runescape.wiki/w/Reward_Chest_(The_Gauntlet)),
> [Crystalline Hunllef](https://oldschool.runescape.wiki/w/Crystalline_Hunllef),
> [Crystal equipment](https://oldschool.runescape.wiki/w/Crystal_equipment) /
> [Crystal singing](https://oldschool.runescape.wiki/w/Crystal_singing).
> Map squares and loc/obj names measured from the osrs239 tree (jl2 / configs).
> LostCity: **none** (Song of the Elves / post-254). Kronos: no Java minigame
> (RuneLite plugin only). Re-measure coords rather than trusting this prose.

Content lives under `OSRS-Content/.../server/scripts/minigames/minigame_gauntlet/`.

## Placement (engine vs content)

LostCity has no Gauntlet. All behaviour is content: `.rs2` + configs. Instance
isolation uses the existing `map_instance_*` surface (`map_instances.md`). No
new Server VM opcodes.

## Map squares (measured)

| Role | Square | Notes |
|---|---|---|
| Prif portal | `m50_95` loc `gauntlet_lobby_entrance` @ local `29,34` (world ~3229,6114; wiki Map 3228,6116) | Enter → lobby |
| Lobby hub | `m47_95` plane 1 | Shared (not instanced). Land `24,43`. Entrance multilocs `gauntlet_entrance` @ `24,48`. Reward chest `gauntlet_chest` @ `19,43` |
| Normal challenge template | `m29_88` | Copied via `~map_instance_from_square`. Facilities on **plane 1** |
| Corrupted template | `m30_88` | Same layout, `_hm` locs/objs |

Start room (local, plane 1): tools ~`55,39`, singing bowl `51,44`, range `59,34`, sink `61,36`, exit platform `60,44`. Boss room centre ~`55,55` (blockades at `49,55` / `55,49` / `55,62` / `62,55`).

## Session flow (wiki)

1. **Portal** stores nothing yet — teleports to lobby.
2. **Enter challenge** (normal always; corrupted after `%total_completed_gauntlet ≥ 1`):
   - Move worn+inv into `gauntlet_holding` (cache inv size 42).
   - Instance template square; set `%player_in_gauntlet`.
   - Give starting kit (sceptre wielded, axe/pick/harpoon/pestle/teleport crystal). Corrupted uses `_hm` tools.
   - Prep softtimer: **1000 ticks (10:00)** normal / **750 ticks (7:30)** corrupted. Expiry forces boss room.
3. **Gather** resource nodes (wiki yields): ore/bark/fibre ×3, herb ×1, fish ×4; deplete via `loc_change`. Shard chance on gather.
4. **Craft** at singing bowl (wiki Jul 2026 costs):
   - Weapons: frame → basic (0 shards); +50 shards → attuned; +component → perfected.
   - Armour: ore+linum+bark; shards 50 / 50–100 / 100 by tier (body attuned/perfected need more ore).
   - Vials, teleport crystals, crystal paddlefish, escape crystal.
5. **Egniol**: vial → water (sink or fishing spot) → grym leaf → dust (10 shards pestled) → potion. Drink restores prayer + run (simplified doses).
6. **Boss**: Pass barrier / timer expiry. Hunllef starts **ranged**, switches style every **4** attacks. On-prayer max hit by armour tier sum (wiki): normal **12/10/8/6**, corrupted **16/13/10/8**. Stomp if underfoot. Kill → lobby + completion chest.
7. **Leave / death loot** (wiki Reward Chest):
   - Hunllef kill → completion table (below).
   - Barrier escape or escape crystal **during** boss fight → points-based token loot.
   - Teleport platform leave, or escape crystal **before** boss → **no** loot.
   - Death → restore stored gear, lobby respawn, points-based token loot (HCIM status is separate).
8. **Reward chest**:
   - Completion normal: 5–9 shards + **2** main rolls + tertiary uniques (weapon/armour seed 1/120, enhanced 1/2000, Youngllef 1/2000).
   - Completion corrupted: 7–12 shards + cape (first) + **3** main rolls + tertiary (seeds 1/50, enhanced 1/400, pet 1/800).
   - Incomplete (≥50 points): incomplete table. Junk (1–49): junk table. 0 points: empty.
   - Points: demi/perfected +10, strong/attuned +5, weak/basic/paddlefish craft +2, cook +1.

## Crystal equipment (outside)

`prif_singing_bowl` Sing-crystal — wiki Crafting/Smithing levels, shard costs, and XP:

| Product | Skills | Seeds / materials | Shards | XP (each) |
|---|---|---|---|---|
| Helm / legs / body | 70 / 72 / 74 | 1 / 2 / 3 armour seeds | 50 / 100 / 150 | 2500 / 5000 / 7500 |
| Axe / pick / harpoon | 76 | tool seed + dragon tool | 120 | 6000 |
| Bow / halberd / shield | 78 | weapon seed (`crystal_seed_old`) | 40 | 2000 |
| Blade / Bow of Faerdhinen | 82 | enhanced weapon seed | 100 | 5000 |
| Eternal teleport | 80 | enhanced teleport seed | 100 | 5000 |
| Enhanced crystal key | 80 | crystal key | 10 | 500 |

Charges: player `%crystal_armour_charges` / `%crystal_tool_charges` / `%crystal_weapon_charges` (100 charges per shard; armour/weapons start 2500, tools 10000; max 20000). Dismantle armour/weapons returns seeds. (`inv_setvar` per-item gap same as ethereum.)

Pack names: `prif_armour_seed`, `prif_tool_seed`, `crystal_seed_old` (Crystal weapon seed), `prif_weapon_seed_enhanced`, `prif_teleport_seed`, `prif_crystal_shard`, `prif_crystal_key` (Enhanced crystal key), `crystal_helmet` / `crystal_chestplate` / `crystal_platelegs`.

## Debugprocs

- `::gauntlet` — lobby
- `::gauntlethm` — unlock corrupted + lobby
- `::gauntletreward` — flag reward chest (normal completion)
- `::crystalsing` — shards/seeds/tools + tele near Prif bowl

## Deferred

- True procedural 7×7 room generation / sceptre room lighting (static template used)
- Tornadoes, damaging floor tiles, full prayer-disable wire
- Elite clue tertiary; exact incomplete adamant armour set rows
- Ilfeen coin enchant; Conwenna/Reese NPC singing (+60 / +20 shards)
- Per-item charge vars (`inv_setvar`); combat/skilling charge drain; clan recolours
- Overlay (`gauntlet_overlay` / map / recipes) wire
- Song of the Elves quest gate (Prif access)
- Iwan's flyer (pack uses `leaflet_dropper_flyer` stand-in)
