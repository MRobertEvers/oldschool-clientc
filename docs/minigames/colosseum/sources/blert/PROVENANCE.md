# Blert and the Fortis Colosseum: what the recorder OBSERVES and what it ASSERTS

Read this before citing Blert for the Colosseum. Written 2026-10-04 from the code pinned beside
it (see `README.md`): plugin `0efb39800b9e1f73e01c0116b962ccc93f0f8f6f`, blert
`7c7750cf01b23e5d223623d7c34c5ef10ca327fd`, protos `450112dffc33e1e51b7ec201e1c8a45bf2f903c9`.
Method: `docs/TOB_RESEARCH.md` "Provenance audit". Paths below are relative to
`plugin/src/main/java/io/blert/` unless they start with `blert/` or `protos/`. Line numbers are
the pinned files' own.

Rules that follow from it:

* **Only an OBSERVED event can carry grade B.** A number that is an ASSERTED constant is "what a
  load-bearing plugin uses", never a measurement, and is tagged `[plugin]`, not `[blert]`.
* An event's **tick** is `DataTracker.getTick()` = the client's tick count minus the tick the wave
  tracker started (`core/DataTracker.java:156-160`, `:371-384`). The wave tracker starts on the
  chat message `Wave: N` (`challenges/colosseum/WaveDataTracker.java:303-306`, `:373-380`) or, for
  wave 12, on the message `Sol Heredit jumps down from his seat...` with a start offset of -1
  (`:46`, `:307-310`; whichever arrives first, `:374` ignores the second). Tick 0 is a recorder
  tick, not a server tick. Because the offset is -1, a wave 12 started by the boss message has its
  tick 0 ONE TICK BEFORE the message: an asserted offset. The sample cannot say which message
  started the tracker: Sol is first seen on tick 6 in 12 of 12 wave-12 streams and the first dust
  on tick 14 in 10 of 12 (15 in 2) (`SAMPLE_SUMMARY.md`); a one-tick shift would show as a 7/6
  split and does not. OPEN: read the stored stage start of wave 12 against its `Wave: 12` line
  in a video before treating Sol's tick 6 as a game constant.
* **The tick-0 batch is a set, not a spawn time.** The wave tracker is created when Minimus
  spawns (`challenges/colosseum/ColosseumChallenge.java:126-129`, `:252-261`) but only STARTS on
  the chat message. `DataTracker.getTick()` returns 0 while not started (`core/DataTracker.java:157-159`),
  so every npc the client held at the message is stamped `spawnTick = 0` and reported as an
  NPC_SPAWN on tick 0, whatever tick it truly appeared on. The wave's fixed npcs are this batch.
  Spawns after tick 0 (reinforcements, Sol's adds, totems) carry a real spawn tick.
* **The wave NUMBER is a count of Minimus spawns**, not read from the game: `currentWave++` on each
  Minimus (npc 12808) spawn (`ColosseumChallenge.java:126-129`, `:252-261`, `if( currentWave < 13 )`).
  A recording that missed Minimus (plugin enabled mid-run: `:227` is a TODO) numbers its waves
  early. The stage id served by the API is that count. A `Wave: N` message whose N differs from the
  count never starts a tracker (`:96`, `:303`).
* The events the API serves for a wave are the stored events of that stage:
  `GET /api/v1/challenges/colosseum/<uuid>/events?stage=<99 + wave>`
  (`blert/web/app/api/v1/challenges/colosseum/[id]/events/route.ts:12-19`: stage 100..111 else 400).

## 1. Event kinds the recorder emits for this minigame

Event ids: `events/EventType.java:31-80` and `protos/event.proto`. The Colosseum defines eight
events of its own (200-207, `events/colosseum/`); everything else is the shared stream.

### OBSERVED

| Event | Anchored to | Where (file:line) | Caveat a spec worker must keep |
|---|---|---|---|
| `NPC_ATTACK` (10) of every npc but the manticore | an `AnimationChanged` on a tracked npc whose animation id is in the npc's row of `ColosseumNpc` (a lookup, `getAttack(animation)`) | `challenges/colosseum/WaveDataTracker.java:129-157`; table `ColosseumNpc.java:32-51`; numbers `core/NpcAttack.java:53-68` | Tick = the tick the client saw the animation set, not the hit tick. Only table animations are reported: Sol has 4 (10883 thrust, 10884 break, 10885 slam, 10887 combo), the javelin colossus 2 (10892 auto, 10893 toss); the minotaur is one animation (10843) under two npc ids 12812/12813; bee swarm, laser prism, healing totem, solarflare have none. An animation id re-set on consecutive cadence ticks may not fire `AnimationChanged` (the code does not say): a gap of 2x the cadence is suspect. `animation == -1` events are dropped (`:138`). `target` = whom the npc is interacting with at that instant |
| `NPC_SPAWN` (7) after tick 0 | `NpcSpawned` on a tracked-type npc | `core/DataTracker.java:763-769` -> `addTrackedNpc` `:735-743`; event by `sendNpcUpdate` `:718-730`; filter `WaveDataTracker.java:103-114` (`ColosseumNpc.withId`) | Real spawn tick, built at the END of the tick. Only the 14 ids of `ColosseumNpc` are tracked: Minimus (12808) and any other id (12820, 12822) are not |
| `NPC_UPDATE` (8) position and id | every tracked npc, every tick, position and `npc.getId()` read from the client | `core/DataTracker.java:144`, `:718-733`; `events/NpcEvent.java:37-65` | The id is the id that tick (a retype shows as an id change). The spawn is the first update |
| `NPC_DEATH` (9) | `NpcDespawned` on a tracked npc | `core/DataTracker.java:772-780`, `:745-749`; `WaveDataTracker.java:117-127` | A DESPAWN, not the tick hitpoints reached 0 and not the start of the death animation; the death animation's length is inside the lifetime. Nothing is emitted after the tracker is terminating (`DataTracker.java:773-775`), so a wipe or leaving the arena emits no deaths for the rest |
| `NPC_UPDATE` hitpoints: the DAMAGE taken | `HitsplatApplied` on a tracked npc: `drain(amount)`, or `boost(amount)` for a HEAL hitsplat | `core/DataTracker.java:831-849` | Damage is observed; the starting figure is NOT (see ASSERTED). A hit absorbed by a modifier is still drained at its displayed amount |
| `PLAYER_UPDATE` (4), `PLAYER_ATTACK` (5), `PLAYER_SPELL` (11) | the local party's position, animation, gear and spells | `core/DataTracker.java:497-690` | Generic, as in every Blert challenge. A player attack is an animation matched against the weapon table; the plugin infers the weapon, the target and the tick; it is not a hit |
| `PLAYER_DEATH` (6) | `ActorDeath` on a party player | `core/DataTracker.java:895-905`, `:137-143` | One tick of resolution. The party is the local player only (`ColosseumChallenge.java:90`) |
| `STAGE_UPDATE` completed, ticks | the in-game chat line `Wave N completed! Wave duration: m:ss.ss` (waves 1-11) or `Colosseum duration: ...` (wave 12) | `WaveDataTracker.java:331-358`; `ColosseumChallenge.java:50`; `core/DataTracker.java:431-495`; `util/Tick.java:24-80` | The wave's length is the GAME's own timer. `DataTracker.java:451-461` replaces the recorded tick count with the game's when they disagree and clears `accurate`. A `m:ss` string without centiseconds is rounded UP to a tick (`Tick.java` doc). The start of the interval is the `Wave: N` message, so the length excludes the Minimus walk-in |
| `COLOSSEUM_HANDICAP_CHOICE` (200): the three options | client script 4931 firing, arguments 2-4 are the three handicap ids | `ColosseumChallenge.java:59`, `:193-203`; `events/colosseum/HandicapChoiceEvent.java` | Observed value. The EVENT TICK is the wave's tick 0 (`WaveDataTracker.java:373-380`), not the tick the options appeared. No event is sent for a wave whose options list is empty (`ColosseumChallenge.java:146`); in the sample every wave 1-12 stream carries one (208 of 208, wave 1 included) |
| `COLOSSEUM_HANDICAP_CHOICE` (200): the chosen one | varbit 9788 (value minus 1 indexes the options), read 3 ticks after Minimus despawns | `ColosseumChallenge.java:60`, `:134-158` | The 3 is a read delay, not a game constant. If the player picks later than that, the chosen option is wrong |
| `COLOSSEUM_DOOM_APPLIED` (201) | a hitsplat of type `DOOM` on the local player | `WaveDataTracker.java:194-202` | Observed tick of the hitsplat |
| `COLOSSEUM_TOTEM_HEAL` (202) | projectile 2687 leaving a tracked healing-totem tile (cycle 0 of its flight), then the HEAL hitsplat on the target npc | `WaveDataTracker.java:60`, `:204-248`; `events/colosseum/TotemHealEvent.java` | `start_tick` = tick the projectile was first seen; `heal_amount` is the hitsplat; target = whoever the projectile's target actor is. The event tick is the hitsplat's. Closer's note (2026-10-03): the event needs the projectile's `getSourcePoint()` to equal the totem's SPAWN tile (`healingTotems.get(projectile.getSourcePoint())`, `WaveDataTracker.java:232`; the key is put at `:107`) and a sighting on exactly cycle 0 of its flight (`:243`); miss either and no heal is recorded. So zero events in 208 waves does not show that totems do not heal: the sample cannot tell "no heal" from "heal not seen" |
| `COLOSSEUM_REENTRY_POOLS` (203) | game object 50743 and ground object 50744 spawn and despawn | `WaveDataTracker.java:58-59`, `:251-280`, `:382-434` | Pools already present at the wave start are collected from the scene and reported at tick 0 (`:382-410`): a set |
| `COLOSSEUM_SOL_DUST` (204): the tiles | graphics objects 2669, 2670, 2671 created | `WaveDataTracker.java:61`, `:283-286`, `:452-499` | The graphics are observed |
| `COLOSSEUM_SOL_DUST`: the PATTERN and DIRECTION labels | DERIVED from the tiles: the count of dust tiles at distance 2 from Sol (0 = shield 1, 2 = trident 1, 3 = trident 2, otherwise shield 2); direction from the first such tile | `WaveDataTracker.java:460-496` | A plugin heuristic over observed tiles, not an observed label. Wave 12 only (`:180-182`) |
| `COLOSSEUM_SOL_POOLS` (206) | graphics object 2698 created | `WaveDataTracker.java:64`, `:287-288`, `:501-503` | Observed |
| `COLOSSEUM_SOL_LASERS` (207) | graphics objects 2689-2691 (SCAN) or 2693-2695 (SHOT) created; a shot wins over a scan on the same tick | `WaveDataTracker.java:62-63`, `:289-297`, `:505-510` | Observed tick of the graphic; the phase is the id range |
| `COLOSSEUM_SOL_GRAPPLE` (205): the announcement and DEFEND / PARRY | chat lines `Sol Heredit: I'LL CRUSH YOUR BODY!` (torso), `...BREAK YOUR BACK!` (cape), `...TWIST YOUR HANDS OFF!` (gloves), `...BREAK YOUR LEGS!` (legs), `...CUT YOUR FEET OFF!` (boots); `You successfully defend...`; `You perfectly parry...` | `WaveDataTracker.java:77-84`, `:312-328`, `:361-371` | Slot, outcome DEFEND / PARRY and `attack_tick` are observed chat ticks |
| `CHALLENGE_END` ticks | the chat line `Colosseum duration: ...` | `ColosseumChallenge.java:50`, `:175-190`, `:242-250` | The game's own total, `-1` when not seen |
| the reward chest (completion status) | game object 50741 spawning | `ColosseumChallenge.java:57`, `:164-172` | Marks the challenge COMPLETE after a 3-tick deferral |

### ASSERTED (the recorder's own constant or inference, never a measurement)

| What | Constant | Where | Consequence |
|---|---|---|---|
| NPC starting hitpoints | `ColosseumNpc` hitpoints: jaguar warrior 125, serpent shaman 125, minotaur 225, Fremennik archer / seer / berserker 50 each, javelin colossus 220, manticore 250, shockwave colossus 125, Sol Heredit 1500, healing totem 1; bee swarm, laser prism, solarflare 0 ("not meaningful") | `challenges/colosseum/ColosseumNpc.java:32-55` | Every NPC `hitpoints` field is `constant - observed damage`. The API's hitpoints rows give no independent max. Compare to the cache (`stat4`) before using; `[plugin]` only |
| Manticore attack events 2 and 3 of each burst | `startAttack()` sets `attacksRemaining = 3`; `onTick` emits one attack per tick while it is above 0 | `challenges/colosseum/Manticore.java:84-97`; `WaveDataTracker.java:129-151`, `:165-176` | The first attack is anchored to animation 10869 (`ATTACK_ANIMATION`, `:36`); the next two ticks are ASSERTED, one NPC_ATTACK per tick for three ticks. Gaps of 1 inside a burst are the constant echoing back. Only the gap between burst starts and the STYLE SEQUENCE carry information |
| Manticore attack STYLE | spotanim ids on the npc: 2681 = mage, 2683 = range, 2685 = melee, read only while the animation is 10868 (`LOADING_ANIMATION`) or 10869; "the first one is its next attack" | `Manticore.java:35-82` | The spotanims are observed; "first spotanim = next attack, consumed one per tick" is the plugin's reading. A burst whose style was never read (`style == null`) emits NO event: bursts can be missing. Closer's note (2026-10-03): the null test is per ORB, not per burst: `continueAttack()` decrements `attacksRemaining` and returns `attackForStyle()`, which is null while `style == null`, then clears `style` (`Manticore.java:88-97`, `:99-102`); `updateStyle()` re-reads the first orb spotanim each tick (`:57-82`). A tick on which no orb spotanim was on the npc loses that one orb's event, which is one way a "short burst" arises |
| A Sol grapple HIT | no DEFEND / PARRY message within 4 ticks of the announcement, tested each tick | `WaveDataTracker.java:436-440`, `:361-371` | A hit is the absence of a message, and its `tick` is announcement + 5. Do not read the hit's tick as a hit |
| Wave 12 tick 0 when started by the boss message | `startWave(-1)` | `WaveDataTracker.java:307-310`, `:373-380`, `core/DataTracker.java:371-373` | See the rules above |
| The wave number | Minimus spawns counted | `ColosseumChallenge.java:126-129`, `:252-261` | See the rules above |
| Challenge start | the player standing inside `WorldArea(1806, 3088, 38, 38, 0)` | `ColosseumChallenge.java:54`, `:205-224`, `:226-235` | Starts a challenge, not a wave; region 7216 is the arena, 7316 the lobby (`:52-53`) |
| A wipe / reset | the player's region leaves 7216, or the tracker is terminated | `ColosseumChallenge.java:216-222`, `:237-250`; `core/DataTracker.java:106-117` | The wiped wave's end tick is the tick the plugin noticed (`accurate = false`, no in-game ticks). Status 2 = reset (left), 3 = wiped; a plugin cannot tell a death from a log-out except by `PLAYER_DEATH` |
| `ticksLost`, `offset`, wave start in the stored record | computed by the server (`blert/challenge-harder/src/processing/colosseum.rs`) | `protos/challenge_storage.proto:64-71` | Not read by this tool |
| The WAVE TABLE of the server | `WAVES` lists, per wave, only the spawn-indexed types: shaman, javelin, manticore, shockwave | `blert/challenge-harder/src/processing/colosseum.rs:58-81` | NOT a full wave table: Fremennik, jaguar warriors, minotaurs and Sol's adds are absent by design. A Blert-side prior, not a recording; the observed tick-0 set per wave is the evidence (`SAMPLE_SUMMARY.md`) |

## 2. What this means for the loop

* Grade B candidates, from this recorder: the gap between consecutive animation-anchored
  `NPC_ATTACK` of one npc (not the manticore burst's inner gaps); a real spawn tick after tick 0
  (reinforcement timing); a despawn lifetime (with the death animation inside); the wave length
  (the game's own timer, `accurate` rows); the Sol effects' ticks from graphics objects.
* Grade `[plugin]` only: any npc hitpoint maximum, the manticore three-in-a-row inner cadence,
  the grapple HIT tick, Sol's tick 0, and the wave number.
* The attack gaps are the animation-change cadence. The recorder does not see the hit, the
  projectile's flight, the damage or the prayer check. A wave test needs our own tick log
  beside this for those.
* Never cite the `blert/challenge-harder` `WAVES` table as a wave table; cite `SAMPLE_SUMMARY.md`'s
  observed tick-0 sets and the wiki.
