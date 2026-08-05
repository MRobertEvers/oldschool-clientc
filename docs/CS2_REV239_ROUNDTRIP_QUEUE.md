# CS2 decompiler / recompiler — closing the rev-239 gap

> Handoff document. Written 2026-08-05 with every number below **measured, not
> estimated**, from `cache.osrs239`. Re-measure before trusting any of it — §1
> is one command.

**The job.** The CS2 decompiler and recompiler in `3rd/rscache/src/cs2/` do not
round-trip rev-239 clientscripts. Find the failing scripts, work out what the
official client actually does with them, and make the tools match. The official
deobfuscated client is the oracle, and it is available both as **source** and as
a **running, instrumented client** (§4).

**The bar.** `cs2 roundtrip` reports `exact` for every script it can, and every
script that still cannot be made exact has a written, specific reason. "It
compiles" is not the bar; byte-exact re-encode is.

---

## 1. The measurement, and today's baseline

```sh
make -C 3rd/rscache tools                       # builds 3rd/rscache/tools/cs2/cs2
./3rd/rscache/tools/cs2/cs2 roundtrip --cache cache.osrs239 --rev osrs239
```

`--rev` is not optional — the tool refuses to run without a cache identity.

Baseline, `cache.osrs239`, 2026-08-05, and where it stands after the work in §8:

```
2026-08-05  9507/9725 decompiled, 9440 compiled, 3850 same-length, 3080 exact
2026-08-05  9523/9725 decompiled, 9456 compiled, 8859 same-length, 8333 exact   (after §8)
```

| | baseline | after §8 | of 9725 |
| --- | ---: | ---: | ---: |
| decompiled at all | 9507 | 9523 | 97.9% |
| **failed to decompile** | **218** | **202** | 2.1% |
| compiled back | 9440 | 9456 | |
| same length as the original | 3850 | 8859 | |
| **byte-exact** | **3080** | **8333** | **85.7%** |

**Read the delta direction correctly.** `run_roundtrip` prints
`DIFF <id>: <written> bytes vs <original>` — the *recompiled* size first. The
baseline write-up in §2.1 below read it the other way round and concluded the
pipeline was dropping instructions; it was adding them. Nothing else in §2 is
affected, but the histogram there is signed backwards.

Other useful invocations (see the header comment in
`3rd/rscache/tools/cs2/main.c`):

```sh
cs2 decompile --cache cache.osrs239 --rev osrs239 --out /tmp/src 9721
cs2 compile   --raw <dir> --src /tmp/src --out /tmp/bin 9721
cs2 roundtrip --raw <dir> --rev osrs239 <id ...>     # RuneStar-style id-named dump
```

The `--raw` source exists so the decompiler can be checked against a corpus
whose known-good output came from the reference implementation, rather than only
against itself.

---

## 2. The three failure families, ranked

### 2.1 The recompile is a multiple of **6 bytes short** — the big one

This is the strongest signal in the data and where to start. Distribution of
`original − recompiled` over the 5,590 length-mismatch cases:

| delta | cases |
| ---: | ---: |
| **+6** | **2147** |
| +12 | 911 |
| +18 | 474 |
| +9 | 209 |
| −3 | 204 |
| +24 | 199 |
| +3 | 192 |
| +30 | 134 |
| +36 | 122 |
| −6 | 89 |

Multiples of six dominate overwhelmingly, and **6 bytes is exactly one CS2
instruction with an int operand** (`p2 opcode` + `p4 operand`). Read plainly:
the pipeline is dropping *whole instructions*, most often exactly one per
script, and the count scales in units of one instruction.

Two hypotheses worth separating before writing any code, because they need
opposite fixes:

- the **decompiler** never emits the instruction into source (it looks like a
  no-op, or it is folded into a neighbouring expression), or
- the **compiler** elides it when re-encoding (a push it thinks is redundant, a
  jump it thinks is fall-through, a trailing element of the script footer).

Find one instance and diff the two byte streams directly. `cs2 decompile` a
short offender, `cs2 compile` it back, and compare with the original — with only
6 bytes of difference the offending opcode is identifiable by eye. Do that
before generalising; the delta histogram says one root cause probably accounts
for thousands of scripts.

The `+3` / `−3` outliers are a second, smaller shape (a 2-byte-operand
instruction, or a string) and should be treated separately.

### 2.2 770 scripts: same length, different bytes

A pure re-encoding difference — the instruction sequence is right and some field
is encoded differently. Likely candidates: operand width chosen differently for
the same value, switch-table ordering, string encoding, or jump offsets computed
from a different base. Cheap to diff because the lengths line up: dump both and
find the first differing byte.

### 2.3 218 scripts fail to decompile at all

108 of those are one message, `opcode N has no recorded signature`, across
**30 distinct opcodes**:

```
208 216 1707 2506 2624 4127 4223 6758 6803 6859 6951 7043 7451 7600 7627
7800 7803 7804 7805 7807 7813 7814 7820 8005 8007 8010 8011 8014 8020 8026
```

Most frequent: `216` (26), `8005` (11), `6758` (9), `6803` (8), `2506` (7),
`6951` (5), `2624` (5), `7600` (4).

The rest are type/stack errors the interpreter raises while walking, and each
names its script and pc — e.g.

```
DECOMPILE 9721: script 9721 pc 14: opcode 1927 underflowed the operand stack
DECOMPILE 9601: return leaves 1 values, the script declares 0
DECOMPILE 9588: opcode N left 4 values on the operand stack
DECOMPILE 9581: opcode N was given an argument of the wrong stack type
DECOMPILE 9526: return value 0 is a int, the script declares a string
```

These are almost certainly **downstream of a wrong signature**: get the pops and
pushes wrong for one opcode and the stack model derails a few instructions
later. Fix signatures first and re-measure before chasing these individually.

---

## 3. Where the code lives

| path | role |
| --- | --- |
| `3rd/rscache/src/cs2/cs2_decompile.c` | bytecode → IR → source |
| `3rd/rscache/src/cs2/cs2_compile.c` | source → bytecode |
| `3rd/rscache/src/cs2/cs2_interp.c` | the stack model the decompiler walks; **`:1247` raises "no recorded signature"** |
| `3rd/rscache/src/cs2/cs2_command.{h,c}` | the opcode table's shape; read the header comment, it is prose |
| `3rd/rscache/src/cs2/cs2_command.gen.h` | the generated table (1106 lines) |
| `3rd/rscache/src/cs2/cs2_{cfa,dfa,ir,gen,names,types}.c` | control flow, data flow, IR, emit, naming |
| `tools/cs2_gen_opcodes/gen_opcodes.py` | generates the tables from vendored RuneStar `Opcodes.kt` |
| `tools/cs2_gen_opcodes/local_opcodes.py` | **local overrides** — `DECODE_OPERAND_OVERRIDES`, `HANDLER_OVERRIDES`, `LOCAL_NAMES`, `LOCAL_ALIASES` |
| `3rd/rscache/tools/cs2/main.c` | the `cs2` tool |
| `src/cs2vm2/` | the C client's runtime VM (separate from these tools; keep them consistent) |

Note the generator's source is **RuneStar's `Opcodes.kt`, vendored**. It is not
authoritative for rev 239 — that is precisely why opcodes are missing. New
signatures belong in `local_opcodes.py` so a vendor refresh does not lose them,
with a comment naming the evidence.

`cs2_command.h` also documents the opcodes whose signature is *not fixed*
(`db_getfield` pushes a whole column's fields; `db_find` pops a value whose
stack depends on the indexed field). Do not force those into a static
signature — there is already a mechanism for installing one at run time.

---

## 4. The oracle: the official client

**The signature of an opcode is whatever the official client's VM does with the
stack.** Do not infer it from the name.

### 4.1 As source

`Deobfuscator/instr/src/` (compilable) and
`Deobfuscator/src_osrs239_rl1_12_33/deob/` (readable, same class numbering).
The map, with provenance, is `Deobfuscator/instr/RENAMES.md`. The parts you
need:

| symbol | what |
| --- | --- |
| `Statics.method4464` | the CS2 interpreter loop — sets up locals from the ScriptEvent, runs fetch/decode/execute to op 21 |
| the inner dispatch at `Statics.java` ~19074 | per-opcode: bumps the counter, checks the budget, fetches, dispatches |
| `Statics.method6889` | second-level dispatch for opcodes **≥ 100** — a range ladder (`<1000`, `<1100`, `<1200`, …) into ~25 group handlers with signature `(ILbk;ZI)I` |
| `Statics.method6590` | the standard invoke wrapper: op budget 500000, warn at 475000 |
| `Statics.method4335` | `runScript` entry; resolves the script id |
| `Statics.method9011` | script load: 128-entry cache, miss = archive read + decode |
| `class43` | the decoded `Script` |

For an unknown opcode, follow `method6889`'s ladder to the group handler that
owns its range, then read the case. **The pops and pushes are literally the
stack operations in that case** — that is the signature, and it is not a guess.

### 4.2 As a running client

A recompiled, instrumented official client runs under RuneLite against the
mock239 server, logs in, and executes content. Procedure:
`Deobfuscator/instr/RUNNING.md` (executed, not aspirational). It has a control
channel on `127.0.0.1:43601` for screenshots, clicks and keys, and
`3draster/tools/perf/watchdog.sh` guards every launch.

Use it when reading the source is ambiguous: add telemetry to `method4464` under
a system property (off by default), print the stack around the opcode in
question, drive the client to a screen that runs the script, and read the answer
off the machine.

**Before touching that tree, read `Deobfuscator/instr/DEOB_DEFECTS.md`.** The
deobfuscation is not behaviour-preserving and the recompiled client only works
because ~11 classes of defect were repaired. `instr/build.sh` then
`instr/tools/verify_api.py` (must stay green) is the build.

---

## 5. Suggested order of work

1. **Re-measure** (§1). Numbers drift; the tool is cheap to run.
2. **Chase the 6-byte delta on one small script.** One root cause plausibly
   accounts for thousands of the 5,590 mismatches. Do this before anything else,
   because fixing it changes what every other number means.
3. **Re-measure.**
4. **Fill the 30 missing signatures** from the deob's group handlers, most
   frequent first (216, 8005, 6758, 6803, 2506). Put them in
   `local_opcodes.py` with a comment citing the handler you read.
5. **Re-measure**, then look again at the type/stack decompile errors — expect
   most to have disappeared.
6. **Attack the 770 same-length cases** by first-differing-byte.
7. Anything that still cannot be made exact gets a written reason naming the
   script id and the construct.

## 6. Rules

- **Never guess a signature.** Read the group handler. A wrong signature
  compiles, decompiles, and corrupts the stack model somewhere else — the
  failure surfaces far from the cause, which is exactly the pattern §2.3 shows.
- **Round-trip is the gate**, and it is a whole-corpus number. Report
  `exact` counts before and after every change; a fix that raises one family and
  lowers another is not progress unless the total moves.
- Do not regress `make -C 3rd/rscache test` (`cachepack-fidelity: all bars met`)
  or `tools/cs2_parity/`. Read `3rd/rscache/EXCEPTIONS.md` **before** touching
  any rscache write path.
- Keep `src/cs2vm2/` (the client's runtime VM) consistent with the tables. A
  signature that only the tools know is a future divergence.
- The C client has its own CS2 lessons already written down and they are worth
  reading before assuming a behaviour: `docs/cs2vm.md`,
  `docs/runclientscript.md`. Several opcodes were implemented wrong once and the
  notes say how it was caught.
- Prefer a generated table over a hand-edited one; if the generator cannot
  express something, extend the override tables rather than editing
  `cs2_command.gen.h` by hand.

## 8. What was found, 2026-08-05

`3080 → 8333` byte-exact. Nine changes, each measured on the whole corpus before
and after. The order below is the order they were found, and each number is the
whole-corpus `exact` count after that change alone.

| # | change | exact |
| --- | --- | ---: |
| — | baseline | 3080 |
| 1 | drop branches that land on the next instruction | **5779** |
| 2 | 14 opcode signatures read out of the rev-239 client | 5786 |
| 3 | discard a statement call's return values | 6047 |
| 4 | hook descriptors carry the stack type, not the fine type | **7536** |
| 5 | epilogue default is the declared type's, not always 0 | 7569 |
| 6 | switch: default body last, after every case | 7728 |
| 7 | markup `<...>` is its own string segment | **8291** |
| 8 | `db_find_with_count` discards its count | 8326 |
| 9 | eleven cc_* rows regain their active-component flag | **8333** |

Everything is in `3rd/rscache/src/cs2/cs2_compile.c` except #2 and #9, which are
`3rd/rscache/tools/cs2/local_commands.py`. Each carries its evidence as a
comment at the site.

**The doc's own file paths were stale.** §3 says new signatures go in
`tools/cs2_gen_opcodes/local_opcodes.py`; the file that actually feeds
`cs2_command.gen.h` is **`3rd/rscache/tools/cs2/local_commands.py`**, via
`3rd/rscache/tools/cs2/gen_cs2_tables.py`. The cs2vm2 side stays consistent by
re-running `src/cs2vm2/gen_opcode_stack.py`, which reads `cs2_command.gen.h` —
so one edit propagates to both, and both were regenerated.

### 8.1 The big one was not the big one

§2.1 said the pipeline was dropping whole instructions. It was **adding** them:
`cs2_cc_if` and `cs2_cc_switch` emitted a "jump to the end of the construct"
after *every* body, including the last one, where the target is the next
instruction and the offset resolves to 0. The official compiler does not.
Measured: 51,711 of the corpus's 51,716 unconditional branches have a non-zero
operand, and no conditional branch has a zero one at all. Removing them is a
fixpoint pass (`cs2_cc_drop_fallthrough_branches`) because a removal can shorten
another branch's span to zero in turn.

That one change was worth 2,699 scripts — as §2.1 predicted, just with the sign
flipped.

### 8.2 Reading signatures off the client, and the two ways to get it wrong

Fourteen opcodes now have signatures read from
`Deobfuscator/src_osrs239_rl1_12_33/deob/Statics.java`: 208, 216, 1707, 2506,
2624, 4127, 4223, 8002, 8005, 8007, 8010, 8011, 8014, 8015, 8020, 8026. Each
cites its group handler. Two traps, both of which produce a *plausible* wrong
answer:

- **the handler pops in its preamble.** `method4787` (2500–2599) and
  `method8067` (2600–2699) each pop a component id before looking at the
  opcode, so 2506 and 2624 take one argument their own cases never show.
- **the case pushes through a helper.** `Statics.method2627` pushes 0 or 1 on
  every path, so opcodes 210 and 217–221 push an int no idiom in their body
  shows. Every signature above was checked against a transitive closure of
  which methods in the tree touch a stack pointer.

Which stack is which is settled, not assumed: `Statics.method7522` kept its
exception strings — `"pushValueOfType() failure - unsupported type"` — and
switches on the base var type into `field258` (long), `field252` (**string**,
despite reading as `Object[]`) and `field254` (int). The full map and the
handler ladder are now in `Deobfuscator/instr/RENAMES.md`.

**Sixteen of the thirty missing opcodes have no signature to read.** 6758, 6803,
6859, 6951, 7043, 7451, 7600, 7627, 7800, 7803, 7804, 7805, 7807, 7813, 7814
and 7820 reach no handler in the rev-239 client: `method10020` (7600–7699) is a
bare `return 2`, and `method11128` (7700–7999) implements only 7900 and 7901.
`return 2` is what the interpreter turns into `IllegalStateException`. They are
absent from `src_osrs239`, `src_20260701/08/30` and `instr/src` too. The scripts
holding them cannot run under this client, so §4's oracle simply has no answer —
these need `cs2 infer-arity` or call-site balance, and infer-arity currently
calls most of them under-determined.

### 8.3 The residue — 1,131 scripts that recompile but not byte-for-byte

Classified by the first structural difference between the original and the
recompiled instruction stream. Percentages are of the 9,725-script corpus.

| count | what differs | why |
| ---: | --- | --- |
| 425 | one `push_constant_int` operand, `-1` vs `0` | §8.4 |
| 253 | one extra `branch` | §8.5 |
| 172 | one missing `branch` | §8.5 |
| 44 | extra `push_constant_int` + `return` | an epilogue the cache does not have; not yet chased |
| 29 | header or trailer bytes only, ops identical | not yet chased |
| 22 | missing `join_string` + `push_constant_string` | markup segmentation that #7 did not reach |
| 21 | missing a trailing `return` | the 32 scripts whose body ends in an explicit `return`, which the decompiler suppresses printing |
| 15 | `_4124` operand `1` vs `0` | §8.6 |
| 13 | missing `push_constant_string` | not yet chased |
| 126 | eleven smaller shapes, ≤ 11 scripts each | not yet chased |

Plus **202 that do not decompile** (46 still on a missing signature, the rest
type/stack errors downstream of one) and **67 that decompile but do not
recompile** — 39 a parse error in the emitted source, 16 a callback argument the
compiler cannot type, 7 an unterminated `(` in a callback, 4 over a fixed limit
on condition or switch size.

### 8.4 Why 425 scripts cannot be exact, specifically

A script's epilogue — the unreachable `push default; return` tail that is the
only record of its return types — pushes a value that depends on the *declared*
type. Measured by pairing every script's decompiled return types against the
constants its own epilogue pushes: `string` is `""` (432 of 432), plain `int` is
`0` (1792), and every narrower type is `-1` without exception (graphic 12, obj 8,
namedobj 6, struct 6, boolean 5, component 5, coord 4, enum 3, stat 1). Change #5
implements exactly that.

The 425 left are scripts whose epilogue is `-1` but whose signature the
decompiler prints as `int` — so the compiler emits `0`. **The rule is not
failing; the type is.** Script 3 is the whole shape of it: it returns literal
`1` and `0` from a `testbit`, nothing in the script constrains the type further,
and its callers are not visible to a one-script-at-a-time decompiler. The
epilogue says the declared type was a reference type; it does not say which one,
and there are eleven candidates that all default to `-1`.

Making these exact means either whole-program typing (type a proc's return from
its callers) or printing a fabricated type name that happens to default to `-1`.
The second would round-trip and would make the decompiled source assert
something about the program that is not known to be true, which is worse than a
wrong byte. Left as is, deliberately.

### 8.5 The 425 branch differences are one decision made without its evidence

`if (a) { return; } if (b) { … }` and `if (a) { return; } else if (b) { … }` are
the same program and different bytecode: the `else` form needs a jump over the
else from the end of the body, and the plain form needs none. Both shapes are in
the cache — script 73 has no jump, script 56 does — so the original says which,
and the decompiler has to read it rather than pick.

It nearly does. `cs2_reconstruct_block` already tracks whether the `if` side
rejoins (`after_if`), and the unreachable jump is what makes it rejoin. What
does not follow the evidence is the `else if` merge, which runs before that
check. Guarding the merge on `after_if` was tried: it moves the whole-corpus
number by **+1** (8325 → 8326) and costs 27 scripts of divergence against the
RuneStar reference corpus in `make -C 3rd/rscache test` — so it was reverted.
The real fix is further in: the short-circuit rewrite in `cs2_dfa.c` restructures
`&`/`|` conditions before this point and is what decides `after_if` for the cases
that matter. Not attempted.

### 8.6 One thing deliberately not done

Thirteen BASIC opcodes carry operand `1` somewhere in the cache while the table
says they have no active-component form, so the compiler writes `0`. Eleven are
in the cc_ range and are now marked dot-capable (#9) — for those the operand
byte *is* that flag, which is what the interpreter passes each group handler as
`var2`.

**4123 and 4124 are not**, though flipping them would recover 22 more scripts.
They sit outside the cc_ range and their cases in `method5814` never read
`var2` — the client ignores that byte entirely. Marking them dot-capable would
make the decompiler print `._4123`, asserting an active-component form that does
not exist, to win a byte. What that operand means for them is unknown, and
saying so is worth more than 22 scripts.

## 7. What "done" looks like

- `cs2 roundtrip --cache cache.osrs239 --rev osrs239` reports **exact** for
  every script that can be exact, with the residue enumerated and explained.
- Every new signature cites the official handler it came from.
- The whole-corpus number is in this document, updated, next to the 2026-08-05
  baseline so the delta is visible.
