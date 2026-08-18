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

*(sections appended as the pass proceeds — see the running list below)*

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

Measured over 13 completed regular-mode raids (2 815 Maiden events):

| Raid | Maiden attack ticks |
|---|---|
| `047e7ac7` | 9, 19, 29, 39, 49, 59, 69, 79, 89, 99, 109, … |
| `0ffb0fcb` | 9, 19, 29, 39, 49, 59, 69, 79, 89, 99, 109, … |
| `630f537c` | 10, 20, 30, … |

* **12 of 13 raids: first attack on tick 9.** One on tick 10 (that raid's recorder
  joined the room a tick late — its whole clock is shifted by one).
* **Inter-attack gap: 10 ticks in 192 of 192 observed gaps.** No exceptions, no drift,
  including across the 70/50/30 crab spawns.

So the room's attack clock is armed at room start and first fires on the **10th tick**
(tick 9, 0-indexed). It does *not* fire on the tick the room starts.

**Corollary for M20:** the Maiden clock free-runs through the crab spawns — the 10-tick
cadence is unbroken across a transmog in every raid measured.

---

## M2 — Maiden: what decides blood splat vs blackstorm? **PARTIALLY CLOSED**

The cache/Wiki rule ("cannot use this attack again for the next two attacks") is
confirmed as a *minimum* gap, and the choice on an eligible tick is a **roll**, not a
fixed rotation.

Observed attack-type sequences (`1 = blackstorm auto`, `2 = blood throw`):

| Raid | attack # of each blood throw | gaps between blood throws |
|---|---|---|
| `047e7ac7` | 1, 6, 11 | 5, 5 |
| `0ffb0fcb` | 3, 6, 10 | 3, 4 |
| `39c0d85d` | 7 | — |
| `3d30c636` | 5 | — |
| `56216003` | 4, 7, 12 | 3, 5 |

* The **smallest observed gap is 3 attacks** — i.e. after a blood throw she throws two
  autos before she is eligible again. That is exactly the Wiki's "cannot use this attack
  again for the next two attacks", now confirmed against recorded raids rather than
  restated.
* Gaps of 3, 4 and 5 all occur, so eligibility is *necessary but not sufficient*: there
  is a per-attack roll on top. A geometric fit to the observed gaps puts the roll near
  ⅓ per eligible attack, but the sample here (11 gaps) is far too small to state a
  number — see "what would close it" below.
* She can throw blood on her **very first** attack (`047e7ac7`, attack #1), so the
  cooldown starts unset rather than "no blood before attack N".

**What would close it:** the same harvest at scale — every Maiden-stage event stream
blert holds, tabulating (gap since last blood throw → threw blood / did not). The
per-eligible-attack probability falls straight out of that table, and it is worth
checking whether it depends on party size or on how many players are in melee distance.

---

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

## M5 — Maiden: blood **spawn trail** lifetime. **STILL OPEN (but bounded)**

The 11-tick figure in the plan is the Maiden's own **splat** (M3 above) and is settled.
The **trail** left by a walking blood spawn is a different object:

* It is game object **32984** (`BLOOD_SPAWN_BLOOD_GAME_OBJECT_ID` in TobMistakeTracker,
  `MAIDEN_BLOOD_TRAIL_OBJECT_ID` in the blert plugin) — a *loc*, spawned and despawned by
  the server.
* **Neither plugin models its lifetime.** Both track it purely by
  `GameObjectSpawned`/`GameObjectDespawned`, which is what you do when the duration is
  unknown or not constant. That is evidence *against* there being a neat published number.
* The Wiki's "roughly 20–30 seconds" (33–50 ticks) is still the only figure.

**What would close it:** blert's Maiden event stream does not carry trail objects, so the
API cannot answer this one. It needs a client-side capture: log `GameObjectSpawned`/
`Despawned` for id 32984 with tick numbers over a few Maiden rooms. A fixed lifetime
would show as a constant despawn−spawn difference (as the Xarpus exhumed did — see M15,
where exactly this method gave a dead-flat 11).

---

## M8 — Nylocas: delay from the last wave-31 nylo dying to Vasilias landing. **CLOSED**

Two independent sources agree, and the second gives the exact rule.

Blert's own Nylocas mechanics guide states it in prose:

> After the waves, Nylocas Vasilias […] spawns. The spawn occurs on the start of the
> **5th cycle** following the despawn of the last regular Nylocas.
> — [blert.io/guides/tob/nylocas/mechanics](https://blert.io/guides/tob/nylocas/mechanics)

Measured against 11 recorded raids (`TOB_NYLO_CLEANUP_END` → `TOB_NYLO_BOSS_SPAWN`):

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
spawned (the room's cycle-0 phase), **every one of the 11 boss spawns satisfies
`(boss − w1) ≡ 0 (mod 4)`** — the boss always lands on a wave-check tick — and

```
boss_spawn = first wave-check tick >= cleanup_end + 16
           = cleanup_end + 16 + ((4 - ((cleanup_end - w1) mod 4)) mod 4)
```

reproduces **11 of 11** observed spawn ticks exactly. So: four full cycles after the last
nylo despawns, rounded up to the next cycle boundary — which is what "the start of the
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

| Scale | Exhumeds (measured) | OpenOSRS `XarpusHandler` |
|---|---|---|
| 5 | — (pull pending) | 18 |
| 4 | **15** (raids `0ffb0fcb`, `3d30c636`, `5905be94`) | 15 |
| 3 | **12** (8 raids, all 12) | 12 |
| 2 | — (pull pending) | 9 |
| 1 | — (pull pending) | 7 |

The recorded 3s and 4s match OpenOSRS' table exactly, which is good reason to trust the
rest of it (note solo is 7, not 6 — the sequence is 7, 9, 12, 15, 18).

### M13 — spawn cadence: **scales with party size**

| Scale | Spawn ticks | Cadence |
|---|---|---|
| 3 | 9, 17, 25, 33, 41, 49, 57, 65, 73, 81, 89, 97 | **every 8 ticks** |
| 4 | 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64 | **every 4 ticks** |

Dead regular in both cases, first exhumed at tick 8–11. So the answer to "fixed or
scaled?" is **scaled**, and dramatically so — a trio gets 12 exhumeds over 96 ticks, a
4-man 15 over 60. Scales 1, 2 and 5 are still to be pulled.

### M14 — heal cadence and amount

An **uncovered exhumed fires a heal orb every tick**, starting **3 ticks after it
spawns**. Clearest case (`3d30c636`, exhumed spawned on tick 8): `healTicks = [11, 12,
13, 14, 15]` — consecutive ticks from spawn+3 until a player covered it.

`healAmount` (the heal hitsplat on Xarpus) was **12 in every trio raid** and **9 in every
4-man raid** measured. That inverse relationship with party size is worth flagging: it
suggests the heal is a share of a fixed pool rather than a flat number, and it means an
implementation cannot use one constant.

### M15 — how long an exhumed stays open: **exactly 11 ticks**

`despawn − spawn` was **11 for every single exhumed in every raid measured** (12 raids ×
12–15 exhumeds ≈ 150 samples, zero variance). OpenOSRS' `exhumes.put(o, 11)` is right;
its commented-out `18` is dead and wrong.

### Bonus: the phase-2 handoff

`TOB_XARPUS_PHASE` (P2) fires **9 ticks after the last exhumed despawns** in every raid
(trio: last spawn 97 → despawn 108 → P2 at 117; 4-man: 64 → 75 → 84), and Xarpus' first
P2 spit is **7 ticks after that** (blert's `FIRST_P2_TURN_TICK = 7`), then a flat 4-tick
cadence. P3 turns are 8 ticks apart in regular mode.

---

## M16 — Verzik: the exact P1 opening offset. **CLOSED**

P1 autos, across 13 completed raids:

```
19, 33, 47, 61, 75, 89, 103, 117 …   (7 raids)
20, 34, 48, 62, 76, 90, 104 …        (6 raids)
```

* **First P1 auto on room tick 19 or 20**, then a flat **14-tick** cadence
  (blert's `P1_ATTACK_SPEED = 14`), unbroken to the end of the phase.
* The 19-vs-20 split is a one-tick offset in when blert's clock starts (which player
  entered first), not two different behaviours: within a raid the cadence never varies.

So the opening offset is **19 ticks after the first player is in the room** (±1 tick of
recorder alignment), *not* an immediate attack and not one attack-speed-worth of delay.

---

## M20 — do boss attack clocks reset on a phase change or free-run? **CLOSED for Verzik and Xarpus; Maiden free-runs**

Verzik, measured on all 13 raids, with zero exceptions:

| Transition | First attack after the phase event |
|---|---|
| P1 → P2 | **+16 ticks**, every raid |
| P2 → P3 | **+12 ticks**, every raid |

A free-running clock would put the first attack of the new phase at a variable offset,
since the phase change happens whenever the HP threshold is crossed. A *constant* offset
across 13 raids is the signature of a **reset**: the clock is re-armed by the transition
and the boss's first attack is scheduled a fixed number of ticks later. The blert plugin
encodes the same conclusion as constants (`P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_SPAWN = 3`
from the P2 npc spawn, `P3_TICKS_BEFORE_FIRST_ATTACK = 12`, and
`P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_REDS = 12` — reds re-arm it too).

Contrast Maiden (M1): her 10-tick clock is *not* re-armed by the 70/50/30 crab spawns —
192 of 192 gaps were 10. So the answer is per-boss: **a phase change that swaps the npc
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
