# Tormented Demon solver: the plan

Written 2026-10-19 before any run, per `docs/CONTENT_SOLVER_MANUAL.md` (phases 1-5). The mechanics are
[README.md](README.md) (RSProx 3312, 1756, 552, 1696, 3308); the strategy is the wiki's
(`sources/wiki_Tormented_Demon_Strategies.wikitext`, "Max Melee/Range").

- Test: `test/bosses/tormented_demon.lua`.
- Solver: `script/plugins/quest_driver/td_solver.lua`, a `QD.T2` room named `tormented_demon`, so
  `t.raid.tormented_demon_solve(opts)`.

## 1. What the recordings settled before the plan

Two facts decide the strategy. Neither was in our content or in the wiki's text precisely enough.

**The player's protect prayer is read at the launch, not at the landing.** (`rsprox_td.py`, "prayer read".)
In 1756 the recorder's prayer was:

| Prayer state | Swings | Outcome |
|---|---|---|
| Turned on during the attack tick T (off at T-1) | 17 | 17 blocked |
| Turned on after T, on by T+3 | 27 | 21 hit; the 6 zeros are misses |

In 1696 a prayer turned off after T still blocked. So a human cannot pray against the animation: the
prayer must already be right on the tick the demon swings.

**The style is held for a whole segment.** (`rsprox_td.py`, "segment style".)

- **The roll.** On the explosion (the engage, and each fire bomb) the demon rolls a style and keeps it until
  the next explosion. In 538 single-demon lives only 2 swung two styles.
- **The odds.** The first style after the engage was melee 272, fireball 142, rib shards 128. That is ½, ¼, ¼
  at every distance from 1 to 9.
- **The approach.** A melee demon walks to the player from **E + 1** (239 of 263; the other 24 were already in
  reach). A fireball or rib demon never walks (264 of 267). It throws even at an edge-adjacent player:
  1756 life 4378 at 1245 is eight rib swings at gap 1.

Our content chose per swing instead: melee when in reach, otherwise a fresh coin. It is fixed to the
recorded rule (section 6).

## 2. The threat field

F.tick = d is the tick whose end I see; a press decided at d is in force from d + 1 (R5). The demon
decides on its own tick and reads my tile at the end of the tick before.

| Threat | Decided / read | Damage | Prayer | Tell, first seen |
|---|---|---|---|---|
| Melee swing | at the swing tick T, my tile in reach | 0..31, lands on T | Protect from Melee **at T** | the demon steps toward me on X + 1 (seen at X + 1; first swing X + 6) |
| Fireball | T; 118-cycle projectile | 0..31, lands T + 4 | Protect from Magic **at T** | none before the first swing of a segment; then sticky |
| Rib shards | T | 0..31, lands T + 4 | Protect from Missiles **at T** | as fireball |
| Fire bomb | B = 60 ticks after the last explosion; tiles fixed at B | 40..45, **no prayer**, lands B + 4 on my tile at the end of B + 3 | none | "The demon's spell binds you!", entangle on me, two `fireball_large` projectiles to their tiles (seen at B) |
| The bind | B .. B + 2 | my feet held; a walk before B + 3 is undone by the walktrigger | — | as the bomb |

The prayer for a segment's first fireball or rib swing is a guess. The wiki gives the same advice: "you
will have to guess and/or correct your protection prayer". The guess is Protect from Magic:

- The melee armour worn is weakest to magic.
- After a bomb, a demon that does not walk uses "whichever wasn't used before" (wiki), so the guess is
  the ranged-type style it did not just use.

## 3. The strategy, as modes

| Mode | When | Do |
|---|---|---|
| `approach` | before the engage | Walk to a tile with gap 3-6 of the demon's 3×3, in the open. |
| `engage` | the demon unengaged, me in place | Bow on. Attack once from range. |
| `fight` | after the engage | See below. |
| `bomb` | bind seen at B | Stand (stunned until B + 2). The plan forbids both bomb tiles at the ends of B + 3 and B + 4. Step on B + 3 to a non-bomb tile with gap ≥ 2, so a melee demon must walk and tell. |
| `done` | death sequence seen | Prayers off. |

**Why engage with the bow from range.** Two reasons:

- The engage switch prays against the engaging style (E + 1), so the next 150 damage is melee's.
- At gap ≥ 2 a melee segment shows itself by walking on E + 1.

**`fight`:**

- **Weapon.** Use the style the demon is NOT praying (its headicon, sprite group 440: 0 melee, 1 ranged,
  2 magic).
  - Melee: the emberlight, with the avernic defender.
  - Ranged: the scorching bow (two-handed), with dragon arrows.
  - It prays the last style that hit it after every 150 damage, so the weapon swaps about every 150.
- **Prayer.** Pray the segment's style:
  - melee: from the approach seen on X + 1, or from any melee swing seen;
  - otherwise the first swing's (`firey_balls` = magic, `spare_ribs` = ranged);
  - otherwise the guess above.

  A swing seen in another style (the demon could not reach in melee and switched, as the wiki says)
  replaces it.
- **Position.**
  - Melee weapon: an edge tile of its footprint.
  - Bow: anywhere within 10.
  - In a melee segment I am adjacent anyway.

Supplies are the shared resource layer (`QD.T2.resources`):

- Food under 60 hp.
- A prayer dose under 30 prayer.
- A super combat when Strength falls to base.

## 4. The plan's terms

Defined in `predict` and `strategy`:

- **The demon as a target.** Range 1 (emberlight) or 10 (bow); speed 4 (both: rapid bow). `value` is the
  expected swing (`SWING_VALUE`).
- **Each bomb tile** is `B.hit{forbid}` with t0 = B + 3, t1 = B + 4, `land = 1`.
- **`S.x2.stunned_until = B + 2`** from the bind seen at B (M5: no move, no swing).
- **Each melee swing due** is `B.hit` on the 3×3's reach at its due tick, style melee: the prayer is
  priced, never a forbid.

## 5. PASS (GREEN), written before the first run

One run is one kill.

- **Required:**
  - the demon's death sequence is seen;
  - I am alive;
  - within 600 ticks of the engage.
- **Zeroes:**
  - 0 bomb hits (hp lost on a B + 4 with me on a bomb tile);
  - 0 melee swings landing on a wrong overhead;
  - 0 fireball or rib swings on a wrong overhead after the segment's first swing (`td.wrong_known`).
- **Allowed:** at most one guessed swing per fireball or rib segment.
- **Judging:** 48 seeds (`--name td0..td47`), after the probe and an 8-seed check.

## 6. Content changes this needs (RSProx-led)

- **`~td_roll_segment_style`.**
  - At the engage: melee ½, magic ¼, ranged ¼.
  - At each bound bomb: a style other than the current one, even odds. This is the wiki's "always
    change"; the 6 recorded lives with bombs agree.
  - Kept in `^td_var_style`.
- **The approach.** In a melee segment the demon's timer walks it beside the player (`npc_approach`) on every
  tick it is not edge-on, from the tick after the explosion.
- **The swing.**
  - It swings the segment's style.
  - A melee segment out of reach at its swing turns to magic or ranged at random and swings that (wiki).
- **The live lane's npc row** gains `headicon_group/index` and `body_model`. Scriptrun's had them.

## 7. Probes (the first run prints these)

- the tick my first move lands after a bind;
- the demon's first step after each explosion;
- the scorching bow's first hit (damage > 0: dragon arrows fire from it);
- the headicon row on both lanes;
- every swing's style, my overhead at T, and the hit.

## 8. Results (2026-10-19)

**Scriptrun: 48 of 48 GREEN** (`td0..td47`, `src/build_td_opt/torirsserver`).

- Engage to death: 189-271 ticks.
- 0 bomb hits in ~150 bombs.
- 0 melee or known-style swings on a wrong overhead.
- Guessed throws were wrong about half the time, as a coin should be.

**Live client: td0 GREEN** (`tools/quest_gate/run.py --script`): 184 ticks, every row.

- Its tick log is not compared with scriptrun's. The live account is `td0` and scriptrun's bot is
  `td0_p1`, so their random streams differ.

**The content against the recordings, over the 48 fights:**

| Rule | Ours | RSProx |
|---|---|---|
| First style after the engage | melee 29, ribs 12, fireballs 7 | ½ / ¼ / ¼ |
| Segments with two styles | 0 of 179 | 1 of 538 |
| A thrower walks | never (116) | 3 of 267 |
| A melee demon's first step | E + 1 | E + 1 |
| The style across a bomb | always changes | wiki: "always changes" |

After a bomb, a melee demon first steps at B + 4: the player is held adjacent until B + 3.

**What the probes found, and the fixes:**

- **Defenceless never forced a hit.** A miss reaches `~player_hit_npc_prepare` as a rolled 0, so
  `td_damage.rs2`'s forced success made a 0. The accuracy is now forced in `~player_npc_hit_roll`
  (combat_stats.rs2). Defenceless hits went from 2 of 8 landing to 18 of 21; the zeros left are
  0 damage rolls.
- **The emberlight's Lunge drew the unarmed punch.** The emberlight had no stance rows. It now has
  the arclight's (`attack_anims_modern.obj`, `attack_sounds.obj`); these are not yet checked against an
  RSProx recording.
- **The solver fought on Slash.** That style is against the demon's slash defence of 175. It now
  uses style slot 2: Lunge, stab, against stab defence 75; on the bow, Longrange. The first probe took
  397 ticks to kill, the later ones about 200.
