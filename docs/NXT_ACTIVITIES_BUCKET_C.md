# Bucket C, re-measured — and implemented

> **Status.** Eight of the twelve rows are done, one is partial, and the
> remaining four are a single server feature with its own handoff at
> `docs/HANDOFF_HELPER_GENERIC.md`. The measurement below is what the
> implementation was built from and is left standing; a closing section records
> what it turned into and the three things the work found that this document did
> not predict.

`NXT_CLIENT_PLUGINS.md` closes with twelve rows it calls the only open problems,
on the grounds that most of them "need a fact this client is not told":

> 5 wants "whose damage was that", 279 "was that my maximum", 182/183 the
> server's loot-ownership rule, 163/184/268 per-course and per-task tables that
> are game knowledge, not cache data.

That premise does not survive measurement. **The client is told whose damage it
was and whether it was a maximum — as the hitsplat type id — and the per-course
and per-task tables are in the cache, not in anyone's head.** None of the twelve
needs a hand-written table, and none of them needs a plugin.

What the twelve actually split into:

| | rows | who implements it |
|---|---:|---|
| **the cache already branches; this client throws the branch away** | 2 | 5, 279 |
| **the server owns the decision; the cache owns the whole display** | 10 | 10, 163, 182, 183, 184, 268, 272, 273, 275, 280 |

and the ten share **one** blocker, which is neither game knowledge nor an
opcode: the server is never told what the setting is set to.

Everything below was read off `cache.osrs239`, the Ghidra decompilation at
`~/Documents/git_repos/osclient_decompile/osclient-216-mac.c`, and this tree.
Where something is inference rather than measurement it says so.

---

## How "no reader anywhere" was checked a second time

The plan's classification came from grepping decompiled clientscripts. That is
right as far as it goes, and it was re-run here over a full decompile (9724 of
9725 scripts):

```sh
3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 --out /tmp/cs2all
```

Every one of the twelve varbits appears in exactly two places — the read hub
`proc,script6716` and its own one-line writer — and nowhere else:

```
varbit10236 : [proc,script6716] [clientscript,script3973]
varbit12379 : [proc,script6716] [clientscript,script5184]
varbit13082 : [proc,script6716] [clientscript,script5331]
...
[clientscript,script5184]
if (~script100 = 0) { return; }
%varbit12379 = calc(1 - %varbit12379);
```

So far so good. The step that was missing is the **other** possible reader. A
setting with no cache reader has exactly two remaining homes: the NXT engine, or
the server. Both are checkable.

**The engine reads none of them.** Grepping the decompilation for all twelve ids,
decimal and hex, finds three hits and all three are struct offsets. The engine's
only `ClientVarCache::GetVarbit` call sites are four:

| site | what it is |
|------|------------|
| `Client::GetIfVar` | an interface component's var dependency |
| `ScriptRunnerImpl::ExecuteScriptInner` case 0x19 | the CS2 `GET_VARBIT` opcode |
| `LocType::GetMultiLoc` | multiloc |
| `NpcType::IsMultiNpcVisible` | multinpc |

There is no `HighlightManager`-shaped class for any of these features. Grepping
for `Infobox`, `WorldmapMarker`, `HintArrow`, `BossHealth`, `Slayer`, `Agility`,
`BlastFurnace`, `LootRestrict` finds **nothing** — the only near-hit is
`GraphicsDefaults::GetSpriteHintMapMarkersID`, which is the hint-arrow sprite
table.

That leaves the server for ten of them. The other two turned out to have a
reader after all, and it is not a reader of the varbit.

---

## 5 and 279: the cache branches inside the hitsplat config

### `.hitmark` opcodes 17 and 18 are a multi-var selector

`3rd/rscache/src/datatypes/dat2_config_hitsplat.h` already decodes these and
calls them "the multi-variant selector", with the fields named `variant_a`,
`variant_b`, `variant_c` because nothing said what they were. The reference says
what they are. `HitmarkType::Decode` case `0x11`/`0x12`:

```
+0x70 = g2()   (65535 -> -1)      // a VARBIT id
+0x74 = g2()   (65535 -> -1)      // a VARP id
default = g2() on opcode 18 only  // the fallback
count = g1(); array = count+1 ids read; array[count+1] = default
```

and `HitmarkType::GetMultiHitmark` resolves it:

```c
if (varbit != -1)      v = GetVarbit(varbit);
else if (varp  != -1)  v = m_var[varp];
else                   v = -1;
if (v >= 0 && v < size - 1) id = array[v];
else                        id = array[size - 1];
return id == -1 ? nullptr : HitmarkType::List(id);
```

That is `LocType::GetMultiLoc` with the names changed — the same shape, the same
`v < size - 1` bound, the same "the last entry is the fallback". **This tree
already has that resolver**, exactly:
[varp_manager.c:602](src/varp/varp_manager.c#L602) `VarPManager_ResolveTransform`,
down to the comment about -1 being a real positional entry.

### The cache is wired for both settings, and the ids pair up

34 of `cache.osrs239`'s 83 hitmark records carry opcode 18. Twenty-five are keyed
on **varbit 10236** (`hitsplat_tint_disabled`, setting 5) and nine on **varbit
14196** (`hitsplat_maxhit_disabled`, setting 279). Nothing else in the whole
table is keyed on anything else.

The records are wrappers over leaf appearances, and they pair:

| wrapper | varbit | value 0 → | value 1 → | fallback | reading |
|--------:|-------:|----------:|----------:|---------:|---------|
| 12 | 10236 | 26 (spr 1358) | 26 | 26 | block, dealt by you |
| 13 | 10236 | **27** (spr 1630) | 26 (spr 1358) | 27 | block, dealt by someone else |
| 16 | 10236 | 28 (spr 1359) | 28 | 28 | damage, dealt by you |
| 17 | 10236 | **29** (spr 1631) | 28 (spr 1359) | 29 | damage, dealt by someone else |
| 43 | 14196 | **48** (spr 3571) | 28 (spr 1359) | 48 | your maximum hit |
| 2 | 14196 | 70 (spr 4763) | 68 (spr 1360) | 70 | a max-hit variant |

and so on for 8/9, 10/11, 14/15, 18/19, 20/21, 22/23, 24/25, 53/54, 59/60,
65/66 on 10236, and 44, 45, 46, 47, 55, 61, 77 on 14196.

Both settings are **inverted** (the gameval names end `_disabled`), so value 0 is
"the feature is on". Read that way the table says exactly what the two rows'
own sentences say:

- Setting 5 — *"hitsplats caused by damage that you did not deal are tinted."*
  Wrapper 17 draws the tinted leaf 29 when tinting is on and the plain leaf 28
  when it is off; wrapper 16 draws 28 either way, because your own damage was
  never the thing being tinted.
- Setting 279 — *"your max hit hitsplats will look different from non max
  hits."* Wrapper 43 draws leaf 48 when on and falls back to the ordinary
  damage leaf 28 when off.

**So "whose damage was that" and "was that my maximum" are both told to the
client — as which wrapper id the server sent.** The 16/17 and 12/13 pairing is
inference from the pair structure plus each row's own description, but it is
corroborated by the ids everyone else uses for them (`DAMAGE_ME` 16,
`DAMAGE_OTHER` 17, `BLOCK_ME` 12, `BLOCK_OTHER` 13, `DAMAGE_MAX_ME` 43).

### Why nothing tints today: the client resolves nothing and the server sends leaves

Two independent halves, and both have to move.

**Client.** `task_dat2_hitsplat_load.c` decodes the whole record and then keeps
three fields of it — `sprite_id`, `duration`, `slot_policy`. The variant arrays
are decoded and dropped on the floor. `RS_Hitsplats` has nowhere to put them and
[app.c:3502](src/app.c#L3502) asks `RS_Hitsplats_SpriteFor` for the sprite of the
type the wire named, with no resolution step in between.

**Server.** `torirs_server_combat.c` names its splats out of the content pack —
`hitsplat_block()`, `hitsplat_poison()`, `hitsplat_shield()` — and
`OSRS-Content/osrs239-content/configs/all.hitsplat.compack` names ten ids, of
which the two that matter are `26=hitsplat_block` and `28=hitsplat_damage`.
Those are **leaves**. A leaf has no opcode 18, so even a client that resolved
perfectly would have nothing to resolve: the server is sending the answer
instead of the question.

### What to implement

Client, and it is small:

1. Carry opcode 17/18 through the loader. `RS_Hitsplats` grows a per-type
   `variant_varbit` / `variant_varp` / variant list. Name the decoder's fields
   while you are there — `variant_a` is a varbit id, `variant_b` a varp id,
   `variant_c` the fallback.
2. Add `RS_Hitsplats_ResolveType(hitsplats, varps, type)`. The array it hands
   `VarPManager_ResolveTransform` is `variants[0..variant_count-1]` **followed
   by** `variant_c`, length `variant_count + 1` — that reproduces the
   reference's `count + 2` layout and its `v < size - 1` bound exactly. A
   resolved id of -1 means **drop the splat**, which is a real state
   (`*local_90 = -1` in `GdmEntityOverlayHitsplats`) even though no record in
   this cache exercises it.
3. Resolve **one hop only**. The reference calls `GetMultiHitmark` once and does
   not re-test the result; a wrapper pointing at a wrapper is not a case it
   handles.
4. Resolve at **draw** time, not on receipt. That is where the reference does it,
   and it is why toggling the setting re-skins splats that are already on screen
   instead of only the next one.
5. Duration and slot policy stay on the **original** type. The reference reads
   `+0xc` (opcode 9) off the type the wire named, before the swap. Moving them
   to the resolved type would be invisible until a wrapper and its leaf
   disagreed.

Server: send wrappers. `hitsplat_damage` should become the pair 16/17 chosen by
"did the local player deal this", and the max-hit path should send 43 when the
roll equalled the attacker's maximum. That means naming the wrappers in
`all.hitsplat.compack` — the file already documents that these names are
authored evidence rather than a cache table, so this is the place for
`hitsplat_damage_me` / `hitsplat_damage_other` / `hitsplat_max_me` and the block
pair.

### How it fails silently, and what to pin

Both errors this can make look like a working client:

- Resolving with the array **without** the fallback appended shifts the bound by
  one, so value 1 (the setting switched off) falls through to the fallback and
  draws the *tinted* leaf. The feature then works backwards and only when
  disabled.
- Reading the row the plain way — the trap `nxt_activities.h` exists for —
  tints your own hits and leaves everyone else's plain.

`test-hitsplat`-shaped coverage: feed the decoder record 17 verbatim, drive
varbit 10236 to 0 and 1, and assert leaf 29 then leaf 28. Two lines, and it is
the only thing that can tell those two failures apart from "the server never
sent a tinted hit".

---

## The other ten: the server owns the decision, the cache owns the display

Every one of these has its display fully built in the cache and no caller. That
is the same signature the plan already identified for bucket B's respawn timers
("nothing in the cache calls it because the SERVER calls it by
`RUNCLIENTSCRIPT`") — it just was not looked for here.

### `helper_generic` (interface 711) is the infobox, and it is driven by 27 scripts

The cache names interfaces `710=helper_cox` and `711=helper_generic`. Twenty-seven
clientscripts build into 711, and among them are entry points **no script calls**:

| row | entry point | what it builds |
|----:|-------------|----------------|
| 163 Agility helper | `clientscript 5170` → 5171, 5182 | "Laps Completed"/"Tickets Gained", course name from `enum_3507` keyed on `%varbit12633` |
| 184 Slayer helper | `clientscript 5317` → 5318 | "Slayer Info", four rows, re-armed on `var394/395/2096/1077/1565/661` |
| 275 Clue helper infobox | `clientscript 6631(row)` → 6632, 6633 | switches on `db_getrowtable` and dispatches to 6634..6644 |

`5170`, `5317` and `6631` have **zero callers in the entire cache**. That is what
an entry point the server is supposed to call looks like, and it is the same
evidence that identified 5471/5475/5478 for the respawn timers.

So the "per-course and per-task tables that are game knowledge" are the cache's:
the Agility course name comes out of `enum_3507`, the Slayer task out of six
server varps the helper re-reads on transmit, and every clue step out of DB
tables 4..14. **Nothing here is authored by hand.** What the server has to do is
decide *whether* (read the varbit) and *when* (the player stepped onto a course,
took a task, read a clue), then send one `RUNCLIENTSCRIPT`.

Row **268 Blast Furnace helper** is the same shape one level up: the cache names
interface `474 blast_furnace_hud` and **no clientscript touches it at all**, which
is a server-opened, server-populated HUD. Its sibling row 269 (highlights) is
already working, through clientscript 6667 off the settings init — so the two
halves of that feature are on opposite sides of the wire, and only one of them
was ever this client's.

### 10 is the health overlay, and it belongs with 111/299/300/301

The plan puts 111, 299, 300 and 301 in bucket B as "an interface only the server
opens" and 10 in bucket C as unreachable. They are one feature.

Interface `303 = hpbar_hud`. Clientscript 2101 already distinguishes boss from
normal — it branches on **`%varbit12401` (`hpbar_hud_boss`)**, and 300's
`%varbit14707` picks compact inside that branch. Setting 10's own varbit 12389 is
never read because the boss/normal choice is not the client's: the server decides,
writes 12401, and opens 303.

So row 10 costs nothing beyond the health-overlay feature bucket B already names.
It should move out of bucket C.

### 272 and 273 are one payload with two renderings

The clue helper's world arrow and worldmap marker have no cache reader and no
engine class. The reference's marker sprite family is
`GetSpriteHintMapMarkersID` / `GetSpriteHintMapEdgeID` / `GetSpriteHintHeadIconsID`
— the **hint arrow**. `HINT_ARROW` is server prot 50 at rev 239 and this client
already parses it
([gameproto_parse.c:578](src/net/rev/gameproto_parse.c#L578)) into
`app.hint_arrow` — where it stops. `app.h` says as much: *"drawing is a flagged
follow-on"*, and nothing in `app.c` reads the struct.

So 273 needs the arrow drawn (an engine change, not a plugin) and 272 needs the
same target marked on the worldmap. The gating is server-side because nothing on
this side reads either varbit.

### 280 is a comparison only the server can make

*"Max hits below this threshold will not show max hit hitsplats."* The threshold
lives in varbit 14195 (`settings_hitsplat_threshold`, 9 bits at
`options_varp`+3), and the only script that touches it is `proc,script3964` — the
settings panel's slider **read**. A damage-versus-threshold comparison cannot be
expressed as a multi-var selector, and the engine does not make it. The server
already chooses the hitsplat type; it sends wrapper 43 only when the damage
equalled the maximum **and** cleared the threshold.

### 182 and 183 are the server's, and 182 needs one more measurement

183 ("occasionally see chatbox messages") is a `MESSAGE_GAME` and needs nothing
here. 182's "indicator icons" has no cache asset — grepping every gameval pack
for `noloot`/`restrict` finds only the two varbit names themselves — and no
engine class. The reference's `GetSpriteHintHeadIconsID` makes a head icon the
likely carrier, but that is a guess and should be measured before anything is
built on it. It is the one row here whose *mechanism* is still open, as opposed
to its *decision*.

---

## The one real blocker: the server is never told

All ten server rows want to read a varbit the server does not have.

- The panel's write is client-local. `RS_CS2Host` → `VarPManager_SetVarbitOptimistic`
  writes `mgr->var[]` only, and `VarPManager_ApplySync` overwrites `var[]` from
  `var_serv[]` whenever the server speaks about that varp.
- **rev 239 has no client packet that carries it.** The whole client prot table
  is `3rd/rsprot/gen/rev239_prot.h`; the only settings-shaped entry is
  `SET_CHATFILTERSETTINGS`. There is no varp, no varbit, no generic setting op.
- The reference client does not transmit either. `ClientVarCache::SetVarbit`
  writes `m_var` and returns; `m_varServ` is written only by the inbound
  `VARP_*` handlers.
- The settings panel does not ask the server either — no `if_triggerop` or
  `cc_triggerop` anywhere in the 134 family, and the cache's own
  server-applied row kind (`~script3968`, whose switch is empty) is not the kind
  any of these rows use.

Which means the reference server already holds these varps by some path outside
this revision's prot table, and the client's write is a prediction of a value the
server is expected to agree with.

For this tree the honest options are two:

1. **Add a client→server settings message on the existing command bus.** One
   `(varbit id, value)` pair, accepted only for varbits that appear in
   `enum_4024`, applied server-side and echoed back as `VARP_SMALL`. This makes
   the ten rows possible and incidentally fixes the latent bug above — today an
   optimistic settings write survives only for as long as the server stays quiet
   about that varp, and `ironman_var_1` carries settings 5, 10 and 279 together.
2. **Leave the client optimistic and let the server drive from its own copy**,
   set out-of-band (content, cheat, or a login default). Cheaper, and enough to
   *demonstrate* every one of the ten, but the panel then lies: toggling a row
   moves the checkbox and the server keeps acting on its own value.

Option 1 is the one worth building — it is roughly the size of one packet and it
is the difference between the ten rows working and the ten rows being
demonstrable.

---

## What this changes in the plan

- **5, 279** move out of "no reader anywhere". The reader is
  `HitmarkType::GetMultiHitmark`, the branch is in the cache, and this tree
  already has the resolver. Two rows, one client change plus a server id change.
- **10** moves into bucket B beside 111/299/300/301 — one server feature, the
  health overlay, and 303 already knows how to be a boss bar.
- **163, 184, 268, 272, 273, 275, 280, 182, 183** are server features whose
  display the cache already carries. Three of them (163, 184, 275) are a single
  `RUNCLIENTSCRIPT` each into `helper_generic`.
- **182** is the only row left whose mechanism is unidentified.
- Nothing in the twelve wants a hand-written per-course, per-task or
  per-minigame table. The plan was right to refuse to invent one; there was
  never one to invent.

## Re-deriving any of it

```sh
# every clientscript, to find the readers and the callerless entry points
3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 --out /tmp/cs2all

# the hitmark table, including opcodes 17/18
3rd/rscache/tools/cachepack/cachepack unpack --cache cache.osrs239 --rev osrs239 \
    --src /tmp/hs --types hitsplat,varbit,varp
grep -A2 variantop /tmp/hs/configs/all.hitsplat

# the reference's own reader and resolver
grep -n 'HitmarkType6Decode\|HitmarkType15GetMultiHitmark' \
    ~/Documents/git_repos/osclient_decompile/osclient-216-mac.c
```

---

# What it turned into

## Done

| id | row | where |
|---:|-----|-------|
| 5 | Hitsplat tinting | `RS_Hitsplats_ResolveType` + `ToriRSServer_HitsplatForViewer` |
| 279 | Max hit hitsplats | the same pair |
| 280 | Max hit hitsplats threshold | `hitsplat_is_max_hit`, server-side |
| 10 | Show boss health overlay | `torirs_server_hpbar.c`, as a veto |
| 183 | Iron loot restriction messages | `ToriRSServer_LootRestrictionWarn` |
| 273 | Clue scroll helper - World arrows | `app_overlay_build_hint_arrow` + the four `hint_*` opcodes |
| 272 | Clue scroll helper - Worldmap marker | client half (the same arrow); the worldmap's own marker is open |
| — | the settings mirror | `settings_mirror_varbit`, which all ten server rows needed |

**Partial: 182.** The rule works and its varbit is registered; there is nothing
to draw. See the plan document's note — no sprite, spotanim, interface,
clientscript or engine class in either the cache or the decompilation is
loot-restriction shaped.

**Open: 163, 184, 268, 275** — one feature, `helper_generic`, handed off
separately.

## Three things the measurement did not predict

### 1. The content tree had been destroying the hitsplat table

`OSRS-Content/.../configs/all.hitsplat` was an export from the *old, broken*
decoder — `opcode8=37`, a phantom `opcode49`, `opcode18` as opaque hex. cachepack
rejects those keys, so every `torirsserver-cache` bake wrote near-empty records:
**83 records in 318 bytes**, every `text` and `duration` lost and **all 34
selectors with them**.

So the client resolver would have found nothing to resolve, and the failure would
have looked exactly like the resolver being broken. Regenerated;
`cachepack verify --types hitsplat` reports 83/83 byte-exact and the boot line
now reads `83 types (83 records, 34 var selectors)`. That count is printed for
this reason and no other.

The general lesson is the one `exporter-owns-generated-configs` already states,
with a sharper edge: a generated file that predates a decoder fix is not stale
in the harmless sense. It is a rejected file, and cachepack's per-key warnings
scroll past in a bake that reports success.

### 2. `test-torirsserver` never rebuilt the server

`ToriRSServer` (capitalised) is listed in `.PHONY` and **has no rule anywhere**.
On a case-insensitive filesystem it resolves to the already-built
`torirsserver`, make answers "Nothing to be done", and the suite runs whatever
binary is on disk.

Found by mutating a function the suite asserts on and watching the suite stay
green — the same shape as the unregistered-stanza hazard
`tools/check_selftest_registration.py` exists for, but on the C side, where
nothing was checking. Four targets depended on it. Fixed.

This is why the mutation step is not optional: the suite reported green for a
binary built before the change under test, which is a negative control that
cannot go red.

### 3. Two settings share one varp, and it is load-bearing

`ironman_var_1` (varp 1425) carries settings **5** (bit 16), **10** (bit 17) and
**279** (bit 20). Any server write to that varp moves all three at once, which is
the concrete reason an unmirrored client-side settings write is not merely
invisible to the server but not durable on the client either. The hitsplat test
asserts the two hitsplat rows do not move each other for exactly this reason.

## What is still not done, precisely

- **163 / 184 / 268 / 275** — `docs/HANDOFF_HELPER_GENERIC.md`.
- **182's icon** — needs an identification, not an implementation.
- **272's worldmap marker** — the arrow is drawn in the world; drawing the same
  target on the open world map is a separate renderer change.
- **Edge-of-screen hint arrows** — frames 1..4 of `headicons_hint` are the
  reference's off-view forms. Not implemented: the edge form needs the
  off-screen direction, and a wrong edge arrow points at nothing.
- **Player-versus-player hitsplat attribution** — `CombatHitPlayer` records
  dealer -1 because it runs with the victim active. Correct for every fight this
  server currently stages (npc versus player); PvP would need the attacker
  threaded through from the script.
