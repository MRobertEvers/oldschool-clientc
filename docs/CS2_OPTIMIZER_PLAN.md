# CS2 optimizer and VM optimization

Two halves, one document.

**Part A (§1–§9)** — an optimizing back end over the CS2 compiler's existing
intermediate representation, producing optimized clientscript bytecode
**beside** the authored sources — never over them — and a way to tell
`cachepack pack` to ship the optimized set.

**Part B (§10–§13)** — the runtime: making `src/cs2vm2` lean on memory
(lazy, right-sized allocation instead of fixed 12 KB frames and 1.3 KB request
structs) and faster per opcode and per host call, without touching its
architecture: opcodes still delegate to the host through `host_exec`, and IO
still surfaces as a yield that is replayed from a per-op checkpoint.

Targets: proc inlining, recursion unrolling (and tail-recursion → loop), loop
unrolling, value coalescing (constant/copy propagation + folding + local-slot
coalescing), plus the small passes those need to pay off (dead code, branch
folding, peephole) and two later, larger wins (cache-constant folding, CSE).

Written 2026-08-17 against `3rd/rscache` and `src/cs2vm2` as they stand on
branch `v3`. Sizes quoted in Part B were measured with `sizeof` against the
current headers on this machine (arm64 macOS).

---

## 0. The decision in one paragraph

Optimize on the **linear IR** that `cs2_interp.c` + the first seven `cs2_dfa.c`
passes already produce (expression trees over labels/branches/switches), and add
the one piece the pipeline is missing — an **IR → bytecode lowering** — instead
of regenerating source and re-running the parser. Bytecode has `goto`; source
does not, and every one of the target optimizations wants to emit a jump the
language cannot spell (early return out of an inlined body, loop exits from an
unrolled copy, tail-call back edges). Lowering also gives us a corpus-wide
identity check (`interp → lower` must reproduce the cache's own bytes) that
proves the IR is a faithful program representation before a single
optimization runs. Output goes to `scripts.opt/<name>.cs2b` (+ a human listing),
`scripts/` is untouched, and `cachepack pack --cs2-opt` prefers the `.cs2b`
when a manifest proves it is fresh.

---

## 1. What exists today

```
                        3rd/rscache/src/cs2/
 source .cs2 ──cs2_compile.c──► bytecode (RSCache_CS2_Script)   [direct emit, no IR]
                                    │
                             cs2_interp.c   bytecode → IR by stack simulation
                                    │
                             cs2_dfa.c      9 passes: dead code, nops, reorder args,
                                    │       array args, combine, inline stack defs,
                                    │       nops, calc types, calc identifiers,
                                    │       short-circuit
                             cs2_cfa.c      IR → if/while/switch (dominator tree)
                             cs2_gen.c      structured IR → source
```

- IR: [cs2_ir.h](../3rd/rscache/src/cs2/cs2_ir.h). `RSCache_CS2_Insn` chain
  (`ASSIGNMENT | RETURN | BRANCH | SWITCH | LABEL | GOTO`), `RSCache_CS2_Expr`
  trees (`CONSTANT | ACCESS | POINTER | EVENT_PROPERTY | COMPOUND | OPERATION |
  PROC | CLIENTSCRIPT`), interned `RSCache_CS2_Variable` (kind, script, id),
  everything arena-allocated per `RSCache_CS2_FunctionSet`.
- Command table: [cs2_command.h](../3rd/rscache/src/cs2/cs2_command.h) +
  generated `cs2_command.gen.h` — name, kind, pop/push prototypes per opcode.
  No effect/purity information.
- Driver: [cs2_decompile.c](../3rd/rscache/src/cs2/cs2_decompile.c) does
  `Interpret(one id) → Transform → Reconstruct → Generate`, one script at a
  time, callees loaded through `RSCache_CS2_ScriptSource` for signatures.
- Packer: [cp_decode.c](../3rd/rscache/tools/cachepack/cp_decode.c)
  `cp_codec_script` — `script_read` compiles `scripts/<name>.cs2`; when it
  declines, [cp_assets.c](../3rd/rscache/tools/cachepack/cp_assets.c) reads the
  raw `<name>.cs2b` (`CP_ASSET_SCRIPT` ext), and failing that keeps `--base`
  bytes and counts a *declined*. `g_cs2` already holds names, param types,
  db columns and a lazy table-12 loader — everything an optimizer driver needs.
- Runtime: [src/cs2vm2](../src/cs2vm2/cs2vm2.c). `gosub` (`CS2VM2_Op_GosubWithParams`)
  is a `host_exec(PUSHSCRIPT)` round trip that may **yield for IO** to load the
  callee, then a 12 KB frame from the pool, argument pops per bank; call depth
  cap `CS2VM_MAX_FRAMES = 128`; `switch` is a linear scan; arrays are
  **per-frame handles parked in string locals** (`CS2VM2_Op_DefineArray`), passed
  to procs as ordinary string arguments.
- Corpus (osrs239 content tree): 9,388 `.cs2` + 357 `.cs2b`; 17,718 static
  `~proc` call sites over 2,905 procs (`~create_graphic` 624, `~max`/`~min`
  591, `~tostring_spacer` 434); 8 self-recursive procs; 888 scripts with `while`.
- Existing gates to keep green: `test/test_cs2.c` gold-corpus decompile,
  `cs2 roundtrip`, cachepack fidelity pass.
- Existing measurement hooks: `TORIRS_CS2_PROFILE` (per-script ns), perf
  counters `TORIRS_PERF_CTR_CS2_OPCODES/CYCLES`, `TORIRS_CS2_TRACE` /
  `CS2VM2_TraceCaptureBegin`, `TORIRS_CS2_HARNESS` + `tools/perf/cs2_cases`.

---

## 2. Design

### 2.1 Pipeline

```
 per script id, resolved exactly as `cachepack pack` would ship it:
     scripts/<name>.cs2 (compiles)  →  scripts/<name>.cs2b  →  --base cache table 12
                                │
                                ▼  RSCache_CS2_Script (bytecode)
                     RSCache_CS2_Interpret               (unchanged)
                     RSCache_CS2_TransformCore           (dfa passes 1–7, NEW split)
                                │  linear IR
                     RSCache_CS2_Optimize                (NEW  cs2_opt.c, level 0..3)
                                │  linear IR
                     RSCache_CS2_Lower                   (NEW  cs2_lower.c)
                                │  RSCache_CS2_Script
                     RSCache_CS2_Interpret again         (NEW use: stack-shape verifier)
                                │
                     RSCache_ClientScriptEncode → scripts.opt/<name>.cs2b
                                                 scripts.opt/<name>.cs2asm   (listing)
                                                 scripts.opt/manifest.ini
```

The unit of exchange between scripts is **bytecode**, not IR: a caller that
inlines `~max` interprets the *optimized* `.cs2b` of `max` it just produced.
That keeps memory bounded to one caller plus its callee closure, means the
inliner sees exactly the bytes that will ship, and lets scripts be processed in
call-graph SCC order (callees first) without a whole-corpus FunctionSet.

### 2.2 Why not "optimize, regenerate source, recompile"

Considered and rejected as the primary route:

- `cs2_gen.c` needs `cs2_cfa.c` to structure the flow graph, and the cfa
  recognises only the shapes Jagex's compiler emits. An inlined body with an
  early `return` becomes a `goto` past the rest of the caller; an unrolled loop
  copy needs a mid-body exit; a tail-recursive proc becomes an unconditional
  back edge. Each would need a restructuring pass (`if (c) { A; return } B` →
  `if (c) {A} else {B}`, flag variables for loop exits) whose only purpose is to
  satisfy a printer.
- The compiler emits **exactly** the shapes it parses (header of
  [cs2_compile.c](../3rd/rscache/src/cs2/cs2_compile.c)), so nothing is gained
  in generated-code quality by going through it.
- The listing is still wanted for review — so where the optimized bytecode
  *does* still decompile, `scripts.opt/<name>.cs2asm` carries the decompiled
  source; otherwise a disassembly. Either way it is an **artifact, never an
  input**; the extension is deliberately not `.cs2` so the packer's script codec
  never tries to compile it.

### 2.3 Which IR stage

After dfa passes 1–7 (`remove_dead_code, delete_nops, reorder_args,
find_array_args, combine_same_line, inline_stack_definitions, delete_nops`) and
**before** `calc_types`, `calc_identifiers`, `add_short_circuit`:

- Types/identifiers are for printing; the IR already carries every stack type
  (`Value.stack_type`, `VarStackType(kind)`), which is all lowering needs.
  Skipping the solver also widens coverage: scripts the decompiler refuses for
  "irreconcilable types" still have a perfectly well-formed stack shape.
- `add_short_circuit` synthesises `SS_AND`/`SS_OR` (opcodes −2/−1) that have no
  bytecode; leaving them out keeps `BRANCH` insns as plain compare ops.

Change to existing code: split `RSCache_CS2_Transform` into
`RSCache_CS2_TransformCore(fs, …)` (passes 1–7) and the existing
`RSCache_CS2_Transform` calling core then the three source-only passes. The
decompiler's behaviour and the gold corpus are unchanged.

### 2.4 Argument mapping is by bank ordinal, not by `function->arguments`

`cs2_reorder_args` reorders a callee's argument *list* to match call-site
interleaving for printing. The VM (`CS2VM2_Op_GosubWithParams`) maps the i-th
int value pushed to int local i and the j-th string value to string local j.
The inliner and the lowerer use the VM's rule and ignore the printed order.

---

## 3. Output layout and the packer contract

### 3.1 On disk — nothing under `scripts/` is written

```
<tree>/scripts/                 authored, untouched
<tree>/scripts.opt/             generated, git-ignored, regenerated by make
    manifest.ini
    script_1045.cs2b            optimized bytecode (encoded clientscript container)
    script_1045.cs2asm          listing: decompiled source when it structures,
                                else disassembly; header comment says which
    ...
```

`manifest.ini`:

```
[cs2opt]
version = 1
level = 2
tool = <git describe of the tree that built it>
inputs = <hash over: every scripts/*.cs2|.cs2b, base table-12 CRCs, names dir,
          the effect table version, the pass set>

[scripts]
; id = <hash of the resolved input bytecode>,<ops before>,<ops after>,<passes that fired>
1045 = 9f2c…,9,9,
1004 = 31ab…,412,377,inline:3 fold:11 dce:2
```

Only scripts the optimizer *changed* need a `.cs2b`; an unchanged script has a
manifest line and no file, and the packer falls through to `scripts/` as today.
Whole-program passes (inlining) mean a change to one callee can invalidate many
callers, so freshness is decided by the single `inputs` hash — an edit anywhere
means "re-run `make cs2-opt`", which is what the make dependency does.

### 3.2 Packer

`cachepack pack --src T --out C [--base B] --cs2-opt [DIR]` — `DIR` defaults to
`T/scripts.opt`. In `script_read` (cp_decode.c), before compiling source:

1. If `--cs2-opt` is set and the manifest lists this id with a `.cs2b` present,
   recompute the resolved-input hash the same way the optimizer did and compare.
2. Fresh → return those bytes (counted as *optimized* in the report line).
3. Stale → **fail the pack** by default with "scripts.opt is stale (N scripts);
   run `make cs2-opt`". `--cs2-opt-stale=fallback` compiles source instead and
   warns. A stale-but-shipped optimized script is precisely the silent defect
   the repo's other codecs refuse (see the *declined* accounting in
   `cp_assets.c`), so failing is the right default.
4. No optimized file → existing behaviour (compile `.cs2`, else `.cs2b`, else
   base bytes).

`cachepack cs2opt --src T [--base B] [--out DIR] [--level N] [--only id,…]
[--names DIR]` — the driver subcommand (see §4.5). It lives in cachepack rather
than `tools/cs2` because it needs the same script resolution and side tables
the pack does; `tools/cs2 optimize --cache DIR` is a thin second front end for
corpus experiments and tests.

### 3.3 Make

```
src/makefile
  cs2-opt:            $(CACHEPACK_BIN) cs2opt --src $(TORIRSSERVER_CONTENT_DIR) \
                        --base $(TORIRSSERVER_CACHE_BASE) --level $(CS2_OPT_LEVEL)
  torirsserver-cache:      … $(if $(CS2_OPT),--cs2-opt,) ; when CS2_OPT=1 depends on cs2-opt
```

`CS2_OPT` defaults off until the gates in §7 pass; then the default flips and
`CS2_OPT=0` is the escape hatch. Add `scripts.opt/` to `OSRS-Content`'s
`.gitignore`.

---

## 4. New modules (all in `3rd/rscache/src/cs2/`, added to `rscache_unity.c`)

### 4.1 `cs2_lower.c/h` — IR → bytecode

```c
bool RSCache_CS2_Lower(struct RSCache_CS2_FunctionSet* fs,
                       struct RSCache_CS2_Function* fn,
                       const struct RSCache_CS2_LowerOptions* opt,   /* keep_dead_gotos, … */
                       struct RSCache_CS2_Script* out,
                       char* error, int error_capacity);
```

- Number labels by chain position; branch/goto/switch operands are relative to
  the next instruction (`frame->pc += operand`).
- Expression emit is post-order, children in list order (that *is* the
  chronological push order — `cs2_translate_proc` keeps it deliberately).
  Per kind: `CONSTANT` → `PUSH_CONSTANT_INT/STRING` (event-property magic ints
  are plain constants); `ACCESS` local → `PUSH_INT_LOCAL / PUSH_STRING_LOCAL`,
  global → `PUSH_VAR / PUSH_VARBIT / PUSH_VARC_INT / PUSH_VARC_STRING /
  PUSH_VARCLANSETTING / PUSH_VARCLAN`; array read → index then
  `PUSH_ARRAY_INT` (operand = handle slot); `OPERATION` → args then opcode
  (operand: dot flag, `JOIN_STRING` count, `DEFINE_ARRAY` packed slot/type,
  discard kinds …); `PROC` → args then `GOSUB_WITH_PARAMS`; `CLIENTSCRIPT` →
  invert `cs2_translate_clientscript` (args, trigger list, component, descriptor
  string operand). Each `cs2_translate_*` in the interpreter is the spec; the
  lowerer is its inverse, one function per kind.
- `ASSIGNMENT`: expression, then one pop per definition, last-defined first;
  array element stores push the index before the value.
- `RETURN`: values in list order (banks separate on the wire anyway), then
  `RETURN`. `BRANCH`: two args then compare opcode (7/8/9/10/31/32).
  `SWITCH`: expr, `SWITCH` with a fresh table.
- Leftover synthetic stack variables (`VAR_STACKINT/STACKSTRING` that
  `inline_stack_definitions` could not fold — a value living on the operand
  stack across a merge) are **spilled to fresh locals**. Rare, semantically
  identical, costs byte-exactness on those scripts only.
- Trailer: local counts = highest referenced slot + 1 per bank (reads count —
  `cs2_cc_declare_local_from_name` explains why); arg counts from the function;
  signature copied from the input script. `dead_goto_follows` is re-emitted when
  `keep_dead_gotos` so the identity check can be byte-exact.

**Gate:** `cs2 lower --cache cache.osrs239` reports exact / same-length /
mismatch / failed over all 7,884 scripts. Every non-exact script must be in an
explained category (spill, dead-code removal, …) before Phase 2 starts.

### 4.2 `cs2_cfg.c/h` — analysis over the chain

Basic blocks from labels/branches/switches/returns; successor lists; reverse
post-order; dominators (Cooper-Harvey-Kennedy, ~100 lines — the same algorithm
`cs2_cfa.c` has internally; keep this a separate module first and fold the cfa
onto it only after the gold corpus proves nothing moved); back edges → natural
loops with header/body/exits; per-variable def/use lists; reaching
definitions and liveness (bit-vectors over the block list). Rebuilt on demand
after any pass that touches the chain (`cfg_invalidate`).

### 4.3 `cs2_effects.c/h` — the safety boundary

Per opcode, one of:

| class | meaning | who may touch it |
|---|---|---|
| `PURE` | deterministic function of its stack args, no reads of any state | fold, propagate, CSE, hoist, delete when unused |
| `READ_STATE` | reads varp/varbit/varc/clan/component/inv/… , no writes | CSE within a block with no intervening `WRITE`/`HOST` (later); never fold |
| `READ_CACHE` | reads immutable cache config (`enum`, `oc_*`, `nc_*`, `loc_*`, `struct_param`, `db_*` on static tables) | fold **only** in the cache-aware level, and the manifest hash then covers those config archives |
| `WRITE_STATE` / `HOST` | everything else, default | order preserved absolutely; may not be deleted |
| `CONTROL` | branch/switch/return/gosub | handled by passes structurally |

The table is generated as **default `HOST`** with an explicit allowlist for the
first three, and the allowlist is derived by reading each `CS2VM2_Op_*` handler
in `src/cs2vm2/cs2vm2.c`: an opcode is `PURE` only if its handler touches
nothing but the operand stacks and the string pool and calls no `host_exec`.
`random`/`randominc` (4004/4005) are not pure. Every allowlisted opcode gets a
line in the table naming the handler it was checked against, and the table has
a version number that feeds the manifest `inputs` hash. Getting one entry wrong
here is a miscompile, so this file is small, boring, and hand-audited.

### 4.4 `cs2_opt.c/h` — passes and driver

```c
struct RSCache_CS2_OptOptions {
    int level;                       /* 0 lower only … 3 cache-aware */
    int inline_max_callee_ops;       /* default 48 */
    int inline_max_growth_ops;       /* caller may grow to max(4x, +2000) */
    int inline_rounds;               /* default 3; callee-first order makes this small */
    int recursion_unroll_depth;      /* default 2 */
    int loop_unroll_full_max_trips;  /* default 8 */
    int loop_unroll_full_max_ops;    /* body*trips cap, default 256 */
    int loop_unroll_partial_factor;  /* default 0 (off) until measured */
    bool cache_constant_folding;     /* level 3 */
    struct RSCache_CS2_ScriptSource optimized_callees;   /* .cs2b already produced */
    struct RSCache_CS2_ParamTypes param_types;
    struct RSCache_CS2_DbColumnTypes db_columns;
    /* level 3: */ enum lookups, param values (from the open cache)
};

bool RSCache_CS2_Optimize(struct RSCache_CS2_FunctionSet* fs,
                          struct RSCache_CS2_Function* fn,
                          const struct RSCache_CS2_OptOptions* opt,
                          struct RSCache_CS2_OptStats* stats,      /* per pass counts */
                          char* error, int error_capacity);
```

Pass order per level (each pass runs to its own local fixpoint; the level-≥1
loop repeats the cheap passes after every structural one):

```
O0   (nothing)                                     — identity, the baseline
O1   const/copy propagation → constant folding → branch folding
     → unreachable-block removal → dead-store elimination
     → jump threading → slot coalescing → peephole (at lowering)
O2   O1 + inlining (callee-first, size-bounded)
        + tail-recursion → loop
        + recursion unrolling (depth k, residual call kept)
        + loop unrolling (full for constant trips; partial off by default)
     then O1 again over the grown body
O3   O2 + cache-constant folding (enum/param/struct/name lookups on constant ids)
        + CSE of PURE / READ_STATE expressions within a block
        + loop-invariant hoisting of PURE expressions
```

### 4.5 Driver (`cachepack cs2opt`, `tools/cachepack/cp_cs2opt.c`)

1. Resolve every id in `pack/12_clientscripts.pack`: compile tree `.cs2` (cache
   the result), else tree `.cs2b`, else base table 12. This mirrors what `pack`
   ships and it is the **input hash** basis.
2. Build the call graph by scanning `GOSUB_WITH_PARAMS` operands (bytecode
   scan, no interp needed). Tarjan SCCs; process in reverse topological order
   so a caller always inlines an already-optimized callee.
3. Per script: interp → core transform → optimize → lower → **re-interpret the
   result** (a wrong pop count does not fail lowering; it fails the second
   interp, which is the point of running it) → encode → write when changed →
   listing → manifest line. A script that fails at any step is *left alone*
   (manifest says `skipped:<reason>`) — an unoptimized script is never a bug,
   a wrongly optimized one is.
4. Report: scripts optimized / unchanged / skipped, static ops before → after,
   gosubs removed, per-pass totals.

Cost estimate: compiling and interpreting the corpus is what `cs2 roundtrip`
already does; the optimizer adds passes that are linear-ish in script size.
Budget a whole-tree run at "coffee, not lunch"; measure in Phase 1 and add a
job-parallel driver only if it is over a minute.

---

## 5. The passes

Each pass states: what it does on *this* IR, its safety condition, and the test
that pins it. Tests are pairs of `.cs2` sources (unoptimized vs expected shape)
compiled by the existing compiler and compared at IR level, plus the runtime
differential in §7.

### 5.1 Constant / copy propagation, folding, branch folding, DCE (O1)

- Reaching definitions over the CFG (§4.2). A local read with exactly one
  reaching definition that is a `CONSTANT`, or an `ACCESS` of another local not
  redefined on any path between, is replaced. Globals are never propagated
  (`READ_STATE`).
- Folding: `OPERATION` with a `PURE` opcode and all-constant args → `CONSTANT`.
  Arithmetic must be **bit-identical to cs2vm2**: 32-bit wrap for
  add/sub/multiply, truncating div/mod, `pow`/`invpow`/`addpercent`/
  `interpolate` copied from the VM's handlers, and *never* fold a div/mod by
  zero (the VM errors; keep the op so the error still happens). String folding
  in v1 is only `tostring(const)` and `join` of constants; `lowercase`,
  `append_num` etc. wait for a shared cp1252-exact helper. A unit test in
  `src/cs2vm2/test/` runs the folder's arithmetic against the live VM over an
  edge-case table.
- Branch folding: `BRANCH` on two constants → `GOTO` or nothing; `SWITCH` on a
  constant → `GOTO` case; unreachable blocks removed; `GOTO` to the next insn
  removed; `GOTO → GOTO` threaded; label with no predecessors removed.
- Dead-store elimination: an `ASSIGNMENT` to a local that is not live-out and
  whose expression is `PURE`/`READ_STATE` (i.e. deletable) is dropped; if the
  expression has effects it stays but the definition becomes a discard
  (`POP_*_DISCARD`). Never touches globals or array stores.

### 5.2 Inlining (O2)

Candidate: a `PROC` expression whose callee (a) is not in the caller's SCC,
(b) has ≤ `inline_max_callee_ops` ops **or** exactly one static call site
in the whole corpus, (c) does not touch arrays in v1 (see §6.4), (d) keeps
the caller under the growth cap, (e) does not exceed the VM's
`CS2VM_MAX_LOCALS`/frame limits after slot allocation.

Mechanics on the linear IR:

1. Interpret the callee's *optimized* bytecode into the caller's FunctionSet
   (fresh function; its variables are interned under the callee's script id, so
   they cannot collide with the caller's).
2. **Rename** every callee local `(kind, callee_id, slot)` to a fresh caller
   local `(kind, caller_id, next_free_slot++)` per bank; array handle slots and
   `DEFINE_ARRAY` operands rename with their string slot (v2, once §6.4 is
   settled).
3. **Initialise** each renamed local that the callee reads before writing on
   some path (definitely-assigned analysis; v1 may simply initialise every
   read local: ints to `0`, strings to `""`) — the VM memsets a fresh frame, the
   caller's slots are not fresh.
4. **Bind arguments** by bank ordinal: the i-th int-typed argument expression
   assigns int local i, the j-th string-typed one string local j, evaluated
   left to right in the call's argument order.
5. **Splice**: callee chain inserted at the statement containing the call, with
   every callee `RETURN(values)` rewritten to `assign result temporaries ←
   values; GOTO Ljoin`, and `Ljoin` placed after the splice. The `PROC` node in
   the caller's expression is replaced by the temporaries (a `COMPOUND` when
   the callee returns more than one value).
6. **Hoisting out of an expression**: if the call is nested (`calc(~foo() + $x)`,
   an argument of a host op, …), everything to its *left* in the same statement
   is evaluated first in the original. Siblings that are `CONSTANT` or `ACCESS`
   of a caller local are safe to leave; every other left sibling is spilled to
   a temporary before the splice unless the callee's effect summary is
   `PURE`-only. Right siblings evaluate after the call in both versions and
   need nothing.
7. Then O1 over the caller (constant args now fold through the body; the
   `Ljoin` gotos thread; dead initialisations vanish).

Callee bodies are never deleted: hooks and other packs address scripts by id.
Callee-first ordering means `~tostring_spacer` is itself already folded before
its 434 callers see it.

### 5.3 Recursion (O2)

- **Tail recursion → loop.** A self-call whose result feeds only a `RETURN`
  (`return(~self(a, b))`) or that is a statement immediately followed by a
  void `RETURN` becomes: assign the argument locals (through temporaries, all
  RHS evaluated before any LHS) → `GOTO Lentry`. Removes a frame per
  iteration and lifts the 128-depth cap for those procs. `clan_permission_get`
  and the eight self-recursive procs are the test set — check each is tail or
  not before assuming a shape.
- **Recursion unrolling.** For non-tail self-calls, apply §5.2 to the self-call
  `k` times (default 2), leaving the residual call to the original id. Each
  unrolled copy gets its own slot range; the size cap still applies. Depth k
  divides dynamic frame count by k+1 and gives the folder k levels of
  constant arguments to chew on.

### 5.4 Loop unrolling (O2)

Loops come from `cs2_cfg` back edges (header + body blocks + exits), so this
works after inlining has made the flow graph un-structurable for the cfa.

- **Full unroll** when the loop has one induction variable `$i` with one
  constant-stride assignment in the body (`$i = calc($i ± c)`), a constant
  initial value reaching the header, an exit test `$i <op> const` on the
  header, no other writes to `$i`, trip count ≤ `loop_unroll_full_max_trips`
  and `trips × body_ops` ≤ cap. Body copies are chain clones with fresh labels;
  the header test is deleted; `$i` becomes a constant per copy and folds. The
  `while ($int3 <= 5) { if (cc_find(…)) … }` shape in `script_1005` is the
  motivating case.
- **Partial unroll** (factor f, off by default): body cloned f times with the
  exit test replicated between copies as `BRANCH → Lexit`; only when the exit
  test is `PURE`/`READ_STATE` and cheap. Enable once the profile (§7.4) shows a
  hot loop that wants it.
- Never unroll a body containing a `RETURN`, a `SWITCH`, a nested loop, or a
  self-call.

### 5.5 Value coalescing (O1 + O2)

The term covers three things here; all three are in scope:

1. **Value numbering.** Within a block, `PURE` operations over the same value
   numbers are one value; the second occurrence reads the first's temporary
   (CSE). `READ_STATE` joins in at O3 only, and only with no `WRITE`/`HOST` op
   between.
2. **Copy coalescing.** `$a = $b` chains collapse (5.1's copy propagation plus
   dead-store elimination); temporaries introduced by 5.2/5.3/5.4 are the
   main customers.
3. **Slot coalescing.** After the passes, build an interference graph over
   locals per bank (liveness from §4.2), colour greedily with the function's
   arguments pinned to slots `0..n-1` (the VM writes arguments there), and
   renumber. This is what keeps an inlined caller's frame small and inside
   `CS2VM_MAX_LOCALS`, and it makes byte-level output deterministic (colour
   order = first-use order).

### 5.6 "and more" — worth doing, after the above

- **Cache-constant folding (O3).** `enum(int,int,enum_681,$k)` with constant
  `$k`, `oc_param(obj_const, param_const)`, `struct_param`, `oc_name` and
  friends read immutable cache data — the packer has the cache open. Folds to
  a `CONSTANT`; the manifest `inputs` hash then covers the config archives
  those opcodes read, so a config edit invalidates the opt tree. `script_1007`'s
  `while ($stat2 ! null) { … enum(int, stat, enum_681, $int1) … }` fully
  unrolls under this.
- **Loop-invariant hoisting** of `PURE` expressions to a preheader.
- **Peephole at lowering**: `push_local x; pop_local x` → nothing;
  `push_const; pop_discard` → nothing; `branch_c L1; goto L2; L1:` →
  `branch_!c L2` when the compare has an inverse; `goto` to `return` →
  `return`. Cheap and byte-visible.
- **Hook argument descriptors** are left alone. `cc_seton*` payloads are data,
  not code.

---

## 6. Semantic hazards — the checklist every pass is reviewed against

1. **Fresh-frame zeroing.** Callee locals start `0`/`""`; caller slots do not.
   §5.2 step 3.
2. **Evaluation order under hoisting.** Only constants and caller-local reads
   may stay to the left of a hoisted call; everything else spills. §5.2 step 6.
3. **Two operand stacks.** Ints and strings are separate banks; argument and
   return mapping is by bank ordinal (§2.4). Interleaving order between banks
   is irrelevant to the stacks but **not** to side effects — the list order in
   the IR is the chronological order and lowering keeps it.
4. **Array identity.** cs2vm2 parks an array handle in a *string local* and a
   proc receives one as an ordinary string argument, while the IR's
   `VAR_ARRAY`/`find_array_args` model comes from the older global-array
   reading. Which model the rev-239 bytecode actually means must be pinned by
   a test in `src/cs2vm2/test/` (define in caller, read in callee, both
   orders) **before** any array-touching callee is inlined. Until then: callees
   that define, read, write or receive arrays are not inline candidates, and
   `DEFINE_ARRAY` never moves relative to other array ops.
5. **Effects table correctness.** Default is `HOST`; every allowlist entry
   names the VM handler it was checked against (§4.3).
6. **Div/mod by zero, overflow.** Not folded / folded with wrap, per §5.1.
7. **Yield/resume.** The VM checkpoints per opcode and replays the yielding op;
   nothing here changes per-op semantics, but an unrolled body must not
   duplicate an array store between an op and its checkpoint (the undo log is
   per store, so it is fine — noted so it is not re-derived).
8. **Recursion.** SCC members are never inlined into each other except by the
   explicit unroll (§5.3); the residual call always remains.
9. **Script ids are stable.** No script is removed, renumbered, or has its
   signature/arg counts changed. Hooks keep working.
10. **Limits.** `CS2VM_MAX_LOCALS`, `CS2VM_STACK_MAX`, switch table byte
    budgets (`u16 case_count`), `CS2VM_MAX_CYCLES` — the size caps in §4.4
    keep everything far below them and the lowerer asserts them.
11. **The decompiler is untouched.** Only the `TransformCore` split lands in
    existing files; `test_cs2` gold-corpus numbers must not move.

---

## 7. Verification and measurement

1. **Identity gate (Phase 1 exit).** `cs2 lower --cache cache.osrs239 --level 0`
   over all scripts: ≥ 99% byte-exact, the rest in named categories with a
   count each, in the style of `EXCEPTIONS.md`.
2. **Re-interpret gate (every script, every run).** The lowered bytecode goes
   back through `RSCache_CS2_Interpret`; a stack-shape error there marks the
   script `skipped` and the run continues. The pipeline may also assert that a
   level-0 re-interp produces a structurally equal IR (expression-tree
   equality) as a stronger check for the test suite.
3. **Runtime differential — the real oracle.** Add a host-request log to the
   client (`TORIRS_CS2_HOSTLOG=path` in
   [task_cs2_run.c](../src/game/task_cs2_run.c) / `rs_cs2_host.c`): one line
   per `host_exec` with kind and arguments, per varp/varbit/varc write, per
   script return values. Run the harness cases (`TORIRS_CS2_HARNESS`,
   `tools/perf/cs2_cases/*.json`) and the headless UI smoke against
   `cache.osrs239.baked` built with and without `--cs2-opt`; logs must be
   identical after filtering `PUSHSCRIPT` (which inlining removes on purpose)
   and screenshots must hash-match. `tools/perf/cs2_trace_diff.py` gets a
   `--hostlog` mode.
4. **Per-pass unit tests** (`3rd/rscache/test/test_cs2_opt.c`): tiny `.cs2`
   pairs compiled by the existing compiler; assert the optimized IR shape and
   run both through the cs2vm2 test host (`src/cs2vm2/test/`) with recorded
   host requests. Include one deliberately broken transformation to prove the
   assertions can fail (repo rule: verify the blocker).
5. **Payoff.** `TORIRS_CS2_PROFILE` and `TORIRS_PERF_CTR_CS2_OPCODES` before
   and after on: login → gameframe settle, bank open, skill guide, spellbook
   filter, world map open. Report dynamic ops, gosubs, PUSHSCRIPT yields, and
   ms per hot script. Static: ops per script and total table-12 size (unrolling
   grows it; watch the web build's JS5 fetch volume).
6. **Existing gates unchanged.** `make -C 3rd/rscache test` (`test_cs2` gold),
   `cs2 roundtrip`, cachepack `verify`.

---

## 8. Milestones

| phase | delivers | exit criterion |
|---|---|---|
| 0 | `TransformCore` split; effects table skeleton (all `HOST`); host-request log in the client | gold corpus unchanged; hostlog diff of two identical runs is empty |
| 1 | `cs2_lower.c`, `cs2 lower` command, identity metric; `cs2_cfg.c` | §7.1 met; every non-exact script categorised |
| 2 | `cachepack cs2opt` driver at level 0; `scripts.opt/` + manifest; `pack --cs2-opt` with staleness check; make targets | a level-0 opt tree packs to a cache byte-identical (table 12) to the plain pack, except the categorised set; stale manifest fails the pack |
| 3 | O1 passes + slot coalescing + peephole; effects allowlist audited against cs2vm2 | §7.3 differential clean on harness + smoke; static op count down, number reported |
| 4 | Inlining (no-array callees), tail-recursion → loop, recursion unrolling | differential clean; dynamic gosub count and PUSHSCRIPT yields down, numbers reported; frame-depth test for the recursive procs |
| 5 | Loop unrolling (full), array-model test → array-touching callees admitted to inlining | differential clean; `script_1005`/`script_1007` shapes verified by IR test |
| 6 | O3: cache-constant folding, CSE, invariant hoisting; `CS2_OPT` default on | differential clean; payoff table in this document; EXCEPTIONS-style list of what is skipped and why |

Each phase lands behind its level flag; nothing before phase 6 changes what a
default `make torirsserver-cache` ships.

---

## 9. Open questions to settle first (in this order)

1. **Array model** (§6.4) — one cs2vm2 test decides how much of the corpus the
   inliner may touch. Cheap; do it during Phase 0.
2. **Byte-exactness of `interp → lower`** — the number from Phase 1 tells us
   whether the dfa's `reorder_args`/`combine_same_line` ever change
   chronological order in the IR (they should not; the metric proves it).
3. **What "value coalescing" was meant to include** — this plan takes it as
   value numbering + copy coalescing + slot coalescing (§5.5). If it meant
   something narrower, §5.5 shrinks; nothing else depends on it.
4. **Whole-tree run time** — decides whether the driver needs a job pool
   (Phase 2 measurement).
5. **Where the listing comes from** — decompile when it structures, else
   disassemble; if the decompiled fraction is high enough to be useful for
   review, keep both; if not, disassembly only.

---

# Part B — the VM

## 10. Where the memory and time go today

The architecture is right and stays: `CS2VM2_RunOp` interprets, anything that
touches the world is a `CS2VM_HostRequest` through `host_exec`, and a host that
must load something returns `CS2VM_EXECNO_YIELD`, after which the op is
replayed from a pointer-only checkpoint (`CS2VM2_SaveYieldCheckpoint`). Earlier
sessions already removed the worst of the up-front reservation (frames became
pooled pointers; the VM block is pooled; `cs2vm2_thread_init` stopped
memsetting bulk arrays). What is left is per-*use* waste, which the numbers make
concrete:

| thing | today | why it costs |
|---|---|---|
| `struct CS2VM2_Frame` | **12,352 B**, `int_locals[1024]` + `char* str_locals[1024]` | every `gosub` → `CS2VM2_PushCallScript` → `memset(frame, 0, 12352)`; a script with 6 locals pays for 1,024. Frame pool holds up to 128 of them = 1.5 MB retained |
| `struct CS2VM_HostRequest` | **1,336 B** (union of 142 members; the `*_seton*` members carry `int[64]` + `char[4][256]` inline) | `memset(&request, 0, sizeof)` at **184** call sites — a varp read zeroes 1.3 KB to send four bytes |
| `struct CS2VM2_Thread` × 4 | 19,120 B each, `CS2VM2` = **76,504 B** | only `threads[0]` ever runs (`CS2VM2_ThreadMain`); each `Task_CS2Run` holds a VM for its whole life, parked yields included |
| string locals | `CS2VM2_PushStrFrameLocal` **allocates a copy on every read**; `PUSH_CONSTANT_STRING` copies the operand on every execution | only because `UPPERCASE`/`LOWERCASE` rewrite their operand buffer in place |
| `CS2VM2_StrEmpty` | `StrPool_Alloc(0)` per empty push | same root cause: nothing may be shared |
| arrays | `define_array` mallocs its cell block per execution; freed at VM `Free` | per-run lifetime that the string pool already models |
| `switch` (op 60) | linear scan of the case table | script 7300 has 1,960 cases; skill guides / catalogues are the same shape |
| dispatch | ~800-way `switch` in `CS2VM2_RunOp` plus a second ~300-way switch for stack metadata; per op: `rs2_dialect` test, checkpoint save (6 stores), `undo_log_len = 0`, trace/debug hooks | fine individually; together the per-op fixed cost is bigger than most opcodes' work |
| host reads | `PUSH_VAR`/`PUSH_VARBIT`/`PUSH_VARC_*`/`cc_get*` each build a request → `RS_CS2Host_Exec` → 232-way switch → provider | the common read is one hash lookup wrapped in ~100 instructions of plumbing |
| host hot path | `getenv("TORIRS_DUMP_SETSIZE")` inside `CC_SETSIZE` (15 `getenv` calls in `rs_cs2_host.c`, several on op paths) | `getenv` is a linear scan of `environ` per call |
| `gosub` | host round trip → `CacheProvider_ClientScriptGet` (hmap) → push | the callee never changes once loaded; the lookup is repeated per call |

Baseline to capture before changing anything (all exist today):
`TORIRS_CS2_PROFILE=1` (per-entry-script wall time), the `TORIRS_PERF_CTR_CS2_*`
counters (opcodes, host ops, frame pool hits/misses, VM init ns), `MEMTRACE=1`
boot peak, and the harness cases. Add two counters: host ops **by request
kind** (a histogram, so the fast-path list in §12.2 is chosen from data) and
bytes memset per frame push. Record the numbers in §13 before Phase B1.

## 11. Memory: allocate what the script declares, when it needs it

### 11.1 Right-sized frames from a per-thread locals stack (the big one)

Replace the fixed frame with a small header plus two slices of thread-owned
growable stacks:

```c
struct CS2VM2_Frame {                /* ~48 B, lives in thread->frames[] inline */
    struct CS2VM2_Script* script;
    int pc;
    int  int_base,  int_count;       /* slice of thread->int_locals_stack  */
    int  str_base,  str_count;       /* slice of thread->str_locals_stack  */
    int return_pc, return_frame;
};
struct CS2VM2_Thread {
    struct CS2VM2_Frame frames[CS2VM_MAX_FRAMES];   /* 128 × 48 B = 6 KB, was 128 pointers + 12 KB blocks */
    int*   int_locals_stack;  int int_locals_top,  int_locals_cap;   /* grown on demand */
    char** str_locals_stack;  int str_locals_top,  str_locals_cap;
    ...
};
```

- `PushCallScript` bumps both tops by the script's trailer counts
  (`local_int_count`, `local_string_count`), zeroes **only that many** cells,
  and pops them on return. A depth-70 quicksort with 8 locals per frame uses
  ~5 KB of locals instead of 70 × 12 KB. The 128-frame pool, its free list and
  its 1.5 MB retention go away entirely.
- Argument passing becomes a straight copy from the operand stacks into the
  new slice (the same pops as today, no intermediate).
- Bounds: `int_locals[operand]` today is only guarded by `CS2VM_MAX_LOCALS`.
  With sized frames an operand ≥ `int_count` is out of the slice. The trailer
  counts are what the reference client sizes its arrays by, and the corpus
  check in §14.1 found **zero** scripts that reference a slot at or above
  their count, so an overrun is a malformed script, not a legitimate read;
  the VM checks `operand < count`, reads 0 / drops the write, and counts it
  once per script (`TORIRS_PERF_CTR_CS2_LOCAL_OOB`) as a tripwire.
- Sizing from the same numbers: median 3 ints / 0 strings, max 50 / 26. Start
  each locals stack at 256 cells and double on demand; a whole 128-deep
  recursion of the largest script fits in 25 KB.
- Array handles keep living in string locals; a slice cell is a `char*` as
  before, so `cs2vm2_array_local` needs only the base offset added.
- Yield/replay: the checkpoint records `int_locals_top`/`str_locals_top`
  alongside `frame_sp`; restore truncates. A yielding op leaves frame contents
  untouched (existing contract), so nothing else changes.

### 11.2 One thread per VM, threads on demand

`CS2VM2_MAX_THREADS` is 4 and `thread_count` is always 4, but only
`threads[0]` is ever started — and the audit (§14.3) found no other user, so
the constant simply becomes 1. With §11.1 a thread is ≈ 6 KB frames + 12 KB operand stacks
+ 4 KB misc; the VM block drops from 76 KB to ≈ 25 KB, and further if the
operand stacks are grown on demand like the locals stacks (start at 64, double;
`CS2VM_STACK_MAX` stays the ceiling).

### 11.3 A request is a header, not a kilobyte

Two changes, either is enough on its own; do both:

- **Shrink the union.** Move the fat payloads (`*_seton*` hook args:
  `int[CS2VM_SETON_INT_ARG_MAX]` + `char[CS2VM_SETON_STR_ARG_MAX][256]`,
  `WidgetSetOpKey`, `Highlight`, `Viewport`, `Loot`) behind pointers into a
  caller-owned scratch buffer (`struct CS2VM_HookArgs` on the opcode
  handler's stack, or bump-allocated in the string pool — its lifetime is the
  call). Target `sizeof(struct CS2VM_HostRequest) ≤ 64`; add a
  `_Static_assert` so it cannot grow back silently.
- **Stop memsetting.** `struct CS2VM_HostRequest r = { .kind = K, .u.x = {…} }`
  at each site (designated initialisers zero only what the compiler cannot
  prove unused, which after the shrink is nothing worth counting). A one-shot
  script rewrite of the 184 `memset(&request, 0, sizeof(request)); request.kind
  = …;` pairs, reviewed by diff.

### 11.4 Strings: immutable, shared, pool-owned

Make every string the VM hands around **immutable** and the copies disappear:

- `UPPERCASE`/`LOWERCASE` (and any other in-place mutator found by grep for
  writes through a popped `char*`) allocate their result from the pool instead
  of rewriting the operand. That is the whole prerequisite.
- Then `PushStrFrameLocal` pushes the local's pointer (no `StrDup`);
  `PUSH_CONSTANT_STRING` pushes the script's operand pointer directly (the
  script outlives every run; hosts that keep a string already copy —
  the strpool header documents that contract); `StrEmpty` returns one static
  `""`; `POP_STRING_LOCAL` stores the pointer.
- The pool remains the arena for *produced* strings (`join`, `tostring`,
  `substring`, …) and is still reset per run. Net effect: string traffic
  becomes proportional to strings *created*, not strings *read*, and the
  8 KB block reset per run holds far more scripts without a second block.
- One rule to keep: a script operand pointer must never be handed to
  something that will `free` it. The pool never frees individually, so the
  only risk is a host that frees a borrowed string; grep the host for `free(`
  on request strings once, add the assertion where the host copies.

### 11.5 Array cells from the run arena

`define_array` allocates its cell block from the same per-run arena as
strings (a second bump pool with pointer-wide alignment, or the same pool with
a `_Alignof(char*)` bump), sized exactly `size` cells, reset with the run. No
malloc/free per definition, no retained cell blocks on the VM, and
`cs2vm2_array_reserve`'s grow path becomes "allocate the new size, copy". The
5,000-cell ceiling stays.

### 11.6 What is *not* changed

The VM pool (`CS2VM2_Acquire/Release`) stays — after §11.1–§11.3 a VM is small
enough that a parked yield holding one is cheap, and the pool still saves the
malloc. `Task_CS2Run` keeps one VM per task.

## 12. Execution

Ordered by expected payoff per line changed; each has a counter that shows it
landed.

### 12.1 Right-sized frame push (from §11.1)

`gosub` cost goes from "12 KB bzero + pool pop" to "bump two tops, zero
2×locals". This is the single largest per-call saving and it is a memory
change, not a dispatch change.

### 12.2 Direct host fast paths for the hot reads

Keep `host_exec` + request as the general and yielding path. Add an optional
vtable the host binds beside it:

```c
struct CS2VM2_HostFastPath {
    int  (*read_varp)(void* user, int varp_id, int* out);      /* 0 = handled */
    int  (*read_varbit)(void* user, int varbit_id, int* out);
    int  (*read_varc_int)(void* user, int id, int* out);
    const char* (*read_varc_string)(void* user, int id);
    struct CS2VM2_Script* (*script_lookup)(void* user, int script_id);   /* NULL = go the slow way (may need to load) */
    /* cc_get* / if_get* readers added from the request-kind histogram */
};
```

`PUSH_VAR` becomes: fast path bound and returns handled → push; else build the
request as today. No memset, no 232-way switch, no yield bookkeeping for a
read that cannot yield. The list is chosen from the request-kind histogram
(§10), not guessed; anything under 1% of host ops stays on the general path.

### 12.3 Callee inline cache for `gosub`

Per script, a lazily allocated `struct CS2VM2_Script** callee_cache` parallel
to `opcodes` (only for scripts that contain a `GOSUB_WITH_PARAMS`, allocated
at first call). On a successful push through the host, store the resolved
`CS2VM2_Script*` at `callee_cache[pc]`; later executions push it directly.
Valid because the provider's clientscript map is never evicted while a client
runs (§14.4: session-lifetime hmap, no LRU, no callers of the cleanup); the
only way a cached pointer could dangle is a duplicate `ClientScriptAdd`
overwriting an entry, so that becomes an assertion. This
plus §12.1 turns a warm `gosub` into: bounds check, bump, zero N, copy args.

### 12.4 `switch` in O(log n)

At script decode (`cs2vm2_script.c`), sort each switch table's cases by key
and binary-search at run time; tables ≥ 64 cases may additionally get an
open-addressed int→pc map bump-allocated next to the script. Duplicate keys do
not occur (the encoder writes a map). Byte-for-byte the same bytecode; only
the in-memory table changes.

### 12.5 One generated dispatch table

`tools/cs2vm2/gen_opcode_stack.py` already generates the stack-shape table.
Extend it to emit, per opcode: the handler pointer, int/string pop counts,
and flags (`CAN_YIELD`, `PURE`, `IS_HOST`) into one
`static const struct CS2VM2_OpInfo g_ops[CS2VM2_OPCODE_TABLE_SIZE]`. Then:

- `CS2VM2_RunOp` is `g_ops[opcode].fn(vm, frame, operand, str)` (unknown
  opcode → the existing `StackMetaStub`), the two switches collapse into one
  table, and the RS2-dialect divert is a per-script pointer swap
  (`script->ops = rs2 ? g_ops_rs2 : g_ops`) instead of a per-op test.
- The per-op checkpoint save runs only when `g_ops[op].flags & CAN_YIELD`;
  `undo_log_len = 0` likewise. Non-yielding ops (arithmetic, local push/pop,
  branches — most executed ops) pay one load and one branch of overhead.
- The `PURE` flag is the same table Part A's `cs2_effects.c` consumes — one
  source of truth for "this opcode has no side effects", generated from one
  place, and the optimizer's allowlist audit (§4.3) becomes "the generator
  read it off the handler list", not a hand copy.

### 12.6 Host-side hygiene

Cache every `getenv` in `rs_cs2_host.c` in a static on first use (the pattern
`cs2vm2_strpool.c` and the profiler already use). Fifteen sites, mechanical.
Confirm `TORIRS_PERF_COUNT` compiles to a plain increment in `OPT=1` builds.

### 12.7 Later, if the profile still says the VM

- Superinstructions for the pairs the corpus emits constantly
  (`push_int_local; push_constant_int; add`, `push_constant_int; pop_int_local`,
  compare-and-branch after two pushes) — a peephole at script decode into a
  private extended opcode range, invisible to the cache and to Part A.
- Computed-goto dispatch (`&&label` tables) inside `cs2vm2_run_script_body`
  behind a compiler feature check; falls back to the table call.
- Neither is worth doing before §12.1–§12.5 have moved the numbers.

## 13. Verification and milestones for Part B

The oracle is the same host-request log Part A adds (§7.3): a VM change is
correct when the hostlog and screenshots for the harness cases and the
headless UI smoke are identical before and after, with `PUSHSCRIPT`
*not* filtered this time (Part B must not change how many scripts run). Add
one more line per run to the log: peak `int_locals_top`/`str_locals_top`,
peak operand stack depth, and pool bytes — so the memory claims are numbers.

Unit tests live in `src/cs2vm2/test/` beside the existing ones:
`frame_slices_test.c` (nested gosub, recursion to depth 100, arg passing by
bank, array handle through a proc, yield-and-replay inside a nested call),
`string_immutable_test.c` (uppercase/lowercase no longer alias locals or
operands; a constant pushed twice compares equal and is not the same buffer as
a produced string), `switch_lookup_test.c` (all cases and misses hit the same
targets as the linear scan over script 7300's table).

| phase | delivers | exit criterion |
|---|---|---|
| B0 | baseline numbers (§10) + request-kind histogram + hostlog memory lines | numbers recorded here |
| B1 | §11.1 sized frames + locals stacks; frame pool removed (`--check-locals` re-check lands with the lowerer, A1) | hostlog/screens identical; frame push bytes counter ≈ 2 × locals; `MEMTRACE` boot peak down, number recorded |
| B2 | §11.3 request shrink + memset removal; §12.6 getenv caching | `sizeof(CS2VM_HostRequest) ≤ 64` static-asserted; host ops/s up, number recorded |
| B3 | §11.4 immutable strings + §11.5 arena arrays | strpool `alloc_count` per run down (recorded); no `malloc` in a steady-state script run under `MEMTRACE` |
| B4 | §12.2 fast paths + §12.3 callee cache | host-op histogram shows the fast-pathed kinds gone from the request path; `gosub` cost measured before/after |
| B5 | §12.4 switch lookup + §12.5 generated dispatch table (shared PURE flag with Part A) | per-op overhead measured on a pure-arithmetic script; skill-guide open time recorded |
| B6 | §11.2 `MAX_THREADS = 1` / smaller VM block | `sizeof(CS2VM2)` recorded; parked-yield memory down |

Part A and Part B are independent until §12.5, where the effects table is
shared; do B0–B4 in parallel with A0–A2 if hands allow — B1 in particular
changes numbers everything else is measured against, so it goes first.

## 14. Open questions for Part B — answered 2026-08-17

1. **Are the trailer local counts trustworthy?** Yes, for the cache. Checked
   over all 9,724 scripts of `cache.osrs239` (`cs2 disassemble` of every id,
   max operand of ops 33/34 vs `local_int_count`, ops 35/36/45/46 and the
   high half of 44 vs `local_string_count`): **0 violations**. Distribution:
   int locals max 50, median 3, p99 29; string locals max 26, median 0, p99 5.
   No script declares more than 64 ints or 32 strings — the 12,352 B frame is
   ~1,000× the median script's need. Part A's lowerer computes counts for its
   own output, so the invariant holds for optimized scripts by construction.
   The runtime still bounds-checks against the slice (§11.1); the check is
   now a tripwire, not a fallback anyone expects to fire. Turn this one-off
   into `cs2 lower --check-locals` when the lowerer exists so it re-runs on
   every cache the client is pointed at.
2. **Does anything hold a `char*` across a run boundary?** It should not (the
   strpool contract). Before B3, one grep-audit of the host's string sinks
   (`UITree_ApplyText`, `VarCManager_SetString`, chat, clipboard, hook arg
   copies) confirms each copies; §11.4 changes the failure mode from
   use-after-free to use-after-reset, so this stays a B3 entry gate.
3. **Is `threads[1..3]` used?** No. `CS2VM2_ThreadMain` (`threads[0]`) is the
   only accessor in the tree; `CS2VM2_Run` runs `threads[0]`. §11.2 therefore
   sets `CS2VM2_MAX_THREADS` to 1 outright rather than allocating lazily —
   less code, same saving (57 KB per VM). Keep the field an array so the
   thread/VM split survives.
4. **Does the provider evict clientscripts?** No. `clientscript_cache` is a
   decode-once, session-lifetime hmap (`cache_provider.c` header: config
   caches "intentionally do not evict within a session"; only models and
   sprites are LRU). `CacheProvider_ClientScriptsCleanup` has no callers; the
   map is freed only at provider teardown. `CacheProvider_ClientScriptAdd`
   uses `HMAP_INSERT` and would silently overwrite an entry if a script were
   loaded twice, so §12.3's inline cache is guarded by making a duplicate
   `Add` an assertion (the load task already checks `Has` first) — a
   generation counter is not needed.
