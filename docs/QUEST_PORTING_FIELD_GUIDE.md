# Quest & content porting — field guide for agents

> Written 2026-08-19, distilled from the failure log of the ToB/CoX/Zulrah/
> Pyramid Plunder/quest-backlog sessions. Audience: an agent converting **OSRS
> wiki pages + Quest Helper code** into `.rs2` content this server runs.
> This is not the architecture doc — read [PORTING_GUIDE.md](PORTING_GUIDE.md)
> for the repo map and the engine/content boundary, and
> [QUESTHELPER_CONTENT_PORT_QUEUE.md](QUESTHELPER_CONTENT_PORT_QUEUE.md) for
> queue state. This doc is the list of ways the work goes wrong *silently*,
> and the cheap test loop that catches it.
>
> The single theme: **almost every failure below compiles cleanly, runs
> without error, and looks finished.** The engine rarely tells you. Your
> defenses are (1) the checklists here, (2) a test that you have PROVEN can
> fail, and (3) the fast build loop in §1 so you actually run things instead
> of reasoning about them.

---

## 0. Before you write anything

1. **Grep the cache first. It probably ships the feature already.** Zulrah
   (all forms, seqs, orbs, the poison-cloud loc, every reward), the entire
   Tombs of Amascut (12 map squares, 183 npcs, 10 interfaces), and the
   Varrock Museum quiz (14 plaque locs with `op1=Study`, interface 533, all
   varbits) were each fully in the cache, referenced by nothing. Search
   `configs/all.{npc,loc,obj,seq,spotanim,varbit,if...}` and the gameval
   names before authoring a single asset. Authoring what the cache ships is
   wasted work *and* usually wrong.
2. **Source ladder.** OSRS wiki raw wikitext (`?action=raw` — NEVER the
   rendered page: multi-version infoboxes render the wrong variant, and
   `{{Skilling success chart}}` params only exist in the raw text) → measured
   plugin data (blert, TobMistakeTracker headers) → Near-Reality/Zenyte
   source for ids the wiki never states (projectiles, sounds, anims) →
   2009scape → Kronos. Quest Helper gives you the **varbit state ladder and
   step order only** (`steps.put(N, ...)`; gameval names resolve in the
   osrs239 pack unchanged) — it defines no dialogue and no combat.
3. **2009scape ports are structurally right and numerically stale.** Pyramid
   Plunder had every mechanic present, was marked `done` in two queues, and
   35 of 38 audited behaviours were wrong because Jagex reworked it three
   times after 2009. **Check the wiki page's Changes section first** on any
   2009scape-derived content.
4. **A `done` row in any queue doc is a claim, not a guarantee.** Audit
   against the current wiki page before trusting it (standing directive in
   the QuestHelper queue).
5. **Before inventing a number for an npc, dump its cache record.**
   `stat1..stat6` in the rev-239 dumps = attack, strength, defence,
   **hitpoints**, magic, ranged. `stat4` + `param=attackrate` frequently
   answer a question filed as "unmeasurable". (But note §4.4: the server
   never *reads* cache stats — you still restate them in the `.npc` block.)

---

## 1. The build/test loop — do it this way, it is seconds not minutes

**You almost never need to rebuild the client or the whole tree.** The loop
for content work is:

```sh
# 1. Rebuild the COMPILER — immediately before EVERY content compile.
#    (Engine gains opcodes constantly; a stale sscompile rejects HEAD's
#    content with "'X' is not a command", then cascades into 100+ phantom
#    "no proc named" errors. That exact shape = stale compiler, not a broken
#    tree. It has cost multiple multi-hour detours.)
make -C src sscompile PLATFORM_OBJ_BASE=/path/to/private/scratch
#    NB: PLATFORM_OBJ_BASE=$S puts the binary at ${S}_opt/sscompile.

# 2. Compile the pack(s). ALWAYS absolute output paths — a relative
#    MOCK230_SCRIPT_OUT resolves against src/ and grows a phantom tree at
#    src/OSRS-Content/ while the real pack never updates.
tools/tob_build_packs.sh          # builds build/ AND the lane pack in one go
# or: make -C src mock230-scripts  (build/ only)

# 3. CONFIRM the "compiled N scripts to .../script.dat" line. A failed
#    compile leaves the OLD script.dat in place and every test then reports
#    on pre-edit data. (Don't pipe through tail -1; the symbols: line prints
#    either way.)

# 4. Run the check.
./src/build_opt/mock230 --selftest              # baseline: 0 failures
```

Rules that keep this honest:

- **Two script packs exist.** `mock230 --selftest` reads
  `server/scripts/build/`; `./run-live.sh <manifest>` reads whatever the
  manifest's `scripts=` names (e.g. `build_summoning_curses`, a *different
  compile* with lanes on). Compiling one and verifying against it proves
  nothing about the other — this cost a 5-hour session of "you didn't fix
  it". The server now **refuses** a stale pack at boot
  (`MOCK230_ALLOW_STALE_SCRIPTS=1` is the escape hatch); do not "fix" a
  refusal by setting the env var — rebuild the pack it names.
- **Selftest flavours cover different stanzas.** No env = the real baseline
  (0 failures). `MOCK230_CACHE=cache.osrs239.rs2012` adds QBD/TD;
  `+ MOCK230_REV=osrs239` adds Inferno/Zuk/ToB and has a **standing ~203-
  failure baseline** — a result there is a *delta against its own baseline*,
  never a boolean. Run the flavour that covers what you changed. Always the
  `src/build_opt/` binary.
- **A/B on the same binary, back to back**, and normalize before diffing
  (`sed -E 's/-?[0-9]+/N/g'` — BSD sed has no `\+`). The selftest shares one
  RNG stream and one tick counter across all stanzas: any change that stops
  npcs wandering shifts 12–15 unrelated-looking failures (facing latches,
  "starting kit has coins"...). Prove it's the RNG by stopping an unrelated
  same-sized npc set and getting the identical failure list. Don't "fix"
  those.
- **A new selftest stanza that spawns npcs or ticks the world moves every
  stanza after it.** Place it immediately before a `selftest_reset_world`
  call, then prove it costs nothing by compiling it out (`if(0)`) and
  diffing normalized failure sets. When the `if(0)` diff is NOT clean, check
  for an npc your own stanza spawned and never despawned before chasing
  anything else — Sea Slug's own C-side checks left one `seaslug` npc
  standing, and that alone shifted 15 unrelated RNG-gated checks several
  stanzas later (Tormented Demons weapon-pierce rolls, a concurrent
  session's own Biohazard test); `mock230_world_npc_free(srv, slot)` +
  `mock230_world_npc_reap(srv)` right after the check that needed it made
  the diff clean again.
- **Headless client runs are only for client-visible behaviour** (anims,
  interfaces, drawing). Embed builds need their own objdir; grep the log for
  `net: this build has no embedded server` before diagnosing anything —
  see [memory: embed-binary-build-isolation]. `TORIRS_SIM_CMD` +
  `TORIRS_ANIM_DEBUG=1` / `MOCK230_SPLAT_DEBUG=1` / `MOCK230_EXT_DEBUG=1`
  are the levers.

**Concurrent sessions share this working tree.** Non-negotiables:

- **Private objdir per session** (`PLATFORM_OBJ_BASE=<scratch>`) for any
  binary you build. Shared `build/` is how another session's half-built .o
  becomes your "bug".
- **Never `git stash`** (two documented disasters — see
  [memory: never-git-stash-here]). Never `git checkout --` a file another
  session may be editing. To baseline, copy the file aside.
- **Save your diff to the scratchpad the moment it compiles**
  (`git diff -- <your files> > $SCRATCH/<topic>.patch`) and refresh it.
  HEAD moves mid-task here; your uncommitted work gets swept into other
  sessions' commits or wiped by a reset. Re-applying a patch takes two
  minutes; reconstructing from a transcript takes twenty.
- A build failing on a file you never touched, or `'X:Y' is not a command`
  for a table that plainly exists, is usually another session mid-edit or an
  `ss_allocate.py` race — check the compile summary counts, wait, re-run.
- Before trusting any before/after suite count, check `git diff` for hunks
  you did not write, and `git log` in OSRS-Content for commits you did not
  make.

**Never rewrite a source file with a Python script using latin-1** (it
truncated `mock230_world.c` to 0 bytes). Use the Edit tool; if scripting is
unavoidable: utf-8, build the full string first, write a temp file,
`os.replace`.

---

## 2. sscompile traps — grep for these BEFORE compiling

Each aborts the whole compile at the first file, so a batch of ported files
surfaces them one build at a time. Sweep first:

| Trap | Symptom | Fix |
|---|---|---|
| `[oploc2,a,b,c]` multi-name header | "expected ']'" | Stack headers: `[oploc2,a]` newline `[oploc2,b]` then one body |
| Comparison as an argument `~p($x = 4)` | "expected ')'" | Resolve to a `def_int` flag first |
| Boolean expr in `return(a >= 1 & b >= 2)` | "expected ')' after return values" | Branch in an `if`, `return(true)` |
| `queue` arity | — | `queue(script, delay, arg)` — 3 args |
| Duplicate script name anywhere in the tree | **hard compile error** | Declare once; branch into a `[label,...]` from the other file. The shared-tool pattern is one owner file + obj categories (see `docs/TOOL_TRIGGER_ORGANISATION.md`) |
| Non-ASCII in a string literal | **silently drops the whole `mes`** — a passing check becomes indistinguishable from one that never ran | ASCII only in literals (comments are fine). `-` not `—` |
| Bare `<`, `>`, `<=` inside a `mes()` string | Lexer desync: "no proc named X" pointing at a proc that exists ~100 lines later | Rephrase ("10 or fewer"); `<...>` is tag syntax |
| `mes(append("lit ",` split across a newline before arg 2 | "expected ')'" reported ~1700 lines later | Keep it on one line |
| Bare stat/enum names colliding with pack symbols | Compiles to the wrong id, silently (`hitpoints` is also param 2100; a `bolts` category vs obj `bolts` cost every crossbow its ammo bonus) | Symbol resolution takes the lowest-numbered kind. When a name exists in two namespaces, rename one (`bolt_ammo`); `switch_category` does NOT disambiguate |
| Coord literals in `.enum` files historically read as 0 | Content walks toward world (0,0) | Fixed for `outputtype=coord`; when adding a NEW `.enum` type, check `pack_kind_for_type` in `mock230_content.c` knows it — the literal fallback is `atoi` |
| A new `param=<name>` on an npc | Silently ignored | Needs a branch in `mock230_content.c` too (npc overlay param whitelist) |
| `attackrate=N` bare key | Parsed nowhere | Must be `param=attackrate,N` |

Runtime abort "`ran past the last instruction without a return`" is a
control-flow hole (script ending in `if (...) { ... return; }` — compiler bug
now fixed, but the message means "look at what the last `if` branches to"),
not a missing statement.

---

## 3. Trigger dispatch — where clicks actually go

- **Rung order for op triggers:** exact type → category → `_` wildcard. A
  **name binding shadows a category binding silently**; `trigger_decline`
  (opcode 11044) lets a rung hand the click to the next rung — it ENDS the
  script. There is deliberately no `[opheldu,_]`.
- **`[opnpc2,goblin]` IS the Attack click.** Content-first dispatch
  *replaces* the engine verb; a script bound over a cache verb must
  re-issue it (`p_opnpc(2)`) or goblins become unattackable.
- **Bind to the multinpc SHELL, never the rung.** The world spawns the
  shell; the server does no runtime multinpc resolution, so
  `[opnpc5,<rung>]` silently never fires. Same for dbtables keyed on npc
  type. And rungs are **player state, not cosmetic variety** — diff
  `op1..op5` across rungs before concluding a cache lacks a feature; the
  varbit ladder is usually the quest's own feature gate.
- **`op<N>` on an obj = GROUND action, `iop<N>` = inventory.** Cache
  `op3=Crush` means `[opobj3,...]`; ground slot 3 is the Take slot and your
  name-bound rung shadows `player/scripts/pickup.rs2` — bind the wrong
  number and the player silently pockets the thing. Probe the negative
  (gone from floor AND absent from backpack).
- **`p_oploc` validates the op against the multiloc-RESOLVED child** (a base
  commonly declares no ops). A skilling loop that "does one pulse then stops
  with no message at all" is this fingerprint.
- **A loc's symbol is this port's name, not the cache's.** Classify what a
  loc IS from its cache `name=`; 49 climb records disagree. And `[oploc...]`
  headers **stack** — a scan keeping only the last header before a body
  misses the rest. **A bound symbol can be entirely invented and still
  compile clean** — Sea Slug's `[oploc1,slugladder]`/`[oploc1,loosepanel]`/
  `[oploc1,fishingcrane]` all resolved to real, valid ids in the pack
  (`configs/all.loc` genuinely has `[slugladder]` etc.) but this cache's own
  Fishing Platform map data placed NONE of them anywhere — the whole
  "help Kennith escape" puzzle was a dead click, discovered only by scanning
  the built scene for the exact id (`mock230_scene_find_loc_id` over the
  loaded window) and finding nothing, then dumping every REAL loc actually
  standing near the target NPC (`mock230_scene_find_loc(x,z,lvl,-1)` +
  `mock230_script_loc_resolve` for the raw id, cross-referenced by counting
  `[` blocks in `configs/all.loc` up to that id) to find the cache's actual
  names (`seaslug_ladder`, `seaslug_crane`, `slug_breakable_panel`).
  **A closed/open pair is often a MULTILOC SHELL, not two standalone
  locs** — `slug_breakable_panel`'s own cache record is
  `multivarp=seaslugquest, multiloc1..9=seaslug_wall_closed, multiloc10..
  13=seaslug_wall_open`, already auto-swapping its own visual off the
  quest's own progress varp; the content only had to bind the shell (never
  a rung) and needed no `loc_change` at all once it did.
- **`[if_close]` keys on the bare interface id; `[if_button]` on the packed
  `(iface<<16)|child` uid.** `[if_close]` is a notification, not a handler —
  the unmount happens regardless.
- **Ops are per-placement** (LOC_ADD_CHANGE_V2 op mask): a door does not
  need a second cache record.

---

## 4. NPC / boss checklist — every one of these shipped "finished" and inert

Run down this list for **every npc a script drives**, and for **every form**
it `npc_changetype`s into (a form missing a line regresses at that threshold,
reading as "it got harder at 70%"):

1. **`huntmode=none` AND `retaliate=no`** on any npc whose attacks come from
   a script. `huntmode=none` alone lasts until the first hit —
   `mock230_combat_hit_npc` latches `combat_target` on damage and the engine
   then runs a second attack clock beside yours. Note
   `combat_stats.generated.npc` emits `huntmode=aggressive` for anything
   with wiki stats, so a generated block can re-arm the bug.
2. **`retaliate=no` is also the "walks toward a goal" fix**: a latched
   `combat_target` makes the npc phase skip its waypoint drain — one player
   splat froze a crab/nylocas forever.
3. **`[ai_timer]` fires only after `npc_settimer`**, conventionally from
   `[ai_spawn,<npc>]`. CoX shipped 21 `[ai_timer]` hooks, zero
   `npc_settimer`: every boss clock inert, zero errors.
   `grep -c npc_settimer` before reasoning about intervals.
4. **Stats come ONLY from `.npc` blocks.** Cache `statN=` is never read;
   defaults are `hitpoints=10`, `attack=1...`. 10 hp reads as "no hitsplats"
   (any hit is lethal, splat gone same tick). The parser takes the FIRST
   `[gameval]` block across all `.npc` files and silently drops later
   duplicates — so an authored combat block must **restate the anim rows**
   the generated file carried, and the packer's "duplicate config" warning
   is not noise. Server-only fields also need the record named in
   `pack/npc.server` or `--server-only` packing drops them.
5. **The swing hangs on `[ai_opplayer2]`, never `[ai_applayer2]`** (approach,
   consumed before the clock). `npc_setmode(applayer2)` once = one swing;
   every tick = a swing per tick bypassing attackrate.
6. **`maxrange` defaults to 7, measured from the SPAWN tile.** Any chase
   across a bigger room silently freezes. State it (CoX rooms are 32);
   also state it when `wanderrange` exceeds ~7.
7. **`npc_findhero` is not a proximity test** (reports the active player
   unconditionally) and `npc_attackplayer` **aborts the script** without an
   active player. Do everything player-shaped inside a `huntall` loop.
8. **One shared search iterator.** `huntall`/`npc_findall*` share it;
   nesting a sweep inside a sweep eats the outer one. `npc_find*uid/exact`
   are safe (lookup, not sweep). A sweeping helper should save
   `npc_uid` on entry and `npc_finduid` it back before returning.
9. **`queue*` fires at the ACTIVE player** — correct inside a hunt loop,
   wrong after it (the active player is whoever the loop reached last);
   `p_finduid($target)` first. `combat_damage_player`'s first arg is the
   attacker **npc_uid**, not the victim.
10. **Queue delays.** Player `queue(s, 1, arg)` is TWO ticks (stored delay+1,
    decrement-then-fire) — per-tick animation wants **0**. An npc queue
    drains in the same phase that armed it and decrements first, so
    `npc_queue(..., 1)` fires the same tick; **2 is the first delay that
    survives a tick** (and from inside the npc phase, add one more).
    `MOCK230_QUEUE_MAX` overflow is an `SSVM_Abort` that kills the REST of
    the calling proc — a big fight's damage sweep can silently drop the code
    after it.
11. **`npc_changetype` traps:** all forms need `hitpoints=` (the Maiden's
    70/50/30 bodies fell to 10 hp and every fight ended at the first
    threshold); a size change moves the south-west anchor (size-3 → size-5
    slides the centre a tile — retele with `movecoord`); any proc matching
    "the boss" by type must try all forms.
12. **`npc_statheal` only moves hitpoints UP** and clamps at the config base
    (the 5-man figure for raid bosses). Scaling DOWN needs the
    heal-up/sub-down pair. **`npc_hitmark` carries the health bar without
    moving health; `npc_damage` latches combat + engine death** — a
    scripted collapse wants `npc_statsub` first, then `npc_hitmark`.
13. **`param=death_drop`** — the npc's own wiki page ("Always" remains line)
    decides bones/ashes/variant/**null**; the default block gives everything
    bones as a fallthrough. Summoned constructs, raid adds: `null`. State it
    in the config; the drop script cannot invent it.
14. **First-attack tick ≠ attack period.** "An opening is one attack period"
    is the recurring wrong guess (Maiden opened on 10 not 9, Verzik on 14
    not 18). They are different numbers; measure or find both.
15. **Dialogue hold is content** (`~chatnpc` → `npc_setmode(playerfaceclose)`),
    not the engine; the mode needs a bound target; release distance is
    footprint-to-footprint.

---

## 5. Scene, loc, and tick-boundary timing

- **The scene rebuilds on the tick boundary, not inside `p_teleport`.**
  `map_blocked` / `loc_find` / `p_walk` in the same script as the teleport
  answer about the OLD scene (a fresh instance reads every tile blocked; a
  route comes back -1 and the player stands in a doorway forever). Queue the
  work one tick later. Corollary: **`p_delay` in a proc reached from a
  `[debugproc]` parks forever under `--selftest`** — a two-tick check cannot
  be a debugproc; put the tick loop in a C stanza or design a control that
  needs no tick.
- **A loc record's `anim=` is the START sequence** — placing the loc plays
  it once and holds the last frame; ask for the loop seq explicitly. And
  **seq `framestep=N` on an N-frame seq means LOOP** (~14 min at default
  maxloops), which reads as "not animating" because the readyanim never gets
  a turn.
- **`spotanim_map`'s 4th arg is a DELAY, not a lifetime.** Anything that
  must persist (a blood pool, a puddle) is a **loc** with a duration.
- **Don't animate an intermediate state and change PAST it in one tick** —
  zone events append, the last word wins, the animation never draws. The
  timed-swap idiom is backwards: `loc_change(final, 0)` then
  `loc_change(intermediate, N)` (reverts to final).
- **The loc revert table is 512 shared world slots**; an overflowing room
  leaves locs changed forever. Sweep your own on cleanup.
- **`loc_del` retires the active-loc slot as a side effect** — read
  `loc_coord`/`loc_angle`/`loc_shape` into locals BEFORE `loc_del`, not
  after (the idiom `quest_cog_gates_and_levers.rs2` uses, `loc_del(500);
  loc_add(loc_coord, ...)`), or the read aborts "the active loc is gone".
  Measured directly during the Family Crest lever puzzle (2026-08-20): the
  read-after form aborted on every single pull; `deal_water_hopper.rs2`
  already snapshots first for this exact reason, and that is the pattern to
  copy. **A `~proc`/`@label` call in between is just as dangerous, and
  silently** — a `loc_find` inside the callee (e.g. a gate's own "is the
  puzzle solved" check) rebinds the active-loc pointer to whatever it just
  matched as a side effect, so `loc_coord` read *after* that call returns
  the callee's loc, not the caller's, with no abort at all. Snapshot before
  calling out, not just before `loc_del`.
- **`~map_instance_from_square` copies exactly one 64×64 square** — floor
  crossing the boundary becomes void (`_block` variant exists; grow east or
  north so local coords keep meaning). `map_instance_alloc` register space
  is shared and finite; pick a private base range and mind neighbours.
- **World state that isn't a player's** goes in `%vars`
  (`configs/*.vars` + `tools/ss_allocate.py`); testing it needs TWO players
  or the shared/per-player distinction is unobservable.
- **A `queue()` you armed will sit BLOCKED forever if `player_can_access()`
  is false, and nothing tells you why.** `player_can_access()` requires
  `mainmodal_group <= 0 && chatmodal_group <= 0` — any open interface,
  including one your OWN debugproc just opened (e.g. `~xxx_journal`
  legitimately does `if_opensub(..., mainmodal, ...)`, matching a real
  player checking their journal), blocks the drain exactly like it would a
  real player who has not clicked it away. `TORIRS_ANIM_DEBUG=1` shows this
  directly: `queue: script=N BLOCKED tick=T mainmodal=<iface>` repeating
  with no `FIRE`. Fix: `mock230_world_close_modal(srv)` before the tick
  meant to drain it — but **one close+tick is not always enough on the
  shared `--selftest` player**: earlier stanzas that also call
  `~quest_complete_rewards` leave their OWN stuck `queue(quest_scroll_show,
  ...)` entries sitting in the same queue array (this player never "clicks
  through" a reward scroll either), and each of those wins the access race
  ahead of yours and re-opens mainmodal the moment it fires. Loop
  close+tick a handful of times per boundary, not once. Found and fixed in
  the Hazeel Cult audit (2026-08-20): `hazeelcultrun2..4`'s completion
  queues never fired until this was in place.

---

## 6. Quests specifically

- **Layout:** `server/scripts/quests/quest_<name>/{scripts,configs}` —
  mirror an existing quest dir. Progress is a varp/varbit allocated via the
  content tree (`ss_allocate.py`), stepped exactly on Quest Helper's
  `steps.put(N)` ladder so the helper's own values document your states.
  Journal + questlist wiring: copy a completed quest, don't invent.
- **Dialogue:** `~chatnpc` / `~chatplayer` / `~mesbox` / `~p_choice2..5` all
  work (choices key on `last_slot`). Write dialogue from the wiki quest
  transcript; the quick guide gives the skeleton and required items.
- **One player script slot.** A conversation left parked disables every
  later dialogue ("dropping a script that suspended while another waits").
  Click-away/`closeModal` handling exists — but a quest script that parks on
  a state it never resumes recreates the bug. Every parked wait needs a
  resume or a discard path.
- **Cutscene/teleport steps:** remember §5 — walk/collision queries a tick
  after the move, and camera/lock state restored on EVERY exit path
  including death and logout (`[logout]` dispatches above the save).
- **Item handouts/rewards:** check `inv_size`/capacity behaviour and use the
  quest's cited wiki rewards table verbatim; xp numbers drift between eras.
- **Locked doors, quest locs:** ops are per-placement; multiloc rungs off
  the quest varbit are usually already in the cache — wire, don't author.

---

## 7. Verification discipline — the part that separates "done" from "demo"

1. **Every new mechanism gets a gate that has FAILED at least once.** Mutate
   the implementation (or a constant) and name which assertion each mutation
   killed. A test that cannot fail is indistinguishable from a broken guard
   — three documented cases read as coverage while proving nothing
   (almost-sorted bsearch, wrong-stack compare, vacuous tile scans).
2. **Vary a LITERAL, not the constant you compare against** — login scripts
   often write the same constant into the varp, so editing the constant
   moves both sides. And don't delete condition clauses "to isolate" one:
   dropping `npc_finduid(...)` also drops the call that sets the active npc.
3. **A selftest silent on success is indistinguishable from one that never
   ran.** Echo the pass line too (and keep it ASCII — §2).
4. **Assert through the dispatch, not around it.** A selftest that calls
   `~make_bolts` directly proves nothing about `[opheldu]`; `::useon` /
   OPLOC1-injection stanzas exist for this. A tile-scan probe must be shown
   able to return non-zero (the ToB blood scan used the wrong loc shape and
   read 0 for weeks — "0 pools on the platform" was vacuous).
5. **Read state at the right time.** Masks (`anim_id`, damage, run steps)
   are cleared by phase_cleanup inside the tick — probe before ticking or
   instrument the encoder (`MOCK230_SPLAT_DEBUG`, `MOCK230_EXT_DEBUG`).
6. **"It does nothing" has two halves** — prove the request reached the
   server and the repaint separately before touching either.
7. **Re-check a documented blocker before quoting it.** "Blocked on data"
   rows go stale; the data is often already in `configs/all.param` or the
   cache.
8. **Report honestly**: what's built, what's a disclosed approximation
   (tag M-numbers like the boss docs do), what's untested and why.

## 8. The two-minute pre-flight, verbatim

```sh
# in OSRS-Content/osrs239-content/server/scripts, before compiling:
grep -rEn '^\[[a-z_0-9]+,[^]]+,[^]]+\]' <your files>        # stacked-name headers
grep -rEn 'return\(.* (&|\|) .*\);' <your files>            # boolean returns
# non-ASCII inside STRING LITERALS only (comments may use any typography):
python3 -c 'import re,sys
for p in sys.argv[1:]:
    for i,l in enumerate(open(p,encoding="utf-8"),1):
        for m in re.finditer(r"\"([^\"]*)\"", l):
            if any(ord(c)>127 for c in m.group(1)): print(f"{p}:{i}: {m.group(1)!r}")' <your files>
grep -c npc_settimer <encounter dir>                        # vs count of [ai_timer]
grep -n 'huntmode=\|retaliate=\|maxrange=\|death_drop' <your .npc>  # §4 lines
```

Then §1's four-step loop. If the first compile error is `is not a command`:
your compiler is stale — rebuild it, do not read another error line.
