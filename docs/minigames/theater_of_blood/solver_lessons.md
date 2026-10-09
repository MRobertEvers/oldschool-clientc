# Verzik solver: lessons from P1

P1 (script/plugins/quest_driver/raid_solve_verzik_p1.lua) reached zero damage
on 16 of 16 seeds, but only after about fifteen run-and-patch rounds and one
failed live run. Every round fixed a fact that was written down in the
content or the engine before the first line of code. These are the rules
that would have prevented each round.

## Before writing code

1. **Write the spec from the content first.** For every hazard: the tick it
   is decided, which tick's positions it reads, its geometry, and its
   damage. The wiki explains intent; the `.rs2` and `tob.constant` say what
   the server does. Where they disagree, the content wins.
2. **Pin the tick mapping once.** Npcs run before players in a server tick,
   so a hazard decided on tick T reads where players stood at the end of
   T-1. A click decided after seeing tick d moves the player during tick
   d+1. Write every deadline in those terms.
3. **Wake on the client's boundary.** Use the `server_tick` drive event
   (SERVER_TICK_END applied), never the server's tick counter: the counter
   moves before the client has the tick's packets, and a live plan woken by
   it reads one tick stale.
4. **Never trust a guessed clock to release a raider.** Before a hazard's
   cadence has been observed once, a guess may only keep raiders safe; it
   must not let them out.

## Facts the team must agree on

5. **Count from what everyone sees.** A pillar's hits come from its own
   hitsplats, not from "the pillar I stood behind". One raider caught out
   once made a different count, picked a different pillar, and hid alone
   behind a pillar on its fourth hit.
6. **Every raider runs the same rule on the same view**, so shared choices
   (which pillar, whose turn with the Dawnbringer) need no messages.

## Movement

7. **Measure paths in steps round obstacles, not Chebyshev tiles.** A
   five-tile run took seven steps round a pillar.
8. **An attack press moves you.** It routes to reach before the next look,
   so a deadline check must allow for the step the press itself makes.
9. **Score plans over a horizon; do not decide one step at a time.** P1's
   per-tick rules each needed a special case; P2 is built on the plan's route
   argmin, where "step back before her scan" is a cost, not a code path.

## Runner and budget

10. **Scriptrun must hold IO to tick boundaries** (inputs queued to the next
    tick, answers visible only after it), **enforce the client's
    400,000-instruction budget per wait**, and **model client-side refusals**
    (a held-item op on a hidden panel). Each gap let a scriptrun pass hide a
    live failure. That includes the API surface: scriptrun's `drive.symbol`
    took `spotanim` and the client's did not, so 16 clean scriptrun seeds
    died on the first live tick of P2's prepare.
11. **Keep two-times headroom on the budget** (`TORIRS_SCRIPTRUN_STEP_BUDGET=200000`).
    Search the arena before the fight; per tick, only look results up.

## Testing

12. **One seed proves nothing.** Sweep seeds with `--name` before calling a
    phase done, and read the tick log, not the bot's own account, for damage.

## What P2 added

13. **Model the order the way the server runs it.** An attack order touching
    its target, diagonals included, swings from where it stands. The plan
    predicted a step off her side that never happened, and the raider was
    slammed.
14. **Zero damage taken is not a finish.** Two seeds took no hits and never
    left P2: she healed as fast as she was hit. Read her heals in the tick log
    (`npc_heal` by source) next to the damage dealt.
15. **A heal window is a cost on the plan, known ahead.** Once the reds clock
    runs, the summon tick is fixed (seventh attack + 8). Swings already under
    way when she summoned healed her for 434. A term that prices "attacking
    her" on those ticks stops them a tick early.
16. **Check the kit after every phase change.** The Dawnbringer leaves with her
    P1 form, and its last holder punched for 550 ticks with the scythe still
    in the backpack.
17. **Compare the run to real teams one phase at a time.** Splitting P2 at
    the first summon showed the solver was already faster than Blert trios
    before the reds (105 ticks against 121) and 2.6 times slower after them.
    The fix was in the reds cycle only; nothing before 35% needed to change.
18. **A plan generator must offer every way to reach a target.** A bare
    attack takes the server's shortest route, and beside her that route can
    run under her. Without a "walk beside it, then attack" plan, the cheapest
    safe choice was to wait.

## What P3 added

19. **Derive every read tick from the queue it runs in.** A ball landing runs
    in the target's own player queue on tick L, before that player moves: it
    reads the target at the end of L-1, a lower pid (moved already) at the
    end of L, a higher pid at the end of L-1. One tick end per raider, not a
    window. A three-tick hold against three converging tornadoes took the
    touch on nine of sixteen seeds; a one-tick hold took none.
20. **Key a hazard on what makes it distinct, never on an id the engine
    reuses.** Every map projectile carries the same element id, so a web
    table keyed on it remembered one web per special and the raiders stood
    on the other twenty-three (184 damage a run, in the old solver and the
    new one alike, until the key became tile plus landing tick).
21. **A press the server may refuse is not a move.** An attack order on an
    invulnerable boss (she is from the webs slot on) stands the raider still,
    and a tornado at its heels stepped onto that stillness. With a chaser
    within reach, walk the tile; press the attack only when staying.
22. **Act on the predicted slot, not the confirmed one.** The webs slot is
    confirmed by the auto that fails to come, on the slot tick itself; her
    arrival reads the end of T+1. A raider walked under the centre on T-1
    was thrown four tiles, held four ticks by the throw, and touched. The
    schedule knows the slot a rotation ahead; the forbid starts from it.
23. **Nobody beside her at a scan means no tank to identify.** Her melee
    reads the tank after her own follow step, so a tank two tiles away is
    one away when she checks. Every raider under her or three or more away
    at the end of every slot-1 makes the melee impossible whoever the tank
    is: zero melees on sixteen seeds, and the facing heuristic is gone.
24. **One search, many contexts.** The rotation names the next special, so
    the context hands the planner a goal and two to four constraints; a
    C beam search over (tile, tick) with each tornado walked along the path
    finds the kite-and-return that hand-written plan shapes kept missing,
    at 3-4 million node expansions a run and no instruction-budget risk.
    Lua keeps the facts, C keeps the search; both lanes serve it.
25. **"Pulled to five" is "set to five".** The enrage writes `map_clock + 5`
    over any pending slot, later as well as sooner. A schedule that moved
    only slots beyond E+5 marked a slot due at E+4 as passed, and her first
    enraged attack found every raider beside her: one melee on four of
    sixteen seeds, nothing else wrong in the run. Read the assignment, not
    the comment above it.

## What the live lane added (2026-10-09)

26. **The two lanes read the same server; what differed was the solver's
    clock and its tiles.** The scriptrun bots never reacted early: their
    event ticks match the tick log to the tick, and the one P1 hit they took
    (her last bolt, 6 of 16 seeds) was the same hit the live seats took.
    On the live client the loop wakes about twice a server tick, so a tick
    counted from wakes ran 2x ahead and every absolute hold landed wrong;
    the npc rows' `x` is the drawn tile, a tile behind `server_x` while it
    walks; and a held op in the tick its tab was switched is refused.
    Take the tick from `api_drive.tick()`, the tiles from `server_x`, act
    the tick after the tab, and never decide twice in one tick.
27. **A standing mode is a frozen npc.** `playerface` runs in the mode
    machine before the waypoint drain, so an `npc_walk` queued beside it is
    never taken: her P3 follow stood on a tank under her, and every crab of
    every rotation stood where it spawned for 25 ticks. Face with a square
    latch, or face on the ticks you do not step.
28. **Measure the npc the way Blert measures it before porting a server's
    reading of it.** Crabs move on 45% of ticks and blow 3-4 from their
    raider at tick 14; with the tank under her she moves on 12% of attack
    ticks and 50% of the rest; her last P1 wind-up is never within 3 ticks
    of the phase change and never lands damage. Each number disagreed with
    a comment that cited NR, and each was settled by the streams.
29. **A trip is priced by its return.** The Dawnbringer trip to her side
    began with the read eight ticks off, past the horizon, and came back one
    tile short of the box. A cover term needs the point of no return: out
    of the box with more tiles to go than the ticks left can cover.

## What lane identity added (2026-10-09)

The owner's bar: "given the same seeds, the scriptrun and the live client
runner's tick log should be identical". They are now, tick for tick, from
the fight's first retype to a member's death 440 ticks later (446 of 452
ticks identical, the rest one hitpoint of regen phase, since synced by
`::synctimers`), and two live runs of the same seed are identical in every
one of their 837 ticks. What stood between the lanes, in the order found:

30. **One fence a tick.** The session closed every answered request with its
    own SERVER_TICK_END, so a client whose click drew any output saw two
    fences a tick: the old world plus the answer, then the tick's own. The
    driver woke on the first, decided on the previous tick's world, and its
    input landed a tick late; scriptrun has no sessions and only the tick's
    fence. The answer now rides the tick's flush (torirs_server_session.c).
31. **`api_drive.tick()` is the server's tick.** The client's own clock
    (world cycle / 30) gained a tick on the server every few hundred; a
    rotation slot read off that clock was a tick early live and never on
    scriptrun. A leader reads the embed world's counter, a member the
    lockstep tick its TICK frame carries, and both equal scriptrun's
    `core->tick`, so F.tick names the same tick-log row on every lane.
32. **A tab settles in its call.** The live switch mounted the panel through
    the task runner and laid it out in the frame's settle, so a press in
    the same resume found nothing; now `app_plugin_tab_select` runs the
    frame's settle to its fixed point and the P2 "tab one tick, act the
    next" shape is gone from P3 (press and act in the tab's tick).
33. **Scriptrun builds the gate's world.** It never called
    `ToriRSServer_WorldInit`: no run seed (TORIRSSERVER_RUN_NAME is read
    there), ten npcs where the live world had a thousand (so every raid npc
    took a different slot), and bots named `<name><seat>` where the gate
    names `<stem9>_p<seat>` -- and a player's random stream is keyed by its
    name. Now `--name sa` is the gate's `--name sa` on both lanes.
34. **A barrier mark counts from the next tick.** The live mark carries its
    writer's lockstep tick and is honoured a boundary later, by every seat at
    once; scriptrun's counted at once, so a party already waiting passed in
    the tick the leader marked, one tick ahead of live, and every player
    action after it sat a tick early against her unchanged clock.
35. **The clocks that run from the login tick.** Each seat logs in on its
    own tick, each lane on different ones: the prayer drain phase (the
    overhead lit before the barrier -- now after it), then the hitpoints
    regen (one hitpoint, 440 ticks in). `::synctimers` restarts hitpoints
    regen, stat drift, special energy and the prayer drain fraction; the
    test sends it to every seat at the ready barrier.
36. **The tools.** `tlcmp.py` aligns two tick logs on a row (the first
    retype, the P3 form, the start), canonicalises npc slots, ignores named
    kinds, and names the first differing ticks; `lag.py` measures walk
    order to arrival and press to prayer bit per seat; `wakematch.py`
    matches a wake's (her tile, my hitpoints) to the tick-log tick it
    perceives. Measure the lag before theorising about it: the two "lags"
    this started with were a drifting clock and a doubled fence.
37. **Three false leads, so they are not walked again.** (a) A grep filter
    that dropped lines starting with `*` hid the two `*out_x = ...`
    assignments in `ToriRSServer_SceneNaivePath`; read unfiltered, it is
    sound. (b) `tlcmp.py` labels npc slots by first appearance across ALL
    rows, so "N7 vs N2" was the ambient npcs' rows, not a slot difference:
    the raid's raw slots (1083..) are the same on both lanes. (c)
    `npc_find`'s `WorldNpcVisibleTo(active_player)` only bites for OWNED
    npcs; the raid's are not. When the visible rows agree and the lanes
    still part, read the digest row (TORIRSSERVER_TICKLOG_DIGEST: players,
    npcs, both random stream sets, instance vars, collision, world stream)
    with `dgcmp.py` before reading more code.
38. **The live `world_op` ignored the copy the script named.** scriptrun's
    `d_world_op` takes an optional element id and acts on that copy;
    the live plugin took the type alone and acted on the NEAREST copy. With
    two reds of one type on the floor, the raider who chose a red by
    position attacked the other on the live lane, the red retaliated
    against a different raider (retaliation latches on the first hit while
    `combat_target < 0`), and the lanes parted at t+422 with every logged
    row identical. Found by the digest row: the raid npcs' hidden state
    parted at t+244, and the `npc_state` change rows named the field
    (`combat_target`, `face_entity`) and the tick. The `hit_npc` row now
    carries the dealing pid so who-hit-which is in the log.
39. **The sanitizer pass is clean for the fight.** ASan + whole-server UBSan
    ran the relay end to end with the plain builds' timings: no ASan
    report; UBSan's thirteen are boot-time hash and decompression
    wraparound plus one `tiles << 16` on a negative delta in the ray caster
    (now a multiply). Memory errors are not the hidden state. A bare ASan
    `torirsserver` HANGS before main on this Mac (no shim); CLAUDE.md has
    the recipe.
40. **A player's hit on an npc landed under the wrong player.** The attack
    script arms the hit as an npc queue (`[ai_queue3,_]` →
    `~npc_default_damage` → `npc_damage`); the entry carried no player, so
    when it fired in the npc phase `srv->active_player` was whatever the
    server had bound last — the last client pumped on the live lane (pid 2,
    every hit), another on scriptrun. Retaliation latches
    `combat_target = active_player`, the hitmark's dealer and the tick
    log's dealer read the same binding, so with identical positions and
    rolls the reds took a different raider on each lane. The entry now
    records its arming player (pid + login generation) and the runner
    binds it for the run. Found by the `hit_npc` row's new dealer column:
    one pid for every hit is impossible, and names the global.
41. **Identical, measured.** Seed sa, live party against scriptrun, aligned
    on the first retype: 926 of 926 ticks of visible rows identical, and
    the digest's player state, both random stream sets and the collision
    flags never part. What still parts is only absolute-clock stamps
    (varp 5732 action delay, the instance's `tob_var_clock`, an npc's
    `born`), offset by the five ticks between the two runs' entry. The
    whole list, in the order found: one fence a tick (30), the server
    tick as the driver's clock (31), the tab settling in its call (32),
    scriptrun building the gate's world under the gate's names (33),
    barrier marks honoured a tick later (34), the login-tick clocks
    synced (35), entity rows on the server tile and animation ticks in
    server ticks, projectiles read from the packet with scriptrun's
    arithmetic, `world_op` honouring the copy the script named (38), and
    the npc queue's arming player bound when it fires (40).
42. **An npc's script runs under no player.** A script's primary player is
    seeded from `srv->active_player` when it starts, so an `[ai_timer]`
    inherited whatever the server had bound last; a `[ai_queue]` it armed
    recorded that as its hero; `npc_damage` dealt its hit under it; the
    reds retaliated against it. Seed sb parted on exactly that label at
    t+635 (pid 0 on live, pid 2 on scriptrun) with every other row equal.
    Timers now run with no player bound (the reference's
    `ScriptRunner.init(script, npc, null)`), queues with their hero or
    none, and `npc_damage` binds the script's own player or none. A timer
    that needs a player finds one with `huntall` or `p_finduid`.
43. **The assertion law, applied to the parity work itself.** A varp read
    with no player is `assert(player)` in the op, not a null check: the
    first run under player-less timers faulted at `varps[6880]` of a NULL
    player, which named `~tob_dbg_attack` (a boss timer writing a raider's
    debug varps through the engine's stale binding) and it now records on
    every raider in range through `huntall`. My own edits had four silent
    paths of the same shape (a full projectile ring returned, a world
    guard around the projectile note, a pid-range guard on the digest's
    shadows, a clamp on a stamp newer than now); all four are asserts now.
44. **An op's player is the script's, not the server's.** The op
    dispatcher gives every command the script state's active player
    (what `huntnext` and `p_finduid` set), falling back to the server's
    binding; `ToriRSServer_WorldGroundFind` read `srv->active_player->pid`
    directly, so `obj_find` inside a hunt checked visibility against the
    wrong raider and, under a player-less timer, faulted on NULL (the
    second fault of the day, the Dawnbringer sweep). It takes an explicit
    viewer now, asserted, and the obj op passes its own player. The same
    audit shows the inventory ops' bank branch still reads the global.
45. **Identical to the last column.** Seed sb on the final engine: 852 of
    852 ticks of visible rows identical between the live party and
    scriptrun, dealer labels included; every `npc_state`, `player_state`
    and `varp` change row identical; the digest's player state, both
    random stream sets and collision never part. What still parts is the
    absolute-tick stamps (varp 5732, `tob_var_clock`, `attack_clock`,
    `born`), by exactly the entry offset. Seed sa the same at 926 of 926.
46. **`obj_del` after a hunt's `obj_find`.** With the ground find seeing the
    hunted raider's private drop, `obj_del` still checked the active obj's
    visibility against the server's binding and aborted the Dawnbringer
    sweep on ten of sixteen seeds ("the active obj is not visible to this
    player"). The obj ops take the script's player now, asserted. A script
    abort is a loud contract failure, and it must not be waved through: the
    sweep ran on stale state before and nobody knew.
47. **A ball hold is a walk order.** An attack order follows her: on the
    tick she steps, the server walks the attacker after her, off the tile
    the landing reads. Seed se (t996): the "next" raider stood beside the
    anchor under an attack order, she stepped, the server moved it two off
    for the read, nobody fresh was in range and the target ate the 74. For
    the target and next roles inside their read window the order that
    keeps the tile is the tile itself; with it se bounces both balls clean.

## What the Blert crab calibration added (2026-10-09)

Blert's Normal trio rooms (`build/blert/verzik`, 25 rooms, 106 P2/P3 crabs,
116 reds) read per tick, against sixteen scriptrun seeds. The reading tool is
the per-crab timeline: for every crab, its spawn tick, the tick of each
step, the nearest raider's distance every tick, and its death tick. Aggregate
statistics (median lifetime, moved fraction) hid every one of the rules
below; the timelines gave them up at once.

48. **A wave is a coin per raider at four fixed points, not one crab per
    raider on the line to her.** Blert's trio waves are 1, 2 or 3 crabs
    (P2 13/13/8, P3 10/12/3) at the room's w, ne, n and e crab points
    (ours 6421,91 / 6441,95 / 6431,96 / 6440,91, her instance base 6400,64).
    The old placement put exactly one crab per raider between her and that
    raider, which no Blert room shows. The west and east points go to the
    raider nearest them, the north to the one left over.
49. **The crab's clock: still on +0..+2, first step on +3, one tile a tick,
    stop on the first tick it STARTS within 2 of its raider, blast on the
    last step +5, never before +10, and 29 if it never arrives.** 101 of
    101 Blert crabs that moved took their first step on +3; every crab that
    spawned within 2 of a raider still took that step; 14 of 14 that stopped
    by +5 blew on +10; 9 of 9 that never arrived blew on +29. The floor
    matters on the e point, where a raider often stands within 2 at the
    spawn: without it the crab blew on +6.
50. **`npc_setmode(none)` does not cancel a queued walk.** The reference's
    NPC_SETMODE never touches the waypoint `npc_walk` queued (the engine's
    note at SS_OP_NPC_SETMODE), so a crab that lit its fuse while its last
    step was blocked took that step a tick or two later, onto its raider's
    tile. A walk to its own tile is the stop; that is what the arrival
    branch issues now.
51. **A stuck crab is Blert's too.** Four of Blert's 43 P3 crabs never moved
    and blew on +29, every one at the n point with her body between it and
    its raider; ours do the same on the e point when she stands on it. Do
    not "fix" the stuck counter.
52. **The first purple cast is a 35% roll, not the wiki's 25%.** Blert's
    first cast lands on attack 1 in 10 of 25 rooms, attack 2 in 7, 3 in 3,
    4 in 2 (mean 3.5); at 25% ours averaged slot 6.9 over sixteen seeds. The
    crabs ride that cast, so this is the P2 crab wave's timing. The later
    casts (floor 19 attacks, then a coin) already matched: +91..+99.
53. **Reds spawn at her south-west tile −4,+1 and +4,+1**, 58 and 58 of
    Blert's 116; ours were −2,0 and +4,0.
54. **What still differs is the raiders, not the crabs.** Blert's crabs live
    a median 14 (P2) and 15 (P3) and die 2.5 tiles from the nearest raider;
    ours 11 and 10, 1.5 tiles: Blert's players walk away from a crab the
    tick it stops ("M2.4" on nearly every timeline), ours stand. Blert's
    reds live 42-44 and die on the next summon with hp left (14 of 116
    killed); ours are killed by the bots inside 7-30 ticks. Both are solver
    choices, written here so nobody reads them as content bugs.
55. **Draw every coin before any spawn.** With `~tob_verzik_add_crab` inside
    the hunt loop, the coin drawn right after a spawn came up heads 24% of
    the time against 44% after a tails: HHH once in 65 fight waves where a
    fair coin gives eight, HTH seventeen. The same loop fired from
    `::tobcrabs 1` (the player's stream) or from her queue was fair. The
    wave now decides every raider's coin and point first and adds the crabs
    after the hunt (fair: HHH 4 of 65, even conditionals). What the spawn op
    does to the next draw of her stream is not root-caused; do not put an
    `npc_add` between two rolls that must be independent.
56. **Queue id 2 is the engine's damage hook.** Every combat hit on an npc
    lands through its `[ai_queue2,...]`. A `[ai_queue2,verzik_phase2]` added
    for a probe turned every raider hit on her into a purple cast with a crab
    wave (46 purples in one P2, waves of 6) and failed every seed for an
    hour, for every session on the tree. Debug entry points go on
    `[debugproc]` only; a queue handler on a boss form is live content.
57. **A content edit costs three minutes of packs for four seconds of test**
    (measured 2026-10-09: tob_build_packs.sh 60 s, torirsserver-servpack
    124 s, one scriptrun seed 4 s at 417 ticks/s). The server refuses a stale
    pack, and another session's edit to any file under the content tree
    makes the pack stale again between a build and a run. Batch content
    edits; never a one-line change per run.
58. **The varp allocator renames what you declare.** A new `[varpNNNN_name]`
    block is rewritten by the pack build to `[varp<next>_varpNNNN_name]` from
    `pack/varp.alloc`, and again on the next build if the name still starts
    with `varp`. Declare the name without a number and let the allocator
    assign it, or do not add varps for a probe.
59. **Her P3 standing is the solver's doing, not the follow's.** Over 279 P3
    ticks of seed sa she stepped 11 times: the raiders stand at range 1 and
    step to range 0 only on her attack tick (the scan-tick dodge), and the
    follow takes no step on an attack tick, so the walk-off never fires; when
    all three stood under her on a non-attack tick (+69) she stepped out in
    three ticks. The follow itself is a sign-of-the-difference diagonal step
    (`~tob_verzik_p3_follow`), not the engine's `playerfollow` naive path,
    which is what a kiting tank would show up.
60. **Her P3 walk is a normal npc's walk, every unsuspended tick.** Blert's
    25 Normal trios, tank = the target of her last auto, special windows
    excluded, P(step) per tick: tank 2+ away 0.92 on free ticks and 0.98 on
    attack ticks (she attacks from range and keeps walking); beside her 0.01
    and 0.00; under her 0.20 and 0.37, cardinal steps 61 to 6. The content
    now calls `npc_walk` to the tank's tile (the engine's naive path:
    diagonal while both axes differ, a random cardinal when the footprints
    overlap, stopping beside) on every tick she is not suspended by the webs
    or the yellows, attack ticks included; with the tank in reach on an
    attack tick she stands (the attack is the tick's act). It used to be a
    hand-rolled sign-of-the-difference step on free ticks only, which gave
    0.15 on far ticks in the approach probe
    (test/raids/_probe_verzik_p3_far.lua). Sixteen seeds after: 0.19/0.33
    under, 0.06/0.00 beside, 0.94 far, no deaths. The tank label in the
    tick log is her face entity (the last attack target), not
    `^tob_var_vz_tank`; a diagonal step read as "under" is that proxy.
61. **Isolate from the shared content tree when another session is mid-edit.**
    A pack built from the shared tree compiles everyone's half-done files;
    on 2026-10-09 another session's dotted-hunt rewrite segfaulted my
    binary at the fight's first tick on every seed. `git -C OSRS-Content
    worktree add --detach <wt> HEAD`, apply your own diff there, build its
    packs with `TORIRSSERVER_CONTENT_DIR=<wt>/osrs239-content` (scripts 29 s,
    servpack 118 s), and run the server from a scratch root of symlinks
    whose `OSRS-Content` points at the worktree: the server resolves
    `OSRS-Content/osrs239-content` relative to its working directory.

## What Maiden's lane identity added (2026-10-09)

The Verzik proof (41) compared from the first retype on, and Verzik's start
never crosses a barrier by loc op, so Maiden found what that window never
covered. Seeds mz and mq, live party against scriptrun: 428 of 428 and 339
of 339 ticks identical with NOTHING ignored (digest, varps, npc and player
state), and both lanes click the barrier on the same absolute tick. In the
order found:

62. **A verb on one lane only is a crash on the other.** `cast_npc` was
    scriptrun-only; the live freezer's first barrage was a Lua error and the
    other seats stood idle and died. Diff the two verb sets before a live run.
63. **The live tick is the APPLIED fence's tick.** The raw counter (the
    leader's `srv->tick`, a member's TICK frame) moves a pump or more before
    that tick's fence is applied, so a level on the tick passed early and the
    next `server_tick` await was answered by the same tick's fence: every
    live seat decided a tick ahead. `PluginDrive_ServerTick` now answers the
    tick of the last fence pumped; barrier marks are stamped and honoured
    with it (as scriptrun's `core->tick`).
64. **Scriptrun pumps like the client.** One event cursor per bot, 32-event
    batches, and a newly armed await is offered the rest of its batch
    (`drive_pump_once`'s R3) -- `bot_step_once` mirrors it.
65. **The copy a verb picks is one rule.** `world_op` with no element chose
    the nearest copy by squared distance live and by Chebyshev on scriptrun,
    ties by pool order: the members crossed the barrier by different copies
    and stood a tile apart. `torirs_drive_pick.h` is the rule on both lanes.
66. **Perception comes from packets, never from asset-gated entities.** The
    live world spawns a first-seen projectile, loc or graphic after its models
    load, a tick late. Projectiles (earlier), loc changes (now:
    `App_DriveLocChangeNote`, answered from the packet until the scenery
    catches up), and a solver reads the SENT spotanim (`spotanim_sent_id`),
    never the drawn one, whose delay counts down on the frame clock.
67. **A stamp names the tick that SENT it.** Live stamped a seq / spotanim
    / face with the perceived tick (one past the tick-log row); scriptrun with
    the sending tick. `PluginDrive_ServerTickOfCycle` now counts fences (the
    first fence at or after the stamp's cycle, less one); a projectile's
    elapsed and expiry are whole server ticks from that.
68. **The driver never runs on a half-applied tick.** Packets apply over
    several frames; a level read a cheat's reply before its tick's fence.
    `App_DriveRing.batch_open` (first packet of a tick .. its fence) holds the
    pump.
69. **Answers leave with the tick.** The session answered input between
    ticks at once; as on LostCity (`processClientsIn` .. `processClientsOut`)
    and scriptrun, what handling input sends is held and released at the start
    of the next world tick (`ToriRSServer_SessionReleaseHeld`).
70. **One absolute start.** Setup and a member's process launch take a
    different number of ticks per lane, so the room's absolute clocks were 13
    apart: the test enters on a fixed tick (`t.until_tick`, after a `kitted`
    barrier), and `::synctimers` restarts the playtime minute too.
71. **A seeded run's clock is its ticks.** `date_minutes` (varp3078, the
    spellbook's clock) read the wall clock, so each lane carried the minute
    it logged in on. `ToriRSServer_WorldRealtimeMs` answers 2026-01-01 plus
    600 ms a tick when `TORIRSSERVER_RUN_NAME` seeds the run, and
    `::synctimers` restarts its 100-tick refresh. `TORIRSSERVER_TICKLOG_SEED=1`
    logs every nonzero varp / state at the log's start, so a digest that parts
    on a value that never changes in the log names it.
