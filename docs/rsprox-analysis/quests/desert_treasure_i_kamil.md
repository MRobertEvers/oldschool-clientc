# Desert Treasure I: Kamil (ice diamond), from RSProx index 4628 [1507..1700]

Index 4628 (rev 242, w442, a normal world, 2026-11-20) is the only session holding npc 3458
(`rsprox_index.py search npc 3458`: 1 session; the NMZ forms 1129 / 6345: 0). One recording is one
roll: every rate below is a count from it. Transcripts used: default, `--all-players --bas`, `--raw`.
Ours: `test/quests/_dt_kamil.lua`, three private scriptruns (`$DT/kamil/run1..3`; run 3 with the
wire log). Files: `scripts/dt_kamil.rs2`, `configs/deserttreasure.npc` `[icediamond_icewarrior]`,
`configs/deserttreasure.constant` (`^dt_kamil_*`).

## Per-life record (1 life)

| | recorded | ours (run 2/3) | verdict |
|---|---|---|---|
| spawn | created [1512] (`creationcycle` 369782 = that tick's map_clock), the tick of "You can feel an evil presence nearby..." and the hint arrow on his index (out of view) | `~dt_kamil_spawn` from dt_ice's evil-presence queue: `npc_add` 2855,3757 on the message tick (t33), arrow 0->7, reset, npc | fixed (was a world spawn at 2863,3757) |
| add | [1521] 2855,3757, spawnangle east, `[facing] player` | spawnangle 1536 (east), entity face from the take | matched |
| take | the player 15 tiles off ([1520] 2870,3757); first step [1522] | `[queue,dt_kamil_wait]` takes at <= 15 (t36), first step t37 | matched |
| approach | 3 steps east [1522..1524], stops at 2858 (10 tiles), never closer while the player stood 7 off ([1524..1546]) | 3 steps, stops on 2858,3757 (`attackrange` 10) | matched |
| first cast | [1526], take + 5 | t41, take + 5 (`npc_attackdelay(5)`) | matched |
| telejump | [1546] Protect from Magic out of reach -> [1547] 2858,3757 -> 2866,3759 (+1,+2 of the player), cast the same tick; [1548] step SW beside the player | t53 jump to (+1,+2), t54 the step (`[ai_timer]` + `npc_approach`) | matched (1 sample; the tile rule is not fitted) |
| movement kinds | add 1, walk 4, idle 77, telejump 1, del 1 | walk, telejump, del | matched |
| facing | add, telejump, the step ([1548]), `type=reset` at death: 4 | 6: one extra at take+1 and jump+2 | engine latch, Open |
| death | [1677] last splat; [1678] `human_transdeath` 843, facing reset, enabled_ops 0b00000, synth `ice_warrior_death` 529; [1680] del | t294 splat, t295 843 + 529 (synth) + reset + ops 0, t297 del | matched |
| headbar | id 0 (30 wide) | healthbar_standard_30 | matched |

## Block kinds (`npc_block_inventory.py --values`)

| kind | recorded | ours | values |
|---|---|---|---|
| enabled_ops | 1 | 1 | 0b00000 both |
| facing | 4 | 6 | see above |
| headbar / hit | 24 / 24 | 47 / 47 | longer fight, the bot's punches |
| say | 16 | 23 | "Sallamakar Ro!" only, one per cast |
| sequence | 40 | 81 | 391 slash, 389 defend, 843 death |
| spotanim | 12 | 20 | the recorder's fire bolts (128; 85 a splash): the player's kit |

No kind recorded and never sent (0).

## The attacks

Attack clock: 4 ticks. In melee reach every attack is on it (33/33, [1547]..[1675]); out of reach
the gaps were 4, 4, 6, 7 ([1526] [1530] [1534] [1540] [1547]): the two long ones are Open.

| | recorded | ours | verdict |
|---|---|---|---|
| choice out of reach | cast 5/5 | cast 4/4 | matched |
| choice in reach (cardinal) | cast 11/32 (34%), slash 21/32 | 19/60 (32%), 1 in 3 | matched |
| back-to-back casts | yes ([1563] [1567]; [1619] [1623] [1627]) | yes | fixed (the old 5-tick cooldown had none) |

### The cast (16/16 recorded)

| tick | recorded | ours |
|---|---|---|
| T npc | `[say] "Sallamakar Ro!"`, NO seq (0/16) | `npc_say`, no anim |
| T tile (player's) | `sound_area ice_magic_all` 172 range 10; 4x `map_projanim_v2 legends_lightbeam` 353 tile->same tile, 0..20, angle/progress/heights 0; `map_anim icebeam_impact` 359 delay 1 | same (wire: 4 projectiles 353, 0, 20) |
| T player | `ice_barrage_impact` 369 delay 10; "You've been frozen!" (gamemessage); busy 1 | same; busy bit set on T |
| T+2 | busy 0 (16/16); a click on T+2 is dropped (shark x3 at [1569]) | `p_delay(1)` from a queue run on T's player turn; busy 0 at T+2 |
| damage | flat 5 (15/15 unprayed), 0 with Protect from Magic up at the cast (1/1, [1547] -> [1549]; the 5 from [1540] landed the same tick under the prayer) | `combat_damage_player` 5 / 0 decided at the cast |
| landing | cast -> splat, paired in order: 10, 9, 8, (9\|2), 2, 5, 2, 5, 6, 2, 3, 9, 4, 7, 6 (6/16 on the freeze's T+2: due while frozen) | `max(2, random(11))` ticks, a normal player queue (waits out a freeze) |
| player block seq | none on the 5s | none |

The wiki ("a magic attack similar to Dessous' combination attack which always hits 5... possible
for these to stack to 10") agrees; Dessous's page says that attack's delay "seems to be random".

### The slash (21/21 recorded)

`human_sword_transslash` 391, synth `hacksword_slash` 2500 on the swing tick, the splat on the swing
tick (21/21), the player's block seq on it. All 21 under Protect from Melee: 0 (21/21); ours 40/40
prayed 0, one unprayed (the test's prayer switch) 19. Max: the wiki's 23 = floor(0.5 + 88 x 164 / 640)
from strength 80 / strengthbonus 100 (cache stat3 80, param 100): UNVERIFIED, no unprayed swing.
Levels: cache stat1..4 190/135/80/130, ours the same (`deserttreasure.npc`).

### What he takes

Defend `human_sword_transdef` 389 delayed to the landing (51/56/81, the recorder's arrows and bolts),
synth `blade3_mail` 22 on every hit (24/24): ours the same, synth on our wire. Fire Bolt 11 hits, 1
splash in 12 (magic defence from his Magic 1). Immune to every non-fire spell: wiki only, already in
`player_magic.rs2` `~npc_max_dealt` (UNVERIFIED: no other spell recorded). Weak to fire: the recorded
`last_npc_elemental_weakness` (5536) = 554 on the first bolt [1599]; the 10% is the wiki's, UNVERIFIED.

## Hit causes (`rsprox_hit_causes.py --by-prayer`), the recorder's 53 splats

| cause | recorded | ours | verdict |
|---|---|---|---|
| the ice (5 / prayed 0) | 15 + 1 | 22 + 1 | matched |
| the slash (prayed 0) | 21 | 40 (+1 unprayed 19) | matched |
| the cold (1, windy + 1) | 16 | 0 | not Kamil's: dt_ice.rs2's timer, armed at the gate squeeze the test skips |
| heals on him | 0 | 0 splats (natural regen +1, no splat) | matched |

## The kill ([1680], death + 3)

| | recorded | ours |
|---|---|---|
| drop | `4dose2restore` x1, `chocolate_cake` x1, x1, reveal 100 despawn 100 ownership self, no bones | `obj_add_private` 100/100, same order |
| loot tracker | 7192 (3024, 1), (1897, 2) | 7192 x3 (cakes not merged), event id differs: engine, Open |
| varbits | `current_hint_arrow` 7->0, `fd_icewarrior_subquest` 2->3, `fd_icewarrior_dead` 0->1 | same tick |
| hint | `[hint_arrow] type="reset"` | `hint_stop` |
| page | chat_right chatneu3 "Well, that must have been the 'bad man' that the troll kid was on about... His parents must be up ahead somewhere." | `~chatplayer_anim(^chat_neutral, ...)`, verbatim (our layout wraps) |

Leaving or dying mid-fight: unrecorded. Ours: he keeps his 1000-tick `npc_add` duration, drops the
target past his 16-tile leash, and a re-entry of the zone re-takes the same Kamil.

## Generic interactions in the window

| R | verdict |
|---|---|
| eat shark [1522] [1583] [1692], ranging potion [1540], super restore [1579] | the recorder's kit; not diffed |
| eagle eye / protect from magic / melee [1537] [1546] [1550] [1679] [1689] | prayers: generic, bound |
| magic shortbow (i) + rune arrows, Fire Bolt via opnpct [1594]+, mystic robes equipped [1591] [1592] | the recorder's kit |
| pickups [1693..1697] (arrows, the drop) | generic pickup, not diffed |

## Variables

`%varp6618_dt_ice_stage` is no longer read or written (dt_kamil.rs2 reads
`%varb382_fd_icewarrior_subquest = 2`); dt_ice.rs2's bridge writes are gone. Old -> new: 0->0,
1->1, 2->2, 3->3 (+ `fd_icewarrior_dead` 1), 4->4, 100->5 (the ice agent's mapping).

## Open

1. The 6- and 7-tick gaps between casts out of reach ([1534] -> [1540] -> [1547]); ours keeps 4.
2. The telejump's landing rule (1 sample at (+1,+2)); ours uses it, else the tile north.
3. Two extra entity-face blocks (take + 1, jump + 2): the engine's latch after `npc_attackplayer`.
4. Unprayed slash max and accuracy (no unprayed swing recorded); the 10% fire weakness; the non-fire immunity.
5. Loot tracker 7192: Jagex merges the two cakes (qty 2) and sends event id 15096; ours one line per obj.
6. `last_npc_elemental_weakness` (5536) never sent by ours on a fire spell: generic, `player_magic.rs2`.
7. The cold (windy, 1 damage, stat drain) was not in the test (teleported past the gate): the ice section's.
8. The ice delay distribution 2..10 is fitted on 16 pairings; the shape behind it (Dessous's too) is unconfirmed.
