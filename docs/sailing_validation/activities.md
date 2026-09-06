# Installed facility actions

Facility operations resolve the installed native option and its actual deck
coordinate. `vessel_project` (server opcode 11113) projects that coordinate
through the vessel transform before querying ocean resources. The retained deck
has its own loc namespace and player collision; boat movement uses the separate
boat collision map. Item use on a deck loc sends coordinates from the picked
world view, then uses the ordinary server interaction and inventory checks.

## Interfaces, construction and access

The cache supplies Sailing Options **937**, Crew Management **938**, Boat
Customisation **939**, and cargo interfaces **943/944**. Facilities, Stats and
Crew tabs are functional. The helm and hull rows contain native informational
icons/text, with hidden click cells; their assignment buttons are interactive.
The sail row exposes the native Set/Un-set, speed and reverse controls.

The shipwright path opens physical shipyard hotspots and the native Build
catalogue. Server validation checks captain ownership, hull/hotspot fit,
Sailing and Construction requirements, extra skill/quest requirements,
schematic unlocks, tools, materials, and the native `build_max` across related
facility subtypes. Twelve normal schematic Read actions set permanent unlock
varbits and consume the schematic. Barrel stand, captured wind mote and Heart
of Ithell retain their native Inspect action. Upgrades and checkpoint restores
reapply the selected native parts, including the separate sail cloth. Successful
self-builds of core equipment and functional facilities award the source-backed
Construction XP for that exact boat part;
failed/repeated builds and paid shipwright construction award none.

Core operation checks use the installed mast/helm requirements. Taking the
helm, setting sails, raising speed and trimming require the current level;
releasing the helm, lowering/un-setting sails and stopping reverse remain
possible after a temporary level drop. Manual facility work also validates the
operator and reachable deck position. Crew work requires a live retained NPC,
an eligible assignment, native proficiency and the required current Sailing
level. Recruitment/roster slots unlock at base Sailing 40/55/70/85/95, subject
to each hull's `crew_capacity` and each recruit's unlock state.

## Implemented operations

Subtype numbers below are the cache's `sailing_boat_facility:facility_subtype`.
Gameplay sources live under
[`server/scripts/sailing/scripts`](../../OSRS-Content/osrs239-content/server/scripts/sailing/scripts).

| Subtype | Facility | Operation and state |
|---|---|---|
| 1 | Salvaging hook | Finds a real wreck within seven tiles of the projected hook, measured to the rotated wreck footprint. Player and crew attempts run every **five game ticks**. Removal, lost reach, inadequate level, full storage or depletion stops work. |
| 2 | Cargo hold | Native open/deposit-all/modify actions and quantity controls operate on the captain's real per-boat inventory **963–967**. The client receives that inventory in the other-inventory namespace (`base + 32768`). Transfers conserve items and respect installed capacity. |
| 3 | Range, loc **59682** | Physical Cook and item use open the existing cooking system, including quantities, burn chance, products and Cooking XP. A sailing approach handler requires a walkable adjacent deck tile; the land-style approach mask would point outside a narrow hull. |
| 4 | Cannon | Real magazines, compatible ammunition, target selection, line of sight, native attack rate, projectile travel and damage. Native panel orders support Follow captain, Hold fire, Fire at will, Clear target and Swap ammunition. |
| 5, 13 | Chum station/spreader | Manual or eligible crew work consumes cargo bait for a non-stacking extra-fish roll. Raw fish can be cut through physical item use; station rates are two/three/four fish per tick and 2 Cooking XP per fish. |
| 6 | Inoculation station | Native passive fetid-water resistance is published; its physical operation is Modify. No invented Inoculate menu option is added. |
| 7 | Salvaging station | Sorts actual salvage into this server's tiered material pool. It does not create salvage without a carried/cargo resource. |
| 8 | Wind/gale catcher | Activate/deactivate, store motes up to native capacity, and release one for 150 Sailing XP and a timed boost. Each boost adds **0.5 tiles/tick**, capped by hull speed cap; ordinary throttle stops at base speed. |
| 9 | Teleport focus | Installed normal/greater focus synchronizes the native owned-boat focus level **0/1/2**, used by boat-selection/teleport gates. Physical operation remains Modify. |
| 10 | Trawling net | Operate/stop, raise/lower and collect. A three-tick attempt queries a real shoal within four tiles of the projected net and requires matching depth, net capability and Fishing level. Catch is retained in inventory **968** (250 slots). |
| 11 | Fathom stone/pearl, locs **59767/59768** | Remote Quick-locate, remembered species selection and Locate-shoal. Searches actual ripple NPCs within 128 tiles; the pearl adds a real NPC hint marker and Deactivate clears it. No matching shoal produces a negative result. |
| 12 | Crystal extractor, locs **59702/59703** | Activate, charge for **100 ticks**, Harvest after a three-tick interaction, and Deactivate. Harvest awards **250 Sailing XP**, stores one mote if capacity permits, and rolls a quest-gated crystal shard. The charge is claimed before suspension to prevent duplicate harvests. |
| 14 | Anchor, locs **59687–59690** | Walk to the actual anchor tile, Drop/Raise and swap native appearances. Anchoring stops translation immediately while permitting collision-checked rotation. Raising resumes the selected sailing state; replacing an anchor releases it. |
| 15 | Keg, locs **59691–59698** | Check, Fill, Empty and use an empty beer glass. Fill consumes 25 pints of one ale, all noted or all unnoted. Empty does not refund them; glass use produces one drink. Duplicate keg bonuses do not stack. |
| 16 | Ballistic attractor | Publishes the native ammunition-save percentage, which the cannon consumption roll uses. Physical operation remains Modify. |
| 17 | Bosun's workbench, locs **29538–29544** | Manual/eligible crew work uses actual cargo repair kits on the native ten-tick interval. A kit is consumed only when its full repair amount fits the missing hull HP. |

Crew automation invokes the same hook, net and gun controllers with operator
UID zero, guarded by the actor's generation, assignment, proficiency and actual
work position. It also repairs with real kits and trims during natural gusts.
Crew gun/net work awards Sailing rather than the player's Ranged/Fishing XP.
Repair and retry scheduling uses a five-tick cycle. Work stops when a crew
assignment/actor becomes invalid; autonomous extractor state is separate from
crew work leases. All three current native boat rows use combined navigation, crew position 4
(Helm and sails). The Sloop actor spawn and route stay on walkable planking;
its native assignment, heading controls and automatic trim are verified in
[sloop-controls.json](sloop-controls.json).

Keg hooks currently apply Horizon's Lure (+2.5% Sailing XP), Trawler's Trust
(extra-fish roll), Perildance Bitter (+1 cannon maximum), and Whirlpool Surprise
(hidden Sailing contribution and minimum helm/deck proficiency). The Kraken
Ink Stout encounter multiplier helper is present; see world-content limits.

## Cannon IDs and publication

Native ammunition IDs **1–8** are bronze, iron, steel, mithril, granite,
adamant, rune and dragon cannonballs. IDs **9–15** are the seven metal
incendiary variants; **16–22** are the seven metal chainshot variants. There
are no granite special-ammunition variants. Compatibility uses the native
maximum tier, with special IDs mapped back to their metal tier.

The cannon state register is 0 stopped, 1 fire at will, 2 hold, or 3 follow
captain. Follow uses the captain's selected cannon target, falling back to
ordinary combat. Native row orders require the captain and an eligible assigned
crewmate; the captain/current operator can swap ammunition. The swap validates
compatibility and inventory room before exchanging whole ammunition stacks.
Loaded guns must be unloaded before replacement.

The panel publishes all 13 ammunition counts (`sailing_sidepanel_ammunition_count_N`;
varps **5129–5139**, **5474–5475**), native ammunition IDs, available-type flags,
`has_target`, and `following_target` (0 follow, 1 hold, 2 fire at will).
Varp **5043** names the actual combat-facility DB row; varbit **19103** is the
operated hotspot plus one. These values drive native enabled buttons and
counters; an isolated server button call is not sufficient UI verification.

Native special projectile/impact assets are used. The current server balance
for chainshot skips alternate movement turns over eight ticks; incendiary adds
three 1-HP burn pulses three ticks apart. Effects refresh rather than stack and
check both target UID generation and effect generation. These durations/damage
are explicit server balance, not a claim of retail special-ammunition parity.

## Runtime and checkpoint contract

A game tick is 600 ms. Script queues execute after their stored delay expires:
`queue*(..., 4)` is a five-tick interval; trawling's delay 2 gives three ticks.
The shared Sailing timer runs once per tick on the aboard captain. It services
crew and utilities before the sails-furled guard pauses wind/gust countdowns.

| Storage | Meaning |
|---|---|
| Instance 0/1/2 | Stored wind motes, catcher enabled, remaining boost ticks |
| Instance 5/6 | Remaining gust countdown / trim window (49 / 15 ticks in this server) |
| Instance 110 | Shipyard mode |
| Instance 112–116 | Retained crew NPC UID plus one |
| Instance 117–121 | Crew retry/repair countdowns |
| `8 + hotspot*7`, key 0 | Manual/crew work state, or cannon mode |
| Same block, keys 5/6 | Operator UID / monotonic work generation |
| Hook keys 2/3/4 | Wreck coordinate, loc type, remaining resource duration |
| Cannon keys 1/2/3/4 | Rounds, native ammunition ID, cooldown, target NPC UID plus one |
| Net key 2 | Raised/shallow/middle/deep setting |
| Keg key 1 | Selected ale (0 empty, 1–7 filled) |
| Extractor keys 2/4 | Remaining charge / autonomous enabled state |
| Vessel `anchored` | Translation lock; independent of chosen sails/heading |

Checkpoints copy scalar state, inventories, facility choices, relative timers
and deck animation state while preserving vessel/deck identity and protocol
history. They reject active VM/world scripts, combat and active resource work;
root-world NPCs and wrecks are not rolled back. Stop those activities before
saving/restoring. Script reload clears checkpoints because queued script IDs
may change. Restoring a boat does not resurrect a damaged shark or replenish a
wreck.

## Retail world-content limits

The hook success roll, trawling success roll, sorting material pool and special
ammunition effects are this server's balance model. Full retail rare-drop
pools, every ocean spawn/route, encounters, island discoveries and quest/recruit
progression are not reconstructed by these facility modules. Positive fixtures
place real targets in the actual ocean; they do not establish complete world
population or drop-table coverage.

Shoal producers supply NPC vars 0=species tier, 1=depth, 2=Fishing XP in tenths,
and 3=Sailing XP in tenths. The current net controller recognizes ripple and
wandering-fish shoals; Fathom searches ripple shoals. Higher-tier producers must
supply the species data rather than deriving fish from player level.

Passive resistance/focus stats and implemented perk consumers are wired, but
full sea-hazard/encounter systems remain outside this module. The extractor
currently stops charging immediately when the captain disembarks, rather than
after the retail shore grace period; crystal-flecked waters do not yet double
its shard reward. Kraken Ink Stout's 104% encounter helper needs an encounter
producer. The chum bait cost/20% roll is the current server implementation,
not a fully measured retail bait/depletion model.

## Evidence and reproducible fixtures

After setting suitable Sailing/Fishing/Ranged levels in an isolated session:

| Boat | Cannon | Hook/net | Wind | Cargo |
|---|---:|---:|---:|---:|
| Skiff | 1 | 4 | 0 | 6 |
| Sloop | 3 or 4 | 7 or 8 | 0 | 10 |

`::sailcannonfixture SLOT` installs a native bronze gun, supplies bronze balls
and spawns an attackable ocean shark. `::sailsalvagefixture SLOT` installs a hook
and nearby small wreck. `::sailnetfixture SLOT` plus `::sailshoal` provides a
native shallow giant-krill shoal. `::sailwindfixture SLOT` installs a catcher
without free motes. `::sailutilityfixture SLOT SUBTYPE` selects a real compatible
utility option. Normal shipyard requirements remain separate from these test
seeds.

- [Crew activity results](crew-actions.json) record real salvage, fish, cannon
  damage, repair and natural trimming, with six inspected screenshots. The hook
  duration was 55 after 50 ticks and 50 after another five ticks; two salvage
  items **32847** reached cargo **963**, with **60 tenths** Sailing XP for Jim's
  Deckhandiness-3 multiplier. Unassignment stopped work and further XP.
- [Bosun results](utility-bosun-results.json) retain a 10-HP oak kit when only
  5 HP are missing, then consume a basic 5-HP kit to heal exactly 75→80.
- [Extractor results](utility-extractor-results.json) show no early-click XP,
  100-tick charging, one stored mote and an exact 2500-tenths harvest.
- [Keg results](utility-keg-results.json) and [item-use trace](utility-keg-use-results.json)
  verify 25-pint filling, glass **1919** → drink **31823**, Empty without refund,
  native appearance rollback, and the 2500→2562-tenths Horizon bonus.
- [Native Build XP](native-build-xp-results.json) verifies the 151-XP bronze hook,
  no XP/material loss for a failed request, no repeat award, and a paid iron
  helm costing 1400 coins with zero Construction XP. The lookup uses native
  facility bottle IDs and explicit boat-class/core-tier totals.
- [Physical Cargo Open](physical-cargo-open.png) verifies world interaction,
  in addition to the native inventory widget tests.
- [Panel results](native-panel-results.json), [level gates](native-core-level-results.json),
  [release after a level drain](native-core-release-results.json),
  [shipyard Build/restore](shipyard-range-results.json), and
  [cargo results](cargo/results.json) exercise the actual native widgets and
  resource changes. The range and chum screenshots also show physical item use.

`python3 tools/sailing_facility_acceptance.py --start` exercises actual ocean
wrecks, shoals, wind and combat targets. `python3 tools/sailing_cargo_acceptance.py
--session /tmp/sailing-ui` exercises real cargo widgets in an existing isolated
session. Screenshot generation leaves visual review pending until inspected.

## Research and source records

The cache's `configs/all.dbrow`, `all.loc`, `all.seq`, `all.spotanim`, `all.varbit`
and `all.inv` supply native IDs, options, placements, proficiency, compatible
ammunition and display states. The five-tick salvaging change and 250-XP
extractor harvest follow the official
[December 3 Sailing XP update](https://secure.runescape.com/m=news/sailing-xp-review--further-fixes?oldschool=1).
Shipyard slots and boat customisation follow Jagex's
[launch preparation guide](https://secure.runescape.com/m=news/prepare-for-sailing---launching-november-19th?oldschool=1).
The Wiki's [Fathom pearl](https://oldschool.runescape.wiki/w/Fathom_pearl),
[anchor](https://oldschool.runescape.wiki/w/Anchor_%28facility%29), and
[schematic transcript](https://osrsindex.com/wiki/salvaging-station-schematic?site=osrs_wiki)
support remote location, anchored rotation and consuming a read schematic.

Native cannon panel controls retain their cache meanings: Follow Captain,
Hold/Fire at will, Clear target, and Swap ammo. All13 ammunition counts,
types, availability flags, target flags, and follow states are transmitted.
Swap exchanges real magazine/backpack stacks atomically; all22 native ammo
rows use their regular, chainshot, or incendiary projectile models. Actual
mouse verification is recorded in `cannon-panel-results.json`.

Special ammunition uses the existing NPC status primitives with explicit
server balance: chainshot skips alternate movement turns for8 game ticks
(4.8 seconds); incendiary applies three1HP burn pulses, three ticks apart.
A new hit refreshes its effect generation instead of stacking independent
loops. Both stop on target death or NPC-generation change. This implements
the qualitative effects described by Jagex while keeping the repo's revision239
content scope; it does not claim exact current retail damage parity. See
[Jagex, The Red Reef is Out Today!](https://secure.runescape.com/m=news/the-red-reef-is-out-today?oldschool=1).
