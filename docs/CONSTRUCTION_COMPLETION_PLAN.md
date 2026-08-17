# Construction backend completion plan

> Status: active implementation plan; completed slices and remaining gates are tracked below.

## Goal and coverage contract

Finish the server-side Player-owned House (POH) and Construction implementation behind instances: durable house state, room composition, hotspot construction, all relevant interfaces, functional furniture, lifecycle and estate systems, servants, visitors, and cross-world Construction activities.

Complete means the revision-239 cache’s entire Construction skill guide is represented and every cache-backed entry has either working behavior or a documented external-system dependency. Appendix A is generated from configs/all.dbrow and lists all **537 entries across all 15 Construction skill-guide subsections**. Every entry links to an Old School RuneScape Wiki search, and every section links to a canonical Wiki topic.

The Wiki is live while this repo is a revision-239 snapshot. Treat cache IDs, interfaces, scripts, models, and visible unlock rows as implementation authority; use the Wiki for rules, materials, XP, restrictions, and behavior. Record current-Wiki/cache mismatches in a versioned crosswalk. Core references are [Construction](https://oldschool.runescape.wiki/w/Construction), [Construction level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table), [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items), [Player-owned house](https://oldschool.runescape.wiki/w/Player-owned_house), and [Construction training](https://oldschool.runescape.wiki/w/Construction_training).

## Audited starting point

| Area | What exists now | Completion gap |
|---|---|---|
| Estate and house identity | A starter purchase now creates durable state for a 1,000-coin Rimmington house; all nine cache-backed locations, relocation requirements/prices, and the level-99 cape sale are wired. | Add styles/redecoration and the cape perk routes. See [Estate agent](https://oldschool.runescape.wiki/w/Estate_agent), [house locations](https://oldschool.runescape.wiki/w/Player-owned_house#House_locations), and [Construction cape](https://oldschool.runescape.wiki/w/Construction_cape). |
| Instance lifecycle | Enter/leave, map allocation/freeing, persisted room composition/rotation, decoration restore, portal placement, and safe rebuilds exist. | Make scene/collision state per-instance, then support simultaneous owners and guests. See [House portal](https://oldschool.runescape.wiki/w/House_portal). |
| Building | Two small-plant hotspots offer six plants, consume plant/water, grant XP, replace the loc, and allow removal. | Replace the hard-coded choice with cache-backed menus and one transactional builder for every furniture/hotspot definition. See [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items). |
| Persistence | Versioned atomic state covers ownership/settings, rooms, decorations, servant fields, and money bag, with legacy starter migration. | Extend functional furniture metadata as those families are implemented and add migration/property coverage. |
| Rooms | The durable starter garden/parlour and newly purchased cache rooms compose with rotations and door masks. | Add preview/replacement, upper-floor support/connectivity, unique-room constraints, safe removal, roofs, and Viewer editing. See [POH rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms). |
| Interfaces | House Options and the native add-room panel have authoritative server callbacks; furniture creation and House Viewer remain incomplete. | Add cache-backed furniture selection/build/remove and full Viewer editing; guest/servant controls depend on their systems. See [House options](https://oldschool.runescape.wiki/w/Settings#House_options). |
| Functional content | Built locs have almost no broad gameplay behavior. | Implement utilities, storage, training, games, chapel, portals/nexus, trophies, dungeon, garden, menagerie, and remaining families. |
| Social POHs | House lock can toggle, but friend entry and guest behavior are absent. | Add owner lookup, private-state validation, doors, expulsion, guest restrictions, advertisement board, and parties. See [House portal](https://oldschool.runescape.wiki/w/House_portal) and [House Advertisement](https://oldschool.runescape.wiki/w/House_Advertisement). |
| Engine constraint | Instance allocation/chunk APIs exist, but collision is one scene per world. | Make collision/pathing instance-aware before enabling simultaneous isolated POHs or guests. |

Live content is in OSRS-Content/osrs239-content/server/scripts/skill_construction/. Its DO_NOT_PARK.txt marks the directory as live and protected. The active scripts are poh_build.rs2, poh_construct.rs2, poh_enter_leave.rs2, poh_estate_agent.rs2, and poh_debug.rs2. Related repository sources are docs/map_instances.md, docs/SKILLS_CONTENT_PORT_QUEUE.md, docs/SCAPE2009_CONTENT_PORT_QUEUE.md, docs/SKILLING_SOUNDS.md, and docs/PORTING_GUIDE.md.

## Source-of-truth map

| Subject | Implementation authority | Wiki behavior reference |
|---|---|---|
| Guide unlocks | 537 skill_features rows | [Level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table) |
| Furniture | 525 furniture rows: model, name, material costs, level, visibility, upgrades | [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) |
| Rooms and hotspots | 30 poh_room and 123 poh_hotspot rows | [POH rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms) |
| Building loop | Cache UI plus server validation | [Construction](https://oldschool.runescape.wiki/w/Construction) and [training](https://oldschool.runescape.wiki/w/Construction_training) |
| Estate and locations | Cache names/IDs plus server scripts | [Estate agent](https://oldschool.runescape.wiki/w/Estate_agent) and [locations](https://oldschool.runescape.wiki/w/Player-owned_house#House_locations) |
| Styles | Cache templates, locs, and models | [House styles](https://oldschool.runescape.wiki/w/House_styles) |
| Settings/access | Native varbits, interface 370, ownership state | [House options](https://oldschool.runescape.wiki/w/Settings#House_options) and [House portal](https://oldschool.runescape.wiki/w/House_portal) |
| Servants | Cache NPC/loc/animation IDs | [Servants’ Guild](https://oldschool.runescape.wiki/w/Servants%27_Guild) and [Butler](https://oldschool.runescape.wiki/w/Butler) |
| Portals | Cache teleport constants/interfaces | [Portal chamber](https://oldschool.runescape.wiki/w/Portal_chamber), [Portal space](https://oldschool.runescape.wiki/w/Portal_space), [Portal nexus](https://oldschool.runescape.wiki/w/Portal_nexus) |
| Storage | Cache inventories/interfaces | [Costume room](https://oldschool.runescape.wiki/w/Costume_room) and [STASH](https://oldschool.runescape.wiki/w/STASH) |
| External loops | Owning world modules and cache unlocks | [Mahogany Homes](https://oldschool.runescape.wiki/w/Mahogany_Homes), [STASH](https://oldschool.runescape.wiki/w/STASH), [Construction cape](https://oldschool.runescape.wiki/w/Construction_cape) |

Do not manually duplicate 525 furniture definitions or 123 hotspot lists. The runtime now imports the binary DBTABLE/DBROW records, and sscompile reads the exported `columndef=<index>:` schema directly. `tools/check_construction_catalog.py` validates the cache counts, schemas, tuple shapes, and room → hotspot → furniture references before packing. The checked-in [generated crosswalk](generated/construction_catalog_crosswalk.json) records every catalog row, all skill-guide rows and Wiki review links, reverse relationships, and every unbuilt hotspot/door placement measured from the room templates; CI fails when it is stale. `tools/update_construction_wiki_snapshot.py` is the opt-in network refresh for the checked-in factual Wiki snapshot; normal builds remain offline and reproducible.

## Implementation status

Implemented foundation (2026-08-16):

- Versioned, atomic POH save/load now covers ownership, location/style/access settings, rooms, decorations, servant state, and the servant money bag. Legacy ownership varps migrate on first use. This establishes the durable house required by [Player-owned house](https://oldschool.runescape.wiki/w/Player-owned_house).
- New houses persist the Wiki-documented starter garden and parlour instead of reconstructing a session-only garden. Room chunks are composed from persisted DB rows and rotations. See [Garden](https://oldschool.runescape.wiki/w/Garden_(Construction)) and [Parlour](https://oldschool.runescape.wiki/w/Parlour).
- The cache catalog gate covers all 525 furniture rows, 30 room rows, 123 hotspot rows, and all 537 Construction skill-guide rows. The compiler now consumes cache-exported database column schemas without a duplicate handwritten table. See [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) and [POH rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms).
- The generated cache/Wiki crosswalk inventories those 1,215 catalog and guide records, 1,013 room-template hotspot/door placements, material and requirement tuples, and 1,092 row-level Wiki links. A versioned snapshot of the Wiki’s 462-row [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) table and page revisions now resolves **462 of 525** cache furniture rows to XP and built loc variants by the exact cache menu-item/infobox-item ID. The remaining 63 stay explicitly unresolved instead of being name-guessed, so the rest of Phase 0 is machine-visible.
- Cache category fallbacks now give every remaining furniture and door hotspot Build action an explicit server response; exact implemented plant and Rimmington-door triggers take precedence. This meets the interface-safety rule while keeping unavailable entries honest rather than silently accepting or mutating incomplete state.
- The existing six small-garden plant choices now use a rollback-safe durable decoration transaction and are restored after re-entry. This is the first vertical slice of the generic build/remove system described by [Construction training](https://oldschool.runescape.wiki/w/Construction_training) and [Garden](https://oldschool.runescape.wiki/w/Garden_(Construction)).
- Durable room set/remove operations now validate edits, reject collisions and invalid records, compact room storage, remove decorations with a deleted room, and reindex later decorations. These are the repository operations needed by the [House Viewer](https://oldschool.runescape.wiki/w/House_Viewer).
- Interface 370 now has server-authoritative House Options callbacks. Building Mode safely recomposes the occupied instance; teleport-inside, default-build, and door choices persist with rollback; room count, leave, viewer gating, guest, and servant controls all respond. See [House options](https://oldschool.runescape.wiki/w/Settings#House_options).
- Estate agents now sell the 1,000-coin starter house, relocate it among all nine current Wiki locations using unboostable Construction requirements and the documented prices (including the distinct 5,000-coin Rimmington relocation fee), and sell the Construction cape and hood at base level 99 for 99,000 coins. Relocation writes the durable location atomically and refunds on failure. See [Construction](https://oldschool.runescape.wiki/w/Construction#Buying_a_house), [house locations](https://oldschool.runescape.wiki/w/Player-owned_house#House_locations), [Estate agent](https://oldschool.runescape.wiki/w/Estate_agent), and [Construction cape](https://oldschool.runescape.wiki/w/Construction_cape).
- Every cache-backed outdoor house portal now validates its own location and supports own-house and Building Mode entry. Teleport to House checks ownership before consuming runes and obeys the persisted inside/outside and default-Building-Mode settings, using the selected location's safe outdoor destination when required. Friend entry remains gated on social-house isolation. See [House portal](https://oldschool.runescape.wiki/w/House_portal), [Teleport to House](https://oldschool.runescape.wiki/w/Teleport_to_House), and [House options](https://oldschool.runescape.wiki/w/Settings#House_options).
- Rimmington door hotspots now open the native add-room interface and resolve all 27 visible room choices back to the 30-row cache catalog. The server revalidates ownership, instance, destination, boosted room level, cache price, floor restriction, the Wiki's 24–38 room progression, and 3×3–7×7 dimension progression before a rollback-safe coin/state/rebuild transaction. See [POH rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms) and [maximum rooms and area](https://oldschool.runescape.wiki/w/Player-owned_house#Maximum_number_of_rooms_and_area).
- Focused POH model tests, compiler schema tests, catalog validation, and a whole-player save/reload self-test cover this foundation.

Still open: deterministic built-loc/variant and XP resolution for the remaining 63 cache furniture rows, isolated per-instance scene/collision state, add-room preview/replacement and the complete furniture/House Viewer flows, generic material/tool/XP transactions, all functional furniture families, house styles/redecoration, friend entry and cape/tablet perk routes, guest/servant simulation, and external Construction systems. Those remain sequenced below.

## Target state

Add one versioned HouseState keyed by account and independent of active instance coordinates:

- identity: ownership, location, style/blueprint;
- layout: dimensions and room records containing grid coordinate, plane, rotation, room definition, doors, and support state;
- decorations: hotspot, furniture definition, upgrade lineage, orientation/variant, and durable functional metadata;
- access/settings: locked/open status, door mode, teleport-inside, and default build mode;
- servant state: type, hire/pay state, last task, delivery task, and money-bag balance;
- functional state: portal destinations, nexus unlocks, storage contents, pets, trophies, and furniture-specific data;
- schema version and migration data, including a legacy default that creates a garden and parlour.

Persist through the existing player save mechanism. Apply edits transactionally: validate immutable inputs, reserve/consume costs, mutate durable state, rebuild/replace scene content, then acknowledge. Restore model and inventory if scene construction fails.

## Implementation sequence

### Phase 0 — Catalog and coverage

1. **Partially implemented:** generate a crosswalk for all 525 furniture, 30 room, 123 hotspot, and 537 guide rows, including stable names, cache IDs, relationships, costs, requirements, XP, upgrades, and Wiki URLs. All rows are present; exact Wiki ID joins supply XP for 462 furniture rows.
2. **Partially implemented:** resolve furniture to unbuilt hotspots and built loc/model/rotation variants from cache/templates. The Wiki ID join resolves 462 rows and fails on ambiguity; 63 cache-only, cosmetic, trophy-upgrade, or external rows remain explicit.
3. Add integrity tests: every visible option has a hotspot, every upgrade source exists, every room hotspot resolves, and every guide row has an implementation feature or named dependency.
4. Compare behavior with [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) and the [level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table); record cache revision and Wiki review date.

Exit gate: reproducible counts of **525/30/123/537**, no unresolved mappings, and no hand-copied numeric catalog.

### Phase 1 — Durable house repository

1. Add versioned load, create, validate, save, and migrate operations.
2. Make the estate purchase create the Wiki-described [Garden](https://oldschool.runescape.wiki/w/Garden_(Construction)) and [Parlour](https://oldschool.runescape.wiki/w/Parlour) in Rimmington.
3. Persist construction/removal immediately and test re-entry, relog, restart, corrupt/old records, and rollback.
4. Exclude transient coordinates, spawned loc handles, and momentary UI state from saves.

Exit gate: a built object and edited layout survive leave, relog, restart, and re-entry without material duplication or loss.

### Phase 2 — Instance compositor and collision isolation

1. Replace the fixed garden with a compositor driven by persisted rooms; copy/rotate cache chunks, apply plane/style/roof/door variants, then layer hotspot/furniture state.
2. Implement adjacency, doorway compatibility, vertical support, stairs/ladders, basement/dungeon semantics, entrance preservation, and safe spawns using [POH rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms) and [Dungeon](https://oldschool.runescape.wiki/w/Dungeon_(Construction)).
3. Isolate collision, loc lookup, pathfinding, line-of-sight, and zone updates per instance/scene.
4. Test repeated entry/rebuild/disconnect and two owners with different simultaneous layouts.

Exit gate: two POHs cannot see, collide with, path through, or interact with each other’s state.

### Phase 3 — Interfaces and server contracts

1. Wire interface 458 furniture rows, requirements, selection, examine, and keybinds.
2. Wire interface 397 categories and interface 212 room choices, prices, restrictions, and responses.
3. Wire interface 422 viewer previews, move, rotate, and delete without committing invalid layouts.
4. Complete interface 370: viewer, building mode, teleport-inside, default building mode, doors, expel, leave, and call servant per [House options](https://oldschool.runescape.wiki/w/Settings#House_options).
5. Bind every event to owner, active instance, and hotspot; reject replay/stale inputs and always restore input state on close/error.

Exit gate: every cache-visible control responds or is deliberately disabled with a clear message; forged events cannot mutate state.

### Phase 4 — Generic build/remove transaction

1. Validate owner, build mode, instance, hotspot, distance/path, state, room compatibility, level/boost rules, prerequisites, and upgrade source.
2. Validate tools/materials; apply hammer/saw, nail bending, watering-can, and special-cost rules from [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items).
3. Lock player/hotspot; play the correct poh_build, saw, and hammer sounds from docs/SKILLING_SOUNDS.md; consume once; grant XP once; persist; then replace the loc.
4. Confirm removal, enforce special restrictions, refund only where required, persist, then restore the hotspot.
5. Make double-clicks, reconnects, menu replay, concurrent actions, and exceptions idempotent.

Exit gate: table tests cover success, validation failures, upgrades, removal, races, replay, and rollback.

### Phase 5 — Every visual construction choice

Enable all catalogued choices through the generic pipeline: surfaces and decoration; kitchen/workshop utilities; storage; trophies; games; garden/menagerie; chapel; dungeon; portals; gallery; stairs; entrances; doors; traps; cages; themes; and upgrades.

Use [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) as the behavior checklist, but expose only cache-backed definitions. Appendix A is the acceptance ledger.

Exit gate: all 525 furniture rows resolve, all 123 hotspot lists open correct choices, and all 537 guide rows have a visual/unlock test or named external dependency.

### Phase 6 — Functional furniture families

| Family | Backend behavior | Wiki references |
|---|---|---|
| Skills/kitchen/workshop | Cooking, water, repair/armour stand, lecterns/tablets, crafting, workbenches, telescope, scrying | [Kitchen](https://oldschool.runescape.wiki/w/Kitchen_(Construction)), [Workshop](https://oldschool.runescape.wiki/w/Workshop), [Study](https://oldschool.runescape.wiki/w/Study) |
| Storage/costume | Owner-only durable inventories, capacities/upgrades, ownership/security, withdraw/deposit UI | [Costume room](https://oldschool.runescape.wiki/w/Costume_room) |
| Trophies/gallery | Mount/remove trophies, cape hanger, jewellery boxes, altars, pools, diary/quest/boss displays | [Achievement Gallery](https://oldschool.runescape.wiki/w/Achievement_Gallery), [Cape hanger](https://oldschool.runescape.wiki/w/Cape_hanger) |
| Games/combat | Rings, targets, dummies, prizes, scoring/reset, safe death, participant cleanup | [Games room](https://oldschool.runescape.wiki/w/Games_room), [Combat room](https://oldschool.runescape.wiki/w/Combat_room) |
| Garden/menagerie | Plants, pets, habitats/themes, pools, obelisk, fairy ring/spirit tree, tip jar | [Superior Garden](https://oldschool.runescape.wiki/w/Superior_Garden), [Menagerie](https://oldschool.runescape.wiki/w/Menagerie) |
| Dungeon | Doors, levers, traps, cages, guards, chests, combat/death/exit, collision updates | [Dungeon](https://oldschool.runescape.wiki/w/Dungeon_(Construction)), [Oubliette](https://oldschool.runescape.wiki/w/Oubliette) |
| Chapel | Altars/burners, bone XP/modifiers, icons, lectern/organ/windows | [Chapel](https://oldschool.runescape.wiki/w/Chapel), [Gilded altar](https://oldschool.runescape.wiki/w/Gilded_altar) |
| Portals/nexus | Unlock/payment, destination assignment, scrying, teleport UI, capacity/upgrades, guest use | [Portal space](https://oldschool.runescape.wiki/w/Portal_space), [Portal nexus](https://oldschool.runescape.wiki/w/Portal_nexus) |
| Servants | Hiring, bedroom gate, follow/call, deliveries, tasks, limits, wage, dismissal | [Servants’ Guild](https://oldschool.runescape.wiki/w/Servants%27_Guild), [Butler](https://oldschool.runescape.wiki/w/Butler), [Demon butler](https://oldschool.runescape.wiki/w/Demon_butler) |
| Sailing/boats | Cache-visible repair kits/facilities through a Sailing-owned API | [Construction level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table) |

Exit gate: each family has owner, guest, requirements, upgrade, persistence, and teardown tests.

### Phase 7 — Room editor and structure

1. Implement purchase/build/remove/move/rotate through add-room and viewer interfaces.
2. Enforce requirements, current room caps (24 at level 1 through 38 at 99), and grid progression (3×3 at 1, 4×4 at 15, 5×5 at 30, 6×6 at 45, 7×7 at 60) from [house dimensions](https://oldschool.runescape.wiki/w/Player-owned_house#House_dimensions).
3. Enforce support, floor/roof rules, doorway connectivity, vertical extent, accessibility, and one-only rooms such as [Portal nexus](https://oldschool.runescape.wiki/w/Portal_nexus).
4. Preserve or deliberately migrate room contents. Block destructive removal until storage, pets, trophies, and portals have defined handling and clear confirmation.

Exit gate: property tests cannot create unsupported, overlapping, disconnected, out-of-bounds, or content-losing houses.

### Phase 8 — Estate, access, servants, and social houses

1. **Implemented:** cache support, durable ordinals, outdoor portal destinations, costs, and unboostable prerequisites are recorded for the Wiki’s Rimmington, Taverley, Pollnivneach, Hosidius, Rellekka, Aldarin, Brimhaven, Yanille, and Prifddinas [house locations](https://oldschool.runescape.wiki/w/Player-owned_house#House_locations).
2. Cross-check [House styles](https://oldschool.runescape.wiki/w/House_styles): Basic wood, Basic stone, Whitewashed stone, Fremennik-style wood, Tropical wood, Fancy stone, Deathly mansion, Cosy Cabin, Twisted, Hosidius, Canifis, Civitas, Wilderness. Expose only styles with cache assets and unlock provenance.
3. **Partially implemented:** [House portal](https://oldschool.runescape.wiki/w/House_portal) own/build/cancel routes work at all nine locations, and the spell obeys teleport settings. Friend entry, tablets, and cape routes remain.
4. Add online/private validation, door modes, guest capacity, expulsion, safe exits, advertisement listing, and privacy changes while occupied.
5. Implement Rick, Maid, Cook, Butler, and Demon Butler through [Servants’ Guild](https://oldschool.runescape.wiki/w/Servants%27_Guild).
6. **Sale implemented:** estate agents sell the [Construction cape](https://oldschool.runescape.wiki/w/Construction_cape) and hood for 99,000 coins at base level 99; perk teleports remain.

Exit gate: multiple owners/guests can enter, leave, be expelled, disconnect, and rejoin without leakage or item loss.

### Phase 9 — External Construction systems

- [Mahogany Homes](https://oldschool.runescape.wiki/w/Mahogany_Homes): all contract tiers, nodes, materials, rewards, and XP.
- [STASH](https://oldschool.runescape.wiki/w/STASH): easy through master units, charts, storage, and checks.
- Guide-listed world work: Piscarilius cranes, Mausoleum/Hallowed bridges, rowboat, bank chest, and Chambers of Xeric storage.
- Quest, diary, league, and event gates for trophies, styles, portals, altars, fairy rings, spirit trees, and related unlocks.
- Sailing repair kits and facilities through the Sailing-owned API.

Exit gate: each Appendix A Other and Boats row has an owning module and end-to-end test.

### Phase 10 — Verification and release

1. Add catalog snapshot, migration, round-trip, replay, layout, collision, access, and functional-family tests.
2. Add a headless self-test that buys a house; checks starter rooms; builds/upgrades/removes; edits; saves/reloads; enters as a guest; cleans up.
3. Compile and pack scripts/configs, run the server suite, and run git diff --check.
4. Client-smoke interfaces 212, 370, 397, 422, and 458, including keybinds, cancel/error paths, previews, and stale responses.
5. Generate coverage with independent catalogued, constructible/unlocked, functional, and persistent/integration-tested columns.

Release requires 100% catalog mapping, no unowned dependencies, no session-only POH state, no cross-instance collision leaks, and tested social/destructive flows.

## Dependency order

| Order | Work package | Depends on |
|---:|---|---|
| 1 | Catalog generator/crosswalk | None |
| 2 | State repository/migrations | Catalog contract |
| 3 | Instance-aware collision/scene ownership | Instance APIs |
| 4 | Room compositor/starter layout | State + collision |
| 5 | Generic construction transaction | Catalog + state + compositor |
| 6 | Interfaces/viewer | Catalog + state contract |
| 7 | Visual options/room editor | Transaction + UI + compositor |
| 8 | Functional families | Visuals + persistence |
| 9 | Guests/social houses | Instance isolation + access |
| 10 | External systems and release | Stable ownership APIs |

## Known blockers and decisions

| Risk | Required resolution |
|---|---|
| One collision scene per world | Per-instance collision/loc/path/LOS isolation is a hard prerequisite for simultaneous POHs and guests. |
| Exported DB grammar differs from server-authored grammar | Resolved for compiler/runtime queries by accepting indexed `columndef=` exports; keep the catalog gate mandatory and never hand-copy hundreds of rows. |
| Furniture rows do not prove every built-loc mapping | Generate from cache/templates, compare old 2009Scape only by symbolic meaning, fail ambiguity, and never copy foreign IDs. |
| Live Wiki versus revision-239 drift | Pin Wiki review date/cache revision and classify each mismatch as supported, behavior-only, external, or excluded with reason. |
| Sailing/recent POH additions | Route cache-visible Boats rows to Sailing; gate Wiki-only additions until assets and provenance exist. |
| Destructive edits and valuable storage | Define move/remove/refund behavior before enabling removal; require confirmation and atomic persistence. |

## Definition of done

- All 537 Appendix A rows are implemented or have an explicit tested dependency.
- All 525 furniture rows, 123 hotspot lists, and 30 room definitions are mapped without duplicated hand-coded data.
- The starter garden/parlour and all edits survive re-entry, relog, and restart.
- Construction/viewer menus, House Options, portals, servants, estate systems, storage, and furniture work through revision-239 interfaces.
- Materials, tools, requirements, upgrades, animation/sound, XP, removal, and refunds match cache plus cited Wiki behavior.
- Owners and guests are isolated across scene, collision, pathing, loc lookup, messages, and persistence.
- Ordinary gameplay needs no debug command, and all coverage/migration/transaction/layout/access/client tests pass.

## Appendix A — complete cache skill-guide inventory (537 entries)

This inventory is extracted from the revision-239 skill_features table. Counts include the five prose rows because they define the client-visible onboarding contract. Exact cache labels use Wiki search links so name drift and multi-concept rows remain discoverable.

| Section | Rows | Canonical Wiki reference |
|---|---:|---|
| Overview | 5 | [Construction](https://oldschool.runescape.wiki/w/Construction) |
| Rooms | 25 | [Player-owned house — rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms) |
| Skills | 35 | [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) |
| Surfaces | 38 | [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) |
| Storage | 73 | [Costume room and storage](https://oldschool.runescape.wiki/w/Costume_room) |
| Decorative | 39 | [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) |
| Trophies | 60 | [Achievement Gallery](https://oldschool.runescape.wiki/w/Achievement_Gallery) |
| Games | 29 | [Games room](https://oldschool.runescape.wiki/w/Games_room) |
| Garden | 64 | [Garden](https://oldschool.runescape.wiki/w/Garden_(Construction)) |
| Dungeon | 40 | [Dungeon](https://oldschool.runescape.wiki/w/Dungeon_(Construction)) |
| Chapel | 31 | [Chapel](https://oldschool.runescape.wiki/w/Chapel) |
| Other | 65 | [Construction level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table) |
| Servants | 6 | [Servants’ Guild](https://oldschool.runescape.wiki/w/Servants%27_Guild) |
| House Size | 20 | [Player-owned house — dimensions](https://oldschool.runescape.wiki/w/Player-owned_house#House_dimensions) |
| Boats | 7 | [Construction level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table) |

### Overview (5)

Canonical reference: [Construction](https://oldschool.runescape.wiki/w/Construction).

| Level | Skill-guide entry |
|---:|---|
| — | [Construction gives you access to your very owned Player-Owned House. To get started, you'll need to buy a house from an Estate agent.](https://oldschool.runescape.wiki/w/Special:Search?search=Construction%20gives%20you%20access%20to%20your%20very%20owned%20Player-Owned%20House.%20To%20get%20started%2C%20you%27ll%20need%20to%20buy%20a%20house%20from%20an%20Estate%20agent.) |
| — | [Once you have a house, you'll need to build furniture. Most furniture can be built with planks, which can be obtained via taking logs to a sawmill.](https://oldschool.runescape.wiki/w/Special:Search?search=Once%20you%20have%20a%20house%2C%20you%27ll%20need%20to%20build%20furniture.%20Most%20furniture%20can%20be%20built%20with%20planks%2C%20which%20can%20be%20obtained%20via%20taking%20logs%20to%20a%20sawmill.) |
| — | [To build furniture, grab a hammer and a saw, enter your house in build mode, select a hotspot and select the furniture you want to build.](https://oldschool.runescape.wiki/w/Special:Search?search=To%20build%20furniture%2C%20grab%20a%20hammer%20and%20a%20saw%2C%20enter%20your%20house%20in%20build%20mode%2C%20select%20a%20hotspot%20and%20select%20the%20furniture%20you%20want%20to%20build.) |
| — | [As your Construction level increases, you'll be able to build bigger houses, gain access to more kinds of room in your house, and be able to build fancier furniture.](https://oldschool.runescape.wiki/w/Special:Search?search=As%20your%20Construction%20level%20increases%2C%20you%27ll%20be%20able%20to%20build%20bigger%20houses%2C%20gain%20access%20to%20more%20kinds%20of%20room%20in%20your%20house%2C%20and%20be%20able%20to%20build%20fancier%20furniture.) |
| — | [You'll also become able to build better facilities for boats.](https://oldschool.runescape.wiki/w/Special:Search?search=You%27ll%20also%20become%20able%20to%20build%20better%20facilities%20for%20boats.) |

### Rooms (25)

Canonical reference: [Player-owned house — rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms).

| Level | Skill-guide entry |
|---:|---|
| 1 | [Garden](https://oldschool.runescape.wiki/w/Special:Search?search=Garden) |
| 1 | [Parlour](https://oldschool.runescape.wiki/w/Special:Search?search=Parlour) |
| 5 | [Kitchen](https://oldschool.runescape.wiki/w/Special:Search?search=Kitchen) |
| 10 | [Dining room](https://oldschool.runescape.wiki/w/Special:Search?search=Dining%20room) |
| 15 | [Workshop](https://oldschool.runescape.wiki/w/Special:Search?search=Workshop) |
| 20 | [Bedroom](https://oldschool.runescape.wiki/w/Special:Search?search=Bedroom) |
| 25 | [Hall (skill trophies)](https://oldschool.runescape.wiki/w/Special:Search?search=Hall%20%28skill%20trophies%29) |
| 27 | [League hall](https://oldschool.runescape.wiki/w/Special:Search?search=League%20hall) |
| 30 | [Games room](https://oldschool.runescape.wiki/w/Special:Search?search=Games%20room) |
| 32 | [Combat room](https://oldschool.runescape.wiki/w/Special:Search?search=Combat%20room) |
| 35 | [Hall (quest trophies)](https://oldschool.runescape.wiki/w/Special:Search?search=Hall%20%28quest%20trophies%29) |
| 37 | [Menagerie](https://oldschool.runescape.wiki/w/Special:Search?search=Menagerie) |
| 40 | [Study](https://oldschool.runescape.wiki/w/Special:Search?search=Study) |
| 42 | [Costume room](https://oldschool.runescape.wiki/w/Special:Search?search=Costume%20room) |
| 45 | [Chapel](https://oldschool.runescape.wiki/w/Special:Search?search=Chapel) |
| 50 | [Portal chamber](https://oldschool.runescape.wiki/w/Special:Search?search=Portal%20chamber) |
| 50 | [Use the Advertisement noticeboard](https://oldschool.runescape.wiki/w/Special:Search?search=Use%20the%20Advertisement%20noticeboard) |
| 55 | [Formal garden](https://oldschool.runescape.wiki/w/Special:Search?search=Formal%20garden) |
| 60 | [Throne room](https://oldschool.runescape.wiki/w/Special:Search?search=Throne%20room) |
| 65 | [Oubliette](https://oldschool.runescape.wiki/w/Special:Search?search=Oubliette) |
| 65 | [Superior garden](https://oldschool.runescape.wiki/w/Special:Search?search=Superior%20garden) |
| 70 | [Dungeon](https://oldschool.runescape.wiki/w/Special:Search?search=Dungeon) |
| 72 | [Portal nexus](https://oldschool.runescape.wiki/w/Special:Search?search=Portal%20nexus) |
| 75 | [Treasure room](https://oldschool.runescape.wiki/w/Special:Search?search=Treasure%20room) |
| 80 | [Achievement Gallery](https://oldschool.runescape.wiki/w/Special:Search?search=Achievement%20Gallery) |

### Skills (35)

Canonical reference: [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items).

| Level | Skill-guide entry |
|---:|---|
| 3 | [Clay fireplace](https://oldschool.runescape.wiki/w/Special:Search?search=Clay%20fireplace) |
| 5 | [Firepit](https://oldschool.runescape.wiki/w/Special:Search?search=Firepit) |
| 7 | [Pump and drain](https://oldschool.runescape.wiki/w/Special:Search?search=Pump%20and%20drain) |
| 11 | [Firepit with hook](https://oldschool.runescape.wiki/w/Special:Search?search=Firepit%20with%20hook) |
| 15 | [Repair bench](https://oldschool.runescape.wiki/w/Special:Search?search=Repair%20bench) |
| 16 | [Pluming stand](https://oldschool.runescape.wiki/w/Special:Search?search=Pluming%20stand) |
| 16 | [Crafting table 1](https://oldschool.runescape.wiki/w/Special:Search?search=Crafting%20table%201) |
| 17 | [Firepit with pot](https://oldschool.runescape.wiki/w/Special:Search?search=Firepit%20with%20pot) |
| 17 | [Wooden workbench](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20workbench) |
| 24 | [Small oven](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20oven) |
| 25 | [Crafting table 2](https://oldschool.runescape.wiki/w/Special:Search?search=Crafting%20table%202) |
| 27 | [Pump and tub](https://oldschool.runescape.wiki/w/Special:Search?search=Pump%20and%20tub) |
| 29 | [Large oven](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20oven) |
| 32 | [Oak workbench](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20workbench) |
| 33 | [Stone fireplace](https://oldschool.runescape.wiki/w/Special:Search?search=Stone%20fireplace) |
| 34 | [Crafting table 3](https://oldschool.runescape.wiki/w/Special:Search?search=Crafting%20table%203) |
| 34 | [Steel range](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20range) |
| 35 | [Whetstone](https://oldschool.runescape.wiki/w/Special:Search?search=Whetstone) |
| 41 | [Shield easel](https://oldschool.runescape.wiki/w/Special:Search?search=Shield%20easel) |
| 42 | [Fancy range](https://oldschool.runescape.wiki/w/Special:Search?search=Fancy%20range) |
| 42 | [Crafting table 4](https://oldschool.runescape.wiki/w/Special:Search?search=Crafting%20table%204) |
| 46 | [Steel framed workbench](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20framed%20workbench) |
| 47 | [Sink](https://oldschool.runescape.wiki/w/Special:Search?search=Sink) |
| 47 | [Gold Sink](https://oldschool.runescape.wiki/w/Special:Search?search=Gold%20Sink) |
| 55 | [Armour stand](https://oldschool.runescape.wiki/w/Special:Search?search=Armour%20stand) |
| 62 | [Workbench with vice](https://oldschool.runescape.wiki/w/Special:Search?search=Workbench%20with%20vice) |
| 63 | [Marble fireplace](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20fireplace) |
| 66 | [Banner easel](https://oldschool.runescape.wiki/w/Special:Search?search=Banner%20easel) |
| 77 | [Workbench with lathe](https://oldschool.runescape.wiki/w/Special:Search?search=Workbench%20with%20lathe) |
| 80 | [Ancient Altar](https://oldschool.runescape.wiki/w/Special:Search?search=Ancient%20Altar) |
| 80 | [Lunar Altar](https://oldschool.runescape.wiki/w/Special:Search?search=Lunar%20Altar) |
| 80 | [Dark Altar](https://oldschool.runescape.wiki/w/Special:Search?search=Dark%20Altar) |
| 90 | [Occult Altar (upgrade from Ancient)](https://oldschool.runescape.wiki/w/Special:Search?search=Occult%20Altar%20%28upgrade%20from%20Ancient%29) |
| 90 | [Occult Altar (upgrade from Lunar)](https://oldschool.runescape.wiki/w/Special:Search?search=Occult%20Altar%20%28upgrade%20from%20Lunar%29) |
| 90 | [Occult Altar (upgrade from Dark)](https://oldschool.runescape.wiki/w/Special:Search?search=Occult%20Altar%20%28upgrade%20from%20Dark%29) |

### Surfaces (38)

Canonical reference: [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items).

| Level | Skill-guide entry |
|---:|---|
| 1 | [Crude wooden chair](https://oldschool.runescape.wiki/w/Special:Search?search=Crude%20wooden%20chair) |
| 8 | [Wooden chair](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20chair) |
| 10 | [Wooden dining table](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20dining%20table) |
| 10 | [Wooden dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20dining%20bench) |
| 12 | [Wooden kitchen table](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20kitchen%20table) |
| 14 | [Rocking chair](https://oldschool.runescape.wiki/w/Special:Search?search=Rocking%20chair) |
| 19 | [Oak chair](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20chair) |
| 20 | [Wooden bed](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20bed) |
| 22 | [Oak dining table](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20dining%20table) |
| 22 | [Oak dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20dining%20bench) |
| 26 | [Oak armchair](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20armchair) |
| 30 | [Oak bed](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20bed) |
| 31 | [Carved oak dining table](https://oldschool.runescape.wiki/w/Special:Search?search=Carved%20oak%20dining%20table) |
| 31 | [Carved oak dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Carved%20oak%20dining%20bench) |
| 32 | [Oak kitchen table](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20kitchen%20table) |
| 34 | [Large oak bed](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20oak%20bed) |
| 35 | [Teak armchair](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20armchair) |
| 38 | [Teak dining table](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20dining%20table) |
| 38 | [Teak dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20dining%20bench) |
| 40 | [Teak bed](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20bed) |
| 44 | [Carved teak dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Carved%20teak%20dining%20bench) |
| 45 | [Carved teak dining table](https://oldschool.runescape.wiki/w/Special:Search?search=Carved%20teak%20dining%20table) |
| 45 | [Large teak bed](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20teak%20bed) |
| 50 | [Mahogany armchair](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20armchair) |
| 52 | [Mahogany dining table](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20dining%20table) |
| 52 | [Mahogany dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20dining%20bench) |
| 52 | [Teak kitchen table](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20kitchen%20table) |
| 53 | [Mahogany four-poster bed](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20four-poster%20bed) |
| 60 | [Oak throne](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20throne) |
| 60 | [Gilded mahogany four-poster bed](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20four-poster%20bed) |
| 61 | [Gilded mahogany dining bench](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20dining%20bench) |
| 67 | [Teak throne](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20throne) |
| 72 | [Gilded mahogany and marble table](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20and%20marble%20table) |
| 74 | [Mahogany throne](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20throne) |
| 81 | [Gilded mahogany throne](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20throne) |
| 88 | [Skeleton throne](https://oldschool.runescape.wiki/w/Special:Search?search=Skeleton%20throne) |
| 95 | [Crystal throne](https://oldschool.runescape.wiki/w/Special:Search?search=Crystal%20throne) |
| 99 | [Demonic throne](https://oldschool.runescape.wiki/w/Special:Search?search=Demonic%20throne) |

### Storage (73)

Canonical reference: [Costume room and storage](https://oldschool.runescape.wiki/w/Costume_room).

| Level | Skill-guide entry |
|---:|---|
| 4 | [Wooden bookcase](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20bookcase) |
| 6 | [Wooden shelves 1](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20shelves%201) |
| 7 | [Beer barrel](https://oldschool.runescape.wiki/w/Special:Search?search=Beer%20barrel) |
| 9 | [Wooden larder](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20larder) |
| 12 | [Cider barrel](https://oldschool.runescape.wiki/w/Special:Search?search=Cider%20barrel) |
| 12 | [Wooden shelves 2](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20shelves%202) |
| 15 | [Tool store 1](https://oldschool.runescape.wiki/w/Special:Search?search=Tool%20store%201) |
| 18 | [Asgarnian Ale barrel](https://oldschool.runescape.wiki/w/Special:Search?search=Asgarnian%20Ale%20barrel) |
| 20 | [Shoe box](https://oldschool.runescape.wiki/w/Special:Search?search=Shoe%20box) |
| 21 | [Wooden shaving stand](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20shaving%20stand) |
| 23 | [Wooden shelves 3](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20shelves%203) |
| 25 | [Tool store 2](https://oldschool.runescape.wiki/w/Special:Search?search=Tool%20store%202) |
| 26 | [Greenman's Ale barrel](https://oldschool.runescape.wiki/w/Special:Search?search=Greenman%27s%20Ale%20barrel) |
| 27 | [Oak chest of drawers](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20chest%20of%20drawers) |
| 29 | [Oak bookcase](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20bookcase) |
| 29 | [Oak shaving stand](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20shaving%20stand) |
| 33 | [Oak larder](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20larder) |
| 34 | [Oak shelves 1](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20shelves%201) |
| 35 | [Tool store 3](https://oldschool.runescape.wiki/w/Special:Search?search=Tool%20store%203) |
| 36 | [Dragon Bitter barrel](https://oldschool.runescape.wiki/w/Special:Search?search=Dragon%20Bitter%20barrel) |
| 37 | [Oak pet house (3 pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20pet%20house%20%283%20pets%29) |
| 37 | [Oak dresser](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20dresser) |
| 39 | [Oak wardrobe (bedroom)](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20wardrobe%20%28bedroom%29) |
| 40 | [Mahogany bookcase](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20bookcase) |
| 42 | [Oak wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20wardrobe%20%28costume%20room%29) |
| 43 | [Teak larder](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20larder) |
| 44 | [Oak fancy dress box](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20fancy%20dress%20box) |
| 44 | [Tool store 4](https://oldschool.runescape.wiki/w/Special:Search?search=Tool%20store%204) |
| 45 | [Oak shelves 2](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20shelves%202) |
| 46 | [Oak armour case](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20armour%20case) |
| 46 | [Teak dresser](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20dresser) |
| 48 | [Chef's Delight barrel](https://oldschool.runescape.wiki/w/Special:Search?search=Chef%27s%20Delight%20barrel) |
| 48 | [Oak treasure chest](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20treasure%20chest) |
| 48 | [Teak pet house (5 pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20pet%20house%20%285%20pets%29) |
| 50 | [Oak toy box](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20toy%20box) |
| 51 | [Carved oak wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Carved%20oak%20wardrobe%20%28costume%20room%29) |
| 51 | [Teak chest of drawers](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20chest%20of%20drawers) |
| 54 | [Oak cape rack](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20cape%20rack) |
| 55 | [Tool store 5](https://oldschool.runescape.wiki/w/Special:Search?search=Tool%20store%205) |
| 56 | [Teak shelves 1](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20shelves%201) |
| 56 | [Fancy teak dresser](https://oldschool.runescape.wiki/w/Special:Search?search=Fancy%20teak%20dresser) |
| 58 | [Servant's money bag](https://oldschool.runescape.wiki/w/Special:Search?search=Servant%27s%20money%20bag) |
| 59 | [Mahogany pet house (7 pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20pet%20house%20%287%20pets%29) |
| 60 | [Teak wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20wardrobe%20%28costume%20room%29) |
| 60 | [Spice Rack](https://oldschool.runescape.wiki/w/Special:Search?search=Spice%20Rack) |
| 62 | [Teak fancy dress box](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20fancy%20dress%20box) |
| 63 | [Teak cape rack](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20cape%20rack) |
| 63 | [Teak wardrobe (bedroom)](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20wardrobe%20%28bedroom%29) |
| 64 | [Teak armour case](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20armour%20case) |
| 64 | [Mahogany dresser](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20dresser) |
| 66 | [Teak treasure chest](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20treasure%20chest) |
| 67 | [Teak shelves 2](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20shelves%202) |
| 68 | [Teak toy box](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20toy%20box) |
| 69 | [Carved teak wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Carved%20teak%20wardrobe%20%28costume%20room%29) |
| 70 | [Consecrated pet house (9 pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Consecrated%20pet%20house%20%289%20pets%29) |
| 72 | [Mahogany cape rack](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20cape%20rack) |
| 74 | [Gilded Mahogany dresser](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20Mahogany%20dresser) |
| 75 | [Mahogany wardrobe (bedroom)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20wardrobe%20%28bedroom%29) |
| 78 | [Mahogany wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20wardrobe%20%28costume%20room%29) |
| 80 | [Mahogany fancy dress box](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20fancy%20dress%20box) |
| 81 | [Basic jewellery box](https://oldschool.runescape.wiki/w/Special:Search?search=Basic%20jewellery%20box) |
| 81 | [Gilded mahogany cape rack](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20cape%20rack) |
| 81 | [Desecrated pet house (12 pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Desecrated%20pet%20house%20%2812%20pets%29) |
| 82 | [Mahogany armour case](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20armour%20case) |
| 86 | [Fancy jewellery box](https://oldschool.runescape.wiki/w/Special:Search?search=Fancy%20jewellery%20box) |
| 86 | [Mahogany toy box](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20toy%20box) |
| 87 | [Gilded mahogany wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20wardrobe%20%28costume%20room%29) |
| 87 | [Gilded mahogany wardrobe (bedroom)](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20wardrobe%20%28bedroom%29) |
| 90 | [Marble cape rack](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20cape%20rack) |
| 91 | [Ornate jewellery box](https://oldschool.runescape.wiki/w/Special:Search?search=Ornate%20jewellery%20box) |
| 92 | [Natural pet house (many pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Natural%20pet%20house%20%28many%20pets%29) |
| 96 | [Marble wardrobe (costume room)](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20wardrobe%20%28costume%20room%29) |
| 99 | [Magic stone cape rack](https://oldschool.runescape.wiki/w/Special:Search?search=Magic%20stone%20cape%20rack) |

### Decorative (39)

Canonical reference: [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items).

| Level | Skill-guide entry |
|---:|---|
| 2 | [Brown rug](https://oldschool.runescape.wiki/w/Special:Search?search=Brown%20rug) |
| 2 | [Torn curtains](https://oldschool.runescape.wiki/w/Special:Search?search=Torn%20curtains) |
| 13 | [Rug](https://oldschool.runescape.wiki/w/Special:Search?search=Rug) |
| 18 | [Curtains](https://oldschool.runescape.wiki/w/Special:Search?search=Curtains) |
| 25 | [Oak clock](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20clock) |
| 28 | [Deadman rug](https://oldschool.runescape.wiki/w/Special:Search?search=Deadman%20rug) |
| 40 | [Oak lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20lectern) |
| 40 | [Opulent curtains](https://oldschool.runescape.wiki/w/Special:Search?search=Opulent%20curtains) |
| 40 | [S.T.A.S.H chart](https://oldschool.runescape.wiki/w/Special:Search?search=S.T.A.S.H%20chart) |
| 41 | [Globe](https://oldschool.runescape.wiki/w/Special:Search?search=Globe) |
| 43 | [Alchemical chart](https://oldschool.runescape.wiki/w/Special:Search?search=Alchemical%20chart) |
| 44 | [Oak telescope](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20telescope) |
| 47 | [Oak eagle lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20eagle%20lectern) |
| 47 | [Oak demon lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20demon%20lectern) |
| 50 | [Ornamental globe](https://oldschool.runescape.wiki/w/Special:Search?search=Ornamental%20globe) |
| 55 | [Teak clock](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20clock) |
| 57 | [Teak eagle lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20eagle%20lectern) |
| 57 | [Teak demon lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20demon%20lectern) |
| 59 | [Lunar globe](https://oldschool.runescape.wiki/w/Special:Search?search=Lunar%20globe) |
| 63 | [Astronomical chart](https://oldschool.runescape.wiki/w/Special:Search?search=Astronomical%20chart) |
| 64 | [Teak telescope](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20telescope) |
| 65 | [Opulent rug](https://oldschool.runescape.wiki/w/Special:Search?search=Opulent%20rug) |
| 67 | [Mahogany eagle lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20eagle%20lectern) |
| 67 | [Mahogany demon lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20demon%20lectern) |
| 68 | [Celestial globe](https://oldschool.runescape.wiki/w/Special:Search?search=Celestial%20globe) |
| 72 | [Dungeon candles](https://oldschool.runescape.wiki/w/Special:Search?search=Dungeon%20candles) |
| 72 | [Decorative dungeon bloodstain](https://oldschool.runescape.wiki/w/Special:Search?search=Decorative%20dungeon%20bloodstain) |
| 73 | [Raging Echoes rug](https://oldschool.runescape.wiki/w/Special:Search?search=Raging%20Echoes%20rug) |
| 77 | [Armillary sphere](https://oldschool.runescape.wiki/w/Special:Search?search=Armillary%20sphere) |
| 77 | [Marble lectern](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20lectern) |
| 83 | [Infernal chart](https://oldschool.runescape.wiki/w/Special:Search?search=Infernal%20chart) |
| 83 | [Decorative dungeon pipe](https://oldschool.runescape.wiki/w/Special:Search?search=Decorative%20dungeon%20pipe) |
| 84 | [Dungeon torches](https://oldschool.runescape.wiki/w/Special:Search?search=Dungeon%20torches) |
| 84 | [Mahogany telescope](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20telescope) |
| 85 | [Gilded mahogany clock](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20clock) |
| 86 | [Small orrery](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20orrery) |
| 94 | [Hanging dungeon skeleton](https://oldschool.runescape.wiki/w/Special:Search?search=Hanging%20dungeon%20skeleton) |
| 94 | [Dungeon skull torches](https://oldschool.runescape.wiki/w/Special:Search?search=Dungeon%20skull%20torches) |
| 95 | [Large orrery](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20orrery) |

### Trophies (60)

Canonical reference: [Achievement Gallery](https://oldschool.runescape.wiki/w/Achievement_Gallery).

| Level | Skill-guide entry |
|---:|---|
| 16 | [Oak wall decoration](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20wall%20decoration) |
| 27 | [Trophy pedestal](https://oldschool.runescape.wiki/w/Special:Search?search=Trophy%20pedestal) |
| 28 | [Rug (League hall)](https://oldschool.runescape.wiki/w/Special:Search?search=Rug%20%28League%20hall%29) |
| 28 | [Trailblazer rug](https://oldschool.runescape.wiki/w/Special:Search?search=Trailblazer%20rug) |
| 28 | [Suit of Armour](https://oldschool.runescape.wiki/w/Special:Search?search=Suit%20of%20Armour) |
| 30 | [Banner stand](https://oldschool.runescape.wiki/w/Special:Search?search=Banner%20stand) |
| 32 | [League statue](https://oldschool.runescape.wiki/w/Special:Search?search=League%20statue) |
| 32 | [Trailblazer globe](https://oldschool.runescape.wiki/w/Special:Search?search=Trailblazer%20globe) |
| 34 | [Oak outfit stand](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20outfit%20stand) |
| 35 | [Small portrait](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20portrait) |
| 36 | [Oak trophy case](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20trophy%20case) |
| 36 | [Oak mounted fish display](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20mounted%20fish%20display) |
| 36 | [Teak wall decoration](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20wall%20decoration) |
| 38 | [Teak mounted head display](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20mounted%20head%20display) |
| 38 | [Small map](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20map) |
| 41 | [Rune display cases](https://oldschool.runescape.wiki/w/Special:Search?search=Rune%20display%20cases) |
| 42 | [Mounted sword](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20sword) |
| 44 | [Small landscape](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20landscape) |
| 47 | [Mounted Anti-Dragon Shield](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Anti-Dragon%20Shield) |
| 47 | [Mounted Amulet of Glory](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Amulet%20of%20Glory) |
| 47 | [Mounted Cape of Legends](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Cape%20of%20Legends) |
| 47 | [Mounted Mythical Cape](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Mythical%20Cape) |
| 48 | [Leagues accomplishment scroll](https://oldschool.runescape.wiki/w/Special:Search?search=Leagues%20accomplishment%20scroll) |
| 55 | [Large portrait](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20portrait) |
| 56 | [Gilded mahogany wall decoration](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mahogany%20wall%20decoration) |
| 56 | [Teak mounted fish display](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20mounted%20fish%20display) |
| 58 | [Medium map](https://oldschool.runescape.wiki/w/Special:Search?search=Medium%20map) |
| 58 | [Mahogany mounted head display](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20mounted%20head%20display) |
| 64 | [Ornate trophy pedestal](https://oldschool.runescape.wiki/w/Special:Search?search=Ornate%20trophy%20pedestal) |
| 64 | [Mounted giant blue krill](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20giant%20blue%20krill) |
| 65 | [Opulent rug (League hall)](https://oldschool.runescape.wiki/w/Special:Search?search=Opulent%20rug%20%28League%20hall%29) |
| 65 | [Large landscape](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20landscape) |
| 66 | [Mounted Harpoonfish](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Harpoonfish) |
| 66 | [Ornate banner stand](https://oldschool.runescape.wiki/w/Special:Search?search=Ornate%20banner%20stand) |
| 66 | [Round wall-mounted shield](https://oldschool.runescape.wiki/w/Special:Search?search=Round%20wall-mounted%20shield) |
| 68 | [Ornate league statue](https://oldschool.runescape.wiki/w/Special:Search?search=Ornate%20league%20statue) |
| 68 | [Mounted golden haddock](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20golden%20haddock) |
| 72 | [Mounted Xeric's Talisman](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Xeric%27s%20Talisman) |
| 74 | [Mahogany outfit stand](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20outfit%20stand) |
| 74 | [Mounted orangefin](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20orangefin) |
| 76 | [Mahogany mounted fish display](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20mounted%20fish%20display) |
| 76 | [Square wall-mounted shield](https://oldschool.runescape.wiki/w/Special:Search?search=Square%20wall-mounted%20shield) |
| 78 | [Mahogany trophy case](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20trophy%20case) |
| 78 | [Gilded mounted head display](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20mounted%20head%20display) |
| 78 | [Large map](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20map) |
| 78 | [Mounted huge halibut](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20huge%20halibut) |
| 80 | [Quest list](https://oldschool.runescape.wiki/w/Special:Search?search=Quest%20list) |
| 80 | [Mounted emblem](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20emblem) |
| 80 | [Mounted coins](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20coins) |
| 80 | [Cape hanger](https://oldschool.runescape.wiki/w/Special:Search?search=Cape%20hanger) |
| 82 | [Mounted Digsite Pendant](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Digsite%20Pendant) |
| 82 | [Mounted Vorkath head](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Vorkath%20head) |
| 82 | [Mounted Alchemical Hydra head](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20Alchemical%20Hydra%20head) |
| 82 | [Mounted purplefin](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20purplefin) |
| 83 | [Mahogany adventure log](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20adventure%20log) |
| 86 | [Wall-mounted kiteshield](https://oldschool.runescape.wiki/w/Special:Search?search=Wall-mounted%20kiteshield) |
| 86 | [Mounted swift marlin](https://oldschool.runescape.wiki/w/Special:Search?search=Mounted%20swift%20marlin) |
| 87 | [Boss lair display](https://oldschool.runescape.wiki/w/Special:Search?search=Boss%20lair%20display) |
| 88 | [Gilded adventure log](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20adventure%20log) |
| 93 | [Marble adventure log](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20adventure%20log) |

### Games (29)

Canonical reference: [Games room](https://oldschool.runescape.wiki/w/Games_room).

| Level | Skill-guide entry |
|---:|---|
| 30 | [Hoop-and-stick game](https://oldschool.runescape.wiki/w/Special:Search?search=Hoop-and-stick%20game) |
| 32 | [Boxing ring](https://oldschool.runescape.wiki/w/Special:Search?search=Boxing%20ring) |
| 34 | [Boxing glove rack](https://oldschool.runescape.wiki/w/Special:Search?search=Boxing%20glove%20rack) |
| 34 | [Oak prize chest](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20prize%20chest) |
| 37 | [Lesser magical balance](https://oldschool.runescape.wiki/w/Special:Search?search=Lesser%20magical%20balance) |
| 39 | [Jester game](https://oldschool.runescape.wiki/w/Special:Search?search=Jester%20game) |
| 39 | [Clay attack stone](https://oldschool.runescape.wiki/w/Special:Search?search=Clay%20attack%20stone) |
| 41 | [Fencing ring](https://oldschool.runescape.wiki/w/Special:Search?search=Fencing%20ring) |
| 44 | [Weapons rack](https://oldschool.runescape.wiki/w/Special:Search?search=Weapons%20rack) |
| 44 | [Teak prize chest](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20prize%20chest) |
| 48 | [Combat dummy](https://oldschool.runescape.wiki/w/Special:Search?search=Combat%20dummy) |
| 49 | [Treasure hunt fairy house](https://oldschool.runescape.wiki/w/Special:Search?search=Treasure%20hunt%20fairy%20house) |
| 51 | [Combat ring](https://oldschool.runescape.wiki/w/Special:Search?search=Combat%20ring) |
| 53 | [Combat dummy (Undead and Slayer)](https://oldschool.runescape.wiki/w/Special:Search?search=Combat%20dummy%20%28Undead%20and%20Slayer%29) |
| 54 | [Dartboard](https://oldschool.runescape.wiki/w/Special:Search?search=Dartboard) |
| 54 | [Extra weapons rack](https://oldschool.runescape.wiki/w/Special:Search?search=Extra%20weapons%20rack) |
| 54 | [Mahogany prize chest](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20prize%20chest) |
| 57 | [Medium balance](https://oldschool.runescape.wiki/w/Special:Search?search=Medium%20balance) |
| 58 | [Ornate combat dummy (Undead and Slayer)](https://oldschool.runescape.wiki/w/Special:Search?search=Ornate%20combat%20dummy%20%28Undead%20and%20Slayer%29) |
| 59 | [Limestone attack stone](https://oldschool.runescape.wiki/w/Special:Search?search=Limestone%20attack%20stone) |
| 59 | [Hangman game](https://oldschool.runescape.wiki/w/Special:Search?search=Hangman%20game) |
| 63 | [Simple pet arena](https://oldschool.runescape.wiki/w/Special:Search?search=Simple%20pet%20arena) |
| 71 | [Ranging pedestals](https://oldschool.runescape.wiki/w/Special:Search?search=Ranging%20pedestals) |
| 73 | [Advanced pet arena](https://oldschool.runescape.wiki/w/Special:Search?search=Advanced%20pet%20arena) |
| 77 | [Greater magical balance](https://oldschool.runescape.wiki/w/Special:Search?search=Greater%20magical%20balance) |
| 79 | [Marble attack stone](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20attack%20stone) |
| 81 | [Archery target](https://oldschool.runescape.wiki/w/Special:Search?search=Archery%20target) |
| 81 | [Balance beam](https://oldschool.runescape.wiki/w/Special:Search?search=Balance%20beam) |
| 83 | [Glorious pet arena](https://oldschool.runescape.wiki/w/Special:Search?search=Glorious%20pet%20arena) |

### Garden (64)

Canonical reference: [Garden](https://oldschool.runescape.wiki/w/Garden_(Construction)).

| Level | Skill-guide entry |
|---:|---|
| 1 | [Exit portal](https://oldschool.runescape.wiki/w/Special:Search?search=Exit%20portal) |
| 1 | [Low-level plant](https://oldschool.runescape.wiki/w/Special:Search?search=Low-level%20plant) |
| 5 | [Decorative rock](https://oldschool.runescape.wiki/w/Special:Search?search=Decorative%20rock) |
| 5 | [Tree](https://oldschool.runescape.wiki/w/Special:Search?search=Tree) |
| 6 | [Mid-level plants](https://oldschool.runescape.wiki/w/Special:Search?search=Mid-level%20plants) |
| 10 | [Pond](https://oldschool.runescape.wiki/w/Special:Search?search=Pond) |
| 10 | [Nice tree](https://oldschool.runescape.wiki/w/Special:Search?search=Nice%20tree) |
| 12 | [High-level plants](https://oldschool.runescape.wiki/w/Special:Search?search=High-level%20plants) |
| 15 | [Imp statue](https://oldschool.runescape.wiki/w/Special:Search?search=Imp%20statue) |
| 15 | [Oak tree](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20tree) |
| 30 | [Willow tree](https://oldschool.runescape.wiki/w/Special:Search?search=Willow%20tree) |
| 37 | [Grassland pet habitat](https://oldschool.runescape.wiki/w/Special:Search?search=Grassland%20pet%20habitat) |
| 40 | [Tip jar](https://oldschool.runescape.wiki/w/Special:Search?search=Tip%20jar) |
| 45 | [Maple tree](https://oldschool.runescape.wiki/w/Special:Search?search=Maple%20tree) |
| 47 | [Forest pet habitat](https://oldschool.runescape.wiki/w/Special:Search?search=Forest%20pet%20habitat) |
| 55 | [Boundary stones](https://oldschool.runescape.wiki/w/Special:Search?search=Boundary%20stones) |
| 56 | [Thorny hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Thorny%20hedge) |
| 57 | [Desert pet habitat](https://oldschool.runescape.wiki/w/Special:Search?search=Desert%20pet%20habitat) |
| 59 | [Wooden fence](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20fence) |
| 60 | [Nice hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Nice%20hedge) |
| 60 | [Yew tree](https://oldschool.runescape.wiki/w/Special:Search?search=Yew%20tree) |
| 63 | [Stone wall](https://oldschool.runescape.wiki/w/Special:Search?search=Stone%20wall) |
| 64 | [Small box hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20box%20hedge) |
| 65 | [Gazebo](https://oldschool.runescape.wiki/w/Special:Search?search=Gazebo) |
| 65 | [Restoration pool](https://oldschool.runescape.wiki/w/Special:Search?search=Restoration%20pool) |
| 65 | [Zen theme](https://oldschool.runescape.wiki/w/Special:Search?search=Zen%20theme) |
| 65 | [Topiary bush](https://oldschool.runescape.wiki/w/Special:Search?search=Topiary%20bush) |
| 66 | [Sunflower](https://oldschool.runescape.wiki/w/Special:Search?search=Sunflower) |
| 66 | [Rosemary](https://oldschool.runescape.wiki/w/Special:Search?search=Rosemary) |
| 66 | [Teak garden bench](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20garden%20bench) |
| 67 | [Iron railings](https://oldschool.runescape.wiki/w/Special:Search?search=Iron%20railings) |
| 67 | [Polar pet habitat](https://oldschool.runescape.wiki/w/Special:Search?search=Polar%20pet%20habitat) |
| 68 | [Topiary hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Topiary%20hedge) |
| 70 | [Dungeon entrance](https://oldschool.runescape.wiki/w/Special:Search?search=Dungeon%20entrance) |
| 70 | [Revitalisation pool](https://oldschool.runescape.wiki/w/Special:Search?search=Revitalisation%20pool) |
| 71 | [Marigolds](https://oldschool.runescape.wiki/w/Special:Search?search=Marigolds) |
| 71 | [Daffodils](https://oldschool.runescape.wiki/w/Special:Search?search=Daffodils) |
| 71 | [Picket fence](https://oldschool.runescape.wiki/w/Special:Search?search=Picket%20fence) |
| 71 | [Small fountain](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20fountain) |
| 72 | [Fancy hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Fancy%20hedge) |
| 75 | [Magic tree](https://oldschool.runescape.wiki/w/Special:Search?search=Magic%20tree) |
| 75 | [Otherworldly theme](https://oldschool.runescape.wiki/w/Special:Search?search=Otherworldly%20theme) |
| 75 | [Spirit tree](https://oldschool.runescape.wiki/w/Special:Search?search=Spirit%20tree) |
| 75 | [Large fountain](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20fountain) |
| 75 | [Garden fence](https://oldschool.runescape.wiki/w/Special:Search?search=Garden%20fence) |
| 75 | [Redwood fence](https://oldschool.runescape.wiki/w/Special:Search?search=Redwood%20fence) |
| 76 | [Tall fancy hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Tall%20fancy%20hedge) |
| 76 | [Roses](https://oldschool.runescape.wiki/w/Special:Search?search=Roses) |
| 76 | [Bluebells](https://oldschool.runescape.wiki/w/Special:Search?search=Bluebells) |
| 77 | [Volcanic pet habitat](https://oldschool.runescape.wiki/w/Special:Search?search=Volcanic%20pet%20habitat) |
| 77 | [Gnome bench](https://oldschool.runescape.wiki/w/Special:Search?search=Gnome%20bench) |
| 79 | [Marble wall](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20wall) |
| 80 | [Obelisk](https://oldschool.runescape.wiki/w/Special:Search?search=Obelisk) |
| 80 | [Rejuvenation pool](https://oldschool.runescape.wiki/w/Special:Search?search=Rejuvenation%20pool) |
| 80 | [Tall box hedge](https://oldschool.runescape.wiki/w/Special:Search?search=Tall%20box%20hedge) |
| 81 | [Posh fountain](https://oldschool.runescape.wiki/w/Special:Search?search=Posh%20fountain) |
| 83 | [Obsidian fence](https://oldschool.runescape.wiki/w/Special:Search?search=Obsidian%20fence) |
| 85 | [Fairy ring](https://oldschool.runescape.wiki/w/Special:Search?search=Fairy%20ring) |
| 85 | [Fancy rejuvenation pool](https://oldschool.runescape.wiki/w/Special:Search?search=Fancy%20rejuvenation%20pool) |
| 85 | [Volcanic theme](https://oldschool.runescape.wiki/w/Special:Search?search=Volcanic%20theme) |
| 88 | [Marble garden bench (decoration only)](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20garden%20bench%20%28decoration%20only%29) |
| 90 | [Ornate rejuvenation pool](https://oldschool.runescape.wiki/w/Special:Search?search=Ornate%20rejuvenation%20pool) |
| 95 | [Spirit tree & fairy ring](https://oldschool.runescape.wiki/w/Special:Search?search=Spirit%20tree%20%26%20fairy%20ring) |
| 98 | [Obsidian garden bench (decoration only)](https://oldschool.runescape.wiki/w/Special:Search?search=Obsidian%20garden%20bench%20%28decoration%20only%29) |

### Dungeon (40)

Canonical reference: [Dungeon](https://oldschool.runescape.wiki/w/Dungeon_(Construction)).

| Level | Skill-guide entry |
|---:|---|
| 61 | [Throne room floor decoration](https://oldschool.runescape.wiki/w/Special:Search?search=Throne%20room%20floor%20decoration) |
| 65 | [Oak cage](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20cage) |
| 65 | [Oubliette spikes](https://oldschool.runescape.wiki/w/Special:Search?search=Oubliette%20spikes) |
| 68 | [Steel cage](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20cage) |
| 70 | [Oak and steel cage](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20and%20steel%20cage) |
| 70 | [Skeleton guard](https://oldschool.runescape.wiki/w/Special:Search?search=Skeleton%20guard) |
| 71 | [Tentacle pool](https://oldschool.runescape.wiki/w/Special:Search?search=Tentacle%20pool) |
| 72 | [Spike trap](https://oldschool.runescape.wiki/w/Special:Search?search=Spike%20trap) |
| 74 | [Large trapdoor](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20trapdoor) |
| 74 | [Oak dungeon door](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20dungeon%20door) |
| 74 | [Guard dog](https://oldschool.runescape.wiki/w/Special:Search?search=Guard%20dog) |
| 75 | [Wooden dungeon treasure crate](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20dungeon%20treasure%20crate) |
| 75 | [Demon](https://oldschool.runescape.wiki/w/Special:Search?search=Demon) |
| 75 | [Steel cage](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20cage) |
| 76 | [Man trap](https://oldschool.runescape.wiki/w/Special:Search?search=Man%20trap) |
| 77 | [Oubliette flame pit](https://oldschool.runescape.wiki/w/Special:Search?search=Oubliette%20flame%20pit) |
| 78 | [Hobgoblin guard](https://oldschool.runescape.wiki/w/Special:Search?search=Hobgoblin%20guard) |
| 79 | [Oak dungeon treasure chest](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20dungeon%20treasure%20chest) |
| 80 | [Spiked cage](https://oldschool.runescape.wiki/w/Special:Search?search=Spiked%20cage) |
| 80 | [Tangle vine](https://oldschool.runescape.wiki/w/Special:Search?search=Tangle%20vine) |
| 80 | [Kalphite soldier](https://oldschool.runescape.wiki/w/Special:Search?search=Kalphite%20soldier) |
| 82 | [Lesser magic cage](https://oldschool.runescape.wiki/w/Special:Search?search=Lesser%20magic%20cage) |
| 82 | [Baby red dragon](https://oldschool.runescape.wiki/w/Special:Search?search=Baby%20red%20dragon) |
| 83 | [Teak dungeon treasure chest](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20dungeon%20treasure%20chest) |
| 83 | [Rocnar](https://oldschool.runescape.wiki/w/Special:Search?search=Rocnar) |
| 84 | [Steel-plated oak door](https://oldschool.runescape.wiki/w/Special:Search?search=Steel-plated%20oak%20door) |
| 84 | [Marble trap](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20trap) |
| 85 | [Tok-Xil](https://oldschool.runescape.wiki/w/Special:Search?search=Tok-Xil) |
| 85 | [Bone cage](https://oldschool.runescape.wiki/w/Special:Search?search=Bone%20cage) |
| 86 | [Huge spider](https://oldschool.runescape.wiki/w/Special:Search?search=Huge%20spider) |
| 87 | [Mahogany dungeon treasure chest](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20dungeon%20treasure%20chest) |
| 88 | [Teleport trap](https://oldschool.runescape.wiki/w/Special:Search?search=Teleport%20trap) |
| 89 | [Greater magic cage](https://oldschool.runescape.wiki/w/Special:Search?search=Greater%20magic%20cage) |
| 90 | [Troll guard](https://oldschool.runescape.wiki/w/Special:Search?search=Troll%20guard) |
| 90 | [Dagannoth](https://oldschool.runescape.wiki/w/Special:Search?search=Dagannoth) |
| 91 | [Magic dungeon treasure chest](https://oldschool.runescape.wiki/w/Special:Search?search=Magic%20dungeon%20treasure%20chest) |
| 94 | [Marble dungeon door](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20dungeon%20door) |
| 94 | [Hellhound](https://oldschool.runescape.wiki/w/Special:Search?search=Hellhound) |
| 95 | [Steel dragon](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20dragon) |
| 99 | [Rune dragon](https://oldschool.runescape.wiki/w/Special:Search?search=Rune%20dragon) |

### Chapel (31)

Canonical reference: [Chapel](https://oldschool.runescape.wiki/w/Chapel).

| Level | Skill-guide entry |
|---:|---|
| 45 | [Oak altar](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20altar) |
| 45 | [Steel torches (chapel)](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20torches%20%28chapel%29) |
| 45 | [Icon of Gnome Child](https://oldschool.runescape.wiki/w/Special:Search?search=Icon%20of%20Gnome%20Child) |
| 48 | [Symbol of Saradomin](https://oldschool.runescape.wiki/w/Special:Search?search=Symbol%20of%20Saradomin) |
| 48 | [Symbol of Guthix](https://oldschool.runescape.wiki/w/Special:Search?search=Symbol%20of%20Guthix) |
| 48 | [Symbol of Zamorak](https://oldschool.runescape.wiki/w/Special:Search?search=Symbol%20of%20Zamorak) |
| 49 | [Wooden torches (chapel)](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20torches%20%28chapel%29) |
| 49 | [Chapel windchimes](https://oldschool.runescape.wiki/w/Special:Search?search=Chapel%20windchimes) |
| 49 | [Small chapel statue](https://oldschool.runescape.wiki/w/Special:Search?search=Small%20chapel%20statue) |
| 49 | [Shuttered chapel window](https://oldschool.runescape.wiki/w/Special:Search?search=Shuttered%20chapel%20window) |
| 50 | [Teak altar](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20altar) |
| 53 | [Steel candlesticks](https://oldschool.runescape.wiki/w/Special:Search?search=Steel%20candlesticks) |
| 56 | [Cloth-covered teak altar](https://oldschool.runescape.wiki/w/Special:Search?search=Cloth-covered%20teak%20altar) |
| 57 | [Gold candlesticks](https://oldschool.runescape.wiki/w/Special:Search?search=Gold%20candlesticks) |
| 58 | [Chapel bells](https://oldschool.runescape.wiki/w/Special:Search?search=Chapel%20bells) |
| 59 | [Icon of Saradomin](https://oldschool.runescape.wiki/w/Special:Search?search=Icon%20of%20Saradomin) |
| 59 | [Icon of Guthix](https://oldschool.runescape.wiki/w/Special:Search?search=Icon%20of%20Guthix) |
| 59 | [Icon of Zamorak](https://oldschool.runescape.wiki/w/Special:Search?search=Icon%20of%20Zamorak) |
| 60 | [Cloth-covered mahogany altar](https://oldschool.runescape.wiki/w/Special:Search?search=Cloth-covered%20mahogany%20altar) |
| 61 | [Oak incense burners](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20incense%20burners) |
| 64 | [Limestone altar](https://oldschool.runescape.wiki/w/Special:Search?search=Limestone%20altar) |
| 65 | [Mahogany incense burners](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20incense%20burners) |
| 69 | [Medium chapel statue](https://oldschool.runescape.wiki/w/Special:Search?search=Medium%20chapel%20statue) |
| 69 | [Chapel organ](https://oldschool.runescape.wiki/w/Special:Search?search=Chapel%20organ) |
| 69 | [Decorative chapel window](https://oldschool.runescape.wiki/w/Special:Search?search=Decorative%20chapel%20window) |
| 69 | [Marble incense burner](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20incense%20burner) |
| 70 | [Marble altar](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20altar) |
| 71 | [Icon of Bob the Cat](https://oldschool.runescape.wiki/w/Special:Search?search=Icon%20of%20Bob%20the%20Cat) |
| 75 | [Gilded marble altar](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20marble%20altar) |
| 89 | [Large chapel statue](https://oldschool.runescape.wiki/w/Special:Search?search=Large%20chapel%20statue) |
| 89 | [Stained-glass chapel window](https://oldschool.runescape.wiki/w/Special:Search?search=Stained-glass%20chapel%20window) |

### Other (65)

Canonical reference: [Construction level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table).

| Level | Skill-guide entry |
|---:|---|
| 1 | [Mahogany Homes (beginner)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20Homes%20%28beginner%29) |
| 5 | [Cat blanket](https://oldschool.runescape.wiki/w/Special:Search?search=Cat%20blanket) |
| 10 | [Crab traps](https://oldschool.runescape.wiki/w/Special:Search?search=Crab%20traps) |
| 12 | [STASH units (beginner)](https://oldschool.runescape.wiki/w/Special:Search?search=STASH%20units%20%28beginner%29) |
| 17 | [Water pump](https://oldschool.runescape.wiki/w/Special:Search?search=Water%20pump) |
| 19 | [Cat basket](https://oldschool.runescape.wiki/w/Special:Search?search=Cat%20basket) |
| 19 | [Cooking pot](https://oldschool.runescape.wiki/w/Special:Search?search=Cooking%20pot) |
| 20 | [Mahogany Homes (novice)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20Homes%20%28novice%29) |
| 24 | [Spinning wheel](https://oldschool.runescape.wiki/w/Special:Search?search=Spinning%20wheel) |
| 24 | [Pottery wheel](https://oldschool.runescape.wiki/w/Special:Search?search=Pottery%20wheel) |
| 26 | [Rope bell pull](https://oldschool.runescape.wiki/w/Special:Search?search=Rope%20bell%20pull) |
| 27 | [Oak staircase](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20staircase) |
| 27 | [STASH units (easy)](https://oldschool.runescape.wiki/w/Special:Search?search=STASH%20units%20%28easy%29) |
| 30 | [Repair Piscarilius fishing cranes](https://oldschool.runescape.wiki/w/Special:Search?search=Repair%20Piscarilius%20fishing%20cranes) |
| 30 | [Chambers of Xeric - Small storage unit](https://oldschool.runescape.wiki/w/Special:Search?search=Chambers%20of%20Xeric%20-%20Small%20storage%20unit) |
| 31 | [Loom](https://oldschool.runescape.wiki/w/Special:Search?search=Loom) |
| 33 | [Cushioned cat basket](https://oldschool.runescape.wiki/w/Special:Search?search=Cushioned%20cat%20basket) |
| 37 | [Teak bell pull](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20bell%20pull) |
| 37 | [Oak pet feeder](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20pet%20feeder) |
| 37 | [Pottery oven](https://oldschool.runescape.wiki/w/Special:Search?search=Pottery%20oven) |
| 38 | [Pet list (for one-off pets)](https://oldschool.runescape.wiki/w/Special:Search?search=Pet%20list%20%28for%20one-off%20pets%29) |
| 39 | [Oak pet scratching post](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20pet%20scratching%20post) |
| 42 | [Crystal ball](https://oldschool.runescape.wiki/w/Special:Search?search=Crystal%20ball) |
| 42 | [STASH units (medium)](https://oldschool.runescape.wiki/w/Special:Search?search=STASH%20units%20%28medium%29) |
| 43 | [Anvil](https://oldschool.runescape.wiki/w/Special:Search?search=Anvil) |
| 48 | [Teak staircase](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20staircase) |
| 48 | [Teak pet feeder](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20pet%20feeder) |
| 49 | [Teak pet scratching post](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20pet%20scratching%20post) |
| 50 | [Teak portal frame](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20portal%20frame) |
| 50 | [Teleport focus](https://oldschool.runescape.wiki/w/Special:Search?search=Teleport%20focus) |
| 50 | [Rowboat](https://oldschool.runescape.wiki/w/Special:Search?search=Rowboat) |
| 50 | [Mahogany Homes (adept)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20Homes%20%28adept%29) |
| 54 | [Elemental sphere](https://oldschool.runescape.wiki/w/Special:Search?search=Elemental%20sphere) |
| 55 | [STASH units (hard)](https://oldschool.runescape.wiki/w/Special:Search?search=STASH%20units%20%28hard%29) |
| 56 | [Hallowed Sepulchre - Repair bridge](https://oldschool.runescape.wiki/w/Special:Search?search=Hallowed%20Sepulchre%20-%20Repair%20bridge) |
| 56 | [Furnace](https://oldschool.runescape.wiki/w/Special:Search?search=Furnace) |
| 59 | [](https://oldschool.runescape.wiki/w/Special:Search?search=) |
| 59 | [Mahogany pet feeder](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20pet%20feeder) |
| 59 | [Mahogany pet scratching post](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20pet%20scratching%20post) |
| 60 | [Gilded teak bell pull](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20teak%20bell%20pull) |
| 60 | [Chambers of Xeric - Medium storage unit](https://oldschool.runescape.wiki/w/Special:Search?search=Chambers%20of%20Xeric%20-%20Medium%20storage%20unit) |
| 65 | [Greater teleport focus](https://oldschool.runescape.wiki/w/Special:Search?search=Greater%20teleport%20focus) |
| 65 | [Mahogany portal frame](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20portal%20frame) |
| 66 | [Crystal of power](https://oldschool.runescape.wiki/w/Special:Search?search=Crystal%20of%20power) |
| 67 | [Limestone spiral staircase](https://oldschool.runescape.wiki/w/Special:Search?search=Limestone%20spiral%20staircase) |
| 68 | [Oak oubliette ladder](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20oubliette%20ladder) |
| 68 | [Oak lever](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20lever) |
| 70 | [Mahogany Homes (expert)](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20Homes%20%28expert%29) |
| 70 | [Bank chest](https://oldschool.runescape.wiki/w/Special:Search?search=Bank%20chest) |
| 72 | [Marble portal nexus](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20portal%20nexus) |
| 77 | [STASH units (elite)](https://oldschool.runescape.wiki/w/Special:Search?search=STASH%20units%20%28elite%29) |
| 78 | [Teak oubliette ladder](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20oubliette%20ladder) |
| 78 | [Teak lever](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20lever) |
| 80 | [Marble portal frame](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20portal%20frame) |
| 80 | [Scrying pool](https://oldschool.runescape.wiki/w/Special:Search?search=Scrying%20pool) |
| 82 | [Marble staircase](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20staircase) |
| 82 | [Gilded portal nexus](https://oldschool.runescape.wiki/w/Special:Search?search=Gilded%20portal%20nexus) |
| 88 | [Mahogany oubliette ladder](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20oubliette%20ladder) |
| 88 | [Mahogany lever](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20lever) |
| 88 | [STASH units (master)](https://oldschool.runescape.wiki/w/Special:Search?search=STASH%20units%20%28master%29) |
| 90 | [Chambers of Xeric - Large storage unit](https://oldschool.runescape.wiki/w/Special:Search?search=Chambers%20of%20Xeric%20-%20Large%20storage%20unit) |
| 92 | [Crystalline portal nexus](https://oldschool.runescape.wiki/w/Special:Search?search=Crystalline%20portal%20nexus) |
| 97 | [Marble spiral](https://oldschool.runescape.wiki/w/Special:Search?search=Marble%20spiral) |
| 99 | [Chambers of Xeric - Massive storage unit](https://oldschool.runescape.wiki/w/Special:Search?search=Chambers%20of%20Xeric%20-%20Massive%20storage%20unit) |
| 99 | [Skillcape — See any city's Estate Agent or Alwyn in Prifddinas](https://oldschool.runescape.wiki/w/Special:Search?search=Skillcape%20%E2%80%94%20See%20any%20city%27s%20Estate%20Agent%20or%20Alwyn%20in%20Prifddinas) |

### Servants (6)

Canonical reference: [Servants’ Guild](https://oldschool.runescape.wiki/w/Servants%27_Guild).

| Level | Skill-guide entry |
|---:|---|
| — | [Servants can be hired at the Ardougne Domestic Service Agency, north of the marketplace in East Ardougne.](https://oldschool.runescape.wiki/w/Special:Search?search=Servants%20can%20be%20hired%20at%20the%20Ardougne%20Domestic%20Service%20Agency%2C%20north%20of%20the%20marketplace%20in%20East%20Ardougne.) |
| 20 | [Rick](https://oldschool.runescape.wiki/w/Special:Search?search=Rick) |
| 25 | [Maid](https://oldschool.runescape.wiki/w/Special:Search?search=Maid) |
| 30 | [Cook](https://oldschool.runescape.wiki/w/Special:Search?search=Cook) |
| 40 | [Butler](https://oldschool.runescape.wiki/w/Special:Search?search=Butler) |
| 50 | [Demon Butler](https://oldschool.runescape.wiki/w/Special:Search?search=Demon%20Butler) |

### House Size (20)

Canonical reference: [Player-owned house — dimensions](https://oldschool.runescape.wiki/w/Player-owned_house#House_dimensions).

| Level | Skill-guide entry |
|---:|---|
| 1 | [Maximum rooms: 24](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2024) |
| 26 | [Maximum rooms: 25](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2025) |
| 32 | [Maximum rooms: 26](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2026) |
| 38 | [Maximum rooms: 27](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2027) |
| 44 | [Maximum rooms: 28](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2028) |
| 50 | [Maximum rooms: 29](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2029) |
| 56 | [Maximum rooms: 30](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2030) |
| 62 | [Maximum rooms: 31](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2031) |
| 68 | [Maximum rooms: 32](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2032) |
| 74 | [Maximum rooms: 33](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2033) |
| 80 | [Maximum rooms: 34](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2034) |
| 86 | [Maximum rooms: 35](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2035) |
| 92 | [Maximum rooms: 36](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2036) |
| 96 | [Maximum rooms: 37](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2037) |
| 99 | [Maximum rooms: 38](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20rooms%3A%2038) |
| 1 | [Maximum dimensions: 3x3](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20dimensions%3A%203x3) |
| 15 | [Maximum dimensions: 4x4](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20dimensions%3A%204x4) |
| 30 | [Maximum dimensions: 5x5](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20dimensions%3A%205x5) |
| 45 | [Maximum dimensions: 6x6](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20dimensions%3A%206x6) |
| 60 | [Maximum dimensions: 7x7](https://oldschool.runescape.wiki/w/Special:Search?search=Maximum%20dimensions%3A%207x7) |

### Boats (7)

Canonical reference: [Construction level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table).

| Level | Skill-guide entry |
|---:|---|
| 1 | [Wooden repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Wooden%20repair%20kit) |
| 19 | [Oak repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Oak%20repair%20kit) |
| 30 | [Teak repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Teak%20repair%20kit) |
| 47 | [Mahogany repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Mahogany%20repair%20kit) |
| 66 | [Camphor repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Camphor%20repair%20kit) |
| 80 | [Ironwood repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Ironwood%20repair%20kit) |
| 92 | [Rosewood repair kit](https://oldschool.runescape.wiki/w/Special:Search?search=Rosewood%20repair%20kit) |


## Appendix B — Wiki/current-state reconciliation

| System | Current Wiki checklist | Disposition |
|---|---|---|
| Starter/core | 1,000 coins, Rimmington, garden + parlour, build mode, hotspots, hammer/saw, materials, watering | Starter price/layout and first garden slice implemented; generic build loop remains. [Construction](https://oldschool.runescape.wiki/w/Construction) |
| Destinations | Nine current Wiki locations | Implemented with cache portals, durable relocation, current prices/levels, and outside-teleport destinations. [Locations](https://oldschool.runescape.wiki/w/Player-owned_house#House_locations) |
| Styles | Thirteen current styles/blueprints | Require cache assets and provenance. [Styles](https://oldschool.runescape.wiki/w/House_styles) |
| Access | Own/build/friend, privacy, doors, expel, leave, guest restrictions | Requires instance isolation. [Portal](https://oldschool.runescape.wiki/w/House_portal), [options](https://oldschool.runescape.wiki/w/Settings#House_options) |
| Room rules | Costs, levels, caps, grid, plane/support/door and uniqueness rules | Cache supplies assets; Wiki supplies validation behavior. [Rooms](https://oldschool.runescape.wiki/w/Player-owned_house#Rooms) |
| Servants | Rick, Maid, Cook, Butler, Demon Butler | All guide servants required. [Servants’ Guild](https://oldschool.runescape.wiki/w/Servants%27_Guild) |
| Cape | Estate-agent sale and perk/teleports | Level-99 sale and hood implemented; perk/teleports remain. [Construction cape](https://oldschool.runescape.wiki/w/Construction_cape) |
| Alternate training | Mahogany Homes, STASH, world repairs, flatpacks | Connect to owning modules. [Training](https://oldschool.runescape.wiki/w/Construction_training) |
| Current items | Level, XP, room, hotspot, flatpack, materials | Exact item-ID reconciliation resolves 462/525 cache rows; research the remaining 63 cache-only/cosmetic/upgrade rows and gate current-only rows. [Constructed items](https://oldschool.runescape.wiki/w/Constructed_items) |
| Sailing | Cache Boats rows and current cross-skill additions | Use versioned Sailing API; do not infer absent behavior. [Level-up table](https://oldschool.runescape.wiki/w/Construction/Level_up_table) |

## Appendix C — per-room Wiki index

- [Garden](https://oldschool.runescape.wiki/w/Garden_(Construction))
- [Parlour](https://oldschool.runescape.wiki/w/Parlour)
- [Kitchen](https://oldschool.runescape.wiki/w/Kitchen_(Construction))
- [Dining room](https://oldschool.runescape.wiki/w/Dining_room)
- [Workshop](https://oldschool.runescape.wiki/w/Workshop)
- [Bedroom](https://oldschool.runescape.wiki/w/Bedroom_(Construction))
- [Skill hall](https://oldschool.runescape.wiki/w/Skill_hall)
- [League hall](https://oldschool.runescape.wiki/w/League_hall)
- [Games room](https://oldschool.runescape.wiki/w/Games_room)
- [Combat room](https://oldschool.runescape.wiki/w/Combat_room)
- [Quest hall](https://oldschool.runescape.wiki/w/Quest_Hall)
- [Menagerie](https://oldschool.runescape.wiki/w/Menagerie)
- [Study](https://oldschool.runescape.wiki/w/Study)
- [Costume room](https://oldschool.runescape.wiki/w/Costume_room)
- [Chapel](https://oldschool.runescape.wiki/w/Chapel)
- [Portal chamber](https://oldschool.runescape.wiki/w/Portal_chamber)
- [Formal garden](https://oldschool.runescape.wiki/w/Formal_Garden)
- [Throne room](https://oldschool.runescape.wiki/w/Throne_Room)
- [Oubliette](https://oldschool.runescape.wiki/w/Oubliette)
- [Superior garden](https://oldschool.runescape.wiki/w/Superior_Garden)
- [Dungeon](https://oldschool.runescape.wiki/w/Dungeon_(Construction))
- [Portal nexus](https://oldschool.runescape.wiki/w/Portal_nexus)
- [Treasure room](https://oldschool.runescape.wiki/w/Treasure_Room)
- [Achievement Gallery](https://oldschool.runescape.wiki/w/Achievement_Gallery)

Inventory provenance: OSRS-Content/osrs239-content/configs/all.dbrow. Wiki links point to the live OSRS Wiki and therefore require the snapshot policy above.
