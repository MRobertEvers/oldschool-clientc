# NPC drop tables — todo

Full detail lives in `OSRS-Content/osrs239-content/wiki/npc_droptable_status.csv`
(one row per roster npc: `id, gameval, display_name, combat_level, size,
category_id, category_name, status, wiki_group, join_state, wiki_title`). This
file is a prioritized reading of it. See `docs/NPC_WIKI_DROPTABLES_PLAN.md` for
the how and why.

Status as of 2026-08-13, against the 1,700-row spawned/attackable roster
(`wiki/npc_roster.csv`), counted against **every** `[ai_queue3,...]` binding
in the whole content tree (an earlier pass of this doc only scanned
`drop_tables/scripts/` and overstated the gap by ~200 npcs already bound
elsewhere — quest/minigame/boss scripts bind their own; see plan §7):

- [x] 790 npcs — exact `[ai_queue3,<gameval>]` binding exists (142 from the
      LostCity port, 648 from elsewhere in the tree or from this plan's first
      generated batch, §0 below)
- [x] 67 npcs — category binding exists (`chicken`, `bear`, `cow`,
      `ice_warrior`, `unicorn` — `werewolf` category exists but binds 0 of the
      roster's actual Werewolf npcs; see plan §1 caution pattern)
- [ ] **843 npcs — no drop table, falls through to `[default]` (bones only)**

## 0. Proof of pipeline, and the first real batch (done)

- [x] `tools/wiki_droptable.py` — the extractor plan §4 called for. Parses
      `{{DropsLine}}` tables, resolves items against `configs/all.obj`'s own
      `name=` field (not a slug guess — see plan §7 for the bug that caught),
      converts to the existing threshold-walk RuneScript shape.
- [x] **477 npcs, 147 new files, `wiki_<slug>.rs2`**, generated from the
      `confirmed`-join bucket, every one checker-clean (0 new problems against
      the 663 pre-existing, unrelated ones). Covers every npc whose wiki table
      was fully expressible (plain fractions/`Always`, simple quantity ranges,
      every item name resolving) with nothing left to hand-review.

- [x] `regicide_rabbit` / `regicide_bunny1` / `regicide_bunny2` (npc ids
      3420/3421/3422) — exact-bound onto the existing `rabbit_drop` label in
      `server/scripts/drop_tables/scripts/rabbit.rs2`. Wiki-confirmed identical
      to `rabbit_1..3`'s existing table (Bones + Raw rabbit, both Always).
      Source cited in-file: OSRS Wiki 'Rabbit', revision 15290488, scraped
      2026-08-12. Verified with `tools/port_droptables_check.py --check`
      (0 new problems; regicide_rabbit/bunny1/bunny2 not in its output).

## 1. Triage the join gaps first (~316 npcs) — blocks everything downstream for these rows

Grouped by `join_state` in the ledger (re-derive exact counts from the CSV —
these shift as batches land and `status` flips out from under a `join_state`
row). These need `tools/wiki_fetch.py`'s disambiguation pass (or a fresh
fetch) before any drop table can be written — skipping straight to a
name-matched wiki page for these is exactly the mistake plan §1 documents
(Shade, Guard).

- [ ] `not_in_any_candidate` — ~256 npcs. Candidate page(s) exist under the
      display name but this npc's id isn't in any infobox block on them.
- [ ] `skipped_authored` — ~40 npcs. Combat stats came from a hand-authored
      config, so the wiki join was never attempted for these at all. Run it.
- [ ] `no_wiki_page` — ~7 npcs. Nothing fetched under this display name; fetch
      with the correct (probably disambiguated) title.
- [ ] `unresolved_other:*` — ~13 npcs. Parser-flagged edge cases; small enough
      to hand-triage individually.

(`death_archer`, `death_guard`, `canafis_citizen` — this section's original
examples — turned out to already be `covered_category` from bindings that
exist elsewhere in the tree, caught when §0's batch widened the coverage scan
to the whole tree instead of just `drop_tables/scripts/`. Left as a reminder
that "todo" in this doc is a live CSV column, not a fixed list — don't trust
a name that appears in this file without checking `status` in the CSV first.)

## 2. Confirmed joins, not yet generated (~527 npcs, ready for `tools/wiki_droptable.py --write`)

477 npcs from this bucket are already done (§0). The remainder either weren't
in the batch's input list (re-run `wiki_droptable.py --report` against the
current `confirmed`-and-`todo` rows to get a fresh list — the tool's own
existing-binding scan is authoritative, not the CSV) or came back with a
skip reason worth a look before forcing:

- [ ] Skipped due to an unhandled rarity/quantity shape (comma lists, `?`,
      cumulative fractions that overflow their stated denominator) — these
      are real bosses/uniques with real tables; hand-author or extend
      `wiki_droptable.py` rather than silently dropping the line.
- [ ] Skipped due to an unresolved item name against `configs/all.obj`
      (~13 instances in the first batch — mostly parenthetical wiki
      annotations like "Fang (Tombs of Amascut)" or items this cache may not
      have ported yet, e.g. "Goat horn"). Small enough to hand-triage.
- [ ] `duck` (category 425) — still `todo`; the wiki `Duck` page has no drop
      table section at all (ducks aren't lootable in OSRS). Per
      `shared_droptables.rs2`'s own stated rule for this exact npc: **do not
      bind it** — a table that states nothing is worse than no table.
      `duckling` (category 426) is `confirmed`-joined and untested; check it
      separately before assuming the same.

## 3. Category-mint candidates, after §2 lands

(Counts below are the pre-batch, pre-whole-tree-rescan snapshot from
2026-08-13 morning — several of these groups (e.g. `death_archer`,
`death_guard`, `canafis_citizen`, `revenant`) turned out already covered once
§0/§1's correction landed. Re-derive live counts from the CSV — `status=todo`
grouped by `wiki_group` with all members sharing one `category_id` — before
starting work here; this section is a shape-of-the-work example, not current
truth.)

Every member shares exactly one `category_id` *in the cache* — a candidate for
minting a `pack/category.pack` name and collapsing to one `[ai_queue3,_name]`
binding, per plan §5 step 4. **Do this only after confirming the resolved
drop data is actually identical across members** — do not batch-mint off the
category id alone. Full list is the ledger filtered to `status=todo` with a
unique `category_id` per `wiki_group`; do not hand-copy it here, it will drift
out of sync with the CSV.

## 4. Exact-bind only, no shared category (146 groups / 638 npcs)

Mixed or absent category — bind per gameval (or per confirmed-identical
sub-group, `rabbit_drop`-style) once joined. Largest groups worth tackling
first for coverage-per-effort (counts from plan §2's cross-tab of unnamed
category ids — re-derive from the ledger before starting, this list is a
snapshot, not a source of truth):

- [ ] Undead one (cat 274, 92 npcs) — zombie family, verify sub-variants
      (armed/unarmed, numbered tiers) don't actually need separate tables.
- [ ] Godwars Dungeon avatars/bodyguards (cat 422, 53 npcs across 24 distinct
      `display_name`s — General Graardor, Commander Zilyana, Kree'arra, and
      their bodyguards). **Not one table** — 24 separate wiki lookups.
- [ ] Cave goblin / Dorgesh-Kaan guard (cat 311, 51 npcs, 2+ distinct names).
- [ ] Skeleton family (cat 257, 48 npcs) — mixes "Skeleton" and "Giant
      skeleton" under one category; confirmed NOT one wiki page.
- [ ] Dragons (cat 347, 42 npcs, 15 distinct names — bronze through brutal
      variants, each its own table).
- [ ] Black demon (cat 275, 41 npcs).
- [ ] (Full ranked list: filter the ledger for `status=todo`, group by
      `wiki_group`, sort by member count — the numbers above are a snapshot
      from 2026-08-13.)

## 5. Explicitly not on this list

- The 16 existing exact drop-table bindings with no roster row (`goblin_guard`,
  `kalphite_flyingqueen`, `lady_pirate`, ...) — not attackable/spawned per the
  roster's own definition. Left alone.
- Treasure Trail clue drops, tertiary/quest-varp-gated lines — no table in this
  family has the machinery to express a varp check yet (plan §6).
