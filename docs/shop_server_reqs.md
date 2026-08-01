# `shopmain`/`shopside` (300/301): what the server owes

> Companion to `docs/questlist_chatmenu_levelup.md` and
> `docs/friends_pm_chat_server_reqs.md`, same discovery pass
> (`docs/PORTING_GUIDE.md` §5.3), for the interface `docs/PORTING_GUIDE.md`'s
> phase plan names right after drop tables: *"shops (wants the shop dbtable
> pattern)."* There is no shop dbtable pattern in play here — see §4 — that
> phrase turned out to describe a different, unrelated reward-shop family.

## 0. Status up front

**Fully greenfield**, and unlike bank there is no existing engine scaffolding
to lean on beyond the generic two-panel open path:
`grep -rniE "\bshop" src/net/mock/*.c src/net/mock/*.h` returns exactly one
hit — a comment (`mock230_scripts.c:4579`, *"the two-panel open a bank (or a
shop, or a trade) is"*) — and no `mock230_shop.{c,h}` exists, no
`iface_shopmain`/`iface_shopside` id anywhere in `mock230_ids.h`.

---

## 1. `shopmain` (300) — the call graph

```
shop_main_init [clientscript 1074](inv $inv0, int $int1, int $int2, int $int3, string $string0)
  cc_deleteall(interface_300:1 / :16 / :18)
  ~steelborder(19660801, $string0, 0)                  cosmetic title bar; $string0 = shop title
  builds cells 1..inv_size($inv0) in interface_300:16 ("items")
    cc_setop(2,"Buy 1") (3,"Buy 5") (4,"Buy 10") [+(5,"Buy 50") iff $int3=1]
    cc_setop(10,"Examine")
  ~scrollbar_vertical(...)
  ~shop_main_update(19660816, $size5, $inv0, $int1, $int2)      -> script_1076
  ~shop_quantity(19660805,19660808,19660810,19660812,19660814,19660802,$int3)  -> script_2230
  if_setoninvtransmit("shop_main_update(...){$inv0}", interface_300:16)
  if_setonvartransmit("shop_quantity(...){var1022}", interface_300:2)
  if (~script2288 = 1) { ~script2461(19660817, 0) }             note_button, see §1.3
```
(`OSRS-Content/osrs239-content/scripts/script_1074.cs2`, read in full and
reproduced above verbatim modulo whitespace.)

`shop_main_update` (`script_1076.cs2`) walks `$int1` slots of the container,
`inv_getobj`/`inv_getnum` per slot, `cc_setobject`, and sets the per-row op
text:

| op | meaning | fixed/dynamic |
|---|---|---|
| 1 | `%varbit6348`-driven: "Value" (mode 0) or "Buy N" (mode 1-4) via `script_2228` | dynamic |
| 2/3/4 | "Buy 1"/"Buy 5"/"Buy 10" | fixed |
| 5 | "Buy 50" | fixed, only when `shop_main_init`'s `$int3 = 1` |
| 6 | "Value" — the demoted twin of op1 when op1 is showing a Buy mode | dynamic |
| 9 | "Buy `<remembered custom qty>`" | only on the one obj matching a remembered `$obj3`/`$int4` |
| 10 | "Examine" | fixed |

**`shop_main_update` never calls `oc_cost`, `db_getfield`, or `db_find`** —
confirmed by grep across `script_1074/1075/1076/2228/2229/2230/2231`. It only
paints item id/count off the container; price display and computation are
entirely server-side (§4).

**Auto-repaint, not a one-shot draw.** `if_setoninvtransmit(...){$inv0}`
(`script_1074.cs2`) means the client re-runs `shop_main_update` on its own
whenever the shop's container changes on the wire — no server round-trip
needed after the initial open, same idiom bank uses for its own containers.

### 1.1 The quantity-mode bar is one selector, not five buttons

`value`/`quantity1`/`quantity5`/`quantity10`/`quantity50` are a
mutually-exclusive mode selector. Clicking one writes **`%varbit6348`**
(named `shop_quantity`, `configs/all.varbit.compack:6349`) and re-arms the
other four buttons (`script_2230.cs2`, `[proc,shop_quantity]`, one
`switch_int(%varbit6348)` case per mode).

**`%varbit6348` packs into varp 1022, named `bank_closing`**
(`configs/all.varp.compack:1065`) — confirmed directly. This is shared state:
`script_3637.cs2` (an unrelated named-obj/enum grid, not interface 300/301)
reads and writes the same varbit through the same helper chain
(`script_3638`/`3640`/`3642`). Whatever that other panel is, it and shopmain
share one client-side "last quantity mode" preference, packed into a varp
named after bank's closing state — a genuine naming collision worth knowing
before writing the `.varbit` overlay, since a naive read-modify-write on
`bank_closing` for shop purposes must not clobber bank's own use of the same
varp's other bit ranges.

`pc_graphic` (graphic 1090, sprite `options_icons,28`) is **not** a
members/F2P indicator (that was a working assumption going in, and the
corpus contradicts it) — it's the icon half of the compound Value-check
button, the same icon `script_3642.cs2` draws for the "value" mode slot in
the sibling selector.

### 1.2 The note_button gate — flag before porting, don't trust as-is

`~script2461(19660817, 0)` (builds the note-toggle — a `scrap_paper` model,
`cc_setop(1,"Toggle")`, tooltip "Noted items") only runs if `~script2288 = 1`,
and `script_2288.cs2` (formally `[clientscript,bondif_redeem_fadeend]`)
returns 1 only when a Leagues relic (`league_relic_active(1131)` or
`(4716)`) is active. Taken literally this gates the noted-buy toggle behind
two Leagues relics — reads like ids reused across features rather than a
purpose-built gate. Same caveat class as `docs/questlist_chatmenu_levelup.md`
§1.3: **re-verify against a fresh decompile before treating this as
load-bearing**, don't port it blind.

---

## 2. `shopside` (301) — a corpus gap, not a mock230 gap

`grep -rl "shopmain\|shopside\|interface_300\|interface_301"` across all
9,368+ decompiled `.cs2` files returns **only `script_1074.cs2` and
`script_1076.cs2`** — nothing in this corpus populates `interface_301:0` or
binds its ops. Same gap class as the missing `~questlist_draw`/
`~chatbox_multi_addoption` bodies (`docs/questlist_chatmenu_levelup.md`
§1.3/§2.1): the live cache has this script, this decompile pass didn't
capture it.

The closest confirmed analogue is **`bankside_init`** (`script_294.cs2`) and
its cell builder `bankside_build` (`script_296.cs2`): always draws the
player's own built-in `inv` (93, no inv-id argument — the side panel's
source is fixed), `cc_setdragdeadzone`/`_deadtime` per cell for drag
interaction, and the same `if_setoninvtransmit`/`if_setonvartransmit`
auto-repaint idiom shopmain uses. **If** shopside follows the same shape —
unconfirmed, re-decompile before relying on it — its population needs no
server-supplied container id, only auto-repaint on the player's own
inventory changing.

---

## 3. Buy and sell mechanics

**Buying (confirmed):** a plain `IF_BUTTON<n>` lands on `interface_300:16`
with sub-id = slot. Op index alone is ambiguous in a way that's worse than
bank's withdraw ladder (`docs/mock230_bank.md` §4): op1's *meaning itself*
depends on client state (`%varbit6348`), so the server must track the same
quantity mode to interpret a click on op1/op6 correctly — it cannot infer
"Buy 1" vs "Value" from the op index alone.

The custom "Buy X" round-trip (op9) — how a player sets a custom amount, how
it threads back into `shop_main_init`'s `$int1`/`$int2` or `shop_main_update`'s
`$obj3`/`$int4` — **is not shown in this corpus.** Bank's own custom
withdraw uses `P_COUNTDIALOG` (opcode 128, `docs/mock230_bank.md` §5); it's a
reasonable but unconfirmed inference that shop reuses the same prompt.

**Selling (not confirmed in this corpus):** no `.cs2` here shows a sell
handler for shopside. LostCity's precedent (§6) binds fixed
Sell-1/5/10 ops directly on the side panel's `inv` widget; osrs239's own
quantity-mode bar suggests the rev-239 shopside ops may instead be dynamic
like shopmain's — that's inference, not a decompiled fact.

---

## 4. The container question — the load-bearing answer

**A shop's stock is a live, server-maintained container transmitted like a
bank tab, not a static dbtable the client reads once.**

- `shop_main_init`'s first argument is `inv $inv0` — a real container
  reference, not an id-lookup key.
- `if_setoninvtransmit(...){$inv0}` is the identical auto-repaint idiom bank
  uses for its own containers.
- Each named shop is its **own** container definition in the cache:
  `OSRS-Content/osrs239-content/configs/all.inv` has 411 `shop`-matching
  blocks (confirmed count) — `axeshop`, `generalshop1`, `swordshop`, etc,
  each `[name]/size=N` — matching LostCity's per-shop `.inv` model (§6)
  rather than one shared "shop stock" table.
- The `omnishop_shop_data`/`omnishop_stock_data` dbtable
  (`configs/all.dbtable`, `docs/DBTABLES.md` rows 39-42) is **not** read by
  any of `shopmain`'s CS2 — grepping `script_1074/1075/1076/2228/2229/2230/2231`
  for `db_getfield`/`db_find`/`oc_cost` returns nothing, and
  `docs/DBTABLES.md`'s own interface-closure column lists `—` for it. The
  only CS2 consumers of "omnishop" (`script_7257`/`script_7258`, called from
  `script_3165`/`script_3166`) belong to separate point-currency reward-shop
  interfaces (Barbarian Assault-style, Bounty Hunter store, Giants'
  Foundry) — **not** `shopmain`/`shopside`. Read `docs/PORTING_GUIDE.md`'s
  "wants the shop dbtable pattern" phrase as referring to that other family,
  not this one.

So: no dbtable/config populates the grid. Base prices are computed
server-side from the item's own cost combined with server-tracked
buy/sell/haggle state, and pushed to the client only as container contents
(id + count) — the client never sees a price until a Value/Buy click gets a
chat-message reply (LostCity precedent, §6). There is no price field
anywhere in the interface or the container.

---

## 5. Server obligations

| what | why | mock230 status |
|---|---|---|
| A per-shop stock container (`inv`), ~411 named shops, transmitted like a bank tab | `shop_main_init`'s `$inv0` + `if_setoninvtransmit{$inv0}` require a live, mutable container the server owns and syncs on buy/sell/restock | **not implemented** — no `mock230_shop.c`, no shop container ids anywhere |
| `if_openmain_side(shopmain, shopside)` wiring, per-shop | Generic `SS_OP_IF_OPENMAIN_SIDE` already falls through correctly for any non-bank pair (`mock230_scripts.c:4579-4610`, confirmed) | **infrastructure exists**, unused for shops — needs a shop-specific open that also pushes `$inv0`/`$int1-3`/`$string0` |
| A per-shop base-price/cost source + buy/sell/haggle multiplier config | `shop_main_update` never reads price data — must come from server content | **not declared** — no `.varp`/param overlay exists for shop multipliers |
| Buy-op handler keyed on (op index, `%varbit6348` mode, slot) | op1/op6 change meaning with the client's quantity-mode varbit; index alone is ambiguous | **not implemented** |
| Custom "Buy X" prompt + remembered-obj threading | op9 and `shop_main_init`'s `$int1`/`$int2` need session state (which obj, what amount) | **not implemented**; likely reuses `P_COUNTDIALOG` (already built for bank) |
| Sell-op handler on shopside's `items` grid | Corpus gap (§2, §3) — shopside's own script isn't in this decompile; re-decompile before scoping precisely | **not implemented**, exact op shape unconfirmed |
| A `.varp` overlay declaring `bank_closing` (backs `shop_quantity`) `transmit=yes` | Without it the quantity-mode selector resets every session; any write must be read-modify-write since the varp is shared with bank's own bit ranges | **not declared** anywhere — `server/scripts/interface_bank/configs/bank.varp` doesn't mention it |
| Restock-over-time mechanism | LostCity ticks stock back toward a baseline every world tick (§6) | **not implemented** — no restock concept exists in mock230 at all |
| `[opnpc]` triggers that call an actual `openshop` | Currently stubbed on purpose: `generalshopkeeper1`'s op3 just says "I've nothing to trade just now" (`areas/lumbridge/scripts/tutors.rs2`); `bob.rs2` states outright the shop container/`oc_cost`/`oc_param` machinery doesn't exist yet | **stubbed, acknowledged in-content** |

---

## 6. LostCity precedent (`content/scripts/shop/`)

- **Interfaces**: `interfaces/shop_template.if` (generic per-row `com_N`
  graphic backings — "the server paints" idiom, contrasted with rev-230's
  CS2-painted grid) and `interfaces/shop_template_side.if` — one `type=inv`
  widget with **fixed** `option1=Value, option2=Sell 1, option3=Sell 5,
  option4=Sell 10`.
- **Session state**: temp varps `%shop` (inv), `%shop_buy`, `%shop_sell`,
  `%shop_haggle` (`configs/shop.varp`, `scope=temp`) set by
  `[proc,openshop]`/`[proc,openshop_activenpc]` from NPC params
  (`configs/shopkeeper.param`: `owned_shop`, `shop_buy_multiplier` default
  60, `shop_sell_multiplier` default 100, `shop_delta` default 10,
  `shop_title`).
- **Opening**: `inv_transmit(inv, shop_template_side:inv)` /
  `inv_transmit($shop, shop_template:inv)` then
  `if_openmain_side(shop_template, shop_template_side)` — the exact
  container-transmit idiom §4 found in osrs239's own `if_setoninvtransmit`.
- **Pricing**: `[proc,adjusted_item_cost_buying]`/`_selling` compute from
  `oc_cost($obj)`, a haggle-scaled delta, and `~price_mod`, which looks up
  the shop's own stocked base count (`inv_stockbase`) so price moves with
  current stock level — buying drives price up, selling drives it down.
- **Buy/sell ladder**: fixed `inv_button1..4` on both panels, routing through
  `[label,shop_request]` into `[label,buy_item]`/`[label,sell_item]`, which do
  space/afford checks then `inv_moveitem_uncert` between shop and player
  `inv`, plus coins add/remove.
- **Restock is engine-level, not content**: `engine/src/engine/World.ts`
  walks every `.inv` flagged `restock=yes` on every world tick and nudges
  each stocked slot's count toward its configured baseline
  (`stockN=obj,count,restockrate` in the `.inv` file — e.g. `restock=yes`,
  `stock1=horsey_brown,5,20` = target count 5, tick-rate 20); `allstock=yes`
  general stores with no baseline decay any non-baseline item back down once
  a minute (`INV_STOCKRATE = 100` ticks). **The config (`restock=yes`,
  `stockN=...`) is content-shaped; the tick-walk is engine**, by LostCity's
  own precedent.
- **Naming correspondence**: LostCity `shop_template`/`shop_template_side` ⇔
  osrs239 `shopmain`(300)/`shopside`(301); LostCity's fixed 4-op ladder per
  panel ⇔ osrs239's dynamic `%varbit6348`-driven op1/op6 plus fixed
  op2-5/9/10; LostCity's `%shop`/`%shop_buy`/`%shop_sell`/`%shop_haggle` temp
  varps have no declared analogue yet in osrs239's tree.

---

## 7. What this doc does not cover

- Shopside's exact population/op-binding script — missing from this
  decompile corpus (§2, §3); re-decompile the live cache before trusting the
  `bankside_init` analogy.
- The custom "Buy X" count-prompt wiring — inferred from bank's
  `P_COUNTDIALOG` precedent, not confirmed against shop's own CS2.
- Whether `%varbit6348`'s sibling consumer (`script_3637.cs2`'s named-obj/enum
  grid) is itself an osrs239 shop variant worth tracing alongside this port —
  flagged only because it shares the varbit, not otherwise investigated.
- Full price-formula parity (haggle curve, stock-based price movement) —
  LostCity's formula is stated in §6 but not re-derived against osrs239 cache
  constants; treat as a starting point, not a spec.
