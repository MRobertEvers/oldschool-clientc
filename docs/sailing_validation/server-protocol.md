# Sailing server verification

The sailing gate runs the same server sections used by the full world selftest,
then adds production multiplayer packet replay and capacity checks. It uses the
revision239 cache and actual ocean at **3072,3160**. It never marks land as sea.

```sh
make -C src OPT=1 test-sailing-server
make -C src OPT=1 test-sailing-collision
make -C src OPT=1 test-mock239-playerinfo
make -C src OPT=1 test-entity-info-shrink
```

`test-sailing-server` builds and runs the shared `torirsserver` without
evidence output. It is the quick gate; the run below is the one that keeps a
per-check record.

To retain every runtime assertion:

```sh
TORIRSSERVER_SELFTEST_SAILING_ONLY=1 \
TORIRSSERVER_REV=osrs239 TORIRSSERVER_CACHE=cache.osrs239 \
TORIRSSERVER_SELFTEST_EVIDENCE=/tmp/sailing-server.tsv \
./src/build_opt/torirsserver --selftest
```

The sailing suite now **runs real content**: the stale-queue and lifecycle
sections call `[queue,...]` procs out of the compiled pack, so the server
refuses to start on a stale `script.dat` rather than testing content that does
not match the tree. Recompile the shared pack first, or point the run at an
isolated one with `TORIRSSERVER_SCRIPTS=<dir>`. Add
`PLATFORM_OBJ_BASE=build_<name>` to every `make` when building privately
alongside the shared build.

**Final run of this phase (2026-09-06 21:41)**, on the SHARED
`src/build_opt/torirsserver`
(`a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db`, the 2026-09-06 21:38 server; the same rows re-ran green on `214b43d7` and on the final `6a0e7f04`, both at 563/0) against
the shared 30,076-script pack
(`902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59`):
**561 checks, 0 failures** —
`docs/sailing_validation/server/sailing-selftest.{log,tsv}`. The same sections
inside the FULL suite reported one failure, `free evacuates both captain and
guest to walkable shore`; that was a fixture defect (the guest never stood on
the Pandemonium shore before boarding, so boarding correctly recorded the
Lumbridge home tile as its shore) and it is diagnosed and fixed in
`docs/sailing_validation/server/README.md`. With the fix the sailing gate is
563/0 and the full suite is 250,071/0, both on a private build; the shared
server must be rebuilt to carry it.

The previous run (2026-09-06 20:0x, `src/build_roles_opt/torirsserver`, isolated
pack `/tmp/sailing-roles-pack`, 30,076 scripts): **520 checks, 0 failures**,
`/tmp/sailing-roles-server.tsv`. The ten new rows are the sidepanel role
ladder (seven) and the sort queue's stale/current pair plus its fixture row
(three); a control build with both blocks compiled out ran 510/0, so the delta
is exactly those rows. The earlier 511-check figure was
`src/build_noether_opt/torirsserver` on pack
`21e85980698a56d26db6d2539263bd8be4c45182d979c3593b3396681ee06be5`.
This supersedes the 481-check run, whose single failure asserted production
logout behaviour while tearing the session down with the low-level
`WorldPlayerFree`; the fixture now uses `WorldRemovePlayer` and row
`production logout frees both session and owned vessel` passes.

| Requirement | Direct coverage |
|---|---|
| S0 independent collision windows | Two players at Lumbridge and Draynor, over129 tiles apart; both route using their own collision; five stationary ticks leave both actual scene-build counters unchanged. |
| S1 separate boat map | All17×17 surveyed ocean tiles permit hulls and block walkers. Shaped shore, sky, unknown maps, static/runtime locs, player/NPC occupancy and independent window changes are covered. |
| S1 complete native hull geometry | Config bounds and offsets remain authoritative even when the deck reservation dimensions differ. Full swept translation and swept turns catch obstacles between clear endpoints; collision parks the entire pose atomically. |
| S1 movement | Heading cap, shortest arcs through zero, quarter-tile quantization, speed caps, anchored rotation, and overlapping boats moving through one another. |
| S1 coordinate parity | Independent production client `Wev_ParentFromDeck` and `Wev_DeckFromParent` comparisons at all16 headings, plus inverse rounding bounds. |
| S1 capacity | Fifteen real hull/view/deck-window identities plus eight unrelated instances; a sixteenth hull is refused before allocation. Free/reuse preserves visibility and changes serial; teardown returns all resources. |
| S2 world entities | Actual spawn/rebuild/active-view packets, native config pivots, zone descriptors, smooth delta sums, per-observer snaps, despawn and same-view serial replacement. Revision230 refuses all vessel packets without changing tracking. |
| S2 player visibility | Production `SendPlayerInfo` packets are fed through the shipping revision239 client decoder. Shore, same-deck and local observers see the same authoritative deck position and two-tile RUN. Leaving view, changing plane, disembarking and re-entry are included. |
| S2 facilities visible to observers | A second hull's native mast reaches both a shore observer and a passenger on another hull. Later loc animation reaches both; quiet decks do not resend state; deck locs never land in the root world. |
| S2 NPC visibility | A real NPC walks on a moving deck; the actual NPC decoder produces its composed root position for shore and same-deck observers. Existing cases also cover root ocean NPC candidate discovery. |
| Stale queued callbacks | A logout/login roundtrip reuses one hull handle with a new serial. Every persisted `[queue,...]` callback from the retired identity is replayed into the replacement hull: salvage, trawling and cannon ticks cannot stop its work, reschedule themselves or award XP, and a stale facility arrival cannot open its customization interface. Each has a current-identity positive control, so none of the refusals is a callback that always no-ops. |
| Social permissions | Captain-only default navigation, explicit guest grants, invalid/unauthorized changes, all three native cargo privacy settings, revoke/leave/logout, and recycled player generation rejection. |
| Sidepanel player role | The per-tick sync's published value for the three roles aboard one hull, checked against the CACHE's own crew-NPC transform table rather than a restated constant: captain 10, navigator 6, aboard guest 3, not aboard 0, and the table read out of the boot cache resolves each of them to a real named npc while the previously published 2 is the hidden rung. |

## Coordinate contract

**This is final.** Two coordinate frames are in play and each answers exactly
one question.

- The **projected root** frame (`obs_level/obs_x/obs_z` — a deck occupant's
  tile composed through its hull's pose back into the ordinary world) answers
  *can these two players see each other, and where is an unseen player
  coarsely?* It decides `player_in_view`, and it is the coordinate written for
  a player who is **not** visible. On the pre-v5 stream it is also the pair of
  5-bit deltas written when a player enters an observer's low-resolution list,
  so that both sides of the pair are described in one frame.
- The **raw authoritative** frame (`level/x/z` — ordinary world coordinates
  ashore, private deck-staging coordinates in the map-instance pool while
  aboard) is what a **visible** player carries in the high-resolution stream.
  The client homes that player to the published view through the staging
  rectangle it already received in `WORLDENTITY_INFO`.

In `player_world_v5` (`src/torirsserver/torirs_server_encode.c`, the v5 branch
of `ToriRSServer_SendPlayerInfo`) this is one line:

```c
update->coord = update->visible ? player_coord_v5(subject->level, subject->x, subject->z)
    : player_coord_v5(subject->obs_level, subject->obs_x, subject->obs_z);
```

The rule holds for **every** observer — a shore observer receives a deck
passenger's staging coordinates just as a same-deck observer does, and the
observer's own aboard/ashore state changes nothing. Earlier revisions projected
the high-resolution coordinate too. That passes a naive assertion and is wrong
on screen: the client then draws the passenger at a root ocean tile that the
deck is merely passing over, so a visual peer test was needed to expose it.

Two consequences the suite pins down:

- A **standing** passenger's decoded position does not move when the hull does.
  Its raw staging tile did not change, and `WORLDENTITY_INFO` carries the deck
  and everything on it together. The decoder is checked directly: after two
  sailing ticks the client's `x`/`z` for that index are unchanged, its removal
  count is still 0 and its appearance count is still 1 — no synthetic walk,
  respawn or re-placement (`a standing passenger gets no fake PLAYER_INFO walk,
  respawn or placement as the hull moves`), while `obs_z` has changed, proving
  the hull really moved.
- A **running** passenger still moves the two deck tiles it actually ran, in the
  same tick that the hull carries it another tile. The two compose to three
  tiles of root travel while the high-resolution stream keeps a two-tile RUN
  (`root visibility moves three tiles while high-resolution staging retains a
  two-tile RUN`). Projecting here would either snap the actor or fabricate a
  three-tile run it never performed.

Because the deck is a private map instance, a deck occupant's feet are hundreds
of squares from the ocean in the zone map, so the zone-map candidate scan can
never offer one to a shore observer. Anyone carrying a non-zero projection
offset is appended to the candidate list explicitly; `player_in_view` still
decides, over the projected frame.

The four-section GPI reader follows the previous tick's high/low membership for
all four passes. A promotion applies any nested coarse-position update before
combining its fine coordinate; an explicit temporary traversal overrides
opcode 3's default snap. The implementation and its regression cases were
checked against [RSProt's revision239 reference client](https://github.com/blurite/rsprot/blob/master/protocol/osrs-239/osrs-239-desktop/src/test/kotlin/net/rsprot/protocol/game/outgoing/info/PlayerInfoClient.kt),
and every assertion above is made by decoding the production packet with the
shipping revision239 client decoder, not by reading server state back.

## Sidepanel player role (varbit 19233)

`sailing_sidepanel_player_role` is four bits on varp 5118, written once per tick
by `ToriRSServer_WorldRefreshObservation`. It has exactly four values the cache
gives a meaning to, and picking one the cache does not know is not a cosmetic
mistake: **it empties the deck.**

Every crew member drawn on a hull is a `multivarbit` NPC shell — npc 15255
`sailing_crew_generic_1_ship` and nine siblings, all switching on this varbit —
and both this client's resolver (`VarPManager_ResolveTransform`) and the
reference's (deob `class393`: `configs[value]` while `value < count - 1`, else
the last entry) index the transform table **by the value**. The shells state
`multinpc1/4/7 = *_ship_no_op` and `multinpc11 = *_ship_op` with every other
slot `-1`, so only 0, 3, 6 and 10 draw anybody.

| Value | Who | Cache evidence |
|---|---|---|
| 0 | not aboard | the sync's own `vessel == NULL` value; also what an outside observer needs, and the shells' rung 0 is the plain `*_ship_no_op` crew a passer-by sees. |
| 3 | aboard guest (passenger) | the only remaining rung with a real npc once 10, 6 and 0 are spoken for. |
| 6 | granted navigator | cs2 8732 `torirs_sailing_facility_row_state` branches `= 6` separately, and that branch is what disables the 8134/8135 rows for a navigator. |
| 10 | captain | cs2 8732 `= 10`, plus the captain-only affordances that test 10 alone: `torirs_sailing_edit_navigator_btn`, the Assign button in `torirs_sailing_facility_row_draw`, and content's `[if_button1,sailing_sidepanel:crew_assignation_clicklayer]`. |

The bit layout says the same thing: 3, 6 and 10 are `0b0011`, `0b0110`,
`0b1010` — one shared "aboard" bit plus one role bit.

**The defect this fixed:** a passenger was published **2**, which is that
aboard bit with no role bit and resolves to the shells' `-1` rung. A guest
aboard someone else's hull saw a deck with no crew on it at all. Nothing failed;
the crew simply were not drawn.

## Stale queued callbacks

A vessel handle is a pooled identity. Freeing a hull and reconstructing one at
login can hand the same handle back with a **new serial**, and every callback
the retired hull queued is still scheduled against that handle. Each recurring
sailing job — arrival, hooks, net/trawling, salvage, sort and cannon — therefore
captures the hull serial alongside its per-operation token and rechecks both
before acting. Projectile impacts stay bound to their target instead.

`src/torirsserver/test/sailing_stale_queues_selftest.u.h` is the fixture for
this, and it is **registered**, not orphaned:

- `torirs_server_world_selftest.c` includes it immediately **before**
  `test/sailing_lifecycle_selftest.u.h`, its only caller — the fixture needs the
  reconstructed hull that the lifecycle roundtrip has just produced.
- `sailing_lifecycle_selftest.u.h` calls
  `selftest_sailing_stale_queues(srv, player, boat, old_serial)` directly after
  the reconstructed-cargo assertion.
- The call has a fixture precondition. `distance` projects both ends into the
  ROOT frame, and on the restarted hull (heading 128) the saved deck tile is two
  deck tiles from hotspot 0 but rounds to three once projected, so the arrival
  control would refuse for a reason unrelated to the serial it exists to test.
  The caller therefore stands the player on the plank beside the hotspot —
  `MapInstanceBase + (3,4)`, where `~sailing_facility_op` walks in real play —
  and restores the saved deck tile afterwards.
- The fixture is non-destructive: it saves and restores instance registers 8..14
  and 110, the player's `queue`/`engine_queue` and the active player, and closes
  the modal its positive control opens.

Each stale case is paired with a current-identity control that **does** act, so
a passing refusal cannot be a callback that no-ops unconditionally. Twelve rows
result, `000448`..`000459` in `/tmp/sailing-noether-server.tsv`: the fixture
precondition, then for each of `[queue,sailing_salvage_tick]`,
`[queue,sailing_trawling_tick]` and `[queue,sailing_cannon_tick]` a signature
row, a stale-refusal row and a current-identity control row, and finally the
stale/current facility-arrival pair against interface 939.

The **sort** queue is now paired too. `[queue,sailing_sort_tick]` opens with the
same serial refusal, but unlike the three jobs above it records nothing on the
hull — its work is a sort of the OPERATOR's items — and the reconstructed skiff
carries no sorting station, so a stale and a current call would both return at
the next line for a reason that has nothing to do with the serial. The fixture
therefore gives the deck the native station this hull can actually carry:
hotspot 1's tenth option, `sailing_boat_facility_salvaging_station_2x5a`, whose
deck tile is the one the arrival fixture already stands the player on. With one
real `sailing_small_shipwreck_salvage` in the backpack the two calls are
distinguishable — the stale one leaves item, XP and queue untouched; the
current one consumes the salvage and advances Sailing. The fixture restores the
facility slot, the inventory and the XP.

One serial guard remains **outside this fixture**: `[queue,
sailing_extractor_shore_tick]`. It is not an omission of convenience — the job
refuses while `vessel_here = $boat`, i.e. it only runs with the captain
**ashore**, which is the opposite of this fixture's aboard hull. Its serial,
grace generation and captain-ownership rechecks are covered by
`tools/sailing_extractor_acceptance.py` instead (five acceptance checks through
real mouse activation and natural coast movement). The rows above therefore
cover five of the six serial-guarded jobs: arrival, salvage (the hook), 
trawling (the net), cannon and sort.

## Which player a `vessel_*` opcode answers for

Every sailing opcode in `src/torirsserver/torirs_server_scripts.c` used to read
`srv->active_player` — the player whose *tick* is running. The rest of that
dispatch reads a local `player`, which is `SSVM_Active(state, SSVM_ENT_PLAYER)`
and is what `p_finduid`, `p_findvisibleplayer` and `p_findmutualfriend` move.
The two agree only for a script that never switches player, and every piece of
social and crew content switches:

| Content | Switches to | Then reads |
|---|---|---|
| `boat_social.rs2` `~sailing_board_friend` | the captain (`p_findvisibleplayer`) | `vessel_here`, `vessel_slot`, `~sailing_owned_port`, `~sailing_materialise_owned` |
| `boat_social.rs2` occupancy loop, `~sailing_player_names_all` | each uid in turn | `vessel_here` |
| `boat_crew_actions.rs2` `~sailing_crew_worker` | the captain | `vessel_here = $handle` (the captain-aboard proof) |
| `boat_facility_utilities.rs2` `~sailing_utility_staffed` | the registered operator | `vessel_here = $boat` |

Read against `srv->active_player` those all answered for the caller: the
occupancy count was the caller counted eight times, the captain-aboard proof
was vacuous, and Board-friend read the guest's dock tile where it meant the
captain's — which is why it always ended in *"That captain has no boat at this
dock."* Each opcode is now bound to the script's active player. The per-opcode
decision, also recorded as a comment block above the vessel band in
`torirs_server_scripts.c`:

| Opcode | Binding | Why |
|---|---|---|
| `vessel_spawn` | active player owns the hull | `~sailing_materialise_owned` runs in the captain's context inside Board-friend |
| `vessel_here` | active player | definitionally "the hull under this player's feet" |
| `vessel_slot` | active player | an owner-gated read/write of that player's personal-boat slot |
| `vessel_owned` | active player | "which of *this* player's five slots" |
| `vessel_stat` key 11 | active player is the actor | `ToriRSServer_VesselSetNavigators` re-checks the actor is the hull's captain and is aboard, so a player switch cannot grant permissions on someone else's boat |
| `vessel_stat` key 9 | active player | "is *this* player the one navigating" |
| `vessel_control` | active player is the rider | `ToriRSServer_VesselPlayerControlAllowed` re-checks the permission |
| `vessel_cargo` | active player is the RIDER | `sailing_cargo()` then resolves the CAPTAIN's persistent inventory from `vessel->owner_uid`; "the captain of this boat" is expressed by `owner_uid`, never by the active player |
| `vessel_cargo_transfer` | active player | same rider, and the ironman check must be the rider's own varbit |
| `vessel_board`, `vessel_disembark`, `vessel_helm` | active player | the player being moved / taking the helm |

`vessel_settarget`, `setheading`, `setspeed`, `pos`, `free`, `sails`, `recover`,
`furnish`, `project`, `info`, `getfacility`, `facility`, `hp`, `damage`,
`nearest` and `vessel_stat` keys 0-8/10/12/13 address the HULL only and name no
player; they were not changed.

Board and disembark are the two that *move* somebody. Both go through
`ToriRSServer_WorldSetActive` + `WorldTeleport` and neither used to restore the
world's active binding, which was harmless only while they read
`srv->active_player`. That restore now lives **inside**
`ToriRSServer_VesselBoardPlayer` and `ToriRSServer_VesselDisembarkPlayer`,
not in the opcodes: the binding is scratch state those two functions borrow, so
every caller — the opcodes, `ToriRSServer_VesselLogin`'s reconstruction,
`vessel_evacuate_player`, the `::vesselboard` cheat, the harness `peer board`
verb — gets it back unchanged. The wrappers the opcodes carried are gone. A
caller that had already bound the player it is moving (login, evacuation, the
peer fixture) sees no difference; a caller that had not (a script under
`p_finduid`, `ToriRSServer_VesselFree`'s evacuation loop) no longer has its own
binding stolen.

## A boarder never lands on another rider's tile

`ToriRSServer_VesselBoardPlayer` searches walkable deck tiles outward from the
native pivot and takes the first one inside the hull extent. Players do not
block each other in this game, so the deck's collision map cannot report the
captain who boarded first: the search took the same tile for everybody and a
guest materialised standing inside the captain.

The search is now two passes over that *same* pivot-outward order. The first
pass additionally refuses any tile another live player occupies —
`vessel_deck_tile_taken` walks the player pool (`active`, not `player_count`,
which is only a high-water mark) at the deck plane; the boarder's own tile is
never "taken", so re-boarding the plank you already stand on keeps it. The
second pass drops that refusal, so a genuinely full deck is still boardable
rather than unreachable. Boarding still teleports to
`ToriRSServer_VesselDeckPlane(vessel)`, so login reconstruction, evacuation,
Board-friend and the peer fixture all still land on the deck plane.

The lifecycle selftest covers both end to end: it stands a named captain on a
skiff moored at the real Pandemonium gangplank, puts a named guest ashore at
3069,2987, runs the production `[proc,sailing_board_friend]`, answers the real
`p_namedialog` with the captain's name and requires the guest to end up on the
captain's hull — plus a control in which naming a passenger who owns no boat
boards nobody. On top of that it now requires that the guest's deck tile is
*not* the captain's (both on the deck plane, both aboard that hull), and, with
the world deliberately bound to the captain, that
`ToriRSServer_VesselDisembarkPlayer` and `ToriRSServer_VesselBoardPlayer` move
only the named guest and hand the captain's binding back. Building the same
sources with the two-pass refusal and the two restores disabled fails exactly
those three rows — the guest lands on the captain's tile (6404,67 in both
cases) and the binding comes back as the guest's pid.

## Native peer fixture

The persistent harness can create one additional real server player. It has no
second external transport; the ordinary multiplayer encoder, decoder and
renderer create its model on the primary native client.

```text
peer create HarnessMate
peer
peer board 1
peer place 1 6403 69
peer walk 6403 67 1
peer private 2
peer remove
```

`peer private` uses native chat privacy values0On/1Friends/2Off. The fixture also
supports `peer varbit NAME [VALUE]`, `peer cheat COMMAND`,
`peer proc NAME [up to four integer arguments]`, and
`peer button GROUP COMPONENT SLOT OP`; button input goes through the real
server interface handler. Readback includes the server position, role and
navigation state, plus the actual client's view, home view, model element,
view-local position and queued traversal.

Checkpoints cover the primary player. Remove the peer before saving or restoring
a movement checkpoint. The fixture refuses a partial multi-player rewind.

```sh
python3 tools/sailing_multiplayer_acceptance.py --start --headless
```

Its captured PNGs require visual inspection alongside the assertions. Loaded
binary and script hashes are recorded from the session metadata in the results.
