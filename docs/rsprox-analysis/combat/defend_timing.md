# Defend (block) animations: when Jagex sends them

Ours plays an NPC's defend animation when the hit lands. Jagex's server sends it when the attack is made. The animation's delay carries the gap: the projectile's end cycle for a ranged or magic attack, 0 for melee. The same rule holds for the player's own block when an NPC attacks. This was found on Zuk (`zuk_defend`, delay 86) and independently by the ToB work on Verzik P1 (`verzik_phase1_defend`, delay 106 for the Dawnbringer).

Tool: `tools/rsprox/rsprox_defend_timing.py`. With `--focus` it gives one row per (attack kind, NPC, what was sent on the attack tick, splat value, landing offset).

```sh
python3 tools/rsprox/rsprox_defend_timing.py --focus ~/.rsprox/binary/archive/{3282,3285,3299,3389,787,788,820,843}.txt
```

Sources are the eight named transcripts in the archive cache: God Wars (3282), CoX (3285, 3389, 820, 843), the Inferno (3299) and ToB (787, 788). The NPCs read are the ones with a defend sequence. Olm, Hespori, Araxxor and many bosses have none and are left out.

## What the server sends

### The player attacks an NPC

| | Recorded | Ours |
|---|---|---|
| Tick | **The attack tick**, the player's swing or cast | the tick the damage queue lands (`ToriRSServer_CombatHitNpc`, `torirs_server_combat.c:1826`) |
| Delay, projectile | **The projectile's end cycle, exactly**: dragon arrows 86 at Zuk (3299), 71 at Zulrah (`snakeboss_defend@71`), blowpipe 37 at an ogre (`ogre_block@37`), Dawnbringer 106 at Verzik P1 (ToB), and every bow/blowpipe shot at the colosseum minotaur and the Inferno adds (`defend@proj_end`, hundreds of rows) | 0 |
| Delay, melee | **0**, on the swing tick, with the splat a tick later: Bandos 167 of 169 swings (3282, voidwaker/sword); ancient ogre, vampyre, gorak, goblins, bloodveld; Guardians `human_spear_block@0` (820) | 0, but a tick late, with the splat |
| A miss | **Still sent**: 18 of 18 Bandos swings that splatted 0 | sent when the 0 lands |
| The NPC swings on the same tick | **The sequence priority decides; equal or higher wins.** The attack is sent when it outranks the defend: Bandos 2 of 2 (`forcedpriority` 6 over 5), `jalxil_attack_ranged` (6 over 5), `jalimkot_attack` (3 over `jalimkot_defend`'s 1). The Ice Demon's `demon_casting` and `demon_block` are both 5, and the defend is sent on 29 of 29 ticks a raider shot him as he cast (CoX 820, 3389; `docs/rsprox-analysis/cox/icedemon.md`) | the same rule (2026-10-19; `npc_defend` used to keep any swing) |
| The NPC is mid-attack from an earlier tick | **The defend is sent anyway.** The client's sequence priority decides what plays. | (same) |
| The NPC's defend SOUND | **When the hit lands**, delay 0: Zuk's 410 `dragon_hit` with each splat (3299); Bandos's 3867 `godwars_bandos_avatar_hit` a tick after the swing (3282 t3486 → 3487) | when the hit lands (correct) |

### An NPC attacks the player

| | Recorded | Ours |
|---|---|---|
| Ranged/magic | The player's block (`warguild_parry_defend`, `human_scythe_block`, `human_stafforb_block`, `human_unarmedblock`, `human_shield_defence`) is sent on the NPC's attack tick, delayed to the projectile's end (`delay=81`, `61`, `66`, `80`, `90`, `150`...). The splat lands 1–3 ticks later: 889 blocks. | Played only where a boss script does it by hand (Zuk, Jad, Hunllef, Zulrah, GWD bosses...). Generic `npc_combat_ranged`/`_magic` play nothing. |
| Melee | The block (delay 0), the NPC's swing and the splat all on one tick: Bandos t3512, and 1,079 of 1,131 undelayed blocks share their tick with the splat. | **Never played.** `[ai_opplayer2,_]` (`skill_combat/combat.rs2`) has no `anim(%varp6284_com_defendanim, 0)`, although its comment and the engine's (`torirs_server_combat.c:2089`, "content plays anim(%com_defendanim)") both say it does. |

## Why it matters

Ranged/magic attacks on an NPC: our flinch starts at the first cycle of the landing tick, not on the landing cycle. Up to 29 cycles (about a tick) out, and it is a separate update from the attack.

Melee attacks on an NPC: our flinch is a whole tick late, arriving with the splat instead of with the swing.

The NPC swinging on the tick it is attacked: `anim_wins` lets a later sequence of equal or higher priority win within a tick (`torirs_server_combat.c`). That is Jagex's rule too. Every recorded attack that beat the defend outranks it; the Ice Demon's, of equal priority, loses to it every time. (Until 2026-10-19 `npc_defend` kept any swing queued that tick, which held the Ice Demon's cast against the recording.)

The player is never seen blocking an ordinary NPC's attack.

## Proposed fix

One engine primitive and three content call sites. No per-weapon changes.

1. **Engine: `npc_defend` (a new script command).** It plays the active NPC's `block_seq` with
   - delay = the end cycle of the projectile the active player sent at this NPC earlier on this tick, or 0 if there was none;
   - the ordinary priority gate (`anim_wins`, `>=`). This first said a strict gate (the swing always wins); the Ice Demon (29 of 29 defends over an equal-priority cast) showed the priority rule is the recorded one.

   `projanim_npc` already passes through the server with (target, end cycle). Recording the last end cycle per (player, NPC) for the current tick costs two ints on the NPC: `defend_delay_tick` and `defend_delay_cycles`.
2. **Engine: stop flinching in `ToriRSServer_CombatHitNpc`.** Remove `play_npc_seq(npc, npc->block_seq)` and keep `npc_sound_nearby(..., block_sound, 0)`, since the sound belongs to the landing. The engine's own NPC-vs-NPC swing (the `combat_target_npc` branch) calls the same flinch on its swing, for the Inferno adds against the glyph and similar cases.
3. **Content: `[proc,player_hit_npc_prepare]`** (`skill_combat/scripts/player/player_hit_npc_prepare.rs2`) runs exactly once per player attack, on the attack tick, with the target active, before its `npc_queue(2, ...)`. It is reached on misses too (`player_magic.rs2:559`). It ends with `npc_defend;`. Every weapon, special and spell inherits the rule from that one line. Specials that fire their projectile after calling prepare are the one ordering hazard: the remembered end cycle must already be set. Audit the 25 callers and move any `projanim` above the prepare call.
4. **Content: the player's block.**
   - `[ai_opplayer2,_]`: add `anim(%varp6284_com_defendanim, 0);` before `~npc_meleeattack` (the LostCity line the comment already quotes).
   - `npc_combat_ranged.rs2` / `npc_combat_magic.rs2`: add `anim(%varp6284_com_defendanim, $duration)` where they compute the projectile's flight.
   - Fix the stale comment at `torirs_server_combat.c:2089`.
5. **Remove the hand-played duplicates** once 1–3 land: Zuk's glyph flinch (`inferno_zuk.rs2`), the Inferno add copies, and any `npc_anim(npc_param(defend_anim), …)` in boss scripts. Each one is now either the same packet or a second, wrong one.

**Proof** (no full sweeps):
- The selftest at `torirs_server_world_selftest.c:14790` (block_seq resolution) still passes.
- One scriptrun each: a melee swing (a goblin), a bow shot and a spell, read with `tools/rsprox/ticklog_tells.py`. Expect `npc_anim <defend>` on the attack tick with delay 0, the arrow's end and the spell's end respectively. Expect no defend on the splat tick. Expect the defend sound on the splat tick.
- A tick where the NPC swings while being attacked shows the attack when its priority is higher than the defend's, else the defend.
- `inferno_zuk_tells.lua` still shows `zuk_defend, delay=<arrow end>` beside each player shot, and `moving_safe_spot_hit` delayed to Zuk's landing.

## Open

- A splashed spell: does a splash still send the defend? The table has no named-magic splat-0 row on an NPC with a defend sequence. Read one before step 3 decides the splash path.
- Multi-projectile attacks (dark bow, a double-hit special): which end cycle the delay takes. The rule above takes the last one sent this tick.
