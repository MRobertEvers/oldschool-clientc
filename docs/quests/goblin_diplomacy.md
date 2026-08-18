# Goblin Diplomacy modernization audit

Status: `audit-pending` — the native quest row, primary and support state,
current actors, three armour crates, Grubfoot forms, goblin-mail colours,
journal dispatch, completion adapter, and admin adapter exist. The supported
current route cannot start or progress: authored dialogue is bound to the
historical pre-2006 generals while the world spawns their current red/green
forms, and Another Slice of H.A.M. owns those current forms with an unconditional
pre-quest refusal. The implementation also preserves the removed Rusty Anchor
bartender start, uses obsolete main-state meanings, leaves all three crate
sources inert, omits the Grubfoot fitting cutscenes, and has unsafe shared
mail-dye selection and incomplete downstream routing.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to both generals, Grubfoot, the obsolete
bartender route, every primary/support state, all three armour crates and the
village ladder, ordinary/coloured goblin mail, dye creation and application,
the three fitting scenes, completion/recovery, journal/admin adapters, shared
NPC ownership, migration, and direct downstream consumers. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable start, route, dialogue, item, cutscene, reward, and integration
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Goblin Diplomacy](https://oldschool.runescape.wiki/w/Goblin_Diplomacy?oldid=15262349) | 15262349, 2026-07-13 | Identity, current general start, supplies, route, rewards, direct dependants, and 2006 start change |
| [Goblin Diplomacy/Quick guide](https://oldschool.runescape.wiki/w/Goblin_Diplomacy/Quick_guide?oldid=15054013) | 15054013, 2025-11-22 | Exact current order, three crate locations, dye preparation, choices, and Dragon Slayer I dialogue overlap |
| [Transcript:Goblin Diplomacy](https://oldschool.runescape.wiki/w/Transcript%3AGoblin_Diplomacy?oldid=15263210) | 15263210, 2026-07-14 | Random opening arguments, accept/refuse, knowledge topics, wrong-item uses, fitting scenes, Grubfoot dialogue, and completion |
| [General Bentnoze](https://oldschool.runescape.wiki/w/General_Bentnoze?oldid=15012457) | 15012457, 2025-10-31 | Current ID 669, historical IDs, shared quests, and hard-clue ownership |
| [General Wartface](https://oldschool.runescape.wiki/w/General_Wartface?oldid=15012456) | 15012456, 2025-10-31 | Current ID 670, historical forms, and shared quest ownership |
| [Grubfoot](https://oldschool.runescape.wiki/w/Grubfoot?oldid=14767510) | 14767510, 2024-10-13 | Brown/orange/blue forms and Goblin Diplomacy/Another Slice/Land of the Goblins ownership |
| [Crate](https://oldschool.runescape.wiki/w/Crate?oldid=15122180) | 15122180, 2026-02-06 | Three one-mail Goblin Village sources and repeat-search behavior |
| [Goblin mail](https://oldschool.runescape.wiki/w/Goblin_mail?oldid=15183044) | 15183044, 2026-04-22 | Tradeable brown mail, three quest copies, drops, dye variants, and Land of the Goblins reuse |
| [Orange goblin mail](https://oldschool.runescape.wiki/w/Orange_goblin_mail?oldid=15291368) | 15291368, 2026-08-09 | Orange recipe, quest hand-in, and colour lifecycle |
| [Blue goblin mail](https://oldschool.runescape.wiki/w/Blue_goblin_mail?oldid=15291366) | 15291366, 2026-08-09 | Blue recipe, quest hand-in, and colour lifecycle |
| [Goblin (Goblin Village)](https://oldschool.runescape.wiki/w/Goblin_%28Goblin_Village%29?oldid=15290837) | 15290837, 2026-08-09 | Red/green village variants, matching mail drops, dialogue, and infighting |
| [Aggie](https://oldschool.runescape.wiki/w/Aggie?oldid=15083478) | 15083478, 2025-12-10 | Dye ingredients/costs, direct item use, unlocked Dyes operation, and current Make-All service |
| [Orange dye](https://oldschool.runescape.wiki/w/Orange_dye?oldid=15262780) | 15262780, 2026-07-14 | Red-plus-yellow mixing and mail use |
| [Blue dye](https://oldschool.runescape.wiki/w/Blue_dye?oldid=15183581) | 15183581, 2026-04-22 | Two woad leaves plus five coins and mail use |
| [Bartender (Rusty Anchor Inn)](https://oldschool.runescape.wiki/w/Bartender_%28Rusty_Anchor_Inn%29?oldid=15258575) | 15258575, 2026-07-09 | Current beer/clue responsibilities; no Goblin Diplomacy start role |
| [The Lost Tribe](https://oldschool.runescape.wiki/w/The_Lost_Tribe?oldid=15292326) | 15292326, 2026-08-10 | Direct prerequisite consumer and shared general dialogue |
| [Recipe for Disaster: Freeing the Goblin generals](https://oldschool.runescape.wiki/w/Recipe_for_Disaster%2FFreeing_the_Goblin_generals?oldid=15292337) | 15292337, 2026-08-10 | Direct prerequisite consumer |
| [Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M.?oldid=15292360) | 15292360, 2026-08-10 | Current general and Grubfoot routing overlap |
| [Land of the Goblins](https://oldschool.runescape.wiki/w/Land_of_the_Goblins?oldid=15292373) | 15292373, 2026-08-10 | Shared Grubfoot/Aggie and repeated multi-colour mail-dye lifecycle |
| [General Bentnoze hard clue](https://oldschool.runescape.wiki/w/Clue_scroll_%28hard%29_-_Generally_speaking%2C_his_nose_was_very_bent?oldid=15192063) | 15192063, 2026-04-22 | Puzzle-box topic that the shared general router must preserve |

The sources define a free-to-play, novice, very-short quest released 8 May
2001. It has no quest, skill, or combat requirements. Current content starts
at either General Bentnoze or General Wartface in Goblin Village; the Rusty
Anchor bartender stopped starting it on 17 July 2006. The player supplies
three brown goblin mails, one blue dye, and one orange dye, then presents
orange, blue, and finally uncoloured brown mail. Rewards are five quest points,
200 Crafting XP, and one gold bar. The Lost Tribe and Recipe for Disaster's
goblin-generals subquest directly require completion.

Transition aid only: Quest Helper's
[`GoblinDiplomacy.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/goblindiplomacy/GoblinDiplomacy.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (the file last
changed in `2330e62f881fd6d7e2bc57e73b9deb5f87470f3f` on 2026-02-15)
confirms current primary states 0, 3, 4, and 5; all three crate bits; the
current red Bentnoze; both ladder endpoints; eight route coordinates; six
required item symbols; five locs; five QP; 200 Crafting XP; and the gold bar.
Running `python3 tools/questhelper_extract.py goblindiplomacy --check` resolves
every referenced gameval and the quest dbrow. It cannot prove server trigger
reachability, dialogue/cutscene ownership, item-slot transactions, recovery,
or prerequisite consumers.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_goblindiplomacy`; quest metadata ID 15 |
| Type / difficulty / length | Free-to-play quest / novice / very short |
| Release / location | 8 May 2001 / Asgarnia |
| Start | Current `general_bentnoze_red` and `general_wartface_green`; marker at Goblin Village |
| Primary state | `%gobdip_main`, bits 0–5 of native carrier varp `goblinquest` (varp 62) |
| Current canonical values | 0 not started; 3 accepted/waiting for orange; 4 orange rejected/waiting for blue; 5 blue rejected/waiting for brown; 6 complete |
| Legacy-only values | 1 and 2 belong to the removed bartender-era path and are not current Quest Helper route states |
| End / quest points | State 6 / 5 QP |
| Requirements | None |
| XP | 2,000 raw Crafting units = 200 XP |
| Item reward | One gold bar |
| Direct consumers | The Lost Tribe and Recipe for Disaster/Freeing the Goblin generals |

The remainder of `goblinquest` already contains the current support contract:

| Field | Native storage | Expected owner / current result |
| --- | --- | --- |
| `%gobdip_crate1_searched` | bit 6 | North crate entitlement/history; orphaned |
| `%gobdip_crate2_searched` | bit 7 | Western-hut crate entitlement/history; orphaned |
| `%gobdip_crate3_searched` | bit 8 | Upstairs crate entitlement/history; orphaned |
| `%gobdip_grubfoot_vis` | bits 9–11 | Brown/orange/blue/hidden Grubfoot transform; orphaned |
| `%gobdip_know_about_armour` | bit 12 | Player asked where mail comes from; orphaned |
| `%gobdip_know_about_dye` | bit 13 | Player asked where dye comes from; orphaned |
| `%gobdip_met_aggie` | bit 14 | Aggie's one-op/two-op service transform; orphaned |

The cache supplies `catwalk_goblin` as a per-player multinpc with brown,
orange, blue, and hidden children, and `aggie` as a multinpc whose second form
adds the `Dyes` operation. These are native presentation/service fields, not
spare quest flags. Aggie owns her met/service bit independently because her
dye shop is permanent shared content, while the two knowledge bits belong to
the generals' optional post-acceptance questions.

### Required legacy migration

The local aliases reproduce the old seven-step route:

| Local value | Local meaning | Current/native destination |
| ---: | --- | --- |
| 0 | Not started | 0 |
| 1 | Bartender rumour heard; journal marks quest started | Versioned bridge to current accepted state 3 after preserving consent/progress |
| 2 | Generals requested orange | 3 |
| 3 | Orange already handed in | 4, not current state 3 |
| 4 | Blue already handed in | 5, not current state 4 |
| 5 | Brown consumed; completion queue pending | Protected settlement/completion, not current state 5 |
| 6 | Complete | 6, subject to reward-history reconciliation |

A numeric alias edit would therefore rewind every local state 3–5 by one
hand-in and could consume duplicate mail. Use a deployment/version marker and
one-time mapping before current handlers become reachable. Support bits and
owned items are useful consistency evidence but cannot alone distinguish every
case: this port never wrote them, while a pristine current state may also have
all zero bits. State 5 is especially ambiguous because the local code consumes
brown mail before queuing state 6, whereas current state 5 means brown is still
required. Never guess historic XP/gold-bar delivery from state 6.

## 3. Implementation surface

The direct root has 406 lines across one constants file and two scripts. The
live route additionally depends on two general files, the bartender, current
world spawns, crate/ladder cache assets, shared dye/Aggie/Wyson/drop systems,
and several quests that use the same actors and mail variants.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_gobdip/configs/quest_gobdip.constant` | Seven local state aliases and QP constant | End/QP correct; states 1–5 encode obsolete meanings |
| `quest_gobdip/scripts/quest_gobdip.rs2` | Shared mail dyeing, historical dialogue, hand-ins, reward | Route-shaped old implementation; no current actor binds, modern start, crates, use-on generals, or fitting scenes |
| `quest_gobdip/scripts/gobdip_journal.rs2` | Full dbrow journal | Well-rendered but entirely aligned to bartender-era states; current states 3–5 read one hand-in ahead |
| `areas/falador/.../general_bentnoze.rs2` and `general_wartface.rs2` | Talk handlers and Lost Tribe delegation | Bind cache IDs 675/676, historical forms removed from the world in 2006 |
| `areas/world/configs/m46_54.spawn` | Current Goblin Village cast | Correctly spawns IDs 669/670 equivalents and the Grubfoot shell |
| `quest_anothersliceofham/scripts/slice_generals.rs2` | Current red/green general handlers | Sole current actor owner; refuses every player below Another Slice's generals state, blocking Goblin Diplomacy, ordinary dialogue, Lost Tribe, and clues |
| native locs / ladder categories | Three Search crates and upstairs travel | Ladder has working shared category bindings; all three crate Search ops are unhandled |
| native NPC support | Grubfoot colours, Aggie Dyes form, current generals | Present but Goblin Diplomacy never writes/delegates them |
| `skill_crafting/scripts/dye_cape.rs2` plus quest dye proc | Dye mixing and two-way mail dye use | Basic orange/blue path exists; selected-mail identity is lost, red/green mail omitted, and Land of the Goblins shares the unsafe proc |
| `areas/draynor/scripts/aggie.rs2` | Dye ingredient trades and other quest topics | Single-dye path works; met bit, op 3, and current Make-All service are absent |
| `areas/falador/scripts/wyson_the_gardener.rs2` | Woad-leaf purchase | Correct 20-coin/two-leaf option; full inventory with a surviving coin stack can charge without delivering |
| goblin drop tables | Alternative mail acquisition | Generic goblins drop brown; red/green Goblin Village NPCs incorrectly share a brown-mail drop instead of matching coloured mail |
| journal dispatcher / generic quest cheat | Journal and state-only completion | Both route to the correct row/varbit; admin completion intentionally grants no rewards |
| The Lost Tribe | Direct prerequisite and shared general topic | Correctly gates start on state 6, but its general topic is attached through the unreachable historical handlers |
| RFD goblin-generals subquest | Direct prerequisite consumer | Local inspect/cook route checks only RFD intro state and does not require Goblin Diplomacy |
| General Bentnoze clue | Hard puzzle-box topic | Explicitly deferred and unavailable on the current actor |

No raw numeric IDs are written in quest scripts and the current completion
renderer is used. The obsolete machinery is semantic and dispatch-related:
old actor forms and state values were copied forward even though the cache,
world spawns, marker row, and helper all describe the redesigned quest.

## 4. Current-state transition audit

| State | Current canonical phase | Local behavior / defect |
| ---: | --- | --- |
| 0 | Talk to either current general, explore optional argument topics, choose a different colour, explicitly accept, then write 3 | Current actors are intercepted by Another Slice and refuse. Historical handler has no state-0 reply branch. Bartender instead writes obsolete state 1 without a quest confirmation. |
| 1 | Reserved/legacy; not a current helper step | Local journal says the bartender started the investigation. Historical generals can advance it, but those NPCs are not spawned. |
| 2 | Reserved/legacy; not a current helper step | Local means waiting for orange. This must migrate to 3. |
| 3 | Accepted and waiting for orange mail | Local means orange was already consumed and asks for blue; the journal also reports orange complete. |
| 4 | Orange rejected and waiting for blue mail | Local means blue was already consumed and asks for brown. |
| 5 | Blue rejected and waiting for plain brown mail | Local consumes brown, writes 5, then queues an unconditional completion. Reconnect re-talk queues again, but current semantics require the brown item here. |
| 6 | Complete | Core reward values are correct; the queue writes 6 before XP/item/completion settlement and has no internal idempotence guard. Current actors still show Another Slice's refusal rather than postquest dialogue. |

The earliest public blocker is NPC dispatch at state 0. Even using the obsolete
bartender to manufacture state 1 does not help because neither historical
general is in the world. A state injector can reach old labels, but that does
not exercise current actor routing, explicit acceptance, crates, cutscenes,
support bits, or migration.

## 5. Detailed lifecycle audit

### Current start, obsolete bartender, and dialogue topology

The quest row names both current generals (cache IDs 669 and 670), not the
historical 675/676 forms used by authored scripts. The current world spawns
`general_bentnoze_red` and `general_wartface_green`. Their only exact Talk-to
handler lives in Another Slice of H.A.M. and, at its default state, says the
generals have no business with surface-dwellers. Its source comment claiming
no other script binds the actors accurately describes the collision, not a
valid ownership model.

Create one shared current-general router. It must compose, in verified
priority, Another Slice's exact active phases, The Lost Tribe's Dorgeshuun
briefing, Goblin Diplomacy start/progress/postquest dialogue, Dragon Slayer I's
coexisting topic/menu wording, General Bentnoze's hard clue, and ordinary
conversation. An ineligible branch must return unhandled rather than replacing
every lower-priority topic with a refusal. All two-speaker chatheads must use
the current red/green types rather than historical cache models.

At state 0 reproduce the random opening arguments and their branches, including
fat/not-fat answers, goblin-century and peace topics, red/green suggestions,
the non-starting leave paths, “different colour”, and explicit Yes/No quest
confirmation. Only Yes writes state 3. If the player prepared orange mail
before starting, allow the canonical continuation into its presentation
without requiring a stale bartender checkpoint.

The Rusty Anchor's current responsibilities are beer and an easy clue. Remove
the quest-start state write and retain those owners. Its adjacent beer trade
also needs stack-aware capacity: with a full inventory and more than two coins,
deleting two does not free a slot, so the present code can charge for a beer
that `inv_add` cannot deliver.

### Knowledge flags, Aggie, and supply guidance

After acceptance, the player can ask where mail or dye comes from. Record the
two native knowledge bits only after their full dialogue completes. They may
adjust re-talk/journal hints but must never gate a player who already owns the
items. The dye answer correctly directs the player to Aggie; the armour answer
identifies the three village crates.

Aggie's base service already makes red for three redberries plus five coins,
yellow for two onions plus five coins, and blue for two woad leaves plus five
coins. Direct ingredient use works and her existing One Small Favour and Land
of the Goblins delegates must remain ahead only when truly eligible. On first
ordinary dye-service discovery, set `%gobdip_met_aggie` and expose the native
`Dyes` child operation. Bind that operation to the current Make-All quantity
surface introduced in 2025; do not make it contingent on Goblin Diplomacy
being active merely because the carrier field is named `gobdip`.

Wyson's canonical generous offer (20 coins for two leaves) is implemented, as
are the lesser offers. Revalidate capacity before charging whenever the coin
stack will survive. A full inventory must neither lose coins nor silently lose
leaves. Aggie's ingredient transactions naturally free slots, but still need
checked deletions/addition and duplicate-packet tests.

### Three armour crates, ladder, and loss policy

All three native crates exist at the helper/Wiki locations: north of the
generals' hut, in the western hut, and upstairs above the southern ladder.
Each offers Search and each has a dedicated native searched bit. No script in
the tree handles any of them. The ladder itself is correctly categorized as a
shared climb-up/climb-down pair and should not receive a quest-specific
teleport override.

Implement one shared crate transaction parameterized by source bit. The first
eligible search grants one brown mail and records only that crate; repeat
search gives the canonical empty result. A full inventory must not consume the
source entitlement. The transcript also documents reacquisition after true
loss while the current article warns that dyeing all copies does not replenish
the crates. Decode the exact drop/destruction reset policy with a live capture
or deob before choosing how a bit resets; do not equate “no brown mail in
inventory” with loss because dyeing and banking are distinct cases.

Test the three sources in every order, bank between searches, full inventory,
drop/death/despawn, dye before collecting the next crate, and imported flag/item
contradictions. Quest Helper uses these bits as acquisition evidence, so false
writes also produce incorrect route guidance.

### Goblin drops and coloured-mail interoperability

Ordinary goblins are an alternative source of tradeable brown mail. The red
and green Goblin Village variants canonically drop mail matching their worn
colour at 10/128. Local `goblin_village_drop_table` routes both forms to one
table and awards brown mail instead. Correct the per-NPC product while
preserving member/free-to-play substitutions, ordinary goblin brown-mail
drops, owner/duration rules, and the rest of the table.

Current guidance permits red or green dropped mail to be dyed for the coloured
trials, while a genuinely uncoloured brown copy is still needed at the end.
The local dye switches and reverse binds omit both red and green mail entirely.
Add them under the verified current re-dye policy and test that this correction
does not make a coloured copy count as brown at state 5.

### Dye mixing and selected-item transaction

Red plus yellow dye correctly creates orange in either click orientation;
blue and orange can transform mail in either orientation through shared
triggers. The mail proc, however, discards the clicked item's identity. It
scans the whole inventory in a fixed order (brown, black, white, yellow, blue,
orange, purple), then consumes the first colour found. Using dye on one mail
can therefore delete a different mail in another slot. This is especially
dangerous during Land of the Goblins' repeated colour cycle.

Derive the source mail from `last_item`/`last_useitem` for the actual click
orientation, validate that exact slot/item and dye still exist, and perform a
one-for-one protected transform. Do not scan for a convenient substitute.
Cover brown/red/green plus every Land of the Goblins colour, attempts to dye to
the current colour, noted items, rapid double use, inventory rearrangement,
and multiple differently coloured copies. Invalid combinations must consume
nothing and use the normal default response.

### Orange presentation and first fitting scene

At state 3, Talk-to with orange mail or using the orange mail on either general
must enter the same hand-in. Using plain, blue, or another coloured mail gives
the specific wrong-colour exchange and consumes nothing. Local content has no
use-on-general handlers; its inaccessible Talk-to arm immediately deletes
orange, writes local state 3, and only narrates two general reactions.

The current scene calls Grubfoot, has him enter the changing area, changes his
per-player form to orange, and returns him for the verdict. Use a protected
player-scoped scene or verified per-player actor mechanism; never walk,
transform, or delete the shared world generals for every player. Consume the
exact orange item once and commit canonical state 4 at a resumable boundary.
After interruption, the save must either replay from an owned item or resume
after a settled hand-in without charging another.

### Blue presentation, Grubfoot dialogue, and second fitting scene

State 4 repeats the same contract for blue mail. At the start of the scene,
Grubfoot is still shown in orange; after changing he appears blue. The verdict
requests a darker earthy brown. Local content consumes blue and writes local
state 4 before a few dialogue lines, without Grubfoot or support state.

After this scene, talking to blue Grubfoot has the optional “makes you feel
blue” dialogue. The spawned `catwalk_goblin` shell has the correct blue child
but no Talk-to handler at any value. Add a shared Grubfoot router that serves
this exact Goblin Diplomacy phase and delegates Another Slice/Land of the
Goblins or ordinary dialogue elsewhere. Commit canonical state 5 and retain a
recoverable blue presentation state across logout/reconnect.

### Brown presentation, completion scene, and visual reset

At state 5, only ordinary `goblin_armour` is correct. Every painted form used
on a general is rejected without consumption. The third fitting returns
Grubfoot to brown and the generals choose their original colour. Local code
deletes brown, writes state 5, and queues completion without any scene. This
local use of 5 directly conflicts with current “waiting for brown” semantics.

Run the fitting and final dialogue through the same protected scene owner.
Restore Grubfoot to the correct postquest form even if the player disconnects,
dies, or an admin completes the quest. Because the mail is ordinary tradeable
content, consume exactly the presented copy and do not purge spare/banked
copies or other colours.

### Completion, reward recovery, and idempotence

The local reward values are correct: raw Crafting XP 2,000, a gold bar, and the
dbrow's five QP through `quest_complete_rewards`. Brown-mail removal normally
creates a slot for the bar. The queue nevertheless writes state 6 before XP,
bar, and completion bookkeeping and has no exact-state guard. A server failure
after the state write can permanently lose rewards; duplicate queued calls can
repeat them.

Create a guarded settlement boundary after the final scene. Revalidate the
accepted brown hand-in, grant 200 Crafting XP, the gold bar, five QP, completion
count/scroll, and state 6 exactly once. Define a capacity-safe outstanding bar
path even though the normal transaction frees a slot. Without a native reward
bit, use a versioned settlement record or a provably atomic modern quest
transaction rather than overloading crate/knowledge fields.

Admin completion should continue to write state 6 without economic rewards.
Run non-economic reconciliation afterward so current general/Grubfoot routing
and direct prerequisite checks agree. Never infer an unprovable historic gold
bar or XP grant from a completed legacy save.

### Journal, shared general consumers, and downstream gates

The dbrow journal dispatcher is correct and the journal is unusually detailed,
but every state is based on the obsolete route. State 0 points to the bartender;
state 3 says orange was already rejected; state 4 says blue was rejected; and
state 5 says brown was handed in. Rebuild it for 0/3/4/5/6, optional knowledge,
crate acquisition, alternate drops, dye sources, wrong-colour recovery, and
settlement without exposing irrelevant legacy values.

The Lost Tribe correctly requires `%gobdip_main = 6` before its start, but its
later general briefing is only delegated from the unspawned historical actor
handlers. Move it into the current shared router. Recipe for Disaster's frozen
goblin subquest checks only that the RFD introduction is complete and can be
started without Goblin Diplomacy; enforce the direct prerequisite in the RFD
owner and its journal/inspection gate.

Preserve Another Slice's current active general phases without letting its
default state consume all ordinary talks. Restore General Bentnoze's hard-clue
puzzle-box route, and verify Dragon Slayer I's concurrent menu wording. Land
of the Goblins must retain Grubfoot/Aggie ownership and repeated mail re-dyeing.
These are router/shared-service tests, not quest-local duplicate triggers.

## 6. Modernization work packages

### Package 1 — migrate state and establish current actor ownership

- Add a versioned one-time mapping from local 1–5 meanings to native current
  3–6 semantics, with explicit settlement handling for legacy state 5.
- Replace historical general binds/chatheads with one current red/green shared
  router.
- Compose Goblin Diplomacy, Lost Tribe, Another Slice, Dragon Slayer I,
  Bentnoze's clue, and ordinary dialogue through eligibility-aware delegation.
- Remove the bartender state write while preserving beer and clue behavior.
- Rebuild journal/admin/login reconciliation against the current state model.

### Package 2 — restore acquisition and dye services

- Implement all three native crate bits with source-specific, capacity-safe
  Search and verified loss behavior; keep ladder travel in its shared owner.
- Correct red/green village-goblin mail drops and all supported dye inputs.
- Make mail dyeing consume the exact clicked slot/item in either orientation;
  regression-test Land of the Goblins' full colour loop.
- Enable Aggie's met/Dyes form and current Make-All surface without tying the
  permanent service to quest progress.
- Fix capacity settlement for Wyson leaves and the bartender's adjacent trade.

### Package 3 — implement dialogue and three player-safe fitting scenes

- Reproduce state-0 random dialogue, optional lore, red/green dead ends,
  explicit Yes/No acceptance, and knowledge flags.
- Add Talk-to and use-on hand-ins plus wrong-colour responses on both generals.
- Build orange, blue, and brown protected scenes using current actors,
  changing-area movement, Grubfoot forms, exact consumption, and resumable
  state commits.
- Add stage-aware Grubfoot dialogue and postquest visual reconciliation.

### Package 4 — settle once and close all consumers

- Atomically grant 200 Crafting XP, one gold bar, five QP, completion count,
  scroll, and state 6 with duplicate/reconnect recovery.
- Preserve state-only admin completion and reconcile presentation/unlocks
  without economic grants.
- Enforce Goblin Diplomacy on the RFD goblin-generals subquest and retest The
  Lost Tribe's already-correct start gate plus its current-general topic.
- Add cross-quest router tests for Another Slice, Dragon Slayer I, Land of the
  Goblins, and Bentnoze's clue.

## 7. Verification matrix

Automated transition coverage must include at least:

| Scenario | Required assertion |
| --- | --- |
| State-0 talk to either current general | Full current menu appears; non-accept paths remain 0; Yes writes 3 |
| Rusty Anchor talk before quest | Beer/clue behavior only; no Goblin Diplomacy state write |
| Legacy states 1–5 | Each migrates once to the correct current stage; no repeated item/reward consumption |
| Three crates in every order | Each grants once, sets only its bit, and repeat/full-inventory/loss behavior matches current policy |
| Upstairs route | Shared ladder reaches both canonical endpoints without quest-specific override |
| Village red/green goblin drops | Matching coloured mail at canonical rate; ordinary goblins still supply brown |
| Dye source click with multiple mail colours | Exact clicked mail transforms in both orientations; no other slot changes |
| Orange/blue dye and red/green mail | Supported current matrix works; invalid/same-colour cases consume nothing |
| Aggie first/repeat service | Met bit selects Dyes child, direct use and Make-All charge exact ingredients/coins, shared quest topics survive |
| Wyson with full inventory and surviving coin stack | No charge without deliverable leaf capacity |
| Wrong mail used at states 3/4/5 | Correct dialogue, no consumption, no state/support change |
| Orange fitting interrupted at each boundary | At most one item consumed; state 4 and Grubfoot orange agree or a deterministic resume exists |
| Blue fitting and Grubfoot talk | State 5, orange-to-blue scene, and optional blue dialogue are per-player and persistent |
| Two simultaneous fitting scenes | No general/Grubfoot movement, colour, item, or dialogue crosses players |
| Brown fitting double-click/reconnect | Brown consumed once; state/reward settle once; Grubfoot returns to canonical form |
| Completion failure injection | XP, gold bar, five QP, count, scroll, and state are each exactly-once or explicitly outstanding |
| Admin completion | Grants no XP/bar/QP side effects; current actors and prerequisite consumers reconcile to complete |
| Lost Tribe and RFD gates | State 5 denies; state 6 permits; Lost Tribe topic and RFD inspection remain reachable through their owners |
| Shared general priority | Another Slice active phases win only when eligible; Dragon Slayer topic, clue, Goblin Diplomacy, and ordinary talk remain reachable |
| Land of the Goblins dye cycle | White/yellow/blue/orange/purple/black transitions consume the selected copy and do not regress |
| Journal values 0/3/4/5/6 | Correct start/current mail/recovery/completion guidance; no bartender-era stage claims |

Manual parity review must replay the pinned article, quick guide, and full
transcript starting from each general, with pre-collected supplies, every
optional menu, wrong-colour item use, all three fitting scenes, Grubfoot's blue
dialogue, inventory-full, bank/drop/death, logout/reconnect, and two concurrent
players. Inspect the actual current spawned NPC and loc IDs, not historical
cache entries that can be invoked only through debug scaffolding.

## 8. Prioritized findings

### P0 — route, migration, or economic-integrity failures

1. Goblin Diplomacy binds removed historical generals, while the current
   spawned red/green generals are monopolized by Another Slice's default
   refusal. The canonical quest cannot start or progress.
2. The port implements the removed bartender-era state path; current/native
   states 3–5 mean different hand-in phases. A direct alias update would rewind
   saves and duplicate item consumption.
3. All three canonical armour crates expose Search but have no handlers, and
   their native support bits are never written.
4. The shared mail-dye proc can consume a different colour/slot than the one
   clicked, corrupting both this quest and Land of the Goblins.
5. Completion writes state 6 before unguarded XP/bar/QP settlement, leaving
   crash-loss and duplicate-queue risk.

### P1 — major current mechanics, recovery, and integrations

- The three Grubfoot fitting cutscenes, per-player colour states, current
  chatheads, use-on-general hand-ins, wrong-colour responses, and blue Grubfoot
  dialogue are absent.
- State-0 acceptance lacks the current random arguments, red/green branches,
  explicit confirmation, optional lore, and knowledge-state writes.
- Red/green village goblins drop the wrong brown mail and their coloured mail
  cannot be dyed locally.
- Aggie's native Dyes form and current Make-All service are absent; Wyson and
  bartender transactions can charge through a full-inventory delivery failure.
- The Lost Tribe's general topic is attached to unreachable actors, RFD omits
  its direct prerequisite, and Bentnoze's hard clue is deferred.
- Grubfoot and general routing across Another Slice, Land of the Goblins, and
  Dragon Slayer I is not compositional.

### P2 — narrative, guidance, and maintainability gaps

- The detailed journal is wholly one version out of date and reports every
  current in-progress state one phase ahead.
- Goblin infighting overheads, full optional dialogue loops, source hints,
  fitting movement, changing-curtain presentation, and postquest ordinary
  conversations are missing or compressed.
- Comments in Another Slice and the Goblin Diplomacy port encode contradictory
  ownership assumptions and will encourage future exact-trigger collisions
  until one router is made authoritative.

## 9. Evidence boundary and completion gate

This audit inspected all authored Goblin Diplomacy files, current/historical
NPC and loc definitions, native state fields, world spawns, shared ladders,
dye/Aggie/Wyson/drop owners, journal/admin adapters, direct consumers, Quest
Helper extraction, and pinned current Wiki contracts. It did not modify
gameplay and does not claim that any route passes.

Goblin Diplomacy may move from `audit-pending` only after:

- versioned migration proves every existing local state and support field has
  a safe current meaning;
- an ordinary player can accept from either current general and complete the
  public 0→3→4→5→6 route without debug commands;
- every crate, dye, fitting scene, item hand-in, reward, and recovery case in
  the matrix has automated coverage;
- two-player scenes and all shared actor topics are isolated and reachable;
- direct prerequisite consumers and the Land of the Goblins dye regression
  suite pass in their owning systems; and
- manual replay against every pinned source finds no unexplained divergence.
