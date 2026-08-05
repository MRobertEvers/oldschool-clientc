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

Baseline, `cache.osrs239`, 2026-08-05:

```
round-trip: 9507/9725 decompiled, 9440 compiled, 3850 same-length, 3080 exact
```

| | count | of 9725 |
| --- | ---: | ---: |
| decompiled at all | 9507 | 97.8% |
| **failed to decompile** | **218** | 2.2% |
| compiled back | 9440 | |
| same length as the original | 3850 | |
| **byte-exact** | **3080** | **31.7%** |

So roughly **two thirds of the corpus does not round-trip**, and that is the
number to move.

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

## 7. What "done" looks like

- `cs2 roundtrip --cache cache.osrs239 --rev osrs239` reports **exact** for
  every script that can be exact, with the residue enumerated and explained.
- Every new signature cites the official handler it came from.
- The whole-corpus number is in this document, updated, next to the 2026-08-05
  baseline so the delta is visible.
