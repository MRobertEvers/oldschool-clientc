# Dream Mentor modernization audit

Status: `audit-pending` — the local quest has a recognisable primary ladder,
Cyrisus recovery, a chest hand-off, potion assembly, four named bosses, a
completion call, and one working spell gate. It is not a playable or safe
revision-239 implementation. The start omits Lunar Diplomacy, the normal route
cannot obtain the dream vial, recovery and equipment selection bypass native
systems, shared-world NPC mutation lets players corrupt one another's quest,
the dream is not instanced and permits Prayer and teleporting, boss mechanics
and Cyrisus are absent, the native reward lamp is replaced by an inert generic
lamp, and six of seven reward spells are missing their completion gate.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to requirements, native persistence, Cyrisus's three
recovery measures, food rules, the bank equipment interface, shared potion
ownership, dream entry and exit, the four-boss encounter, death/recovery,
completion, rewards, spells, banking, Nightmare Zone, downstream quests,
diaries, migration, debug tooling, and verification. It is an implementation
specification, not evidence that the quest is complete.

## 1. Authoritative references

The current article and quick guide define requirements, route, encounter,
recovery, rewards, and unlocks. The transcript defines offer/refusal,
reassurance choices, equipment feedback, lost-item replacement, dream consent,
completion, and post-quest dialogue. Revisions were resolved through the OSRS
Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Dream Mentor](https://oldschool.runescape.wiki/w/Dream_Mentor?oldid=15292367) | 15292367, 2026-08-10 | Identity, requirements, full route, encounter, rewards, and unlocks |
| [Dream Mentor/Quick guide](https://oldschool.runescape.wiki/w/Dream_Mentor/Quick_guide?oldid=15279895) | 15279895, 2026-07-29 | Critical path, item counts, equipment sets, boss order, and reset behavior |
| [Transcript:Dream Mentor](https://oldschool.runescape.wiki/w/Transcript%3ADream_Mentor?oldid=15299716) | 15299716, 2026-08-14 | Offers, staged dialogue, recovery, ritual, cutscenes, and post-quest dialogue |
| [Cyrisus](https://oldschool.runescape.wiki/w/Cyrisus?oldid=15279856) | 15279856, 2026-07-29 | Recovery measures, combat style, dream ally, and state appearances |
| ['Birds-Eye' Jack](https://oldschool.runescape.wiki/w/%27Birds-Eye%27_Jack?oldid=14974013) | 14974013, 2025-08-26 | Equipment bank and no-seal banking reward |
| [Oneiromancer](https://oldschool.runescape.wiki/w/Oneiromancer?oldid=15205243) | 15205243, 2026-05-02 | Vial issue/replacement, ritual, and completion dialogue |
| [Dream vial](https://oldschool.runescape.wiki/w/Dream_vial?oldid=15184995) | 15184995, 2026-04-22 | Empty, water, and herb forms plus recovery |
| [Dream potion](https://oldschool.runescape.wiki/w/Dream_potion?oldid=15184994) | 15184994, 2026-04-22 | Recipe, ritual use, and loss behavior |
| [Our lives](https://oldschool.runescape.wiki/w/Our_lives?oldid=15202084) | 15202084, 2026-04-29 | The dream's only voluntary exit and encounter reset |
| [The Inadequacy](https://oldschool.runescape.wiki/w/The_Inadequacy?oldid=15292423) | 15292423, 2026-08-11 | Melee/ranged switching and A Doubt summons |
| [The Everlasting](https://oldschool.runescape.wiki/w/The_Everlasting?oldid=15199540) | 15199540, 2026-04-28 | Second encounter, combat profile, and safespot behavior |
| [The Untouchable](https://oldschool.runescape.wiki/w/The_Untouchable?oldid=15199541) | 15199541, 2026-04-28 | Third encounter, combat profile, and safespot behavior |
| [The Illusive](https://oldschool.runescape.wiki/w/The_Illusive?oldid=15199542) | 15199542, 2026-04-28 | Burrowing, relocation, final form, and Cyrisus's stomp |
| [A Doubt](https://oldschool.runescape.wiki/w/A_Doubt?oldid=15199544) | 15199544, 2026-04-28 | Inadequacy summon behavior |
| [Dreamy lamp](https://oldschool.runescape.wiki/w/Dreamy_lamp?oldid=15184988) | 15184988, 2026-04-22 | One-time 15,000-XP combat choice and exclusions |
| [Lunar spellbook](https://oldschool.runescape.wiki/w/Lunar_spellbook?oldid=15279811) | 15279811, 2026-07-29 | Seven completion-gated reward spells |
| [NPC Contact](https://oldschool.runescape.wiki/w/NPC_Contact?oldid=15279366) | 15279366, 2026-07-29 | Cyrisus equipment feedback and post-quest contact |
| [Spellbook Swap](https://oldschool.runescape.wiki/w/Spellbook_Swap?oldid=15252700) | 15252700, 2026-07-04 | One-spell-or-two-minute temporary swap contract |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-21 | Quest eligibility and three unlocked bosses |
| [Seal of passage](https://oldschool.runescape.wiki/w/Seal_of_passage?oldid=15273680) | 15273680, 2026-07-24 | Lunar access, death loss, replacement, and bank exception |

The Wiki currently names the banker page `'Birds-Eye' Jack`; the revision-239
NPC and local dialogue use the historical display spelling `'Bird's-Eye'
Jack. Symbolic cache names and the actual client display must win over prose
spelling when implementing triggers.

Transition aid only: Quest Helper at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/dreammentor)
observes the native primary ladder, recovery thresholds, equipment choices,
item alternatives, world locations, and encounter presence. `python3
tools/questhelper_extract.py dreammentor --check` resolves the expected
`quest_dreammentor` dbrow and all referenced gamevals. Quest Helper is a
state/test oracle, not server behavior evidence.

## 2. Canonical contract

Dream Mentor is a members-only, master, medium quest released 15 May 2007. It
starts by speaking to the fallen man in the small cave within the Lunar Isle
mine. Starting requires combat level 85 and completion of Lunar Diplomacy and
Eadgar's Ruse. The encounter requires defeating four bosses in one run without
Prayer; this is a gameplay requirement, not optional flavor.

A canonical run must:

1. enforce both quest prerequisites and combat 85 before an explicit
   accept/refuse branch, then preserve each re-talk checkpoint;
2. let the player inspect Cyrisus's Health, Spirit, and Armament panel, nurse
   him with approximately 20 pieces from at least three food types without
   repeating the recent type, and improve Spirit through positive dialogue;
3. open Cyrisus's real bank through Jack, select exactly one item for each of
   five slots, support NPC Contact feedback, and reject the wrong set;
4. accept the correct chest, continue food and reassurance until Health,
   Spirit, and Armament all reach 100, and play the associated appearances and
   scenes;
5. obtain or replace the Dream vial from the Oneiromancer, fill it, add
   goutweed, hammer an astral rune, grind the shards, and add the ground rune;
6. light the ceremonial brazier with the potion and tinderbox, stage Cyrisus
   at the hall, and require explicit consent before entering the dream;
7. create a private, unsafe dream instance; disable Prayer, emergency
   teleports, rings of life, and escape crystals; and make `Our lives` the only
   voluntary exit, resetting all four bosses when used;
8. fight The Inadequacy, The Everlasting, The Untouchable, and The Illusive in
   that fixed order, with A Doubts, burrowing, transforms, Cyrisus's courage,
   allied attacks, Vengeance Other, and final stomp represented;
9. preserve ordinary unsafe-death/grave semantics outside the instance,
   restart the sequence after exit or death, and make the seal recoverable
   through its canonical owner; and
10. return to the Oneiromancer with the seal, play the knowledge-transfer and
    completion scenes, award the exact rewards once, and activate every
    downstream owner.

The required preparation is a seal of passage; three different food types,
normally six or seven of each for 20 total; goutweed; one astral rune; a
tinderbox; a hammer (including the supported Imcando variant); a pestle and
mortar; and combat equipment, food, and supplies. Sacks and baskets count
according to their canonical extraction rules. Unsupported items such as
purple sweets must produce their real refusal rather than silently advancing a
counter.

Completion awards 2 quest points, 15,000 Hitpoints XP, 10,000 Magic XP, and a
Dreamy lamp that grants 15,000 XP to one combat skill other than Attack or
Prayer. It unlocks Monster Examine, Humidify, Hunter Kit, Stat Spy, Dream,
Plank Make, and Spellbook Swap; lets the player bank with Jack without a seal;
and unlocks The Inadequacy, The Everlasting, and The Untouchable in Nightmare
Zone. Dream Mentor is required by Dragon Slayer II and While Guthix Sleeps and
is a practical gate for the hard Desert and Kourend & Kebos diary spell tasks
and the elite Varrock Plank Make task.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID | 134 |
| Dbrow | `quest_dreammentor` |
| Type / difficulty / length | Members; master; medium |
| Release | 15 May 2007 |
| Start | `dream_cyrisus_unconscious` (NPC 3465), coordinate 575154280 |
| Primary | `%dream_prog`, bits 0–5 of `dream_main` |
| Recovery | `%dream_health` bits 9–15; `%dream_spirit` bits 16–22; `%dream_armament` bits 23–29 |
| Style / scene | `%dream_combattype` bits 6–7; `%dream_lastfood` bit 8; `%dream_banker_intro` bit 30; `%dream_cutscene_seen` bit 31 |
| Secondary | `%dream_spirit_ques` bits 0–3 and `%dream_arma_item1..5` five-bit fields in `dream_main2` |
| Reward ledger | `%dream_lampused`, bit 29 of `dream_main2` |
| Encounter support | `%dream_damagedealt` bits 0–7 and `%dream_cyris_multi` bits 8–10 of `dream_main3` |
| Shared brazier | `%lunar_brazier_lit`, bit 9 of `canoeing_menu` |
| Native end | 28 |
| Rewards | 2 QP; 150000 raw Hitpoints XP; 100000 raw Magic XP |

The dbrow's decoded quest requirements point at unrelated quests, a known
metadata corruption in this cache. Start scripts must explicitly gate Lunar
Diplomacy and Eadgar's Ruse rather than trusting those rows.

`dream_main`, `dream_main2`, and `dream_main3` are correct native carriers.
They should remain field-written through varbits; whole-carrier writes would
destroy adjacent quest state. Exact value meanings for `%dream_combattype`,
`%dream_cyris_multi`, `%dream_lastfood`, and the five equipment indexes must be
confirmed from a revision-239 clientscript/cache trace before authoring
constants. The current local 1/2/3 style values are explicitly invented and
must not be treated as native evidence.

### 3.1 Primary ladder

| `%dream_prog` | Canonical checkpoint | Current local result |
| ---: | --- | --- |
| 0 | Not started / fallen man offer | Combat and Eadgar only; Lunar Diplomacy omitted |
| 2 | Offer accepted / initial inspection | Omitted; local jumps directly to 4 |
| 4 | First food phase | Present as a four-item +10 counter |
| 6 | First talk / second food phase | Present, but dialogue and Spirit are omitted |
| 8 | Second feed checkpoint | Omitted |
| 10 | Reassurance / sitting transition | Omitted |
| 12 | Third food and dialogue phase | Present only as a generic feed-to-100 stage |
| 14 | Standing/re-talk transition | Omitted |
| 16 | Equipment selection and final recovery | Present, but bank selection and final recovery are bypassed |
| 18 | Fully restored / Oneiromancer next | Written as soon as any chest is handed over |
| 20 | Dream vial and potion preparation | Present, but no vial can be issued on this route |
| 22 | Potion prepared / ritual re-talk | Omitted |
| 24 | Dream encounter active | Static shared arena; `%dream_spirit` is corrupted as a boss counter |
| 26 | Dream won / return to Oneiromancer | Forced teleport after fourth generic death |
| 28 | Complete | Shared completion runs, but item and unlock transaction is wrong |

The omitted values are real checkpoints, not spare integers. Modernization
should restore them where the transcript, cache transforms, and helper agree,
while allowing old local saves at the retained endpoints to recover safely.

### 3.2 Native interfaces and entities

Revision 239 already contains the machinery that local comments claim is
missing:

- `dream_cyrisus` interface 521 displays Health, Spirit, and Armament with
  native thresholds and clickable information;
- `dream_armour` interface 260, `dream_bank_inventory` (36 slots),
  `dream_crate_inventory` (five slots), and clientscripts
  `dream_bank_init`/`dream_chest_init` implement the selection surface;
- all 15 canonical equipment objects resolve symbolically, including
  `adamant_armoured_boots`, `magictraining_infinityboots`, and
  `staff_of_zaros`;
- `dream_couragebar` interface 520 plus `dream_title`, `dream_player_stats`,
  and `dream_monster_stat` provide the encounter HUD;
- `dream_plinth`, displayed as `Our lives`, has Read and Leave operations;
- `dream_lantern` is the real Dreamy lamp and already has Rub and Destroy
  menu operations; and
- battle variants for melee, ranged, and caster Cyrisus plus spawn,
  transform, burrow, Vengeance, and stomp sequences/spotanims exist.

Every panel must be mounted with the modern named slot, receive the vars its
onload clientscript reads, arm server-handled operations on each mount, and be
verified from a real client capture. Hand-painted substitutes are unnecessary.

### 3.3 Save migration

Add a versioned, idempotent migration before modern handlers interpret these
fields. It must identify saves from this condensed local implementation and
snapshot all three carriers, primary state, all recovery fields, style,
equipment slots, scene bits, lamp ledger, brazier state, and quest-item
ownership across inventory, bank, reclaim, and private ground storage.

- Preserve native primary values. Values 2, 8, 10, 14, and 22 were never
  produced locally and must not be remapped as legacy states.
- Local states 18 and later prove that the condensed chest was accepted; they
  may reconcile Health, Spirit, and Armament to 100 after provenance is
  confirmed. State 16 does not prove that equipment was selected or final
  recovery occurred.
- At local state 24, `%dream_spirit` values 0–4 are an invalid boss counter,
  not a recovery percentage. Restore canonical Spirit to 100 and restart the
  private encounter at The Inadequacy; boss progress is intentionally
  ephemeral because leaving must reset it.
- Do not copy the local 1/2/3 `%dream_combattype` values. Reconstruct the native
  appearance only from a confirmed local provenance marker and the combat
  calculation or accepted equipment evidence, then set `%dream_cyris_multi`
  using traced native encodings.
- A state-16 local chest without equipment slot bits should reopen the native
  bank workflow. Do not fabricate a correct selection merely to keep the old
  shortcut moving.
- Completed local saves received a generic `thosf_reward_lamp`, which is
  indistinguishable from rewards used by other quests. Never delete or convert
  a generic lamp without quest-specific provenance. Establish a durable
  unclaimed Dreamy-lamp entitlement when `%dream_lampused` is clear, and let
  the Oneiromancer recover the native lamp.
- Reconcile XP and quest points through a one-time completion ledger; never
  repeat the already granted 15,000/10,000 XP merely because the lamp needs
  repair.
- Preserve unrelated packed bits and legitimate current-native saves. Log
  ambiguous ownership for review instead of guessing.

Migration tests must cover every locally emitted state, 0–4 misuse of Spirit,
all three invented style values, chest/potion/lamp in every storage location,
full inventory, encounter relog, repeated migration, and mixed Dream Mentor /
Dragon Slayer II potion ownership.

## 4. Current implementation surface

The quest root contains six files and 681 lines:

| File | Lines | Current responsibility |
| --- | ---: | --- |
| `configs/dreammentor.constant` | 175 | State constants, coordinates, and stale simplification claims |
| `configs/dreammentor.varp` | 23 | Three native carrier declarations |
| `scripts/dreammentor_cyrisus.rs2` | 215 | Cave access, recovery, morphs, chest, Jack, and dream entry |
| `scripts/dreammentor_dream.rs2` | 170 | Oneiromancer branch, potion, brazier, and boss sequence |
| `scripts/dreammentor_journal.rs2` | 37 | Condensed dynamic journal |
| `scripts/dreammentor_shared.rs2` | 61 | Shared spawn/feed/style/completion helpers |

Gate A also owns these external surfaces:

- Dragon Slayer II's `dragonslayer2.rs2`, which owns the only
  `[opnpc1,lunar_oneiromancer]` trigger and separately uses the same dream-vial
  forms;
- Lunar Diplomacy's Oneiromancer fallback, seal issue/replacement, transport,
  Lunar Isle NPC transforms, and spellbook state;
- `general_use/scripts/hammer.rs2` and Herblore's
  `grind_ingredient.rs2`, which route the astral transformations;
- Lunar utility, player-target, and spellbook-swap scripts containing the
  seven reward spells;
- global bank option handlers and Jack's native option mapping;
- ordinary death, grave, teleport, ring-of-life, escape-crystal, Prayer, and
  instance lifecycle owners;
- Nightmare Zone boss eligibility and spawn tables;
- Dragon Slayer II, While Guthix Sleeps, achievement diary, and quest-cheat
  consumers; and
- static spawns in the Lunar mine and dream map plus all native transforms.

The shared Oneiromancer header residing in a later quest root is an ownership
defect. Move it to a neutral Lunar/quest dispatcher or a single clearly owned
shared file with explicit precedence for Dragon Slayer II, Dream Mentor, Lunar
Diplomacy, and default dialogue. Compilation order must not decide which quest
works.

## 5. Requirement, access, and start defects

- The unconscious Cyrisus checks combat 85 and Eadgar's Ruse but never checks
  Lunar Diplomacy. Physical travel through another quest is not an acceptable
  prerequisite gate; alternate transports and debug positioning can start it.
- The offer is a two-line approximation that writes 4 and clears Health. It
  omits primary state 2, the full offer/refusal transcript, status inspection,
  and re-talk behavior.
- `dream_cave_wall_entrance` teleports in either direction without validating
  quest/access state, approach, busy state, or a safe destination.
- The native Inspect operation and recovery interface have no handler.
- The journal claims all three requirements but does not enforce one of them.
- `::complete` writes only 28 and cannot establish the lamp, spell, bank,
  Nightmare Zone, or migration invariants. It is not idempotent completion
  preparation.

Modernization must centralize the hard requirement predicate, use it in start,
journal, and debug tests, preserve explicit refusal, and exercise access from
the real Lunar Diplomacy transport rather than relying on teleport cheats.

## 6. Cyrisus recovery and actor ownership

### 6.1 Health, Spirit, and food

The local implementation accepts only 12 hard-coded foods, increments Health
by 10, and reaches 100 after ten items. It accepts consecutive identical food,
does not support the three-type cycling contract, sacks/baskets or canonical
refusals, and ignores `%dream_lastfood`. The real route uses about 20 pieces in
several phases, with dialogue between phases and more feeding after equipment.

Spirit is entirely absent. `%dream_spirit_ques` is never read or written,
wrong and positive dialogue choices do not exist, and `%dream_spirit` is later
overwritten as encounter progress. Armament is set to 100 immediately on chest
hand-in. Consequently the local player never earns the three 100% measures
that the quest is about.

Implement a data-driven food classification and recent-type rule, including
container extraction and refusal cases. Make every consume-and-progress action
atomic: validate stage and quantity, reserve capacity if a replacement is
needed, delete exactly once, then update the corresponding native field.
Restore the staged reassurance questions, harmless negative answers, native
animations/scenes, and the Health/Spirit/Armament interface at every inspection
point. Journal text should describe the lowest incomplete measure rather than
only the broad primary state.

### 6.2 Shared-world mutation

The static fallen man is deleted globally with `npc_del`; each later form is
then hand-spawned through a radius-based `npc_find`/`npc_add` helper. One player
can remove, replace, or suppress another player's Cyrisus, and nearby stale
forms can block the expected spawn. The same helper creates globally shared
bosses and awards death credit through `npc_findhero`.

Use the native per-player multiloc/multinpc state where available or a
player-owned actor registry with explicit owner tokens, type checks, respawn,
relog reconstruction, and cleanup. Never use proximity as ownership. The cave,
Oneiromancer, brazier, and dream appearances must derive from the player's
state without changing another player's visible quest.

The outside Cyrisus wrappers are currently default/invisible because the local
quest never writes `%dream_cyris_multi`. The player is told to meet Cyrisus at
the brazier but can only progress through whichever cave-spawned variant still
exists. Trace the native wrapper values and stage the correct style-specific
appearance at both Oneiromancer and brazier checkpoints.

## 7. Cyrisus's bank and equipment

The local comment says adamant boots, infinity boots, and ancient staff are
absent. All three, all other canonical choices, the bank interface, both
inventories, and both initialization clientscripts are present. The shortcut
is therefore not an engine or cache limitation.

Jack currently calculates a style, gives one `dream_chest`, and never opens
Cyrisus's bank. It ignores `%dream_arma_item1..5`, `%dream_banker_intro`, chest
contents, slot restrictions, wrong-set feedback, NPC Contact, Look-in, bank
or ground ownership, inventory capacity, and the result of `inv_add`. A full
inventory can advance dialogue without granting a recoverable chest; a chest
elsewhere can permit duplication; handing it over deletes without checking the
result.

The current NPC Contact cast only reports that no addressable roster exists.
The equipment check therefore also requires the shared NPC Contact picker and
its stage-specific Cyrisus conversation; it cannot live solely in the bank
panel.

Restore the 36-choice bank and five-slot chest. Permit exactly one helmet,
body, legs, boots, and weapon; preserve swaps/removals; serialize each choice
to its native field; and compare the complete set against the combat-style
contract:

| Style determining combat level | Helmet | Body | Legs | Boots | Weapon |
| --- | --- | --- | --- | --- | --- |
| Melee | Dragon med helm | Ahrim's robetop | Ahrim's robeskirt | Ranger boots | Abyssal whip |
| Ranged | Splitbark helm | Karil's leathertop | Torag's platelegs | Adamant boots | Magic shortbow |
| Magic | Robin hood hat | Dragon chainbody | Black d'hide chaps | Infinity boots | Ancient staff |

Mount the native interface, run its clientscripts, re-arm every server op, and
test close/reopen and reconnect with partial choices. NPC Contact must report
whether the selection suits Cyrisus without consuming or completing it. The
chest must reflect those exact five items when inspected and be recoverable
without duplication. Only a validated correct chest may set Armament to 100;
Health and Spirit still need their final phase afterward.

Audit every Jack menu option. Talk-to currently offers banking, but no exact
Bank or Collect-option trigger was found. Before completion his banking must
require a seal; after completion the canonical Jack service must waive it.
Do not accidentally waive the seal at unrelated Lunar Isle bankers.

## 8. Dream potion and cross-quest ownership

The Oneiromancer explains the recipe at state 18 but does not issue an empty
vial. Only Dragon Slayer II's higher-precedence branch can issue the same item,
and only while that quest is in its own dream range. A normal Dream Mentor run
therefore stops at state 20.

Every recipe operation is inventory-only and non-transactional. Sink, herb,
ground-rune, hammer, and pestle handlers delete/add without checking results.
The two global tool hooks have no Dream Mentor or Dragon Slayer II state gate,
so any player can transform an astral rune. None of the four vial forms has a
quest-specific Destroy/replacement ledger; generic drop behavior can publish
them to the world.

Create one neutral dream-potion owner used by both quests. It must:

- derive eligibility from either quest's exact stage and retain an explicit
  purpose/owner token where the identical objects cannot prove origin;
- count all relevant storage and private-ground locations before issuing a
  vial, while still offering canonical loss replacement;
- make each conversion atomic and accept both use-on directions and supported
  hammer variants;
- reject crafting outside an eligible stage without consuming ingredients;
- preserve one quest's potion when talking about or progressing the other;
- prevent public quest-item drops, use canonical Destroy confirmation, and
  route recovery to the Oneiromancer; and
- handle full inventory, repeated clicks, logout between delete/add, and two
  concurrently active quest uses without loss or duplication.

The first brazier interaction must validate state, owner, potion, tinderbox,
and actor presence before consuming anything. `%lunar_brazier_lit` is a shared
permanent bit and needs a traced reset/visibility contract; it cannot be used
as sufficient proof that this player completed the ritual. Preserve first-time
and repeat-entry scenes separately via the native scene fields.

## 9. Dream instance, restrictions, and recovery

The local quest teleports into a static map where The Inadequacy is globally
spawned and adds later bosses to that same world. It creates no instance, has
no owner or encounter token, and relies on `npc_findhero` for credit. Two
players can share kills, suppress spawns, or advance the wrong save. Relog,
death, simultaneous death, stale queues, and abandoned encounters have no
defined outcome.

Build the arena with the modern map-instance API and record one player owner
and one encounter generation. Spawn only the expected boss and owned adds;
validate owner, generation, NPC type, and expected phase on every hit, timer,
death, and transition; suppress ordinary loot; and clean up on leave, death,
logout, completion, or timeout. Re-entry while state 24 must create a fresh
instance beginning at The Inadequacy.

The instance is unsafe. Route death through normal grave/item-loss behavior,
placing recoverable items outside the instance and never restoring Hardcore
status. Explicitly test the seal's loss/replacement contract. Simultaneous
player/boss death must follow the canonical credit decision and cannot award a
kill from a stale death queue after cleanup.

At entry, deactivate and block all Prayer activation paths, quick Prayer, and
restoration-assisted activation. Block spell, tablet, jewellery, minigame,
home, emergency, ring-of-life, escape-crystal, and other transport escape
paths. `dream_plinth`/`Our lives` must implement Read and Leave; leaving is the
sole voluntary exit and always discards encounter progress. These rules belong
to a general area/instance restriction facility where possible, not scattered
special cases in every teleport script.

## 10. Bosses and Cyrisus ally

The fixed order below is canonical; the local comment calling it an invention
is stale. Current boss handlers do nothing beyond generic retaliation and a
death message.

| Phase | Required behavior | Current gap |
| ---: | --- | --- |
| 1 — The Inadequacy | Melee in range, Ranged outside; summon owned A Doubts | Generic retaliation; no style switch or summons |
| 2 — The Everlasting | Correct 3x3 melee/pathing/defence and north-plinth safespot | Generic retaliation only |
| 3 — The Untouchable | Correct 2x2 melee/pathing/defence and safespot | Generic retaliation only |
| 4 — The Illusive | Burrow, relocate, noncombat/final transform, Cyrisus stomp | Generic retaliation followed by immediate teleport |

Cyrisus never appears in the local arena. Restore the correct melee, ranged, or
caster battle NPC as an owned ally, display the Courage bar, update
`%dream_damagedealt` and the native encounter UI according to traced behavior,
and implement his intermittent attacks and Vengeance Other support. His action
queues must obey the same owner/generation/cleanup contract as the bosses.

Use cache-authored spawn, attack, defend, death, burrow, transformation, and
stomp assets. Validate sizes, collision, attack distance/speed, max hits,
accuracy, immunity, add caps, target selection, and safespot geometry against
the pinned pages and live revision-239 data. The final phase should play
Cyrisus's stomp and wake/knowledge scenes before state 26; it must not teleport
the player directly from a generic NPC death queue.

## 11. Completion, rewards, and downstream unlocks

`~dreammentor_finish` writes state 28 before granting XP or the item, performs
unchecked grants, and has no retry ledger. A crash, full inventory, or duplicate
resume can lose the reward or duplicate XP. Completion must instead be one
idempotent transaction with separately durable XP, lamp entitlement, quest
points, completion-count, and scene markers. Only then should the final scroll
and post-quest state become visible.

The local reward is `thosf_reward_lamp`; no generic Rub handler exists. Use
native `dream_lantern`, check `%dream_lampused`, offer Strength, Defence,
Ranged, Magic, or Hitpoints only, grant exactly 15,000 XP once, handle maximum
XP, and support replacement before use. The completion scene should require
the canonical seal/context and include Cyrisus's knowledge transfer and
Oneiromancer re-talk rather than a two-line summary.

The seven spells are not merely completion-scroll text:

| Spell | Current result | Required modernization |
| --- | --- | --- |
| Hunter Kit | Explicitly gates state 28; creates native box | Retain gate; verify canonical contents, transactions, visibility, and full inventory |
| Monster Examine | No quest gate; HP-only readout | Gate cast/UI; implement canonical stat display and unsupported-target behavior |
| Humidify | No quest gate; fills only vials, buckets, jugs | Gate cast/UI; implement current container set and atomic conversion |
| Stat Spy | No quest gate; always reports player targeting unavailable | Gate cast/UI; route through the now-hosted player-target machinery and exact display |
| Dream | No quest gate; flat six-HP heal | Gate cast/UI; implement recurring regeneration and action interruption |
| Plank Make | No quest gate; converts up to five logs in one cast | Gate cast/UI; restore current single-target/cost/animation/queue contract |
| Spellbook Swap | No Dream Mentor gate; timer-only revert | Gate cast/UI; revert after one eligible spell or two minutes, whichever comes first |

Completion gating must affect client visibility/usability and server cast
guards. A crafted packet must not bypass a hidden spell. Runes, XP, animation,
cooldown, target validation, and conversion must occur only after every
quest-specific precondition passes.

Audit these additional consumers:

- Jack's Talk-to, Bank, and Collect menu operations before and after completion;
- Nightmare Zone eligibility plus normal/hard variants for The Inadequacy,
  The Everlasting, and The Untouchable (not The Illusive), including the
  Dream Mentor/Desert Treasure point modifier;
- Dragon Slayer II and While Guthix Sleeps start gates without permitting
  their debug helpers to mutate Dream Mentor completion;
- hard Desert Humidify, hard Kourend & Kebos Monster Examine, and elite Varrock
  Plank Make diary event hooks; and
- the dynamic journal, completion registry, quest points, music unlocks, and
  `::complete quest_dreammentor` idempotence.

## 12. Journal, item recovery, and failure paths

The current seven-branch journal collapses Health, Spirit, equipment selection,
potion forms, ritual readiness, and boss reset state. Rebuild it from native
fields and owned items so it accurately names the next recoverable action.
Completed text should list the real outcome without claiming an unlock whose
consumer is absent.

Define an ownership matrix for the Dream vial's four forms, astral shards,
ground astral, Cyrisus's chest and its five selected items, Dreamy lamp, and
seal of passage across inventory, bank, worn, reclaim, instance ground, and
ordinary private ground. For each quest stage specify Keep/Drop/Destroy,
replacement NPC, duplicate prevention, full-inventory response, and behavior
after death/relog. Do not use inventory-only absence as proof of loss.

Every staged action needs an explicit retry path: refusing the offer; wrong
food; negative reassurance; closing recovery/bank panels; incomplete or wrong
equipment; lost chest; missing vial; wrong ingredient order; full inventory;
lighting without Cyrisus; declining dream entry; leaving through `Our lives`;
death at each boss; logout at each scene; simultaneous final death; completion
resume; lost unused lamp; and post-completion Jack banking without a seal.

## 13. Modernization sequence

1. Freeze the pinned reference set, trace the uncertain native varbit/index and
   clientscript contracts, and add a machine-readable state/entity/owner map.
2. Add the versioned legacy-save migrator and tests before changing meanings of
   Spirit, style, equipment slots, scene state, or lamp entitlement.
3. Centralize start requirements and shared Oneiromancer routing; restore the
   offer, re-talks, Inspect/status panel, and per-player Cyrisus appearances.
4. Implement data-driven food and Spirit phases with atomic consumption and
   exact primary checkpoints.
5. Mount and wire the native equipment bank/chest, style validation, NPC
   Contact, recovery, and Jack's complete option surface.
6. Create the shared Dream Mentor/Dragon Slayer II potion owner with atomic
   transformations, item recovery, and explicit ritual state.
7. Build the private unsafe dream instance, restriction policy, `Our lives`
   reset, grave/death routing, generation-safe cleanup, and relog behavior.
8. Implement Cyrisus's ally state and all four boss mechanics using native
   assets and deterministic encounter tests.
9. Make completion/reward delivery idempotent; implement the native Dreamy
   lamp, scenes, banking, spells, Nightmare Zone, diaries, music, and downstream
   quest gates.
10. Rebuild the journal and debug adapter, run Gate D, attach real-client
    captures, and change status only when the entire route and consumers pass.

No quest-specific C shortcut is justified by this audit. If Prayer/teleport
restrictions, recurring Dream interruption, player-target Stat Spy, or instance
grave routing expose a genuine VM gap, add one general tested capability and
keep Dream Mentor policy in RuneScript/config.

## 14. Verification matrix

### Static and pack checks

- Run `python3 tools/questhelper_extract.py dreammentor --check`.
- Add a quest contract check that resolves the dbrow, primary/end states, three
  carriers, all 15 equipment objects, potion forms, native interfaces,
  clientscripts, `Our lives`, Cyrisus variants, four bosses, A Doubt, and
  Dreamy lamp.
- Fail on duplicate Oneiromancer/Jack triggers, raw IDs, whole-carrier writes,
  shared-world dream spawns, unchecked quest-item transactions, a generic lamp,
  or any of seven spell handlers lacking a Dream Mentor guard.
- Run `make -C src torirsserver-scripts` and the revision-239 pack check.

### Transition and persistence tests

- Exercise 0→2→4→6→8→10→12→14→16→18→20→22→24→26→28, every refusal/re-talk,
  all recovery thresholds, positive/negative Spirit choices, and inspection at
  each state.
- Cover valid three-food rotation, recent repeats, all supported categories,
  sacks/baskets, invalid foods, empty containers, full inventory, and repeated
  use packets.
- Test all three combat-style outcomes and ties, all 15 correct choices, every
  wrong/missing/duplicate-slot set, NPC Contact, close/reopen, relog, chest
  loss, and cross-player isolation.
- Test both quests' potion stages independently and concurrently, both item-use
  directions, every intermediate item location, ingredient failure, supported
  hammers, loss/replacement, repeated packets, and transactional rollback.
- Enter, leave, die, relog, and reconnect at every boss; verify restart at boss
  one, private ownership, no stale credit, no loot, no cross-player visibility,
  grave placement, seal recovery, and simultaneous-death behavior.
- Attempt every Prayer activation and teleport class inside the instance;
  verify only `Our lives` exits voluntarily and that Read/Leave remount safely.
- Verify completion and migration twice, with full inventory and interruption
  after each durable reward step; XP, QP, lamp, completion count, and unlocks
  must remain exactly once.

### Combat and downstream tests

- Verify Inadequacy range-style switching and owned A Doubts; Everlasting and
  Untouchable collision/safespots; Illusive burrow/relocation/final form; and
  Cyrisus style, courage, attacks, Vengeance, and stomp.
- Assert exact levels, HP, size, attack cadence, max hit, animations,
  transformations, zero-loot policy, phase order, and cleanup from live
  revision-239 observations.
- Before state 28, reject all seven reward spells server-side and show their
  correct client lock. After state 28, run each success and failure contract,
  including Spellbook Swap's next-cast reversion.
- Test Jack with/without a seal on every menu option before and after
  completion, three Nightmare Zone boss selections, downstream quest starts,
  and all three diary events.
- Test Dreamy lamp choices, excluded Attack/Prayer, maximum XP, replacement,
  destruction, relog, duplicate use, and `%dream_lampused` persistence.

### Real-client evidence

Capture the real start requirement failure and acceptance; recovery panel at
each threshold; equipment bank initialization, choices and chest; NPC
Contact response; Oneiromancer replacement; potion transformations; brazier
and consent scenes; private dream HUD; every boss and Cyrisus mechanic; blocked
Prayer/teleports; `Our lives` reset; death/grave recovery; knowledge transfer;
completion scroll; Dreamy lamp; each spell; no-seal Jack banking; Nightmare
Zone selection; and journal/relog behavior.

`verified-modern` requires all critical paths and permanent consumers above to
pass. Missing dialogue, animation, or scene detail is cosmetic only if the
exact deviation, player impact, Wiki revision, and regression test are recorded;
the current recovery, equipment, instance, combat, reward, and spell shortcuts
are all critical and cannot be waived.
