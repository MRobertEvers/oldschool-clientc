# Jad — TzTok-Jad and JalTok-Jad

Mechanics, tick timings, animation lengths and healer behaviour for both Jads,
sourced from the Old School RuneScape Wiki and cross-checked against this
tree's `cache.osrs239` config data.

Two kinds of statement appear below and they are kept apart on purpose:

- **[W*n*]** — stated by the wiki. The numbered reference list is at the end.
- **[C]** — read out of this tree's cache (`OSRS-Content/osrs239-content/configs/`).
  The wiki does not publish sequence ids or frame timings, so every animation
  length here is measured, not cited. Frame delays in a `.seq` are client
  cycles of 20 ms; **30 cycles = 1 game tick = 600 ms**.

Anything neither source states is called out as **unstated** rather than
guessed. There are three such holes and they are listed in §6.

---

## 1. TzTok-Jad (Fight Caves)

### 1.1 Record

| Field | Value | Source |
|---|---|---|
| npc id | 3127 | [W1] |
| Combat level | 702 | [W1] |
| Size | 5×5 | [W1] |
| Hitpoints | 250 | [W1] [W4] |
| Attack / Strength / Defence | 640 / 960 / 480 | [W1] |
| Magic / Ranged | 480 / 960 | [W1] |
| Magic attack bonus | +60 | [W1] |
| All other offensive/defensive bonuses | 0 | [W1] |
| Attack styles | Stab (melee), Magic, Ranged | [W1] |
| Attack speed | **8 ticks** (4.8 s) | [W1] |
| Melee attack speed | **4 ticks** (2.4 s) | [W1] |
| Max hit — melee | 97 | [W1] |
| Max hit — ranged | 97 | [W1] |
| Max hit — magic | 95 | [W1] |
| Attack range | **15 tiles** | [W1] |
| Aggressive | Yes | [W1] |
| Slayer XP | 25,250 | [W1] |
| Elemental weakness | Water, 40% (25 Jun 2025) | [W1] |

Cache cross-check [C]: `[tzhaar_fightcave_swarm_boss]` in `all.npc` carries
`size=5`, `vislevel=702`, `stat4=250` (hp), `stat1=640`, `stat3=960`,
`stat2=480`, `stat6=480`, `stat5=960`, `param=magicattack,60`. Every wiki stat
matches the cache row exactly.

### 1.2 Attack cycle

1. Jad attacks every **8 ticks** at range [W1].
2. **Melee is a separate, faster clock**: "Jad can attack with melee every 4
   ticks, as opposed to its normal 8 tick attack speed" [W1] [W2]. Melee is
   only used when the player is adjacent [W1] [W5].
3. Style selection:
   - Player **not** adjacent → Jad "will randomly alternate between Magic and
     Ranged attacks" [W5]. Melee is impossible at range.
   - Player adjacent → "When the player is in melee range, TzTok-Jad will use
     Magic and Ranged attacks as well" [W5], i.e. all three styles are live and
     melee is only a fraction of the rolls.
4. **Melee has no tell and no delay.** "Unlike the Magic and Ranged attacks,
   the Melee attack is instant" [W1]; "this attack has no warning for you to
   pray for" [W5]. Its hit delay is therefore 0 ticks, consistent with the
   general rule that melee attacks damage on the first tick they are processed
   [W8].
5. **Magic and Ranged are telegraphed by the attack animation**, and the
   animation is the *only* warning — "an emote precedes the attack, telling the
   player which protection Prayer to use" [W5].
6. Jad's magic attack "has no delay if the player is directly adjacent to the
   boss and therefore cannot be tick-eaten in this situation" [W9] — the
   magic hit delay is distance-derived and collapses to 0 at range 1.

### 1.3 The three attacks

**Melee — a forward bite.** "TzTok-Jad thrusts forward with a sharp bite. He
only does this if the player is within melee distance" [W1].

**Magic — a fireball from the mouth.** "TzTok-Jad rears up on his hind legs and
dangles his forward legs for a few seconds before launching a fireball from his
mouth at the player" [W1]. "You can hear him inhale/growl loudly … while
rearing up and before he actually attacks" [W5]. Protect from Magic must be on
*while he is rearing up*; once the fire starts it is too late [W5].

**Ranged — a boulder from the ceiling.** "TzTok-Jad rears up on his hind legs,
then slams down his front legs onto the ground causing a boulder to fall on the
player (it is not possible to evade this boulder by running)" [W1]. "Large
cracks appear in the floor at his feet — the player must turn on their Prayer
now" [W5]. The boulder tracks the player rather than a tile, which is why
running does not dodge it. Note the audio quirk: "the sound from this attack
only occurs **after** the hit has been registered" [W5].

### 1.4 Animation lengths [C]

Jad's rig in this cache is the `lordmagmus_*` family (the working title "Lord
Magmus" is still the name in the configuration files [W1]).

| Sequence | id | Frames | Cycles | Ticks | ms | Role |
|---|---|---|---|---|---|---|
| `lordmagmus_ready` | 2650 | 16 | 81 | 2.70 | 1620 | idle |
| `lordmagmus_walk` | 2651 | 16 | 112 | 3.73 | 2240 | walk |
| `lordmagmus_smash` | 2652 | 12 | 55 | 1.83 | 1100 | **Ranged** (slam) |
| `lordmagmus_defend` | 2653 | 17 | 95 | 3.17 | 1900 | flinch |
| `lordmagmus_death` | 2654 | 28 | 149 (+3392 hold) | 4.97 | 2980 | death |
| `lordmagmus_attack` | 2655 | 14 | 72 | 2.40 | 1440 | **Melee** (bite) |
| `lordmagmus_fire` | 2656 | 31 | 155 | 5.17 | 3100 | **Magic** (fireball) |

Two things fall straight out of the table and both match the wiki's prose:

- The magic wind-up is **long** — 5.17 ticks, and the sequence is a
  palindrome (frames 73→88 then 88→73), i.e. it rears up over ~2.6 ticks and
  comes back down over ~2.6. That is the "dangles his forward legs for a few
  seconds" the player prays through [W1].
- The ranged slam is **short** — 1.83 ticks, under a third of the magic
  wind-up. Ranged gives the player far less warning than magic, which is why
  [W5] recommends holding Protect from Missiles as the default.

`lordmagmus_smash`, `lordmagmus_attack` and `lordmagmus_fire` all carry
`forcedpriority=10 precedence=2 priority=2` [C] — the attack animation cannot
be interrupted by the flinch, so a hit landing mid-cast never hides the tell.

Associated graphics [C]: `tzhaar_fire_spit_launch` (447, muzzle flare),
`tzhaar_fire_spit_travel` / `_follow_travel` / `_end_travel` (448 / 449 / 450,
the three-projectile fan), `tzhaar_rock_smash` (451, the boulder impact),
`firewave_impact` (157, the on-player hit graphic).

### 1.5 Hit delay — what is and is not known

The wiki does not publish a per-attack hit delay for Jad. What it does publish:

- Melee: 0 ticks [W1] [W8].
- Magic: 0 at range 1 [W9], distance-derived above that.
- The general magic projectile table, `1 + floor((1 + distance) / 3)` ticks
  [W8], which gives 2 ticks at distance 2–4, 3 at 5–7, 4 at 8–10.
- The general ranged projectile table, `1 + floor((3 + distance) / 6)` [W8].

Those tables are documented for *player* attacks; the wiki does not assert
they govern Jad. This is **unstated hole #1** (see §6).

### 1.6 Yt-HurKot (Fight Caves healers)

| Field | Value | Source |
|---|---|---|
| npc id | 3128 | [W3] |
| Combat level | 108 | [W3] |
| Size | 1×1 | [W3] |
| Hitpoints | 60 | [W3] [W4] |
| Attack / Strength / Defence | 140 / 100 / 60 | [W3] |
| Magic / Ranged | 120 / 120 | [W3] |
| Attack style | Crush | [W3] |
| Attack speed | **4 ticks** | [W3] |
| Max hit | 14 | [W3] |
| Melee defence bonuses | 0 | [W3] |
| Magic / Ranged defence bonuses | +100 each | [W3] |
| Aggressive on spawn | No | [W3] |
| Slayer XP | 60 | [W3] |

Behaviour, in the order it happens:

1. **Spawn trigger.** "They spawn when their respective Jad falls to or below
   50% of their maximum hitpoints" [W3]. Two other pages instead say "150
   health points" [W4] and "below 150 health" [W1] — 150 is *not* half of 250.
   This is **unstated hole #2**; see §6 for which one this tree implements.
2. **Count and position.** Four spawn, at four fixed points in the arena [W3].
   The wiki's spawn diagram marks them **SE, S, NW and SW** [W3] — the same
   named spawn points the waves themselves use [W4]. They are not spawned next
   to Jad; they walk in.
3. **Healing.** "It's a flat +5 (per NPC), running every 4 ticks" — Mod Ash,
   quoted at [W3] [W12]. So four healers restore **20 hp every 4 ticks**, i.e.
   5 hp/tick, which is why out-damaging them is a real strategy [W3].
4. **Healing range.** Two statements, and the more specific one is on the Fight
   Cave page: healers "will heal Jad if they are within melee range of it, or
   if they are in combat with the player and are within 4 tiles of Jad" [W4].
   The Yt-HurKot page compresses this to "unable to restore Jad's health if
   they are more than 5 tiles away" [W3]. The important consequence is stated
   identically by both and by [W5]: **tanking a healer does not stop it
   healing** — "If you take the attention of the Yt-HurKots and they are
   hitting you, they WILL still heal Jad if they are close enough to him" [W1].
5. **Self-healing.** "Will sometimes heal themselves or adjacent Yt-HurKot that
   are below half hp if they cannot reach Jad" [W4].
6. **Aggression.** Not aggressive on spawn. "Attacking them will cause them to
   stop healing and immediately attempt to target the player with melee. If the
   player is too far from the Yt-HurKot after attacking them, they will quickly
   lose aggression and re-target Jad" [W3]. One hit is enough to pull one [W5].
7. **Respawn.** "Fight Cave healers will respawn if they manage to restore
   Jad's health to full before they were killed" [W3] — a fresh set of four
   spawns the next time Jad drops to the threshold [W1] [W4]. If they are
   distracted *before* healing him to full, killed healers do **not** come back
   [W1].

Healer animations [C] (the `lizard_cleric_*` family, shared with Yt-MejKot):

| Sequence | id | Frames | Cycles | Ticks | Role |
|---|---|---|---|---|---|
| `lizard_cleric_walk` | 2634 | 16 | 128 | 4.27 | walk |
| `lizard_cleric_defend` | 2635 | 17 | 61 | 2.03 | flinch |
| `lizard_cleric_ready` | 2636 | 12 | 132 | 4.40 | idle |
| `lizard_cleric_attack` | 2637 | 14 | 38 | 1.27 | melee swing |
| `lizard_cleric_death` | 2638 | 22 | 90 (+hold) | 3.00 | death |
| `lizard_cleric_heal` | 2639 | 10 | 38 | 1.27 | **heal cast** |
| `lizard_cleric_heal_spot` | 2640 | 49 | 147 | 4.90 | heal graphic (spotanim 444) |

The heal cast is 1.27 ticks, comfortably inside the 4-tick heal cadence.

### 1.7 Encounter frame

- Jad is wave 63, the final wave [W1] [W4].
- He spawns where the off-coloured (orange) Ket-Zek spawned on wave 62, and
  equivalently where the first Tz-Kek spawned on wave 3 [W4] [W5] [W14].
- Rewards: fire cape + 8,032 Tokkul (16,064 with the Elite Karamja Diary)
  [W1]; TzRek-Jad pet at 1/200, or 1/100 on a TzHaar Slayer task [W1].

---

## 2. JalTok-Jad (Inferno / TzHaar-Ket-Rak's Challenges)

### 2.1 Record

| Field | Value | Source |
|---|---|---|
| npc ids | 7700 (waves 67/68), 7704 (wave 69, Zuk phase), 10623 (Ket-Rak's) | [W2] |
| Combat level | 900 | [W2] |
| Size | 5×5 | [W2] |
| Hitpoints | 350 | [W2] |
| Attack / Strength / Defence | 750 / 1020 / 480 | [W2] |
| Magic / Ranged | 510 / 1020 | [W2] |
| Magic attack bonus | +100 | [W2] |
| Magic damage bonus | +75 | [W2] |
| Ranged attack bonus | +80 | [W2] |
| Attack styles | Ranged, Magic, Stab | [W2] |
| Attack speed | **8 ticks** (9 on wave 68) | [W2] [W7] |
| Melee attack speed | **4 ticks** | [W2] |
| Max hit (all styles) | 113 | [W2] |
| Poison / venom immunity | Yes | [W2] |
| Slayer XP | 350 | [W2] |
| Elemental weakness | Water, 40% (25 Jun 2025) | [W2] |

"It behaves nearly identically to TzTok-Jad from the TzHaar Fight Caves,
although its stats are higher" [W7]. Every mechanic in §1.2–§1.3 carries over:
three styles, melee only when adjacent and on a 4-tick clock [W2], the same
two telegraphs.

The attack-animation tells, restated for the Inferno model [W7]:

- **Magic** — "it rises on its hind legs and casts a fire spell from its mouth".
- **Ranged** — "it rises on its hind legs only for a brief moment before
  slamming the ground, causing a boulder from the ceiling to hit the player".
- **Melee** — "it bashes its head, which has to be preemptively protected from".

### 2.2 Animation lengths [C]

| Sequence | id | Frames | Cycles | Ticks | ms | Role |
|---|---|---|---|---|---|---|
| `jaltokjad_walk` | 7588 | 32 | 96 | 3.20 | 1920 | walk |
| `jaltokjad_ready` | 7589 | 16 | 65 | 2.17 | 1300 | idle |
| `jaltokjad_attack_melee` | 7590 | 24 | 72 | 2.40 | 1440 | **Melee** |
| `jaltokjad_defend` | 7591 | 18 | 55 | 1.83 | 1100 | flinch |
| `jaltokjad_attack_magic` | 7592 | 50 | 160 | 5.33 | 3200 | **Magic** |
| `jaltokjad_attack_ranged` | 7593 | 18 | 55 | 1.83 | 1100 | **Ranged** |
| `jaltokjad_death` | 7594 | 51 | 154 | 5.13 | 3080 | death |

The shape is the same as TzTok-Jad's to within a few cycles — magic ≈ 5.2–5.3
ticks, ranged ≈ 1.83 ticks, melee 2.40 ticks in both — which is the strongest
available confirmation that "nearly identically" [W7] is literal. `_magic` and
`_ranged` both carry `forcedpriority=10 precedence=2 priority=2` [C].

### 2.3 Where JalTok-Jad appears

Five JalTok-Jads appear across the end of the Inferno [W2]:

| Wave | Count | Attack speed | Healers each | Notes |
|---|---|---|---|---|
| 67 | 1 | 8 ticks | **5** | Lone Jad [W2] [W6] |
| 68 | 3 | **9 ticks** | **3** | Staggered 3 ticks apart [W2] [W6] [W7] |
| 69 | 1 | 8 ticks | **3** | Zuk phase; targets the shield first [W2] |

**Wave 68 — the triple.** This is the one place the timing is unusual and the
wiki is explicit about it:

> "The Jads will spawn in a triangle formation around the centre, have an
> attack speed of 9 (5.4 seconds) rather than the typical 8. The first Jad
> attacks normally, and the second and third ones will attack 3 ticks
> afterwards, resulting in a 9 tick cycle (with 3 ticks to react to each Jad)."
> — [W7]

So the schedule is Jad A at *t*, Jad B at *t*+3, Jad C at *t*+6, each repeating
on a 9-tick period. The player therefore sees one incoming attack every 3
ticks, evenly spaced, forever. [W2] and [W6] state the same thing more briefly
("The Jads attack three ticks apart"). "The first Jad will engage normally,
while the other two will have a slight delay before engaging the player so that
all three can be flicked at once" [W2].

**Wave 69 — the Zuk-phase Jad.** "This Jad will appear when TzKal-Zuk reaches
480 or less hitpoints and will initially attempt to target the Ancestral Glyph
upon spawning. Aggression can be changed by attacking it, and it summons 3
Yt-HurKot upon reaching half health, who always spawn north of it" [W2]. The
glyph is the 600-hitpoint shield the player hides behind [W11]; a Jad left on
it will bring it down. Spawning Jad also unpauses and adds 1:45 to the
Jal-Xil/Jal-Zek set timer, which is paused between 600 and 480 Zuk hitpoints
[W6].

**TzHaar-Ket-Rak's Challenges.** One to six JalTok-Jads, challenge *n* fielding
*n* of them; challenges 3–6 require a prior Inferno completion [W2] [W10].
There is also an endurance challenge where a fresh Jad replaces each one killed
until the player dies [W10]. In this variant "the healers have higher stats,
but their mechanics are more forgiving — needing to be untagged and directly
next to Jad in order to heal" [W1].

### 2.4 Yt-HurKot (Inferno healers)

| Field | Value | Source |
|---|---|---|
| npc ids | 7701, 7705, 10624 | [W3] |
| Combat level | 141 | [W3] |
| Hitpoints | 90 | [W3] |
| Attack / Strength / Defence | 165 / 125 / 100 | [W3] |
| Magic / Ranged | 150 / 150 | [W3] |
| Magic attack bonus | +100 | [W3] |
| Ranged attack bonus | +80 | [W3] |
| Attack style | Crush | [W3] |
| Attack speed | **4 ticks** | [W3] |
| Max hit | 18 | [W3] |
| Melee defence bonuses | 0 | [W3] |
| Magic / Ranged / Light / Heavy defence | +130 each | [W3] |

Differences from the Fight Caves set, all four of which matter:

1. **Heal amount.** "Its other heal code appears to do 15-24 every 4 ticks" —
   Mod Ash, quoted at [W3] [W13]. Not the flat +5 of the Fight Caves. Five
   healers on wave 67 therefore restore **75–120 hp every 4 ticks** against a
   350-hp Jad: ignoring them is not an option.
2. **Spawn position.** "Inferno healers will spawn next to their respective
   Jad" [W3], rather than at fixed arena points. On wave 69 they "always spawn
   north of it" [W2].
3. **Heal range.** "Inferno healers will restore their Jad's health when in
   melee distance of them" [W3] — adjacency to the 5×5 footprint, not the Fight
   Caves' 4–5 tile radius.
4. **No respawn.** "Inferno healers do not [respawn]" [W3]. Killing them is
   permanent; a Jad healed back to full does not get a second set.

Aggression works the same way: not aggressive on spawn, one hit pulls them onto
the player with melee, and moving too far after tagging drops the aggression
and sends them back to Jad [W3].

The Inferno Yt-HurKot reuses the `lizard_cleric_*` animation family [C], so the
timings in §1.6 apply unchanged.

### 2.5 Not to be confused with Jal-MejJak

Zuk's own healers, spawned at 240 Zuk hitpoints, are **Jal-MejJak**, not
Yt-HurKot: four of them, healing "around 15-25 hitpoints every three ticks",
with an unprotectable 3×3 AoE spark attack for 5–10 that they use *after* being
tagged [W7]. Different npc, different cadence, different heal band — see
`inferno_adds.rs2`, not this document.

---

## 3. Side-by-side

| | TzTok-Jad | JalTok-Jad |
|---|---|---|
| Level / HP | 702 / 250 | 900 / 350 |
| Attack speed | 8 (melee 4) | 8, or 9 on wave 68 (melee 4) |
| Max hit | 97 / 97 / 95 (melee/range/magic) | 113 all styles |
| Attack range | 15 tiles | unstated (hole #3) |
| Healer count | 4 | 5 (w67), 3 (w68 each), 3 (w69) |
| Healer level / HP | 108 / 60 | 141 / 90 |
| Healer max hit | 14 | 18 |
| Healer heal | flat +5 per 4 ticks | 15–24 per 4 ticks |
| Healer heal range | melee, or ≤4 tiles while tanking | melee only |
| Healers respawn | Yes, if Jad reaches full HP | No |
| Healer spawn point | 4 fixed arena points (SE/S/NW/SW) | next to their Jad |
| Magic wind-up | 5.17 ticks (seq 2656) | 5.33 ticks (seq 7592) |
| Ranged wind-up | 1.83 ticks (seq 2652) | 1.83 ticks (seq 7593) |
| Melee swing | 2.40 ticks (seq 2655) | 2.40 ticks (seq 7590) |

---

## 4. Reference list

| # | Page | URL |
|---|---|---|
| W1 | TzTok-Jad | https://oldschool.runescape.wiki/w/TzTok-Jad |
| W2 | JalTok-Jad | https://oldschool.runescape.wiki/w/JalTok-Jad |
| W3 | Yt-HurKot | https://oldschool.runescape.wiki/w/Yt-HurKot |
| W4 | TzHaar Fight Cave | https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave |
| W5 | TzHaar Fight Cave/Strategies | https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave/Strategies |
| W6 | Inferno | https://oldschool.runescape.wiki/w/Inferno |
| W7 | Inferno/Strategies | https://oldschool.runescape.wiki/w/Inferno/Strategies |
| W8 | Hit delay | https://oldschool.runescape.wiki/w/Hit_delay |
| W9 | Tick eating | https://oldschool.runescape.wiki/w/Tick_eating |
| W10 | TzHaar-Ket-Rak's Challenges | https://oldschool.runescape.wiki/w/TzHaar-Ket-Rak%27s_Challenges |
| W11 | Ancestral Glyph | https://oldschool.runescape.wiki/w/Ancestral_Glyph |
| W12 | Mod Ash, 6 May 2020 — "It's a flat +5 (per NPC), running every 4 ticks." | https://twitter.com/JagexAsh/status/1258128136388173824 (archived: https://reldo.runescape.wiki/i/97816) |
| W13 | Mod Ash, 30 Dec 2020 — "Its other heal code appears to do 15-24 every 4 ticks." | https://twitter.com/JagexAsh/status/1344226351654514688 (archived: https://reldo.runescape.wiki/i/106494) |
| W14 | TzHaar Fight Cave/Rotations | https://oldschool.runescape.wiki/w/TzHaar_Fight_Cave/Rotations |

Cache sources [C], all under `OSRS-Content/osrs239-content/configs/`:
`all.npc` (`[tzhaar_fightcave_swarm_boss]`, `[tzhaar_fightcave_swarm_boss_cleric]`),
`all.seq` + `all.seq.compack` (the `lordmagmus_*`, `jaltokjad_*` and
`lizard_cleric_*` families), `all.spotanim.compack` (447–451, 444, 157).

---

## 5. What this tree implements

The Fight Caves and the Inferno are separate ports and were in very different
states before this pass.

### 5.1 Fight Caves — `server/scripts/minigames/minigame_fightcave/`

Before: `fightcave.rs2` was a wave table and nothing else. Jad
(`tzhaar_fightcave_swarm_boss`) had **no combat script at all** — no
`[ai_timer]`, no attack handler, no healers — so wave 63 stood up a 250-hp
melee-default monster and the Yt-HurKot type was never spawned by anything.
`[ai_queue3,tzhaar_fightcave_swarm_boss_cleric]` was bound to the wave-credit
proc for a monster that could not exist.

Now: `fightcave_jad.rs2` implements §1.2–§1.6 — the three attacks with their
own animations and graphics, melee on its own 4-tick clock, the four healers at
the four arena spawn points, the flat +5/4-tick heal, and the respawn-on-full
rule. Records for both npcs live in `configs/fightcave.npc`.

### 5.2 Inferno — `server/scripts/minigames/minigame_inferno/`

`inferno_jad.rs2` already had the right shape (the engine's native npc combat
rather than a second timer-driven one). Six things were wrong against the
sources above and are fixed:

1. **Healer heal was 1–10, not 15–24** [W3] [W13]. `[proc,inferno_healer_heal]`
   queued `random(10) + 1`. Against a 350-hp Jad with five healers that is a
   third of the intended pressure.
2. **Wave 68 used attack speed 8 and no stagger** [W2] [W7]. All three Jads
   swung together on the record's 8 — not a harder version of that wave but an
   unsurvivable one, three unprotectable 113s on one tick. Now 9 with a
   0/3/6-tick offset handed out at the spawn site.
3. **Only one of the three wave-68 Jads could ever summon healers.** The
   "already spawned" flag was the player varp `%inferno_jad_healers_spawned`,
   one flag for the whole encounter; the second and third Jad were healerless.
   It is now an `npc_var` slot on the Jad itself, and the varp is deleted.
4. **The half-health test was strict.** `< hp/2` spawns at 174, not 175;
   the wiki says "falls to or below 50%" [W3].
5. **Melee did not use its 4-tick clock** [W2]. Jad melee'd on the record's 8.
6. **Every swing at the player cost two extra ticks.**
   `[ai_opplayer2,inferno_jad]` was `npc_setmode(applayer2)` with the swing in
   `[ai_applayer2]`. The engine dispatches an AP mode *before* it decrements
   the attack clock and returns without spending that tick, so the cycle was
   `attackrate + 2` — a Jad whose record says 8 swung every ten ticks, while
   the same Jad's shield attack (`[ai_opnpc2]`, which has always called its
   proc directly) swung on the record. Both Jad copies now state the swing in
   `[ai_opplayer2]`. Measured, not reasoned: see §5.3.

Deliberately unchanged: everything about the Zuk-phase Jad's targeting of the
Ancestral Glyph, the three-projectile magic fan and its source height, the
per-Jad healer ownership tag, and the `npc_delay`-free structure the encounter
was rebuilt around. Those are all correct and the file's own header explains
why each one is the way it is.

### 5.3 What was measured, and how

The Inferno changes are pinned in `mock230_world_selftest`'s rev-239 Zuk
section, extending the existing `::zukstill 4` fixture: after the shield dies
and Jad turns on the player, 60 ticks are sampled and two things are asserted —
that its mode is never an AP mode (item 6 above), and that the gap between
consecutive swings is one of the two numbers content states, 8 from the record
or 4 from `~inferno_jad_pace`'s melee branch. Both pass. That second assertion
is also the fence for §7's engine fix: 9 there means the attack clock is a
countdown again, 10 means the AP redirect is back.

The Fight Caves side has its own stanza and its own fixture, `::fightcave
<wave>`, added beside `::inferno <wave>` for it. It asserts the authored record
reaches the server (attack speed 8, attack range 15, 250 hitpoints — the range
is the one field the npc generator cannot produce and is what would read 0 if
the authored block lost the directory-order race against the two generated
`.npc` files), that exactly four healers spawn at half health, that a heal is
the flat +5 and not the Inferno's roll, and that a Jad healed to full summons a
second set.

---

## 6. Unstated — what neither source settles

1. **Jad's hit delay per distance.** Neither wiki page gives the tick count
   between the attack animation starting and the damage landing for either Jad.
   [W8]'s tables are documented for player weapons. This tree keeps the flight
   time the projectile itself measures (`~npc_projectile` / `~player_projectile`
   return the flight in client cycles, divided by 30 for the queue delay), which
   makes the delay distance-proportional in the same shape as [W8] without
   claiming to reproduce a number nobody published.
2. **The healer threshold: 125 or 150?** [W3] says "to or below 50% of their
   maximum hitpoints" (125 of 250). [W1] says "below 150 health" and [W4] says
   "upon reaching 150 health points". 150 is 60%, not 50%. This tree implements
   **50%** — it is the statement on the page that is *about* the healers, it is
   the only one of the three that generalises to JalTok-Jad (whose own page
   says "half health" [W2]), and it is what the Inferno side already did.
   The Fight Caves 150 is recorded here in case a capture ever settles it.
3. **JalTok-Jad's attack range.** [W1] gives TzTok-Jad 15 tiles; no page gives
   JalTok-Jad a number. The Inferno port uses a 40-tile scan (`^inferno_scan_range`)
   for every long-range monster in the arena, sourced from [W7]'s "longer than
   10 tiles", and Jad shares it.

---

## 7. The engine's attack clock — found here, fixed here

Everything above is about content stating the right numbers. It only means
anything if the engine spends them correctly, and it did not.

**Every npc in the tree swung on `attackrate + 1` ticks, not `attackrate`.**
`mock230_combat_npc_tick` and its npc-versus-npc twin counted a countdown down
*before* testing it against zero:

```c
if( npc->attack_clock > 0 ) { npc->attack_clock--; return; }
npc->attack_clock = npc_def(npc)->attackrate;   /* then fire the swing */
```

A reload of N is spent over N whole ticks and only the tick *after* them may
swing. Measured on the Zuk-phase Jad before the fix: reloads of 4 produced
swings 5 apart, reloads of 8 produced swings 9 apart. `npc_attackdelay(N)`
inherited it, and so did the retaliation flinch.

### What the reference does

LostCity keeps no countdown. It keeps a **deadline** on the map clock — armed
with `add`, read with `>`:

```
%npc_action_delay = add(map_clock, npc_param(attackrate));   // npc_combat_melee.rs2
...
if (%npc_action_delay > map_clock) return;                   // ~60 monster scripts
```

A swing on tick *T* sets the deadline to *T+N*; on *T+N* the guard is false and
the npc swings. The interval is exactly N, with no arithmetic anywhere that
could be off by one.

### The fix

`attack_clock` is now that deadline, in `srv->tick` — which is also how
`poison_clock` and `death_tick` are already written two fields away in the same
struct:

```c
if( srv->tick < npc->attack_clock )
    return;
npc->attack_clock = srv->tick + npc_def(npc)->attackrate;
```

A deadline rather than a corrected countdown (`attackrate - 1`) because the ±1
has to live in one place or it lives in all of them: the swing reload, the
flinch (`+ attackrate / 2`, the reference's own halving) and `npc_attackdelay`
would each need their own correction, and the one that got it wrong would be a
second silent off-by-one. Zero keeps meaning "swing at the first opportunity" —
tick 0 is always in the past — so every `= 0` site (`npc_attackplayer`,
`npc_attacknpc`, `maybe_aggress`, `mock230_combat_stop_npc`, respawn) is
untouched, and a target switch mid-cooldown carries the deadline over for free,
which is the behaviour `mock230_combat_hit_npc` had to argue for in prose
before.

### Verified

- **Default lane:** a new zero-cost assertion in the cow-retaliation stanza —
  folded into the tick loop that already runs, so the suite's timing is
  unchanged — pins that a cow which swings on tick *T* arms *T + attackrate*.
  Failure set identical to before the change (3, all pre-existing and all from
  unrelated in-flight work).
- **rev-239 lane:** the Jad stanza now measures the interval between
  consecutive swings end to end and accepts only 8 (the record) or 4 (the melee
  branch). 224 failures against a 225 baseline: **no new failures, and one
  fixed** — `the shot should land inside the window (flinch tick -1)`, which
  was the flinch paying the same off-by-one.
- `npc_attackdelay(7)`'s own assertion now reads `srv->tick + 7`. It used to
  assert a bare `7` and passed while the swing it bought was eight ticks away.

One harness fault surfaced with it and is fixed: `selftest_park_player` topped
`hitpoints` up without `mock230_combat_sync_hitpoints`, leaving the hitpoints
*stat* stale. Whether the later "the hitpoints stat should track the player's
hitpoints" check passed depended on which side of that reset the previous
section's last npc swing landed — latent for as long as it has existed, and
exposed the moment every fight in the suite shifted by a tick.
