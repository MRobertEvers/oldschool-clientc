# Zulrah — full implementation plan

> **Status, 17 August 2026: implemented.** Steps 1 and 3–11 of §15 have landed in
> [`minigames/minigame_zulrah/`](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_zulrah/)
> (6 configs, 8 scripts), plus the funnel rung in
> [rs2012_td_player_hit.rs2](../../OSRS-Content/osrs239-content/server/scripts/areas/area_rs2012_tormented_demons/scripts/rs2012_td_player_hit.rs2),
> the logout and death hooks in `player/logout.rs2` and `player/death.rs2`, and a
> `::zulrahrun` selftest stanza in `mock230_world.c`.
>
> `::zulrahrun` reports **OK — 6 checks passed** and was proven able to fail
> (corrupting rotation 4's Jad count from 8 to 10 makes two independent checks
> catch it). `make -C src mock230-scripts` and `mock230 --selftest` show no
> Zulrah failures and no Zulrah content-load errors.
>
> **Step 2 and step 7 — the measurement passes — have NOT been done.** Every
> number they were supposed to produce is in the tree as a disclosed
> approximation, tagged in `zulrah.constant` and listed in §13. The fight runs
> end to end; it is not yet calibrated. What is knowingly absent:
>
> | Gap | Where |
> |---|---|
> | Boss positions are channel-centred guesses (M1) | `^zulrah_pos_*_l[xz]` |
> | Cadence derived from "about three minutes", not measured (M2/M3) | `^zulrah_attack_ticks` etc. |
> | Venom-cloud tiles land near the player, not per-phase footprints (M6) | `~zulrah_add_tile` |
> | Cloud damage/lifetime, snakeling lifetime, tanzanite mix ratio (M5/M7/M8) | `zulrah.constant` |
> | D2/D3 deviation gated off pending M9/M10 | `~zulrah_may_deviate` |
> | No engine player-stun primitive; tail plays the animation + `%action_delay` | `~zulrah_stun` |
> | CA 227 "Snake Rebound" needs a Vengeance damage-source tag | `~zulrah_check_achievements` |
> | Boss-task Slayer assignment absent tree-wide (`slayer_task_zulrah` has `id = -1`) | `~zulrah_slayer_credit` |
> | Elite clue drop: cache has only per-step `trail_clue_elite_*`, no clue system | `~zulrah_roll_tertiary` |
> | Telescope spectating describes rather than shows | `zulandra.rs2` |
> | A stray "Badger" (npc 2124) remains at the Zul-Andra dock from the upstream spawn dump | `areas/world/configs/m34_47.spawn` |
>
> One dialect trap found while testing, worth knowing before writing any `mes`:
> **a non-ASCII character in a message string silently drops the whole message.**
> An em dash in the selftest's summary line made a passing run look like a run
> that never happened. All message strings here are ASCII; comments are not.

This is the completion plan for Zulrah: the Zul-Andra access chain, the
sacrificial boat, the instanced shrine, all three boss forms, both snakeling
types, venom clouds, all four rotations phase by phase, the post-first-rotation
deviation, the death/exit paths, the drop table, and the collection-log /
Combat Achievement / diary / Slayer hooks that hang off a kill.

The implementation target is current OSRS behaviour as of **17 August 2026**.
The principal sources are pinned by revision so later Wiki edits cannot
silently move the acceptance target:

- [Zulrah](https://oldschool.runescape.wiki/w/Zulrah?oldid=15298781)
  (revision 15298781)
- [Zulrah/Strategies](https://oldschool.runescape.wiki/w/Zulrah/Strategies?oldid=15299905)
  (revision 15299905) — **the authority for §7; every phase below is
  transcribed from its `Rotation overview` section**
- [Snakeling](https://oldschool.runescape.wiki/w/Snakeling?oldid=15298799)
  (revision 15298799)
- [Venom](https://oldschool.runescape.wiki/w/Venom?oldid=15289946)
  (revision 15289946)
- [Zul-Andra](https://oldschool.runescape.wiki/w/Zul-Andra?oldid=15250608)
  (revision 15250608)
- [Western Provinces Diary](https://oldschool.runescape.wiki/w/Western_Provinces_Diary)
  (hard task 11; elite reward = the daily Zulrah resurrection)
- [Perfect Zulrah](https://oldschool.runescape.wiki/w/Perfect_Zulrah) ·
  [Snake. Snake!? Snaaaaaake!](https://oldschool.runescape.wiki/w/Snake._Snake!%3F_Snaaaaaake!)
  (CA ids and their exact wording)
- [Zulrah's scales](https://oldschool.runescape.wiki/w/Zulrah%27s_scales?oldid=15225111) ·
  [Serpentine helm](https://oldschool.runescape.wiki/w/Serpentine_helm?oldid=15262696) ·
  [Toxic blowpipe](https://oldschool.runescape.wiki/w/Toxic_blowpipe?oldid=15240322)
- [Module:Tile markers/Zulrah.json](https://oldschool.runescape.wiki/w/Module:Tile_markers/Zulrah.json)
  (the five in-game tile markers the rotation chart's "x" positions refer to)

The Wiki is authoritative for player-visible behaviour, quantities, rates and
timings. The revision-239 cache is authoritative for symbolic names, object and
NPC ids, models, sequences, spot animations and locs. Where the Wiki gives only
an image, an approximation or descriptive prose, this plan states a
**measurement task** rather than inventing a number — every such point is
tagged `[MEASURE]` below.

---

## 1. Definition of done

A Zulrah implementation is complete only when all of these are true:

1. A player who has reached Port Tyras in Regicide can find Zul-Andra, talk to
   every villager, offer themselves as sacrifice to High Priestess
   Zul-Harcinqa, and only then use the deposit chest and board the boat.
2. Boarding creates a **private instance** of the shrine, teleports the player
   in, and shows the entry scene; Zulrah appears on the first continue/action.
   Quick-Board skips both the confirmation and the camera lock.
3. Zulrah runs a data-driven rotation: one of four patterns chosen at random,
   each phase naming a position, a form, and an **ordered** script of attacks,
   snakeling orbs and venom-cloud barrages, matching §7 step for step.
4. Rotation 1 of a fight is deterministic. Every rotation after it can deviate,
   per §7.6, including the unpredictable green form.
5. All three forms have the correct stats, defence profile, attack style,
   attack speed, max hit, the 50-damage cap, the fire-spell weakness, and the
   melee-only-by-halberd reach rule.
6. The crimson tail attack telegraphs on the player's tile, is dodgeable by
   moving clear, and deals 20–30 plus a stun when it connects.
7. Both snakeling types spawn from white orbs, envenom, have 1 HP, expire after
   40 seconds, and die instantly when Zulrah dies.
8. Venom clouds spawn from dark-green orbs as 3×3 locs, tick damage, and do
   **not** envenom. An orb of either kind launched on a dive tick defers its
   spawn until Zulrah resurfaces.
9. Every exit path — kill, death, logout, teleport, hop — restores state once,
   frees the instance once, and never strands a player on unreserved map.
10. Elite Western Provinces Diary holders resurrect once per day at 0 HP with
    full health and restored stats, tracked in the diary task list, counted as
    a safe death for Hardcore Ironman.
11. A kill drops the full two-roll table under the player, plus the unpickable
    Zul-andra teleport scroll, and increments kill count.
12. Collection log, all 9 Combat Achievements (ids 224–232), the hard Western
    Provinces diary task, and Konar/Nieve/Duradel Slayer credit all fire.
13. The automated and live-client matrix in §14 passes.

Out of scope, and owned elsewhere: the Regicide quest itself
([`quests/quest_regicide/`](../../OSRS-Content/osrs239-content/server/scripts/quests/quest_regicide/)),
the serpentine helm / toxic staff charge system (already landed in
[zulrah_item_charges.rs2](../../OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/gear/zulrah_item_charges.rs2)),
the toxic blowpipe special
([pvm_toxic_blowpipe.rs2](../../OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/specs/pvm_toxic_blowpipe.rs2)),
sacred eel fishing
([sacred_eel.rs2](../../OSRS-Content/osrs239-content/server/scripts/skill_fishing/scripts/fishing_spots/sacred_eel.rs2)),
and the paid 100k / 50-kc private instance sold by Priestess Zul-Gwenwynig
(an upsell *over* the instance this plan builds; add it after §4 lands).

---

## 2. Current tree — what exists, and the measured gaps

Content lives in
[`minigames/minigame_zulrah/`](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_zulrah/).
It is 5 lines of varp, 15 lines of constants and 78 lines of script. What it
does today:

- Adds `snakeboss_boat_quickboard` so the multiloc boat grows its second op.
- Teleports the player to a **shared, non-instanced** shrine pad and sets
  `%zulrah_active`.
- Spawns one `snakeboss_boss_ranged` and prints "Zulrah emerges…".
- Any of the three forms dying ends the run with a `mes()` that says
  "(Phase rotation / loot deferred)" and teleports home.

Everything else is missing. The gaps, each verified against this tree:

| # | Gap | Evidence |
|---|---|---|
| G1 | No instance. `^zulrah_shrine` is a live-world tile; two players collide. | `zulrah.constant` says "Instance still outstanding" |
| G2 | **No combat stats at all.** `snakeboss_boss_*` appears in `npc_anims.generated.npc` but *not* in `combat_stats.generated.npc`, and there is no `npc_stats/s/snakeboss_boss_*.stats` ledger. | `grep '^\[snakeboss' --include=*.npc` |
| G3 | Cache `statN=` fields are **not** read as combat stats. `mock230_content.c:1412` only honours authored `hitpoints=`/`attack=`/`defence=`/`magic=`/`ranged=`. So Zulrah's `stat4=500` is inert and it fights on engine defaults. | [mock230_content.c:1412](../../src/net/mock/mock230_content.c#L1412) |
| G4 | No rotation machine, no phases, no positions, no dive/emerge. | `zulrah.rs2` |
| G5 | No snakelings, no orbs, no venom clouds. | ditto |
| G6 | No drop table, no kill count, no collection log, no CAs. | `drop_tables/scripts/` has no zulrah file |
| G7 | Nothing applies venom to a player anywhere in the tree. The state machine exists and is correct; no monster feeds it. | [venom.rs2](../../OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/venom.rs2) header, lines 17-20 |
| G8 | **Priestess Zul-Gwenwynig (npc 1617, `snakeboss_priest_1op`) is not spawned anywhere.** The Zul-Andra roster instead spawns `snakeboss_priest` — npc **2124**, whose cache `name=` is **"Badger"**. | [m34_47.spawn:18](../../OSRS-Content/osrs239-content/server/scripts/areas/world/configs/m34_47.spawn#L18) vs `configs/all.npc` |
| G9 | No Zul-Andra dialogue at all: no sacrifice offer, no deposit-chest gate, no Zul-Urgish refusal. | no script references `snakeboss_highpriest` |
| G10 | `ai_queue3` death handlers are duplicated verbatim three times, one per form. Once forms become states of one encounter this collapses to one. | `zulrah.rs2:56-78` |
| G11 | No elite-diary resurrection anywhere. | no `resurrect` in `skill_combat/` |

The good news is that the substrate is unusually complete. Nothing in §3 needs
to be authored into the cache — every model, sequence, spot animation, loc, npc
and item already ships.

---

## 3. Asset inventory — everything already in the rev-239 cache

Never use raw ids in script; these are listed so the plan can be checked
against the cache, and so a wrong-record mistake like G8 is catchable.

### 3.1 NPCs — `configs/all.npc`

| Cache name | id | Wiki | Notes |
|---|---|---|---|
| `snakeboss_boss_ranged` | 2042 | Zulrah (Serpentine, green) | `magicdefence=-45`, ranged defence 50 |
| `snakeboss_boss_melee` | 2043 | Zulrah (Magma, crimson) | `magicdefence=0`, ranged defence 300 |
| `snakeboss_boss_magic` | 2044 | Zulrah (Tanzanite, blue) | `magicdefence=300`, ranged defence 0 |
| `snakeboss_minion_melee` | 2045 | Snakeling (Melee) | `stat1=140 stat3=138`, `attackrate=3` |
| `snakeboss_minion_magic` | 2046 | Snakeling (Magic) | `stat6=185`, `magicattack=80` |
| `snakeboss_minion_dying` | 2047 | Snakeling (dying) | `vislevel=0`, no ops — the despawn form |
| `snakeboss_highpriest` | 1616 | High Priestess Zul-Harcinqa | spawned at 2192,3055 |
| `snakeboss_priest_1op` | 1617 | Priestess Zul-Gwenwynig | **not spawned — G8** |
| `snakeboss_gnome_victim` | 2037 | Sacrifice | spawned at 2210,3056 |
| `snakeboss_gnome_1` | — | Zul-Cheray | hard clue step target |
| `snakeboss_gnome_2` | — | Zul-Gutusolly | |
| `snakeboss_ogre_1` | — | Zul-Urgish | refuses the deposit chest pre-sacrifice |
| `snakeboss_fishingspot` | — | Sacred eel spot | already wired |
| `snakeboss_priest` | 2124 | **"Badger"** | wrong record, wrongly spawned — G8 |

The `2045` / `2046` split is player-visible and matters: the Perfect Zulrah
guide tells players to highlight the two ids separately, so they must be two
distinct npcs, not one npc with a style flag.

All three boss records already carry `size=5`, `footprintsize=256`,
`readyanim=snakeboss_idle`, `param=attackrate,3` and `vislevel=725`.

### 3.2 Sequences — `configs/all.seq`

`snakeboss_spawn` (5071) · `snakeboss_emergefast` (5073) ·
`snakeboss_sinkfast` (5072) · `snakeboss_idle` (5070) ·
`snakeboss_attack_acidx3` (5068) · `snakeboss_attack_acidx1` (5069) ·
`snakeboss_attack_tail_left` (5806) · `snakeboss_attack_tail_right` (5807) ·
`snakeboss_defend` (5808) · `snakeboss_death` (5804).

`snakeboss_spawn` is the long entry animation; `snakeboss_emergefast` /
`snakeboss_sinkfast` are the per-phase pair. The `acidx3` / `acidx1` split is
almost certainly the multi-projectile barrage versus the single attack —
**[MEASURE]**, then bind barrages to `acidx3` and single attacks to `acidx1`.

Snakelings reuse the pet rig: `snakeboss_pet_idle`, `snakeboss_pet_attack`,
`snakeboss_pet_defend`, `snakeboss_pet_death`, `snakeboss_pet_spawn`, and the
four directional walks — already wired into the records.

`npc_combat/s/snakeboss_boss_{ranged,melee,magic}.combat` currently pick
`snakeboss_attack_acidx3` for all three forms, generated with the note
"4 candidates". The crimson form must be pinned by hand to
`snakeboss_attack_tail_left`/`_right` (`source = authored`), because the tail
whip is a different animation from the acid spit.

### 3.3 Spot animations — `configs/all.spotanim`

| Cache name | id | Proposed role |
|---|---|---|
| `snakeboss_orb` | 1044 | **dark-green orb** — the venom-cloud seed |
| `snakeboss_double_orb` | 1045 | magic attack projectile |
| `snakeboss_fireball` | 1046 | **[MEASURE]** unassigned; candidate for the ranged attack |
| `snakeboss_egg` | 1047 | **white orb** — the snakeling seed; pale recolour of `snakeboss_orb`'s model 20390 (recol1d 127) |
| `snakeboss_minion_spell` | 1230 | magic snakeling's venom bolt |

The Wiki names the two orb colours explicitly — snakelings come from "white
orbs", venom clouds from "a barrage of dark-green orbs" — which is what pins
`snakeboss_egg` (pale recolour) to snakelings and `snakeboss_orb` (green
recolour) to clouds. The attack-projectile assignments are still inference from
model and recolour; confirm in-client before pinning.

### 3.4 Locs — `configs/all.loc`

| Cache name | id | Role |
|---|---|---|
| `snakeboss_poisoncloud` | 11700 | **the venom cloud** — `width=3 length=3`, `blockwalk=0`, `anim=misty` |
| `snakeboss_pillar` | 11698 | the two melee safespot pillars, `blockwalk=1` |
| `snakeboss_pillar_broken` | 11699 | decorative, south in the water |
| `snakeboss_exit` | 11701 | the un-pickupable Zul-andra teleport scroll, `op1=Read` |
| `snakeboss_boat_1op` / `_2ops` | 46241 / 46242 | multiloc on `%snakeboss_info`; only value 3 grows Quick-Board |
| `inviswall` | 38848 | seals the two gaps beside the pillars |

The cloud being a 3×3 loc is the single most important asset fact in this
document, and it matches the Wiki exactly ("If a player stands within a cloud's
3x3 area"). A venom cloud barrage is a volley of projectiles that each *place a
loc*, not a spotanim.

### 3.5 Objects — `configs/all.obj`

`snakeboss_scale` 12934 · `blowpipe_fang` 12922 (Tanzanite fang) ·
`magic_fang` 12932 · `serpentine_visage` 12927 · `uncut_onyx` 6571 ·
`cyan_mutagen` 13200 (Tanzanite) · `red_mutagen` 13201 (Magma) ·
`jar_of_swamp` 12936 · `snakepet` 12921 (Pet Snakeling), with
`snakepet_orange` 12939 / `snakepet_blue` 12940 mutagen variants ·
`teleportscroll_zulandra` 12938 · `snakeboss_book` 12935 (Ohn's diary).

### 3.6 Maps

`maps/m35_47` and `maps/m35_48` both ship, and contain the shrine.

---

## 4. The shrine — geometry, derived from the cache

Extracted from `maps/m35_47.jm2` / `m35_48.jm2` by testing each tile for a
swamp-water overlay. `.` is land, `~` is water:

```
     2255                          2285
3078 ~~~~~~~~~.~~~~~~~.~~~~~~~~~~~~~
3077 ~~~~~~~~..~~~~~~~...~~~~~~~~~~~
3076 ~~~~~~~...~~~~~~~...~~~~~~~~~~~
3075 ~~~~~~~...~~~~~~~...~~~~~~~~~~~
3074 ~~~~~~~...~~~~~~~...~~~~~~~~~~~
3073 ~~~~~~~...~~~~~~~...~~~~~~~~~~~
3072 ~~~~~~~...~~~~~~~...~~~~~~~~~~~
3071 ~~~~~~~....~~~~~....~~~~~~~~~~~
3070 ~~~~~~~............~~~~~~~~~~~~
3069 ~~~~~~~~~.........~~~~~~~~~~~~~
3068 ~~~~~~~~~~~.....~~~~~~~~~~~~~~~
```

This is the Wiki's "small U-shaped island with two pillars in the middle
corners", opening **north**. Confirmed placements from `maps/m35_47.jl2`:

- `snakeboss_pillar` at **(2265, 3071)** and **(2271, 3071)** — the two inner
  corners, exactly where the arms meet the base. These are the melee safespots
  the Wiki's `File:Zulrah safespot.png` highlights ("the equivalent space
  beside the other pillar").
- `snakeboss_pillar_broken` at (2268, 3067), in the water south of the base.
- `inviswall` at (2266, 3071), (2270, 3071), (2265, 3072), (2271, 3072) —
  sealing the pillar gaps.

The Wiki's five tile markers, converted from `regionId`/`regionX`/`regionY`.
These are the "x" run-to positions the rotation chart refers to, and they are
the **only** structured per-phase positional data the Wiki publishes:

| # | Region | Local | World | Where |
|---|---|---|---|---|
| T1 | 9007 | 28, 61 | **2268, 3069** | south-centre of the base — the existing `^zulrah_shrine` constant |
| T2 | 9008 | 24, 0 | **2264, 3072** | west arm, north of the west pillar |
| T3 | 9008 | 32, 0 | **2272, 3072** | east arm, north of the east pillar |
| T4 | 9008 | 34, 5 | **2274, 3077** | east arm, north-east |
| T5 | 9008 | 32, 6 | **2272, 3078** | east arm, north tip |

### 4.1 Zulrah's four positions — `[MEASURE]`

Zulrah is 5×5 and swims in the water. The rotations name only four positions:
**middle**, **south**, **east**, **west** (there is no north). The island
geometry constrains each to one 5-tile-wide water channel, which pins the axis
but not the offset along it:

| Position | Constraint from the map | Candidate SW corner |
|---|---|---|
| middle | the inner bay, x 2266–2270 | (2266, 3071) — x is forced, z needs measuring |
| south | water south of the base, z ≤ 3067 | (2266, 3063) |
| east | water east of the east arm, x ≥ 2275 | (2275, 3072) |
| west | water west of the west arm, x ≤ 2261 | (2257, 3072) |

**Measurement method**: take the shrine into an instance, place a 5×5 marker
npc at each candidate, screenshot from a south-facing camera (the Wiki says to
orient south to match the charts), and compare against `File:Zulrah
Patterns.png` and `File:Zulrah's_Shrine.png`. The five tile markers are the
cross-check — a correct *middle* puts T1 (2268, 3069) directly in front of the
boss, and a correct *west* leaves T5 (2272, 3078) outside its ranged cone.

Do not ship guessed coordinates: a wrong offset changes which attacks reach the
player, which silently rewrites every rotation's difficulty.

### 4.2 Instancing

The engine already has everything needed —
`map_instance_alloc` / `setchunk` / `build` / `coord` / `free` / `find`
(opcodes 11009–11014), with helper procs in
[map_instance_procs.rs2](../../OSRS-Content/osrs239-content/server/scripts/general/scripts/misc/map_instance_procs.rs2).

The shrine straddles the m35_47 / m35_48 boundary (base at z 3068–3070 on
m35_47, arms running to z 3078 on m35_48), so `~map_instance_from_square` is
the wrong helper — it copies **one** square. Use `map_instance_alloc(8, 8)`
plus two `~map_instance_copy_area` calls, or a purpose-built
`~zulrah_build_instance` that copies the 4×4-zone rectangle covering
x 2240–2271 / z 3040–3103. Follow the Gauntlet's shape in
[gauntlet.rs2:137-165](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet.rs2#L137-L165).

Store the handle in `%map_instance_handle` so the existing
`~map_instance_logout_release` backstop catches an unclean logout, and add a
Zulrah-specific `[logout,_]` proc ahead of it so the player lands at Zul-Andra
rather than the generic `^respawn_coord`.

---

## 5. Access chain — Zul-Andra

The Wiki's gate is two-step and currently absent (G8, G9):

1. Reaching Port Tyras in **Regicide** puts the player in range. Transport:
   charter ship to Port Tyras, fairy ring **BJS** + stepping stones (76
   Agility, boostable), or a `teleportscroll_zulandra`.
2. Speaking to **High Priestess Zul-Harcinqa** to offer yourself as sacrifice
   in place of the gnome child. Until that happens, **Priestess
   Zul-Gwenwynig** blocks the boat and **Zul-Urgish** refuses the deposit
   chest.

Work items:

- **A1** Fix the roster: replace `snakeboss_priest` (2124, "Badger") in
  [m34_47.spawn](../../OSRS-Content/osrs239-content/server/scripts/areas/world/configs/m34_47.spawn)
  with `snakeboss_priest_1op` (1617) at the dock. Because that file is
  generated by `tools/gen_spawns.py`, the correction belongs in the generator's
  override path, not as a hand edit that the next run reverts.
- **A2** New `%zulrah_sacrifice_offered` varp (perm scope, protect=yes).
- **A3** Dialogue for Zul-Harcinqa, Zul-Gwenwynig, Sacrifice, Zul-Cheray,
  Zul-Gutusolly, Zul-Urgish. Zul-Cheray is a hard clue step and Sacrifice is a
  master clue step, so their `opnpc1` handlers must not swallow clue dispatch.
- **A4** Gate the deposit chest (`snakeboss_bankchest`) on
  `%zulrah_sacrifice_offered`, with Zul-Urgish's refusal when unset.
- **A5** Ohn's diary (`snakeboss_book`) in the chest-building crate.
- **A6** Keep the existing `%snakeboss_info = 3` write that grows the boat's
  Quick-Board op — but move it out of `zulrah_enter` (where it only fires
  *after* someone already boarded the one-op boat) into login/area entry.
- **A7** Telescope (`snakeboss_telescope`) on the island north of Zul-Andra
  (fairy ring **DLR**) for spectating.

---

## 6. Mechanics — exact behaviour

Every statement in this section is from the pinned Wiki revisions. Where the
Wiki says "several seconds" or "varying", the number is a `[MEASURE]`, not a
licence to invent.

### 6.1 Shared stats (all three forms)

| Field | Value | Where it goes |
|---|---|---|
| Hitpoints | 500 | authored `hitpoints=500` |
| Attack / Strength | 1 / 1 | `attack=1` `strength=1` |
| Defence | 300 | `defence=300` |
| Magic / Ranged | 300 / 300 | `magic=300` `ranged=300` |
| Combat level | 725 | already `vislevel=725` |
| Size | 5×5 | already `size=5` |
| Attack speed | **3 ticks (1.8 s)** | already `param=attackrate,3` |
| Melee defence (stab/slash/crush) | 0, all forms | — |
| Magic attack bonus | +50 | `amagic=50` |
| Ranged attack bonus | +50 | `arange=50` |
| Magic / Ranged strength | +20 / +20 | present as params |
| Fire-spell weakness | **50%** accuracy and damage | 29 May 2024 rebalance |
| Poison / venom resistance | 100% | immune |
| Slayer XP | 500 | slayer hook |
| Immune to cannon / thralls | No / No | — |

Because of **G3**, these must be authored. Two routes, and the tree has both:
run `tools/gen_npc_stats.py` to produce `npc_stats/s/snakeboss_boss_*.stats`
ledgers (regenerable, joined to the Wiki infobox by npc id — which is exactly
what that tool does), or hand-author a `minigame_zulrah/configs/zulrah.npc` in
the Inferno style
([inferno.npc:87-95](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/configs/inferno.npc#L87-L95)).
Prefer the generator.

### 6.2 Per-form profile

| | Serpentine (green) | Magma (crimson) | Tanzanite (blue) |
|---|---|---|---|
| npc | `snakeboss_boss_ranged` 2042 | `snakeboss_boss_melee` 2043 | `snakeboss_boss_magic` 2044 |
| Attack style | **Ranged** (except Jad phases) | **Typeless** tail | **Magic and Ranged**, magic much more frequent |
| Max hit | 41 | 30 (tail deals 20–30) | 41 |
| Magic defence | **−45** | 0 | **+300** |
| Ranged defence (light/std/heavy) | +50 | **+300** | 0 |
| Weak to | Magic | Magic (relatively) | Ranged |
| Player prays | Protect from Missiles | — (dodge instead) | Protect from Magic |
| Examine | "The green hooded serpent…" | "The crimson hooded serpent…" | "The turquoise hooded serpent…" |

The tanzanite form "can attack with Ranged up to five times in succession", so
the magic/ranged mix is a per-attack roll with a long ranged tail, not a fixed
alternation. **[MEASURE]** the ratio; the Wiki says only "much more
frequently".

### 6.3 The crimson tail attack

1. Zulrah **stares at the player's current tile for several seconds**
   (`[MEASURE]` the telegraph length) — this fixes the target tile.
2. It whips its tail at that area (`snakeboss_attack_tail_left`/`_right`).
3. A player still in the targeted area takes **20–30 typeless damage and is
   stunned for several seconds** (`[MEASURE]` the stun length).
4. The attack is avoided by **moving two tiles away** from the target.

Typeless means no protection prayer reduces it and no defensive style applies.
The two pillar tiles at (2265, 3071) and (2271, 3071) are the standing
safespots for this phase.

### 6.4 Rules that need engine or funnel work

- **Damage cap.** Any player hit above 50 is replaced by a uniform roll in
  45–50. Implement as a rung on `~player_hit_npc_prepare` — the funnel the
  Gauntlet and QBD already hook
  ([gauntlet_hunllef.rs2:252](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet_hunllef.rs2#L252)).
  Boss only; snakelings take their 1 damage uncapped.
- **Melee reach.** Since 7 May 2025 Zulrah is no longer melee-immune, but only
  halberds reach it — a consequence of the 5×5 footprint plus the water gap,
  so it should fall out of correct positioning. Verify rather than special-case.
- **Attacks resolve at animation time, not impact.** The Wiki is explicit:
  "Damage is calculated on animation/attack generation not when projectile hits
  you", and "a protection prayer only needs to be active **starting** on the
  same tick that Zulrah begins its attack animation". So: roll the hit when the
  animation plays, queue the hitsplat for the flight duration.
  `~player_projectile`
  ([projectile.rs2:30](../../OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/projectile.rs2#L30))
  returns exactly that duration. During a Jad phase the player is *expected* to
  have already switched prayers by the time the projectile lands.
- **A death mid-swing still lands.** "Zulrah's existing attacks will cause
  damage even after it is killed" — the death handler must not cancel queued
  hitsplats. This is the opposite of the usual cleanup instinct and is worth
  its own test.
- **A hit landing on the dive tick is deferred, not lost.** "If struck with a
  hit just as it dives, the hit will register on Zulrah shortly after it
  resurfaces." So player damage queued across a dive must survive the
  `npc_del`/`npc_add` — which is an argument for `npc_changetype_keepall` over
  a delete/re-add wherever the position does not move.
- **Envenoming.** "The ranged and magic attacks will envenom the player unless
  they miss, **even if blocked by a protection prayer**." So venom is applied
  on any non-miss, before the protection-prayer damage reduction — not on
  damage dealt. `~apply_venom` already exists and is correct; nothing feeds it
  yet (G7). This is the first caller.
- **Venom clouds do NOT envenom.** They deal venom-*flavoured* damage only. Use
  `damage(uid, hitsplat_poison, n)` directly; never call `~apply_venom`.
- **Stun.** Confirm the tree has a player-stun primitive for the tail hit; if
  not, that is a scoped engine task, not a silent omission.
- **Visibility.** Since 15 January 2015, "Zulrah now remains visible when it's
  a long distance from its attacker" — do not let the normal view-distance cull
  drop it from the west/east positions.

### 6.5 Snakelings

| | Melee (2045) | Magic (2046) |
|---|---|---|
| Hitpoints | 1 | 1 |
| Max hit | 15 | 13 |
| Attack / Strength | 140 / 138 | 1 / 1 |
| Magic | 1 | 185 |
| Attack bonus | +120 stab | +80 magic |
| All defences | −40 | −40 |
| Attack speed | 3 | 3 |
| Combat level | 90 | 90 |
| Projectile | — (melee) | `snakeboss_minion_spell` |

Behaviour:

- Summoned by **white orbs** (`snakeboss_egg`). Each snakeling is melee **or**
  magic, never both.
- Both envenom. Protection prayers block their attacks.
- **Lifetime: 40 seconds.** "Snakelings will also die off if they have been
  lingering around for more than 40 seconds, whether they are in combat with
  the player or not." At 0.6 s/tick that is **~67 ticks** — pin the tick count
  by `[MEASURE]`, since 40 s is a rounded figure.
- They die instantly when Zulrah dies.
- Zulrah "will usually summon three to four snakelings at a time".
- `snakeboss_minion_dying` (2047, `vislevel=0`, no ops) is the cache's own
  despawn form: `npc_changetype` into it, play `snakeboss_pet_death`, `npc_del`.

### 6.6 Venom clouds

- Spawned by a **barrage of dark-green orbs** (`snakeboss_orb`).
- Each cloud is a **3×3 area** (`snakeboss_poisoncloud`, `blockwalk=0` — a
  player can and sometimes must walk through one).
- Standing in one deals **varying damage per tick** (`[MEASURE]` the range).
- They do **not** envenom.
- "Certain phases may force the player to run through venom clouds in order to
  re-position themselves" — so cloud placement must leave the run path
  crossable, not sealed.
- **[MEASURE]** cloud lifetime.

### 6.7 Deferred spawns across a dive

Both orb types share one rule, stated twice on the Wiki:

> While most snakelings spawn immediately, in some of Zulrah's attack rotations
> it will occasionally throw a white orb and then dive into the swamp. The
> summoned snakeling will only appear **after** Zulrah reemerges from the swamp.

> Similarly to the snakeling orbs, if Zulrah launches a dark-green orb just as
> it dives, the venomous cloud will not appear until Zulrah resurfaces.

So an in-flight orb at dive time is **deferred, never cancelled**. Implement as
a pending-spawn list drained on emerge, not as a flight timer that keeps
running through the dive.

### 6.8 The elite-diary resurrection

From the Western Provinces Diary elite rewards, added 5 March 2015:

> Resurrect with full health and restored stats once per day when you reach 0
> hitpoints against Zulrah, allowing you to continue on with the fight (the
> task list of this diary will show you whether you've used this daily
> resurrection, and this is considered a "safe death" for Hardcore Ironman
> players).

Three consequences: it needs a daily-reset varp, it must be readable from the
diary interface, and it must be classified as a safe death — so it has to
intercept **before** the normal death path, not after.

---

## 7. The rotations — every phase, exactly

Zulrah picks one of four rotations at random at fight start and runs it to
completion, then picks again — possibly the same one. A full rotation takes
about three minutes.

Notation used in the `Script` column. Each token is one scripted event, in
order, left to right:

| Token | Meaning |
|---|---|
| `RANGED×n` | *n* ranged attacks (green form's normal attack) |
| `MAGIC×n` | *n* magic attacks |
| `MIXED×n` | *n* attacks, each independently magic or ranged, magic weighted heavier (tanzanite form) |
| `MELEE×n` | *n* tail attacks, per §6.3 |
| `JAD-R×n` | *n* attacks strictly alternating, **starting ranged** |
| `JAD-M×n` | *n* attacks strictly alternating, **starting magic** |
| `ORB×n` | *n* white orbs → snakelings |
| `CLOUD×n` | *n* venom-cloud barrages (dark-green orbs) |
| `ALT(A,B)×n` | *n* events **total**, alternating between A and B, starting with A |

`ALT` carries a real ambiguity and it is worth naming before it silently
changes every count below. The Wiki says "an alternating series of 5 snakeling
orbs and venom cloud barrages" (R1/R2 phase 8) and "an alternating series of 6
venom cloud barrages and snakeling orbs" (R3 phase 3). That parses two ways:
*n* events in total, or *n* of each for 2*n* events. This document takes the
first reading throughout — so `ALT(ORB, CLOUD)×5` is 3 orbs and 2 clouds, not
5 and 5. **[MEASURE]** which is right; the second reading adds 5 events to
rotations 1 and 2 and 6 to rotation 3, and shifts §7.7's timing budget with
them.

### 7.1 Structural rules

Three rules shape the data model, and each is a separate correctness trap:

1. **The fight's very first phase is unique.** Green, middle, venom clouds
   only, **no attacks** — "It will never attack directly at the start; rather,
   Zulrah will fill the area with venom clouds, leaving the tips on the east
   and west sides clear." This is why world-hopping to reroll the rotation
   works: the player is not yet in combat, and stays out of combat until
   phase 2.
2. **The last listed phase of every rotation is the first phase of the next
   one.** "At the end of every rotation, Zulrah will once again appear in the
   middle of the shrine in its green form. It will attack the player with
   ranged attacks several times before filling the area with venom clouds.
   **This phase counts as the first phase of the new rotation.**" So the tables
   below end on a bridge node — `RANGED×5 → CLOUD×4`, middle, green — which is
   *not* the same as the fight-opening phase 1 (which has no attacks). Model
   them as two distinct nodes.
   Rule 2 is self-checking. The Wiki calls every Jad phase the "second-to-last
   phase", and that only holds once the bridge is excluded: rotation 1's Jad is
   listed at #9 of 11, rotation 3's at #10 of 12, rotation 4's at #11 of 13 —
   second-to-last in all three only if the last row belongs to the *next*
   rotation. If an implementation treats the bridge as a terminal phase, every
   Jad lands one phase too early.

3. **Rotations 1 and 2 are indistinguishable until phase 4.** Their phases 1–3
   are byte-identical. The player identifies the rotation from phase 2, and
   disambiguates Crimson A from Crimson B at phase 4 (south = R1, west = R2).
   The implementation must therefore pick the rotation at fight start and
   commit, not decide lazily per phase.

Identification, from the Wiki, as a correctness cross-check on the tables:

- Phase 2 **crimson** → rotation 1 or 2; phase 4 **south** = R1, **west** = R2.
- Phase 2 **green** → rotation 3.
- Phase 2 **tanzanite** → rotation 4.

### 7.2 Rotation 1 — "Crimson A", 11 phases

| # | Position | Form | Script |
|---|---|---|---|
| 1 | middle | green | `CLOUD×4` |
| 2 | middle | crimson | `MELEE×2` |
| 3 | middle | tanzanite | `MIXED×4` |
| 4 | **south** | green | `RANGED×5` → `ORB×2` → `CLOUD×2` → `ORB×2` |
| 5 | middle | crimson | `MELEE×2` |
| 6 | **west** | tanzanite | `MIXED×5` |
| 7 | **south** | green | `CLOUD×3` → `ORB×4` |
| 8 | **south** | tanzanite | `MIXED×5` → `ALT(ORB, CLOUD)×5` |
| 9 | **west** | green | **JAD** `JAD-R×10` → `CLOUD×4` |
| 10 | middle | crimson | `MELEE×2` |
| 11 | middle | green | `RANGED×5` → `CLOUD×4` — *bridge to next rotation's phase 1* |

Phase 8's Wiki wording is "attacking with both Magic **and** Ranged 5 times",
where every other tanzanite phase says "and/or". Treat as prose variance unless
measurement says otherwise, but do not silently normalise it away — record it.

Phase 9's trailing clouds carry an unresolved Wiki editor comment asking
whether both side tips really stay clear in rotations 1 and 2. **[MEASURE]**.

### 7.3 Rotation 2 — "Crimson B", 11 phases

| # | Position | Form | Script |
|---|---|---|---|
| 1 | middle | green | `CLOUD×4` |
| 2 | middle | crimson | `MELEE×2` |
| 3 | middle | tanzanite | `MIXED×4` |
| 4 | **west** | green | `CLOUD×3` → `ORB×4` |
| 5 | **south** | tanzanite | `MIXED×5` → `ORB×2` → `CLOUD×2` → `ORB×2` |
| 6 | middle | crimson | `MELEE×2` |
| 7 | **east** | green | `RANGED×5` |
| 8 | **south** | tanzanite | `MIXED×5` → `ALT(ORB, CLOUD)×5` |
| 9 | **west** | green | **JAD** `JAD-R×10` → `CLOUD×4` |
| 10 | middle | crimson | `MELEE×2` |
| 11 | middle | green | `RANGED×5` → `CLOUD×4` — *bridge* |

Rotations 1 and 2 differ **only** in phases 4, 5 and 7. Phases 1–3 and 8–11 are
identical, which is exactly what the "rotations separate until the
second-to-last phase" prose describes.

### 7.4 Rotation 3 — "Serp", 12 phases

| # | Position | Form | Script |
|---|---|---|---|
| 1 | middle | green | `CLOUD×4` |
| 2 | **east** | green | `RANGED×5` → `ORB×3` |
| 3 | middle | crimson | `ALT(CLOUD, ORB)×6` → `MELEE×2` |
| 4 | **west** | tanzanite | `MIXED×5` |
| 5 | **south** | green | `RANGED×5` |
| 6 | **east** | tanzanite | `MIXED×5` |
| 7 | middle | green | `CLOUD×3` → `ORB×3` |
| 8 | **west** | green | `RANGED×5` |
| 9 | middle | tanzanite | `MIXED×5` → `CLOUD×2` → `ORB×3` |
| 10 | **east** | green | **JAD** `JAD-M×10` — **no clouds afterwards** |
| 11 | middle | tanzanite | `ORB×4` |
| 12 | middle | green | `RANGED×5` → `CLOUD×4` — *bridge* |

Two things distinguish rotation 3 and both are load-bearing:

- Its Jad phase starts with **magic**, at the **east** position, and — unlike
  rotations 1 and 2 — is **not** followed by a venom-cloud barrage. "Zulrah
  does not send out venom clouds at the end of this phase and simply dives back
  into the swamp when it's done attacking."
- Phase 3 is the only crimson phase in any rotation that fires orbs and clouds
  *before* its melee attacks. It is also the reason the Perfect Zulrah guide
  recommends this rotation: "the snakelings spawn almost all together at the
  start of the kill, and many can be trapped on the far side of the shrine".

Rotation 3 also carries the "right before the second-to-last phase, Zulrah will
send out snakelings; this does not occur in Rotation 4" note — that is phase 9's
`ORB×3`.

### 7.5 Rotation 4 — "Tanz", 13 phases

| # | Position | Form | Script |
|---|---|---|---|
| 1 | middle | green | `CLOUD×4` |
| 2 | **east** | tanzanite | `ORB×4` → `MIXED×6` |
| 3 | **south** | green | `RANGED×4` → `CLOUD×2` |
| 4 | **west** | tanzanite | `ORB×4` → `MIXED×4` |
| 5 | middle | crimson | `MELEE×2` → `CLOUD×2` |
| 6 | **east** | green | `RANGED×4` |
| 7 | **south** | green | `ORB×6` → `CLOUD×3` |
| 8 | **west** | tanzanite | `MIXED×5` → `ORB×4` |
| 9 | middle | green | `RANGED×4` |
| 10 | middle | tanzanite | `MIXED×4` → `CLOUD×3` |
| 11 | **east** | green | **JAD** `JAD-M×8` — **8 attacks, not 10** |
| 12 | middle | tanzanite | `ORB×4` |
| 13 | middle | green | `RANGED×5` → `CLOUD×4` — *bridge* |

Rotation 4 is the outlier in three ways, all of which will be silently wrong if
the implementation generalises from rotations 1–3:

- It is the only rotation with **13** phases.
- Its Jad phase is **8** attacks, not 10.
- Its green phases mostly fire **4** ranged attacks, not 5 — the only rotation
  where the green attack count is not uniformly 5.
- Crimson appears once (phase 5), as in rotation 3, against three times in
  rotations 1 and 2.

It also matches the Wiki's warning that "rotation 4 tends to cause more damage"
— it fires 22 snakeling orbs against rotation 1's 11, and it front-loads them:
8 orbs are out before the fight is a third over.

### 7.6 How rotations vary after the first — the deviation model

This is the part most implementations get wrong by leaving it out. The Wiki is
precise about *what* varies and vague about *how much*, so the design must be
explicit about which is which.

**What the Wiki states:**

> At the start of the fight, Zulrah will be in one of four possible rotations,
> with each rotation consisting of predefined phases. It takes Zulrah about
> three minutes to complete each full rotation. After it finishes a rotation,
> Zulrah will then begin a new one; **it is possible to be the same rotation as
> the previous one**. **All rotations after the first one have a chance for
> Zulrah to randomly be in a phase different from the expected phase.** There
> is also a chance for Zulrah to enter a form where it will **randomly switch
> between mage and ranged attacks in a way that cannot be predicted and prayed
> against** (unlike the "Jad" phase, which alternates between Magic and Ranged
> each attack). Because of this, it is best to try to kill Zulrah within the
> first rotation.

And from the main page, giving the same thing on a clock:

> At approximately 2 minutes, Zulrah will be in its green form and attack with
> alternating Ranged and Magic attacks (colloquially called "Jad phase").
> If the fight takes a long time (3+ minutes), it is possible for Zulrah to be
> in its green form and attack unpredictably with Ranged and Magic.

The provenance is the **15 October 2015** update: "Zulrah's attack sequences now
have a little more variety, since they were too predictable."

**The model this yields — four distinct behaviours, not one:**

| | Behaviour | When | Implementation |
|---|---|---|---|
| D0 | Rotation 1 is fully deterministic | first ~3 minutes | no deviation roll at all while `%zulrah_rotation_count = 0` |
| D1 | The next rotation is re-rolled uniformly from all four, repeats allowed | at every rotation boundary | `%zulrah_rotation = random(4)`; do **not** exclude the previous value |
| D2 | Individual phases may deviate from the pattern | any rotation after the first | per-phase roll; on a hit, substitute a different position/form for that phase only, then resume the pattern |
| D3 | The unpredictable green form | 3+ minutes into a fight | a green phase whose per-attack style is a coin flip instead of `RANGED` or a strict Jad alternation |

D3 is deliberately distinct from a Jad phase and must not be implemented as
one. A Jad phase alternates strictly and *can* be prayed; D3 cannot. The
distinction is the whole point of the Wiki's parenthetical.

**What is not on the Wiki, and must be measured before shipping:**

- `[MEASURE]` the D2 per-phase deviation probability.
- `[MEASURE]` what a deviated phase substitutes — a phase drawn from another
  rotation, a random position/form pair, or a fixed alternative set.
- `[MEASURE]` whether D2 can move the Jad phase, or only ordinary phases.
- `[MEASURE]` the D3 trigger: whether it is a wall-clock threshold, a rotation
  index, or a per-green-phase roll that only becomes reachable late.
- `[MEASURE]` the D3 magic/ranged weighting.

Until these are measured, ship D0 and D1 (both fully specified above) and put
D2/D3 behind a constant defaulting to off, with the disclosure written into the
script header. A deviation model invented from nothing is worse than an absent
one: it makes rotation 1 tests pass while making every long fight wrong in a
way no test can name.

### 7.7 Phase timing budget

The Wiki gives attack *counts* but never *ticks*, and its one timing claim —
"about three minutes" per rotation — does not survive contact with a uniform
3-tick cadence. Counting each token in §7.2–§7.5 as one scripted event:

| Rotation | Phases | Events | ORB total | CLOUD total | 3 ticks/event | Implied per-phase transition to reach 300 ticks |
|---|---|---|---|---|---|---|
| 1 "Crimson A" | 11 | 70 | 11 | 19 | 210 ticks | ~8.2 ticks |
| 2 "Crimson B" | 11 | 70 | 11 | 19 | 210 ticks | ~8.2 ticks |
| 3 "Serp" | 12 | 79 | 16 | 16 | 237 ticks | ~5.3 ticks |
| 4 "Tanz" | 13 | 86 | 22 | 18 | 258 ticks | ~3.2 ticks |

Rotations 1 and 2 are identical on every count in this table — they differ only
in *where* phases 4, 5 and 7 happen, never in what is fired. That is a cheap
regression check on the transcription: if a change makes their totals diverge,
the change is wrong.

The implied transition cost is not constant, so at least one of these is false:
every event takes 3 ticks; every dive/emerge costs the same; or "about three
minutes" is uniform across rotations. The most likely resolution is that a
barrage (`CLOUD`, `ORB`) fires faster than a 3-tick attack — which is what
`snakeboss_attack_acidx3` versus `snakeboss_attack_acidx1` hints at.

**[MEASURE]**, in this order, because everything downstream depends on them:

1. Ticks per ordinary attack (expected: 3, from `attackrate`).
2. Ticks per `CLOUD` barrage and per `ORB`.
3. Ticks for `snakeboss_sinkfast` → reposition → `snakeboss_emergefast`.
4. The crimson telegraph and stun durations (§6.3).

The table above is the calibration target: a correct set of four numbers
reproduces ~180 s for all four rotations, and any set that does not is wrong.
Also cross-check against the CA thresholds — a sub-54-second kill (Zulrah
Speed-Runner) must be reachable, which bounds how long the early phases can be.

### 7.8 Per-phase player tiles

The rotation charts mark an "x" run-to tile for each phase, and those tiles are
the five in §4 (T1–T5). **The mapping from phase to tile exists only inside
`File:Zulrah Patterns.png` and `File:Zulrah Rotations.png`** — there is no
structured version anywhere on the Wiki (`Module:Tile markers/Zulrah.json` is
the only tile-marker module, and it lists the five tiles without phase
attribution).

This is a transcription task, not a design task: read the two chart images and
fill a fifth column into each of the four tables above. It is **not** required
for a correct server — the server places Zulrah, not the player — but it is
required for the test matrix, because "does the player standing on the charted
tile actually avoid this phase's clouds and attacks?" is the only end-to-end
check that the measured positions in §4.1 are right.

---

## 8. Implementing the rotation machine

Encode §7 as **data, not control flow**. The four rotations are 47 phases and
305 scripted events; written as `if`-ladders they will drift from this document
within a week.

Recommended shape — a `zulrah_phase` dbtable, one dbrow per *event*:

```
zulrah_phase:
  rotation      int    1..4
  phase         int    1-based, matches the # column in §7
  step          int    ordinal within the phase
  position      int    ^zulrah_pos_middle | _south | _east | _west
  form          int    ^zulrah_form_green | _crimson | _tanzanite
  event         int    ^zulrah_ev_ranged | _magic | _mixed | _melee
                       | _jad | _orb | _cloud
  count         int    repetitions of this event
  jad_start     int    0 = ranged first, 1 = magic first (jad only)
```

`position` and `form` repeat on every row of a phase — denormalised on purpose,
so a single `db_find` on `(rotation, phase)` yields everything the emerge needs
without a second lookup.

The tree already has the DB opcodes (`DB_*` 7500–7510) and
[drop_table.rs2](../../OSRS-Content/osrs239-content/server/scripts/drop_tables/scripts/drop_table.rs2)
demonstrates the walker pattern.

Runner state, all `%`-scope temp varps:

| Varp | Role |
|---|---|
| `%zulrah_rotation` | 1–4, chosen at fight start and at each boundary (D1) |
| `%zulrah_rotation_count` | 0 for the first rotation; gates D2/D3 |
| `%zulrah_phase` | current phase index |
| `%zulrah_step` | current step within the phase |
| `%zulrah_step_left` | repetitions remaining of the current event |
| `%zulrah_jad_next` | 0 = ranged, 1 = magic; flipped each Jad attack |
| `%zulrah_pending_spawns` | orbs launched but not yet resolved (§6.7) |
| `%zulrah_ticks` | fight length, for D3 and the speed CAs |
| `%zulrah_ca_perfect` | cleared by any of the four Perfect-Zulrah damage sources |

Driven from a single `[ai_timer,snakeboss_boss_*]`, in the shape of
[gauntlet_hunllef.rs2:309-330](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet_hunllef.rs2#L309-L330).

**Phase transition.** `snakeboss_sinkfast` → drain nothing → reposition →
`snakeboss_emergefast` → drain `%zulrah_pending_spawns`. Where the next phase
keeps the same position, prefer `npc_changetype_keepall` (2506): it preserves
the health bar and the in-flight player damage that §6.4 requires to survive a
dive. Where the position moves, `npc_del` + `npc_add` is unavoidable, so the
deferred-damage rule needs explicit handling there.

**Projectiles.** Attacks use `~player_projectile` (returns flight duration, so
one call times the hitsplat, the splash and the sound). Orbs and cloud barrages
are ground-targeted: `projanim_map` for the lob, `loc_add` or `npc_add` on
landing, `spotanim_map` for the impact — the Inferno meteor
([inferno_adds.rs2:529-531](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/scripts/inferno_adds.rs2#L529-L531))
is the exact shape.

### 8.1 Venom-cloud placement

The only footprint the Wiki fully specifies is the opening barrage: it "fills
the area with venom clouds, **leaving the tips on the east and west sides
clear**". Against §4's geometry, the clear tiles are the arm tips —
(2263–2264, 3077–3078) west and (2272–2274, 3077–3078) east.

Every other barrage's tile set is **[MEASURE]**, read from the chart images per
rotation-phase. Two constraints bound any answer:

- Clouds must never seal the island: "certain phases may force the player to
  run through venom clouds in order to re-position themselves" — through, not
  around, and never blocked.
- The charted "x" tile for a phase must be outside that phase's clouds, or the
  chart would be telling players to stand in one.

---

## 9. Entry, exit and death

**Entry.** Board `snakeboss_boat_1op`/`_2ops` → confirm (Quick-Board skips it)
→ build instance → teleport → "a still scene of the shrine will be shown with a
prompted dialogue. Zulrah will appear as soon as the player continues the
dialogue or performs an action." So phase 1 is armed by the dialogue close or
the first input, not by a fixed delay.

Since 28 September 2022, Quick-Board additionally does **not** lock the camera
for the spawn animation — "thus meaning Zulrah can be fought slightly faster".
Both boarding paths must therefore differ in more than the confirmation prompt.

**Exits.** The Wiki lists exactly four: kill, death, logout/hop, teleport.
Every one must free the instance exactly once.

| Path | Handling |
|---|---|
| Kill | drops **under the player** ("much like cave krakens"), `snakeboss_exit` loc placed nearby, player stays until they Read it or teleport |
| Death | elite-diary resurrection first (§6.8) if available; otherwise normal death → Zul-Andra, instance freed |
| Logout / hop | `[logout,_]` proc → Zul-Andra coord, instance freed. Must run before `~map_instance_logout_release` |
| Teleport | no special case; the instance-free is driven by leaving, not by the teleport op |

The Zul-andra teleport scroll appears as the `snakeboss_exit` loc (11701,
`op1=Read`, un-pickupable), not as a ground item.

Collapse the three duplicated `ai_queue3` handlers (G10) into one shared label.

---

## 10. Drop table

Two rolls per kill on the main table; tertiaries roll once. 100% drop first.

**100%** — `snakeboss_scale` ×100–299. (The 5/249 ×500 scale drop is a separate
main-table entry, not part of this.)

**Uniques** — 1/128 chance per kill of hitting the unique table; per roll each
specific item is 1/1024, two rolls:
`blowpipe_fang`, `magic_fang`, `serpentine_visage`, `uncut_onyx`.
Effective rate for *any* Zulrah unique (fang, magic fang, visage, either
mutagen) is ~1/162.5 — use that as the simulation's acceptance target.

**Mutagens** — nested: 10/249 to reach the flax table, then 10/2632 to reach the
mutagen sub-table, then 50/50 between them. Net ≈ 1/13,107 per roll for a
specific mutagen. Implement the real nesting, not a flattened 1/13107 — the
flax drop and the mutagen drop are the *same* roll, and flattening makes them
independently rollable, which is wrong.

| Table | Rarity /roll | Contents |
|---|---|---|
| Flax | 10/249 × 5244/5264 | flax ×1000 (noted) |
| ↳ mutagen | 10/249 × 10/5264 | `cyan_mutagen` or `red_mutagen` |
| Weapons/armour | 10/249 · 2/249 · 2/249 | battlestaff ×10n · dragon med helm · dragon halberd |
| Runes | 12/249 each | death ×250 · law ×200 · chaos ×400 |
| Herbs | 2/249 each | snapdragon ×10n · dwarf weed ×30n · toadflax ×25n · torstol ×10n |
| Seeds | 6/249 ×3, 4/249, 2/249 ×4, 1/249 | palm · papaya ×3 · calquat ×2 · magic · toadflax ×2 · snapdragon · dwarf weed ×2 · torstol · spirit |
| Resources | 11/249 ×2, 10/249 ×2, 8/249 ×4 | snakeskin ×35n · runite ore ×2n · pure essence ×1500n · yew logs ×35n · adamantite bar ×20n · coal ×200n · dragon bones ×12n · mahogany ×50n |
| Shark table | 12/249 | raw shark ×35n (3/8) · shark lure ×70 (3/8) · manta ray ×35n (2/8) |
| Other | 15/249 · 9/249 · 8/249 · 6/249 ×2 · 5/249 ×2 | Zul-andra teleport ×4 · antidote++(4) ×10n · dragonstone bolt tips ×12 · grapes ×250n · coconut ×20n · swamp tar ×1000 · `snakeboss_scale` ×500 |
| Rare drop table | 10/249 | with nature talisman |

**Tertiary** (one roll each): brimstone key (Konar task only, level-725 rate) ·
elite clue 1/75 · `jar_of_swamp` 1/3000 · `snakepet` 1/4000.

Follow the convention in
[drop_tables/scripts/](../../OSRS-Content/osrs239-content/server/scripts/drop_tables/scripts/) —
an `[ai_queue3,<npc>]` handler with a threshold walk and `obj_add(…,
^lootdrop_duration)`. Two departures from the ported LostCity tables:

- The denominator is 249, so roll `random(249)` twice, not the 128-based walk.
- The 22 Feb 2023 change made Zulrah's loot stay on the ground **longer** than
  `^lootdrop_duration` (200 ticks). This table needs its own constant.
  **[MEASURE]** the duration.

---

## 11. Rewards, log and achievements

**Collection log** — a new Zulrah page under
[interface_collection/](../../OSRS-Content/osrs239-content/server/scripts/interface_collection/):
tanzanite fang, magic fang, serpentine visage, uncut onyx, tanzanite mutagen,
magma mutagen, jar of swamp, pet snakeling.

**Combat Achievements** — all 9, 40 points, ids 224–232:

| id | Task | Type | Tier | Requirement |
|---|---|---|---|---|
| 224 | Zulrah Adept | Kill Count | Hard (3) | 25 kills |
| 225 | Zulrah Veteran | Kill Count | Elite (4) | 75 kills |
| 226 | Zulrah Master | Kill Count | Master (5) | 150 kills |
| 227 | Snake Rebound | Mechanical | Elite (4) | kill with Vengeance as the finishing blow |
| 228 | Snake. Snake!? Snaaaaaake! | Mechanical | Elite (4) | kill 3 Snakelings simultaneously |
| 229 | Perfect Zulrah | Perfection | Master (5) | take no damage from Snakelings, Venom Clouds, or the Green or Crimson phase |
| 230 | Zulrah Speed-Trialist | Speed | Elite (4) | < 1:20, off task |
| 231 | Zulrah Speed-Chaser | Speed | Master (5) | < 1:00, off task |
| 232 | Zulrah Speed-Runner | Speed | Grandmaster (6) | < 54 s, off task |

Two of these constrain the design rather than merely reading it:

- **229 Perfect Zulrah** names *four* damage sources and pointedly omits the
  tanzanite phase. So damage must be attributable at its source: the venom-cloud
  tick, each snakeling hit, and each boss attack must each clear
  `%zulrah_ca_perfect` at their own site, and the tanzanite attack must **not**.
  The Gauntlet's `%gauntlet_ca_perfect` is the precedent.
- **228** requires three snakelings to die on the same tick, which means
  snakeling deaths cannot be processed one-per-tick.

Speed tasks need a per-run tick counter started at phase 1 and a "without a
slayer task" check.

**Also**: hard Western Provinces diary task 11 ("Kill Zulrah"), the elite diary
resurrection (§6.8), 500 Slayer XP, and Konar/Nieve/Duradel boss-task
assignment at 3–35 kills per task (14 July 2021) — the tree currently has no
Zulrah entry in `skill_slayer/`.

---

## 12. Engine seams

Everything the fight needs already exists. Confirmed present:

- Instancing: `map_instance_*` 11009–11014
- Form switching preserving state: `npc_changetype_keepall` (2506)
- Ground projectiles and impacts: `projanim_map` (1019), `spotanim_map` (1021)
- Entity projectiles: `projanim_pl` (2095), via `~player_projectile`
- Scheduling: `ai_timer`, `npc_queue` (2531), `queue`/`queuevararg` (2096/2097)
- Player damage funnel: `~player_hit_npc_prepare`
- Venom state machine: `~apply_venom`, `[timer,venom]`
- Data tables: `DB_*` 7500–7510

Three open questions, all to answer before coding starts:

- **Stun.** Does the tree have a player-stun primitive the crimson tail can
  use? If not, scope it explicitly rather than dropping the stun.
- **Loc lifetime.** `loc_add` with a duration is what a venom cloud wants.
  Confirm the revert path is safe here — note the `^max_32bit_int` overflow
  trap that broke Inferno loc reverts at `-O3`.
- **Resurrection.** Intercepting death *before* the normal path, and marking it
  a safe death for HCIM, may need a new hook in the death queue.

---

## 13. Summary of every `[MEASURE]`

Nothing below can be derived from this tree or from the Wiki's text. Each needs
an in-client observation, and each is listed with what depends on it.

| # | Unknown | Blocks |
|---|---|---|
| M1 | The four boss positions' exact 5×5 SW corners (§4.1) | everything visual; all phase tests |
| M2 | Ticks per ordinary attack / per `CLOUD` / per `ORB` (§7.7) | rotation duration, speed CAs |
| M3 | Dive → reposition → emerge cost in ticks (§7.7) | rotation duration |
| M4 | Crimson telegraph length and stun length (§6.3) | phase 2/5/10 of every rotation |
| M5 | Tanzanite magic:ranged weighting (§6.2) | every `MIXED` event |
| M6 | Venom-cloud tile sets per rotation-phase (§8.1) | phase tests, player tiles |
| M7 | Venom-cloud damage per tick and lifetime (§6.6) | Perfect Zulrah, survivability |
| M8 | Snakeling lifetime in ticks (~67?) and the melee:magic split (§6.5) | CA 228, phase tests |
| M9 | D2 deviation probability and substitution rule (§7.6) | long-fight correctness |
| M10 | D3 trigger and its magic:ranged weighting (§7.6) | long-fight correctness |
| M11 | Per-phase player "x" tiles, from the chart images (§7.8) | end-to-end validation of M1 |
| M12 | Loot ground duration post-22-Feb-2023 (§10) | drop tests |
| M13 | `acidx3` vs `acidx1` animation binding (§3.2) | visual parity |
| M14 | Whether R1/R2 phase 9's clouds leave both tips clear (§7.2) | phase 9 test |
| M15 | The `ALT` reading: *n* events total, or *n* of each (§7) | event counts and timing for R1/R2 phase 8, R3 phase 3 |
| M16 | Whether R1 phase 8's "Magic **and** Ranged" differs from every other phase's "and/or" (§7.2) | `MIXED` semantics |

M1, M2 and M3 are the critical path: every other measurement is checkable only
once the boss stands in the right place at the right cadence.

---

## 14. Test matrix

**Automated** — a `zulrah_selftest.rs2` in the minigame directory, following
[gauntlet_selftest.rs2](../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet_selftest.rs2):

1. Instance builds, spans both map squares, contains both pillars at (2265,
   3071) and (2271, 3071), and frees on every one of the four exit paths.
2. **Rotation fidelity**: with `%zulrah_rotation` forced to each of 1–4, the
   emitted (phase, position, form, event, count) sequence equals §7.2–§7.5
   exactly, including R4's 13 phases and its 8-attack Jad.
3. **Bridge phase**: rotation N's final phase is green/middle/`RANGED×5 →
   CLOUD×4`, and the *fight-opening* phase 1 is green/middle/`CLOUD×4` with
   **zero** attacks. The two are distinguishable.
4. **Rotation identification**: phase 2 form uniquely narrows the rotation per
   §7.1, and phase 4's position separates R1 from R2.
5. **Jad alternation**: R1/R2 emit ranged first, R3/R4 magic first; R1–R3 run
   10 attacks, R4 runs 8; the sequence strictly alternates with no repeats.
6. **Jad tails**: R1/R2 phase 9 is followed by `CLOUD×4`; R3 phase 10 is
   followed by nothing.
7. **D0/D1**: rotation 1 never deviates; the boundary re-roll can produce the
   same rotation twice in a row.
8. **Damage cap**: a 100-damage hit lands in 45–50; a 40-damage hit is
   unchanged; a snakeling takes 1 uncapped.
9. **Defence profile**: identical magic hits deal more to green than to blue;
   identical ranged hits the reverse; crimson resists ranged hardest; a fire
   spell out-accurates and out-damages an equivalent non-fire spell by 50%.
10. **Venom**: a boss ranged/magic hit envenoms even through a protection
    prayer; a *miss* does not; a cloud tick damages but does **not** envenom.
11. **Snakelings**: die on boss death; die at the lifetime bound; the 2045/2046
    split is preserved; three dying on one tick fires CA 228.
12. **Deferred spawns**: an orb launched on the dive tick spawns after the
    emerge, not during the dive and not never.
13. **Deferred damage**: a player hit landing on the dive tick registers after
    the emerge.
14. **Post-mortem attack**: an attack launched on the tick Zulrah dies still
    lands, and can kill the player.
15. **Tail attack**: standing on the target tile takes 20–30 and a stun; moving
    two tiles clear takes nothing.
16. **Resurrection**: at 0 HP with elite diary, restores full HP and stats once
    per day, flags a safe death, and does not fire twice.
17. **Drop table**: 100k simulated kills land unique rates within tolerance of
    1/1024 per roll, ~1/162.5 per kill for any unique, 1/13107 per roll per
    mutagen; tertiaries at 1/75, 1/3000, 1/4000; flax and mutagen never both
    drop from one roll.
18. **CAs**: each of 224–232 fires from its own trigger; 229 is cleared
    independently by snakeling damage, cloud damage, green damage and crimson
    damage, and is **not** cleared by tanzanite damage.

**Live client** — the shrine renders with both pillars and the water channel;
dive/emerge animations play at the right positions; venom clouds appear as 3×3
misty locs; the health bar survives a same-position form change; the boat's
Quick-Board op is present before the first board; Quick-Board does not lock the
camera.

**Blocker discipline** — per the tree's rule, prove each assertion can fail by
mutating the implementation before trusting a green run. Test 2 in particular
is worthless unless a deliberately corrupted phase table makes it fail.

---

## 15. Sequencing

Eleven steps, each independently shippable and testable.

| Step | Work | Unblocks |
|---|---|---|
| 1 | **Stats** (§6.1): `gen_npc_stats.py` ledgers for 2042–2046; pin the crimson attack anim in `.combat` | everything |
| 2 | **Measurement pass A** (M1, M2, M3): boss positions and cadence | 5, 6 |
| 3 | **Instance** (§4.2): two-square build, logout proc, exit paths, drop the shared-pad constant | 4 |
| 4 | **Access chain** (§5): roster fix, sacrifice varp, six dialogues, chest gate, telescope | — |
| 5 | **Phase runner** (§8): dbtable schema + `ai_timer` walker, dive/emerge, deferred spawns and deferred damage. Ship with rotation 1 only | 6 |
| 6 | **Attacks** (§6.2–§6.4): ranged, magic, mixed, tail, Jad alternation, damage cap, animation-time resolution, envenoming | 7 |
| 7 | **Measurement pass B** (M4–M8, M11): telegraphs, weightings, cloud tile sets, player tiles | 8 |
| 8 | **Adds** (§6.5, §6.6): snakelings, orbs, venom clouds | 9 |
| 9 | **All four rotations** (§7.2–§7.5) plus D0/D1 (§7.6) | 10 |
| 10 | **Loot** (§10): two-roll table, nested mutagens, under-player placement, `snakeboss_exit` scroll | 11 |
| 11 | **Meta** (§11, §6.8): collection log, 9 CAs, both diary hooks, resurrection, Slayer assignment | — |

D2/D3 (§7.6) land after M9/M10 and are explicitly *not* on this critical path —
they ship behind a default-off constant until measured.

Steps 3 and 4 are independent of 1 and 2 and can run in parallel. Step 2 is the
critical path for everything visual: it is the only step whose output cannot be
derived from the tree or the Wiki text, and shipping guessed coordinates would
make every later step's tests meaningless.
