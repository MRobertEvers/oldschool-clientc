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
