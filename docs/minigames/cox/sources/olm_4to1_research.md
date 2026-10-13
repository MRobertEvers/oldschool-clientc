# Solo Great Olm, "4:1" method, budget setup, and tick-level mechanics a bot needs

Researched 2026-10-13. Each fact carries a source tag. The source list is at the end (S-numbers).
Terms: **tick** = 0.6 s game tick. **Step** = one Olm head action slot (one every 4 ticks).
Frame of reference: unless stated otherwise, "left/right" means **as the player stands in the
arena facing Olm** (this is how the community guides talk). Olm's own left/right is the mirror
image. The two wiki pages that list safespots use different frames, which is why they seem to
contradict each other (see 4.3).

## Authority ranking (where sources disagree)

1. **Mod Ash tweets** (Jagex developer reading the real code). The wiki quotes them [S1, S2]. Treat as
   ground truth: 1/5 style-switch chance, 1/3 (1/2 in the final phase) phase-attack chance, clench
   threshold 1/20 of base HP.
2. **OSRS Wiki** pages Great Olm [S1], Chambers of Xeric/Strategies [S2], Perfect Olm (Solo) [S3],
   CM Strategies [S4], the CoX update log [S5]. Mostly reliable, but some numbers are rounded ("~30 s")
   and some frames of reference are mixed.
3. **Wiki DPS calculator source** (weirdgloop) [S6]: the exact HP formula for Olm.
4. **Experienced solo raiders' videos** [S10–S24]. These are the best sources for *how* 4:1 is
   executed. They are empirical and agree with each other on the core loop.
5. **Client plugin built against the live game** (OpenOSRS "coxhelper" Olm tracker) [S8]: the real
   animation IDs, the 4-tick cadence, the first-attack delay after the head rises, 45-tick cripple.
6. **RuneScape private-server reimplementations** (Zenyte/Near-Reality by "Kris", 2018 [S7]; Kronos
   [S9]). **Not authoritative** (written by reverse-engineering), but they are the only *explicit*
   statement of the head-vision boxes and the skip/catch-up logic. The two independent
   implementations agree on the boxes, and the boxes reproduce every safespot on the wiki and every
   tile the guides use. Use them as a model to be checked against the wiki, not as truth.
7. Low-quality, ignore: mypvm.com "CoX Guide 2026" (lists 5 invented phases) [S27]; battle-scape.com
   (an RSPS guide) [S28].

---

## 1. What "4:1" means

### 1.1 Definition
- An "X:Y" method means **the player attacks X times while Olm attacks the player Y times**, then the cycle
  repeats. 4:1 = four player attacks per one Olm hit taken [S2, S11, S12, S13].
- 4:1 is the **melee-hand** method with a **4-tick melee weapon** (dragon hunter lance, whip,
  tentacle, hasta, rapier) [S2, S11, S14, S19]. The 5-tick fang cannot be your only melee weapon:
  "to do Olm properly you need a four tick weapon" [S19].
- Why it works: Olm's head runs a **4-step cycle, one step every 4 ticks (16 ticks per loop)**:
  **Auto, Null, Auto, Special** ("A-N-A-S") [S11, S12, S19]. A 4-tick weapon gives exactly 4 swings per
  loop. The player stands where the head can see them **only at the first Auto**. At the Special step
  and at the second Auto step the player is outside the head's view, so the head spends that step
  turning instead of acting. Each loop: one auto taken, the special skipped, the second auto skipped
  [S12, S11, S16].
- A correct 4:1 "should avoid all attacks outside of the flame walls and acid pools for the melee hand"
  [S3]. On the CoX strategy page: "when used correctly, will prevent Olm from using a special attack and
  the player will only face standard attacks" [S2].

### 1.2 Olm's real attack rotation (authoritative form)
The wiki lists 12 steps, each 4 ticks apart [S1]:

```
standard, empty, standard, CRYSTAL BURST,
standard, empty, standard, LIGHTNING,
standard, empty, standard, TELEPORT,   (loop)
```
- The CoX strategies page shows the same loop starting at a different point [S2]. Same cycle, different
  start.
- In the **penultimate phase (P3)** a fourth special is added: the left-hand **infinity/heal**, giving
  crystal, lightning, teleport, heal [S12, S17, S8]. The Zenyte model uses a 16-step loop that ends
  `... auto, null, auto, LEFT_CLAW_PROTECTION` [S7].
- **Specials always come in this order**: crystal burst, lightning, teleport (then heal in P3) [S12, S17, S1].
  On these steps the melee hand briefly shows a crystal, bolt, swirl or infinity icon [S1, S2].
- **Catch-up rule (authoritative wiki):** if a standard attack was skipped because no player was in
  view, the next step also fires a standard attack on top of its own action. That is how "auto + special
  on the same step" or "two autos back to back" happens. Catch-up only covers **one** skipped step and
  **never** restores a skipped special. "A well-timed skip by the player can completely prevent a
  special attack from occurring at all" [S1, S2].
- **Turning rule (authoritative wiki):** Olm faces the zone (left/middle/right) holding the most players.
  If its current zone has nobody in it, it turns and **that step's attack is skipped**. "If a player is
  in his attack zone but Olm swerves his head regardless, no attacks are skipped" [S1].
- **Hand damage steers the turn (wiki):** "If the right hand is damaged within 4 ticks and Olm does not
  scan a player, it will always turn its head to the right, and vice-versa for the left. This factors in
  all damage, such as thralls and burns" [S2]. Here "right/left" is Olm's own: right hand = mage hand.
  A **0 (splash/miss) does not count**. Guides agree that a splash on the mage hand makes the head go to
  the middle instead of the far side [S17, S19, S13]. Thralls ignore defence, so they make the head
  turn predictably [S19, S24].

### 1.3 Tick timing: when to swing, when to step
- **Mage hand:** attack **on the same tick** Olm acts (you predict the head turn rather than react to it).
  **Melee hand:** attack **one tick after** Olm's attack [S10, S11, S12].
- Practical trigger used by every guide: stand next to the thumb, hover the melee hand, and click the
  instant the head's attack animation (the "jolt"/"bob") starts. Your swing then lands one tick after
  Olm [S10, S11, S20, S17]. Aatykon: "you have 0.6 seconds, plenty of time" [S20].
- WildMudkip's equivalent: click the hand **one tick before** the head moves, so that your character
  moves on the same tick the head moves [S13].
- Within a phase, **the tick Olm acts on stays fixed for the whole phase** (VideoGameBot uses a 4-tick
  visual metronome: "he turns his head on 1 ... on this phase two he was turning on 3 ... it stays that
  way until both hands die") [S15]. Plugin data: after the head-rise animation (7335 / enraged 7383)
  the first attack comes **4 ticks** later in the first phase and **8 ticks** later in later phases [S8].

### 1.4 The 4:1 pathway (tiles and per-hit positions)
Tile-marker exports from two guides give exact tiles in region **12889** (Olm room template;
world = (3200 + regionX, 5696 + regionY)). They agree [S11-paste, S17-doc]:

| Role | Olm on WEST wall (player column x=28) | Olm on EAST wall (player column x=37) |
|---|---|---|
| "Mid skip" (just left of head centre, player view) | (28,43) | (37,45) |
| "Thumb" of melee hand = hit 1 and hit 4 | (28,47) | (37,41) |
| Hit 3 tile (Kaoz) | (28,48) | (37,40) |
| "Right side" / ring finger / pinky = hit 2 | (28,50) | (37,38) |
| "Mage side" far tile | (28,38) | (37,50) |
| "Flame skip (4:1)" tile | (29,51) | (36,36) |
| P2/P3 mage start | (30,48) | (35,40) |

The east column mirrors the west: y_east = 88 − y_west. The melee hand is on the player's **right**
("technically his left hand but will be on the right side when facing him") [S17]. When Olm is on the
west wall, the melee hand is the **north** hand; on the east wall it is the **south** hand.

**Loop (ThePerfectG / WildMudkip / Kaoz) [S11, S13, S17]:**
1. From mid skip, click the melee hand. **Hit 1** lands at the thumb.
2. Run straight to the ring-finger/"right side" tile. **Hit 2** there. ("You always want to run to the far
   right tile, the ring finger, after your first hit ... click to the ring finger as soon as your true tile
   is on the thumb tile marker" [S13].)
3. **Hit 3** at the right side, or one or two tiles back toward the head (Kaoz's tile "3"). **Olm's one
   auto lands between hit 2 and hit 3** [S11, S13].
4. Move to the thumb. **Hit 4** at the thumb. Olm's **null** falls between hits 3 and 4 [S11, S13].
5. As soon as hit 4 fires (xp drop or attack animation), click mid skip, **touch it without stopping**,
   and click the hand again. "You don't want to stall in that tile, you need to be moving the whole time"
   [S13]. The head turns from right to middle here: this is the **second auto being skipped** [S11].
6. Repeat. Running from mid skip to the right side makes the head turn middle→right: this is **the
   special being skipped** [S11].

**Derived tick timeline.** Combine the wiki rotation with the head-view boxes in 4.1 and set t=0 at a
special-step tick. Olm on west, 4-tick weapon:

| tick | Olm step | head before → after | player must be | player action |
|---|---|---|---|---|
| 0 | SPECIAL | MIDDLE → turns RIGHT (special skipped) | y=50 (outside MIDDLE view y39–49) | — |
| 1 | — | | (28,50) | **hit 2** |
| 4 | AUTO #1 | RIGHT (sees you) → **you take this hit** | y 48–50 | — |
| 5 | — | | (28,48–50) | **hit 3** |
| 8 | NULL | stays RIGHT if the melee hand took damage since t=4, else turns MIDDLE early ("noodling") | y≈47–48 | — |
| 9 | — | | (28,47) thumb | **hit 4** |
| 10–11 | — | | run to (28,43) | — |
| 12 | AUTO #2 | RIGHT view is y44–52; you are at y43 → no one seen → turns MIDDLE, **auto skipped** | y ≤ 43 | leave on this tick |
| 13 | — | | (28,47) thumb | **hit 1** |
| 14–15 | — | | run to (28,50) | — |
| 16 | SPECIAL | (loop) | | |

This table is **derived**. It is consistent with all the guides (auto between hits 2 and 3, null between
hits 3 and 4, swings one tick after Olm's steps). The exact y-limits come from the reimplementation
boxes (4.1), which reproduce the wiki safespots, so they are the best available. OSRS processes NPCs
before players within a tick, so Olm judges the position you held at the end of the previous tick.
That is why touching mid skip and leaving straight away works.

### 1.5 Getting into 4:1 (the "four possibilities")
Start at the thumb. Click the hand as Olm's head attacks (one tick after Olm). Then read what Olm does
next [S10, S11, S13, S17, S19]:

| What Olm does after your first synced hit | Fix (ThePerfectG wording) |
|---|---|
| one auto, then nothing (a null) | already in 4:1; just keep the pathway |
| two autos back to back | do **one 3:1** (3 hits, then mid skip), then 4:1 |
| an auto, then a special | do **6 hits** total, then mid skip |
| a special first (the catch-up auto+special) | do **5 hits** total, then mid skip |

The other guides compress this to: "if he does a special, do 3 more hits after it, then run" [S13, S17,
S10]. VideoGameBot's alternative: if a special appears, do one quick **1:0** (one hit, run across), and
Olm stops doing specials. If he then double-autos, do one hit fewer that loop [S14]. On a teleport,
deal with the portal and restart from 1:0 or from the thumb [S10, S11, S13].

### 1.6 Variants
- **1:0** (any weapon, beginner): run back and forth between mid skip/left and the ring finger, one hit per
  crossing; Olm never attacks. It costs a lot of run energy [S11, S15, S13, S3]. Perfect Olm: "guarantees
  Olm will never be able to attack the player" [S3]. The 1:0 click timing is the same as the first hit of
  4:1 [S11].
- **3:0 (mage hand, 4-tick powered staff on ACCURATE, not long-range):** 3 casts per 12 ticks, never hit
  [S2, S12, S15, S19]. Long-range breaks it because the method relies on the staff **dragging** you into
  range while you are still moving [S12, S19, S15].
- **2:0 mage** (beginner) [S24]; ThePerfectG's "one-tick-after 3:0" for starting P2/P3 [S12].
- **3:1** with a **5-tick weapon** (scythe, fang): 3 hits per loop [S2, S3, S14, S24]. Fix rule: on a double
  auto do **2:1** once [S14]. The wiki says the scythe "usually outperforms the dragon hunter lance even
  when accounting for its bonuses" [S2]. Kuji calls a scythe at solo Olm a waste of money [S22].
- **4:2 / 5:2 / 4:0 / 7:1, etc.** ThePerfectG names "4:2" (staying in a loop that tanks both autos: twice
  the damage, but never a special) and "5:2" (deliberately tanking a 2nd auto to recover from a flame wall
  on the thumb tile, then 3:1) [S11, S18]. "4:0" is mentioned as an option for the first three phases [S10].
  No primary source in this pass defined a melee "7:1" or "6:2" except as head-phase/bowfa names (below).
- **Shadow (Tumeken's) methods:** 2:0 simple, or 12:0 / 8:1 advanced (switching to long-range at set
  times). 12:0 only works with Olm on the west, 8:1 with Olm on the east, because NPC true tiles anchor
  at the south-west corner. 7:0 can replace 12:0 at a cost of 1 tick per cycle [S2].
- **Head phase (ranged)** [S25, S16, S17]:
  - **1:0** (any ranged weapon; tbow or crossbow on accurate preferred).
  - **4:1** with a **5-tick** weapon (twisted bow, any crossbow on rapid, e.g. DHCB or rune crossbow).
    "If you're using a five tick weapon ... you're not losing any ticks ... attacking every fifth tick."
  - **3:1** = 4:1 with attack 3 skipped.
  - **Bowfa** (4-tick): **5:1** or **6:2**. Blowpipe: 7:0, 10:1 or 8:2. Blowpipe plus another weapon: 2:0.
- **Head 4:1 pathway** [S25]: go to the side the head is *not* facing. Attack 1 there. Run across, moving on
  the tick the head turns. Attack 2 as you cross the middle. On reaching the far side wait for the cooldown.
  On its last tick, as the head turns toward you, step one tick (2 tiles) back toward the middle and fire
  attack 3. Run back across and fire attack 4 as you cross the middle again. Pattern recognition: "if you
  cross the middle and Olm does attack you, your next attack is attack 4; if he does not, it is attack 2."
  VideoGameBot's version: "hit from right, run, hit past middle, double back, hit past middle and wait,
  hit from the left" [S16].

---

## 2. Olm's structure

### 2.1 Parts, IDs, orientation
| Part | NPC id (normal / CM) | Name | Weak to | Notes |
|---|---|---|---|---|
| Head | 7551 / 7554 | Great Olm | ranged | does all the attacking [S1, S6] |
| Melee hand | 7552 / 7555 | Great Olm (Left claw) | melee | clenches; shows special icons; heals in P3 [S1, S2, S6] |
| Mage hand | 7550 / 7553 | Great Olm (Right claw) | magic | [S1, S2, S6] |

- **Correction to the brief:** the *left* claw is the **melee** hand, not the mage hand. The wiki infobox
  gives the left claw dStab/Slash/Crush 50 and dMagic/dRanged 200. The right claw has dMagic 50 and
  dMelee/dRanged 200 [S1]. The DPS calculator maps 7552 to the melee hand and 7550 to the mage hand [S6].
  "Left/right" here is **Olm's own** left/right. Seen from the player, the melee hand is on the player's
  **right** [S17].
- Damage mitigation: the head takes 66% less from non-ranged; the mage hand 66% less from non-magic; the
  melee hand 66% less from non-melee [S1, S2]. (Zenyte divides off-style damage by 3.5, about 71%. The wiki's
  66% is preferred [S7].)
- Size 5 [S1]. Olm rises on the **west or east** wall at random, **switches sides at every phase
  transition**, and the head phase stays where P3 was [S4, S19, S7].
- 50% elemental weakness to earth spells (since 25 Jun 2025). The dragonbane bonus applies, e.g. +20% accuracy
  and damage for the DHL [S1, S29].

### 2.2 HP (solo) and phases
- **HP formula** (wiki DPS calc source) [S6]:
  - `f = min(partySize−1, 50) − 3·floor(min(partySize,50)/8)`
  - hands = `600 + 300·f`; head = `800 + 400·f`
  - **Solo: each hand 600 HP, head 800 HP** (matches the infobox [S1]). Hand HP resets every phase.
  - Olm's HP depends on party size only, not on combat level (wiki talk note [S2-talk]; [S6]).
- Solo combat stats (maxed account, infobox) [S1]: head att/str/mage/range 250, def 150. Claws att/str/range
  250, def 175. Mage-hand magic 87 (halved in the DPS code [S6]). In CM all of these are ×1.5.
- **Phases:** in a team of 1–7, Olm has **four phases: three standard (hand) phases plus a head phase**.
  Every 8 players add one more standard phase [S2, S1]. The CM page says small teams have "only three phases"
  [S4]. That counts only the hand phases; the RSPS models agree (3 hand phases, then a "final stand" on the
  same rise) [S7, S9]. The Perfect Olm page numbers the head phase "phase 4" [S3].
  - P1 and P2: one random power each (acid, flame or crystal), announced as
    `The Great Olm rises with the power of Acid/Flame/Crystal!`. Powers do not repeat [S2, S17].
  - P3 (penultimate): head glows green and **all powers** are available. Both hands must go down
    together (2.4). Melee-hand infinity heal added [S2, S1].
  - P4 (head / "final stand"): head attackable and no longer heals. Generic specials (burst, lightning,
    teleport) removed. Life-siphon pools added. Crystals fall constantly [S2, S4, S25].
- Max hits (normal mode) per the infobox: **27, 28, and 29 in phase 4**. CM: 38 (phases 1–2), 39, 41
  (phase 4) [S1]. Max hit rises slightly in P3 and again in P4 [S2]. Prayer cuts standard attacks by about
  75–80% [S1]. Zenyte uses ×0.2 [S7].
- Transition: Olm sinks, the cavern shakes, and 3×3 crystals fall (shadowed; 20–25 dmg direct, 12–16 adjacent;
  one always aimed at a player). After **7–8** targeted crystals it rises on the other side [S2].

### 2.3 Which hand first, and the clench ("cripple")
- **P1 and P2: mage hand first, then melee hand** [S2, S4, S13, S19]. A heavy hit on the melee hand while the
  mage hand is alive makes it **clench**. While clenched it is invulnerable, and Olm cannot use its generic
  specials (burst, lightning, teleport). Phase-power attacks still happen [S2, S4].
  - Trigger: Mod Ash says the threshold is **1/20 of base health** (= **30 dmg** for a 600-HP solo hand)
    [S1, Mod Ash 26 Aug 2020]. The strategies page: "≥ 5% of its maximum health within 8 ticks,
    specifically the eight ticks between null and special" [S2]. *(Zenyte: ≥20×players over 10 ticks; Kronos:
    1/5 HP. Both are guesses. Prefer Mod Ash.)*
  - Duration: the wiki says "roughly 30 seconds" [S2]. The live-client tracker counts **45 ticks (27 s)** [S8],
    as does Zenyte [S7]. The CM page says the clench also ends when the mage hand is disabled [S4]. The
    strategies page says it "is removed for the phase once the mage hand is incapacitated" [S2].
  - Animations (live client): 7360 into clench, 7361 clenched, 7362 and 7364 unclench [S8].
  - **How solo players use it:** open each phase with a DWH/BGS/elder-maul spec on the melee hand (defence
  drain). That hit usually clenches the hand ("if done right the hand will be crippled") [S17, S19]. Then
  3:0 the mage hand (specials blocked meanwhile). When the mage hand dies, switch to melee and 4:1 the
  melee hand [S17]. Killing the melee hand first in P1/P2 is "unfeasible as it will clench very often" [S4].
- **P3: no clench.** Common plan: spec the melee hand, 4:1 it low (Kaoz: below 50 HP; wiki: about 75 HP).
  Then 3:0 the mage hand low. Then **kill the melee hand first** (its infinity-heal special would waste time),
  and finish the mage hand [S2, S17, S13]. ThePerfectG: on P3 start, do two hammer specs from the hand's
  "right side" tile, then run to mid skip. "This will put you into four to one instantly" [S11].

### 2.4 P3 hand regeneration
- Once a hand is down in P3, a bar appears in its place. If the bar fills before the other hand dies, the
  hand **returns at full HP** and gives no further points. The wiki says the bar takes "roughly 30 seconds"
  [S2, S4]. Zenyte uses 34 ticks (about 20 s) [S7], which is a guess. **Treat ~30 s as the planning figure.
  It is not "~10 ticks".**
- Learning tip: you can practise indefinitely as long as you don't kill both hands within about 30 s of each
  other on P3 [S2].

### 2.5 Head outside the last phase
- The head can be attacked in any phase, but it **fully heals** after a few ticks and gives no points or xp
  [S1, S4]. Zenyte heals every 5 ticks [S7].

---

## 3. Attacks and timing

### 3.1 Standard attacks (the head)
- Magic: green orb/dragonfire projectile. Ranged: crystal chunk. Same damage [S1, S2].
  Projectile/graphic ids from Kronos: magic 1339, ranged 1340 [S9]. The live plugin keys prayer off
  `OLM_MAGE_ATTACK`/`OLM_RANGE_ATTACK` projectiles [S8].
- **1/5 chance to switch style after each head attack** (Mod Ash) [S1]. So keep praying the style it last
  used ("he's most likely to use the same attack in a row") [S19].
- Hits every player in the zone the head faces [S2].
- **Phase-power attacks replace a standard attack:** a 1/3 chance per auto (1/2 in the final phase), unless on
  cooldown (Mod Ash) [S1]. Olm tries not to repeat the same phase attack within a phase [S1]. After a phase
  attack, Olm uses at least two normal autos before another ("soft cooldown") [S18, S11].
  *(Zenyte uses 1/7 plus a 10 s cooldown. Disagrees; prefer Mod Ash [S7].)*

### 3.2 Spheres (prayer orbs)
- Red = Protect from Melee; green = Protect from Missiles; purple = Protect from Magic [S1, S2].
- If you are praying any overhead when the sphere is launched, that prayer is **switched off** and
  **prayer points are halved**. To block, turn on the matching prayer before it lands. An unblocked sphere
  takes **50% of current HP** [S1, S2].
- Chat lines (bot trigger): "The Great Olm fires a sphere of aggression / accuracy and dexterity / magical
  power your way." with "Your prayers have been sapped." if one was active [S8].
- Time to impact: Zenyte lands it **4 ticks** after launch [S7] (model value). The OSRS sources say only
  "before the sphere hits".

### 3.3 Phase powers
- **Acid** [S1, S2]. Acid spray: pools around the room, 3–6/tick plus poison (starts at 2). Acid drip: you
  leave pools as you move, and standing still hurts. Solo answer: keep moving, or walk it off and restart
  the cycle [S11, S13, S17].
- **Flame** [S1, S2]:
  - Deep burn ("Burn with me!"): 6 procs of 5 dmg, each draining combat stats by 2. It spreads to adjacent
    players.
  - Fire wall: two walls trap you. A player on a wall tile leaps out for 8–10 dmg. Anyone left between the
    walls takes **50–65 dmg after ~5 s**. Douse one segment with any water spell, ice spell, Humidify, or a
    watering can (8 uses [S19]).
  - **The wall fails if there is no room to place it.** Taking the auto on the wall-side "pinky" or flame-skip
    tile, or at the edge of the middle view, nulls it, and the rotation continues with no catch-up [S1, S11,
    S13, S19].
- **Crystal** [S1, S2]. Falling crystals: 11 spikes on the targeted player, 15–20 each, 1×1; keep moving
  every tick. Crystal bombs: up to 2 in small groups, radius 4. Damage is 15 at the rim, +15 per tile closer,
  **60 on top**.

### 3.4 Generic specials (all hand phases; shown on the melee hand)
- **Crystal burst** (hand anims 7356/7366): a seedling under every player bursts a few seconds later. Heavy
  damage and a shove [S1, S8].
- **Lightning** (7358/7368): bolts run north/south along the room. A hit disables overheads, deals moderate
  damage (Zenyte 15–25), and binds you [S1, S7, S8].
- **Teleport/swap** (7359/7369): in solo, a portal on a random tile **within 10 tiles** (since 29 Nov 2023 [S5]).
  Stand on it. Damage scales with distance: 0 on it, 5 adjacent, 10 at 2 tiles, up to 50 [S2]. Chat line:
  "the teleport attack had no effect" when dodged [S13].
- **Infinity/heal** (P3 only, 7357/7367): two attacks after the teleport, for two of Olm's attacks; damage
  heals the melee hand [S2]. Zenyte: 16 ticks [S7].
- None of these is caught up if skipped [S1].

### 3.5 Head phase (P4)
- No crystal burst, lightning, or teleport. Phase powers and spheres remain [S2, S25].
- **Life siphon:** two blue projectiles mark two tiles. Stand on one within **6 s**, or take up to **18**
  (sources disagree slightly). Olm heals **5× the damage dealt** [S2, S1]. The healing-pool attack has had a
  **10-attack cooldown since 29 Nov 2023** [S5]. ThePerfectG's earlier "no cooldown" predates that patch [S25].
  ThePerfectG's habit: be on the pool "one tick before Olm's next attack" so a flame wall can't trap you [S25].
- **Falling crystals** all phase: "one rock falls onto where the player stands every four ticks, the rest
  are random" [S25]. Move diagonally [S25].
- Head max hit is highest in this phase [S1]. Heal up before starting it [S19].

---

## 4. Positioning

### 4.1 Head vision boxes (model; reproduces every wiki safespot)
Room template rows **y = 34 … 52** (regionY in 12889), player columns x≈27/28 … 37 [S7, S9].

| Head facing | sees rows | means |
|---|---|---|
| SOUTH side | y 34–44 (Kronos: 34–45) | |
| MIDDLE | y 39–49 | |
| NORTH side | y 44–52 | |

- **y=38 row**: only the south-facing head can see you. **y=50 row**: only the north-facing head can.
  These are Kaoz's two "long lines" of yellow tiles across the room ("if you stand anywhere on the long
  lines Olm will turn his head towards you") [S17-doc], and the wiki's spots 1, 4, 5, 8 [S3].
- With Olm west: spot 2 (28,43) is seen by south and middle but **not north**. Spot 3 (28,46) is seen by
  middle and north but **not south**. That is the wiki rule "spots 2 and 7 force middle if facing right;
  3 and 6 force middle if facing left" [S2].
- Kronos says the south box ends at y=45, Zenyte at 44. The east mid-skip tile used by every guide is
  (37,45) and must be unseen by the south box, so **44 fits the guides**.
- Turn target (Zenyte model) [S7]: count the players in each box. Middle wins ties. A hand that took damage
  since Olm's last step adds +1 to its side (wiki: "damaged within 4 ticks") [S2]. That is why you take
  the null at the thumb only if a hit since the last auto did damage. Otherwise the head goes middle early.

### 4.2 Arena facts
- World tiles (template): chamber x 3227–3237, y 5730–5748. Head face tile (3227,5740) on the west,
  (3238,5740) on the east [S7]. Centre row y=5740 (regionY 44). Hands sit 5 tiles north and south of the
  head (claw objects at y 5743 and y 5733) [S7]. CoX is instanced, so use region-relative coordinates
  [S11-paste].
- Practical distances: mid skip to ring finger is 7 tiles (about 4 ticks running). Thumb to mid skip is
  4 tiles (2 ticks). The ring finger is 3 tiles from the thumb (2 ticks) [derived from S11-paste].

### 4.3 Why the two wiki safespot lists "disagree"
- Strategies page: spots 1 and 8 force the head **left**; 4 and 5 force it **right** [S2].
- Perfect Olm page: 1 and 8 force it **right**; 4 and 5 force it **left** [S3].
- Spot 1 is (28,38) and spot 8 is (37,50) [S3 tile JSON]. Both are the **mage-hand-side** tiles: player's
  left, Olm's right. The pages simply use opposite frames. No mechanical disagreement.

### 4.4 Per-hand positioning
- **Mage hand (3:0):** cycle the mage-side far tile, the tile left of the head, the thumb, and the far
  melee-side tile. Attack from outside staff range so the drag pulls you while you move. If a cast splashes,
  run all the way to the opposite side and wait for the turn, or tank one hit at the head-centre "edge of
  vision" (which blanks flame walls) [S12, S15, S17]. ThePerfectG also shows a walking 3:0 that saves about
  half the run energy [S12].
- **Melee hand (4:1):** see 1.4. Flame phase and P3: after hit 2, step one more tile toward the wall (the
  "Flame Skip" tile), so Olm cannot fit a wall when it autos you [S11, S13, S17].
- **Head:** run the head across the room between the two long lines; 1:0 or 4:1 (1.6) [S25, S16, S17].
  "Big mistake I see people make on head phase is running too fast": standing on the line as the head turns
  to the middle makes it turn all the way and you take an extra hit [S19].

---

## 5. Budget gear and supplies

### 5.1 What the guides actually bring
- **Wiki "Minimum" CoX setup** [S2]: Neitiznot helm, fire cape, amulet of fury, tentacle, fighter torso, black
  d'hide chaps, dragon defender, barrows gloves, dragon boots, lightbearer. Switches: dragon crossbow (ruby
  dragon bolts (e)), Ava's, trident of the swamp, occult, black d'hide body, mystic top/bottom, imbued Saradomin
  cape, toxic blowpipe, BGS, rune pickaxe, salve (ei), dragon sword, lockpick. Inventory: sanfew, brews,
  restores, super combat, ranging potion.
- **VideoGameBot "any gear" solo** (3:0 mage / 1:0 melee / 1:0 head) [S15]: tentacle, rune defender, dragon
  boots, mystic bottoms as legs, fire cape, barrows gloves (lightbearer, unused). Rune crossbow in black
  d'hide with assembler/accumulator. Mystics, occult or glory, **trident of the swamp (must be a 4-tick
  staff, not on long range)**. **One overload, one prayer enhance, many staminas and restores/revites**, rest
  brews. No offensive prayers. Visual-metronome plugin set to 4 plus a true-tile indicator. "This might
  actually only be like a four stamina run".
- **Gnomonkey "first budget solo CoX"** (ironman-tier) [S19]: hasta (crush, good for Tekton) or whip,
  amulet of blood fury, **Eclipse moon set (melee armour and the Eclipse atlatl as the head weapon)**,
  Blue Moon set for mage, barrows gloves, dragon boots, berserker ring. DWH "huge" for the hands; burning
  claws if no hammer; fang not usable as the only melee weapon. Thralls (Arceuus) "really good for learning
  solo Olm". Two staminas. **Overload, prayer enhance, about 3 restores, the rest brews (~12)**. Biggest
  upgrades: **lance**, then **sang**; then elder maul. Lightbearer "barely does anything", so camp a melee ring.
- **Noodely "bare minimum"** [S24]: barrows gloves, blood fury, ring of endurance (optional since the run-energy
  rework). Fang to learn 3:1 (more forgiving) or whip/DHL for 4:1. Sang or trident of the swamp (eye of
  Ayak as an upgrade). Rune crossbow with ruby bolts. **The lowest water spell in the rune pouch** for fire
  walls. Overload, prayer enhance, "a ton of food". "Blood fury is more important than sang for learning,
  because the melee hand always requires tanking at least one hit."
- **Kaoz** [S17]: base 75s "heavily recommended", 85s better. Learner kit DHL, bowfa, trident of the swamp.
  Lunar Diplomacy (Humidify); A Kingdom Divided (thralls). **Minimum 4 stamina potions**; "you want to be
  under the effect of a stamina potion at all times". Plugins: ground markers (his export), CoX Additions,
  tile indicators (true tile).
- **ThePerfectG** [S12]: bring 2 full staminas (some bring 3–4); enter with **Protect from Missiles + Augury**.
- **Wiki supplies line** [S2]: small teams want at least 1 overload, 1 prayer enhance, 2–3 restores and 6–7
  brews each; a stamina is "highly recommended" for solo; Olm is "generally expected to be cleared within
  10 minutes". Supplies you die with drop in the room, so take a fresh batch from the chest rather than going
  back for them [S17].
- **Overload (+)**: boosts all combat stats and re-applies for **5 minutes**; costs 50 HP, which is healed
  back when it expires [S23, S15].

### 5.2 Weapon speeds that matter
- 4-tick melee (4:1): DHL, whip/tentacle, hasta, rapier [S14, S19, S2].
- 5-tick melee (3:1): scythe, fang [S2, S14, S24].
- Mage: trident/sang are 4-tick powered staves (3:0) [S2, S15]; eye of Ayak is 3-tick (advanced) [S24].
- Head: tbow / DHCB / rune crossbow at 5 ticks on rapid → 4:1; bowfa at 4 ticks → 5:1 or 6:2; blowpipe →
  7:0 etc. [S25, S16].
- Head weapon ranking per wiki: tbow > DHCB > blowpipe/bowfa > dragon crossbow > rune crossbow/atlatl [S2].
  ThePerfectG rates blowpipe "barely better than rune crossbow" after its nerf [S25].

### 5.3 Prayers
- Overheads: Protect from Missiles or Magic, matching the last style seen (1/5 switch chance) [S1, S12, S19].
- Offensive: Augury on mage hand, Piety on melee hand, Rigour on head (Eagle Eye/Mystic Might for low prayer).
  ThePerfectG starts with Augury [S12]; VideoGameBot runs with none [S15].
- Once the cycle is solid and thralls keep the head predictable, some raiders drop the overhead entirely
  [S19, S15].

---

## 6. Common mistakes and how the method avoids them

| Mistake | What happens | Fix |
|---|---|---|
| Staff on long range | no drag, so you are late and get seen | Accurate (either accurate style) [S12, S15, S19] |
| Splash/0 on the mage hand from the far side | head turns middle and you take a hit | run fully across and reset, or tank at the head-centre edge; thralls make it near-impossible [S12, S17, S19] |
| 0s on melee hits 2–3 ("noodling") | head turns middle during the null, extra auto | carry on (one extra auto), or restart from the middle of the hand [S11, S13] |
| Stalling on mid skip or late by a tick | you tank the 2nd auto | wait one tick on mid skip, do a 3:1 [S11] |
| Too early to mid skip | head stays right; you are out of sync | go back to the middle, wait a tick, re-read [S11] |
| Ignoring the cycle after a special | specials keep coming (a different one each loop) | count: special → 3 more hits (or 5/6 total), then skip [S11, S13, S17] |
| Flame wall on the pathway | trapped for 50–65 | wall-side "flame skip" tile; carry a water spell, Humidify or watering can; 5:2/4:2 then 3:1 recovery [S1, S11, S18, S24, S19] |
| Acid trail | per-tick damage | keep moving; walk it off and re-enter the cycle [S11, S13, S17] |
| Falling crystals (crystal phase / P4) | 15–25 hits | never stand more than one tick; move diagonally on head [S10, S13, S25] |
| Teleport portal far away | up to 50 dmg | stand on it, then restart with 1:0 [S10, S11, S2] |
| Heavy melee damage in P1/P2 before the mage hand is dead | melee hand clenches (~45 ticks) | mage hand first; use the clench on purpose with the opening spec [S2, S4, S17] |
| P3: one hand dies long before the other | it regenerates at full HP, no points | bring both low (≤50–75), kill the melee hand first, then the mage hand within the ~30 s bar [S2, S17] |
| P3 infinity heal | melee damage heals the hand | skip it with the cycle or kill the melee hand first; 1:0 for the last hits [S13] |
| Not praying spheres / leaving a prayer on as one launches | 50% HP, or prayer off and points halved | re-pray the matching style before it lands [S1, S2] |
| Running out of run energy | cannot run the head | staminas (2–4), Vile Vigour on Arceuus, walking 3:0 [S17, S12, S13] |
| Head phase: reaching the line too early | Olm turns fully, extra hit | leave just as the head is about to turn [S19] |

---

## 7. Bot-oriented summary of signals (live-client facts)
- Objects: head 29880 (rising) / 29881 (ready); melee-hand object 29883/29884; mage-hand object 29886/29887
  [S8].
- Head animations: 7334/7335 rise; 7336 idle mid; 7337 idle right; 7338 idle left. Turns: 7339 mid→right,
  7340 right→mid, 7341 mid→left, 7342 left→mid, 7343 left→right, 7344 right→left. Attacks: 7345 mid,
  7346 right, 7347 left. 7348 dying. Enraged (P3+) set 7371–7383 [S8]. A turn animation means that step was
  skipped; an attack animation means it fired.
- Melee-hand animations: idle 7355/7365; crystal 7356/7366; heal 7357/7367; lightning 7358/7368; portals
  7359/7369; crippling 7360; crippled 7361; uncrippling 7362/7364; dying 7370 [S8].
- The live tracker re-syncs its 4-step counter on every special icon: crystal → next special is lightning,
  and so on [S8]. **The first head attack comes 4 ticks after rise in P1 and 8 ticks after rise in later
  phases** [S8].
- Phase power comes from the chat message "the great olm rises with the power of acid/crystal/flame." [S8].

---

## Sources
- **S1** OSRS Wiki, Great Olm: https://oldschool.runescape.wiki/w/Great_Olm (raw: `?action=raw`). Embeds Mod
  Ash tweets: style switch 1/5 https://twitter.com/JagexAsh/status/1287733801398009857 ; phase attack 1/3 or
  1/2 https://twitter.com/JagexAsh/status/1434921194860519431 ; phase-attack choice and memory
  https://twitter.com/JagexAsh/status/1562157994384805888 ; clench 1/20 base health
  https://twitter.com/JagexAsh/status/1298675860111798274
- **S2** OSRS Wiki, Chambers of Xeric/Strategies (Great Olm, Safespotting, Phases, Setups):
  https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies ; talk page (scaling note):
  https://oldschool.runescape.wiki/w/Talk:Chambers_of_Xeric/Strategies
- **S3** OSRS Wiki, Perfect Olm (Solo), with a tile JSON for spots 1–8:
  https://oldschool.runescape.wiki/w/Perfect_Olm_(Solo)
- **S4** OSRS Wiki, Chambers of Xeric/Challenge Mode/Strategies (Great Olm):
  https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode/Strategies
- **S5** OSRS Wiki, Chambers of Xeric (changes, 29 Nov 2023: portal within 10 tiles in solo; healing-pool
  10-attack cooldown): https://oldschool.runescape.wiki/w/Chambers_of_Xeric
- **S6** Wiki DPS calculator, Olm scaling code:
  https://github.com/weirdgloop/osrs-dps-calc/blob/main/src/lib/scaling/ChambersOfXeric.ts and ids in
  https://github.com/weirdgloop/osrs-dps-calc/blob/main/src/lib/constants.ts
- **S7** Zenyte/Near-Reality RSPS Olm implementation (Kris, 2018; NOT authoritative):
  https://github.com/Ecoscape16/near-reality-server-new/tree/master/core/src/main/java/com/zenyte/game/content/chambersofxeric/greatolm
  (GreatOlm.java, OlmRoom.java, LeftClaw.java, scripts/*)
- **S8** OpenOSRS "coxhelper" Olm tracker (live-client plugin):
  https://github.com/karankurbur/OpenOSRSPlugins/tree/master/coxhelper/src/main/java/net/runelite/client/plugins/coxhelper
  (Olm.java, OlmAnimation.java, CoxPlugin.java)
- **S9** Kronos RSPS GreatOlm.java (NOT authoritative; Facing boxes, projectile ids):
  https://github.com/The-RSPS-Archive/184---Kronos-Source/blob/main/Kronos-master/kronos-server/src/main/java/io/ruin/model/activities/raids/xeric/chamber/combat/GreatOlm.java
- **S10** SoupRS, "How to 4:1 at Olm": https://www.youtube.com/watch?v=cRsOeK9U8U0
- **S11** ThePerfectG, "Solo Olm Guide – 4:1 And How To Melee Hand – In Depth":
  https://www.youtube.com/watch?v=MbxjaO6JX9Y ; **S11-paste** tile markers https://pastebin.com/acC6t26R
- **S12** ThePerfectG, "Solo Olm Guide – How To Mage Run Perfectly": https://www.youtube.com/watch?v=1NMexj5cKhQ
- **S13** WildMudkip, "Solo Olm 4:1 Guide (and more tips)": https://www.youtube.com/watch?v=QmfylzVqKm8
- **S14** VideoGameBot, "Master the Melee Hand! Solo Olm 4:1 + 3:1 Guide": https://www.youtube.com/watch?v=_KruQfUdVWI
- **S15** VideoGameBot, "How To Solo Olm at Any Level, With Any Gear Setup": https://www.youtube.com/watch?v=P4UrbK_Yz8I
  (tiles https://pastebin.com/BwprYmCD)
- **S16** VideoGameBot, "Efficient Solo Olm Head Phase! 4:1 Range Guide": https://www.youtube.com/watch?v=qFgXJ_f20jI
- **S17** Kaoz OSRS, "The Only Solo Olm Guide You'll Need (3:0, 4:1)": https://www.youtube.com/watch?v=49C4mqEzUAg ;
  **S17-doc** tiles https://docs.google.com/document/d/1hvIwBaeW_jf3-EfxvFNEIJkEosrnQJRhLJAyywQ_kI8
- **S18** ThePerfectG, flame walls during 4:1: https://www.youtube.com/watch?v=MI0lkYNamJM
- **S19** Gnomonkey, "Your First Budget Solo CoX Guide": https://www.youtube.com/watch?v=wxGBi89vE_s
- **S20** Aatykon, easy 4:1 entry: https://www.youtube.com/watch?v=8C56hE9JUhE
- **S21** Khufu, Olm attack cycle and special skipping: https://www.youtube.com/watch?v=3VTDkm0Z7NY
- **S22** Kuji OSRS, "3:0 mage and 4:1 melee Solo Olm": https://www.youtube.com/watch?v=-uuxf_QsSeg
- **S23** KrakWithaK, "Complete Beginners Guide to Solo Olm" (overload 5 min): https://www.youtube.com/watch?v=81Mr1S-H_Qw
- **S24** Noodely, "The Easiest Way To Learn SOLO Olm (DPS Progression Ladder)": https://www.youtube.com/watch?v=iiNQAiEfibk
- **S25** ThePerfectG, "Solo Olm Guide – Head Phase": https://www.youtube.com/watch?v=4_35Zw9ceHo
- **S26** OSRS Wiki, A Not So Special Lizard (CA: no P3 specials): https://oldschool.runescape.wiki/w/A_Not_So_Special_Lizard
- **S27** (unreliable) https://mypvm.com/blog/osrs-chambers-of-xeric-guide-2026-complete-cox-raid-walkthrough
- **S28** (RSPS) https://www.battle-scape.com/threads/15140-Solo-Olm-Guide
- **S29** OSRS Wiki, Dragon hunter lance: https://oldschool.runescape.wiki/w/Dragon_hunter_lance

Not reached: Reddit (the reddit.com JSON is blocked; pullpush.io rate-limited after the first query and
asks agents not to scrape), the Medium solo guide (HTTP 403), and Stay West's "4:1 cycle" video (no captions).
