# The high-level Theatre of Blood community — who they are and what they publish

Companion to [`THEATRE_OF_BLOOD_PLAN.md`](THEATRE_OF_BLOOD_PLAN.md) and
[`ENCOUNTER_TIMING.md`](ENCOUNTER_TIMING.md).

Almost nothing in this raid is documented by Jagex. The tick-level truth lives with
a few hundred people who run it competitively, and it reaches the outside world in
four forms: **plugin code**, **practice simulators**, **Discord role guides**, and
**video guides**. This document names the sources, says what each one is worth, and
records what this project took from each.

The ranking rule used throughout: **code a competitive player relies on > a
simulator built from observed data > a written guide > a video > a forum post.**
A recorder that mis-times a tick produces a broken parse and gets fixed; a video
that mis-states a tick gets 40k views.

---

## 1. Organisations

### We Do Raids (WDR) — <https://wedoraids.com> · <https://discord.gg/wdr>

The largest raiding community in the game (155,000+ members) and the closest thing
ToB has to a standards body. Structured in tiers — `learner` / `standard` /
`advanced` / `maxeff` — with mentors, and its written guides live in Discord
channels (`#room-resources`, the TOB channels) rather than on the public site.
The wiki's own advanced guide says outright that much of its content is
**copy-pasted from WDR**.

**Used here:** indirectly, as the upstream of the wiki's
[Guide:Advanced Theatre of Blood](https://oldschool.runescape.wiki/w/Guide:Advanced_Theatre_of_Blood?oldid=15222073),
and directly as the source of the maze corpus behind the Sotetseg generator (§3.1).

### GM30 · Max Eff Moneys · Flash's Hideout

The three other communities the wiki's advanced guide names as its sources. These
are max-efficiency groups: their material is the origin of the vocabulary this
plan uses — *5.3t vs 5t*, *BOAK*, *pogtanking*, *pogwebs*, *crossfreezing*,
*ice rush*, *off on 3*, *stacking*, *30s skip*, *BSCP*. Their guides are
Discord-internal.

**Used here:** as the provenance of the technique names in §6–§11 of the plan.
Where a technique is named, the plan explains the mechanic it depends on, because
**a technique that stops working is the sharpest possible regression test**.

### Blert — <https://blert.io> · <https://github.com/blert-io>

A PvM analytics platform: a RuneLite recorder plugin, a Rust analysis pipeline and
a website with per-raid tick-indexed event streams and leaderboards. Open source.
Run by Alexei Frolov (`capslock13`) and contributors.

This is the **single most valuable source in this project**. Its recorder cannot
work unless its tick constants match the live game, so every number in it is
load-bearing.

**Used here:** the Verzik attack clock, the Bloat down cycle, the Nylocas 4-tick
cycle and stall table, the Xarpus turn cadence, the Sotetseg attack speed, all six
room world-areas and region ids, the Maiden crab tiles, the Verzik crab tiles, the
nylo split geometry, and the HP-by-scale table. See
[`sources/blert_plugin/`](sources/blert_plugin/).

### RuneNation — <https://runenation.org>

A long-running PvM clan with public ToB pages. Mostly learner-facing; no
tick-level material this project needed.

---

## 2. Record holders

From the decommissioned official records page, preserved at
[Theatre of Blood/Records](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Records):

| Record | Time | Team |
|---|---|---|
| World-first completion | 37:24 | B0aty, Woox, Cloud Badass, Zulu, H ard |
| Fastest completion | 16:57 | Golpar seed, Oblv Crabs, Dream Realm, Kitten, Oblv Catreus |
| Fastest Maiden | 1:32 | Oblv De0, BoringName, Dumpster, Bogi153, Hoodler |
| Fastest Bloat | 1:14 | lost a tick, BoringName, Aurosallia, Hoodler, Dumpster |
| Fastest Nylocas | 3:51 | Oblv Rusty, Catreus, Dream Realm, Legit Cursed, Golpar seed |
| Fastest Sotetseg | 1:41 | Arkeela, Miqup, TRACTORBOB, TRACTOR KING, TURBOTRACTOR |
| Fastest Xarpus | 2:58 | Oblv De0, BoringName, Aurosallia, Dumpster, Oblv Spices |
| Fastest Verzik | 4:20 | Oblv Catreus, Oblv Gitz, Oblv Tamam, Oblv Classix, Oblv Karl |

The `Oblv` prefix is the **Oblivion** clan, which holds or co-holds most of these.
Current solo Hard Mode leaderboards on Blert are dominated by **grraffes**, with
**Caywu** also in the top three.

**Why this table is in a technical document:** these times are the hard lower bound
on the implementation. A raid that cannot be completed in 16:57 by a perfect team
has a clock somewhere that is too slow; one that can be completed materially faster
has a clock that is too fast. The Nylocas record of 3:51 is especially useful — the
wave phase has a *computable* floor of 236 ticks (2:22) from the stall table, so a
3:51 room clear is consistent and a sub-2:22 one would be proof of a bug.

---

## 3. Practice simulators — the highest-value community artefacts

These are built by people who watched hundreds of recordings and reverse-engineered
the rules. They are the only community sources that are *executable*, and therefore
the only ones that can be diffed against an implementation.

### 3.1 Sotetseg maze trainer — devqhp

<https://devqhp.github.io/osrs/sotetseg/> · source saved as
[`sources/community_tools/devqhp_sotetseg.js`](sources/community_tools/devqhp_sotetseg.js)

Its header credits the corpus it was built from:

> *"Additional thanks to **Deflne_Alive, LucidDream, SNIPERBDS, xZact**, and the
> **WeDoRaids** discord for sharing mazes from which I could establish rules for
> maze generation."*

That is a direct naming of high-level players who contributed observed data, and it
tells you the generator below is **empirical, not decompiled** — treat it as a very
strong hypothesis, not as Jagex's code.

**The generator, recovered in full:**

```js
const maze_width   = 14;
const maze_height  = 15;
const path_turns   = 8;     // the seed is 8 numbers  -> matches the Wiki's
                            // example seed "7 10 12 11 12 7 11 12"
const max_x_change = 5;     // a seed step moves at most 5 columns
const tornado_row  = 4;

function makeSeed() {
    seed[0] = randRange(1, maze_width - 1);   // never starts on the far-west column
    for (i = 1; i < 8; i++)
        seed[i] = randRange(max(seed[i-1] - 5, 0), min(seed[i-1] + 5, 13));
}

// Build from the bottom row (y = 14) upward:
//   y EVEN  -> a single tile at the current x
//   y ODD   -> a horizontal run filling every column between x and the next seed,
//              then x becomes that seed
// start = (seed[0], 14)   end = (seed[7], 0)
```

Rows 14, 12, 10, 8, 6, 4, 2, 0 are single tiles; rows 13, 11, 9, 7, 5, 3, 1 are
horizontal runs. Eight seeds, seven runs. This resolves plan task **M11** down to
"verify the empirical model", which is a much smaller job than "derive it".

It also states the player-movement model the maze depends on — *"Movement mechanics
work just as they do in OSRS and are processed every tick (600 ms)"* — and confirms
`tornado_row = 4`.

### 3.2 Xarpus melee trainer — SpaceScape

<https://spacescape20xx.itch.io/xarpus-melee-trainer>

The instructions name the scan tick outright, in the community's own words:

> *"**On tick 1 the boss registers your position.** Move away on tick 2; you will be
> away from the boss by tick 3. Attack on tick 4, repeat."*

and

> *"Wait for a tick if poison is on any 'wrong' tiles (it means you're not in
> cycle)."*

"The boss **registers your position**" is exactly the scan stage of
[`ENCOUNTER_TIMING.md` §1](ENCOUNTER_TIMING.md#1-the-attack-pipeline), arrived at
independently by a player building a trainer rather than by a plugin author reading
event streams. Two completely different methodologies, one model.

The trainer also notes that the **`Players` setting changes Xarpus' attack
frequency**, and that *"setting Players to 1 is more accurate for 5-ticking"* —
a hint that the spit cadence may not be a flat 4 ticks at every scale. That is now
plan task **M31**.

---

## 4. Plugin authors

Each of these is a named individual maintaining code that competitive teams run
every raid. Their constants are the de-facto specification.

| Plugin | Author / org | What this project took |
|---|---|---|
| [blert-io/plugin](https://github.com/blert-io/plugin) | Alexei Frolov (`capslock13`) et al. | the entire tick backbone (§1 above) |
| [damencs/tob-qol](https://github.com/damencs/tob-qol) | damencs | Nylocas lane spawn block (12 scene tiles), Sotetseg maze scene offsets, Bloat scenery ids, room-status varbits, loot-room chest ids |
| [QuestingPet/TobMistakeTracker](https://github.com/QuestingPet/TobMistakeTracker) | QuestingPet | the `previousWorldLocation` rule, Maiden blood-splat lifetime (11 ticks), Bloat hand one-tick window, Verzik P2 bomb one-tick window, the exact melee-chance predicate |
| [NCG-RS/TobUtilities](https://github.com/NCG-RS/TobUtilities) | NCG-RS | Bloat floor ids, Maiden "scuff" model |
| [EIKOOT/nyloer](https://github.com/EIKOOT/nyloer) | EIKOOT | nylo role/menu model |
| [JourneyDeprecated/OpenOSRS](https://github.com/JourneyDeprecated/OpenOSRS) `theatre` | OpenOSRS contributors | independent confirmation of the wave table and stall sequence; Xarpus exhumed counts by scale; the orientation-change resync that pins the Xarpus turn to the attack clock |

---

## 5. Video guide authors

Nineteen transcripts are in [`sources/transcripts/`](sources/transcripts/). The ones
that contributed a *number* rather than context:

| Author | Video | Contribution |
|---|---|---|
| **Crusher** | [5 Tick Xarpus Guide — Master the Scythe Walk](https://www.youtube.com/watch?v=fPpIRjQWtlE) | the four-beat scythe cycle; the turn used as a cycle anchor; the spit chirp landing on the step-back tick |
| **Plank2g** | [ToB Made Easy: Melee Xarpus](https://www.youtube.com/watch?v=Lt-iZwJUKmc) · [Verzik P2](https://www.youtube.com/watch?v=eswoo8D364c) | *"click attack right before he turns"*; *"stay close for every tick except the dangerous tick"* |
| **Granddad Jad** | [Phase 3 Verzik Tanking](https://www.youtube.com/watch?v=3lQjrLeuvHo) | *"step away from Verzik one tick before she launches her attack"* — the P3 scan tick in one sentence |
| **07samsquanches** | [P3 Verzik Guide](https://www.youtube.com/watch?v=oGPT3sZMnd8) | the web rotation: wait one tick after the XP drop, then run; recovery rules for being ±1 tick |
| **S2L OSRS** | [Max-Efficiency ToB 4s](https://www.youtube.com/watch?v=yNZZQNAdQAM) · [Complete Verzik P3](https://www.youtube.com/watch?v=D1b4eWwnOHU) | the 7-tick metronome tanking method |
| **BillNylo** | [Bloat Flinching and Tick Fixing](https://www.youtube.com/watch?v=oKXoj9Yxy7Q) | the rise window and the five-tile flinch distance |
| **cBold** | [Sotetseg Maze — Diagonals and L-Shapes](https://www.youtube.com/watch?v=JdtL9UI5uy0) | cardinal-first-then-diagonal player pathing; optimal paths of 12–13 steps |
| **Deflne Alive** | [Optimal Nylocas Strategy: Sub 4](https://www.youtube.com/watch?v=_QXdNAZh7Yo) | per-role wave routing; also credited in the Sotetseg maze corpus as `Deflne_Alive` |
| **Indarkment** | [Grandmaster Explains Maiden](https://www.youtube.com/watch?v=VEqiIF9EbcM) · [Maiden Freezing 101](https://www.youtube.com/watch?v=1ldGvUsOx2M) | gear-flick drain timing; the stacking/skipping decision points |
| **xzact** | One Minute Guides (Maiden, Nylocas, Sotetseg, Verzik P2) | credited as `xZact` in the Sotetseg maze corpus |

Others downloaded for coverage: Patyfatycake, Chriskies, Beleti, Kuji, Lone Gym Rat,
Okirra, 2tick rick, RS Mina, Abys, YefTalks, Rob, Horselord, Evse, AsukaYen,
Molgoatkirby.

---

## 6. Jagex sources

Not community, but the only first-party statements that exist. All are Mod Ash or
Mod Kieren replies on Twitter/X, preserved through the wiki's citation archive:

| Topic | Dev | Statement |
|---|---|---|
| Verzik P1 damage cap | Mod Ash | *"It's 10 for melee, 3 for magic and ranged."* |
| Verzik P2 lightning vs Verzik | Mod Ash | *"15-20. No accuracy calculation."* |
| Verzik P2.5 heal spell | Mod Ash | *"75 % chance of using a healing spell… heals her for half of the total."* |
| Xarpus poison base damage | Mod Kieren | *"The base damage for poison is 4-8 & is buffed by a percentage based upon how many exhumed absorbs you missed."* |
| Xarpus P3 retaliation | Mod Ash | *"The base damage is a random number 50-75. A percentage buff is then calculated off 40 % of the percentage uplift applied to his stats by absorptions."* |
| Verzik magic defence | Mod Ash | *"Defence like the Ice Demon, in all phases."* |
| Bloat line of sight | Mod Ash | *"that boss has custom line-of-sight checks… a manual check to see if any tile on the NPC's nearest side has line-of-sight to the target."* |
| Tick order | Mod Ash | client input → NPC turns → player turns |

---

## 7. What this changes about how to build it

1. **Two of the plan's `[MEASURE]` tasks are now answered by community artefacts**
   rather than needing live measurement: the Sotetseg maze generator (M11) and the
   Maiden blood-splat lifetime (part of M5).
2. **Three simulators exist that an implementation can be diffed against** — the
   Sotetseg maze trainer, the Xarpus melee trainer, and Blert's replay of any real
   raid. That is a far better acceptance test than a stopwatch, and §19 of the plan
   should grow a row for each.
3. **The record times are a bound, not trivia.** Put them in the test matrix.
4. **Where a technique has a name, it is a test.** If a correct implementation does
   not permit *off on 3*, *scythe walking*, *pogtanking*, *flinching*, *stacking*
   and *5-ticking*, a tick is wrong somewhere, and no amount of damage-formula
   testing will find it.

---

*Compiled 17 August 2026.*
