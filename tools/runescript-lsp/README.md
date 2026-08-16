# `runescript-lsp` — language server for RuneScript and the content tree

Syntax highlighting and intellisense for the two languages this repo authors
content in, and for the declaration files they resolve against:

| what | files |
| --- | --- |
| ServerScript | `.rs2` |
| ClientScript | `.cs2` |
| records | `.npc` `.obj` `.loc` `.inv` `.enum` `.struct` `.param` `.seq` `.spotanim` `.varp` `.varc` `.varbit` `.dbtable` `.dbrow` `.dbi` `.if` `.idk` `.mesanim` `.flo` `.flu` `.hitsplat` `.healthbar` `.mapelement` `.overlay` `.underlay` |
| allocations | `pack/<ns>.alloc` |
| name indexes | `.pack` `.compack` |
| membership | `pack/<ns>.client` `pack/<ns>.server` |
| constants | `.constant` |
| spawns | `.spawn` |

The editor front end is [`tools/vscode-runescript`](../vscode-runescript);
this is the server it talks to. It also speaks plain LSP over stdio, so any
client works — Neovim, Helix, Emacs `eglot`, Zed.

---

## Building

```bash
make -C tools/runescript-lsp            # -> tools/runescript-lsp/runescript-lsp
make -C tools/runescript-lsp test       # the protocol suite
```

Windows, or anywhere you would rather use CMake:

```bash
cmake -S tools/runescript-lsp -B build-lsp
cmake --build build-lsp --config Release
```

No Node and no tree-sitter CLI are needed: both parsers are generated and
checked in. Regenerating them after a grammar edit does need Node:

```bash
make -C tools/runescript-lsp grammars
```

---

## What it knows

Two things, joined:

**Structure**, from two tree-sitter grammars —
[`tools/tree-sitter-runescript`](../tree-sitter-runescript) for `.rs2`/`.cs2`
and [`tools/tree-sitter-runeconfig`](../tree-sitter-runeconfig) for the
declaration files. Both parse 100% of this repo's corpus (11,791 scripts and
4,899 declaration files); `make -C tools/tree-sitter-runescript test` and the
parse sweeps in each grammar's README are how that is kept true.

**Names**, from an index of the workspace. Every declaration site is recorded
separately, because they answer different questions:

```
%bankpin_code
    record       configs/…/bankpin.varp   [bankpin_code]     — what it is
    allocation   pack/varp.alloc          5727=bankpin_code  — the id we gave it
    name index   configs/all.varp.compack 5727=bankpin_code  — the id the cache knows
    membership   pack/varp.server         bankpin_code       — which half owns it
```

Go-to-definition offers all of them rather than choosing one.

### Qualified names

`fletching_table:product` is a column declared inside a table, `bankmain:items`
a component inside an interface. Clicking one reaches both halves, most
specific first:

```
fletching_table:product
    fletching.dbtable:21   column=product,namedobj,int   — the column
    fletching.dbtable:18   [fletching_table]             — the table
    pack/dbtable.alloc:43  22=fletching_table            — the table's id
```

A declaration always sorts ahead of a site that merely uses the name:
`[opheldu,bow_string]` is a handler bound to an obj, not where the obj comes
from, so go-to-definition on an obj with a dozen handlers still reaches the obj
first.

The decompiled client corpus writes `interface_774:48`, where 774 is an id and
the cache calls that interface `toa_partydetails`. `pack/3_interfaces.pack`
supplies an `interface_<id>` alias for every interface, and a qualified lookup
re-spells the left half through it before searching — so
`interface_774:48` lands on the component's line in
`interfaces/toa_partydetails.compack`.

### Built-in commands

The engine's own vocabulary comes from the tables that define it, not from a
copy: server opcode names and arity from `src/serverscript/ss_meta.c`, client
command names and typed prototypes from `3rd/rscache/src/cs2`, trigger words
from `src/serverscript/ss_trigger.h`. A command renamed in the engine is
renamed here on the next build.

Names and arity are not a signature, though — "pops 3 int" does not answer
"what does `inv_del` take?". The argument names and the descriptions come from
the reference server's own `content/scripts/engine.rs2`, which declares every
command as `[command,name](args)(returns)` with a `// info:` line above it:

> **command** `inv_del`
> ```runescript
> inv_del(inv $inv, obj $obj, int $count)
> ```
> Delete an $count of an object from an inventory
>
> `.inv_del` reads the secondary pointer.
>
> _server opcode 4307_

366 server commands, 345 of them with a description, plus 262 client-opcode
summaries taken from `tools/cs2_gen_opcodes/opcode_docs.py`. The table is
generated and checked in:

```bash
python3 tools/runescript-lsp/gen_command_docs.py [path-to-LostCity_Server]
```

A command the reference does not declare — this tree adds a few — keeps the
arity line, which is what it had before.

## What it does

| request | what you get |
| --- | --- |
| hover | the namespace, the id, the declaring file, the signature, and the `//` block above the declaration; for a built-in, the declared argument names and what the command does |
| definition / declaration | every site that declares the name, declarations before use sites; for a qualified name, the member and its container |
| references | every use, across the workspace |
| document & workspace symbols | scripts and records |
| completion | sigil-aware — `~` offers procs, `^` constants, `%` variables, `$` the locals this script declares; inside a config file, the keys that file type actually uses |
| signature help | the callee's declared arguments, with the active one counted |
| document highlight | the same name elsewhere in the file |
| folding | scripts, blocks, switch arms, records |
| semantic tokens | index-aware colouring: a name that resolves is coloured as what it resolves to, one that does not is left plain |
| diagnostics | syntax errors, and names that resolve to nothing |

### Diagnostics, and what is deliberately not reported

On by default:

- syntax errors and missing tokens, from the parse;
- a `~proc`, `@label`, `^constant`, `%var` or command that nothing declares;
- a `$local` the script never declares.

Off by default (`runescript.diagnostics.unknownNames`): a bare name that
resolves to nothing. A tree indexed without its cache packs would light up end
to end, so this is opt-in.

A `.cs2` is a different dialect and gets different rules. Decompiled client
scripts address things by id, and those spellings are not missing
declarations — there is nothing for them to declare:

- `$int0`, `$intarray0`, `$fontmetrics7` — a CS2 local's *name* states its
  bank. Locals start empty and may be read without ever being written, so the
  name is the only thing that says which stack it lives on. The type half is
  checked against the cs2 library's own prototype table.
- `%varbit6285`, `%varcint70`, `%var1356` — a variable named by its cache id.
- `~script222`, `[clientscript,script7592]` — a script named by its id.
- `^white`, `^setpos_abs_centre` — the decompiler's constant vocabulary, which
  is not in this tree's `.constant` files.
- a header's trigger word — the client's triggers are the cache's
  interface-hook vocabulary, and nothing here holds that list, so it is left
  unchecked rather than checked against the server's table.

Measured against the real corpus: 399 of 400 sampled `.rs2` files and 396 of
400 sampled `.cs2` files are diagnostic-free, and every remaining report is a
name the tree genuinely does not declare.

---

## Configuration

Sent as `initializationOptions`, and again on `workspace/didChangeConfiguration`:

```json
{
  "diagnostics": {
    "unknownSymbols": true,
    "unknownLocals": true,
    "unknownNames": false
  },
  "contentRoots": ["/path/to/another/content/tree"]
}
```

`contentRoots` indexes a tree that is not inside the open folder.

---

## Testing

```bash
make -C tools/runescript-lsp test                        # fixture suite, 40 checks
python3 tools/runescript-lsp/test/content_tree_test.py   # against OSRS-Content
```

The fixture in `test/fixture/` is a miniature content tree whose scripts get
three things wrong on purpose: a test that only opens correct input cannot tell
a working diagnostic from an absent one. `content_tree_test.py` runs the same
questions against content nobody wrote for the test, and SKIPs when
`OSRS-Content/` is not present.

Both suites were checked against mutated builds — with the diagnostic settings
forced off three checks fail, and with the qualified-name split removed the
container half stops resolving — so the assertions are known to be capable of
failing.

---

## Layout

```
src/server.c            the protocol loop, and one handler per request
src/index.c             the workspace index and the engine's built-in vocabulary
src/doc.c               open documents, their trees, and the line table
src/json.c              a read-only JSON DOM, sized for request bodies
src/util.c              buffers, paths, and the UTF-8 <-> UTF-16 column conversion
src/platform.c          directory walking and binary stdio, per platform
src/command_docs.gen.h  generated by gen_command_docs.py; do not edit
```

Two notes on things that look like choices and are not:

**UTF-16 columns.** LSP positions count UTF-16 code units; tree-sitter counts
UTF-8 bytes. This tree's comments are full of em dashes, so the two disagree on
any line carrying one — which shows up as highlighting sliding sideways for the
rest of the line. The conversion is a named function for that reason.

**The config family is scanned, not parsed.** The runeconfig grammar parses
every one of those files and drives every editor feature once one is open. But
the index reads ~55 MB of them at startup, most of it `configs/all.*` and 1,151
`.compack` name indexes whose entire content is `id=name`. Building a syntax
tree to read a line's first field costs about ten times what reading it does,
and nothing downstream looks at the shape. Scripts *are* parsed: a header
carries a trigger, a subject, an argument list and a return list, and picking
those out with a scanner would be re-implementing the grammar one regex at a
time.

**Header dependencies are tracked, and have to be.** The Makefile compiles with
`-MMD -MP` and includes the resulting `.d` files. Without that, a header edit
rebuilds nothing: `struct RS_Symbol` grew a field once, `index.o` was rebuilt
and `server.o` was not, and a silent struct-size mismatch read as a segfault a
long way from the edit.

Indexing the full `OSRS-Content/osrs239-content` tree: 16,664 files, ~800,000
names, 3.5 s, 200 MB resident.
