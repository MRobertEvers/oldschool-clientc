#include "pg_model.h"

#include "rscache.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The client's 2048-entry 16.16 sin/cos. Frame rotations are 1/256 of a turn
 * scaled onto this table (the `* 8` below), which is what makes a rotation of
 * 64 a quarter turn. */
static int g_sin[2048];
static int g_cos[2048];
static int g_trig_ready = 0;

static void
trig_init(void)
{
    if( g_trig_ready )
        return;
    for( int i = 0; i < 2048; i++ )
    {
        g_sin[i] = (int)(65536.0 * sin((double)i * 0.0030679615757712823));
        g_cos[i] = (int)(65536.0 * cos((double)i * 0.0030679615757712823));
    }
    g_trig_ready = 1;
}

/* The pivot, module state exactly as the reference keeps it static on
 * ModelDefinition: a rotate reads whatever the preceding origin left behind. */
static int g_anim_offset_x = 0;
static int g_anim_offset_y = 0;
static int g_anim_offset_z = 0;

void
pg_model_anim_offset(int* x, int* y, int* z)
{
    *x = g_anim_offset_x;
    *y = g_anim_offset_y;
    *z = g_anim_offset_z;
}

struct PG_ModelDef*
pg_model_new(void)
{
    struct PG_ModelDef* def = calloc(1, sizeof(*def));
    if( def )
        def->id = -1;
    return def;
}

void
pg_model_free(struct PG_ModelDef* def)
{
    if( !def )
        return;
    free(def->vertices_x);
    free(def->vertices_y);
    free(def->vertices_z);
    free(def->face_a);
    free(def->face_b);
    free(def->face_c);
    free(def->vertex_skins);
    free(def->orig_x);
    free(def->orig_y);
    free(def->orig_z);
    free(def->face_colors);
    free(def->face_alphas);
    free(def->face_priorities);
    free(def->face_render_types);
    if( def->groups )
    {
        for( int i = 0; i < def->group_count; i++ )
            free(def->groups[i]);
        free(def->groups);
    }
    free(def->group_counts);
    free(def);
}

struct PG_ModelDef*
pg_model_from_rscache(const struct RSCache_Model* src, int id)
{
    struct PG_ModelDef* def = pg_model_new();
    int shift;

    if( !def )
        return NULL;
    def->id = id;
    if( !src )
        return def;

    def->vertex_count = src->vertex_count;
    def->face_count = src->face_count;
    def->priority = src->model_priority;

    /* rs-map-viewer's decodeV1 ends `if (version >= 13) scaleDown(2)`. rscache
     * keeps the wire values so its encoders round-trip; the shift belongs to the
     * consumer, and this is the consumer. */
    shift = src->format_version >= 13 ? 2 : 0;

    if( def->vertex_count > 0 )
    {
        size_t n = (size_t)def->vertex_count * sizeof(int);
        def->vertices_x = malloc(n);
        def->vertices_y = malloc(n);
        def->vertices_z = malloc(n);
        if( !def->vertices_x || !def->vertices_y || !def->vertices_z )
        {
            pg_model_free(def);
            return NULL;
        }
        for( int i = 0; i < def->vertex_count; i++ )
        {
            def->vertices_x[i] = src->vertices_x[i] >> shift;
            def->vertices_y[i] = src->vertices_y[i] >> shift;
            def->vertices_z[i] = src->vertices_z[i] >> shift;
        }
        if( src->vertex_bone_map )
        {
            def->vertex_skins = malloc(n);
            if( !def->vertex_skins )
            {
                pg_model_free(def);
                return NULL;
            }
            for( int i = 0; i < def->vertex_count; i++ )
                def->vertex_skins[i] = src->vertex_bone_map[i];
        }
    }

    if( def->face_count > 0 )
    {
        size_t n = (size_t)def->face_count * sizeof(int);
        def->face_a = malloc(n);
        def->face_b = malloc(n);
        def->face_c = malloc(n);
        def->face_colors = malloc((size_t)def->face_count * sizeof(uint16_t));
        if( !def->face_a || !def->face_b || !def->face_c || !def->face_colors )
        {
            pg_model_free(def);
            return NULL;
        }
        memcpy(def->face_a, src->face_indices_a, n);
        memcpy(def->face_b, src->face_indices_b, n);
        memcpy(def->face_c, src->face_indices_c, n);
        for( int i = 0; i < def->face_count; i++ )
            def->face_colors[i] = src->face_colors ? src->face_colors[i] : 0;

        if( src->face_alphas )
        {
            def->face_alphas = malloc((size_t)def->face_count);
            if( def->face_alphas )
                memcpy(def->face_alphas, src->face_alphas, (size_t)def->face_count);
        }
        if( src->face_priorities )
        {
            def->face_priorities = malloc((size_t)def->face_count);
            if( def->face_priorities )
                memcpy(def->face_priorities, src->face_priorities, (size_t)def->face_count);
        }
        if( src->face_infos )
        {
            def->face_render_types = malloc((size_t)def->face_count);
            if( def->face_render_types )
                memcpy(def->face_render_types, src->face_infos, (size_t)def->face_count);
        }
    }
    return def;
}

void
pg_model_recolour(struct PG_ModelDef* def, uint16_t from, uint16_t to)
{
    if( !def || !def->face_colors )
        return;
    for( int i = 0; i < def->face_count; i++ )
        if( def->face_colors[i] == from )
            def->face_colors[i] = to;
}

/* ---- merge --------------------------------------------------------------- */

/*
 * Deduplicate by position, exactly as the deob does.
 *
 * That the search is linear over everything accepted so far is not an oversight
 * on either side: the merge only ever runs over a worn outfit, a few thousand
 * vertices, and the dedup is what makes a shared seam animate as one piece
 * instead of splitting when the two source models bend on different labels.
 */
static int
merge_vertex(
    struct PG_ModelDef* out,
    const struct PG_ModelDef* current,
    int index)
{
    int x = current->vertices_x[index];
    int y = current->vertices_y[index];
    int z = current->vertices_z[index];

    for( int i = 0; i < out->vertex_count; i++ )
    {
        if( x == out->vertices_x[i] && y == out->vertices_y[i] && z == out->vertices_z[i] )
            return i;
    }

    out->vertices_x[out->vertex_count] = x;
    out->vertices_y[out->vertex_count] = y;
    out->vertices_z[out->vertex_count] = z;
    if( current->vertex_skins && out->vertex_skins )
        out->vertex_skins[out->vertex_count] = current->vertex_skins[index];
    return out->vertex_count++;
}

struct PG_ModelDef*
pg_model_merge(struct PG_ModelDef** defs, int count)
{
    struct PG_ModelDef* out = pg_model_new();
    bool any_render_types = false;
    bool any_priorities = false;
    bool any_alphas = false;
    int total_vertices = 0;
    int total_faces = 0;

    if( !out )
        return NULL;
    out->priority = -1;

    for( int i = 0; i < count; i++ )
    {
        struct PG_ModelDef* cur = defs[i];
        if( !cur )
            continue;
        total_vertices += cur->vertex_count;
        total_faces += cur->face_count;

        if( cur->face_priorities )
        {
            any_priorities = true;
        }
        else
        {
            if( out->priority == -1 )
                out->priority = cur->priority;
            if( out->priority != cur->priority )
                any_priorities = true;
        }
        any_render_types = any_render_types || cur->face_render_types != NULL;
        any_alphas = any_alphas || cur->face_alphas != NULL;
    }

    if( total_vertices > 0 )
    {
        out->vertices_x = calloc((size_t)total_vertices, sizeof(int));
        out->vertices_y = calloc((size_t)total_vertices, sizeof(int));
        out->vertices_z = calloc((size_t)total_vertices, sizeof(int));
        out->vertex_skins = calloc((size_t)total_vertices, sizeof(int));
    }
    if( total_faces > 0 )
    {
        out->face_a = calloc((size_t)total_faces, sizeof(int));
        out->face_b = calloc((size_t)total_faces, sizeof(int));
        out->face_c = calloc((size_t)total_faces, sizeof(int));
        out->face_colors = calloc((size_t)total_faces, sizeof(uint16_t));
        if( any_render_types )
            out->face_render_types = calloc((size_t)total_faces, 1);
        if( any_priorities )
            out->face_priorities = calloc((size_t)total_faces, 1);
        if( any_alphas )
            out->face_alphas = calloc((size_t)total_faces, 1);
    }

    out->vertex_count = 0;
    out->face_count = 0;

    for( int i = 0; i < count; i++ )
    {
        struct PG_ModelDef* cur = defs[i];
        if( !cur )
            continue;
        for( int f = 0; f < cur->face_count; f++ )
        {
            int dst = out->face_count;
            if( any_render_types && out->face_render_types )
                out->face_render_types[dst] = cur->face_render_types ? cur->face_render_types[f] : 0;
            if( any_priorities && out->face_priorities )
                out->face_priorities[dst] =
                    cur->face_priorities ? cur->face_priorities[f] : (uint8_t)cur->priority;
            if( any_alphas && out->face_alphas )
                out->face_alphas[dst] = cur->face_alphas ? cur->face_alphas[f] : 0;

            out->face_colors[dst] = cur->face_colors ? cur->face_colors[f] : 0;
            out->face_a[dst] = merge_vertex(out, cur, cur->face_a[f]);
            out->face_b[dst] = merge_vertex(out, cur, cur->face_b[f]);
            out->face_c[dst] = merge_vertex(out, cur, cur->face_c[f]);
            out->face_count++;
        }
    }
    return out;
}

/* ---- bone groups --------------------------------------------------------- */

void
pg_model_compute_groups(struct PG_ModelDef* def)
{
    int counts[256];
    int num_groups = 0;

    if( !def || !def->vertex_skins )
        return;

    memset(counts, 0, sizeof(counts));
    for( int i = 0; i < def->vertex_count; i++ )
    {
        int label = def->vertex_skins[i];
        if( label < 0 || label > 255 )
            continue;
        counts[label]++;
        if( label > num_groups )
            num_groups = label;
    }

    def->group_count = num_groups + 1;
    def->groups = calloc((size_t)def->group_count, sizeof(int*));
    def->group_counts = calloc((size_t)def->group_count, sizeof(int));
    if( !def->groups || !def->group_counts )
    {
        def->group_count = 0;
        return;
    }
    for( int i = 0; i < def->group_count; i++ )
    {
        def->groups[i] = counts[i] > 0 ? malloc((size_t)counts[i] * sizeof(int)) : NULL;
        def->group_counts[i] = 0;
    }
    for( int i = 0; i < def->vertex_count; i++ )
    {
        int label = def->vertex_skins[i];
        if( label < 0 || label >= def->group_count || !def->groups[label] )
            continue;
        def->groups[label][def->group_counts[label]++] = i;
    }

    free(def->vertex_skins);
    def->vertex_skins = NULL;
}

/* ---- animation ----------------------------------------------------------- */

static void
capture_rest_pose(struct PG_ModelDef* def)
{
    size_t n;
    if( def->orig_x || def->vertex_count <= 0 )
        return;
    n = (size_t)def->vertex_count * sizeof(int);
    def->orig_x = malloc(n);
    def->orig_y = malloc(n);
    def->orig_z = malloc(n);
    if( !def->orig_x || !def->orig_y || !def->orig_z )
        return;
    memcpy(def->orig_x, def->vertices_x, n);
    memcpy(def->orig_y, def->vertices_y, n);
    memcpy(def->orig_z, def->vertices_z, n);
}

void
pg_model_reset_transformations(struct PG_ModelDef* def)
{
    if( !def )
        return;
    g_anim_offset_x = 0;
    g_anim_offset_y = 0;
    g_anim_offset_z = 0;
    if( def->orig_x && def->vertex_count > 0 )
    {
        size_t n = (size_t)def->vertex_count * sizeof(int);
        memcpy(def->vertices_x, def->orig_x, n);
        memcpy(def->vertices_y, def->orig_y, n);
        memcpy(def->vertices_z, def->orig_z, n);
    }
}

void
pg_model_animate(
    struct PG_ModelDef* def,
    int type,
    const int* labels,
    int label_count,
    int dx,
    int dy,
    int dz)
{
    int* vx;
    int* vy;
    int* vz;

    if( !def || def->vertex_count <= 0 )
        return;
    trig_init();
    capture_rest_pose(def);

    vx = def->vertices_x;
    vy = def->vertices_y;
    vz = def->vertices_z;

    if( type == 0 ) /* reference / origin */
    {
        int count = 0;
        g_anim_offset_x = 0;
        g_anim_offset_y = 0;
        g_anim_offset_z = 0;
        for( int g = 0; g < label_count; g++ )
        {
            int label = labels[g];
            if( label < 0 || label >= def->group_count || !def->groups[label] )
                continue;
            for( int i = 0; i < def->group_counts[label]; i++ )
            {
                int v = def->groups[label][i];
                g_anim_offset_x += vx[v];
                g_anim_offset_y += vy[v];
                g_anim_offset_z += vz[v];
                count++;
            }
        }
        if( count > 0 )
        {
            g_anim_offset_x = dx + g_anim_offset_x / count;
            g_anim_offset_y = dy + g_anim_offset_y / count;
            g_anim_offset_z = dz + g_anim_offset_z / count;
        }
        else
        {
            /* Matching nothing does not skip: the raw frame value becomes an
             * absolute pivot, which is what makes a mis-labelled model stretch
             * rather than simply not move. */
            g_anim_offset_x = dx;
            g_anim_offset_y = dy;
            g_anim_offset_z = dz;
        }
        return;
    }

    for( int g = 0; g < label_count; g++ )
    {
        int label = labels[g];
        if( label < 0 || label >= def->group_count || !def->groups[label] )
            continue;

        for( int i = 0; i < def->group_counts[label]; i++ )
        {
            int v = def->groups[label][i];

            if( type == 1 ) /* translate */
            {
                vx[v] += dx;
                vy[v] += dy;
                vz[v] += dz;
            }
            else if( type == 2 ) /* rotate */
            {
                int pitch = (dx & 255) * 8;
                int yaw = (dy & 255) * 8;
                int roll = (dz & 255) * 8;
                int sn, cs, tmp;

                vx[v] -= g_anim_offset_x;
                vy[v] -= g_anim_offset_y;
                vz[v] -= g_anim_offset_z;

                /* Roll, then pitch, then yaw. Rotations do not commute, and doing
                 * them in index order instead yields a pose that reads as a bad
                 * rig rather than as a bug here. */
                if( roll != 0 )
                {
                    sn = g_sin[roll];
                    cs = g_cos[roll];
                    tmp = (sn * vy[v] + cs * vx[v]) >> 16;
                    vy[v] = (cs * vy[v] - sn * vx[v]) >> 16;
                    vx[v] = tmp;
                }
                if( pitch != 0 )
                {
                    sn = g_sin[pitch];
                    cs = g_cos[pitch];
                    tmp = (cs * vy[v] - sn * vz[v]) >> 16;
                    vz[v] = (sn * vy[v] + cs * vz[v]) >> 16;
                    vy[v] = tmp;
                }
                if( yaw != 0 )
                {
                    sn = g_sin[yaw];
                    cs = g_cos[yaw];
                    tmp = (sn * vz[v] + cs * vx[v]) >> 16;
                    vz[v] = (cs * vz[v] - sn * vx[v]) >> 16;
                    vx[v] = tmp;
                }

                vx[v] += g_anim_offset_x;
                vy[v] += g_anim_offset_y;
                vz[v] += g_anim_offset_z;
            }
            else if( type == 3 ) /* scale */
            {
                vx[v] -= g_anim_offset_x;
                vy[v] -= g_anim_offset_y;
                vz[v] -= g_anim_offset_z;

                vx[v] = dx * vx[v] / 128;
                vy[v] = dy * vy[v] / 128;
                vz[v] = dz * vz[v] / 128;

                vx[v] += g_anim_offset_x;
                vy[v] += g_anim_offset_y;
                vz[v] += g_anim_offset_z;
            }
        }
    }
}
