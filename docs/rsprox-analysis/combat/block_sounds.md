# The player's block sound

A hit on the player that shows a block splat (a 0) comes with a synth to that player, delay 20, sent on the splat's tick. A red damage splat comes with one of the `human_hit_1..4` synths instead. The block's synth depends on what the player wears. Ours sent nothing on a block until 2026-10-19.

## Recorded

In the Inferno recordings, block splats and block synths pair one for one: 1,055 of 1,055 (3299), 1,496 of 1,497 (1754), 1,304 of 1,304 (198) and 3 of 3 (199).

Equipment is filtered out of the shared transcripts. To get it, 199 archive sessions were re-transcribed with `PLAYER_APPEARANCE_EQUIPMENT` on (recipe: [../ancients/README.md](../ancients/README.md)). Each block synth was then paired with the recorder's worn items. That covers 106,000 blocks, and no item was ever heard with two families.

**The shield decides first, when it has a family. Otherwise the torso decides. Otherwise the sound is `human_block_1`.**

| Off hand | Family |
|---|---|
| Thousand-dragon ward, wyvern shield, anti-dragon shield | `dragon_shield_1..5` (2958-2962) |
| Crystal shield | `crystal_armour_1..5` (1345, 1349, 1350, 30, 2968) |
| Elidinis' ward, Toktz-ket-xil | `granite_shield_1..5` (2963-2967) |
| Wooden shield | `wood_armour3_1..5` (2756-2760) |
| Defenders, parrying daggers, devil's element, forager's pouch, arcane spirit shield | none: the torso decides |

| Torso | Family |
|---|---|
| Verac's brassard (all charges), black d'hide body, studded body, Lunar torso, eclipse/frost moon chestplate | `leather_hit_1..5` (2811-2815) |
| Masori body, Bandos chestplate, Armadyl chestplate, Torva platebody (and sanguine) | `blade2_1..5` (2841-2845) |
| Justiciar chestguard, Torag's platebody | `torag_breastplate_1..5` (2366-2370) |
| Robes (mystic, ancestral, Virtus, black, wizard), no torso | `human_block_1` (511) |

The five takes are picked evenly at random: 6,238 / 6,395 / 6,334 / 6,308 / 6,430 for `blade2_1..5`. `human_block` has only one take.

The family follows no material or obj param that can be read. Bandos and Justiciar are both `category=7, param_1564=4`, and they sound different. So the table is Jagex's own server data, and only the items above are known. An item no recording shows falls back to `human_block_1`.

## Ours

- The engine passes `$damage = -1` to `[proc,combat_player_hit_received]` for a block splat, meaning a 0 that was not a fully absorbed hit (`torirs_server_combat.c`).
- `~combat_player_block_sound` (`skill_combat/combat.rs2`) picks the family from the table above and plays a random take at delay 20.

Verified in scriptruns (2026-10-19):
- Unarmed: 42 of 42 blocks gave `human_block_1`.
- Bandos chestplate: `blade2_1..5`.
- Bandos chestplate with a wyvern shield: `dragon_shield_1..5`.

```sh
python3 tools/rsprox/rsprox_block_sounds.py <home>/.rsprox/binary/archive/*.txt
```
