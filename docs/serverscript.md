# ServerScript — LostCity's RuneScript, in C

`src/serverscript/` compiles, reads and runs the server-side script bytecode
that LostCity's engine executes. It exists so the mock rev-230 server can
express behaviour as content rather than as `case` labels in a `switch`.

Reference implementation: `/Users/matthewevers/Documents/git_repos/LostCity_Server`
(TypeScript, `engine/src/engine/script/`).

**LostCity's content ids are 2004-era and do NOT match `cache.osrs230`.** Its
9,333 compiled scripts are a conformance corpus for this reader and VM, not
runnable content for the mock. Anything the mock actually runs is authored
against rev-230 ids — see `docs/mock230_content.md`. Wiring the two together produces a client showing random
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

The opcodes the VM does *not* implement itself are the mock server's, and they
live outside this directory in `src/net/mock/mock230_ops_<domain>.c`, one file per
opcode family — see [The host command seam](#the-host-command-seam).

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

## The host command seam

Everything the VM does not implement itself is a *host command*. `SSVM_EnvBindHost`
(`ssvm.h:155`) binds one callback per env; `ssvm.c:1278` calls it and reads the
return as **1 = handled, 0 = not mine**:

```c
    if( state->env->host.command &&
        state->env->host.command(state, opcode, state->dot) )
        return state->execution == SSVM_ABORTED ? 0 : 1;

    return unimplemented_stub(state, opcode);
```

Note the `SSVM_ABORTED` test: a handler that aborts *still returns 1* — it owned
the opcode and the script is already dead — and the dispatcher stops the
interpreter anyway. Returning 0 after aborting would fall through to the stub and
push values onto a stack nobody will read.

Two things happen before the callback and are therefore **not** a handler's job:

- **Pointer requirements**, checked from the generated table at `ssvm.c:1265`.
  `LOC_PARAM` (3011) carries `require = 0x040 / require2 = 0x080`, so by the time
  a handler runs, an active loc is guaranteed to have been *set*. What the table
  cannot check is whether the entity is still alive — a script can suspend between
  `loc_find` and `loc_param`, and a scene rebuild reallocates underneath it. That
  is why the active loc and the active npc ride as `slot + 1` rather than as
  pointers, and why handlers still re-resolve and abort on a dead slot.
- **Arity is not checked.** It is declared in `ss_meta.gen.h` and it is the
  handler's contract to honour. A handler that returns 1 without popping what its
  row declares is worse than no handler at all: it never reaches
  `unimplemented_stub`, so nothing notices, and both stacks stay skewed for the
  rest of the script.

### Per-domain opcode files

The mock server's implementation of that callback — `mock230_script_command` in
`src/net/mock/mock230_scripts.c` — was a single `switch`; that file still carries
**162** `case SS_OP_*:` labels across **5,827** lines (161 distinct opcodes in the
coverage table — `SS_OP_RANDOM` also carries a label in the VM core and is counted
once, there). It is now a chain. Each
`src/net/mock/mock230_ops_<domain>.c` exports one function with the callback's own
signature:

```c
int
mock230_ops_<domain>(struct SSVM_State* state, int opcode, int dot);
```

and `mock230_script_command` offers each domain the opcode in turn before falling
through to the switch (`mock230_scripts.c:2402-2415`):

```c
    if( mock230_ops_db(state, opcode, dot) )
        return 1;
    if( mock230_ops_param(state, opcode, dot) )
        return 1;
    if( mock230_ops_loc(state, opcode, dot) )
        return 1;
    if( mock230_ops_npc(state, opcode, dot) )
        return 1;
    if( mock230_ops_obj(state, opcode, dot) )
        return 1;
    if( mock230_ops_inv(state, opcode, dot) )
        return 1;

    switch( opcode )
```

**The split composes without a dispatch table of its own.** "Return 1 when you
handled it" is already the contract between the VM and the host callback, so a
domain file is a host callback in miniature rather than a new mechanism — there is
no registry to keep in sync, no ordering that matters (two domains must simply
never claim the same opcode), and no way for a domain to be reachable-but-unlisted.

Two consequences worth the pattern on their own:

- **A family lands without serialising on one file.** The `db_*`, `*_param`,
  `loc_*`, `npc_*`, `obj_*` and `inv_*` families landed as six files whose entire
  shared footprint is six two-line hooks. The alternative is every opcode author
  editing the same switch. That is not a hypothetical saving: `obj_*` and `inv_*`
  landed on 2026-08-02 in the same worktree as another lane holding
  `mock230_scripts.c`, and their whole overlap with it was two lines.
- **Coverage cannot silently under-report.** `gen_opcode_coverage.py` *globs*
  `mock230_ops_*.c` (`gen_opcode_coverage.py:39-45`) rather than listing them, so
  a new domain file is picked up with no generator edit. A hand-kept list would be
  the same staleness the generator exists to remove, one level up.

Adding a domain, in full:

1. Write `src/net/mock/mock230_ops_<domain>.c`. One `switch( opcode )`, every arm
   ending in `return 1;` (there is no `break`), `default: return 0;`.
2. Declare it in `mock230.h` in the block beside `mock230_ops_db` (`mock230.h:2587`).
3. Add the two-line hook to `mock230_script_command`.
4. Add the source to `MOCK230_CORE_SRCS` (`src/makefile:251`). That one list
   reaches every consumer — the four `mock230*` binaries, `test-mock230-embed`,
   and the client under `EMBED_SERVER=1`. `MOCK230_PACK_SRCS` is separate and
   deliberately has no VM; do not add there.
5. `cd src && python3 net/mock/gen_opcode_coverage.py`, then
   `make -C src test-mock230-coverage`.

Inside a handler:

| what | how |
|---|---|
| the server | `(struct Mock230Server*)state->env->host.user` |
| the acting player | `srv->active_player` — *whose turn it is*, not "the player" |
| the active npc | `state->host_tag - 1` is the **slot** |
| the active loc | `(intptr_t)SSVM_Active(state, SSVM_ENT_LOC) - 1` is the scene **slot** |
| the active obj | `SSVM_Active(state, SSVM_ENT_OBJ)` is a **handle**, not a slot — put it through `mock230_world_ground_slot` |
| arguments | pop in **reverse** declaration order |
| underflow | `if( !SSVM_PopInt(state, &v) ) return 1;` — **1**, not 0 |
| errors | `SSVM_Abort(state, fmt, ...)` then `return 1` |
| suspension | `SSVM_Suspend(state, ...)` then `return 1` |

The npc and loc conventions encode `slot + 1` so that 0 can mean "none".
**The obj convention deliberately does not**, and copying the other two is the
mistake to avoid: `srv->ground[256]` is a free list, so a bare slot re-validates
as *whatever landed in it while the script was parked*. See `obj_*` below.

A domain file gets no access to `mock230_scripts.c`'s file-scope statics, which is
the pattern's one real friction. Two were resolved by moving the shared thing out:
`push_typed_param` became `mock230_push_typed_param` in `mock230_ops_param.c`
(declared at `mock230.h:2646`), and the four coord helpers became
`mock230_coord_pack/level/x/z` as `static inline` in `mock230.h` — the coord
packing had been separately restated in five places. `script_active_loc` and
`active_npc` were *not* promoted: each is three lines, and each domain wants a
different abort message.

### Coverage is generated, and what it cannot see

`src/net/mock/mock230_opcode_coverage.gen.h` is derived from the `case SS_OP_*:`
labels in the VM core and in every dispatch source. Measured today
(`python3 net/mock/gen_opcode_coverage.py --check` is green):

```
 63  VM core
161  host commands            (mock230_scripts.c)
  9  host commands (db)
  5  host commands (inv)
  5  host commands (loc)
  7  host commands (npc)
  8  host commands (obj)
  2  host commands (param)
260  total, of 401 declared opcodes
```

`(obj)` is eight rather than five because `mock230_ops_obj.c` took the
`oc_wearpos*` config reads as well as the active-obj family — the same
instance-half/config-half pairing `mock230_ops_loc.c` has for `loc_*` and
`lc_*`. `(inv)` is the sixth domain file: `inv_movefromslot`, `inv_dropslot`
and the `inv_moveitem` trio, which moved out of `mock230_scripts.c` (hence
164 → 161) so the domain is whole.

The extraction regex is `^\s*case\s+(SS_OP_[A-Z0-9_]+)\s*:` — plain `case` labels
only. An opcode handled inside a case body via `if( opcode == SS_OP_X )` is *not*
counted, which is correct: `mock230_ops_db.c` disambiguates fallthrough groups
exactly that way, and only the bodies use `opcode ==`.

**What this cannot see is whether the answer is right.** `NPC_PARAM` (2529) has
carried a `case` label in `mock230_scripts.c` since before the param family
existed. That case compared the popped param id against a single
`mock230_content_symbol(MOCK230_PACK_PARAM, "death_drop")` — a game-facing name
spelled in C — pushed that one field and pushed **0 for every other param**, always
onto the int stack, ignoring the declared `default=`. It has been counted as
covered the whole time, the load-time gap report has been silent, and `--selftest`
has been green through it — while **this tree's own content calls it 22 times
across 5 files** (`skill_combat/combat_stats.rs2` and four `drop_tables/*.rs2`),
including every `~npc_combat_defence` and `~npc_combat_maxhit` bonus and
`$damagetype`. Every npc defence roll and max hit in the committed combat slice
was rolled from bonus 0.

The lesson generalises past this one bug: **a generated coverage table measures
that a `case` exists, and nothing else.** The three states an opcode can be in are
*missing* (loud), *implemented* (green), and *implemented and wrong* (invisible),
and only the third needs a test of its own.

`INV_MOVEITEM` (4321) is the second instance and it is worth recording beside
the first, because it is *partly* right rather than wrong — which is harder to
see. Until 2026-08-02 its body was three arms (bank→inv, inv→bank, worn→bank)
and then a `fprintf` saying `is not modelled`, followed by a silent return. So
`inv_moveitem(inv, worn, …)` and `inv_moveitem(worn, inv, …)` — which is every
reference equip and unequip path — did nothing at all, while the opcode counted
as covered for the whole time, correctly by this table's own definition. The
generic container-to-container arm is in `mock230_ops_inv.c` now, appended after
the three bank arms so none of them can change behaviour by the move.

A third state is worse than either and `OC_WEARPOS` (4213) is the example: an
opcode that is **declared, unimplemented, and whose stub value is in range**.
All three `oc_wearpos*` have `known = 1` in `ss_meta.gen.h`, so a script calling
one compiled and ran into `unimplemented_stub`, which pushes 0 — and 0 is
`^wearpos_hat`, a legal equipment slot. The gap report names it and the coverage
header omits it, which is the system working; but *content* written against it
would have run, done something plausible, and logged nothing.

`mock230_ops_npc.c` now claims 2529, and the domain hooks run *before* the switch,
so **`mock230_scripts.c`'s `case SS_OP_NPC_PARAM:` is unreachable dead code**. It
should be deleted; it is recorded here rather than left to be rediscovered, because
editing it changes nothing and reading it suggests otherwise.

### The families that have landed

`db_*` (9 ops) is documented in `mock230_ops_db.c`'s own header. The one thing
worth restating here is the packed column reference `combat_style_table:damagestyle`,
which `ssc_symbols.c` emits as `(table << 12) | (column << 4) | tuple_index` — and
whose **tuple index is 1-based, with 0 meaning "the whole tuple"**. Reading 0 as
"element zero" pushes one value where content expects the whole tuple, so the
failure is a wrong *shape* rather than a wrong number and surfaces far away.
Verified against the reference's own unpack in `DbOps.ts`.

#### `obj_*` — the active ground obj, and a handle that is not a slot

**Eight** ops in `mock230_ops_obj.c`, in two halves. The *instance* half reads or
mutates the active ground obj: `obj_type` (3511), `obj_count` (3503),
`obj_coord` (3502), `obj_takeitem` (3510), `obj_del` (3504). The *config* half
reads the obj record: `oc_wearpos` (4213), `oc_wearpos2` (4214), `oc_wearpos3`
(4215) — the same instance-half/config-half pairing `mock230_ops_loc.c` has for
`loc_*` and `lc_*`, and the reason the file is `_obj` rather than `_groundobj`.
`obj_add` (3500) stays in `mock230_scripts.c`'s switch — it takes a coord rather
than an active obj and predates the split.

| op | id | `engine.rs2` signature | note |
|---|---:|---|---|
| `obj_coord` | 3502 | `()(coord)` | `mock230_coord_pack` |
| `obj_count` | 3503 | `()(int)` | the reference's `isValid(hash64)` receiver gate degenerates — this server has no per-killer loot window |
| `obj_del` | 3504 | `()` | duration is content's `^lootdrop_duration`, not the reference's per-obj `respawnrate`, which this tree has no field for |
| `obj_takeitem` | 3510 | `(inv $inv)` | all-or-nothing add through `mock230_container_add`, then `mock230_world_ground_take` |
| `obj_type` | 3511 | `()(obj)` | |
| `oc_wearpos` | 4213 | `(obj $obj)(int)` | obj config opcode 13, default **-1**, which is RuneScript `null` — no sentinel invented |
| `oc_wearpos2` | 4214 | `(obj $obj)(int)` | config opcode 14 |
| `oc_wearpos3` | 4215 | `(obj $obj)(int)` | config opcode 27 |

`obj_del` and `obj_takeitem` both go through `mock230_world_ground_take` rather
than clearing the slot themselves, and the ordering inside it is load-bearing:
it queues the zone's `OBJ_DEL` **while the obj is still filed in its zone** and
unfiles it after. Unfile first and the event is addressed to nowhere, so every
other client in that zone keeps drawing a pile that is gone.

The three `oc_wearpos*` are the family's cautionary tale rather than its
interesting part — see *what coverage cannot see* above: all three were
`known = 1` and unimplemented, so a script calling one ran into the stub, which
pushes **0**, and 0 is `^wearpos_hat`. `-1` from the real handler is `null`; `0`
from the stub is the head slot.

The one thing that does not follow the npc/loc pattern is how the active entity
rides. Those two are `slot + 1`; a ground obj cannot be, because
`srv->ground[256]` is a **free list** and `mock230_world_obj_add` hands a freed
index straight to the next drop. A script parked between `obj_find` and
`obj_takeitem` would resume onto whatever landed in the slot meanwhile.
`mock230_world_obj_handle` therefore packs `slot + 1` in nine bits with the
slot's `generation` above it, and `mock230_world_ground_slot` refuses a handle
whose generation has moved. The reference is immune for free — it holds an `Obj`
reference and `World.removeObj` clears `isActive` on that object.

The handle reaches a trigger through `srv->pending_active_obj`, a one-shot latch
the `MOCK230_INTERACT_OBJ` dispatch arm sets and `run_trigger_script` consumes.
Not a sixth parameter on `mock230_scripts_run_trigger`: `[opobj<n>]` is the only
family with an obj subject, so the other eighteen call sites would pass 0.

#### `*_param` — the declared type picks the stack, and that is the whole difficulty

| op | id | signature | pops |
|---|---:|---|---:|
| `oc_param` | 4209 | `(obj $obj, param $param)(any)` | 2 |
| `nc_param` | 4005 | `(npc $npc, param $param)(any)` | 2 |
| `lc_param` | 4106 | `(loc $loc, param $param)(any)` | 2 |
| `struct_param` | 4700 | `(struct $struct, param $param)(any)` | 2 |
| `npc_param` | 2529 | `(param $param)(any)` — **active npc** | 1 |
| `loc_param` | 3011 | `(param $param)(any)` — **active loc** | 1 |
| `obj_param` | 3509 | `(param $param)(any)` — active obj | 1 |

All seven are `runtime_typed = 1`. `mock230_push_typed_param` is the single seam:
it reads the param's *declared* type from `configs/all.param`, routes the value to
the int or the string stack accordingly, **aborts in both directions** when the
declaration and the stored value disagree, and answers an absent row with the
declared `default=` rather than 0. It is shared rather than copied because two
copies would be two places for that disagreement to be handled differently.

`lc_param` and `struct_param` landed in `mock230_ops_param.c`; `loc_param` landed
in `mock230_ops_loc.c` because it resolves the *active* loc. **The one-word,
one-argument difference between `loc_param` and `lc_param` is the trap in this
family**: reading one as the other leaves every later value on the wrong rung of
the int stack and nothing fails at the call.

`obj_param` is still not implemented, and the second half of that sentence has
expired: it has **0 callers** in the reference, which is the whole reason now.
The dispatch *does* set an active obj since `mock230_ops_obj.c` landed
(`osrs230_mockserver.md` §3.18), so the VM's `0x100` requirement would be
satisfied — the opcode is simply unwanted. Left out rather than written blind.

**The sort is load-bearing, and that is measured, not assumed.** A record's params
arrive in the order the cache wrote them, which is *not* ascending by key:

| table | records | param rows | string rows | records with ≥2 params | **of those, out of key order** |
|---|---:|---:|---:|---:|---:|
| loc | 62,194 | 1,709 | 0 | 599 | **525 (87.6 %)** |
| struct | 3,988 | 20,751 | 6,115 | 2,833 | **1,847 (65.2 %)** |

(`make -C src test-mock230-param` prints those two lines every run; obj and npc
show the same pattern and have carried explicit sorts since they were written.)

A binary search over an almost-sorted array does not crash. It *misses* — the
lookup reports "this record carries no such param", content pushes the declared
default, and the answer is wrong everywhere and loud nowhere. `mock230_paramtable.c`
is one flat `(owner, key)` table with one `qsort` shared by all four types,
replacing what would otherwise have been four hand-written copies of the same
sort — four chances to reintroduce the one bug in this family that does not
announce itself. loc is the worst of the four, so `lc_param` written "sorted by
construction" would have missed on nearly nine records in ten that carry more than
one param.

**The memory question was not one.** Measured live retention at boot:

| table | retained | decode |
|---|---:|---|
| loc params | **41,016 bytes** | reuses the linked `RSCache_Dat2ConfigLoc*` decoder |
| loc names | 565,681 bytes (30,033 names, blob + sorted index) | same pass |
| loc footprints | 138,472 bytes (17,309 non-1×1) | same pass |
| struct params | **877 KB** (20,751 rows) | `RSCache_Dat2ConfigStructDecodeInplace`, previously called by nobody |
| obj params | 1,262 KB | pre-existing |
| npc params | 700 KB | pre-existing |

The pre-implementation estimate for struct was 1,230 KB; the real figure is
**877 KB**, and 61,124 of loc's 62,194 records carry no params at all. Peak RSS is
unchanged within noise (133–148 MB across three runs with and without the two new
loads) — the peak is the scene build, not these tables. The 60k-record loc group
was called "a real memory question" in planning; it is 41 KB.

#### `loc_*` / `lc_*` — the config reads

| op | id | signature |
|---|---:|---|
| `loc_param` | 3011 | `(param $param)(any)` — active loc |
| `loc_name` | 3010 | `()(string)` — active loc |
| `lc_name` | 4104 | `(loc $loc)(string)` |
| `lc_width` | 4107 | `(loc $loc)(int)` |
| `lc_length` | 4103 | `(loc $loc)(int)` |

The *mutating* half of the family — `loc_add`, `loc_change`, `loc_del`, `loc_find`,
`loc_coord`, `loc_type`, `loc_angle`, `loc_shape`, the find-all iterators — stays
in `mock230_scripts.c`'s switch with the scene and the revert queue it needs. What
moved is exactly what is a pure read of the config record.

`loc_name` is the family's only string-stack op, so it is the one that catches a
handler pushing to the wrong stack: a value on the int stack leaves `def_string $s
= loc_name` underflowing rather than comparing wrongly.

#### `npc_*` / `nc_*` — the config reads and the pure searches

| op | id | signature |
|---|---:|---|
| `npc_param` | 2529 | `(param $param)(any)` — **fixed; it was implemented and wrong** |
| `npc_category` | 2505 | `()(category)` |
| `nc_category` | 4000 | `(npc $npc)(category)` |
| `npc_hasop` | 2523 | `(int $op)(boolean)` |
| `npc_huntall` | 2526 | `(coord $source, int $distance, int $checkvis)` |
| `npc_hunt` | 2525 | `(coord $source, int $distance, int $checkvis)(boolean)` |
| `npc_findcat` | 2517 | `(coord, category, int, int)(boolean)` |

Three findings worth carrying:

- **A name gate was hiding a field.** `mock230_npcinfo()` reports a "Someone"
  placeholder for a nameless record, which is right for text and wrong for a field:
  of 16,292 npc records, **9,149 carry a category and 1,585 of those are
  nameless**, and 10,505 declare a menu op with 177 of those nameless. Read
  through the gated accessor, `npc_category` answers 0 for every multinpc
  instance. `mock230_npcinfo_record()` is the ungated row.
- **`npc_huntall` is not `npc_findallany`.** The reference's
  `NpcHuntAllCommandIterator` skips any npc whose type declares no `op[1]` (op2).
  Without that filter, "every npc nearby" hands content the scenery.
  `npc_hunt`/`npc_findcat` additionally **filter** by Chebyshev distance but
  **rank** by euclidean-squared, with a `<=` tie-break that keeps the **last**
  candidate.
- **Fixing `npc_param` from the cache alone would have been a regression.**
  `death_drop` is a server-band param authored only in a rank-1 `.npc` overlay and
  is nowhere in the cache record, so a cache-only handler answers the declared
  default and the drop tables add obj 0 — which is what the `"death_drop"`
  hardcode was hiding. The overlay's authored rows are now filed under their param
  id too, and `npc_param` consults the overlay first and the cache second.
  Measured: **39 npc defs author 205 param rows between them.**

`.npc_*` dot forms are refused by the VM before reaching any handler: nothing in
this server ever sets the *secondary* npc (`require2 = 0x020` is never added). That
is pre-existing and shared with `npc_type` / `npc_coord`.

#### `inv_*` — the moves, and the arm that coverage could not see

The sixth domain file, `mock230_ops_inv.c`, 2026-08-02. It holds the inv family's
**moves** — one container into another, and a container onto the floor — and
nothing else. The reads and the single-slot writes (`inv_add`, `inv_del`,
`inv_delslot`, `inv_getobj`, `inv_getnum`, `inv_itemspace`, `inv_itemspace2`,
`inv_movetoslot`, `inv_setslot`, `inv_size`, `inv_total`, `inv_freespace`) stay
in `mock230_scripts.c`; they predate the split and moving them is a second change
wearing this one's clothes.

| op | id | `engine.rs2` signature |
|---|---:|---|
| `inv_dropslot` | 4312 | `(inv $inv, coord $coord, int $slot, int $duration)` |
| `inv_movefromslot` | 4318 | `(inv $from_inv, inv $to_inv, int $from_slot)` |
| `inv_moveitem` | 4321 | `(inv $from, inv $to, obj $obj, int $count)` |
| `inv_moveitem_cert` | — | same, certing on the way |
| `inv_moveitem_uncert` | — | same, uncerting |

**`inv_moveitem` is the reason this file exists, and the shape of its gap is the
lesson.** It was not missing. It had a `case` label, three arms — bank→inv,
inv→bank, worn→bank — and then a `fprintf` saying `is not modelled` followed by a
silent return. Every reference equip and unequip path is
`inv_moveitem(inv, worn, …)` or `inv_moveitem(worn, inv, …)`, so all of them did
nothing at all, quietly, while `gen_opcode_coverage.py` reported the opcode
**covered** — correctly, by its own definition, because a `case` label is the
whole of what it can see. The three bank arms are preserved byte for byte in the
new file and the generic container-to-container arm is appended *after* them, so
no bank behaviour can change by the move.

**Overflow goes on the floor, and that is ported rather than chosen.** All three
moves can be asked to put more into a container than fits; `InvOps.ts:339-348`,
`:522-530` and `:245-253` all answer the same way and this is a port of that
answer — the remainder becomes a ground obj at the player's tile, **singly** for
an unstackable obj (which is what makes twelve dropped bones twelve piles) and as
one pile otherwise. The reference's literal `200` duration is
`mock230_ids()->lootdrop_duration` here: the same number, resolved through the
pack instead of restated in C (PORTING_GUIDE §2.4 item 2).

One limit inherited rather than introduced: `mock230_container_add` still takes
**one slot for all units** of an unstackable obj, because `InvType.stackType`
needs `fields/inv.ini` and that does not exist yet. `inv_movefromslot` and the
generic `inv_moveitem` arm inherit it.

### What was deliberately left to the loud stub

`docs/osrs230_mockserver.md` §3.13d's rule — *an opcode that cannot be answered
from real data is better left to the VM's loud stub than implemented from a
plausible guess* — is the reason each of these is absent rather than approximated.
All are `known = 1`, so the stub pops the declared arguments, pushes zeros, and
reports once per env: the stacks stay consistent and the gap stays visible. Counts
re-measured over `LostCity_Server/content` today (see below for the method):

| op | uses / files | why not |
|---|---:|---|
| `npc_walk`, `npc_walktrigger` | 90 / 44, 2 / 1 | `NpcOps.ts` queues a **waypoint** the tick drains. `struct Mock230Npc` has no destination and no queue. Faking it as a teleport would make 44 files *look* ported while every npc arrived instantly — worse than the stub, because it is silent. |
| `loc_anim` | 51 / 21 | missing **wire packet**, not missing data: `World.animLoc` is a zone event and this server's `zone_sub_opcode` enumerates no loc-anim. |
| `loc_category`, `lc_category` | 38 / 16, 3 / 1 | the linked rscache decoder throws the bytes away — `dat2_config_loc.c` has `case 61: g2(buffer); // Skip`. Landing it needs opcode 61 confirmed against the OSRS `LocType` reference *and* an `EXCEPTIONS.md`-governed edit to the vendored tree. |
| `npc_changetype_keepall` | 31 / 15 | verified identical to `npc_changetype` in *this* engine — the only difference upstream is re-deriving a per-npc `levels[]` store that does not exist here. One fallthrough line by whoever owns that switch. |
| `npc_statheal` / `statsub` / `statadd` | 16 / 10, 9 / 6, 1 / 1 | all write `npc.levels[stat]`; here `npc_stat` reads the content block and hitpoints is the only number that moves. Writing a value nothing reads is worse than the stub. |
| `npc_sethuntmode`, `npc_sethunt` | 14 / 7, 1 / 1 | one *feature* wearing five opcodes (a `hunt` namespace, per-npc huntMode/range, a HuntVis test, a per-tick pass). `npc_hunt`/`npc_huntall` are the two of the five that are pure searches. |
| `npc_heropoints` | 14 / 14 | needs per-player damage attribution plus the drop consumer; `npc_findhero` pushes the constant 1 because there is one player. |
| `npc_arrivedelay`, `npc_inrange` | 2 / 2, 2 / 1 | both read per-npc fields the mover and the interaction machinery would have to write. |
| `lc_desc` | 0 / 0 | **measured: 0 of 62,194 loc records carry a `desc`.** The field is gone from OSRS loc configs at this revision; a handler could only ever push `'null'`. |
| `lc_debugname` | 1 / 1 | `debugname` is a LostCity build-time symbol; the dat2 record has no such field. |
| `buildappearance` | 18 / 7 | **the sharpest §3.13d case so far.** In the reference it is two lines — `this.appearanceInv = inv; masks \|= APPEARANCE` — and the first of them is *read*: `Player.ts:1366` builds the appearance out of `getInventory(this.appearanceInv)`. Its job is **selecting which container the encoder reads**. `put_appearance` (`mock230_encode.c:915`) reads `player->worn` unconditionally, so accepting the argument and raising the mask would make `buildappearance(<anything else>)` silently paint the worn set — plausible, wrong, quiet. The mask half meanwhile is already *unforgettable* here and is not in the reference: the worn container is adopted with `appearance = 1`, so every ServerScript write to it raises `MOCK230_PMASK_APPEARANCE` through `mock230_container_mark`. Implementable the day the encoder gains a selectable source, and not before. |
| `p_clearpendingaction` | 20 / 18 | real, and it was **misfiled** as an equipment blocker for a week: `mock230_world_clear_pending_action` is called from `handle_move`, `opnpc`, `opobj`, `oploc`, `opheldu` and `useon_interact` and never from `handle_opheld`. It belongs to the worn-tab unequip (`[inv_button1,wornitems:wear]` opens with it), which is a different move. |
| `nc_desc`, `npc_findallzone`, `obj_param` | 0 / 0 | no caller anywhere in the reference. |

`LC_OP`, `OC_IOP` and `OC_OP` remain the sharper case one rung up: they are
`known = 0`, so the VM refuses to execute them at all rather than guessing an
arity (see *What is not ported, on purpose*). `oc_desc` was written and then
**removed** on the same ground.

Seven of the npc refusals are blocked on the same thing — per-npc engine state in
`mock230_world.c`. The next npc item is not an opcode.

### Measured, 2026-08-01, and re-run 2026-08-02

Everything above is measured on this tree against `cache.osrs239` and the
reference at `/Users/matthewevers/Documents/git_repos/LostCity_Server`. Two
numbers this run measured differently from the prose it inherited, both from
**census method**, both in the same direction:

- **Zero-argument commands are invisible to a census that requires a `(`.** They
  are written bare in RuneScript. `last_useitem` is `800 uses / 213 files` — six
  times `loc_param`, and the single largest opcode gap in the reference tree — and
  scored 0 under the old pattern. Same class: `loc_category` 38/16, `npc_category`
  13/9, `npc_arrivedelay` 2/2, `npc_inrange` 2/1.
- **`content/scripts/_unpack/` and `_test/` are not authored content.** Including
  them inflates `npc_walk` from 90 to ~108 uses and `loc_param` from 133 to ~140.

The counting method used here, stated so it can be re-run: all `*.rs2` under
`LostCity_Server/content` **excluding** `engine.rs2`, `scripts/_unpack/` and
`scripts/_test/` (1,266 files); `//` comments stripped; inside a `"…"` literal only
`<…>` interpolation is scanned (nine `struct_param` call sites live inside message
strings and vanish if the whole literal is dropped); the RuneScript sigils
`$ % ^ ~ @` exclude a match, a leading `.` does not — it is a real call form.

Reference command surface, re-counted: `engine.rs2` declares **511 `[command,…]`
lines = 359 unique names** (355 plain plus the four `queue*` vararg forms) with
**152 `.`-prefixed secondary-entity variants**, which share an opcode with the
plain form.

Engine readiness against the reference tree, measured with the coverage header
above:

```
distinct commands used by content       318;  190 implemented,  128 missing
scripts using only implemented commands 770 / 1,266  (60.8 %)
```

That readiness figure is a lexical scan, so it is a *lower* bound: a name that
also appears as a field or a local counts as a use. It is nonetheless ~8 points
below the number recorded before this run, and the whole difference is the
zero-argument blindness above — `last_useitem` alone blocks 213 files.

State of the tree at the end of the run, all re-run rather than quoted:

```
test-mock230-coverage      current, 260 / 401
test-mock230-param         all checks passed
test-mock230-loc           all checks passed
test-mock230-npc           all checks passed
mock230 --selftest         all checks passed, exit 0
mock230_pack --check-only  0 error(s), 15 warning(s)
```

Re-run 2026-08-02 after the `obj_*` and `inv_*` files landed: same lines, and the
pack's warning count is **15**, not the 13 this block said — the two new ones are
pre-existing content warnings surfaced by content added in the same window, not
by these opcodes.

### Testing a domain file

```
make -C src test-mock230-coverage    # the generated table is not stale
make -C src test-mock230-param       # the *_param family
make -C src test-mock230-loc         # loc_* / lc_*
make -C src test-mock230-npc         # npc_* / nc_*
```

**There is deliberately no `test-mock230-obj` and no `test-mock230-inv`.** Both
were considered and refused on the same ground, which is worth stating because
the absence otherwise reads as an omission: a standalone VM fixture is the right
shape for a family that is a *pure function of cache data* — which is what
`param`, `loc` and `npc` are — and the wrong shape for a family whose whole
subject is server state. `obj_takeitem` is only meaningful against a live
ZoneMap, a container registry and the real dispatch; `inv_moveitem`'s new arm is
only meaningful against two real containers. All three exist in exactly one
place, `mock230 --selftest`, so that is where the permanent checks went
(*"taking an obj is content's"*, *"equipping is content's rule"*). A fixture here
would have covered strictly less for more code.

Each of the three family tests is two halves, and the shape is worth copying:

**Half one re-decodes the config group with the same rscache decoder the loader
used, and asks the table about *every* record it saw** — both directions. The
failure mode is a *miss*, and a miss returns a neighbouring record's answer, so a
spot check has to be lucky and an exhaustive walk cannot be. It also asserts the
*preconditions of its own assertions*: `param_test` fails if the count of
out-of-order records reaches zero, because at that point the sort it is testing
would be untestable.

**Half two compiles RuneScript, runs it on the VM, and reads the string stack.**
This is the only way to catch wrong-stack routing: `def_string $s =
struct_param(...)` underflows and aborts inside the VM when the value went to the
int stack, where an int-stack assertion on the same value would pass while the two
stacks were out of step. The domain files never touch `struct Mock230Server`
beyond the roster, so these bind with a NULL or zeroed host user — no world, no
socket, no pack. No ids are written into the test sources; every subject is found
in the decoded data at run time.

Every assertion was proved able to fail by mutating the implementation and
re-running — the discipline `docs/LOSTCITY_PORT_TRIAGE.md` §10.3 sets out. Three
of those runs are worth recording because the *first* version of the test passed
under the mutation:

- The first "declared int-ish param" subject was declared `i`, so a handler that
  routed only `type=i` to the int stack passed. The test now insists its int
  subject is declared something other than `i` (it lands on `'1'`) and asserts
  that fact.
- The first authored-overlay subject was `attackrate = 4` against a declared
  `default=` of **4**, so "read the overlay" and "fall through to the default"
  produced the same number. The search now requires the two to differ.
- Three npcs in a row on one axis rank identically under Chebyshev and euclidean.
  The layout now puts candidates at (+1,0), (−1,0) and (+1,+1) so that one
  assertion has three distinguishable answers: correct, Chebyshev-ranked, and
  first-tie-kept.

And one trap: a membership assertion cannot catch a swapped coord/distance
argument, because a coord literal packs to a number in the millions and a radius
of millions finds everything. `npc_huntall` at distance 0 collecting **nothing**
is what catches it.

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
- **A call's argument count is checked against the callee's header.** The
  declare pass already reads every `[proc,name](int $a, string $b)`, so it
  records the two counts and `parse_call` compares. Without it a wrong-arity
  `~proc()` compiles, and the callee pops what its header declared — leaving the
  stack skewed for everything after it, so the damage surfaces as an unrelated
  command reading someone else's value. This is the same failure class
  `test-ss-verify` catches in the corpus, and nothing was catching it in a
  tree's own content; it found two stale call sites the moment it was turned on.
- **A stat argument is resolved with a kind hint, because the bare name is
  ambiguous.** `SSC_SymbolsFind` with no kind returns the lowest-numbered kind
  holding the name, and `cache.osrs239` uses three of the 23 stat names for
  something that sorts earlier: `hitpoints` is also param 2100, `attack` varp
  259, `fishing` loc 20926. So `stat_heal(hitpoints, 3, 0)` compiled to
  `stat_heal(2100, 3, 0)` and healed nothing. `parse_command` sets
  `arg_kind_hint = SSC_SYM_STAT` for the `STAT*` / `NPC_STAT*` family only, so a
  bare `fishing` anywhere else still means the loc. The reference's typed
  argument lists make this a non-problem for it; this compiler has no types, and
  the hint is the narrowest thing that works. The mock's stat commands abort on
  an out-of-range id as the second half of the same fix.

  **The hint does not reach a comparison, and there is no safe spelling for one.**
  `if ($stat = attack | ...)` has no command to take a hint from, so `attack`
  resolves to varp 259 and the test is false for every Attack requirement in the
  game. That is not hypothetical: it was the first version of
  `~levelrequire_gates_wearing` (`skill_combat/scripts/levelrequire.rs2`,
  2026-08-02) and it made **357 of 1,496** gated items wearable at level 1 —
  found by an exhaustive selftest leg, invisible to reading. The answer is that
  a stat literal belongs in a **config key**, never in an identifier position:
  a `.enum` declaring `inputtype=stat` has its keys resolved by the content
  loader against that type and cannot see the symbol table at all. See
  `skill_combat/configs/levelrequire.enum`, and `general/configs/stat.enum`,
  which had been relying on the same property without saying so.

  The general form, worth carrying to the next bare-name enumeration
  (`locshape`, `npc_stat`): **the compiler can only disambiguate a name it sees
  as an argument.** Everywhere else, put the name in data.

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
