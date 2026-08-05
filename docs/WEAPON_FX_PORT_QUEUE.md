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
make -C src mock230-pack && "$OBJ_DIR"/mock230_pack --check-only   # 0 errors
```

`mock230_pack` is **not** at `./build/` — that path in this file was wrong and
never existed. `make -C src mock230-pack` writes it to `$(OBJ_DIR)/mock230_pack`;
the default-objdir copy is `./src/build/mock230_pack`.

If a slice touched C: `make -C src`. Agents sharing this repo **must** set a
private objdir — stale-`.o` races are real (`embed-binary-build-isolation`).
Three things about that, all measured 2026-08-04, all of which silently produce
a *shared* build if you get them wrong:

```sh
# WRONG — an env prefix does NOT override; platform.mk does `PLATFORM_OBJ_BASE := build`
# unconditionally, and an environment variable loses to a makefile assignment.
PLATFORM_OBJ_BASE=build_wfx_A make -C src mock230-pack     # -> builds into src/build/

# RIGHT — a command-line variable wins over `:=`
make -C src mock230-pack PLATFORM_OBJ_BASE=build_wfx_A     # -> builds into src/build_wfx_A/
```

- **It must be a make *argument*, not an environment prefix.** Verified by
  running both forms back to back: the prefix form printed
  `built build/mock230_pack`, the argument form `built build_wfx_orch/mock230_pack`.
- **Use a relative objdir.** An absolute one (`/tmp/objdir-<lane>`) breaks every
  recipe that does `./$(OBJ_DIR)/...` or `./src/$(OBJ_DIR)/...` — the `./`
  prefix makes `.//tmp/...` resolve against cwd. That hits `mock230-scripts`
  (`./$(OBJ_DIR)/sscompile`), `test-mock230-embed:1478` and `test-content:1289`.
- **If a lane also links the client, override the target too**
  (`PLATFORM_TARGET=torirs_<lane>`, with `torirs_<lane>` as the make goal —
  the goal name must equal the target). `make -C src` otherwise links one shared
  `src/torirs` and two lanes will race it; a relink racing a `cp` dies with
  SIGKILL/exit 137 and an empty log on Apple Silicon.

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
| **C — sound wire** | 6, 7 | `src/net/rev/osrs230/packetin.h`, `src/net/mock/mock230_encode.c`, `src/net/mock/mock230_scripts.c`, `src/net/mock/test/embed_test.c` | A, B, D, E |
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
| 1 | Stance params + resolver | A | done | — |
| 2 | Cross-check the overlap | B | done | 1 |
| 3 | The 667 rsmod-sourced weapons | B | anims done; sounds blocked | 1, 2; sounds need a `synth` **param** kind |
| 4 | The 246 with no direct row | B | done | 3 |
| 5 | BAS + equip sound | B | BAS done+verified; equip sound blocked | 3; equip sound needs a `synth` param kind |
| 6 | `SYNTH_SOUND` wire | C | done | — |
| 7 | `sound_synth` opcode body | C | done | 6 |
| 8 | Swing sounds through content | F | blocked | 1, 7, **3 or 5** (bar only) |
| 9 | Seq frame sounds (client) | D | done | — |
| 10 | Special attack obj table | E | done (orb bar unproven) | — |
| 11 | Special attack FX | G | pending | 8, 10 |
| 12 | Special attack behaviour | G | pending | 11 |

**Statuses above were re-derived from the tree on 2026-08-04, not carried
forward.** Slices 1, 6, 7 and 9 had been completed by an earlier run that did
not update this table; see the Log. Do not trust a status here without
`git show <rev>:<path>` behind it — that stale column cost a whole wave of
agents on this port.

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

> **SPLIT as of 2026-08-04. Anims landed; sounds blocked.** Use
> `--families anims` (now the working command) — the unqualified `--write` emits
> sound params that cannot validate. The anim half is in the tree and green:
> `swing the param default today` **913 → 246**.
>
> **Do not shard this slice** — see §1.1's correction in the Log.
>
> The sound half stays blocked. Original diagnosis:
>
> The 667 rows write and `--check` passes, but `mock230_pack --check-only` then
> reports **2,821 errors**, all of the form
> `cannot resolve param value 'synth_2498'`. Cause, traced to source:
>
> - Slice 1 correctly found no `synth` type in `fields/param.ini`, so it declared
>   `attack_sound_stance1..4` and `equipment_sound` as `type=int`.
> - `--write` emits sound values as **names** (`synth_2498`), which §0.3 says is
>   safe because `pack/4_soundeffects.pack` really is `N=synth_N` for 12,010 ids
>   (verified: line 2499 is `2498=synth_2498`).
> - But the engine's param-type → pack-kind table
>   (`src/net/mock/mock230_content.c:1655-1669`) **has no `synth` entry and there
>   is no `MOCK230_PACK_SYNTH` kind at all**; the comment right below it says
>   `int` and anything unlisted is "a literal, not a name."
>
> So a name-valued sound param is unrepresentable today: `type=int` cannot hold
> `synth_2498`, and no other type exists to declare. **The `synth` namespace has
> a pack file but no server-side pack kind — per PORTING_GUIDE §2.4 the missing
> namespace surface *is* the bug, so this stops rather than being worked around.**
> The 2,821 failures are exactly the sound params
> (`attack_sound_stance1..4` = 662+661+311+656, `equipment_sound` = 531); the
> 3,500-odd anim/BAS params in the same file all resolve.
>
> **This is a decision the queue has not made.** The candidates are: add a
> `synth` pack kind (engine, and no lane in §1 owns
> `src/net/mock/mock230_content.c`); or have the writer emit integers for
> int-typed params; or split sounds into their own file so the anims can land
> now. Pick one deliberately — do not let a lane choose it silently.
>
> **Scope of the blocker, measured — it is narrower than it first looked.**
> `sscompile` **already has a synth symbol kind**:
> `src/serverscript/ssc_symbols.c:485` maps the `4_soundeffects` pack to
> `SSC_SYM_SYNTH`, `:498` declares `synth` as a type name and `:1289` gives it
> type code 80. So **a `.rs2` script may name a synth today** —
> `sound_synth(synth_2720, …)` compiles, and that is naming, not the
> hand-copied integer §0.3 forbids. The gap is **only** the obj-param path:
> `mock230_content.c`'s param-type → pack-kind map has no `synth` row, so
> `type=synth` on a param falls through to "literal" exactly like `type=int`.
>
> Consequences: **slice 11's per-weapon spec sounds are NOT blocked** (they are
> script-level `sound_synth` calls). Lane F's `specwep.rs2` stop should be
> **re-examined** on the same grounds. Only param-carried sounds
> (`attack_sound_stance1..4`, `equipment_sound`) actually need the engine change.
>
> The generated 667-row file is parked at
> `<scratchpad>/attack_anims_modern.obj.blocked` rather than deleted, so whoever
> resolves this does not have to regenerate blind.

**Bar.**

- Standing bar (§0.6).
- **Verify on pixels, headlessly** (PORTING_GUIDE §7). For each of
  `abyssal_whip`, `scythe_of_vitur`, `twisted_bow`, `ghrazi_rapier`,
  `dragon_scimitar`, `dragon_warhammer`, `dragon_claws`, `osmumtens_fang`:
  wield it, attack, and confirm the seq that plays is the one the table names.

  ```sh
  SDL_VIDEODRIVER=dummy MOCK230_VERBOSE=1 TORIRS_ANIM_DEBUG=1 \
    TORIRS_SIM_CLICK_AT=... TORIRS_EXIT_BMP=/tmp/whip.bmp ./src/<client>
  ```

  `::equip <slot>` swaps weapons headlessly. Capture the before/after for the
  log — it is the only evidence that distinguishes "the table landed" from "the
  table landed and is read".

  > **Two corrections, measured 2026-08-04 — read before you try this.**
  >
  > 1. **`TORIRS_ANIM_DEBUG=1` does not print the combat swing seq.** For a
  >    synced world entity, `app_world_apply_entity_anim_tracks` (`src/app.c`
  >    ~12065) sets `el->anim_seq_id` directly and never reaches either path that
  >    prints (`app_world_try_bind_seq`, or the dat2 sequence-load task, which
  >    prints only on the skeletal or failure branch). Use an lldb breakpoint on
  >    `mock230_anim_play_player` instead, with `breakpoint command add <N>` /
  >    `DONE` block syntax — the `-o` auto-continue form silently swallows the
  >    output and reads as "no hits".
  > 2. **"Before this slice all eight play `human_sword_slash` or `human_bow`"
  >    is wrong.** The pre-slice default varies by damage type through
  >    `combat_stats.rs2`'s existing fallback chain: stab → `human_unarmedpunch`,
  >    crush → `human_unarmedkick`, slash → `human_sword_slash`. Measured
  >    `ghrazi_rapier` = `human_unarmedpunch`, `abyssal_whip` and
  >    `dragon_warhammer` = `human_unarmedkick`, `scythe_of_vitur`,
  >    `dragon_scimitar`, `dragon_claws` = `human_sword_slash`. All six are
  >    genuinely reading an unported default — just not the *same* one.
  >
  > Also: `::fight` with no slot picks the **globally** nearest attackable npc
  > across the whole spawn table, not the nearest loaded one — at the plain
  > Lumbridge spawn that is a 2054-tile-away chicken and no swing ever happens.
  > Teleport onto a spawn-file-confirmed tile first (e.g. `::tele 3229 3298`,
  > `areas/lumbridge/configs/lumbridge.spawn:133`) and it lands every time.
- `python3 tools/port_weapon_fx.py --summary` shows `swing the param default
  today` down from 913 by the number you wrote.

---

### Slice 4 — the 246 with no direct row  *(lane B)*

236 share a cache `category=` with a covered sibling (a `_trouver` variant, an
inactive/uncharged form, a barrows ornament kit, a league reskin). **10 have no
source at any tier.**

> **Measured 2026-08-04, and this framing oversold it.** "236 share a category
> with a covered sibling" is true but nearly useless: cache `category=` is a
> *coarse weapon-type bucket* (`1`=weapon_staff, `21`=weapon_slash_sword,
> `55`=weapon_blunt_heavy), and one id spans dozens of unrelated families.
> Checked against the real overlay content rather than category-string overlap,
> **only 4 of 26 relevant categories agree unanimously**, so only **9** of the
> 246 inherit. The other 227 are correctly left bare via this slice's own escape
> hatch — that is the expected outcome, not a shortfall. `weapon_blunt_heavy`
> alone holds 34 gap members across 16 distinct answers (rubber chicken, casket,
> balloon, trophy…) and could never have produced one default.

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

> **Status 2026-08-04: data confirmed, behaviour NOT observed.** The BAS half of
> the data is in the tree and resolvable — `abyssal_whip` carries
> `walk_{f,b,l,r}_baseanim=slayer_abyssal_whip_walk` and
> `running_baseanim=slayer_abyssal_whip_run`, and both names exist in
> `configs/all.seq`. The read path is intact
> (`player/scripts/appearance.rs2:20-26` → `oc_param` →
> `SS_OP_READYANIM/WALKANIM/RUNANIM`, `mock230_scripts.c:4755-4825` → APPEARANCE
> mask). **But nobody has yet watched the stance change while the player moves**
> — the one run made equipped and teleported without walking, so "while moving"
> was never exercised. Grep plus an architecture argument is not this bar.
> **Do not mark slice 5 done on the strength of the data being present.**
>
> The `equipment_sound` half **cannot pass at all today**: it is one of the
> sound params split out of slice 3 and is absent from the tree
> (`grep -c equipment_sound …/attack_anims_modern.obj` → `0`). It is blocked on
> the same missing `synth` pack kind. Verifying it with `oc_param` is not
> possible until that lands.

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
extend `src/net/mock/test/embed_test.c` **against the client's own decoder**, the way
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

> **This bar depends on slices 3/5, which the "Blocked on" column does not
> name.** Slice 8 wires the *call*; the *value* it passes comes from an
> `attack_sound_stanceN` overlay, and those live in lane B's
> `bas/attack_anims_modern*.obj`. With no overlay authored, the swing correctly
> emits `sound_synth(0, 1, 0)` — `combat.param`'s declared default, the "nothing
> plays" convention. So slice 8 is completable and provable on its own, but this
> bar's *specific id* is not reachable until slice 3 or 5 has written data.
> Measured 2026-08-04; see the Log.

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

> **A §1 ownership gap this slice cannot close by itself (2026-08-04).** Ten
> `pvm_*.rs2` files now exist and their anims/sounds are proven at runtime, but
> **none of them can be reached from real gameplay.** Dispatch runs through
> `player_special_attack.rs2`'s `switch_int(oc_param($weapon, sa_kind))`, so
> arming a new spec needs **two files no lane-G agent may write**: an
> `sa_kind` row in `special_attack.obj` (**lane E's**, §1) and a matching
> `case N:` in `player_special_attack.rs2` (**owned by nobody in §1**).
> Measured: all 12 rows carrying `sa_kind` today are the pre-2004 LostCity set;
> every one of the 10 new weapons has `specwep` + `sa_energy` and **no**
> `sa_kind`.
>
> This is not a lane-G shortfall — it is a hole in §1. Decide who owns the
> wiring before slice 12, or every spec written stays dead code.

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

- **2026-08-04** — orchestrated multi-lane run. Wave 1 (slices 1, 6, 7, 9, 10)
  was dispatched against this file's Status column and **four of the five were
  already done**, so most of that wave was wasted. Verified against the
  session-start commit `194aafa4` / submodule `a5f23f2765`, not against prose:

  | slice | table said | tree said |
  |---|---|---|
  | 1 | pending | done — `attack_anim_stance1` + `equipment_sound` in `combat.param`; `[proc,combat_attack_sound]` at `combat_stats.rs2:447` |
  | 6 | pending | done — `packetin.h:142` is `{ 102, 5, PKT_NAME_SYNTH_SOUND }` |
  | 7 | pending | done — `mock230_send_synth_sound` at `mock230_encode.c:1107`, opcode case wired |
  | 9 | pending | done — `app_play_frame_sounds` `src/app.c:5204`, `seq_copy_frame_sounds` `task_dat2_sequence_load.c:124` |
  | 10 | pending, "12 rows" | 97 rows, clean in git |

  **Slice 9's premise is stated as a measurement and is false in the tree it was
  written against**: it says ``grep -rn "frame_sounds" src/ | grep -v /build`` is
  empty. It is not, and was not. Slice 1's completion even left a comment at
  `combat_stats.rs2:438` reading "slice 8 wires it into the swing" — whoever did
  it was reading this queue and did not update it. **The lesson is §0.7 applied
  to this file itself: a status is a claim and needs a command behind it.**

  **A second writer was active in this tree throughout the run.** Commit
  `1db3f5e4` "239 impls" (256 files) landed mid-run and moved the `OSRS-Content`
  pointer `a5f23f2765` → `c3a103eeed`. `make -C src mock230-scripts` drifted
  `12206` → `12246` → `12319` scripts across three measurements taken hours
  apart, none of it from these lanes. §1's guarantee ("no two concurrently-
  running lanes write the same file") holds only over lanes one orchestrator
  controls; it says nothing about a second session. **If you run this queue in
  parallel, establish first that you are the only writer.**

  **Slice 2 — verdicts on the six REVIEW shapes.** Measured
  `403 agree / 46 REVIEW`, matching the queue exactly; `--diff` md5
  `ea5473f9a993aa14a506d22005457858` identical across three runs straddling the
  other writer's commits, which is the proof nothing was edited.

  | n | shape | verdict | evidence |
  |---:|---|---|---|
  | 20 | `rangeattack_anim` `human_stake2`→`human_stake2_pvn` | era-drift, **left undecided** | rsmod re-animates the whole `weapon_category='Thrown'` group to one modern id — category-wide, not a one-off |
  | 14 | `rangeattack_anim` `human_stake2`→`ii_human_dart_throw_pvn` | era-drift, **left undecided** | rsmod gives darts a dedicated throw (`bronze_dart anim_stance1=7554`), split out even from other thrown weapons; the 2004 tree lumps knives/darts/axes under one anim |
  | 7 | `defend_anim` `human_sword_def`→`human_dhsword_block` | **tree-wrong** | same-file self-contradiction: all 7 `*_2h_sword` tiers already use `human_dhsword_slash`/`_chop` for attack (in the agree bucket) but were left on the 1h block; rsmod is `dhsword` end-to-end |
  | 2 | `defend_anim` `human_staff_block`→`human_stafforb_block` | era-drift, **left undecided** | cache geometry: `plainstaff`/`battlestaff` carry only `manwear`, orb staves also carry `manwear2`; LostCity tracks the distinction, the modern client unified the grip |
  | 2 | `crushattack_anim` `human_staff_pummel`→`human_stafforb_pummel` | era-drift, **left undecided** | same two objs, same id pair, same reasoning |
  | 1 | `rangeattack_anim` `human_crossbow`→`xbows_human_fire_and_reload_pvn` | era-drift, **left undecided** | dedicated modern id, same re-animation wave as the thrown shapes |

  Per slice 2's own rule the five era-drift shapes are **reported, not decided** —
  whether this project wants the 2004 animation is not a call the port makes
  silently. The one `tree-wrong` shape becomes slice 2a below rather than being
  fixed in place.

  **Three corrections to this queue, measured:**
  - §0.6's `./build/mock230_pack` does not exist. `make -C src mock230-pack`
    puts it at `$(OBJ_DIR)/mock230_pack`; the default-objdir copy is
    `./src/build/mock230_pack`.
  - §1 lane C's `src/embed/embed_test.c` does not exist. It is
    `src/net/mock/test/embed_test.c`.
  - §0.6 recommends `PLATFORM_OBJ_BASE=/tmp/objdir-<lane>`. An **absolute**
    objdir breaks any recipe that does `./src/$(OBJ_DIR)/...` — it renders as
    `./src//tmp/...`. `test-mock230-embed:1478` and `test-content:1289` are both
    that shape. Use a **relative** private objdir (`build_wfx_<lane>`) for those
    targets.

  **One bug found that is not this port's**: `src/Makefile:1471-1478`
  (`test-mock230-embed`) cannot link for anyone — line 1474 compiles
  `net/bitbuffer.c` from source while line 1477 links `$(OBJ_DIR)/bitbuffer.o`
  out of `NET_CORE_OBJS` (line 772). Duplicate symbol on Apple's linker,
  reproduced from a clean objdir, so it is deterministic rather than a stale-`.o`
  race. Left unfixed: no lane in §1 owns `src/Makefile`.

---

### Slice 2a — the seven 2h-sword `defend_anim` rows  *(lane B, blocked on 2)*

From slice 2's one `tree-wrong` verdict. Seven `*_2h_sword` records
(adamant/black/bronze/iron/mithril/rune/steel) in
`skill_combat/configs/bas/attack_anims.obj` set
`param=defend_anim,human_sword_def` — the 1h-sword block — while their own
`slashattack_anim`/`crushattack_anim` on the same records already use the
`human_dhsword_*` family. Every genuine 1h sword/dagger/longsword in the same
file uses `human_sword_def` correctly, so this is the tree contradicting itself
on one weapon class, not an era question.

**Before writing, resolve one thing this queue has not decided:**
`attack_anims.obj` is **not** in lane B's exclusive-write list in §1 (which names
only `attack_anims_modern*.obj`, `attack_anims_bycategory.obj` and
`attack_anims_manual.obj`). Confirm it is hand-authored and not tool-regenerated
(§0.5). If it is generated, the fix belongs in `attack_anims_manual.obj` instead,
and §1 needs a row for this slice either way.

**Bar.** Standing bar (§0.6); the 7 rows move REVIEW → agree in
`tools/port_weapon_fx.py --diff`; headless, one tier (e.g. `rune_2h_sword`)
blocks with `human_dhsword_block` and not `human_sword_def`.

---

- **2026-08-04** — slices 3 and 10.

  **Slice 3 is blocked on a namespace gap, not on data.** The write itself is
  fine: 667 records, `--check` `0 problem(s)`,
  `--summary` moving `swing the param default today` **913 → 246** with
  `no source at all` still **10** (exactly slice 4's expected remainder). Then
  `mock230_pack --check-only` went **0 → 2,821 errors**, every one
  `cannot resolve param value 'synth_NNNN'`. See the blocker note at slice 3:
  `type=int` cannot hold a name and there is no `synth` pack kind to declare
  instead. Reverted to green (`0 error(s), 17 warning(s)`); the file is parked
  in the scratchpad.

  **§1.1's sharding is broken when the shards actually run.** The queue states
  "4 shards of the 667 gap rows produce 167+167+167+166 = 667 records with 0
  duplicates." Measured, four `--shard i/4` agents produced
  **167 + 125 + 94 + 70 = 456 — a 211-row silent shortfall.** Cause:
  `cmd_write` filters on `not src.row(n)["overlaid"]`, and every landed shard
  makes its own rows overlaid, so each later shard sees a smaller eligible set.
  The shards are disjoint (`uniq -d` empty) — the loss is **coverage, not
  overlap**, which is why nothing errored. The queue's "verified" claim is true
  only if all four read a pristine tree, which is not how they run.

  Proof it was interference and not a changed tree: deleting the four shard
  files returned `--summary` to exactly `170` / `913`, and a single unsharded
  `--write` then produced exactly `667`. **All four shard agents independently
  concluded "the tree has fewer gap rows than the queue documented." All four
  were wrong** — a good illustration of why §0.7 asks for the command and not
  the conclusion. **Do not shard slice 3. Use the unsharded form.**

  **A scratch file leaked into tracked content.** Lane A's slice-1 mutation
  proof, `bas/_scratch_wfx_mutation_proof.obj` — whose own header reads "Deleted
  before the slice is reported done; do not commit" — was swept into submodule
  commit `5ee1b4ae9b` by the concurrent actor's broad commit after its agent was
  stopped mid-run. It set `bronze_dagger`'s `attack_anim_stance1` to
  `human_scythe_slash` for every future reader. Removed. **A mutation proof that
  lives in a packed config directory is a loaded gun; put the next one outside
  the tree.**

  **Slice 10 landed: 97 → 135 rows (+38).** The wiki list was built from
  `Special_attack` → redirects to `Special_attacks`, fetched as raw wikitext via
  the MediaWiki `action=parse&prop=wikitext` API and parsed programmatically —
  not paraphrased, and not URL-mangled from obj names (the first attempt at this
  slice did exactly that and was rejected). Section counts cross-checked against
  the queue's own breakdown: non-combat is exactly 14 (7 axes, 3 harpoons, 4
  pickaxes); combat plus the 4 cooldown semi-specials is 119; 133 total.

  Seven wiki names resolved to two objs each, one plain and one `br_`-prefixed;
  the plain obj was taken in all seven, on the measured grounds that **0 of the
  97 pre-existing rows use a `br_` obj** and `br_` objs are non-tradeable 30–50gp
  Bounty-Hunter reskins.

  **Four items stopped rather than guessed**, each needing a call this queue has
  not made: the **Dragon hasta** family is channelled at "5-100%" and one flat
  `sa_energy` int cannot express it; **Rod of ivandis** has no obj under that
  name, only 10 numbered charge variants; **Vesta's spear (Deadman Mode)** has no
  distinct obj; and **Ancient wyvern shield / Dragonfire shield / Dragonfire
  ward** cost "None (2-minute cooldown)" — declaring `specwep` would arm an orb
  for a mechanic that never touches it.

  **Flagged, not acted on:** `soulreaper` is in the pre-existing 97 with
  `sa_energy=500`, but the wiki says Soulreaper axe's special costs "None,
  consumes Soul Stacks instead". Auditing the original 97 was out of this
  slice's scope.

  **Slice 3's anim half then landed** once the sound params were split out
  (`--families anims`, now the working command): **667 records**,
  `already carry an FX overlay` **170 → 837**, `swing the param default today`
  **913 → 246**, `--check` `0 problem(s)`, `compiled 12369 scripts`, pack
  `0 error(s), 17 warning(s)`.

  **A second aliasing bug, same root cause, found while doing it.** `--write` is
  **not idempotent**: two identical runs produced different md5s and **the second
  wrote 0 records and wiped the file**, because `load_overlays()` scans every
  `.obj` in `bas/` — which is where `--write` puts its output. The slice-0 Log's
  "`--write` verified idempotent" was never true. Fixed by excluding the write
  target from the overlay scan (`load_overlays(exclude=…)`); two runs now give
  identical md5 and 667 records both times. **This fix does not rescue sharding**
  — shard 2 still sees shard 1's differently-named file. Do not shard slice 3.

  **Slice 4 landed: 9 records**, `swing the param default today` **246 → 237**,
  `no source at all` still **10**, pack still `0 error(s), 17 warning(s)`.
  Accounting closes exactly: 9 inherited + 227 left bare + 10 no-source = 246.
  See the correction at slice 4 — the "236 share a category" framing implied
  most would inherit, and measured against real overlay content only 4 of 26
  categories agree. The 10 no-source objs, named rather than guessed:
  `rotten_venator_bow` (cat 15); `keris_partisan` + `_amascut`, `_breach`,
  `_corruption`, `_sun` (cat 1588); `infernal_tecpatl` (cat 2418) and
  `hallowed_flail` (cat 2447), whose category ids are not even named in
  `pack/category.pack`; `tangled_lizard_charged` / `_uncharged` (cat 975).

  One judgement call made and declared rather than buried: four `fishingrod_pearl*`
  objs state **no** cache category, and the only other record in that "unstated"
  group is `osb9_reward_skis`. "Unstated" is not a shared category, so they were
  left bare instead of inheriting ski-pole animations.

  **Slice 3's bar is met — the paired before/after, on the real animation
  path.** `TORIRS_ANIM_DEBUG` was useless here (see the correction at slice 3),
  so the seq was read at an lldb breakpoint on `mock230_anim_play_player`:

  | weapon | BEFORE | AFTER | changed |
  |---|---|---|---|
  | `abyssal_whip` | `human_unarmedkick` (423) | `slayer_abyssal_whip_attack` (1658) | yes |
  | `scythe_of_vitur` | `human_sword_slash` (390) | `scythe_of_vitur_attack` (8056) | yes |
  | `ghrazi_rapier` | `human_unarmedpunch` (422) | `ghrazi_rapier_attack` (8145) | yes |
  | `dragon_warhammer` | `human_unarmedkick` (423) | `human_blunt_pound` (401) | yes |
  | `dragon_claws` | `human_sword_slash` (390) | `human_axe_chop` (393) | yes |
  | `dragon_scimitar` | `human_sword_slash` (390) | `human_sword_slash` (390) | **no** |
  | `osmumtens_fang` | not reached | `human_osmumtens_fang` (9471) | AFTER-only |

  **`dragon_scimitar`'s unchanged value was checked, not waved through.** Its
  new row states `attack_anim_stance1..4 = human_sword_slash, human_sword_slash,
  human_sword_stab, human_sword_slash`, and the sessions resolved `%com_mode=1`
  → stance2 → `human_sword_slash`. The authored value legitimately coincides
  with the old damage-type fallback for a plain scimitar. That is the row being
  read and returning the same answer, not the row being ignored — the other five
  changing is what makes that distinguishable.

  **`twisted_bow` was not captured, and the reason is not slice 3.** Bow +
  `dragon_arrow` produced **zero** `mock230_anim_play_player` hits at 900 and
  2000 frames. `[proc,player_ranged_check_ammo]` has an early
  `return(null)`/`p_stopaction` path *before* its `anim()` call when ammo does
  not validate for the weapon. That is a ranged ammo-compatibility gate, not a
  read failure. Flagged, not chased.

  **Slice 5's BAS bar passed, in motion.** Equip-and-teleport never moves the
  player, so the stance was exercised by teleporting away from the target and
  letting `::fight`'s approach walk, breaking on `World_ApplySecondaryAnim`
  (`world_cycle.c:546`) rather than the attack path:
  - walking (`::run 0`): `slayer_abyssal_whip_walk` (1660) — **640 hits**
  - running (`::run 1`, 16 tiles): `slayer_abyssal_whip_run` (1661) — **96 hits**
    (plus 32 walk hits before run engages), with `mes: Run on (100%).` confirming
    run was actually armed

  Read through the real movement path (`World_UpdateMoverMovementAndAnimation` →
  `World_ApplySecondaryAnim`), not inferred from the config file. **Row written
  and row read.** The `equipment_sound` half remains blocked and absent.

  **Slice 10's bar is only partly met and is recorded that way.** Equipping two
  of the 38 new weapons (`darkbow_blue`, `ancient_goblin_mace`) was confirmed
  end-to-end through `mock230_pack` → `sscompile` → live server → client. The
  click-driven orb arm/drain was **not** conclusively observed: `TORIRS_DUMP_BOUNDS=160`
  printed nothing, so the orb rect was only visually estimated, and the shared
  `testc` account's persisted state produced a misleading "no ammo left in your
  quiver". The arming path is shared unmodified code across all 135 rows, so this
  is not new logic — but the positive proof the bar asks for was not obtained.

- **2026-08-04** — slice 8, lane F. **Content wiring landed and proven; the
  bar's specific id is not yet reachable.**

  `%com_attacksound = ~combat_attack_sound($weapon, %damagestyle)` at
  `combat_stats.rs2:380` and `sound_synth(%com_attacksound, 1, 0)` beside
  `anim(%com_attackanim, 0)` at `:524` in `[proc,player_melee_swing]`; the same
  call at `scripts/player/player_ranged.rs2:53`, mirroring LostCity's own
  `player_ranged.rs2:67`. Upstream shape cited:
  `LostCity_Server/content/scripts/skill_combat/scripts/combat.rs2:64`
  returns `(seq, synth)` from one proc; this tree had already split that into
  `~combat_attack_anim` / `~combat_attack_sound` in slice 1, so slice 8 was only
  the second half plus the callers.

  Proven from a **real combat swing**, not an opcode bypass — headless
  `torirs_F` (`EMBED_SERVER=1`, banner verified as
  `mock230: features era=osrs …`, not the no-embed stub), driven by
  `::container inv 0 abyssal_whip 1; ::equip 0; ::tele 3263 3232; ::fight`:

  ```
  mock230: no trigger for [apnpc2,goblin]      <- op-2 Attack on a goblin
  mock230: sound_synth(0, 1, 0)                <- the new line in player_melee_swing fired
  mock230: -> SYNTH_SOUND  op=102 payload=5    <- encoded and sent, length matches packetin.h:102
  ```

  The `sound_synth(...)` print is `SS_OP_SOUND_SYNTH`'s own host handler
  (`mock230_scripts.c:6757`) and fires only when a **compiled script** executes
  the opcode — which is what distinguishes this from `embed_test.c`, which
  dispatches the opcode directly.

  **The id is `0`, and that is correct, not a bug.** No weapon in the tree
  carries an authored `attack_sound_stanceN` value —
  `grep -rn "attack_sound_stance" …/server/scripts/` finds only `combat.param`'s
  declarations and this new read, and
  `port_weapon_fx.py --sources abyssal_whip` prints
  `overlay <none — swings the param default>`. Lane B's
  `bas/attack_anims_modern*.obj` do not exist on disk yet. So the swing passes
  `combat.param`'s `default=0`. **Slice 8's "Blocked on 1, 7" is incomplete: its
  bar also needs 3 or 5.** Noted at the slice.

  **Scope correction.** The `drop sound_synth` marker count of 145 is
  **tree-wide**; §1 restricts lane F to `skill_combat/**`, which is **12**, of
  which **4** are lane G's `scripts/player/specs/pvm_*.rs2`. Real lane-F scope
  was 8 files plus `combat_stats.rs2` (a caller site, unmarked). In-scope count
  went 12 → 11; tree-wide went 145 → 147 because the concurrent actor added new
  marked files elsewhere mid-run.

  **Four restorations stopped rather than decided** — each needs a call this
  queue has not made:
  - `npc_combat_magic.rs2`, `player_magic.rs2`, `crumble_undead.rs2`,
    `god_iban.rs2` read `magic_spell_table:sound_success` / `sound_fail`.
    **Those columns do not exist** in `skill_magic/configs/magic.dbtable`, which
    is outside lane F and not a `.rs2` file.
  - `npc_combat_ranged.rs2` reads `npc_param(rangeattack_sound)`, **undeclared**;
    the only places to declare it are `npc_combat.param` / `combat.param`
    (lane A's).
  - `specwep.rs2` needs literal synth ids (`rampage`, `sanctuary`).
    `port_weapon_fx.py` resolves only stance-keyed swing sounds from
    `objs.toml`; `rampage` appears in rsmod's `synth.sym` as 2538 and `sanctuary`
    is absent entirely. §0.3 forbids hand-copying the integer past the tool, so
    this stopped — per PORTING_GUIDE §2.4, the missing surface *is* the bug.
  - `player_special_attack.rs2` carries the marker but **LostCity's own file has
    zero `sound_synth` calls** — it is a pure dispatcher. Nothing to restore; the
    marker is wrong. Left untouched rather than editing a header for a
    restoration that does not exist.

  **A transient failure worth recording.** Mid-run,
  `make -C src mock230-scripts` failed on
  `quests/quest_legends/scripts/irvig_senay.rs2:20: unknown variable
  '%lastcombat'` — the concurrent actor's in-flight file, not this port's. It
  self-resolved. The lane correctly refused to park or silence it (§0.1) and
  reported the bar as failed instead of working around it. Re-measured after:
  `compiled 12368 scripts`, `--check` 0 problems, pack `0 error(s), 17
  warning(s)`.
