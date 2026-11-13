#include "trspk_world_build.h"
#include "../trspk_alloc.h"
#include "trspk_pose.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* A slot's storage, of the kind the build was made with. */
static void
trspk_world_slot_create_storage(
    const struct TRSPK_WorldBuild* build,
    struct TRSPK_WorldSlot* w)
{
    if( build->paged )
    {
        w->pages = trspk_batch16_create(build->format);
        assert(w->pages);
    }
    else
    {
        w->batch = trspk_batch32_create(build->format);
        assert(w->batch);
    }
}

static bool
trspk_world_slot_has_storage(const struct TRSPK_WorldSlot* w)
{
    return w->batch != NULL || w->pages != NULL;
}

static void
trspk_world_build_init_common(
    struct TRSPK_WorldBuild* build,
    bool paged,
    enum TRSPK_VertexFormat format,
    const char* prep_target_us,
    const char* prep_slice_us)
{
    assert(build);
    memset(build, 0, sizeof(*build));
    build->paged = paged;
    build->format = format;
    trspk_world_slot_create_storage(build, &build->world);
    build->world.batch_id = -1;
    build->stage.batch_id = -1;
    build->prep_target_us =
        prep_target_us && prep_target_us[0] ? strtoull(prep_target_us, NULL, 0) : 6000u;
    build->prep_slice_us =
        prep_slice_us && prep_slice_us[0] ? strtoull(prep_slice_us, NULL, 0) : 4000u;
    build->prep_floor_us = 500u;
}

void
trspk_world_build_init(
    struct TRSPK_WorldBuild* build,
    enum TRSPK_VertexFormat format,
    const char* prep_target_us,
    const char* prep_slice_us)
{
    trspk_world_build_init_common(build, false, format, prep_target_us, prep_slice_us);
}

void
trspk_world_build_init_paged(
    struct TRSPK_WorldBuild* build,
    enum TRSPK_VertexFormat format,
    const char* prep_target_us,
    const char* prep_slice_us)
{
    trspk_world_build_init_common(build, true, format, prep_target_us, prep_slice_us);
}

static void
trspk_world_slot_free(struct TRSPK_WorldSlot* w)
{
    if( w->batch )
        trspk_batch32_destroy(w->batch);
    w->batch = NULL;
    trspk_batch16_destroy(w->pages);
    w->pages = NULL;
    trspk_element_mask_free(&w->detached);
    TRSPK_FREE(w->records);
    w->records = NULL;
    w->record_count = 0u;
    w->record_cap = 0u;
    TRSPK_FREE(w->record_head);
    w->record_head = NULL;
    w->record_head_cap = 0u;
}

void
trspk_world_build_free(struct TRSPK_WorldBuild* build)
{
    if( !build )
        return;
    trspk_world_slot_free(&build->world);
    trspk_world_slot_free(&build->stage);
}

/* ---- records ------------------------------------------------------------- */

/*
 * World builds are keyed by element INDEX (ToriDraw_ElementIndexOfRaw), not by
 * the id as given: an adoption's adds (ToriDraw_SceneAdoptPool) carry the
 * scene's untagged index while the frame's draws carry the world's tagged id
 * (kind in the top bits), and an exact compare made every draw of an adopted
 * scene miss its build -- the background rebuild baked its whole batch and
 * then baked every drawn element a second time into the static arena. Two
 * live elements never share an index, so the index is the identity.
 */
int
trspk_world_record_find(
    const struct TRSPK_WorldSlot* w,
    int element_id,
    int anim_index,
    int pose_id)
{
    int const index = ToriDraw_ElementIndexOfRaw(element_id);
    assert(w);
    if( index < 0 || (uint32_t)index >= w->record_head_cap )
        return -1;
    for( int r = w->record_head[index]; r >= 0; r = w->records[r].next )
    {
        struct TRSPK_WorldRecord const* record = &w->records[r];
        if( record->element_id == index && record->anim_index == anim_index &&
            record->pose_id == pose_id )
            return r;
    }
    return -1;
}

/* Forget every record: the heads of the indices they used, then the list. */
static void
trspk_world_records_reset(struct TRSPK_WorldSlot* w)
{
    for( uint32_t r = 0u; r < w->record_count; r++ )
    {
        int const index = ToriDraw_ElementIndexOfRaw(w->records[r].element_id);
        assert(index >= 0);
        assert((uint32_t)index < w->record_head_cap);
        w->record_head[index] = -1;
    }
    w->record_count = 0u;
    w->prefill_cursor = 0u;
    w->unbaked = 0u;
    w->record_vertices = 0u;
}

void
trspk_world_record_add(
    struct TRSPK_WorldSlot* w,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_count,
    struct ToriDraw_ModelHandle model,
    const struct ToriDraw_Position* world_position)
{
    int const index = ToriDraw_ElementIndexOfRaw(element_id);
    int r;
    assert(w);
    assert(world_position);
    assert(index >= 0);
    assert(vertex_count > 0u);
    /* A page holds a pose whole; a model that cannot fit one is not a record
     * of a paged build (its lane bakes it into its arena on draw instead). */
    assert(!w->pages || vertex_count <= TRSPK_BATCH16_MAX_VERTICES);
    if( (uint32_t)index >= w->record_head_cap )
    {
        uint32_t cap = w->record_head_cap ? w->record_head_cap : 4096u;
        while( cap <= (uint32_t)index )
            cap *= 2u;
        int* grown = (int*)TRSPK_REALLOC(w->record_head, (size_t)cap * sizeof(*grown));
        assert(grown);
        for( uint32_t i = w->record_head_cap; i < cap; i++ )
            grown[i] = -1;
        w->record_head = grown;
        w->record_head_cap = cap;
    }
    r = trspk_world_record_find(w, index, anim_index, pose_id);
    if( r < 0 )
    {
        if( w->record_count == w->record_cap )
        {
            uint32_t const cap = w->record_cap ? w->record_cap * 2u : 4096u;
            struct TRSPK_WorldRecord* grown = (struct TRSPK_WorldRecord*)TRSPK_REALLOC(
                w->records, (size_t)cap * sizeof(*grown));
            assert(grown);
            w->records = grown;
            w->record_cap = cap;
        }
        r = (int)w->record_count++;
        w->records[r].element_id = index;
        w->records[r].anim_index = anim_index;
        w->records[r].pose_id = pose_id;
        w->records[r].next = w->record_head[index];
        w->records[r].baked = false;
        w->record_head[index] = r;
        w->unbaked++;
        w->record_vertices += vertex_count;
    }
    else if( w->records[r].baked )
    {
        /* Baked already, under a reservation sized for the model it had. A
         * different size cannot go back into it: the build stops describing
         * the element, and its draws bake it on their own. */
        uint32_t reserved;
        if( w->pages )
        {
            const struct TRSPK_Batch16Entry* entry =
                trspk_batch16_find_entry(w->pages, index, anim_index, pose_id);
            assert(entry);
            reserved = entry->vertex_count;
        }
        else
        {
            const struct TRSPK_Batch32Entry* entry =
                trspk_batch32_find_entry(w->batch, index, anim_index, pose_id);
            assert(entry);
            reserved = entry->vertex_count;
        }
        if( reserved != vertex_count )
        {
            trspk_element_mask_set(&w->detached, index);
            return;
        }
        w->records[r].baked = false;
        w->unbaked++;
        /* Behind the prefill's place: start it over to find this one. */
        w->prefill_cursor = 0u;
    }
    w->records[r].vertex_count = vertex_count;
    w->records[r].model = model;
    w->records[r].world_position = *world_position;
}

/* Record `r` is baked from now: the bookkeeping both kinds of take share. */
static struct TRSPK_WorldRecord*
trspk_world_record_mark_taken(
    struct TRSPK_WorldSlot* w,
    int r)
{
    struct TRSPK_WorldRecord* record;
    assert(r >= 0);
    assert((uint32_t)r < w->record_count);
    record = &w->records[r];
    assert(!record->baked);
    assert(!trspk_element_mask_test(&w->detached, record->element_id));
    record->baked = true;
    assert(w->unbaked > 0u);
    w->unbaked--;
    return record;
}

void
trspk_world_record_take(
    struct TRSPK_WorldSlot* w,
    int r,
    struct TRSPK_Batch32Reservation* out_reservation)
{
    struct TRSPK_WorldRecord* record;
    assert(w);
    assert(w->batch);
    assert(out_reservation);
    record = trspk_world_record_mark_taken(w, r);
    trspk_batch32_reserve_pose(
        w->batch,
        record->element_id,
        record->anim_index,
        record->pose_id,
        record->vertex_count,
        out_reservation);
}

void
trspk_world_record_take_paged(
    struct TRSPK_WorldSlot* w,
    int r,
    struct TRSPK_Batch16Reservation* out_reservation,
    uint32_t* out_entry_index)
{
    struct TRSPK_WorldRecord* record;
    const struct TRSPK_Batch16Entry* entry;
    bool reserved;
    assert(w);
    assert(w->pages);
    assert(out_reservation);
    assert(out_entry_index);
    record = trspk_world_record_mark_taken(w, r);
    /* Fits a page (trspk_world_record_add), and a key already reserved has
     * this size (a different one detaches there): it cannot be refused. */
    reserved = trspk_batch16_reserve_pose(
        w->pages,
        record->element_id,
        record->anim_index,
        record->pose_id,
        record->vertex_count,
        out_reservation);
    assert(reserved);
    (void)reserved; /* read by the assert alone */
    entry = trspk_batch16_find_entry(w->pages, record->element_id, record->anim_index, record->pose_id);
    assert(entry);
    *out_entry_index = (uint32_t)(entry - trspk_batch16_get_entry(w->pages, 0u));
}

bool
trspk_world_slot_describes(
    const struct TRSPK_WorldSlot* w,
    int element_id)
{
    assert(w);
    return w->drawable && element_id >= 0 && !trspk_element_mask_test(&w->detached, element_id);
}

int
trspk_world_record_unbaked(
    const struct TRSPK_WorldSlot* w,
    int element_id,
    int anim_index,
    int pose_id)
{
    int r;
    assert(w);
    if( w->unbaked == 0u )
        return -1;
    r = trspk_world_record_find(w, element_id, anim_index, pose_id);
    if( r < 0 || w->records[r].baked )
        return -1;
    return r;
}

bool
trspk_world_prefill_more(struct TRSPK_WorldSlot* w)
{
    assert(w);
    if( w->unbaked > 0u && w->prefill_cursor < w->record_count )
        return true;
    /* Whatever the cursor passed unbaked is detached: never drawn from this
     * build. An add still to come restarts the cursor. */
    if( w->prefill_cursor >= w->record_count )
        w->unbaked = 0u;
    return false;
}

int
trspk_world_prefill_next(struct TRSPK_WorldSlot* w)
{
    int r;
    assert(w);
    assert(w->prefill_cursor < w->record_count);
    r = (int)w->prefill_cursor++;
    if( w->records[r].baked || trspk_element_mask_test(&w->detached, w->records[r].element_id) )
        return -1;
    return r;
}

/* ---- the two slots --------------------------------------------------------- */

void
trspk_world_slot_drop(struct TRSPK_WorldSlot* w)
{
    assert(w);
    assert(trspk_world_slot_has_storage(w));
    trspk_element_mask_clear(&w->detached);
    if( w->pages )
        trspk_batch16_clear(w->pages);
    else
        trspk_batch32_clear(w->batch);
    trspk_world_records_reset(w);
    w->drawable = false;
    w->adds_open = false;
    w->batch_id = -1;
}

void
trspk_world_slot_begin(
    struct TRSPK_WorldSlot* w,
    int batch_id)
{
    assert(w);
    assert(trspk_world_slot_has_storage(w));
    assert(batch_id >= 0);
    assert(w->record_count == 0u);
    if( w->pages )
        trspk_batch16_begin(w->pages);
    else
        trspk_batch32_begin(w->batch);
    w->batch_id = batch_id;
    w->adds_open = true;
}

struct TRSPK_WorldSlot*
trspk_world_build_open_slot(
    struct TRSPK_WorldBuild* build,
    int batch_id)
{
    assert(build);
    if( build->world.adds_open && batch_id == build->world.batch_id )
        return &build->world;
    if( trspk_world_slot_has_storage(&build->stage) && build->stage.adds_open &&
        batch_id == build->stage.batch_id )
        return &build->stage;
    return NULL;
}

bool
trspk_world_build_end(
    struct TRSPK_WorldBuild* build,
    int batch_id)
{
    assert(build);
    if( !build->world.adds_open || batch_id != build->world.batch_id )
        return false;
    build->world.adds_open = false;
    build->world.drawable = true;
    return true;
}

void
trspk_world_build_detach(
    struct TRSPK_WorldBuild* build,
    int element_id)
{
    assert(build);
    if( element_id < 0 )
        return;
    trspk_element_mask_set(&build->world.detached, element_id);
    if( trspk_world_slot_has_storage(&build->stage) )
        trspk_element_mask_set(&build->stage.detached, element_id);
}

bool
trspk_world_build_stage_ensure(struct TRSPK_WorldBuild* build)
{
    assert(build);
    if( trspk_world_slot_has_storage(&build->stage) )
        return false;
    trspk_world_slot_create_storage(build, &build->stage);
    build->stage.batch_id = -1;
    return true;
}

void
trspk_world_build_stage_begin(
    struct TRSPK_WorldBuild* build,
    int batch_id)
{
    assert(build);
    assert(trspk_world_slot_has_storage(&build->stage));
    /* Not released yet: reused as it is. */
    build->stage_release_pending = false;
    trspk_world_slot_begin(&build->stage, batch_id);
}

bool
trspk_world_build_stage_open(const struct TRSPK_WorldBuild* build)
{
    assert(build);
    return trspk_world_slot_has_storage(&build->stage) && build->stage.batch_id >= 0;
}

bool
trspk_world_build_clear_hits_world(
    const struct TRSPK_WorldBuild* build,
    int batch_id,
    bool clear_all)
{
    assert(build);
    return clear_all || batch_id == build->world.batch_id;
}

bool
trspk_world_build_clear_hits_stage(
    const struct TRSPK_WorldBuild* build,
    int batch_id,
    bool scene_reset)
{
    assert(build);
    return trspk_world_build_stage_open(build) &&
           (scene_reset || batch_id == build->stage.batch_id);
}

void
trspk_world_build_swap(
    struct TRSPK_WorldBuild* build,
    int batch_id)
{
    struct TRSPK_WorldSlot slot;
    assert(build);
    /* The scene sends SWAP only for the stage it opened. */
    assert(trspk_world_slot_has_storage(&build->stage));
    assert(batch_id == build->stage.batch_id);
    (void)batch_id; /* read by the asserts alone */
    assert(!build->world.drawable);
    assert(build->world.record_count == 0u);
    slot = build->world;
    build->world = build->stage;
    build->stage = slot;
    build->world.adds_open = false;
    build->world.drawable = true;
    /* Adds that arrived after the prefill passed them start it over. */
    build->world.prefill_cursor = 0u;
    build->stage_release_pending = true;
}

uint64_t
trspk_world_build_stage_reserve_wanted(struct TRSPK_WorldBuild* build)
{
    assert(build);
    if( !trspk_world_build_stage_open(build) ||
        (build->stage.pages ? trspk_batch16_chunk_count(build->stage.pages) != 0u
                            : trspk_batch32_vbo(build->stage.batch)->vertex_count != 0u) ||
        build->stage.record_vertices == 0u )
        return 0u;
    return build->stage.record_vertices + build->stage.record_vertices / 8u;
}

void
trspk_world_build_stage_release(struct TRSPK_WorldBuild* build)
{
    assert(build);
    assert(build->stage_release_pending);
    assert(build->stage.record_count == 0u);
    if( build->stage.batch )
        trspk_batch32_destroy(build->stage.batch);
    build->stage.batch = NULL;
    trspk_batch16_destroy(build->stage.pages);
    build->stage.pages = NULL;
    trspk_world_slot_create_storage(build, &build->stage);
    build->stage_release_pending = false;
}

bool
trspk_world_build_has_work(const struct TRSPK_WorldBuild* build)
{
    assert(build);
    return build->world.unbaked > 0u ||
           (trspk_world_slot_has_storage(&build->stage) && build->stage.unbaked > 0u) ||
           build->stage_release_pending;
}

uint64_t
trspk_world_build_slice_us(
    const struct TRSPK_WorldBuild* build,
    uint64_t render_us)
{
    uint64_t spent_us;
    uint64_t slice_us;
    assert(build);
    spent_us = build->frame_elapsed_us + render_us;
    slice_us = spent_us < build->prep_target_us ? build->prep_target_us - spent_us : 0u;
    if( slice_us < build->prep_floor_us )
        slice_us = build->prep_floor_us;
    if( slice_us > build->prep_slice_us )
        slice_us = build->prep_slice_us;
    return slice_us;
}

/* ---- announced animation tracks ----------------------------------------- */

void
trspk_anim_tracks_free(struct TRSPK_AnimTracks* tracks)
{
    if( !tracks )
        return;
    TRSPK_FREE(tracks->tracks);
    tracks->tracks = NULL;
    tracks->cap = 0u;
}

/* The slot of (element_id, anim_index), or NULL when the table has no room
 * for it (grow = false). */
static struct TRSPK_AnimTrack*
trspk_anim_track_slot(
    struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index,
    bool grow)
{
    int const index = ToriDraw_ElementIndexOfRaw(element_id);
    assert(index >= 0);
    assert(anim_index >= 0);
    assert(anim_index < TRSPK_POSE_TRACK_COUNT);
    if( (uint32_t)index >= tracks->cap )
    {
        if( !grow )
            return NULL;
        uint32_t cap = tracks->cap ? tracks->cap : 1024u;
        while( cap <= (uint32_t)index )
            cap *= 2u;
        struct TRSPK_AnimTrack* grown = (struct TRSPK_AnimTrack*)TRSPK_REALLOC(
            tracks->tracks, (size_t)cap * TRSPK_POSE_TRACK_COUNT * sizeof(*grown));
        assert(grown);
        for( size_t i = (size_t)tracks->cap * TRSPK_POSE_TRACK_COUNT;
             i < (size_t)cap * TRSPK_POSE_TRACK_COUNT; i++ )
            grown[i].element_id = -1;
        tracks->tracks = grown;
        tracks->cap = cap;
    }
    return &tracks->tracks[(size_t)index * TRSPK_POSE_TRACK_COUNT + (size_t)anim_index];
}

void
trspk_anim_track_announce(
    struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index,
    void const* model,
    void const* animation,
    const struct ToriDraw_Position* world_position)
{
    struct TRSPK_AnimTrack* track;
    assert(tracks);
    assert(model);
    assert(animation);
    assert(world_position);
    track = trspk_anim_track_slot(tracks, element_id, anim_index, true);
    /* By index: see trspk_world_record_find. */
    track->element_id = ToriDraw_ElementIndexOfRaw(element_id);
    track->announced_id = element_id;
    track->model = model;
    track->animation = animation;
    track->world_position = *world_position;
}

const struct TRSPK_AnimTrack*
trspk_anim_track_match(
    const struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index,
    void const* model,
    void const* animation)
{
    int const index = ToriDraw_ElementIndexOfRaw(element_id);
    struct TRSPK_AnimTrack const* track;
    assert(tracks);
    assert(anim_index >= 0);
    assert(anim_index < TRSPK_POSE_TRACK_COUNT);
    if( index < 0 || (uint32_t)index >= tracks->cap )
        return NULL;
    track = &tracks->tracks[(size_t)index * TRSPK_POSE_TRACK_COUNT + (size_t)anim_index];
    if( track->element_id != index || track->animation != animation || track->model != model )
        return NULL;
    return track;
}

void
trspk_anim_track_forget(
    struct TRSPK_AnimTracks* tracks,
    int element_id,
    int anim_index)
{
    struct TRSPK_AnimTrack* track;
    assert(tracks);
    if( element_id < 0 )
        return;
    track = trspk_anim_track_slot(tracks, element_id, anim_index, false);
    if( track )
        track->element_id = -1;
}

void
trspk_anim_track_forget_element(
    struct TRSPK_AnimTracks* tracks,
    int element_id)
{
    for( int anim_index = 0; anim_index < TRSPK_POSE_TRACK_COUNT; anim_index++ )
        trspk_anim_track_forget(tracks, element_id, anim_index);
}

void
trspk_anim_track_forget_all(struct TRSPK_AnimTracks* tracks)
{
    assert(tracks);
    for( size_t i = 0; i < (size_t)tracks->cap * TRSPK_POSE_TRACK_COUNT; i++ )
        tracks->tracks[i].element_id = -1;
}
