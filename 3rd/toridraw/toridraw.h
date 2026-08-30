#ifndef TORIDRAW_H
#define TORIDRAW_H

#include "toridraw_animation.h"
#include "toridraw_hsl16.h"
#include "toridraw_light_model.h"
#include "toridraw_lighting.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_model_sprite.h"
#include "toridraw_model_transform.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_render_hd.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"
#include "toridraw_vec.h"
#include "graphics/projection.h"
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
 * presorted-run kernels draw from it, and whether the flat SIMD face sort
 * (cull + composite keys + bitonic/radix) replaces the bucket sort. -1
 * hands the decision back to the environment.
 */
void
ToriDraw_RasterBatchSetArmed(int enabled);

void
ToriDraw_FaceSortSetFlat(int enabled);

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

int
ToriDraw_RenderModel2SortFacesPresorted(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene);

/*
 * The three stages, each taking the kernel that names them. A renderer that
 * runs the stages itself -- every platform renderer does, to hit-test between
 * projection and sort, or to sort for a vertex upload and never raster --
 * holds one kernel and passes it to each stage, so which projection, which
 * face sort and which raster it runs is one object chosen once at init
 * rather than three environment reads. A GPU renderer holds
 * ToriDraw_RasterKernelSDGetGpu(), whose raster vtable is NULL; stages 1 and
 * 2 accept it, stage 3 asserts.
 */
int
ToriDraw_RenderModel1ProjectWithKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    const struct ToriDraw_RasterKernelSD* kernel);

int
ToriDraw_RenderModel2SortFacesWithKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel);

int
ToriDraw_RenderModel2SortFacesPresortedWithKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel);

/* Stage 3 through a held kernel, with the per-model z-buffer swap that
 * ToriDraw_RenderModel3Raster does: a model flagged for the depth test goes
 * to the stock z-buffered kernel whatever painter kernel is held. */
int
ToriDraw_RenderModel3RasterWithKernel(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel);

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

/* Sort back to front WITHOUT the pre-sort store. The right entry for every
 * caller whose faces do not go to the batched software raster walk -- the
 * D3D9 and GL renderers, HD, the sprite baker. See toridraw.c. */
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

#endif
