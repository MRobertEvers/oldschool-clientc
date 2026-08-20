# Farming (`farming_tools` 125, `farming_tools_side` 126, `farming_view` 179): what the server owed

> **BUILT — 2026-08-02, verified in the client:** `farming_tools` (125) and
> `farming_tools_side` (126). `farming_view` (179) is **not** built and is
> triaged at the bottom of this block. The case file is
> [`farming_tools.md`](farming_tools.md). The discovery pass is kept below as
> written; what landed and what it got wrong come first.

## What landed

The Tool Leprechaun's store: twelve tool/compost cells, Store and Remove of
each at 1/5/X/All, the four quantity radios, deposit-all, examine, and a
save/load round trip. Opened from the npc's own op, closed by the engine's
existing modal path.

| layer | what was missing | what landed |
|---|---|---|
| compiler | `if_openmain_side(farming_tools, …)` compiled to **loc 7516** — `farming_tools` is interface 125, varp 615 *and* loc 7516, and `SSC_SymbolsFind` with no kind returns the lowest-numbered one. The server sent a well-formed mount for an interface that does not exist | a `base_hint = SSC_SYM_INTERFACE` on the `IF_OPEN*` family in `parse_command`, the same mechanism already there for stat names. IF_OPENSUB's *component* first argument is unaffected — the hinted lookup misses and the unhinted one finds it. **Latent for every colliding interface name, not just this one** |
| client | `RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX` was **128** and the rev-230 gameframe alone uses **131** (measured), so the *next* panel to mount got NULL and its `if_setonvartransmit` was dropped silently — the store drew correctly and then never updated | all three hook tables **128 → 512**, and overflow now prints once |
| server | `ToriRSServer_SavePlayer`/`ToriRSServer_LoadPlayer` had **zero callers** | load at the top of `ToriRSServer_WorldLogin` (before the burst and before `[login,_]`, so `%newplayer_seeded` still works); save at the top of `ToriRSServer_WorldRemovePlayer`, while the player is still whole; `ToriRSServer_EmbedStop` now logs its clients out instead of freeing their sessions where they stand |
| server | a loaded varp is not a *changed* varp — state restored perfectly and the client was never told | login burst step 4b sends every declared-`transmit` varp with a non-zero value directly. Non-zero is the right filter because the client starts every session zeroed |
| server | `oc_desc` (**4204**) declared-and-uncovered because the examine text was decoded and discarded | `ToriRSServer_ObjInfo` now keeps `RSCache_Dat2ConfigObj.examine` (config opcode 3); notes inherit the item's line the way they already inherit its name. Op 10 is "Examine" on nearly every panel in the game and had no server-side answer at all |
| content | everything | `interface_farming/` — `farming_tools.constant` (101), `.varp` (81), `.npc` (18), `.spawn` (21), `farming_tools.rs2` (706), `farming_tools_ops.rs2` (393). No existing content file touched |

Permanent check: `ToriRSServer --selftest` section **"the tool leprechaun's store"**
(`torirs_server_world.c:7462`) — a real `OPNPC3`, then six op/mode combinations on
one component, the varbit *split* (five rakes must pack as `rake=1
extrarakes=2`, 300 buckets as `12/1/1`), Remove-All returning five rakes in
five slots, a full store refusing, and a save/load round trip. Five mutations,
five distinct failures — table in [`farming_tools.md`](farming_tools.md).

## What it cost

**One of the four seams was farming's.** The other three — the compiler's
name-kind collision, the transmit-hook ceiling, and persistence having no
callers — were general and had been waiting for the first feature that needed
them. Persistence was stage 1's cut, handed here deliberately because this
feature has varbits to assert it against. No new packet, no new trigger; the
`IF_BUTTON<n>` triggers stage 1 added are what carry the op index, and
`runclientscript*` was not needed at all on this panel.

The best mutation in the suite is the persistence one: dropping `scope=perm`
on one varp makes five rakes come back as **four**, because the low bit of the
count lives in the varp that was un-declared. Neither right nor obviously
wrong.

## What was deliberately left

- **`farming_view` (179) — not built, and it is not an interface problem.**
  It is up to 107 small mutable per-player records with a growth clock behind
  them, and **no tick-based crop simulation of any kind exists in this
  engine**. The per-patch render `script1119(int, obj, coord, int, string)` has
  zero callers in 9,433 scripts, which is the cache saying the server runs it;
  with `SS_OP_RUNCLIENTSCRIPTVARARG` (11003) landed in stage 1 the *packet* is
  now expressible, so what remains is the simulation, not the wire. §2 of the
  body below still stands.
- **Ops 6..10 cannot be picked in this client at all.**
  `UITREE_MENU_OPTION_SLOTS` is **5**, so `rs_cs2_apply_op` drops any
  `if_setop(index > 5)` and the component never carries the label — verified by
  right-clicking a cell and getting Store-1/5/X/All with **no Examine**. The
  wire and the dispatch are already ready for ten. This is a data-width gap in
  the component's op table, not a protocol one, and it is invisible for **916
  `op6..op10` declarations across 35 interface files** in this cache, including
  13 plain `op10=Examine`. Farming's op-9/op-10 bindings are written and armed
  and are the cheapest test case for widening it.
- **Store-X** is implemented and wired (`~farming_resolve_quantity`,
  `P_COUNTDIALOG`) but cannot be *reached* in this client for the same reason.
- **The twelve capacities and the watering-can charge table are transcribed
  into content and marked as such.** The server still cannot read a cache
  enum: `torirs_server_content.c` walks `.enum` under `server/scripts` only, so
  `configs/all.enum`'s 6,024 rank-0 enums are absent (stage 1 found the same
  thing). This is the one place in the feature where the server and the client
  hold the same number twice.
- **"Banknotes" (op 9) has no source anywhere** — clientscript 1060 gives it a
  label and no number. Read as "the selected default quantity, noted", stated
  as a content decision in one place.
- Untouched on purpose: `container_for`, `POP_VAR`/`POP_VARBIT`,
  `CS2VM2_OPCODE_STACK_MAX`.

## What the discovery pass got wrong

> The body below is kept as written. Its central finding — that this is
> varbit-backed storage and not a shop — is **right**, and it is what made the
> feature buildable without touching `container_for`. Six things in it are
> wrong or misleading, in the order they cost time:
>
> 1. **§1.5 "no click handler body exists in this corpus, same gap class as
>    shop's buy-op" is a mis-diagnosis, and it is the load-bearing one.**
>    `op1=* … op4=*` in the `.if` with **no `onop=`** is not a missing handler:
>    it is the *positive* signature of a server op at rev 230, the same one
>    `friends:ignore` and the stats tab's "View \<skill\> guide" carry. What the
>    client does attach (`if_setonop("script487(...)")`) is a six-line click
>    flash. A numbered op runs the local onop hook **and** goes to the server as
>    `IF_BUTTON<n>`; the events mask is the only gate. Any spec in this series
>    that concluded "client-handled, so nothing for the server to owe" should be
>    re-read with that in hand.
>
> 2. **"~14 storage-count varps/varbits" is 22**, and the shape is not a list of
>    counters. Five cells are *one integer split across two or three varbits in
>    two or three different varps* — five rakes are `farming_tools_rake = 1` and
>    `farming_tools_extrarakes = 2`, and those live in varp 615 and varp 2084.
>    That is the difference between a save that works and one that reads back
>    four. Full table in `farming_tools.md` §1.1.
>
> 3. **§1.2's "one real collision" with the Hallowed Sepulchre is not a
>    collision.** `[proc,script376]`'s `case 2193, 342:` is the *cache's own*
>    switch: the two features share `%farming_tools_selectedquantity`
>    deliberately, as one "default quantity" preference. There is nothing to
>    avoid clobbering. The real finding about that varbit is the opposite one and
>    the doc misses it: the client's own write to it (clientscript 377) is a CS2
>    `pop_varbit`, which is a **stack-balanced no-op** in this VM — so the server
>    must own the varbit or the op labels and the op-index table disagree
>    permanently.
>
> 4. **The op index is not a fixed quantity.** Not mentioned at all. Clientscript
>    1060/1062 reorder Remove-1/5/X/All per the selected mode, so op 2 means "5"
>    under mode 0 and "1" under the other three. A server with a fixed table
>    stores 1 when the row said 5, silently.
>
> 5. **§1.4's "zero new engine work needed" was true of the side panel and not
>    of the feature.** Four engine seams were missing and only one of them was
>    farming's: sscompile resolved a bare interface name to the *loc* of that
>    name (`farming_tools` is interface 125, varp 615 **and** loc 7516);
>    `RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX` was 128 and the gameframe alone uses
>    131, so the store's repaint listener was silently dropped;
>    `ToriRSServer_SavePlayer`/`ToriRSServer_LoadPlayer` had no callers and, once given
>    some, still did not transmit what they loaded; and `oc_desc` (4204) was
>    declared-and-uncovered because `ToriRSServer_ObjInfo` decoded the examine text
>    and threw it away.
>
> 6. **§2.2's "the caller is missing entirely from this corpus"** is a
>    *decompiler* gap rather than a cache gap, the same way
>    `docs/skill_guide.md` §7 records for `script9176`. `farming_view` is still
>    unbuilt and the rest of §2 stands.
>
> 7. Line drift, for anyone following a citation: §1.4's
>    `torirs_server_scripts.c:1505-1526` for `container_for` is **2088** today, and it
>    still has the same three cases. §1.2's varbit count and the op-slot
>    ceiling are both covered above.

---

# The discovery pass, as written

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

| interface | id | mechanism | ToriRSServer status |
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
(`src/torirsserver/torirs_server_scripts.c:1505-1526`) — **zero new engine work**
needed for the side panel.

### 1.5 Server obligations

| what | ToriRSServer status |
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

| state | delivery | ToriRSServer status |
|---|---|---|
| 107-row static location register | client cache, already generic | **landed** (cache content) |
| Per-player, per-patch dynamic state (planted crop, growth clock, flags) × 107 | unknown wire shape — corpus gap on the caller | **partial** — mid-era sim patches paint via `~farming_view_refresh` → `farming_view_setpanel` (1119); Geomancy opener still deferred; unowned patches stay Loading… |
| Selected tab (`%varbit4776`) | varbit transmit | not declared (trivial) |
| The underlying crop-growth/disease/compost simulation this UI displays | — | **mid-era core landed** (herbs/allot/flower/trees/fruit/hops/bushes/compost); disease/watering mostly stubbed |

---

## 3. Landed vs. gap in ToriRSServer

`grep -rniE "farming|\bpatch\b" src/torirsserver/ src/game/` — exactly two
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
