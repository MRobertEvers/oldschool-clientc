# M0 implementation register

M0 discovery and baseline preparation are complete for milestone sequencing.
Every mutation family and discovered consumer has a recorded disposition; the
implementation gaps below remain requirements of M1–M6. This is not acceptance
of complete tree-mutation correctness. M1 now has major-3 declarations, shared
policy tests and observed failing native conformance cases, now repaired in the
[first M2 checkpoint](M2_PROGRESS.md). M2 remains in progress. The production host
still uses V2 until the coordinated cutover.

## Workspace and evidence

Work is isolated on `codex/plugin-engine`, based on PR #82 head `9fca0e47a`.
The original dirty workspace, its submodules and the uncommitted plan are
preserved. No stash or shared-tree cleanup was used.

Local evidence is under `/private/tmp/plugin-engine-evidence`. Each capture has
`fixture.json`, its complete preferences, player seed provenance, client exit
status, native trace, BMP and pixel scores. `fixture.json` hashes the binary,
manifest, cache/pack files and revision profiles. LostCity's eight downloaded
JAG archives are checked against its nine-entry login CRC response. Server and
content commits and dirty source hashes are recorded. These are preflight
checks, not proof that every mutable source input has been covered.

LostCity engine: `55e89e60209a958f0499b35f26cade1b17cdab94`, content:
`b6e11d98c1542221286af3ceb63c635b6469ba0e`. The reproducible fixture helper
`tools/gameframe_lostcity_fixture.py` creates isolated worktrees, installs the
locked dependencies, applies the checked-in Node worker patch and builds all
cache/script content from source. Bun can build the pack but cannot run this
engine's `node:sqlite` dependency; the pinned runtime is Node 24.5.0. The helper
derives the public key from the actual private key because the upstream public
PEM has reversed values. Original key files are untouched.

`/private/tmp/plugin-engine-lc-pinned` is the independent prepared fixture on
TCP 43894 / HTTP 8982. `fixture-build.json` hashes 10,776 source/config inputs
and 76 packed files; serving rejects changed inputs or a changed pack. Each
connection gets a different account copied exclusively from the test-owned
seed. The original LostCity server stays running and unchanged.

The cold-stream tests exposed a harness bug: pipeline iterations could consume
all test frames before login/mount completion. `TORIRS_SIM_AFTER_READY=1` now
waits for a ready game tree, logged-in network and idle pipeline, then advances
scenario ticks at 20 ms intervals while retaining the ordinary native logic
clock. The existing harness enables it and rejects missing readiness evidence.
`lc-rebuilt-contract` failed on the old clock; `lc-ready-contract` and the
independent `lc-replay-keyed-contract` pass both real packet/CS1 scenarios.
The latest 2× controls show 19 skill cells, Strength 20/20, and exactly the two
skill-guide operations plus Close Window.

OSRS content is now `26aba4540c` on `codex/plugin-engine-content`, in
`/private/tmp/plugin-engine-content`. The source corrections preserve native
script IDs: 1,922 proc headers are backed by decoded native call edges, and
107 renamed call/hook references are repaired against native GOSUBs or decoded
hook expressions. All 60 initially repaired hook references were checked against
the native AST; a final type-12 focus hook was repaired the same way. Explicit
`script<ID>` calls retain their native IDs where trigger names differ. The
compiler's ambiguous clientscript-to-proc fallback remains disabled.

The full base-cache recipe now succeeds: **189,969 config records, zero failed**,
all 23 required tables present, and no script compile failures. The Ancient
Curses overlay passes its isolation/compile gates and post-bake decompilation
checks. Its server pack rebuilds **29,603 scripts**. All three normal freshness
predicates report up to date. Logs: `/private/tmp/plugin-engine-cache-pass4.log`,
`/private/tmp/plugin-engine-curses-preparation.log`, and
`/private/tmp/plugin-engine-fresh-scripts.log`.

`prepared-native-baselines` passes **0/4 failures**, and
`prepared-native-window` passes the Cocoa native launch, with the prepared
cache, scripts and explicitly selected matching content tree. No stale-script
bypass or diagnostic acceptance override is used. The existing foundation
matrix also passes **0/40** on the freshly prepared cache. These establish the
preparation/baseline checks, not complete tree-mutation correctness.

`prepared-native-contract` also passes **0/10 failed groups, 13 captures** on
that prepared fixture, including minimap packets, server hides, resize/tab
selection and remounts. Root commit `ae29d3d20` was checked out cleanly and its
native client and content-tool/fixture tests built/passed. Tool builds modify
the repository's two tracked generated executables; those binary changes are
build outputs, not additional source changes or a shipped API migration.

Remaining content diagnostics are explicit: the source for sailing interface
937 is byte-identical to the pinned cache's native export, but its partial
`.compack` naming file is incompatible with that source; the asset pass retains
the base interface. Hosidius group 236 has neither source nor a native cache
group and remains absent. The packer also reports 3,002 server-side inventory
keys it cannot encode into the client cache. Their scope must remain visible
in the inventory; none is evidence that a missing native capability exists.

## Implemented mechanisms and observed controls

* `tools/gameframe_matrix.sh` now selects rs289lc or OSRS239, supports native
  baselines without plugins, and runs revision-specific scenarios through the
  existing capture/scoring path. It rejects reused output directories, missing
  captures, failed client exits and rejected fixture provenance. Diagnostic runs
  cannot return an acceptance exit code.
* `tools/gameframe_fixture.py` reuses launcher manifest/freshness parsing. It
  rejects stale checks, missing coverage, mismatched checked/consumed paths,
  wrong revision/codec/logic and inherited freshness bypasses (including `=0`).
* `NATIVE_UI` records incarnation, parent, bounds, native hide and separate
  paint/input availability. `NATIVE_CS1` records evaluated state changes.
  Packet traces distinguish receipt from applied stat values. Nothing assumes
  an asynchronous hide has applied merely because it was received.
* A baseline launch defect was reproduced and fixed in `net/loginproto.c`:
  LostCity interprets RSA ciphertext as a signed BigInteger. The unsigned
  magnitude needs a leading zero when its high bit is set. The independent
  modular-exponentiation vector in `net_login_test.c` failed before the fix and
  passes afterward for both the one-byte and escaped revision encodings.

Observed native runs:

| Evidence directory | Result and limits |
|---|---|
| `rs289-contract-isolated` | 0/2 failed groups. Strength changes through the real UPDATE_STAT → CS1 path; both value components read 20. Skill-guide packets arrive before mounting; eight control layers remain hidden and the two-operation layer is shown. Native paint and input agree with final hide state. |
| `negative-cs1` | Actual client with CS1 evaluation disabled: CS1-value gate fails while the four chat controls still pass. Restored evaluation passes. |
| `negative-rs289-server-hide` | Actual client with `UITree_SetHideAt` disabled: packet gate passes, final hide/availability gate fails. Production source is restored and rebuilt. |
| `osrs239-native-diagnostic` | 0/4 capture failures for 548/161/164/601 with plugins disabled; fixture acceptance remains blocked. |
| `rs289-native-window` | Real Cocoa native launch and exit bitmap, no freshness bypass. Four chat controls and fourteen sidebar controls inspected at 2×. |
| `osrs239-native-window` | Real Cocoa native launch; eight filters inspected at 2×. Diagnostic because full content preparation is blocked. |

The rs289 Stats screenshot has **19 skill cells**, including Strength **20/20**.
The skill-guide screenshot has Attack and Defence operations plus Close Window;
the other server-hidden operation groups are absent. OSRS desktop screenshots
have eight filters and six readable mode cells; mobile has seven filters and
six mode cells. These counts do not establish every action's correctness.

The native Cocoa rs289 capture has a 1530×1006 drawable with the native fixed
765×503 frame at its upper left. The dummy-driver reference is 765×503. This
logical/drawable behavior remains an explicit frontend question, not a claim
that arbitrary native rs289 resizing works.

## Measured native surfaces

Coordinates are canvas pixels from the instrumented dummy-driver runs, with
plugins disabled and a requested 765×503 window. Each cell is `x,y w×h`.

| Revision/root | World | Minimap | Compass | Chat |
|---|---|---|---|---|
| rs289lc | 4,4 513×335 | 575,9 146×151 | 550,4 33×33 | builtin 17,357 479×96 |
| OSRS 548 | 4,4 512×334 | 570,9 145×151 | 545,4 32×33 | 162:0 at 0,338 519×165 |
| OSRS 161 | 0,0 765×503 | 607,8 152×152 | 588,5 35×35 | 162:0 at 0,338 519×165 |
| OSRS 164 | 0,0 765×503 | 607,8 152×152 | 588,5 35×35 | 162:0 at 0,338 519×165 |
| OSRS 601 | 0,0 765×503 | 590,8 152×152 | 571,5 35×35 | 162:0 at 11,0 519×145 |

## Mutation discovery and dispositions

`tools/plugin_engine_inventory.py` extracts the complete tree/owned-record
schema with Clang, discovers repository consumers and enumerates the full CS2 opcode table, all 648 hosted requests and
312 direct UI requests, including overlay and anti-drag operations. Optional `--compile-log` resolves field references in translation
units from an actual build using Python libclang. The generated register keeps
unreviewed entries blocking. Lexical matches can refer to other structs; typed
accesses still need phase/alias/bulk-write review. Counts are not proof of closure.

The examined build has 408 C translation units and now parses with **zero
failures** using the same Xcode libclang and builtin headers as the compiler.
Lvalue classification distinguishes index reads, owned-array writes, pointee
writes, address escapes and local value assembly, and handles macro operators
through Clang's operator API. Tests pin those distinctions. The registered
1,560 mutating references are in `m0-mutations.json`; their dispositions retain
construction/async-publication, native mutation, local assembly and derived
cache distinctions. Unselected preprocessor branches still need final review.

| Mutation family | Mechanisms read / current evidence | Blocking disposition |
|---|---|---|
| Identity/topology | `uitree.c`: Push, Clear, reclaim, Reparent, CcCreate/Copy/Delete/DeleteAll; incarnation allocation. | Audit bulk copies, subtree reuse, role/index invalidation and every async completion. Existing incarnation support is incomplete evidence. |
| Hide/availability | SetHideAt, App_IfHideSet/reapply, uitree_host native paint/input, replacement-chain presentation. Real rs289 pending hides and restored negative control. | Complete cross-feature descendants/controller and all lifecycle sequences; do not reuse OSRS minimap meanings on rs289. |
| Geometry | Native setters retain requested geometry under frame ownership. GetLayoutWidth/Height and GetRelativeX/Y call EnsureLayoutFor. | Settle allocation/readback semantics, zero-size fallback, ancestor/descendant propagation and one-script read-after-write under allocations. |
| Scroll/clip | Native scroll setters; frame_stretched changes ancestor clipping. | Replace implicit broad escape with an explicit policy; measure scroll extent/position/clipping under moved native descendants. |
| Opacity/appearance | Typed trans/colour/image setters coexist with direct CS2 writes to flips, line width, trans_bot, active graphic and fill. | Define self/subtree and per-property ownership, state mappings and all paint/resource invalidations. |
| Animation | CS2 directly updates sequence/frame/cycle/orthographic fields; app_player_model_poll writes native pose state. | Classify tick and async writers and preserve native animation beneath skins. |
| Live content | Real UPDATE_STAT → CS1 readback; packet inventory clears directly mutate item/icon fields. | Audit text/CS1 variants, inventory epochs, asynchronous icon resources and release to current native content. |
| Operations/hooks | CS2 direct drag/deadzone/no-click-through writes; mutable menu/options/hook blocks are accessible internally. | Classify action labels, masks, listener/index registration and callback authority, including bulk/alias writes. |
| Retained interaction | Input/drag and menus have some incarnation checks. Focus currently stores only component ID; pending CS1 evaluation retains an array cursor across awaits. | Prove focus, press/release, drag, menus, tooltip and queued callback outcomes for delete/reuse/reparent/remount. |
| Composition/lifecycle | Existing frame host validates anchors and scopes active providers. | Specify exclusive property conflict outcomes, additive ordering, transactional enable/failure/reload and resource replacement for C/Lua together. |

## Consumer checklist

[`m0-consumers.json`](m0-consumers.json) records every discovered consumer,
plugin definition, widget request and packet dispatcher case. Current discovery
finds all 13 C products, Lua runtime, six product scripts, ten probes/demos and
23 C test plugin definitions. It also includes disabled manifests, Lua metadata,
plugin chrome executors/archived snapshots, frontend bridges and API examples.
All ports are **pending**. User preferences are marked preserve-user-data.

The 163-file discovery list has explicit host/plugin/test/manifest/archive/data
dispositions and needs a final independent review for indirect build
references and disabled branches; discovery scripts introduced in this pass are
themselves tooling consumers. No API has been removed or port declared complete.

## Reproduction

Build the source separately from installed runtime data:

```sh
make -C src OPT=0 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_plugin_engine \
  PLATFORM_TARGET=torirs_plugin_engine torirs_plugin_engine -j8
make -C src test-gameframe-fixture test-net-login test-net-loopback test-net-exec test-cs1 test-uitree
python3 tools/plugin_engine_inventory.py --out /tmp/plugin-engine-inventory.json
```

For typed discovery, install Python `libclang` in an isolated virtual environment
and add `--compile-log /path/to/the/full-build.log`. Parse errors remain blockers.

Use a fresh output directory for each harness invocation:

```sh
REPO=/path/to/installed/runtime BIN=/path/to/exact/client \
GF_MATRIX_REVISION=rs289lc GF_MATRIX_LC_SERVER=/path/to/LostCity_Server \
GF_MATRIX_LC_SAVE=/path/to/test-owned-gameplay-seed.sav GF_MATRIX_SCENARIOS=1 \
tools/gameframe_matrix.sh /tmp/rs289-new-run

REPO=/path/to/installed/runtime BIN=/path/to/exact/client \
TORIRSSERVER_CONTENT=/path/to/matching/OSRS-Content/osrs239-content \
MANIFEST=/path/to/prepared-osrs239.ini GF_MATRIX_BASELINE=1 \
GF_MATRIX_TAGS=m01,m11,m21,m31 tools/gameframe_matrix.sh /tmp/osrs239-new-run
```

Set `SDL_VIDEODRIVER=cocoa` for the macOS native-window run. Default captures
use SDL's dummy driver and the real software renderer. Never set the stale-script
override for accepted runs. `GF_MATRIX_DIAGNOSTIC=1` exits 3 even if pixels pass.

## Continuing acceptance obligations

1. Enforce the recorded producer dispositions in M2, including indirect/bulk
   writes and construction/publication boundaries; verify unselected frontend
   branches at M6. The inventory does not claim these paths already conform.
2. Keep the completed OSRS preparation recipe pinned through the API cutover;
   close the remaining content-diagnostic dispositions and verify final commits
   against their rebuilt artifacts. The earlier targeted cache is no longer the
   baseline.
3. Preserve the completed source-built LostCity fixture. The native fixed
   layout versus drawable-size behavior remains a frontend contract item for M4.
4. `m0-performance.json` records optimized native, mutation and multi-plugin
   baselines on both revisions, excludes startup samples, and sets explicit
   comparative budgets for M6. The rs289 multi-plugin run uses the native frame:
   Classic Fixed currently declines its missing native orb surface. That is an
   M3 adapter/port requirement, not a fabricated native surface or a passing
   custom-frame result.
5. Apply the M1 ownership/action/capability rules in both adapters and all ports.
   Major-3 policy tests pass; the native conformance target is intentionally red
   for hide/content independence, zero-size readback and recycled-ID focus.

## Confirmed implementation gaps to carry into conformance

These are source findings, not claims that the new contract is implemented:

- `UITree_ApplyObject` can unhide an item node and a silhouette sibling; native
  hide authority must not be acquired implicitly by a content update.
- Speculative mount hiding shares `UITree_SetHideAt` with explicit script/server
  hides; opening a mount must not clear an independently authored native hide.
- Focus and queued drag pickup retain plain component IDs. Retained gesture
  references partly use incarnations, but that does not cover these paths.
- `UITree_Reparent` rejects self-parenting but does not reject a descendant cycle.
- `UITree_CcCopy` copies some fields explicitly and others through its union;
  owned payloads and native-state completeness need a classified copy operation.
- `GetLayoutWidth/Height` fall back from resolved zero to requested dimensions.
- Native `trans` suppresses self paint, not children; drag presentation has its
  own subtree translation/ghosting semantics. Do not treat them as one opacity.
- The exact rs289 encoder documents states 0 normal, 1 unclickable, 2 blacked out.
  The reference client draws map/compass except state 2 (compass only) and walks
  only in state 0. OSRS states 3–5 must not become rs289 semantics by accident.
