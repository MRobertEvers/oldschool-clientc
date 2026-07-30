# Implementation plan — one pack file per namespace, two encoders

A step-by-step plan for the content pipeline described in
[`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md), narrowed by two scoping
decisions that remove most of that document's machinery:

1. **The client cache is frozen.** One revision, pinned. Absorbing future
   official caches is out of scope, so nothing here defends against ids moving.
2. **Content is authored in its own schema.** dat2 emission is a *backend*, not
   the shape content is written in. "The cache cannot express this" is a normal,
   declared condition rather than a bug.

Both decisions delete work. What survives is smaller and sharper than §4 of the
architecture doc, and the ordering below is chosen so each phase is independently
useful and independently revertible.

---

## 0. Scope

### Goal

- Author RS2 scripts, configs (npc, obj, loc, enum, struct, …) and new assets
  (models, sprites, sounds) in one tree.
- One place ties every id to a name: `pack/<ns>.pack`, for **both** config types
  and idx-addressed asset tables.
- Two encoders off one authored record: a **client** half packed into dat2 for
  the client to read, and a **server** half the engine reads directly. Server
  fields never reach the client unless a field explicitly says they do.

### Explicitly out of scope

Cut by decision 1, and listed so nobody re-adds them by reflex:

| dropped | what it was for |
|---|---|
| `names/pins.ini` fingerprints | detecting ids that moved between revisions |
| `cachepack unpack --compare` in the build | reviewing a new cache's diff |
| symbol-closure filter over the diff (§5.1) | making that diff tractable |
| `server_base` headroom **gate** | upstream growing into a reserved id band |
| per-revision codec negotiation | encoding changes between revisions |

`--compare` stays in the tool — it costs nothing and is useful ad hoc. It just
stops being a step anything depends on.

### Non-goals

- No `dbtable`/`dbrow` authoring. Both are `CP_TYPE_NO_ENCODER`
  (`3rd/rscache/tools/cachepack/cp_types.c:98`), so cachepack refuses to write
  them and the base records pass through untouched. Writing that encoder is a
  separate piece of work; see [Risks](#7-risks-and-open-questions).
- No change to the wire protocol or to `src/net/rev/*`.

---

## 1. What already exists

Credit where the substrate is already in place, so no phase below rebuilds it.

| capability | where | state |
|---|---|---|
| `id=name` pack files, sparse, sorted on write | `3rd/rscache/tools/port_lostcity/lc_pack.c` | done |
| `max+1` allocation, appending rather than hole-filling | `lc_pack_alloc`, `lc_pack.h:148` | done |
| filler names not persisted (`<type>_<id>` resolves without a line) | `lc_pack_save_sparse`, `lc_pack.h:93`; `lc_pack_synthetic_id`, `:104` | done |
| names seeded from the cache's own symbol table (OSRS idx 24) | `cp_names.c:227` `seed_pack_from_gameval`; `3rd/rscache/src/dat2disk.h:154` | done |
| archive→kind claim *verified* before trusting | `cp_names.c:215-275` | done |
| namespace register with id/name authority | `src/content/content_register.{h,c}` | done, 3 rows wrong (Phase 2) |
| register read by the script compiler | `src/serverscript/ssc_symbols.c:342` | done |
| write gate keyed on the register | `cp_register_may_write_pack`, `cp_register.c:125` | done |
| client encoder for npc/obj/loc/seq/param/struct/… | `cp_pack_npc` etc., `cp_types.c:56-103` | done |
| server-side record overlay (config block over cache record) | `src/net/mock/mock230_content.c` | done |
| base-cache copy + per-record re-encode | `mock230_pack.c:596`, `:737`; `cp_pack.c:145` | done |
| param baking (npc combat, loc door stages) | `bake_npc_params`, `mock230_pack.c:684`; `bake_loc_params`, `:719` | done, to be replaced by Phase 5 |

So the work below is mostly **consolidation and declaration**, not new
subsystems.

---

## 2. Phase 0 — the safety net

Do this first. Nothing depends on it, and everything after is safer with it.

### 0.1 Byte-identity bake test

**Why now.** `npc`, `obj`, `loc`, `seq`, `enum` and `mapelement` are
`CP_TYPE_LOSSY` (`cp_types.c:56-95`): the decoder drops fields it does not
model, so re-encoding a record from text produces "a shorter, valid record"
(`cp_types.c:32-36`). The existing mitigation is to copy the base bytes and
re-encode **only** records that have an overlay (`mock230_pack.c:737`,
`cp_pack.c:86`). That rule is load-bearing and currently unverified.

A frozen base cache makes the check trivial to state: **bake with zero content
overlays, and the output must be byte-identical to the input.**

**Changes**

- New test, `src/net/mock/test/test_bake_identity.c` (or alongside the existing
  cachepack tests in `3rd/rscache/test/`).
- Run `mock230_pack --cache-out` against an empty/absent content tree.
- Compare every file in the output against the base: `main_file_cache.dat2`,
  `.idx255`, `idx0..idx31`, `xteas.json` (the copy set is
  `mock230_pack.c:601-603`).
- Then the same for `cachepack pack --base` with no `configs/` present.

**Skip-if-absent.** No cache is committed (`.gitignore:11` is `cache.*/`), so
the test must skip cleanly when `MOCK230_CACHE` resolves to nothing — matching
how `3rd/rscache/test/test_sound.c` handles its corpus. A skipped test must say
so loudly rather than pass silently.

**Exit criteria**

- `make -C src test-bake-identity` passes against a real cache and skips without
  one.
- Deliberately breaking the per-record guard (force re-encode of every npc)
  makes it fail. Verify this by hand once; do not commit the broken variant.

### 0.2 Pack round-trip test

**Changes**

- `load → save → load` over a fixture pack file must be byte-identical,
  including comments and blank lines. This test *fails* before Phase 1 and is
  the acceptance criterion for it, so write it now and mark it expected-fail.

---

## 3. Phase 1 — make one pack file safe to own by hand

This is the phase the whole design rests on. Under the two-layer split, comments
lived in `names/` and nothing wrote that file. With a single hand-owned file,
**any tool that saves a pack destroys every comment in it.**

The cost is already measured, at `cp_names.c:105-113`:

> `lc_pack_save` emits nothing but `id=name` lines, so rewriting a pack whose
> names a human wrote destroys the prose justifying them — which is measurable:
> `pack/param.pack` lost all 58 of its comment lines to one unpack, and that
> header is the record of which param-id claims were checked against a cache and
> how.

### 1.1 Teach `LC_Pack` to carry comments

`lc_pack_load` currently *parses and discards* them: a trailing `//` is cut at
`lc_pack.c:114-120`, and any line without a leading-digit `id=` is skipped
(`:125-126`, `:131-132`). `struct LC_Pack` (`lc_pack.h:20-34`) has nowhere to
put them.

**Changes** to `struct LC_Pack`:

```c
/** Comment text trailing `id=name`, without the `//`. NULL when none. */
char** trailing;          /* parallel to `names`, same capacity */
/** Whole-line comments and blank lines appearing immediately before an id. */
char** preceding;         /* parallel to `names`; may hold several lines */
/** Comment block before the first id line — the file header. */
char* preamble;
```

Anchoring standalone comments to **the id that follows them** is what preserves
`param.pack`'s 58-line header (it becomes `preamble`) and per-line
justifications, while keeping the file sorted by id on write. A comment block at
the end of the file with no following id anchors to a `trailer` field.

**Changes** to the three entry points:

- `lc_pack_load` — capture instead of skip. A line that is neither a comment nor
  a valid `id=name` is still skipped, but now *counted* and reported, so a
  malformed pack is visible rather than silently thinned.
- `lc_pack_save` (`lc_pack.h:76`) — emit `preamble`, then per id: `preceding`
  lines, then `id=name` plus ` // trailing`.
- `lc_pack_save_sparse` (`lc_pack.h:93`) — same, and **keep the comments of an
  omitted filler line only if they were authored**. A filler id with a real
  comment on it means someone said something about that id; dropping the line
  would lose it. Simplest correct rule: a filler entry carrying any comment is
  written out in full rather than omitted.

### 1.2 Make every pack write a merge

**Rule.** *Read the existing file, preserve comments and order, add only ids not
already named, never delete a line that was not explicitly removed.*

That single property replaces what the directory split was buying.

**Changes**

- `lc_pack_save`/`_save_sparse` gain merge semantics against the file already at
  `path`: an id present on disk and absent from the in-memory pack is
  **preserved**, not dropped. Deletion goes exclusively through
  `lc_pack_remove` (`lc_pack.h:126`), which is explicit and already recomputes
  `max`.
- Audit every writer for truncating opens. `tools/gameval_import.py` is the
  known offender (it opened outputs with `"w"`, truncating `pack/npc.pack` from
  16,292 lines to 39); `docs/mock230_content.md:518-526` says it now merges —
  verify that, and add a test rather than trusting the doc.

**Exit criteria**

- Phase 0.2's round-trip test passes, comments included.
- New test: save a pack that is missing ids present on disk → those ids survive.
- New test: `param.pack`'s header survives a full unpack. This is the regression
  that motivated the whole phase.

### 1.3 Report what changed

`LC_Pack` already tracks `added`/`removed` (`lc_pack.h:30-33`). Print them per
namespace on every write, and print nothing when a file is unchanged. A pack
write that reports `removed=N` with N > 0 and no explicit `lc_pack_remove` call
is a bug this makes visible.

---

## 4. Phase 2 — stop the register from licensing the corruption

With one file, `cp_register_may_write_pack` (`cp_register.c:125`) is the **only**
thing standing between a tool and your symbol table. It returns
`machine_owned`, which is derived from the register's `names` authority.

### 2.1 Fix three wrong rows

`src/content/content_register.c:25-51` declares `CONTENT_NAMES_CACHE` for three
namespaces that have **no gameval archive at all** — their 4th column in
`cp_types.c` / `cp_assets.c` is `-1`, meaning the cache names nothing:

```c
{ "param",    CONTENT_IDS_CACHE, CONTENT_NAMES_CACHE, 0 },   /* cp_types.c:67  → -1 */
{ "hitsplat", CONTENT_IDS_CACHE, CONTENT_NAMES_CACHE, 0 },   /* cp_types.c      → -1 */
{ "synth",    CONTENT_IDS_CACHE, CONTENT_NAMES_CACHE, 0 },   /* cp_assets.c:79  → -1 */
```

Every name in those packs is therefore either `<type>_<id>` filler or something
a human wrote. Declaring them machine-owned tells cachepack it may rewrite
them — and the save path drops comments. **This is the exact mechanism behind
the `param.pack` incident quoted above:** the register was meant to prevent it
and currently authorises it.

**Change.** All three become `CONTENT_NAMES_AUTHORED`.

### 2.2 Assert the register agrees with the codec tables

The bug in 2.1 is a *class*, not an instance: it recurs every time someone adds
a namespace, because "does this namespace have a gameval archive" is stated in
two places that nothing compares.

**Change.** A single check, run at build or at tool start:

> for every namespace in the register, `names == CONTENT_NAMES_CACHE` **iff**
> the corresponding `cp_types.c` / `cp_assets.c` row has a `gameval_archive`
> other than `-1`.

Prefer a static assertion or a generated table over a runtime check if the two
tables can be made to see each other; a loud failure at tool start is an
acceptable fallback. Either way the failure message should name the offending
namespace and both sources.

**Exit criteria**

- The check fails on the tree as it stands today (proving it works), then passes
  after 2.1.
- Adding a namespace to the register with the wrong `names` authority fails the
  build.

---

## 5. Phase 3 — collapse to a single pack directory

Mechanical, and only safe after Phases 1–2: without merge-preserving writes,
collapsing two layers into one file means the machine-owned writer can reach
authored names.

### 3.1 Migrate the files

- `names/<ns>.pack` → merge into `pack/<ns>.pack`.
- `server/pack/stat.pack` and `server/pack/varp_mock.pack` → `pack/stat.pack`
  and `pack/varp.pack` (these are the two legacy layer-1 locations listed at
  `mock230_content.c:1717-1722`).
- Where an authored line **overrides** a cache name for the same id, the merged
  file keeps the authored name and carries the cache's name as a trailing
  comment — the convention `lc_pack.c:114-117` already documents
  (`3254=guard // cache: guard1`). That preserves the provenance the directory
  split used to encode, at the cost of one comment.
- Where an authored line is an **alias** (a second name for an id the cache
  already names), the single-file format cannot hold both — `names[id]` is one
  name per id. Decide per case: keep the authored name and drop the cache name
  to a comment, or keep the cache name. **Record every such decision in the
  commit message**; this is the one lossy step in the migration.

### 3.2 Collapse the loaders

- `src/net/mock/mock230_content.c` — `mock230_content_load` (`:1672`) drops the
  second and third load loops (`:1762-1771`), keeping only
  `<dir>/pack/<ns>.pack`. The `PACK_LAYER_*` enum (`:133`) and the layer field
  in `struct PackEntry` (`:141`) go away, and `mock230_content_symbol` (`:209`)
  / `_symbol_name` (`:251`) lose their two-pass structure.
- `3rd/rscache/tools/cachepack/cp_names.c` — `packs[]` and `authored[]`
  (`cachepack.h:165`, `:176`) collapse to one array, as do `asset_packs[]` and
  `asset_authored[]` (`:189`, `:192`). Note the reason they were separate
  (`cp_names.c:46-58`) is *exactly* the save-path hazard Phase 1 fixes — so this
  collapse is only correct once Phase 1 has landed.

### 3.3 Keep both collision checks

`validate_name_layers` (`mock230_content.c:300`) loses its cross-layer rule but
keeps the two that matter, neither of which depends on there being two layers:

- **A duplicate name within a namespace is a load error**, not last-one-wins.
- **`varp`/`varbit`/`varn`/`vars` share one RuneScript `%name` domain**
  (`mock230_content.c:336`, `content_register.c` `shared` column), so a name
  meaning varp 115 and varbit 4 cannot coexist.

Rename the function accordingly — `validate_symbols` or similar; "layers" stops
being the thing it checks.

### 3.4 Extend coverage to the unnamed asset namespaces

These have no cache names whatsoever and so start empty, filling as things get
named. From `cp_assets.c:72-119`, the tables with `gameval_archive == -1`:

`model` `synth` `song` `jingle` `sample` `patch` `texture` `animset` `base`
`map` `font` `script` `binary` `dbindex` `animaya`

Only `sprite` (archive 12) and `interface` (archive 14) are seeded. This is why
the authored side is not optional: for models and sounds, the pack file *is* the
name table, not an overlay on one.

**Useful side effect.** Lines in `pack/model.pack` versus models in the cache is
a literal progress metric — "how much of this have I named". Print it.

---

## 6. Phase 4 — allocation from the pack file

The pack file is now both symbol table and **allocator lockfile**. `lc_pack_alloc`
(`lc_pack.h:148`) already implements the rule, including the important part:

> Appends at `max` rather than filling the first hole: a gap in a pack is usually
> a deliberately retired id, and re-using it would silently rebind every
> reference an old build still holds.

### 4.1 Wire allocation to the register's id authority

- `CONTENT_IDS_SERVER` namespaces (`script`, `enum`, `struct`, `dbtable`,
  `varn`, `vars` — `content_register.c:36-49`): a name with no id line gets
  `lc_pack_alloc`'d and the file is rewritten. The pack is the lockfile;
  assignments are stable forever after.
- `CONTENT_IDS_CACHE` namespaces: **a name with no id line is a hard error.**
  Never auto-allocate — the client's cache fixes these ids. This is LostCity's
  `transmitted` rule and it is the check that catches a typo'd symbol before it
  becomes a wrong record.
- `CONTENT_IDS_PROTOCOL` (`stat`): never allocated; the wire fixes it.

### 4.2 New ids in cache-owned namespaces

Adding an npc or a model means picking a number the frozen cache does not use.
With the cache frozen, the max per namespace is a **constant**, so this needs no
gate — just a declared base per namespace, well above the known max, recorded in
the register. Allocate upward from it with `lc_pack_alloc`.

### 4.3 Renumber the bogus server params

`pack/param.pack` claims ids `2000+` are "invented by this server for things no
cache states … above every real param id so they cannot collide". They are not:
`cache.osrs239` has 2,634 param records covering **0–2633**, so every server id
(2000–2008, 2100–2105) is a param the cache already defines
(`CONTENT_ARCHITECTURE.md` §3.3).

**Change.** Reallocate to `max+1` off the pack file — 2634 upward.

**This is a content migration, not just a code change.** Every `.rs2`, every
config overlay and every previously baked derived cache references the old
numbers. Sequence it as: allocate the new ids → update content → rebuild →
verify with `tools/dump_npc` that the new params read back → only then delete
the old lines. Do it in its own commit, separate from Phase 4's code.

---

## 7. Phase 5 — the field register and two encoders

The core of the design once revision-bumping is gone. Content is authored in its
own schema; the client encoder is a **projection** of it, and is allowed to be
lossy *by declaration*.

### 5.1 A field register per type

```ini
; fields/npc.ini
[npc.name]           scope = client   client = native
[npc.size]           scope = client   client = native
[npc.hitpoints]      scope = server   client = param:2634
[npc.respawnrate]    scope = server   client = param:2635
[npc.attackrate]     scope = server   client = param:14      ; the cache already has it
[npc.death_drop]     scope = server   client = drop          ; never leaves the server
[npc.some_new_field] scope = server   client = error         ; must be expressible; fail if not
```

Four `client` dispositions, and `error` is the one worth having deliberately —
it is how you learn at build time that you authored something the client will
never see, instead of discovering it in game:

| disposition | meaning |
|---|---|
| `native` | the record's own field in the dat2 encoding |
| `param:N` | projected into param N on the record |
| `drop` | server-only; the client encoder omits it silently |
| `error` | the client encoder must refuse; no silent loss allowed |

**Default is `scope = server` with `client = drop`.** A field reaches the client
only because someone wrote down that it does — opt-in, never opt-out.

### 5.2 Seed the register from what already exists

Two tables already hold most of the answer, so this is transcription rather than
design:

- The **client** column is `cachepack`'s emitter. `cp_npc.c:40` `emit_npc` writes
  43 keys, every one client/render: `name size readyanim walkanim …13 anims…
  recol retex minimap vislevel resizeh resizev ambient contrast turnspeed
  category bastype footprintsize zbuf …`
- The **server** column is `mock230_content.c`'s npc key ladder: `hitpoints
  attack strength defence magic ranged respawnrate wanderrange moverestrict
  huntmode`.
- The **`param:N` column** is `mock230_pack.c:689-702`'s `BakedParam` table
  verbatim.

The two key sets overlap on exactly **`name`** and **`param`**. That is what
makes this tractable: the union is nearly disjoint, so unifying is additive on
both sides rather than a conflict resolution.

### 5.3 Unify the grammar

One `[name]` block accepting the union. Because the sets are disjoint, neither
existing parser has to change its interpretation of any key it already handles —
each simply stops rejecting the other's.

`mock230_content.c`'s per-key `strcmp` ladders become register lookups. Its
current "accepted and ignored" behaviour becomes "declared `scope = client`, so
this overlay is patching the cache" — a different and much more interesting
thing to report.

### 5.4 One baker, three audiences

Today two tools write a derived cache and explicitly do not compose:
`cachepack pack --base` (whole config records, from `configs/all.<type>`) and
`mock230_pack --cache-out` (params only, from `server/` overlays). See
`3rd/rscache/tools/cachepack/main.c:29-33` and `CONTENT_ARCHITECTURE.md` §3.5.

Collapse to one baker emitting three outputs from one merged record:

| output | contents | audience |
|---|---|---|
| **client cache** | `native` fields + `param:N` projections marked client-safe | shipped; players read it |
| **server defs** | every field | the engine, in process |
| **portable cache** | client cache + all `param:N` projections | `tools/dump_npc`, other servers |

The client/portable split matters: `--cache-out` currently bakes `hitpoints`→2100
and **`death_drop`→2000** into the param table (`docs/mock230_content.md:616`
shows `dump_npc` reading them back). A param in a cache is readable by anyone
holding the cache, so the file players download must not be the same artifact.
Give `param:N` an audience qualifier.

**Preserve the per-record rule.** Only records with an overlay get re-encoded;
untouched records keep their original bytes. Phase 0.1's test guards this.

**`bake_npc_params` and `bake_loc_params` disappear** — ~100 lines re-deriving
what the register now states.

---

## 8. Phase 6 — authoring new assets

### 6.1 The add path

Adding a model, sprite or sound is: drop the file in `assets/<type>/`, give it a
name in `pack/<type>.pack` (allocated per Phase 4.2), reference that name from a
config. The client encoder appends a new group to the relevant idx.

Notes from `cp_assets.c:72-119`:

- Most tables are opaque passthrough — fine for adding, since you are supplying
  bytes the client already understands.
- `sprite`, `map` and `script` have codecs (`cp_codec_sprite`, `cp_codec_map`,
  `cp_codec_script`); authored input goes through them.
- `map` is `CP_ASSET_ENCRYPTED` — authoring maps means owning `xteas.json`.
- `texture` and `dbindex` are `CP_ASSET_MULTIFILE`.

### 6.2 Scripts: two destinations, one symbol table

Keep this split explicit, because blurring it puts server logic in a file players
download:

- **Server RS2** → `sscompile` → server bytecode pack (`<content>/scripts/build`,
  which the engine already reads). **Never enters the client cache.**
- **Client CS2** → the `script` asset table. **Does** enter the cache.

The symbol table is shared by construction already: `ssc_symbols.c:342` reads
`ContentRegister_Defaults()` rather than its own filename list.

---

## 9. Ordering and dependencies

```
Phase 0  safety net            ─── independent, do first
   │
Phase 1  comment-preserving,   ─── blocks 3 (collapsing layers is unsafe without it)
         merging pack writes
   │
Phase 2  register correctness  ─── blocks 3 (may_write_pack is the only guard once
   │                                          there is one file)
Phase 3  single pack directory
   │
Phase 4  allocation ───────────┐   4.3 (param renumber) is a content migration;
   │                           │   keep it in its own commit
Phase 5  field register,       ◄─┘ needs 4.3's ids to write `param:N` rows
         two encoders
   │
Phase 6  asset authoring       ─── needs 4.2's id bases
```

Phases 0–2 are small, independently valuable, and touch no content. They are the
recommended first commit set. Phase 3 is mechanical but wide. Phases 4–6 are
where the design becomes visible to someone authoring content.

---

## 10. Risks and open questions

**`dbtable`/`dbrow` cannot be written.** `CP_TYPE_NO_ENCODER` (`cp_types.c:98`,
`:101`) — cachepack refuses and the base records pass through. Modern OSRS
content leans on dbtables, so if authored content needs them, an encoder is
prerequisite work not costed here.

**`enum` is `CP_TYPE_LOSSY`** (`cp_types.c:59`). If enums are load-bearing for
content, establish what the decoder drops before trusting a round trip. Phase
0.1's test will catch damage but not tell you what was lost.

**Aliases do not survive the single-file collapse.** `names[id]` is one name per
id. Phase 3.1 must decide each case explicitly; there is no mechanism that keeps
both.

**The param renumber (4.3) invalidates previously baked caches.** Anything
holding 2000–2008 / 2100–2105 must be rebuilt. Sequence deliberately.

**`re-unpack never renames`** (`cp_unpack.c:428`) is correct behaviour under the
new model but worth restating: existing pack names are kept, so a re-seed adds
and never overwrites. With one hand-owned file that is exactly what is wanted.

**Directory naming.** `names/` was a poor name — `pack/` is also full of names,
and that ambiguity actively confused review. Phase 3 removes the directory, so
the problem dissolves; do not reintroduce a second names-ish directory later
without a better word for it.

---

## 11. Reference tables

### Namespaces the cache names (OSRS gamevals, idx 24)

`dat2disk.h:154` — `RSCACHE_DAT2_OSRS_TABLE_GAMEVALS = 24`; absent in the
pre-dat2 epoch (`dat2disk.c:639`). The archive→kind mapping is **not** recorded
in the cache and is verified before trusting (`cp_names.c:215-225`).

| named by the cache | archive |
|---|---|
| obj, npc, inv, varp, varbit | 0, 1, 2, 3, 4 |
| loc, seq, spotanim | 6, 7, 8 |
| dbrow, dbtable, varc | 9, 10, 15 |
| sprite, interface *(assets)* | 12, 14 |

**Not named by the cache** (`gameval_archive == -1`) — every name is filler or
authored:

- configs: `enum` `param` `struct` `hitsplat` `healthbar` `mapelement`
  `underlay` `overlay` `idk`
- assets: `model` `synth` `song` `jingle` `sample` `patch` `texture` `animset`
  `base` `map` `font` `script` `binary` `dbindex` `animaya`

Note `varp 43 = com_mode` is the proof these are a real symbol table rather than
derived display names: varps have no display-name field, so there is nothing to
derive from.

### Encoder support by type

| flags | types | meaning |
|---|---|---|
| `0` | `param` `struct` `inv` `varp` `varbit` `varc` `hitsplat` `healthbar` `idk` `underlay` `overlay` | byte-exact round trip |
| `CP_TYPE_LOSSY` | `npc` `obj` `loc` `seq` `spotanim` `enum` `mapelement` | encoder exists; "a repack is not a copy" |
| `CP_TYPE_NO_ENCODER` | `dbrow` `dbtable` | pack refuses; base records pass through |

Source: `cp_types.c:56-103`, flag semantics documented at `:31-39`.
