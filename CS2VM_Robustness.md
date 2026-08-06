# CS2 de/re-compiler robustness

The goal is one sentence: **every clientscript in `cache.osrs239` survives the
full loop, byte for byte.**

```
binary dump  ──decode──▶  bytecode  ──decompile──▶  .cs2 source
     ▲                                                    │
     └──────encode──────  bytecode  ◀──compile────────────┘
```

Four stages, four gates, and a script only counts when it clears all four. The
machinery lives in `3rd/rscache/src/cs2/` (the language layer, part of the
`rscache` library) with `3rd/rscache/tools/cs2/` as the front end. Nothing here
is client code — the same library backs `cachepack`, so a script that will not
round-trip is a script `cachepack pack` ships as raw bytecode with only a
counter to say so.

## Stage gates

| # | Stage | Command | Measures |
|---|---|---|---|
| 1 | binary → bytecode → binary | `cs2 codec` | the container codec alone, no language layer |
| 2 | bytecode → source | `cs2 decompile` | the decompiler |
| 3 | source → bytecode | `cs2 roundtrip` (`compiled`) | the compiler accepts what the decompiler wrote |
| 4 | bytecode → binary, vs the cache's own bytes | `cs2 roundtrip` (`exact`) | the loop is lossless |
| 5 | source fixed point (A → B → C, `diff A C`) | see Procedures | meaning preserved where stage 4 is capped |

## Scoreboard — `cache.osrs239`, 9,745 scripts

| Stage | Baseline (2026-08-06) | Now | Bar |
|---|---|---|---|
| 1. codec byte-exact | 9,744 / 9,745 | **9,744 / 9,745** | 9,744 — met (D1) |
| 2. decompiled | 9,524 / 9,745 | **9,524** | 9,745 |
| 3. compiled | 9,457 / 9,524 | **9,468** | = stage 2 |
| 4. **byte-exact** | 3,086 / 9,457 — **32.6%** | **8,430 / 9,468 — 89.0%** | = stage 3 |
| — same length | 3,856 | 9,020 | |
| 5. source fixed point | 6,939 / 6,995 (99.2%, quoted) | **8,190 / 8,193 — 99.96%** | see caveat |

Baseline is the state at the start of this work, measured on this machine, not
quoted. Stage 4 is where the movement is: **+5,344 scripts**, from nine compiler
defects and one decompiler one.

Stage 5's comparison set is smaller than stage 3's because it is measured
through `--raw`, which has no cache to read param types or dbtable columns from;
1,256 of the 9,449 recompiled scripts will not re-decompile without them. That
is a limitation of the measurement, not of the compiler — see Procedures.

### Why this is not 100%, and what 100% would take

**Stage 4 is capped at about 8,900 by one missing bit per return slot** (D6),
and that is a property of the source language rather than of either half. 471 of
the 1,038 remaining differences — the largest bucket by four times — are a
script's epilogue pushing `-1` where the compiler emits `0`. The constant is the
default of the *declared* return type; the decompiler prints `int` both for a
slot it solved and for one it could not, and only the first has default `0`.

I tried to predict it from the source and measured that it cannot be: over 2,378
`int` return slots, "every value returned into this slot is `0`/`1`" agrees with
the epilogue constant **83% of the time** (1,973 / 2,378), with 296 slots
returning non-boolean values and still defaulting `-1`. That is a correlation,
not a rule, and installing it would produce confidently wrong bytes on a sixth
of the corpus.

Reaching 100% therefore needs the source to carry the bit — a language change,
described under D6 — and that decision belongs to whoever owns the content tree,
because every `.cs2` in it and the reference comparison are in scope.

A second cluster of about 170 scripts sits behind the same wall: where a
switch's `default` body *is* the epilogue, the decompiler cannot tell it from
real code without knowing the exact per-slot default (D15).

**Stage 2 is capped at 9,635** by 110 scripts on 31 opcodes with no signature.
`cs2 infer-arity` solved one more this session and reports the rest
under-determined; opcode 216, the largest at 26 scripts, has exactly two
surviving candidates that produce *different programs* (D18). EXCEPTIONS G4's
verdict stands: they need a client that implements them.

---

## TODO

Counts are scripts still differing, from the current run.

### Done

- [x] Build the `cs2` tool on Windows (mingw64) — it was Mach-O in the tree.
- [x] Split stage 1 into its own gate (`cs2 codec`).
- [x] `roundtrip --dump DIR` + `disassemble --raw --rev` so a byte difference can
      be read as instructions.
- [x] **D2** redundant `branch` to the next instruction — **+2,701**
- [x] **D4** hook descriptors carried type letters, not stack letters — **+1,489**
- [x] **D8** markup tags are their own string push — **+564**
- [x] **D3** missing `pop_*_discard` after a statement-position call — **+259**,
      and a real miscompile
- [x] **D7** switch default body belongs last, not first — **+159**
- [x] **D14** `else` vs two `if`s, recovered from the dead jump the DFA deletes
      — **+82**
- [x] **D16** db calls in statement position now get their discards — **+36**
- [x] **D5** epilogue default per return type (`-1`, not `0`) — **+33**
- [x] **D13** `enum(…)` / `*_param(…)` as a hook argument — **+11**, and the
      first thing to move stage 3 (9,457 → 9,468)
- [x] **D15** the bare `return` before the epilogue — **+9**, and a miscompile:
      a script falling out of its body returned nothing where its declared arity
      said otherwise
- [x] **D9** `else if` chain built even when the `if` never rejoins — **+1**

### Stage 4 — 1,038 scripts still differ

- [ ] **471** — **D6**, the epilogue's `int` ambiguity. *Blocked on a language
      decision, not on evidence.* Measured this session: not predictable from the
      source (83%, D17). Four times the next bucket.
- [ ] **~170** — a switch `default` body that *is* the epilogue: the extra break
      and the duplicated epilogue (D15's residue). Needs the exact per-slot
      default to be safe, so it is behind D6.
- [ ] **39** — the duplicated epilogue itself, same cause.
- [ ] **32** — jump targets still disagreeing with the same op sequence.
- [ ] **29** — same op sequence, same operands, different length.
- [ ] **23** — `push_constant_string` runs the cache splits and we do not: D8's
      residue, in `join_string` arguments rather than markup.
- [ ] **22** — `_4123` / `_4124` operand 0 against 1: the dot-form flag on two
      unnamed opcodes. Likely `dot_capable` missing from their table rows.
- [ ] **9** — the cache has a second `return` we do not emit.

### Stage 2 — 221 scripts do not decompile

- [ ] **110** blocked on 31 opcodes with no signature. **Opcode 216 alone is 26
      of them**, then 8005 (11), 6758 (9), 6803 (8), 2506 (7). `cs2 infer-arity`
      and the scoring pass are both exhausted on them — D18.
- [ ] **65** operand-stack shape disagreements on *signed* opcodes — a recorded
      signature has drifted from what rev 239 does. G4 names this and notes the
      same solver could be pointed at signed opcodes to find which. Nobody has,
      and it is the most promising unexplored lead in stage 2.
- [ ] **18** `return leaves K values, the script declares K`.
- [ ] **28** the rest: array element type (4), enum/hook descriptor bytes (5),
      gosub argument stack types (3), one flow graph with no single entry point,
      one callee absent from the cache.

### Stage 3 — 56 scripts do not compile

- [ ] **39** parse errors in the decompiled source — the generator emitting text
      its own parser rejects. Two shapes, both in script 465: a bare local as a
      statement (`$int0;`) and an array pointer printed without its `$`
      (`~script465(intarray0, …)`, an `EXPR_POINTER`). The second is a parser
      gap and should be fixed there, since the spelling is upstream's; the first
      is spurious output that `cs2_delete_nops` ought to have removed.
- [ ] **7** a callback string containing nested quotes:
      `if_setonclick("script2470(event_com, "B", $string0)", …)`. The lexer ends
      the outer literal at the inner quote. There is no escape in the dialect,
      so this needs one, or a quote-aware split.
- [ ] **5** callback arguments whose type still cannot be determined: an unnamed
      opcode (`_7253`), a `db_getfield` call, and `calc(…)` inside one.
- [ ] **4** switches over the 256-jump limit; **1** `tostring` arity.

### Blocking everything above it

- [ ] **Get the reference corpus.** `test_cs2` is the only external control on
      the decompiler and it cannot run here (**D10**). Two changes this session
      touched the decompiler (D9, D14); both are unvalidated against it.

---

## Procedures

### Build the tool (Windows)

The tree ships a Mach-O `cs2` binary; on Windows the build produces `cs2.exe`
beside it.

```sh
export PATH="/c/Users/mrobe/Documents/git_repos/oldschool-clientc/toolchain/mingw64/bin:$PATH"
cd 3rd/rscache/tools && mingw32-make CC=gcc cs2
# -> 3rd/rscache/tools/cs2/cs2.exe
```

`toolchain/mingw64` is the bundled compiler. There is no `cc`/`gcc` on the
default PATH and no WSL on this machine.

### The gates, in order

```sh
CS2=3rd/rscache/tools/cs2/cs2.exe
$CS2 codec     --cache cache.osrs239 --rev osrs239 --quiet      # stage 1
$CS2 decompile --cache cache.osrs239 --rev osrs239 --out A      # stage 2
$CS2 roundtrip --cache cache.osrs239 --rev osrs239 --quiet      # stages 3 + 4
```

`--names DIR` is legibility only. Verified, not assumed: with RuneStar's tables
loaded the run is identical in every column to the run without them. That is
EXCEPTIONS G5's claim, re-measured.

### The library's own suite

```sh
cd 3rd/rscache && mingw32-make CC=gcc test CACHE_ROOT="$(pwd)/../.."
```

`test_roundtrip`'s `cs2script` row is the codec's regression gate and reads
`records=1999 exact=1999 (100%)` — it samples 2,000 records, which is why stage
1 above is measured over all 9,745 instead.

The suite needed three Windows fixes to run at all: `test_membership` and
`test_pack` called POSIX `mkdir(path, mode)` and stopped the build partway
through, and `lc_pack.c` needed `tools/common` on its include path. The last
step (`test_cachepack_fidelity.sh`) still fails here because it shells out to
`make`, which is `mingw32-make` on this machine — unrelated to anything below.

### Read a stage-4 difference

A byte difference is a *bytecode* difference, so read it as instructions:

```sh
$CS2 roundtrip --cache cache.osrs239 --rev osrs239 --dump /tmp/rt
$CS2 disassemble --raw /tmp/rt/orig    --rev osrs239 92
$CS2 disassemble --raw /tmp/rt/rebuilt --rev osrs239 92
```

`--rev` on a `--raw` directory is what picks the trailer width; without it an
osrs239 dump reports "not in this cache" for every id, which reads as a missing
file rather than the wrong trailer.

`scratchpad/dis_diff.sh <id>` wraps the two calls in a `diff -u`. **Mind the
direction** — `-` lines are the cache, `+` lines are the rebuild. Reading it the
other way gives a plausible story that sends the fix backwards; it cost me ten
minutes on D4.

**The running-depth column is the tell.** It must return to 0 at the `return`; a
non-zero depth is a stack the compiler left unbalanced, not a cosmetic
difference. That is how D3 turned out to be a miscompile rather than a size
difference.

### Bucket the differences by size

```sh
grep -E "^DIFF [0-9]+: [0-9]+ bytes vs" roundtrip.log \
  | sed -E 's/.*: ([0-9]+) bytes vs ([0-9]+)/\1 \2/' \
  | awk '{print $1-$2}' | sort -n | uniq -c | sort -rn
```

Deltas cluster on instruction widths, which is why this is the first thing to
run: **3 bytes** is one INT8-operand instruction, **6 bytes** one INT32-operand
instruction. A bucket at exactly ±6×N is one instruction class appearing or
vanishing N times, and that names the bug before any script is opened. 2,148
scripts at exactly +6 was D2; 491 at −3 was D3.

### Bucket the differences by cause

`scratchpad/classify.py <dis_orig.txt> <dis_rebuilt.txt>` aligns the two
disassemblies, finds the first differing instruction in each script, and groups
by what kind of difference it is (opcode chosen, operand, string operand, jump
target) with four worked examples per group. Feed it bulk disassemblies:

```sh
grep -E "^DIFF [0-9]+" roundtrip.log | sed -E 's/DIFF ([0-9]+).*/\1/' > ids
$CS2 disassemble --raw /tmp/rt/orig    --rev osrs239 $(cat ids) > dis_orig.txt
$CS2 disassemble --raw /tmp/rt/rebuilt --rev osrs239 $(cat ids) > dis_rebuilt.txt
```

Batch the id list at ~800 per call: Windows refuses a longer command line, and
the failure is `[WinError 206] The filename or extension is too long`, which
does not mention the argument list.

### Bucket the differences by *structure* — the sharper instrument

`scratchpad/align.py` takes the same two bulk disassemblies and runs a sequence
diff over the opcode names, reporting the first structural edit — what the
rebuild has extra, or is missing — with the two instructions before it as
context, grouped and counted.

This is the one to reach for once `classify.py` stops separating things.
`classify.py` said "490 scripts: the first difference is a `branch` operand",
which is a symptom and names nothing. `align.py` said "roughly 300 scripts
differ by one unreachable `branch` after a `return`, and 132 of them have it in
the cache while 164 have it in the rebuild". **Both directions at once is the
tell**: a difference that goes both ways is not a rule the compiler is getting
wrong, it is information the source is failing to carry. That reading is what
produced D14.

### Bucket the stage-2 failures by cause

```sh
grep "^FAIL" decompile.log \
  | sed -E 's/^FAIL [0-9]+: script [0-9]+ (pc [0-9]+: )?//' \
  | sed -E 's/opcode [0-9]+/opcode N/; s/[0-9]+ values/K values/g' \
  | sort | uniq -c | sort -rn
```

And the unsigned opcodes specifically, by how many scripts each one costs:

```sh
grep -oE "opcode [0-9]+ has no recorded signature" decompile.log \
  | grep -oE "[0-9]+" | sort -n | uniq -c | sort -rn
```

The stage-3 equivalent is the same shape over `^COMPILE` lines from
`roundtrip.log`.

### Ask the cache a question directly

Several findings below came from scanning the cache's own instructions rather
than from reading either implementation. `scratchpad/descriptors.py` is the
pattern: disassemble every script, walk back from each `*_seton*` to the nearest
`push_constant_string`, count the characters. The answer — 27,063 `i`, 1,124 `s`,
1,159 `Y`, nothing else — settled D4 in one run.

`scratchpad/epilogue.py` does the same for return-type defaults, cross-tabbing
each declared type against the constant its epilogue pushes.

This is the most productive instrument in the set. Both implementations are
opinions; the cache is the record.

### The source fixed point (stage 5)

Byte-exactness is capped wherever the decompiler discards information; the fixed
point is not.

```sh
$CS2 decompile --cache cache.osrs239 --rev osrs239 --out A
$CS2 compile   --src A --rev osrs239 --out B      # B holds bytecode named by id
$CS2 decompile --raw B --rev osrs239 --out C
diff -r A C                                       # A == C is the bar
```

**Pass `--rev` to the compile step too.** Without it the encoder writes a legacy
trailer and every file in B fails to decode on the way back — 9,449 failures
that look like a compiler collapse and are a one-word argument error.

The measurement is weaker than it looks. `--raw B` has no cache, so the
decompiler gets no param config and no dbtable columns, and 1,301 scripts that
decompile fine from the cache will not decompile from B. Closing that means
letting one invocation take bytes from `--raw` and config from `--cache`, which
the tool cannot currently do.

### The control

`test_cs2` decompiles RuneStar's own 7,884-script dump and diffs against the
output their Kotlin implementation produced. It is the only external check on
the decompiler. **It does not run here** — see D10.

---

## Discoveries

### D1. Archive 0 of table 12 is not a script

`cs2 codec` reports 9,744 of 9,745 decoded and all 9,744 byte-exact. The miss is
archive 0, and it holds **two bytes: `00 09`**.

Measured with `cs2 codec --dump`, which now keeps the payload even when the
decode fails — it used to keep the bytes only on success, so the one archive
worth looking at was the one with nothing to look at.

Two bytes cannot be a script: the trailer alone is 13 bytes before any
instruction (`op_count` u4, three or five u2 counts, a switch-table count), and
`op_count` must be ≥ 1. The reference table lists the id, so the enumeration is
right; the archive simply is not a clientscript. **Stage 1's bar is 9,744, not
9,745**, and it is met.

This also settles what the later stages mean. With the codec at 100%, a stage-4
miss is a language-layer defect and cannot be anything else.

### D2. The compiler emitted a jump to the next instruction — +2,701

Found by bucketing the length deltas: 2,148 scripts differed by exactly +6
bytes, 912 by +12, 474 by +18 — a run of multiples of one INT32-operand
instruction. Disassembling both sides of the smallest (script 2155, 50 bytes
against 56) named it in six lines:

```
  cache                             rebuilt
    3      6 branch          1         3      6 branch          2
    4     40 gosub_with_params 923     4     40 gosub_with_params 923
    5     21 return                    5      6 branch          0     <-- this
                                       6     21 return
```

`cs2_cc_if` emits the jump over the `else` before it knows whether an `else`
follows, so an `if` with no `else` ended in `branch 0` — six bytes that jump to
the instruction already next. Jagex's compiler does not emit it.

Fixed as a post-pass over the finished instruction list
(`cs2_cc_drop_redundant_branches`) rather than by not emitting the jump: the
same redundant jump falls out of `while`, `switch` and every nesting of them,
and one rule over the final listing cannot get out of step with the parser the
way four call sites can. It converts every jump and switch-case target to
absolute, deletes, remaps, and converts back; iterated to a fixed point, because
deleting one branch can leave the branch before it pointing at the new next
instruction.

### D3. A statement-position call left its result on the stack — +259

Unlike the rest of these, a *miscompile*.

The next bucket after D2 was 491 scripts exactly 3 bytes short — one
INT8-operand instruction. Script 92, four instructions against three:

```
  cache                                  rebuilt
    0     33 push_int_local        +1      0     33 push_int_local     +1
    1     40 gosub_with_params     +1      1     40 gosub_with_params  +1
    2     38 pop_int_discard       +0      2     21 return             +1   <-- depth
    3     21 return                +0
```

`~script486($int0);` as a statement still pushes the proc's return value, and
CS2 has no implicit drop. The rebuilt script returns with a value stranded on
the operand stack — the `+1` in the depth column. Every argument of every
statement after it sits one slot off from where it was written. The byte count
was the symptom; the stranded value was the defect, and nothing in the project
was checking for it.

`cs2_cc_emit_call_discards` now reads the result shape off the statement's
top-level call — the last instruction emitted, since everything before it is
arguments the call consumed — and emits one discard per value, in reverse of
push order because values come off top-first. A proc's shape comes from
`RSCache_CS2_ScriptReturnTypes` on the callee; a command's from its def
prototypes.

The `enum`, `param` and db families deliberately emit nothing: their result
shape is a property of an operand, not of the opcode (EXCEPTIONS G8), and a
wrong discard count desynchronises the stack exactly the way the missing one
did. That is the remaining 33-script `db_find*` bucket.

### D4. Hook descriptors carry the stack letter, not the type letter — +1,489

The largest same-length bucket: 1,507 scripts where the descriptor string
differed by case or by letter — the cache holding `"ii"` where the compiler
wrote `"Ii"`, `"iis"` against `"izs"`, `"iiiii"` against `"Ii11i"`.

Settled by asking the cache rather than either implementation
(`scratchpad/descriptors.py`): over every `*_seton*` instruction in
cache.osrs239, the descriptors contain **27,063 `i`, 1,124 `s`, 1,159 `Y`, and
nothing else**. Not one type letter in the whole cache.

EXCEPTIONS G7 has the same fact from the other direction — OldSchool stopped
putting the real type in hook descriptors and started writing `i`, which is why
the decompiler must not freeze a callee's parameter to `int` on the strength of
one. G9 drew the operational conclusion: the letter's only job is to name the
stack. The compiler had simply never been changed to match, and writing the more
informative letter is what a careful person would do.

`cs2_cc_descriptor_letter` is now the one place that decides, and it answers `i`
or `s`.

A pre-237 cache does write the type letter, and there is none in this tree to
measure against, so no revision threshold is invented — the position B6 takes on
the trailer width, for the same reason. If one is added,
`RSCache_ClientScriptFlags`'s `RevisionAtLeastOsrs(…, 237, …)` gate is where the
era switch belongs.

### D5. The epilogue's default is `-1`, not `0` — +33

A script's epilogue is one default per declared return type, then a `return`. It
is unreachable; it exists so the arity and stack shape can be read back off the
bytecode. The constants are still part of the record.

Cross-tabbing each declared return type against the constant its epilogue
actually pushes (`scratchpad/epilogue.py`):

| slots | type | pushes |
|---|---|---|
| 1,791 | `int` | `0` |
| 587 | `int` | `-1` |
| 436 | `string` | `""` |
| 50 | `graphic` `obj` `namedobj` `struct` `boolean` `component` `coord` `enum` `stat` | `-1` |
| 1 | `obj` | `0` |

So the default is `0` for `int` and `-1` for everything else on the int stack.
`RSCache_CS2_TypeEpilogueDefault` now says so; the compiler emitted `0` for all
of them before.

### D6. The `int` half of D5 is information the source does not carry — 471 open

The largest remaining bucket by four times, and the one that decides whether
100% is reachable. No amount of further measurement will settle it, because the
ambiguity is not in the cache. It is in the source text.

The decompiler prints `int` for two different things: a return slot it solved to
`int`, and a return slot it could not solve at all (EXCEPTIONS G7 — `int` in a
modern descriptor "is no longer a claim, it is the absence of one"). The
epilogue constant distinguishes them and the printed type does not.

What the slots look like is suggestive. Sampling the affected scripts, the
returns are almost all bare `0` and `1` literals:

```
--- 100 (slot 0) : ['int']          --- 1260 (slot 0) : ['int']
    return(1);                          return(1);
    return(1);                          return(0);
    return(0);                          return(1);
```

Those look like `boolean` returns the solver had no evidence for — script 1972
returns `on_mobile` in the same shape and *is* typed `boolean`, because a command
signature said so.

**So I measured whether that reading is a rule, and it is not** (D17). Two
designs remain, neither free:

1. **Teach the solver.** Feed the epilogue's default in as a constraint — "this
   slot's type is not `int`" — and let usage resolve the rest. The round trip is
   a real oracle: a wrong guess stays non-exact, so it cannot make anything
   worse. It changes the decompiler's output, which is what D10 makes
   unverifiable, and D17 says usage will not resolve most of them anyway.
2. **Carry it in the source.** One bit per int return slot, written somewhere in
   the signature. Lossless and dull, and it changes the language, which puts
   every `.cs2` in the content tree and the reference comparison in scope.

Design 2 is the only one that reaches 100%, and it is a decision rather than a
discovery. It also unblocks about 170 more scripts (D15's residue), because
knowing the exact per-slot default is what would let the decompiler tell a
switch `default` body that *is* the epilogue from one that is real code.

### D7. The switch default body belongs last — +159

Four hundred scripts had a `branch` in the cache where the rebuild had a value
push, at the same address.

The compiler put the default body immediately after the SWITCH — where control
falls through to, which is the reading that makes sense — with a jump over it to
the case bodies. The cache does the opposite:

```
    switch
    branch  -> D            the no-match path
    case body 0 ; branch -> END
    case body 1 ; branch -> END
    ...
 D: default body            last, and it falls through
 END:
```

Script 110 settles the shape, because its no-match path and its break path have
different destinations: `branch +18` after the SWITCH lands on the default body
at 21, while every case body's terminating branch lands on 23, the statement
after the switch. A default body compiled first cannot produce those two
addresses.

The gain was smaller than the bucket because of a second, separate shape
difference: where a switch has no default and simply falls through to the
statement after it, the decompiler renders that statement as `case default :`.
The two are indistinguishable in the bytecode *when nothing follows the switch*,
and they compile differently. Script 660 is the example.

### D8. A markup tag is its own string push — +564

`"Blue token:<br>"` is **two** pushes in cache.osrs239, not one.
`"<col=ff9040>Lever</col>"` is three. Script 8 builds its message from 22 pieces
where merging each tag into its neighbouring text gives 15 — the rebuild came
out 90 instructions shorter than the cache's own.

`<...>` is ambiguous in this language: string interpolation, and the game's own
markup, told apart by trying to parse it as an expression (EXCEPTIONS G2 records
a bug in the rollback). On the markup reading the compiler continued the literal
run *through* the tag, which is right about the text — the client concatenates
and renders the lot — and wrong about the instructions.

The literal run before a tag, the tag itself, and each interpolated expression
are now one stack value each.

### D9. The `else if` chain was built even when the `if` never rejoins — +1

`cs2_reconstruct_block` already flattens a plain `else` when the `if` side never
rejoins — "printing it as an else would add a level for nothing" — and did not
apply the same rule to a chain, so `if (a) { return; } else if (b) { … }` came
out where the cache holds two separate `if`s. The shapes compile differently:
the chain needs a jump past the else that two `if`s do not, and it is
unreachable either way, so the cache's own bytes are the only evidence for which
the source was. Building the chain unconditionally threw that evidence away.

Worth recording is that it is nearly worthless as a fidelity fix: **+1 exact, +7
same-length, 45 decompiled files changed**. It was kept because two arms of one
decision disagreeing is a defect on its own terms, not because it moved the
number. It is also the only change in this document that touches the decompiler.

### D10. The reference control cannot run on this machine

`test_cs2` is the decompiler's only external check: it decompiles RuneStar's
`input/` dump and compares byte for byte against the output their implementation
produced. EXCEPTIONS G1 records it as the regression signal for every change to
this layer — 6,489 identical, 2 different, held across the client's opcode
table, 41 inferred arities, a new type and three relaxations of upstream
strictness.

`git clone https://github.com/RuneStar/cs2` gets the name tables. It does **not**
get the corpus: `input/` and `scripts/` are in that repo's `.gitignore`, so
neither the inputs nor the expected outputs are in the tree. `test_cs2` finds no
fixture and reports `SKIP`.

Everything in this document except D9 is compiler-side and outside what
`test_cs2` measures. D9 is not, and is unvalidated.

### D11. Tooling gaps that hid all of the above

Four, and each one made a real defect unreadable:

- **The tool did not build on Windows.** `tools/cs2/main.c` called POSIX
  `mkdir(path, mode)`; `tools/common/tool_posix_compat.h` has existed for this
  since the cachepack work but `cs2` never included it. The checked-in `cs2`
  binary is Mach-O, so the tool had only ever been run from a Mac. The same held
  for `test_membership` and `test_pack`, which stopped `make test` dead partway
  through the suite.
- **`disassemble --raw` had no way to say which era a dump came from.** The
  trailer width comes from a profile and `--raw` has no profile, so it defaulted
  to legacy — and osrs239 is modern. Every id in a `roundtrip --dump` directory
  reported "script N is not in this cache", which reads as a missing file.
- **Nothing dumped the two sides of a difference.** `roundtrip` printed
  `DIFF 92: 23 bytes vs 26` and stopped.
- **`store_load` discarded the bytes of anything that failed to decode**, so the
  one archive that needed diagnosing was the one with nothing to diagnose (D1).

This is the same shape as EXCEPTIONS G9 — "the compiler was measured by a gate
that measured nothing". The gate here reported a number; reading *why* the number
was what it was needed four things that did not exist.

### D12. The last switch break — a measured wrong turn

Recorded because the evidence looks conclusive and is not, and the next reader
will find the same two scripts.

Script 8950 has one case body, ending in `return`, and **no** break jump after
it. Script 660 has thirteen case bodies, all ending in `return`, and **twelve**
break jumps — one between each pair, none after the last. Both say: omit the
break after the final case body when that body returned.

Implementing exactly that moved byte-exact **8,292 → 8,233** while same-length
went 8,804 → 8,858. So it fixed the lengths of some scripts and broke the bytes
of 59 that were already exact — meaning there are scripts whose last case body
returns and whose final break the cache *does* carry.

What distinguishes them is not deadness (660's other twelve breaks are equally
dead and all present) and not whether a default body exists (8950 has one). It
may be that the decompiler's case order differs from the cache's layout order
for those 59, which would make "the last case in the source" a different body
from "the last case in the bytecode". Not established. Reverted, and the reason
is a comment in `cs2_cc_switch` so it is not rediscovered.

### D13. `enum` and `*_param` as hook arguments — +11, and stage 3 finally moved

A hook needs one descriptor letter per callback argument, derived from the
argument expression. Two commands defeated it, and both for the reason
EXCEPTIONS G8 gives about the db family: **what they push is named by an
operand, not by the opcode**, so the generated table carries no result
prototype. `enum(int, string, $enum6, $int8)` and
`struct_param($struct0, param_1279)` were refused, and 26 scripts with them.

Both answers are to hand. `enum`'s result type is its second argument, a type
literal sitting in the source two tokens ahead. A `*_param`'s is the param
config's, reachable through the same `param_types` provider the decompiler uses
— and the id is either a name or the `param_<id>` spelling a names-less
decompile writes, so both resolve.

The lexer is one token wide, so `cs2_cc_call_argument_word` reads the argument
list as text. It accepts only a bare word followed by an argument boundary and
refuses anything else, because a half-read expression would be worse than
declining.

This is the first change here to move stage 3 at all: 9,457 → 9,468 compiled.
Five callback arguments still cannot be typed, and each needs something real —
an unnamed opcode's signature, or the db column provider.

---

### D14. `else` versus two `if`s, recovered from a jump that is deleted — +82

`if (a) { return; } else if (b) { … }` and `if (a) { return; } if (b) { … }` run
identically and compile differently: the first needs a jump past the else, the
second does not. The jump is unreachable either way — the body returned — so
nothing at run time can tell, and `cs2_remove_dead_code` deletes it before the
flow graph is built. Both spellings then reconstruct from identical reachable
graphs, and the decompiler picked one.

The cache is the only evidence for which the source was, and it holds both:
before this, 132 scripts had a jump we did not emit and 164 had one we emitted
and the cache did not.

`RSCache_CS2_Insn::dead_goto_follows` is that jump, recorded on the `return` as
the dead-code pass removes it. `cs2_reconstruct_block` reads it to decide
whether there is an `else` at all, in one place that both the plain-`else` and
the `else if` arms consult — they disagreed before, which is what D9 was.

All the "cache has extra branch" buckets went to zero. The "rebuild has extra"
ones did not, and they turned out to be D15's shape rather than this one.

### D15. The bare `return` before the epilogue — +9, and a miscompile

Every script ends with an epilogue: one default per declared return type, then a
`return`. The compiler emitted a bare `return` *first* whenever the body did not
end in one.

For a script with no declared returns that is right — the epilogue is that bare
return. For one that has them it is wrong twice over. The extra instruction
returns with an empty stack where the declared arity says otherwise, which is a
miscompile; and it displaced the epilogue by one, so every `break` in a trailing
`switch` aimed at the wrong address. Script 376 is the smallest example: 13
instructions in the cache, 15 here, and its switch is the whole body.

The epilogue is now the only thing emitted there, unconditionally.

**What this did not fix**, and it is the second-largest bucket in the file: when
a switch has no `default` in the cache, control falls through to the epilogue —
so the epilogue *is* reachable, and the decompiler renders it as
`case default : return(0)`. Compiling that emits a default body *and* an
epilogue.

I tried teaching the decompiler that the trailing block is never source
(`cs2_block_is_epilogue`). It cost 106 byte-exact scripts, and the reason is
worth recording: the flow graph knows only the *stack* type of a return slot, so
the check had to accept either `0` or `-1` for an int — and that means a body
genuinely ending `return(0)` for a `component` gets replaced by `return(-1)`.
Not a byte difference, a **behaviour** difference. Reverted. Doing it safely
needs the exact declared type, which is D6.

### D16. db calls in statement position — +36

`db_find_with_count(335872, $int0, 0);` as a statement pushes a value the cache
discards and the compiler did not — the same defect as D3, left open there
because a db call's result shape "is a property of an operand".

It is, and the operand is available. `RSCache_CS2_CompileOptions.db_columns` now
carries the same provider the decompiler is given
(`tools/common/cs2_db_columns.c`, so there is still one reading of what a column
holds). `db_getfield` resolves the `dbcolumn` literal two instructions back and
drops one value per field — four for a whole-tuple read, one for a single field.

`db_find` and its siblings push one int whatever the column is, so they need no
operand at all, which is what the first cut got wrong: it demanded a literal in
the second argument slot, and for the find family the column is the *first*. The
fix measured +0 until that was noticed, which is a good argument for checking
that a change moved the number rather than assuming it did.

`enum` and `param` remain uncovered here, their shape being an argument the
finished instruction list no longer shows.

### D17. The epilogue default is not predictable from the source — measured

D6 turns on whether the missing bit can be recovered from what the source *does*
say. The best candidate: an `int` return slot defaults to `-1` when every value
the script returns into it is `0` or `1` — i.e. it was really a `boolean`.

Cross-tabulated over every `int` return slot in cache.osrs239
(`scratchpad/epi_predicate.py`):

| all returns are 0/1 | epilogue | slots |
|---|---|---|
| no | `0` | 1,682 |
| no | `-1` | 296 |
| yes | `-1` | 291 |
| yes | `0` | 109 |

**1,973 of 2,378 — 83%.** The 296 that return non-boolean values and still
default `-1` are not noise, and the 109 the other way are not either. Installing
this would put confidently wrong bytes in a sixth of the corpus, which is the one
failure mode this layer exists to refuse.

So D6 is not solvable by inference. Recorded so the next reader does not spend
the afternoon I did on it.

### D18. Opcode 216 has two candidates and they are different programs

216 is the largest single stage-2 blocker: 26 scripts. `cs2 infer-arity` reports
it under-determined with two surviving candidates, so I ran the README's third
method — install each with `--override`, decompile only the scripts that use it,
and count.

```
opcode 216: 26 witness scripts
    23/26  --override 216:3,0,1,0
    23/26  --override 216:2,0,0,0
```

An exact tie, and the two are **not output-equivalent**, which is the precedent
for taking the smaller of a tie:

```
216:2,0,0,0   ->        _216($int21, 0);
216:3,0,1,0   ->  $int27 = _216(2320, $int21, 0);
```

Same net stack effect, different programs: one consumes a preceding constant and
yields a value, the other leaves that constant to whatever came before. Choosing
either is guessing which, and a wrong pop count is the error that produces a
confident listing of a different program.

The same run solved opcode 7627 uniquely (`in 0/0 out 0/0`, one witness) and
found the remaining 23 either under-determined or blocked by a second problem in
every witness. `infer-arity` and scoring are both exhausted on this cache;
EXCEPTIONS G4's conclusion stands — they need a client that implements them.


## Decisions

### Stage 1 gets its own gate

`roundtrip` conflates the codec, the decompiler and the compiler into one
`exact` figure. Splitting stage 1 off (`cs2 codec`) makes the later numbers mean
something: with stage 1 at 9,744/9,744, a stage-4 miss is a language-layer
problem and cannot be anything else. It cost about eighty lines and it is what
let D2 through D8 be attributed with confidence rather than argued about.

### The peephole is a post-pass, not four call-site fixes

`cs2_cc_if` would have to look ahead past a whole parsed block to know whether to
emit its jump. `cs2_cc_while` and `cs2_cc_switch` have their own copies of the
same jump. A single rule over the finished instruction list — *delete an
unconditional branch whose target is the next instruction* — covers all of them,
is checkable by inspection, and cannot disagree with the parser about what was
emitted.

### Unknown result shapes emit no discard

D3's fix stops at `enum`, `param` and the db families. Their push counts depend
on an operand — `db_getfield` on a four-field column pushes four values, on a
one-field column, one (EXCEPTIONS G8). Emitting "probably one" would produce a
plausible listing of a different program, which is the failure mode this whole
layer exists to refuse. 33 scripts stay short and are counted.

Note D13 does *not* contradict this. Reading `enum`'s result type off its own
type-literal argument is reading the operand, not guessing at it.

### No era threshold is invented for D4

Every descriptor character in cache.osrs239 is `i`, `s` or `Y`, and that is the
era this repo targets. A pre-237 cache writes the type letter, and there is none
in this tree to measure against. B6 already refuses to guess a revision
threshold for the trailer width on exactly this reasoning — "guessing one would
silently break script decode on whichever side of the guess is wrong" — so the
compiler writes the stack letter unconditionally and the comment says where the
switch belongs when a cache exists to verify it against.

### A change that costs more than it earns is reverted, and the measurement kept

Three times: the unreachable jump after a returning `if` body (−25), the last
switch break (−59, D12), and treating the trailing block as never-source
(−106, D15). All three looked obviously right, and one of them would have
changed behaviour rather than only bytes. Each is now a comment in the code with
its number attached, so the next person tries something else. The first is what
led to D9 and then to D14.

### The cache is the arbiter, both implementations are opinions

Four of the findings here (D4, D7, D8, D14) were settled by scanning
cache.osrs239's own instructions rather than by reading this code or RuneStar's.
`scratchpad/descriptors.py` and `epilogue.py` are the pattern and both fit on a
page. Where the two implementations disagree with each other there is no way to
tell who is right; where either disagrees with the cache, the cache wins.

### Nothing is installed on a tie

Opcode 216 would be worth 26 scripts and its two candidate arities score
identically. `enum` and `param` result shapes in statement position would be
worth a few more and are not recoverable from the finished instruction list.
D6's `boolean` reading would be worth 471 and is right 83% of the time. All
three are left undone, and the reason is the same one the README gives: a wrong
pop count or a wrong default does not fail, it produces a plausible listing of a
different program. 83% is not a rule.

### Measure, don't quote

Every number in this file was produced by running the gate on this machine.
`tools/README.md` and `EXCEPTIONS.md` describe a cache of 9,725 scripts; the
`cache.osrs239` in this tree has **9,745**. Both documents are right about the
cache they were written against — which is exactly why the baseline row was
re-measured rather than copied.

---

## Log

**Research.** Read `docs/cs2vm.md`, `3rd/rscache/README.md` §CS2,
`3rd/rscache/tools/README.md` §cs2, and `EXCEPTIONS.md` §G (G1–G10). Prior work
is well recorded and the standing gaps are named: G2 (byte-exactness capped by
`else`-collapse; source fixed point 99.2%), G3/G9 (hook descriptors, resolved),
G4 (unsigned opcodes), G5 (name tables optional), G7 (hook argument types went
untyped in modern OSRS), G8 (db stack shapes), G10 (two opcodes whose arity
differs between eras, open).

The claim I most wanted to check was G2's, because it is the one that says this
task's target is unreachable: "byte-exactness is limited by information the
decompiler deliberately discards". It is true, and it is small. The figure it
was defending — 2,877 of 6,614 — was not mostly `else`-collapse. It was six
mechanical compiler bugs sitting on top of it, and one of them (D3) was emitting
stack-unbalanced code.

**Build.** The tool did not compile on Windows (D11). Fixed by including the
compat header that already existed for the purpose. Built with the bundled
`toolchain/mingw64`.

**Baseline.** Ran all four gates. Stage 1 came back 9,744/9,745 exact, which made
the rest tractable: everything below is the language layer. Stage 4 at 32.6% was
far worse than G2's cap could explain, so I went looking for mechanical causes
rather than accepting the documented one.

**D2, D3.** Bucketing the deltas by size was the whole trick — a spike at exactly
+6 with a tail at +12, +18, +24 is not a diffuse quality problem, it is one
instruction. Then the same method on the new largest bucket. D3 turned up a
defect that byte-exactness was only the symptom of, which is worth carrying
forward: the round trip is not only a fidelity measure, it is the only
end-to-end check on the compiler's stack discipline that exists.

32.6% → 63.9%.

**D4.** The same-length bucket needed a different instrument, so I wrote
`classify.py` to align the two disassemblies and group the first differing
instruction by kind. 1,341 hook-descriptor case differences fell straight out,
and `descriptors.py` settled the rule against the cache in one run rather than
against either implementation. I had the direction backwards for about ten
minutes — reading a `diff -u` and taking `-` lines for the rebuild — which is
worth admitting because the wrong direction is a *plausible* story (the compiler
losing type information) and it would have sent the fix the wrong way.

63.9% → 79.7%.

**D5, D6.** `epilogue.py` cross-tabbed the declared return type against the
constant. The named types were unambiguous and are fixed; `int` was not, and D6
is the write-up of why more measurement will not help. This is the one place
where the honest answer is that the source language is lossy, and the fix is a
decision rather than a discovery.

**D7, D8.** Two shape rules, both read off the cache: the default body goes last,
and a markup tag is its own push. D8 is the larger and the less obvious — a tag
*is* text, so merging it into the surrounding run is right about meaning and
wrong about instructions.

79.7% → 87.7%.

**D9, and two reverts.** I tried the obvious compiler-side fix for the remaining
jump-target differences first and it lost 25 scripts, which is the useful kind of
negative result: it proved the difference is about `else`, not about `if`, and
pointed at the reconstruction. The reconstruction fix is right and earns almost
nothing (+1), and I have said so rather than filing it under the wins. D12 is the
second revert, and its evidence was more convincing than D9's — two scripts that
agree exactly and are still not a rule.

**D13.** Stage 3 had not moved all session, so I went at it directly. The
hook-argument type failures split into two commands whose result shape is in an
operand, and both operands are readable. 9,457 → 9,468 compiled.

---

**Second pass: to 89%.** Picking up at 8,303 with an instruction to get to 100%.

The instrument that made the difference was `scratchpad/align.py` — instead of
reporting the first differing *byte position*, run a sequence diff over the two
op listings and report the first structural edit with its context. That turned
"490 jump targets disagree" into "roughly 300 scripts differ by one unreachable
`branch` after a `return`, and it goes both ways". Both ways is the tell: it is
not a rule about when to emit a jump, it is information the source is failing to
carry.

**D14** followed from that in about twenty minutes once the question was right.
The jump is deleted by the dead-code pass, so record its existence on the
`return` before deleting it. All the "cache has extra branch" buckets went to
zero.

**D15** is the one I would flag to a reviewer. Chasing the buckets that did *not*
go to zero led to script 376, where the compiler emitted a bare `return` before
the epilogue — a script falling out of its body returned nothing where its
declared arity said two values. That is a miscompile of the same family as D3,
and again the round trip found it while looking for something else.

The other half of 376 — a switch whose `default` body *is* the epilogue — I could
not fix safely, and the attempt is written up because the failure is instructive:
the flow graph knows a return slot's stack but not its type, so the check had to
accept `0` or `-1` interchangeably, and that turns a body ending `return(0)` into
`return(-1)`. It cost 106 scripts and would have changed behaviour. Reverted.

**D16** cost more time than it should have. The fix measured +0, which I nearly
filed as "the plumbing does not help"; it was a wrong argument index for the find
family, and the second attempt was +36. Check that a change moved the number.

**D17 and D18** are both negative results, and both are the point. D6 is the
whole remaining gap and I wanted it badly enough to test the `boolean` reading
properly rather than install it and watch the number go up on the scripts it
happened to fit. 83% is not a rule. Opcode 216's two arities score 23/26 each and
produce different programs; there is no honest way to pick.

**Where it stands.** Stage 4 at 89.0% of what compiles, from 32.6% at the start
of the first pass. Stage 5's source fixed point at 99.96%. Stage 3 moved for the
first time. The library suite is green, `test_roundtrip`'s `cs2script` row still
100%, and the codec gate over all 9,745 records still 9,744.

**100% is reachable and it is not free.** The residue is no longer a collection
of bugs — it is one missing bit per return slot, measured and cornered, plus 110
scripts on opcodes nobody has a client for. The first is a language decision; the
second is EXCEPTIONS G4's standing conclusion. Everything either side of them
that I could find and prove, I fixed.
