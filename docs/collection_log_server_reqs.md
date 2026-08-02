# Collection Log (`collection` 621, `collection_overview` 908): what the server owes

> **NOT BUILT — triaged 2026-08-02: NEEDS-ONE-THING, and it is the highest
> leverage per line in the survey.** The blocker is the fourth container this
> doc already names: `container_for` — re-measured today at
> `mock230_scripts.c:2088`, not §2's `1504-1526` — still has exactly three
> hardcoded cases, and `container_dirty` the same. ~50-80 lines (a
> `Mock230Item*` + size on `Mock230Player`, a case in each, an
> `inv_collection` row resolving `"collection_transmit"`, init and shutdown),
> and it forces the container-registry generalisation shop, GE, trading and
> death all need. **§2's persistence dependency is no longer a gate**:
> `mock230_save_player`/`_load_player` were given callers on 2026-08-02 with
> `farming_tools`. One claim to drop: §2's "this ruled out … a bitset of
> varbits" — 55 `collection_item_<name>` varbits across 12 `collection_items_N`
> varps do exist; no decompiled script reads them, so it is a parallel store to
> decide about, not something ruled out. Otherwise the most accurate doc in the
> set.

> Companion to `docs/questlist_chatmenu_levelup.md` and
> `docs/chrome_panels_server_reqs.md`, same discovery pass. **This is the
> single largest new per-player state requirement found in this whole survey
> series** — not a handful of undeclared varps, but an entire 500-slot
> container mock230 has no concept of at all.

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `collection` (detail view) | 621 | mechanism fully traced | a 500-slot per-player container (`inv_620`) never wired server-side, plus ~10 small varps |
| `collection_overview` (summary grid) | 908 | mechanism fully traced | same container dependency, plus ~15 aggregate/ring varps |

---

## 1. The mechanism

`collection.if` `[tabs]` onload (`i:2388,i:0`) → `script_2388`
(`collection_draw_tabs_all`) draws the 5 category tabs (Bosses/Raids/Clues/
Minigames/Other) and dispatches to `collection_draw_list`
(`script_2730`/`2731`), which for the selected source calls
**`~collection_draw_log`** (`script_2732.cs2`) — the item-grid renderer.
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

**And mock230 has zero support for it, confirmed directly**:
`container_for()` (`src/net/mock/mock230_scripts.c:1504-1526`) has exactly
three cases — `inv_backpack`, `inv_worn`, `inv_bank` — and falls through to
`*out_slots = 0; return NULL;` for anything else, including 620.
`container_dirty()` has the identical three-way branch: a write to inv 620
would mark nothing dirty and transmit nothing. There is no
`struct Mock230Player` field for it. The wire encoders themselves need no
change — `mock230_send_inv_full`/`_partial` already take an arbitrary
container id and slot array, and were already widened once for the bank's
1220 slots — so the entire gap is "no per-player storage exists for id 620,
and nothing ever populates it," not a wire-format limitation.

**Why this is the largest finding in the series**: 500 slots × (obj id +
count) per player, and unlike the bank it's **monotonic** — write-once,
grows forever, must persist indefinitely. That collides directly with an
already-known gap: `mock230_save_player`/`mock230_load_player` have zero
callers anywhere (`docs/PORTING_GUIDE.md` §2.5) — collection log is the
first feature in this survey series where "persistence doesn't work yet" is
load-bearing rather than a footnote, since a collection log that resets on
reconnect defeats the entire point of the feature.

## 3. Server obligations

| state | meaning | delivery | mock230 status |
|---|---|---|---|
| container **620** (`collection_transmit`, 500 slots) | the load-bearing per-item obtained+count state | generic container wire, same as bank | **not wired** — `container_for`/`container_dirty` have no case for it; no player struct field; no persistence path |
| `%collection_count`/`_max` | overview "Collections Logged: N/M" | generic varp transmit | not declared |
| `%collection_count_highscores` (Account Summary's own line) | a *different* numerator than the overview's — worth resolving as a naming inconsistency, not assuming identical | generic varp transmit | not declared |
| per-subsection counts (Bosses/Raids/Clues/Minigames/Other ×2 each) | the 5 progress rings | generic varp transmit | not declared |
| 12-slot "Latest Collections" ring (item + day-obtained ×12) | the overview's recent-items strip | generic varp transmit | not declared; a fixed rotation, not the container |
| `%varbit9535` (`current_runeday`) | "obtained N days ago" display | pre-existing systemwide clock — **not collection-log-specific**, check if landed for other reasons first | not confirmed landed |
| per-source kill-count/PB scratch varps (`%collection_category_count` family) | a **transmit mailbox** — server copies the selected source's real counter into shared scratch varps on demand, one set, not one-per-boss | generic varp transmit | not declared; and the real persistent kill-count/PB tracking underneath is a **pre-existing, larger dependency**, confirmed zero kill-count infrastructure anywhere in mock230 |
| `%varbit14577` (`collection_player_bodytype`) | gendered item-variant substitution for counting | generic varbit transmit | not declared |
| point-of-earning trigger (write the slot + message the player) | the actual "you got a new collection log item" moment | — | **corpus gap** — no script anywhere writes to `inv_620` or contains a "New item added" string; same missing-proc class as the rest of this series |

The transport for every varp/varbit row is generic and already works — each
is a small, isolated content-side declaration, same fix as `%qp` in
`docs/questlist_chatmenu_levelup.md` §1.2. **The container is not** — it's
new engine surface (a fourth `container_for` case, a player struct field,
persistence), plus a content-side trigger at the point of earning.

## 4. Landed vs. gap

- **Landed / reusable as-is**: the entire container wire path, generic varp/
  varbit transmit, client-side `inv_total`/`inv_getobj`/`inv_getnum`, both
  interfaces' chrome (needs zero server input, same "panel paints itself"
  pattern as the loot tracker and xptracker).
- **Gap, small**: ~20 varps/varbits — declare `transmit=yes`/`scope=perm`,
  compute content-side.
- **Gap, large and novel**: a fourth `container_for`/`container_dirty` case
  for `inv_620` (500 slots), a new player struct field, persistence (riding
  whatever eventually fixes `mock230_save_player`'s zero-callers problem,
  since this state is permanent by definition), and — per
  `docs/PORTING_GUIDE.md` §2's "port the proc, not the field" — a
  content-side trigger fired at the moment a loggable item is received,
  whose body is the missing corpus piece above.
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
