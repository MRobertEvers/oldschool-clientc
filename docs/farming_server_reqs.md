# Farming (`farming_tools` 125, `farming_tools_side` 126, `farming_view` 179): what the server owes

> Companion to `docs/questlist_chatmenu_levelup.md` and
> `docs/shop_server_reqs.md`, same discovery pass. **Headline correction to
> the working hypothesis going in**: `farming_tools`/`farming_tools_side`
> are NOT shopmain/shopside despite the "Amazing Farming Equipment Store"
> title string — they're the Tool Leprechaun's varbit-backed tool storage,
> structurally closer to a second `%qp`-shaped accumulator than a live
> container. `farming_view` is confirmed exactly the hypothesized
> "static location register × per-player dynamic state" shape, with 107
> confirmed patch locations across the whole map.

## 0. Status at a glance

| interface | id | mechanism | mock230 status |
|---|---|---|---|
| `farming_tools` | 125 | Tool Leprechaun storage — 12 tool/compost slots, counts packed into varbits, not a container | zero |
| `farming_tools_side` | 126 | mirrors what's carried in the player's own backpack | container mechanism already landed; no farming-specific wiring |
| `farming_view` | 179 | 107-patch status grid — static location register × per-player dynamic state | zero, and the per-patch data walker itself is a corpus gap |

---

## 1. `farming_tools`/`farming_tools_side` — not a shop

### 1.1 The smoking gun

`farming_tools_main_init`'s signature is `(int $int0..$int12, component
$component13)` — **every argument is an int, none is `inv`** (confirmed).
Compare shopmain: `shop_main_init(inv $inv0, int $int1, int $int2, int
$int3, string $string0)` (`docs/shop_server_reqs.md` §1) — a real
container reference as the first argument. There is no container reference
anywhere in this call graph. **Stored** tool/compost counts come entirely
from **varbits** (`farming_tools_getstored`, one `switch` case per tool);
**carried** counts come from `inv_total(inv, obj)` against the player's own
backpack — the same mechanism the bank's own carried-side idiom uses.

### 1.2 The varbits are cleanly named, with one real collision

~14 storage-count varps/varbits, all confirmed cleanly named
(`farming_tools_rake` 1435, `_extrarakes` 8357, `_dibber` 1436, `_spade`
1437, `_secateurs` 1438, `_trowel` 1440, `_wateringcan` 1439, `_plantcure`
6268, `_bottomless_bucket_type/_quantity` 7915/7916, `_buckets` 1441 +
extras, `_compost`/`_supercompost`/`_ultracompost` 1442/1443/5732 + extras)
— no collision among these themselves, unlike most findings in this series.

**One real collision, confirmed**: the shared 1/5/10/All/X quantity-mode
selector reads `%varbit7792` (confirmed `farming_tools_selectedquantity`)
via `get_selected_quantity`, whose switch has **`case 2193, 342:`** — group
342 is the **Hallowed Sepulchre** relic-hunter tool storage, confirmed to
use the identical `_main_create`/`_side_create`/`_getstored`/`_getcarried`
template and share this same varbit. Any read-modify-write on `%varbit7792`
for farming must not clobber Hallowed Sepulchre's last-selected quantity
mode — same collision class as shop's `bank_closing` and slayer's `if1-6`.

### 1.3 The title string is cosmetic, not evidence

`~steelborder($int0, "Amazing Farming Equipment Store", 0)` is the same
**generic** title-bar setter dozens of unrelated interfaces use, including
shopmain itself — not evidence this is shop machinery. Corroborating this
is the Tool Leprechaun: `docs/LOSTCITY_PORT_TRIAGE.md` independently names
the npc `farming_tools_leprechaun`, and the npc pack confirms three
variants exist. The "Deposit inventory" button reuses `bankmain_depositall`
— the bank's own deposit-all clientscript — reinforcing this is a
bank-adjacent storage widget, not a shop.

### 1.4 `farming_tools_side` — already-landed mechanism

Wires `if_setoninvtransmit{inv}` — the identical auto-repaint idiom bank's
own side panel uses. This container (`inv`, id 93, the backpack) is one of
the three cases `container_for()` already implements
(`src/net/mock/mock230_scripts.c:1505-1526`) — **zero new engine work**
needed for the side panel.

### 1.5 Server obligations

| what | mock230 status |
|---|---|
| ~14 storage-count varps/varbits | **not declared** — same small, isolated fix-shape as `%qp` |
| Store/withdraw transaction handler | **not implemented** — op text is dynamically set but no click handler body exists in this corpus, same gap class as shop's buy-op |
| Shared quantity-mode varbit (also used by Hallowed Sepulchre) | **not declared**; must not clobber the other feature's use |
| Watering-can charge-object selection | **not declared** |
| Deposit-all reuse of bank's clientscript | infrastructure exists, unused for farming |
| `farming_tools_side`'s carried-count repaint | **landed mechanically** — rides the already-working backpack container path |

---

## 2. `farming_view` (179) — the 107-patch status grid

### 2.1 Confirmed: a real 107-row static location register

`enum_1233`(name)/`1234`(type-label)/`1235`/`1236`/`1237`(component-triple
mapping) all key off the same 107 numeric patch ids, spanning the whole
map — Taverley/Falador/Varrock/Lumbridge through Kastori/Auburnvale/The
Great Conch/Civitas illa Fortis, every patch type (allotment, herb, hops,
bush, tree, fruit tree, spirit tree, mushroom, calquat, cactus, belladonna,
grapevine ×12 Vinery slots, seaweed, hardwood, hespori, celastrus, anima,
redwood, crystal tree, coral nursery). This is content data, already in the
cache — **no server work needed for the register itself**. Implemented as
CS2 enums, not a formal dbtable (confirmed: no `farming_patch`/`patchN`
dbtable exists; the one "patch"-matching dbtable hit is an unrelated
proper-noun, `patchy_data`, a scarecrow-combining table).

### 2.2 The critical finding — per-player dynamic state, and the caller is missing entirely

`farming_view_setpanel(int $int0, obj $obj1, coord $coord2, int $flags3,
string $string0)` is **the per-patch render** — it decodes growth stage
(`coordx($coord2) % 64`), minimum yield (`coordx($coord2) / 64`), watered
flag (`coordy($coord2) = 1`), and a 10-bit flag word (diseased/dead/
gardener-protected/flower-protected/composted at 3 tiers/Hosidius-
community-protected/permanently-undead-immune).

**Unlike collection log (a real container) or shop (a real `inv` argument),
these are just plain scalar arguments to a clientscript call — there is no
wire opcode, container, or dbtable read visible anywhere.** And critically,
**the caller that supplies these 107 tuples is not present in this
corpus at all** — confirmed, `grep -rl "farming_view_setpanel"` returns
only the render proc itself and a "Loading..." placeholder shown before
real data arrives. Same corpus-gap class as `~questlist_draw` and the
collection log's point-of-earning trigger — re-decompile the live cache
before implementing; the caller's script id is unknown from this corpus.

The "Loading..." placeholder is itself a signal: this screen visibly
populates progressively, patch by patch, consistent with the server
needing to compute/fetch 107 patches' worth of state per open.

### 2.3 Why this isn't simply "collection log again"

It's not one big write-once container; it's **up to 107 small, mutable,
per-player records** (planted crop + a growth clock + a flag word) that
must be kept live and tick-updated for the grid to mean anything — closer
in kind to slayer's "6 repurposed scratch varps," but ×107 and with an
actual clock behind it, than to collection log's monotonic container. The
magnitude per-slot is smaller, but the *mechanism* — a live, per-patch,
tick-driven simulation — is a wholly new gameplay system this UI is just a
window onto.

### 2.4 Server obligations

| state | delivery | mock230 status |
|---|---|---|
| 107-row static location register | client cache, already generic | **landed** (cache content) |
| Per-player, per-patch dynamic state (planted crop, growth clock, flags) × 107 | unknown wire shape — corpus gap on the caller | **entirely absent** — not a small varp gap, a structurally new per-player state surface |
| Selected tab (`%varbit4776`) | varbit transmit | not declared (trivial) |
| The underlying crop-growth/disease/compost simulation this UI displays | — | **does not exist at all**, a much larger pre-existing dependency |

---

## 3. Landed vs. gap in mock230

`grep -rniE "farming|\bpatch\b" src/net/mock/ src/game/` — exactly two
hits, both confirmed false positives (an unrelated code comment about
network routing using "patch" as an ordinary word). **Zero implementation
of anything farming-related, and — checked specifically as its own
question — zero tick-based crop-growth mechanic of any kind exists**,
independent of this UI. No growth-stage clock, no disease-roll, no
compost-decay, nothing in the engine models a planted crop advancing over
time. `container_for()` still has only its three original cases.

## 4. LostCity precedent — confirmed absent

`ls LostCity_Server/content/scripts | grep -i "^skill_"` — no
`skill_farming` directory, confirmed directly, same check as the Slayer
finding. Two other "farming"-matching files were checked individually
rather than assumed to be false positives: a message-id reservation file
explicitly marked `// all bellow are guessed` from a modern RS3 structs
reference (documentation, not implemented content — nothing ever raises
these ids), and a comment on an unrelated monster's drop-table script
noting real-game history ("Entire drop table structure changed on Farming
release") — a comment about the future, not farming content in this tree.

**Farming launched 16 February 2005, roughly five months after LostCity's
September 2004 snapshot** — confirmed absent, not a stale-reference gap,
same class as Slayer rather than questlist/levelup. Fully greenfield on
both engine and content sides — nearer in shape to clan chat/collection log
than to anything with a 2004 reference to adapt.

## 5. What this doc does not cover

- `farming_view_setpanel`'s real caller — genuinely absent from this
  decompile; re-decompile before implementing, its script id is unknown.
- The store/withdraw click handler for `farming_tools`/`_side` — op text
  is dynamic but no handler body exists in this corpus.
- Any NPC/object trigger that opens these interfaces — zero hits in
  `server/scripts/` for any farming identifier; the access point is
  unconfirmed.
- The actual crop-growth/disease/compost-decay simulation — out of scope
  for an interface-discovery pass, flagged as the larger dependency.
- Full semantics of the bit-packing formulas in `farming_tools_getstored`
  — reported verbatim, not independently re-derived.
