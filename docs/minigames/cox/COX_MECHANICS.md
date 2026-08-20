# Chambers of Xeric — tick-by-tick mechanics reference

Companion to [`COX_PLAN.md`](COX_PLAN.md). That file is the build order; this one
is the behavioural specification the implementation has to reproduce.

**Every number here carries its source.** Where sources disagree, both are shown
and the conflict is left open rather than silently resolved — a plausible-looking
constant with no provenance is how a boss ends up subtly wrong for months.

## Confidence markers

| | Meaning |
| --- | --- |
| ✅ | Read directly from this repo's cache, or from working plugin source |
| 📖 | OSRS Wiki |
| 🔧 | RuneLite / OpenOSRS plugin code — a *working implementation*, so tick values are load-bearing, but it is a client-side model of the server and can be approximate |
| 🎥 | Video guide transcript (machine transcription — verify before encoding) |
| 💬 | Community/forum/Jagex-dev statement |
| ❓ | **Not found.** Listed explicitly so the gap is visible |

## Sources

Downloaded into [`sources/`](sources/):

| File | What it is |
| --- | --- |
| [`synq_transcript.md`](synq_transcript.md) | Synq, *Remastered Solo Raids Guide 2026*, 3:54:55, 99 chapters. The deepest Olm-mechanics source found — 40+ chapters on Olm alone |
| [`sources/transcript_tubby_solo_cox.md`](sources/transcript_tubby_solo_cox.md) | Tubby, *LEARN CHAMBERS, MAKE BILLIONS*, 51:49, 2025-07-04 |
| [`sources/transcript_edb0ys_cox.md`](sources/transcript_edb0ys_cox.md) | TheEdB0ys, *Full Chambers of Xeric Guide*, 1:35:22, 2022-08-17 |
| [`sources/runelite/CoxPlugin.java`](sources/runelite/CoxPlugin.java) | OpenOSRS CoX helper — the tick state machine |
| [`sources/runelite/CoxOverlay.java`](sources/runelite/CoxOverlay.java), [`CoxConfig.java`](sources/runelite/CoxConfig.java), [`NPCContainer.java`](sources/runelite/NPCContainer.java) | supporting plugin code |

Not downloadable: **Woox**, [*Efficient Raids Solo Guide*](https://www.youtube.com/watch?v=ox1leZ5Z44E)
(41:42, 2017-02-20) — the foundational solo-CoX video from the player who also
did the first Inferno solo. YouTube serves **no captions** for it, auto or
manual, so it is cited but not transcribed. Worth a manual pass by someone who
can watch it.

Live sources:
- [Chambers of Xeric](https://oldschool.runescape.wiki/w/Chambers_of_Xeric) ·
  [/Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies) ·
  [**/Talk:Strategies**](https://oldschool.runescape.wiki/w/Talk:Chambers_of_Xeric/Strategies) — the scaling formulas live on the *talk* page, not the article
- [Great Olm](https://oldschool.runescape.wiki/w/Great_Olm) ·
  [Perfect Olm (Solo)](https://oldschool.runescape.wiki/w/Perfect_Olm_(Solo)) — the CA that enumerates every avoidable attack
- [Challenge Mode](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode) ·
  [/Strategies](https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Challenge_Mode/Strategies)
- [Jagex dev blog: *Chambers of Xeric: Scouting & Scaling*](https://secure.runescape.com/m=news/chambers-of-xeric-scouting--scaling?oldschool=1)
- [RuneLite raids plugin](https://github.com/runelite/runelite/blob/master/runelite-client/src/main/java/net/runelite/client/plugins/raids/RaidsPlugin.java) ·
  [de0 plugin hub (room times, Olm phases, tree/thaw timers)](https://github.com/dey0/pluginhub-plugins) ·
  [CoX-Scouter-External](https://github.com/Blackberry0Pie/CoX-Scouter-External)

---

## 1. Global: scaling

The single most important formula set, and it lives on the **talk** page 📖💬:

| Quantity | Rule |
| --- | --- |
| **HP per party member** | For each *maxed* member added (126 cb, 99 hp), a monster **adds its base HP to itself**. Tekton: 300 solo → 600 duo → 1200 in a team of four |
| **Defence per member** | ≈ **+2** per additional party member |
| **Other base stats per member** | ≈ **+8 %** |
| **Combat level below 126** | ≈ **−2 HP** per missing combat level |
| **Hitpoints level below 99** | ≈ **−1.3 defence** per missing HP level |
| **Challenge Mode** | "just about everything is multiplied by 1.5" |
| **Scaling target** | Scales to the **highest combat level** in the party; full scaling at **115+** |

⚠️ Every one of these is prefixed "about" by its source. They are the
community's regression fits, **not** Jagex's formula. Treat them as the
acceptance target for a solo max-stat raid (where they reduce to the clean base
values) and as approximate elsewhere.

❓ **The exact point cap formula is unknown.** The wiki states it "is currently
believed to scale non-linearly with the number of players… the exact formula is
not currently known." Do not invent one; solo is well-defined, so build solo
first.

---

## 1b. The room library — independently verified

[`sources/de0/CoxUtil.java`](sources/de0/CoxUtil.java) 🔧 identifies rooms by a
packed `(plane, x, y)` key. Decoding its 30 room constants gives the **complete
room library**, and it **agrees exactly with the template survey** I measured
off this repo's own map data ([`COX_PLAN.md` §0.3](COX_PLAN.md)) — two fully
independent sources, one a third-party plugin and one the cache itself.

**Every room exists in exactly three door-orientation variants**, selected by
the x value:

| x | Variant |
| --- | --- |
| 102 | **CCW** |
| 103 | **THRU** |
| 104 | **CW** |

Room type is `(plane, y)`:

| Plane | y | Room |
| --- | --- | --- |
| 0 | 160, 161 | floor ends |
| 0 | 162 | lobby |
| 0 | 163 | scavengers (small) |
| 0 | 164 | **lizardman shamans** |
| 0 | 165 | **Vasa Nistirio** |
| 0 | 166 | **Vanguards** |
| 0 | 167 | **Ice demon** |
| 0 | 168 | **Thieving** |
| 0 | 170 | farming / fishing (resource) |
| 0 | 178, 179 | floor starts |
| 1 | 164 | **Skeletal mystics** |
| 1 | 165 | **Tekton** |
| 1 | 166 | **Muttadiles** |
| 1 | 167 | **Tightrope** |
| 2 | 164 | **Guardians** |
| 2 | 165 | **Vespula** |
| 2 | 167 | **Jewelled crabs** |

**Cross-check against my own survey** ✅ — every plane matches:

| Room | Plugin plane 🔧 | My survey plane ✅ |
| --- | --- | --- |
| Tekton | 1 | m51_82 **plane 1** ✓ |
| Tightrope | 1 | m51_83 **plane 1** ✓ |
| Crabs | 2 | m51_83 **plane 2** ✓ |
| Guardians, Vespula | 2 | m51_82 **plane 2** ✓ |
| Ice demon | 0 | m51_83 **plane 0** ✓ |
| Thieving | 0 | m51_84 **plane 0** ✓ |

**Why this matters for the implementation.** The three variants are *authored
door layouts*, not runtime rotations — the same shape as the Gauntlet's three
door archetypes. So the layout generator picks a **variant**, it does not rotate
geometry. That confirms the choice already made in
[`cox.rs2`](../../../OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox.rs2)
to stamp rooms with `^map_instance_turn_none`.

⚠️ The plugin's x/y are its own key encoding, **not** world coordinates — do not
try to convert them to map squares. Use them for the *taxonomy* (which room,
which variant, which plane) and the survey manifest for the *geometry*.

---

## 2. The Great Olm — tick-by-tick

This is the encounter that most needs to be exactly right, and it is also the
best documented.

### 2.1 The attack-step rotation 📖

Olm runs a **fixed 12-step rotation**, quoted from the wiki:

```
 1  standard attack
 2  empty event
 3  standard attack
 4  Crystal Burst
 5  standard attack
 6  empty event
 7  standard attack
 8  Lightning
 9  standard attack
10  empty event
11  standard attack
12  Teleport
    -> loops
```

### 2.2 Step timing 🔧

From [`CoxPlugin.java` `handleOlm()`](sources/runelite/CoxPlugin.java):

- `Olm_TicksUntilAction` resets to **4** after every action → **Olm acts once
  every 4 ticks.**
- `Olm_ActionCycle` counts **4 → 3 → 2 → 1**; when it fires at 1 it resets to 4
  and advances `Olm_NextSpec`. So **every 4th action is the special slot**,
  which is the 12-step rotation above expressed as 3 specials × 4 steps.
- `Olm_NextSpec` enum, verbatim from the source comment
  `// 4 = heal 3= cry 2 = lightn 1 = swap`:

  | Value | Special |
  | --- | --- |
  | 4 | Heal (life siphon) |
  | 3 | Crystal burst |
  | 2 | Lightning |
  | 1 | Swap / teleport |

- **Phase-dependent wrap:** when `Olm_NextSpec` reaches 1, it resets to **4 if
  `OlmPhase == 1`**, otherwise to **3**. In plugin terms the heal special is
  reachable in only one phase — the head/final phase — and the other phases
  cycle through crystals → lightning → teleport only. This matches the wiki's
  rotation, which lists no heal.

⚠️ The 4-tick action clock is from the **plugin**, not the wiki. It is the most
load-bearing single number in the encounter (every 3:1 and 4:1 method is derived
from it) and it is consistent with the community's "Olm has a 4 tick attack
cycle" 💬, so confidence is high — but it should be confirmed against the real
server before it becomes a constant.

### 2.3 Head turn, skip, and catch-up 📖 — the mechanic the whole fight rests on

Three rules, and getting any one wrong breaks every documented strategy:

1. **Skip:** if **no player is in the zone Olm is targeting**, he swerves his
   head to find one and **skips that attack** in the rotation.
2. **No free skip:** if a player **is** in the zone and Olm swerves anyway,
   **nothing is skipped.** The skip is caused by the empty zone, not by the
   head movement.
3. **Catch-up:** if a standard attack was skipped, on the next step Olm uses a
   standard attack **in addition to** that step's own action — so a standard and
   a special can land on the same tick. Crucially, **"Olm will only try to catch
   up an attack from one step prior"** — the debt does not accumulate. That
   bound is exactly what lets a player suppress specials by positioning.

Transcript treatment: Synq calls this
[*"Head Turn Mechanics (Arguably the MOST IMPORTANT SECTION)"* 1:51:38](https://www.youtube.com/watch?v=klhBxOH8reQ&t=6698) 🎥.
Build this first; nothing downstream is testable until it is right.

### 2.4 Special-attack frequency 📖

- **1/3** chance of a phase-specific attack normally.
- **1/2** during the final phase.
- Olm requires **two standard attacks before any special** 💬 — the fact that
  makes the "force the head, then attack the hand" opener work.

### 2.5 Basic attacks 📖

- Two: **magic** (large green orbs) and **ranged** (crystal chunks). Identical
  damage.
- **1/5 chance to switch style** on each head attack.
- Prayer reduces damage by **roughly 75–80 %**.

### 2.6 Prayer orbs (spheres) 📖

Coloured spheres demanding a specific overhead; **~50 % of current HP** if
unprotected:

| Colour | Prayer |
| --- | --- |
| Red | Protect from Melee |
| Green | Protect from Missiles |
| Purple | Protect from Magic |

⚠️ The chat line is **colour-coded to match the sphere** 🎥 — "they will be
colour coded in the chat though so you don't even actually have to see the
spheres at all, you can just check your chat"
([edb0ys 1:10:23](https://www.youtube.com/watch?v=uM2VZicSSZM&t=4223)). The
message *is* the tell; an uncoloured line loses half the mechanic. RuneLite is
no evidence either way here — `CoxPlugin` runs `Text.standardize()` before
matching, which strips the tags.

### 2.7 Phase-specific attacks 📖

| Phase | Attack | Numbers |
| --- | --- | --- |
| Crystal | Falling crystals | 11 crystals. Direct hit (under the shadow) **16–25**; indirect (1 tile off) **10–16**. ⚠️ The same page elsewhere says "up to 15–20" — **conflict, unresolved** |
| Crystal | Crystal bombs | up to 3 (2 for small groups); **minimum 15**, **+15 per tile closer** |
| Flame | Deep burn | green fireball; **5 damage every few ticks**, **−2 to stats** each tick ❓ exact tick interval |
| Flame | **Fire wall** | two walls; **50–65 damage over 5 seconds**. Implemented in-cache as an **NPC**, `olm_firewall_npc` (7558) ✅ |
| Acid | Acid spray | pools; **3–6 damage per tick** standing in one |
| Acid | Acid drip | marks a player, who then *generates* pools as they walk |
| Any non-head | Crystal burst | a seedling under every player |
| Any non-head | Lightning | runs north and/or south; **disables overhead prayers** |
| Any non-head | Teleport | damage scales with distance from the target |
| Final | Life siphon | two blue projectiles mark tiles; several ticks later everyone **not** on a marked tile is damaged and Olm heals **5× the total** |

### 2.8 Auto-heal and clenching 📖

- **Auto-heal:** during non-head phases, damage to the head is **fully healed
  after several ticks** ❓ exact tick count.
- In the **penultimate** phase the **left hand** gains the same behaviour, and
  there it is worse than a no-op: *"any attacks that deal damage will cause it
  to heal instead"*, signalled by an **infinity symbol**. ⚠️ A sign flip, not a
  no-op — test it explicitly.
- **Clenching:** the left hand clenches (untargetable) on a hit of at least
  **1/20 of its base health** — ≥30 on a 600 HP hand.

### 2.9 Phase count and stats 📖 ✅

**At least 4 phases, +1 per 8 players.**

| | Head | Left claw | Right claw |
| --- | --- | --- | --- |
| Combat level 📖 / `vislevel` ✅ | **1043** | **750** | **549** |
| Cache id ✅ | 7554 `olm_head` | 7555 `olm_hand_left` | 7553 `olm_hand_right` |
| Spawning-form id ✅ | 7551 | 7552 | 7550 |
| Dying-form id ✅ | — | 7557 | 7556 |
| Hitpoints 📖 | 800 | 600 | 600 |
| Defence 📖 | 150 | 175 | 175 |
| Magic 📖 | 250 | 175 | 87 |

**Damage gating** 📖, corroborated by the cache's own defence params ✅:

| | 66 % mitigation from | cache melee def ✅ | cache magic def ✅ |
| --- | --- | --- | --- |
| Head | all **non-ranged** | 200 | 200 |
| Left claw | all **non-melee** | **50** | 200 |
| Right claw | all **non-magic** | 200 | **50** |

Also: **50 % earth-spell weakness** (June 2025), **100 % poison/venom
resistance** 📖.

**Challenge Mode:** head and hands keep **normal HP** but take the elevated
combat stats 📖 — the one place CM does not follow ×1.5.

### 2.10 The methods, as acceptance tests

Methods are named by *player attacks : Olm attacks*. A correct **4:1** (4-tick
weapon) or **3:1** (5-tick weapon) "should avoid all attacks outside of the
flame walls and acid pools for the melee hand" 📖.

Documented 4:1 opener 📖: ensure Olm's next attack is **not** a special and he is
**facing left**; force the head to the middle, then run back and attack the left
hand to make it turn left. That yields **~14 ticks** before Olm attacks — room
for three more hits with a 4-tick weapon.

Synq's four setup methods 🎥, which are the acceptance corpus for §2.2–2.3:

| Method | Transcript |
| --- | --- |
| 1: Basic 1 → Empty | [2:51:04](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10264) |
| 2: Basic 2 → Special | [2:51:54](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10314) |
| 3: Basic 1 → Basic 2 | [2:52:55](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10375) |
| 4: Basic 2 + Special → Basic 1 | [2:54:28](https://www.youtube.com/watch?v=klhBxOH8reQ&t=10468) |

Note method 4's name is itself evidence for the catch-up rule: a step carrying
*both* a basic and a special is exactly §2.3 rule 3.

### 2.11 What `Perfect Olm (Solo)` enumerates 📖

The CA gives an authoritative list of Olm's *avoidable* damage sources — useful
as a checklist that nothing is missing: teleport portals, fire walls, healing
pools, crystal bombs, crystal burst, prayer orbs. Plus: claws must not
regenerate during phase 3, and no consecutive damage from the same acid pool.
Falling crystals in phase 4 do **not** disqualify.

---

## 3. Tekton — tick-by-tick

**Cache** ✅ 7540 waiting → 7541 walking_standard → 7542 fighting_standard →
7545 hammering → 7543 walking_enraged → 7544 fighting_enraged.
Anvil is a **loc**, `raids_tekton_anvil` (29867), 6×4, `blockwalk=1` ✅.

### 3.1 Attack timing — sources conflict

| Source | Value |
| --- | --- |
| 📖 Wiki infobox | attack speed **3 ticks** |
| 📖 Talk page | "Tekton attacks on a three-tick cycle" |
| 🔧 Plugin, normal auto anims | `setTicksUntilAttack(4)` → **4 ticks** |
| 🔧 Plugin, "fast auto" anims | `setTicksUntilAttack(3)` → **3 ticks** |

⚠️ **Unresolved and important.** The plugin distinguishes two auto families
(`TEKTON_AUTO1..3` / `TEKTON_ENRAGE_AUTO1..3` at 4 ticks, and
`TEKTON_FAST_AUTO1..2` at 3 ticks) where the wiki reports a single speed. Most
likely reading: Tekton's *base* speed is 4 with a faster variant, and the wiki
records the faster one. **Resolve before encoding** — this alone changes the
room's difficulty by a third.

### 3.2 The anvil cycle 🔧

From the plugin's animation handler:

| Event | Value |
| --- | --- |
| On spawn | `tektonAttackTicks = 27` — ticks from spawn to first engagement |
| On `TEKTON_ANVIL` animation | `tektonAttackTicks = 47`, and `setTicksUntilAttack(15)` |
| During anvil | `tektonActive = false` (the countdown only runs while active) |

So: **~27 ticks** spawn → engage, and after starting the anvil animation
**15 ticks** until he can attack again with a **47-tick** window tracked for the
walk-back-and-re-engage.

Wiki/CM detail 📖: he sends **up to two sparks per player**, and **"after five
sets of sparks, he will resume combat"** — so the anvil phase is **5 spark
volleys**, not a wall-clock duration. That is a better model than a tick count
and should be what the implementation uses.

Repair rate 📖: ~**1.11–1.33 % of health every few ticks** ❓ exact interval.

### 3.3 Other rules 📖

- Melee only; **immune to ranged**; magic defence +0 with **20 % water
  weakness**.
- Hits everyone **in front of and to his right**.
- **The first elder maul or dragon warhammer special always lands.** A rule, not
  a roll. BGS drains 10 defence on a miss; DWH drains 5 % on a miss.
- Immune to binding.
- Defence bonuses: stab +155/+280, slash +165/+290, **crush +105/+180** — crush
  is intended.

---

## 4. Guardians

**Cache** ✅ 7569/7570 left/right, 7571/7572 dead forms. Always exactly two 📖.
Pickaxe spawn is a loc, `raids_stoneguardians_pickaxe` (41754) ✅.

| Quantity | Value |
| --- | --- |
| Attack speed | ✅ **4 ticks** — wiki infobox and weirdgloop `monsters.json` agree. 🔧 plugin's `setTicksUntilAttack(5)` is an animation-timing observation, not the combat-defs attack speed field; not adopted (resolved 2026-08-19). |
| Damage gate 📖 | pickaxe only; everything else **reduced to 0** |
| Damage formula 📖 | `D = (50 + Mining level + pickaxe level req) / 150`; crystal pickaxe caps at dragon |
| Stomp 📖 | 3×3 centred on the attacked player's tile at swing time; population re-checked one tick later, which is the dodge window ("attacking then stepping 2 tiles away") |
| Melee vs. stomp 🔧 | RaidGuardianNPC.attack: forced stomp when no player is within melee range, otherwise a 50/50 coin flip. No rung1-3 numbers; NR's shape adopted uncontradicted. |
| Flinch 🔧 | RaidGuardianNPC.flinch: a landed hit halves the current attack countdown to `floor(attackrate/2)`, gated by an attackrate-tick cooldown so it cannot re-trigger inside one window. No rung1-3 numbers; NR's shape adopted uncontradicted. |
| Pushback 📖🔧 | stepping within one tile of the gap between the statues: wiki confirms damage + points are awarded but states the point-cap formula is unpublished; NR supplies the only concrete numbers (15-30 damage, flat 40 points, `ForceMovement` slide to the paired push tile). |
| Stat regen 📖 | 1 per **8** ticks |
| Immunities 📖 | thralls, poison, venom |
| HP formula 📖 | `H = 151 × (1 + ⌊T × 1/2⌋) + ⌊M̄⌋ × T` — resolved 2026-08-19: the raw wikitext is LaTeX `\lfloor T \times \frac{1}{2} \rfloor`, i.e. T times **one-half**, not T times twelve; the `× 12` in the row above was a markdown-rendering artifact of an earlier transcription. At T=1, M=99 this evaluates to 151+99=250, matching the infobox's own `hitpoints1=250` exactly. |

---

## 5. Vasa Nistirio

**Cache** ✅ 7565 dormant → 7566 walking → 7567 healing; 7568 crystal npc; locs
29774/29775 crystal on/off.

| Quantity | Value |
| --- | --- |
| Attack speed 📖 | 3 ticks |
| Stat regen 📖 | **1 per 10 ticks** |
| Glowing crystal regen 📖 | **1 per 9 ticks** |
| Crystal timer 📖 | **40 s ≈ 66–67 ticks**, started when Vasa **reaches** the crystal the first time |
| Crystal healing 📖 | **1 % every 2 ticks**, plus (10 % + 1) defence per cycle |
| Crystal defence 📖 | ranged-**immune**; 66 % less from magic; resists crush and slash → **stab** |
| Boulder range 📖 | any player within **8 tiles** |
| Stomp 📖 | up to **8 damage per tick** underneath |
| Teleport special 📖 | half the team to the edges, half adjacent; the adjacent group is **stunned with prayers disabled** and takes `(current HP − 5)` split across absorbers |

⚠️ The timer starting on **arrival**, not activation, is the detail that decides
whether the room is trivial or impossible.

---

## 6. Vespula

**Cache** ✅ 7530 flying, 7531 enraged, 7532 walking, 7533 portal, 7534–7537 the
four grub health states, 7538/7539 vespine soldier. Blossom source is a loc,
`raids_vespula_herb`/`_empty` (30068/30069) ✅.

| Quantity | Value |
| --- | --- |
| Vespula HP / size 📖 | 200, **5×5** |
| Attack speed 📖 | 3 ticks |
| Max hits 📖 | 14 ranged, 8 stomp, 20 sting |
| Stat regen 📖 | 1 per 10 ticks |
| **Grub regen** 💬 | **10000 ticks — i.e. never**, attributed to **Mod Ash**. Grubs do not heal on their own; only blossoms restore them |
| Portal HP 📖 | 250 |
| Portal regen 📖 | 1 HP per **45** ticks |
| Portal prayer drain 📖 | **3 prayer points every 2 ticks** in range; **3 damage** instead if prayer is empty |
| Grounding threshold 📖 | **20 %**, lands for **~20 seconds**. ⚠️ another page says "below 23 %" — **conflict** |
| Enrage 📖 | attacking the portal while airborne → flies back and forth, **5–20 typeless** in melee range, **2–8 per tick** stomp underneath |
| Weakness 📖 | 50 % fire; halberds reach her while flying (Aug 2025) |

The four grub *states* being separate cache NPCs ✅ means grub health is a
**transform chain**, not an HP bar — healthy → sickly → infected → dead, then
dead becomes a vespine soldier that heals both Vespula and the portal.

---

## 7. Vanguards

**Cache** ✅ 7525 dormant, 7526 walking, 7527 melee, 7528 ranged, 7529 magic.
Plugin confirms the style mapping ✅🔧: `VANGUARD_7527` = MELEE, `_7528` = RANGE,
`_7529` = MAGE.

| Quantity | Value |
| --- | --- |
| HP each 📖 | 180 (CM 280) |
| Defence 📖 | 160 (CM 240) |
| Attack speed 📖 | 4 ticks |
| Max hit 📖 | 22 |
| Attacks per action 📖 | **three** — ranged/magic put one on the main target and two elsewhere |
| **Force-heal threshold** 📖 | HP divergence of **40 %** (teams ≤4) or **33.3 %** (teams ≥5) → **all three heal to full** |
| Shuffle 📖 | each shuffles independently every **20–36 ticks** |
| Style triangle 📖 | ranged weak to melee; melee weak to magic; magic weak to ranged |

Visual tell 📖: a swirling energy effect brightens as the party approaches the
heal threshold — a client-visible warning driven by the same computation, so it
must be fed by the same single writer.

---

## 8. Muttadiles

**Cache** ✅ 7561 submerged, 7562 junior (small), 7563 large, 7564 meat tree npc.
Tree also exists as locs `raids_meat_tree_full`/`_empty` (30012/30013) ✅.

| Quantity | Value |
| --- | --- |
| HP 📖 | 250 each (large CM 375) |
| Attack speed 📖 | 4 ticks |
| Small max hits 📖 | melee 28, ranged 30 |
| Large max hits 📖 | melee 41, ranged 44, magic 23; **stomp up to 72** (CM 106) |
| **Style lock** 📖 | at range the large one holds one style for a **minimum of three auto-attacks** before it may switch |
| Submerged phase 📖 | while the small one lives, the large one attacks with erratic magic at **1/3 hit chance per player** |
| Meat tree 📖 | triggers ~**50 % HP**; **up to three times**, *each consisting of **two bites*** (CM page); abandons after **~15 s** without access |
| Tree HP 📖 | **100–500** scaling with Woodcutting 1–99 |
| Heal amount 📖 | "up to 50 %" on one page, "up to 40 %" on another ⚠️ **conflict** |
| Weakness 📖 | 40 % earth (June 2025) |

---

## 9. Lizardman shamans

**Cache** ✅ 7573/7574 a/b, 7575 blocker.

| Quantity | Value |
| --- | --- |
| HP 📖 | 190 (CM 285) |
| Defence 📖 | 210 |
| Attack speed 📖 | 4 ticks |
| Size 📖 | 3×3 |
| Poison 📖 | level 12 |
| Poison blob 📖 | ~25 % stronger than surface — up to **39–40** damage |
| Stat regen 📖 | 1 per **20** ticks |
| Ranged defence 📖 | **+0** — the reason ranged is used |
| **Count** 📖 | "at least two, dependent on party size"; **max 5** for large teams. ⚠️ /Strategies says 2–4, the monster page says 2–5 — **conflict** |
| Safespot 🎥 | possible at 10+ tiles |

❓ **The spawn-count formula by party size is not published** — only the bounds.

---

## 10. Skeletal mystics

**Cache** ✅ 7604/7605/7606 a/b/c. Room also has an undocumented three-part
portal: `raids_mystics_portal_start`/`_middle`/`_end` (49996–49998) ✅ ❓.

| Quantity | Value |
| --- | --- |
| HP 📖 | 160 (CM 240) |
| Defence 📖 | 187 |
| Attack speed 📖 | 4 ticks |
| Attack range 📖 | 10 |
| Attacks 📖 | standard magic, a **damaging Vulnerability**, and melee |
| Crush defence 📖 | **+75** vs +155 stab/slash — crush is intended |
| Prayer reaction 📖 | they read the target's overhead; Protect from Magic cuts accuracy and damage **~50 %** |
| Count 📖 | 3–12 by party size ❓ formula unpublished |

⚠️ 📖 says both "cannot be safespotted" and that corner positioning forces
melee-only. Presumably "no line of sight → walks into melee". **Verify.**

---

## 11. Ice demon

**Cache** ✅ 7584 noncombat, 7585 combat, 7586 icefiend. Braziers 29747/29748,
wood 29763/29764, plus `raids_icedemon_axe`/`_tinderbox`/`_snow` ✅.

| Quantity | Value |
| --- | --- |
| HP 📖 | 140 |
| Magic / Ranged 📖 | **390 / 390** |
| Attack speed 📖 | 3 ticks, 3×3 AoE |
| Damage reduction 📖 | **67 % on everything** except fire spells and demonbane |
| Fire spells 📖 | 250 %, later revised to **150 % weakness** ⚠️ two values, era-dependent |
| Demonbane 📖 | +15 %, revised to **115 %** as of July 2026 |
| **Icefiends** 📖 | one per brazier; try to extinguish **every 3–4 ticks** with a **1/6 chance** of succeeding |
| Firemaking 📖 | **8 %** success at level 1 → **78 %** at 99 |
| Kindling per swing 📖 | +1 per **12** Woodcutting levels, **max 8** at level 96+ |
| Tree depletion 📖 | **×5** the standard rate in this room |
| Points 📖 | kindling deposits, ~**6,050** max in normal mode |
| Drops 📖 | 1 fiendish ashes + 7 stinkhorn + 7 endarkened juice + 5 cicely **per player** |

⚠️ **Prayer inversion — verify.** The wiki says Protect from Missiles → it only
throws snow boulders; Protect from Magic → it only casts Ice Burst. That reads
backwards (praying a style makes it *use* that style). Confirm before encoding;
this is exactly the kind of thing a summary flips.

---

## 12. Jewelled (crystal) crabs

**Cache** ✅ crabs 7576 grey / 7577 red / 7578 green / 7579 blue; beams
7580 white / 7581 red / 7582 green / 7583 blue; big crystals 29753–29757,
small crystals `_black`/`_cyan`/`_magenta`/`_yellow`/`_white` 29758–29762,
hammer spawn 29711.

The wiki has almost nothing here; **Synq's transcript is the primary source** 🎥.

**Colour production** 🎥 — what a style does to a crab:

| Style | Crab colour |
| --- | --- |
| (default) | white |
| Magic | blue |
| Ranged | green |
| Melee **or smash** | red |

**Crystal ↔ beam pairing** 🎥 — note this is **not** an identity mapping:

| Crystal | Satisfied by beam |
| --- | --- |
| Black | white |
| Yellow | blue |
| Cyan | red |
| Magenta | green |

The cache's small-crystal loc names are exactly `black, cyan, magenta, yellow,
white` ✅ — independent corroboration of that list.

| Rule | Value |
| --- | --- |
| Beam travel 🎥 | bounces **clockwise** off crabs |
| Attack cadence 🎥 | **no tick delay** between attacking successive crabs |
| Magic splash 🎥 | won't splash while magic bonus > **−64** |
| Aggro 🎥 | standing **2 tiles** from a crab aggros it immediately; de-aggros if out of range too long |
| Smash 🎥 | wielded DWH/elder maul (or the room's hammer) stuns **and** colours red |
| **Stun duration** 📖 | solo **50–60** ticks; 2–3 players **30–40**; 4–5 **20–30**; 6+ **10–20** |
| Crab stat regen 📖 | **1 per tick** |
| Beam self-destruct 🎥 | recolour the *next* crab it will hit, or run into the beam. Damage scales **down** with your current HP but **can kill at 1 HP** |
| Layouts 🎥 | three: *good crabs*, *bad crabs* (10–25 s slower), *rare crabs* |
| Crabs needed 📖 | "all layouts require the use of **three** crabs"; more players → more crabs present |

---

## 13. Tightrope

**Cache** ✅ 7559 ranger, 7560 mage; locs 29749 barrier, 29750 end, 29751 keystone.

| | Deathly ranger 📖 | Deathly mage 📖 |
| --- | --- | --- |
| HP | 120 | 120 |
| Defence | 155 | 155 |
| Offensive stat | Ranged 210, rng str +70 | Magic 210, magic dmg +120 |
| Attack speed | 4 ticks | 4 ticks |
| **Max hit** | **70** | **22** |

- Both are **passive until a player steps onto the rope**, then all turn on 📖.
- Solo/small: **2 rangers + 2 mages** 📖.
- **Using the keystone on the barrier dispels it and kills every surviving
  ranger and mage** 📖 — the room is a traversal puzzle, not a kill room. The
  *No Time for Death* CA requires clearing it without killing any 📖.
- Damage after crossing is inflicted **in one tick** 📖.
- Ironmen may pick up a dropped keystone; **points go to whoever collected it
  first** 📖.

---

## 14. Thieving (creature keeper)

**Cache** ✅ 7602/7603 beast active/sleeping; locs 29742/29743 chest
closed/open, 29744/29745 eggs whole/hatched, 29746 trough empty, 29874 trough
full, 29875 creature keeper.

| Quantity | Value |
| --- | --- |
| Points per grub 📖 | **115** |
| Room point cap 📖 | `max(4500, ⌊total thieving level / 6⌋ × 150)` |
| Grub stack cap 📖 | **28** |
| Minimum per successful open 📖 | **1** grub guaranteed |
| Hunger regen delay 📖 | starts **60 s** after the last feed (raised from 30 s, Jan 2019) |
| Grubs required | ❓ "depends on party size" — **formula unpublished** |

### 14.1 Exact chest positions — solved 🔧

[`sources/de0/ChestData.java`](sources/de0/ChestData.java) carries the **complete
chest tile layout** for all three room variants as room-local `(x, y)` pairs:

| Variant | Chest count |
| --- | --- |
| CCW | 64 |
| THRU | 66 |
| CW | 74 |

It also carries **solution sets** — arrays of chest indices grouped per entrance
rotation (`// Entrance angles: south=0, west=1, north=2, east=3`), ten solutions
per rotation. So the room is not "open chests at random": there is a known set of
chests that yields grubs for a given variant + entrance angle, and the plugin
highlights them.

This is the single most precise spawn data found for any CoX room, and it means
the thieving room can be built **exactly** rather than approximated. Port the
tables directly from that file rather than re-deriving them.

❓ The `_eggs_whole`/`_eggs_hatched` locs and the `_beast_sleeping`/`_active`
NPC pair imply a mechanic neither page documents.

---

## 15. Scavengers and supplies

**Scavenger beast** 📖 ✅ (7548/7549): 30 HP, max hit 13 crush, **4 ticks**,
aggressive. **Two drop rolls** per kill since Nov 2023 (was three) plus a
guaranteed bone. Cave worms 30–50, endarkened juice 5–14, stinkhorn 3–11,
cicely 3–6, mallignum root planks 2 — and **cicely, endarkened juice and
stinkhorn always drop together**.

**Farming** 📖: two plots per resource room, herbs grow in **30 seconds**;
golpar (27), buchu (39), noxifer (55). The cache carries the whole growth chain
as locs (`_seed`, `_growth1..3`, `_fullygrown`, ids 29997–30011) ✅ — four loc
swaps, not a timer with one model.

**Fishing / Hunter tiers** 📖 — seven each, gated 1/15/30/45/60/75/90, healing
5/8/11/14/17/20/23. Bats are `raids_bat_0..6` = 7587–7593 ✅, in order.

**Cooking points** 📖: `4 + (8 × bat tier)`, **+50 %** if another player eats it.

**Overload** 📖: boost `⌊level × 13 / 100⌋ + 5` (+17 at 99); **−50 HP on drink**,
re-applying **every 15 s for 5 minutes**, then **heals 50 back**.
⚠️ The main CoX page instead says "4 + 10 %" — **conflict, unresolved.**

---

## 16. Open questions — what research did *not* answer

These are the gaps that remain after the sweep. None should be filled with a
guess.

1. ❓ **Damage → points coefficient.** Not published anywhere. Currently
   `^cox_points_per_damage = 5`, which is **ours**, flagged as such in the
   constant file.
2. ❓ **Point cap by party size** — the wiki explicitly says the formula is not
   known to the community.
3. ❓ **Spawn-count formulas** for shamans (2–5) and mystics (3–12); only bounds
   are published.
4. ❓ **Spawn *locations*** for every combat room. The template cells contain no
   NPC spawn data (§0.3 of the plan) — Jagex's spawn points lived in raid code.
   These will have to be authored from video observation or chosen by us.
5. ❓ Exact tick intervals for: Olm auto-heal delay, deep burn ticks, Tekton's
   repair interval.
6. ✅ Guardian HP formula — resolved 2026-08-19: the wiki's raw wikitext is
   `T × 1/2`, not `T × 12`; see S4.
7. ⚠️ **Eight remaining known conflicts** between sources, each flagged inline
   above (Guardian attack speed resolved to 4 ticks, 2026-08-19):
   Tekton attack speed (3 vs 4), Vespula grounding
   (20 % vs 23 %), Muttadile heal (40 % vs 50 %), shaman count (2–4 vs 2–5),
   falling-crystal damage, ice demon prayer-reaction direction, ice demon fire
   multiplier (250 % vs 150 %), overload formula.

### Highest-value unexplored sources

- **Woox's 2017 solo guide** — no captions; needs a human viewing.
- **de0's plugin hub repo** — advertises room times, Olm phase tracking,
  Muttadile tree and Ice demon thaw timers. Those timers are exactly the ❓
  intervals above; reading that source is the single best next step.
- **`RaidsPlugin.java`** (official RuneLite) — room/rotation detection, useful
  for the layout generator in Phase 4.
- Jagex's **Scouting & Scaling** dev blog — may contain the official scaling
  statement the talk page is fitting against.
