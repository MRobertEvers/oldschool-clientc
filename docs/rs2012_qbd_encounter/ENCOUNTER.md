# Queen Black Dragon — Encounter Design Guide

Prescriptive design contract for the 29-May-2012 Queen Black Dragon encounter as
implemented in ToriRSServer. Where `RS2012_QBD_TD.md` is the *evidence record* (what
the sources establish and how assets were ported), this document is the
*encounter script*: what the player must experience, attack by attack, tick by
tick, and how the implementation must realise it. Deviations between this guide
and the running scripts are bugs in the scripts.

Companion documents:

- `RS2012_QBD_TD.md` — provenance, evidence classes, asset ledger.
- `docs/rs2012_qbd_encounter/RESEARCH.md` — the 2012 wiki-revision, fansite,
  open727 and OSRS moving-wall research this guide's timings come from, with
  source labels for every number.
- `docs/rs2012_qbd_encounter/AUDIO.md` — what the encounter sounds like: the
  two music tracks, the per-attack frame-sound table, the arena's loc ambient
  bed, and the three audio layers that are still incomplete. Every attack
  below fires its sound off the animation it already plays, so an animation
  the server never binds is also an effect nobody hears.

Timing conventions used throughout:

- **tick** = one server game tick = 600 ms.
- **cycle** = one client animation cycle = 20 ms (`APP_LOGIC_TICK_MS`); 30
  cycles = 1 tick. Cache sequence durations and zone-packet delays/durations
  are cycle counts.
- Player/world queue semantics: `queue(script, N)` fires after **N+1** real
  ticks (the op stores `delay+1`; delay 0 = next tick). NPC queues fire after
  `max(N,1)` ticks. `ai_timer` interval N fires every N ticks. Every timeline
  in this guide is written in **real ticks after the triggering event (T0)**;
  scripts must convert to queue arguments through these rules rather than
  copying tick numbers verbatim. Several historical mistimings (half-speed
  wall front, half-speed shadows, 8-tick wave spacing) traced to ignoring the
  `+1`.
- One further drain subtlety: an entry queued from **inside a draining queue
  script** can land in a slot the current pass has not reached and lose a
  tick to that pass's decrement. Timelines with more than one future event
  (the wall's waves and steppers) are therefore scheduled **upfront from the
  attack context** (`ai_timer` arms after the tick's queue pass, so delay N
  is exactly N+1 ticks, deterministically); only uniform self-chains (a
  stepper re-arming itself from its own slot) may re-queue in-drain.

## 1. Overview

The Queen Black Dragon is a strictly solo boss. She is a colossal, mostly
stationary serpent at the north end of a dragonkin platform; the player fights
from the platform south of her. She is never killed: the player drains four
1,875-LP life-force pools (7,500 LP total, player hits capped at 100 LP) and,
after each drained pool, restores a dragonkin artefact while she is dormant
and grotworms erupt around the arena. The fourth artefact forces her back to
sleep and opens the stairs to the Dragonkin coffer.

The fight escalates across the four pools ("phases"). Phase 1 is a pure
positioning fight (melee zone, ranged sweep, dragonfire, one moving fire
wall). Each later phase layers on mechanics — tortured souls, armour forms +
siphon, then time stop + extreme dragonfire — while the fire wall gains extra
waves, so the end of the fight interleaves every mechanic at once.

Player-side damage numbers below are ToriRSServer hitpoints (2012 LP ÷ 10; the
2012 client showed a 99-HP OSRS player as 990 LP).

## 2. Arena

Region 5731, map square (22,99), presentation plane 1, private instance per
player. Arena-local coordinates (x,z) below are offsets within the square.

```
        z=38  ─ fire-wall spawn row (the wall detaches from her breath)
   QBD anchor (31,37); her body spans the full north end
        z=35  ─ left crystal (21,35) / right crystal (39,35): these swap
                appearance with her armour form (locs 70818-24 family)
        z=31  ─ artefact 1 (33,31), platform centre
        z=28  ─ platform edge: MAIN PLATFORM is z≥28 (always walkable)
        z=27..19 ─ raw magical platforms: phase-gated walk masks,
                    standing hazard, artefacts 2/3/4 at (24,21)/(42,21)/(33,21)
        player entry (33,28)
```

- The main platform is **19 squares wide, x 24..42** ("a platform of 19
  squares", 2012 wiki). Fire-wall gap squares are counted 1..19 from the west
  edge: square N is column x = 23 + N.
- Tiles with z < 28 are the raw magic platforms. They are walk-vetoed until
  their phase unlocks them (phase 2: west mask; phase 3: west+east; phase 4:
  west+east+centre) and standing on them deals 20 every 4th consecutive tick
  ("You are damaged for standing too long on the raw magical platforms").
- Artefact 1 sits on the main platform; artefacts 2–4 are at the south ends
  of the west, east, and centre raw platforms, so each intermission is also a
  navigation exercise across hazard tiles while worms chase.

### 2.1 The raw platforms are one loc, not three

The glass wedges are drawn by a **single** loc that is `loc_change`d from one
model to the next; every model in the family is the whole southern floor at a
stage of its growth, and the growth order is the order the walk masks unlock:

| model | ids (recol / plain) | picture |
|---|---|---|
| 110146 | 70843 / 70844 | south-WEST wedge alone |
| 110147 | 70845 / 70846 | + south-EAST wedge |
| 110148 | 70847 / 70848 | + south-CENTRE wedge |
| 110149 | 70849 | all three, every wedge green |

Measured off the art (`make -C src rs2012-model-view`, `--wire-out`, vertex
cloud binned into 128-unit tiles), each body is 20.5 × 7.2 tiles with the
wedges' wide ends on the +z side, tapering south to a 3-tile tip — the same
profile as the west/east/centre masks above, each tip ending on the artefact
that stands one row off it.

Placement is **anchor (24,22), angle `^loc_west` (0), `centrepiece_straight`**.
A `width=20`/`length=7` centrepiece is positioned at `tile*128 + 64*size`
(`world_scenery.u.c`), so that SW corner centres the model on column 33 and
lays the wedges over rows 22..27 with their north edge under the main
platform's rim. Two traps, both of which were live bugs:

- **The angle must be 0.** An odd angle swaps width and length and turns the
  model 270°, which puts the whole family across the main platform.
- **The anchor must not be row 21.** That is the artefacts' row, and the wire
  carries one loc per (tile, layer): a `loc_add` whose corner tile is an
  artefact's takes its slot, after which `loc_find` no longer sees the artefact
  and it can never be activated.

Colour is baked per wedge and is the state — newest wedge orange (the live
path), the ones behind it green. `recol1s=3023 -> recol1d=2127` is orange to
grey at the same lightness, and 3023 occurs in 110146 alone, so only the first
wedge has a dormant state: it is revealed grey when artefact 1 is restored and
lights when artefact 2 becomes unguarded. `::rs2012qbdplatform 1..7` steps the
whole family without fighting for it.

## 3. Encounter state machine

1. **Entry** (portal 70812, 60 Summoning): teleport to (33,28,p1), music 1119
   *Awoken*, HUD 1285 opens, sleeping queen (`rs2012_qbd_sleeping`) at (31,37).
   "The Queen Black Dragon is sleeping. Her breathing shakes the chamber."
2. **Sleep → wake**: wake animation (16714) begins at **tick 6**; she becomes
   attackable (`rs2012_qbd_default`) at **tick 33** as the wake motion
   settles; her **first attack lands around tick 40** (~24 s after entry —
   era guides tell the player to pot up during this window). The wake-anim
   duration bookkeeping is documented at `^rs2012_qbd_wake_anim_ticks`.
3. **Phase n combat** (n = 1..4): the rotation of §5/§6 on the pacing model
   of §7. Entering phase 4 additionally summons souls immediately (§5.5).
4. **Intermission n**: pool n reaches 1 LP → "The Queen Black Dragon's
   concentration wavers; the …th artefact is now unguarded." She becomes
   untargetable and inert; artefact n gains its `Activate` option; she coughs
   a grotworm **every 5 ticks** (§5.11) until the artefact is restored.
   Surviving souls/worms persist and keep fighting throughout.
5. **Restoration**: activating artefact n heals a fresh 1,875-LP pool,
   reveals the next raw-platform path (n=1 south-west, n=2 south-east, n=3
   south-centre — §2.1), plays the stop-cough (16748), and
   resumes combat in phase n+1 — her **first attack comes 20 ticks after the
   restoration**, giving the player the documented breather. The first
   restoration switches music to 1118 *Queen Black Dragon*.
6. **Completion**: artefact 4 → every surviving add removed, the arena floor
   opens (70838 at (21,24) and 70841 at (33,24) on plane 0, both `^loc_west`,
   each *replacing* the intact slab on its own slot rather than stacking on it)
   and the reward stairs (70790 at (31,29), `^loc_west`) appear, artefact 1's
   restored loc (70778) is deleted because the 5x-scaled staircase reaches over
   its tile, and the coffer is rolled once into the persistent ten-slot
   `rs2012_qbd_rewardinv`. open727 also spawns 70775 under the stairwell; it
   has no home here, see §2.1. Her death is **three ticks' work**, not one:

   | tick | what | why not sooner |
   |---|---|---|
   | 0 | stop-cough 16748, claws revert to the default form | — |
   | +`^rs2012_qbd_stopcough_anim_ticks` | death 16742, `[queue,rs2012_qbd_death]` | 16742 is priority 5 and everything before it is priority 6; the client gate is `wanted >= incumbent`, so it is refused until 16748 has ended |
   | +`^rs2012_qbd_sleep_anim_ticks` | retype to `rs2012_qbd_sleeping`, `[queue,rs2012_qbd_sleep]` | `npc_changetype` clears the transient animation and the wire writes SEQUENCE before TRANSFORMATION, so the two on one tick render as an instant snap |

   The stop-cough is load-bearing, not cosmetic. She coughs worms for the whole
   intermission and 16747 is `framestep=8` over eight frames — it loops, and it
   is still playing when the artefact is restored, so without 16748 displacing
   it her death is dropped and she goes on coughing until the retype. The
   restoration must also close the fight for good: `%rs2012_qbd_active` stays 1
   until the player takes the stairs, so the `[ai_queue3]` emergency pool
   restore is gated on `%rs2012_qbd_reward_ready` as well — otherwise it heals
   her a life point back, re-opens a fifth intermission, and the re-armed worm
   cough steals the death animation two ticks in.

   `::rs2012qbdfinish` drives this whole ending on demand; playing up to it
   kills an unattended QA account long before it is on screen.
7. **Departure at any point** (teleport/logout/death): the one-tick lifecycle
   watchdog tears down queues, timers, locks, HUD, music, and owned NPCs. An
   unclaimed coffer survives everything.

## 4. The cast

| Actor | Type(s) | Level | LP | Notes |
|---|---|---:|---:|---|
| Queen Black Dragon | `rs2012_qbd_sleeping` / `default` / `crystal` / `hardened` | 2,100 | 4 × 1,875 | never dies; forms swap via `npc_changetype`, phase 3+ |
| Tortured soul | `rs2012_qbd_tortured_soul` | 147 | 50 | slow (1 tile per 2 ticks); casts a homing shadow, then weak melee; weak to slash |
| Giant worm | `rs2012_qbd_giant_worm` | 123 | 65 | intermission add; melee in reach, accurate magic bolt otherwise; drops bones |

Souls per phase (target population): 0 / 1 / 2 / 4 — and phase 4 opens with
all four summoned at once. Fire-wall waves per phase: 1 / 2 / 3 / 3. Souls
and worms are ordinary mortal NPCs on ToriRSServer's own HP scale, the same as
every other NPC in the tree; they bypass QBD's hit cap and
intermission immunity.

## 5. Attack compendium

Every attack follows the same grammar: **tell** (chat line and/or wind-up
animation) → **delivery** (wall, projectile, ground effect, or strike
synchronised to the animation) → **counterplay window**. Tick timelines are
the contract; §7 states when each attack may be selected. Recovery = ticks
until her next attack of any kind.

### 5.1 Melee bite — phase 1+, melee zone only

- Selected only when the player is north of one square above the centre
  artefact (z ≥ 33). Centre bite 16717; west/east head-swing 16743/16744 when
  the player is ≥3 tiles off her centre line.
- T0: animation; T0+1: hit lands. Max 48 (475 LP). Protect/Deflect Melee
  blocks fully. No warning line. Recovery 4–15.

### 5.2 Ranged sweep — phase 1+, any range including melee

- 16718: she rakes her head across the platform in a slashing motion. No
  projectile — it lands on the animation.
- T0: animation; T0+1: hit lands. Usually high 30s, max 53 (525 LP).
  Protect/Deflect Missiles blocks fully. This is her default attack on a
  distanced player. No warning line. Recovery 4–15.

### 5.3 Ordinary dragonfire — phase 1+, any range

- Tell: 16721 breath + muzzle flame 3143 on her. No chat warning.
- Delivery: the breath washes over the player; the tall flame column 3149
  plays on the player's tile as it lands at **T0+1**.
- Damage: 70–90 unprotected (700–900+ LP; "It is even so strong it can
  charge a Dragonfire shield in one attack"); 10–23 with shield or antifire;
  6–45 with Protect Magic only. Never fully blockable; the absorb message
  states which mitigation applied.
- Release profile: no cooldown — she may chain-breathe. 7-Aug profile: 17-tick
  (~10 s) cooldown (the "Some Like it Cold" patch). Recovery 4–15.

### 5.4 Moving fire wall — phase 1+ (waves: 1/2/3/3)

The signature attack. Full specification in §6.

### 5.5 Tortured soul summon — phase 2+

- Tell: `The Queen Black Dragon summons one of her captive, tortured souls.`
  (or `…several of her captive, tortured souls.`) in purple.
- Missing souls (up to the phase target 1/2/4) materialise — the first **one
  square west of the player** (era-documented; fall back to the flanking
  spawn tiles when blocked), extras on the remaining sides/fixed tiles. Each
  new soul speaks one of its six lament lines ("NO MORE! RELEASE ME, MY
  QUEEN! I BEG YOU!", …).
- Every idle surviving soul also re-arms its shadow cast (§5.6) on a summon
  event — old spirits cast alongside newly summoned ones.
- **Phase 4 opens with a full summon to 4** the moment combat resumes; the
  ordinary summon attack keeps the population topped up afterwards.
- Cooldown 41–100 ticks; recovery 4–15. 7-Aug profile also enforces the
  ~10-s summon↔shadow separation.

### 5.6 Soul shadow cast — soul behaviour, no warning line

- T0: the soul teleports adjacent to the player (3147 + 16861).
- T0+2 (**exactly 1.2 s** — the era-documented dodge cue is "move the moment
  it speaks"): cast 16864 + 3145 on the soul; the black-flame shadow (3146)
  spawns on the **opposite** adjacent tile.
- The shadow homes at **1 tile per tick**, re-rendering 3146 on its current
  tile each step, and never expires while the player is in the arena
  (bounded to the platform envelope).
- Contact with the player: 20–26 (era convergent ~200–260 LP), prayer-proof.
- The shadow is interceptable: it deals its monster hit (500) to any soul it
  touches — one-shot — and wounds a worm. Stepping through/behind the caster
  redirects the cloud into it. This defining counterplay must never be
  optimised away.
- After its single cast the soul closes for weak, inaccurate melee (max 26,
  5-tick cadence), walking at half speed (1 tile per 2 ticks).

### 5.7 Crystal armour / hardened carapace — phase 3+

- `The Queen Black Dragon takes on the consistency of crystal; she is more
  resistant to magic, but weaker to physical damage.` (cyan) — form
  `rs2012_qbd_crystal`; or `The Queen Black Dragon hardens her carapace; she
  is more resistant to physical damage, but more vulnerable to magic.`
  (green) — form `rs2012_qbd_hardened`. 50/50 pick.
- **Both flanking crystals swap to the matching appearance** (west 70822
  family, east 70818 family: default/crystal/hardened children) for the
  duration, and revert with her.
- Pure defence-row swap (no damage transform); 40-tick duration; 41–100-tick
  cooldown; recovery 4–10.

### 5.8 Soul siphon — phase 3+ while souls are alive

- Tell: `The Queen Black Dragon starts to siphon the energy of her mages.`
  (purple).
- **Channeled drain**, not a one-shot: every 2 ticks, each living idle soul
  plays 3148 and takes 2, and she heals 4 per soul drained; the adjacent
  siphon effect 3150 plays on her while the channel runs. The channel ends
  when no soul remains or after 25 drains (a full 50-LP soul drains to death
  for 100 LP healed — "any damage done to them is worth double the health
  the QBD would restore", so killing siphoned souls quickly is the
  counterplay).
- Ordinary attacks continue during the channel (it is a background drain).
- Release profile: no cooldown. 7-Aug profile: 50-tick (~30 s) cooldown.
  Recovery 4–15.

### 5.9 Time stop — phase 4 only, cast by a soul

- An existing idle soul is commandeered (never a fifth spawn): it teleports
  to the east or west edge — (24,28) or (42,28) — and channels, speaking its
  four lines **overhead** at 3-tick intervals: `Kill me, mortal... quickly!
  HURRY! BEFORE THE SPELL IS COMPLETE!` → `Time is short!` → `She is pouring
  her energy into me... hurry!` → `The spell is nearly complete!`
- **Kill window: 15 ticks** (9 s; era wiki says "10 seconds"). Killing the
  caster cancels the spell; the channel survives intermission checks only as
  long as combat continues.
- On completion: screen tints green (HUD overlay), `The tortured soul has
  stopped time for everyone except himself and the Queen Black Dragon.`, the
  player is action-locked for **12 ticks** (7.2 s): movement, eating,
  attacking rejected; other souls and worms freeze; QBD and the caster keep
  acting. All damage dealt during the freeze accumulates and lands as **one
  hitsplat** when time resumes.
- A surviving caster rejoins the ordinary soul population afterwards.
- Cooldown 40–90 ticks from resolution; recovery 5–10; never overlaps an
  active channel.

### 5.10 Extreme dragonfire — phase 4

- Tell: `The Queen Black Dragon gathers her strength to breathe extremely
  hot flames.` (yellow) + 16745 + the multi-coloured breath 3152.
- Delivery: **three rounds of damage** centred on the platform centre
  (33,31), landing at **T0+4, T0+6, T0+8**. Per-round damage divides by
  `(distance from centre / 3) + 1` — over 30 close to her, ~6 at the
  east/west edges; max 97 (970 LP) per round unprotected at the epicentre.
  Dragonfire mitigation applies per round.
- The refuge is the far east/west platform edge — never the south (walls and
  hazard rows own that space).
- Each round is also the Royal crossbow forge/repair trigger (`Brandish`
  during phase 4 in her extreme flame).
- Recovery 8–15.

### 5.11 Grotworm eruption — intermissions

- Every **5 ticks** while an artefact awaits restoration: she plays the
  cough 16747 and **lobs a worm projectile (3141) to a random mid-field tile**
  (x 28..38, z 25..31); the landing splash 3142 plays there and the giant
  worm hatches on that tile **3 ticks after the cough**, already aggressive.
- Restoring the artefact plays the stop-cough 16748 and ends the eruption.
- Worm behaviour: melee in reach (5-tick cadence); otherwise an accurate
  magic bolt (3162 cast, 3164 projectile, damage on the arrival tick;
  Protect/Deflect Magic blocks fully). One-life; drops bones.

## 6. The moving fire wall — full specification

### 6.1 What the player experiences

`The Queen Black Dragon takes a huge breath.` (orange) — she rears up
(16846) and exhales a **wall of licking orange flame spanning the full
19-square platform**. The wall detaches from her at row z=38 and sweeps south
at **one tile per tick**, burning past the last platform row (z=19) before
dissipating (~11.4 s of travel). Each wall has exactly one cool gap, and the
three wall types place it at a fixed, era-documented square:

| Type | Gap square | Gap column x | Spawn anchor x |
|---|---:|---:|---:|
| 1 | 15th ("5th from the right") | 38 | 42 |
| 2 | 9th ("one west of the centre artefact") | 32 | 36 |
| 3 | 5th | 28 | 32 |

All three waves render the 3160 wall (dest 10019, model 110101), whose baked
hole sits 4 tiles west of the model centre — hence anchor = gap + 4. The
era-distinct 3158/3159 models place their holes 20 tiles west / 16 tiles
east of centre, which puts their anchors at columns 48/22: off the platform,
where the endpoint terrain sample is the water level and the wall sinks out
of sight. The three era *types* survive as the three gap positions, which is
the only gameplay-visible difference between the source models.

**She cycles these types in order (1 → 2 → 3 → 1 …) across the whole fight**,
so an attentive player predicts the next gap — the era strategy of camping
one square west of the centre artefact relies on it. The cycle position
advances per wave, persists across casts and phases, and each wave in a
multi-wave cast uses the next type in the cycle.

The player survives by standing in the gap square as the wall washes past,
running through the front (usually one hit), or eating through contact
damage. Standing still in the flame takes **two hits** as the two-tile-deep
front passes. The wall leaves nothing behind — the danger is the moving
front itself.

- Contact damage per hit, rolled on the dragonfire ladder ("getting caught
  in the fire is the same as being hit by her Dragonfire attack"): 40–76
  unprotected, 20–45 with Protect Magic only, 20–26 with shield/antifire.
  Message: `You are horribly burned by the fire wall!`
- Waves per phase 1/2/3/3, consecutive waves **7 ticks apart**, each with
  the next gap type in the cycle — the player repositions laterally between
  waves, usually along the southern rows, while her ordinary attacks
  continue.

### 6.2 Timeline (per cast, T0 = the ai-tick that selects the attack)

| Tick | Event |
|---|---|
| T0 | orange warning line; the breath OPENS on 16846 |
| T0+3 | wave 1 wall materialises across z=38 |
| T0+3+k | wave 1 occupies row z=38−k; that row and the one behind it burn |
| T0+10 | wave 2 materialises (phase ≥ 2), next gap type |
| T0+11 | 16846 ends; 16747 takes over and loops — she holds the breath |
| T0+17 | wave 3 materialises (phase ≥ 3), next gap type |
| T0+17 | the last wall has left her: 16748 closes the breath |
| T0+22 | wave 1 passes z=19 and dissipates |
| T0+29 / T0+36 | waves 2 / 3 dissipate |

Wire-observable form: 1 / 2 / 3 wall rows on ticks 3 / 10 / 17, and 60 rows
total for a three-wave cast (3 waves × 20 rows).

Recovery after the cast is 8 + 2×waves ticks (10/12/14), and the attack's own
cooldown is uniform over (7×waves + 5)..60 ticks (open727's range), so walls
punctuate the fight roughly twice a minute and overlap her ordinary attacks
while in flight.

### 6.3 Mechanic model (server)

The cast schedules every wave and every wave's damage stepper upfront
(deterministic delays; see the drain subtlety in the conventions above);
each stepper then advances itself **one row per real tick**:

- state per wave: leading row `front_z` (starts 38), gap column `gap_x`,
  both encoded in the queue argument;
- the damage front is rows `front_z` and `front_z+1` (two-tile depth
  reproduces the stationary double-hit); a hit is rolled at most once per
  tick per wave: player in the front rows, `px ≠ gap_x`, and inside the
  arena instance;
- damage rolls the dragonfire-protection ladder of §6.1 and routes through
  the standard queued-hit path (so time-stop accumulation and teardown
  guards apply);
- the wave expires after `front_z` passes 19; intermission and session
  teardown clear all wave queues.

### 6.4 Visual model (engine): the `flamewall_map` moving zone graphic

The authentic rev-727 wall spot-animations glide with the damage front. The
wall models are **not** platform-sized props: their face geometry spans
**76.25 tiles** of X (deliberately overshooting the whole arena so the flame
reads as a sea of fire beyond the platform), ~10 tiles tall, ~9 deep, origin
at the geometry centre, base on the ground plane, at 100% scale with no
rotation (verified against source bytes). Each pattern's gap sits at a fixed
model-local offset (§6.1 table), which is why **each type needs its own
spawn anchor column** — anchor = gap column − gap offset. A shared anchor
puts every visual gap in the wrong place; this was one of the original
implementation's defects.

Realisation — **official packets only**, no engine change. Retail delivered
the wall as one ballistic-free `MAP_PROJANIM` glide, and that remains the
target shape; this client, however, does not currently draw coord-targeted
projectiles (the same gap that leaves Inferno glyph projectiles invisible —
see `inferno-adds-glyph-projectiles`). Until that is repaired the wall rides
the other official packet:

- Each wave is a queue chain that, every tick, spawns the wall
  spot-animation via `spotanim_map` (**MAP_ANIM**) on the row it is about to
  burn, then re-arms itself one row south. The wall sequence (22043) is a
  two-frame, 30-cycle loop — exactly one game tick — so consecutive
  per-row spawns read as one wall sweeping south at one row per tick.
- The **same queue** does the damage, so the visual row and the damage front
  are the same object by construction rather than by matched clocks.
- The spawn anchor per wave is the type's anchor column, so the model's
  baked hole lands on the era gap column; every anchor lies on the main
  platform (§6.1) because an off-platform anchor samples the water height
  and sinks the whole wall below the floor.
- When the projectile path is repaired, one wave becomes a single
  `projanim_map((anchor,38) → (anchor,19), wall_gfx, 0, 0, 0, 570, 0, 0)` —
  duration 570 cycles = 19 rows × 30 = one row per tick, `peak 0`/`arc 0`
  degenerating the arc math to a flat, unrotated ground glide — with the
  damage stepper unchanged.

The previous implementation sent `peak 46` and `duration 18` cycles at a
shared anchor of x 33: a 0.36-second ballistic lob racing an 11-second
damage front, every visual gap in the wrong column, and (given the
projectile-draw gap) nothing on screen at all.

## 7. Attack selection and pacing

She acts on a single attack clock, driven every tick (`ai_timer` 1): when
the recovery counter reaches zero she selects one attack, applies it, and
sets the counter to that attack's recovery (§5). The 2012 infobox "attack
speed 4" is the floor of every recovery range: at her fastest she acts every
2.4 s. Special-attack cooldowns tick down in real time regardless of what
she is doing (decrementing them only on selection made the documented
10/30-second separations several times too long).

Selection is **random among the attacks the phase has unlocked**, not a
priority ladder. One `random(100)` per attack tick picks a band; a band whose
attack is phase-locked or still cooling falls through to the ordinary
rotation, which is what keeps melee/ranged/dragonfire the bulk of the fight.

| Roll | Attack | Gate |
|---|---|---|
| 0–21 | **Fire wall** | cooldown (7×waves+5)..60 ticks |
| 22–33 | **Time stop** | phase 4, an idle soul alive, no live channel, 40–90-tick cooldown |
| 34–51 | **Extreme fire** | phase 4 |
| 52–63 | **Armour form** | phase 3+, 41–100-tick cooldown |
| 64–78 | **Soul summon** | phase 2+, population below target, 41–100-tick cooldown |
| 79–88 | **Siphon** | phase 3+, souls alive, no channel running |
| 89–99, or any band that fell through | **Ordinary** | dragonfire ~30%, else melee in her zone (z ≥ 33), else the ranged sweep |

In phase 1 only the wall band is live, so roughly four attack ticks in five
are ordinary ones. A measured phase-1 opening: ranged, ranged, ranged,
dragonfire, ranged, ranged, wall, ranged, wall.

Her cooldowns all read zero on a fresh NPC, so the wall is armed with a short
opening cooldown when she wakes and again after each restoration — otherwise
the very first attack of every phase was a fire wall.

Attack-selection randomness is a design fact of the 2012 fight ("she uses
more of them as the battle progresses… some of these attacks have cooldowns
to prevent multiple powerful attacks at once"); the only deterministic
sequencing is the wall-gap cycle of §6.1 — preserve both properties.

During intermissions all clocks freeze; on restoration they resume rather
than reset (a phase must not open with every special simultaneously ready),
and her first post-restoration attack waits 20 ticks.

## 8. Phase-by-phase experience

- **Phase 1** — the tutorial in miniature: learn the melee zone, the breath,
  and a single wall at a time. Only the main platform exists. First attack
  ~24 s after entry.
- **Phase 2** — the west raw platform unlocks (artefact 2 at its south end).
  One tortured soul stalks the player; walls come in pairs with cycling
  gaps; the shadow punishes standing still in yesterday's safe square.
- **Phase 3** — east platform unlocks. Two souls; armour forms demand a
  style swap (watch the flanking crystals change with her); the siphon
  channel punishes leaving souls alive; triple walls.
- **Phase 4** — centre platform unlocks; four souls immediately, time stop
  (kill the channeler across the arena, through walls, over hazard tiles),
  extreme fire (hug the east/west edge), and the Royal crossbow brandish.
- **Intermissions** — dormant queen, glowing artefact at increasing distance
  over hazard platforms, a worm lobbed every 3 s, surviving souls still
  casting.

## 9. Implementation mapping

| Concern | Owner |
|---|---|
| session, phases, artefacts, walk gate, hazard, lifecycle | `rs2012_qbd_session.rs2` |
| attack selection, ordinary attacks, forms, siphon channel, extreme fire | `rs2012_qbd_combat.rs2` |
| fire-wall waves, souls, shadows, time stop, worm eruption | `rs2012_qbd_adds.rs2` |
| moving wall visual | official `projanim_map` with flat-glide parameters (§6.4) — no engine change |
| HUD pool bar / time overlay | `rs2012_qbd_ui.rs2`, cs2 13000/13001 |
| music, sound effects, area sounds | `AUDIO.md` — music is dispatched from `rs2012_qbd_session.rs2`; every effect rides a sequence frame |
| assets (all present in the composed cache; ids per port ledger) | `RS2012_QBD_TD.md` §5, RESEARCH.md §4 |

NPC var-slot allotment on the queen: 0 attack recovery, 2 soul-summon cd,
3 armour cd, 4 siphon cd, 5 time-stop cd, 6 armour ticks left, 7 dragonfire
separation (7-Aug), 8 fire-wall cd, 9 siphon channel ticks. Wall-gap cycle
position and wave bookkeeping live in player varps (session-scoped).

## 10. Verification checklist

- `::rs2012qbdtest` — encounter contract (pools, wave counts, soul targets,
  cooldown switches, caps, masks, teardown) plus the new wall-cycle, anchor,
  and cadence assertions.
- `tools/test_rs2012_qbd_combat_contract.py` — static contract greps.
- Wire: a ToriRSServer selftest capture stanza byte-decoding the wall rows off
  MAP_ANIM — 1/2/3 rows on ticks 3/10/17 and 60 rows per three-wave cast.
- Timing: server queue tracing (`TORIRS_ANIM_DEBUG`) across one phase-4
  rotation; every §5/§6 timeline lands on its tick.
- Visual: headless client screenshot of a wall mid-travel; live
  `::qbd` run through all four phases.
