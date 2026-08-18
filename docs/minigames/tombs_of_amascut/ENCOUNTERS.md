# Tombs of Amascut — every encounter, npc, mechanic, sound and track

The research log. One section per room, in the order the raid presents them.
Everything here is sourced; nothing here is a design decision. The plan
([`TOMBS_OF_AMASCUT_PLAN.md`](TOMBS_OF_AMASCUT_PLAN.md)) decides what to build
from it, and [`ASSET_INDEX.md`](ASSET_INDEX.md) is the flat id catalogue.

**Provenance tags**, on every number that has one:

| Tag | Means |
|---|---|
| `[cache]` | the rev239 cache states it — `configs/all.npc`, `all.struct`, `all.loc`, a map square |
| `[wiki]` | the pinned OSRS Wiki revision in [`SOURCES.md`](SOURCES.md) states it |
| `[nr]` | RSPS-NEAR-REALITY encodes it. A reimplementation, **not** authoritative — good for ids the wiki never states (projectiles, sounds, animations), suspect for balance |
| `[toa-plugin]` | the duckblade Tombs of Amascut RuneLite plugin encodes it. A client-side observer, so its constants are things the real server does |
| `[Mn]` | **not measured.** A disclosed gap; `Mn` is its task in the plan's §12 |

---

## 0. Shape of the raid

Eight players maximum, four paths in any order, then the Wardens.
[wiki] [nr]

```
                 Nexus  (m55_80, region 14160)
                   |
   +--------+------+------+--------+
   |        |             |        |
 Crondis  Scabaras       Het    Apmeken        <- challenge ("puzzle") rooms
   |        |             |        |
 Zebak    Kephri        Akkha    Ba-Ba         <- path bosses
   +--------+------+------+--------+
                   |
          Wardens P1  (m59_80)  ->  Wardens P2/P3  (m61_80)
                   |
              Tomb / vault  (m57_80, region 14672)
```

**Eleven rooms, twelve map squares, and every one of them ships in the rev239
cache.** [cache] The reference implementation's 8-tile chunk origins and the
RuneLite plugin's region ids are two independent derivations of the same
geometry, and they agree:

| Room | Region | Square | Chunk origin `[nr]` | Spawn tile `[nr]` |
|---|---:|---|---|---|
| Nexus / main hall | 14160 | `m55_80` | 440, 640 → (3520, 5120) | 3550, 5161 |
| Crondis challenge | 15698 | `m61_82` | 488, 656 → (3904, 5248) | 3954, 5279 |
| Zebak | 15700 | `m61_84` | 488, 672 → (3904, 5376) | 3958, 5407 |
| Scabaras challenge | 14162 | `m55_82` | 440, 656 → (3520, 5248) | 3522, 5279 |
| Kephri | 14164 | `m55_84` | 440, 672 → (3520, 5376) | 3535, 5408 |
| Het challenge | 14674 | `m57_82` | 456, 656 → (3648, 5248) | 3698, 5279 |
| Akkha | 14676 | `m57_84` | 456, 672 → (3648, 5376) | 3698, 5406, **plane 1** |
| Apmeken challenge | 15186 | `m59_82` | 472, 656 → (3776, 5248) | 3792, 5279 |
| Ba-Ba | 15188 | `m59_84` | 472, 672 → (3776, 5376) | 3790, 5407 |
| Wardens P1 | 15184 | `m59_80` | 472, 640 → (3776, 5120) | 3807, 5176, **plane 1** |
| Wardens P2/P3 | 15696 | `m61_80` | 488, 640 → (3904, 5120) | 3935, 5168, **plane 1** |
| Vault / reward | 14672 | `m57_80` | 456, 640 → (3648, 5120) | 3680, 5170 |

Two consequences worth stating up front: each room is **a whole map square**, so
instancing is one square per room rather than a sub-rectangle; and three of them
sit on **plane 1**, so any "which room am I in" test that keys on x/z alone is
wrong.

### Difficulty: raid level, invocations, path levels

* **Raid level** is the sum of the active invocations' `param_1162` values.
  Modes: Entry **0–149**, Normal **150–299**, Expert **300+** (cap 600).
  [wiki] [toa-plugin]
* Every **5 raid levels** adds +2% additively to every raid npc's hitpoints,
  defence, accuracy and damage. Damage caps at **+150%**; the other three do not.
  [wiki]
* **Path level** 1 adds +8% to hitpoints and damage; each further level +5%, to a
  cap of 6 levels. Every *two* path levels also changes that path's boss
  mechanics, capping at four. [wiki]
* **Team scaling:** the 2nd and 3rd member each add **90%** of a boss's base
  hitpoints; the 4th and beyond each add **60%**. [wiki]
  *(The reference implementation uses 90% for everyone — see
  [`SOURCES.md`](SOURCES.md) §3. Follow the wiki.)*

The reference's scaling arithmetic, which matches the wiki everywhere except the
team term: [nr]

```
levelFactor  = pathLevel > 0 ? 0.08 + (pathLevel - 1) * 0.05 : 0
raidFactor   = 1 + (raidLevel / 5) * 0.02
damageFactor = min(2.5, raidFactor + levelFactor)
accuracy     = raidFactor
defence     *= raidFactor
hitpoints    = base * raidFactor * teamFactor * (1 + levelFactor)
```

### The 46 invocations are cache data, not content

`configs/all.struct` carries the entire table. Struct 417 is `Try Again`; the
last is 2971 `Insanity`, plus two Leagues-only entries. Per struct: [cache]

| Param | Meaning |
|---:|---|
| 1159 | index, 1–46 — also the bit position in the party's invocation state |
| 1160 | name |
| 1161 | category, 3–15 |
| 1162 | raid-level modifier |
| 1299 | numeric argument (attempt count, time limit in ticks, supply %) |
| 1346 | prerequisite struct, e.g. 2934 `Overclocked 2` requires 2933 |
| 1297 / 1298 | inactive / active sprite |
| 1262 | the in-game description |

Full table: [`sources/cache_invocations_toa.tsv`](sources/cache_invocations_toa.tsv).
The fourteen categories, by their `param_1161` value:

| Cat | Group | Invocations |
|---:|---|---|
| 3 | Attempts | Try Again +5, Persistence +10, Softcore Run +15, Hardcore Run +25 |
| 4 | Time Limit | Walk +10 / Jog +15 / Run +20 / Sprint for It +25 (args 4000/3500/3000/2500 ticks) |
| 5 | Helpful Spirit | Need Some Help? +15 (66%), Need Less Help? +25 (33%), No Help Needed +40 (10%) |
| 6 | Path Level | Pathseeker +15 (1), Pathfinder +40 (2), Pathmaster +50 (3) |
| 7 | Prayer | Quiet Prayers +20 (protection prayers 10% less effective), Deadly Prayers +20 |
| 8 | Restoration | On a Diet +15, Dehydration +30, Overly Draining +15 |
| 9 | Paths | Walk the Path +50 |
| 10 | Akkha | Double Trouble +20, Keep Back +10, Stay Vigilant +15, Feeling Special? +20 |
| 11 | Kephri | Lively Larvae +5, More Overlords +15, Blowing Mud +10, Medic! +15, Aerial Assault +10 |
| 12 | Zebak | Not Just a Head +15, Arterial Spray +10*, Blood Thinners +5*, Upset Stomach +15 |
| 13 | Ba-Ba | Mind the Gap! +10, Gotta Have Faith +10, Jungle Japes +5, Shaking Things Up +10, Boulderdash +10 |
| 14 | Wardens | Ancient Haste +10, Acceleration +10, Penetration +10, Overclocked +10, Overclocked 2 +10*, Insanity +50* |
| 15 | Leagues | Blazing Tombs I / II, +200 each — out of scope |

`*` has a prerequisite in `param_1346`.

Maximum reachable raid level from categories 3–14 — one invocation from each of
the four exclusive categories (Attempts, Time Limit, Helpful Spirit, Path Level)
at 25 + 25 + 40 + 50 = 140, plus all 460 from the rest — is exactly **600**,
which is the documented ceiling. That the cache's own numbers reproduce the
ceiling to the point is the check that this table was read correctly. [cache] [wiki]

### Points and rewards

* Start at **5,000** points; the 5,000 is subtracted again at the end. [wiki]
* Room points cap at **20,000**; total at **64,000**. [toa-plugin]
* 1 damage = 1 point, times an npc multiplier: [wiki] [toa-plugin]

  | Npc | × |
  |---|---:|
  | Warden's Core, Energy Siphon, Ba-Ba's boulders | 0 |
  | Apmeken path baboons | 1.2 |
  | Ba-Ba | 2.0 |
  | Zebak | 1.5 |
  | Kephri's scarabs (soldier / spitting / arcane) | 0.5 |
  | Het's Seal (weakened) | 2.5 |
  | Obelisk | 1.5 |
  | Wardens P2 | 2.0 (capped at **3 downs**) |
  | Wardens P3 | 2.5 |
  | everything else | 1.0 |

* Puzzle-room completion awards a flat amount — the plugin's estimates are
  Scabaras 300, Crondis 400, Apmeken 450, Het and Wardens 300 shared. `[M1]`
* **MVP** in a room: `+300 × teamSize` to total. [wiki]
* Death: lose `max(20% of total, 1000)`, floored at 0. [wiki] [toa-plugin]
* Unique chance: 1% per `10500 - 20 × RL` points, capped at **55%**, where [wiki]

  ```
  RL = RaidLevel                             if RaidLevel <= 310
     = 310 + (RaidLevel - 310) / 3           if 310 < RaidLevel <= 430
     = 350 + (RaidLevel - 430) / 6           if RaidLevel > 430
  ```

* Under **1,500** points the chest gives fossilised dung and nothing else. [wiki]

---

## 1. The Nexus (lobby, region 14160)

Not a fight. What it owes: [wiki]

* Path selection — the **leader must enter first**; others follow.
* Poison and venom cleared after every challenge. Run energy refilled on
  entering a boss chamber. Health, prayer, run and special all refilled after a
  boss, **including after a wipe**.
* After **two paths**, the **Helpful Spirit** (`toa_midraidloot_trader`, npc
  **11694**) appears with three bundles and a deposit pot.

Bundles, and the eleven raid consumables: [wiki]

| Bundle | Contents |
|---|---|
| Life | nectar ×5, tears of elidinis ×5, silk dressing 1–3, blessed crystal scarab 1–3, ambrosia 1–2. `On a Diet` removes the dressings |
| Chaos | nectar 1–8, tears 0–6, smelling salts 0–2, ambrosia 0–1, liquid adrenaline 0–2 |
| Power | smelling salts ×2, liquid adrenaline ×1 |

| Item | Obj id `[cache]` | Effect `[wiki]` |
|---|---:|---|
| Honey locust | 27351 | heals 20 (overheals) + one prayer-potion dose. Auto-given on a wipe unless `On a Diet` |
| Ambrosia (2) | 27323/27325 | full HP + prayer + run + antidote++; then overheals `floor(base/4 + 2)` HP and `floor(base/5 + 5)` prayer |
| Blessed crystal scarab (2) | 27335/27337 | 8 prayer every 4 ticks, 9 times = 72 over 40 ticks |
| Liquid adrenaline (2) | 27339/27341 | halves special-attack cost for 150 s |
| Nectar (4) | 27315–27321 | heals `floor(base × 15/100) + 3`, drains combat stats `floor(cur/20 + 5)` |
| Silk dressing (2) | 27327–27333 (heal-over-time pair) | 5 HP every 5 ticks, 20 times = 100 over 100 ticks |
| Smelling salts (2) | 27343/27345 | +25% run; boosts all combat by `floor(base × 16/100) + 11`, refreshed every 15 s for 8 min |
| Tears of elidinis (4) | — | stats `floor(base/4 + 3)`, prayer `floor(base × 25/100) + 10`; 3×3 party effect, receivers get `floor(base × 10/100) + 10` |

Supplies arrive as a **Supplies Bag** (obj 27314, inv 810, size 28) with three
7-slot bundle invs (807/808/809) behind interface 778. [cache]

Deaths: dangerous. A disconnect outside the Nexus counts as a death. Hardcore
ironmen lose the status. [wiki]

---

## 2. Path of Crondis — Test of Resourcefulness (region 15698)

Music: **Test of Resourcefulness**, archive 733, unlock varp 23 bit 2. [cache]

Grow the **Palm of Resourcefulness** (npc **11700–11704**, five growth stages,
size 5) by carrying water to it. **175 units solo, +125 per extra player.**
[wiki] [cache]

* Water containers (obj **27295**) are picked up off the floor and filled at the
  **waterfalls** (`toa_crondis_waterfall_*`).
* Two trap families guard the run: the **side statues** fire retracting spears
  (`crondis_spear_trap_spear*` seqs), and the **facing statue** discharges
  poison clouds down the corridor (`crondis_poison_tile*` locs, ten
  `crondis_poison_tile_activate_*` seqs). Only the **dark** cloud damages; the
  light one is the telegraph. [wiki]
* A hit from either **halves the water in the container** and drains Defence 6,
  Agility 3 and 2 prayer points. [wiki]
* Waterfalls deplete and refill; refill rate scales with team size. [wiki]
* **Crocodiles** (`toa_crondis_crocodile`, npc **11705**, 30 HP, attack rate 7,
  size 2) spawn from the corners. Aggression priority: **watered tree > player
  carrying water > player without**. They do not scale with raid level, always
  max **18**, +3 per trap/acid hit taken by that player to a cap of **36**.
  Protect from Melee costs 12 prayer and still takes 33%, and still halves the
  water. First crocodile reaches the palm about **40 s** after the challenge
  starts. [wiki]
* Solo no-damage clears exist at 34.8 s and 32.4 s; the traps are on a fixed
  cycle, and using the container *on* the palm/waterfall saves 1 tick per
  interaction (4 ticks total). [wiki]

`Upset Stomach` swaps the areas: acid splatters 5×5, jug water shrinks to 3×3.
[wiki]

---

## 3. Zebak — Jaws of Gluttony (region 15700)

Music: **Jaws of Gluttony**, archive 736, varp 23 bit 6. [cache]

| | |
|---|---|
| Npc | `toa_zebak` **11730**, enraged `toa_zebak_enraged` **11732**, dead **11733**; tail **11731** / **11734**; transmog **10680** [cache] |
| Level / size | 371 / **9×9** [cache] |
| Base HP | **580** [cache] [wiki] |
| Attack rate | **7**, → **4** when enraged [cache] [wiki] |
| Max hits | 38 melee, 16 magic, 16 ranged [wiki] |
| Defence | 70, drainable to **50** [wiki] |

Supporting npcs: `toa_zebak_jug` **11735**, rolling jug **11736**, safespot rock
**11737**, wave **11738** / bloody wave **11739**, land crocodile **11740**,
water crocodile **11741**, blood cloud **11742** / small **11743**, and two
projectile-carrier npcs **11744** (`spotanim_zebak_ranged01_npc`) and **11745**
(`spotanim_zebak_jugbreak_npc`). [cache]

**Attacks.** All three styles. Magic spits a pot that splits into red orbs;
ranged spits a rock that splinters. Both are **fully blockable** (absent
`Quiet Prayers`) and **damage resolves just before impact**, so both are
flickable. Melee hits through prayer at reduced accuracy and damage, and can
apply a bleed that damages the player when they move. [wiki]

Projectile / sound ids, from the reference: magic **2176** splitting into
**2181** with sound **5878**; ranged **2178** splitting into **2187** with sound
**5896**; impact sound **5884**; floor-roar spotanim **2184**. [nr]

**Specials**, queued at **85% / 70% / 55% / 40%**, alternating; a queued one is
discarded if he drops to the enrage threshold first. Poison and jugs come out
with every special. [wiki]

* **Great Roar** — throws jugs, poison and two or three rocks, then several
  attacks later roars for **three hits** of massive damage to everything not
  behind a rock (up to 3 tiles behind). Each hit costs the sheltering rock **50**
  of its **150** health. Every pattern always leaves at least one jug that
  creates a safespot. First iteration puts rocks in front of him with poison
  behind; the second shifts east.
* **Tidal Waves** — tail slam, boulders drop from one side, a wave sweeps across.
  Being caught pushes the player along and deals moderate damage; being pushed
  off the arena means walking speed, no attacking, and crocodile bites from the
  water until the ramp is used. Waves push jugs and *sometimes* clear poison.
  First iteration comes from the **south**, second from the **north**. Every two
  path levels narrows the gap by **one tile**.

**Poison pools** land on a random tile and may splatter again in a 3×3. They are
permanent unless a jug shatters near them or a wave clears them. Damage starts
low and escalates fast; also inflicts poison at 2. [wiki]

**`Not Just a Head`** gives him blood magic: either a Blood Blitz variant that
ignores defence bonuses for 8–11, healing him ~66% of it and splashing onto
clumped players, or a **Blood Cloud** that chases a random player, takes 2 damage
per tick by itself, and heals by touching someone. `Blood Thinners` makes it
three clouds; `Arterial Spray` widens range and healing. If nothing is in range
at spawn the cloud wanders and never aggros. [wiki]

**Enrage at 25%** — attack rate 4, no more roars or waves, distinct roar sound,
examine text changes. [wiki]

---

## 4. Path of Scabaras — Test of Isolation (region 14162)

Music: **Test of Isolation**, archive 738, varp 22 bit 31. [cache]

Five puzzles across two branched paths; both branches must be cleared. Solo
players cross between the two via shortcuts. Throughout, `toa_scabaras_scarab`
(npc **11697**, level 56, ranged) trickles out of the first two rooms — a
nuisance, blocked by Protect from Missiles. [wiki] [cache]

| # | Puzzle | Rule | Cache evidence |
|---:|---|---|---|
| 1 | **Sequence** | a button lights tiles in an order; step them in that order. Wrong tile = damage. Re-press for a new order | `toa_flip_tile` seq 9490 |
| 2 | **Obelisks** | hit five obelisks in the right order with a ranged/magic weapon; a wrong one drops avoidable ceiling rocks. 5! = **120** permutations, 1 correct. Standing on the lower stair step is untargetable; needs attack range 10 | npcs **11698** / **11699** (`_hit`), seq `toa_obelisk01_glow` 9515 |
| 3 | **Lights-out** | step on plates to light all of them; stepping toggles neighbours. Right-click flips one plate for a flat **20** unavoidable damage | `toa_scabaras_lightsout_tile_off` loc **45344** |
| 4 | **Addition** | requires the other four done first. A tablet names a number **20–45**; step on symbol tiles summing exactly to it. Overshooting damages. The symbol values are written on the north and south walls at the path entrance | — |
| 5 | **Matching** | two boards of nine tiles; step the matching pair on each. **Solo starts with four of nine solved** and the blocking statue moved up one tile | locs **45356–45364** (buttons 1–9), **45365–45373** (tiles 1–9) |

Solo players should route so as to skip the obelisk room — it is random and on
average the slowest. [wiki]

---

## 5. Kephri — A Mother's Curse (region 14164)

Music: **A Mother's Curse**, archive 737, varp 23 bit 3. [cache]

| | |
|---|---|
| Npc | shielded **11719**, weakened **11720**, enraged **11721**, dead **11722** [cache] |
| Level / size | 341 / **5×5** [cache] |
| HP | shield phases **150**, final phase **80** [cache] [wiki] |
| Attack rate | **6**, magic [cache] |
| Max hit | 24 [wiki]; `param_65` = 50 [cache] |
| Defence | 80, drainable to **60**, restored on entering the final phase [wiki] |
| Weakness | stab; resistant to crush, very resistant to slash/ranged/magic; **+35% from fire spells** [wiki] |

**Auto-attack:** a fireball lobbed into the air with a shadow marking the landing
tile. Protect from Magic mitigates ~66% if hit. `Aerial Assault` makes it a 3×3
and increases damage — and also applies to the Bomber Scarabs and to Kephri's
Phantom in Wardens P3. [wiki]

**Shielded specials:**

* **Dung Strike** — scarabs circle a target, scatter, then hurl them to the back
  of the arena (`Kephri throws you back!`), stunned **2 s**; dung piles fall along
  the knockback line, blocking tiles. Targets are chosen in orb order, top row
  then bottom, left then right. `Blowing Mud` targets two (no effect solo). If no
  walkable tile exists within **ten tiles**, the player takes rapid damage
  instead — a fast death. Dung locs **45149 / 45150 / 45151**. [wiki] [cache]
* **Mass Incubation** — eggs splatter the arena. `kephri_egg_hatch` **11729**
  (large, dark) hatch **Agile Scarabs** (`toa_kephri_scarab_rangekite` **11727**,
  level 53, 30 HP, ranged, max 5, flees when approached, does **not** scale with
  raid level); `kephri_egg_explode` **11728** (small, light) simply explode 3×3.
  Players always deal their max hit to an egg regardless of style.
  `Lively Larvae` raises the dark eggs from 2 to 4. Dung landing on a dark egg
  does not destroy it, but kills the scarab on hatch. Too many scarabs already
  present → Dung Strike is used instead. [wiki] [cache]

**Shield-break intermissions.** Kephri is dazed and calls her children; one
shield charge is restored. [wiki]

* **Scarab swarms** (`toa_kephri_shield_scarab` **11723**, and legacy
  `scarab_swarm` **729**) walk to her and recharge her shield, ~10% each or less
  for bigger teams, overcharging up to ~115% of capacity.
  * First intermission: the first **18 of 28** reach her in time. Second: the
    first **14 of 24**.
  * Six spawn points around the room — NE, SE, S, SW, NW, N. The first is
    random, then **clockwise**, occasionally skipping one.
  * Spawn cadence accelerates in batches: first 4 every **4** ticks, next 4 every
    **3**, next 4 every **2**, final 16 every **1**.
* **Bomber Scarabs** behave exactly like her auto-attack, with a fixed flight
  time regardless of spawn point.
* **Arcane Scarab** (`toa_kephri_guardian_mage` **11726**, 3×3, level 89, 40 HP,
  attack rate 6): charges a 3×3 swarm around itself; a full charge deals **65+**
  through everything, ~66% less under Protect from Magic. Relocates on a heavy
  hit (**39+ within 4 ticks**), on three moderate hits (+1 per extra player), or
  after a successful discharge. Its four positions are fixed and always in the
  order **SE → NW → mid-S → NE**. Always spawns SE.
* **Spitting Scarab** (`toa_kephri_guardian_ranged` **11725**): ranged, poisons
  from 6, accurate, hits through prayer, attacks **everyone within 8 tiles**
  (the strategies page says ten in one place — `[M2]`). Always spawns NE.
* **Soldier Scarab** (`toa_kephri_guardian_melee` **11724**, 40 HP): melee, and
  every ~5 s launches an unattackable swarm that recharges the shield (or heals
  her, on real health) for ~5–7 charges, scaled to the scarab's **current**
  health. Always spawns east.
* Threshold composition: **first** shield break spawns a Spitting Scarab;
  **second** spawns a Soldier and an Arcane. `More Overlords` adds one more per
  break — a Soldier on the first, a Spitting on the second.
* Both the Soldier and the Spitting scarab can be **trapped by dung** — the
  Soldier by standing on the tiles east of Kephri aligned with her, the Spitting
  by running west toward the teleport crystal.
* Path level ≥ 2 periodically spawns a **trio** of swarms.
* `Medic!` spawns swarms *outside* the intermissions, moving 50% slower.

**Final phase** begins after the third shield clears: the surrounding swarm
disperses, she uses one Dung Strike on everyone, and her health becomes
damageable. [wiki]

---

## 6. Path of Het — Test of Strength (region 14674)

Music: **Test of Strength**, archive 732, varp 23 bit 0. [cache]

Break **Het's Seal** — `toa_het_goal` **11706** (protected, 3×3) /
`toa_het_goal_vulnerable` **11707**. **119 HP solo**, scaling with player count
but **not** with raid level or invocations. [cache] [wiki]

* **Caster statue** fires an energy beam every **9 ticks**. [toa-plugin] Its
  graphics-object ids are 2114 (horizontal), 2064 (vertical), 2120 (crash).
* **Mirrors** — `toa_het_mirror` **45455**, fixed **45456**, dirty **45457**,
  destroyed **45467**. Three are portable: pick up, place, rotate, push. The rest
  are fixed, and in groups some start **dirty** and must be cleaned. [cache] [wiki]
* **Barriers** — `toa_het_barrier_internal` **45458**, `_end` **45460**, the two
  destructible variants **45462** / **45464**, `_destroyed` **45466**, plus the
  `_rising` forms. Undamaged barriers block the beam; worn ones are minable.
  [cache] [wiki]
* The beam can always be solved with **1–3 mirrors** plus some barrier removal.
  [wiki]
* **Orbs of Darkness** — `toa_het_orb` **11708** — cross the room in straight
  lines dealing moderate damage. Two can share a tile and both will hit. In
  groups they dirty any mirror they hit. The beam itself also damages, less.
  [cache] [wiki]
* Once the beam reaches the shielded statue the seal drops for **15 ticks (9 s)**.
  Standing one tile from the seal and clicking to mine one tick before the beam
  fires yields **8 hits per cycle**. [wiki]
* A wipe resets statue progress **and redraws the puzzle**. [wiki]

**Pickaxes.** The statues hold a pickaxe, persisting across raids; bronze by
default. The cache ships a loc per tier — `toa_het_statue_pickaxe_empty01`
**45469** through `bronze/iron/steel/black/mithril/adamant/rune/gilded/dragon
(×3)/3rdage/infernal/crystal/trailblazer` **45470–45486**. [cache]

Damage per hit, by **visible** Mining level (invisible boosts do nothing;
dragon/crystal/3rd age are equivalent): [wiki]

| Visible Mining | Dragon+ | Rune | Bronze–Adamant |
|---|---|---|---|
| 99+ | **19–21** | **17–19** | 14–16 |
| 85–98 | **17–19** | **15–17** | 12–15 |
| 1–84 | 10–12 | 8–10 | 5–7 |

Mod Ash's statement of the same rule: base minimum 8 / 15 / 17 for <85 / <100 /
100+, +2 for dragon or better, −3 for below rune, then +0/1/2 uniformly at
random. A max hit of **17** is the threshold for a reliable one-cycle. [wiki]

---

## 7. Akkha — Sands of Time (region 14676, **plane 1**)

Music: **Sands of Time**, archive 739, varp 23 bit 4. [cache]

| | |
|---|---|
| Npcs | spawn **11789**, melee **11790**, range **11791**, mage **11792**; enrage spawn **11793**, initial **11794**, `akkha_enrage` **11795**, dummy **11796**; shadow **11797**, shadow-enrage **11798** / dummy **11799**; trail orbs lightning **11800**, darkness **11801**, burn **11802**, freeze **11803**; enrage orb **11804**; headbar npc **11805** [cache] |
| Level / size | 337 / **3×3** [cache] |
| Base HP | **400** [cache] [wiki] |
| Attack rate | **6** [cache] [wiki] |
| Max hit | 55 [wiki] |
| Defence | 80, drainable only to **70**, and restored fast. Eye of Ayak's magic-defence drain is permanent [wiki] |

**Aggression.** Locks to the first player in. After each mechanic he re-picks by
distance; ties broken by highest defence bonus — so a tank can hold him by
standing close. [wiki]

**Prayer.** He runs the overhead matching his own current style. Melee is
target-only but cleaves anyone in the way; ranged and magic hit **everyone** in
the arena, with damage resolved when the projectile spawns. Protection prayers
are partial: ~80–90% off magic and ranged, ~75% off melee. [wiki]

Ids for the styles, from the reference: melee anim **9770**, ranged-sword swing
**9772**, magic-sword swing **9774**; ranged projectile **2255** with impact
sound **5640**; magic projectile **2253** with impact sound **5774**;
style-change sound **5585**; quadrant-symbol sound **5667**; invisible **9784**
/ visible **9785**; ground detonation **9776**, memory **9777**, trail **9778**;
final stand **9779**. [nr]

**Quadrants.** The arena is quartered: **fire NW, shadow/skull NE, lightning SE,
ice SW**. Whichever quadrant a player is standing in decides the rider on any
heavy hit: [wiki]

| Quadrant | Rider |
|---|---|
| Fire | burn, 5 damage repeatedly, **spreads to adjacent players** |
| Shadow | low rapid damage, drains all combat stats by 3 |
| Lightning | temporarily disables protection prayers |
| Ice | attack speed slowed by 1 tick; ends with *"The ice around you melts away."* |

Reference quadrant hit sounds: **5591, 3887, 173, 156**; the four light objects
are locs **45868–45871**. [nr]

**Shadows.** At **80%** Akkha goes invulnerable and four `Akkha's Shadow`
(**11797**, level 108, **70 HP**, 3×3, same combat stats but lower defence)
appear — *"Shadows appear throughout the room!"*. Kill one and lure him to it to
break the invulnerability. Repeats at **60% / 40% / 20%**, announced as *"The
shadows in the room are restored!"*, which heals every damaged shadow. [wiki] [cache]

Each shadow independently charges its quadrant's hourglass, shown as a bar over
its head. Kill it first and that quadrant is spared; let it fill and the shadow
slams its spear and floods the quadrant from edge to centre — dodgeable with
precise timing. Charging resumes after a short delay, resets on any health
threshold, and is **paused during Memory Blast** (a blast that completes during
one fires immediately after). [wiki]

**Specials, every 7 attacks** (6 with `Stay Vigilant`): [wiki]

* **Detonate** — everyone marked (*"You have been marked for detonation!"*),
  then detonates in a 3×3 **plus**; `Feeling Special?` makes it a **cross**.
  Avoided by not sharing a row or column, or by everyone standing on one tile.
  **Group encounters only.** Reference: a 3-tile reach per arm, tinting
  `(8,1,100,112,0,245)`, warning at 8 ticks before. [nr]
* **Memory Blast** — he vanishes; floor markers glow in a set order naming the
  safe quadrant for each burst. **Four ticks** to move between bursts — **two**
  with `Feeling Special?`. Consecutive markers are always **adjacent**
  quadrants, never opposite. Wrong quadrant = heavy damage plus that quadrant's
  rider. Iterations = `4 + min(2, pathLevel / 2)` in the reference `[M3]`. [wiki] [nr]
* **Trailing Orbs** — orbs materialise **behind** the player as they move
  (*"Magical orbs begin to materialise around you!"*), detonating with a short
  delay for heavy damage plus the rider. Standing still spawns nothing.
  `Feeling Special?` adds an orb **in front**. Ends with *"The magical orbs stop
  materialising."* Reference: 20-tick warning, 605-duration tint. [nr]

`Keep Back` adds a melee hit alongside the ranged and magic attacks, landing
just before the projectile with no animation. `Double Trouble` runs two specials
at once. [wiki]

**Enrage phase.** At 0 HP he heals **20%** and makes a final stand, praying
Magic **and** Missiles at once — melee only. He stops attacking and instead
floods the arena with **Unstable Orbs** (white, one direction, arrow underneath),
capped at **25** damage and scaling with raid and path level. Protect from Magic
cuts 25% *before* the cap, so above raid level 300 it only helps stacked groups.
He swaps to another shadow after **3 successful hits solo**, +1 per extra player;
multi-hit specials count each hit; thrall hits do not count. [wiki] [nr]

---

## 8. Path of Apmeken — Test of Companionship (region 15186)

Music: **Test of Companionship**, archive 741, varp 23 bit 1. [cache]

The only non-puzzle challenge: **eight waves** of baboons plus a room-maintenance
quick-time layer. Baboon health is fixed regardless of raid level *and* party
size. [wiki]

Each baboon is hugely defensive against everything but its counter, and the
counter style **always lands a max hit** — for brawler, thrower and mage. For the
specialist baboons, **ranged** guarantees the max hit. [wiki]

| Baboon | Npc `[cache]` | Weak to | HP | Behaviour |
|---|---:|---|---:|---|
| Brawler (small / large) | 11709 / 11712 | Magic | 25 / 30 | melee |
| Thrower (small / large) | 11710 / 11713 | Melee | 30 / 35 | ranged |
| Mage (small / large) | 11711 / 11714 | Ranged | 20 / 25 | magic |
| Shaman | 11715 | Ranged | 40 | summons up to 4–5 **Thralls**; at the cap it casts instead. **Poison or venom stops the summoning silently.** Magic hits drain prayer by the damage dealt if unprotected |
| Thrall | 11718 | any | 9 (cache: 2 `[M4]`) | melee; drains prayer as a fraction of the hit, **including hits prayer would have blocked** |
| Volatile | 11716 | Ranged, Magic | 8 | walks to a random player and explodes 3×3; damage scales with its remaining health; **kills any baboon caught in the blast outright** |
| Cursed | 11717 | Ranged, Magic | 20 | not aggressive; wanders leaving venom pools (6–8 per tick, applies venom). Never walks onto a vent tile or the tile in front of a pillar, and usually not under a player |

Cache attack rates: **4** for everything except the Shaman at **8**. Cache
`param_26` styles: 2 melee, 4 ranged, 5 magic. [cache]

**Waves** (solo; group adds one extra combat baboon per wave): [wiki]

| Wave | Composition |
|---:|---|
| 1 | 2× Brawler, 1× Shaman |
| 2 | 2× Thrower, 1× Volatile |
| 3 | 2× Mage, 1× Cursed |
| 4 | 2× Thrower, 3× Special |
| 5 | 2× Mage, 1× Shaman, 2× Special |
| 6 | 2× Brawler, 1× Shaman, 2× Special |
| 7 | 1× Brawler, 1× Thrower, 1× Shaman, 2× Cursed |
| 8 | 1× Shaman, 2× Volatile, 2× Special |

**Apmeken's Sight.** The first player in gets it; in groups it rotates randomly
throughout. The holder sees which of three problems the room has, announced to
everyone as *"You sense an issue somewhere in the room."*: [wiki]

| Problem | Fix | Loc `[cache]` |
|---|---|---|
| **Roof supports** ("P") | click a support once, with a hammer (a dragon warhammer works) | `toa_path_apmeken_pillar` **45494**, `_shaking` **45495**, `_no_repair` **45496**; hammers crate **45497** |
| **Floor vents** ("V") | stand on the vent centre and drink/pour the neutralising potion | `toa_path_apmeken_vent` **45499**; potions crate **45498**; obj **27297** |
| **Corruption** ("DD") | the sighted player sees teammates glow dark red; everyone stacks on one tile and the sighted player pours. Or pour the potion **on** a corrupted player. **Group only** | — |

Missing the required count damages everyone, scaled by how many actions were
missed; fixing something that did **not** need fixing damages that player. The
required count equals the party size — solo does one vent or one pillar. [wiki]

Venom pools left by cursed baboons: loc `toa_path_apmeken_venom_pool` **45493**.
[cache]

---

## 9. Ba-Ba — Ape-ex Predator (region 15188)

Music: **Ape-ex Predator**, archive 740, varp 23 bit 5. [cache]

| | |
|---|---|
| Npcs | `toa_baba` **11778**, coffin **11779**, digging **11780**, baboon **11781**, boulder **11782** / weak **11783**, rubble **11784–11787**, sarcophagus npc **11788** [cache] |
| Level / size | 359 / **5×5** [cache] |
| Base HP | **380** [cache] [wiki] |
| Attack rate | **6**, crush [cache] |
| Max hit | 24 [wiki] |
| Aggression | first player in; then **lowest current Hitpoints**, so a low-HP tank holds her [wiki] |

Protect from Melee blocks her auto-attack **entirely**. [wiki]

**Mechanics:** [wiki]

* **Rockfall** — one rock per player, landing on the tile they occupied when it
  was used, plus two large debris piles under the same shadow marker. Debris has
  a 3×3 footprint but only damages directly under the shadow; otherwise it shoves
  players aside harmlessly. Debris has **10 HP, +10 per player in the room**.
* **Rock Throw** — a boulder at every player, landing **~6 ticks** later for
  massive damage. Standing next to **debris**, an **unopened sarcophagus**, or a
  **baboon** nulls it completely, at a cost of 10 / 30 / 12 damage to that shield
  respectively. Less damaging in groups than solo. If debris was destroyed,
  **Rockfall follows immediately**.
* **Shockwave** — she faces one of **eight** directions, shadows appear, then she
  swipes a roughly **5×5** area (**7×7** with `Shaking Things Up`). Also used as
  a fast "stomp" if her target loiters underneath her.
* **Baboon's Discharge** — **2–4** level-77 baboons (npc **11781**, **10 HP**,
  ranged; players always max hit them). Periodically one breaks off to open a
  sarcophagus, hitting it for **20** per attempt. An opened sarcophagus fires
  unstable energy in a cone; `Gotta Have Faith` scales that damage inversely to
  the player's remaining prayer.
* **Rolling Stones** at **66.6%** and **33.3%** — she leaps to her throne and
  screams, knocking everyone to the far side, then rolls **ten sets** of
  boulders down the slope. Contact = knocked back four tiles and heavy damage.
  She stops early if meleed past the boulder start line.
  * One of the five boulders per column is **cracked**; ranged attacks on it
    always max hit.
  * The cracked boulder starts in the **middle three** positions, and each later
    column moves it at most **two positions** north or south.
  * The scream destroys debris; boulders destroy debris and kill baboons instantly.
  * Solo boulder health starts at **25**, +2 per two Apmeken path levels → 27 at
    level 2–3, 31 at level 4+. `Boulderdash` doubles the spawn rate.
* `Mind the Gap!` turns the two knockbacks lethal unless the player is at the
  room's north or south edge.
* `Jungle Japes` drops two banana peels per baboon death; stepping on one
  damages and stuns for **3 s**.

---

## 10. The Wardens — Amascut's Promise (regions 15184 then 15696, **plane 1**)

Music: **Amascut's Promise**, archive 735, varp 23 bit 7 — the longest track in
the raid at 938 units. [cache]

All party members must speak to **Osmumten** (npc **11834**
`toa_osmumten_wardens`, or **11835** `toa_amascut` during *Into the Tombs*)
before the fight starts. [wiki] [cache]

### Phase 1 — the obelisk (region 15184)

| | |
|---|---|
| Npcs | inactive wardens **11746** / **11747**; active **11748** Elidinis' / **11749** Tumeken's; obelisk **11750** inactive / **11751** active; energy orb `toa_wardens_energy` **11769** [cache] |
| Obelisk | 3×3, **260 HP**, attack rate 4, defence 100 → drainable to **60**, weakest to standard ranged [cache] [wiki] |
| Warden bodies | level 489, 5×5, **800 HP**, attack rate 14 [cache] |

The obelisk emits orbs down an energy trough that branches into three. Standing
beside a trough **blocks** orbs at **3 damage each**, slowing but not stopping
that Warden's charge. Blocking one side deliberately **desynchronises** the two
Wardens — and decides which one is fought in phase 2, because **the Warden whose
charge was disrupted most is destroyed** when the obelisk dies. [wiki]

A charged Warden (red circle full, Amascut's seal in front of it) discharges one
of two, alternating **UFOs → Charged Shot**: [wiki]

* **Charged Shot** — *"A large ball of energy is shot your way..."*. Elidinis'
  ball is small with a **blue** message; Tumeken's is larger and **orange**. In
  groups, **spread** for Elidinis (damage clips neighbours) and **stack** for
  Tumeken (damage is shared) — capped at 30 and ~7 respectively when handled
  right. Solo there is no mitigation: caps **45** Elidinis, **30** Tumeken.
* **UFOs** — floating pyramids spread out and discharge damaging light. If both
  Wardens fire simultaneously the arena edge by the stairs is safe. Seqs
  `toa_wardens01_pyramid_load` 9527, `_appear` 9525, `_attack` 9524, `_leave`
  9526, `_invisible` 9528. [cache]

An orb in mid-air with ~3 seconds left when the obelisk dies **still lands**.
[wiki] `Ancient Haste` speeds the charge rate. [wiki]

### Phase 2 — the standing Warden (region 15696)

| | |
|---|---|
| Npcs | Elidinis' mage **11753** / range **11754** / exposed **11755**; Tumeken's mage **11756** / range **11757** / exposed **11758**; obelisk **11752**; cores **11770** Tumeken / **11771** Elidinis [cache] |
| Level / size | 489 / 5×5 [cache] |
| HP | **140** — the shield bar, not the real pool [cache] |
| Attack rate | Elidinis **7**, Tumeken **8** [cache] [wiki] |
| Max hit | Elidinis 20, Tumeken 22 [wiki] |

Two protection prayers are always up, **Protect from Melee always among them**;
Tumeken's starts praying Magic, Elidinis' starts praying Ranged. The shield gives
the player **100% accuracy** but converts the player's accuracy bonus into
*damage* — so a twisted bow or Tumeken's shadow is disproportionately strong. An
unattacked Warden slowly restores shields. Fill the yellow bar and the **core**
is ejected. [wiki]

**Warden attacks:** magic is a red skull, ranged a plain skull, both resolving on
impact so both are late-flickable; melee cannot be reacted to. Elidinis' Warden
stops a few tiles short of its target; Tumeken's closes to melee, which makes
Protect from Melee mandatory against it. Both are weak to magic, exposed once
every two cycles by the prayer rotation. Two specials, both launched with the
**magic animation**: [wiki]

* **Divine Projectile** (above 55% HP) — disables everyone's overheads and fires
  a **red scimitar** (melee), **white arrow** (ranged) or **blue spell** (magic);
  pray accordingly or take heavy damage.
* **Imprisonment** (below 55% HP) — a dark projectile at the player's position
  when the attack began; a hit encases them in stone, blocking movement and
  attacking. Prayer switching and eating still work.

**Obelisk attacks**, cycled in a set pattern with no repeat until the set is
exhausted, each telegraphed by the lightning colour around the obelisk: [wiki]

| Attack | Warning | Shape |
|---|---|---|
| **Converging Beam** | red | two beams N/S then two E/W, each spreading and converging inward. The opposite quadrant is safe |
| **Windmill Beam** | orange | four beams sweeping clockwise; the floor glows first |
| **Lightning Skull** | yellow | red skulls land, then 7×7 lightning centred on each. The two tiles in each cardinal direction from the landing tile are safe |

`Penetration` adds prayer-disabling to obelisk hits and raises their damage;
`Acceleration` speeds both the Warden and the obelisk. [wiki]

**The core.** Damage to it counts **×5** against the Warden — a 55 becomes 275 —
and **melee always max hits** it. Exposure grows as the Warden weakens: [wiki]

| Warden HP | Core exposed |
|---|---|
| 100–80% | **21 ticks** (12.6 s) |
| 79.9–60% | **29 ticks** (17.4 s) |
| 59.9–0% | **37 ticks** (22.2 s) |

Points cap at **three downs**; the fourth and beyond award nothing.
[wiki] [toa-plugin] The reference watches animation **9670** to count them. [nr]

When the core dies, the Warden collapses and spends its last power **restoring
the Warden destroyed in phase 1**. The obelisk destabilises and explodes, and the
room warps into a void. [wiki]

### Phase 3 — the restored Warden

| | |
|---|---|
| Npcs | inactive **11759** / **11760**; active Elidinis' **11761** / Tumeken's **11762**; charging **11763** / **11764**; death **11765** / **11766**; Amascut **11767**, enraged **11768**; orbs `wardens_p3_orb_blue` **11772** / `_red` **11773**; phantoms `toa_wardens_zebak` **11774**, `_baba` **11775**, `_kephri` **11776**, `_akkha` **11777** [cache] |
| Level / size | 544 / 5×5 [cache] |
| Base HP | **880** [cache] |
| Attack rate | **7** by default [wiki]; **6 / 4 / 3** with `Overclocked` / `Overclocked 2` / `Insanity` [wiki] |
| Max hit | 26 [wiki] |
| Defence | 150, drainable to **120**; raised to **180** in enrage, cap still 120 [wiki] |

No prayers, no combat styles — only floor slams, dealing **typeless** damage. The
order is fixed: **right (east) first, then left, then both**. For a side slam,
stand at least one tile to the side **opposite** the raised half; for the double,
stand in the centre columns in front of its face. Travel time means late
reactions work from 6–9 tiles away. [wiki]

**Energy Siphons** at **80 / 60 / 40 / 20%**. [wiki] [cache: `toa_wardens_energy` 11769]

* Immune to ranged and magic — **melee only**, with no attack delay.
* **1 HP** each solo; **2** at four players, then +1 per two more (3 at six, 4 at
  eight). The layout and health follow the number of players **still alive**.
* Damage one enough and it turns to face the Warden. Reverse them all in time and
  they collide with it for damage proportional to its maximum health. Fail and
  the Warden detonates them all in a shockwave from the centre, scaling with raid
  level **and with how many were left untouched** — two missed at high raid level
  is lethal. The shockwave is dodgeable by standing far out and running in.
* Time allowed depends on group size and on `Insanity`.
* The **second and third** batches bring a **phantom** of an upper-floor boss:
  facing Elidinis' Warden gives **Akkha's** and **Kephri's** phantoms; facing
  Tumeken's gives **Zebak's** and **Ba-Ba's**. **Invocations that applied to the
  original apply to the phantom** — which is why Tumeken's is the usual pick.

**Enrage at 5%:** a one-time heal of **20%** of maximum, +30 Defence, and the
phantom behind the Warden begins **eating the arena**, ripping out the furthest
row of tiles repeatedly until only the row in front of the Warden is left. Slams
stop; **lightning bolts** strike instead, each telegraphed by a growing shadow,
at a rate **unaffected by invocations**. On the last row, larger shadows are
safer — a struck tile has a short grace period. `Insanity` rips tiles faster,
speeds the phantoms, and makes the slam resume from where it left off rather than
resetting. [wiki]

At 0 HP the phantom consumes the arena and a **white teleport crystal**
(`toa_teleport_crystal_continue_wardens`, loc **45138**) appears where the Warden
stood. [wiki] [cache]

---

## 11. The vault — Laid to Rest (region 14672)

Music: **Laid to Rest**, archive 731, varp 22 bit 30. [cache]

* **Osmumten's sarcophagus** — purple flames mean a unique was rolled and names
  the player who opens it; white means no unique. `toa_vault_sarcophagus` is
  varbit **14373**, the eight side chests are varbits **14356–14360** and
  **14370–14372**, backed by inv **811** (`toa_chests`, size 9) and interface
  **771** (`toa_chests`). [cache] [wiki]
* Unopened rewards can be reclaimed from the **Rewards Niche** in the lobby's
  south-east corner — but **re-entering the raid destroys them**, with a warning
  first. [wiki]
* The **scoreboard** on the east side shows the raid summary: damage dealt,
  damage taken, deaths and an honorary title. Interface **775**
  (`toa_scoreboard`) and **482** (`toa_raid_summary`); eighteen title varbits
  **14326–14343** (`swimmer`, `chocolateteapot`, `leech`, `specialist`,
  `pickyeater`, `peasant`, `showoff`, `tank`, `baller`, `entomophobe`, `moth`,
  `carry`, `anchor`, `glutton`, `sorcerer`, `archer`, `brawler`, `clutch`), with
  `toa_title_selected` **14324**. [cache] [wiki]

### Loot table [wiki]

**Pre-roll.** Under 1,500 points: 1 noted **Fossilised dung**, and no common
rewards at all.

**Uniques** — one per raid, chosen with these weights once the game has decided
to give one:

| Item | Obj `[cache]` | Weight |
|---|---:|---|
| Osmumten's fang | 26219 | 7/24 |
| Lightbearer | 25975 | 7/24 |
| Elidinis' ward | 25985 | 3/24 |
| Masori mask | 27226 | 2/24 |
| Masori body | 27229 | 2/24 |
| Masori chaps | 27232 | 2/24 |
| Tumeken's shadow (uncharged) | 27277 | 1/24 |

The fang and Lightbearer are drastically rarer below raid level 50; the other
five below raid level 150 — in both cases behind an extra **1/50** roll, failing
which an untradeable tertiary is given instead. Above raid level 300 the fang and
Lightbearer weights fall while the rest rise:

| Raid level | Fang | Lightbearer | Ward | each Masori | Shadow |
|---|---|---|---|---|---|
| 150–300 | 1/3.43 | 1/3.43 | 1/8 | 1/12 | 1/24 |
| 350 | 1/3.67 | 1/3.67 | 1/7.33 | 1/11 | 1/22 |
| 400 | 1/4.75 | 1/3.8 | 1/6.33 | 1/9.5 | 1/19 |
| 450 | 1/4.5 | 1/4.5 | 1/6 | 1/9 | 1/18 |
| 500 | 1/5.5 | 1/4.71 | 1/5.5 | 1/8.25 | 1/16.5 |

**Common rewards** — three rolls, each 1/27 across 26 entries: cache of runes,
coins 1126–51285, death runes 57–2564, soul runes 28–1282, gold ore, dragon dart
tips, mahogany logs, sapphire, emerald, gold bar, potato cactus, raw shark, ruby,
diamond, raw manta ray, cactus spine, dragonstone, battlestaff, coconut milk,
lily of the sands, toadflax/ranarr/torstol/snapdragon/magic seeds, dragon med
helm, blood essence. Quantity:

```
qty = points / itemDivisor                                          if RL < 300
    = points / itemDivisor * (1.15 + 0.01 * (RL - 300) / 5)         if RL >= 300
```

**Tertiaries**, rolled alongside anything:

| Item | Obj `[cache]` | Rate |
|---|---:|---|
| Thread of elidinis | 27279 | 1/10, with bad-luck mitigation scaling to 1/3.33 by 15 KC; **1/50 forever after the first** |
| Eye of the corruptor | 27285 | 1/50 (→3/50 by 75 KC) |
| Jewel of the sun | 27289 | 1/50 (→3/50 by 75 KC) |
| Breach of the scarab | 27283 | 1/50 (→3/50 by 75 KC) |
| Jewel of amascut | 30893 | 1/50 (→3/50 by 75 KC) |
| Clue scroll (elite) | — | +1% per 2,000 personal points, capped at **25%**, ×1.05 with the elite CA |
| Tumeken's guardian | `wardenpet_tumeken` | base 1/350,000 scaled by raid level `[M5]` |

The four jewels share a **1/12.5** "any jewel" roll; missing jewels are
guaranteed to be the one given, so an unowned jewel is effectively 1/37.5, 1/25
or 1/12.5 when the player owns one, two or three.

**Challenge rewards**, guaranteed once their condition is met: Cursed phalanx,
Masori crafting kit, Menaphite ornament kit, Remnant of akkha **27377**, of ba-ba
**27378**, of kephri **27379**, of zebak **27380**, and the Ancient remnant
**27381**. [wiki] [cache]

**Books** — six, one per boss plus one found in Osmumten's bags: `toa_book_lobby`
27300, `_akkha` 27302, `_baba` 27304, `_kephri` 27306, `_zebak` 27308,
`_wardens` 27310, `_icthlarin` 27312, with read-state varbits 14449–14454.
[cache] [wiki]

**Icthlarin's shroud** tiers at **100 / 500 / 1000 / 1500 / 2000** Normal or
Expert completions. [wiki]

---

## 12. Music — all eleven tracks

From DBTable 44 of cache.osrs239, already extracted in
`docs/audio/music_tracks_osrs239.tsv`. `unlock` is the varp and bit the client
sets when the track is first heard. [cache]

| Track | Archive | Length field | Unlock varp,bit | Room |
|---|---:|---:|---|---|
| Into the Tombs | 734 | 719 | 22,28 | entrance / miniquest |
| Beneath Cursed Sands | 730 | 518 | 22,29 | Nexus (main hall) |
| Test of Resourcefulness | 733 | 439 | 23,2 | Crondis challenge |
| Jaws of Gluttony | 736 | 566 | 23,6 | Zebak |
| Test of Isolation | 738 | 432 | 22,31 | Scabaras challenge |
| A Mother's Curse | 737 | 622 | 23,3 | Kephri |
| Test of Strength | 732 | 720 | 23,0 | Het challenge |
| Sands of Time | 739 | 770 | 23,4 | Akkha |
| Test of Companionship | 741 | 509 | 23,1 | Apmeken challenge |
| Ape-ex Predator | 740 | 656 | 23,5 | Ba-Ba |
| Amascut's Promise | 735 | 938 | 23,7 | Wardens, both rooms |
| Laid to Rest | 731 | 563 | 22,30 | vault |

The room→track mapping is the reference implementation's `EncounterType`
soundtrack column, which names exactly these tracks and assigns Amascut's Promise
to both Warden rooms. [nr]

---

## 13. Sound effects — 946 of them

`sources/cache_sound_toa.txt` has the full list with ids. Families, by count:

| Family | Count | Notes |
|---|---:|---|
| `toa_akka_attack_*` | 41 | plus `toa_akka_ranged_*` 40, `toa_akka_melee_*` 21, `toa_akkha_*` 8, `npc_akkha_*` 15 |
| `toa_zebak_attack_*` | 33 | plus `_vomit` 12, `_death` 12, `_ranged` 8 |
| `toa_wardens_death_*` | 32 | plus `_wake` 30, `_range` 17, `_red` 14, `_explosion` 13, `_melee` 10, `_tiles` 9, `_release` 8, `_obelisk` 7, `_enraged` 7, `_charge` 7 |
| `toa_crondis_croc_*` | 27 | |
| `toa_kephri_attack_*` | 20 | plus `_special` 12 |
| `toa_mandrill_attack_*` | 17 | Ba-Ba; plus `toa_small_mandrill_*` 18, `toa_big_mandrill_*` 9, `toa_baba_spawn_*` 16, `toa_baba_attack_*` 8 |
| ambience | ~100 | `toa_water_drop` 20, `toa_frog` 20, `toa_water_bubble` 12, `toa_wind` 10, `toa_sand` 10, `toa_rumble` 10, `toa_rocks` 10, `toa_murmur` 10, `toa_insects` 10, `toa_bats` 8 |

Ids run **1478–10195**, i.e. the ToA block is not contiguous — the two
`scarabs*` / `scarab_boss_*` families at 741–743 and 1478–1483 predate the raid
and are reused by it. Sound ids that the reference binds to specific attacks are
listed per boss above and are the only ones with known semantics; the remaining
~900 are named but unassigned `[M6]`.

---

## 14. Combat Achievements — 51 tasks, ids 421–471

`sources/wiki_combat_achievements_toa.tsv` carries id, name, tier, monster
(which of the three modes), type and description. Distribution:

| Tier | Count |
|---|---:|
| Hard | 4 |
| Elite | 13 |
| Master | 22 |
| Grandmaster | 12 |

By type: Mechanical 16, Perfection 16, Kill Count 9, Restriction 7, Speed 3.

The nine `Perfect ...` and four `Perfection of ...` tasks are per-room no-damage
or no-mistake flags, and following the Theatre of Blood precedent in this tree
they must be **cleared by damage** rather than set by its absence, so an
unimplemented mechanic cannot award one.
