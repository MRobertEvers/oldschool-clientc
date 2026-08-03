# Collection Log (`collection` 621, `collection_overview` 908): what the server owes

> **UPDATE 2026-08-03 — body-draw bootstrap landed.** Stretch desktop's
> `collection_init` (script_2240) never calls script7798 (only the mobile
> enum_1132 branch does), so mount alone left tabs visible and the parchment
> empty. `~collection_open` now mounts 621 then `runclientscript*` 7798
> (skill-guide order); tab/list/close `if_button1` handlers re-run the same
> draw. Constant `^clientscript_collection_draw` lives in
> `interface_collection/configs/collection.constant`.
>
> **UPDATE 2026-08-03 — content plumbing landed.** Container 620 was already
> proven. What this feature still owed is now in
> `server/scripts/interface_collection/`: the ~20 varps, open procs for 621/908,
> `%collection_count_max` from the catalog enums, `~collection_earn` /
> `::collect`, and a catalog-guarded hook on `~npc_default_death`. Character
> Summary ops that open these panels are in `interface_summary/` — see
> [`account_summary_server_reqs.md`](account_summary_server_reqs.md). Per-table
> drop scripts and kill-count/PB scratch remain ongoing content.
>
> **Blockers A + B, 2026-08-02 — both cleared; A was needed after all.**
> A (`runclientscript` carrying ints) *is* on this path for stretch: script_2240
> does not self-boot 7798 the way overview's 7802 sets `if_setonresize`. B (the
> container registry) was the other structural blocker and is gone, container
> 620 proven end to end including a logout round trip. **Still blocked on:
> nothing structural.**
>
> **UPDATE 2026-08-02 (lane-blockers): the container blocker is CLEARED.**
> `container_for`/`container_dirty` are a registry now — see
> [`mock230_containers.md`](mock230_containers.md). Resolve-or-create means
> **no `inv_collection` row, no player struct field and no case were needed**:
> container 620 sizes itself to 500 from the cache the first time content names
> it, transmits as a whole-container UPDATE_INV_FULL because it is past 32
> slots, and persists as `[container.620]`.

> Companion to `docs/questlist_chatmenu_levelup.md` and
> `docs/chrome_panels_server_reqs.md`, same discovery pass. The engine half of
> the "500-slot container" finding is done; the content half is
> `interface_collection/`.

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `collection` (detail view) | 621 | **landed** (open + body draw via 7798 + tab/list/close) | per-source kill-count/PB scratch; earn hooks beyond default death |
| `collection_overview` (summary grid) | 908 | **landed** (open + ring/counts) | same |

---

## 1. The mechanism

**Server bootstrap (required on stretch):** after `if_opensub` of 621,
`~collection_draw` runs `runclientscript*(^clientscript_collection_draw=7798)`
with the selected tab's scroll/bg/text/scrollbar comps and the tab struct from
`enum_2102`. Script 7798 paints `~steelborder` and calls
`~collection_draw_list`. Without that push, stretch open shows tabs only.

`collection.if` `[tabs]` onload (`i:2388,i:0`) → `script_2388`
(`collection_draw_tabs_all`) draws the 5 category tabs (Bosses/Raids/Clues/
Minigames/Other). The body list/grid is **not** from that onload — it comes
from 7798 → `collection_draw_list` (`script_2730`/`2731`), which for the
selected source calls **`~collection_draw_log`** (`script_2732.cs2`).
`collection_overview.if`'s onload (`script_7802`) is pure chrome (same
drag-follow-toplevel/close-button pattern as the loot tracker's
`script_7128`, `docs/chrome_panels_server_reqs.md` §3) and delegates to
`script_7809`, which draws 5 subsection buttons, a "Collections Logged: N/M"
bar, and a 12-slot "Latest Items" ring.

Corpus gap, same class as `questlist_draw`/loottools' `script7166`:
`~script2600` (called from `script_2599.cs2`/`script_2240.cs2`) and
`script1912` (`script_7809.cs2:17`) have no definitions anywhere in this
decompile. Neither affects the finding below — both are chrome/geometry
helpers, confirmed by call-site shape, not data plumbing.

## 2. The critical finding — a real container, not a bitset or a native store

`script_2732.cs2:65`:
```
$int14 = inv_total(inv_620, $namedobj17);
```

Every item row's "obtained" state — greyed vs. lit, stack count, per-source
"Obtained: X/Y" colour — is drawn by calling the standard IF3 opcode
**`inv_total`** (confirmed client-side, `src/game/rs_cs2_host.c:876-887`)
against **container id 620**. This ruled out the two more exotic hypotheses
going in (a client-native store, or a bitset of varbits): confirmed zero
"collection"-named ops anywhere in `3rd/rscache/src/cs2/cs2_command.gen.h`.

**Container 620 is a real, named cache config**:
`OSRS-Content/osrs239-content/configs/all.inv.compack:621` →
`620=collection_transmit`; `configs/all.inv:1863-1864` → `size=500`
(confirmed). `docs/INVENTORIES.md:17` independently lists `620 | Collection
log` in the editor's mock inventory registry — three independent sources
agree. It rides the exact same generic
`inv_total`/`UPDATE_INV_FULL`/`UPDATE_INV_PARTIAL` machinery the bank
already uses — no new opcode, no new wire format.

**And mock230 supports it via the container registry** (re-measured
2026-08-02 / content 2026-08-03): resolve-or-create sizes 620 to 500 from the
cache, dirty/transmit work like any other large container, and persistence is
`[container.620]`. See [`mock230_containers.md`](mock230_containers.md). The
historical note below is kept so the discovery trail stays readable — the
three-case `container_for` it describes no longer exists.

~~**And mock230 has zero support for it, confirmed directly**:
`container_for()` (`src/net/mock/mock230_scripts.c:1504-1526`) has exactly
three cases — `inv_backpack`, `inv_worn`, `inv_bank` — and falls through to
`*out_slots = 0; return NULL;` for anything else, including 620.
`container_dirty()` has the identical three-way branch: a write to inv 620
would mark nothing dirty and transmit nothing. There is no
`struct Mock230Player` field for it.~~ **Superseded** — registry + content
plumbing landed; §3's status column is current.

**Why this was the largest finding in the series**: 500 slots × (obj id +
count) per player, and unlike the bank it's **monotonic** — write-once,
grows forever, must persist indefinitely. Persistence callers exist now
(`mock230_save_player`/`_load_player`); the remaining work is content density.

## 3. Server obligations

| state | meaning | delivery | mock230 status |
|---|---|---|---|
| container **620** (`collection_transmit`, 500 slots) | the load-bearing per-item obtained+count state | generic container wire, same as bank | **landed** — registry resolve-or-create; persists as `[container.620]`; selftested |
| `%collection_count`/`_max` | overview "Collections Logged: N/M" | generic varp transmit | **landed** — `interface_collection/`; max from catalog enums |
| `%collection_count_highscores` (Account Summary's own line) | kept in lockstep with `%collection_count` on earn/login | generic varp transmit | **landed** |
| per-subsection counts (Bosses/Raids/Clues/Minigames/Other ×2 each) | the 5 progress rings | generic varp transmit | **declared**; subsection bump on earn still open |
| 12-slot "Latest Collections" ring (item + day-obtained ×12) | the overview's recent-items strip | generic varp transmit | **landed** item ring; day-obtained pairing still open |
| `%varbit9535` (`current_runeday`) | "obtained N days ago" display | pre-existing systemwide clock | not confirmed landed |
| per-source kill-count/PB scratch varps (`%collection_category_count` family) | a **transmit mailbox** — server copies the selected source's real counter into shared scratch varps on demand | generic varp transmit | not declared; kill-count infrastructure still absent |
| `%varbit14577` (`collection_player_bodytype`) | gendered item-variant substitution for counting | generic varbit transmit | **declared** on the collection carrier |
| point-of-earning trigger | write the slot + message the player | content | **partial** — `~collection_earn` + `::collect` + catalog-guarded hook on `~npc_default_death`; per-table drops ongoing |

The transport for every varp/varbit row is generic and already works. The
container is no longer novel engine surface — see
[`mock230_containers.md`](mock230_containers.md).

## 4. Landed vs. gap

- **Landed**: container registry path for 620, open procs for 621/908, stretch
  body-draw bootstrap (`runclientscript*` 7798 + tab/list/close handlers),
  aggregate varps, catalog-derived `%collection_count_max`, `~collection_earn` /
  `::collect`, default-death catalog hook, Character Summary ops that mount
  the panels.
- **Gap, content**: earn hooks in every `drop_tables/` script; subsection
  counter bumps on first find; kill-count/PB mailbox; day-obtained ring half.
- **Not new work**: per-source kill counts/PBs look collection-log-adjacent
  but are a pre-existing dependency this feature merely displays — the same
  relationship the loot tracker has to the NPC-death drop-roll mechanism
  (`docs/chrome_panels_server_reqs.md` §3.1).

## 5. LostCity precedent — none, confirmed

`grep -ril "collection.?log" LostCity_Server/` — zero hits across both
`engine/` and `content/`. Expected: collection log shipped to live OSRS in
June 2020, sixteen years after rev 254. Fully greenfield on both engine and
content sides — closer in shape to the friends/clan-chat class
(`docs/PORTING_GUIDE.md` §5.2) than to questlist/levelup, which at least had
something to translate. The nearest LostCity-side analogue in spirit is the
bank itself (a large per-player container synced over the same wire
primitives) — not a collection-log precedent, just the closest container
this port has already built.

## 6. What this doc does not cover

- `~script2600`'s and `script1912`'s bodies — missing from this decompile;
  confirmed geometry/chrome helpers by call-site shape, not re-verified by
  reading them.
- The actual point-of-earning trigger's real body — genuinely absent from
  the corpus; re-decompile before implementing.
- Whether `%collection_count_highscores` and `%collection_count` are meant
  to be the same value read twice or genuinely track different things —
  flagged as a naming inconsistency, not resolved here.
