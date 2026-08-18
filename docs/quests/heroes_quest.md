# Heroes' Quest modernization audit

Status: `audit-pending` — the three collection arms and both gang routes have
substantial LostCity-era coverage, and the configured XP rewards match the
current OSRS Wiki. The implementation is not a valid modern Heroes' Quest,
however. It intentionally permits the co-operative mansion sequence to be
soloed, does not bind Grip, his keyring, or either candlestick to the two
participants, accepts candlesticks without canonical provenance, and has no
attack-style gate for Grip. Completion publishes state 15 before settling
twelve XP grants, one quest point, item consumption, and unlock presentation.
Several current dialogue, recovery, route, and downstream contracts are also
absent.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to acceptance, the three independently
ordered collection arms, both Shield of Arrav gang routes, co-operative player
identity, item provenance, combat and drop ownership, recovery, completion,
journal/admin adapters, and every direct consumer found. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable requirements, dialogue, route, recovery, reward, and integration
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Heroes' Quest](https://oldschool.runescape.wiki/w/Heroes%27_Quest?oldid=15285461) | 15285461, 2026-08-01 | Identity, requirements, all routes, multiplayer rules, rewards, and downstream requirements |
| [Heroes' Quest/Quick guide](https://oldschool.runescape.wiki/w/Heroes%27_Quest/Quick_guide?oldid=15147261) | 15147261, 2026-03-11 | Ordered actions, partner hand-offs, equipment, and traversal |
| [Transcript:Heroes' Quest](https://oldschool.runescape.wiki/w/Transcript%3AHeroes%27_Quest?oldid=15275392) | 15275392, 2026-07-25 | Quest offer, dialogue topology, provenance failures, full-inventory behavior, recovery, and completion boundary |
| [Heroes' Guild](https://oldschool.runescape.wiki/w/Heroes%27_Guild?oldid=15216794) | 15216794, 2026-05-25 | Entrance, shop, fountain, guild scenery, and postquest access |
| [Ice gloves](https://oldschool.runescape.wiki/w/Ice_gloves?oldid=15214105) | 15214105, 2026-05-20 | Ice Queen source, repeatability, fire-feather handling, and downstream uses |
| [Smiths' gloves (i)](https://oldschool.runescape.wiki/w/Smiths%27_gloves_%28i%29?oldid=15190929) | 15190929, 2026-04-22 | Modern alternative to ice gloves for the fire feather |
| [Ice Queen](https://oldschool.runescape.wiki/w/Ice_Queen?oldid=15254243) | 15254243, 2026-07-05 | Level-111 encounter and guaranteed ice-gloves drop |
| [Entrana firebird](https://oldschool.runescape.wiki/w/Entrana_firebird?oldid=15199440) | 15199440, 2026-04-28 | Level-2 encounter, feather drop, and postquest behavior |
| [Fire feather](https://oldschool.runescape.wiki/w/Fire_feather?oldid=15183799) | 15183799, 2026-04-22 | Pickup requirements and temporary-item lifecycle |
| [Lava eel](https://oldschool.runescape.wiki/w/Lava_eel?oldid=15183895) | 15183895, 2026-04-22 | Fishing/cooking levels, no-burn contract, and fishing locations |
| [Oily fishing rod](https://oldschool.runescape.wiki/w/Oily_fishing_rod?oldid=15248137) | 15248137, 2026-07-02 | Rod preparation and replacement route |
| [Blamish oil](https://oldschool.runescape.wiki/w/Blamish_oil?oldid=15184075) | 15184075, 2026-04-22 | Herblore recipe and item behavior |
| [Pete's candlestick](https://oldschool.runescape.wiki/w/Pete%27s_candlestick?oldid=15185297) | 15185297, 2026-04-22 | Two-item chest source, tradeability, and route provenance |
| [Grip's keyring](https://oldschool.runescape.wiki/w/Grip%27s_keyring?oldid=15187117) | 15187117, 2026-04-22 | Partner-specific key ownership and treasure-door use |
| [Miscellaneous key](https://oldschool.runescape.wiki/w/Miscellaneous_key?oldid=15185857) | 15185857, 2026-04-22 | Black Arm source, Phoenix hand-off, and replacement |
| [Thieves' armband](https://oldschool.runescape.wiki/w/Thieves_armband?oldid=14565945) | 14565945, 2024-03-18 | Leader reward and postquest replacement |
| [Grip](https://oldschool.runescape.wiki/w/Grip?oldid=15199713) | 15199713, 2026-04-28 | Level-22 combat, luring, attack restrictions, and helper behavior |
| [Fountain of Heroes](https://oldschool.runescape.wiki/w/Fountain_of_Heroes?oldid=15285767) | 15285767, 2026-08-02 | Four-charge glory recharge unlock |
| [Charge dragonstone jewellery scroll](https://oldschool.runescape.wiki/w/Charge_dragonstone_jewellery_scroll?oldid=15187672) | 15187672, 2026-04-22 | Completion-gated scroll unlock |

These sources define a members, experienced, medium quest released 27 February
2002. Starting requires 55 quest points and completion of Shield of Arrav,
Lost City, Merlin's Crystal, and Dragon Slayer I. Cooking 53, Fishing 53,
Mining 50, and Herblore 25 are all boostable and are deliberately not start
requirements. Combat level 50 is recommended. Another player from the
opposite Shield of Arrav gang is still required for the candlestick sequence;
completed players can help, and Iron accounts can use the required partner
items on one another.

The three final items may be obtained in any order and must be handed to
Achietties together. Current alternatives include ice gloves or Smiths'
gloves (i), and four lava-eel locations: Taverley Dungeon, Lava Maze, Charred
Dungeon, and Wyrmscraig Cavern. Completion awards one quest point and the
twelve XP grants recorded in section 2. It unlocks the Heroes' Guild, dragon
mace/battleaxe purchase and wielding, Fountain of Heroes, charge-dragonstone
jewellery scrolls, and six-charge glory recharging at the Fountain of Rune.

Transition aid only: Quest Helper's
[`HeroesQuest.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/heroesquest/HeroesQuest.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary states
0-13, both gang routes, participant hand-offs, areas, item identities, and the
Taverley routes. The file last changed in
`fa2a36898cd075953f2731747b4dfc197cfa5732` on 2026-02-23. Running
`python3 tools/questhelper_extract.py heroesquest --check` resolves every
referenced item, object, and varp except a position-specific lava-fishing NPC;
its expected dbrow name `quest_heroesquest` is also a helper/tool naming
mismatch because the cache row is `quest_heroes`. Quest Helper lists 2,275
Mining XP, duplicating Smithing, while the current Wiki and native dbrow both
specify 2,575. The latter two are authoritative. Quest Helper also does not
yet describe the newer Charred Dungeon and Wyrmscraig routes. It cannot prove
server writes, player pairing, provenance, drop visibility, transaction
atomicity, or migration.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_heroes`; quest metadata ID 22 |
| Type / difficulty / length | Members quest / experienced / medium |
| Release / series | 27 February 2002 / Heroes' Guild |
| Start | `achietties` outside the Heroes' Guild |
| Primary state | `%heroquest`, native permanent/transmitted varp 188 |
| Proven values | 0 not started; 1 started; Phoenix 2 Straven, 3 Alfonse, 4 Charlie, 5 killed Grip, 6 obtained armband; Black Arm 7 Katrine, 8 HQ door, 9 ID papers, 10 mansion, 11 papers handed to Grip, 12 chest looted, 13 obtained armband; 15 complete |
| Unresolved value | State 14 exists between authored route endpoints and native end state but has no writer in the repository; exact current-client meaning requires capture |
| End / quest points | State 15 / 1 QP |
| Skill policy | Cooking 53, Fishing 53, Mining 50, Herblore 25; all boostable and checked at use, not acceptance |
| Other requirements | 55 QP; Shield of Arrav, Lost City, Merlin's Crystal, Dragon Slayer I; opposite-gang partner |
| Combat | Ice Queen 111 if gloves needed; Entrana firebird 2; optional jailer 47; Grip 22 for the Phoenix participant |
| Direct unlocks | Guild, Happy Heroes' H'emporium, dragon mace/battleaxe wielding, both fountains' benefits, charge-dragonstone scrolls |

The native dbrow correctly records the start NPC, state 15, one quest point,
requirements, boostability, deferred skill checks, and recommended combat 50.
Its XP values are in tenths and match the current Wiki exactly:

| Skill | XP |
| --- | ---: |
| Attack | 3,075 |
| Defence | 3,075 |
| Strength | 3,075 |
| Hitpoints | 3,075 |
| Ranged | 2,075 |
| Fishing | 2,725 |
| Cooking | 2,825 |
| Woodcutting | 1,575 |
| Firemaking | 1,575 |
| Smithing | 2,275 |
| Mining | 2,575 |
| Herblore | 1,325 |

No native Heroes' Quest support varbits were found beyond varp 188. That does
not make one overloaded scalar sufficient for the current multiplayer
contract. A modern owner needs durable secondary data for at least:

| Support responsibility | Why primary state is insufficient |
| --- | --- |
| Route identity | Numeric ordering interleaves two mutually exclusive paths and can contradict the Shield of Arrav gang varps |
| Miscellaneous-key issued | Treasure-door validity depends on the Black Arm participant having obtained it at least once, even if the tradeable key has moved |
| Partner identity / encounter generation | Keyring validity is specific to the pair and to the current Grip attempt |
| Grip lured | Phoenix may attack only after the paired Black Arm player has moved Grip into position |
| Phoenix kill provenance | Straven accepts a candlestick only when this player killed the paired Grip |
| Black Arm chest provenance | Katrine accepts a candlestick only when this player personally retrieved it from the paired chest |
| Chest settlement | Exactly two candlesticks must be delivered or safely dropped, once per valid attempt |
| Armband issued / replacement | Item possession alone cannot distinguish pre-completion hand-in from canonical postquest replacement |
| Three final-item consumptions | Completion must resume without duplicating or losing any item |
| Twelve XP grants, QP, completion, unlock publication | State 15 cannot safely prove that every settlement component occurred |

These fields should use native cache carriers if current-server capture finds
them; otherwise use versioned server-side quest records rather than inventing
another client-visible primary state. Pair/encounter records must expire or
reconcile deterministically without invalidating legitimate completed-player
help.

### Required state capture and migration

Capture varp 188, both Shield of Arrav gang varps, all relevant inventory,
bank, worn, keyring, ground-item, NPC, and location state after every canonical
action. Include fresh players, both gang routes, completed helpers, Iron item-
on-player exchanges, full inventories, death/logout/region leave, multiple
simultaneous pairs, and every state 0-15.

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 1 with one completed gang | Normal route selection | Derive route from a valid completed Shield of Arrav gang, then create no partner/provenance milestones until actions prove them |
| Phoenix state 2-6 with Black Arm gang, or Black Arm state 7-13 with Phoenix gang | Old scripts or admin writes produced contradictory identity | Quarantine for deterministic repair; never silently switch the player's gang or grant provenance |
| Either Shield gang appears joined/complete | Current scripts use inconsistent `joined` versus `complete` comparisons | Normalize against captured native gang semantics before route dispatch |
| State 5/12 with public key/candlestick remnants | Local solo/public-drop path may have been used | Preserve earned primary progress for compatibility, but do not fabricate a partner identity; tag as legacy provenance and prevent new duplication |
| State 6/13 without armband | Lost item, failed add, or interrupted route reward | Reconcile item domains and route issue history; provide canonical route-leader replacement without replaying the candlestick hand-in |
| State 14 | Journal says complete while guild door and completion helper do not | Capture current meaning; until then, treat as an explicit repair case, never as state 15 |
| State 15 without one or more XP/QP components | Current state-first queue may have partially settled | Repair only missing components from durable history or audited save evidence; never replay all rewards from state alone |
| State 15 with armband absent | Canonical because Achietties consumes it | Do not restore automatically; only the explicit postquest leader dialogue gives a replacement |
| Completed helper re-entering mansion | Canonically may help another player | Create a new bounded assistance attempt, not a new personal quest route or reward claim |

Do not infer that a held or banked tradeable candlestick proves either leader's
provenance. Do not infer pairing from proximity alone. Legacy saves that used
the authored solo bypass need an explicit compatibility policy and telemetry,
not fabricated evidence that another player participated.

## 3. Implementation surface

The direct quest root contains 808 lines across eleven scripts and four config
files, but most critical owners live elsewhere. This is a distributed quest,
not a self-contained `quest_hero` port.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_hero.constant` / `.varp` | State constants and native varp 188 | Named states 0-13 and 15 are present; 14 and all secondary ownership/settlement state are unresolved |
| `quest_hero.rs2` | Requirements, route helpers, completion XP | Requirement metadata and XP are correct; completion is state-first and non-idempotent |
| `achietties.rs2` | Start, help, final hand-in | Route-shaped but bypasses modern quest offer/warning and has unsafe completion ordering |
| `hero_journal.rs2` | Journal rendering | Registered and detailed; substitutes item possession for durable milestones and treats state 14 as complete |
| `straven.rs2` / `katrine.rs2` | Route start and armband hand-in | Recently added bridge makes routes reachable; provenance, transactional output, exact dialogue, and replacement are absent |
| `brimhaven_thin.rs2` / `charlie_the_cook.rs2` | Phoenix password and kitchen | Broad dialogue route exists; state/gang comparisons and current re-talk behavior need capture |
| `brimhaven_restaurant.rs2` | Restaurant door and secret panel | Traversal exists; uses teleport-like door helper and no assistance-attempt ownership |
| `grubor.rs2`, `trobert.rs2`, `garv.rs2`, `grip.rs2` | Black Arm infiltration and key | Broad route exists; several adds are unchecked and completed-helper semantics are incomplete |
| `brimhaven_scarface_mansion.rs2` | Lure, side door, treasure door/chest | Contains an explicit noncanonical solo bypass; shared actor/loc and item provenance are unsafe |
| `drop_tables/scripts/grip.rs2` | Grip kill credit and keyring | Public shared drop with only killer-state update; no pair, lure, style, or owner binding |
| `drop_tables/scripts/ice_queen.rs2` | Guaranteed ice gloves | Correct quest-independent guaranteed drop shape; private/public loot semantics remain unproven |
| `drop_tables/scripts/entrana_firebird.rs2` / `fire_feather.rs2` | Feather drop and pickup | Main path exists; Smiths' gloves (i) and owner-safe drop behavior are missing |
| White Wolf Mountain / generic ladders | Rockslide and Ice Queen traversal | Mining gate and route exist; item-on-rockslide whitelist is obsolete |
| Gerrant / Herblore / oily rod | Slime, level-25 oil, rod preparation | Generally sound and recoverable; current dialogue and all ownership domains still need runtime proof |
| lava-fishing category / cooking row | Level-53 catch and no-burn level-53 cook | Taverley and Lava Maze placements found; newer Charred/Wyrmscraig access is absent |
| Entrana monk / high priest | Canonical feather hints | Both files explicitly defer Heroes' Quest dialogue, including the 2025 Holy Grail interaction fix |
| Heroes' Guild / Helemos shop | Entrance, shop, Fountain of Heroes | Entrance and fountain work behind state-15 door; dialogue trade text is stale while op3 shop works |
| combat equipment gate | Dragon mace/battleaxe wielding | Explicit state-15 gate exists |
| Fountain of Rune | Six-charge jewellery and eternal-glory roll | Recharges without checking Heroes' Quest; unlock is globally available |
| charge-dragonstone scroll item | Completion-gated scroll use | Item exists in loot data, but no use handler or Heroes' Quest gate was found |
| later quests / diaries | Canonical downstream prerequisites | Throne, Legends, and RFD gates exist; Fremennik Exiles start and Falador Hard task enforcement are absent/incomplete |
| quest cheat | Admin completion | Writes state 15 only, without items, rewards, pairing, support state, or settlement history |
| automated tests | State, two-player route, transactions, rewards | No Heroes' Quest-specific tests found |

The cache contains all principal legacy items, NPCs, locations, state varp,
combat stats, and quest metadata. Missing modern content is mainly lifecycle
and ownership machinery plus new-world route integration, not a reason to keep
the solo fallback.

## 4. Route reachability

### Acceptance and shared collection model

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Offer | Skill-warning box if below any later skill; explicit Start Heroes' Quest? yes/no | Direct two-choice dialogue; no generic offer or warning |
| Requirement failure | Standard quest requirement interface; no state mutation | Custom prose distinguishes QP from quest prerequisites; functional but not current presentation |
| Acceptance | State changes after Yes and the acceptance dialogue | State 1 is written before the help menu; the modern offer boundary is absent |
| Collection | Feather, eel, and armband may be completed in any order | Supported; journal chooses a suggested order but final check is item-based |
| Re-talk with raw eel | Achietties specifically says it must be cooked | No raw-eel branch; generic incomplete response |
| Final hand-in | All three inventory items are consumed as one completion transaction | Queue and three deletes are separate, with no durable settlement record |

### Phoenix Gang route

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Straven | Current master-thief dialogue sets route checkpoint and gives gherkin password | Recently added short substitute dialogue writes state 2; exact transcript/re-talk is absent |
| Alfonse / Charlie | Password unlocks kitchen discussion and secret panel | States 3 and 4 are reachable; broad route is present |
| Partner key | Receive/use Black Arm participant's tradeable miscellaneous key | Any key item works; no paired issuer or attempt is recorded |
| Side room | Enter garden, side door, then key the internal door | Side door accepts key without pairing; old teleport door helper owns traversal |
| Attack Grip | Only ranged or magic through the arrowslit, after Black Arm partner lures Grip | No Grip attack trigger/gate exists; the file explicitly defers it, so generic melee/attack rules can apply |
| Kill credit | Paired Phoenix killer reaches state 5 | Any hero who kills shared Grip while exactly state 4 receives state 5; no paired lurer, location, style, or attempt validation |
| Candlestick | Paired Black Arm retrieves two and gives one to the killer | Any tradeable candlestick can be received |
| Straven hand-in | Accept only if this player killed the relevant Grip | Accepts any inventory candlestick at route range; no provenance check |
| Armband | Checked one-for-one exchange, state 6 | Delete/add/state are separate and output is unchecked |

### Black Arm Gang route

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Katrine | Current dialogue gives Palm Street target and password | Recently added short substitute writes state 7 |
| Grubor | Exact password opens HQ | Implemented; mixed `complete`/`joined` gang comparisons need capture |
| Trobert | Gives ID papers; lost set is replaceable, including full-inventory response | Initial and replacement adds are unchecked; messages claim delivery on failure, though re-talk can retry |
| Garv | Exact black full helm/body/legs plus papers permits entry | Exact untrimmed set is correctly enforced; papers are not consumed here |
| Grip report | Papers are consumed and deputy dialogue begins | Implemented at state 11; current dialogue is mostly present |
| Miscellaneous key | Ask duties, then current work; replace for completed helpers when absent | Inventory/bank total suppresses duplicates, but add is unchecked and completed-helper access is not fully modeled |
| Partner hand-off | Give key to the paired Phoenix participant | Item is tradeable, but no partner or generation identity exists |
| Lure | Black Arm searches cabinet while paired Phoenix waits at slit | Any player can move a shared ambient Grip via broad `npc_find`; no route/attempt gate protects the lure |
| Keyring | Phoenix kill creates a keyring unique to this Black Arm participant/pair | Public ground `grip_keys` is dropped for every credited Grip death; no owner or pairing |
| Treasure door | Requires Grip keyring and prior miscellaneous-key issue | Explicit code accepts either Black Arm state >=11 or Phoenix state >=5, enabling solo completion |
| Chest | Paired Black Arm personally receives two candlesticks | Public loc change; unchecked quantity-two add and no durable chest provenance |
| Katrine hand-in | Accept only if this player retrieved the pair's candlesticks | Accepts any inventory candlestick; no provenance check |
| Armband | Checked one-for-one exchange, state 13 | Delete/add/state are separate and output is unchecked |

The treasure-door comment expressly labels canonical Heroes' Quest as
two-player, then installs a “Soft” one-player path. This is not an accidental
edge case: a Phoenix player can kill Grip, collect the public keyring, open the
door using the Phoenix state predicate, loot the chest, and hand a candlestick
to Straven. A Black Arm player can benefit from any public Grip kill/keyring.
The implementation therefore defeats the quest's defining co-operative
contract and creates cross-pair theft/race conditions when multiple players
are present.

### Completed-player assistance

Current OSRS lets a completed Black Arm member enter the mansion without black
armour, request a new miscellaneous key when appropriate, and lure Grip. A
completed Phoenix member can return through the restaurant and kill the Grip
lured by an in-progress Black Arm member. The in-progress participant still
owns the relevant chest/kill provenance.

Current code has fragments of that behavior but no assistance session. Garv's
door denies completed Phoenix members, while completed Black Arm behavior is
incidental to broad numeric predicates. Grip's key issue path and mansion
doors do not bind the completed helper to a current participant. Repeated or
concurrent help can therefore share one Grip and one public key drop across
unrelated players. Modernization needs a bounded pair/attempt record that lets
completed players help without replaying their own quest or minting arbitrary
keys/candlesticks.

## 5. Detailed lifecycle audit

### Co-operative identity, Grip, and mansion ownership

The modern engine owner should treat the mansion sequence as a two-principal
encounter:

1. The Black Arm participant obtains a miscellaneous-key generation and
   transfers it to one eligible Phoenix participant.
2. That generation establishes or joins a bounded assistance attempt.
3. The Black Arm participant opens the cabinet; only that attempt's Grip is
   lured into the valid room.
4. Only the paired Phoenix participant can attack from the valid side with
   Ranged or Magic.
5. The Phoenix kill milestone is written once; the keyring is claimable only
   by the paired Black Arm participant.
6. The keyring opens the treasure door only for that participant and attempt.
7. Chest settlement produces exactly two candlesticks and records Black Arm
   retrieval provenance.
8. One candlestick can be transferred to the paired Phoenix participant while
   retaining Phoenix kill provenance.
9. Each gang leader validates the participant's own route milestone before
   consuming one candlestick and issuing one armband.

The current world Grip cannot satisfy those invariants. `npc_find` selects a
shared ambient NPC, the cabinet makes it walk globally, generic attack owns
damage, `npc_findhero` identifies only the killer at death, and `obj_add`
creates a public keyring. Another pair can lure, damage, kill, or take the
drop. A completed helper and a fresh participant cannot be distinguished from
unrelated bystanders. Use an owner/attempt-scoped NPC or a modern encounter
controller; do not repair this with more numeric state comparisons on the
public NPC.

Grip's combat stats are current-scale level 22 data, despite Quest Helper's
stale panel text saying level 26. The defect is combat authorization, not the
server's stat block. The attack gate must reject melee, wrong-side attacks,
premature attacks, Black Arm attacks, bystanders, and unrelated Phoenix
players with the canonical player message. It must preserve a legitimate
Ranged/Magic fight if Grip moves, is blocked, respawns, or the lurer repeats
the cabinet action.

### Items and transaction boundaries

| Operation | Required transaction | Current risk |
| --- | --- | --- |
| Trobert -> ID papers | Check all ownership domains and one free slot; add, then confirm | Unchecked add; false success and state advance on initial issue |
| Grip consumes ID papers | Delete one inventory set, then publish state 11 | Precondition/delete is reasonable; no settlement marker needed once state write follows successful delete |
| Grip -> miscellaneous key | Verify valid participant/generation and delivery capacity | Unchecked add; no issued marker or partner generation |
| Key use | Preserve tradeable key and associate its generation with attempt | Any identical item is accepted; identity cannot survive ordinary item trade |
| Grip keyring drop | Owner-safe claim for paired Black Arm player | Public ground drop, no pair, no generation |
| Chest -> two candlesticks | Require two slots or use canonical private-ground fallback; settle exactly two | `inv_add(...,2)` unchecked; state 12 advances. A partial one-item add makes the chest permanently appear empty because possession suppresses re-search |
| Leader exchange | Validate provenance; consume one; add one armband; publish route end | Any candlestick accepted; add unchecked; state can advance without armband |
| Gerrant -> slime | Check all domains and capacity, then add | Explicit global ownership and free-space checks are a good baseline |
| Slime + potion -> oil | Generic Herblore level-25 recipe with 80 XP | Data-driven recipe exists; verify boost, failure, and slot atomicity in shared Herblore tests |
| Oil + rod | Consume two inputs; create empty vial and oily rod | Two consumed slots provide output room; still test reversed use order and interruption |
| Final three items | Consume all three only inside replay-safe settlement | Three deletes occur outside a state-first reward queue |

Pete's candlestick and the miscellaneous key are intentionally tradeable; the
keyring and armband are not. Consequently, provenance cannot be encoded by
making all items untradeable. It belongs in the quest assistance record and
must be preserved through normal trade and Iron item-on-player delivery. The
leader dialogue already has canonical rejection text for an unqualified
candlestick; modern code must use it.

The chest's `~obj_gettotal(petecandlestick)` check correctly includes banked
items and prevents ordinary duplicate looting. It is insufficient because it
does not record who retrieved a candlestick or which attempt produced it. It
also turns a partial quantity-two add into a hard co-operation failure: one
candlestick exists, so the second can no longer be obtained for the partner.

### Fire feather and Ice Queen arm

The rockslide correctly delays Mining 50 until use and calls `stat(mining)`,
allowing boosts. Its item-on-location handler recognizes only bronze through
rune pickaxes. Modern dragon, infernal, crystal, gilded, and other valid
pickaxes fall through even though the direct Mine option's generic checker may
accept them. Replace the hard-coded item list with the engine's canonical
pickaxe capability/category.

The Ice Queen can be fought without starting Heroes' Quest and drops ice
gloves on every credited kill, matching current OSRS. Runtime evidence must
still show normal drop ownership, repeated kills, death, and full-inventory
pickup behavior. The cave ladder chain appears to be supplied by generic
world traversal; capture every entrance/exit coordinate rather than adding a
quest teleport shortcut.

The firebird drops a feather for any killer with `%heroquest < 15`, including
state 0, and the pickup handler then refuses a state-0 player. That creates a
public temporary object without clear owner semantics. Use the normal owner-
safe quest drop path and suppress or harmlessly clean up unclaimable drops.
Pickup currently recognizes worn `ice_gloves` only. Current OSRS also accepts
worn `smithing_uniform_gloves_ice` (Smiths' gloves (i)); the cache item exists
and Quest Helper already identifies it.

Both Entrana guidance owners explicitly say the Heroes' Quest branch is
deferred. The monk should redirect an in-progress player to the High Priest;
the High Priest should explain the firebird, its heat, and the Ice Queen clue.
The 23 July 2025 OSRS change specifically fixed the High Priest failing to give
this guidance after Holy Grail. Route the multi-quest dialogue by relevance so
the Heroes branch remains available after that quest instead of being hidden
by the existing Holy Grail handler.

### Lava eel arm

Gerrant is available only while state is 1-14, gives one slime when no slime,
oil, or oily rod exists in any checked domain, and correctly reports a full
inventory before delivery. The generic Herblore table requires boostable level
25 and makes blamish oil; the oily-rod operation returns the empty vial. Lava
fishing checks boosted Fishing 53, oily rod, bait, and output space. The
cooking row requires boosted Cooking 53, awards 30 XP, and never burns. These
are sound foundations.

The fishing handler is category-bound, which is preferable to one hard-coded
NPC. Authored world placements were found for Taverley Dungeon at
`0_45_152` and the Lava Maze at `0_47_59`. A cache NPC identity exists for
`0_42_138`, but no authored world spawn was found. Current OSRS additionally
allows Charred Dungeon at Sailing 60 and Wyrmscraig Cavern after Fallen From
Grace. Capture the 2026 cache/world placements and route prerequisites, then
ensure the shared category reaches all four without making Heroes' Quest own
Sailing or later-quest access.

Wrong fishing gear is deliberately destroyed by the lava handler. Test every
listed gear family, stacked bait at a full inventory, last-bait consumption,
failed rolls, logout during the action loop, Wilderness interruption, and the
diary hook for catching a Lava Maze eel. The quest journal should not imply
only the old Taverley route.

### Dialogue, journal, and recovery

The current script mostly preserves LostCity dialogue while recent patches add
very short Straven/Katrine route bridges. Restore the pinned current transcript
as a state-tested dialogue graph. Particularly important missing branches are:

- the generic start interface and low-skill warning;
- exact requirement-failure interface behavior;
- Straven/Katrine initial, early-candlestick rejection, and re-talk text;
- completed-helper Grip/key behavior;
- the Black Arm attack refusal and partner hint;
- monk and High Priest firebird guidance after other Entrana quests;
- full-inventory ID papers and two-candlestick ground fallback;
- Achietties's raw-lava-eel response;
- postquest armband replacement from the player's gang leader.

The journal has useful route detail but treats any state greater than 13 as
complete. State 14 therefore displays `QUEST COMPLETE!` while the guild door,
completion helper, downstream quests, and equipment gate require state 15.
Before completion, the journal repeatedly uses current item possession to
stand in for milestones. Dropping a feather, eel, or armband can make text
regress even when the route acquisition was valid; traded candlesticks cannot
represent provenance at all. Render from authoritative route/support/
settlement state, using item ownership only for immediate actionable hints.

| Recovery case | Canonical requirement | Current behavior / gap |
| --- | --- | --- |
| Lost ID papers | Trobert replaces while still needed | Retry exists; unchecked full-inventory add reports success |
| Lost miscellaneous key | Grip can replace under route/helper rules; banked copy prevents another | Global item check partly works; no durable issue/pair record |
| Stale keyring for a different partner | Drop/reconcile it and obtain the current pair's keyring | No pair identity; any public keyring satisfies the door |
| Grip blocked/leaves lure room | Cabinet can lure again without advancing unrelated players | Shared ambient NPC can be controlled by any player |
| Full inventory at chest | Two candlesticks fall privately/safely; retrieval remains attributable | Unchecked add may grant zero/one while state advances; public loc change |
| Early/traded candlestick | Leader rejects with provenance-specific dialogue | Both leaders consume any inventory candlestick |
| Lost armband before finish | Route leader supplies a replacement according to captured current behavior | No replacement branch at state 6/13 |
| Lost armband after quest | Own gang leader gives a spare in members worlds | Transcript branch absent |
| Lost slime/oil/rod | Gerrant or snail-derived recipe permits recovery without duplication | Gerrant suppresses issue if any intermediate exists; alternate snail path is shared content and needs proof |
| Lost fire feather | Kill/retrieve another before completion | Repeat drop works; owner/public semantics unproven |
| Lost ice gloves | Kill Ice Queen again | Repeatable guaranteed drop exists |
| Interrupt final hand-in | Resume missing consumption/rewards once | No settlement record; state/item/reward ordering is unsafe |

## 6. Completion settlement and downstream consumers

Achietties queues completion, then deletes the feather, armband, and eel in the
caller. The completion queue immediately writes state 15, grants twelve XP
awards, then calls the generic quest-completion helper for QP and presentation.
There is no durable marker for any component. Failure after the state write
can make every later re-talk return immediately while one or more rewards are
missing. Conversely, retrying an incompletely recorded queue cannot safely
distinguish settled from unsettled XP.

Replace this with one replay-safe settlement record containing independent
once-only markers for:

1. verified route and possession preconditions;
2. fire feather consumption;
3. cooked lava eel consumption;
4. thieves' armband consumption;
5. each of the twelve exact XP awards;
6. one quest point and completion-count publication;
7. reward/unlock presentation; and
8. final state-15 publication.

State 15 should be the final externally visible commit, or a reconciliation
reader must complete missing settlement before any state-15 consumer runs.
Failure injection after every operation must resume only missing work. The
reward scroll string currently names only four of twelve XP grants plus guild
and dragon-weapon access. Capture the current client interface and either show
all canonical rewards through its supported layout or document deliberate
pagination; do not silently omit the other skills, fountains, or scroll
unlock from the user-facing contract.

| Consumer | Canonical dependency | Current result / required work |
| --- | --- | --- |
| Heroes' Guild doors | State 15 completion | Explicit gate works; state-14 journal disagreement must be removed |
| Happy Heroes' H'emporium | Guild access; dragon mace and battleaxe stock | Shop stock/op3 exists; Helemos talk still says nothing to trade |
| Dragon mace/battleaxe equip | Completion plus normal skill requirements | Explicit Heroes gate exists and is called by equip path |
| Fountain of Heroes | Completed guild access; recharge glories to four | Handler exists behind guild topology; add explicit authorization defense if alternate access is possible |
| Fountain of Rune | Completion permits six-charge glory outcome | Handler has no Heroes check and grants the benefit globally |
| Charge dragonstone jewellery scroll | Completion required to use | Loot identity exists, but no use handler or Heroes gate was found |
| Throne of Miscellania | Completion prerequisite | Door/guard checks state 15; `misc_journal.rs2` visually inverts completed/missing markup and needs correction |
| Legends' Quest | Completion prerequisite | Journal and Radimus start checks exist |
| Recipe for Disaster: Sir Amik Varze | Completion prerequisite | Explicit state-15 gate exists |
| The Fremennik Exiles | Completion prerequisite | Quest script explicitly defers quest/skill hard gates and starts without checking Heroes' Quest |
| Hard Falador Diary | Completion prerequisite | Generic diary counters exist; no Heroes-specific task/start enforcement was found |
| Wilderness Diary | Catching a Lava Maze eel is a hard task | Fishing placement exists; no task hook was found in the lava handler |
| Ice gloves consumers | Fareed, dwarven rock cake, Blast Furnace, and other shared uses | Treat as shared item regression scope; do not make the gloves quest-private |
| Quest journal/list | Correct state and route presentation | Registered; state 14 and possession-derived progress are unsafe |
| Quest cheat | Coherent completed/admin fixture | State-only write bypasses all rewards and settlement history |

The direct downstream prerequisite list on the current Wiki is Throne of
Miscellania, Legends' Quest, Recipe for Disaster, The Fremennik Exiles, and
Hard Falador Diary. Completion modernization is not done until each gate is
tested against fresh, migrated, partially settled, and admin-completed saves.
Unlock benefits are equally important: a perfect reward scroll does not
compensate for globally available Fountain of Rune charges or an unusable
charge-dragonstone scroll.

## 7. Modernization sequence

### Phase 0 — capture, ownership map, and save safety

1. Capture exact primary/gang state, state-14 meaning, current dialogue, full-
   inventory outcomes, completed-helper flows, item-on-player delivery, and
   provenance rejection from current OSRS.
2. Inventory every mansion actor/loc placement, arrowslit collision rule,
   attack route, Ice Queen ladder, and all four lava-fishing placements.
3. Select the engine's modern owner for pair sessions, private NPCs, private
   ground items, item transactions, and replay-safe quest settlement.
4. Freeze fixtures for every primary state, both gangs, contradictory legacy
   states, solo-bypass saves, completed helpers, state 14, and partial state-15
   rewards.
5. Define a versioned migration and legacy-provenance policy before changing
   any live write.

Exit: exact evidence resolves state 14, gang thresholds, pair/recovery rules,
and every current route; no migration decision relies on item possession or
proximity alone.

### Phase 1 — acceptance and independent collection arms

1. Move acceptance to the generic quest-offer/requirements interface and
   restore the current transcript, including skill warning.
2. Keep skills boostable and deferred to their actual actions.
3. Modernize the rockslide pickaxe capability, Smiths' gloves (i), private
   feather drop, monk/High Priest dialogue, and Ice Queen loot proof.
4. Preserve the data-driven Herblore/cooking implementation while adding the
   two missing modern lava-fishing worlds and diary hook.
5. Make all temporary-item delivery/recovery paths checked and truthful.

Exit: feather and eel arms are independently complete through every loss,
full-inventory, logout, and modern-route case, with no false-success write.

### Phase 2 — two-player armband encounter

1. Introduce a bounded opposite-gang assistance attempt supporting in-progress
   participants, completed helpers, ordinary trade, and Iron item-on-player.
2. Replace shared Grip control with attempt-scoped lure/combat ownership.
3. Enforce location, lurer, gang, participant, and Ranged/Magic attack rules.
4. Make keyring/chest drops private and generation-bound; deliver exactly two
   candlesticks with durable Black Arm and Phoenix provenance.
5. Remove the solo treasure-door predicate and validate each gang leader's
   canonical provenance before a transactional armband exchange.
6. Implement lost/stale key, blocked Grip, disconnect, death, re-pair, and
   completed-helper recovery without cross-pair influence.

Exit: two unrelated pairs can run concurrently without sharing Grip, keys,
candlesticks, state, or credit, and no single account can complete the armband
route alone.

### Phase 3 — settlement, migration, and consumers

1. Implement replay-safe item/reward settlement and publish state 15 last.
2. Repair audited legacy state-14 and partial state-15 fixtures component by
   component; never infer rewards from state alone.
3. Drive the journal and admin fixtures from the same route/support/settlement
   model.
4. Implement every unlock gate: Fountain of Rune, charge-dragonstone scroll,
   Fremennik Exiles, Falador Hard, and Wilderness diary catch task.
5. Correct stale Helemos and Miscellania journal presentation and verify all
   already-present downstream gates.

Exit: all rewards and benefits settle exactly once and all consumers observe
one coherent completion history.

## 8. Required tests

### State, acceptance, and migration

- Fresh state 0 shows the correct journal and current offer interface.
- Low Cooking/Fishing/Mining/Herblore produces the warning but does not block
  a qualified player's acceptance.
- Refusing the offer changes no state; accepting publishes state 1 only at the
  captured boundary.
- Each missing QP/prerequisite combination reports correctly.
- Phoenix states 2-6 and Black Arm states 7-13 route only with compatible
  Shield of Arrav gang state.
- Joined/complete gang thresholds match captured current semantics.
- Contradictory primary/gang fixtures are repaired or quarantined
  deterministically.
- State 14 never reports completion until its captured meaning is satisfied.
- Legacy solo-bypass fixtures migrate under the documented provenance policy.
- Journal output is correct for every primary/support state without treating
  a traded item as proof.

### Ice gloves and fire feather

- Every valid modern pickaxe works through use-on and direct Mine; invalid
  tools fail; boosted 50 Mining succeeds and 49 fails.
- Rockslide/ladders work in both directions and remain safe under concurrency.
- Ice Queen is available before the quest and drops exactly one owner-visible
  pair of gloves on every credited kill.
- Death, logout, full inventory, repeated kills, and another player's kill do
  not lose or duplicate the gloves.
- Monk and High Priest dialogue appears only in the relevant quest window and
  remains reachable before/after Holy Grail.
- Firebird kill ownership is correct at states 0, 1-14, and 15.
- State 0 cannot claim a feather; state 1-14 can with worn ice gloves or worn
  Smiths' gloves (i), but not with either merely carried.
- Hot pickup damage/message and safe ground-item cleanup match the transcript.
- Lost feather can be reacquired before completion and not after canonical
  completion.

### Lava eel

- Gerrant issues exactly one slime across inventory/bank/worn/ground rules and
  reports full inventory without claiming delivery.
- Boosted Herblore 25 makes oil for correct XP; 24 fails without consuming.
- Oil/rod use works in either order and produces exactly one empty vial and
  oily rod.
- Boosted Fishing/Cooking 53 succeeds; 52 fails without loss.
- Each of Taverley, Lava Maze, Charred Dungeon, and Wyrmscraig has correct
  access prerequisites and a working category handler.
- Dusty-key and 70-Agility Taverley routes remain shared dungeon behavior.
- Wrong nets, harpoons, and rods burn exactly once; unrelated items do not.
- Full inventory with one versus multiple bait behaves correctly.
- Raw lava eel never burns and cooking awards exactly 30 XP.
- Lava Maze catch completes the correct Wilderness Diary hard task once.
- Achietties gives the raw-eel-specific response and does not consume it.

### Two-player armband route

- Every matrix case covers Black Arm/Phoenix in-progress players, each side as
  completed helper, ordinary accounts, Iron item-on-player, and disallowed
  same-gang pairing.
- Miscellaneous key issue is capacity-safe, replaceable, and generation-bound.
- An unrelated or stale key cannot join/open another attempt.
- Black Arm exact armour is required during first infiltration; trimmed pieces,
  plateskirt, and missing papers fail; completed-helper exception is correct.
- ID paper initial/replacement delivery is capacity-safe and nonduplicating.
- Only the paired Black Arm participant can lure the attempt's Grip.
- Only the paired Phoenix participant can attack after the lure, through the
  valid slit, using Ranged or Magic.
- Melee, Black Arm, premature, wrong-side, unrelated, and cross-pair attacks
  receive canonical refusal and award no credit.
- Grip blocking, retreat, respawn, cabinet retry, death, logout, and region
  leave recover without corrupting the attempt.
- The keyring is visible/claimable only to the paired Black Arm participant.
- Treasure door rejects a keyring when no valid miscellaneous-key issue exists.
- Chest grants exactly two attributed candlesticks or safe private-ground
  fallbacks at 0, 1, and 2 free slots.
- Banked/held candlestick makes the chest empty without erasing provenance.
- Black Arm gives one candlestick to Phoenix; Iron delivery preserves both
  participants' distinct provenance.
- Straven rejects a Phoenix candlestick without the paired kill; Katrine
  rejects a Black Arm candlestick without paired chest retrieval.
- Leader exchange consumes one, grants one armband, and advances once under
  injected failure at every operation.
- Two or more pairs in one mansion cannot move, attack, loot, or settle one
  another's encounter.
- Lost armband replacement works before and after completion without replaying
  a candlestick or creating quest progress.

### Completion and downstream consumers

- All three required items must be in inventory simultaneously; banked,
  ground, raw, or unproven substitutes fail.
- Failure after each item consumption, each of twelve XP grants, QP, screen,
  and final state resumes only the missing operations.
- Every XP amount matches the native dbrow/current Wiki, including 2,575
  Mining and 2,275 Smithing.
- Completion awards exactly one QP and one completion publication.
- State 15 is unavailable to consumers until settlement is durable.
- Guild door, shop stock, dragon weapon equip, and Fountain of Heroes work only
  for a coherently completed player.
- Fountain of Rune gives six-charge glory benefits only after completion while
  preserving its other jewellery and eternal-glory rules.
- Charge-dragonstone jewellery scroll rejects incomplete players and works
  after completion.
- Throne of Miscellania, Legends, RFD Sir Amik, Fremennik Exiles, and Falador
  Hard all enforce completion; their journals show the same result.
- Admin fixtures distinguish “set state for testing” from fully settled
  completion and can build either route/helper shape explicitly.

## 9. Acceptance evidence

Gate A requires a full state/ownership table from fresh, migrated, interrupted,
legacy-solo, completed-helper, Iron, and admin fixtures; exact current captures
for state 14 and gang thresholds; and a source map for every NPC, loc, item,
skill owner, pairing field, and downstream consumer.

Gate B requires automated traces for acceptance, every feather/eel route, both
gang routes, every partner/helper combination, lost-item recovery, full-slot
outcomes, and completion. Each trace must assert primary/support state, all
item domains, actor/location presentation, provenance, and settlement after
every transition.

Gate C requires concurrent two-pair mansion tests plus interruption injection
during key issue/trade, lure, combat, death, keyring pickup, door use, chest
delivery, both leader exchanges, and every completion operation. No solo
completion, public/cross-pair loot, wrong-player credit, duplicate item, or
lost reward is acceptable.

Gate D requires a pinned-reference review against the article, guide, and
transcript; native dbrow/reward parity; Quest Helper discrepancy notes;
journal/admin parity; and automated proof for every guild benefit, later-quest
prerequisite, and diary task. Record the Wiki revisions and Quest Helper commit
in the test artifact.

The dossier may move to `modernized` only after all four gates have checked
evidence. Reaching state 15 on one account, opening the solo treasure door, or
displaying the current reward scroll is not completion evidence.

## 10. Prioritized findings

### P0 — canonical reachability, ownership, and settlement

1. The treasure door intentionally permits a Phoenix or Black Arm player to
   substitute route state for the opposite-gang partner, making the defining
   two-player sequence soloable.
2. Shared Grip, broad luring, generic attack, killer-only state, and a public
   keyring have no pair/attempt ownership; unrelated players and pairs can
   interfere or steal credit/items.
3. No Grip attack gate enforces Phoenix identity, paired lure, arrowslit side,
   or Ranged/Magic; Black Arm/melee/bystander attacks are not canonically
   rejected.
4. Straven and Katrine accept any tradeable inventory candlestick without the
   current kill/retrieval provenance rules.
5. Completion writes state 15 before twelve XP awards and generic QP/reward
   settlement, while final-item deletes occur separately; no replay-safe
   settlement exists.
6. Chest quantity-two delivery and both leader armband outputs are unchecked;
   partial delivery can permanently remove the partner's second candlestick,
   and route state can advance without its required item.
7. Existing state-15 saves may be partially rewarded, and state alone cannot
   identify which components need repair.

### P1 — modern route, dialogue, and recovery

1. There is no durable secondary state for key issue, partner identity, lure,
   kill/retrieval provenance, chest settlement, armband issue, or reward
   settlement.
2. Start bypasses the modern quest-offer interface and low-skill warning.
3. Entrana monk and High Priest Heroes dialogue is explicitly deferred,
   including the post-Holy-Grail routing fixed in OSRS in 2025.
4. Fire-feather pickup omits Smiths' gloves (i), and the rockslide use-on path
   hard-codes only bronze-rune pickaxes.
5. Only Taverley and Lava Maze fishing placements were found; current Charred
   Dungeon and Wyrmscraig routes are absent.
6. ID papers, miscellaneous key, chest items, and leader outputs have unchecked
   add paths or false-success messaging.
7. Completed-player assistance, stale partner keyrings, and lost armband
   replacement are not modeled canonically.
8. Journal state 14 disagrees with every state-15 consumer and uses item
   possession in place of authoritative milestones.

### P2 — downstream completeness and diagnostics

1. Fountain of Rune grants completion-gated six-charge glory behavior without
   checking Heroes' Quest.
2. Charge dragonstone jewellery scrolls have no use handler/quest gate despite
   appearing in loot data.
3. The Fremennik Exiles explicitly defers all quest/skill hard gates, and no
   Heroes requirement was found for it or Hard Falador Diary.
4. The Lava Maze fishing handler has no Wilderness Diary hard-task hook.
5. Helemos dialogue says the shop has nothing while op3 opens stocked dragon
   weapons; the Miscellania journal visually inverts Heroes completion.
6. The reward presentation lists only four of twelve XP grants and omits
   several unlocks pending exact interface capture.
7. Quest cheat completion writes state 15 only and can masquerade as a normal
   completion to every current consumer.
8. No automated state, two-player, provenance, transaction, migration,
   completion, or downstream Heroes' Quest tests were found.

## 11. Evidence still required before implementation

- Exact current-server state values/writes for state 14 and every route action,
  including how the client distinguishes route provenance without visible
  support varbits.
- Exact Shield of Arrav `joined`/`complete` semantics for Heroes route choice,
  imported saves, and players repaired by the 5 October 2022 OSRS fix.
- Current pairing/key-generation behavior for ordinary trade, item-on-player
  Iron delivery, changing partners, stale keyrings, and multiple concurrent
  attempts.
- Whether Grip is instanced, per-player transformed, or controlled through
  server-side encounter metadata on current OSRS, including respawn and
  completed-helper recovery.
- Exact attack geometry/style validation and messages when Grip is blocked,
  moves, retreats, or is attacked from the wrong tile/style.
- Exact private-ground ownership/duration for keyring, two candlesticks,
  fire feather, ice gloves, full-inventory fallback, and death/logout.
- Exact pre-completion lost-armband recovery, supplementing the confirmed
  postquest transcript branch.
- All four 2026 lava-fishing NPC placements and the Charred/Wyrmscraig access
  owners in this cache/content revision.
- Current reward-interface layout and whether all twelve XP/unlock lines are
  shown, paged, or summarized.
- The intended modern engine records/helpers for paired quest encounters,
  item provenance across trade, and replay-safe multi-reward settlement.
- Exact Falador Hard task that creates the aggregate Heroes requirement and
  how the diary UI exposes unmet quest prerequisites.
