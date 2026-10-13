/*
 * action_plan (the ToB v2 planner) on a hand-built open collision map:
 * one case per rule of TOB2_DESIGN.md "PLAN" that the planner must get right
 * before any room trusts it. `make -C src test-action-plan`.
 */
#include "test_harness.h"

#include "engine/world_builder/action_plan.h"
#include "engine/world_builder/collision_map.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int g_failures = 0;

enum
{
    NOW = 100,
};

static struct ActionPlanSpec g_spec;
static struct ActionPlanResult g_res;

static void
spec_reset(int x, int z, int h)
{
    memset(&g_spec, 0, sizeof(g_spec));
    g_spec.src_x = x;
    g_spec.src_z = z;
    g_spec.now = NOW;
    g_spec.h = h;
    g_spec.beam = 64;
    g_spec.run = 1;
    g_spec.hp = 99;
    g_spec.inter = -1;
    g_spec.swing_ready = 0;
    g_spec.stunned_until = 0;
    g_spec.click_cost = 0.05;
    g_spec.move_cost = 0.01;
}

/* a static 1x1 target at (x, z) */
static struct ActionPlanTarget*
add_target(int x, int z, int range, int speed, double value)
{
    struct ActionPlanTarget* tg = &g_spec.targets[g_spec.n_targets++];
    memset(tg, 0, sizeof(*tg));
    tg->size = 1;
    tg->path_n = 1;
    tg->path_x[0] = x;
    tg->path_z[0] = z;
    tg->range = range;
    tg->speed = speed;
    tg->value = value;
    tg->t_ok0 = -(1 << 30);
    tg->t_ok1 = 1 << 30;
    tg->refuse_under_way = 1;
    return tg;
}

static struct ActionPlanHit*
add_hit(int x0, int z0, int x1, int z1, int t0, int t1, double dmg_exp, double dmg_max)
{
    struct ActionPlanHit* hit = &g_spec.hits[g_spec.n_hits++];
    memset(hit, 0, sizeof(*hit));
    hit->x0 = x0;
    hit->z0 = z0;
    hit->x1 = x1;
    hit->z1 = z1;
    hit->t0 = t0;
    hit->t1 = t1;
    hit->sample = ACTION_PLAN_SAMPLE_END;
    hit->dmg_exp = dmg_exp;
    hit->dmg_max = dmg_max;
    return hit;
}

static void
dump(char const* name)
{
    int k;
    printf("  %s: risk %.2f loss %.2f soft %.2f value %.2f hp_worst %.1f clicks %d steps %d why %d[%d]k%d"
           " expanded %d routes %d\n   ",
           name, g_res.risk, g_res.loss, g_res.soft, g_res.value, g_res.hp_worst, g_res.clicks, g_res.steps,
           g_res.why_kind, g_res.why_index, g_res.why_k, g_res.expanded, g_res.routes);
    for( k = 0; k < g_res.n; k++ )
        printf(" k%d:(%d,%d)%s%s%s", k + 1, g_res.path_x[k], g_res.path_z[k],
               g_res.act[k] == ACTION_PLAN_ATTACK ? "A" : (g_res.act[k] == ACTION_PLAN_WALK ? "W" : "S"),
               g_res.swing[k] ? "*" : "", g_res.refused[k] ? "r" : "");
    printf("\n");
}

static int g_quiet;
static long g_total_expanded, g_total_routes;

static int
run_case(struct CollisionMap* cm, char const* name)
{
    int before = g_failures;
    action_plan(cm, &g_spec, &g_res);
    g_total_expanded += g_res.expanded;
    g_total_routes += g_res.routes;
    if( !g_quiet )
        dump(name);
    return before;
}

static void
report(char const* name, int before)
{
    if( g_quiet )
        return;
    printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", name);
}

/* (a) a hit on my tile next tick: step off */
static void
case_a(struct CollisionMap* cm)
{
    int before;
    spec_reset(20, 20, 4);
    add_hit(20, 20, 20, 20, NOW + 1, NOW + 1, 10, 10);
    before = run_case(cm, "a");
    TEST_ASSERT(!(g_res.path_x[0] == 20 && g_res.path_z[0] == 20), "a: steps off the hit tile");
    TEST_ASSERT(g_res.loss == 0.0, "a: no loss");
    TEST_ASSERT(g_res.risk == 0.0, "a: no risk");
    report("(a) stand vs hit on my tile -> steps off", before);
}

/* (b) a melee target 3 tiles east: attack now, the swing on arrival */
static void
case_b(struct CollisionMap* cm)
{
    int before;
    spec_reset(10, 10, 6);
    g_spec.run = 0;
    add_target(13, 10, 1, 4, 5.0);
    before = run_case(cm, "b walk");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "b walk: path[1] is ATTACK");
    TEST_ASSERT(g_res.tgt[0] == 0, "b walk: path[1] targets 0");
    TEST_ASSERT(g_res.path_x[0] == 11 && g_res.path_z[0] == 10, "b walk: k1 at (11,10)");
    TEST_ASSERT(!g_res.swing[0], "b walk: no swing at k1 (not in reach)");
    TEST_ASSERT(g_res.path_x[1] == 12 && g_res.path_z[1] == 10, "b walk: k2 at (12,10)");
    TEST_ASSERT(g_res.swing[1], "b walk: the swing at k2, on arrival");
    TEST_ASSERT(g_res.clicks == 1, "b walk: one click");

    spec_reset(10, 10, 6);
    add_target(13, 10, 1, 4, 5.0);
    run_case(cm, "b run");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "b run: path[1] is ATTACK");
    TEST_ASSERT(g_res.path_x[0] == 12 && g_res.path_z[0] == 10, "b run: k1 at (12,10)");
    TEST_ASSERT(g_res.mid_x[0] == 11 && g_res.mid_z[0] == 10, "b run: the middle tile (11,10)");
    TEST_ASSERT(g_res.swing[0], "b run: the swing at k1, on arrival");
    report("(b) melee target 3 away -> ATTACK, swing on arrival", before);
}

/* (c) the swing timer: speed 4 swings at k, k + 4 only */
static void
case_c(struct CollisionMap* cm)
{
    int before, k;
    spec_reset(12, 10, 10);
    g_spec.swing_ready = NOW + 1;
    add_target(13, 10, 1, 4, 5.0);
    before = run_case(cm, "c");
    for( k = 0; k < 10; k++ )
    {
        int want = k == 0 || k == 4 || k == 8;
        TEST_ASSERT(g_res.swing[k] == want, "c: swings at k1, k5, k9 only");
    }
    TEST_ASSERT(g_res.value == 15.0, "c: three swings' value");
    TEST_ASSERT(g_res.clicks == 1, "c: one click keeps the attack");
    report("(c) swing timer speed 4", before);
}

/* (d) a held op that ends the attack at now + 1: re-click, swing delayed */
static void
case_d(struct CollisionMap* cm)
{
    int before, k, first = -1, att = -1;
    spec_reset(12, 10, 6);
    g_spec.inter = 0;
    g_spec.swing_ready = NOW + 1;
    add_target(13, 10, 1, 4, 5.0);
    g_spec.n_held = 1;
    g_spec.held[0].t = NOW + 1;
    g_spec.held[0].ends_attack = 1;
    g_spec.held[0].swing_delay = 2;
    g_spec.held[0].heal = 20;
    before = run_case(cm, "d");
    for( k = 0; k < g_res.n; k++ )
    {
        if( first < 0 && g_res.swing[k] )
            first = k;
        if( att < 0 && g_res.act[k] == ACTION_PLAN_ATTACK )
            att = k;
    }
    TEST_ASSERT(first == 2, "d: the first swing at k3 (now + 1 + swing_delay)");
    TEST_ASSERT(att == 0, "d: the ATTACK re-click on the held op's tick (k1)");
    TEST_ASSERT(g_res.clicks == 1, "d: the re-click is counted");
    TEST_ASSERT(g_res.hp_worst == 119.0, "d: the heal raised hp_worst");
    /* without the held op the same spec swings at k1 with no click */
    g_spec.n_held = 0;
    run_case(cm, "d no held");
    TEST_ASSERT(g_res.swing[0], "d control: swing at k1");
    TEST_ASSERT(g_res.clicks == 0, "d control: no click while attacking");
    report("(d) held op ends attack -> re-click, swing delayed", before);
}

/* (e) the same hit is lethal at 10 hp and worth taking at 99 */
static void
case_e(struct CollisionMap* cm)
{
    int before;
    struct ActionPlanTarget* tg;
    spec_reset(12, 10, 4);
    g_spec.hp = 10;
    g_spec.swing_ready = NOW + 1;
    tg = add_target(13, 10, 1, 4, 20.0);
    tg->t_ok0 = NOW + 1;
    tg->t_ok1 = NOW + 1;
    add_hit(12, 9, 14, 11, NOW + 1, NOW + 1, 5, 15);
    before = run_case(cm, "e hp10");
    TEST_ASSERT(g_res.risk == 0.0, "e hp10: no risk");
    TEST_ASSERT(!g_res.swing[0], "e hp10: gives up the swing");
    TEST_ASSERT(!(g_res.path_x[0] >= 12 && g_res.path_x[0] <= 14 && g_res.path_z[0] >= 9 && g_res.path_z[0] <= 11),
                "e hp10: k1 off the hit");
    g_spec.hp = 99;
    run_case(cm, "e hp99");
    TEST_ASSERT(g_res.risk == 0.0, "e hp99: no risk");
    TEST_ASSERT(g_res.swing[0], "e hp99: takes the hit for the swing");
    TEST_ASSERT(g_res.loss == 5.0, "e hp99: loss is dmg_exp");
    TEST_ASSERT(g_res.hp_worst == 84.0, "e hp99: hp_worst ran down by dmg_max");
    report("(e) hp-dependent lethality", before);
}

/* (f) a mark at now + 1 lands on now + 3..now + 4 where I stood */
static void
case_f(struct CollisionMap* cm)
{
    int before;
    spec_reset(20, 20, 6);
    g_spec.n_marks = 1;
    g_spec.marks[0].t_mark = NOW + 1;
    g_spec.marks[0].r = 0;
    g_spec.marks[0].t0 = NOW + 3;
    g_spec.marks[0].t1 = NOW + 4;
    g_spec.marks[0].sample = ACTION_PLAN_SAMPLE_END;
    g_spec.marks[0].dmg_exp = 10;
    g_spec.marks[0].dmg_max = 10;
    g_spec.n_pulls = 1;
    g_spec.pulls[0].x = 20;
    g_spec.pulls[0].z = 20;
    g_spec.pulls[0].size = 1;
    g_spec.pulls[0].t0 = -1;
    g_spec.pulls[0].t1 = 1 << 30;
    g_spec.pulls[0].weight = 1.0;
    before = run_case(cm, "f");
    TEST_ASSERT(g_res.loss == 0.0, "f: no loss");
    TEST_ASSERT(!(g_res.path_x[2] == g_res.path_x[0] && g_res.path_z[2] == g_res.path_z[0]),
                "f: off the marked tile at k3");
    TEST_ASSERT(!(g_res.path_x[3] == g_res.path_x[0] && g_res.path_z[3] == g_res.path_z[0]),
                "f: off the marked tile at k4");
    report("(f) reactive mark", before);
}

/* (g) a target that refuses swings until t_ok0 */
static void
case_g(struct CollisionMap* cm)
{
    int before, k;
    struct ActionPlanTarget* tg;
    spec_reset(12, 10, 6);
    tg = add_target(13, 10, 1, 4, 5.0);
    tg->t_ok0 = NOW + 3;
    before = run_case(cm, "g");
    for( k = 0; k < 2; k++ )
    {
        TEST_ASSERT(g_res.act[k] != ACTION_PLAN_ATTACK, "g: no ATTACK before t_ok0");
        TEST_ASSERT(!g_res.swing[k], "g: no swing before t_ok0");
    }
    TEST_ASSERT(g_res.act[2] == ACTION_PLAN_ATTACK, "g: ATTACK at t_ok0");
    TEST_ASSERT(g_res.swing[2], "g: the swing at t_ok0");
    report("(g) target refuses swings until t_ok0", before);
}

/* (h) a forbid hit is never taken, whatever the swing is worth */
static void
case_h(struct CollisionMap* cm)
{
    int before, k;
    struct ActionPlanHit* hit;
    spec_reset(19, 20, 6);
    add_target(20, 20, 1, 4, 1000.0);
    hit = add_hit(19, 19, 21, 21, NOW + 1, NOW + 6, 0, 0);
    hit->forbid = 1;
    before = run_case(cm, "h");
    TEST_ASSERT(g_res.risk == 0.0, "h: no risk");
    TEST_ASSERT(g_res.value == 0.0, "h: no swing taken");
    for( k = 0; k < g_res.n; k++ )
        TEST_ASSERT(!(g_res.path_x[k] >= 19 && g_res.path_x[k] <= 21 && g_res.path_z[k] >= 19 && g_res.path_z[k] <= 21),
                    "h: never on the forbidden rect");
    report("(h) forbid hit never taken", before);
}

/* (i) stunned: no move, no swing until stunned_until has passed; a stun hit
 * extends it */
static void
case_i(struct CollisionMap* cm)
{
    int before;
    spec_reset(12, 10, 6);
    g_spec.stunned_until = NOW + 2;
    add_target(13, 10, 1, 4, 5.0);
    before = run_case(cm, "i");
    TEST_ASSERT(!g_res.swing[0] && !g_res.swing[1], "i: no swing while stunned");
    TEST_ASSERT(g_res.swing[2], "i: the swing on the first free tick");
    TEST_ASSERT(g_res.path_x[0] == 12 && g_res.path_x[1] == 12, "i: no move while stunned");
    report("(i) stun", before);
}

/* (j) a chaser walks at me: step out of its path */
static void
case_j(struct CollisionMap* cm)
{
    int before;
    spec_reset(20, 20, 6);
    g_spec.hp = 10;
    g_spec.n_chasers = 1;
    g_spec.chasers[0].x = 23;
    g_spec.chasers[0].z = 20;
    g_spec.chasers[0].dmg_exp = 8;
    g_spec.chasers[0].dmg_max = 20;
    before = run_case(cm, "j");
    TEST_ASSERT(g_res.risk == 0.0, "j: the chaser never touches");
    TEST_ASSERT(g_res.loss == 0.0, "j: no loss");
    report("(j) chaser", before);
}

static void
block_tile(struct CollisionMap* cm, int x, int z)
{
    cm->flags[collision_map_index_at(cm, x, z)] |= COLL_FLAG_LOC;
}

/* the route the server walks to an entity's reach (the planner's model) */
static int
expected_route(struct CollisionMap* cm, int x, int z, int fx, int fz, int size, int* rx, int* rz, int cap)
{
    struct CollisionApproach ap;
    collision_approach_from_shape(-2, 0, size, size, 0, 1, &ap);
    return collision_map_route_tiles(cm, x, z, fx, fz, &ap, NULL, rx, rz, cap, NULL, NULL, NULL);
}

/* (k) melee reach to a BLOCKED footprint (a big npc, a loc): reachable from
 * beside it; a wall on the edge between blocks it */
static void
case_k(struct CollisionMap* cm)
{
    int before, k, x, z, swung = 0;
    struct ActionPlanTarget* tg;
    collision_map_reset(cm);
    for( x = 20; x <= 22; x++ )
        for( z = 20; z <= 22; z++ )
            block_tile(cm, x, z);
    spec_reset(19, 21, 6);
    g_spec.swing_ready = NOW + 1;
    tg = add_target(20, 20, 1, 4, 5.0);
    tg->size = 3;
    before = run_case(cm, "k blocked");
    TEST_ASSERT(g_res.path_x[0] == 19 && g_res.path_z[0] == 21, "k blocked: stands beside the footprint");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "k blocked: attacks");
    TEST_ASSERT(g_res.swing[0], "k blocked: the melee swing at k1");

    cm->flags[collision_map_index_at(cm, 19, 21)] |= COLL_FLAG_WALL_EAST;
    cm->flags[collision_map_index_at(cm, 20, 21)] |= COLL_FLAG_WALL_WEST;
    run_case(cm, "k wall");
    for( k = 0; k < g_res.n; k++ )
    {
        if( g_res.swing[k] )
        {
            swung = 1;
            TEST_ASSERT(!(g_res.path_x[k] == 19 && g_res.path_z[k] == 21), "k wall: no swing through the wall");
        }
    }
    TEST_ASSERT(!(g_res.path_x[0] == 19 && g_res.path_z[0] == 21 && g_res.swing[0]), "k wall: not from (19,21)");
    TEST_ASSERT(swung, "k wall: swings from another side");
    collision_map_reset(cm);
    report("(k) melee reach: blocked footprint yes, wall no", before);
}

/* (l) a ranged chase in open ground walks the server's route */
static void
case_l(struct CollisionMap* cm)
{
    static int rx[COLLISION_SIZE * COLLISION_SIZE], rz[COLLISION_SIZE * COLLISION_SIZE];
    int before, n, i;
    collision_map_reset(cm);
    spec_reset(30, 24, 4);
    add_target(20, 20, 5, 4, 5.0);
    n = expected_route(cm, 30, 24, 20, 20, 1, rx, rz, COLLISION_SIZE * COLLISION_SIZE);
    if( !g_quiet )
    {
        printf("  l route:");
        for( i = 0; i < n && i < 8; i++ )
            printf(" (%d,%d)", rx[i], rz[i]);
        printf("\n");
    }
    before = run_case(cm, "l");
    TEST_ASSERT(n >= 4, "l: a route exists");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "l: path[1] is ATTACK");
    TEST_ASSERT(g_res.mid_x[0] == rx[0] && g_res.mid_z[0] == rz[0], "l: k1 middle = route tile 1");
    TEST_ASSERT(g_res.path_x[0] == rx[1] && g_res.path_z[0] == rz[1], "l: k1 end = route tile 2");
    /* straight first (oracle M1, the probe's freezers): W, W off (30,24) */
    TEST_ASSERT(rx[0] == 29 && rz[0] == 24 && rx[1] == 28 && rz[1] == 24, "l: the route goes straight west first");
    TEST_ASSERT(g_res.path_x[1] == rx[3] && g_res.path_z[1] == rz[3], "l: k2 end = route tile 4");
    /* in range 5 with sight at (25,y): the swing once the gap is <= 5 */
    for( i = 0; i < g_res.n; i++ )
    {
        int gx = g_res.path_x[i] - 20, gz = g_res.path_z[i] - 20;
        int gap = (gx < 0 ? -gx : gx) > (gz < 0 ? -gz : gz) ? (gx < 0 ? -gx : gx) : (gz < 0 ? -gz : gz);
        if( g_res.swing[i] )
            TEST_ASSERT(gap <= 5, "l: swings only in range");
    }
    report("(l) ranged chase follows the route", before);
}

/* (m) a melee chase round a wall */
static void
case_m(struct CollisionMap* cm)
{
    static int rx[COLLISION_SIZE * COLLISION_SIZE], rz[COLLISION_SIZE * COLLISION_SIZE];
    int before, n, z, k, swung = -1;
    collision_map_reset(cm);
    for( z = 16; z <= 24; z++ )
        block_tile(cm, 13, z);
    spec_reset(10, 20, 12);
    add_target(16, 20, 1, 4, 5.0);
    n = expected_route(cm, 10, 20, 16, 20, 1, rx, rz, COLLISION_SIZE * COLLISION_SIZE);
    before = run_case(cm, "m");
    TEST_ASSERT(n > 6, "m: the route goes round");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "m: path[1] is ATTACK");
    TEST_ASSERT(g_res.path_x[0] == rx[1] && g_res.path_z[0] == rz[1], "m: k1 = route tile 2");
    for( k = 0; k < g_res.n; k++ )
    {
        TEST_ASSERT(!(g_res.path_x[k] == 13 && g_res.path_z[k] >= 16 && g_res.path_z[k] <= 24), "m: never in the wall");
        if( swung < 0 && g_res.swing[k] )
            swung = k;
    }
    TEST_ASSERT(swung >= 0, "m: arrives and swings");
    collision_map_reset(cm);
    report("(m) melee chase round a wall", before);
}

/* (n) a watcher (Bloat's flies): a tile in its sight is avoided, the tile
 * behind a pillar is used; with no pillar every tile is seen */
static void
case_n(struct CollisionMap* cm)
{
    int before, k, z;
    struct ActionPlanWatcher* w;
    collision_map_reset(cm);
    for( z = 20; z <= 22; z++ )
        cm->flags[collision_map_index_at(cm, 17, z)] |= COLL_FLAG_LOC | COLL_FLAG_LOC_PROJ_BLOCKER;
    spec_reset(16, 24, 6);
    g_spec.hp = 10;
    g_spec.n_watchers = 1;
    w = &g_spec.watchers[0];
    memset(w, 0, sizeof(*w));
    w->x = 20;
    w->z = 20;
    w->size = 3;
    w->t0 = NOW + 2;
    w->t1 = NOW + 6;
    w->dmg_exp = 6;
    w->dmg_max = 20;
    TEST_ASSERT(collision_plan_watcher_sees(cm, 20, 20, 3, 16, 24), "n: the start tile is in sight");
    TEST_ASSERT(!collision_plan_watcher_sees(cm, 20, 20, 3, 16, 21), "n: (16,21) is behind the pillar");
    before = run_case(cm, "n pillar");
    TEST_ASSERT(g_res.risk == 0.0, "n pillar: no risk");
    TEST_ASSERT(g_res.loss == 0.0, "n pillar: no loss");
    for( k = 1; k < g_res.n; k++ )
        TEST_ASSERT(!collision_plan_watcher_sees(cm, 20, 20, 3, g_res.path_x[k], g_res.path_z[k]),
                    "n pillar: never in sight inside the window");
    TEST_ASSERT(g_res.path_x[g_res.n - 1] < 17, "n pillar: ends behind the pillar");
    w->forbid = 1;
    run_case(cm, "n forbid");
    TEST_ASSERT(g_res.risk == 0.0, "n forbid: no risk");
    collision_map_reset(cm);
    run_case(cm, "n open");
    TEST_ASSERT(g_res.risk > 0.0, "n open: nowhere to hide");
    TEST_ASSERT(g_res.why_kind == ACTION_PLAN_WHY_WATCHER, "n open: why is the watcher");
    TEST_ASSERT(g_res.why_k == 2, "n open: first charged at k2");
    report("(n) watcher: sight avoided, shadow used", before);
}

/* (p) refuse_under_way: an attack under way outside the press window */
static void
case_p(struct CollisionMap* cm)
{
    int before, k;
    struct ActionPlanTarget* tg;
    collision_map_reset(cm);
    spec_reset(12, 10, 8);
    g_spec.inter = 0;
    g_spec.swing_ready = NOW + 1;
    tg = add_target(13, 10, 1, 4, 5.0);
    tg->t_ok0 = NOW + 20; /* no fresh press inside the horizon */
    before = run_case(cm, "p refused");
    TEST_ASSERT(g_res.value == 0.0, "p refused: the attack under way is refused too (default)");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK && g_res.refused[0], "p refused: reported as refused");
    TEST_ASSERT(g_res.clicks == 0, "p refused: no click");
    tg->refuse_under_way = 0;
    run_case(cm, "p swung");
    TEST_ASSERT(g_res.swing[0] && g_res.swing[4], "p swung: the attack under way keeps swinging");
    for( k = 0; k < g_res.n; k++ )
        TEST_ASSERT(!g_res.refused[k], "p swung: nothing refused");
    TEST_ASSERT(g_res.clicks == 0, "p swung: no click");
    /* a fresh press is still refused */
    g_spec.inter = -1;
    run_case(cm, "p fresh");
    TEST_ASSERT(g_res.value == 0.0, "p fresh: a fresh press outside the window is refused");
    report("(p) refuse_under_way", before);
}

/* (q) swing costs: swings from the west are forbidden -> attacked from the east */
static void
case_q(struct CollisionMap* cm)
{
    int before, k, swings = 0;
    struct ActionPlanTarget* tg;
    collision_map_reset(cm);
    spec_reset(19, 20, 8);
    tg = add_target(20, 20, 1, 4, 5.0);
    tg->n_swing_costs = 1;
    memset(&tg->swing_costs[0], 0, sizeof(tg->swing_costs[0]));
    tg->swing_costs[0].x0 = 0;
    tg->swing_costs[0].z0 = 0;
    tg->swing_costs[0].x1 = 20;
    tg->swing_costs[0].z1 = 39;
    tg->swing_costs[0].t0 = NOW;
    tg->swing_costs[0].t1 = NOW + 50;
    tg->swing_costs[0].forbid = 1;
    before = run_case(cm, "q");
    TEST_ASSERT(g_res.risk == 0.0, "q: no risk");
    for( k = 0; k < g_res.n; k++ )
    {
        if( !g_res.swing[k] )
            continue;
        swings++;
        TEST_ASSERT(g_res.path_x[k] == 21 && g_res.path_z[k] == 20, "q: swings only from the east");
    }
    TEST_ASSERT(swings >= 1, "q: still swings");
    /* the forbid is charged when the swing is taken anyway: why names it */
    tg->swing_costs[0].x1 = 39;
    run_case(cm, "q all");
    TEST_ASSERT(g_res.value == 0.0, "q all: no swing anywhere");
    tg->swing_costs[0].forbid = 0;
    tg->swing_costs[0].dmg_exp = 1.0;
    tg->swing_costs[0].dmg_max = 1.0;
    run_case(cm, "q cheap");
    TEST_ASSERT(g_res.value > 0.0 && g_res.loss > 0.0, "q cheap: a cheap retaliation is worth the swing");
    report("(q) swing costs", before);
}

/* (r) a late hazard: a forbid over the whole area opens at k12 with a target
 * beside me; the plan swings while it can and leaves in time */
static void
case_r(struct CollisionMap* cm)
{
    int before, k, swings = 0;
    struct ActionPlanHit* hit;
    collision_map_reset(cm);
    spec_reset(19, 20, 16);
    g_spec.swing_ready = NOW + 1;
    add_target(20, 20, 1, 2, 5.0);
    hit = add_hit(4, 4, 36, 36, NOW + 12, NOW + 16, 0, 0);
    hit->forbid = 1;
    before = run_case(cm, "r");
    TEST_ASSERT(g_res.risk == 0.0, "r: leaves in time");
    for( k = 0; k < g_res.n; k++ )
        swings += g_res.swing[k];
    TEST_ASSERT(swings >= 2, "r: swings until it must leave");
    TEST_ASSERT(!(g_res.path_x[11] >= 4 && g_res.path_x[11] <= 36 && g_res.path_z[11] >= 4 && g_res.path_z[11] <= 36),
                "r: off the rect when it opens");
    if( !g_quiet )
    {
        action_plan_lookahead_enable(0);
        action_plan(cm, &g_spec, &g_res);
        dump("r WITHOUT the lookahead");
        action_plan_lookahead_enable(1);
    }
    report("(r) late hazard: swing, then leave in time", before);
}

/* (s) a lethal REQUIRE box opening at k10, 6 tiles away, a target beside me:
 * swing, then leave in time to be inside the box when it is read */
static void
case_s(struct CollisionMap* cm)
{
    int before, k, swings = 0;
    struct CollisionPlanZone* zn;
    collision_map_reset(cm);
    spec_reset(19, 20, 12);
    g_spec.run = 0;
    g_spec.swing_ready = NOW + 1;
    add_target(20, 20, 1, 2, 5.0);
    g_spec.n_zones = 1;
    zn = &g_spec.zones[0];
    memset(zn, 0, sizeof(*zn));
    zn->x0 = 11;
    zn->z0 = 19;
    zn->x1 = 13;
    zn->z1 = 21;
    zn->lo = 0;
    zn->hi = 0;
    zn->t0 = NOW + 10;
    zn->t1 = NOW + 10;
    zn->cost = 1.0;
    zn->tier = COLLISION_PLAN_LETHAL;
    zn->require = 1;
    before = run_case(cm, "s");
    TEST_ASSERT(g_res.risk == 0.0, "s: inside the box at its read");
    TEST_ASSERT(g_res.path_x[9] >= 11 && g_res.path_x[9] <= 13 && g_res.path_z[9] >= 19 && g_res.path_z[9] <= 21,
                "s: k10 inside the box");
    for( k = 0; k < g_res.n; k++ )
        swings += g_res.swing[k];
    TEST_ASSERT(swings >= 2, "s: swings before it leaves");
    action_plan_lookahead_enable(0);
    action_plan(cm, &g_spec, &g_res);
    if( !g_quiet )
        dump("s WITHOUT the lookahead");
    TEST_ASSERT(g_res.risk > 0.0, "s: without the lookahead the beam misses the box");
    action_plan_lookahead_enable(1);
    report("(s) require box later in the horizon", before);
}

/* (t) a pull goal at (+2, +2) with row +1 forbidden at k1 only: running, the
 * plan moves at step 1 (the 2-step diagonal; an end-sampled row does not
 * read its middle tile). The pass-sampled and walking variants are printed:
 * there waiting walks fewer tiles to arrive on the same tick (action_plan.h) */
static void
case_t(struct CollisionMap* cm)
{
    int before, variant;
    struct ActionPlanHit* hit;
    collision_map_reset(cm);
    for( variant = 0; variant < 3; variant++ )
    {
        spec_reset(20, 20, 6);
        g_spec.run = variant != 2;
        g_spec.n_pulls = 1;
        g_spec.pulls[0].x = 22;
        g_spec.pulls[0].z = 22;
        g_spec.pulls[0].size = 1;
        g_spec.pulls[0].t0 = -1;
        g_spec.pulls[0].t1 = 1 << 30;
        g_spec.pulls[0].weight = 1.0;
        hit = add_hit(0, 21, 39, 21, NOW + 1, NOW + 1, 0, 0);
        hit->forbid = 1;
        hit->sample = variant == 1 ? ACTION_PLAN_SAMPLE_PASS : ACTION_PLAN_SAMPLE_END;
        if( variant == 0 )
        {
            before = run_case(cm, "t run, end-sampled row");
            TEST_ASSERT(g_res.risk == 0.0, "t: no risk");
            TEST_ASSERT(g_res.path_x[0] == 22 && g_res.path_z[0] == 22, "t: moves at step 1, onto the goal");
        }
        else
            run_case(cm, variant == 1 ? "t run, pass-sampled row (waits: 2 tiles vs 4)" : "t walking (waits: 2 tiles vs 3)");
    }
    report("(t) pull goal at (+2,+2), row +1 forbidden at k1", before);
}

/* (u) swings_max 1 on a valuable target: one swing, then the second target */
static void
case_u(struct CollisionMap* cm)
{
    int before, k, on_a = 0, on_b = 0;
    struct ActionPlanTarget* a;
    collision_map_reset(cm);
    spec_reset(12, 10, 10);
    g_spec.swing_ready = NOW + 1;
    a = add_target(13, 10, 1, 4, 10.0);
    a->swings_max = 1;
    add_target(16, 10, 1, 4, 3.0);
    before = run_case(cm, "u");
    for( k = 0; k < g_res.n; k++ )
    {
        if( !g_res.swing[k] )
            continue;
        if( g_res.tgt[k] == 0 )
            on_a++;
        else
            on_b++;
    }
    TEST_ASSERT(g_res.swing[0] && g_res.tgt[0] == 0, "u: the valuable target first, at k1");
    TEST_ASSERT(on_a == 1, "u: one swing on the target with swings_max 1");
    TEST_ASSERT(on_b >= 1, "u: then the second target");
    TEST_ASSERT(g_res.value == 10.0 + 3.0 * on_b, "u: no value after the kill");
    a->swings_max = 0;
    run_case(cm, "u unlimited");
    TEST_ASSERT(g_res.value >= 30.0, "u unlimited: keeps on the valuable target");
    report("(u) swings_max", before);
}

/* a target whose footprint is (x0, z0) now and (x1, z1) from step 1 on */
static struct ActionPlanTarget*
add_mover(int x0, int z0, int x1, int z1, int range, int speed, double value)
{
    struct ActionPlanTarget* tg = add_target(x0, z0, range, speed, value);
    tg->path_n = 2;
    tg->path_x[1] = x1;
    tg->path_z[1] = z1;
    return tg;
}

/* (v) a MOVING target, the server's order (action_plan.h ATTACK) */
static void
case_v(struct CollisionMap* cm)
{
    static int rx[COLLISION_SIZE * COLLISION_SIZE], rz[COLLISION_SIZE * COLLISION_SIZE];
    int before, n0, n1, z;
    struct ActionPlanTarget* tg;

    /* v1: PRESS in reach of F0 (the npc's tile while the click is handled):
     * the op fires in the handler, from my tile, before the npc steps; then
     * combat re-routes toward F1 and I follow */
    collision_map_reset(cm);
    spec_reset(10, 10, 5); /* h 5: pressing at k1 swings at k1 and k5 */
    tg = add_mover(11, 10, 12, 10, 1, 4, 5.0);
    tg->n_swing_costs = 1;
    memset(&tg->swing_costs[0], 0, sizeof(tg->swing_costs[0]));
    tg->swing_costs[0].x0 = tg->swing_costs[0].x1 = 10;
    tg->swing_costs[0].z0 = tg->swing_costs[0].z1 = 10;
    tg->swing_costs[0].t0 = NOW;
    tg->swing_costs[0].t1 = NOW + 50;
    /* small enough that pressing now still beats walking then pressing */
    tg->swing_costs[0].dmg_exp = tg->swing_costs[0].dmg_max = 0.01;
    before = run_case(cm, "v1 press, F0 in reach");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK && g_res.swing[0], "v1: swings at k1");
    TEST_ASSERT(g_res.loss == 0.01 || g_res.loss == 0.0, "v1: no swing cost charged off (10,10)");
    TEST_ASSERT(g_res.loss > 0.0, "v1: the k1 swing went out from my start tile (10,10), F0's side");
    TEST_ASSERT(g_res.path_x[0] == 11 && g_res.path_z[0] == 10, "v1: then follows to F1's reach (11,10)");

    /* v2: PRESS out of reach of F0, the route toward F0 TURNS: walked stale
     * (path_to_pathing_target re-routes only at the last waypoint) */
    collision_map_reset(cm);
    spec_reset(10, 10, 4);
    add_mover(14, 12, 14, 7, 1, 4, 5.0);
    n0 = expected_route(cm, 10, 10, 14, 12, 1, rx, rz, COLLISION_SIZE * COLLISION_SIZE);
    {
        int f0x = rx[1], f0z = rz[1];
        n1 = expected_route(cm, 10, 10, 14, 7, 1, rx, rz, COLLISION_SIZE * COLLISION_SIZE);
        if( !g_quiet )
            printf("  v2 route->F0 k1 end (%d,%d), route->F1 k1 end (%d,%d)\n", f0x, f0z, rx[1], rz[1]);
        TEST_ASSERT(n0 >= 3 && n1 >= 3, "v2: routes exist");
        TEST_ASSERT(!(f0x == rx[1] && f0z == rz[1]), "v2: the two routes differ at k1 (fixture)");
        /* every tile but the stale route's k1 end is forbidden at k1: only
         * a press that walks the stale route (or a plain walk there) is safe */
        add_hit(0, 0, 39, f0z - 1, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
        add_hit(0, f0z + 1, 39, 39, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
        add_hit(0, f0z, f0x - 1, f0z, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
        add_hit(f0x + 1, f0z, 39, f0z, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
        run_case(cm, "v2 press, turning route to F0");
        TEST_ASSERT(g_res.risk == 0.0, "v2: no risk");
        TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "v2: presses at k1");
        TEST_ASSERT(g_res.path_x[0] == f0x && g_res.path_z[0] == f0z, "v2: walks the stale route toward F0");
    }

    /* v3: PRESS out of reach of F0, the route toward F0 is STRAIGHT: it is
     * replaced by the route toward F1 */
    collision_map_reset(cm);
    spec_reset(10, 10, 4);
    add_mover(14, 10, 14, 13, 1, 4, 5.0);
    n0 = expected_route(cm, 10, 10, 14, 10, 1, rx, rz, COLLISION_SIZE * COLLISION_SIZE);
    TEST_ASSERT(n0 == 3 && rx[2] == 13 && rz[2] == 10, "v3: the route toward F0 is straight east (fixture)");
    n1 = expected_route(cm, 10, 10, 14, 13, 1, rx, rz, COLLISION_SIZE * COLLISION_SIZE);
    run_case(cm, "v3 press, straight route to F0");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "v3: presses at k1");
    TEST_ASSERT(n1 >= 2 && g_res.path_x[0] == rx[1] && g_res.path_z[0] == rz[1], "v3: walks the route toward F1");

    /* v4: UNDER WAY, ranged, in Chebyshev range of F1 without sight: combat
     * approach clears the route -> I stand and do not swing. Every other
     * tile is forbidden at k1, so ATTACK (no click) vs STAY (a click) is the
     * choice and the attack's own outcome shows */
    collision_map_reset(cm);
    for( z = 8; z <= 12; z++ )
        cm->flags[collision_map_index_at(cm, 12, z)] |= COLL_FLAG_LOC | COLL_FLAG_LOC_PROJ_BLOCKER;
    spec_reset(10, 10, 1);
    g_spec.inter = 0;
    add_target(14, 10, 5, 4, 5.0);
    add_hit(0, 0, 39, 9, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
    add_hit(0, 11, 39, 39, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
    add_hit(0, 10, 9, 10, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
    add_hit(11, 10, 39, 10, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
    run_case(cm, "v4 under way, ranged, in range without sight");
    TEST_ASSERT(g_res.risk == 0.0, "v4: no risk");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "v4: keeps the attack (no click)");
    TEST_ASSERT(g_res.path_x[0] == 10 && g_res.path_z[0] == 10 && !g_res.swing[0], "v4: stands, no swing");

    /* v5: path_alt -- the npc either stays (path) or steps a tile east
     * (alt); following the alt walks me onto (11,10), forbidden at k1: the
     * worse outcome is kept, so the plan does not keep that attack */
    collision_map_reset(cm);
    spec_reset(10, 10, 2);
    g_spec.inter = 0;
    tg = add_mover(11, 10, 11, 10, 1, 4, 5.0);
    add_hit(11, 10, 11, 10, NOW + 1, NOW + 1, 0, 0)->forbid = 1;
    run_case(cm, "v5 no alt");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK && g_res.swing[0], "v5 no alt: swings at k1");
    tg->n_alts = 1;
    tg->alt_n[0] = 2;
    tg->alt_x[0][0] = 11;
    tg->alt_z[0][0] = 10;
    tg->alt_x[0][1] = 12;
    tg->alt_z[0][1] = 10;
    run_case(cm, "v5 alt");
    TEST_ASSERT(g_res.risk == 0.0, "v5 alt: no risk");
    TEST_ASSERT(g_res.act[0] != ACTION_PLAN_ATTACK, "v5 alt: the attack's worse outcome is refused");
    collision_map_reset(cm);
    report("(v) moving target: press vs F0, stale/straight routes, ranged stop, path_alt", before);
}

/* (w) risk by cause: a required stack tile inside a priced melee reach at hp
 * where hp_worst goes <= 0 -- the rule outranks the estimate, the plan stays */
static void
case_w(struct CollisionMap* cm)
{
    int before;
    struct CollisionPlanZone* zn;
    collision_map_reset(cm);
    spec_reset(20, 20, 4);
    g_spec.hp = 30;
    g_spec.n_zones = 1;
    zn = &g_spec.zones[0];
    memset(zn, 0, sizeof(*zn));
    zn->x0 = zn->x1 = 20;
    zn->z0 = zn->z1 = 20;
    zn->lo = 0;
    zn->hi = 0;
    zn->t0 = zn->t1 = NOW + 2;
    zn->tier = COLLISION_PLAN_LETHAL;
    zn->require = 1;
    zn->cost = 1.0;
    /* his melee slot + the ball share, priced, on the stack at the landing */
    add_hit(19, 19, 21, 21, NOW + 2, NOW + 2, 20, 35);
    before = run_case(cm, "w");
    TEST_ASSERT(g_res.path_x[1] == 20 && g_res.path_z[1] == 20, "w: on the stack at the read (k2)");
    TEST_ASSERT(g_res.risk_rule == 0.0, "w: no rule broken");
    TEST_ASSERT(g_res.risk_hp > 0.0, "w: the hp estimate is the risk taken");
    TEST_ASSERT(g_res.risk == g_res.risk_rule + g_res.risk_hp, "w: risk is the sum");
    TEST_ASSERT(g_res.whyh_kind == ACTION_PLAN_WHY_HIT && g_res.whyh_k == 2, "w: why_hp names the hit at k2");
    TEST_ASSERT(g_res.whyr_kind == ACTION_PLAN_WHY_NONE, "w: no why_rule");
    report("(w) risk by cause: the rule outranks the hp estimate", before);
}

/* (x) a FORBID chaser 2 tiles away, a valuable target beside me: never
 * touched, still swings */
static void
case_x(struct CollisionMap* cm)
{
    int before, k, swings = 0;
    collision_map_reset(cm);
    spec_reset(19, 20, 10);
    g_spec.swing_ready = NOW + 1;
    add_target(20, 20, 1, 4, 5.0);
    g_spec.n_chasers = 1;
    memset(&g_spec.chasers[0], 0, sizeof(g_spec.chasers[0]));
    g_spec.chasers[0].x = 17;
    g_spec.chasers[0].z = 20;
    g_spec.chasers[0].forbid = 1;
    before = run_case(cm, "x");
    TEST_ASSERT(g_res.risk_rule == 0.0, "x: never touched");
    TEST_ASSERT(g_res.loss == 0.0 && g_res.hp_worst == 99.0, "x: a forbid touch prices nothing anyway");
    TEST_ASSERT(g_res.swing[0], "x: swings at k1 (the chaser needs 2 ticks)");
    for( k = 0; k < g_res.n; k++ )
        swings += g_res.swing[k];
    TEST_ASSERT(swings >= 2, "x: keeps swinging when the timing allows");
    /* stunned for 3 ticks with it 2 away: the touch is certain and named */
    g_spec.stunned_until = NOW + 3;
    run_case(cm, "x stunned");
    TEST_ASSERT(g_res.risk_rule > 0.0, "x stunned: the touch cannot be avoided");
    TEST_ASSERT(g_res.whyr_kind == ACTION_PLAN_WHY_CHASER && g_res.whyr_k == 2, "x stunned: why_rule chaser[1] k2");
    TEST_ASSERT(g_res.risk_hp == 0.0, "x stunned: no hp risk");
    report("(x) forbid chaser", before);
}

/* (y) nine possible steps for the target (hold + 8); only ONE of them sends
 * my attack's chase onto the tile a forbid chaser reaches at k1: the plan
 * must not make that ATTACK */
static void
case_y(struct CollisionMap* cm)
{
    static int rx[COLLISION_SIZE * COLLISION_SIZE], rz[COLLISION_SIZE * COLLISION_SIZE];
    static const int steps[9][2] = { { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
                                     { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    int ex[9], ez[9];
    int before, a, b, pick = -1, tx = 13, tz = 10, sx = 10, sz = 10;
    struct ActionPlanTarget* tg;
    collision_map_reset(cm);
    /* where a press at k1 ends for each step: out of reach of F0 (gap 2), the
     * route toward F0 is straight east, so it is replaced by the route toward
     * F1 and I run its first 2 tiles (holding, that is (12,10): in reach, a
     * swing at k1) */
    for( a = 0; a < 9; a++ )
    {
        int n = expected_route(cm, sx, sz, tx + steps[a][0], tz + steps[a][1], 1, rx, rz,
                               COLLISION_SIZE * COLLISION_SIZE);
        int take = n < 2 ? n : 2;
        ex[a] = take > 0 ? rx[take - 1] : sx;
        ez[a] = take > 0 ? rz[take - 1] : sz;
    }
    for( a = 1; a < 9 && pick < 0; a++ )
    {
        int unique = 1;
        for( b = 0; b < 9; b++ )
            if( b != a && ex[b] == ex[a] && ez[b] == ez[a] )
                unique = 0;
        if( unique )
            pick = a;
    }
    if( !g_quiet )
    {
        printf("  y press ends:");
        for( a = 0; a < 9; a++ )
            printf(" (%+d,%+d)->(%d,%d)", steps[a][0], steps[a][1], ex[a], ez[a]);
        printf("  pick %d\n", pick);
    }
    TEST_ASSERT(pick > 0, "y: one step whose chase end is unique (fixture)");
    if( pick <= 0 )
        return;

    /* h 1: the k1 outcome alone (the chaser keeps closing on the target's
     * side after k1, which would decide a longer plan by itself) */
    spec_reset(sx, sz, 1);
    tg = add_target(tx, tz, 1, 4, 5.0);
    {
        /* the chaser lands on that end tile at the end of k1: one tile
         * further from my end-of-k0 tile, on the same side */
        int px = ex[pick], pz = ez[pick];
        g_spec.n_chasers = 1;
        memset(&g_spec.chasers[0], 0, sizeof(g_spec.chasers[0]));
        g_spec.chasers[0].x = px + (px > sx) - (px < sx);
        g_spec.chasers[0].z = pz + (pz > sz) - (pz < sz);
        g_spec.chasers[0].forbid = 1;
    }
    before = run_case(cm, "y hold only");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK && g_res.swing[0], "y hold only: presses and swings at k1");
    TEST_ASSERT(g_res.risk_rule == 0.0, "y hold only: no touch");

    tg->n_alts = 8;
    for( a = 1; a < 9; a++ )
    {
        tg->alt_n[a - 1] = 2;
        tg->alt_x[a - 1][0] = tx;
        tg->alt_z[a - 1][0] = tz;
        tg->alt_x[a - 1][1] = tx + steps[a][0];
        tg->alt_z[a - 1][1] = tz + steps[a][1];
    }
    run_case(cm, "y 9 steps");
    TEST_ASSERT(g_res.risk_rule == 0.0, "y 9 steps: no touch");
    TEST_ASSERT(g_res.act[0] != ACTION_PLAN_ATTACK, "y 9 steps: the ATTACK whose chase may end on the touch is not made");

    if( !g_quiet )
    {
        /* the cost of the alternatives: the same plan 50 times each way */
        clock_t t0;
        int r, e9 = 0, e0 = 0;
        double ms9, ms0;
        t0 = clock();
        for( r = 0; r < 50; r++ )
        {
            action_plan(cm, &g_spec, &g_res);
            e9 = g_res.expanded;
        }
        ms9 = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 50.0;
        tg->n_alts = 0;
        t0 = clock();
        for( r = 0; r < 50; r++ )
        {
            action_plan(cm, &g_spec, &g_res);
            e0 = g_res.expanded;
        }
        ms0 = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 50.0;
        printf("  y cost: 9 paths %.3f ms/plan (%d expanded), 1 path %.3f ms/plan (%d expanded), h 1\n", ms9, e9,
               ms0, e0);
        g_spec.h = 12;
        tg->n_alts = 8;
        for( a = 1; a < 9; a++ )
            tg->alt_n[a - 1] = 2;
        t0 = clock();
        for( r = 0; r < 20; r++ )
            action_plan(cm, &g_spec, &g_res);
        ms9 = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 20.0;
        e9 = g_res.expanded;
        tg->n_alts = 0;
        t0 = clock();
        for( r = 0; r < 20; r++ )
            action_plan(cm, &g_spec, &g_res);
        ms0 = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 20.0;
        printf("  y cost: 9 paths %.3f ms/plan (%d expanded), 1 path %.3f ms/plan (%d expanded), h 12\n", ms9, e9,
               ms0, g_res.expanded);
    }
    report("(y) nine target steps, one leads into a touch", before);
}

/* (z) a press at a BIG npc (Verzik P3's 7x7) from the middle of its south
 * edge: the server fires from where I stand -- no step */
static void
case_z(struct CollisionMap* cm)
{
    int before, x, z;
    struct ActionPlanTarget* tg;
    collision_map_reset(cm);
    for( x = 10; x <= 16; x++ )
        for( z = 10; z <= 16; z++ )
            block_tile(cm, x, z);
    spec_reset(13, 9, 6);
    g_spec.swing_ready = NOW + 1;
    tg = add_target(10, 10, 1, 4, 5.0);
    tg->size = 7;
    before = run_case(cm, "z big blocked");
    TEST_ASSERT(g_res.path_x[0] == 13 && g_res.path_z[0] == 9, "z: stands where it presses");
    TEST_ASSERT(g_res.act[0] == ACTION_PLAN_ATTACK, "z: attacks");
    TEST_ASSERT(g_res.swing[0], "z: the swing at k1");
    collision_map_reset(cm);
    spec_reset(13, 9, 6);
    g_spec.swing_ready = NOW + 1;
    tg = add_target(10, 10, 1, 4, 5.0);
    tg->size = 7;
    run_case(cm, "z big open");
    TEST_ASSERT(g_res.path_x[0] == 13 && g_res.path_z[0] == 9, "z open: stands where it presses");
    TEST_ASSERT(g_res.swing[0], "z open: the swing at k1");
    report("(z) a press at a 7x7 from beside it: no step", before);
}

static void
run_all(struct CollisionMap* cm)
{
    case_a(cm);
    case_b(cm);
    case_c(cm);
    case_d(cm);
    case_e(cm);
    case_f(cm);
    case_g(cm);
    case_h(cm);
    case_i(cm);
    case_j(cm);
    case_k(cm);
    case_l(cm);
    case_m(cm);
    case_n(cm);
    case_p(cm);
    case_q(cm);
    case_r(cm);
    case_s(cm);
    case_t(cm);
    case_u(cm);
    case_v(cm);
    case_w(cm);
    case_x(cm);
    case_y(cm);
    case_z(cm);
}

int
main(void)
{
    struct CollisionMap* cm = collision_map_new(40, 40);
    int pass;
    printf("== action_plan unit tests ==\n");
    run_all(cm);
    /* the chase's cost: the whole set with and without the route cache */
    g_quiet = 1;
    for( pass = 0; pass < 2; pass++ )
    {
        clock_t t0;
        int rep;
        action_plan_route_cache_enable(pass == 0);
        g_total_expanded = g_total_routes = 0;
        t0 = clock();
        for( rep = 0; rep < 10; rep++ )
            run_all(cm);
        printf("cost (%s): expanded %ld, route floods %ld, %.2f ms per pass of all cases\n",
               pass == 0 ? "cache" : "no cache", g_total_expanded / 10, g_total_routes / 10,
               (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 10.0);
    }
    action_plan_route_cache_enable(1);
    collision_map_free(cm);
    /* the same on a full 104 x 104 scene: a flood's cost scales with the map */
    cm = collision_map_new(COLLISION_SIZE, COLLISION_SIZE);
    for( pass = 0; pass < 2; pass++ )
    {
        clock_t t0;
        int rep;
        action_plan_route_cache_enable(pass == 0);
        g_total_expanded = g_total_routes = 0;
        t0 = clock();
        for( rep = 0; rep < 10; rep++ )
            run_all(cm);
        printf("cost 104x104 (%s): expanded %ld, route floods %ld, %.2f ms per pass of all cases\n",
               pass == 0 ? "cache" : "no cache", g_total_expanded / 10, g_total_routes / 10,
               (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 10.0);
    }
    action_plan_route_cache_enable(1);
    collision_map_free(cm);
    if( g_failures == 0 )
        printf("ALL TESTS PASSED\n");
    else
        printf("%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
