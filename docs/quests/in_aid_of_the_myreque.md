# In Aid of the Myreque modernization audit

Status: `audit-pending` — the native dbrow, permanent state, world transforms,
actors/items, journal, completion scroll, shared furnace hook, and downstream
Temple Trekking gate exist. The organic route can reach completion under ideal
conditions, but it is not equivalent to OSRS: major restoration mechanics are
collapsed or skippable, both battles use public cross-creditable NPCs, Ivan is
not escorted, the library book is never granted, required-item recovery can
hardlock, and completion keeps the Rod of Ivandis while failing to give the
Gadderhammer. This is a broad compatibility port, not a modern quest.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to the native ladder, Burgh de Rott restoration,
portable supply crate, shared actors, combat ownership, Ivan escort, library,
Rod of Ivandis lifecycle, completion transaction, downstream unlocks, journal,
and debug adapters. It is an implementation specification, not verification
evidence.

## 1. Authoritative references

Revisions were resolved through the OSRS Wiki API on 2026-08-17. The article
and guide define mechanics; the transcript informs dialogue and handoffs but is
explicitly incomplete and untidy, so it cannot override the article, guide, or
cache on disputed mechanics.

| Reference | Pinned revision | Audit use |
| --- | --- | --- |
| [Article](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque?oldid=15302854) | 15302854, 2026-08-16 | Identity, requirements, route, rewards |
| [Quick guide](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque/Quick_guide?oldid=15022775) | 15022775, 2025-11-11 | Exact sequence, supplies, alternatives |
| [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AIn_Aid_of_the_Myreque?oldid=15266856) | 15266856, 2026-07-18 | Choices, re-talks, handoffs; incomplete source |
| [In Search of the Myreque](https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque?oldid=15292283) | 15292283, 2026-08-10 | Direct prerequisite and shared hideout |
| [The Myreque](https://oldschool.runescape.wiki/w/The_Myreque?oldid=15303400) | 15303400, 2026-08-16 | Series context |
| [Burgh de Rott](https://oldschool.runescape.wiki/w/Burgh_de_Rott?oldid=15287430) | 15287430, 2026-08-04 | Restoration and post-quest world state |
| [Aurel](https://oldschool.runescape.wiki/w/Aurel?oldid=14992445) | 14992445, 2025-09-25 | Shop, crate assignment, hammer recovery |
| [Cornelius](https://oldschool.runescape.wiki/w/Cornelius?oldid=14767810) | 14767810, 2024-10-13 | Bank recruitment |
| [Crate](https://oldschool.runescape.wiki/w/Crate_%28In_Aid_of_the_Myreque%29?oldid=15185575) | 15185575, 2026-04-22 | Portable crate fill/reclaim lifecycle |
| [Rubble](https://oldschool.runescape.wiki/w/Rubble_%28In_Aid_of_the_Myreque%29?oldid=15238457) | 15238457, 2026-06-24 | Mining, buckets, loot, cleanup |
| [Gadderanks](https://oldschool.runescape.wiki/w/Gadderanks?oldid=15199449) | 15199449, 2026-04-28 | Blood-tithe encounter and defeat |
| [Gadderhammer](https://oldschool.runescape.wiki/w/Gadderhammer?oldid=15183027) | 15183027, 2026-04-22 | Grant, equip gate, reclaim, combat effects |
| [Vampyre Juvinate](https://oldschool.runescape.wiki/w/Vampyre_Juvinate?oldid=15199450) | 15199450, 2026-04-28 | Fight and escort enemies |
| [Ivan Strom](https://oldschool.runescape.wiki/w/Ivan_Strom?oldid=15247755) | 15247755, 2026-07-02 | Equipment/food, escort, failure and retry |
| [Drezel](https://oldschool.runescape.wiki/w/Drezel?oldid=15271643) | 15271643, 2026-07-22 | Library key and replacement |
| [Temple library key](https://oldschool.runescape.wiki/w/Temple_library_key?oldid=15185823) | 15185823, 2026-04-22 | Unlock and loss policy |
| [The Sleeping Seven](https://oldschool.runescape.wiki/w/The_sleeping_seven?oldid=15282302) | 15282302, 2026-07-30 | Book, reading, reclaim, POH lifecycle |
| [Rod mould](https://oldschool.runescape.wiki/w/Rod_mould?oldid=15185576) | 15185576, 2026-04-22 | Coffin impression and replacement |
| [Silvthrill rod](https://oldschool.runescape.wiki/w/Silvthrill_rod?oldid=15224295) | 15224295, 2026-06-03 | Smelting and enchantment |
| [Rod of ivandis](https://oldschool.runescape.wiki/w/Rod_of_ivandis?oldid=15290468) | 15290468, 2026-08-08 | Charges, special attack, final handoff |
| [Temple Trekking](https://oldschool.runescape.wiki/w/Temple_Trekking?oldid=15296985) | 15296985, 2026-08-13 | Direct minigame unlock |
| [Darkness of Hallowvale](https://oldschool.runescape.wiki/w/Darkness_of_Hallowvale?oldid=15292356) | 15292356, 2026-08-10 | Direct sequel and shared actors |
| [Morytania Diary](https://oldschool.runescape.wiki/w/Morytania_Diary?oldid=15280663) | 15280663, 2026-07-29 | Diary consumers |
| [Haunted Mine](https://oldschool.runescape.wiki/w/Haunted_Mine?oldid=15292305) | 15292305, 2026-08-10 | Partial-completion shortcut dependency |
| [Oarswoman Olga](https://oldschool.runescape.wiki/w/Oarswoman_Olga?oldid=15197454) | 15197454, 2026-04-25 | Current post-quest Sailing reward |

Presentation references are
[Gadderanks...](https://oldschool.runescape.wiki/w/Gadderanks..._%28In_Aid_of_the_Myreque%29?oldid=15261866)
(revision 15261866),
[Vampyre Assault quest jingle](https://oldschool.runescape.wiki/w/Vampyre_Assault_%28In_Aid_of_the_Myreque%29?oldid=15301910)
(15301910),
[Vampyre Assault music](https://oldschool.runescape.wiki/w/Vampyre_Assault?oldid=15253423)
(15253423),
[Fangs for the Memory](https://oldschool.runescape.wiki/w/Fangs_for_the_Memory?oldid=15258728)
(15258728), and
[Stagnant](https://oldschool.runescape.wiki/w/Stagnant?oldid=15267510)
(15267510). The two historical quest jingles have been replaced in current
OSRS; the modernization should use current cutscene/fight music rather than
reviving superseded behavior.

Transition aid only: local Quest Helper commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/inaidofthemyreque)
maps states, coordinates, item requirements, and actors. Its last quest-path
change is `241eaec` from 2025-08-27. Its general requirements also omit the
current 25 Agility requirement, demonstrating why it cannot override the Wiki.
`python3 tools/questhelper_extract.py inaidofthemyreque --check` exits 0.

## 2. Canonical contract

This is a members, intermediate, medium quest released 22 March 2006 and the
second Myreque quest. It starts with Veliaf Hurtz in the Myreque Hideout and
requires completion of In Search of the Myreque.

| Requirement | Canonical policy | Current defect |
| --- | --- | --- |
| 25 Agility | Boostable; checked at start | Missing from dbrow, start gate, and journal |
| 25 Crafting | Unboostable; checked at start | Base-stat gate is correct |
| 15 Mining | Unboostable; checked at start | Base-stat gate is correct |
| 7 Magic | Boostable; checked at start | `stat_base` incorrectly rejects boosts |
| In Search of the Myreque | Must be complete | Organic dispatcher gates correctly; comments claim otherwise |

The cache's single `requirements_boostable=1` value cannot express the live
mixed policy. The implementation needs explicit per-skill checks and corrected
metadata/journal text. `requirement_check_skills_on_start=0` is also stale:
current OSRS checks all four at the start.

Core supplies include food for Florin; a pickaxe, spade, and bucket for rubble;
a hammer, eleven planks, forty-four nails of any type, and one swamp paste for
repairs; ten bronze axes, ten assigned mackerel or snails, and three tinderboxes
for Aurel's crate; two steel bars, coal, and a tinderbox for the furnace; a
silver/Efaritay-compatible weapon for Juvinates; and soft clay, silver and
mithril bars, a sapphire, one cosmic and one water rune or valid rune sources,
rope, and the normal spellbook for the rod. Requirements must be checked at the
action that consumes them, not collapsed into an up-front inventory checklist.

Rewards are 2 QP and 2,000 XP each in Attack, Strength, Crafting, and Defence.
The quest restores Burgh de Rott, unlocks Temple Trekking/Burgh de Rott Ramble,
and enables Rod of Ivandis manufacture. The player receives the Gadderhammer
during the Gadderanks scene and hands the completed 10-charge Rod to Veliaf at
completion. Current OSRS also exposes five Varrock Museum Kudos for the story
and a post-quest Oarswoman Olga reward at 65 Sailing; both consumers need an
explicit integration decision and test.

## 3. Native identity and state

| Field | Native value |
| --- | --- |
| Cache quest / dbrow | 107 / `quest_inaidofthemyreque` |
| Implementation root | `quest_inaidofthemyreque`, 13 files, 1,118 lines |
| Start actor | shared `route_veliaf_hurtz_parent` in the old hideout |
| Direct prerequisite | `quest_insearchofthemyreque`, dbrow 79 |
| Primary progress | `%myreque_2_quest`, bits 0–8 of `myreque2_multivar` |
| Visual/side state | `myreque_2_main_var`, `myreque2_multivar`, `myreque2_extravar` |
| End state / QP | 430 / 2 |
| XP tenths | Attack, Strength, Crafting, Defence: 20000 each |

The raw dbrow is correct: `all.dbrow.compack` row 79 is In Search of the
Myreque, not Desert Treasure I. The long constant-file claim of cache decode
corruption is false. Its 10 January 2006 release date is also stale. In Search
now has real writers through its 105 completion state, so the comment that it
is missing and soft-skipped is obsolete.

All three base variables are native permanent transmitted carriers. Preserve
them; modernizing the quest does not require an authored parallel progress var.

### 3.1 State ladder

Quest Helper exposes these meaningful states:

`0, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150,
160, 165, 170, 180, 190, 200, 205, 210, 220, 230, 240, 250, 260, 280,
290, 300, 310, 315, 320, 330, 340, 350, 360, 370, 375, 380, 390, 400,
410, 420`, followed by cache end state `430`.

The native port defines only `0, 20, 30, 40, 80, 100, 110, 140, 150, 160,
165, 170, 180, 190, 200, 205, 210, 220, 230, 240, 260, 280, 290, 300,
315, 350, 370, 375, 410, 430`. The omitted values include recovery and
presentation boundaries; they are not all cosmetic arrow redraws.

| Phase | Canonical boundaries | Current result |
| --- | --- | --- |
| Offer and arrival | 0–30 | Direct start commit; no accept/refuse branch |
| Florin/Razvan and inn | 30–140 | Food search and one-click rubble collapse stages |
| Store and crate | 140–170 | Wall can be skipped; portable crate omitted |
| Bank and furnace | 170–230 | Bank opens early; Cornelius can be skipped |
| Tithe raid | 230–280 | Public three-death counter; no owned attempt/reset |
| Hideout and Ivan | 280–315 | One informer and one teleported ambush only |
| Library | 315–375 | Key handoff can hardlock; book is only a message |
| Rod | 375–420 | Direct rune use bypasses spell system |
| Handoff/completion | 420–430 | Rod retained, hammer absent, settlement replayable |

### 3.2 Side-state audit

`myreque_2_main_var` holds Florin's food-pelt state, collapsed wall, trapdoor,
rubble count, plaque, shop roof/wall and food assignment, bank booth/wall and
teller, furnace, blood-tithe visibility, temple trapdoor, tomb boards, shop
stock, party visibility, and Ivan's armour. `myreque2_multivar` additionally
holds portable-crate contents, tithe conversations, both fight counters, Ivan's
route, and room identity. `myreque2_extravar` holds Ivan's sickle, food and
armour handoffs, Gadderhammer delivery, rubble messaging, cutscene state, and a
garlic chest.

Many of these native fields are unused; others are jumped directly to their
terminal value. That destroys the precise resume state the cache supplied.
Modernization should assign one owner and invariant to each field, retain
native visual transforms, and remove obsolete fields only if cache and client
behavior prove them genuinely dead.

## 4. Implementation and ownership surface

| Surface | Current ownership and risk |
| --- | --- |
| `myreque2_hideout.rs2` | Shared start, old-hideout reports, Ivan dispatch; start and escort are compressed |
| `myreque2_burgh.rs2` | Florin, Razvan, inn/trapdoor/rubble; food and rubble mechanics are non-canonical |
| `myreque2_shop.rs2` | Store repairs and direct bulk hand-in; wall/state ordering is skippable |
| `myreque2_bank.rs2` | Bank repair and Cornelius; early bank access and state-190 alias permit skips |
| `myreque2_furnace.rs2` | Correct three visual phases and quantities; thin presentation |
| `myreque2_fight.rs2` | Tithe dialogue and public NPC spawns/death counter; no attempt ownership |
| `myreque2_trek.rs2` | Public two-NPC ambush, Drezel key, trapdoor, message-only book |
| `myreque2_rod.rs2` | Mould, shared furnace recipe, direct enchant, blessing, final handoff |
| `myreque2_shared.rs2` | Requirements and non-idempotent completion settlement |
| `myreque2_journal.rs2` | Dynamic journal arm, but only broad phases |
| shared smithing furnace | Correctly delegates `burgh_rod_clay` to `myreque2_make_rod` |
| Sins of the Father Veliaf owner | Correctly delegates state 410–429 to the final handoff |
| quest-list / POH adapters | Dynamic journal and 0/started/430 POH status are registered |
| `quest_cheat.rs2` | Writes only 430 before generic QP/count award; no coherent fixtures |
| Temple Trekking | Correctly gates entry on state 430 |
| Darkness of Hallowvale | Reads state 430 and shares Veliaf/Drezel ownership |
| A Taste of Hope | Downstream Rod lifecycle is absent/soft-skipped |

The root uses symbolic cache names, modern chat helpers, native multilocs and
multinpcs, and no quest-specific C routing. Those are good foundations. The old
machinery is behavioral: public `npc_add`/`npc_find`, global death attribution,
state jumps substituting for items or travel, and irreversible state writes
before delivery succeeds.

Shared actor precedence needs an explicit matrix. At minimum, the real owners
must dispatch in series order for route/In Search, In Aid, Darkness of
Hallowvale, A Taste of Hope, and Sins of the Father. Drezel's current
`priestperiltrappedmonk_vis` leaf binding must be reconciled with the actual
Priest in Peril wrapper; uniqueness of the gameval name does not prove the
world actor routes into this trigger.

## 5. Burgh de Rott route and restoration

### Start, travel, and Florin

Keep the organic prerequisite dispatcher, but restore the full offer,
accept/refuse, and re-talk branches. Apply the mixed skill policy and commit
state only after acceptance. The generated metadata, journal, and runtime gate
must agree on all four skills.

Florin expects the exact food the player uses on the open chest. Current code
binds Search, scans a short hard-coded list, and deletes the first priority
match, which can remove an item the player did not select and rejects many
valid foods. Bind both item-on-loc directions, validate the used object against
the canonical food category, consume exactly that object, and drive the native
food/pelting presentation field. Re-talk and repeated-use paths must not consume
extra food.

### Inn rubble

The canonical loop is a real restoration mechanic:

1. mine the blocked entrance with any valid pickaxe;
2. mine basement rubble piles;
3. use a spade and bucket to collect up to three loads per bucket;
4. empty filled buckets only onto the outside rubble pile;
5. repeat until the native five-stage counter is complete; and
6. show the cleared-basement/plaque presentation before Razvan's next phase.

The piles provide assorted nails, rock, broken glass, dusty scroll, and plaster
fragment loot; mined rubble reappears if it was not carried away, and dropped
quest rubble needs cleanup/recovery behavior. Current code requires a manually
enumerated bronze-through-rune pickaxe plus a spade, grants no bucket or loot,
then sets the rubble counter to five and progress to 100 in one click. Replace
the shortcut with per-player pile state, inventory-capacity checks, mining and
shovelling animations, exact item transactions, and relog/drop recovery. Use
the engine's valid-pickaxe category so later valid variants are not rejected.

### General store

Repair the roof and wall separately. Each uses three basic planks and twelve
nails of any type with a valid hammer, after a Yes/No confirmation. Current
code recognizes only `hammer` and `nails` (Steel nails), has no confirmation,
and Aurel at state 150 hands out the conceptual crate without verifying that
the wall repair happened. The wall action changes only its visual, so talking
to Aurel can skip it entirely.

Implement the real `burgh_generalstore_crate`:

- Aurel assigns one per-player food type, mackerel or snails, and may reroll it
  only before the crate is accepted;
- the crate records ten bronze axes, ten assigned foods, and three tinderboxes
  in the native subfields;
- Search reports partial contents;
- item-on-item supports add one/add all and mixed valid snail variants;
- raw or cooked valid foods are accepted, but noted items are not;
- repeated/full/invalid additions preserve both item and count; and
- Destroy/loss permits Aurel to replace the correct partial crate.

Current Aurel instead requires all supplies at once, accepts either food branch
rather than the assigned one, accepts only raw mackerel or ten copies of one
snail subtype, performs sequential deletes, and writes every count to its cap.
It never grants the crate item. Gate Aurel's generated Trade op and stock until
`burgh_store_stocked`; today the shop is available before the quest.

### Bank and furnace

The bank booth consumes two planks, eight nails of any type, and one swamp
paste; the wall consumes three planks and twelve nails. Retain the native
multiloc transforms but make each transaction confirmed, atomic, and
replay-safe. The generic repaired-booth handler currently opens the bank as soon
as the booth is fixed, before the wall and teller are ready. Gate access on both
repairs plus successful Cornelius recruitment.

State 190 currently means both “wall repaired” and “Cornelius recruited.”
Cornelius sets only the teller bit and Razvan advances the same state whether
or not that conversation occurred. Split these boundaries using the native
state/visual fields so Cornelius cannot be skipped and re-talks remain useful.

The furnace correctly consumes two steel bars, then one coal, then retains a
tinderbox while changing visual values 0→1→2→3. Keep the shared usable furnace
at value 3, but add confirmation, movement/animation, and the current
Vanstrom/Gadderanks scene with `Stagnant` rather than the retired jingle.

## 6. Blood-tithe fight and Gadderhammer

The live encounter includes a cutscene, Gadderanks and two Juvinates, and
Veliaf arriving to assist. The player needs a silver weapon or Efaritay's aid
for the vampyres. Leaving resets the fight. Since the 7 January 2026 behavior
update, progression must also succeed if Veliaf lands the final blow on
Gadderanks.

Current code spawns all three enemies at one fixed public coordinate for 1,000
ticks. Global `npc_find` suppresses another player's attempt, while generic
`npc_findhero` can credit the wrong player. Every death increments one counter,
so it proves neither exact roster nor attempt membership. There is no ally
Veliaf, vampyre weapon immunity, instance/session identity, protected queue,
reset, or reliable respawn. States 240–260 can strand a player when public or
timed NPCs disappear.

Model an owned encounter or private instance with an attempt token, exact actor
UIDs, attack eligibility, ally contribution, leave/death/logout reset, cleanup,
and deterministic re-entry. State 250 is a genuine recovery boundary. Do not
advance merely because any three matching death callbacks fired.

Gadderanks dies after explaining his weakness and family, rather than fleeing.
Grant one Gadderhammer in that scene and set `gadderanks_warhammer_give` only
after delivery is durable. If inventory is full, use the canonical floor or
re-talk policy without losing entitlement. It cannot be wielded before this
defeat; afterward Aurel sells a lost replacement for 3,000 coins. Preserve its
25% Shade damage bonus and 5% double-hit effect, and do not treat it as a normal
construction hammer. None of that lifecycle is currently implemented.

Unlock `Vampyre Assault` when the fight begins and verify the current cutscene
music separately from historical quest jingles. Add the encounter to the
combat manifest with handler, validation, loot, ownership, and regression-test
fields; its current row is effectively blank.

## 7. Hideout report and Ivan escort

After the raid, returning to Veliaf is part of the story, not an optional
shortcut. In the hideout, either Polmafi Ferdygris or Radigad Ponfit may be the
first informed member and can direct the player to the other. Current code
implements only Polmafi and lets old-hideout Veliaf move 280→290 directly.

Ivan's journey must be an escort encounter, not a teleport:

- reject entry when pets or incompatible followers make the instance unsafe;
- offer both routes and store `juvinate_ambush_routetaken`;
- allow the canonical optional sickle, steel med helm, chainbody, platelegs,
  and food handoffs, with Ivan retaining his configuration between attempts;
- run the short route against two level-75 Juvinates or the long route against
  four level-50 Juvinates;
- keep Ivan as an owned actor with health, target/follow behavior, and food use;
- require the right anti-vampyre damage rules;
- return Ivan and his remaining supplies on player escape/logout; and
- on Ivan's defeat, fail/reset the attempt and consume only what canon says was
  used before permitting a retry.

Current code teleports the player to one public tile, spawns two public NPCs,
has no Ivan actor, and teleports to Paterdomus after two globally attributable
deaths. Route choice and all native gift fields are unused. The state usually
allows another trigger after returning to the source Ivan, but partial counters
and global spawns remain unsafe. Use `Fangs for the Memory` for the ambush and
verify its unlock policy.

## 8. Drezel, library, and Rod of Ivandis

### Key and books

Drezel currently advances 315→350 whether or not `inv_add(burgh_key)` can
succeed. A full inventory therefore loses the only key, and state 350 offers no
replacement. Make grant and progress one transaction. During the quest Drezel
replaces a destroyed/lost Temple library key; once the keyhole is unlocked, the
trapdoor stays open. Test the odd post-quest key/toggle behavior separately
rather than weakening the quest-time recovery contract.

Searching the Ivandis bookcase currently displays a sentence and writes state
370. It never grants `burgh_book_sevenwarriors`, never exposes Read or Destroy,
and cannot replace the book. Grant The Sleeping Seven, require the player to
read it before the tomb boards are actionable, support library reclaim, and
wire its later POH bookcase availability. Audit the other two library books and
the entrance/exit topology while owning this area.

### Mould, smelting, enchantment, and blessing

Using soft clay on Ivandis's coffin to make a mould and delegating the mould to
the shared furnace are sound ownership choices. Harden the smelting recipe as
one atomic transform: mould plus one silver bar, one mithril bar, and one
sapphire becomes one Silvthrill rod only when output can be delivered. Recovery
must consider inventory, equipment, bank, ground, and later rod forms rather
than inventory-only duplicate checks.

Current enchantment is a custom item-on-rune handler. It checks base Magic,
requires literal water and cosmic runes, deletes them, manually grants XP, and
transforms the rod. Canon requires an actual level-1 enchant cast from the
normal spellbook; a tablet does not work, while valid elemental rune sources
such as a water staff do. Extend the shared magic conversion machinery using a
supported data/config path and let it own spellbook, boosted level, rune-source,
animation, XP, and interruption policy.

Blessing the enchanted rod at the Paterdomus well requires rope, retains the
rope, and produces a 10-charge Rod. The current transform broadly matches that
transaction, but needs output-capacity, duplicate, interrupted-action, and
equipment/bank tests.

At completion Veliaf takes the Rod; current code merely checks inventory and
leaves it with the player. A Taste of Hope later reclaims this exact Rod from
Veliaf, so this is cross-quest durable state, not disposable dialogue. Define a
single ownership model for the stored rod and fix that sequel's current
soft-skip. If the Rod is equipped, the quest should not complete until the
player actually hands it over.

The reward includes the ability to manufacture additional Rods. Audit the
weapon itself: all charge forms, special attack, charge consumption, standard
spellbook autocasting, recharge/manufacture, death/loss, and post-quest access.
The content search found base animation/sound configs but no clear rod-specific
special-attack implementation, so ability-to-craft is not enough to prove the
reward works.

## 9. Completion, unlocks, and downstream consumers

`myreque2_quest_complete` writes state 430 first, grants all four XP awards,
then calls `~quest_complete_rewards`. That helper updates QP and completed count
without an idempotence receipt. Re-entering the proc can duplicate XP, points,
and count; interruption after the state write can instead lose settlement.

Implement one resumable completion transaction:

1. prove state 420 and possession of the exact 10-charge Rod;
2. consume/store the Rod for Veliaf;
3. reconcile the earlier Gadderhammer entitlement without granting a duplicate;
4. grant each XP award and 2 QP exactly once;
5. write end state only when settlement is durably complete; and
6. make repeat dialogue and `::complete` no-ops for already-settled players.

The completion scroll's item icon does not grant the Gadderhammer. Its reward
text must distinguish the earlier hammer acquisition from direct final rewards
and include or deliberately route Kudos and Olga's current Sailing reward.

| Consumer | Required verification |
| --- | --- |
| Burgh de Rott | Final store, bank, furnace, inn, travel, actors, and world transforms persist |
| Temple Trekking | State-430 gate works in both directions and does not unlock early |
| Darkness of Hallowvale | Real shared Veliaf/Drezel owners dispatch only after 430 |
| A Taste of Hope | Retrieves the Rod stored with Veliaf and handles later loss policy |
| Varrock Museum | Historian exposes the five-Kudos story at the right completion state |
| Oarswoman Olga | 65 Sailing post-quest reward exists once and is replay-safe |
| Morytania Diary | Every quest-dependent task reads the correct state/action hook |
| Haunted Mine | Low-fence shortcut uses the documented partial/full completion boundary |
| POH bookcase | The Sleeping Seven becomes obtainable at the correct point |

The current Temple Trekking and direct sequel comparisons are positive. The
Kudos historian and Olga scripts were not found; Olga has cache assets only.
The diary framework does not visibly wire the quest-specific tasks, and the
Haunted Mine shortcut needs a direct predicate audit. Treat these as missing
integration evidence, not implied unlocks.

## 10. Journal, admin, provenance, and recovery

The dynamic journal is registered and uses the native state, but it has only
seven broad in-progress branches. It omits Agility, partial rubble buckets,
crate contents/assignment, separate repair and recruitment state, fight and
escort recovery, key/book/mould loss, and the final handoff. Its catch-all
branch treats any invalid state at or above 430 as completed. Render exact
objectives from both main and side state and reserve completion text for the
exact terminal invariant.

The generated POH adapter correctly maps 0/started/430. The quest cheat sets
only 430 and then relies on common QP/count bookkeeping; it grants no XP or item
history and leaves restoration/stock/teller/furnace/library/tomb state
incoherent. Keep a lightweight state-only debug command only if it is labelled
as such. Build hermetic fixture constructors for every phase, action,
interruption, loss, and post-quest consumer.

`QUESTHELPER_CONTENT_PORT_QUEUE.md` row 407 and its log repeat the false claim
that dbrow 79 is Desert Treasure, call In Search missing/soft-skipped, and use
the January release date. Correct those records when implementation begins.
The large `myreque2.constant` provenance block repeatedly treats “no precedent”
as permission to omit mechanics. Replace it with concise source/state notes;
absence of an earlier implementation is modernization work, not fidelity
evidence.

Required recovery matrix:

| Boundary | Resume invariant |
| --- | --- |
| Start | Refusal changes nothing; accepted requirements remain coherent |
| Florin | Exactly the selected food is consumed once |
| Rubble | Pile, bucket/load count, loot, drops, and outside total reconcile |
| Repairs | Each site consumes supplies once and independently owns its visual |
| Crate | Assignment, partial contents, loss, reclaim, and final handoff survive relog |
| Bank | Access remains closed until booth, wall, and teller are all complete |
| Furnace | Each 0–3 visual phase resumes without duplicate consumption |
| Tithe raid | Leave/death/logout cleans actors and restarts one owned attempt |
| Gadderhammer | Grant/reclaim never loses or duplicates entitlement |
| Ivan escort | Route, gifts, health, food, failure, and return are deterministic |
| Library | Full inventory cannot advance past a failed key/book grant |
| Rod | Every form is unique across inventory/equipment/bank/ground/stored owner |
| Completion | Rod handoff and every reward settle exactly once |

## 11. Modernization work packages

### P0 — identity, state, and ownership

- Correct release/prerequisite comments and mixed requirements.
- Restore all meaningful native state boundaries and define side-field owners.
- Build the Veliaf/Ivan/Polmafi/Drezel/shared-loc precedence matrix.
- Add transaction/attempt primitives only when the existing general engine
  cannot safely express them; do not add quest-specific C routing.

### P1 — Burgh restoration

- Restore Florin's exact item-on-chest action.
- Implement pile/bucket rubble, loot, cleanup, and recovery.
- Make every repair atomic with any valid nails/hammer and confirmations.
- Implement the portable assigned-food crate and gate Aurel's shop.
- Gate the bank on both repairs plus Cornelius; retain the furnace transforms.

### P2 — blood-tithe encounter

- Implement the current cutscene/music and owned fight instance.
- Add silver/Efaritay damage policy, ally Veliaf, resets, and exact kill rules.
- Implement Gadderhammer grant, wield gate, Aurel recovery, and combat effects.

### P3 — Ivan escort

- Restore both routes, owned Ivan actor, optional equipment/food, and pet gate.
- Implement route-specific enemies, failure/retry, cleanup, and music.
- Restore either-member hideout reporting and useful re-talks.

### P4 — library and rod

- Make key and book delivery/reclaim ownership-safe.
- Grant/read The Sleeping Seven and audit the complete library topology.
- Harden mould/smelting/blessing transactions and integrate level-1 enchant.
- Implement Rod charges/special/autocast and the Veliaf/A Taste ownership chain.

### P5 — settlement and consumers

- Make final handoff and all rewards resumable and idempotent.
- Verify every world transform, travel, minigame, sequel, Kudos, diary, POH,
  Haunted Mine, and Olga consumer.
- Replace the state-only cheat with labelled debug state plus full fixtures.

### P6 — journal and tests

- Render every main/side-state objective and recovery hint.
- Populate combat and quest manifests.
- Add transition, transaction, concurrency, relog, loss, failure, and post-quest
  integration tests before a client smoke.

## 12. Gate D verification matrix

| Gate D evidence | Current result | Required pass condition |
| --- | --- | --- |
| Static quest audit | Audit record only | No undisclosed shortcut, stale source claim, trigger collision, or blank manifest field |
| Quest Helper extraction | Pass on 2026-08-17 | Continue passing against the pinned helper tree |
| `mock230-scripts` / pack check | Not run for this docs-only audit | Clean build and `mock230_pack --check-only` after code changes |
| Automated organic route | Absent | 0→430 through real triggers with canonical state/item invariants |
| Relog/inventory/loss tests | Absent | Every recovery row above automated |
| Combat concurrency tests | Absent | Two simultaneous players cannot suppress or credit each other |
| Real-client smoke | Absent | Start through scroll plus post-quest store/bank/travel captured |
| `::complete` twice | Fails by inspection | First creates coherent settled fixture; second is a no-op |
| Downstream integrations | Partial | All consumers above have predicate and once-only tests |

## 13. Exit criteria

Do not mark this quest `verified-modern` until:

- the correct prerequisite and mixed skill policy are enforced and displayed;
- no restoration phase can be skipped and all item transactions are atomic;
- the portable crate, rubble loop, store, bank, and furnace behave canonically;
- both combat sequences are player-owned, recoverable, and concurrency-safe;
- Ivan exists as an escort with both routes and failure/retry behavior;
- the key, book, mould, all Rod forms, and Gadderhammer have complete recovery;
- level-1 enchant and Rod combat behavior use shared modern systems;
- Veliaf takes the Rod and completion settles every reward exactly once;
- every downstream unlock and post-quest world state has an integration test;
- the journal, debug fixtures, manifests, build, pack check, automated suite,
  and real-client smoke satisfy Gate D; and
- the dossier records final commands, captures, Wiki revisions, and any precise
  non-critical deviation.

This audit intentionally makes no gameplay changes.
