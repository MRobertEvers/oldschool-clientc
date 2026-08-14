/*
 * ev_swing — a weapon swing and its attached graphic, measured.
 *
 * The question this exists to answer is "the scythe's red sweep is off — by how
 * much, and in which of the three things that could be off". Looking at the game
 * cannot separate them, because all three produce the same complaint:
 *
 *   1. TIME     the graphic plays too early or too late against the swing
 *               (`spotanim_pl`'s third argument, in client cycles)
 *   2. PLACE    the graphic sits in the wrong part of the player's local space
 *               (the model's own vertices — `spotanim_pl` has no lateral term)
 *   3. HEIGHT   the graphic rides too high or too low
 *               (`spotanim_pl`'s second argument)
 *
 * So this builds the player the way the client builds one, merges the graphic in
 * the way the client merges one, walks the swing a client cycle at a time, and
 * prints where the blade is and where the lit part of the graphic is at each of
 * them. Then it sweeps every possible delay and reports the one that lines the
 * two up, and the leftover translation after that.
 *
 * It renders too, straight down, because a number that says "17 units left" and
 * a picture that shows the arc through the player's chest are different kinds of
 * evidence and the second one catches the case where the first is measuring the
 * wrong thing.
 *
 *     make -C tools/entity_viewer ev_swing
 *     tools/entity_viewer/ev_swing --rev osrs239 cache.osrs239 \
 *         --arc-model OSRS-Content/osrs239-content/models/spot/dragon_halberd_special_west_red.model \
 *         --out /tmp/scythe
 *
 * Defaults are the scythe of vitur's: obj 22325 in the right hand, sequence 8056
 * (`scythe_of_vitur_attack`), spotanim 1231 (`dragon_halberd_special_west_red`),
 * height 100, delay 16 — the values `scythe_of_vitur.rs2` actually ships.
 *
 * Nothing here is scythe-specific below the defaults: any weapon, any attack
 * sequence, any player-attached graphic.
 */

#include "ev_build.h"
#include "ev_player.h"
#include "ev_render.h"
#include "ev_wire.h"

#include "asset_access.h"
#include "bmp.h"
#include "tool_profile.h"
#include "toridraw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the scythe of vitur, as `scythe_of_vitur.rs2` ships it -------------- */

#define DEF_WEAPON_OBJ 22325 /* scythe_of_vitur */
#define DEF_ATTACK_SEQ 8056  /* scythe_of_vitur_attack */
#define DEF_SPOTANIM 1231    /* dragon_halberd_special_west_red */
#define DEF_HEIGHT 100
#define DEF_DELAY 16

/*
 * Which face alphas count as drawn.
 *
 * toridraw stores alpha the reference's way round — 0 is opaque, and the raster
 * inverts it on the way to the blend (`0 -> 0xFF`, see ToriDraw_ModelNewMerge's
 * own comment). So a HIGH stored alpha is an invisible face. 250 rather than 255
 * because a fade's last step need not land exactly on the endpoint, and a face
 * one unit from full transparency is not evidence of anything.
 *
 * This matters more than a threshold usually does: `sp_d_halberd_glow` never
 * moves a vertex. Its framemap is twelve alpha transforms and one origin, so the
 * whole animation is which segments of a static crescent are visible. Measure
 * the mesh instead of the visible part of it and every frame reads identical.
 */
#define DEF_ALPHA_VISIBLE 250

#define MAX_CYCLES 4096
#define MAX_FRAMES 512

struct Vec3
{
    double x, y, z;
};

struct ArcSample
{
    int visible_faces;
    int total_faces;
    struct Vec3 centroid; /* of the visible faces' corners */
    int alpha_min;
    int alpha_max;
};

struct BladeSample
{
    struct Vec3 centroid; /* every weapon vertex */
    struct Vec3 tip;      /* the weapon vertex furthest from the player's axis */
    double tip_radius;
};

/* ---- small helpers ------------------------------------------------------- */

static double
dist3(struct Vec3 a, struct Vec3 b)
{
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static double
dist_xz(struct Vec3 a, struct Vec3 b)
{
    double dx = a.x - b.x, dz = a.z - b.z;
    return sqrt(dx * dx + dz * dz);
}

/** Serialise a built model straight into ev_render, which is the only way in.
 *  Going through the wire rather than around it is deliberate: the browser sees
 *  exactly these bytes, so a defect in the format shows up here too. */
static int
adopt_model(struct ToriDraw_Model* model, int spot)
{
    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_model(&buf, model);
    int faces = 0;
    if( ok )
        faces = spot ? ev_set_spot_model(buf.data, (int)buf.len)
                     : ev_set_model(buf.data, (int)buf.len);
    ev_wire_free(&buf);
    return faces;
}

static int
adopt_anim(struct ToriDraw_Animation* anim, int spot)
{
    struct EV_WireBuf buf = { 0 };
    int ok = ev_wire_write_anim(&buf, anim);
    int frames = 0;
    if( ok )
        frames = spot ? ev_set_spot_anim(buf.data, (int)buf.len)
                      : ev_set_anim(buf.data, (int)buf.len);
    ev_wire_free(&buf);
    return frames;
}

/* ---- reading geometry out of the posed model ----------------------------- */

/*
 * The graphic's currently-lit part, in the player's local space.
 *
 * `first` is where the graphic's vertices begin in the merged model
 * (ev_spot_vertex_first). A face belongs to the graphic when its corners do; the
 * body's faces never index past that boundary, so the test is one comparison.
 */
static struct ArcSample
measure_arc(const struct ToriDraw_Model* m, int first, int alpha_visible)
{
    struct ArcSample s;
    double sx = 0, sy = 0, sz = 0;
    int n = 0;

    memset(&s, 0, sizeof(s));
    s.alpha_min = 255;
    s.alpha_max = 0;
    if( !m || first < 0 )
        return s;

    for( int f = 0; f < m->face_count; f++ )
    {
        int a = m->face_indices_a[f];
        int b = m->face_indices_b[f];
        int c = m->face_indices_c[f];
        int alpha;
        if( a < first || b < first || c < first )
            continue;
        s.total_faces++;
        alpha = m->face_alphas ? (int)(unsigned char)m->face_alphas[f] : 0;
        if( alpha < s.alpha_min )
            s.alpha_min = alpha;
        if( alpha > s.alpha_max )
            s.alpha_max = alpha;
        if( alpha >= alpha_visible )
            continue;
        s.visible_faces++;
        sx += (double)(m->vertices_x[a] + m->vertices_x[b] + m->vertices_x[c]) / 3.0;
        sy += (double)(m->vertices_y[a] + m->vertices_y[b] + m->vertices_y[c]) / 3.0;
        sz += (double)(m->vertices_z[a] + m->vertices_z[b] + m->vertices_z[c]) / 3.0;
        n++;
    }
    if( n )
    {
        s.centroid.x = sx / n;
        s.centroid.y = sy / n;
        s.centroid.z = sz / n;
    }
    if( s.total_faces == 0 )
        s.alpha_min = 0;
    return s;
}

/*
 * Where the weapon is.
 *
 * The centroid is the whole wear model, haft included, so it barely moves. The
 * tip is the vertex furthest from the player's own vertical axis, which for a
 * polearm is the far end of the blade and is what actually sweeps. Both are
 * reported because the tip is a single vertex and a single vertex can be a
 * decoration; when the two disagree about the direction of travel, the centroid
 * is the one to trust.
 */
static struct BladeSample
measure_blade(const struct ToriDraw_Model* m, int first, int count)
{
    struct BladeSample s;
    double sx = 0, sy = 0, sz = 0;
    int best = -1;
    double best_r = -1;

    memset(&s, 0, sizeof(s));
    if( !m || count <= 0 )
        return s;

    for( int i = first; i < first + count && i < m->vertex_count; i++ )
    {
        double x = m->vertices_x[i];
        double y = m->vertices_y[i];
        double z = m->vertices_z[i];
        double r = x * x + z * z;
        sx += x;
        sy += y;
        sz += z;
        if( r > best_r )
        {
            best_r = r;
            best = i;
        }
    }
    s.centroid.x = sx / count;
    s.centroid.y = sy / count;
    s.centroid.z = sz / count;
    if( best >= 0 )
    {
        s.tip.x = m->vertices_x[best];
        s.tip.y = m->vertices_y[best];
        s.tip.z = m->vertices_z[best];
        s.tip_radius = sqrt(best_r);
    }
    return s;
}

/* ---- the timeline -------------------------------------------------------- */

/*
 * A sequence's frame for a given client cycle.
 *
 * Frame durations are in client cycles (30 to the server tick) — the unit the
 * cache stores, the unit `spotanim_pl`'s delay is in, and the unit
 * pkt_player_info.h names on the wire. Returns -1 once the sequence has run out,
 * which is a real state and not an error: a graphic that has finished is not
 * drawn, and holding its last frame instead would put a bright arc on screen for
 * the whole of the recovery.
 */
static int
frame_at_cycle(const int* delays, int frame_count, int cycle)
{
    int acc = 0;
    if( cycle < 0 )
        return -1;
    for( int i = 0; i < frame_count; i++ )
    {
        int d = delays[i] > 0 ? delays[i] : 1;
        if( cycle < acc + d )
            return i;
        acc += d;
    }
    return -1;
}

static int
total_cycles(const int* delays, int frame_count)
{
    int acc = 0;
    for( int i = 0; i < frame_count; i++ )
        acc += delays[i] > 0 ? delays[i] : 1;
    return acc;
}

/* ---- rendering ----------------------------------------------------------- */

/* Copy one ev_render frame into a cell of a contact sheet. */
static void
blit(int* sheet, int sheet_w, int cell_x, int cell_y, const uint8_t* rgba, int side)
{
    if( !rgba )
        return;
    for( int y = 0; y < side; y++ )
        for( int x = 0; x < side; x++ )
        {
            int i = y * side + x;
            sheet[(cell_y + y) * sheet_w + cell_x + x] =
                (int)(0xFF000000u | ((uint32_t)rgba[i * 4] << 16) |
                      ((uint32_t)rgba[i * 4 + 1] << 8) | (uint32_t)rgba[i * 4 + 2]);
        }
}

/* A one-pixel rule between cells, so a contact sheet reads as a grid rather
 * than as one wide image with things in it. */
static void
rule(int* sheet, int sheet_w, int sheet_h, int x, int colour)
{
    if( x < 0 || x >= sheet_w )
        return;
    for( int y = 0; y < sheet_h; y++ )
        sheet[y * sheet_w + x] = colour;
}

int
main(int argc, char** argv)
{
    const char* rev_name = "osrs239";
    const char* cache_dir = NULL;
    const char* arc_model_file = NULL;
    const char* out_prefix = NULL;
    int weapon_obj = DEF_WEAPON_OBJ;
    int attack_seq = DEF_ATTACK_SEQ;
    int spotanim_id = DEF_SPOTANIM;
    int height = DEF_HEIGHT;
    int delay = DEF_DELAY;
    int alpha_visible = DEF_ALPHA_VISIBLE;
    int side = 320;
    int columns = 12;
    int extra_worn[EV_PLAYER_MAX_WORN];
    int extra_worn_count = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( strcmp(argv[i], "--weapon") == 0 && i + 1 < argc )
            weapon_obj = atoi(argv[++i]);
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            attack_seq = atoi(argv[++i]);
        else if( strcmp(argv[i], "--spotanim") == 0 && i + 1 < argc )
            spotanim_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--height") == 0 && i + 1 < argc )
            height = atoi(argv[++i]);
        else if( strcmp(argv[i], "--delay") == 0 && i + 1 < argc )
            delay = atoi(argv[++i]);
        else if( strcmp(argv[i], "--arc-model") == 0 && i + 1 < argc )
            arc_model_file = argv[++i];
        else if( strcmp(argv[i], "--alpha-visible") == 0 && i + 1 < argc )
            alpha_visible = atoi(argv[++i]);
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_prefix = argv[++i];
        else if( strcmp(argv[i], "--side") == 0 && i + 1 < argc )
            side = atoi(argv[++i]);
        else if( strcmp(argv[i], "--columns") == 0 && i + 1 < argc )
            columns = atoi(argv[++i]);
        else if( strcmp(argv[i], "--wear") == 0 && i + 1 < argc )
        {
            if( extra_worn_count < EV_PLAYER_MAX_WORN - 1 )
                extra_worn[extra_worn_count++] = atoi(argv[++i]);
            else
                i++;
        }
        else if( argv[i][0] != '-' )
            cache_dir = argv[i];
        else
        {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    if( !cache_dir )
    {
        fprintf(
            stderr,
            "Usage: %s --rev osrs239 <cache_dir> [options]\n"
            "\n"
            "  --weapon <obj>        weapon obj id (default %d, scythe of vitur)\n"
            "  --seq <id>            attack sequence (default %d)\n"
            "  --spotanim <id>       attached graphic (default %d)\n"
            "  --height <units>      spotanim_pl height (default %d)\n"
            "  --delay <cycles>      spotanim_pl delay (default %d)\n"
            "  --arc-model <file>    use this model record for the graphic instead\n"
            "                        of the cache's, for measuring an edited asset\n"
            "  --wear <obj>          equip another obj (repeatable)\n"
            "  --alpha-visible <n>   faces with stored alpha below n count as drawn\n"
            "                        (default %d; 0 opaque, 255 invisible)\n"
            "  --out <prefix>        write <prefix>_top.bmp and <prefix>_side.bmp\n"
            "  --side <px>           contact-sheet cell size (default %d)\n"
            "  --columns <n>         cells per row (default %d)\n",
            argv[0],
            DEF_WEAPON_OBJ,
            DEF_ATTACK_SEQ,
            DEF_SPOTANIM,
            DEF_HEIGHT,
            DEF_DELAY,
            DEF_ALPHA_VISIBLE,
            side,
            columns);
        return 2;
    }

    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(rev_name, NULL, NULL, NULL, NULL, &profile) )
        return 1;
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "cannot open cache at %s\n", cache_dir);
        return 1;
    }
    ToriDraw_Init();
    ev_init();

    /* ---- build ---------------------------------------------------------- */

    struct EV_PlayerSpec spec;
    struct EV_PlayerPartMap map;
    ev_player_spec_init(&spec);
    for( int i = 0; i < extra_worn_count; i++ )
        spec.worn[spec.worn_count++] = extra_worn[i];
    spec.worn[spec.worn_count++] = weapon_obj;

    struct ToriDraw_Model* body = ev_build_player_model(&cache, &spec, &map);
    if( !body )
    {
        fprintf(stderr, "could not build the player model\n");
        return 1;
    }

    /* The weapon's slice of the merged player. Wear models come after the body
     * kits, so the last entries for this obj id are it; a two-handed weapon can
     * contribute more than one. */
    int weapon_vertex_first = -1;
    int weapon_vertex_count = 0;
    for( int i = 0; i < map.count; i++ )
    {
        if( !map.parts[i].is_obj || map.parts[i].source_id != weapon_obj )
            continue;
        if( weapon_vertex_first < 0 )
            weapon_vertex_first = map.parts[i].vertex_first;
        weapon_vertex_count += map.parts[i].vertex_count;
    }
    if( weapon_vertex_first < 0 )
    {
        fprintf(
            stderr,
            "obj %d contributed no wear model — nothing to measure the graphic against\n",
            weapon_obj);
        return 1;
    }

    int body_framemap = -1;
    struct ToriDraw_Animation* body_anim =
        ev_build_seq_anim(&cache, attack_seq, &body_framemap);
    if( !body_anim || body_anim->skeletal || !body_anim->frames )
    {
        fprintf(stderr, "sequence %d is not a classic keyframe animation\n", attack_seq);
        return 1;
    }

    int spot_seq = -1;
    struct ToriDraw_Model* spot =
        ev_build_spotanim_model(&cache, spotanim_id, arc_model_file, &spot_seq);
    if( !spot )
    {
        fprintf(stderr, "spotanim %d did not build\n", spotanim_id);
        return 1;
    }
    int spot_framemap = -1;
    struct ToriDraw_Animation* spot_anim =
        spot_seq >= 0 ? ev_build_seq_anim(&cache, spot_seq, &spot_framemap) : NULL;
    if( !spot_anim || spot_anim->skeletal || !spot_anim->frames )
    {
        fprintf(stderr, "spotanim %d's sequence %d is not playable here\n", spotanim_id, spot_seq);
        return 1;
    }

    adopt_model(body, 0);
    adopt_anim(body_anim, 0);
    adopt_model(spot, 1);
    adopt_anim(spot_anim, 1);

    int body_frames = body_anim->frame_count;
    int spot_frames = spot_anim->frame_count;
    if( body_frames > MAX_FRAMES || spot_frames > MAX_FRAMES )
    {
        fprintf(stderr, "sequence longer than this harness's %d-frame tables\n", MAX_FRAMES);
        return 1;
    }

    static int body_delays[MAX_FRAMES];
    static int spot_delays[MAX_FRAMES];
    for( int i = 0; i < body_frames; i++ )
        body_delays[i] = ev_frame_delay(i);
    for( int i = 0; i < spot_frames; i++ )
        spot_delays[i] = ev_spot_frame_delay(i);
    int body_total = total_cycles(body_delays, body_frames);
    int spot_total = total_cycles(spot_delays, spot_frames);

    printf("weapon obj %d      wear model vertices %d..%d of %d\n",
           weapon_obj, weapon_vertex_first, weapon_vertex_first + weapon_vertex_count - 1,
           body->vertex_count);
    printf("attack seq %d     %d frames, %d client cycles, rig %d\n",
           attack_seq, body_frames, body_total, body_framemap);
    printf("spotanim %d       model %s, seq %d: %d frames, %d cycles, rig %d\n",
           spotanim_id,
           arc_model_file ? arc_model_file : "(from cache)",
           spot_seq, spot_frames, spot_total, spot_framemap);
    printf("shipped placement: height %d, delay %d cycles\n\n", height, delay);

    /* ---- per-frame tables ------------------------------------------------ */

    /*
     * The graphic is frozen inside the merged model — its labels were dropped
     * before the combine — so where it is depends on ITS frame and nothing else,
     * and where the blade is depends on the body's frame and nothing else. That
     * is what makes a delay sweep arithmetic instead of thousands of merges.
     *
     * It is also an assumption about someone else's code, so it is checked
     * below rather than asserted in a comment.
     */
    static struct ArcSample arc[MAX_FRAMES];
    static struct BladeSample blade[MAX_FRAMES];

    for( int f = 0; f < spot_frames; f++ )
    {
        ev_set_spot_state(height, f);
        ev_pose(-1); /* body at rest: the graphic's placement cannot depend on it */
        arc[f] = measure_arc(ev_drawn_model(), ev_spot_vertex_first(), alpha_visible);
    }

    ev_set_spot_state(height, -1); /* detached: the plain player model */
    for( int b = 0; b < body_frames; b++ )
    {
        ev_pose(b);
        blade[b] = measure_blade(ev_drawn_model(), weapon_vertex_first, weapon_vertex_count);
    }

    /* The independence check. Two arbitrary frames, both halves measured out of
     * the same merged, posed model, compared against the tables above. */
    {
        int b = body_frames / 2;
        int f = spot_frames / 2;
        ev_set_spot_state(height, f);
        ev_pose(b);
        struct ArcSample a2 = measure_arc(ev_drawn_model(), ev_spot_vertex_first(), alpha_visible);
        struct BladeSample b2 =
            measure_blade(ev_drawn_model(), weapon_vertex_first, weapon_vertex_count);
        double da = dist3(a2.centroid, arc[f].centroid);
        double db = dist3(b2.centroid, blade[b].centroid);
        printf(
            "independence check at body frame %d / graphic frame %d: "
            "graphic moved %.2f, blade moved %.2f -> %s\n\n",
            b,
            f,
            da,
            db,
            (da < 0.5 && db < 0.5) ? "PASS (the two are separable)"
                                   : "FAIL - the sweep below is not valid");
    }

    /* ---- the swing, cycle by cycle --------------------------------------- */

    printf("cycle  bfrm  blade tip (x,y,z)     travel   gfrm  lit/all  arc centroid (x,z)   gap\n");
    printf("-----  ----  --------------------  -------  ----  -------  -------------------  ------\n");

    struct Vec3 prev_tip = { 0, 0, 0 };
    int have_prev = 0;
    double best_gap = 1e18;
    int best_gap_cycle = -1;
    double peak_travel = -1;
    int peak_travel_cycle = -1;
    int first_lit_cycle = -1;
    int last_lit_cycle = -1;

    for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
    {
        int b = frame_at_cycle(body_delays, body_frames, t);
        int f = frame_at_cycle(spot_delays, spot_frames, t - delay);
        double travel = 0;
        if( b < 0 )
            break;
        if( have_prev )
            travel = dist3(blade[b].tip, prev_tip);
        prev_tip = blade[b].tip;
        have_prev = 1;
        if( travel > peak_travel )
        {
            peak_travel = travel;
            peak_travel_cycle = t;
        }

        printf("%5d  %4d  %6.0f %6.0f %6.0f  %7.1f", t, b, blade[b].tip.x, blade[b].tip.y,
               blade[b].tip.z, travel);
        if( f < 0 )
            printf("     -        -  %19s  %6s\n", "-", "-");
        else
        {
            double gap = dist_xz(blade[b].tip, arc[f].centroid);
            printf("  %4d  %3d/%3d", f, arc[f].visible_faces, arc[f].total_faces);
            if( arc[f].visible_faces == 0 )
                printf("  %19s  %6s\n", "(nothing drawn)", "-");
            else
            {
                printf("  %9.0f %9.0f  %6.0f\n", arc[f].centroid.x, arc[f].centroid.z, gap);
                if( first_lit_cycle < 0 )
                    first_lit_cycle = t;
                last_lit_cycle = t;
                if( gap < best_gap )
                {
                    best_gap = gap;
                    best_gap_cycle = t;
                }
            }
        }
    }

    printf("\n");
    printf("blade moves fastest at cycle %d (%.1f units in that cycle)\n", peak_travel_cycle,
           peak_travel);
    if( first_lit_cycle >= 0 )
        printf("graphic is lit over cycles %d..%d; closest approach %.0f units at cycle %d\n",
               first_lit_cycle, last_lit_cycle, best_gap, best_gap_cycle);
    else
        printf("graphic is never lit anywhere in the swing at delay %d\n", delay);
    printf("\n");

    /* ---- what the graphic's own sequence does ---------------------------- */

    printf("graphic frame table (its own timeline, before the delay is applied)\n");
    printf("gfrm  cycles      lit/all  alpha lo..hi  centroid (x,z)\n");
    {
        int acc = 0;
        for( int f = 0; f < spot_frames; f++ )
        {
            int d = spot_delays[f] > 0 ? spot_delays[f] : 1;
            printf("%4d  %3d..%-3d    %3d/%3d  %5d..%-5d ", f, acc, acc + d, arc[f].visible_faces,
                   arc[f].total_faces, arc[f].alpha_min, arc[f].alpha_max);
            if( arc[f].visible_faces )
                printf(" %7.0f %7.0f\n", arc[f].centroid.x, arc[f].centroid.z);
            else
                printf(" %15s\n", "(nothing drawn)");
            acc += d;
        }
    }
    printf("\n");

    /* ---- the sweep: which delay lines the two up ------------------------- */

    /*
     * For each candidate delay, the mean distance between the lit part of the
     * graphic and the blade tip over every cycle where both exist. Fewest units
     * wins.
     *
     * `paired` is reported alongside the score because a delay that puts the
     * graphic almost entirely outside the swing scores well on the two cycles
     * that do overlap, and a mean over two samples is not a measurement. Read
     * the two columns together.
     */
    printf("delay sweep — mean blade/graphic separation per candidate delay\n");
    printf("delay  paired  mean gap  best delay so far\n");
    int best_delay = delay;
    double best_score = 1e18;
    int best_paired = 0;
    for( int d = 0; d <= body_total; d++ )
    {
        double sum = 0;
        int paired = 0;
        for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            int f = frame_at_cycle(spot_delays, spot_frames, t - d);
            if( b < 0 || f < 0 || arc[f].visible_faces == 0 )
                continue;
            sum += dist_xz(blade[b].tip, arc[f].centroid);
            paired++;
        }
        /* A candidate has to overlap the swing for at least half of the
         * graphic's own lit span to be judged at all. */
        if( paired < 4 )
            continue;
        double mean = sum / paired;
        if( mean < best_score )
        {
            best_score = mean;
            best_delay = d;
            best_paired = paired;
        }
        if( d % 4 == 0 || d == delay )
            printf("%5d  %6d  %8.1f  %s\n", d, paired, mean, d == delay ? "<- shipped" : "");
    }
    printf("\nbest delay %d (%d paired cycles, mean gap %.1f units)\n", best_delay, best_paired,
           best_score);

    /* ---- the leftover translation ---------------------------------------- */

    /*
     * Having chosen a delay, what is left is a rigid offset: the mean vector
     * from the graphic's lit centroid to the blade tip across the paired cycles.
     * That is the amount the ASSET has to move, because `spotanim_pl` has no
     * lateral argument — only the model's own vertices can express it. The y
     * term is the exception: it is the height argument, and needs no asset edit.
     *
     * Signs, so the numbers can be acted on without re-deriving them: a player
     * at yaw 0 faces south, model space is world-aligned there, so -z is the
     * direction they face and -x is their right hand. `shift_halberd_arc.py`
     * takes dx in these units and 128 units is one tile.
     */
    {
        double sx = 0, sy = 0, sz = 0;
        int paired = 0;
        for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            int f = frame_at_cycle(spot_delays, spot_frames, t - best_delay);
            if( b < 0 || f < 0 || arc[f].visible_faces == 0 )
                continue;
            sx += blade[b].tip.x - arc[f].centroid.x;
            sy += blade[b].tip.y - arc[f].centroid.y;
            sz += blade[b].tip.z - arc[f].centroid.z;
            paired++;
        }
        if( paired )
        {
            double dx = sx / paired, dy = sy / paired, dz = sz / paired;
            printf("\nresidual offset at delay %d, averaged over %d cycles:\n", best_delay, paired);
            printf("  dx %+7.1f units (%+.2f tiles)   %s\n", dx, dx / 128.0,
                   dx < 0 ? "graphic must move to the player's RIGHT"
                          : "graphic must move to the player's LEFT");
            printf("  dz %+7.1f units (%+.2f tiles)   %s\n", dz, dz / 128.0,
                   dz < 0 ? "graphic must move FORWARD" : "graphic must move BACK");
            printf("  dy %+7.1f units                 %s\n", dy,
                   "model y is negative-up: spotanim_pl height += this");
            printf("\nto apply the lateral part:  python3 tools/shift_halberd_arc.py %d\n",
                   (int)(dx + (dx < 0 ? -0.5 : 0.5)));
            printf("to apply the height part:   spotanim_pl(..., %d, %d)\n",
                   height + (int)(dy + (dy < 0 ? -0.5 : 0.5)), best_delay);
        }
    }

    /* ---- pictures --------------------------------------------------------- */

    if( out_prefix )
    {
        int cells = columns > 0 ? columns : 12;
        int rows = (body_total + cells - 1) / cells;
        int step = 1;
        /* One cell per cycle when the sequence is short enough to fit; otherwise
         * an even sample. Sampling is stated rather than silently applied. */
        if( rows > 4 )
        {
            step = (body_total + (cells * 4) - 1) / (cells * 4);
            rows = 4;
        }
        int sheet_w = cells * side;
        int sheet_h = rows * side;
        int* top = calloc((size_t)sheet_w * sheet_h, sizeof(int));
        int* sid = calloc((size_t)sheet_w * sheet_h, sizeof(int));

        /* Frame the whole player from above. The bounds of the merged model
         * change frame to frame, so the zoom is fixed off the rest pose — a
         * per-frame zoom would rescale the picture under the thing being
         * measured. */
        ev_set_spot_state(height, -1);
        ev_pose(-1);
        int zoom = ev_model_height() * 3;
        if( zoom < 400 )
            zoom = 400;

        int cell = 0;
        for( int t = 0; t < body_total && cell < cells * rows; t += step, cell++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            int f = frame_at_cycle(spot_delays, spot_frames, t - delay);
            int cx = (cell % cells) * side;
            int cy = (cell / cells) * side;
            if( b < 0 )
                break;
            ev_set_spot_state(height, f);
            /* Straight down: pitch 512 of 2048 is a quarter turn, so the orbit
             * sits directly above the model and screen x/y read as world x/z. */
            blit(top, sheet_w, cx, cy, ev_render(side, side, 0, 512, zoom, b), side);
            /* And from the side, at the yaw a player fighting south is drawn at,
             * because "is it at the right height" is not visible from above. */
            blit(sid, sheet_w, cx, cy, ev_render(side, side, 0, 100, zoom, b), side);
            rule(top, sheet_w, sheet_h, cx, 0x00404040);
            rule(sid, sheet_w, sheet_h, cx, 0x00404040);
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s_top.bmp", out_prefix);
        bmp_write_file(path, top, sheet_w, sheet_h);
        snprintf(path, sizeof(path), "%s_side.bmp", out_prefix);
        bmp_write_file(path, sid, sheet_w, sheet_h);
        printf("\nwrote %s_top.bmp and %s_side.bmp — %d cells, every %d cycle(s), %dx%d each\n",
               out_prefix, out_prefix, cell, step, side, side);
        free(top);
        free(sid);
    }

    ToriDraw_ModelFree(body);
    ToriDraw_ModelFree(spot);
    ToriDraw_AnimationFree(body_anim);
    ToriDraw_AnimationFree(spot_anim);
    return 0;
}
