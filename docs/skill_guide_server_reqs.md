# Skill Guide (`skill_guide_v2`, 860): what the server owed

> **BUILT — 2026-08-01, verified in the client.** The case file is
> [`skill_guide.md`](skill_guide.md). This document is the discovery pass that
> preceded the build; the record of what landed comes first, then the
> corrections, then the original body kept as written. **Four of its
> load-bearing claims were wrong**, and one of them — the engine blocker it
> names — does not exist.

## What landed

One click: "View \<skill\> guide" on any of the 24 stats-tab cells. The server
arms op 2 at login, and answers the click with a mount and a layout script, in
that order. Once the guide is open, tab clicks never reach the server —
`%varcint1173` and a local re-layout — so opening it on the right skill is the
*whole* obligation.

| layer | what was missing | what landed |
|---|---|---|
| trigger | all ten `IF_BUTTON<n>` opcodes collapsed into one `SS_TRIGGER_IF_BUTTON`, so content could not tell op 2 from op 1 on the same component | `SS_TRIGGER_IF_BUTTON1..IF_BUTTON10` = **168..177** (`SS_TRIGGER_MAX` **168 → 178**), allocated by a new `EXTRA_TRIGGERS` mechanism in `gen_opcode_meta.py`. Reference-named: `ClientGameProt.ts:71` says the numbered family *is* `IF_BUTTON<n>` at this revision |
| opcode | `runclientscript_ss` (11002) is fixed at one int and two strings; clientscript 1902 takes four ints | `SS_OP_RUNCLIENTSCRIPTVARARG` = **11003** (`SS_OPCODE_MAX` **11003 → 11004**), in the reference's own vararg convention. Coverage **246/399 → 248/400** |
| client VM | `if_callonresize` unimplemented — the panel mounted and then aborted the client | `CS2_OP_IF_CALLONRESIZE` **2927**, arity `(1 in, 0 out)` read off script 1911's bytecode, queued and drained beside the `onTimer` loop |
| client VM | `if_getcomponentparam` in neither opcode table — 20 scripts in this cache fail to decompile at it | `CS2_OP_IF_GETCOMPONENTPARAM` **2703**, `(3 in, 1 out)`, arity pinned by a stack-balance argument at script 9181 rather than inferred |
| content | nothing armed op 2 and nothing opened 860 | `interface_skill_guide/` — `skill_guide.constant` (107 lines) and `skill_guide.rs2` (208 lines): `~skill_guide_login` arms all 24 cells, `~skill_guide_open` mounts and runs 1902, 24 `[if_button2,stats:<cell>]` triggers. One line in `player/login.rs2` |

The permanent check is `mock230 --selftest` section **"skill guide"**
(`mock230_world.c:7225`), which sends a real `IF_BUTTON2` per cell and asserts
mount-before-clientscript, the type string `"iiii"`, and 24 distinct skill
indices. Three mutations were run against it and each produced a distinct
failure — table in [`skill_guide.md`](skill_guide.md) §6.

## What it cost

Zero new packets — `IF_BUTTON2`, `IF_OPENSUB` and `RUNCLIENTSCRIPT` were all
already on the wire. Two ServerScript surface additions (one trigger family,
one opcode) and two client CS2 opcodes. **No engine fallback was widened**:
both new `run_if_button` lookup rungs are content, and `enum Mock230Fallback`
is unchanged. `player->last_verb` turned out to be written by the engine and
read by nothing — there is no `last_verb` command here or in the reference —
and the comment claiming otherwise was corrected rather than given a command.

## What was deliberately left

- **The Overview tab.** It aborts the client: `~script9176` and the
  `9150..9199` widget library under it use twelve CS2 opcodes with no
  signature (`211 212 213 215 4036 8003 8012 8018 8019 8022 8023 8024`).
  Measured, not assumed — the same gosub walk with 9176 excluded finds none at
  all. Content therefore opens on `^skill_guide_tab_default = 1`; set it back
  to **0** when the twelve land and nothing else moves. **Clicking Overview in
  the strip still takes the client down**, because the tab is the cache's and
  no packet can hide it. This is the top follow-on.
- **`cc_callonresize` (1927).** Its row in `cs2_command.gen.h` claims one
  argument, which no other `cc_*` component op takes, and no script in this
  cache calls it — nothing to verify an arity against, so it stays
  unimplemented and aborting.
- **Persistence.** Carried by the plan into this stage and cut here: the skill
  guide has no persisted state at all (`%varcint1172/1173` are client varcs),
  so there was nothing to verify a save/load path against. Landed in stage 2
  (`farming_tools`), which has varbits to assert.

## What the discovery pass got wrong

> 1. **"`db_find_filter_with_count` (7507) — new host op, gap, confirmed" is
>    wrong, and it is the wrong VM.** `script_1904` is a *clientscript*: 7507 is
>    declared `{2,0,1,0,1}` in `cs2vm2_opcode_stack.gen.h`, dispatched at
>    `cs2vm2.c`, and fully implemented in `src/game/rs_cs2_host.c` — including
>    the in-flight query intersect this document says the query state has no
>    room for. `mock230_ops_db.c` is the *ServerScript* db surface and was never
>    in this path. Nothing about the skill guide needed a new db opcode.
>
> 2. **The real blocker was the packet, not the query.** `runclientscript_ss`
>    (11002) is fixed at one int and two strings; clientscript 1902 takes four
>    ints, and there was no way to spell that. That is `SS_OP_RUNCLIENTSCRIPTVARARG`
>    (11003). The second blocker was a *trigger*: all ten `IF_BUTTON<n>` opcodes
>    collapsed into one `SS_TRIGGER_IF_BUTTON`, so content could not tell op 2
>    ("View … guide") from op 1 ("Toggle … XP") on the same component, and
>    `player->last_verb` — which the engine sets — is read by nothing and has no
>    command behind it. That is `SS_TRIGGER_IF_BUTTON1..10` (168..177).
>
> 3. **"The entry point is missing from the corpus" is right, and it is the
>    answer rather than a gap.** `~script1902` having no caller in 9,433 scripts,
>    in front of an interface whose only two onloads are the cosmetic `thinbox`
>    border, is the cache saying the server runs it. §5's advice to re-decompile
>    first was followed: 1902, 1904 and 1911 are all present and complete. What a
>    fresh decompile does *not* recover is `~script9176` (the Overview body) or
>    `script9345` — both are in the decompiler's 292-script failure list, i.e.
>    present in the cache and unreadable by the tool, not absent.
>
> 4. **"Two dbtables … both already loading generically — landed" is wrong.**
>    The server's `.dbtable`/`.dbrow` parser reads the *authored* grammar
>    (`column=`, `data=`); the machine-exported `configs/all.dbtable` and
>    `all.dbrow` use `columndef=`/`values=`, which it silently skips. So
>    `skill_guide_subsections` loads as a name and an id with **zero columns**
>    and 196 empty rows. It does not matter here — the client reads those tables
>    from the cache — but it is not "landed".
>
> 5. **What actually blocks the feature today is neither of the two this
>    document names.** It is twelve CS2 opcodes with no signature in the client
>    VM (`211 212 213 215 4036 8003 8012 8018 8019 8022 8023 8024`), all of them
>    inside `~script9176`'s subtree — the **Overview** tab. Everything else opens
>    and draws. See `docs/skill_guide.md` §5.
>
> 6. Minor: `%varcint1172` is a key of `enum_681`, not a stat id, and the
>    quest-progress battery in §3 is not on the path this build exercises —
>    it is read by `~script9350`/`~script9352` for the *feature-row* colouring,
>    which draws uncoloured rather than not at all when the varps are absent.

---

# The discovery pass, as written

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
