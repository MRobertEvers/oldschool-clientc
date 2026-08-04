# The LostCity port — operating guide

> Written 2026-08-01. This is the **entry point** for anyone (human or agent)
> working on the LostCity → this-engine port. It does not replace the deep
> docs — it tells you which decision you are facing, gives you the procedure
> for making it, and links the doc that owns the detail. Read §8 for the
> reading order before starting a large task.

The goal: **forward-port the LostCity engine's functionality onto this
engine, update it for the modern (rev-230 wire / osrs239 cache) client, then
port the content — keeping LostCity's discipline that almost everything is
content, not engine.** Then add the client features LostCity never had.

The three recurring failure modes this guide exists to prevent:

1. **Hardcoding content in C** because it was faster than finding where
   content puts it. §2 is the decision procedure; it is not a judgement call.
2. **Confusion about which cache pack data belongs in** and how a new data
   type gets packed and loaded. §3 is the model and the recipes.
3. **Treating a modern client feature as un-portable** because LostCity has
   no reference for it. §5 is the pattern: the client usually already
   implements the feature; the server's job is to drive it.

---

## 1. The map — repos, trees, and who reads what

Four repositories:

| repo | what it is |
|---|---|
| this repo (`3draster`) | the client, the server (`src/net/mock/`, "mock230" — a misnomer, it is *the* server), ServerScript (`src/serverscript/`), cachepack (`3rd/rscache/tools/cachepack/`) |
| `OSRS-Content/osrs239-content` (submodule) | **the content tree** — the destination for all ported content |
| `/Users/matthewevers/Documents/git_repos/LostCity_Server` | **the primary content reference.** `engine/` = Engine-TS (branch `254_zuk`), `content/` = the content tree (branch `254_inferno`). Rev **254** (Sept 2004), not 225 — the `_unpack/225` dir is decompiled reference data, not the tree itself |
| `/Users/matthewevers/Documents/git_repos/2009scape` | **authentic mid-era (~Jan 2009 / rev 530) behaviour reference** (Java/Kotlin). Prefer over Kronos for anything that existed by 2009 (farming, hunter, construction, slayer, Pest Control, Barrows, mid-era quests). Never copy rev-530 ids; skip bots/holiday/Summoning/RS2-only. Queue: [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) |
| `/Users/matthewevers/Documents/git_repos/Kronos184-Fixed_2` | **modern / post-2009 OSRS behaviour reference** (Java). Use when LostCity *and* 2009scape have no proc (Wintertodt, Motherlode, rooftops, Zulrah, …). Never copy rev-184 ids; skip custom private-server packs. Queue: [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md). Wire/UI role still per [`UI_ERA_PORTING_GUIDE.md`](UI_ERA_PORTING_GUIDE.md) |

The reference content tree is 1,326 `.rs2` files / 113k lines / 9,376 script
blocks, organized **by subject** (area, quest, skill, minigame) with configs
colocated next to the scripts that use them. Its best documentation is
`LostCity_Server/content/scripts/README.md` — read it before porting anything
behavioral. `content/scripts/engine.rs2` (511 `[command,…]` declarations) is
the reference's entire engine surface, in one file.

The pipeline, end to end:

```
cache.osrs239 (pristine, frozen, archived)
      │ cachepack unpack --assets            (one-time baseline; re-runs are merges)
      ▼
OSRS-Content/osrs239-content/                ← source of truth for what it states
  content.ini      the three-axis namespace register (§3.1)
  fields/*.ini     per-type field register: client vs server scope (§3.1)
  pack/*.pack      id ↔ name, one file per namespace
  configs/all.*    machine-exported config records (rank 0)
  server/scripts/  authored RuneScript + config overlays (rank 1)
  maps/ models/ …  assets
      │ cachepack pack   (ONE baker, merges rank0+rank1, splits per fields/*.ini)
      ├────────► client cache (--out)   native fields + param:N projections + gamevals
      └────────► server/pack            opcode 64..255 bands + idx-128+ name tables
      │ make -C src mock230-scripts     (ss_allocate.py → sscompile)
      └────────► server/scripts/build/{script.dat,script.idx}

mock230 boot (mock230_boot.c — the order is a function, not a convention):
  cache.osrs239.baked  → obj/npc/seq/varbit configs, collision (client's own
                         collision_map.c, LINKED not reimplemented — flags and
                         routing), inv sizes — same bake the client boots
  content tree         → symbols, config overlays, spawns, db tables
  script pack          → ServerScript VM (per session)
```

Key property to preserve: **the server and client read the same cache** for
everything the client can see (collision, varbit layouts, container sizes,
combat params), so they cannot drift. The server band exists only for data
the client has no representation for.

Deep docs: [`osrs230_mockserver.md`](osrs230_mockserver.md) (the server),
[`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md) (the register model),
[`CONTENT_PACK_PLAN.md`](CONTENT_PACK_PLAN.md) (the pack pipeline),
[`LOSTCITY_PORT_TRIAGE.md`](LOSTCITY_PORT_TRIAGE.md) (the measured port plan),
[`serverscript.md`](serverscript.md) (the VM/compiler),
`3rd/rscache/tools/cachepack/README.md` (the tree ↔ cache border).

---

## 2. Decision one: engine or content?

### 2.1 The rule

From [`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md) §8.1, which is the
authority:

> **If LostCity states it in a `.rs2` proc or a config field, it is
> content's. The engine may only be the thing that calls the proc and reads
> the field.** That is the whole test, and it is deliberately not a judgement
> call. Consult the reference before deciding. The answer is in the source,
> not in intuition.

What legitimately stays in the engine is short: the tick clock, hitpoints
and death bookkeeping, the accuracy/max-hit *rolls* (content supplies the
inputs), the wire encoders, the collision map, and dispatch.

### 2.2 The procedure — grep the reference, do not reason from vibes

Before implementing behavior X, run both of these and read what comes back:

```sh
# Is it engine? (LostCity's engine is ~36k lines of TS)
grep -ril '<keyword>' /Users/matthewevers/Documents/git_repos/LostCity_Server/engine/src

# Is it content?
grep -ril '<keyword>' /Users/matthewevers/Documents/git_repos/LostCity_Server/content/scripts
```

Interpretation rules:

- Engine hits that are only vars/flags/pass-through (e.g. `combat` appears in
  `Player.ts` as a timestamp field) do **not** make it engine. Look for
  *logic*.
- If content has a proc, **port the proc, not the field** — do not extract
  the proc's conclusion into a config value and read it from C (that was the
  `levelrequire` lesson's inverse; see triage §10.1 for when data-not-script
  *is* right: when the scripts are pure data tables wearing script syntax).
- If the trigger fires but there is no script, the reference's answer is the
  `_` wildcard script, not a C fallback — and that is now the engine's answer
  too (`osrs230_mockserver.md` §3.18). A trigger with no script does nothing;
  the C behaviours that still stand in are enumerated and counted (five today,
  down from seven — `ai_queue3` moved 2026-08-01, `oploc` 2026-08-02).

### 2.3 Where LostCity actually puts things

Measured from the reference (details in the triage and in
`LostCity_Server/content/scripts/README.md`):

| subsystem | owner | evidence |
|---|---|---|
| tick loop, 11 phases | ENGINE | `engine/src/engine/World.ts` `cycle()` |
| pathfinding/collision | ENGINE | `rsmod-pathfinder` npm; here: linked client `collision_map.c` (tile flags **and** routing — `collision_map_route_tiles` / `collision_map_reached`, used by both `app.c` and `mock230_scene.c`) |
| op/ap interaction resolution | ENGINE | `Player.tryInteract` |
| npc modes (wander/patrol/follow), hunt *mechanism* | ENGINE | `Npc.ts`, `HuntType.ts` |
| queues/timers/delays *primitives* | ENGINE | `ScriptState` suspension |
| player/npc info streams, packets | ENGINE | `rsbuf`; here: `rsareabuf` + `mock230_encode.c` |
| **combat — all formulas, styles, spec, poison, pvp** | **CONTENT** | `skill_combat/` 52 files; engine has zero combat logic |
| **shops** | **CONTENT** | `shop/scripts/shop.rs2` + varps/params/interfaces |
| **dialogue trees** | **CONTENT** | engine provides `chatnpc`/`chatplayer`/`p_countdialog` ops only |
| **all 14 skills, levelup, level requirements** | **CONTENT** | `skill_*/`, `levelup/`, `levelrequire/` |
| **quests (45k lines), drop tables, minigames** | **CONTENT** | `quests/`, `drop tables/` (`ai_queue3`), `minigames/` |
| **npc AI *policy* (death, retaliate, aggro responses)** | **CONTENT** | `ai_queue1/2/3`, `ai_timer`, `.hunt` profiles |
| starting kit, spawn point, login messages | CONTENT | `[login]`, `player/` |
| hunt *profiles* (who to acquire, rates) | CONTENT | 11 `.hunt` files |
| cheats beyond raw engine pokes | CONTENT | 250 `[debugproc,…]` |

If a thing you are about to write is on the CONTENT side of this table and
you are writing C, stop.

### 2.4 The checklist (from `CONTENT_ARCHITECTURE.md` §8.4)

Any "yes" means write content instead of engine code:

1. Does the reference have a proc for this? → port the proc.
2. Is there a game-facing string in your C? → `[proc,*_message]` /
   `player/messages.rs2`.
3. Is there a config-shaped constant in your C? → a param, a `.constant`, or
   a config field.
4. Did the compiler/loader reject a name and you are working around it? →
   the register/namespace is the bug. Fix the namespace policy. **"A
   namespace that cannot grow is a bug, not a constraint."**
5. Are you about to spell a script's name in C? → dispatch a trigger
   instead. (The **10** named hooks in `mock230_scripts.c` are the
   sanctioned exceptions — this line said 9 until 2026-08-01 and 11 until
   2026-08-02, which is the same decay item 7 is about; the list has now gone
   both ways, `equip_level_message` coming off with `MOCK230_FALLBACK_OPHELD`. Do not grow that list casually,
   and note it is not the whole surface: `[proc,npc_default_chat]` goes
   through `mock230_scripts_run_proc_on_npc` and the `mock230_say` family
   names scripts outside the table.)
6. Are you about to write C that runs "when content binds nothing"? → it is a
   row in `enum Mock230Fallback` or it does not exist. That list is four long,
   row in `enum Mock230Fallback` or it does not exist. That list is five long,
   each row names its blocker, and the selftest pins the count: it shrinks as
   the opcode surface widens (§2.5), and adding to it is not a choice a content
   port gets to make. See `osrs230_mockserver.md` §3.18.
7. Did you just implement an opcode, or land an entity binding? → **re-read the
   fallback rows that were waiting on it.** A row's `blocked_on` decays
   silently, and this is measured rather than feared: the 2026-08-01 audit found
   **four of the seven rows** naming a blocker that was false or misdirected —
   three of them cleared by opcodes that had since landed, one wrong from the
   start — and not one had been edited to become so. `ai_queue3` printed "drop tables need npc
   categories" at every boot for two stages after categories landed — including
   through the commit whose own subject line was "the AI_QUEUE3 category rung".
   An expired reason is indistinguishable from a live one by reading.
   `mock230_scripts_stale_blockers` catches the opcode-shaped half automatically
   — the selftest goes red the day a cited opcode lands. The rest (a volume of C,
   an entity kind with no writer, two component lists that disagree) is on you,
   which is why each row cites the one command that settles it.

### 2.5 The current violation worklist

Known content-in-C, verified 2026-08-01 (line numbers drift; grep the
symbol). **Trivial moves** — each is a `[login]`-block or config edit plus
deleting C.

Two things the first batch turned up, both worth knowing before the next one:
**a move is a test**. Seeding hitpoints as xp instead of as a level made the
engine compute the level for the first time and it came out 9, because
`level_for_xp` (now `mock230_combat_level_for_xp`) summed the xp formula's
terms without the reference's per-term `floor` — 94 of the 98 thresholds were
one xp too high, invisible for as long as the only caller stated the level and
the xp as two independent literals. Player saves store boosted + xp only and
derive base on load; `::setlevel` writes the matching XP threshold.

- ~~starting inventory `kit[]`~~ — **moved**, `[proc,newplayer_inv]`
- ~~starting bank stock `stock[]`~~ — **moved**, `[proc,newplayer_bank]`; the
  slots[] bypass is gone with it (`inv_add` now marks the bank through
  `container_dirty`, which it did not — it dirtied a *worn* slot instead)
- ~~starting stats / hitpoints-10~~ — **moved**, `[proc,newplayer_stats]`, as
  `stat_advance(hitpoints, 11540)`. Level 1 in everything else stays engine:
  it is the floor, not a starting state, and the reference agrees
  (`PlayerLoading.load()`). All three are seeded from `[login,_]` via
  `~newplayer_setup`, behind a `%newplayer_seeded` perm varp so the script is
  idempotent — see `player/newplayer.rs2`.
- ~~fallback NPC greeting~~ — **moved**, `[proc,npc_default_chat]`, and now
  visible: the C set only the SAY mask, which this client stores and never
  draws.
- ~~four raw `"Nothing interesting happens."` literals~~ — **moved**, one
  `[proc,nothing_interesting_message]` behind `mock230_say`
- ~~default appearance kit `k_default_kits[12]`~~ — **moved**,
  `player/configs/appearance.enum`, read by the encoder the way
  `mock230_equipment.c` already reads `worn_slots`
- ~~the npc `death_drop` fallback~~ — **moved** 2026-08-01, `[ai_queue3,_]` in
  `skill_combat/npc_combat.rs2`; `enum Mock230Fallback` 7 → **6**. Not a
  trivial move by the list above's definition — it is the first completed
  Phase 3 eviction — but it belongs here because the *blocker* was trivial:
  there was not one. See below and `osrs230_mockserver.md` §3.18.
- ~~the ground-obj take~~ — **moved** 2026-08-02, `[opobj3,_]` in
  `player/scripts/pickup.rs2` over five new `obj_*` opcodes and a new active-obj
  entity binding; `enum Mock230Fallback` 6 → **5**. The second completed
  eviction and the first whose *order* was measured: the two selftest legs that
  assert the take stayed **green** under "unbind the script" while the C was
  still present (the fallback answered them) and turn red only after it is gone
  — 3 red before, 11 after, same mutation. That asymmetry is why item 7's order
  is not a formality. `osrs230_mockserver.md` §3.18.
- ~~wear/wield and drop~~ — **moved** 2026-08-02, `[opheld2,_] ~equip(last_slot)`
  and `[opheld5,_] ~dropslot(last_slot)` in `player/scripts/{equip,drop}.rs2`;
  `enum Mock230Fallback` 5 → **4**. Third completed eviction, and the one whose
  blocker turned out not to be an opcode at all: the level requirement had no
  script-readable form, so binding `[opheld2,_]` would have dropped the gate in
  silence. `skill_combat/configs/levelrequire.dbtable` is that home, and building
  it found that the requirement is a **merge** — the `.obj` overlay every account
  of it named is 857 objs of 1,496. Same before/after asymmetry as `opobj`, and
  starker: **0** checks red under "unbind the script" with the C present, **18**
  after it is gone. `osrs230_mockserver.md` §3.18.
- most of the `::` cheat ladder — (`::pray`, `::dropobj`, `::equip` and
  `::dropslot` already migrated to `[debugproc]`s — the `::equip` C branch went
  with `equip_from_slot`; the rest follow the same pattern) — **still open, and
  the only trivial one left**
- ~~the loc category rung~~ — **landed 2026-08-02**. Not a trivial move (it is
  Phase 2 surface, not Phase 3 eviction) but it belongs on this list because it
  is the precondition the `oploc` row was measured against: `interaction_category`
  answers for locs now, `pack/category.pack` covers all three record types, and
  `tools/port_category_crawl.py --domain loc` is the crawl. 91 reference loc
  categories → 11 minted, 2 held back as `broader`, 2 `allocated`, 3 `split`,
  73 `orphan`.
- ~~the doors and the ladders/stairs~~ — **content as of 2026-08-02**.
  `doors/scripts/{doors,door_procs}.rs2` (the reference verbatim: category-keyed,
  `loc_del` + `loc_add`, so the door swings to the next tile) and
  `ladders_stairs/scripts/{ladders,climb_shared}.rs2` (four allocated categories
  over 1,428 of the cache's 1,445 climb-verb records). Three engine defects fell
  out of the move and all three are the same
  shape, an argument pair every caller happened to pass as equal numbers:
  `LOC_ADD` popped angle/shape transposed, `MOVECOORD` added `$z` to the plane,
  and `P_TELEPORT` did no plane-change bookkeeping at all. A fourth was a content
  *surface* gap: a `.loc` overlay's `param=` was readable only from C, so
  `loc_param(next_loc_stage)` answered the declared default.
- ~~the bank booths, and with them the `oploc` row~~ — **evicted 2026-08-02**.
  78 loc records in this cache put "Bank" on a menu op and content bound one;
  `tools/bank_import.py` generates the other 77 as `[oploc<n>,<name>] ~openbank;`
  into `interface_bank/scripts/bank_booths.rs2`, `--check`ed by `test-port`.
  `mock230_bank_open` is untouched and stays engine — what moved is which locs
  reach it. Names and not a category because the three cache categories the
  booths carry have 63/44/1 members of which 58/11/1 say "Bank" (one of the
  strays is the Grand Exchange wall), and because the reference binds every
  booth by name too. `interaction_engine_loc` + `climb` deleted, count 6 → 5.
- most of the `::` cheat ladder — `mock230_world.c:2388-2683` (`::pray`
  already migrated to a `[debugproc]`; the rest follow the same pattern) —
  **still open, and the only trivial one left**

**Blocked moves**, re-measured 2026-08-01 — blocked on the ServerScript opcode
surface, not on willingness. The order is deliberate: *widen the opcode surface
until a script can say it, then move it* (`osrs230_mockserver.md` §6.1 steps
4–5). Do not move these early by inventing non-reference C↔script hooks.

**The first correction is the size of the problem.** The standing figure was
"~3,200 lines: bank 1,370, combat 858", which is (a) stale and (b) measuring
the wrong thing — it counts whole files, and the fallback rows are not whole
files. Both columns measured today:

| row | the file it blamed | what the row actually is |
|---|---:|---|
| `opnpc` | `mock230_combat.c` 858 → **1,061** | `interaction_engine_npc`, **31 lines** — a strcmp on the cache's Attack verb plus the FACE_ENTITY latch. Combat stays engine either way |
| `oploc` | — | `interaction_engine_loc`, **84 lines** + `climb` **34** |
| `opheld` | "equipment is C", then "seven opcodes" | the OPHELD arm of `handle_opheld` (150 lines total), ~50 of them. **Both reasons are now gone** (2026-08-02): `mock230_equipment.c` is **134** lines of component→worn-slot map and was never the policy, the *screen* is already content, and of the seven opcodes five landed and two were wrong. What is left is not an opcode — see below |
| `inv_button` | `mock230_bank.c` 1,370 → **1,395** | `mock230_bank_quantity_for_op`, **107 lines** |
| `if_button` | same | `mock230_bank_handle_button`, **64 lines** — and it is the *settings/deposit* ladder, not the quantity one; the two rows share one router |
| world map | `mock230_worldmap.c` **199** | not a fallback row at all |

So the blocked surface is roughly **370 lines of dispatch**, not 3,200 lines of
game. The 3,200 is what those 370 lines *reach*, and most of it (combat, the
bank's arithmetic) is engine in the reference too.

**The second correction is the blockers themselves, and it is the one that
matters.** All seven were re-checked against the tree rather than inherited, and
**four named something false or misdirected** — three overtaken by work that
landed in other lanes, one wrong from the start:

- `ai_queue3` — "drop tables need npc categories": categories, the category rung
  and 69 drop-table files had all landed. Nothing blocked it. **Row deleted.**
- `oploc` — "needs `loc_*` and a per-loc destination": the whole `loc_*` family
  and `p_teleport` landed. The real gate was that `[oploc<n>]` binds no active
  loc — `SSVM_ENT_LOC` was written only by `loc_find` and the iterator, so
  `loc_coord` in a door script aborted. **All of it is cleared as of
  2026-08-02** (the active-loc binding, the category rung, `LOC_CATEGORY` /
  `LC_CATEGORY` / `LC_DEBUGNAME` / `P_OPLOC`) and the row is still there, because
  the 118 lines of C have not moved. `blocked_ops` is empty for the first time.
- `opobj` — "no `obj_take`/`inv_add` opcode pair": not a pair and not opcodes
  first. `SSVM_ENT_OBJ` had zero writers *and zero readers* tree-wide, so no obj
  opcode would have had a subject. **Closed 2026-08-02**, both halves: the
  entity kind, five `obj_*` opcodes and `player/scripts/pickup.rs2` landed, then
  the 39 lines of C went and **the row with them**. What that C did was answer
  `opobj` 1, 2, 4 and 5 by taking the pile — it read no op number — over 75
  ground-op lines in `configs/all.obj` that say Light, Remove, Study and Lay.
  The reference answers those in content (`[opobj4,_category_22]` is
  firemaking's Light) and answers an unbound one with `Player.defaultOp`.
  Deleting it was a behaviour decision, taken and stated
  (`osrs230_mockserver.md` §3.18).
- `opheld` — "equipment is C", then "seven declared-unimplemented opcodes".
  **Both false as of 2026-08-02, and the row still stands** — which is the
  honest shape of a half-finished eviction rather than a failure. Five opcodes
  landed (`oc_wearpos`/`2`/`3`, `inv_movefromslot`, `inv_dropslot`, plus
  `inv_moveitem`'s missing generic arm) and two came off the list as wrong
  rather than deferred: `BUILDAPPEARANCE`, whose real job is *selecting which
  container the appearance encoder reads* — which this encoder cannot do, so an
  implementation would be silently plausible and wrong (§3.13d) — and
  `P_CLEARPENDINGACTION`, which `handle_opheld` never calls at all. The content
  is written (`player/scripts/equip.rs2`, `player/scripts/drop.rs2`) and reached
  through `[debugproc]`s. **The one live blocker is that the level requirement
  has no script-readable form**: 857 objs / 1,254 (stat, level) pairs, which
  `oc_param` cannot hold (a param maps one id to one scalar) and which no
  reference opcode reads. Triage §10.1's *conclusion* survives — the values are
  data — and its *inference* does not: "therefore the gate is C" rested on
  opcodes that now exist. A dbtable with two `LIST` columns is the shape.
  `osrs230_mockserver.md` §3.18.

`inv_button`/`if_button` blamed the bank's line count when the obstacle was
**addressing**. That is closed for the bank: numbered `[if_button1..8,bankmain:items]`
binds the sparse CS2 ladder (no `last_verb`); the client emits `IF_BUTTON1..10`
for armed component ops. The fallback rows remain for unbound clicks elsewhere.

The cheapest next unlock is therefore **structural, not an opcode**: give
`[oploc<n>]` an active loc — or, for `opheld`, give the level requirement a
script-readable home, which is a data relocation rather than an opcode too. Note it only unblocks *part* of `oploc` — the
reference's `[oploc1,_door_closed]` needs a loc category rung, and the real loc
category field needs `dat2_config_loc.c` to stop discarding config opcode 61,
an rscache write-path change. That is the expensive half and it was invisible
from the old string.
~~The cheapest next unlock is therefore **structural, not an opcode**: give
`[oploc<n>]` an active loc.~~ — **done 2026-08-02**, together with the expensive
half it was supposed to only partly unblock. `dat2_config_loc.c` keeps config
opcode 61 now; `mock230_loc_category` reads it. One correction the estimate got
wrong and it is worth carrying forward: the rscache change was **not** what
landed the doors. Not one of this cache's 776 door records states a category, so
`[oploc1,_door_closed]` needed the `category` namespace to be allowed to *grow*
(§2.4 item 4) — an allocation base, two authored ids, and a crawler that reports
the difference. The cache field bought the other half of the row: 8,407
categorised loc records, including the 63 the cache files as bank booths.

Each surviving string is now written to be **checkable in one command** — a
symbol as `ss_opcode.h` spells it, a `file:line`, a `wc -l`, a reference path —
and the opcode-shaped half is machine-checked: every row lists the opcodes it is
waiting on, and `mock230_scripts_stale_blockers` fails the selftest the moment
one of them is implemented. That is the guard against how the list actually
failed. `ai_queue3` was not a wrong row; it was a right row whose reason had
expired, printed unchanged at every boot for two stages. Prose cannot go stale
loudly, so the citation is data as well as text. Two blockers are deliberately
*not* covered, because they are not opcodes and a wrong assertion is worse than
none: `opnpc` waits on 1,061 lines of `mock230_combat.c`, and `if_button` waits
partly on `bank.rs2`'s bindings and `bank_set_events`'s arms naming different
components (verified disjoint against `interfaces/bankmain.compack` — 23 vs 24,
25 vs 26, 29 vs 30 — so eleven compiled `[if_button,bankmain:*]` scripts can
never fire). Both cite their grep instead.

---

## 3. Decision two: which pack, and how to add a data type

### 3.1 The model

There is **one content tree, one baker, two outputs**. The split question is
never "which directory do I put the file in" — it is answered per **field**
by a register, and per **namespace** by another register:

- **`content.ini`** — the namespace register. Three axes per namespace
  (`src/content/content_register.h`): id authority (`cache` / `server` /
  `protocol`), name authority (`cache`=gameval / `authored` / `derived` /
  `imported`), gameval archive, `server_base` (first server-allocatable id:
  npc 20000, obj 40000, loc 70000, …), `cache_index`. Read by three
  consumers — cachepack, sscompile, mock230 — as a contract, not shared code.
- **`fields/<type>.ini`** — the field register. Each field of a config type
  declares its client-side disposition and/or server-side opcode.
  **The default is `scope = server`, `client = drop`: a field reaches the
  client only because someone wrote that it does.**

Field dispositions (see `3rd/rscache/tools/cachepack/cp_fields.h` and
`src/net/mock/mock230_servercodec.h` — both written as prose, read them):

| declaration | meaning | lands in |
|---|---|---|
| `client = <native>` | a real cache opcode the client decodes | client cache |
| `client = param:<name>` | projected into the record's param table | client cache (params are readable by anyone; secrecy is explicitly not a goal — `CONTENT_PACK_PLAN.md` §0 decision 4) |
| `client = drop` | no sensible cache representation | nowhere client-side |
| `client = error` | this field must never appear | fails the build |
| `server = opcode:<N>` | server band, opcodes **64..255** (disjoint from client 1..147 by design) | `<tree>/server/pack` |
| `ref = <namespace>` | values resolve symbolically through that pack file | — |

Server-only **record types** (not fields on a cache record — e.g. prayers,
and future ones like hunt profiles) get a dat2 group id at **128..255**
(`dat2_configs.h` uses 1..39; 40..127 is reserved for OldSchool growth).

The merge that feeds the baker: `configs/all.<type>` is rank 0 (machine
export from the cache), `server/scripts/**/configs/*` is rank 1 (authored).
Merge is **per key**; rank 1 overrides rank 0; a duplicate at the same rank
is an error. New records are **opt-in** per type (`records = client`) —
a block that exists only at rank 1 is presumed a server table unless the
type says otherwise.

### 3.2 The decision table for new data

Ask in this order:

1. **Can the client already represent it?** (a native opcode exists in the
   client's decoder for that config kind) → `client = <native>`.
2. **Does the client need it but only via scripts/params?** (CS2 reads it,
   or tooling wants it) → `client = param:<name>`, declare the param.
3. **Only the server needs it, and it is a field on a cache record?**
   (combat stats on an npc, `next_loc_stage` on a loc) →
   `server = opcode:<N>` in `fields/<type>.ini`, next free opcode ≥64.
4. **Only the server needs it, and it is a whole new record type?**
   (prayer defs, hunt profiles) → new namespace in `content.ini` with
   `ids = server`, new group id at 128+, a `fields/<name>.ini`, a decode
   struct in the server.
5. **It is behavior, not data?** → it is RuneScript, and it never enters any
   cache (`cachepack` does not look inside `server/scripts` at all — that is
   what makes "scripts are not encoded into the cache" true by construction).

### 3.3 Recipe — new field on an existing type

1. Declare it in `OSRS-Content/osrs239-content/fields/<type>.ini` with the
   dispositions above (`ref = <ns>` if the value is symbolic).
2. Author it in a rank-1 config overlay under `server/scripts/**/configs/`.
3. `cachepack pack` — watch the per-field counts it reports; `client = error`
   or an undeclared opcode fails the build, silence is a bug.
4. Server side: add the field to the type's decode table for
   `mock230_servercodec.c` (generic over `(field table, record base)`) and
   the runtime struct. `mock230_servercodec_test` iterates the registry, so
   a registered type without a `fields/<name>.ini` fails the test.
5. If ServerScript should read it: expose it as a param (preferred — zero
   new opcodes) or add the `oc_/nc_/lc_` opcode with a **real signature** —
   the VM refuses `known=0` opcodes deliberately.

### 3.4 Recipe — new server-only record type

Follow prayer, the worked example (`mock230_db.h`, and
`CONTENT_PACK_PLAN.md` §5.4 "declared but not implemented" for the parts
still open): prefer a **dbtable** if the shape is tabular — the server
already has a full `.dbtable`/`.dbrow` runtime with ids allocated above the
cache's high-water mark — before inventing a bespoke record type. Only mint
a new namespace + 128..255 group when the data genuinely isn't tabular
(needs strings, repeated groups, cross-references), and note the band
currently has integer wires only; string/list wires are part of the job.

### 3.5 Recipe — new client-visible content (models, seqs, interfaces…)

Assets go in the tree's asset directories and pack via cachepack's asset
tables; ids come from `pack/<ns>.pack` (id authority per `content.ini`;
server-allocated bases keep clear of cache ids). Cross-era asset conversion
(LostCity `.ob2`/`.anim` → dat2) is **`tools/port_lostcity`'s** job — going
the other direction from its current use; cachepack deliberately transcodes
nothing. Keep hand-authored additions in manifest `[extra:<name>]` sections
so re-export is idempotent (see `3rd/rscache/tools/port_lostcity/lc_manifest.h`).

### 3.6 Phase 0 — the gaps that make this confusing today

These four are why "what goes in which pack" currently *feels* unresolved
even though the design is done. Close them before large-scale porting:

1. **~~The server band is written but never read~~ — read at every boot now.**
   `make -C src mock230-servpack` (`cachepack pack --server-only`, no cache
   needed) rebuilds `server/pack`, and `mock230_boot_load` step 2b reads it:
   every archive is verified identical to the text parse before being decoded
   over the live defs, a stale or unreadable band falls back to text loudly,
   and `mock230_pack` fails on a band that disagrees with the tree. See
   `osrs230_mockserver.md` §3.10b for the verification's three-way rule and
   what stays text (`huntmode`/`nomove` have no integer wire; `[default]`,
   patrol and `loc.category` were never band fields). What remains of this
   item is the last sub-step: remove the text parse for the band-carried
   fields now that boot proves the two identical.
2. **~~Delete the second baker~~ — deleted.** `mock230_pack.c` is a validator
   only now (861 lines, from 1,377): the whole `--cache-out` export — by then
   one register-driven `bake_params`, the successor of `bake_npc_params` /
   `bake_loc_params` — went, because `cachepack pack` emits the cache
   projection and the server band from one merged record (`CONTENT_PACK_PLAN.md`
   §5.4) and a second baker could only disagree with it. `--check-only` is
   accepted explicitly, so the documented invocation runs as written.
3. **~~The param-defaults bug~~ — fixed.** `load_param_types` reads `default=`
   and `push_typed_param` answers an absent row with the declared default
   (0 when none is declared, the cache decoder's own zeroed value). The
   selftest pins `default=-1` and `default=4` absent-row cases through both
   `oc_param` and `nc_param`. Still open, recorded in `osrs230_mockserver.md`
   §"What oc_param and nc_param still get wrong": `defaultstr=` is read by
   nothing, and the server-overlay `.param` declarations (`death_anim`,
   `next_loc_stage`, …) are not walked at runtime, so their types and
   symbolic defaults are invisible to the VM.
4. **The `gameval_import.py` truncation hazard** — it opens outputs `"w"`
   and would truncate `configs/all.npc.compack` 16,292 → 39 lines
   (`CONTENT_ARCHITECTURE.md` §3.1). Make it merge or retire it.

---

## 4. Decision three: porting a content slice

### 4.1 The workflow

The precedents to imitate are in [`LOSTCITY_PORT_TRIAGE.md`](LOSTCITY_PORT_TRIAGE.md)
§10.1 — `levelrequire/` (ported as *data*, because the scripts were data
wearing script syntax) and Cook's Assistant (ported as *scripts*, playable
end to end). Its "the format, and the rule for every future port" section is
binding. The loop:

1. **Measure first.** Triage §12 has the re-measure commands. Which opcodes
   does the slice call that the engine lacks (`mock230_scripts_report_gaps`
   prints this at load)? Which names don't resolve? Do not start a slice
   whose opcode gaps you haven't listed.
2. **Symbols before scripts.** Constants → categories → params/structs/
   enums/dbtables → varps → name maps (triage §9 step 3 order — each layer
   is named by the next).
3. **Configs before scripts that name them; interfaces before scripts that
   drive them.**
4. **Re-resolve every id by name through the gate — never copy ids.** Only
   `npc` wholesale differs (0% id agreement), but the near-matches are the
   trap: 94.7% agreement means 5.3% silent corruption (the Dragon Slayer
   gang varps are *swapped* between trees). Unresolved names fail at pack
   time, loudly, by design.
5. **Port the scripts**, adapting per §4.2. Compile via
   `make -C src mock230-scripts` (runs `tools/ss_allocate.py` then
   `sscompile`). Remember: a fresh checkout has **no script pack** (build
   output is gitignored) — the server prints a banner and then does almost
   nothing, because the engine's fallbacks are gated on a pack being loaded.
   That is the symptom now; it used to be a game that played fine and was not
   the one in the content tree.
6. **Verify in the real client, headlessly** (§7), and leave the check
   permanent (a `mock230_pack` rule, a test, or a selftest stanza).

### 4.2 Era translation — rev 254 content on a rev 239 cache / 230 wire

The traps, all documented in the triage:

- **Interfaces are the wall** (§7.3): 35 of 1,415 interface names resolve.
  LostCity `.if` files are the old flat-id era; this client is IF3 +
  CS2-driven UI. Interfaces are ported **per interface, on demand**, driven
  by the slice being ported — usually by *driving the existing rev-230
  interface* rather than importing the 2004 one. Decompile the rev-230
  clientscripts before guessing what an interface expects.
- **`~p_choice*`** (879 uses, 28% of the tree, §7.4): cannot port as
  written; the rev-230 chat menu is CS2 (`chatmenu` 219, cc_created rows) —
  the shape that landed is `runclientscript_ss` (opcode 11002) with the
  answer read as `last_slot`.
- **varp vs varbit reclassification** (§7.5, and triage §18 for what landed):
  a 2004 varp is often a 230 varbit bit-range, and the wrong class compiles,
  runs, transmits and corrupts. The *mechanism* is closed — `sscompile`
  refuses a whole-varp write to a varp that has varbits based on it (2,872 of
  the 5,705 do), content opts out per varp with `wholewrite=allow`, and the
  selftest asserts the engine made no such write either. The *data* is not: 27
  reference varps clobber, and **the danger is the ones that resolve** —
  `%prayer9` names the Clan Wars block here, `%prayer0` names the right
  container at the wrong granularity. `port/vars.map` states what each of the
  reference's 357 `%name` references becomes; 117 are still undecided and are
  listed as such.
- **Bare stat names** (§7.6): deliberately not guessed by the compiler —
  they collide and compile to the wrong id. Use the explicit enumerations.
- **npc categories** (§7.6b, and triage §16 for what landed): a *category* is
  the one config type the reference never authors — it crawls the `category=`
  key out of every record — so the port re-runs that crawl rather than looking
  anything up. §7.6b's "npc categories don't exist in the osrs239 cache" was
  wrong: the cache states one on **9,149 of its 16,292 npc records** and the
  decoder has always read it. It was unread. 18 names are crawled now
  (`tools/port_category_crawl.py`, `port/categories.map`); the rest are split,
  colliding, or need an id nobody had allocated. **The namespace can be allocated
  into as of 2026-08-02** (base 8192, `content/content_register.c`) and **`loc`
  joined it in the same pass** — config opcode 61, which the linked decoder had
  been discarding, is on 8,407 of the 62,194 loc records;
  `--domain loc` crawls them into `port/categories_loc.map`, 91 reference
  categories, 11 minted off this cache's own records and 2 `allocated` (the door
  pair). An allocated id must still be *stated* by an authored config block
  before a trigger bound to it can fire, and `mock230_pack` is what says so. This gated
  `drop tables/`, which landed 2026-08-01 (triage §10.1) — and the way it landed
  is the pattern for the next slice that hits this: **six of its sixteen
  category subjects are bound as categories and the other ten are expanded to
  the reference's own member list**, because a reference category is a list of
  npc names and those names resolve. Nothing had to be minted, and no `split`
  was resolved by inventing a name.
- **npc / loc / seq / spotanim names** (§4 group B, and triage §19 for what
  landed): the reference names **524** records this tree has no spelling for —
  not §4's 206, which counts only `.rs2` and only names the reference authors.
  Those failing loudly is bar 1 working; `port/names.map` is about the other
  direction, a name that resolves to the wrong record and says nothing. 180 rows
  carry a target, 113 of them port-manifest **lookups** (the exporter writes the
  source id into the name it mints, and all 113 cross-check on a structural
  field). The rest are a work queue with the reason stated per row, and §4's own
  worked examples are corrected there: `harlow` is `dr_harlow_vis` not
  `dr_harlow`, `king_lathas` is not the DS2 cutscene copy, and no osrs239 npc
  displays 'Leprechaun' at all.
- **What not to port** (§11): respect it. And the reverse direction (§7.8):
  osrs239 content can express things 254 couldn't — porting a script is
  allowed to *modernize* it (e.g. real dbtables instead of 254-era
  workarounds), but modernizing must not move logic into C.

### 4.3 Definition of done for a slice

From the Cook's Assistant precedent: boot the server, perform the content in
the real client (headless harness), state persists across logout/login,
`mock230_pack` at 0 errors, existing content untouched, and the gap report
shows no *new* silently-missing opcodes. "It compiles" is not done;
"the graphic plays but the character is frozen"-class bugs (anim priority,
missing `IF_SETEVENTS`) only show up in the client.

### 4.4 Kronos → OSRS-Content (post-254 skills / activities)

LostCity stops at Sept 2004. Farming, hunter, slayer, construction, and most
modern minigames/bosses live in Kronos as Java. Port them as content, not as
engine:

1. Confirm LostCity has no proc (§2.2). If it does, use
   [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) instead.
2. Read Kronos under
   `Kronos184-Fixed_2/Kronos-master/kronos-server/src/main/{java,kotlin}/io/ruin/`
   for *policy* (growth rules, task weights, brother order). Cross-check the
   osrs239 cache (dbtables, varbits, CS2) for the *wire* — when they disagree,
   the cache wins.
3. Skip the custom skip-list in [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md)
   (donor zones, Easy/Med/Hard slayer chooser, custom bosses, …).
4. Same §4.1 order: measure opcode gaps → symbols → configs → scripts → verify.
   If a new Server VM opcode is required, add it to that queue's opcode-gap
   log, implement it, then land the content — never a one-off C hook that
   content could have said once the opcode existed.
5. Agent loop state: [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md).

### 4.5 2009scape → OSRS-Content (authentic mid-era)

LostCity stops at Sept 2004. Farming, hunter, construction, slayer, and most
2005–2009 quests/minigames live in 2009scape as Java/Kotlin. Port them as
content, not as engine — and **prefer 2009scape over Kronos** for this era
(authenticity-first remake vs private-server inventiveness):

1. Confirm LostCity has no proc (§2.2). If it does, use
   [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) instead.
2. Read 2009scape under
   `2009scape/Server/src/main/content/{global/skill,minigame,region,global/activity}/`
   for *policy* (growth rules, trap lifecycle, brother order, quest steps).
   Cross-check the osrs239 cache (dbtables, varbits, CS2, enums) for the
   *wire* — when they disagree, the cache wins. Classic farming varbits often
   still share numeric ids with rev 530, but **measure** before binding;
   never paste `FarmingPatch.kt` ints into authored content unchecked.
3. Skip the custom / non-OSRS skip-list in
   [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md)
   (bots, holiday events, Summoning, Fist of Guthix, While Guthix Sleeps, …).
4. Same §4.1 order: measure opcode gaps → symbols → configs → scripts → verify.
   If a new Server VM opcode is required, add it to that queue's opcode-gap
   log, implement it, then land the content — never a one-off C hook that
   content could have said once the opcode existed.
5. Agent loop state: [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md).
   Mid-era slices that also appear on the Kronos queue are owned here; Kronos
   keeps post-2009-only content.

---

## 5. Decision four: modern features with no LostCity reference

This is the clan-chat / stat-orbs / XP-drops class: features of the modern
client that a 2004 engine never drove.

### 5.1 The principle

**The modern client already implements the feature.** Its interfaces are in
the cache; its behavior is CS2 clientscripts; what is missing is (a) the
server packets/services that feed it and (b) sometimes host ops the CS2 VM
calls into. So the procedure is *discover the client's surface first*:

1. Find the interface + clientscripts in the cache (decompile; do not
   guess ids — `worldmap-open-click-session` and the bank both proved the
   ids live in the scripts).
2. Find the CS2 ops the scripts call (`3rd/rscache/src/cs2/cs2_command.gen.h`
   is the table; `src/cs2vm2/` is the VM) and which lack host
   implementations.
3. Find the wire surface in `src/net/rev/osrs230/packetin.h` — many packets
   frame correctly but map to `PKT_NAME_NONE` (they drop cleanly and are
   waiting for a decoder).
4. Then split the work by the same rule as everything else: the engine gets
   the *mechanism* (packets, services, host ops); the *policy* goes in
   content wherever content can express it.

### 5.2 Status of the named three

- **Stat orbs — done.** Orb fill is client-side CS2 (a clipping layer over
  the full orb); server obligations are `UPDATE_STAT` (boosted level is the
  consumed field; send base twice to pin the HP orb),
  `UPDATE_RUNENERGY` (77), `UPDATE_RUNWEIGHT` (27, **kilograms**). See
  `mock230_player_systems.md` §2.3, `REV230_UI_BLANK_PANELS.md` §2.
- ~~**XP drops**~~ — **done**, and the diagnosis this entry used to carry was
  wrong on three counts. It said the blocker was "the non-terminating varc
  queue-shift loop in script 1004", that `IF_SETONSTATTRANSMIT` registered
  "behind `TORIRS_XP_DROPS=1`", and "fix the VM loop, not the server".
  Measured: the loop **terminates by construction** — varcs 953..959 are a
  7-deep queue that shifts one slot per pass, so it exits within 7, and the
  induction variable is `%varcint953` itself rather than the counter the
  wording implies (1,026 invocations, 1,026 returns). The VM runs all 35 of
  its opcodes correctly. And `TORIRS_XP_DROPS` **never existed** — `grep` is
  empty. The real bug was one omission in the transmit pump's early-return
  guard: it tested five dirty flags and not `stat_transmit_dirty`, while the
  branch serving stat transmits sat below the return and the clear-down below
  that. Flag set, guard returns, clear wipes it. Fixed; drops render, verified
  on pixels.
- **Clan chat — greenfield, on both sides.** The CS2 table has the full op
  surface (`3611..3627 clan_*`, `74/76 push_varclan*`, transmit-listener
  ops); none have host implementations. No clan packets are decoded — and
  the adjacent **friends/ignore/private-message** packets (15, 56, 21, 29)
  frame but map to `PKT_NAME_NONE`. LostCity *does* have a FriendServer
  (`engine/src/server/friend/FriendServer.ts`, 688 lines, a separate-thread
  service) — port that pattern for the social layer, then build clan chat
  as: engine service + packets + varclan state + CS2 host ops; membership
  policy/messages in content where expressible. Do friends/PM first.

  **Two corrections from the §5.4 survey, which traced both features:**
  clan chat does *not* simply "reuse friends/PM's plumbing" — it is the
  strictly larger gap. Friends/PM at least has declared (if unrouted)
  `PKT_NAME_*` constants and a complete, unreached client-side decode path
  to reconnect; clan chat has **no packet-name constants at all** and no
  dead decode path, so it is built from nothing rather than rewired. And
  the op surface is three families, not one: `clan_*` (the currently-open
  channel) plus `activeclanchannel_*` (3850-3861, who is connected right
  now) plus `activeclansettings_*` (3800-3822, the persistent roster).
  Also: **LostCity has no Friends Chat precedent** — that feature launched
  August 2008, after its rev-254 target, so only the plain friend-list
  service shape is portable. See `clan_chat_server_reqs.md`,
  `friends_pm_chat_server_reqs.md`.

### 5.3 Feature checklist

Before writing anything: (1) which interface/CS2 scripts implement the
client half, verified by decompilation; (2) which host CS2 ops it needs
(`cs2_command.gen.h` row + stack shape); (3) which packets, and whether
rev-230 already frames them; (4) what state the server must track and where
it persists (`.varp`-declared with `scope=perm`, or a service); (5) what is
expressible as content. Write the doc section (in the feature's topic doc)
*with* the implementation — every landed feature above has one, and the docs
are why this guide could be written.

### 5.4 The interface survey — §5.3 already run on 19 interfaces

The §5.3 pass has been run, as a discovery-only exercise, across the
interface surface. Each doc below states: the CS2 call graph with
file:line, the varps/varbits/containers/host-ops the panel reads, what
mock230 already does, and the LostCity precedent (or its confirmed
absence). **They are server-requirements specs, not implementations** — per
§2, re-grep the reference before writing code against any of them.

| doc | interfaces | verdict |
|---|---|---|
| [`questlist_chatmenu_levelup.md`](questlist_chatmenu_levelup.md) | questlist 399, chatmenu 219, levelup_display 233 | chatmenu **already landed**; questlist needs 7 undeclared varps; levelup popup never opens |
| [`shop_server_reqs.md`](shop_server_reqs.md) | shopmain 300, shopside 301 | greenfield; stock is a **live container**, not a dbtable |
| [`friends_pm_chat_server_reqs.md`](friends_pm_chat_server_reqs.md) | friends 429, pm_chat 163 | greenfield on the CS2 path; dead lc254-era client scaffolding exists to reconnect |
| [`clan_chat_server_reqs.md`](clan_chat_server_reqs.md) | clans_sidepanel 701, clans_members 693 | greenfield, **larger than friends/PM** — see §5.2's correction |
| [`emote_tab_server_reqs.md`](emote_tab_server_reqs.md) | emote 216 | **already landed + selftested**; only unlock-bit content is open |
| [`skill_guide_server_reqs.md`](skill_guide_server_reqs.md) | skill_guide_v2 860 | mostly static dbtable; needs a query-state change for `db_find_filter_with_count` |
| [`collection_log_server_reqs.md`](collection_log_server_reqs.md) | collection 621, collection_overview 908 | **landed** (container registry + `interface_collection/`); per-table earn hooks / kill counts still open |
| [`account_summary_server_reqs.md`](account_summary_server_reqs.md) | account_summary_sidepanel 712 | **landed** — all 8 click-layer ops + counter plumbing; selftested |
| [`grand_exchange_server_reqs.md`](grand_exchange_server_reqs.md) | ge_offers 465 + family | **largest feature**: 3 data idioms + a world-wide matching engine |
| [`trading_server_reqs.md`](trading_server_reqs.md) | trademain 335, tradeside 336, tradeconfirm 334 | **most novel architecturally**: needs cross-player container read/write |
| [`chrome_panels_server_reqs.md`](chrome_panels_server_reqs.md) | xptracker 729, hiscores 894, loottools 650 | xptracker needs ~nothing; hiscores is **out-of-band HTTP**, not a game packet |
| [`death_mechanics_server_reqs.md`](death_mechanics_server_reqs.md) | deathkeep 4, gravestone_generic 672, death_coffer 670 | preview + on-death + gravestone/coffer landed 2026-08-03 (high-alch fees) |
| [`slayer_rewards_server_reqs.md`](slayer_rewards_server_reqs.md) | slayer_rewards 426, task_list 924 | greenfield; postdates LostCity |
| [`farming_server_reqs.md`](farming_server_reqs.md) | farming_tools 125/126, farming_view 179 | greenfield; 107 tick-driven per-player patch records |
| [`settings_panel_server_reqs.md`](settings_panel_server_reqs.md) | settings 134 | two real gameplay settings; the rest is chrome |
| [`bank_pin_server_reqs.md`](bank_pin_server_reqs.md) | bankpin_keypad 213, bankpin_settings 14 | greenfield; PIN compare is server-side, reuses `P_COUNTDIALOG` |
| [`world_switcher_server_reqs.md`](world_switcher_server_reqs.md) | worldswitcher 69 | **not a game-world obligation** — login-server tier |

Four findings from that pass generalise beyond any one interface, and are
worth knowing before starting the next one:

1. **The decompiled corpus is missing procs, routinely.** `questlist_draw`,
   `chatbox_multi_addoption`, `friend_update`, shopside's populator,
   loottools' `script7166`/`7133`, farming's per-patch walker, settings'
   toggle getter/setter — all *called* by scripts that are present, with no
   definition anywhere in the 9,368 files. "Not in the corpus" does **not**
   mean "doesn't exist" — re-decompile from the live cache before
   concluding anything about a missing body.
2. **Reused and collided varp names are the norm, not a surprise.**
   `bank_closing` backing shop's quantity mode, GE's tax-slot flags packed
   into *music-player* varps, slayer's ownership bit 19 colliding with an
   unrelated named varbit, `diango_hols_sack` as deathkeep's category tags,
   generic `if1..if6` scratch varps serving both slayer tasks and Death's
   Coffer. Always read-modify-write; never assume a varp's name describes
   its current use.
3. **An interface's title string is not evidence of its mechanism.**
   `farming_tools` says "Amazing Farming Equipment Store" and is not a shop
   — the title comes from a generic `steelborder` helper every interface
   shares.
4. **Some features are not the game server's job at all.** Hiscores and
   world switching both terminate outside the game-world protocol (HTTP and
   the login tier respectively). Confirm where a feature's mechanism
   *actually* lives before scoping server work for it.

---

## 6. The phase plan

Phases 2–4 interleave (slices *pull* opcode and interface work — see triage
§9's note that 4a precedes bulk content); phase boundaries are gates, not
walls. Authorities: `osrs230_mockserver.md` §6.1 (engine),
`LOSTCITY_PORT_TRIAGE.md` §9 (port), `CONTENT_PACK_PLAN.md` §9 (pipeline).

**Phase 0 — make the pipeline honest** (§3.6 above)
Read the server band at boot · delete the second baker · fix param defaults
· defuse `gameval_import.py`. *Gate: one baker, one load path, and a boot
that fails loudly on a stale band.*

**Phase 1 — engine substrate** (`osrs230_mockserver.md` §6.1 items 1, 3, 6)
1. ~~Multiplayer~~ — **done.** Two embedded clients in one world see each
   other move, asserted in `embed_test.c` against the *client's own*
   PLAYER_INFO reader. The change was not the raised `MOCK230_PLAYER_MAX`: it
   was moving every "the client has been told about this" set off the world
   and onto the player (npc tracking, ground objs, the rebuild and login
   latches), plus a new per-player *player* tracking set; making
   `srv->player` into `srv->active_player`, a documented "whose turn it is"
   seam the phases and the packet dispatcher write; splitting the world build
   from the login so a second player does not respawn the roster; and
   broadcasting loc changes so a door one player opens is open for the other.
   Still single-player on purpose: the socket server accepts one connection at
   a time, and the *scene origin* is one per world — see
   `osrs230_mockserver.md` §6.1 step 1 for the full list, which item 2 below
   is what closes.
2. ~~`ZoneMap` keyed `(zx,zz,level)` with buffered/replayable events~~ —
   **done**, `src/net/mock/mock230_zone.{c,h}`; the whole of it is
   `osrs230_mockserver.md` §3.17. Per-zone loc/obj/npc lists plus a per-tick
   event buffer, and the loc and obj packets moved onto it: a door one client
   opens is open for a client that connects *afterwards*, asserted in
   `embed_test.c` against the client's own decoder with a third peer.
   What it actually was, since "add a hash map" is the tempting summary and is
   wrong: **the ZoneMap owns loc mutations, the scene does not** (the scene is
   re-read from the cache on every rebuild, so the server was forgetting its own
   doors — two comments in two files described a mechanism that could not work);
   **a newly-loaded zone gets state and not the tick's events**, because the
   state already includes them and sending both put every ground obj on the
   floor twice; and **the npc cap and the wire's tracked count are two
   numbers**, 2048 and 255, now that NPC_INFO asks the zones who is nearby
   instead of scanning the world once per client.
   Re-measured rather than inherited: the zone-trigger count is 806
   (`zone` 262, `zoneexit` 165, `mapzone` 306, `mapzoneexit` 73) and the
   triage is right about it — but only **427** are zone-keyed. `mapzone` and
   `mapzoneexit` key off the map square (`>> 6`), not the zone (`>> 3`), and
   never touch this structure. Dispatching any of them is Phase 2.
3. ~~**Invert the script fallback**~~ — **done**, `osrs230_mockserver.md`
   §3.18. Lookup is `ScriptProvider.getByTrigger` — exact type, then category,
   then the bare `_`, and nothing after that — and the `_` wildcard is the only
   fallback the design has. A trigger with no script does nothing, and says so
   under `MOCK230_VERBOSE` for the triggers a *player* initiated, which is where
   the reference puts the message too.

   The C that still answers an unbound trigger is not deleted and not moved: it
   is **enumerated**, in `enum Mock230Fallback`, each row naming its
   blocker, counted at boot and pinned by the selftest — it may shrink, it must
   not grow. Seven when this landed; **four** now — `ai_queue3`, `opobj` and
   `opheld` have all moved (Phase 3, 2026-08-01 and 2026-08-02). That is the same rule as §2.4's named
   hooks, and it is what makes "widen the opcode surface, then move it" (§2.5,
   phase 3) auditable rather than aspirational. The 2026-08-01 audit found the
   limit of the count on its own: it pins the *number* of rows and nothing pins
   the *reasons*, and four of the seven reasons had expired without the count
   moving. Phase 3 has what came of that.

   Three things that were indistinguishable now are not, and the third is why
   this gated the bulk import: a script that **aborted** versus a trigger
   nothing was bound to (they both returned 0, so a broken quest script produced
   a plausible wrong conversation); an engine fallback versus "what happens
   otherwise"; and a server with **no script pack**, which turned every fallback
   on at once — so the default state of a fresh checkout was a second, complete,
   silently different implementation of the game. It prints a banner and does
   nothing now, verified in the client side by side with a normal login.

   Two bugs fell out of reading the reference beside the code: `[if_close]` is a
   notification, not a handler (the unmount was conditional on no script being
   bound), and its dispatch asked with a packed component uid against a compiler
   that keys it on the interface id — so no `[if_close]` in this tree had ever
   run. A third was a selftest that had stopped testing anything.
*Gate: two clients see each other fight over a door.* **Met.** They see each
other, a door one opens is open for the other and for whoever logs in next, and
an npc now faces the player it is fighting on every stream rather than only on
the one the retaliation was encoded to.

This entry used to say the fight was outstanding and that closing it "needs the
npc masks to be per-observer". Measured, that overstated it by a lot: of the
~20 observer-relevant fields on `struct Mock230Npc`, exactly **one** is
observer-dependent, and only in one bit. The reference does not have
per-observer masks either — LostCity keeps one shared set and makes the *id*
absolute (`target.slot + 32768`), telling each client its real slot. This tree
diverged at one line, sending `UPDATE_PID(2047)` to everybody, which made 2047
a self-alias meaning "me" on every client at once. Fixed by sending the real
pid; memory cost measured at zero. What remains of §6.1 step 1 is unrelated to
masks: the socket server still accepts one connection at a time and the scene
origin is one per world.

**Phase 2 — symbols and surface** (triage §9 steps 2–5)
Name-resolution gate → constants (1,562) → ~~npc categories (§7.6b)~~ **done**
(19 npc names in `pack/category.pack`, `mock230_npc_category()`, and the
`[ai_queue3]` dispatch passes the rung) →
param/struct/enum/dbtable ids → varp/varbit reclass → name maps → opcodes by
leverage (param decoder → `loc_*` → `npc_*` → `runclientscript_ss` strings →
small-wide) → ~~undispatched triggers~~ **done** (queue/timer
`osrs230_mockserver.md` §3.19, `*u` use-on §3.20, the zone family §3.21 — all
four of the last, both granularities. What they needed was *name*-keyed dispatch,
`[zone,<level>_<mx>_<mz>_<lx>_<lz>]`, which the numeric-subject
`mock230_scripts_run_trigger` cannot express, plus two coordinate latches on the
player; the ZoneMap was not the blocker and is not in the path). Still
undispatched after step 5: `walktrigger`/`ai_walktrigger`,
`advancestat`/`changestat`, `inv_buttond`, `logout`, `ai_despawn`, `tutorial`,
and the `*t` spell-target family (89 uses). Track via the generated
`mock230_opcode_coverage.gen.h` (**260/401** on 2026-08-02, 246/399 two days
before; this line said 224 and was 22 low, which is the reason the sentence
after it exists) and the
load-time gap report — **never via numbers typed in prose.**

**Phase 3 — evict the C content**
The trivial list (§2.5) immediately — each is an hour. The blocked rows as
Phase 2 unblocks each. *Gate: `grep` finds no game-facing string literals and
no id constants in `src/net/mock/` outside the wire tables.*

The mechanism is `enum Mock230Fallback` and nothing else: the C that answers a
trigger content does not bind is **enumerated**, one row per behaviour, each
naming its own blocker, counted at boot and pinned by the selftest. Evicting
the C *is* deleting a row, and the order is not negotiable — **widen the
surface until a script can say it, land the content, verify the behaviour
through it with mutations that prove the assertions can fail, then delete the C
and the row, then re-verify.** Never the reverse. The list may shrink; it must
not grow.

**Status 2026-08-02: 7 → 6 → 5 → 4.** `ai_queue3` evicted to `[ai_queue3,_]` in
`skill_combat/npc_combat.rs2`, then `opobj` to `[opobj3,_]` in
`player/scripts/pickup.rs2`, then `opheld` to `[opheld2,_]` / `[opheld5,_]` in
`player/scripts/{equip,drop}.rs2` (`osrs230_mockserver.md` §3.18). Four remain:
`opnpc`, `oploc`, `inv_button`, `if_button`.

`opheld` is the one to read before starting another: its blocker was cited as
seven opcodes and was none of them. Five landed, two were misfiled, and what
actually stood in the way was that a *rule* — refuse the wear and say two
sentences — had no data it could read. Check what a row is really waiting on
before widening anything.

`opheld`'s first three steps landed the same day and **the row was deliberately
not deleted** — five opcodes in, `player/scripts/equip.rs2` and
`player/scripts/drop.rs2` written, compiled and exercised through `[debugproc]`s
with seven mutations run, and the two `[opheld<n>,_]` bindings left commented
out because the level requirement still has no script-readable form. That is
what "the list may shrink; it must not grow" looks like when a stage stops
honestly: the blocker is rewritten to the one thing that is true, the count does
not move, and nothing in the game changed.

**`opobj` is the eviction to read before planning another, because it measured
the order rather than assuming it.** The two selftest legs that assert the take
were moved onto the op content binds *while the C was still there*, and
unbinding the script left them **green** — the fallback answered, so they were
measuring the C. After the deletion the same mutation turns 11 checks red. A
test written against a behaviour that two implementations both produce proves
nothing until one of them is gone, which is exactly why "verify, then delete"
cannot be reordered into "delete, then verify" and cannot be shortened to
"verify" either.

**Status 2026-08-02: 6 → 5.** `oploc` evicted; `interaction_engine_loc` (84
lines) and `climb` (34) deleted. Five remain: `opnpc`, `opobj`, `opheld`,
`inv_button`, `if_button`.

It took three stages and the two intermediate end states are the part worth
copying, because neither of them was "the row goes". Stage 1 discharged every
`blocked_ops` entry — the `SSVM_ENT_LOC` binding, the loc category rung,
`LOC_CATEGORY`/`LC_CATEGORY`/`LC_DEBUGNAME`/`P_OPLOC` — and **kept the row**,
because a blocker being cleared does not move any behaviour. Stage 2 landed the
doors and the ladders and **kept the row again**, because a third behaviour was
uncovered. Stage 3 found what that was, and it was not engine-shaped at all: 78
loc records in this cache put "Bank" on a menu op and content bound one of them,
so the `strcmp` in C was reaching 77 booths for free and nothing in the suite
would have gone red on losing them. The eviction is a generated list
(`tools/bank_import.py`), not an opcode. **A row can be one unglamorous list
away from going, and that list is invisible from the row's own text.**

The one measured behaviour change: the C's door branch never read an op number,
so it also answered `Pick-lock`, `Repair`, `Force`, `Remove`, `Attack`, `Search`
and `Quick-open` by opening the door — 54 (record, op) pairs on 26 records, all
of which now get `Player.defaultOp`'s message, which is what the reference gives
them since it binds none of those verbs either.

**The result worth reading before you plan any of the five is not the deletion.**
The audit that preceded it re-measured all seven blockers and found **four
naming something false or misdirected** — `ai_queue3` (categories, the rung and
69 drop-table files had all landed, so nothing blocked it), `oploc` ("needs
`loc_*`", which landed), `opheld` ("equipment is C", which is content already),
and `opobj`, which was never right: "an opcode pair" when the missing thing is
an entity kind with no writer in the tree — and the
other three carrying stale line counts that also blamed the wrong code. Nothing
had been edited to make them wrong; the things they waited for simply arrived
and nobody came back. `opobj`'s corrected blocker was then acted on, discharged
and the row deleted the next day (§2.5) — and the day in between, with the
blocker cleared and the row still standing, is the distinction this list exists
to keep: a discharged blocker and a deleted row are two different claims.
**Re-checking a row's blocker is the first step of acting
on it, not preparation for it**, and a corrected blocker on a row that stays is
worth more than a deletion, because a wrong row is visible the moment someone
tries to act on it and an expired reason is not visible at all. §2.5 has the
corrected set and the measured sizes; the live text is at boot under
`MOCK230_VERBOSE`, and it is the authority over both docs.

Two things now enforce that. The opcode-shaped half of every blocker is
**machine-checked** — each row lists the opcodes it cites and
`mock230_scripts_stale_blockers` turns the selftest red the day one is
implemented, so the fix becomes "rewrite or delete the row", not "notice". And
every string is written to be checkable in one command. Neither covers a blocker
that is not an opcode, deliberately (§2.5).

**Order for the remaining six, by cost — measured, and it is not the order the
old blockers implied.** The cheapest unlock is structural: (1) bind
`SSVM_ENT_LOC` on the `[oploc<n>]`/`[aploc<n>]` dispatch — zero new opcodes,
and it unblocks the `doors/` and `ladders_stairs/` ports; (2) ~~wire
`SSVM_ENT_OBJ` end to end, then the `SS_OP_OBJ_*`, then
`player/scripts/pickup.rs2`~~ — **done 2026-08-02**, five opcodes not six
(`OBJ_FIND` was never needed) and the C is still there awaiting deletion; (3) `opheld`'s eight opcodes, then `equip.rs2` +
**Order for the remaining five, by cost — measured, and it is not the order the
old blockers implied.** ~~(1) bind `SSVM_ENT_LOC` on the
`[oploc<n>]`/`[aploc<n>]` dispatch~~ — **done 2026-08-02**, with the category
rung and `P_OPLOC` beside it; the row itself went the same day. (2) wire
`SSVM_ENT_OBJ` end to end, then the six `SS_OP_OBJ_*`, then
`player/scripts/pickup.rs2`; (3) `opheld`'s eight opcodes, then `equip.rs2` +
`drop.rs2`; (4) a single `last_verb` reader, which unblocks **both** bank rows'
addressing; ~~(5) `oploc`'s remaining half — a real loc category~~ — **done in the same
pass**: `dat2_config_loc.c` keeps opcode 61 and `cachepack` round-trips it. It
was the expensive item and it was not the one that landed the doors; see §2.5.
(6) `opnpc`, which needs the `player_combat` closure ported and nothing less.

**One shortcut is available and is a trap.** `[opnpc2,_] p_opnpc(2)` would
delete the `opnpc` row today with no new opcodes — and move nothing, because
`SS_OP_P_OPNPC` calls `mock230_combat_engage` directly. All 1,061 lines would
stay, now unreachable from the list that exists to track them. The row's own
`blocked_on` says so. Deleting a row without moving the behaviour is the one
way to make this list lie.

**Phase 4 — content, in slices** (triage §10)
Done: `levelrequire/`, Cook's Assistant, ~~`drop_tables/`~~ (**69 files, 136
`[ai_queue3]` bindings, 6 of them on the category rung** — the categories that
step 3b gated landed first, and the `[ai_queue3,_]` wildcard on top of it is
what closed the Phase 3 row of the same name).

Next, in order of unlock: `doors/` and `ladders_stairs/` (`doors/configs/` is
already in the tree — `doors.loc`, `doors.param`, `doubledoors.constant` — but
it is data the *engine* reads, not a binding; `ladders_stairs/` is a README.
Both wait on Phase 3's `SSVM_ENT_LOC`) → skills by directory (each unlocks with
the `loc_*`/`npc_*` families) → areas/quests on demand. Each slice per §4's
workflow with a permanent check.

**Shops moved off the front of that queue, twice over, and the corrections
arrived from two independent directions.** The §5.4 survey killed the
parenthetical this line used to carry: there is no "shop dbtable pattern" to
want — shop stock is a live per-shop container, and the `omnishop_*` dbtable
belongs to unrelated reward-point stores. Then the survey's own readiness
triage found `shop_server_reqs.md` describes something **blocked three ways**,
not ready: `shopmain.if` carries no `onload=` at all, so the panel cannot draw
itself and needs a server-driven `shop_main_init(inv,int,int,int,string)`;
LostCity's shop invs are `scope=shared` while `container_for` resolves only off
`active_player`; and `restock=`/`stockN=`/`scope=` have nowhere to live,
because there is no `fields/inv.ini` and no `[namespace:inv]` in `content.ini`.
Two of those three are the cross-cutting blockers §5.4 names — the fixed
1-int/2-string shape of `runclientscript_ss`, and `container_for` needing to be
a registry rather than a fourth `if`. **Both are discharged as of 2026-08-02**
(`SS_OP_RUNCLIENTSCRIPTVARARG` 11003, and
[`mock230_containers.md`](mock230_containers.md)) — and discharging the second
sharpened the third rather than clearing it: the registry resolves as
`(srv, player, inv_id)` and branches on `owner_kind`, so a world-scoped
container is expressible, but `mock230_container_scope()` classifies everything
as per-player because the client cache has no `scope` field to read. `shop` is
now blocked on exactly one thing, `fields/inv.ini` + `[namespace:inv]`.

**Phase 5 — modern features** (§5)
XP-drops VM loop fix → friends/ignore/PM decode + service → clan chat →
whatever the client's cache surfaces next (the discovery procedure is §5.1).
**§5.4 has already run the §5.3 discovery pass on 19 interfaces** — start
from the relevant doc there rather than re-deriving it, and read §5.4's
four generalisations first. Rough order by cost, from that survey:
declare-a-varp work (questlist, settings, emote unlocks) → self-contained
features (bank PIN, skill guide, slayer rewards) → new per-player state
(collection log's container, farming's patch records) → new cross-cutting
mechanisms (trading's cross-player containers, clan chat's three op
families, GE's matching engine).

**Phase 6 — rename** (`osrs230_mockserver.md` §6.1 item 7): `mock230` is a
double misnomer; cheapest while consumers are few.

---

## 7. Guardrails and verification

- **Build:** `make -C src` (plain make, not CMake). Script pack:
  `make -C src mock230-scripts`. Agents sharing the repo must set a private
  objdir (`PLATFORM_OBJ_BASE`) — stale-`.o` races are real.
- **Tests:** `make -C src test-db`, `test-mock230-coverage` (fails if the
  generated coverage header is stale), `test-ss-provider` (the trigger lookup
  order — every way of getting it wrong still finds *a* script),
  `mock230_servercodec_test`; cache
  fidelity `make -C 3rd/rscache test` (byte-exact round-trip is the bar;
  read `3rd/rscache/EXCEPTIONS.md` **before** touching rscache write paths);
  ServerScript conformance corpus = LostCity's 9,333 compiled scripts
  (`ss_corpus_test` — the corpus is test data, **not runnable content**).
- **Content validation:** `mock230_pack --check-only` at 0 errors, always.
- **In-client verification:** `SDL_VIDEODRIVER=dummy` + `TORIRS_SIM_CLICK_AT`
  / `TORIRS_SIM_MOUSE_CLICK` / `TORIRS_EXIT_BMP` + `MOCK230_VERBOSE=1`.
  `TORIRS_SIM_*` hooks run pre-loop. Memory debugging: `MallocScribble`,
  **not** ASAN (hangs on this machine).
- **Determinism:** the VM seed is fixed (`0x5eed1234`); keep new randomness
  behind it.
- **Never `git stash`** in this repo (restores the user's old stash on pop).
  `pkill -f build/mock230` also kills `mock230_dev` — match tighter.
- **Distrust prose counts.** Coverage, id agreement, gap lists — all have
  generated sources (`mock230_opcode_coverage.gen.h`, triage §12's
  commands). Re-measure; docs self-describe as having gone stale before.
- **When a mounted panel draws nothing, it is a client bug until proven
  otherwise** (`REV230_UI_BLANK_PANELS.md` §1): `TORIRS_DUMP_TREE_EXIT=1` →
  `TORIRS_DUMP_BOUNDS` → `TORIRS_DUMP_SETSIZE` → only then suspect a packet.
- **Docs are part of done.** Every landed system above has a topic doc that
  self-corrects; keep that discipline. Update the triage's §10.1 running log
  when a slice lands.

---

## 8. Reading order

For any substantial task, in this order:

1. This file, the section matching your decision.
2. [`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md) §8 — the ownership
   rule and checklist (always).
3. Task-specific:
   - server behavior → [`osrs230_mockserver.md`](osrs230_mockserver.md)
     header + §3.13b–d + §6.1
   - pack/cache work → [`CONTENT_PACK_PLAN.md`](CONTENT_PACK_PLAN.md) §0 +
     §5.4, then `src/net/mock/mock230_servercodec.h` and
     `src/content/content_fields.h` (prose-quality headers)
   - content porting (LostCity / pre-254) → [`LOSTCITY_PORT_TRIAGE.md`](LOSTCITY_PORT_TRIAGE.md)
     §1, §9, §10.1, §12,
     `LostCity_Server/content/scripts/README.md`, and
     [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md)
   - content porting (2009scape / mid-era) → this file §4.5, then
     [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md);
     behaviour under `2009scape/Server/src/main/content/`; still grep
     LostCity first (§2.2); prefer over Kronos for pre-2013 content
   - content porting (Kronos / post-2009) → this file §4.4, then
     [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md); behaviour
     under `Kronos184-Fixed_2/.../io/ruin/`; still grep LostCity first (§2.2)
   - ServerScript → [`serverscript.md`](serverscript.md)
   - instanced maps / dynamic regions (POH, Pest Control island, private
     mazes, cutscene sets) → [`map_instances.md`](map_instances.md). Its §5 is
     the trap: the copy is written twice (server collision, client scenery) and
     the two halves walk the rotation in opposite directions
   - pathfinding / LoS / NPC movement →
     [`COLLISION_MAP.md`](COLLISION_MAP.md) (wall flags / directionality),
     [`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md), then
     [`PATHING_INTERACTION_PARITY.md`](PATHING_INTERACTION_PARITY.md) §7
   - UI-facing features → [`REV230_UI_BLANK_PANELS.md`](REV230_UI_BLANK_PANELS.md),
     [`UI_ERA_PORTING_GUIDE.md`](UI_ERA_PORTING_GUIDE.md)
   - anything that reads or writes the client canvas size, the gameframe
     layout, or the window mode →
     [`gameframe_layout_resize.md`](gameframe_layout_resize.md). Its §2 is the
     three-copies trap (the canvas existed as three independently-written
     variables and had already silently drifted); its §8 is the one part still
     open, the fixed/resizable **toplevel** switch (161 ↔ 548), which needs a
     packet, a per-mode mount table in content, and a real root swap.
   - **implementing a specific interface → §5.4's table**, then that
     interface's own `*_server_reqs.md`. Read §5.4's four generalisations
     before the doc itself; they apply to every interface, not just the one
     you're on.
4. The reference source itself. The answer is in the source, not in
   intuition. A `*_server_reqs.md` doc is a *starting point* for that
   grep, never a replacement for it — the survey is discovery-only and
   states its own corpus gaps rather than filling them.
