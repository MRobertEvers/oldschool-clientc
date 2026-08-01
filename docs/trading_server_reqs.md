# Player Trading (`trademain` 335, `tradeside` 336, `tradeconfirm` 334): what the server owes

> Same discovery pass as `docs/shop_server_reqs.md`. Unlike GE/collection
> log, this is 2004-era core content — LostCity has a full, well-commented
> reference (`content/scripts/interface_trade/`). **This is also the first
> genuinely cross-player economic interaction traced in this series**, and
> it needs a real primitive nothing else here has required: reading and
> mutating *another player's* container from inside a script context.

## 0. Status up front

**Fully greenfield, and more so than shop.** No `mock230_trade.{c,h}`, no
`tradeoffer` container, no dispatch for the player-vs-player trigger that
opens a trade at all. The CS2 op table already reserves the exact ops
LostCity's engine needs (`invother_getobj/getnum`, `SS_OP_INVOTHER_TRANSMIT`)
— declared, zero host implementation anywhere.

## 1. `trademain` (335) — the offer grids

`trade_main_init` (`script_755.cs2`) binds two grids to **`inv_90`**
(confirmed cache name `tradeoffer`, size 28): "your offer" reads
`inv_getobj(inv_90, slot)` with a full Remove ladder; "other player's
offer" reads **`invother_getobj(inv_90, slot)`** — confirmed opcodes 3313/
3314, `CS2_HANDLER_HOST`, zero dispatch anywhere in `src/cs2vm2`/`src/game`
— with `Examine` only, no remove ops. This is the actual novel mechanism:
not the shop/bank container-transmit idiom alone, but that idiom **plus a
cross-player read of the same numbered container**.

Per-item value is a separate side channel: the server calls
`script_1216`/`_1217` directly with 28 raw ints each (your/their per-slot
value) — the same "server calls a named clientscript with raw values"
pattern already seen for XP drops.

### 1.1 "Check offer!" — the accept-delay anti-scam mechanic

`%varbit13141` (confirmed `trade_accept_delay`, packed into varp 1042
confirmed `traderemoved`) is **read-only everywhere in the corpus** —
server-set, never written by CS2. While it's nonzero, the Accept button
shows a countdown instead of "Accept." **Nothing in this corpus intercepts
the actual click during the countdown** — the server alone must reject or
queue an accept click that arrives before the delay elapses. The other two
bits of the same varp (`trade_this_player_removed`/`trade_other_player_removed`)
drive a red "Trade modified" label — also server-only state, confirmed no
CS2 anywhere writes to any of these three bits.

## 2. `tradeside` (336) — corpus gap, same class as shopside

A single bare `side_layer` component, no onload, no ops. Confirmed nothing
in the corpus populates it. LostCity's own `tradeside.if` (read in full,
§6) has the identical shape — a single `type=inv` widget with a fixed
Offer-1/5/10/All/X ladder — strongly implying osrs239's version is the
same, just not captured by this decompile pass.

## 3. `tradeconfirm` (334) — the second screen and the flash

`trade_confirm_redraw` (`script_768.cs2`) builds a text list (not icons) of
both offers, and for any *other-side* slot that recently emptied
(`script148(slot) > clientclock - 750`), draws a **flashing red rectangle**
fading over ~750ms — the literal client implementation of the classic
"item pulled after you saw the offer" scam warning. The timestamp array
(`%varcint12..39`) is stamped by `script_147.cs2`, called as
`~script147(slot)` from `trade_slot_changed` (`script_765.cs2`) — a
clientscript with **no CS2 caller anywhere in the corpus**, meaning the
engine/server invokes it directly the instant it detects the other
player's `tradeoffer` container changed at that slot. **This cannot be
inferred from the container transmit alone — the server must diff the
other player's container tick-to-tick and fire this per changed slot.**

The disclaimer text ("There is NO WAY to reverse a trade...") is baked
into the interface, not server-driven.

### 3.1 Value-mismatch warning — searched for, not found, and LostCity doesn't have one either

Exhaustive grep across every script for value-disparity-warning phrasing:
zero real hits. LostCity's own confirm screen (§6) is unconditionally "Are
you sure you want to make this trade?" — no value-comparison branch
anywhere in the file. **Flag this as an unconfirmed/likely-nonexistent
mechanic**, not something to build without a fresh confirmed source.

## 4. Server obligations

| what | mock230 status |
|---|---|
| A `tradeoffer` container (id 90, 28 slots) per player, transmitted like a bank tab | **not implemented** — `container_for()` recognizes only backpack/worn/bank |
| Cross-player container read (`invother_getobj/getnum`) | **CS2 host ops declared, zero dispatch** |
| `SS_OP_INVOTHER_TRANSMIT` (opcode 4332, declared) | **zero implementation** — the sibling `SS_OP_INV_TRANSMIT` (4331) *is* implemented but only against `active_player`; no second-player parameter exists anywhere in that path |
| A pairing/session concept ("who is my trade partner") | **does not exist** — `mock230.h`'s own doc comment confirms `active_player` means "whose turn it is," not a pair; no second pointer analogous to LostCity's `_activePlayer2` |
| Cross-player atomic item transfer on final confirm | **not implemented** — no equivalent primitive anywhere |
| The player-vs-player click that opens the whole flow | **trigger constants exist (`SS_TRIGGER_OPPLAYER1..U`), zero dispatch** anywhere in `mock230_scripts.c`/`mock230_world.c` outside unrelated NPC-approaches-player triggers |
| Accept-delay countdown | **not implemented** — no analogue timer, and no LostCity precedent either (their accept flow is a plain boolean handshake) |
| Per-slot "other player removed this" diff → flash trigger | **not implemented** — server-side diffing logic with no client-computable fallback |
| Two-screen open, symmetric on both connections | infrastructure partially reusable (`if_openmain_side`), unused for trade — needs a trade-specific open firing on both connections at once |
| Item-space/tradeability checks before accept | **not implemented**, no analogue anywhere |
| Value-mismatch warning | **not applicable** — no evidence it exists in this client or in LostCity's reference |

## 5. Landed vs. gap — the multiplayer head start, precisely scoped

The first cross-player economic interaction in this series, so worth being
exact about what existing multiplayer work does and doesn't buy:

**Genuinely helps**: a real player pool (`players[MOCK230_PLAYER_MAX]`),
with `active_player` explicitly documented as "whose turn it is" rather
than a singleton — confirmed, the header comment already states this
design intent. Per-player entity streams are proven cross-player
(`embed_test.c` decodes Alice's movement out of Bob's own client reader).

**Does not carry over — new plumbing needed, not reuse**: `container_for()`
resolves containers only off `active_player` — there's no "look up player
P's container" from inside a script. Its own comment states the assumption
outright: *"There is one client here and the bindings are fixed"*. No
pairing/session object exists. The op-click trigger that starts a trade is
declared but never dispatched — the entry point does nothing today.

**Net**: the connection/presence layer has a head start; the container
layer and interaction-trigger layer for player-to-player content are both
greenfield — trade needs a container type *plus* a cross-player read/pub-
sub mechanism *plus* a pairing concept nothing else in mock230 has needed
yet, making this architecturally the most novel gap found in this series.

## 6. LostCity precedent — full and detailed

`content/scripts/interface_trade/` (confirmed present, read in full):

- Interfaces are **server-painted** `type=inv`/`type=invtext` widgets (same
  client-paints-vs-server-paints split already documented for shop).
  `tradeside.if` confirms a single `[inv]` widget with a fixed 5-op ladder
  — supporting the §2 inference for osrs239's equivalent.
- Session state: **`%tradepartner`** (the other party's uid) and
  **`%tradestatus`** (reset/received/pending_accept/accept/pending_confirm),
  both per-player. No numeric accept-delay analogue at all.
- **Opening**: `[opplayer4,_]` — first click sets the target's
  `%tradepartner`/`%tradestatus`; the target re-clicking op4 back detects
  the reciprocal state, and only then does one script execution, mirrored
  with `.`-prefixed calls, open both clients' interfaces simultaneously.
- **Accept flow, screen 1→2**: sets `pending_accept`; only proceeds once
  the *partner's* status is also `pending_accept`. Runs `enough_trade_space`
  in both directions before opening the confirm screen on both clients.
- **Screen 2→commit**: only once *both* sides hit `pending_confirm` does it
  call **`both_moveinv(tradeoffer, inv)` then `.both_moveinv(tradeoffer, inv)`**
  — the atomic cross-player swap is **one engine command pair, not
  client-visible logic**. `BOTH_MOVEINV` resolves both players via
  `_activePlayer`/`_activePlayer2` (swapped with the `.` prefix), moving one
  player's container into the other's in one engine operation — this is
  the exact mechanism that removes the "last-second swap" scam class.
- **The `invother_*` primitive itself**: both bottom out in
  `player.invListenOnCom(invType, com, uid)` — a per-player list of
  `(container type, component, source-uid)` bindings; the engine walks
  every player's binding list whenever *any* container mutates. **A general
  pub/sub mechanism, not trade-special-cased** — trade is just the one
  feature that calls it with `uid ≠ self`.
- **Anti-scam checks that do exist**: `oc_tradeable`, `oc_members` (blocks
  members items on F2P), and `enough_trade_space` in both directions. **No
  value-mismatch check anywhere** — confirms §3.1 is not a real mechanic.
- Naming correspondence: identical interface names on both sides; LostCity's
  boolean handshake is the same shape as osrs239's, plus an additional
  numeric `trade_accept_delay` layer the 2004-era reference doesn't have.

## 7. What this doc does not cover

- `tradeside`'s exact op-binding CS2 — missing from this decompile,
  inferred from LostCity's identical-shaped interface; re-decompile before
  trusting the analogy.
- The exact osrs239 trigger/packet path for opening a trade — only the
  trigger constants are known to exist server-side, undispatched.
- `tradeopponent`/removal-warning text population — inferred to be a raw
  server push, not directly confirmed for this client revision.
- Full accept-delay timing semantics — the varbit's value is read but never
  seen written in this corpus, and has no LostCity precedent to check
  against.
- Any wilderness-specific trade restriction — not searched for in this pass.
