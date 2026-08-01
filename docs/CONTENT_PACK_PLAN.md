# Implementation plan — one pack file per namespace, two encoders

A step-by-step plan for the content pipeline described in
[`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md), narrowed by four scoping
decisions that remove most of that document's machinery.

> **This title is superseded in one respect.**
> [`PACK_ENTITY_SPLIT_PLAN.md`](PACK_ENTITY_SPLIT_PLAN.md) plans *two* pack files
> per namespace — a client membership list and a server one — so that **which
> entities** go to a side is stated on disk rather than inferred from where a
> block was authored. §7's field register (which *fields* go to a side) is
> unaffected and is its prerequisite; the four decisions in §0 still hold. Read
> this document first; that one is the successor and is not built.

---

## 0. Scope and decisions

### The four decisions

1. **The client cache is frozen.** One revision, pinned. Absorbing future
   official caches is out of scope, so nothing here defends against ids moving.
2. **Content is authored in its own schema.** dat2 emission is a *backend*, not
   the shape content is written in. "The cache cannot express this" is a normal,
   declared condition rather than a bug.
3. **A one-time full export establishes the baseline.** The cache is exported to
   an asset/config tree once; that tree is thereafter the editing surface and the
   conceptual source of truth. Builds run tree → cache, never the reverse.
4. **Nothing is secret.** Gamevals and server params may be emitted into the
   client cache. This collapses what would otherwise be two cache audiences into
   one (§5.4).

All four decisions delete work. What survives is smaller and sharper than §4 of
the architecture doc, and the ordering below is chosen so each phase is
independently useful and independently revertible.

### The substrate rule

Decision 3 does **not** mean building a cache from an empty directory. The tool
has no such mode — `cp_pack.c:21-28`: *"pack starts from a base cache … Use
`--base` to copy first; without it the cache at `--out` is edited in place."*

That is the right shape, not a limitation to work around:

> **The tree is the source of truth for everything it states. The pristine cache
> is the substrate for everything it does not.**

Anything the tree does not emit — `dbtable`/`dbrow`, unmodeled opcodes on records
never touched, whole tables nobody has exported yet — falls through from the base
untouched (`cp_pack.c:86-89`). This is what makes decision 3 affordable: the tree
becomes authoritative incrementally, without total round-trip fidelity as a
prerequisite.

### Archive the original — policy, not suggestion

**Keep the pristine cache Jagex shipped, forever, and treat the tree as
derived.** Decoding happens exactly once, at the export border, and whatever the
decoder does not model is dropped there (§10). The cost of that is bounded and
mostly invisible — but it is not zero, and it has already come due once:

> loc opcode 69, `force_approach`, left the lossy list on 2026-07-29 because the
> client's pathfinder approach test needs it. **2,459 of `cache.osrs230`'s locs
> carry it.** (`3rd/rscache/EXCEPTIONS.md:185`)

A field sat safely on the "nothing reads this" list until the client grew a
feature that needed it. That will happen again. With the original archived, a
promoted opcode means re-exporting one type; without it, the data is gone. This
is a file in cold storage, so the mitigation costs nothing — which is exactly why
there is no excuse for skipping it.

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

**Future cache releases are handled case by case**, deliberately. Decision 3 makes
that cheaper than it sounds: once the tree is the source of truth, absorbing a new
cache stops being a cache-diff problem and becomes a **tree-diff** problem —
export the new cache into a second tree, `diff -r` against yours, merge what you
want by hand. That is ordinary reviewable text, and a better artifact than
`all.<type>.merge` ever was. No machinery required, which is why the case-by-case
choice is a real strategy rather than a deferral.

### Non-goals

- **No `dbtable`/`dbrow` authoring.** Both are `CP_TYPE_NO_ENCODER`
  (`cp_types.c:98`, `:101`), so cachepack refuses to write them. Under the
  substrate rule that is harmless — the base records pass through untouched — so
  this stays a non-goal rather than becoming a blocker. Writing the encoder is
  separate work, needed only if authored content must *create* dbtable rows.
- **No up-front campaign to close decoder loss.** The lossy decoders (§10) are
  not a prerequisite for anything here. Promote an opcode when a feature needs
  it, exactly as `force_approach` was promoted — lazily, with the original cache
  as the recovery path. A phase to model all ~24 loc opcodes ahead of demand
  would be work spent against a need nobody has stated.
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
| param baking (npc combat, loc door stages) | was `bake_npc_params` / `bake_loc_params` in `mock230_pack.c` | replaced by Phase 5 and deleted; `mock230_pack` validates only |
| **semantic** round-trip at 100% for every type, every cache | `EXCEPTIONS.md:169-172` | done — this is what makes decision 3 safe |
| FileList encoder, for emitting gamevals back (§5.5) | `RSCache_FileListEncode`, `3rd/rscache/src/filelist.h:37` | done |

So the work below is mostly **consolidation and declaration**, not new
subsystems.

---

## 2. Phase 0 — baseline and safety net

Do this first. Nothing depends on it, and everything after is safer with it.

### 0.1 Establish the baseline: full export

The one-time border crossing of decision 3. Export every table cachepack
supports into the tree, commit the result, and archive the source cache
alongside it (per §0 policy).

**Changes**

- `cachepack unpack` over all config types and all asset tables into the content
  tree — `configs/`, `assets/<type>/`, `pack/<ns>.pack`.
- Commit the tree. This is the baseline every later phase builds on.
- Record the source cache's identity in the tree — revision, and a checksum of
  `main_file_cache.dat2` — so "which cache is this tree derived from" is
  answerable later without guessing.

**Do not chase completeness here.** A table nobody has exported yet falls through
from the substrate, so a partial export is a working state, not a broken one. Add
tables as content needs them.

### 0.2 Fidelity bars, split by table kind

**Why now.** The build's correctness rests on a rule that is currently
unverified: re-encode only what the tree states, and let everything else fall
through from the substrate (`mock230_pack.c:737`, `cp_pack.c:86`).

**Whole-cache byte-identity is the wrong bar and must not be used.** It is
unreachable for reasons that lose nothing: Jagex's packer does not write opcodes
in ascending order and the encoders do (`EXCEPTIONS.md:200`, B3), so records come
back at identical length with different byte order. `idk` measures 41% exact and
**100% same-length** — purely reordering. Matching Jagex's ordering is explicitly
not worth chasing.

The bar therefore differs per table kind:

| kind | bar | rationale |
|---|---|---|
| opaque asset tables — `model` `synth` `song` `sample` `patch` `binary` `animset` `base` `font` `jingle` | **byte-identical** | bytes out, bytes in; no codec to lose anything |
| codec'd assets — `sprite` `map` `script` | same-length + semantic | e.g. sprites are 100% same-length, 24–29% exact; the shortfall is byte ordering only |
| config types | **semantic 100%**, and cachepack's `lost-here` = 0 | already achieved today (`EXCEPTIONS.md:169-172`); byte-exactness ignored by design |
| whole cache | **the client boots and renders identically** | the only end-to-end statement that means anything |

**Changes**

- New test asserting each row above, run against a real cache.
- The per-record guard is what it is really testing: force a re-encode of every
  npc and the config row must fail. Verify by hand once; do not commit the broken
  variant.

**Skip-if-absent.** No cache is committed (`.gitignore:11` is `cache.*/`), so the
test must skip cleanly when `MOCK230_CACHE` resolves to nothing — matching how
`3rd/rscache/test/test_sound.c` handles its corpus. A skipped test must say so
loudly rather than pass silently.

### 0.3 Pack round-trip test

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
> `configs/all.param.compack` lost all 58 of its comment lines to one unpack, and that
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
- Audit every writer for truncating opens. the cache's own gameval table is the
  known offender (it opened outputs with `"w"`, truncating `configs/all.npc.compack` from
  16,292 lines to 39); `docs/mock230_content.md:518-526` says it now merges —
  verify that, and add a test rather than trusting the doc.

**Exit criteria**

- Phase 0.3's round-trip test passes, comments included.
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
  and `configs/all.varp.compack` (these are the two legacy layer-1 locations listed at
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

**Useful side effect.** Lines in `pack/7_models.pack` versus models in the cache is
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

`configs/all.param.compack` claims ids `2000+` are "invented by this server for things no
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

### 5.4 One baker, two outputs

When this section was written, two tools wrote a derived cache and explicitly
did not compose: `cachepack pack --base` (whole config records, from
`configs/all.<type>`) and `mock230_pack --cache-out` (params only, from
`server/` overlays; deleted since — `mock230_pack` is a validator only now).
See `3rd/rscache/tools/cachepack/main.c:29-33` and `CONTENT_ARCHITECTURE.md`
§3.5.

Collapse to one baker emitting **two** outputs from one merged record:

| output | contents | audience |
|---|---|---|
| **cache** | `native` fields + every `param:N` projection + gamevals (§5.5) | the client, `tools/dump_npc`, any other server |
| **server defs** | every field | the engine, in process |

Decision 4 is what makes this two rather than three. An earlier draft split the
cache into a client artifact and a portable/tooling one, because `--cache-out`
baked `hitpoints`→2100 and `death_drop`→2000 into the param table (the old
`docs/mock230_content.md` §8 showed `dump_npc` reading them back) and a param is
readable by anyone holding the cache. With secrecy explicitly not a goal, that
split buys nothing and costs a whole second artifact — so `param:N` needs no
audience qualifier, and the cache players download is the same self-describing
file the tools read.

`drop` still exists and still matters: it is for fields with no sensible cache
representation at all — drop tables, dialogue, RuneScript — not for fields being
withheld.

**Preserve the per-record rule.** Only records the tree states get re-encoded;
everything else falls through from the substrate. Phase 0.2's config-row bar
guards this.

**`bake_npc_params` and `bake_loc_params` disappear** — ~100 lines re-deriving
what the register now states. (Done: the whole export path in `mock230_pack.c`
is deleted; the tool validates only.)

#### What has landed

**One baker, two outputs, from one merged record.** `pack_type` builds the merge,
`client_view_build` splits it, and both halves are written in the same pass —
which is §5.4's shape rather than an approximation of it.

- `cachepack pack` writes `<src>/server/pack` — a dat2 with no reference table,
  one archive per record at **(config kind, record id)**. `npc` lands in `idx9`,
  `loc` in `idx6`, matching the kinds `dat2_configs.h` gives them.
- `3rd/rscache/tools/cachepack/cp_fields.{h,c}` reads `fields/<type>.ini`. A
  second, small reader of a file `src/content/content_fields.c` also reads, for
  the reason `cp_register.h:6-10` gives: cachepack links nothing from `src/`, so
  the `.ini` is the contract rather than a shared header. It has **no built-in
  defaults** — a writer that invents an opcode the tree did not declare writes
  bytes nothing agreed to read.
- `src/net/mock/mock230_servercodec.c` is generic over `(field table, record
  base)`. Adding a type is a register file plus an offset table; `npc` and `loc`
  are the two instances, and `mock230_servercodec_test` iterates the registry, so
  a type registered without a `fields/<name>.ini` fails.
- Every archive carries `'S' 'P' version kind crc32(payload)`.
  `RSCache_Dat2DiskWriteArchive` creates the container from nothing but writes no
  `idx255`, so there is no per-archive CRC for stale detection; the header is
  what replaces it. `kind` distinguishes an opcode band from a name table.
- **Sparse by presence, never by value.** The writer emits a field because the
  tree states it, not because it differs from something — it has no defaults
  record to compare against, and comparing against *zero* would be wrong in both
  directions (`death_drop` defaults to -1, obj 0 is a real obj). The reader,
  which does have defaults, compares against those.
- **`ref = <namespace>`**, one added register key. The band is integers and
  content is not: `param=death_drop,bones` names an obj, `param=attack_anim,
  cow_attack` a sequence, `param=next_loc_stage,poordooropen` a loc. Nothing else
  in the register says which pack file to resolve a value through —
  `client = param:<name>` names the param, not the type of its value. A field
  with no `ref` takes only a decimal literal, which is correct for
  `huntmode = aggressive` (an engine enum with no cache namespace); those are
  counted and reported per field rather than guessed at.
- Server-only *namespaces* — `stat`, `category` — get name tables at **group 128
  and up**. The space is one byte wide because every dat2 sector carries its
  table id in a single byte (`dat2disk.c`, `data[7] = index_id & 0xFF`), so a
  group of 256 is written as 0 and aliases another table. `dat2_configs.h` runs
  1..39; 128 is the top half, leaving 40..127 — more than double the current
  maximum — for OldSchool to grow into first. Same argument as the 64..255 opcode
  band.

Measured on this tree: 2,196 npc bands (2,396 fields), 776 loc bands, 23 stat
names, 6 category names. The npc count is far above the 38 authored npcs because
`configs/all.npc` already carries `param=attackrate` for 2,196 records and the
register declares `attackrate` as `scope = server` — the register is about
fields, not layers, so a cache-sourced server field belongs in the pack too.

**5.4's cache half.** `cp_fields.c` reads `client = param:<name>`, and
`client_view_build` folds the field into the record's param table under that
param's *name*. Skipped when the record states the param itself, so the 2,196
npcs that already carry `param=attackrate` are untouched; 169 params are
projected across the tree. Two spellings of a `param=` line now parse — the
machine export's `<name>,<kind>,<value>` and LostCity's `<name>,<value>` — because
the kind and a symbolic value are both recoverable from the param's own declared
type, which is what `cp_param_types_load` builds before the type loop.

**4.3 is complete.** The 15 ids at 2634..2648 were named and had no records; they
are authored now (`npc_combat.param`, `doors.param`) and the pack writes 2,649
param records where it wrote 2,634. `cp_pack_param` accepts LostCity's type
*words* (`int`, `seq`, `namedobj`, `loc`) alongside the export's single character,
and resolves a reference type's symbolic default — `default=bones` is obj 526
because `death_drop` is a `namedobj`, and nothing else in the record says so.

**5.5, gamevals.** `cachepack pack --gamevals` writes `pack/<ns>.pack` back into
idx 24, 12 archives, verified by re-seeding a tree from the emitted cache alone —
npc 3028 comes back as `goblin` with no content tree present. Archive 14 is
refused rather than half-written: it nests 26,491 component names inside the
interface records, and a flat write would delete them. Off by default; nothing
outside cachepack reads the table.

**New records are opt-in**, declared by `records = client` in a bare `[<type>]`
section. Verified against the tree rather than assumed: `enum` (7 blocks) and
`coord_pair.dbtable` exist only at rank 1 and are *server tables wearing a config
grammar* — `bank_tabs` and `worn_slots` are read by `mock230_content_enum` and no
client script has heard of them — so `fields/enum.ini` says `records = server` and
they stay out. `param` opts in, because a projection referencing a param with no
record is a reference the client cannot interpret. `varp` (16 blocks) and
`combat.param` (18) turned out to be **overlays on cache records**, not new
records, so the question never applied to them.

#### The two client-cache changes, accounted for

- **enum, 2,592 records.** Opcodes 1 and 2 — the input and output ScriptVarType
  characters — left the lossy list, for the reason §10 gives: an opcode is
  modelled when a feature needs it. Checked exactly: every one of the 2,592 is a
  record carrying opcode 1 or a non-string opcode 2, with zero unexplained and
  zero expected-but-unchanged. (`diff` initially reported 2,600 by mis-aligning 8
  byte-identical lines among the many empty enums; a key-by-key compare is the
  honest count.)
- **`configs/all.obj`, 198 records.** Not a cache change — a stale *export*. Every
  `team=` line in the tree exceeded 255 because the pre-fix decoder read opcode
  115 as two bytes, which desynchronised the rest of each record and truncated
  it. Re-exporting restored name, desc, params and the rest on exactly those 198,
  and renamed 31 models the recovered `manwear` fields now reference. Digests are
  computed from the cache, so this moved none of them.

#### What is declared but not implemented

The largest item first, because it is the one that makes the others feel
unresolved:

- **~~The band is written but not read~~ — read, verified and preferred at
  boot.** `mock230_boot_load` step 2b decodes `server/pack` over the live
  defs after proving every archive identical to what the text parse loaded
  (`osrs230_mockserver.md` §3.10b); `make -C src mock230-servpack` refreshes
  the pack via the new `cachepack pack --server-only`, which skips the client
  half entirely — no base cache, no `--out`, and no exposure to the `.dbrow`
  merge blocker (§ the dbtable doc), because a type with no `server = opcode:`
  row is never merged. The text parse remains, for now, as the migration
  fallback and the proof; removing it for the band-carried fields is the
  outstanding sub-step. (`mock230_pack.c`'s baking, which §5.4 retires, is
  deleted — the tool validates only.) Tracked as Phase 0 in
  [`PORTING_GUIDE.md`](PORTING_GUIDE.md) §3.6.

Each of the rest is a register row saying where the field stands, rather than
a silence:

- **`obj.levelrequire`** — `client = drop`, no server band.
  `param=levelrequire,<stat>,<level>` repeats up to eight times per record; the
  band is one fixed-width integer per opcode. Needs a list-valued wire on both
  the writer and `mock230_servercodec.c`.
- **`loc.category`, `npc.huntmode`, `npc.moverestrict`** — `client = drop`, no
  band. Their values are engine enum names (`door_closed`, `aggressive`,
  `nomove`) with no cache namespace, so the band would need either a symbol
  table cachepack does not have or a `ref` that does not exist.
- **`varp.transmit` / `scope` / `protect`** — `client = drop`, no band. A band
  needs a `Mock230VarpDef` for the reader to decode into.
- **`prayer`** — not a field on a cache record but a server-only *record type*:
  no config kind, no gameval archive. It would take a group from the reserved
  128..255 space plus string and repeatable wires, since its fields are a string
  (`name`), a component reference (`button`), a repeated enum (`group`) and a
  constant reference (`headicon`).

### 5.5 Emit gamevals from the pack files

The cache's own symbol table (`GAMEVALS`, OSRS idx 24) is where layer 0 came from
in the first place, and nothing prevents writing it back: the format is a
FileList — file id = record id, file contents = the name string — and
`RSCache_FileListEncode` / `…EncodeBound` already exist (`3rd/rscache/src/filelist.h:37`,
`:44`). Emitting one archive per namespace from `pack/<ns>.pack` makes the cache
**self-describing**: anything pointed at the cache alone recovers your names
without the content tree.

**The client never reads it** — nothing outside cachepack opens
`RSCACHE_DAT2_TABLE_GAMEVALS` — so this cannot break the client, which also makes
it safe to add late or omit entirely.

Four things to know:

- **Not a faithful round trip.** `sanitise_name` (`cp_names.c:172`) collapses
  anything outside `[A-Za-z0-9_.+-]` to `_`, and `uniquify` (`:198`) appends
  `i2`/`i3` on collision. Regenerating overwrites Jagex's original strings with
  your normalised ones. Fine for a cache you own — and another reason the
  pristine original stays archived.
- **Archive 14 is nested.** It names interfaces *and* their components in one
  record: `<interface name> \0`, then repeating `u16 child_id  <component name>
  \0`, terminated by `0xffff` (`cp_names.c:330`). Emitting it means re-merging
  `pack/3_interfaces.pack` and `pack/component.pack` into that shape.
- **Sparse in, sparse out.** Filler names are not stored in the pack file, so
  unnamed ids simply get no entry. Correct and harmless.
- **The 90% verification becomes vacuous.** `seed_pack_from_gameval` rejects an
  archive when fewer than 90% of its ids match real records (`cp_names.c:283`).
  A self-generated archive passes by construction; do not read that as
  validation.

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
Phase 0  baseline + safety net ─── independent, do first
         0.1 full export, 0.2 fidelity bars, 0.3 pack round-trip
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

### What the export border drops

Decoding happens **once**, at Phase 0.1. This is a one-time toll, not a
compounding error: every build after the export is encode-only, so nothing
degrades further. The question is therefore not "is the round trip faithful" but
"do these specific fields matter" — and for most of the list, demonstrably not
(`EXCEPTIONS.md:169-186`).

| type | dropped at the border | matters? |
|---|---|---|
| npc | opcodes 93, 107, 109 — clear-flags never set true | no; *"No client code reads those three fields"* |
| param | which opcode delivered the type (1 vs 8); the resulting char is kept | no |
| obj | opcode 9 (a discarded string); `"Hidden"` action and `"null"` name normalised to NULL | no |
| enum | opcodes 1, 7, 8 | unlikely; check if enums become load-bearing |
| spotanim | recolour/retexture slots past 6 | unlikely |
| sequence | v1 frame-sound `retain`/`weight`; opcode 100's blend table | unlikely |
| sprites | the per-sprite flags byte — 100% same-length, 24–29% exact | no; ordering only |
| texture v1 | a `count-1` run re-encoded as zeros | no |
| **loc** | **~24 opcodes** (25, 44, 45, 61, 88/90/91/96–105, 163–191, the boolean-flag block) plus opcode 95's pre-220 payload | **the real exposure** — see below |
| **mapelement** | ~24 opcodes; only sprite, name, text size and category kept — 0% byte-exact by construction | **the other one** |

`loc` and `mapelement` are the exposure, and the mitigation is not an
up-front modelling campaign — it is the archived original (§0). Promote an opcode
when a feature needs it, as `force_approach` was promoted on 2026-07-29 for the
pathfinder approach test, and re-export that type. Note npc opcode 127
(`basTypeId`) is already flagged as *"the one worth promoting later"*: it
redirects an npc's idle and walk sequences through a separate type.

**Why an unknown opcode cannot be preserved opaquely.** A config record is an
opcode stream in which each opcode's payload length is implied by the opcode
itself. Not knowing what opcode 25 means is not knowing how many bytes it
consumes, so it cannot be skipped and re-emitted verbatim. Hence
`EXCEPTIONS.md:1065`: *"a decoder that continues past an unknown opcode destroys
the evidence needed to fix it."* Modelling a field is the only way to keep it —
there is no escape hatch, and no point looking for one.

### Other risks

**`dbtable`/`dbrow` cannot be written.** `CP_TYPE_NO_ENCODER` (`cp_types.c:98`,
`:101`) — cachepack refuses and the base records pass through. Harmless under the
substrate rule, and the reason it stays a non-goal. It becomes real work only if
authored content must *create* dbtable rows: that is absence, not degradation,
and would show up as a missing table rather than a thinner record.

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
