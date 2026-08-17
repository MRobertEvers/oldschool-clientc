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
than a description of the current tree. Section 13 remains deliberately open:
the Wiki does not publish the exact floor masks, room/content probability
tables, normal-mode secondary-drop denominators, prayer-disable probability,
or several hidden damage/spawn choices. The implementation uses the documented
behaviour plus labelled inferences for those values; exact parity requires a
reproducible live-game trace as specified there.

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

Build a connected 7×7 room graph with the boss fixed at (3,3) and the starting
room in one of the four adjacent positions. Choose compatible room archetypes
and rotations from normal `m29_88` or corrupted `m30_88`; do not merely sample
visual rooms with replacement and assume their doors form a valid graph.

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

Put room resource/monster probabilities in data. The Wiki does not publish the
full distribution, so capture it from an authoritative live/client source and
check the measured corpus into `docs/` or `tools/data/` before declaring parity.

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

The corrupted NPC pages publish the exact denominators above; normal pages use
“common/uncommon”, while the [strategy page](https://oldschool.runescape.wiki/w/The_Gauntlet/Strategies#The_Corrupted_Gauntlet)
states that both modes share mechanics and drop tables. Treat using the same
denominators for normal mode as a documented inference and verify it against a
live/drop-log source before closing the slice.

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

Measure/cache-cross-check the exact prayer-disable chance, projectile delays,
normal stomp maximum and normal wrong-prayer maximum because the Wiki does not
publish all four as exact numbers. Corrupted stomp/off-prayer max is 68 with no
armour. Put measured values in named constants with a source note.

### 8.3 Player protection and damage

Correct-prayer maximum hit and tornado range are selected by the **sum of the
three worn armour tiers**:

| Tier sum | Crystalline standard | Crystalline tornado | Corrupted standard | Corrupted tornado | Corrupted off-prayer max |
|---:|---:|---:|---:|---:|---:|
| 0 | 12 | 10–20 | 16 | 15–30 | 68 |
| 3 | 10 | 10–16 | 13 | 15–25 | 55 |
| 6 | 8 | 7–13 | 10 | 10–20 | 45 |
| 9 | 6 | 5–10 | 8 | 7–15 | 35 |

This table follows the newer, boss-specific Corrupted Hunllef revision. The
pinned main Gauntlet page instead says 14–25 for tier sum 3 and 8–15 for tier
sum 9, versus 15–25 and 7–15 above. Resolve that Wiki-source conflict with a
live test before freezing the damage data.

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
- pathfinds toward the player every tick without changing the boss’s active
  target or swallowing a queued hit;
- deals its mode/tier damage every tick on the same tile;
- remains for exactly 20 ticks (12 seconds), then despawns;
- cannot be attacked or interacted with and is cleared on every fight exit.

### 8.6 Damaging floor

Implement the floor as a per-fight state machine, not temporary locs emitted and
checked in the same procedure:

`choose legal pattern → warning colour → damaging orange → clear/next pattern`.

- There are three authored pattern sets and speeds selected by HP thirds.
  Corrupted thresholds are 1000–667, 666–333 and ≤332; normal uses equivalent
  thirds of 600.
- During every damaging tick, each player on an orange tile takes 10–20 damage.
- Later patterns become harder and warning-to-orange time becomes shorter.
- The final set preserves the documented door-adjacent safe tiles shown on the
  [strategy page](https://oldschool.runescape.wiki/w/The_Gauntlet/Strategies#Boss_fight).
- Patterns must be translated against fixed arena coordinates, not offsets from
  wherever the boss happens to stand.
- Visual loc state, collision query and damage state must change on the same
  tick and survive boss movement/pathing.

The Wiki documents floor layouts primarily through images. Extract exact tile
masks and timings from the cache/live client, check them into a small data file,
and add a renderer/self-test that proves the four door-safe tiles are absent
from every final-phase danger mask.

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

## 13. Source holes that require measurement

Do not guess these:

1. Full room-archetype/resource/monster probability distribution and exact
   connectivity algorithm.
2. Exact normal-mode secondary enemy-drop denominators (the Wiki labels them
   common/uncommon; same-as-corrupted is an inference).
3. Exact prayer-disable roll/timing and the normal stomp/wrong-prayer damage
   ceilings where the Wiki says only “50+”/“very high”; also resolve the main
   Gauntlet page versus Corrupted Hunllef page disagreement over tier-1/tier-3
   tornado minima.
4. Exact floor masks and warning/damage durations represented mainly by Wiki
   images.
5. Initial Hunllef protection-prayer distribution and exact tornado spawn tiles.

For each, use a reproducible cache/client trace or sufficiently large live
sample, store the artifact, and cite it next to the constant/data row. “Feels
like OSRS” is not an acceptance criterion.

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

- 10,000 layout seeds per mode: connected graph, fixed start/boss adjacency,
  guarantees satisfied, demi-bosses only on perimeter, legal rotations.
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
