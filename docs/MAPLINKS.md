# Map links — ladders, stairs, trapdoors, dungeon entrances

Every ladder, staircase and trapdoor in `OSRS-Content` answers the same way
today: one plane up or down, **same tile**. That is `~climb` in
`ladders_stairs/scripts/ladders.rs2`, bound to four generated categories that
cover every one of the 1,445 climb-verbed records `tools/ladder_import.py`
finds in `cache.osrs239`. It is a faithful port of what `climb` in
`torirs_server_world.c` did, and `ladders_stairs/README.md` already declared the
rest of the job open: a 2004+ loc's own menu verb ("Climb-up", "Climb-down")
never states a *destination*, only a direction, so the ±1-plane default is a
guess that happens to be right for a plain staircase and wrong for anything
that actually goes somewhere — a dungeon, a different building, a cave.

This document is the pipeline that closes that gap: where the destinations
come from, how they are verified against this exact cache before anything is
trusted, and what `tools/maplink_import.py` generates from the result.

## 1. Where the wiki keeps map-link data

The OSRS Wiki does not publish its "intra-map links" (the yellow squares
connecting dungeon entrances to their surface exits) as a downloadable file.
Probed directly:

- `https://maps.runescape.wiki/osrs/data/dataloader.json` — the map viewer's
  own data manifest. Lists icon sets, 52 base maps and one overlay
  (`MainMapIconLoc.json`, marker pins). No map-link dataset.
- `RuneScape:Map` (the wiki's own documentation of its map system) lists "Map
  links (doors, portals, ...)" in a data-sources table with source "Game
  Cache, Manual (user defined)" and no file — the wiki draws them from the
  game cache and hand-authored overrides at render time, not from a published
  table.
- `RuneScape:Map/mapIDs` points only to `basemaps.json` (52 named map
  bounding boxes — used below for dungeon naming, not links).

**The RuneLite `shortest-path` plugin (`Skretzo/shortest-path` on GitHub) is
the machine-readable source**, and it is the plugin actually named in the
wiki's own tools list. Its whole transport network — everything a player can
click, walk into, or activate to change location — lives as TSV under
`src/main/resources/transports/`, one file per transport class:

```
transports.tsv                stairs, ladders, trapdoors, doors, gates, cave
                               mouths — anything answered by clicking a loc
agility_shortcuts.tsv         loc + Agility level, may fail
boats.tsv / ships.tsv         npc dialogue, sometimes a loc
canoes.tsv                    loc + Woodcutting + a shaping interface
charter_ships.tsv             npc + coins + a destination-picker interface
fairy_rings.tsv                loc + a 3-dial code interface
gnome_gliders.tsv             npc + a destination-picker interface
hot_air_balloons.tsv          loc + logs + a destination-picker interface
magic_carpets.tsv             npc + coins
magic_mushtrees.tsv           loc + a destination-picker interface
minecarts.tsv                 loc + dialogue + coins
quetzals.tsv / quetzal_whistle.tsv   npc / obj, a destination-picker interface
seasonal_transports.tsv       Leagues-only
spirit_trees.tsv              loc + a destination-picker interface
teleportation_boxes.tsv       POH jewellery box
teleportation_items.tsv       an obj's own op, not a loc
teleportation_levers.tsv      loc, no requirements
teleportation_minigames.tsv   grouping-interface teleports
teleportation_portals.tsv     loc, click to teleport
teleportation_portals_poh.tsv POH — no player-owned house in this tree
teleportation_spells.tsv      spellbook casts — skill_magic's job, not this
teleportation_spells_home.tsv spellbook home teleports — same
wilderness_obelisks.tsv       4 loc pillars, activate to teleport
```

### 1.1 The TSV shape

Tab-separated, `#`-prefixed header and comments, parsed by the plugin's own
`shortestpath/transport/parser/TsvParser.java` (94 lines — a header split on
tabs, then one `Map<String,String>` per data line, nothing cleverer than
that):

| Column | Meaning |
|---|---|
| `Origin` | `x z plane`, space-separated absolute world tile. Empty means a location permutation (fairy rings, spirit trees — no fixed origin) |
| `Destination` | `x z plane`. Empty means a permutation on that side |
| `menuOption menuTarget objectID` | one field: the click verb, then the target's **display name** (may itself contain spaces), then the **object id** as the last whitespace-separated token |
| `Skills`, `Items`, `Quests`, `Varbits`, `VarPlayers` | requirement gates |
| `Duration`, `Display info`, `Consumable`, `Wilderness level` | pathfinder/UI-only fields |

`shortestpath/transport/parser/WorldPointParser.java` confirms there is no
packing subtlety in the coordinate fields: split on spaces, three ints, or a
sentinel for "permutation" if empty.

**`Origin` is the PLAYER's tile at the moment the transport fires, not
necessarily the clicked object's own registered tile.** This matters for the
binding design in §3 — a multi-tile staircase is clickable from more than one
tile, and the loc's own placement in `maps/*.jl2` can sit a tile or two off
of any given `Origin` row. Measured directly: with an exact-coordinate match,
~85% of otherwise-valid climb rows failed placement verification, with `±1`
to `±3` tile deltas scattered in every direction and no dominant offset — i.e.
real per-loc footprint noise, not a coordinate bug. The importer accepts a
placement within 2 tiles (Chebyshev distance) on the same plane.

## 2. Verifying a row against `cache.osrs239`

Every TSV row is an unverified *claim* about a different game revision. A row
is accepted only when **this cache** agrees on all three legs of record
identity — the same rule `ladder_import.py` already applies before it will
resolve a `loc_<id>` by name, and for the same reason: `loc` is 94.7%
id-identical between the wiki's rev and this cache's rev, which is the worst
case there is — 5.3% of a copied id lands on a real, occupied, *wrong*
record and says nothing:

1. **Id.** The row's trailing object id exists in `configs/all.loc.compack`
   (falling back to `all.npc.compack` for the classes that key an npc — boats,
   gnome gliders, quetzals, magic carpets; not needed for climb verbs, which
   are always locs).
2. **Name.** That record's `name=` in `configs/all.loc` equals the row's menu
   target.
3. **Op.** One of that record's `opN=` fields equals the row's menu option —
   and which slot it is on is kept, since a spiral staircase can answer
   "Climb-up" on op2 and "Climb-down" on op3 from the very same tile.
4. **Placement.** The object is actually placed within 2 tiles of `Origin`,
   same plane, somewhere in `maps/*.jl2` (2,933 files, ~7M loc placements).

A row failing any leg is dropped and written to `docs/MAPLINKS_REJECTS.md`
with the reason — never guessed, and never silently absorbed into a count
that looks like full coverage.

### 2.1 What passed, for `transports.tsv`'s climb-verbed rows

(`Climb-up` / `Climb-down` / `Climb` only — everything else in `transports.tsv`
is a later stage, §5.)

| Check | Rows | Fraction of 5,168 |
|---|---:|---:|
| Total climb-verb rows | 5,168 | — |
| Id resolves in `all.loc.compack` | 5,153 | 99.7% |
| Name matches | 4,857 | 94.0% |
| Op matches | 4,873 | 94.3% |
| **Placed within 2 tiles of Origin** | **4,479** | **86.7%** |
| — of which, destination ≠ the ±1-plane default | 3,201 | — |

The headline: **every object id `transports.tsv` names for a climb verb
exists in this cache**, and 87% of them are placed exactly where the wiki's
data says. What doesn't verify (§2.2) is overwhelmingly content added to the
wiki's source after this tree's rev 239, which is exactly what the
placement check is for.

### 2.2 What `~climb` gets right today, and what it doesn't

Classifying the 4,479 verified rows by how far `Destination` actually is from
`Origin`:

| Displacement | Rows | `~climb` today |
|---|---:|---|
| Same tile, plane ±1 (the default) | 777 | **already correct** |
| Within 3 tiles, plane ±1 (a near-miss) | 1,676 | **wrong tile** — can land in a wall |
| A real jump (a different building, a dungeon) | 926 | **wrong plane** entirely, or the same tile a plane away when the truth is a mile off |

So the ±1-plane default is right for 777 of 4,479 verified placements — 17%.
The other 83% is why the wiki lists player reports of "the trapdoor puts you
in the wrong room" as a known class of bug in unofficial servers that skip
this step.

## 3. The binding: a coord-indexed dbtable, read inside `~climb`

`server/scripts/**/configs/*.dbtable` is a **server-only** structure —
`cachepack` never looks inside `server/`, so a new dbtable here allocates no
cache id and enters no gameval table. `legends_gem_data.dbtable` and
`hunter_pitfall.dbtable` already prove the shape: a `coord` column can be
`INDEXED` and `db_find` on it.

```
[maplink]
column=src,coord,INDEXED,REQUIRED
column=dest,coord,REQUIRED
column=loc,loc,REQUIRED
column=dir,int,REQUIRED
```

`src` is keyed on the **player's** tile (`coord`), not the clicked loc's own
tile (`loc_coord`) — deliberately, matching both `~climb` itself (which reads
`coord`, since climbing moves the *player*) and the semantics of the TSV's
`Origin` column established in §1.1. Keying on `loc_coord` would simply never
match for a multi-tile staircase.

`dir` is the plane change the row's own menu verb names: +1 `Climb-up`, -1
`Climb-down`, ±2 the `Top-floor`/`Bottom-floor` op that skips a landing, 0 for
a verb that names no direction (a bare `Climb`, and every transition). It is
part of the key, not decoration — see §3.4.

`~climb` gains three lines at its top:

```
[proc,climb](int $delta)
if (~maplink_try($delta) = true) {
    return;
}
def_int $level = calc(coordy(coord) + $delta);
...             // unchanged ±1-plane body, the fallback
```

`~maplink_try` (`scripts/maplink.rs2`) does the lookup:

```
[proc,maplink_try](int $dir)(boolean)
db_find(maplink:src, coord);
def_dbrow $link = db_findnext;
while ($link ! null) {
    if (db_getfield($link, maplink:loc, 0) = loc_type) {
        def_int $rowdir = db_getfield($link, maplink:dir, 0);
        if ($rowdir = 0 | $dir = 0 | $rowdir = $dir) {
            p_teleport(db_getfield($link, maplink:dest, 0));
            return (true);
        }
    }
    $link = db_findnext;
}
return (false);
```

`~climb` has roughly 20 other call sites across quest scripts
(`quest_seaslug`, `quest_itwatchtower`, `mcannon_ladder`, …) that already know
their own destination and call it directly, never through an `[oploc]`
trigger. None of them are touched: their tile is never a key in `maplink`, so
the lookup misses and execution falls straight through to the same body it
always had.

### 3.1 The coordinate that can't be answered by a coord-only table

A handful of tiles carry two verified climb rows with two *different*
destinations — almost always a spiral staircase's middle floor, where op2
("Climb-up") and op3 ("Climb-down") from the same standing tile lead to
different places. A single `src -> dest` row can't hold two answers, so these
are excluded from `maplink.dbrow` and instead bound by **name and op slot** in
a generated `scripts/maplink_shared.rs2`:

```
[oploc2,<name>] p_teleport(<up-destination>);
[oploc3,<name>] p_teleport(<down-destination>);
```

This is the same mechanism `ladders_stairs/scripts/climb_shared.rs2` already
uses for the 15 open trapdoors that are simultaneously a door and a ladder:
the trigger table's *name* rung outranks its *category* rung, so a per-name
block here wins over the generic `_climb_up`/`_climb_down` category binding
for that op.

A smaller set is ambiguous at **more than one** tile under the same name — a
name-bound override can't tell tiles apart either, so those are dropped
outright and listed by `tools/maplink_import.py`'s stdout, never bound to one
tile's answer everywhere.

**A name+op binding is name-wide, not tile-specific** — `[oploc1,<name>]`
fires for every placement of that name in the world, not just the one tile it
was generated from. This was found the hard way: `my2arm_cliff_shortcut_1`
and `_2` are placed at both ends of one bidirectional cliff jump — one end
unambiguous (goes into `maplink.dbrow` cleanly), the other end sharing its
tile with the other shortcut's reverse jump (ambiguous). The first version of
this importer bound the ambiguous end by name without checking whether the
same name *also* had a clean placement elsewhere — which would have meant
every click on either end teleported to the SAME fixed destination, including
an already-arrived player clicking to go back. `split_unambiguous` now
requires a name to have **exactly one** placement in the whole accepted set
before it is eligible for a name+op binding at all; a name with more than one
placement has its ambiguous-tile rows dropped and reported instead, and its
unambiguous placements keep working correctly through the coord table.

## 3.2 Transitions: cave mouths, portals, levers — a new category

Enter/Exit/Board (cave mouths), and the standalone `teleportation_portals.tsv`
/ `teleportation_levers.tsv` files, carry **no climb verb**, so unlike stairs
they have no `_climb_up`/`_climb_down` category to ride into `~maplink_try`.
There is also no sensible ±1-plane default for "Enter a cave mouth" — the
whole action *is* the destination — so a miss here falls to a message, not a
plane change.

The fix is a genuinely new category, `maplink_transition`, allocated in
`pack/category.pack` (id 8206) exactly the way `climb_up`/`climb_down`/etc.
were — this is a hand-maintained file, not something `sscompile` allocates
automatically, so the id had to be picked past the file's existing high-water
mark and entered by hand. `configs/maplinks.loc` (generated) puts
`category=maplink_transition` on every record with a verified, unambiguous
destination, subject to the same two conflict rules `ladder_import.py`
already applies to `ladders.loc`:

1. A record another authored `.loc` overlay already categorises is not
   re-categorised here — `cachepack` refuses a second `category=` on one
   record.
2. A record whose cache category `pack/category.pack` already names is
   skipped and reported, never overwritten.

`scripts/maplink.rs2` binds three static lines — one per op slot a member
could carry, the same shape `ladders.rs2`'s four climb categories use:

```
[proc,maplink_transition]
if (~maplink_try = true) {
    return;
}
~displaymessage(^dm_default);

[oploc1,_maplink_transition] ~maplink_transition;
[oploc2,_maplink_transition] ~maplink_transition;
[oploc3,_maplink_transition] ~maplink_transition;
```

Note the spelling split: `category=maplink_transition` (bare, matching
`pack/category.pack`'s own name) in the `.loc` file, but
`[oploc1,_maplink_transition]` (leading underscore) in the trigger — the
underscore is what tells `sscompile` the trigger subject is a category
lookup rather than a loc name (`ssc_compile.c`'s `is_category = subject[0] ==
'_'`). Getting this backwards compiles without complaint (declaring an
unbound category is not an error) and silently does nothing; it was only
caught by checking `pack/category.pack` for the literal string, not by a
compiler error.

Records that verify but can't take the category (already claimed, per the
two rules above, or ambiguous the same way §3.1 describes) are handled by the
identical name+op fallback into a separate generated file,
`scripts/maplink_transitions_shared.rs2` — reusing the exact same
single-placement safety check.

## 3.3 Agility shortcuts: same shape, no unsafe fallback

`agility_shortcuts.tsv` (572 rows) adds one requirement climb/transitions
don't have — an Agility level — and one hazard: `skill_agility/scripts/
agility_shortcuts_osrs.rs2` already hand-authors real content (animations,
XP, a documented Kronos-parity policy) for some of these shortcuts. Both are
handled before anything is bound:

- **The level.** A new `maplink_agility` dbtable adds a fourth `level,int`
  column next to `src`/`dest`/`loc`. `~maplink_agility` (a level-aware sibling
  of `~maplink_try`, since the shared proc has no fourth column to read)
  checks `stat_base(agility)` against it before teleporting, and shows the
  same style of message `agility_shortcuts_osrs.rs2` already uses
  (`~mesbox("You need an Agility level of <tostring($level)> to do this.")`)
  on a miss. Multi-skill rows — 15 of the 572 — are grapple shortcuts
  (Agility *and* Ranged *and* Strength, for a crossbow-and-rope) and are a
  different mechanic `parse_agility_level` explicitly rejects rather than
  half-modelling.
- **The collision.** Before any category is assigned, every `.rs2` file in
  the tree is scanned for `[oploc<n>,<name>]` triggers already bound by hand
  (`scan_existing_oploc_bindings`) — 62 of the verified names turned out to
  already have one, not just the 6 in `agility_shortcuts_osrs.rs2` itself but
  others across doors/gates/quest scripts too. Those are excluded from
  `configs/maplink_agility.loc` entirely: name beats category regardless, so
  assigning one would be dead code at best, and excluding it up front keeps
  the generated coverage count honest rather than inflated by rows that will
  never actually fire.
- **No unsafe fallback.** Unlike climb and transitions, a record that can't
  take the category (conflict, or ambiguous at more than one tile) is
  **dropped and reported, not bound by name** — a name+op override here would
  need to bake in its OWN level check per block to stay safe, and getting a
  skill gate wrong (silently admitting an under-levelled player) is worse
  than leaving the shortcut unfixed. 144 of 390 verified rows ended up
  category-bound; 62 were already real content; 57 were dropped for safety
  and are listed in `docs/MAPLINKS_REJECTS.md`.

`pack/category.pack` gets a second hand-allocated entry the same way
`maplink_transition` did — `8207=maplink_agility` — and
`skill_agility/scripts/maplink_agility.rs2` binds the same three static
`[oploc<n>,_maplink_agility]` lines.

**A second self-poisoning bug was caught here, distinct from §3.1's.**
Computing which records are "claimed by another `.loc` overlay" must exclude
the importer's OWN previous output for THIS class specifically — the first
version reused transitions' claimed-set, which excludes `maplinks.loc` but
not `maplink_agility.loc`, so on a second run every one of agility's own 144
just-categorised records read back as "claimed by another file" and
self-conflicted into nothing. Caught by running the generator twice in a row
and diffing the output, which is now the standard check (§6) rather than a
one-off.

## 3.4 The direction is part of the key, and the op that skips a floor

The first version of this table was keyed on the tile alone, and Lumbridge
castle is where that came apart. `spiralstairsmiddle` states `op1=Climb`,
`op2=Climb-up`, `op3=Climb-down`; the wiki lists all four combinations of its
two landings' two standing tiles and two directions; and §2.2's
`classify_displacement` then dropped, as "already correct", exactly the two
whose destination equals the ±1-plane default. What reached the table was one
row per tile — one that only knew *up*, one that only knew *down* — and a
tile-keyed lookup handed that answer to **both** ops. Standing on the west
tile of the south landing, "Climb-down" took the player up.

So `dir` is part of the key and part of the lookup, `~climb` passes its own
`$delta`, and a row that contradicts the op is skipped rather than taken. The
fallback on a miss is not a loss: for that staircase, the dropped row's
destination IS the ±1 default that now answers instead.

A direction of 0 means "the verb names none" — a bare `Climb`, and every
transition — and matches any caller; a caller passing 0 (`~maplink_transition`)
matches any row. `split_unambiguous` therefore treats a 0-direction row as
colliding with every other row on its tile, since that is what it does at
runtime.

**`Top-floor` / `Bottom-floor` is why `dir` is a plane count and not a sign.**
Three-storey spiral staircases state a second climb op on their ground and top
floors that skips the landing entirely, so one tile carries two *up* rows
landing on two different planes. The verbs are in `CLIMB_DELTAS` as ±2, the
six records that state them are bound by name to `~climb_floor_skip` in
`ladders.rs2` (the category rung can only say one direction, and it is op1's),
and the skip asks the table for a ±2 row before falling back to two ±1 hops —
each of which consults the table for itself. Only the Horror from the Deep
lighthouse has harvested rows for it; Lumbridge castle's three storeys are
`spiralstairs*_3` records the wiki data does not know, because it lists the
pre-rework 16671/16673 there and this cache does not place them.

## 4. What `tools/maplink_import.py` generates

```
python3 tools/maplink_import.py [--content <dir>] [--data <dir>]
                                 [--near-miss] [--check] [--report]
```

| Output | What |
|---|---|
| `ladders_stairs/configs/maplink.dbtable` | the schema (hand-authored, stable) |
| `ladders_stairs/configs/maplink.dbrow` | **generated** — one row per verified, unambiguous origin tile, climb AND transition combined |
| `ladders_stairs/scripts/maplink_shared.rs2` | **generated** — climb name+op bindings for single-placement-ambiguous records |
| `ladders_stairs/configs/maplinks.loc` | **generated** — `category=maplink_transition` overlay for non-climb transitions |
| `ladders_stairs/scripts/maplink_transitions_shared.rs2` | **generated** — transition name+op bindings (category conflicts + single-placement-ambiguous records) |
| `ladders_stairs/scripts/maplink.rs2` | hand-authored — `~maplink_try`, `~maplink_transition`, the three category triggers |
| `skill_agility/configs/maplink_agility.dbtable` | the schema (hand-authored, stable) — src/dest/loc/**level** |
| `skill_agility/configs/maplink_agility.dbrow` | **generated** — category-eligible agility shortcuts only |
| `skill_agility/configs/maplink_agility.loc` | **generated** — `category=maplink_agility` overlay |
| `skill_agility/scripts/maplink_agility.rs2` | hand-authored — `~maplink_agility`, the three category triggers |
| `docs/MAPLINKS_REJECTS.md` | **generated** — every dropped row from all three classes, grouped by reason |

`--near-miss` includes the "within 3 tiles" class alongside genuine jumps;
without it, only jumps are emitted. Applies to climb only. All three classes
compile clean through `sscompile` — see §6.

Measured on the full run (`--near-miss`, this cache, this vendor snapshot):

```
Climb (stairs, ladders, trapdoors):
5,168 transports.tsv climb-verb rows considered
1,708 accepted (including the 2 Top-floor/Bottom-floor rows — see §3.4)
1,686 table rows
    0 name+op bindings (my2arm_cliff_shortcut_1/2 among the 6 dropped — see §3.1)
    6 names dropped — multi-placement collision with an ambiguous (tile, direction)
1,110 rejected — see docs/MAPLINKS_REJECTS.md

Transitions (cave mouths, portals, levers):
5,304 transports.tsv/portals/levers rows considered
  440 accepted
  394 category-bound (173 distinct records, category=maplink_transition)
    0 name+op bindings
    5 names dropped — category conflict or multi-placement collision
4,864 rejected — see docs/MAPLINKS_REJECTS.md

Agility shortcuts:
  572 agility_shortcuts.tsv rows considered (15 multi-skill grapple rows excluded)
  390 accepted
  144 category-bound (category=maplink_agility)
   62 already hand-scripted elsewhere (agility_shortcuts_osrs.rs2 and others) — excluded, not double-bound
   57 dropped — category conflict or multi-placement collision, no unsafe fallback (see §3.3)
  182 rejected — see docs/MAPLINKS_REJECTS.md

Combined: 1,686 + 394 = 2,080 candidate rows in maplink.dbrow, minus 2 (one
climb row and one transition row sharing an origin tile with different
destinations — neither kept) = 2,078 rows. Agility's 144 live in a separate
maplink_agility.dbrow, since that table carries a fourth `level` column the
shared table doesn't.
```

`--check` writes nothing and exits non-zero if any generated file disagrees
with what the source data currently produces — the same "generated, do not
edit" contract `ladder_import.py --check` already has in this tree's
`test-port` gate. Because two of this importer's own bugs were only caught by
running it twice in a row (§3.1, §3.3), that is now part of what "verified"
means for this tool specifically, not just a byte-for-byte `--check` pass.

## 5. What is deliberately NOT in this pass, and why

`transports.tsv` itself carries more than climb verbs — `Cross` (732 rows,
mostly `Agility` obstacles, mostly covered separately by
`agility_shortcuts.tsv` §3.3), `Open`/adjacent-tile transitions (349 rows,
already `doors.rs2`/`gates.rs2`'s job). Cave mouths (`Enter`/`Exit`/`Board`),
the standalone portal/lever files, and Agility shortcuts are now covered
(§3.2, §3.3). What's still not touched, and why:

- **Doors, gates.** Already answered generically by loc shape/angle in
  `doors/scripts` and `general_use/scripts/gates.rs2`. Rebinding by name would
  silently take the "Close" op away from a door that also happens to verify.
  `maplink_import.py` reports disagreements; it does not rebind.
- **Wilderness obelisks.** Deliberately excluded even though 54 rows verify
  cleanly: `wilderness_obelisks.tsv` states an EMPTY `Destination` for every
  row — activating an obelisk teleports to a *random* other active obelisk in
  the network, not a fixed `src -> dest` pair. That is a different mechanic
  (pick one of N, excluding self, with a wilderness-level gate) than anything
  a coord-keyed table can represent, and is real future work rather than
  something this pipeline's data shape stretches to cover.
- **Grapple shortcuts.** The 15 multi-skill rows `parse_agility_level`
  rejects (§3.3) — Agility, Ranged and Strength together, for a
  crossbow-and-rope combination this importer does not model.
- **Minecarts, boats/ships.** Need npc dialogue and, for minecarts, a coin
  payment — a thin script over `maplink`-shaped data, not implemented here.
  56 minecart rows and a mix of boat/ship rows (some loc-keyed, some
  npc-keyed) already verify cleanly against this cache per §1's
  transportation table and are ready for that script the moment it exists.
- **Fairy rings, quetzals, magic mushtrees.** Every one needs a
  destination-picker interface this tree does not have yet. Their rows are
  real and would resolve against `maplink`-shaped data the moment that
  interface exists; until then they are out of scope rather than faked.
- **Canoes, spirit trees, gnome gliders, magic carpets, hot air balloons.**
  Landed since this section was written; see `docs/CANOES.md` and the
  `[opnpc]`/`[oploc]` destination menus in `server/scripts/`.
- **Charter ships.** Landed 2026-08-17 as `server/scripts/transport_charter/`,
  and it did *not* need a new picker — the cache already carries the whole one
  (interfaces 72 + 885, clientscripts 8940/8941/9104), which is the assumption
  this list got wrong. Sixteen ports, the wiki's fare matrix, and Trader Stan's
  Trading Post at each. `charter_ships.tsv` is used for the destination tiles
  and quest gates only: it disagrees with the wiki on cost and the wiki wins.
  The picker is the cache's own — interfaces 72 + 885, decoded back to a
  destination with `db_listall`/`db_findbyindex`, which is what this list
  assumed did not exist. Full record in `docs/transport/CHARTER_SHIPS.md`; only
  the voyage animation is still open there.
- **POH jewellery boxes and portals, Leagues seasonal transports, minigame
  grouping teleports.** No player-owned house, no Leagues mode, no grouping
  interface in this tree — out of scope, not deferred.
- **Teleport spells, teleport items.** `teleportation_items.tsv` keys an obj's
  own op, not a loc — a different importer's job. Spellbook teleports belong
  to `skill_magic`.

## 6. Verification performed

1. **The real compiler.** `sscompile` (`make -C src sscompile`), run via
   `make -C src torirsserver-scripts TORIRSSERVER_CONTENT_DIR=<content>` against every
   generated file across all three classes and the two hand-authored proc
   files (`ladders_stairs/scripts/maplink.rs2`,
   `skill_agility/scripts/maplink_agility.rs2`) plus the edited
   `ladders.rs2`. Result: **compiled 15,577 scripts, zero errors
   attributable to any of these files** (every remaining `note:`/`warning:`
   in the log is pre-existing, in files this change never touches —
   `poison.rs2`, `prayer.rs2`, `charge.rs2`, `%emote_access` varp-container
   reads).
2. **The real packer.** `cachepack pack --src <content> --server-only`
   accepts every new dbtable/dbrow with **0 unresolved names** in the server
   pack, and reports `category server pack: 119 name(s)` — up from 117
   before this work (117 → 118 for `maplink_transition`, 118 → 119 for
   `maplink_agility`), confirming both new categories resolved against their
   `pack/category.pack` entries rather than silently declaring an unbound
   category (see §3.2's underscore-spelling note — that mistake compiles
   clean either way, so this count is what actually catches it).
3. **`--check` is idempotent, verified by actually running it twice — not
   assumed.** That distinction mattered twice in this pipeline: §3.1's
   `my2arm_cliff_shortcut_1`/`_2` name-binding bug and §3.3's agility
   self-poisoning bug were BOTH found by generating output, generating it
   again, and diffing — not by review. A single-run `--check` pass would not
   have caught either, since both bugs were internally consistent within one
   run and only diverged on the second.
4. **Static self-consistency.** Every accepted row passed the identity check
   in §2 before being written; every rejected row is named in
   `docs/MAPLINKS_REJECTS.md`, not silently dropped.

**Not yet done: a live, in-client click-through of a specific link.** This
tree's headless repro lane (`TORIRS_SIM_CLICK_AT` + `TORIRS_EXIT_BMP`,
documented in `BUILD_AND_RUN.md`) needs either a working `::tele`-equivalent
debug command or a screenshot-guided walk-and-click loop to position the
player exactly on a `maplink` source tile; neither is wired up today (the
`ClientCheatHandler` in `torirs_server_world.c` recognizes `style`, `setlevel`,
`run`, `bank`, `fight`, `give`, `talk` — no coordinate-teleport command). A
future pass should add a `[debugproc,goto]`-style cheat (following the
pattern `cheat_equip.rs2`/`cheat_stat.rs2` already establish) specifically to
make this class of change spot-checkable without a full walk. Recommended
first spot checks once that exists: the Lumbridge Cook's trapdoor
(`qip_cook_trapdoor_open`, (3210,3216,0) → (3210,9616,0)), the goblin cave
entrance near Lumbridge swamp, and one near-miss staircase landing.

## 7. Files

| Path | What |
|---|---|
| `tools/maplink_import.py` | the generator |
| `tools/data/shortest_path/transports/*.tsv` | vendored upstream data, see `VENDORED.md` alongside them for the commit stamp |
| `docs/MAPLINKS.md` | this document |
| `docs/MAPLINKS_REJECTS.md` | generated — every dropped row and why, all three classes |
| `ladders_stairs/configs/maplink.dbtable` | schema, shared by climb and transition rows |
| `ladders_stairs/configs/maplink.dbrow` | generated rows, climb + transition combined |
| `ladders_stairs/configs/maplinks.loc` | generated — `category=maplink_transition` overlay |
| `ladders_stairs/scripts/maplink.rs2` | `~maplink_try`, `~maplink_transition`, the three category triggers |
| `ladders_stairs/scripts/maplink_shared.rs2` | generated — climb name+op bindings |
| `ladders_stairs/scripts/maplink_transitions_shared.rs2` | generated — transition name+op bindings |
| `ladders_stairs/scripts/ladders.rs2` | edited — 3 lines at the top of `~climb` |
| `skill_agility/configs/maplink_agility.dbtable` | schema — src/dest/loc/level |
| `skill_agility/configs/maplink_agility.dbrow` | generated — category-eligible agility rows only |
| `skill_agility/configs/maplink_agility.loc` | generated — `category=maplink_agility` overlay |
| `skill_agility/scripts/maplink_agility.rs2` | `~maplink_agility`, the three category triggers |
| `pack/category.pack` | edited — `8206=maplink_transition`, `8207=maplink_agility`, allocated by hand |
