/*
 * The ToriRS -> ToriDraw model adaptor must not forward decode flags into the
 * renderer's flag word.
 *
 * The two structs each have a `uint8_t flags` and they mean different things.
 * ToriRS_Model::flags is decode bookkeeping (TORIRS_MODEL_FLAG_DECODED, set on
 * every model ToriRS_ModelFromRSCache produces, plus _TEXTURED). ToriDraw_Model
 * ::flags is render policy, and its bit 0 is TORIDRAW_MODEL_FLAG_ZBUFFER, which
 * routes a model through the depth-tested kernels AND drops its face priorities
 * at sort time. Copying the byte across therefore turned every model in the
 * cache -- scenery, players, obj icons, widget models -- into a depth-tested
 * model with no priority layering, the moment the app's scene grew a z-buffer.
 *
 * What is asserted:
 *
 *   - a converted model carries no renderer flags, whatever the source's decode
 *     flags were. This is the regression guard: it is the one line of the fix.
 *   - the consequence is real, in the sort the client actually runs. A model
 *     whose priority order and depth order DISAGREE keeps its priority order
 *     when the flag is clear, and loses it when the flag is set. Without this
 *     half, the flag assertion is a claim about a byte nobody has shown matters.
 *   - and it is visible: the high-priority face's pixels survive in one case
 *     and not the other.
 *
 * The fixture is the shape of the asset that surfaced this -- a wall banner
 * whose emblem sits BEHIND the cloth in depth and is pinned in front of it by a
 * face priority. Depth alone hides the emblem; that is what the screenshot
 * showed.
 *
 * Build and run:
 *   make -C src test-model-render-flags
 */

#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"

#include "toridraw.h"
#include "toridraw_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 128
#define VIEW_H 128
#define CAMERA_DISTANCE 900
#define EXTENT 200

/* Model-space +z is away from the eye: the position below places the model at
 * z = CAMERA_DISTANCE and a vertex's own z adds to that. */
#define EMBLEM_Z_BEHIND 40

#define HSL_CLOTH 5000
#define HSL_EMBLEM 40000

#define PRIO_CLOTH 0
/* A fixed-priority run (0..9), not one of the flexible 10/11 pair, so the
 * expected order is "every cloth face, then every emblem face" and not an
 * interleave decided by average depths. */
#define PRIO_EMBLEM 9

#define MAX_VERTICES 32
#define MAX_FACES 16

static int g_failures = 0;

static void
check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if( !ok )
        g_failures++;
}

/* toridraw_model_from_torirs.c calls this from ToriDraw_ModelDropNonSdTextures,
 * which nothing here drives. Stubbed rather than linking the cache provider. */
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id)
{
    (void)provider;
    (void)texture_id;
    return true;
}

/* --- the source model ----------------------------------------------------- */

struct Source
{
    struct ToriRS_Model model;
    gc_vertexint_t vx[MAX_VERTICES];
    gc_vertexint_t vy[MAX_VERTICES];
    gc_vertexint_t vz[MAX_VERTICES];
    gc_faceint_t fa[MAX_FACES];
    gc_faceint_t fb[MAX_FACES];
    gc_faceint_t fc[MAX_FACES];
    gc_hsl16_t ca[MAX_FACES];
    gc_hsl16_t cb[MAX_FACES];
    gc_hsl16_t cc[MAX_FACES];
    int infos[MAX_FACES];
    uint8_t priorities[(MAX_FACES + 1) / 2];
    struct ToriRS_BoundsCylinder bounds;
    /* Which faces belong to the emblem, by index into the model. */
    int emblem_faces[MAX_FACES];
    int emblem_face_count;
};

static void
source_init(struct Source* s)
{
    memset(s, 0, sizeof(*s));

    s->model.vertices_x = s->vx;
    s->model.vertices_y = s->vy;
    s->model.vertices_z = s->vz;
    s->model.face_indices_a = s->fa;
    s->model.face_indices_b = s->fb;
    s->model.face_indices_c = s->fc;
    s->model.face_colors_a = s->ca;
    s->model.face_colors_b = s->cb;
    s->model.face_colors_c = s->cc;
    s->model.face_infos = s->infos;
    s->model.face_priorities = s->priorities;
    s->model.bounds_cylinder = &s->bounds;

    s->bounds.radius = EXTENT * 2;
    s->bounds.min_y = -EXTENT;
    s->bounds.max_y = EXTENT;
    s->bounds.center_to_top_edge = EXTENT * 3;
    s->bounds.center_to_bottom_edge = EXTENT * 3;
    s->bounds.min_z_depth_any_rotation = EXTENT * 3;

    /* Exactly what ToriRS_ModelFromRSCache leaves on a decoded model. */
    s->model.flags = TORIRS_MODEL_FLAG_DECODED | TORIRS_MODEL_FLAG_TEXTURED;
}

static int
source_add_vertex(struct Source* s, int x, int y, int z)
{
    int const at = s->model.vertex_count;

    if( at >= MAX_VERTICES )
        exit(2);
    s->vx[at] = (gc_vertexint_t)x;
    s->vy[at] = (gc_vertexint_t)y;
    s->vz[at] = (gc_vertexint_t)z;
    s->model.vertex_count = at + 1;
    return at;
}

static void
source_set_priority(struct Source* s, int face, int prio)
{
    uint8_t* byte = &s->priorities[face >> 1];

    if( face & 1 )
        *byte = (uint8_t)((*byte & 0x0Fu) | ((unsigned)prio << 4));
    else
        *byte = (uint8_t)((*byte & 0xF0u) | (unsigned)prio);
}

/* Flat-shaded, so a drawn pixel has one exact expected colour. */
static int
source_add_face(struct Source* s, int a, int b, int c, int hsl, int prio)
{
    int const at = s->model.face_count;

    if( at >= MAX_FACES )
        exit(2);
    s->fa[at] = (gc_faceint_t)a;
    s->fb[at] = (gc_faceint_t)b;
    s->fc[at] = (gc_faceint_t)c;
    s->ca[at] = (gc_hsl16_t)hsl;
    s->cb[at] = (gc_hsl16_t)hsl;
    s->cc[at] = TORIDRAWHSL16_FLAT;
    s->infos[at] = 0;
    s->model.face_count = at + 1;
    source_set_priority(s, at, prio);
    return at;
}

/**
 * Both windings of a quad. The raster culls one screen-space winding and which
 * one depends on the camera; emitting both makes the quad visible without the
 * test encoding the winding convention. The two copies are coincident and the
 * same colour, so whichever survives paints the same pixels.
 */
static void
source_add_quad(struct Source* s, int v0, int v1, int v2, int v3, int hsl, int prio, bool emblem)
{
    int const faces[4][3] = {
        { v0, v1, v2 }, { v0, v2, v3 }, { v0, v2, v1 }, { v0, v3, v2 },
    };

    for( int i = 0; i < 4; i++ )
    {
        int const f = source_add_face(s, faces[i][0], faces[i][1], faces[i][2], hsl, prio);

        if( emblem )
            s->emblem_faces[s->emblem_face_count++] = f;
    }
}

/**
 * The banner: a large cloth quad, and a smaller emblem quad sitting BEHIND it
 * and pinned in front by its priority. Depth and priority disagree, which is
 * the only arrangement that can tell the two sorts apart.
 */
static void
source_build_banner(struct Source* s)
{
    int const e = EXTENT;
    int const h = EXTENT / 2;
    int const z = EMBLEM_Z_BEHIND;

    int const c0 = source_add_vertex(s, -e, -e, 0);
    int const c1 = source_add_vertex(s, e, -e, 0);
    int const c2 = source_add_vertex(s, e, e, 0);
    int const c3 = source_add_vertex(s, -e, e, 0);

    int const m0 = source_add_vertex(s, -h, -h, z);
    int const m1 = source_add_vertex(s, h, -h, z);
    int const m2 = source_add_vertex(s, h, h, z);
    int const m3 = source_add_vertex(s, -h, h, z);

    source_add_quad(s, c0, c1, c2, c3, HSL_CLOTH, PRIO_CLOTH, false);
    source_add_quad(s, m0, m1, m2, m3, HSL_EMBLEM, PRIO_EMBLEM, true);
}

/* --- rendering ------------------------------------------------------------ */

struct Frame
{
    toripixel_t pixels[VIEW_W * VIEW_H];
};

static struct ToriDraw_ViewPort
viewport(void)
{
    struct ToriDraw_ViewPort vp = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    return vp;
}

static struct ToriDraw_Camera
camera(void)
{
    struct ToriDraw_Camera cam = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
    };
    return cam;
}

/** Project + sort. Returns the emitted face count, or -1 if the model culled. */
static int
sort_faces(struct ToriDraw_Scene* scene, struct ToriDraw_ModelHandle hnd)
{
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera();
    struct ToriDraw_Position pos = { .x = 0, .y = 0, .z = CAMERA_DISTANCE };

    if( ToriDraw_RenderModel1Project(hnd, scene, &pos, &vp, &cam) != TORIDRAW_CULL_VISIBLE )
        return -1;
    return ToriDraw_RenderModel2SortFaces(hnd, scene);
}

static bool
raster(struct ToriDraw_Scene* scene, struct Frame* frame)
{
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera();

    memset(frame->pixels, 0, sizeof(frame->pixels));
    ToriDraw_RenderModel3Raster(scene, &vp, &cam, frame->pixels, false);
    return true;
}

static bool
is_emblem_face(const struct Source* s, int face)
{
    for( int e = 0; e < s->emblem_face_count; e++ )
        if( s->emblem_faces[e] == face )
            return true;
    return false;
}

/**
 * Are the emblem's faces an unbroken run at the tail of the draw order?
 *
 * Counted from what the sort actually emitted rather than from the model's face
 * count: each quad is authored in both windings and the raster culls one, so
 * roughly half of every quad's faces never reach the order. Sizing the tail by
 * the emblem faces PRESENT is what makes this independent of which winding the
 * camera happens to keep.
 */
static bool
emblem_draws_last(const struct Source* s, const int* order, int count)
{
    int present = 0;

    for( int i = 0; i < count; i++ )
        if( is_emblem_face(s, order[i]) )
            present++;
    if( present == 0 || present == count )
        return false;
    for( int i = count - present; i < count; i++ )
        if( !is_emblem_face(s, order[i]) )
            return false;
    return true;
}

static toripixel_t
centre_pixel(const struct Frame* f)
{
    return f->pixels[(VIEW_H / 2) * VIEW_W + (VIEW_W / 2)];
}

int
main(void)
{
    struct Source source;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_Scene* scene;
    struct Frame with_priorities;
    struct Frame without_priorities;
    toripixel_t emblem_rgb;
    int count;

    ToriDraw_Init();

    source_init(&source);
    source_build_banner(&source);

    /* 1. The fix itself. */
    model = ToriDraw_ModelFromToriRS(&source.model);
    if( !model )
    {
        fprintf(stderr, "ToriDraw_ModelFromToriRS returned NULL\n");
        return 2;
    }
    check(
        source.model.flags != 0,
        "source model carries decode flags (else the next check proves nothing)");
    check(
        model->flags == 0,
        "converted model carries no renderer flags");
    check(
        (model->flags & TORIDRAW_MODEL_FLAG_ZBUFFER) == 0,
        "converted model is not opted into the depth-tested kernels");
    check(
        model->face_priorities != NULL,
        "converted model kept its face priorities array");

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;

    /* The app's scene: it carries a z-buffer for the models that ask for one,
     * which is what made the leaked flag load-bearing rather than inert. */
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    if( !scene )
    {
        fprintf(stderr, "ToriDraw_SceneNew failed\n");
        return 2;
    }

    emblem_rgb = (toripixel_t)g_hsl16_to_pixel_table[HSL_EMBLEM];

    /* 2. As converted: the priority wins over depth. */
    count = sort_faces(scene, hnd);
    check(count > 0, "the model projected and sorted");
    check(
        count > 0 && emblem_draws_last(&source, ToriDraw_FaceOrder(scene), count),
        "priority pins the emblem last in the draw order");
    raster(scene, &with_priorities);
    check(
        centre_pixel(&with_priorities) == emblem_rgb,
        "emblem is visible over the cloth");

    /* 3. Negative control: the state the leak put every cache model into. If
     *    this half passed too, the checks above would be measuring nothing. */
    model->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
    count = sort_faces(scene, hnd);
    check(
        count > 0 && !emblem_draws_last(&source, ToriDraw_FaceOrder(scene), count),
        "the z-buffer flag drops the priority: emblem no longer draws last");
    raster(scene, &without_priorities);
    check(
        centre_pixel(&without_priorities) != emblem_rgb,
        "and the emblem is then hidden by the cloth");

    /* 4. The two halves of that behaviour are separable. An imported model whose
     *    priorities its authoring client never honoured drops them WITHOUT being
     *    depth-tested -- which is what the rs2012 npcs get while the depth-test
     *    kernels are off (app_model_apply_import_render_flags). Same drop as the
     *    z-buffer flag's, reached by the flag that does nothing else. */
    model->flags &= (uint8_t)~TORIDRAW_MODEL_FLAG_ZBUFFER;
    model->flags |= TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY;
    count = sort_faces(scene, hnd);
    check(
        count > 0 && !emblem_draws_last(&source, ToriDraw_FaceOrder(scene), count),
        "NO_FACE_PRIORITY drops the priority on its own");
    check(
        (model->flags & TORIDRAW_MODEL_FLAG_ZBUFFER) == 0,
        "and does so without opting the model into the depth-tested kernels");

    ToriDraw_SceneFree(scene);
    ToriDraw_ModelFree(model);

    printf("%s\n", g_failures ? "FAILED" : "all checks passed");
    return g_failures ? 1 : 0;
}
