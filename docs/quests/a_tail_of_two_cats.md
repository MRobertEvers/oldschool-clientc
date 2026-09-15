# A Tail of Two Cats modernization audit

Status: `audit-pending` — native quest state, cache transforms, and the shared
completion scroll exist, but a legitimate player cannot start the quest, Bob
does not run this quest's dialogue, the chore sequence cannot complete, and
the cutscene, locator, reward-container, reclaim, music, kudos, and downstream
contracts are absent or bypassed.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the complete quest, Bob's shared world
presence, the Catspeak amulet(e) locator, both narrative cutscenes, Unferth's
five chores, and every post-quest integration. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

These revisions are pinned so implementation and review use a stable route,
dialogue, item, timer, cutscene, reward, replacement, and downstream contract.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [A Tail of Two Cats](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats?oldid=15302023) | 15302023, 2026-08-15 | Requirements, complete route, Bob's world behavior, cutscenes, chores, disguise, rewards, music, kudos, and downstream quests |
| [A Tail of Two Cats/Quick guide](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats/Quick_guide?oldid=15270143) | 15270143, 2026-07-20 | Ordered native checkpoints, required actions/items, locator use, equipment checks, and finish |
| [Transcript:A Tail of Two Cats](https://oldschool.runescape.wiki/w/Transcript%3AA_Tail_of_Two_Cats?oldid=15263326) | 15263326, 2026-07-14 | Acceptance/refusal, re-talks, cat interjections, research, Sphinx choices, chore hints, medical arc, cutscenes, and completion |
| [Catspeak amulet(e)](https://oldschool.runescape.wiki/w/Catspeak_amulet%28e%29?oldid=15275381) | 15275381, 2026-07-25 | Enchantment, uniqueness, Open/Locate behavior, compass feedback, loss, and replacement |
| [Bob (cat)](https://oldschool.runescape.wiki/w/Bob_%28cat%29?oldid=15303543) | 15303543, 2026-08-16 | World spawn/wander contract, locator feedback, quest history, and post-quest mouse-toy reclaim |
| [Chores](https://oldschool.runescape.wiki/w/Chores?oldid=15185466) | 15185466, 2026-04-22 | Five independent tasks, dynamic Read state, destroy text, and replacement through the player's cat |
| [Unferth](https://oldschool.runescape.wiki/w/Unferth?oldid=15232719) | 15232719, 2026-06-13 | Hair growth, repeated Crafting success rolls, visual stages, and NPC identity |
| [Unferth's patch](https://oldschool.runescape.wiki/w/Unferth%27s_patch?oldid=15232684) | 15232684, 2026-06-12 | Three weed states, seed planting, potato growth, and quest-only farming behavior |
| [Recipe](https://oldschool.runescape.wiki/w/Recipe?oldid=15184603) | 15184603, 2026-04-22 | Bookcase source, chocolate-cake hint, Read/Destroy, and replacement |
| [Present](https://oldschool.runescape.wiki/w/Present?oldid=15185467) | 15185467, 2026-04-22 | Actual completion item and atomic open result |
| [Antique lamp](https://oldschool.runescape.wiki/w/Antique_lamp_%28A_Tail_of_Two_Cats%29?oldid=15190501) | 15190501, 2026-04-22 | Two native 2,500-XP lamps and skill eligibility |
| [Mouse toy](https://oldschool.runescape.wiki/w/Mouse_toy?oldid=15231454) | 15231454, 2026-06-10 | Fun-weapon behavior, cat pounce, and pre-/post-Dragon-Slayer-II replacement paths |
| [Kudos](https://oldschool.runescape.wiki/w/Kudos?oldid=15105676) | 15105676, 2026-01-15 | Five separately claimable Varrock Museum kudos from Historian Minas |

The transcript itself is marked incomplete for some Hild, enchanted-amulet,
inventory-full, hat-claim, and minor dialogue cases. Those gaps require a
current-client trace or another pinned first-party/cache source; they are not
permission to invent dialogue or silently skip capacity handling.

Transition aid only: the local Quest Helper checkout's
[`ATailOfTwoCats.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/atailoftwocats/ATailOfTwoCats.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (2026-07-25) confirms
the native primary values, world coordinates, alternate Unferth/Reldo NPCs,
and cache names. It guides transitions and tests but does not override the
Wiki or cache.

`tools/questhelper_extract.py atailoftwocats --check` currently resolves all
39 referenced gamevals, but incorrectly reports `quest_icthlarinslittlehelper`
as this helper's dbrow. The extractor sees the prerequisite's `Quest.*` token
and lacks an `atailoftwocats` dbrow hint. Fix that before treating the command
as Gate D evidence; the authoritative quest row is `quest_tailoftwocats`.

## 2. Native quest identity and player contract

The cache-native `quest_tailoftwocats` dbrow and pinned Wiki define this
contract:

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 91 |
| Type | Members' quest; Dragonkin series #2 |
| Difficulty / length | Intermediate / medium |
| Release date | 26 September 2005 |
| Start | Talk to the base multi-NPC `twocats_unferth` in north-east Burthorpe |
| Prerequisite | Icthlarin's Little Helper complete, transitively requiring Gertrude's Cat |
| Required levels / combat | None; no enemies must be defeated |
| Required follower | A kitten or cat, following or carried where allowed |
| Required start item | Regular Catspeak amulet, equipped for the conversation |
| Other mandatory items | 5 death runes, chocolate cake, logs, tinderbox, bucket of milk, shears, 4 potato seeds, rake, seed dibber, vial of water, valid white top/bottom disguise |
| Primary state | `%twocats_quest`, cache varbit 1028 on `twocats_varbit` |
| End state | `%twocats_quest = 70` |
| Quest points | 2 |
| Completion item | One `twocats_present`, containing two `twocats_rewardlamp` and one `twocats_mouse_toy` |
| Lamp reward | 2,500 XP per lamp in an eligible skill above level 30 |
| Hat reward | One chosen doctor's or nurse hat during the medical stage; lost hats can be exchanged/replaced through the Apothecary |
| External reward | 5 kudos, claimed separately from Historian Minas |
| Music | `Strange Place` during the Robert/Dragonkin memory and `Bob's on Holiday` during Bob and Neite's travels |
| Required for | Dragon Slayer II and While Guthix Sleeps |

The native dbrow supplies the prerequisite row, quest-point count, start NPC,
and end state. Content must enforce them; merely displaying native metadata in
the quest list is not a start gate.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_atailoftwocats/configs/atailoftwocats.constant` | Intended symbolic constants | Empty except stale history; every milestone is a literal number |
| `server/scripts/quests/quest_atailoftwocats/configs/atailoftwocats.varp` | Allocates `[twocats]` | This extra permanent varp is unused; native state actually lives on cache `twocats_varbit` |
| `server/scripts/quests/quest_atailoftwocats/configs/twocats.varp` | Documents native varbits | Contains comments only; the actual shared carrier reservation is elsewhere |
| `server/scripts/quests/quest_atailoftwocats/scripts/twocats.rs2` | Entire quest scaffold, shared NPC dispatch, chores, and completion | Start is impossible legitimately, Bob is bypassed, several handlers are inert, growth/cutscenes/items are missing, and rewards diverge |

The script header says “Apr 2016 release” and mentions TzTok-Jad/TzKal-Zad,
although this quest released in 2005 and introduces Robert/Dragonkin. It also
says Icthlarin's Little Helper is deferred even though that quest now completes
at state 26. These are stale copy/provenance markers, not requirements.

### Mandatory shared and cross-directory files

| Path | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/general/configs/loc_transform_carriers.varp` | Correctly reserves cache `twocats_varbit` as permanent/transmitted | Retain; remove the unrelated unused `[twocats]` allocation after proving no save depends on it |
| `server/scripts/areas/world/configs/m45_55.spawn` | Base Unferth, Bob, Hild, and two Neite-era carriers | Cache-native world anchors exist; no 30,000-tile Bob roaming/locator service or quest dialogue ownership exists |
| `server/scripts/areas/world/configs/m51_43.spawn` | Sophanem Sphinx | Correct world anchor; shared dialogue arbitration is in Dragon Slayer II |
| `server/scripts/areas/area_burthorpe/scripts/burthorpe_thin_npcs.rs2` | Normal Hild/Burthorpe citizen dialogue | A Tail owns Hild's trigger and permanently replaces her normal dialogue with “leave me be” after state 5 |
| `server/scripts/areas/varrock/scripts/apothecary.rs2` | Canonical shared Apothecary trigger | Correct ownership location and A Tail branch, but quest body has no choice, capacity stop, replacement, or exchange contract |
| `server/scripts/quests/quest_dragonslayer2/scripts/dragonslayer2.rs2` | Canonical Bob and Sphinx triggers; downstream quest | Bob's default is always “Meow” for A Tail states; Sphinx delegates only 35–40; no Dragon Slayer II prerequisite check on state 70 was found |
| `server/scripts/quests/quest_ratcatchers/scripts/ratcatchers_shared.rs2` | Shared cat-speech predicate and Gertrude eligibility | Reusable worn-amulet logic exists; Ratcatchers can steal Gertrude's turn while A Tail is at state 20 |
| `server/scripts/quests/quest_giantdwarf/scripts/gdwarf_axe.rs2` | Reldo's other quest branch | Giant Dwarf state 17 takes priority and can steal A Tail's state-25 Reldo turn |
| `server/scripts/quests/quest_icthlarin/` | Prerequisite, regular amulet source, Sphinx fallback | Complete implementation exists; enforce its state 26 and reuse its cat/amulet rules |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dbrow journal dispatcher | No `quest_tailoftwocats` arm or quest journal proc exists |
| `server/scripts/quests/scripts/quest_cheat.rs2` | `::complete` adapter | Sets only primary 70, leaving all native side state, rewards, and integrations unproved |
| `server/scripts/quests/scripts/{questpoints,questscroll}.rs2` | Shared points and modern completion scroll | Correct shared path; current call is delayed until after an unsafe state/reward sequence |
| `server/scripts/interface_music/scripts/music.rs2` | Reads native music unlock metadata | Neither A Tail track is unlocked by quest content |
| `server/scripts/areas/varrock/` | Varrock Museum systems | Natural-history kudos exist, but no Historian Minas trigger or A Tail timeline-kudos claim was found |
| `server/scripts/quests/quest_whileguthixsleeps/` | Downstream prerequisite | Explicitly soft-skips A Tail in `wgs_prereqs_met`; stale comments claim it cannot progress past 25 |
| `server/scripts/skill_combat/configs/bas/{attack_anims_modern,attack_sounds}.obj` | Mouse-toy combat presentation | Generic wield/attack data exists; pet pounce/play behavior does not |

Shared characters need one canonical trigger each, but “first active quest
wins” is insufficient when two quests can be active. Use exact-stage priority
only where chronology makes overlap impossible; otherwise present a transcript-
appropriate subject choice and dispatch deliberately.

### Cache-native content already available

The osrs239 cache contains substantially more of the intended quest than the
scripts use:

- `%twocats_quest` plus independent house, fire, food, hair, garden, locator,
  reward, and Reldo varbits on cache-owned carriers;
- a base Unferth multi covering bald, short, medium, spiky, long, and the
  reverse haircut states;
- three weed stages, weeded soil, five potato-growth stages, bare/empty/milk/
  cake/meal tables, made/unmade bed, and empty/logged/lit fireplace transforms;
- the bookcase, post-Dragon-Slayer-II chest, regular/enhanced amulets, Chores,
  Recipe, Present, native lamp, both hats, and Mouse toy;
- `bob_locator_amulet` (group 48), with named whisker, eye, mouth, nose, and
  close components plus cache nose/eye animations;
- young/cutscene Bob, Sphinx, Neite, Robert, Odysseus, Dragonkin, King Black
  Dragon, spoof adventurer, carpet-cats, trawler-cats, and other cutscene NPCs;
- cat travel/love/sit, human wood/fire, nose-direction, and arrow animations;
- `twocats_shear` and `twocats_fry_noob` synths; and
- native music rows for `Strange Place` and `Bob's on Holiday`, including
  unlock bits.

The locator is an IF1-authored cache group (`if3=no`), but its named group and
components can be mounted and armed through the current top-level/subinterface
system. Do not replace it with a hand-painted modern imitation, and do not
reintroduce a numeric legacy opener.

## 4. Native state model and current reachability

Quest Helper and the native end state give this primary transition model:

| State | Required phase | Current writer / defect |
| ---: | --- | --- |
| 0 | Not started; Unferth acceptance | Checks for the *enchanted* amulet in inventory, which cannot legitimately exist yet; no prerequisite, cat, equipped-amulet, or Yes/No gate |
| 5 | Speak to Hild first time | Acceptance writes 5; Unferth and enchanted-amulet handlers can incorrectly skip straight toward 20 |
| 10 | Return to Hild with 5 death runes and regular amulet | Hild removes runes and writes 10 immediately, but never exchanges the regular amulet or advances to 15 |
| 15 | Locate and speak to Bob first time | Never written; local locator substitute jumps from 5/10 to 20 without Bob |
| 20 | Ask Gertrude about Bob's parents | Gertrude writes 25, but Ratcatchers can intercept the same turn |
| 25 / 28 | Ask Reldo about Robert the Strong / re-talk | Local Reldo writes 30 immediately; 28 is never written and Giant Dwarf can intercept |
| 30 | Locate and speak to Bob again | Opening the item writes 35 without finding/talking to Bob |
| 35 | Bring Bob's problem to the Sphinx | Sphinx writes 40 without requirements, summary/full cutscene, Chores, hair/house setup, music, or teleport choice |
| 40 | Complete all five chores and wait for potatoes | Multiple required triggers cannot fire and no growth timer can reach garden state 8 |
| 45 | Report completed chores to Unferth | Unferth can advance based on garden 8 alone; other four chores are not checked |
| 50 | Consult the Apothecary | Always offers a doctor's hat only and advances even if inventory-full delivery fails |
| 55 | Cure Unferth in a valid disguise with vial of water | Dialogue unconditionally writes 60; no equipment, weapon/shield, vial, or consumption check |
| 60 | Locate Bob, watch the travel cutscene | Opening the item writes 65 without Bob, Neite, cutscene, music, interruption handling, or Burthorpe return |
| 65 | Return to Unferth and receive the present | Sets 70 before a queued, capacity-sensitive substitute reward path |
| 70 | Complete | Native end state; post-quest replacements, kudos, music, and downstream gates remain incoherent |

The legitimate route stops at state 0. Icthlarin's Little Helper awards the
regular `ics_little_amulet_of_catspeak`; A Tail's start instead requires
`twocats_amuletofcatspeak`, the enchanted item Hild is supposed to create only
after the quest starts. It checks `inv`, not `worn`, even though the real start
requires the regular amulet equipped.

If an administrator gives the enchanted item, later progression still does
not prove the quest. The item itself advances all three Bob-search checkpoints,
while the only `[opnpc1,death_growncat_black_vis]` body belongs to Dragon
Slayer II and falls through to “Meow.” Bob therefore performs none of A Tail's
three mandatory dialogues or the final travel cutscene.

The old queue's `done` label and prior clean compilation are contradicted by
the live tree. Compilation proves names/types, not a state transition. The
queue also says the script never advances beyond 25, which is stale: later
literal writers exist, but are bypasses or unreachable scaffolds rather than a
playable route.

## 5. Item, follower, and locator contracts

### Cat and Catspeak amulets

The quest requires a kitten/cat and uses its dialogue throughout. The pet may
be following or carried for the quest's supported interactions. Current code
never checks a cat at start or any later milestone and contains no player-cat
chore/help dialogue.

Amulet checks are consistently in the wrong container or for the wrong item:

- start requires the future enchanted item in `inv`, not the regular amulet in
  `worn`;
- Hild does not require/delete the regular amulet or add the enchanted one;
- Gertrude/Reldo/chore actions often require the enchanted item in `inv`, while
  the transcript requires it worn for cat speech; and
- no Sphinx/Hild replacement path or one-enchanted-amulet uniqueness check
  exists.

Reuse a single shared “can understand cats” predicate that accounts for the
regular/enhanced amulet in `worn` and the later Dragon Slayer II ability where
appropriate. Quest step requirements remain stricter where the enchanted
locator itself is mandatory.

### Locator UI and Bob ownership

Opening the enchanted amulet or invoking its worn `Locate` verb must show the
cache `bob_locator_amulet` panel. The player turns the nose through eight
directions; the eyes/mouth animate and a sound plays when the selected sector
contains Bob, with all-direction feedback when sufficiently close.

Current code opens no interface, computes no bearing, finds no Bob NPC, arms no
whisker/close events, and sets `%twocats_locator_direction = 1` once. It then
advances the quest merely for opening the object.

The target design must:

1. give the world one authoritative Bob with a controlled large wander range
   and excluded/restricted regions;
2. resolve Bob's current coordinate, not a hard-coded common location;
3. compute the player-to-Bob eight-way sector and near-distance state;
4. mount `bob_locator_amulet` by name on the modern main modal;
5. initialize its legacy CS1 scratch input deliberately, arm both whiskers and
   close on every mount, and update the cache animations/models;
6. support inventory `Open` and worn-neck `Locate` without duplicate trigger
   names; and
7. close cleanly on close, logout, region change, item loss, death, or a newer
   modal, without advancing state.

If a reusable long-range world-NPC locator or named IF1 model-animation setter
is genuinely absent after repository-wide proof, add the smallest general
engine/VM capability. Bob policy and quest state remain content.

### Quest texts and replacements

`twocats_chores` and `twocats_recipe` have no Read/Destroy handlers. The Sphinx
never gives Chores, the player's cat cannot replace it, the bookcase cannot
yield Recipe, and neither item has capacity-safe delivery. The Chores text
must cross out each independent task from live side state; Recipe remains an
optional chocolate-cake hint.

## 6. Current versus required playable route

### Stage 1 — Unferth, Hild, and the enchanted amulet

Required behavior:

- enforce Icthlarin's Little Helper complete before offering the quest;
- distinguish no cat, cat stored, cat present, and missing/not-worn regular
  amulet paths;
- offer a modern Yes/No acceptance and preserve state 0 on refusal;
- route 5 and 10 through full Unferth/Hild re-talks;
- atomically exchange regular amulet plus five death runes for the unique
  enchanted amulet; and
- explain rather than auto-complete the locator.

Current behavior starts with one paraphrased player line and no choice, checks
the unobtainable future item, removes only runes, and never creates the locator.
After state 5, Hild's normal Burthorpe dialogue is replaced forever for this
player, including after state 70.

### Stage 2 — Bob, Gertrude, Reldo, and Bob again

Required behavior:

- locating Bob never changes the primary state; speaking to the live Bob with
  the cat and Catspeak ability does;
- Bob explains Neite, his missing lineage, and Gertrude;
- Gertrude and Reldo expose the exact quest subjects without swallowing their
  Ratcatchers/Giant Dwarf dialogue;
- Reldo uses both 25/28 checkpoints and the native `%twocats_reldo` transform;
- the second Bob conversation establishes the Sphinx objective; and
- every re-talk and missing-cat/amulet branch remains available.

Current behavior bypasses both Bob conversations. Gertrude and Reldo each use
single hard-priority shared branches that can make A Tail unavailable when the
other quest is simultaneously active. Dialogue is reduced to one or two lines.

### Stage 3 — Sphinx and Robert's memory

Required behavior:

- require the cat and ability to understand it;
- summon Bob and offer the full approximately five-minute scene or a summary;
- show hypnosis, Robert and Odysseus versus the Dragonkin, Neite's arrival,
  and Bob asking the player to care for Unferth;
- unlock/play `Strange Place` at the authoritative memory segment;
- give/reclaim Chores without losing progression on a full inventory;
- set the house/table/hair starting transforms for the chore phase; and
- offer the Burthorpe teleport only on the full-cutscene route, matching the
  pinned walkthrough distinction.

Current Sphinx dialogue is a single player sentence followed by state 40. None
of the cache-native NPCs, cameras, animations, audio, item delivery, summary,
restart checkpoints, or teleport choice is used.

### Stage 4 — five independent chores

The cache-side state is well designed; the handlers are not.

| Chore | Native/cache behavior | Current defect |
| --- | --- | --- |
| Tend garden | Use rake on patch through three weed states; use dibber and 4 seeds; progress potato stages 4–8 over 15–35 minutes across relog | Rake is registered as `oploc2` although patch has no op2; planting checks for **three rakes**, ignores `last_useitem`/dibber, and no timer writes 5–8 |
| Tidy house | Click cache op1 `Make` on unmade bed | Registered as `oploc2`; handler cannot be reached from the cache menu |
| Warm human | Use valid logs, then tinderbox; run wood/fire animation and lit transform | Use-on branches exist, but an artificial prior-chore order is imposed and presentation is skipped |
| Feed human | Cake and milk in either order, using native milk-only/cake-only/meal table states | Handler ignores `last_useitem`; any used item can consume cake/milk, milk-first writes complete state 4, and no Inspect or empty-bucket reconciliation exists |
| Tidy human | Use shears on every current Unferth multi variant; repeat Crafting rolls/animation until bald | Registered as nonexistent NPC op2 on long-hair only, while real action is item-on-NPC; hair is never made long and the code instantly writes 8 |

All five chores may be performed independently. Current handlers enforce
garden → bed → fire → food → hair. Worse, `twocats_wait_for_potatoes` checks
only garden state 8 before writing 45; it can accept four unfinished chores.

The permanent garden state alone cannot encode a relog-safe 15–35 minute
deadline. Reuse the persisted farming/time service if it can own this special
patch; otherwise add a general persisted-deadline content primitive and resume
growth on login. A player must not be forced to remain logged in, and logout
must neither instantly finish nor restart the crop.

Implement the bookcase Recipe, table Inspect text, dynamic Chores Read text,
cat help menus, exact item-consumption behavior, object/NPC animations, shear
sound, and “all chores complete” notification. The Unferth Wiki reports eight
Crafting success rolls while the cache exposes a 0–8 hair multi; trace the
exact roll-to-visual cadence rather than guessing from either in isolation.

### Stage 5 — Unferth's illness and the Apothecary

Required behavior:

- report only after every chore is complete, including grown potatoes;
- route to the Apothecary through the full hypochondria conversation;
- choose one doctor's/nurse hat with capacity-safe delivery;
- replace or switch a lost/destroyed hat through the Apothecary;
- accept the authoritative white top/bottom alternatives, one selected hat,
  no weapon/shield, and one vial of water;
- reject invalid equipment without consuming the vial or advancing; and
- consume the vial on the successful “miracle cure” and write 60 once.

Current Apothecary gives only a doctor's hat, silently advances after a failed
full-inventory add, and has no replacement. Unferth ignores every disguise and
item condition, consumes nothing, and immediately writes 60.

### Stage 6 — Bob and Neite's travels

Required behavior:

- locate and speak to Bob; the amulet itself does not advance;
- show the carpet, King Black Dragon/spoof adventurer, and trawler/ship scenes;
- unlock/play `Bob's on Holiday`;
- prevent Bob/Neite clicks from corrupting the scene and restart cleanly after
  interruption, logout, disconnect, or modal replacement; and
- return the player to Unferth's house with state 65 only after the scene.

Current code changes 60 to 65 on item Open, so the entire scene and both cats
are absent despite the cache providing their models and animations.

### Stage 7 — completion and post-quest behavior

Required behavior:

- Unferth reveals one package under the bed and gives `twocats_present`;
- completion, 2 quest points, present delivery, and a durable claim state are
  atomic/idempotent across full inventory, disconnect, and repeated clicks;
- opening the present atomically replaces it with two `twocats_rewardlamp` and
  one Mouse toy, requiring the correct net capacity;
- each native lamp grants exactly 2,500 XP once to an eligible chosen skill;
- the player retains one chosen hat, with Apothecary exchange behavior;
- Bob reclaims the Mouse toy before Dragon Slayer II and the Unferth-house
  chest does so afterward; cat pounce/play behavior works while wielded;
- Historian Minas awards the five kudos once when separately claimed;
- Dragon Slayer II and While Guthix Sleeps both enforce state 70; and
- post-quest Bob, Neite, Unferth, Hild, Sphinx, and item dialogue is coherent.

Current completion writes 70 before an unprotected zero-delay queue. A logout
or lost queue can leave the account complete without points or rewards. The
queue gives two unusable shared `thosf_reward_lamp` objects directly, attempts
both hats across the earlier and completion stages, gives the Mouse toy
directly, never gives/opens the native Present, and skips each item silently
when inventory space is insufficient. Neither `twocats_present` nor
`twocats_rewardlamp` has a server handler, and the shared lamp has no Rub
handler anywhere in `server/scripts`.

`%twocats_gotreward` exists but is unused. Determine its native semantics from
a trace and use it—or an explicit durable claim record—to distinguish “quest
complete,” “present delivered/opened,” and “Mouse toy replacement” without
duplication.

## 7. Gap and oversight register

| Priority | Area | Current defect | Required correction |
| --- | --- | --- | --- |
| P0 | Legitimate start | State 0 requires the future enchanted amulet in inventory, so a normal prerequisite-complete player cannot start. | Enforce native prerequisite/cat/regular-worn-amulet checks and modern Yes/No acceptance; test every refusal/missing requirement branch. |
| P0 | Hild exchange | Runes disappear, but the regular amulet is neither required nor transformed and state 15 is never written. | Atomically exchange regular amulet + 5 runes for one enchanted amulet, with capacity/loss/retry/uniqueness handling. |
| P0 | Bob authority | Shared Bob trigger has no A Tail branch; opening the amulet advances three checkpoints. | Add exact-stage A Tail dispatch to canonical Bob ownership; locator only locates, Bob dialogue alone advances 15/30/60. |
| P0 | Chore reachability | Rake/bed/shear use wrong trigger kinds, Unferth lacks handlers for hair variants, and potatoes never grow. | Register cache-correct use-on/op1 handlers for all transforms and implement persisted 4–8 growth lifecycle. |
| P0 | Chore completion | Garden 8 alone advances 40→45; other tasks can be skipped. | Require all five independent terminal values in one idempotent completion predicate. |
| P0 | Completion atomicity | State 70 precedes a lossy queue and capacity-sensitive adds. | Commit state, points, and a durable Present claim through one retry-safe completion transaction. |
| P0 | Reward identity | Native Present/lamp are bypassed; substitute shared lamps are unusable and wrong-context hats/toy are direct-added. | Implement Present Open and native lamp redemption; give only the selected hat and obtain the toy from the Present. |
| P1 | Locator UI | Cache panel is never opened; direction is fixed to 1 and no buttons/events/close paths exist. | Build the named modern mount around authoritative Bob bearing/near feedback and arm every operation per mount. |
| P1 | Bob world lifecycle | Only a world anchor was found; no large-range roaming/restriction/locator ownership is implemented. | Add a reusable authoritative wandering-NPC service with one Bob per world, safe reload, and restricted-region rules. |
| P1 | Prerequisite/follower | Icthlarin completion and kitten/cat are never checked. | Reuse native quest state and shared pet/follower predicates at every dialogue/cutscene boundary. |
| P1 | Worn/inventory semantics | Expected worn amulets fail many `inv_total(inv, ...)` checks; carried items can improperly enable cat dialogue. | Centralize exact worn/carried/follower predicates and test all supported item locations. |
| P1 | Shared NPC arbitration | Ratcatchers/Giant Dwarf/Dragon Slayer II can steal Gertrude, Reldo, Bob, or Sphinx interactions. | Canonicalize per-NPC dispatch with exact stages and explicit subject choices for simultaneous valid quests. |
| P1 | Native states | 15 and 28 are absent; several transitions happen on the wrong action. | Name all 0/5/10/15/20/25/28/30/35/40/45/50/55/60/65/70 constants and advance only on authoritative actions. |
| P1 | Narrative | Dialogue is heavily paraphrased and both defining cutscenes are absent. | Implement all reachable pinned transcript branches plus restart-safe full/summary cutscene controllers. |
| P1 | Chore independence | Handlers impose an invented strict order. | Let all five native side states progress independently while retaining exact per-action prerequisites. |
| P1 | Garden | One rake click jumps all weeds; planting requires 3 rakes, ignores dibber/use item, and lacks relog-safe time. | Model weed stages, exact seed/dibber consumption, randomized authoritative duration, staged transforms, relog resume, and notification. |
| P1 | Food table | Handler ignores used item and cannot represent milk-first correctly. | Branch on `last_useitem`, preserve milk/cake-only states, handle empty bucket exactly, and expose Inspect text. |
| P1 | Haircut | Long-hair setup, use-on-NPC, Crafting rolls, repeated animation, sound, and cancel/retry are absent. | Reconcile native 0–8 cadence and implement all variants with authoritative success/cancel behavior. |
| P1 | Quest texts | Chores/Recipe cannot be obtained, read, destroyed, or replaced. | Implement dynamic lists, bookcase/cat sources, capacity behavior, and state-aware destroy/reclaim paths. |
| P1 | Medical disguise | Hat choice/reclaim and every clothing/weapon/shield/vial check are absent. | Implement exact equipment-set predicate, failure dialogue, vial consumption, choice, replacement, and exchange. |
| P1 | Music | Neither native track unlocks. | Unlock each row at its authoritative cutscene and prove replay/relog/idempotence. |
| P1 | Kudos | No Historian Minas/timeline claim exists. | Add reusable per-quest museum-history claim state and award exactly 5 kudos once. |
| P1 | Downstream gates | WGS explicitly skips A Tail; DS2 has no state-70 prerequisite enforcement. | Enforce native completion in both start predicates and update stale comments/tests. |
| P1 | Replacements | Enchanted amulet, hat, Mouse toy, Present/lamp, Chores, and Recipe loss rules are missing. | Implement every pinned pre-/postquest source and non-reclaimable warning without dupes. |
| P1 | Journal | No journal arm exists. | Add native dbrow dispatch and exact state/side-state objectives, including growth time and missing quest items. |
| P1 | Cheat adapter | Only primary 70 is set. | Establish the authoritative completed side/world/integration state and prove the second call is a no-op. |
| P1 | Quest Helper verifier | The check validates the prerequisite dbrow instead of this quest. | Add the canonical hint and assert `quest_tailoftwocats`, end state 70, and current source commit. |
| P2 | Presentation | Cameras, exact animation timing, cat overheads, item boxes, sound cadence, and close timing need a live trace. | Reconcile cache assets after critical route/state work and document only genuinely cosmetic deviations. |
| P2 | Stale records | Source/queue contain wrong release/lore, obsolete prerequisite/cache claims, and contradictory reachability notes. | Remove or correct every stale marker when implementation evidence supersedes it. |

## 8. Modern-engine assessment

Parts to retain:

- native `%twocats_quest` and the eight cache side varbits on
  `twocats_varbit`;
- native dbrow identity, prerequisite, start multi-NPC, 2 QP, and end state 70;
- cache-driven patch/table/bed/fireplace/Unferth transforms;
- symbolic NPC/loc/object/interface/music/animation/sound names;
- canonical shared NPC trigger ownership rather than duplicate headers;
- shared quest journal/completion scroll/point services; and
- native Present, lamps, hats, enchanted amulet, Chores, Recipe, and Mouse toy.

Parts that are old, unsafe, or placeholder machinery:

- an unused extra permanent `[twocats]` varp and an empty comment-only varp
  file beside the real cache carrier;
- literal primary state numbers with no named contract;
- item Open scripts that stand in for world NPC interaction;
- hard-priority shared-NPC routing without simultaneous-quest arbitration;
- cache operations wired to the wrong trigger type;
- a missing real-time lifecycle replaced by a harness comment;
- direct reward-item injection instead of native container/redemption flows;
- completion state written before an unprotected queue; and
- soft messages/direct writes replacing the locator and two cutscenes.

The target architecture should be:

```text
native durable quest + independent chore/reward state
                         |
       +-----------------+------------------+
       |                 |                  |
shared character    Bob world service   item/reclaim service
dispatcher          + locator bearing   + atomic exchanges
       |                 |                  |
       +--------> quest state controller <--+
                         |
              restart-safe cutscene controller
                         |
            persisted special-patch growth timer
                         |
             atomic completion + integrations
```

Do not add quest-specific C routing. If the current VM lacks a general
persisted-deadline service, long-range NPC lookup, or named IF1 model update,
prove that repository-wide and add the smallest reusable capability with
general tests. All A Tail dialogue, route, Bob eligibility, chores, cutscenes,
items, and rewards remain RuneScript/config content.

## 9. Implementation sequence

### ATTC-1 — formalize native state and provenance

- add named constants for every primary milestone and documented side value;
- enforce the native Icthlarin requirement and remove the impossible start
  condition;
- add the dbrow journal arm and side-state-aware journal;
- fix the Quest Helper dbrow hint and pin/check its commit;
- reconcile/remove the unused `[twocats]` allocation and stale comments; and
- capture live values for Hild 5/10/15, Reldo 25/28, hair 0–8, locator scratch
  vars, reward bit, and all cutscene checkpoints.

Exit evidence: static state table, corrected extractor output, journal for all
states, and save-migration proof for any carrier removal.

### ATTC-2 — canonicalize cats and shared characters

- implement shared carried/following-cat and Catspeak predicates;
- add A Tail branches to canonical Bob/Sphinx ownership;
- make Gertrude/Reldo/Apothecary/Hild arbitration safe for overlapping quests;
- support every Unferth transform in Talk-to and item-on-NPC dispatch; and
- build one authoritative Bob world-wander/coordinate service.

Exit evidence: pairwise overlapping-quest dispatch tests, no duplicate trigger
headers, cat/worn-item matrix, one Bob across reload, and no swallowed default
dialogue.

### ATTC-3 — implement start, Hild, and locator

- implement exact acceptance/refusal/re-talk requirements;
- atomically enchant/re-enchant the amulet and enforce uniqueness;
- mount/initialize/arm/update/close `bob_locator_amulet` by name;
- support inventory Open and worn Locate; and
- ensure only live Bob dialogue advances 15.

Exit evidence: real UI packets/screenshots for all eight directions, near state,
both entry verbs, every close path, loss/replacement, and full inventory.

### ATTC-4 — implement lineage research and Sphinx memory

- implement first Bob, Gertrude, Reldo 25/28, and second Bob transcript routes;
- implement Sphinx requirements and full/summary choices;
- build the Robert/Odysseus/Dragonkin/Neite cutscene with `Strange Place`;
- deliver Chores durably and initialize chore-world transforms; and
- support the full-scene-only Burthorpe teleport choice and interruption retry.

Exit evidence: transitions 15→40 through real NPCs, both cutscene choices,
music unlock, input ownership, logout/reconnect, and capacity tests.

### ATTC-5 — implement all chores and persisted growth

- use correct cache ops/use-on directions for every loc/NPC variant;
- implement independent bed/fire/food/hair/garden state machines;
- connect patch stages to a 15–35 minute persisted deadline and relog resume;
- implement exact item consumption, animations, sound, Inspect, Chores, Recipe,
  cat help, and completion notification; and
- gate 45 on all five terminal conditions.

Exit evidence: every legal chore order, all partial journals/list states, timer
boundary/relog/server-restart tests, repeated actions, wrong item, cancellation,
and full inventory for Recipe/Chores.

### ATTC-6 — implement medical and travel chapters

- implement full Unferth/Apothecary dialogue and hat choice/exchange/reclaim;
- implement exact disguise plus no-weapon/no-shield/vial predicate;
- consume the vial and advance exactly once;
- implement the Bob/Neite world-tour scene and `Bob's on Holiday`; and
- return to Burthorpe/state 65 only after a successful or deliberately
  supported summary endpoint.

Exit evidence: equipment cross-product, failed/successful vial tests, both hat
choices, loss/full inventory, cutscene click/logout/restart, music, and final
position/state.

### ATTC-7 — make rewards and integrations exact

- make state 70, points, and Present claim atomic/idempotent;
- implement Present Open and native lamp selection/redemption;
- implement Mouse toy cat play and both reclaim eras;
- add Historian Minas's one-time five-kudos claim;
- enforce A Tail completion in Dragon Slayer II and WGS; and
- make post-quest NPC/item/world state and `::complete` coherent.

Exit evidence: inventory capacities 0–28, disconnect/repeat delivery, XP skill
eligibility, points, kudos, downstream start gates, pre/post-DS2 reclaim, and
double-cheat tests.

### ATTC-8 — verify and remove scaffolding

- remove item-Open state shortcuts, harness-only assumptions, literal states,
  stale queue claims, and unused carriers/files proven safe to delete;
- run script/cache builds and quest-specific automated suites;
- run a real-client start-to-present/postquest smoke; and
- record exact commands, packets/screenshots, trace values, and any cosmetic
  deviations in this audit before changing status.

## 10. Verification matrix

| Scenario | Required assertions |
| --- | --- |
| Prerequisite/start | Icthlarin <26 cannot accept; missing/stored cat and missing/not-worn regular amulet use correct branches; No remains 0; Yes writes 5 |
| Hild | States 5/10 re-talk correctly; exactly 5 death runes + regular amulet become one enchanted amulet; all capacity/loss/retry/duplicate cases are atomic |
| Bob world | Exactly one Bob starts at the authoritative anchor, roams within allowed world rules, survives reload coherently, and remains locatable by all players |
| Locator entry | Inventory Open and worn Locate both mount the named panel; neither changes quest state |
| Locator feedback | Eight sectors, axis/diagonal boundaries, plane/region differences, and near-all-directions state animate/sound correctly against live Bob |
| Locator lifecycle | Whiskers and close are armed after every mount; logout, death, item loss, region/modal change, and repeated open close safely |
| First Bob | Requires supported cat + Catspeak state; full dialogue alone writes 20; repeated talk gives correct reminder |
| Shared NPCs | Gertrude/Ratcatchers, Reldo/Giant Dwarf, Sphinx/DS2/Icthlarin, and Apothecary quest overlaps route every valid subject without starvation |
| Research | Gertrude writes 25; Reldo preserves 25/28 and writes 30; second Bob alone writes 35; missing/worn amulet branches match transcript |
| Sphinx requirements | Missing cat/understanding cannot advance; full and summary choices are deliberate and restart-safe |
| Memory cutscene | All actors/cameras/combat beats/Neite/Chores appear; Strange Place unlocks once; full route alone offers Burthorpe teleport |
| Chores delivery | 0–28 used slots and lost/destroyed Chores cannot lose or duplicate the item/state; Read crosses out exact live tasks |
| Garden rake | Correct use-on-rake only; all weed visuals/actions/cancels; rake retained; wrong items do nothing authoritative |
| Garden plant | Dibber + exactly 4 seeds; correct consumption and stage 4; repeat/wrong item/insufficient seeds safe |
| Garden time | Authoritative 15–35 minute deadline, stages 5–8, offline time, relog/reconnect/server restart, no watering/compost requirement, one final notification |
| Bed | Cache Make op1 works only in chore phase, animates once, and repeats safely |
| Fireplace | Accepted logs then tinderbox in order; wrong items/repeats safe; animations/sound/transforms exact |
| Table | Cake-first and milk-first both preserve partial transforms; wrong use cannot consume either; exact bucket result and Inspect text |
| Hair | Every Unferth variant accepts shears use; exact Crafting rolls/visual cadence/cancel/retry; final value 8 and shear sound once per intended beat |
| Chore independence | All legal permutations work; partial states survive relog; state 45 requires bed1/fire2/food4/hair8/garden8 together |
| Recipe/cat hints | Bookcase capacity/duplicate/reclaim and Read/Destroy work; cat hint branches reflect every partial chore state |
| Apothecary | Both hat choices, no-space stop, loss/reclaim, destroy/switch, and unrelated potion/quest subjects remain available |
| Cure | Valid white outfits and alternatives accepted; invalid hat/top/bottom or equipped weapon/shield rejected; vial consumed only on success; writes 60 once |
| Travel cutscene | Bob dialogue starts scene; all three travel beats and actors run; Bob's on Holiday unlocks; click/logout/disconnect restart is coherent; finish writes 65 and returns Burthorpe |
| Completion capacity | Full/partial inventory, repeated click, disconnect before/after commit, and relog always yield exactly state70, 2 QP, and one claimable Present |
| Present | Open requires exact net capacity, removes one present, gives two native lamps + one toy atomically, and cannot duplicate/reclaim after deliberate destruction |
| Lamps | Each offers eligible skills, rejects boundary-ineligible skills, grants exactly 2,500 XP once, and handles cancel/destroy/relog |
| Mouse toy | Generic combat stats/animation/sound plus supported cat pounce work; Bob reclaims before DS2 and chest after DS2 only when absent |
| Kudos | Historian Minas offers A Tail only at state70, awards exactly 5 once, updates totals/interface, and survives relog/double click |
| Music | Strange Place and Bob's on Holiday unlock at exact cutscene stages once and remain unlocked after relog/cheat policy |
| Downstream | DS2 and WGS reject state69 and accept state70, while all their other prerequisites remain enforced |
| Journal | Every primary state, Reldo 28, each chore combination/growth wait, missing item, and complete state display the correct objective |
| Cheat adapter | First `::complete quest_tailoftwocats` establishes 70, 2 QP, completed world/integration state; second is a no-op and grants no reward items |

Minimum repository checks after implementation:

```sh
python3 tools/questhelper_extract.py atailoftwocats --check
make -C src torirsserver-scripts
ToriRSServer_Pack --check-only
```

Also run quest-specific state/shared-NPC/locator/timer/chore/cutscene/item/reward/
music/kudos/downstream suites and capture real-client packets/screenshots for
the locator, both cutscenes, completion scroll, Present, and lamp. Compilation
alone cannot prove Bob's world coordinate, a 35-minute offline timer, shared
dialogue arbitration, or atomic reward delivery.

## 11. Definition of done

A Tail of Two Cats may be marked `verified-modern` only when:

- a legitimate Icthlarin-complete player with a supported cat and regular worn
  amulet can take the real Unferth route from 0 to 70 without cheats, literal
  state shortcuts, or harness-only mutation;
- every native primary value and side transform is named, correctly written,
  journaled, durable, and advanced only by its authoritative action;
- Bob is one coherent world NPC whose live position drives a fully mounted,
  animated, closable, modern-armed cache locator, and the amulet never stands
  in for speaking to him;
- Gertrude, Reldo, Hild, Apothecary, Sphinx, Bob, and all Unferth variants
  preserve every overlapping quest/default dialogue path;
- both cutscenes, their summary/restart/teleport rules, actors, music, camera,
  input ownership, and interruption recovery match pinned evidence;
- all five chores are independent and exact, the patch grows over authoritative
  persisted time, and no wrong trigger/item/order can block or bypass state 45;
- the medical disguise, hat choice/exchange, vial, equipment exclusions, and
  every quest-item loss/full-inventory/replacement path are exact;
- completion yields exactly 2 QP and one native Present claim atomically, the
  Present/native lamps/Mouse toy function, and disconnect/repeat/capacity cases
  cannot lose or duplicate value;
- five kudos, both music tracks, Mouse toy reclaim eras, Dragon Slayer II, and
  While Guthix Sleeps integrate durably and idempotently;
- the Quest Helper check names `quest_tailoftwocats`, stale queue/source claims
  are corrected, unused allocations are reconciled, and no active critical
  soft-skip or old-engine shortcut remains; and
- script compilation, cache validation, automated state/UI/timer/cutscene/item/
  integration coverage, real-client captures, and idempotent cheat evidence
  are recorded in this file.
