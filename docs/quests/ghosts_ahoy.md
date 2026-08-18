# Ghosts Ahoy modernization audit

Status: `audit-pending` — the native quest row, primary state, support fields,
actors, scenery, quest items, journal dispatch, completion adapter, and admin
adapter exist. The implementation is not completable through its intended
route. Velorina never advances the main state after the first Necrovarus
conversation; the Port Phasmatys barrier cannot be used to leave and its
post-quest child has no handler; the shipwreck route has no working gangplank
or rock jumps; a returning player can be stranded on Dragontooth Island; and
the ectophial reward has no teleport, empty, refill, or recovery implementation.
Several native states and support values have also been reinterpreted, so old
saves require reconciliation before corrected handlers are exposed.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to every Ghosts Ahoy state, dialogue,
route, puzzle, transaction, combat encounter, reward, recovery path, travel
integration, journal/admin adapter, and downstream consumer. It is an
implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, puzzle, economy, recovery, and unlock contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Ghosts Ahoy](https://oldschool.runescape.wiki/w/Ghosts_Ahoy?oldid=15297463) | 15297463, 2026-08-13 | Identity, requirements, route, supplies, mechanics, rewards, and unlocks |
| [Ghosts Ahoy/Quick guide](https://oldschool.runescape.wiki/w/Ghosts_Ahoy/Quick_guide?oldid=15297469) | 15297469, 2026-08-13 | Ordered route, skill checks, costs, items, and recovery |
| [Transcript:Ghosts Ahoy](https://oldschool.runescape.wiki/w/Transcript%3AGhosts_Ahoy?oldid=15297814) | 15297814, 2026-08-13 | Acceptance, repeat dialogue, item hand-ins, petition, command, and completion |
| [Energy Barrier (Port Phasmatys)](https://oldschool.runescape.wiki/w/Energy_Barrier_%28Port_Phasmatys%29?oldid=15134513) | 15134513, 2026-02-25 | Two-token entry, free exit, ghostspeak dialogue, and free post-quest passage |
| [Transcript:Ghost guard](https://oldschool.runescape.wiki/w/Transcript%3AGhost_guard?oldid=14328656) | 14328656, 2022-09-27 | Barrier choices and pre/post-quest dialogue |
| [Ecto-token](https://oldschool.runescape.wiki/w/Ecto-token?oldid=15185729) | 15185729, 2026-04-22 | Five-token worship payout, 1,000-token disciple capacity, tolls, and costs |
| [Ectophial](https://oldschool.runescape.wiki/w/Ectophial?oldid=15195604) | 15195604, 2026-04-24 | Teleport, automatic refill, empty form, Wilderness restriction, duplicates, and recovery |
| [Nettle tea](https://oldschool.runescape.wiki/w/Nettle_tea?oldid=15218650) | 15218650, 2026-05-28 | Gloves, level-20 Cooking, burn chance, 52 XP, milk, cup, and heat-source flow |
| [Old crone](https://oldschool.runescape.wiki/w/Old_crone?oldid=15063835) | 15063835, 2025-11-24 | Tea sequence, model ship, ritual hand-ins, and recovery dialogue |
| [Model ship](https://oldschool.runescape.wiki/w/Model_ship?oldid=15185508) | 15185508, 2026-04-22 | Repair, random three-part colours, painting, wind, and chest-key puzzle |
| [Chest key (Ghosts Ahoy)](https://oldschool.runescape.wiki/w/Chest_key_%28Ghosts_Ahoy%29?oldid=15187116) | 15187116, 2026-04-22 | Correctly painted ship exchange and loss replacement |
| [Ghost captain](https://oldschool.runescape.wiki/w/Ghost_captain?oldid=14768954) | 14768954, 2024-10-13 | 25-token fare, return travel, permanent fee, and reduced fare |
| [Dragontooth Island](https://oldschool.runescape.wiki/w/Dragontooth_Island?oldid=15297465) | 15297465, 2026-08-13 | Map route, book location, repeat access, and island geography |
| [Giant lobster](https://oldschool.runescape.wiki/w/Giant_lobster?oldid=15272822) | 15272822, 2026-07-22 | Level, spawn, ownership, despawn, kill, and hull-chest scrap |
| [Book of haricanto](https://oldschool.runescape.wiki/w/Book_of_haricanto?oldid=15293864) | 15293864, 2026-08-12 | Dig reward, use in ritual, loss/recovery, and later uses |
| [Rune-Draw](https://oldschool.runescape.wiki/w/Rune-Draw?oldid=14666162) | 14666162, 2024-05-21 | 25-coin games, rune values, death-rune loss, draw/hold, and Robin's debt |
| [Ak-Haranu](https://oldschool.runescape.wiki/w/Ak-Haranu?oldid=15270908) | 15270908, 2026-07-21 | Oak longbow request, signed bow exchange, and manual replacement |
| [Translation manual](https://oldschool.runescape.wiki/w/Translation_manual?oldid=15185197) | 15185197, 2026-04-22 | Ritual hand-in, loss, and unlimited replacement after the trade |
| [Gravingas](https://oldschool.runescape.wiki/w/Gravingas?oldid=15289487) | 15289487, 2026-08-07 | Bedsheet, petition issue/replacement, count, and return dialogue |
| [Petition form](https://oldschool.runescape.wiki/w/Petition_form?oldid=15185196) | 15185196, 2026-04-22 | Ten signatures, Count operation, replacement reset, and presentation |
| [Ghost villager](https://oldschool.runescape.wiki/w/Ghost_villager?oldid=15003290) | 15003290, 2025-10-13 | Random support, token bribes, refusals, and consecutive-NPC restriction |
| [Bone key (Ghosts Ahoy)](https://oldschool.runescape.wiki/w/Bone_key_%28Ghosts_Ahoy%29?oldid=15185195) | 15185195, 2026-04-22 | Petition aftermath, floor placement, temple door, and loss constraints |
| [Ghost innkeeper](https://oldschool.runescape.wiki/w/Ghost_innkeeper?oldid=15079137) | 15079137, 2025-12-06 | Bedsheet issue and replacement |
| [Porcelain cup](https://oldschool.runescape.wiki/w/Porcelain_cup?oldid=15185506) | 15185506, 2026-04-22 | Old Crone issue, tea transfer, and capacity behavior |
| [Mystical robes](https://oldschool.runescape.wiki/w/Mystical_robes?oldid=15185505) | 15185505, 2026-04-22 | Coffin source, ritual hand-in, duplicate and capacity behavior |
| [Ghostspeak amulet](https://oldschool.runescape.wiki/w/Ghostspeak_amulet?oldid=15289500) | 15289500, 2026-08-07 | Dialogue requirement, ritual enchantment, and replacement dependency |
| [Morytania Diary](https://oldschool.runescape.wiki/w/Morytania_Diary?oldid=15280663) | 15280663, 2026-07-29 | Ectophial medium task and completion dependency |
| [Dragon Slayer II](https://oldschool.runescape.wiki/w/Dragon_Slayer_II?oldid=15303675) | 15303675, 2026-08-17 | Ghosts Ahoy prerequisite consumer |

These sources define a members, intermediate, medium quest released 15
February 2005. Priest in Peril and The Restless Ghost are prerequisites.
Agility 25 and Cooking 20 are boostable action requirements and do not prevent
starting the quest. The mandatory supplies include a ghostspeak amulet, milk,
silk, dyes, spade, oak longbow, knife, needle/thread or supported costume
needle, slime, nettle-tea equipment, about 400 coins, and ecto-tokens. Rewards
are two quest points, 2,400 Prayer XP, an ectophial, and free Port Phasmatys
entry.

Transition aid only: Quest Helper's
[`GhostsAhoy.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/ghostsahoy/GhostsAhoy.java)
and
[`DyeShipSteps.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/ghostsahoy/DyeShipSteps.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (the files last
changed in `c467ead24df725b60dc2dabf23ae64b335f27e8a` on 2025-10-23)
confirm native states 0–7, the support thresholds, actors, locs, route
coordinates, requirements, items, and three-colour ship puzzle. Running
`python3 tools/questhelper_extract.py ghostsahoy --check` resolves the quest
dbrow, all referenced state fields, 40-plus coordinates, 41 quest/item symbols,
11 NPCs, and 12 locs. It cannot prove server trigger reachability, randomness,
transactions, combat ownership, recovery, or downstream unlocks.

## 2. Native quest identity and state contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_ghostsahoy`; quest metadata ID 73 |
| Type / difficulty / length | Members quest / intermediate / medium |
| Release / location | 15 February 2005 / Morytania |
| Start | Velorina in Port Phasmatys; native row supplies the quest marker and coordinate |
| Primary state | `%ahoy_questvar`, bits 28–31 of permanent/transmitted native varp `ahoy_varbits_1` |
| Canonical values | 0 start; 1 Necrovarus; 2 Velorina; 3 tea/Old Crone; 4 collect and hand in robes/manual/book; 5 amulet; 6 command Necrovarus; 7 finish at Velorina; 8 complete |
| End / quest points | State 8 / 2 QP |
| Requirements | Priest in Peril and The Restless Ghost; boosted Agility 25 and Cooking 20 at their actions, not at acceptance |
| XP | 24,000 raw Prayer units = 2,400 XP |
| Item reward | Ectophial, including full-inventory delivery and post-loss replacement policy |
| Unlocks | Free Port Phasmatys barrier passage; ectophial teleport; Morytania Diary integration; prerequisite for Dragon Slayer II |

Native support fields are densely packed and must be retained rather than
replaced by parallel authored variables:

| Field | Native storage | Canonical responsibility / audit note |
| --- | --- | --- |
| `%ahoy_ectotokens_base` | bits 0–5, `ahoy_varbits_1` | Low ecto-token balance component; current economy also uses `%ahoy_ectotokens_more` |
| `%ahoy_windspeed` | bit 6 | Per-player mast wind state for revealing ship colours; currently orphaned |
| `%ahoy_given_manual` | bit 7 | Translation manual handed to the Crone; local sets only in an all-at-once transaction |
| `%ahoy_given_robes` | bit 8 | Mystical robes handed in independently; same defect |
| `%ahoy_given_book` | bit 9 | Book of haricanto handed in independently; same defect |
| `%ahoy_signaturecounter` | bits 10–14 | Petition issued at 1, ten signatures at 11, presented at 31; local stops at 10 and writes 11 after presentation |
| `%ahoy_grinder_status` | bits 15–16 | Ectofuntus grinder cycle; currently not part of a complete modern cycle |
| `%ahoy_subquest_nettletea` | bits 17–18 | Tea progress within main state 3; entirely orphaned |
| `%ahoy_subquest_bow` | bits 19–22 | Ak-Haranu/Robin/manual progress; local invented 1/2/3 values conflict with native/helper threshold 8 |
| `%ahoy_templedoor_unlocked` | bit 23 | Per-player temple door unlock; local writes it |
| `%ahoy_subquest_toyboat` | bits 24–25 | Repaired ship/chest-key/captain-chest progress; local values roughly align but mechanics do not |
| `%ahoy_killed_lobster` | bit 26 | Player's giant-lobster kill; local public NPC ownership makes the write unsafe |
| `%ahoy_requested_sheet` | bit 27 | Innkeeper bedsheet request/recovery; local can consume the entitlement on a full inventory |
| `%ahoy_questvar` | bits 28–31 | Primary state |
| `%ahoy_ectotokens_more` | bits 6–7 of native varp `dragonresist` | High balance component; ownership must be preserved when fixing the token ledger |

The cache quest row's numeric prerequisite decode does not agree with current
authoritative content and must not be copied blindly. Runtime acceptance must
test completed Priest in Peril and The Restless Ghost explicitly. The cache
correctly says skill requirements are boostable and not checked on start.

The local aliases redefine the middle states as 3 “told of Crone”, 4 “cup
given”, and 5 “gathering items”. Native/helper evidence instead keeps the tea
flow under state 3, uses state 4 for three independent ritual hand-ins, and
uses state 5 for the ordinary ghostspeak amulet. Modernization must first map
existing state-4/state-5 saves using support bits and owned quest items; merely
renaming aliases would strand players or duplicate supplies.

## 3. Implementation surface

The direct root has 1,020 lines across one constants file and five scripts.
The quest also depends on shared Ectofuntus, charter, cooking, digging, travel,
diary, POH, quest-requirement, journal, combat, item-ownership, and recovery
systems.

| Path / subsystem | Present responsibility | Audit result |
| --- | --- | --- |
| `quest_ghostsahoy/configs/ghostsahoy.constant` | Aliases, actors, items, locs, coordinates, text constants | Rich coverage, but middle-state and several substate aliases encode noncanonical meanings |
| `quest_ghostsahoy/scripts/ahoy_hub.rs2` | Start/Necrovarus/Crone route, barrier, nettles/tea, petition, temple | Contains the state-2 deadlock, broken barrier, collapsed tea, off-by-one petition, and unsafe Crone ownership |
| `quest_ghostsahoy/scripts/ahoy_book.rs2` | Model ship, chest key, scraps, lobster, captain, island/book | Ship puzzle and traversal are absent; NPC ownership and return travel are unsafe |
| `quest_ghostsahoy/scripts/ahoy_manual.rs2` | Ak-Haranu, Robin, signed bow, manual | Rune-Draw is a deterministic narration; no coin transaction or recovery |
| `quest_ghostsahoy/scripts/ahoy_robes.rs2` | Bedsheet, Gravingas, signatures, door, coffin/robes | Capacity loss, nine-signature completion, no randomness, and weak duplicate/recovery rules |
| `quest_ghostsahoy/scripts/ahoy_shared.rs2` | Requirements, journal, completion, debug/admin adapters | Wrong start gate, non-atomic reward, unusable ectophial, coarse journal, and route-skipping debug |
| Ectofuntus scripts | Bone grinding, slime worship, disciple payout | Worship credit is redeemed one token per worship instead of five; stack-space logic truncates payouts and no 1,000-token cap is enforced |
| `quest_animalmagnetism/scripts/anma_farm.rs2` | Shared Old Crone topic hub | Ghosts Ahoy's broad state-3–6 interception blocks Animal Magnetism dialogue during overlapping progress |
| generic spade/cooking hooks | Dragontooth dig and bucket-milk combination | Additive extension points exist; coverage is incomplete and quest state/ordering needs exact tests |
| charter travel | Port Phasmatys routes and bedsheet refusal | Port route exists; refusal checks inventory sheet forms and may be bypassed by wearing one |
| quest journal dispatcher | Calls `~ahoy_journal` | Correct registration; guidance follows the wrong middle-state model |
| generic quest cheat | Writes the native end state without rewards | Correct state-only intent; no post-write world/item reconciliation |
| POH Morytania painting | Completion-gated decoration | Correctly checks `%ahoy_questvar >= 8` |
| Morytania Diary / Dragon Slayer II | Downstream consumers | Ectophial task and Ghosts Ahoy prerequisite are not demonstrably implemented in their current local owners |

The code uses the modern quest completion renderer and additive shared hooks in
places; “old machinery” here is chiefly behavioral. Static teleports replace
interactive travel, narrated shortcuts replace puzzles and a minigame, public
NPC lookup substitutes for player-owned encounter state, and item removal plus
state writes are not protected as recoverable transactions.

## 4. Primary-state transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Ordinary Velorina dialogue; prerequisite check; explicit accept/refuse | Start checks only The Restless Ghost and incorrectly requires unboosted base Agility/Cooking. Both sympathetic menu answers accept; there is no real refusal/cancel path. |
| 1 | Speak to Necrovarus | Necrovarus writes 2. Normal barrier exit is broken, though alternative travel can mask it. |
| 2 | Return to Velorina; she directs player to the Old Crone and writes 3 | Velorina falls into a generic “keep trying” arm and never writes 3. This is the definitive live main-route deadlock. |
| 3 | Make/serve tea, obtain the ship, and learn about the ritual, with tea substate | Reachable only by injection. Local Crone hands over a cup, writes the invented state 4 even if capacity prevented delivery, and ignores the native tea substate. |
| 4 | Obtain and independently hand in book, manual, and robes; native given bits record each | Local Crone consumes tea, conditionally gives the model ship, and writes 5. The three canonical hand-ins do not occur here. |
| 5 | Hand in the ordinary ghostspeak amulet after all three items | Local collection branches live here. Crone requires book, manual, robes, and amulet simultaneously, deletes all four, sets all three bits, conditionally gives the enchanted amulet, and writes 6. |
| 6 | Wear enchanted amulet and command Necrovarus | Core transition to 7 exists; exact dialogue/optional command presentation is incomplete. Lost enchanted-amulet recovery is absent. |
| 7 | Return to Velorina | Completion call is reachable, but settlement writes 8 before all rewards are safely delivered. |
| 8 | Complete; permanent barrier/ectophial/downstream benefits | Cache selects a post-quest barrier child with no handler; ectophial is inert and unrecoverable; downstream integrations are incomplete. |

Debug command `ghostsahoyrun` fabricates every state and needed item, so it is
not acceptance evidence and hides every route, capacity, puzzle, combat, and
recovery failure. The shorter debug start grants tokens and marks only The
Restless Ghost, reproducing the missing Priest in Peril prerequisite.

## 5. Detailed lifecycle audit

### Acceptance, requirements, and shared dialogue

Acceptance must require completion of both prerequisite quests, offer a real
accept/refuse decision, and change state only on explicit acceptance. Agility
25 and Cooking 20 must not gate the start. Check the player's current boosted
`stat()` at the rock jumps and tea-cooking action respectively, preserving
normal boost/drain behavior and failure text.

The local comments claim Priest in Peril is unfinished, but its current route
advances to completion state 60. That stale exception is no longer defensible.
The local engine also exposes current/boosted stats, so `stat_base` is not a
technical necessity.

Old Crone is shared with Animal Magnetism. `~ahoy_crone_hub` currently returns
handled for every Ghosts Ahoy state from 3 through 6, even when it has no
specific interaction. Compose explicit quest topics and allow the other quest
hub to run when Ghosts Ahoy has nothing to say. Test both quests at every
overlapping state.

### Port Phasmatys barrier and token economy

Before completion, canonical entry costs two ecto-tokens and requires
ghostspeak interaction; exit is free. After completion, both directions are
free without an amulet. Local op 1 only prints text and op 4 always charges and
teleports to one fixed inside coordinate. From inside it therefore teleports
inside again. State 8 selects `ahoy_town_barrier_post_quest`, but no script owns
that resolved child, so the permanent reward is itself inert.

Implement direction from the clicked leaf and player side, preserve ordinary
guard choices, charge exactly once only for inward pre-completion travel, and
bind every resolved child selected by the multiloc. Cancellation, insufficient
tokens, duplicate clicks, a completed save, and both travel directions must
leave consistent location and balance.

The Ectofuntus currently accrues one worship credit and lets the disciple turn
it into one token. Canonical output is five tokens per worship. Its capacity
calculation also uses free inventory slots as the token quantity even though
ecto-tokens stack: a player with one free slot can receive only one token, and
a player with an existing stack but no free slots is refused. Replace this
with a verified combined token ledger, stack-aware capacity, five-per-credit
settlement, the disciple's 1,000-token holding limit, and the current Morytania
legs interaction. Preserve grinder/slime ownership and make interruption or a
full inventory recoverable without duplicate worship rewards.

### Nettles, tea, porcelain cup, and model ship issue

Nettles are globally bound but accept only one exact leather-gloves object.
Modernize the current closed-finger glove allowlist and preserve the canonical
damage path for bare/invalid hands. Tea cooking is a direct item-to-item
conversion with no level-20 current Cooking check, fire/range/log interaction,
burn chance, animation, or 52 Cooking XP. Milk/cup ordering and empty-container
returns must match current accepted recipes.

The native tea substate should own the bowl, milk, cup, and Crone dialogue
checkpoints under main state 3. Old Crone must not claim a delivery that failed
for capacity: the current cup grant writes the next state after a full-inventory
failure, and the current ship grant has the same problem. Follow canonical
inventory-or-ground delivery and replacement behavior. Only completion of the
tea exposition should write main state 4.

### Model ship, wind, dyes, key, gangplank, and rocks

The repaired model consumes silk and thread but has no exact stage/ownership
policy and recognizes only ordinary needle handling. The major puzzle is
missing. Every player needs a persistent random top/skull/bottom colour
combination; the mast reveals components only during sufficiently low wind;
repeated searches expose the whole combination; and dyes repaint individual
parts, including correcting mistakes. Local mast dialogue always reports the
same three primary colours and never uses `%ahoy_windspeed`.

The Old Man currently consumes one loose primary dye rather than inspecting
the painted ship, leaves the model behind, and advances the toy-boat substate.
It cannot replace a lost key. Implement a verified correct-ship comparison,
ship/key transaction, wrong-colour dialogue, all mixed dye colours, repainting,
and key replacement without duplicating banked/ground ownership.

The shipwreck gangplank only prints a message, and `ahoy_rock_invisible` has a
Jump-to option but no trigger. Implement both sides of the wreck traversal and
the full rock route. Each rock jump must check current boosted Agility 25,
deduct five percent run energy under the current rounding policy, animate and
move safely, model canonical failure, and recover from disconnect. Wrong-side,
double-click, movement-lock, zero-energy, and two-player tests are required.

### Map scraps and giant lobster encounter

Canonical scraps come separately from the shipwreck lobster's hull chest, the
captain's keyed chest, and the rock route. Local code awards two scraps
together from either of two chests and gives `ahoy_map_scrap_2` from the
captain chest, contrary to the native/helper source mapping. The map's Read or
follow behavior is also absent.

The giant lobster is spawned publicly at the player's coordinate after a
radius lookup. Another player's lobster can suppress spawning, another player
can attack or kill it, and death AI credits `npc_findhero` rather than a
quest-owned encounter identity. Give each eligible player an isolated or
ownership-tagged level-32 target, correct spawn/despawn behavior, attack
eligibility, death credit, arrow/feedback, and exactly one hull-chest unlock.
Never let one player's target or death change another player's bit.

Scrap combination must be order-independent, exact to the proper phase, and
atomic. It must neither consume partial sets nor create duplicate complete maps
across inventory, bank, ground, death, and reconnect.

### Ghost captain, Dragontooth Island, and book recovery

Outbound travel charges 25 ecto-tokens but omits the ring-reduced fare and the
optional 500-token permanent-access contract. The local captain only serves
main state 5 while the book is absent. After the player digs up the book on
Dragontooth Island, that first condition rejects travel with “Fair winds”,
stranding the player unless an unrelated teleport is available. Return travel
must always be free and available from the island, including post-quest and
later content uses.

Implement directional captain dialogue/travel, exact token settlement,
reduced/permanent fares, interruption-safe movement, and repeat island access.
The completed map/spade dig must occur at the canonical tile, grant the book
with capacity/ground policy, remove or retain the map as canonically specified,
and support book recovery and later clue/content ownership. A banked, dropped,
or already-handed-in book must not be duplicated accidentally.

### Rune-Draw, signed bow, and translation manual

Local Ak-Haranu/Robin progress uses invented substate values 1, 2, and 3,
where native/helper logic treats threshold 8 as the manual-acquired boundary.
Migrate this field before installing corrected handlers.

Robin checks for 400 coins, narrates a deterministic win, never charges or
adjusts money, consumes the oak longbow, and immediately creates a signed bow.
Implement Rune-Draw: 25 coins per game, random runes with their canonical
values, death-rune automatic loss, draw/hold choices, win/loss debt movement,
and signing only when Robin owes 100 coins. Use a modern modal/chat interface
if no reusable native interface survives, while retaining server-authoritative
randomness and ledger settlement. Cancellation and disconnect must resolve a
started game exactly once.

Ak-Haranu must exchange the signed oak longbow for the manual, with inventory
or ground delivery and owned-item checks. Once the trade has occurred, he
provides unlimited loss replacements as canonically allowed. Banked, ground,
or already-handed-in manuals must not be mistaken for a genuinely lost copy.

### Bedsheet, petition, signatures, and bone key

The innkeeper writes `%ahoy_requested_sheet` even when inventory capacity
prevents delivery, then refuses all replacements. Gravingas similarly writes
signature counter 1 after a full-inventory failure. Both are hard loss paths.
Issue to inventory or ground without consuming entitlement, and support the
documented repeat sheet. Wearing either sheet form outside Port Phasmatys and
charter refusal must follow current policy rather than checking inventory only.

The signature loop is off by one: issue sets counter 1 and local villagers stop
at counter 10, producing only nine signatures. Canonical completion is counter
11 after ten successes. Villager responses must be random among support,
ecto-token bribes, and refusal, and the same physical villager cannot be asked
twice consecutively. Track NPC identity per player, not merely the shared NPC
type. Add the petition's Count operation and accurate progress dialogue.

If a petition is lost, Gravingas replaces it with signatures reset as current
content specifies. Presenting the ten-signature petition to Necrovarus consumes
it, sets the native presented sentinel 31, produces the ashes outcome, and
places the bone key on the floor. Local accepts the wrong counter, writes 11,
and puts the key straight in inventory. Capacity, ground ownership, pickup,
loss, and duplicate presentation need explicit tests.

### Temple door, coffin, and mystical robes

The per-player door-unlocked bit is appropriate, but verify both resolved door
forms, use-key and Open ordering, animation, movement, and persistence. The
key must not be charged twice or leave the player on the wrong side after an
interruption.

The coffin checks only inventory for robes and can create duplicates when a
copy is banked or on the ground. Full inventory currently refuses instead of
using the canonical ground delivery. Implement all-owned singleton logic,
correct coffin animation/search feedback, post-hand-in state behavior, and
loss recovery. The given-robe bit, not a renamed main state, is the ritual's
authoritative hand-in record.

### Independent ritual hand-ins and enchanted amulet recovery

At main state 4, Old Crone accepts the book, manual, and robes independently
and records each native given bit. The local all-at-once state-5 deletion makes
banking/capacity/recovery brittle and destroys the native resume contract.
Each hand-in must be an exact, protected transaction with distinct repeat
dialogue, no duplicate consumption, and no dependence on carrying all three.
Only after all three bits are set should main state 5 request an ordinary
ghostspeak amulet.

The amulet exchange must give the enchanted form safely and write state 6 only
after delivery is recoverable. If it is lost, replacement begins with Father
Urhney's ordinary amulet and returns through the Crone's already-completed
ritual path; it must not demand the three consumed objects again or manufacture
their source items. Wearing the enchanted amulet at Necrovarus runs the exact
command sequence once and writes 7.

### Completion, ectophial, and replay safety

The completion procedure writes state 8 before Prayer XP, quest points, and
item delivery. It adds an ectophial only when a free inventory slot exists and
checks inventory rather than all ownership. A disconnect/error or full
inventory can therefore create a completed save permanently missing XP or the
reward. Admin completion likewise leaves barrier/item/unlock state
contradictory, though its no-reward policy should remain.

Create one guarded settlement boundary from exact state 7. Award 2,400 Prayer
XP, two QP, completion count/scroll, barrier unlock, and ectophial exactly once.
Use inventory-or-ground/outstanding-reward delivery, all-owned duplicate rules,
and login/admin reconciliation. Never infer ambiguous historic XP or reward
ownership into a duplicate grant.

No handler exists for the full ectophial's Empty option or teleport. Implement
the full/empty item pair, server-authoritative teleport to the Ectofuntus area,
the current above-level-20 Wilderness restriction, protected teleport timing,
automatic uninterruptible refill, temporary damage protection only for the
verified refill interval, and manual Empty behavior. Loss recovery must support
Velorina with the required ghostspeak/Morytania-legs exception and Perdu's
current fee, plus the canonical multiple-vial rule in which a player who owns
a full vial may receive an empty one. Bank, death, ground, reconnect, combat,
teleblock, capacity, and duplicate-click cases require tests.

### Journal, admin adapter, and downstream consumers

The journal uses the modern dbrow dispatcher but reflects the invented cup and
all-at-once states, omits Priest in Peril, and cannot guide recovery from any
lost quest item. Rebuild it from the corrected primary/support contract. It
should name the current actionable subtask, costs/skills only at their actions,
each independently handed item, safe return travel, and outstanding reward or
recovery without leaking the randomized ship answer.

The generic quest cheat correctly writes state 8 without granting rewards.
Preserve that policy, then run a non-economic reconciliation that selects the
post-quest barrier state, removes obviously invalid transient presentation,
and makes downstream completion checks coherent. The local POH Morytania
painting already consumes the primary correctly. Add/verify the Morytania
medium diary's ectophial task and Dragon Slayer II's Ghosts Ahoy prerequisite
in their owning systems; do not implement quest-local substitutes.

Migration must explicitly classify primary states 0–8, especially legacy 4/5,
plus every support field, petition counters 10/11/31, bow values 1/2/3 versus
native threshold 8, toy-boat progress, item ownership, players on Dragontooth
Island, outstanding ectophial reward, and any player located on the wrong side
of a barrier or inert traversal. Ambiguous cases need a conservative recovery
dialogue or operator report, not guessed consumption or rewards.

## 6. Modernization work packages

### Package 1 — repair state meaning and unblock the core route

- Add Priest in Peril and The Restless Ghost acceptance checks; move boosted
  skills to the tea and rock actions.
- Restore explicit accept/refuse dialogue and the missing Velorina state-2 to
  state-3 transition.
- Decode/migrate main states 3–5 and all support counters before exposing new
  handlers.
- Compose Old Crone topics safely with Animal Magnetism.
- Bind both-direction pre/post-quest barrier children with atomic tolling.

### Package 2 — restore tea, protest, and temple transactions

- Implement nettle protection, real boostable cooking, heat/burn/52-XP flow,
  milk/cup ordering, and capacity-safe cup/ship delivery using the tea substate.
- Add bedsheet recovery, randomized per-NPC signature attempts, bribes,
  petition Count/replacement, ten-signature threshold 11, and presentation 31.
- Put the bone key on the owned ground path, then repair door and coffin
  transactions with persistence and all-owned checks.

### Package 3 — rebuild the model-ship and map route

- Generate/persist three random ship colours; implement low-wind mast searches,
  all dyes, part selection, repainting, and correct Old Man comparison/key
  replacement.
- Implement gangplank and every boosted-Agility rock jump with energy/failure
  policy.
- Give each player an owned giant lobster and distinct canonical scrap source;
  combine/read the map transactionally.
- Modernize captain fares, ring/permanent discounts, free return, post-quest
  travel, dig/book issue, and recovery so no island state strands a player.

### Package 4 — implement Rune-Draw and ritual hand-ins

- Replace the narrated auto-win with server-authoritative 25-coin Rune-Draw,
  rune/death rules, choices, debt, cancellation, and signed-bow settlement.
- Migrate the bow substate and add manual loss replacement.
- Accept robes/manual/book individually at main state 4 through their native
  flags, then handle ordinary/enchantable amulet and loss recovery at state 5.
- Run the Necrovarus command once from exact state 6.

### Package 5 — settle rewards and complete integrations

- Make state-7 completion atomic/idempotent with correct Prayer XP, QP, count,
  scroll, barrier state, and capacity-safe ectophial.
- Implement ectophial Empty/teleport/refill/Wilderness/recovery/multiple-item
  behavior and correct the Ectofuntus five-token ledger/capacity model.
- Rebuild the journal, debug fixtures, login migration, and admin reconciliation.
- Add owner-level automated coverage for Morytania Diary and Dragon Slayer II;
  preserve the already-correct POH painting consumer.

## 7. Verification matrix

Automated transition coverage must include at least:

| Scenario | Required assertion |
| --- | --- |
| Start cancellation and each missing prerequisite | State stays 0; no item/currency change |
| Boosted skills below/at threshold | Start is allowed; tea/rock action alone enforces current Cooking 20 / Agility 25 |
| State-2 Velorina return | Exact dialogue writes 3 once and unlocks recoverable tea flow |
| Barrier outside/inside before and after completion | Only prequest entry costs two; exit and all postquest passage are free and directional |
| Ectofuntus worship redemption | Five tokens per credit, stack-aware capacity, 1,000 held cap, no duplicate on interruption |
| Full inventory for cup, ship, sheet, petition, key, robes, book, manual, enchanted amulet, ectophial | State/entitlement never advances into permanent loss; canonical ground/retry path works |
| Tea variants | Valid gloves, current level, fire/range, burn, milk order, containers, and exactly 52 XP match policy |
| Two players' mast puzzles | Independent random answers, wind/search persistence, dyes and repainting cannot cross-contaminate |
| Shipwreck traversal | Both gangplank directions and all rocks enforce movement, energy, skill, failure, and reconnect safely |
| Two simultaneous lobsters | Spawn, combat, despawn, kill bit, and scrap credit remain player-owned |
| Three scrap sources and all combination orders | Exactly one complete map, no partial loss or duplicate |
| Captain outbound/return | Exact normal/reduced/permanent fare; return is free with map, book, after hand-in, and postquest |
| Rune-Draw deterministic seed tests | Every rune/death/hold/draw outcome, 25-coin settlement, debt ±25, cancellation, and signing at debt 100 |
| Petition loop | Ten successes produce counter 11; refusal/bribes/consecutive NPC/count/replacement behave correctly; presentation writes 31 |
| Shared Old Crone | Ghosts Ahoy and Animal Magnetism topics remain independently reachable at all overlapping states |
| Independent ritual hand-ins in every order | Each object consumed once, correct bit persists, missing/lost items recover, main 5 begins only after all three |
| Enchanted amulet loss | Ordinary replacement can be re-enchanted without repeating consumed ritual items |
| Completion double-click/reconnect/full inventory | XP, two QP, count, ectophial, and unlocks settle exactly once or remain claimable |
| Ectophial lifecycle | Full/empty, teleport/refill protection, >20 Wilderness denial, bank/death/loss recovery, and multiple-item rule |
| Admin completion and legacy saves | No economic rewards granted; barrier and downstream completion reconcile without guessing ambiguous history |
| Downstream gates | State 7 does not unlock; state 8 unlocks POH painting, diary behavior, and Dragon Slayer II requirement |

Manual parity review must replay the pinned article, quick guide, and transcript
with inventory-full, banked-item, dropped-item, death, logout, reconnect, travel
interruption, and two-player variants. Confirm every quest item option and every
resolved multiloc/multinpc child, not only the cache shell named in source.

## 8. Prioritized findings

### P0 — progression, travel, reward, or state-integrity failures

1. Velorina never writes state 2 to state 3, so the live main route cannot
   progress after the first Necrovarus conversation.
2. The prequest barrier cannot provide canonical exit and the state-8 resolved
   barrier child is unhandled, breaking both route travel and the free-passage
   reward.
3. Gangplank and rock-jump interactions are inert, so the canonical map route
   is absent even where the shortened local shortcut can fabricate progress.
4. The captain refuses return after the book is obtained, which can strand a
   player on Dragontooth Island.
5. The ectophial reward has no Empty, teleport, refill, or loss-recovery
   implementation, and full inventory can lose it permanently.
6. Local main-state 4/5 and support-counter meanings conflict with native saves;
   corrected scripts without migration would strand or duplicate existing
   progress.

### P1 — major mechanics, economy, recovery, and integration gaps

- Priest in Peril is omitted; boostable action skills are wrongly unboostable
  start gates and are not checked where used.
- Ectofuntus grants one token per worship instead of five and mishandles
  stack capacity.
- Ship colour/wind/dye puzzle, rock mechanics, three scrap sources, and owned
  lobster encounter are absent or collapsed.
- Rune-Draw is a free deterministic narration with no actual game or coin/debt
  settlement.
- Petition completion records nine signatures, omits canonical randomness and
  bribes, and uses the wrong presented sentinel/key-delivery path.
- Cup, ship, sheet, petition, manual, robes, enchanted amulet, and ectophial
  have permanent capacity/loss/duplicate failures.
- Ritual items are wrongly required together at state 5 instead of independent
  hand-ins at state 4.
- Broad Old Crone ownership can block Animal Magnetism.
- Morytania Diary and Dragon Slayer II consumers are incomplete or absent.

### P2 — parity, guidance, and maintainability gaps

- Dialogue, re-talks, optional Necrovarus command, item presentation, journal
  detail, and failure feedback are heavily compressed.
- Captain discounts/permanent access, sheet-area restrictions, model repainting,
  map reading, and several postquest/revisit branches are missing.
- Debug routes assert states and fabricate inventory instead of exercising
  public triggers, capacity, movement, combat, recovery, and settlement.

## 9. Evidence boundary and completion gate

This audit inspected authored quest content, cache-backed native state/assets,
shared integrations, Quest Helper extraction, and the pinned current Wiki
contract. It did not modify gameplay and does not claim that any route passes.

Ghosts Ahoy may move from `audit-pending` only after:

- legacy state/support migration is implemented and tested;
- the unassisted public route completes from state 0 through state 8;
- every package above has automated transition, negative, capacity, recovery,
  reconnect, and two-player coverage;
- all ecto-token, Rune-Draw, toll, and reward transactions are exactly-once;
- ectophial and free barrier passage work as persistent rewards;
- shared Crone, charter, diary, POH, and Dragon Slayer II integrations pass in
  their owning suites; and
- a manual replay against every pinned reference finds no unexplained
  divergence.
