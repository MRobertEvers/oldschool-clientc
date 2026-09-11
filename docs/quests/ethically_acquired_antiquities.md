# Ethically Acquired Antiquities modernization audit

Status: `audit-pending` — the native quest row, complete 0–38 primary state
machine, permanent secondary fields, quest items, per-player display and tools
transforms, Shame-o-meter interface/client script, nineteen shame-game rows,
cutscene actors, journal, rewards, and shared character/charter owners exist.
The command-free route is nevertheless blocked at the first Trader Crewmember:
Fortis Cothon spawns `crew_man3` and `crew_woman3`, while the quest binds Talk
only for `crew_man1` and three leaves. The start fails to enforce both completed
prerequisites, the storeroom opens without its key, the display becomes full
before the diadem is returned, and the randomized witnesses, shame game,
confession cutscene, item recovery, dialogue multiplexing, and safe reward
settlement under interruption are absent or materially abbreviated.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the Fortis investigation, Regulus and
the charter crew, both sail transactions, Port Sarim and Betty, the Varrock
Museum heist, the Shame-o-meter, the confession cutscene, return and reward,
loss/recovery, shared NPC topics, journal, and administrative adapters. It is
an implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, reward, and prerequisite contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Ethically Acquired Antiquities](https://oldschool.runescape.wiki/w/Ethically_Acquired_Antiquities?oldid=15252769) | 15252769, 2026-07-04 | Identity, requirements, walkthrough, randomized witnesses, shame choices, cutscene, and rewards |
| [Ethically Acquired Antiquities/Quick guide](https://oldschool.runescape.wiki/w/Ethically_Acquired_Antiquities/Quick_guide?oldid=15252778) | 15252778, 2026-07-04 | Ordered interactions, Port Sarim alternatives, storeroom route, and return |
| [Transcript:Ethically Acquired Antiquities](https://oldschool.runescape.wiki/w/Transcript%3AEthically_Acquired_Antiquities?oldid=15097048) | 15097048, 2025-12-29 | Exact accept/refuse, topic menus, loss branches, witness responses, all shame lines, failure/success, confession, and full-inventory completion |
| [Xerna's Diadem](https://oldschool.runescape.wiki/w/Xerna%27s_Diadem?oldid=15226665) | 15226665, 2026-06-05 | Empty-before-completion/full-after-completion display contract |
| [Tools](https://oldschool.runescape.wiki/w/Tools_%28Ethically_Acquired_Antiquities%29?oldid=15202734) | 15202734, 2026-04-29 | Investigation scenery and quest phase |
| [Tattered sails](https://oldschool.runescape.wiki/w/Tattered_sails?oldid=14764693) | 14764693, 2024-10-09 | Destroy behavior and Cothon crew recovery |
| [Sails](https://oldschool.runescape.wiki/w/Sails_%28Ethically_Acquired_Antiquities%29?oldid=14764673) | 14764673, 2024-10-09 | Repaired-sail hand-in and Cothon recovery source |
| [Betty's notes](https://oldschool.runescape.wiki/w/Betty%27s_notes?oldid=15192720) | 15192720, 2026-04-22 | Read/Destroy behavior, recovery from Betty, and clue meaning |
| [Storeroom key](https://oldschool.runescape.wiki/w/Storeroom_key_%28Ethically_Acquired_Antiquities%29?oldid=14777575) | 14777575, 2024-10-16 | Haig pickpocket source, door use, Destroy/recovery, and post-quest disposal |
| [Crate](https://oldschool.runescape.wiki/w/Crate_%28Ethically_Acquired_Antiquities%29?oldid=14785098) | 14785098, 2024-10-22 | Diadem crate identity and location |
| [Charter ship](https://oldschool.runescape.wiki/w/Charter_ship?oldid=15264112) | 15264112, 2026-07-15 | Shared crew/port service and optional transport cost |
| [Curator Haig Halen](https://oldschool.runescape.wiki/w/Curator_Haig_Halen?oldid=15302146) | 15302146, 2026-08-15 | Shared museum actor and quest relationships |
| [Curator Herminius](https://oldschool.runescape.wiki/w/Curator_Herminius?oldid=14840901) | 14840901, 2025-01-22 | Fortis curator identity and non-quest topics |
| [Artima](https://oldschool.runescape.wiki/w/Artima?oldid=15197115) | 15197115, 2026-04-25 | Crafting-shop owner and sail repair favor |
| [Regulus Cento](https://oldschool.runescape.wiki/w/Regulus_Cento?oldid=14961536) | 14961536, 2025-08-08 | Shared quetzal keeper topics and Cothon clue |
| [Children of the Sun](https://oldschool.runescape.wiki/w/Children_of_the_Sun?oldid=15241067) | 15241067, 2026-06-27 | Direct completed prerequisite and Varlamore access |
| [Shield of Arrav](https://oldschool.runescape.wiki/w/Shield_of_Arrav?oldid=15302202) | 15302202, 2026-08-15 | Direct completed prerequisite, distinct from merely joining a gang |

The sources identify a members, novice, short quest released 25 September
2024. Starting requires non-boosted level 25 Thieving and completion of
Children of the Sun and Shield of Arrav. No item is required. Three thousand
coins are only one recommended travel method: the player may reach Port Sarim
by any route. Rewards are one quest point, 6,000 Thieving XP, and 5,000 coins.
The current article lists no downstream quest requirement.

Transition aid only: Quest Helper's
[`EthicallyAcquiredAntiquities.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/ethicallyacquiredantiquities/EthicallyAcquiredAntiquities.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary states
0/2/.../36, all three witness-category bits, actors, coordinates, items,
prerequisites, rewards, and positive shame lines. Its 3,000-coin item
requirement is a route convenience, not a server start requirement, and its
dialog-option automation is not authoritative shame scoring. `python3
tools/questhelper_extract.py ethicallyacquiredantiquities --check` resolves
all referenced dbrows, NPCs, locs, objects, and varbits.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_ethicallyacquiredantiquities`; dbrow pack index 3713, quest metadata ID 197 |
| Type / difficulty / length | Members quest / novice / short |
| Release / location | 25 September 2024 / Varlamore |
| Start | Empty Xerna's Diadem display at 1721,3165,0; native start loc wrapper `civitas_museum_display_diadem` |
| Requirements | Base Thieving 25; `quest_childrenofthesun` row 3450 complete; `quest_shieldofarrav` row 132 complete |
| Primary state | `%eaa`, bits 0–6 of permanent, transmitted `eaa_primary` varp 4400 |
| Canonical values | 0 not started; then even values 2 through 36; 38 complete |
| Investigation | display bit 7; tools bit 8; citizen/tourist/academic bits 9–11 |
| Shame | `%eaa_shame`, bits 12–18, native 0–100 percentage; camera-movement toggle bit 22 |
| Favor/item facts | crew asked bit 19; Artima asked bit 20; Betty told bit 21; sails given/fixed bits 23–24; notes-given bit 25 |
| Diadem/crates | display-driving diadem bit 26; crate-search field bits 27–29 |
| Native UI/data | interface 881 `eaa_shameometer`; 19 `eaa_shame_game_choice_*` dbrows in table 73 |
| Native scene actors | `eaa_curator_cutscene` NPC 13816; `eaa_thief_cutscene` NPC 13817 |
| End state / quest points | 38 / 1 |
| Reward | 6,000 Thieving XP (native raw tenths 60,000); 5,000 coins |

The carrier and all native fields must remain stable. The interface's on-load
client script subscribes to varp 4400, reads varbit 11199 (`eaa_shame`), writes
the percentage text, and animates progress/sliding bars. Modernization should
mount and drive that existing interface, not replace it with messages or a new
variable. The imported shame dbtable and rows currently expose `columns=0`, so
their lost schema/data must be recovered from cache/upstream evidence or
re-authored against live behavior; row names alone do not prove scoring.

The `civitas_museum_display_diadem` multiloc maps bit 0 to the empty display
and bit 1 to the full display. The current Wiki says the empty form exists
before quest completion. Consequently `%eaa_diadem` is a return/display fact,
not permission to make Fortis appear restored at the moment the player finds
the crate in Varrock. The `eaa_tools` multiloc correctly exposes Investigate
only at primary state 4.

## 3. Implementation surface

The direct root contains 542 lines across four files. Four mandatory shared
owners add 760 lines and cannot be modernized safely as isolated quest copies.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/ethicallyacquiredantiquities.constant` | State values, requirements, rewards, shame constants, reference coordinates | Primary values and rewards match native metadata; header explicitly soft-skips a real prerequisite and models all shame answers as +25 |
| `configs/ethicallyacquiredantiquities.varp` | Re-declares native carrier | Correct permanent/transmitted carrier; no replacement state is needed |
| `scripts/ethicallyacquiredantiquities.rs2` | Characters, favors, witnesses, shame, completion, journal, debug | Route-shaped but blocked at crew dispatch and missing most modern mechanics/dialogue/recovery |
| `scripts/ethicallyacquiredantiquities_locs.rs2` | Display, tools, diadem crate, notes | Correct named assets, but the key/door transaction, flavor crates, display timing, and recovery are wrong |

Mandatory shared/cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow`, `all.varp`, `all.varbit` | Metadata and state schema | Correct row/end state/requirements/reward and all native fields are present |
| `configs/all.loc`, `interfaces/eaa_shameometer.if`, client scripts 6950/7022/7023/7032/7034 | Per-player transforms and Shame-o-meter | Correct display/tools/UI assets exist; server never mounts the UI and sets the display bit at the wrong phase |
| `configs/all.obj` | Sails, notes, key, and cutscene props | Required carried items have correct Read/Destroy ops; diadem/tools/helmet/label props exist but scene/search coverage is incomplete |
| `configs/all.npc` | Quest actors and charter multinpcs | Curator/thief cutscene actors exist unused; charter parent-to-port leaves expose a much wider interaction surface than the quest binds |
| `transport_charter/configs/charter.spawn` | Fortis Cothon roster | Spawns parent `crew_man3` at 1742,3136 and `crew_woman3` at 1744,3136; neither has the quest Talk trigger |
| `transport_charter/scripts/charter_npc.rs2` and `charter_shop.rs2` | Charter/quick-charter/trade ops | Ops 3–5 cover all six parent models; op1 is deliberately delegated to this quest, which currently covers only a fraction |
| `quest_twilightspromise/scripts/twilightspromise.rs2` | Sole Regulus op1 owner | Correctly branches to EAA at state 8, avoiding duplicate-trigger replacement, but generic/travel/appearance topics remain abbreviated |
| `areas/port_sarim/scripts/betty.rs2` | Sole Betty op1 owner | Routes states 18–20 to EAA, then Hand in the Sand; no post-decoding EAA topic and normal shop is still a stub |
| `areas/varrock/scripts/curator.rs2` | Sole Haig Talk/Pickpocket owner | Priority is Defender of Varrock, EAA, Bone Voyage, legacy museum; mutually active topics can hide one another and Golem pickpocket can pre-empt the key |
| shared doors | Varrock Museum storeroom | `vm_store_room_door` is registered as an ordinary open/close pair, so anyone opens it without the quest key |
| quest journal dispatcher | Row-to-journal adapter | Correctly dispatches row 197 to the modern dynamic journal proc |
| shared completion API | QP/count/scroll/jingle | Modern presentation exists but point/count awarding is additive and unguarded; callers must make settlement exactly-once |
| quest cheat | Administrative state adapter | Writes 38 only, with no reward or reconciliation; useful as an idempotent state adapter, not route evidence |

No downstream `%eaa` or `quest_ethicallyacquiredantiquities` consumer was
found outside these owners, journal, and quest cheat. Modernization must still
test shared NPC and charter behavior because this quest presently owns their
Talk dispatch even when EAA is inactive.

## 4. State and transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Inspect empty display; enforce both completed quests and base Thieving 25 | Base-level check is correct, Children of the Sun is absent, and either gang's *joined* state is accepted instead of Shield of Arrav completion. Accept/refuse text is abbreviated. |
| 2 | Ask Herminius about the theft | One short branch reaches 4; normal history/religion/recent-history menu and progress re-talks are replaced. |
| 4 | Investigate specialist tools, then inspect the case | Ordered bits and transform work, but narrative is reduced and interaction/reconnect tests are absent. |
| 6 | Question visitors until the useful witness points toward quetzals | Current code gives every category a distinct useful clue and advances only after all three bits. It has no randomized per-player witness or canonical unhelpful responses. |
| 8 | Ask Regulus, preserving his normal quetzal topics | Shared owner reaches 10, but collapses Regulus's menu and dialogue. |
| 10 | Ask any Fortis Cothon crewmember and accept the sail favor | Hard-blocked: actual parent spawns are man3/woman3; only man1 is bound. |
| 12 | Take/recover tattered sails and have Artima repair them | Initial repair works if manually placed here with the item; crew cannot replace lost sails and Artima has no refusal/re-talk/full transaction coverage. |
| 14 | Return repaired sails to any Cothon crewmember | Same dispatch block; only Talk consumes the sail, while current article says Use and transcript/quick guide say Talk. Lost repaired-sail recovery is impossible. |
| 16 | Reach Port Sarim by any method; ask Stan or any southern-dock crew | Stan can advance, but Port Sarim man3/woman3 have no Talk binding. Stan's branch does not itself assert location. |
| 18 | Ask Betty; receive notes with a free slot | Basic capacity gate works; all shared shop/topics and exact conversation are replaced. |
| 20 | Read/recover notes | Read reaches 22. Recovery checks inventory only, so banked notes permit duplicates; native `eaa_given_notes` is unused. |
| 22 | Confront Haig | One abbreviated exchange reaches 24; DOV priority may suppress it and Bone Voyage/museum topics are not composed. |
| 24 | Pickpocket key | One carried key is granted after a slot check; banked key duplicates, Golem can consume first Pickpocket, and no door state is changed. |
| 26 | Unlock storeroom and search diadem crate | Door is globally openable without key. Crate accepts state 26 without possession, deletes every carried key, sets the Fortis display full immediately, and ignores the other two canonical crate searches. |
| 28 | Confront Haig and begin interrogation | Two lines reset shame to zero and reach 30; opening cutscene/focus and exact dialogue are absent. |
| 30 | Random shame challenge with positive/negative choices and failure | Fixed three-option menu repeats; every answer adds 25, no choice can fail, native UI/data/camera flag are unused, and four clicks guarantee success. |
| 32 | Haig yields; begin confession | One sentence reaches 34; no resumable scene ownership. |
| 34 | Watch Grand Museum flashback and arrange return | One message reaches 36; both native actors, camera, movement toggle, confrontation/combat, dialogue, and cleanup are unused. |
| 36 | Tell Herminius; require reward space; settle once | No preflight capacity check. State 38 and XP are written before the unchecked coin add; full inventory can lose the 5,000 coins. Completion proc has no entry/idempotency guard. |
| 38 | Full display, rewards and post-quest dialogue | Display is already full from state 26; generic thank-you replaces Herminius's normal menu. Direct/debug settlement can duplicate XP, coins, QP, and completed count. |

The even primary values match cache and Quest Helper and must not be
renumbered. Recovery and scene settlement should use the existing secondary
facts where their meanings fit. If exact live witness randomization or reward
delivery requires a durable fact absent from the cache, add one explicitly;
do not overload the crate-search field, camera toggle, or another quest's
carrier.

## 5. Detailed lifecycle audit

### Start, requirements, Herminius, and clues

The display uses `stat_base(thieving)`, correctly making level 25
non-boostable. The remainder of the gate is materially wrong. Children of the
Sun completion is not checked at all. Shield of Arrav's Phoenix values are
joined 9 and complete 10; Black Arm values are joined 3 and complete 4. The
current predicate accepts 9/3, so a player partway through Shield of Arrav may
start. Require `%vmq1 >= ^cots_complete` and either gang's exact completed
predicate through a shared quest-requirement helper, aligned with the two
native requirement rows. Keep the displayed failure non-mutating and test
each prerequisite independently.

Acceptance currently offers bare Yes/No and immediately summarizes the theft.
Restore the display's canonical internal line, refusal, and instruction to
speak to the curator. Herminius must retain his early-history, religion,
recent-history, and exit topics alongside the contextual EAA option. His
progress branch distinguishes incomplete investigation, the quetzal lead, the
longer journey, and recovered diadem. An unconditional EAA label with a
generic fallback is not a modern shared-NPC menu.

The tools-then-case order is correct and driven by native state/transform. The
visitor model is not. All four academic IDs, all six tourist IDs, and the two
Quest Helper citizen IDs are bound, but they always report fixed useful clues.
The current article says the useful person is randomized per player, while the
native three bits and pinned helper track all three categories. Reconstruct
the live selection/termination rule before coding: preserve durable questioned
facts, give canonical no-information responses, choose the correct witness
per player, and advance only on the real success condition. Test every chosen
category, every talk order, repeat talks, logout, and two players with
different witnesses.

Regulus is correctly multiplexed in Twilight's Promise's sole trigger, which
avoids the engine's duplicate-name replacement behavior. Restore the complete
menu around the EAA option and test both quests at all overlapping states,
normal Varrock travel, appearance changes, and re-talk after the clue.

### Charter crew, sails, Artima, and Port Sarim

This is the first hard blocker. The locally maintained Fortis roster spawns
the charter parents `sailing_transport_trader_stan_crew_man3` and
`...crew_woman3`. EAA binds only the man1 parent, base/Piscarilius/Port Sarim
man1 leaves, and no women. Because this runtime intentionally does not resolve
NPC multinpcs before name dispatch, neither Cothon NPC reaches
`@eaa_crew_talk`. Bind one authoritative op1 owner for all six charter parent
models and the required leaves if future engine resolution is enabled, then
route by the actor's actual port and EAA state. Do not create duplicate op1
triggers.

The shared menu must keep “who are you?”, charter travel, no-thanks, and EAA
topics. Dedicated op4 Charter and op5 quick-charter already work across all
parents, and op3 owns the shop; successful dedicated travel is not proof that
Talk works. The server must not require payment or a charter journey: any
arrival method at Port Sarim is valid.

The first Cothon favor needs exact acceptance differences for One Small
Favour, refusal/re-offer, a one-slot preflight, and an acknowledged item add.
Both sail objects have Destroy text directing the player back to the Fortis
Cothon crew. At state 12, current crew dialogue has no replacement branch. At
state 14, destroying repaired sails leaves Artima saying only that she already
fixed them and crew asking where they are. Implement the canonical cycle:
crew replace truly lost tattered sails and Artima can repair replacements.
Verify whether live recovery treats banked or dropped copies as lost, then
encode that policy explicitly; do not let an accidental inventory-only check
decide duplicate behavior. Ensure every delete/add is checked or
capacity-neutral.

Artima's full favor thanks the crew for their latest shipment and supports
decline/re-talk/re-repair. Preserve her crafting-store topics rather than
claiming op1 globally for quest prose. The main article currently instructs
using repaired sails on a crewmember, while the pinned transcript and quick
guide describe talking and Quest Helper automates Talk. Verify live packet
behavior; support every confirmed action direction through one transaction so
neither can double-consume or advance twice.

At state 16, only the southern Port Sarim dock may provide the rune-store clue.
The local roster there is Stan plus parent man3/woman3; Stan works, both crew
Talk actions do not. Route Stan and every crew model through a port-aware
predicate and preserve their non-quest options. Test arriving by charter,
teleport, walking, spirit tree, and administrative placement; actors at any
other charter port must not advance the quest.

### Betty, notes, Haig, key, door, and crates

Betty's initial slot check is sound, but recovery currently equates “not in
inventory” with “lost.” Verify live bank/drop-trick behavior and encode it
explicitly. Use the native first-delivery fact consistently, make adds
explicit, and preserve the notes' Read/Destroy lifecycle. Reading must reveal
the Varrock Teleport rune pattern, not just name the destination.
After visiting Haig, Betty also has a one-time “I managed to decipher those
notes” topic; current routing ends at state 20 and can never show it. Compose
this with Hand in the Sand, pink dye, and her shop rather than ordering whole
quests by an if-chain.

Haig has three simultaneous ownership problems:

- Defender of Varrock's candidate check runs before EAA and can suppress every
  EAA Talk while both are active.
- EAA runs before Bone Voyage and legacy museum topics, replacing rather than
  adding the current option menu.
- Golem statuette recovery runs before EAA Pickpocket, so the first action may
  award another quest's tiny key.

Build a shared topic/action dispatcher that exposes all eligible Talk options
and intentionally sequences distinct pickpocket outcomes. Do not let starting
or completing an unrelated quest permanently hide the diadem route. EAA's
storeroom pickpocket appears guaranteed in the transcript; restore its
animation/message after verifying live behavior and reserve a slot. Determine
whether a banked/dropped key permits another pickpocket rather than inheriting
the current inventory-only policy. The pinned key page says a destroyed key is
stolen from Haig again and that it can be safely destroyed after the quest.
Verify the exact post-search/post-quest availability, then implement it without
unbounded accidental duplicates.

The storeroom key is currently ceremonial. Both door locs are registered in
the generic door category, so ordinary Open performs a public door swap for
any player at any quest state. There is no EAA door or use-key handler. Remove
this pair from unrestricted generic ownership or add a higher-priority
quest-aware lock transaction: outside access needs the carried key at the
right phase, inside exit must remain safe, opening must not consume the key,
and concurrent players must not inherit one another's authorization. Test
Open and use-key actions, both sides, carried/banked/destroyed/duplicate key,
logout inside, collision, and two players.

The target `eaa_large_crate` should reveal the diadem and other displaced
artefacts, then send the player back to Haig. Current code advances from state
26 even without a key, deletes every carried key, and immediately sets
`%eaa_diadem=1`, making the Fortis display full before Haig confesses or
arranges return. Retain the key according to the item contract and set the
display fact only when the return is committed. The room's `eaa_large_crates`
also has Search but no server handler; restore the random Broodoo shield,
Arceuus Library book, and Outpost label descriptions. Determine and honor the
native three-bit `eaa_crate_search` semantics rather than setting it to 1 for
the main crate and ignoring the rest.

### Shame-o-meter and confession cutscene

State 30 is a placeholder, not the shipped mechanic. It repeats these three
choices forever, all worth +25: protect history, stealing is against the law,
and give it back. The canonical interaction draws varying choices from a much
larger matrix, includes lines that decrease shame, ends unsuccessfully when
the player fails the round, and succeeds only on reaching 100%. The transcript
contains both endings and the current article classifies positive/negative
answers. Never infer the exact grouping, score deltas, or failure limit from
the article table alone. Recover the nineteen-row schema/client-server
contract or capture live traces, encode the matrix as data, and add
deterministic seed fixtures for tests.

Mount native `eaa_shameometer` for the interrogation, let its existing client
script render `%eaa_shame`, and close it on success, failure, cancellation,
logout, death, region change, or another modal. Respect the native camera-
movement toggle. A failed round must leave the quest retryable without stale
UI or impossible percentage; successful delivery and repeated packets must
not overshoot 100 or enter the confession twice. Two concurrent players need
independent choice sequences and meters.

The confession is almost wholly absent. The canonical sequence focuses on
Haig, moves to the Grand Museum, shows him visiting during a Colosseum event,
pans to the thief, stages the theft and confrontation, has Haig subdue the
thief, explains his bad “safekeeping” decision, returns to Varrock, and ends
with the player's non-disclosure deal. Native `eaa_curator_cutscene` and
`eaa_thief_cutscene` records exist specifically for it. Current states 32 and
34 produce one line and one message and use neither actor.

Implement a player-owned scene/instance with explicit entry, camera, actor,
movement/combat, dialogue, return, and cleanup boundaries. Make states 32/34
resumable: interruption before the return agreement must replay/resume safely;
after the agreement it must reach 36 exactly once. The public Fortis curator,
Haig, thief, and display must never be retyped or moved for another player.

### Return, reward, and post-quest state

The transcript has an explicit full-inventory branch: Herminius tells the
player to make space and does not complete the quest. Current code skips that
preflight, writes state 38, awards XP, attempts an unchecked coin add, then
adds QP/completed count and shows the scroll. A full inventory without a coin
stack loses the 5,000 coins permanently; a near-maximum stack also needs
defined overflow behavior. Direct or duplicated calls add all rewards again.

Create one idempotent settlement transaction with an entry guard and durable
reward fact if needed. At state 36, require capacity for the exact coin result,
finish Herminius's dialogue, grant 6,000 XP and 5,000 coins, award one QP and
one completed count, set the display-return fact and state 38 at the defined
commit boundary, then present the scroll/jingle. On cancellation or reconnect,
re-talk must resume before or after that boundary without lost or duplicated
rewards. Preserve Herminius's ordinary post-quest history topics plus the
thank-you outcome.

### Journal and administrative adapters

The journal uses the modern dbrow dispatcher and maps every primary phase. It
is too coarse around the favor and heist: it does not distinguish missing
tattered/fixed sails, banked/lost notes/key, the three visitor bits, crate
searches, current shame percentage/failure, or an interrupted cutscene/reward.
Render meaningful partial progress and recovery without exposing the random
answer, preserve standard completed styling, and test all valid secondary
combinations.

`::ethicallyacquiredantiquities` resets EAA, mutates Shield of Arrav to the
*joined* Phoenix state, and adds only 1,000 Thieving XP (raw 10,000 tenths) if
the base level is below 25. A low-level account may remain below 25, while the
incorrect joined predicate disguises the Shield completion bug. It also does
not complete Children of the Sun. Replace destructive prerequisite mutation
with explicit fixtures or a diagnostic that reports unmet setup.

`::eaarun` directly writes every state, clears all carried quest-item copies,
fabricates items, skips real charter/shame/cutscene interactions, and invokes
the unguarded reward proc. It can destroy user items and duplicate XP/coins/QP;
it is not a verification test. Retire it in favor of automated transition
fixtures and a command-free route. Generic `::complete` writes only state 38;
run it twice as an admin-adapter test, but never treat it as reward or route
evidence.

## 6. Migration and recovery

Primary values and native fields already have stable meanings. Deployment
must reconcile legacy saves rather than reset or reinterpret them silently:

1. Preserve valid `%eaa` values 0–38 and all native secondary fields. Reject
   impossible odd/out-of-range values with telemetry and a support path.
2. Re-evaluate state-0 start eligibility only; do not roll back a quest already
   started because the old handler admitted joined Shield/unfinished Children
   saves. Record those grandfathered cases for audit.
3. State 10 is unreachable through current world interactions because of the
   Fortis crew binding. Preserve imported/debug states and require every state
   10–16 save to resume through the repaired parent-model dispatcher.
4. Reconcile tattered/fixed sails, notes, and key across inventory and bank.
   Preserve one legitimate copy according to live behavior; quarantine or
   support-review duplicates instead of deleting arbitrary bank contents.
5. For states 26–36 with `%eaa_diadem=1`, keep quest progress but render the
   Fortis display empty until the authoritative return commit. Do not clear a
   legitimate state-38 full display.
6. Preserve valid shame percentages at state 30 if live scoring supports them;
   otherwise restart only the current interrogation round with an explicit
   migration notice. Close any orphaned interface/scene on login.
7. Normalize crate-search values only after the three-bit live semantics are
   recovered. The current value 1 may be a valid observed search and must not
   be repurposed as reward or cutscene state.
8. States 32/34 created by the placeholder have not seen the real confession.
   Resume them through a migration-safe cutscene entry; do not replay the
   heist or reset them to require another key.
9. State-38 players may have lost coins because of full inventory or gained
   duplicate rewards through debug/direct calls. Aggregate XP/QP/quest count
   cannot attribute this quest reliably. Use telemetry, an explicit migration
   ledger, or support reconciliation; never blindly replay all rewards.
10. Derive full-display and post-quest dialogue from settled completion on
    login. Administrative completion must not invoke fresh-route rewards.

## 7. Modernization sequence

### Gate A — state, requirements, and command-free route

1. Lock the native 0–38/secondary-field contract in transition tests and add
   migration fixtures for every legacy state before changing handlers.
2. Enforce completed Children of the Sun and Shield of Arrav plus base 25
   Thieving, with exact accept/refuse/full progress dialogue.
3. Restore Herminius's topic menu, tools/case transaction, randomized witness
   behavior, Regulus multiplexing, and complete re-talk paths.
4. Repair charter op1 ownership for all six parents/leaves and prove a fresh
   player can reach Artima and Port Sarim without a debug write.

### Gate B — item transactions and museum heist

1. Implement bank-aware tattered/fixed-sail recovery and Artima's full favor,
   then unify verified Talk/use-item hand-in directions.
2. Restore Port Sarim Stan/crew location-aware dialogue and Betty's
   delivery/recovery/decoding topics without breaking other quests or shops.
3. Compose Haig's Talk/Pickpocket topics across DOV, Bone Voyage, Golem, and
   legacy museum content; implement deterministic key ownership/recovery.
4. Replace the unrestricted storeroom door with a key-authorized traversal,
   restore all crate searches, and delay the full-display fact until return.

### Gate C — Shame-o-meter and cutscene

1. Recover/re-author the nineteen-row shame data from authoritative live/cache
   evidence, including grouping, deltas, failure, retry, and randomization.
2. Mount interface 881 and drive native `%eaa_shame`; handle modal/camera and
   every cleanup/interruption boundary.
3. Stage the confession in a player-owned scene using both native actors and
   camera/movement/combat assets, with deterministic teardown.
4. Make state 28→36 retryable and exactly-once under repeated Talk, logout,
   death, region change, and simultaneous players.

### Gate D — settlement, integration, and migration

1. Add Herminius's canonical capacity preflight and an idempotent XP/coin/QP/
   count/display/state/scroll settlement boundary.
2. Expand journal recovery details and replace unsafe debug walks with real
   transition/runtime fixtures.
3. Reconcile legacy prereq, item, early-display, shame, cutscene, and ambiguous
   reward saves according to the migration policy above.
4. Run fresh, migrated, recovery, isolation, shared-NPC, charter, scene, reward,
   and administrative verification through real interactions.

## 8. Verification matrix

| Area | Required checks |
| --- | --- |
| Start | Every Children 0/24 × Shield not-started/joined/complete × Thieving 24/25 combination; accept/refuse; no state mutation on failure; display/start coordinate |
| Herminius | States 0–38; all history/religion/recent-history/EAA/exit topics; partial clues and post-quest menu; repeated Talk |
| Tools/display | Both interaction orders; repeat clicks; state/bit transforms; logout between tools and display; two players with different phases |
| Witnesses | Every selected witness category/NPC; all talk orders; unhelpful/repeat responses; logout/reconnect; no cross-player random seed leak |
| Regulus | EAA state 8 before/during/after Twilight's Promise; Varrock travel; appearance topics; re-talk; no duplicate trigger |
| Charter dispatch | All six parent models and representative base/Fortis/Port Sarim leaves; op1/3/4/5; Fortis actual man3/woman3; no action replacement |
| Tattered sails | Accept/refuse/re-offer; full inventory; carried/banked/destroyed/duplicate; crew recovery; exact one add |
| Artima/fixed sails | Favor accept/refuse/re-talk; carried/banked/lost input; repeated repairs; slot safety; shop topics; exact one input/output |
| Sail hand-in | Talk and every live-confirmed item-use direction; all crew models; wrong port/state/item; repeated packet; one consume/advance |
| Port Sarim | Stan plus man3/woman3; southern dock boundary; every travel method; other ports rejected; charter menu preserved |
| Betty/notes | Full inventory; first/replacement copy; bank awareness; Read/Destroy; decoded follow-up; Hand in the Sand/shop coexistence |
| Haig Talk | Every EAA state with DOV/Bone Voyage combinations; all museum topics and option ordering; no quest suppression |
| Pickpocket/key | Golem overlap; carried/banked/destroyed/duplicate/full inventory; animation/message; pre/post search and post-quest recovery policy |
| Storeroom door | No key/key in inventory/key banked; Open/use-key; both sides; logout trapped side; collision; two players; key retained |
| Crates/display | Target and two flavor crates; native three-bit values; repeat search; key lifecycle; Fortis empty through state 36 and full only on settlement |
| Shame data | Every row/choice/delta; positive and negative choices; random groups; exact 0/100 clamps; failure/retry; seeded deterministic tests |
| Shame UI | Correct text/bar animation; mount/close; camera toggle; cancel/logout/death/region/modal cleanup; two players |
| Cutscene | All actors/cameras/movement/combat/dialogue; state 32/34 resume; cancel at every tick; missing actor/map; simultaneous players; no public mutation |
| Completion | Full inventory/no coin stack; existing and near-max stack; exact 6,000 XP/5,000 coins/1 QP/count once; repeated Talk/call/login; scroll/jingle order |
| Migration | Every primary state and valid secondary combination; prereq-grandfathering; item/bank duplicates; early full display; shame 0–100; ambiguous state-38 rewards |
| Journal/admin | Every phase/partial/recovery/scene state; standard completion; safe setup fixtures; `eaarun` unavailable to normal verification; `::complete` twice |

Required static evidence includes a clean RuneScript/config build, duplicate
trigger and unresolved-symbol scans, an exhaustive primary/secondary
transition suite, charter parent/leaf coverage, shame-data validation, no
unexpected numeric IDs, and `python3 tools/questhelper_extract.py
ethicallyacquiredantiquities --check`. Required runtime evidence is a
command-free fresh 0→38 playthrough, all item-loss and full-inventory branches,
every shared character with overlapping quest topics, alternate Port Sarim
travel, two concurrent witness/shame/cutscene players, interruption at every
scene and settlement boundary, and migrated saves at every state. A debug
runner, manually written state, completion scroll, or successful compile alone
is not route proof.

## 9. Definition of done

Ethically Acquired Antiquities is modernized only when an eligible fresh
player can start from the native empty display, investigate a genuinely
per-player witness trail, use any intended Fortis/Port Sarim charter crew,
complete and recover both sail forms, decode/recover Betty's notes, confront
and pickpocket Haig without suppressing other quests, unlock the storeroom with
the key, inspect all crates, and complete the native randomized Shame-o-meter.
The full confession must run in a player-owned, interruption-safe scene using
the native actors, and the diadem must remain absent from Fortis until its
return is committed. Herminius must refuse completion without reward space and
settle exactly one 6,000-XP, 5,000-coin, one-QP, one-count reward with state 38,
full display, jingle, and scroll in canonical order. Every loss, bank, repeat,
logout, migration, shared-NPC, and concurrent-player case must recover without
debug writes, duplicated rewards, stranded state, leaked scene state, or
regressions to charter, Twilight's Promise, Hand in the Sand, Bone Voyage,
Defender of Varrock, Golem, or normal museum/shop services.
