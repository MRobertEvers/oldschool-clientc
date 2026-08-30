/*
 * The kernel TABLE matrix: what a caller is allowed to swap without the
 * picture changing, and what a caller is not.
 *
 * Every other kernel test in this tree holds ONE seam. This one holds the
 * agreements BETWEEN the three stages, which is the thing no single-stage test
 * can see: a projection, a face sort and a raster can each be correct on their
 * own while the pair of them disagrees about the buffer they hand each other,
 * and the only symptom is pixels.
 *
 * Two claims, and they pull in opposite directions on purpose.
 *
 *   INVARIANT.  Within one table, the picture does not depend on which face
 *               sort ran or whether the batched walk was armed. The flat sort
 *               emits the bucket sort's order face for face; the run doors
 *               draw what the per-face walk draws. Both are load-bearing
 *               performance choices, and both are supposed to be invisible
 *               here. A regression in either shows up as an arm whose hash
 *               moved -- and, crucially, it shows up whichever of the three
 *               stages actually broke.
 *
 *   DISTINCT.   Different tables are allowed -- required -- to differ.
 *               `scanline` is a different rasteriser, not a mode of the
 *               branching one; `zbuffered` resolves per pixel and runs no
 *               sort at all. If those hashed the same as the painter, this
 *               test would be passing because nothing reached the kernels,
 *               which is exactly the failure a hash comparison is worst at
 *               noticing. So the difference is asserted, not tolerated.
 *
 * Nothing is hardcoded to a golden constant: the goldens are taken from the
 * first arm of each family in the same run. A kernel change that legitimately
 * moves every pixel keeps this test green; a change that moves the pixels of
 * one arm and not its twin does not. That is the property worth pinning, and
 * a checked-in hash would fail on the first legitimate kernel edit and be
 * deleted a week later.
 *
 * The models are built to make face order matter. Sorting is invisible on
 * geometry that does not overlap, so the fixture is a fan of triangles that
 * all cover the same patch of screen at different depths, mixing the four SD
 * face classes and both opacities. Rendering that with the order shuffled
 * changes almost every covered pixel.
 *
 * WHAT THIS DOES NOT REACH, and which test does. A hash of a frame is a blunt
 * instrument on purpose: it sees a stage that disagrees with its neighbour
 * SYSTEMATICALLY -- a permutation table one kernel reads differently from
 * another, an order emitted backwards, a whole class of face routed wrong --
 * and it is measurably deaf to one face moving a few slots in a 23-face stack,
 * because the pixels that face lands on are composited from twenty others
 * either way. Measured, not assumed: mutating the batched walk's y-order table
 * or reversing the flat sort's emitted order both turn this red, and shifting
 * a single face's depth does not.
 *
 * That single face is toridraw_face_sort_flat_test's job. It compares the two
 * sorts order for order over 864 fixtures instead of comparing pictures, and
 * it catches exactly the mutation this one misses. The two are complementary
 * and neither replaces the other: order-for-order cannot see a raster that
 * misreads the order it was given, and a frame hash cannot see a face that
 * moved somewhere invisible.
 *
 * Standalone TU, no cache or disk.
 */

#include "toridraw.h"
#include "toridraw_scene.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_WIDTH 320
#define VIEW_HEIGHT 200
#define VIEW_STRIDE 336
#define CLIP_LEFT 4
#define CLIP_TOP 6
#define CLIP_RIGHT 316
#define CLIP_BOTTOM 194

#define TEST_TEXTURE_ID 7
#define TEST_TEXTURE_WIDTH 64

/*
 * Enough overlap that a shuffled order repaints nearly every covered pixel.
 *
 * DELIBERATELY NOT A MULTIPLE OF FOUR. The flat sort culls four faces at a
 * time in its lane kernel and sweeps what is left over with a scalar tail, so
 * a face count divisible by four leaves that tail unreached -- and the tail is
 * the only arm on a no-SIMD lane, which makes it the half of the sort most
 * likely to drift. A fixture that never runs it agrees with itself no matter
 * what the tail does.
 */
#define STACK_FACES 23
#define STACK_VERTS (STACK_FACES * 3)

static int failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while( 0 )

/* ---- fixture --------------------------------------------------------- */

struct StackModel
{
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle handle;
    vertexint_t vertex_x[STACK_VERTS];
    vertexint_t vertex_y[STACK_VERTS];
    vertexint_t vertex_z[STACK_VERTS];
    faceint_t face_a[STACK_FACES];
    faceint_t face_b[STACK_FACES];
    faceint_t face_c[STACK_FACES];
    hsl16_t shade_a[STACK_FACES];
    hsl16_t shade_b[STACK_FACES];
    hsl16_t shade_c[STACK_FACES];
    alphaint_t alpha[STACK_FACES];
    faceint_t texture[STACK_FACES];
};

static struct ToriDraw_ViewPort
test_viewport(void)
{
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_WIDTH,
        .height = VIEW_HEIGHT,
        .stride = VIEW_STRIDE,
        .x_center = VIEW_WIDTH / 2,
        .y_center = VIEW_HEIGHT / 2,
        .clip_left = CLIP_LEFT,
        .clip_top = CLIP_TOP,
        .clip_right = CLIP_RIGHT,
        .clip_bottom = CLIP_BOTTOM,
    };
    return viewport;
}

static struct ToriDraw_Camera
test_camera(void)
{
    struct ToriDraw_Camera camera = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
        .texture_affine = true,
    };
    return camera;
}

static struct ToriDraw_Texture*
make_test_texture(void)
{
    struct ToriDraw_Texture* texture = calloc(1, sizeof(*texture));
    int const size = TEST_TEXTURE_WIDTH * TEST_TEXTURE_WIDTH;

    /* An allocation failure here is not a case to handle: it would produce a
     * quietly untextured matrix that still passes. */
    if( !texture )
        return NULL;
    texture->texels = malloc((size_t)size * sizeof(*texture->texels));
    if( !texture->texels )
    {
        free(texture);
        return NULL;
    }
    /* A pattern, not a constant: a flat texture hides every uv error, and a uv
     * error is exactly the kind of cross-stage disagreement this test is for
     * (projection writes the orthographic vertices the textured raster reads). */
    for( int i = 0; i < size; i++ )
    {
        int const u = i % TEST_TEXTURE_WIDTH;
        int const v = i / TEST_TEXTURE_WIDTH;

        texture->texels[i] = ((u * 4) << 16) | ((v * 4) << 8) | ((u ^ v) * 4);
    }
    texture->width = TEST_TEXTURE_WIDTH;
    texture->height = TEST_TEXTURE_WIDTH;
    texture->opaque = false;
    return texture;
}

/*
 * A fan of overlapping triangles at descending depths, cycling the four SD
 * face classes and dropping an alpha face into every fifth slot.
 *
 * `depth_spread` is what turns this from one flat card into something the
 * depth sort has to order: face i sits at z = i * spread, so its draw position
 * is a function of the sort and nothing else.
 */
static void
stack_model_init(struct StackModel* m, int depth_spread)
{
    memset(m, 0, sizeof(*m));
    m->model.vertex_count = STACK_VERTS;
    m->model.face_count = STACK_FACES;
    m->model.vertices_x = m->vertex_x;
    m->model.vertices_y = m->vertex_y;
    m->model.vertices_z = m->vertex_z;
    m->model.face_indices_a = m->face_a;
    m->model.face_indices_b = m->face_b;
    m->model.face_indices_c = m->face_c;
    m->model.face_colors_a = m->shade_a;
    m->model.face_colors_b = m->shade_b;
    m->model.face_colors_c = m->shade_c;
    m->model.face_alphas = m->alpha;
    m->model.face_textures = m->texture;
    m->model.textured_face_count = 1;

    for( int face = 0; face < STACK_FACES; face++ )
    {
        int const v = face * 3;
        /* Rotate each triangle a little so they overlap without coinciding --
         * coincident triangles would make the order matter only where the
         * shades differ, and z-fight in the depth-tested table. */
        int const spin = face * 37;
        int const rx = 70 + (face % 5) * 9;
        int const ry = 60 + (face % 7) * 7;

        m->vertex_x[v + 0] = (vertexint_t)(-rx + (spin % 23));
        m->vertex_y[v + 0] = (vertexint_t)(-ry + (spin % 17));
        m->vertex_x[v + 1] = (vertexint_t)(rx - (spin % 19));
        m->vertex_y[v + 1] = (vertexint_t)(-ry + (spin % 13));
        m->vertex_x[v + 2] = (vertexint_t)((spin % 29) - 14);
        m->vertex_y[v + 2] = (vertexint_t)(ry - (spin % 11));

        /*
         * Depth is a PERMUTATION of the face index, not a multiple of it.
         *
         * With z rising monotonically with the index, model order already is
         * depth order: the sort has nothing to do, the deepest faces are the
         * last ones -- which is also the block the SIMD cull leaves to its
         * scalar tail -- and a mutation that shifts those faces' depth moves
         * them from the back of the order to the back of the order. The fixture
         * agrees with itself no matter what the sort does with them.
         *
         * 7 and 23 are coprime, so (face * 7) % 23 visits every slot: index
         * order and depth order now disagree everywhere, and any face's
         * position in the drawn order is a real result of the sort.
         */
        {
            int const slot = (face * 7) % STACK_FACES;

            m->vertex_z[v + 0] = (vertexint_t)(slot * depth_spread);
            m->vertex_z[v + 1] = (vertexint_t)(slot * depth_spread);
            m->vertex_z[v + 2] = (vertexint_t)(slot * depth_spread);
        }

        m->face_a[face] = (faceint_t)(v + 0);
        m->face_b[face] = (faceint_t)(v + 2);
        m->face_c[face] = (faceint_t)(v + 1);

        m->texture[face] = -1;
        m->alpha[face] = 0;

        switch( face % 4 )
        {
        case 0: /* gouraud */
            m->shade_a[face] = (hsl16_t)(0x1200 + face * 3);
            m->shade_b[face] = (hsl16_t)(0x1240 + face * 5);
            m->shade_c[face] = (hsl16_t)(0x1280 + face * 7);
            break;
        case 1: /* flat: the sentinel in slot c */
            m->shade_a[face] = (hsl16_t)(0x2300 + face * 11);
            m->shade_b[face] = (hsl16_t)(0x3400 + face * 13);
            m->shade_c[face] = TORIDRAWHSL16_FLAT;
            break;
        case 2: /* textured gouraud */
            m->shade_a[face] = (hsl16_t)(30 + face);
            m->shade_b[face] = (hsl16_t)(60 + face);
            m->shade_c[face] = (hsl16_t)(90 + face);
            m->texture[face] = TEST_TEXTURE_ID;
            break;
        default: /* textured flat */
            m->shade_a[face] = (hsl16_t)(50 + face);
            m->shade_b[face] = (hsl16_t)(80 + face);
            m->shade_c[face] = TORIDRAWHSL16_FLAT;
            m->texture[face] = TEST_TEXTURE_ID;
            break;
        }

        /*
         * Translucency is where draw order stops being a tie-break and starts
         * being arithmetic: a blended face composites with whatever the sort
         * put underneath it.
         *
         * MOST faces, not a sprinkling. Under an opaque stack only the nearest
         * face at each pixel survives, so moving a buried face changes nothing
         * a hash can see, and the matrix only notices reorderings big enough to
         * disturb the top layer. Blending puts every face in the stack into
         * every covered pixel, which is what makes a single face's position in
         * the order observable at all. One in three is left opaque so the
         * opaque span kernels stay on the path.
         */
        if( face % 3 != 0 )
            m->alpha[face] = (alphaint_t)(60 + (face * 37) % 150);
    }

    m->handle.kind = TORIDRAWMK_MODEL;
    m->handle.u.model.model = &m->model;
    ToriDraw_ModelSetBoundsCylinder(&m->model);
}

/* ---- the rendered scene ---------------------------------------------- */

/*
 * Placements chosen to exercise the paths the stages disagree on, not just to
 * put paint on screen: a plain model, a yaw-rotated one (the prepared
 * projection kernel's fast path is yaw-only), a pitched one (which is NOT that
 * path, so both projection families run in one frame), and one pushed up
 * against the near plane so the clip family and the near-clip sentinel are
 * live -- the sentinel is a value the sort and the raster both have to read
 * the same way.
 */
struct Placement
{
    const char* name;
    int x, y, z;
    int yaw, pitch;
    bool zbuffered;
};

static const struct Placement g_placements[] = {
    { "centre", 0, 0, 420, 0, 0, false },
    { "yaw", -90, 20, 520, 512, 0, false },
    { "pitch+yaw", 95, -15, 480, 300, 240, false },
    { "near-plane", 20, 10, 95, 128, 0, false },
    { "depth-tested", -40, -30, 380, 700, 0, true },
};

#define PLACEMENT_COUNT ((int)(sizeof(g_placements) / sizeof(g_placements[0])))

static uint64_t
hash_pixels(const toripixel_t* pixels)
{
    /* FNV-1a over the clip rectangle only. Outside it nothing is written, and
     * hashing the untouched margin would dilute a real difference. */
    uint64_t h = 1469598103934665603ULL;

    for( int y = CLIP_TOP; y < CLIP_BOTTOM; y++ )
    {
        for( int x = CLIP_LEFT; x < CLIP_RIGHT; x++ )
        {
            uint32_t const p = (uint32_t)pixels[y * VIEW_STRIDE + x];

            for( int b = 0; b < 4; b++ )
            {
                h ^= (uint64_t)((p >> (b * 8)) & 0xFFu);
                h *= 1099511628211ULL;
            }
        }
    }
    return h;
}

static long
count_covered(const toripixel_t* pixels)
{
    long n = 0;

    for( int y = CLIP_TOP; y < CLIP_BOTTOM; y++ )
        for( int x = CLIP_LEFT; x < CLIP_RIGHT; x++ )
            n += pixels[y * VIEW_STRIDE + x] != 0;
    return n;
}

/*
 * Draw the whole fixture once, through one table, and hash it.
 *
 * The table is taken through its getter on every arm rather than once at the
 * top, because that getter IS where the library resolves the face sort -- the
 * point of the sort knob in this matrix is to move what the getter hands out,
 * and caching the table across arms would quietly test one sort four times.
 */
static uint64_t
render_arm(
    struct ToriDraw_Scene* scene,
    toripixel_t* pixels,
    struct StackModel* stack,
    struct StackModel* zstack,
    const struct ToriDraw_Kernel* table,
    long* out_covered)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();

    memset(pixels, 0, (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*pixels));

    for( int i = 0; i < PLACEMENT_COUNT; i++ )
    {
        const struct Placement* p = &g_placements[i];
        struct StackModel* m = p->zbuffered ? zstack : stack;
        struct ToriDraw_Position position = {
            .x = p->x,
            .y = p->y,
            .z = p->z,
            .yaw = p->yaw,
            .pitch = p->pitch,
        };

        ToriDraw_RenderModelWithTable(
            m->handle, scene, &position, &viewport, &camera, pixels, table);
    }

    if( out_covered )
        *out_covered = count_covered(pixels);
    return hash_pixels(pixels);
}

/* ---- the matrix ------------------------------------------------------ */

struct TableArm
{
    const char* name;
    const struct ToriDraw_Kernel* (*get)(void);
    /* Tables whose raster is the branching family draw the same pixels
     * whether or not the whole-model door is used, so they share one hash. */
    bool branching_family;
};

static const struct TableArm g_tables[] = {
    { "software-painter", ToriDraw_KernelGetSoftwarePainter, true },
    { "sprite-baker", ToriDraw_KernelGetSpriteBaker, true },
    { "software-scanline", ToriDraw_KernelGetSoftwareScanline, false },
    { "software-zbuffered", ToriDraw_KernelGetSoftwareZBuffered, false },
};

#define TABLE_COUNT ((int)(sizeof(g_tables) / sizeof(g_tables[0])))

struct SortBatchArm
{
    const char* name;
    int flat;
    int batch;
};

static const struct SortBatchArm g_arms[] = {
    { "bucket/batch=0", 0, 0 },
    { "bucket/batch=1", 0, 1 },
    { "flat/batch=0", 1, 0 },
    { "flat/batch=1", 1, 1 },
};

#define ARM_COUNT ((int)(sizeof(g_arms) / sizeof(g_arms[0])))

static void
test_table_matrix(struct ToriDraw_Scene* scene, toripixel_t* pixels)
{
    struct StackModel stack;
    struct StackModel zstack;
    uint64_t table_hash[TABLE_COUNT];
    long table_covered[TABLE_COUNT];
    bool table_ok[TABLE_COUNT];

    stack_model_init(&stack, 11);
    stack_model_init(&zstack, 9);
    /* The depth-tested placement's model opts in; the scene carries the buffer
     * because it was created with TORIDRAW_SCENE_MODEL_ZBUFFER. */
    zstack.model.flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;

    printf("table x sort x batch: the picture must not depend on the last two\n");

    for( int t = 0; t < TABLE_COUNT; t++ )
    {
        uint64_t first = 0;
        long first_covered = 0;

        table_ok[t] = true;
        for( int a = 0; a < ARM_COUNT; a++ )
        {
            uint64_t h;
            long covered = 0;

            ToriDraw_FaceSortSetFlat(g_arms[a].flat);
            ToriDraw_RasterBatchSetArmed(g_arms[a].batch);

            h = render_arm(
                scene, pixels, &stack, &zstack, g_tables[t].get(), &covered);

            if( a == 0 )
            {
                first = h;
                first_covered = covered;
                continue;
            }
            if( h != first )
            {
                table_ok[t] = false;
                CHECK(
                    false,
                    "%s: arm %s hashes %016llx, arm %s hashes %016llx -- a stage "
                    "changed the picture that was only allowed to change the speed",
                    g_tables[t].name,
                    g_arms[0].name,
                    (unsigned long long)first,
                    g_arms[a].name,
                    (unsigned long long)h);
            }
            if( covered != first_covered )
                CHECK(
                    false,
                    "%s: arm %s covers %ld px, arm %s covers %ld",
                    g_tables[t].name,
                    g_arms[0].name,
                    first_covered,
                    g_arms[a].name,
                    covered);
        }

        table_hash[t] = first;
        table_covered[t] = first_covered;

        /* A matrix of blank frames agrees with itself perfectly. */
        CHECK(
            first_covered > 2000,
            "%s: only %ld covered pixels -- the fixture did not reach the kernels",
            g_tables[t].name,
            first_covered);

        if( table_ok[t] )
            printf(
                "  ok   %-19s %016llx  %ld px, %d arms agree\n",
                g_tables[t].name,
                (unsigned long long)table_hash[t],
                table_covered[t],
                ARM_COUNT);
    }

    /*
     * The branching family draws the same pixels with and without its
     * whole-model door: software-painter uses the run doors where they are
     * built, sprite-baker never does, and that is a speed decision.
     *
     * Off a lane with no presorted-run assembly the two are literally the same
     * walk, so this passes trivially -- it is the lane WITH the assembly that
     * this is for, and it is the only automated check that the eight run doors
     * agree with the C per-face kernels through the full pipeline rather than
     * one triangle at a time.
     */
    for( int t = 1; t < TABLE_COUNT; t++ )
    {
        if( !g_tables[t].branching_family )
            continue;
        CHECK(
            table_hash[t] == table_hash[0],
            "%s hashes %016llx but %s hashes %016llx -- the whole-model door "
            "does not draw what the per-face walk draws",
            g_tables[t].name,
            (unsigned long long)table_hash[t],
            g_tables[0].name,
            (unsigned long long)table_hash[0]);
    }

    /*
     * And the tables that are supposed to differ, do. Without this the whole
     * matrix could be green because every arm rendered nothing distinguishable
     * -- the classic way a hash-equality suite rots into a tautology.
     */
    for( int t = 0; t < TABLE_COUNT; t++ )
    {
        if( g_tables[t].branching_family )
            continue;
        CHECK(
            table_hash[t] != table_hash[0],
            "%s hashes the same as %s -- a different rasteriser produced an "
            "identical frame, so this matrix is not distinguishing anything",
            g_tables[t].name,
            g_tables[0].name);
    }

    printf("  ok   the branching family agrees, and the other rasterisers differ\n");
}

/*
 * The GPU table has no raster stage, so it cannot be in the pixel comparison.
 * What it must still deliver is stage 2: the back-to-front order a vertex
 * upload consumes. That order has to be the painter's, or a GL frame and a
 * software frame disagree about what is in front.
 *
 * It must also NOT leave the presort stash behind. Nothing downstream of a GPU
 * table walks a software span, so filling sm_face_x4/y4 would be stores into a
 * buffer no one loads -- the exact regression the presort split exists to
 * prevent.
 */
static void
test_gpu_table_order(struct ToriDraw_Scene* scene)
{
    struct StackModel stack;
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = 420, .yaw = 512 };
    int painter_count;
    int gpu_count;
    int* painter_order;
    bool same = true;

    printf("the gpu table: stage 2 only, same order, and no stash\n");

    stack_model_init(&stack, 11);

    ToriDraw_FaceSortSetFlat(1);
    ToriDraw_RasterBatchSetArmed(1);

    ToriDraw_RenderModel1ProjectWithTable(
        stack.handle, scene, &position, &viewport, &camera,
        ToriDraw_KernelGetSoftwarePainter());
    painter_count = ToriDraw_RenderModel2SortFacesWithTable(
        stack.handle, scene, ToriDraw_KernelGetSoftwarePainter());

    painter_order = malloc((size_t)(painter_count > 0 ? painter_count : 1) * sizeof(*painter_order));
    if( !painter_order )
    {
        CHECK(false, "gpu: order snapshot allocation");
        return;
    }
    memcpy(painter_order, ToriDraw_FaceOrder(scene), (size_t)painter_count * sizeof(*painter_order));

    ToriDraw_RenderModel1ProjectWithTable(
        stack.handle, scene, &position, &viewport, &camera, ToriDraw_KernelGetGpu());
    gpu_count = ToriDraw_RenderModel2SortFacesWithTable(
        stack.handle, scene, ToriDraw_KernelGetGpu());

    CHECK(gpu_count == painter_count, "gpu: %d faces ordered, painter ordered %d",
          gpu_count, painter_count);
    CHECK(painter_count > 0, "gpu: the fixture produced no visible faces");

    if( gpu_count == painter_count )
    {
        const int* gpu_order = ToriDraw_FaceOrder(scene);

        for( int i = 0; i < gpu_count; i++ )
        {
            if( gpu_order[i] != painter_order[i] )
            {
                same = false;
                CHECK(false, "gpu: order differs at %d (%d vs %d)", i, gpu_order[i],
                      painter_order[i]);
                break;
            }
        }
    }

    CHECK(!scene->sm_face_xy_valid,
          "gpu: the sort stashed for a table with no software raster");

    if( same )
        printf("  ok   %d faces, order for order, and the stash stayed empty\n", gpu_count);

    free(painter_order);
}

/*
 * Taking a table is supposed to answer "will I actually get what I selected?"
 * before the first frame, not in a profile afterwards.
 */
static void
test_validate_and_scratch(void)
{
    struct ToriDraw_Scene* small;
    struct ToriDraw_Scene* full;
    const char* why = NULL;
    enum ToriDraw_KernelFit fit;

    printf("validate: a table against the scene it will draw into\n");

    small = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_MODEL_ZBUFFER, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    full = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(small != NULL && full != NULL, "validate: scenes allocated");
    if( !small || !full )
        return;

    for( int t = 0; t < TABLE_COUNT; t++ )
    {
        why = NULL;
        fit = ToriDraw_KernelValidate(g_tables[t].get(), small, &why);
        CHECK(why != NULL, "validate: %s left `why` NULL", g_tables[t].name);
        CHECK(fit != TORIDRAW_KERNEL_FIT_INCOMPATIBLE,
              "validate: %s is INCOMPATIBLE with a stock small scene (%s)",
              g_tables[t].name, why ? why : "?");
        CHECK(ToriDraw_KernelEnsureScratch(small, g_tables[t].get()),
              "validate: ensure-scratch failed for %s", g_tables[t].name);
    }

    /* The flat sort's key arrays are small-tier scratch, so on a full scene the
     * painter cannot be the kernel it names. That is not an error -- the frame
     * draws, down the bucket walk -- and DEGRADED is how a caller who chose it
     * for the speed finds out. */
    ToriDraw_FaceSortSetFlat(1);
    why = NULL;
    fit = ToriDraw_KernelValidate(ToriDraw_KernelGetSoftwarePainter(), full, &why);
    CHECK(fit != TORIDRAW_KERNEL_FIT_INCOMPATIBLE,
          "validate: the painter must still DRAW on a full scene (%s)", why ? why : "?");

    printf("  ok   %d tables validate and provision on a small scene\n", TABLE_COUNT);

    ToriDraw_SceneFree(small);
    ToriDraw_SceneFree(full);
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    toripixel_t* pixels;
    struct ToriDraw_Texture* texture;

    ToriDraw_Init();

    /* The tier the client actually ships (src/app.c): SMALL is what makes the
     * flat sort, its keys and the presort stash exist at all, so a matrix run
     * on a full scene would silently test one sort twice. */
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    pixels = calloc((size_t)VIEW_STRIDE * VIEW_HEIGHT, sizeof(*pixels));
    texture = make_test_texture();

    CHECK(scene != NULL, "scene allocation");
    CHECK(pixels != NULL, "framebuffer allocation");
    CHECK(texture != NULL, "texture allocation");
    if( !scene || !pixels || !texture )
        return 1;

    ToriDraw_SceneSetTexture(scene, TEST_TEXTURE_ID, texture);
    CHECK(ToriDraw_SceneZBufferResize(scene, VIEW_STRIDE, VIEW_HEIGHT),
          "z-buffer resize");

    test_table_matrix(scene, pixels);
    test_gpu_table_order(scene);
    test_validate_and_scratch();

    /* Leave the process knobs as they were found. */
    ToriDraw_FaceSortSetFlat(-1);
    ToriDraw_RasterBatchSetArmed(-1);

    ToriDraw_SceneFree(scene);
    free(pixels);

    if( failures )
    {
        printf("\ntoridraw_kernel_matrix_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nall kernel matrix checks passed\n");
    return 0;
}
