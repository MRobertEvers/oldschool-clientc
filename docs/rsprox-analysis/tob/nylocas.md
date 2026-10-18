# The Nylocas, Normal Mode

What Jagex's server sends in the Nylocas room, read from 30 recorded kills, and how our room compares. The sources, caveats and units are in the [README](README.md). Heights below are in our units (raw ÷ 4 for the rev-234 v2 projectile packet).

**Recordings:** 3299 (5 kills), 3285 (1), 788 (1), 1833 (7), 1764 (4), 553 (3), 1779 (5), 1837 (3), 582 (1). The full output of `tools/rsprox_nylocas.py` over all 30 is in [nylocas.out](nylocas.out).

**Ours:** `test/raids/tob_nylocas.lua`, trio, seeds 1–3, scriptrun tick logs.

**Status key:** **fixed** = changed in this pass. **match** = ours already sends it. **open** = ours differs and nothing has been changed yet.

## Summary

The npc lifecycle already matches in every count that matters for play: lifetimes of 52 and 55 ticks, big deaths at +6/+7, splits on the delete tick, and the death graphic on the small's tile when it is deleted. What does not match:

1. **Ranges.**
   - Wave nylocas throw from at most 2 tiles; ours throw from 8.
   - Vasilias' melee form hits only adjacent players; ours hits from 8.

   These two change how the room plays.
2. **Small deaths.**
   - A small plays no animation on its own body, killed or self-destructed. Ours plays `top_spider_*_death` / `_death_detonate`.
   - Every small death also sends an area shriek, and ours sends none.
3. **Supports.**
   - Bites use the `*_meleeattack` seqs with a random 5–10 cycle delay; ours use the throwing `*_attack` seqs with none.
   - A support never shows a hitsplat (its bar still moves).
   - A collapse is a scripted rockfall with camera shake, not an instant hit on everyone.
4. **Projectiles.** Each one has its own start, angle, offset and heights, and flies 10 cycles per tile.
5. **Chat.** We send four messages Jagex never does, and word the collapse messages differently.
6. **Npc options.** These were an engine gap, now fixed: a dying npc's right-click options are hidden, and a transform restores them and restates the level (below).

## Waves

| | Recorded | Ours | Status |
|---|---|---|---|
| Supports | 4 `tob_nylocas_support` npcs added on the room's first tick | same | match |
| Room music | `midi_song 579` (Arachnids of Vampyrium), fade out 0/60, fade in 60/0, at +1..+3 | 579 | match (fades not in our tick log) |
| First wave | +4..+7 after the supports | +4 | match |
| Wave cadence | 4, 4, 4, 4, then 16 (in 25 of 30); later gaps 8–64 as the cap holds waves back. The last waves run `16, 8, 12, 8, 8, 8, 4, 8` in nearly every kill | Blert-derived table | match in shape (not re-derived here) |
| Vasilias | lands 56–92 ticks after the last wave's add | | not compared |
| Incoming movement | one tile per tick, sent as a non-jump teleport (`[teleport]` in npc_info), not a walk step | walk steps | not copied: the client draws a 1-tile non-jump teleport as a walk |

## Deaths (`--section deaths`)

T is the killing hit (headbar fill 0) for a kill, and the delete for a self-destruct.

| | Recorded | Ours | Status |
|---|---|---|---|
| Small killed (3,888) | +1 ops hidden; +2 delete, `tob_nylocas_death_<colour>_standard` map_anim on its tile, `sound_area tob_nylocas_melee_death_shriek_2` range 5 on its tile. About 40% run a tick sooner (+0/+1). **No seq on the npc** | +1 `top_spider_<colour>_death` on the npc; +1/+2 delete + map_anim | **open:** drop the npc seq, add the shriek |
| Small self-destruct (994) | lifetime 52 ticks; −1 ops hidden; 0 delete + `_detonate` map_anim + the same shriek. **No seq on the npc** | lifetime 52; −1 `top_spider_<colour>_death_detonate` on the npc; 0 delete + map_anim | **open:** drop the seq, add the shriek |
| Big killed (1,212) | +1 ops hidden; +2 `top_spider_<colour>_death`; +7 delete + 2 splits on its tiles (or +0/+1/+6). No map_anim, no area sound | +1 death seq; +6/+7 delete + splits | match (ops: **fixed**, engine) |
| Big self-destruct (78) | lifetime 55; −3 `top_spider_<colour>_death_detonate`; 0 delete + 2 splits. No map_anim, no area sound | same | match |
| Shriek colour | `tob_nylocas_melee_death_shriek_2` (3980) for **every** colour, both kinds of death | none (the comment in `~tob_nylo_detonate` assumes the detonate seq's frame sound) | **open** |

The shriek is the same sound for all three colours; the map_anim count (4,861) equals the shriek count exactly.

## Support bites (`--section bites`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Seq | Ischyros `top_spider_melee_attack`; Hagios `top_spider_magic_meleeattack`; Toxobolos `top_spider_ranged_meleeattack` (the `meleeattack` seqs, not the throwing ones), small and big alike | `top_spider_<colour>_attack` for all three | **open** |
| Seq delay | uniform 5–10 cycles (3,862–4,040 each) | 0 | **open** |
| Rate | one bite every 3 ticks per nylocas (20,201 gaps of 3) | 3 | match |
| Support sound | a `sound_area` with id −1, range 10, at the support's centre, on the bite tick (21,506) | none | not copied: id −1 plays nothing |
| Support hitsplat | **none at all**; the support's headbar updates (1,180 in one kill) | a hitsplat per bite (584 in one run) | **open:** `hitsplat=no` on the three support records keeps the bar and drops the splat |

## Projectiles at players (`--section attacks`)

| Thrower | Graphic | Start | Angle | Offset ("progress") | Heights | Flight | Source tile |
|---|---|---|---|---|---|---|---|
| Toxobolos (small) | `tob_nylocas_rangedprojectile_size1` | 39 | 16 | 48 | 8 → 16 | 10 × footprint distance | its tile |
| Toxobolos (big) | `_sizemid` | 39 | 16 | 48 | 8 → 16 | 10 × footprint distance | south-west tile |
| Vasilias ranged | `_size2` | 39 | 16 | 48 | 8 → 16 | 10 × Chebyshev distance from her centre | SW + (2,2) |
| Hagios (small) | `inferno_babysplitter_mage` | 25 | 24 | 24 | 12 → 16 (v2 raw 50 → 64) | 10 × footprint distance | its tile |
| Hagios (big) | `_big` | 25 | 24 | 24 | 12 → 16 | 10 × footprint distance | south-west tile |
| Vasilias magic | `_biggest` | 25 | 24 | 24 | 12 → 16 | 10 × Chebyshev distance from her centre | SW + (2,2) |

**Ours:**
- Every projectile uses `~player_projectile(src, at, target, gfx, 40, 31, 20, 5, 36, 0, 8)`: start 20, angle 5, offset 0, heights 40 → 31, flight 36 + 8d.
- The graphics are right in all six rows.
- **Open:** the numbers in every row.

**Range.** In 1,999 recorded throws by wave nylocas, the footprint distance to the target was never more than 2:
- small: 1 tile in 275 throws, 2 in 1,063
- big: 1 in 179, 2 in 482

Our `^tob_nylo_attack_range = 8` is commented as "the ordinary ranged/magic distance", and it is a guess. **Open, and it affects gameplay:** wave throwers at range 2.

## Vasilias (`--section boss`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Landing | `nylocas_boss_spawning` added with `top_spider_melee_spawn` (8075); deleted at +2; a **new** `nylocas_boss_melee` npc added at +2 (28 of 28) | spawning form with `top_spider_melee_spawn_noloop` (9030), then `npc_changetype` to melee at +2 | **open:** seq 8075; the melee form as a new npc |
| First attack | +1 (13), +4 (13), +2 (2) after the melee npc's add | +1 or +4 (Blert table) | match |
| Form change | every 10 ticks (251 of 251), 2 attacks per form (279 of 279) | same | match |
| On the turn | `seq -1` (cancels her animation), `enabled_ops 0b11111`, `level_change 800` | no seq cancel | **open:** `npc_anim(null, 0)` on the turn; ops and level: **fixed** (engine) |
| Melee reach | adjacent: 69 swings at footprint distance 1, 4 at 2 (the player had just stepped) | `^tob_nylo_attack_range` 8 in every form ("M38") | **open, and it affects gameplay:** melee form must be adjacent |
| Ranged/magic reach | throws seen at footprint distance 1–5 | 8 | not contradicted |
| Movement | stayed on one tile in most kills; moved up to 8 tiles in a few | | consistent |
| Death | death seq; delete **+5** (28 of 28) | death seq; free +2 | **open** |

## Wrong style

| | Recorded | Ours | Status |
|---|---|---|---|
| Wave nylocas | `map_anim tob_nylocas_shielded` on the nylo's **south-west tile**, `delay` = the hit's delay (30 for a 1-tick dart, 60, 90…), **height 15**; `synth_sound pillory_wrong` with the same delay; a 0 block splat on the nylo; **no message** (176 seen; never on Vasilias) | `spotanim_map(tob_nylocas_shielded, npc_coord, 0, 0)` with no delay and height 0, plus `"Your attack has no effect on this Nylocas."` | **open** |
| Nulling | after the recorder's own wrong hit (block splat id 12), the recorder never damaged that nylo again in 100 of 102 cases (the 2 are ambiguous timing) | nulled per player | not contradicted |
| Vasilias | spam message `"Your attack is absorbed by the nylocas and bounced right off!"` on the swing tick; the next tick the attacker takes the hit and she shows a heal splat (id 6) | reflect + heal; but also `~tob_nylo_no_effect` (shield graphic and the wave message) | **open:** her own message, no shield |

## Support collapse (`--section collapse`)

Recorded, T = the support's death:

| Tick | Recorded | Ours |
|---|---|---|
| T | `loc_add_change tob_nylocas_support_collapsing` on the visible loc. `sound_area swan_rumble` range 12 at loc + (1,1). `cam_shake(0, 5, 15, 10)`. `synth_sound contact_rumble_long`. `"<col=ff0000>A support was destroyed!</col>"` (the last one: `"<col=ff0000>All four supports were destroyed!</col>"`) | loc → collapsing (dead under it). `~tob_npc_sound(tob_pillar_collapse)`. `"<col=ff3040>A support collapses!"` / `"The roof gives way."`. **1–50 to everyone in the room, now** |
| T+1 | `loc_anim nylocas_pillar_collapse` (8074); `sound_area poh_stone_shatter` delay 10, range 12 at the loc | `loc_anim 8074` (match) |
| T+2 | the rockfall: per raider one `motherlode_rockfall` aimed at them (start 0–10, end 30, height 225 → 6, angle 0, offset 0, from one tile beside them) with `sound_area chinchompa_explode` delay 30 range 3; plus 4 loose rocks on random tiles near the room centre, each with `castlewars_catapult_splash` and `chinchompa_explode` (range 10) delayed to its landing | none |
| T+3 | the aimed rock lands: the raider takes the hit (24 in 3285). `cam_reset` | |
| T+7 | `loc_add_change tob_nylocas_support_dead` | the revert to dead at T+5 |

**Open:** the whole sequence above, the messages, and the damage (an aimed rock landing at T+3, not 1–50 on the death tick).

## Room end (`--section end`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Message | `Wave 'The Nylocas' (Normal Mode) complete!<br>Duration: …` 5–8 ticks after her death seq | | not compared (our tick log has no messages) |
| Book | `obj_add tob_book_nylocas` at the room centre (3295, 4248 template), private to each raider, despawn 200 | dropped at her tile by `wiki_nylocas_vasilias.rs2` | **open** |
| Heal | `"You feel refreshed as your health is replenished."` | | not compared |
| Music | `midi_song 147`, `midi_jingle 250` (7,100 ms), then `midi_song 580` (Dance of the Nylocas) 0–2 ticks later | `midi_jingle 250` 3 ticks after her death seq; no song change | **open:** the songs |

## Chat we send that Jagex does not

None of these four appears in 30 kills: "Nylocas Vasilias drops into the chamber.", "The chamber shakes.", "The chamber is full - the next wave is held back.", and "Your attack has no effect on this Nylocas.". **Open.**

## Engine changes

**Npc right-click options and combat level (rev 239), fixed.** Jagex's npc_info carries two persistent blocks that our server never sent:

- **Visible ops** (`enabled_ops`, RSProt `NpcVisibleOpsEncoder`, p1Alt2, flag `0x1000`).
  - Every dying npc hides all five, on the tick its death starts. That held for 1,042 of 1,072 deaths across the Inferno, the Colosseum, the Gauntlet and ToB: 758 with the death animation, 284 a tick before it.
  - For a Nylocas small, which has no death animation, this is the only sign it is dying.
- **Combat level** (`level_change`, `NpcCombatLevelChangeEncoder`, p4Alt1, flag `0x8000`).
  - Every recorded transformation re-sends ops `0b11111`: all 826 in 3285 and 788.
  - It also sends the new type's own level in all but 17. Those 17 are newer bosses with no level.

What changed:

- **`torirs_server.h`:** `ops_hidden`, `level_override`, `level_override_set` on the npc; `TORIRSSERVER_NMASK_OPS` / `_LEVEL` (rev-239 only, like `_HEADBAR`).
- **`torirs_server_combat.c`:** `npc_death_begin` hides all five. A revive and a respawn restore them.
- **`torirs_server_scripts.c`:**
  - `ToriRSServer_NpcChangeType` restores the ops and states the new type's level.
  - The new command `npc_setops(int $shown)` (opcode 11140) lets content set the mask itself.
- **`torirs_server_encode.c`:**
  - Both blocks are written in RSProt's order (LEVEL_CHANGE, OPS, then HITMARKS).
  - An observer the npc enters view of is sent the current state when it differs from a fresh npc. A corpse walked into view offers no Attack.
- **The client:** no change. It already decoded both blocks and the menu already honours `visible_ops`.

**Not engine gaps:**
- Silent support damage: the support records' `hitsplat=no` exists.
- Cancelling her animation: `npc_anim(null, 0)` exists.
- A small with no death animation: a null `death_anim` sends nothing.
- `cam_shake`, `cam_reset`, `projanim_map` and `sound_area` all exist.

The remaining open rows are content.

## Checks

- **`TORIRSSERVER_REV=osrs239 --selftest`:**
  - The goblin fight now checks that the death hides all five options.
  - It decodes the captured NPC_INFO with the client's own rev-239 reader. The test requires exactly one ops block with mask 0, and the killing hitmark read after it in the same block, which proves the block is framed right.
  - The failure list is otherwise identical to the pre-change binary's. The default-wire selftest has no new failures.
- **`test/raids/tob_nylocas.lua`:** seeds 1–3 PASS on the new server. The scriptrun client decodes every death's and transformation's blocks with no undecoded tail.

## Reproducing

```sh
A=~/.rsprox/binary/archive
F="$A/3299.txt $A/3285.txt $A/788.txt $A/1833.txt $A/1764.txt $A/553.txt $A/1779.txt $A/1837.txt $A/582.txt"
python3 tools/rsprox_tells.py --anchor 8342-8358 $F                     # every signature, counted
python3 tools/rsprox_nylocas.py $F > docs/rsprox-analysis/tob/nylocas.out
python3 tools/rsprox_nylocas.py --section deaths $F                     # or waves, bites, attacks, boss, collapse, wrongstyle, end
./src/build_opt/torirsserver --scriptrun test/raids/tob_nylocas.lua --bots 3 --name ny --seed 1 --ticks 8000 \
    --session $S/ny --fixture test/raids/fixtures/fresh_lumbridge.ini     # ours: $S/ny/ticklog.tsv
python3 tools/rsprox_tells.py --ticklog --anchor 8342-8358 $S/ny/ticklog.tsv
```

`--session` needs `$S/ny/saves` to exist. Hard Mode (1852) and Entry Mode (101) recordings exist but are not read here; their npc ids are 10791–10811 and 10774–10790.
