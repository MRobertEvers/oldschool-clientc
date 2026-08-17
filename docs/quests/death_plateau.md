# Death Plateau modernization audit

Status: `audit-pending` — this is a comparatively substantial LostCity port:
the native quest state, both parallel quest branches, real actors and doors,
most dialogue, the Harold gamble, secret-path traversal, journal, post-quest
boots, combat scenery, and shared completion call exist. It is nevertheless not
organically completable. All five world-spawned stone balls are green, and the
authored replacement puzzle state permanently locks a mechanism slot when a
ball is picked up, expires, or was placed before the Combination was read.
Claw smithing is also available before completion, the gambling presentation
and danger cutscene are legacy stubs, several item grants are non-atomic, and
the advertised Troll Stronghold continuation is a dead dialogue branch.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native state, parallel secret-route state,
Harold's wager, world-object puzzle, shared Burthorpe actors, item recovery,
completion transaction, claw/boot unlocks, downstream quests, journal, combat
scenery, and debug adapters. It is an implementation specification, not
verification evidence.

## 1. Authoritative references

The article and quick guide define requirements, parallel route, puzzle,
rewards, and unlocks. The transcript defines choices, re-talks, gambling,
full-inventory ground fallback, loss/replacement, either-order final handoff,
and post-quest dialogue. Revisions were resolved through the OSRS Wiki API on
2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Death Plateau](https://oldschool.runescape.wiki/w/Death_Plateau?oldid=15292389) | 15292389, 2026-08-11 | Identity, requirements, walkthrough, rewards, and unlocks |
| [Death Plateau/Quick guide](https://oldschool.runescape.wiki/w/Death_Plateau/Quick_guide?oldid=14785226) | 14785226, 2024-10-23 | Exact route, wager thresholds, puzzle layout, supplies, and scout boundary |
| [Transcript:Death Plateau](https://oldschool.runescape.wiki/w/Transcript%3ADeath_Plateau?oldid=15263284) | 15263284, 2026-07-14 | Dialogue, dice flow, ground fallback, recovery, handoffs, and completion |
| [Denulth](https://oldschool.runescape.wiki/w/Denulth?oldid=15196200) | 15196200, 2026-04-25 | Start, certificate, completion, and Troll Stronghold continuation |
| [Eohric](https://oldschool.runescape.wiki/w/Eohric?oldid=15240477) | 15240477, 2026-06-27 | Harold routing and repeated dialogue |
| [Harold](https://oldschool.runescape.wiki/w/Harold?oldid=15283607) | 15283607, 2026-07-30 | Ale, Blurberry, dice bankroll, IOU, and replacement |
| [Saba](https://oldschool.runescape.wiki/w/Saba?oldid=15229194) | 15229194, 2026-06-07 | Secret-route lead and post-quest dialogue |
| [Tenzing](https://oldschool.runescape.wiki/w/Tenzing?oldid=15229195) | 15229195, 2026-06-07 | Supplies, boots/map recovery, sale, and Troll Romance bridge |
| [Dunstan](https://oldschool.runescape.wiki/w/Dunstan?oldid=15289542) | 15289542, 2026-08-07 | Certificate and spiked-boots exchange plus shared quest roles |
| [The Toad and Chicken](https://oldschool.runescape.wiki/w/The_Toad_and_Chicken?oldid=15296989) | 15296989, 2026-08-13 | Harold room and free ale location |
| [Burthorpe Castle](https://oldschool.runescape.wiki/w/Burthorpe_Castle?oldid=15240478) | 15240478, 2026-06-27 | Eohric, puzzle, equipment room, and trapped archer geography |
| [Death Plateau (location)](https://oldschool.runescape.wiki/w/Death_Plateau_%28location%29?oldid=15229238) | 15229238, 2026-06-07 | Main and secret paths, troll risk, and shortcuts |
| [Asgarnian ale](https://oldschool.runescape.wiki/w/Asgarnian_ale?oldid=15248094) | 15248094, 2026-07-02 | Required first drink |
| [Blurberry special](https://oldschool.runescape.wiki/w/Blurberry_special?oldid=15183590) | 15183590, 2026-04-22 | Optional guaranteed-win route |
| [Iou](https://oldschool.runescape.wiki/w/Iou?oldid=15187403) | 15187403, 2026-04-22 | Gamble result, Read transform, and recovery |
| [Combination](https://oldschool.runescape.wiki/w/Combination?oldid=15183788) | 15183788, 2026-04-22 | Five spatial clues and final handoff |
| [Stone ball](https://oldschool.runescape.wiki/w/Stone_ball?oldid=15183566) | 15183566, 2026-04-22 | Five colours, ground spawns, placement, and pickup |
| [Certificate](https://oldschool.runescape.wiki/w/Certificate_%28Death_Plateau%29?oldid=15185787) | 15185787, 2026-04-22 | Denulth/Dunstan handoff and replacement |
| [Secret way map](https://oldschool.runescape.wiki/w/Secret_way_map?oldid=15183593) | 15183593, 2026-04-22 | Tenzing grant/replacement and final handoff |
| [Climbing boots](https://oldschool.runescape.wiki/w/Climbing_boots?oldid=15183452) | 15183452, 2026-04-22 | Quest pair, wear gate, and post-quest purchase |
| [Spiked boots](https://oldschool.runescape.wiki/w/Spiked_boots?oldid=15239316) | 15239316, 2026-06-25 | Dunstan transformation and Tenzing supply |
| [Steel claws](https://oldschool.runescape.wiki/w/Steel_claws?oldid=15182876) | 15182876, 2026-04-22 | Completion item reward |
| [Claws](https://oldschool.runescape.wiki/w/Claws?oldid=15248984) | 15248984, 2026-07-02 | Post-quest smithing unlock |
| [Lucky Win](https://oldschool.runescape.wiki/w/Lucky_Win_%28Death_Plateau%29?oldid=15290782) | 15290782, 2026-08-09 | Winning gamble jingle |
| [Bad Roll](https://oldschool.runescape.wiki/w/Bad_Roll_%28Death_Plateau%29?oldid=15290740) | 15290740, 2026-08-09 | Losing gamble jingle |
| [Troll Stronghold](https://oldschool.runescape.wiki/w/Troll_Stronghold?oldid=15231622) | 15231622, 2026-06-11 | Direct downstream quest |

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/deathplateau)
maps all nine pre-completion primary states, 32 route coordinates, 17 items,
six NPCs, and seven locs. `python3 tools/questhelper_extract.py deathplateau
--check` exits 0. It does not prove generated spawns, public ground-object
ownership, side-state persistence, inventory transactions, modal recovery, or
unlock enforcement.

## 2. Canonical contract

Death Plateau is a members, novice, short quest released 9 August 2004 and the
first Troll-series quest. It starts with Denulth at `(2896,3528,0)` in the
Burthorpe Imperial Guard camp. It has no skill, combat, quest-point, or quest
prerequisite.

The required supplies are an Asgarnian ale, at least 60 coins for Harold's
bankroll (up to 1,000 is practical), ten unnoted loaves of bread, ten unnoted
cooked trout, and one iron bar. A Blurberry special or premade Blurberry special
is optional and makes every wager an automatic win. Delivering Tenzing's 21
items requires them together in inventory.

The two independent objectives may progress in parallel:

1. identify Harold, buy him an ale, bankrupt him at dice, read the IOU, place
   five coloured balls, and unlock the equipment room; and
2. find Saba and Tenzing, arrange Godric's enlistment, spike Tenzing's boots,
   deliver his winter supplies, receive the map, and scout the secret path.

Completion awards one quest point, 3,000 Attack XP, steel claws, the ability to
smith claws, the ability to buy and wear climbing boots, and access to the
mountain route. Death Plateau directly gates Troll Stronghold; Troll Romance
depends on that successor and therefore on Death Plateau transitively.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 58 / 23 |
| Dbrow | `quest_deathplateau` |
| Type / difficulty / length | Members; novice; short |
| Release / series | 9 August 2004; Troll #1 |
| Start | Denulth (`death_ig_commander`, native NPC 4083), `(2896,3528,0)` |
| Primary state | Native permanent transmitted `%death_equiproom` (osrs239 varp 314) |
| Secret-route state | Server-authored permanent `%death_map`, values 0–8 |
| Gamble/final handoff | Server-authored permanent `%death_bits` |
| Puzzle placement | Server-authored permanent `%death_stones` |
| End state | 80 |
| Reward | 1 QP; Attack `30000` tenths; steel claws and unlocks |

The dbrow correctly supplies identity, membership, difficulty, length,
Burthorpe location, release, series, start, end state, quest points, and Attack
XP. It has no requirement rows, matching the Wiki.

Only `%death_equiproom` survives as a named native osrs239 varp. Its local
config comment incorrectly calls it osrs239 varp 58; `all.varp.compack` resolves
it to 314. The number 58 is the quest dbrow's metadata `id`, not this varp. The LostCity
source packed secret-route state and current wager into separate bit ranges of
`death_map`; osrs239 exposes no matching named side fields. The port therefore
uses authored permanent fields. This is acceptable only with a documented
migration contract and tests; do not guess that anonymous `varp_315` retains
the old meaning. A live-client trace should settle whether current OSRS has
hidden replacement varbits before Gate D.

### 3.1 Primary equipment-room ladder

| `%death_equiproom` | Canonical transition | Current result |
| ---: | --- | --- |
| 0 | Not started; accept or refuse Denulth | Detailed choice tree works |
| 10 | Accepted; ask Eohric for last night's guard | Works |
| 20 | Eohric identifies Harold | Works |
| 30 | Harold refuses; return to Eohric | Works, journal hint is weak |
| 40 | Eohric reveals drink/gambling weakness | Works |
| 50 | Asgarnian ale delivered; gamble | Works mechanically via inline approximation |
| 55 | Harold bankrupt; IOU held | Read/reclaim routes exist |
| 60 | IOU read; Combination held; solve balls | Impossible organically because only green balls spawn |
| 70 | Door unlocked; retain Combination for Denulth | Door and final handoff work if state is forced |
| 80 | Complete | Shared completion is called, but queued procedure is not internally guarded |

### 3.2 Secret-route ladder

| `%death_map` | Canonical transition | Current result |
| ---: | --- | --- |
| 0 | No route lead | Saba is reachable but appropriately unhelpful prequest |
| 1 | Saba identifies a nearby sherpa | Correct |
| 2 | Tenzing gives climbing boots and supply list | State may advance before boot delivery succeeds |
| 3 | Dunstan demands Godric's enlistment | Correct |
| 4 | Denulth gives certificate | Replacement exists; grant is not capacity-safe |
| 5 | Dunstan consumes certificate; boots still need spiking | Exchange works and can recover a previously lost quest pair |
| 6 | Tenzing consumes spiked boots, bread, and trout | Exact unnoted quantities required; map grant follows immediately |
| 7 | Secret map held; scout back path | Map replacement and door/stile route exist |
| 8 | Safe path scouted | Correct zone transition; ready for final handoff |

### 3.3 Bitfields

`%death_bits` reserves four bits each for Harold/player rolls, one gold-claimed
bit, 15 bits of Harold bankroll, lost-all/drunk flags, and map/Combination
handoff flags. The inline wager uses only bankroll, lost-all, and drunk; roll,
gold-claimed, and persistent-bet semantics were dropped with the dice modal.
Final handoffs correctly support either order. Completion clears bits 0–27.

`%death_stones` stores correct-colour bits 0–4 and occupied bits 8–12. It is a
lossy replacement for LostCity's `obj_find` inspection of actual ground items.
No pickup, expiry, reset, logout, or completion path clears it.

## 4. Implementation and world surface

The quest root contains 2,116 lines across five configs and eleven scripts.
It has no raw entity IDs or active legacy `if_openmain` calls. Dialogue uses
modern choices and completion/journal use shared APIs. Its external surface is
material.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `quest_death.constant` / `.varp` | State aliases and three authored carriers | Ladder correct; custom puzzle carrier is unsafe |
| `death_denulth.rs2` | Start, certificate, final handoff, completion, successor offer | Faithful dialogue; completion queue re-entry and Troll Stronghold are unresolved |
| `death_harold.rs2` | Ale/Blurberry, wagers, IOU/reclaim | Functional inline approximation; modal, animations, persistence, jingles omitted |
| `death_doors_mechanism.rs2` | Four doors and stone puzzle | Doors work; puzzle is critically broken |
| `death_saba_eohric.rs2` | Side-state accessors, Saba, Eohric | Core quest branches are faithful |
| `death_tenzing.rs2` | Supplies, map, boots, postquest sale, Troll Romance bridge | Core flow exists; several grants/purchase are non-atomic |
| `death_dunstan.rs2` | Certificate/boots and shared later quest menu | Core exchange works; broad use-on fallback and deferred shared arms need reconciliation |
| `death_iou_scout.rs2` | IOU transform, Combination read, scout zone | State transitions work; Combination uses a text box instead of handwriting panel |
| `death_locs.rs2` | Cave, stile, rocks, boots, warning sign | Traversal exists; warning cinematic is a message stub |
| combat/thin NPC files | Archers, soldiers, ambient dialogue | Real actors exist; ambient timers, clue drop, and ranged audit remain |
| generated `m45_55.spawn` | Burthorpe actors/items | All actors present; all five ball entries incorrectly resolve to green |
| area Burthorpe scripts | Troll throwers and postquest citizens | Main-path danger and world reaction exist |
| Smithing dbrows/scripts | All metal claw products | No Death Plateau predicate; advertised reward is globally available |
| `quest_troll` | Troll Stronghold partial root | No start arm owns Denulth; Death Plateau prints “not available” |
| journal / cheat | Dbrow dispatch and `::complete` | Present; cheat sets only primary state 80; no quest reset/walk harness exists |

All required quest NPCs have world spawns: Denulth, Eohric, Harold, Saba,
Tenzing, Dunstan, and the trapped archer. The cave, Tenzing doors, stile,
equipment-room door, puzzle locs, warning sign, and climbing rocks are native
placed locs with unique trigger ownership.

## 5. Equipment-room branch

### 5.1 Eohric and Harold

The start/refusal and Eohric/Harold re-talk structure closely follows the
transcript. Preserve that work. The free Asgarnian ale is spawned at the pub
bar. `opnpcu` accepts the canonical ale and Blurberry variants plus other
alcohols that route to Harold's requested-drink dialogue rather than consuming
arbitrary alcohol.

Harold begins with 100 coins. Wagers must be 1–1,000 and the player must retain
the stake when the roll begins. Normal play rolls two dice each, with ties lost
by the player; each win transfers the wager until Harold cannot cover it, when
he pays the remainder and issues an IOU. A wager of 101 can bankrupt his initial
bankroll in one win. Blurberry sets the drunk state and makes Harold believe the
player rolled sixteen, guaranteeing a win.

Current arithmetic broadly models that economy. It replaces the gambling
interface with two `mesbox` roll lines, does not use the available dice-roll
sequences or stored roll/gold-claimed fields, has no close-after-Harold-roll
stake-loss path, and omits Lucky Win/Bad Roll jingles and drunk-result variants.
Before changing it, resolve the osrs239 interface and its onload/server ops from
cache or a live client. If it exists, mount it in a named modern slot, persist
bet/roll ownership across remount, and arm all operations. If it does not,
retain a server-authored presentation that still proves animations, cancellation,
stake ownership, logout, and exactly-once settlement.

When Harold cannot pay, state 55 is written before `inv_add(death_iou)`. His
re-talk detects absence and replaces either IOU or Combination, which prevents
a permanent lock, but canonical full-inventory delivery drops the item as an
owner-scoped ground object. Make the initial and replacement grants explicit,
safe, and idempotent. Reading the IOU must atomically transform exactly one IOU
into one Combination; with the input slot reused, no capacity failure is needed.

The Combination currently renders as a generic `mesbox`. The original used a
handwritten message scroll. Resolve the current cache panel; if present, mount
and populate it with the five clues through modern interface machinery. The
plain text remains an acceptable fallback only if the cache genuinely has no
usable current panel, and that finding is documented.

### 5.2 Stone puzzle: critical blocker

The target layout, with north at the top, is:

| Tile | Required ball |
| --- | --- |
| `(2894,3562,0)` | Blue, bottom-left |
| `(2895,3562,0)` | Yellow, bottom-right |
| `(2894,3563,0)` | Red, middle-left |
| `(2895,3563,0)` | Purple, middle-right |
| `(2895,3564,0)` | Green, top-right |

The generated world file supplies five `death_cannonball_green` objects at
`(2893,3561..3565,0)`. Blue, yellow, red, and purple have no production spawn or
grant anywhere. Correct the authoritative spawn dataset/generation mapping and
regenerate; do not hand-edit a generated `.spawn` file.

Changing those five names is insufficient. On placement, current code drops
the inventory ball on the mechanism for 1,000 ticks and permanently sets an
occupied bit. Generic pickup does not clear either occupancy or correctness.
Expiry, another player's pickup, logout, and wrong placement also leave the bit.
The player can never put a replacement in that slot. Further, the Wiki permits
placing balls before reading the IOU, then picking up and replacing one after
state 60; the custom bits make that recovery impossible.

Restore actual object inspection with the now-hosted `obj_find`, or implement a
player-owned puzzle controller whose visual objects and stored slots are one
atomic source of truth. It must support pickup, replacement, wrong layouts,
early correct layouts, expiry/recreation, relog, multiple simultaneous players,
and cleanup on unlock/reset. Solve only while state 60 and after the final
placement. Do not persist “occupied” independently of the visible/takeable ball.

## 6. Secret-path branch

Saba's cave routing, Tenzing's gated front/back doors, the stile, map state, and
scout zone are present. The main danger route also has real aggressive troll
spawns and ranged thrower combat. The danger sign omits its camera/troll-throw
sequence and shows a message instead; restore the bounded cinematic with player
protection, camera reset, interruption, logout, region-change, and death safety.

Tenzing's initial acceptance writes state 2 and then adds climbing boots. The
pinned article documents a live-game full-inventory loss edge case, while the
transcript marks several replacement grants as ground fallback. Modernization
must not silently copy a known item-loss oversight: use one free-slot check or
an owner-scoped ground fallback, record the deliberate safety behavior, and
make replacement immediately available rather than only after advancing through
Dunstan and Denulth.

The Dunstan/certificate sequence otherwise matches the parallel ladder. Denulth
can replace a missing certificate at state 4. Dunstan consumes it before asking
for boots and iron, allowing Tenzing to replace missing quest boots at state 5.
Both flows still call `inv_add` without an explicit space/fallback contract.
The boots conversion consumes two items and produces one, so it is naturally
capacity-safe once both inputs are verified. Existing ownership checks using
`obj_gettotal` correctly recognize a pre-owned/banked spiked pair.

Tenzing requires exactly ten unnoted bread, ten unnoted cooked trout, and one
spiked pair in one inventory. Current code validates all three before deleting
anything, consumes them, writes 6, then immediately grants the map and writes 7.
This is mostly sound because 21 slots have just been freed. Map replacement at
states 7/8 lacks explicit ground/capacity behavior. Scouting at the correct
zone writes 8 and does not consume the map, as expected.

## 7. Completion and permanent unlocks

At primary state 70 and secret state 8, Denulth accepts the map and Combination
in either order. The two permanent handoff bits preserve partial delivery and
the dialogue reports whichever item remains. This is a strong existing design.

The final transaction is less safe. Once both bits are set, the dialogue queues
`death_quest_complete`, but the queue itself does not re-check state or a
completion guard. Its first action writes 80, then clears bits, adds steel claws,
grants Attack XP, and calls the shared completion UI. Duplicate network ops or
duplicate queued delivery can therefore award XP/claws twice; if both final
items were handed in together, two inventory slots are available for duplicate
claws. Move the idempotence check into the completion procedure and commit a
single transaction. The final handoff guarantees one free slot, but still test
capacity, relog, disconnect, repeated ops, and shared completion idempotence.

Post-quest climbing-boot wear is correctly denied before state 80 and allowed
afterward. Tenzing's 12-coin sale is correctly post-quest, but removes coins
before ensuring a destination slot; a full inventory with more than 12 coins
can lose money without boots. Make purchase atomic.

The Smithing table exposes bronze through rune claws to every player. No
Smithing script checks `%death_equiproom`. This grants a headline reward before
the quest. Gate claw visibility/selection or at least the production attempt on
state 80 while preserving each metal's level and two-bar recipe. Test every
metal and both UI and direct-operation paths.

Denulth's post-quest “I'll get Godric back” branch only displays “Troll
Stronghold is not available yet.” The `quest_troll` root has states and several
later interactions but no Denulth start transition. Treat this as a downstream
partial-root blocker: when Troll Stronghold is modernized, its start branch must
merge into Denulth's sole owner and require Death Plateau 80. Troll Romance
already checks Troll Stronghold complete at Ug, so its transitive gate is sound.

## 8. Dialogue, journal, combat, and shared actors

The dialogue is unusually complete and should be modernized by preservation,
not condensation. Retain Denulth's refusal, White Knight/history options,
Eohric's baseline castle conversation, Saba's refusal/re-talk, Tenzing's “No
milk today” door behavior, all missing-item lists, Harold's drunk/normal menus,
either-order final handoff, and post-quest Burthorpe reactions.

The journal uses the dynamic dbrow route and covers both branches. Improve these
specific defects:

- state 10 does not explicitly direct the player to Eohric;
- state 30 records Harold's refusal but does not clearly say to return to Eohric;
- missing IOU/Combination/map/certificate recovery is not surfaced;
- inventory-only spiked-boots presence can state that Dunstan received the
  certificate even when `%death_map` is still 2–4 and the pair was pre-owned;
- completed text has several missing separators; and
- puzzle progress cannot currently distinguish visible placements from stale
  `%death_stones` bits.

Archers and troll throwers are live rather than quest soft-skips. The quest-local
archer implementation drops its legacy medium-clue branch and ambient soldier
timers. Its ranged accuracy uses `npc_param(damagetype)` while the overlay sets
that parameter to crush; compare with the shared ranged controller and verify
the intended ranged defence style, projectile fields, poison, hunt initiation,
and contemporary drop table. These are world-combat correctness issues, not a
reason to block the narrative route if safely isolated.

Dunstan is shared with Troll Stronghold, Troll Romance, and other item-repair
roles. Its `[opnpcu,death_smithy]` catches every use-on item and returns a generic
failure while comments admit law-talisman recovery and item repair are deferred.
Build one combined owner and preserve all concurrently available menu/use-on
arms; do not let Death Plateau's fallback mask another quest or service.

## 9. Debug and recovery contract

There is no quest-specific reset or deterministic production walk. The generic
`::complete` arm writes only `%death_equiproom=80`; it leaves secret-route,
gamble, handoff, and puzzle fields untouched and does not define boot/claw
unlock test state beyond the primary predicate.

Add a hermetic reset that clears all four carriers, removes only quest-owned
certificate/map/IOU/Combination/quest boots and puzzle objects under a stated
storage policy, resets Harold's bankroll to the accepted-start value, and does
not delete tradeable player-owned boots or ordinary supplies. Add a production
walk that invokes real transitions and asserts both branches rather than
writing states. Define `::complete` as an idempotent prerequisite adapter and
test it twice without organic XP/item rewards.

## 10. Defect ledger

| Severity | Defect | Player impact | Owning work |
| --- | --- | --- | --- |
| Critical | Generated world supplies five green balls and no other colours | Equipment puzzle cannot be solved organically | Spawn source/generator plus quest verification |
| Critical | Permanent occupied bits never clear on pickup/expiry/wrong/early placement | One action can permanently hardlock a character | Puzzle object/state controller |
| Critical | Claw smithing has no completion gate | Major reward is globally unlocked | Shared Smithing product policy |
| High | Completion queue has no internal idempotence guard | Duplicate ops may duplicate 3,000 XP and claws | Completion transaction |
| High | Initial boots, replacement items, certificate, map, and postquest purchase lack explicit capacity/fallback transactions | Lost quest item or coins; delayed recovery | Actor item-grant helpers |
| High | Troll Stronghold start is a dead postquest message | Direct successor cannot begin | Denulth/`quest_troll` shared owner |
| Medium | Dice interface, sequences, stored rolls, cancellation, and jingles omitted | Central minigame presentation/recovery differs | Harold controller and modern panel |
| Medium | Danger-sign camera/troll cinematic is a message | Route warning scene missing | Burthorpe cinematic owner |
| Medium | Journal has incorrect pre-owned-boots inference and weak next steps | Misleading recovery guidance | Journal procedure |
| Medium | Dunstan use-on fallback masks deferred shared services | Other quest/service interactions can be hidden | Combined Dunstan dispatcher |
| Medium | Archer combat/drop/hunt policy is partly legacy/deferred | World combat may use wrong defence/drop behavior | Shared ranged/drop owners |
| Low | Ambient soldier timers and gamble sound effects are absent | Cosmetic loss | Burthorpe ambient/audio package |
| Low | Config comment identifies `death_equiproom` as varp 58 instead of 314 | Conflates quest metadata and varp namespaces | Config documentation cleanup |

## 11. Modernization work packages

Implement in this dependency order:

1. **Identity and harness:** preserve native state 0–80, document authored side
   carriers, add reset/production-walk/static ownership checks, and pin refs.
2. **Puzzle recovery:** correct the authoritative five-colour spawn data and
   replace `%death_stones` with actual or player-owned object truth supporting
   pickup, early placement, expiry, relog, and concurrent players.
3. **Harold:** retain the economy/dialogue while restoring modern dice
   presentation, animations, cancellation settlement, jingles, and atomic
   IOU/Combination recovery.
4. **Secret route:** make boots/certificate/map grants safe, preserve the
   21-item transaction, restore the warning cinematic, and test all doors,
   stile, scout zone, and loss paths.
5. **Completion/unlocks:** guard completion atomically, make Tenzing's sale
   atomic, gate all claw recipes, and merge Troll Stronghold's start into
   Denulth when that partial root is completed.
6. **Narrative/world polish:** correct journal inference, reconcile Dunstan's
   shared services, and verify archers, troll throwers, drops, ambient actors,
   postquest dialogue, and audio.

No package should introduce quest-specific C routing, raw IDs, or a parallel
primary state. Shared fixes land in the owning actor/skill/quest system.

## 12. Gate D verification matrix

| Area | Required evidence |
| --- | --- |
| Static identity | Metadata ID 58, dbrow index 23, varp 314, states 0–80, side carriers, one trigger owner, all actors/locs/spawns resolve |
| Build | `make -C src mock230-scripts` and `mock230_pack --check-only` against intended cache |
| Quest Helper | `python3 tools/questhelper_extract.py deathplateau --check` exits 0 |
| Start/dialogue | No requirements; accept/refuse/re-talk; both branch orders; baseline and postquest menus |
| Gamble | 0/over-held/>1000; normal win/loss/tie; 60/101/1000 bankroll paths; Blurberry; cancellation/logout; IOU exactly once |
| Puzzle | One of each spawn; every wrong layout; pickup/replacement; placement before/after state 60; expiry/relog; two players; unlock once |
| Items | Full/zero slots; bank/ground/death loss; IOU/Combination/certificate/boots/map replacement; exact 10+10+boots consumption |
| Traversal | Every door direction, cave, stile, main-path warning, secret path/scout boundary, climbing-rock boot gate |
| Completion | Map/Combination either order; missing item; duplicate op/queue; exact 1 QP and 3,000 XP; one claw; reconnect boundaries |
| Unlocks | All claw metals denied before 80/allowed after; boot wear/sale and rocks; Troll Stronghold denied before/available after |
| Shared/world | Dunstan quest-state cross-products; Tenzing Troll Romance bridge; archer/thrower combat and drops; citizen reactions |
| Debug/client | Hermetic reset, production walk, `::complete` twice, real-client start-through-postquest capture including gamble/puzzle UI |

## 13. Exit criteria

Do not mark Death Plateau `verified-modern` until the five-colour puzzle is
organically solvable and fully recoverable, both parallel branches survive
loss/full inventory/relog, completion and rewards are exactly-once, claws and
boots are correctly gated, Troll Stronghold can consume the completion
contract, and Gate D evidence is attached.

This audit intentionally makes no gameplay changes.
