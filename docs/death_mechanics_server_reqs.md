# Death mechanics (`deathkeep` 4, `gravestone_generic` 672, `death_coffer` 670): what the server owes

> **LANDED 2026-08-03 (preview + on-death + gravestone/coffer).** Engine:
> `oc_cost` / `oc_tradeable` / `oc_members`, `inv_dropall`, `obj_addall`;
> container registry already reached the death invs. Content:
> `interface_equipment/scripts/deathkeep.rs2` (worn-tab preview + scenario
> pausebuttons), `player/death.rs2` (`~player_death_lose_items` → gravestone),
> `player/scripts/gravestone.rs2` (timer, reclaim, coffer, office). Ranking and
> fees use high alch (`oc_cost * 60 / 100`). Deferred: `findhero` PvP floor-drop,
> full wildy PK-skull acquisition, incinerator drag-target, coffer quantity
> radios (numeric component names), Death NPC dialogue entry points (use
> `::gravestone` / `::coffer` / `::deathoffice`).

> Companion to `docs/questlist_chatmenu_levelup.md` and
> `docs/shop_server_reqs.md`, same discovery pass.

## 0. Status at a glance

| interface | id | status | headline finding |
|---|---|---|---|
| `deathkeep` | 4 | landed | worn-tab preview; parallel `deathkeep_items` + `diango_hols_sack` tags; `deathkeep_init` |
| `gravestone_generic` | 672 | landed | fill on death; softtimer resync; free/pay reclaim |
| `death_coffer` | 670 | landed | sacrifice → `%if1` balance; fee discount on pay reclaim |
| `death_office` (669) | 669 | landed (shallow) | reclaim from `death_permanent` after expiry |
| `gravestone_generic` | 672 | gap | timer is a server varbit resynced periodically, client-interpolated between pushes with a hard freeze cap |
| `death_coffer` | 670 | gap | sacrifice-for-fee-discount screen; item value is server-supplied, never computed client-side |
| `death_office` (669, adjacent) | 669 | gap, out of primary scope | Death's permanent post-expiry hold; shares varps with `death_coffer` |
| `death_coffer_side` (671, adjacent) | 671 | corpus gap | population script missing from this decompile |

---

## 1. `deathkeep` (4) — "Items Kept on Death"

`deathkeep_init` (`script_972.cs2`) delegates to `deathkeep_left_redraw`
(`script_974.cs2`), which walks two **parallel 50-slot containers**:
**`inv_584`** (confirmed name `deathkeep_items`) holding the real objs, and
**`inv_468`** (confirmed name `diango_hols_sack` — a coincidental name reuse,
same class of finding as `bank_closing`/`shop_quantity`,
`docs/shop_server_reqs.md` §1.1) holding a parallel array of placeholder
objs used purely as **per-slot category tags**. A `switch_obj` on each tag
routes the real item into one of 5 buckets: KEPT, GRAVESTONE (fee), coins,
LOST/DROPPED, or DELETED.

**The client never sorts or values anything**: zero `oc_cost` calls exist
anywhere in this call graph. The bucket assignment and the per-item fee (a
pre-multiplied int passed as an argument) both arrive pre-decided; the
client only formats and displays. Ironman fee-halving is a display-side
clamp of an already-server-decided number, not a client computation. A PvP
flag (arg 0) swaps "LOST to the player who kills you" vs "DROPPED near
where you die" wording, and the ironman varbit (`%varbit1777`, already known
undeclared per `docs/grand_exchange_server_reqs.md`) reroutes the whole
gravestone bucket to ground-drop framing for Ultimate Ironman.

## 2. `gravestone_generic` (672) — the grave itself

**Corpus gap, confirmed**: the interface's own onload names script 3462
directly, and **neither `script_3462.cs2` nor `script_3466.cs2` exist
anywhere in this decompile** (verified by presence-scanning the whole
3456-3490 id range). 3462 is almost certainly the interface's own init;
re-decompile before relying on either.

### 2.1 The timer — a hybrid, not a pure clock and not a pure live push

```
[proc,gravestone_hud_resynch]
if (%varbit10465 <= 0 | %varbit12139 = 1) { hide; return; }
def_int $int1 = calc(%varbit10465 * 30);
if_setontimer("gravestone_hud_tickdown(event_com, cc_getid, $int1, clientclock)", ...);
```
```
[clientscript,gravestone_hud_tickdown](component, comsubid, int base, int t0)
$int4 = ~min(calc(clientclock - t0), calc(3 * 30));
~gravestone_hud_write(calc(base - $int4));
```

**Varbit 10465 (confirmed `gravestone_duration`) is server ground truth,
resynced whenever the server retransmits it.** Between resynchs the client
interpolates off its own `clientclock`, but the interpolation window is
hard-capped at `3*30 = 90` client-clock units — **if the server goes longer
than that without a fresh push, the countdown freezes rather than running
away.** The server owes a periodic re-push shorter than that cap, not a
one-time timestamp the client free-runs from. `%varbit12139` (confirmed
`gravestone_tli_hide`) suppresses the HUD entirely (e.g. inside instances
where gravestones don't apply).

### 2.2 Reclaim mechanics — free tier, paid tier, incinerate; no timer-extension found

Grave contents live in **`inv_525`** (confirmed name `gravestone`, size
120). Three confirmed varbits (`gravestone_feethreshold_under100k/1m/10m`,
10472-10474) set free-vs-paid split thresholds and a
1000×/10000×/100000× fee multiplier by value rank. The pay panel's
aggregate fee and a **live Death's Coffer balance** are pushed together via
`gravestone_transmit_data(fee, coffer)` — the coffer text literally states
the mechanic: *"Death's Coffer: N coins. Discard items to reduce a fee."*

**Only three interactive ops exist in this corpus**: free-tier reclaim
("Take-All"), fee-gated reclaim ("Unlock"), and an incinerator drag-target
(same clickmask bank's own incinerator uses). **There is no "pay to extend
the timer" button anywhere in this corpus** — if that mechanic exists on
the live cache, it's a corpus gap, not confirmed present or absent.

## 3. `death_coffer` (670) — "Sacrifice items to Death's Coffer"

Fully present, no corpus gaps. Selection state is three generic scratch
varps (`%var262` slot, `%var263` quantity, `%var264` **server-supplied
per-unit value**) — `%var264 = 1` is a sentinel for an item the coffer
refuses. **Zero `oc_cost` calls anywhere** — value is pushed, never
computed client-side. Confirm is a plain `IF_BUTTON`; the server owns
destroying the stack and crediting the balance (`%var261`), entirely off
CS2 by design. The panel's own static text is the normative statement:
*"Items sacrificed here can NEVER be retrieved, and the money CANNOT be
withdrawn or used for any other purposes."* `%var261-264` are the
generically-named `if1-if4` compack rows — same reused-scratch-varp
collision class found throughout this series.

### 3.1 Adjacent, same feature family, not deep-traced

- **`death_office` (669)** — reached via dialogue with Death; reads
  **`inv_636`** (confirmed `death_permanent`, size 120) — where a
  gravestone's contents move once the timer expires. Shares
  `%var261-263` with `death_coffer`. Genuinely part of this feature (grave
  → expired-into-permanent-store → paid reclaim from Death directly) but
  not deep-traced here.
- **`death_coffer_side` (671)** — a single `items` grid, presumably the
  player's own `inv` like other side panels. **Corpus gap**: zero scripts
  reference it anywhere; population script missing, same class as
  `shopside`'s own gap.

## 4. Server obligations

| what | mock230 status |
|---|---|
| Item-loss/retention computation on death (bucketing, priciest-item ranking) | **not implemented** — `death.rs2` states this is deliberate, not an omission |
| `deathkeep_items`/`inv_468` populated and pushed at time of death | **not implemented**; containers exist in cache, unpopulated |
| The actual `deathkeep_init` call site on death | **not implemented** — 0 hits anywhere in server/scripts |
| Gravestone-duration varbit, resynced < 90-unit window | **not declared** |
| `gravestone_tli_hide` | **not declared** |
| `inv_525` (`gravestone`) populated on death | **not implemented**; container exists, unpopulated |
| Fee-tier threshold varbits (3) | **not declared** |
| `gravestone_transmit_data` push (fee + coffer balance) | **not implemented** |
| Reclaim (free/pay) + incinerate handlers | **not implemented** |
| `death_coffer` sacrifice pipeline (select, value, destroy-and-credit) | **not implemented** |
| `death_coffer_side` population | **not implemented**, exact shape unconfirmed (corpus gap) |
| `death_permanent`/`death_office` reclaim flow | **not implemented**, out of primary scope, same feature family |
| `%varbit1777` (ironman) | **not declared** anywhere (already independently confirmed elsewhere in this series) |

## 5. Landed vs. gap — the precise boundary

**Basic death handling is landed, entirely as content**:
```
[queue,player_death](int $unused)
anim(human_death, 0);
p_stopaction;
~combat_death_message;
p_delay(^death_delay);
p_teleport(^respawn_coord);
stat_heal(hitpoints, 99, 100);
healenergy(10000);
~prayer_deactivate_all;
~player_combat_stat;
~respawn_message;
```
(`server/scripts/player/death.rs2:24-42`, verbatim, confirmed.) Its own
header states the boundary directly: *"The reference's `[queue,player_death]`
also drops items, checks for a duel, clears a PK skull and cures poison.
None of those systems exist on this server, and `~player_death_lose_items`
in particular is a whole design decision rather than an omission: a mock
that empties your inventory is a mock nobody uses twice."*

The engine's only contribution is two facts about the simulation, both
landed and confirmed: `player->dying` gates corpse behavior and is set when
hitpoints hit zero (`mock230_combat.c:533,553`), and it clears only when
the script's own `stat_heal` brings hitpoints back up
(`mock230_combat.c:597` region) — the length of a death is decided
entirely by content, not the engine.

**Everything this doc traces — deathkeep's preview, the gravestone, Death's
Coffer — sits entirely past that declared boundary.** No C anywhere
references `deathkeep`, `gravestone`, `death_coffer`, or `death_office`.

## 6. LostCity precedent — splits cleanly by era

**Gravestones and Death's Coffer have zero LostCity precedent, confirmed.**
Every "gravestone" grep hit in the content tree is an unrelated quest
object/loc name; the engine has zero death logic beyond a wealth-tracking
log label. This is a modern (post-2004) addition with nothing to port from.

**The 2004-era base mechanic is present, though, and it's the direct
ancestor of `deathkeep`'s bucketing** — `content/scripts/player/scripts/death.rs2`'s
`[proc,move_priciest_item_on_hero_to_death]` (confirmed present, called 7
times across `player_death_lose_items`/`pvp_death_lose_items`) already does
a server-side value-ranking loop functionally identical in spirit to
`deathkeep`'s: walk inventory + worn, find the highest `oc_cost`, move it to
a **named `deathkeep` container** — confirmed the exact name origin of
osrs239's own `inv_584`/interface-4 naming. Called 3 times unskulled, +1 for
Protect Item, 0 if skulled — the classic RuneScape retention rule. Anything
not kept drops on the ground at a lootdrop timer, or is destroyed outright
if `oc_param` flags it (e.g. god capes, with a warning message first).

| piece | era | precedent |
|---|---|---|
| Server ranks items by cost, picks N most valuable to keep | 2004 | `move_priciest_item_on_hero_to_death` |
| 3 kept + Protect Item + skull rule | 2004 | `death.rs2` |
| Non-kept items drop on ground at a timer | 2004 | `inv_dropall`/`both_dropslot` |
| Named `deathkeep` holding container | 2004, name reused directly | ancestor of `inv_584`/interface 4 |
| A **preview UI** before acting | **not in 2004** | osrs239's `deathkeep` interface — 2004 is a silent server computation, no client screen |
| Gravestone timer, free/paid tiers, incinerate | **not in 2004** | zero precedent |
| Death's Coffer | **not in 2004** | zero precedent |
| Death's permanent hold + remote reclaim | **not in 2004** | zero precedent |

**Port the ranking/keep-count logic from `death.rs2`** directly into
whatever builds `deathkeep_items`/`inv_468`'s categorization, and port the
drop-vs-destroy split for whatever isn't bucketed as kept/gravestone. **The
gravestone-timer/coffer/office machinery has no reference to port** — build
it from this doc's traced client behavior, the same class of work as
stat-orbs/XP-drops/clan-chat.

## 7. What this doc does not cover

- `script_3462`/`script_3466` — missing from this decompile; re-decompile
  before scoping either.
- `death_coffer_side` (671) and `death_office` (669) — traced only
  shallowly; `death_office`'s full call graph wasn't read in full.
- What the 4 `deathkeep` scenario-switcher buttons actually represent —
  inferred to be a preview-context switcher, semantics not stated anywhere.
- Skull status and Protect Item's own server implementation — the duration
  constant is ported, the mechanic isn't; both are direct inputs to the
  ranking logic above and need to land before `deathkeep`'s bucketing can
  be built correctly.
- Full price-formula parity for the fee tiers and value-acceptance rule —
  traced structurally, not re-derived against real OSRS's published numbers.
