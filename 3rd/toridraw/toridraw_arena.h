#ifndef TORIDRAW_ARENA_H
#define TORIDRAW_ARENA_H

/*
 * A scene whose scratch lives in memory the CALLER owns.
 *
 * ToriDraw_SceneNew answers "how big is a scene?" with a tier -- LOW_2K,
 * MED_4K, HIGH_8K -- chosen for a client that draws a world. The smallest of
 * them allocates 413 KB across twenty-odd malloc calls, because a world scene
 * has to hold the largest model it might ever meet and the sorter arrays that
 * go with it.
 *
 * A client that draws ONE item icon does not have that problem and cannot
 * afford that answer. It knows its largest model exactly -- it decoded it --
 * and it usually has no heap worth fragmenting. So this API inverts both
 * decisions: the caller states the limits, asks what they cost, and hands over
 * the bytes.
 *
 *     struct ToriDraw_SceneLimits limits = {
 *         .max_vertices = 256,
 *         .max_faces    = 512,
 *         .depth_levels = 256,
 *         .textures     = false,
 *     };
 *     static uint8_t g_scratch[12 * 1024];   // sized by the assert below
 *
 *     assert(ToriDraw_SceneArenaBytes(&limits) <= sizeof(g_scratch));
 *     struct ToriDraw_Scene* scene =
 *         ToriDraw_SceneArenaInit(g_scratch, sizeof(g_scratch), &limits);
 *
 * There is no free. The arena is the caller's; when it goes away, so does the
 * scene. ToriDraw_SceneFree ABORTS on an arena scene rather than handing it to
 * free(), because a stack buffer passed to free() is not a leak, it is a heap
 * corruption several frames later.
 *
 * ## What an arena scene does not have
 *
 * No asset registry (models/animations/sprites/fonts/sounds), no element pool,
 * no event queue, no shared-model store, no batch arena. Those are the world
 * client's, they are where the other 50 KB and the five hash maps go, and a
 * caller holding a model handle needs none of them: every render entry point
 * takes the handle directly.
 *
 * A texture map is the one optional extra, because the raster reads it through
 * the scene -- `limits.textures` provisions it in the arena.
 *
 * ## The sorter
 *
 * Always the CSR (small-mode) face sort, whose arrays scale with `max_faces`
 * rather than carrying the dense depth_levels x 512 bucket table. The
 * bitonic+radix key arrays and the batched walk's y-ordered stash are NOT
 * provisioned: they are what the stock branching raster's batched walk reads,
 * they cost 160 KB at LOW_2K, and a scene drawing one model at a time has
 * nothing to batch. ToriDraw_SceneHasScratch reports them absent, and the sort
 * reads that flag rather than a request, so the fallback is automatic.
 *
 * ## depth_levels
 *
 * The face sort buckets by depth, and a face whose depth exceeds this is
 * dropped. It must therefore be at least the model's bounding-sphere DIAMETER
 * in world units, not its face count -- ToriDraw_SceneArenaDepthForModel
 * answers it from a model, which is the only way to get it right without
 * knowing the geometry. The reference client's value is 1500.
 */

#include "toridraw_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Alignment ToriDraw_SceneArenaInit requires of its memory block. */
#define TORIDRAW_ARENA_ALIGN 16

/**
 * Set by ToriDraw_SceneArenaInit on `scene->flags`, so a scene can be asked
 * where it came from. Deliberately above the TORIDRAW_SCENE_* flags a caller
 * passes to ToriDraw_SceneNew: it is not selectable, it is a fact.
 */
#define TORIDRAW_SCENE_ARENA (1u << 8)

struct ToriDraw_SceneLimits
{
    /** Vertices in the largest model this scene will project. */
    int max_vertices;
    /** Faces in the largest model this scene will sort. */
    int max_faces;
    /** Face-sort depth resolution; see the note above. 0 selects 1500, the
     *  reference client's value. */
    int depth_levels;
    /** Provision the texture map (TORIDRAW_TEXTURE_ID_CAPACITY pointers).
     *  Leave false for a model with no textured faces -- the raster never
     *  reads it. Build with -DTORIDRAW_TEXTURE_ID_CAPACITY=64 to shrink it
     *  from 16 KB to 512 bytes on a client whose texture ids are small. */
    bool textures;
    /**
     * Provision the batched raster walk's y-ordered stash: 32 bytes per face,
     * which is more than the rest of the sorter costs.
     *
     * Only ONE table reads it -- the software painter, through its
     * whole-model door. The sprite baker (per-face) and the scanline table
     * never do, and ToriDraw_MiniView takes the sprite baker, so this is
     * false for the case this API exists for. Set it when driving an arena
     * scene with ToriDraw_KernelGetSoftwarePainter and you want the batched
     * walk rather than its fallback.
     *
     * Getting it wrong is not silent: ToriDraw_SceneEnsureScratch aborts on
     * an arena scene that is asked to grow.
     */
    bool batched_raster;
};

/**
 * Exactly the bytes ToriDraw_SceneArenaInit will consume for these limits,
 * including the alignment padding between groups but NOT any misalignment of
 * the block itself -- pass a TORIDRAW_ARENA_ALIGN-aligned pointer and this is
 * the whole cost.
 *
 * Deterministic: same limits, same number, on a given target. That is what
 * makes it usable in a _Static_assert against a fixed buffer.
 */
size_t
ToriDraw_SceneArenaBytes(const struct ToriDraw_SceneLimits* limits);

/**
 * Place a scene and its scratch in `memory`, and return it.
 *
 * Aborts rather than failing: a caller sizes with ToriDraw_SceneArenaBytes and
 * cannot get this wrong, and one that guessed the size is holding a buffer too
 * small for the model it is about to draw. Both of the ways that goes wrong --
 * a short buffer, a misaligned one -- produce a corrupt frame or a
 * general-protection fault a long way from here.
 *
 * The returned pointer aliases `memory`; the scene does not own it and must
 * not be passed to ToriDraw_SceneFree.
 */
struct ToriDraw_Scene*
ToriDraw_SceneArenaInit(
    void* memory,
    size_t bytes,
    const struct ToriDraw_SceneLimits* limits);

/** Whether this scene's scratch came from a caller's arena. */
static inline bool
ToriDraw_SceneIsArena(const struct ToriDraw_Scene* scene)
{
    return scene && (scene->flags & TORIDRAW_SCENE_ARENA) != 0;
}

/**
 * Limits that cover `hnd`: its vertex and face counts, and a depth resolution
 * derived from its bounds cylinder.
 *
 * The depth answer is the model's projected diameter plus a margin, which is
 * what the face sort's bucket index can reach once the model is rotated
 * arbitrarily. A model with no bounds cylinder gets the reference 1500.
 *
 * `textures` is set from whether the model has textured faces, so a caller
 * that passes the result straight to ToriDraw_SceneArenaBytes does not pay for
 * a texture map it will never read.
 */
void
ToriDraw_SceneLimitsForModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_SceneLimits* out_limits);

/** Widen `limits` so it also covers `hnd`. For a client holding several
 *  models in one scene: start from a zeroed limits and fold each model in. */
void
ToriDraw_SceneLimitsInclude(
    struct ToriDraw_SceneLimits* limits,
    struct ToriDraw_ModelHandle hnd);

#endif /* TORIDRAW_ARENA_H */
