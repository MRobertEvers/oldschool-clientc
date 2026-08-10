/*
 * Standalone OB3 model viewer — renders a model through the real ToriDraw
 * painter's-algorithm sorter and writes a multi-angle contact sheet BMP.
 *
 * This exists to make face-priority authoring on the imported RS2012 lane an
 * iteration loop instead of a full client boot. It links only rscache,
 * toridraw and the two model adaptors: no cache, no window, no game.
 *
 * The point is the SORT, so it deliberately renders the same way the client
 * does — ToriDraw_RenderModel1Project / 2SortFaces / 3Raster — rather than
 * with any private ordering. Whatever priorities the .ob3 carries are honoured
 * exactly as the client honours them.
 *
 * Build and run:
 *   make -C src rs2012-model-view
 *   src/build/rs2012_model_view --model <a.ob3> [--model <b.ob3>] --out shot.bmp
 *
 * Multiple --model arguments are merged the way an npc's model1/model2 are,
 * through ToriDraw_ModelNewMerge, so the merged priority behaviour is the one
 * under test.
 *
 * Options:
 *   --angles N      yaw steps around the model (default 4)
 *   --pitch N       camera pitch, 0..2047 (default 200, slightly above)
 *   --yaw0 N        first yaw (default 0)
 *   --zoom F        framing multiplier, >1 is closer (default 1.0)
 *   --tile N        tile pixel size (default 320)
 *   --textures      keep face texture ids (default: stripped, no texture map)
 *   --ignore-priorities   render as if the model carried none
 *   --stats         print the face-priority histogram and model shape
 */

#include "datatypes/model.h"

#include "engine/torirs_model_from_rscache.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"

#include <bmp.h>
#include <toridraw.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODELS 16
#define MAX_ANGLES 16
#define MAX_PITCHES 8

/* ToriDraw_ModelDropNonSdTextures is the only thing in the adaptor that reaches
 * a CacheProvider, and this tool never calls it. Satisfying the link here is
 * cheaper than dragging the provider (and its cache) into a file viewer. */
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id);
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id)
{
    (void)provider;
    (void)texture_id;
    return true;
}

static uint8_t*
read_file(const char* path, long* out_size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* bytes;
    long size;

    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 )
    {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if( size <= 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return NULL;
    }
    bytes = (uint8_t*)malloc((size_t)size);
    if( !bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size )
    {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return bytes;
}

static struct ToriDraw_Model*
load_ob3(const char* path)
{
    long size = 0;
    uint8_t* bytes = read_file(path, &size);
    struct RSCache_Model* rs;
    struct ToriRS_Model* mid;
    struct ToriDraw_Model* out;

    if( !bytes )
    {
        fprintf(stderr, "rs2012_model_view: cannot read %s\n", path);
        return NULL;
    }
    rs = RSCache_ModelNewDecode(bytes, (int)size);
    free(bytes);
    if( !rs )
    {
        fprintf(stderr, "rs2012_model_view: cannot decode %s\n", path);
        return NULL;
    }
    /* Moves the arrays out of rs, leaving it hollow but still freeable. */
    mid = ToriRS_ModelFromRSCache(rs);
    RSCache_ModelFree(rs);
    if( !mid )
        return NULL;
    out = ToriDraw_ModelFromToriRS(mid);
    ToriRS_ModelFree(mid);
    return out;
}

static int
face_priority_of(const struct ToriDraw_Model* m, int face)
{
    int byte;
    if( !m->face_priorities )
        return m->model_priority;
    byte = m->face_priorities[face >> 1];
    return (face & 1) ? (byte >> 4) : (byte & 0x0F);
}

static void
print_stats(const struct ToriDraw_Model* m)
{
    int hist[16] = { 0 };
    int min_y = 1 << 30, max_y = -(1 << 30);
    int textured = 0, alpha = 0;

    for( int f = 0; f < m->face_count; f++ )
    {
        hist[face_priority_of(m, f) & 15]++;
        if( m->face_textures && m->face_textures[f] >= 0 )
            textured++;
        if( m->face_alphas && m->face_alphas[f] )
            alpha++;
    }
    for( int v = 0; v < m->vertex_count; v++ )
    {
        if( m->vertices_y[v] < min_y )
            min_y = m->vertices_y[v];
        if( m->vertices_y[v] > max_y )
            max_y = m->vertices_y[v];
    }

    printf(
        "model: %d vertices, %d faces, y %d..%d, textured %d, alpha %d, "
        "per-face priorities %s (model_priority %u)\n",
        m->vertex_count,
        m->face_count,
        min_y,
        max_y,
        textured,
        alpha,
        m->face_priorities ? "yes" : "no",
        (unsigned)m->model_priority);
    printf("  priority histogram:");
    for( int p = 0; p < 13; p++ )
        if( hist[p] )
            printf(" %d=%d", p, hist[p]);
    printf("\n");
}

static void
strip_textures(struct ToriDraw_Model* m)
{
    if( !m->face_textures )
        return;
    for( int f = 0; f < m->face_count; f++ )
        m->face_textures[f] = (faceint_t)-1;
    if( m->face_texture_coords )
        for( int f = 0; f < m->face_count; f++ )
            m->face_texture_coords[f] = (faceint_t)-1;
}

/**
 * Move the model so its bounding-box centre sits on the model origin, and
 * rebuild the bounds cylinder around the result.
 *
 * The viewer orbits by rotating the MODEL (position->pitch/yaw) with the
 * camera left at identity, because the camera-pitch form orbits about the eye
 * and swings a model this tall straight out of frame. Rotating the model works
 * only if the pivot is the model's centre, and an npc model's origin is at its
 * feet, so the recentre is what makes the orbit stay framed. Only the viewer
 * does this; nothing is written back to the .ob3.
 */
static void
recenter_model(struct ToriDraw_Model* m, const int* explicit_center)
{
    int min_x = 1 << 30, max_x = -(1 << 30);
    int min_y = 1 << 30, max_y = -(1 << 30);
    int min_z = 1 << 30, max_z = -(1 << 30);
    int cx, cy, cz, radius2 = 0;

    if( m->vertex_count <= 0 )
        return;

    for( int v = 0; v < m->vertex_count; v++ )
    {
        int x = m->vertices_x[v], y = m->vertices_y[v], z = m->vertices_z[v];
        if( x < min_x ) min_x = x;
        if( x > max_x ) max_x = x;
        if( y < min_y ) min_y = y;
        if( y > max_y ) max_y = y;
        if( z < min_z ) min_z = z;
        if( z > max_z ) max_z = z;
    }
    cx = (min_x + max_x) / 2;
    cy = (min_y + max_y) / 2;
    cz = (min_z + max_z) / 2;
    if( explicit_center )
    {
        cx = explicit_center[0];
        cy = explicit_center[1];
        cz = explicit_center[2];
    }

    min_y = 1 << 30;
    max_y = -(1 << 30);
    for( int v = 0; v < m->vertex_count; v++ )
    {
        int x = (m->vertices_x[v] -= (vertexint_t)cx);
        int y = (m->vertices_y[v] -= (vertexint_t)cy);
        int z = (m->vertices_z[v] -= (vertexint_t)cz);
        int r2 = x * x + z * z;
        if( r2 > radius2 )
            radius2 = r2;
        if( y < min_y ) min_y = y;
        if( y > max_y ) max_y = y;
    }

    if( !m->bounds_cylinder )
        m->bounds_cylinder = calloc(1, sizeof(struct ToriDraw_BoundsCylinder));
    if( m->bounds_cylinder )
    {
        struct ToriDraw_BoundsCylinder* bc = m->bounds_cylinder;
        bc->min_y = min_y;
        bc->max_y = max_y;
        bc->radius = (int)sqrt((double)radius2);
        bc->center_to_bottom_edge = (int)sqrt((double)radius2 + (double)min_y * min_y) + 1;
        bc->center_to_top_edge = (int)sqrt((double)radius2 + (double)max_y * max_y) + 1;
        bc->min_z_depth_any_rotation = bc->center_to_top_edge > bc->center_to_bottom_edge
                                           ? bc->center_to_top_edge
                                           : bc->center_to_bottom_edge;
    }
}

static void
drop_priorities(struct ToriDraw_Model* m)
{
    free(m->face_priorities);
    m->face_priorities = NULL;
    m->model_priority = 0;
}

/* ---------------------------------------------------------- sort score -- */

/**
 * How much of the picture the painter's sort gets wrong, in pixels.
 *
 * Eyeballing a contact sheet finds gross breakage and nothing else; a priority
 * edit that fixes the jaw and quietly wrecks a wing looks like an improvement.
 * So score it: rasterize the model twice off the SAME projection, once in the
 * order ToriDraw_RenderModel2SortFaces produced and once with a real per-pixel
 * z-buffer, and count the pixels where the painter's winner is genuinely
 * behind the z-buffer's winner at that pixel.
 *
 * That last clause is what makes the number mean something. Two faces that
 * touch a pixel at equal depth (a shared edge, a coplanar seam) disagree
 * harmlessly and are not counted; only a farther surface painted over a nearer
 * one is a visible error, and it is exactly what a wrong priority band causes.
 *
 * The z-buffer here is the reference the content was authored against, not
 * something the client can run — it exists only to grade the sort.
 */
struct score
{
    long covered;   /* pixels the model touches */
    long wrong;     /* pixels showing a surface behind the true nearest */
    long depth_sum; /* summed depth error over the wrong pixels */
};

static void
raster_id_z(
    const struct ToriDraw_Model* m,
    const struct ToriDraw_Scene* scene,
    const int* order,
    int order_count,
    int w,
    int h,
    int x_center,
    int y_center,
    int* id_painter,
    int* z_painter,
    int* id_zbuf,
    int* z_zbuf)
{
    /* The projection leaves screen coordinates relative to the viewport
     * centre; the raster adds x_center/y_center as it draws. Scoring reads the
     * same scratch, so it has to apply the same offset or the mask lands half
     * a tile away from the picture it is grading. */
    const int* sx = scene->screen_vertices_x;
    const int* sy = scene->screen_vertices_y;
    const int* sz = scene->screen_vertices_z;

    for( int i = 0; i < w * h; i++ )
    {
        id_painter[i] = -1;
        id_zbuf[i] = -1;
        z_painter[i] = 0;
        z_zbuf[i] = 1 << 30;
    }

    for( int o = 0; o < order_count; o++ )
    {
        int const f = order[o];
        int const ia = m->face_indices_a[f];
        int const ib = m->face_indices_b[f];
        int const ic = m->face_indices_c[f];
        int min_x, max_x, min_y, max_y;
        long area;
        int ax, ay, az, bx, by, bz, cx, cy, cz;

        /* A near-clipped vertex carries a sentinel x, not a coordinate. */
        if( sx[ia] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
            sx[ib] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
            sx[ic] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
            continue;

        ax = sx[ia] + x_center; ay = sy[ia] + y_center; az = sz[ia];
        bx = sx[ib] + x_center; by = sy[ib] + y_center; bz = sz[ib];
        cx = sx[ic] + x_center; cy = sy[ic] + y_center; cz = sz[ic];

        area = (long)(bx - ax) * (cy - ay) - (long)(by - ay) * (cx - ax);
        if( area == 0 )
            continue;

        min_x = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
        max_x = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
        min_y = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
        max_y = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
        if( min_x < 0 ) min_x = 0;
        if( min_y < 0 ) min_y = 0;
        if( max_x >= w ) max_x = w - 1;
        if( max_y >= h ) max_y = h - 1;

        for( int y = min_y; y <= max_y; y++ )
        {
            for( int x = min_x; x <= max_x; x++ )
            {
                long w0 = (long)(bx - ax) * (y - ay) - (long)(by - ay) * (x - ax);
                long w1 = (long)(cx - bx) * (y - by) - (long)(cy - by) * (x - bx);
                long w2 = (long)(ax - cx) * (y - cy) - (long)(ay - cy) * (x - cx);
                double la, lb, lc;
                int z, at;

                if( area > 0 )
                {
                    if( w0 < 0 || w1 < 0 || w2 < 0 )
                        continue;
                }
                else if( w0 > 0 || w1 > 0 || w2 > 0 )
                    continue;

                /* Barycentric from the same edge functions: w1 belongs to a,
                 * w2 to b, w0 to c. */
                la = (double)w1 / (double)area;
                lb = (double)w2 / (double)area;
                lc = (double)w0 / (double)area;
                z = (int)(la * az + lb * bz + lc * cz);

                at = y * w + x;
                id_painter[at] = f;
                z_painter[at] = z;
                if( z < z_zbuf[at] )
                {
                    z_zbuf[at] = z;
                    id_zbuf[at] = f;
                }
            }
        }
    }
}

static void
score_accumulate(
    struct score* s,
    int w,
    int h,
    const int* id_painter,
    const int* z_painter,
    const int* id_zbuf,
    const int* z_zbuf)
{
    /* One raster unit of slack: coplanar seams and shared edges land on either
     * side of the same depth and must not read as errors. */
    int const slack = 1;

    for( int i = 0; i < w * h; i++ )
    {
        if( id_painter[i] < 0 )
            continue;
        s->covered++;
        if( id_painter[i] == id_zbuf[i] )
            continue;
        if( z_painter[i] > z_zbuf[i] + slack )
        {
            s->wrong++;
            s->depth_sum += z_painter[i] - z_zbuf[i];
        }
    }
}

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s --model FILE.ob3 [--model FILE.ob3 ...] --out OUT.bmp\n"
        "       [--angles N] [--yaw0 N] [--pitch N] [--zoom F] [--tile N]\n"
        "       [--focus X,Y,Z] [--radius R]\n"
        "       [--textures] [--ignore-priorities] [--stats] [--score]\n",
        argv0);
}

int
main(int argc, char** argv)
{
    const char* model_paths[MAX_MODELS];
    int model_count = 0;
    const char* out_path = "model_view.bmp";
    int angles = 4;
    int yaw0 = 0;
    int pitches[MAX_PITCHES] = { 0 };
    int pitch_count = 1;
    double zoom = 1.0;
    int tile = 320;
    bool keep_textures = false;
    bool ignore_priorities = false;
    bool stats = false;
    bool do_score = false;
    const char* score_path = NULL;
    bool have_focus = false;
    int focus[3] = { 0, 0, 0 };
    int focus_radius = 0;
    struct score total = { 0, 0, 0 };

    struct ToriDraw_Model* parts[MAX_MODELS];
    struct ToriDraw_Model* model = NULL;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_Scene* scene = NULL;
    int* pixels = NULL;
    int* tile_pixels = NULL;
    int* id_painter = NULL;
    int* z_painter = NULL;
    int* id_zbuf = NULL;
    int* z_zbuf = NULL;
    int* score_pixels = NULL;
    int cols, rows, sheet_w, sheet_h;
    int extent, distance;
    int center_y;
    int rc = 1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
        {
            if( model_count >= MAX_MODELS )
            {
                fprintf(stderr, "rs2012_model_view: too many --model\n");
                return 2;
            }
            model_paths[model_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( strcmp(argv[i], "--angles") == 0 && i + 1 < argc )
            angles = atoi(argv[++i]);
        else if( strcmp(argv[i], "--yaw0") == 0 && i + 1 < argc )
            yaw0 = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pitch") == 0 && i + 1 < argc )
        {
            const char* s = argv[++i];
            pitch_count = 0;
            while( *s && pitch_count < MAX_PITCHES )
            {
                pitches[pitch_count++] = (int)strtol(s, (char**)&s, 10);
                if( *s == ',' )
                    s++;
            }
        }
        else if( strcmp(argv[i], "--zoom") == 0 && i + 1 < argc )
            zoom = atof(argv[++i]);
        else if( strcmp(argv[i], "--tile") == 0 && i + 1 < argc )
            tile = atoi(argv[++i]);
        else if( strcmp(argv[i], "--textures") == 0 )
            keep_textures = true;
        else if( strcmp(argv[i], "--ignore-priorities") == 0 )
            ignore_priorities = true;
        else if( strcmp(argv[i], "--stats") == 0 )
            stats = true;
        else if( strcmp(argv[i], "--score") == 0 )
            do_score = true;
        else if( strcmp(argv[i], "--score-out") == 0 && i + 1 < argc )
        {
            score_path = argv[++i];
            do_score = true;
        }
        else if( strcmp(argv[i], "--focus") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%d,%d,%d", &focus[0], &focus[1], &focus[2]) != 3 )
            {
                usage(argv[0]);
                return 2;
            }
            have_focus = true;
        }
        else if( strcmp(argv[i], "--radius") == 0 && i + 1 < argc )
            focus_radius = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if( model_count == 0 || angles < 1 || angles > MAX_ANGLES || tile < 32 || zoom <= 0.0 )
    {
        usage(argv[0]);
        return 2;
    }

    ToriDraw_Init();

    for( int i = 0; i < model_count; i++ )
    {
        parts[i] = load_ob3(model_paths[i]);
        if( !parts[i] )
            goto done;
    }

    if( model_count == 1 )
    {
        model = parts[0];
        parts[0] = NULL;
    }
    else
    {
        model = ToriDraw_ModelNewMerge(parts, model_count);
        if( !model )
        {
            fprintf(stderr, "rs2012_model_view: merge failed\n");
            goto done;
        }
    }

    if( ignore_priorities )
        drop_priorities(model);
    if( !keep_textures )
        strip_textures(model);

    recenter_model(model, have_focus ? focus : NULL);

    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;

    /* Actor regime: this lane's subjects are npcs. Lighting has to run before
     * the render or every face_colors_* stays zero and the sheet is black. */
    ToriDraw_ModelCalculateVertexNormals(model);
    ToriDraw_LightModelActor(hnd, 0, 0);

    if( stats )
        print_stats(model);

    /* Recentred, so the framing radius is just the cylinder's widest reach in
     * any rotation — the same number the cull uses. */
    center_y = 0;
    extent = focus_radius > 0
                 ? focus_radius
                 : (model->bounds_cylinder ? model->bounds_cylinder->min_z_depth_any_rotation
                                           : 512);
    if( extent < 1 )
        extent = 1;

    /* One column per yaw, one row per pitch: the sheet reads as an orbit. */
    cols = angles;
    rows = pitch_count;
    sheet_w = cols * tile;
    sheet_h = rows * tile;
    pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
    if( !pixels )
        goto done;
    if( score_path )
    {
        score_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
        if( !score_pixels )
            goto done;
    }

    /* screen = world * scale / z, so the distance that makes `extent` fill
     * 90% of a half-tile is extent * scale / (0.45 * tile). */
    distance = (int)((double)extent * (double)TORIDRAW_PROJ_SCALE_DEFAULT /
                     (0.45 * (double)tile * zoom));
    if( distance < 100 )
        distance = 100;

    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    if( !scene )
        goto done;

    /* Each tile renders into its own origin-anchored buffer and is then
     * blitted. The AABB cull compares the projected box against 0..width, so a
     * tile whose x_center sits at sheet coordinates is culled outright. */
    tile_pixels = (int*)calloc((size_t)tile * (size_t)tile, sizeof(int));
    if( !tile_pixels )
        goto done;
    if( do_score )
    {
        size_t const n = (size_t)tile * (size_t)tile;
        id_painter = (int*)malloc(n * sizeof(int));
        z_painter = (int*)malloc(n * sizeof(int));
        id_zbuf = (int*)malloc(n * sizeof(int));
        z_zbuf = (int*)malloc(n * sizeof(int));
        if( !id_painter || !z_painter || !id_zbuf || !z_zbuf )
            goto done;
    }

    for( int view = 0; view < angles * pitch_count; view++ )
    {
        int const a = view % angles;
        int const p = view / angles;
        int const tx = a * tile;
        int const ty = p * tile;
        int const yaw = (yaw0 + (int)((long)a * 2048 / angles)) & 2047;
        int const pitch = pitches[p] & 2047;
        struct ToriDraw_ViewPort vp = {
            .width = tile,
            .height = tile,
            .stride = tile,
            .x_center = tile / 2,
            .y_center = tile / 2,
            .clip_left = 0,
            .clip_top = 0,
            .clip_right = tile,
            .clip_bottom = tile,
        };
        struct ToriDraw_Camera camera = {
            .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
            .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
            .near_plane_z = 50,
            .pitch = 0,
            .yaw = 0,
            .roll = 0,
        };
        struct ToriDraw_Position pos = {
            .x = 0,
            .y = -center_y,
            .z = distance,
            .pitch = pitch,
            .yaw = yaw,
            .roll = 0,
        };
        int cull;

        memset(tile_pixels, 0, (size_t)tile * (size_t)tile * sizeof(int));
        cull = ToriDraw_RenderModel1Project(hnd, scene, &pos, &vp, &camera);
        if( cull != TORIDRAW_CULL_VISIBLE )
            fprintf(stderr, "rs2012_model_view: yaw %d culled (code %d)\n", yaw, cull);
        else if( ToriDraw_RenderModel2SortFaces(hnd, scene) <= 0 )
            fprintf(stderr, "rs2012_model_view: yaw %d sorted 0 faces\n", yaw);
        else
        {
            /* Scored off the projection BEFORE the raster runs: the raster
             * family is free to rewrite the screen-vertex scratch it consumes,
             * and a score taken afterwards is a score of whatever it left. */
            if( do_score )
            {
                raster_id_z(
                    model,
                    scene,
                    ToriDraw_FaceOrder(scene),
                    ToriDraw_FaceOrderCount(scene),
                    tile,
                    tile,
                    vp.x_center,
                    vp.y_center,
                    id_painter,
                    z_painter,
                    id_zbuf,
                    z_zbuf);
                score_accumulate(&total, tile, tile, id_painter, z_painter, id_zbuf, z_zbuf);

                /* The mask is the reason the score is actionable: a percentage
                 * says the sort is wrong, the mask says which feature. */
                if( score_pixels )
                    for( int row = 0; row < tile; row++ )
                        for( int col = 0; col < tile; col++ )
                        {
                            int const at = row * tile + col;
                            int const to = (ty + row) * sheet_w + tx + col;
                            if( id_painter[at] < 0 )
                                score_pixels[to] = 0;
                            else if( id_painter[at] != id_zbuf[at] &&
                                     z_painter[at] > z_zbuf[at] + 1 )
                                score_pixels[to] = 0xFF0000;
                            else
                                score_pixels[to] = 0x203040;
                        }
            }
            ToriDraw_RenderModel3Raster(scene, &vp, &camera, tile_pixels, false);
        }

        for( int row = 0; row < tile; row++ )
            memcpy(
                &pixels[(size_t)(ty + row) * (size_t)sheet_w + tx],
                &tile_pixels[(size_t)row * (size_t)tile],
                (size_t)tile * sizeof(int));
    }

    bmp_write_file(out_path, pixels, sheet_w, sheet_h);
    printf("rs2012_model_view: wrote %s (%dx%d, %d angles)\n", out_path, sheet_w, sheet_h, angles);
    if( score_pixels )
    {
        bmp_write_file(score_path, score_pixels, sheet_w, sheet_h);
        printf("rs2012_model_view: wrote %s (sort-error mask)\n", score_path);
    }
    if( do_score )
        printf(
            "sort score: %ld/%ld pixels behind the true surface (%.3f%%), "
            "mean depth error %.1f\n",
            total.wrong,
            total.covered,
            total.covered ? 100.0 * (double)total.wrong / (double)total.covered : 0.0,
            total.wrong ? (double)total.depth_sum / (double)total.wrong : 0.0);
    rc = 0;

done:
    ToriDraw_SceneFree(scene);
    free(pixels);
    free(tile_pixels);
    free(id_painter);
    free(z_painter);
    free(id_zbuf);
    free(z_zbuf);
    free(score_pixels);
    ToriDraw_ModelFree(model);
    for( int i = 0; i < model_count; i++ )
        ToriDraw_ModelFree(parts[i]);
    return rc;
}
