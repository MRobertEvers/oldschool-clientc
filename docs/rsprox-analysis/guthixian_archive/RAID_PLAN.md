# The Fractured Archive, room by room (RSProx index 4294..4303)

The raid after the Archive Guardian (`guardian_plan.md`). One spec per room,
then the implementation; each spec cites its sessions and ticks and the
counts behind each rule, and names what no recording shows as *unverified*.

## Sessions (rev 241, 2026-10-20/21, recorder "Das Idle", normal worlds)

| session | what it holds | party |
|---|---|---|
| index 4298 | the recorder's own Guardian fight [127..376] (dies), the gravestone | 1 |
| index 4299 | spectating; a Guardian kill's tail [..109] | - |
| index 4300 | spectating: Guardian [10..847], **Ezekiel** [865..1188] (killed at 1181, session ends) | 2 |
| index 4301 | spectating: Guardian tail [..27], **Ezekiel** [52..273], atrium 1 [286], **Dreadnoth** [307..546], atrium 2 + Quartermaster [565], **Xarth** [617..844], atrium 3 [859], **Ocellus** [875..1943], **the vault** [1969..2153] | 3 |
| index 4302 | spectating: **Bork** tail [85..102], atrium 1 [117], **Dreadnoth** [134..409], atrium 2 [423], **Xarth** [502..749] | 5 |
| index 4303 | spectating: Guardian [37..311], **Bork** [331..691], atrium 1 [705], **Dreadnoth** [726..~1144]; Guardian [~1260..1849], **Bork** [1876..2365], atrium 1 [2400], **Dreadnoth** [2421..] | 2, then 5 |
| index 4304 | spectating: Guardian [..329], **Bork** [357..667], **Dreadnoth** [720..~1045]; Guardian [1050..1187]; Guardian [..1722], **Bork** [1744..2126], **Dreadnoth** [2166..2557], **Xarth** [2657..] | 3, then 2 |
| index 4306 | spectating: Guardian [..434], **Bork** [469..~1100]; Guardian [..1147], **Bork** [1172..~1740], Guardian [1749..] | |
| index 4308 | B0aty's run, spectating: Guardian [252..510], **Bork** [523..781], **Dreadnoth** [820..1027], **Xarth** [1151..1431], **Ocellus** [1458..2382], **the vault** [2407..] | 4 |
| index 4294 | (earlier) Guardian x2, **Bork** from ~[1491] | 5 |

Moia and the Dagon'hai are in no recording: not built.

All transcripts: `python3 tools/rsprox/rsprox_index.py get <id> --all-players`
(raiders' movement, seqs and hits; the recorder spectates). Room views:
`tools/boss_view.py <txt> <boss regex> <from> <to>` (every tile relative to the
boss's south-west tile), `tools/room_inv.py <txt>:<from>:<to> ...` (counts).

## The rooms

| stage | room | template square | arrives at | spec | built (test) |
|---|---|---|---|---|---|
| 1 | Archive Guardian (the nexus, plane 0) | (64,87) | (4126,5573,0) | `guardian_plan.md` | yes (`tfa_guardian*.lua`) |
| 2 west | Ezekiel | (63,86) | (4080,5535,0) facing west | `ezekiel_plan.md` | yes, audited (`tfa_ezekiel.lua`) |
| 2 east | Bork and the Smorks | (65,87) | (4174,5600,0) | `bork_plan.md` | yes, audited (`tfa_bork.lua`, `tfa_bork_smorks_first.lua`) |
| - | atrium, after each of stages 2..4 (the nexus, plane 2) | (64,87) | see `nexus_plan.md` | `nexus_plan.md` | yes |
| 3 east | Dreadnoth the Jellied | (65,88) | (4172,5662,0) | `dreadnoth_plan.md` | yes, audited (`tfa_dreadnoth.lua`) |
| 4 east | Xarth | (65,86) | (4172,5535,0) | `xarth_plan.md` | yes, audited (`tfa_xarth.lua`) |
| 5 | Ocellus (plane 1) | (64,88) | (4126,5636,1) | `ocellus_plan.md` | yes, audited (`tfa_ocellus.lua`) |
| - | the vault (plane 2) | (64,90) | (4127,5777,2) | `vault_plan.md` | yes, audited (`tfa_vault.lua`; loot per the wiki) |

Every room is its own instance (the raw transcripts show a new region per
room, 4301 `--raw`), built from its template square; ours: one
`~map_instance_from_square` per room, the raid's clock and totals carried from
instance to instance.

## Scaling (every boss bar, 8 fights)

The bar's base hp at party sizes 1/2 (the two-player floor), 3 and 5:

| boss | 1-2 | 3 | 5 | ratio |
|---|---|---|---|---|
| Guardian shield / health | 550 / 1100 | - / 1600 | 1260 / 2520 | x1.45, x2.29 |
| Ezekiel | 3000 (4300) | 4350 (4301) | | x1.45 |
| Bork + Smorks (`tfa_bork_smork_hp_ui_handler`) | 1500 (4303) | | 3440 (4294, 4302, 4303) | x2.29 |
| Dreadnoth | 2100 (4303) | 3050 (4301) | 4810 (4302, 4303) | x1.45, x2.29 |
| Xarth | | 3770 (4301) | 5950 (4302) | 2600 x1.45, x2.29 |
| Ocellus p1 / p2 | | 3630 / 7610 (4301) | | 2500 / 5250 at 1-2 |

Only a solo party is built, so every boss takes its 1-2 value (the cache's
stat4 for Ezekiel 3000, Bork 1500, Dreadnoth 2100, Xarth 2600).

## The clocks the raid carries

- The HUD timer (script 10036) counts the whole raid from the Guardian's Touch
  and never resets between rooms (4301 [24..363]: 196 -> 363 across the move to
  Ezekiel, flag 0 all along).
- "Challenge complete: <boss>. Duration: m:ss. Total: m:ss": duration from the
  boss's engage to the line (Ezekiel 4301: engage [65], line [273], 208 ticks =
  2:05), total from the raid's start (1:59 + 2:28 = 4:27).

## Scaling

Every boss's health, as its bar states it (`hpbar_hud_basehp`, varbit 6100;
the Smorks' second bar `hpbar_hud_extra_base`, 16571), against the party (the
names on the raid HUD, `script10042`), over index 4297..4308
(`tools/scaling.py`):

| boss | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Guardian, shielded | 550 (4297, 4298) | 550 (4300, 4303, 4304, 4305, 4306) | 800 (4304) | 1030 (4306, 4308) | 1260 (4303) |
| Guardian | 1100 (4297) | 1100 (4299, 4300, 4303, 4304, 4305, 4306) | 1600 (4301, 4304) | 2060 (4306, 4308) | 2520 (4303) |
| Ezekiel | | 3000 (4300) | 4350 (4301, 4304) | | |
| Bork (the bar) | | 1500 (4303, 4304, 4306) | 2180 (4304) | 2810 (4306, 4308) | 3440 (4302, 4303) |
| the Smorks (second bar) | | 600 (4303, 4304, 4306) | 870 (4304) | 1120 (4306, 4308) | 1370 (4302, 4303) |
| Dreadnoth | | 2100 (4303, 4304) | 3050 (4301, 4304) | 3930 (4308) | 4810 (4302, 4303) |
| Xarth | | 2600 (4304) | 3770 (4301) | 4860 (4308) | 5950 (4302) |
| Ocellus, phase 1 | | | 3630 (4301) | 4680 (4308) | |
| Ocellus, phases 2-4 | | | 7610 (4301) | 9820 (4308) | |

**The rule** (32/32 boss and party pairs, 0 misses): health = the solo/duo
figure x **1.000** at one or two raiders, **1.450** at three, then **+0.420**
for each raider more (1.870 at four, 2.290 at five), rounded to the nearest
10. Ocellus's bases are 2500 and 5250 (no duo recorded; both his pairs fit).
Six to eight raiders are in no recording: the same step is assumed.

Ours (`~tfa_scale`, tfa_nexus.rs2): the party size is locked when the
Guardian's challenge begins (the raiders in the room then) and carried room
to room with the raid's clock; every boss pool, the Smorks' bar, and every
percentage taken of a pool (Ezekiel's finale, Dreadnoth's slams, Bork's
speed, Ocellus's floors, the batteries' blast on the Guardian's shield) go
through it. A form change that the engine re-reads from the config
(`npc_changetype`) puts the scaled pool back with the damage taken
(`~tfa_changetype_scaled`). Checked by `test/guthixian_archive/tfa_scaling.lua`
(`::tfaparty`): Xarth 4860 at four, Dreadnoth 4810 at five, Ocellus 3630 at
three through his change of form.

### The adds

How many come at each event, against the raiders (`tools/adds.py`: adds of a
type within a tick or two of each other are one event). The adds follow the
raiders **still standing in the room**, not the party that started:

| add | 1 | 2 | 3 | 4 | 5 | rule (ours) |
|---|---|---|---|---|---|---|
| Guardian tentacle, a wave | 1 (20/20) | 2 | 3 (17/17) | 2..4 | 2..5 | one on or beside each living raider; fewer at four and five as raiders fell |
| Ezekiel's swords, a summon | | 3-4 | 3-4 | | | the layout's, not the party's |
| Dreadnoth's jellies, a phase k | | 6, 12, 12 | 9, 11-12, 11-12 | 12, 11, 11 | 12, 12, 12 | min(12, 3 x living x k), split over the k slams (11 = a blocked tile); 7 at his death |
| Xarth's corpses, a volley | 1 | 2 | 2 | 2 | 3 | max(min(n, 2), (n + 1) / 2), n living: 4304's duo drops to one a volley after a death at [3051] |
| Ocellus's void rifts, phase 3 | | | 1 (12/12) | 2 (14/14) | | max(1, living / 2); five and more unrecorded |
| Ocellus's void rifts, phase 4 | | | 2, 3, 4 | 2 | | two, three, four: not the party's |
| Ocellus's void vine | | | 1 | 1 | | one |

The Guardian's mushrooms are two by each raider (§6 of guardian_plan.md). Unverified
(no bar to read): the adds' health (jellies 75, corpses 62, Ezekiel's swords
80, the Guardian's batteries 180), held at the cache's.

Open: today each raider's later rooms are their own instance (each move
makes one), so a party's size only reaches the rooms through this lock.
