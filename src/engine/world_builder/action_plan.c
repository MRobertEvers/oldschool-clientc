/*
 * action_plan: the ToB v2 beam search over my actions (action_plan.h for the
 * model, TOB2_DESIGN.md "PLAN" for the spec). Modelled on collision_plan
 * (collision_map.c): static buffers, one kept beam per step, the best node
 * per state up to the beam width, the path read back through parents.
 */
#include "action_plan.h"

#include "features/features.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct ActionPlanNode
{
    int x, z;
    int mid_x, mid_z;
    int inter;
    int swing_ready;
    int stunned_until;
    double hp_worst;
    int mark_x[ACTION_PLAN_MARKS_MAX];
    int mark_z[ACTION_PLAN_MARKS_MAX];
    int cx[ACTION_PLAN_CHASERS_MAX];
    int cz[ACTION_PLAN_CHASERS_MAX];
    double risk, loss, soft, value;
    /* risk split by cause (risk = risk_rule + risk_hp): a RULE (forbid,
     * lethal zone) against an hp ESTIMATE (hp_worst <= 0 from priced damage);
     * compared rule first, then hp. The first charge of each names its why. */
    double risk_rule, risk_hp;
    int whyr_kind, whyr_index, whyr_k;
    int whyh_kind, whyh_index, whyh_k;
    int steps, clicks;
    /* the sum of the steps k on which a click was made: among equals the
     * plan that clicks EARLIER wins (action_plan.h) */
    int click_k;
    /* the last walk's heading, 0/0 after a stand or an attack (v1's click
     * count: a straight run is one click's worth) */
    int dir_x, dir_z;
    int act, tgt, swing;
    /* an attack under way: the route steps still to walk to the target's
     * reach (0 in reach), -1 when no attack; the beam's progress measure */
    int chase_left;
    int refused;
    unsigned char swings[ACTION_PLAN_TARGETS_MAX]; /* swings made per target */
    /* the late-hazard lookahead: a lower bound on the risk still to come
     * (ranked with risk), and the ticks to spare against the tightest
     * pending hazard (INT_MAX when none) */
    double bound_rule, bound_hp;
    int slack;
    int parent;
    int why_kind, why_index, why_k;
    int order;
};

enum
{
    AP_WALKS = 24,
    AP_CHILDREN = ACTION_PLAN_TARGETS_MAX + 1 + AP_WALKS,
    AP_CAND_MAX = ACTION_PLAN_BEAM_MAX * AP_CHILDREN,
    AP_HASH = 4096,
    AP_GONE = -1000, /* a chaser that has touched and faded */
    AP_CLAMP = 15,
    AP_NO_ROUTE = 1 << 20,
};

static struct ActionPlanNode g_ap_kept[ACTION_PLAN_H_MAX + 1][ACTION_PLAN_BEAM_MAX];
static struct ActionPlanNode g_ap_cand[AP_CAND_MAX];
static int g_ap_sorted[AP_CAND_MAX];
static unsigned char g_ap_reserved[AP_CAND_MAX];
static int g_ap_hash[AP_HASH];
static double g_ap_click_cost, g_ap_move_cost; /* for the qsort comparator */

/* collision_map.c's plan_step_ok (static there): one legal step. */
static int
ap_step_ok(struct CollisionMap* cm, int x, int z, int dx, int dz)
{
    int nx = x + dx, nz = z + dz;
    if( nx < 0 || nz < 0 || nx >= cm->size_x || nz >= cm->size_z )
        return 0;
    if( dx == -1 && dz == 0 )
        return collision_map_can_step_west(cm, x, z);
    if( dx == 1 && dz == 0 )
        return collision_map_can_step_east(cm, x, z);
    if( dx == 0 && dz == -1 )
        return collision_map_can_step_south(cm, x, z);
    if( dx == 0 && dz == 1 )
        return collision_map_can_step_north(cm, x, z);
    if( dx == -1 && dz == -1 )
        return collision_map_can_step_diagonal_south_west(cm, x, z);
    if( dx == 1 && dz == -1 )
        return collision_map_can_step_diagonal_south_east(cm, x, z);
    if( dx == -1 && dz == 1 )
        return collision_map_can_step_diagonal_north_west(cm, x, z);
    if( dx == 1 && dz == 1 )
        return collision_map_can_step_diagonal_north_east(cm, x, z);
    return 0;
}

static int
ap_gap_rect(int x, int z, int x0, int z0, int x1, int z1)
{
    int gx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0);
    int gz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0);
    return gx > gz ? gx : gz;
}

static int
ap_iabs(int v)
{
    return v < 0 ? -v : v;
}

/* Walk m: 1..8 one step, 9..24 two steps (collision_map.c's plan_move with
 * the stay taken out). */
static void
ap_walk(int m, int* dx, int* dz, int* steps)
{
    static const int one[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    int n = 0;
    assert(m >= 1);
    assert(m <= AP_WALKS);
    if( m <= 8 )
    {
        *dx = one[m - 1][0];
        *dz = one[m - 1][1];
        *steps = 1;
        return;
    }
    m -= 9;
    for( *dx = -2; *dx <= 2; (*dx)++ )
    {
        for( *dz = -2; *dz <= 2; (*dz)++ )
        {
            if( *dx != -2 && *dx != 2 && *dz != -2 && *dz != 2 )
                continue;
            if( n == m )
            {
                *steps = 2;
                return;
            }
            n++;
        }
    }
    assert(0 && "ap_walk: move index out of range");
}

static void
ap_target_at(const struct ActionPlanTarget* tg, int k, int* fx, int* fz)
{
    int i = k < tg->path_n - 1 ? k : tg->path_n - 1;
    *fx = tg->path_x[i];
    *fz = tg->path_z[i];
}

/* The entity approach the server routes an attack by: rsmod
 * reachExclusiveRectangle (shape -2) for a size-1 mover -- cardinal-adjacent,
 * never overlapping, and no WALL on my tile's edge toward the footprint (the
 * source tile's directional wall bit only: the footprint's own occupancy or
 * full-block flags do not matter). */
static void
ap_approach(const struct ActionPlanTarget* tg, struct CollisionApproach* ap)
{
    collision_approach_from_shape(-2, 0, tg->size, tg->size, 0, 1, ap);
}

/* Reach (action_plan.h): melee is the approach above (collision_map_reached,
 * the predicate the server's interaction uses); ranged is Chebyshev gap
 * 1..range and collision_map_approached (line of sight, never overlapping),
 * the server's ranged rule (collision_map.c collision_in_range_at). */
static int
ap_in_reach(struct CollisionMap* cm, const struct ActionPlanTarget* tg, int fx, int fz, int x, int z)
{
    if( tg->range <= 1 )
    {
        struct CollisionApproach ap;
        ap_approach(tg, &ap);
        return collision_map_reached(cm, x, z, fx, fz, &ap) != 0;
    }
    {
        int x1 = fx + tg->size - 1, z1 = fz + tg->size - 1;
        int gx = x < fx ? fx - x : (x > x1 ? x - x1 : 0);
        int gz = z < fz ? fz - z : (z > z1 ? z - z1 : 0);
        int gap = gx > gz ? gx : gz;
        if( gap < 1 || gap > tg->range )
            return 0;
        return collision_map_approached(cm, x, z, fx, fz, 1, 1, tg->size, tg->size) != 0;
    }
}

/* The route cache: the first two tiles of the server's route from a tile to
 * a footprint, per action_plan call. Keyed on (from, footprint SW, size): the
 * route does not depend on the range (a ranged attack walks the same route
 * and stops where it fires) nor on k (a target standing still shares it). */
struct ApRoute
{
    int used;
    int fx, fz, size, x, z;
    int n;   /* tiles kept, 0..2 (0: arrived or no route) */
    int len; /* the whole route's steps; AP_NO_ROUTE when there is none */
    int straight; /* one direction from the source to the end: one waypoint */
    int tx[2], tz[2];
};

enum
{
    AP_ROUTE_HASH = 8192,
    AP_ROUTE_FILL_MAX = AP_ROUTE_HASH / 2,
};

static struct ApRoute g_ap_route[AP_ROUTE_HASH];
static int g_ap_route_fill;
static int g_ap_route_floods;
static int g_ap_route_nocache; /* test knob: 1 floods every call */
static int g_ap_route_x[COLLISION_SIZE * COLLISION_SIZE];
static int g_ap_route_z[COLLISION_SIZE * COLLISION_SIZE];

static void
ap_route_flood(struct CollisionMap* cm, const struct ActionPlanTarget* tg, int fx, int fz, int x, int z, struct ApRoute* r)
{
    const struct ToriRS_FeatureTable* features = ToriRS_Features_OSRS();
    struct CollisionNearestOpts nearest = { 0 };
    struct CollisionApproach ap;
    int steps, i, used = 0, ax = fx, az = fz;
    /* d_route's options for an entity route (torirs_server_scriptrun.c), so
     * the plan walks what api_drive.route(x, z, {size}) answers */
    ap_approach(tg, &ap);
    nearest.range = features->op_click_nearest_range;
    nearest.max_dist = 100;
    nearest.rank_by_rect_distance = features->nearest_ranks_by_rect_distance;
    nearest.unbounded = 0;
    g_ap_route_floods++;
    steps = collision_map_route_tiles(
        cm, x, z, fx, fz, &ap, &nearest, g_ap_route_x, g_ap_route_z, COLLISION_SIZE * COLLISION_SIZE, &used, &ax, &az);
    r->n = 0;
    r->len = steps < 0 ? AP_NO_ROUTE : steps;
    r->straight = 1;
    for( i = 1; i < steps; i++ )
    {
        int dx0 = g_ap_route_x[0] - x, dz0 = g_ap_route_z[0] - z;
        if( g_ap_route_x[i] - g_ap_route_x[i - 1] != dx0 || g_ap_route_z[i] - g_ap_route_z[i - 1] != dz0 )
        {
            r->straight = 0;
            break;
        }
    }
    for( i = 0; i < steps && i < 2; i++ )
    {
        r->tx[i] = g_ap_route_x[i];
        r->tz[i] = g_ap_route_z[i];
        r->n++;
    }
}

static const struct ApRoute*
ap_route(struct CollisionMap* cm, const struct ActionPlanTarget* tg, int fx, int fz, int x, int z)
{
    static struct ApRoute scratch;
    unsigned h;
    if( g_ap_route_nocache || g_ap_route_fill >= AP_ROUTE_FILL_MAX )
    {
        ap_route_flood(cm, tg, fx, fz, x, z, &scratch);
        return &scratch;
    }
    h = ((unsigned)x * 73856093u ^ (unsigned)z * 19349663u ^ (unsigned)fx * 83492791u ^ (unsigned)fz * 2654435761u ^
         (unsigned)tg->size * 40503u) &
        (AP_ROUTE_HASH - 1);
    while( g_ap_route[h].used )
    {
        const struct ApRoute* r = &g_ap_route[h];
        if( r->x == x && r->z == z && r->fx == fx && r->fz == fz && r->size == tg->size )
            return r;
        h = (h + 1) & (AP_ROUTE_HASH - 1);
    }
    g_ap_route[h].used = 1;
    g_ap_route[h].x = x;
    g_ap_route[h].z = z;
    g_ap_route[h].fx = fx;
    g_ap_route[h].fz = fz;
    g_ap_route[h].size = tg->size;
    ap_route_flood(cm, tg, fx, fz, x, z, &g_ap_route[h]);
    g_ap_route_fill++;
    return &g_ap_route[h];
}

/* One step's risk: (h - k + 1), to the rule or the hp half; the first of
 * each names its why, and the first overall names `why`. */
static void
ap_risk(struct ActionPlanNode* n, const struct ActionPlanSpec* spec, int k, int rule, int kind, int index)
{
    double r = (double)(spec->h - k + 1);
    n->risk += r;
    if( rule )
    {
        n->risk_rule += r;
        if( n->whyr_kind == ACTION_PLAN_WHY_NONE )
        {
            n->whyr_kind = kind;
            n->whyr_index = index;
            n->whyr_k = k;
        }
    }
    else
    {
        n->risk_hp += r;
        if( n->whyh_kind == ACTION_PLAN_WHY_NONE )
        {
            n->whyh_kind = kind;
            n->whyh_index = index;
            n->whyh_k = k;
        }
    }
    if( n->why_kind == ACTION_PLAN_WHY_NONE )
    {
        n->why_kind = kind;
        n->why_index = index;
        n->why_k = k;
    }
}

static void
ap_charge(
    struct ActionPlanNode* n,
    const struct ActionPlanSpec* spec,
    int k,
    double dmg_exp,
    double dmg_max,
    int stun,
    int forbid,
    int kind,
    int index)
{
    int t = spec->now + k;
    n->loss += dmg_exp;
    n->hp_worst -= dmg_max;
    if( stun > 0 && t + stun > n->stunned_until )
        n->stunned_until = t + stun;
    if( forbid )
        ap_risk(n, spec, k, 1, kind, index);
    else if( n->hp_worst <= 0.0 )
        ap_risk(n, spec, k, 0, kind, index);
}

static int
ap_in_rect(int x, int z, const struct ActionPlanHit* hit)
{
    return x >= hit->x0 && x <= hit->x1 && z >= hit->z0 && z <= hit->z1;
}

static int
ap_near(int x, int z, int mx, int mz, int r)
{
    return ap_iabs(x - mx) <= r && ap_iabs(z - mz) <= r;
}

static void
ap_score(struct CollisionMap* cm, struct ActionPlanNode* n, const struct ActionPlanSpec* spec, int k)
{
    int t = spec->now + k;
    int i;
    for( i = 0; i < spec->n_hits; i++ )
    {
        const struct ActionPlanHit* hit = &spec->hits[i];
        int on;
        if( t < hit->t0 || t > hit->t1 )
            continue;
        on = ap_in_rect(n->x, n->z, hit);
        if( !on && hit->sample == ACTION_PLAN_SAMPLE_PASS && n->mid_x >= 0 )
            on = ap_in_rect(n->mid_x, n->mid_z, hit);
        if( on )
            ap_charge(n, spec, k, hit->dmg_exp, hit->dmg_max, hit->stun, hit->forbid, ACTION_PLAN_WHY_HIT, i);
    }
    for( i = 0; i < spec->n_watchers; i++ )
    {
        const struct ActionPlanWatcher* w = &spec->watchers[i];
        if( t < w->t0 || t > w->t1 )
            continue;
        /* the tick-END tile only, like an "end" hit (collision_plan reads its
         * watchers the same way) */
        if( collision_plan_watcher_sees(cm, w->x, w->z, w->size, n->x, n->z) )
            ap_charge(n, spec, k, w->dmg_exp, w->dmg_max, w->stun, w->forbid, ACTION_PLAN_WHY_WATCHER, i);
    }
    for( i = 0; i < spec->n_marks; i++ )
    {
        const struct ActionPlanMark* mk = &spec->marks[i];
        /* read before the record: a mark lands on a tile recorded earlier */
        if( n->mark_x[i] >= 0 && t >= mk->t0 && t <= mk->t1 )
        {
            int on = ap_near(n->x, n->z, n->mark_x[i], n->mark_z[i], mk->r);
            if( !on && mk->sample == ACTION_PLAN_SAMPLE_PASS && n->mid_x >= 0 )
                on = ap_near(n->mid_x, n->mid_z, n->mark_x[i], n->mark_z[i], mk->r);
            if( on )
                ap_charge(n, spec, k, mk->dmg_exp, mk->dmg_max, mk->stun, mk->forbid, ACTION_PLAN_WHY_MARK, i);
        }
        if( t == mk->t_mark )
        {
            n->mark_x[i] = n->x;
            n->mark_z[i] = n->z;
        }
    }
    for( i = 0; i < spec->n_chasers; i++ )
    {
        if( n->cx[i] != n->x || n->cz[i] != n->z )
            continue;
        /* a forbid chaser's touch is a rule broken, not damage priced */
        if( spec->chasers[i].forbid )
            ap_risk(n, spec, k, 1, ACTION_PLAN_WHY_CHASER, i);
        else
            ap_charge(
                n, spec, k, spec->chasers[i].dmg_exp, spec->chasers[i].dmg_max, 0, 0, ACTION_PLAN_WHY_CHASER, i);
    }
    for( i = 0; i < spec->n_zones; i++ )
    {
        const struct CollisionPlanZone* zn = &spec->zones[i];
        int gap, inside;
        if( t < zn->t0 || t > zn->t1 )
            continue;
        gap = ap_gap_rect(n->x, n->z, zn->x0, zn->z0, zn->x1, zn->z1);
        inside = gap >= zn->lo && gap <= zn->hi;
        if( inside && zn->sight &&
            !collision_map_line_of_sight(
                cm, n->x, n->z, zn->x0, zn->z0, 1, 1, zn->x1 - zn->x0 + 1, zn->z1 - zn->z0 + 1, 0) )
            inside = 0;
        if( zn->require ? inside : !inside )
            continue;
        if( zn->tier == COLLISION_PLAN_LETHAL )
            ap_risk(n, spec, k, 1, ACTION_PLAN_WHY_ZONE, i);
        else if( zn->tier == COLLISION_PLAN_DAMAGE )
            n->loss += zn->cost;
        else
            n->soft += zn->cost;
    }
    for( i = 0; i < spec->n_pulls; i++ )
    {
        const struct CollisionPlanPull* p = &spec->pulls[i];
        if( t < p->t0 || t > p->t1 )
            continue;
        n->soft += p->weight * ap_gap_rect(n->x, n->z, p->x, p->z, p->x + p->size - 1, p->z + p->size - 1);
    }
    if( spec->goal.present )
    {
        const struct CollisionPlanGoal* g = &spec->goal;
        int x1 = g->x + g->size - 1, z1 = g->z + g->size - 1;
        int gx = n->x < g->x ? g->x - n->x : (n->x > x1 ? n->x - x1 : 0);
        int gz = n->z < g->z ? g->z - n->z : (n->z > z1 ? n->z - z1 : 0);
        if( (gx == 1 && gz == 0) || (gx == 0 && gz == 1) )
        {
            int side = gx == 1 ? (n->x < g->x ? 0 : 2) : (n->z > z1 ? 1 : 3);
            if( g->side >= 0 && side != g->side )
                n->soft += g->off_side;
        }
        else if( gx == 0 && gz == 0 && g->under_ok )
            n->soft += g->under;
        else
            n->soft += g->pull * (double)(gx > gz ? gx : gz);
    }
    if( spec->edge.present )
    {
        const struct CollisionPlanEdge* e = &spec->edge;
        int d = n->x - e->x0;
        if( e->x1 - n->x < d )
            d = e->x1 - n->x;
        if( n->z - e->z0 < d )
            d = n->z - e->z0;
        if( e->z1 - n->z < d )
            d = e->z1 - n->z;
        if( d < e->margin )
            n->soft += e->weight * (double)(e->margin - d);
    }
}

/* The late-hazard lookahead (action_plan.h): the hits that are certainly
 * lethal and open later in the horizon, found once per call. */
static int g_ap_pending[ACTION_PLAN_HITS_MAX];
static int g_ap_pending_n;
static int g_ap_pend_zone[ACTION_PLAN_ZONES_MAX];
static int g_ap_pend_zone_n;
static int g_ap_pend_chasers; /* any forbid chaser */
static int g_ap_lookahead = 1; /* test knob */

void
action_plan_lookahead_enable(int on)
{
    g_ap_lookahead = on != 0;
}

static void
ap_pending_init(const struct ActionPlanSpec* spec)
{
    double heal = 0.0;
    int i;
    g_ap_pending_n = 0;
    g_ap_pend_zone_n = 0;
    g_ap_pend_chasers = 0;
    if( !g_ap_lookahead )
        return;
    for( i = 0; i < spec->n_chasers; i++ )
        if( spec->chasers[i].forbid )
            g_ap_pend_chasers = 1;
    /* lethal-tier zones opening later in the horizon: a REQUIRE zone must be
     * reached by its t0 (Verzik P1's pillar box at the bolt's read tick), a
     * plain one must be left by it */
    for( i = 0; i < spec->n_zones; i++ )
    {
        const struct CollisionPlanZone* zn = &spec->zones[i];
        if( zn->tier != COLLISION_PLAN_LETHAL )
            continue;
        if( zn->t0 <= spec->now + 1 || zn->t0 > spec->now + spec->h )
            continue;
        g_ap_pend_zone[g_ap_pend_zone_n++] = i;
    }
    for( i = 0; i < spec->n_held; i++ )
        if( spec->held[i].t > spec->now && spec->held[i].heal > 0.0 )
            heal += spec->held[i].heal;
    for( i = 0; i < spec->n_hits; i++ )
    {
        const struct ActionPlanHit* hit = &spec->hits[i];
        if( hit->t0 <= spec->now + 1 || hit->t0 > spec->now + spec->h )
            continue;
        if( !hit->forbid && hit->dmg_max < spec->hp + heal )
            continue;
        g_ap_pending[g_ap_pending_n++] = i;
    }
}

/* n->bound: the risk the pending hazards will certainly still charge (a node
 * that cannot be off a hazard's rect when it opens, by straight-line running);
 * n->slack: ticks to spare against the tightest one. */
static void
ap_lookahead(struct ActionPlanNode* n, const struct ActionPlanSpec* spec, int k)
{
    int t = spec->now + k;
    int per = spec->run ? 2 : 1;
    int start = n->stunned_until > t ? n->stunned_until : t;
    int i;
    n->bound_rule = 0.0;
    n->bound_hp = 0.0;
    n->slack = INT_MAX;
    for( i = 0; i < g_ap_pending_n; i++ )
    {
        const struct ActionPlanHit* hit = &spec->hits[g_ap_pending[i]];
        int d = 0, need, avail, sl;
        if( hit->t0 <= t )
            continue;
        if( n->x >= hit->x0 && n->x <= hit->x1 && n->z >= hit->z0 && n->z <= hit->z1 )
        {
            d = n->x - hit->x0;
            if( hit->x1 - n->x < d )
                d = hit->x1 - n->x;
            if( n->z - hit->z0 < d )
                d = n->z - hit->z0;
            if( hit->z1 - n->z < d )
                d = hit->z1 - n->z;
            d++;
        }
        need = (d + per - 1) / per;
        avail = hit->t0 - start;
        if( avail < 0 )
            avail = 0;
        sl = avail - need;
        if( sl < 0 )
        {
            if( hit->forbid )
                n->bound_rule += (double)(spec->h - (hit->t0 - spec->now) + 1);
            else
                n->bound_hp += (double)(spec->h - (hit->t0 - spec->now) + 1);
        }
        if( sl < n->slack )
            n->slack = sl;
    }
    for( i = 0; i < g_ap_pend_zone_n; i++ )
    {
        const struct CollisionPlanZone* zn = &spec->zones[g_ap_pend_zone[i]];
        int g, d, need, avail, sl;
        if( zn->t0 <= t )
            continue;
        /* the zone's own measure: the Chebyshev gap to its rect, against
         * its band [lo, hi]; one tile moved changes the gap by at most 1 */
        g = ap_gap_rect(n->x, n->z, zn->x0, zn->z0, zn->x1, zn->z1);
        if( zn->require )
            d = g < zn->lo ? zn->lo - g : (g > zn->hi ? g - zn->hi : 0);
        else if( g < zn->lo || g > zn->hi )
            d = 0;
        else
        {
            /* out past hi, or in below lo (only when lo > 0: the gap
             * cannot go under 0) */
            d = zn->hi - g + 1;
            if( zn->lo > 0 && g - zn->lo + 1 < d )
                d = g - zn->lo + 1;
        }
        need = (d + per - 1) / per;
        avail = zn->t0 - start;
        if( avail < 0 )
            avail = 0;
        sl = avail - need;
        if( sl < 0 )
            n->bound_rule += (double)(spec->h - (zn->t0 - spec->now) + 1);
        if( sl < n->slack )
            n->slack = sl;
    }
    /* forbid chasers: one closes 1 tile a tick on my end-of-last-tick tile,
     * so standing still it touches d ticks from now (d = Chebyshev gap). Any
     * move outruns or sidesteps it on open ground, so the only CERTAIN touch
     * is one that lands while a stun holds me: d <= stun ticks left. Slack
     * is the ticks before it could touch, less the stun. */
    if( g_ap_pend_chasers )
    {
        int stun_left = n->stunned_until > t ? n->stunned_until - t : 0;
        for( i = 0; i < spec->n_chasers; i++ )
        {
            int dx, dz, d, sl;
            if( !spec->chasers[i].forbid || n->cx[i] == AP_GONE )
                continue;
            dx = n->cx[i] - n->x;
            dz = n->cz[i] - n->z;
            dx = dx < 0 ? -dx : dx;
            dz = dz < 0 ? -dz : dz;
            d = dx > dz ? dx : dz;
            if( d < 1 )
                continue;
            if( d <= stun_left && k + d <= spec->h )
                n->bound_rule += (double)(spec->h - (k + d) + 1);
            sl = d - 1 - stun_left;
            if( sl < n->slack )
                n->slack = sl;
        }
    }
}

static double
ap_cost(const struct ActionPlanNode* n)
{
    return n->loss + n->soft + (double)n->clicks * g_ap_click_cost + (double)n->steps * g_ap_move_cost - n->value;
}

static int
ap_better(const struct ActionPlanNode* a, const struct ActionPlanNode* b)
{
    double ca, cb;
    if( a->risk_rule + a->bound_rule != b->risk_rule + b->bound_rule )
        return a->risk_rule + a->bound_rule < b->risk_rule + b->bound_rule;
    if( a->risk_hp + a->bound_hp != b->risk_hp + b->bound_hp )
        return a->risk_hp + a->bound_hp < b->risk_hp + b->bound_hp;
    ca = ap_cost(a);
    cb = ap_cost(b);
    if( ca != cb )
        return ca < cb;
    if( a->steps != b->steps )
        return a->steps < b->steps;
    if( a->clicks != b->clicks )
        return a->clicks < b->clicks;
    if( a->click_k != b->click_k )
        return a->click_k < b->click_k;
    return a->order < b->order;
}

static int
ap_cmp(const void* pa, const void* pb)
{
    const struct ActionPlanNode* a = &g_ap_cand[*(const int*)pa];
    const struct ActionPlanNode* b = &g_ap_cand[*(const int*)pb];
    if( ap_better(a, b) )
        return -1;
    if( ap_better(b, a) )
        return 1;
    return 0;
}

static int
ap_clamp(int v)
{
    return v < 0 ? 0 : (v > AP_CLAMP ? AP_CLAMP : v);
}

/* The dedup key: (x, z, inter, min(swing_ready - t, 15), stun ticks left
 * clamped the same way, the recorded mark tiles, the chasers). */
static int
ap_same_state(const struct ActionPlanNode* a, const struct ActionPlanNode* b, const struct ActionPlanSpec* spec, int t)
{
    int i;
    if( a->x != b->x || a->z != b->z || a->inter != b->inter )
        return 0;
    if( ap_clamp(a->swing_ready - t) != ap_clamp(b->swing_ready - t) )
        return 0;
    if( ap_clamp(a->stunned_until - t) != ap_clamp(b->stunned_until - t) )
        return 0;
    for( i = 0; i < spec->n_targets; i++ )
        if( spec->targets[i].swings_max > 0 && a->swings[i] != b->swings[i] )
            return 0;
    for( i = 0; i < spec->n_marks; i++ )
        if( a->mark_x[i] != b->mark_x[i] || a->mark_z[i] != b->mark_z[i] )
            return 0;
    for( i = 0; i < spec->n_chasers; i++ )
        if( a->cx[i] != b->cx[i] || a->cz[i] != b->cz[i] )
            return 0;
    return 1;
}

static unsigned
ap_hash_state(const struct ActionPlanNode* a, const struct ActionPlanSpec* spec, int t)
{
    unsigned h = (unsigned)a->x * 73856093u ^ (unsigned)a->z * 19349663u;
    int i;
    h = h * 31u + (unsigned)(a->inter + 2) * 83492791u;
    h = h * 31u + (unsigned)ap_clamp(a->swing_ready - t) * 2654435761u;
    h = h * 31u + (unsigned)ap_clamp(a->stunned_until - t) * 40503u;
    for( i = 0; i < spec->n_targets; i++ )
        if( spec->targets[i].swings_max > 0 )
            h = h * 31u + (unsigned)a->swings[i] * 2246822519u;
    for( i = 0; i < spec->n_marks; i++ )
        h = h * 31u + (unsigned)(a->mark_x[i] + 2) * 83492791u + (unsigned)(a->mark_z[i] + 2) * 19349663u;
    for( i = 0; i < spec->n_chasers; i++ )
        h = h * 31u + (unsigned)(a->cx[i] + 2000) * 83492791u + (unsigned)(a->cz[i] + 2000) * 2654435761u;
    return h & (AP_HASH - 1);
}

static void
ap_target_alt_at(const struct ActionPlanTarget* tg, int a, int k, int* fx, int* fz)
{
    int i;
    assert(a >= 0);
    assert(a < tg->n_alts);
    assert(tg->alt_n[a] >= 1);
    i = k < tg->alt_n[a] - 1 ? k : tg->alt_n[a] - 1;
    *fx = tg->alt_x[a][i];
    *fz = tg->alt_z[a][i];
}

/*
 * ATTACK on target c at step k, the server's order (action_plan.h "ATTACK"):
 * F0 = the footprint the npc stands on while my click is handled (path[k-1]),
 * F1 = where it stands after phase_npcs (path[k]). `n` arrives as the step's
 * base state with the click counted and inter = c; this fills the tile, the
 * middle tile, steps, the swing (value, timer, count, swing costs) and
 * chase_left. `alt` (-1 = path) names the alternative path whose next
 * footprint the next step's route is measured to.
 */
static void
ap_attack(
    struct CollisionMap* cm,
    const struct ActionPlanSpec* spec,
    const struct ActionPlanTarget* tg,
    int c,
    int k,
    int press,
    int f0x,
    int f0z,
    int f1x,
    int f1z,
    int alt,
    struct ActionPlanNode* n)
{
    int t = spec->now + k;
    int sx = n->x, sz = n->z;
    int fired = 0, fire_x = sx, fire_z = sz;
    int moved = 0;

    n->mid_x = n->mid_z = -1;
    /* the press only latches and routes toward F0 (handle_opnpc resolves
     * nothing between ticks); phase_player's pre-move try is the first, against
     * F1 from my tile */
    if( ap_in_reach(cm, tg, f1x, f1z, sx, sz) )
        fired = 1;
    else
    {
        /* combat_target is set once an Attack op has fired (p_opnpc(2)'s
         * re-arm): ToriRSServer_CombatPlayerApproach then stops a weapon IN
         * RANGE -- Chebyshev gap <= range, no sight test, 0 included -- and
         * re-routes toward F1 otherwise */
        int combat = !press;
        int gap = ap_gap_rect(sx, sz, f1x, f1z, f1x + tg->size - 1, f1z + tg->size - 1);
        if( !(combat && tg->range > 1 && gap <= tg->range) )
        {
            const struct ApRoute* r;
            int take = spec->run ? 2 : 1;
            if( press )
            {
                /* the handler routed toward F0; interaction_path_to_pathing_
                 * target re-routes toward F1 only at the last waypoint, i.e.
                 * when that route is one straight run */
                r = ap_route(cm, tg, f0x, f0z, sx, sz);
                if( r->straight )
                    r = ap_route(cm, tg, f1x, f1z, sx, sz);
            }
            else
                r = ap_route(cm, tg, f1x, f1z, sx, sz);
            if( take > r->n )
                take = r->n;
            if( take > 0 )
            {
                n->x = r->tx[take - 1];
                n->z = r->tz[take - 1];
                if( take == 2 )
                {
                    n->mid_x = r->tx[0];
                    n->mid_z = r->tz[0];
                }
                n->steps += take;
                moved = 1;
            }
            /* the post-move try */
            if( ap_in_reach(cm, tg, f1x, f1z, n->x, n->z) )
            {
                fired = 1;
                fire_x = n->x;
                fire_z = n->z;
            }
        }
    }
    (void)moved;
    if( fired && t >= n->swing_ready )
    {
        int j;
        n->swing = 1;
        n->value += tg->value;
        if( n->swings[c] < 255 )
            n->swings[c]++;
        n->swing_ready = t + tg->speed;
        for( j = 0; j < tg->n_swing_costs; j++ )
        {
            const struct ActionPlanSwingCost* sc = &tg->swing_costs[j];
            if( t < sc->t0 || t > sc->t1 )
                continue;
            if( fire_x < sc->x0 || fire_x > sc->x1 || fire_z < sc->z0 || fire_z > sc->z1 )
                continue;
            ap_charge(n, spec, k, sc->dmg_exp, sc->dmg_max, sc->stun, sc->forbid, ACTION_PLAN_WHY_SWING_COST,
                      c * ACTION_PLAN_SWING_COSTS_MAX + j);
        }
    }
    if( ap_in_reach(cm, tg, f1x, f1z, n->x, n->z) )
        n->chase_left = 0;
    else
    {
        /* the same route the next step's chase reads (cached) */
        int nfx, nfz;
        if( alt >= 0 )
            ap_target_alt_at(tg, alt, k + 1, &nfx, &nfz);
        else
            ap_target_at(tg, k + 1, &nfx, &nfz);
        n->chase_left = ap_route(cm, tg, nfx, nfz, n->x, n->z)->len;
    }
}

/* A new candidate from the step's base state (held ops applied, chasers
 * stepped); the caller sets the action's fields, then scores it. */
static struct ActionPlanNode*
ap_child(const struct ActionPlanNode* base, int parent, int* cand_n)
{
    struct ActionPlanNode* n;
    assert(*cand_n < AP_CAND_MAX);
    n = &g_ap_cand[*cand_n];
    *n = *base;
    n->parent = parent;
    n->mid_x = -1;
    n->mid_z = -1;
    n->act = ACTION_PLAN_STAY;
    n->tgt = -1;
    n->swing = 0;
    n->chase_left = -1;
    n->refused = 0;
    n->bound_rule = 0.0;
    n->bound_hp = 0.0;
    n->slack = INT_MAX;
    n->order = *cand_n;
    (*cand_n)++;
    return n;
}

void
action_plan_route_cache_enable(int on)
{
    g_ap_route_nocache = !on;
}

int
action_plan(
    struct CollisionMap* cm,
    const struct ActionPlanSpec* spec,
    struct ActionPlanResult* out)
{
    int k, i, m, c, kept_n, best;
    assert(cm);
    assert(spec);
    assert(out);
    assert(spec->h >= 1);
    assert(spec->h <= ACTION_PLAN_H_MAX);
    assert(spec->beam >= 1);
    assert(spec->beam <= ACTION_PLAN_BEAM_MAX);
    assert(spec->n_targets >= 0);
    assert(spec->n_targets <= ACTION_PLAN_TARGETS_MAX);
    assert(spec->n_hits >= 0);
    assert(spec->n_hits <= ACTION_PLAN_HITS_MAX);
    assert(spec->n_marks >= 0);
    assert(spec->n_marks <= ACTION_PLAN_MARKS_MAX);
    assert(spec->n_held >= 0);
    assert(spec->n_held <= ACTION_PLAN_HELD_MAX);
    assert(spec->n_chasers >= 0);
    assert(spec->n_chasers <= ACTION_PLAN_CHASERS_MAX);
    assert(spec->n_zones >= 0);
    assert(spec->n_zones <= ACTION_PLAN_ZONES_MAX);
    assert(spec->n_pulls >= 0);
    assert(spec->n_pulls <= ACTION_PLAN_PULLS_MAX);
    assert(spec->n_watchers >= 0);
    assert(spec->n_watchers <= ACTION_PLAN_WATCHERS_MAX);
    for( i = 0; i < spec->n_watchers; i++ )
    {
        /* every tile a watcher casts from is inside the map */
        assert(spec->watchers[i].size >= 1);
        assert(spec->watchers[i].x >= 0);
        assert(spec->watchers[i].z >= 0);
        assert(spec->watchers[i].x + spec->watchers[i].size <= cm->size_x);
        assert(spec->watchers[i].z + spec->watchers[i].size <= cm->size_z);
    }
    assert(spec->inter >= -1);
    assert(spec->inter < spec->n_targets);
    assert(spec->src_x >= 0);
    assert(spec->src_x < cm->size_x);
    assert(spec->src_z >= 0);
    assert(spec->src_z < cm->size_z);
    for( i = 0; i < spec->n_targets; i++ )
    {
        const struct ActionPlanTarget* tg = &spec->targets[i];
        int p;
        assert(tg->size >= 1);
        assert(tg->speed >= 1);
        assert(tg->range >= 1);
        assert(tg->path_n >= 1);
        assert(tg->path_n <= ACTION_PLAN_H_MAX + 1);
        assert(tg->swings_max >= 0);
        assert(tg->n_swing_costs >= 0);
        assert(tg->n_swing_costs <= ACTION_PLAN_SWING_COSTS_MAX);
        assert(tg->n_alts >= 0);
        assert(tg->n_alts <= ACTION_PLAN_ALTS_MAX);
        for( c = 0; c < tg->n_alts; c++ )
        {
            assert(tg->alt_n[c] >= 1);
            assert(tg->alt_n[c] <= ACTION_PLAN_H_MAX + 1);
            for( p = 0; p < tg->alt_n[c]; p++ )
            {
                assert(tg->alt_x[c][p] >= 0);
                assert(tg->alt_z[c][p] >= 0);
                assert(tg->alt_x[c][p] + tg->size <= cm->size_x);
                assert(tg->alt_z[c][p] + tg->size <= cm->size_z);
            }
        }
        for( p = 0; p < tg->path_n; p++ )
        {
            /* every footprint tile a reach test reads is inside the map */
            assert(tg->path_x[p] >= 0);
            assert(tg->path_z[p] >= 0);
            assert(tg->path_x[p] + tg->size <= cm->size_x);
            assert(tg->path_z[p] + tg->size <= cm->size_z);
        }
    }
    for( i = 0; i < spec->n_marks; i++ )
    {
        assert(spec->marks[i].t_mark >= spec->now);
        assert(spec->marks[i].r >= 0);
    }

    memset(g_ap_route, 0, sizeof(g_ap_route));
    g_ap_route_fill = 0;
    g_ap_route_floods = 0;
    ap_pending_init(spec);
    g_ap_click_cost = spec->click_cost;
    g_ap_move_cost = spec->move_cost;
    memset(out, 0, sizeof(*out));
    {
        struct ActionPlanNode* root = &g_ap_kept[0][0];
        memset(root, 0, sizeof(*root));
        root->x = spec->src_x;
        root->z = spec->src_z;
        root->mid_x = root->mid_z = -1;
        root->inter = spec->inter;
        root->swing_ready = spec->swing_ready;
        root->stunned_until = spec->stunned_until;
        root->hp_worst = spec->hp;
        root->parent = -1;
        root->tgt = -1;
        for( i = 0; i < spec->n_marks; i++ )
        {
            int at_now = spec->marks[i].t_mark == spec->now;
            root->mark_x[i] = at_now ? spec->src_x : -1;
            root->mark_z[i] = at_now ? spec->src_z : -1;
        }
        for( i = 0; i < spec->n_chasers; i++ )
        {
            root->cx[i] = spec->chasers[i].x;
            root->cz[i] = spec->chasers[i].z;
        }
    }
    kept_n = 1;
    for( k = 1; k <= spec->h; k++ )
    {
        int t = spec->now + k;
        int cand_n = 0;
        for( i = 0; i < kept_n; i++ )
        {
            const struct ActionPlanNode* from = &g_ap_kept[k - 1][i];
            struct ActionPlanNode base = *from;
            struct ActionPlanNode* n;
            int h;

            /* 1. held ops at t, BEFORE the action (E4/M6) */
            for( h = 0; h < spec->n_held; h++ )
            {
                const struct ActionPlanHeld* hd = &spec->held[h];
                if( hd->t != t )
                    continue;
                if( t + hd->swing_delay > base.swing_ready )
                    base.swing_ready = t + hd->swing_delay;
                base.hp_worst += hd->heal;
                if( hd->ends_attack )
                    base.inter = -1;
            }
            /* 2. the chasers step toward my end-of-(t-1) tile; one standing
             * on it has touched and fades (collision_plan's rule) */
            for( c = 0; c < spec->n_chasers; c++ )
            {
                int cx = from->cx[c], cz = from->cz[c];
                if( cx == AP_GONE )
                    ;
                else if( cx == from->x && cz == from->z )
                    cx = cz = AP_GONE;
                else
                {
                    cx += from->x > cx ? 1 : (from->x < cx ? -1 : 0);
                    cz += from->z > cz ? 1 : (from->z < cz ? -1 : 0);
                }
                base.cx[c] = cx;
                base.cz[c] = cz;
            }

            /* 3. the action */
            if( t <= base.stunned_until )
            {
                /* M5: no move, no swing; a press would be refused */
                n = ap_child(&base, i, &cand_n);
                n->dir_x = n->dir_z = 0;
                ap_score(cm, n, spec, k);
                continue;
            }
            for( c = 0; c < spec->n_targets; c++ )
            {
                const struct ActionPlanTarget* tg = &spec->targets[c];
                int dead = tg->swings_max > 0 && base.swings[c] >= tg->swings_max;
                if( dead )
                {
                    /* it no longer exists: never pressed; an attack under way
                     * on it is dropped by the server, no click */
                    if( base.inter != c )
                        continue;
                    n = ap_child(&base, i, &cand_n);
                    n->act = ACTION_PLAN_ATTACK;
                    n->tgt = c;
                    n->dir_x = n->dir_z = 0;
                    n->inter = -1;
                    n->refused = 1;
                    ap_score(cm, n, spec, k);
                    continue;
                }
                n = ap_child(&base, i, &cand_n);
                n->act = ACTION_PLAN_ATTACK;
                n->tgt = c;
                n->dir_x = n->dir_z = 0;
                if( base.inter != c )
                {
                    n->clicks++;
                    n->click_k += k;
                }
                if( (t < tg->t_ok0 || t > tg->t_ok1) && (base.inter != c || tg->refuse_under_way) )
                {
                    /* lesson 21: the press is refused, I stand; an attack
                     * under way too unless the target says it is still
                     * swung (refuse_under_way = 0) */
                    n->inter = -1;
                    n->refused = 1;
                }
                else
                {
                    struct ActionPlanNode pre;
                    int f0x, f0z, f1x, f1z;
                    n->inter = c;
                    pre = *n;
                    ap_target_at(tg, k - 1, &f0x, &f0z);
                    ap_target_at(tg, k, &f1x, &f1z);
                    ap_attack(cm, spec, tg, c, k, base.inter != c, f0x, f0z, f1x, f1z, -1, n);
                    ap_score(cm, n, spec, k);
                    for( h = 0; h < tg->n_alts; h++ )
                    {
                        /* the npc's other possible steps: keep the WORST
                         * outcome (hits and chasers read its tile) */
                        struct ActionPlanNode alt = pre;
                        ap_target_alt_at(tg, h, k - 1, &f0x, &f0z);
                        ap_target_alt_at(tg, h, k, &f1x, &f1z);
                        ap_attack(cm, spec, tg, c, k, base.inter != c, f0x, f0z, f1x, f1z, h, &alt);
                        ap_score(cm, &alt, spec, k);
                        if( ap_better(n, &alt) )
                            *n = alt;
                    }
                    continue;
                }
                ap_score(cm, n, spec, k);
            }
            /* STAY: a walk to my own tile when an attack is under way (E3) */
            n = ap_child(&base, i, &cand_n);
            n->dir_x = n->dir_z = 0;
            if( base.inter != -1 )
            {
                n->clicks++;
                n->click_k += k;
                n->inter = -1;
            }
            ap_score(cm, n, spec, k);
            /* WALK: click the tick's end tile (E6) */
            for( m = 1; m <= (spec->run ? AP_WALKS : 8); m++ )
            {
                int dx, dz, steps, mx = -1, mz = -1, ndx, ndz;
                ap_walk(m, &dx, &dz, &steps);
                if( steps == 1 )
                {
                    if( !ap_step_ok(cm, base.x, base.z, dx, dz) )
                        continue;
                }
                else
                {
                    int ax = ap_iabs(dx), az = ap_iabs(dz);
                    int d1x, d1z;
                    if( ax == 2 && az == 2 )
                    {
                        d1x = dx / 2;
                        d1z = dz / 2;
                    }
                    else if( ax == 2 )
                    {
                        d1x = dx / 2;
                        d1z = 0;
                    }
                    else
                    {
                        d1x = 0;
                        d1z = dz / 2;
                    }
                    if( !ap_step_ok(cm, base.x, base.z, d1x, d1z) )
                        continue;
                    mx = base.x + d1x;
                    mz = base.z + d1z;
                    if( !ap_step_ok(cm, mx, mz, dx - d1x, dz - d1z) )
                        continue;
                }
                n = ap_child(&base, i, &cand_n);
                n->act = ACTION_PLAN_WALK;
                n->x = base.x + dx;
                n->z = base.z + dz;
                n->mid_x = mx;
                n->mid_z = mz;
                n->inter = -1;
                n->steps += steps;
                ndx = (dx > 0) - (dx < 0);
                ndz = (dz > 0) - (dz < 0);
                if( ndx != base.dir_x || ndz != base.dir_z )
                {
                    n->clicks++;
                    n->click_k += k;
                }
                n->dir_x = ndx;
                n->dir_z = ndz;
                ap_score(cm, n, spec, k);
            }
        }
        out->expanded += cand_n;
        assert(cand_n > 0);
        if( g_ap_pending_n > 0 || g_ap_pend_zone_n > 0 || g_ap_pend_chasers )
            for( i = 0; i < cand_n; i++ )
                ap_lookahead(&g_ap_cand[i], spec, k);
        for( i = 0; i < cand_n; i++ )
            g_ap_sorted[i] = i;
        qsort(g_ap_sorted, (size_t)cand_n, sizeof(int), ap_cmp);
        /* the best per state, up to the beam width -- with slots RESERVED,
         * per target, for the best node whose attack on it is under way and
         * for the one with the fewest route steps left to its reach. A chase
         * pays clicks and steps for ticks before its swing's value lands, so
         * a plain cost-ranked beam fills with cheap stands and walks, and the
         * cheapest attacking node is always the chase begun a tick ago: the
         * far end of a chase round a wall was dropped every step and the plan
         * stood still for the whole horizon. Reserved nodes keep their sorted
         * place. */
        {
            int best_cost[ACTION_PLAN_TARGETS_MAX], best_left[ACTION_PLAN_TARGETS_MAX];
            int best_slack = -1;
            int pending = 0;
            for( c = 0; c < ACTION_PLAN_TARGETS_MAX; c++ )
                best_cost[c] = best_left[c] = -1;
            for( i = 0; i < cand_n; i++ )
            {
                const struct ActionPlanNode* n = &g_ap_cand[g_ap_sorted[i]];
                g_ap_reserved[g_ap_sorted[i]] = 0;
                if( n->slack != INT_MAX && (best_slack < 0 || n->slack > g_ap_cand[best_slack].slack) )
                    best_slack = g_ap_sorted[i];
                if( n->inter < 0 )
                    continue;
                if( best_cost[n->inter] < 0 )
                    best_cost[n->inter] = g_ap_sorted[i];
                if( n->chase_left >= 0 &&
                    (best_left[n->inter] < 0 || n->chase_left < g_ap_cand[best_left[n->inter]].chase_left) )
                    best_left[n->inter] = g_ap_sorted[i];
            }
            for( c = 0; c < spec->n_targets; c++ )
            {
                if( best_cost[c] >= 0 && pending < spec->beam && !g_ap_reserved[best_cost[c]] )
                {
                    g_ap_reserved[best_cost[c]] = 1;
                    pending++;
                }
                if( best_left[c] >= 0 && pending < spec->beam && !g_ap_reserved[best_left[c]] )
                {
                    g_ap_reserved[best_left[c]] = 1;
                    pending++;
                }
            }
            /* the node with the most ticks to spare against a late hazard */
            if( best_slack >= 0 && pending < spec->beam && !g_ap_reserved[best_slack] )
            {
                g_ap_reserved[best_slack] = 1;
                pending++;
            }
            for( i = 0; i < AP_HASH; i++ )
                g_ap_hash[i] = -1;
            kept_n = 0;
            for( i = 0; i < cand_n && kept_n < spec->beam; i++ )
            {
                const struct ActionPlanNode* n = &g_ap_cand[g_ap_sorted[i]];
                int reserved = g_ap_reserved[g_ap_sorted[i]];
                unsigned hh;
                int dup = 0;
                if( !reserved && kept_n + pending >= spec->beam )
                    continue;
                hh = ap_hash_state(n, spec, t);
                while( g_ap_hash[hh] >= 0 )
                {
                    if( ap_same_state(&g_ap_kept[k][g_ap_hash[hh]], n, spec, t) )
                    {
                        dup = 1;
                        break;
                    }
                    hh = (hh + 1) & (AP_HASH - 1);
                }
                if( reserved )
                    pending--;
                if( dup )
                    continue;
                g_ap_hash[hh] = kept_n;
                g_ap_kept[k][kept_n] = *n;
                kept_n++;
            }
        }
    }
    /* the best leaf, and its path back to the root */
    best = 0;
    for( i = 1; i < kept_n; i++ )
        if( ap_better(&g_ap_kept[spec->h][i], &g_ap_kept[spec->h][best]) )
            best = i;
    {
        const struct ActionPlanNode* leaf = &g_ap_kept[spec->h][best];
        out->n = spec->h;
        out->risk = leaf->risk;
        out->loss = leaf->loss;
        out->soft = leaf->soft;
        out->value = leaf->value;
        out->hp_worst = leaf->hp_worst;
        out->cost = ap_cost(leaf);
        out->steps = leaf->steps;
        out->clicks = leaf->clicks;
        out->risk_rule = leaf->risk_rule;
        out->risk_hp = leaf->risk_hp;
        out->whyr_kind = leaf->whyr_kind;
        out->whyr_index = leaf->whyr_index;
        out->whyr_k = leaf->whyr_k;
        out->whyh_kind = leaf->whyh_kind;
        out->whyh_index = leaf->whyh_index;
        out->whyh_k = leaf->whyh_k;
        out->why_kind = leaf->why_kind;
        out->why_index = leaf->why_index;
        out->why_k = leaf->why_k;
    }
    out->routes = g_ap_route_floods;
    for( k = spec->h; k >= 1; k-- )
    {
        const struct ActionPlanNode* n = &g_ap_kept[k][best];
        out->path_x[k - 1] = n->x;
        out->path_z[k - 1] = n->z;
        out->mid_x[k - 1] = n->mid_x;
        out->mid_z[k - 1] = n->mid_z;
        out->act[k - 1] = n->act;
        out->tgt[k - 1] = n->tgt;
        out->swing[k - 1] = n->swing;
        out->refused[k - 1] = n->refused;
        best = n->parent;
    }
    return 0;
}

/* ======================================================== MEET RANK
 * (action_plan.h). Mirrors tob2_verzik_p3.lua VZ.ball_meet_rank (v22) cell
 * for cell: the padded window, the floods, the levels, the sort. */

enum
{
    MR_W_MAX = 2 * MEET_RANK_R_MAX + 1,
    MR_WP_MAX = MR_W_MAX + 2,
    MR_CELLS = MR_WP_MAX * MR_WP_MAX,
};

static unsigned char g_mr_free[MR_CELLS], g_mr_under[MR_CELLS];
static int g_mr_dt[MR_CELLS], g_mr_dn[MR_CELLS];
static int g_mr_q[MR_CELLS];

static int
mr_gap(int x, int z, int fx, int fz, int n)
{
    int gx = x < fx ? fx - x : (x > fx + n - 1 ? x - (fx + n - 1) : 0);
    int gz = z < fz ? fz - z : (z > fz + n - 1 ? z - (fz + n - 1) : 0);
    return gx > gz ? gx : gz;
}

/* 0 acceptable, else the first failing test: 1 edge, 2 her, 3 web, 4 crab */
static int
mr_bad(const struct MeetRankSpec* s, int x, int z, int her_gap)
{
    int i;
    if( x < s->ax0 + 1 || x > s->ax1 - 1 || z < s->az0 + 1 || z > s->az1 - 1 )
        return 1;
    if( s->has_her && mr_gap(x, z, s->her_x, s->her_z, s->her_size) < her_gap )
        return 2;
    for( i = 0; i < s->n_webs; i++ )
        if( s->web_x[i] == x && s->web_z[i] == z )
            return 3;
    for( i = 0; i < s->n_crabs; i++ )
        if( mr_gap(x, z, s->crab_x[i], s->crab_z[i], 2) <= 3 )
            return 4;
    return 0;
}

/* floor division (Lua's //) */
static int
mr_fdiv(int a, int b)
{
    int q = a / b;
    if( (a % b != 0) && ((a < 0) != (b < 0)) )
        q--;
    return q;
}

static int
mr_flood(int fx, int fz, int ox, int oz, int w, int wp, int* dist)
{
    static const int OFF_DX[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    static const int OFF_DZ[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    int off[8];
    int px = fx - ox, pz = fz - oz, s0, h = 0, n = 0, k, reached = 0;
    for( k = 0; k < 8; k++ )
        off[k] = OFF_DX[k] * wp + OFF_DZ[k];
    for( k = 0; k < wp * wp; k++ )
        dist[k] = -1;
    if( px < 1 || px > w || pz < 1 || pz > w )
        return 0;
    s0 = px * wp + pz;
    dist[s0] = 0;
    g_mr_q[n++] = s0;
    while( h < n )
    {
        int c = g_mr_q[h++];
        int d = dist[c] + 1;
        int inside = g_mr_under[c];
        reached++;
        for( k = 0; k < 8; k++ )
        {
            int i = c + off[k];
            if( (g_mr_free[i] || (inside && g_mr_under[i])) && dist[i] < 0 )
            {
                dist[i] = d;
                g_mr_q[n++] = i;
            }
        }
    }
    return reached;
}

static int
mr_cmp(const void* a, const void* b)
{
    const struct MeetRankTile* p = (const struct MeetRankTile*)a;
    const struct MeetRankTile* q = (const struct MeetRankTile*)b;
    if( p->k1 != q->k1 )
        return p->k1 < q->k1 ? -1 : 1;
    if( p->k2 != q->k2 )
        return p->k2 < q->k2 ? -1 : 1;
    if( p->x != q->x )
        return p->x < q->x ? -1 : 1;
    if( p->z != q->z )
        return p->z < q->z ? -1 : 1;
    return 0;
}

int
meet_rank(const struct MeetRankSpec* s, struct MeetRankTile* out, int* n_out, int* cells)
{
    static const int OFF_DX[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    static const int OFF_DZ[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    int w, wp, mx, mz, ox, oz, px, pz, i, k, best = 99, n = 0, work = 0;
    assert(s);
    assert(out);
    assert(n_out);
    assert(s->r >= 1);
    assert(s->r <= MEET_RANK_R_MAX);
    assert(s->ticks >= 1);
    assert(s->max_level >= 1);
    assert(s->max_level <= 4);
    assert(s->n_webs >= 0);
    assert(s->n_webs <= MEET_RANK_WEBS_MAX);
    assert(s->n_crabs >= 0);
    assert(s->n_crabs <= MEET_RANK_CRABS_MAX);
    assert(!s->has_her || s->her_size >= 1);
    w = 2 * s->r + 1;
    wp = w + 2;
    mx = s->t_x;
    mz = s->t_z;
    if( s->has_n )
    {
        mx = mr_fdiv(s->t_x + s->n_x, 2);
        mz = mr_fdiv(s->t_z + s->n_z, 2);
    }
    ox = mx - s->r - 1;
    oz = mz - s->r - 1;
    memset(g_mr_free, 0, sizeof(g_mr_free));
    memset(g_mr_under, 0, sizeof(g_mr_under));
    for( px = 1; px <= w; px++ )
    {
        int wx = px + ox;
        int colx = wx >= s->ax0 && wx <= s->ax1;
        int underx = s->has_her && wx >= s->her_x && wx <= s->her_x + s->her_size - 1;
        for( pz = 1; pz <= w; pz++ )
        {
            int wz = pz + oz;
            if( colx && wz >= s->az0 && wz <= s->az1 )
            {
                if( underx && wz >= s->her_z && wz <= s->her_z + s->her_size - 1 )
                    g_mr_under[px * wp + pz] = 1;
                else
                    g_mr_free[px * wp + pz] = 1;
            }
        }
    }
    work += mr_flood(s->t_x, s->t_z, ox, oz, w, wp, g_mr_dt);
    if( s->has_n )
        work += mr_flood(s->n_x, s->n_z, ox, oz, w, wp, g_mr_dn);
    /* the levels: one pass, the best level's tiles kept */
    for( i = 0; i < wp * wp; i++ )
    {
        int dt = g_mr_dt[i], dn, need, lvl = 0, r3;
        if( dt < 0 )
            continue;
        dn = 0;
        if( s->has_n )
        {
            dn = g_mr_dn[i];
            for( k = 0; k < 8; k++ )
            {
                int j = i + OFF_DX[k] * wp + OFF_DZ[k];
                int v;
                if( j < 0 || j >= wp * wp )
                    continue;
                v = g_mr_dn[j];
                if( v >= 0 && (dn < 0 || v < dn) )
                    dn = v;
            }
            if( dn < 0 )
                continue;
        }
        {
            int a = (dt + 1) / 2, b = (dn + 1) / 2;
            need = a > b ? a : b;
        }
        {
            int x = i / wp + ox, z = i % wp + oz;
            r3 = mr_bad(s, x, z, 3);
            if( r3 == 0 )
                lvl = need <= s->ticks - 1 ? 1 : (need <= s->ticks ? 2 : 3);
            else if( r3 == 2 && need <= s->ticks && mr_bad(s, x, z, 2) == 0 )
                lvl = 4;
            if( lvl == 0 || lvl > s->max_level )
                continue;
            if( lvl < best )
            {
                best = lvl;
                n = 0;
            }
            if( lvl == best )
            {
                assert(n < MEET_RANK_OUT_MAX);
                out[n].x = x;
                out[n].z = z;
                out[n].k1 = dt > dn ? dt : dn;
                out[n].k2 = dt + dn;
                n++;
            }
        }
    }
    qsort(out, (size_t)n, sizeof(out[0]), mr_cmp);
    *n_out = n;
    if( cells )
        *cells = work;
    return best <= 4 ? best : 0;
}

/* ================================================== LUA'S TABLE.SORT
 * (action_plan.h lua_sort): 3rd/lua/ltablib.c auxsort and partition,
 * operation for operation, on an index array (1-based as the Lua's). */

static int* g_ls_a;
static int (*g_ls_lt)(int, int, const void*);
static const void* g_ls_ud;
static int g_ls_det;

#define LS_A(i) g_ls_a[(i) - 1]

static int
ls_lt(int x, int y)
{
    return g_ls_lt(x, y, g_ls_ud);
}

static void
ls_swap(unsigned i, unsigned j)
{
    int t = LS_A(i);
    LS_A(i) = LS_A(j);
    LS_A(j) = t;
}

static unsigned
ls_partition(unsigned lo, unsigned up, int pv)
{
    unsigned i = lo, j = up - 1;
    for( ;; )
    {
        while( ls_lt(LS_A(++i), pv) )
            assert(i != up - 1);
        while( ls_lt(pv, LS_A(--j)) )
            assert(j >= i);
        if( j < i )
        {
            /* a[up - 1] = a[i], a[i] = P */
            LS_A(up - 1) = LS_A(i);
            LS_A(i) = pv;
            return i;
        }
        ls_swap(i, j);
    }
}

static void
ls_aux(unsigned lo, unsigned up)
{
    while( lo < up )
    {
        unsigned p, n;
        int pv;
        if( ls_lt(LS_A(up), LS_A(lo)) )
            ls_swap(lo, up);
        if( up - lo == 1 )
            return;
        p = (lo + up) / 2; /* rnd stays 0 (a randomization is flagged below) */
        if( ls_lt(LS_A(p), LS_A(lo)) )
            ls_swap(p, lo);
        else if( ls_lt(LS_A(up), LS_A(p)) )
            ls_swap(p, up);
        if( up - lo == 2 )
            return;
        pv = LS_A(p);
        LS_A(p) = LS_A(up - 1);
        LS_A(up - 1) = pv;
        p = ls_partition(lo, up, pv);
        if( p - lo < up - p )
        {
            ls_aux(lo, p - 1);
            n = p - lo;
            lo = p + 1;
        }
        else
        {
            ls_aux(p + 1, up);
            n = up - p;
            up = p - 1;
        }
        if( lo < up && (up - lo) / 128 > n && up - lo >= 100u )
            g_ls_det = 0; /* the Lua would randomize its next pivot here */
    }
}

int
lua_sort(int* a, int n, int (*lt)(int, int, const void*), const void* ud)
{
    assert(a || n == 0);
    assert(lt);
    if( n <= 1 )
        return 1;
    g_ls_a = a;
    g_ls_lt = lt;
    g_ls_ud = ud;
    g_ls_det = 1;
    ls_aux(1, (unsigned)n);
    return g_ls_det;
}

/* ============================================================ YELLOWS */

static int
yl_cheb(int ax, int az, int bx, int bz)
{
    int dx = ax > bx ? ax - bx : bx - ax, dz = az > bz ? az - bz : bz - az;
    return dx > dz ? dx : dz;
}

struct YlChain
{
    int a, m, b, step, len;
};

static struct YlChain g_yl_all[YELLOW_ALL_MAX];
static int g_yl_through[YELLOW_POOLS_MAX][YELLOW_ALL_MAX];
static int g_yl_through_n[YELLOW_POOLS_MAX];

static int
yl_len_lt(int x, int y, const void* ud)
{
    (void)ud;
    return g_yl_all[x].len < g_yl_all[y].len;
}

static struct
{
    int np, hop, nodes, work;
    unsigned char used[YELLOW_POOLS_MAX];
    int cur[YELLOW_CHAINS_MAX], ncur;
    int best[YELLOW_CHAINS_MAX], nbest, have_best, best_cost;
} g_yl;

static void
yl_rec(int cost, int left)
{
    int p = -1, i, k;
    if( g_yl.work > g_yl.nodes )
        return;
    if( g_yl.have_best && cost >= g_yl.best_cost )
        return;
    if( left == 0 )
    {
        g_yl.have_best = 1;
        g_yl.best_cost = cost;
        g_yl.nbest = g_yl.ncur;
        for( k = 0; k < g_yl.ncur; k++ )
            g_yl.best[k] = g_yl.cur[k];
        return;
    }
    for( i = 0; i < g_yl.np; i++ )
        if( !g_yl.used[i] )
        {
            p = i;
            break;
        }
    if( p < 0 )
        return;
    for( k = 0; k < g_yl_through_n[p]; k++ )
    {
        const struct YlChain* c = &g_yl_all[g_yl_through[p][k]];
        g_yl.work++;
        if( c->step <= g_yl.hop && !g_yl.used[c->a] && !g_yl.used[c->m] && !g_yl.used[c->b] )
        {
            g_yl.used[c->a] = g_yl.used[c->m] = g_yl.used[c->b] = 1;
            assert(g_yl.ncur < YELLOW_CHAINS_MAX);
            g_yl.cur[g_yl.ncur++] = g_yl_through[p][k];
            yl_rec(cost + c->len, left - 1);
            g_yl.ncur--;
            g_yl.used[c->a] = g_yl.used[c->m] = g_yl.used[c->b] = 0;
        }
    }
}

int
yellow_chains(const int* px, const int* pz, int np, int n, int hop0, int top, int nodes, int out[][3], int* hop_out)
{
    int d[YELLOW_POOLS_MAX][YELLOW_POOLS_MAX];
    int i, j, a, m, b, nall = 0, hop;
    assert(px);
    assert(pz);
    assert(out);
    assert(hop_out);
    assert(np >= 0);
    assert(np <= YELLOW_POOLS_MAX);
    assert(n >= 1);
    assert(n <= YELLOW_CHAINS_MAX);
    for( i = 0; i < np; i++ )
        for( j = 0; j < np; j++ )
            d[i][j] = yl_cheb(px[i], pz[i], px[j], pz[j]);
    for( i = 0; i < np; i++ )
        g_yl_through_n[i] = 0;
    for( m = 0; m < np; m++ )
        for( a = 0; a < np; a++ )
            if( a != m && d[a][m] <= top )
                for( b = a + 1; b < np; b++ )
                    if( b != m && d[m][b] <= top )
                    {
                        struct YlChain* c = &g_yl_all[nall];
                        assert(nall < YELLOW_ALL_MAX);
                        c->a = a;
                        c->m = m;
                        c->b = b;
                        c->step = d[a][m] > d[m][b] ? d[a][m] : d[m][b];
                        c->len = d[a][m] + d[m][b];
                        g_yl_through[a][g_yl_through_n[a]++] = nall;
                        g_yl_through[m][g_yl_through_n[m]++] = nall;
                        g_yl_through[b][g_yl_through_n[b]++] = nall;
                        nall++;
                    }
    for( i = 0; i < np; i++ )
        lua_sort(g_yl_through[i], g_yl_through_n[i], yl_len_lt, NULL);
    for( hop = hop0; hop <= top; hop++ )
    {
        memset(&g_yl, 0, sizeof(g_yl));
        g_yl.np = np;
        g_yl.hop = hop;
        g_yl.nodes = nodes;
        yl_rec(0, n);
        if( g_yl.have_best )
        {
            for( i = 0; i < g_yl.nbest; i++ )
            {
                const struct YlChain* c = &g_yl_all[g_yl.best[i]];
                out[i][0] = c->a;
                out[i][1] = c->m;
                out[i][2] = c->b;
            }
            *hop_out = hop;
            return 1;
        }
    }
    return 0;
}

static struct
{
    int nr, nc;
    const int *rx, *rz, *c1x, *c1z, *c3x, *c3z;
    unsigned char taken[YELLOW_CHAINS_MAX];
    int pick[YELLOW_CHAINS_MAX], rev[YELLOW_CHAINS_MAX];
    int best_pick[YELLOW_CHAINS_MAX], best_rev[YELLOW_CHAINS_MAX];
    int have_best, best_cost;
} g_ym;

static void
ym_rec(int j, int cost)
{
    int c;
    if( g_ym.have_best && cost >= g_ym.best_cost )
        return;
    if( j >= g_ym.nr )
    {
        int k;
        g_ym.have_best = 1;
        g_ym.best_cost = cost;
        for( k = 0; k < g_ym.nr; k++ )
        {
            g_ym.best_pick[k] = g_ym.pick[k];
            g_ym.best_rev[k] = g_ym.rev[k];
        }
        return;
    }
    for( c = 0; c < g_ym.nc; c++ )
    {
        if( !g_ym.taken[c] )
        {
            int a = yl_cheb(g_ym.rx[j], g_ym.rz[j], g_ym.c1x[c], g_ym.c1z[c]);
            int b = yl_cheb(g_ym.rx[j], g_ym.rz[j], g_ym.c3x[c], g_ym.c3z[c]);
            int w = b < a ? b : a;
            g_ym.taken[c] = 1;
            g_ym.pick[j] = c;
            g_ym.rev[j] = b < a;
            ym_rec(j + 1, cost + w);
            g_ym.taken[c] = 0;
        }
    }
}

int
yellow_match(const int* rx, const int* rz, int nr, const int* c1x, const int* c1z, const int* c3x, const int* c3z,
             int nc, int* pick, int* rev)
{
    int k;
    assert(nr >= 0);
    assert(nr <= YELLOW_CHAINS_MAX);
    assert(nc >= 0);
    assert(nc <= YELLOW_CHAINS_MAX);
    assert(pick);
    assert(rev);
    memset(&g_ym, 0, sizeof(g_ym));
    g_ym.nr = nr;
    g_ym.nc = nc;
    g_ym.rx = rx;
    g_ym.rz = rz;
    g_ym.c1x = c1x;
    g_ym.c1z = c1z;
    g_ym.c3x = c3x;
    g_ym.c3z = c3z;
    ym_rec(0, 0);
    if( !g_ym.have_best )
        return 0;
    for( k = 0; k < nr; k++ )
    {
        pick[k] = g_ym.best_pick[k];
        rev[k] = g_ym.best_rev[k];
    }
    return 1;
}

/* ====================================================== NEAREST TILES */

struct TnKey
{
    int i, d, x, z;
};

static int
tn_cmp(const void* pa, const void* pb)
{
    const struct TnKey* p = (const struct TnKey*)pa;
    const struct TnKey* q = (const struct TnKey*)pb;
    if( p->d != q->d )
        return p->d < q->d ? -1 : 1;
    if( p->x != q->x )
        return p->x < q->x ? -1 : 1;
    if( p->z != q->z )
        return p->z < q->z ? -1 : 1;
    return p->i < q->i ? -1 : (p->i > q->i ? 1 : 0);
}

int
tiles_nearest(int fx, int fz, const int* tx, const int* tz, int n, int max_d, int cap, int* out)
{
    static struct TnKey keys[8192];
    int i, k = 0;
    assert(n >= 0);
    assert(n <= 8192);
    assert(out || cap == 0);
    for( i = 0; i < n; i++ )
    {
        int d = yl_cheb(tx[i], tz[i], fx, fz);
        if( d <= max_d )
        {
            keys[k].i = i;
            keys[k].d = d;
            keys[k].x = tx[i];
            keys[k].z = tz[i];
            k++;
        }
    }
    qsort(keys, (size_t)k, sizeof(keys[0]), tn_cmp);
    if( k > cap )
        k = cap;
    for( i = 0; i < k; i++ )
        out[i] = keys[i].i;
    return k;
}

/* ========================================================= BLOAT HOLDS */

static int
bl_lap_at(const struct BloatHoldSpec* s, int dir, int speed, int m)
{
    int L = s->n_lap, j = s->pos, n, step;
    for( n = 0; n < m; n++ )
    {
        for( step = 1; step <= speed; step++ )
        {
            int q = (j - 1 + dir) % L;
            if( q < 0 )
                q += L;
            j = q + 1;
            if( step < speed && s->corner[j - 1] )
                break;
        }
    }
    return j - 1;
}

void
bloat_first_bad(struct CollisionMap* cm, const struct BloatHoldSpec* s, int x, int z, int at, int* lethal_t, int* damage_t)
{
    int t, i;
    assert(cm);
    assert(s);
    assert(lethal_t);
    assert(damage_t);
    assert(s->n_lap >= 1);
    assert(s->pos >= 1);
    assert(s->pos <= s->n_lap);
    *lethal_t = 0;
    *damage_t = 0;
    for( t = at + 1; t <= at + s->hold_long; t++ )
    {
        int lethal = 0, damage = 0, R = t + 1, m, have_m = 1;
        for( i = 0; i < s->n_hands; i++ )
            if( s->hand_x[i] == x && s->hand_z[i] == z && t >= s->hand_t0[i] && t <= s->hand_t1[i] )
                lethal = 1;
        if( s->has_stomp && at < s->stomp_before && t == s->stomp_t &&
            collision_plan_watcher_sees(cm, s->stomp_x, s->stomp_z, s->size, x, z) )
            lethal = 1;
        /* moves_by (tob_bloat.lua) */
        if( s->has_down && R < s->down_at + s->rise )
            have_m = 0;
        else if( s->has_down && s->tick < s->down_at + s->rise )
            m = R - (s->down_at + s->rise) + 1;
        else
            m = R - s->tick;
        if( have_m )
        {
            int bi, mv;
            for( bi = 0; bi < s->n_br; bi++ )
            {
                if( bi == 0 ? lethal : (lethal || damage) )
                    continue;
                for( mv = 0; mv < 2; mv++ )
                {
                    int moved = mv == 0 ? m - 1 : m;
                    int j = bl_lap_at(s, s->br_dir[bi], s->br_speed[bi], moved > 0 ? moved : 0);
                    if( collision_plan_watcher_sees(cm, s->lap_x[j], s->lap_z[j], s->size, x, z) )
                    {
                        if( bi == 0 )
                            lethal = 1;
                        else
                            damage = 1;
                        break;
                    }
                }
            }
        }
        if( lethal && *lethal_t == 0 )
            *lethal_t = t;
        if( (lethal || damage) && *damage_t == 0 )
            *damage_t = t;
        if( *lethal_t )
            return;
    }
}

/* ======================================================== SOTE GUARD */

/* The flights as Jagex sends them (RSProx; docs/rsprox-analysis/tob/sotetseg.md):
 * his ball ends at 203 cycles at ANY distance, so it lands 6 ticks after the
 * throw wherever the target stands; a red ricochet ends at 36 + 30 x max(d, 3)
 * and its impact queue runs a tick short of that from the victim's queue, so it
 * lands max(d, 3) after the victim's landing, +1 when the recipient's turn runs
 * first (`o`). The grey ends at 216 wherever it goes - a tile cannot move it -
 * so the red's is the landing a tile decides. */
#define SG_BALL_TICKS 6
#define SG_RED_MIN_TILES 3

static int
sg_red_ticks(int d)
{
    return d < SG_RED_MIN_TILES ? SG_RED_MIN_TILES : d;
}

static int
sg_land_mov(const struct SoteGuardSpec* s, int i, int x, int z)
{
    int d = yl_cheb(x, z, s->mov_x[i], s->mov_z[i]);
    if( s->mov_red[i] )
        return s->mov_a[i] + SG_BALL_TICKS;
    return s->mov_a[i] + sg_red_ticks(d) + s->mov_o[i];
}

static int
sg_clash(const struct SoteGuardSpec* s, int t, int grey_only)
{
    int i;
    for( i = 0; i < s->n_fixed; i++ )
        if( s->fixed_t[i] == t && (!grey_only || s->fixed_grey[i]) )
            return 1;
    return 0;
}

int
sote_guard(const struct SoteGuardSpec* s, int* out_x, int* out_z, int* out_t, int* out_kind)
{
    int n = 0, dx, dz, i, j;
    int l[SOTE_MOV_MAX];
    assert(s);
    assert(s->n_mov >= 0);
    assert(s->n_mov <= SOTE_MOV_MAX);
    assert(s->n_hyp >= 0);
    assert(s->n_hyp <= SOTE_HYP_MAX);
    assert(s->n_fixed >= 0);
    assert(s->n_fixed <= SOTE_FIXED_MAX);
    assert(s->max >= 0);
    assert(s->max <= SOTE_OUT_MAX);
#define SG_FORBID(X, Z, T, K)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if( n < s->max )                                                                                               \
        {                                                                                                              \
            out_x[n] = (X);                                                                                            \
            out_z[n] = (Z);                                                                                            \
            out_t[n] = (T);                                                                                            \
            out_kind[n] = (K);                                                                                         \
            n++;                                                                                                       \
        }                                                                                                              \
    } while( 0 )
    for( dx = -s->r; dx <= s->r; dx++ )
    {
        for( dz = -s->r; dz <= s->r; dz++ )
        {
            int x = s->mx + dx, z = s->mz + dz;
            for( i = 0; i < s->n_mov; i++ )
                l[i] = sg_land_mov(s, i, x, z);
            for( i = 0; i < s->n_hyp; i++ )
            {
                int lh = s->hyp_lv[i] + sg_red_ticks(yl_cheb(x, z, s->hyp_x[i], s->hyp_z[i])) + s->hyp_o[i];
                if( sg_clash(s, lh, 0) )
                    SG_FORBID(x, z, s->hyp_tk[i], SOTE_MAYBE);
            }
            for( i = 0; i < s->n_mov; i++ )
            {
                if( sg_clash(s, l[i], s->mov_red[i]) )
                    SG_FORBID(x, z, s->mov_tk[i], s->mov_red[i] ? SOTE_BALL : SOTE_RIC);
                for( j = i + 1; j < s->n_mov; j++ )
                {
                    int dt = s->mov_tk[j] - s->mov_tk[i];
                    if( (dt <= 1 && dt >= -1) && l[j] == l[i] && !(s->mov_red[i] && s->mov_red[j]) )
                    {
                        SG_FORBID(x, z, s->mov_tk[i], SOTE_PAIR);
                        if( s->mov_tk[j] != s->mov_tk[i] )
                            SG_FORBID(x, z, s->mov_tk[j], SOTE_PAIR);
                    }
                }
            }
        }
    }
#undef SG_FORBID
    return n;
}
