# Plan: name the cache dbtables, and write the README the system deserves

> **EXECUTED.** Parts A–E were run. `docs/DBTABLES.md` is the deliverable and
> supersedes this file for anything factual; what stays here is the plan as
> written plus, in each part, what actually happened where it differed.
>
> **The one thing that changed everything.** Part B's premise — that the names in
> `configs/all.dbtable.compack` are *derived* and need evidence-based
> replacements — is false. Gameval archive 10 is **keyed, not flat**: key 1 is
> the table's name, key n ≥ 2 is the name of column n-2. `cp_names.c` read it
> flat through `sanitise_name`, which turns the framing bytes into underscores,
> into a 256-byte buffer. `_quest__id__sortname__…` is `quest` plus its 49 column
> names, truncated at 255 characters. So there is nothing to *name* — only a
> decoder to fix and a layer-0 file to regenerate. The tier system in B4 has
> nothing to rank; see `DBTABLES.md` §4.2 for the four independent checks that
> establish this.
>
> **What shipped:** Part A in full; the evidence in full (and reusable); the
> archive-10 decode in the seeder, plus two defects it exposed, each with a
> negative control that went red; and **all 246 tables renamed** — table 0 first,
> end to end and gated, then the other 245 as a single layer-0 regeneration once
> the sweep was authorised.
>
> Nothing in it is committed policy until executed and verified; every claim below
> that came from inspection says where it was inspected.

**Deliverables**

1. Evidence-based friendly names for every cache dbtable that can be
   *identified* — in `OSRS-Content/osrs239-content/configs/all.dbtable.compack`,
   with the data files kept in agreement and the round-trip verified.
2. `docs/DBTABLES.md` — the reference for what dbtables are, where they are
   used, how the two populations relate, and how server content consumes them.
   Outline in Part E; most of its raw material is gathered by Parts B–C.

**Read first, in this order**

- `docs/CONTENT_ARCHITECTURE.md` §8 — who owns a rule. The register/allocator
  disagreement pattern in §8.2(c) recurs *in this plan* (Part A).
- The header of `configs/all.dbtable.compack` — the two-populations rule and
  the id-collision constraint, already written down there.
- The header of `configs/all.varp.compack` §6.5 story (in
  `docs/CONTENT_ARCHITECTURE.md`) — **a pack line is `names[id]`: an authored
  name REPLACES the cache's, it does not alias it.** This is the single most
  important mechanic in the whole plan.
- `3rd/rscache/EXCEPTIONS.md` and `3rd/rscache/tools/cachepack/README.md`
  before touching any cachepack round-trip (memory: `rscache-write-expansion`,
  `cachepack-tool`).

**Ground rules carried over from the sessions that produced this tree**

- Build: `make -C src` targets; embed binary is
  `make -C src torirs_embed_wf EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_embed_wf
  PLATFORM_TARGET=torirs_embed_wf`. Gate: `make -C src test-mock230` (which now
  rebuilds `mock230-scripts` first — do not undo that dependency).
- **Every new assertion needs a negative control**: mutate the input to prove
  the assertion can fail, then restore. Two assertions this session passed for
  weeks on the strength of the bug they should have caught.
- Commit nothing; leave a verified/partial/blocked summary.
- `getenv` returns non-NULL for empty strings — `FOO=` does not unset; use
  `unset`.

---

## Part A — Fix the register first (small, and it gates the rest)

`src/content/content_register.c:81-82` declares `dbrow` and `dbtable` as
`CONTENT_IDS_CACHE` with the comment *"neither has an encoder, so authored
content cannot create one either way."* Both claims are stale: this tree
created `coord_pair_table` (259) and `combat_style_table` (260) and they work,
because `mock230_db.c` parses text and needs no cache encoder. The allocation
succeeded only because `tools/ss_allocate.py`'s `id_authority()` defaults to
`'server'` when `content.ini` is silent — and `content.ini` says nothing about
either namespace. The Python default silently outvoted the C register: the
exact two-tables-agreeing-by-hand failure of `CONTENT_ARCHITECTURE.md` §8.2(c),
third occurrence.

1. Add `[namespace:dbtable]` and `[namespace:dbrow]` to
   `OSRS-Content/osrs239-content/content.ini` with `ids = server` and a comment
   explaining the dual population (cache ids 0..258 / 0..16724 keep their ids;
   `server` licenses growth above the high-water mark — same correction `param`
   and `varp` already record in that file).
2. Correct the stale comment in `content_register.c` and make the C rows agree
   (`CONTENT_IDS_SERVER`, keeping `names` as-is and `server_base` 259).
   Check what `CONTENT_IDS_SERVER` changes behaviourally in
   `mock230_content.c` / `cachepack` before flipping — read the consumers, do
   not assume the enum is inert documentation.
3. Verify: `python3 tools/ss_allocate.py --tree OSRS-Content/osrs239-content
   --check` still exits 0; `make -C src test-mock230` green;
   `validate_id_bases` (`src/net/mock/mock230_pack.c:553`) still passes on
   boot.

**As executed.** Both steps done, and the register comment was stale in a
*second* way the plan did not catch: the encoders exist now
(`RSCache_Dat2ConfigDbTableEncode`, byte-identity against every record;
`CP_TYPE_NO_ENCODER` is not set on either type), so "neither has an encoder" is
false as well as irrelevant. Corrected in the comment.

Step 3's premise is wrong on the last clause: `validate_id_bases` is **not a boot
check**. `mock230_pack.c` has its own `main`; it is the validator binary run by
`make -C src test-content`. It also reads `ContentRegister_Defaults`, not the
tree's `content.ini`, and gates on `server_base != 0` rather than on `ids`.

Read before flipping, as instructed, and the honest answer is that **`ids` has
exactly one consumer**: `ContentRegister_Validate`'s protocol-plus-base
contradiction check. `cachepack`'s `cp_register.c` reads only `names`;
`ss_allocate.py` already had both namespaces in `DEFAULT_SERVER_NAMESPACES` and
`id_authority()` already defaulted to `'server'`. So the C change is
behaviourally inert and the `content.ini` change is not — with `ids = cache`,
`server_namespaces()` *discards* the namespace and dbtable drops out of the
allocator's sweep entirely. That was the negative control, and it went red.

`src/content/test/content_register_test.c` pinned `dbtable` to `IDS_CACHE` and
failed on the change, which is the register test doing its job; the assertion is
corrected and a matching one added for `dbrow`, with its own control (flip to
`IDS_CACHE`, red; restore, green).

---

## Part B — Build the evidence, then decide names from it

Do **not** name anything by intuition or by what OSRS wikis suggest a table
"should" be. Every name is a claim; the method below makes each one checkable,
and the confidence tier decides whether it ships.

### B1. The row-name clustering (the primary signal)

Every row section in `configs/all.dbrow` carries `table=<derived name>`, and
the row sections are headed by the cache's own **row** gameval names, which are
good names (`[quest_animalmagnetism]`, `[music_rat_boss]`). Verified sample
(this session, script over `all.dbrow`, `errors='replace'` — the file has
non-UTF8 bytes):

```
rows   table (derived name, truncated)                        row-name prefixes
3447   _skill_features__icon__sprite__text__skill__quest...   skill:3447
2174   _action__action_name__action_desc__...                 league:2031, action:143
 876   _music__sortname__displayname__unlockhint__...         music:876
 668   _synth__name__sub_menu__synth__parent_directory__      synth:668
 535   _cluehelper_target_coord__coord__description...        cluehelper:535
 332   _slayer_master_task__master_id__task__weight...        nieve:46, vannaka:46, ...
 213   _quest__id__sortname__displayname__...                 quest:184, miniquest:19, subquest:10
```

242 of 246 cache tables have rows. Write a scratch script that emits, per
table id: the derived name, row count, top row-name prefixes, and 3 sample
rows with their string values (`values=N:0:<text>` lines carry display names —
`Animal Magnetism` — which are decisive evidence).

### B2. The derived name itself (the schema signal)

The gameval name embeds the column list: `_quest__id__sortname__displayname__…`
is name + columns, `__`-joined. This is the cache's own naming (gameval archive
10) — Jagex's, not this tree's derivation. It corroborates or corrects the
cluster: a table whose rows are all `league_*` but whose columns say
`_action__action_name__…` is the *action* table that leagues happens to fill.

### B3. Cross-reference the consumers (the usage signal)

Which CS2 scripts read each table, and which interface those scripts belong
to. The decompiler exists (`3rd/rscache/src/cs2/cs2_decompile.h`; memory
`cs2-db-opcodes-session`: DB ops 7500-7510). A table read only by the music
tab's scripts is the music table with a second, independent proof. This pass
is also raw material for the README's "where used" section — record
table → script → interface as you go, do not re-derive it later.

### B4. Confidence tiers, and what ships

- **Tier 1 — two independent signals agree** (cluster + schema, or cluster +
  consumer): rename.
- **Tier 2 — one signal, plausible**: do NOT rename. Record the evidence and
  the candidate name in the README's appendix table instead. The derived name
  is ugly but honest; a plausible wrong name is the hitsplat-colour mistake
  (memory: `all.hitsplat.compack` header) in a new namespace.
- **Tier 3 — no signal** (the ~4 rowless tables, anything opaque): leave
  alone, list in the appendix as unidentified.

Naming convention: bare snake-case nouns for cache tables (`quest`, `music`,
`slayer_master_task`); the `_table` suffix stays reserved for server-authored
tables (`combat_style_table`), so the suffix itself tells a reader which
population a name belongs to. Names must be unique across BOTH populations —
one flat id space (`(table << 12) | (column << 4)`, per the compack header).

**As executed.** The naming convention turns out to be a description of what the
cache already does rather than a choice: the decoded names *are* bare snake-case
nouns, and none of the 246 ends in `_table` except where Jagex meant it
(`fsw_info_fresh_table`, `dbg_dummy_table`). Nothing had to be invented.

B1 and B3 were built as specified and are the reusable part of this pass:

- **B1** — 242 of 246 tables have rows; the row names corroborate the decoded
  table name everywhere and contradict it nowhere. The four rowless tables are
  17, 169, 183 and 240, and they are named on the same footing as the rest,
  because the cache names them whether or not this revision ships rows.
- **B3** — exact rather than approximate, because the packed column id is a
  literal in the decompiled scripts: 422 clientscripts read 128 tables, every
  referenced table id resolves to a named table (zero orphans), and no script
  reads a column above the decoded count. Mapped on to interfaces by taking the
  transitive `~scriptNNNN` closure of each `.if` file's hooks — 57 of the 128,
  with the rest unreachable because this era arms most hooks at runtime.

**B4's tiers do not apply and were not used.** They were designed for a world
where a name is an inference that can be more or less well supported. These names
are transcription: the cache states them, two independently written decoders
agree on all 246, and the risk being managed is a *decoder* being wrong rather
than a *table* being misidentified — which is checked four ways in one place
instead of 246 times. Recording a "tier" per table would have dressed a fact up
as a judgement.

---

## Part C — The rename mechanics (where the mistakes live)

For each Tier-1 table, in this order:

1. **The name line** in `configs/all.dbtable.compack`, ABOVE the allocator
   marker, replacing the derived line for that id, keeping provenance:

   ```
   2=quest // cache: _quest__id__sortname__displayname__...
   ```

   The trailing `// cache:` note is the established convention (see the varp
   compack) and is the only record of the derived name once replaced —
   remember, replacement not alias.

2. **The data files must agree.** `all.dbtable` and `all.dbrow` section
   headers / `table=` lines are keyed by NAME. After renaming, regenerate via
   `cachepack unpack --types dbtable dbrow` (expected to rewrite them under
   the current name layer) — **verify this expectation on ONE table before
   doing forty**; if unpack does not re-key, the fallback is a scripted
   rewrite of the two data files, and that decision goes in the README.
   Warning from §6.4 of CONTENT_ARCHITECTURE.md: a clean re-unpack rewrites
   many lines and wants its own review — diff it, and confirm the merge did
   not eat authored comments.

3. **Round-trip proof.** Pack back and byte-compare (the codec-baseline
   `lost-here` verify column — see the cachepack README for the invocation).
   Semantic round-trip proves nothing (memory: `rscache-write-expansion`);
   byte-exact or a documented, understood diff.

4. **Boot + suite.** `make -C src test-mock230`; boot the embed binary and
   confirm the quest journal / music tab still populate (they are dbtable-
   backed client-side — a broken table 21 index or a mangled row shows up
   there). `SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=600 TORIRS_EXIT_BMP=...`
   with a music-tab click is the harness shape; see `docs/` harness notes.

5. **One negative control for the batch**: corrupt one renamed table's data
   line, confirm the selftest or the boot check notices, restore. If nothing
   notices, say so in the README — "renames are unvalidated by any test" is a
   finding, not a failure of the plan.

**Traps**, all already paid for once:

- Column ORDER in a `.dbtable` is the tuple order; `db_getfield` addresses by
  index. Reordering columns silently swaps every lookup.
- `,` inside string values is escaped `\,` (1,245 lines in this cache) —
  never split a `values=` line on bare commas (`config/cp_db.c` header).
- `columns=` (the alloc count) is NOT the number of present columns — 8,257
  rows have alloc > highest present. Preserve it; never re-derive it.
- `21_dbtableindex` is DERIVED from the rows (`cp_decode.c:4352`) — never
  hand-edit it; if the tooling regenerates it, let it.
- The allocator marker line: hand-edits below it are forbidden; the server
  tables 259+ live there and are the allocator's.
- The non-UTF8 bytes in `all.dbrow` — any script over it needs
  `errors='replace'` (hit during B1's verification sample).

**As executed, step by step.**

1. **The name line.** Done for table 0, and written as plain `0=quest` with *no*
   `// cache:` note. The note would have been a lie: `quest` **is** the cache's
   name, so there is no replaced cache name to record. It would also have blocked
   the fix — `seed_pack_from_gameval` skips any id the pack already names, so a
   hand-written line is the one line a corrected re-seed cannot reach.
2. **The data files.** The expectation held, and better than §6.4's warning
   suggested. `cachepack unpack --types dbtable,dbrow` re-keys and is otherwise
   idempotent: **exactly 215 lines changed** (one `[block]` header, 213 `table=`
   lines), both compacks byte-identical, prose header and allocator marker
   untouched. No scripted rewrite of the data files was needed.
3. **Round-trip proof — and this step does not do what the plan thought.**
   `cachepack verify` reads records *from the cache*, round-trips them through
   text in memory, and never opens `configs/all.dbtable` or `all.dbrow`. A
   compack renamed with the data files left stale verifies **fully green**.
   Verify is a check on the codecs and the text layer, not on step 2.
4. **Boot + suite.** Green: `test-content`, `test-mock230`, and a headless embed
   boot that renders the world, sidebar and inventory. Worth stating that this is
   a *regression* check and not a proof — the client reads dbtables from the
   cache, and `verify` says all 246 tables and 16,711 rows still encode
   byte-identically, so there is nothing for a client to notice either way.
   (`make -C src test-db` aborts, and did so before this pass — hardcoded
   `cache.osrs230`, profile unidentified.)
5. **The negative control — this is where the finding is.** Staling one `table=`
   line took the packed dbrow group from 1,774,143 to 1,774,141 bytes and
   `cachepack pack` reported `0 failed, 0 unknown keys, 0 unresolved names`.
   `cp_pack_dbrow` resolved `table=` with a bare `cp_name_find` — the one
   reference in the tool not going through `cp_resolve_ref` — and -1 is exactly
   how the decoder spells "opcode 4 never appeared", so the encoder drops the
   row's table binding and `DB_GETROWTABLE` answers -1. Fixed; the same control
   now prints `unknown dbtable reference` and `1 unresolved names`, and the
   consistent tree still reports 0.

   The plan's fallback answer also holds: **nothing else in the tree notices.**
   `mock230_db.c` reads only the server population, `sscompile` reads the
   compack, the client reads the cache, and `test-mock230` stays green
   throughout. Renames are validated by `cachepack pack` and by nothing else.

**A second defect, outside the plan's list.** `cp_names_emit_gamevals` wrote
`dbtable` back to archive 10 as a flat string. It skipped archive 14 for being
nested and did not know 10 is too, so `cachepack pack --gamevals` would have
replaced the keyed record with one truncated name and destroyed ~3,000 column
names that exist nowhere else. Now skipped, with the same message shape as 14.

**The remaining 245 followed, once the sweep was authorised.** They were held
back at first because the correct route is a layer-0 regeneration and dropping
245 machine-written lines is exactly the "script that rewrites the compack
wholesale" the instructions ruled out; that was lifted, and it ran. Not a
different mechanism from table 0's — the same `cachepack unpack`, with the
machine-written half of layer 0 removed first so the corrected seeder can reach
ids that already have a name. Results in `DBTABLES.md` §7.

**C.1's `// cache:` provenance convention was not used, for any table.** It
records the cache name an authored line *replaces*, and there is no such thing
here: every one of these names is the cache's. Writing `0=quest // cache:
_quest__id__…` would have asserted that the cache calls table 0 the mangled
thing, which is the opposite of what §4.2 establishes. The provenance lives in
the file's header instead, where it is stated once rather than 246 times.

---

## Part D — The column-name gap (investigate, then decide; do not assume)

A renamed table is readable by humans but still unusable from RuneScript:
`quest:displayname` cannot compile because cache tables have no column
declarations (`all.dbtable` has `columns=`/`defaulttypes=` — types, no names;
the compiler's "N db columns" all come from server `.dbtable` files). And
`mock230_db.c` loads only the server population — cache rows are the
*client's*, read by CS2.

The column names exist — embedded in the derived gameval string. So the
naming pass has the raw material. What is NOT settled is where declarations
for a cache table live:

- A server `.dbtable` block whose name resolves (via the compack override) to
  a cache id would register columns on that id — but check whether the boot
  validation (`validate_id_bases`, and the compack header's claim that
  authored ids "must sit above the largest id the cache states") rejects it.
  Read the check; do not trust the comment (memory:
  `verify-blocker-and-failing-test`).
- If it is rejected, the design question is whether to add an explicit
  "adopt" mechanism or a separate declarations file. **Scope for this plan:
  investigate, document the answer and the recommended design in the README,
  implement only if it turns out to be trivial.** Feeding the cache's 16k
  rows into `mock230_db` is explicitly out of scope — a real feature for a
  session that needs it (the quest journal server-side would be the natural
  driver).

**As executed: it is not rejected. It already works, and that is the problem.**

Answered by probe rather than by reading alone. A server `.dbtable` whose block
header is `[quest]` resolves through
`SSC_SymbolsFind(…, SSC_SYM_DBTABLE)` — which searches the *whole* namespace,
cache half included — so `quest:displayname` compiled, the db column count went
3 → 6, `mock230_pack` reported 0 errors, `ss_allocate --check` exited 0 (no
allocation: the name already has a line), and `test-mock230` passed. Probe
reverted.

So no adopt mechanism needs inventing and none was invented. What is missing is a
**check**, and the recommendation in `DBTABLES.md` §8 is deliberately narrow:
`validate_id_bases` should also report an authored block landing below the cache
high-water mark in a `names = cache` namespace, and should read
`ContentRegister_Load` rather than `ContentRegister_Defaults` so the tree's
`content.ini` reaches it at all. Adoption is a legitimate thing to want — it is
what the column-name gap needs — but it should be visible in the validator rather
than indistinguishable from a typo that happened to resolve.

The column names themselves are recovered (§4.2 of the README) with exact
indices. Where they should *live* is the remaining open question, and the answer
that fits the existing shape is a documentation-only key emitted by
`cp_unpack_dbtable` beside the types it names, which needs the packer to
round-trip it. Not trivial enough to fold in here.

---

## Part E — `docs/DBTABLES.md` outline

Write it last, from what Parts B–D actually found. Sections:

1. **What a dbtable is.** The client database: config group 39 = schema
   (columns, tuple types, defaults; type ids are ScriptVarType — 0 int, 36
   string), group 38 = rows, archive 21 = derived find-index. Read
   client-side via CS2 `DB_*` (7500-7510).
2. **Where they are used.** The B3 table → script → interface map. Quest
   journal, music tab, collection log at minimum.
3. **Two populations, one id space.** Cache 0..258 / rows 0..16724; server
   259+ / 16725+; the `(table<<12)|(column<<4)` packing that forbids
   collision; the high-water allocation rule and `validate_id_bases`.
4. **The files and the pipeline.** Cache binary ↔ `all.dbtable`/`all.dbrow`
   (cachepack, both directions, escape rules, `columns=` semantics) ↔
   `all.dbtable.compack` (names; gameval archive 10; replacement semantics);
   server `server/scripts/**/*.dbtable`/`.dbrow` → `mock230_db.c` →
   `db_find`/`db_getfield` ops.
5. **How server content uses them — worked example.** `combat_style_table`
   end to end: the schema, the 15 rows, `~combat_get_weapon_style_data`,
   `%com_mode` clamped to the row length, and why the spear row is the proof
   a heuristic could not express (all-controlled styles walking
   stab/slash/crush).
6. **Editing workflows.** Data edit = restart only (verified: a `data=` edit
   changed a selftest answer with no rebuild); new name = `ss_allocate` +
   `mock230-scripts`; rename = Part C's four steps; column reorder = the trap
   at the top of the list.
7. **The naming register.** The full appendix table: id, shipped name (or
   retained derived name), tier, evidence one-liner. Tier 2/3 entries carry
   their candidate names here — this is where the next session picks up.
8. **Known gaps.** Column declarations for cache tables (Part D's findings);
   server cannot read cache rows; `dbcolumn` packing unverified (memory:
   `cs2-db-opcodes-session`); whatever Part C's negative control revealed
   about validation coverage.

---

## Execution order and exit criteria

A (register) → B (evidence) → C (renames, one table first, then the batch) →
D (investigation) → E (README). Each part ends with the suite green.

Done means: Tier-1 tables renamed with provenance notes and a byte-verified
round-trip; data files consistent; suite + boot green with one negative
control exercised; `docs/DBTABLES.md` written with the full appendix table;
Part D's question answered in writing; a closing summary of
verified / partial / blocked. If the cachepack round-trip in C.2 does not
behave as expected, STOP the batch, do one table by hand, document the
discrepancy, and finish the README anyway — the README is the deliverable
that keeps its value even if the renames wait.
