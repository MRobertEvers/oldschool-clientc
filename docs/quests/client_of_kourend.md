# Client of Kourend modernization audit

Status: `audit-pending` — the native quest row, permanent primary and house
varbits, all six world NPCs, quest items, Dark Altar coordinate, journal,
completion hook, and broad interview-to-orb route exist. A normal player can
broadly reach state 7, but this is not a modern or canonically complete
implementation. Inventory-full paths can advance without granting required
items or rewards, the wrong lamp is awarded and neither lamp can be used, the
native 2/3/6 phases are unreachable, the possession finale is absent, Veos's
cache-authored travel operations have no handlers, and four of seven current
downstream quest gates are missing.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to prerequisite enforcement, both Veos
locations, travel, accept/refuse/re-talk dialogue, item loss and replacement,
the five house interviews, Dark Altar activation, the controlled-Veos finale,
completion, both lamps, Kharedst's memoirs, Kourend Castle Teleport, dependent
quests, the post-quest Copper interaction, journal text, and debug adapters. It
is an implementation specification, not completion evidence.

## 1. Authoritative references

These pinned OSRS Wiki revisions define the currently documented route,
dialogue, rewards, unlocks, and dependent-content contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Client of Kourend](https://oldschool.runescape.wiki/w/Client_of_Kourend?oldid=15263161) | 15263161, 2026-07-14 | Identity, requirements, route, rewards, changes, and required-for list |
| [Client of Kourend/Quick guide](https://oldschool.runescape.wiki/w/Client_of_Kourend/Quick_guide?oldid=15215315) | 15215315, 2026-05-22 | Exact item flow, five interviews, altar step, and return location |
| [Transcript:Client of Kourend](https://oldschool.runescape.wiki/w/Transcript%3AClient_of_Kourend?oldid=15248039) | 15248039, 2026-07-02 | Accept/refuse/re-talk, interview choices, mysterious voice, possession, reward, and loss dialogue |
| [Veos](https://oldschool.runescape.wiki/w/Veos?oldid=15289968) | 15289968, 2026-08-07 | Port Sarim/Piscarilius lifecycle, travel, item recovery, and post-quest interactions |
| [Enchanted scroll](https://oldschool.runescape.wiki/w/Enchanted_scroll?oldid=15188334) | 15188334, 2026-04-22 | Feather interaction and loss/replacement behavior |
| [Enchanted quill](https://oldschool.runescape.wiki/w/Enchanted_quill?oldid=15188335) | 15188335, 2026-04-22 | Interview requirement and quest-item lifecycle |
| [Mysterious orb](https://oldschool.runescape.wiki/w/Mysterious_orb?oldid=8594411) | 8594411, 2019-02-07 | Activation location and destruction |
| [Antique lamp (Client of Kourend)](https://oldschool.runescape.wiki/w/Antique_lamp_%28Client_of_Kourend%29?oldid=15188333) | 15188333, 2026-04-22 | Two 500-XP lamps, any-skill selection, destroy, and recovery contract |
| [Kharedst's memoirs](https://oldschool.runescape.wiki/w/Kharedst%27s_memoirs?oldid=15300116) | 15300116, 2026-08-14 | Base reward, page/charge lifecycle, teleports, recharge, destroy, and recovery |
| [X Marks the Spot](https://oldschool.runescape.wiki/w/X_Marks_the_Spot?oldid=15240941) | 15240941, 2026-06-27 | Mandatory prerequisite and Veos visibility handoff |
| [Kourend Castle Teleport](https://oldschool.runescape.wiki/w/Kourend_Castle_Teleport?oldid=14918403) | 14918403, 2025-06-12 | Durable spell unlock now awarded by the quest |
| [Dark Altar](https://oldschool.runescape.wiki/w/Dark_Altar?oldid=15241930) | 15241930, 2026-06-29 | Orb destination and memoir recharge ecosystem |
| [Architectural Alliance](https://oldschool.runescape.wiki/w/Architectural_Alliance?oldid=15221147) | 15221147, 2026-05-29 | Historical Kourend favour reward context; not a current reward |
| [Veos' Client](https://oldschool.runescape.wiki/w/Veos%27_Client?oldid=15122510) | 15122510, 2026-02-06 | Current identity/lore boundary for the possessed speaker |
| [Copper's crimson collar](https://oldschool.runescape.wiki/w/Copper%27s_crimson_collar?oldid=15250585) | 15250585, 2026-07-03 | Optional post-quest wolf-bones/collar sequence |
| [Leenz](https://oldschool.runescape.wiki/w/Leenz?oldid=14992569) | 14992569, 2025-09-25 | Piscarilius interview owner |
| [Horace](https://oldschool.runescape.wiki/w/Horace?oldid=14992435) | 14992435, 2025-09-25 | Hosidius interview owner |
| [Jennifer (Shayzien)](https://oldschool.runescape.wiki/w/Jennifer_%28Shayzien%29?oldid=15239703) | 15239703, 2026-06-25 | Shayzien interview owner |
| [Munty](https://oldschool.runescape.wiki/w/Munty?oldid=14994364) | 14994364, 2025-09-26 | Lovakengj interview owner |
| [Regath](https://oldschool.runescape.wiki/w/Regath?oldid=14992513) | 14992513, 2025-09-25 | Arceuus interview and Dark Altar reaction owner |
| [Dragon Slayer II](https://oldschool.runescape.wiki/w/Dragon_Slayer_II?oldid=15303675) | 15303675, 2026-08-17 | Downstream prerequisite |
| [Tale of the Righteous](https://oldschool.runescape.wiki/w/Tale_of_the_Righteous?oldid=15272074) | 15272074, 2026-07-22 | Downstream prerequisite and memoir page |
| [The Ascent of Arceuus](https://oldschool.runescape.wiki/w/The_Ascent_of_Arceuus?oldid=15241054) | 15241054, 2026-06-27 | Downstream prerequisite and memoir page |
| [The Depths of Despair](https://oldschool.runescape.wiki/w/The_Depths_of_Despair?oldid=15241049) | 15241049, 2026-06-27 | Downstream prerequisite and memoir page |
| [The Forsaken Tower](https://oldschool.runescape.wiki/w/The_Forsaken_Tower?oldid=15270136) | 15270136, 2026-07-20 | Downstream prerequisite and memoir page |
| [The Queen of Thieves](https://oldschool.runescape.wiki/w/The_Queen_of_Thieves?oldid=15241048) | 15241048, 2026-06-27 | Downstream prerequisite and memoir page |
| [A Kingdom Divided](https://oldschool.runescape.wiki/w/A_Kingdom_Divided?oldid=15300076) | 15300076, 2026-08-14 | Downstream prerequisite and Book of the Dead upgrade |

The revisions were resolved through the OSRS Wiki API on 2026-08-17. They
identify Client of Kourend as a members, novice, short quest released 20 April
2017 and the second quest in the Great Kourend series. Its current prerequisite
is X Marks the Spot. A normal feather is required; the polar, woodland, jungle,
desert, eagle, and stripy variants also work, while the magic gold feather does
not.

The current reward contract is exactly 1 quest point, two quest-specific
antique lamps worth 500 XP each in any skill, Kharedst's memoirs, and the
Kourend Castle Teleport spell unlock. The 10 January 2024 update removed the
Kourend favour certificate and replaced that reward with the teleport unlock.
Modernization must not restore the obsolete certificate or infer a reward from
the unused `%veos_housereward` field.

Transition aid only: the local Quest Helper checkout's
[`ClientOfKourend.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/clientofkourend/ClientOfKourend.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms steps 0–6,
the five independent interview varbits, accepted feather variants, altar zone,
prerequisite, two lamps, memoirs, and spell unlock.
`python3 tools/questhelper_extract.py clientofkourend --check` exits 0. That
proves symbol extraction only; it does not prove inventory safety, operation
dispatch, item usability, travel ownership, or phase reachability.

## 2. Native quest identity and player contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache quest ID | 142 |
| Type | Members quest; Great Kourend #2 |
| Difficulty / length | Cache 0 / 1; Wiki novice / short |
| Release date | 20 April 2017 |
| Start | Veos on the Port Piscarilius docks; Port Sarim Veos provides passage after X Marks the Spot |
| Requirements | X Marks the Spot; one accepted feather variant; no skill or combat requirement |
| Primary state | `%veos_progress`, bits 0–5 of transmitted permanent `veos_quest` |
| Route side state | Five one-bit house interview fields on `veos_quest` |
| Other native state | `%veos_reveal`, `%veos_housereward`, and `%coppers_collar` |
| Quest points | 1 |
| Item rewards | Two `veos_lamp`; `veos_kharedsts_memoirs` |
| Unlocks | Kourend Castle Teleport; dependent quests; continued Veos travel/recovery services |
| Combat | None |
| End state | 7 |

The native `quest_clientofkourend` dbrow has the correct members flag,
difficulty, length, location, release, series, start coordinate/NPC, quest
point, and end state. It omits the current X Marks the Spot prerequisite, the
feather requirement, both lamps, memoirs, XP, and unlock metadata. Its activity
adviser reason is correctly “Gain Kharedst's Memoirs.” Gate A should align the
dbrow with the current contract without making UI metadata a substitute for
server-side start checks.

### Primary state inventory

| State | Canonical phase | Current use / mismatch |
| ---: | --- | --- |
| 0 | Not started; travel to Piscarilius and accept/refuse Veos's request | Prerequisite check and choice exist; Port Sarim's client choice teleports and opens start dialogue immediately |
| 1 | Make the enchanted quill and interview all five storekeepers | Production state; interviews are compressed and require only the quill, not scroll plus quill |
| 2 | Return to Veos after all interviews | Declared, journaled, and handled; never written |
| 3 | Veos removes the writing tools and offers the final task | Declared, journaled, and handled identically to 2; never written |
| 4 | Carry/replace the mysterious orb and activate it near the Dark Altar | Production state; grant/replacement can advance or claim success with no inventory space |
| 5 | Return to Veos; initial finale/possession phase | Production state; one short exchange immediately completes |
| 6 | Controlled-Veos/client dialogue and final completion checkpoint | Declared, journaled, and handled identically to 5; never written |
| 7 | Complete | Written before reward grants, so failed grants become permanent losses |

Exact ownership of writes 2, 3, and 6 should be confirmed during implementation
against the transcript and, where ambiguity remains, live var capture. The
states are not permission to invent extra gameplay, but they are strong native
evidence that the current 1→4 and 5→7 collapses are incomplete. The modern
route should preserve resume points across logout rather than putting the
entire handoff or finale inside one fragile dialogue queue.

### Side-state inventory

| Field | Native meaning / likely ownership | Current behavior |
| --- | --- | --- |
| `%veos_piscarilius` | Leenz interview complete | Correctly written once |
| `%veos_arceuus` | Regath interview complete | Correctly written once; presentation is incomplete |
| `%veos_lovakengj` | Munty interview complete | Correctly written once |
| `%veos_shayzien` | Jennifer interview complete | Correctly written once |
| `%veos_hosidius` | Horace interview complete | Correctly written once |
| `%veos_reveal` | Historical client/reveal presentation | Unused; do not revive retconned identity dialogue without current evidence |
| `%veos_housereward` | Historical multi-bit house/favour reward state | Unused; current quest no longer awards a favour certificate |
| `%coppers_collar` | Two-bit post-quest Copper/collar lifecycle | Native field exists but has no implementation |
| `%veos_sarim_vis` | X Marks/Client Port Sarim Veos wrapper | X Marks completion writes 1; Client completion writes 2 |
| `%veos_pisc_vis` | Port Piscarilius Veos wrapper | X Marks completion writes 1, exposing the Client start actor |

Normal completion of X Marks the Spot makes both required Veos wrappers
reachable. This cross-quest handoff is a production dependency and must be
covered by integration tests, not only by direct state assignment.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

The quest root contains 407 lines: 379 lines of script, 23 lines of constants,
and a five-line varp overlay.

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_clientofkourend/configs/clientofkourend.constant` | State values, route coordinates, altar range, lamp count | Broadly correct; states 2/3/6 have no production writers |
| `server/scripts/quests/quest_clientofkourend/configs/clientofkourend.varp` | Native permanent carrier overlay | Correct carrier properties; bit fields come from shared cache config |
| `server/scripts/quests/quest_clientofkourend/scripts/clientofkourend.rs2` | Start, interviews, items, altar, finale, completion, journal, and debug | Playable outline with unsafe transactions, compressed route, wrong/unusable reward, and stale deferred claims |

The file header says the Kourend Castle Teleport unlock is deferred. That is
stale: `skill_magic/scripts/spells/teleport.rs2` already refuses the Kourend
spell below `%veos_progress >= 7` and executes the shared teleport at or above
7. Preserve and test this existing durable gate rather than duplicating it in
the quest script.

### Mandatory shared and cross-directory surfaces

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow` | Native quest metadata | Missing current prerequisite, required item, reward, XP, and unlock declarations |
| `configs/all.varp`, `configs/all.varbit` | Quest, visibility, legacy, and Copper state | Native state is sufficient; no second progress variable is needed |
| `configs/all.npc` | Veos wrappers/leaves and controlled-Veos asset | World actors exist; `veos_controlled` is available but unused |
| `configs/all.obj` | Scroll, quill, orb, correct lamp, memoirs, collar | Correct assets exist; current completion grants a different quest's lamp |
| `configs/all.loc` | Old Memorial | `kourendwoodland_statue` exists with Inspect but has no recharge handler |
| `areas/world/configs/m28_57.spawn` | Piscarilius Veos | Spawned and visibility-gated |
| `areas/world/configs/m47_50.spawn` | Port Sarim Veos | Spawned and visibility-gated |
| `areas/world/configs/m28_58.spawn`, `m27_56.spawn`, `m23_56.spawn`, `m24_58.spawn`, `m26_58.spawn` | Five storekeepers | All canonical interview owners are spawned |
| `quest_xmarksthespot` | Prerequisite and Veos travel/visibility handoff | Normal completion exposes Piscarilius Veos; Talk-to delegation exists only while Client is incomplete |
| `skill_magic/scripts/spells/teleport.rs2` | Kourend Castle Teleport | Correct state-7 unlock predicate already exists |
| `general/scripts/enchanted_jewellry/kharedst_memoirs.rs2` | Reward item, pages, charges, teleports | Reminisce/Check and page consumption exist; Read, recharge, safe destroy, and full recovery lifecycle do not |
| Five Kourend house quest roots | Memoir page rewards | Ascent, Depths, and Queen grant pages; Tale and Forsaken omit their documented page item grants |
| `quest_kingdomdivided` | Memoirs → Book of the Dead upgrade | Grants Book of the Dead without consuming/upgrading memoirs or preserving native page/charge state |
| Downstream quest roots | Required-for contract | Only 3 of 7 explicitly enforce Client completion in production |
| `quests/scripts/quest_cheat.rs2` | Generic completion adapter | Writes only state 7; omits reward entitlements, visibility/travel normalization, and quest-item cleanup |
| Generic `[opheld5,_]` drop owner | Destroy fallback | Scroll, quill, orb, lamp, memoirs, and collar use cache `Destroy` text but currently fall into ordinary drop behavior |

### Production placement and visibility

All core actors are spawned at appropriate world locations:

| Actor | Spawn file | Observed coordinate |
| --- | --- | --- |
| Veos, Piscarilius | `m28_57.spawn` | 1825,3691 |
| Veos, Port Sarim | `m47_50.spawn` | 3054,3245 |
| Leenz | `m28_58.spawn` | 1807,3723 |
| Horace | `m27_56.spawn` | 1773,3588 |
| Jennifer | `m23_56.spawn` | 1519,3591 |
| Munty | `m24_58.spawn` | 1551,3749 |
| Regath | `m26_58.spawn` | 1720,3724 |

`%veos_pisc_vis = 1` resolves to the amulet-wearing visible Piscarilius leaf;
X Marks the Spot writes that value on normal completion. Port Sarim values 1
and 2 similarly select visible variants around the two quests. This means the
canonical start is reachable after a normal prerequisite run.

The quest-specific `::clientofkourend` adapter is not equivalent. It writes X
Marks complete and teleports to Piscarilius, but does not write
`%veos_pisc_vis = 1`; on a fresh or cheat-prepared account its instruction to
“Talk to Veos” can point at a hidden NPC. Repair the adapter to establish the
same visibility postconditions as real prerequisite completion, or make it
invoke an idempotent shared completion-state normalizer.

## 4. Canonical route versus current behavior

| Phase | Required behavior | Current behavior |
| --- | --- | --- |
| Passage | Port Sarim Veos sails the player to Port Piscarilius; talk to Piscarilius Veos to start | Client menu choice teleports and immediately starts dialogue; no ship presentation or second interaction |
| Start | Ask about quests; hear the client request; accept/refuse; receive scroll only when space permits | Accept/refuse exists; `inv_add` is unchecked and state 1 is written even if the scroll is not received |
| Quill | Use an accepted feather on the enchanted scroll; scroll remains and feather becomes quill | Correct accepted list and broad transaction; no richer presentation |
| Interviews | Carry both scroll and quill; ask each storekeeper about their city's contribution/hierarchy and local activities | Requires only quill; each actor has one compressed question/answer |
| Regath | Dark Altar mention makes writing tools glow and causes a painful magical reaction without HP loss | One ordinary message; no glow, pain, or reaction sequence |
| Final interview | Writing tools direct the player back to Veos through a mysterious voice | Generic “Return to Veos” message |
| Handoff | Veos takes scroll and quill, explains another task, and gives orb safely | Leaves both writing tools, grants orb unchecked, and jumps 1→4 |
| Orb loss | Veos replaces a lost orb only when inventory can receive it | Auto-adds without a space guard and still presents replacement text |
| Altar | Activate near Dark Altar; orb shatters; client voice thanks the player | Distance check, deletion, and state 5 exist; presentation is one message |
| Finale | Return specifically to Piscarilius Veos; he denies speaking since arrival; the client controls him and explains the mystery | Veos accepts the claim and completes after two lines; `veos_controlled` and state 6 are unused |
| Completion | Atomically award two usable quest lamps and memoirs, unlock spell/travel, clean quest items, and preserve recovery rights | Writes state 7 first, blindly adds memoirs and the wrong lamp, then shows completion |
| Post-quest | Veos continues travel/recovery services; optional wolf-bones/Copper sequence | Piscarilius Veos only thanks the player; direct travel ops and Copper sequence are absent |

### 4.1 Start and replacement transactions can strand the route

At acceptance, `inv_add(inv, veos_scroll, 1)` is followed unconditionally by
the state-1 write. With a full inventory the player can enter the interview
phase without receiving the required scroll. The loss branch repeats the same
unchecked add and says the item was replaced whether or not it was received.

Replacement is also conditioned on both scroll and quill being absent. If the
player has a quill but loses or banks the scroll, Veos does not replace the
scroll. Canonical interviews require both, so a corrected interview gate would
expose this existing trap immediately.

Modernize these as explicit inventory transactions:

1. calculate the exact items to remove and add;
2. account for slots freed in the same transaction;
3. refuse with canonical no-room dialogue before any state write;
4. commit item and state changes together;
5. support each legitimate loss combination without duplicating items; and
6. distinguish inventory from bank where canonical recovery rules require it.

The feather conversion already frees one slot before adding the quill and
leaves the scroll in place. Retain that shape, but route it through the same
quest-item helper and confirm both items and state immediately before commit.

### 4.2 Storekeeper ownership shadows ordinary dialogue

The quest binds exact Talk-to operations for all five storekeepers. Leenz
reproduces a small shop fallback when the quest condition is false; Horace,
Jennifer, Munty, and Regath merely say “Can I help you?” and return. Their
separate Trade operations may still open stores, but ordinary Talk-to dialogue
owned elsewhere is bypassed.

The modern implementation should have one dispatcher per actor that delegates
to the existing non-quest owner outside the exact interview condition. During
the quest it must require both scroll and quill, retain independent completion
bits for arbitrary visit order, preserve already-interviewed branches, and run
the two-stage transcript choices and actor-specific response. The transition
after the fifth bit should write state 2 and present the mysterious return
instruction exactly once.

### 4.3 States 2, 3, and 6 are unreachable

`~cok_all_houses` is consulted only when talking to Veos at state 1. That
branch gives the orb and writes state 4 directly. States 2 and 3 have handlers
and journal text but no production writer. Similarly, orb activation writes 5
and the first return conversation invokes completion directly; state 6 is
never written.

This has practical consequences beyond fidelity:

- the writing tools are never removed before the orb grant;
- two slots that should be freed cannot help receive the orb;
- a full inventory can lose the orb while still advancing to state 4;
- logout/reconnect has no stable midpoint for either handoff or finale; and
- the cache-controlled Veos actor is never used.

Restore the native ladder with idempotent boundary procedures. Each procedure
must be safe when re-entered after logout, dialogue interruption, or a partial
legacy save, and should normalize leftover quest items only where the canonical
phase proves they are obsolete.

### 4.4 Orb activation is mechanically present but under-presented

The current operation correctly limits activation to state 4 and a radius of
eight tiles around the Dark Altar, deletes the orb, and writes state 5. Retain
that server-authoritative location check. Add the shatter, magical reaction,
and mysterious voice in a protected interaction, with the state write committed
after the successful item deletion. Repeated Activate, remote activation,
logout during presentation, and duplicate-orb legacy saves need explicit tests.

## 5. Completion, rewards, and permanent unlocks

### 5.1 Completion is non-atomic

`~cok_quest_complete` currently performs these operations in unsafe order:

1. write `%veos_progress = 7`;
2. add Kharedst's memoirs;
3. add two lamps;
4. write Port Sarim visibility 2; and
5. show the shared completion interface.

If inventory grants fail, the completed state prevents the normal finale from
running again. The player can permanently lose one or all item rewards. The
modern completion transaction must either require sufficient effective space
before committing, or create durable unclaimed-reward entitlements recoverable
from the documented NPCs. It must be idempotent: replay may repair missing
entitlements and side state but may never award extra XP-bearing lamps.

### 5.2 The wrong lamp is awarded and no lamp works

The script grants `thosf_reward_lamp`, an unrelated generic antique lamp.
The cache contains the correct `veos_lamp` with Rub and Destroy operations,
but no server handler exists for it. No Rub handler exists for the currently
awarded item either. As a result, the headline 1,000 XP reward is unusable.

Implement the two `veos_lamp` rewards with the modern skill-selection
interface and exactly 500 XP per successful claim. Because lamps may be
destroyed and reclaimed from Veos or Cabin Boy Herbert, inventory presence is
not a sufficient entitlement ledger. Persist the number of unused claims (or
an equivalent exactly-once state), decrement only after XP is successfully
applied, and cap the lifetime total at two. Destroy confirmation, inventory
loss, bank checks, reclaim with no space, interface cancellation, logout, and
repeated button submission all require tests.

The Quest Helper source labels its lamp item as a placeholder; that does not
override the native cache evidence. `veos_lamp` is the quest-specific asset.

### 5.3 Kharedst's memoirs is only partially implemented

The quest grants the correct base item, but without space protection or a
recovery path. The shared memoir script supports Reminisce, Check, five page
bits, adding 20 charges per page, and available page destinations. It lacks:

- the cache-authored Read operation;
- safe Destroy confirmation and canonical recovery;
- recharge at the Old Memorial after at least one page is added;
- a handler for the existing `kourendwoodland_statue` Old Memorial loc;
- two documented page grants, from Tale of the Righteous and Forsaken Tower;
- a coherent upgrade into the Book of the Dead after A Kingdom Divided; and
- preservation/normalization rules for page and charge state during recovery
  and upgrade.

Three page sources are wired: Depths of Despair grants the Hosidius page,
Queen of Thieves grants the Piscarilius page, and Ascent of Arceuus grants the
Arceuus page. Tale of the Righteous and Forsaken Tower currently mention their
pages in reward text without adding them, leaving two of five destinations
unobtainable through the intended route.

The Client work package owns the initial memoir entitlement and Veos/Herbert
recovery. Shared memoir and downstream-quest work should be coordinated in the
same modernization series so the reward is tested end to end, not declared
complete merely because the base item enters inventory.

### 5.4 Kourend Castle Teleport already has the correct gate

The spell handler already checks `%veos_progress >= 7`. This is a durable
unlock predicate, not an inventory reward, and should remain available after
the player destroys or upgrades the memoirs. Required modernization work is to
remove the stale “deferred” comment, add completion/cheat/import integration
tests, and expose the unlock in accurate completion/dbrow metadata. Do not add
a redundant unlock bit unless live cache behavior proves one exists.

## 6. Veos travel lifecycle

The cache-authored Veos leaves expose direct Port Piscarilius, Port Sarim, and
Land's End operations, but the production scripts contain no matching
`[opnpc3]` or `[opnpc4]` handlers. Those options therefore do nothing.

While Client is incomplete, X Marks the Spot delegates Port Sarim Talk-to to
`cok_veos_sarim_gate`; its travel choice directly teleports Sarim→Piscarilius.
After Client reaches 7, that delegation condition is false. Port Sarim Talk-to
falls back to stale X Marks dialogue, Piscarilius Veos only gives a thank-you
line, and the direct travel operations remain unowned. The completion write to
`%veos_sarim_vis = 2` selects a travel-capable cache leaf whose operations have
no server behavior.

Create one shared Veos transport owner that:

- handles every operation exposed by each current wrapper leaf;
- supports Sarim↔Piscarilius and Land's End as documented for the applicable
  lifecycle;
- uses modern transport/cutscene protection and arrival validation;
- delegates quest dialogue without losing ordinary travel;
- remains available after Client completion; and
- has a safe fallback for legacy visibility values.

The ship scene is presentation work, but the unhandled direct operations and
loss of post-quest transport are functional defects. Tests must enumerate the
actual wrapper value × operation matrix before and after X Marks and Client,
rather than exercising only the Talk-to menu.

## 7. Dependent quests and post-quest content

### Downstream prerequisite matrix

| Current Wiki dependency | Production Client gate | Audit result |
| --- | --- | --- |
| Dragon Slayer II | None found at start | Missing |
| The Tale of the Righteous | Comment says the hard gate is deferred; no production check | Missing |
| The Ascent of Arceuus | Explicit `%veos_progress >= 7` | Present |
| The Depths of Despair | Explicit Client + X Marks checks | Present |
| The Forsaken Tower | None found at start | Missing |
| The Queen of Thieves | Explicit Client + X Marks checks | Present |
| A Kingdom Divided | None found at start | Missing |

Only three of seven documented dependents enforce the prerequisite. Some story
paths may imply it transitively, but explicit start predicates are still
required because imports, cheats, admin repair, and future route changes can
produce otherwise valid-looking saves. Use a shared symbolic quest-completion
predicate and give the player useful unmet-requirement dialogue; do not scatter
raw number comparisons or silently advance.

### Copper's crimson collar

The current post-quest easter egg begins by using wolf bones on Veos, reveals
the story of his wolf Copper, allows the player to obtain Copper's crimson
collar from foxes, and ends by returning it to Veos. It gives no material
reward. The cache already supplies `coppers_collar` and a two-bit
`%coppers_collar` field on `veos_quest`, but no script uses either and no
wolf-bones-on-Veos or fox-drop integration exists.

Implement this only after the core quest/reward/travel contract. Reuse the
native field, define its observed lifecycle from transcript/live capture, make
collar loss/recovery safe, and do not attach an invented reward. Current lore
has retconned older ring-of-Charos dialogue that identified the client; do not
restore that obsolete reveal while adding the still-current Copper sequence.

## 8. Journal, destroy operations, and debug adapters

The journal has a useful broad branch per primary phase, including currently
unreachable states. State 1 always tells the player to make a quill and visit
all five houses, even when the quill already exists, an item is missing, or
only one interview remains. Modern text should derive from item and house-bit
state, identify the next recoverable action, and distinguish return-to-Veos,
altar, and finale checkpoints. Completed text should list the actual permanent
unlocks without implying that physical reward possession is completion proof.

All quest items whose cache operation says Destroy currently fall through the
generic `[opheld5,_] ~dropslot` behavior unless another exact handler owns them.
That treats Destroy like an ordinary ground drop and provides neither the
canonical confirmation nor recovery guidance. Give the scroll, quill, orb,
lamp, memoirs, and collar explicit lifecycle-aware handlers. Each handler must
avoid deleting the only route-critical item unless a reliable reclaim exists.

The quest debug tools also need modernization:

- `::clientofkourend` must reproduce the real X Marks visibility handoff;
- `::cokrun` currently mutates phases directly and expects the wrong lamp, so it
  proves assignments rather than gameplay;
- the generic `::complete` adapter writes only 7 and therefore omits reward
  entitlements, Veos side state, cleanup, and recovery normalization; and
- debug cleanup must include the correct lamp and entitlement state without
  erasing unrelated player history.

Retain direct state tools for surgical repair, but add interaction-driven tests
that click the spawned actors/items/locs and verify operation routing.

## 9. Prioritized defect ledger

### P0 — reward, completion, or permanent-service failure

1. Completion writes state 7 before unchecked item grants, permanently losing
   rewards when inventory space is insufficient.
2. Completion grants `thosf_reward_lamp` instead of `veos_lamp`; neither item
   has a Rub handler, so the 1,000 XP reward cannot be claimed.
3. Cache-authored Veos travel operations have no handlers and post-completion
   dialogue delegation removes the only temporary Sarim→Piscarilius path.

### P1 — route, state, recovery, or dependency defect

1. Start, scroll replacement, orb grant, and orb replacement have unchecked
   inventory writes that can advance or report success without the item.
2. States 2, 3, and 6 have no production writer; item handoff and possession
   finale are collapsed.
3. Scroll and quill are not both required for interviews and are not removed
   before the orb grant.
4. The controlled-Veos finale, denial, client dialogue, and resumable finale
   state are absent.
5. Lamp and memoir destroy/reclaim/entitlement lifecycles are absent.
6. Four of seven current downstream quest starts omit the Client prerequisite.
7. The memoir ecosystem lacks Read, recharge, two page grants, safe recovery,
   and a coherent Book of the Dead upgrade.
8. Exact Talk-to handlers shadow four storekeepers' ordinary dialogue.
9. Quest-specific and generic debug adapters do not reproduce valid production
   postconditions.

### P2 — presentation, optional content, and maintenance debt

1. Port travel has no ship presentation.
2. Five interviews omit their two-stage choices and most actor-specific text.
3. Regath's glow/pain sequence, the final mysterious voice, and the orb's
   shatter/voice presentation are absent.
4. The Copper/collar post-quest sequence is absent despite native assets/state.
5. The script header incorrectly says the spell unlock is deferred.
6. Unused historical reveal/favour fields are undocumented migration hazards.

## 10. Modernization work packages

Implement in this order so each package leaves a testable, recoverable state.

### Package 0 — metadata and shared predicates

- Update the quest dbrow to reflect X Marks, feather, lamps, memoirs, XP, and
  spell unlock using the current schema's established conventions.
- Introduce/reuse symbolic `started` and `complete` predicates for Client and X
  Marks; keep server-side prerequisite checks authoritative.
- Document the legacy `%veos_reveal` and `%veos_housereward` policy and preserve
  unknown live values unless a migration is proven safe.

### Package 1 — Veos visibility, transport, and start

- Centralize both Veos wrappers' dialogue/travel dispatch.
- Implement every exposed travel operation and protected ship transport.
- Preserve X Marks delegation, Piscarilius second interaction, accept/refuse,
  and no-space start behavior.
- Make real completion, cheat, import, and repair paths converge on valid Veos
  visibility without duplicating rewards.

### Package 2 — quest items and five interviews

- Add a common quest-item grant/remove/reclaim transaction helper.
- Correct scroll/quill loss combinations and explicit Destroy behavior.
- Require scroll plus quill at all five interviews.
- Restore the two-stage, actor-specific dialogue and Regath reaction.
- Delegate ordinary storekeeper dialogue outside the quest branch.
- Write state 2 exactly once after the fifth independent bit and present the
  mysterious return message.

### Package 3 — Veos handoff, altar, and possessed finale

- Restore resumable 2→3→4 ownership, removing writing tools before safely
  granting the orb.
- Add safe orb replacement and explicit Destroy handling.
- Retain the authoritative Dark Altar range check and restore shatter/voice
  presentation.
- Use `veos_controlled` or the current scene-owner pattern for the transcript
  finale, with a resumable 5→6→completion boundary.

### Package 4 — atomic completion and reward claims

- Grant exactly two `veos_lamp` claims and one memoir entitlement.
- Make completion atomic/idempotent and no-space safe.
- Implement lamp skill selection, exactly 500 XP, cancellation, destroy, and
  Veos/Herbert reclaim.
- Implement memoir initial recovery and retain the existing spell predicate.
- Normalize obsolete quest items and Veos visibility only after durable reward
  rights exist.

### Package 5 — shared memoir ecosystem

- Implement Read, Old Memorial recharge, safe Destroy, and recovery.
- Wire the missing Shayzien and Lovakengj page grants.
- Define page/charge preservation across loss and the Book of the Dead upgrade.
- Test all five destinations, charge consumption, recharge prerequisites, and
  upgrade/recovery combinations.

### Package 6 — dependency and optional-content integration

- Add the four missing downstream start gates and regression-test all seven.
- Implement the native Copper/collar lifecycle and fox-drop integration after
  live/transcript state ownership is established.
- Do not restore obsolete favour-certificate or client-identity content.

### Package 7 — journal, adapters, and evidence

- Make the journal phase-, inventory-, and house-bit-aware.
- Replace state-walk smoke tests with interaction-driven route tests.
- Make debug/complete/import repair adapters invoke the same idempotent
  postcondition procedures as production where appropriate.
- Capture Gate D evidence and update this dossier only after every required
  artifact passes.

## 11. Verification matrix

### Gate A — static contract

- Quest row and journal dispatcher resolve; start NPC/coordinate and end state
  remain correct.
- Dbrow prerequisite, required item variants, rewards, XP, and unlock metadata
  match the pinned Wiki contract.
- All named NPC, loc, item, varbit, interface, and spell symbols compile.
- Trigger-owner search finds one intentional owner for each Veos/storekeeper
  operation and no accidental generic-shadow path.
- No production or debug script still grants `thosf_reward_lamp` for Client.

### Gate B — clean gameplay route

1. Complete X Marks normally and verify both Veos visibility values and travel.
2. Sail from Sarim, talk to Piscarilius Veos, refuse once, then accept.
3. Make a quill with every accepted feather variant in parameterized tests and
   reject the magic gold feather.
4. Visit the five storekeepers in several orders; require both tools and verify
   each bit writes once.
5. Confirm the fifth interview writes the canonical return phase.
6. Verify Veos removes both writing tools and grants one orb.
7. Reject remote orb activation; activate near the Dark Altar and consume it.
8. Complete the controlled-Veos finale and receive exactly two unused lamp
   claims plus one memoir entitlement.
9. Claim 500 XP from each lamp in different skills and reject a third claim.
10. Verify Kourend Castle Teleport, Veos travel, memoir operations, and all seven
    downstream start predicates after completion.

### Gate C — interruption, loss, and adversarial cases

- Full inventory at acceptance, every replacement, orb handoff, completion,
  lamp reclaim, and memoir reclaim.
- Scroll-only loss, quill-only loss, both lost, either banked, and legacy
  duplicates.
- Logout/reconnect at every primary state and during every protected dialogue,
  transport, altar presentation, finale, reward interface, and XP claim.
- Repeat clicks, simultaneous item use, interface cancellation, duplicate
  buttons, and interrupted completion.
- Destroy/drop/death/bank behavior for every quest and reward item.
- Replay completion/repair against states 0–7 and legacy saves with inconsistent
  house bits, visibility values, leftover tools/orbs, or missing rewards.
- Travel operation matrix for every visible Veos leaf before X Marks, after X
  Marks, during Client, and after Client.
- Storekeeper Talk-to and Trade behavior before, during, and after the quest.
- Each dependent quest below/at Client state 7, including transitive-prerequisite
  and cheat/import saves.

### Gate D — evidence required before `modernized`

- Compile and content-lint output for every changed quest/shared root.
- Passing targeted tests for transactions, lamps, memoirs, transport, state
  resume, visibility, prerequisites, and post-quest services.
- Interaction-driven clean-route transcript with state/item deltas.
- Inventory-full and interrupted-finalization transcripts proving no permanent
  reward loss or duplication.
- Operation ownership report for both Veos wrappers and all five storekeepers.
- Manual presentation pass for ship travel, Regath reaction, Dark Altar effect,
  controlled-Veos finale, lamp interface, and completion UI.
- Updated audit status, remaining limitations, and exact commands/results.

## 12. Exit criteria

Client of Kourend may move from `audit-pending` to `modernized` only when a
normal player can complete the pinned route without debug state writes; every
native phase and side-state transition used by the implementation is reachable
and resumable; all quest-item and reward transactions are loss-safe and
idempotent; the two 500-XP lamp claims cannot be lost or duplicated; Kharedst's
memoirs is recoverable and its five-page ecosystem works; Kourend Castle
Teleport and Veos transport remain available after completion; all seven
dependent starts enforce the prerequisite; ordinary store dialogue still
works; and Gate D evidence is recorded.

Until then, the existing route should be treated as a broad legacy outline,
not as proof that the quest or its permanent rewards are implemented.
