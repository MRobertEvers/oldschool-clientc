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
* **"Shops" that are wiki reference tables for a different mechanic, not a
  shopmain/shopside storefront** — found while investigating why 141
  catalogued rows have no price multiplier *and* no currency (§8: the
  scraper correctly named these shops, but not all of them are shops).
  `{{StoreTableHead|hidebuy=y|hidecaption=y|...}}` — buying hidden, no
  caption — marks a page documenting what an npc pays for junk, not a
  browsable coin shop; 109 of the 141 carry it (checked directly, not
  estimated). Confirmed against one case in depth: `Apothecary's Potions`
  catalogues cleanly by every rule in §1–2, but the Apothecary is a Romeo &
  Juliet-quest brewing NPC (`LostCity_Server/.../apothecary.rs2`) — turn in
  ingredients and coins, get a potion back — with no `shopmain` interface
  anywhere in the reference. This is not a binding gap or a spawn gap; these
  rows should never generate a `.rs2` regardless of how good the tooling
  gets, and the honest addressable-shop count is smaller than the raw 609
  distinct-tables figure by roughly this many.
* **The remaining 32 of the 141 "no price" rows are real pub/bar buyback
  shops with a genuine parser gap, now partly fixed.** `sellmultiplier`/
  `buymultiplier` present, `delta` simply absent — because a shop with
  `buymultiplier=0` (nothing is ever purchasable, sell-only) has nothing for
  haggle to move the price of, so its page has no reason to state a
  meaningless `delta=0`. `gen_shop_catalog.py` now defaults `delta` to `0`
  for exactly that shape (`buymultiplier=0` and `delta` missing); applied to
  the live CSVs directly (not a full wikitext re-parse, which would have
  discarded the wiki-id and dedup fixes) — 26 rows correctly re-priced. None
  of the 9 that are otherwise ready (Dancing Donkey Inn, Flying Horse Inn,
  Fight Arena Bar, ...) generate yet — checked, and none has a matching inv
  in this cache — but they are correctly in the funnel for whenever that
  changes, instead of permanently mis-filed as an unrelated shop type. A
  narrower case was found and *not* fixed: `Erdan` and `Myths' Guild
  Armoury` catalogue with identical stock (same items, same counts) but
  disagreeing multiplier columns, which looks like a real parsing
  inconsistency between how the two pages structure `StoreTableHead` — worth
  a dedicated look, not a guessed default.
* **The buy-only mirror image, found next: `sellmultiplier` missing while
  `buymultiplier`/`delta` are present.** `Initiate Temple Knight Armoury` and
  `Proselyte Temple Knight Armoury` state `buymultiplier=500|delta=0` and
  nothing else — a reward-armour shop the wiki never gives a sell rate,
  because unlike the pub case the *item itself* is genuinely tradeable
  (checked `obj_stats.csv` before assuming otherwise), so "sell disabled"
  couldn't be inferred from the item record. Defaulted `sellmultiplier` to
  equal `buymultiplier` for this exact shape only — the zero-arbitrage
  choice, so a wrong guess costs nothing in either direction. Found the
  correct inv by tier number (`templeknight_armoury1`/`2`, Initiate/Proselyte)
  and generated the first; the second shares its owner npc with the first and
  is correctly blocked by the trigger-collision guard, same shape as the
  skillcape shops — a dialogue-choice candidate, not a generator gap.
* **Re-scoped the "no price" bucket with a broader, more accurate marker.**
  The `stock=inf` check that found 109 reference-table rows undercounted:
  `hidebuy=y`/`hidesell=y` on the page's own `StoreTableHead` is the real
  signal (buying or selling hidden = not a browsable two-way shop), and
  `stock=inf` is just one way that manifests. Re-run with the broader marker:
  of the 141 original "no price" rows, **122 are now accounted for** — 109
  reference tables, 26 pub-buyback (2 of those also missing `hidebuy`, still
  correctly caught), 2 buy-only reward shops — leaving **19 genuinely
  unresolved**, mostly `__2`/`__3` sub-tables of npcs whose primary shop
  entry is already handled. That is the real remaining size of this bucket,
  not the 141 or 24 earlier passes reported.
* **Buying back sold items at the price the shop paid** — OSRS tracks a
  per-shop sold-item history that neither LostCity nor this plan models.
* **Whether the 2,542 `ambiguous` obj rows are all lowest-id** — spot-checked,
  not proven. They are flagged so the generator refuses them, which is the
  point; the proof is phase 3's work.

---

## 8. Status, 2026-08-13

Phase 1 (engine) is done. **184 shops are live** — every shop the pipeline can
currently clear without a human decision. Seven improvements past the
original write-up widened that from the initial 21/36, the seventh being a
by-hand review pass rather than another mechanical rule (§8, general stores).

### 8.4 A second by-hand binding pass, and the wall behind the rest

A later pass reviewed the ~161 shops blocked *only* on an unverified inv
binding (everything else about them — coin pricing, reviewed stock, a
spawned owner — already checked out). 20 were promoted to `verified` under
the same evidentiary bar as §8's general-store pass: exact npc/inv name
identity, or a specific location+domain stem match with zero collision
against another shop's proposed binding in the same batch. The other ~141
were explicitly rejected, not skipped — the two failure shapes worth naming
because they'll recur:

* **Location-blind collisions.** `tal_teklan_dyeshop` was namematch's
  proposal for three unrelated pages (an archery shop, a dye shop, a rune
  shop) because they share the `tal_teklan_` owner prefix; `keldagrim_*`
  stall names got proposed for Ardougne/Prifddinas pages on the same
  "stall" stem with the location word stripped as noise. Any candidate that
  collides with another shop's proposal in the same run is refused, full
  stop — a specific-but-wrong match is worse than no match, because it ships
  silently.
* **Genuinely unspawned owners, not a binding problem.** 5 of the 20
  promoted bindings (`deepfin_dwarf_korgan`, the `myq6_efaritay_*` family,
  `fortis_shop_general_1`, `port_roberts_shopkeeper`,
  `lovakengj_thirus_*`) are correct and still didn't generate — grep against
  every `areas/world/configs/*.spawn` file confirms zero spawn rows for any
  of them. This is the same Varlamore/post-2004-region gap the original
  write-up found in bucket (c): the only spawn source this tree has
  (`xrsps-typescript`'s npc-spawns.json) has zero Varlamore rows, and a
  broader sample of the "no spawned owner" bucket (59 checked) is almost
  entirely Fortis/Cam Torum/Port Roberts/Aldarin/Sunset Coast/Kastori —
  Varlamore locations, confirming this is one root cause, not 59 separate
  ones. Not fixable from inside this pipeline; needs a spawn data source
  this tree doesn't have.

A third root cause, found while chasing the Prifddinas cluster (11 shops:
Amlodd's Magical Supplies, Aneirin's Armour, Branwen's Farming Shop, Elgan's
Exceptional Staffs, Guinevere's Dyes, Gwyn's Mining Emporium, Lliann's
Wares, Sian's Ranged Weaponry, the four Prifddinas stalls, plus Arceuus'
Filamina's Wares and Regath's Wares): their owner npcs *are* spawned
(confirmed against `areas/world/configs/*.spawn`), so this isn't the
Varlamore no-spawn gap. It's that `configs/all.inv.compack` has exactly four
`prif_*` invs total — `prif_food_store`, `prif_mace_store`,
`prif_weapon_store`, `prif_leigh_store` — none matching any of these 11
shops' domains, and zero `arceuus_*` invs at all. This cache snapshot
predates that game content; no binding rule fixes a shop whose inv was never
packed. 13 more shops added to the "blocked on missing data" ledger, next to
the 129 Varlamore ones — same shape of problem, different game update.

The "not coin-priced" bucket (206) was also checked rather than assumed: a
sample of the 129 with a *blank* currency field (not a named alt-currency
like League Points) turned out not to be a scraper gap. `Ajjat` — a
representative case — has no `StoreTableHead` at all; it is a one-way
fixed-price sale (99,000gp for a skill cape) with no buy/sell ladder, no
restock, no haggle delta. That is a different feature
(`buy_item`/`sell_item` are ladder+haggle by construction — see §3.5) than
what this pipeline builds, correctly out of scope per §7, not a missed 206
shops.

### 8.4.1 Three "collisions" that were actually already-solved

`general_store_canifis__2`, `karamja_general_store__2`, and
`oblis_general_store__2` all proposed the same wrong inv
(`regicide_general_shop_2`, itself already correctly claimed by an unrelated
Tyras Camp shop) purely because they share the generic "general store"
naming stem. Checked what each one's own `__1` sibling already resolved to
instead of retrying the namematch guess: `general_store_canifis__1` is
already live on `werewolfgeneralstore`, `karamja_general_store__1` and
`oblis_general_store__1` are already live too. All three `__2` shop_keys are
just that same real single shop's second wiki stock table — the shop is
already fully implemented via `__1`, same pattern as
`fernaheis_fishing_hut__2` and `alis_discount_wares__2`–`__7`. Reclassified
from "blocked" to "already covered," not left as an open question.

### 8.5 Whether the Prifddinas/Arceuus/Darkmeyer wall has a door — tested, no

The 13 shops in that cluster looked, on closer inspection, like they might
not be a spawn-data gap at all: their owner npcs (Aneirin, Elgan, Guinevere,
Gwyn, Lliann, Filamina, Regath, Thyria, ...) turned out to have real spawn
coordinates in the wider reference data, and were already confirmed present
in this repo's own `areas/world/configs/*.spawn` files (§8.4's grep). The
actual blocker is narrower than "no data" — it's that `configs/all.inv.compack`
was never packed with a `prif_*`/`arceuus_*`/`darkm_*` inv for any of them.

That looked closeable: `tools/ss_allocate.py` already lets content declare a
brand-new inv name in `pack/inv.alloc` with no cache slot behind it at all
(`rs2012_qbd_rewardinv`, `summoning_bob`). Tested directly — wrote a real
`aneirins_armour_shop` block (12 already-resolved stock lines, verified
spawned owner `prif_armourstore`, matching `~openshop` call), let the
allocator assign it id 2002, and rebuilt. It compiled clean, but selftest
logged `shop inv 2002 has a definition but no cache size; not seeded`:
`mock230_container_resolve` sizes a shared shop container from the cache's
own inv definition, and a content-only id has none. `rs2012_qbd_rewardinv`
and `summoning_bob` get away with this because they're fixed-purpose
containers with a size the reading code already assumes; a shop is a
variable-size, client-rendered grid with no such assumption to fall back on.

This is a real, fixable engine gap — teach `mock230_shop_seed`/
`mock230_container_resolve` a content-declared size for shop invs specifically
(e.g. a `size=` line in the `.inv` file itself, already parsed and currently
inert per `inv_config_key`'s own `size=` branch) — but it is engine work, not
content wiring, and deliberately out of scope to improvise under this plan.
Reverted the experiment cleanly (deleted the two test files, removed the
allocator entry, rebuilt, `all checks passed`) rather than leave broken
content behind. Filed here so the next attempt at this cluster starts from
"the fix is a container-sizing feature" instead of re-discovering it.

**Update, same day: built it.** `inv_config_key`'s `size=` branch now only
errors when the inv already has a real cache size (`mock230_bank_inv_size`
> 0) — otherwise it calls the new `mock230_shop_def_set_size`, and
`mock230_container_resolve` falls back to `mock230_shop_content_size` when
the cache lookup comes back empty. A real cache size still always wins; this
only ever fires for a `pack/inv.alloc` id with nothing behind it. Re-ran the
Aneirin's Armour test with `size=12` added: container created with exactly
12 slots, all 12 stock lines seeded with the right obj_ids and baselines
(confirmed with a temporary debug dump, since removed), zero shop-related
selftest errors. Rolled the same pattern out to the rest of the cluster —
Amlodd's Magical Supplies, Branwen's Farming Shop, Elgan's Exceptional
Staffs, Guinevere's Dyes, Gwyn's Mining Emporium, Lliann's Wares, Sian's
Ranged Weaponry, Filamina's Wares, Regath's Wares, Thyria's Wares, and the
five Prifddinas stalls (gem/general/herbal/silver/spice) — 16 more shops,
each `size=`d to its own resolved stock-line count. Full-tree selftest after
all 16 landed: 225 shop definitions, 224 seeded, zero "not seeded" warnings,
zero shop-related content errors (the one remaining selftest failure is 39
pre-existing errors in `tormented_demons.npc`, unrelated content). The
Darkmeyer shops (5) and the rest of the "no matching inv" ledger are the
same fix away from unlocking too — this mechanism generalizes to any shop
blocked purely on a missing cache inv, not just Prifddinas/Arceuus.

**Update, same day: 4 of 5 Darkmeyer shops done the same way** — Darkmeyer
General Store, Darkmeyer Lantern Shop, Darkmeyer Meat Shop, Darkmeyer
Seamstress. Full-tree selftest after: 229 shop definitions, 228 seeded, zero
new errors. The 5th, Despoina Callidra, does not: its one stock line is
wiki-stated as infinite stock with no restock rate (a sell-only novelty
item — one pint, never restocked because it never depletes).
`stockN=obj,baseline,rate` has no representation for "no baseline, no rate,"
and inventing a large placeholder number would be guessing a mechanic, not
reading one. Left unbound rather than fabricate a number — the sizing fix
doesn't help this one, a separate stock-model gap does.

**Update: the mechanism generalizes past Prifddinas/Arceuus/Darkmeyer.** Every
shop in the "no matching inv anywhere in the cache" bucket found across the
whole session — regardless of region — was blocked by exactly this, not a
binding problem. Re-scanned the full unbound-candidate list with that
understanding and batch-generated 44 more shops the same way (Ardougne
Silver Stall, Blair's Armour, Briget's Weapons, all three Crossbow Shop
locations — no longer ambiguous, since each gets its own fresh inv instead
of fighting over 2-3 existing ones — Jennifer's General Supplies, Kenelme's
Wares, Little Shop of Horace, Lletya's archery/seamstress pair, Logava
Gricoller's, Nardok's Bone Weapons, Toothy's Pickaxes, Vannah's Farming
Stall, Warrens General Store, Zaff's Superior Staffs, and more). Two real
bugs surfaced and were fixed before landing: `silver_merchant_ardougne` and
`zaff` already owned an `[opnpc3,...]` trigger elsewhere (a stall-theft-alert
script, a stub) — patched those in place to call the new shop's label
instead of declaring a duplicate trigger, the same discipline as every
dialogue-wiring pass earlier in this plan.

**7 of the 44 hit the same "∞ stock" wall as Despoina Callidra** — every pub
in the batch (Dancing Donkey Inn, Flying Horse Inn, the Dragon Inn
bartender, Falador Party Room, Fight Arena Bar, the Sheared Ram) and
Gardener Gunnhild's tool-lending stall have **100% infinite-stock lines**,
confirmed against the live wiki (`Number in stock: ∞`, `Restock time: N/A`).
Not partial — every line in all 7. Reverted all 7 rather than ship a shop
whose container silently ends up empty (the content parser rejects an `∞`
`stockN=` line outright and moves on, so the shop would open with a real
trigger and title but zero items). This is the concrete shape of the
remaining stock-model gap: OSRS pubs are typically genuinely unlimited, and
`stockN=obj,baseline,rate` has no field for that. Fixing it means adding an
"unlimited" flag to the shop stock line, not another sizing trick — filed
next to Despoina Callidra as the same problem, now confirmed to recur.

Full-tree selftest after the batch: 266 shop definitions, 265 seeded, zero
content errors, zero "not seeded" warnings, `all checks passed`.

**Correction, same day: `is_shop_page` was never checked, and it should
have been.** The user asked directly whether the "infinite stock" pubs were
actually a dialogue-granted item rather than a real shop — checking that
properly surfaced that `shop_catalog.csv` already has a field for exactly
this question (`is_shop_page`, set by the scraper from whether the wiki page
actually carries the shop infobox/template), and no filter anywhere in this
session's binding work ever read it. Audited every generated shop against
it. Result: 4 false positives caught, of very different severity —

* `bartender_dragon_inn`, `gardener_gunnhild`, `despoina_callidra`:
  `is_shop_page=no`, and checking confirmed why — these are reference
  tables on an NPC page, not shopfronts. Reverted; the infinite-stock
  approximation went with them since it no longer had anything to attach
  to. Despoina Callidra's 5th-shop status from earlier in this section is
  now moot for the same reason.
* `tramp_pillory`: `is_shop_page=no`, and this one is the real find — it
  is not a merchant at all, it is a pillory-punishment minigame mechanic
  (throws rotten fruit at an imprisoned player). This had been generated as
  a working `~openshop` for two full binding cycles before this check
  caught it. Reverted.
* `scavvo`, `nulodion__1`, `drogo_dwarf`: also `is_shop_page=no`, but
  verified live against the wiki as real, working shops whose NPC page
  simply doubles as the shop page (no separate shop article exists) — a
  scraper false-negative, not a modeling error. Left as-is.

The lesson isn't "always trust is_shop_page" (3 of 4 flags here were
noise) or "never trust it" (1 of 4 was a real bug) — it's that the field is
a prompt to go verify, the same evidentiary bar this whole plan has used
for bindings, now extended to "is this even a shop" rather than just
"which inv." Full-tree selftest after: 270 shop definitions, 269 seeded,
zero content errors, `all checks passed`.

### 8.6 Milestone: the coin-shop catalog is done

`shop_inv_map.tsv` had drifted from reality twice this session — every
batch generated by writing files directly (bypassing
`gen_shop_scripts.py`'s own bookkeeping step) left its bindings unrecorded,
so the tracking sheet kept reporting shops as "unbound" that were actually
long since live. Fixed properly this time: scanned every `.rs2` under
`server/scripts/shop/` for its real `~openshop(inv, ...)` call and synced
the sheet from that ground truth (46 rows corrected) instead of
reconstructing it from memory. Re-ran the unbound-candidate query against
the corrected sheet: **16 hits, all already accounted for** — `Ali's
Discount Wares`' six sub-tables (2 through 7, one real shopfront, already
live under `__1`), five more `__2`/`__3` sub-tables of already-live shops
(Fernahei's Fishing Hut, General Store (Canifis), Jiminua's Jungle Store,
Karamja General Store, Ned's Handmade Rope, Obli's General Store, the Magic
Guild Store's two 1-line skillcape variants), and `Etceteria Fish`
(confirmed byte-identical duplicate stock to the already-live `Island
Fishmonger`). Zero of the 16 are a real, distinct, resolvable shop sitting
unbuilt.

That means every coin-priced shop in the catalog with (a) a real shopfront
page, (b) a spawned owner npc, (c) fully-resolved stock, and (d) no
alternate currency is now implemented — 270 shop definitions, verified via
`all checks passed` on a clean full-tree rebuild. What remains is
exhaustively itemized, not vague: ~130 shops with no spawned owner
anywhere in this tree's data (overwhelmingly Varlamore, confirmed against
the wider reference spawn source too — a missing-data wall, not a binding
gap), the "not coin-priced" 206 (a different shop mechanic — fixed-price
single sales — never built this session), a handful of alt-currency shops
(Tokkul, Trading sticks, Stardust, ...) that need a currency-aware
`buy_item`/`sell_item` before they can be bound at all, and a small number
of stock-line ambiguities the wiki itself doesn't resolve (the "pink boxing
gloves" cluster). Each of those is a different, named, already-diagnosed
problem — none of them a shop this pipeline could have generated and
simply hadn't gotten to yet.

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
* **A skillcape-tier naming convention for multi-table pages, applied only
  where it's a fact, not a guess.** A page's 2nd/3rd `StoreTableHead` is
  usually its skillcape / trimmed-skillcape sub-stock, and the cache follows
  one name pattern for it — `{base_inv}_skillcape` / `_skillcape_trimmed` —
  but only where that variant actually exists (`gen_shop_inv_map.py`'s
  `skillcape_variant`). Applying it also **surfaced and removed 26 wrong
  bindings**: the old namematch pass had been pointing a page's sub-table at
  the exact same inv as its own base table (`aemad__2` on `adventurershop`,
  same as `aemad__1`) purely because both tables share one owner npc — a real
  correctness bug in the worksheet, not a coverage gap, now fixed by trying
  the skillcape derivation *before* the owner-based lostcity lookup for any
  sub-table, and refusing the owner-based fallback for one entirely (the
  bug's only source). It also exposed that `__` in a shop key was never a
  real reason to skip generation — the base table (`__1`, no section) is an
  ordinary shop, and blanket-skipping it alongside its genuinely-unbindable
  siblings had been hiding 14 ready shops.
* **Exact npc↔inv gameval identity, a distinct signal from token overlap.**
  This cache's newer (post-2004) content routinely names a shop's inv after
  the one npc who owns it — `aldarin_general_store` the npc runs
  `aldarin_general_store` the inv, `port_roberts_silver_trader`,
  `cam_torum_shop_blacksmith`, and 101 more. That is not weak evidence merely
  scored low; `tokens()`'s token-overlap heuristic strips "shop"/"store" (the
  right call for *that* rule, since those words are near-universal noise
  across the namespace) and was scoring these as ordinary 2-token matches
  when the truth is a literal string match. Checked as its own rule, before
  token-overlap, gated the same way as the plain lostcity lookup (skipped for
  a multi-table sub-table, for the same shared-owner reason as above): 106 of
  the 311 `proposed` rows this pass looked at were exactly this shape. Zero
  regressions on the 262 already-verified rows (checked by diff before
  writing).
* **`tools/wiki_item_ids.py` — resolving a `review`-flagged stock line from
  the item's own wiki page instead of the shop's.** `Skirt (blue)` and
  `Skirt (lilac)` share one cache display name (`review=variant`, §2.1), but
  each has its *own* wiki page, and that page states
  `{{Infobox Item|...|id=5052}}` — the exact cache id, not a guess. Fetched
  all 240 distinct flagged item names; 152 carried a plain `id=` (multi-
  version items using `id1=`/`id2=` are not read — a real remaining gap, not
  a false positive) and every one of those 152 resolved to a real record in
  this cache (`configs/all.obj.compack`), patching 412 of the 592 flagged
  stock lines with `match_rule=wiki-infobox-id`. `shop_catalog.csv`'s
  `lines_needing_review` is recomputed from the patched stock, in place —
  re-running `gen_shop_catalog.py` itself was deliberately avoided here, since
  that would re-derive `shop_stock.csv` by name-matching again and silently
  discard everything this pass just resolved by wiki authority.
* **`owner-stem-match` — one step weaker than exact identity, still an
  identity check rather than a count.** `exact-gameval-match` requires the
  raw strings to be equal; a lot of near-misses were an owner's *role* suffix
  away from that — `warguild_armour_shopkeeper` runs `warguild_armour_shop`,
  `roguesden_trader` runs `roguesden_shop`. Stripping a fixed, small set of
  role suffixes off the owner (`_shopkeeper`, `_owner`, `_1op`, ...) and a
  shop suffix off the inv (`_shop`, `_store`, ...) and requiring the
  *remainder* be identical — not a prefix, not an overlap count — is still an
  identity claim, just after removing two known-meaningless decorations. All
  29 matches this produced were inspected by hand before promoting the rule
  to `confidence=verified` (`royal_generalstore_owner`/`royal_generalstore`,
  `seed_merchant`/`seed_stall`, `piscarilius_fishing_supplies_trader`/
  `piscarilius_fishing_supplies`, ...) — none were a judgment call.

* **A by-hand review pass, scoped to general stores** (73 catalogued rows —
  the most iconic, most-requested shop family, and predictable enough to
  review at volume). Cross-referenced every general-store-shaped inv name in
  `configs/all.inv.compack` (`grep`-built, not guessed — `generalshop1..9`,
  `<place>_general_store`, `<place>generalstore`, ...) against each unbound
  or weakly-`proposed` general store's own `location=`. 12 place-name matches
  confirmed this way, all cross-checked against `shop_owners.csv` for a real
  spawned owner before trusting them: `dorgesh_kaan_general_supplies` ->
  `dorgesh_general_store`, `lletya_general_store` -> `lletyageneralshop1`,
  `razmire_general_store` -> `razmiregeneralstore`, and so on. Two of these
  caught the token-overlap rule pointing at a **wrong** place entirely —
  `tal_teklan_general_store` had been proposed onto `tal_teklan_dyeshop` (the
  *dye* shop) and `port_phasmatys_general_store` onto `port_roberts_general_store`
  (a different Port, in a different content era) — both fixed to the correct
  same-place inv. A third finding was a genuine namematch collision, not a
  single wrong guess: `karamja_general_store__2`, `oblis_general_store__2` and
  `general_store_canifis__2` had all independently landed `proposed` on the
  exact same inv (`regicide_general_shop_2`) — three different shops cannot
  share one container, so a fourth row's own `lostcity`-verified binding to
  that same inv (`quartermasters_stores`, correctly) confirmed which one (if
  any) was right, and the other three were cleared back to unbound rather
  than left to mislead a later pass. One more fix generalized past this
  session: `leenzs_general_supplies`'s owner is `piscarilius_generalstore_keeper`
  — `_keeper` was missing from `owner-stem-match`'s role-suffix list, added,
  and it now resolves on its own without a one-off entry.
* **The multinpc-base spawn gap, found while reviewing stalls.** Two Sophanem
  stall owners (Nathifa, Jamila) read `no-spawned-owner` even though they are
  old (Contact! quest-era) content, not Varlamore — worth checking rather
  than filing under the confirmed spawn-dump gap. Their wiki-stated npc ids
  resolve to `contact_market_baker`/`contact_market_craft`, and *those*
  really don't spawn — but `.spawn` files name
  `contact_market_baker_multi`/`contact_market_craft_multi`, the multinpc
  *base* (same shape as the Lumbridge doomsayer,
  [[mock230-lumbridge-content]]: a varbit picks the display variant at
  runtime, but only the base ever stands in the world). `wiki_shop_owners.py`
  now checks for a spawned `<gameval>_multi` sibling before giving up, and
  `owner-stem-match`'s suffix list strips `_multi` (looping, since it can
  stack with a role suffix — `ahoy_akharanu_multi`). 13 owner rows across 13
  shops fixed this way, all confirmed by checking the actual `.spawn` file
  contents, not inferred from the suffix pattern alone. **Also fixed while
  applying this**: `--refresh` had been silently deleting every
  `manual-review` row — it re-derives every row from the mechanical rules,
  which have no way to know a hand-verified finding exists, so the first
  `--refresh` after this session's general-store pass wiped all 12 of those
  rows. `manual-review` is now exempt from `--refresh` the same way a
  pre-existing `verified` row already was.
* **A third by-hand pass, on the generic "X store" family** — turned up a
  classifier bug, not just missing bindings. `NON_SHOP_PREFIXES` excluded
  every `smithing_`-prefixed inv as a smithing-minigame progression menu
  (`smithing_bronze1..6`, `smithing_iron_claws`, ...), which is right for
  those but was also hiding `smithing_guild_buyer` and
  `smithing_guild_ore_seller` — two real Blast Furnace storefronts. Narrowed
  the exclusion to the specific metal/tier prefixes that are genuinely not
  shops (checked against every `smithing_*` name the cache has, not
  guessed), and both resolved on their own via the existing
  `exact-gameval-match` rule the moment they were reachable. 7 more
  place-name matches on top of that (`hendors_awesome_ores` ->
  `mguild_oreshop`, `weapons_galore` -> `frisd_weaponshop`, `void_knight_archery_store`
  -> `pest_archery_store`, ...), each cross-checked against a spawned owner
  first. One (`davons_amulet_store__2`) is a second wiki table for a shop
  already bound under `__1` — recorded for completeness; `gen_shop_scripts.py`
  correctly leaves it unclaimed rather than declaring the same npc's trigger
  twice, the same collision guard that already covers the general-store and
  skillcape cases.
* **A fourth by-hand pass — numbered/prefixed families found by scanning the
  121 remaining "blocked only by binding" shops for a repeated owner
  prefix.** `wilderness_capeseller_1..10` -> `wildernesscapeshop1..10`, a
  clean numbered correspondence, all 10 confirmed spawned before applying
  (10 shops). `dwarf_city_shop_{bakery,pickaxe,warhammer,cloth}` ->
  `keldagrim_{bread_stall,pickaxe_shop,warhammer_shop,clothes_shop}`, place
  and purpose both matching (4 shops). Three Prifddinas shops matched an
  underscore-normalization of their own owner name (`prif_foodstore` /
  `prif_food_store`, `prif_macestore` / `prif_mace_store`,
  `prif_weaponstore` / `prif_weapon_store`) — **the other 9 Prifddinas shops
  on the list have no inv of that shape in this cache at all** (checked by
  grep, not assumed), a real content gap rather than a binding one, left
  unbound. Same for Darkmeyer (4 shops, zero `darkm`-prefixed invs exist) and
  most of Shayzien (only `shayzien_rangeshop`/`_pub`/`_clothesshop` exist;
  `shayzien_armourshop`/`_weaponshop`/`_generalstore` do not).

* **A catalog-level bug, not a binding gap: `gen_shop_catalog.py`'s dedup was
  merging different real shops that share a stock template.** Its
  `mark_duplicates` collapses two catalogued tables into one whenever their
  stock and multipliers match exactly — right for a shop-page +
  hosting-npc-page pair (the case it was built for), wrong for two genuinely
  separate shops Jagex built from the same template. `varrock_general_store`
  and `al_kharid_general_store` sell an identical item list at identical
  prices and are obviously not the same shop; the same was true for 59 pairs
  across the catalogue (fur/gem/silver/spice stalls, general stores, pub
  templates), and the merge meant `gen_shop_scripts.py` silently generated
  only one of each pair, forever, with the other's stock and location visible
  in the CSVs but never producing a file. Fixed by requiring `location=` be
  either equal or blank on at least one side before merging — blank is the
  common real case (an npc's own infobox usually states none), an active
  disagreement is not. Unmerging is safe by construction: a location-agreeing
  pair still merges exactly as before, so nothing that was correctly deduped
  stopped being deduped. Re-running `mark_duplicates` against the already-
  patched `shop_stock.csv` (not the wikitext pass, which would have discarded
  every `wiki_item_ids.py` fix) freed 59 shops from a false merge, and 10 of
  those — `varrock_general_store`, `falador_general_store`,
  `zanaris_general_store`, `rimmington_general_store`, and six more — turned
  out to already have everything else they needed and generated immediately.

Two avenues were tried and deliberately not taken, because both would have
traded real coverage for false confidence: cross-checking the remaining
`proposed` bindings against LostCity's own stock data (zero of them show any
meaningful overlap — LostCity never ported these shops, so there is no
independent signal to check against) and lowering the token-match bar on the
*token-overlap* rule itself for whatever is left after every identity-based
rule above (still 2–5 shared tokens, weak enough that auto-approving risks a
shop silently selling the wrong items at the wrong price under the wrong
container).

The remaining gap to the full 609-shop catalogue is what's left after all of
that: the shops still short a *reviewed* inv binding or a spawned owner, the
sub-tables with no skillcape-style cache counterpart, and the token-shop
family (out of scope by design, §7). Nothing in that remainder is blocked by
a tooling bug any more — it is the review work §5 phases 3–6 describe, and it
needs a person, not a smarter heuristic.

**The "no spawned owner" gate was checked against its actual source, not just
re-reported.** 42 shops today are otherwise fully ready (priced, clean stock,
verified inv) and blocked only on their owner not appearing in any `*.spawn`
file. All 42 are Fortis Colosseum / Aldarin / Cam Torum / Port Roberts /
Sunset Coast / Quetzacalli / Salvager Overlook npcs — every one of them
Varlamore content, the most recent OSRS region. Checked directly against
`gen_spawns.py`'s own upstream source
(`~/Documents/git_repos/xrsps-typescript/server/data/npc-spawns.json`, 24,145
records): **zero of these 42 npcs appear in it at all**, by name or by any
match. This is not a naming mismatch this repo's tooling could fix — the
spawn dump itself predates Varlamore. Closing this gate needs a newer spawn
source (or hand-recorded coordinates from someone who can walk there
in-game), not another pass over the existing pipeline.

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
  (content loads clean, 114 shop definitions parsed — 113 shared shops plus
  `rs2012_qbd_rewardinv`, a pre-existing non-shared inv the same loader now
  parses — 113 seeded) and by reading the generated files against the wiki
  source, not by opening a shop as a connected client.

> **Verification, this pass:** the `maplink.rs2` breakage above was another
> stream's, and cleared on its own — `make -C src mock230-scripts` now
> compiles clean and `--selftest` confirms 162 shop definitions parsed, 161
> seeded, matching this section's count exactly. (Three unrelated,
> already-committed door-count failures remain in the suite —
> `doors.loc`/`doubledoors.loc`, from the same door-opening work referenced
> in earlier status notes — not shop content and not this pass's to fix.)

### 8.2 Content — 270 shop definitions live (mock230 selftest count; most
cache-bound, dozens now content-declared via §8.5)

Generated by `tools/gen_shop_scripts.py --write` from the reviewed rows of
`shop_inv_map.tsv` (the `verified` tier — 386 rows after §8's binding
accuracy passes, of which 113 also clear every other gate today: coin-priced,
clean stock, a spawned owner, an inv whose slot count fits, and a trigger
this tool could safely take over. Of the other verified-but-not-generated
rows, 42 are blocked *only* by their owner missing from the spawn roster —
confirmed a real external data gap, not a binding problem, see below). Each
is one `.rs2` + one `.inv` under `server/scripts/shop/<area>/`, area from the
wiki's `location=` (over 40 areas, one to six shops each — al_kharid,
ape_atoll, ardougne, falador, grand_tree, rellekka and varrock currently carry
the most; run `find server/scripts/shop -mindepth 3 -name '*.rs2' | wc -l`
for the live count, since it moves every time this pipeline runs):

al_kharid (5) · ape_atoll (4) · ardougne — east (4) · bandit_camp (1) ·
barbarian_village (1) · bedabin_camp (1) · brimhaven (1) · burthorpe (1) ·
canifis (2) · catherby (3) · draynor_village (1) ·
dwarven_mine/dwarven_mines (2) · edgeville (1) · entrana (1) · falador (4) ·
fishing_guild (1) · grand_tree (5) · gutanoth (2) · jatizso (1) ·
keldagrim (1) · kourend_castle (1) · lighthouse (1) · lumbridge (1) ·
mage_arena (1) · misc (5) · miscellania_and_etceteria_dungeon (2) ·
museum_camp (1) · myths_guild (1) · of_champions_guild (1) ·
of_legends_guild (1) · of_legends_guild_west_side (1) ·
of_the_heroes_guild (1) · of_warriors_guild (2) · of_the_warriors_guild (1) ·
port_khazard (1) · port_piscarilius (1) · port_sarim (2) · rimmington (1) ·
ranging_guild (2) · rellekka (4) · rogues_den (1) · shayzien (2) ·
tai_bwo_wannai (1) · taverley (1) · tree_gnome_village (1) · tyras_camp (1) ·
varrock (6) · west_ardougne (1) ·
wilderness_bandit_camp (2) · wizards_guild (1) · yanille (1) · zanaris (2)

Four gamevals in `generalshop.rs2` — `generalshopkeeper3/5/7`,
`khazard_shopkeeper`, `dwarven_shopkeeper` — needed the inline-stub patch
(§8.1) alongside the 19 block-form stubs `bob.rs2` and its siblings carried;
all now route to their real shop rather than `@generalshop_trade_stub`.

`gen_shop_scripts.py` refused (and reported) rather than guessed on: three
owners whose op3 body isn't a recognised stub shape (`magearena_guardian`,
`grum`, plus a handful the skillcape pass's `[opnpc3,X]` collision check
caught), `mcannonshop`'s wiki stock (7 lines) exceeding its cache inv's
6-slot size (§2.4), and every skillcape sub-table whose owner npc's trigger
is already claimed by its own base shop — those need a dialogue-level
"which cape do you want" branch inside the base shop's open script, not a
second `[opnpc3,...]` trigger on the same npc, and that is genuinely a
hand-authoring task, not a generator gap. `--selftest` compiles this shop
tree with zero duplicate-trigger warnings and zero content errors.

### 8.3 Next

1. ~~The 8 skillcape sub-shops~~ **Done.** `aarons_archery_appendages__1/2/3`,
   `auburys_rune_shop__1/2/3`, `hicktons_archery_emporium__1/2/3`,
   `martin_thwaits_lost_and_found__1/2/3`'s base `.rs2` files now open with a
   `~p_choice3` — general wares / cape / trimmed cape — instead of a bare
   `~openshop`, each branch calling its own already-`skillcape-variant`-bound
   inv. The 8 `.inv` files were hand-authored following the generator's own
   format (one stock line each — the cape itself, `stock=1,restock=1`,
   matching the wiki exactly). Not modelled: OSRS gates a skillcape purchase
   on the buyer's own skill being 99; these sell to anyone, the same
   simplification every unlevelled shop in this tree already makes. 169 live
   shops now counts these 8.
2. Phase 3/4 review work exactly as §5 describes, resized to today's counts:
   ~165 `proposed` `shop_inv_map.tsv` rows (outside general stores and stalls,
   which §8's by-hand passes cleared — every remaining one genuinely needs a
   person to look at the specific shop, not a lower threshold) and 130
   `review`-flagged `shop_stock.csv` lines with no wiki-stated item id at all
   (down from 592 — `wiki_item_ids.py` now also reads a whitelisted
   `version1=`/`id1=` pair, e.g. `Inventory`/`Worn`, `Fixed`/`Broken`; the
   other version-pair vocabularies in the corpus — `Active`/`Inactive` charge
   states, `Reward`/`During event` exclusives, pure cosmetic variants — are
   genuinely too heterogeneous to resolve safely and stay flagged on
   purpose) are what stands between 115 shops and the 265-shop phase-4
   target. The same by-hand technique §8 used for
   general stores — grep the inv namespace for a shop-family's naming
   pattern, cross-check against `location=`, verify a live owner before
   trusting it — applies to any other shop family with a memorable/systematic
   name (e.g. skill guides, `_stall` shops) and is worth repeating rather
   than treating the remainder as one undifferentiated queue.
3. A newer spawn source. 42 otherwise-ready shops are blocked purely on their
   Varlamore-region owner missing from `xrsps-typescript`'s spawn dump
   (confirmed absent, not misnamed, §8) — nothing in this pipeline can fix
   that without a newer dump or hand-recorded in-game coordinates.
4. An actual connected-client playtest of at least one shop, to catch what
   `--selftest` cannot: whether `shopmain` draws, whether the buy ladder's
   op1/op6 Value/Buy-N toggle behaves, whether the sell ladder's inferred op
   indices are right.

---

## 9. Selling, as fixed (2026-08-19)

Reported from a live client: *"selling to a general store or any other store
doesn't change the stock of the store."* Three separate causes, in the order
the sale hits them.

### 9.1 A shop's stock is a count, not a pile — `stackall` was general-store-only

`tools/gen_shop_scripts.py` emitted `stackall=yes` (and `allstock=yes`) only
for a shop whose wiki `special=` reads *general store* — 41 of the 275 shop
invs in the tree. For the other 234 a shop slot held one physical item, so
`inv_itemspace(%shop, <an unstackable obj>, 1, inv_size(%shop))` asked for a
**free slot**. A specialty shop's cache inv is sized to its stock table
exactly (Bob's Brilliant Axes: 7 slots, 7 stock lines, all occupied), so the
answer was always "no" and `~sell_item` returned on

    The shop has run out of space.

before touching anything. Measured, not inferred — this is the whole of the
report for every non-general shop: the axe stayed in the backpack, the shelf
stayed at 10, no coins.

`stackall` is not a general-store trait and was never meant to read as one.
The wiki writes *Bronze axe x10* for a shop that has never held ten cells, and
LostCity states `stackall=yes` on all 121 of its shop invs, general or not.
The generator now emits it for every shop, and the 233 already-generated files
got the same line (the generator is deliberately write-once per shop, so it
does not re-render a file it already wrote).

`allstock` is a different question — *acceptance*, not storage — and stays
general-store-only, which is what the wiki says: a general store "usually
accept[s] all tradeable items", a specialty shop buys only what it already
stocks. It also still gates the one-per-minute destock of a line the shop has
no baseline for.

### 9.2 A noted item could not be sold at all

`~shop_sell_slot` passed `oc_uncert($item)` to `~sell_item`, which LostCity's
own `shop_request` does not do. Everything downstream then asked the
*backpack* about an id it was not holding: `inv_total` came back 0,
`~calculate_items_amount_sold` returned 0, and the click did nothing — no
message, no coins, no stock. It now hands on the raw obj, the way the
reference does, and `~sell_item`'s existing `oc_uncert(...)` calls keep
answering the shop-side questions.

### 9.3 `inv_moveitem_uncert` did not un-note

Which is why §9.2's uncert was there. The generic (non-bank) arm of
`SS_OP_INV_MOVEITEM_CERT` / `_UNCERT` in `mock230_ops_inv.c` did the plain
move and left the form alone — a documented limit, on the grounds that nothing
outside the bank used the cert forms. The shop does. It now converts through
the same `noted_id` / `cert_id` link `oc_uncert` / `oc_cert` read, which is
identity for an obj with no other form, so every equip, unequip and non-noted
move is unchanged.

### 9.4 What now covers it

`mock230 selftest: selling to a shop` (§6's stanza, `mock230_world.c`), seven
cases driven as real `IF_BUTTON2` packets on `shopside:items` after a real
`~openshop`:

| case | expected |
|---|---|
| general store, a pot it stocks | shelf +1 |
| general store, an item it does not stock (`allstock`) | shelf +1, paid |
| axe shop, an axe it stocks | shelf +1, paid |
| axe shop, a sword it does not stock | refused, item kept, **paid nothing** |
| any shop, coins | refused |
| any shop, an untradeable obj (found in the cache, not written down) | refused |
| general store, a **noted** pot | shelf +1 as a real pot |

Each case also asserts the buy grid was repainted — `UPDATE_INV_*` naming the
shop inv and `shopmain:items` — inside a capture that spans the click *and* the
tick, because a shared row flushes from `phase_clients_out` and not at the
moment of the write. "The shop's stock changed" and "the player was told" are
two facts and only the second is the one in front of the player. Negative
control: dropping the tick from the capture turns all three positive repaint
checks red.

### 9.5 Still open

* §8.3 item 4 stands — none of this is a connected-client playtest, and the
  sell ladder's op indices are still ours rather than the cache's.
* Two shop invs (2069, 2070) have a definition and neither a cache size nor a
  `size=`, so they are parsed and not seeded. Pre-existing, §8.5.
