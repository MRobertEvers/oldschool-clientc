# Server packs: incremental builds, staleness, and the reload contract

The server boots from three packs built out of `OSRS-Content/osrs239-content`:

| pack | built by | where | what |
|---|---|---|---|
| base script pack | `sscompile` | `server/scripts/build/` | `script.dat` + `script.idx`, every `.rs2` with no lane |
| lane script pack | `sscompile --lane …` | `server/scripts/build_summoning_curses/` | the same plus the Summoning and Curses lanes |
| server pack | `cachepack pack --server-only` | `server/pack/` | a dat2 store of every config record the server reads |

One command builds all three, full or incremental — they are the same command:

```sh
make -C src torirsserver-packs        # what tools/tob_build_packs.sh runs
PACKS_ARGS=--verbose ./tools/tob_build_packs.sh               # every unit's line
PACKS_ARGS="--explain tob_maiden.rs2" ./tools/tob_build_packs.sh   # why is it stale?
```

`make torirsserver-scripts`, `torirsserver-scripts-lanes` and `torirsserver-servpack`
still work and are incremental too (they call the same tools).

## Measured (2026-10-09, the owner's Mac, a frozen snapshot of the tree)

| | before | after |
|---|---|---|
| base script pack, full | 28.9 s | 2.7 s |
| lane script pack, full | 26.5 s | 2.7 s (in parallel with the base) |
| server pack, full | 114 s (124 s via make) | 12.6 s |
| **all three, full** (`make torirsserver-packs`, no state) | ~170 s sequential | **14.0 s** wall |
| one `.rs2` body edit (`tob_verzik.rs2`) | 60 s (`tob_build_packs.sh`) | **1.1 s** (1 unit per script pack) |
| one config record (`[verzik_initial_story] hitpoints`) | 124 s | **4.6 s** (npc repacked, 1 archive changed) |
| a `.constant` only scripts read | 60 s + 124 s | ~1 s (its users recompile, 0 types repacked) |
| no-op rebuild | 60 s + a fingerprint walk | **0.27 s** (0.45 s through make) |
| change detection alone | — | sscompile 0.14–0.17 s, cachepack 0.08 s, allocator 0.05 s |

Every full build here is byte-identical to the old tools' output (the server pack
except `stamp.txt`'s `writer` line, which is the binary's identity). The script
packs are byte-identical when given the same path spelling — a script embeds its
source path, and the old makefile passed absolute paths as the new one does.

Where the time went (sampled with `sample`):

- **sscompile**: 66% re-sorting the 250,000-entry symbol table (every lookup after
  an add `qsort`ed it — dbtable columns, builtins and constants all interleave adds
  with lookups); 11% a linear duplicate-name scan per declaration (quadratic in
  the tree); `stat()` on every one of the ~50,000 selftest screenshots under
  `server/scripts`. Now a hash index, a hashed script-name table, and `d_type`.
- **cachepack**: linear `lc_pack_find` (every name lookup scanned every id, ~3 per
  record × 62,000 locs), linear key-table and register lookups per merged line,
  a quadratic duplicate-block check per config file, `cp_walk_find` copying and
  re-sorting all 54,000 walked files per call and then searching each match back
  linearly, and a whole-tree fingerprint walk that `stat`ed every screenshot.
- **ss_allocate.py**: one `os.walk` of the 54,000 files per namespace (a dozen).

## Units and what they depend on

### Script packs (src/serverscript/ssc_build.c)

A unit is one `.rs2` file. What it compiled to depends on:

1. its own text — size, mtime and a 64-bit content hash (`ssc_hash.h`);
2. every question it asked while compiling, each recorded with a digest of the
   answer: symbol lookups (`SSC_SymbolsFind`, `FindValue`, `ValueKinds`, the
   varbit-carrier table) and script lookups (a callee's id and signature —
   argument counts, return counts, the declared parameter kinds);
3. the configuration: the compiler binary's bytes, the format version, the lane
   selection, every root, exclusion and pack directory (`config` in the manifest).

A build stats every unit (hashing only those whose size or mtime moved), re-asks
every recorded question against the new tables (~100,000 distinct questions,
10 ms), and recompiles exactly the units whose text or answers moved. A proc
whose signature changed recompiles its callers and nothing else; a constant
whose value changed recompiles the files that expand it. Everything else is
reused byte for byte from `<out>/ssc.state`.

The symbol table's own inputs — every file the loaders opened, and every
directory they listed — are recorded too. When neither those nor any unit moved,
the build stops before loading the symbol table at all (the no-op path). A
directory counts as changed only when the set of its subdirectories and
loader-relevant files (`.pack .compack .alloc .constant .dbtable .varp .varbit`,
`all.*`) changed — not its mtime, which packs, logs and selftest screenshots move
all day.

Declarations (names and signatures) are a pure function of a file's text, so a
reused unit's declarations are reused too; only the registration against the
whole pack (the duplicate rules, lane seams) runs every build.

### Server pack (3rd/rscache/tools/cachepack/cp_incremental.c)

A unit is one config type (npc, loc, varp, …, plus the stat/category name
tables). Records merge per type with type-wide rules (an authored layer over the
cache's, keys learned type-wide), so a group cannot be repacked without
re-merging its type; the type is the unit and the group is what changes inside
it. A type's output depends on:

1. its own files: every `*.<type>` the walk finds, `pack/<type>.client` and
   `.server` (routing), and for `dbrow` every `*.dbtable` (its schemas);
2. its recorded lookups: names (`cp_name_find`, `_find_alloc`, `_get`,
   `_is_lane`) and constants (`cp_resolve_caret`, `cp_value_constant_text`), each
   with a digest of the answer — so adding a varp to `pack/varp.alloc`, or editing
   a `.constant`, repacks only the types that looked it up;
3. the global key — what no encoder's lookups can be traced through: `fields/`,
   `content.ini`, `meta.ini`, the asset/stat/category packs, the interface member
   indexes, the param-type table, this binary, the lanes. Any change there repacks
   every type (12.6 s).

A type that mints a name while packing (`cp_name_ensure`) changes what later
types see; it is recorded as such and never reused. None does today.

The store is written whole, in the order a full build writes it, from each
type's archives (reused ones verbatim). It is 12 MB and takes milliseconds, and
it is why an incremental build and a full one produce identical files. When no
archive changed the store is not touched.

### The id allocator (tools/ss_allocate.py)

Runs first: new `[name]` blocks in server-allocated namespaces get ids appended to
`pack/<ns>.alloc`. `--stamp` skips the run (0.05 s) when no file or directory it
read last time has moved.

A block that already spells a `varp<N>_` prefix used to be prefixed again
(`[varp6883_x]` → `varp7344_varp6883_x`), and a hand fix back to `[varp7344_x]`
matched nothing, so every build allocated a fresh id and a deeper prefix. Now:
when id N is free (and at or above the namespace's floor) the block is adopted as
written; when N belongs to another name, or the base name already has a ledger
spelling, the build stops and names the spelling to use. A silent respelling
cannot converge, because `var_prefix_names.py` only respells blocks whose base
name the ledger holds.

## Stable ids

**Script ids** live in `ssc.state`: a name keeps the id it was first given for as
long as the state lives; a new name takes the next unused id; a removed script
leaves an empty slot (size 0 in `script.idx`, which is how the format has always
recorded a retired id) and its id is never handed to another name. A name that
comes back gets its old id. A build with no state numbers the tree in sorted
path order, exactly as the old compiler did. `sscompile --renumber` forgets the
ids and numbers afresh (the output then equals the old compiler's again).

**Config ids** were already stable: an archive's key is the record id from the
name tables (`configs/all.<type>.compack`, `pack/<type>.alloc`), never a position.

## The manifests

Both are text, written atomically beside the pack, and are what the server checks
staleness against and what a reload diffs. Paths are relative to the content
root (absolute when outside it). Hashes are `ssc_hash_bytes` (64-bit, in both
tools and the server).

`server/scripts/build/pack.manifest`:

```
torirs-script-pack 1
config <hex>                    the configuration key (binary, lanes, roots)
generation <n>                  +1 whenever script.dat or script.idx changes
lanes <names | ->
output script.dat <size> <hash>
output script.idx <size> <hash>
root strong|weak <dir>          the source roots, as the compiler walked them
exclude <dir>                   subtrees subtracted from them (lanes not built)
input <size> <mtime_ns> <hash> <path>          a file the symbol table was read from
unit <size> <mtime_ns> <hash> <rev> <path>     a source file and its revision
script <id> <hash> <rev> <unit> <name>         a compiled script: blob hash, revision
retired <id> <name>                            an id whose script is gone
```

`server/pack/pack.manifest`:

```
torirs-server-pack 1
config <hex>                    the global key
generation <n>
lanes <names | ->
output main_file_cache.dat2 <size> <hash>
input <size> <mtime_ns> <hash> <path>          every file the pack is built from
unit <type> <key> <rev> <archives>             a config type
archive <index> <archive> <hash> <rev> <type>  every archive in the store
```

A revision (`rev`) moves only when the thing's bytes did: a script's when its
encoded blob changed, an archive's when its payload did, a unit's when it was
rebuilt with a different result.

## Staleness (the server)

`src/torirsserver/torirs_server_packcheck.c`, at boot, for each pack with a
manifest:

- every `input` and `unit` is `stat`ed; one whose size or mtime moved is hashed,
  and only a different hash makes it stale (a touch is not an edit);
- the script pack's roots are walked (`d_type`, no `stat`) for `.rs2`,
  `.constant`, `.dbtable` and `.varp` files the build never saw; the server pack's
  input set is listed the same way for new config files;
- every stale unit is **named**, in one banner, with the server-pack unit it feeds:

```
torirsserver: ------------------------------------------------------------
torirsserver: the script pack was built before these change(s) to its sources:
torirsserver:   edited   server/scripts/skill_afk/scripts/afk.rs2
torirsserver: 1 stale; the pack at …/build runs as it was built.
torirsserver: Rebuild (incremental, seconds): make -C src torirsserver-packs
```

**The pack still runs.** Another session's edit to a file this run never touches
is no reason to refuse it, and an edit of your own shows up in the list by name.
`TORIRSSERVER_STALE=refuse` restores the refusal (exit 1 with the old
`STALE SCRIPT PACK` / `the server pack at … is STALE` lines the gates grep for).

A pack without a manifest — built before this change — is held to the old rules
(the script pack's mtime walk, the server pack's whole-tree stamp), and every
build still refreshes `script.dat`'s mtime and rewrites `stamp.txt` when an input
moved, so a server binary from before this change still accepts the new packs.

Both builds hold `<pack>/.pack.lock` exclusive while they write; the server holds
it shared while it reads the pack (`ToriRSServer_PackLockShared`), so it never
loads a `script.dat` from one build with a `script.idx` from another. Outputs are
written to a temporary and renamed into place.

## The hot-reload contract

Not implemented in the server yet; nothing in the format precludes it, and this is
what a reload may rely on.

1. **Diff, don't reload.** Read the new manifest; a script whose `script` line's
   hash differs from the loaded one changed; a missing line is a retired id; a new
   id is a new script. For the server pack, an `archive` line whose hash differs is
   the only thing to re-read, by (index, archive) — dat2 reads one archive without
   touching the rest.
2. **Ids are stable**, so every compiled gosub, queue and timer reference in the
   *unchanged* scripts still points at the right script after a reload. A reload
   never renumbers; `--renumber` is a cold-boot-only operation.
3. **Take the pack lock shared** while reading the new files, and check the
   manifest's `output` hashes against what was read before swapping anything in.
4. **Swap at a tick boundary.** A suspended or queued script instance keeps the
   script object it started on until it finishes (refcount the old one); new
   invocations, queue arms and trigger lookups use the new one. A signature change
   is safe because the build recompiles every caller in the same pack, and the
   reload applies the whole manifest diff at once.
5. **A changed config archive** replaces that group's records in the type's table
   at the same tick boundary. Entities hold type ids, not record pointers, so they
   see the new record on their next lookup.
6. **Not reloadable**: `.spawn` files, `.constant` values the server reads as text
   at boot (`torirs_server_content.c`), map squares (read from the cache), and the
   symbol tables of the content loader — those need a boot. Spawns are text read
   at boot and need no build; a spawn hot-reload would be a separate world feature.

`src/game/content_test.c`'s `reload` (a full `ScriptsFree` + `ScriptsLoad` that
re-binds queues and timers by name) is the existing, coarse form; the manifest
diff is what makes a fine-grained one possible.

## Logs

Every tool prints one line per unit it rebuilt, with the reason, and a summary
with each phase's time; every unit's line (built, reused, removed) goes to
`pack.log` beside the pack. Warnings are printed in full every build (the
ambiguous-name family is cut at 20 as before, `SSCOMPILE_AMBIGUOUS=all` for
every one). `build_packs.py` prints each tool's whole output under a header and a
last summary line:

```
== script pack (base) -> …/build
compiled  server/scripts/minigames/minigame_tob/scripts/tob_verzik.rs2 — edited
compiled  server/scripts/player/containers.rs2 — answer changed: constant ^if_event_worn_slot (now "1031")
sscompile: 2 compiled, 3016 reused, 0 removed; 43572 scripts in 43572 ids (0 new, 0 retired) -> …/script.dat
  phases: walk 0.08s, symbols 0.34s, declare 0.01s, check 0.01s, compile 0.01s, write 0.05s; total 0.50s
== server pack -> …/server/pack
cachepack: repacked npc — its files changed
Server pack: 1 type(s) repacked, 20 reused; 1 of 15935 archive(s) changed [inputs 0.05s, types 3.86s, write 0.52s; total 4.45s]
== packs: ok [allocate 0.05s, script pack (base) 0.56s, script pack (…) 0.59s, server pack 4.05s; wall 4.57s]
```

`sscompile … --explain FILE` (or `PACKS_ARGS="--explain FILE"`) prints why a unit
is stale — edited, new, or which recorded answers moved and what they are now —
and builds nothing:

```
stale     server/scripts/interface_bank/scripts/bank_worn.rs2 — 1 of 98 recorded answer(s) changed
            answer changed: constant ^if_event_worn_slot (now "1030")
```

`sscompile --dry-run` prints that for every unit.

## What is not incremental, and why

- **A server-pack type is the unit, not a record or a group.** The merge is
  type-wide (keys learned per type, the authored layer over the cache's), so an
  edit to one npc re-merges all 16,000 npcs (4 s); only the archives whose bytes
  changed get a new revision.
- **Global inputs repack every type** (12.6 s): `fields/`, `content.ini`,
  `meta.ini`, the asset and interface indexes, the param types. They are rare
  edits and the encoders read them in ways a lookup record cannot trace.
- **The symbol table is reloaded whenever any of its inputs moved** (~0.35 s);
  the replay then decides which units actually recompile. Loading it is cheaper
  than tracking which loader read what.
- **The two script packs are compiled separately.** Each has its own ids (a lane
  inserts scripts into the sorted order of a fresh numbering), so their bytecode
  differs; they run in parallel instead.
- **A changed compiler or cachepack binary rebuilds everything**, by design: its
  hash is part of the configuration key.

## Files

- `src/serverscript/ssc_build.{c,h}` — the incremental script build; `ssc_hash.h`
- `src/serverscript/ssc_compile.c` — `SSC_ScanDeclarations` / `SSC_DeclarePrepare` /
  `SSC_DeclareAt` (the declare pass split so it can be cached), the query observer
- `src/serverscript/ssc_symbols.c` — the hashed symbol index, input and query observers
- `3rd/rscache/tools/cachepack/cp_incremental.{c,h}` — the incremental server pack
- `src/torirsserver/torirs_server_packcheck.{c,h}` — the server's per-unit check and pack lock
- `tools/build_packs.py`, `tools/tob_build_packs.sh`, `src/makefile` `torirsserver-packs`
- `tools/ss_allocate.py` — the prefix fix and `--stamp`
