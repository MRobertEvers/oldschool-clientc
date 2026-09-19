#include "toridraw_mini.h"

#include "toridraw.h"
/* For TORIDRAW_RASTER_BATCH: whether this build HAS a whole-model door to
 * take. It is a fact about the lane, not a caller's choice, which is why it
 * is read here and not passed in. */
#include "toridraw_raster_batch.h"

#include <assert.h>
#include <string.h>

/*
 * A view IS its scene, at the same address.
 *
 * ToriDraw_SceneArenaInit places the scene at offset zero of the block, so the
 * two pointers are the same value and the cast below is not a coincidence to
 * be maintained -- it is the layout. The distinct type exists so a mini client
 * cannot be handed a heap scene by accident and a world client cannot pass its
 * scene here; ToriDraw_MiniViewScene is the one sanctioned crossing.
 *
 * The kernel table is not stored: the getters return process-lifetime objects
 * and asking for one is a load and a compare.
 *
 * WHICH TABLE, AND WHY IT IS NOT ALWAYS THE BAKER. A build whose lane has the
 * presorted-run assembly gets the software painter, whose whole-model door
 * hands a RUN of same-class faces to one call instead of paying the per-face
 * marshal on each; a build without it gets the per-face baker, because there
 * the door does not exist and the painter would be the same walk with staging
 * on top. The two draw the same pixels either way -- the assembly is scored
 * against the C it replaces -- so this only ever moves the time.
 *
 * The stash the door reads costs 32 bytes per face, and the arena provisions
 * it only when the limits ask; the two answers therefore have to come from
 * the same predicate, which is what mini_wants_batched() is for. Getting them
 * out of step is not silent -- ToriDraw_SceneEnsureScratch aborts on an arena
 * scene asked to grow -- but it is a bad way to find out.
 */

static bool
mini_wants_batched(void)
{
#ifdef TORIDRAW_RASTER_BATCH
    return true;
#else
    return false;
#endif
}

static const struct ToriDraw_Kernel*
mini_kernel(void)
{
    return mini_wants_batched() ? ToriDraw_KernelGetSoftwarePainter()
                                : ToriDraw_KernelGetSpriteBaker();
}

static struct ToriDraw_Scene*
mini_scene(struct ToriDraw_MiniView* view)
{
    return (struct ToriDraw_Scene*)view;
}

void
ToriDraw_MiniLimitsForModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_MiniLimits* out_limits)
{
    assert(out_limits);

    memset(out_limits, 0, sizeof(*out_limits));
    ToriDraw_SceneLimitsForModel(hnd, &out_limits->scene);
    out_limits->scene.batched_raster = mini_wants_batched();
}

void
ToriDraw_MiniLimitsInclude(
    struct ToriDraw_MiniLimits* limits,
    struct ToriDraw_ModelHandle hnd)
{
    assert(limits);
    ToriDraw_SceneLimitsInclude(&limits->scene, hnd);
    limits->scene.batched_raster = mini_wants_batched();
}

size_t
ToriDraw_MiniViewBytes(const struct ToriDraw_MiniLimits* limits)
{
    assert(limits);
    return ToriDraw_SceneArenaBytes(&limits->scene);
}

struct ToriDraw_MiniView*
ToriDraw_MiniViewInit(
    void* memory,
    size_t bytes,
    const struct ToriDraw_MiniLimits* limits)
{
    struct ToriDraw_Scene* scene;

    assert(memory);
    assert(limits);
    /* The palette a solid span indexes and the sine table the projection reads
     * are process-wide and are not this view's to build. A zero palette draws
     * a black model, which reads as "the model is wrong" rather than "the
     * library was not initialised". */
    assert(g_sin_table && "call ToriDraw_Init() before building a view");

    scene = ToriDraw_SceneArenaInit(memory, bytes, &limits->scene);

    /*
     * Validate and report once, here, rather than on the first draw. The
     * sprite baker's per-face raster reads tmp_face_order and nothing else, so
     * on an arena scene -- which never provisions the batched walk's stash
     * unless asked -- this asks for exactly what the arena holds and the take
     * cannot allocate.
     */
    ToriDraw_KernelTake(scene, mini_kernel());

    return (struct ToriDraw_MiniView*)scene;
}

struct ToriDraw_Scene*
ToriDraw_MiniViewScene(struct ToriDraw_MiniView* view)
{
    assert(view);
    return mini_scene(view);
}

void
ToriDraw_MiniClear(
    const struct ToriDraw_MiniTarget* target,
    toripixel_t value)
{
    int stride;
    int y;

    assert(target);
    assert(target->pixels);
    assert(target->width > 0);
    assert(target->height > 0);

    stride = target->stride > 0 ? target->stride : target->width;
    for( y = 0; y < target->height; y++ )
    {
        toripixel_t* row = target->pixels + (size_t)y * (size_t)stride;
        int x;

        for( x = 0; x < target->width; x++ )
            row[x] = value;
    }
}

void
ToriDraw_MiniSetTexture(
    struct ToriDraw_MiniView* view,
    int id,
    struct ToriDraw_Texture* texture)
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_TextureMap* map;

    assert(view);
    assert(texture);
    assert(id >= 0 && id < TORIDRAW_TEXTURE_ID_CAPACITY);

    scene = mini_scene(view);
    /* Not ToriDraw_SceneTexState: that one builds the state on demand, and on
     * an arena scene there is nothing to build it from. A view whose limits
     * said `textures = false` is a view sized without a texture map, and the
     * caller asked for something their own limits ruled out. */
    assert(scene->tex_state && "view was built with limits.scene.textures == false");

    /*
     * The slot is written directly rather than through ToriDraw_SceneSetTexture
     * or ToriDraw_TextureMapSet, because both of those OWN what they are
     * handed: they free the texture they displace, and the scene frees the lot
     * at shutdown. A mini view has no shutdown and does not own its assets --
     * the caller's texture is very likely a const array in flash, and freeing
     * one is not a leak, it is a fault.
     */
    map = &scene->tex_state->texture_map;
    map->textures[id] = texture;
    if( id >= map->count )
        map->count = id + 1;
}

bool
ToriDraw_MiniDrawModel(
    struct ToriDraw_MiniView* view,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_MiniTarget* target,
    const struct ToriDraw_MiniPose* pose)
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position position;
    const struct ToriDraw_BoundsCylinder* bounds;
    int stride;
    int zoom;
    int model_height;
    int result;

    assert(view);
    assert(target);
    assert(target->pixels);
    assert(target->width > 0);
    assert(target->height > 0);
    assert(pose);

    scene = mini_scene(view);
    stride = target->stride > 0 ? target->stride : target->width;
    assert(stride >= target->width);

    zoom = pose->zoom > 0 ? pose->zoom : 2000;

    memset(&view_port, 0, sizeof(view_port));
    view_port.width = target->width;
    view_port.height = target->height;
    view_port.stride = stride;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = target->width;
    view_port.clip_bottom = target->height;
    view_port.x_center = target->width / 2 + pose->offset_x;
    view_port.y_center = target->height / 2 + pose->offset_y;

    memset(&camera, 0, sizeof(camera));
    camera.pitch = pose->pitch;
    camera.yaw = 0;
    camera.roll = 0;
    camera.near_plane_z = 1;

    if( pose->orthographic )
    {
        /*
         * Match the perspective framing at the same `zoom` rather than
         * defining a second, unrelated scale: a caller flipping this flag is
         * asking to remove the foreshortening, not to resize the model. The
         * perspective projection is `coord * scale / z` and the model sits at
         * z ~= zoom, so `scale / zoom` in 16.16 is the parallel factor that
         * lands its silhouette in the same pixels.
         */
        camera.projection_mode = TORIDRAW_PROJECTION_MODE_PARALLEL;
        camera.parallel_zoom16 =
            (int)(((int64_t)TORIDRAW_PROJECTION_SCALE_DEFAULT << TORIDRAW_ORTHO_ZOOM_SHIFT) /
                  (int64_t)zoom);
        /* Nothing is unsafe under a parallel projection, so the near plane is
         * policy rather than a singularity -- and this view's policy is that a
         * model is never clipped by the camera sitting inside it. */
        camera.near_plane_z = -0x40000000;
    }
    else
    {
        camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
        camera.projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT;
    }

    /*
     * Where the eye is, expressed as where the MODEL is: ToriDraw projects
     * about a fixed origin and a camera pitch, so pitching the view by `pitch`
     * at distance `zoom` puts the model at (0, sin*zoom, cos*zoom), and its
     * vertical centre is then lifted by half its own height so a tall model
     * frames like a short one. This is the reference client's widget-model
     * placement (ObjType.getSprite / Model.drawModel2D), reproduced.
     */
    bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    /* Without it the cull returns TORIDRAW_CULL_ERROR and the model silently
     * does not draw -- a blank target with no other symptom, which is the
     * single easiest way to lose an afternoon with this library. Say which
     * call is missing instead. */
    assert(bounds && "call ToriDraw_ModelSetBoundsCylinder on the model first");
    model_height = bounds->max_y - bounds->min_y;

    memset(&position, 0, sizeof(position));
    position.x = 0;
    position.y = ((ToriDraw_Sin(pose->pitch) * zoom) >> 16) - (target->height / 2) +
                 (model_height / 2);
    position.z = (ToriDraw_Cos(pose->pitch) * zoom) >> 16;
    position.pitch = 0;
    position.yaw = pose->yaw;
    position.roll = pose->roll;

    result = ToriDraw_RenderModelWithTable(
        hnd,
        scene,
        &position,
        &view_port,
        &camera,
        target->pixels,
        mini_kernel());

    return result == TORIDRAW_CULL_VISIBLE;
}
