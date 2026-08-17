# CS2VM2 optimization notes

Working record for Part B of `docs/CS2_OPTIMIZER_PLAN.md` (§10–§14): the VM's
memory and speed work. Numbers here are measured on this machine (arm64 macOS,
`OPT=1`, clang `-O3`) and are what later work should be compared against.

---

## B0 — baseline `sizeof`, before any change

Measured against the headers as they stood on branch `v3` at the start of this
work (commit `fc163240`), with a one-file program that includes `cs2vm2.h`:

| struct | before | after B1–B6 |
|---|---:|---:|
| `struct CS2VM2_Frame` | 12,352 | **40** |
| `struct CS2VM_HostRequest` | 1,336 | **64** |
| `struct CS2VM2_Thread` | 19,120 | **23,296** |
| `struct CS2VM2` | 76,504 | **23,328** |
| `struct CS2VM2_Script` | 96 | 104 |

Where the "before" numbers came from:

- `CS2VM2_Frame` — `int int_locals[1024]` (4,096 B) + `char* str_locals[1024]`
  (8,192 B) is 12,288 of the 12,352. Every `gosub` memset all of it.
- `CS2VM_HostRequest` — the four `*_seton*` payloads carried
  `int int_args[64]` (256 B) + `char str_args[4][256]` (1,024 B) inline, so the
  union was sized by a member four hundred times bigger than the varp read that
  shares it.
- `CS2VM2_Thread` — two `[1024]` operand stacks (12 KB) plus `arrays[128]`
  (4 KB) plus the frame pointer table.
- `CS2VM2` — `threads[4]`, of which only `threads[0]` is ever run.

The thread grew by 4 KB because `frames[]` went from 128 pointers (1 KB) to 128
inline 40-byte headers (5 KB). That is a trade, and a good one: the 12 KB blocks
those pointers used to point at are gone, along with the 1.5 MB frame free list
that retained up to 128 of them. `struct CS2VM2` is what a running task actually
holds, and it fell from 76,504 to 23,328 bytes (-69%).

`CS2VM2_Script` grew 8 bytes for the lazily-allocated gosub callee cache
(§12.3).

---

## B0 — host-op histogram

`TORIRS_CS2_HOSTSTATS=1` counts every `CS2VM_HostRequest` the VM issues, by
request kind, and dumps a sorted table at exit. It exists so §12.2's fast-path
member list is chosen off measurement rather than intuition.

Implementation: `cs2vm2_host_exec()` in `cs2vm2.c` is the single seam between
the VM and `host_exec` — all 186 opcode call sites go through it. Kind names
come from `cs2vm2_host_kind_names.gen.h`, generated from the enum and
`_Static_assert`ed against `CS2VM_HOST_REQUEST_KIND_COUNT` so the table cannot
drift.

Baseline, `SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=300 ./src/torirs --manifest
manifest_osrs239.ini` (boot to the login screen + gameframe build), **14,874
host ops**:

| kind | ops | share |
|---|---:|---:|
| `vars_read_varc_int` | 2,733 | 18.4% |
| `if_sethide` | 1,945 | 13.1% |
| `cc_create` | 1,824 | 12.3% |
| `cc_setsize` | 1,822 | 12.3% |
| `pushscript` | 1,409 | 9.5% |
| `enum_lookup` | 865 | 5.8% |
| `if_getwidth` | 726 | 4.9% |
| `if_getheight` | 724 | 4.9% |
| `clientclock` | 711 | 4.8% |
| `vars_read_varbit` | 360 | 2.4% |
| `if_getlayer` | 357 | 2.4% |
| `minimap` | 355 | 2.4% |
| `if_gettop` | 234 | 1.6% |
| `cc_find` | 160 | 1.1% |
| everything else | < 1% each | |

Caveat worth carrying: this is a **boot**, not a world. `vars_read_varp` is
0.2% here only because a login screen has no game state to read; it stays on
the fast-path list for that reason.

---

## B1 — sized frames

`CS2VM2_Frame` is a 40-byte header with `int_base/int_count` and
`str_base/str_count` into two per-thread growable stacks. `frames[]` is inline
headers; `g_frame_pool`, `cs2vm2_frame_acquire/release`,
`cs2vm2_thread_frame_grow` and `CS2VM2_FRAME_POOL_MAX` are gone.

Direct measurement (`TORIRS_PERF=1`, 300-frame boot):

| counter | value |
|---|---:|
| `cs2_scripts` | 2,118 |
| `cs2_opcodes` | 101,062 |
| `cs2_locals_zeroed` | **122,376 bytes** |
| `cs2_local_oob` | **0** (absent — counters print only when non-zero) |

The same boot pushes roughly 3,700 frames (2,118 top-level runs plus ~1,600
gosubs, from the pre-B4 `pushscript` count). At 12,352 bytes each that was
**~45.7 MB of `memset` per boot**; it is now 122 KB — about 375× less, and the
1.5 MB frame free list is not retained at all.

`cs2_local_oob` staying at zero across a whole boot is the tripwire from
§14.1 confirming the trailer local counts: no script referenced a slot at or
above its declared count.

Test: `src/cs2vm2/test/frame_slices_test.c` (`make -C src test-cs2-frames`).
It was verified to fail by breaking the slice math twice — once by not
rewinding the tops on `RETURN` (5 failures), once by giving every frame base 0
(4 failures, including the array-handle case).

---

## B2 — request shrink

`sizeof(struct CS2VM_HostRequest)`: **1,336 → 64** bytes, with
`_Static_assert(<= 64)` in `cs2vm2_host.h` so it cannot grow back silently.

The 1,296-byte hook-argument payload moved into `struct CS2VM_HookArgs`, owned
by the opcode handler that builds the request and valid for exactly the
`host_exec` call. What dominates the union now is
`CS2VM_HostRequest_WidgetSetOpKey` at 52 bytes — real scalar payload, not a
buffer — so 64 (4 for `kind`, 4 padding, 56 for the 8-aligned union) is where it
stops.

166 of the 184 `memset(&request, 0, sizeof(request)); request.field = …;` blocks
became designated initialisers; the remaining 18 have a shape the rewrite would
not have handled safely (a trailing comment inside an assignment, a conditional
field) and were left alone — at 64 bytes the memset is eight stores.

§12.6: nine `getenv` call sites in `rs_cs2_host.c` now go through
`rs_cs2_env_flag(name, &cache)`, which resolves once. `TORIRS_DUMP_SETSIZE` was
the one on a real opcode path (`CC_SETSIZE`, 12% of host ops).

---

## B3 — immutable strings, arena arrays

`UPPERCASE`/`LOWERCASE` allocate a fresh pool string instead of rewriting their
operand. They were the only in-place mutators (verified by scanning every
function that pops a string for a write through it, and for `str*`/`mem*` with
it as destination). With that gone:

- `CS2VM2_PushStrFrameLocal` pushes the local's pointer,
- `PUSH_CONSTANT_STRING` pushes the script's operand pointer,
- `CS2VM2_StrEmpty` returns one static `""`.

Strpool traffic over the same 300-frame boot, measured with
`TORIRS_CS2_STRPOOL_DEBUG=1` by A/B-ing the three changes:

| | strings allocated | bytes |
|---|---:|---:|
| before | 264 | 2,666 |
| after | **56** | **690** |

Array cells come from a second per-thread bump pool (`array_pool`) instead of
`malloc`/`realloc`. It is reset with the run — like the strings — but with
`CS2VM2_StrPool_ResetKeepBlocks`, which rewinds the blocks instead of freeing
them: a list rebuild defines the same arrays at the same sizes every time it
runs, and the per-array `realloc`-and-retain this replaced already got that
right. Arrays are now also dropped per run (`array_alloc` back to 0), which the
struct comment always claimed and the code did not do; no script can observe it,
because a handle only ever lives in a string local and the frames are gone.

String-sink audit (the §14.2 entry gate): **no host sink retains a request
string in place of a copy, and nothing `free()`s one.** Every sink copies —
`UITree_ApplyText` and `UITree_ApplyComponentParam` `strdup`, `VarCManager_SetString`
`strdup`s, the op/verb/submenu setters `strncpy` into fixed buffers, chat and
the social queue `snprintf`, `LootStore_*` `strdup`s on every storing path,
`exec_para_height` measures in-call and stores nothing. Three yield sites
(`para_height.text`, `oc_find.query`, and the never-populated
`cc_component_param.str_value` getter path) park a whole-request copy in
`host->pending`, so a string pointer does outlive the call there — but nothing
ever dereferences it: every consumer of `pending` reads int fields only, and the
replay re-executes the opcode and re-pops its operands. That is the one place to
watch if `pending` ever grows a string consumer.

One test changed: `component_param_ops_test.c`'s recording host kept
`request->u.cc_component_param.str_value` and read it *after* the run, i.e. it
was relying on a use-after-free. It now `snprintf`s a copy during the call, the
way the contract requires and the sibling tests already did. The assertion it
makes is unchanged.

Test: `src/cs2vm2/test/string_immutable_test.c`
(`make -C src test-cs2-strings-immutable`).

---

## B4 — host fast paths, gosub callee cache

`struct CS2VM2_HostFastPath` is bound beside `host_exec`
(`CS2VM2_BindHostFastPath`); `RS_CS2Host_FastPath()` implements it. Members
chosen from the B0 histogram: `read_varp`, `read_varbit`, `read_varc_int`,
`read_varc_string`, `widget_metric` (width / height / layer, 12% together),
`client_clock` (4.8%), `script_lookup`.

The gosub callee cache is a lazily-allocated `struct CS2VM2_Script**` parallel
to `opcodes`, populated after a successful resolve. Safe because the provider's
clientscript map never evicts — and `CacheProvider_ClientScriptAdd` now asserts
it is not overwriting an entry, which is the one way a cached pointer could
dangle. (`HMAP_INSERT` zeroes a fresh entry, so `entry->script == NULL` is a
sound test for "new".)

Same 300-frame boot, requests reaching `RS_CS2Host_Exec`:

| | host requests |
|---|---:|
| before | 14,840 |
| after | **7,909** (-46.7%) |

Every fast-pathed kind is gone from the request path — `vars_read_varc_int`,
`if_getwidth`, `if_getheight`, `if_getlayer`, `clientclock`,
`vars_read_varbit`, `vars_read_varp` — and `pushscript` went 1,405 → 0.
`TORIRS_PERF_CTR_CS2_HOST_OPS` is still incremented on the fast path, so
"host ops per frame" keeps meaning the same thing.

Screenshots (`TORIRS_EXIT_BMP`) are byte-identical across B2 → B3 → B4 → B6.

---

## B5 — switch lookup landed, dispatch-flag gating did NOT

**Landed.** Switch tables are sorted by key at decode
(`CS2VM2_ScriptSortSwitches`, called from `cs2vm2_script_from_rscache.c`) and
binary-searched. The table carries a `sorted` flag and the linear scan remains
the fallback, because scripts are also built by hand in the tests and an
unsorted table must stay *correct*, not merely slow. The comparator does not
subtract — keys span the whole int range and `a - b` overflows.
Test: `src/cs2vm2/test/switch_lookup_test.c` (`make -C src test-cs2-switch`),
which uses the linear scan as the oracle for the binary search over the same
shuffled table, `INT_MIN`/`INT_MAX` included.

**Landed.** `gen_opcode_stack.py` emits per-opcode flags
(`CS2VM2_OP_FLAG_CAN_YIELD` / `IS_HOST` / `PURE`) into
`cs2vm2_opcode_stack.gen.h`, and the `PURE` set separately into
**`src/cs2vm2/cs2vm2_op_effects.gen.h`** — the header Part A's `cs2_effects.c`
consumes, with no dependency on the VM's headers. 63 opcodes cannot yield, 44
are pure.

**Did not land: gating the per-op checkpoint on `CAN_YIELD`.** It was
implemented, measured, and reverted. On arm64 at `-O3` it is reproducibly
*slower*:

| | with the gate | always checkpoint |
|---|---:|---:|
| arithmetic | 4.1 ns/op | **3.8 ns/op** |
| recursion to depth 100 | 33.3 ns/call | **31.2 ns/call** |

`yield_cp` never escapes the run loop, so the six "stores" the gate was meant to
save stay in registers; the flag costs a real load from an opcode-indexed table
and a branch the predictor has no pattern for. Tried with the flags in the main
table (48 KB) and in a byte array of their own (8 KB) — both lost. The flags are
still generated and still correct; the run loop simply does not read them. The
reasoning is recorded at the site in `cs2vm2_run_script_body`.

**Not attempted: converting `CS2VM2_RunOp`'s switch to function pointers, and
the per-script RS2 dialect table pointer.** The task explicitly permits skipping
the first, and the second has no target: for an OldSchool script the dialect
test is already one perfectly-predicted branch on a field the frame just loaded.
The 43-entry linear scan behind it only runs for RS2-era caches, and the table
is grouped by comment rather than sorted, so making it searchable would cost
more in readability than it buys.

Benchmark harness: `src/cs2vm2/test/vm_bench.c`, `make -C src bench-cs2-vm`.
Current numbers:

```
  gosub (1 call/iter)             39.6 ns/call
  arithmetic                       3.9 ns/op
  recursion to depth 100          31.2 ns/call
  VM acquire+release              67.7 ns/vm
```

---

## B6 — one thread per VM

`CS2VM2_MAX_THREADS` is 1; the field stays an array. `sizeof(struct CS2VM2)`
23,328 bytes, down from 93,016 with four threads (and 76,504 before any of this
work). A `Task_CS2Run` parked on a yield holds that for its whole life, so this
is the number that multiplies by the number of concurrently parked scripts.
