# CS2 load-time JIT and its platform cache

Status: 2026-11-22. The VM side (sections 2 and 3, the VM tests of section 6)
is implemented in `src/cs2vm2`, and the platform cache (section 4, the host
and platform tests of section 6) in `src/engine/cs2_jit_cache.*`,
`src/engine/task_cs2_jit_pack.*`, the two IO executors and the App
(uncommitted, branch `cs2-jit`). Section 4.12 lists where the implementation
differs from the plan below. The interface the two implementers share is [`src/cs2vm2/cs2vm2_jit.h`](../src/cs2vm2/cs2vm2_jit.h);
where this page and that header disagree, the header wins and this page is
corrected.

## The rulings this follows

- The VM is single threaded and does no IO. IO happens when the VM yields to
  the host, the host yields to the platform, and the platform does it.
- The host/VM boundary stays. `CS2VM2_Ask` is the VM's only door to the host;
  the jit adds no host request and calls none.
- 2026-11-22: "During script loading, we could run an optimization pass, that is
  fine, but the packing of the script should be as it appears in the cache."
  So: an in-memory form built at load is allowed. The script's opcodes,
  operands, strings and switch tables stay as decoded (the existing dialect
  remap at decode is unchanged). Nothing ships rewritten bytecode in the cache,
  and cachepack emits nothing for this.
- 2026-11-22: "the jit should use the platform to cache its jitted scripts." The
  jitted form is serialized, persisted through the IO queue seam
  (`platform_io.h`: `PlatformX_*` on every native lane, `PlatformWeb_*` in the
  browser), and loaded back on a later run.
- "Make sure to profile the wasm build as well." Wasm numbers are part of done
  (section 7).

## 1. What is already measured

Measured at v3 c62dc8bb8 on the bank build (scripts 276 x2 + 274 x1 per bank
open, 567k opcodes, 88k host crossings, about 2.7 ms on the Mac, 64% VM side),
with the record/replay rig in the session scratchpad (`vmlab/`):

| change | VM side (replay) | real bank build |
| --- | --- | --- |
| fused-opcode overlay, 24 shapes, one byte per pc | -13.6% | -5.8% |
| overlay + `PUSH_CONSTANT_STRING` pushed by pointer in the hot loop | -25.8% | -16.3% (2.72 -> 2.28 ms) |
| int runs translated to register instructions | +13.5% (slower) | not built |
| frame int locals pre-sized to the declared count at push | neutral alone | |

The register form is out on the measurement: runs between non-int opcodes are
2.7 opcodes long, so a region is 1.15 instructions and the generic operand
decode costs more than the dispatches it saves. Do not build it.

Size of the working set, counted from the recording's `scripts.bin`: the bank
cycles touch 241 scripts, 27,308 opcodes in total, median 49 opcodes, largest
1,883. An overlay is one byte per opcode, so this working set's overlays are
about 27 KB plus a 48-byte header each (about 39 KB serialized).

## 2. The jitted form (v1)

### 2.1 The overlay

`struct CS2VM2_Jit` (VM-private, `cs2vm2_jit_internal.h`) holds one `uint8_t`
code per pc, `code[0 .. op_count)`, allocated in one `ToriMem_Script` block
with the struct. The code at a pc says what the hot loop runs when it reaches
that pc. Inner pcs of a fused group keep their own code, so a branch into the
middle of a group lands on a valid entry; the overlay never depends on how a pc
was reached.

The numbering is part of the blob format and frozen under
`CS2VM2_JIT_FORMAT_VERSION` (2 since the long compares and `BRANCH_IF_ONE`
joined the branch-range refusal; 1 checked only the hot branches and `SWITCH`):

| codes | meaning |
| --- | --- |
| 0 | general path (`CS2VM2_RunOp`). Always legal at any pc. |
| 1 .. 31 | the plain hot instruction: exactly `g_cs2vm2_hot_op[opcode]`, today's `CS2VM2_HOT_*` numbering, moved unchanged from `cs2vm2.c` into `cs2vm2_jit_internal.h`. Same checks as today's loop. |
| 32 `LD` | `PUSH_INT_LOCAL a` with `0 <= a < local_int_count <= CS2VM_MAX_LOCALS`: `stack[top++] = locals[a]` with the stack check only. |
| 33 `PD` | `POP_INT_LOCAL b` with the same bound: `locals[b] = stack[--top]`, no grow path. |
| 34 `CSTR` | `PUSH_CONSTANT_STRING` whose `string_constants[pc]` is not NULL: push that `struct CS2VM2_Str*` (2.5). |
| 64 .. 87 | the 24 fused groups below. |

Codes 32 and 33 are the one-instruction form of the proof the fused groups
already rely on (2.3); they are the "load-time proof that drops a bounds check"
item, cost nothing extra to build, and are unmeasured on their own (section 7
says how they are checked).

In the table, `L a` is `PUSH_INT_LOCAL` with a declared local, `C x` is
`PUSH_CONSTANT_INT`, `P b` is `POP_INT_LOCAL` with a declared local, `BR o` is
`BRANCH`, and `BRcc o` is one of `BRANCH_EQUALS`, `BRANCH_NOT`,
`BRANCH_LESS_THAN`, `BRANCH_GREATER_THAN` (the four the lab measured). `k` is
the instruction count; `need` the int-stack depth the group requires on entry;
`peak` how far above the entry top the unfused sequence reaches. A branch at
group position `k-1` taken with operand `o` goes to `pc + k + o`, not taken to
`pc + k` (the same pc the unfused `pc += 1 + operand` reaches).

| code | instructions | k | need | peak | effect |
| --- | --- | --- | --- | --- | --- |
| 64 `LL` | L a; L b | 2 | 0 | 2 | push loc[a], loc[b] |
| 65 `LC` | L a; C x | 2 | 0 | 2 | push loc[a], x |
| 66 `CL` | C x; L a | 2 | 0 | 2 | push x, loc[a] |
| 67 `CC` | C x; C y | 2 | 0 | 2 | push x, y |
| 68 `LLL` | L a; L b; L c | 3 | 0 | 3 | push loc[a], loc[b], loc[c] |
| 69 `LCAP` | L a; C x; ADD; P b | 4 | 0 | 2 | loc[b] = add(loc[a], x) |
| 70 `LCA` | L a; C x; ADD | 3 | 0 | 2 | push add(loc[a], x) |
| 71 `CA` | C x; ADD | 2 | 1 | 1 | top = add(top, x) |
| 72 `AP` | ADD; P b | 2 | 2 | 0 | loc[b] = add(s[-2], s[-1]); pop 2 |
| 73 `CP` | C x; P b | 2 | 0 | 1 | loc[b] = x |
| 74 `LP` | L a; P b | 2 | 0 | 1 | loc[b] = loc[a] |
| 75 `PBR` | P b; BR o | 2 | 1 | 0 | loc[b] = pop; pc = pc + 2 + o |
| 76-79 `CBRcc` | C x; BRcc o | 2 | 1 | 1 | v = pop; taken if v cc x |
| 80-83 `LCBRcc` | L a; C x; BRcc o | 3 | 0 | 2 | taken if loc[a] cc x |
| 84-87 `LLBRcc` | L a; L b; BRcc o | 3 | 0 | 2 | taken if loc[a] cc loc[b] |

`add` is `cs2_math_add`, the function the general handler calls. Comparison
order is the unfused one: `BRcc` pops `b` (top) and `a` (under it) and tests
`a cc b`, so `C x; BRcc` tests `v cc x` where `v` was on the stack.

### 2.2 The builder

`CS2VM2_JitBuild` walks `pc = 0 .. op_count-1` once. At each pc it tries the
shapes in the lab's priority order (`vmfuse_pre.inc`: `LCAP`, `LCBRcc`, `LLBRcc`,
`LCA`, `LLL`, `LL`, `LC`, `LP`, `CBRcc`, `CA`, `CP`, `CL`, `CC`, `AP`, `PBR`),
then `CSTR`, `LD`, `PD`, then the plain code, then 0. A shape matches only if
all its instructions exist (`pc + k <= op_count`), every local it names is in
`[0, local_int_count)` and `local_int_count <= CS2VM_MAX_LOCALS`, and any branch
in it has an in-range target. The pc then advances by ONE, not by `k`: every pc
gets its own code.

Refusals (`enum CS2VM2_JitBuildResult`), which leave `script->jit` NULL and the
script on today's path:

- `op_count <= 0`;
- `dialect->rs2_commands` (the hot loop never runs those scripts);
- any jump by operand (the seven hot int branches, the six long compares
  68..73 and `BRANCH_IF_ONE` 86: `cs2vm2_op_jumps_by_operand`, the one list,
  which `CS2VM2_JumpRelative` asserts against in a debug build) whose target
  `pc + 1 + operand` is outside `[0, op_count]`,
  or any case of a `SWITCH` whose table exists whose target `pc + 1 +
  target_pc` is (the hot loop is entered at whatever pc a switch leaves).
  The plain loop would read `ops[pc]` out of range there today; that is an
  existing defect of the general path and of the plain loop (later work 2.7),
  and the jit simply does not cover such a script.

### 2.3 The loop

> Since 2026-11-24 there is one loop. `cs2vm2_run_hot`, `cs2vm2_run_hot_jit`
> and the outer loop that ran one handler between two stretches of hot
> instructions are merged into `cs2vm2_run_script_body`: it switches on
> `script->run_code[pc]` (the overlay's codes, or the script's own plain codes
> from `CS2VM2_ScriptPrepare` when it has no overlay), the cases below are its
> cases, and code 0 writes the stack top and pc back, calls the handler and
> reads them again. The text below is the design as it was built.

`cs2vm2_run_hot` gains a sibling, `cs2vm2_run_hot_jit`, chosen once on entry by
`frame->script->jit != NULL`. The plain loop is left byte for byte as it is, so
a script without an overlay runs exactly today's code. The jit loop is the same
loop with `switch( code[pc] )` in place of the table lookup, the plain cases
unchanged, and these additions:

- **Stack guard.** A fused case first tests `top >= need` and
  `top + peak <= CS2VM_STACK_MAX`; `CSTR` tests
  `strs_stack_top < CS2VM_STACK_MAX`. On failure it changes nothing and does
  `goto stop` with `for_general` true: the general path runs the group's first
  instruction alone, exactly as an unfused run would have reached it, and the
  hot loop is re-entered at the next pc (whose own code is checked afresh).
  The general path is the reference for every opcode, so falling back is always
  equivalent.
- **Cycle guard.** The caller has already counted the instruction at `pc`
  (`*cycles` includes it). An unfused run would stop with the cycle error
  after the j-th instruction if `*cycles + j - 1 >= CS2VM_MAX_CYCLES`. So a
  group of `k` runs only if `*cycles + (k - 1) <= CS2VM_MAX_CYCLES`; otherwise
  it falls back as above. After the group `*cycles += k - 1`, and the loop's
  common tail (`pc >= op_count || *cycles >= CS2VM_MAX_CYCLES`, then
  `(*cycles)++`) runs exactly as after the group's last instruction unfused.
  The cycle error therefore fires at the same pc with the same
  `last_error_*`, and every executed instruction is counted once.
- **Locals.** `LD`, `PD` and the fused groups index `locals[]` directly. That is
  in range because of the next item, which holds for every frame, whether or
  not the overlay was attached before the frame was pushed.
- **`pc >= op_count`** cannot happen inside a group: the builder and the
  validator both require `pc + k <= op_count`, and only a group's last
  instruction branches.

### 2.4 Frame locals pre-sized at push

`CS2VM2_PushCallScript`, after `cs2vm2_frame_clear`, when
`0 < script->local_int_count <= CS2VM_MAX_LOCALS`:
`cs2vm2_frame_reserve_int_locals(frame, n); frame->int_locals_dirty = n;`.
Unconditional, for every script, so the locals proof never depends on when an
overlay was attached. The two invariants on `struct CS2VM2_Frame` survive:
`reserve` zeroes the slots it adds and `clear` zeroed `[0, old dirty)`, so
`[0, n)` reads 0 until written, as before. Measured neutral on the bank build;
the cost it can add elsewhere is clearing up to `n` ints per push for a script
that declares many locals and writes few (watched in section 7).

### 2.5 Constant strings by pointer

`CSTR` pushes `script->string_constants[pc]` (a `struct CS2VM2_Str*`, see
`cs2vm2_str.h`, made from `string_operands[pc]` by
`CS2VM2_ScriptEnsureConstants`) with no pool copy, and
`CS2VM2_Op_PushConstantString` on the general path does the same (a NULL
operand still pushes a fresh `CS2VM2_StrEmpty`), so there is one semantics.

The prerequisite: nothing may write through a string it popped. In the VM the
only two writers are `CS2VM2_Op_Uppercase` and `CS2VM2_Op_Lowercase`
(`cs2vm2.c` near lines 1786 and 1808: the `*ch = ...` loops). They change to
allocate the result (`CS2VM2_StrAlloc(vm, len)`, transform while copying, push
the new buffer). Every other string producer already writes into a fresh
`StrAlloc`/`StrFmt` buffer (checked: join, number formatting, array join). The
comments on `CS2VM2_StrEmpty` ("in-place opcodes such as UPPERCASE mean no two
stack slots may alias") and `CS2VM2_PushStrFrameLocal` are updated; the copy in
`PushStrFrameLocal` itself stays in v1 (later work).

Host side, the request structs carry `struct CS2VM2_Str*` (not `const`) strings, so a host
handler could write through one. The VM implementer audits every reader of a
request string field (`git grep` the `.def` string fields across `src/game`,
`src/app`, `src/plugin`, `src/ui`) for writes through it and for pointer
identity comparisons (`text == previous`), and records the result in the shared
notes directory; any writer found is fixed on the host side. As a net, a debug
build records a hash of the script's `string_constants` (text and length) when an overlay is built
or attached and `CS2VM2_JitFree` asserts it unchanged.

Lifetime: a constant now lives as long as its script, not until the next
`CS2VM2_ResetRuntime`. Scripts in the provider are never freed before
`CacheProvider_ClientScriptsCleanup` at provider teardown, and
`CacheProvider_ClientScriptAdd` keeps the first decode, so every pointer a VM
holds stays valid for the session. `ScriptCache` (the dummy host) frees its
copies only in `ScriptCache_Free`.

### 2.6 Why behaviour cannot change

| observable | why it is unchanged |
| --- | --- |
| host requests, order, arguments | groups contain no host opcode; `CSTR` pushes the same bytes the copy had |
| errors and `last_error_*` | every guard failure falls back to the general path at the group's first pc |
| cycle count and the cycle limit | groups add `k - 1`; the cycle guard keeps the limit off a group's inside |
| yields | no group contains an opcode that can yield; a resume pc is always a code-0 pc |
| locals read before written | 0 either way (2.4) |
| dead stack slots above `top` | not written by some groups. Nothing in the VM or the host reads above `top`; the replay digest does at kind 7505 (`DB_GETROWTABLE`) because it adds `int_in` to `top`, which is a property of the digest |
| traced or profiled runs | `TORIRS_CS2_TRACE`, trace capture and `TORIRS_CS2_PROFILE` make a run "observed", which never enters either hot loop. `TORIRS_CS2_PROFILE` exists only in a `CS2VM2_PROFILING=1` build (docs/PERF_HARNESS.md); elsewhere it says "not compiled in" once |

### 2.7 Later work, not v1

1. More shapes (the `<=`/`>=` branches, `SUB`, string pushes feeding a string
   op) from a shape census over the recording. Measure each.
2. Defined behaviour for a branch out of range on the general path (today it
   reads out of range; the jit refuses such scripts).
3. `PushStrFrameLocal` without the copy, on the same prerequisite as 2.5.
   Unmeasured.
4. A per-block stack-depth proof that lets fused groups skip the stack guard.
   Needs a control-flow graph and the host opcodes' stack effects
   (`g_cs2vm2_opcode_stack`), GOSUB and RETURN included.
5. One-to-one predecode (opcode and operand side by side). Unmeasured; the
   overlay already removes the table load.
6. Resolved call sites (a GOSUB target cached per pc). Skips a host round trip,
   which touches the `CS2VM2_Ask`-only ruling; needs the owner's word first.
7. Flush the pack at desktop shutdown and on the page's `pagehide`.
8. Do not build: the register form (measured slower).

## 3. The VM API

Authoritative text: `src/cs2vm2/cs2vm2_jit.h`. Summary:

| function | what |
| --- | --- |
| `CS2VM2_JitHash64(data, size, seed)` | the format's 64-bit hash |
| `CS2VM2_JitFingerprint(script)` | one pass; identity of the decoded content |
| `CS2VM2_JitDialectTag(dialect)` | 32-bit stable dialect name; NULL means OSRS |
| `CS2VM2_JitBuild(script)` | build and attach; returns BUILT or a refusal |
| `CS2VM2_JitBlobSize(script)` / `CS2VM2_JitSerialize(script, fp, out, size)` | the blob |
| `CS2VM2_JitAttachBlob(script, fp, blob, size)` | validate, copy out, attach; returns ATTACHED or a rejection |
| `CS2VM2_JitCopy(src, dst)` | `CS2VM2_ScriptCopy`'s half |
| `CS2VM2_JitFree(script)` | deallocator; a script with no overlay is a no-op |
| `CS2VM2_JitGetInfo(script, out)` | static counts for the host's counters |
| `CS2VM2_JitBuildResultName` / `CS2VM2_JitAttachResultName` | names for the report |

Ownership: the overlay hangs off `struct CS2VM2_Script::jit` (new last field,
NULL from `CS2VM2_ScriptInit`). `CS2VM2_ScriptFree` calls `CS2VM2_JitFree`;
`CS2VM2_ScriptCopy` does `if( src->jit ) CS2VM2_JitCopy(src, dst);`. The VM
never computes the fingerprint on the load path: build does not need it, the
host computes it when it must compare or store, and passes it in.

`cs2vm2.c` reads only struct fields of the overlay (through
`cs2vm2_jit_internal.h`) and calls no `cs2vm2_jit.c` function, so the four
standalone test links that compile `cs2vm2.c` without `cs2vm2_script.c`
(`test-cs2-host-request-producers`, `-array-fill`, `-array-bounds`,
`-vm-pool`) keep linking unchanged. Every target that links `cs2vm2_script.c`
must also link `cs2vm2_jit.c`; in the tree that is the main `SRCS` list in
`src/makefile` only.

One cheap VM addition for tests and the replay rig: `uint64_t cycles_run` on
`struct CS2VM2_Thread`, increased by `cycles` at each return of
`cs2vm2_run_script_body`, next to the existing `TORIRS_PERF_COUNT(...OPCODES...)`
(one add per `RunScript` call, nothing per opcode). Tests compare it between a
jitted and an unjitted run; the rig reads it instead of redefining
`TORIRS_PERF_COUNT`.

### Hash

`CS2VM2_JitHash64(data, n, seed)`, all arithmetic mod 2^64:

```
h = seed ^ (n * 0x9E3779B97F4A7C15)
for each full 8-byte block, read as a little-endian u64 w:
    h = step(h, w)
if n % 8 != 0:
    w = the remaining bytes little-endian into a zeroed u64
        | ((uint64_t)(n % 8) << 56)
    h = step(h, w)
h ^= h >> 33; h *= 0xFF51AFD7ED558CCD
h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53
h ^= h >> 33
return h

step(h, w):
    w *= 0xBF58476D1CE4E5B9; w ^= w >> 31
    h ^= w
    h = rotl64(h, 27) * 0x94D049BB133111EB
    return h
```

The fingerprint and the checksums hash a byte stream; an implementation may
feed it in pieces but must produce the value of hashing the whole stream at
once (buffer to 8-byte blocks). Golden values for a fixed input are recorded
once in `jit_blob_test.c` and never edited without a format version bump.

### Dialect tag

`CS2VM2_JitDialectTag(d)` (NULL means `&CS2VM2_DIALECT_OSRS`): the hash with
seed `0x43533244494131` ("CS2DIA1") over `str name`, then `u8
cc_create_nested`, `u8 array_handles`, `u8 rs2_commands`; the 64-bit result
folded to 32 bits as `(uint32_t)(h ^ (h >> 32))`.

### Fingerprint

`CS2VM2_JitHash64` with seed `CS2VM2_JIT_FINGERPRINT_SEED` over this stream,
every integer little-endian; `str` is `u32 0` for NULL, else `u32 len + 1`
then `len` bytes:

```
u32 op_count, local_int_count, local_string_count, local_long_count,
    int_argument_count, string_argument_count, long_argument_count,
    int_stack_depth, str_stack_depth
str signature
u16 opcodes[op_count]
i32 int_operands[op_count]
u8  long_operands != NULL;  if so: i64 long_operands[op_count]
u8  string_operands != NULL; if so: str string_operands[pc] for each pc
u32 switch_table_count; per table: u32 case_count, then (i32 key, i32 target_pc) per case
```

### Blob

Little-endian, written byte by byte (never a struct dump), so it is the same on
x86-64, i686 (XP), arm and wasm32:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | magic `CS2VM2_JIT_BLOB_MAGIC` ("CJIT") |
| 4 | 2 | `CS2VM2_JIT_FORMAT_VERSION` |
| 6 | 2 | header size, 48 |
| 8 | 4 | `CS2VM2_JitDialectTag(script->dialect)` |
| 12 | 4 | script_id |
| 16 | 8 | fingerprint |
| 24 | 4 | op_count |
| 28 | 4 | local_int_count |
| 32 | 4 | code length (= op_count) |
| 36 | 4 | flags, 0 in v1 |
| 40 | 8 | checksum: `CS2VM2_JitHash64` seeded with the magic over bytes 0..40 then the code |
| 48 | op_count | code |

### Validation

`CS2VM2_JitAttachBlob(script, fp, blob, size)`, in this order, first failure
wins:

1. `size < 48`: TRUNCATED.
2. magic: MAGIC. 3. version: VERSION. 4. dialect tag: DIALECT.
5. script_id: SCRIPT_ID.
6. header fingerprint != `fp`, or header op_count or local_int_count differ
   from the script's: STALE.
7. header size != 48, code length != op_count, flags != 0: SHAPE.
8. `size < 48 + op_count`: TRUNCATED; `size > 48 + op_count`: SHAPE.
9. checksum: CHECKSUM.
10. the script is empty or rs2-dialect: UNJITTABLE.
11. one pass over the pcs. A jump by operand or a switch case out of range in the script: UNJITTABLE.
    A code that is not legal at that pc: ILLEGAL_CODE. Legal means: 0; or the
    plain code `g_cs2vm2_hot_op[ops[pc]]` when that is non-zero; or `LD`/`PD`/
    `CSTR`/a fused code whose instruction shape matches `ops[pc .. pc+k)`,
    with `pc + k <= op_count`, every named local below `local_int_count`,
    `local_int_count <= CS2VM_MAX_LOCALS`, `string_operands` and
    `string_operands[pc]` non-NULL for `CSTR`. Any other byte value is illegal.
12. allocate, copy the code, attach: ATTACHED.

Step 11 is the safety argument; steps 1-10 only decide which miss it is. Any
legal code is semantics-preserving on its own (2.6), so a blob whose choices
differ from this build's builder (an `LL` where the builder would now pick
`LLL`) is still safe and still correct. The format version is bumped when the
builder's choices change anyway, so such a blob only arrives corrupted.

Note on cost, stated here so nobody is surprised later: step 11 is a linear
pass over the script, which is about what `CS2VM2_JitBuild` costs, and the host
must also run `CS2VM2_JitFingerprint` (another pass) to know which entry to
validate. Section 5 is how that is measured.

## 4. The platform cache

### 4.1 The flow

```
boot (App_Init, after the cache profile is set)
  CS2JitCache_New(dialect)                     pack name from dialect tag + version
  Task_CS2JitPackLoad  -> parallel queue       DERIVED_READ "<pack name>"
        lands ----------> CS2JitCache_PackLanded(bytes) or _PackAbsent()

script load (Task_Dat2ClientScriptLoad, between decode and ClientScriptAdd)
  CacheProvider_ClientScriptJit(provider, script)
     jit disabled (TORIRS_CS2_JIT=0)         -> nothing
     no cache (TORIRS_CS2_JIT_CACHE=0, tests) -> CS2VM2_JitBuild
     cache, pack not resolved yet            -> CS2VM2_JitBuild, counted "before_pack"
     cache, pack has an entry for this id    -> fingerprint, compare, CS2VM2_JitAttachBlob
                                                any rejection -> CS2VM2_JitBuild, counted by reason
     cache, no entry                         -> CS2VM2_JitBuild, id marked dirty

once per frame, after present (App_RunOnce)
  CS2JitCache_MaybeStore(now): pack resolved, dirty, no store in flight,
                               STORE_MS since the first dirty mark
     -> Task_CS2JitPackStore -> background queue
          plan (fingerprint each dirty script; drop those the pack already holds unchanged)
          nothing changed -> done, no IO
          serialize in units of 64 scripts (PT_TASK_YIELD_IF_SPENT between units)
          DERIVED_WRITE "<pack name>" -> lands -> CS2JitCache_StoreDone(ok)
```

The VM never yields for the cache; a script always runs, with an overlay built
in memory whenever the cached one is absent, stale, corrupt, or not yet read.
Building while the read is outstanding (rather than waiting for it) is
justified by the numbers: a build is one pass over about 50 opcodes for the
median script, microseconds, while the read on the web lane is at least one
IndexedDB round trip, milliseconds; waiting would put the slower path in front
of the faster one on the script's critical path, and the result is the same
overlay either way.

Scripts built before the pack landed are not re-attached; at store time their
fingerprints are compared with the pack's entries, and equal ones are not
rewritten. A session whose read was merely slow therefore writes nothing.

Writes never happen before the read has resolved (landed, absent, or failed):
writing first would replace a pack the session never saw with one holding only
its own scripts.

### 4.2 Batching: one pack per cache and dialect

The unit of IO is one pack holding every jitted script of one cache in one
dialect at one format version: one read at boot, one write per flush. Chosen
because of what the web lane already measured for its boot prefetch:

- Per-item IndexedDB transactions were the cost: 24,000 round trips ahead of
  the first JS5 wave, answers landing one at a time 0.15 ms apart
  (`torirs_idb.js` comments on `groupGetMany`, `groupGetAll`, `flushGroups`).
  The fix was one `getAll` per table ("a boot that fills sprites, interfaces
  and scripts reads three of them") and batched writes. One pack is the same
  shape: one request.
- Per interface group does not work here: which scripts an interface needs is
  only known as its GOSUBs run, so the batch could not be named in advance, and
  each batch would be another round trip.
- Per boot wave is what one pack read at boot already gives, without a wave
  boundary to define.
- Size is small: about 39 KB for the bank cycles' 241 scripts; a whole cache's
  roughly 9,700 scripts are a low number of MB. A 16 MB ceiling is enforced at
  serialization (entries past it are left out and counted).

The pack is resident after it lands (it is what the lazy attaches read and
what a store carries forward), so its memory is the pack size for the session.

### 4.3 Pack format

Little-endian, written byte by byte:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | magic "CJPK" (0x4B504A43) |
| 4 | 2 | pack version, 1 |
| 6 | 2 | header size, 40 |
| 8 | 4 | `CS2VM2_JIT_FORMAT_VERSION` of every entry |
| 12 | 4 | dialect tag |
| 16 | 4 | entry count |
| 20 | 4 | reserved, 0 |
| 24 | 8 | index checksum: `CS2VM2_JitHash64` seeded with the magic over bytes 0..24 then the index |
| 32 | 8 | total size |
| 40 | 24 x count | index, sorted by script_id, unique: i32 script_id, u32 reserved, u64 fingerprint, u32 offset, u32 length |
| ... | | blobs (each a `CS2VM2_JitSerialize` blob, self-validating) |

A pack whose header fails (magic, version, format version, dialect, sizes,
index checksum, unsorted or duplicate ids) is treated as absent and counted;
the first store replaces it. An entry whose `offset + length` exceeds the total
size is ignored. Lookup is a binary search on the resident index.

A store writes the sorted union of (a) the old pack's entries for scripts not
rebuilt this session, copied byte for byte, and (b) a fresh blob for every
script built this session whose fingerprint differs from the old entry or has
none. Entries for scripts this session never loaded are carried, so the pack
converges on everything the player has run rather than on the last session.

### 4.4 Keys

| key part | where it lives |
| --- | --- |
| cache id | the location: the executor scopes every derived record to the cache it was given in `Platform_IO_InitCacheId` (4.5, 4.6) |
| dialect | pack name and pack header, and each blob header |
| JIT format version | pack name and pack header, and each blob header |
| script id | pack index and blob header |
| script fingerprint | pack index and blob header |

Pack name: `cs2jit-<dialect tag, 8 hex digits>-v<format version>.pack`, for
example `cs2jit-1a2b3c4d-v2.pack`. A different version or dialect is a
different name, so it is never even read.

### 4.5 Desktop executor (`platform_x_io.c`)

Every native lane builds this one file: macOS, Linux, Windows 64, Windows XP
(`PLATFORM=win32`, i686), Android (`PLATFORM=android`, `platform.mk` line 736).
There is no iOS lane in `platform.mk`. So one implementation covers them all.

- `PlatformX_IO_InitCacheId` records `dir` (today it ignores it). The derived
  directory is `<dir>.derived/`, the cache directory's sibling, with trailing
  separators stripped from `dir` first. Next to the cache, never inside it, so
  nothing that reads, copies or checks a cache directory sees a new file, and
  pristine caches stay pristine. The repo's cache directories match
  `.gitignore`'s `cache.*/`, and the siblings match it too; implementer 2 also
  adds `*.derived/` to `.gitignore` for caches named otherwise. An empty `dir`,
  or `.`, disables the derived store (reads answer absent, writes fail).
- `TORIRS_DERIVED_DIR=<root>` (read once, at `InitCacheId`) replaces the
  location with `<root>/<basename of dir>/`. Every verify run from the main
  checkout sets it to a scratch directory, so no run writes beside the main
  checkout's caches.
- DERIVED_READ: read the whole file into a `ToriMem_Net` buffer, answered
  inside Process like every desktop read. A missing file is error_code -1 with
  no log line (a miss). Any other failure is -1 and one log line per session.
- DERIVED_WRITE: create the directory, write `<path>.<pid>.tmp`, then
  `rename` (POSIX) or `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH)` (both Windows lanes; available on XP), the same
  write-then-rename `write_client_file_item` does, with the pid in the temp
  name because two clients on one cache (two sessions from one checkout) may
  write at once. Last writer wins; each writer's pack is complete and valid.
  A read-only location fails the write with -1 and one log line; the cache
  then reads as a miss next time, never an error.
- The name is validated by the executor too (`[A-Za-z0-9._-]`, 1..63 bytes, no
  leading dot): a bad name is -1, never a path outside the directory.
- The executor counts derived reads and writes, bytes, and nanoseconds spent
  inside LoadItem for them (two clock reads per derived item; there are a
  handful per session). `PlatformX_IO_DerivedStats` returns them.

### 4.6 Web executor (`platform_web_io.js`, `torirs_hostio.js`, `torirs_idb.js`)

- Its own database: `<database name>-derived` (default
  `torirs-cache-derived`, following `?idb=` / `TORIRS_IDB_NAME`), version 1,
  object store `derived`, keyPath `k`, index `by_cache` on `c`. Row:
  `{k: "<cacheKey>|<name>", c: cacheKey, n: name, d: ArrayBuffer, t: Date.now()}`.
  A separate database rather than a new store in `torirs-cache`: adding a store
  means a version upgrade, and an upgrade is blocked for as long as any other
  tab holds version 1 open (the module sets no `onversionchange`), which would
  leave every IndexedDB read of the new page waiting. A new database has no
  open connections to wait for. It also keeps a large pack write in its own
  transactions, never in front of a `groups` or `files` read.
- `cacheKey` is the manifest's `[cache:boot] dir=`, the same key `groups` uses
  (`torirs_hostio.js` `cacheKey()`), so the pack is scoped exactly as the
  cache's archives are.
- Clearing: `ToriRS_IDB.clearCache(cacheKey)`, which `?cache_reset=1` calls,
  also deletes this cache's `derived` rows (cursor over `by_cache`). Clearing
  the site's data removes both databases.
- `describe` copies a DERIVED_WRITE's bytes out of the heap before any await
  (the reason FILE_WRITE does); `execute` calls `inst.host.readDerived(name)` /
  `writeDerived(name, bytes)`. A host without them answers absent (the pack is
  optional by contract). A READ's answer is copied into a
  `ToriRS_IO_AllocPayload` buffer like every other byte answer.
- Telemetry: `__torirs_io_telemetry.derived = {reads, readMs, readBytes,
  writes, writeMs, writeBytes}`, measured with `performance.now()` around the
  host call.

### 4.7 Queues and the frame-settle gate

- `Task_CS2JitPackLoad` goes on the provider's asset queue (the parallel frame
  queue) as an ordinary task. `Task_CS2JitPackStore` goes on the provider's
  compute queue (the background queue) as an ordinary task.
- Neither is added with `TaskRunner_AddRenderBlockingSerialTask`, so neither
  sets `render_blocking` or `frame_settle_pending`; `TaskRunner_SettleFrame`
  stops when no render-blocking task remains (`task_runner.h`), so a pending
  pack read or write never holds a frame or the settle gate
  (`docs/CS2_EXECUTION.md`). No task, boot step or script joins either of them.
- On the desktop a DERIVED_WRITE is answered synchronously inside the
  background queue's Process (fwrite and rename of tens to hundreds of KB). It
  happens at most once per `STORE_MS` and only when something changed. The
  store task's units are bounded (64 scripts) and yield with
  `PT_TASK_YIELD_IF_SPENT`.
- Both tasks are protothreads: no `switch` between `PT_BEGIN` and `PT_END`.
  Reason-to-counter mapping lives in plain helpers. `make -C src
  check-pt-switch` must print `total 0`.
- Shutdown order: the provider is closed before the background runner is freed
  (`App_Shutdown`), so a store task is never stepped after the scripts are
  gone, and its `free` releases only its own buffer. `CS2JitCache_Free` runs
  after `app_background_runner_free`.

### 4.8 Failure semantics

| situation | result |
| --- | --- |
| no pack (first run, cleared, read-only location) | every script built in memory; first store writes one |
| pack read still outstanding | scripts built in memory, counted `before_pack` |
| pack header bad, other version, other dialect | pack ignored, counted; replaced by the next store |
| entry stale (script changed) | built in memory, counted `reject_stale`; replaced at the next store |
| entry corrupt, truncated, illegal | built in memory, counted by reason; replaced |
| read IO error | as absent; one log line |
| write IO error | counted `store_failures`, one log line; dirty set kept, retried after `STORE_MS` (at most 3 failures, then the cache stops storing for the session) |

Nothing here is an error to the player, and nothing asserts on data that came
from storage.

### 4.9 Proposed additions to the IO seam

Not in any header yet. Implementer 2 applies this block.

```c
/* ---- PROPOSED: src/asyncio.h -------------------------------------------- */

enum ToriRS_IOKind
{
    /* ... existing kinds, unchanged values ... */
    TORIRS_IOK_CACHE_PREFETCH,
    /*
     * A record DERIVED from the cache this executor serves -- something the
     * client computed from cache content and keeps so a later run can skip
     * the work (today: the CS2 jit pack, docs/CS2_JIT.md). The executor scopes
     * every name to the cache it was given in InitCacheId: two caches never
     * share a record, and a record never lands in the repository's tracked
     * tree. `u.derived.name` is a bare file name, [A-Za-z0-9._-], 1..63 bytes.
     *
     * DERIVED_READ answers `data`/`data_size` (a NET buffer, freed by
     * ClearItem like FILE_READ's), or error_code -1 when there is no such
     * record -- the ordinary first-run state, not a failure.
     *
     * DERIVED_WRITE LENDS `data`/`data_size` for the duration of the request,
     * exactly as FILE_WRITE does, and replaces the record atomically: a reader
     * sees the old bytes or the new ones, never a mix.
     */
    TORIRS_IOK_DERIVED_READ,   /* 8 */
    TORIRS_IOK_DERIVED_WRITE,  /* 9 */
};

#define TORIRS_IOITEM_DERIVED_NAME_MAX 64

struct IOItem_Derived
{
    char name[TORIRS_IOITEM_DERIVED_NAME_MAX];
};

/* in struct ToriRS_IOItem's union: */
        struct IOItem_Derived derived;

/* ToriRS_IO_ClearItem: DERIVED_READ frees `data` from NET (with CONFIG_FILE,
 * SCRIPT, FILE_READ); DERIVED_WRITE owns nothing (with FILE_WRITE). */

/* Asserts io, name, a valid name; slot_id is 0 like every other queue call. */
static inline void
ToriRS_IO_QueueDerivedRead(
    struct ToriRS_IOBatch* io,
    int slot_id,
    char const* name);

/* Asserts io, name, a valid name, data unless data_size is 0. The bytes stay
 * the caller's and must outlive the request (until the task resumes). */
static inline void
ToriRS_IO_QueueDerivedWrite(
    struct ToriRS_IOBatch* io,
    int slot_id,
    char const* name,
    void* data,
    int data_size);

/* ---- PROPOSED: src/asyncio_abi.c ---------------------------------------- */
/* Two slots before TORIRS_IO_ABI_COUNT, and the magic bumped, so a browser
 * executor built against the old order refuses rather than misreads:       */
    TORIRS_IO_ABI_DERIVED_NAME_OFF,   /* offsetof(struct IOItem_Derived, name) */
    TORIRS_IO_ABI_DERIVED_NAME_MAX,   /* TORIRS_IOITEM_DERIVED_NAME_MAX */

/* ---- PROPOSED: src/platform/platform_io.h -------------------------------- */
/*
 * DERIVED RECORDS (TORIRS_IOK_DERIVED_READ / _WRITE, asyncio.h). No new entry
 * point: LoadItem and Process carry them like every other kind. What each
 * executor promises:
 *
 *   PlatformX_*    <cache dir>.derived/<name>, beside the directory given to
 *                  InitCacheId (TORIRS_DERIVED_DIR=<root> moves it to
 *                  <root>/<basename of that dir>/). Read inside Process;
 *                  write-then-rename with the pid in the temporary name.
 *   PlatformWeb_*  IndexedDB database "<idb name>-derived", store "derived",
 *                  key "<cacheKey>|<name>", index by_cache; cleared with the
 *                  cache by ToriRS_IDB.clearCache (?cache_reset=1). Always
 *                  answered after an await.
 *
 * Either may answer any read "absent" and any write "failed". A derived
 * record is an optimisation: its consumer must run correctly without it.
 */
```

### 4.10 Host module (implementer 2 writes the header)

`src/engine/cs2_jit_cache.h`, owned by the engine layer next to the provider,
because the one decode site that adds scripts
(`engine/dat2/task_dat2_clientscript_load.c`) is there:

```c
struct CS2JitCache* CS2JitCache_New(struct CS2VM2_Dialect const* dialect);
void CS2JitCache_Free(struct CS2JitCache* cache);          /* NULL ok */
char const* CS2JitCache_PackName(struct CS2JitCache const* cache);
void CS2JitCache_SetTiming(struct CS2JitCache* cache, bool on);

/* the boot read */
void CS2JitCache_ReadIssued(struct CS2JitCache* cache);
void CS2JitCache_PackLanded(struct CS2JitCache* cache, void* bytes, int size); /* takes the NET buffer */
void CS2JitCache_PackAbsent(struct CS2JitCache* cache);

/* a script load; asserts the script's dialect tag is the cache's */
void CS2JitCache_Prepare(struct CS2JitCache* cache, struct CS2VM2_Script* script);

/* write-behind */
bool CS2JitCache_MaybeStore(struct CS2JitCache* cache, uint64_t now_ms,
                            struct ToriRS_TaskQueue* background, struct CacheProvider* provider);
/* used by the store task: plan, serialize in units, finish */
size_t CS2JitCache_StorePlan(struct CS2JitCache* cache, struct CacheProvider* provider);
bool CS2JitCache_StoreUnit(struct CS2JitCache* cache, struct CacheProvider* provider,
                           uint8_t* out, size_t out_size, int max_scripts); /* true: done */
void CS2JitCache_StoreDone(struct CS2JitCache* cache, bool ok);

void CS2JitCache_Stats(struct CS2JitCache const* cache, struct CS2JitCacheStats* out);
void CS2JitCache_Report(struct CS2JitCache const* cache, char const* why); /* one "cs2jit:" line */
```

The state functions are free of tasks and IO so the cache test drives them
directly. The store task looks scripts up by id through the provider in each
unit (the provider keeps the first decode of a script, so an id names one
script for the session) and holds no script pointer across a yield.

`CacheProvider` gains `int cs2jit_enabled` (default 1, so every provider a
test builds runs overlays) and `struct CS2JitCache* cs2jit` (default NULL),
set by `CacheProvider_SetCS2Jit(provider, enabled, cache)`, and
`CacheProvider_ClientScriptJit(provider, script)`, the dispatch in 4.1. The
load task calls it after `CS2VM2_ScriptCopyFromRSCache` succeeds and before
`CacheProvider_ClientScriptAdd`. If `ClientScriptAdd` drops the new decode for
an earlier one, the dropped script's overlay goes with it; the dirty set holds
ids, so nothing points at it.

### 4.11 Knobs

Added to `src/torirs_env.h` with `TORIRS_ENV_KNOB`; all read at boot, none on a
frame path, none read by the VM:

| knob | default | effect |
| --- | --- | --- |
| `TORIRS_CS2_JIT=0` | on | no overlays at all: today's VM, for A/B in one binary |
| `TORIRS_CS2_JIT_CACHE=0` | on | overlays built in memory every time; no pack read or write |
| `TORIRS_CS2_JIT_STATS=1` | off | timers on; `cs2jit:` report after each store and at shutdown |
| `TORIRS_CS2_JIT_STORE_MS=<ms>` | 10000 | write-behind delay after the first dirty mark |
| `TORIRS_DERIVED_DIR=<root>` | unset | desktop derived-record root (4.5) |

On the web each is passed as `?env=NAME=value`. The default of
`TORIRS_CS2_JIT_CACHE` is the owner's call once section 5's numbers exist.

### 4.12 As built (differences from the plan above)

- `CacheProvider_ClientScriptJit` is defined in `engine/cs2_jit_cache.c` and
  declared in `engine/cs2_jit_cache.h`, not in `cache_provider.c`: it reads
  only the provider's two jit fields, so every standalone link of
  `cache_provider.c` (test-cache-provider-invalidate, ...) needs nothing new.
  `cache_provider.c` gains only the fields' defaults and
  `CacheProvider_SetCS2Jit`.
- The store looks scripts up through a `CS2JitScriptLookup` callback
  (`App_CS2JitLookup` wraps `CacheProvider_ClientScriptGet`; the tests pass an
  array), so the state machine and both tasks link without the provider.
  `CS2JitCache_StorePlan` / `_StoreUnit` take it instead of the provider, the
  cache owns the store buffer (`CS2JitCache_StoreBytes` lends it to the
  write), and a successful store makes the written pack the resident one, so
  the next store carries everything forward.
- `CS2JitCache_MaybeStore` is in `task_cs2_jit_pack.h` (it spawns the task);
  the cache's own test asks `CS2JitCache_StoreWanted`. STORE_MS counts from the
  first frame that saw something to store (the load path takes no clock), on
  `PlatformWindow_Ticks64`.
- An entry whose blob the VM rejected this session (corrupt, illegal, other
  version) is marked and rewritten at the next store even though its
  fingerprint still matches; a stale one is rewritten because it does not.
- `TORIRS_CS2_JIT_CACHE=0` keeps a cache object in memory-only mode
  (`CS2JitCache_SetMemoryOnly`): no pack read or written, but the builds are
  counted and timed, so the cold cost has a `cs2jit:` line too.
- Desktop shutdown flush (2.7 item 7, native half): `App_Shutdown` runs one
  store by hand against a private IO, as `app_prefs_flush` does, so scripts
  built in the last STORE_MS are not lost to the next session. Not on the web.
- The report line adds `read=<state>/<reason>` (how the boot read resolved,
  which a later store does not change) and `ignored=`, `unchanged=`,
  `skipped=`, `dropped=`; native also prints a `cs2jit_io:` line from
  `PlatformX_IO_DerivedStats` and the derived directory. Both only under
  `TORIRS_CS2_JIT_STATS=1`.
- `test-cs2-jit-corpus` (every clientscript of a real cache) is NOT written
  yet. The real-cache coverage so far is the bank loop below: 794 scripts of
  the rev 242 cache built, stored, and attached back in the next session with
  0 refusals and 0 rejections.
- `make -C src test-cs2-jit-host` runs `test-cs2-jit-cache`,
  `test-platform-derived-io` and `test-cs2-jit-pack-tasks`.
- `io_server` refuses the two derived kinds by kind (with the client-file
  kinds); the IO wire does not carry them (`IOWire_RequestToItem` maps them
  to NONE).
- Web tests: the executor's derived kinds are in
  `src/platform/test/platform_web_io_derived_test.js` (the older
  `platform_web_io_test.js` is in no make target and already failed at HEAD
  on a missing `_ToriRS_WebApi_ReferenceTableHasArchive` stub; only its ABI
  stub was moved to IOA4). The hostio checks are in
  `src/web/test/idb_derived_store_test.js`; there is no separate
  `hostio_derived_test.js`. `test-web-channel` already stopped at HEAD on
  `chrome_layout_test.js` reading a removed `ioserver/http_server.c`.

Measured once on the bank loop (rev 242 cache, 12 bank opens, native OPT=1,
`TORIRS_CS2_JIT_STATS=1`; one run each, not a timing study):

| run | scripts | built | attached | build | fingerprint + attach | pack | IO |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cold (no pack) | 794 / 77,214 ops | 794 | 0 | 1.93 ms | (fingerprint at store 0.59 ms) | written 134,422 B | write 0.74 ms |
| warm | 794 / 77,214 ops | 0 | 794 | 0 | 0.51 + 0.61 ms | parse 0.012 ms | read 0.38 ms |
| `TORIRS_CS2_JIT_CACHE=0` | 794 / 77,214 ops | 794 | 0 | 1.82 ms | - | - | - |

So a warm session spent about 1.5 ms (fingerprint, validation, parse, read)
to save about 1.9 ms of builds: roughly 0.4 ms a session on this Mac, inside
the timers' own clock reads. The bank scripts 274 and 276 ran the same opcode
counts in all three runs.

## 5. Cost accounting

Building an overlay is one pass over a script. Using the cache costs a
fingerprint pass, a validation pass, the pack's parse, and the IO. So the cache
may well cost more than it saves in v1, and the design does not assume
otherwise. These counters let the verify agents say which, per lane.

Counters (`struct CS2JitCacheStats`, always counted, cheap integer adds at
script load): `prepared`, `prepared_ops`, `before_pack`, `built`, `built_ops`,
`refused[reason]`, `index_hits`, `index_misses`, `fingerprinted`,
`fingerprint_ops`, `attached`, `attached_ops`, `rejected[reason]`,
`pack_bytes`, `pack_entries`, `pack_state`, `stores`, `store_bytes`,
`store_entries_new`, `store_entries_carried`, `store_skipped_unchanged`,
`store_failures`, and the static overlay totals from `CS2VM2_JitGetInfo`
(fused group pcs, declared-local pcs, constant-string pcs).

Timers (only while `TORIRS_CS2_JIT_STATS=1`; `clock_gettime(CLOCK_MONOTONIC)`,
which is a JavaScript call in wasm, hence off by default): `build_ns`,
`fingerprint_ns`, `attach_ns`, `pack_parse_ns`, `serialize_ns`,
`pack_read_wait_ns` (issue to land, wall), `store_wait_ns`. Executor-side IO
time: `PlatformX_IO_DerivedStats` on native, `__torirs_io_telemetry.derived` on
the web. The VM itself takes no clock.

The report line (one line, about 400 bytes):

```
cs2jit: why=<store|shutdown|request> prepared=<n>/<ops> built=<n>/<ops>/<ms> before_pack=<n>
  refused=<empty,dialect,branch> hits=<n> fp=<n>/<ops>/<ms> attached=<n>/<ops>/<ms>
  rejected=<trunc,magic,version,dialect,id,stale,shape,checksum,illegal,unjittable>
  pack=<state>/<bytes>/<entries>/parse <ms>/wait <ms> stores=<n>/<bytes>/<ms> fused=<pcs>
```

Derived for the owner: build ns/op = `build_ns / built_ops`; hit ns/op =
`(fingerprint_ns + attach_ns) / attached_ops`; what the cache bought in a warm
session = `attached_ops x build ns/op - (fingerprint_ns + attach_ns +
pack_parse_ns)`, with the IO wall reported beside it (on the web it is off the
critical path; on the desktop it is inside Process at boot). Build ns/op comes
from a cold session (`TORIRS_CS2_JIT_CACHE=0`, everything built) and from the
cost bench (section 7), since a warm session builds almost nothing.

Native: the line is printed at `App_Shutdown`. Web: printed after each
completed store, and on demand through an exported `torirs_cs2jit_report()`
(`platform_web_api.c`, `EMSCRIPTEN_KEEPALIVE`, the pattern of
`_torirs_memtrace_web_flush`), so `tools/web/browser_probe.py --log` captures
it. The cache registers itself as the process's active cache for that export.

Estimate to be checked, not trusted: the bank cycles' 241 scripts are 27,308
opcodes. At a few ns per opcode, building them all is a fraction of a
millisecond once per session, and a warm session would save that fraction
minus two passes of its own. The win of the overlay is in the run, not the
load; the cache's worth is expected to be close to zero either way, and the
counters will say by how much and in which direction.

## 6. Tests

All new C tests follow the `test-cs2-*` pattern in `src/makefile` (compile the
test file against `$(filter-out $(OBJ_DIR)/main.o,$(OBJS))`, run it), print
`ok:`/`FAIL:` lines, and exit non-zero on any failure. None pins a
silent-failure behaviour.

### VM side (implementer 1), `src/cs2vm2/test/`

`jit_test_util.h`: assemble a script from a list of (opcode, operand), run it
twice in fresh VMs (once with `CS2VM2_JitBuild`, once without), and compare
everything observable: return code, int stack (depth and every live value),
string stack contents, the entry frame's int locals `[0, local_int_count)`,
`last_error_opcode/pc/script_id`, and `thread->cycles_run`.

| target | file | what |
| --- | --- | --- |
| `test-cs2-jit-shapes` | `jit_shapes_test.c` | every one of the 24 fused groups, `LD`, `PD`, `CSTR`, with taken and not-taken branches and both signs of offset, each against its unfused run; `CS2VM2_JitGetInfo` says the expected code was placed; a branch landing on each inner pc of every group of length 3 and 4; a builder census (no shape placed where a local is `>= local_int_count`, where `local_int_count > CS2VM_MAX_LOCALS`, or where it would run past `op_count`) |
| `test-cs2-jit-limits` | `jit_limits_test.c` | int stack pre-filled to `CS2VM_STACK_MAX - peak + {-1, 0, +1}` and below `need` for every shape, string stack full for `CSTR`; the cycle limit landing at every position inside groups of 2, 3 and 4 (loops whose length shifts the limit by one each run); locals at `local_int_count - 1`, at `local_int_count`, `local_int_count == CS2VM_MAX_LOCALS`, `> CS2VM_MAX_LOCALS`; a frame reused by a script that reads locals the previous occupant wrote (zero); the three build refusals |
| `test-cs2-jit-blob` | `jit_blob_test.c` | golden values for `CS2VM2_JitHash64` (lengths 0..17) and one fixed script's fingerprint; fingerprint changes when any one input changes (one opcode, operand, long operand, string byte, string presence, switch key, switch target, each count, the signature) and not when script_id or dialect change; serialize then attach to a fresh copy gives identical code and identical runs; every rejection in `enum CS2VM2_JitAttachResult` produced on purpose (every prefix length 0..size-1, bad magic, version + 1, another dialect's tag, another id, a stale script, header size and flags, one trailing byte, a flipped code byte, a flipped code byte with the checksum recomputed, an rs2 script); a byte-flip and random-code fuzz with recomputed checksums where every accepted blob runs identically to the plain run. Run it under `ENABLE_ASAN=1` too (the target depends on `$(ASAN_DYLD_SHIM)` like `test-cs2-host-request-producers`) |
| `test-cs2-jit-strings` | `jit_strings_test.c` | a loop pushing a constant, `UPPERCASE`, `LOWERCASE`, storing and pushing it again: results correct, `string_operands` byte-identical after the run, the pushed pointer is the constant's; a NULL constant pushes a fresh ""; `CS2VM2_JitFree`'s debug canary fires (in a forked child) when a test writes through a constant |
| `test-cs2-jit` | | runs the four above |

Plus, unchanged and required to pass: `test-cs2-math`, `test-cs2-string`,
`test-cs2-long`, `test-cs2-dialect`, `test-cs2-script-hooks`,
`test-cs2-array-bounds`, `test-cs2-array-fill`, `test-cs2-vm-pool`,
`test-cs2-host-request-producers`, `test-cs2-frame-settle`,
`make -C src check-cs2-yield`, `make -C src check-pt-switch` (`total 0`).

### Host and platform side (implementer 2)

| target | file | what |
| --- | --- | --- |
| `test-cs2-jit-cache` | `src/engine/test/cs2_jit_cache_test.c` | the state machine with in-memory packs, no IO: miss builds; a pack made by a store and fed back as landed gives hits with identical code; prepare before landing builds and counts `before_pack`; no store is wanted before the read resolves; a store after a slow read whose builds equal the pack writes nothing; stale entry rebuilt and replaced; each pack-header rejection makes the whole pack absent; an entry pointing past the end is ignored; the merge carries untouched entries byte for byte and keeps the index sorted and unique; the 16 MB ceiling; three write failures stop storing |
| `test-platform-derived-io` | `src/platform/test/platform_x_derived_io_test.c` | a real `PlatformX_IO` with `InitCacheId` on a temp dir: absent read is -1 with no data and no log; write then read round trips; the file is at `<dir>.derived/<name>` and no `.tmp` is left; `TORIRS_DERIVED_DIR` moves it; bad names refused; a read-only directory fails the write without crashing |
| `test-cs2-jit-pack-tasks` | `src/engine/test/cs2_jit_pack_task_test.c` | the two tasks on role-less `TaskRunner`s over that executor: boot read lands, store writes, a second "session" attaches; neither task is render-blocking and `frame_settle_pending` stays 0 while either is pending |
| `test-cs2-jit-corpus` | `src/engine/test/cs2_jit_corpus_test.c` | `CS2_JIT_TEST_CACHE=<cache dir>` (required; the target fails with a message when it is unset): decode every clientscript, build, serialize, attach to a fresh decode, assert identical code; print the refusal census and total ops. Run against the main checkout's `cache.osrs239` read-only |
| `test-web-derived` | `src/platform/test/platform_web_io_test.js` (extended), `src/web/test/idb_derived_store_test.js`, `src/web/test/hostio_derived_test.js` | node, no browser: the executor's DERIVED kinds keep `pending` truthful, copy write bytes before the await, answer absent as -1; the derived database is separate, keyed by cache, and `clearCache` removes only that cache's rows; the hostio functions key by `cacheKey()`. Added to `test-web-channel` |

The web path end to end is exercised by the verify agents in a real browser
(section 7), not by a make target.

## 7. Verification and profiling plan (for the verify agents)

Everything runs from scratch copies; nothing edits the main checkout. Every run
from the main checkout sets `TORIRS_DERIVED_DIR` to a scratch directory.
Timing follows the house rule: a `yes > /dev/null` spinner during timing,
interleaved arms, several rounds, medians, repeats for any outlier.

Do not time with `TORIRS_CS2_PROFILE=1` or any trace: they make every run
"observed", and an observed run never enters the hot loop, so the overlay
would not run at all. Per-entry timing comes from the `vmrec.inc` splice
(exact per-entry wall time, which does not observe), as in the lab.

1. **Equivalence, native.** Copy `vmlab/replay.c` and adapt it to the worktree
   VM: read `thread->cycles_run` instead of `g_vmrec_ops`; `REPLAY_JIT=0`
   (no overlay), `build` (`CS2VM2_JitBuild` on load), `blob` (build, serialize,
   free, attach from the blob). All 18,412 runs must validate their opcode
   counts in all three arms, and `REPLAY_DIGEST=2` must agree at every kind
   (7505 excepted, or exactly if the digest is changed to hash only `[0, top)`,
   re-baselined from the `REPLAY_JIT=0` arm of the same binary).
2. **VM side, native.** The same binary, `REPLAY_ENTRIES=274,276`, arms 0 and
   build, interleaved. Compare with the lab's -13.6% (overlay) and -25.8%
   (overlay + constant strings). If v1 is short of the lab, rebuild once with
   `LD`/`PD` disabled (a rig-only compile define) to see whether the singleton
   codes are the cause.
3. **VM side, wasm.** The same replay built with `emcc -O3 -sNODERAWFS=1
   -sALLOW_MEMORY_GROWTH=1` and the web lane's compile flags for `cs2vm2.c`
   (taken from a `make PLATFORM=web` build log in a private objdir), run in
   node, warm-up passes discarded (V8 tiers up), arms interleaved. The digest
   and opcode counts must match the native ones.
4. **Cost bench, native and wasm.** Over `rec150/scripts.bin`: per script,
   build, fingerprint, serialize, attach, each repeated; report ns/op medians
   per lane. This is the build-versus-cache number without IO.
5. **Real client, native.** The bank loop (`vmprof/run_skip.sh`, adapted, one
   binary built in the worktree with the vmrec splice),
   `TORIRS_CS2_JIT=0` against `1`, interleaved; per-entry ns for 274/276 and
   the whole bank build. `sample` for where the remaining time is.
6. **Cache, native.** Cold run then warm run with one scratch
   `TORIRS_DERIVED_DIR` and `TORIRS_CS2_JIT_STATS=1`; then
   `TORIRS_CS2_JIT_CACHE=0`. Report the three `cs2jit:` lines and
   `PlatformX_IO_DerivedStats`.
7. **Cache, web.** `make -C src PLATFORM=web` into a private objdir, served
   the way `docs/WEB_CACHE_INDEXEDDB.md` says; `tools/web/browser_probe.py`
   with `--profile <scratch chrome profile> --log` and
   `?env=TORIRS_CS2_JIT_STATS=1`, a cold boot to login and then a warm one on
   the same profile; read the `cs2jit:` lines and
   `__torirs_io_telemetry.derived`. A bank-open driver for the browser does not
   exist; say so in the report rather than inventing a number.
8. **Report.** One table per lane (native, wasm): VM-side run time with and
   without the overlay, build ns/op, hit ns/op, pack read and write cost, and
   what the cache bought per warm session. The owner decides the cache's
   default from it.

## 8. File lists

### Implementer 1: the VM (src/cs2vm2 only, its tests, its make targets)

- `src/cs2vm2/cs2vm2_jit.h`: exists; implement against it. A change to it is
  written to the notes directory for implementer 2.
- `src/cs2vm2/cs2vm2_jit_internal.h` (new): `struct CS2VM2_Jit` (code pointer,
  length, debug strings hash); the overlay code enum (the `CS2VM2_HOT_*` list
  moved from `cs2vm2.c`, then 32-34, then 64-87); `g_cs2vm2_hot_op` as a
  `static const` table (so `cs2vm2.c` and `cs2vm2_jit.c` each have it and the
  standalone links need nothing new); the shape table (k, need, peak,
  instructions) shared by the builder and the validator.
- `src/cs2vm2/cs2vm2_jit.c` (new): hash, fingerprint, dialect tag, build,
  blob size, serialize, attach and validate, copy, free (with the debug
  canary), info, names. No IO, no env, no clock, no log.
- `src/cs2vm2/cs2vm2_script.h`, `cs2vm2_script.c`: the `jit` field;
  `CS2VM2_ScriptFree` calls `CS2VM2_JitFree`; `CS2VM2_ScriptCopy` copies.
- `src/cs2vm2/cs2vm2.c`: `cs2vm2_run_hot_jit` and the dispatch in
  `cs2vm2_run_hot`; presize in `CS2VM2_PushCallScript`; pointer push in
  `CS2VM2_Op_PushConstantString`; `Uppercase`/`Lowercase` allocate; comment
  updates (`StrEmpty`, `PushStrFrameLocal`); `cycles_run`. Keep the line
  `result = vm->vm->host_exec(vm, request);` in `CS2VM2_Ask` and the
  `#include "log/torirs_log.h"` line byte-identical, so the rig's recorder
  splice (`mkvariant.py`) still applies.
- `src/cs2vm2/cs2vm2.h`: `cycles_run`; the `StrEmpty` comment.
- `src/cs2vm2/README.md`: a JIT section (overlay, where it hangs, the rules).
- `src/cs2vm2/test/jit_test_util.h`, `jit_shapes_test.c`, `jit_limits_test.c`,
  `jit_blob_test.c`, `jit_strings_test.c`.
- `src/makefile`: `cs2vm2/cs2vm2_jit.c` in the main source list beside
  `cs2vm2/cs2vm2_script.c`; targets `test-cs2-jit`, `test-cs2-jit-shapes`,
  `test-cs2-jit-limits`, `test-cs2-jit-blob`, `test-cs2-jit-strings` (and their
  `.PHONY`).
- The host-string audit of 2.5, written to the notes directory (read only
  outside `src/cs2vm2`).

### Implementer 2: host and platform

- `src/asyncio.h`, `src/asyncio_abi.c`, `src/platform/platform_io.h` (comment):
  the block in 4.9.
- `src/platform/platform_x_io.c`, `platform_x_io.h`: `InitCacheId` records the
  directory; `TORIRS_DERIVED_DIR`; DERIVED_READ/WRITE; name check;
  `PlatformX_IO_DerivedStats`.
- `src/platform/platform_web_io.js`: the two kinds (`kinds` table, `describe`,
  `execute`, telemetry), the new ABI slots.
- `src/platform/platform_web_api.c`: `torirs_cs2jit_report` export.
- `src/web/torirs_idb.js`: the derived database, `derivedGet`, `derivedPut`,
  `clearCache` extended.
- `src/web/torirs_hostio.js`: `readDerived`, `writeDerived`.
- `src/engine/cs2_jit_cache.h`, `cs2_jit_cache.c` (new): 4.10 and section 5.
- `src/engine/task_cs2_jit_pack.h`, `task_cs2_jit_pack.c` (new):
  `CreateTask_CS2JitPackLoad`, `CreateTask_CS2JitPackStore`.
- `src/engine/cache_provider.h`, `cache_provider.c`: `cs2jit_enabled`,
  `cs2jit`, `CacheProvider_SetCS2Jit`, `CacheProvider_ClientScriptJit`.
- `src/engine/dat2/task_dat2_clientscript_load.c`: the one call.
- `src/app.c` (and `src/app.h` for the field): create the cache after
  `app_provider_set_cache_profile`, enqueue the pack read, read the knobs,
  free order at shutdown; `src/app/app_frame.c`: `CS2JitCache_MaybeStore`
  once per frame after present.
- `src/torirs_env.h`: the five knobs.
- `.gitignore`: `*.derived/`.
- Tests: `src/engine/test/cs2_jit_cache_test.c`,
  `src/engine/test/cs2_jit_pack_task_test.c`,
  `src/engine/test/cs2_jit_corpus_test.c`,
  `src/platform/test/platform_x_derived_io_test.c`,
  `src/platform/test/platform_web_io_test.js` (extended),
  `src/web/test/idb_derived_store_test.js`, `src/web/test/hostio_derived_test.js`.
- `src/makefile`: `engine/cs2_jit_cache.c` and `engine/task_cs2_jit_pack.c`
  in the main source list; targets `test-cs2-jit-cache`,
  `test-cs2-jit-pack-tasks`, `test-cs2-jit-corpus`, `test-platform-derived-io`,
  `test-web-derived` (and `test-web-channel` runs it).
- Docs: `docs/WEB_CACHE_INDEXEDDB.md` (the derived database beside the
  schema table), `docs/CS2_EXECUTION.md` (one line: the jit pack tasks are
  ordinary tasks and never part of the settle).
- Any host-side writer through a request string that implementer 1's audit
  found.

## 9. Risks

- The cache may cost more than it saves (section 5). It is built, measured,
  and switchable; the default is the owner's decision.
- A host handler that writes through, keeps, or compares by pointer a request
  string becomes a cross-run bug once constants are pushed by pointer. The
  audit (2.5) and the debug canary are the guard.
- Pre-sizing locals at push adds clearing for scripts that declare many locals
  and write few. Neutral on the bank build; any other workload the verify
  agents run shows it if it is not.
- The fused overlay's win in wasm is unmeasured: `br_table` dispatch in V8 is
  not the native jump table. Section 7 step 3 measures it before anything is
  claimed for the web.
- The JIT grew `cs2vm2_run_script_body` by about 5 KB of wasm. In Safari the
  loop's size matters more than its dispatch does: JavaScriptCore stops
  optimizing a function at 100,000 bytes. The wasm build splits the opcode
  switch to stay under that, and the web link checks it
  ([web_wasm_function_size.md](web_wasm_function_size.md)).
- A desktop write is synchronous inside the background queue's Process: tens
  to hundreds of KB, at most once per `STORE_MS`, only when something changed.
- Two clients on one cache race their writes; last writer wins and loses the
  other's new entries, which are rebuilt next time.
- A blob's code choices are validated as legal, not as the builder's current
  choices; that is safe by construction, and the format version is bumped
  whenever the builder changes.
- The four standalone test links that compile `cs2vm2.c` without
  `cs2vm2_script.c` break if `cs2vm2.c` ever calls into `cs2vm2_jit.c`; the
  design keeps it to struct reads.
