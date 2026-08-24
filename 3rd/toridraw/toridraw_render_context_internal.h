#ifndef TORIDRAW_RENDER_CONTEXT_INTERNAL_H
#define TORIDRAW_RENDER_CONTEXT_INTERNAL_H

#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>

struct ToriDraw_RenderContext;

/*
 * Additional simultaneously live complete-render contexts allocated with
 * every scene. The scene's resident scratch is the primary context, so the
 * default supports eight simultaneous nested leases beneath it. A complete
 * nested render uses one; recursive phase 3 may use one more for mutable depth
 * scratch. Override at compile time for an application with a deliberately
 * shallower or deeper callback stack.
 */
#ifndef TORIDRAW_NESTED_RENDER_CONTEXTS
#define TORIDRAW_NESTED_RENDER_CONTEXTS 8
#endif

#if defined(__GNUC__) || defined(__clang__)
#define TORIDRAW_RENDER_CONTEXT_COLD __attribute__((cold, noinline))
#define TORIDRAW_RENDER_CONTEXT_ALWAYS_INLINE inline __attribute__((always_inline))
#define TORIDRAW_RENDER_CONTEXT_RETURNS_NONNULL __attribute__((returns_nonnull))
#define TORIDRAW_RENDER_CONTEXT_LIKELY(value) __builtin_expect(!!(value), 1)
#else
#define TORIDRAW_RENDER_CONTEXT_COLD
#define TORIDRAW_RENDER_CONTEXT_ALWAYS_INLINE inline
#define TORIDRAW_RENDER_CONTEXT_RETURNS_NONNULL
#define TORIDRAW_RENDER_CONTEXT_LIKELY(value) (value)
#endif

/* Cold helpers reached only from an actual raster-callback re-entry. */
TORIDRAW_RENDER_CONTEXT_COLD
TORIDRAW_RENDER_CONTEXT_RETURNS_NONNULL
struct ToriDraw_RenderContext*
ToriDraw_RenderContextAcquireNested(struct ToriDraw_Scene* scene);

TORIDRAW_RENDER_CONTEXT_COLD
void
ToriDraw_RenderContextReleaseNested(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_RenderContext* context);

/* Reserve one preallocated context as scratch without publishing it as the
 * active projection context. Used by recursive phase 3 to isolate mutable
 * depth storage while continuing to read the outer prepared arrays. */
TORIDRAW_RENDER_CONTEXT_COLD
TORIDRAW_RENDER_CONTEXT_RETURNS_NONNULL
struct ToriDraw_RenderContext*
ToriDraw_RenderContextAcquireScratch(struct ToriDraw_Scene* scene);

TORIDRAW_RENDER_CONTEXT_COLD
void
ToriDraw_RenderContextReleaseScratch(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_RenderContext* context);

/*
 * The ordinary path activates the resident context at offset zero without
 * changing a counter or copying state. A callback re-entry takes the cold arm.
 * There are no application callbacks before rasterization, so keeping the
 * context active across projection and sorting costs only this pointer store
 * and lets the face loop avoid separate publication bookkeeping.
 */
static inline bool
ToriDraw_RenderContextTryAcquireRoot(struct ToriDraw_Scene* scene)
{
    assert(scene);
    if( TORIDRAW_RENDER_CONTEXT_LIKELY(!scene->active_render_context) )
    {
        scene->active_render_context = &scene->render_context;
        return true;
    }
    return false;
}

static inline void
ToriDraw_RenderContextReleaseRoot(struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(scene->active_render_context == &scene->render_context);
    scene->active_render_context = NULL;
}

static inline bool
ToriDraw_RenderContextIsActive(const struct ToriDraw_Scene* scene)
{
    return scene && scene->active_render_context != NULL;
}

bool
ToriDraw_RenderContextZBufferResize(
    struct ToriDraw_RenderContext* context,
    int stride,
    int rows);

bool
ToriDraw_RenderContextHasZBuffer(
    const struct ToriDraw_RenderContext* context,
    int stride,
    int rows);

int
ToriDraw_RenderContextProject(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_RenderContext* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);

int
ToriDraw_RenderContextSortFaces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_RenderContext* context);

void
ToriDraw_RenderContextRaster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_RenderContext* context,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth);

void
ToriDraw_RenderContextRasterZBuffered(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_RenderContext* context,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth);

#undef TORIDRAW_RENDER_CONTEXT_RETURNS_NONNULL
#undef TORIDRAW_RENDER_CONTEXT_LIKELY

#endif
