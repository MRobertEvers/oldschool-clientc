# Skill Guide (`skill_guide_v2`, 860): what the server owes

> Companion to `docs/questlist_chatmenu_levelup.md`, same discovery pass.
> Dominated by static dbtable content already generic in mock230 — but the
> entry point that opens this interface and picks a skill is genuinely
> missing from the decompiled corpus, not merely untraced.

## 0. Status at a glance

| aspect | finding |
|---|---|
| the two `onload=i:714,i:-2147483645` | cosmetic border-drawing only (`thinbox`, shared by 20+ unrelated interfaces) — not the real driver |
| real content | two dbtables (`skill_guide_subsections` 212, `skill_features` 213), both already loading generically |
| player-state reads | `stat_base` (level colouring), the shared per-quest progress battery (same one `questlist` needs), Magic-only owned-rune check (client-local, no new transport) |
| entry point | **missing from the corpus** — nothing opens interface 860 or picks a skill anywhere in the 9,368 files |
| mock230 | zero references anywhere — clean unstarted slice |
| new opcode needed | `db_find_filter_with_count` (7507) — declared in the CS2 op table, no host case, and the query-state struct has no room for a second predicate |

---

## 1. The layout and what's cosmetic vs. real

23 components. Window chrome (`universe`/`frame`/`close`/`resize_preview`)
is fully generic drag/resize, persisted via `persist=yes` varcs — no server
touch. The `overview` sub-tree and the `tabs`/`list` sub-tree each carry
`onload=i:714,i:-2147483645` — confirmed this is `script_714`
(`[clientscript,thinbox]`), a cosmetic border-draw shared across many other
interfaces, not skill-guide logic.

`quest_journal_button_trigger`/`skill_guide_button_trigger` (declared
components 17-18) are referenced by **zero scripts** in this corpus. There
is no per-skill list of 23 icons visible anywhere in the traced call graph —
the picker itself is part of the missing entry point (§4).

## 2. The real call graph

```
~script1902(skill, tab, from, to)      -- MISSING as a caller: nothing calls this
  sets %varcint1172 (current_skill), %varcint1173 (current_tab)
  ~script1911(...)                      -- MISSING body

script_1904 (249 lines) -- the list/detail builder, present and read in full
  tab strip:
    db_find_with_count(skill_guide_subsections.skill, %varcint1172, 0)
    db_find_filter_with_count(...subsections.id, ..., 0)
    db_getfield(...header, ...membersonly)
  feature list:
    db_find_with_count(skill_features.skill, %varcint1172, 0)
    db_find_filter_with_count(...)
    ~script9350 (level/skill match test) / ~script9347 (row builder)
```

Two dbtables, both confirmed present:

| id | name | shape | rows |
|---|---|---|---|
| 212 | `skill_guide_subsections` | `skill, id, header, membersonly` | 196 |
| 213 | `skill_features` | `icon, sprite, text, skill/level/subsection tuple, quest dbrow, otherreq string, membersonly, per-skill extra data` | 3447 |

**Correction to the working hypothesis going in**: `skill_features` has no
xp/action-rate column at all — it's a *"things you unlock and where"*
reference table (shortcuts, area unlocks, quest-gated content, spells), not
a training-methods-per-hour table. If a training-methods list exists for
this interface, it lives inside the missing `~script9176` (the Overview
tab body) — a corpus gap, not something to infer.

## 3. Player-state reads — confirmed, narrow

| read | purpose |
|---|---|
| `stat_base` | colours each "Requires: Level N" clause against the unboosted level |
| shared per-quest progress battery (`~quest_status_get`, ~200 hardcoded cases) | colours quest-gated features — **the same infrastructure `questlist` already needs**, not a new dependency |
| ~20 hardcoded varp/varbit cases (`~script9352`) | diary tier, total QP≥100, assorted booleans, prayer-book mode — for the free-text `otherreq` column |
| owned runes (Magic only) | client-local inventory/equipment read for a "Check runes" tooltip — **no new server transport**, worn slots already sync |

No `stat_xp`/xp-drop read anywhere. "Highlight owned items" is real but
narrow — the Magic "Check runes" popup only, entirely client-side.

## 4. Server obligations

| item | delivery | mock230 status |
|---|---|---|
| `skill_guide_subsections`/`skill_features` dbtable load | generic dbtable runtime | **landed** — same mechanism `questlist` uses |
| `db_find_filter_with_count` (opcode 7507) | new host op | **gap, confirmed** — declared at `cs2_command.gen.h:1045`, but `mock230_ops_db.c` has no case for it (only `SS_OP_DB_FIND`/`_WITH_COUNT`/`FINDNEXT`/`FINDBYINDEX` exist, confirmed at `mock230_ops_db.c:341-395`). Not a one-line add: the player's query state (`db_query_table`/`_index`/`_column`/`_value`, confirmed single-slot) has no room for a second predicate — this needs a query-state change, not just a new `case` |
| `stat_base` | existing generic stat op | **landed** |
| per-quest progress vars | `.varp`/`.varbit` declaration + transmit | **shared gap with `questlist`** — nothing new to add here beyond what that doc already flags |
| misc unlock varps (`~script9352`'s ~20 cases) | transmit | not individually checked — presumably pre-existing diary/QP trackers shared across many features, out of scope here |
| the actual "open skill guide" entry point | `runclientscript_ss` (same pattern as `chatmenu`'s `script_58`) or an `if_open`, unconfirmed | **entirely missing from the corpus** |

Everything else — window drag/resize, tab-strip layout, feature-row layout,
scrollbar — is client-only chrome with no server touch at all.

## 5. Corpus gaps — do not guess these bodies

- `~script9176` — the **Overview tab** body (tab id 0). The single most
  likely place any training-methods/xp-rate content would live, if it
  exists at all. Not present.
- `~script1911` — final step of window setup; strongest candidate for
  wiring the 23-skill picker and the two button-trigger components.
- `script9345` ("Check materials" handler) — sibling of `script9346`
  ("Check runes"), which is present; this one is called but never defined.
- **The entry point itself**: `~script1902` has no caller anywhere in the
  corpus, and `%varcint1172`/`%varcint1173` are never assigned outside its
  own body. No script opens interface 860 or picks a skill. Same class of
  gap as levelup_display's missing `if_open`
  (`docs/questlist_chatmenu_levelup.md` §3.1) — re-decompile the live cache
  before implementing against this trace.

## 6. Cross-reference

`grep -rniE "skill_guide|skillguide" src/net/mock/ src/game/ docs/` — two
hits, both in `docs/DBTABLES.md`'s generated schema listing, not a feature
doc. Zero implementation, zero design coverage — a clean unstarted slice,
not a stale-doc situation.

## 7. LostCity precedent — none, confirmed

`grep -ril "skillguide\|skill_guide\|skill guide" LostCity_Server/` — three
hits, all trivial (one commented-out tutorial line referencing a feature
that isn't implemented; two unrelated uses of "guide" as an ordinary word
in item text). **2004 RuneScape did not ship a skill guide at all.** Same
class as clan chat/stat orbs (`docs/PORTING_GUIDE.md` §5) — a modern feature
with no LostCity reference, not a content-porting slice.

## 8. Verdict

Dominated by static dbtable content already generic in mock230, with narrow,
mostly-shared player-state reads (nothing new beyond what `questlist`
already needs, except the Magic rune-check which is client-only). Two real
blockers before this could be built: one engine change
(`db_find_filter_with_count` needs a second query-predicate slot, not
just a new opcode case), and one corpus gap (the entry point and skill
picker are genuinely absent, not merely untraced) — re-decompile the live
cache for `~script1902`'s real caller and `~script9176` before implementing.
