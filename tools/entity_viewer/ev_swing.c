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
    struct Vec3 tip;      /* one tracked vertex at the far end of the weapon */
    struct Vec3 head;     /* the far quarter of the weapon, averaged */
};

/*
 * Which of a weapon's vertices are the part a motion streak is meant to trail.
 *
 * Two wrong answers were tried first, and both produced numbers that looked
 * like measurements.
 *
 *   "The vertex furthest from the player's axis, per frame" is not a point on
 *   the weapon at all — it is whichever vertex happens to be furthest this
 *   frame, and it swaps between opposite ends as the weapon turns. It read as
 *   the tip travelling 288 units in one cycle during the impact hold, while the
 *   weapon was barely moving.
 *
 *   "The quarter furthest from the grip" is a fixed index set, which fixes that,
 *   but on a polearm the grip sits mid-haft and BOTH ends are far from it. The
 *   set spanned the blade and the butt, which move in opposite directions, so
 *   their average cancelled: a 145-unit weapon reporting a 20-unit sweep.
 *
 * The answer that needs no guess about which end is which: the part that
 * actually moves. Pose every frame, total each vertex's path length, and take
 * the ones in the top band. A streak is a record of motion, so the geometry it
 * should trace is the geometry with motion — and this measures that rather
 * than inferring it from the shape.
 */
struct BladeSelection
{
    int tip_index;      /* the single fastest vertex */
    int* head_indices;  /* the top band by path length */
    int head_count;
    double tip_path;    /* that vertex's total travel, for the report */
    double span;        /* how far the head set reaches from the weapon centroid */
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
 * Choose the sweeping part of the weapon from `track`, a [frames][count] table
 * of every weapon vertex's position in every frame. Returns 0 on failure.
 *
 * The band is "within 40% of the fastest vertex's path length", not a fixed
 * fraction of the vertex count: how much of a weapon sweeps is a property of
 * the weapon, and a count-based cut would take the same number of vertices off
 * a dagger and off a halberd.
 */
static int
select_blade(
    const struct Vec3* track,
    int frames,
    int first,
    int count,
    struct BladeSelection* out)
{
    double* path;
    double cut;

    memset(out, 0, sizeof(*out));
    if( !track || frames < 2 || count <= 0 )
        return 0;

    path = calloc((size_t)count, sizeof(double));
    out->head_indices = calloc((size_t)count, sizeof(int));
    if( !path || !out->head_indices )
    {
        free(path);
        free(out->head_indices);
        out->head_indices = NULL;
        return 0;
    }

    for( int f = 1; f < frames; f++ )
        for( int i = 0; i < count; i++ )
            path[i] += dist3(track[f * count + i], track[(f - 1) * count + i]);

    for( int i = 0; i < count; i++ )
        if( path[i] > out->tip_path )
        {
            out->tip_path = path[i];
            out->tip_index = first + i;
        }

    cut = out->tip_path * 0.60;
    for( int i = 0; i < count; i++ )
        if( path[i] >= cut )
            out->head_indices[out->head_count++] = first + i;

    /* How far the chosen set reaches, so the report can say whether "the part
     * that moves" is a blade or a scattering of trim. */
    if( out->head_count > 0 )
    {
        struct Vec3 c = { 0, 0, 0 };
        for( int i = 0; i < out->head_count; i++ )
            c.x += track[out->head_indices[i] - first].x,
                c.y += track[out->head_indices[i] - first].y,
                c.z += track[out->head_indices[i] - first].z;
        c.x /= out->head_count;
        c.y /= out->head_count;
        c.z /= out->head_count;
        for( int i = 0; i < out->head_count; i++ )
        {
            double d = dist3(track[out->head_indices[i] - first], c);
            if( d > out->span )
                out->span = d;
        }
    }

    free(path);
    return out->head_count > 0;
}

/*
 * Where the weapon is, this frame.
 *
 * Three readings, because each answers a different objection. The centroid is
 * the whole wear model and barely moves — it is the control. The tip is the
 * single fastest vertex and shows the full sweep, but one vertex can be a
 * decoration hanging off the model. The head is the whole sweeping band
 * averaged, which is what the offset arithmetic uses.
 */
static struct BladeSample
measure_blade(const struct ToriDraw_Model* m, int first, int count,
              const struct BladeSelection* sel)
{
    struct BladeSample s;
    double sx = 0, sy = 0, sz = 0;

    memset(&s, 0, sizeof(s));
    if( !m || count <= 0 )
        return s;

    for( int i = first; i < first + count && i < m->vertex_count; i++ )
    {
        sx += m->vertices_x[i];
        sy += m->vertices_y[i];
        sz += m->vertices_z[i];
    }
    s.centroid.x = sx / count;
    s.centroid.y = sy / count;
    s.centroid.z = sz / count;

    if( !sel || sel->head_count <= 0 )
        return s;

    s.tip.x = m->vertices_x[sel->tip_index];
    s.tip.y = m->vertices_y[sel->tip_index];
    s.tip.z = m->vertices_z[sel->tip_index];

    sx = sy = sz = 0;
    for( int i = 0; i < sel->head_count; i++ )
    {
        int v = sel->head_indices[i];
        sx += m->vertices_x[v];
        sy += m->vertices_y[v];
        sz += m->vertices_z[v];
    }
    s.head.x = sx / sel->head_count;
    s.head.y = sy / sel->head_count;
    s.head.z = sz / sel->head_count;
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

static void
plot(int* buf, int w, int h, int x, int y, int colour)
{
    if( x < 0 || y < 0 || x >= w || y >= h )
        return;
    buf[y * w + x] = colour;
}

/** A small cross, so a marker is findable on a busy picture without hiding what
 *  is under it the way a filled dot would. */
static void
cross(int* buf, int w, int h, int x, int y, int r, int colour)
{
    for( int i = -r; i <= r; i++ )
    {
        plot(buf, w, h, x + i, y, colour);
        plot(buf, w, h, x, y + i, colour);
    }
}

static void
line(int* buf, int w, int h, int x0, int y0, int x1, int y1, int colour)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for( ;; )
    {
        plot(buf, w, h, x0, y0, colour);
        if( x0 == x1 && y0 == y1 )
            break;
        int e2 = 2 * err;
        if( e2 >= dy )
        {
            err += dy;
            x0 += sx;
        }
        if( e2 <= dx )
        {
            err += dx;
            y0 += sy;
        }
    }
}

/*
 * Model space -> the top-down render's pixels.
 *
 * ev_render's framing, at pitch 512 (a quarter turn, camera straight above),
 * reduces to something exact: it places the model at world
 * (0, zoom + lift, 0) and the projection is screen = centre + cam * scale /
 * cam_z, with cam_x = model x, cam_y = -model z and cam_z = zoom + lift +
 * model y. So a measured vertex can be marked on the picture without
 * re-deriving the camera, and the marker landing on the thing it names is the
 * check that the measurement and the render are talking about the same model.
 *
 * Note what this says about reading the image: +x is to the RIGHT, and the
 * player's facing (-z at yaw 0, which is south) is DOWN.
 */
static void
project_top_down(struct Vec3 v, int side, int zoom, int lift, int* out_x, int* out_y)
{
    double cam_z = (double)zoom + lift + v.y;
    if( cam_z < 1 )
        cam_z = 1;
    *out_x = side / 2 + (int)(v.x * (double)TORIDRAW_PROJ_SCALE_DEFAULT / cam_z);
    *out_y = side / 2 + (int)(-v.z * (double)TORIDRAW_PROJ_SCALE_DEFAULT / cam_z);
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
    int zoom_arg = 0;
    int orient = -1;
    int shift_x = 0;
    int shift_z = 0;
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
        else if( strcmp(argv[i], "--shift-x") == 0 && i + 1 < argc )
            shift_x = atoi(argv[++i]);
        else if( strcmp(argv[i], "--shift-z") == 0 && i + 1 < argc )
            shift_z = atoi(argv[++i]);
        else if( strcmp(argv[i], "--orient") == 0 && i + 1 < argc )
            orient = atoi(argv[++i]);
        else if( strcmp(argv[i], "--zoom") == 0 && i + 1 < argc )
            zoom_arg = atoi(argv[++i]);
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
            "  --orient <0-3>        force the graphic's quarter-turn rotation,\n"
            "                        overriding the spotanim record's own angle\n"
            "  --shift-x / --shift-z <units>\n"
            "                        move the graphic's vertices before merging, so a\n"
            "                        candidate placement can be measured and drawn\n"
            "                        before any asset is edited (-x is the player's\n"
            "                        right, -z is forward; 128 units is a tile)\n"
            "  --alpha-visible <n>   faces with stored alpha below n count as drawn\n"
            "                        (default %d; 0 opaque, 255 invisible)\n"
            "  --out <prefix>        write <prefix>_top.bmp and <prefix>_side.bmp\n"
            "  --zoom <units>        camera distance; larger sees more (default: 6x\n"
            "                        the player's height, enough for the whole graphic)\n"
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
        ev_build_spotanim_model(&cache, spotanim_id, arc_model_file, orient, &spot_seq);
    if( !spot )
    {
        fprintf(stderr, "spotanim %d did not build\n", spotanim_id);
        return 1;
    }
    /*
     * A trial translation, applied after the record's own transforms and before
     * the merge. It is where an asset edit would land, so measuring with it is
     * measuring the edit — without an edit to undo if the answer is no.
     *
     * Applied after lighting rather than before, which is safe only because a
     * uniform translation does not change a single face normal: RS lighting is
     * baked per face from the geometry's orientation, and this moves the
     * geometry without turning it. A rotation could not be applied here, which
     * is why --orient goes in ahead of the lighting instead.
     */
    if( shift_x || shift_z )
    {
        ToriDraw_ModelTranslate(spot, shift_x, 0, shift_z);
        ToriDraw_ModelSetBoundsCylinder(spot);
        ToriDraw_ModelCaptureOriginalVertices(spot);
        printf("trial shift: graphic moved dx %+d, dz %+d before merging\n", shift_x, shift_z);
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

    /*
     * Pin the framing to the player alone, measured with the graphic detached.
     * Merging the arc in more than doubles the combined bounds, and the framing
     * lifts by half the height it measures, so leaving this to the renderer
     * rescales and shifts the player between one cell of the contact sheet and
     * the next — under the very thing being compared.
     */
    ev_set_spot_state(height, -1);
    ev_pose(-1);
    int player_height = ev_model_height();
    ev_set_frame_height(player_height);

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

    /*
     * Every weapon vertex, in every frame, before anything is decided about
     * which of them matter. Choosing the sweeping band needs the whole
     * animation, so it cannot be done in the same pass that measures it.
     */
    ev_set_spot_state(height, -1); /* detached: the plain player model */
    struct BladeSelection sel;
    struct Vec3* track =
        calloc((size_t)body_frames * (size_t)weapon_vertex_count, sizeof(struct Vec3));
    if( !track )
        return 1;
    for( int b = 0; b < body_frames; b++ )
    {
        ev_pose(b);
        struct ToriDraw_Model* m = ev_drawn_model();
        for( int i = 0; i < weapon_vertex_count; i++ )
        {
            int v = weapon_vertex_first + i;
            struct Vec3* dst = &track[(size_t)b * weapon_vertex_count + i];
            dst->x = m->vertices_x[v];
            dst->y = m->vertices_y[v];
            dst->z = m->vertices_z[v];
        }
    }
    if( !select_blade(track, body_frames, weapon_vertex_first, weapon_vertex_count, &sel) )
    {
        fprintf(stderr, "no part of obj %d's wear model moves during sequence %d\n", weapon_obj,
                attack_seq);
        return 1;
    }
    printf("weapon sweep: %d of %d vertices move within 40%% of the fastest, and the\n"
           "              fastest travels %.0f units over the swing. That set spans %.0f\n"
           "              units — the streak is being compared against a region this\n"
           "              size, not a point, so expect a residual of the same order.\n\n",
           sel.head_count, weapon_vertex_count, sel.tip_path, sel.span);
    for( int b = 0; b < body_frames; b++ )
    {
        ev_pose(b);
        blade[b] = measure_blade(ev_drawn_model(), weapon_vertex_first, weapon_vertex_count, &sel);
    }
    free(track);

    /* The independence check. Two arbitrary frames, both halves measured out of
     * the same merged, posed model, compared against the tables above. */
    {
        int b = body_frames / 2;
        int f = spot_frames / 2;
        ev_set_spot_state(height, f);
        ev_pose(b);
        struct ArcSample a2 = measure_arc(ev_drawn_model(), ev_spot_vertex_first(), alpha_visible);
        struct BladeSample b2 =
            measure_blade(ev_drawn_model(), weapon_vertex_first, weapon_vertex_count, &sel);
        double da = dist3(a2.centroid, arc[f].centroid);
        double db = dist3(b2.head, blade[b].head);
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

    printf("cycle  bfrm  blade head (x,y,z)    travel   gfrm  lit/all  arc centroid (x,z)   gap\n");
    printf("-----  ----  --------------------  -------  ----  -------  -------------------  ------\n");

    struct Vec3 prev_head = { 0, 0, 0 };
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
            travel = dist3(blade[b].head, prev_head);
        prev_head = blade[b].head;
        have_prev = 1;
        if( travel > peak_travel )
        {
            peak_travel = travel;
            peak_travel_cycle = t;
        }

        printf("%5d  %4d  %6.0f %6.0f %6.0f  %7.1f", t, b, blade[b].head.x, blade[b].head.y,
               blade[b].head.z, travel);
        if( f < 0 )
            printf("     -        -  %19s  %6s\n", "-", "-");
        else
        {
            double gap = dist_xz(blade[b].head, arc[f].centroid);
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
    printf("largest pose change at cycle %d (%.0f units in one cycle) — the strike\n",
           peak_travel_cycle, peak_travel);
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

    /* ---- along the arc and across it ------------------------------------- */

    /*
     * Timing and placement have to be separated before either can be answered,
     * and plain distance cannot separate them: if the graphic sits half a tile
     * to one side, every delay scores about that half tile and the minimum is
     * noise on top of a constant. The first attempt here did exactly that and
     * produced a sweep that varied by 13% end to end — a number with a shape
     * but no signal in it.
     *
     * The arc is a long thin crescent, so it has an obvious axis, and the two
     * questions fall apart along it:
     *
     *   ALONG   how far the lit segment has travelled down the crescent versus
     *           how far the blade has. This is timing, and a lateral offset
     *           does not touch it.
     *   ACROSS  the perpendicular gap. This is placement, and it is very nearly
     *           constant with time, which is what makes it a rigid offset the
     *           asset can absorb.
     *
     * The axis is the principal direction of the graphic's own vertices in the
     * XZ plane — the closed-form eigenvector of a 2x2 covariance, no iteration.
     * The mesh is static (`sp_d_halberd_glow` animates alpha and nothing else),
     * so one axis serves every frame.
     */
    double axis_x = 0, axis_z = 1, arc_cx = 0, arc_cz = 0;
    {
        ev_set_spot_state(height, 0);
        ev_pose(-1);
        struct ToriDraw_Model* m = ev_drawn_model();
        int first = ev_spot_vertex_first();
        int n = 0;
        double sxx = 0, sxz = 0, szz = 0;
        if( m && first >= 0 )
        {
            for( int v = first; v < m->vertex_count; v++, n++ )
            {
                arc_cx += m->vertices_x[v];
                arc_cz += m->vertices_z[v];
            }
            if( n )
            {
                arc_cx /= n;
                arc_cz /= n;
                for( int v = first; v < m->vertex_count; v++ )
                {
                    double dx = m->vertices_x[v] - arc_cx;
                    double dz = m->vertices_z[v] - arc_cz;
                    sxx += dx * dx;
                    sxz += dx * dz;
                    szz += dz * dz;
                }
                /* Larger eigenvalue of [[sxx,sxz],[sxz,szz]], then its vector. */
                double tr = sxx + szz;
                double det = sxx * szz - sxz * sxz;
                double lam = tr / 2 + sqrt(fmax(0.0, tr * tr / 4 - det));
                double ex = sxz, ez = lam - sxx;
                if( fabs(ex) + fabs(ez) < 1e-9 )
                {
                    ex = 1;
                    ez = 0;
                }
                double len = sqrt(ex * ex + ez * ez);
                axis_x = ex / len;
                axis_z = ez / len;
            }
        }
    }
    /* Perpendicular, right-hand: (x,z) -> (-z,x). */
    double perp_x = -axis_z, perp_z = axis_x;
    printf("graphic axis: (%+.2f, %+.2f) in model xz, centred at (%.0f, %.0f)\n", axis_x, axis_z,
           arc_cx, arc_cz);

    /*
     * The graphic's pose relative to the player, in words.
     *
     * A slash that trails a swing lies TANGENT — its long axis across the
     * player's facing, its body out in front, the way a blade passes. One lying
     * RADIAL runs front-to-back through the player instead, and reads as a
     * quarter turn out however well its timing is tuned. This is stated
     * separately from every gap and residual below because no amount of delay
     * or translation fixes it: it is the spotanim record's rotation opcode, or
     * the wrong one of the four pre-rotated compass models.
     *
     * Facing at yaw 0 is -z, so the tangential component of the axis is |x| and
     * the forward position of the centre is -z.
     */
    {
        double tangential = fabs(axis_x);
        double forward = -arc_cz;
        printf("graphic pose: axis is %s (%.0f%% across the facing direction)\n",
               tangential > 0.7 ? "TANGENT — across the swing, as a slash should lie"
                                : "RADIAL — front-to-back through the player, a quarter turn out",
               tangential * 100);
        printf("              centre sits %.0f units %s and %.0f units to the %s\n", fabs(forward),
               forward >= 0 ? "IN FRONT" : "BEHIND", fabs(arc_cx),
               arc_cx < 0 ? "player's right" : "player's left");
    }

#define ALONG(p) (((p).x - arc_cx) * axis_x + ((p).z - arc_cz) * axis_z)
#define ACROSS(p) (((p).x - arc_cx) * perp_x + ((p).z - arc_cz) * perp_z)

    /*
     * How much overlap the best-placed delay can possibly achieve, so the sweep
     * can reject candidates that score well only because they barely overlap.
     *
     * Without this the sweep's winner was a delay of 54 on 13 paired cycles: it
     * pushes almost the whole graphic past the end of the swing, leaving a
     * handful of cycles that happen to agree, and a mean over a handful beats a
     * mean over fifty every time. The tail of a sweep is where that failure
     * lives, and it looks exactly like a result.
     */
    int max_paired = 0;
    for( int d = 0; d <= body_total; d++ )
    {
        int paired = 0;
        for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            int f = frame_at_cycle(spot_delays, spot_frames, t - d);
            if( b >= 0 && f >= 0 && arc[f].visible_faces )
                paired++;
        }
        if( paired > max_paired )
            max_paired = paired;
    }
    int min_paired = (max_paired * 3) / 5;

    printf("\ndelay sweep — mean ALONG-axis mismatch, which is timing alone\n");
    printf("(candidates overlapping fewer than %d of a possible %d cycles are not judged)\n",
           min_paired, max_paired);
    printf("delay  paired  along err  across (const)  total gap\n");
    int best_delay = delay;
    double best_score = 1e18;
    int best_paired = 0;
    for( int d = 0; d <= body_total; d++ )
    {
        double along = 0, across = 0, total = 0;
        int paired = 0;
        for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            int f = frame_at_cycle(spot_delays, spot_frames, t - d);
            if( b < 0 || f < 0 || arc[f].visible_faces == 0 )
                continue;
            along += fabs(ALONG(blade[b].head) - ALONG(arc[f].centroid));
            across += fabs(ACROSS(blade[b].head) - ACROSS(arc[f].centroid));
            total += dist_xz(blade[b].head, arc[f].centroid);
            paired++;
        }
        if( paired < min_paired )
            continue;
        double mean = along / paired;
        if( mean < best_score )
        {
            best_score = mean;
            best_delay = d;
            best_paired = paired;
        }
        if( d % 4 == 0 || d == delay )
            printf("%5d  %6d  %9.1f  %14.1f  %9.1f%s\n", d, paired, mean, across / paired,
                   total / paired, d == delay ? "  <- shipped" : "");
    }
    printf("\nbest delay %d (%d paired cycles, mean along-axis error %.1f units)\n", best_delay,
           best_paired, best_score);

    /*
     * A landmark check, because the sweep above is a mean over the whole
     * animation and a mean can be minimised by a candidate that is wrong
     * everywhere by a similar amount.
     *
     * Two events, each identified independently and each checkable by hand
     * against the two frame tables printed earlier:
     *
     *   the strike        the largest pose change in the swing — the cycle at
     *                     which the blade snaps from one side of the player to
     *                     the other
     *   the full arc      the first cycle of the graphic's own timeline at
     *                     which every one of its segments is lit
     *
     * A streak whose brightest moment is not the moment of the strike is late
     * or early by exactly the difference, and that difference is the delay.
     * Agreement between this and the sweep is the reason to believe either.
     */
    {
        int arc_peak_own = -1;
        int arc_peak_lit = 0;
        int acc = 0;
        for( int f = 0; f < spot_frames; f++ )
        {
            if( arc[f].visible_faces > arc_peak_lit )
            {
                arc_peak_lit = arc[f].visible_faces;
                arc_peak_own = acc;
            }
            acc += spot_delays[f] > 0 ? spot_delays[f] : 1;
        }
        /*
         * When the graphic first shows anything at all, in its own timeline.
         * Not frame 0: this sequence holds every segment fully transparent for
         * its first four frames, so a graphic "played at delay 16" does not
         * appear until cycle 26, and reading the delay as the moment of
         * appearance is wrong by those ten cycles.
         */
        int arc_first_own = -1;
        acc = 0;
        for( int f = 0; f < spot_frames; f++ )
        {
            if( arc[f].visible_faces && arc_first_own < 0 )
                arc_first_own = acc;
            acc += spot_delays[f] > 0 ? spot_delays[f] : 1;
        }

        /*
         * When the blade first comes in front of the player.
         *
         * Forward is -z at yaw 0. "In front" is taken as the first cycle past
         * halfway to the blade's own deepest forward reach, rather than any
         * absolute distance, because how far in front a weapon reaches is a
         * property of the weapon.
         */
        double deepest = 0;
        for( int b = 0; b < body_frames; b++ )
            if( blade[b].head.z < deepest )
                deepest = blade[b].head.z;
        /*
         * The crossing that counts is the one AFTER the wind-up, not the first
         * one in the sequence: the blade already starts slightly forward, so
         * "the first cycle past halfway" fires at cycle 1 and reports a
         * negative delay. Find the furthest-back cycle first — the top of the
         * wind-up — and take the crossing after that.
         */
        int back_cycle = 0;
        double furthest_back = -1e18;
        for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            if( b >= 0 && blade[b].head.z > furthest_back )
            {
                furthest_back = blade[b].head.z;
                back_cycle = t;
            }
        }
        int front_cycle = -1;
        for( int t = back_cycle + 1; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            if( b >= 0 && blade[b].head.z <= deepest * 0.5 )
            {
                front_cycle = t;
                break;
            }
        }

        printf("\nlandmark check — two events, each checkable by hand in the tables above\n");
        if( front_cycle >= 0 && arc_first_own >= 0 )
            printf("  the wind-up peaks at cycle %d (blade furthest back, z %+.0f) and the blade\n"
                   "  comes in front at cycle %d (z past %.0f, half its %.0f reach); the graphic\n"
                   "  first shows anything %d cycles in, so they meet at delay %d\n",
                   back_cycle, furthest_back, front_cycle, deepest * 0.5, deepest, arc_first_own,
                   front_cycle - arc_first_own);
        if( arc_peak_own >= 0 && peak_travel_cycle >= 0 )
            printf("  the strike (largest pose change, %.0f units) is at cycle %d;\n"
                   "  the graphic is fully lit %d cycles in (%d/%d segments), so they meet at "
                   "delay %d\n",
                   peak_travel, peak_travel_cycle, arc_peak_own, arc_peak_lit, arc[0].total_faces,
                   peak_travel_cycle - arc_peak_own);
        printf("  the along-axis sweep independently chose %d; shipped is %d\n", best_delay, delay);

        /*
         * Which way each one travels along the axis.
         *
         * Two candidates a half turn apart lie in the same place and cover the
         * same ground, so every distance in this report is near enough
         * identical for both — and one of them plays the streak backwards
         * through the swing. Nothing else here can tell them apart, and
         * backwards is not a subtle defect once it is on screen.
         */
        /*
         * Both spans are chosen WITHOUT reference to the delay, or the test is
         * circular: the sweep picks the delay that best lines the two up, so
         * measuring over the window that delay produces will report agreement
         * whatever the candidate. A first attempt did exactly that and called
         * all four orientations correct, including the two a quarter turn out.
         *
         * The blade's span is the swing itself — the top of the wind-up to its
         * furthest-forward reach. The streak's is its own lit span, which is a
         * property of the sequence and of nothing else.
         */
        int deep_cycle = back_cycle;
        double deepest_seen = 1e18;
        for( int t = back_cycle; t < body_total && t < MAX_CYCLES; t++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            if( b >= 0 && blade[b].head.z < deepest_seen )
            {
                deepest_seen = blade[b].head.z;
                deep_cycle = t;
            }
        }
        int bb = frame_at_cycle(body_delays, body_frames, back_cycle);
        int bf = frame_at_cycle(body_delays, body_frames, deep_cycle);
        int first_lit = -1, last_lit = -1;
        for( int f = 0; f < spot_frames; f++ )
            if( arc[f].visible_faces )
            {
                if( first_lit < 0 )
                    first_lit = f;
                last_lit = f;
            }
        if( bb >= 0 && bf >= 0 && first_lit >= 0 && last_lit > first_lit )
        {
            double bd = ALONG(blade[bf].head) - ALONG(blade[bb].head);
            double ad = ALONG(arc[last_lit].centroid) - ALONG(arc[first_lit].centroid);
            printf("  direction along that axis — blade over its swing (cycles %d..%d) %+.0f,\n"
                   "  streak over its own lit span %+.0f -> %s\n",
                   back_cycle, deep_cycle, bd, ad,
                   (bd > 0) == (ad > 0)
                       ? "SAME WAY"
                       : "OPPOSED — this copy plays the streak backwards through the swing");
        }
    }

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
            sx += blade[b].head.x - arc[f].centroid.x;
            sy += blade[b].head.y - arc[f].centroid.y;
            sz += blade[b].head.z - arc[f].centroid.z;
            paired++;
        }
        if( paired )
        {
            double dx = sx / paired, dy = sy / paired, dz = sz / paired;
            /*
             * The across-axis component is the part that is genuinely a rigid
             * offset; the along-axis remainder is whatever the chosen delay
             * could not remove, and moving the asset by it would trade a timing
             * error for a placement error. So the recommendation is the
             * across-axis component projected back into model x and z, and the
             * raw mean is printed beside it to show how much was discarded.
             */
            struct Vec3 mean = { dx, dy, dz };
            double across = mean.x * perp_x + mean.z * perp_z;
            double rec_dx = across * perp_x;
            double rec_dz = across * perp_z;

            printf("\nresidual offset at delay %d, averaged over %d cycles:\n", best_delay, paired);
            printf("  raw mean       dx %+7.1f  dz %+7.1f  dy %+7.1f\n", dx, dz, dy);
            printf("  across-axis    %+7.1f units (%+.2f tiles) -> dx %+.1f, dz %+.1f\n", across,
                   across / 128.0, rec_dx, rec_dz);
            printf("  along-axis     %+7.1f units, left to the delay rather than the asset\n",
                   mean.x * axis_x + mean.z * axis_z);
            /*
             * Height is the one term that INVERTS. The merge translates the
             * graphic by -height in y, so the drawn y is spot_y - height; to
             * move the drawn y by dy the height must change by -dy. Adding dy
             * moves it the wrong way by twice the error, which reads as the
             * knob being backwards rather than as a sign slip.
             */
            printf("  height         %d -> %d (dy %+.1f, and height subtracts)\n", height,
                   height - (int)(dy + (dy < 0 ? -0.5 : 0.5)), dy);
            printf("\n  %s\n", rec_dx < 0 ? "the graphic must move to the player's RIGHT (-x)"
                                          : "the graphic must move to the player's LEFT (+x)");
            printf("  %s\n", rec_dz < 0 ? "and FORWARD (-z, the way they face at yaw 0)"
                                        : "and BACK (+z)");
            printf("\nto apply the lateral part:  python3 tools/shift_halberd_arc.py %d\n",
                   (int)(rec_dx + (rec_dx < 0 ? -0.5 : 0.5)));
            printf("  (that script only moves x; a dz of %+.0f needs the z stream too)\n", rec_dz);
            printf("to apply the timing and height: spotanim_pl(..., %d, %d)\n",
                   height - (int)(dy + (dy < 0 ? -0.5 : 0.5)), best_delay);
        }
    }
#undef ALONG
#undef ACROSS

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

        /* Wide enough for the whole graphic, not just the player. The arc is
         * over two tiles long and a zoom framed on the player alone crops the
         * ends off exactly where the alignment question lives. */
        int zoom = zoom_arg > 0 ? zoom_arg : player_height * 6;
        if( zoom < 400 )
            zoom = 400;

        int cell = 0;
        for( int t = 0; t < body_total && cell < cells * rows; t += step, cell++ )
        {
            int b = frame_at_cycle(body_delays, body_frames, t);
            int f = frame_at_cycle(spot_delays, spot_frames, t - delay);
            int cx = (cell % cells) * side;
            int cy = (cell / cells) * side;
            int mx, my;
            if( b < 0 )
                break;
            ev_set_spot_state(height, f);
            /* Straight down: pitch 512 of 2048 is a quarter turn, so the orbit
             * sits directly above the model and screen x/y read as world x/z. */
            blit(top, sheet_w, cx, cy, ev_render(side, side, 0, 512, zoom, b), side);

            /*
             * The two measured points, marked on the picture they were measured
             * from. This is the check that the table above and the render agree
             * about which model they are describing: a cyan cross that does not
             * sit on the blade means the vertex slice is wrong, and every number
             * downstream of it is describing the wrong geometry.
             */
            project_top_down(blade[b].head, side, zoom, player_height / 2, &mx, &my);
            cross(top, sheet_w, sheet_h, cx + mx, cy + my, 6, 0x0000FFFF); /* blade: cyan */
            if( f >= 0 && arc[f].visible_faces )
            {
                project_top_down(arc[f].centroid, side, zoom, player_height / 2, &mx, &my);
                cross(top, sheet_w, sheet_h, cx + mx, cy + my, 6, 0x0000FF00); /* arc: green */
            }
            /* The player's own origin, so the two crosses can be read as offsets
             * from the player rather than as positions on a canvas. */
            {
                struct Vec3 origin = { 0, 0, 0 };
                project_top_down(origin, side, zoom, player_height / 2, &mx, &my);
                cross(top, sheet_w, sheet_h, cx + mx, cy + my, 3, 0x00FFFFFF);
            }

            /* And from the side, at the yaw a player fighting south is drawn at,
             * because "is it at the right height" is not visible from above. */
            blit(sid, sheet_w, cx, cy, ev_render(side, side, 0, 100, zoom, b), side);
            rule(top, sheet_w, sheet_h, cx, 0x00404040);
            rule(sid, sheet_w, sheet_h, cx, 0x00404040);
        }

        /*
         * The trace: everything the swing does, on one overhead diagram in world
         * units.
         *
         * A contact sheet answers "what does it look like at cycle N" and cannot
         * answer "do the two paths coincide", because the answer is spread over
         * forty cells. Here the blade's path and the lit graphic's path are two
         * curves on the same axes, over a one-tile grid, and whether they lie on
         * top of each other is the whole question, settled at a glance.
         */
        {
            int pw = 720, ph = 720;
            int* pix = calloc((size_t)pw * ph, sizeof(int));
            /* Fit the diagram to whatever the two paths and the arc mesh span. */
            double span = 8;
            for( int f = 0; f < spot_frames; f++ )
                if( arc[f].visible_faces )
                {
                    if( fabs(arc[f].centroid.x) > span )
                        span = fabs(arc[f].centroid.x);
                    if( fabs(arc[f].centroid.z) > span )
                        span = fabs(arc[f].centroid.z);
                }
            for( int b = 0; b < body_frames; b++ )
            {
                if( fabs(blade[b].head.x) > span )
                    span = fabs(blade[b].head.x);
                if( fabs(blade[b].head.z) > span )
                    span = fabs(blade[b].head.z);
            }
            span *= 1.15;
            double sc = (pw / 2) / span;
#define PX(wx) (pw / 2 + (int)((wx) * sc))
#define PY(wz) (ph / 2 - (int)((wz) * sc))

            for( int i = 0; i < pw * ph; i++ )
                pix[i] = 0x00141821;
            /* A one-tile grid: 128 model units, so a reader can convert any gap
             * on the picture into tiles without the legend. */
            for( int g = -8; g <= 8; g++ )
            {
                int gx = PX(g * 128.0), gy = PY(g * 128.0);
                for( int i = 0; i < ph; i++ )
                    plot(pix, pw, ph, gx, i, g == 0 ? 0x00505a6e : 0x00232937);
                for( int i = 0; i < pw; i++ )
                    plot(pix, pw, ph, i, gy, g == 0 ? 0x00505a6e : 0x00232937);
            }

            /* The graphic's full mesh at its brightest frame, so the paths can
             * be read against the shape they are supposed to trace. */
            {
                int brightest = 0;
                for( int f = 0; f < spot_frames; f++ )
                    if( arc[f].visible_faces > arc[brightest].visible_faces )
                        brightest = f;
                ev_set_spot_state(height, brightest);
                ev_pose(-1);
                struct ToriDraw_Model* m = ev_drawn_model();
                int first = ev_spot_vertex_first();
                if( m && first >= 0 )
                    for( int v = first; v < m->vertex_count; v++ )
                        plot(pix, pw, ph, PX((double)m->vertices_x[v]),
                             PY((double)m->vertices_z[v]), 0x00803038);
            }

            /* Blade path (cyan) and lit-graphic path (green), cycle by cycle. */
            int have_b = 0, have_a = 0, bx = 0, by = 0, ax = 0, ay = 0;
            for( int t = 0; t < body_total && t < MAX_CYCLES; t++ )
            {
                int b = frame_at_cycle(body_delays, body_frames, t);
                int f = frame_at_cycle(spot_delays, spot_frames, t - delay);
                if( b < 0 )
                    break;
                int nx = PX(blade[b].head.x), ny = PY(blade[b].head.z);
                if( have_b )
                    line(pix, pw, ph, bx, by, nx, ny, 0x0000C0D0);
                bx = nx;
                by = ny;
                have_b = 1;
                if( f >= 0 && arc[f].visible_faces )
                {
                    int gx = PX(arc[f].centroid.x), gy = PY(arc[f].centroid.z);
                    if( have_a )
                        line(pix, pw, ph, ax, ay, gx, gy, 0x0000C000);
                    /* A tie line each cycle: its length IS the gap the table
                     * reports, and a fan of long ties is what "out of step"
                     * looks like. */
                    line(pix, pw, ph, nx, ny, gx, gy, 0x00404820);
                    ax = gx;
                    ay = gy;
                    have_a = 1;
                }
            }
            cross(pix, pw, ph, PX(0.0), PY(0.0), 6, 0x00FFFFFF);

            char ppath[1024];
            snprintf(ppath, sizeof(ppath), "%s_plot.bmp", out_prefix);
            bmp_write_file(ppath, pix, pw, ph);
            free(pix);
#undef PX
#undef PY
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s_top.bmp", out_prefix);
        bmp_write_file(path, top, sheet_w, sheet_h);
        snprintf(path, sizeof(path), "%s_side.bmp", out_prefix);
        bmp_write_file(path, sid, sheet_w, sheet_h);
        printf("\nwrote %s_top.bmp / _side.bmp (%d cells, every %d cycle(s), %dx%d)\n"
               "  and %s_plot.bmp — the overhead trace, one-tile grid,\n"
               "  cyan = blade head, green = lit graphic, white = the player's own origin\n",
               out_prefix, cell, step, side, side, out_prefix);
        free(top);
        free(sid);
    }

    ToriDraw_ModelFree(body);
    ToriDraw_ModelFree(spot);
    ToriDraw_AnimationFree(body_anim);
    ToriDraw_AnimationFree(spot_anim);
    return 0;
}
