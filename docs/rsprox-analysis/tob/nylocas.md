# The Nylocas

What Jagex's server sends in the Nylocas room, read from 30 recorded Normal Mode kills (Hard and Entry checked where the rules could differ), and how our room compares. The sources, caveats and units are in the [README](README.md). Heights below are in our units (raw ÷ 4 for the rev-234 v2 projectile packet).

**Recordings:**
- **Normal:** 3299 (5 kills), 3285 (1), 788 (1), 1833 (7), 1764 (4), 553 (3), 1779 (5), 1837 (3), 582 (1).
- **Hard:** 1852.
- **Entry:** 101.

The full output of `tools/rsprox_nylocas.py` over the 30 Normal kills is in [nylocas.out](nylocas.out).

**Ours:**
- `test/raids/tob_nylocas.lua`, trio, seeds 1–6.
- `tob_nylocas_hard.lua`, seeds 1–2.
- `tob_nylocas_probe.lua`, where nobody kills a nylo, so the supports collapse.

All of them are scriptrun tick logs.

**Status key:**
- **fixed:** changed to match the recording, and checked on our tick log.
- **match:** ours already sent it.
- **not copied:** deliberately left, with the reason.

## Summary

Before this pass the npc lifecycle already matched: lifetimes of 52 and 55 ticks, big deaths at +6/+7, splits on the delete tick, and the death graphic on the small's tile when it is deleted. Everything that did not match is now fixed. In order of what changes play:

1. **Ranges.** Wave nylocas throw from at most 2 tiles (ours threw from 8), and Vasilias' melee form hits only adjacent players (ours hit from 8).
2. **Collapse.** A support collapse is a scripted rockfall: each raider takes an aimed rock three ticks after the support dies. Ours hit everyone on the death tick, with no rocks.
3. **Small deaths.** A small plays no animation on its own body, and every small death sends an area shriek.
4. **Support bites.** Bites use the `*_meleeattack` seqs with a random 5–10 cycle delay.
5. **Projectiles.** Each projectile has its own start, angle, offset and heights, and flies 10 cycles per tile.
6. **Vasilias.** She lands as a new npc with seq 8075, cancels her animation on every turn, and lies dead for 5 ticks.
7. **Wrong style.**
   - On a wave nylocas: a delayed shield and a sound, and no chat.
   - On Vasilias: her own line, sent as a filterable message.
8. **Room end.**
   - Every raider gets their own book at the room's centre.
   - Song 147, jingle 250, then the room's own track. This is now done for the first five rooms, because all five end this way.
9. **Chat.** Four lines we sent that Jagex never does are gone, and the collapse lines are Jagex's.
10. **Engine.**
    - A dying npc's right-click options are hidden, and a transform restores them and restates the level.
    - `mes_spam` sends the filterable message kind.

## Waves

| | Recorded | Ours | Status |
|---|---|---|---|
| Supports | 4 `tob_nylocas_support` npcs added on the room's first tick | same | match |
| Room music | `midi_song 579` (Arachnids of Vampyrium), fade out 0/60, fade in 60/0, at +1..+3 | 579 | match (fades not in our tick log) |
| First wave | +4..+7 after the supports | +4 | match |
| Wave cadence | 4, 4, 4, 4, then 16 (in 25 of 30); later gaps 8–64 as the cap holds waves back. The last waves run `16, 8, 12, 8, 8, 8, 4, 8` in nearly every kill | Blert-derived table | match in shape (not re-derived here) |
| Cap stall | no chat line | "The chamber is full - the next wave is held back." | **fixed:** silent |
| Last nylocas gone | no chat line | "The chamber shakes." | **fixed:** silent |
| Vasilias | lands 56–92 ticks after the last wave's add | | not compared |
| Incoming movement | one tile per tick, sent as a non-jump teleport (`[teleport]` in npc_info), not a walk step | walk steps | not copied: the client draws a 1-tile non-jump teleport as a walk |

## Deaths (`--section deaths`)

T is the killing hit (headbar fill 0) for a kill, and the delete for a self-destruct.

| | Recorded | Ours now | Status |
|---|---|---|---|
| Small killed (3,888) | +1 ops hidden; +2 delete, `tob_nylocas_death_<colour>_standard` map_anim on its tile, `sound_area tob_nylocas_melee_death_shriek_2` range 5 on its tile. About 40% run a tick sooner (+0/+1). **No seq on the npc** (Hard 274 and Entry 87 the same) | +1/+2 delete + map_anim + shriek; no seq | **fixed:** `death_anim,null` on the 18 small records; shriek in `~tob_nylo_small_death_gfx` |
| Small self-destruct (994) | lifetime 52; −1 ops hidden; 0 delete + `_detonate` map_anim + the same shriek. **No seq on the npc** | lifetime 52; delete + map_anim + shriek | **fixed:** `~tob_nylo_detonate` animates bigs only |
| Big killed (1,212) | +1 ops hidden; +2 `top_spider_<colour>_death`; +7 delete + 2 splits on its tiles (or +0/+1/+6). No map_anim, no area sound | +1 death seq; +6/+7 delete + splits | match; ops **fixed** (engine) |
| Big self-destruct (78) | lifetime 55; −3 `top_spider_<colour>_death_detonate`; 0 delete + 2 splits. No map_anim, no area sound | same | match |
| Shriek | `tob_nylocas_melee_death_shriek_2` (3980) for **every** colour, both kinds of small death; the map_anim count (4,861) equals the shriek count exactly | 3980 at range 5 | **fixed** |

## Support bites (`--section bites`)

| | Recorded | Ours now | Status |
|---|---|---|---|
| Seq | Ischyros `top_spider_melee_attack`; Hagios `top_spider_magic_meleeattack`; Toxobolos `top_spider_ranged_meleeattack`, small and big alike | same (`~tob_nylo_bite_seq`) | **fixed** (was each colour's throwing `*_attack`) |
| Seq delay | uniform 5–10 cycles (3,862–4,040 each) | uniform 5–10 | **fixed** (was 0) |
| Rate | one bite every 3 ticks per nylocas (20,201 gaps of 3) | 3 | match |
| Support sound | a `sound_area` with id −1, range 10, at the support's centre, on the bite tick (21,506) | none | not copied: id −1 plays nothing |
| Support hitsplat | none; the support's headbar updates (1,180 in one kill) | none; bar only | match: the records were already `hitsplat=no` (the first pass of this doc misread our tick log's damage rows as splats) |

## Projectiles at players (`--section attacks`)

| Thrower | Graphic | Start | Angle | Offset ("progress") | Heights | Flight | Source tile |
|---|---|---|---|---|---|---|---|
| Toxobolos (small) | `tob_nylocas_rangedprojectile_size1` | 39 | 16 | 48 | 8 → 16 | 10 × footprint distance | its tile |
| Toxobolos (big) | `_sizemid` | 39 | 16 | 48 | 8 → 16 | 10 × footprint distance | south-west tile |
| Vasilias ranged | `_size2` | 39 | 16 | 48 | 8 → 16 | 10 × Chebyshev distance from her centre | SW + (2,2) |
| Hagios (small) | `inferno_babysplitter_mage` | 25 | 24 | 24 | 12 → 16 (v2 raw 50 → 64) | 10 × footprint distance | its tile |
| Hagios (big) | `_big` | 25 | 24 | 24 | 12 → 16 | 10 × footprint distance | south-west tile |
| Vasilias magic | `_biggest` | 25 | 24 | 24 | 12 → 16 | 10 × Chebyshev distance from her centre | SW + (2,2) |

Hard and Entry throw with the same flight rules.

**Fixed:**
- Waves throw through `~tob_nylo_throw`, Vasilias through `~player_projectile` from her centre, with the constants in `tob_nylocas.constant`.
- Before, every projectile was start 20, angle 5, offset 0, heights 40 → 31, flight 36 + 8d.
- Our tick log now shows the recorded flight shapes, including a big's 10 or 20 at two tiles from its corner.

**Range.**
- In 1,999 recorded throws by wave nylocas, the footprint distance to the target was never more than 2:
  - small: 1 tile in 275 throws, 2 in 1,063
  - big: 1 in 179, 2 in 482
- Hard and Entry are the same.

**Fixed:** `^tob_nylo_wave_attack_range = 2`. It was `^tob_nylo_attack_range` 8, "the ordinary ranged/magic distance", a guess. Vasilias keeps 8 for her two throwing forms.

## Vasilias (`--section boss`)

| | Recorded | Ours now | Status |
|---|---|---|---|
| Landing | `nylocas_boss_spawning` added with `top_spider_melee_spawn` (8075); deleted at +2; a **new** `nylocas_boss_melee` npc added at +2 (28 of 28; Hard's Prinkipas the same) | the same, through `~tob_vasilias_land_as` (pool, hitpoints, birth clock carried; boss uid republished) | **fixed** (was `_noloop` 9030 and `npc_changetype`) |
| "Drops into the chamber" | no chat line | none | **fixed** (was a broadcast) |
| First attack | +1 (13), +4 (13), +2 (2) after the melee npc's add | +1 or +4 (Blert table) | match |
| Form change | every 10 ticks (251 of 251), 2 attacks per form (279 of 279) | same | match |
| On the turn | `seq -1` (cancels her animation), `enabled_ops 0b11111`, `level_change 800` | all three, every turn (31 of 31 in three runs) | **fixed**: `npc_anim(null, 0)`; ops and level by the engine |
| Melee reach | adjacent: 69 swings at footprint distance 1, 4 at 2 (the player had just stepped) | adjacent | **fixed** (was 8, research note "M38") |
| Ranged/magic reach | throws seen at footprint distance 1–5 | 8 | not contradicted |
| Death | death seq; delete **+5** (28 of 28); room complete +5..+8 | death seq; delete +5; complete +6 | **fixed**: `death_delay=5` on her nine fighting records (was the default 2) |

## Wrong style

| | Recorded | Ours now | Status |
|---|---|---|---|
| Wave nylocas | `map_anim tob_nylocas_shielded` on the nylo's south-west tile, `delay` = the hit's delay × 30 (30, 60, 90…; none for melee), **height 15**; `synth_sound pillory_wrong` with the same delay; a 0 block splat on the nylo; **no message** (176 seen; never on Vasilias) | the same; the delay from the wiki hit-delay formulas (`~tob_player_hit_delay_ticks`) | **fixed** (was undelayed, height 0, with "Your attack has no effect on this Nylocas.") |
| Nulling | after the recorder's own wrong hit (block splat id 12), the recorder never damaged that nylo again in 100 of 102 cases (the 2 are ambiguous timing) | nulled per player | not contradicted |
| Vasilias | spam-type message `"Your attack is absorbed by the nylocas and bounced right off!"` on the swing tick; the next tick the attacker takes the hit and she shows a heal splat (id 6) | reflect + heal + that line through `mes_spam`; no shield | **fixed** (was the shield and the wave message) |
| Verzik's crabs | not recorded | the old shield and message (`~tob_verzik_crab_no_effect`) | unchanged |

## Support collapse (`--section collapse`)

T = the support's death. Ten recorded; ours checked on the probe's collapses.

| Tick | Recorded | Ours now |
|---|---|---|
| T | `loc_add_change tob_nylocas_support_collapsing` on the visible loc. `sound_area swan_rumble` range 12 at the support's centre. Per raider: `cam_shake(0, 5, 15, 10)`, `synth_sound contact_rumble_long`, and `"<col=ff0000>A support was destroyed!</col>"` (the last support: `"<col=ff0000>All four supports were destroyed!</col>"`) | the same |
| T+1 | `loc_anim nylocas_pillar_collapse` (8074); `sound_area poh_stone_shatter` delay 10, range 12 at the loc | the same |
| T+2 | per raider one `motherlode_rockfall` aimed at them: start 0–10, end 30, height 225 → 6, angle 0, offset 0, from a tile beside them. `sound_area chinchompa_explode` delay 30 range 3. Plus 4 loose rocks on random tiles in the room's middle, each with `castlewars_catapult_splash` and `chinchompa_explode` (range 10) delayed to its landing | the same (`[queue,tob_nylo_rockfall]`; the loose rocks on the first raider's queue) |
| T+3 | the aimed rock lands; the raider takes the collapse hit (24 in 3285). `cam_reset` | the same; the 1–50 roll (30–50 Entry) is unchanged, only delivered by the rock |
| T+7 | `loc_add_change tob_nylocas_support_dead` | the same (was T+5) |

Before this pass ours played `tob_pillar_collapse` and sent "A support collapses!" or "The roof gives way.", and every raider was hit on the death tick with no rocks.

## Room end (`--section end`)

| | Recorded | Ours now | Status |
|---|---|---|---|
| Message | `Wave 'The Nylocas' (Normal Mode) complete!<br>Duration: …` 5–8 ticks after her death seq | 6 ticks after | match |
| Book | `obj_add tob_book_nylocas` at the room centre (3295, 4248 template = local 31, 24), one per raider, theirs alone, despawn 200 | the same (`~tob_nylo_cleared_book`) | **fixed**: it was one public drop at her tile from the wiki drop table, and only if she died in her melee form, the only form that hook bound |
| Heal | `"You feel refreshed as your health is replenished."` | none | not copied: seen only on Leagues and Grid Master worlds, most likely a relic |
| Music | `midi_song 147`, `midi_jingle 250` (7,100 ms), then the room's track 0–2 ticks later. **Every room one to five** does this: Maiden 570, Bloat 578, Nylocas 580, Sotetseg 584, Xarpus 567 (3299 and 1833). Verzik's ends with the vault's 582, faded, and no jingle | the same for rooms one to five (`~tob_music_room_cleared`, `[queue,tob_music_after_clear]` a tick later); Verzik keeps the jingle | **fixed** (was the jingle alone) |

## Engine changes

**Npc right-click options and combat level (rev 239).** Jagex's npc_info carries two persistent blocks that our server never sent:

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

**Filterable messages.**
- `mes` always sent MESSAGE_GAME type 0. Jagex sends routine lines as type 105 ("spam"), which the chatbox's Game: Filtered hides: potion doses, eating, Vasilias' wrong-style line.
- The new command `mes_spam(string)` (opcode 11141) sends type 105, through `ToriRSServer_SendMessageTyped`.
- The legacy chat model (`rs_chat.c`) shows type 105 as a game line. The rev-239 chatbox script filters it itself.

**Not engine gaps:**
- Silent support damage: the support records' `hitsplat=no` exists.
- Cancelling her animation: `npc_anim(null, 0)` exists.
- A small with no death animation: a null `death_anim` sends nothing.
- `cam_shake`, `cam_reset`, `projanim_map`, `obj_add_private` and `sound_area` all exist.

## Content changes

| File | What |
|---|---|
| `tob_nylocas.rs2` | wave throw range and `~tob_nylo_throw`; `~tob_nylo_bite_seq` and its delay; small self-destruct without a seq; the shriek; drop-in seq 8075; the collapse (rumble, shake, rocks, damage on the rock, rubble at +7, shatter); `~tob_nylo_cleared_book`; the chamber messages removed |
| `tob_nylocas_boss.rs2` | melee reach 1; projectiles from her centre at the recorded numbers; `~tob_vasilias_land_as` (her and the Prinkipas); `npc_anim(null, 0)` on each turn; the landing broadcast removed |
| `tob_damage.rs2` | `~tob_nylo_no_effect` (delayed shield at height 15, `pillory_wrong`, no chat) and `~tob_player_hit_delay_ticks`; Vasilias' `mes_spam` line; `~tob_verzik_crab_no_effect` keeps the crabs' old behaviour |
| `tob_music.rs2`, `tob_raid.rs2` | `~tob_music_room_cleared`: song 147, jingle, the room's track a tick later (rooms 1–5); the book for the Nylocas |
| `tob.npc` | `death_anim,null` on the 18 small records; `death_delay=5` on Vasilias' nine fighting records |
| `tob_nylocas.constant`, `tob.constant` | the recorded figures, each commented |
| `wiki_nylocas_vasilias.rs2` | the book line removed (it is the room's now) |

## Checks

- **`TORIRSSERVER_REV=osrs239 --selftest`:**
  - The goblin's death is decoded with the client's own reader. The test requires one ops block that hides all five, framed right ahead of the killing hitmark.
  - `::tobrun` passes 56 of 56.
  - The failure list is identical to the pre-change binary's. The default-wire run has no new failures.
  - Three room checks were updated for the new behaviour:
    - A small has no death animation.
    - The landing check follows Vasilias to the melee npc.
    - Her landing tile is read where she landed, since the melee form now walks.
- **Scriptrun runs:** all PASS with no script aborts.
  - `test/raids/tob_nylocas.lua` seeds 1–6.
  - `tob_nylocas_hard.lua` seeds 1–2.
  - `tob_nylocas_probe.lua` seed 1 shows the whole collapse sequence on the right ticks: rumble and synth at T, shatter at T+1, 3 aimed + 4 loose rocks at T+2, every raider hit at T+3, rubble at T+7.
- **Our tick logs vs the recording:**
  - deaths: no seq on a small; shriek on every small death
  - bites: `meleeattack`, delays 5–10
  - wave throws: within 2 tiles, flights 10 per tile
  - Vasilias: lands with 8075 as a new npc, 0 → 9 → 10 tick windows, `seq -1` on every turn, deleted 5 ticks after her death seq
  - room end: song 147, jingle 250, the book, then 580

## Reproducing

```sh
A=~/.rsprox/binary/archive
F="$A/3299.txt $A/3285.txt $A/788.txt $A/1833.txt $A/1764.txt $A/553.txt $A/1779.txt $A/1837.txt $A/582.txt"
python3 tools/rsprox_tells.py --anchor 8342-8358 $F                     # every signature, counted
python3 tools/rsprox_nylocas.py $F > docs/rsprox-analysis/tob/nylocas.out
python3 tools/rsprox_nylocas.py --section deaths $F                     # or waves, bites, attacks, boss, collapse, wrongstyle, end
python3 tools/rsprox_nylocas.py --anchor 10791-10811 $A/1852.txt        # Hard (Entry: --anchor 10774-10790 $A/101.txt)
./src/build_opt/torirsserver --scriptrun test/raids/tob_nylocas.lua --bots 3 --name ny --seed 1 --ticks 8000 \
    --session $S/ny --fixture test/raids/fixtures/fresh_lumbridge.ini     # ours: $S/ny/ticklog.tsv
python3 tools/rsprox_tells.py --ticklog --anchor 8342-8358 $S/ny/ticklog.tsv
```

`--session` needs `$S/ny/saves` to exist. Our tick log reuses npc slots: split each slot's rows into lives at its `npc_spawn` before comparing deaths.
