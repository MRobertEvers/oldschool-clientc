# Desert Treasure I modernization audit

Status: `audit-pending` — the quest has a dynamic journal, recognizable
dialogue and item routes, four boss actors, native mirror/column transforms,
the correct Magic XP amount, and a working post-quest altar gate. It is not
startable or completable through ordinary play. Asgarnia Smith is absent from
the world; the Ring of visibility writes an obsolete field instead of the
placed Shadow ladder's visibility field; the five ice trolls, cave entrance,
cold path, and spiked-boots ledge are not implemented; and the Ancient
Pyramid's central door has no handler. The source explicitly defers the
pyramid maze and its debug command teleports directly to Azzanadra.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to discovery, native persistence, requirements, the
archaeology and Bandit Camp route, all four diamond hunts, the Ancient
Pyramid, Azzanadra, rewards, recovery, shared systems, downstream unlocks,
journal, migration, and debug tooling. It is an implementation specification,
not completion evidence.

## 1. Authoritative references

The article and quick guide define the current critical path, item rules,
hazards, boss mechanics, recovery, rewards, and unlocks. The transcript is the
dialogue and re-talk authority. Revisions were resolved through the OSRS Wiki
API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Desert Treasure I](https://oldschool.runescape.wiki/w/Desert_Treasure_I?oldid=15292300) | 15292300, 2026-08-10 | Identity, requirements, full route, bosses, rewards, and unlocks |
| [Desert Treasure I/Quick guide](https://oldschool.runescape.wiki/w/Desert_Treasure_I/Quick_guide?oldid=15166630) | 15166630, 2026-04-06 | Exact critical path, item quantities, diamond order, and recovery |
| [Transcript:Desert Treasure I](https://oldschool.runescape.wiki/w/Transcript%3ADesert_Treasure_I?oldid=15292398) | 15292398, 2026-08-11 | Offers, refusals, re-talks, route dialogue, and completion |
| [Asgarnia Smith](https://oldschool.runescape.wiki/w/Asgarnia_Smith?oldid=15123075) | 15123075, 2026-02-07 | Start, etchings, translation hand-off, and recovery dialogue |
| [Terry Balando](https://oldschool.runescape.wiki/w/Terry_Balando?oldid=14922467) | 14922467, 2025-06-19 | Translation sequence and shared Dig Site routing |
| [Bartender](https://oldschool.runescape.wiki/w/Bartender?oldid=15258577) | 15258577, 2026-07-09 | Bandit Camp beer transaction and hostility context |
| [Eblis](https://oldschool.runescape.wiki/w/Eblis?oldid=15196248) | 15196248, 2026-04-25 | Materials, mirrors, diamonds, staff shop, signets, and post-quest service |
| [Etchings](https://oldschool.runescape.wiki/w/Etchings?oldid=15184406) | 15184406, 2026-04-22 | Early-route quest item and loss behavior |
| [Translation](https://oldschool.runescape.wiki/w/Translation?oldid=15282330) | 15282330, 2026-07-30 | Read operation, hand-off, and recovery behavior |
| [Blood diamond](https://oldschool.runescape.wiki/w/Blood_diamond?oldid=15239321) | 15239321, 2026-06-25 | Blood route reward and loss behavior |
| [Smoke diamond](https://oldschool.runescape.wiki/w/Smoke_diamond?oldid=15239318) | 15239318, 2026-06-25 | Smoke route reward and loss behavior |
| [Ice diamond](https://oldschool.runescape.wiki/w/Ice_diamond?oldid=14923425) | 14923425, 2025-06-21 | Ice route reward and Troll child recovery |
| [Shadow diamond](https://oldschool.runescape.wiki/w/Shadow_diamond?oldid=15239319) | 15239319, 2026-06-25 | Shadow route reward and ground persistence |
| [Malak](https://oldschool.runescape.wiki/w/Malak?oldid=15012979) | 15012979, 2025-11-01 | Blood-route contract and diamond recovery |
| [Ruantun](https://oldschool.runescape.wiki/w/Ruantun?oldid=15149178) | 15149178, 2026-03-15 | Silver-pot transaction |
| [Silver pot](https://oldschool.runescape.wiki/w/Silver_pot?oldid=14362868) | 14362868, 2023-01-16 | Pot preparation stages and item operations |
| [Garlic powder](https://oldschool.runescape.wiki/w/Garlic_powder?oldid=15187409) | 15187409, 2026-04-22 | Blood-route preparation |
| [Dessous](https://oldschool.runescape.wiki/w/Dessous?oldid=15272137) | 15272137, 2026-07-22 | Combat speed, combo attack, silver bonus, and rematch policy |
| [Smoke Dungeon](https://oldschool.runescape.wiki/w/Smoke_Dungeon?oldid=15233177) | 15233177, 2026-06-13 | Entry, smoke hazard, face protection, and post-quest access |
| [Standing Torch (Smoke Dungeon)](https://oldschool.runescape.wiki/w/Standing_Torch_%28Smoke_Dungeon%29?oldid=15232813) | 15232813, 2026-06-13 | Lighting success, burnout, and chest access |
| [Warm key](https://oldschool.runescape.wiki/w/Warm_key?oldid=15184425) | 15184425, 2026-04-22 | Chest recovery and Fareed-gate persistence |
| [Fareed](https://oldschool.runescape.wiki/w/Fareed?oldid=15199315) | 15199315, 2026-04-28 | Water weakness, gloves, disarm, ashes, magic, and recovery |
| [Troll child](https://oldschool.runescape.wiki/w/Troll_child?oldid=15232906) | 15232906, 2026-06-13 | Food variants, parents, reunion, and diamond recovery |
| [Spiked boots](https://oldschool.runescape.wiki/w/Spiked_boots?oldid=15239316) | 15239316, 2026-06-25 | Ice-path equipment requirement |
| [Kamil](https://oldschool.runescape.wiki/w/Kamil?oldid=15292399) | 15292399, 2026-08-11 | Fire weakness, freezing attacks, and arena behavior |
| [Rasolo](https://oldschool.runescape.wiki/w/Rasolo?oldid=15122155) | 15122155, 2026-02-06 | Gilded-cross task and ring replacement |
| [Secure chest](https://oldschool.runescape.wiki/w/Secure_chest?oldid=15254668) | 15254668, 2026-07-05 | Lock sequence, failure damage/poison, XP, and recovery |
| [Gilded cross](https://oldschool.runescape.wiki/w/Gilded_cross?oldid=15184410) | 15184410, 2026-04-22 | Shadow-route hand-off and loss behavior |
| [Ring of visibility](https://oldschool.runescape.wiki/w/Ring_of_visibility?oldid=15183687) | 15183687, 2026-04-22 | Ladder visibility, continuous equip rule, and replacement |
| [Damis](https://oldschool.runescape.wiki/w/Damis?oldid=15210041) | 15210041, 2026-05-12 | Two forms, prayer drain, route monsters, and diamond recovery |
| [Ancient Pyramid](https://oldschool.runescape.wiki/w/Ancient_Pyramid?oldid=15276680) | 15276680, 2026-07-27 | Four floors, monsters, traps, ladders, temple, altar, and back door |
| [Azzanadra](https://oldschool.runescape.wiki/w/Azzanadra?oldid=15301694) | 15301694, 2026-08-15 | Finale dialogue and post-quest topics |
| [Ancient Magicks](https://oldschool.runescape.wiki/w/Ancient_Magicks?oldid=15299490) | 15299490, 2026-08-14 | Immediate unlock and spellbook semantics |
| [Altar (Ancient Pyramid)](https://oldschool.runescape.wiki/w/Altar_%28Ancient_Pyramid%29?oldid=14918351) | 14918351, 2025-06-12 | Spellbook switching and Prayer drain |
| [Ancient staff](https://oldschool.runescape.wiki/w/Ancient_staff?oldid=15301378) | 15301378, 2026-08-14 | Eblis's 80,000-coin post-quest sale |
| [Ancient sceptre](https://oldschool.runescape.wiki/w/Ancient_sceptre?oldid=15275937) | 15275937, 2026-07-26 | Secrets of the North gate and icon/staff upgrade |
| [Recipe for Disaster](https://oldschool.runescape.wiki/w/Recipe_for_Disaster?oldid=15302153) | 15302153, 2026-08-15 | Culinaromancer requirement |
| [The Frozen Door](https://oldschool.runescape.wiki/w/The_Frozen_Door?oldid=14727875) | 14727875, 2024-08-26 | Post-quest God Wars unlock |
| [Desert Treasure II - The Fallen Empire](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire?oldid=15303590) | 15303590, 2026-08-16 | Sequel requirement and shared identity/transforms |

Transition aid only: the local Quest Helper snapshot at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/deserttreasure)
maps native states 0–14, about 70 route points, and the native blood, smoke,
ice, shadow, mirror, and column fields. Its extraction check currently reports
four unresolved `_4d_standing_torch*` ObjectIDs because the extractor does not
normalize their leading-underscore gameval names; the cache symbols are
`4d_standing_torch1..4`. Treat the route as useful corroboration and the
extractor mismatch as tooling debt, not server proof.

## 2. Canonical contract

Desert Treasure I is a members, master, long quest released 18 April 2005. It
starts with Asgarnia Smith at the Bedabin Camp and belongs to the Mahjarrat
series. The player needs 50 Magic to start. The route later needs 53 Thieving,
50 Firemaking, and either 10 Slayer or a gas mask from Plague City. Magic,
Thieving, and Slayer are not boostable; Firemaking is boostable.

The prerequisite quests are The Dig Site, Temple of Ikov, The Tourist Trap,
Troll Stronghold, Waterfall Quest, and Priest in Peril. Required supplies are
650 coins, 12 magic logs, six steel bars, six molten glass, ashes, charcoal,
one blood rune, and bones. The logs, bars, and glass may be submitted as noted
items. Combat, lockpicks, food for the Troll child, a silver bar, garlic,
spices, a face covering, ice gloves, cakes, climbing boots/spiked boots, and
other route equipment are required or strongly implied by the chosen path.

A canonical run must:

1. accept Asgarnia's commission, take the etchings to Terry Balando, complete
   the translation sequence, read/return the translation, and learn of the
   Bandit Camp;
2. buy the bartender's 650-coin drink, learn of Eblis, deliver all six material
   types, and talk to Eblis at his six mirrors;
3. complete the Blood, Smoke, Ice, and Shadow diamond routes in any order,
   with the Stranger attack, hazards, bosses, lost-item recovery, and per-route
   durable state working independently;
4. insert exactly one of each diamond into the correct exterior obelisk,
   preserving the four native column transforms;
5. traverse all four floors of the Ancient Pyramid, including mummy/scarab
   interruptions and random trap ejection, and open the central temple door;
6. talk to Azzanadra, complete exactly once, and immediately switch to Ancient
   Magicks; and
7. retain canonical recovery and unlocks: the back door and altar, Ring of
   visibility replacement, Eblis's ancient staff/signets service, Smoke
   Dungeon access, Nightmare Zone bosses, and downstream requirements.

Completion awards 3 quest points and exactly 20,006.9 Magic XP. The completion
screen should describe unlocks but must not present coins as an awarded item.
Every item hand-off, boss lifecycle, floor transition, completion queue, and
lost-item recovery must be safe across a full inventory, interruption, death,
logout, reconnect, and two simultaneous players.

## 3. Native identity, requirements, and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 79 / 27 |
| Dbrow | `quest_deserttreasure` |
| Type / difficulty / length | Members; master; long |
| Series / release | Mahjarrat #7; 18 April 2005 |
| Start | `fourdiamonds_indiana` (metadata NPC 684), (3178, 3042, plane 0) |
| Prerequisite dbrow indices | Dig Site 29; Temple of Ikov 146; Tourist Trap 148; Troll Stronghold 153; Waterfall Quest 158; Priest in Peril 111 |
| Skill metadata | 53 Thieving; 50 Magic; 50 Firemaking; 10 Slayer |
| Recommended combat | 70 |
| Primary field / end state | `%deserttreasure`, `deserttreasuremain` bits 0–14 / 15 |
| Rewards | 3 QP; 200,069 tenths of Magic XP |

### 3.1 Requirement defects

`~dt_has_requirements` currently calls `stat()` for all four skills, requires
all four before the offer, and accepts boosts for each. That permits forbidden
Magic, Thieving, and Slayer boosts; refuses players who legitimately use a gas
mask instead of 10 Slayer; and refuses a player at the start for Thieving or
Firemaking even though only Magic is a start requirement. Implement two
checks: an exact offer/start gate and contextual use-site gates for each later
obstacle. Use base-level checks for non-boostable skills and the current
effective Firemaking level where the live rule permits a boost.

The implementation checks five prerequisite quests and has a stale comment
claiming The Dig Site has no carrier. Revision 239 metadata includes packed
dbrow 29 and the repository has `%itexam`; require it. The journal already
lists The Dig Site, so current dialogue, metadata, and start logic disagree.
Present a useful requirement summary and refusal instead of merely leaving
Asgarnia “lost in thoughts.”

### 3.2 Native primary ladder

| `%deserttreasure` | Native checkpoint | Current local interpretation |
| ---: | --- | --- |
| 0 | Not started; talk to Asgarnia Smith | `dt_not_started` |
| 1 | Etchings obtained; talk to Terry | `dt_etchings` |
| 2 | Translation conversation in progress | `dt_translating` |
| 3 | Continue/finish Terry's translation | `dt_have_translation` |
| 4 | Bring/read translation with Asgarnia | `dt_read_notes` |
| 5 | Translation accepted; seek Bandit Camp | `dt_bandit_camp` |
| 6 | Buy the special drink | `dt_heard_diamonds` |
| 7 | Talk to the bartender after buying it | `dt_gather_mirrors` |
| 8 | Find/talk to Eblis | Unused |
| 9 | Deliver Eblis's materials | Unused |
| 10 | Talk to Eblis at the mirrors | `dt_mirrors_ready`; also all four hunts |
| 11 | Obtain the four diamonds in any order | Unused |
| 12 | Place the diamonds in the four columns | Unused |
| 13–14 | Pyramid and Azzanadra finale checkpoints | Only old `dt_pyramid = 13`; exact split untraced |
| 15 | Complete | `dt_complete` |

The current 2009scape-derived ladder compresses and repurposes revision-239
states. It remains at 10 throughout all diamond routes, so native client and
Quest Helper consumers continue to describe “talk to Eblis at the mirrors”
even after bosses die. Do not guess the exact boundary between native 2/3 or
13/14. Capture a live revision-239 trace of every dialogue/cutscene commit,
then make all source constants match it.

### 3.3 Native side fields

| Carrier | Native field(s) | Current use / defect |
| --- | --- | --- |
| `deserttreasurevarbit` | `%fd_trollchild_intro`, four torch bits, `%fd_firechest`, and material counters for logs, bars, glass, bones, ashes | Materials and torches are used; troll intro and fire chest are not |
| `deserttreasurevarbit2` | charcoal, blood rune, Blood cutscene/subquest, fire warnings, Fareed killed, Kamil killed, five-troll count, boots hint, parents freed, Ice subquest, Shadow quest, chest disarmed, Shadow intro, fire gate | Some item/parent/chest fields are used; most route progress is duplicated in custom vars |
| `deserttreasuremain` | primary state, four column bits, Zaros-staff flag, mirror-present bit, obsolete ladder bit, pyramid timer | Primary/columns/mirrors are used; staff/timer are not; obsolete ladder bit is written |
| DT2 tertiary carrier | `%shadow_realm_visibility` | Drives the placed Shadow ladder but has no DT1 writer |

The implementation correctly avoids whole-carrier writes, so modernization is
not a side-bit-erasure problem. It is a semantic split-brain problem. Permanent
custom fields `dt_bought_beer`, `dt_blood_stage`, `dt_smoke_stage`,
`dt_smoke_gate`, `dt_ice_stage`, and `dt_shadow_stage` duplicate native cache
fields while native transforms and external consumers read the originals.
Temporary `dt_fareed_fighting`, `dt_kamil_fighting`, and `dt_damis_fighting`
are also too weak to represent durable encounter ownership or continuation.

### 3.4 One-time save migration

Migration must be versioned, idempotent, and executed before modern handlers
interpret either state family. Snapshot every authored custom and native field,
owned quest item, bank item, and boss-continuation signal before writing.

| Current primary | Native target / decision rule |
| ---: | --- |
| 0–1 | Preserve as native 0–1 |
| 2 | Map to native 2 or 3 only after the Terry dialogue trace distinguishes the checkpoint |
| 3 | Native 4 |
| 4 | Native 5 |
| 5 | Native 6 if the drink is not bought; native 7 if `dt_bought_beer = 1` |
| 6 | Native 8 |
| 7 | Native 9 |
| 10 | Native 10 before the mirrors dialogue; infer 11 only from durable diamond-route evidence after an exact trace |
| 13 | Native 13 or 14 from durable pyramid position/progress after the finale trace |
| 15 | Native 15 |

Map Blood stages offered/agreed/killed/complete into the traced native
`%fdvw_subquest` sequence; Quest Helper corroborates values 1/2/3/4. Map Smoke
key/chest/gate/kill evidence into `%fd_firechest`, `%fd_firewargate`, and
`%fd_killed_firewarrior`. Map Ice cake/agreement/Kamil/reunion/complete into
the native 1–5 Ice subquest and Kamil-dead flag. Map Shadow fetch/unlocked/ring/
complete into its traced native sequence; Quest Helper corroborates talked-to-
Rasolo 2, ring at least 3, and Damis killed 5. Preserve material counts,
columns, mirrors, parent states, and chest-disarmed evidence.

Never fabricate the absent five ice-troll kills, a completed pyramid floor, a
boss kill, or an item grant. If state 10/13 evidence is ambiguous, retain a
versioned compatibility checkpoint and route the player to a safe re-talk or
re-entry point. Test every old primary/custom combination, held/banked/ground
quest items, repeated login, and partial completion before deleting custom
fields.

## 4. Implementation and ownership surface

The quest root contains three scripts and two config files, 2,017 lines total.
The primary route lives in the 1,622-line `deserttreasure.rs2`; several
critical steps and every unlock cross into shared systems.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `quest_deserttreasure/configs/deserttreasure.constant` | Old primary/custom stages, coordinates, combat constants | Primary ladder is incompatible; comments document several approximations |
| `quest_deserttreasure/configs/deserttreasure.varp` | Native carriers plus authored custom state | Safe bit carriers, but duplicated route truth |
| `quest_deserttreasure/scripts/deserttreasure.rs2` | Main route, bosses, journal, completion, debug | Recognizable skeleton with deterministic discovery, route, and finale blockers |
| `quest_deserttreasure/scripts/ancient_sceptre.rs2` | Eblis staff/icon combine | Missing Secrets of the North gate; not a substitute for staff/signets service |
| `quest_deserttreasure/scripts/ancient_sceptre_elemental.rs2` | Quartz variants | Missing DT2 gate and passive effects |
| `quest_itexam/scripts/archaeological_expert.rs2` | Terry Balando and Dig Site sharing | DT1 routing exists; needs native-state/recovery regression |
| `areas/entrana/scripts/high_priest_of_entrana.rs2` | Blessing the silver pot | Shared item step exists; must preserve all other priest topics |
| world spawn files | Quest NPC discovery | Asgarnia base/visible form is absent; most later route NPCs exist |
| ladder/maplink system | Smoke Dungeon, pyramid floors, and back-door travel | Static links exist, but links do not implement route gates or pyramid encounters |
| combat damage/magic/poison systems | Boss damage, spell typing, poison | Current handlers cover only fragments of canonical mechanics |
| `skill_magic/scripts/spellbook_switch.rs2` | Ancient altar | Completion gate works; Prayer drain and immediate completion switch do not |
| POH spellbook altars / spellbook swap | Post-quest Ancient access | Completion consumers exist and need migrated-state regression |
| Nightmare Zone | Boss availability | Dream roster/instance remains deferred; DT bosses are not a working unlock |
| RFD finale / Frozen Door / DT2 | Downstream completion consumers | Some gates exist; sequel and shared presentation are incomplete independently |

The cache contains the native Asgarnia transform family, six mirrors, four
columns, route items, all four bosses and Damis forms, troll family, Shadow
ladder, ice cave entrance, gates, pyramid ladders, temple door, mummies,
scarabs, Azzanadra, altar, animations, and many presentation fields. Prefer
symbolic revision-239 identities and cache transforms. A hand-spawn or
teleport is not evidence that the world route works.

Static tooling corroborates the key presentation split. `loc_var_audit.py`
classifies the placed torch, mirrors, and Blood column as `implemented`; the
five-troll cave field is `unwritten`; and the hidden Shadow ladder is an
`op_bound_gap` because its visible child has an operation handler but
`%shadow_realm_visibility` has no writer. `%fd_ladder_present` has no loc
transform at all. `check_quest_combat_contract.py` exits 0 for its global
145-unit ledger, but Desert Treasure I's manifest entry is still
`audit-pending` with empty source, gameval, handler, test, and gap evidence.
That global pass proves none of the four boss contracts in this audit.

## 5. Discovery, Asgarnia, and Terry Balando

The metadata start NPC is base `fourdiamonds_indiana`; its visible child is
`fourdiamonds_indiana_vis`, selected by a DT2-backed transform. Neither the
base NPC nor an appropriate symbolic `npc_add` is present in world spawns. A
fresh player therefore cannot discover or start the quest at Bedabin Camp.
Restore the symbolic base actor at the canonical location and let its native
transform choose the visible form. Do not spawn the numeric child directly,
because that would bypass the DT2 visibility contract.

Asgarnia's first hand-off adds etchings and advances state without proving
inventory capacity or a successful add. A full inventory can strand the
player after acceptance. There is no robust lost-etchings branch. Make the
offer, refusal, requirement summary, item grant, and state commit one
transaction; use inventory/bank ownership rules before replacement.

Terry's shared Talk-to route prioritizes DT2, then DT1, then The Dig Site.
Preserve that additive ownership explicitly for every combination of the
three quest states. The present state 1–3 dialogue recognizes translation
progress, but its loss check is inventory-only and can duplicate a banked
translation. The translation item has a Read operation but no handler, and
Asgarnia does not distinguish the canonical read/return checkpoint. Implement
the native operation, scene/dialogue states, bank-aware recovery, cancellation
continuation, and exact state 2/3 trace before migration.

## 6. Bandit Camp, bartender, Eblis, and mirrors

The bartender path removes 650 coins, adds the special drink, and sets a
custom beer bit. Modernize it as a capacity-safe transaction and commit native
state only after the drink has been delivered and the follow-up dialogue is
reachable. Regression-test Bandit Camp's god-symbol hostility, ordinary bar
topics, death, destroy, bank, and re-talk behavior rather than treating the
bartender as quest-exclusive.

Eblis uses native counters for 12 magic logs, six steel bars, six molten glass,
bones, ashes, charcoal, and one blood rune. The counted hand-offs are useful,
but logs, bars, and glass accept only unnoted item IDs. Canonical noted
submissions must be accepted, with exact remaining counts and no over-delete.
All six categories need atomic deletion/count commits, mixed noted/unnoted
tests, interruption continuation, capacity-neutral behavior, and a precise
status response.

`%fd_mirror_present` drives all six placed mirror transforms and is both read
and written. The mirrors themselves have no Look-into handlers or viewing
cutscenes. Although looking into each is optional, the six native operations
and presentation should exist and remain per-player. Talking to Eblis at the
mirrors must advance native 10 to 11 at the traced point and leave the four
diamond routes independent and order-free.

## 7. Shared diamond and column contract

The level-95 Stranger who may randomly attack while a player carries one or
more diamonds is absent. The cache actor `fourdiamonds_assasin` exists, but no
spawn, timer, ownership, or bank-sensitive trigger uses it. Implement the
canonical random attack as a player-owned encounter with region, safety,
logout, death, teleport, multi-diamond, and no-diamond cleanup. A banked
diamond must not trigger it.

Each diamond has a different canonical loss policy. Blood and Ice are reclaimed
through Malak and the Troll child after completion of their route. Smoke and
Shadow remain or reappear at their boss locations under their respective
ground-item rules. Do not replace this with unconditional `inv_add` recovery.
Centralize ownership checks over inventory, worn slots where relevant, bank,
player-owned ground items, pending transactions, inserted columns, and
completed route state.

All four exterior columns use native bits and transform correctly. Current
Use handlers have no primary-stage gate, so any player with a diamond can
insert it or pre-set presentation. Require native state 12 and the correct
item/column pair. Delete exactly one diamond only after validating the target,
then set the column bit atomically. Handle interruption, duplicate use, two
players, and logout between delete/commit. When all four are set, advance to
the traced pyramid state without skipping native 11/12.

## 8. Blood diamond: Malak, Ruantun, Entrana, and Dessous

Malak, Ruantun, the Entrana High Priest, and Dessous's grave are world
reachable. The authored `dt_blood_stage` replaces native `%fdvw_subquest`, so
client state and native consumers do not follow the conversation. Migrate and
then use only the native subquest/cutscene fields.

The preparation chain must enforce exactly one silver-bar-to-pot conversion,
blessing, blood filling, garlic powder, and spice progression. Ruantun can
currently convert repeated silver bars because he lacks held/bank ownership
guards. Malak accepts either blessed or unblessed pot for the blood step, and
the shared priest can bless later variants; trace and reproduce the canonical
order and each item operation. Every delete/add must be capacity-safe and
idempotent. Re-talks must identify the precise missing ingredient without
granting duplicate pots.

Using the completed pot on the tomb globally changes the grave for 50 ticks
and hand-spawns a global Dessous with no player owner. Two players can observe,
suppress, damage, or complete one another's encounter. Replace it with a
player-owned encounter/instance or equivalent ownership-safe lifecycle.
Leaving or dying canonically requires preparing another pot; reconnect and
actor-expiry logic must preserve that cost without cross-player state.

Dessous currently uses generic melee and receives a 10% silver-weapon damage
bonus through the shared damage funnel. Canonical combat also has a fast
three-tick melee attack up to 19 and an unavoidable combined ranged/magic
attack that deals two hits of 5 when distance/position demands it. Implement
the attack selection, projectiles, paired damage, target ownership, resets,
and safe death transition. Audit the silver weapon family against current
items rather than freezing the old list.

Dessous's death writes only the custom killed stage. Malak then adds the Blood
diamond directly and checks inventory but not bank on recovery, allowing
banked duplicates. Commit native killed/subquest state from the owned actor,
then make Malak's first grant and canonical replacement bank-aware,
capacity-safe, and exactly once. Implement the unused Blood cutscene field if
the revision-239 trace requires it.

## 9. Smoke diamond: hazard, torches, Fareed, and recovery

The generic maplink reaches the Smoke Dungeon, but the dungeon's defining
hazard is absent. Canonically an unprotected player takes 20 damage every 12
seconds, stopping at one hitpoint; a face mask, gas mask, or qualifying Slayer
helmet protects them. Implement a region-owned repeating hazard with exact
equipment-family checks, no lethal tick, clean region/logout cancellation,
and regression against the generic poison/damage systems.

The four standing torches use native bits and soft timers, but any player can
light them regardless of quest state. At level 50 they always succeed rather
than using the canonical Firemaking success roll. The authored 250-tick
burnout came from 2009scape and needs a live revision-239 timing trace. More
seriously, a logout can discard a soft timer while leaving its permanent torch
bit lit. Make each timer reconstructible or normalize it on logout/re-entry;
make all four player-specific; and test boosts, failure, tinderbox, simultaneous
players, burnout order, logout, death, and chest eligibility.

The burnt chest uses custom `dt_smoke_stage` instead of `%fd_firechest` and
adds a warm key on every eligible click without ownership or capacity checks.
Canonical loss recovery lets the player reclaim a warm key without relighting
the torches. Encode that in the native chest field, distinguish held/banked/
genuinely lost keys, and never duplicate one.

The Fareed gate uses custom `dt_smoke_gate`, not native `%fd_firewargate`.
After the first key use, leaving before the kill must allow re-entry without a
new key. Validate, consume, and commit that transition atomically and use the
native field for all presentation and access checks.

Fareed is a global hand-spawn with a temporary fight guard. If the actor
expires or the player escapes, the guard can remain set until logout and block
a rematch; another player can also interfere. Build an owned encounter with
durable re-entry and cleanup. Current water weakness recognizes Strike, Bolt,
Blast, and Wave but omits Water Surge. The fight also lacks the rare magic
attack and the canonical conversion of non-ice ranged ammunition into ashes.
Preserve the ice-glove weapon rule, but verify all equipped/worn transitions,
manual spell entry points, splash behavior, and modern water-spell family.

On death the script advances custom state even if the direct diamond add
fails. Canonical Smoke-diamond recovery is location/ground based in the
chamber. Commit `%fd_killed_firewarrior` from the owned Fareed death, create a
player-owned recoverable ground item, and prove full inventory, death, region
exit, logout, decay, reclaim, bank, column insertion, and duplicate prevention.

## 10. Ice diamond: Troll child, five trolls, Kamil, and parents

The Troll child exists and its transform/native Ice subquest is partially
used. The food handler accepts only a full chocolate cake. Canonical options
include cakes and partial cakes, chocolate cake portions, chocolate bars,
cooking apples, and pineapple pizza. Implement the exact accepted family and
consumption amount, preserving the selected dialogue and inventory semantics.
Agreement currently writes only `dt_ice_stage`, leaving native subquest value
1 instead of the expected post-dialogue value 2.

The next route is a deterministic blocker. `%fd_icewarrior_trollskilled` has
no reader or writer even though a placed cave entrance transforms only when
the count reaches five. There are no quest handlers for the cave entrance,
the spiked-boots ledge blank model, or the small ice gate. The current first
surface gate instead hand-spawns Kamil immediately, skipping five trolls, the
cave, cold path, and ledge.

Implement the native path in order: owned qualifying ice trolls, count 0–5,
cave reveal/entry, environmental cold, Kamil's arena, spiked-boots climb,
slipping path, parent blocks, reunion, and recovery. The cold region must drain
stats, run energy to zero, special-attack energy to zero, and deal one damage
at canonical intervals/limits. It needs equipment and warming exceptions,
region exit, logout, death, teleport, and reconnect tests.

Kamil is currently a global hand-spawn in the wrong sequence. His magic is an
accuracy-based 0–5 roll that freezes for ten ticks on one-third of attack
cycles, while other combat is generic. Canonical Kamil uses strong melee and
frequent freezing magic, especially at distance, is vulnerable only to fire
spells, and has anti-blocking/arena behavior. Add Fire Surge to the modern
spell family and trace exact revision-239 attack, freeze, movement, and
re-entry semantics before tuning constants.

The parent ice blocks currently disappear from an option click without hit
points or combat. Implement the two 10-HP combat objects, progress only from
their legitimate destruction, and isolate them per player. Require spiked
boots at the ledge and model slips/falls and path reset. The reunion currently
teleports and advances even when the direct Ice-diamond add fails. Commit
native subquest 4/5 safely and make the Troll child's canonical replacement
check inventory, bank, ground/pending ownership, and inserted column state.

## 11. Shadow diamond: chest, visibility, Damis, and recovery

Rasolo and the Bandit Camp secure chest exist. Their route uses custom
`dt_shadow_stage` instead of `%fd_shadowwarrior_quest`. Migrate the native
intro, chest-disarmed, ring, and killed checkpoints and preserve ordinary
Rasolo topics.

The chest correctly requires a lockpick and runs three independent lock
checks, but failure always breaks the lockpick and deals fixed damage. The
canonical failure deals 2–3 damage and can poison; success awards 150 Thieving
XP. The XP and poison chance are absent. Implement the exact per-lock chance,
failure reset, damage, poison, tool breakage, success XP, interruption, and
full-inventory behavior. Its cross recovery checks inventory and bank, which
is a useful basis, but must also cover pending/ground ownership and never
duplicate the item.

Rasolo's cross-to-ring exchange is slot-neutral but writes only custom state.
There is no post-loss Ring of visibility replacement even though canonical
Rasolo supplies replacements indefinitely. Make the exchange and replacement
native, bank/worn-aware, capacity-safe, and compatible with later quests that
also use visibility rings.

The current route cannot reveal its ladder. The placed `fd_shadowladder1`
transforms on `%shadow_realm_visibility`; value 0 hides it. No script reads or
writes that field. Equipping the ring instead writes `%fd_ladder_present`, an
obsolete field with no placed loc transform, and never clears it. Replace the
equip/unequip hook with the shared visibility contract that owns the actual
field, including worn-ring checks, login reconstruction, unequip, death,
destroy, bank, and DT2/Ring of shadows interoperability.

The dungeon spawn must require the ring to remain equipped. Damis currently
uses global hand-spawned forms and a temp guard; phase-one death globally
spawns phase two. The harder form drains 5% plus one current Prayer once per
attack cycle, while canonical behavior drains every tick when it is attacking
within melee distance. Create an owned two-phase encounter, retain the same
owner/UID contract across form change, implement tick-accurate drain, and
handle escape, ring removal, logout, death, actor expiry, and another player's
damage.

Current completion directly adds the Shadow diamond. Canonically it remains
on the ground in the boss location when the player leaves. Use a player-owned
ground recovery contract, native killed state, and ownership checks through
bank/column insertion. Regression-test the multicombat route monsters as
shared world content without letting them own or complete the boss encounter.

## 12. Ancient Pyramid and Azzanadra

The exterior entrance and all four column transforms exist. Generic maplinks
cover several internal ladders, and static mummies are placed across the
floors. That is only the map skeleton. The source says, “Pyramid
traps/sarcophagus maze deferred; ::deserttreasure jumps to Azzanadra.”
`%fd_pyramid_timer` has no reads or writes. There are no scarab floor
eruptions, sarcophagus mummy popouts, movement interruptions, or random
pitfall/trap ejections.

Build the four-floor traversal from native locs and a player-specific pyramid
timer/state machine. Each floor needs its exact ladder route, scarab chance,
sarcophagus behavior, mummy aggression, interruption, trap destination,
floor-entry checkpoint, logout/reconnect reconstruction, and death/teleport
cleanup. Static shared mummies can remain only where canonical; spawned
hazards and presentation must not leak between players.

The central `dt_ancient_temple_door_open` advertises Open but has no operation
handler or generic-maplink category. It is therefore a second deterministic
pyramid blocker even though the ladders exist. Implement the native state and
side checks, door animation/transition, cancellation, and safe return. Prove a
fresh player can reach it from the exterior without `::deserttreasure` or a
manual teleport.

Azzanadra's base spawn exists in the chamber and transforms visible at native
main 13/14. The current completion conversation is shortened and does not
prove the full scene, camera, music, animation, or native 13/14 boundary.
Implement the pinned transcript and capture the exact cache/client transition.
Interruption at each dialogue/cutscene step and reconnect in the chamber must
resume without replaying rewards or trapping the player.

## 13. Completion, rewards, and post-quest services

The completion queue awards the correct 3 QP and 200,069 fixed-point Magic XP,
but it has no explicit idempotence token. It writes main 15 before queuing the
shared reward scroll, which prevents the ordinary dialogue from being repeated
but does not prove duplicate queued delivery/reconnect safety. Commit one
durable completion boundary and make quest points, completed-quest count, XP,
and scroll delivery exactly once under cancellation, logout, reconnect, and
duplicate queue execution.

Canonical completion immediately switches the player to Ancient Magicks. The
current queue never writes `%spellbook`; it only unlocks later altar use. Set
the Ancient book as part of the one-time completion transaction and redraw it
safely. The reward scroll uses a coins icon despite awarding no coins; use an
appropriate non-item presentation and verify exact reward text.

The Ancient Pyramid back-door maplink and completion-gated altar operation
exist. `spellbook_switch.rs2` toggles Ancient/standard but never drains Prayer
to zero, contrary to the altar's canonical contract. Fix this in the shared
spellbook-switch owner without changing Lunar, Arceuus, POH, or Spellbook Swap
semantics unintentionally. Test completion, book already active, zero/nonzero
Prayer, full interface state, and reconnect.

Eblis lacks the canonical 80,000-coin ancient staff sale, lost Ring of
visibility service, and free ancient signets. `%fd_got_zaros_staff` is unused.
Implement each post-quest topic with inventory, worn, bank, staff-family,
pending-transfer, capacity, and coin checks; never charge before a successful
grant. Determine whether the native staff bit represents first purchase,
ownership, or presentation from cache/client trace.

Nightmare Zone currently defers its dream-boss roster and instances, so
Dessous, Fareed, Kamil, and Damis are not a functional reward despite the
quest's expectation. Add them only through the NMZ owner's modernization and
combat contract; never expose world-route quest actors as replay bosses.

The local ancient-sceptre combine handler permits Ancient icon plus ancient
staff after DT1 alone and even says the Secrets of the North requirement is
deferred. Canonically the upgrade needs Secrets of the North. Its elemental
variants also defer DT2 gates and passive spell effects. Gate and prove these
post-quest systems in their owning quest/content slices rather than granting
them as DT1 completion rewards.

## 14. Downstream and shared-system integration

Recipe for Disaster's Culinaromancer finale reads DT completion, and the
Frozen Door login route begins after completion. POH spellbook altars,
Spellbook Swap, and the Ancient altar also consume it. Preserve all of these
after the primary-state migration and add focused before/active/complete tests.

The canonical quest is also required for The Curse of the Empty Lord,
Hopespear's Will, Desert Treasure II, and multiple hard/elite diary tasks. The
first two and relevant diary surfaces are absent or incomplete in this
repository and cannot count as implemented unlocks. DT2 exists but is itself
shallow and must be audited separately; its transforms already affect
Asgarnia and Shadow visibility, so shared-field ownership must be explicit.

Do not turn missing consumers into DT1-owned monoliths. Export one stable
native completion predicate and route-specific public services, then fix each
consumer in its owning module. Regression-test shared Terry, Eblis, altar,
visibility, world maplink, desert heat, poison, item-drop, magic-damage, and
boss-replay systems with unrelated content.

## 15. Journal, debug, and recovery adapters

The journal is dynamically registered through `~deserttreasure_journal`, so
no legacy interface migration is needed. Its content follows the compressed
primary and custom stages. At state 10 it reports authored diamond paths while
native consumers still see the mirrors checkpoint. Its not-started section
lists The Dig Site despite the start check omitting it and describes all four
skills as unconditional start requirements.

Rewrite the journal from native main and side fields after migration. Show the
correct start-only Magic requirement and later boosted/non-boosted rules; exact
remaining Eblis materials; mirror/diamond/column status; Blood pot ingredient;
torch/chest/gate state; troll count, cold path, boots, parents; Shadow
chest/ring/visibility; pyramid floor and safe re-entry; and every canonical
lost-item owner. Do not expose RNG or internal migration fields.

The `::deserttreasure` command completes prerequisite carriers, sets current
state 13, marks all custom diamond paths complete, sets all columns, and
teleports directly to Azzanadra. It does not normalize native Blood, Smoke,
Ice, Shadow, killed-boss, troll, chest, gate, pyramid, visibility, quest-item,
spellbook, staff, or encounter state. The generic quest cheat merely writes
main 15 and therefore skips XP/QP/unlock invariants.

Replace both with explicit setup, advance, complete, and reset adapters. Every
adapter must produce a coherent native snapshot, use symbolic actors/locs,
clean only DT-owned temporary state, and offer deliberate checkpoints for all
four routes and pyramid floors. Debug completion must choose whether to award
real rewards and must never be accepted as live-route completion evidence.

## 16. Modernization work order

1. Characterize current primary/custom combinations, native transforms,
   requirements, item ownership, shared NPC routes, all downstream readers,
   and ordinary-world discovery before changing saves.
2. Capture live revision-239 traces for native 2/3, 10/11/12, 13/14, all four
   subquest ladders, torch timing/chance, boss combat, and pyramid events.
3. Implement and prove a versioned idempotent save migration; switch all
   readers/writers/journal/debug consumers to named native fields; retain a
   safe compatibility checkpoint for ambiguous saves.
4. Restore symbolic Asgarnia discovery and modernize requirements,
   acceptance, etchings/translation Read and recovery, bartender payment,
   Eblis materials including notes, and six mirror operations.
5. Build the shared Stranger and diamond ownership/recovery ledger, then make
   each column insertion native, gated, atomic, and order-independent.
6. Modernize Blood preparation and owned Dessous combat; close pot, combo
   attack, cutscene, death, and Malak recovery gaps.
7. Implement the Smoke hazard and durable torches, then modernize chest/gate,
   Fareed combat, ammunition ashes, and ground-item recovery.
8. Implement the full five-troll/cave/cold/Kamil/boots/path/parents Ice route
   and remove the surface-gate shortcut.
9. Repair the shared visibility field, chest poison/XP, Rasolo replacement,
   owned two-form Damis encounter, tick prayer drain, and ground recovery.
10. Build the complete four-floor pyramid, central door, Azzanadra scene, and
    reconnect-safe per-player lifecycle from the real exterior route.
11. Make completion and rewards exactly once, immediately activate Ancient
    Magicks, fix altar Prayer drain, and implement Eblis staff/signets/ring
    services plus correct SotN/DT2 post-quest gates.
12. Rewrite journal and debug adapters, fix each downstream owner, and execute
    the full verification matrix with two simultaneous players.

Keep commits reviewable and reversible: save migration, discovery/intro,
materials/mirrors, shared diamond ownership, each of four routes, pyramid,
completion, services, and downstream integrations should be separate proving
slices.

## 17. Verification matrix

| Area | Required evidence |
| --- | --- |
| Discovery | Fresh player sees symbolic Asgarnia at Bedabin; all Terry, bartender, Eblis, route NPCs, locs, ladders, and Azzanadra resolve without injection |
| Requirements | Every prerequisite; exact start-only Magic; non-boostable Magic/Thieving/Slayer; boostable Firemaking; Slayer-or-gas-mask; useful refusal |
| Migration | Every primary/custom/native combination; held/banked/ground items; repeated login; no fabricated kills/items; no remaining custom truth |
| Archaeology | Full inventory acceptance; etchings loss; Terry shared routing; translation Read/recovery; exact native 1–5 transitions |
| Bandit/Eblis | Exact 650 coins; interrupted transaction; all material amounts; mixed notes; exact remainder; six optional mirror views; native 6–11 |
| Shared diamonds | Any order; Stranger trigger/non-trigger; bank suppression; simultaneous players; all loss policies; four gated atomic columns; native 11–13 |
| Blood | Every pot stage/order, Entrana sharing, full inventory, owned grave/Dessous, fast melee/combo, silver family, death/escape/relog, Malak recovery |
| Smoke | Face protection family; 20/12-second nonlethal hazard; torch chance/timing/logout; chest/key/gate recovery; owned Fareed; water family; gloves/disarm/ashes |
| Ice | All accepted foods; five owned trolls/count; cave transform; cold drain/damage; Kamil fire/freeze; spiked boots/slips; two 10-HP blocks; reunion/recovery |
| Shadow | Three locks; 2–3 damage/poison/break; 150 XP; cross/ring recovery; ladder visibility lifecycle; ring-held Damis; two forms/tick drain/ground item |
| Pyramid | Four columns, exterior entry, every floor/ladder, scarabs, sarcophagi, mummies, random ejection, timer, central door, death/logout/reconnect, two players |
| Completion | Exact 13/14 trace; full Azzanadra scene; native 15; one 3-QP and 20,006.9-XP award; immediate Ancient; no coin icon/reward; duplicate queue proof |
| Post-quest | Back door; altar and Prayer zero; ring replacement; one safe 80k staff sale; signets; Smoke access; NMZ owner; Ancient sceptre SotN/DT2 gates |
| Downstream | RFD finale, Frozen Door, POH altars, Spellbook Swap, DT2, miniquest and diary consumers before/active/complete; shared systems unchanged |
| Journal/debug | Every native checkpoint, exact remainders/recovery, coherent setup/advance/complete/reset adapters, no teleport-based proof |
| Tooling/live | Quest Helper extractor naming fix, loc/var audit, combat manifest entries for all four bosses, compile/lint, focused tests, full quest suite, two-player smoke |

## 18. Gate verdict

| Gate | Verdict | Reason |
| --- | --- | --- |
| Gate A — discovery and state reachability | Fail | Start NPC absent; native state ladder bypassed; Shadow ladder invisible; Ice cave count unwritten; central pyramid door unhandled |
| Gate B — resource and transaction safety | Fail | Full-inventory hand-offs can strand progress; noted materials fail; multiple recovery paths duplicate banked items; direct boss rewards can be lost |
| Gate C — encounter and multiplayer safety | Fail | Bosses are global hand-spawns; temp guards strand rematches; major boss mechanics, hazards, route encounters, and pyramid lifecycle are absent |
| Gate D — completion and integration | Fail | Finale reached only by debug shortcut; completion lacks idempotence and immediate Ancient switch; altar, services, NMZ, sceptre gates, journal, and consumers are incomplete |

Desert Treasure I remains `audit-pending`. Do not mark it modernized until a
fresh player can start at the ordinary world NPC, traverse every canonical
step and all four order-independent routes, survive and recover from every
interruption under player ownership, reach Azzanadra through the real pyramid,
receive exactly one reward, and exercise every post-quest and downstream
contract against migrated native state.
