# Grand Exchange (`ge_offers` 465 + family): what the server owes

> **Blockers A + B, 2026-08-02 — B cleared, A cleared *in kind but not in
> size*.** B: the registry resolves all 17 per-player GE containers
> (`enum_150` maps slots 0-7 onto `tradingpost_sell_0..5` + `ge_collect_6/7`,
> size 2 each) with no new C. A: `ge_history_addline` (6 ints) sends today;
> **`ge_pricechecker_prices` wants 28 and three caps say 16 / 16 / 20**
> (`runclientscript.md` §8) — capacity, not mechanism. **Still blocked on:
> everything shop needs** (`fields/inv.ini`, world persistence), **plus CS2
> host ops 3903-3913 with 3909 a genuine hole, plus a world-wide matching
> engine that has no reference anywhere** — LostCity's rev 254 is 2004 and the
> GE is 2007. Correctly last.

> **UPDATE 2026-08-02 (lane-blockers): the container blocker is cleared.**
> `container_for` is a registry ([`mock230_containers.md`](mock230_containers.md));
> all the per-player GE containers resolve and transmit with no new C. Nothing
> else in this doc changed — the matching engine still has no reference
> anywhere, and `ge_pricechecker_prices` still needs 28 ints through
> `runclientscript`, which is capped at 16 by the compiler and 20 by the client
> wire parser (`docs/runclientscript.md` §8).

> **NOT BUILT — triaged 2026-08-02: BLOCKED, and correctly last.** It needs
> everything shop needs (a world-scoped container registry, an inv field
> register) and unlocks nothing else in the survey. Four claims to fix before
> anyone sizes work against it: (1) §1.1's "nine host ops, 3903-3913" is
> **ten** — 3903-3908 + 3910-3913 — and **3909 is a genuine hole**, `_unknown`
> in both `cs2_command.gen.h` and `cs2_opcode_meta.c`; the doc says "nine"
> three times. (2) §7's "every offer slot renders as empty today" is
> **backwards**: `script_798.cs2:26` branches on
> `stockmarket_isofferempty($slot) = true` and StackMetaStub pushes 0 =
> *false*, so every slot takes the active-offer branch — 8 garbage all-zero
> offers, not 8 empty slots. (3) §7's "every GE screen's auto-repaint is a
> no-op" is overstated: `script_803.cs2:16-17` wires the panel switch to
> `if_setonvartransmit{var375}` **as well as** the dead
> `if_setonstocktransmit`, and vartransmit works — panel switching is live,
> only offer-progress repaint is dead. (4) §4's "RUNCLIENTSCRIPT (opcode
> 11002, already generically landed)" was false when written and is true now:
> 11002 is fixed at one int and two strings, and the general form
> `SS_OP_RUNCLIENTSCRIPTVARARG` (11003) landed 2026-08-01. **§5.2 also
> contradicts §1.2/§8**: it calls the `tradingpost_sell_0..5` inv blocks
> orphaned and says "do not build against it", while §8 requires those exact
> blocks — `enum_150` maps slots 0-5 onto them and 6-7 onto `ge_collect_6/7`.
> §8 is right; the scripts reach them through the enum, not by name. §1.2's
> varp-collision finding is verified correct, and the mechanism is
> `script_5732.cs2`: slot 0→`%var3200`, 1→`%var3201`, 2→`%var297`, 3→`%var915`,
> 4→`%var914`, 5→`%var295`, 6→`%var3202`, 7→`%var3203` — whole varps, not
> varbits.

> Companion to `docs/shop_server_reqs.md`, same discovery pass. **The biggest
> feature in this entire survey series** — not one mechanism like bank/shop,
> but three different data-delivery idioms layered into one interface
> family, plus a world-wide matching engine with zero existing precedent
> anywhere (LostCity's rev 254 snapshot is 2004; the real GE launched in
> 2007 — a hard chronological impossibility, not a missed grep).

## 0. Status up front

**Fully greenfield on the server** (confirmed zero hits for
`grand.?exchange|ge_offer|ge_slot|stockmarket|ge_collect` anywhere in
`src/net/mock/`, `src/game/`, `docs/`) — no `mock230_ge.{c,h}`, no ids, no
prior doc. **And the client isn't fully ready either**, independent of any
server work: the redraw-trigger opcode and all 9 query ops the client's own
CS2 depends on are parsed but not modeled (§5). Two separate pieces of work,
not one.

---

## 1. `ge_offers` (465) — one interface, three panels, three mechanisms

34 components: a persistent 8-cell **index** grid plus three mutually
exclusive panels (**index** / **setup** / **details**) selected by
`%varbit4439` (`ge_selectedslot`, confirmed) — 0 = nothing selected, N+1 =
slot N being viewed/edited.

### 1.1 An active offer's state comes from 9 host ops, not any container

`script_798.cs2` (the cell paint routine) reads an active offer entirely
through nine CS2 host ops, all slot-parameterized, opcodes 3903-3913
(confirmed present in `3rd/rscache/src/cs2/cs2_command.gen.h:687-696`):
`stockmarket_getoffertype/item/price/count/completedcount/completedgold`,
`isofferempty/isofferstable/isofferfinished/isofferadding`. **No container,
dbtable, or push packet drives the live progress number** — it's a pull, on
the client's own redraw cadence.

### 1.2 The empty-slot fallback — a real varp-per-slot scar

When a slot has no live offer, the cell instead shows a **2-item container**
(one item + coins/notes to collect, resolved per-slot via `enum_150` →
`inv_518..523,539,540`, all 8 confirmed real inv blocks). Two more lookups
back this: `ge_itemsink_obj_N`/`ge_itemsink_price_N` (confirmed, 16 clean
dedicated varps, one pair per slot), and a second per-slot flag that is
**genuinely irregular**: slots 0/1/6/7 get dedicated `ge_tax_slot_N` varps
(confirmed), but **slots 2-5 are packed into four pre-existing, unrelated
music-player varps** — `musiclength`, `last_musiclength`, `last_song`,
`currentsong` (all four confirmed by name in `all.varp.compack`). This is a
real collision, not a misread — same class as shop's `bank_closing`
overlap (`docs/shop_server_reqs.md` §1.1), except asymmetric across slots,
which is itself evidence the varp allocation grew unevenly over time. **A
server overlay must not treat these four ids as free scratch space.**

`ge_transmit_taxrate` (varbit 13139, confirmed) feeds the fee estimate — the
real 2% GE tax mechanic (a 2023-era addition) is present in this cache
snapshot, corroborated by a developer economy-monitor interface
(`ge_itemsink_monitor`, §4).

### 1.3 Creating an offer — four dedicated vars, no dedicated submit op

The setup panel's state is `tradingpost_search` (varp 1151, chosen item —
its name is direct cache-level evidence "Trading Post" was this feature's
pre-launch codename, corroborated by an orphaned op family, §5.2),
`ge_newoffer_quantity`/`_type`/`_price` (varbits 4396/4397/4398, all
confirmed by name), and `ge_last_offer_item` (varp 4725, "Repeat Offer").
Quantity buttons (+1/+5/+1K/All) mutate the varbit directly, client-only.

**The Confirm button's `onop` is bound only to a click sound in this
corpus** — no script anywhere binds a real submit handler. Either the
server interprets a plain click using the already-visible varbits (same
idiom as shop's buy ladder), or the real wiring lives in
`ge_offers_setup_init`, whose body is missing from this decompile (§6).

**Ironman gating exists and is real**: an empty slot shows a simplified
bond-only layout gated behind `%varbit1777` (`ironman`, confirmed) —
matching real OSRS's rule that ironman GE access is bond-only.

### 1.4 Abort/Modify/Collect

Abort and Modify are plain op2/op3 clicks on the details-panel cell — no
dedicated CS2 op exists for either (confirmed, no `offer`/`exchange` match
beyond `stockmarket_*`/`tradingpost_*`). Collecting reuses the same 8
per-slot 2-item containers as the empty-slot fallback.

---

## 2. `ge_offers_side` (467) — the inventory panel

A standard 28-cell grid off the player's own built-in `inv` — same idiom as
bank's/shop's side panels. Clicking "Offer" on an item just re-runs slot
selection; no dedicated "start offer from this item" op exists. The actual
`tradingpost_search` write happens off-corpus.

## 3. `ge_collect` (402) — not a separate data source

Confirmed: this reads the **identical 8 per-player 2-item containers**
`ge_offers`'s details panel uses. It's a second view onto the same
server-owned state — the bank/shop "container transmitted like a bank tab"
idiom (`docs/shop_server_reqs.md` §4) — but only for post-completion payout,
never the live in-flight offer (which is host-op-only, §1.1).

## 4. `ge_history` (383) — a third, distinct mechanism: pure RUNCLIENTSCRIPT push

No component has an onload that populates rows. Population is
server-initiated: `ge_history_init` (clear + reset a **varc** row counter),
then `ge_history_addline(index, item, sold?, qty, total, tax)` **once per
historical trade**, then `ge_history_finish` (sizes the scrollbar, or shows
"no recorded trades"). **There is no client-pull mechanism for this screen
at all.** The server must persist a per-player trade ledger and push it
entirely via RUNCLIENTSCRIPT (opcode 11002, already generically landed per
`docs/friends_pm_chat_server_reqs.md` §2) — nothing here is inferable as a
container or dbtable.

## 5. Lower priority, briefly

- **`ge_pricechecker`** (464) — single-item guide-price search. Populated by
  a **third RUNCLIENTSCRIPT variant**: a batch push of 28 ints straight into
  28 varcints in one call (`ge_pricechecker_prices`), redrawn from those.
  Search-text mechanism itself is off-corpus.
- **`ge_pricelist`** (237) — the scrollable all-items guide-price list, via
  a genuine `inv`-typed container argument (`if_setoninvtransmit{$inv0}`) —
  a large synthetic container of (item, guide price) pairs, distinct from
  every other GE mechanism here.
- **`ge_itemsink_monitor`** (731) — a developer/GM economy dashboard
  ("Coffer Value", "Sunk Quota", "Time × Rates") corroborating the tax/sink
  mechanic above. Not player-facing.

## 5.2 The orphaned `tradingpost_*` op family

Opcodes 3914-3926 (`sortby_name/price/age/count`, `gettotaloffers`,
`getofferworld`, etc.) plus `tradingpost_sell_0..5`/`tradingpost_display`
inv blocks — **confirmed zero decompiled scripts reference any of them**.
This looks like a pre-launch, per-world offer-browser screen superseded
before `ge_offers`/`ge_history` shipped, or a screen this decompile pass
didn't capture. **Flag as corpus gap — do not build against it.**

## 6. Corpus gaps — flagged, not guessed

- `ge_offers_setup_init` — called, body missing. Likely owns the item
  picker/search wiring for a *new* offer.
- The item-search-to-`tradingpost_search` write — read everywhere, written
  nowhere in this corpus.
- The custom quantity/price textbox tap — the button ladder is confirmed;
  the plain numeric entry isn't shown (probably `P_COUNTDIALOG` by analogy
  to bank/shop, unconfirmed).
- The Confirm-button real submit path (§1.3).
- `ge_pricechecker_side`, `ge_viewonly`, `isofferstable`/`isofferadding` —
  referenced but no populating script found.

## 7. Client-side gaps (independent of server work)

- **`IF_SETONSTOCKTRANSMIT`/`CC_SETONSTOCKTRANSMIT`** (opcodes 2425/1425,
  confirmed present) route to the documented discard-stub
  (`CS2VM2_Op_IF_SetOnEventDiscard`/`_CC_...`) — parsed so later opcodes
  don't desync, but **no listener is ever stored and no redraw ever fires**.
  Every GE screen's auto-repaint is a no-op on this client today, regardless
  of what the server sends.
- **All 9 `stockmarket_*` ops fall to `StackMetaStub`** — confirmed zero
  `case` for any of them in `cs2vm2.c`'s dispatch. Because their arity is
  statically known, the stub silently pushes `0` for every query rather than
  asserting — **every offer slot renders as "empty" today, with nothing
  flagging it.** This needs real `CS2VM_HOST_REQUEST_*` cases wired to
  `rs_cs2_host.c`, the same shape inv/var transmit already use.

## 8. Server obligations

| what | why | mock230 status |
|---|---|---|
| 8-slot per-player offer state machine, exposed via the 9 `stockmarket_*` answers | §1.1 — nothing else supplies this | **not implemented**, no scaffolding |
| **A world-wide matching engine** — continuously matching buy/sell across every active offer, all players, at compatible prices, advancing `completedcount`/`completedgold` | Implied by "fulfilled" progressing independently of "wanted" — **the single largest piece of unwritten logic in this whole survey series** | **not implemented** |
| 8 dedicated per-player 2-item collect containers (`inv_518..523,539,540`) | §1.2, §3 | **not implemented** — no ids allocated |
| 16 itemsink varps + 4 dedicated + 4 *reused* tax-slot varps | §1.2 — the reused four must be read-modify-write, never blind-overwritten | **not declared** |
| 4 dedicated varbits (`ge_selectedslot`, `ge_newoffer_quantity/type/price`) + 2 varps (`tradingpost_search`, `ge_last_offer_item`) | session/build state the server must also read to interpret bare clicks | **not declared** |
| GE tax/item-sink mechanic (`ge_transmit_taxrate`) | fee estimate + history's net-proceeds column | **not implemented** |
| Per-player trade-history ledger, pushed via RUNCLIENTSCRIPT | §4 — no pull path exists for this screen | **not implemented** — no persistence, no plumbing |
| Create-offer submit path | server must accept a plain click and read the already-visible new-offer vars | **not implemented**; exact client trigger partly unconfirmed |
| Abort/Modify/Item-picker | same "op + component, server infers meaning" idiom as shop | **not implemented** |
| `IF_SETONSTOCKTRANSMIT` real listener model | §7 | **client bug**, not server — flag alongside the XP-drops class (`docs/PORTING_GUIDE.md` §5.2) |
| `stockmarket_*` real host-op answers | §7 | **client gap** |
| Guide-price table + update cadence (`ge_pricechecker`/`ge_pricelist`) | §5 | **not implemented**, likely wants its own content-side price-history record |

## 9. LostCity precedent — confirmed absent, and why that's not a gap in the research

Zero hits anywhere in `LostCity_Server` for "grand exchange," no `content/scripts/ge/` directory, nothing in the engine. **This is a hard chronological impossibility, not a missed grep**: LostCity's reference is rev 254 (September 2004); the real Grand Exchange launched 21 November 2007, over three years later. Every mechanic here — matching engine, 8-slot machine, collect box, trade history, and especially the 2023-era tax layer — has to be designed from the rev-230/239 client's own CS2 surface and cache layout. There is no `.rs2` proc to port, only client behavior to reverse and a server to write from scratch, per `docs/PORTING_GUIDE.md` §5.1's discovery procedure.

## 10. What this doc does not cover

- The matching-engine algorithm itself (price-time priority, partial fills)
  — pure server design, no client evidence bears on it.
- `ge_offers_setup_init`'s body, the item-picker, and the Confirm-click's
  real wire shape — re-decompile before scoping precisely.
- `ge_pricechecker`'s search mechanism and `ge_pricechecker_side` —
  unconfirmed, lower priority.
- The orphaned `tradingpost_*` family — may be dead pre-launch scaffolding
  or an uncaptured screen; not investigated further.
- Wire-level packet shape for any server→client "stock transmit" — no
  packet was identified; the client-side hook is unmodeled regardless.
