# ServerScript — LostCity's RuneScript, in C

`src/serverscript/` compiles, reads and runs the server-side script bytecode
that LostCity's engine executes. It exists so the mock rev-230 server can
express behaviour as content rather than as `case` labels in a `switch`.

Reference implementation: `/Users/matthewevers/Documents/git_repos/LostCity_Server`
(TypeScript, `engine/src/engine/script/`).

**LostCity's content ids are 2004-era and do NOT match `cache.osrs230`.** Its
9,333 compiled scripts are a conformance corpus for this reader and VM, not
runnable content for the mock. Anything the mock actually runs is authored
against rev-230 ids. Wiring the two together produces a client showing random
items and a long afternoon.

## Layout

| file | role |
|---|---|
| `gen_opcode_meta.py` | generator; run by hand, output checked in |
| `ss_opcode.h`, `ss_trigger.h`, `ss_meta.gen.h` | **generated** — ids, names, arity, pointer masks |
| `ss_meta.h`, `ss_meta.c` | hand-written interface to the generated tables |
| `ssvm_script.h/.c` | one script: decode, encode, line lookup |
| `ssvm_provider.h/.c` | `script.dat`/`script.idx` container + name and trigger indices |
| `ssvm_strpool.h/.c` | per-state bump allocator for derived strings |
| `ssvm.h`, `ssvm.c` | the VM: core language, arithmetic, strings, host seam |
| `ssc.h`, `ssc_lex.*`, `ssc_symbols.c`, `ssc_compile.c` | the compiler |
| `ssc_main.c` | `sscompile` CLI |

Dependencies are libc plus `3rd/rsareabuf` and nothing else — no SDL, no task
queue, no UI tree — so the whole subsystem links into the standalone `mock230`
binary and every test runs without a cache.

Regenerate the tables after a reference-server update:

```
python3 src/serverscript/gen_opcode_meta.py [path-to-LostCity_Server]
```

## Binary format

Big-endian throughout. Container:

```
script.idx   i32 entry_count; i32 size[i]           (0 = no script at that id)
script.dat   i32 entry_count; i32 compiler_version; bodies concatenated
```

Per script:

```
HEADER   cstr name ("[opnpc1,hans]"); cstr source_path; i32 lookup_key;
         u8 param_count; u8[] param_types;
         u16 line_count; {i32 pc, i32 line} * line_count
CODE     until pos == trailer_pos:  u16 opcode, then one operand:
           opcode == PUSH_CONSTANT_STRING -> cstr
           ss_is_large_operand(opcode)    -> i32
           otherwise                      -> u8
TRAILER  at len - trailer_len - 14:
         i32 instruction_count; u16 int_locals, string_locals, int_args, string_args;
         u8 switch_table_count;
         {u16 case_count; {i32 key, i32 delta} * case_count} * switch_table_count
         u16 trailer_len            (last two bytes; == switch_bytes + 1)
```

`lookup_key = trigger | kind << 8 | subject << 10`, kind 0 global / 1 category /
2 type; `-1` for name-addressed scripts (proc, label, queue, timer, softtimer,
walktrigger, debugproc).

## Things that cost time to learn

**Branch and switch operands are instruction-index deltas, not byte offsets.**
The VM does `pc += delta` then `++pc`, so the target is `pc + delta + 1`. Reading
them as byte offsets works on a hand-built five-op test and fails on every real
script. Corpus range is −268…+984.

**`ss_is_large_operand` is not the CS2 rule.** ServerScript: `opcode <= 100`
minus `{21,22,23,38,39}`. CS2 (`3rd/rscache/src/datatypes/clientscript.c`):
`opcode >= 100` plus `{21,38,39,62,63}`. The boundary differs by one *and* the
exception sets differ. Never share the predicate.

**`param_types` must be read unsigned.** `NPC_STAT` is 254 and `AUTOINT` is 255;
a signed `char` turns both negative and every later type comparison misses.

**Name-addressed scripts must stay out of the trigger index.** The reference
guards with `lookupKey !== 0xffffffff` against a value it read *signed*, so the
test is always true and all ~4,300 of them land under key `-1`, clobbering each
other. Any trigger miss can then return whichever was inserted last. Use
`lookup_key < 0`.

**The `.dat` has no per-entry offsets.** Bodies are concatenated and the `.idx`
sizes are the only way to find each one, so a single wrong size silently
desynchronises every script after it — each of which still decodes into
something plausible. The loader asserts the running offset ends exactly at the
end of the file; that one check covers the whole failure class.

**A subject must fit in ~21 bits.** The on-disk `lookup_key` is an i32 with the
subject at bit 10. rev-230 addresses a component as a packed
`(interface << 16) | child` uid — 231:5 is 15,138,821 — which cannot be a
subject at all. Content targeting a component uses a flat component id, as the
reference's own does. The in-memory index key is `uint64_t` so an oversized
lookup finds nothing rather than wrapping onto an unrelated script.

**The reference's mapzone/zone collisions are a `parseInt` bug, not a key-width
bug.** Its compiler derives those subjects with `parseInt("0_29_75")`, which
stops at the underscore and returns 0, so all 118 land on the same key. No key
width fixes that, and it is inert because the reference never dispatches those
triggers at runtime.

**`opcount` is not reset when a suspended script resumes.** A script that
suspends a thousand times shares one 500,000-instruction budget. That is what
makes it an anti-runaway guarantee rather than a per-call formality — and the
intuitive implementation resets it.

## Deliberate divergences from the reference

| behaviour | reference | here | why |
|---|---|---|---|
| stack underflow | `popInt` returns 0 | abort | a silent 0 becomes a wrong answer hundreds of instructions later |
| divide / modulo by zero | 0 (JS `Infinity` through `ToInt32`) | abort | dividing by zero in content is a bug; a plausible number hides it |
| known-but-unimplemented opcode | silent stack-correct no-op | same, but reports once | how CS2's `oc_examine` shipped returning an int where content wanted a string |
| trigger index key | `int32` | `uint64` | an oversized subject finds nothing instead of aliasing |

## What is *not* ported, on purpose

`src/cs2vm2`'s yield machinery — `CS2VM2_YieldCheckpoint`, `undo_log`,
`CS2VM2_ArrayStore` — is about 200 lines of the hardest code in that file, and
it exists because CS2 host requests are asynchronous and get *replayed*. Server
suspension is a cooperative `execution = SUSPENDED` plus a return, with the
state left exactly where it stopped. Different problem, different shape; mixing
the two models would be the likeliest source of subtle bugs in this port.

Arrays (`DEFINE_ARRAY`, `PUSH_ARRAY_INT`, `POP_ARRAY_INT`) are not implemented:
the reference throws `unimplemented` on all three and the corpus never emits
them.

`LC_OP`, `OC_IOP` and `OC_OP` have opcode ids but no handler in the reference,
so their arity is genuinely unknown and they stay `known = 0` — the VM refuses
to execute them rather than guessing an arity and corrupting the stack.

## Suspension

The engine parks a returned-but-unfinished state and re-enters it later.

| status | set by | resumed by |
|---|---|---|
| `SSVM_SUSPENDED` | `p_delay`, `p_arrivedelay` | the player phase, once the delay expires |
| `SSVM_PAUSEBUTTON` | `p_pausebutton` | a matching resume-button click |
| `SSVM_COUNTDIALOG` | `p_countdialog` | a count-dialog reply |
| `SSVM_NPC_SUSPENDED` | `npc_delay` | the npc phase |
| `SSVM_WORLD_SUSPENDED` | `world_delay` | the world queue |

This is why a state owns its string pool rather than resetting per call
(`src/cs2vm2/cs2vm2_strpool.c` resets at script start, which is right there and
wrong here): a chat dialogue builds a page of text, suspends on `p_pausebutton`,
and reads it back when the player clicks minutes later.

## The compiler

```
make -C src sscompile
src/build/sscompile --src DIR --out DIR [--pack DIR] [--constants DIR]
```

Single pass: bytecode is emitted as the parser walks, with jump targets
backpatched once known. There is no AST because nothing in the language needs
one — no construct's *code* depends on something later in the same expression,
only its jump *targets* do.

Two passes over the file *set* are still required. The first collects every
script's name so `~proc()` can resolve a callee defined in a file compiled
later; the second emits code. Sources compile in sorted path order so script ids
are stable across machines, which matters because a gosub compiles to a script
*id* — an unstable ordering silently repoints every call in the pack.

In scope: trigger headers with arguments and returns, `if` / `else if` / `else`,
`while`, `switch_<type>` with multi-value and `default` cases, `return`,
`def_<type>`, assignment, `%varp` and `%varbit`, `^constants`, `~proc()` and
`@label()`, command calls including the `.dot` form, `calc()` with `+ - * / %`,
comparisons `= ! < > <= >=`, string literals with `<$var>` interpolation, `null`,
`true`/`false`, and coord literals.

Out of scope: arrays, and the `queue*` vararg type-string sugar.

Things worth knowing:

- **Locals live in two separate spaces.** `PUSH_INT_LOCAL` and
  `PUSH_STRING_LOCAL` address different arrays, each indexed from zero, so the
  compiler keeps two counters. One shared counter compiles fine and reads the
  wrong slot at run time.
- **A constant expands to source text, compiled in place.** That is what lets
  `^greeting` hold a string literal and `^some_coord` hold a coord, without the
  symbol table having to know which.
- **A subject above 2^21 is a compile error.** The on-disk `lookup_key` is an
  i32 with the subject at bit 10, so a larger one cannot be represented. Failing
  at compile time is the difference between an error and a script that silently
  never fires.
- **A trailing `RETURN` is appended when content does not write one.** The VM
  errors if pc runs past the last instruction, so a script without one could
  never complete.
- **Emitted branches are the inverse of the source comparison** — `if ($x = 1)`
  emits `BRANCH_NOT`, because the branch jumps *over* the block.

## Tests

```
make -C src test-ss-meta        # generated tables: arity, pointer masks, ap->op, lookup key
make -C src test-ss-corpus      # 9,333 real scripts: exact consumption, names vs script.pack
make -C src test-ss-roundtrip   # decode -> encode -> memcmp over all 5.3 MB
make -C src test-ss-vm          # the VM end to end; no corpus needed
make -C src test-ss-verify      # static CFG stack verifier over every corpus instruction
make -C src test-ssc            # compiler: .rs2 -> bytecode -> reader -> VM, end to end
```

Corpus-backed tests SKIP when the reference server is not checked out beside
this repo; point them elsewhere with `SS_CORPUS=/path/to/LostCity_Server`.

Nothing asserts a measured constant — the corpus is regenerated whenever the
reference's content changes, so counts and byte totals are *printed for drift*
and only self-consistency is asserted.

Two of these are worth understanding rather than just running:

**`test-ss-roundtrip`** re-encodes every script and compares bytes. A decoder can
pass every structural check and still be wrong — skip a field it does not model,
mis-size one it does, normalise a value on the way in. Reproducing the original
bytes exactly is the only evidence none of that happened.

**`test-ss-verify`** walks each script's control-flow graph tracking stack depth,
so every branch forks and every join must agree. It measures the deepest stack
any real content reaches (**18 ints, 16 strings** against limits of 256 and 128),
which is what turns `SSVM_INT_STACK_MAX`/`SSVM_STR_STACK_MAX` from guesses into
measurements. It found two real bugs in the generated table during bring-up:
`[command,enum]`'s signature is parked in a trailing comment and was being read
as zero-arity, and the var ops are runtime-typed because a varp's declared type
decides which stack it touches.

Its model is optimistic about var ops (assumes int, the overwhelming majority)
and counts the scripts where tracking then breaks. About a dozen do, all reading
string varps. The threshold is calibrated so that scale distinguishes the two
causes: a wrong arity on any common command would break hundreds at once.
