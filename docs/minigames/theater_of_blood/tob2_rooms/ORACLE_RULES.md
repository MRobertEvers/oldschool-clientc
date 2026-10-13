# Replay-oracle server model: rules established from tick logs (2026-10-12)

Tools (all scratchpad, read-only over runs/<tag>/r*/ticklog.tsv + stdout.txt):
oracle.py (movement), budget.py (damage/heal per room per seat, by source),
nylo_prayer_policy.py (prayer policy scorer), maiden_leaks.py, nylo_collapse.py.
Evidence sets: runs/all128f, runs/all128i (128 relay seeds each), smk*_* (8-seed room runs).

## Timing
R1. INPUT/CONSUME rows logged at tick T are handled BETWEEN T and T+1, before
    T+1's npc turn (torirs_server_ticklog.c header; RAIDER "input" note).
R2. RAIDER(T) is written at the end of T BEFORE the between-tick inputs, so the
    interaction a seat moves under during T+1 is RAIDER(T+1).tgt
    (oracle.py: idle misses 5759 -> 1887 when switched).
R3. The prayer in force at an npc's swing on tick S is RAIDER(S).varp83, not S-1
    (nylo_prayer_policy.py: "prayed but landed" 12.1 -> 5.8 per seat-run).
R4. An npc row's seq_tick (both lanes compute it client-side) is ONE tick before
    the server's npc_anim tick (smk7_nylocas: predicted+1 matched 535 vs 244).
R5. A press decided at bot tick D (after D) is in force for swings at D+1.

## Movement
M1. A walk click's route is the client BFS, open ground: neighbour order
    W,E,S,N,SW,SE,NW,NE, waypoint at each turn (collision_map.c
    collision_flood / collision_route_backtrace); the server steps toward the
    next waypoint diagonal-first, 2 steps a tick when running (walk:run click
    misses 666 -> 410; remaining ~2% are collision/edge cases, not modelled).
M2. Running = run toggle on AND run energy > 0; out of energy a seat walks one
    tile a tick and EVERY tile is a tick-end tile (rmaiden128 r020 walked a trail).
M3. Only tick-end tiles are sampled by: Xarpus stomp/acid, Verzik P2 pool chain,
    Maiden blood, Sotetseg rag (a run's middle tile is free).
M4. Attack-follow: in reach = melee cardinal-adjacent / blowpipe 5 / trident 7
    from the npc footprint at T+1 (npcs move first); otherwise the server routes
    toward the target's moving tile (chase misses still ~30%: routing to a moving
    footprint is the open item).
    SETTLED by 3draster-b0 (v2 probes, 2026-10-12): melee reach is the server's
    entity approach (collision_map_reached, shape -2): cardinal-adjacent with
    no WALL flag on my tile's edge facing the footprint; the footprint's own
    occupancy does not count. The chase is the client BFS route (M1) to the
    reach set (api_drive.route(x, z, {size, range})), reach checked at the
    START of each step against the target's post-step footprint; in reach:
    stand; else walk run?2:1 tiles of that route. Mismatch 47/664 -> 0-7/269.
M5. A packet from a stunned player is DROPPED (`dropped:stunned` INPUT row,
    torirs_server_world.c ToriRSServer_WorldHandle); Bloat hands stun 5 ticks,
    drop every 6 (4 hurt) -> a stun-lock on a crowded tile.
M6. A held op (eat/drink/wield) ends an attack interaction; whether it stops a
    walk is NOT settled by the logs (P3 notes say no) - re-send after a held op.
    SETTLED by 3draster-b0 (consume_shared.rs2): every backpack op clears the
    attack interaction but NOT a walk.

## Prayer / targeting
P1. varp83 bits: protect magic 12, missiles 13, melee 14, piety 26, rigour 27
    (configs/all.varbit varb4116-4118).
P2. Nylocas (tob_nylocas.rs2 ~tob_nylo_fight_tick): swing every 3 ticks
    (Prinkipas 4), target = nearest raider by npc_range (footprint gap), tie ->
    first found = LOWEST pid (all128f: unique 4690, tie-lowest 2955, tie-highest
    624, not-nearest 2886 - the last unexplained). Prayer read at the swing.
P3. Maiden blackstorm: nearest to centre SW+(3,3) Chebyshev, tie -> LOWER orb;
    max 36.5 + 3.5*leaks, halved by Protect from Magic, +25% on the 3rd
    consecutive hit on one target.
P4. Verzik P3 autos: halved by the matching prayer read at LANDING; all of
    all128f's P3 auto damage was prayed (unprayed 1/seat-run).

## Budget (all128i, per seat-run in rooms reached)
Maiden 84, Nylocas 101 (pillars 33), Sotetseg 174 (death balls 158),
Verzik 288 (P3 autos 222). Passing seats: 317 before Verzik, 281 at it.

## P3 time accounting (all128i, 112 runs reaching P3; p3_swings.py)
Windows by her anim: autos 224 ticks/run (swings/seat 35.6 vs ideal 56 ->
~82 ticks/seat lost), webs 67 (13.7 vs 16.7: the web run works), yellows 50
(1.1 vs 12.5: she is invulnerable, unavoidable), crabs 13.
By time: ~5-6 swings/seat/25t until her HP < ~20% (t+150, the enrage:
tornadoes, attack speed 7 -> 5), then ~2.5-3.5 -> the enrage kite halves DPS.
HM's one-time heal at <=5% (tob_verzik.rs2 ~tob_verzik_hard_last_heal: 30% of
the pool) is 1050 hp/run - a quarter of P3 again unless she dies from >5% in
one tick. Unavoidable P3 damage = prayed autos (all prayed, P4); the lever is
P3 LENGTH (autos hit everyone), i.e. the enrage kite's uptime and the heal.

## Healing economics (all128j, 2026-10-12)
H1. P3 deaths are starvation: 80 of 81 seats dead at Verzik had 0 healing
    left. P3-failing runs entered Verzik with 1202 hp of team healing
    (20/fish, 16/brew dose), passing runs 1685 (survivors kept 433).
    Rough line: a team needs ~1300+ in at Verzik. Every hp saved before
    Verzik is P3 margin; brewwaste.py: drinks waste only ~80/run (Sotetseg's
    death-ball top-up above 99). opheld5 consume rows are DROPS (share healing).
H2. Sotetseg 10868 damage is ~all death-ball splits (~19/seat a ball, ~6
    balls a run; sote_hits.py); env splat 28 is the realm chip (1-3 a hit
    every ^tob_sote_maze_chip_ticks while running the maze).

## Verzik P3 tank (content tob_verzik.rs2)
T1. She faces her tank (npc_state 3 = 32768+pid) only while it stands at
    range 1 and she stands; a step clears the face. 2+ away she walks toward
    it (naive path); tank under her -> one random cardinal step a tick.
T2. Hard Mode re-picks a RANDOM tank on every special. Her melee check is the
    tank alone, at range 1 after her follow step.
T3. Enraged auto windows (all128j): face known 51 of 125 ticks; 3108 of her
    steps pointed at no seat (the tank dodging under her). Reading the tank
    from her step direction: 252 right / 141 wrong unique - not usable.
T4. Owner 2026-10-12: tank unknown -> run away (AWAY_GAP 3), never under.

## Thralls
TH1. A [softtimer] has no protected player pointer: npc_findowned aborted on
     every thrall tick (all128j: cast 28x/run, never swung). Now [timer].

## Nylocas pillars
N1. A support (10811) collapsing hits EVERY raider in the room (1..max,
    tob_nylocas.rs2 ~1797); ~1.1 collapses a run in all128i/j/k, ~25 a seat
    each; the r044/r090 deaths were two and three collapses. Pillar defence is
    a healing lever.

## Consumables (3draster-b0, content: consume_shared.rs2, food.rs2, sara_brew.rs2)
C1. Food and potions have SEPARATE 3-tick timers: food + brew in one tick is legal.
C2. Food adds 3 to the weapon delay only if that delay is still running;
    potions and wields add nothing.
C3. Anglerfish heals 22% of base and can overheal; a brew heals 2 + 15% of base.
C4. Eating (seq 829) is not a swing for any swing-clock reader. A fresh
    attack press already in reach swings on server tick d, not d+1.
C5. Maiden (v2 lessons): a cast set takes ~3 ticks to put on (one panel a
    tick), so freezers prepare by her HP rate (ticks to the threshold), not a
    permille margin; a spawn freeze cast just before a wave holds the 5-tick
    cast clock through the slot+1 tick.
