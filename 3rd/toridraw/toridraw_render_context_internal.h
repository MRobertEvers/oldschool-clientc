#ifndef TORIDRAW_RENDER_CONTEXT_INTERNAL_H
#define TORIDRAW_RENDER_CONTEXT_INTERNAL_H

#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>

struct ToriDraw_RenderContext;

/*
 * Additional simultaneously live complete-render contexts allocated with
 * every scene. The scene's resident scratch is the primary context, so the
 * default supports eight nested projections beneath it. Override at compile
 * time for an application with a deliberately shallower or deeper callback
 * stack.
 */
#ifndef TORIDRAW_NESTED_RENDER_CONTEXTS
#define TORIDRAW_NESTED_RENDER_CONTEXTS 8
#endif

/*
 * One dynamic extent of model rendering.  The first extent on a scene keeps
 * using the scene's resident scratch.  A nested extent may request isolation;
 * it then uses one of the independent scratch contexts allocated with the
 * scene.  The scope object itself is intentionally a tiny stack local.
 */
struct ToriDraw_RenderContextScope
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_RenderContext* nested_context;
};

/* Cold helpers used only when a complete render actually nests. */
bool
ToriDraw_RenderContextBeginNested(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_RenderContextScope* scope);

void
ToriDraw_RenderContextEndNested(struct ToriDraw_RenderContextScope* scope);

/*
 * Keep the overwhelmingly common depth-zero path in the caller: one scene
 * depth load/branch at entry and one decrement at exit. The context swap and
 * its larger stack frame stay behind cold out-of-line helpers.
 */
static inline bool
ToriDraw_RenderContextBegin(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_RenderContextScope* scope,
    bool isolate_scratch)
{
    if( !scope )
        return false;
    scope->scene = NULL;
    scope->nested_context = NULL;
    if( !scene )
        return false;

    if( isolate_scratch && scene->render_context_depth != 0 )
        return ToriDraw_RenderContextBeginNested(scene, scope);

    scene->render_context_depth++;
    scope->scene = scene;
    return true;
}

static inline void
ToriDraw_RenderContextEnd(struct ToriDraw_RenderContextScope* scope)
{
    struct ToriDraw_Scene* scene;

    assert(scope);
    scene = scope->scene;
    assert(scene);
    assert(scene->render_context_depth > 0);

    if( scope->nested_context )
    {
        ToriDraw_RenderContextEndNested(scope);
        return;
    }

    scene->render_context_depth--;
    scope->scene = NULL;
}

static inline bool
ToriDraw_RenderContextIsActive(const struct ToriDraw_Scene* scene)
{
    return scene && scene->render_context_depth != 0;
}

#endif
