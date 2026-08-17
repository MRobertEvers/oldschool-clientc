# Demon Slayer modernization audit

Status: `audit-pending` — the quest has a dynamic journal, shared completion
adapter, working key dialogue fragments, a static Delrith combat proof, and
many native cache assets. It is not presently startable or completable from
the live world: neither Aris nor Wizard Traiborn is spawned. More importantly,
the implementation treats the whole `demonstart` carrier as a 0–30 quest
counter, while revision 239 defines a 0–3 primary varbit and stores the
incantation, key/case presentation, summoning cutscene, and wizard kills in the
remaining bits. Every current quest-state write can therefore erase native
side state. The drain key is a shared ground object, the finale is not
instanced, key destruction is really a public drop, and the Museum reward is
absent.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to discovery, native persistence, the Aris and Wally
scenes, all three key routes, Silverlight recovery, the summoning instance,
Delrith combat and banishment, completion, Museum Kudos, shared-NPC branches,
downstream quests, journal, migration, and debug tooling. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The article and quick guide define the current route, combat, recovery,
rewards, and unlocks. The transcript defines dialogue, re-talks, optional
branches, cutscenes, and hand-offs. Revisions were resolved through the OSRS
Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Demon Slayer](https://oldschool.runescape.wiki/w/Demon_Slayer?oldid=15291214) | 15291214, 2026-08-09 | Identity, requirements, walkthrough, combat, rewards, and unlocks |
| [Demon Slayer/Quick guide](https://oldschool.runescape.wiki/w/Demon_Slayer/Quick_guide?oldid=15109448) | 15109448, 2026-01-19 | Exact critical path, items, key recovery, and finale |
| [Transcript:Demon Slayer](https://oldschool.runescape.wiki/w/Transcript%3ADemon_Slayer?oldid=15263169) | 15263169, 2026-07-14 | Offers, re-talks, Wally scene, key branches, chant, and completion |
| [Aris](https://oldschool.runescape.wiki/w/Aris?oldid=15271705) | 15271705, 2026-07-22 | Start, incantation, shared RFD dialogue, and post-quest dialogue |
| [Sir Prysin](https://oldschool.runescape.wiki/w/Sir_Prysin?oldid=15278795) | 15278795, 2026-07-28 | Key hunt, Silverlight case, and replacement service |
| [Captain Rovin](https://oldschool.runescape.wiki/w/Captain_Rovin?oldid=15278799) | 15278799, 2026-07-28 | Key dialogue, possession checks, and shared quest ownership |
| [Wizard Traiborn](https://oldschool.runescape.wiki/w/Wizard_Traiborn?oldid=15002750) | 15002750, 2025-10-12 | Bone ritual, key recovery, and shared dialogue ownership |
| [Delrith](https://oldschool.runescape.wiki/w/Delrith?oldid=15216579) | 15216579, 2026-05-24 | Summoning, damage, weakening, recoil, chant, and banishment |
| [Silverlight](https://oldschool.runescape.wiki/w/Silverlight?oldid=15286297) | 15286297, 2026-08-03 | Demon damage, transformations, and replacement policy |
| [Key (Captain Rovin)](https://oldschool.runescape.wiki/w/Key_%28Captain_Rovin%29?oldid=15186927) | 15186927, 2026-04-22 | Duplicate prevention and destroy/recovery behavior |
| [Key (Wizard Traiborn)](https://oldschool.runescape.wiki/w/Key_%28Wizard_Traiborn%29?oldid=15186929) | 15186929, 2026-04-22 | Destroy/recovery behavior and repeated bone cost |
| [Key (Sir Prysin)](https://oldschool.runescape.wiki/w/Key_%28Sir_Prysin%29?oldid=15187029) | 15187029, 2026-04-22 | Drain/sewer acquisition and recovery behavior |
| [Drain](https://oldschool.runescape.wiki/w/Drain?oldid=15202528) | 15202528, 2026-04-29 | Water interaction and native drain presentation |
| [Sewer pipe](https://oldschool.runescape.wiki/w/Sewer_pipe?oldid=14839782) | 14839782, 2025-01-20 | Rusty-key retrieval surface |
| [Wally](https://oldschool.runescape.wiki/w/Wally?oldid=15094778) | 15094778, 2025-12-26 | Historical banishment scene |
| [Delrith (music track)](https://oldschool.runescape.wiki/w/Delrith_%28music_track%29?oldid=15264933) | 15264933, 2026-07-16 | Summoning-scene music |
| [Wally the Hero](https://oldschool.runescape.wiki/w/Wally_the_Hero?oldid=15292574) | 15292574, 2026-08-11 | Wally-scene music |
| [Dark wizard](https://oldschool.runescape.wiki/w/Dark_wizard?oldid=15289844) | 15289844, 2026-08-07 | Quest encounter variants and level/roster evidence |
| [Stone table](https://oldschool.runescape.wiki/w/Stone_table_%28Delrith%29?oldid=15201851) | 15201851, 2026-04-29 | Summoning and post-quest transform behavior |
| [Historian Minas](https://oldschool.runescape.wiki/w/Historian_Minas?oldid=15006333) | 15006333, 2025-10-16 | Separately claimed 5-Kudos reward |
| [Shadow of the Storm](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm?oldid=15292331) | 15292331, 2026-08-10 | Requirement and Silverlight/Darklight continuity |
| [Recipe for Disaster/Freeing the Lumbridge Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Lumbridge_Guide?oldid=15297799) | 15297799, 2026-08-13 | Requirement and Aris/Traiborn shared dialogue |
| [Defender of Varrock](https://oldschool.runescape.wiki/w/Defender_of_Varrock?oldid=15266905) | 15266905, 2026-07-18 | Downstream requirement and shared palace NPCs |

No current Wiki journal-transcript page exists, so journal wording must be
validated against a real client rather than reconstructed from an absent page.

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/demonslayer)
maps the native states, 27 route points, nine items, six NPCs, twelve locs, and
eight relevant varbits. `python3 tools/questhelper_extract.py demonslayer
--check` exits 0. It is a routing oracle, not proof that server triggers,
transactions, actors, combat, instances, or rewards work.

## 2. Canonical contract

Demon Slayer is a free-to-play, novice, short quest released 4 January 2001.
It starts by paying Aris one coin in her tent in Varrock Square. It has no
quest or skill prerequisite; combat level 15 is recommended. The required
route items are one coin, a bucket of water, 25 unnoted bones, and the
quest-provided Silverlight. The player must be able to defeat Delrith, level
27, while quest dark wizards occupy the stone circle.

A canonical run must:

1. find Aris in her tent, pay exactly one coin, receive the prophecy and Wally
   flashback, and retain a randomized five-word incantation;
2. ask Sir Prysin for Silverlight and learn that Captain Rovin, Wizard
   Traiborn, and the palace drain hold its three keys;
3. obtain one Rovin key through the correct dialogue, with inventory and bank
   ownership preventing duplicates;
4. give Traiborn 25 unnoted bones incrementally, play his wardrobe ritual, and
   receive his key; losing it requires another 25 bones;
5. pour a bucket of water into the palace drain, see the drain/sewer state
   change, and retrieve the rusty key from the sewer pipe;
6. exchange the three keys with Prysin, play the Silverlight-case sequence,
   and receive the sword transactionally;
7. enter a per-player stone-circle instance, watch the summoning scene, fight
   through or around the quest dark wizards, weaken Delrith while wielding
   Silverlight, and chant the player's exact incantation; and
8. complete exactly once, restore the ordinary stone circle, retain
   Silverlight, and expose the separate Museum claim and downstream quest
   requirements.

Completion awards 3 quest points and Silverlight; no XP is awarded. Historian
Minas separately awards 5 Kudos after completion. Leaving to restock and
re-entering the final encounter must be safe. A wrong chant restores Delrith
for another attempt; the correct chant must remain finishable if the weakened
actor disappears during the dialogue sequence, as the current game permits.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 2 / 25 |
| Dbrow | `quest_demonslayer` |
| Type / difficulty / length | Free-to-play; novice; short |
| Release | 4 January 2001 |
| Start | `aris` (NPC 11868), (3204, 3424, plane 0) |
| Prerequisites | None |
| Combat recommendation | 15 |
| Primary field | `%demonslayer_main`, `demonstart` bits 0–4 |
| Native end state | 3 |
| Reward | 3 QP; Silverlight |

### 3.1 Native primary ladder

| `%demonslayer_main` | Canonical checkpoint | Current implementation |
| ---: | --- | --- |
| 0 | Not started; talk to Aris | Aris is not spawned, so the offer is unreachable |
| 1 | Prophecy complete; talk to Sir Prysin | Stored as old whole-carrier state 1 |
| 2 | Collect keys, receive Silverlight, and confront Delrith | Expanded into old states 2–29, overwriting side fields |
| 3 | Complete | Current value 3 means Traiborn has just requested bones; current completion is 30 |

The dbrow and Quest Helper agree on the compact 0–3 contract. The local
constant file instead declares the carrier itself as a writable permanent
varp and gives one primary value to every donated bone. Because native state 3
means complete but local state 3 means zero bones delivered, code must not
switch to native constants without a deliberate save conversion.

### 3.2 Native side fields in `demonstart`

| Field | Bits | Native meaning | Current result |
| --- | ---: | --- | --- |
| `%demonslayer_main` | 0–4 | Primary 0–3 quest state | Whole 0–30 values are written instead |
| `%delrith_incantation_1..5` | 5–19 | Five zero-based word IDs, three bits each | Five unrelated permanent vars store one-based IDs |
| `%delrith_silverlight_case` | 20 | Closed/open case presentation | Never written |
| `%delrith_drain_key` | 21–22 | Drain, sewer-key, and retrieval presentation | Never written |
| `%delrith_seen_summoning_cutscene` | 23 | Final-scene continuation | Never written |
| `%delrith_dark_wizards_killed` | 24–26 | Per-player encounter attrition | Never written |
| Unused in revision 239 | 27–31 | Reserved | Must remain untouched |

The cache transforms `questdrain`, `qip_ds_sewer_key`,
`qip_ds_silverlight_case`, and `qip_ds_stone_table` from these side fields.
The local scripts never directly write those fields. Static loc/var auditing
instead finds 29 whole-carrier write sites, each capable of clearing the other
bits. Modernization must use named native varbits exclusively after migration
and remove `wholewrite=allow`.

### 3.3 One-time save migration

Run migration before any modern handler interprets the carrier. Snapshot the
old whole value first; do not write a native field until all old values and
authored incantation vars have been read.

| Old whole `demonstart` | Native result | Additional preservation |
| ---: | --- | --- |
| 0 | main 0 | No quest progress |
| 1 | main 1 | Preserve/generated incantation |
| 2 | main 2 | Key hunt started |
| 3–28 | main 2 | Preserve `old - 3` donated bones, capped at 25 |
| 29 | main 2 | Set Silverlight case/open state consistently |
| 30 | main 3 | Completed |

Map authored incantation values 1–5 to native values 0–4, preserving their
order and validating that they form a permutation. If values are missing or
invalid, regenerate once without changing a valid existing chant. After a
real-client/server trace confirms no hidden bone counter, introduce one
dedicated permanent donated-bones field; do not encode the count into the
primary quest state. Normalize drain, case, summoning, and wizard-kill fields
from durable evidence where possible, but never invent consumed keys or grant
Museum rewards during migration.

Migration must be idempotent and version-marked. Test every old value 0–30,
all partially generated chant combinations, held/banked keys, held/worn/banked
Silverlight, and repeated login. Then update Shadow of the Storm, its debug
scripts, journal conditions, cheats, and every shared-NPC comparison before
disabling whole-carrier writes.

## 4. Implementation and ownership surface

The quest root has three config files and three scripts, 435 lines total. Its
headline route is split across older shared area scripts, bringing the main
audited source surface to roughly 1,394 lines before generic object, journal,
combat, museum, and downstream owners are counted.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `quest_demon/configs/quest_demon.constant` | Old 0–30 aliases, coordinates, and quest constants | Conflicts with native 0–3 state model |
| `quest_demon/configs/quest_demon.varp` | Whole carrier and five authored chant vars | Erases native side fields and duplicates native chant storage |
| `quest_demon/configs/quest_demon.npc` | Delrith definitions/drop contract | Useful null-drop setup; encounter lifecycle remains incomplete |
| `quest_demon/scripts/demon_slayer.rs2` | Completion and drain interaction | Global drain-key object and no native transforms |
| `quest_demon/scripts/delrith.rs2` | Chant generation, zone summon, damage, weakening, banishment | Static proof exists; shared world and continuity defects remain |
| `quest_demon/scripts/demon_journal.rs2` | Dynamic journal | Correct adapter, obsolete state ladder and incomplete guidance |
| `areas/varrock/scripts/aris.rs2` | Start and incantation dialogue | Older LostCity/RSC-shaped dialogue; unreachable NPC; no Wally scene |
| `areas/varrock/scripts/sir_prysin.rs2` | Key explanation, exchange, recovery | Inventory-only checks and unsafe paid replacement |
| `areas/varrock/scripts/captain_rovin.rs2` | Rovin key and shared DOV branch | Duplicate key grants and missing capacity/ownership contract |
| `areas/draynor/scripts/traiborn.rs2` | Bones/key ritual plus RFD/TOTE branches | Unreachable NPC; old per-bone primary states; scene absent |
| `npc/scripts/dark_wizard.rs2` and generated combat/drop scripts | Ordinary dark-wizard behavior | Not a player-owned final encounter |
| quest journal dispatcher / quest cheat | UI registration and debugging | Journal registration is modern; cheat writes obsolete completion value |
| world spawn files | Native NPC discovery | Prysin/Rovin exist; Aris/Traiborn were dropped during ID/name migration |
| Varrock Museum | Historian Minas claim | `%vm_silverlight` exists, but no dialogue owner implements it |
| Shadow of the Storm / RFD / Defender of Varrock / The Golem | Requirements, shared NPCs, Silverlight consumers | Require additive routing and migrated completion checks |

The cache already supplies Aris, Prysin and his Silverlight form, Rovin,
Traiborn, Delrith and weakened Delrith, Denath, Wally, four quest dark-wizard
variants, all three keys, Silverlight, the case, drain, sewer key, wardrobe,
pipe, stone table, scene actors, animations, sounds, and music rows. Prefer
these native assets and transforms. Hand-spawned generic substitutes should
not survive merely because their dialogue or combat currently compiles.

## 5. Discovery and shared NPC routing

Aris NPC 11868 has no `.spawn` entry and no `npc_add`, making state 0 a hard
blocker. Traiborn NPC 5081 is also absent, making one key unobtainable even if
Aris is injected. The spawn migration report explains both omissions as
identity drift: the old Aris numeric ID now resolves to another cache NPC, and
Traiborn's old display name no longer matches `wizardtraiborn`. Restore both
by symbolic revision-239 identity at their canonical coordinates, then add a
spawn/discovery test that starts from the normal world rather than a developer
teleport or injected actor.

Prysin and Rovin are present in the Varrock Palace world spawn. Their Demon
Slayer branches must coexist with Defender of Varrock. Traiborn's Talk-to
owner currently prioritizes Recipe for Disaster and Temple of the Eye before
Demon Slayer. Aris also needs her Recipe for Disaster topic. Replace implicit
branch shadowing with an explicit top-level topic router, preserving ordinary
dialogue and every quest's pre-, active-, complete-, and recovery states.

## 6. Aris, acceptance, and the Wally scene

The current start takes one coin and generates a valid one-based permutation,
but follows the older LostCity/RSC-shaped conversation and message-only
history. It has no native Wally, Denath, dark-wizard, camera, animation,
stone-table, sound, or `Wally the Hero` music sequence. Implement the current
transcript using the cache-authored actors and scene assets. Keep refusal,
insufficient-coin, interruption, re-talk, incantation reminder, Silverlight
direction, final-fight direction, completed, and RFD topic branches.

Acceptance is a transaction boundary. Do not remove the coin or write main 1
until the player has selected the paid reading and the route can safely
continue. Establish the native randomized chant exactly once and retain it
across cancellation, logout, death, and reconnect. The Wally flashback must
be per-player and cleanup-safe; another player's conversation must never
move, reveal, or remove its actors. If interrupted after payment, re-talking
must resume without a second charge and without rerolling the incantation.

Use the current NPC name, Aris, in new journal and dialogue text. Historical
wording can remain only where the pinned transcript actually requires it.

## 7. Three-key route and transaction ledger

| Key route | Canonical acquisition/recovery | Current defect | Required transaction |
| --- | --- | --- | --- |
| Captain Rovin | Dialogue grants one; repeat after genuine loss | Checks neither inventory nor bank and can grant unlimited duplicates | Test inventory, bank, and relevant pending transfer before capacity; grant once; commit dialogue state only after add succeeds |
| Wizard Traiborn | Deliver 25 unnoted bones incrementally; loss requires 25 more | NPC absent; count encoded as main states 3–28; ritual is messages | Store count separately; delete/add atomically per bone; play wardrobe ritual; reset count only for canonical lost-key recovery |
| Sir Prysin's drain | Pour water, retrieve key at sewer pipe; repeat after loss | Available even before key hunt; creates a global timed ground object | Gate at main 2; write native drain stages; provide a player-owned/key-loc pickup; make recovery deterministic |
| Silverlight exchange | Prysin takes all three keys, opens case, gives sword | Uses inventory-only progress and never writes case state | Verify all ownership/capacity conditions, consume exactly one of each, animate case, add sword, then commit presentation/state |

Banked keys count as owned for dialogue and duplicate prevention, but the
exchange itself must require the required items in inventory. A failed add,
full inventory, dialogue cancellation, logout, or duplicate event delivery
must never consume a key, bone, bucket of water, coins, or all three keys
without producing the corresponding durable result.

All key configs advertise `Destroy`, but no quest-specific option-5 handler
exists. The global fallback drops them to the ground. Implement per-key
destroy confirmation and canonical recovery instead of exposing quest keys as
ordinary owner/public drops. Audit death storage, bank, trade, drop, alchemy,
death, logout, and duplicate paths as well as the visible Destroy option.

## 8. Drain and sewer presentation

The current bucket handler consumes water and returns the empty bucket in the
same slot, which is a sound inventory shape. It does not require main 2,
however, so states 0 and 1 can flush the key. It never writes
`%delrith_drain_key`; instead it adds key 3 at a fixed sewer coordinate for
about 300 ticks. Any player may race for it, and repeated pours can create
more keys. The placed `qip_ds_sewer_key` has no operation owner.

Use the native three-stage field and verify exact values from the live client:
initially key visible/stuck in the drain, after water the drain is empty and
the rusty key is visible by the sewer pipe, and after pickup only mud/empty
presentation remains. Current cache transforms and Quest Helper imply values
0, 1, and 2 respectively, but that mapping remains a trace item before code.
Both the above-ground drain and underground key must be player-specific.
Destroying or genuinely losing the key should re-enable the documented pour
and retrieval route without duplicating a held or banked copy.

The ordinary palace sink, water filling, manhole, and sewer stairs are shared
world systems. Preserve their non-quest behavior and test the complete route
from an empty bucket as well as a pre-filled bucket.

## 9. Traiborn's bone ritual

Once spawned, Traiborn must offer the modern Demon Slayer topic alongside
Recipe for Disaster, Temple of the Eye, and ordinary dialogue. Accept unnoted
bones incrementally, report the exact remainder, and never consume noted or
unsupported substitutes. The present recursive loop consumes one bone per
iteration and creates inventory space before giving the key, but its count is
the obsolete primary state. Move that count to its own durable field only
after confirming no unexposed native/server counter exists.

At 25, use the native wardrobe loc, operation, animation, and sound sequence,
then grant Traiborn's key. Interruption must resume at the correct ritual or
grant checkpoint without re-consuming bones. If the key is destroyed or
truly lost, canonical recovery resets the delivered count and requires 25 new
bones; a banked key is not lost. The optional spinach-roll dialogue must check
capacity and must not affect quest progress.

## 10. Sir Prysin, Silverlight, and replacement service

Prysin's key explanation and three-owner structure are recognizable, but his
status checks are inventory-only and incorrectly call banked keys missing.
On exchange, verify exactly one of each key in inventory, then perform a
failure-safe consume/case/sword transition. Write the native case bit so both
normal and speedrun case placements transform for that player. Use the native
Prysin/Silverlight presentation and case movement/shine effects where the
current transcript and cache trace require them.

Before completion, a genuinely lost Silverlight is replaced free. After
Demon Slayer but before Shadow of the Storm, the normal price is 500 coins.
After Shadow of the Storm, members normally recover the upgraded Darklight
line under its current policy, while free-to-play Silverlight exceptions also
exist. Ownership checks must include inventory, worn, bank, and the relevant
Silverlight/Darklight/Arclight/Emberlight family and transformation state.

The present 500-coin path removes coins before ensuring the sword can be
added. A full inventory can therefore lose the payment. Make replacement a
single validated transaction: determine the correct item and price, verify
ownership and capacity, remove payment, add the item, and commit only after
success. Re-test The Golem's Silverlight-on-portal use and Shadow of the
Storm's dye/upgrade/recovery flow.

## 11. Stone-circle instance and dark wizards

The current world contains eleven persistent `qip_ds_young_dark_wizard*`
actors. Crossing either of two fixed zone strips at old state 29 while wearing
Silverlight prints a message and hand-spawns Delrith for 500 ticks. The
pre-spawn `npc_find` is global, so one player's owned Delrith can suppress
another player's encounter. There is no quest instance, Denath, summoning
scene, stone-table transform, music, player-specific wizard roster, or killed
wizard counter.

Build the native per-player encounter and lifecycle:

1. enter or re-enter only at native main 2 with Silverlight obtained;
2. construct the quest-specific stone circle, Denath, wizard roster, Wally or
   other scene actors, and correct table presentation;
3. play the summoning cutscene and `Delrith` music once, then set
   `%delrith_seen_summoning_cutscene`;
4. record quest-wizard deaths in `%delrith_dark_wizards_killed` so killed
   actors do not respawn on restock/re-entry;
5. preserve the remaining encounter across safe exits and reconstruct it from
   native state after reconnect; and
6. destroy all temporary actors, zones, queues, and ownership handles on
   completion while restoring the post-quest stone table.

The Wiki article currently has an internal roster discrepancy: its
requirements section describes three level-20 and two level-7 dark wizards,
while its final-fight section describes one level-20 and two level-7 wizards.
Do not guess. Resolve the exact revision-239 roster and respawn semantics from
cache placement plus a real-client trace, and record the result in the
implementation evidence.

Test two simultaneous players entering, leaving, killing different wizards,
logging out, dying, and returning. No actor discovery, damage, chant, music,
transform, or cleanup event may cross player ownership.

## 12. Delrith combat and banishment

The current combat slice has several useful properties: Delrith has a null
death drop, Silverlight doubles damage, the lethal transition uses the exact
NPC UID, a wrong chant heals/restores that actor, and owner checks prevent an
unqualified ordinary death. `tools/check_quest_combat_contract.py` passes its
145-unit static ledger and Delrith proving slice. Its manifest remains
`implementation-in-progress`, and the proof explicitly does not cover the
missing instance, ritual choreography, or live concurrency/death/relog smoke.

Current engagement nevertheless requires the player to initiate through the
melee Attack handler before the lethal queue accepts them. Canonical manual
spell casting and recoil-only finishing are valid while Silverlight is worn;
they can damage Delrith now but fail the temporary engagement predicate at
lethal damage. Replace operation-specific authorization with encounter-owner,
quest-state, and equipped-Silverlight validation at all attack and damage
entry points. Preserve the Delrith-specific doubled damage without changing
generic Silverlight's ordinary demon bonus.

On lethal eligible damage, transition to the native weakened form and allow
the owner to begin Banish. Every chant selection must compare the five native
zero-based word fields. Wrong order restores the same encounter for another
attempt. Correct order completes even if the weakened visual actor expires
during the conversation, provided the player's durable encounter token proves
the valid weakening. The current code instead reports failure when the UID is
gone. Temp `%demon_delrith_engaged` also disappears on logout and can strand a
weakened actor; replace it with instance/durable continuation state.

Verify melee, ranged where permitted, manually cast magic, recoil-only lethal
damage, weapon switching, Silverlight removed before lethal damage, another
player's damage, simultaneous lethal queues, wrong chants, cancellation at
each word, actor expiry, region exit, logout, death, and repeated correct
completion delivery.

## 13. Completion, Museum, and downstream quests

The completion queue writes old state 30 before calling the shared reward
scroll, so its ordering intent is sound but its field/value is not. It has no
explicit idempotence guard: duplicate queue delivery could increment quest
points and completed-quest count twice. Commit native main 3 exactly once,
then invoke the shared reward adapter once for 3 QP and the Silverlight reward
display. Silverlight is already held; do not grant another sword from the
scroll. No XP should be awarded.

Completion must also clean the instance, actors, queues, and temporary combat
state; normalize the stone table and other player-specific transforms; retain
the randomized chant where current post-quest dialogue needs it; and make key
items unobtainable without deleting unrelated ground or bank state
unsafely. Reconnect before, during, and after the completion queue must yield
one coherent state and exactly one reward.

Historian Minas is present in the Museum, and `%vm_silverlight` exists in the
native `vm_displays` carrier, but no Talk-to owner awards the quest's 5 Kudos.
Implement this as a separately claimed post-quest Museum dialogue, not an
automatic completion reward. Check completed main 3 and an unclaimed native
display bit, award exactly 5 Kudos, set the bit transactionally, and support
ordinary/re-talk/already-claimed dialogue.

Native completion is required by Recipe for Disaster's Freeing the Lumbridge
Guide, Shadow of the Storm, and Defender of Varrock. Shadow of the Storm
currently compares the old whole carrier with local `demon_complete = 30`, and
three of its debug scripts write 30. Migrate all to `%demonslayer_main >= 3`.
The RFD Lumbridge Guide implementation does not currently enforce Demon
Slayer and needs its requirement fixed. Defender of Varrock's dbrow correctly
contains packed requirement 25, but that quest currently omits all requirement
checks; its separate audit owns that correction. Preserve shared Prysin,
Rovin, Traiborn, and Aris routes for every combination of these quest states.

## 14. Journal, debug, and recovery adapters

The journal is dynamically registered and uses the modern shared journal
renderer, so no legacy IF migration is needed. Its content follows old values
0–30, calls Aris “Gypsy,” omits the coin, water, and bones from the initial
requirements, checks key inventory but not bank, and cannot report exact bones
remaining. It has no guidance for the native drain/case/cutscene states,
instance re-entry, lost-item recovery, or the separate 5-Kudos claim.

Rewrite it from native main and side fields. Distinguish held, banked, consumed,
recoverable, and not-yet-earned keys without exposing random internal state.
Show exact bone remainder after the player has begun that route, retain useful
incantation reminders, and provide a safe re-entry/recovery hint during the
final fight. Validate journal text against the live client because no current
Wiki journal transcript is available.

The generic quest cheat writes old state 30. Shadow of the Storm debug paths
do the same. Replace debug completion with an explicit adapter that initializes
valid native main/side state, item and case presentation, encounter cleanup,
and Museum eligibility without pretending the Kudos were already claimed.
Add an exhaustive reset that removes only Demon Slayer-owned actors/items and
fields. Debug and migration paths must exercise the same invariants as real
play and must never be accepted as completion evidence.

## 15. Modernization work order

1. Add characterization tests for old states 0–30, all five authored chant
   vars, native transforms, shared NPC routing, completion consumers, and
   current item ownership/recovery behavior.
2. Implement and prove the idempotent save migration; convert every reader and
   writer to native named varbits; remove whole-carrier writes.
3. Restore symbolic Aris and Traiborn world discovery and regression-test all
   shared Aris/Prysin/Rovin/Traiborn dialogue routers.
4. Modernize acceptance, the player-owned Wally scene, incantation storage,
   interruption continuation, and journal checkpoint 1.
5. Build atomic key, bone, drain/sewer, case, Silverlight, Destroy, death, and
   replacement transactions with inventory/bank/family ownership checks.
6. Implement the native player-owned stone-circle instance, scene, transforms,
   wizard persistence, re-entry, and cleanup after resolving the roster trace.
7. Integrate the existing combat proof into the instance and close manual
   magic, recoil-only, actor-expiry, death, logout, and concurrency gaps.
8. Make completion idempotent at native main 3, implement the separate Museum
   Kudos claim, and migrate RFD, Shadow of the Storm, Defender of Varrock, The
   Golem, cheats, and shared recovery consumers.
9. Rewrite journal/re-talk/recovery text from native state and execute the full
   verification matrix from ordinary world entry on at least two players.

Keep commits narrow enough that state migration, world discovery, each key
route, instance lifecycle, combat, completion, and downstream integrations can
be reviewed and reverted independently.

## 16. Verification matrix

| Area | Required evidence |
| --- | --- |
| Discovery | Fresh player sees and talks to symbolic Aris at (3204,3424,0); Traiborn, Prysin, Rovin, drain, sewer pipe, case, and stone circle resolve without injection |
| Migration | Table-driven old values 0–30; chant permutations; repeated login; no native side-bit loss; no remaining whole-carrier writes |
| Acceptance | Refusal, no coin, exact one-coin payment, every dialogue branch, interruption/re-talk, one chant, Wally scene, two-player isolation |
| Rovin key | Held/banked/absent/full-inventory/destroyed/death cases; never more than one owned copy |
| Traiborn key | 0–25 incremental unnoted bones, noted rejection, full inventory, ritual interruption, held/banked/lost key, required second set of 25 |
| Drain key | State gate, filled and empty bucket route, all native transforms, player-owned pickup, duplicate pour, destroy/recovery, two-player isolation |
| Silverlight | Three-key exchange, every missing/banked key combination, case sequence, full inventory, free and paid recovery, item-family ownership, no lost coins |
| Instance | Exact traced wizard roster, cutscene once, music, table, killed count, leave/re-enter, logout, death, two simultaneous players, cleanup |
| Delrith | Static checker plus live melee/manual-magic/recoil proofs, doubled damage, Silverlight lethal gate, wrong/correct chant, cancellation, expired actor, duplicate lethal queue |
| Completion | Native main 3, one 3-QP award, no XP, retained single Silverlight, no duplicate reward, post-quest transforms, reconnect at every queue boundary |
| Museum | Minas unavailable before completion, 5 Kudos once after completion, native bit set, already-claimed response, no automatic completion grant |
| Shared systems | Every Aris/Prysin/Rovin/Traiborn topic permutation; RFD, Shadow, Defender, The Golem; ordinary sink/manhole/stairs/dark-wizard behavior |
| Journal/debug | Every native checkpoint and recovery state; exact bone remainder; bank-aware key text; valid complete/reset adapters |
| Tooling | Quest Helper extraction, loc/var audit, combat contract, compile/lint, focused tests, full quest suite, two-player live smoke |

## 17. Gate verdict

| Gate | Verdict | Reason |
| --- | --- | --- |
| Gate A — discovery and state reachability | Fail | Aris and Traiborn are absent; old state 3 collides with native completion; native side fields are erased by whole writes |
| Gate B — resource and transaction safety | Fail | Rovin duplicates keys, drain exposes a shared timed object, Destroy drops quest keys, and paid Silverlight recovery can lose coins |
| Gate C — encounter and multiplayer safety | Fail | No instance/cutscene/wizard persistence; global actor discovery cross-blocks players; valid magic/recoil and actor-expiry continuations fail |
| Gate D — completion and integration | Fail | Wrong completion value, no idempotence proof, no Museum claim, stale downstream checks, incomplete journal/debug normalization |

Demon Slayer remains `audit-pending`. Do not mark it modernized until both
missing NPCs are reachable from the ordinary world, every write uses the
native state model after a proven migration, all key and Silverlight
transactions are recoverable, the final encounter is isolated and reconnect
safe, completion awards exactly once, the Museum claim works, and every shared
and downstream route passes its regression evidence.
