# TzHaar Fight Cave — inventory and completion plan

Every npc, instance, mechanic, cycle and reward the Fight Caves needs, sourced
from the Old School RuneScape Wiki and cross-checked against this tree's
`cache.osrs239` config data, followed by what is already implemented and what
is left.

Two kinds of statement appear below and they are kept apart on purpose:

- **[W*n*]** — stated by the wiki. The numbered reference list is §8.
- **[C]** — read out of this tree's cache (`OSRS-Content/osrs239-content/configs/`)
  or its content tree. The wiki publishes no npc record fields, sequence ids or
  recolour tables, so everything of that kind here is measured, not cited.

Anything neither source settles is called out as **unstated** rather than
guessed. There are three, and they are §7.

TzTok-Jad and Yt-HurKot are **not** restated here. `docs/bosses/jad.md` §1 is
their record, attack cycle, animation table and healer behaviour, and it is the
citation for every Jad statement below.

---

## 1. The encounter in one paragraph

A single player enters a cave under the Karamja volcano and fights 63 waves of
TzHaar creatures, alone, with no item loss on death [W1]. Waves 1–62 draw from
five monster types in a fixed arithmetic pattern; wave 63 is TzTok-Jad and its
four healers. Completing it awards the fire cape and 8,032 Tokkul [W1].

---

## 2. NPCs

Cache names are the `[gameval]` keys in `OSRS-Content/osrs239-content/configs/all.npc`.
The cache's stat slots map `stat1`=attack, `stat2`=defence, `stat3`=strength,
`stat4`=hitpoints, `stat5`=ranged, `stat6`=magic [C] — established by
`[tzhaar_fightcave_swarm_boss]`, whose six values are the six the wiki publishes
for Jad in that order (`docs/bosses/jad.md` §1.1).

### 2.1 The wave monsters

| Cache name | Wiki | npc ids | Lvl | HP | Size | Style | Max hit | Speed |
|---|---|---|---|---|---|---|---|---|
| `tzhaar_fightcave_swarm_1a` `_1b` | [Tz-Kih][W5] | 2189, 2190, 3116, 3117 | 22 | 10 | 1×1 | Stab | 4 | 4t |
| `tzhaar_fightcave_swarm_2a` `_2b` | [Tz-Kek][W6] | 2191, 2192, 3118, 3119 | 45 | 20 | 2×2 | Crush | 7 | 4t |
| `tzhaar_fightcave_swarm_2spawn` | Tz-Kek (22) [W6] | 3120 | 22 | 10 | 1×1 | Crush | — | 4t |
| `tzhaar_fightcave_swarm_3a` `_3b` | [Tok-Xil][W7] | 2193, 3121 | 90 | 40 | 3×3 | Ranged, Crush | 14 rng / 13 melee | 4t |
| `tzhaar_fightcave_swarm_4a` `_4b` | [Yt-MejKot][W8] | 3123, 3124 | 180 | 80 | 4×4 | Crush | 25 | 4t |
| `tzhaar_fightcave_swarm_5a` `_5b` | [Ket-Zek][W9] | 3125, 3126 | 360 | 160 | 5×5 | Magic, Stab | 52 magic / 55 melee | 4t |
| `tzhaar_fightcave_swarm_boss` | [TzTok-Jad][W10] | 3127 | 702 | 250 | 5×5 | Stab, Magic, Ranged | 97 / 97 / 95 | 8t, melee 4t |
| `tzhaar_fightcave_swarm_boss_cleric` | [Yt-HurKot][W11] | 3128 | 108 | 60 | 1×1 | Crush | 14 | 4t |

Cache cross-check [C]: every hitpoints figure above is the record's `stat4`
(10 / 20 / 10 / 40 / 80 / 160), every size is the record's `size=` field
(absent = 1), and every combat level is `vislevel=`. All eight agree with the
wiki exactly. No wave monster's record needed correcting.

### 2.2 The mechanics, one per monster

**Tz-Kih — prayer drain.** "Each hit it inflicts on a player will drain the
player's Prayer points, **whether it inflicts damage or not**" [W5]. The
emphasis is the whole mechanic: a 0 roll still costs prayer, so a handler that
drains inside a damage branch is wrong. The wiki does not publish the points
drained per hit — **unstated hole #1**, §7.

**Tz-Kek — recoil, and the split.** "If attacked with melee, it will reflect 1
point of damage (regardless of the damage inflicted and distance) back at
whoever damaged it" [W6]. Flat 1, melee only, independent of distance. "Upon
death, it splits into two level 22 Tz-Keks, which are weaker and **lack the
recoil mechanic**" [W6] — `_2spawn` must not reflect.

**Tok-Xil — ranged spines.** Fires spines at the player from up to **15 tiles**
[W7], and crushes at melee distance for a lower max hit. Two attack sequences
exist in the cache for exactly this, `magmaquris_punch` and
`magmaquris_spine_attack` [C].

**Yt-MejKot — the heal.** "Restore up to 10 hitpoints to itself or any adjacent
monster whose health has dropped below half" [W8]. Three conditions the wiki
states and all three matter: it "can only heal when it's standing on a tile
adjacent the player", "only after 4 game ticks have passed since its last
attack", and "healing replaces an attack, and once it does so, the lizard cannot
attack again for another 4 ticks" [W8]. It is not a free action on a separate
clock — it is a swing spent on healing instead. The trivia's "they seem to heal
themselves every two attacks" is the observable consequence [W8].

**Ket-Zek — the fireball.** Launches a fireball from its tail with a 15-tile
range [W9], and stabs at melee distance. As with Tok-Xil the cache carries two
attack sequences, `igniferum_attack` and `igniferum_ranged` [C].

**TzTok-Jad and Yt-HurKot** — `docs/bosses/jad.md` §1.2–§1.6.

### 2.3 The `a` / `b` pairs — what the second id is for

Each wave monster has two cache records. The second is the **off-coloured**
variant the wiki uses as a tell: "TzTok-Jad will spawn exactly where the orange
Ket-Zek spawns in the next round" [W1].

Measured [C], counting recolour rows per record:

| Pair | `a` recolours | `b` recolours |
|---|---|---|
| Tz-Kih `_1a` / `_1b` | 0 | **0** |
| Tz-Kek `_2a` / `_2b` | 10 | **10, identical to `_2a`** |
| Tok-Xil `_3a` / `_3b` | 0 | **6** |
| Yt-MejKot `_4a` / `_4b` | 0 | **2** |
| Ket-Zek `_5a` / `_5b` | 0 | **12** |

Only the top three tiers carry a visible difference, and those are exactly the
three waves that field a doubled pair of that tier — 14 (two Tok-Xil), 30 (two
Yt-MejKot) and 62 (two Ket-Zek). Wave 6's two Tz-Keks have no off-colour because
the cache ships none. The existing wave table already special-cases 14 / 30 / 62
and nothing else, which is right; **this is settled and needs no work**.

### 2.4 The attendant

`tzhaar_fightcave_master` — [TzHaar-Mej-Jal][W12], npc **2180**. Guards the cave
and "owns all the monsters within, even TzTok-Jad" [W12]. Two options, "Talk-to"
and "Exchange fire cape": he buys a fire cape for 8,000 Tokkul, or the player may
trade it for a 1/200 chance at the pet instead [W12].

---

## 3. Assets — all present, nothing to author [C]

Every sequence and graphic the five wave monsters need is already in the cache.

| Family | Members | Owner |
|---|---|---|
| `firebat_*` | `ready` `walk` `attack` `defend` `death` | Tz-Kih |
| `lavabeast_*` | `ready` `walk` `attack` `defend` `death` | Tz-Kek |
| `magmaquris_*` | `ready` `walk` **`punch`** **`spine_attack`** `defend` `death` | Tok-Xil |
| `lizard_cleric_*` | the above plus **`heal`**, **`heal_spot`** | Yt-MejKot, Yt-HurKot |
| `igniferum_*` | `ready` `walk` **`attack`** **`ranged`** `defend` `death`, `fire_launch_travel`, `fire_launch_impact` | Ket-Zek |
| `lordmagmus_*` | see `docs/bosses/jad.md` §1.4 | TzTok-Jad |

Note what the two-attack families give away: `magmaquris` and `igniferum` each
ship a melee sequence *and* a projectile sequence. The cache has always
described a ranger and a mager; only the content was missing.

Spotanims [C]: `tzhaar_spine_attack` (Tok-Xil's thrown spine),
`tzhaar_fire_launch_travel` / `tzhaar_fire_launch_impact` /
`tzhaar_ranged_fire_attack` (Ket-Zek's fireball), `tzhaar_heal` (the heal
graphic, already in use by the Yt-HurKot).

Sounds, from `docs/INFERNO_SOUNDS.md` §6 — attack / defend / death: Tz-Kih
291/296/295, Tz-Kek 595/597/596, Tok-Xil 598/600/599, Yt-MejKot 608/610/609,
Ket-Zek none recorded.

Locs [C]: `tzhaar_fightcave_wall_entrance` (op1 *Enter*),
`tzhaar_fightcave_wall_exit` (op1 *Leave*, op2 *Escape*).

Items [C]: `tzhaar_cape_fire` (**6570** [W13]), `tzhaar_token` (**6529** [W14]),
`jad_pet` — TzRek-Jad, already present in `all.obj` and unused by any script.

---

## 4. The arena and the instance

Region **9551**, map square 37,79, base 2368,5056 [C]. The five spawn points the
wiki names are **C, NW, SE, S, SW** [W3]; this tree has them as
`^fightcave_point_*` in `configs/fightcave.constant`.

The cave is **single-player**: one player, alone, for the whole run [W1]. This
tree currently runs it on the shared map, so two concurrent players share one
arena and one set of npcs. The instancing surface needed to fix that already
exists and has five working users (Inferno, Pest Control, Gauntlet, QBD, Temple
Trekking):

```
~map_instance_from_square($square)(int)   general/scripts/misc/map_instance_procs.rs2:60
map_instance_coord(handle, lx, lz, level)
map_instance_find(coord)
map_instance_free(handle)
```

The pool is **8 concurrent instances** (`src/torirsserver/torirs_server_mapinstance.h:58`),
so every exit path must free or the ninth entrant is refused.

---

## 5. Cycles

### 5.1 The wave table

63 waves [W1]. The composition of wave *N* is a **greedy decomposition of N**
over the monster weights

| Monster | Weight |
|---|---|
| Ket-Zek | 31 |
| Yt-MejKot | 15 |
| Tok-Xil | 7 |
| Tz-Kek | 3 |
| Tz-Kih | 1 |

taking the largest weight ≤ the remainder, repeatedly. Wave 29 = 15+7+3+3+1
(Yt-MejKot, Tok-Xil, 2 Tz-Kek, Tz-Kih); wave 61 = 31+15+7+3+3+1+1. Waves 14, 30
and 62 field the doubled pair of a tier and use the off-coloured second record
(§2.3). Wave 63 is Jad.

**This was checked row by row against all 63 published rows** [W4] and the
implementation at `scripts/fightcave.rs2:65-85` reproduces every one. It is
correct and needs no change.

### 5.2 The spawn cycle — and what the 15 rotations actually are

The wiki gives a fifteen-entry cycle of spawn points, repeating indefinitely
[W1]:

```
 index   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14
 point  SE  SW   C  NW  SW  SE   S  NW   C  SE  SW   S  NW   C   S
```

and separately a table of 15 "rotations", each identified by which points waves
1–4 use [W3]. It does not say how the two relate. **They relate exactly, and the
derivation is worth stating because it collapses the whole feature to one
integer.**

Take the rule that wave *N* begins at cycle index `start + N − 1`, with the
monsters inside a wave taking consecutive entries from there, highest level
first [W1]. Solving each published rotation for its `start`:

| Rotation | W1 | W2 | W3 | W4 | `start` |
|---|---|---|---|---|---|
| 1 | C | NW, SW | SW | | **2** |
| 2 | C | S, SE | SE | | **13** |
| 3 | C | SE, SW | SW | | **8** |
| 4 | NW | C, S | S | | **12** |
| 5 | NW | C, SE | SE | | **7** |
| 6 | NW | SW, SE | SE | | **3** |
| 7 | S | NW, C | C | S, SE | **11** |
| 8 | S | NW, C | C | SE, SW | **6** |
| 9 | S | SE, SW | SW | | **14** |
| 10 | SE | S, NW | NW | | **5** |
| 11 | SE | SW, C | C | | **0** |
| 12 | SE | SW, S | S | | **9** |
| 13 | SW | C, NW | NW | | **1** |
| 14 | SW | S, NW | NW | | **10** |
| 15 | SW | SE, S | S | | **4** |

Every rotation solves, and the fifteen answers are **0–14, each used exactly
once**. The 15 rotations are the 15 starting offsets into the one cycle — there
is no second table to port.

Two consequences fall straight out, and both are things the wiki states as
separate facts:

- **Jad spawns where wave 3's monster spawned** [W1] [W3]. Wave 63 begins at
  `start + 62`, wave 3 at `start + 2`, and `62 ≡ 2 (mod 15)`. It is not a rule
  to implement; it is arithmetic, and it will hold for free once the indexing is
  right.
- **Jad spawns where the off-coloured Ket-Zek stood on wave 62** [W1]. Wave 62
  begins at `start + 61 ≡ start + 1`, and its two Ket-Zeks take `start + 1` and
  `start + 2` — the second of which is Jad's tile. Same identity, seen from one
  wave earlier.

Rotations 7 and 8 share waves 1–3 and separate only at wave 4, which is why the
wiki says two rotations need four waves to identify [W3]; with `start` 11 and 6
they are distinct from the first tile onwards internally.

**Rotation selection** is by wall-clock time of entry: "The rotation that the
player will do changes exactly at every minute, except for Rotation 4, which
lasts for two minutes before changing" [W3]. Which minute maps to which rotation
is **unstated hole #2**, §7.

**What this tree does today is not this.** `~fightcave_next_spawn` advances one
entry per *monster spawned* and never re-bases per wave, so the index at wave 3
is a running total of monsters (1 + 2 = 3 → `NW`) rather than `start + 2`. It
also always starts at 0, i.e. rotation 11, forever.

---

## 6. Rewards and integrations

| Reward | Value | Source |
|---|---|---|
| Tokkul on leaving | **N·(N+1)**, N = waves completed | [W1] |
| Tokkul for killing Jad | **+4,000** on top | [W1] |
| — the total that implies | 63·64 + 4000 = **8,032** ✔ matches the published total | [W1] [W10] |
| Fire cape | `tzhaar_cape_fire` 6570, untradeable; +1 attack all styles, +11 defence all, +4 strength, +2 prayer | [W13] |
| TzRek-Jad pet | **1/200**, or **1/100** on a TzHaar slayer task | [W10] |
| Fire cape exchange | 8,000 Tokkul from TzHaar-Mej-Jal, **or** a 1/200 pet roll instead | [W12] |
| Slayer XP | **11,520** for waves 1–62, **25,250** for Jad, **60** per Yt-HurKot | [W1] |
| Karamja **elite** diary | Tokkul **doubled** (16,064); one resurrection per day inside the cave | [W1] [W15] |
| Combat achievements | 10 tasks, 50 points | [W16] |

The `N·(N+1)` formula and the flat 4,000 are stated separately by [W1]; the fact
that they sum to the published 8,032 at N = 63 is the arithmetic check that both
were read correctly, and is why neither is guessed.

**The 11,520 is not a reward — it is a consequence, and checking that is what
proves three other things at once.** Slayer xp in this tree is
`npc_basestat(hitpoints)` (`skill_slayer/scripts/slayer_kill.rs2`). Counting
what the wave table stands up over waves 1-62 gives 33 Ket-Zek, 34 Yt-MejKot,
36 Tok-Xil, 40 Tz-Kek and 48 Tz-Kih, plus the two level-22 halves each Tz-Kek
splits into:

| | Count | HP | Total |
|---|---|---|---|
| Ket-Zek | 33 | 160 | 5,280 |
| Yt-MejKot | 34 | 80 | 2,720 |
| Tok-Xil | 36 | 40 | 1,440 |
| Tz-Kek (45) | 40 | 20 | 800 |
| Tz-Kek (22), from splits | 80 | 10 | 800 |
| Tz-Kih | 48 | 10 | 480 |
| | | | **11,520** |

Exactly the wiki's figure [W1], with **nothing awarded for the waves at all**.
Getting there requires the wave table, the split, and the hitpoints-as-xp rule
all to be right simultaneously, so the agreement is a check on all three rather
than a reason to add a fourth number. Jad is the one genuine override: 250
hitpoints against a published 25,250.

**Session rules** [W1] [W2]:

- **Safe death** — no items are lost, "including those stored inside an Item
  Retrieval Service (deathbank)".
- **Logout is a request, not an action.** "If the player clicks log out while in
  the cave, they will not be logged out immediately. Instead, the wave following
  the current wave will be prevented from spawning, giving the player the
  opportunity to log out without loss of progress" [W2]. A second click logs out
  immediately at the cost of repeating the current wave.
- **No re-entry cooldown.**

**What the tree already has to hook into**: `~diary_tier_complete_flag(^diary_area_karamja,
^diary_tier_elite)` at `interface_diaries/scripts/diaries.rs2:241`; the slayer
kill path `~slayer_on_npc_kill` at `skill_slayer/scripts/slayer_kill.rs2:79`;
`~inferno_pet_roll` at `minigame_inferno/scripts/inferno.rs2:142` as the pet
pattern including its full-inventory fallback. **There is no combat-achievement
subsystem** — `combat_achieve` matches nothing in the tree.

---

## 7. Unstated — what neither source settles

1. **Tz-Kih's prayer drain amount.** [W5] states that every hit drains prayer
   and that a 0-damage hit still does; it gives no number of points. Neither
   does the Fight Cave page. Any figure implemented here is a choice, and should
   be a named constant carrying that admission rather than a bare literal.
2. **Which minute maps to which rotation.** [W3] states the cadence — a change
   every minute, rotation 4 lasting two — and that entry time decides it, but
   publishes no mapping from a clock reading to a rotation number. The cadence
   and the set of fifteen are reproducible; the assignment is not. Deriving the
   offset from `map_clock` reproduces everything the wiki actually claims.
3. **Yt-MejKot's heal target priority.** [W8] says "itself or any adjacent
   monster … below half"; with two eligible neighbours it does not say which.

---

## 8. Reference list

| # | Page | URL |
|---|---|---|
| W1 | TzHaar Fight Cave | https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave |
| W2 | TzHaar Fight Cave/Strategies | https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave/Strategies |
| W3 | TzHaar Fight Cave/Rotations | https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave/Rotations |
| W4 | Template:TzHaar Fight Cave Waves | https://oldschool.runescape.wiki/w/Template:TzHaar_Fight_Cave_Waves |
| W5 | Tz-Kih | https://oldschool.runescape.wiki/w/Tz-Kih |
| W6 | Tz-Kek | https://oldschool.runescape.wiki/w/Tz-Kek |
| W7 | Tok-Xil | https://oldschool.runescape.wiki/w/Tok-Xil |
| W8 | Yt-MejKot | https://oldschool.runescape.wiki/w/Yt-MejKot |
| W9 | Ket-Zek | https://oldschool.runescape.wiki/w/Ket-Zek |
| W10 | TzTok-Jad | https://oldschool.runescape.wiki/w/TzTok-Jad |
| W11 | Yt-HurKot | https://oldschool.runescape.wiki/w/Yt-HurKot |
| W12 | TzHaar-Mej-Jal | https://oldschool.runescape.wiki/w/TzHaar-Mej-Jal |
| W13 | Fire cape | https://oldschool.runescape.wiki/w/Fire_cape |
| W14 | Tokkul | https://oldschool.runescape.wiki/w/Tokkul |
| W15 | Karamja Diary | https://oldschool.runescape.wiki/w/Karamja_Diary |
| W16 | Combat Achievements | https://oldschool.runescape.wiki/w/Combat_Achievements |

Cache sources [C], under `OSRS-Content/osrs239-content/`: `configs/all.npc`
(the eleven `tzhaar_fightcave_swarm_*` records and `tzhaar_fightcave_master`),
`configs/all.loc` (the two wall records), `configs/all.obj`, `configs/all.seq`
(the `firebat_*`, `lavabeast_*`, `magmaquris_*`, `lizard_cleric_*`,
`igniferum_*` families), `configs/all.spotanim`,
`server/scripts/npc/configs/npc_anims.generated.npc`.

Sibling docs: `docs/bosses/jad.md` (TzTok-Jad, Yt-HurKot, and the engine
attack-clock fix behind them), `docs/INFERNO_SOUNDS.md` §6 (the Fight
Caves → Inferno asset lineage), `docs/map_instances.md` (the instancing surface).

---

## 9. What this tree implements

`server/scripts/minigames/minigame_fightcave/`:

| File | Lines | State |
|---|---|---|
| `scripts/fightcave.rs2` | 210 | Wave engine, enter/leave, rewards, `[debugproc,fightcave]` |
| `scripts/fightcave_jad.rs2` | 471 | **Complete** — Jad's three styles, the four healers |
| `configs/fightcave.constant` | 133 | Waves, spawn cycle, Jad/HurKot numbers, npc_var slots |
| `configs/fightcave.npc` | 129 | Authored records for Jad and Yt-HurKot only |
| `configs/fightcave.varp` | 20 | Four session varps, all `scope=temp` |

plus `scripts/fightcave_ai.rs2` and `scripts/fightcave_selftest.rs2`, added by
this pass.

**Was already correct:** the 63-wave table (§5.1), the off-colour pairing on
waves 14/30/62 (§2.3), the Tz-Kek split, the five named spawn points and the
15-entry cycle as *data*, and the whole of TzTok-Jad and Yt-HurKot.

**Landed by this pass**, phases 1-5 of §10:

1. **Per-player instance.** Region 9551 is copied whole into a private
   reservation on entry; every arena tile is a local offset through
   `~fightcave_coord`. Despawn is by instance membership, and every exit path
   frees. Two players no longer share one Jad.
2. **Death, logout, pause and resume.** `~fightcave_on_death` /
   `~fightcave_finish_death` / `[queue,fightcave_death_cleanup]` wired into
   `player/death.rs2`; `~fightcave_on_logout` above the instance backstop in
   `player/logout.rs2`; `~fightcave_login` in `player/login.rs2`. The cave is a
   **safe activity** — `~player_death_lose_items` is skipped entirely for it,
   keyed on the same handle the death coordinate uses.
3. **All five wave monsters.** `fightcave_ai.rs2`: Tz-Kih's prayer drain (outside
   the damage branch), Tz-Kek's melee-only 1-point reflect, Tok-Xil's 15-tile
   spines, Yt-MejKot's heal-instead-of-swing, Ket-Zek's 15-tile fireball. All
   state their swing in `[ai_opplayer2]` rather than redirecting through
   `[ai_applayer2]`, which is the two-ticks-a-swing fault docs/bosses/jad.md
   §5.2 item 6 measured and moved both Jads off.
4. **Rotations.** The per-wave base of §5.2, an offset drawn from `date_minutes`
   on entry, and Jad's tile falling out of the arithmetic with no latch.
5. **Rewards.** `N·(N+1)` + 4,000, doubled on the elite Karamja diary; the
   TzRek-Jad pet at 1/200 (1/100 on task); the fire cape exchange; Jad's 25,250
   slayer override.

**Two defects found while doing it**, neither of them in the list the plan
started from:

- **Every wave monster had 10 hitpoints.** `npc_def_seed_from_cache`
  (`torirs_server_content.c`) seeds a server record from `g_npc_default` plus the
  cache's *params*; it does not read `stat1`..`stat6`. With no `.npc` block
  anywhere and no row in `combat_stats.generated.npc`, all eleven wave monsters
  carried the default — a 160-hitpoint Ket-Zek died to one scimitar hit. Nothing
  said so, because 10 is a plausible number. `configs/fightcave.npc` now states
  the six stats for all eleven, and `::fightcavetest` reads them back off a
  spawned npc rather than trusting the file.
- **Leaving at wave 63 paid the fire cape.** `~fightcave_leave` derived winning
  from `$wave >= 63`, so walking up to Jad and clicking Leave awarded the cape
  and 8,032 Tokkul. Winning is now passed in, and only Jad's death passes 1.

**Still open:** combat achievements (§10 phase 6 — the subsystem does not exist),
and a TzHaar slayer task to hang the 25,250 override and the 1/100 pet rate on
(both are written and gated on `~slayer_npc_matches_task`, which cannot match
until a task row exists).

---

## 10. Completion plan

Six phases, each independently landable and independently testable. Every one
has a sibling in `minigame_inferno/` that already does the same thing correctly;
the Inferno is the template throughout and its header comments carry the
reasoning for the shapes copied.

### Phase 1 — per-player instance

- `configs/fightcave.constant`: add `^fightcave_template = 0_37_79_0_0`; convert
  `^fightcave_entry`, `^fightcave_centre`, the five `^fightcave_point_*` and the
  fifteen `^fightcave_spawn_*` from absolute coords to local `lx`/`lz` integer
  pairs. `^fightcave_exit` stays absolute — it is outside the instance.
- New `[proc,fightcave_coord](int $lx, int $lz)(coord)` over
  `map_instance_coord(%map_instance_handle, $lx, $lz, 0)`, the twin of
  `inferno.rs2:5-6`. Every spawn and every teleport routes through it.
- `~fightcave_enter` allocates with `~map_instance_from_square`, bails with a
  message on `^map_instance_none`, stores `%map_instance_handle`, teleports in.
- New `~fightcave_despawn_arena`, in the shape of `inferno.rs2:40-50`: sweep by
  `map_instance_find(npc_coord) = $handle`, **not** by a type list. That file's
  note explains why a type list fails silently the first time a wave gains a
  monster, and the reason carries over unchanged.
- New `~fightcave_free_instance` / `~fightcave_free_instance_handle`
  (`inferno.rs2:111-124`). `~fightcave_leave` despawns, frees, then teleports.

The pool is 8 wide. Every exit path frees or the ninth entrant is refused.

### Phase 2 — death, logout, pause and resume

- `configs/fightcave.varp`: `fightcave_paused` and `fightcave_saved_wave` at
  **`scope=perm`** (they must survive the logout they exist for),
  `fightcave_logout_req` and `fightcave_death_pending` at `temp`. Re-run
  `tools/ss_allocate.py`.
- `~fightcave_on_death()(int)` / `~fightcave_finish_death(int $handle)` /
  `[queue,fightcave_death_cleanup]`, on `inferno.rs2:335-378`, wired into
  `player/death.rs2` beside `~inferno_on_death` with `$death_coord = ^fightcave_exit`.
- `~fightcave_on_logout` in `player/logout.rs2`, **above**
  `~map_instance_logout_release`. It must teleport out before freeing: a
  character saved on a released reservation logs back in on void.
- `~fightcave_request_pause` / `~fightcave_pause_now` / `~fightcave_login` on
  `inferno.rs2:189-214`, which already implements [W2]'s request semantics
  including the "you will have to repeat this wave" warning.
  `[oploc1/oploc2,tzhaar_fightcave_wall_exit]` arms the request while
  `%fightcave_alive > 0` and pauses otherwise; `~fightcave_npc_died` checks the
  flag before re-arming the wave softtimer.
- Resume prompt on the entrance loc via `~p_choice2` (`inferno.rs2:273-286`).
- **Safe death** (§6): confirm the cave is exempt from item loss, and if
  `player/death.rs2` has no safe-activity concept, add one keyed on the returned
  handle rather than a special case per minigame.

### Phase 3 — the five wave monsters

New `scripts/fightcave_ai.rs2`, on `inferno_ai.rs2`. Reuse the existing
`~fightcave_hit_player($duration, $style, $maxhit)` at `fightcave_jad.rs2:44`.

Authored records go in `configs/fightcave.npc` — `attackrate`, `attackrange`,
`huntmode=aggressive`, `huntrange`, `maxrange=40`, the three sounds from §3.
**Re-run `tools/gen_npc_stats.py` afterwards** so the generated blocks drop these
names; that file's own header explains that two blocks for one id resolve by
directory order.

| Monster | Handler |
|---|---|
| Tz-Kih | `[ai_opplayer2]` — `firebat_attack`, max 4 stab, then drain prayer **outside** the damage branch (§2.2). |
| Tz-Kek | `[ai_queue2]` override: a melee hit costs the attacker exactly 1. `_2spawn` does not bind it. |
| Tok-Xil | `[ai_opplayer2] npc_setmode(applayer2)` + `[ai_applayer2]` on `inferno_creature_ranger` (`inferno_ai.rs2:255-273`): `magmaquris_punch` max 13 when adjacent on a 1/3 roll, else `magmaquris_spine_attack` + `~player_projectile(… tzhaar_spine_attack …)` max 14. `param=attackrange,15`. |
| Yt-MejKot | `[ai_opplayer2]` — crush max 25; heal only when adjacent to the player, at most once per two swings, on itself or an adjacent monster below half, up to 10, **replacing** that swing. `lizard_cleric_heal` + `tzhaar_heal`, delivered by `npc_findallany` + `npc_queue` — the device `~fightcave_healer_heal` already uses. |
| Ket-Zek | As Tok-Xil but on `inferno_creature_mager` (`inferno_ai.rs2:285-298`): `igniferum_attack` max 55 stab adjacent, else `igniferum_ranged` + `tzhaar_fire_launch_travel` max 52 magic. `param=attackrange,15`. |

Two traps, both already paid for elsewhere in this tree:

- Every ranged or magic monster needs the `[ai_opplayer2] npc_setmode(applayer2)`
  redirect, or a *provoked* one falls through to the default melee swing and
  walks over to punch. `inferno_ai.rs2:120-131` records that exact symptom.
- Never use `npc_delay` as an attack cadence — it makes the npc invalid for its
  whole turn, and that is the turn it does not drain the queue every player hit
  arrives on. Use `npc_attackplayer` + `npc_attackdelay` (`inferno_ai.rs2:42-57`,
  51 sites).

### Phase 4 — rotations

- `configs/fightcave.varp`: `fightcave_rotation` (temp).
- Replace the running-total index with the per-wave base of §5.2: wave *N*
  begins at `(%fightcave_rotation + N − 1) mod 15`, and monsters within the wave
  take consecutive entries from there, highest level first — which
  `~fightcave_begin_wave` already emits in the right order.
- Pick `%fightcave_rotation` on entry from `map_clock` so it changes once a
  minute (100 ticks), recording in §7 hole #2 that the cadence is reproducible
  and the assignment is not.
- **No Jad-spawn special case.** Wave 63's base is `start + 62 ≡ start + 2`,
  which is wave 3's base; getting the indexing right is what makes Jad land on
  the wave-3 tile, and a forced latch would hide the indexing being wrong.

### Phase 5 — rewards and integrations

- `[proc,fightcave_tokkul_for_wave](int $waves)(int)` returning
  `$waves × ($waves + 1)`, plus 4,000 on a Jad kill, replacing
  `multiply($wave, 128)`.
- Karamja elite doubling via `~diary_tier_complete_flag(^diary_area_karamja,
  ^diary_tier_elite)`; the once-a-day resurrection needs a perm varp holding the
  day it was last used.
- `~fightcave_pet_roll` on the Jad kill, on `~inferno_pet_roll`
  (`inferno.rs2:142-155`) including the full-inventory `obj_add` fallback. Rate
  200, or 100 on a TzHaar task.
- `[opnpc3,tzhaar_fightcave_master]` — `~p_choice2` between 8,000 Tokkul and a
  1/200 pet roll, on `~inferno_master_talk` (`inferno.rs2:319-333`).
- Slayer: 25,250 on the Jad kill, 60 per Yt-HurKot, 11,520 on clearing wave 62,
  through `~slayer_on_npc_kill`. A *TzHaar task* is a new dbrow in
  `skill_slayer/configs/slayer_task_member.dbrow` plus the 100-point unlock —
  last, because it is slayer work rather than cave work.

### Phase 6 — combat achievements

The ten Fight Caves tasks are 50 points [W16]. **The subsystem does not exist**,
so this phase is two things: build a task registry (dbtable), per-task completion
bits, a points total, tier thresholds and the completion interface; then register
the ten tasks against it. That is larger than phases 1–5 combined and belongs in
its own plan once the cave itself is done. It is recorded here so the scope is
on the page, not because it lands with the rest.

---

## 11. Verification

```sh
make -C src torirsserver-scripts       # ss_allocate.py + sscompile
make -C src torirsserver-servpack
make -C src test-ToriRSServer          # builds, then runs ToriRSServer --selftest
```

**Two fixtures, and the split between them is deliberate.**

`::fightcavetest` (`scripts/fightcave_selftest.rs2`) owns everything that is
arithmetic over content's own tables — the fifteen rotation offsets against the
wiki's published wave 1–4 columns, Jad's tile falling out of `62 ≡ 2 (mod 15)`,
the `N·(N+1)` formula summing to 8,032, and every monster's hitpoints and attack
range read back **off a spawned npc** rather than off the file. A C copy of any
of those tables would be a second authority to keep in step, so it is written in
the language the tables are written in, and it can be run from a client. The
headless suite invokes it and reads the verdict out of `%fightcave_test_fails`,
which the fixture sets to `-1` on entry so an abort reads as failure rather than
as a pass it never reached.

The C stanza in `src/torirsserver/torirs_server_world.c` keeps what needs the engine: it
already pinned Jad's authored record (attack speed 8, attack range 15, 250
hitpoints), that exactly four healers spawn at half health, that a heal is the
flat +5 and not the Inferno's roll, and that a Jad healed to full summons a
second set. All of those still pass with the cave instanced — which is itself
the check that `::fightcave 63` allocates a reservation, spawns Jad inside it,
and puts the four healers on instance-local corners.

### Verification status at the time this landed

All of the following was run:

- Every file here **compiles** (`sscompile` clean, on the live tree and again in
  an isolated worktree carrying only these changes).
- The content tree **loads with zero errors from any Fight Caves file**, and no
  Fight Caves script aborts at runtime.
- **`::fightcavetest` passes all three groups**, stably across repeated runs.
- The C stanza's existing Jad and Yt-HurKot assertions **all still pass with the
  cave instanced**, which is the end-to-end check that `::fightcave 63` allocates
  a reservation, stands Jad up inside it, and puts four healers on
  instance-local corners.

**Every group was proved able to fail**, per `docs/LOSTCITY_PORT_TRIAGE.md`
§10.3 — a fixture that has never failed has not been tested:

| Mutation | Expected | Observed |
|---|---|---|
| `^fightcave_cycle_3` NW → SE | rotations | mask 1 |
| `^fightcave_tokkul_jad_bonus` 4000 → 4001 | tokkul | mask 2 |
| Ket-Zek `hitpoints` 160 → 150 | records | mask 4 |

All three were restored and the clean run re-confirmed.

One trap worth recording for anyone re-running this: `server/scripts/build` is a
**shared output directory** and more than one build can be writing it. A pack
that looks stale — or a `::fightcavetest` that "should run" and does not — is
usually somebody else's build, or a partially written `script.dat` read before
its compiler finished. Build with `--out <own dir>` and run with
`TORIRSSERVER_SCRIPTS=<own dir>`, and confirm provenance with
`strings script.dat | grep fightcavetest`.

```sh
make -C src torirsserver-scripts && make -C src ToriRSServer
TORIRSSERVER_REV=osrs239 src/build_opt/torirsserver --selftest 2>&1 | grep -i fightcavetest
```

Expected: no output — the suite prints only failures. A verdict is a bitmask:
1 = rotations, 2 = tokkul, 4 = records; `-1` means the fixture aborted before
reporting. Run `::fightcavetest` in a client for the per-check lines.

### What is covered now

`::fightcavetest` runs three groups:

- **The fifteen rotations.** Every published row's wave 1, wave 2 (both
  monsters) and wave 3 points, plus Jad's tile as wave 3's; that the fifteen
  offsets are 0–14 once each (a bitmask, so two rotations cannot share one and
  leave a third unused); and that rotations 7 and 8 — identical through wave 3 —
  actually differ at wave 4, or the table could be satisfied by one of them twice.
- **The reward.** 0, 1, 5 and 62 waves as well as the headline 8,032, because
  the formula this replaced (`wave × 128`) gives 8,064 at wave 63 and is wrong
  everywhere else — a check only at the top would have passed it.
- **The records.** Hitpoints for all eight npcs and attack range for the three
  that state one, read off a spawned npc. This is the group that catches a
  `.npc` block which fails to parse, loses a file-order race, or names a key the
  loader does not know — all of which are silent, and all of which leave a
  monster on the 10-hitpoint default.

### Still worth adding

- **Instance** — every spawned npc answers `map_instance_find(npc_coord) ==
  handle`; leaving frees it and `mapinstance_block_available` reports the square
  free again; two sessions in sequence do not see each other's npcs.
- **Death and pause** — dying mid-wave clears `%fightcave_active`, deletes every
  arena npc, frees the handle, and leaves the player on `^fightcave_exit` **with
  a full inventory**. A pause request at wave N logs out and resumes at N.
- **The monsters, in a fight** — each swings on its record's `attackrate` with
  no AP redirect in front of it (the Jad stanza's swing-interval assertion is
  the template); a Tok-Xil and a Ket-Zek **at 10 tiles** attack rather than
  walking in; a melee hit on a level-45 Tz-Kek costs the attacker exactly 1 and
  the same hit on a `_2spawn` costs 0; a Tz-Kih hit reduces prayer on a
  **0-damage** roll; a Yt-MejKot adjacent to the player and below half gains up
  to 10 and does **not** also swing that tick.

**Prove each assertion can fail** by mutating the implementation and re-running,
per `docs/LOSTCITY_PORT_TRIAGE.md` §10.3. The fight assertions need it most: one
that a monster "attacks at range" passes trivially if the default melee handler
happens to reach, so the subject must stand past melee range and the check must
be seen to fail with the record's `attackrange` removed.

Live:

```sh
make -C src EMBED_SERVER=1 torirs
./run-live.sh manifests/manifest_osrs239.ini
::fightcavetest    # the three arithmetic/record groups, with per-check lines
::fightcave 3      # rotation base and the Jad tile
::fightcave 31     # Ket-Zek magic at range
::fightcave 62     # the off-coloured pair, then Jad on the second one's tile
```

[W1]: https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave
[W2]: https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave/Strategies
[W3]: https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave/Rotations
[W4]: https://oldschool.runescape.wiki/w/Template:TzHaar_Fight_Cave_Waves
[W5]: https://oldschool.runescape.wiki/w/Tz-Kih
[W6]: https://oldschool.runescape.wiki/w/Tz-Kek
[W7]: https://oldschool.runescape.wiki/w/Tok-Xil
[W8]: https://oldschool.runescape.wiki/w/Yt-MejKot
[W9]: https://oldschool.runescape.wiki/w/Ket-Zek
[W10]: https://oldschool.runescape.wiki/w/TzTok-Jad
[W11]: https://oldschool.runescape.wiki/w/Yt-HurKot
[W12]: https://oldschool.runescape.wiki/w/TzHaar-Mej-Jal
[W13]: https://oldschool.runescape.wiki/w/Fire_cape
[W14]: https://oldschool.runescape.wiki/w/Tokkul
[W15]: https://oldschool.runescape.wiki/w/Karamja_Diary
[W16]: https://oldschool.runescape.wiki/w/Combat_Achievements
