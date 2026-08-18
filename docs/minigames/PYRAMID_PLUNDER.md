# Pyramid Plunder

> Written 2026-08-17; **implemented the same day** — see §13 for what shipped
> and §14 for what is still unverified. The plan below is kept as written
> because the measurements in it are the spec; §13 records the three places
> where building it proved the plan wrong.
>
> **Behaviour authority (wiki):**
> [Pyramid Plunder](https://oldschool.runescape.wiki/w/Pyramid_Plunder) ·
> [Jalsavrah Pyramid](https://oldschool.runescape.wiki/w/Jalsavrah_Pyramid) ·
> [An anonymous looking door](https://oldschool.runescape.wiki/w/An_anonymous_looking_door) ·
> [Guardian mummy](https://oldschool.runescape.wiki/w/Guardian_mummy) ·
> [Urn (Pyramid Plunder)](https://oldschool.runescape.wiki/w/Urn_(Pyramid_Plunder)) ·
> [Tomb Door](https://oldschool.runescape.wiki/w/Tomb_Door) ·
> [Tomb Door (exit)](https://oldschool.runescape.wiki/w/Tomb_Door_(exit)) ·
> [Grand Gold Chest](https://oldschool.runescape.wiki/w/Grand_Gold_Chest) ·
> [Sarcophagus (Pyramid Plunder)](https://oldschool.runescape.wiki/w/Sarcophagus_(Pyramid_Plunder)) ·
> [Mummy (Pyramid Plunder)](https://oldschool.runescape.wiki/w/Mummy_(Pyramid_Plunder)) ·
> [Scarab swarm](https://oldschool.runescape.wiki/w/Scarab_swarm) ·
> [Pharaoh's sceptre](https://oldschool.runescape.wiki/w/Pharaoh%27s_sceptre) ·
> [Snake charm](https://oldschool.runescape.wiki/w/Snake_charm) ·
> [Lockpick](https://oldschool.runescape.wiki/w/Lockpick) ·
> [Simon Templeton](https://oldschool.runescape.wiki/w/Simon_Templeton) ·
> [Rocky](https://oldschool.runescape.wiki/w/Rocky) ·
> [Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper) ·
> [Transcript:Guardian mummy](https://oldschool.runescape.wiki/w/Transcript:Guardian_mummy)
>
> Every id, coord, plane and placement count below was **measured from this
> tree** — `OSRS-Content/osrs239-content/configs/*.compack`, `configs/all.npc`,
> `configs/all.varbit`, `maps/m30_69.jl2`, `maps/m51_43.jl2`,
> `interfaces/ntk_overlay.if`. Rows marked *(to measure)* have not been.
> Re-measure rather than trusting this prose.
>
> LostCity: **none** (Pyramid Plunder is 2006 content, post-254). The existing
> in-tree implementation was ported from 2009scape's `PyramidPlunderMinigame` /
> `PlunderUtils` / `PlunderData`, whose numbers **predate the 24 Mar 2021 and
> 30 Aug 2023 reworks**. That is the single largest source of drift in §6.

---

## 1. Status: what is already here

Three files, 810 lines, already compiled into every build (the whole
`server/scripts` tree is a `--src` root of `mock230-scripts`):

| File | Lines |
|---|---|
| [plunder.rs2](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_pyramidplunder/scripts/plunder.rs2) | 729 |
| [plunder.constant](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_pyramidplunder/configs/plunder.constant) | 33 |
| [plunder.varp](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_pyramidplunder/configs/plunder.varp) | 48 |

The port queue marks slices `7`, `7b`, `7c` **done**
([SCAPE2009_CONTENT_PORT_QUEUE.md:128-152](../SCAPE2009_CONTENT_PORT_QUEUE.md)),
and [SKILLS_CONTENT_PORT_QUEUE.md:274](../SKILLS_CONTENT_PORT_QUEUE.md) row 114
"Pyramid Plunder remainder" is **blocked**. What exists is a playable skeleton
with the right *shape* — entrance, join, 8 rooms, timer, overlay, spear traps,
tomb doors, sarcophagus, chest, urns, snake charm — and almost none of the
right *numbers*. §6 is the defect list.

### The cache ships the entire asset set

Nothing here needs authoring. Everything below is already in the rev239 cache
and mostly unreferenced by server content:

```
grep -cE "=ntk_" OSRS-Content/osrs239-content/configs/all.loc.compack   # 96
grep  -n "ntk_"  OSRS-Content/osrs239-content/pack/npc.server           # 8
grep  -n "ntk_"  OSRS-Content/osrs239-content/configs/all.varbit.compack # 32
```

Measured placement counts in `maps/m30_69.jl2` — these **confirm the wiki's
layout numbers exactly**, which is the strongest evidence the cache map is the
live OSRS one:

| Placement | Count | Wiki | Plane |
|---|---|---|---|
| Urn multilocs (`ntk_urn_type{1,2,3}_multi_*`, 15 types) | **103** | 103 map pins = 13/room, 12 in room 3 | 0 |
| `ntk_speartrap_inmotion` | 32 | 4 per room | 0 |
| `ntk_tomb_door{1,2,3,4}` | 8 each = 32 | 4 per room × 8 | 0 |
| `ntk_tomb_door_exit` | 16 | 2 per room | 0 |
| `ntk_golden_chest_multi` | 8 | 1 per room | 0 |
| `ntk_sarcophagus_multi` | 8 | 1 per room | 0 |
| `ntk_urn_rough_multi` | 8 | *not* on the wiki urn map — lobby decor | 2, 3 |
| `ntk_antechamber_exit` | 4 | 4 anonymous-door rooms | 2, 3 |
| Outside doors `ntk_pyramid_door_{n,e,s,w}_multi` (`m51_43`) | 1 each | 4 | 0 |

**All eight treasure rooms are on plane 0 of `m30_69`.** The four antechambers
are on planes 2 and 3.

---

## 2. Prerequisites and gates

| Gate | Wiki | In tree |
|---|---|---|
| Started [Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper) (Sophanem access) | required | `%ics_little_var` ≥ 2 — quest slice 28 done |
| Thieving 21, **unboostable** | required to start | not gated; existing code reads boosted `stat(thieving)` |
| Thieving 31/41/51/61/71/81/91 | rooms 2–8, unboostable | `~ntk_room_level` = `11 + room*10` — correct arithmetic, boosted read |
| [Contact!](https://oldschool.runescape.wiki/w/Contact!) | recommended only (Sophanem Dungeon bank) | out of scope |
| [Easy Desert Diary](https://oldschool.runescape.wiki/w/Desert_Diary) | required to sell artefacts noted to Simon | out of scope, see §9 |

---

## 3. Entity inventory

### 3.1 NPCs

| Name (server) | Cache id | Level | HP | Notes | Wiki |
|---|---|---|---|---|---|
| `ntk_mummy_guardian` | 1779 | — | — | `op1=Talk-to`, `op3=Start-minigame`, `vislevel=0`. Spawned at **(3, 1934, 4427)** in `m30_69.spawn:8` | [Guardian mummy](https://oldschool.runescape.wiki/w/Guardian_mummy) |
| `ntk_mummy_guardian_dummy` | 1780 | — | — | "Annoyed guardian mummy". **Zero content references** — the sceptre-found broadcast (§5.8) is what it is for | [Guardian mummy](https://oldschool.runescape.wiki/w/Guardian_mummy) |
| `ntk_mummy_1` … `ntk_mummy_5` | 7658–7662 | 84 | 90 | att 90 / def 90 / str 30. `param_26=2`. **No `attackrate`** (wiki: speed 4), no max-hit param (wiki: 6), no drop table (wiki: Bones ×1 always) | [Mummy (Pyramid Plunder)](https://oldschool.runescape.wiki/w/Mummy_(Pyramid_Plunder)) |
| `ntk_scarab_swarm` | 4192 | 98 | 25 | att 255 / def 30 / str 5, `attackrate=1`, `crushdefence=5`. Matches the wiki exactly | [Scarab swarm](https://oldschool.runescape.wiki/w/Scarab_swarm) |
| `ntk_tarik` / `ntk_tarik_nospots` | — | — | — | `op1=Talk-to`. Flavour NPC at (0, 3289, 2787) in `m51_43.spawn:40`. **Not a gameplay gate** on the wiki | [Tarik](https://oldschool.runescape.wiki/w/Tarik) |
| Simon Templeton | `configs/all.npc:161048` | — | — | Present in the cache config, **no `npc.server` name binding**, no shop/sale script | [Simon Templeton](https://oldschool.runescape.wiki/w/Simon_Templeton) |

### 3.2 Locs

| Name | Cache id(s) | Options | Notes |
|---|---|---|---|
| `ntk_pyramid_door_{n,e,s,w}_multi` | 26622–26625 | Search | Outside multiloc parents |
| `ntk_pyramid_door_*_noanim` | 20974, 20977, 20987, 21253 | Search | Closed children |
| `ntk_pyramid_door_*_open_noanim` | 20975, 20978, 21251, 21254 | — | Opened |
| `ntk_pyramid_door_*_anim` | 20956, 20976, 20979, 21252 | — | **Punching arm** — the failure animation, currently unused |
| `ntk_antechamber_exit` | 20932 | — | 4 placements, planes 2/3 |
| `ntk_speartrap_inmotion` | 21280 | Pass | 4 per room |
| `ntk_tomb_door{,1,2,3,4}` | 26617–26621 | Pick-lock | 26617 has **0 placements**; 1–4 have 8 each |
| `ntk_tomb_door_noanim` / `_anim` | 20948 / 20949 | Pick-lock / Enter | Closed / opened children |
| `ntk_tomb_door_exit` | 20931 | Open, Quick-leave | 2 per room |
| `ntk_golden_chest_multi` | 26616 | Search | 1 per room |
| `ntk_golden_chest_closed` / `_open` | 20946 / 20947 | Search / — | |
| `ntk_sarcophagus_multi` | 26626 | Open / Search | 1 per room |
| `ntk_sarcophagus` / `_anim` / `_open` | 21255 / 21257 / 21256 | Open / — / Search | **Three states** — the middle "opening" state is unused today |
| `ntk_urn_type1_multi_1..5` | 26580, 26600–26603 | — | Slot 1–5 |
| `ntk_urn_type2_multi_6..10` | 26604–26608 | — | Slot 6–10 |
| `ntk_urn_type3_multi_11..15` | 26609–26613 | — | Slot 11–15 |
| `ntk_urn{1,2,3}_closed` | 21261–21263 | Search, Check for Snakes | Cup / Bulb / Jar |
| `ntk_urn{1,2,3}_snake` | 21269, 21270, 21273 | Search, Charm Snake | |
| `ntk_urn{1,2,3}_snake_charmed` | 21276–21278 | Search | |
| `ntk_urn{1,2,3}_open` | 21265–21267 | Search | |
| `ntk_urn_rough_*` | 21264, 21268, 21275, 21279 | — | **Antechamber decoration only** (planes 2/3) — do not wire |

### 3.3 Objs

All nine artefacts, their `cert_` notes and `placeholder_` forms exist:
`ntk_ivory_comb`, `ntk_scarab_pottery`, `ntk_statuette_pottery`,
`ntk_seal_stone`, `ntk_scarab_stone`, `ntk_statuette_stone`, `ntk_seal_gold`,
`ntk_scarab_gold`, `ntk_statuette_gold`
(`configs/all.obj.compack:9027-9051`).

Sceptre: `ntk_jewelled_sceptre_0` … `ntk_jewelled_sceptre_8`
(`configs/all.obj.compack:9045-9051, 13075-13079`). `_0` is uncharged.

Support items: `lockpick` (1523), `snake_flute` (4605 — this is the cache name
for [Snake charm](https://oldschool.runescape.wiki/w/Snake_charm)).

**[Rocky](https://oldschool.runescape.wiki/w/Rocky) does not exist anywhere in
the tree.** See §9.

### 3.4 Varbits

`configs/all.varbit.compack:2347-2384` — 32 bits, all already allocated:

| Varbit | Id | Base varp | Bits |
|---|---|---|---|
| `ntk_urn1_state` … `ntk_urn15_state` | 2346–2360 | `ntk_urn` (820) | 2 each |
| `ntk_sarcophagus_push` | 2361 | — | **unused today** |
| `ntk_sarcophagus_state` | 2362 | — | |
| `ntk_golden_chest_state` | 2363 | — | |
| `ntk_current_room_level` | 2364 | — | **unused today** |
| `ntk_trap_active` | 2365 | — | **unused today** |
| `ntk_door1_state` … `ntk_door4_state` | 2366–2369 | — | |
| `ntk_played_before` | 2370 | — | **unused today** |
| `ntk_outside_door1_state` … `4` | 2371–2374 | — | **unused today** |
| `ntk_player_timer_count` | 2375 | `ntk_var2` (**821**) | 22–31 (max 1023 ≥ 500 ✓) |
| `ntk_thieving_required` | 2376 | `ntk_var_temp` (**822**) | 0–8 |
| `ntk_room_number` | 2377 | `ntk_var_temp` (**822**) | 9–12 |
| `ntk_right_door_opened` | 2383 | — | **unused today** |

### 3.5 Interfaces

`ntk_overlay` = **IF 428** ([interfaces/ntk_overlay.if](../../OSRS-Content/osrs239-content/interfaces/ntk_overlay.if)),
8 components: a `type=6` model (the hourglass model 16350, removed from live
OSRS 12 Apr 2018 but still in this cache), a `type=4` timer text, and a
two-layer `type=3` progress bar (`com_6` track + `bar` fill).

- `universe.onload` → **clientscript 971** with the four layer ids
- `com_5.onload` / `com_5.onvarptransmit` → **clientscript 477**,
  `varptriggers=822`

`ntk_scores` = IF 215, unreferenced.

> ⚠️ **`varptriggers=822` is the only trigger on the overlay, but the timer
> count lives on varp 821.** Either clientscript 971 polls 821 by another path
> or the bar never repaints. *(to measure)* — probe with the headless layout
> harness (see `headless-layout-remount-probe` notes) and dump 971/477 with
> `tools/dump_interface`.

### 3.6 Coordinates (measured)

| Constant | Value now | Measured truth |
|---|---|---|
| `^ntk_outside` | `0_51_43_24_50` = (0, 3288, 2802) | ✓ just north of the north door (0, 3288, 2799) — wiki: "leaving always places players outside the northern door" |
| `^ntk_guardian_room` | `2_30_69_48_4` = (2, 1968, 4420) | ✗ **wrong room.** The guardian spawns at **(3, 1934, 4427)** |
| `^ntk_empty_room` | `2_30_69_14_34` = (2, 1934, 4450) | ✓ an antechamber — but there are **three** wrong ones |
| Antechamber exits | — | (2,1934,4449) · (2,1968,4419) · (3,1934,4419) · (3,1968,4449) |
| `^ntk_room1` … `^ntk_room8` | plane 0 of `m30_69` | plausible; each sits by a `ntk_tomb_door_exit` *(to measure per room)* |
| Outside doors | — | N (0,3288,2799) · E (0,3293,2794) · S (0,3288,2789) · W (0,3283,2794) |

---

## 4. The success-rate formula

Every roll in this minigame is the standard OSRS skilling interpolation in
256ths. **The engine already exposes it as one opcode**: `stat_random(stat,
low, high)` (`SS_OP_STAT_RANDOM` 2119), used by
[thieving.rs2:51](../../OSRS-Content/osrs239-content/server/scripts/skill_thieving/scripts/thieving.rs2#L51).
Where the room penalty has to be applied, the arithmetic form is the one
already written twice in this tree — `~agility_success_chance`
([agility_lap.rs2:100](../../OSRS-Content/osrs239-content/server/scripts/skill_agility/scripts/agility_lap.rs2#L100))
and `~hunter_success_chance_for`
([hunter_traps.rs2:40](../../OSRS-Content/osrs239-content/server/scripts/skill_hunter/scripts/hunter_traps.rs2#L40)):

```
chance256 = floor((low * (99 - level) + high * (level - 1) + 49) / 98) + 1
          clamped to [0, 256]
```

Verified against the wiki's stated percentages (urn Search at Thieving 21:
`(100*78 + 180*20 + 49)/98 + 1 = 117` → 45.7% ✓; Check for Snakes at 99:
`250` → 97.7% vs wiki 98.0% ✓).

### Endpoints, all measured from wiki `{{Skilling success chart}}` params

| Action | Skill | low | high | Source |
|---|---|---|---|---|
| Anonymous looking door | Thieving | 160 | 250 | [An anonymous looking door](https://oldschool.runescape.wiki/w/An_anonymous_looking_door) |
| Urn — Search (closed) | Thieving | 100 | 180 | [Urn](https://oldschool.runescape.wiki/w/Urn_(Pyramid_Plunder)) |
| Urn — Search (snake visible, uncharmed) | Thieving | 130 | 220 | ″ |
| Urn — Check for Snakes | Thieving | 160 | 250 | ″ |
| Urn — Search (charmed) | Thieving | ? | ? | **not charted** *(to measure)* — §11 |
| Tomb Door — no lockpick | Thieving | 130 | 220 | [Tomb Door](https://oldschool.runescape.wiki/w/Tomb_Door) |
| Tomb Door — lockpick | Thieving | 160 | 250 | ″ |
| Grand Gold Chest | Thieving | 130 | 220 | [Grand Gold Chest](https://oldschool.runescape.wiki/w/Grand_Gold_Chest) |
| Sarcophagus | **Strength** | `80 − roomLvl` | `270 − roomLvl` | [Sarcophagus](https://oldschool.runescape.wiki/w/Sarcophagus_(Pyramid_Plunder)) HTML comment |

**Room penalty.** Urn, tomb door and chest rates "decrease by 1/256 every
room": subtract `(room − 1)` from the computed `chance256`. The charted rates
are the room-1 values.

**Sarcophagus** takes no room penalty — the room is already in the endpoints
via `roomLvl` (21/31/41/51/61/71/81/91). Room 7 low is −1 and room 8 low is
−11, which is why Strength 1 cannot open room 7 and Strength 1–7 cannot open
room 8. **Clamp at 0, do not clamp at 1.**

**Boosts.** Wiki: the room *entry* levels "cannot be boosted" → `stat_base`.
The sarcophagus explicitly uses **current** Strength (Mod Ash, 17 Jun 2021) →
`stat`. Every other roll: *(to measure)*, but the tree's convention for
obstacle-style rolls is `stat_base` (`~agility_success`).

---

## 5. Mechanics, as they must behave

### 5.1 Entrance — the four anonymous looking doors

- One of four is correct; **world-shared**, rerolled **every 25 minutes**
  (2500 ticks).
- Correct door: hard-gated at Thieving 21 (unboostable). Roll `160/250`. On
  success → guardian chamber **(3, 1934, 4427)** + **20 Thieving XP**.
- On failure, and on the correct door only: play `ntk_pyramid_door_*_anim`
  (the bandaged arm), message *"A mysterious bandaged hand punches you away
  from the door. Ouch!"*, **stun** and **1–4 damage**. The stun idiom in this
  tree is `%action_delay = calc(map_clock + $ticks)`
  ([thieving.rs2:71](../../OSRS-Content/osrs239-content/server/scripts/skill_thieving/scripts/thieving.rs2#L71)).
- Wrong doors: open at **any** level, no XP, no damage — deposit into one of
  the three empty antechambers.
- Leaving any antechamber → `^ntk_outside` (north door).

### 5.2 Join and the timer

- `op3` "Start-minigame" on the guardian, or the dialogue path.
- 5 minutes = **500 ticks**. `^ntk_time_ticks = 500` ✓ already correct.
- Timer must decrement **per tick**, not every 5 (§6.27).
- Expiry → expel to `^ntk_outside`, mesbox, close overlay, clear state.
- The 2014-08-14 poll added a right-click **Quick-leave** on the exit doors ✓
  already wired as `oploc2`.

### 5.3 Rooms and XP

XP in this engine is tenths — the tree already writes urn room 1 as `600` for
the wiki's 60. **All four tables, ×10:**

| Room | Thieving req | Urn | Chest | Door | Sarcophagus (Strength) | Sceptre |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 21 | 600 | 400 | 400 | 200 | 1/4200 |
| 2 | 31 | 900 | 600 | 600 | 300 | 1/2800 |
| 3 | 41 | 1500 | 1000 | 1000 | 500 | 1/1600 |
| 4 | 51 | 2150 | 1400 | 1400 | 700 | 1/950 |
| 5 | 61 | 3000 | 2000 | 2000 | 1000 | 1/800 |
| 6 | 71 | 4500 | 3000 | 3000 | 1500 | 1/750 |
| 7 | 81 | 6750 | 4500 | 4500 | 2250 | 1/650 |
| 8 | 91 | 8250 | 5500 | — | 2750 | 1/650 |

Door XP is **halved** with a lockpick. Sarcophagi grant **no Thieving XP at
all** (the wiki's `skill1exp*` are all 0).

### 5.4 Spear traps

4 loc placements per room, one row. Pass grants **10 Thieving XP**
(`^ntk_spear_xp = 100` ✓). Failure deals 1–4 damage. Direction per room is
already tabled in `~ntk_spear_dir`. *(to measure: the success endpoints — the
wiki charts no curve for the trap.)*

### 5.5 Tomb doors

- 4 per room, one correct, **same for every player**.
- The correct door **changes whenever any player enters room 1** — not on room
  entry, not per player.
- **Fourth-door rule:** after three failed *distinct* doors, the fourth always
  opens. (`ntk_right_door_opened` varbit 2383 is very likely this flag.)
- Room 8's four doors **exit the minigame**, like the exit doors — they do not
  print "this is the final room".
- Failing to pick has no penalty; the player simply retries.

### 5.6 Grand Gold Chest

1 per room. One roll at `130/220 − (room−1)`:

- **Success** → Thieving XP + artefact from the room table.
- **Failure** → **scarab swarm** spawns, **exactly 3 unavoidable damage**,
  **no Thieving XP**, but **the artefact is still given**.
- The **sceptre is rolled either way** (30 Aug 2023).
- **Rocky is rolled either way** (Mod Ash, 2 Apr 2017).

Per-room artefact table (exact, from the wiki drop tables):

| Room | Table |
|---|---|
| 1 | Stone seal 1/4 · Stone scarab 1/2 · Stone statuette 1/4 |
| 2 | Stone scarab 1/2 · Stone statuette 1/2 |
| 3 | Stone scarab 1/4 · Stone statuette 1/2 · Gold seal 1/4 |
| 4 | Stone statuette 1/2 · Gold seal 1/2 |
| 5 | Stone statuette 1/4 · Gold seal 1/2 · Golden scarab 1/4 |
| 6 | Gold seal 1/2 · Golden scarab 1/2 |
| 7 | Gold seal 1/4 · Golden scarab 1/2 · Golden statuette 1/4 |
| 8 | Golden scarab 1/2 · Golden statuette 1/2 |

### 5.7 Sarcophagus

**Two actions, not one** — the cache's three loc states exist for this:

1. **Open** (`ntk_sarcophagus`, 21255). Roll on **current Strength** with
   `low = 80 − roomLvl`, `high = 270 − roomLvl`. *Failure has no downside* —
   the player automatically retries after a few ticks (this is what the
   existing `while` loop models, and the unused `ntk_sarcophagus_push` varbit
   is very likely the "opening" animation state, loc 21257). On success:
   **Strength XP**, and a mummy spawns at **`room / 9`**.
2. **Search** (`ntk_sarcophagus_open`, 21256) → the artefact.

The sceptre is rolled **on the search regardless of success/failure**, but
**only if a mummy did not spawn**.

| Room | Table |
|---|---|
| 1 | Pottery scarab 1/4 · Pottery statuette 1/2 · Stone seal 1/4 |
| 2 | Pottery statuette 1/2 · Stone seal 1/2 |
| 3 | Pottery statuette 1/4 · Stone seal 1/2 · Stone scarab 1/4 |
| 4 | Stone seal 1/2 · Stone scarab 1/2 |
| 5 | Stone seal 1/4 · Stone scarab 1/2 · Stone statuette 1/4 |
| 6 | Stone scarab 1/2 · Stone statuette 1/2 |
| 7 | Stone scarab 1/4 · Stone statuette 1/2 · Gold seal 1/4 |
| 8 | Stone statuette 1/2 · Gold seal 1/2 |

Scarab swarm combat **does not interrupt** opening the sarcophagus (1 Mar 2023).

### 5.8 Urns

13 per room, 12 in room 3 — **measured, matches the map exactly**. Slot state
is keyed off the multiloc type, which works because each room draws 13 of the
15 types and state is reset on room entry.

Three flows:

- **Search** a closed urn: `100/180 − (room−1)`. Failure → snake bite, 1–4
  damage **and poison of the same amount**. The player may retry.
- **Check for Snakes**: `160/250 − (room−1)`. On success → *"You find a snake.
  It stares at you menacingly."*, urn → snake state. Then **Search** at
  `130/220 − (room−1)`.
- **Charm Snake** with `snake_flute`: **always succeeds** → charmed state,
  then Search.

> **XP invariant** (main-page footnote n1): *"Searching an urn grants as much
> experience as first checking and then searching one."* So the check+search
> split must **sum to the full room value**, not 66% of it. *(to measure: the
> split.)*

### 5.9 The sceptre

- Rolled from chest and sarcophagus independently, at the §5.3 rates,
  **regardless of whether the search succeeded**.
- On finding it the player is **immediately ejected**. If the inventory is
  full it is **dropped on the ground outside the pyramid**, under the player.
- Other players in the pyramid briefly see `ntk_mummy_guardian_dummy`
  ("Annoyed guardian mummy") saying, in order:
  - *"Grrr! Someone has found the Pharaoh's sceptre!"*
  - *"Right! That's it. I'm clearing all the urns and refilling them now."*

  The finder never sees him. Whether the urns are actually reset for everyone
  is *(to measure)*.

**Charges and teleports** (post-Beneath Cursed Sands): 3 charges by default,
10/25/50/100 with easy/medium/hard/elite Desert Diary. Worn options
Jalsavrah / Jaleustrophos / Jaldraocht / Jaltevas. Recharged by the guardian
mummy for **6 gold, 12 stone, or 24 pottery/ivory** artefacts (mixable within
a class, notes accepted). See §9 — this is a separate slice.

### 5.10 Rocky

`1 / (B − Lvl*25)` where `Lvl` is the Thieving level and `B` is the base:

| Room | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| B | 41355 | 29540 | 25847 | 20678 | 20678 | 20678 | 10339 | 6893 |

---

## 6. Defect list — what the current implementation gets wrong

Each row is a concrete change. Grouped by area, not by priority.

### Geometry
1. **`^ntk_guardian_room` points at an empty antechamber.** `2_30_69_48_4` =
   (2, 1968, 4420); the guardian spawns at (3, 1934, 4427). Finding the
   correct door currently drops you in the wrong room. `plunder.constant:29`
2. **Three wrong doors share one `^ntk_empty_room`.** There are four
   antechambers; three are empty. `plunder.constant:30`
3. `^ntk_room1..8` are unverified against the plane-0 layout. *(to measure)*

### Entrance
4. `%ntk_active_entrance` is a **per-player perm var picked once**
   (`~ntk_pick_entrance`, `plunder.rs2:219`). Must be **world-shared and
   rerolled every 2500 ticks**. §8 covers the engine problem.
5. Entrance search is `random(3) ! 0` with no level check, no damage, no stun,
   no failure message and no `ntk_pyramid_door_*_anim`. `plunder.rs2:246-269`
6. Wrong doors currently succeed silently; the correct-door 20 XP is granted
   with no roll.
7. `%ntk_tarik_spoken` gates the outside doors. **Not a real requirement** —
   Tarik is flavour. The real gate is Icthlarin's Little Helper + Thieving 21.
   `plunder.rs2:247`

### Levels and XP
8. Every gate reads boosted `stat(thieving)`. Room entry levels are
   **unboostable** → `stat_base`. `plunder.rs2:342, 588`
9. No Thieving 21 or Icthlarin gate on `~ntk_join`. `plunder.rs2:222`
10. `~ntk_chest_xp` = `urn_xp * 66 / 100` is an approximation that is wrong in
    rooms 4, 7, 8 (1419 vs 1400 · 4455 vs 4500 · 5445 vs 5500). Replace with
    the §5.3 table. `plunder.rs2:140`
11. `~ntk_door_xp` inherits that error. `plunder.rs2:143`

### Rolls
12. **Every success roll is invented.** Replace all of them with §4:
    - entrance `random(3)` → `160/250`
    - spear trap `random(5)=0` / `random(20)=0` → *(to measure)*
    - tomb door `random(3)`/`random(2)` → `130/220` or `160/250 − (room−1)`
    - chest `random(25)=0` → `130/220 − (room−1)`
    - sarcophagus `random(125) > stat(strength)` → `80−roomLvl / 270−roomLvl`
    - urn `random(stat(thieving)) <= room*4` → `100/180`, `130/220`, `160/250`
13. `~ntk_sceptre_chance` returns **1500/1350/1250/1150/1000** — 2009scape's
    pre-2021 numbers. Wiki: 4200/2800/1600/950/800/750/650/650.
    `plunder.rs2:150`
14. The sceptre is rolled **only on success**. Since 30 Aug 2023 it rolls on
    failure too. `plunder.rs2:488, 516`
15. `~ntk_roll_artifact` is a level-scaled formula that can drop **gold
    artefacts in room 1**. Replace with the three fixed per-room tables
    (§5.6, §5.7, and the urn table in §11). `plunder.rs2:165`

### Chest
16. Scarab spawn is `random(25)=0` rather than the failure branch of the
    Thieving roll. `plunder.rs2:511`
17. On scarabs the player currently gets **no artefact**. The wiki is explicit
    that the loot "is still obtained normally". `plunder.rs2:511-519`
18. Scarab spawn damage is `random(4)+1`; it is **exactly 3**.
    `plunder.rs2:513`
19. No Rocky roll.

### Sarcophagus
20. Open and Search are collapsed into one action; the cache's `_anim`
    (21257) middle state and `ntk_sarcophagus_push` varbit go unused.
    `plunder.rs2:465-491`
21. Mummy chance is `random(25)=0`; it is **`room / 9`**. `plunder.rs2:486`
22. `npc_add(coord, ntk_mummy_1, 500)` always spawns variant 1 of 5, with no
    aggression binding to the opener and no Bones drop. `plunder.rs2:487`
23. Sceptre-vs-mummy ordering is inverted: the sceptre should roll **only when
    no mummy spawned**, and the artefact should roll regardless.

### Tomb doors
24. `%ntk_correct_door` is per-player and re-randomised on every room entry
    (`~ntk_enter_room`, `plunder.rs2:206`). It must be **world-shared** and
    change **only when a player enters room 1**.
25. The **fourth-door rule** is not implemented. `ntk_right_door_opened`
    (varbit 2383) is unused and is very likely the flag.
26. Room 8 doors print "This is the final room"; they should **exit the
    minigame**. `plunder.rs2:423`

### Timer and overlay
27. `^ntk_timer_poll = 5` makes the bar move in 3-second steps and lets
    expiry land up to 5 ticks late. Poll every tick.
    `plunder.constant:6`, `plunder.rs2:232-242`
28. Leaving by teleport (not through a door) leaves the softtimer running and
    the overlay open. Needs a walk/teleport/logout hook — the tree already has
    the logout-trigger seam (see `logout-trigger-and-instance-lifetime`).
29. Overlay repaint is unverified (§3.5, varp 821 vs `varptriggers=822`).

### Urns
30. Charmed search grants 66% of the urn XP. The invariant is that
    check+search **sums to** the plain-search value. `plunder.rs2:579`
31. Check for Snakes **always succeeds and always finds a snake**; it needs
    the `160/250` roll and the *"You find a snake…"* message.
    `plunder.rs2:633`
32. Snake bite poison is a flat `queue(poison_player, 0, 2)`; it should be the
    **same amount as the damage** (1–4). `plunder.rs2:619`
33. `~ntk_urn_slot` returns 0 for `ntk_urn_rough_multi`. That is *correct* —
    those 8 placements are antechamber decoration on planes 2/3 — but the
    handler silently no-ops instead of `~displaymessage(^dm_default)`.

### Content
34. Guardian mummy dialogue is invented; the wiki has a full transcript
    including the minigame explanation and the sceptre-charging branch.
    `plunder.rs2:309-319`
35. No annoyed-mummy broadcast (§5.8); `ntk_mummy_guardian_dummy` unused.
36. Sceptre inventory-full handling missing (`inv_add` with no check) —
    should drop outside. `plunder.rs2:197`
37. No sceptre teleports, charges or recharging (§9).
38. No Simon Templeton artefact sale (§9).

---

## 7. Implementation phases

Each phase compiles and is separately testable. Phase 0 first — three of its
findings invalidate work in later phases if they land late.

| # | Phase | Contents | Blocks |
|---|---|---|---|
| **0** | **Measure** | Fix §6.1–3 coords by dumping `m30_69.jl2` per room; resolve the §3.5 overlay varp question; decide `stat` vs `stat_base` per roll; settle the urn artefact table (§11) | everything |
| **1** | Rolls and tables | `~ntk_success_chance` helper (§4), the four XP tables, three artefact tables, sceptre rates. §6.8–15 | 2–5 |
| **2** | Entrance | World-shared 25-min rotation, `160/250` roll, punch anim + stun + damage, 4 antechambers, drop the Tarik gate. §6.1–7 | — |
| **3** | Room objects | Chest failure branch + Rocky; sarcophagus split into Open/Search with the mummy at `room/9` and the 5 variants; urn three-flow with correct rolls, messages and the XP invariant; spear trap. §6.16–23, 30–33 | — |
| **4** | Tomb doors | World-shared correct door, changes on room-1 entry, fourth-door rule, room 8 exits. §6.24–26 | engine §8 |
| **5** | Timer and overlay | Per-tick poll, teleport/logout teardown, overlay repaint. §6.27–29 | — |
| **6** | Dialogue and broadcast | Guardian transcript, annoyed-mummy overhead, sceptre inventory-full drop. §6.34–36 | — |
| **7** | Sceptre lifecycle | Charges, four teleports, recharging (6/12/24), Desert Diary charge caps. §6.37 | §9 |
| **8** | Simon Templeton | Artefact sale, noted after Easy Desert Diary. §6.38 | §9 |

Phases 1, 3, 5 and 6 are pure content and can proceed immediately. Phases 2
and 4 need the §8 decision first.

---

## 8. Engine gaps

### 8.1 World-shared state — the one real blocker

Two mechanics are **world state, not player state**:

- the correct anonymous door (rotates every 25 min, same for everyone)
- the correct tomb door per room (same for everyone, changes when anyone
  enters room 1)

The tree has **no shared-var facility in use**. The compiler *does* support
`vars`:

```
src/serverscript/ssc_symbols.c:529   { "vars",  SSC_SYM_VARS }
src/serverscript/ssc_compile.c:395   { SSC_SYM_VARS, SS_OP_PUSH_VARS, SS_OP_POP_VARS }
```

but **nothing in `src/net/mock/` implements opcodes 11/12** — the only other
references are `ss_opcode.h` and `ss_verify_test.c`. There is no `.vars`
config file anywhere in `OSRS-Content/`. So `%vars` compiles and would fault
or no-op at runtime.

Two options:

- **(a) Derive from `map_clock`.** The 25-minute entrance rotation needs no
  mutable state at all: `door = (map_clock / 2500) % 4`, evaluated fresh on
  every search. `map_clock` is already used this way in
  [sewerpipe.rs2:45](../../OSRS-Content/osrs239-content/server/scripts/quests/quest_elena/scripts/sewerpipe.rs2#L45).
  This solves the entrance **completely and for free**. It does *not* solve
  the tomb doors, whose reroll is event-driven.
- **(b) Implement `vars`.** Add a `.vars` config kind, the alloc file, and VM
  handlers for opcodes 11/12. This is the honest fix and unblocks every future
  minigame that needs world state.

**Recommendation: (a) now for the entrance, (b) as its own engine slice for
the doors.** Until (b) lands, the tomb door can stay per-player — it is
invisible to a single player and the fourth-door rule still works. Ship the
divergence documented rather than blocking phase 4 behind an engine change.

### 8.2 Scoped aggression

Both the mummy and the scarab swarm are aggressive **only to the player who
spawned them**, and the swarm despawns once that player leaves. The tree has
`npc_add` and the NPC-pursuit machinery (see `npc-pursuit-and-aggro`), but
per-spawner aggression scoping is *(to measure)* — check whether
`npc_setmode(opplayer2)` against a stored uid is enough.

### 8.3 Missing, cheap

- Mummy `attackrate` (4) and max-hit (6) params, and the Bones drop table —
  `configs/all.npc:209850+` has neither. Per `npc-content-config-gotchas`,
  hitpoints/stats have to come from an authored `.npc` block.
- Rocky does not exist as an obj (§3.3) and neither do the pet-insurance /
  follower hooks. Phase 3's Rocky roll can be written and left inert behind a
  constant until the pet lands.

---

## 9. Out of scope for the minigame slice

These are real gaps but belong to their own slices; the minigame is playable
and accurate without them:

- **Sceptre charges + 4 teleports + recharging** (phase 7). Needs the charged-item
  seam in `general/scripts/charges/`, Desert Diary tiers, and the
  Jaltevas obelisk commune.
- **Simon Templeton** (phase 8) — no `npc.server` binding today.
- **Rocky** — no obj, no pet system hook.
- **Hard clue** "The King's magic shouldn't be wasted by me" → puzzle box.
- **Music track** "Tomb Raider" region unlock.
- **Serpentine helm consuming 10 Zulrah's scales** per swarm.

---

## 10. Verification

**Compile** (do not use the shared build dirs — see
`embed-binary-build-isolation`):

```
make -C src sscompile PLATFORM_OBJ_BASE=<scratch>
# then the full invocation from src/makefile's `mock230-scripts` target,
# with --out and the summoning-constants --out pointed at <scratch>
```

A clean run compiles ~19,400 scripts and prints `compiled N scripts`, exit 0.

**Debugprocs.** Three exist (`::ntkplunder`, `::ntkroom`, `::ntkcharm`) and
should be extended, not replaced:

| Proc | Add |
|---|---|
| `::ntkplunder` | print the `map_clock`-derived correct entrance so the rotation is observable |
| `::ntkroom <n>` | take a room argument; currently hardcodes room 1 |
| `::ntkcharm` | already fine |
| `::ntkrates <room>` | **new** — print all six `chance256` values for the current level and room, so §4 can be checked without grinding |

**Per-defect tests.** Each §6 row needs an assertion that can fail. Per
`verify-blocker-and-failing-test`: after writing a test, mutate the
implementation to prove the assertion actually fires. Specifically —

- The XP tables: assert `~ntk_chest_xp` for all 8 rooms against §5.3. The
  current 66% formula must fail rooms 4, 7 and 8.
- The artefact tables: assert room 1 can **never** produce a gold artefact.
  The current `~ntk_roll_artifact` must fail this.
- The sceptre rate: assert `~ntk_sceptre_chance(1) = 4200`.
- Geometry: assert `^ntk_guardian_room` is within 5 tiles of the
  `ntk_mummy_guardian` spawn. The current constant must fail.
- Urn slots: assert all 13 room-1 urn types map to distinct non-zero slots.

**Headless.** The overlay repaint (§3.5) needs the layout-remount probe —
force clientscript 3998, `grep -a` the logs (`headless-layout-remount-probe`).

---

## 11. Open measurement questions

| # | Question | How to settle |
|---|---|---|
| 1 | **The urn artefact table.** The wiki marks it `{{Incomplete}}`. It *can* be bounded: the main page's "Rooms" column gives each artefact's room range, and the chest and sarcophagus tables are exact — so the urn table is the residual. **Ivory comb appears in rooms 1–2 and is in neither the chest nor the sarcophagus table**, which proves urns have their own pool. Derive the residual, document the derivation, and mark it provisional. |
| 2 | Charmed-urn Search endpoints (§4) — not charted anywhere. | Bound it: must be better than the `130/220` uncharmed case, and the check+search XP invariant constrains the expected attempt count. |
| 3 | The check/search XP split (§5.8) that sums to the plain-search value. | Same invariant. |
| 4 | Spear trap success endpoints. | Not on the wiki. Take 2009scape's, flag it. |
| 5 | Overlay varp 821 vs `varptriggers=822` (§3.5). | `tools/dump_interface` on 428, disassemble clientscripts 971 and 477. |
| 6 | `^ntk_room1..8` correctness. | Dump plane 0 of `m30_69.jl2` per room and check each against its `ntk_tomb_door_exit`. |
| 7 | Whether the annoyed mummy actually clears everyone's urns, or is flavour. | Unresolvable from the wiki; treat as flavour. |
| 8 | `stat` vs `stat_base` per roll (only the sarcophagus is documented). | Follow the tree's obstacle convention (`stat_base`) and note the assumption. |
| 9 | Per-spawner aggression scoping (§8.2). | Read `npc-pursuit-and-aggro` and the `npc_setmode` surface. |

---

## 12. Summary

The cache ships everything: 96 locs, 8 NPCs, 9 artefacts + 9 sceptre charge
states, 32 varbits, 2 interfaces, and a map whose 103 urn placements match the
wiki pin-for-pin. The server content exists and has the right *shape*.

What is missing is **accuracy**. Thirty-eight concrete defects, of which the
large majority are numbers imported from a 2009-era reference implementation
whose values Jagex changed in March 2021, March 2023 and August 2023. The
single genuine engine blocker is world-shared state (§8.1), and half of it —
the entrance rotation — dissolves into a `map_clock` expression.

Phases 1, 3, 5 and 6 are pure content edits to one 729-line file and cover
most of the defect list.

---

## 13. What shipped (2026-08-17)

### 13.1 The engine gap was real, and it is closed

§8.1 called world-shared state the one genuine blocker and was right about the
diagnosis and wrong about the remedy — it recommended deriving the entrance
from `map_clock` and deferring the doors. Both halves are now implemented
properly instead, because the engine change turned out to be small:

| Layer | Change |
|---|---|
| `src/net/mock/mock230.h` | `MOCK230_VARS_COUNT` (256) and `Mock230Server.vars[]` |
| `src/net/mock/mock230_scripts.c` | `SS_OP_PUSH_VARS` / `SS_OP_POP_VARS` handlers |
| `src/content/content_register.c` | a `vars` row, `shared_var_domain = 1`, base 0 |
| `tools/ss_allocate.py` | `vars` added to `SERVER_NAMESPACES` |
| `OSRS-Content/.../pack/vars.alloc` | the ledger, created by the allocator |

The compiler already knew `vars` — `SSC_SYM_VARS`, `ssc_compile.c:395` — so
`%name = 1` had been producing legal bytecode that fell through to the
unhandled-opcode abort for as long as the namespace existed. Nothing in the
tree used it, so nothing had noticed.

**The pure-`map_clock` idea in §8.1(a) is a trap** and was rejected during
implementation: a rotation that is a pure function of the world clock is
*predictable*, so every world would open its north door in the same minute of
its life. The shipped version stores the roll **and** the epoch it belongs to
(`ntk_shared_entrance`, `ntk_shared_entrance_epoch`), which keeps it random,
shared, and on a 25-minute boundary at once.

The tomb doors did not have to be deferred: `ntk_shared_door1..8` are rerolled
by `~ntk_reroll_shared_doors` on any player's entry to room 1, exactly as the
wiki describes.

### 13.2 Three things the plan got wrong

- **The overlay was never broken.** §3.5 flagged `varptriggers=822` against a
  timer count on varp 821 as a probable dead repaint. Disassembling
  clientscripts 971/477/480/481/482 shows **both** varps are wired: 971
  registers `ntk_timer_resynch(...){var821}` on the bar, and `varptriggers=822`
  feeds the *text*. The bar even interpolates locally (`clientclock - t0)/30`,
  one step per game tick) between transmits. No change was needed, and the
  planned "fix" would have been damage.
- **The room coords were already right.** §6.3 listed `^ntk_room1..8` as
  unverified. Clustering the 103 urn / 32 door / 8 chest / 8 sarcophagus
  placements into eight connected components puts every one of the eight
  constants inside its own room's bounding box. Only `^ntk_guardian_room` was
  wrong.
- **`ntk_urn_rough_*` is settled, not open.** §11 row 1 wondered what those 8
  placements were. Its multiloc keys off **`burgh_inn_colapsed_wall`** — the
  *In Aid of the Myreque* rubble varbit — which is the wiki's trivia note about
  the lobby sarcophagus opening after that quest. Decoration. Not wired.

### 13.3 The urn table, derived rather than guessed

§11 row 1 asked for a derivation and it closes tighter than expected. Subtract
the chest and sarcophagus tables (both exact) from the main article's "Rooms"
column and **every artefact range is accounted for except the ivory comb
(rooms 1–2)** — which is the proof that urns have a pool of their own. The urn
drop list also names no pottery at all.

So the urn table is the same 1/4–1/2–1/4 sliding window the other two provably
use, on the pottery-free seven-rung ladder starting at the ivory comb, anchored
so the ladder's ends match the two ranges the article states exactly. Result:
**ivory comb 1–2, golden scarab 5–8 and golden statuette 7–8 land exactly**;
the other four are proper subsets; every item in the wiki's urn list is
reachable. Provisional, but constrained on five sides.

### 13.4 Files

| File | |
|---|---|
| `.../minigame_pyramidplunder/scripts/plunder.rs2` | rewritten, 729 → ~1250 lines |
| `.../configs/plunder.constant` | rewritten — roll endpoints, measured geometry, multiloc states |
| `.../configs/plunder.varp` | `ntk_doors_tried`, `ntk_sarc_looted` added |
| `.../configs/plunder.vars` | **new** — 10 world-shared vars |
| `.../configs/plunder.npc` | **new** — mummy ×5 and scarab swarm combat blocks |
| `.../scripts/plunder_selftest.rs2` | **new** — 9 assertion procs |
| `src/net/mock/mock230_world.c` | selftest hookup for `vars` and for plunder |
| `OSRS-Content/.../selftest.rs2` | `selftest_vars_write` / `_read` |

### 13.5 Defect list status

Of the 38 defects in §6, **35 are fixed**. The three that are not:

- **§6.19 Rocky** — the pet does not exist as an obj anywhere in the tree
  (`grep -i rocky configs/all.obj.compack` is empty), so the roll would be a
  call to nothing. Out of scope, as §9 already said.
- **§6.37 sceptre charges/teleports** and **§6.38 Simon Templeton** — phases 7
  and 8, deliberately their own slices.

One defect was found *during* implementation and is not in the §6 list: the
sarcophagus's sceptre roll has to live on the **Open**, not the Search, because
"the pharaoh's sceptre only attempts to drop if a mummy does not spawn" and the
mummy is decided on the Open. Putting it on the Search — the obvious reading of
"upon searching the sarcophagus" — makes the two wiki sentences unsatisfiable.

### 13.6 Verification

`mock230 --selftest` now runs ten new assertions. All pass, and **all were
shown to fail** by mutating the implementation back to its previous values
(the discipline in `verify-blocker-and-failing-test`):

| Mutation | Caught by |
|---|---|
| `vars` stored per-player instead of per-world | `a SECOND player sees the shared vars write, got 0` |
| chest XP back to `urn_xp * 66 / 100` | XP tables (10 failures) |
| sceptre room 1 back to 1500 | sceptre rates (1 failure) |
| a gold rung in the room-1 chest window | room 1 pays no gold (56 failures) |
| charmed-search XP back to 66% | check + search invariant (8 failures) |
| `^ntk_guardian_room` back to `2_30_69_48_4` | guardian room (1034 tiles, wrong plane) |

The `vars` mutation is the sharpest of these: storing per-player still passes
the *same-player* read-back and fails only the cross-player check, which is why
that test needs two players to mean anything.

Content compiles clean — **25,275 scripts, zero errors across the whole tree**
against HEAD `307bd64e` — and `plunder.npc` loads with zero content errors.

One caveat worth recording, because it wasted more time than the feature did:
`sscompile` **must be rebuilt immediately before each verification run**. This
tree's engine gains ServerScript opcodes regularly, and content is committed
that uses them; a compiler binary even an hour old rejects HEAD's content with
`'x' is not a command`, and every such rejection then cascades as `no proc
named ...` through whatever quarantines broken files — which reads exactly like
a broken content tree and is not one. Two separate multi-hour detours in this
session came from that, both of them chasing damage that did not exist.

---

## 14. Still unverified

- **The sarcophagus "impossible" levels are off by two.** The wiki says
  Strength 1–7 cannot open room 8; the shipped curve makes 1–5 impossible and
  gives 6–7 a 1–2/256 chance. The single interpolation helper is verified
  *exactly* against five charted Thieving percentages, so it was kept rather
  than forked for one prose sentence — but the divergence is real and is at the
  level nobody plays.
- **The spear-trap curve is still a stand-in** (§11 row 4). It uses the tomb
  door's endpoints, stated once in `plunder.constant` so a measurement replaces
  it in one place.
- **The charmed-urn search curve** reuses the Check-for-Snakes endpoints
  (§11 row 2). It has to beat the uncharmed rate and has to stop being worth it
  around level 51; that is the constraint it was chosen against, not a measurement.
- **Per-spawner aggression** is `npc_setmode(opplayer2)` on the tick of the
  spawn, which names the right player only because the opener *is* the active
  player at that instant. A second player attacking the same mummy has not been
  tested.
- **Nothing has been driven end-to-end in a live client.** The assertions cover
  the tables, the curve, the slot scheme, the shared vars and the geometry;
  they do not cover the feel of a run.
