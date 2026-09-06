# Sailing validation

The acceptance tools run the actual revision-239 client, renderer, embedded
server, cache maps, and content scripts. The default fixture boards a wooden
skiff on natural ocean at **3072, 3160**. There is no stamped water arena.

## Fast visual loop

Build once, then keep the client loaded:

```sh
make -C src -j8 EMBED_SERVER=1 torirs
make -C src sscompile
src/build_opt/sscompile --src OSRS-Content/osrs239-content/server/scripts \
  --out OSRS-Content/osrs239-content/server/scripts/build \
  --content-root OSRS-Content/osrs239-content
python3 tools/sailing_harness.py --session /tmp/sailing start --headless
python3 tools/sailing_harness.py --session /tmp/sailing save ocean
python3 tools/sailing_harness.py --session /tmp/sailing cheat 'vesselsail 0 2'
python3 tools/sailing_harness.py --session /tmp/sailing step 30
python3 tools/sailing_harness.py --session /tmp/sailing capture /tmp/sailing.png
python3 tools/sailing_harness.py --session /tmp/sailing restore ocean
```

Warm measurements on the development machine (2026-09-06; 30 samples for
query/tick/capture and 20 for checkpoint operations):

| Operation | Median | p95 |
|---|---:|---:|
| State query | 2.83 ms | 2.91 ms |
| Pause | 2.94 ms | 5.41 ms |
| Save checkpoint | 2.90 ms | 5.23 ms |
| Restore checkpoint | 9.95 ms | 10.39 ms |
| Advance one server tick / 30 client cycles | 17.38 ms | 18.16 ms |
| Actual software-renderer PNG capture | 47.72 ms | 80.06 ms |

See [measurement JSON](harness-performance.json) and the
[harness design and checkpoint scope](../sailing_harness.md). Cold loading is
measured separately; observed native starts were approximately 2.4–4.1 seconds.
Commands neither rebuild nor restart the warm client. Session metadata records
the binary and loaded script pack hashes, including whether a stale-pack
override was used during concurrent development.
The [final core run](final-core-results.json) used the current compiled pack
without an override: collision acceptance took 1.03 seconds and the deck
animation/restore acceptance took 0.57 seconds.

## Reproducible checks

```sh
python3 tools/sailing_collision_acceptance.py --session /tmp/sailing
python3 tools/sailing_cargo_acceptance.py --session /tmp/sailing
python3 tools/sailing_deck_acceptance.py --session /tmp/sailing
python3 tools/sailing_facility_acceptance.py --start
python3 tools/sailing_crew_acceptance.py --start
```

The collision check surveys separate boat/player maps, sails toward the real
shore, checks the complete hull stops, rejects a land spawn, and restores a
checkpoint partway through client interpolation. The focused C test also
checks rotation sweeps, thin obstacles, native hull offsets, and scene rebuilds.

The cargo check clicks the native interface: dismiss its warning, withdraw all
13 coins, deposit exactly 10, and verify the backpack and captain's inventory
conserve the total while server time stays paused.

The deck check verifies that a real sailcloth model exists, native sail
sequences alter its rendered vertices, animation advances exactly once per
client cycle, and restoration recovers the saved frame and mesh hash. It also
installs a net and verifies restoration removes its actual scene model.
The same deck regression passes with `--renderer gl3-zbuffer`; the GPU renders
the composed boat/deck geometry and captures the presented framebuffer.

The activity check uses genuine wreck/shoal/NPC entities and real gust timing.
Fixtures supply inputs; the production activity handlers must earn the wind,
salvage, fish, experience and cannon damage. Assertions compare inventory
deltas, stop/cancel behaviour, and visible client NPCs. Captures require human
visual inspection in addition to the executable assertions.

The crew check uses the real recruitment interface and assignment controls,
then verifies autonomous salvage, trawling, repairs, cannon fire and natural
sail trimming, including resource/experience deltas and cancellation. The
captain can walk on deck and issue bearings while qualified crew navigate.

Focused native tests pass for boat collision, compass projection, GPU deck
traversal, NPC movement, packet encoding, cargo inventory isolation, native
array operations, typed interface triggers and the CS2 settlement contract.
The host request tests cover all 653 declared request kinds.

## Visual evidence

![Ready ocean fixture and native Sailing Options](ocean-ready.png)

![Actual GPU-rendered boat and sailcloth](gpu/deck-sailing.png)

![Real shipyard build replacing an empty hotspot](shipyard-range-built.png)

![Native cargo widget conserving the item total](cargo/native-cargo-roundtrip.png)

![Native customisation models and requirements](sloop-customisation.png)

The deck, cargo, raft/sloop model previews and shipyard build/restore captures
above were opened and visually inspected. Individual JSON reports retain the
commands and measured state used to produce the captures.
Additional inspected evidence includes [crew work](crew-actions.json),
[cannon panel mouse controls](cannon-panel-results.json),
[special ammunition effects](ammunition-effects-results.json), and
[safe script reload](reload-results.json).
Native Build also [awards Construction XP](native-build-xp-results.json) only
for successful self-builds; failed, repeated and paid builds award none.
The [Captain's Log hint view](captains-log-hint.png) and
[native spyglass](spyglass.png) were also opened and visually inspected.

## Research and implementation boundaries

The repository's [Sailing research](../SAILING.md),
[collision design](../sailing_collision.md), and the revision-239 cache provide
the native interfaces, model/animation IDs, hull dimensions, facilities,
requirements, capacities and crew stats. Research also used Jagex's
[launch preparation article](https://secure.runescape.com/m=news/prepare-for-sailing---launching-november-19th?oldschool=1),
[launch article](https://secure.runescape.com/m=news/sailing-is-out-today?oldschool=1),
[preparation video](https://www.youtube.com/watch?v=17G2iSghB4I), and
[launch video](https://www.youtube.com/watch?v=CFtwftwNWRI).
Video storyboards were inspected for the boat facilities, interfaces and
shipyard presentation.

Checkpoint restoration covers the owned sailing fixture, inventories, stats,
facilities, timers and visual phases. It rejects active encounters and does
not rewind depleted wrecks or damaged root-world NPCs. Activity fixtures tag
their own temporary NPCs so repeat tests can clean up those entities safely.
Retail rare-drop tables and every sea encounter are separate world content;
the facility reward choices and supported producers are documented in
[activities.md](activities.md).
