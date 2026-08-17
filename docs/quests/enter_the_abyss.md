# Enter the Abyss modernization audit

Status: `audit-pending` — the native miniquest row, canonical 0–4 state
carrier, five permanent essence-source bits, both Mage of Zamorak actors,
scrying orbs, rewards, all five essence-mine teleport services, Abyss map,
12-layout loc system, five passage families, eleven older rifts, Dark Mage,
essence-pouch runtime, and aggressive abyssal creatures exist. The direct
miniquest can be completed, but its permanent unlock is wired backwards: the
Wilderness mage never teleports while the safe Varrock mage does. Entry then
lands at a fixed inner-ring tile, bypassing skulling, bracelet consumption,
the randomized outer ring, monsters, and passages. Reward settlement can lose
both items, orb recovery duplicates banked quest items, passage success never
moves the player, rifts ignore quest/equipment gates, pouch drops and recovery
are absent, and Wanted!, Temple of the Eye, Wilderness Diary, and Slayer task
contracts are incomplete or incorrect.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the direct miniquest, revision-239
cache state/transforms/locs, all five essence teleports, Mage services, the
shared Abyss and pouch systems, reward recovery, diaries, and downstream quest
and Slayer consumers. It is an implementation specification, not completion
evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, reward, Abyss, pouch, and consumer contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Enter the Abyss](https://oldschool.runescape.wiki/w/Enter_the_Abyss?oldid=15292255) | 15292255, 2026-08-10 | Identity, requirements, route, rewards, unlock, and required-for relationships |
| [Enter the Abyss/Quick guide](https://oldschool.runescape.wiki/w/Enter_the_Abyss/Quick_guide?oldid=15292365) | 15292365, 2026-08-10 | Ordered Wilderness/Varrock conversations, equipped-god-item restriction, orb capacity, three sources, and separate finish/danger talks |
| [Transcript:Enter the Abyss](https://oldschool.runescape.wiki/w/Transcript%3AEnter_the_Abyss?oldid=15263217) | 15263217, 2026-07-14 | Rewritten acceptance/refusal tree, shop choice, orb loss/bank branches, handover, settlement, and post-quest lore |
| [Abyss](https://oldschool.runescape.wiki/w/Abyss?oldid=15228428) | 15228428, 2026-06-07 | Entry penalties, two rings, 12 layouts, five passages, success formula/XP, creatures, rifts, and access restrictions |
| [Mage of Zamorak](https://oldschool.runescape.wiki/w/Mage_of_Zamorak?oldid=15225428) | 15225428, 2026-06-05 | Two locations, Wilderness-only teleport, Tele Block behavior, god-item restriction, shop, and pouch service |
| [Battle Runes](https://oldschool.runescape.wiki/w/Battle_Runes?oldid=14947554) | 14947554, 2025-07-24 | Pre/post-miniquest stock and Wilderness shop ownership |
| [Scrying orb](https://oldschool.runescape.wiki/w/Scrying_orb?oldid=15185191) | 15185191, 2026-04-22 | Quest-only empty/charged lifecycle and Destroy behavior |
| [Abyssal book](https://oldschool.runescape.wiki/w/Abyssal_book?oldid=15282368) | 15282368, 2026-07-30 | Reward identity and Dark Mage/POH replacement sources |
| [Small pouch](https://oldschool.runescape.wiki/w/Small_pouch?oldid=15279982) | 15279982, 2026-07-29 | Capacity, no degradation, ownership-aware drops, replacement, and legitimate pre-completion second pouch |
| [Essence pouch](https://oldschool.runescape.wiki/w/Essence_pouch?oldid=15280006) | 15280006, 2026-07-29 | Essence types, levels, degradation, bank-aware repair, death/drop semantics, and sequential acquisition |
| [Abyssal bracelet](https://oldschool.runescape.wiki/w/Abyssal_bracelet?oldid=15184161) | 15184161, 2026-04-22 | Five charges, worn-only skull prevention, one charge per entry, and crumble lifecycle |
| [Devious Minds](https://oldschool.runescape.wiki/w/Devious_Minds?oldid=15292315) | 15292315, 2026-08-10 | Full miniquest prerequisite and large/colossal-pouch Law-rift route |
| [Wanted!](https://oldschool.runescape.wiki/w/Wanted%21?oldid=15292311) | 15292311, 2026-08-10 | Partial miniquest interaction and Mage topic multiplexing |
| [Temple of the Eye](https://oldschool.runescape.wiki/w/Temple_of_the_Eye?oldid=15279929) | 15279929, 2026-07-29 | Completion prerequisite, one-time centre teleport, Dark Mage, and shared actors |
| [Wilderness Diary](https://oldschool.runescape.wiki/w/Wilderness_Diary?oldid=15293951) | 15293951, 2026-08-12 | Easy task for the Wilderness Mage's teleport; Temple of the Eye teleport must not count |
| [Slayer task/Abyssal demons](https://oldschool.runescape.wiki/w/Slayer_task/Abyssal_demons?oldid=15299680) | 15299680, 2026-08-14 | Current assignment gate: 85 Slayer/combat plus Priest in Peril or partial Fairytale II, not this miniquest |

The sources identify Enter the Abyss as an intermediate, very-short members'
miniquest in the Order of Wizards series, released 13 June 2005. It requires
Rune Mysteries, has no required items or enemies, awards no quest points, and
gives 1,000 Runecraft XP, an Abyssal book, a small pouch, and access to the
Abyss through the Wilderness Mage of Zamorak.

Transition aid only: Quest Helper's
[`EnterTheAbyss.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/entertheabyss/EnterTheAbyss.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` confirms primary
states 0/1/2/3, the five native source bits, actors/coordinates, one free slot
for the initial orb, the separate state-3 finish interaction, prerequisite,
XP, pouch, and access reward. It chooses Aubury, Sedridor, and Cromperty as a
convenient route; that is not evidence that Brimstail or Wizard Distentor are
optional implementations. `python3 tools/questhelper_extract.py
entertheabyss --check` resolves every referenced dbrow, NPC, loc, object, and
varbit.

## 2. Native miniquest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `miniquest_entertheabyss`; dbrow pack index 43, quest metadata ID 85 |
| Type / difficulty / length | Members miniquest / intermediate / very short |
| Series / release | Order of Wizards #2 / 13 June 2005 |
| Start | Wilderness Mage wrapper `rcu_zammy_mage1` at 3106,3558,0, roaming near the River Lum; native start coordinate 3106,3557,0 |
| Requirement | Rune Mysteries complete; no item or skill requirement |
| Primary state | `%abyssal_miniquest`, permanent varp 492 |
| Canonical values | 0 not started; 1 meet in Varrock; 2 carry/charge orb; 3 orb delivered/reward pending; 4 complete |
| Triangulation | Five independent bits on permanent `%abyssal_warp`: Wizards' Tower 13, Aubury 14, Cromperty 15, Brimstail 16, Wizards' Guild 17 |
| Abyss layout | `%rcu_abyssal_generator`, bits 18–29 of the same carrier; cache `rcu_outer_multi1..12` locs encode 12 arrangements |
| Warning | `%rcu_abyssal_warning`, bit 30, exists natively and must retain verified client semantics |
| End state / quest points | 4 / 0 |
| XP / item reward | 1,000 Runecraft XP (raw tenths 10,000); Abyssal book; small pouch |
| Unlock | Wilderness Mage Teleport option, Battle Runes post-quest stock, dangerous outer Abyss, rifts, creatures/pouch acquisition |
| Downstream | Devious Minds; Temple of the Eye; partial Wanted! route; Wilderness easy task; Abyssal Sire access alternative |

No replacement primary variable is justified. The cache already exposes the
correct five-state machine, durable source bits, 12-layout field, warning bit,
NPC transforms, obstacle wrappers, rifts, item variants, and bracelet charges.
Modernization should preserve those identities and add only narrowly scoped
state that is proven absent, such as idempotent reward reconciliation or diary
task identity. Do not overload the source bits, pouch contents, or aggregate
diary counts.

## 3. Implementation surface

The direct root contains 283 lines across three files.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/entertheabyss.constant` | State, XP, and reference coordinates | Primary values/XP are correct; Sedridor and Cromperty constants are stale and unused, while the direct comments understate implemented shared teleports |
| `configs/entertheabyss.varp` | Re-declares cache carriers with permanent scope | Correct carrier names/scope; must remain schema-compatible with all native bitfields |
| `scripts/entertheabyss.rs2` | Orb counting/charging, completion, journal, both Mage routes, debug | Route reaches state 4, but dialogue, bank-aware recovery, settlement, actor ownership, permanent unlock, and debug evidence are defective |

Mandatory shared/cache surfaces:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `configs/all.dbrow`, `all.varp`, `all.varbit` | Metadata and permanent state | Correct row, end state, XP, five source bits, layout and warning fields |
| `configs/all.npc` and world spawns | Mage transforms and both locations | State 4 changes Wilderness leaf `a` to `b`, whose op4 is Teleport; Varrock wrapper is hidden at state 0 and visible at 1–4. Current triggers ignore this intended ownership |
| `skill_runecraft/scripts/essence_mine.rs2` | Shared teleport transaction and orb hook | Calls `~eta_charge_orb` before every mine teleport; stale header claims three services are deferred |
| Aubury, Sedridor, Cromperty, Brimstail, Distentor area scripts | Five distinct source owners | All five are live and call the shared teleport with the correct return coordinate; a normal three-distinct-source route is possible |
| `skill_runecraft/scripts/runecraft_abyss.rs2` | Entry, passages, rifts, Dark Mage bridge | Correct chance formula and eleven basic destinations exist, but entry and traversal are noncanonical and access gates are absent |
| `configs/all.loc` | 12 outer layouts, passage states, older/blood/soul rifts | Cache already supplies `rcu_outer_multi1..12`, all five passage families, Soul rift, and parent/true/Kourend Blood variants; server uses only a subset |
| `skill_combat/scripts/pk_skull.rs2`, death/deathkeep | Skull state | Deathkeep consumes `%pk_skull`, but acquisition, expiry, and visible headicon remain deferred; the Abyss file's “no skull system” claim is stale |
| `configs/all.obj` | Bracelet/orbs/book/pouches | Five bracelet charge objects, both orb forms, book, and all pouch tiers exist; no Abyss bracelet runtime is present |
| `skill_runecraft/scripts/runecraft_pouch.rs2` | Fill/Check/Empty/degrade/repair | Core carried-item behavior exists, but contents are simplified to one essence type, degradation differs from current variable threshold behavior, bank repair/death/drop are absent |
| NPC stats and `areas/world/configs/m47_75.spawn` / `m47_76.spawn` | Abyss creatures and Dark Mage | Creatures are placed, aggressive, and combat-capable; only ashes are configured and no pouch/talisman/runecraft drop tables are bound |
| `quest_templeoftheeye` | Shared Varrock Mage and Dark Mage trigger owner | Correctly multiplexes actor IDs in principle, but provides a narrated quest teleport and only repairs carried degraded pouches |
| shops pipeline / Battle Runes Wiki catalog | Mage op3 Trade | Cache exposes Trade and current stock data is imported, but no `opnpc3` handler or generated shop exists for either Wilderness Mage leaf |
| `interface_diaries` | Wilderness task | Only aggregate tier counters exist; no per-task “Mage teleported player” fact or entry event is published |
| `quest_wanted`, `quest_deviousminds`, Slayer, quest cheat | Consumers and admin setup | Each is audited below; several contracts use the wrong prerequisite or omit side effects |

## 4. State, transforms, and migration risk

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Not started; Wilderness talk requires Rune Mysteries and allows Battle Runes | Requirement is checked and talk writes 1, but equipped Saradomin/Guthix items are ignored and no shop choice/Trade handler exists |
| 1 | Meet safe Varrock mage; complete the rewritten offer/refusal tree; receive one orb with capacity | One shortened path works, but literal player-facing “Soft-skip: full refuse tree” text replaces most branches; recovery/ownership is not checked before grant |
| 2 | Carry orb through any three distinct source teleports; recover only if truly lost | Unique bit counting is correct. Replacement checks inventory only, so banking either orb permits duplicates; a banked charged orb is misdiagnosed as lost |
| 3 | Charged orb delivered; resumable final explanation/reward | Handover consumes one inventory orb and writes 3 correctly, but forces an abridged extra talk and leaves duplicate orbs in bank |
| 4 | Complete; Wilderness leaf has Teleport; Varrock supplies lore only | Completion is written before capacity/reward safety. Wilderness talk still redirects to Varrock, while Varrock talk offers the actual teleport |

The transform evidence is decisive. `rcu_zammy_mage1` maps values 0–3 to
`rcu_zammy_mage1a` (Talk/Trade) and value 4 to `rcu_zammy_mage1b`
(Talk/Trade/Teleport). There is no handler for leaf/wrapper op4. The Varrock
wrapper maps values 1–4 to `rcu_zammy_mage1_edgeb`, which has no Abyss
Teleport option, yet its Talk handler calls `~runecraft_enter_abyss` after
completion. This is not an acceptable alternate design: current dialogue and
the Wiki explicitly reject teleporting from Varrock.

### Required one-time migration

Primary values 0–4 and the five source bits already match native semantics;
do not remap or clear them. Reconcile data before repaired handlers run:

1. Preserve every source bit. Values represent distinct historical spell
   observations and are safe even if an orb was later lost.
2. At state 2, inventory and bank ownership of empty and charged orbs must be
   inspected together. Keep at most the canonical active quest item after an
   explicit duplicate policy; prefer a charged orb if three source bits are
   set. Never silently delete a legitimate only copy.
3. At states 3–4, retire residual quest orbs from inventory and bank through a
   logged migration/recovery transaction. They have no post-quest purpose.
4. State-4 saves must never replay 1,000 XP or completion UI. Reconcile a
   missing book/pouch through verified replacement/reward-retry ownership and
   inventory/bank checks. Preserve a legitimate second small pouch obtained
   before completion; the pinned Small pouch source explicitly permits that
   historical case.
5. Do not alter state merely because a legacy player used the erroneous safe
   Varrock teleport. Correct future service routing; there is no durable
   access bit to revoke.
6. Preserve `%rcu_abyssal_generator` only outside an active entry settlement;
   every new Wilderness teleport must choose a fresh one of the 12 native
   layouts. Never clear unrelated `%abyssal_warp` fields wholesale.

The pinned Mage and Small pouch pages disagree on whether the Wilderness Mage
or only the Dark Mage replaces a post-quest lost small pouch. Treat that as an
explicit live-capture question before assigning the permanent replacement
owner. It does not justify the current behavior, which provides no recovery
from either actor.

## 5. Detailed lifecycle audit

### Wilderness start, alignment, and Battle Runes

Both Mage locations refuse Talk and Trade while Saradomin- or Guthix-affiliated
items are equipped; inventory ownership alone is allowed, and Zamorak,
Bandos, Zaros, and other non-Saradomin/Guthix affiliations do not block. The
Teleport option remains usable after completion even when such gear is worn.
Current code has no equipment classification or gate. Implement one shared,
data-driven affiliation predicate over all worn slots, apply it before Talk
and Trade at both locations, and do not apply it to Wilderness Teleport.
Test mixed affiliations and every equipment slot rather than maintaining a
quest-local hand list.

Before Rune Mysteries, refusal is correct. Afterward, Wilderness Talk must say
that the location is unsafe, offer “Let's see what you're selling” and
“Alright, I'll go,” and write 0→1 exactly once. Re-talk retains the same shop
choice. Trade opens Battle Runes at any miniquest state. The post-completion
stock increases and replaces body runes with blood runes; bind both leaf IDs
to a generated, state-selected shop rather than narrating the service.

### Varrock offer and scrying-orb transaction

Restore the 23 March 2022 dialogue tree captured in the pinned transcript:
runecrafting inquiry, mercenary/false-loyalty branches, Deal/No deal/think
about it/rat-walk refusals, faithful-Saradomin rejection, and the dedicated
re-offer after “think about it.” Refusals must leave state 1. On acceptance,
reserve one free slot first, add exactly one empty orb, then write state 2.
The literal “Soft-skip” line and compressed Yes/No sequence are not shippable.

At state 2, each shared essence teleport may set only its own permanent bit.
Repeat teleports from the same source do not increase the count. On the third
distinct source, replace one carried empty orb with one charged orb. The
current delete-then-add is capacity-safe, but it assumes one active orb;
enforce ownership before the transition and make repeated packets idempotent.
All five sources must work:

| Source | Native bit | Shared return coordinate |
| --- | --- | --- |
| Aubury, Varrock | `%rcu_essencespot_aubury` | `0_50_53_53_9` |
| Archmage Sedridor, Wizards' Tower | `%rcu_essencespot_wizardstower` | `0_48_149_34_36` |
| Wizard Cromperty, East Ardougne | `%rcu_essencespot_cromperty` | `0_41_51_60_58` |
| Brimstail, Tree Gnome Stronghold | `%rcu_essencespot_brimstail` | `0_37_153_22_18` |
| Wizard Distentor, Wizards' Guild | `%rcu_essencespot_wizardsguild` | `0_40_48_31_14` |

The direct constant's Sedridor `32_35` and Cromperty `60_59` values are stale;
the hook correctly compares the shared constants above. Remove or correct the
unused duplicates and update the essence-mine header that still calls three
working sources deferred.

Orb recovery must use inventory-and-bank totals for both forms. If no orb
exists, require one free slot and replace one empty orb. If a charged orb is
banked, tell the player to retrieve it, as the transcript requires. A banked
empty orb is not lost and must not generate another. Destroy/loss, full
inventory, duplicate/repeated Talk, and three-bits-already-set recovery all
need explicit tests.

### Handover, completion, rewards, and journal

With a charged orb in inventory, consume exactly one and write state 3 before
the long explanation so interruption can resume safely. Restore the findings,
Z.M.I., direct-altar/Abyss, dangers, prayer, book, pouch, and Wilderness-only
access dialogue. Preserve the guide/helper's state-3 resume interaction; do
not award anything from state 2 or replay the handover after reconnect.

`~eta_quest_complete` currently writes 4 first, deletes only inventory orbs,
conditionally adds the book and then pouch, always awards XP, and always opens
completion. With zero free slots both items vanish; with one, only the book is
given. The proc also has no entry guard, so a direct/repeated invocation can
replay XP and completion presentation.

Make settlement idempotent and two-item-safe. Validate state 3 and reward
ownership/capacity before irreversible writes; either reserve two slots or
stage each item behind durable received facts with a retry dialogue. Then add
the exact missing items, award 1,000 XP once, write 4, and present completion
once. Clean quest orbs across inventory/bank without deleting unrelated
items. Verify current live behavior for a full reward inventory because the
pinned transcript has no full-inventory branch; safety and retry are required
regardless.

The journal dispatch is live and all five broad states are represented, but
state 2 only sees a charged orb in inventory and therefore gives false advice
for banked items. Show the number/distinct sources already recorded, identify
charged-orb retrieval, and use the shared `^journal_complete` styling. Journal
text must remain observational and never repair state.

### Permanent Wilderness teleport and entry penalties

After state 4, bind `opnpc4` on `rcu_zammy_mage1b` and the wilderness wrapper
to one authoritative entry proc. Talk at the Wilderness actor should still be
brief and expose danger/shop information; Varrock post-quest Talk supplies
lore and directs the player to the Wilderness, never teleports. Tele Block
does not prevent the Mage's teleport.

The entry transaction must, in order: validate completion/location and any
interaction lock; consume one charge from a worn abyssal bracelet or apply a
20-minute PK skull; drain Prayer to zero in either case; choose one of 12
native layouts and its matching outer-ring spawn; perform the teleport; and
publish the Wilderness Diary event only after arrival commits. Bracelet
objects `jewl_runerunning_bracelet_5..1` already exist: decrement 5→4→3→2→1
and remove the one-charge item. Inventory bracelets do not protect. Existing
skulls should be refreshed according to verified OSRS semantics, not cleared.

The current fixed `0_47_75_32_32` destination is in/near the centre and makes
all danger unreachable. `%rcu_abyssal_generator` and
`rcu_outer_multi1..12` already encode the per-player arrangements; set a new
layout and land in front of its blocked route as the cache expects. The Abyss
is not itself PvP, but the outer ring is multicombat against NPCs.

### Outer ring passages and creatures

The five local passage families correctly require a carried/equipped pickaxe,
carried/equipped axe, tinderbox, or no tool for Thieving/Agility. The chance
is correctly implemented as `(relevant level + 1)%`, capped at 100, with 25
XP on success. Failure correctly gives no XP. Everything after the roll is
missing: success only prints a message and leaves the player on the outer
side. Restore family-specific animations/loc animation, protected interaction
time, damage immunity while attempting, and movement to the matching inner
side. Failure leaves the passage intact and permits retry. Determine the
opposite tile from the clicked loc/orientation/layout; do not teleport every
passage to a generic centre tile or globally mutate the player varbit so all
12 wrappers change together.

The placed leeches, guardians (`abyssal_pyramid`), and walkers have current
combat stats, aggressive hunt mode, attacks, ashes, and dense outer-ring
spawns. They have no bound Wiki drop tables. Add each creature's exact
talisman, essence, binding-necklace, and other drops plus ownership-aware
small/medium/large/giant pouch rolls. Pouches are sequential: the next size is
chosen from inventory and bank ownership, no Runecraft level is needed to
receive it, and owning all four suppresses another roll unless one was lost.
Use each creature's pinned rate rather than one shared guessed rate.

### Inner ring rifts

Eleven handlers currently teleport to `runecraft_table:enter_coord` without a
talisman, which is the right base mechanism for Air, Mind, Water, Earth, Fire,
Body, Chaos, and Nature. Add the canonical gates before teleporting:

| Rift | Additional contract |
| --- | --- |
| Cosmic | Lost City complete (`%zanaris >= ^zanaris_complete`) |
| Law | Troll Stronghold complete and the shared Entrana prohibited-item check over inventory/equipment |
| Death | Mourning's End Part II complete (`%mourning_quest >= ^mend2_complete`) |
| Blood | Sins of the Father complete for the true Blood Altar; support the cache parent and true/Kourend destination preference |
| Soul | No ordinary click-through; unlock Kourend routing only after the documented dark-essence use/crafted-soul condition |
| Astral / Wrath | No Abyss rift; remain unavailable |

The cache includes Soul and three Blood loc variants, while the shared
Runecraft table stops at Death. Implement Blood/Soul altar data and their
use-on/preference lifecycle in the shared Runecraft modernization before
binding those locs. A direct numeric teleport must not bypass quest access,
Entrana restrictions, or future destination ownership.

### Essence pouches and Dark Mage

Small pouch capacity 3, level 1, and no degradation are correct. Current
pouches accept rune, pure, and daeyalt essence but not guardian essence and
store only one type per family; current OSRS accepts all four and the
single-type simplification is not equivalent for inventory behavior. Use a
real sub-container or an explicit per-type count schema. Fill/Empty must be
atomic at inventory boundaries and nonboostable Runecraft levels 1/25/50/75
must gate use, not acquisition.

Medium/large/giant/colossal degradation is currently a deterministic total
with a single degraded form. The pinned source describes per-essence random
threshold growth, reduced capacities, eventual disappearance, Runecraft/Max
cape protection, and Guardians lantern exception. Repair must find every
degraded pouch in inventory and bank, restore all at once, and reset wear
without requiring the item to be carried.

Dark Mage Talk is owned by Temple of the Eye and only branches to repair when
a carried degraded pouch is found; op3 Repairs has no handler. Centralize
Talk/Repairs in a shared Dark Mage dispatcher, preserving Temple of the Eye's
active quest branch, bank-aware repair, small-pouch and Abyssal-book recovery,
and post-quest lore. Death/drop must spill or erase stored essence as current
OSRS specifies; the present comment declaring this an uncloseable hook gap is
not completion evidence and needs an engine issue plus acceptance test if a
server hook truly remains unavailable.

### Downstream consumers and actor multiplexing

- Devious Minds correctly checks state 4 in its offer predicate, but its Law
  rift delivery depends on the missing Troll Stronghold/Entrana gate and on
  correct large/colossal pouch acquisition, degradation, and use.
- Wanted! currently treats state 4 as a hard start requirement. Canonical
  Wanted! permits the player to begin Enter the Abyss only far enough to meet
  the Mage, then uses the shared Mage for the 20-essence Solus clue. Replace
  the hard completion gate with the exact partial state and fix the comment
  claiming Quest Helper's Wilderness route is dead. The shared actor
  dispatcher must offer explicit topics without stealing ETA or shop access.
- Temple of the Eye correctly requires miniquest completion at its own start,
  but the Mage scene is a `Soft-skip` teleport directly to a fixed quest
  coordinate. Its one-time safe centre teleport is distinct, must not skull,
  must not count the Wilderness diary, and must coexist with normal Varrock
  ETA lore. Its Dark Mage puzzle remains heavily compressed.
- The Wilderness easy diary requires a successful post-quest Wilderness Mage
  teleport. The generic diary API only increments aggregate counts and has no
  per-task idempotency, so calling it directly on every entry would corrupt
  totals. Add a task-specific durable fact/event and derive the count once.
- Current Slayer assignment filters only combat/Slayer levels and block list.
  Abyssal demons therefore ignore their present requirement of Priest in
  Peril **or** partial Fairytale II. Add a general task quest/other-requirement
  predicate. Do not use Enter the Abyss completion as that assignment gate;
  the 1 October 2025-era contract is based on reachable demon locations.
- `::complete` writes state 4 only. That is acceptable as an administrative
  state adapter only if post-quest recovery can establish missing book/pouch
  safely; it must never masquerade as route or reward evidence.

### Debug and test adapters

`::etarun` fabricates three bits, transforms the orb directly, deletes reward
items, and calls the unguarded reward proc. It proves neither a source
teleport nor permanent access and can replay/destruct user state. Replace it
with isolated setup/assert helpers that refuse production accounts, preserve
unrelated inventory/bank data, exercise real source and actor entry points,
and assert state/items/XP/layout/penalties rather than writing success.

## 6. Modernization sequence

### Gate A — state, actor routing, and safe settlement

1. Lock the native 0–4/source-bit contract and add migration/reconciliation
   tests for duplicate/banked orbs and state-4 missing rewards.
2. Restore both Mage dialogue trees, equipped-god restriction, explicit topic
   multiplexing, and Battle Runes handler/state stock.
3. Make orb grant/recovery/charging/handover bank-aware and idempotent across
   all five shared teleport sources.
4. Make state-3 completion capacity-safe and exactly-once; repair journal and
   debug adapters.

### Gate B — authoritative Abyss entry and outer ring

1. Move normal Teleport ownership to Wilderness op4; remove safe Varrock
   entry and retain its lore/direction dialogue.
2. Complete shared PK skull acquisition/expiry/headicon and five-charge worn
   bracelet lifecycle; drain Prayer and preserve Tele Block behavior.
3. Randomize the 12 native layouts and matching outer spawns per entry.
4. Implement passage animations, attempt locks/immunity, success movement,
   failure/retry, and inner-ring arrival for all five families.

### Gate C — rifts, creatures, pouches, and recovery

1. Gate Cosmic/Law/Death/Blood, add Entrana equipment policy, and implement
   cache-native Blood/Soul routing while keeping Astral/Wrath absent.
2. Bind exact abyssal-creature drops with sequential, bank-aware pouch rolls.
3. Modernize pouch contents/degradation/death/drop and bank-aware repair.
4. Centralize Dark Mage Talk/Repairs/replacement and verify the Wiki's
   conflicting small-pouch replacement owner against live behavior.

### Gate D — consumers and integration

1. Correct Wanted!'s partial prerequisite and shared Mage topic routing.
2. Preserve Temple of the Eye's distinct one-time safe teleport and ensure it
   never fires normal entry penalties or diary credit.
3. Add an idempotent Wilderness easy task event on committed normal entry.
4. Add current Abyssal-demon Slayer assignment access requirements without
   coupling them to miniquest completion.
5. Run fresh, migrated, loss/recovery, and all consumer scenarios through real
   interactions; remove every player-facing `Soft-skip` and stale deferral
   claim in the audited surface.

## 7. Verification matrix

| Area | Required checks |
| --- | --- |
| Start/alignment | Rune Mysteries 3/4; all worn slots; Saradomin/Guthix block Talk/Trade; inventory-only allowed; Zamorak/other gods allowed; Teleport exempt |
| Battle Runes | Talk shop choice and Trade; states 0–4; pre/post exact stock; body→blood transition; repeated open; no state mutation |
| Offer | Every rewritten branch/refusal/re-offer; one free slot vs full; exactly one empty orb; state writes only after grant; Wanted! topic coexistence |
| Five sources | Every source independently; all ten source pairs; repeated same source; third conversion; already-three-bit replacement; exact return/exit behavior |
| Orb recovery | Empty/full in inventory or bank; neither; duplicates; Destroy; full inventory; charged-bank advice; repeated packet; state-3 reconnect |
| Completion | State 2 rejection; state 3 with 0/1/2 slots; book/pouch ownership; exact 1,000 XP and completion once; repeated proc/Talk/login; residual bank orbs |
| Migration | States 0–4; all 32 source-bit combinations; empty/full duplicates across inventory/bank; missing one/both rewards; legitimate two-small-pouch save; no XP replay |
| Actor routing | Wilderness leaves/wrapper Talk/Trade/Teleport at every state; Varrock never normal-teleports; Tele Block; transforms after state write |
| Entry penalties | Bracelet absent/in inventory/worn at charges 5..1; crumble; existing skull; Prayer 0; cancellation; repeated click; deathkeep/headicon/expiry |
| Layouts | All 12 generator values and matching spawns; fresh randomization; per-player isolation; no inner-ring landing; reconnect/cancellation |
| Passages | Five skills at levels 1/98/99; exact tools and both carried/worn categories; failure; 25 XP success only; animation/immunity; correct opposite tile; retry |
| Creatures/drops | Aggression/multicombat, attacks and ashes; each exact drop table/rate; next-pouch order from inventory+bank; lost tier; all tiers owned; no level acquisition gate |
| Rifts | Eight basic rifts; Cosmic 5/6; Law 49/50 plus every Entrana restriction; Death 59/60; Blood 137/138; Blood preferences/use-on; Soul unlock; Astral/Wrath absent |
| Pouches | Four essence types/mixing; levels; partial Fill/Empty capacity; each degrade stage; cape/lantern exception; disappearance; death/drop; bank repair; all-at-once repair |
| Recovery | Lost/banked book and pouch; full inventory; Dark Mage Talk/Repairs; NPC Contact integration if implemented; no duplicate suppression of legitimate second small pouch |
| Consumers | Devious Minds route; Wanted! partial and complete states; Temple one-time centre teleport/no penalties/no diary; Wilderness easy once; Slayer PIP/Fairytale-II truth table |
| Journal/debug | Every state and bank condition; source progress; standard completion styling; debug cannot award/replay or erase unrelated state |

Required static evidence includes a clean RuneScript/config build, duplicate
trigger and unresolved-symbol scans, actor/loc transform review, no unexpected
numeric IDs, and `python3 tools/questhelper_extract.py entertheabyss --check`.
Required runtime evidence is a command-free fresh 0→4 playthrough using at
least two different three-source combinations, every refusal/recovery and
inventory boundary, all 12 layouts, success/failure for every passage, all
rifts and restrictions, bracelet/skull/deathkeep behavior, pouch acquisition
and repair, a migrated-save matrix, and Wanted!/Temple/diary/Slayer integration.
A state write, completion scroll, fixed teleport, or successful compile alone
is not route proof.

## 8. Definition of done

Enter the Abyss is modernized only when a fresh Rune Mysteries-complete player
can meet the correctly restricted Wilderness Mage, use his real shop, navigate
the full Varrock offer/refusal tree, receive and recover exactly one active
orb, charge it from any three distinct working sources, hand it over, and
receive every reward exactly once under full inventory, banking, loss,
disconnect, and repeated interaction. Completion must unlock only the
Wilderness Teleport, which applies the correct Prayer/skull/bracelet lifecycle,
chooses one of 12 dangerous outer layouts, lets all five passages genuinely
reach the safe inner ring, and exposes only properly gated rifts. Creatures,
sequential pouch drops, pouch contents/degradation/death, bank-aware Dark Mage
repair/recovery, Battle Runes, journal, migration, Wanted!, Devious Minds,
Temple of the Eye, Wilderness Diary, and current Slayer assignment semantics
must all remain correct without debug commands or player-facing soft skips.
