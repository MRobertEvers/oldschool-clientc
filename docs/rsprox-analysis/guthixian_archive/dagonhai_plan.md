# The Dagon'hai: spec from RSProx

One kill and three wipes, all spectated (rev 241, 2026-10-21, `--all-players`):
index 4315 [5304..5751] (the kill, B0aty, Woox, Saxerpillar), and the wipes 4315
[1717..1877], [3500..3937] (three, then two raiders) and 4314 [2143..2490] (a
duo). The wiki has no mechanics for them. The Dagon'hai are the west fourth boss
(beside Xarth), the room after Moia.

Content: `scripts/tfa_dagonhai.rs2`, `configs/tfa_dagonhai.npc`,
`configs/tfa_west.constant`; the channel hook in
`skill_combat/scripts/player/player_magic.rs2` (`~tfa_dh_spell_lands`). Test:
`test/guthixian_archive/tfa_dagonhai.lua` (`::tfaroom 9 3`, `::tfadhinterrupt`,
`::tfadhbreak`, `::tfadhritual`).

Monks: 0 the Elder (`tfa_dagonhai_leader`), 1 and 2 (`tfa_dagonhai_minion_1/2`,
"Dagon'hai Monk"); the cauldron `tfa_dagonhai_focus` ("Ritual Stability").

## 1. The room

| fact | evidence |
|---|---|
| Template square (63,87); raiders arrive at (4079..4081, 5598..5600) | 4/4 |
| The transporter is `tfa_boss_teleporter` at (4079,5599) rot 3 (the map's (47,31)): ten across, (4080,5599) -> (4069,5599) | 8/8 crossings |
| The three and the cauldron stand from the room's build: Elder (4057,5599), monk 1 (4058,5600), monk 2 (4058,5598) facing west, the cauldron (4064,5598) facing south; level 427 (452 at three) | 4/4 |
| Before anyone crosses: the Elder "Ah, the Book of Life and Death." at +14, monk 1 "What does it do?" 6 on, monk 2 "Gives us power, of course!" 7 after | 4314 [2157..2170]; 4315 [1736..1749] (+19) |
| Song 889 | 4/4 |

## 2. The shield, the stability, the rifts

| rule | evidence |
|---|---|
| The three share one shield: 640 at two raiders (cache stat4), 930 at three; each monk's headbar (id 5) shows the pool; hits on it are `hitsplat_shield` (19 seen by others) | between breaks 901, 915, 899, 956 dealt (the kill) |
| Spent: the cauldron loses 200 of its 1000 (its splat 20, bar -200), all three `heal_casting` h120 and healed to full, one says "The power of Zamorak protects us!" | 5/5 breaks |
| Two rifts (`tfa_dagonhai_rift`, healthbar 2) at a site facing the cauldron; imps (`tfa_dagonhai_demon`) two at a time at +8, +11, +14; the rifts fade at +17 (transparency 0->255 / 30) and go at +19 | 4315 [5372..5391], [5438..5457] |
| An imp: "For the masters!" or "Chaos reign!", `sound_area` 314 (`bloodveld_hit`) r10 size 1, chases a raider (faced), `leagues_6_thrall_imp_melee_attack_silent` every 4 beside them (0..39), after two swings `[facing] type=reset`, ops 0b00000, `leagues_6_thrall_imp_melee_despawn_vfx_spotanim` + `_despawn_silent`, deleted 5 on; killed, its killer gets `vfx_dagonhai_demon_buff_player_spotanim_01` slot 1 | 27 imps |
| The fifth break is the kill (§8) | 4315 [5733] |

## 3. The cycle

50 ticks from the first crossing +20 (26 cycles over the four fights):

| t0+ | recorded |
|---|---|
| 0 | the Elder channels (§4) |
| 13 | the book's monk (1 on even cycles, 2 on odd) `search_books_end_0n`, "What have we here?" |
| 16 | the throw (§5) |
| 20 | the book flies; +21 its attack (§6) |
| 23 | the cauldron starts a corner of its loop (4063,5599) -> (4066,5599) -> (4066,5597) -> (4063,5597), a tile each 2 ticks |
| 25 | the other monk channels |

Between, the book's monks stand at their shelves (monk 1 north and east, monk 2
south and west) playing `npc_dagonhai_search_books_idle_0n` every tick (468
rows), faced away from the room (angles 1024 / 1792).

## 4. Channels

| rule | evidence |
|---|---|
| `npc_dagonhai_charge_cauldron_idle_0n` + `vfx_dagonhai_charge_cauldron_idle_0n_<element>` slot 2 loop, an element at random with its line (air "I'll focus the winds!", water "I'll guide the flow!", earth "I'll strengthen the bond!", fire "I'll feed the flames!"); `[facing] coord=` the cauldron every tick | 40 channels |
| A spell of that element landing on the channeller interrupts it: `charge_cauldron_interrupted_0n`, "Argh!", `stunned_2cycle` slot 2 (h150 Elder, 124 monks) every 2 ticks for 11 | 40/40 interrupts land 2..4 after the element's impact graphic |
| Left alone 18 ticks it completes: `charge_cauldron_end_0n` + `vfx_..._end_0n_<element>`, "Ever closer to ultimate power!", the ritual +25 | 4315 [1791], [1816], [3625], 4314 [2447] |

## 5. The books

| rule | evidence |
|---|---|
| The god turns Zamorak, Bandos, Saradomin | 22/22 throws |
| t0+16: `throw_book_<god>_0n` + `vfx_dagonhai_throw_book_<god>_0n` slot 2, faced to the cauldron, its line ("Zamorak fuels us!", "Bandosian strength, feed the flame!", "Saradominist zeal, feed the flame!"), `body_customisation` (empty); the lane painted: every tile of the line from the monk to the cauldron's middle, both corners of each diagonal step, `vfx_dagonhai_throw_book_floor_saradomin_<way>_01` for every god, the way pure when one axis is over 3x the other | 22/22 lanes (6 ways) |
| t0+20: a raider standing on the lane catches it: `vfx_dagonhai_throw_book_projectile_<god>_01` (st 0, et 30, angle 16, progress 64, heights 160/140 raw) to them, `vfx_dagonhai_throw_book_impact_<god>_01` slot 2 delay 30, the monk's "Curse you!"; else into the cauldron (heights 160/50): `npc_dagonhai_cauldron_attack_0k_<god>` + its graphic delay 30, the ritual +25 (hitsplat 11, 25) | 20/22 (two catches at the lane's edge at +4 went to the cauldron) |

## 6. The god attacks (from the catcher's tile, or the cauldron's middle)

| god | recorded | evidence |
|---|---|---|
| Zamorak | the catcher 23..27 with `cauldron_attack_zamorak_aoe_attack_01` slot 2, then `hitsplat_74` 3 every 4 ticks five times; fire bolts (`cauldron_attack_zamorak_projanim_01`, st 0, angle 45, heights 140/0, et 90, 150 far) to each raider's tile and one beside it, pools `aoe_idle_00/02` at once, `aoe_attack_01` + `sound_area` 9206 r3 as each lands | 4315 [5355..5376], [5505..5526] |
| Bandos | the catcher 51 (slot 4 `bandos_aoe_attack_01`); a square ring from the origin, Chebyshev k at delay 30k to 8, its four graphics in turn; a raider struck as it reaches them, 23..29 | [5405..5410], [5555..5563], [3904] |
| Saradomin | 27 strike tiles (each raider's own, the rest at random) `saradomin_damage_telegraph_150` + `_damage_lightning` delay 121..126; 6 healing tiles `heal_telegraph_180` + `heal_lightning` 151..156; the catcher `heal_lightning` slot 2 and healed 13 | [5455..5461], [5605..5612] |

## 7. The Elder's attacks

Every 5 ticks while neither channelling nor stunned: `npc_dagonhai_attack_ranged_01`
+ `vfx_dagonhai_attack_ranged_01` slot 2 (75) or `attack_magic_01` +
`vfx_dagonhai_attack_magic_01` (54), at a raider and any beside them (1 target 88,
2: 27, 3: 6); `vfx_projanim_dagonhai_attack_ranged_01` or `watersurge_travel`
(st 60, et 150, angle 0, progress 80, heights 188/124 raw); lands +5, the ranged
with `vfx_hitanim_dagonhai_attack_ranged_01` slot 2. Damage 0..30 each, 74/100 and
59/70 zero (prayed).

## 8. The kill and the enrage

| | recorded | evidence |
|---|---|---|
| kill (fifth break) K | all three `heal_casting` + healed, to their unshielded forms (`_unshielded`: 7 each, 10 at three, healthbar 16), ops 0b11111, "AAARRGGH!" (monks), the Elder "You incompetent fools!"; the cauldron `cauldron_attack_01_zamorak` + its graphic + `cauldron_dust_explosion` slot 1 delay 25 | 4315 [5733] |
| K+1.. | the cauldron fades over 20, gone K+3; the Elder "You cannot stop the will of Zamorak!" (+1), "We cannot fail!" (+8), `attack_magic_01` every 5; each monk falls to a hit: `death_0n` (monk 2 with `vfx_dagonhai_death_03`), ops 0b00000, deleted 6 on; the line 6 after the last; the navigator (4073,5599) +3 | [5733..5754] |
| enrage (ritual 100) E | the three unshielded, ops on; `cauldron_attack_04_guthix_01`; E+1 "Life and death, at our command!", "Guthix's power, now Zamorak's!", "The tome's power is ours!", `_guthix_02` + its graphic, `vfx_dagonhai_cauldron_unlock_book` west from the cauldron 4 cycles a tile; E+2 the Book of Life and Death opens (`loc_anim` on (4049,5597)), a Bandos ring from the cauldron; E+4 "No survivors!"; from E+6 every 5 the Elder's `attack_magic_guthix_04` (+ `vfx_dagonhai_attack_magic_04`, `_guthix_energy` slot 1, `sound_area` 12327 r10 size 5), the monks' `attack_magic_02/03` a tick later; every raider two bolts of 15..22 | 3/3 wipes; dead 18..23 ticks after E |

## Audit: our scriptrun against 4315 (2026-10-21)

Run: `test/guthixian_archive/tfa_dagonhai.lua` (solo: `::tfadhinterrupt` every
8 ticks for the element spells a test bot cannot pick, `::tfadhbreak 1` then 4
for the kill). `tools/rsprox_tells.py --anchor 16643..16651 idx4315.txt --ours`.

| tell | recording | ours | verdict |
|---|---|---|---|
| engage, opening lines, walks | §1, §3 | 107..116 | matched |
| cycle (channel, search end, throw, flight, attack, corner, monk channel) | 0/13/16/20/21/23/25 | 126/139/142/146/147/(149)/151 | matched |
| channel interrupt / completion | §4 | both | matched |
| lanes and catches | §5 | painted lanes; a catch with "Curse you!", a cauldron cast | matched |
| Zamorak / Bandos / Saradomin | §6 | all three | matched (geometry of the random tiles **approximate**) |
| shield, breaks, rifts, imps | §2 | 266..292 | matched |
| kill sequence | §8 | 296..333 | matched |
| search idle | 190 / window | 91 | matched (fewer cycles) |
| channel loop flag | `loop=true` | none | **open (engine)** |
| monks' `[bas]` (empty, every tick) | 1850 rows | none | **open**: RSProx prints it empty; content unknown |
| the cauldron's movement | `crawl` | walk steps every 2 ticks | **open (engine)**: no crawl move speed for npcs |
| defend / attack sounds | none to a spectator | null | **open** |

## Damage

| attack | recorded | ours |
|---|---|---|
| Elder ranged / magic | 0..30 (26/100, 11/70 nonzero) | 0..30, its prayer blocks |
| Zamorak catch / burn | 23..27 / 3 x5 | 20..27 / 3 x5 |
| Zamorak pool | no raider caught on one | 0..25 (**unverified**) |
| Bandos catch / ring | 51 / 23..29 | same |
| Saradomin strike / heal | 0..13 / 13 | same |
| imp | 9..39 | 0..39 |
| enrage bolts | 15..22 x2 | same |

## Open

- Rift sites: two recorded (north-east, east); ours picks between those two.
- Six or more raiders are unrecorded.
- The shield and the cauldron's 200 do not scale in the recordings beyond the shield (930 at three); the cauldron was 1000 at three and two.
