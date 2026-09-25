# Forgettable Tale... modernization audit

Status: `audit-pending` — the native quest row, primary carrier, farming
transform, seed/recovery flags, shared brewing fields, company membership,
boarded-door transform, three puzzle interfaces, nineteen junction fields,
five box fields, library evidence fields, tunnel maps, cutscene actors, journal,
completion API, and rewards all exist. The authored route is nevertheless not
command-free completable: its state-0 branch steals Commander Veldaban from The
Giant Dwarf before that prerequisite can finish. An administratively prepared
account can reach a reward call, but the route uses incompatible primary-state
meanings, reverses two seed flags and both minecart tickets, replaces nine rail
puzzles and six rooms with three messages, omits the library and both decisive
cutscenes, bypasses native brewing, loses quest items on full inventories, and
can duplicate completion rewards.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Veldaban and The Giant Dwarf hand-off,
the Drunken Dwarf and seed owners, Rind's patch and optional letter, the shared
brewery, both ordinary minecart journeys, every company director, the boarded
tunnel, all nine junction routes and six rooms, memory loss, the pub finale,
reward settlement, recovery, journal/admin adapters, and shared consumers. It
is an implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, timing, puzzle, reward, and recovery
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Forgettable Tale...](https://oldschool.runescape.wiki/w/Forgettable_Tale...?oldid=15301583) | 15301583, 2026-08-14 | Identity, current requirements, complete route, timing, tunnel rooms, ending, and rewards |
| [Forgettable Tale.../Quick guide](https://oldschool.runescape.wiki/w/Forgettable_Tale.../Quick_guide?oldid=15290517) | 15290517, 2026-08-08 | Ordered interactions, exact ticket prices, three puzzle groups, hands-free rule, and final order |
| [Transcript:Forgettable Tale...](https://oldschool.runescape.wiki/w/Transcript%3AForgettable_Tale...?oldid=15278851) | 15278851, 2026-07-28 | Start/refusal, shared-topic menus, alternate seed choices, lost-item recovery, six rooms, both cutscenes, post-quest dialogue, and 2025 sailor text |
| [Kelda seed](https://oldschool.runescape.wiki/w/Kelda_seed?oldid=15273768) | 15273768, 2026-07-24 | Four-seed ownership, planting, and recovery lifecycle |
| [Kelda hops](https://oldschool.runescape.wiki/w/Kelda_hops?oldid=15273744) | 15273744, 2026-07-24 | Patch harvest, brewing use, and loss recovery |
| [Kelda stout](https://oldschool.runescape.wiki/w/Kelda_stout?oldid=15273757) | 15273757, 2026-07-24 | Barrel collection, use restrictions, hand-in, and replacement |
| [Brewing](https://oldschool.runescape.wiki/w/Brewing?oldid=15290591) | 15290591, 2026-08-08 | Shared vat/barrel order, vessel returns, fermentation, drain, and capacity behavior |
| [Keldagrim minecart system](https://oldschool.runescape.wiki/w/Keldagrim_minecart_system?oldid=15290096) | 15290096, 2026-08-07 | Shared destinations, direction-specific tickets, fares, and Ring of charos price |
| [Dwarven machinery](https://oldschool.runescape.wiki/w/Dwarven_machinery?oldid=14207425) | 14207425, 2021-11-28 | Junction interface behavior and stone placement |
| [Square stone](https://oldschool.runescape.wiki/w/Square_stone?oldid=15185620) | 15185620, 2026-04-22 | Green/yellow stone acquisition, increasing route inventory, and Drop restriction |
| [Annual Asgarnia Gardening Conference](https://oldschool.runescape.wiki/w/Annual_Asgarnia_Gardening_Conference?oldid=11270067) | 11270067, 2020-01-06 | Rind-to-Elstan optional letter window, refusal, and two-marrentill-seed reward |
| [Transcript:Rowdy dwarf](https://oldschool.runescape.wiki/w/Transcript%3ARowdy_dwarf?oldid=15029502) | 15029502, 2025-11-15 | Random request, re-talk, hand-in, and ordinary shared dialogue |
| [Transcript:Khorvak, a dwarven engineer](https://oldschool.runescape.wiki/w/Transcript%3AKhorvak,_a_dwarven_engineer?oldid=15079336) | 15079336, 2025-12-06 | Free borrow and optional stout routes composed with Between a Rock... topics |
| [Transcript:Gauss](https://oldschool.runescape.wiki/w/Transcript%3AGauss?oldid=15079334) | 15079334, 2025-12-06 | Beer-or-empty-glass toast without consuming the vessel |
| [Transcript:Rind the gardener](https://oldschool.runescape.wiki/w/Transcript%3ARind_the_gardener?oldid=15095306) | 15095306, 2025-12-27 | Permission, grow-time dialogue, optional job, brewing hints, and lost-hop recovery |
| [Transcript:Blandebir](https://oldschool.runescape.wiki/w/Transcript%3ABlandebir?oldid=14289266) | 14289266, 2022-05-28 | Empty-pot plus 25-coin yeast exchange and shared brewery topics |
| [Mature dwarven stout](https://oldschool.runescape.wiki/w/Mature_dwarven_stout?oldid=6246394) | 6246394, 2018-04-20 | Exact two-item reward identity and ordinary post-quest use |

The sources define a members, intermediate, long quest released 26 July 2005,
the second Rise of the Red Axe quest. It requires completed The Giant Dwarf and
Fishing Contest, boostable level 22 Cooking and 17 Farming, no combat, about
400 coins, two barley malt, two buckets of water, a rake, seed dibber, ale
yeast or an empty pot plus 25 coins, two beers, a beer glass, a kebab, and one
random Rowdy-Dwarf item. The tunnel additionally requires two free inventory
spaces and empty weapon and shield slots. Rewards are two quest points, 5,000
Cooking XP, 5,000 Farming XP, and two dwarven stout(m).

The current Wiki and Quest Helper agree on 22 Cooking. Revision 239's dbrow and
the local constant say 20. This is a stale cache metadata conflict, not license
to lower the current mechanic: update the displayed/runtime requirement to 22
and retain boostability and the native `requirement_check_skills_on_start=0`
policy. The player may accept before having the skill, but the Farming and
brewing actions must enforce the current boostable requirements.

Transition aid only: Quest Helper's
[`ForgettableTale.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/forgettabletale/ForgettableTale.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms native states
0/10/20/30/40/50/60/65/70/80/100/105/110/115/118/119/120/130/140, all
seven possible company directors, vat thresholds 1/2/68/69/71, barrel value 3,
the nineteen junction values, actors, coordinates, requirements, and rewards.
`python3 tools/questhelper_extract.py forgettabletale --check` resolves its
dbrow and every referenced item, NPC, loc, and varbit with no unresolved
symbol. The helper guides transitions and tests; it does not replace the Wiki
or transcript.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_forgettabletale`; dbrow pack index 53, quest metadata ID 88 |
| Type / difficulty / length | Members quest / intermediate / long |
| Release / series | 26 July 2005 / Rise of the Red Axe #2 |
| Start | Commander Veldaban (`dwarf_city_black_guard_leader`) at 2827,10214,0 |
| Prerequisite rows | Pack row 63 The Giant Dwarf and row 52 Fishing Contest; the local comment claiming these decode to wrong rows is false |
| Skills | Cooking 22 and Farming 17, boostable, not start-gated; cache Cooking 20 must be corrected |
| Primary | `%forget_quest`, bits 0–7 of permanent native `forget_main_var`; end state 140 |
| Farming | `%forget_farming`, bits 8–11; values 0–9 directly transform the dedicated ten-state Kelda patch |
| Story flags | exposition, boarding removed, three seed given, three seed told, gardener task, three room evidence bits, brothers success, region status, beer given, and three cutscene bits occupy the remaining 20 bits of `forget_main_var` |
| Puzzle carriers | `forget_puzzle_var` and `forget_puzzle_var2`: nineteen two-bit junctions, left/right counts, start/five box flags, and room-one visit/listening bits |
| Shared brew | `brewing_vat_varbit_1`, byte 0 of `farming_varp_9`; values 0 empty, 1 water, 2 malt, 68 Kelda hops, 69/70 fermenting, 71 ready. `brewing_barrel_varbit_1`, byte 2; value 3 is Kelda stout |
| Company routing | `giantdwarf_current_company` values 1–7 choose Purple, Yellow, Blue, Green, White, Silver, or Brown director |
| World transforms | Dedicated Kelda patch, boarded tunnel, vat, barrel, card boxes, tracks, carts, entrances/exits, library crates/bookcase, and all six tunnel-room maps exist |
| Presentation | Interfaces 248, 244, and 247 cover puzzle groups 1–3; dedicated Red Axe, gnome, chaos-dwarf, shaman, patron, Drunken Dwarf, chair, and beer cutscene assets exist |
| End / rewards | State 140; 2 QP; 50,000 raw Cooking XP and 50,000 raw Farming XP; two `mature_dwarven_stout` |

The primary carrier is correct, but most local values are not. Native progress
is a coarse chapter field whose substeps are expressed by named secondary
fields. The port instead inserted values 25 and 42–48 into the primary, failed
to write `forget_beer_given`, swapped the meanings of seed 3 and seed 4, and
used chapter values only after their canonical work was already over. Imported
or client-observed state therefore disagrees with the server even when the
integer happens to be recognized.

The shared brewing bytes are not unverified scratch fields. Their multilocs
fully enumerate ordinary ales and the Kelda-specific 68–71/3 states, and Quest
Helper reads exactly those values. Replacing them with primary quest plateaus
leaves both the physical brewery and every shared brewing invariant false.

## 3. Implementation surface

The direct root contains 817 lines in six files. Four mandatory external
owners contain the shared Veldaban, director, Khorvak, and kebab triggers;
journal dispatch, admin completion, cache configs, maps, brewing, Farming,
shops, and world spawns complete the real surface.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/forgettabletale.constant` | Source notes, primary aliases, requirements, rewards, and coordinates | Documents acknowledged substitutions as fidelity; adds incompatible 25/42–48 plateaus; Cooking is stale at 20 |
| `configs/forgettabletale.varp` | Re-declares native `forget_main_var` | Correct permanent/transmitted carrier; no parallel varp is needed |
| `scripts/forget_keldagrim.rs2` | GE travel, Drunken Dwarf, Rowdy, Gauss, Rind, and patch | Route-shaped but wrong item choices/consumption, reversed seed flags, non-atomic grants, no recovery, no optional letter, and a session timer |
| `scripts/forget_brewing.rs2` | Pub stairs, Blandebir, vat, valve, barrel | Explicitly bypasses shared brewing and visual transforms; invents the pot, loses buckets, and can advance after failed item grants |
| `scripts/forget_tunnels.rs2` | Ordinary carts, conductor, secret cart, and tunnel | Reverses both tickets, charges no fare, ignores equipment/capacity, and replaces the quest's defining nine puzzles and six rooms with three messages |
| `scripts/forget_shared.rs2` | Requirement helpers, completion, and journal | Skill helper is never called; completion is unguarded; journal reports a story the character canonically forgot |

Mandatory shared owners:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `quest_giantdwarf/scripts/gdwarf_start.rs2` | Sole Veldaban trigger | State-0 Forgettable routing returns before Giant Dwarf; it deadlocks the prerequisite and makes this start unreachable without admin state |
| `quest_giantdwarf/scripts/gdwarf_consortium.rs2` | Shared company director proc | Only Blue Opal is bound and The Giant Dwarf forces Blue Opal; all other valid/imported company memberships are stranded |
| `quest_betweenarock/scripts/betweenarock_schematics.rs2` | Shared Khorvak trigger | Forgettable branches permanently steal Between a Rock dialogue and force a stout despite a canonical free-borrow route |
| `player/scripts/consumption/kebab.rs2` | Shared Kebab Eat | Completes from any location, consumes the beer automatically, omits the pub cutscene, and calls an unguarded reward proc |
| `interface_questjournal/scripts/quest_journal.rs2` | Dynamic journal dispatch | Correctly dispatches row 88 to `~forget_journal` |
| `quests/scripts/quest_cheat.rs2` | Administrative completion | Correct idempotent adapter shape: writes 140 and lets the common cheat layer award QP/count without route XP/items |
| shared Farming/brewing/map/world configs | Native mechanics and presentation | Assets are present but almost entirely unused; modernization belongs in shared systems where their semantics are general |

There is no legacy `if_openmain`/`if_openoverlay`, raw numeric entity ID, or
hand-painted quest-list panel in the direct root. The old machinery is more
fundamental: incompatible state meaning, bypassed shared systems, public/shared
topic theft, plain teleports, player-session timers, and text standing in for
interfaces, rooms, actors, and cutscenes.

## 4. State and transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Veldaban exposition and explicit Start quest? Yes/No; then title/camera scene and transport to Drunken Dwarf | A two-choice paraphrase starts in place. More seriously, failed prerequisites return before Giant Dwarf's own branches, so Giant Dwarf cannot finish and this state cannot be left naturally. |
| 10 | First Drunken Dwarf conversation | Local state 10 reaches him, but the required opening route/camera and full shared options are absent. |
| 20 | Offer beer; native `forget_beer_given` distinguishes the follow-up | Local first talk writes 20, then consumes or auto-buys a beer, never writes the beer bit, grants a seed unchecked, and writes invented state 25. |
| 30 | Collect Rowdy, Gauss, and Khorvak seeds using per-friend told/given flags | Local uses state 25, replaces Rowdy's random item with one coin, consumes Gauss's beer, forces Khorvak's stout route, and writes Gauss/Khorvak given bits in reverse. |
| 40 | Permission, rake/plant, growth, optional letter, harvest/recovery | Local enters Farming at 30 and reaches 40 only after harvesting, so native observers present the wrong chapter throughout. |
| 50 | Empty/prepare shared vat, buy yeast if needed, add ingredients, wait, valve, and collect | Local uses 40 plus invented 42–48 and reaches 50 only after collecting the stout; vat/barrel bytes remain unchanged. |
| 60 | Give the Kelda stout to the Drunken Dwarf | Local state 50 is this phase; its successful hand-in writes 60. |
| 65 | Continue the dwarf's tale/cutscene and learn of the disused tunnel | Local uses a short second conversation and writes 65, without the story scene. |
| 70 | Ask the southern conductor about the closed tunnel | Local state 65 reaches the conductor and writes 70; dialogue is abbreviated. |
| 80 | Ask the player's current company director; remove boards | Local state 70 is this phase, but only Blue Opal can handle it. |
| 100 | Enter tunnel; search starting box; solve three Room-1 routes and collect stones | Local state 80 takes the secret cart, then 100; searching one box immediately writes 105 without stones or any puzzle. |
| 105 | Listening Room: remain through the Red Axe conversation, then exit | Local uses this value for the first one-click control message and immediately writes 110. |
| 110 | Solve the three Room-3 route puzzles and collect more stones | Local uses this value for a second one-click message and jumps to 118. |
| 115 | Search Room 4's bookcase and two relevant crates; read three pieces of evidence | No constant, transition, loc trigger, item text, journal phase, or route exists. |
| 118 | Solve the three Room-5 route puzzles | One click at the same hub prints a war-council message and writes 119. |
| 119 | Start/watch the Room-6 army and memory-wipe cutscene | Local treats 119 as already done and allows Veldaban reporting; no actor is used. |
| 120 | Back in Keldagrim after memory wipe; speak to Veldaban | Declared but never written. Veldaban accepts 119 or 120 and jumps directly to 130. |
| 130 | Buy/hold beer and kebab; trigger finale in the east pub | Reachable, but Kebab Eat works anywhere and no finale runs. |
| 140 | Complete and post-quest Veldaban dialogue | Reward call can reach 140; post-quest text is invented and permanently steals Veldaban's other topics. |

The mismatches are not harmless extra checkpoints. A live/imported state 30
means “collect seeds,” while this server interprets it as “farm”; state 40
means “grow hops,” while this server interprets it as “brew”; state 50 means
“brew,” while this server interprets it as “give the finished stout.” The same
shift continues through state 80. A modern implementation must restore native
chapter semantics and express substeps through the already named varbits.

## 5. Detailed lifecycle audit

### Start, Veldaban ownership, and Keldagrim travel

Veldaban is a shared actor and needs a topic router, not a first-match quest
return. Every new account has `%forget_quest=0`; the local branch checks
Forgettable prerequisites and returns “Nothing more” before evaluating
`%giantdwarf_quest >= gdwarf_ready_to_finish`. Thus an account at The Giant
Dwarf's final report cannot complete it, can never satisfy Forgettable's
prerequisite, and can only escape via `::complete`. This is a critical route
deadlock. Route Giant Dwarf progress first until it is complete, then expose
the Forgettable start topic when Fishing Contest is also complete. A declined,
ineligible, active, completed, or side-topic interaction must not suppress an
unrelated valid Veldaban topic.

Restore the current exposition, the 5 November 2025 “half the world has become
a sailor” text, interested/not-now loop, dwarf-recognition choice, explicit
Start quest confirmation, refusal, state re-talks, and post-quest conversation.
On acceptance, commit state 10 exactly once, run the statue/title/camera scene,
and deliver the player outside the Drunken Dwarf's house. Cancellation and
disconnect need a resumable boundary that cannot replay the acceptance or
strand the player in a camera.

The Grand Exchange cart trapdoor is shared travel, not a quest shortcut. The
local handler teleports directly from the surface to Veldaban. Restore the
minecart confirmation, animation, real Keldagrim station arrival, collision,
and ordinary availability in the shared transport owner. It must not skip the
quest's acceptance cutscene or act as an unrecorded Veldaban teleport.

### Beer, seeds, shared NPC topics, and recovery

The first beer is an ordinary player-supplied transaction. Do not create a
beer inside the Drunken Dwarf trigger merely because two coins exist; the
Laughing Miner and King's Axe Inn own sales and choices. On a valid offer,
consume one beer, set `forget_beer_given`, present the Kelda explanation, and
deliver the first seed capacity-safely before moving primary 20→30. If the
seed is lost, the Drunken Dwarf replaces missing seed count during this phase.
The local unchecked add commits state 25 even when a full inventory rejects
the seed, leaving no recovery path.

Rowdy's request is a per-player random item from the documented list, retained
across re-talks and eligible to change after the live waiting interval. One
coin is not a representative exchange. `forget_seed2_told` and
`forget_seed2_given` distinguish request and delivery; if an additional
request-choice/timestamp carrier is genuinely absent, add the smallest named
persistent field after confirming no unnamed native field owns it. Consume
only the requested item in the same capacity-safe commit that grants the seed.

Khorvak must compose Forgettable and Between a Rock... in one menu. The player
may promise to return the seed and receive it free, or offer one dwarven stout;
if the stout is absent, dialogue points to the real table spawn rather than
materializing a drink. The local top-level `forget_seed3_given` branch steals
Khorvak forever, including after Forgettable completion, and blocks his
schematic topic. It also writes the field whose native/Quest Helper meaning is
Gauss. Bind Khorvak to native seed 4 and preserve every valid Between a Rock
state before, during, and after this quest.

Gauss accepts either a beer or an empty beer glass for the toast, consumes
neither, plays the toast, and grants native seed 3. The local implementation
requires/auto-buys beer, deletes it, and writes seed 4. Both friends' existing
saves therefore need a simultaneous semantic swap during migration. Every
seed grant must handle full inventory, repeated Talk, ground/bank copies, stale
packets, and recovery without duplicating or permanently losing a seed.

The barmaid, barman, Drunken Dwarf, Rowdy, Gauss, Khorvak, and the three dwarf
brothers all have ordinary/shared dialogue in the transcript. Quest choices
must be added to their menus only in relevant phases; a completed quest bit
must not replace their normal personalities with a single quest line.

### Rind, the native patch, and the optional letter

Rind grants permission only when all four seeds are available, then the native
Farming patch handles three rake states, planting four seeds, growth states
4–8, and harvest state 9. Modernize through the shared Farming interaction
shape: correct Rake/Inspect/Compost/Plant/Pick behavior, seed dibber or verified
Barbarian hand-plant alternative, boostable Farming 17 gate, weed products if
live behavior grants them, transforms, messages, and notifications. The local
single op1 wrapper skips Farming ops, increments one state per click without
normal products, jumps 4→8, and can commit 9/state 40 after a failed hop add.

Growth lasts 15–20 minutes, can progress while the player is offline, cannot
disease, and has the documented login/reset nuance. A one-shot player timer
armed only at planting is not durable: logout, restart, or a lost timer can
leave state 4 forever. Use the established persistent Farming/time machinery,
record the deadline or cycle state, rebuild presentation on login, and notify
once. Harvest must reserve a hop slot before changing patch or primary state.
Rind replaces lost harvested hops; the current empty patch is terminal.

During the growing window, Rind can offer the Annual Asgarnia Gardening
Conference letter to Elstan. Acceptance, refusal, delivery, return, and two
marrentill seeds are tracked by the native three-bit `forget_gardener_task` and
the existing `forget_gardener_letter`. The opportunity ends after harvest and
refusal is final. This optional but reachable transcript path is wholly absent
despite its native state and item; implement it with capacity-safe letter and
reward exchanges and shared Elstan dialogue.

### Shared brewing and ingredient transactions

The Keldagrim vat is a real shared brewing vat. It may already contain another
brew; the canonical route tells the player to empty finished contents or drain
fermenting contents before Kelda brewing. Local primary values ignore that
state, so the model remains empty while items disappear and concurrent/shared
brewing rules are bypassed. Drive `brewing_vat_varbit_1` through 0→1→2→68→69/
70→71 and `brewing_barrel_varbit_1` through Kelda value 3, using the native
multiloc leaves and a shared fermentation deadline. Do not overwrite the other
three bytes of `farming_varp_9`.

Blandebir exchanges one empty pot plus 25 coins for ale yeast. The local
dialogue invents a pot, consumes no pot, performs an unchecked add, and writes
42 even if no slot was available. Keep the nearby real pot spawn, verify pot,
coins, and capacity, and commit the exchange atomically. The player may also
bring ale yeast, so buying it must never be a mandatory primary transition or
permit repeated 25-coin charges after loss.

Adding two buckets of water must return two empty buckets according to shared
brewing behavior; local code deletes the filled buckets outright. Then consume
two malt, one Kelda hops, and one yeast only in order and only after current
item/use revalidation. Enforce boostable Cooking 22 at the live action, not the
stale local 20. Fermentation must survive logout/restart, show intermediate
models, notify ready once, allow the defined drain/loss path, transfer ready
stout to barrel once, and exchange one empty beer glass for one Kelda stout.
Rind replaces lost hops before brewing and the Laughing Miner barmaid replaces
a lost finished stout; no local recovery exists.

Pub stairs should use shared climb animation, approach, and validated arrival
rather than bare `p_teleport`. Every transaction needs tests for a pre-existing
brew, wrong ingredient/order, full inventory, item removed during a delay,
repeated use packet, drained vat, logout at every fermentation state, and two
players observing independent per-player multivars.

### Ordinary minecarts, conductor, and company director

The ordinary White Wolf Mountain trip is optional and costs 100 coins each
direction, 50 for the verified Ring of charos route on the return ticket. The
local scripts charge nothing and use both direction-specific objects backwards:
`dwarf_minecart_ticket_kelda_whitewolf` describes Keldagrim→White Wolf, while
the Keldagrim conductor currently grants/consumes
`dwarf_minecart_ticket_whitewolf_kelda`; the return side is reversed likewise.
Restore the shared minecart shop/service for all eligible players, exact fare,
choice/refusal, ticket capacity/recovery, direction, ride presentation, and
arrival. Do not gate the service on Forgettable or silently call a free item a
purchase.

After the stout story, the southernmost conductor supplies an influential-
friend hint. Veldaban can then explicitly point to the player's company
director. The local route skips that useful Veldaban state branch and sends
the player directly to “the Director.” Compose those dialogue topics rather
than letting any one quest own the actors.

All seven company values and directors are canonical. Fresh local Giant Dwarf
currently forces Blue Opal, but imported saves and a modernized prerequisite
may hold any value 1–7. Bind the boarded-tunnel option to every corresponding
director and preserve each company's ordinary/Giant Dwarf choices. Only the
member's director may remove the boards. Set `forget_boarding_removed` and
advance 80→100 at a single commit; repeated dialogue is informational. The
wide stairs remain shared travel and must not alter unrelated Giant Dwarf
state on a Forgettable visit.

The secret cart needs no ticket but requires both hands free and two free
inventory spaces. The local cart checks neither, teleports straight to one hub
tile, and conditionally writes primary 100. Restore equipment/capacity refusal,
camera introduction, ride/return choice, region ownership, collision, and safe
escape. Logging out anywhere in the caves must return the player to Keldagrim
while retaining the exact canonical puzzle checkpoint.

### Nine rail puzzles and six tunnel rooms

The tunnel is the quest's main gameplay, not a cosmetic widget. Three chasms
each require three different routes. The player searches the starting box for
green/yellow square stones, mounts the corresponding interface (248, 244, or
247), cycles junctions through empty/green/yellow, sees the projected path,
confirms only a valid connected route, rides a physical cart to the selected
platform, searches additional boxes, returns, and finally reaches the next
story room. The native nineteen two-bit junctions, left/right counts, six box
flags, carts, tracks, maps, and interfaces exist specifically for this.

Implement the interfaces with the modern modal mount/events/client-script
contract. Decompile each onload and button handler, pass the native vars it
reads, arm every junction and OK component on every mount, validate the route
server-side, and close/remount safely. If the VM lacks a genuinely general
widget-position/route primitive, add that tested general capability first; a
message-only quest shortcut is not an engine fallback. Square stones are
stackable quest items, cannot be dropped, and must reconcile with box flags,
inventory capacity, death, logout, and repeat searches.

After puzzle group one, Room 2 plays the full Red Axe listening conversation.
The east exit refuses until listening finishes; native `forget_room1_once` and
`forget_room1_listening` support entry/replay protection. After group two,
Room 4 requires reading the employee book and the two relevant papers before
the player may continue; `forget_room2_bookcase`, `forget_room2_paper1`, and
`forget_room2_paper2` are already present. Primary 115 and this entire evidence
room are missing locally. After group three, Room 6 runs the army reveal and
Grunsh memory-wipe cutscene, using the many dedicated native actors, then
returns the player to the tunnel entrance with state 120.

The local handler searches one box, then uses the same control loc three times
to write 105, 110, 118, and 119 while printing summaries. It does not use one
junction field, count, box flag, stone, alternate platform, return cart,
story exit, library loc, or cutscene actor. Its claim of “faithful compression”
is incompatible with Gate C and must be removed along with the shortcut.

### Memory loss, Veldaban, pub finale, and rewards

The player does not report the plot successfully. Grunsh erased the evidence
from their memory. At state 120, the player reaches Veldaban, forgets the
important message mid-sentence, and develops a craving for beer and a kebab;
Veldaban suspects a spell and primary becomes 130. Local Veldaban instead has
the player clearly describe a planned seizure, says the Consortium will be
warned, and praises the investigation. This reverses the story and destroys
the sequel hook. State 119 also jumps straight to 130, leaving declared state
120 dead.

At 130, the beer and kebab must be used in the Laughing Miner in the verified
order/allowed alternatives. The local Kebab Eat splice checks no zone, so it
can complete anywhere in Gielinor; it consumes both objects immediately and
prints two messages. Restore the full private pub scene with the player,
Drunken Dwarf, Nolar, Factory Worker, Gauss, Gunslik, chair/beer props, and
Colonel Grimsson's reveal. Protect public NPCs and other players, handle either
consumable's generic handler without branch theft, and settle the quest only
at the cutscene's canonical boundary. Cancel/logout/death/region change must
either resume the scene or return to retryable state 130 without double
consumption.

`~forget_quest_complete` writes 140, grants both XP rewards, adds two stout(m),
then calls the common completion API with no entry guard or settlement ledger.
A repeated direct call duplicates XP, items, QP, and completed count. Although
the finale consumes two unstackable items and normally frees two reward slots,
that coincidence is not atomicity. Reserve/reconcile reward capacity, award
5,000 Cooking XP, 5,000 Farming XP, two stout(m), two QP/count, state 140,
jingle, and scroll exactly once across repeat packets and every interruption.
Use the mature stout reward icon and retain ordinary item use after completion.

Post-quest Veldaban says the player seems bewitched and that they must wait and
see; the local joke line is invented and permanently steals all other Veldaban
topics. The current Wiki identifies no later OSRS quest prerequisite or unique
unlock beyond rewards as of the pinned revision. The important integrations
are upstream prerequisite enforcement, shared actors/transports/brewing, and
post-quest topic composition; do not invent a downstream gate.

### Journal and administrative adapters

The journal dispatch and `~quest_journal` API are modern, but its content is
not. It cannot display which friend/request/seed is pending, permission versus
growth, exact shared-vat phase, director company, one of nine puzzle routes,
listening/evidence requirements, or item recovery. Worse, after the memory
wipe it says the player uncovered and reported the plot. The current quest
journal becomes nonsense such as “Hi hi hi ha ha ha ha” and “Rock hot nice
butterfly bad wolf...” after Grunsh's spell. Rebuild every chapter from primary
plus native subfields, preserve the deliberately corrupted late journal, and
use standard completed styling.

`::complete quest_forgettabletale` is correctly an administrative state
adapter: first use writes 140 and awards row-derived QP/count in the common
cheat layer; repeat use is a no-op; it intentionally does not grant gameplay
XP/items or run cutscenes. Correct the row's Cooking metadata, preserve this
separation, and never use the gameplay completion proc from the cheat. A debug
state must not count as route verification.

## 6. Migration and recovery

This port has already written incompatible meanings into the native carrier,
so deployment needs an explicit one-time migration rather than simply changing
handlers:

1. Preserve completed state 140 and active state 130. Reconcile their XP,
   items, QP, and count through a settlement ledger/telemetry; aggregate totals
   cannot safely prove which unguarded calls ran, so never replay rewards
   blindly.
2. Preserve canonical 0/10/20 where secondary evidence agrees. Convert legacy
   25 to canonical 30, set `forget_beer_given`, and reconcile the first seed
   without creating duplicates.
3. Simultaneously swap legacy `forget_seed3_given` and
   `forget_seed4_given` meanings (Khorvak/Gauss) into native Gauss/Khorvak
   semantics. Preserve seed count and repair told flags from proven dialogue;
   do not clear an already correct imported combination.
4. Convert legacy farming chapter 30 to canonical 40 using
   `%forget_farming`, carried seeds/hops, and patch transform. A legacy 40 that
   already holds hops belongs to canonical brewing state 50, not Farming 40.
5. Translate legacy brew plateaus into native bytes only when no genuine
   shared brew conflicts: 43→vat 1, 44→2, 45→68, 46→69 with a durable
   fermentation deadline, 47→71, 48→barrel 3, and 50 with carried Kelda stout
   →canonical 60. Values 40/42 require item-aware empty-vat recovery. Never
   overwrite other brewing bytes or silently discard a pre-existing ale.
6. Convert legacy 60/65/70/80 to canonical 65/70/80/100 respectively after
   verifying stout hand-in, story, conductor, company, and board facts. Repair
   `forget_boarding_removed` only when the correct company transaction is
   evidenced.
7. Do not map shortcut 105/110/118/119 to equivalently numbered canonical
   chapters: those local values prove no rail route, listening scene, library
   evidence, or memory wipe. Resume at the earliest unverified tunnel checkpoint
   (normally canonical 100), preserve legitimate stones/box bits from imported
   saves, and provide a clear migration notice rather than granting fake
   evidence. Preserve state 120 only when a real/imported cutscene flag set
   proves the memory wipe.
8. Reconcile all quest items across inventory, bank, and owned ground items:
   four seeds, hops, stout, letter, square stones, and direction-correct
   tickets. Recovery actors must supply only live-defined missing items and
   must reserve capacity before changing ownership flags.
9. Replace lost session timers with persistent Farming/brewing deadlines. For
   an ambiguous legacy fermenting/growing state, choose a bounded retryable
   recovery and telemetry; never leave it dependent on a vanished timer.
10. On login/region entry, move players found inside a noncanonical hub or cave
    tile to the proper saved checkpoint, close orphaned puzzle/camera modals,
    rebuild private actors/transforms, and preserve the documented safe logout
    return to Keldagrim.
11. Correct the Cooking requirement from 20 to 22 in metadata, constants,
    journal, and runtime without rolling back already accepted quests. Active
    players remain accepted but must meet the boostable live-action gate.
12. Remove no state solely because the direct root did not read it. Imported
    native puzzle, evidence, gardener-task, company, and brew values outrank
    the shortcut port and must be preserved after validation.

## 7. Modernization sequence

### Gate A — repair shared routing and native state

1. Add transition fixtures for all canonical states/subfields, correct Cooking
   22 metadata, and implement the legacy migration before changing live reads.
2. Replace Veldaban's first-match branches with composed Giant Dwarf,
   Forgettable, progress, and post-quest topics; prove The Giant Dwarf can
   finish naturally and this quest can then start.
3. Restore the current start confirmation/title/camera/arrival transaction and
   shared GE/Keldagrim travel.
4. Restore beer/seed ownership and recovery, correct Gauss/Khorvak flags, and
   implement Rowdy's persisted random request.

### Gate B — shared Farming, brewing, and transport

1. Move the Kelda patch to normal Farming ops/transforms and persistent growth,
   including inspection, capacity-safe harvest, loss recovery, and the optional
   Rind/Elstan letter.
2. Drive the existing shared vat/barrel fields and multilocs, exact ingredient
   exchange, 22 Cooking gate, persistent fermentation, drain, and stout
   replacement; retire primary 42–48 authority.
3. Correct both ordinary minecart ticket directions, 100/50-coin fares,
   capacity-safe sale, ride presentation, and shared availability.
4. Bind the boarded-tunnel topic to all seven company directors and compose
   every shared NPC/menu without branch theft.

### Gate C — tunnel, cutscenes, and exactly-once completion

1. Implement interfaces 248/244/247 as modern mounted puzzles backed by all
   nineteen native junctions and server-validated routes.
2. Implement stones/boxes, physical cart routes/returns/dead ends, Room 2
   listening, Room 4 evidence, entry/exit checks, and cave logout recovery.
3. Build the start, Drunken Dwarf, tunnel entry, Room 6 memory wipe, and final
   pub scenes from native actors with private ownership and cleanup.
4. Restore state 119→120→130 semantics and a guarded, resumable, capacity-safe
   XP/item/state/QP/count/jingle/scroll settlement.
5. Rebuild the journal, corrupted-memory text, recovery dialogue, and
   administrative assertions from native facts.

### Gate D — regression and integration

1. Run the migration over every incompatible plateau, swapped seed bit, shared
   brew conflict, timer state, tunnel shortcut, item combination, and reward
   ambiguity.
2. Reverify Giant Dwarf and Fishing Contest prerequisites, Between a Rock
   Khorvak topics, every company director, ordinary minecarts, shared brewery,
   Farming, pubs, Kebab Eat, and Veldaban before/during/after both quests.
3. Run static pack/build, Quest Helper extraction, state/property tests,
   two-player isolation tests, and a real-client command-free 0→140 route.
4. Record captures for all puzzle mounts, camera/cutscene boundaries,
   completion scroll, recovery paths, logout returns, and post-quest dialogue
   before changing status from `audit-pending`.

## 8. Verification matrix

| Area | Required checks |
| --- | --- |
| Shared Veldaban | Giant Dwarf states before/at/after final report × Forgettable 0/active/complete; Fishing Contest incomplete/complete; all side topics; no branch theft; command-free prerequisite completion |
| Start | Prerequisites independently missing; Cooking/Farming below/at/boosted; accept/decline/re-talk; current sailor text; disconnect before/after commit; title/camera cleanup; exact state 10 and arrival |
| GE/Keldagrim travel | Trapdoor confirm/refuse; all quest states; real station arrival; collision/animation; no Veldaban shortcut; two players |
| First beer/seed | Beer/no beer/coins; bar purchase remains external; full inventory; carried/banked/dropped seed; repeated/stale Talk; exact beer bit and 20→30 commit |
| Rowdy | Every request item; unavailable item and timed reroll; request survives relog; right/wrong/no item; full inventory; repeated exchange; normal dialogue before/after |
| Khorvak | Free borrow and stout routes; real table spawn; all Forgettable × Between a Rock states; native seed 4; schematic topic preserved; full inventory/repeat |
| Gauss | Beer, empty glass, neither, and both; no vessel consumption; toast scene; native seed 3; normal pub topics; full inventory/repeat |
| Seed recovery | Zero through four carried/banked/ground seeds; every given/told combination; imported swapped flags; duplicates; death/relog; exact replacement count |
| Rind/patch | Permission/no permission; rake states 0–3; dibber/Barbarian hand planting; Farming 16/17 boosted; full inventory; offline/relog/restart growth; states 4–8; no disease/water/compost; inspect; harvest 9 |
| Optional letter | Offer/accept/refuse; full inventory; lose/recover letter; Elstan before/during/after; return during/after crop window; two marrentill seeds; final refusal semantics |
| Brewery | Pre-existing empty/finished/fermenting ordinary brew; pot spawn; 0/24/25 coins; pot/no pot; correct/wrong ingredient and order; empty buckets returned; Cooking 21/22 boosted; vat 0/1/2/68/69/70/71; barrel 0/3 |
| Brew timing/recovery | Logout/restart at every state; notification once; drain; full inventory; lost hops/yeast/glass/stout; Rind/barmaid replacement; repeat valve/use; two players' transforms |
| Ordinary carts | Correct direction ticket and conductor; 0/49/50/99/100 coins as applicable; Ring of charos; buy/refuse/full inventory; ticket loss/duplicate; outbound/return arrivals; non-quest use |
| Conductor/directors | Southern conductor first/re-talk; Veldaban hint; company values 0–7; correct and wrong director; all ordinary company topics; board transform once; shared stairs do not mutate Giant Dwarf |
| Secret cart | Boards present/removed; ticket irrelevant; one/both hands occupied; 0/1/2 free slots; ride/return/refuse; camera/collision; logout/death/teleport; two players |
| Puzzle UI | Each 248/244/247 mount/remount; every junction 0/1/2 cycle; native field/count sync; valid/invalid/dead-end path; OK/cancel; repeated packet; reconnect; resize mode; server route validation |
| Stones/boxes/carts | Starting and five box flags; correct green/yellow quantities; full inventory/existing stacks; Drop refusal; all nine platform routes and return carts; repeat search; death/relog reconciliation |
| Story rooms | Room 2 full dialogue and early-exit denial; Room 4 three evidence sources and early-exit denial; all evidence orders/repeats; Room 6 actors, memory wipe, state 120, and safe return |
| Journal | Every canonical primary × relevant seed/farming/brew/puzzle/evidence flag; loss recovery; optional letter; corrupted late text; completed styling; no invented successful report |
| Finale | State 119/120/130; Veldaban first/re-talk; kebab/beer either order per live trace; east-pub zone and outside refusal; all private actors/props; cancel/logout/death at every scene boundary |
| Completion | State 130/140 entry guard; 0/1/2 reward slots; repeated Eat/use/queue/login; exact 5,000+5,000 XP, two stout(m), 2 QP/count, state, jingle, icon, and scroll once |
| Post-quest/shared | Canonical Veldaban dialogue plus other topics; Khorvak/Rowdy/Gauss/pubs/directors ordinary topics; Kebab Eat generic effects; brewery/Farming/minecarts unaffected |
| Migration/admin | Every local 0/10/20/25/30/40/42–48/50/60/65/70/80/100/105/110/118/119/120/130/140 value × native flags/items/timers; swapped seeds; conflicting brew; `::complete` twice without XP/item replay |

Required static evidence includes a clean RuneScript/config build, duplicate-
trigger and unresolved-symbol scans, no shortcut/collapse comments in the live
path, native-state and migration fixtures, all seven director bindings, all
three interface event maps, shared-topic routing tests, and a clean Quest
Helper `--check`. Required runtime evidence is a command-free fresh 0→140
playthrough after completing both prerequisites normally; both Khorvak routes;
several Rowdy requests; offline crop and brew waits; all nine cart paths and
six rooms; loss/full/repeat/interruption cases; two players independently in
the brewery, puzzles, and scenes; completion and post-quest dialogue; and
regressions through Giant Dwarf, Between a Rock, Farming, brewing, ordinary
minecarts, and generic food consumption. A debug state, printed cutscene
summary, visible transform, or successful compile is not route proof.

## 9. Definition of done

Forgettable Tale... is modernized only when a player can complete The Giant
Dwarf through the same shared Veldaban, satisfy Fishing Contest, accept the
current quest and title scene, obtain and recover all four correctly owned
seeds through every valid friend route, grow Kelda hops through the native
durable patch, optionally complete Rind's letter, and brew/recover Kelda stout
through the shared visible vat/barrel. They must be able to use correctly paid
ordinary carts, consult the southern conductor and any valid company director,
enter hands-free with capacity, manipulate every native junction, obtain every
stone, ride all nine routes, listen/read all evidence, and undergo the real
memory-wipe and pub cutscenes. Primary 0–140 and all native subfields must agree
with Wiki/cache semantics; every loss, full inventory, logout, death, repeat
packet, shared-topic, imported save, and two-player case must recover without
debug writes, wrong fares/items, public actor mutation, skipped rooms, or stale
timers. State 140, 5,000 Cooking XP, 5,000 Farming XP, two stout(m), two quest
points/count, jingle, and scroll must settle exactly once, while Veldaban,
Khorvak, all directors, pubs, Farming, brewing, minecarts, Kebab Eat, journal,
and admin completion retain their other valid behavior.
