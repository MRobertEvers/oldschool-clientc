#ifndef TORIDRAW_H
#define TORIDRAW_H

#include "toridraw_animation.h"
#include "toridraw_arena.h"
#include "toridraw_hsl16.h"
#include "toridraw_light_model.h"
#include "toridraw_lighting.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_mini.h"
#include "toridraw_model.h"
#include "toridraw_model_sprite.h"
#include "toridraw_model_transform.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_render_hd.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"
#include "toridraw_vec.h"
#include "impl/projection/projection.scalar_reference.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void
ToriDraw_Init(void);

/*
 * The two runtime knobs the raster A/B benchmark turns, overriding the
 * TORIDRAW_RASTER_BATCH and TORIDRAW_FACE_SORT environment variables:
 * whether the depth sort leaves its pre-sorted stash and the batched
 * presorted-run kernels draw from it, and whether the bitonic+radix SIMD face
 * sort (cull + composite keys + bitonic/radix) replaces the bucket sort. -1
 * hands the decision back to the environment.
 */
void
ToriDraw_RasterBatchSetArmed(int enabled);

void
ToriDraw_FaceSortSetBitonicRadix(int enabled);

/**
 * Compatibility selector used by the legacy render entry points. Off by
 * default; ToriDraw_Init() also honours TORIDRAW_RASTER_SCANLINE=1 in the
 * environment. Kernel-explicit calls are unaffected.
 */
void
ToriDraw_RasterSetScanline(bool enabled);

bool
ToriDraw_RasterGetScanline(void);

/**
 * Prepare camera-only projection constants once for a run of models using the
 * same camera object. Call again after changing that camera or the selected
 * sine/cosine tables, and clear it when the run ends. Other camera objects use
 * the ordinary portable projection path.
 */
void
ToriDraw_ScenePrepareProjectionCamera(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Camera* camera);

void
ToriDraw_SceneClearProjectionCamera(struct ToriDraw_Scene* scene);

/*
 * A scratch VIEW of a scene: a second ToriDraw_Scene that shares the scene's
 * elements, models, animations, textures and events, and owns only the
 * per-model scratch (the projected vertex arrays, the sort tables, the face
 * order). A second thread projects and sorts on the view while the first
 * uses the scene itself; each has its own bench, and neither writes the
 * other's.
 *
 * The view is a SNAPSHOT of the scene's non-scratch fields, taken by New and
 * again by every Sync. Sync it once per frame, after the last mutation of
 * the scene and before the view is used, and use it only while the scene is
 * not being mutated -- a view that outlives a scene rebuild points at freed
 * storage. Sync re-sizes the view's scratch when the scene's capacities or
 * resident scratch groups changed.
 *
 * ToriDraw_SceneFrameEnd, ToriDraw_SceneFree and every scene mutator are for
 * the scene, never the view: the view's only destructor is
 * ToriDraw_SceneScratchViewFree, which frees the scratch it owns and nothing
 * it shares.
 */
struct ToriDraw_Scene*
ToriDraw_SceneScratchViewNew(const struct ToriDraw_Scene* source);

void
ToriDraw_SceneScratchViewSync(
    struct ToriDraw_Scene* view,
    const struct ToriDraw_Scene* source);

void
ToriDraw_SceneScratchViewFree(struct ToriDraw_Scene* view);

void
ToriDraw_RenderModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer);

/* Project and raster one model through a complete SD kernel, satisfying the
 * face-sort and depth-buffer requirements in `kernel->flags`. */
int
ToriDraw_RenderModelWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel);

int
ToriDraw_RenderModel1Project(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);

/*
 * The three stages, driven by a table.
 *
 * A renderer that runs the stages by hand -- every platform renderer does, to
 * hit-test between projection and sort, or to sort for a vertex upload and
 * never raster -- holds one ToriDraw_Kernel and passes it to each. Which
 * projection, which face sort and which raster it runs is then one object
 * chosen once at init.
 *
 * Stage 2 decides the presort itself from the table, so there is no presorted
 * twin to pick between and no way to pick wrong.
 */
int
ToriDraw_RenderModel1ProjectWithTable(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    const struct ToriDraw_Kernel* table);

int
ToriDraw_RenderModel2SortFacesWithTable(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Kernel* table);

/* Asserts on a GPU table, whose raster slot is NULL. */
int
ToriDraw_RenderModel3RasterWithTable(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_Kernel* table);

/** All three stages through one table. */
int
ToriDraw_RenderModelWithTable(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_Kernel* table);

/* Sort back to front WITHOUT the pre-sort store, for a caller that names no
 * kernel -- HD, the sprite baker, the tests. A caller that holds a kernel or a
 * table takes the entry for it and does not state the choice. See toridraw.c. */
int
ToriDraw_RenderModel2SortFaces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene);

int
ToriDraw_RenderModel3Raster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth);

/* Raster the active projected model. A kernel that requires face sorting
 * assumes ToriDraw_RenderModel2SortFaces has already completed for it. */
int
ToriDraw_RenderModel3RasterWithRasterKernel(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel);

/**
 * ToriDraw_RenderModel, resolved per pixel instead of per face.
 *
 * Project, then raster through the depth-tested (`zbuf`) family with **no face
 * sort at all** — no depth buckets, no `face_priorities`, no `model_priority`.
 * Faces are drawn in the order the model stores them and the z-buffer decides
 * what is visible. Back-facing faces are still culled: the face sort is what
 * used to do that, and there is no face sort here.
 *
 * TORIDRAW_MODEL_FLAG_ZBUFFER is a different feature and is not consulted. That
 * flag keeps the sort and adds a depth test on top of it, which is the
 * conservative choice for a game frame; this discards the sort outright, which
 * is the right choice when the order is the problem — a model whose parts
 * interpenetrate, or one carrying priority bytes from a client that meant
 * something else by them. Layering BETWEEN models is untouched either way; the
 * scene's painter order still applies and the buffer is reset per model.
 *
 * The scene needs no TORIDRAW_SCENE_MODEL_ZBUFFER flag: calling this is the
 * opt-in and the depth scratch is sized on the first call. Returns the cull
 * result, as ToriDraw_RenderModel1Project does.
 */
int
ToriDraw_RenderZBuffered(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth);

/* The explicit kernel must require a z-buffer. Its face-sort flag is still
 * honored; the compatibility function above uses a model-order Z kernel. */
int
ToriDraw_RenderZBufferedWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel);

/**
 * Cull one model WITHOUT projecting its vertices, and say whether one screen
 * point could possibly lie on it.
 *
 * The bound tested is the model's bounds cylinder projected to a screen box --
 * the same conservative box ToriDraw_FastCull rejects models with, so every
 * vertex the full projection would produce lies inside it. `false` therefore
 * means the point cannot hit this model, whatever its geometry.
 *
 * For a GPU backend, which draws from baked world-space vertices and uses the
 * software projection only for the cull and the pick: a model this rejects
 * needs no projection at all.
 *
 * Returns the same TORIDRAW_CULL_* verdict ToriDraw_RenderModel1Project would.
 */
int
ToriDraw_RenderModel1CullPoint(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    int point_x,
    int point_y,
    bool* out_point_inside);


/** Bounding-box hit test against the last projected model (reference
 *  Model.useAABBMouseCheck). Cheaper and far more forgiving than the per-face
 *  test — the reference uses it for npcs, players and ground objs. */
bool
ToriDraw_ProjectedModelContainsAabb(
    struct ToriDraw_Scene* scene,
    int screen_x,
    int screen_y);

/** Exact per-face point-in-triangle test against the last projected model. */
bool
ToriDraw_ProjectedModelContainsPoint(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y);

/** Per-face mouse hit test against the last projected model, ported from the
 *  reference render2 pick pass. A face counts when the cursor (grown by 5px)
 *  overlaps its screen bounding box — deliberately loose, so the gaps in a
 *  sparse model still pick. Faces hidden by lighting/mergeNormals and faces
 *  with a near-clipped vertex are skipped; backfacing ones are not. */
bool
ToriDraw_ProjectedModelMouseHitTest(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y);

/** The analogous test for a GROUND TILE mesh, which the reference does not pick
 *  through Model.draw at all — and which therefore differs from the model test
 *  in BOTH of its rules:
 *
 *  - Hidden faces still pick. World3D.drawTileUnderlay / drawTileOverlay run
 *    their click test BEFORE the `!== 12345678` colour gate, so a tile triangle
 *    whose colour says "draw nothing" still sets clickTileX/clickTileZ. A tile
 *    decoded from a 0xFF00FF overlay (or from no underlay at all) is therefore
 *    invisible AND clickable in the reference, where the same face on a model
 *    would be neither.
 *  - It is exact containment, NOT the model test's 5px-slop bounding box. The
 *    239 deob reaches it as class155 -> class112.method3946 -> method4206,
 *    which rejects on the bbox and then tests the three edge signs; Client-TS
 *    spells the same thing as World3D.insideTriangle. The slop exists so the
 *    gaps in a sparse model stay clickable; ground tiles tile the plane and
 *    have no gaps, and slop there would make every click match its neighbours
 *    too — which resolves the click to whichever of them drew last rather than
 *    to the tile under the cursor. */
bool
ToriDraw_ProjectedTileMouseHitTest(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y);


/*
 * Debug census of the bitonic+radix face sort's radix shapes (see
 * toridraw_radix_sort_depth): models this frame whose depth range let the
 * radix finish in one pass, and models that took two. A renderer that prints
 * frame statistics reads and clears them; nothing else touches them.
 */
/*
 * The projection gate's census, always on (seven increments per model):
 * models handed to ToriDraw_ProjectWithVTable, where each left, and for the
 * ones projected their vertex total and how many had a vertex count that is
 * not a multiple of four (the kernels' scalar tail). A renderer that prints
 * frame statistics reads and clears it.
 */
struct ToriDraw_ProjectCensus
{
    int calls;
    int cull_fast;
    int cull_error;
    int cull_aabb;
    int projected;
    int projected_vertices;
    int tail_models;
};
extern struct ToriDraw_ProjectCensus g_toridraw_project_census;

extern int g_toridraw_sort_k16_models;
extern int g_toridraw_sort_k16_declined;
extern int g_toridraw_radix_shallow_models;
extern int g_toridraw_radix_two_pass_models;
/* Same census for the priority emit: models whose priorities were all one
 * value (emitted in key order) against models that took the partition. */
extern int g_toridraw_prio_uniform_models;
extern int g_toridraw_prio_varied_models;

/*
 * The kernel A/B arm: 0 runs the current shape of every kernel that has a
 * control arm, 1 runs the previous shape. Flipped between frames by a
 * harness that alternates arms in one launch (the gles2 dual-core lane's
 * TORIRS_GLES2_DUALCORE_KERNEL_AB=1) so a kernel change is measured against
 * itself rather than against another launch. Read at the top of each sort,
 * so a flip mid-frame is safe; the arms must be bit-exact with each other.
 */
extern int g_toridraw_kernel_ab_arm;

#endif
