# Ice Demon: recorded behaviour

Read from submissions 3285 (two rooms), 843, 3282 (solo, normal), 3389 (solo, Challenge Mode) and 820 (a four-man
room), all on Grid Master worlds; see [README.md](README.md). Every recorded Ice Demon room is turned a quarter, so the
geometry here is read from **untranslated** transcripts (instance tiles, every raider shown; README.md, "Instance
tiles"). Tool: `tools/cox/rsprox_icedemon.py` (sections `attacks`, `pop`, `fiends`, `braziers`, `blocks`, `death`);
`tools/rsprox_tells.py --anchor 7584-7586` lists every tell in the room. The content cites these rows as `RI-*`
(`cox_icedemon.rs2`, `cox.constant`, `cox.npc`). The room's spec is `docs/minigames/cox/rooms/icedemon_spec.md`.

    python3 tools/cox/rsprox_icedemon.py <raw>/archive/{3285,843,3282,3389,820}.txt

**C** is an attack's tick (his projectile), **L** its landing, **P** the pop (the first smoke), **D** the killing hit.
**g** is the target's distance from his 2x2. Heights are raw (our byte is raw / 4).

## His attacks (46)

| Row | Recorded |
|---|---|
| RI-A1 | One attack every **3** ticks (39 of 40 gaps). The projectile leaves his **south-west tile** for the tile the target ended C-1 on (46 of 46). |
| RI-A2 | His seq on C is `demon_casting` (17), or his delayed `demon_block` when a hit lands on him that tick (29): one seq a tick, and the block wins. |
| RI-A3 | **Ice Burst:** `ice_burst_travel` (366), start 51, angle 8, progress 64, heights 325 -> 10, end **30 x (1 + (1 + g) / 3)**: 30 at g 1, 60 at g 2-4. At g 1 it ends before it starts, so the client never draws it; at 60 it flies 9 cycles -- the wiki's "nearly impossible to spot". `ice_cast` (171, range 10) at his tile on C. |
| RI-A4 | The burst lands on **L = C + end / 30** as nine `ice_blitz_impact` (363) at height 10 on the 3x3 round the target, all nine always (none culled by walls): the target at delay 0, then SW+NE at 3, W+E at 6, NW+SE at 9, S+N at 12 (38 of 38, the same in every turned room, so in world tiles), and `ice_burst_impact` (170, range 6) on the target. A raider it freezes hears `paralyze` (154) on L. |
| RI-A5 | **Snow boulder:** `raids_icedemon_iceball_travel` (1324), start 51, angle 8, progress 64, heights 350 -> 10, end **30 x (2 + g / 3)**: 60 at g 1-2, 90 at g 3-4. `castlewars_catapult_fire` (1440, range 10) at his tile on C. It lands on L as one `raids_icedemon_iceball_hit` (1325) at height 10 on the target and `100_cauldron_explodes` (1021, range 6) there. |
| RI-A6 | His block is `demon_block` (65), sent on the attacker's attack tick delayed to its projectile's end (70 of 70); each hit on him plays `demon_hit` (404) to its attacker. |

## The braziers and the fiends

| Row | Recorded |
|---|---|
| RI-B1 | Every successful deposit plays `fire_crackling_med` (2066, range 4) on the brazier. A light shows `human_light_torch` and the brazier turns lit **the tick after** (20 of 20); fuel on a lit brazier shows `human_pickuptable` and changes nothing. |
| RI-B2 | The braziers are the template's: two `centrepiece_straight` and two `centrepiece_diagonal` a room (m51_83 / m52_83), each at its own rotation. |
| RI-F1 | One icefiend a raider (820: four), beside its brazier. It acts every **3 or 4** ticks (180 of 182 gaps), and only once its brazier is lit: `pyrefiend_attack` and `icefiend_attack` (531, range 4) on the brazier's tile. |

## The pop (six)

| Row | Recorded |
|---|---|
| RI-P1 | **P:** `smokepuff_large` (188) at height 40 on all four braziers, brazier i (template order) delayed **5 i** cycles; one `smokepuff` (1930, range 8, **4 loops**) on brazier 1; the lit braziers go out. No message. |
| RI-P2 | **P+1:** every fiend plays `pyrefiend_death` with `icefiend_death` (532, range 10) at it, and he steps out of the storm, a tile a tick, for 7 tiles (P+1..P+7). |
| RI-P3 | **P+8** (P+9 once): the fighting form (7585). |

## His death (five)

| Row | Recorded |
|---|---|
| RI-D1 | **D:** `demon_death` (67) with the killing hit, and `demon_death` (403) to the killer. |
| RI-D2 | **D+5:** the drops on his south-west tile, and the storm's tile becomes `invisible_type8_nonblocking` (the way on). |
| RI-D3 | The storm (`raids_icedemon_snow`) is added at **rotation 0** in every room, whatever its turn. |
| RI-D4 | **The secondaries:** one public stack on his south-west tile on D+5, lying 200 ticks: 7 stinkhorn, 7 endarkened juice and 5 cicely solo (843); 13, 13 and 8 for 820's four. That is 2 x party + 5 of the first two and party + 4 cicely. The fiendish ashes are separate: the killer's, private for 100 ticks. |

## Fixed in our content (2026-10-18)

- RI-A1..A5: his attacks from his south-west tile with the recorded projectiles, ends, heights, sounds, and the
  landing's graphics and sounds sent once on the landing tick from his own queue (`[ai_queue4]`); the burst's nine in
  the recorded order. Before: Near-Reality's boulder (start 40, heights 60 -> 15), no burst projectile, a plain 3x3 of
  363 at delay 30 skipping blocked tiles, no sounds. A burst that freezes plays `paralyze`.
- RI-A6, D1: `cox.npc` gives him `demon_block`, `demon_hit`, `demon_death` (the generated record had
  `demon_update_defend` and no sounds); the ledger `npc_combat/r/raids_icedemon_combat.combat` says the same.
- RI-B1, F1: the crackle on every deposit, the light catching the tick after, the fiend's sound on its brazier.
- RI-B2, D3: the spawn no longer re-adds the braziers as `centrepiece_straight` at the room's rotation (the diagonal
  ones were drawn square); the storm at rotation 0.
- RI-P1..P3: the smoke at height 40 staggered by brazier, its sound on brazier 1; the fiends' deaths and his walk at
  P+1, the fighting form at P+8; the unrecorded "The ice around the demon melts away." removed.
- RI-D2: `death_delay=5` puts the drops, the storm's removal and his despawn on D+5.
- RI-D4 (2026-10-19): the secondaries are one public stack under him, scaled by the party, for 200 ticks. They used to go into each raider's pack as a flat 7/7/5.
- RI-A2 (2026-10-19): a raider's hit on his attack tick now sends `demon_block`, not `demon_casting`. The engine's
  `npc_defend` kept any swing queued that tick; it now takes the ordinary priority gate (equal or higher wins), and
  the two are of equal priority. Found by the full `rsprox_tells.py` inventory against our tick logs (ours cast on
  every attack, 133 of 133).
- `test/raids/_icedemon_tells.lua` checks every attack's tells off the server's tick log (both styles: it prays
  nothing, where the room solver's Protect from Missiles only ever draws boulders).

## Open

| Item | State |
|---|---|
| `smokepuff` loops 4 | `sound_area` sends 1 loop; needs a loops argument on the engine command (Xarpus's `apple_splat` waits on the same). |
| The freeze | Of ten raiders caught by a burst who stayed, most heard `paralyze` on L and on following ticks; three caught raiders moved within two ticks. Who is frozen, and for how long, is not settled here. |
| Aggression | He answers an attack; whether he starts one with a raider who walks up after the transform is not recorded (every recorded raider attacked at once). |
