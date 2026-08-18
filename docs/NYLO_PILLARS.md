# Theatre of Blood — Nylocas support pillars

Research log of **HP scaling** and **behavior** for the four Nylocas-room supports (not Verzik P1 pillars). Contradictory sources are kept, not discarded.

Gathered 18 August 2026 from Jagex newsposts, GitHub, local trees, OSRS Wiki, cache dumps, client plugins, player guides, and one RSPS-vs-live forum thread. YouTube transcripts of dedicated nylo guides almost never state an absolute pillar HP; collapse damage is the number they actually say out loud.

This is **not** Verzik’s Supporting Pillars (npc 8379 / 10840 / 10857). Those use a different formula in every Zenyte fork (`380 - partySize * 50`) and a different collapse rule. That Verzik formula is the same expression that best fits Jagex’s **Nylo** numbers — likely a swapped constant.

---

## Closest to live OSRS (ranked)

### HP — Jagex 21 June 2018 + cache 130 is the winner

Jagex published the live Regular numbers themselves, two weeks after launch, when they cut the first 21 waves and compensated by lowering pillar HP:

> Players in groups of five will find that the pillar has been reduced from **140 hitpoints to 130 hitpoints**. A solo player will find that the pillar has been reduced from **350 hitpoints to 330 hitpoints**.
>
> — [Theatre of Blood Changes & Deadman Summer Finals](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood_Changes_%26_Deadman_Summer_Finals) (21 June 2018)

That single paragraph settles four things that the earlier log treated as open:

1. Regular **5-man is 130**. Matches rev-239 `stat4` on `tob_nylocas_support` (8358). Cache is the 5-man figure, not a flat-for-all-sizes figure.
2. Pillars have **more HP at lower scale**. Solo 330 is 2.5× five-man 130.
3. Scaling **goes all the way to 1 player**. Mc’s “will scale down to 1” is the same wording they used two sentences earlier for bosses (“down to trio”). It is not “HP becomes 1.”
4. Pillars are **not** on the boss 75% / 87.5% curve. That curve was announced in the same post, in a different paragraph, for *bosses*.

No later Regular patch has published a new 5-man number. Cache is still 130. Treat 130 / 330 as current Regular unless a capture contradicts it.

**2 / 3 / 4-man Regular were never published.** The two Jagex points fit a straight line:

```
HP = 380 − 50 × partySize
```

| Party | Inferred Regular HP | Status |
|---|---|---|
| 1 | 330 | **Jagex published** |
| 2 | 280 | inferred |
| 3 | 230 | inferred |
| 4 | 180 | inferred |
| 5 | 130 | **Jagex published + cache** |

Pre-nerf (7–21 June 2018) was 350 solo / 140 five-man. That pair is *not* `400 − 50n` (that would be 150 at five). Launch may have been a lookup table, not this line. Do not back-apply `380 − 50n` to week-one ToB.

Zenyte’s `380 − 20n` (360…280) has the **380** and the inverse slope, but the step is 20 instead of 50. Same repo puts `380 − 50n` on **Verzik** pillars (`VerzikPillar.java`). That is the Nylo formula sitting on the wrong encounter.

Near Reality’s flat **500** matches nothing Jagex published. Combined HUD of four 130s is 520 — a plausible misread of the green bar.

Entry / Hard 5-man cache values (155 / 150) are still the best 5-man bases for those modes. 9 June 2021 hotfix: “Nylocas Pillar HP increased by **5–10, depending on group size**” in Story. That increment is scale-dependent and sits on top of whatever Story launched with; it is not the whole Entry table.

### Damage **to** a pillar — small 2 is still the best guess; 17/24 vs players is not pillar damage

Wiki [Nylocas Ischyros](https://oldschool.runescape.wiki/w/Nylocas_Ischyros) max hits (small **17**, large **24**, attack speed **3**) are the hits against **players**. If those applied to pillars, one ignored small would delete a 130 HP 5-man pillar in ~8 swings (~24 ticks after it arrives). Learners would lose pillars in the first few waves every time. They do not.

Zenyte / clientc `^tob_nylo_pillar_hit_max = 2` (small) and Zenyte large **4**, accuracy skipped, is the only formula that survives that duration check:

- 52-tick lifetime, ~10–15 ticks of walking, attack speed 3 → ~12–14 small swings if left alone → **24–28** damage per full-life small.
- 5-man 130 / 26 ≈ **five** full-life smalls to kill one pillar.
- Solo 330 / 26 ≈ **thirteen** full-life smalls.

That matches: ignore a 5-man pillar for a few waves and it dies; solo pillars “tank way more.” [Alora suggestion, Jul 2021](https://www.alora.io/forums/topic/73366-tobhm-nylocas-162260/) (live OSRS player, could not find official HP or pillar max-hit): “In OSRS you can let the pillars tank way more compared to Alora” and asked to “upscale the pillar HP depending on team size.” One sentence in that post also claims 162s and 260s share a pillar max-hit *and* that the big hits harder — it contradicts itself. Keep both readings; do not treat 2-vs-4 as closed.

### Collapse — room-wide rocks, about 30–50 Regular, 30+ Entry; not Zenyte’s 20–35

| Claim | Weight | Why |
|---|---|---|
| Rocks fall on **all players**; four down = instant wipe | **live** | Official [Support](https://oldschool.runescape.wiki/w/Support_(Theatre_of_Blood)) page |
| **Up to 50** to the **entire team** | **best Regular number** | Wiki Strategies (long-standing, not a Verzik-P1 mixup — that page separately says Verzik collapse is adjacent-only + stun) |
| **35** | typical / copied | [osrsguide.com](https://www.osrsguide.com/theatre-of-blood-osrs-guide/), [Gaming Elephant](https://www.gamingelephant.com/osrs-nylo-guide/) — same wording, likely one source |
| **30+** and can stack | **Entry** | Wiki Entry Mode table |
| Nylos that were on it **attack players** | **live** | Blert mechanics guide; Gaming Elephant; clientc M24 |
| `random(20, 35)` | **RSPS only** | Zenyte / NR. Floor of 20 fights Entry’s “30+”. |

YouTube P1 Verzik clips talk about **65** off-prayer and **60+** if you stand next to a falling Verzik pillar. That is the other encounter. Do not mix them.

### Targeting / waves — Blert + wiki, not Zenyte shuffle

Wave→pillar is **fixed** every raid (Blert; clientc 199-raid table). Aggros are a fixed subset. Splits cannot be aggros. 52-tick explode (Blert; wiki “~30 s”). Frozen nylos do not chew (Wiki Entry, explicit). Zenyte `getNextPillar` shuffle is invented. NR never spawns the horde.

---

## Identity (sources agree)

| Kind | Regular | Entry / story | Hard |
|---|---|---|---|
| Invisible HP npc | `8358` `tob_nylocas_support` | `10790` `tob_nylocas_support_story` | `10811` `tob_nylocas_support_hard` |
| Scenery | `32862` intact → `32863` breaking → `32864` broken | same | same |
| Collapse anim | `8074` (`nylocas_pillar_collapse`); precollapse `8073` | same | same |

Cache (rev-239 dump in [MRobertEvers/oldschool-clientc](https://github.com/MRobertEvers/oldschool-clientc) `docs/minigames/theater_of_blood/sources/cache_npc_nylocas.txt`):

```
[tob_nylocas_support]        // id 8358
  vislevel=0  interactable=no  size=3  footprintsize=153
  stat4=130

[tob_nylocas_support_story]  // id 10790
  stat4=155

[tob_nylocas_support_hard]   // id 10811
  stat4=150
```

`stat4` is Hitpoints in this dump (small nylos `stat4=11` / big `22` match the Wiki 5-man figures; Maiden `stat4=3500`, etc.).

Blert and tob-qol treat the same three npc ids as the pillar set:

- [blert-io/plugin](https://github.com/blert-io/plugin) `NylocasDataTracker`: `NULL_10790`, `NULL_8358`, `NULL_10811`
- [damencs/tob-qol](https://github.com/damencs/tob-qol) `NylocasConstants.PILLAR`
- [SuperNerdEric/combat-logger](https://github.com/SuperNerdEric/combat-logger) `TOB_NYLOCAS_SUPPORT[_STORY|_HARD]`

Four pillars sit at the corners of a 10×10 box. Measured footprints from the clientc encounter notes (occupancy, not the Wiki map pins):

- `(3290–3291, 4243–4244)` SW
- `(3290–3291, 4253–4254)` NW
- `(3300–3301, 4243–4244)` SE
- `(3300–3301, 4253–4254)` NE

Only the two room-facing sides are stood on → **at most four attackers per pillar**.

Zenyte / Near Reality spawn the HP npc at slightly different SW tiles (`3289,4253` / `3289,4242` / `3300,4253` / `3300,4242`) and attach object `32863` immediately (already the “breaking” loc). That is a geometry / object-state disagreement with the official 32862→32863→32864 sequence.

---

## HP numbers — every formula found

### H. Jagex newsposts (highest-weight live numbers)

[Feedback Tweaks](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_Feedback_Tweaks) (12 June 2018) promised, after cutting early waves: “we’ll also **lower the pillar health**.” Same post proposed **boss** HP 75% at three players. Pillars and bosses were never the same sentence.

[Theatre of Blood Changes](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood_Changes_%26_Deadman_Summer_Finals) (21 June 2018) then gave the numbers quoted in the verdict. [A Night At The Theatre](https://secure.runescape.com/m=news/a-night-at-the-theatre?oldschool=1) (2021) repeated that Hard Mode “will continue to scale … using the existing scaling system of 75% health at three (or fewer) players” — again **boss** language, not pillars.

[Theatre of Blood: New Modes](https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_New_Modes) hotfix 9 June 2021, Story Nylocas: “Nylocas Pillar HP increased by **5–10, depending on group size**.”

### A. Rev-239 cache (5-man bases)

| Mode | `stat4` | Reads as |
|---|---|---|
| Regular | **130** | 5-man, matches Jagex |
| Hard | **150** | 5-man Hard; never printed by Jagex |
| Entry / story | **155** | 5-man Entry after the 2021 hotfixes |

### B. Wiki — User:Mc/Mechanics/ToB

> HP Scales down according to the number of players, down to trio: 5-scale will have full HP as listed in the infobox, 4-scale 80%, 3-scale 60%.
>
> Small nylos are an exception: 11/9/8 and 22/18/16.
>
> **Nylo pillars will have more HP in lower scales, and will scale down to 1.**

https://oldschool.runescape.wiki/w/User:Mc/Mechanics/ToB — author contact on that page: `@mousecream` / Summit Discord. The rest of the page is tick-accurate (Maiden freeze tables, Verzik clap). The pillar sentence has no table.

Read against Jagex: “scale down to 1” = the curve continues to solo, unlike bosses which stop at trio. “More HP in lower scales” = 330 vs 130. The official [Support](https://oldschool.runescape.wiki/w/Support_(Theatre_of_Blood)) page still only says “a certain amount of hitpoints” and has no infobox.

### C. Zenyte lineage — `380 - (partySize * 20)`

Tommeh’s Java `PillarSupport` (June 2020). Same text in:

- local `ZenyteLikeServer/.../nylocas/npc/PillarSupport.java`
- [Rims-Naps/Zyrox-Server](https://github.com/Rims-Naps/Zyrox-Server)
- [Skryllzz/SSLCode](https://github.com/Skryllzz/SSLCode)
- [matthewl99/Server-RSPS](https://github.com/matthewl99/Server-RSPS)

```
HP = 380 - 20 * partySize
```

| Party | Zenyte Nylo | Jagex / inferred Regular |
|---|---|---|
| 1 | 360 | **330** |
| 2 | 340 | 280 |
| 3 | 320 | 230 |
| 4 | 300 | 180 |
| 5 | 280 | **130** |

Direction is right. Magnitudes are not. Five-man 280 is more than double live 130. Same tree’s Verzik pillars use `380 - 50 * partySize` (130 / 180 / 230 / 280 / 330) — the **Nylo** line, on Verzik.

`getMaxHitpoints()` recomputes from **current** party size, so a logout mid-room would change the max if the party set shrinks.

### D. Near Reality Kotlin rewrite — flat 500

Local and the three public NR dumps all override `setStats()` to ignore party size and cache:

```
maxHpScaled = 500
setHitpoints(maxHpScaled)
```

- this repo: `plugins/excluded/theatreofblood/.../nylocas/npc/PillarSupport.kt`
- [Winktabulous/regarded-dev](https://github.com/Winktabulous/regarded-dev)
- [Ecoscape16/near-reality-server-new](https://github.com/Ecoscape16/near-reality-server-new)
- [kurdowns/RSPS-NEAR-REALITY](https://github.com/kurdowns/RSPS-NEAR-REALITY)

If that override were removed, `TheatreNPC.setStats()` would apply the **boss** curve (less HP in smaller teams): 97 / 113 / 130 on cache 130. That is the opposite of Jagex and Mc.

500 is close to **4 × 130 = 520** (combined HUD). Treat as a combined-bar mixup until someone finds a design note.

### E. Client plugins — ratio only

OpenOSRS / Kronos theatre plugins never store an absolute max. On spawn they put `100` in a map, then each tick replace it with `npc.getHealthRatio()` (RuneLite’s 0–30-ish ratio, treated as a percent for overlay color).

Seen in:

- local `Kronos184-Fixed/.../theatre/rooms/nylocas/NyloHandler.java`
- [Leif-Yggdrasil/Kronos-184-Fixed](https://github.com/Leif-Yggdrasil/Kronos-184-Fixed)
- [MatthewBishop/Kronos184-Client](https://github.com/MatthewBishop/Kronos184-Client)
- [Dirro/osrs-plugins](https://github.com/Dirro/osrs-plugins)
- [karankurbur/OpenOSRSPlugins](https://github.com/karankurbur/OpenOSRSPlugins)
- copies under `oldschool-clientc/docs/minigames/theater_of_blood/sources/openosrs_theatre/`

This is **not** a HP formula. It only proves the live npc exposes a health bar.

### F. Kronos / Zelus / Okronos — no support implementation found

[tamerab1/Zelus-server-website-monorepo](https://github.com/tamerab1/Zelus-server-website-monorepo) `VasiliasRoom` / `Nylocas.java` have no support npc, no 8358, no 32862. Local `Kronos184-Fixed` ToB likewise has no nylocas pillars. These servers are **not** a source for HP.

### G. Plan constant for damage, not HP

`oldschool-clientc` plan / M7: `^tob_nylo_pillar_hit_max = 2`. That is **damage per small swing**, not pillar max HP.

---

## Older implied tables (kept; superseded for Regular 1 and 5)

Mc’s boss percents inverted, or applied the same direction, were guesses from before the Jagex quote was in this file:

| Guess | 5-man | 4-man | 3-man |
|---|---|---|---|
| Invert 80/60 (5=100%, 4=125%, 3=166%) | 130 | 162 | 216 |
| Same percents as bosses | 130 | 104 | 78 |
| `380 − 50n` (current best Regular line) | **130** | 180 | 230 |

Do not treat 162 / 216 as measured.

Entry 155 and Hard 150 being **above** Regular 130 is now expected (Entry hotfix + Hard wave speed). M19’s Entry *boss* rule (per-player × 4) was **not** applied to support `stat4`s.

---

## Damage dealt **to** a pillar

| Source | Small nylo | Large nylo |
|---|---|---|
| Zenyte / Zyrox / SSL `Nylocas.attack` | **2** if target is `PillarSupport` | **4** |
| clientc plan `^tob_nylo_pillar_hit_max` | **2** | (not stated; 4 would be a guess) |
| Wiki Ischyros infobox | **17** vs players (speed 3) | **24** vs players |
| Alora Jul 2021 (live player) | “same max hit” *and* “bigger hits harder” in one sentence | same |
| Wiki Strategies / Entry | silent on pillar hitsplats | silent |

Zenyte bypasses accuracy and max-hit rolls against pillars. Official accuracy against a `vislevel=0` unattackable npc is unpublished.

Wiki Entry: frozen nylos **cannot** attack pillars until unfrozen, even in melee range. Wiki Strategies Regular is weaker: “usually stop attacking a pillar for a short time.”

If 17/24 applied to pillars, 5-man 130 dies in 8 small or 6 large hits. Incompatible with how long live pillars last and with the Alora “tank way more” report. Keep 17/24 in the table as the **player** max-hit, not as a pillar formula.

---

## Collapse and wipe

| Source | One pillar dies | All four die |
|---|---|---|
| Official Support page | rocks on all players, “considerable damage” | instant death |
| Wiki Strategies | **up to 50** to the **entire team** | instant kill |
| Wiki Entry Mode | **30+**, can stack if several fall together | wipe |
| osrsguide.com / Gaming Elephant | **35**, remaining nylos go aggressive | wipe |
| Blert nylo guide | “significant damage”; nylos that were on it **start attacking players** | (implied fail) |
| clientc `ENCOUNTER_TIMING.md` | up to 50; **every nylo on it retargets a player** | instant team death |
| Zenyte `PillarSupport.onDeath` | `Utils.random(20, 35)` Regular hit, **delayed one tick**, only **alive** party members | `wipeTeam()` |
| Near Reality | `Utils.random(20, 35)` immediately on `validTargets` | `room.raid.activityFailed()` |

Contradictions still standing:

- **50** (Wiki Strategies) vs **35** (two third-party guides that copy each other) vs **30+** (Entry) vs **20–35** (RSPS).
- Whole room vs alive-only vs `validTargets`.
- Immediate vs delayed a tick.
- Official: surviving nylos **retarget players**. Zenyte split-spawn code retargets a **random remaining pillar**, then players only if none remain. NR has **no wave nylos**, so this path does not exist.

Zenyte also sets `PerfectNylocas = false` on the first pillar death. NR does not.

Wiki [Nylocas Vasilias](https://oldschool.runescape.wiki/w/Nylocas_Vasilias): Entry Mode still fails if all four pillars fall, even though horde HP can scale to 20% in a solo.

---

## Who attacks which pillar

| Source | Wave-spawned (pillar-bound) | Aggros | Splits from bigs |
|---|---|---|---|
| Blert guide | **Fixed** pillar every encounter | players only; fixed per wave | **cannot** be aggros |
| clientc M24 (199 raids) | deterministic table; same spawn tile can feed **different** pillars on different waves; not nearest-pillar | fixed | M37 **open** |
| Zenyte `getNextPillar` / `spawnRandomNylo` | **shuffle** remaining pillars | separate aggressive flag | shuffle remaining, else random player |
| Wiki Strategies | most focus pillars; a few “aggros” stop short and hit the team | — | — |

Zenyte’s random assignment **contradicts** every recording-based source. Near Reality never assigns because it never spawns the horde (see below).

---

## HUD

Wiki Strategies: during the build-up waves the **green bar is the combined health of all four pillars**. After Vasilias spawns it becomes the boss. Losing one equal pillar therefore drops the bar by about **25%**. Combined Regular 5-man max is **4 × 130 = 520**.

Near Reality `NylocasRoom.currentHitpoints`:

- while `phase == BOSS` or `phase == null`: Vasilias HP
- otherwise: **0 / max 1**

It never sums pillar HP. Zenyte refreshes the bar on every pillar hit (`refreshHealthBar(raid)`) but the room’s displayed current/max during waves was not re-read here in full.

---

## What this repo actually does

`NylocasRoom` constructs four `PillarSupport`s, spawns them and object `32863` on start, and sets `phase = NylocasPhase.BOSS`. `process()` then waits 8 ticks and every 5 ticks **spawns Vasilias**. There is no wave table, no pillar-bound nylo, no aggro, no 52-tick self-destruct.

So NR has pillar **objects + a flat 500 HP npc**, and a wipe if all four die, but **nothing in-tree deals the 2/4 pillar damage** the Zenyte horde used. Pillars only matter if something else hits them.

Hit-bar size on the pillar npc is **5** in both Zenyte and NR (`PillarSupportHitBar`).

If this tree is meant to match live Regular: `380 − 50 × partySize` (or at least hardcode 130 at five and 330 at one), collapse **up to 50** room-wide, and retarget surviving nylos onto **players**. Do not copy Zenyte’s 20-step or NR’s 500.

---

## Source list

### Jagex (live numbers)

1. https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_Feedback_Tweaks — 12 Jun 2018: will lower pillar HP when waves are cut; boss 75% at 3 is a **separate** proposal
2. https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood_Changes_%26_Deadman_Summer_Finals — 21 Jun 2018: **140→130** (5-man), **350→330** (solo)
3. https://oldschool.runescape.wiki/w/Update:Theatre_of_Blood:_New_Modes — 9 Jun 2021 Story hotfix: pillar HP **+5–10 by group size**
4. https://secure.runescape.com/m=news/a-night-at-the-theatre?oldschool=1 — Hard uses existing **boss** 75%-at-trio scaling (do not apply to pillars)

### Cache / research notes

5. [MRobertEvers/oldschool-clientc](https://github.com/MRobertEvers/oldschool-clientc) `docs/minigames/theater_of_blood/sources/cache_npc_nylocas.txt` — `stat4` 130 / 155 / 150
6. Same repo `docs/TOB_RESEARCH.md` **M7** (2/3/4-man Regular still uncaptured); M19 Entry boss scaling (do not silently apply to pillars)
7. Same repo `docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md` §4.5 — geometry, 50 damage, retarget
8. Same repo `docs/minigames/theater_of_blood/THEATRE_OF_BLOOD_PLAN.md` — npc id table, `nylocas_pillar_*` anims, `^tob_nylo_pillar_hit_max = 2`

### Wiki / mechanics notes

9. https://oldschool.runescape.wiki/w/Support_(Theatre_of_Blood) — rocks on all; four = wipe; scenery ids 32862–32864
10. https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies — combined HUD; collapse **up to 50**; explode ~30 s / 18–21
11. https://oldschool.runescape.wiki/w/Theatre_of_Blood/Entry_Mode — collapse **30+**; freeze stops pillar damage
12. https://oldschool.runescape.wiki/w/User:Mc/Mechanics/ToB — inverted pillar scale; “down to 1”
13. https://oldschool.runescape.wiki/w/Nylocas_Vasilias — Entry still wipes on four pillars
14. https://oldschool.runescape.wiki/w/Nylocas_Ischyros — player max hit 17/24, speed 3, HP 11/22 (5-man)

### Guides / forums / recorders

15. https://blert.io/guides/tob/nylocas/mechanics — fixed wave→pillar; collapse + player retarget; 52-tick explode
16. https://www.osrsguide.com/theatre-of-blood-osrs-guide/ — collapse **35**
17. https://www.gamingelephant.com/osrs-nylo-guide/ — collapse **35**; nylos on a fallen pillar go aggressive
18. https://www.alora.io/forums/topic/73366-tobhm-nylocas-162260/ — live player: OSRS pillars last longer; inverse scale requested; pillar max-hit **unfound** and self-contradictory
19. YouTube ToB / Entry guides (e.g. [M1t2qWMbzEs](https://www.youtube.com/watch?v=M1t2qWMbzEs), Verzik P1 shorts) — **no** Nylo-pillar HP spoken; Verzik clips say 65 / 60+ for the **other** pillars

### Server implementations (Zenyte family)

20. local `ZenyteLikeServer` `PillarSupport.java` + `Nylocas.java` — `380-20n`, 2/4 swings, 20–35 collapse
21. same tree `VerzikPillar.java` — `380-50n` (the Nylo line, wrong room)
22. [Rims-Naps/Zyrox-Server](https://github.com/Rims-Naps/Zyrox-Server) — same Nylo formula
23. [Skryllzz/SSLCode](https://github.com/Skryllzz/SSLCode) — same
24. [matthewl99/Server-RSPS](https://github.com/matthewl99/Server-RSPS) — same
25. this repo / regarded-dev / Ecoscape16 / kurdowns — **flat 500**, no horde

### Client / recorders (no absolute HP)

26. [blert-io/plugin](https://github.com/blert-io/plugin) `NylocasDataTracker` — ids; does not log HP
27. [damencs/tob-qol](https://github.com/damencs/tob-qol) — pillar npc + loc ids
28. [SuperNerdEric/combat-logger](https://github.com/SuperNerdEric/combat-logger) — pillar spawn starts the room
29. OpenOSRS `NyloHandler` (many forks, Kronos client) — `healthRatio` overlay

### Absent

30. Zelus / Okronos / local Kronos184 — no nylocas support code
31. Reddit `r/2007scape` JSON search returned 403; Google `site:reddit.com` nylo-pillar-HP queries returned no threads that state 130 / 330 / 2. The number lives in the Jagex newspost, not in surviving Reddit titles.

---

## What is still open

- **2 / 3 / 4-man Regular** — only the line through (1,330) and (5,130). A trio-vs-5-man hitsplat or health-ratio capture would confirm or kill `380 − 50n`.
- **Entry / Hard full tables** — 5-man cache 155 / 150 plus “+5–10 by size” is not enough to write duo/trio/solo Entry.
- **Small vs large pillar hitsplat** — 2 is the only number with a research-note and an RSPS implementation; 4 for larges is Zenyte-only. Alora’s post is unusable either way. Count swings against a known 130 in a 5-man.
- Collapse **distribution** — “up to 50” vs a typical 35 vs Entry 30+. Could be a range, scale-dependent, or two people rounding the same roll.

M7 in the clientc note is **half-closed**: Regular 5-man and solo HP are Jagex-canonical. The swing-vs-HP capture is still the way to close damage-per-hit.
