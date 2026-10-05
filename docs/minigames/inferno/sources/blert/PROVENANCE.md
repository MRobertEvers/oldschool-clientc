# Blert and the Inferno: what the recorder OBSERVES and what it ASSERTS

Read this before citing Blert for the Inferno. Written 2026-10-03 from the code pinned beside
it (see `README.md`): plugin `0efb39800b9e1f73e01c0116b962ccc93f0f8f6f`, blert
`7c7750cf01b23e5d223623d7c34c5ef10ca327fd`, protos `450112dffc33e1e51b7ec201e1c8a45bf2f903c9`.
Method: `docs/TOB_RESEARCH.md` "Provenance audit". Paths below are relative to
`plugin/src/main/java/io/blert/` unless they start with `blert/` or `protos/`.

Rules that follow from it:

* **Only an OBSERVED event can carry grade B.** A number that is an ASSERTED constant is "what a
  load-bearing plugin uses", never a measurement, and is tagged `[plugin]`, not `[blert]`.
* An event's **tick** is `DataTracker.getTick()` = the client's tick count minus the tick the wave
  tracker started (`core/DataTracker.java:156-160`, `:371-385`). The wave tracker starts on the
  chat message `Wave: N` (`challenges/inferno/WaveDataTracker.java:142`). Tick 0 of a wave is
  therefore the tick that message arrived on; it is a recorder tick, not a server tick number.
* The events the API serves for a wave are the stored events of that stage; the processor DROPS
  `INFERNO_WAVE_START` from the stream (`blert/challenge-harder/src/processing/inferno.rs:280-285`
  returns `false`), so a wave start tick is only reachable through the per-wave record
  (`GET /api/v1/challenges/inferno/<uuid>` -> `inferno.waves[].startTick`).

## 1. Event kinds the recorder emits for this minigame, and what each is anchored to

Event ids: `events/EventType.java` and `protos/event.proto`. The Inferno defines ONE event of its
own (`INFERNO_WAVE_START` = 300, `events/inferno/InfernoWaveStartEvent.java`); everything else is the
shared stream. There is **no** Inferno-specific event for the Zuk shield, the Zuk healers, the
set spawns, the pillars' damage, the blob splits, the mager's revive target or any projectile.
They exist only as NPC_SPAWN / NPC_UPDATE / NPC_DEATH / NPC_ATTACK rows of the npc ids in
`challenges/inferno/InfernoNpc.java:33-76`.

### OBSERVED

| Event | Anchored to | Where (file:line) | Caveat a spec worker must keep |
|---|---|---|---|
| `NPC_ATTACK` (10) | an `AnimationChanged` on a tracked npc whose animation id is in the npc's row of `InfernoNpc` (a lookup, `getAttack(animation)`) | `challenges/inferno/WaveDataTracker.java:112-139`; table `InfernoNpc.java:33-76` | Tick = the tick the client saw the animation set, not the hit tick. Only animations in the table are reported: nibblers have none; Zuk has one (7566) for every Zuk attack; npc 7707 (`ZUK_MOVING_SAFESPOT` in the plugin, `ZUK_SHIELD` size 3 in `protos/npc_definitions.json:179-190`) and the Zuk healers (7708) have none. The attack KIND is the plugin's name for the animation id, not a seen projectile. `target` = whom the npc is interacting with at that instant (may be null). Whether a repeat of the same animation id on consecutive cadence ticks fires `AnimationChanged` is NOT stated by the code; read it off the gap distribution (a gap of 2x the cadence is a suspect) |
| `NPC_SPAWN` (7), spawned **after** the wave's tick 0 | `NpcSpawned` client event on a tracked-type npc (blob splits, mager resurrections, Jad healers, Zuk set spawns) | `core/DataTracker.java:763-770` -> `addTrackedNpc` `:735-743` sets `spawnTick = getTick()`; event built by `sendNpcUpdate` `:718-730` | Emitted at the END of the tick (inside `tick()`), with the npc's location read then. Real spawn tick |
| `NPC_UPDATE` (8) position and npc id | every tracked npc, every tick, location and `npc.getId()` read from the client | `core/DataTracker.java:718-731`, `events/NpcEvent.java:37-65` | The npc id of an update is the id at that tick, so a retype (e.g. a Zuk phase form) shows as an id change on an update row |
| `NPC_DEATH` (9) | `NpcDespawned` on a tracked npc (`onNpcDespawn` returns true) | `core/DataTracker.java:772-780`, `:745-749`; `WaveDataTracker.java:104-111` | A DESPAWN, **not** the tick hitpoints reached zero and not the start of the death animation. The delay from the lethal hit to the despawn is the death animation's length and is inside the number. The pillar's collapse is also an NPC_DEATH of npc 7709. A wave tracker that is terminating (the player left, died, logged out) emits none (`:773-775`) |
| NPC hitpoints inside NPC events (current value) | `HitsplatApplied` on the npc: `drain(amount)`, or `boost(amount)` when the hitsplat type is HEAL | `core/DataTracker.java:831-850` | Observed damage and heal amounts; but the BASE and the starting value are asserted (below). Damage the client never saw (a hitsplat before tracking) is absent |
| `PLAYER_UPDATE` (4) | every tick, per party member: location, stats, prayers. The local player's active prayers come from the client's own prayer varbits (`getPrayersFromClient`); a non-local player's only from the OVERHEAD ICON (so a non-local player's piety or rigour is invisible) | `core/DataTracker.java:497-510`; `events/PlayerUpdateEvent.java:86-126`, `:183` | The record of when the player prayed. Hitpoints/stat fields are packed (current in the high half, base in the low, see the data) |
| `PLAYER_ATTACK` (5) | the player's attack animation id + equipped weapon id (+ a matching in-flight projectile id and start cycle to tell spells/ammo apart), gated by the attack's cooldown | `core/DataTracker.java:613-716` (`checkForPlayerAttack`, `adjustForProjectile`); table `protos/attack_definitions.json` | The animation is observed; the COOLDOWN used to decide "this animation was a new attack" is an asserted per-weapon table (`protos/attack_definitions.json`), so a re-animation inside the assumed cooldown is not reported |
| `PLAYER_SPELL` (11) | the player's animation id or a graphic id on the caster | `core/DataTracker.java:526-611` | as above |
| `PLAYER_DEATH` (6) | the client's `ActorDeath` event for a party member (`onActorDeath` stores the tick; `tick()` emits it) | `core/DataTracker.java:895-905`, `:137-145` | Tick = tick the death event reached the client |
| `STAGE_UPDATE` STARTED | the chat message `Wave: N` | `WaveDataTracker.java:142-143`; `core/DataTracker.java:383` | Defines tick 0 |
| `STAGE_UPDATE` COMPLETED / WIPED, with `ticks` | the chat message `Wave completed!`, or the `Duration: m:ss.cc` message at the end of wave 69; WIPED when the tracker is torn down unfinished | `WaveDataTracker.java:144-150`; `core/DataTracker.java:431-490` | `ticks` = `getTick()` when the message arrived. This is the wave's length as the client saw the message |
| `CHALLENGE_END` ticks | the game's own `Duration: ...` chat string, converted at 0.6 s a tick (`util/Tick.java:30`, `:68-100`) | `challenges/inferno/InfernoChallenge.java:164-185`, `:256-262` | The only place the run's total comes from the game's clock, to a centisecond when the string is precise |
| `CHALLENGE_START` | the local player's location entering region 9043 | `challenges/inferno/InfernoChallenge.java:52`, `:220-254` | No tick; says only that the player was in the Inferno |

### OBSERVED but with a clamp (read the caveat before quoting a tick)

| Event | Anchored to | Where | The clamp |
|---|---|---|---|
| `NPC_SPAWN` (7) on **tick 0** (the wave's own set: bats, blobs, melee, ranger, mager, nibblers, and the three pillars every wave) | `NpcSpawned`, but stamped with `getTick()` while the tracker is `NOT_STARTED`, which is **0** | `core/DataTracker.java:156-160` (`if (notStarted()) return 0;`), `WaveDataTracker.java:71-82` (comment: "NPC spawn events are typically received before the wave start message, so capture any NPCs that are already present") | Whatever tick the npcs truly appeared on, they are reported on tick 0. The tile is observed and the SET is observed; the spawn TIME is not. Do not quote "spawns at tick 0" as a measurement. The pillars (7709) are re-added to every wave's tracker at tick 0 (`InfernoChallenge.java:264-280`), so they spawn on tick 0 of every wave by construction |
| `NPC_UPDATE` first row of the wave's npcs | same | same | same |

### ASSERTED (constants or the plugin's/server's own clock)

| Constant / event | Value | Where | Consequence |
|---|---|---|---|
| `INFERNO_WAVE_START` (300) `startTick` | `currentTick - challengeStartTick`, where `challengeStartTick = tick of "Wave: 1" - 10` | `InfernoChallenge.java:61`, `:164-170`; `WaveDataTracker.java:66-69`; `InfernoChallenge.java:90-97` (`recordedDurationTicks`) | The origin is an ASSERTED 10 ("The inferno timer begins 6 seconds (10 ticks) before the first wave"), so wave 1's startTick is always 10 by construction. The DIFFERENCES between waves' start ticks are observed (both ends are chat messages). Not sent at all if the player logged out mid-run (`hasLogged`, `WaveDataTracker.java:66`) |
| `WAVE_INTERVAL_TICKS` | 6 | `blert/challenge-harder/src/processing/inferno.rs:26`, used `:327-335` only when no start event exists; TS twin `blert/challenge-server/event-processing/inferno.ts:196` | When a wave has no start event the server ADDS 6 ticks. Where start events exist the inter-wave gap is measurable (`verify_blert.py overviews`); do not cite 6 as measured from the constant |
| NPC hitpoints | nibbler 10, bat 25, blob 40, bloblets 15, melee 75, ranger 125, mager 220, Jad 350, Jad healer 90, Zuk ranger 125 / mager 220 / Jad 350 / Jad healer 90, Zuk 1200, `ZUK_MOVING_SAFESPOT` (7707) 600, Zuk healer 75, pillar 255 | `challenges/inferno/InfernoNpc.java:33-76` | The `hitpoints` field of an npc's first event is this constant, reduced by observed hitsplats. It is not read from the cache and not measured. Compare with the cache dump before using any of them |
| Resurrected npc hitpoints | `(hp + 1) / 2` of base, for any npc of a resurrectable type (bat, blob, melee, ranger, mager) that spawns after tick 0 in a wave of 66 or lower | `WaveDataTracker.java:94-96`; `InfernoNpc.java:109-126` | An npc that appears after tick 0 is ASSUMED to be a mager's revival. A bat that spawns late for any other reason would be mis-tagged. The half-HP rule itself is the plugin's belief |
| Pillar tiles | west (2257,5349), east (2274,5351), south (2267,5335) | `challenges/inferno/Pillar.java:43-45`; `blert/challenge-harder/src/processing/inferno.rs:196-203` | Used to name a pillar. Matched against observed spawn tiles, so a mismatch is visible in the data |
| The nine spawn tiles and the arena base | base (2257,5358); tiles (2258,5330) (2262,5335) (2272,5330) (2258,5353) (2273,5341) (2279,5353) (2260,5347) (2280,5333) (2280,5346) | `blert/web/app/utils/spawn-index.ts:61-90`; `blert/challenge-harder/src/processing/inferno.rs:28-45` | The spawn index accepts a wave's set only when each expected npc is on one of these tiles. A spawn off the list leaves the wave "undetermined" (`blert/challenge-harder/src/processing/spawn_index.rs:129-160`), so the list is a filter as well as a record |
| The wave table (which of bat / blob / melee / ranger / mager each wave carries) | 69 rows | `blert/challenge-harder/src/processing/inferno.rs:47-117` | An EXPECTATION used to match spawns, not an observation. It lists no nibblers (not an arena type), and rows 3, 8, 17, 34 and 67-69 are `[]`; an empty row means "no indexed npc type", not "no monsters". Check each row against the recorded spawns and the wiki before use |
| Region ids | 9043 (the Inferno), 9807, 9808, 10063, 10064 (Mor Ul Rek quadrants) | `InfernoChallenge.java:52-56` | Where the plugin decides it is in the minigame |
| 0.6 s a tick | 600 ms | `util/Tick.java:30` | Used to turn `Duration:` into ticks |

## 2. What this leaves for a Blert-backed spec row

| Quantity | Reachable as | Grade ceiling |
|---|---|---|
| Which npcs a wave carries and where they stand at its start | tick-0 NPC_SPAWN set + tile (observed set, clamped time) and the server's `spawns` record | B |
| Spawn tick of a blob split, a resurrected npc, a Jad healer, a Zuk set npc | NPC_SPAWN after tick 0 | B |
| An npc's attack cadence (gap between its NPC_ATTACK rows) | observed animation ticks | B (check repeat-animation suspicion against the gap distribution) |
| First attack after a real spawn | NPC_SPAWN then NPC_ATTACK | B |
| Ticks from a hit to the despawn | NPC_DEATH minus the lethal NPC_UPDATE hitpoints drop | B, includes the death animation |
| Wave length, gap between waves | per-wave record `ticks`, `startTick` differences | B |
| Hitpoints, resurrected hitpoints, healing amounts | constants | plugin constant only (`[plugin]`, never `[blert]`) |
| Projectile flight, hit tick, damage, max hit | **not recorded for any npc** | none |
| What a monster targeted, retaliation, aggro, pathing | NPC_ATTACK `target`, NPC_UPDATE positions only | B for what moved; nothing for why |
| The Zuk shield path, the safespot, the healers' behaviour | positions of npcs 7707/7708 in NPC_UPDATE only | B for positions, never for rules |

Never in this corpus: a prayer-timing rule, a damage figure, or a line-of-sight rule from Blert.
