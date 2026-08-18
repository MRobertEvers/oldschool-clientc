# Theatre of Blood — open-question research log

Live log of the deep-research pass against the open `[MEASURE]` questions in
[`minigames/theater_of_blood/THEATRE_OF_BLOOD_PLAN.md`](minigames/theater_of_blood/THEATRE_OF_BLOOD_PLAN.md)
(§ "What is still unmeasured", items M1–M39).

Started 18 August 2026. Everything below is either a **quoted source** or a
**measurement I ran against a public dataset**; where a question is still open,
it says so and says what would close it.

Evidence ranking used (same as `COMMUNITY_SOURCES.md`):
**game cache > code a competitive player relies on > a dataset built from
recordings > a written guide > a video > a forum post.**

---

## 0. New sources found in this pass

| Source | Why it is new | What it settled |
|---|---|---|
| **Blert public HTTP API** — `https://blert.io/api/v1/...` | Not previously used by the plan. Undocumented but public and unauthenticated. Exposes `trends/bloat-downs`, `trends/bloat-hands`, `challenges`, `raids/tob/{uuid}/events` (per-tick event stream), `splits/distributions`, `leaderboards`. | M17 (measured over 98 445 first downs), M6 (in progress) |
| **`QuestingPet/TobMistakeTracker` full source** | The plan had only 7 files of it; the head comment of `MaidenMistakeDetector` states the **entire blood-splat projectile law**. | M3 (closed), M5 (splat half re-confirmed) |
| **Blert Nylocas mechanics guide** (`web/app/guides/tob/nylocas/mechanics/page.tsx` in `blert-io/blert`) | A written, tick-level mechanics spec by the recorder's own author. | M8 (closed), M9 (regular-mode figure), M37 (bounded), plus the nylo death-animation table |
| **rev-239 cache `stat4` on the Entry-Mode ("story") npcs** | Already in the tree but never cross-read against the Wiki for the Entry-mode scaling question. | M19 (closed) |

---

## 1. Findings by question

Sections are in the order the answers arrived, not in M-number order; the
[status summary](#status-summary) at the end indexes all of them, and the
[provenance audit](#provenance-audit--what-blert-observes-versus-what-blert-asserts)
records which figures are observations and which are another project's constants.

**Outcome: 16 questions closed, 3 partially closed, and every question still open now names
the dataset or capture that would close it.**

### How the measurements below were taken

Blert's website exposes an unauthenticated JSON API. The two endpoint families used:

```
GET https://blert.io/api/v1/trends/bloat-downs?downNumber=eq1[&mode=&scale=]
GET https://blert.io/api/v1/trends/bloat-hands
GET https://blert.io/api/v1/challenges?type=1&mode=11&scale=eq3&status=eq1
GET https://blert.io/api/v1/raids/tob/{uuid}/events?stage={10..15}
```

`stage` is `10 = Maiden, 11 = Bloat, 12 = Nylocas, 13 = Sotetseg, 14 = Xarpus, 15 = Verzik`.
`mode` is `10 = Entry, 11 = Regular, 12 = Hard`. `status=eq1` is a completed raid.
The `events` stream is the **full per-tick event log** of a real raid: npc spawns,
deaths, attacks (with the attack's identity), and per-room special events.

**Tick origin.** Every event tick is `client tick − room start tick`
([`DataTracker.getTick`](https://github.com/blert-io/plugin/blob/master/src/main/java/io/blert/core/DataTracker.java)),
and the room starts on the tick blert first sees a player inside the room's world area
(`RoomDataTracker.checkEntry` → `startRoom`). So **tick 0 is the tick the first player
is in the room**, and every figure below is on that clock. Boss attacks are recorded on
the tick the boss *animates*, not the tick the projectile lands.

**Politeness note.** A first bulk crawl (~450 requests, 0.3 s apart) was rate-limited by
blert with HTTP 429 partway through. The crawl was then re-run at 3 s/request and kept
small. If this research is ever repeated, throttle from the start — the dataset is a
volunteer-run service.

---

## M1 — Maiden: does the first blackstorm land on the room-start tick or 10 ticks later? **CLOSED**

**Answer: neither exactly — her first attack *animation* is on room tick 9, and the
cadence is a flat 10 ticks from there.**

Measured over **26 completed Maiden rooms** (608 recorded Maiden attacks):

| Raid | Maiden attack ticks |
|---|---|
| `047e7ac7` | 9, 19, 29, 39, 49, 59, 69, 79, 89, 99, 109, … |
| `0ffb0fcb` | 9, 19, 29, 39, 49, 59, 69, 79, 89, 99, 109, … |
| `630f537c` | 10, 20, 30, … |

* **25 of 26 rooms: first attack on tick 9.** One on tick 10 (that raid's recorder joined
  the room a tick late — its whole clock is shifted by one).
* **Inter-attack gap: 10 ticks in 582 of 582 observed gaps.** No exceptions, no drift,
  including across the 70/50/30 crab spawns.

So the room's attack clock is armed at room start and first fires on the **10th tick**
(tick 9, 0-indexed). It does *not* fire on the tick the room starts.

**Corollary for M20:** the Maiden clock free-runs through the crab spawns — the 10-tick
cadence is unbroken across all 78 transmogs in the sample.

---

## M2 — Maiden: what decides blood splat vs blackstorm? **COOLDOWN CONFIRMED, ROLL ESTIMATED**

Measured across **608 Maiden attacks in 26 rooms** (`1 = blackstorm auto`,
`2 = blood throw`, both read from her animations 8092 / 8091).

**The cooldown is exact and never violated.** Modelling eligibility as *"at least 3 attacks
since the last blood throw"* — i.e. blood, auto, auto, then eligible — produced **zero
violations in 608 attacks**. That is the Wiki's *"cannot use this attack again for the next
two attacks"*, now confirmed against recorded raids rather than restated. She may also throw
blood on her **very first** attack of the room, so nothing suppresses it at the start.

**Eligibility is necessary but not sufficient — there is a roll on top.** Of **385 eligible
attacks, 119 were blood throws: p̂ = 0.309** (95 % CI ≈ 0.26–0.36, so consistent with a flat
**1/3**). The gaps between successive blood throws, in attacks:

| gap | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 19 |
|---|---|---|---|---|---|---|---|---|---|---|
| count | 35 | 19 | 17 | 4 | 5 | 4 | 4 | 1 | 3 | 1 |
| geometric p=1/3 would give | 31 | 21 | 14 | 9 | 6 | 4 | 3 | 2 | 1 | — |

Close to geometric, with a mild excess at gap 5 and a longer tail than a flat roll predicts
(the single gap of 19 is one room where the recorder lost ticks).

**What is still not pinned:** whether the roll is exactly 1/3, and whether it is conditional
on anything (party size, how many players are in melee distance). The same harvest run over
every Maiden room blert holds would settle the first — 385 eligible attacks gives ±0.05, and
ten times the sample gives ±0.015, enough to separate 1/3 from 0.3.

## M3 — Maiden: blood-splat projectile flight time as a function of distance. **CLOSED**

Stated outright by `QuestingPet/TobMistakeTracker`, in the head comment of
[`MaidenMistakeDetector.java`](https://github.com/QuestingPet/TobMistakeTracker/blob/master/src/main/java/com/tobmistaketracker/detector/MaidenMistakeDetector.java):

> When Maiden throws her blood at players, she always throws 1 to where each player is
> currently standing, and 2 extra to the furthest player. The remainingCycles on the
> blood Projectile depends on how far the player is from her **actual hitbox** (not what's
> shown in-game, NE is closer). **1 tile away takes 65 cycles, incrementing by 15 for every
> extra tile away from her hitbox (80, 95, 110, etc.)**. The extra two bloods thrown at the
> furthest player are **always +25 cycles** from that player's blood spot (65 → 90, 80 → 105,
> 95 → 120, etc.), which guarantees that they will activate one tick later than the main
> blood spot.
> […] Once the blood spot is active, it *always* lasts for **exactly 11 GameTicks**.

In this tree's units (a client cycle is 20 ms, 30 cycles to a tick):

```
flight_cycles(d)      = 50 + 15 * d          # d = tiles from Maiden's hitbox, d >= 1
flight_cycles_extra(d)= flight_cycles(d) + 25   # the 2 bonus splats on the furthest player
ticks_until_active    = floor(flight_cycles / 30)
splat_lifetime        = 11 ticks, from the tick it becomes active
```

The plugin *runs* this arithmetic every raid to decide whether a player stood in blood, so
it is load-bearing: if the constants were wrong the plugin would mis-flag mistakes and be
fixed. The `+25` rule is the interesting part — it is a deliberate guarantee that the two
bonus splats activate one tick *after* the splat under the player's feet.

---

## M5 — Maiden: blood **spawn trail** lifetime. **MEASURED — 29 ticks**

The 11-tick figure in the plan is the Maiden's own **splat** (M3) and is settled. The
**trail** left by a walking blood spawn is a different object — game object **32984** — and
no plugin models its lifetime: both TobMistakeTracker and blert track it purely by
`GameObjectSpawned`/`GameObjectDespawned`, which is what you do when you do not know the
duration.

But blert *transmits* the live trail set every tick (`TOB_MAIDEN_BLOOD_SPLATS` carries the
full list of active trail tiles), so the lifetime can be recovered: take each tile's
contiguous run of presence across 26 Maiden rooms.

| run length (ticks) | count | share |
|---|---|---|
| **29** | **2 678** | **80 %** |
| 13–18 | 398 | 12 % |
| 1–12, 19–28 (flat, ~8 each) | ~200 | 6 % |
| 30, 40, 42–50, 57, 60, 70, 73, 90, 110 | ~90 | 3 % |

**A trail tile stays active for 29 ticks (17.4 s).** The long tail is a tile being
re-covered before it expires — a blood spawn walking back over its own trail restarts the
clock, giving runs of 40, 50, 60, 90 and 110 — which is exactly why a stopwatch reading of
this mechanic lands on the Wiki's vague *"roughly 20–30 seconds"*: what a player sees is the
lifetime of a *patch*, not of a tile, and patches under a circling spawn last far longer
than 29 ticks.

Two caveats, stated rather than hidden:

* The 13–18 cluster (12 %) has no identified cause. It is **not** blood-spawn deaths — of
  398 such runs only 3 ended within 3 ticks of a blood spawn dying, versus 43 of the 2 678
  29-tick runs. The most likely explanation is dropped ticks in the recording (blert tracks
  a `missing_tick_count` per room for exactly this reason), which would truncate a run.
* 29 is an odd-looking constant. If the true value is 30 ticks (18 s) with the despawn
  observed one tick early, every figure here shifts by one. Worth confirming with a direct
  `GameObjectDespawned` capture before it is written into `tob.constant`.

## M8 — Nylocas: delay from the last wave-31 nylo dying to Vasilias landing. **CLOSED**

Two independent sources agree, and the second gives the exact rule.

Blert's own Nylocas mechanics guide states it in prose:

> After the waves, Nylocas Vasilias […] spawns. The spawn occurs on the start of the
> **5th cycle** following the despawn of the last regular Nylocas.
> — [blert.io/guides/tob/nylocas/mechanics](https://blert.io/guides/tob/nylocas/mechanics)

Measured against 22 recorded raids (`TOB_NYLO_CLEANUP_END` → `TOB_NYLO_BOSS_SPAWN`); the first 11 are shown:

| cleanup end | boss spawn | delta |
|---:|---:|---:|
| 339 | 356 | 17 |
| 324 | 340 | 16 |
| 329 | 345 | 16 |
| 333 | 351 | 18 |
| 331 | 348 | 17 |
| 275 | 292 | 17 |
| 304 | 320 | 16 |
| 276 | 292 | 16 |
| 286 | 305 | 19 |
| 316 | 332 | 16 |
| 313 | 332 | 19 |

The delta is not constant, but the rule behind it is exact. With `w1` the tick wave 1
spawned (the room's cycle-0 phase), **every one of the 22 boss spawns satisfies
`(boss − w1) ≡ 0 (mod 4)`** — the boss always lands on a wave-check tick — and

```
boss_spawn = first wave-check tick >= cleanup_end + 16
           = cleanup_end + 16 + ((4 - ((cleanup_end - w1) mod 4)) mod 4)
```

reproduces **22 of 22** observed spawn ticks exactly (deltas ranged 16–19). So: four
full cycles after the last nylo despawns, rounded up to the next cycle boundary — which is what "the start of the
5th cycle following the despawn" means when the despawn is itself on a cycle boundary.

Note this is keyed on the last nylo's **despawn**, not its death: blert's guide gives the
death-animation table (small nylo killed by a player: animation on `t+1`, despawn `t+2`;
big: despawn `t+6` stationary, `t+7` if it was walking), so a room implementation needs
the despawn tick, and the death-to-despawn delay is itself a function of size, cause and
whether the nylo was moving.

---

## M9 — Nylocas: Vasilias colour-switch interval. **REGULAR MODE CLOSED, ENTRY STILL OPEN**

> Nylocas Vasilias switches between all three combat styles. It always begins as melee,
> and switches to a **different random style every 10 ticks (6 seconds)**. […] In Regular
> Mode, Nylocas Vasilias uses an auto attack matching his current style, **attacking twice
> per phase**.
> — [blert.io/guides/tob/nylocas/mechanics](https://blert.io/guides/tob/nylocas/mechanics)

Two things the plan did not have: the switch is to a *different* style (never a repeat),
and the boss gets exactly two autos per 10-tick window.

Entry Mode's "takes longer" is still unquantified. Blert holds Entry-mode raids, but a
`status=eq1&mode=10` query returned none in the recent window, so it needs a targeted
pull over a longer date range (`startTime=lt<epoch_ms>` pages backwards). The measurement
itself is easy once raids are in hand: the boss's npc id changes with its style, so the
switch ticks come straight out of the `NPC_UPDATE` stream.

---

## M12 / M13 / M14 / M15 — Xarpus exhumeds. **ALL FOUR CLOSED**

Measured from `TOB_XARPUS_EXHUMED` events, which carry `spawnTick`, the despawn tick,
`healAmount` and the list of `healTicks` per exhumed.

### M12 — count per party size

| Scale | Exhumeds (measured) | Spawn cadence | Heal per orb | OpenOSRS `XarpusHandler` |
|---|---|---|---|---|
| 1 | **7** (6 raids) | **12 ticks** | 20 | 7 |
| 2 | **9** (4 raids) | **8 ticks** | 16 | 9 |
| 3 | **12** (8 raids) | **8 ticks** | 12 | 12 |
| 4 | **15** (3 raids) | **4 ticks** | 9 | 15 |
| 5 | **18** (6 raids) | **4 ticks** | 8 | 18 |
| 3, **Hard Mode** | **16** (4 raids) | **4 ticks** | 12 | — |

Every scale matches OpenOSRS' table exactly (note solo is 7, not 6 — the sequence is
7, 9, 12, 15, 18), and Hard Mode is a case no source in the plan covered: a **trio in Hard
Mode gets 16 exhumeds**, more than a regular-mode four-man, on the 4-tick cadence.

### M13 — spawn cadence: **scales with party size**

| Scale | Cadence | Exhumed phase length |
|---|---|---|
| 1 | **12 ticks** | 7 over 72 ticks |
| 2 | **8 ticks** | 9 over 64 ticks |
| 3 | **8 ticks** | 12 over 88 ticks |
| 4 | **4 ticks** | 15 over 56 ticks |
| 5 | **4 ticks** | 18 over 68 ticks |

Dead regular within a raid (one 9-tick gap appeared in 2 of 31 rooms, next to a
recorder hiccup), first exhumed always on tick 8–11. So the answer to "fixed or scaled?"
is **scaled** — and not monotonically in phase length: a trio is slow-drip (8t × 12) while
a four-man is a fast burst (4t × 15).

### M14 — heal cadence and amount

An **uncovered exhumed fires a heal orb every tick**, starting **3 ticks after it
spawns**. Clearest case (`3d30c636`, exhumed spawned on tick 8): `healTicks = [11, 12,
13, 14, 15]` — consecutive ticks from spawn+3 until a player covered it.

`healAmount` (the heal hitsplat on Xarpus) is **constant within a raid and set by scale**:
20 solo, 16 at two, 12 at three, 9 at four, 8 at five. It moves *inversely* to party size,
and the product `count × heal` is nearly invariant — 140, 144, 144, 135, 144 — which points
at a fixed total healing pool divided across the exhumeds rather than a flat per-orb figure.
Hard Mode trio is 16 × 12 = 192, i.e. a deliberately bigger pool.

An implementation therefore cannot use one heal constant, and should not derive it from
max HP either (Xarpus' HP does not move between scales 1–3 while the heal does).

### M15 — how long an exhumed stays open: **exactly 11 ticks**

`despawn − spawn` across **391 exhumeds in 31 Xarpus rooms**:

| lifetime | count | which raids |
|---|---|---|
| **11 ticks** | 327 | every regular-mode raid, all scales |
| **9 ticks** | 64 | every Hard Mode raid (4 raids × 16 exhumeds — an exact match) |

Zero variance inside each group. So OpenOSRS' `exhumes.put(o, 11)` is right for regular
mode and its commented-out `18` is dead and wrong — **but Hard Mode closes them two ticks
sooner**, which no source in the plan mentions and which matters, because 9 ticks is a
tighter cover window on top of a 4-tick spawn cadence.

### Bonus: the phase-2 handoff

`TOB_XARPUS_PHASE` (P2) fires **9 ticks after the last exhumed despawns** in every raid
(trio: last spawn 97 → despawn 108 → P2 at 117; 4-man: 64 → 75 → 84), and Xarpus' first
P2 spit is **7 ticks after that** (blert's `FIRST_P2_TURN_TICK = 7`), then a flat 4-tick
cadence. P3 turns are 8 ticks apart in regular mode.

---

## M16 — Verzik: the exact P1 opening offset. **CLOSED**

P1 autos, across 29 completed Verzik rooms:

```
19, 33, 47, 61, 75, 89, 103, 117 …   (12 rooms)
20, 34, 48, 62, 76, 90, 104 …        (17 rooms)
```

* **First P1 auto on room tick 19 or 20**, then a flat **14-tick** cadence
  (blert's `P1_ATTACK_SPEED = 14`), unbroken to the end of the phase.
* The 19-vs-20 split is a one-tick offset in when blert's clock starts (which player
  entered first), not two different behaviours: within a raid the cadence never varies, and
  the split does not track scale or mode (trios appear in both groups).

So the opening offset is **19 ticks after the first player is in the room** (±1 tick of
recorder alignment), *not* an immediate attack and not one attack-speed-worth of delay.

---

## M20 — do boss attack clocks reset on a phase change or free-run? **SPLIT VERDICT — see the provenance audit below**

> **Corrected later in this document:** the P1→P2 figure below survives scrutiny, the
> P2→P3 figure does **not** — it is a constant blert asserts rather than observes. Read the
> [provenance audit](#provenance-audit--what-blert-observes-versus-what-blert-asserts)
> with this section.

Verzik, measured on all 29 Verzik rooms, with zero exceptions:

| Transition | First attack after the phase event |
|---|---|
| P1 → P2 | **+16 ticks**, 29 of 29 rooms |
| P2 → P3 | **+12 ticks**, 29 of 29 rooms — *but see the provenance audit: this is blert's own constant* |

A free-running clock would put the first attack of the new phase at a variable offset,
since the phase change happens whenever the HP threshold is crossed. A *constant* offset
across 29 rooms is the signature of a **reset**: the clock is re-armed by the transition
and the boss's first attack is scheduled a fixed number of ticks later. The blert plugin
encodes the same conclusion as constants (`P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_SPAWN = 3`
from the P2 npc spawn, `P3_TICKS_BEFORE_FIRST_ATTACK = 12`, and
`P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_REDS = 12` — reds re-arm it too).

Contrast Maiden (M1): her 10-tick clock is *not* re-armed by the 70/50/30 crab spawns —
582 of 582 gaps were 10. So the answer is per-boss: **a phase change that swaps the npc
resets the clock; an in-phase event that does not swap the npc does not.**

---

## M17 — Bloat: is the first walk 38–46 or 39–47? **CLOSED — 39–47**

`GET /api/v1/trends/bloat-downs?downNumber=eq1` — blert's aggregate over **98 445
recorded first downs**:

| walk ticks | 39 | 40 | 41 | 42 | 43 | 44 | 45 | 46 | 47 | 48 | 49 | 50 | 51 | 53 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| count | 20 477 | 15 975 | 12 715 | 10 121 | 8 361 | 6 884 | 5 314 | 4 098 | **12 964** | 637 | 441 | 274 | 182 | 2 |

**The first walk is 39–47 ticks. Not 38–46: there is not a single recorded 38.**

The shape says more than the range does:

* Counts fall off geometrically from 39 to 46 — consistent with a per-tick chance to go
  down, checked once he is eligible.
* **47 is a spike** (12 964, vs 4 098 at 46): a hard cap. If he has not gone down by 47
  he goes down at 47.
* The 48–53 tail (1 536 of 98 445, 1.6 %) is the documented extension: *"If the Bloat
  turns or changes speed during the walk, Bloat cannot go down for another 5 ticks"*
  ([Wiki](https://oldschool.runescape.wiki/w/Pestilent_Bloat)), which pushes a would-be
  47 out to at most 52–53.

Subsequent walks are a different, **shorter** window — same 9-wide shape moved down 5:

| down # | range | cap spike | n |
|---|---|---|---|
| 1 | 39–47 | 47 | 98 445 |
| 2 | 34–42 | 42 | 86 355 |
| 3 | 34–42 | 42 | 22 433 |

(A handful of 33s appear at down ≥ 2 — 15 of 86 355 — which is within what a mis-parsed
recording can produce; treat 34 as the floor.)

---

## M18 — Verzik: the Athanatos spawn rule. **CLOSED — floor + roll, with a gate**

> After every third lightning ball, there is a **25 % chance** of the nylocas being
> summoned again. **Should the summon trigger while the Athanatos is present, no nylocas
> will spawn.**
> — [Theatre of Blood/Strategies](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies)
> (local copy `sources/wiki_Theatre_of_Blood_Strategies.wikitext:925`)

So it is **floor + roll + a liveness gate**, all three:

1. a floor — the roll only happens after every third lightning ball;
2. a roll — 25 % on that occasion;
3. a gate — a live Athanatos suppresses the spawn entirely.

The gate is the mechanically interesting one, and it is the basis of the well-known
strategy of deliberately leaving the Athanatos alive to stop further nylo waves. An
implementation that models only the floor, or only the roll, will produce a visibly
different room.

---

## M19 — Entry Mode hitpoints: is the Wiki figure the 5-scale base? **CLOSED — no. It is per-player × 4.**

Three sources, one conclusion.

**1. The rev-239 cache** states a per-npc `stat4` on the Entry-mode ("`_story`") records:

| Boss | cache `stat4` (Entry) | cache `stat4` (Normal) |
|---|---|---|
| Maiden | 500 | 3500 |
| Bloat | 320 | 2000 |
| Sotetseg | 560 | 4000 |
| Xarpus | 520 | 5080 |
| Verzik P1 / P2 / P3 | 300 / 400 / 600 | — |
| Maiden Matomenos | 16 | 100 |

**2. The blert plugin** scales those numbers by party size, and *only* in Entry mode
([`TobNpc.getBaseHitpoints`](https://github.com/blert-io/plugin/blob/master/src/main/java/io/blert/challenges/tob/TobNpc.java)):

```java
if (mode == ChallengeMode.TOB_ENTRY) {
    if (isNylocas(this.id)) return hitpointsByScale[0];   // nylos are flat
    // Entry mode scales hitpoints linearly with party size.
    return hitpointsByScale[0] * scale;
}
switch (scale) { case 5: …[2]; case 4: …[1]; default: …[0]; }   // Normal/Hard scale DOWN
```

**3. The Wiki's Entry-mode infobox figures are all exactly 4 × the cache unit:**

| Boss | cache unit | × 4 | Wiki Entry infobox |
|---|---:|---:|---:|
| Maiden | 500 | 2000 | **2000** |
| Bloat | 320 | 1280 | **1280** |
| Sotetseg | 560 | 2240 | **2240** |
| Nylocas Vasilias | 360 | 1440 | **1440** |
| Verzik P1 / P2 / P3 | 300 / 400 / 600 | 1200 / 1600 / 2400 | **1200 / 1600 / 2400** |
| Maiden Matomenos | 16 | 64 | **64** |

Six for six. So:

* **Entry mode scales *up* from a per-player base**; Normal and Hard scale *down* from a
  5-man base (2625 / 3062 / 3500 for Maiden). Two different directions in one raid.
* **The Wiki's Entry figure is the 4-player value**, not the 5-scale base and not the
  per-player unit. Anything reading Wiki Entry numbers as a base must divide by 4.
* The Wiki's prose *"the Maiden can scale down to 20 % of her original Hitpoints when
  done in a solo party"* is consistent with the 5-man Entry value (2500 × 20 % = 500 =
  the cache unit), i.e. its "original" is the 5-man figure, not the 2000 in its own
  infobox. Two numbers on one page measured from different baselines.
* **Blert's Entry values for Xarpus and Verzik disagree with the rev-239 cache**
  (blert: Xarpus 680, Verzik 240/320/320; cache: 520 and 300/400/600). The Wiki agrees
  with the cache. Prefer the cache; blert's Entry-mode table looks stale, which is
  unsurprising — almost nobody records Entry raids, so an error there never surfaces.

---

## M4 — Maiden: spawn-tile selection for <10 crabs, and the spawn tick. **CLOSED**

Measured from `NPC_SPAWN` events for Nylocas Matomenos (npc 8366) across 27 Maiden rooms
at scales 1–5, 81 spawn events, 282 crabs.

### The tile table (data agrees with blert's constants exactly)

| Slot | Base tile | "Scuffed" tile |
|---|---|---|
| N1 | 3173, 4456 | 3174, 4457 |
| N2 | 3177, 4456 | 3178, 4457 |
| N3 | 3181, 4456 | 3182, 4457 |
| N4 inner | 3185, 4454 | 3186, 4455 |
| N4 outer | 3185, 4456 | 3186, 4457 |
| S1 | 3173, 4436 | 3174, 4435 |
| S2 | 3177, 4436 | 3178, 4435 |
| S3 | 3181, 4436 | 3182, 4435 |
| S4 inner | 3185, 4438 | 3186, 4437 |
| S4 outer | 3185, 4436 | 3186, 4435 |

(All coordinates are the crab's **south-west tile**. The scuffed variant is one tile east
and one tile *outward* — north row +y, south row −y.)

### Selection

* **Count = 2 × party size**, confirmed at every scale: 2 / 4 / 6 / 8 / 10 crabs for
  scales 1–5.
* **Scale 5 uses all ten slots, every spawn** — deterministic, no selection at all
  (6 five-man rooms × 3 spawns = 18 spawn events, all ten slots every time).
* **Scale < 5 draws a fresh random subset of size 2 × scale on every spawn** — the same
  raid uses different slots at 70 %, 50 % and 30 %.
* The draw looks **uniform over the ten slots**: over 60 sub-5 spawn events the per-slot
  counts were 33, 30, 30, 30, 30, 29, 28, 27, 23, 22 (mean 28.2, χ² ≈ 3.8 on 9 df,
  p ≈ 0.92 — no detectable bias).
* The north/south split is *close* to what drawing 2k of 10 uniformly would give, with a
  slight excess of balanced splits (trio: 3–3 in 13 of 21 spawns vs ~48 % expected). Not
  enough samples to claim a balance constraint; treat as uniform until a larger pull says
  otherwise.

### Timing — **on the transmog tick, not the next**

The Maiden's npc id changes (8360 → 8361 → 8362 → 8363) at each HP threshold. In **every
one of the 81 spawn events**, the crab `NPC_SPAWN` tick equals the id-change tick exactly:

```
047e7ac7  id changes at 43, 93, 134   crabs spawn at 43, 93, 134
0ffb0fcb  id changes at 44, 99, 157   crabs spawn at 44, 99, 157
029e4be8  id changes at 114, 216, 317 crabs spawn at 114, 216, 317
```

### Bonus: "scuffed" is a property of the spawn, not of the crab

In 2 of 60 sub-5 spawn events (~3 %) **every** crab in the set spawned on its scuffed
tile. No event mixed base and scuffed slots. So the scuff is decided once per spawn event
(all ten offset together), which is worth knowing: blert models `scuffed` per crab, but
the underlying thing appears to be per-spawn.

---

## M6 — Bloat: falling-flesh spawn rate, shadow→land delay, stun duration. **TWO OF THREE CLOSED**

### Shadow → land delay: **3 ticks**

Both sources agree, and one of them is measured.

> When Bloat spawns hands/feet, they hit the ground after **3 GameTicks**. However, they
> also create a new GraphicsObject for a blood squirt animation on that tick […] For any
> player standing on the same tile as the blood object, they are stunned.
> — [`BloatMistakeDetector.java`](https://github.com/QuestingPet/TobMistakeTracker/blob/master/src/main/java/com/tobmistaketracker/detector/BloatMistakeDetector.java)

Blert emits a `TOB_BLOAT_HANDS_DROP` (shadows appear) and a `TOB_BLOAT_HANDS_SPLAT`
(they land) event. Across every drop in 7 Bloat rooms, `splat_tick − drop_tick = 3`,
with no exceptions.

### Spawn rate: **16 hands per drop, and the drop cadence is HP-gated: 6 ticks → 4 ticks**

Hands only fall while Bloat is **walking**; no drops occur between his down and his next
up. Every drop event carried **16 hand tiles** (a few carried 15, consistent with two
hands landing on one tile or one landing outside the recorded chunk).

The cadence is not fixed — it tightens as he is damaged:

| Raid | drop gaps | Bloat HP % at those drops |
|---|---|---|
| `5905be94` | 6, 6 | 87 → 80 % |
| `0ffb0fcb` | 6 × 6 | 46.6 % |
| `047e7ac7` | 6 × 5 | 41.7 % |
| `7ca85464` | **6, 6, 6, 6, 6**, then **4 × 10** | **41.4 %** for the 6s, **2.3 %** for the 4s |
| `56216003` | 4 × 9 | 34.0 % |
| `74700b67` | 4 × 9 | 24.4 % |
| `d5ef6c12` | 4 × 10 | 20.6 % |

`7ca85464` is the decisive raid: the same Bloat dropped hands every 6 ticks in one walk
and every 4 in a later one. So **the rate is a function of his health, not of the walk
number or the party**. The threshold is bracketed to **(34 %, 41.3 %]** — 34.0 % already
gives 4-tick drops and 41.3 % still gives 6-tick. A larger pull aimed at rooms sitting in
that band would pin it (35 %, 37.5 % and 40 % are all plausible round numbers).

### Down/up cycle (bonus, and it re-confirms M17)

`TOB_BLOAT_DOWN` carries `downNumber`, `walkTime` and `upTicks`; `TOB_BLOAT_UP` gives the
rise tick. Examples: down 1 at tick 45 (`walkTime` 45) → up at 78 = **33 ticks down**;
down at 42 → up 75 = 33; down at 39 → up 72 = 33. The down phase looks like a flat
33 ticks in regular mode, and the first `walkTime` values (45, 42, 39, 47, 41, 39) sit
inside the 39–47 window M17 established.

### Stun duration: **STILL OPEN**

Neither plugin models it (TobMistakeTracker only needs the tick of the splat to flag the
mistake, and blert records the hit, not its consequence), and no source found gives a
tick figure — the Wiki says only *"stun them temporarily"* and secondary guides say
*"a few seconds"*.

**What would close it:** a client-side capture of the stun. The player's stun is visible
as the `Stun` graphic and, more usefully, as the tick range in which queued input is
discarded — record the tick of the hands splat and the first tick the stunned player can
act again. This is exactly the kind of thing the harness in
[`ENCOUNTER_TIMING.md`](minigames/theater_of_blood/ENCOUNTER_TIMING.md) is for.

---

## M9 (continued) — the Vasilias style clock, measured

The regular-mode figure from blert's guide is confirmed against recorded raids. The boss's
npc id encodes its style (8355 melee / 8356 range / 8357 mage), so the switch ticks are
visible in the `NPC_UPDATE` stream:

```
0f88919b  spawn 351 melee, then 360, 370, 380, 390, 400, 410, 420, 430, 440, 450, 460, 470, 480
634f5837  spawn 358 melee, then 367, 377, 387, 397, 407, 417, 427, 437
3319b641  spawn 317 melee, then 326, 336, 346, 356, 366, 376, 386
```

* Always spawns **melee**.
* **First switch 9 ticks after the spawn, then exactly every 10 ticks.** (The 9 is the same
  off-by-one as Maiden's first attack on tick 9: the spawn tick counts as tick 1 of the
  window.)
* **No style ever repeats back-to-back** in any observed switch, confirming the guide's
  "switches to a *different* random style".

Entry Mode is still unmeasured — see M9 above for why and how.

---

## M10 — Sotetseg: tick gap between maze end and his first post-maze attack. **CLOSED — 1 tick**

Sotetseg's npc id is the tell: he becomes the inactive form (8387) on the maze proc tick
and returns to the active form (8388) when the maze ends. Measured across **26 mazes**; six
rooms shown:

| Raid | maze proc | re-activates | first attack after | gap |
|---|---:|---:|---:|---:|
| `047e7ac7` | 61 / 143 | 89 / 173 | 90 / 174 | **1 / 1** |
| `0ffb0fcb` | 64 / 154 | 101 / 193 | 102 / 194 | **1 / 1** |
| `39c0d85d` | 68 / 153 | 100 / 188 | 101 / 189 | **1 / 1** |
| `3d30c636` | 57 / 147 | 98 / 194 | 99 / 195 | **1 / 1** |
| `56216003` | 53 / 140 | 77 / 169 | 78 / 170 | **1 / 1** |
| `5905be94` | 42 / 113 | 65 / 141 | 66 / 142 | **1 / 1** |

**25 of 26 mazes: he attacks on the tick immediately after re-activating**, then resumes
his 5-tick cadence. The one exception gave 5 ticks, in a room with a gap in the recording. The gap from his *last pre-maze* attack varies wildly (25–51 ticks)
because the maze's length is player-dependent — which is why this question looked open:
measuring from the wrong end gives noise. Measure from the re-activation and it is a
constant 1.

He can also attack **on** the proc tick itself (e.g. `047e7ac7`: attacks at 56 and 61 with
the proc at 61), so the maze does not cancel an attack already scheduled for that tick.

---

## M21 — Verzik: web hitpoints by party size. **CLOSED — flat 10, in every mode**

The rev-239 cache settles the contradiction between the Wiki's Web page ("10 Hitpoints")
and its Verzik page ("depends on the amount of team members"):

```
[verzik_web_npc]       // id 8376   name=<col=00ffff>Web</col>   stat4=10
[verzik_web_npc_hard]  // id 10854  name=<col=00ffff>Web</col>   stat4=10
[verzik_web_npc_story] // id 10837  name=<col=00ffff>Web</col>   (no stat4)
```

**10 hitpoints, identical in Regular and Hard.** What scales with the party is the
**number** of webs (one per player), not the hitpoints of each — which is almost certainly
what the Verzik page meant and said badly.

---

## M38 — does Nylocas Vasilias walk? **CLOSED — yes, it moves**

Measured from the boss's own coordinates in the `NPC_SPAWN`/`NPC_UPDATE` stream over its
full lifetime (6 raids, 80–190 ticks each):

| Raid | distinct tiles occupied |
|---|---|
| `0f88919b` | **4** — (3294,4246), (3294,4247), (3295,4246), (3296,4246) |
| `3319b641` | **2** — (3293,4246), (3293,4247) |
| `634f5837`, `19d4d96a`, `5a69843c`, `6a2a12a3` | 1 |

Two of six raids show the boss on more than one tile, once two tiles east of where it
landed. A stationary npc cannot do that, so **Vasilias is mobile** and the plan's decision
to leave it walking in `tob.npc` is right. It simply does not *need* to move often: it
attacks at range in every style, so it only walks when nothing is reachable — which is why
four of six raids show it never leaving its spawn tile.

---

## M11 — Sotetseg maze: verifying the empirical generator. **STILL OPEN, but a corpus is now reachable**

Blert emits `TOB_SOTE_MAZE_PATH` events carrying `activeTiles` — the maze tiles as they
light up — so real mazes can be harvested from recorded raids. 11 mazes were pulled as a
trial and the tiles do come through in maze grid coordinates (x 0–13, y 0–14, matching
devqhp's 14 × 15 grid).

**Do not use this trial as a verification yet.** Two things must be resolved first:

1. Each event carries only the tiles lit *at that moment* (usually one), so a maze is the
   union over its events. If the recorder joined late or the player skipped ahead, the
   union is a partial maze — several of the 11 have gaps.
2. The `maze` index field was `0` on 70 of 71 events in one raid that clearly ran two
   mazes, so the second maze's tiles risk being merged into the first. A merged pair looks
   like two tiles per row, which is exactly the shape that would *falsely refute*
   devqhp's "even rows are single tiles" rule.

**What would close it:** harvest maze paths at volume (hundreds of raids), keep only mazes
whose union covers all 15 rows with no gaps, split them by the `maze` field *and* by a tick
gap, and only then compare the row structure and the seed-to-seed `max_x_change ≤ 5` claim
against devqhp's generator. That is a bounded, mechanical job against an API that already
holds the data — the best available answer to M11 short of Jagex's source.

---

## Provenance audit — what blert *observes* versus what blert *asserts*

This matters more than any single number below. Blert's event stream mixes two kinds of
event, and only one kind is evidence:

**Observed** (derived from something the client saw — an animation, a projectile, a
graphics/ground object, an npc id change, a spawn or despawn):

| Event | Anchored to |
|---|---|
| Maiden attacks (auto vs blood throw) | animations 8092 / 8091 |
| Maiden crab spawns, blood trails | `NpcSpawned`, game object 32984 |
| Bloat hands drop / splat | graphics objects; splat is the blood-squirt object |
| Bloat down / up | npc state |
| Nylo wave spawns, cleanup end, boss spawn | npc spawns and despawns |
| Nylo boss style | npc id (8355/8356/8357) |
| Sotetseg attacks (melee/ball), maze proc/tiles | animations 8138 / 8139, ground objects 33033-33035 |
| Sotetseg active ↔ inactive | npc id (8388 / 8387) |
| Xarpus exhumeds (spawn, despawn, heal orbs) | ground object 32743, projectile 1550 |
| Xarpus phase changes | npc id change; P3 by overhead text |
| Verzik P1 autos, P2 autos | animations 8109 / 8114 |
| Verzik phase changes | npc id / transition animation |

**Asserted** (blert runs its own clock and emits attacks from it, resyncing off animations
where it can):

| Constant | Value | Consequence |
|---|---|---|
| `TICKS_PER_TURN_P2` (Xarpus spit) | 4 | Xarpus `TOB_XARPUS_SPIT` events are *generated* on this cadence |
| `TICKS_PER_TURN_P3` (Xarpus turn) | 8 | same |
| `FIRST_P2_TURN_TICK` | 7 | first spit offset is assumed |
| `P3_TICKS_BEFORE_FIRST_ATTACK` (Verzik) | 12 | the P3 opening offset is assumed |
| `P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_SPAWN` | 3 | seeds the P2 clock |
| `P1_ATTACK_SPEED`, `P3_ATTACK_SPEED`, … | 14, 7, 5 | cadences, though animations/projectiles resync them |

**So two of the results above must be downgraded, and are corrected here:**

* **M20, Verzik P2 → P3 (+12) is *not* an independent measurement.** It is
  `P3_TICKS_BEFORE_FIRST_ATTACK` echoed back: `startVerzikPhase` seeds
  `nextVerzikAttackTick = tick + 12` and P3 attack events are emitted from that clock. The
  supporting argument is weaker than 29-for-29 looks: had the 12 been wrong, blert's later
  P3 projectiles would not line up with its clock and the author would have noticed. Treat
  it as *"the constant a load-bearing plugin uses"*, not as *"measured"*.
* **M20, Verzik P1 → P2 (+16) does survive.** Blert's own seed for that transition is
  `+3`, and the recorded first-attack offset is 16 in 29 of 29 raids — a number blert did
  not assume, arriving from Verzik's P2 attack animation. The subsequent P2 cadence is a
  flat 4 (1 515 of 1 601 gaps; the rest are the reds pause at 20 and specials).
* **M1, M2, M4, M6, M8, M9, M10, M12–M15, M16, M38 all rest on observed events** and are
  unaffected.

---

## M31 — does Xarpus' spit cadence vary with party size? **NOT ANSWERED — the obvious dataset is circular**

The `TOB_XARPUS_SPIT` stream shows a flat 4 ticks at every scale (1 300+ gaps, no
exceptions) — but that is blert's `TICKS_PER_TURN_P2 = 4` replaying itself, so it proves
nothing. This is exactly the trap the SpaceScape trainer's note ("the `Players` setting
changes Xarpus' attack frequency") set up.

The **observed** substitute is the splat stream (`TOB_XARPUS_SPLAT`, anchored to projectile
1555 and graphics object 1556). Clustering splats into spits (bounces land within a tick or
two of the original) gives a modal gap of **4 ticks at scales 2, 3 and 4** — 61 of 158
trio gaps, 144 of 465 overall — with a broad spread from bounce chains being merged or
split by the clustering. So: **consistent with a flat 4 at all scales, not proof of it.**

**The filtered version was then run, and it exposed a confounder that rules this route out.**
Keeping only `source == XARPUS` splats inside the P2 window:

| Scale | raids | gaps | distribution | share of 4s |
|---|---|---|---|---|
| 3 | 8 | 165 | mode **4** (83) | 50 % |
| 4 | 3 | 56 | mode **4** (20) | 36 % |
| 2 | 4 | 84 | mode **5–6** (19 + 23), only 3 gaps of 4 | **4 %** |

The duo column looks like a slower cadence, and it is not: blert keys splats by **target
tile** and drops a splat whose tile is already recorded
(*"This splat has already been recorded, no need to do it again"*). With two players there are
far fewer distinct target tiles, so re-hits on an active splat tile are invisible and the gaps
inflate. The observable is censored exactly where the party is small — i.e. precisely where
the question is.

So: **trios and four-mans are consistent with a flat 4-tick spit, and duos/solos cannot be
measured this way at all.** Closing M31 needs the tick of Xarpus' *own* attack — his
animation or the projectile leaving him — which no public dataset records. It is a small
capture plugin: log the spit animation tick at scale 1, 2 and 5.

---

## M22 — the supply-chest point formula. **BANDS KNOWN, FORMULA UNPUBLISHED**

What is actually documented:

* **Range is 6–13 points per player, per chest**, and points carry over from the first
  chest (after Bloat) to the second (after Sotetseg) —
  [Chest (Theatre of Blood)](https://oldschool.runescape.wiki/w/Chest_(Theatre_of_Blood)).
* The bands, from the original Jagex newspost (*Theatre of Blood Changes & Deadman Summer
  Finals*): **above average 10–13, average 8–11, below average 6–9.** Note they overlap,
  so the roll inside a band is not a straight partition of 6–13.
* One hard gate is documented: *"If a player has died during both the Maiden and Pestilent
  Bloat fights, the chest will only contain an onion. As long as the player has taken the
  onion out, they will be eligible to receive more points for the next supply chest,
  provided they do not die in the next two fights"* —
  [Theatre of Blood/Strategies](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies).
  So it is **per-player**, not per-team, and two deaths in the two preceding rooms zeroes
  it.

**Still unpublished:** what "performance" is computed from. Jagex never said, no plugin
models it (no plugin needs to — the chest tells you the number), and the newspost's own
wording is qualitative. Note also that the Jagex newspost is served with HTTP 403 to
non-browser clients; the figures above come via the Wiki's citation of it, which is the
only reachable copy.

**Recommendation for the implementation:** treat the classification as a free parameter
with the documented gate and bands, and do not invent inputs. A wrong points formula
cannot break a raid; a wrong *gate* can (players expect the onion).

---

## M23–M30 — items from the timing pass

Two of these are now settled by the same event streams.

### Crab walk-vs-run: **they walk. Never more than one tile per tick.**

Nylocas Matomenos positions were tracked tick-by-tick across 27 Maiden rooms
(14 895 consecutive tick pairs, 4 812 of them moves):

| tiles moved in one tick | count |
|---|---|
| 0 | 10 083 |
| 1 | **4 812** |
| 2 or more | **0** |

Not a single two-tile step in any recorded raid, so the crabs use walk speed. (The
stationary ticks are frozen, blocked or attacking crabs.)

### Whether "one tick before" means the click or the move

Not resolvable from blert's stream — it records where a player *was*, and both readings
produce the same position history. This one needs the local harness: script an input at a
known tick and observe which tick the boss's scan reads. The pipeline in
[`ENCOUNTER_TIMING.md` §1](minigames/theater_of_blood/ENCOUNTER_TIMING.md) already predicts
both answers, so it is a discriminating test.

### The remaining items

The Verzik scan frame, the Xarpus turn/fire alignment, the underworld maze origin and the
per-weapon projectile flight times are all still open. The most promising single lever for
the projectile questions is the method that closed M3: **RuneLite's `Projectile`
`remainingCycles`/`endCycle` fields are exact**, and any plugin that reads them (as
TobMistakeTracker does for the Maiden's blood) recovers the flight-time law directly rather
than by stopwatch. Blert records projectile ids for Verzik and Xarpus but not their cycle
counts, so this needs a small capture plugin, not another API query.

---

## M7 — Nylocas pillars: damage per swing, and does pillar HP scale with party size? **STILL OPEN**

What is now certain from the cache (`sources/cache_npc_nylocas.txt`):

```
[tob_nylocas_support]        // id 8358   stat4=130
[tob_nylocas_support_story]  // id 10790  stat4=155
[tob_nylocas_support_hard]   // id 10811  stat4=150
```

Why this does **not** settle the scaling question: in this cache the Normal-mode boss
figures are the *5-man* values and the server scales them down for smaller parties (Maiden
3500 → 3062 → 2625). A single cache figure for the support is therefore equally consistent
with "130 flat" and with "130 at five players, scaled down below that". The cache cannot
distinguish them.

Blert does not track the supports at all — they never appear in the npc stream (the ids
present in the Nylocas room are only 8342–8357), so the API cannot answer it either.

**What would close both halves:** a capture of the support's HP varbit (or its death after
a counted number of nylo swings) in a trio and in a five-man. Damage per swing then falls
out of the same recording: count swings between HP readings. `^tob_nylo_pillar_hit_max = 2`
is the plan's disclosed figure and nothing found in this pass contradicts it.

---

## M37 — which pillar a split attacks. **STILL OPEN, but the survivorship bias is now fixable**

The blocker the plan describes — both samples biased toward the near pillar because dead
pillars redirect their nylos — is removable with the event API, because the stream gives
each nylo's full position history *and* its lineage:

* `NPC_SPAWN` for a nylo carries `nylo: {wave, parentRoomId, big, style, spawnType}`, so
  splits are identifiable and their **parent** is named (`parentRoomId`).
* `NPC_UPDATE` gives per-tick coordinates, so the pillar a split settles next to is
  recoverable without any pillar-target field.
* Raids where **no pillar dies** can be selected up front (a pillar collapse is a large,
  visible event), which removes the redirect entirely rather than correcting for it.

That is the clean version of the experiment the plan asks for: *"watch splits in a room
where nothing dies"* — the recordings already exist. Conditioning on `parentRoomId` also
directly tests the plan's 37 %/34 %/25 % figures for "parent's pillar / nearest / baseline".

Blert's own guide is worth quoting alongside, because it settles the *wave-spawn* half
beyond doubt and is silent on splits:

> After spawning, most Nylocas path toward one of the four pillars in the room to attack
> it. **Each wave-spawned Nylocas always targets the same pillar in every encounter.** […]
> Some wave-spawned Nylocas attack players instead of pillars. These are called aggros. As
> with all spawns, aggros are fixed across encounters. **Splits from bigs cannot be aggros.**

So wave spawns are deterministic (which is what makes the plan's M24 table valid), and
splits are the only stochastic part left.

---

## M39 — `tools/gen_npc_stats.py` overlay narrowing

Not a research question — it is an internal tooling change with no external evidence to
find. Nothing in this pass bears on it. It stays as the plan describes: narrow the skip to
blocks that actually declare stats, in its own diff, since it would emit generated stats
for ~352 npcs at once.

---

## Second pass — non-blert sources

Blert closed the timing questions but is a dead end for anything it does not record:
Entry Mode (**one** Entry raid exists in its entire database, and it is unfinished), the
Nylocas supports (they never appear in its npc stream), and anything the *server* decides
that the client cannot see. So the second pass went after two other kinds of source.

### Where the sources came from

A `gh search code` sweep for **server-side** class signatures — `"class Xarpus extends"`,
`"NylocasVasilias extends"`, `"PestilentBloat extends NPC"` — rather than plugin names.
That surfaced something the plan never had: **Zenyte**, a well-regarded OSRS-accurate
private server whose source is mirrored publicly (read from
[`Skryllzz/SSLCode`](https://github.com/Skryllzz/SSLCode), also mirrored in `matthewl99/Server-RSPS`
and `Rims-Naps/Zyrox-Server`; a fuller local copy now sits at
`/Users/matthewevers/Documents/git_repos/ZenyteLikeServer` — 98 ToB files versus the
mirror's ~60, and every constant cited below is **identical** in both, so they are the same
version of the codebase), with a complete `com.zenyte.game.content.theatreofblood`
package: every room, `PillarSupport`, `BloodTrail`, `XarpusExhumedPhase`, `Web`,
`NylocasAthanatosV`, and a Sotetseg maze **generator**.

**Its evidence tier is below plugin code and it must be read that way.** A recorder that
mis-times a tick produces a broken parse; a server that mis-implements a mechanic just
plays slightly wrong and nobody notices. Zenyte is a *reimplementation*, so it is
corroboration where it agrees with a measurement and a **lead** where it has a number
nobody else does — never an authority. Two places below it is provably wrong.

### Independent confirmations (Zenyte agreeing with measurements it cannot have copied)

| Quantity | Measured here | Zenyte | Verdict |
|---|---|---|---|
| Maiden attack period | 10 ticks | `TICKS_PER_ATTACK = 10` | agree |
| Maiden blood cooldown shape | cooldown flag + roll, never violated in 608 attacks | `canRollBloodAttack` — *"can only roll blood attack after 2 storm attacks"* | agree, same **shape** |
| Xarpus exhumed heal start | every tick from spawn **+3** | *"First three ticks are used for animation"*, heals while `ticks >= start+3` | agree |
| Xarpus exhumed window end | closes at **+11** | heal window ends `< start + 11` | agree |
| Xarpus exhumed count | 7 / 9 / 12 / 15 / 18 | 7 / **8** / 12 / 15 / 18 | agree on 4 of 5 |
| Xarpus heal per orb | 20 / 16 / 12 / 9 / 8 | 20 / 16 / 12 / 9 / **6** | agree on 4 of 5 |
| Xarpus spawn interval | 12 / 8 / 8 / 4 / 4 | **13** / 8 / 8 / 4 / 4 | agree on 4 of 5 |
| Bloat shadow → land | 3 ticks | hands drop on `ticks % 5 == 0`, land on `% 5 == 3` | agree |
| Bloat down duration | 33 ticks | `freeze(32)` on the stomp | agree within a tick |
| Vasilias switch period | 9 then every 10 | `ticks % 10 == 0` | agree |
| Vasilias never repeats a style | 185 switches, no repeat | `types.remove(type)` before the roll | agree |
| Verzik web hitpoints | cache `stat4=10` | `setHitpoints(10)` | agree |

The Xarpus row is the striking one. **20 / 16 / 12 / 9** is not a sequence anybody guesses,
and Zenyte parameterises it exactly the way the measurements came out
(`XarpusExhumedPhase(xarpus, maxSpawns, healAmount, spawnInterval, lifespan)`), so two
independent reconstructions — one from recorded raids, one from a server author watching the
game — produced the same table. Where they differ (scale 2 count, scale 5 heal, solo
interval) **prefer the measurement**: mine is 31 recorded rooms of the live game.

### M5 — the 29-vs-30 caveat is resolved: it is **30**

The measurement flagged that 29 looked like an odd constant and that a one-tick observation
error would make it 30. Zenyte's `BloodTrail`:

```java
private int ticks = 31;
...
case 30: maiden.addSplat(tile); World.spawnObject(this); return true;   // becomes active
case 0:  remove(); return false;                                       // gone
public void resetTimer() { ticks = 30; }                               // re-covered
```

**30 ticks of active life**, and `resetTimer()` restarts it when a spawn re-covers the tile —
which is exactly the mechanism inferred from the long tail (runs of 40, 50, 60, 90, 110).
Take the trail as **30 ticks**, with the measured 29 being the despawn seen a tick early.

### M2 — the roll is 1/3 or 1/4, and the measurement favours 1/3

Zenyte rolls `Utils.random(1, BLOOD_ATTACK_CHANCE) == 1` with **`BLOOD_ATTACK_CHANCE = 4`**,
i.e. **1/4**, gated by the same two-storm cooldown.

Measured here: **119 blood throws in 385 eligible attacks, p̂ = 0.309, 95 % CI
[0.263, 0.355]**. That interval **excludes 0.25** (two-sided p ≈ 0.01) and contains 1/3.
So the live game looks like **1/3**, and Zenyte's 1/4 is likely its author rounding down.
Not conclusive on a single sample this size — but the plan should carry 1/3 and note 1/4 as
the alternative, rather than treating either as settled.

### M6 — a stun figure, at last: Zenyte says **3 ticks**

`member.stun(3)` on a hand hit, and `Entity.stun(final int ticks)` confirms the unit is
ticks. This is the only number any source gives for the Bloat hand stun — the Wiki says
*"temporarily"*, secondary guides say *"a few seconds"*, and no plugin models it at all.

**Treat it as a lead, not a fact.** It is suspiciously equal to the 3-tick shadow→land delay
sitting two lines above it in the same file, which is exactly how a reimplementation
accidentally reuses a constant. Worth implementing as 3 with the tag, and worth the small
capture (splat tick → first tick the player can act) to confirm.

### M7 — Zenyte contradicts the cache, and the cache wins

```java
protected void setStats() {
    final var partySize = getRaid().getParty().getSize();
    combatDefinitions.setHitpoints(380 - (partySize * 20));   // 360 solo … 280 at five
}
```

Zenyte's supports scale **inversely** with party size — *more* pillar hitpoints in a smaller
team — and none of its five values (360/340/320/300/280) matches the cache's `stat4=130`.
This is invention, and it is the clearest demonstration in this pass of why an RSPS ranks
below the cache. **M7 stays open**, with the cache's 130/150/155 as the figures and the
scaling question unresolved (see that section above for why the cache cannot settle it).

Zenyte also **shuffles the pillar list** to pick a target
(`Collections.shuffle(pillars)`), which blert's measurement over 199 raids flatly
contradicts — wave spawns are deterministic. Another invention, and a useful warning about
taking Zenyte's *rules* rather than just its numbers.

### M11 — a **second, independent** maze generator, and it disagrees with devqhp's

Zenyte generates the maze rather than replaying one, so its generator is a rival hypothesis
to the devqhp trainer's. `ShadowRealmArea.generatePath`:

```java
MAZE_WIDTH = 14, MAZE_HEIGHT = 15
final var yIndexOffset = 2;
final var maxXChange  = 5;
int xIndex = Utils.random(0, MAZE_WIDTH - 1);            // start column
while (yIndex < MAZE_HEIGHT - 1) {
    int newXIndex = Utils.random(max(0, xIndex - maxXChange),
                                min(xIndex + maxXChange, MAZE_WIDTH - 1));
    // ... fill every tile between xIndex and newXIndex on this row  (a horizontal run)
    int newYIndex = yIndex + (Utils.random(1) == 0 ? yIndexOffset : yIndexOffset * 2);
    // ... fill every tile between yIndex and newYIndex at newXIndex (a vertical run)
}
```

Where the two agree — and this is worth a lot:

* the grid is **14 × 15**;
* **`max_x_change = 5`**. Two people reconstructing this independently both landed on 5,
  which promotes it from "devqhp's parameter" to a near-certain game constant;
* turns are horizontal runs on a row, joined by straight vertical segments;
* the path only ever moves **towards** the far row.

Where they disagree — and this is now the whole of M11:

| | devqhp trainer | Zenyte |
|---|---|---|
| rows advanced per turn | always **2** | **2 or 4**, rolled each turn |
| number of horizontal runs | always **7** | 4–7, varies per maze |
| start column | `random(1, 13)` — never the far-west column | `random(0, 13)` |

**That makes M11 a single yes/no question against real mazes: does every maze have exactly
seven horizontal runs?** If yes, devqhp is right and Zenyte's double-height step is
invention. If some mazes have five or six, Zenyte is right and devqhp's strict alternation
is an artefact of a corpus too small to show the rarer shape. This is a far sharper target
than "compare distributions over 10 000 mazes", and it is answerable from the maze corpus
blert can supply (with the two harvesting traps noted in the M11 section above).

### M9 — Entry Mode: the Wiki contradicts itself, and the specific number should win

Blert cannot help (one Entry raid, unfinished). But the Wiki states the interval twice, in
two places, and they disagree:

> The boss **takes longer** to switch colours compared to regular and hard mode.
> — [Nylocas Vasilias](https://oldschool.runescape.wiki/w/Nylocas_Vasilias), Entry Mode section

> The Nylocas Vasilias always starts grey, but will **change colours after 10 ticks
> (6 seconds)** … The boss will never remain in the same form twice in a row.
> — [Theatre of Blood/Entry Mode](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Entry_Mode)

The second is the Entry Mode page itself, it gives a number, and its 10 ticks matches
regular mode, blert's guide, Zenyte, and the 185 switches measured here. The first is a
vague comparative with no figure.

Supporting the "no difference" reading: the **cache shows no per-mode difference** in the
boss records at all — `nylocas_boss_{melee,magic,ranged}` and their `_story` counterparts
carry the identical `param=attackrate,int,4`, and the Entry boss records are structurally
identical to the regular ones. Anything mode-specific would have to be script-side.

**Recommendation: implement Entry at 10 ticks like every other mode**, tagged, and treat the
"takes longer" sentence as unsourced until somebody records an Entry raid. Note the plan's
§ on Entry Mode currently asserts a longer interval — that line should be softened.

### Verzik: two more corroborations, and one fresh contradiction

Zenyte's Verzik attack speeds match the measurements and blert's constants exactly:
`VerzikPhase2.ATTACK_DELAY = 4`, `VerzikPhase3.attackDelay = 7` dropping to `5` when
enraged. The observed P3 gaps here were 7 (428 of them) and 5 (203), so three sources agree.

The **phase openings do not agree**, which matters because M20's P2→P3 figure was already
unverified:

| | blert | Zenyte |
|---|---|---|
| P1 → P2 opening | first attack **+16** from its phase event (observed) | transition animation runs 10 ticks, then `ticksUntilAttack = 3` → **+13** from transition start |
| P2 → P3 opening | `P3_TICKS_BEFORE_FIRST_ATTACK = 12` (asserted) | transition animation runs 4 ticks, then `ticksUntilAttack = 3` → **+7** from transition start |

The two anchor their clocks differently — Zenyte from the start of its transition, blert from
the npc change it detects — so 16-vs-13 and 12-vs-7 are not straight contradictions. But
neither is either figure *confirmed*: blert asserts its 12 and Zenyte's 7 is its own reading.
**The P2 → P3 opening is the one Verzik number this pass could not pin**, and it needs the
same treatment M16 got: read it off the P3 attack *animation*, not off anybody's clock.

### Nylocas room cap: measured, and it does not scale with the party

Zenyte shrinks the cap for small teams:

```java
var cap = wave.getNylocasCap();
if (raid.getParty().getSize() <= 2) {
    cap = (cap == 12 ? 7 : 15);
}
```

That is testable *observationally*, because blert detects a **stall** by seeing that a wave
failed to spawn on a wave-check tick, and reports the live nylo count at that moment (both
are counts of real npcs, not derived constants — unlike its `roomCap` field, which is
computed and therefore useless here).

| Scale | stalls observed | live nylos at a pre-wave-20 stall | at a post-20 stall |
|---|---|---|---|
| 2 | 16 | **12**–13 | 24 |
| 3 | 57 | **12**–14 | 24 |
| 4 | 26 | **12**–13 | 24 |

A cap of 7 would produce stalls at 7–8 alive. **The minimum is 12 at every scale, including
16 stalls in duos** — so the pre-cap ceiling is 12 and the post-wave-20 ceiling is 24
regardless of party size, exactly as blert's guide states. Zenyte's small-party reduction is
invention. (Counts slightly above the cap — 13, 14 — are the documented behaviour that the
cap is checked *at* the spawn tick while splits can push the room over it in between.)

### M22 — a second source also failed to find the formula

Zenyte implements the chest with a scheme of its own — the top-damage player gets +2 points
at the Bloat chest and +1 at the Sotetseg chest, a death costs −4 — which matches nothing in
the published bands. That is not evidence about the game, but it *is* evidence about the
record: a server author with every incentive to copy the real formula could not find it
either. M22's formula should be treated as genuinely unpublished, not merely un-looked-for.

---

## Third source — Near-Reality, and a provenance warning about it

The user supplied a second server tree at `/Users/matthewevers/Documents/git_repos/RSPS-NEAR-REALITY`
(119 ToB files, Kotlin) as a fuller implementation than Zenyte's.

**Read the header before the constants.** Its ToB lives in the *same package*
(`com.zenyte.game.content.theatreofblood`) and its files carry `@author Tommeh` — Zenyte's
author — plus `@author Jire`. Its Sotetseg maze generator is a line-for-line Kotlin port of
Zenyte's (same `yIndexOffset = 2`, `maxXChange = 5`, same loop), and its Maiden keeps
`BLOOD_ATTACK_CHANCE = 4` with the same trailing comment. **Near-Reality's ToB is a
descendant of Zenyte's, not an independent reconstruction**, so where the two agree that is
*one* source, not two. What *is* informative is where a later maintainer **changed** a
value — that is a deliberate correction by someone with the live game in front of them.

### The corrections it made, and what they are worth

| Quantity | Zenyte | Near-Reality | Measured here |
|---|---|---|---|
| Xarpus exhumed lifespan | 9 | **11** | **11** (327 samples, regular mode) |
| Nylocas support hitpoints | `380 − 20 × party` | **flat 500** | cache says **130** |
| Hard Mode falling flesh | not modelled | `ticks % 3 == 0 && randomBoolean()` | Wiki: falls constantly in HM |

The Xarpus row is the useful one: an independent maintainer moved the lifespan from 9 to
**11**, landing on the figure measured here from 391 recorded exhumeds. Its whole exhumed
table is otherwise identical to Zenyte's — `(7,20,13,11) (8,16,8,11) (12,12,8,11)
(15,9,4,11) (18,6,4,11)` — so the count/heal/interval columns still corroborate the
measurements at four scales out of five.

### M7 — three sources, three numbers, and why the cache still wins

| Source | Nylocas support hitpoints |
|---|---|
| rev-239 cache `stat4` | **130** (Hard 150, Entry 155) |
| Zenyte | `380 − 20 × party` → 360 solo … 280 at five |
| Near-Reality | flat **500** |

Nobody agrees with anybody, and the two servers do not even agree on *whether it scales* —
which is the clearest possible sign that neither author knew. **M7 stays open**, and the
positive argument for the cache's 130 is worth stating because it is not circular: on every
ToB npc where a figure has been independently verified in this pass, `stat4` has been right —
Maiden 3500, small nylo 11, big nylo 22, Verzik web 10, and the whole Entry-mode per-player
table behind M19. A cache field with that track record beats two disagreeing
reimplementations.

Its other half — damage per swing — gets a first figure from Zenyte:

```java
if (target instanceof PillarSupport) { damage = large ? 4 : 2; }
```

A flat **2 from a small nylo, 4 from a big**, no roll. The plan's disclosed
`^tob_nylo_pillar_hit_max = 2` matches the small case; the big-nylo 4 is new, and is a lead
rather than a fact.

### M22 — the best reconstruction of the points system that exists

Near-Reality models the whole thing, and one detail proves its author was reading the real
game rather than inventing: the points live in **varbit 6460**, which our own rev-239 cache
names `tob_midwaychest_points` (`sources/cache_vars.txt:20`).

Its model (`TheatreOfBloodContributions.kt`):

```
START_CONTRIBUTION_POINTS                 = 8     # every player starts with 8
ENCOUNTER_PARTICIPATION_..._REWARD        = 3     # per encounter survived/participated
DEATH_..._PENALTY                         = 4     # per death
most-damage (MVP) bonus per room          = Maiden 2, Bloat 2, Nylocas 1, Sotetseg 1, Xarpus 2
                                            Verzik 2 per phase
POST_VERZIK_..._SUBTRACTED                = 8     # the starting 8 is removed at the end
```

and the party's `totalContributionPoints() / maxContributionPoints(playerCount)` ratio is
what its reward generator consumes. The **supply-chest** currency itself accrues at **+3 per
encounter** (`TheatreRoom.kt:210`, with a matching subtraction on death at `:410`).

Check that against the documented bands: at the first chest (after Maiden and Bloat) a player
has 3 + 3 = **6** with no bonuses, and up to 6 + 2 + 2 = **10** with both MVPs. The Wiki's
range is **6–13** and Jagex's bands are below-average 6–9 / average 8–11 / above-average
10–13 — so the floor matches exactly and the MVP bonuses land a top performer in the
above-average band. That is a genuinely credible skeleton: **a flat per-room award, plus
damage-based bonuses, minus a death penalty**, which is also the only shape consistent with
the one hard rule the Wiki does state (two deaths → onion).

It is still a reconstruction, and the exact inputs remain unpublished, so M22 stays a
disclosed parameter. But "+3 per room, +MVP, −4 per death" is a far better default than a
made-up classifier, and it reproduces the documented floor.

### What Near-Reality confirms without changing

`stun(3)` on a Bloat hand and `freeze(32)` on the down are carried over unchanged, so they
remain **one** source's figures (Zenyte's), not two. The Verzik web is `hitpoints = 10`,
agreeing with the cache. Nothing in the tree pins Verzik's P2 → P3 opening, which stays the
one Verzik figure this pass could not settle.

---

### What else the second sweep of Near-Reality gave, and did not give

Reading the files I had skipped rather than grepping for names:

* **M49 (the P3 melee chance, outside this list but in the plan's table) gets a second
  opinion at 50 %.** `ThirdPhase.kt` uses exactly the predicate TobMistakeTracker settled for
  M28 — adjacent and not overlapping — and then a coin flip:
  ```kotlin
  if (middleLocation.getTileDistance(victim.location) == 4
      && !CollisionUtil.collides(x, y, size, victim.x, victim.y, victim.size)
      && Utils.randomBoolean()) meleeAttack(victim)
  else if (Utils.randomBoolean()) magicAttack() else rangeAttack()
  ```
  The plan's disclosed 50 % now has an independent implementation behind it, and the
  magic-vs-range split is also a coin flip there.
* **Verzik P3 specials every 4 attacks** (`attackCounter == 4`) — matching blert's
  `P3_ATTACKS_BEFORE_SPECIAL = 4`.
* **Hard Mode falling flesh** is `ticks % 3 == 0 && Utils.randomBoolean()`, i.e. a coin flip
  every third tick while he walks, which is a concrete reading of the Wiki's "flesh falls all
  of the time in Hard Mode".
* **It does not help M7's damage-per-swing at all**: Near-Reality never ported the small
  nylocas' pillar attack — there is no `is PillarSupport` branch anywhere in its tree — so
  Zenyte's flat `large ? 4 : 2` remains the only figure in existence.
* **It does not help M20's P2 → P3 opening.** `ThirdPhase.onPhaseStart` sets the transform
  animations and resets `attackCounter`; no opening delay is modelled, so that figure is
  still unsourced in every implementation examined.

## The cache states four attack rates — and pointedly not the others

Near-Reality's Xarpus poison phase (the file I had missed on the first sweep) does not
inherit Zenyte's cadence — it sets its own:

```kotlin
private val startDelay     = TimeUnit.SECONDS.toTicks(4).toInt()   // ~6-7 ticks
private val attackInterval = TimeUnit.SECONDS.toTicks(3).toInt()   // 5 ticks
...
if (ticks >= startDelay && ticks % attackInterval == 0) { /* spit */ }
```

**5 ticks, flat, not scale-dependent** — against blert's asserted 4. Two implementations
disagreeing sent me to the one source that can arbitrate, which I should have checked first:
the npc's own `attackrate` param in the rev-239 cache.

| npc | cache `attackrate` | agrees with |
|---|---|---|
| `tob_bloat` (8359) | **1** | the flies hitting every tick |
| `tob_sotetseg_combat` (8388) | **5** | the 5-tick cadence measured here |
| `tob_xarpus_combat` (8340) | **4** | blert's constant, and the modal splat gap measured here |
| `verzik_phase3` (8374) | **7** | blert's `P3_ATTACK_SPEED`, Zenyte's `attackDelay` |
| `nylocas_boss_{melee,magic,ranged}` | **4** | Vasilias' two autos per 10-tick window |
| `verzik_nylocas_*` | **3** | — |

And the absences are as informative as the values: **Maiden carries no `attackrate` at all**
(zero occurrences in her entire cache extract, every phase), and neither does
`verzik_phase1` or `verzik_phase2`. So the raid splits cleanly in two:

* **Cache-stated cadences** — Bloat 1, Sotetseg 5, Xarpus 4, Verzik P3 7, Vasilias 4.
  Readable without measuring anything.
* **Script-driven cadences** — Maiden's 10, Verzik P1's 14, Verzik P2's 4. Nothing in the
  cache states them, which is precisely why they needed measuring, and why the figures in M1,
  M16 and M20 above are the only sources for them.

### M31 — therefore closed: the spit cadence is a flat 4, at every scale

`tob_xarpus_combat` carries **one** `attackrate` field, value **4**, with no per-scale
variant. A cadence that changed with party size would have to be scripted on top of it, and
no source suggests that: blert's constant is 4, the observed splat gaps are modally 4 wherever
the dedupe does not censor them (trio 50 %, four-man 36 %), and the cache states 4.

**Near-Reality's 5 is wrong, and looks like a units slip** — 3 seconds converts to 5 ticks,
whereas the real 4 ticks is 2.4 s. The Xarpus melee trainer's hint that the `Players` setting
changes his attack frequency describes the trainer, not the game.

That also disposes of the last worry in the M31 section above: the censored duo data was never
going to be needed, because the cache answers it outright.

---

## Status summary

| # | Room | Status after this pass | Basis |
|---|---|---|---|
| M1 | Maiden | **Closed** — first attack tick 9, then flat 10 (582/582 gaps) | observed, 26 rooms |
| M2 | Maiden | **Mostly closed** — cooldown never violated in 608 attacks; roll p̂ = 0.309, CI excludes Zenyte's 1/4 and contains 1/3 | observed + server source |
| M3 | Maiden | **Closed** — `50 + 15·d` cycles, `+25` for the two bonus splats, 11-tick splat | TobMistakeTracker source |
| M4 | Maiden | **Closed** — 10-slot table, uniform subset of 2×scale, on the transmog tick | observed, 81 spawns |
| M5 | Maiden | **Closed — 30 ticks.** Measured 29 over 3 366 runs; Zenyte's `BloodTrail` states 30 with a `resetTimer()` on re-cover, resolving the off-by-one | observed + server source |
| M6 | Bloat | **2 of 3 closed** — 16 hands per drop, 3-tick shadow→land, cadence 6→4 ticks HP-gated in (34 %, 41.3 %]. Stun length: Zenyte says **3 ticks**, the only figure anywhere, but it may be a copied constant | observed + both source trees |
| M7 | Nylocas | Open — three sources give three support HPs (cache 130, Zenyte `380−20×party`, Near-Reality flat 500) and the two servers disagree on whether it scales at all; damage per swing gets a first figure from Zenyte (small 2, big 4) | cache + 2 servers |
| M8 | Nylocas | **Closed** — first wave-check tick ≥ cleanup+16; 22/22 raids | observed |
| M9 | Nylocas | **Regular closed**, Entry **resolved on the balance of evidence** — the Entry Mode page says 10 ticks, matching every other source; the "takes longer" line is a numberless comparative on a different page, and the cache shows no per-mode difference | observed + Wiki + cache |
| M10 | Sotetseg | **Closed** — 1 tick after re-activation, 25/26 mazes | observed |
| M11 | Sotetseg | Open, but **reduced to one yes/no question**: do real mazes always have exactly 7 horizontal runs? Two independent generators agree on the 14×15 grid and `max_x_change = 5`, and disagree only on whether a turn advances 2 rows or 2-or-4 | two reconstructions |
| M12 | Xarpus | **Closed** — 7/9/12/15/18 by scale (HM trio 16) | observed |
| M13 | Xarpus | **Closed** — scaled: 12t solo, 8t at 2–3, 4t at 4–5 | observed |
| M14 | Xarpus | **Closed** — a heal orb every tick from spawn+3; 20/16/12/9/8 by scale | observed |
| M15 | Xarpus | **Closed** — 11 ticks (327 samples); **9 in Hard Mode** (64 samples) | observed |
| M16 | Verzik | **Closed** — first P1 auto tick 19–20, then flat 14 | observed |
| M17 | Bloat | **Closed** — 39–47, cap spike at 47; later walks 34–42 | 98 445 recorded downs |
| M18 | Verzik | **Closed** — every 3rd lightning ball, 25 %, suppressed while an Athanatos lives | Wiki/Strategies |
| M19 | All | **Closed** — Entry scales *up* per player; the Wiki figure is the 4-player value | cache + plugin + Wiki |
| M20 | All | **Split** — Verzik P1→P2 resets (+16, observed); P2→P3 +12 is blert's constant, not measured; Maiden's clock free-runs through crab spawns | mixed |
| M21 | Verzik | **Closed** — webs are 10 HP flat; the party scales their *number* | cache |
| M22 | All | Bands and the death gate documented; formula unpublished, but Near-Reality's reconstruction (+3/encounter, MVP bonuses, −4/death, on the correct varbit 6460) reproduces the documented 6-point floor | Jagex/Wiki + servers |
| M23–M30 | — | Crab walk-vs-run **closed** (never >1 tile/tick); rest open | observed |
| M31 | Xarpus | **Closed — flat 4 at every scale.** `tob_xarpus_combat` carries a single `attackrate=4`; blert's constant, the modal splat gap, and the cache all agree, and Near-Reality's 5 is a seconds→ticks slip | cache + observed |
| M37 | Nylocas | Open — but the bias-free experiment is now a query, not a capture | — |
| M38 | Nylocas | **Closed** — it moves (up to 4 distinct tiles in one fight) | observed |
| M39 | All | Not a research item | — |

**Net: 18 questions closed (M31 closed by the cache's `attackrate`), 3 partially closed, and for every one still open there is now a
named dataset or capture that would close it.**

---

## Applied to the tree — Maiden

Every Maiden finding above is now in the encounter. What each one changed:

| # | Finding | Was | Is | Pinned by |
|---|---|---|---|---|
| M1 | First attack on room tick **9** | `~tob_first_attack_delay` returned her 10-tick period, so she opened a tick late every raid | `^tob_maiden_first_attack_ticks = 9`, a separate constant because the two numbers answer different questions | `~tob_st_maiden_research` — asserts 9 *and* that it differs from the period |
| M2 | Cooldown **plus a 1/3 roll** | deterministic: blood on every 4th attack, forever, on the same beat | `^tob_maiden_blood_roll_in = 3`; a failed roll deliberately does **not** arm the cooldown, or a 1/3 roll becomes a 1-in-12 beat | `~tob_st_maiden_blood_cadence` — the simulated floor is now `>= 3` rather than `= 3`, since the gap is no longer fixed |
| M3 | `50 + 15·d` cycles, `+25` for the two bonus splats | one shot at one player, on `~player_projectile`'s own distance from the source tile | `~tob_maiden_flight`, distance from her **footprint** via `npc_range` | `~tob_st_maiden_flight` — the 65/80/95/110 table, and "+25 lands one tick later" at every distance |
| M4 | Scuffed is per **spawn event**, ~3 % | the twenty tiles existed but the fight always passed `false` | one roll per set in `~tob_maiden_spawn_set`; `^tob_maiden_scuffed_chance = 3` | `~tob_st_maiden_research` — the scuffed tile is one east and one *outward*, as a rule over all ten slots |
| M4 | Spawn on the **transmog tick** | already correct | unchanged | — |
| M5 | Trail is **30** ticks, and re-covering **restarts** it | 29, and a second `loc_add` queued a *second* revert so the first still fired — a re-covered tile expired 30 ticks after it was *first* painted | 30, and `mock230_world_loc_revert_queue` now replaces a pending revert for the same tile+shape | `~tob_st_maiden_research` — 30 for the trail, still 11 for her own pool, as two distinct objects |
| M19 | Entry scales **up** per player | `^tob_maiden_hp_entry = 2000` used as a base: a solo Entry Maiden had 4× her health | 500, the cache's `stat4` unit, multiplied by scale at the call site; Matomenos likewise 16 | `~tob_st_maiden_research` — `4 × unit` must equal the Wiki's 2000 / 64 |
| M23 | Crabs **walk**, never 2 tiles/tick | — | `npc_walk` + the waypoint stepper is walk speed; nothing sets a run | — |

All six mutation-proved: each constant was reverted to its pre-finding value and the
corresponding `::tobrun` check was confirmed to go red with the right message.

**Not applied, and why:** M2's roll is *probably* 1/3 — the CI excludes 1/4 but a single
385-attack sample cannot separate 1/3 from 0.30, so the constant carries the tag rather
than a claim. M4's "slight excess of balanced north/south splits" is below the sample's
resolution and is treated as uniform, as the finding recommends.

---

## Evidence stored in this repo

Raw event streams (~30 MB) were deliberately **not** committed; they are reproducible from
the scripts. What is committed, in
[`minigames/theater_of_blood/sources/blert_api/`](minigames/theater_of_blood/sources/blert_api/):

| File | Contents |
|---|---|
| `harvest3.py` | throttled fetcher (3 s/request) — challenge lists and per-stage event streams |
| `harvest2.py` | earlier paging fetcher, kept for the `startTime=lt<epoch_ms>` pagination idiom |
| `extract.py` | turns raw streams into the CSVs below; documents the event-type and NpcAttack id numbers |
| `trend_bloat_downs_all.json`, `_1`…`_4.json` | blert's own aggregates: 216 886 downs overall, 98 445 first downs (M17) |
| `trend_bloat_hands.json` | 5 802 952 hands over 26 095 Bloat rooms, by tile |
| `maiden_attacks.csv` | 608 attacks with tick and type (M1, M2) |
| `maiden_crab_spawns.csv` | 462 crab spawns with tile and whether the tick was a transmog tick (M4) |
| `bloat_events.csv` | 2 283 rows: downs, ups, hand drops/splats with Bloat's HP % (M6, M17) |
| `nylo_boss_spawn.csv` | 22 raids with the boss-spawn formula's prediction and whether it held (M8) |
| `nylo_boss_styles.csv` | 185 style switches (M9) |
| `sote_maze.csv` | 26 mazes: proc, re-activation, first attack after (M10) |
| `xarpus_exhumeds.csv` | 391 exhumeds: spawn, despawn, lifetime, heal amount, heal ticks (M12–M15) |
| `verzik_phases.csv` | 87 rows: P1 opening and both phase transitions (M16, M20) |

Re-run with:

```sh
python3 harvest3.py 11 6 10,12,13,14,15   # mode, raids, stages
python3 extract.py /tmp/blertdata
```
