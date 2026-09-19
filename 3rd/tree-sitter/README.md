# tree-sitter (vendored)

The tree-sitter C runtime, v0.26.12, from
<https://github.com/tree-sitter/tree-sitter>. MIT licensed; see `LICENSE`.

Vendored rather than depended on: the language server that uses it
([`tools/runescript-lsp`](../../tools/runescript-lsp)) has to build on macOS,
Linux and Windows from a checkout, with no package manager step.

## What is here

```
include/tree_sitter/api.h    the public header
src/                         the runtime; src/lib.c is the unity that builds it
```

`src/lib.c` `#include`s every other `.c`, so one translation unit is the whole
runtime:

```
cc -c -O2 -std=c11 -I3rd/tree-sitter/include -I3rd/tree-sitter/src \
   3rd/tree-sitter/src/lib.c
```

## What was left out

`lib/binding_rust`, `lib/binding_web`, and `lib/src/wasm/` — the Rust and
JavaScript bindings, and the WebAssembly stdlib blob. `wasm_store.c` is kept
because `lib.c` includes it and `parser.c` calls into it; its body is behind
`#ifdef TREE_SITTER_FEATURE_WASM`, which is not defined, so what compiles is
the stub half.

## Upgrading

Replace `include/` and `src/` from a release tarball's `lib/`, drop the same
three directories, and re-run both grammars' generators — a generated parser
declares the ABI version it was built for, and the runtime refuses one it does
not implement:

```bash
make -C tools/runescript-lsp grammars
make -C tools/runescript-lsp && make -C tools/runescript-lsp test
```

The grammars that use it are `tools/tree-sitter-runescript` and
`tools/tree-sitter-runeconfig`; their `src/parser.c` is checked in so that
building needs no Node.
