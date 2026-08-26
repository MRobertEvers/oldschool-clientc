# The retiring stack

Nothing in the default build path reaches any of the files below. `make test`
runs the JavaScript-native suites; `make dev` serves the canvas runtime;
`make generated`, `corpus-aot`, `roundtrip-if` and `bench-rebuild` touch none
of it. They remain on disk only so the cutover is reversible.

## Why they go

Each replaced a piece of the C/WASM bridge the redesign removes. The bridge
was measured at 10.7–12.1 ms for a transaction the JavaScript-native path does
in 3.9 ms, because it encoded 22,622 packed mutations per tick and replayed
them — see `CS2_DOM_REDESIGN_PLAN.md` §II.2.

| file | lines | replaced by |
|---|---|---|
| `wasm/cs2vm_wasm.c` + the `wasm` make targets | ~3,000 | `src/cs2_js_emit.js` — CS2 compiles to JavaScript |
| `src/wasm_runtime.js` | 1,610 | nothing; there is no bridge |
| `src/cs2_engine_router.ts` | 829 | nothing; there is one engine |
| `src/cs2_host_adapter.ts` | 680 | `src/host_kernel.js` — direct positional calls |
| `src/cs2_backend_coverage.ts` | 344 | `hostCoverage()` |
| `scripts/audit_ts_backend.js` | — | `scripts/emit_corpus_js.mjs` |
| `src/cs2_vm_core.ts` | 1,060 | the AOT generators; no interpreter needed for the corpus |
| `src/cs2_bytecode_decoder.ts` | 537 | `cs2 decompile --emit ast-json` |
| `src/bytecode.js` | 671 | nothing; the browser owns the scripts as source |
| `src/runtime_worker.js` | 888 | `src/session.js` — one thread |
| `src/runtime_worker_protocol.js` | 193 | nothing to send |
| `src/worker_runtime_controller.js` | 696 | nothing to mirror |
| `src/ui_tree_store.js` | 1,112 | `src/uitree.js` — one tree, no committed projection |
| `src/host_runtime.js` | 6,951 | `src/host_kernel.js` + `host_config.js` + `host_widgets.js` |
| `src/react_tree_renderer.js`, `react_stage_mount.js`, `react_tree_mount.js` | ~600 | `src/painter.js` — one canvas |
| `src/font_runtime.js` | 824 | `src/assets.js` — no cooperative slicing needed |
| `src/dev_page.js` | 2,488 | `src/dev_page_canvas.js` |
| `src/dev.js` | 859 | `src/dev_canvas.js` |
| `src/preview.js` | 577 | `src/layout.js` + `src/emit.js` |
| `wasm/cs2_host_executable_semantics.json`, the review manifests | — | nothing; there is no eligibility gate |

Their test suites moved with them: `make test-legacy` still runs
`test/run_all_tests.js`, which is the only thing that reaches them.

## Deleting them

**Several are untracked in git and cannot be recovered once removed** —
`cs2_engine_router.ts`, `cs2_host_adapter.ts`, `cs2_backend_coverage.ts`,
`cs2_vm_core.ts`, `cs2_bytecode_decoder.ts`, `ui_tree_store.js`,
`react_tree_renderer.js` and their tests among them. That is why this file
exists instead of the deletion: destroying uncommitted work is not something
to do on an inference about intent.

Once the tree is committed, the deletion is:

```sh
## Before running any of this: eleven of these are recoverable, ten are not

`git status` was checked against the list above. The split matters, because
`git rm` on a file git has never seen is an unrecoverable delete of work
nobody committed:

**TRACKED — git can restore them** (`git checkout HEAD~1 -- <path>`):
`src/wasm_runtime.js`, `src/bytecode.js`, `src/runtime_worker.js`,
`src/runtime_worker_protocol.js`, `src/worker_runtime_controller.js`,
`src/host_runtime.js`, `src/font_runtime.js`, `src/dev_page.js`, `src/dev.js`,
`src/preview.js`, `test/run_all_tests.js`.

**UNTRACKED — deleting them destroys the only copy**:
`src/ui_tree_store.js`, `src/react_tree_renderer.js`, `src/react_stage_mount.js`,
`src/react_tree_mount.js`, `src/cs2_engine_router.ts`, `src/cs2_host_adapter.ts`,
`src/cs2_backend_coverage.ts`, `src/cs2_vm_core.ts`,
`src/cs2_bytecode_decoder.ts`, `scripts/audit_ts_backend.js`.

Commit the untracked ones first if any of them is worth keeping, and only then
delete.

## And two make targets still reach them ON PURPOSE

`make dev-legacy` runs `bin/cs2dom.js dev`, which is `src/dev.js` and
`src/dev_page.js`; `make test-legacy` runs `test/run_all_tests.js` and the
suites it names. They are the fallback the cutover kept while the new stack was
being proved, so "unreachable from the build" means unreachable from `make
test` and `make dev` — not from every target. Deleting the files means
deleting those two targets in the same change.

```sh
git rm -r tools/cs2dom/wasm tools/cs2dom/web
git rm tools/cs2dom/src/{wasm_runtime,bytecode,runtime_worker,runtime_worker_protocol}.js
git rm tools/cs2dom/src/{worker_runtime_controller,ui_tree_store,host_runtime}.js
git rm tools/cs2dom/src/{react_tree_renderer,react_stage_mount,react_tree_mount}.js
git rm tools/cs2dom/src/{font_runtime,dev_page,dev,preview}.js
git rm tools/cs2dom/src/cs2_{engine_router,host_adapter,backend_coverage,vm_core,bytecode_decoder}.ts
git rm tools/cs2dom/scripts/audit_ts_backend.js
git rm tools/cs2dom/test/run_all_tests.js   # and the suites it names
```

and then dropping `dev-legacy` / `test-legacy` from the makefile and
`package.json`.
