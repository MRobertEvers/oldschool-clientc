# In Search of Knowledge modernization audit

Status: `audit-pending` — the native miniquest dbrow, permanent state, Aimeri
multinpc, three shelves/tomes/page counters, Logosia hand-ins, dynamic journal,
completion scroll, cheat arm, and POH status adapter exist. The organic route
cannot complete: none of the five Forthos monster families drops the twelve
required pages. The entrance trigger also teleports directly to Aimeri, the
reward is the wrong and unusable lamp, a full inventory permanently loses that
lamp after state advances, and the post-quest page economy is absent. This is a
debug-completable compatibility slice, not a modern miniquest.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native state, Forthos Dungeon traversal,
Brother Aimeri, all page-drop owners, tome lifecycle, shared Logosia dialogue,
completion settlement, Lamp of knowledge, page-sale toggle, downstream Knight
of Varlamore state, journal, and admin fixtures. It is an implementation
specification, not verification evidence.

## 1. Authoritative references

Revisions were resolved through the OSRS Wiki API on 2026-08-17. The article
and guide define the route, drops, and reward. The miniquest and actor
transcripts define dialogue choices, bulk page insertion, multi-tome handoff,
post-quest toggles, and shared-NPC branches.

| Reference | Pinned revision | Audit use |
| --- | --- | --- |
| [Article](https://oldschool.runescape.wiki/w/In_Search_of_Knowledge?oldid=15292249) | 15292249, 2026-08-10 | Identity, requirements, drop rates, reward, post-quest economy |
| [Quick guide](https://oldschool.runescape.wiki/w/In_Search_of_Knowledge/Quick_guide?oldid=15292368) | 15292368, 2026-08-10 | Exact route, shelf positions, prayer advice |
| [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AIn_Search_of_Knowledge?oldid=15263416) | 15263416, 2026-07-14 | Start, page insertion, handoff, completion dialogue |
| [Brother Aimeri](https://oldschool.runescape.wiki/w/Brother_Aimeri?oldid=15135315) | 15135315, 2026-02-25 | Feeding policy and shared item interactions |
| [Brother Aimeri dialogue](https://oldschool.runescape.wiki/w/Transcript%3ABrother_Aimeri?oldid=15141618) | 15141618, 2026-03-03 | Lore choices, artifacts, tomes, silk exchange context |
| [Logosia](https://oldschool.runescape.wiki/w/Logosia?oldid=14935316) | 14935316, 2025-07-10 | Tome owner and tertiary-drop toggle |
| [Logosia dialogue](https://oldschool.runescape.wiki/w/Transcript%3ALogosia?oldid=15294573) | 15294573, 2026-08-12 | Standard dialogue, handoff, toggle, clue composition |
| [Forthos Dungeon](https://oldschool.runescape.wiki/w/Forthos_Dungeon?oldid=15300055) | 15300055, 2026-08-14 | Geography, monsters, temple systems |
| [Musty bookshelf](https://oldschool.runescape.wiki/w/Musty_bookshelf?oldid=15111987) | 15111987, 2026-01-24 | Exact tome shelves and acquisition gate |
| [Tome of the sun](https://oldschool.runescape.wiki/w/Tome_of_the_sun?oldid=15288205) | 15288205, 2026-08-05 | Read/check/destroy/reclaim and POH lifecycle |
| [Tome of the moon](https://oldschool.runescape.wiki/w/Tome_of_the_moon?oldid=15288207) | 15288207, 2026-08-05 | Read/check/destroy/reclaim and POH lifecycle |
| [Tome of the temple](https://oldschool.runescape.wiki/w/Tome_of_the_temple?oldid=15282332) | 15282332, 2026-07-30 | Read/check/destroy/reclaim and POH lifecycle |
| [Tattered sun page](https://oldschool.runescape.wiki/w/Tattered_sun_page?oldid=15189324) | 15189324, 2026-04-22 | Pre-start drops and post-quest sale |
| [Tattered moon page](https://oldschool.runescape.wiki/w/Tattered_moon_page?oldid=15189323) | 15189323, 2026-04-22 | Pre-start drops and post-quest sale |
| [Tattered temple page](https://oldschool.runescape.wiki/w/Tattered_temple_page?oldid=15189322) | 15189322, 2026-04-22 | Pre-start drops and post-quest sale |
| [Lamp of knowledge](https://oldschool.runescape.wiki/w/Lamp_of_knowledge?oldid=15189327) | 15189327, 2026-04-22 | Correct reward object, restrictions, XP |
| [Temple spider](https://oldschool.runescape.wiki/w/Temple_spider?oldid=9728159) | 9728159, 2019-06-26 | 1/30 page-drop owner |
| [Baby red dragon](https://oldschool.runescape.wiki/w/Baby_red_dragon?oldid=15199796) | 15199796, 2026-04-28 | 1/25 page-drop owner |
| [Red dragon](https://oldschool.runescape.wiki/w/Red_dragon?oldid=15240522) | 15240522, 2026-06-27 | 1/10 page-drop owner |
| [Undead Druid](https://oldschool.runescape.wiki/w/Undead_Druid?oldid=15293903) | 15293903, 2026-08-12 | 1/20 page-drop owner and magic combat |
| [Sarachnis](https://oldschool.runescape.wiki/w/Sarachnis?oldid=15291785) | 15291785, 2026-08-10 | 1/5 page-drop owner |
| [Knight of Varlamore](https://oldschool.runescape.wiki/w/Knight_of_Varlamore_%28Kourend_Castle%29?oldid=15207537) | 15207537, 2026-05-07 | Completion plus temple-relief consumer |

Transition aid only: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/insearchofknowledge)
maps states 0, 1, and 2; the shelves, actors, item sets, page counters, return
flags, and route coordinates. Its last quest-path change is `241eaec` from
2025-08-27. `python3 tools/questhelper_extract.py insearchofknowledge --check`
exits 0. Quest Helper is a transition aid, not evidence that server drops,
transactions, item interfaces, or completion are correct.

Its reward declaration uses `THOSF_REWARD_LAMP` with an inline note that item
4447 is only a display/filter placeholder. The native cache object and Wiki
identify `hosdun_miniquest_reward` instead. The port copied the helper
placeholder into server reward policy, a concrete example of why helper data
cannot override cache identity.

## 2. Canonical contract

In Search of Knowledge is a members, experienced, medium miniquest released
4 July 2019. It starts with Brother Aimeri near Sarachnis in Forthos Dungeon.
There are no quest or skill requirements; combat level 45 is recommended.
Players need five pieces of cooked fish, meat, or vegetables, and a knife or
slash weapon to pass the dungeon web. Combat equipment and protection prayers
are recommendations rather than start gates.

Canonical flow:

1. enter Forthos Dungeon through its real entrance and traverse/cut the web;
2. feed Aimeri five valid foods, one used item at a time;
3. talk to healed Aimeri and choose “Who are you?” to begin the miniquest;
4. retrieve the sun, moon, and temple tomes from their three exact shelves;
5. obtain four matching pages for each tome from Forthos monsters;
6. insert the pages, with one action able to add all still-needed matching
   pages from a stack;
7. use a completed tome on Logosia, who also accepts other completed tomes in
   the inventory during the same conversation; and
8. receive a non-bankable Lamp of knowledge and the completion scroll.

The twelve pages can drop before the miniquest starts. The three page types are
stackable, untradeable, and remain useful after completion: Logosia buys spare
pages for 1,000 coins each. Page drops continue by default until the player
asks Logosia to stop, and can later be enabled again.

The Lamp of knowledge grants exactly 10,000 XP to any current skill whose base
level is at least 40. It cannot be banked or reclaimed after destruction. A
player without an eligible skill must retain the lamp until one qualifies; the
UI must never consume it on an invalid selection.

## 3. Native identity and state

| Field | Native value |
| --- | --- |
| Cache miniquest / dbrow | 155 / `miniquest_insearchofknowledge` |
| Implementation root | `quest_insearchofknowledge`, 4 files, 328 lines |
| Start | native `hosdun_aimeri` at `(1840,9926,0)` |
| Main carrier | permanent transmitted `hosdun_status` |
| Main state | `%hosdun_knowledge_search`, bits 18–19 |
| End state / QP | 3 / 0 |
| Metadata | members; experienced; medium; Kourend; combat 45 recommended |
| Correct reward object | `hosdun_miniquest_reward` / “Lamp of knowledge” |

The cache dbrow agrees with the current Wiki on release, classification,
length, recommendation, start actor, zero prerequisites, zero quest points, and
end state. Preserve it as the metadata source.

### 3.1 Main and side state

| State/field | Width | Canonical meaning | Current use |
| --- | ---: | --- | --- |
| `%hosdun_knowledge_search` 0 | 2 bits | Not started | Correct |
| `%hosdun_knowledge_search` 1 | 2 bits | Aimeri briefed; collecting/returning tomes | Correct but all collection collapses here |
| `%hosdun_knowledge_search` 2 | 2 bits | All three tomes returned; completion dialogue recovery | Written after three separate hand-ins |
| `%hosdun_knowledge_search` 3 | 2 bits | Complete and reward settled | Written before lamp capacity/reward |
| `%hosdun_aimeri_status` 0–5 | 3 bits | Five-food heal counter and multinpc transform | Correct counter; wrong food policy |
| three `%hosdun_*_pages` 0–4 | 3 bits each | Pages inserted in each tome | Correct persistence; one page per click only |
| three `%hosdun_*_tome_returned` | 1 bit each | Durable Logosia ownership | Correct basic purpose |

The remainder of `hosdun_status` owns Olbertus, stone relief, Grubby Chest,
temple doors, chest warning, Ralos favour, and the bone burner. These are shared
Forthos systems, not spare quest bits.

Two additional native fields are relevant but unused:

- `%hosdun_miniquest_queue_flag`, bit 25 of `zeah_perm_transmit`, is a native
  persistent queue/presentation flag whose client and script meaning must be
  decompiled before assigning it; and
- `%hosdun_page_prevention`, bit 4 of `karam_dungeon_varbit`, is the named
  post-quest tertiary-page opt-out. Do not invent a parallel toggle.

The Aimeri wrapper maps status 0–4 to the injured actor, 5 to the healed actor,
and higher values to invisible. Clamp every write and include invalid 6/7
fixtures so corrupted state cannot silently remove the start NPC.

## 4. Implementation and ownership surface

| Surface | Audit result |
| --- | --- |
| `insearchofknowledge.rs2` | Aimeri, Logosia, completion, journal, destructive debug walk |
| `insearchofknowledge_locs.rs2` | Direct entrance teleport, shelves, single-page insertion |
| `insearchofknowledge.constant` | Correct 0–3 and 4/5 thresholds; debug coordinates |
| `insearchofknowledge.varp` | Correct permanent transmitted `hosdun_status` carrier |
| shared ladder/maplink | Already maps the real entrance to the dungeon's north side |
| shared web/slash scripts | Modern symbolic slash check and temporary cut transform exist |
| Forthos world spawns | Aimeri, druids, spiders, baby/adult dragons and wrappers are present |
| red-dragon drop table | Exact `red_dragon` only; no Forthos page tertiary |
| Sarachnis owner | Stub encounter; explicitly defers loot and teleports out on death |
| other four monster owners | Stats/spawns exist; no page-drop death handlers |
| `questpoints.rs2` | Shared scroll works, but completion/count helper has no receipt |
| quest list / POH adapters | Journal and 0/started/complete status are registered |
| `quest_cheat.rs2` | Sets only state 3; leaves lamp and every side field incoherent |
| quest-combat manifest | Row exists with all audit/gameval/handler/loot/test fields blank |

The root uses symbolic cache names, native varbits, the native Aimeri multinpc,
modern chat helpers, the dynamic journal, and shared completion UI. Those are
good foundations. Its old machinery is direct teleport-through-content,
hard-coded item allowlists, state-before-delivery, global/shared reward aliases,
and a debug proc masquerading as end-to-end evidence.

The root's latest path commit is `ee80695f` on 2026-08-17. The old queue's
“done” status and `isokrun OK` claim therefore describe this current incomplete
slice, not an older implementation superseded elsewhere.

## 5. Entrance, Aimeri, and start

### Real dungeon route

The cache and shared ladder system already provide a maplink from the surface
entrance to the northern dungeon entrance. The quest's named `oploc1` instead
teleports from the entrance directly to `(1840,9926,0)`, Aimeri's tile. This
skips almost the entire dungeon route and the web for which the knife/slash
weapon is required.

Remove the quest-specific transport override and let the shared maplink own the
ladder. The generic `bigweb_slashable` handler now supports an equipped slash
weapon or a knife used on the web, including animation, failure, temporary
transform, and regrowth. The root's “knife web cut deferred” comment is stale;
verify the placed Forthos web resolves to that handler rather than duplicating
it in quest code. Test both directions, movement/collision, regrowth, relog,
and the climb-up route.

Protect from Magic is not a quest action. It is recommended because the temple
library contains aggressive Undead Druids. Validate their real magic attack,
projectile, protection-prayer interaction, aggression, and safe positioning in
the shared combat owner. Do not fake the recommendation with a quest message.

### Feeding Aimeri

Canonical food is cooked fish, meat, or vegetables. Current code accepts a
short list containing invalid bread, cake, and chocolate cake, while rejecting
vegetables and most valid fish/meat. Replace the allowlist with the engine's
canonical food categories, constrained to the three accepted families. Consume
exactly `last_useitem`, only after validation, and increment 0→5 once per
successful transaction.

Partial progress must survive leaving, death, logout, and reconnect. Repeated
packets at five must not consume food. The healed multinpc must appear
immediately and remain healed before, during, and after completion.

### Dialogue and shared item use

Healed Aimeri offers “What is this place?”, “Who are you?”, “How did you get
injured?”, temple-system questions when applicable, and an exit. Choosing “Who
are you?” starts the miniquest; other branches let the player defer it. Current
Talk-to always compresses directly into that branch and commits state 1.

Aimeri also identifies all three pages, incomplete/complete tomes, the temple
key/coin and other Forthos artifacts, and exchanges spare pristine spider silk
for a Grubby key. The root's broad item-on-wrapper handler routes every object
to feeding; after state 0 it only says Aimeri was already helped. Compose the
full shared item dispatcher and give each consumed/retained item an explicit
transaction policy. Quest modernization must not break his non-quest Forthos
services.

## 6. Page drops and combat dependencies

The page loop is entirely absent from organic gameplay. `isok_soft_pages`
writes all three counters to four but is called only by `::isokrun`. No page
object is granted by a live death handler.

| Monster | Canonical page rate | Current owner defect |
| --- | ---: | --- |
| Temple spider, level 75 | 1/30 | Spawn/stats only; no death drop |
| Baby red dragon, level 48 | 1/25 | Spawn/stats only; no Forthos death drop |
| Red dragon, level 152 | 1/10 | One generic exact-type table; Forthos variants and page tertiary absent |
| Undead Druid, level 105 | 1/20 | Spawn/stats only; no drop or explicit magic combat handler |
| Sarachnis, level 318 | 1/5 | Boss stub explicitly omits all loot |

Implement one shared Forthos page-tertiary proc called from every exact death
owner after normal loot. Requirements:

- pages can roll at state 0, 1, 2, or 3;
- after state 3, `%hosdun_page_prevention` alone controls whether they roll;
- the exact per-monster rate and page-selection distribution match live OSRS;
- the drop is attributed/protected for the eligible killer, including group or
  boss ownership rules;
- it does not replace bones, hides, boss loot, Slayer credit, or other tertiary
  drops;
- inventory capacity is irrelevant until pickup because the result is ground
  loot, and its visibility/expiry follow the shared loot policy; and
- two players cannot receive, suppress, or pick up each other's protected roll
  before public transition.

Audit exact NPC variants. The Forthos map spawns `red_dragon`, `red_dragon2`,
`red_dragon3`, and `red_dragon4`; an exact trigger on only `red_dragon` does not
prove coverage. Baby dragons, druids, spiders, and Sarachnis need their normal
drop owners modernized as part of this quest's dependency work, not a quest
state shortcut.

Populate the quest-combat manifest with all five families, exact gamevals,
handlers, page tertiary, base-loot composition, prayer/weakness notes, and test
IDs. “Undead Druid” as a toughest-enemy summary is not sufficient coverage.

## 7. Tome acquisition and lifecycle

The three native shelf coordinates and outputs are correct. Shelves require
state 1+, reject a returned tome, check capacity, and preserve the native page
counter when replacing a lost tome. Those are positive behaviors.

Current duplicate detection checks inventory only. Since the tomes are
bankable, a banked copy permits another shelf copy; debug/admin paths can create
more. Define an ownership-domain helper across inventory, bank, ground/pending
delivery, and any temporary container. One logical tome of each kind may exist
until Logosia owns it. Full inventory must not set ownership or state.

Each cache object exposes Read, Check, and Destroy. No Read or Check handler is
present. Implement the cache-authored book interface/text, display filled and
missing sections according to its 0–4 counter, and make Check report exact
progress. Destroy must use the canonical warning and preserve the page counter;
the correct dungeon shelf then replaces it. Once fully restored, the POH
bookcase also supplies a replacement. No POH integration currently exists.

Page insertion currently consumes one page and increments once. The transcript
has distinct one-page, final-page, and all-remaining-pages outcomes. On using a
matching page stack, atomically consume `min(held quantity, 4 - counter)` and
advance by the same amount. Invalid page/tome pairings consume nothing. Bind
the expected item-on-item direction(s), preserve progress after loss/reclaim,
and reject duplicate/repeated packets once the counter reaches four.

The counters are account state, not per-copy item metadata. That makes strict
duplicate prevention essential: two copies would both appear repaired and
could create confusing handoff behavior.

## 8. Logosia, completion, and Lamp of knowledge

### Tome handoff

Using one completed tome on Logosia should show it, consume it, and also offer
or automatically hand over any other completed Forthos tomes in inventory in
the same dialogue. Current code consumes only the selected tome, requiring up
to three interactions. It also accepts a duplicate even when that tome's
returned bit is already set.

Make the handoff atomic per tome and idempotent:

1. require state 1 and that exact tome's page counter at four;
2. reject an already-returned logical tome without deleting anything;
3. delete exactly one verified inventory object;
4. set its returned bit only after deletion succeeds; and
5. continue through other completed held tomes and the final reward dialogue.

State 2 is a recovery boundary inside the final interaction. If dialogue is
interrupted after the last tome but before settlement, Talk-to Logosia must
resume the reward. It must not require the player to reconstruct any tome.

### Completion transaction

Current `isok_quest_complete` writes state 3 before checking inventory space.
If full, it returns with the miniquest permanently complete, no lamp, and no
replacement branch. It then grants `thosf_reward_lamp`, a shared generic
“Antique lamp,” instead of native `hosdun_miniquest_reward`. Neither object has
a Rub handler in the current server tree, so even the wrong delivered reward
cannot grant XP.

Use the native Lamp of knowledge and one resumable settlement:

1. prove state 2 and all three returned bits;
2. exploit the inventory slot freed by the final tome, or remain at state 2 if
   delivery genuinely cannot succeed;
3. grant exactly one `hosdun_miniquest_reward`;
4. record the reward receipt and set state 3 atomically;
5. call the shared completion scroll/count path exactly once with the correct
   lamp icon; and
6. make resumed dialogue and `::complete` idempotent.

The dbrow correctly contributes zero QP, and the shared formatter correctly
suppresses a “0 Quest Points” line. Verify separately whether miniquests belong
in `%quests_completed_count`; the shared helper currently increments it for
every row and has no settlement receipt.

### Lamp redemption

Build or reuse a general modern XP-lamp chooser rather than a quest-specific
dialogue chain. It must mount the cache-appropriate modern interface, enumerate
all current skills, show eligibility, validate base level 40 server-side,
confirm the choice, grant 100,000 XP tenths exactly once, and consume the exact
lamp only after XP succeeds. Cancellation, invalid component packets, logout,
death, repeated clicks, and having no eligible skill retain the lamp. Enforce
its non-bankable and unreclaimable policies and include current skills such as
Sailing rather than freezing a pre-Sailing list.

## 9. Post-quest economy and shared actors

### Spare pages

After completion, all three page types sell to Logosia for 1,000 coins each.
Current item-on-Logosia supports tomes only. Add exact page handling with an
explicit one/all quantity policy matching the live interaction, overflow-safe
`quantity * 1000`, atomic page deletion and coin delivery, and no sale before
state 3. Stack replacement by coins should remain safe at a full inventory.

Post-quest Talk-to offers stop/start choices for tertiary pages. Drive the
native `%hosdun_page_prevention` bit and make the message, choice, drop proc,
relog, and reconnect agree. Stopping future drops must not delete held pages or
prevent their sale.

### Logosia composition

Logosia is a shared Arceuus actor with her “Shhhh!” dialogue tree and a master
Treasure Trail branch. The quest root is her only named Talk-to/item owner and
currently replaces standard dialogue with a one-line welcome/thanks. Merge
miniquest handoff, post-quest toggle, page sale, standard dialogue, and clue
behavior into one precedence-tested owner. Do not create competing exact
triggers.

### Aimeri and Knight of Varlamore

Aimeri's artifact identification and pristine-silk exchange remain available
around the miniquest, subject to their own Forthos state. Test them before,
during, and after completion.

Completion plus using the temple coin on the Stone Relief lets the player
inform the Knight of Varlamore at Kourend Castle. Native
`varlamore_sun_knight` then hides the castle wrapper and shows the dungeon
wrapper. Both spawns/transforms exist, but no script writes this field or owns
the relief/coin interaction. Implement and test the conjunction; neither
miniquest completion alone nor relief state alone should move him.

## 10. Journal, admin, provenance, and recovery

The dynamic journal is registered but renders only state 0, 1, 2, and complete.
At state 1 it cannot distinguish partial feeding history already completed,
which tome is missing/banked, 0–4 page counters, repaired tomes, or returned
tomes. Render actionable objectives from native side state and exact ownership
domains. State 2 must explicitly say the reward dialogue needs resuming.

The generated POH status adapter correctly maps 0/started/3. That is separate
from the missing tome bookcase integration.

The quest cheat writes only state 3. It leaves Aimeri injured, return flags
zero, page toggle undefined, and no lamp/receipt. The debug reset/walk is worse:
it directly writes all page counters and return bits, ignores failed `inv_add`,
and deletes every `thosf_reward_lamp` in inventory—even generic lamps belonging
to other quests. Remove destructive shared-item cleanup and replace the walk
with hermetic fixtures plus assertions. A debug command that bypasses combat
drops cannot be called an organic headless test.

`QUESTHELPER_CONTENT_PORT_QUEUE.md` labels this slice done, cites `isokrun OK`,
and explicitly lists page drops, web traversal, Protect from Magic, and lamp Rub
as deferred. Update that provenance when implementation begins. The current
generic web already exists, while the other deferrals are critical path or
reward failures.

Required recovery matrix:

| Boundary | Resume invariant |
| --- | --- |
| Entrance/web | Shared maplink and web transform preserve the real route |
| Feeding 0–5 | Exactly one valid used food increments once; invalid food remains |
| Healed/not started | Lore choices remain available without forced commit |
| Page drops | Pre-start and post-complete rolls respect owner/rate/toggle |
| Shelves | One logical tome per domain; full inventory changes nothing |
| Page insertion 0–4 | Stack consumption equals counter delta and survives tome loss |
| Tome Destroy/reclaim | Counters persist; shelf/POH source follows eligibility |
| Handoff | Each returned bit proves one successful deletion; duplicates remain |
| State 2 | Logosia resumes final reward without tomes |
| Completion | Correct lamp, count, scroll, and state settle once |
| Lamp Rub | Invalid/cancelled selection retains lamp; valid level-40 skill gets 10,000 XP once |
| Spare-page sale | Exact pages and coins exchange atomically at state 3 |
| Drop toggle | Prevention bit persists and affects only future page rolls |
| Shared actors | Logosia, Aimeri, clue, silk, relief, and Knight branches retain precedence |

## 11. Modernization work packages

### P0 — state, route, and ownership

- Preserve the dbrow and `hosdun_status`; expose the two native ancillary bits.
- Remove the direct entrance teleport and verify shared ladder/web ownership.
- Build precedence matrices for Aimeri, Logosia, five death owners, and Knight.
- Replace stale queue claims with the audited implementation surface.

### P1 — Aimeri and tome lifecycle

- Use canonical cooked fish/meat/vegetable categories and exact transactions.
- Restore the full dialogue/item dispatcher without breaking Forthos services.
- Add domain-wide tome uniqueness, Read/Check/Destroy, shelf and POH recovery.
- Support atomic all-needed page insertion and precise progress messages.

### P2 — Forthos page tertiary

- Compose normal drops for spiders, baby/adult dragons, druids, and Sarachnis.
- Add exact pre-start page rates, selection, ownership, expiry, and opt-out.
- Verify Undead Druid magic/prayer behavior and all exact NPC variants.
- Populate combat-manifest sources, handlers, loot contract, and tests.

### P3 — Logosia and post-quest economy

- Implement multi-tome handoff and state-2 resume.
- Add 1,000-coin spare-page exchanges and native drop stop/start toggle.
- Restore standard “Shhhh!” and Treasure Trail composition.
- Implement the relief plus completion Knight transition.

### P4 — completion and lamp

- Grant the native Lamp of knowledge through an idempotent settlement receipt.
- Add a reusable modern XP-lamp chooser with level-40 and 10,000-XP policy.
- Enforce non-bankable/unreclaimable behavior and all interruption cases.
- Verify miniquest completed-count semantics and completion presentation.

### P5 — journal, fixtures, and verification

- Render page/tome/return/reward recovery from native state.
- Replace `isokrun` shortcuts and shared-item deletion with hermetic fixtures.
- Add organic transition, drop-distribution, concurrency, loss, lamp, economy,
  shared-owner, and downstream integration tests.

## 12. Gate D verification matrix

| Gate D evidence | Current result | Required pass condition |
| --- | --- | --- |
| Static quest audit | This dossier; combat row blank | Every root/external owner and native field recorded; no undisclosed shortcut |
| Quest Helper extraction | Pass on 2026-08-17 | Continue passing against pinned helper tree |
| `torirsserver-scripts` / pack check | Not run for docs-only audit | Clean build and `ToriRSServer_Pack --check-only` after code changes |
| Organic 0→3 route | Impossible | Pages acquired only from live monster deaths; real route to scroll |
| Page-drop tests | Absent | Rates/selection/owner/toggle tested for every exact NPC family |
| Item/recovery tests | Absent | Every shelf, counter, loss, bank, handoff, and state-2 row automated |
| Lamp tests | Fails by inspection | Correct native lamp grants 10,000 XP once to valid level-40+ skill |
| Concurrency tests | Absent | Two killers cannot suppress, steal, or cross-credit protected rolls |
| Shared-owner tests | Absent | Aimeri, Logosia, clues, silk, relief, and Knight branches compose |
| Real-client smoke | Absent | Entrance through reward, Rub UI, page sale/toggle, and Knight captured |
| `::complete` twice | Incoherent fixture | First creates settled state; second is a no-op |

Statistical drop tests should prove the exact roll contract deterministically
through seeded/table-level tests; a short live sample cannot establish a 1/30
rate. The client smoke proves routing and presentation, not probabilities.

## 13. Exit criteria

Do not mark this miniquest `verified-modern` until:

- the surface ladder and web lead through the real Forthos route;
- Aimeri accepts exactly five valid foods and retains all shared dialogue;
- all five monster families supply protected page drops at canonical rates
  before start and after completion unless explicitly disabled;
- normal monster/boss loot and combat remain intact around the tertiary;
- each tome is unique across ownership domains and fully supports
  Read/Check/Destroy/shelf/POH recovery;
- page stacks repair tomes atomically and every counter survives loss/relog;
- Logosia accepts all completed held tomes and state 2 safely resumes;
- completion grants the correct Lamp of knowledge and settles exactly once;
- Rub grants 10,000 XP only to a base-level-40+ chosen skill and handles every
  cancel/invalid/interruption path;
- spare-page sale and the native drop toggle work after completion;
- standard Aimeri/Logosia, Treasure Trail, relief, and Knight integrations pass;
- journal, debug fixtures, combat manifest, build, pack check, automated suite,
  and real-client captures satisfy Gate D; and
- this dossier records final commands, captures, revision pins, and any precise
  non-critical deviations.

This audit intentionally makes no gameplay changes.
