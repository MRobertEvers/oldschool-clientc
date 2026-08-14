# Shops — catalogue, scrape, and implementation plan

> **UPDATE 2026-08-13, later the same day: phase 1 and the first slice of
> phase 2 are live.** §3.1–3.5's engine work all landed — `.inv` server
> parsing, `scope=shared` world-container classification, the two missing
> opcodes (`INV_STOCKBASE`/`INV_ALLSTOCK`), per-player world-container
> listener fan-out (a real gap the original write-up under-scoped — see the
> new note at the end of §3.1), boot seeding, and the restock tick. 21 shops
> are authored and live — one `.rs2` + one `.inv` each, `tools/gen_shop_scripts.py`
> generated from the reviewed catalogue — and `--selftest` loads the content
> tree with zero errors. See §8 for the exact list and what is still open.

> Written 2026-08-13. Companion to [`shop_server_reqs.md`](shop_server_reqs.md),
> which surveyed what `shopmain`/`shopside` (300/301) need from the server.
> That survey is still correct about the interface; **§0 below re-measures its
> blocker list, four of which have since cleared.** This document adds the two
> things it did not have: the actual shop data, and a build order.

The goal: **every shop in the game opens, sells at the right price, restocks,
and is authored as one `.rs2` file per shop citing the wiki revision it came
from.**

---

## 0. Status, re-measured 2026-08-13

`shop_server_reqs.md` (2026-08-02) lists seven blockers. Re-running each check
against today's tree:

| blocker as stated | today |
|---|---|
| `SS_OP_OC_COST` (4202) declared-and-uncovered | **cleared** — `mock230_ops_obj.c:287` |
| `SS_OP_RUNCLIENTSCRIPTVARARG` for `shop_main_init`'s 4 ints + string | **cleared** — `mock230_scripts.c:6665` |
| `container_for` resolves only off `active_player`, three hardcoded cases | **cleared** — `mock230_container_resolve(srv, player, inv_id)` is a registry over all 1,026 cache invs |
| no `fields/inv.ini`, no `[namespace:inv]` in `content.ini` | **cleared** — both exist; `fields/inv.ini` declares `inv.size` and explicitly reserves stock/scope for a server-side slice |
| `SS_OP_INV_STOCKBASE` (4325) | **still missing** |
| `SS_OP_INV_ALLSTOCK` (4303) | **still missing** |
| world-scope persistence / boot re-seed | **still missing** — `mock230_container_scope()` returns `MOCK230_CONTAINER_PLAYER` unconditionally; the WORLD table exists and is empty by construction |

Two further gaps the survey did not name, both found by this pass:

* **No `.inv` config parser server-side.** `mock230_content.c:3607-3630` walks
  `.param .constant .enum .varp .npc .obj .loc .spawn`. There is no `.inv`, so
  `stockN=`/`restock=`/`allstock=`/`scope=` have a grammar (LostCity's) and a
  declared home (`fields/inv.ini` says where they must *not* go) but no reader.
* **No restock tick.** LostCity does this in `World.ts`, not content — see §3.4
  for the exact rule, which is short.

Also worth recording because it contradicts an assumption that would otherwise
be made: `SS_OP_OC_TRADEABLE` (4211), `SS_OP_NPC_PARAM` (2530),
`SS_OP_INV_MOVEITEM_UNCERT` (4320), `SS_OP_INV_ITEMSPACE`/`_2` (4316/4317),
`SS_OP_INV_TRANSMIT`/`_STOPTRANSMIT` (4331/4326) and `SS_OP_IF_OPENMAIN_SIDE`
(2035) are **all covered today**. The engine surface a shop needs is two
opcodes short, not eleven.

---

## 1. What has been catalogued and recorded

Three new tools, all following `wiki_fetch.py`'s conventions (1 req/sec,
contact address in the User-Agent, manifest recording revid + fetch date so a
re-crawl is a reviewable diff):

    tools/wiki_shop_fetch.py --fetch      # 1,062 shop-bearing pages
    tools/gen_shop_catalog.py --write     # behaviour + stock, per table
    tools/wiki_shop_owners.py --fetch --write   # 435 owner npc pages, then the id join
    tools/gen_shop_inv_map.py --write     # the one join that cannot be computed

### 1.1 The roster: which wiki pages are shops

Not `Category:Shops` (506, hand-maintained, misses npc-hosted stores). The
roster is the **union of two template transclusions**:

| source | pages | what it catches |
|---|---|---|
| `Template:Infobox Shop` | 454 | pages whose subject is a shop |
| `Template:StoreTableHead` | 1,056 | pages carrying a stock table |
| union | **1,062** | both, and neither alone is enough |

608 pages carry a table but no shop infobox — a store hosted on an npc's page
(`Duradel`, `Thurgo`, `Dunstan`), a minigame reward counter, a quest hub. Those
are shops this server has to open. 6 carry an infobox but no table.

### 1.2 What was written

    OSRS-Content/osrs239-content/wiki/
      shops/*.wikitext          1,062 raw pages
      shops_manifest.tsv        title, revid, infobox?, table?, fetch_date
      owners/*.wikitext           435 owner npc pages
      owners_manifest.tsv       title, resolved_title, revid, fetch_date
      shop_catalog.csv          1,229 rows — one per stock table (behaviour)
      shop_stock.csv           12,110 rows — one per stocked obj (stock)
      shop_owners.csv           1,742 rows — one per (shop, owner npc id)
      shop_inv_map.tsv            the shop -> cache inv worksheet

`shop_catalog.csv` is the **behaviour**: the three price multipliers, owner,
location, members flag, currency, and `page_title`/`revid`/`fetch_date` so a
generated `.rs2` can cite the exact revision it was written from.

`shop_stock.csv` is the **stock**: `(shop_key, slot, obj, stock, restock)`.

### 1.3 What the numbers say

```
1,229 stock tables across 1,062 pages
  620 are the same shop catalogued twice        ->   609 distinct shops
                                                     593 of those carry stock
  865 carry all three price multipliers
1,152 name an owner npc (by link or by stated id)
12,110 stock lines; 11,835 (97.7%) resolve to a cache obj id
```

Narrowing to distinct shops, the funnel that sets the build order:

| gate | shops |
|---|---|
| distinct, with stock | 593 |
| …and all three coin multipliers (a coin shop, not a token shop) | 387 |
| …and at least one owner npc spawned in this world | **265** |
| …and every stock line resolved with no review flag | 96 |
| …and a cache-inv binding already verified upstream | 36 |

88 distinct shops trade in a currency that is not coins (Castle Wars tickets,
Pest Control points, Barbarian Assault); 396 are members-only. 5,944 stock lines
live in the 593 distinct shops.

---

## 2. The data model — why the wiki maps cleanly onto the engine

This is the load-bearing derivation, and it is not an analogy: the wiki's shop
template fields and LostCity's shop config grammar are **the same numbers in the
same units**, verified against a shop both sources describe.

Lumbridge General Store, wiki (rev 15231736, fetched 2026-08-13):

```
{{StoreTableHead|sellmultiplier=1300|buymultiplier=400|delta=30}}
{{StoreLine|name=Shears|stock=2|restock=100}}
```

Lumbridge General Store, LostCity `area_lumbridge/configs/lumbridge.npc`:

```
param=shop_sell_multiplier,1300
param=shop_buy_multiplier,400
param=shop_delta,30
```

…and its `lumbridge.inv`: `stock3=shears,2,100`. Identical, field for field.
The correspondence holds across the corpus, so:

| wiki | engine | meaning |
|---|---|---|
| `StoreTableHead\|sellmultiplier` | `%shop_sell` | price the **player pays**, per mille of `oc_cost` |
| `StoreTableHead\|buymultiplier` | `%shop_buy` | price the **shop pays**, per mille of `oc_cost` |
| `StoreTableHead\|delta` | `%shop_haggle` | per-mille price movement per unit away from baseline stock |
| `StoreLine\|name` | `stockN`'s obj | resolved to a gameval, §2.1 |
| `StoreLine\|stock` | `stockN`'s count | baseline stock, and the starting count |
| `StoreLine\|restock` | `stockN`'s rate | ticks between ±1 nudges toward baseline |
| `Infobox Shop\|owner` | the `[opnpc<n>,…]` gameval | §2.2 |

The price formula is LostCity's `[proc,calc_shop_value]`, unchanged:

```
value = scale(max(100, multiplier - clamp(-5000, 1000, delta_from_base * haggle)),
              1000, oc_cost(obj))
```

with a floor of 1 on the buy side (a shop never sells for 0) and no floor on the
sell side (a general store paying 0 for a pot is correct).

### 2.1 Item name → obj id

By display name against `dump_stats --obj-csv`, dropping noted and placeholder
records (they duplicate every name they alias). Three rules, each recorded in
`shop_stock.csv:match_rule`:

| rule | lines | trust |
|---|---|---|
| `exact` | 11,355 | authoritative |
| `displayname` | 84 | authoritative — `name=` was a page title, `displayname=` the in-game name |
| `strip-qualifier` | 396 | **provisional** |

`strip-qualifier` handles `Shantay pass (item)` → `Shantay pass`, and it is
never authoritative because the cache gives every colour of a garment the same
display name: `Skirt` is `dwarf_skirt1..4`, so `Skirt (blue)` and
`Skirt (lilac)` both land on the lowest id and one of them is wrong. Those rows
carry `review=variant` and must not reach a `.inv` unlooked-at.

A further 2,542 lines match exactly but onto more than one real record (an old
id and its modern replacement — `Knife` is 946 and also 5605 and 28413). Lowest
id is right in every case spot-checked, but they carry `review=ambiguous`
rather than being silently accepted.

275 lines (112 distinct names) resolve to nothing, and correctly so: they are
not items. `Barbarian Assault#Level Upgrades` is a section link; the Leagues
Relic Hunter tiers are purchases, not objs.

### 2.2 Owner → npc id → gameval

The trigger this server needs is `[opnpc<n>,<gameval>]`, so the join has to
reach a *gameval*, and npc display names are not unique (`Shop keeper` names 14
records). The id is the pivot, from two sources:

* the shop page **is** an npc page and states `{{Infobox NPC|id=}}` — 644 tables;
* the shop names its owner as a wikilink — 435 distinct owner pages, crawled
  into `wiki/owners/`, id read from their own infobox.

1,720 of 1,742 shop/owner rows carry an id; 1,716 of those resolve to a gameval
via `configs/all.npc.compack`. **933 of those gamevals are spawned in this
world** (same test `wiki_npc_roster.py` uses: first column of an `==== NPC ====`
row in any `*.spawn`), covering 844 tables.

Spot-check: `Lumbridge General Store` → `Shop keeper (Lumbridge)` id 2813 →
`generalshopkeeper1`, and `Shop assistant (Lumbridge)` id 2814 →
`generalassistant1`. Both spawned. Both already have stubbed `[opnpc1]` handlers
in `areas/lumbridge/scripts/tutors.rs2`.

### 2.3 Shop → cache inv: the join that cannot be computed

The osrs239 cache already names its inventories, and ~556 of the 1,028 are
storefronts (`configs/all.inv.compack`: `axeshop`=1, `generalshop1`=3,
`swordshop`=6, …). **Which one backs which wiki shop is a naming judgement, not
a computation** — `generalshop1` is Lumbridge and `generalshop3` is Al-Kharid,
and nothing in either name says so.

`wiki/shop_inv_map.tsv` is therefore a *worksheet*, seeded in three tiers:

| tier | rows | what it is |
|---|---|---|
| `lostcity` / `verified` | 262 | LostCity already binds this npc to this inv via `param=owned_shop`; the 2004 *stock* is discarded but the npc↔inv identity does not change between eras |
| `namematch` / `proposed` | 305 | ≥2 shared tokens between inv gameval and shop/owner gameval. A suggestion |
| unbound | 661 | needs a decision: reuse a cache inv, or allocate one in `pack/inv.alloc` |

Plus 322 shop-shaped invs nothing claimed — each is either dead content, a shop
this crawl missed, or an account-mode variant the classifier let through.
`--refresh` is the only way to overwrite a row a human marked `verified`.

**Reuse the cache name when one exists.** `pack/inv.alloc` refuses a name
already bound in `configs/all.inv.compack`, and one namespace per data type is
the tree's rule ([`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md)).

### 2.4 Slot counts are a cache fact, and they are not all 40

`configs/all.inv` sizes each shop individually — `axeshop` is 7, `scimitarshop`
is 4, `runeshop` is 14, `generalshop1` is 40. The size is not authored (§3.1);
it is decoded at boot for every inv id, and a shop cannot hold more baseline
stock than the cache gives it slots.

Checked across all 262 verified bindings: **260 fit, 2 overflow.**
`mcannonshop` is sized 6 and the wiki lists 7 lines; `aprilfoolshorseshop` is
sized 31 against a 227-line table that is Diango's holiday-item index rather
than a storefront. So the constraint is real but rare — and it is a hard error
the generator must raise, not truncate, because a silently dropped `stock7=` is
an item that simply never appears in game.

---

## 3. Engine work

Five slices. Each is independently testable and lands in this order because
each is the previous one's precondition.

### 3.1 `.inv` server config parser + `scope` classification

*Files:* `OSRS-Content/osrs239-content/fields/inv.ini`,
`src/net/mock/mock230_content.c`, `src/net/mock/mock230_container.c`

Add the server half of the inv field register — the file already says the shape
of this and refuses to pretend otherwise:

```ini
[inv.scope]      scope = server     ; shared | temp; default temp (player-owned)
[inv.restock]    scope = server
[inv.allstock]   scope = server
[inv.stackall]   scope = server
[inv.stock]      scope = server     ; stockN = obj,count,rate
```

`size` stays `scope = client, client = native`: the slot count is a cache fact
decoded at boot for every inv id, and an authored `size=` should warn the way
an authored npc `name=` does — restating a field the client's own record
carries. (LostCity's `.inv` files state `size=40`; ours must not, and the
import must drop it rather than fight it.)

Then `walk_configs(path, ".inv", load_inv_config)` alongside the other eight,
and `mock230_container_scope()` stops being a constant: it returns
`MOCK230_CONTAINER_WORLD` for an inv the tree declared `scope=shared`. The
registry's world table already exists and is already keyed correctly — this
fills it.

**Trap, from `mock230_container.h`:** `mock230_container_adopt` allows a
per-slot dirty mask only up to 32 slots. Shop invs are sized 40 in the cache.
World containers must take the whole-container `dirty_ref` path, not the mask.

**Trap, from [[uitree-hook-slot-right-sizing]]:** a world container's transmit
has to fan out to *every* player watching it, not just the one who bought. A
per-player listener list on the container row, not a single `active_player`.

> **Landed 2026-08-13, and this needed more than a listener list.** The
> existing listener struct (`mock230.h`) tracked only a `component` id, which
> is not unique across players on a shared row — `shopmain:items` is the same
> numeric id for every client, so two players opening the same shop collapsed
> onto one listener. Fixed by adding a `player` field to each listener
> (`NULL`/unused on a per-player row, meaningful on a world row) and changing
> every match/bind/unbind to key on `(component, player)` when the row is
> `MOCK230_CONTAINER_WORLD`. `mock230_container_unbind` gained an `srv`
> parameter for the same reason: a component id alone cannot say whether the
> row behind it is the caller's own or shared, so unbinding needs the world
> table too. A new `mock230_container_flush_world(srv)` is the shared row's
> sibling to the existing per-player `mock230_container_flush` — called once
> per tick from `phase_clients_out`, before the per-player pass, so a shop
> transaction reaches every listener's outgoing batch before that player's own
> tick-end closes it. `MOCK230_WORLD_CONTAINER_MAX` went 16→640 (shared rows
> are created on first use and never evicted, so this has to cover every
> distinct shop any player visits in one server lifetime) and
> `MOCK230_CONTAINER_LISTENERS_MAX` went 4→16 — both cheap: with
  `MOCK230_PLAYER_MAX` at 8, the whole increase costs well under a MB. See
  `mock230_container.c`/`.h` and `mock230.h`'s listener struct comment.

### 3.2 The two missing opcodes

*File:* `src/net/mock/mock230_ops_inv.c` (the `host commands (inv)` layer)

| opcode | signature | body |
|---|---|---|
| `SS_OP_INV_STOCKBASE` (4325) | `(inv, obj) -> int` | baseline count for that obj from the parsed `stockN`, or `-1` if not a baseline item |
| `SS_OP_INV_ALLSTOCK` (4303) | `(inv) -> boolean` | the parsed `allstock=` flag |

Both are pure reads off §3.1's parsed definition. `make -C src test-mock230-coverage`
fails until `mock230_opcode_coverage.gen.h` is regenerated
(`python3 net/mock/gen_opcode_coverage.py`).

`SS_OP_WEALTH_EVENT` (2137) is also uncovered and LostCity's shop calls it
twice. It is an audit log, not a mechanic — stub or omit, and say which.

### 3.3 World-container persistence and boot seed

*Files:* `src/net/mock/mock230_save.c`, `mock230_boot.c`

`mock230_save.c` is player-only. A shop's stock is world state that must
survive a restart, or every reboot hands players a fully-stocked Aubury's.

Cheapest correct answer, and the one LostCity uses: **do not persist it.**
Seed every `scope=shared` inv from its `stockN` baselines at boot and let the
restock tick converge. That is authentic (2004 shops reset on world restart) and
removes the whole durable-world-state slice from the critical path. If durable
stock is wanted later it is a new file, not a change to this one.

Whichever is chosen, **state it in the boot order comment** — `mock230_boot.c`'s
ordering is a function, not a convention.

### 3.4 The restock tick

*File:* `src/net/mock/mock230_world.c`, in the per-tick cleanup phase

LostCity's `World.ts:1157-1193`, ported literally:

```
for each shared inv with restock=yes:
    for each occupied slot:
        if count < baseline and tick % rate == 0:  count += 1
        if count > baseline and tick % rate == 0:  count -= 1
        if allstock and slot has no baseline and tick % 100 == 0:  count -= 1
```

`rate` is per-slot (`stockN`'s third field); the 100-tick constant is
`World.INV_STOCKRATE`, one minute, and applies only to the non-baseline decay
that general stores need. Mark the container dirty on any change — the client
re-runs `shop_main_update` off `if_setoninvtransmit` with no server round-trip.

### 3.5 The open path and the quantity-mode varbit

*Files:* `server/scripts/shop/scripts/shop.rs2` (new), `shop.varp`, `shop.constant`

Port LostCity's `shop/scripts/shop.rs2` — `~openshop`, `buy_item`, `sell_item`,
`can_sell_obj`, `adjusted_item_cost_buying`/`_selling`, `price_mod`,
`calc_shop_value` — onto rev-230's interface. Four differences from the
reference, and they are the whole port:

1. **`if_openmain_side(shopmain, shopside)`**, then push the init:
   `runclientscript*(^clientscript_shop_main_init)($inv, $size, $slots, $buy50, $title)`.
   `shopmain.if` carries **zero `onload=`** — it is the only panel in
   `shop_server_reqs.md`'s survey that cannot draw itself.
2. **There is no `shopmain:inv` component.** Item ops name `shopmain:items`
   (component 16) plus `last_slot`; `shopside:items` is component 0. Same shape
   as bank ([[rev230-ui-notes]], and `bank.rs2`'s own header comment §2).
3. **Op 1's meaning is client state.** `%varbit6348` (`shop_quantity`) selects
   Value / Buy-1 / 5 / 10 / 50; op 1 shows the selected mode and op 6 shows the
   demoted twin. The server must mirror the varbit to interpret a click on
   op 1 or op 6 — the op index alone is ambiguous. Ops 2/3/4/5 are fixed
   Buy-1/5/10/50, op 9 is a remembered custom quantity, op 10 is Examine.
4. **`shop_quantity` packs into varp 1022, named `bank_closing`.** A naive
   read-modify-write on that varp for shop purposes clobbers bank's own bit
   ranges in the same varp. Same trap class as [[varp-two-writers-side-effects]].

Two things `shop_server_reqs.md` flags as corpus gaps and this plan does not
resolve: **shopside's own population script is not in the decompile corpus**
(§2 there), so its sell-op indices are inferred from `bankside_init`, not known;
and the custom **Buy-X round trip** (op 9) is inferred to use `P_COUNTDIALOG`
because bank does. Re-decompile `cache.osrs239` with `3rd/rscache/tools/cs2`
before writing either — [[worldmap-open-click-session]]: decompile before
guessing ids.

---

## 4. Content shape — one `.rs2` per shop

### 4.1 Layout

```
server/scripts/shop/
  scripts/shop.rs2                     the engine-facing procs (§3.5), one copy
  configs/shop.varp  shop.constant     session state, clientscript ids
  <area>/
    scripts/<shop_key>.rs2             ONE PER SHOP — triggers, dialogue, open
    configs/<shop_key>.inv             ONE PER SHOP — stock
```

`<area>` groups by the wiki's `location=` so the tree stays navigable at 593
files; it carries no semantics.

Multipliers and title are passed **inline to `~openshop`** rather than through
npc params. LostCity offers both (`~openshop` and `~openshop_activenpc`), and
the explicit form is right here: it keeps a shop's whole behaviour readable in
its own file, and avoids a third generated file per shop plus 4 param
allocations × 593 shops. `owned_shop` and friends stay unallocated.

### 4.2 The header — required by this task

Every generated `.rs2` and `.inv` opens with the citation:

```
// Lumbridge General Store
// https://oldschool.runescape.wiki/w/Lumbridge_General_Store
// Wiki revision 15231736, scraped 2026-08-13 by tools/wiki_shop_fetch.py.
// Regenerate: tools/gen_shop_scripts.py --shop lumbridge_general_store
```

`revid` and `fetch_date` come from `shop_catalog.csv`, so the comment cannot
drift from the data: a re-crawl that changes a price changes the header in the
same commit.

### 4.3 Worked example — real data, generated from the CSVs

`shop/misthalin/configs/lumbridge_general_store.inv`:

```
[generalshop1]
// Lumbridge General Store
// https://oldschool.runescape.wiki/w/Lumbridge_General_Store
// Wiki revision 15231736, scraped 2026-08-13.
scope=shared
restock=yes
stackall=yes
allstock=yes
stock1=pot_empty,5,10
stock2=jug_empty,2,100
stock3=pack_jug_empty,5,20
stock4=shears,2,100
stock5=knife,5,100
stock6=bucket_empty,3,10
stock7=pack_bucket,15,10
stock8=bowl_empty,2,50
stock9=cake_tin,2,50
stock10=tinderbox,2,100
stock11=chisel,2,100
stock12=spade,5,100
stock13=hammer,5,100
stock14=newcomer_map,5,100
stock15=sos_security_book,5,100
```

`shop/misthalin/scripts/lumbridge_general_store.rs2`:

```
// Lumbridge General Store
// https://oldschool.runescape.wiki/w/Lumbridge_General_Store
// Wiki revision 15231736, scraped 2026-08-13 by tools/wiki_shop_fetch.py.
// Owner: Shop keeper (2813) / Shop assistant (2814). F2P. General store.

[opnpc1,generalshopkeeper1] @lumbridge_general_store_talk;
[opnpc1,generalassistant1]  @lumbridge_general_store_talk;
[opnpc3,generalshopkeeper1] @lumbridge_general_store_open;
[opnpc3,generalassistant1]  @lumbridge_general_store_open;

[label,lumbridge_general_store_talk]
~chatnpc_anim(^chat_happy, "Can I help you at all?");
def_int $option = ~p_choice2("Yes please. What are you selling?", 1, "No thanks.", 2);
if ($option = 1) {
    ~chatplayer_anim(^chat_quiz, "Yes please. What are you selling?");
    ~chatnpc_anim(^chat_happy, "Take a look.");
    @lumbridge_general_store_open;
} else if ($option = 2) {
    ~chatplayer_anim(^chat_neutral, "No thanks.");
}

[label,lumbridge_general_store_open]
~openshop(generalshop1, 400, 1300, 30, "Lumbridge General Store");
```

Note this **replaces** the stub in `areas/lumbridge/scripts/tutors.rs2`
(`"I've nothing to trade just now."`). 87 files in the tree carry a
"nothing to trade" stub; each one a generated shop lands on is a stub to
delete, and the generator must report collisions rather than produce a
duplicate trigger binding — a duplicate `[opnpc1,x]` is a compile error, which
is the good failure, but the *silent* case is a stub left behind in a file the
generator never opened.

### 4.4 The generator

`tools/gen_shop_scripts.py` reads the four catalogue files plus the reviewed
`shop_inv_map.tsv` and emits the pair above. Rules:

* **Refuse a stock list longer than the cache's slot count** (§2.4). Truncating
  loses an item silently; erroring names the shop that needs a decision.
* **Skip, loudly, any shop with an unreviewed flag.** A `review=variant` or
  `review=ambiguous` line, an unbound `cache_inv`, a `proposed` confidence, or
  an owner with no spawned gameval — none of those may produce a file. Print
  the count and the reason, per [[verify-blocker-and-failing-test]]'s sibling
  rule: silent truncation reads as coverage.
* **Never overwrite hand-edited content.** Same contract as
  [[exporter-owns-generated-configs]]: the generator owns the generated block;
  hand edits live in a separate file or a marked section.
* **Emit the dialogue by shop type.** `Infobox Shop|special=` gives
  `General store` / `Weapon shop` / … and the greeting differs per family; the
  neutral variant (`khazard_shopkeeper`, `dwarven_shopkeeper`) is the exception
  LostCity's `generalshop.rs2` already documents.

---

## 5. Build order

Each phase ends with something playable, which is the point of the ordering.

| phase | scope | gate |
|---|---|---|
| **1. One shop end to end** | Lumbridge General Store only | §3.1–3.5 all land; buy, sell, price movement, restock and reopen all work in-game |
| **2. The verified set** | 36 shops: coin-priced, owner spawned, stock clean, inv binding already verified | generator runs, no hand edits |
| **3. Review the stock flags** | +60 shops (to 96) | resolve `review=variant` / `ambiguous` rows in `shop_stock.csv` |
| **4. Review the inv bindings** | +169 shops (to 265 — every coin shop with a reachable owner) | work `shop_inv_map.tsv` down: 305 proposed to confirm, then the unbound |
| **5. Unreachable owners** | +122 (to 387 coin shops) | needs the owner npc spawned; that is a `gen_spawns.py` question, not a shop question |
| **6. Token shops** | +88 | a different currency and often a different interface — treat as its own feature, not a shop variant |

Phase 1 is the only one with engine work in it. Phases 2–5 are content and
review, and are parallelisable across people.

---

## 6. Verification

* **Unit** — `calc_shop_value` against known wiki prices. Lumbridge General
  Store sells a pot for 1gp and buys it for 0gp at baseline stock; those are
  assertions, not observations.
* **Opcode coverage** — `make -C src test-mock230-coverage` after regenerating
  `mock230_opcode_coverage.gen.h`.
* **Selftest** — a `shop` stanza in `mock230-selftest`: open, buy, check coins
  and stock, sell, check the price moved, tick past `restock`, check it
  converged. Run with **no env set** ([[mock230-selftest-two-lanes]] — the
  `osrs239` flavour carries ~205 pre-existing failures and a stanza's placement
  perturbs sections 1,000 lines away, [[selftest-stanza-placement]]).
* **In-game** — the client is the only place the CS2 gaps in §3.5 show up.
  `shopmain` cannot draw itself, so a wrong `shop_main_init` argument is a
  blank panel, not an error.
* **Re-crawl** — `tools/wiki_shop_fetch.py --refetch` then
  `git diff wiki/shop_catalog.csv` is the price-drift review. It is a no-op
  against the network for anything already in the manifest.

---

## 7. What this plan does not cover

* **Shopside's real op indices and the Buy-X round trip** — corpus gaps
  inherited from `shop_server_reqs.md` §2/§3. Re-decompile before writing them.
* **The `note_button` Leagues gate** (`~script2288`) — reads like reused ids
  rather than a purpose-built gate; `shop_server_reqs.md` §1.2 says re-verify
  before treating it as load-bearing, and this plan does not.
* **Token/point shops** (88) — `omnishop_shop_data`/`omnishop_stock_data`
  dbtables and a separate interface family. Related by name only.
* **Account-mode variants** — 191 invs are `_gim`/`_uim`/`_im`/`_leagues`/
  `deadman_` copies of a base shop. One mode, one inv; the rest are out of scope.
* **Buying back sold items at the price the shop paid** — OSRS tracks a
  per-shop sold-item history that neither LostCity nor this plan models.
* **Whether the 2,542 `ambiguous` obj rows are all lowest-id** — spot-checked,
  not proven. They are flagged so the generator refuses them, which is the
  point; the proof is phase 3's work.

---

## 8. Status, 2026-08-13

Phase 1 (engine) is done. **70 shops are live** — every shop the pipeline can
currently clear without a human decision. Two improvements past the original
write-up widened that from the initial 21/36:

* **The obj resolver now prefers a tradeable id over a lower one**
  (`gen_shop_catalog.py`'s `load_obj_index`) instead of always taking the
  lowest. Spot-checking the most-repeated ambiguous names (`Bucket`,
  `Tinderbox`, `Chisel`, `Rope`, ...) showed the cache's real pattern: a
  tradeable general item plus an untradeable quest/minigame duplicate sharing
  its name. Sorting tradeable-first and only flagging a tie when *no*
  candidate is tradeable dropped `shop_stock.csv`'s review-flagged lines from
  2,970 to 592 and pushed clean tables from 429 to 1,101 of 1,229 — most of
  the "review" backlog §5/§7 described was this, not genuine ambiguity.
* **The trigger scan gained the inline shape.** `generalshop.rs2` binds
  `[opnpc3,generalshopkeeper2] @generalshop_trade_stub;` — trigger and call on
  one line — which the original scan's exact-line match never recognised as
  an existing trigger, so it read as "nothing bound here" and let a generated
  file declare the same `[opnpc3,...]` a second time. `make mock230-scripts`
  caught it as a duplicate-script-name warning; fixed by matching the header
  as a prefix and, for the inline case, resolving one level of `@label;`
  indirection to check whether *that* label's body is the bare
  "nothing to trade" stub.

The remaining gap to the full 609-shop catalogue is exactly what §5/§7 always
said it would be: the 15 multi-table pages (need section-aware inv binding),
the shops still short an inv binding or a spawned owner, and the token-shop
family. Nothing in that remainder is blocked by a tooling bug any more — it
is the review work §5 phases 3–6 describe.

### 8.1 Engine — all landed, `--selftest` clean

* `.inv` server config parsing (`scope=`, `restock=`, `allstock=`,
  `stackall=`, `stockN=`) — `mock230_content.c`'s `load_inv_config`.
* `mock230_shop.{c,h}` — the definition table, boot-time seed
  (`mock230_shop_seed`, called from every boot path: `mock230_main.c`'s
  `serve()` and `--selftest`, `mock230_embed.c`), and the restock tick
  (`mock230_shop_restock_tick`, called once per tick from `phase_cleanup` in
  `mock230_world.c`, in the same slot LostCity's `World.ts` puts it — right
  after the npc reset).
* `mock230_container_scope` classifies a shop's inv `WORLD` via
  `mock230_shop_is_shared`; every other inv is unaffected.
* `SS_OP_INV_STOCKBASE` / `SS_OP_INV_ALLSTOCK` — `mock230_ops_inv.c`,
  coverage table regenerated.
* The world-container multi-listener fan-out described in §3.1's update:
  `mock230_container_flush_world`, called once per tick from
  `phase_clients_out` before the per-player pass.
* `server/scripts/shop/scripts/shop.rs2` — the shared engine procs, ported
  from LostCity's `shop/scripts/shop.rs2` per §3.5. `openshop_activenpc` is
  present but commented out: it reads `npc_param(owned_shop)` and three
  siblings that this tree never allocates (§4.1 chose the explicit
  `~openshop(inv, ...)` call specifically to avoid that allocation), so a
  live body would fail to compile against an undeclared param.

**Open from §3.4/§3.5, not yet done:**

* The restock tick is written but not called anywhere per-tick. Wire
  `mock230_shop_restock_tick(srv, tick)` into `mock230_world.c`'s cleanup
  phase, next to the npc `resetEntity` loop LostCity's own `World.ts` puts it
  beside (§3.4).
* Shopside's real op indices and the Buy-X round trip are still unconfirmed
  (§3.5 point 4, §7) — the sell ladder in `shop.rs2` is LostCity's fixed
  Sell-1/5/10 shape on `inv_button2/3/4`, not verified against a rev-239
  decompile of `shopside`'s own script.
* No in-game playtest yet — everything above is verified by `--selftest`
  (content loads clean, 71 shop definitions parsed — 70 shared shops plus
  `rs2012_qbd_rewardinv`, a pre-existing non-shared inv the same loader now
  parses — 70 seeded) and by reading the generated files against the wiki
  source, not by opening a shop as a connected client.

### 8.2 Content — 70 shops live

Generated by `tools/gen_shop_scripts.py --write` from the reviewed rows of
`shop_inv_map.tsv` (the `verified`, LostCity-sourced tier — 262 rows, of
which 70 also clear every other gate today). Each is one `.rs2` + one `.inv`
under `server/scripts/shop/<area>/`, area from the wiki's `location=`:

al_kharid (5) · ape_atoll (3) · barbarian_village (1) · bedabin_camp (1) ·
brimhaven (1) · burthorpe (1) · canifis (2) · catherby (3) ·
dwarven_mine/dwarven_mines (2) · east_ardougne (3) · edgeville (1) ·
entrana (1) · falador (4) · fishing_guild (1) · grand_tree (5) ·
gutanoth (2) · kourend_castle (1) · lighthouse (1) · lumbridge (1) ·
mage_arena (1) · misc (3) · of_champions_guild (1) · of_legends_guild (1) ·
of_the_heroes_guild (1) · port_khazard (1) · port_sarim (2) ·
ranging_guild (2) · rellekka (4) · tai_bwo_wannai (1) · taverley (1) ·
tree_gnome_village (1) · tyras_camp (1) · varrock (4) ·
west_ardougne (1) · wilderness_bandit_camp (2) · wizards_guild (1) ·
yanille (1) · zanaris (2)

Two of those — `khazard_general_store` and `dwarven_shopping_store` — share
`generalshop.rs2`'s "general shop family" dialogue with F2P Lumbridge/Varrock/
Al Kharid/Edgeville and needed the same inline-stub patch (§8.1); all four
general-store gamevals in that file now route to their real shop.
| varrock | varrock_swordshop |
| wilderness_bandit_camp | tonys_pizza_bases |
| zanaris | irksol_shop |

Where a shop's owner already had a `[opnpc3,<gameval>]` "nothing to trade"
stub (19 of 21 — `bob.rs2`, `drogo_dwarf.rs2`, etc.), `gen_shop_scripts.py`
rewrote that stub's trailing `mes("...nothing to trade...")` line in place to
call the new shop's open label, and left everything else in that file —
dialogue, quest gates like Bob's `lost_tribe_bob_witness` check — untouched.
It refused (and reported) three shops rather than guess: `magearena_guardian`
and `grum`'s op3 blocks are not bare stubs, and `mcannonshop`'s wiki stock
(7 lines) exceeds its cache inv's slot count (6, §2.4). `--selftest` compiles
this shop tree with zero duplicate-trigger warnings and zero content errors.

### 8.3 Next

1. Section-aware inv binding in `gen_shop_inv_map.py`, to unblock the 15
   multi-table shops sitting at 100% otherwise-ready.
2. Phase 3/4 review work exactly as §5 describes, resized to today's counts:
   305 `proposed` `shop_inv_map.tsv` rows and 592 `review`-flagged
   `shop_stock.csv` lines are what stands between 70 shops and the
   265-shop phase-4 target.
3. An actual connected-client playtest of at least one shop, to catch what
   `--selftest` cannot: whether `shopmain` draws, whether the buy ladder's
   op1/op6 Value/Buy-N toggle behaves, whether the sell ladder's inferred op
   indices are right.
