#ifndef ACTION_PLAN_H
#define ACTION_PLAN_H

/*
 * action_plan: the ToB v2 planner -- a beam search over MY ACTIONS (stay,
 * walk a tile, attack a target) with the server's process for each click in
 * the node (the walk queue, an attack that chases its npc, the swing timer,
 * a held op that ends a swing, a stun), scored in one currency: expected hp.
 * docs/minigames/theater_of_blood/TOB2_DESIGN.md, "PLAN: api_drive.aplan",
 * is the spec; plugin/torirs_drive_plan_lua.h (DriveActionPlanLua) binds it
 * as the drive verb `aplan` on both lanes. It sits beside collision_plan
 * (collision_map.h) and reuses its zone/pull/goal/edge terms and the
 * collision map's step tests and line of sight.
 *
 * Ticks: `now` is the tick whose END the source tile is; step k of the path
 * is my tile at the end of tick t = now + k (the click decided after seeing
 * now + k - 1, acting during now + k). Every window is in absolute ticks.
 *
 * Per step, in this order:
 *   1. held ops scheduled at t: swing_ready = max(swing_ready, t +
 *      swing_delay), hp_worst += heal, and `ends_attack` clears `inter`
 *      BEFORE the action (E4/M6), so keeping the attack is a counted click.
 *   2. the chasers step toward my end-of-(t-1) tile (the v1 tornado rule).
 *   3. the action. While t <= stunned_until (M5) the only child is a stand
 *      with no click and no swing; `inter` is kept.
 *        STAY      0 clicks when inter == -1; else a walk to my own tile (E3),
 *                  1 click, inter = -1.
 *        WALK m    the 8 one-step moves, and the 16 two-step moves when
 *                  `run`; the middle tile by the v1 rule (the straight step on
 *                  the dominant axis first, the diagonal second). inter = -1.
 *                  1 click unless the heading equals the previous step's walk
 *                  heading (v1's count).
 *        ATTACK i  1 click unless inter was already i. The target refuses the
 *                  press when t is outside [t_ok0, t_ok1]: stand still, inter
 *                  = -1, no swing (lesson 21). Else inter = i and the server's
 *                  ORDER (M4 refined, torirs_server_world.c; F0 = path[k-1],
 *                  the npc's tile while my click is handled between ticks,
 *                  F1 = path[k], its tile after phase_npcs):
 *                  PRESS (inter was not i):
 *                   1. handle_opnpc (:7019) latches the interaction, routes
 *                      toward F0 (ToriRSServer_WorldWalkToApproach, :1189)
 *                      and tries it at once (WorldProcessInteraction :2721 ->
 *                      interaction_try :2084): in reach of F0 from my tile
 *                      -> the op fires now (a swing if the timer allows), the
 *                      route is dropped, and content's p_opnpc(2) re-arm
 *                      (torirs_server_scripts.c :9858) sets combat_target.
 *                   2. the tick (:18664 clients_in, npc_events, npcs,
 *                      players): phase_player (:16603) tries again BEFORE
 *                      moving (:16713), against F1: reached -> fires, I
 *                      stand.
 *                   3. else interaction_path_to_pathing_target (:2531)
 *                      re-routes toward F1 only at the LAST waypoint
 *                      (:2544 waypoint_index > 0 returns): a route toward F0 that turns is
 *                      walked stale, a straight one is replaced by the route
 *                      toward F1. If step 1 fired, combat_target is set and
 *                      ToriRSServer_CombatPlayerApproach (:16722;
 *                      torirs_server_combat.c :2584) re-routes toward F1.
 *                   4. advance_player (:16728) walks run ? 2 : 1 tiles of
 *                      it; the post-move try (:16742) against F1 from the
 *                      end tile.
 *                  UNDER WAY (inter == i, combat_target set): step 2's try
 *                  against F1 from my tile; else CombatPlayerApproach
 *                  re-routes toward F1 every tick -- EXCEPT a ranged weapon
 *                  whose Chebyshev gap to F1 is <= range (no sight test, 0
 *                  included, in_attack_range_with :161): the route is
 *                  cleared and I stand without swinging; then step 4.
 *                  The swing goes out on the first try that reaches while t
 *                  >= swing_ready (lesson 8), from that try's tile (the swing
 *                  costs read it): value += target.value, swing_ready = t +
 *                  speed. Reach is collision_map_reached / approached
 *                  (below); routes are collision_map_route_tiles with the
 *                  entity approach, as api_drive.route(x, z, {size}).
 *   4. scoring at t on the tile after the move: hits, marks, watchers,
 *      chasers, zones, pulls, goal, edge (hits/marks with sample PASS read the middle tile
 *      too).
 *
 * Comparison: risk_rule (forbid hits / marks / watchers / swing costs and
 * lethal zones, require or not), then risk_hp (hp_worst <= 0 from priced
 * damage), then cost = loss + soft + clicks * click_cost + steps *
 * move_cost - value; then fewer tiles walked; fewer clicks; EARLIER clicks
 * (the lower sum of the clicks' k -- an addition to the doc's order, below);
 * insertion order.
 *
 * CHOICES where the design doc is open (each is a statement, change it here):
 *  - Reach, melee (range 1): collision_map_reached with the entity approach
 *    (shape -2, rsmod reachExclusiveRectangle, mover size 1): cardinal-
 *    adjacent to the footprint, never overlapping, and no WALL bit on MY
 *    tile's edge toward it. The footprint's own blocking (a big npc, a loc)
 *    does not matter. (The first version demanded a legal step INTO the
 *    footprint, which a blocked footprint never allows: Maiden's melee never
 *    fired, 2026-10-12 probe.) Ranged (range > 1): Chebyshev gap 1..range and
 *    collision_map_approached (line of sight, never overlapping), the server's
 *    collision_in_range_at.
 *  - The chase is the route, not a greedy step (the greedy diagonal-first
 *    step mispredicted every freezer's chase in the probe): the route goes to
 *    the entity approach for melee AND ranged (a ranged walk is the same
 *    route, stopped where it fires: the start-of-step reach test and the
 *    one-tick horizon of each step do that cut), with d_route's nearest
 *    fallback (the OSRS feature table's op_click_nearest_range). No route ->
 *    stand. Routes are cached per (from tile, footprint SW, size) within one
 *    call; result.routes counts the floods actually run.
 *  - The beam reserves up to two slots per target, kept in their sorted
 *    place: the cheapest node whose attack on it is under way (inter == i),
 *    and the one with the fewest route steps left to its reach (the cached
 *    route's length from the node's tile to the footprint at k + 1). A chase
 *    pays clicks and steps for ticks before the swing's value lands, so a
 *    purely cost-ranked beam dropped it, and the cheapest attacking node is
 *    always the chase begun a tick ago (a chase round a wall stood still the
 *    whole horizon). This is the planner's only progress heuristic.
 *  - Late hazards (the beam's myopia: a stomp at k8 that no kept node had
 *    walked away from by the time it came into view). Every hit that is
 *    certainly lethal (forbid, or dmg_max >= hp + every heal still to come)
 *    and whose window opens later inside the horizon is a PENDING hazard. Per
 *    node: to be off its rect at its t0 I need ceil(d / (run ? 2 : 1)) ticks,
 *    d the straight-line distance to the nearest tile outside it, with t0 -
 *    max(t, stunned_until) ticks to do it. When I cannot, that hit WILL charge
 *    at least (h - k0 + 1) risk. The sum is a lower bound on future risk
 *    (walls only lengthen the escape), and the beam ranks partial plans by
 *    risk + bound (A*-like; the bound is 0 at the leaf, so the final choice
 *    is the plain comparison). Lethal-tier ZONES opening later are pending
 *    too, measured by the zone's own gap band: a require zone (Verzik P1's
 *    pillar box at the bolt's read tick) needs |gap - band| ticks of running
 *    to get INTO the band, a plain lethal zone the ticks to get OUT of it.
 *    Sight on a zone is ignored (optimistic, so still a lower bound).
 *    Watchers and marks have no cheap distance and are not read. One more node is
 *    reserved per step: the one with the most slack (ticks to spare) against
 *    its tightest pending hazard, so a margin the straight line overrates
 *    still has a node that left early.
 *  - Target windows mean "the server refuses the PRESS": a fresh press
 *    outside [t_ok0, t_ok1] is refused (lesson 21) always; an attack already
 *    under way follows `refuse_under_way` (default 1, refused, the old
 *    behaviour). A refused ATTACK is reported as act "attack" with
 *    refused = 1, so the executive knows the server drops it on its own.
 *  - Swing costs are charged on the tick the swing goes out, at the
 *    tick-end tile the swing is made from; why_index encodes target and
 *    entry (target * ACTION_PLAN_SWING_COSTS_MAX + entry).
 *  - "Waiting wins ties" (Sotetseg maze, 2026-10-12) is NOT a tie in the
 *    geometry reported: a pull goal at (+2, +2) with the diagonal's row
 *    blocked at k1. Running with an end-sampled hit the plan already moves at
 *    k1 (the 2-step diagonal's middle tile is not read). Pass-sampled, or
 *    walking, stand-then-diagonal walks 2 tiles and every move-first path 3-4
 *    tiles to arrive on the SAME tick with the same pull cost at every step,
 *    so waiting is strictly cheaper by move_cost (and by "fewer tiles" at
 *    move_cost 0). No discount or "soft falls earlier" tie-break can flip
 *    that without overriding move_cost, so neither was added. A stall of
 *    hundreds of ticks means the predictor re-blocks the diagonal at k1
 *    every tick (a tornado that keeps coming) while telling the plan it clears
 *    at k2: the fix belongs in the prediction (block it for the ticks the
 *    tornado will keep blocking it) or in an explicit progress term.
 *  - swings_max: the node counts its swings per target; the count of a
 *    target with a finite swings_max is part of the dedup key (a plan that
 *    killed it is not the same state as one that did not).
 *  - path_alt: the chase (reach, route, swing, swing costs) is run against
 *    path[k-1]/path[k] and against path_alt[k-1]/path_alt[k], each scored,
 *    and the WORSE node by the plan comparison is kept (its tile, swing and
 *    charges). Hits, marks, chasers and zones are then read on that tile.
 *  - Risk is split by cause (Sotetseg q16: a lethal require stack and the hp
 *    estimate of his melee plus the ball share traded 1:1, and every seat
 *    stepped off the stack). A charge that is both forbid and hp-lethal
 *    counts once, as a rule. The lookahead's bound splits the same way:
 *    pending forbid hits and lethal zones feed the rule bound, pending
 *    hp-lethal hits the hp bound. The "most slack" reservation reads both.
 *  - Earlier clicks break a tie before insertion order. Without it a click
 *    deferred is free (it ties at the leaf and the branch that stood first
 *    was inserted first), so the plan procrastinated: after a held op that
 *    ended the attack it stood two ticks and re-pressed on the tick the swing
 *    was due, instead of re-pressing with the held op (E4).
 *  - Candidate order per parent is ATTACK 0..n-1, then STAY, then the walks:
 *    a tie between keeping on a target and standing goes to the target (so a
 *    re-press after a held op is made on the tick of the held op).
 *  - A refused ATTACK (target outside its t_ok window) is still generated:
 *    with inter == i already it costs no click and drops the attack, which is
 *    what the server does; from anything else it is a stand with a click and
 *    always loses to STAY.
 *  - A hit with sample PASS whose rect holds both the middle and the end tile
 *    charges once per tick, not twice. Marks the same.
 *  - The "stunned" in the dedup key is the stun ticks remaining, clamped to
 *    0..15 (a superset of a boolean: two nodes stunned for different lengths
 *    are not the same state).
 *  - A mark's t_mark must be >= now (asserted); t_mark == now records the
 *    source tile at the root. A mark already past is a plain hit.
 *  - Held ops apply at t == now + k for k >= 1 only. heal is not capped.
 *  - Zones keep their v1 tier: lethal adds (h - k + 1) to risk (why kind
 *    ZONE), damage adds `cost` to loss (not to hp_worst), soft to soft.
 *    Pulls, goal and edge are soft, in hp.
 *  - Chasers carry no forbid: they are lethal only through hp_worst.
 *  - hp_worst <= 0 charges risk on every hit/mark/chaser charge while it
 *    stays <= 0; `why` records the first.
 *  - result.soft is the soft terms only; move/click costs are in `cost`
 *    (with steps and clicks reported beside it).
 */

#include "engine/world_builder/collision_map.h"

enum
{
    ACTION_PLAN_H_MAX = 16,
    ACTION_PLAN_BEAM_MAX = 64,
    ACTION_PLAN_HITS_MAX = 512,
    ACTION_PLAN_MARKS_MAX = 8,
    ACTION_PLAN_TARGETS_MAX = 6,
    ACTION_PLAN_HELD_MAX = 8,
    ACTION_PLAN_CHASERS_MAX = 4,
    ACTION_PLAN_ZONES_MAX = 48,
    ACTION_PLAN_PULLS_MAX = 8,
    ACTION_PLAN_WATCHERS_MAX = 128,
    ACTION_PLAN_SWING_COSTS_MAX = 16, /* per target */
};

enum ActionPlanAct
{
    ACTION_PLAN_STAY = 0,
    ACTION_PLAN_WALK = 1,
    ACTION_PLAN_ATTACK = 2,
};

enum ActionPlanSample
{
    ACTION_PLAN_SAMPLE_END = 0,  /* the tick-end tile only (M3) */
    ACTION_PLAN_SAMPLE_PASS = 1, /* the run's middle tile too */
};

enum ActionPlanWhy
{
    ACTION_PLAN_WHY_NONE = 0,
    ACTION_PLAN_WHY_HIT = 1,
    ACTION_PLAN_WHY_MARK = 2,
    ACTION_PLAN_WHY_CHASER = 3,
    ACTION_PLAN_WHY_ZONE = 4,
    ACTION_PLAN_WHY_WATCHER = 5,
    /* why_index = target * ACTION_PLAN_SWING_COSTS_MAX + entry */
    ACTION_PLAN_WHY_SWING_COST = 6,
};

/* "A swing here is a hit on me": charged like a hit when a swing on the
 * target goes out at tick t in [t0, t1] from a tick-end tile inside the rect
 * (Xarpus HM P3 retaliating on the quadrant he faces). */
struct ActionPlanSwingCost
{
    int x0, z0, x1, z1;
    int t0, t1;
    double dmg_exp, dmg_max;
    int stun;
    int forbid;
};

/* A target: footprint SW tile at step k = 0..path_n-1 (k = 0 is now); steps
 * past path_n - 1 hold the last tile. */
struct ActionPlanTarget
{
    int size;
    int path_n; /* 1..ACTION_PLAN_H_MAX + 1 */
    int path_x[ACTION_PLAN_H_MAX + 1];
    int path_z[ACTION_PLAN_H_MAX + 1];
    /* the npc's OTHER possible step (a follow step that is random when its
     * target is under it): 0 none; else the chase is scored on both paths and
     * the WORSE outcome kept. Same layout as path. */
    int alt_n;
    int alt_x[ACTION_PLAN_H_MAX + 1];
    int alt_z[ACTION_PLAN_H_MAX + 1];
    int range;  /* 1 melee, > 1 ranged */
    int speed;  /* ticks between swings, >= 1 */
    double value;
    int t_ok0, t_ok1; /* accepts a PRESS on [t_ok0, t_ok1] */
    /* An attack already under way at a tick outside [t_ok0, t_ok1]: 1 (the
     * default) the server refuses it too -- I stand, inter = -1, no swing, no
     * click (an invulnerable target); 0 the window gates only a fresh press
     * and the attack under way chases and swings as usual (an attackable
     * target the room merely does not want pressed then). */
    int refuse_under_way;
    /* 0 unlimited; else after this many swings in the plan the target is
     * dead: no more value, an attack under way on it is dropped (stand,
     * inter = -1, refused, no click) and it is never pressed again (it no
     * longer exists, so no ATTACK child is made for it) */
    int swings_max;
    int n_swing_costs;
    struct ActionPlanSwingCost swing_costs[ACTION_PLAN_SWING_COSTS_MAX];
};

/* rect [x0..x1] x [z0..z1], window [t0, t1] on tick ends */
struct ActionPlanHit
{
    int x0, z0, x1, z1;
    int t0, t1;
    int sample;
    double dmg_exp, dmg_max;
    int stun;
    int forbid;
};

/* lands where I stood at the end of t_mark: tiles within Chebyshev r of it,
 * over [t0, t1] */
struct ActionPlanMark
{
    int t_mark;
    int r;
    int t0, t1;
    int sample;
    double dmg_exp, dmg_max;
    int stun;
    int forbid;
};

/* An npc footprint (SW tile, size; inside the map) that hits what it SEES
 * by collision_plan's near-edge rule (collision_plan_watcher_sees: under the
 * footprint, or line of sight from its near column / near row; tob_bloat.rs2
 * ~tob_bloat_sees). Read on the tick-END tile only, every tick in [t0, t1],
 * charged like an "end" hit. */
struct ActionPlanWatcher
{
    int x, z, size;
    int t0, t1;
    double dmg_exp, dmg_max;
    int stun;
    int forbid;
};

struct ActionPlanHeld
{
    int t;
    int ends_attack;
    int swing_delay;
    double heal;
};

struct ActionPlanChaser
{
    int x, z;
    double dmg_exp, dmg_max;
};

struct ActionPlanSpec
{
    int src_x, src_z; /* scene-local */
    int now;
    int h;    /* 1..ACTION_PLAN_H_MAX */
    int beam; /* 1..ACTION_PLAN_BEAM_MAX */
    int run;  /* 1: two steps a tick */
    double hp;
    int inter; /* -1 none, else a target index */
    int swing_ready;
    int stunned_until;
    double click_cost, move_cost;
    int n_targets, n_hits, n_marks, n_held, n_chasers, n_zones, n_pulls, n_watchers;
    struct ActionPlanTarget targets[ACTION_PLAN_TARGETS_MAX];
    struct ActionPlanHit hits[ACTION_PLAN_HITS_MAX];
    struct ActionPlanMark marks[ACTION_PLAN_MARKS_MAX];
    struct ActionPlanHeld held[ACTION_PLAN_HELD_MAX];
    struct ActionPlanChaser chasers[ACTION_PLAN_CHASERS_MAX];
    struct ActionPlanWatcher watchers[ACTION_PLAN_WATCHERS_MAX];
    struct CollisionPlanZone zones[ACTION_PLAN_ZONES_MAX];
    struct CollisionPlanPull pulls[ACTION_PLAN_PULLS_MAX];
    struct CollisionPlanGoal goal;
    struct CollisionPlanEdge edge;
};

struct ActionPlanResult
{
    int n; /* = spec->h; step k at index k - 1 */
    int path_x[ACTION_PLAN_H_MAX];
    int path_z[ACTION_PLAN_H_MAX];
    int mid_x[ACTION_PLAN_H_MAX]; /* -1 when the step had no middle tile */
    int mid_z[ACTION_PLAN_H_MAX];
    int act[ACTION_PLAN_H_MAX]; /* enum ActionPlanAct */
    int tgt[ACTION_PLAN_H_MAX]; /* target index for ATTACK, else -1 */
    int swing[ACTION_PLAN_H_MAX];
    /* an ATTACK the server refused (the target's window): I stood, inter = -1,
     * no click was needed -- the executive must not read it as "keep on" */
    int refused[ACTION_PLAN_H_MAX];
    double risk, loss, soft, value, hp_worst, cost;
    /* risk = risk_rule + risk_hp; each with the why of its first charge */
    double risk_rule, risk_hp;
    int whyr_kind, whyr_index, whyr_k;
    int whyh_kind, whyh_index, whyh_k;
    int steps, clicks;
    int routes; /* route floods run (the chase's cost; cached per call) */
    int why_kind, why_index, why_k; /* enum ActionPlanWhy, spec index, step */
    int expanded;
};

/* Test knob: 0 drops the late-hazard lookahead (the bound and its reserved
 * node), 1 (the default) keeps it. */
void
action_plan_lookahead_enable(int on);

/* Test knob: 0 floods every chase (no route cache), 1 (the default) caches. */
void
action_plan_route_cache_enable(int on);

/* Returns 0 and fills `out`. Contract violations (source off the map, caps,
 * a target footprint off the map, a mark before now) are asserted. */
int
action_plan(
    struct CollisionMap* cm,
    const struct ActionPlanSpec* spec,
    struct ActionPlanResult* out);

#endif
