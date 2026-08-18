# Chambers of Xeric — implementation plan

Status: **partially built, not yet running.**
[`minigame_cox/`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/)
is ~3,400 lines across 18 scripts and three config files: Tekton, Olm, Vasa,
Vespula, the Vanguards, the Guardians, the minion packs, the crab and thieving
puzzles, points, scaling and rewards all have code. None of it ticks — see
**§11.3 F0**: 21 `[ai_timer]` hooks and no `npc_settimer` anywhere, so every
heartbeat in the raid is unarmed.

> ⏱️ **§11 is the tick-level audit** (2026-08-17): what measures CoX and what
> does not (Blert does **not** cover it yet), every published tick constant with
> its Jagex quote, and 22 findings ranked against the implementation. Read §11.3
> before touching an encounter — several constants below lost their argument.

This document is the room-by-room specification, the reference index, and the
build order. It is meant to be read alongside two sources, both linked from
every item:

> 📐 **The tick-by-tick behavioural spec now lives in
> [`COX_MECHANICS.md`](COX_MECHANICS.md)** — Olm's 12-step rotation and 4-tick
> action clock, the head-turn skip/catch-up rule, per-room attack cadences,
> the verified room library, exact thieving-chest tables, the scaling formulas,
> and a list of every source conflict left unresolved. Read it before
> implementing any encounter. This file remains the build order.

| Tag | Source |
| --- | --- |
| **W** | [OSRS Wiki](https://oldschool.runescape.wiki/w/Chambers_of_Xeric) — the authority on numbers |
| **T** | [`synq_transcript.md`](synq_transcript.md) — Synq, *Remastered Solo Raids Guide 2026*, 3:54:55, 99 chapters — the authority on **behaviour a player actually observes** (tick counts, pathing, head turns, room solve orders) |
| **P** | [`sources/`](sources/) — RuneLite/OpenOSRS plugin code (exact tick constants), plus two more guide transcripts |

Every **T** link is a deep-link into the video; the same chapter title is a `##`
heading in [`synq_transcript.md`](synq_transcript.md), so
`grep -n "Head Turn" synq_transcript.md` lands on it. The transcript is
machine-transcribed ASR — treat spoken numbers as a **claim to verify**, never as
a constant to paste. Where the wiki and the transcript disagree, the wiki wins
for stats and the transcript wins for tick-level behaviour.

Confidence markers used below:

- ✅ read off this repo's cache / source — trustworthy.
- 📖 from the wiki via summarisation — **re-read the page before encoding it as a constant.**
- 🎥 from the transcript — behavioural, verify by building it and watching.
- ❓ unknown, listed as an explicit research task.

---

## 0. What the cache already ships

This is the single biggest fact about this project: **the rev239 cache already
contains essentially all of Chambers of Xeric.** Nothing needs to be modelled,
animated, or map-authored. The work is server-side content plus a small amount
of engine plumbing.

### 0.1 NPCs ✅

All ids below are verified from
[`configs/all.npc.compack`](../../../OSRS-Content/osrs239-content/configs/all.npc.compack).

| Room | Cache ids |
| --- | --- |
| Tekton | 7540 `raids_tekton_waiting`, 7541 `_walking_standard`, 7542 `_fighting_standard`, 7543 `_walking_enraged`, 7544 `_fighting_enraged`, 7545 `_hammering` |
| Vanguards | 7525 `raids_vanguard_dormant`, 7526 `_walking`, 7527 `_melee`, 7528 `_ranged`, 7529 `_magic` |
| Vespula | 7530 `raids_vespula_flying`, 7531 `_enraged`, 7532 `_walking`, 7533 `_portal`, 7534–7537 `_caterpillar_healthy/sickly/infected/dead`, 7538 `_vespine_flying`, 7539 `_vespine_walking` |
| Vasa Nistirio | 7565 `raids_vasanistirio_dormant`, 7566 `_walking`, 7567 `_healing`, 7568 `_crystal` |
| Muttadiles | 7561 `raids_dogodile_submerged`, 7562 `_junior`, 7563 `raids_dogodile`, 7564 `_meat_tree` |
| Guardians | 7569 `raids_stoneguardians_left`, 7570 `_right`, 7571 `_left_dead`, 7572 `_right_dead` |
| Lizardman shamans | 7573 `raids_lizardshaman_a`, 7574 `_b`, 7575 `_blocker` |
| Skeletal mystics | 7604 `raids_skeletonmystic_a`, 7605 `_b`, 7606 `_c` |
| Ice demon | 7584 `raids_icedemon_noncombat`, 7585 `_combat`, 7586 `raids_icefiend` |
| Jewelled crabs | 7576–7579 `raids_lasercrabs_crab_grey/red/green/blue`, 7580–7583 `_energy_white/red/green/blue` |
| Tightrope | 7559 `raids_tightrope_ranger`, 7560 `raids_tightrope_mage` |
| Thieving | 7602 `raids_thievingchest_beast_active`, 7603 `_sleeping` |
| Scavengers | 7548 `raids_scavenger_beast_a`, 7549 `_b` |
| Great Olm | 7550 `olm_hand_right_spawning`, 7551 `olm_head_spawning`, 7552 `olm_hand_left_spawning`, 7553 `olm_hand_right`, 7554 `olm_head`, 7555 `olm_hand_left`, 7556 `olm_hand_right_dying`, 7557 `olm_hand_left_dying`, 7558 `olm_firewall_npc` |
| Supplies | 7587–7593 `raids_bat_0..6`, 7594 `raids_fishing_snake` |
| Lobby / misc | 7489 `raids_corridor_boulder`, 7490 `raids_party_groupholder_lobby`, 7491 `_dungeon`, 7520 `raids_olm_pet`, 7599/7600 `raids_mountainguide_*`, 7601 `raids_xerician_priest` |

> ✅ **Naming trap — resolved, task R1 closed. The cache is right, the wiki is
> wrong.** The wiki calls 7551 the normal head and 7554 the challenge head 📖.
> There is no separate challenge-mode id in this cache. `vislevel` settles it:
>
> | id | name | vislevel | `op2=Attack`? | category |
> | --- | --- | --- | --- | --- |
> | 7554 | `olm_head` | **1043** | yes | 1024 |
> | 7555 | `olm_hand_left` | **750** | yes | 1024 |
> | 7553 | `olm_hand_right` | **549** | yes | 1024 |
> | 7551 | `olm_head_spawning` | 0 | no | 1023 |
> | 7552 | `olm_hand_left_spawning` | 0 | no | 1023 |
> | 7550 | `olm_hand_right_spawning` | 0 | no | 1023 |
>
> 1043 / 750 / 549 are exactly the combat levels the wiki lists for head / left
> claw / right claw. The `_spawning` records are the un-attackable spawn-in
> forms — vislevel 0, no attack op, a different category. The split is
> **spawning vs live**, not normal vs challenge.
>
> **Bonus corroboration of the damage gating.** The live records carry defence
> params that match the wiki's mitigation rules structurally ✅:
>
> | | stab/slash/crush def | magic def |
> | --- | --- | --- |
> | `olm_head` | 200 | 200 |
> | `olm_hand_left` | **50** | 200 |
> | `olm_hand_right` | 200 | **50** |
>
> Left hand is soft to melee, right hand is soft to magic, and the head is hard
> to both — leaving ranged. That is the wiki's "66% mitigation from non-melee /
> non-magic / non-ranged" rule expressed as defence bonuses, from an independent
> source. Encode the gating, but note the cache already biases it the right way.
>
> ⚠️ None of the live records carry `hitpoints` or combat `statN` rows — only
> the `_spawning` ones have `stat4=100`. HP and combat stats must come from an
> authored `.npc` block, as always in this tree.

### 0.2 Locs ✅

259 `raids_*` locs in
[`configs/all.loc.compack`](../../../OSRS-Content/osrs239-content/configs/all.loc.compack).
The ones that carry mechanics:

| Purpose | Loc ids |
| --- | --- |
| Entrance / exit / party | 29777 `raids_entrance_steps`, 29778 `raids_exit_steps`, 29776 `raids_party_recruitment` |
| Floor traversal | 29734 `raids_descentto2`, 29735 `raids_bossentrance` |
| Corridor blockers | 29736/29737 `raids_corridor_roots`/`_cleared`, 29738/29739 `raids_corridor_rocks`/`_cleared`, 29740 `raids_corridor_boulder` |
| Thieving | 29742/29743 `raids_thievingchest_closed`/`_open`, 29744/29745 `_eggs_whole`/`_hatched`, 29746 `_foodtrough_empty` |
| Ice demon | 29747/29748 `raids_icedemon_brazier_unlit`/`_lit`, 29763/29764 `raids_woodsource_roots`/`_depleted` |
| Tightrope | 29749 `raids_tightrope_barrier`, 29750 `_end`, 29751 `_keystone_loc` |
| Jewelled crabs | 29711 `raids_lasercrabs_hammer`, 29752 `_xeric_relief`, 29753–29757 `_bigcrystal_1..5`, 29758–29762 `_smallcrystal_black/cyan/magenta/yellow/white` |
| Vasa | 29774/29775 `raids_vasanistirio_crystal_on`/`_off` |
| Resource room | 29765 `raids_patch_empty`, 29769 `raids_storage_hotspot`, 29770/29779/29780 `raids_storage_1/2/3`, 29771 `raids_farming_tools`, 29772 `raids_gourd_tree`, 29773 `raids_weeds` |
| Reward | 28848 `raids_reward_lootbeam` |
| Boss books (scouting) | 12310 `raids_vasanistirio_book_vis`, 13482–13485 `raids_tekton/vespula/vanguard/houndmaster_book_vis` |

### 0.3 Maps — **measured**, task R2 closed ✅

[`tools/cox_template_survey.py`](../../../tools/cox_template_survey.py) measures
the template block off the `.jl2` map data and emits
[`tools/data/cox_templates.json`](../../../tools/data/cox_templates.json).
Re-run it with `python3 tools/cox_template_survey.py`; `--sweep` re-derives the
block if the cache revision changes.

Findings:

| Quantity | Value |
| --- | --- |
| Template block | `m50_89`, `m51_80`–`m51_85`, `m51_89`, `m52_80`–`m52_85`, `m52_89` |
| **Room pitch** | **32 tiles = 4 zones** |
| Rooms per map square | 2 × 2 per plane |
| Planes used per square | up to 3 (0, 1, 2) |
| Occupied room cells | **65**, across 15 squares |

⚠️ My original guess of `m50_89`–`m52_95` was **wrong** — the bulk of the raid
sits at mz **80–85**, not 89–95. The `--sweep` mode exists because of that miss;
never hand-pick the block again.

**How the pitch was measured.** Two methods failed first and both failures are
recorded in the tool's docstring so nobody retries them:

- *Wall-loc gaps* — `raids_wall_*` is decorative scenery blanketing every column
  (48–67 locs per column out of 64 rows), so there are no gaps to measure.
- *Position mod P* — assumes each copy of a room places markers at the same
  offset. False: the two ice demon rooms in `m51_83` have visibly different
  brazier arrangements. Scores were flat (0.18–0.27) for every P from 8 to 64.

What works is **translational self-similarity**: score candidate offsets by how
many loc placements they map onto another placement of the same loc. `(32,0)`
and `(0,32)` win on five independent square/plane combinations, mapping 28–50%
of placements. It is not 100% precisely because neighbouring cells hold
*different* rooms — only shared structural scenery translates — which is itself
the confirmation that the grid is real.

**Only puzzle rooms are identifiable from the cache.** The survey names 24 of
65 cells (lobby, olm, resource, crabs, ice demon, tightrope, thieving). The
combat-room cells contain nothing but terrain scenery (`raids_floor_*`,
`raids_rocks*`, `raids_plant*`) because a combat room is defined by the NPCs the
raid spawns into it, and those spawns lived in Jagex's raid code, not in a cache
table. **This is not a gap to close** — we write the layout code, so we choose
those cells. Only the puzzle rooms must be taken as the cache laid them out.

The Mount Quidamortem surface (`m19_55`) is already the stub's home coord.

### 0.4 What is *not* wired — **done**, with a correction

Namespace entries added: **+29 npc** (all 8 Olm records, the tightrope pair, the
guardians, the meat tree, the Vasa crystal, the lobby npcs) and **+121 loc**
(every `raids_*` loc carrying an `op` or a `name` — the 138 pure-scenery records
were deliberately left out, since they have no server half to give them).

> ⚠️ **Correction to an earlier claim in this plan.** I originally wrote that the
> missing `pack/npc.server` entries meant "scripts cannot address the Olm at
> all". **That was wrong.** `sscompile` loads symbols only from files suffixed
> `.pack`, `.compack` or `.alloc`
> ([`ssc_symbols.c:667`](../../../src/serverscript/ssc_symbols.c#L667)) — it
> never opens `npc.server` or `loc.server`. Script symbols resolve from
> `configs/all.npc.compack`, which already had every raid name.
>
> Proved by A/B: removing `olm_head` from `pack/npc.server` and recompiling a
> script that calls `npc_add(coord, olm_head, 0)` produces **no error**.
>
> What `*.server` actually controls is **cache packing** — per the file's own
> header, "a record gets a server band only if `npc.server` names it". So the
> additions are still necessary, just for a different reason and at a different
> stage. They were never a blocker on writing scripts.

Still open:

- ❌ No obj namespace entries for the reward items.
- ❌ No `.npc` authored stat blocks — hitpoints and combat stats come from
  authored `.npc` only, never from the cache's `statN=` opcodes.

> ⚙️ **Build note: this tree is edited concurrently — use the harness.**
> `sscompile` produced errors that moved between unrelated files run to run on
> identical CoX input: `quest_troll.rs2:177`, `quest_troll.rs2:268`,
> `combat.rs2:354`, `flamtaer_temple.rs2:2`. Every one was in a file whose
> modification timestamp fell **inside this session's own window** — another
> session mid-edit. Because `sscompile` stops at the first error, their edit
> hides whether yours is clean.
>
> **[`tools/cox_compile_check.sh`](../../../tools/cox_compile_check.sh)** solves
> this: it stages a throwaway copy of the tree with every other session's script
> edits reverted to HEAD, leaving the CoX package as the only working-tree
> change. Run `--selftest` to have it append a deliberately broken proc to a CoX
> file first and confirm the compiler reports it — a gate that cannot fail is
> not a gate.
>
> Two traps the harness encodes so nobody rediscovers them:
> - Pointing `--src` at a bare copy of `server/scripts` drops components from
>   26,951 to 351 and produces a cascade of bogus "not a symbol" errors.
>   `interfaces/` must be copied alongside it.
> - **Do not revert `pack/*.alloc`.** They are shared id ledgers; reverting them
>   to HEAD desynchronises them from already-committed scripts and breaks
>   unrelated content.
>
> The practical rule either way: **a failing `sscompile` here is not evidence
> about your change until you have isolated it.** My own first instinct was to
> blame a fixed-size table in the compiler; the timestamps ruled that out.

---

## 1. Architecture

### 1.1 The map-instance ceiling is the design constraint ✅

From [`mock230_mapinstance.h`](../../../src/net/mock/mock230_mapinstance.h):

```
MOCK230_MAPINSTANCE_MAX          8    concurrent instances
MOCK230_MAPINSTANCE_ZONES       16    zones per axis per reservation  (128 tiles)
MOCK230_MAPINSTANCE_SCENE_ZONES 13    zones the wire can describe     (104 tiles)
MOCK230_MAPINSTANCE_LEVELS       4    planes
```

With the room pitch now measured at **4 zones** (§0.3), the geometry resolves —
and it resolves in our favour. **Neither engine change E1 nor E2 is needed.**

| | Rooms | Grid | Zones | Fits in 16? |
| --- | --- | --- | --- | --- |
| Normal floor | 7–8 📖 | 4 × 2 | 16 × 8 | ✅ exactly |
| CM top floor | 6 📖 | 3 × 2 | 12 × 8 | ✅ |
| CM middle floor | 6 📖 | 3 × 2 | 12 × 8 | ✅ |
| CM lower floor | 6 📖 | 3 × 2 | 12 × 8 | ✅ |

A floor laid out as a **row** would need 8 × 4 = 32 zones and would not fit. As
a **4×2 packing** it lands on exactly 16 — the existing cap, with none to spare.
That is the design constraint: *CoX floors are packed 4 rooms wide, not 8.*

Planes are equally tight but sufficient. A template room occupies one plane of
one template square, and gets stamped onto whichever instance level we choose:

| | Levels needed |
| --- | --- |
| Normal: 2 room floors + Olm | 3 of 4 ✅ |
| Challenge: 3 room floors + Olm | **4 of 4** ✅ exactly |

⚠️ Zero headroom on both axes. Any later feature wanting a fifth plane or a
wider floor forces an engine change. Record that here rather than discovering it
mid-build.

**8 concurrent instances** caps concurrent raids. Fine for now; note it.

### 1.2 The Gauntlet is the architectural precedent ✅

[`minigame_gauntlet`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/)
(4,659 lines) already solves the same shape of problem: a seeded room graph, a
map instance assembled zone-by-zone from template squares, per-instance state,
and a boss with a tick-scripted attack machine. Read these four first:

- [`gauntlet_layout.rs2`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet_layout.rs2)
  — seeded graph, room→(source square, rotation) mapping. **The direct model for CoX layout generation.**
- [`gauntlet.rs2:139`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet.rs2#L139)
  — the `map_instance_setchunk` loop that stamps template zones into the instance.
- [`gauntlet_map_state.rs2`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet_map_state.rs2)
  — per-instance state via `map_instance_flag_get/set`.
- [`gauntlet_hunllef.rs2`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/scripts/gauntlet_hunllef.rs2)
  (643 lines) — a tick-driven boss attack cycle. The Olm is this, times four.

Engine ops available ✅: `map_instance_alloc`, `_setchunk`, `_build`, `_free`,
`_coord`, `_find`, `_owner`, `_flag_get`, `_flag_set`.

### 1.3 Where state lives

CoX has more per-raid state than any existing content here: points (per player
*and* per team), room completion flags, per-room mechanic state, supply
inventories, and the Olm phase machine. Proposed split:

| State | Home | Why |
| --- | --- | --- |
| Room graph seed, floor layout | instance flag | Reconstructable from the seed, exactly like `%gauntlet_layout_seed` |
| Room completion bitmap | instance flag | Session-local, must not reach the save |
| Per-player points | `varp` scope=temp | Per player, must survive room transitions, must not persist |
| Team total points | instance flag | One writer |
| Olm phase / hand HP / cycle index | instance flags | Tick-hot; keep out of varps |
| Raid supplies (potions, food) | a raid-scoped `inv` | Must be destroyed on exit — CoX items cannot leave 📖 |
| Storage unit contents | player-scoped `inv` | Persists between raids, emptied on re-entry 📖 |

⚠️ Instance flags are a small fixed set. Count what CoX needs against what
`mock230_mapinstance.h` actually provides before committing — this may need a
proper per-instance content struct rather than a flag array.

### 1.4 Engine work anticipated

These are the pieces no existing content exercises. Each is a plausible source
of the "it silently does nothing" failures this tree keeps hitting.

| # | Gap | Notes |
| --- | --- | --- |
| ~~E1~~ | ~~`LEVELS` 4 → 5~~ | **Not needed** — 4 planes is exactly enough (§1.1) |
| ~~E2~~ | ~~`ZONES` 16 → more~~ | **Not needed** — a 4×2 room packing lands on exactly 16 (§1.1) |
| E3 | Per-instance content state larger than flags | §1.3 |
| E4 | Damage→points accounting hook | Needs a seam in the combat damage path, not a script-side approximation |
| E5 | Multi-tile NPC anchored at non-SW corner | The Olm is huge and its hands are separate NPCs on a shared body. See [`npc-footprint-vs-anchor-bugs`] — view range and combat reach both measured the SW corner before |
| E6 | NPC that is attackable but not pathable-to (Olm head) | Head is behind the hands |
| E7 | Prayer-drain aura (Vespula portal, 3 pts / 2 ticks 📖) | No existing content drains prayer by proximity |
| E8 | Projectile-to-tile ground effects (acid pools, flame walls, falling crystals) | Precedent exists in Inferno/Zuk and the QBD wall (`MAP_ANIM` per row) — reuse, don't re-invent |

---

## 2. Room catalogue

Each room below lists: cache assets ✅, the mechanics that must be simulated,
and the implementation note. **W** = wiki page, **T** = transcript chapter.

### 2.1 Combat rooms

---

#### Tekton

**W** [Tekton](https://oldschool.runescape.wiki/w/Tekton) ·
**T** [Tekton 1:09:31](https://www.youtube.com/watch?v=klhBxOH8reQ&t=4171),
[Spec Start 1:14:47](https://www.youtube.com/watch?v=klhBxOH8reQ&t=4487),
[CM Skip 1:08:07](https://www.youtube.com/watch?v=klhBxOH8reQ&t=4087)

Cache ✅: 7540 waiting → 7541 walking_standard → 7542 fighting_standard →
7545 hammering → 7543 walking_enraged → 7544 fighting_enraged. The six-NPC split
*is* the state machine — Jagex encoded Tekton's phases as npc transforms, so the
script's job is choosing the transform, not inventing states.

Stats 📖: 300 HP normal / 450 enraged; att 390/585, str 390/585, def 205/246,
mag+rng 205. Attack speed 3 ticks. Max hit 52 / 78. Melee only, **immune to
ranged**, magic def +0 with 20% water weakness. Def bonuses stab +155/+280,
slash +165/+290, crush +105/+180.

Mechanics to build:
1. **Directional melee** 📖 — hits everyone in front of *and to the right of*
   him, not just the target. Needs an AoE hit shape, not a single-target swing.
2. **Anvil return** 📖🎥 — after ~10–14 attacks he walks back to the anvil and
   repairs ~1.11–1.33% HP every few ticks over ~10s, then transforms enraged.
   The `_walking_*` and `_hammering` npcs cover this.
3. **Spark AoE during the repair** 📖 — 3×3 targeted at every player; dodged by
   standing ≥2 tiles apart 🎥.
4. **First DWH / elder maul spec always lands** 📖 — an explicit special-case
   in the accuracy roll. BGS drains 10 def on a miss, DWH drains 5% on a miss 📖.
   ⚠️ This is a *rule*, not a coincidence — encode it, don't hope the RNG does it.
5. Immune to binding 📖.

Impl note: the anvil walk is the same "latch and act on arrival" shape as
[`interaction-pathing-parity`]. Reuse `[ai_*]` triggers as in the existing stub,
which already binds `ai_queue3` on three of the six Tekton npcs.

---

#### Vasa Nistirio

**W** [Vasa Nistirio](https://oldschool.runescape.wiki/w/Vasa_Nistirio) ·
**T** [Vasa 0:55:16](https://www.youtube.com/watch?v=klhBxOH8reQ&t=3316)

Cache ✅: 7565 dormant → 7566 walking → 7567 healing; 7568 `_crystal` is the
corner crystal NPC; locs 29774/29775 `_crystal_on`/`_off`.

Stats 📖: 300 HP (450 CM). att 1, str 1, def 175, mag 230, rng 230. Speed 3
ticks. Def: stab +170, slash +190, **crush +40**, magic +400, ranged +40/+40/+30.
Poison/venom immune. Stat regen 1 per 10 ticks (vs standard 1/100) 📖.

Mechanics to build:
1. **Opening teleport** 📖 — grabs everyone; half go to the room edges, half go
   adjacent to him. The adjacent group is **stunned with prayers disabled** and
   takes `(current HP − 5)` split across the absorbing players. The edge group
   must run in and get Protect from Magic up before the barrage ends.
2. **Corner crystal healing** 📖 — after the teleport he activates one of four
   corner crystals and walks to it. Crystal is **ranged-immune**, takes 66% less
   from magic, resists crush and slash — **stab is the answer**. Healing runs at
   1% per 2 ticks plus (10% + 1) defence per cycle.
3. **The 40-second timer** 📖 — ~66–67 ticks from when he *reaches* the crystal.
   Beat it and healing is negated; miss it and he fully heals and re-teleports.
   ⚠️ The timer starts on arrival, not on activation. Getting this wrong makes
   the room either trivial or impossible.
4. **Boulders + stomp** 📖 — ranged boulders at anything within 8 tiles; up to 8
   damage per tick stomp on anyone underneath.

---

#### Vespula

**W** [Vespula](https://oldschool.runescape.wiki/w/Vespula),
[Abyssal portal](https://oldschool.runescape.wiki/w/Abyssal_portal) ·
**T** [Vespula 1:25:34](https://www.youtube.com/watch?v=klhBxOH8reQ&t=5134)

Cache ✅: 7530 flying, 7531 enraged, 7532 walking, 7533 portal, 7534–7537 the
four caterpillar (lux grub) health states, 7538/7539 vespine soldier.
**The four grub states are cache npcs — the grub health mechanic is a transform
chain, not a hitpoints bar.**

Stats 📖: Vespula 200 HP, 5×5, speed 3 ticks. Max hits 14 ranged / 8 stomp /
20 sting. 50% fire weakness. Poison/venom immune. Stat regen 1/10 ticks.
Portal: 250 HP, magic def +60, ranged def +140/+140/+110, regen 1 HP / 45 ticks,
50% fire weakness 📖.

Mechanics to build:
1. **Grub decay chain** 📖 — Vespula stings her own grubs; they walk
   healthy → sickly → infected → dead. A dead grub becomes a **vespine soldier**
   (7538/7539), which poisons for 15 and **heals both Vespula and the portal
   while alive**. Players heal grubs with medivaemia blossoms.
2. **Portal prayer drain** 📖 — 3 prayer points every 2 ticks to anyone in
   range; 3 damage instead if prayer is already empty. **Engine gap E7.**
3. **Flight phase** 📖 — while airborne she is immune to melee *except
   halberds* (Aug 2025 change). The portal cannot be safely killed.
4. **Grounding** 📖 — at ~20–23% HP she lands for ~20s; only then can the portal
   be destroyed to end the fight. ⚠️ Wiki text says both "20%" and "below 23%"
   in different places — resolve before encoding.
5. **Enrage** 📖 — attacking the portal while she flies triggers her to fly back
   and forth stinging for 5–20 typeless and stomping 2–8/tick underneath.
6. Guaranteed overload drop on clear 📖.

---

#### Vanguards

**W** [Vanguard](https://oldschool.runescape.wiki/w/Vanguard) ·
**T** [Vanguards 1:38:49](https://www.youtube.com/watch?v=klhBxOH8reQ&t=5929)

Cache ✅: 7525 dormant, 7526 walking, 7527 melee, 7528 ranged, 7529 magic.

Stats 📖: 180 HP each (280 CM), att/str/mag/rng 150, def 160 (240 CM). Speed 4
ticks. Max hit 22. Each attacks **three times per attack** — ranged and magic
send one at the main target and two elsewhere.

Mechanics to build:
1. **HP-sync force-heal** 📖 — this is the whole room. If any vanguard's HP
   diverges from the others by **40%** (parties ≤4) or **33.3%** (parties ≥5),
   **all three instantly heal to full**. A swirling energy effect brightens as
   the party approaches the threshold — that's a client-visible warning that
   needs a spotanim/graphic driven by the same computation.
2. **Style triangle** 📖 — ranged vanguard weak to melee, melee weak to magic,
   magic weak to ranged. Identify by model: rocks under tentacles = ranged,
   tentacles raised = melee, no tentacles = magic.
3. **Shuffle** 📖 — each vanguard independently rotates position every **20–36
   ticks**, using 7526 `_walking`. Edge positioning minimises the unavoidable
   stomp 🎥.

Impl note: the force-heal check runs on *every* damage application, not on a
timer. Put it in one proc called from the damage hook so there is a single
writer.

---

#### Muttadiles

**W** [Muttadile](https://oldschool.runescape.wiki/w/Muttadile) ·
**T** [Muttadiles 0:42:50](https://www.youtube.com/watch?v=klhBxOH8reQ&t=2570)

Cache ✅: 7561 `_submerged`, 7562 `_junior` (small), 7563 `raids_dogodile`
(large), 7564 `_meat_tree`. Loc 29767 `raids_dogodile_tendrils_entrance`.

Stats 📖: both 250 HP (large 375 CM), att/str 150, def 138, rng 150, mag 1.
Speed 4 ticks. Small: melee 28 / ranged 30. Large: melee 41 / ranged 44 /
magic 23, **stomp up to 72** (106 CM). 40% earth weakness (June 2025).

Mechanics to build:
1. **Submerged phase** 📖 — while the small one lives the large one is 7561
   `_submerged` and attacks with erratic magic at 1/3 hit chance per player.
2. **Style lock** 📖 — at range the large one holds one combat style for a
   **minimum of three auto-attacks** before switching. Not per-attack random.
3. **Meat tree healing** 📖 — at ~50% HP it goes for the tree, heals up to
   40–50%, **max three times**, and gives up after ~15s without access. Chopping
   the tree (or binding it away from the tree) prevents this. Woodcutting level
   sets the tree's HP (100–500 at 1–99) and the player's accuracy. Chopping is
   also a **points source**.
   ⚠️ The wiki summary gave both "up to 50%" and "up to 40%" — resolve.
4. **Stomp** 📖 — in melee range, the shockwave; avoid by positioning 🎥.

---

#### Lizardman shamans

**W** [Lizardman shaman (CoX)](https://oldschool.runescape.wiki/w/Lizardman_shaman_(Chambers_of_Xeric)) ·
**T** [Shamans 0:34:38](https://www.youtube.com/watch?v=klhBxOH8reQ&t=2078)

Cache ✅: 7573 `_a`, 7574 `_b`, 7575 `_blocker`. Loc 29768
`raids_shaman_tendrils_entrance`.

Stats 📖: 190 HP (285 CM), att/str/mag/rng 130, def 210. Speed 4 ticks. 3×3.
Poison level 12. Att bonuses stab +58, slash +160, crush +150, rng +56. Def
bonuses stab +102, slash +160, crush +150, magic +160, **ranged +0**. Stat regen
1 per 20 ticks.

Mechanics to build:
1. **Count scales with team** 📖 — 2 for solo/small, up to 5 for large teams.
   Room strategy page says 2–4 📖 — ⚠️ conflicting, resolve.
2. **Spawn special** — the jumping minions from the surface shaman. Reuse
   whatever `minigame_shaman` already implements; check it first.
3. **Poison blob** 📖 — ~25% stronger than surface, up to 39–40 damage.
4. Safespottable at 10+ tiles 🎥.

Impl note: `minigames/minigame_shaman` already exists — read it before writing
anything here.

---

#### Skeletal mystics

**W** [Skeletal mystic](https://oldschool.runescape.wiki/w/Skeletal_mystic) ·
**T** [Skeletal Mystics 0:31:53](https://www.youtube.com/watch?v=klhBxOH8reQ&t=1913)

Cache ✅: 7604 `_a`, 7605 `_b`, 7606 `_c`. Loc 29741 `raids_skeletalmystics_symbol`.

Stats 📖: 160 HP (240 CM), att/str/mag 140, def 187, rng 1. Speed 4 ticks.
Attack range 10. Magic att +85, magic dmg +40, magic str +38, str +50.
Def stab/slash +155, **crush +75**, magic +140, ranged +115/+115/+75.

Mechanics to build:
1. **Three attacks** 📖 — a standard magic attack, a *damaging Vulnerability*,
   and melee.
2. **Prayer reaction** 📖 — they read the target's overhead prayer and react
   based on position. Protect from Magic cuts their accuracy and damage ~50%.
3. **Not safespottable** 📖 — but corner positioning forces melee-only 📖🎥.
   ⚠️ These two claims are in tension; the mechanism is presumably "if no
   ranged line, walk into melee". Verify.
4. Count 3–12 by party size 📖.

---

#### Guardians

**W** [Guardian (CoX)](https://oldschool.runescape.wiki/w/Guardian_(Chambers_of_Xeric)) ·
**T** [Guardians 0:30:39](https://www.youtube.com/watch?v=klhBxOH8reQ&t=1839)

Cache ✅: 7569 left, 7570 right, 7571/7572 the dead variants. Always exactly two 📖.

Stats 📖: 250 HP (375 CM), att/str 140, def 100, mag/rng 1. Speed 4 ticks.
Max hit 20, slash. Def stab +80, slash +180, **crush −10**. Immune to thralls,
poison, venom. Stat regen 1 per 8 ticks.

Mechanics to build:
1. **Pickaxe-only damage** 📖 — every other damage source is **reduced to 0**.
   Damage multiplier `D = (50 + Mining level + pickaxe level req) / 150`.
   Crystal pickaxe caps at dragon-pickaxe damage.
   ⚠️ This is a damage *gate*, not a weapon requirement — attacks still land.
2. **HP scaling** 📖 — `H = 151 × (1 + ⌊T × 12⌋) + ⌊M̄⌋ × T` where T = team size,
   M̄ = average Mining level. ⚠️ That transcription is almost certainly mangled
   (the `⌊T × 12⌋` term is nonsense for T=1). **Re-read the page.**
3. **Stomp** 📖 — 3×3 ceiling rocks at the attacking player's tile. Defeated by
   the flinch: attack, immediately step 2 tiles away 🎥.
4. **Points from being pushed** 📖 — knockback damage when entering the passage
   early awards points.

---

### 2.2 Puzzle rooms

---

#### Ice demon

**W** [Ice demon](https://oldschool.runescape.wiki/w/Ice_demon) ·
**T** [Ice Demon 0:14:39](https://www.youtube.com/watch?v=klhBxOH8reQ&t=879)

Cache ✅: 7584 noncombat, 7585 combat, 7586 `raids_icefiend`. Locs 29747/29748
brazier unlit/lit, 29763/29764 wood source roots/depleted.

Stats 📖: 140 HP, att 1, str 1, def 160, **mag 390, rng 390**. Speed 3 ticks.
Def melee +70/+70/+110, magic +40, ranged +140. Poison/venom immune.

Mechanics to build:
1. **67% damage reduction on everything** 📖 except fire spells (250%, later
   150% weakness) and demonbane weapons (115% as of July 2026). This is the
   room's identity — a non-fire setup essentially cannot kill it.
2. **The brazier gate** 📖 — four braziers must be lit with kindling before the
   demon can be engaged. Kindling comes from chopping the roots.
   - Firemaking success 8% at level 1 → 78% at 99 📖.
   - Kindling per swing scales with Woodcutting: **+1 per 12 levels**, max 8 at
     level 96+ 📖.
   - Tree depletion rate ×5 vs standard in this room 📖.
3. **Icefiends** 📖 — one per brazier, try to extinguish it every 3–4 ticks with
   a **1/6 chance of succeeding**.
4. **Prayer-reactive attacks** 📖 — Protect from Missiles → it only throws snow
   boulders; Protect from Magic → it only casts Ice Burst. Both 3×3 AoE.
   ⚠️ Note the inversion: praying against a style makes it use *that* style.
   Read the page again before encoding — this is the kind of thing a summary
   flips.
5. **Points** — kindling deposits, ~6,050 max in normal mode 📖.
6. Drops 📖: 1 fiendish ashes + 7 stinkhorn mushroom + 7 endarkened juice +
   5 cicely per player.

---

#### Jewelled (crystal / laser) crabs

**W** [Jewelled Crab](https://oldschool.runescape.wiki/w/Jewelled_Crab) ·
**T** [Crystal Crabs 0:20:40](https://www.youtube.com/watch?v=klhBxOH8reQ&t=1240)

Cache ✅: crabs 7576 grey / 7577 red / 7578 green / 7579 blue; energy beams
7580 white / 7581 red / 7582 green / 7583 blue; locs 29753–29757 big crystals
1–5, 29758–29762 small crystals black/cyan/magenta/yellow/white, 29711 hammer
spawn, 29752 the Xeric relief.

**The transcript is the primary source for this room** — the wiki page has
almost nothing 📖, while Synq specifies the whole puzzle 🎥:

1. **Colour ↔ style mapping** 🎥: default **white**; **magic → blue**;
   **ranged → green**; **melee or smash → red**. Magic won't splash as long as
   magic bonus > −64.
2. **Crystal ↔ beam pairing** 🎥: black crystal ← white beam; yellow ← blue;
   cyan ← red; magenta ← green. Note this is *not* an identity mapping — the
   crystal colour and the beam colour that satisfies it are different. The cache
   loc names (`_smallcrystal_black/cyan/magenta/yellow/white`) match this list
   exactly ✅, which is good corroboration.
3. **Beam bounces clockwise** off crabs 🎥, and there is **no tick delay between
   attacking successive crabs** 🎥.
4. **Smash** 🎥 — a wielded dragon warhammer or elder maul (or the room's hammer
   spawn, loc 29711) stuns a crab in place *and* colours it red.
5. **Stun duration scales with party size** 📖: solo 50–60 ticks, 2–3 players
   30–40, 4–5 20–30, 6+ 10–20. Crabs regen stats at **1 per tick** 📖.
6. **Aggro** 🎥 — standing 2 tiles from a crab aggros it immediately; it
   de-aggros if you stay out of range too long.
7. **Self-destruct outs** 🎥 — a mis-coloured beam can be destroyed early by
   recolouring the *next* crab it will hit, or by running into the beam. Beam
   damage scales *down* with your current HP but **can still kill at 1 HP**.
8. **Three layouts** 🎥, named by the community: *good crabs*, *bad crabs*
   (10–25s slower), and *rare crabs*. The five `_bigcrystal_1..5` locs ✅ suggest
   the crystal positions are per-layout.

Impl note: this room is a small deterministic simulation — beam position, beam
colour, crab positions, crab colours, tick clock. Build it as an explicit state
machine with a headless test that solves each of the three layouts, before
touching any client-visible pieces.

---

#### Tightrope

**W** [Keystone crystal](https://oldschool.runescape.wiki/w/Keystone_crystal),
[Deathly ranger](https://oldschool.runescape.wiki/w/Deathly_ranger),
[Deathly mage](https://oldschool.runescape.wiki/w/Deathly_mage) ·
**T** [Tightrope 0:26:21](https://www.youtube.com/watch?v=klhBxOH8reQ&t=1581)

Cache ✅: npcs 7559 `raids_tightrope_ranger`, 7560 `raids_tightrope_mage`; locs
29749 barrier, 29750 `_end`, 29751 keystone.

Stats 📖: **Deathly ranger** 120 HP, def 155, rng 210, ranged str +70, speed 4,
**max hit 70**. **Deathly mage** 120 HP, def 155, mag 210, magic dmg +120,
speed 4, max hit 22.

Mechanics to build:
1. **Passive until you step on the rope** 📖 — both types are non-aggressive
   until a player attempts to cross, then all of them turn on.
2. Solo/small groups get 2 rangers + 2 mages 📖.
3. **The keystone dispels the barrier and kills every surviving ranger and
   mage** 📖. This is the intended solve — the room is a traversal puzzle, not a
   kill room. The "No Time for Death" combat achievement requires clearing it
   without killing any of them 📖.
4. Ironmen can pick up a keystone a teammate dropped; **points go to whoever
   collected it first** 📖.

---

#### Thieving (creature keeper)

**W** [Cavern grubs](https://oldschool.runescape.wiki/w/Cavern_grubs),
[Corrupted scavenger](https://oldschool.runescape.wiki/w/Corrupted_scavenger) ·
**T** [Thieving 0:18:13](https://www.youtube.com/watch?v=klhBxOH8reQ&t=1093)

Cache ✅: npcs 7602 `raids_thievingchest_beast_active`, 7603 `_sleeping`; locs
29742/29743 chest closed/open, 29744/29745 eggs whole/hatched, 29746 food trough.

Mechanics to build:
1. **Feed the scavenger until full** 📖 — it blocks the corridor. Grub count
   required scales with team size ❓ (formula not on either page — see
   [Task R3](#r3-missing-formulas)).
2. **Chests → cavern grubs** 📖. Stackable to a cap of 28. A successful open
   guarantees ≥1 grub.
3. **115 points per grub deposited** 📖; room max is
   `max(4500, ⌊total thieving level / 6⌋ × 150)` 📖.
4. **Hunger regen** 📖 — the scavenger starts regenerating hunger 60s after the
   last feed (raised from 30s in Jan 2019). ⚠️ A slow team can lose ground.
5. The `_eggs_whole`/`_eggs_hatched` loc pair ✅ and the `_beast_sleeping`/
   `_beast_active` npc pair ✅ imply mechanics neither page documents. ❓

---

### 2.3 Support rooms

#### Scavenger rooms

**W** [Scavenger beast](https://oldschool.runescape.wiki/w/Scavenger_beast)

Cache ✅: 7548 `_a`, 7549 `_b`.

Stats 📖: 30 HP, att/str 120, def 45. Max hit 13, crush, 4 ticks. Aggressive.

Drops 📖: **two rolls** per kill since Nov 2023 (was three), plus a guaranteed
bone. Tools (fishing rod, pickaxe, axe, butterfly net, hammer, tinderbox,
lockpick) and materials: cave worms 30–50, endarkened juice 5–14, stinkhorn
mushroom 3–11, cicely 3–6, mallignum root planks 2. **Cicely, endarkened juice
and stinkhorn always drop together** 📖.

#### Resource rooms

**W** [Chambers of Xeric](https://oldschool.runescape.wiki/w/Chambers_of_Xeric) ·
**T** [General Info 0:08:20](https://www.youtube.com/watch?v=klhBxOH8reQ&t=500)

Cache ✅: locs 29765 empty patch, 29771 farming tools, 29772 gourd tree, 29773
weeds, 29769 storage hotspot, 29770/29779/29780 storage 1/2/3.

- **Farming** 📖 — two plots per resource room, herbs grow in **30 seconds**:
  golpar (27 Farming), buchu (39), noxifer (55).
- **Storage units** 📖 — small (30 Con, 2 planks, 250 cap), medium (60, 4, 500),
  large (90, 6, 1000), massive (99, 8, 1500). **100 points per tier built.**
- Energy wells fully restore run energy 📖.

#### Fishing / hunter / cooking

📖 Seven tiers each, gated on level and healing 5/8/11/14/17/20/23:

| Tier | Fish (Fishing) | Bat (Hunter) | Heals |
| --- | --- | --- | --- |
| 1 | Pysk (1) | Guanic (1) | 5 |
| 2 | Suphi (15) | Prael (15) | 8 |
| 3 | Leckish (30) | Giral (30) | 11 |
| 4 | Brawk (45) | Phluxia (45) | 14 |
| 5 | Mycil (60) | Kryket (60) | 17 |
| 6 | Roqed (75) | Murng (75) | 20 |
| 7 | Kyren (90) | Psykk (90) | 23 |

Cache ✅: `raids_bat_0..6` = 7587–7593 — the seven bat tiers, in order.
`raids_fishing_snake` = 7594. Fishing needs a rod + cave worms as bait 📖;
99 Hunter allows barehanded bat catching 📖.

Cooking points 📖: `4 + (8 × bat tier)`, **+50% if another player eats it**.

There is a partial implementation already: [`cox_bats.rs2`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_bats.rs2) (90 lines) ✅ — read it first.

#### Herblore / potions

**W** [Overload (CoX)](https://oldschool.runescape.wiki/w/Overload_(Chambers_of_Xeric))

Three tiers of everything, brewed from gourds + water + herbs 📖:

| Potion | Effect |
| --- | --- |
| Elder / Twisted / Kodai | single-stat combat boosts (the overload's components) |
| **Overload** | boosts **all** combat stats, **damages 50 HP**, reapplies every 15s for 5 min, **heals 50 back at the end** |
| Revitalisation | stat restore |
| Prayer enhance | prayer restore over time |
| Xeric's aid | acts as a Saradomin brew |
| Antipoison | poison cure |

Overload boost formula 📖: `⌊level × 13 / 100⌋ + 5` (so +17 at 99).
⚠️ The main CoX page instead said "4 + 10%" 📖 — **two different formulas from
two pages. Resolve before encoding.** Herblore reqs: overload (−) 60, overload
75, overload (+) 90 📖 (the main page said 47+ for weak potions generally 📖).

**Nothing brewed in the raid can leave it** 📖 — the raid-scoped inv in §1.3
must be destroyed on exit and on death.

---

## 3. The Great Olm

This is roughly half the project. Give it its own file
(`docs/minigames/cox/COX_OLM.md`) once building starts.

**W** [Great Olm](https://oldschool.runescape.wiki/w/Great_Olm) ·
**T** — 40+ chapters, [The Great Olm 1:49:02](https://www.youtube.com/watch?v=klhBxOH8reQ&t=6542) onward.

Cache ✅: 7550–7557 the spawning/live/dying triplets for head and both hands,
7558 `olm_firewall_npc` (the flame wall is an **NPC**, not a ground effect —
that resolves engine gap E8 for this one case), loc 29735 `raids_bossentrance`.

### 3.1 Structure

📖 At least **4 phases**, **+1 per 8 players**. Each phase = disable both hands.

| | Head | Left claw | Right claw |
| --- | --- | --- | --- |
| Combat level 📖 | 1043 | 750 | 549 |
| Hitpoints 📖 | 800 | 600 | 600 |
| Att / Str 📖 | 250 / 250 | 250 / 250 | 250 / 250 |
| Defence 📖 | 150 | 175 | 175 |
| Magic 📖 | 250 | 175 | 87 |
| Ranged 📖 | 250 | 250 | 250 |

**Damage gating** 📖 — the mechanic that forces a three-style setup:

- Head: 66% mitigation from **all non-ranged** attacks.
- Right hand: 66% mitigation from **all non-magic** attacks.
- Left hand: 66% mitigation from **all non-melee** attacks.
- 50% **earth spell** weakness (June 2025). 100% poison/venom resistance.
- Melee def +200 stab/slash/crush, magic def +200, ranged def +50–200.

**Challenge mode** 📖: head and hands keep *normal* HP but get the elevated
combat stats — CM Olm hits much harder without taking longer. ⚠️ This is the one
place CM does *not* follow the ×1.5 HP rule.

### 3.2 Head turn — the mechanic everything else hangs off

**T** [Head Turn Mechanics — "arguably the MOST IMPORTANT SECTION" 1:51:38](https://www.youtube.com/watch?v=klhBxOH8reQ&t=6698)

The Olm's head faces one side of the chamber; which side it faces determines
where it attacks and what the player can safely do. Every movement pattern in
the rest of the guide is expressed relative to head facing. **Build this first
and build it exactly** — get it wrong and every downstream tick pattern is
untestable. The transcript's own framing is the strongest available signal about
what matters here 🎥.

### 3.3 Attack cycle

**T** [Attack Cycle 2:09:48](https://www.youtube.com/watch?v=klhBxOH8reQ&t=7788),
[Basic Attacks 2:03:39](https://www.youtube.com/watch?v=klhBxOH8reQ&t=7419),
[Specials 2:06:46](https://www.youtube.com/watch?v=klhBxOH8reQ&t=7606),
[Phases 2:00:09](https://www.youtube.com/watch?v=klhBxOH8reQ&t=7209)

📖 The documented rotation:

```
standard → empty → standard → Crystal Burst
standard → empty → standard → Lightning
standard → empty → standard → Teleport
(loop)
```

- **Basic attacks** 📖 — magic (large green orbs) and ranged (crystal chunks),
  same damage. **1/5 chance to switch style each head attack.** Prayer reduces
  damage ~75–80%.
- **Phase-specific attack chance** 📖 — 1/3, rising to **1/2 in the final phase**.
- **Spheres** 📖 — red (pray melee), green (pray missiles), purple (pray magic);
  ~50% of current HP if unprotected.

The transcript's slot names — *Basic 1*, *Basic 2*, *Empty*, *Special* — map
onto this rotation and are used throughout the setup methods 🎥:

- [Method 1: Basic 1 → Empty 2:51:04](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10264)
- [Method 2: Basic 2 → Special 2:51:54](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10314)
- [Method 3: Basic 1 → Basic 2 2:52:55](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10375)
- [Method 4: Basic 2 + Special → Basic 1 2:54:28](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10468)

⚠️ **These four methods are the acceptance test for the cycle implementation.**
If a player can reproduce all four in our client, the rotation is right. If they
can't, it isn't — regardless of what the code looks like.

### 3.4 Phase-specific attacks

| Phase | Attack | Detail |
| --- | --- | --- |
| Crystal | Falling crystals 📖 | 11 crystals; direct hit (under the shadow) 16–25, indirect (1 tile off) 10–16. Elsewhere the page says "up to 15–20" ⚠️ resolve |
| Crystal | Crystal bombs 📖 | up to 3 (2 for small groups); min 15 damage, **+15 per tile closer** |
| Flame | Deep burn 📖 | green fireball; 5 damage every few ticks, **−2 to stats each tick** |
| Flame | **Fire wall** 📖 | two walls of fire; 50–65 damage over 5 seconds. Cache npc 7558 ✅ |
| Acid | Acid spray 📖 | pools; **3–6 damage per tick** while standing in one |
| Acid | Acid drip 📖 | marks a player, who then *generates* pools as they walk |
| Any non-head | Crystal burst 📖 | a seedling under every player |
| Any non-head | Lightning 📖 | runs north and/or south, **disables overhead prayers** |
| Any non-head | Teleport 📖 | damage scales with distance from the target |
| Final | **Life siphon** 📖 | two blue projectiles mark tiles; several ticks later everyone *not* on a marked tile is damaged and Olm heals **5× the total** |

**T** [Flame Wall 1:57:41](https://www.youtube.com/watch?v=klhBxOH8reQ&t=7061) ·
[Flame 3:01:29](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10889) ·
[Acid 3:03:05](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10985) ·
[Phase Specifics 1:55:06](https://www.youtube.com/watch?v=klhBxOH8reQ&t=6906)

### 3.5 Autoheal and hand clenching

- 📖 **Autoheal** — during non-head phases, damage to the head is fully healed
  after several ticks. In the **penultimate phase the left hand gets the same
  treatment**. ⚠️ "Heals instead of damaging" is a sign flip, not a no-op —
  exactly the class of bug that hides for months. Test it explicitly.
- 📖 **Clenching** — the left hand clenches (becomes untargetable) if it takes a
  hit ≥ **1/20 of its base health** (i.e. ≥30 on a 600 HP hand).

### 3.6 Movement patterns — the acceptance corpus

The transcript's second half is essentially a spec for what a correct Olm lets
players do. Each of these is a **behavioural test case**, not a feature to
implement — they should fall out of a correct head-turn + cycle + pathing model.
If one of them is impossible in our client, something upstream is wrong.

| Pattern | **T** |
| --- | --- |
| Pathing | [2:14:50](https://www.youtube.com/watch?v=klhBxOH8reQ&t=8090) |
| 4-tick mage running | [2:18:34](https://www.youtube.com/watch?v=klhBxOH8reQ&t=8314) |
| Splashing | [2:23:54](https://www.youtube.com/watch?v=klhBxOH8reQ&t=8634) |
| Eye 3-tick mage running | [2:38:27](https://www.youtube.com/watch?v=klhBxOH8reQ&t=9507) |
| Melee 4-tick 3:1 | [2:39:33](https://www.youtube.com/watch?v=klhBxOH8reQ&t=9573) |
| Melee 4-tick 4:1 | [2:48:05](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10085) |
| 4-tick 4:0 | [3:08:43](https://www.youtube.com/watch?v=klhBxOH8reQ&t=11323) |
| 5-tick (scythe) 3:1 | [3:13:21](https://www.youtube.com/watch?v=klhBxOH8reQ&t=11601) |
| Scythe 7:3 | [3:16:11](https://www.youtube.com/watch?v=klhBxOH8reQ&t=11771) |
| Mage 7:3 | [3:26:01](https://www.youtube.com/watch?v=klhBxOH8reQ&t=12361) |
| Shadow mage running, 12:0 west / 8:1 east | [3:32:33](https://www.youtube.com/watch?v=klhBxOH8reQ&t=12753) |
| Head phase | [3:44:37](https://www.youtube.com/watch?v=klhBxOH8reQ&t=13477) |
| 5-tick 4:1 (most common) | [3:50:49](https://www.youtube.com/watch?v=klhBxOH8reQ&t=13849) |
| DHCB 3:0 | [3:52:29](https://www.youtube.com/watch?v=klhBxOH8reQ&t=13949) |

Two chapters describe *engine* behaviour rather than strategy and are worth
reading before implementing:

- [Mage Hand "Glitch" 2:37:26](https://www.youtube.com/watch?v=klhBxOH8reQ&t=9446) 🎥
- [Weird West Side Flame Null 2:47:28](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10048) 🎥 — a null/edge case in flame targeting on one side of the chamber
- [Scuffed Olm 2:44:51](https://www.youtube.com/watch?v=klhBxOH8reQ&t=9891) 🎥
- [Sound Tick Eating 3:06:51](https://www.youtube.com/watch?v=klhBxOH8reQ&t=11211) 🎥 — relevant to this tree's [`cycle-vs-frame-cadence`] note

---

## 4. Points and scaling

**T** [General Info 0:08:20](https://www.youtube.com/watch?v=klhBxOH8reQ&t=500),
[Scouting 0:05:49](https://www.youtube.com/watch?v=klhBxOH8reQ&t=349)

### 4.1 Earning 📖

| Source | Points |
| --- | --- |
| Combat | proportional to damage dealt (exact coefficient ❓) |
| Storage unit | 100 per tier built/upgraded |
| Shortcuts | level requirement × 5 (86 Mining = 430) |
| Ice demon | kindling deposits, ~6,050 max normal mode |
| Thieving | 115/grub, cap `max(4500, ⌊total thieving/6⌋ × 150)` |
| Cooking | `4 + 8 × tier`, +50% if another player eats it |
| Potions | capped at 5 sips × player count, +5 more at a reduced rate |
| Crabs | per successful colour change |
| Tightrope | keystone use |
| Muttadile | chopping the meat tree |
| Guardians | damage from being pushed |

### 4.2 Caps and penalties 📖

- Common loot scales up to **131,071** points. Personal points are **no longer
  capped** at that value during the raid (Jan 2025) but loot rolls still are.
- Points beyond the cap still count toward unique rolls.
- **Death: lose 40% of your personal points.** If you die with <5% of the team's
  points, the **team** loses 5% instead. ⚠️ Two different penalties depending on
  a threshold — a single-branch implementation will be wrong for one of them.
- Party size 1–100. Monster stats and HP scale with team size; skill-gated
  content scales off the team's collective levels.

### 4.3 Deaths 📖

Dying in the raid keeps your items (including deathbank contents), **but items
you manually dropped on the ground are lost**, and all raid-only items (potions,
food) drop.

❓ The damage→points coefficient and the per-monster scaling formulas are not on
the pages fetched. See [Task R3](#r3-missing-formulas).

---

## 5. Rewards

**W** [Ancient chest](https://oldschool.runescape.wiki/w/Ancient_chest) ·
Cache ✅ loc 28848 `raids_reward_lootbeam`, npc 7520 `raids_olm_pet`.

### 5.1 Unique chance 📖

**1% per 8,676 total points**, capping at **65.7% (570,000 points)**. Points past
that roll for a **second** unique; **up to six** uniques per raid.

### 5.2 Unique table 📖 (normal mode, total weight 60)

| Item | Weight |
| --- | --- |
| Dexterous prayer scroll | 14 |
| Arcane prayer scroll | 14 |
| Ancestral hat | 4 |
| Ancestral robe top | 4 |
| Ancestral robe bottom | 4 |
| Twisted buckler | 4 |
| Dragon hunter crossbow | 4 |
| Dragon claws | 3 |
| Dinh's bulwark | 3 |
| Elder maul | 2 |
| Kodai insignia | 2 |
| Twisted bow | 2 |

Challenge Mode: total weight 56; prayer scrolls drop to 12, ancestral pieces
rise 📖. ⚠️ The summary says ancestral "increased to 4/56" while normal is
already 4/60 — that's a denominator change, not a numerator change. Re-read.

### 5.3 Common loot 📖

Two rolls when no unique lands, guaranteed to be different items, each roll
`2 × 1/33` across: death/blood/soul runes, rune and dragon arrows, grimy herbs
ranarr→torstol (33% of herb rolls come as seeds instead), silver→runite ore,
uncut gems, pure essence, lizardman fang, ancient tablet (1/10).

⚠️ The quantity ranges the summary produced (up to 65,535) are clearly the
serialisation ceiling, not the game's quantities. **Re-read the page.**

### 5.4 Tertiary 📖

- Dark journal — always, if not already banked
- Elite clue scroll — 1/12 (1/11 with the Elite CA unlock)
- **Olmlet** — 1/53, **only on a broadcast unique**
- Twisted ancestral colour kit — 1/75, **CM only, within the time limit**
- Metamorphic dust — 1/400, **CM only, within the time limit**

### 5.5 Beam colours 📖

White = standard, purple = unique, turquoise = metamorphic dust, green = twisted
ancestral colour kit. Loc 28848 is the beam ✅.

---

## 6. Challenge Mode

**W** [Chambers of Xeric/Challenge Mode](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode) ·
**T** [CM Skip 1:08:07](https://www.youtube.com/watch?v=klhBxOH8reQ&t=4087)

- **All enemy combat stats including HP ×1.5** 📖 — except Olm's head and hands,
  which keep normal HP and take only the stat increase.
- **Static, linear layout across four floors** 📖 containing *every* room:
  - Top: Tekton → Jewelled crabs → Scavengers → Ice demon → Shamans → Resource
  - Middle: Vanguards → Thieving → Scavengers → Vespula → Resource → Tightrope
  - Lower: Guardians → Vasa → Scavengers → Skeletal mystics → Muttadiles → Resource
- Time limits for the CM-only rewards 📖: solo 1h10m, duo 1h05m, trio 50m,
  4-player 45m, 5–10 players 42m. Faster clears award **+5,000 points/player**.
- Completion capes at 100 / 500 / 1,000 / 1,500 / 2,000 clears via Captain Rimor 📖.

CM is a good **milestone 2** target precisely *because* it is static: it
exercises every room without needing layout generation.

---

## 7. Build order

Ordered so each phase produces something runnable and testable, and so the
riskiest unknowns are measured before anything is built on top of them.

### Phase 0 — measure and unblock (no gameplay)

| | Task |
| --- | --- |
| 0.1 | **[R2] Dump the room templates.** `m50_89`–`m52_95`; measure room tile size, packing, and which square holds which room. Produce a machine-readable manifest under `tools/data/`, the way the Gauntlet's door-mask manifest works ✅ |
| 0.2 | **[R1] Resolve the Olm npc identity** (7551 vs 7554). Load both, look at them, write down the answer |
| 0.3 | Add every `raids_*` and `olm_*` name to `pack/npc.server` and `pack/loc.server`. **Nothing else can proceed without this** — currently 0 raids locs are addressable |
| 0.4 | Decide the instance geometry against `ZONES`/`LEVELS` (§1.1). File the engine changes E1/E2 if needed |
| 0.5 | Read `minigame_gauntlet` end to end; read the existing `cox_bats.rs2` |

### Phase 1 — one room, real instance — **written, compiles, not yet run**

Goal: enter a **map-instanced** single-room raid, fight Tekton, exit, get a
points readout. Replaces the old staging-tile stub.

Files, all under
[`minigame_cox/`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/):

| File | What it holds |
| --- | --- |
| `configs/cox.constant` | geometry (all **measured**, §0.3), points formulas, Tekton timings |
| `configs/cox.varp` | session state, all `scope=temp` |
| `configs/cox.npc` | authored HP/attackrate for all six Tekton records |
| `scripts/cox.rs2` | instance alloc / setchunk / build, enter, exit |
| `scripts/cox_tekton.rs2` | the six-record transform machine, anvil, sparks |
| `scripts/cox_points.rs2` | points accrual, loot cap, unique %, death penalty |

Done in this pass:

- ✅ Instance alloc/setchunk/build for one room, modelled on `gauntlet.rs2:139`
- ✅ Entrance/exit locs 29777/29778 bound; party loc 29776 messaged as a stub
- ✅ Per-player points varp, loot cap, unique-% formula, **both** death-penalty
  branches (the 40%-personal and the 5%-team-under-threshold case)
- ✅ Tekton's transform machine, anvil walk, repair heal, spark AoE, the
  attack-count threshold rolled **once per cycle** rather than per swing
- ✅ Compiles clean, verified with a self-proving gate (see the build note in
  §0.4)

- ✅ **The damage→points hook (E4) is wired.** `~cox_on_damage` is called from
  `~npc_default_damage` in
  [`skill_combat/npc_combat.rs2:117`](../../../OSRS-Content/osrs239-content/server/scripts/skill_combat/npc_combat.rs2#L117),
  beside the existing `~pest_on_damage` / `~mole_on_damage` hooks — the seam
  this codebase already uses for per-encounter damage accounting.

  ⚠️ Worth recording how nearly this went wrong: the first version of the
  harness reverted every file outside `minigame_cox/`, which included this hook,
  and then reported "compiled clean" **without ever having compiled the change
  under test**. The harness now carries an explicit `KEEP` list. A verification
  tool that silently drops your change is worse than no tool.

Partly verified at runtime:

- ✅ **CoX content loads clean into the server.** `mock230` was built and booted
  against an isolated tree; the content loader reported **zero** errors from
  `minigame_cox` (all 130 remaining loader errors were in `skill_construction`,
  `quest_upass`, `quest_arena` and other packages this work never touched).
- ✅ `cox_selftest.rs2` exists with seven checks — points scale, points guarded
  outside a raid, loot cap, unique %, unique-% ceiling, and **both** death
  penalty branches — and a `SELFTEST_CHECK` pair in `mock230_world.c` beside
  `::hamstoreroomtest`.

### ✅ Blocker resolved — and the cause was my own tooling

`::coxrun` now runs and its result is read back correctly. **Verified in both
directions**: mutating `^cox_points_per_damage` 5→7 or
`^cox_death_personal_loss_pct` 40→30 turns the selftest red (`got 1`), and
restoring turns it green. The fixture can fail, so its passing means something.

Two self-inflicted faults, both worth remembering:

1. **A build helper that suppressed compiler output.** When a compile failed,
   the *previous* `script.dat` stayed in place and the selftest happily reported
   a pass — so a deliberately broken constant looked correct. Every "the test
   cannot fail" result came from this. Never redirect the compiler to
   `/dev/null`.
2. **A tautological assertion.** The points check compared against
   `multiply(10, ^cox_points_per_damage)` — the same constant the implementation
   reads — so mutating it moved both sides together. It is now a literal `50`.

There was never a bug in the debugproc dispatcher; I spent a long time chasing
one. Both lessons are now enforced by
[`tools/cox_verify.sh`](../../../tools/cox_verify.sh), which fails loudly at
every step, refuses to report a selftest result when the compile did not
produce a new pack, and **falls back to the isolated tree** when another
session's in-flight edit breaks the shared one (which happened repeatedly:
`quest_troll`, `combat.rs2`, `flamtaer_temple`, `viking_peer`,
`viking_thorvald`).

### Historic: the symptom as it looked before the cause was found

`mock230_scripts_run_debugproc(srv, "coxrun")` returns `MOCK230_TRIGGER_RAN`,
but the fixture's very first statement never takes effect: with
`%mock_quest_progress = 99` as the literal first line of the debugproc, the C
side still reads back the `-1` it seeded. So the proc is reported as found and
run while none of its body executes.

**The sharpest form of the symptom:** put `%mock_quest_progress = 42;` as the
literal first statement of `[debugproc,coxrun]`, recompile the pack, run — the
harness still reads back `-1`. Reproduced in both the real tree and the isolated
tree, each with a pack rebuilt *after* the edit and verified to contain the
write. So this is not "some check inside the fixture fails"; the body does not
run at all, or its writes land somewhere the harness cannot see.

What has been ruled out:

- Not the varp: `mock_quest_progress` is id 7 in `all.varp.compack`, matching
  `SELFTEST_VARP_QUEST_PROGRESS`, and the neighbouring `::hamstoreroomtest`
  block writes that same varp successfully **in the same run**.
- Not the C harness: the CoX block is structurally identical to the ham block
  (seed −1, run, assert 0, restore) and its message text does reach stdout.
- Not a stale binary or stale pack: `mock230` rebuilt; pack rebuilt after every
  source edit and its mtime checked against the source file's.
- Not a compile failure: clean under `tools/cox_compile_check.sh --selftest`.
- **Not a dispatch prefix collision** (my first hypothesis, and it was wrong).
  `mock230_scripts_run_debugproc` does an exact `SSVM_ProviderGetByName` on
  `"[debugproc,coxrun]"` ([`mock230_scripts.c:3607`](../../../src/net/mock/mock230_scripts.c#L3607))
  and returns `MOCK230_TRIGGER_NONE` when absent. `TRIGGER_RAN` therefore means
  the script was found *and* `run_hook_sv` returned truthy. The sibling
  `[debugproc,cox]` cannot be intercepting it.
- **Inconclusive, do not repeat:** grepping `script.dat` for the string
  `coxrun` finds nothing — but it also finds nothing for `hamstoreroomtest`,
  which demonstrably works. Trigger names resolve to ids and are not stored as
  text, so that check says nothing either way.
- **Inconclusive:** `MOCK230_VERBOSE=1` printed no dispatch line for *either*
  `coxrun` or `hamstoreroomtest`, so `srv->verbose` is evidently not set from
  that variable at this point in the run.

Next things to try, in order of expected yield:
1. Instrument `mock230_scripts_run_hook_sv` to log the script name and its
   return, so "found and truthy" can be separated from "actually executed".
2. Check whether the debugproc executes against a different player than the one
   the harness inspects — the ham fixture also does `npc_del`/npc work, so it
   may be establishing a player context that a pure-arithmetic fixture never
   does.
3. Try the fixture with a single trivial body and no `[proc]` calls at all, to
   see whether the seven sub-proc definitions in the same file matter.

Still open before Phase 1 can be called done:

- ❌ **The room has never been played.** Loading clean is not the same as
  working. The instance may assemble the wrong chunk, Tekton may never path to
  the anvil, the `ai_timer` triggers may never fire, and points may never
  actually accrue. None of that is observable from a compile or a content load.
- ❌ `^cox_points_per_damage = 5` is **ours, not measured** (task R3).
- ❌ The DWH/elder-maul guaranteed-first-spec rule has a varp
  (`cox_tekton_first_spec_used`) but no implementation.
- ❌ Tekton's wedge is currently a symmetric box around his footprint, not the
  real "in front of and to the right" shape.
- ❌ No reward chest, no headless test.

**Done when:** a headless test enters, kills Tekton, and reads back a nonzero
point total, and a second run with a different damage total produces a
proportionally different number.

### Phase 2 — Challenge Mode's static layout

Goal: all 17 rooms, linear, fixed. No layout generation, no Olm.

- Multi-floor instance, the four CM floors
- All 8 combat rooms + 4 puzzle rooms + scavengers + resource rooms
- The full supply loop: farming, fishing, hunter, cooking, herblore
- Storage units
- Corridor blockers (roots/rocks/boulder locs) as the room gates

**Done when:** a player can clear every room in sequence and reach the Olm door.

### Phase 3 — the Great Olm — **started; rotation and clock implemented**

[`cox_olm.rs2`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_olm.rs2)
now implements the parts the research pinned down exactly
([`COX_MECHANICS.md` §2](COX_MECHANICS.md)):

- ⚠️ **4-tick action clock**, driven from a single writer (`[ai_timer,olm_head]`)
  so the hands cannot race the rotation — correct as written and **never armed**:
  nothing calls `npc_settimer`, so the head has no timer and the trigger cannot
  fire (§11.3 F0)
- ✅ **The 12-step rotation** — steps 4/8/12 special, 2/6/10 empty, rest standard.
  The empty step is a real step and is commented as such: deleting it would
  shorten the cycle from 12 to 9
- ✅ **Catch-up**, paid off on the next step *in addition to* that step's action,
  and explicitly non-accumulating
- ✅ Special order from the plugin enum, with the phase-dependent wrap (heal only
  reachable in the final phase)
- ✅ 1/5 style switch; 1/3 special chance rising to 1/2 in the final phase
- ✅ Phase transitions gated on **both** hands being down
- ✅ Spawning forms → live forms via the cache's own transform pair

Remaining, in order:

1. ❌ **`~cox_olm_zone_occupied` is a stub that always returns true**, which
   disables the skip and therefore the entire 3:1 / 4:1 family. It needs the
   chamber's zone geometry — which third of the room the head faces. It is
   isolated in its own proc precisely so the rotation above is already correct
   and only this has to change. **This is the next Olm task.**
2. ❌ Chamber tile positions are **placeholders** — the Olm room's interior has
   not been surveyed (R2 measured the room grid, not the chamber)
3. ❌ The four specials are named, correctly slotted stubs
4. ❌ Per-hand damage gating, clenching, autoheal (both variants)

**Done when:** all four setup methods from §3.3 are reproducible, and a sample
of the §3.6 movement patterns work.

### Phase 4 — normal mode layout generation

**The room library is now known** — see
[`COX_MECHANICS.md` §1b](COX_MECHANICS.md). Every room exists in exactly three
**authored door variants (CCW / THRU / CW)**, and the room↔plane taxonomy from
the de0 plugin matches my template survey on every room checked. Two
consequences:

- The generator **picks a variant**, it does not rotate geometry — so
  `^map_instance_turn_none` in `cox.rs2` is right, and the Gauntlet's rotation
  logic should *not* be copied wholesale.
- Room type is `(plane, index)`; the plane assignments are confirmed
  (Tekton/mystics/muttadiles/tightrope on plane 1; guardians/vespula/crabs on
  plane 2; the rest on plane 0).

Remaining:

- Seeded room graph (the `gauntlet_layout.rs2` pattern, minus rotation)
- Floor composition rules (7–8 rooms normal, 8 large) 📖
- Scouting: the boss books (locs 12310, 13482–13485) that tell a scout what's inside

### Phase 5 — rewards, parties, polish

- The real unique table, chance formula, common loot
- Party formation, the recruitment board
- Olmlet, CM dust/kit, time limits, completion capes
- Combat achievements

---

## 8. Testing

This tree's history says the failures will be silent, not loud. Specific
defences:

1. **Headless first.** Every room's mechanic gets a headless test before it gets
   a client-visible effect. The crab beam simulation and the Olm attack cycle
   are pure state machines — test them as such.
2. **Prove the assertions can fail.** Per [`verify-blocker-and-failing-test`],
   mutate the implementation and confirm the test goes red. A test that passes
   against a stubbed-out mechanic is worse than no test.
3. **Watch for the shapes that have bitten this tree before:**
   - A new `param=` name needs a `mock230_content.c` branch too
     ([`npc-overlay-param-whitelist`])
   - Authored `.npc` blocks must **restate** anim rows ([`zulrah-implementation`])
   - `p_oploc` checks the **base** multiloc op — the brazier and crystal locs are
     multilocs ([`p-oploc-multiloc-op-check`])
   - Loc ops are **per-placement**; there are 5 braziers-worth of placements
     ([`loc-ops-are-per-placement`])
   - Duplicate trigger names are a **hard compile error** — 17 rooms will collide
     ([`duplicate-triggers-error-use-categories`]); route through a hub + categories
   - `mes` strings must be ASCII ([`mes-string-must-be-ascii`])
4. **A/B before blaming your change** — `embed_test` decode is already broken
   pre-existing ([`embed-test-decode-broken`]).
5. **Points regression:** a scripted raid with a fixed damage script must produce
   a stable point total across runs. Shared-RNG false regressions are a known
   trap here ([`mock230-selftest-operational-notes`]).

---

## 9. Open research tasks

### R1 — Olm npc identity — ✅ **CLOSED**
The cache is right, the wiki is wrong: the split is spawning vs live, not normal
vs challenge, settled by `vislevel` 1043/750/549 matching the wiki's own combat
levels. Full table and the defence-param corroboration in §0.1.

### R2 — Measure the room template grid — ✅ **CLOSED**
Room pitch is **32 tiles (4 zones)**, 2×2 rooms per map square, 65 occupied
cells across 15 squares. Method, the two approaches that failed first, and the
block correction are in §0.3;
[`tools/cox_template_survey.py`](../../../tools/cox_template_survey.py) emits
[`tools/data/cox_templates.json`](../../../tools/data/cox_templates.json).
This also closed engine gaps E1 and E2 — see §1.1.

### R3 — Missing formulas — ⚠️ **mostly CLOSED**, see §11.2
Re-fetched every page as raw wikitext (`?action=raw`), which is where the
`{{CiteTwitter}}` / `{{CiteDiscord}}` quotes survive. That closed four of the six:

- ✅ **cavern grubs by team size** — 16 × players, or 30 solo; and
  `MaxGrubs = ⌈max(4500, ⌊ΣThieving/6⌋ × 150) / 115⌉` for the points ceiling.
- ✅ **Guardian HP** — `H = 151 × (1 + ⌊T × ½⌋) + ⌊M̄⌋ × T`, transcribed
  cleanly this time, alongside the damage multiplier already implemented.
- ✅ **Ice demon kindling / firemaking / tree depletion** — three separate Ash
  quotes, all in §11.2.
- ✅ **Shaman and mystic spawn counts** — full tables, not bounds.
- ❌ **damage → points coefficient** — still unpublished. Searched the wiki, its
  talk page, `Ancient chest`, and open-source servers; even the *cap* is
  unpublished (Mod Ash, 3 June 2024, on the Guardians' cap: "the exact formula
  is not currently known").
- ❌ **per-monster HP/stat scaling by party size**, except the Guardians.
- ❌ **room-count / floor-composition rules** for normal-mode generation.

### R4 — Numbers where two pages disagree — ⚠️ **mostly CLOSED**, see §11
- ✅ **Muttadile meat-tree heal: 50%.** Settled by the changelog itself —
  29 November 2023: "Both muttadiles will attempt to heal at the meat tree when
  reduced below 50% HP (**up from 40%**)." The 40% figure is a stale page.
- ✅ **Shaman count: 2–5**, per the exact table (1-4 → 2 … 15+ → 5). The "2–4"
  page is out of date.
- ✅ **Ice demon prayer reaction is NOT inverted, but it is counter-intuitive:**
  it uses the style you are praying *against*. "If Protect from Magic is used, it
  will only focus on Ice Burst, while if Protect from Missiles is used, it will
  simply throw snowballs instead." Protect from Missiles is recommended only
  because the boulder is easier to see coming.
- ✅ **CM unique table denominator: 56**, with dexterous and arcane scrolls at
  12/56 and every other weight unchanged (news, 12 August 2026).
- ✅ **Skeletal mystics** — both statements are true and not in conflict: the npc
  has attack range 10 and "cannot be safespotted"; the corner trick does not
  break line of sight, it *forces melee* and then strands the walk.
- ❌ **Vespula grounding threshold: 20% vs 23%** — still two pages, still no
  tiebreak. Keeping 20.
- ❌ **Falling crystals damage** — three published ranges across two pages; they
  may describe two different attacks (transition crystals vs the crystal power).
- ❌ **Overload boost** and **common-loot quantity ranges** — not revisited this
  pass; neither is tick-level.

### R5 — Undocumented cache assets — partly answered
Enumerating the loc namespace for §0.4 turned up a lot the wiki pages never
mention. Several **answer** open questions rather than raise them:

- ✅ **Four lootbeam variants exist as separate locs** — `raids_reward_lootbeam_basic`
  (30029), `_special` (30030), `_kit` (37976), `_dust` (37977) — exactly matching
  the wiki's four beam colours (white / purple / green kit / turquoise dust).
  Plus `raids_reward_chest` (30028) and `raids_reward_crystal` (30027).
- ✅ **The full farming chain is authored**, not synthesised: for each of noxifer,
  golpar and buchu there is `raids_patch_<herb>_seed`, `_growth1`, `_growth2`,
  `_growth3`, `_fullygrown` (29997–30011). The 30-second grow is four loc swaps.
- ✅ **Tekton's anvil is a loc** — `raids_tekton_anvil` (29867), 6×4, `blockwalk=1`.
  Also `raids_tekton_fire` (30021) and `raids_tekton_walkblocker` (30023).
- ✅ **Muttadile's meat tree** — `raids_meat_tree_full`/`_empty` (30012/30013),
  so the "chop it down" state is a loc swap, not an HP bar.
- ✅ **Vespula's blossom source** — `raids_vespula_herb`/`_empty` (30068/30069),
  plus `raids_vespula_boil_blocking`/`_burst` (30070/30071) and a
  `raids_vespula_portal`/`_closed` loc pair (30072/30073) distinct from the npc.
- ✅ **Guardians' pickaxe** — `raids_stoneguardians_pickaxe` (41754), a spawn.
- ✅ **Energy wells** — `raids_energy_pool`/`_glow` (30066/30067).
- ✅ **Floor traversal is a named set** — `raids_descentto1` (32542), `_descentto2`
  (29734), `raids_ascentto2` (32543), `_ascentto3` (29995), `raids_bossentrance`
  (29735), `raids_bossexit` (29996), `raids_olm_barrier` (29879).
- ✅ **CM has a scoreboard loc** — `raids_challenge_scores` (32544).
- ✅ **Lobby facilities** — `raids_bank_chest_lobby` (47419) and `_working`
  (47420), `raids_storage_lobby` (30107), `raids_storage_1..4`.

Still unexplained, and each implies a mechanic neither page covers:
- `raids_thievingchest_eggs_whole` / `_hatched` (29744/29745) and the
  `raids_thievingchest_beast_sleeping` / `_active` npc pair (7602/7603)
- `raids_lasercrabs_bigcrystal_1..5` — five, for three known layouts
- `raids_mystics_portal_start` / `_middle` / `_end` (49996–49998) — the skeletal
  mystics room has a three-part portal nothing documents
- `raids_blockage_purple` / `_purple_small` / `_orange` / `_green` (30015–30018)
- `raids_olmic_head` (29811) / `raids_olmic_head2` (30035), `raids_hells_bells`
  (30086), `raids_bucket_of_blood` (30085), `raids_blood_crystal` (30014),
  `raids_sacrificial_table` (29863)
- `raids_exit_steps_multi` / `_reload` (49999/50000)
- `raids_corridor_boulder` as both an npc (7489) and a loc (29740)

---

## 10. Reference index

**Wiki**

- [Chambers of Xeric](https://oldschool.runescape.wiki/w/Chambers_of_Xeric) · [Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies) · [Challenge Mode](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode) · [Ancient chest](https://oldschool.runescape.wiki/w/Ancient_chest)
- Bosses: [Great Olm](https://oldschool.runescape.wiki/w/Great_Olm) · [Tekton](https://oldschool.runescape.wiki/w/Tekton) · [Vasa Nistirio](https://oldschool.runescape.wiki/w/Vasa_Nistirio) · [Vespula](https://oldschool.runescape.wiki/w/Vespula) · [Vanguard](https://oldschool.runescape.wiki/w/Vanguard) · [Muttadile](https://oldschool.runescape.wiki/w/Muttadile) · [Ice demon](https://oldschool.runescape.wiki/w/Ice_demon)
- Minions: [Lizardman shaman (CoX)](https://oldschool.runescape.wiki/w/Lizardman_shaman_(Chambers_of_Xeric)) · [Skeletal mystic](https://oldschool.runescape.wiki/w/Skeletal_mystic) · [Guardian (CoX)](https://oldschool.runescape.wiki/w/Guardian_(Chambers_of_Xeric)) · [Scavenger beast](https://oldschool.runescape.wiki/w/Scavenger_beast) · [Deathly ranger](https://oldschool.runescape.wiki/w/Deathly_ranger) · [Deathly mage](https://oldschool.runescape.wiki/w/Deathly_mage) · [Abyssal portal](https://oldschool.runescape.wiki/w/Abyssal_portal) · [Jewelled Crab](https://oldschool.runescape.wiki/w/Jewelled_Crab)
- Items: [Overload (CoX)](https://oldschool.runescape.wiki/w/Overload_(Chambers_of_Xeric)) · [Keystone crystal](https://oldschool.runescape.wiki/w/Keystone_crystal) · [Cavern grubs](https://oldschool.runescape.wiki/w/Cavern_grubs) · [Corrupted scavenger](https://oldschool.runescape.wiki/w/Corrupted_scavenger)

**Transcript** — [`synq_transcript.md`](synq_transcript.md), 99 chapters, full
index at the top of that file. Regenerate with:

```sh
yt-dlp --skip-download --write-auto-subs --sub-langs "en.*" --sub-format vtt \
       -o cox https://www.youtube.com/watch?v=klhBxOH8reQ
```

**Measurement sources** — surveyed and rated in §11.1; fetch the corpus with
[`tools/fetch_cox_wiki.sh`](../../../tools/fetch_cox_wiki.sh)

- [Blert](https://blert.io) — per-tick PvM logs. **Does not cover CoX**: the
  stage enum exists, the recorder does not ("Coming Soon!"). Repos:
  [blert](https://github.com/blert-io/blert) · [plugin](https://github.com/blert-io/plugin)
- [weirdgloop `osrs-dps-calc` `monsters.json`](https://github.com/weirdgloop/osrs-dps-calc/blob/main/cdn/json/monsters.json)
  — attack speed in ticks, size, HP and stats for every CoX npc
- [Perfect Olm (Solo)](https://oldschool.runescape.wiki/w/Perfect_Olm_(Solo)) —
  the CA page, which pins where a phase's rotation starts
- [`sources/runelite/CoxPlugin.java`](sources/runelite/CoxPlugin.java) — Olm
  action clock, `crippleTimer`, Tekton timers · [`sources/de0/`](sources/de0/) — room splits
- Mod Ash quote archive: [reldo.runescape.wiki](https://reldo.runescape.wiki) —
  every `{{CiteTwitter}}` / `{{CiteDiscord}}` in §11.2 resolves here

**In-tree**

- [`minigame_gauntlet/`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/) — the architectural precedent
- [`minigame_inferno/`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/) — tick-scripted multi-wave boss, ground effects
- [`mock230_mapinstance.h`](../../../src/net/mock/mock230_mapinstance.h) — instance limits and the rotation contract
- [`docs/GAUNTLET.md`](../../GAUNTLET.md), [`docs/minigames/FIGHT_CAVES.md`](../FIGHT_CAVES.md)

---

## 11. Tick-level verification audit — 2026-08-17

The question this section answers: **is the implementation tick-perfect?** The
answer is no, and this records exactly where, with the source text for each
divergence so the next pass argues with a quote rather than with a memory.

Everything below was fetched as raw wikitext (`?action=raw`, not the rendered
page) so the citation templates — which is where Jagex's own tick numbers live —
survive. Re-fetch with [`tools/fetch_cox_wiki.sh`](../../../tools/fetch_cox_wiki.sh).

### 11.1 Is there a blert.io for CoX? — not yet, and that matters

[Blert](https://blert.io) is the closest thing PvM has to Warcraft Logs: a
RuneLite plugin, a data pipeline and a site that stores **per-tick** event
streams for a challenge and replays them. It would be the ideal oracle. It does
not cover Chambers of Xeric.

Its own CoX page, verbatim
([`web/app/(challenges)/raids/cox/page.tsx`](https://github.com/blert-io/blert/blob/main/web/app/(challenges)/raids/cox/page.tsx)):

> **Coming Soon!**
> We are adding raid recording support for the Chambers of Xeric raid soon! Stay
> tuned for updates.

The *schema* is already there, which is why the site's front page lists CoX —
`Challenge.COX("Chambers of Xeric", 2)` in
[`plugin/src/main/java/io/blert/core/Challenge.java`](https://github.com/blert-io/plugin/blob/main/src/main/java/io/blert/core/Challenge.java),
and thirteen stages in
[`common/challenge.ts`](https://github.com/blert-io/blert/blob/main/common/challenge.ts):
`COX_TEKTON, COX_CRABS, COX_ICE_DEMON, COX_SHAMANS, COX_VANGUARDS, COX_THIEVING,
COX_VESPULA, COX_TIGHTROPE, COX_GUARDIANS, COX_VASA, COX_MYSTICS, COX_MUTTADILE,
COX_OLM`. Nothing records into them. Treat "Blert supports CoX" claims from
search summaries as false until that page changes.

**What does exist, and what each is actually good for:**

| Source | What it measures | Tick-level? | Authority |
|---|---|---|---|
| [OSRS Wiki](https://oldschool.runescape.wiki/w/Chambers_of_Xeric) `{{CiteTwitter}}` / `{{CiteDiscord}}` refs | Jagex staff (Mod Ash) quoting the server's own numbers | **Yes** — regen rates, stun ranges, heal cadence, shuffle windows | Highest available. This is Jagex, quoted verbatim, archived |
| [weirdgloop `osrs-dps-calc` `monsters.json`](https://raw.githubusercontent.com/weirdgloop/osrs-dps-calc/main/cdn/json/monsters.json) | `speed` (attack rate in ticks), size, HP, offensive/defensive stats for every CoX npc | **Yes**, for attack speed | High — wiki+cache derived, machine-readable, one row per npc version |
| [RuneLite / OpenOSRS CoX plugins](sources/runelite/CoxPlugin.java) (in tree) | Olm action clock, Tekton/guardian attack timers, cripple timer | **Yes**, but *observed*, not authoritative | Medium — a working client that had to match the server |
| [de0 CoX Timers](sources/de0/CoxTimersPlugin.java) (in tree) | Room and Olm **phase splits**, wall-clock | Second resolution | Medium |
| [Blert](https://blert.io) | Per-tick replays for ToB / ToA / Inferno / Colosseum / Doom | Yes — **but not CoX** | Would be highest, if it covered CoX |
| CalcOSRS, Oldschool.gg raids simulator, loot trackers | Points → purple chance, loot distributions | No | Loot maths only |
| [Synq's 2026 solo guide](sources/../synq_transcript.md) | Method-level tick counts (4:1, 3:0, 12:0, 8:1) | Player-facing | Medium — describes the fight from the outside |

Conclusion for the plan: **there is no CoX log corpus to diff against.** The
substitute is the Ash-quote corpus in §11.2 plus the dps-calc attack-speed table,
and that is what the acceptance tests must encode.

### 11.2 The tick corpus — every published tick constant

Verbatim quotes. Each row is a number the implementation is allowed to assert.

**Attack speeds** — from `monsters.json` (`speed`, in ticks). Cross-checked against
the wiki infoboxes' `attack speed`, which agree everywhere they exist:

| NPC | id | Speed | Size | HP (max-scaled) |
|---|---|---|---|---|
| Great Olm (head) | 7551 | **4** | 5 | 800 |
| Great Olm (left/right claw) | 7552 / 7550 | — (do not attack) | 5 | 600 |
| Tekton (normal / enraged) | 7540 / 7543 | **3** | 4 | 300 |
| Vasa Nistirio | 7566 | **3** | 5 | 300 |
| Glowing crystal | 7568 | 4 | 4 | 120 |
| Vespula | 7530 | **3** | 5 | 200 |
| Abyssal portal | 7533 | **2** | 4 | 250 |
| Vespine soldier | 7538 | 4 | 3 | 100 |
| Muttadile (large / small) | 7561 / 7562 | 4 | 5 / 3 | 250 |
| Vanguard (melee/ranged/magic) | 7527/7528/7529 | 4 | 3 | 180 |
| Guardian | 7569 | **4** | — | 250 |
| Lizardman shaman (CoX) | 7573 | 4 | 3 | 190 |
| Skeletal Mystic | 7604 | 4 | 2 | 160 |
| Ice demon | 7584 | **3** | 2 | 140 |
| Deathly ranger / mage | 7559 / 7560 | 4 | 1 | 120 |
| Scavenger beast | 7548 | 4 | 2 | 30 |

The abyssal portal's `speed 2` is independently corroborated by the prose —
"drains the prayer points of players standing in range by three points every two
ticks" — which is the same 2-tick clock. That agreement is why the table is
trustworthy.

**Stat regeneration** — Mod Ash, Discord, 9 July 2026
([archived](https://reldo.runescape.wiki/i/27964246#chat-messages-434286997360738305-1524701424460628080)),
answering "would you per chance be able to say the regen time (in ticks) for mobs
in chambers?":

> Stone guardians - 8 · Muttadiles - 15 · Vasa Nistirio - 10 · Vasa's crystal - 9 ·
> Tekton - 15 · LizardShamans - 20 · Skeletal mystics - 25 · Jewelled Crabs - 1 ·
> Vespula - 10 · Vespula's portal - 45 · Vespula's grubs - 10000 (i.e. never) ·
> Vespine soldier (flying) - 16 · Vespine soldier (walking) - 10000
> Otherwise default should be 100, as for outside CoX.

That is one tick number per npc that no other source publishes, and it is the
single largest block of new fact this pass found. Note the two 10000 entries are
"never" expressed as a rate, not an error.

**Jewelled Crab stun** — Mod Ash, 4 March 2020
([archived](https://reldo.runescape.wiki/i/99812)):

> 1 player: 50-60 ticks; 2-3 players: 30-40 ticks; 4-5 players: 20-30 ticks;
> 6+ playerss: 10-20 ticks

**Vasa's siphon** — Mod Ash, 19 May 2020
([archived](https://reldo.runescape.wiki/i/98628)):

> Every 2 ticks, it heals 1% HP and (10% + 1) Defence.

with the wiki adding the window: "When Vasa reaches a Glowing crystal for the
first time, a 40 second, **66- or 67-tick** timer begins. This timer only counts
down while the glowing crystal is active."

**Vanguard shuffle** — Mod Ash, 14 September 2021
([archived](https://web.archive.org/web/20210915112056/https://twitter.com/JagexAsh/status/1437925375535984642)):

> [How long do the vanguards stay attacking until they go back down to rotate,
> excluding the effects of healing which makes them go down instantly?]
> 20 - 36 ticks, I think.

**Icefiend brazier drain** — Mod Ash, Discord, 13 July 2026
([archived](https://reldo.runescape.wiki/i/27964245#chat-messages-434286997360738305-1526211486844715088)):

> Regardless of whether the fiend decided to wait 3 ticks or 4 ticks before it
> next executed its action, the action contains a 1/6 deduction chance. It's like
> having a weapon with an attack rate that's either 3 or 4, randomly, and that has
> a 1/6 chance of succeeding when it attacks.

**Tekton's cycle** — [Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies):

> Tekton attacks on a **three-tick cycle**, and it is possible for the animation
> to not appear while using this method, so a metronome is recommended for tick
> counting.

**Olm style switch** — Mod Ash, 27 July 2020:

> it looks to me like the head has a **1/5 chance of switching**, each time it
> performs an attack from the head.

**Olm phase-specific attack rate** — Mod Ash, 6 September 2021:

> As it can depend on whether the Olm's doing a cool-down from a previous one,
> I'm not sure the probability is so helpful here (...) but it'd be **1/3
> normally, or 1/2 when the Olm's on its final phase**.

and Mod Ash, 23 August 2022, on how the choice is made:

> It'd decide to do a phase attack, then look at which was available and roll for
> its choice. (...) It tries to remember which it already did, so as not to do
> that one again in the phase.

**Olm hand clench** — [Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies):

> During all phases but the penultimate phase, Olm's left hand will clench if it
> takes a hit equal to or more than **5% of its maximum health within 8 ticks,
> specifically the eight ticks between null and special**. If this occurs, the
> hand will become invulnerable for **roughly 30 seconds**. This will also prevent
> Olm from using its generic special attacks, though power-specific attacks will
> still be used. This mechanic is **removed for the phase once the mage hand is
> incapacitated**.

RuneLite's `CoxPlugin` models that invulnerability as `crippleTimer = 45` ticks —
27 seconds, consistent with "roughly 30".

**Olm head turn** — [Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies):

> Olm will always attempt to face the **quadrant that contains the most players**;
> if no players are present in that quadrant, it will turn towards the quadrant
> with the most players and skip an attack in the process. **If the right hand is
> damaged within 4 ticks and Olm does not scan a player, it will always turn its
> head to the right**, and vice-versa for the left. This factors in all damage,
> such as thralls and burns.

**Olm penultimate-phase heal window**:

> [the left hand's heal-from-damage] is used **two attacks after the teleport
> attack**, and will last for **two of Olm's attacks** before it is lifted.

**Olm phase transition**: "After **7-8** of these targeting crystals have fallen,
he will appear on the other side and start using a new power."

**Olm life siphon** (final phase): the marked tiles "must be stood on within **6
seconds** of the attack being used"; damage "up to **18** damage per player, and
Olm will heal for **five times** the amount of this damage."

**Olm teleport damage** is a distance ladder, not a per-tile constant:

> No damage is incurred if directly on top of each other, **5 if adjacent, 10 if
> two tiles away**. The damage scales the further the targets are and can deal up
> to **50 damage** if as far away as possible.

with solo picking "a random tile in the arena, **up to ten tiles** from the
player's current position".

**Where the phase starts** — [Perfect Olm (Solo)](https://oldschool.runescape.wiki/w/Perfect_Olm_(Solo)):

> Olm starts off each phase beginning with **two auto-attacks** before performing
> its first special attack, Crystal Burst.

That settles an ambiguity the two wiki pages create on their own: the
[Great Olm](https://oldschool.runescape.wiki/w/Great_Olm) page lists the rotation
starting `standard, empty, standard, Crystal Burst`, while
[Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies)
lists it starting `empty, standard, Crystal Burst, standard`. They are the same
12-step cycle read from different origins; the CA page says the *phase* begins at
the Great Olm page's origin, which is what `%cox_olm_step = 1` already does. **No
change needed — but the reason is now recorded rather than assumed.**

**Non-tick formulas that were open questions (R3) and are now published:**

- **Thieving grubs** — [CoX](https://oldschool.runescape.wiki/w/Chambers_of_Xeric):
  `MaxPoints = max(4500, ⌊totalThievingLevel / 6⌋ × 150)`,
  `MaxGrubs = ⌈MaxPoints / 115⌉`, and "the minimum number of grubs required to
  complete the room is equal to **16 times the number of players** in the raid,
  **or 30 in a solo raid**."
- **Guardian HP** — `H = 151 × (1 + ⌊T × ½⌋) + ⌊M̄⌋ × T` where `M̄` is the party's
  average Mining level and `T` the team size. Damage multiplier
  `D = (50 + Mining + pickaxeReq) / 150`, capped so no pickaxe beats dragon.
- **Ice demon kindling** — Mod Ash, 17 June 2021: "the max is your visible
  Woodcutting level divided by 12. (Boosts are respected.) At level 96 that'd be
  8. The game picks a random number **0-max inclusive**, with equal chance of each
  integer. **If it rolls 0, it treats it as 1.**"
- **Brazier lighting** — Mod Ash, Discord, 16 July 2026: "it's a normal skill-roll,
  using **8% success at level 1 and scaling up linearly to 78% at level 99**."
- **Tree depletion** — Mod Ash, 28 June 2020: "The depletion chance normally is
  1/X where X is: 3, or Party size x 2 whichever is higher. **In the Ice Demon
  room, X is multiplied by 5** to make depletion rarer."
- **Tightrope requirement** — Mod Ash, 29 December 2020: the Agility level needed
  is "between 80% and 100% of the average agility level of the team, scaled
  randomly."
- **Shaman count** — 1-4 → 2, 5-9 → 3, 10-14 → 4, 15+ → 5.
- **Mystic count** — 1-2 → 3, 3-5 → 4, 6-8 → 5, 9-11 → 6, 12-14 → 7, 15-17 → 8,
  18-20 → 9, 21-23 → 10, 24-26 → 11, 27+ → 12.
- **CM unique table** — denominator **56**, dexterous and arcane scrolls **12/56**
  each, every other weight unchanged from normal mode (news, 12 August 2026).
- **CM time limits** — 1 → 70 min, 2 → 65, 3 → 50, 4 → 45, **5-10 → 42, 11-15 → 45,
  16-23 → 60, 24+ → 80**. Beating the limit also awards **+5,000 points per
  player**.

### 11.3 Findings — the implementation against that corpus

Ordered by how far the fight drifts from the real one. File references are to
[`minigame_cox/`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/).

**F0 — nothing in the raid is on a clock: 21 `[ai_timer]` hooks, zero
`npc_settimer` calls.** Found while checking F8. The engine gates the trigger on
an interval the npc does not have by default
([`mock230_world.c:3872`](../../../src/net/mock/mock230_world.c#L3872)):

```c
if( npc->active && npc->timer_interval > 0 )
{
    if( ++npc->timer_clock >= npc->timer_interval )
    {
        npc->timer_clock = 0;
        mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_TIMER, npc->type, -1, slot);
    }
}
```

`timer_interval` starts at 0, and the raid never sets it — `grep -c npc_settimer`
over `minigame_cox/scripts/` is **0**, and there is no `[ai_spawn]` block either.
The precedents both arm theirs on spawn: `gauntlet_monsters.rs2:35` and
`inferno_jad.rs2:295` are `npc_settimer(1)`. Phase 4's own header says so
outright — "without it `[ai_timer]` can never fire for an npc no script has
touched".

So today: Olm's action clock never advances, Tekton never wakes out of
`raids_tekton_waiting`, Vasa's 67-tick crystal window never counts down, the
Vanguards never shuffle, Vespula never stings a grub, the icefiends never snuff a
brazier. **Every finding below about a wrong interval is currently masked by
there being no interval at all**, which is also why the tick bugs in F8 and F16
have never been observed. One `[ai_spawn,<npc>] npc_settimer(1)` per timer-driven
record is the fix, and it must land before any of the rest can be tested.

**F1 — Olm's special slot is gated by a probability that belongs to a different
mechanic.** `scripts/cox_olm.rs2:201` rolls `^cox_olm_spec_chance` (1/3, 1/2 final)
before firing Crystal Burst / Lightning / Teleport, and advances the rotation
whether or not it fires. Ash's 1/3 is the chance that a **standard attack** is
replaced by a *phase-specific* attack (acid / flame / crystal). The specials at
steps 4, 8 and 12 are unconditional — that is precisely why suppressing them by
positioning is a *skill*, and why the wiki can say "a well-timed skip by the
player can completely prevent a special attack from occurring at all." As
written, two thirds of specials silently vanish. **Highest-impact single bug in
the raid.**

**F2 — powers are missing entirely.** "Each phase will only feature one power;
Olm will not switch powers during a phase, this is communicated to the player
through the chatbox. Ex: *The Great Olm rises with the power of Acid!*" Acid
spray/drip, deep burn, fire wall, falling crystals and crystal bombs are the
content of every non-final phase; none exist. Their entry point is the 1/3 roll
F1 frees up. The chat lines are also what RuneLite keys its phase detection on,
so implementing them is what makes third-party tooling work against this server.

**F3 — spheres are missing.** They are part of the standard attack, not a
special: red/green/purple, "damaged approximately 50% of their current
Hitpoints" if unprotected, and if the matching protection prayer *was* up when
the sphere launched it is "disabled and the player's Prayer is reduced by 50%".
Without them Olm has no prayer pressure at all.

**F4 — the final phase still runs the wrong specials.** `~cox_olm_advance_spec`
keeps cycling crystals → lightning → teleport → heal in the last phase. The wiki:
"the forced teleport, lightning strikes and crystal burst are **removed**"; the CA
page agrees ("During phase 4, the teleport attack and crystal bombs are no longer
used"). The final phase should be life siphon + constant falling crystals +
powers + spheres, nothing else.

**F5 — clench has a threshold but no clock.** `~cox_olm_check_clench`
(`cox_olm.rs2:335`) tests 5% of base HP correctly and then sets
`%cox_olm_left_clenched`, which **nothing reads**. Missing: the 8-tick window
("the eight ticks between null and special"), the ~30 s / 45-tick invulnerability,
the suppression of generic specials while clenched, and the rule that the whole
mechanic switches off for the phase once the right hand is down. Every solo hand
order in the guides is chosen around this.

**F6 — head turning ignores player counts and the damage tiebreak.**
`~cox_olm_turn_head` (`cox_olm.rs2:186`) takes the first npc-hunted player's zone.
It must pick the zone holding the **most** players, and when no player is scanned
it must fall to the side whose hand was damaged **within the last 4 ticks**
(thralls and burns counting). Solo hides F6 — every team method depends on it.

**F7 — no npc stat blocks for anything except Tekton.** `configs/cox.npc` is 62
lines and covers the six Tekton transforms only. As the file's own header warns,
"an npc that says nothing here silently gets `npc_default.npc`'s
`hitpoints=10`". So Olm's head, both claws, Vasa, Vespula, the portal, the
Muttadiles, Vanguards, Guardians, shamans, mystics, the ice demon and the crabs
all have 10 HP and the default attack rate. Every HP constant in `cox.constant`
is currently decorative. The §11.2 speed/HP table is exactly the block that has
to be written.

**F8 — the abyssal portal will drain every tick instead of every two.**
`cox_vespula.rs2:118` hangs the drain straight off `[ai_timer]` with no counter,
so once F0 arms the timer at interval 1 it drains at double rate. Both the prose
("three points every two ticks") and `monsters.json` (`speed: 2`) say two — so
either `npc_settimer(2)` or an explicit tick counter, and the former is the
honest encoding since it *is* the npc's attack rate.

**F9 — Vasa heals twice, and by a rule that was removed in 2023.**
`cox_vasa.rs2:72` heals 1% every 2 ticks *during* the siphon and then
`~cox_vasa_crystal_expired` heals him to **full** on top. The 29 November 2023
update: "Vasa's healing has been adjusted — he will only heal if he **fully
siphons** from the crystal, rather than healing over time." The 2020 Ash quote our
constant cites is pre-change; the wiki flags that whole section `{{Obsolete}}`.
Correct behaviour: accumulate 1%-per-2-ticks as *pending*, apply it only if the
66-67 tick window expires with the crystal alive, discard it if the crystal dies.

**F10 — Guardian HP is a flat 250 and the published scaling formula is unused.**
`H = 151 × (1 + ⌊T/2⌋) + ⌊M̄⌋ × T`. The damage side is already right
(`~cox_guardian_damage`, cap 61) — this is the other half of the same wiki
paragraph.

**F11 — minion counts don't match their tables.** `~cox_shaman_count` is
`2 + party/2` (party 4 → 4; table says 2) and `~cox_mystic_count` is
`3 + 2×(party-1)` (party 3 → 7; table says 4). Both tables are now known in full,
so both should be exact:
`shamans = min(5, 2 + ⌈max(0, party-4) / 5⌉)`,
`mystics = min(12, 3 + ⌈max(0, party-2) / 3⌉)`.

**F12 — no stat regeneration anywhere.** The 9 July 2026 Ash table above gives a
per-npc regen tick for thirteen npcs, several of them aggressive (Guardians every
8 ticks, Vasa's crystal every 9, Tekton every 15). Nothing in the tree regenerates.
On a slow kill this is a visible difficulty difference, and on the crystal it is
the difference between a clearable and an unclearable window.

**F13 — teleport damage uses the wrong curve.** `cox_olm.rs2:269` is
`distance × 3`, uncapped, measured from Olm. The published ladder is 0 on the
tile / 5 adjacent / 10 at two tiles, rising to a **cap of 50**, measured between
the *teleported targets* (solo: the marked tile, up to ten tiles away).

**F14 — life siphon max hit is 20; the wiki says 18.** `^cox_olm_siphon_maxhit`.
The ×5 heal is right; the 6-second window before the drain is not modelled.

**F15 — Tekton's disputed attack speed is settled, and the constant that lost is
still in the file.** `configs/cox.npc` says `param=attackrate,3` — correct, and
matching both the infobox and "Tekton attacks on a three-tick cycle".
`^cox_tekton_attack_ticks = 4` / `_fast = 3` in `cox.constant` are dead (no script
reads them) but their comment argues for the wrong number. Delete them, or the
next reader re-litigates it. Same for `^cox_guardian_attack_ticks = 5`: the wiki
and `monsters.json` both say **4**, and the plugin's 5 was an animation
observation, not the server's rate.

**F16 — the ice demon's fire multiplier is half what it should be.**
`^cox_icedemon_fire_pct = 150` applied as `damage × 150/100`. The wiki:
"All damage dealt to the demon is reduced by 67%, except for fire spells (which
deal **250%** damage)" — the Strategies page phrases the same number as "fire
spells deal 150% *more* damage". 250 is the multiplier. The 67% reduction (33%
kept) and the 1/6 icefiend odds are right; the icefiend's 3-4 tick action interval
is not modelled at all, so once F0 arms the timer the braziers snuff ~3.5× too
fast. Ash's wording is the implementation note — "It's like having a weapon with
an attack rate that's either 3 or 4, randomly" — so re-roll the interval each
time rather than averaging it to a fixed 3 or 4.

**F17 — thieving's grub requirement is a placeholder that a published formula
replaces.** `^cox_thieving_base_grubs = 20` ("OURS"). Solo is **30**; teams are
**16 × players**. The points side (115/grub, 4500 floor) is already right, and the
max-grub formula `⌈max(4500, ⌊ΣThieving/6⌋ × 150) / 115⌉` is now known.

**F18 — elite clues roll at the wrong time.** `cox_rewards.rs2:90` rolls the 1/12
clue unconditionally. The chest page: "The elite clue scroll is only rolled when
the player does **not** get a broadcasted unique reward." The pet's 1/53 is
correctly gated the other way (uniques only).

**F19 — the unique roll cannot exceed one purple.** `random(100) < ~cox_unique_percent`
tops out at a single item; the real chest carries surplus points into a second
roll ("a team which possesses 855,000 points in total has a 65.7% chance to
receive a unique loot, and then a 32.85% chance to obtain a second"), up to six per
raid. Also unimplemented: the per-player weighting of *who* receives it.

**F20 — CM time limits stop at five players, and the +5,000 bonus is missing.**
`~cox_challenge_time_limit` returns 42 minutes for every party of 5 or more; the
table rises again above 10 (45 / 60 / 80 min). And "completion of a Challenge Mode
raid within the required time will also yield an additional 5,000 points per
player" — nothing awards it.

**F21 — points still hang on an invented coefficient.**
`^cox_points_per_damage = 5` is flagged "OURS" in the file, correctly. Two
documented modifiers are also absent: "when players battle mini-bosses, a *decay*
point multiplier is put into effect", and no points at all for damage to Olm's
hands "anytime it regains control of its hands" in the penultimate phase.

**What is already tick-correct** — worth stating so it does not get "fixed":
the 12-step rotation and its `%step % 4` classification; the 4-tick action clock;
the 1/5 style switch; the catch-up rule including the one-step bound; the 5%
clench threshold; Vasa's 67-tick window, 2-tick heal cadence and `currentHP − 5`
special; the Vanguard 20-36 tick shuffle and the 40% / 33.3% spread thresholds;
the crab stun ladder (`cox_crabs.rs2:62`) which matches Ash's four bands exactly;
the Guardian damage multiplier and its dragon-pickaxe cap; Muttadile's 50% heal
trigger, three meals, 1/3 submerged targeting, three-attack style lock and the
25-tick (≈15 s) give-up; the firemaking 8→78 ladder; the normal-mode unique table
weights; and the tertiary rates 1/12, 1/53, 1/75, 1/400.

### 11.4 Verdict

Not tick-perfect. F0 makes the stronger statement: **there is currently no tick
behaviour to verify** — every heartbeat in the raid is unarmed, so Olm does not
act, Tekton does not wake, and no interval in this document has ever run. Below
that, F1 removes two thirds of Olm's special attacks and F2/F3 leave a phase
consisting of nothing but a ranged/magic auto.

What *is* right is the arithmetic: the 12-step rotation and its classification,
the 4-tick action clock, the 1/5 style switch, the one-step catch-up bound, the
5% clench threshold, the crab stun ladder, the Guardian damage multiplier. Those
are the parts that are hardest to get right and that every solo method depends
on, and they now have their sources recorded next to them. The gap is breadth and
wiring, not foundations.

Ranking for the next pass: **F0 first and alone** — until the timers are armed,
none of the rest can be observed, and any fix to them is unverifiable. Then
**F7** (one config file; gives every npc its HP and attack rate, and `npc_settimer`
has somewhere to live) → **F1, F5, F6** (they change the fight's shape and are
small edits) → **F2, F3, F4** (the actual content of a phase) → the rest.

The lesson F0 carries beyond CoX: a `[ai_timer]` hook that is never armed fails
*silently and completely*, and it reads as finished code. `cox_selftest.rs2` has
495 lines and did not catch it, because it tests the procs — `~cox_olm_step_kind`,
`~cox_guardian_damage` — and never asserts that a spawned npc has a non-zero
`timer_interval`. That assertion is the one to add first.

### 11.5 What is still unmeasured after this pass

- **Damage → points coefficient.** Searched the wiki, its talk page, the Ancient
  chest page, and open-source implementations; nobody publishes it. Even the *cap*
  is unpublished: the wiki says the Guardians' point cap "is currently believed to
  scale non-linearly with the number of players in the raid, but the exact formula
  is not currently known" (Mod Ash, 3 June 2024). `^cox_points_per_damage` stays
  flagged.
- **Per-monster HP/stat scaling by party size**, except the Guardians, whose
  formula is now in §11.2.
- **Room-count and floor-composition rules** for normal-mode generation, beyond
  "7 or 8 rooms per floor" and the room-class counts already in §0.
- **Olm's left/right forced-turn spot labels.** [Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies)
  and [Perfect Olm (Solo)](https://oldschool.runescape.wiki/w/Perfect_Olm_(Solo))
  give the same eight safespots with **left and right swapped** (player-facing vs
  Olm-facing, presumably). The *rule* is unambiguous; only the labelling is. Pick
  one convention, state it at the top of `cox_olm.rs2`, and do not trust a
  screenshot to disambiguate it.
- **Vespula's sting cadence** (`^cox_vespula_sting_interval = 25`, "OURS"). What
  *is* published: grubs have 125 HP, a Medivaemia blossom heals 30, a hatched
  soldier explodes 20 seconds after landing, and the grubs never regenerate
  (regen 10000). Those four constrain it but do not give it.
- **Vespula's grounding threshold** remains 20% ([Vespula](https://oldschool.runescape.wiki/w/Vespula):
  "Upon reaching 20% of her health, Vespula will land") vs 23%
  ([Abyssal portal](https://oldschool.runescape.wiki/w/Abyssal_portal): "she will
  stop flying once she reaches 23% of her Hitpoints"). Two pages, still no
  tiebreak. Keep 20 and keep the note.
- **Falling-crystal damage** has two published ranges: 16-25 direct / 10-16
  indirect ([Great Olm](https://oldschool.runescape.wiki/w/Great_Olm)) against
  20-25 direct / 12-16 indirect (Strategies, phase transition). Same for the
  targeted-crystal power: "up to 15-20" vs "16-20". Unresolved; they may genuinely
  differ between the transition crystals and the power.
