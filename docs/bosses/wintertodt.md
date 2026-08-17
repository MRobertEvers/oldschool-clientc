# Wintertodt — full implementation plan

Implementation plan for bringing the Wintertodt encounter, its camp, rewards,
NPCs, interfaces, persistence, and adjacent interactions to current OSRS
behaviour.

This plan targets the **post-9 October 2024** encounter: Warmth replaces direct
Hitpoints damage and successful rounds add searches to the Reward Cart rather
than issuing new supply-crate items. That is the correct target for this tree:
the revision-239 cache already contains `wint_warmth`, `wint_reward_pool`,
interface 396's Warmth bar, Brew'ma, the off-hand bruma torch, and all six
Reward Cart states. The existing server scripts predate those assets and are
only a partial prototype. [W1] [W4] [W29]

Two source kinds are kept separate:

- **[W*n*]** — the Old School RuneScape Wiki. Links are in §15.
- **[C]** — this tree's revision-239 cache or server source.

Where the Wiki does not publish a value, this plan says so and requires a
capture or another authoritative observation instead of silently inventing a
number.

---

## 1. Definition of complete

Wintertodt is complete only when all of the following work together on the
shared overworld map:

1. The world cycles atomically through a 60-second rest phase and an active
   fight; all players on the world see the same energy, brazier, pyromancer,
   storm, and countdown state. [W1] [W2]
2. Entering requires 50 Firemaking and the initial Ignisia introduction.
   Enter, leave, Peek, warm-clothing warning, unsafe death, logout relocation,
   and activity-item cleanup all match the Wiki. [W1] [W7] [W29]
3. All four lanes support chop, fletch, light, feed, break, extinguish, repair,
   pyromancer damage, incapacitation, healing, and energy drain, including
   correct contention when many players act on one object. [W1] [W2]
4. All five Wintertodt actions exist: standard cold, 3x3 snowfall, brazier
   shatter, brazier extinguish, and pyromancer hit. Telegraphs, safe areas,
   damage formulae, action interruption, and target restrictions are correct.
   [W1] [W2]
5. Warmth is a real 0–100% resource connected to food, rejuvenation potions,
   passive regeneration, warm clothing, Rapid Heal, Heal Other, Redemption,
   regenerative equipment, ring-of-life/Defence-cape escape, Phoenix necklace,
   death, hitsplats, and the HUD. [W1] [W3] [W29]
6. Points, activity XP, round-completion XP, kill count, lifetime score, best
   score, diary credit, Combat Achievements, collection log, and music unlocks
   are awarded exactly once and persist at the correct scope. [W1] [W28]
7. The Reward Cart stores up to 8,000 earned searches, rolls loot when searched
   using the player's then-current skills, supports Search/Check/Big-search,
   updates its visible fill state, and implements every unique and material
   reward in §9. [W4] [W5]
8. Ignisia's exchanges, legacy supply crates, extra supply crates, the
   interface-only pyromancer-set preview, the tome/pages, both bruma-torch
   hands, warm gloves, and the Phoenix pet integrate with the repository's
   general item, charge, storage, pet, and collection-log systems. [W18] [W19]
   [W20] [W21] [W22] [W23]
9. Every NPC and usable piece of scenery in §§7–8 is present with its correct
   operation, dialogue/transcript, quest-state morph, and score/travel/exchange
   branch. Decorative entities remain non-interactive.
10. Deterministic mechanics tests, multiplayer contention tests, persistence
    tests, and statistical loot tests in §13 pass.

Out of scope: implementing the whole of The Forsaken Tower, A Kingdom Divided,
the Lovakengj minecart network, banking, polls, costume-room storage, pets,
diaries, or Combat Achievements from scratch. Wintertodt must call those shared
systems and provide all of its own hooks; missing shared facilities are named
dependencies, not reasons to duplicate them locally.

---

## 2. Current tree audit

### 2.1 What already exists

The partial port is in
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_wintertodt/`.

| Area | Present now [C] | Required correction |
|---|---|---|
| Door | 50 Firemaking gate, Ignisia flag, enter/leave teleport | Add Peek, shared phase/player count, modern warning, HUD lifecycle, death/logout cleanup, and correct in-progress entry behaviour. |
| Supply crates | Hammer, knife, bronze axe, tinderbox, and unfinished-potion operations | Rename the concoction op to the current cache text if needed; preserve the one-copy-in-inventory check (including OSRS's drop-trick bypass) and 1/5/10 potion withdrawal. |
| Roots | Axe check and repeated chop | Replace the fixed 1/4 approximation with the axe/Woodcutting success curve; use the 3-tick skilling timer; choose the correct usable axe and animation; gate by active phase. [W2] [W10] |
| Fletching | Knife-on-root loop | Correct normal knife timing to 4 ticks and fletching-knife acceleration after the first action; allow movement; interrupt correctly; forbid it in the safe lobby. [W2] [W13] |
| Braziers | Light/feed/fix and basic XP/points | Use tinderbox **or either bruma torch in inventory/equipment**; require a healthy pyromancer to light; use four-tick light/repair semantics; remove the prototype's extra `Firemaking + 5` feed XP; make state changes shared and contention-safe. [W2] [W9] |
| Potions | Herb picking and manual combine | Remove the obsolete Farming XP; implement manual auto-create timing, Druidic Ritual gate, prison-only rule, four doses, Drink, Help/use-on-pyromancer, and Brew'ma bulk mixing. [W14] [W15] [W16] [W17] |
| Points | Temporary `%wint_points` | Add 75-point heals, reset conditions, HUD transmission, lifetime/high score, and round settlement. |
| Rewards | `::wintcrate` debug command issues legacy crates | Replace gameplay use with persistent Reward Cart searches. Keep a developer-only deterministic settlement command and legacy crate opener. |
| NPCs | A shortened Ignisia introduction | Port the full transcript and exchanges; implement every NPC in §7. |

The cache already supplies these important client/data contracts [C]:

- interface `wint_status` (396), sprites `wint_icons,0..3`, clientscript
  `wint_update` (1421), `wint_inside_event` (1433), `wint_outside_event`
  (1432), and the countdown scripts 2753–2757;
- `wint_warmth` (varbit 11434, 0–1000), `wint_reward_pool` (11435,
  13 bits), `wint_needs_warning` (11436), and
  `wint_transmit_respawndelay` (7980);
- `total_wintertodt_kills` (varp 1528);
- active/resting storm locations 29308/29309, all brazier/root/crate/attack
  locations, area spotanims 1310/1311, and sequences 7174/7321/7322/7323;
- four pyromancer spawns and fourteen invisible Snow NPC spawns in map square
  `m25_62`.

### 2.2 Structural gaps to solve first

The encounter is a **world-shared event**, not a per-player instance. A player
varp cannot own energy or lane state. Implement one controller for map square
25_62 (prefer a dedicated invisible controller NPC with `npc_var` state and a
single `ai_timer`; add a true map-event primitive only if the engine lacks a
safe persistent controller). The controller is the sole writer of phase,
energy, lane state, attack schedules, and settlement. Player scripts submit
validated actions to it.

Do not let four independent brazier scripts or every entered player start a
second encounter loop. Controller boot must be idempotent across map loading,
server startup, and hot reload.

The generated spawn files currently omit Brew'ma and Lassin. Add them through
the repository's non-generated/overlay spawn mechanism; do not hand-edit the
generated `m25_61.spawn` or `m25_62.spawn` files. [C]

---

## 3. Authoritative state model

### 3.1 Shared world state

| Field | Representation/invariant |
|---|---|
| `phase` | `RESTING` or `ACTIVE`; transitions occur once. |
| `energy` | Integer 0..3500 because clientscript 1421 interpolates 3500 to 100%. Start at 3500; zero ends the round. [C] |
| `return_tick` | Absolute map tick for the fixed 60-second/100-tick countdown. Transmit remaining seconds through the cache countdown contract. [W1] |
| four lanes | Lane identity NW, NE, SW, SE; each has brazier `UNLIT`, `LIT`, or `BROKEN`, pyromancer HP 0..14, incapacitated flag, next bolt tick, and stable NPC/loc coordinates. |
| attack clocks | Separate scheduled attempts for standard player cold, area snowfall, lane/brazier, and pyromancer actions. Only the controller advances them. |
| round flags | `any_all_broken`, `any_pyro_fell`, used for Combat Achievements; clear only at round start. |
| participant set | Players currently in the prison plus their current-round session token; used for HUD broadcast and settlement, never for ownership of world state. |

Energy semantics [W2]:

- each healthy pyromancer behind a lit brazier removes 1% energy every 14
  ticks; represent 1% as 35 units;
- relighting or reviving introduces a Wiki-described random resume delay of
  roughly 1–5 seconds before the next bolt;
- adding roots/kindling gives points and XP but **does not** accelerate energy
  drain;
- with all four braziers unlit/broken, energy regains 1% every 35 ticks after
  a random initial delay; any lit brazier stops regeneration;
- at zero, latch settlement, stop attacks/actions, morph the storm to
  quiescent, announce rest, settle each participant once, and start the
  100-tick return timer.

### 3.2 Per-player temporary state

- points for this round;
- Warmth in tenths of a percent, 0..1000;
- current round/session id and settled flag;
- standard-cold interruption immunity/cooldown (at least 10 ticks per [W2]);
- any action queue identifiers needed to interrupt feed/fletch without
  cancelling protected actions such as chopping;
- round-local CA observations if a task is participant-specific.

Points and the session token clear on death, logout, or voluntary leave before
settlement. Entering midway starts at zero points but joins the shared current
phase. Do not reset global state when one player enters or leaves. [W1]

### 3.3 Persistent player state

- total Wintertodt kills (`total_wintertodt_kills`);
- lifetime points and highest single-round score for Captain Kalt;
- Reward Cart searches owed, capped at 8,000 (`wint_reward_pool` must remain
  compatible with its 13-bit client varbit);
- total Reward Cart claims for collection-log display;
- collection-log bits for the ten entries in §9.2;
- Kourend & Kebos medium-diary completion hook;
- eight Wintertodt Combat Achievement hooks;
- first-conversation/unlock state and one-time post-Warmth warning state;
- Phoenix ownership and unlocked colour variants through the shared pet data.

Changing world, logging out, or restarting the server must not lose owed cart
searches or scoreboard values.

---

## 4. Round lifecycle and door rules

### 4.1 Resting phase

1. Show `wint_snow_storm_idle`, disable attacks and encounter skilling, set
   braziers to unlit and pyromancers healthy, and make them say “We can rest
   for a time.” [W17]
2. Run exactly 60 seconds between rounds. The HUD inside the prison shows the
   return countdown; Peek outside reports the same state. [W1] [W7]
3. Allow lobby crate/root/potion preparation and the level-60 northern Gap.
   Verify by capture which arena gathering actions remain usable while resting;
   do not infer that from strategy advice.
4. At zero, switch the storm to `wint_snow_storm`, set energy to 100%, clear
   world round flags and all present players' points, enable active actions,
   and push one complete HUD snapshot.

### 4.2 Active phase

The controller advances energy bolts, regeneration, attacks, and HUD snapshots.
It ends immediately and exactly once when energy reaches zero. Simultaneous
last-tick bolt, heal, light, and player-leave events must be ordered and tested
so rewards cannot duplicate.

### 4.3 Doors of Dinh

- First entry is unlocked through the complete Ignisia conversation; Firemaking
  50 is still checked on every entry. [W1] [W18]
- `Enter` warns when fewer than four warm items are equipped, initializes
  Warmth/HUD, and does not reset a running fight. Use the cache
  `wint_needs_warning` for the one-time historical rules warning if applicable.
- `Peek` reports current energy (or return countdown) and number of players in
  the prison. [W7]
- Leaving during a fight asks for confirmation, loses all points, clears all
  activity-only items, closes the HUD, and teleports south of the door.
- Death is unsafe: use ordinary gravestone/item-loss and Hardcore status rules,
  then clear points/activity items and close the HUD. Warmth reaching zero is
  the death cause. [W1]
- Logout inside loses points and, on next login, relocates a normal-world
  player outside the Doors (Deadman goes to the nearby bank). [W29]
- Block High Level Alchemy while the event is active with the OSRS message
  recorded on [W1].
- Preserve quest-owned interactions on the same scenery: Undor's Dinh's-hammer
  repair state in The Forsaken Tower and applying the shielding potion during
  A Kingdom Divided. Those handlers must coexist with Enter/Peek rather than
  being swallowed by the minigame dispatcher. [W7]

### 4.4 Transportation seams

Verify every route documented by [W1] reaches the camp and observes its shared
unlock rules: Games necklace (and Basic+ POH jewellery-box destination) after
the account has travelled to Great Kourend once, Northern Tundras through
Lassin/the Lovakengj Minecart Network, fairy ring CIS after a prior Kourend
visit, and Arceuus Home Teleport. The first-visit flag is a shared Kourend
dependency already named by the Construction plan; Wintertodt must consume the
same durable flag, not introduce a second one.

---

## 5. Warmth and attacks

### 5.1 Warmth resource

Use 1000 internal units = 100.0%. Clamp every mutation. The Warmth HUD is
visible in the lobby/prison and is the health-like bar for cold hits; do not
also show a normal Hitpoints bar for those hits. [W1] [C]

Restoration [W1] [W29]:

| Source | Effect |
|---|---|
| Food that heals at least 4 HP per bite | +35% Warmth per healing component. Multi-part hunter meat restores twice, for 70% total. Food still follows its ordinary consume and HP behaviour. |
| Rejuvenation potion dose | +30% Warmth and decrement to the next dose/empty vial. |
| Natural regeneration | +8% each minute, plus +1% per equipped warm item, capped at four items. |
| Hitpoints regeneration systems | Rapid Heal, Heal Other, Redemption, regen bracelet, Hitpoints cape, and max-cape equivalents must be translated through one shared HP-to-Warmth adapter. Capture exact rounding/cadence where the Wiki is silent. |
| Ring of life / Defence cape | Trigger below 10% Warmth and teleport using the normal effect. |
| Phoenix necklace | Trigger at or below/below 20% according to the shared item rule, consume the necklace, and restore Warmth rather than only HP. Capture exact restored percentage because the Wiki does not publish it. |

### 5.2 Damage formulae

Round down exactly as [W2] specifies, where `FM` is base Firemaking, `W` is
warm items capped at 4, and `B` is lit braziers capped at 3:

```text
standard = floor((16 - W - min(2*B, 6)) * 100 / FM)
brazier  = 2 * floor((10 - W) * 100 / FM)
area     = 3 * floor((10 - W) * 100 / FM)
```

These values are percentage points; subtract `value * 10` from the 0..1000
Warmth store. Test the boundary at Firemaking 50/99, warm items 0/4, and lit
braziers 0..4.

### 5.3 Five encounter actions

| Action | Target/telegraph/result | Interruptions and restrictions |
|---|---|---|
| Standard cold | Independently checks every player outside the safe area. Its probability is linear in remaining energy: an attempt succeeds when a random 0..max-energy roll is below current energy. | At most once per player per 10 ticks. Interrupt fletching and feeding, not root chopping. [W2] |
| 3x3 area snow | Select an eligible player, show heavy snow over a centred 3x3 warning, then damage every player still inside with `area`. Use locs 29324/29325 and spotanims 1310/1311. | Never centre on a player adjacent to an obstacle or where a full 3x3 cannot fit; a protected player can still be clipped by another player's target. [W2] |
| Brazier heavy | Heavy-snow warning over one lane, then morph its brazier to broken and deal `brazier` damage to adjacent players. | Eligible increasingly often as energy falls. Mark the all-four-broken CA failure if applicable. |
| Brazier light | Light-snow warning, then morph lit brazier to unlit; no player damage. | Eligible increasingly often as energy falls. |
| Pyromancer | Light-snow warning, then deal 7 of the pyromancer's 14 HP so two hits incapacitate her. | No player damage. At 0 morph 7371→7372, stop bolts, block relighting, announce distress, and set the no-falls CA failure. [W1] [W17] |

The Wiki states that delays are random and gives the energy-dependent acceptance
tests, but does **not** publish the base attempt intervals, warning delay, or
relative selection weight between lane actions. Those values belong in the
capture list in §12, not as arbitrary constants.

Safe area [W2]: none of the five actions may damage a player in the southern
lobby/safe polygon. Potion creation is allowed there; fletching is not. Encode
the actual polygon/blocked-target tiles from the map rather than a broad
coordinate guess.

---

## 6. Player activities, timings, XP, and points

| Activity | Requirements and timing | XP | Points | Important behaviour |
|---|---|---:|---:|---|
| Chop bruma roots | Usable axe and Woodcutting success roll every 3 ticks | `Woodcutting level * 0.3` | 0 | Add one bruma root on success; no Beaver roll; infernal axe uses no charges. Use best usable axe, including equipped. [W10] [W12] |
| Fletch root | Knife: 4 ticks each. Fletching knife: first action 4 ticks, later actions one tick faster while uninterrupted. | `Fletching level * 0.6` | 0 | Convert one-for-one; may run; standard cold visibly interrupts; forbidden in safe lobby. [W13] |
| Light brazier | Firemaking 50; tinderbox or either bruma torch in inventory/equipment; 4-tick action | `Firemaking level * 6` | 25 | Healthy pyromancer required. All players who began before state changes receive XP/points. [W2] [W9] |
| Feed root | Lit brazier; repeated action | `Firemaking level * 3` | 10 | Consume one root; standard cold interrupts; does not change energy. Mixed root/kindling priority requires capture. |
| Feed kindling | Lit brazier; repeated action | `Firemaking level * 3.8` | 25 | Consume one kindling; standard cold interrupts; does not change energy. Mixed root/kindling priority requires capture. |
| Repair brazier | Hammer or supported equipped hammer; 4-tick action | `Construction level * 4` only if player owns a POH | 25 | Broken→unlit. Multiple already-started players all receive credit. [W2] [W9] |
| Pick bruma herb | Inventory space; repeatable | 0 | 0 | No Farming XP after 9 Oct 2024. [W11] [W14] |
| Make potion manually | Druidic Ritual; one herb + one unfinished potion; first immediate/then auto-create cadence 1, 2, 3 ticks per [W15] | `Herblore level * 0.1` | 0 | Prison influence only; can move; output is four-dose potion. |
| Brew'ma bulk mix | Use herb or unfinished potion on NPC | 0 | 0 | Instantly convert `min(herbs, unfinished)` pairs; exact zero/one/many messages. [W16] |
| Heal pyromancer | Help or use potion; take a dose from the potion with fewest remaining doses | 0 | 75 | Fully restore from any missing HP; only one player wins a contested heal; no consume/points at full HP or during rest. [W15] [W17] |
| Jump northern Gap | Agility 60 | 18 Agility | 0 | Traverse between NW and NE with proper force-movement and orientation. [W27] |

All XP uses base/current level exactly as the corresponding Wiki formula says;
preserve tenths where the engine supports fractional XP, and verify its
fixed-point rounding and pyromancer-set 2.5% outfit hook.
The prototype's `scale` approximations and extra feed XP are not acceptance
criteria.

Round completion with at least 500 points grants `Firemaking level * 100` XP.
Below 500, award no completion XP/searches and show the Wiki's not-worthy
message. [W1]

---

## 7. NPC inventory

| NPC / cache record | ID | Required implementation | Wiki |
|---|---:|---|---|
| Pyromancer / `wint_wizard` | 7371 | Four fixed lane NPCs, 14 HP, channel bolts, context overheads, Help/use-item rules, damage and morph. | [W17] and [transcript](https://oldschool.runescape.wiki/w/Transcript:Pyromancer) |
| Incapacitated Pyromancer / `wint_wizard_down` | 7372 | Same lane identity; Help left-click; distress overheads; revive to 7371. | [W17] |
| Decorative sacred flame / `wint_fire` | 7373 | Non-interactive visual used only where the map/controller requires it. | [W1] |
| Ignisia / `wint_master_pyromancer` | 7374 | Full first/return dialogue, FM and Forsaken Tower variants, Talk-to, Exchange, environmental trap/mithril-seed responses. | [W18] and [transcript](https://oldschool.runescape.wiki/w/Transcript:Ignisia) |
| Esther / `wint_wcguild_rep` | 7376 | Full Bruma-tree/Xeric dialogue. | [page](https://oldschool.runescape.wiki/w/Esther), [transcript](https://oldschool.runescape.wiki/w/Transcript:Esther) |
| Captain Kalt / `wint_shay_guard` | 7377 | Full dialogue plus Check Scores: kills, lifetime score, highest score and correct never/few/many wording. | [W24] and [transcript](https://oldschool.runescape.wiki/w/Transcript:Captain_Kalt) |
| Ish the Navigator / `wint_pisc_navigator` | 7378 | Full Dark Altar/conspiracy dialogue. | [page](https://oldschool.runescape.wiki/w/Ish_the_Navigator), [transcript](https://oldschool.runescape.wiki/w/Transcript:Ish_the_Navigator) |
| Winter Soldier / `wint_soldier` | 7379 | All camp spawns and full dialogue. | [page](https://oldschool.runescape.wiki/w/Winter_Soldier), [transcript](https://oldschool.runescape.wiki/w/Transcript:Winter_Soldier) |
| Cat / `wint_troublecat` | 7380 | Normal/catspeak dialogue; with a catspeak amulet and tuna, offer give/keep and consume exactly one tuna on give. | [page](https://oldschool.runescape.wiki/w/Cat_(Wintertodt_Camp)), [transcript](https://oldschool.runescape.wiki/w/Transcript:Cat_(Wintertodt_Camp)) |
| Wintertoad / `wint_toad` | 7381 | Wandering, non-interactive pet of Kalt. | [page](https://oldschool.runescape.wiki/w/Wintertoad) |
| Invisible Snow / `wint_area_npc` | 7383 | Preserve all fourteen static map NPCs and examine; do not confuse these with dynamically telegraphed snowfall. | [page](https://oldschool.runescape.wiki/w/Snow_(Wintertodt)) |
| Undor / `wint_master_smith` | morph 8544/8545 | Full dialogue and `lovaquest` visual morph; integrate The Forsaken Tower branches rather than shadowing its quest state. | [page](https://oldschool.runescape.wiki/w/Undor), [transcript](https://oldschool.runescape.wiki/w/Transcript:Undor) |
| Ed / `wint_cluehunter` | morph 9019/9238 | Northern-Tundras neighbour: full clue-hunter dialogue and disappear/move after Song of the Elves. Include in area-completeness testing, not encounter logic. | [page](https://oldschool.runescape.wiki/w/Ed), [transcript](https://oldschool.runescape.wiki/w/Transcript:Ed) |
| Lassin / `lovakengj_minecart_northern_tundras` | 12706 | Talk-to and Travel through the shared minecart network, including Forsaken Tower/free-ride branches. Missing from generated spawn today. | [page](https://oldschool.runescape.wiki/w/Lassin), [transcript](https://oldschool.runescape.wiki/w/Transcript:Lassin) |
| Brew'ma / `wint_brewma` | 13793 | Talk, use herb, use unfinished potion, invalid-item branches, and bulk mixing. Missing from generated spawn today. | [W16] and [transcript](https://oldschool.runescape.wiki/w/Transcript:Brew%27ma) |

The Wintertodt itself is scenery (`wint_snow_storm`, 29308), not an attackable
NPC. Dinh is historical lore and has no encounter spawn. [W1] [W7]

---

## 8. Scenery and interaction inventory

| Scenery / cache symbol | ID(s) | Operations / role |
|---|---:|---|
| Howling/Quiescent Snow Storm / `wint_snow_storm[_idle]` | 29308/29309 | Controller-owned active/resting visual; examine only. [W1] |
| Bruma roots / `wint_roots` | 29311 | Chop at all four corners. [W10] |
| Brazier / broken / burning | 29312/29313/29314 (plus 31926) | Light, Fix, Feed; four lane coordinates; shared morphs. [W9] |
| Sprouting Roots / `wint_herb_roots` | 29315 | Pick at the original arena points and two 2024 lobby additions. [W11] |
| Tool/concoction crates | 29316–29320 | Take hammer, knife, bronze axe, tinderbox, or 1/5/10 unfinished potions. [W8] |
| Bank chest / `wint_bankchest` | 29321 | Bank/Collect through shared bank system; outside active game use. |
| Doors of Dinh / `wint_door` | 29322 | Enter and Peek; internal face also leaves with confirmation. [W7] |
| Snow warning/impact locs | 29324/29325 | Dynamic 3x3 and lane telegraphs/impacts; controller cleanup on all exits. |
| Gap / `wint_pillar_jump` | 29326 | Agility 60 shortcut, 18 XP. [W27] |
| Bank Deposit Box / `wint_deposit_box` | 29327 | Shared Deposit interface. |
| Bonfire / `wint_bonfire` | 29300 | Permanent non-interactive camp scenery; do not turn it into the encounter brazier. [page](https://oldschool.runescape.wiki/w/Bonfire) |
| Reward Cart master morph / `wint_reward_pile` | 55423 → 55411–55416 | Search, Check, Big-search; empty, 1–9, 10–24, 25–49, 50–99, and 100+ visual states. [W4] |
| Poll booth and camp bank/deposit | map/cache standard records | Route to their general systems; verify reachability and correct ops. [W6] |
| Ornamental anvil, tents, crates, walls, helix, floors | 29301–29310, 29722 and map decoration | Examine/decorative only unless their cache exposes an op; no invented Smithing/Crafting mechanic. |

Also wire both music regions: **The Doors of Dinh** in camp and **Ice and
Fire** in the prison. [W1]

---

## 9. Items and rewards

### 9.1 Encounter-only and enabling items

| Item | ID / cache record | Behaviour |
|---|---|---|
| Bruma root | 20695 `wint_bruma_root` | Nontradeable, nonbankable; Feed or knife/fletching-knife use; shatters on drop/cannot leave. [W12] |
| Bruma kindling | 20696 `wint_bruma_kindling` | Nontradeable, nonbankable; Feed; shatters/cannot leave. [W13] |
| Rejuvenation potion (unf) | 20697 `wint_vial` | Crate withdrawal, herb combine, Brew'ma use; nonbankable/cannot leave. [W15] |
| Bruma herb | 20698 `wint_herb` | Pick, potion combine, Brew'ma use; no Farming XP; nonbankable/cannot leave. [W14] |
| Rejuvenation potion (4/3/2/1) | 20699–20702 | Drink +30% Warmth or Help/use on pyromancer; consume lowest-dose potion first for healing; empty vial result; nonbankable/cannot leave. [W15] |
| Bronze through supported axes | standard axe category | Root success/animation uses best axe the player's levels allow. Infernal axe is warm and consumes no charges here. [W2] |
| Knife and fletching knife | standard items | Root→kindling from inventory; an equipped knife alone does not satisfy the interaction. Fletching knife has the Wiki timing acceleration. [W2] |
| Hammer and equipped Imcando variants | standard items | Repair. Both hand variants must satisfy tool lookup. |
| Tinderbox | standard item | Light brazier from inventory. Barbarian Firemaking is not a substitute. [W9] |
| Bruma torch / off-hand | 20720 / 29777 | Warm item, light source, brazier tinderbox substitute from inventory/equipment, reversible Swap without losing ownership/storage state. [W20] |

The activity cleanup routine must remove every bruma root/kindling/herb,
unfinished potion, all four potion doses, and any dynamic ground versions on
leave/death/logout. It must **not** delete ordinary tools or earned rewards.

### 9.2 Unique reward sequence and collection log

Each normal cart search rolls these in order until one succeeds, then falls
through to §9.3 [W4]:

| Order | Reward | Raw roll | Special rule |
|---:|---|---:|---|
| 1 | Phoenix (20693 `phoenixpet`) | 1/5,000 | Use shared pet delivery; at 200m Firemaking apply the Wiki's 15x pet-rate rule. Already-owned behaviour must match the shared pet system. [W22] |
| 2 | Dragon axe (6739 `dragon_axe`) | 1/10,000 | Ordinary tradeable item. |
| 3 | Tome of fire (empty) (20716 `tome_of_fire_uncharged`) | 1/1,000 | Add empty tome and collection entry. [W23] |
| 4 | Warm gloves (20712 `pyromancer_gloves`) | 1/150 | If bank+inventory already contain at least three, give one magic seed instead. Do not assume wardrobe storage counts for gloves without a capture. [W21] |
| 5 | Bruma torch (20720 `wint_torch`) | 1/150 | Count both hand variants across bank+inventory+magic wardrobe; at three, give 2–3 torstol seeds instead. [W20] |
| 6 | Pyromancer outfit piece (garb 20704, robe 20706, hood 20708, boots 20710) | 1/150 | Give the least-owned piece; tie order Garb, Hood, Robe, Boots. Count relevant owned storage exactly as OSRS does. [W4] [W19] |
| 7 | Burnt pages (20718 `wint_burnt_page`) | 1/45 | Give 7–29. [W4] [W23] |

Collection log has exactly ten entries: Phoenix, Tome of fire (empty), Burnt
page, Pyromancer garb, hood, robe, boots, Warm gloves, Bruma torch, and Dragon
axe. Increment total claims separately from unique collection bits. [W1]

The pyromancer outfit pieces apply their normal piece bonuses and the full-set
2.5% Firemaking XP bonus; Warm gloves visually match but do not count toward
the set. All five are warm clothing. [W19] [W21]

### 9.3 Complete material table

After all unique checks fail, choose one of the 25 equal-weight normal-table
slots: each of the six skill categories occupies 3/25, coins 5/25, saltpetre
1/25, and dynamite 1/25. Quantities and every possible item are [W4]:

| Category (skill sampled at search time) | Complete contents and quantity |
|---|---|
| Logs (Woodcutting) | Oak, Willow, Teak, Maple, Mahogany, Yew, Magic logs — each 10–20 noted. Minimum levels for Mahogany/Yew/Magic eligibility: 25/36/50. |
| Gems (Crafting) | Uncut sapphire 1–3, emerald 1–3, ruby 2–4, diamond 1–3, all noted and all eligible at level 1. |
| Ores (Mining) | Pure essence 20–70, Limestone 3–7, Silver ore 10–12, Iron ore 5–15, Coal 10–14, Gold ore 8–11, Mithril ore 3–5, Adamantite ore 2–3, Runite ore 1–2; noted. Minimum Mithril/Adamantite/Runite eligibility: 14/39/60. |
| Herbs (Herblore) | Grimy guam 3–6, marrentill 3–6, tarromin 3–6, harralander 3–6, ranarr 1–3, irit 3–5, avantoe 3–5, kwuarm 2–4, cadantine 2–4, lantadyme 2–4, dwarf weed 2–4, torstol 1–3; noted. Minimum Cadantine/Lantadyme/Dwarf weed/Torstol eligibility: 14/34/50/64. |
| Seeds (Farming) | Acorn 1; Willow, Maple, Banana tree, Teak, Mahogany, Yew 1–2; Ranarr, Tarromin, Harralander, Toadflax, Irit, Avantoe, Kwuarm, Snapdragon, Cadantine, Lantadyme, Dwarf weed 1–3; Watermelon and Snape grass 3–7; Spirit seed 1. Minimum Watermelon/Snape grass/Kwuarm/Snapdragon/Cadantine/Lantadyme/Dwarf weed/Spirit eligibility: 7/11/12/21/50/67/71/80. |
| Fish (Fishing) | Raw anchovies, trout, salmon, tuna, lobster, swordfish, shark — each 6–11 noted. Minimum Lobster/Swordfish/Shark eligibility: 15/34/43. |
| Other | Coins 2,000–4,999 (5/25); noted Saltpetre 3–5 (1/25); noted Dynamite 3–5 (1/25). |

Implement the level-dependent subtable with the Wiki calculator's published
data rather than threshold-only approximations. For each category, process its
rare-to-common rows in order. For a row with calculator coefficients `low` and
`high` at skill level `L`:

```text
threshold = clamp(floor(low + (high-low) * (L-1) / 98) + 1, 0, 256)
success   = random(256) < threshold
```

Stop at the first success; the final common row has 255→255 coefficients and
therefore always succeeds. Transcribe the item ordering and coefficients from
the source of [W5] into a typed server config/table and unit-test level 1 and
99. Note one source discrepancy to settle by live observation: the rendered
Reward Cart table says Limestone 3–7, while the Wiki calculator module currently
contains 2–7. Default to the player-facing table (3–7) until a capture proves
otherwise.

### 9.4 Reward count and cart operations

For points `P >= 500`, guaranteed searches are `floor(P / 500) + 1`; roll one
additional search with probability `(P mod 500) / 500`. Thus 500→2, 750→2 plus
50%, 1000→3, and 1500→4. Add atomically to the persistent cart, capped at
8,000. Points affect count, never material quality. Skills are sampled only
when a search is claimed. [W1] [W4]

- Search claims one reward every 3 ticks.
- Big-search attempts five sequential searches per click, respecting inventory
  space and preserving unclaimed searches if delivery stops.
- Check reports exact searches owed.
- Each successful claim decrements the pool, updates `wint_reward_pool`, cart
  morph, total-claimed counter, collection log, pet/rare messages, and inventory
  atomically. Never decrement on full inventory or failed pet delivery.

### 9.5 Ignisia exchange and legacy items

Ignisia accepts a Warm glove, either bruma-torch hand, any pyromancer piece,
or an empty Tome of fire for the player's choice of an extra supply crate or
burnt pages. Single glove/torch/outfit piece gives 50 pages; empty tome gives
250; a complete four-piece pyromancer outfit gives 250 pages or one five-roll
extra crate. Individual extra crates contain one roll. [W18] [W19]

`wint_pyro_set` (24554) is an unobtainable interface preview used by this
exchange, not a packable inventory reward. Never add, drop, bank, or unpack it.
[Pyromancer set](https://oldschool.runescape.wiki/w/Pyromancer_set)

Extra supply crates allow Dragon axe, then their own legacy material table, but
exclude Phoenix, Tome, gloves, torch, outfit, and burnt pages. Do **not** simply
call the modern 25-slot Reward Cart normal table: [W26] documents roughly 1/9
for each of the six skill subtables and separate Other rolls of coins
2,000–5,000 at 36/180, saltpetre 3–5 at 6/180, pure essence 20–70 at 1/180,
and dynamite 3–5 at 6/180. Preserve their one-roll or full-set five-roll count
on the item, or use distinct safe metadata if the existing item model cannot
encode it. The Wiki does not expose the exact six “about 1/9” numerators or
failed/excluded-slot reroll rule, so capture those before freezing this table.
[W26]

Discontinued supply crate 20703 remains openable for existing saves. It uses
its stored old roll count and the legacy documented unique order: Dragon axe
1/10,000, Phoenix 1/5,000, Tome 1/1,000, then gloves, torch, and outfit at
1/150 each. Its normal table is the extra-crate legacy table above plus Burnt
pages 7–29 at 4/180, including the legacy 2,000–5,000 coin upper bound and
standalone pure-essence roll. No new round creates one. Provide an explicit
one-time migration from old crates to Reward Cart searches only if save
compatibility requires it, and make it idempotent. [W25]

### 9.6 Reward-item follow-through

Receiving an item is not sufficient if its defining operations are inert:

- Pyromancer hood/garb/robe/boots give 0.4%/0.8%/0.6%/0.2% Firemaking XP;
  the complete set adds the set bonus for 2.5% total. All pieces use normal
  Wear/Drop and magic-wardrobe storage. [W19]
- Warm gloves use Wear/Drop, count as warm but not as outfit, store separately
  in the wardrobe, and participate in Ignisia/duplicate rules. [W21]
- Both bruma torches use Wield/Swap/Drop, are reversible one-for-one, work as a
  Wintertodt tinderbox from inventory or equipment, act as ordinary light
  sources, and share wardrobe/duplicate ownership semantics. [W20]
- Empty/charged Tome of fire supports Wield, Pages, Check, and charge removal;
  accepts only one page family at a time; holds up to 1,000 pages; each burnt
  or searing page adds 20 charges; and uses the shared modern fire-spell rules.
  Ignisia accepts only the empty tome. [W23]
- Phoenix uses ordinary pet delivery, Talk-to, Pick-up, death insurance, and
  Metamorphosis. Using 250 blue, green, purple, or white gnomish firelighters
  permanently unlocks the corresponding colour (item/NPC variants), after
  which Metamorphosis can switch freely. Preserve the bucket-of-water response
  through the shared pet/item-use dispatcher. [W22]
- The Burnt page stack is tradeable and charges the tome; any non-Wintertodt
  creation route belongs to its existing shared Runecraft content, not the
  Reward Cart roller. [W23]

---

## 10. Warm clothing — exhaustive acceptance list

Count equipped items only and cap the combat benefit at four. Represent this
as a config-driven item category/enum so variants can be audited and new cache
items do not require encounter-code edits. The complete current Wiki list is
below; slash variants are separate cache objects and must all resolve. [W3]

| Slot | Warm items |
|---|---|
| Head | Santa mask; Antisanta mask; Bunnyman mask; Larupia hat; Graahk headdress; Kyatt hat; Chicken head; Evil chicken head; Pyromancer hood; Santa hat; Black santa hat; Inverted santa hat; Festive elf hat; Festive games crown; Bearhead; Fire tiara; Elemental tiara; Lumberjack hat; Forestry hat; Snow goggles & hat; Snowglobe helmet; Firemaking hood; Fire max hood; Infernal max hood; Bomber cap; Cap and goggles; Bobble hat; Earmuffs; Wolf mask; Woolly hat; Jester hat; Tri-jester hat; Slayer helmet and Slayer helmet (i); Araxyte, Black, Green, Hydra, Purple, Red, Turquoise, Twisted, TzKal, TzTok, and Vampyric slayer helmets, each normal and imbued; Hooded slayer helmet and Hooded slayer helmet (i). |
| Neck | Jester scarf; Tri-jester scarf; Woolly scarf; Bobble scarf; Gnome scarf; Rainbow scarf; Festive scarf. |
| Hands | Santa gloves; Antisanta gloves; Bunny paws; Clue hunter gloves; Gloves of silence; Fremennik gloves; Warm gloves; Grey, Red, Yellow, Teal, and Purple gloves. |
| Cape | Firemaking cape and (t); Max cape; Fire cape; Fire max cape; Infernal cape; Infernal max cape; Obsidian cape and (r); Accumulator, Ardougne, Assembler, Mythical, Imbued Guthix, Imbued Saradomin, Imbued Zamorak, Guthix, Saradomin, and Zamorak max capes; Wolf cloak; Rainbow cape; Clue hunter cloak. |
| Weapon | Staff of fire; Fire, Lava, Steam, and Smoke battlestaves; Mystic fire, lava, steam, and smoke staves; Twinflame staff; all Infernal axe states; Infernal pickaxe; Infernal harpoon; Volcanic abyssal whip; Ale of the gods; Bruma torch; Dragon candle dagger. |
| Shield | Tome of fire (empty/charged states as applicable); Bruma torch (off-hand); Lit bug lantern. |
| Body | Santa jacket; Antisanta jacket; Bunny top; Clue hunter garb; Polar, Wood, Jungle, and Desert camo tops; Larupia, Graahk, and Kyatt tops; Bomber jacket; Yak-hide armour top; Pyromancer garb; Chicken wings; Evil chicken wings; all Ugly halloween jumper colours; Christmas jumper; Oldschool jumper; Rainbow jumper; Icy jumper. |
| Legs | Santa pantaloons; Antisanta pantaloons; Bunny legs; Clue hunter trousers; Polar, Wood, Jungle, and Desert camo legs; Larupia, Graahk, and Kyatt legs; Yak-hide armour legs; Chicken legs; Evil chicken legs; Pyromancer robe. |
| Ring | Ring of the elements. |
| Feet | Santa boots; Antisanta boots; Bunny feet; Clue hunter boots; Pyromancer boots; Chicken feet; Evil chicken feet; Festive elf slippers; Mole slippers; Holy moleys; Bear feet; Demon feet; Frog slippers; Bob the cat slippers; Jad slippers; Cow slippers; Brutus slippers. |

Add negative regression cases because visually plausible items are deliberately
not warm [W3]: Lumberjack top/legs/boots, Spotted and Spottier capes, Obsidian
armour, Bunny ears, Strung rabbit foot, Wise old man's santa hat, fire/elemental
Hat of the eye, Abyssal lantern, Burning amulet, all three Trailblazer Reloaded
relic-hunter sets, Trailblazer reloaded torch, Blazing blowpipe, Scorching bow,
Burning claws, Emberlight, Helm of Raedwald, Beer belly sweater, Jad jumper,
Devil's element, and Searing boots.

Before implementation, resolve every name above to revision-239 object symbols
and emit a checked-in audit table or compiler-validated enum. Items absent from
this cache are documented as post-revision exclusions rather than mapped to a
lookalike.

---

## 11. HUD, messaging, persistence, and integrations

### 11.1 HUD protocol

Open interface 396 on entering the lobby/prison and close it on exit/death.
Drive clientscript 1421 with:

1. player points;
2. shared energy 0..3500;
3. four pyromancer healthy/incapacitated flags;
4. four brazier states (`0` unlit, `1` lit, `2` broken).

`wint_warmth` separately drives the 0..1000 Warmth bar. Use scripts 1432/1433
to hide/show the appropriate outside/inside layers and scripts 2753–2757 for
the rest countdown. Validate the existing timer's varbit-transmit dependency;
the decompiled clientscript listens to `var1142` while the named cache varbit is
7980, so a trace is required before wiring it. [C]

Push one snapshot on entry and on every state mutation. Coalesce repeated
per-tick broadcasts; do not let stale lane icons survive a new round.

### 11.2 Score and meta progression

- Qualifying settlement (`points >= 500`) increments kill count once, lifetime
  points by the exact round score, highest score by `max`, completion XP, cart
  searches, diary hook, and CA hooks.
- Captain Kalt's estimated lifetime rolls formula is `kills + lifetime/500`;
  his interface/dialogue still displays the authoritative raw three values.
  [W24]
- Medium Kourend & Kebos diary credit requires a successful 500+ round. [W1]
- Unlock camp/prison music on region entry.

Combat Achievement hooks [W28]:

| Task | Trigger |
|---|---|
| Wintertodt Novice | 5 successful subdues. |
| Wintertodt Champion | 10 successful subdues. |
| Mummy! | Heal an incapacitated pyromancer. |
| Handyman | Repair a brazier broken by Wintertodt. |
| Can We Fix It? | Subdue without all four braziers ever being broken simultaneously. |
| Leaving No One Behind | Subdue without any pyromancer becoming incapacitated. |
| Cosy | Subdue while four warm items are equipped at settlement. |
| Why Fletch? | Subdue after personally earning at least 3,000 points. |

### 11.3 General-system hooks

Use shared APIs for bank/deposit, item charges, POH/costume storage, catspeak,
quests, prayers, food, teleports, death/graves, pets, collection log, diaries,
achievements, and music. Add the smallest generic hook where one is missing
(for example, a health-restoration observer that can restore Warmth while in
Wintertodt); do not scatter Wintertodt conditionals through every food item.

---

## 12. Wiki-unspecified facts requiring capture

These are not permission to choose convenient numbers:

1. Base attempt intervals, warning duration, and relative weighting for the
   five random Wintertodt actions. The Wiki gives the energy-dependent
   acceptance rule but not the clocks. [W1] [W2]
2. Exact axe/Woodcutting success formula for bruma roots. The Wiki establishes
   a 3-tick roll, axe dependence, and that level 46 with a steel axe reaches
   maximum speed, but not the formula. [W2] [W10]
3. Exact Warmth conversion/rounding for Rapid Heal, Heal Other, Redemption,
   regen bracelet, Hitpoints cape/max cape, and the Phoenix necklace. [W1]
4. Exact random 1–5 second bolt-resume distribution after light/heal and exact
   initial energy-regeneration offset when all fires go out. [W2]
5. Whether resting-phase arena roots/actions are enabled, and precise action
   timings for feeding, picking herbs, Help, and Brew'ma where no Wiki tick
   count is stated; also which item is consumed first from an inventory that
   contains both roots and kindling.
6. Exact full-inventory delivery behaviour for Search and Big-search, pet
   reroll/delivery when already owned, and legacy crate metadata in live saves.
7. Reward calculator discrepancy for Limestone quantity (rendered table 3–7,
   module 2–7).
8. Sound IDs, exact overhead cadence, projectile timing, and the client
   countdown transmit varbit alias.
9. Exact pyromancer-hit value/distribution. Fourteen HP and incapacitation
   after two hits strongly imply 7 per hit, but the Wiki does not state “7”.
10. Exact extra-supply-crate category numerators/rerolls; its Wiki page exposes
    the four Other weights but describes each skill category only as “about
    1/9”.

Capture each with a tick-stamped OSRS video/client trace or a documented
upstream content source, record the evidence beside the resulting constant,
and add a deterministic regression test.

---

## 13. Implementation phases and verification gates

### Phase 0 — data and engine readiness

- Resolve every §10 warm item to cache symbols; add the config category and
  negative tests.
- Add persistent vars for lifetime score, best score, cart pool/claims, and
  necessary warning/session state without colliding with cache varbits.
- Prove one world-shared controller can survive zero/one/many players and can
  enumerate participants and mutate the four fixed locs/NPCs.
- Add overlay spawns for Brew'ma/Lassin if still absent.

Gate: script compilation and cache packing are clean; controller boot is
idempotent and two players observe one shared test counter.

### Phase 1 — lifecycle, doors, HUD

- Implement rest/active state machine, 3500 energy, 100-tick restart,
  storm morph, participant sessions, Enter/Peek/Leave, login relocation,
  cleanup, HUD snapshots/countdown, and full Ignisia intro.

Gate: two clients can join at different times, Peek reports them, neither
resets the fight, and one leaving does not affect the other.

### Phase 2 — lanes and skilling

- Implement exact roots/fletching, tools, sprouting roots, potion creation,
  Brew'ma, all brazier states, pyromancer HP/help, bolts, energy regeneration,
  points, XP, and Gap.

Gate: deterministic solo and two-player traces cover every state transition,
including simultaneous light/repair/heal credit and only-one-player heal
contention.

### Phase 3 — attacks and Warmth

- Implement safe polygon, formulas, all five attacks/telegraphs, interruption
  rules, passive restoration and every recovery/escape integration.

Gate: formula matrix tests pass; 3x3 clipping and obstacle exclusions pass;
chopping survives standard cold while feed/fletch stop; zero Warmth follows
unsafe-death rules.

### Phase 4 — settlement and Reward Cart

- Implement qualifying/nonqualifying settlement, scoreboard persistence,
  cart count/cap/morph/operations, unique cascade, all material subtables,
  inventory safety, collection log, pet, diary, CA, and music hooks.

Gate: fixed RNG vectors produce every unique/material; 100,000+ simulated
searches at levels 1/50/99 match expected category/unique probabilities within
statistical tolerance; skills are sampled at claim time; restart preserves all
owed searches.

### Phase 5 — exchanges, legacy, and camp completeness

- Implement Ignisia's transaction matrix and interface-only set preview, extra
  and discontinued crate openers, torch swapping, all NPC
  transcripts/scores/travel, and camp bank/deposit/poll/music interactions.

Gate: transaction table tests prove exact inputs/outputs and cancellation has no
side effects; all dialogue branches are reachable under their quest/item states.

### Phase 6 — parity soak

- Run solo, duo, and mass-client simulations through repeated rounds.
- Test reconnects and server restart during both phases; repeated final-tick
  actions; cart cap 7999→8000; full inventory; pet following/inventory; POH/no
  POH repair; all warm-count 0..5; FM 50/99; 200m Firemaking pet modifier.
- Capture client screenshots for resting/active HUD, each brazier icon, downed
  pyromancer, Warmth hitsplat, countdown, and every Reward Cart fill morph.
- Remove gameplay access to `::wintcrate`; retain named deterministic admin
  controls for phase, energy, lane, attack, points, Warmth, and RNG only in
  debug builds.

Final gate: no deferred “core” mechanics remain in the Kronos queue; update its
Wintertodt row from partial/prototype language to link this document and list
only genuinely external shared-system dependencies.

---

## 14. Minimum deterministic test matrix

| Test | Expected result |
|---|---|
| Enter at FM 49/50, Ignisia unseen/seen | Only the valid seen+50 case enters; Peek always remains read-only. |
| Join active fight with another player at 42% | Shared energy remains 42%; joiner starts 0 points and receives current HUD. |
| All four healthy/lit for 14 ticks | Each lane removes exactly 35 energy once; feeding changes no energy. |
| All four unlit for 70 ticks after fixed zero offset | Energy restores exactly twice by 35, capped at 3500. |
| Pyromancer takes two 7 hits | Morphs to incapacitated, bolt stops, brazier cannot relight; one potion dose restores to 14. |
| Two players finish one repair/light | Both pre-started actions get XP/points; loc morph occurs once. |
| Two players Help same pyromancer | Exactly one dose consumed and one 75-point award. |
| Standard cold during chop/feed/fletch | Chop continues; feed/fletch interrupt; repeat cold blocked for 10 ticks. |
| Area target against wall and open floor | Wall target rejected; open 3x3 telegraphs; only players still in area are hit. |
| Warmth 1 hit to zero with/without escape jewellery | Escape effect wins at its threshold when available; otherwise ordinary unsafe death and point loss. |
| Points 499/500/750/1000/1500 | 0 / 2 / 2+50% / 3 / 4 searches and completion XP only for 500+. |
| Cart at 7999 gains 4 | Stores 8000, never wraps 13-bit state or silently creates legacy crates. |
| Claim at skill 1 then raise to 99 | First and second search use the respective current skill subtables. |
| Three gloves/torches across all counted stores | Replacement Magic seed / 2–3 Torstol seeds; hand variant is not double-counted. |
| Leave, logout, or die with every activity item | Points and activity items clear; tools/rewards persist; HUD closes; owed cart searches persist. |
| Round ending with simultaneous actions | One settlement, one kill increment, one score update, one restart timer. |

---

## 15. Wiki references

All links were reviewed 17 August 2026.

- **[W1]** [Wintertodt](https://oldschool.runescape.wiki/w/Wintertodt) — requirements, lifecycle, attacks, points, XP, HUD, rewards, cleanup, diary, collection log, music.
- **[W2]** [Wintertodt/Strategies](https://oldschool.runescape.wiki/w/Wintertodt/Strategies) — formulas, attack selection, safe area, interruptions, energy drain/regeneration, tick timings and contention.
- **[W3]** [Warmth / warm clothing](https://oldschool.runescape.wiki/w/Warmth#Warm_clothing) — exhaustive warm and non-warm equipment lists.
- **[W4]** [Reward Cart](https://oldschool.runescape.wiki/w/Reward_Cart) — cart operations/cap, reward count, unique sequence, complete material drops and quantities.
- **[W5]** [Module:Wintertodt supply crate](https://oldschool.runescape.wiki/w/Module:Wintertodt_supply_crate) — Wiki calculator's level-interpolation coefficients and ordered subtables.
- **[W6]** [Wintertodt Camp](https://oldschool.runescape.wiki/w/Wintertodt_Camp) — camp facilities and resident roster.
- **[W7]** [Doors of Dinh](https://oldschool.runescape.wiki/w/Doors_of_Dinh) — entry requirement and Peek output.
- **[W8]** [Crate (Wintertodt)](https://oldschool.runescape.wiki/w/Crate_(Wintertodt)) — five supply crates and withdrawal rules.
- **[W9]** [Brazier](https://oldschool.runescape.wiki/w/Brazier) — states, tools, POH requirement and XP.
- **[W10]** [Bruma roots](https://oldschool.runescape.wiki/w/Bruma_roots) — four locations, 3-tick rolls, XP and no Beaver.
- **[W11]** [Sprouting Roots](https://oldschool.runescape.wiki/w/Sprouting_Roots) — herb source and 2024 lobby additions.
- **[W12]** [Bruma root](https://oldschool.runescape.wiki/w/Bruma_root) — item restrictions, feed/fletch behaviour and XP.
- **[W13]** [Bruma kindling](https://oldschool.runescape.wiki/w/Bruma_kindling) — conversion, timings and XP.
- **[W14]** [Bruma herb](https://oldschool.runescape.wiki/w/Bruma_herb) — potion use and removal of Farming XP.
- **[W15]** [Rejuvenation potion](https://oldschool.runescape.wiki/w/Rejuvenation_potion) and [unfinished potion](https://oldschool.runescape.wiki/w/Rejuvenation_potion_(unf)) — doses, creation, Warmth and pyromancer healing.
- **[W16]** [Brew'ma](https://oldschool.runescape.wiki/w/Brew%27ma) — bulk potion service.
- **[W17]** [Pyromancer](https://oldschool.runescape.wiki/w/Pyromancer) — HP, channel/incapacitation, Help and heal restrictions.
- **[W18]** [Ignisia](https://oldschool.runescape.wiki/w/Ignisia) — introduction and unique exchanges.
- **[W19]** [Pyromancer outfit](https://oldschool.runescape.wiki/w/Pyromancer_outfit) — set bonus, storage and full-set exchange.
- **[W20]** [Bruma torch](https://oldschool.runescape.wiki/w/Bruma_torch) and [off-hand variant](https://oldschool.runescape.wiki/w/Bruma_torch_(off-hand)) — lighting, warmth, swapping, storage and duplicate replacement.
- **[W21]** [Warm gloves](https://oldschool.runescape.wiki/w/Warm_gloves) — warmth, storage, exchanges and duplicate replacement.
- **[W22]** [Phoenix](https://oldschool.runescape.wiki/w/Phoenix) — delivery, 200m modifier, recolours and pet behaviour.
- **[W23]** [Tome of fire](https://oldschool.runescape.wiki/w/Tome_of_fire) and [Burnt page](https://oldschool.runescape.wiki/w/Burnt_page) — reward item charging and exchange outputs.
- **[W24]** [Captain Kalt](https://oldschool.runescape.wiki/w/Captain_Kalt) — kill/lifetime/high-score reporting.
- **[W25]** [Supply crate (discontinued)](https://oldschool.runescape.wiki/w/Supply_crate_(discontinued)) — legacy opening and stored-roll behaviour.
- **[W26]** [Extra supply crate](https://oldschool.runescape.wiki/w/Extra_supply_crate) — eligible exchanges, excluded uniques and one/five-roll forms.
- **[W27]** [Gap (Wintertodt)](https://oldschool.runescape.wiki/w/Gap_(Wintertodt)) — Agility 60 and 18 XP.
- **[W28]** [Wintertodt Combat Achievements](https://oldschool.runescape.wiki/w/Combat_Achievements/Tasks_by_boss#Wintertodt) — all eight tasks.
- **[W29]** [Wintertodt Updates & Varlamore Drop Rates](https://oldschool.runescape.wiki/w/Update:Wintertodt_Updates_%26_Varlamore_Drop_Rates) — October 2024 Warmth, Brew'ma/roots, interruption, logout and Reward Cart changes.
