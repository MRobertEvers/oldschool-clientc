#ifndef TORIDRAW_MINI_H
#define TORIDRAW_MINI_H

/*
 * ToriDraw for a client that draws a handful of models into a buffer.
 *
 * The full library is a world renderer: a scene holds an element pool, five
 * asset registries, an event queue and a shared-model store, and drawing a
 * frame means choosing a kernel table, provisioning its scratch, and driving
 * three stages by hand. All of that exists because a world client needs it.
 *
 * An item viewer, a wiki thumbnailer, a handheld with a 16-bit panel and a
 * quarter-megabyte of RAM needs none of it. It has one model, one buffer, and
 * a fixed budget. This header is that client's whole interface:
 *
 *     static _Alignas(16) uint8_t g_scratch[48 * 1024];
 *
 *     ToriDraw_Init();
 *
 *     struct ToriDraw_MiniLimits limits;
 *     ToriDraw_MiniLimitsForModel(model, &limits);
 *     assert(ToriDraw_MiniViewBytes(&limits) <= sizeof(g_scratch));
 *
 *     struct ToriDraw_MiniView* view =
 *         ToriDraw_MiniViewInit(g_scratch, sizeof(g_scratch), &limits);
 *
 *     struct ToriDraw_MiniTarget target = {
 *         .pixels = framebuffer, .width = 64, .height = 64, .stride = 64,
 *     };
 *     struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
 *     pose.yaw = 512;
 *
 *     ToriDraw_MiniDrawModel(view, model, &target, &pose);
 *
 * Five calls, no malloc, and one number -- ToriDraw_MiniViewBytes -- that a
 * _Static_assert can hold a fixed buffer to.
 *
 * ## The pixel format
 *
 * `toripixel_t` is whatever -DTORIDRAW_PIXEL_FORMAT selected; RGB565 and
 * ARGB1555 are two bytes wide. See graphics/pixel_format.h. Nothing in this
 * header is format-specific -- the target is `toripixel_t*` and every raster
 * family draws on every format.
 *
 * ## What it costs
 *
 * The view is the only per-client allocation, and it is the caller's. Beyond
 * it ToriDraw holds process-wide lookup tables, which on a RAM-constrained
 * target should be moved to ROM -- see TORIDRAW_TABLES_PRECOMPUTED in
 * graphics/shared_tables.h and tools/toridraw_tables_gen.c.
 */

#include "toridraw_arena.h"
#include "toridraw_types.h"
#include "graphics/pixel_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ToriDraw_Scene;

/** Opaque. A view IS an arena scene, at the same address -- the distinct type
 *  is what keeps a heap scene from reaching ToriDraw_MiniDrawModel and a mini
 *  view from reaching ToriDraw_SceneFree. ToriDraw_MiniViewScene crosses. */
struct ToriDraw_MiniView;

/** What the view must be able to hold. A superset of ToriDraw_SceneLimits,
 *  which it is layout-compatible with by containment rather than by cast. */
struct ToriDraw_MiniLimits
{
    struct ToriDraw_SceneLimits scene;
};

/** A destination buffer. `stride` is in PIXELS, not bytes. */
struct ToriDraw_MiniTarget
{
    toripixel_t* pixels;
    int width;
    int height;
    /** Pixels per row. 0 means `width`. */
    int stride;
};

/**
 * Where the model sits and how big it comes out.
 *
 * Angles are in the 2048-per-turn unit every ToriDraw rotation uses, not
 * degrees: a quarter turn is 512. `zoom` is the reference client's widget
 * zoom -- larger is closer, and it is a distance along the camera axis rather
 * than a scale factor, so doubling it does not halve the model.
 *
 * TORIDRAW_MINI_POSE_DEFAULT is the reference client's inventory-icon pose,
 * which is what a caller who has no opinion actually wants.
 */
struct ToriDraw_MiniPose
{
    /** Turn about the model's own vertical axis. 0..2047. */
    int yaw;
    /** Camera pitch: how far above the model the eye sits. 0..2047. */
    int pitch;
    /** Roll about the view axis, applied to the model. 0..2047. */
    int roll;
    /** Camera distance. 0 selects 2000, the reference widget default. */
    int zoom;
    /** Shift in destination pixels, from the centre of the target. */
    int offset_x;
    int offset_y;
    /** Orthographic instead of perspective. An icon baker wants this; a
     *  viewer showing the model as the game does, does not. */
    bool orthographic;
};

#define TORIDRAW_MINI_POSE_DEFAULT                                                                 \
    ((struct ToriDraw_MiniPose){                                                                   \
        .yaw = 0, .pitch = 280, .roll = 0, .zoom = 2000, .offset_x = 0, .offset_y = 0,             \
        .orthographic = false })

/**
 * Limits that cover `hnd` -- its vertex and face counts, its depth extent, and
 * whether it needs a texture map.
 *
 * Widen with ToriDraw_MiniLimitsInclude for a view that draws several models,
 * which is what a client showing an item beside its icon does.
 */
void
ToriDraw_MiniLimitsForModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_MiniLimits* out_limits);

void
ToriDraw_MiniLimitsInclude(
    struct ToriDraw_MiniLimits* limits,
    struct ToriDraw_ModelHandle hnd);

/** Exact bytes ToriDraw_MiniViewInit consumes for these limits. Deterministic
 *  for a given target, so it may be used in a _Static_assert. */
size_t
ToriDraw_MiniViewBytes(const struct ToriDraw_MiniLimits* limits);

/**
 * Place a view in `memory`, which must be TORIDRAW_ARENA_ALIGN-aligned and at
 * least ToriDraw_MiniViewBytes long. Aborts if it is not -- see
 * ToriDraw_SceneArenaInit for why that is an abort and not a NULL.
 *
 * ToriDraw_Init must have run first: the palette and trigonometric tables are
 * process-wide, not per view.
 *
 * There is no teardown. The memory is the caller's and the view holds nothing
 * else; when the buffer goes, so does the view.
 */
struct ToriDraw_MiniView*
ToriDraw_MiniViewInit(
    void* memory,
    size_t bytes,
    const struct ToriDraw_MiniLimits* limits);

/**
 * The scene behind the view, for a caller that wants the full API for one
 * thing -- registering a texture, driving the three stages by hand, hit
 * testing a projected model.
 *
 * It is an arena scene: it has no asset registry and no element pool, and
 * passing it to ToriDraw_SceneFree aborts. See toridraw_arena.h.
 */
struct ToriDraw_Scene*
ToriDraw_MiniViewScene(struct ToriDraw_MiniView* view);

/**
 * Draw one model into `target`.
 *
 * Does NOT clear the target: a caller compositing several models into one
 * buffer would have to undo it, and one drawing a single model already knows
 * what it wants behind it. ToriDraw_MiniClear is there for that.
 *
 * Returns false when the model projected to nothing -- entirely behind the
 * camera, or culled against the target's bounds. That is an ordinary answer
 * for a pose the caller chose, not an error, and the target is untouched.
 */
bool
ToriDraw_MiniDrawModel(
    struct ToriDraw_MiniView* view,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_MiniTarget* target,
    const struct ToriDraw_MiniPose* pose);

/** Fill the target with one pixel value. `toripixel_pack_argb8888(0)` is the
 *  colour key on every format, which is what an icon baker wants behind it. */
void
ToriDraw_MiniClear(
    const struct ToriDraw_MiniTarget* target,
    toripixel_t value);

/**
 * Register a texture the view's models reference by id.
 *
 * The view must have been built with `limits.scene.textures` true, which
 * ToriDraw_MiniLimitsForModel sets for a model with textured faces. The view
 * does not take ownership: `texture` must outlive it.
 */
void
ToriDraw_MiniSetTexture(
    struct ToriDraw_MiniView* view,
    int id,
    struct ToriDraw_Texture* texture);

#endif /* TORIDRAW_MINI_H */
