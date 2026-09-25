# Giving the rest of the roster real drop tables

## 0. Where this starts from

Drop tables are not a greenfield feature. `server/scripts/drop_tables/scripts/*.rs2`
(77 files) is a 1:1 port of LostCity's own `drop tables/` directory — 164
`[ai_queue3,<target>]` bindings, threshold-walk RuneScript (`random(N)` then
cumulative `if ($dropint < X)`), documented at length in
`docs/LOSTCITY_PORT_TRIAGE.md` and gated by `tools/port_droptables_check.py`
(four bars: no duplicate bindings, no category-id drift, no exact-shadows-category
half-conversions, death_drop always restated). `lane-droptables` (merged into `v3`
2026-08-02) fixed the engine's category-dispatch bug and wrote that checker; this
plan is downstream of that work, not a repeat of it.

What that port does **not** cover: anything OSRS added after 2004scape's data
freeze. Cross-referencing the 1,700-row spawned/attackable roster
(`wiki/npc_roster.csv`) against every `[ai_queue3,...]` binding in the tree
(**not just `drop_tables/scripts/`** — the first pass of this plan only
scanned that directory and overstated the gap by ~200 npcs; quest, minigame
and boss scripts routinely declare their own death-drop handler outside it,
e.g. `quest_legends/scripts/ranalph_devere.rs2`. `port_droptables_check.py`
already scans the whole tree for exactly this reason and should have been
the model from the start):

| bucket | npcs | |
|---|--:|---|
| `covered_exact` — bound by gameval | 790 | 142 from the LostCity port, 648 from elsewhere in the tree or this plan's first generated batch |
| `covered_category` — bound by category | 67 | |
| **`todo` — no table, falls through to the tree-wide `[default]` (`death_drop=bones`)** | **843** | down from 1,527 after the correction above and one generated batch (§7) |

As of 2026-08-13, **477 of those npcs already have a generated, wiki-cited,
checker-clean table** — see §7. This section's numbers are current as of that
batch landing.

Separately, `tools/gen_npc_stats.py` (landed 2026-08-12, `docs/NPC_WIKI_STATS_PLAN.md`)
built a wiki pipeline for **combat stats** — `wiki_fetch.py` cached 978 monster
pages as wikitext, `wiki_infobox.py` parses `{{Infobox Monster}}` into per-version
blocks keyed by `id`/`idN`, and `npc_stats/<shard>/<gameval>.stats` records, per
npc, which page and which version block it resolved to. **Nobody parses
`{{DropsLine}}`/`{{DropsTableHead}}` yet** — grepped `wiki_infobox.py`,
`gen_npc_stats.py`, `wiki_fetch.py`: zero hits. But the wikitext already sitting
on disk has the drop data; a `Goblin.wikitext` excerpt:

```
==Drop table 1==
{{DropsTableHead|dropversion=Drop table 1}}
{{DropsLine|name=Bones|quantity=1|rarity=Always}}
{{DropsLine|name=Bronze sq shield|quantity=1|rarity=3/128}}
{{DropsLine|name=Coins|quantity=5|rarity=28/128|gemw=No}}
{{DropsLine|name=Nothing|rarity=38/128}}
```

So this is a parser-and-generator gap, not a fetch gap, for most of the roster.

## 1. The join must be the same one gen_npc_stats.py already made — id, not name

`docs/NPC_WIKI_STATS_PLAN.md` §2 states the rule and it applies here without
modification: **match by npc cache id against a version block's `id`/`idN` list,
never by display name.** A name match binds the wrong variant silently. This is
not a theoretical risk — it was hit twice while scoping this plan:

- The `shade` category (id 345, named in `pack/category.pack`, 6 roster npcs:
  `shadeshadow_level1..6`) looks like a clean quick win. `wiki/monsters/Shade.wikitext`
  is a real, plausible-looking page — generic Stronghold-of-Security /
  Catacombs-of-Kourend Shade, combat levels 159/140. But the six npcs in this
  category are actually the five Shades of Mort'ton "Shadow" bosses (Asyn/Fiyr/
  Loar/Phrin/Riyl Shadow) plus one more, at unrelated combat levels 40–140. The
  matching wiki pages are `Asyn Shadow.wikitext` etc. (fetched under those
  titles, confirmed via `npc_stats/s/shadeshadow_level3.stats`: `// Riyl Shadow
  (npc 1281)`). Binding this category to `Shade.wikitext`'s drop table would
  have been wrong for all six members.
- The `death_guard` category (id 559, named, 2 roster npcs, both display_name
  "Guard") looked like an even smaller win. `Guard.wikitext` exists and parses
  to **26 version blocks** — none of which list npc ids 4099 or 4100. Their
  `.stats` ledger says `source = skipped ... not consulted; authored elsewhere`
  (their hitpoints came from a hand-authored overlay, so `gen_npc_stats.py`
  never needed to resolve them against the wiki). Their real wiki identity is
  still open.

Both are exactly the failure mode `docs/NPC_WIKI_STATS_PLAN.md` was written to
prevent for combat stats. It has to be enforced here too, and the good news is
most of the work is already done: **`npc_stats/<shard>/<gameval>.stats` already
carries the resolution for every roster npc it managed to confirm.** A ledger
entry with a `Source: OSRS Wiki '<title>' (version ids include <id>)` line is a
ready-made, verified id→page→version join — reuse it instead of re-deriving it.

### 1.1 What that gives us, counted

Cross-referencing the 1,527 `todo` npcs against `npc_stats/*/*.stats`'
`join_state` (see `wiki/npc_droptable_status.csv`, column `join_state`):

| `join_state` | npcs | meaning |
|---|--:|---|
| `confirmed` | 1,153 | id-matched wiki page + version block already on file. Extract `{{DropsLine}}` from that block's section and generate. |
| `not_in_any_candidate` | 262 | wiki_fetch found candidate page(s) for this display name but this npc's id isn't in any of their infobox blocks. Needs a real disambiguation pass (see `wiki_fetch.py`'s "candidate links" mechanism) or is missing from the wiki's own infobox coverage. |
| `skipped_authored` | 86 | combat stats came from a hand-authored config, so `gen_npc_stats.py` never attempted the wiki join. The join is simply undone — run it. |
| `no_wiki_page` | 9 | no wikitext cached under this display name at all. Needs `tools/wiki_fetch.py --fetch` with the right title (probably a disambiguated one). |
| `unresolved_other:*` | 17 | parser-flagged edge cases (ambiguous match, etc.) — read individually, small enough to hand-triage. |

**1,153 of 1,527 (75%) need no new resolution work at all** — the bottleneck is
a drop-table extractor over an id→version join that already exists, not wiki
access. The remaining 374 need the disambiguation/fetch work `wiki_fetch.py`
already knows how to do, just not yet run for these specific names.

## 2. What "todo list for all NPCs" actually means here

A flat 1,527-row checklist is not an actionable unit of work — it hides that
most rows are duplicates of the same monster (multiple gamevals, one wiki page,
one drop table) and that the mechanically sound way to bind many of them is one
category trigger, not N exact ones. The real work-item count, after collapsing
by the thing that actually varies (which wiki page a table gets extracted from):

- **1,527 todo npcs collapse to 502 distinct `display_name` groups.**
  (`wiki_group` column in the ledger.) This is the practical "how many drop
  tables do I need to write" number, not 1,527 and not the raw category-id count
  (which the Shade/Guard cases above show is unsafe to treat as identical to
  "one monster").
- Of those 502, **356 groups (889 npcs) have every member sharing exactly one
  `category_id`** — a real candidate for minting a name in `pack/category.pack`
  and binding once with `[ai_queue3,_name]`, the way `chicken`/`cow`/`bear`
  already work. **But confirm the wiki join agrees before minting** — category
  membership in this cache tracks shared spell-weakness/combat mechanics as much
  as shared loot, and 61 category ids are already known to span more than one
  `display_name` (godwars category 422 alone spans Aviansie, Cyclops, Bree,
  General Graardor, Kree'arra... — nobody should mint one table for that).
- The other **146 groups (638 npcs)** have no single shared category (mixed or
  none) — these get exact `[ai_queue3,<gameval>]` bindings per npc, or per
  sub-group if the wiki data itself turns out identical across a few gamevals
  (as it did for `regicide_rabbit`/`regicide_bunny1`/`regicide_bunny2`, §5).
- **7 categories (45 npcs) are quick wins in a narrower sense**: already named
  in `pack/category.pack`, nothing binds them yet (`canafis_citizen` 20,
  `revenant` 11, `shade` 6 — **caution, see §1** — `death_archer` 3,
  `death_guard` 2 — **caution, see §1** — `duck` 2, `duckling` 1). "Named" is
  not "verified"; each still needs its own id-join check before a table is
  written for it, same as everything else. Treat this as a starting point to
  triage first, not a green light to batch-author unread.

Full detail is in `wiki/npc_droptable_status.csv` — one row per roster npc,
columns `id, gameval, display_name, combat_level, size, category_id,
category_name, status, wiki_group, join_state, wiki_title`. That CSV, not this
document, is the todo list; `docs/NPC_DROPTABLES_TODO.md` is a prioritized
reading of it.

## 3. The citation convention

Every generated or hand-extended table must say, in the `.rs2` file itself,
which wiki page and revision the drop data came from and when it was fetched —
not just that a wiki was consulted. The existing `npc_stats/*.stats` convention
(`// Source: OSRS Wiki '<title>' (version ids include <id>)`) doesn't carry a
date or a permalink; `wiki/manifest.tsv` has both (`title`, `revid`,
`requested_as`, `fetch_date`) but that link is implicit today. State it
explicitly at the point of use instead:

```
// Source: OSRS Wiki '<Title>', https://oldschool.runescape.wiki/w/<Title>,
// revision <revid>, scraped <fetch_date>. <one line on what version/table
// this npc's id matched, and anything intentionally left out (tertiary,
// quest-gated, clue drops).>
```

`<revid>` and `<fetch_date>` come straight from `wiki/manifest.tsv` (already
populated for 978 pages, all stamped 2026-08-12) — no new lookup needed for
anything already fetched. Worked example, landed on `rabbit.rs2`:

```
// regicide_rabbit / regicide_bunny1 / regicide_bunny2 (npc ids 3420/3421/3422) are the
// Tirannwn-region rabbits added for the Regicide questline -- same category (739) as
// rabbit_1..3 above but a separate gameval, so they need their own exact bindings.
// Source: OSRS Wiki 'Rabbit', https://oldschool.runescape.wiki/w/Rabbit, revision
// 15290488, scraped 2026-08-12. Tirannwn version block lists id1 = 3420, 3421, 3422
// and a 100% table of Bones (Always) + Raw rabbit (Always) -- identical to rabbit_drop
// below. Its Tertiary table (Rabbit bone, 1/4) is quest-gated to Rag and Bone Man II
// and intentionally omitted here; no varp check exists in this table family yet.
[ai_queue3,regicide_rabbit]
@rabbit_drop;
```

This npc had a *third* wiki table (Tertiary, quest-gated) that the target
RuneScript pattern has no way to express yet (no varp check in these tables at
all today) — it's named and skipped explicitly, not silently dropped. That's
the same discipline `port_droptables_check.py` bar 4 already enforces for
`death_drop` (state it or waive it with a reason) and the same one
`LOSTCITY_PORT_TRIAGE.md` used for clue-scroll drops (stripped, 21 sites,
documented as out of scope rather than omitted quietly).

## 4. Generation approach

1. **Extractor**: a new `tools/wiki_droptable.py`, same shape as
   `wiki_infobox.py`, adding a `parse_drops(version_block_wikitext) ->
   list[{name, quantity, rarity, tertiary: bool, notes}]` over `{{DropsLine}}`
   / `{{DropsTableHead}}` / `{{DropsTableBottom}}`, scoped to the matched
   version's own `==Drop table N==`/`===...===` section when a page has more
   than one (mirrors `split_versions`' existing per-version scoping in
   `wiki_infobox.py`).
2. **Rarity → RuneScript**: convert each non-`Always`, non-tertiary line's
   `a/b` fraction into the existing cumulative threshold-walk style
   (`random(N)` then `if ($dropint < X) { ... } else if ...`), same as every
   hand-ported table already does — `N` is the LCD across that npc's own
   fractions, thresholds walk in wiki order, "Nothing" lines become gaps with
   no branch. `Always` lines are unconditional `obj_add` before the roll, same
   place `npc_param(death_drop)` sits today. Tertiary and quest-gated lines are
   **not** generated in this pass — no existing table has the varp-check
   machinery for it, so treat every tertiary line as "name it in the comment,
   skip it," same as the rabbit example.
3. **Join, driven by `npc_stats`**: for `join_state = confirmed` rows, read the
   already-resolved title + version straight from the `.stats` file rather than
   re-running id-matching — one join, shared by both pipelines, one source of
   truth. For the other 374, run (or extend) `wiki_fetch.py`'s disambiguation
   pass first; do not guess a title.
4. **Binding, per §2**: category bind only after confirming every member of the
   candidate category resolves to the *same* wiki version block (byte-for-byte
   drop lines, not just same display name) — mint the name in
   `pack/category.pack` (flat `id=name`, no crawl-tool dependency for
   OSRS-only categories that never existed in LostCity's reference) and bind
   once. Otherwise, exact-bind each gameval, sharing one `[label,...]` when the
   resolved drop data is actually identical (as `rabbit_drop` now does for six
   gamevals across two eras of content).
5. **Verification**: `tools/port_droptables_check.py --check` after every batch
   — it already scans the whole tree, not just the 77 ported files, and its
   four bars (duplicate binding, category-id drift, exact-shadows-category,
   missing death_drop) apply identically to newly-authored tables. It found
   nothing wrong with the rabbit change and nothing new was introduced (663
   pre-existing, unrelated problems before and after — a tree-wide `death_drop`
   restatement gap in files this plan doesn't touch, out of scope here). Where
   possible, mutation-check a sample the way `lane-droptables` did for
   `chicken` — swap an item, rebuild, confirm the minimenu label changes,
   swap back.

## 5. Order of work

1. Triage the 374 `not_in_any_candidate` / `skipped_authored` / `no_wiki_page`
   / `unresolved_other` rows in small batches through `wiki_fetch.py`'s
   disambiguation flow, writing back a real `join_state = confirmed` into
   `npc_stats/*.stats` the same way `gen_npc_stats.py --write` already would
   (this also improves combat-stats coverage as a side effect — shared
   ledger).
2. Build `tools/wiki_droptable.py` (extractor) against a handful of already-
   `confirmed` singles first (no category-binding decisions yet) — validate
   output against a hand-checked page or two, `rabbit_drop`-style.
3. Run the extractor across all `confirmed` rows, generating one `.rs2` per
   `display_name` group (following existing directory conventions under
   `server/scripts/drop_tables/scripts/`), exact-bound to every member gameval,
   each carrying the §3 citation comment.
4. Second pass: for the 356 clean single-category groups, verify identical
   drop data across members, mint category names, and convert the relevant
   files from N exact bindings to one `[ai_queue3,_name]` binding — an
   optimization, not correctness-bearing, do after correctness lands.
5. `port_droptables_check.py --check` after every batch; keep the 502-group
   ledger (`wiki/npc_droptable_status.csv`) updated as each group's `status`
   flips from `todo` to `covered_exact`/`covered_category`.

## 7. What shipped in the first batch (2026-08-13)

`tools/wiki_droptable.py` exists and does §4 steps 1–3: parses
`{{DropsTableHead}}`/`{{DropsLine}}`/`{{DropsTableBottom}}` (generic top-level
template walk, reusing `wiki_infobox`'s brace-depth helpers so a nested
`{{plink|...}}` inside a drop name doesn't corrupt the parse), selects the
right block by `dropversion` when a page has more than one, converts
`Always`/`a/b` lines into the existing threshold-walk shape, and resolves
every item name against `configs/all.obj`'s own `name=` field rather than
guessing a gameval from the wiki text.

Run against the 1,153-npc `confirmed` bucket (`tools/wiki_droptable.py
--batch <gamevals> --write`): **147 new files, 477 npcs bound**, all
`port_droptables_check.py --check` clean (663 pre-existing problems before
and after — zero introduced). Three real bugs were caught building this,
each worth stating so the next batch doesn't re-hit them:

1. **A stale ledger nearly created a duplicate binding.** The CSV still
   listed `regicide_rabbit`/`bunny1`/`bunny2` as `todo` after they'd been
   hand-bound into `rabbit.rs2` (§0 of this section's prior revision) — a
   first `--write` run generated a second file binding them again under a
   second label of the same name. Fixed two ways: labels are now
   `wiki_<slug>_drop` (never bare `<slug>_drop`, so a hand file and a
   generated file can't collide on a label even if they somehow target the
   same npc), and every run checks bindings **fresh from the `.rs2` files
   themselves**, not from any CSV snapshot.
2. **That same check was scoped too narrowly at first** — only
   `drop_tables/scripts/`, matching this plan's original (wrong) coverage
   scan. Widening it to the whole tree is what surfaced the ~200-npc
   overcount folded into the table above, and is why the generator's
   pre-flight check now globs `server/scripts/**/*.rs2`.
3. **Naive slugification produced invalid item references.** `"Water rune"`
   slugified to `water_rune`; the actual gameval is `waterrune`. Every
   multi-word or irregularly-named item was silently wrong until this was
   caught by hand-checking a sample against `configs/all.obj` — not by the
   drop-table checker, which has no reason to know what a valid obj id looks
   like. Fixed by resolving every drop name against `configs/all.obj`'s own
   `name=` field (case-insensitive exact match; when more than one gameval
   shares a display name — reskins, joke items, quest variants — prefer the
   one without an obvious prefix like `fake_`/`roguetrader_`/`100guide_`,
   then the shortest gameval) instead of generating a guess. All 430 distinct
   item references in the batch were verified present in `configs/all.obj`
   after the fix; none were before it.

A fourth bug, caught by a direct question rather than by the checker: the
death_drop restatement was **unconditional**
(`obj_add(npc_coord, npc_param(death_drop), 1, ...)`), matching every
hand-ported table. But 28 npcs in this tree are configured
`param=death_drop,null` — OldSchool giant spiders and (still `todo`)
`shadow_spider` drop nothing at all, not even bones, and
`shared_droptables.rs2` says so explicitly. None of the 28 were in this
batch's 477, so nothing shipped broken, but the *next* batch could easily
include one and would have silently generated `obj_add(..., null, ...)`.
Fixed by guarding the restatement instead of asserting it —
`if (npc_param(death_drop) ! null) { obj_add(...); }` — which is correct
for every npc regardless of its own death_drop value (including the common
case of a non-bones override like `wolf_bones`/`dragon_bones`, which
`npc_param(death_drop)` already resolves correctly per-npc at runtime) and
needed no per-gameval special-casing. All 147 files were regenerated with
this fix; the checker and item-reference verification (§ above) were re-run
clean against the new output.

What the 477 covered *don't* include, on purpose: anything with a skipped
rarity/quantity line (ranges outside the simple `a-b` pattern, comma lists,
tables whose cumulative fractions overflow their own stated denominator —
663 npcs), an npc whose id didn't resolve to a page/version at all (still the
join-gap work in §1.1/§5 step 1), or a table with nothing beyond the default
bones drop (126 npcs — matching the project's existing rule for `duck`: a
binding that states nothing is worse than no binding).

## 8. Scope, stated

- **In scope**: main (non-tertiary, non-quest-gated) drop tables for the 1,527
  `todo` roster npcs, sourced from already-cached or freshly-fetched OSRS Wiki
  monster pages, joined by npc id the way combat stats already are, cited with
  page + revision + fetch date in the generated `.rs2` file.
- **Out of scope, explicitly** (consistent with what the original LostCity port
  already excluded, per `docs/LOSTCITY_PORT_TRIAGE.md`): Treasure Trail clue
  drops, tertiary/quest-varp-gated drop lines (no varp-check machinery exists
  in this table family), the 16 existing exact bindings with no roster row
  (`goblin_guard`, `kalphite_flyingqueen`, ... — not attackable/spawned per the
  roster's own definition, left alone), and npcs the wiki itself has no
  infobox coverage for.
- **Not this plan's job**: minting every unnamed category regardless of table
  content (§2 already found 61 category ids that must NOT become one shared
  table) — category-binding is a follow-on optimization pass (§5 step 4), not
  a precondition for closing the correctness gap.

## 9. `death_drop` is a statement now, not a fallthrough (2026-08-14)

Reported as "Tzhaar are still dropping bones". They were, and so was most of the
roster, for a reason no drop-table script could have fixed.

### The mechanism

`general/configs/npc_default.npc`'s `[default]` block authors
`param=death_drop,bones`, and `npc_def_seed_from_cache` copies the whole default
record — params included — into every npc def. An npc that never states the
param therefore drops bones as a *fallthrough*, and neither consumer can tell
that apart from a considered `bones`: `[ai_queue3,_]`
(`skill_combat/npc_combat.rs2`) and all the generated `wiki_*.rs2` tables both
just restate `npc_param(death_drop)`.

§7's guard (`if (npc_param(death_drop) ! null)`) is often mistaken for the fix.
It only honours npcs *already* configured `null`; it cannot invent that
statement. Three populations were wrong at once, measured across the 1,700-row
roster:

| | npcs | symptom |
|---|---|---|
| page states no remains | 289 | phantom bones — TzHaar (rock), vyrewatch, rockslugs, killerwatts, animated tools |
| page states **ashes** | ~146 | left bones instead — every demon (Vile/Malicious/Fiendish/Abyssal/Eldritch/Infernal) |
| page states a bones **variant** | ~300 | left the variant *and* plain bones — Big/Dragon/Wolf/Zogre/Wyvern/Hydra/monkey families |

### The rule

An npc's own cited wiki page decides, via its `Always` remains line — `bones`,
`ashes` or `remains` as whole words. `tools/gen_npc_stats.py` derives it
(`death_drop_for_page`) and states `param=death_drop` in **all 1,278** roster
blocks, including the 788 whose answer is the same plain `bones` the `[default]`
would have given. Restating those costs a line in a generated file and buys the
property whose absence caused every bug in this family: the value is always a
statement, never a default that happens to read correctly.

`tools/wiki_droptable.py` skips that same line when building the table, so
nothing is dropped twice. **Both tools call one function** —
`wiki_droptable.death_drop_choice` — deliberately: computed twice, the two
answers drift and the npc drops its remains twice or not at all.

Resulting distribution: 788 `bones`, 169 `null`, 140 `big_bones`, 41
`dragon_bones`, 25 `wolf_bones`, 16 `vile_ashes`, 14 `zogre_bones`, 12 `ashes`,
11 `malicious_ashes`, 10 `fiendish_ashes`, and 22 more families.

Regenerating the tables against this took the batch from 147 files/477 npcs to
124/424. The 53 that dropped out are correct: their whole table was their
remains, which `param=death_drop` now answers on its own through `[ai_queue3,_]`
— the project's existing `duck` rule (a binding that states nothing is worse
than no binding).

### Four things that bit, worth not repeating

1. **Plain `Bones` keeps its blanket filter in `build_table`, at every rarity.**
   Several pages state bones twice — once `Always`, once as a fraction from a
   sub-table the cumulative-rarity model cannot represent (Bloodveld: `Vile
   ashes` Always, then `Bones 10/128`). Letting the fractional one through
   pushed 5 bloodvelds, the ancient hellhound and the giant frog past their own
   denominator (133/128), which fails the sanity check and deletes those tables
   outright. Widening this needs the sub-table model first.
2. **An "ambiguous page version" fallback must not fall back to `bones`.** That
   is the default being removed. `tzhaar_hur1` regressed exactly this way — its
   page's blocks are headed `Drops`/`Pickpocketing`, so `select_blocks` matches
   nothing. Ask the weaker question the evidence still answers: does *any* main
   block state remains? None does, so `null` is a conclusion, not a guess.
3. **A kill-based test for a TzHaar silently never runs.** `selftest_find_npc`
   scans active npc slots; the selftest scene is Lumbridge and TzHaar spawn at
   2506,5168. Written that way it passed against deliberately broken data.
   `torirs_server_world.c`'s "the death drop is content's" case D asserts on the def
   instead — `ToriRSServer_ContentNpc` + `ToriRSServer_ContentNpcParam`, the exact pair
   `npc_param` reads at rank 1 — and covers null, plain, variant and ashes.
4. **That call's "was it stated" return is always true**, because of the default
   seed. Asserting it is a check that cannot fail. Assert the value.

### The version split (fixed in the same pass)

`write_group` wrote `results[0]`'s table under a single `wiki_<slug>_drop` label
and pointed every `[ai_queue3]` on the page at it. A wiki page is one *monster*,
not one *drop table*, so that served **48 npcs across 11 groups** another
version's loot: the level-13 giant frog got the level-99 one's table, the God
Wars hellhound got the surface one's smouldering stone, the level-149
TzHaar-Ket got the level-221's, and 20 goblins and 10 zombies got the wrong one
of their page's two.

`report_one` already resolved each npc to its own `dropversion` and built that
version's table -- the information was there and was being discarded at the last
step. `partition_by_table` now groups a page's npcs by the table they actually
generate (by the table, not by the version string, so two versions that roll the
same drops still share one label) and `write_group` emits one label per group:

    [ai_queue3,dungeon_rat]
    @wiki_dungeon_rat_full_tail_drop;

    [ai_queue3,dungeon_rat2]
    @wiki_dungeon_rat_normal_drop;

    // dropversion: Full tail
    // covers: dungeon_rat
    [label,wiki_dungeon_rat_full_tail_drop]
    ...

Single-table pages keep the bare `wiki_<slug>_drop` label, so 113 of the 124
files are byte-identical across this change and only the 11 real multi-version
pages moved. Regeneration is deterministic (same md5 over two full runs).

Worth stating because it reads backwards: npc 2865's version is `Full tail` and
it drops **Raw rat meat**, while 2866/2867 are `Normal` and drop **Rat's tail**.
That is what the page says -- a rat with a full tail has no severed tail to
leave -- and the page annotates the third version "doesn't drop meat, even
though some of their tails are long". Verify against the wikitext before
"correcting" it.

### The regression gate

`tools/test_droptable_versions.py` (`make -C src test-droptable-versions`) pins
both halves, and every check in it was confirmed to fail against deliberately
broken input rather than merely passing:

- `partition_by_table` splits differing tables and, equally, does *not* split
  identical ones -- otherwise every page becomes one label per npc.
- Every `@label` in a generated file resolves, no duplicate `[label,...]`, and
  at least 11 files still carry more than one table.
- `dungeon_rat` drops `raw_rat_meat` and **not** `rats_tail`; `dungeon_rat2` the
  reverse. A named, recorded case rather than a shape check.
- All 1,278 roster blocks state `param=death_drop`, and every family that was
  wrong (`null`, `bones`, the variants, the ashes) is still represented.
- No table emits an item its npc's `param=death_drop` already gives.

### Still open

Nothing from this section. `port_droptables_check.py`'s 607 findings are
unrelated and pre-existing (`slayer_superior.rs2` and friends, none of which
restate `npc_param(death_drop)`).
