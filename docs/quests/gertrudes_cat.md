# Gertrude's Cat modernization audit

Status: `audit-pending` — the native quest row, canonical seven-state primary,
Gertrude multinpc, both children, broken fence, Fluffs, six mewing crates,
quest items, journal dispatch, completion adapter, and modern reward API are
present. The route is not reliably completable: one of the six randomized
kitten coordinates does not correspond to any crate. The rescue also deletes
a shared map NPC, completion commits before three unchecked item grants and a
cancellable reward queue, and the advertised kitten reward has no follower,
care, growth, rat-catching, colour-selection, or medal implementation. Shared
Gertrude routing can additionally hide A Tail of Two Cats and kitten services.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to Gertrude, Shilop and Wilough, the
Lumber Yard entrance, Fluffs, the randomized crate search, rescue and
settlement, the kitten shop and cat-training medal, the shared cat lifecycle,
Icthlarin's Little Helper, Ratcatchers, A Tail of Two Cats, Recipe for Disaster,
the Varrock Diary, West Ardougne cat sales, journal/admin adapters, migration,
and recovery. It is an implementation specification, not completion evidence.

## 1. Authoritative references

The current OSRS Wiki revisions below are pinned so implementation and review
use stable route, dialogue, item, reward, follower, recovery, and downstream
contracts.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Gertrude's Cat](https://oldschool.runescape.wiki/w/Gertrude%27s_Cat?oldid=15286277) | 15286277, 2026-08-03 | Identity, required items, route, randomized crates, rewards, colour selection, and downstream requirements |
| [Gertrude's Cat/Quick guide](https://oldschool.runescape.wiki/w/Gertrude%27s_Cat/Quick_guide?oldid=15283871) | 15283871, 2026-07-31 | Ordered interactions, either-child rule, item order, crate behavior, and completion |
| [Transcript:Gertrude's Cat](https://oldschool.runescape.wiki/w/Transcript%3AGertrude%27s_Cat?oldid=15263272) | 15263272, 2026-07-14 | Start/refusal, re-talks, Fluffs ops, hand-in, kitten-care explanation, and settlement order; page is explicitly incomplete pending dialogue reverification |
| [Crate (Gertrude's Cat)](https://oldschool.runescape.wiki/w/Crate_%28Gertrude%27s_Cat%29?oldid=14826744) | 14826744, 2024-12-23 | The six valid mewing-crate coordinates and per-player random crate contract |
| [Fluffs' kitten](https://oldschool.runescape.wiki/w/Fluffs%27_kitten?oldid=15187382) | 15187382, 2026-04-22 | Quest-item source, use, Drop behavior, and re-search recovery |
| [Gertrude's cat](https://oldschool.runescape.wiki/w/Gertrude%27s_cat_%28NPC%29?oldid=15196222) | 15196222, 2026-04-25 | Fluffs' ops, three-damage scratch, post-quest presence, feeding, and follower conversation |
| [Gertrude](https://oldschool.runescape.wiki/w/Gertrude?oldid=15238069) | 15238069, 2026-06-23 | Kitten eligibility, 100-coin price, quick-buy op, ring colour choice, diary task, and cat-training medal |
| [Kitten](https://oldschool.runescape.wiki/w/Kitten?oldid=15217864) | 15217864, 2026-05-27 | Item/follower conversion, one-kitten rule, care deadlines, three-hour growth, rat catching, hell-kitten conversion, and loss |
| [Cat](https://oldschool.runescape.wiki/w/Cat?oldid=15213220) | 15213220, 2026-05-19 | Adult/overgrown lifecycle, replacement eligibility, rat catching, West Ardougne sale, death, and downstream uses |
| [Cat training medal](https://oldschool.runescape.wiki/w/Cat_training_medal?oldid=15185846) | 15185846, 2026-04-22 | 100-rat counter, eligible cat forms, reset rules, award, and replacement |
| [Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper?oldid=15292330) | 15292330, 2026-08-10 | Direct quest prerequisite and cat/follower consumer |
| [Recipe for Disaster/Freeing Evil Dave](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Evil_Dave?oldid=15294908) | 15294908, 2026-08-12 | Direct prerequisite and hell-rat/cat consumer |
| [Varrock Diary](https://oldschool.runescape.wiki/w/Varrock_Diary?oldid=15293707) | 15293707, 2026-08-12 | Medium task completed by choosing a kitten colour |

The sources define a members, novice, very-short quest released 28 July 2003.
It has no skill, quest, or combat requirement. Required items are 100 coins, a
bucket of milk, and a seasoned sardine, which may be made from a raw sardine
and doogle leaves. Rewards are one quest point, 1,525 Cooking XP, one kitten,
a chocolate cake, a stew, and the ability to buy later kittens from Gertrude
for 100 coins. Wearing an activated Ring of charos allows the first or later
kitten colour to be selected. The quest is directly required for Icthlarin's
Little Helper, Freeing Evil Dave, and the Medium Varrock Diary task.

The first kitten is not merely an inventory collectible. Dropping its item is
the canonical way to place it as a follower. While following, it needs food
and attention, grows into an adult cat after 120 ninety-second growth events
(three hours), can catch rats, and may run away if neglected. The wider unlock
therefore belongs to this quest's completion contract even though much of its
code should live in a shared pet/cat subsystem.

Transition aid only: Quest Helper's
[`GertrudesCat.java`](https://github.com/Zoinkwiz/quest-helper/blob/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/gertrudescat/GertrudesCat.java)
at commit `5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d` (the file itself last
changed in commit `58c6e5ef8f0b2dc06378897ad2eb7068a14d7e58` on 2025-10-25)
confirms primary states 0 through 5, both children, the upstairs/downstairs
route, required items, six relevant entity families, all three item rewards,
1,525 Cooking XP, one quest point, and the kitten-raising unlock. `python3
tools/questhelper_extract.py gertrudescat --check` resolves every referenced
dbrow, item, NPC, loc, coordinate, and state. It cannot detect the authored
sixth-coordinate typo, NPC ownership, item-grant failure, queue cancellation,
or missing pet engine, so a passing extract is not route verification.

## 2. Native quest identity and contract

| Field | Native value / expected behavior |
| --- | --- |
| Cache row | `quest_gertrudescat`; dbrow pack index 60, quest metadata ID 50 |
| Type / difficulty / length | Members quest / novice / very short |
| Release / location | 28 July 2003 / Varrock |
| Start | `gertrude` multinpc shell; native coordinate 3150,3410,0; world spawn 3151,3410,0 |
| Primary state | `%fluffs`, permanent/transmitted native varp 180 |
| Canonical values | 0 not started; 1 accepted; 2 paid child; 3 gave milk; 4 gave sardine/searching; 5 returned kitten; 6 complete |
| Crate selection | `%fluffs_crate`, server-authored permanent coordinate because the cache exposes no native carrier |
| Fence support | `%lumberyard_fence_used`, server-authored temporary player variable |
| End / quest points | State 6 / 1 QP |
| XP | 15,250 raw Cooking units = 1,525 XP |
| Items | Random-colour kitten, chocolate cake, stew |
| Unlocks | Cat follower/care/growth ecosystem; later kittens for 100 coins; activated-ring colour choice; 100-rat medal; downstream quest/diary gates |

The primary is already the correct cache state and agrees exactly with Quest
Helper. It must not be replaced or renumbered. `%fluffs_crate` is justified
secondary state, but storing an unchecked world coordinate makes invalid and
legacy values indistinguishable from a valid roll. Modernization should store
or derive a validated six-way index, migrate the five correct coordinates,
repair/reroll the incorrect sixth value, and initialize state-4 saves whose
selection is null or outside the approved set.

The cache already supplies far more of the cat system than the scripts use:

- six colour variants each of `kittenpet_*`, `growncat_*`, and
  `overgrowncat_*`, plus hell variants, are pet NPCs with Pick-up, Talk-to,
  Chase, and Interact ops;
- matching item families exist in categories `kitten`, `cat`, and
  `overgrown`;
- the cat NPCs expose combat stats and cache pet metadata;
- Fluffs, the lost-kitten actor, all six mewing crates, the cat-training medal,
  kitten walk/idle/death assets, and kitten/cat sound assets exist; and
- `follower_id`, `pet_item_id`, and `cat_next_id` parameter types were copied
  into `quest_fluffs.param`, but no config assigns them and no script reads
  them.

This is an implementation gap, not missing cache art. The modern solution may
need general follower capability, but quest-specific state should remain in
RuneScript/config and use these symbolic assets.

## 3. Implementation surface

The direct root contains 411 lines across five files. The route's real
ownership surface includes Gertrude and both children in Varrock, general
crates, the map spawn, shared completion/journal code, a large cat ecosystem,
and four downstream quests.

| Quest-owned path | Present responsibility | Audit result |
| --- | --- | --- |
| `configs/quest_fluffs.constant` | Primary aliases and QP constant | All seven values match the native row and Quest Helper |
| `configs/quest_fluffs.varp` | Primary, crate coordinate, and fence marker | Primary is correct; crate needs validation/migration; the alleged fence mutex is player-local and cannot serialize multiple players |
| `configs/quest_fluffs.param` | Follower/item/next-cat relation schemas | Orphan declarations: no assignments or consumers exist |
| `scripts/quest_fluffs.rs2` | Children, fence, sardine, Fluffs, crates, kitten Drop, XP/completion queue | Route-shaped, but contains the one-in-six deadlock, shared-NPC deletion, missing op, incomplete scene, unsafe settlement, and explicit pet deferrals |
| `scripts/fluffs_journal.rs2` | Dynamic journal for states 0–6 | Uses the modern API but loses item, crate, and recovery detail |

Mandatory external owners:

| Path / subsystem | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `areas/varrock/scripts/gertrude.rs2` | Start, all re-talks, completion, kitten shop, shared quest routing | Core dialogue exists; settlement and shop are unsafe, colour choice/medal/quick-buy are absent, ownership predicate is wrong, and first-match routing hides valid topics |
| `areas/varrock/scripts/shilop.rs2`, `wilough.rs2` | Child Talk-to wrappers | Correctly delegate both current valid children to one quest proc |
| `general_use/scripts/crates.rs2` | Ordinary crate fallback | Correctly receives `gertrudeempty_crate` outside state 4; quest flavor remains quest-owned |
| `areas/world/configs/m51_54.spawn` | Six mewing crates and Fluffs | All six canonical crate NPCs and one static public Fluffs are present |
| `configs/all.dbrow`, `all.npc`, `all.loc`, `all.obj` | Metadata and symbolic asset schema | Native quest row and entity families are present; the direct scripts use symbolic names |
| `quests/scripts/questpoints.rs2` | QP/count/scroll/jingle settlement | Correct modern API, but it is not intrinsically idempotent and the caller reaches it through an unguarded queue |
| quest journal / quest cheat | Dynamic dispatch / admin completion | Both use the correct row; cheat is state-only and idempotent, as an admin adapter should be |
| shared pet/follower ownership | Item-to-NPC lifecycle, login/logout, call/pick-up, one-follower rule | No cat integration exists; the separate summoning scripts are not wired to these cats |
| `quest_icthlarin` | Direct prerequisite, Wanderer and Sphinx cat use | Uses inventory ownership as a follower substitute and never explicitly checks `%fluffs`; its debug commands intentionally manufacture the prerequisite and kitten |
| `quest_ratcatchers` | Transitive prerequisite and intensive cat consumer | Counts an inventory kitten/cat, turns rat clicks directly into progress, and never exercises follower movement, catch chance, or the medal counter |
| `quest_atailoftwocats` | Transitive prerequisite and shared Gertrude topic | Start omits its cache prerequisite and required cat, while Gertrude topic priority can make its state-20 interaction unreachable |
| `quest_recipefordisaster/scripts/recipefordisaster_evildave.rs2` | Direct prerequisite and hell-rat/cat route | Checks only RFD intro, ignoring the cache's Gertrude/Shadow requirements, and soft-skips the entire cat/spice puzzle while incorrectly claiming cat items do not exist in the cache |
| `interface_diaries` | Medium Varrock colour-selection task | Counter framework exists, but no Gertrude colour-choice hook or task completion bit is implemented |
| `areas/area_ardougne_west/scripts/civilian.rs2` | Adult/overgrown cat sale | Inventory-category sale grants 100 death runes safely; follower ownership and the easy-diary 200-rune reward are absent |

There is no legacy `if_openmain`/`if_openoverlay`, raw numeric entity ID, or
old IF1 choice handler in the direct root. The obsolete machinery is
behavioral: a public map NPC is mutated as if it were a private scene actor,
player temp state is called a shared lock, follower mechanics are represented
by inventory possession, completion is split across an interruptible queue,
and service/topic composition relies on first-match returns.

## 4. State and transition audit

| State | Canonical phase | Current behavior / defect |
| ---: | --- | --- |
| 0 | Not started; Gertrude offers Yes/No | Local dialogue adds an older recursive “What's in it for me?” branch, but refusal remains at 0 and acceptance writes 1 |
| 1 | Find and question either Shilop or Wilough | Either child works and 100 coins are validated before removal; successful payment writes 2 |
| 2 | Enter the Lumber Yard and give Fluffs milk | Fence and ladder route exist; milk becomes an empty bucket and writes 3; Talk-to Fluffs is missing despite the cache op |
| 3 | Make/give the seasoned sardine | Both item-use directions make the sardine; valid hand-in consumes it and writes 4, then chooses one of six coordinates |
| 4 | Search six mewing crates, recover the kitten, return it to Fluffs | Five rolls are completable; the sixth points at no crate. Search reports success before proving the item fit. Drop recovery exists. Returning the kitten deletes static Fluffs globally and skips the scene. |
| 5 | Return to Gertrude | Full dialogue exists, then state 6 is committed before an unchecked kitten, cake, stew, XP, QP, count, and scroll sequence |
| 6 | Complete; cat ecosystem and Gertrude services available | Primary end state is correct, but the follower/growth ecosystem, colour choice, diary task, medal, quick-buy op, and correct replacement rules are absent |

### Randomized crate proof

The first five authored selections exactly match five map-spawned mewing
crates. The sixth does not.

| Roll | Stored coordinate | Spawned mewing crate | Result |
| ---: | --- | --- | --- |
| 0 | 3305,3500,0 | 3305,3500,0 | valid |
| 1 | 3310,3499,0 | 3310,3499,0 | valid |
| 2 | 3307,3507,0 | 3307,3507,0 | valid |
| 3 | 3303,3506,0 | 3303,3506,0 | valid |
| 4 | 3298,3514,0 | 3298,3514,0 | valid |
| 5 | **3311,3511,0** | **3315,3515,0** | no NPC can satisfy `npc_coord = %fluffs_crate`; permanent route deadlock |

The Wiki crate page independently lists the six spawned coordinates. This is
not a cosmetic waypoint discrepancy: state 4 never rerolls, so approximately
one sixth of new players can never receive Fluffs' kitten.

## 5. Detailed lifecycle audit

### Start, children, and the Lumber Yard entrance

The local start has a real refusal branch and only commits state 1 on
acceptance. Current transcript exposes just Yes/No, while local content keeps
an older “What's in it for me?” recursion. Reconcile wording against a live
capture because the pinned transcript flags itself incomplete, but preserve
the important invariant that cancellation and every non-accept branch leave
state 0 unchanged.

Both children are valid in current OSRS, and the shared dynamic-speaker proc
correctly allows either. Payment checks for 100 coins before removal, removes
exactly 100, and writes state 2 after the explanatory dialogue. Repeated
threat/refusal paths do not charge or advance. Retain these semantics while
making the boy-to-boy banter and current transcript exact.

The broken fence uses `p_teleport` followed by `~agility_exactmove`, so the
modern exact-move packet exists. `%lumberyard_fence_used` is nevertheless a
temporary **player** variable. Its message claims the fence is occupied by
“someone else,” but it cannot observe another player and therefore provides
no multi-user exclusion. It also teleports the player onto the start square
before checking the marker. Replace it with the established obstacle helper's
real busy/approach semantics, or make a genuine shared obstacle lock with
well-defined cleanup. Test clicks from both sides, membership denial, repeated
packets, two simultaneous players, movement interruption, and logout.

### Seasoned sardine and Fluffs interactions

Using doogle leaves and a raw sardine in either direction removes two inputs
and adds one seasoned sardine. This is capacity-safe and appropriately remains
available outside the active quest because the result is a tradeable item.
Revalidate both inputs at the commit point so duplicate/stale use packets
cannot remove only one side.

Fluffs' Pick-up and Stroke ops face the player, animate, hiss, attack, deal the
canonical three damage, and then give a state-appropriate clue. The NPC config
also exposes Talk-to as op 4, but no `[opnpc4,gertrudescat]` exists; the
transcript requires a “Miaoww” response. After state 5 the current guard rejects
all interaction with “better leave her alone.” Current OSRS instead keeps
Fluffs in the Lumber Yard after completion, accepts defined milk/fish uses, and
can converse with the player's following cat. Restore the missing op and split
quest clues from post-quest behavior rather than treating completion as a
blanket rejection.

Milk hand-in is a one-for-one bucket transform at exact state 2. Sardine
hand-in is exact-state 3 and initializes the crate selection. Both should
validate item removal success before state mutation and use one protected,
atomic transaction. Invalid food needs the current food matrix rather than a
small hard-coded historical list; quest-stage Fluffs must still reject food
other than the seasoned sardine.

### Crate search, full inventory, loss, and relog

Six `kittens_mew` NPCs are world-spawned and periodically say “Mew!”. This
matches current behavior: visible mews may move while the per-player winning
crate stays fixed. The generic loc crates and barrel provide nearby flavor,
and the ordinary crate script correctly owns those locs outside state 4.

Repair the sixth coordinate and validate every state-4 selection on login and
before search. An imported/legacy save may have zero, the bad coordinate, or an
otherwise corrupt value; it must be mapped or rerolled once without changing
the primary. A correct-crate search should reserve one slot, add the item, and
only then display “You find a kitten.” Current code displays success first and
ignores `inv_add`; a full backpack receives no kitten but sees a false success.
The state does remain 4, so a retry is possible, but silent recovery is not an
acceptable transaction contract.

The anti-duplicate check covers inventory and bank. Drop is a special quest
action: it removes the item, briefly spawns the lost-kitten actor, and tells the
player it ran back into the crates. Because state remains 4 and ownership is
absent, the winning crate can supply another. Preserve that behavior with an
owned/private temporary actor and defined cleanup on logout/region change.
Test banking, full inventory, repeated searches, duplicate packets, Drop at
every tile/plane, logout during the temporary scene, and return after relog.

### Returning the kitten and Fluffs ownership

Using the kitten at state 4 consumes it, plays Fluffs' purr, writes state 5,
calls `npc_del`, and prints a message. It omits the kitten purr and both actors'
run-home scene acknowledged in the source header.

More seriously, `gertrudescat` is a static NPC from `m51_54.spawn`, not an
owned `npc_add` actor or player-local multinpc. `npc_del` is unconditional and
removes the active world NPC. One player's hand-in can therefore remove Fluffs
for every nearby player, including players at states 2–4, and no local script
respawns her. The Wiki also says Fluffs remains present after completion, so a
permanent public deletion is wrong even for the triggering player.

Use a player-scoped scene/visibility mechanism: preserve the public quest NPC,
animate private/owned scene actors if necessary, commit state 5 after the
kitten transaction, clean them up on every exit, and let `%fluffs` determine
what each player sees/interacts with. A public deletion is not a shortcut for
player-local narrative state.

### Completion settlement and rewards

At state 5 Gertrude performs the main conversation, closes dialogue, writes
state 6, randomly grants a kitten, delays, adds cake and stew without checking
either result, delays again, and queues XP/QP/scroll. There are four distinct
failure windows:

1. Handing the quest kitten to Fluffs frees only one inventory slot. The reward
   kitten can fill it; the cake and stew then fail silently.
2. `~gertrude_give_cat` ignores `inv_add`. If the player has no slot at that
   moment, the first/free kitten is lost after state 6 has already committed.
3. Logout, modal cancellation, or queue replacement after state 6 but before
   `fluffs_complete` can permanently skip 1,525 Cooking XP, the quest point,
   completed count, and reward scroll.
4. The queue has no settlement guard. If delivered twice by an engine or
   recovery bug, `~quest_complete_rewards` itself adds the point/count again.

The reward is also always random. Current OSRS lets a player wearing an
activated Ring of charos choose the first kitten colour, and that selection is
the Medium Varrock Diary task. The completion scroll uses `coins` as its model
despite coins not being a reward; use the verified kitten reward model.

Build one resumable settlement transaction. Establish the live full-inventory
behavior where the transcript is silent, reserve or deliberately deliver all
three item rewards, choose/randomize and persist one colour exactly once, grant
XP/QP/count exactly once, and commit state 6 only at the defined settlement
boundary. A dedicated settlement fact is warranted because the native primary
has no spare post-dialogue state and the shared completion API is additive.
Re-talk at interrupted settlement must resume missing work without replaying
completed grants.

### Cat follower, care, growth, and rat lifecycle

The source explicitly defers `pet.rs2`, and current reward items have no cat
Drop handler. A normal item drop therefore cannot fulfill the Wiki contract of
turning the kitten into an owned follower. No cat script implements Pick-up,
Talk-to, Chase, Interact, feed, stroke, ball of wool, call, shoo-away, hunger,
attention, growth, combat, death, membership loss, hell conversion, or
rat-catching even though the relevant item/NPC/assets are in the cache.

Create a shared cat subsystem with a single authoritative ownership model:

- persist colour, life stage, growth events, hunger/attention state, eligible
  rat catches, and item/follower/menagerie location;
- atomically convert each cat item to its matching owned NPC on Drop and back
  on Pick-up, respecting the global one-follower rule and inventory capacity;
- restore/call/clean up the owned NPC across login, logout, teleport, region
  change, death, instance transition, membership change, and disconnect;
- advance kitten growth only while it follows, every 90 seconds, pausing in the
  same conditions as current OSRS and reaching adult at 120 events;
- implement 24-minute hunger and 25-minute attention warning chains, feed and
  interaction reset behavior, and eventual run-away without leaking an NPC;
- map all six colours through kitten, adult, and overgrown forms and preserve
  original colour through hell-kitten/hellcat transformations;
- implement cat-type catch chances and combat ownership for rats, Ratcatchers,
  hell-rats, rat pits, and relevant bosses; and
- increment/reset the eligible 100-rat medal counter at the canonical events,
  announce 10/50/100, and let Gertrude award or replace `felinemedal`.

This shared subsystem is a prerequisite to marking Gertrude's Cat modern. An
inventory-only stand-in makes the quest's headline reward and multiple
downstream quests behaviorally false.

### Gertrude's post-quest service and shared topics

`~fluffs_has_pet_cat` totals the six ordinary kitten, adult, and overgrown item
families across inventory, bank, and worn containers. That predicate has
opposite errors:

- it treats an overgrown cat as a reason to refuse a new kitten, while current
  OSRS specifically permits buying a new kitten once the current cat becomes
  overgrown; and
- it omits every hell variant and all followers, so a player with a hell-kitten
  or following ordinary kitten may buy an illegal second kitten.

It also cannot represent normal cats stored in a POH menagerie, which current
OSRS deliberately excludes from the blocking ownership check. Replace the
enumeration with the shared cat ownership API and encode the actual rule:
block while a kitten or non-overgrown adult cat is owned outside an eligible
menagerie case; allow a replacement after loss, overgrowth, or valid storage.

The purchase removes 100 coins before proving a slot exists. With a full
backpack and more than 100 coins, removing part of the coin stack frees no
slot, the kitten add fails, and the money is lost. It also ignores an activated
ring, always randomizes colour, and never completes the diary task. Reserve
capacity, choose/persist colour, grant the kitten, and remove coins in one
atomic transaction.

The native post-quest NPC exposes op 3 “Kitten,” added as a quick-buy service,
but no `[opnpc3,gertrude_post]` or shell-equivalent handler exists. Implement
it through the same purchase transaction; do not maintain two service rules.

Talk-to is currently a priority chain: eligible/active/completed Ratcatchers
wins first, then A Tail of Two Cats states 20–28, then ordinary post-quest
dialogue/shop. Once Ratcatchers is eligible it steals every Talk-to until and
after completion. A player at the overlapping A Tail Gertrude step cannot
reach that quest, and ordinary kitten/medal dialogue is hidden as well. Both
the live `gertrude` shell trigger and resolved `gertrude_post` trigger repeat
this policy. Route both types to one topic composer that offers every relevant
quest/service subject, with op 3 remaining a direct kitten shortcut.

## 6. Downstream contract audit

| Consumer | Canonical dependency | Current divergence | Required modernization |
| --- | --- | --- | --- |
| Icthlarin's Little Helper | Completed Gertrude's Cat and a cat at relevant scenes | Wanderer checks only inventory cat ownership, not `%fluffs`; Sphinx consumes only inventory forms; dev debug commands set `%fluffs=6` and add a kitten by design | Enforce the cache prerequisite in live start routing and use the shared follower owner; retain clearly scoped debug preparation without treating it as route evidence |
| Ratcatchers | Completed Icthlarin and The Giant Dwarf started; non-overgrown following cat and Catspeak amulet | Inventory kitten/cat stands in for follower; clicking a rat increments progress directly; no catch chance, movement, combat, or medal integration | Consume the shared cat API and its chase/catch events; keep overgrown exclusion and formal prerequisite chain |
| A Tail of Two Cats | Completed Icthlarin, cat, catspeak equipment, and Gertrude topic | Local start checks only an upgraded amulet, not the cache prerequisite or cat; Gertrude can be monopolized by Ratcatchers | Enforce requirements and compose shared NPC topics |
| Freeing Evil Dave | RFD intro, Shadow of the Storm, and Gertrude's Cat; cat catches hell-rats for spice | Runtime checks only intro and replaces the full cat/spice experiment with one deterministic stew transform | Reinstate cache gates and build the hell-rat/spice path on shared cat catch events |
| Medium Varrock Diary | Select a kitten colour using activated Ring of charos | No colour menu or diary hook exists | Complete the exact task once on a successful chosen-colour grant, for reward or later purchase as live behavior dictates |
| West Ardougne civilians | Sell adult/overgrown cat for 100 death runes, 200 after easy diary | Inventory categories work; follower and diary multiplier do not | Pick up/transfer the owned follower atomically or accept its item; use diary reward state for quantity |

The Icthlarin files contain several `%fluffs = ^fluffs_complete` writes, but all
are inside named `debugproc` setup commands. They are appropriate test setup,
not live prerequisite enforcement and not evidence of runtime state
corruption. The real defect is the absence of an explicit completion check in
the Wanderer start path.

## 7. Journal, completion adapter, and migration

The journal is modern dbrow-rendered text and covers every primary value. It
needs more exact state guidance:

- state 2 should name milk, seasoned sardine, and the Lumber Yard rather than
  saying only “find the lost cat and return it”;
- state 3 should explain doogle leaves/raw sardine and the hungry cat;
- state 4 should direct the player downstairs to the six mewing crates and
  explain dropped-kitten recovery, not say only “get her to follow me home”;
- state 5 should explicitly direct return to Gertrude; and
- partial item ownership, full inventory, and relog recovery should never leave
  a completed-looking line while the required item is absent.

The `quest_cheat.rs2` arm correctly treats state 6 as already complete and
otherwise writes only state 6. It must stay a state-only, idempotent admin
adapter: it should not fabricate XP, reward items, cat age, or medal progress.
Quest route tests must use the real actors and cannot cite `::complete` as
gameplay evidence.

Migration needs an explicit version and settlement policy:

1. Preserve `%fluffs` values 0–6 without translation.
2. At state 4, map the five correct coordinate values, replace the bad
   `(3311,3511)` value with `(3315,3515)`, and initialize any other value once.
3. Preserve all existing kitten/cat/overgrown items and map their colour/form
   into the shared cat owner when first placed as a follower.
4. Do not infer quest completion from cat possession; `%fluffs` remains the
   authority.
5. Treat already-state-6 legacy saves as settled to avoid duplicating QP/XP,
   unless a one-time migration can prove the old reward ledger. The old code
   records no fact that distinguishes a fully settled save from one interrupted
   between state 6 and its queue, so blind compensation is unsafe.
6. Add a settlement fact for all future state-5 completions and make recovery
   idempotent before enabling the new route.

## 8. Modernization work packages

### Package 1 — make the existing route reliable

- Keep native `%fluffs`; validate/migrate crate selection and correct the sixth
  coordinate.
- Restore Talk-to Fluffs and exact state feedback.
- Make item transformations and hand-ins protected transactions.
- Make crate pickup capacity-aware and its success message truthful.
- Replace public Fluffs deletion with a player-scoped rescue scene and complete
  cleanup/recovery.
- Replace the fake player-local fence mutex with defined modern obstacle
  ownership.

### Package 2 — make settlement atomic and resumable

- Add a narrowly scoped settlement fact/version.
- Establish and implement current full-inventory behavior for all three item
  rewards.
- Persist one random/selected kitten colour once.
- Grant kitten, cake, stew, Cooking XP, QP, count, jingle, and scroll exactly
  once; use the kitten scroll model.
- Commit state 6 at the proven settlement boundary and resume safely on re-talk
  after any interruption.

### Package 3 — implement the shared cat engine

- Wire item, NPC, colour, and next-growth mappings with symbolic configs.
- Implement owner spawn/pick-up/call/drop, persistence, care, growth, death,
  catching, combat, hell forms, and medal tracking.
- Reconcile login/logout, teleports, instances, membership, and menagerie
  ownership.
- Expose one API for “owns blocking kitten/cat,” “has usable following cat,”
  transfer/sale, chase result, and eligible catch count.

### Package 4 — rebuild Gertrude services and consumers

- Compose Ratcatchers, A Tail, ordinary service, medal, clue, and future topics
  instead of first-match interception.
- Implement op 3 quick-buy through the same capacity-safe transaction.
- Add activated-ring colour selection and the diary task hook.
- Correct replacement eligibility, including hell forms, followers,
  overgrown/wily/lazy stages, and menagerie storage.
- Migrate Icthlarin, Ratcatchers, A Tail, RFD Evil Dave, and West Ardougne from
  inventory approximations to the shared owner/catch contract.

### Package 5 — narrative, journal, and regression closure

- Reverify the incomplete transcript against a live client, retaining explicit
  accept/refuse and every re-talk.
- Restore the rescue presentation, post-quest Fluffs behavior, kitten-care
  explanation, shop/medal dialogue, and journal details.
- Add route, migration, concurrency, interruption, pet-lifecycle, and
  downstream tests before changing status.

## 9. Verification matrix

Automated transition coverage must include at least:

| Scenario | Required assertion |
| --- | --- |
| Start accept/refuse/re-talk | Only acceptance writes 1; refusal and cancellation remain 0 |
| Either child | Shilop and Wilough independently accept exactly 100 coins and write 2 once |
| Payment failures | 0/99 coins, full inventory, duplicate click, disconnect during dialogue; no partial charge/advance |
| Fence | Both directions, F2P denial, simultaneous players, double-click, interruption, logout; no false “someone else” or stranded coordinate |
| Milk/sardine | Correct order and both sardine-combination directions; invalid foods/items do not consume or advance |
| Crate rolls | Force all six selections and prove exactly the selected spawned NPC grants the kitten |
| Invalid legacy crate | Zero, old bad coordinate, and arbitrary coordinate reconcile once and remain stable |
| Search capacity | Full inventory gives no success message/item; freeing one slot allows exactly one kitten |
| Kitten loss | Drop, bank, relog, region change, and repeated search recover without duplication or leaked actors |
| Two-player rescue | One player reaching state 5 does not remove or alter Fluffs for another at states 2–4 |
| Completion capacity | Test 0–3 free slots and existing cake/stew stacks under the verified live policy |
| Completion interruption | Disconnect after each dialogue/delay/grant boundary; re-talk resumes and final QP/XP/count/items are exactly once |
| Ring colour | No ring randomizes once; activated ring offers all six; cancellation is non-destructive; successful choice completes the diary task once |
| Kitten purchase | 99/100/>100 coins, full inventory, every blocking/eligible cat form, following pet, bank, loss, overgrowth, and menagerie |
| Topic composition | Ratcatchers eligible/active/complete crossed with A Tail state 20–28, kitten purchase, medal, and ordinary dialogue |
| Cat lifecycle | Drop/Pick-up/Call, hunger, attention, feed/stroke/wool, 120 growth events, logout, death, membership loss, shoo-away, and hell conversion |
| Rat lifecycle | Kitten/adult/overgrown chances, Ratcatchers modifiers, hell-rats, 10/50/100 announcements, reset rules, medal award/replacement |
| Downstream gates | Icthlarin, A Tail, and RFD reject unmet Gertrude state even if a cat item is injected; valid completed/follower cases work |
| Admin completion | First `::complete quest_gertrudescat` writes 6; second is a no-op; neither grants route rewards |

Gate D commands and evidence:

1. `python3 tools/questhelper_extract.py gertrudescat --check`.
2. Quest-specific static audit: state writers/readers, trigger uniqueness,
   six-coordinate/spawn equality, symbolic resolution, completion/journal/cheat
   registration, and no undisclosed `deferred`/legacy UI marker.
3. `make -C src torirsserver-scripts` and `ToriRSServer_Pack --check-only` against the
   intended cache.
4. Automated route and shared-cat tests covering the matrix above.
5. Two-client headless smoke from Gertrude through every forced crate roll,
   rescue, reward scroll, follower Drop/Pick-up, ring-selected later purchase,
   and one downstream cat interaction.
6. Packet/screenshots for exact move, Fluffs scene isolation, completion scroll,
   follower add/remove, colour menu, quick-buy op, and diary completion.

## 10. Prioritized findings

| Priority | Finding | Player impact |
| --- | --- | --- |
| P0 | Sixth random coordinate is `(3311,3511)` but its crate is `(3315,3515)` | Roughly one sixth of players are permanently blocked at state 4 |
| P0 | Returning the kitten calls `npc_del` on static public Fluffs | One player can remove another player's required NPC; Fluffs also vanishes contrary to post-quest behavior |
| P0 | State 6 precedes unchecked reward grants and an interruptible, unguarded completion queue | Permanent kitten/food/XP/QP loss or duplicate completion awards |
| P0 | No cat follower/care/growth/catch engine exists | The headline reward and required downstream mechanics are nonfunctional |
| P0 | Ratcatchers-first Gertrude routing can hide A Tail state 20–28 and all ordinary services | Valid quest progression and kitten/medal access become unreachable |
| P1 | Replacement predicate blocks overgrown cats but ignores hell cats/followers; purchase charges before capacity proof | Canonical eligibility is inverted in important cases and players can lose 100 coins |
| P1 | Activated-ring colour choice and Medium Varrock Diary hook are absent | Current quest reward choice and required diary task cannot be completed |
| P1 | Icthlarin/A Tail/RFD live paths omit formal cache prerequisites; RFD soft-skips the cat puzzle | Downstream access can be forged and core cat content is bypassed |
| P1 | Fluffs Talk-to and current post-quest interactions are absent | Native menu op is dead and persistent world behavior is incomplete |
| P1 | Crate pickup and reward/shop grants report success without checking `inv_add` | Misleading feedback and silent item loss under full inventory |
| P2 | Fence uses player-local temp state as a supposed shared mutex | Misleading concurrency behavior and unnecessary pre-check teleport |
| P2 | Journal omits exact item, crate, loss, and recovery guidance | Players receive stale guidance at the most failure-prone states |
| P2 | Source comments call key scenes/trails/pets “deferred” and RFD claims cat items do not exist | Known omissions are normalized and future work is directed by false inventory claims |

## 11. Current evidence and acceptance boundary

Completed during this audit:

- traced every `%fluffs` read/write in the live tree;
- decoded the native quest row, state carrier, start actor/coordinate, end
  state, QP, and XP;
- resolved every direct entity/item/loc symbol and all map spawns;
- compared all six randomized coordinates with the six spawned crates and
  pinned Wiki coordinates;
- inspected Gertrude shell/post routing, shop ownership, both child wrappers,
  crate fallback, completion/journal/cheat adapters, and shared reward API;
- inventoried the cache's kitten/cat/overgrown item and NPC families, medal,
  categories, ops, and orphan relation params;
- traced Icthlarin, Ratcatchers, A Tail, RFD Evil Dave, Varrock Diary, and West
  Ardougne consumers; and
- ran the pinned Quest Helper extractor successfully.

Not yet performed: no gameplay implementation was changed, no compile/pack
claim is made, no transition or two-player test exists, and no real-client
smoke/capture has been recorded. `verified-modern` requires every P0/P1 item,
all five work packages, and the full verification matrix to pass. Until then,
the quest remains `audit-pending` even though five of six random route branches
can reach the nominal completion dialogue.
