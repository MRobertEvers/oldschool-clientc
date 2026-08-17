# The Gauntlet and Corrupted Gauntlet — full implementation plan

This is the completion plan for the solo Gauntlet minigame, not just the
Hunllef fight. It covers the Prifddinas portal and lobby, session isolation,
the 7×7 procedural dungeon, every facility and resource interaction, every
temporary item, every crystalline/corrupted NPC, both Hunllef fights, the
reward chest, scorekeeping, collection-log entries and Combat Achievements.

The implementation target is current OSRS behaviour as of **17 August 2026**.
The principal sources are pinned so later Wiki edits cannot silently change the
acceptance target:

- [The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet?oldid=15273190)
  (revision 15273190)
- [The Gauntlet/Strategies](https://oldschool.runescape.wiki/w/The_Gauntlet/Strategies?oldid=15275827)
  (revision 15275827)
- [Crystalline Hunllef](https://oldschool.runescape.wiki/w/Crystalline_Hunllef?oldid=15273480)
  (revision 15273480)
- [Corrupted Hunllef](https://oldschool.runescape.wiki/w/Corrupted_Hunllef?oldid=15301498)
  (revision 15301498)
- [Reward Chest (The Gauntlet)](https://oldschool.runescape.wiki/w/Reward_Chest_%28The_Gauntlet%29?oldid=15274408)
  (revision 15274408)

The Wiki is authoritative for player-visible behaviour, quantities, rates and
timings. The revision-239 cache is authoritative for symbolic names, object and
NPC ids, models, sequences, spot animations, interfaces and client scripts.
Where the Wiki gives only “common”, an image, or descriptive prose, the plan
marks a measurement task instead of inventing a number.

This document supersedes the completion claims in [`docs/GAUNTLET.md`](../GAUNTLET.md).
That file is useful historical context for the first vertical slice and outside
crystal equipment; the baseline audit below records the gaps that this plan was
written to close.

### Implementation status — 17 August 2026

The Wiki-specified code surfaces in slices 1–6, plus the automated contract
work in slice 7, have now been landed in the Gauntlet content package. The
shipped surface includes transactional entry and teardown, a 7×7 connected
instance with discovery UI, gathering and all native recipes, ordinary
monsters and drops, both Hunllef encounters, immutable pending rewards,
records, collection-log hooks, all 21 Combat Achievements, the Gauntlet cape
and the Youngllef follower. The contract manifest and checker live in
`tools/data/gauntlet_contract.json` and `tools/check_gauntlet_contract.py`; run
them with `make -C src check-gauntlet-contract`. The live-client matrix and the
measurements below remain acceptance work, not completed verification.

Sections 2 and 12 remain the original baseline audit and delivery plan rather
than a description of the current tree. The research audit in §13 resolves the
floor masks, room connectivity and normal-mode drop-table question, and narrows
the remaining gaps to server-side generation/combat rolls and timings. Any
value that remains unresolved there is an approximation, not literal parity.

---

## 1. Definition of done

A Gauntlet implementation is complete only when all of these are true:

1. A player who has completed Song of the Elves can enter the lobby, talk to
   Bryn, use the deposit box, scoreboard, books, entrances and reward chest.
2. Entering a run safely stores all carried/worn items, removes temporary stat
   boosts, creates an isolated valid 7×7 dungeon, equips the correct starting
   kit, opens the exact timer/map UI and starts a 10:00 or 7:30 timer.
3. Room discovery, doors, resource guarantees, monster tiers, perimeter
   demi-bosses and corrupted-mode differences match the Wiki, including the
   22 July 2026 generation and crafting changes.
4. Every temporary item in §6 can be obtained, used, crafted, equipped,
   dropped and cleared at the correct time; nothing can enter or escape the
   instance.
5. All 18 ordinary enemy variants have correct stats, styles, ranges,
   animations, death handling and drop tables. Duplicate demi-boss components
   and guaranteed weapon frames work per run, not per NPC type.
6. Crystalline and Corrupted Hunllef reproduce the attack/prayer cycles,
   stomp, prayer-disable projectile, tornadoes, floor patterns, damage tables,
   telegraphs, sounds, death race and escape rules in §§8–9.
7. Success and every failure/exit path restore the exact pre-run state once,
   free the instance once and award the right pending chest state once—even
   across death, logout or reconnect.
8. The reward chest implements all junk, incomplete, regular, corrupted and
   independent tertiary rolls, including elite clues, pet/cape ownership,
   inventory overflow and collection-log hooks.
9. Personal completions, deaths and best times; the five collection-log slots;
   and all 21 current Combat Achievements are updated correctly.
10. The automated and live-client matrix in §14 passes for both modes.

Out of scope: reimplementing the outside-Prifddinas crystal singing economy,
Ilfeen, the Blade of Saeldor or Bow of Faerdhinen. Those systems must accept
the seeds awarded here, but their broader work remains owned by
[`docs/GAUNTLET.md`](../GAUNTLET.md) and the crystal-equipment scripts.

---

## 2. Current tree — reusable foundation and measured gaps

Content currently lives in
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_gauntlet/`.
There are about 2,645 lines across eight scripts, plus constants and varps.
The cache already contains the maps, all temporary item/NPC/loc definitions,
models, sequences, projectiles, map icons and interfaces 637–640.

| Area | Current state | Work required |
|---|---|---|
| Portal/lobby/entry | Basic portal, SotE gate, two entrances and gear holding exist | Bryn, books, scoreboard, deposit box, exact dialogue/quick options, boost reset, follower/container/security checks |
| Instance | 7×7 rooms are copied from the 4×4 library | Generate and persist a valid room graph and content manifest; current sampling-with-replacement has no topology or distribution proof |
| Map/timer UI | Cache interfaces and CS2 exist | Nothing opens them or writes the 49 discovery bits/current-room coordinates; timer updates only once per minute through chat |
| Room population | One deterministic NPC is put at every room centre | Real zero-to-many monster/resource populations, tiers, normal guarantees and 2026 perimeter demi-boss rules |
| Gathering | Five node handlers exist | Per-action loops, 1/3 shard roll of 10–30, XP, animation/timing, inventory handling and fishing-with-vial continuity |
| Crafting | A text-choice vertical slice exists | Native recipe interface, correct XP, quantity/tick rules, burn chance, confirmations and several recipe corrections |
| Ordinary enemies | Cache stats and generic combat animations exist | Correct ranged/magic AI and virtually all drop-table logic |
| Hunllef | A playable approximation exists | Replace damage, stomp, tornado and floor approximations with explicit state machines; add visuals/audio and exact race handling |
| Rewards | Main 24-way tables and independent seed/pet rolls mostly exist | Correct junk/incomplete tables, elite clues, ownership/log hooks, overflow and atomic pending rewards |
| Meta | Two completion counters exist | Deaths, best times, global display policy, collection log, pet metamorphosis, CAs, music/jingle |

Known parity defects that must not be preserved:

- `gauntlet_spawn_room_monsters` cycles deterministically through NPCs and puts
  demi-bosses away from the perimeter.
- Every ordinary kill currently gives only 1–5 shards and none of the published
  paddlefish, herb, teleport-crystal or secondary frame rolls.
- Gathering grants an entire node in one operation, gives no XP and rolls only
  1–3 shards at 1/4 instead of 10–30 at 1/3 per gather action.
- The current singing bowl charges 40 shards for a teleport crystal and 1 for
  an escape crystal; the pinned Wiki recipes are 50 and 200 respectively.
- An egniol potion currently consumes one dust and produces four doses. It must
  consume ten dust, award 10 Herblore XP and produce a three-dose potion.
- Paddlefish never burn; the Wiki specifies a burn chance through Cooking 46,
  15 Cooking XP and one-tick cooking.
- Hunllef’s “on-prayer max hit” is halved a second time, off-prayer damage is
  merely doubled, adjacency is treated as stomp, tornado minima are wrong, and
  floor warning/hazard locs are created together instead of transitioning.
- Floor damage is checked only when a new pattern is emitted, not every tick
  while the player occupies an orange tile.
- The junk table substitutes an Al Kharid flyer and potato even though cache
  objects `flier_prif` (Iwan’s flyer) and `acne_potion` (Potion) exist.
- Twelve of the 27 incomplete-table outcomes are absent and several remaining
  entries are duplicated to fill their slots.
- Elite clues, recipe/book UIs, room map, overlay, Bryn, scoreboard, collection
  log and Combat Achievements have no server wiring.

---

## 3. Implementation shape

Keep minigame policy in RuneScript and add engine work only for primitives that
cannot be expressed safely. Split the current large scripts by ownership:

| File/surface | Responsibility |
|---|---|
| `gauntlet.rs2` | Lobby, entry, run state machine, timing, teardown and reconnect recovery |
| new `gauntlet_layout.rs2` | Seeded room graph, rotation, room manifest, discovery and map varbits |
| `gauntlet_gather.rs2` | Nodes, tool storage, door lighting, monster spawn manifest and ordinary drops |
| `gauntlet_craft.rs2` | Cooking, food, potions, pestle and native singing recipes |
| new `gauntlet_monsters.rs2` | Tier AI, attack styles/projectiles/ranges and death/drop dispatch |
| `gauntlet_hunllef.rs2` | Boss-only state machines and hazards |
| `gauntlet_rewards.rs2` | Immutable pending reward generation and chest delivery |
| new `gauntlet_lobby.rs2` | Bryn, books, scoreboard and deposit-box integration |
| new `gauntlet_progress.rs2` | KC/deaths/PBs, collection log and CA event flags |
| `gauntlet_selftest.rs2` | Deterministic layout, recipes, drops, boss cycles, teardown and reward fixtures |
| `.dbtable`/`.dbrow` or enums | Recipes, monster drops, room archetypes and reward rows—no 100-line switch ladders |

Represent a run explicitly: `LOBBY → PREPARING → BOSS → WON/FAILED →
REWARD_PENDING → LOBBY`. All exit paths call one idempotent finalizer. Store the
run seed and manifest, start tick, mode, room discovery, guarantee counters,
points and CA flags as session state. Store pending rewards and personal records
persistently. Do not let active-NPC globals stand in for encounter ownership.

Engine prerequisites to verify before content work:

- a 14×14-zone instance remains supported and all zone rotations remap loc
  collision and NPC spawn coordinates correctly;
- a player can own one private ground-item/drop stream inside an instance;
- interface 637 can be opened as an overlay and interfaces 638–640 can be
  opened/updated from server content;
- per-tick floor/tornado collision can query a player tile without changing the
  active NPC used by a queued hit;
- `gauntlet_holding` (42 slots) serialises its contents, and an interrupted
  session can recover it before accepting another run.

---

## 4. Lobby, entry and lifecycle interactions

Implement every interactive lobby surface below.

| Surface | Required behaviour | Wiki |
|---|---|---|
| Gauntlet Portal | Enter the shared lobby; require Song of the Elves | [The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet) |
| Bryn (`gauntlet_instructor`, NPC 9020) | Talk-to tutorial covering purpose, preparation, crafting, Hunllef and corrupted mode | [Bryn](https://oldschool.runescape.wiki/w/Bryn) |
| The Gauntlet entrance | Enter normal; expose corrupted option only after one normal completion | [The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet#Corrupted_Gauntlet) |
| Lobby Teleport Platform | Channel back to Prifddinas | [The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet) |
| Scoreboard | Read personal/global completions, deaths and best times for both modes | [Scoreboard](https://oldschool.runescape.wiki/w/Scoreboard_%28The_Gauntlet%29) |
| Crystal Singing Recipes | Read/open interface 640 in both lobby and starting room | [Crystal Singing Recipes](https://oldschool.runescape.wiki/w/Crystal_Singing_Recipes) |
| Egniol Potions | Read the recipe/effect transcript in lobby and starting room | [Egniol Potions](https://oldschool.runescape.wiki/w/Egniol_Potions) |
| Bank Deposit Box | Use the shared bank deposit flow; never expose stored run gear | [Bank deposit box](https://oldschool.runescape.wiki/w/Bank_deposit_box) |
| Reward Chest | Open only when a reward is pending; transform closed/open loc correctly | [Reward Chest](https://oldschool.runescape.wiki/w/Reward_Chest_%28The_Gauntlet%29) |

Entry transaction, in order:

1. Reject without SotE, while already active, while a prior holding container
   needs recovery, or when instance allocation fails.
2. Record mode, seed and start tick. Corrupted requires at least one normal KC.
3. Store inventory and worn equipment without losing slot metadata/charges.
   Audit rune pouch, looting bag, quiver/ammo, bond pouch, carried pet and any
   other accessible container so no item can enter the run indirectly.
4. Remove pre-applied temporary skill boosts/debuffs as the Wiki specifies.
   Preserve effects known to survive entry, notably an already-cast Vengeance;
   add an explicit test rather than using a blanket status reset.
5. Build the instance and manifest atomically. On failure, restore state and do
   not start a timer.
6. Equip the sceptre and give axe, pickaxe, harpoon, pestle and one teleport
   crystal. Open overlay 637, initialise map 638, set the starting-room discovery
   bit and start the exact tick countdown.

Exit matrix:

| Exit | Loot | Records | Restoration |
|---|---|---|---|
| Starting-room platform Exit/Quick-exit | None | no death/KC/PB | immediate |
| Boss barrier Escape | Points-based failure loot | no death/KC/PB | immediate |
| Escape crystal before boss | None | no death/KC/PB | immediate |
| Escape crystal during boss | Points-based failure loot | no death/KC/PB | immediate |
| Player death | Points-based failure loot; dangerous activity rules apply | increment mode death | after death animation in lobby |
| Hunllef death | Complete reward | increment KC and update PB/CAs | lobby after boss death resolves |
| Logout/disconnect | End run safely; no duplicated reward or gear | define as abandonment unless a completed reward was already committed | recover exactly once on login |

The simultaneous player/Hunllef death rule is explicit: if the player dies as
the boss dies, no kill-count credit is awarded. Commit success only after the
death arbitration has established that the player survived.

---

## 5. Dungeon generation, room discovery and UI

### 5.1 Layout contract

Build a **full orthogonal 7×7 grid**, with the boss fixed at (3,3) and the
starting room in one of the four adjacent positions. Every horizontally or
vertically adjacent pair is connected: corners have two neighbours, edge rooms
three and interior rooms four. This is not a maze-generation problem and the
implementation must not randomly delete interior edges. The current Plugin Hub
[Gauntlet Map adjacency table](https://github.com/StickySerum/gauntlet-map/blob/25d36de0cd6a2cd319bd2721885325b81c6756f4/src/main/java/com/gauntletmap/GauntletMapPlugin.java#L478-L555)
enumerates that complete topology; its
[room constants](https://github.com/StickySerum/gauntlet-map/blob/25d36de0cd6a2cd319bd2721885325b81c6756f4/src/main/java/com/gauntletmap/GauntletMapConstants.java#L62-L63)
identify all 24 perimeter rooms and the four guaranteed cardinal rooms.
Choose compatible room archetypes and rotations from normal `m29_88` or
corrupted `m30_88` without changing this connectivity.

Generation constraints from [The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet#Resource_rooms)
and its 22 July 2026 changes:

- start and boss rooms contain no random resources/ordinary monsters;
- every other room contains zero to five resource nodes and/or monsters;
- normal mode’s inner 3×3 guarantees four separate rooms containing at least
  three ore deposits, three linum plants, three phren roots and weak monsters;
- corrupted mode removes that mitigation but guarantees one room with three
  fishing spots;
- demi-bosses spawn only in edge/corner rooms;
- the north-, east-, south- and west-most rooms from the centre each guarantee
  a random demi-boss, and other perimeter rooms may also contain one;
- duplicate demi-boss species are legal and interact with component guarantees
  as described in §7.3.

The four guaranteed room indices in the client map's 1-based, south-west-first
ordering are **4, 22, 28 and 46**; the perimeter candidate set is
`1–8, 14–15, 21–22, 28–29, 35–36, 42–49`. Jagex independently confirms the
four cardinal guarantees in the
[22 July 2026 update](https://secure.runescape.com/m=news/summer-sweep-up-gear--pvm-changes?oldschool=1).

Put room resource/monster probabilities in data. Neither the Wiki, Jagex's
update nor client plugins publish the server's content weights. Capture them
from a sufficiently large live corpus and check the corpus plus derivation into
`docs/` or `tools/data/` before declaring distribution parity.

### 5.2 Nodes, doors and map

Unlit door nodes require the carried or wielded [crystal sceptre](https://oldschool.runescape.wiki/w/Crystal_sceptre)
or corrupted sceptre. Lighting performs the cast animation/graphic/sound,
changes the correct rotated door loc, reveals the adjacent room and starts its
population only once. Walking between already lit rooms does not respawn it.

Wire the existing cache varbits:

- `gauntlet_room_0_found` … `gauntlet_room_48_found`;
- `gauntlet_current_room_x`, `gauntlet_current_room_z`;
- `gauntlet_start`, `gauntlet_corrupted`;
- `player_in_gauntlet`, `gauntlet_boss_started`.

Open interface 637 for the countdown and interface 638 for the map. Reuse cache
clientscripts `gauntlet_overlay_init` (2913), `gauntlet_map_init` (2908) and its
room/start/player update helpers. Update current room and discoveries on room
boundary crossings, not on arbitrary player clicks. The overlay must show every
second from 10:00/7:30 to 0:00 and force-entry exactly at zero; chat messages are
supplementary, not the timer UI.

### 5.3 Starting/boss room scenery

All required actions:

- [Singing Bowl](https://oldschool.runescape.wiki/w/Singing_Bowl_%28The_Gauntlet%29):
  Sing-crystal and keep the recipe interface open while another valid recipe is
  affordable.
- [Range](https://oldschool.runescape.wiki/w/Range_%28The_Gauntlet%29): Cook
  raw paddlefish in one tick.
- [Water Pump](https://oldschool.runescape.wiki/w/Water_Pump_%28The_Gauntlet%29):
  Fill-from one or multiple vials according to the native interaction.
- [Tool Storage](https://oldschool.runescape.wiki/w/Tool_Storage): return each
  missing sceptre, axe, pickaxe, harpoon and pestle, but no free teleport/escape
  crystal.
- [Teleport Platform](https://oldschool.runescape.wiki/w/Teleport_Platform_%28The_Gauntlet%29):
  Exit with confirmation; Quick-exit without it.
- [Barrier](https://oldschool.runescape.wiki/w/Barrier_%28The_Gauntlet%29): Pass
  with the “no turning back” warning; Quick-pass without it; transform to the
  boss-room Escape-only form after entry.
- Warning and damaging floor locs: server-controlled hazard states, never
  ordinary click interactions.

---

## 6. Complete temporary item and recipe inventory

Normal and corrupted items have the same player-facing function and combat
stats; corrupted resources/equipment use their red cache variants. All are
run-bound and are deleted before held gear is restored.

### 6.1 Tools, resources, food and consumables

| Items to implement | Behaviour/reference |
|---|---|
| [Crystal sceptre](https://oldschool.runescape.wiki/w/Crystal_sceptre), [corrupted sceptre](https://oldschool.runescape.wiki/w/Corrupted_sceptre) | Starting weapon; Wield/Drop; lights nodes |
| [Crystal axe](https://oldschool.runescape.wiki/w/Crystal_axe_%28The_Gauntlet%29), [corrupted axe](https://oldschool.runescape.wiki/w/Corrupted_axe) | Chop phren roots |
| [Crystal pickaxe](https://oldschool.runescape.wiki/w/Crystal_pickaxe_%28The_Gauntlet%29), [corrupted pickaxe](https://oldschool.runescape.wiki/w/Corrupted_pickaxe) | Mine deposits |
| [Crystal harpoon](https://oldschool.runescape.wiki/w/Crystal_harpoon_%28The_Gauntlet%29), [corrupted harpoon](https://oldschool.runescape.wiki/w/Corrupted_harpoon) | Fish paddlefish |
| [Pestle and mortar](https://oldschool.runescape.wiki/w/Pestle_and_mortar_%28The_Gauntlet%29) | Shared item; 10 shards → 10 dust; component → 80 shards after confirmation |
| [Crystal shards](https://oldschool.runescape.wiki/w/Crystal_shards_%28The_Gauntlet%29), [corrupted shards](https://oldschool.runescape.wiki/w/Corrupted_shards) | Stackable since 22 Jul 2026; activity shards go directly to inventory |
| [Crystal dust](https://oldschool.runescape.wiki/w/Crystal_dust_%28The_Gauntlet%29), [corrupted dust](https://oldschool.runescape.wiki/w/Corrupted_dust) | Ten units are the egniol secondary ingredient |
| [Weapon frame](https://oldschool.runescape.wiki/w/Weapon_frame), [corrupted weapon frame](https://oldschool.runescape.wiki/w/Weapon_frame_%28corrupted%29) | Tier-1 weapon ingredient |
| [Crystal spike](https://oldschool.runescape.wiki/w/Crystal_spike), [corrupted spike](https://oldschool.runescape.wiki/w/Corrupted_spike) | Perfected halberd component; crush to 80 shards |
| [Crystal orb](https://oldschool.runescape.wiki/w/Crystal_orb), [corrupted orb](https://oldschool.runescape.wiki/w/Corrupted_orb) | Perfected staff component; crush to 80 shards |
| [Crystalline bowstring](https://oldschool.runescape.wiki/w/Crystalline_bowstring), [corrupted bowstring](https://oldschool.runescape.wiki/w/Corrupted_bowstring) | Perfected bow component; crush to 80 shards |
| [Crystal ore](https://oldschool.runescape.wiki/w/Crystal_ore), [corrupted ore](https://oldschool.runescape.wiki/w/Corrupted_ore) | Stackable armour resource |
| [Phren bark](https://oldschool.runescape.wiki/w/Phren_bark), [corrupted phren bark](https://oldschool.runescape.wiki/w/Phren_bark_%28corrupted%29) | Stackable armour resource |
| [Linum tirinum](https://oldschool.runescape.wiki/w/Linum_tirinum), [corrupted linum tirinum](https://oldschool.runescape.wiki/w/Linum_tirinum_%28corrupted%29) | Stackable armour resource |
| [Grym leaf](https://oldschool.runescape.wiki/w/Grym_leaf), [corrupted grym leaf](https://oldschool.runescape.wiki/w/Grym_leaf_%28corrupted%29) | Egniol herb |
| [Raw paddlefish](https://oldschool.runescape.wiki/w/Raw_paddlefish), [burnt paddlefish](https://oldschool.runescape.wiki/w/Burnt_fish_%28paddlefish%29), [paddlefish](https://oldschool.runescape.wiki/w/Paddlefish) | Shared fish; cook/burn; cooked fish heals 20 |
| [Crystal paddlefish](https://oldschool.runescape.wiki/w/Crystal_paddlefish), [corrupted paddlefish](https://oldschool.runescape.wiki/w/Corrupted_paddlefish) | Heals 16 and can be combo-eaten immediately after paddlefish |
| [Vial](https://oldschool.runescape.wiki/w/Vial_%28The_Gauntlet%29), [water-filled vial](https://oldschool.runescape.wiki/w/Water-filled_vial), [grym potion (unf)](https://oldschool.runescape.wiki/w/Grym_potion_%28unf%29) | Empty variant follows mode; filled/unfinished products are shared |
| [Egniol potion (1–4)](https://oldschool.runescape.wiki/w/Egniol_potion) | Creation yields (3); each sip restores floor(Prayer/4)+7, 40% run and stamina effect; optional vial smashing |
| [Teleport crystal](https://oldschool.runescape.wiki/w/Teleport_crystal_%28The_Gauntlet%29), [corrupted teleport crystal](https://oldschool.runescape.wiki/w/Corrupted_teleport_crystal) | Single use to start room; reject in start room and boss room with canonical messages |
| [Escape crystal](https://oldschool.runescape.wiki/w/Escape_crystal_%28The_Gauntlet%29), [corrupted escape crystal](https://oldschool.runescape.wiki/w/Corrupted_escape_crystal) | Single use to lobby; failure loot only once the boss fight has begun |

### 6.2 Weapons and armour

Implement/equip all 36 equipment items, not just their crafting transforms:

| Family | Basic | Attuned | Perfected |
|---|---|---|---|
| Crystal halberd | [basic](https://oldschool.runescape.wiki/w/Crystal_halberd_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Crystal_halberd_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Crystal_halberd_%28perfected%29) |
| Crystal bow | [basic](https://oldschool.runescape.wiki/w/Crystal_bow_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Crystal_bow_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Crystal_bow_%28perfected%29) |
| Crystal staff | [basic](https://oldschool.runescape.wiki/w/Crystal_staff_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Crystal_staff_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Crystal_staff_%28perfected%29) |
| Corrupted halberd | [basic](https://oldschool.runescape.wiki/w/Corrupted_halberd_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Corrupted_halberd_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Corrupted_halberd_%28perfected%29) |
| Corrupted bow | [basic](https://oldschool.runescape.wiki/w/Corrupted_bow_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Corrupted_bow_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Corrupted_bow_%28perfected%29) |
| Corrupted staff | [basic](https://oldschool.runescape.wiki/w/Corrupted_staff_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Corrupted_staff_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Corrupted_staff_%28perfected%29) |
| Crystal helm | [basic](https://oldschool.runescape.wiki/w/Crystal_helm_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Crystal_helm_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Crystal_helm_%28perfected%29) |
| Crystal body | [basic](https://oldschool.runescape.wiki/w/Crystal_body_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Crystal_body_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Crystal_body_%28perfected%29) |
| Crystal legs | [basic](https://oldschool.runescape.wiki/w/Crystal_legs_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Crystal_legs_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Crystal_legs_%28perfected%29) |
| Corrupted helm | [basic](https://oldschool.runescape.wiki/w/Corrupted_helm_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Corrupted_helm_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Corrupted_helm_%28perfected%29) |
| Corrupted body | [basic](https://oldschool.runescape.wiki/w/Corrupted_body_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Corrupted_body_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Corrupted_body_%28perfected%29) |
| Corrupted legs | [basic](https://oldschool.runescape.wiki/w/Corrupted_legs_%28basic%29) | [attuned](https://oldschool.runescape.wiki/w/Corrupted_legs_%28attuned%29) | [perfected](https://oldschool.runescape.wiki/w/Corrupted_legs_%28perfected%29) |

Use cache bonuses and weapon categories, then test attack speed, reach, style,
animations/projectiles and no-ammo/no-rune operation. Higher tiers must improve
the published bonuses. The armour tier sum, not “full matching set” alone,
selects Hunllef and tornado mitigation.

### 6.3 Recipe table

All recipes are level 1 inside the minigame. Singing grants total XP equal to
shards consumed, split equally between Crafting and Smithing. One-tick actions
must award XP and points once.

| Output/upgrade | Ingredients | XP/points |
|---|---|---|
| Basic weapon, any style | 1 weapon frame; **0 shards** | 0 singing XP; 2 points |
| Attuned weapon | basic + 50 shards | 25 Crafting + 25 Smithing; 5 points |
| Perfected weapon | attuned + matching component | 0 shard XP; 10 points |
| Basic helm/body/legs | 1 ore + 1 bark + 1 linum + 50 shards | 25+25 XP; 2 points |
| Attuned helm/legs | basic + 1 of each resource + 50 shards | 25+25 XP; 5 points |
| Attuned body | basic + 2 of each resource + 100 shards | 50+50 XP; 5 points |
| Perfected piece | attuned + 2 of each resource + 100 shards | 50+50 XP; 10 points |
| Vial | 10 shards | 5+5 XP; 0 points |
| Teleport crystal | 50 shards | 25+25 XP; 0 points |
| Crystal/corrupted paddlefish | paddlefish + 10 shards | 5+5 XP; points rule below |
| Escape crystal | 200 shards | 100+100 XP; 0 points |
| Crystal/corrupted dust ×10 | 10 shards + pestle | no XP; one tick |
| Egniol potion (3) | grym potion (unf) + 10 dust | 10 Herblore XP; 0 points |
| Paddlefish | raw paddlefish at range | 15 Cooking XP; 1 point; burn through level 46 |

The reward page is internally inconsistent about crystallised fish: its prose
lists both 2 and 1 point, while its worked example assigns four converted fish
four points. Use **1 point per conversion** unless a live measurement disproves
the worked example; record that measurement in the implementation commit.

Crafting is transactional: verify ingredients and output space first, consume
once, produce once, then award XP/points. Support inventory and worn source
equipment without deleting an item if the output cannot be placed.

---

## 7. Resources, ordinary NPCs and drops

### 7.1 Resource nodes

Each gather action yields one resource and depletes the node after its fixed
count. Do not add the whole yield in one click.

| Node (normal / corrupted) | Tool | Actions | XP each | Shard roll each | Wiki |
|---|---|---:|---:|---|---|
| Crystal / Corrupt Deposit | matching pickaxe | 3 | 10 Mining | 10–30 at 1/3 | [Crystal](https://oldschool.runescape.wiki/w/Crystal_Deposit), [corrupt](https://oldschool.runescape.wiki/w/Corrupt_Deposit) |
| Phren / Corrupt Phren Roots | matching axe | 3 | 10 Woodcutting | 10–30 at 1/3 | [Crystal](https://oldschool.runescape.wiki/w/Phren_Roots), [corrupt](https://oldschool.runescape.wiki/w/Corrupt_Phren_Roots) |
| Linum / Corrupt Linum Tirinum | none | 3 | 1 Farming | 10–30 at 1/3 | [Crystal](https://oldschool.runescape.wiki/w/Linum_Tirinum_%28plant%29), [corrupt](https://oldschool.runescape.wiki/w/Corrupt_Linum_Tirinum_%28plant%29) |
| Grym / Corrupt Grym Root | none | 1 | 1 Farming | 10–30 at 1/3 | [Crystal](https://oldschool.runescape.wiki/w/Grym_Root), [corrupt](https://oldschool.runescape.wiki/w/Corrupt_Grym_Root) |
| Fishing / Corrupt Fishing Spot | matching harpoon | 4 | 10 Fishing | 10–30 at 1/3 | [Crystal](https://oldschool.runescape.wiki/w/Fishing_Spot_%28The_Gauntlet%29), [corrupt](https://oldschool.runescape.wiki/w/Corrupt_Fishing_Spot) |

Use the correct skilling animation and one-action timing. Filling a vial from a
fishing spot must not interrupt fishing; clicking the spot after filling can
produce the next fish in one tick. Handle the documented one-free-slot resource
edge consistently and never discard stackable output silently.

### 7.2 Complete NPC roster

The normal and corrupted roster has nine combatants plus Hunllef/tornadoes.
The table gives the minimum combat contract; also load the cache’s full attack,
strength, defence, magic, ranged and bonus records.

| NPC | id | level / HP | style, speed, max | Corrupted counterpart | id | level / HP | style, speed, max |
|---|---:|---:|---|---|---:|---:|---|
| [Crystalline Rat](https://oldschool.runescape.wiki/w/Crystalline_Rat) | 9026 | 23 / 12 | Crush, 4, 4 | [Corrupted Rat](https://oldschool.runescape.wiki/w/Corrupted_Rat) | 9040 | 33 / 12 | Crush, 4, 14 |
| [Crystalline Spider](https://oldschool.runescape.wiki/w/Crystalline_Spider) | 9027 | 22 / 12 | Stab, 4, 4 | [Corrupted Spider](https://oldschool.runescape.wiki/w/Corrupted_Spider) | 9041 | 32 / 12 | Stab, 4, 5 |
| [Crystalline Bat](https://oldschool.runescape.wiki/w/Crystalline_Bat) | 9028 | 33 / 12 | Slash, 4, 8 | [Corrupted Bat](https://oldschool.runescape.wiki/w/Corrupted_Bat) | 9042 | 48 / 12 | Slash, 4, 11 |
| [Crystalline Unicorn](https://oldschool.runescape.wiki/w/Crystalline_Unicorn) | 9029 | 45 / 38 | Stab, 4, 6 | [Corrupted Unicorn](https://oldschool.runescape.wiki/w/Corrupted_Unicorn) | 9043 | 61 / 38 | Stab, 4, 17 |
| [Crystalline Scorpion](https://oldschool.runescape.wiki/w/Crystalline_Scorpion) | 9030 | 64 / 38 | Slash, 4, 11 | [Corrupted Scorpion](https://oldschool.runescape.wiki/w/Corrupted_Scorpion) | 9044 | 89 / 38 | Slash, 4, 17 |
| [Crystalline Wolf](https://oldschool.runescape.wiki/w/Crystalline_Wolf) | 9031 | 69 / 38 | Crush, 4, 8 | [Corrupted Wolf](https://oldschool.runescape.wiki/w/Corrupted_Wolf) | 9045 | 97 / 38 | Crush, 4, 19 |
| [Crystalline Bear](https://oldschool.runescape.wiki/w/Crystalline_Bear) | 9032 | 172 / 100 | Slash, 4, 28 | [Corrupted Bear](https://oldschool.runescape.wiki/w/Corrupted_Bear) | 9046 | 258 / 100 | Slash, 4, 48 |
| [Crystalline Dragon](https://oldschool.runescape.wiki/w/Crystalline_Dragon) | 9033 | 172 / 100 | Magic, 4, 28 | [Corrupted Dragon](https://oldschool.runescape.wiki/w/Corrupted_Dragon) | 9047 | 258 / 100 | Magic, 4, 48 |
| [Crystalline Dark Beast](https://oldschool.runescape.wiki/w/Crystalline_Dark_Beast) | 9034 | 172 / 100 | Ranged, 4, 28 | [Corrupted Dark Beast](https://oldschool.runescape.wiki/w/Corrupted_Dark_Beast) | 9048 | 258 / 100 | Ranged, 4, 48 |

The 22 July 2026 update reduced demi-boss attack range from 16 to 10 and
standardised non-demi-boss defensive stats. Implement dragon and dark-beast
projectiles/hit delay rather than allowing generic melee AI to stand in for
their published styles.

### 7.3 Enemy drops and per-run guarantees

Shard bundles go directly to inventory after the 2026 update. Other drops use
normal private ground/inventory behaviour and must survive a full inventory.

| Tier | Always | Secondary roll | Frame guarantee | Points |
|---|---|---|---|---:|
| Weak: rat/spider/bat | 20–30 shards | nothing 9/24; 3–7 shards 9/24; raw paddlefish 1–3 at 3/24; grym leaf 2/24; teleport crystal 1/24; frame base roll 1/4 | first weak kill in the run always gives a frame | 2 |
| Strong: unicorn/scorpion/wolf | 80–100 shards | 7–14 shards 9/21; nothing 6/21; raw paddlefish 2–4 at 3/21; grym leaf 2/21; teleport crystal 1/21; frame 2/7 | guaranteed by the second strong kill if no applicable frame was received; second-frame common behaviour must be retained | 5 |
| Demi-boss | 50–60 shards + weapon frame + perfected component | nothing 3/18; 10–21 shards 9/18; raw paddlefish 3–5 at 3/18; grym leaf 2/18; teleport crystal 1/18 | frame always | 10 |

The corrupted NPC pages publish the exact denominators above from very large
Drop Log Project samples: for example,
[Corrupted Rat](https://oldschool.runescape.wiki/w/Corrupted_Rat) (2,758,424
kills at the audit date),
[Corrupted Unicorn](https://oldschool.runescape.wiki/w/Corrupted_Unicorn)
(796,616) and [Corrupted Bear](https://oldschool.runescape.wiki/w/Corrupted_Bear)
(1,822,439). The normal NPC pages render only “common/uncommon”, but the
[strategy page](https://oldschool.runescape.wiki/w/The_Gauntlet/Strategies#The_Corrupted_Gauntlet)
explicitly says the corrupted monsters have the same mechanics and drop tables
as their crystalline counterparts and differ in combat stats. Therefore use
the same exact secondary denominators in both modes. This is a Wiki-backed
equivalence, not an independently sampled normal-mode table; retain that
provenance in the data manifest.

Demi-boss component rules:

- first bear → spike, first dragon → orb, first dark beast → bowstring;
- killing the same species again guarantees a different component the player
  does not already own/receive, so three demi-bosses of any species can supply
  all three components;
- the selection is based on components obtained in this run, not three separate
  permanent booleans tied only to species;
- each demi-boss always drops a frame, even after prior frame drops.

---

## 8. Hunllef combat state machine

### 8.1 Boss records

| Boss | ids/forms | level | size | HP | attack speed | Wiki |
|---|---|---:|---:|---:|---:|---|
| Crystalline Hunllef | 9021–9024 | 674 | 5×5 | 600 | 5 ticks | [record](https://oldschool.runescape.wiki/w/Crystalline_Hunllef) |
| Corrupted Hunllef | 9035–9038 | 894 | 5×5 | 1,000 | 5 ticks | [record](https://oldschool.runescape.wiki/w/Corrupted_Hunllef) |
| Normal tornado | 9025 | — | non-interactive | 20-tick life | moves/damages each tick | [Tornado](https://oldschool.runescape.wiki/w/Tornado_%28The_Gauntlet%29) |
| Corrupted tornado | 9039 | — | non-interactive | 20-tick life | moves/damages each tick | [Corrupted tornado](https://oldschool.runescape.wiki/w/Tornado_%28Corrupted_Gauntlet%29) |

Spawn the boss on its authored centre tile, not the player’s landing tile. It is
immune to poison/venom. Its protection prayer starts in the authentic style and
must be visually observable before the first player attack.

### 8.2 Attack cycle

Use one authoritative attack counter:

1. Hunllef always begins with Ranged.
2. After four qualifying attacks it telegraphs and changes to Magic; after four
   more it changes back. Standard, prayer-disable and tornado attacks count.
   Stomp does not.
3. Ranged uses the crystal projectile; Magic uses the orb projectile.
4. A magic-cycle roll may choose the distinct prayer-disabling projectile. It
   deactivates prayers at the authentic impact/event timing and is identifiable
   by both colour/animation and sound.
5. A tornado summon replaces a qualifying standard attack and increments the
   same counter.
6. If the player is underneath the 5×5 boss when it attacks, perform the typeless
   stomp/trample. Being merely adjacent to the footprint is not “underneath”.
7. Before every style switch play the cache animation and audio cue added to
   OSRS in 2020/2023; a chat message is not a substitute.

The distinct projectile proves which attack was selected, but no public client
source exposes the server roll that selected it. Measure the exact
prayer-disable chance, remaining projectile/impact delays, tornado-attack
cadence and normal armour-reduced wrong-prayer maxima as specified in §13.
Crystalline Hunllef's unarmoured base/stomp ceiling is 50 by the cross-checked
derivation in §13.3; corrupted stomp is exactly 68 typeless and corrupted
off-prayer maxima are in the table below. Put measured values in named constants
with a source note. In particular, the current `1/5` prayer-disable and `1/6`
tornado rolls are unsourced guesses.

### 8.3 Player protection and damage

Correct-prayer maximum hit and tornado range are selected by the **sum of the
three worn armour tiers**:

| Tier sum | Crystalline standard | Crystalline tornado | Corrupted standard | Corrupted tornado | Corrupted off-prayer max |
|---:|---:|---:|---:|---:|---:|
| 0 | 12 | 10–20 | 16 | 15–30 | 68 |
| 3 | 10 | 10–16 | 13 | 15–25 | 55 |
| 6 | 8 | 7–13 | 10 | 10–20 | 45 |
| 9 | 6 | 5–10 | 8 | 7–15 | 35 |

The mode-specific
[normal tornado](https://oldschool.runescape.wiki/w/Tornado_%28The_Gauntlet%29)
and [corrupted tornado](https://oldschool.runescape.wiki/w/Tornado_%28Corrupted_Gauntlet%29)
pages give the ranges in this table. They supersede the older pinned main-page
values of 14–25 and 8–15 for corrupted tier sums 3 and 9.

Do not halve those standard maxima again. Roll inclusively in the documented
range. Wrong/no prayer uses its own table/formula, not `2 × correct damage`.
Tornado damage is independent for every tornado occupying the player tile and
is not reduced by protection prayer.

### 8.4 Hunllef protection prayer

- Every sixth attack made with a style Hunllef is not currently protecting
  against triggers a switch to the sixth attack’s style.
- Zero-damage accuracy misses count if they are off-prayer; attacks made into
  its active protection do not advance the counter and deal zero.
- The sixth hit resolves against the old prayer, then changes the displayed
  protection for subsequent hits.
- Melee, ranged and magic must all map correctly, including an unarmed kick used
  by the 5:1 method.

Track “attacked wrong protection” separately for perfection CAs. Preserve the
existing active-NPC restoration regression test when tornadoes spawn during a
player-hit callback.

### 8.5 Tornadoes

Normal summons 1 above 66% HP, 2 from 33–66%, and 3 below 33%. Corrupted summons
one extra in every band (2/3/4). HP controls the count of a tornado **attack**;
crossing a threshold does not itself summon a wave. Each tornado:

- spawns at a valid distinct arena tile with the cache graphic/NPC;
- takes exactly one walking step per game tick toward the player's last
  resolved server tile, without changing the boss's active target or
  swallowing a queued hit;
- deals its mode/tier damage every tick on the same tile;
- remains for exactly 20 ticks (12 seconds), then despawns;
- cannot be attacked or interacted with and is cleared on every fight exit.

Do **not** implement the tornado as a conventional combat NPC, an intercepting
homing projectile or an A* pursuit of the player's queued destination. Two
specialist demonstrations expose the actual chase semantics. CaptBartlett's
[controlled player-path demonstration](https://www.youtube.com/watch?v=adGjfVmD5JQ&t=20s)
shows that tornadoes use the same direct walk-to-tile geometry as a player and
[continually click the player's tile rather than predicting it](https://www.youtube.com/watch?v=adGjfVmD5JQ&t=81s).
Bad OSRS's tick-by-tick
[Tornado Class](https://www.youtube.com/watch?v=XAq5fRFUOPk&t=55s)
independently demonstrates that each tornado continues toward the tile occupied
on the preceding resolution while the player runs away. Implement this order:

1. snapshot the player's resolved tile for the chase;
2. advance the player through the normal player-movement phase;
3. for each tornado, recompute a one-step direct route to the snapshot: step
   diagonally toward it when both coordinate deltas are non-zero, otherwise
   step one tile on the differing cardinal axis;
4. permit tornadoes to share a tile--they do not spread out or avoid one
   another--and resolve contact independently after movement;
5. damage only when a tornado and the player's resolved tile coincide on that
   tick. Crossing in opposite directions and a tornado occupying the skipped
   middle tile of a two-tile run are not contact.

The last rule is shown directly in Molgoatkirby's
[same-tile explanation and two-tile loop](https://www.youtube.com/watch?v=X936zH1VUO8&t=1780s)
and is why a player can run through an adjacent tornado. Add golden coordinate
traces for a stationary diagonal target, a moving cardinal target, a 90-degree
turn, stacked tornadoes, an adjacent cross-through and a skipped-middle-tile
run. A generic `npc_walk(player_coord)` is acceptable only if those traces pass;
ordinary collision-aware NPC chase code is not the specification.

The strongest public spawn-location evidence is the community
[Gauntlet Safe Tiles observation](https://www.reddit.com/r/2007scape/comments/r1gvfp/gauntlet_safe_tiles/):
at most one tornado per arena corner/quadrant and never on the player's tile,
although an adjacent tile is possible. Its author explicitly labels this
anecdotal. Use it as a hypothesis for the trace in §13, not as an exact spawn
algorithm. One open-source practice tool models this as one random tile in each
[4×4 corner quadrant](https://github.com/kareth/osrs-cg/blob/80e8e01f71644727618c56d1b068a7ce0df78277/index.html#L642-L650),
but it is a trainer with documented approximations, not server evidence.

### 8.6 Damaging floor

Implement the floor as a per-fight state machine, not temporary locs emitted and
checked in the same procedure:

`choose legal pattern → warning colour → damaging orange → clear/next pattern`.

- There are three speed bands selected by HP thirds, but the recovered atlas
  has two mask banks: 14 masks at or above one-third HP and five masks below
  one-third. Corrupted speed thresholds are 1000–667, 666–333 and ≤332;
  normal uses equivalent thirds of 600. The low-HP bank begins below one-third
  (≤332 corrupted, ≤199 normal), not at exactly one-third.
- During every damaging tick, each player on an orange tile takes the Wiki's
  stated 10–20 damage. The Wiki currently marks that value “confirmation
  needed”, so §13.5 must verify the support and distribution.
- Later patterns become harder and warning-to-orange time becomes shorter.
- The final set preserves the documented door-adjacent safe tiles shown on the
  [strategy page](https://oldschool.runescape.wiki/w/The_Gauntlet/Strategies#Boss_fight).
- Patterns must be translated against fixed arena coordinates, not offsets from
  wherever the boss happens to stand.
- Visual loc state, collision query and damage state must change on the same
  tick and survive boss movement/pathing.

Use the exact masks transcribed in §13.2 from the community atlas and add a
renderer/self-test that round-trips those 12×12 strings. The masks are resolved;
warning/orange durations still require the per-tick trace in §13.5.

---

## 9. Boss-room transitions and presentation

- Manual Barrier Pass asks for confirmation; Quick-pass skips it. Timer expiry
  forces the same transition without a dialogue race.
- Close interfaces, cancel gathering/crafting, set `gauntlet_boss_started`, move
  the player to a safe entry tile and spawn exactly one mode-correct Hunllef.
- Teleport crystals fail in the boss room. Barrier Escape and an escape crystal
  award points-based failure loot.
- Clear all tornado/floor actors before freeing the instance.
- On a valid kill, play [Clearing the Gauntlet](https://oldschool.runescape.wiki/w/Clearing_the_Gauntlet)
  and return to the lobby only after the death animation/event commits.
- Play [The Gauntlet](https://oldschool.runescape.wiki/w/The_Gauntlet_%28music_track%29)
  in normal and corrupted room regions; verify map-instance music region lookup.
- Use cache attack, style-switch, prayer-disable, floor and tornado sounds and
  spot animations. Add an audiovisual live-client checklist so mechanically
  correct but unreadable attacks cannot pass.

---

## 10. Reward chest and all loot

Generate a reward once at run finalisation and persist the concrete item list.
Opening the chest delivers that list; closing, logging out or lacking space must
not reroll it. If only one slot is free and several stacks/items are awarded,
place what fits and drop remaining tradeable items privately at the player as
the Wiki specifies. Route untradeables/pets through their normal ownership
systems.

### 10.1 Failure loot and points

Thresholds: 0 points leaves the chest open/empty; 1–49 rolls junk once; 50+
rolls the incomplete table once. Points:

- 10: demi-boss kill or attuned→perfected upgrade;
- 5: strong kill or basic→attuned upgrade;
- 2: weak kill or basic item creation;
- 1: raw paddlefish cooked; crystallised-paddlefish conversion per §6.3;
- 0: boss damage, gathering, potion/vial/teleport/escape creation.

Junk table, equal 1/3:
[Iwan’s flyer](https://oldschool.runescape.wiki/w/Iwan%27s_flyer),
[Potion](https://oldschool.runescape.wiki/w/Potion_%28Apothecary%29),
[rotten tomato](https://oldschool.runescape.wiki/w/Rotten_tomato).

Incomplete table, each exactly 1/27:

| Weapons/armour (15) | Runes (6) | Other (6) |
|---|---|---|
| Adamant dagger ×1; adamant full helm ×1; adamant mace ×2–3 noted; adamant pickaxe ×1; adamant platebody ×1; adamant platelegs ×1; adamant plateskirt ×1; adamant scimitar ×1; maple longbow ×7–13 noted; maple shortbow ×8–11 noted; mithril full helm ×1; mithril mace ×2–5 noted; mithril platebody ×1; mithril platelegs ×1; mithril plateskirt ×1 | air 200–300; body 250–350; earth 200–300; fire 200–300; mind 300–400; water 200–300 | cake 10–20 noted; cod 75–125 noted; trout 50–100 noted; eye of newt 300–500 noted; silver bar 15–30 noted; uncut sapphire 1–3 noted |

Every item above is sourced by the [incomplete reward table](https://oldschool.runescape.wiki/w/Reward_Chest_%28The_Gauntlet%29#Incomplete_loot_table).

### 10.2 Complete main tables

Normal gives 5–9 crystal shards and **two** main rolls. Corrupted gives 7–12
shards, the cape when eligible and **three** main rolls.

| Outcome | Normal quantity/rate per roll | Corrupted quantity/rate per roll |
|---|---|---|
| Battlestaff | 4–8 noted, 1/24 | 8–12 noted, 1/24 |
| Rune full helm | 2–4 noted, 1/24 | 3–5 noted, 1/24 |
| Rune chainbody | 1–2 noted, 1/24 | 2–3 noted, 1/24 |
| Rune platebody | 1–2 noted, 1/24 | 2 noted, 1/24 |
| Rune platelegs | 1–2 noted, 1/24 | 2–3 noted, 1/24 |
| Rune plateskirt | 1–2 noted, 1/24 | 2–3 noted, 1/24 |
| Rune halberd | 1–2 noted, 1/24 | 2–3 noted, 1/24 |
| Rune pickaxe | 1–2 noted, 1/24 | 2–3 noted, 1/24 |
| Dragon halberd | 1 noted, 1/24 | 1–2 noted, 0.5/24 |
| Cosmic rune | 160–240, 1/24 | 175–250, 1/24 |
| Nature rune | 100–140, 1/24 | 125–150, 1/24 |
| Law rune | 80–140, 1/24 | 100–150, 1/24 |
| Chaos rune | 180–300, 1/24 | 200–350, 1/24 |
| Death rune | 100–160, 1/24 | 125–175, 1/24 |
| Blood rune | 80–140, 1/24 | 100–150, 1/24 |
| Mithril arrow | 800–1,200, 1/24 | 1,000–1,500, 1/24 |
| Adamant arrow | 400–600, 1/24 | 500–750, 1/24 |
| Rune arrow | 200–300, 1/24 | 250–450, 1/24 |
| Dragon arrow | 30–85, 1/24 | 50–100, 1/24 |
| Uncut sapphire | 20–60 noted, 1/24 | 25–65 noted, 1/24 |
| Uncut emerald | 10–50 noted, 1/24 | 15–60 noted, 1/24 |
| Uncut ruby | 5–30 noted, 1/24 | 10–40 noted, 1/24 |
| Uncut diamond | 3–7 noted, 1/24 | 5–15 noted, 1/24 |
| Coins | 20,000–80,000, 1/24 | 75,000–150,000, **1.5/24** |

The corrupted half-slot removed from dragon halberds belongs to coins, yielding
the 1.5/24 coin rate. Keep that weighting explicit in data rather than hiding it
in a special-case branch.

### 10.3 Independent tertiary rolls

Each row rolls independently of the main table and of the other tertiary rows:

| Item | Normal | Corrupted | Integration |
|---|---:|---:|---|
| [Elite clue scroll](https://oldschool.runescape.wiki/w/Clue_scroll_%28elite%29) | 1/25 | 1/20 | apply shared Combat Achievement clue-rate modifier (1/23 and 1/19 at Elite reward tier) and scroll-box rules |
| [Crystal weapon seed](https://oldschool.runescape.wiki/w/Crystal_weapon_seed) | 1/120 | 1/50 | collection log |
| [Crystal armour seed](https://oldschool.runescape.wiki/w/Crystal_armour_seed) | 1/120 | 1/50 | collection log |
| [Enhanced crystal weapon seed](https://oldschool.runescape.wiki/w/Enhanced_crystal_weapon_seed) | 1/2,000 | 1/400 | collection log |
| [Youngllef](https://oldschool.runescape.wiki/w/Youngllef) | 1/2,000 | 1/800 | pet ownership/insurance + collection log; normal/corrupted Metamorphosis |
| [Gauntlet cape](https://oldschool.runescape.wiki/w/Gauntlet_cape) | — | guaranteed if not owned | check inventory, bank **and cape rack**; collection log |

The pet roll still records/logs the drop when the follower/item cannot simply be
placed. Use the project’s shared pet award path, not a raw `inv_add` ownership
check.

---

## 11. Records, collection log and Combat Achievements

### 11.1 Persistent records

Track per mode:

- completions;
- deaths;
- best completion time in ticks/centiseconds;
- current immutable pending reward.

The scoreboard displays personal and global completions/deaths/PBs for both
modes. If this server has no durable global aggregate, label the server-local
aggregate honestly; do not hard-code volatile Wiki totals. Corrupted completions
also count toward normal Gauntlet kill-count Combat Achievements, per the 25 June
2025 Combat Achievement change, while scoreboard KCs remain separate.

The [collection log](https://oldschool.runescape.wiki/w/Collection_log#The_Gauntlet)
has exactly five slots: Youngllef, crystal armour seed, crystal weapon seed,
enhanced crystal weapon seed and Gauntlet cape.

### 11.2 All current Combat Achievements

Instrument flags during the run rather than reconstructing them after the kill.

| Mode | Task | Acceptance |
|---|---|---|
| Normal | [Crystalline Warrior](https://oldschool.runescape.wiki/w/Crystalline_Warrior) | kill in full perfected armour |
| Normal | [Wolf Puncher](https://oldschool.runescape.wiki/w/Wolf_Puncher) | make no more than one attuned weapon |
| Normal | [Gauntlet Veteran](https://oldschool.runescape.wiki/w/Gauntlet_Veteran) | 5 completions |
| Normal | [3, 2, 1 - Range](https://oldschool.runescape.wiki/w/3,_2,_1_-_Range) | no off-prayer damage |
| Normal | [Egniol Diet](https://oldschool.runescape.wiki/w/Egniol_Diet) | make no egniol potion |
| Normal | [Perfect Crystalline Hunllef](https://oldschool.runescape.wiki/w/Perfect_Crystalline_Hunllef) | no tornado/floor/stomp/off-prayer damage and no attack into wrong protection |
| Normal | [Gauntlet Master](https://oldschool.runescape.wiki/w/Gauntlet_Master) | 20 qualifying completions, including corrupted |
| Normal | [Gauntlet Speed-Chaser](https://oldschool.runescape.wiki/w/Gauntlet_Speed-Chaser) | under **4:45** |
| Normal | [Defence Doesn't Matter](https://oldschool.runescape.wiki/w/Defence_Doesn%27t_Matter) | make no armour |
| Normal | [Gauntlet Speed-Runner](https://oldschool.runescape.wiki/w/Gauntlet_Speed-Runner) | under **3:45** |
| Corrupted | [Corrupted Gauntlet Veteran](https://oldschool.runescape.wiki/w/Corrupted_Gauntlet_Veteran) | 5 completions |
| Corrupted | [3, 2, 1 - Mage](https://oldschool.runescape.wiki/w/3,_2,_1_-_Mage) | no off-prayer damage |
| Corrupted | [Corrupted Gauntlet Master](https://oldschool.runescape.wiki/w/Corrupted_Gauntlet_Master) | 10 completions |
| Corrupted | [Perfect Corrupted Hunllef](https://oldschool.runescape.wiki/w/Perfect_Corrupted_Hunllef) | no tornado/floor/stomp/off-prayer damage and no wrong-protection attack |
| Corrupted | [Corrupted Warrior](https://oldschool.runescape.wiki/w/Corrupted_Warrior) | kill in full perfected corrupted armour |
| Corrupted | [Defence Doesn't Matter II](https://oldschool.runescape.wiki/w/Defence_Doesn%27t_Matter_II) | make no armour |
| Corrupted | [Corrupted Gauntlet Speed-Chaser](https://oldschool.runescape.wiki/w/Corrupted_Gauntlet_Speed-Chaser) | under **7:05** |
| Corrupted | [Corrupted Gauntlet Grandmaster](https://oldschool.runescape.wiki/w/Corrupted_Gauntlet_Grandmaster) | 50 completions |
| Corrupted | [Corrupted Gauntlet Speed-Runner](https://oldschool.runescape.wiki/w/Corrupted_Gauntlet_Speed-Runner) | under **6:05** |
| Corrupted | [Wolf Puncher II](https://oldschool.runescape.wiki/w/Wolf_Puncher_II) | make no more than one attuned weapon |
| Corrupted | [Egniol Diet II](https://oldschool.runescape.wiki/w/Egniol_Diet_II) | make no egniol potion |

Use strict “less than” comparisons for speed tasks. Capture total elapsed time
from run entry through valid boss completion, not preparation time remaining or
boss-fight duration alone.

---

## 12. Ordered delivery slices

### Slice 0 — freeze data and add audit fixtures

- Add DB/enums for room archetypes, recipes, NPC drops and reward rows.
- Add a Wiki snapshot header/revision note beside each table.
- Add self-tests that currently fail for known recipe, drop, reward and damage
  mismatches. Preserve `gauntletrun`’s active-NPC regression.
- Correct stale claims in `docs/GAUNTLET.md` or point it to this plan.

### Slice 1 — lifecycle and lobby

- Implement Bryn, books, scoreboard, deposit box and canonical entry options.
- Make holding/restoration and run finalisation transactional/idempotent.
- Add boost reset, disconnect/login recovery and every exit path.
- Persist deaths, KCs, start tick and PBs.

### Slice 2 — generator, discovery and interfaces

- Land seeded connected graph + room manifest.
- Enforce normal/corrupted resource guarantees and 2026 demi-boss placement.
- Wire door lighting, 49 discovery bits, current-room updates, map and exact
  countdown overlay.
- Add seed-based property tests over at least 10,000 layouts per mode.

### Slice 3 — gathering, items and crafting

- Replace atomic nodes with per-action gathering and correct XP/shards.
- Implement every temporary item interaction in §6.
- Drive interface 640 from recipe data, including XP, points and repeated craft.
- Correct cooking/burn, egniol, teleport/escape and combo-food timing.

### Slice 4 — ordinary monsters and drops

- Populate rooms from manifest and implement tier/style AI.
- Land complete shard/secondary/frame/component tables and guarantees.
- Test full inventory, private drops, duplicate demi-bosses and all 18 variants.

### Slice 5 — Hunllef

- Implement attack/prayer cycles, exact damage and stomp geometry.
- Implement tornado lifecycle and floor-mask state machine.
- Add protection visuals, projectiles, animations and sounds.
- Add deterministic fight simulation tests and simultaneous-death arbitration.

### Slice 6 — rewards and progression

- Make pending reward rolls immutable and persistent.
- Land junk, all 27 incomplete rows, main tables and independent tertiaries.
- Integrate clue modifiers, pet/cape ownership, collection log and 21 CAs.
- Wire completion jingle/music and final scoreboard presentation.

### Slice 7 — parity soak and cleanup

- Run the complete automated/live matrix, perform long random-seed and reward
  simulations and fix lifecycle leaks.
- Remove obsolete approximation constants/switches and replace misleading
  debug messages with test-only fixtures.
- Update `docs/GAUNTLET.md` to describe shipped behaviour and leave no deferred
  item in this plan without an explicit owner.

---

## 13. Hidden-value research audit — 17 August 2026

### 13.1 Evidence standard and source inventory

This audit searched Jagex news/dev statements, current and historical Wiki
pages, the current RuneLite Plugin Hub, open-source Gauntlet plugins and
simulators, Reddit and bot-development forums, and captioned/frame-inspected
YouTube guides and uninterrupted kills. Rank evidence as follows:

1. a Jagex statement or reproducible live-game/cache observation;
2. a specific Wiki value, preferably backed by the Drop Log Project;
3. current client code that directly observes game state;
4. a reproducible community dataset or exhaustive diagram;
5. anecdote or simulator choice, which may form a test hypothesis only.

RuneLite sees NPCs, projectiles, locs and varps **after** the server has made a
random choice. Client code can prove identifiers and observable outcomes but
cannot, by itself, prove the hidden probability that selected them. Private
server implementations and unsourced guide claims are not parity evidence.

Useful pinned research sources:

- Jagex's
  [Summer Sweep-Up 2026](https://secure.runescape.com/m=news/summer-sweep-up-gear--pvm-changes?oldschool=1)
  gives the four guaranteed cardinal demi-boss rooms and other July 2026 prep
  changes.
- The Plugin Hub currently pins the
  [Gauntlet Map commit](https://github.com/runelite/plugin-hub/blob/f790018f55c764de0c9cc1c987a9215d16197f5d/plugins/gauntlet-map)
  used for the 7×7 adjacency and perimeter-room evidence.
- The community
  [floor-pattern post](https://www.reddit.com/r/2007scape/comments/jla470/a_comprehensive_list_of_all_of_the_floor_patterns/)
  links an exhaustive
  [19-pattern atlas](https://imgur.com/gallery/diagrams-of-floor-tile-patterns-gauntlet-zfwXjSG),
  split into [14 masks at or above one-third HP](https://i.imgur.com/qOiEQy9.png)
  and [five masks below one-third HP](https://i.imgur.com/sU15nY5.png).
- The Plugin Hub also pins the
  [Gauntlet Recorder commit](https://github.com/runelite/plugin-hub/blob/f790018f55c764de0c9cc1c987a9215d16197f5d/plugins/gauntlet-recorder).
  Its [trace schema](https://github.com/lsmith090/gauntlet-recorder/blob/4647c8397508d76388772d72d4a94eee2260f6f6/DEVELOPMENT.md#log-format)
  records per-tick boss/tornado/floor positions, resources, NPC spawns,
  projectiles, animations, hitsplats, inventory, stats and prayer changes.
- Mod Curse confirmed the first attack is Ranged and that correct/off-prayer
  damage was rebalanced in a
  [2019 Jagex-mod reply](https://www.reddit.com/r/2007scape/comments/cjre20/hunllefs_damage_through_prayer_is_being_changed/evfr9ie/).
  That statement gives behaviour, not a direct normal-mode damage table.
- Three independent high-level movement demonstrations are especially useful:
  CaptBartlett's
  [controlled tornado-path experiment](https://www.youtube.com/watch?v=adGjfVmD5JQ&t=20s),
  Bad OSRS's diagrammed
  [Tornado Class](https://www.youtube.com/watch?v=XAq5fRFUOPk&t=55s), and
  Molgoatkirby's
  [same-tile collision explanation](https://www.youtube.com/watch?v=X936zH1VUO8&t=1780s).
  These expose deterministic movement outcomes, although they cannot expose a
  server-side random-number denominator.
- A post-update
  [150-run cardinal-room sample](https://www.reddit.com/r/2007scape/comments/1vh5a00/the_corrupted_gauntlet_demiboss_spawn_rates/)
  includes a Wiki crowdsource check over 2,970 observed demi-boss spawns and
  several independent full-perimeter laps. It is the strongest public evidence
  found for the July 2026 demi-boss bias, but remains community observation.

### 13.2 Exact floor masks recovered

The atlas is a community observation rather than Jagex source, but it is an
explicit exhaustive tile diagram rather than a simulator guess. Its author
describes the sample as more than 700 kills and explicitly calls out the
unrotated south-west-only low-HP mask in the
[methodology comment](https://www.reddit.com/r/2007scape/comments/jla470/a_comprehensive_list_of_all_of_the_floor_patterns/ganvxtd/).
The following is its machine-readable transcription. Each value is a
12×12 arena-interior mask: rows run north to south, characters west to east,
`/` separates rows,
`#` is dangerous and `.` is safe. Preserve orientation; notably low-HP mask
`L04` is south-west only, not a mask to rotate randomly.

At or above one-third HP (normal HP 200–600; corrupted HP 333–1,000):

```text
H01 ###......###/###......###/###......###/###......###/###......###/###......###/###......###/###......###/###......###/###......###/###......###/###......###
H02 ####......../####......../####......../####......../####......../####......../####......../####......../####......../####......../####......../####........
H03 ............/............/............/............/....######../....######../....######../....######../....######../....######../............/............
H04 ######....../######....../######....../######....../######....../######....../............/............/............/............/............/............
H05 ############/############/############/............/............/............/............/............/............/############/############/############
H06 ############/############/############/############/............/............/............/............/............/............/............/............
H07 ............/............/....######../....######../....######../....######../....######../....######../............/............/............/............
H08 ......######/......######/......######/......######/......######/......######/............/............/............/............/............/............
H09 ........####/........####/........####/........####/........####/........####/........####/........####/........####/........####/........####/........####
H10 ............/............/..######..../..######..../..######..../..######..../..######..../..######..../............/............/............/............
H11 ............/............/............/............/............/............/......######/......######/......######/......######/......######/......######
H12 ............/............/............/............/............/............/............/............/############/############/############/############
H13 ............/............/............/............/..######..../..######..../..######..../..######..../..######..../..######..../............/............
H14 ............/............/............/............/............/............/######....../######....../######....../######....../######....../######......
```

Below one-third HP (normal HP 0–199; corrupted HP 0–332):

```text
L01 ############/############/##........##/##........##/##........##/##........##/##........##/##........##/##........##/##........##/############/############
L02 ####....####/####....####/####....####/####....####/............/............/............/............/####....####/####....####/####....####/####....####
L03 ............/.####..####./.####..####./.####..####./.####..####./............/............/.####..####./.####..####./.####..####./.####..####./............
L04 ............/............/............/............/............/............/######....../######....../######....../######....../######....../######......
L05 ###......###/###......###/###......###/............/....####..../....####..../....####..../....####..../............/###......###/###......###/###......###
```

Implementation consequences:

- replace the current three synthetic patterns with these 19 masks;
- select `H01`–`H14` in the first two speed bands and `L01`–`L05` only below
  one-third HP;
- do not generate rotations unless that rotated mask is separately present in
  the bank;
- validate all strings are exactly 12 rows × 12 columns and render them in a
  fixture so transcription errors are visible;
- compare every observed live mask against this set before promoting the atlas
  from high-confidence community evidence to locally reproduced evidence.

### 13.3 Values recovered or materially narrowed

| Question | Finding | Evidence / implementation decision |
|---|---|---|
| Room connectivity | Complete orthogonal 7×7 grid; no random missing interior edges | Current [Gauntlet Map adjacency](https://github.com/StickySerum/gauntlet-map/blob/25d36de0cd6a2cd319bd2721885325b81c6756f4/src/main/java/com/gauntletmap/GauntletMapPlugin.java#L478-L555). Remove the matching-based edge deletion in `gauntlet_layout.rs2`. |
| Guaranteed demi-boss rooms | Four cardinal extremes, indices 4/22/28/46; species remains random | [Jagex update](https://secure.runescape.com/m=news/summer-sweep-up-gear--pvm-changes?oldschool=1) plus [client constants](https://github.com/StickySerum/gauntlet-map/blob/25d36de0cd6a2cd319bd2721885325b81c6756f4/src/main/java/com/gauntletmap/GauntletMapConstants.java#L62-L63). |
| Normal secondary drops | Same exact 24-, 21- and 18-part tables as corrupted mode | Exact corrupted Wiki tables plus the [Wiki's explicit same-drop-table statement](https://oldschool.runescape.wiki/w/The_Gauntlet/Strategies#The_Corrupted_Gauntlet). No separate normal denominator is required. |
| Tornado counts/lifetime | Normal 1/2/3, corrupted 2/3/4 by HP thirds; exactly 20 ticks | Dedicated [normal](https://oldschool.runescape.wiki/w/Tornado_%28The_Gauntlet%29) and [corrupted](https://oldschool.runescape.wiki/w/Tornado_%28Corrupted_Gauntlet%29) tornado pages. |
| Tornado damage | Normal: 10–20, 10–16, 7–13, 5–10; corrupted: 15–30, 15–25, 10–20, 7–15 for armour tier sums 0/3/6/9 | Same dedicated tornado pages; use these over contradictory older main-page minima. |
| First attack/style counter | First attack Ranged; switch every fourth qualifying attack; prayer-disable/tornado count, stomp does not | Hunllef Wiki pages, Mod Curse's reply above and observable client attack counters. |
| Corrupted hidden damage | Stomp max 68 typeless; off-prayer maxima 68/55/45/35 at tier sums 0/3/6/9 | [Corrupted Hunllef](https://oldschool.runescape.wiki/w/Corrupted_Hunllef). |
| Floor loc states | Normal base/warn/damage 36149/36150/36151; corrupted 36046/36047/36048 | Recorder [IDs and observed transitions](https://github.com/lsmith090/gauntlet-recorder/blob/4647c8397508d76388772d72d4a94eee2260f6f6/src/main/java/com/gauntletrecorder/GauntletIds.java#L19-L34). |
| Tornado chase and collision | **High confidence, implement now:** one direct walking step per tick toward the player's preceding resolved tile; no interception; tornadoes may stack; damage requires same-tile occupancy on the same tick, so cross-through and run-skipped tiles are safe | The three timestamped video experiments in §8.5 agree, and the Wiki independently fixes speed/lifetime/contact at one tile per tick, 20 ticks and same-tile damage. Replace generic combat-NPC pursuit with the explicit tick order and golden traces in §8.5. |
| Current demi-boss species bias | **High-confidence observation, exact generator still inferred:** patch-day full laps repeatedly report ten perimeter demis in a fixed 4 bear / 3 dragon / 3 dark-beast population. In the four guaranteed cardinal rooms, a 150-run sample found 90 bears, 36 dark beasts and 24 dragons, and a Wiki crowdsource check found 1,438 bears, 750 dragons and 782 dark beasts--both close to a 2:1:1 bear/dragon/beast mix | [Dataset and Wiki-admin crowdsource comment](https://www.reddit.com/r/2007scape/comments/1vh5a00/the_corrupted_gauntlet_demiboss_spawn_rates/) plus Gnomonkey's patch guide stating that [extra bears were deliberately made more common](https://www.youtube.com/watch?v=CA6-2CqM_NQ&t=1323s). Model a shuffled four-cardinal multiset of 2/1/1 and two additional copies of each species only behind a parity flag until a full-room trace confirms that allocation; do not use uniform independent species rolls. |
| Normal unarmoured base/stomp ceiling | **High-confidence derivation: 50.** Crystalline Hunllef has Strength 240 and +64 Strength bonus. The standard NPC effective level of 249 in the [published max-hit formula](https://oldschool.runescape.wiki/w/Strength_max_hit) gives `floor(0.5 + 249 × (64 + 64) / 640) = 50`. The same calculation gives 68 for Corrupted Hunllef (240, +112), exactly matching its published stomp/base maximum | The current normal value 40 is wrong. Use 50 for the unmitigated base roll and stomp, guarded by a regression that also derives corrupted 68. The normal armour-reduced off-prayer rows remain unresolved. Historical [Crystalline Hunllef revision 14321183](https://oldschool.runescape.wiki/w/Crystalline_Hunllef?oldid=14321183) also listed 50 before an unsourced edit changed it to `50+`. |
| Initial Hunllef protection behaviour | **Exact behaviour, weights unproved:** Hunllef spawns protecting from a random one of Melee, Ranged or Magic | The Wiki's [Protection prayers table](https://oldschool.runescape.wiki/w/Protection_prayers) is explicit. Keep all three outcomes and immediate pre-entry visibility; retain uniform `random(3)` as a labelled distribution hypothesis rather than calling the whole mechanic unknown. |
| Projectile distance effect | **Exact partial rule:** at one or two tiles, Hunllef's projectile lands one tick sooner than at ordinary ranged positioning | Molgoatkirby's [Redemption timing demonstration](https://www.youtube.com/watch?v=X936zH1VUO8&t=1628s). Implement the close-range one-tick branch only after a local projectile trace fixes the absolute base delay and boundary beyond two tiles. |

### 13.4 Residual values not located after deep-source search

Do not describe this list simply as "unpublished". Several values that looked
unpublished were recoverable from specialist videos, Wiki history, formulae or
community crowdsource data. For the narrower residuals below, this audit found
no reproducible exact value after checking official/Wiki history, Reddit and
forum archives, captioned high-level YouTube guides, public bot discussions,
RuneLite plugins and open-source simulators. That is a search result, not proof
that no private measurement exists. Current choices must remain labelled and
replaceable:

| Hidden value | What public evidence establishes | Current choice / status |
|---|---|---|
| Resource and ordinary-monster generation weights | 0–5 resource nodes, legal tiers and the normal/corrupted guarantees | Uniform node count/type and fixed guarantee rooms are approximations. Exact joint distributions, room/archetype weights and monster-pack weights remain unknown. |
| Current demi-boss construction | Ten-perimeter, 4/3/3 full-lap observations and approximately 2:1:1 cardinal species outcomes strongly constrain the result (§13.3) | The proposed `cardinals = shuffle(2 bears, 1 dragon, 1 beast)` plus `additional = 2 of each species` construction explains every public observation, but no source proves it. Exact six additional-room selection, room weighting, guarantee override order and whether the population is a fixed multiset rather than conditioned rolls still need complete traces. |
| Prayer-disable selection | Only occurs during Magic, has distinct projectile IDs 1713/1714 and counts in the four-attack cycle. A long-running community observation says it cannot recur until three Magic attacks after the prior disable; two disables in one four-Magic cycle remain possible (attacks 1 and 4) | [The three-Magic-attack spacing report](https://www.reddit.com/r/ironscape/comments/pn03xg/psa_tip_for_corrupted_gauntlet/) is consistent with player reports but does not give the eligible-roll probability. Implement a three-Magic cooldown behind a measurement flag; `random(5) = 0`, Bernoulli independence, denominator and selection/resolution tick remain unsupported. |
| Tornado-attack cadence | A summon replaces a qualifying attack and counts in the style cycle | `random(6) = 0` is unsupported. A high-level simulator explicitly says the true mechanism is unknown in its [10–14-attack approximation](https://github.com/ArtemisRS/hunllef/blob/c26b9047756e5ee7f45e2a63da6332007483374c/src/lib.rs#L186-L205). |
| Tornado spawn tiles | Community observation: no more than one per corner/quadrant, never directly on the player, adjacent is possible. This concerns **initial spawn only**; the exact chase algorithm is resolved in §8.5 | Fixed corner tiles are not parity. Exact candidate tiles, within-quadrant weights, collision rejection and fallback order remain unknown. Do not let this residual block the correct one-step, last-resolved-tile movement. |
| Floor-mask selection | The 14 high/mid-HP and five low-HP masks establish the outcome set | Relative weights, repeat prevention and transition restrictions are unknown; uniform selection is a hypothesis. |
| Floor warning/orange timing | Three speeds; per-tick loc states are observable. The recorder author observed warning leading damage by approximately 5–8 ticks. Frame inspection of an [uninterrupted no-armour kill](https://www.youtube.com/watch?v=qqtmSiz6oIg&t=9s) independently shows that the opening telegraph is about eight ticks, disproving the current four-tick phase-one value, but video blending is not a safe exact timer | Exact per-band or per-pattern warning distribution, orange duration and next-pattern gap remain unknown. Delete the current 4/3/2 warning and 5/4/3 active claims; collect ground-object IDs per tick rather than promoting rounded video timestamps. |
| Floor damage roll | The Wiki states 10–20 per orange-tile tick but marks it “confirmation needed” | Inclusive support, weighting and whether concurrent hazards roll independently need hitsplat traces; uniform 10–20 is provisional. |
| Normal armour-reduced off-prayer damage | Correct-prayer maxima are published and the unarmoured base/stomp ceiling is derivable as 50 (§13.3) | Tier-sum 3/6/9 off-prayer maxima and reduction formula remain unknown. Delete 40/32/26/20; do not scale corrupted rows without live hits. |
| Initial Hunllef protection weights | Random Melee/Ranged/Magic selection is Wiki-confirmed (§13.3) | Equality, mode/entry-side dependence and RNG timing are not published. Uniform `random(3)` is the best simple hypothesis, not a blocker to implementing the confirmed three-outcome behaviour. |
| Projectile impact timing | Projectile IDs and paths are client-visible; one-to-two-tile casts land exactly one tick faster (§13.3) | Absolute cast-to-hit delay and every distance band, including the prayer-wipe event tick, still need projectile `startCycle`/`endCycle` traces. Do not use only a guessed distance divisor. |
| Boss/player/tornado placement fallback | Boss has a stable authored arena placement; tornadoes reject or alter some spawns | Exact entry tile, tornado obstruction rules and fallback choice need coordinate traces. |

Search results also surfaced old guides claiming demi-bosses cannot appear in
corner rooms. Those claims conflict with the current Wiki, current Plugin Hub
constants and the July 2026 Jagex update; do not use them.

### 13.5 Reproducible measurement protocol

Use the current Plugin Hub Gauntlet Recorder rather than video timestamps. Its
`gameTick` stream and raw instance coordinates are sufficient, but its repository
does **not** publish the author's run logs; the documented “5–8 ticks” is a
range, not the missing exact duration table.

Check anonymised raw logs, the parser, derived CSV/JSON and game build/date into
`tools/data/gauntlet_measurements/`. Preserve raw observations so future
updates can be re-analysed. Required experiments:

1. **Generation census:** in each run reveal all 47 non-start/boss rooms before
   gathering or killing. Record mode, room index, archetype/rotation, every
   resource and every NPC. Separate the four forced cardinal rooms and all
   normal-mode guarantee overrides before estimating base distributions. For
   post-22-July-2026 runs, first test whether every complete perimeter is
   exactly 4 bears/3 dragons/3 dark beasts and whether the cardinal multiset is
   always 2/1/1; only then estimate room selection or independent weights. Use
   at least 10,000 complete runs per mode and publish multinomial counts plus
   confidence intervals; a small convenience-route sample is selection-biased.
2. **Prayer-disable roll:** classify projectile 1707/1708 as ordinary Magic
   and 1713/1714 as prayer-disable. Exclude stomps and tornado replacements.
   Record position in the four-attack cycle and attacks since each special.
   Gather at least 10,000 eligible Magic attacks, explicitly test the reported
   three-Magic-attack exclusion window and cycle-position dependence, then
   rationally reconstruct a denominator only if those tests support an
   independent roll.
3. **Tornado cadence/spawn:** for at least 10,000 waves, join the summon
   animation/attack counter to every `NPC_SPAWN`, player tile, boss footprint,
   HP and collision state. Test one-per-quadrant, exclusion of the player's
   tile, distinctness, candidate-tile support, weighting and fallback order.
   Separately replay the §8.5 golden traces against per-tick coordinates. Chase
   movement is already specified by reproducible video; do not wait for the
   random spawn selector before replacing ordinary NPC pursuit.
4. **Floor timing/masks:** on every boss-room tick, normalise the 36150/36151 or
   36047/36048 tiles to the 12×12 arena. Group by exact boss HP, measure every
   warning→orange→base transition and assert the grid is one of the 19 masks in
   §13.2. Continue until each mask and each speed band has repeated observations
   in both modes.
5. **Damage/impact:** record worn tier sum, active protection, cast tick,
   projectile arrival, prayer delta and hitsplat. Deliberately sample normal
   off-prayer hits across tier sums 3/6/9 and verify unarmoured stomp/base 50.
   Stratify projectile duration by exact Chebyshev distance, especially 1–2
   versus 3+ tiles. A largest observed hit is a lower bound, not proof of the
   ceiling; freeze a maximum only with exhaustive roll evidence, a stable
   inferred support over a large sample and an explicit confidence statement,
   or a first-party disclosure.
6. **Initial protection:** record the Hunllef NPC form on the first boss-room
   tick for a large set of fresh encounters, separately by mode and entry side.
   Test uniformity and dependence before replacing `random(3)`.

Until those artifacts exist, the residual rows in §13.4 are acceptance blockers
for **literal** parity, though they do not block a clearly labelled playable
approximation. “Feels like OSRS” is not an acceptance criterion.

---

## 14. Verification and acceptance matrix

### Automated gates

- `make -C src mock230-scripts` — all RuneScript/config symbols compile.
- `make -C src mock230-servpack` — server loc/NPC overlays pack cleanly.
- `make -C src test-mock230` — no engine/session regression.
- `make -C src mock230-cache` when CS2/interface/config assets change.
- A new Gauntlet contract test validates all recipe/drop/reward rows against
  checked-in manifests, like the existing God Wars contract checks.

Required deterministic fixtures:

- 10,000 layout seeds per mode: every legal orthogonal edge present, fixed
  start/boss adjacency, guarantees satisfied, demi-bosses only on perimeter
  and legal visual-room rotations.
- Every resource node: exact action count, XP, 1/3 shard branch and depletion.
- Every recipe: insufficient/exact/surplus ingredients, worn upgrades, full
  inventory, correct XP and points.
- All 18 NPCs: style/max/attack speed and every drop branch; duplicate component
  permutations and frame pity rules.
- Hunllef: four-attack alternation; stomp exclusion; six off-prayer hits; misses;
  prayer-disable; all armour/damage rows; 1/2/3 and 2/3/4 tornado bands; exact
  20-tick despawn; all floor masks/phase thresholds.
- Exit/death/logout at every run phase: gear restored once, instance freed once,
  temporary items gone, reward committed at most once.
- Reward tables: all outcomes reachable with exact weights/quantity bounds;
  all independent tertiary combinations; full-inventory continuation; no reroll
  after reconnect.
- Each CA positive case plus one failure event for every tracked restriction.

### Live-client checklist

Run at least one full normal and corrupted clear plus every failure exit:

- entry gear/boost handling and no item smuggling;
- map discovery, rotated doors, timer display/force-entry and interfaces;
- gather animations, fishing-vial continuity, cooking burn and combo eating;
- weapon reach/projectiles, monster magic/ranged attacks and private drops;
- Hunllef protection icon, style-switch cue, prayer-disable cue, floor visuals,
  tornado movement/damage and door safe tiles;
- barrier/escape messages, death animation, jingle, lobby return and chest loc;
- scoreboard/KC/PB/death, collection-log popup, CA popup, cape and Youngllef
  metamorphosis.

Completion means this matrix passes with no source hole from §13 left as an
uncited approximation.
