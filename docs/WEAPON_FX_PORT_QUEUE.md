# Weapon FX port — guide and queue

> Agent-loop state for weapon attack animations, block animations, swing
> sounds, equip sounds and special attacks (osrs239 cache + rsmod + Kronos +
> wiki → OSRS-Content).
>
> **Read [`WEAPON_FX.md`](WEAPON_FX.md) first** — the measured findings this
> queue was derived from. Then PORTING_GUIDE §4 (porting a slice) and §7
> (guardrails).
>
> This file is written to be executed by a small model without judgement calls.
> Every slice states its inputs, the literal commands, the exact files it may
> write, and a bar that can fail. **If a step requires a decision this file has
> not made, stop and report — do not decide it.**

Status values: `pending` | `in_progress` | `done` | `blocked`.

---

## 0. Rules — read before any slice

### 0.1 Never park sibling content

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content to
green `sscompile`. Live trees that must stay normal paths: `skill_construction/`
(POH), `minigame_mta/`. If `sscompile` fails, fix *your own* file. See
PORTING_GUIDE §7 and `.cursor/rules/no-park-sibling-content.mdc`.

### 0.2 Never `git stash` in this repo

A no-op stash push turns `pop` into restoring the user's *old* stash.

### 0.3 Emit names, never ids

Every reference states integers from *its own* cache. `tools/port_weapon_fx.py`
translates them through rsmod `.data/symbols/seq.sym` and cross-checks against
RuneLite `gameval/AnimationID.java`, refusing any id whose two sources disagree
or whose name is absent from `configs/all.seq`. **Never hand-copy an integer
past the tool.** PORTING_GUIDE §4.1 rule 4.

Sound ids are the apparent exception and are not one:
`pack/4_soundeffects.pack` is `N=synth_N` for 12,010 ids, so `synth_2720` *is*
the name. Still route it through the tool so the range check runs.

### 0.4 Three sources per weapon

`tools/port_weapon_fx.py --sources <obj>` prints the card. **Cache wins for ids
and names. Wiki wins for behaviour. RuneLite is the id↔name cross-check and
nothing else.** The wiki states no animation or sound ids; do not read one out
of it.

### 0.5 Never overwrite a generated file by hand

`--write` output is regenerated. Hand corrections go in a separate file at the
same rank, named `*_manual.obj`. See the `exporter-owns-generated-configs` rule.

### 0.6 Standing bar — every slice, no exceptions

```sh
cd /Users/matthewevers/Documents/git_repos/3draster
python3 tools/port_weapon_fx.py --check          # must print "0 problem(s)"
make -C src mock230-scripts                      # must succeed
./build/mock230_pack --check-only                # must report 0 errors
```

If a slice touched C: `make -C src`. Agents sharing this repo **must** set a
private objdir (`PLATFORM_OBJ_BASE=/tmp/objdir-<lane>`) — stale-`.o` races are
real (`embed-binary-build-isolation`).

`pkill -f build/mock230` also kills `mock230_dev` — match tighter.

### 0.7 Report what you measured, not what you expected

Every log entry states a number and the command that produced it. If a bar
failed, say so with the output. A slice is `done` only when its own bar and the
standing bar both pass.

---

## 1. Parallel lanes — who may write what

**The single rule that makes this safe: no two concurrently-running lanes write
the same file.** The table below is the authority. A lane that needs to touch a
file it does not own must stop and report instead.

| Lane | Slices | Files it owns (exclusive write) | May run concurrently with |
|---|---|---|---|
| **A — resolver** | 1 | `skill_combat/configs/combat.param`, `skill_combat/combat_stats.rs2` | C, D, E (not B) |
| **B — data** | 2, 3, 4, 5 | `skill_combat/configs/bas/attack_anims_modern*.obj`, `.../attack_anims_bycategory.obj`, `.../attack_anims_manual.obj` | C, D, E (needs A done) |
| **C — sound wire** | 6, 7 | `src/net/rev/osrs230/packetin.h`, `src/net/mock/mock230_encode.c`, `src/net/mock/mock230_scripts.c`, `src/embed/embed_test.c` | A, B, D, E |
| **D — client audio** | 9 | `src/engine/dat2/task_dat2_sequence_load.c`, `src/game/rs_audio.c`, the anim tick | A, B, C, E |
| **E — specials data** | 10 | `skill_combat/configs/special_attack.obj` | A, B, C, D |
| **F — sound content** | 8 | the `skill_combat/**/*.rs2` files listed in slice 8 | B, D, E (needs A, C done) |
| **G — specials FX/behaviour** | 11, 12 | one `skill_combat/scripts/player/specs/pvm_<weapon>.rs2` per agent | itself, sharded (needs E, F done) |

Lanes A and B conflict because slice 1 defines the params slice 3 writes. **A
must finish before B starts.** Everything else in the first wave is independent.

### 1.1 Within-lane sharding

Slices 3, 10 and 12 are shardable — many independent items, one file each.

```sh
# slice 3 — N agents, disjoint by construction, verified below
python3 tools/port_weapon_fx.py --write \
  OSRS-Content/osrs239-content/server/scripts/skill_combat/configs/bas/attack_anims_modern_<i>.obj \
  --shard <i>/<n>
```

The partition is on **sorted position**, so `1/8` and `2/8` are disjoint and a
re-run months later produces the same sets. Verified: 4 shards of the 667 gap
rows produce 167+167+167+166 = 667 records with **0 duplicates**.

Slice 12 shards by weapon: one agent per weapon, one file per agent, no shared
file. Do not shard slice 12 by anything else — two agents in one spec file will
conflict.

### 1.2 The three waves

```
wave 1 (parallel)   A(1)    C(6,7)    D(9)    E(10)
wave 2 (parallel)   B(2,3,4,5)                F(8)
wave 3 (sharded)    G(11), then G(12) one weapon per agent
```

---

## 2. The slices

| # | Slice | Lane | Status | Blocked on |
|---|---|---|---|---|
| 0 | Findings catalog + audit tool | — | done | — |
| 1 | Stance params + resolver | A | pending | — |
| 2 | Cross-check the overlap | B | pending | 1 |
| 3 | The 667 rsmod-sourced weapons | B | pending | 1, 2 |
| 4 | The 246 with no direct row | B | pending | 3 |
| 5 | BAS + equip sound | B | pending | 3 |
| 6 | `SYNTH_SOUND` wire | C | pending | — |
| 7 | `sound_synth` opcode body | C | pending | 6 |
| 8 | Swing sounds through content | F | pending | 1, 7 |
| 9 | Seq frame sounds (client) | D | pending | — |
| 10 | Special attack obj table | E | pending | — |
| 11 | Special attack FX | G | pending | 8, 10 |
| 12 | Special attack behaviour | G | pending | 11 |

---

### Slice 1 — stance params and the resolver  *(lane A)*

**Why first.** Everything in lane B writes into the params this slice creates.

**Decision already made** (`WEAPON_FX.md` §7, do not re-litigate): **add stance
keying, keep damage-type keying as the fallback.** Additive — the 170 existing
overlays must keep working with no edit.

**Files you may write:** `skill_combat/configs/combat.param`,
`skill_combat/combat_stats.rs2`. Nothing else.

**Steps.**

1. Read `OSRS-Content/osrs239-content/fields/param.ini` and find out whether a
   `synth` param type exists. If it does, use it; if not, use `type=int` and
   write one comment line saying which and why. Do not invent a type.
2. In `skill_combat/configs/combat.param`, add nine blocks, copying the comment
   style of the file (it explains *why*, not *what*):
   - `attack_anim_stance1` … `attack_anim_stance4`, `type=seq`,
     `default=human_unarmedpunch` for 1/3/4 and `default=human_unarmedkick` for
     2 (these are rsmod's own defaults, `MeleeAnimationAndSound.kt`)
   - `attack_sound_stance1` … `attack_sound_stance4`
   - `equipment_sound` (slice 5 needs it; declaring it now avoids a second edit)
   All with `autodisable=yes`, matching the neighbours.
3. In `skill_combat/combat_stats.rs2`, edit `[proc,combat_attack_anim]` (starts
   line 383) to read the stance param for `%com_mode` **first**, falling through
   to the existing `%damagetype` switch when it is unstated. `%com_mode` is
   0..3 (0 accurate, 1 aggressive, 2 controlled, 3 defensive — see
   `combat.constant`); stance N is `%com_mode + 1`.
4. Add `[proc,combat_attack_sound](obj $weapon, int $damagestyle)(synth)` beside
   it with the same shape. **It has no caller until slice 8.** That is correct
   and is the order PORTING_GUIDE Phase 3 requires — widen the surface, then
   move the behaviour.
5. Leave the punch/kick `if` at the end of `combat_attack_anim` in place, and
   update its comment: it is now the one case the obj layer cannot reach
   (unarmed has no obj to hang a param on), not a patch over a wrong key.
6. **Correct `combat.param:116`** while you are in the file: it says the general
   `attack_anim` is "the cache's own `attack_anim` (param 2635)". Measured,
   `grep -c "param_2635" OSRS-Content/osrs239-content/configs/all.obj` is **0**,
   and no obj param of any number in this cache carries an animation. Rewrite
   the sentence to say it is server-allocated like the rest.

**Bar.**

- Standing bar (§0.6).
- The spear-wield BAS and bronze-scimitar selftest legs still green.
- **A mutation that proves the assertion can fail:** state
  `param=attack_anim_stance1,human_scythe_slash` on `bronze_dagger` in a scratch
  overlay, confirm in the client that is what plays, remove it, confirm the
  damage-type fallback returns `human_sword_stab`. A test green both before and
  after your change is measuring the fallback, not your change
  (PORTING_GUIDE Phase 3, the `opobj` lesson).

---

### Slice 2 — cross-check the overlap  *(lane B)*

449 (weapon, param) rows carry both a LostCity overlay and an rsmod value.

```sh
python3 tools/port_weapon_fx.py --diff > /tmp/wfx_diff.tsv
awk -F'\t' 'NR>1{print $5}' /tmp/wfx_diff.tsv | sort | uniq -c
# expect: 403 agree, 46 REVIEW
awk -F'\t' '$5=="REVIEW"{print $2"  "$3" -> "$4}' /tmp/wfx_diff.tsv | sort | uniq -c | sort -rn
```

**The 46 REVIEW rows collapse to six distinct shapes.** They are already
measured; your job is a verdict per shape, not per row:

| n | param | tree says | rsmod says | what it looks like |
|---:|---|---|---|---|
| 20 | `rangeattack_anim` | `human_stake2` | `human_stake2_pvn` | era drift — modern client re-animated thrown weapons |
| 14 | `rangeattack_anim` | `human_stake2` | `ii_human_dart_throw_pvn` | era drift, darts specifically |
| 7 | `defend_anim` | `human_sword_def` | `human_dhsword_block` | **likely a tree bug** — 2h swords blocking with the 1h block |
| 2 | `defend_anim` | `human_staff_block` | `human_stafforb_block` | staff vs orb-staff variant |
| 2 | `crushattack_anim` | `human_staff_pummel` | `human_stafforb_pummel` | same |
| 1 | `rangeattack_anim` | `human_crossbow` | `xbows_human_fire_and_reload_pvn` | era drift |

**Do not edit any overlay in this slice.** Its only output is the verdict table,
appended to this file's Log with one of `agree` / `era-drift` / `tree-wrong` /
`rsmod-wrong` per shape and one sentence of evidence. "The 2004 animation" may
be the intended answer for this project; that is not a call this port makes
silently. If a verdict is `tree-wrong`, open a new slice for it — do not fix it
here.

**Bar.** All six shapes have a verdict and a cited reason. `--diff` output is
unchanged (you edited nothing).

---

### Slice 3 — the 667  *(lane B, shardable)*

The bulk, and it is mechanical: the tool already resolves every field to a name
and reports 0 problems.

```sh
cd /Users/matthewevers/Documents/git_repos/3draster
OUT=OSRS-Content/osrs239-content/server/scripts/skill_combat/configs/bas
python3 tools/port_weapon_fx.py --write $OUT/attack_anims_modern.obj      # 667 records
# or, sharded across n agents:
python3 tools/port_weapon_fx.py --write $OUT/attack_anims_modern_<i>.obj --shard <i>/<n>
```

A **new file**, never appended to `attack_anims.obj`, so the LostCity-sourced
and rsmod-sourced sets stay separately attributable. The header the writer emits
already names the source and the method; do not edit it.

**Bar.**

- Standing bar (§0.6).
- **Verify on pixels, headlessly** (PORTING_GUIDE §7). For each of
  `abyssal_whip`, `scythe_of_vitur`, `twisted_bow`, `ghrazi_rapier`,
  `dragon_scimitar`, `dragon_warhammer`, `dragon_claws`, `osmumtens_fang`:
  wield it, attack, and confirm the seq that plays is the one the table names.

  ```sh
  SDL_VIDEODRIVER=dummy MOCK230_VERBOSE=1 TORIRS_ANIM_DEBUG=1 \
    TORIRS_SIM_CLICK_AT=... TORIRS_EXIT_BMP=/tmp/whip.bmp ./build/<client>
  ```

  `::equip <slot>` swaps weapons headlessly. Before this slice all eight play
  `human_sword_slash` or `human_bow`; after, each plays its own. Capture the
  before/after for the log — it is the only evidence that distinguishes "the
  table landed" from "the table landed and is read".
- `python3 tools/port_weapon_fx.py --summary` shows `swing the param default
  today` down from 913 by the number you wrote.

---

### Slice 4 — the 246 with no direct row  *(lane B)*

236 share a cache `category=` with a covered sibling (a `_trouver` variant, an
inactive/uncharged form, a barrows ornament kit, a league reskin). **10 have no
source at any tier.**

```sh
python3 tools/port_weapon_fx.py --report | awk -F'\t' '$5=="no" && $8=="-"'
```

Write `bas/attack_anims_bycategory.obj` giving each cache `category=` the answer
its covered members already agree on. If a category's covered members do **not**
agree on an answer, that category gets no default and its members stay bare —
say which in the log.

**The 10 with no source stay on the param defaults.** Name every one in the log
with its category and why nothing covers it. **Do not guess one.** A plausible
guess is precisely the failure this whole port exists to fix.

**Bar.** `--summary`'s `no source at all` still reads 10, and every one of the
236 either inherits or is listed as a non-agreeing category.

---

### Slice 5 — BAS and equip sound  *(lane B)*

368 gap rows carry `ready_anim` / `turn_anim` / `walk_anim*` / `run_anim`; 2,307
rsmod rows carry `equipment_sound`. The `*_baseanim` params and `~update_bas`
already exist (`EQUIP_BAS_PORT_QUEUE.md` slices 1–2), and `--write` already
emits both families, so if slice 3 used `--write` unmodified this data is
**already in the file**. Confirm that first; if so, this slice is verification
only.

**Bar.** Wield `abyssal_whip` and confirm the *stance* changes, not just the
swing — `slayer_abyssal_whip_walk` / `slayer_abyssal_whip_run` while moving.
Equip sound is inaudible until lane C lands; verify the param reads back with
`oc_param` and say so.

---

### Slice 6 — the `SYNTH_SOUND` wire  *(lane C, engine)*

Small and fully specified. `WEAPON_FX.md` §6 has the chain with file:line.

1. `src/net/rev/osrs230/packetin.h:132` — change
   `{ 102, 5, PKT_NAME_NONE }, /* SYNTH_SOUND */` to route
   `PKT_NAME_SYNTH_SOUND`. `src/net/rev/lc254/packetin.h:157` and
   `src/net/rev/lc245_2/packetin.h:154` show the shape.
2. Write the server-side encoder in `src/net/mock/mock230_encode.c` beside its
   neighbours. The field order is **not a choice** — match
   `src/net/rev/gameproto_parse.c:706-710` exactly, which is the client's own
   reader and therefore the spec: id `g2`, loops `g1`, delay `g2`, 5 bytes.
3. The client half needs **nothing**: `src/game/rs_gameproto_exec.c:907` already
   decodes into `RS_Audio_Synth(id, loops, delay)`, and
   `src/game/test/rs_audio_test.c` already asserts a SYNTH_SOUND packet reaches
   the platform as audible PCM from a real cache. Do not touch either.

**Bar.** `make -C src` with a private objdir; `rs_audio_test` still green; and a
packet sent from the server produces audible PCM in the embedded client —
extend `src/embed/embed_test.c` **against the client's own decoder**, the way
the multiplayer and zone work did. Socket RSA is broken; use `embed_test`
(`mock230-persistence-and-xp-table`).

---

### Slice 7 — the opcode body  *(lane C)*

`src/net/mock/mock230_scripts.c:6406` currently pops three ints, prints them
under `MOCK230_VERBOSE`, and returns. Make it send the packet from slice 6.

Then add a line to `docs/osrs230_mockserver.md`: `SS_OP_SOUND_SYNTH` was already
counted in `mock230_opcode_coverage.gen.h` while doing nothing, so the coverage
number has been over-reporting. That is the same "expired reason" failure
PORTING_GUIDE §2.4 item 7 is about, in a different table, and it is worth
recording that the coverage header counts *declared* opcodes rather than
*working* ones.

**Bar.** A `[debugproc]` calling `sound_synth` is audible headlessly.

---

### Slice 8 — swing sounds through content  *(lane F)*

Needs slices 1 and 7.

1. Call `~combat_attack_sound` from the swing in `skill_combat/combat_stats.rs2`
   beside `anim(%com_attackanim, 0)` (line 463), through `%com_attacksound`
   (`skill_combat/configs/combat.varp:143` — the varp already exists and is
   unwritten).
2. Restore the `sound_synth` calls the LostCity port dropped, file by file. Each
   file names itself in its own header:

   ```sh
   grep -rn "drop sound_synth\|sound_synth dropped" OSRS-Content/osrs239-content/server/scripts/
   ```

   Upstream, `[proc,combat_swing_anim_and_synth]`
   (`LostCity_Server/content/scripts/skill_combat/scripts/combat.rs2:64`) returns
   `(seq, synth)` from one proc — that is the shape to restore.
3. **Correct each header comment as you restore it.** A header that still says
   "Era: drop sound_synth" after the drop is undone is the stale-blocker failure
   again, and it is how the next reader concludes the feature is impossible.

**Bar.** Attack with an `abyssal_whip` in the headless client and hear
`synth_2720`. State how you confirmed audibility, not that you assume it.

---

### Slice 9 — seq frame sounds  *(lane D, client, independent)*

Takeable at any time; blocks nothing and is blocked by nothing.

`3rd/rscache/src/datatypes/dat2_config_sequence.c` already decodes all four era
shapes of frame sounds into `def->frame_sounds`. `grep -rn "frame_sounds" src/ |
grep -v /build` is **empty** — nothing consumes it. Wire it into the animation
tick so a frame's sounds queue through `RS_Audio_Synth`.

This slice should need **no rscache change**, only a consumer in `src/`. If you
find yourself editing an rscache write path, stop — read
`3rd/rscache/EXCEPTIONS.md` first and report why.

**Bar.** Mine a rock and hear it (1,588 seq records carry `sound=`, mostly
`human_mining_*` / `human_woodcutting_*` / `human_fishing_casting` /
`human_smithing`). **Do not claim this fixes weapon sounds — it does not.**
`slayer_abyssal_whip_attack` and `human_scythe_slash` carry no frame sounds;
weapon swing sound is server-driven (slices 6–8) and skilling sound is
cache-driven (this one). Say that in the log so the two lanes are not confused
later.

---

### Slice 10 — the special attack obj table  *(lane E, shardable)*

Data only. No behaviour.

```sh
python3 tools/port_weapon_fx.py --specials | head        # obj, display, already_declared, wiki
```

`skill_combat/configs/special_attack.obj` currently has 12 rows (LostCity's set,
stopping in 2004). The wiki's
[Special attack](https://oldschool.runescape.wiki/w/Special_attack) page is the
authority for **which weapons have one and what it costs** — ~63 melee, 20
ranged, 5 magic, 14 skilling, 4 cooldown semi-specials. All 76 sampled resolve
to an osrs239 obj by display name, with zero misses, so the wiki is a usable
index into this cache.

`sa_energy` is out of `^sa_max_energy` = 1000, so a wiki "50%" is `500`.

Give each new row `specwep`, `sa_energy`, and **no** `sa_kind`. An armed spec
with no `sa_kind` handler falls through to a normal swing, which
`player_special_attack.rs2` already does for `$kind = 0`. That is the correct
interim state.

**Bar.** The spec orb arms and drains on every weapon the wiki says has a spec.
Cite the wiki URL per weapon in the log — the cost is a claim about the game and
must be attributable.

---

### Slice 11 — special attack FX  *(lane G)*

Per-weapon animation + spotanim + sound, from Kronos
(`Kronos184-Fixed_2/Kronos-master/kronos-server/src/main/java/io/ruin/model/combat/special/`
— 30 melee, 12 ranged, 1 magic, 4 skilling), routed through the tool's name
translation exactly like slice 3. Kronos states them as:

```java
player.animate(1658);        // seq   -> name via seq.sym / AnimationID
player.publicSound(2713);    // synth -> synth_2713
target.graphics(341, 96, 0); // spotanim, height, delay
```

**rsmod is not the reference here** — its `content/other/special-attacks/` is
early (29 registrations, mostly skilling axes/pickaxes/harpoons).

**Bar.** Each spec plays its own animation and sound. `human_dragon_claws_spec`,
`dragon_warhammer_sa_player`, `sp_attack_dragon_scimitar`,
`dragon_halberd_special_attack` and `slayer_whip_sp_attack` are all present in
this cache by name — confirm on pixels, not by `grep`.

---

### Slice 12 — special attack behaviour, one weapon per agent  *(lane G, sharded)*

**Not mechanical. Do not batch this.** Each spec is a rule: dragon claws' four-hit
damage cascade, dragon warhammer's 30% defence drain, SGS's heal, dragon spear's
shove.

Two sources per weapon, both required:

1. the **wiki article** for what it does (`--sources <obj>` prints the URL);
2. **Kronos** for a working shape.

Where they disagree **the wiki wins** — Kronos is rev 184 and predates several
reworks.

One weapon per agent, one new
`skill_combat/scripts/player/specs/pvm_<weapon>.rs2` per agent, following the
seven existing files' convention. Two agents in one spec file will conflict;
shard by weapon and nothing else.

Take `dragon_claws`, `dragon_warhammer`, `armadyl_godsword`, `bandos_godsword`
and `abyssal_whip` first — they are what players reach for.

**Bar.** Per weapon: the effect is observable in the client, the wiki URL is
cited in the log, and `--check` is still 0.

---

## 3. Log

- **2026-08-04** — slice 0. Measured the gap end to end; wrote
  `docs/WEAPON_FX.md` and `tools/port_weapon_fx.py`.

  Headline: **1,083 combat weapons in this cache, 170 overlaid, 913 swinging a
  param default.** rsmod's `objs.toml` covers 667 of them directly and 236 more
  by category; 10 have no source anywhere. 154 distinct seqs and 85 distinct
  synths across the whole port. `--check` at **0 problems** — every cited id
  resolves to a name that exists here, and rsmod's `seq.sym` and RuneLite's
  `AnimationID` never disagree on one. `--write` verified idempotent, and 4
  shards of the 667 produce 167+167+167+166 with **0 duplicates**.

  `--diff` on the 449 overlapping rows: **403 agree, 46 REVIEW collapsing to six
  distinct shapes** (slice 2's table). One of the six looks like a real tree bug
  rather than era drift — seven 2h swords block with `human_sword_def` where
  rsmod uses `human_dhsword_block`.

  Three findings that were not the question asked: `SS_OP_SOUND_SYNTH` is a
  counted stub that sends nothing; rev230's SYNTH_SOUND packet is
  `PKT_NAME_NONE` while the client's playback path is complete and tested; and
  1,588 seq records carry frame sounds that `src/` never reads.

  One doc correction: `combat.param:116`'s "the cache's own `attack_anim` (param
  2635)" is wrong — this cache has no param 2635, and no obj param of any number
  carries an animation.
