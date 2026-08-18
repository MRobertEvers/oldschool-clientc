# Haunted Mine modernization audit

Status: `audit-pending` — an ideal-path player can force the local route from
the Zealot to the completion scroll, but the result is not a valid Haunted
Mine implementation. Crafting is checked at quest start instead of at the
outcrop, the Zealot's key is optional and then wrongly consumed, the cart and
valve systems collapse their native outcomes, Treus Dayth is a shared generic
NPC without his encounter mechanics, and completion deletes the permanent
crystal-mine key while granting no salve shard. Postquest shard cutting, the
crystal shortcut, Tarn's Lair gating, the Morytania Diary task, Nightmare Zone
Treus, Salve-amulet creation, and its normal-combat undead multiplier are
absent or disconnected. Several inventory operations also report success when
delivery failed, and completion is not replay-safe.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A-D to native state, dialogue, requirements,
mine topology, environmental hazards, the cart puzzle, temporary light,
valve timing, encounter ownership, item transactions, completion, recovery,
journal/admin adapters, and downstream consumers. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, skill, puzzle, combat, recovery, reward, and
integration contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Haunted Mine](https://oldschool.runescape.wiki/w/Haunted_Mine?oldid=15292305) | 15292305, 2026-08-10 | Identity, requirements, route, hazards, boss, completion, rewards, and unlocks |
| [Haunted Mine/Quick guide](https://oldschool.runescape.wiki/w/Haunted_Mine/Quick_guide?oldid=14834641) | 14834641, 2025-01-11 | Ordered traversal, lever solution, valve race, supplies, boss sequence, and reward action |
| [Transcript:Haunted Mine](https://oldschool.runescape.wiki/w/Transcript%3AHaunted_Mine?oldid=15263301) | 15263301, 2026-07-14 | Dialogue topology, item consequences, valve states, ambush, key pickup, and completion text |
| [Zealot](https://oldschool.runescape.wiki/w/Zealot?oldid=15233649) | 15233649, 2026-06-14 | Start/re-talk behavior, key disclosure, and postquest handling |
| [Zealot's key](https://oldschool.runescape.wiki/w/Zealot%27s_key?oldid=15183508) | 15183508, 2026-04-22 | Pickpocket source, valve use, retention, and postquest return |
| [Glowing fungus](https://oldschool.runescape.wiki/w/Glowing_fungus?oldid=15183507) | 15183507, 2026-04-22 | Unique light behavior, drop/exit/teleport destruction, and dark-room routing |
| [Treus Dayth](https://oldschool.runescape.wiki/w/Treus_Dayth?oldid=15234627) | 15234627, 2026-06-17 | Stats, visibility/movement, attacks, environmental hazards, reset, and NMZ unlock |
| [Crystal-mine key](https://oldschool.runescape.wiki/w/Crystal-mine_key?oldid=15183506) | 15183506, 2026-04-22 | Permanent key ownership, doors, key ring, loss, and reacquisition |
| [Salve shard](https://oldschool.runescape.wiki/w/Salve_shard?oldid=15183505) | 15183505, 2026-04-22 | Completion product, repeat cutting, and postquest access |
| [Salve amulet](https://oldschool.runescape.wiki/w/Salve_amulet?oldid=15241628) | 15241628, 2026-06-28 | Stringing recipes, exact undead multipliers, exclusivity, upgrades, and imbues |
| [Abandoned Mine](https://oldschool.runescape.wiki/w/Abandoned_Mine?oldid=15284892) | 15284892, 2026-08-01 | Six-level topology, entrances, doors, hazards, ores, lift persistence, and shortcut |
| [Tarn's Lair](https://oldschool.runescape.wiki/w/Tarn%27s_Lair?oldid=15302490) | 15302490, 2026-08-16 | Permanent access and Salve amulet (e) integration |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-21 | Treus unlock and dream variant behavior |
| [Morytania Diary](https://oldschool.runescape.wiki/w/Morytania_Diary?oldid=15280663) | 15280663, 2026-07-29 | Hard task for mining mithril in the Abandoned Mine |

These sources define a members, experienced, short quest released 21 December
2004. Priest in Peril is the prerequisite. Crafting 35 is boostable and
explicitly **not** required to start; it is checked when cutting the crystal.
The player must defeat Treus Dayth (level 95). A chisel is the only mandatory
item and is obtainable inside. Two empty slots, combat equipment, food, Prayer
40, and run energy are recommendations rather than start gates. Rewards are
two quest points, 22,000 Strength XP, a salve shard, the ability to make Salve
amulets, the postquest crystal shortcut, and access to Tarn's Lair. The
crystal-mine key remains owned after completion.

Transition aid only: Quest Helper's
[`HauntedMine.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/hauntedmine/HauntedMine.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (the file last
changed in `eae6616f20fea5b197cfe7daf3a3d4942590b23e` on 2026-04-09)
confirms all route zones and objects, the eight lever targets, both lift
fields, state 9 as Treus defeated, the item sequence, reward amounts, and
permanent unlock labels. Running
`python3 tools/questhelper_extract.py hauntedmine --check` at that commit
resolves every referenced item, NPC, object, varp, varbit, and the quest row.
Quest Helper intentionally maps primary values 1-10 to one conditional route;
it cannot prove exact server writes for values 1-8, placement-to-placement
travel, timed mechanics, actor ownership, item transactions, recovery,
completion atomicity, or downstream runtime consumers.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_hauntedmine`; quest metadata ID 68 |
| Type / difficulty / length | Members quest / experienced / short |
| Release / location | 21 December 2004 / Morytania |
| Start | `saradominist_zealot` at the Abandoned Mine |
| Primary state | `%hauntedmine`, permanent/transmitted native varp |
| Proven values | 0 not started; 9 Treus defeated; 10 pre-completion route; 11 complete |
| Unresolved values | 1-8 are all in-progress but their exact native write meanings are not recoverable from Quest Helper |
| End / quest points | State 11 / 2 QP |
| Requirement policy | Priest in Peril complete; Crafting 35 boostable at crystal cutting, not acceptance |
| XP | `stat_xp_awarded=(Strength,220000)` in tenths, exactly 22,000 XP |
| Quest product | One salve shard on the completion cut; repeat shards thereafter |
| Permanent key | Crystal-mine key retained, replaceable, door/shortcut/key-ring owner |
| Direct consumers | Salve crafting/effect/upgrades, crystal shortcut and mine doors, Tarn's Lair, Treus NMZ, Morytania Diary, POH Morytania painting |

The dbrow correctly records end state, quest points, Crafting 35, boostability,
`requirement_check_skills_on_start=0`, and Strength XP. Its
`requirement_quests=111` resolves locally to `quest_swansong`, contradicting
both current sources and the runtime. The current explicit Priest in Peril
check is safer than enforcing that corrupt row, but modernization must repair
the metadata and retain a tested runtime guard until all generic quest-start
consumers read the corrected source.

The native support carrier is richer than the port's constants describe:

| Native field | Bits | Intended responsibility / audit result |
| --- | ---: | --- |
| `%hauntedmine_pointspuzzlestarted` | 0 | Panel/puzzle lifecycle; written once, never read |
| `%hauntedmine_lever_b` | 1 | Point lever 1, target 1; used |
| `%hauntedmine_lever_a` | 2 | Point lever 2, target 1; used |
| `%hauntedmine_lever_c` | 3 | Point lever 3, target 0; used |
| `%hauntedmine_lever_d` | 4 | Point lever 4, target 0; used |
| `%hauntedmine_lever_e` | 5 | Point lever 5, target 1; used |
| `%hauntedmine_lever_i` | 6 | Point lever 6, target 1; used |
| `%hauntedmine_lever_j` | 7 | Point lever 7, target 0; used |
| `%hauntedmine_lever_k` | 8 | Point lever 8, target 0; used |
| `%hauntedmine_liftpoweredonce` | 9 | Valve unlocked historically; used as permanent lift gate |
| `%hauntedmine_liftpowerednow` | 10 | Current timed water flow; set to 1 and never cleared or consulted by the lift |
| `%hauntedmine_begincart_fungus` | 11 | Fungus at origin cart; used |
| `%hauntedmine_endcart_fungus` | 12 | Fungus at destination cart; set but never cleared on collection |
| `%hauntedmine_animate_start` | 13 | Valve/ghost sequence; entirely omitted |
| `%hauntedmine_animate_success` | 14 | Valve/ghost success state; entirely omitted |
| `%hauntedmine_animate_38_42` | 15 | Native timed animation/path checkpoint; omitted |
| `%hauntedmine_animate_38_44` | 16 | Native timed animation/path checkpoint; omitted |
| `%hauntedmine_animate_37_47` | 17 | Native timed animation/path checkpoint; omitted |
| `%hauntedmine_animate_39_50` | 18 | Native timed animation/path checkpoint; omitted |
| `%hauntedmine_animate_35_59` | 19 | Native timed animation/path checkpoint; omitted |
| `%hauntedmine_animate_returned` | 20 | Ghost returned/valve closed; omitted |
| `%hauntedmine_heardaboutkey` | 21 | Zealot conversation/pickpocket entitlement; used in collapsed form |

The constants claim a “full native varbit group” while omitting bits 13-20.
Those fields are direct evidence that the valve race was designed as a stateful
sequence, not an unknowable flavour event that should be made permanent.

### Required state capture and migration

Do not continue treating Quest Helper panel keys as permission to invent the
primary state machine. Capture `%hauntedmine` plus every support bit after each
current-reference action: Zealot topics and acceptance, first ingress, key
theft, cart deposit/outcomes/retrieval, first valve unlock, ghost close/retry,
first lift descent, ambush, leave/re-entry/reset, boss death, key collection,
door unlock, crystal cut, and postquest return. Include interruption and death
at every yielded sequence.

Existing local saves need a versioned reconciliation because the port has
written only 1, 9, 10, and 11:

| Existing shape | Risk | Migration rule |
| --- | --- | --- |
| State 1 plus arbitrary support bits | Locally valid but may not match native state 1 semantics | Preserve route ownership; infer no finer native value until captures exist |
| States 2-8 imported from native/current data | Local generic dialogue and journal flatten their meaning | Preserve values and route access; add captured exact presentation without rewriting them |
| State 9 without an owned key/encounter record | May be valid boss kill or cross-player/shared-NPC history | Reconcile against key ownership and encounter completion; never respawn/reward blindly |
| State 10 without crystal key | Can result from the current false-success add | Permit canonical key reacquisition; do not downgrade the kill |
| State 11 without key or shard | Expected for every locally completed player | Repair permanent key/shard ownership through an explicit one-time migration, without replaying XP/QP |
| State below 11 with 22,000 XP already awarded | Interrupted local completion | Use a settlement marker/history check; never award from state alone |

The repair must distinguish historic XP/QP settlement, permanent item
ownership, and postquest unlocks. State 11 alone is not proof that the current
completion proc successfully incremented points after it wrote the state.

## 3. Implementation surface

The direct quest root has 788 lines across one constants file and four scripts.
The implementation also relies on generic ladders, self-staging doors, world
spawns, inventory/drop/teleport/death systems, combat, equipment, Crafting,
Nightmare Zone, diaries, Tarn's Lair geometry, journal dispatch, and the quest
completion helper.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_hauntedmine.constant` | Local states, requirements/reward, lever targets, zones, arrival tiles | Good cache inventory and lever deduction; omits eight native fields, invents landings, and records required mechanics as deferrals |
| `hauntedmine_zealot.rs2` | Offer, prerequisite/skill gate, re-talks, pickpocket | Correct Priest check and explicit choices; wrong start-time Crafting gate, collapsed transcript, false-success/full-slot and bank-duplicate risks |
| `hauntedmine_dungeon.rs2` | Two back entrances, ladders, fungus, cart/levers, chisel, valve/lift | Broadly route-shaped; incomplete entrances/exits, invented teleports, missing hazards/outcomes, wrong recovery, no real interface/race, key optional/consumed |
| `hauntedmine_dayth.rs2` | Dark stairs, key ambush, Treus, key pickup, outcrop/completion | Shared generic encounter, player-position dispatch, no dark rooms, unsafe item state, wrong key lifecycle, no shard, no action skill check, unsafe settlement |
| `hauntedmine_journal.rs2` | Quest journal | Registered, but presents collapsed/inaccurate state and claims rewards never delivered |
| native varp/varbits/dbrow | Persistent client/server contracts | Present; dbrow prerequisite is corrupt, eight valve fields are orphaned |
| world locs/NPCs/items | Mine geometry, Zealot, key, carts, doors, hazards, shard/amulet assets | Assets are extensive; many interactive owners and state transforms are absent |
| shared ladders | Generic category fallback | Named quest handlers override some fallbacks, but several placement routes remain wrong or inert |
| shared self-stage doors | Opens registered doors unconditionally | Both crystal reward doors and the boss door have no quest-specific key/state owner |
| equipment/combat | Normal target multipliers | Equipment explicitly leaves undead multiplier at zero; only the POH dummy knows Salve tiers |
| Crafting/stringing | Salve shard plus wool / String Jewellery | No Salve recipe trigger found |
| Tarn's Lair | Maplinks/assets | No Haunted Mine completion gate or miniquest/amulet-enchant owner found |
| Nightmare Zone | Quest-boss dreams | Fixed two-wave Count Draynor/Elvarg stub; Treus variants exist only as assets |
| Morytania Diary | Hard task | Generic diary counters exist; no Abandoned Mine mithril event owner found |
| POH quest hall | Morytania painting | Correctly checks Haunted Mine along with the other Morytania quests |
| journal / quest cheat | Journal dispatch and state-only admin completion | Correct row routing; cheat writes only 11 and cannot reconcile items, bits, or settlement |
| automated tests | Route and failure coverage | No Haunted Mine tests found |

This is primarily an ownership and lifecycle problem, not a missing-cache
problem. The cache already supplies dedicated entrance/inside/shortcut locs,
the two keys, salve shard and amulet, Treus active/faded forms, environmental
NPCs, valve animation bits, doors, lifts, and puzzle objects. Modernization
should wire those assets through exact placement-aware, player-owned,
transaction-safe machinery instead of adding more broad zone teleports.

## 4. Primary route reachability

| Phase | Canonical transition | Current behavior / defect |
| --- | --- | --- |
| Start | Complete Priest in Peril, explore Zealot dialogue, accept regardless of current Crafting, learn of key | Priest is checked, but boosted Crafting 35 is also required at acceptance; most transcript branches collapse into three invented choices |
| Key | Ask about second entrance/key, pickpocket Zealot; full inventory drops key to the floor | Heard bit is set at acceptance; pickpocket reports success even when `inv_add` fails and has no private-ground fallback; bank ownership is ignored |
| South route | Use either valid ingress, traverse exact ladders and hazards | Only two back entrances are owned; front/secondary and several inside locs are inert; arrival tiles are invented and carts/ghosts/pickaxes lack quest hazard behavior |
| Fungus | Pick one; dropping, sunlight, teleport, or relevant departure destroys it | Inventory-only duplicate test; no capacity preflight; no drop/exit/teleport destruction lifecycle |
| Cart | Deposit fungus, configure points UI, observe one of four route outcomes, recover/retry | Eight toggles are correct, but the UI/cutscene and all wrong-route outcomes collapse to one message; destination bit never clears after collection |
| North route | Leave, enter north, descend, retrieve fungus, climb back, redescend to lift room | Local collect ladder teleports directly to the lift room, skipping the canonical level-3 return/redescent; the lift-room wrong-way ladder does not move the player |
| Valve | Use Zealot's key once, retain it, race ghost; retry by turning unlocked valve | First turn works without a key, consumes it if present, and permanently powers the lift; current-power and eight animation fields are ineffective |
| Lower mine | Descend lift with fungus; wrong light leads through dark recovery rooms | Lift checks historical power, not current power; stairs simply refuse entry without fungus, and `hauntedmine_dark_stairs_bottom` is unowned |
| Treus | Key ambush, private boss with teleport/invisibility and room hazards; resume/reset safely | Shared public 50-duration NPC with generic retaliation; faded form, cranes, carts, pickaxes, reset, safe door persistence, and owner isolation are absent |
| Crystal key | After the owner's kill, take and permanently retain the key; full inventory preserves pickup | Public base NPC plus state 9; add is not preflighted and state advances despite failure, although the persistent NPC permits a later retry |
| Crystal room | Use key on locked door, retain it, check boosted Crafting 35 at outcrop | Stair handler gates on inventory key and jumps to an invented room tile; generic doors are unconditional; no Crafting action check |
| Completion | Cut and receive salve shard; atomically settle 22,000 Strength XP, 2 QP, state 11, and unlocks | Deletes crystal key, grants no shard, awards XP, writes state, then awards QP/scroll without a settlement guard |
| Postquest | Cut unlimited shards, use shortcut, keep/recover key, make/use/upgrade amulet, access consumers | Outcrop explicitly refuses every postquest cut; shortcut locs are unowned; all listed consumers are absent or disconnected except the POH painting |

The earliest canonical contract failure is the start-time Crafting gate. The
earliest progression bypass is the first valve activation without the Zealot's
key. The route can still display a completion scroll on an ideal run, but that
run destroys both permanent quest products and leaves no way to make the
quest's signature reward.

## 5. Detailed lifecycle audit

### Acceptance, dialogue, and Zealot's key

Keep the explicit Priest in Peril prerequisite and accept/refuse behavior, but
remove Crafting from acceptance. Rebuild the transcript topology: allegiance,
what the Zealot is doing, the mine history, Treus, the legendary amulet, other
entrances, key questions, and the actual acceptance path must survive re-talks
and interruptions. Wrong allegiance is dialogue, not a permanent lock. Write
`heardaboutkey` only at the canonical disclosure rather than as a side effect
of a shortened quest start.

Pickpocketing is deterministic quest interaction, but it is still an item
transaction. Preflight inventory space; on the canonical full-inventory branch
create an owner-private ground key at the Zealot instead of narrating success.
Revalidate state and ownership after any yield. Define ownership across
inventory, bank, private ground, and the valve's historical unlock so banking
cannot create duplicates. The key must not be deleted on valve use. After
completion, talking to the Zealot may return it permanently; after that return
the player must not be able to steal another.

### Entrances, ladders, doors, and environmental hazards

Inventory every physical placement, not merely every loc name. The cache has
`hauntedmine_secondary_entrance`, both back entrances, their inside variants,
the main inside stairs, `lotr_back_entrance1_inside`, dedicated postquest
`hauntedmine_crystalshortcut_entrance/exit`, both dark-stair forms, and keyed
reward doors. Only three entrance/inside names currently have handlers.

Replace broad player-zone dispatch and invented `_enter` tiles with reviewed
placement-to-placement links keyed by the interacted loc coordinate and angle.
The dark-stairs decision currently reads `coordx(coord)`, so interacting across
the split can select a destination from the player's tile rather than the
object. Apply the same interacted-placement rule to every reused ladder/cart.
Verify collision, approach, facing, animation, and both directions for each
link.

The collect-room `hauntedmine_ladder_1w` must return to level 3 north; the
player then descends the separate ladder into the lift room. The lift-room
placement must return to level 3 north on recovery rather than print that the
player is back where they started. Own every inside exit and both shortcut
ends. The shortcut is available only after state 11 and leads between the
upper entrance and outcrops without a fungus.

Static mine carts, hidden/surprise ghosts, corpses, possessed pickaxes, cranes,
and track triggers are not scenery-only. Implement their current-reference
activation, damage, collision, drops, and post-boss safety using placement- or
encounter-owned controllers. Do not attach unconditional generic door behavior
to keyed reward doors: opening must validate the crystal key/key-ring state;
boss-room doors must preserve encounter HP and safe-zone behavior as specified.

### Glowing fungus and cart puzzle

Fungus is a temporary quest light with a lifecycle. Picking must preflight a
slot and prevent duplicate active ownership. Dropping it, crawling into
sunlight, teleporting out, and relevant random relocation convert it to ashes;
death/re-entry behavior must match the current reference. Ordinary light
sources are extinguished or ineffective in the flooded/lower route, while the
fungus remains valid.

The current lever-to-varbit mapping and target values are supported by Quest
Helper and can remain. The panel must own the native puzzle interface and use
`pointspuzzlestarted` as an actual lifecycle field. Starting the cart should
animate the selected route and resolve the canonical outcomes:

- sink: fungus is destroyed and the origin/destination ledger clears;
- return to origin: fungus remains in the starting cart;
- successful destination: origin clears and destination becomes collectible;
- empty-cart success: route animates but no fungus appears.

Wrong lever configurations are not all equivalent. Capture the route/outcome
mapping before implementing it. Collection must clear the destination bit
atomically with item delivery. The current permanent `endcart_fungus=1`
allows a player who later loses fungus to reclaim it repeatedly without
resending the cart, contrary to the death/recovery route.

Both deposit and collect need preflight -> revalidate -> consume/deliver ->
commit ordering. Journal logic should distinguish fungus at origin, cart in
motion/outcome, fungus at destination, fungus held, and lost fungus rather
than treating one sticky bit as the whole phase.

### Valve race, chisel, lift, and dark rooms

The first valve activation requires using the Zealot's key. It unlocks the
valve without consuming the key, writes historical `liftpoweredonce`, opens
current flow, and begins the mischievous ghost sequence. Subsequent attempts
after the ghost wins can use the valve directly. `liftpowerednow`, not the
historical bit, gates descent while the quest is active. After completion the
lift remains usable without repeating the race.

Use the eight native animation fields for a replay-safe controller: start,
ghost movement checkpoints, success/return, close, timeout, logout, teleport,
death, and reconnect. The exact ticks/path must be captured; until then keep
the fields and controller boundaries intact rather than substituting permanent
power. Two players must not close or open one another's logical valve race.

The chisel is an ordinary recoverable spawn, not a one-time narrated grant.
Current code reports finding one even with no free slot. Prefer the map spawn
or a capacity-safe owner-private fallback and allow repeat acquisition without
creating bank-driven duplicates that matter to the quest ledger.

When the player reaches lower stairs without glowing fungus, route them to the
correct dark Dayth/crystal maps and let `hauntedmine_dark_stairs_bottom`
return them. Apply darkness behavior and prevent unintended interactions
there. Do not turn the evidence gap about landing tiles into a permanent block.

### Treus Dayth encounter

Replace the shared public `npc_find`/`npc_add` encounter with a player-owned
encounter controller or private instance. The innocent-looking key, Treus,
his HP, active/faded form, hazard scheduler, door persistence, kill credit,
leave/re-entry, reset timeout, death, logout, and teardown must belong to the
same player. A second player must neither suppress spawn nor damage/kill the
first player's boss.

The encounter needs all current mechanics:

- the key floats away, returns, and starts the ambush dialogue/cutscene;
- Treus alternates attackable and invisible/faded movement around obstacles;
- disappearance frequency increases as the fight progresses;
- possessed carts trigger on tracks and deal their typeless repeated damage;
- adjacent cranes deal typeless damage;
- projectile pickaxes use the canonical ranged behavior/protection reduction;
- Treus retains his rare melee attack and current stats/attributes;
- the eastern door is a safe recovery boundary that preserves HP temporarily;
- an overlong/abandoned encounter resets to the innocent-looking key;
- death/teleport permits a fresh owner encounter after the fungus recovery.

On the owner's killing blow, silence that owner's machinery, atomically mark
the native defeated state, and expose the key to that player. Do not infer
credit from whichever player happens to be `npc_findhero` on a shared NPC.
The normal combat and Nightmare Zone variants may share a tested Treus combat
module, with the dream variant's documented reduced environment and its own
stats/config.

### Crystal key, outcrop, and completion settlement

Key pickup must preserve the innocent-looking NPC until delivery succeeds.
With a full inventory, show the canonical refusal/ground behavior and leave
state/entitlement retryable. Once obtained, the crystal-mine key is permanent:
it opens the reward room, other mine doors and shortcuts, can be added to the
steel key ring, and can be re-obtained from the lower room if lost. It is not
consumed by opening the reward door or cutting the crystal.

At the outcrop, check effective Crafting 35 at the action. A temporary boost is
valid; a boost that expired during the long route is not. Require a chisel and
the legal room/key route. Preflight one output slot (or confirm canonical
full-slot behavior), play the cutting action, add
`crystalshard_necklace_unstrung` (Salve shard), and only then commit quest
completion. The first successful cut settles exactly once:

1. deliver one salve shard;
2. award 22,000 Strength XP;
3. write state 11 and permanent unlock/settlement evidence;
4. award 2 quest points and increment the completed count;
5. show the completion scroll with the shard as its item model.

The current order is XP -> state -> points/scroll, with no transaction marker.
Interruption before state can duplicate XP; interruption after state can leave
QP/count/scroll missing forever. Implement a protected one-time settlement
record or replay-safe completion helper. Do not repair partial historic
settlement by blindly rerunning the rewards.

After completion, outcrops must cut unlimited replacement shards with a chisel
and no further XP/QP. The local `%hauntedmine >= 11` refusal must be removed.
Postquest cutting through the shortcut does not require a fungus.

## 6. Recovery and downstream consumers

| Item / condition | Required recovery | Current result |
| --- | --- | --- |
| Zealot's key | Retry pickpocket/full-slot ground pickup; retain after valve; optionally return postquest | False success with full inventory, bank ignored, consumed by valve, no postquest return |
| Glowing fungus | Resend cart after loss/death; destroy on drop/sunlight/teleport | Destination bit never clears, allowing repeated free pickup; destruction hooks absent |
| Chisel | Bring one or take another from mine spawn | Script retry is possible but reports success on full inventory |
| Crystal-mine key | Full-slot-safe pickup; permanent ownership; lower-room reacquisition; key ring | State can advance before delivery; completion deletes it; no key-ring/door recovery owner found |
| Salve shard | First cut awards one; postquest shortcut supports unlimited replacements | Never granted; outcrop permanently disabled at completion |
| Partial completion | Resume missing shard/XP/QP/count without duplicating settled parts | No settlement marker or repair path |

Permanent integration work is part of the quest, not flavour:

- Add salve shard + ball of wool stringing and String Jewellery support. The
  base amulet must apply its exact undead melee accuracy/damage factor and be
  mutually exclusive with Slayer helmet/black mask bonuses. General equipment
  currently documents its undead multiplier as zero; POH dummy-only logic is
  not a combat implementation.
- Preserve the upgrade seam for Tarn's diary, Salve amulet (e), Nightmare Zone
  or alternate imbues, and the distinct melee/ranged/magic multipliers. Do not
  duplicate target-classification logic across each item.
- Wire both dedicated crystal-shortcut locs and key-gated mine doors. Verify
  the secret ore entrance and postquest lift remain usable.
- Gate/access Tarn's Lair from Haunted Mine completion and ensure its
  miniquest, diary, amulet-enchant, bank, traps, and return route have explicit
  owners. Current assets/maplinks alone do not prove the unlock.
- Add Treus Dayth to Nightmare Zone only after state 11. The current minigame
  is a fixed two-boss stub despite normal/hard Treus cache actors existing.
- Fire the Hard Morytania Diary event when a qualified player mines mithril in
  the correct Abandoned Mine area. Generic diary counters do not implement the
  task.
- Keep the existing POH Morytania landscape purchase gate and cover it in
  regression tests.

## 7. Modernization sequence

### Phase 0 — evidence, ownership map, and save safety

1. Capture exact primary/support writes, lever outcome routes, valve ticks and
   animation fields, every physical transition landing, dark-room behavior,
   encounter reset/re-entry, full-slot branches, and completion ordering.
2. Enumerate every entrance, ladder, stair, door, cart, hazard, shortcut, and
   downstream trigger by loc coordinate/angle and current owner.
3. Add a versioned migration/settlement ledger for local states 1/9/10/11,
   especially completed accounts missing the key and shard.
4. Correct the quest dbrow prerequisite and test every generic metadata
   consumer before removing the explicit compatibility guard.

### Phase 1 — canonical route and transactions

1. Restore transcript-shaped acceptance and move Crafting 35 to the outcrop.
2. Implement capacity-safe Zealot key and fungus lifecycles, exact entrances,
   exact ladder links, and all recovery exits.
3. Build the native points interface/outcome controller and atomic cart ledger.
4. Implement first-use keyed valve, retained key, timed current-power race,
   chisel recovery, and dark rooms.

### Phase 2 — modern owned encounter

1. Build a player-owned Treus controller with private key/HP/kill credit.
2. Add active/faded movement, frequency ramp, carts, cranes, pickaxes, melee,
   safe-door persistence, reset, death, logout, and teardown.
3. Make kill/key delivery replay-safe and test simultaneous players.
4. Share only verified mechanics with the Nightmare Zone variant.

### Phase 3 — completion and permanent content

1. Implement boosted Crafting action, shard delivery, permanent key retention,
   and protected one-time settlement.
2. Enable repeat shard cutting, keyed doors, all shortcut directions, and key
   reacquisition/key-ring integration.
3. Implement Salve creation and normal-combat effects through one target
   multiplier owner, then preserve upgrade/imbue seams.
4. Wire Tarn's Lair, Treus NMZ, Morytania Diary, POH painting regression, and
   read-only admin reconciliation.

## 8. Required tests

### State, acceptance, and migration

- Snapshot every captured primary value 0-11 with all 22 support bits.
- Start with Priest incomplete/complete; Crafting below/exact/boosted; prove
  only Priest blocks acceptance and Crafting is checked at the outcrop.
- Exercise every Zealot topic, allegiance, accept/refuse, interruption, re-talk,
  key disclosure, postquest return, and no-repickpocket state.
- Migrate local states 1, 9, 10, and 11 with key/shard in inventory, bank,
  private ground, key ring, or absent; include partial XP/QP settlement.
- Load imported states 2-8 and contradictory support bits without restart,
  completion, or destructive normalization.

### Route, fungus, puzzle, and valve

- Test every surface/inside entrance, ladder, stair, lift, door, and shortcut
  placement in both directions, including interaction across zone boundaries.
- Verify exact landing, collision, animation, follower policy, and no generic
  fallback stealing a keyed/name-specific trigger.
- Test fungus pick at full inventory; drop, sunlight exit, teleport, random
  relocation, death, bank, cart deposit, and every loss/recovery path.
- Cover all lever bits, all correct/incorrect routes, four cart outcomes,
  interface close/reopen, reconnect mid-cutscene, empty cart, destination full
  inventory, and clearing/re-sending after collection.
- Test valve without key, key in inventory/bank/ground, first unlock, retained
  key, every ghost checkpoint, win/loss boundary tick, retry, logout, death,
  teleport, two players, postquest permanent lift, and full-slot chisel.
- Enter both lower stairs with fungus, without fungus, and with every ordinary
  light source; prove both dark rooms can exit.

### Encounter ownership and combat

- Run two simultaneous players: independent keys, Treus actors, HP, hazards,
  kill credit, state, key pickup, reset, and teardown.
- Force every active/faded transition and movement point at multiple HP bands.
- Trigger cart, crane, projectile pickaxe, and melee attacks at boundary tiles;
  verify damage type/rate, protection behavior, collision, and post-kill silence.
- Test eastern-door leave/re-entry, reset timeout, death, logout, teleport,
  finite spawn expiry, non-owner damage, and owner disconnect.
- Verify normal and hard Nightmare Zone Treus unlock only after completion and
  use the intended dream-specific mechanics/stats.

### Items, completion, and consumers

- Full inventory on Zealot key, fungus, cart collect, chisel, crystal key, and
  salve shard; interruption before and after every delete/add/state write.
- Crystal-room entry with key in inventory and steel key ring; key lost,
  banked, re-obtained, and never consumed by a door or completion.
- Outcrop at Crafting 34, 35, boosted 35, expired boost, no chisel, no key,
  first cut, repeat cut, and postquest shortcut entry without fungus.
- Interrupt completion before/after shard, XP, state, QP/count, and scroll;
  prove each settles once and historic state-11 repair never duplicates XP/QP.
- Create base/e/i/ei Salve variants through every supported recipe; test exact
  melee/ranged/magic accuracy and damage against undead/non-undead, Slayer
  exclusivity, Void stacking, and POH dummy parity.
- Verify Tarn's Lair access, diary/upgrade route, Morytania mithril task once,
  Treus NMZ selection, crystal/ore shortcuts, and POH Morytania painting.
- Test journal/admin views for every valid and contradictory state without
  silent mutation.

## 9. Acceptance evidence

Do not mark Haunted Mine modernized until one evidence bundle contains:

- all pinned Wiki/Quest Helper inputs and captured unresolved native behavior;
- a reviewed state/settlement migration with before/after save fixtures;
- clean script compile, cache/pack validation, and a trigger-ownership report
  proving no generic ladder/door or duplicate name handler steals a placement;
- deterministic tests for every dialogue, state, transition, item transaction,
  puzzle outcome, timed valve branch, dark room, encounter mechanic, recovery,
  completion boundary, and downstream consumer;
- a clean-account end-to-end run without state/item injection, including the
  intended south/north traversal and postquest shortcut;
- reconnect, death, teleport, full-inventory, item-loss, boost-expiry, and
  two-player interference runs;
- verified salve crafting/combat calculations and key/key-ring lifecycle;
- journal/admin traces at all canonical checkpoints; and
- explicit regression sign-off for Priest in Peril metadata, generic travel,
  doors, combat, equipment, Crafting, Tarn's Lair, Nightmare Zone, diaries,
  and POH quest-hall content.

## 10. Prioritized findings

### P0 — reward, state, transaction, and ownership integrity

1. Completion grants no salve shard, deletes the permanent crystal-mine key,
   and disables every future shard cut.
2. Completion is XP -> state -> QP/count/scroll without a settlement guard,
   allowing duplicate XP or permanently missing QP/count after interruption.
3. Treus is a shared public NPC; one player can block, damage, or receive
   credit around another player's encounter.
4. Crystal-key pickup advances state even if delivery fails; Zealot key,
   fungus, cart collect, and chisel also report success without capacity.
5. First valve activation bypasses the Zealot's key, consumes it if present,
   and permanently powers the lift.
6. Existing local state-11 accounts lack both permanent quest products and
   need a non-duplicating migration.

### P1 — canonical route, mechanics, and recovery

1. Boostable Crafting 35 wrongly blocks starting and is never checked when
   cutting the crystal.
2. Eight native valve animation fields and current-power semantics are unused;
   the ghost race does not exist.
3. Treus has no invisibility/movement ramp, carts, cranes, pickaxes, safe-door
   persistence, or encounter reset.
4. Fungus does not crumble on drop/sunlight/teleport, and its sticky cart bit
   bypasses the required loss/death recovery route.
5. The points interface, cutscene, and four cart outcome families collapse to
   one binary success check.
6. Collect/lift ladder routing is wrong, several ingress/inside/shortcut locs
   are unowned, and dark-room recovery is replaced by a block.
7. Keyed crystal doors inherit unconditional generic self-stage behavior and
   placement-specific travel uses player zones/invented landing tiles.
8. Crystal-key loss/reacquisition/key-ring behavior is absent.

### P2 — permanent consumers, fidelity, and diagnostics

1. Salve stringing and general-combat undead effects are absent; only the POH
   dummy recognizes Salve items.
2. Treus Nightmare Zone, Tarn's Lair gating/miniquest seam, and the Morytania
   Diary task are absent or disconnected.
3. Zealot dialogue and ambush/completion cutscenes are short invented summaries.
4. Mine environmental hazards and many native assets are inert.
5. Journal guidance follows sticky/collapsed bits and claims a shard was cut
   despite no shard delivery.
6. No state audit/repair view or Haunted Mine automated coverage exists.

## 11. Evidence still required before implementation

- Exact meanings and write points for primary states 1-8.
- Exact Zealot dialogue-state writes and key full-inventory/private-ground
  behavior, including bank and postquest return semantics.
- Physical coordinate/angle and landing pairs for every entrance, ladder,
  stair, lift, door, dark room, and shortcut placement.
- Full lever route-to-outcome table and native interface/cutscene protocol.
- Exact valve timing, all eight animation-bit transitions, ghost ownership,
  reconnect behavior, and boundary-tick ordering.
- Dark-room damage/visibility and return semantics for both lower staircases.
- Treus movement points/form timings, HP-dependent frequency, hazard scheduler,
  door persistence, poison/freeze details, reset timeout, and death ownership.
- Full-inventory ordering for crystal key and first salve shard, plus exact
  interrupted quest-scroll behavior.
- Current key-ring add/remove and lost-key recovery integration hooks.
- Tarn's Lair entry gate/miniquest ownership and Morytania Diary event ID.

Until those captures exist, preserve all native fields and isolate unknown
branches behind explicit controllers and tests. The port queue's historical
compile success and “done” label establish symbol validity only; they do not
establish route fidelity, encounter ownership, reward delivery, or permanent
content integration.
