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

Three repositories:

| repo | what it is |
|---|---|
| this repo (`3draster`) | the client, the server (`src/net/mock/`, "mock230" — a misnomer, it is *the* server), ServerScript (`src/serverscript/`), cachepack (`3rd/rscache/tools/cachepack/`) |
| `OSRS-Content/osrs239-content` (submodule) | **the content tree** — the destination for all ported content |
| `/Users/matthewevers/Documents/git_repos/LostCity_Server` | **the reference.** `engine/` = Engine-TS (branch `254_zuk`), `content/` = the content tree (branch `254_inferno`). Rev **254** (Sept 2004), not 225 — the `_unpack/225` dir is decompiled reference data, not the tree itself |

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
  cache.osrs239        → obj/npc/seq/varbit configs, collision (client's own
                         collision_map.c, LINKED not reimplemented), inv sizes
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
  `_` wildcard script, not a C fallback.

### 2.3 Where LostCity actually puts things

Measured from the reference (details in the triage and in
`LostCity_Server/content/scripts/README.md`):

| subsystem | owner | evidence |
|---|---|---|
| tick loop, 11 phases | ENGINE | `engine/src/engine/World.ts` `cycle()` |
| pathfinding/collision | ENGINE | `rsmod-pathfinder` npm; here: linked client `collision_map.c` |
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
   instead. (The 9 named hooks in `mock230_scripts.c:152-164` are the
   sanctioned exceptions; do not grow that list casually.)

### 2.5 The current violation worklist

Known content-in-C, verified 2026-08-01 (line numbers drift; grep the
symbol). **Trivial moves** — each is a `[login]`-block or config edit plus
deleting C:

- starting inventory `kit[]` — `mock230_world.c:3549` → `[login]` `inv_add`s
- starting bank stock `stock[]` — `mock230_world.c:3581` (writes
  `player->bank.slots[]` directly, bypassing the bank API — doubly wrong)
- starting stats / hitpoints-10 — `mock230_world.c:3511`
- fallback NPC greeting `"Hello there, adventurer!"` — `mock230_world.c:2064`
- four raw `"Nothing interesting happens."` literals — `mock230_world.c:1979,
  2275, 2288, 2333` (every other message goes through `mock230_say`)
- default appearance kit `k_default_kits[12]` — `mock230_encode.c:732`
- most of the `::` cheat ladder — `mock230_world.c:2388-2683` (`::pray`
  already migrated to a `[debugproc]`; the rest follow the same pattern)

**Blocked moves** (~3,200 lines: bank 1,370, combat 858, equipment, world
map, doors, login burst) — blocked on the ServerScript opcode surface, not
on willingness. The order is deliberate: *widen the opcode surface until a
script can say it, then move it* (`osrs230_mockserver.md` §6.1 steps 4–5).
Do not move these early by inventing non-reference C↔script hooks.

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
   output is gitignored) — if every trigger falls back to C, you forgot to
   build it.
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
- **varp vs varbit reclassification** (§7.5): 28 by hand; a 2004 varp is
  often a 230 varbit bit-range. Wrong class compiles and runs and corrupts.
- **Bare stat names** (§7.6): deliberately not guessed by the compiler —
  they collide and compile to the wrong id. Use the explicit enumerations.
- **npc categories don't exist in the osrs239 cache** (§7.6b): 19% of
  compile failures. The category field + crawler is triage §9 step 3b and
  gates `drop tables/`.
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
- **XP drops — blocked on one client bug, not the server.** The panel
  (interface 122, clientCode 1354) is entirely client-driven off stat
  transmits; `IF_SETONSTATTRANSMIT` now registers (behind
  `TORIRS_XP_DROPS=1`, `src/game/rs_cs2_dispatch.c`), and the remaining work
  is the non-terminating varc queue-shift loop in script 1004
  (`xpdrops_stattransmit`, varcs 953..966) — plus re-arming the listener
  (`src/game/task_cs2_run.c` notes the spot). The server already calls
  `RS_CS2Host_NotifyStatChanged` on `UPDATE_STAT`. Fix the VM loop, not the
  server.
- **Clan chat — greenfield, on both sides.** The CS2 table has the full op
  surface (`3611..3627 clan_*`, `74/76 push_varclan*`, transmit-listener
  ops); none have host implementations. No clan packets are decoded — and
  the adjacent **friends/ignore/private-message** packets (15, 56, 21, 29)
  frame but map to `PKT_NAME_NONE`. LostCity *does* have a FriendServer
  (`engine/src/server/friend/FriendServer.ts`, 688 lines, a separate-thread
  service) — port that pattern for the social layer, then build clan chat
  as: engine service + packets + varclan state + CS2 host ops; membership
  policy/messages in content where expressible. Do friends/PM first; clan
  chat reuses its plumbing.

### 5.3 Feature checklist

Before writing anything: (1) which interface/CS2 scripts implement the
client half, verified by decompilation; (2) which host CS2 ops it needs
(`cs2_command.gen.h` row + stack shape); (3) which packets, and whether
rev-230 already frames them; (4) what state the server must track and where
it persists (`.varp`-declared with `scope=perm`, or a service); (5) what is
expressible as content. Write the doc section (in the feature's topic doc)
*with* the implementation — every landed feature above has one, and the docs
are why this guide could be written.

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
1. Multiplayer: encoders take a player (81 decls, ~90 sites), tick phases
   iterate the pool, `PLAYER_INFO` encodes others. The bulk of remaining
   engine work.
2. `ZoneMap` keyed `(zx,zz,level)` with buffered/replayable events — what
   multiplayer actually needs, and what the 806 zone-trigger uses land on.
3. **Invert the script fallback** — one `_` wildcard fallback; a trigger
   with no script does nothing, loudly. Must precede bulk trigger import
   (triage §9 step 1) or every behavior has two implementations that can
   disagree.
*Gate: two clients see each other fight over a door.*

**Phase 2 — symbols and surface** (triage §9 steps 2–5)
Name-resolution gate → constants (1,562) → npc categories (§7.6b) →
param/struct/enum/dbtable ids → varp/varbit reclass → name maps → opcodes by
leverage (param decoder → `loc_*` → `npc_*` → `runclientscript_ss` strings →
small-wide) → undispatched triggers (queue/timer cheap; `*u` use-on; zone
family once ZoneMap exists). Track via the generated
`mock230_opcode_coverage.gen.h` (224/399 today) and the load-time gap
report — **never via numbers typed in prose.**

**Phase 3 — evict the C content**
The trivial list (§2.5) immediately — each is an hour. The blocked ~3,200
lines (bank, combat policy, equipment, world map, doors, login burst) as
Phase 2 unblocks each — the bank's own header documents what it is waiting
for. *Gate: `grep` finds no game-facing string literals and no id constants
in `src/net/mock/` outside the wire tables.*

**Phase 4 — content, in slices** (triage §10)
Done: `levelrequire/`, Cook's Assistant. Next, in order of unlock:
`drop tables/` (needs categories, 3b) → shops (wants the shop dbtable
pattern) → skills by directory (each unlocks with `loc_*`/`npc_*` families)
→ areas/quests on demand. Each slice per §4's workflow with a permanent
check.

**Phase 5 — modern features** (§5)
XP-drops VM loop fix → friends/ignore/PM decode + service → clan chat →
whatever the client's cache surfaces next (the discovery procedure is §5.1).

**Phase 6 — rename** (`osrs230_mockserver.md` §6.1 item 7): `mock230` is a
double misnomer; cheapest while consumers are few.

---

## 7. Guardrails and verification

- **Build:** `make -C src` (plain make, not CMake). Script pack:
  `make -C src mock230-scripts`. Agents sharing the repo must set a private
  objdir (`PLATFORM_OBJ_BASE`) — stale-`.o` races are real.
- **Tests:** `make -C src test-db`, `test-mock230-coverage` (fails if the
  generated coverage header is stale), `mock230_servercodec_test`; cache
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
   - content porting → [`LOSTCITY_PORT_TRIAGE.md`](LOSTCITY_PORT_TRIAGE.md)
     §1, §9, §10.1, §12, and
     `LostCity_Server/content/scripts/README.md`
   - ServerScript → [`serverscript.md`](serverscript.md)
   - UI-facing features → [`REV230_UI_BLANK_PANELS.md`](REV230_UI_BLANK_PANELS.md),
     [`UI_ERA_PORTING_GUIDE.md`](UI_ERA_PORTING_GUIDE.md)
4. The reference source itself. The answer is in the source, not in
   intuition.
