#ifndef TRSPK_WORLD_BUILD_H
#define TRSPK_WORLD_BUILD_H

#include "trspk_batch32.h"
#include "trspk_element_mask.h"
#include "trspk_vertex.h"

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * THE WORLD BUILD, PREPARED A SLICE AT A TIME (2026-11-12): the part of it
 * every retained Batch32 lane shares (GL3, WebGL2, GLES3).
 *
 * A world build is a batch of ~18,500 element poses, ~700k faces, ~100 MB of
 * vertices. It used to be baked whole on the frame that received it, together
 * with every frame of every animated loc's tracks (ANIM_LOAD): 30-280 ms on
 * one frame. Now nothing is baked on arrival:
 *
 *   ADD          is a record (TRSPK_WorldRecord). It is baked by the first
 *                draw that resolves to it, or by the lane's prefill,
 *                whichever is first.
 *   ANIM_LOAD    announces a track (TRSPK_AnimTrack); each pose is baked on
 *                the first draw that asks for it.
 *   prefill      runs after the frame is drawn, in what is left of the
 *                frame's work budget (trspk_world_build_slice_us). The unit is
 *                one record's bake; the lane reads its clock before each
 *                (trspk_world_prefill_more / _next), so a slice overruns by
 *                at most one model.
 *
 * A BACKGROUND rebuild goes further: the next scene's build is prepared in a
 * second slot (`stage`) beside the drawn one (`world`) before the swap, and
 * the swap itself is an exchange of the two:
 *
 *   BATCH3D_STAGE_BEGIN  opens the stage (render/torirs_render.h has the
 *                        command contract).
 *   prefill              bakes the drawn build's records first, then the
 *                        stage's; the lane uploads what it baked to the
 *                        stage's GPU buffer as it goes.
 *   prepared             when the stage is baked and uploaded, the lane says
 *                        so (ToriDraw_SceneStageBatchMarkPrepared).
 *   BATCH3D_SWAP         the stage and the drawn build change places; the old
 *                        build is dropped, its CPU storage released by a later
 *                        prefill (trspk_world_build_stage_release).
 *
 * What is here is everything that does not touch a GL call or a vertex
 * layout: the records and their lookup, the two slots, the swap, the prefill's
 * order and budget, and the track table. What each lane keeps: the bake
 * itself (its vertex writer), its GPU buffers and their ranged upload, its
 * depth-pass materials, and the draw.
 */

/* One BATCH3D_*_ADD of a build, as received: what to bake and where. */
struct TRSPK_WorldRecord
{
    int element_id; /* its index (ToriDraw_ElementIndexOfRaw) */
    int anim_index;
    int pose_id;
    int next; /* the next record of the same element index, -1 */
    uint32_t vertex_count;
    bool baked;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Position world_position;
};

/*
 * One world build.
 *
 *   batch       the baked poses (Batch32): reservations made as records bake.
 *               Begun at BEGIN and never ended: it takes reservations until
 *               every record is baked.
 *   batch_id    the scene's id for it, -1 none
 *   drawable    draws may resolve to it (END or SWAP seen)
 *   adds_open   ADDs naming batch_id are taken (BEGIN/STAGE_BEGIN .. END/SWAP)
 *   detached    by element index: reloaded, animated or removed since its add,
 *               so the build no longer describes it (and its model may be gone)
 *   records     in arrival order; record_head by element index -> first, -1
 *   prefill_cursor, unbaked   the prefill's place, and how many are left
 *   record_vertices           the build's whole size, for the reservations
 */
struct TRSPK_WorldSlot
{
    struct TRSPK_Batch32* batch;
    int batch_id;
    bool drawable;
    bool adds_open;
    struct TRSPK_ElementMask detached;
    struct TRSPK_WorldRecord* records;
    uint32_t record_count;
    uint32_t record_cap;
    int* record_head;
    uint32_t record_head_cap;
    uint32_t prefill_cursor;
    uint32_t unbaked;
    uint64_t record_vertices;
};

/*
 * The drawn build and the stage. `stage.batch` is made by the first
 * STAGE_BEGIN (trspk_world_build_stage_ensure), so a client that only ever
 * rebuilds blocking never holds a second build.
 *
 * The prefill's share of a frame: it fills the frame up to prep_target_us of
 * work (TORIRS_PREP_TARGET_US, default 6000), never more than prep_slice_us
 * (TORIRS_PREP_SLICE_US, default 4000) and never less than prep_floor_us
 * (500), so a frame that is already long still moves the bake forward a
 * little. frame_elapsed_us: the host's work before the render, told to the
 * lane each frame (its SetFrameElapsed).
 */
struct TRSPK_WorldBuild
{
    struct TRSPK_WorldSlot world;
    struct TRSPK_WorldSlot stage;
    /* After a swap the stage holds the retired build's CPU storage; a later
     * prefill with room releases it (a ~100 MB free). */
    bool stage_release_pending;
    enum TRSPK_VertexFormat format;
    uint64_t prep_target_us;
    uint64_t prep_slice_us;
    uint64_t prep_floor_us;
    uint64_t frame_elapsed_us;
};

/* `prep_target_us` / `prep_slice_us`: the environment's strings, NULL or
 * empty for the defaults. */
void
trspk_world_build_init(
    struct TRSPK_WorldBuild* build,
    enum TRSPK_VertexFormat format,
    const char* prep_target_us,
    const char* prep_slice_us);

void
trspk_world_build_free(struct TRSPK_WorldBuild* build);

/* ---- records ------------------------------------------------------------- */

/* The record of (element, track, pose) in `w`, or -1. By element INDEX: see
 * trspk_world_build.c. */
int
trspk_world_record_find(
    const struct TRSPK_WorldSlot* w,
    int element_id,
    int anim_index,
    int pose_id);

/* A record of the open build, or the one already holding its key (a repeated
 * add replaces the pose). `vertex_count`: the model's faces times three. */
void
trspk_world_record_add(
    struct TRSPK_WorldSlot* w,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_count,
    struct ToriDraw_ModelHandle model,
    const struct ToriDraw_Position* world_position);

/* Record `r` is being baked now: marked baked and given its reservation,
 * into which the caller bakes record->model at record->world_position. Only
 * for an unbaked record whose element is not detached. */
void
trspk_world_record_take(
    struct TRSPK_WorldSlot* w,
    int r,
    struct TRSPK_Batch32Reservation* out_reservation);

/* Draws may resolve `element_id` to `w`: it is drawable and the element is
 * not detached from it. */
bool
trspk_world_slot_describes(
    const struct TRSPK_WorldSlot* w,
    int element_id);

/* The record a draw of (element, track, pose) should bake now -- the build
 * holds it but has not baked it yet -- or -1. */
int
trspk_world_record_unbaked(
    const struct TRSPK_WorldSlot* w,
    int element_id,
    int anim_index,
    int pose_id);

/*
 * The prefill's walk over `w`'s records in arrival order:
 *
 *   while( trspk_world_prefill_more(w) )
 *   {
 *       if( <clock> >= deadline )
 *           return true;
 *       r = trspk_world_prefill_next(w);
 *       if( r >= 0 )
 *           <bake record r>;
 *   }
 *
 * _more is false once nothing is left (what the cursor passed unbaked is
 * detached: never drawn from this build, so it stops counting); _next steps
 * the cursor over one record and returns it, or -1 when it needs no bake.
 */
bool
trspk_world_prefill_more(struct TRSPK_WorldSlot* w);

int
trspk_world_prefill_next(struct TRSPK_WorldSlot* w);

/* ---- the two slots --------------------------------------------------------- */

/* Empty a slot: its detach marks, its contents, its records. The caller
 * forgets whatever it keyed by the build's poses first. */
void
trspk_world_slot_drop(struct TRSPK_WorldSlot* w);

/* A new build in an emptied slot (trspk_world_slot_drop), taking adds. */
void
trspk_world_slot_begin(
    struct TRSPK_WorldSlot* w,
    int batch_id);

/* The slot an add with this id is for: the drawn build or the stage while it
 * takes adds, else NULL. */
struct TRSPK_WorldSlot*
trspk_world_build_open_slot(
    struct TRSPK_WorldBuild* build,
    int batch_id);

/* END of the drawn build: drawable now; it keeps taking reservations until
 * every record is baked. False when END names no open build. */
bool
trspk_world_build_end(
    struct TRSPK_WorldBuild* build,
    int batch_id);

/* A batched element reloaded, animated or removed since the build: draws stop
 * resolving it to either build, until the next one. Marked on the stage too:
 * an element of the scene being staged can change before the swap, and its
 * record must not be baked from a model that may be gone. */
void
trspk_world_build_detach(
    struct TRSPK_WorldBuild* build,
    int element_id);

/* The stage's slot, made by the first STAGE_BEGIN. True when it was made by
 * this call: the lane makes the stage's GPU buffer then. */
bool
trspk_world_build_stage_ensure(struct TRSPK_WorldBuild* build);

/* STAGE_BEGIN(batch_id) into the dropped stage: a retired build not released
 * yet is simply reused. */
void
trspk_world_build_stage_begin(
    struct TRSPK_WorldBuild* build,
    int batch_id);

/* There is a stage taking or holding a build. */
bool
trspk_world_build_stage_open(const struct TRSPK_WorldBuild* build);

/* BATCH3D_CLEAR(batch_id, clear_all, scene_reset): which builds it drops. The
 * stage goes with a cancelled rebuild (its own id) or a whole-scene reset;
 * view 0's static pool cleared for the swap leaves it. */
bool
trspk_world_build_clear_hits_world(
    const struct TRSPK_WorldBuild* build,
    int batch_id,
    bool clear_all);

bool
trspk_world_build_clear_hits_stage(
    const struct TRSPK_WorldBuild* build,
    int batch_id,
    bool scene_reset);

/*
 * SWAP(batch_id): the stage becomes the drawn build. The caller has dropped
 * the drawn one (trspk_world_slot_drop, after its own forgetting) and swaps
 * its GPU sides alongside. What the stage had not baked yet bakes as the
 * drawn build's would; its CPU storage is released by a later prefill.
 */
void
trspk_world_build_swap(
    struct TRSPK_WorldBuild* build,
    int batch_id);

/* The stage's whole size plus an eighth, to reserve before its first bake
 * (its adds arrive together, so neither buffer grows mid-bake), or 0 once
 * reserved or while empty. */
uint64_t
trspk_world_build_stage_reserve_wanted(struct TRSPK_WorldBuild* build);

/* Free the retired build's storage and start the stage empty. The lane
 * re-points anything that held the old batch's VBO. */
void
trspk_world_build_stage_release(struct TRSPK_WorldBuild* build);

/* Anything for a prefill to do: a record to bake or storage to release. */
bool
trspk_world_build_has_work(const struct TRSPK_WorldBuild* build);

/* The prefill's slice for a frame whose render has taken `render_us` so far:
 * what the frame has left of prep_target_us, clamped to the floor and the
 * slice. */
uint64_t
trspk_world_build_slice_us(
    const struct TRSPK_WorldBuild* build,
    uint64_t render_us);

/* ---- announced animation tracks ----------------------------------------- */

/*
 * ANIM_LOAD's tracks, by element index, TRSPK_POSE_TRACK_COUNT per element:
 * the model and animation a track was announced with. A track's pose is baked
 * when a draw first asks for it, not every frame of every track when the
 * track is announced -- a region's rebuild announced ~10k poses in one frame
 * (148 ms, 380 MB of static arena) and draws a few hundred. The pointers are
 * compared, never dereferenced: a draw bakes from its own.
 */
struct TRSPK_AnimTrack
{
    int element_id; /* its index (ToriDraw_ElementIndexOfRaw), -1: none */
    int announced_id; /* the id ANIM_LOAD named: the poses are baked under it,
                       * as ANIM_LOAD baked them, so its unloads find them */
    void const* model;
    void const* animation;
    struct ToriDraw_Position world_position; /* ANIM_LOAD's: the bake's placement */
};

struct TRSPK_AnimTracks
{
    struct TRSPK_AnimTrack* tracks;
    uint32_t cap; /* elements */
};

void
trspk_anim_tracks_free(struct TRSPK_AnimTracks* tracks);

/* ANIM_LOAD(element_id, anim_index): the track's poses will be baked from
 * `model` posed by `animation`, placed at `world_position`. */
void
trspk_anim_track_announce(
    struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index,
    void const* model,
    void const* animation,
    const struct ToriDraw_Position* world_position);

/* The track a draw of `element_id` with this model and animation may bake a
 * pose of, or NULL when none was announced for it. */
const struct TRSPK_AnimTrack*
trspk_anim_track_match(
    const struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index,
    void const* model,
    void const* animation);

void
trspk_anim_track_forget(
    struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index);

void
trspk_anim_track_forget_element(
    struct TRSPK_AnimTracks* tracks,
    int element_id);

void
trspk_anim_track_forget_all(struct TRSPK_AnimTracks* tracks);

#endif
