/**
 * The painter's-algorithm renderer for WebGL1 -- a WHOLE renderer, not a
 * hook layer.
 *
 * The legacy RS ordering: no depth buffer at all. Every model's faces are
 * sorted back-to-front on the CPU by ToriDraw_RenderModel2SortFaces and drawn
 * in that order, and correctness lives entirely in the submission order.
 *
 * This file owns its own frame: the GL context it asks for (no depth bits),
 * begin_3d, draw_model, end_3d, the draw sequence's issue loop, the command
 * dispatch and the frame lifecycle. It COMPOSES those out of platform_web_renderer_webgl1_core.c,
 * which is a toolkit: the scene walk, the bake, the arenas, the streams, the
 * uploads, the shaders and the state cache, with no test anywhere in it for
 * which world path is running. platform_web_renderer_webgl1_zbuffer.c is the depth-tested peer, written the
 * same way; neither calls the other.
 *
 * That separation is not tidiness. A shared core that branched on ::zbuffer
 * is what made the ES3 family 27% slower than this one -- five of the
 * optimisations below were gated behind a lever that defaulted off there --
 * and it is what hid the ES3 flicker. One file serving two renderers is how
 * both stayed invisible.
 *
 * WHAT THIS RENDERER CARRIES THAT THE ES3 ONE DOES NOT
 *
 * An OpenGL ES 2.0 element index is 16 bits, so it reaches 65,536 vertices
 * from wherever the attributes are bound and rebinding ends the draw. The
 * retained world is far bigger than that -- a loaded region is ~960k static
 * vertices -- and painter order hops between chunks tile by tile. So this
 * path cannot index the retained store in place, and carries two machines
 * instead:
 *
 *   - a RESIDENT WINDOW, a GPU ring each drawn static model is copied into
 *     so that it lands somewhere a U16 index can reach, with per-entry
 *     placement serials, an overwrite guard, and a fragmentation compaction
 *     with hysteresis;
 *   - a GATHER, which copies each sorted face's 84 bytes out of the retained
 *     bake into one ordered per-frame stream for everything the ring refuses.
 *
 * An actor goes through the stream as well: its pose is this frame's, it has
 * no retained home, and it is baked straight into the stream in sorted face
 * order, which is an array draw with no indices at all.
 */

#include "es2/es2_core.h"

#include "core/trspk_math.h"
#include "engine/boot_bar.h"
#include "log/torirs_log.h"
#include "painters/painters.h"
#include "perf/torirs_perf.h"
#include "platform/platform_web_renderer_webgl1_placement.h"
#include "es2/es2_indices.h"
#include "toridraw.h"
#include "toridraw_element_id.h"
#include "toridraw_math.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* es2p_draw_model rebakes a missing animation through the load path. */
static void
es2p_animation_load(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command);

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

/* One face's three vertices: 84 bytes, moved as five 16-byte NEON transfers
 * and one word. Written out by hand because the struct assignment it
 * replaced did NOT inline: clang lowered `destination[written] = source[face]`
 * to `bl __aeabi_memcpy8`, and with ten thousand faces a frame that call --
 * its length dispatch, its alignment checks, its tail loop -- was 16% of the
 * phone's frame in `__memcpy_base` (simpleperf, 2026-09-01). The compiler
 * only inlines the copy below the threshold it happens to have; five
 * intrinsics do not depend on that. Exact length, no over-read: the source
 * face may be the last one in its buffer. */
struct ES2FaceVertices
{
    struct TRSPK_VertexGLES2 corner[3];
};
_Static_assert(sizeof(struct ES2FaceVertices) == 84u, "three packed vertices");

static inline void
es2_face_copy(struct ES2FaceVertices* destination, const struct ES2FaceVertices* source)
{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const uint8_t* source_bytes = (const uint8_t*)source;
    uint8_t* destination_bytes = (uint8_t*)destination;
    vst1q_u8(destination_bytes, vld1q_u8(source_bytes));
    vst1q_u8(destination_bytes + 16, vld1q_u8(source_bytes + 16));
    vst1q_u8(destination_bytes + 32, vld1q_u8(source_bytes + 32));
    vst1q_u8(destination_bytes + 48, vld1q_u8(source_bytes + 48));
    vst1q_u8(destination_bytes + 64, vld1q_u8(source_bytes + 64));
    {
        uint32_t tail;
        memcpy(&tail, source_bytes + 80, sizeof(tail));
        memcpy(destination_bytes + 80, &tail, sizeof(tail));
    }
#else
    *destination = *source;
#endif
}


static void
es2p_apply_world_states(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    /* Painter order: the submission order IS the depth order, so the depth
     * test must never reject. And no culling: the painter sorts faces and has
     * its own reasons to see every one of them. */
    es2_set_depth(renderer, false, false);
    es2_set_cull(renderer, false);
    es2_set_blend(renderer, true);
}

static int
es2p_sort_faces(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int* out_sorted_face_count)
{
    int face_count;

    assert(renderer);
    assert(command);
    assert(out_sorted_face_count);
    face_count = ToriDraw_RenderModel2SortFacesWithTable(
        command->model, renderer->scene, renderer->kernel);
    /* Every face this mode draws is a sorted face: the two counts are the same
     * number, and the core wants both. */
    *out_sorted_face_count = face_count;
    return face_count;
}

/* ---- the gather (stream fallback) ------------------------------------------------- */

/* Copy `count` sorted faces of one retained model into the stream at
 * `destination`. A face the source cannot supply (the order names faces by
 * model index, which can run past the bake) becomes a degenerate triangle,
 * so the reservation stays contiguous. */
static void
es2_painter_gather(
    struct ES2FaceVertices* destination,
    const struct ES2FaceVertices* source,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    /* The gather is a random walk over the bake -- sorted order is depth
     * order, not memory order -- so every face is a cold cache line pair.
     * The order array names the faces ahead of time, so ask for them ahead
     * of time: a prefetch a few faces out overlaps that miss with this
     * face's copy. */
    enum { ES2_GATHER_PREFETCH_AHEAD = 4 };
    uint32_t index;

    assert(destination);
    assert(source);
    assert(faces);
    for( index = 0u; index < count; index++ )
    {
        uint32_t face = (uint32_t)faces[index];
        if( index + ES2_GATHER_PREFETCH_AHEAD < count )
        {
            uint32_t ahead = (uint32_t)faces[index + ES2_GATHER_PREFETCH_AHEAD];
            if( ahead < source_face_limit )
            {
                __builtin_prefetch(&source[ahead], 0, 0);
                __builtin_prefetch((const uint8_t*)&source[ahead] + 64, 0, 0);
            }
        }
        if( face < source_face_limit )
            es2_face_copy(&destination[index], &source[face]);
        else
            memset(&destination[index], 0, sizeof(destination[index]));
    }
}

/* ---- the resident window ---------------------------------------------------------- */

static bool
es2_painter_hot_ensure(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    if( renderer->hot_vbo )
        return true;
    if( !renderer->gl_context )
        return false;
    glGenBuffers(1, &renderer->hot_vbo);
    es2_bind_array_buffer(renderer, renderer->hot_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)((size_t)ES2_HOT_RING_VERTICES * sizeof(struct TRSPK_VertexGLES2)),
        NULL,
        GL_DYNAMIC_DRAW);
    if( !es2_check_error("resident window buffer") )
    {
        glDeleteBuffers(1, &renderer->hot_vbo);
        renderer->hot_vbo = 0u;
        return false;
    }
    /* Serial 0 is "never placed"; start the head a whole ring in so no
     * placement can ever be handed serial 0. The head is 64-bit: at ~65k
     * vertices a lap it would take ~2^16 laps to reach 2^32, which a long
     * session walking the world does, and at the wrap `head - serial` of
     * every live serial went huge (evicting the ring) while a serial that
     * happened to sit at 0 read as freshly placed. 2^64 does not happen. */
    renderer->hot_head = ES2_HOT_RING_VERTICES;
    /* Let the first fragmentation verdict act at once. */
    renderer->hot_frames_since_compaction = ES2_HOT_COMPACT_MIN_INTERVAL_FRAMES;
    return true;
}

static void
es2_painter_batch_reset(
    struct TRSPK_Renderer_ES2* renderer,
    struct ES2StaticBatch* batch,
    uint32_t entry_count)
{
    assert(renderer);
    assert(batch);
    (void)renderer;
    if( entry_count > batch->hot_serial_capacity )
    {
        uint32_t capacity = batch->hot_serial_capacity ? batch->hot_serial_capacity : 256u;
        uint64_t* grown;
        while( capacity < entry_count )
            capacity *= 2u;
        grown = (uint64_t*)realloc(batch->hot_serial, (size_t)capacity * sizeof(*grown));
        assert(grown);
        batch->hot_serial = grown;
        batch->hot_serial_capacity = capacity;
    }
    if( batch->hot_serial_capacity )
        memset(batch->hot_serial, 0, (size_t)batch->hot_serial_capacity * sizeof(*batch->hot_serial));
}

/* Send the staged residents to the ring. The staging run is one contiguous
 * span of the ring, so it is one glBufferSubData. Called at the end of the
 * pass and whenever a placement crosses a lap boundary mid-pass. */
static void
es2_painter_send_staged(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    if( renderer->hot_stage_count == 0u )
        return;
    assert(renderer->hot_vbo);
    assert(renderer->hot_stage_address + renderer->hot_stage_count <= ES2_HOT_RING_VERTICES);
    es2_bind_array_buffer(renderer, renderer->hot_vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        (GLintptr)((size_t)renderer->hot_stage_address * sizeof(struct TRSPK_VertexGLES2)),
        (GLsizeiptr)((size_t)renderer->hot_stage_count * sizeof(struct TRSPK_VertexGLES2)),
        renderer->hot_stage);
    renderer->painter_stat_placed_vertices += renderer->hot_stage_count;
    renderer->hot_stage_count = 0u;
}

static void
es2_painter_flush(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    /* Fragmentation check, on the frame that is ending: a long walk places
     * new residents at the head, far from the neighbours they are drawn
     * between, and every such seam is a window switch and a draw. Past the
     * threshold, empty the ring -- one frame of re-placing the live set in
     * painter order puts everything back into a few windows. Advancing the
     * head a whole ring is the eviction: every serial fails the residency
     * test.
     *
     * With hysteresis. A live set that GENUINELY spans more than
     * ES2_HOT_COMPACT_DRAWS windows -- one long sight line across models
     * baked far apart -- is not fragmentation, and re-placing it does not
     * make it smaller; without the interval the verdict fired every frame
     * and the ring was rewritten in full every frame, ~2.5 MB of upload a
     * frame that a camera-still profile never showed (a still frame has few
     * draws). So after a compaction the ring gets at least
     * ES2_HOT_COMPACT_MIN_INTERVAL_FRAMES frames to prove itself; the
     * frames the verdict was held back are counted on the debug line.
     *
     * Only the end-of-pass call judges this; the lap-change send inside a
     * placement must not, since draw_item_count is partial there. */
    if( renderer->hot_vbo )
    {
        if( renderer->hot_frames_since_compaction < UINT32_MAX )
            renderer->hot_frames_since_compaction++;
        if( renderer->draw_item_count > ES2_HOT_COMPACT_DRAWS )
        {
            if( renderer->hot_frames_since_compaction >= ES2_HOT_COMPACT_MIN_INTERVAL_FRAMES )
            {
                renderer->hot_head += ES2_HOT_RING_VERTICES;
                renderer->hot_frames_since_compaction = 0u;
                renderer->painter_stat_compactions++;
            }
            else
                renderer->painter_stat_compactions_deferred++;
        }
    }
    es2_painter_send_staged(renderer);
}

/*
 * The residency test alone: is this entry's bake in the ring right now? Its
 * first vertex's ring address, or UINT32_MAX. Nothing but the serial array
 * is read, which is the point -- the steady-state frame is ~1,300 of these
 * and no placements, and the CPU source (page -> batch -> chunk -> vbo) is
 * only needed for the copy a placement makes.
 *
 * A model placed at serial s is intact while the head has not come a full
 * lap past it. The subtraction is 64-bit and cannot wrap in practice; serial
 * 0 is "never placed" (the head starts one ring in).
 */
static uint32_t
es2_painter_find_resident(
    struct TRSPK_Renderer_ES2* renderer,
    struct ES2StaticBatch* batch,
    uint32_t entry_index)
{
    uint64_t serial;
    assert(renderer);
    assert(batch);
    if( !renderer->hot_vbo || entry_index >= batch->hot_serial_capacity )
        return UINT32_MAX;
    serial = batch->hot_serial[entry_index];
    if( serial == 0u || renderer->hot_head - serial > ES2_HOT_RING_VERTICES )
        return UINT32_MAX;
    if( serial < renderer->hot_frame_oldest_serial )
        renderer->hot_frame_oldest_serial = serial;
    return (uint32_t)(serial % ES2_HOT_RING_VERTICES);
}

/*
 * Make one static model resident, or find it already so. Returns its first
 * vertex's address in the ring, or UINT32_MAX when it cannot be placed.
 *
 * The ring is written in serial order: a placement takes the head, and a
 * model whose span would cross the ring's end is placed at the start of the
 * next lap instead, the tail going unused. A model placed at serial s is
 * still intact while the head has not come a full lap past it.
 */
static uint32_t
es2_painter_place(
    struct TRSPK_Renderer_ES2* renderer,
    struct ES2StaticBatch* batch,
    uint32_t entry_index,
    const struct TRSPK_VertexGLES2* vertices,
    uint32_t span)
{
    uint32_t address;

    assert(renderer);
    assert(batch);
    assert(vertices);
    /* A model must fit one draw window, not merely the ring. */
    if( span == 0u || span > ES2_HOT_WINDOW_VERTICES )
        return UINT32_MAX;
    if( entry_index >= batch->hot_serial_capacity )
        return UINT32_MAX;
    if( !es2_painter_hot_ensure(renderer) )
        return UINT32_MAX;

    address = es2_painter_find_resident(renderer, batch, entry_index);
    if( address != UINT32_MAX )
        return address;

    /* Not resident: place at the head, on a fresh lap if it would not fit
     * the rest of this one. The staging run must stay contiguous in the
     * ring, so a lap change sends what is staged first. */
    address = (uint32_t)(renderer->hot_head % ES2_HOT_RING_VERTICES);
    {
        uint64_t head_after = renderer->hot_head + span;
        if( address + span > ES2_HOT_RING_VERTICES )
            head_after = renderer->hot_head + (ES2_HOT_RING_VERTICES - address) + span;
        /* The overwrite guard: this frame's draw reads every resident it
         * has been handed, from the oldest one on, and those bytes must
         * survive until it runs. A frame whose live set outgrows the ring
         * gathers the overflow instead of eating its own tail. */
        if( renderer->hot_frame_oldest_serial != UINT64_MAX &&
            head_after - renderer->hot_frame_oldest_serial > ES2_HOT_RING_VERTICES )
            return UINT32_MAX;
    }
    if( address + span > ES2_HOT_RING_VERTICES )
    {
        es2_painter_send_staged(renderer);
        renderer->hot_head += ES2_HOT_RING_VERTICES - address;
        address = 0u;
    }
    if( renderer->hot_stage_count == 0u )
        renderer->hot_stage_address = address;
    if( renderer->hot_stage_count + span > renderer->hot_stage_capacity )
    {
        uint32_t capacity = renderer->hot_stage_capacity ? renderer->hot_stage_capacity
                                                         : ES2_HOT_STAGE_INIT_VERTICES;
        struct TRSPK_VertexGLES2* grown;
        while( capacity < renderer->hot_stage_count + span )
            capacity *= 2u;
        grown = (struct TRSPK_VertexGLES2*)realloc(
            renderer->hot_stage, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->hot_stage = grown;
        renderer->hot_stage_capacity = capacity;
    }
    memcpy(
        renderer->hot_stage + renderer->hot_stage_count,
        vertices,
        (size_t)span * sizeof(*vertices));
    renderer->hot_stage_count += span;
    batch->hot_serial[entry_index] = renderer->hot_head;
    if( renderer->hot_head < renderer->hot_frame_oldest_serial )
        renderer->hot_frame_oldest_serial = renderer->hot_head;
    renderer->hot_head += span;
    renderer->painter_stat_placed_models++;
    return address;
}

/* Push one resident model's sorted faces as U16 indices into the ring,
 * relative to a draw window. The window is the open item's when the model
 * lies inside it -- so consecutive residents keep merging into one draw --
 * and otherwise opens at the model's own address. A face the source cannot
 * supply indexes the model's first vertex three times: a degenerate
 * triangle. */
static void
es2_painter_push_resident(
    struct TRSPK_Renderer_ES2* renderer,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    uint16_t* indices;
    uint32_t window = address;
    uint32_t span = source_face_limit * 3u;

    assert(renderer);
    assert(faces);
    if( count == 0u )
        return;
    if( renderer->draw_item_count > 0u )
    {
        const struct ES2DrawItem* open = &renderer->draw_items[renderer->draw_item_count - 1u];
        if( open->indexed && open->binding == ES2_HOT_BINDING && address >= open->page_base &&
            address + span <= open->page_base + ES2_HOT_WINDOW_VERTICES )
            window = open->page_base;
    }
    renderer->painter_stat_faces_indexed += count;
    /* Written straight into the draw sequence's staging: no scratch, no copy. */
    indices = es2_sequence_reserve_indexed(renderer, count * 3u);
    assert(indices);
    assert(address - window + span <= ES2_HOT_WINDOW_VERTICES);
    address -= window;
    es2_painter_write_indices(indices, address, source_face_limit, faces, count,
                                 /*use_neon=*/false);
    es2_sequence_commit_indexed(renderer, ES2_HOT_BINDING, window, true, false, count * 3u);
}

/* ---- emission ---------------------------------------------------------------------- */

static void
es2p_emit_model(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ES2ModelPlacement* placement)
{
    const struct TRSPK_VBO* source_vbo;
    const struct TRSPK_Triangles* source_triangles;
    const int* face_order;
    uint32_t source_base;
    uint32_t source_face_limit;
    uint32_t face_count;
    uint32_t first;

    assert(renderer);
    assert(placement);
    if( placement->sorted_face_count <= 0 )
        return;
    face_count = (uint32_t)placement->sorted_face_count;

    /* An actor was baked into the stream in sorted order by the core; its
     * placement already names the stream. */
    if( placement->binding == ES2_FRAME_STREAM_BINDING )
    {
        renderer->painter_stat_faces_actor += face_count;
        es2_sequence_push_array(
            renderer, ES2_FRAME_STREAM_BINDING, placement->absolute_base, face_count * 3u, true,
            false);
        return;
    }

    /* Whose ever bench it came from -- the scene's, or a stage source's. */
    face_order = placement->face_order;
    assert(face_order);

    /*
     * TORIRS_GLES2_RESIDENT_FAST: a batch entry is asked "are you resident?"
     * before anything else -- one load from the batch's serial array -- and
     * in the steady state that is the whole per-model cost. The entry's own
     * vertex count is the span that was placed (asserted where it is
     * placed, below), so nothing about the CPU bake is needed for the hit.
     * Only a miss resolves the source, and through the page's cached VBO
     * rather than the page -> batch -> chunk walk of es2_binding_cpu_source.
     */
    if( placement->binding == ES2_STATIC_PAGE_BINDING &&
        placement->batch_slot < renderer->static_batch_count &&
        placement->entry_index != UINT32_MAX && placement->entry_vertex_count > 0u )
    {
        struct ES2StaticBatch* batch = &renderer->static_batches[placement->batch_slot];
        const struct ES2StaticPageRef* page;
        uint32_t entry_faces = placement->entry_vertex_count / 3u;
        uint32_t address = es2_painter_find_resident(renderer, batch, placement->entry_index);
        if( address != UINT32_MAX )
        {
            renderer->painter_stat_resident_hits++;
            es2_painter_push_resident(renderer, address, entry_faces, face_order, face_count);
            return;
        }
        /* A miss. es2_draw_model validated the page before it built this
         * placement, and a page is only valid with its VBO cached. */
        assert(placement->page_id < renderer->static_page_count);
        page = &renderer->static_pages[placement->page_id];
        assert(page->valid);
        assert(page->cpu_vbo);
        source_vbo = page->cpu_vbo;
        if( source_vbo->format != TRSPK_VERTEX_FORMAT_GLES2 )
            return;
        source_base = placement->local_base;
        /* The reservation that made this entry grew the chunk's VBO to
         * cover it (trspk_batch16_reserve_pose), so the entry never runs
         * past its bake. */
        assert(source_base + placement->entry_vertex_count <= source_vbo->vertex_count);
        address = es2_painter_place(
            renderer,
            batch,
            placement->entry_index,
            source_vbo->vertices.as_gles2 + source_base,
            placement->entry_vertex_count);
        if( address != UINT32_MAX )
        {
            es2_painter_push_resident(renderer, address, entry_faces, face_order, face_count);
            return;
        }
        /* The ring refused it (too big for a window, or this frame's live
         * set fills the ring): gathered like any other retained model. */
        source_face_limit = entry_faces;
        goto gather;
    }

    /* A retained model, the control arm: resolve the CPU source first. */
    if( !es2_binding_cpu_source(
            renderer, placement->binding, placement->page_id, &source_vbo, &source_triangles) ||
        source_vbo->format != TRSPK_VERTEX_FORMAT_GLES2 )
        return;
    (void)source_triangles;
    /* A Batch16 chunk's CPU copy is the chunk alone and starts at zero; an
     * arena's is the whole buffer. */
    source_base = placement->binding == ES2_STATIC_PAGE_BINDING
        ? placement->local_base
        : placement->page_base + placement->local_base;
    if( source_base > source_vbo->vertex_count )
        return;
    /* The order names faces by their index in the MODEL, which runs past the
     * sorted count -- placement->face_count is that sorted count here, so it
     * is NOT a bound on face indices. The only bound is the bake itself. */
    source_face_limit = (source_vbo->vertex_count - source_base) / 3u;

    /* A batch entry lives in the resident window: the entry's whole bake is
     * what gets placed, since any of its faces may be named this frame or
     * the next. */
    if( placement->binding == ES2_STATIC_PAGE_BINDING &&
        placement->batch_slot < renderer->static_batch_count &&
        placement->entry_index != UINT32_MAX && placement->entry_vertex_count > 0u )
    {
        uint32_t entry_faces = placement->entry_vertex_count / 3u;
        uint32_t address;
        if( entry_faces < source_face_limit )
            source_face_limit = entry_faces;
        address = es2_painter_place(
            renderer,
            &renderer->static_batches[placement->batch_slot],
            placement->entry_index,
            source_vbo->vertices.as_gles2 + source_base,
            source_face_limit * 3u);
        if( address != UINT32_MAX )
        {
            es2_painter_push_resident(renderer, address, source_face_limit, face_order, face_count);
            return;
        }
    }

gather:
    /* Everything else is gathered into the stream in sorted order. */
    renderer->painter_stat_faces_gathered += face_count;
    first = es2_frame_stream_reserve(renderer, face_count * 3u);
    es2_painter_gather(
        (struct ES2FaceVertices*)&renderer->frame_stream_cpu->vertices.as_gles2[first],
        (const struct ES2FaceVertices*)(source_vbo->vertices.as_gles2 + source_base),
        source_face_limit,
        face_order,
        face_count);
    es2_sequence_push_array(
        renderer, ES2_FRAME_STREAM_BINDING, first, face_count * 3u, true, false);
}

void es2p_sequence_issue(struct TRSPK_Renderer_ES2* renderer,uint32_t index_base_bytes)
{
    uint32_t item_index,draw_calls=0;int program_cutout=-1;
    es2p_apply_world_states(renderer);

    for( item_index = 0u; item_index < renderer->draw_item_count; item_index++ )
    {
        const struct ES2DrawItem* item = &renderer->draw_items[item_index];
        if( program_cutout != (int)item->cutout )
        {
            es2_use_world_program(renderer, item->cutout != 0u);
            program_cutout = (int)item->cutout;
        }
        if( !es2_bind_stream(renderer, item->binding, item->indexed ? item->page_base : 0u) )
            continue;
        if( item->indexed )
            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)item->count,
                GL_UNSIGNED_SHORT,
                (const void*)(uintptr_t)(index_base_bytes + (size_t)item->first * sizeof(uint16_t)));
        else
            glDrawArrays(GL_TRIANGLES, (GLint)item->first, (GLsizei)item->count);
        draw_calls++;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, renderer->draw_item_count);
}

void
es2p_sequence_draw(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t index_base_bytes = 0u;

    assert(renderer);
    if( renderer->draw_item_count == 0u )
        return;
    if( renderer->ibo_staging_count > 0u )
    {
        uint32_t bytes = renderer->ibo_staging_count * (uint32_t)sizeof(uint16_t);
        index_base_bytes = es2_stream_set_append(
            &renderer->index_stream,
            renderer->frame_slot,
            GL_ELEMENT_ARRAY_BUFFER,
            ES2_INDEX_STREAM_INIT_BYTES,
            renderer->ibo_staging,
            bytes,
            false);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES, (int64_t)bytes);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOADS, 1);
    }

#if defined(TORIRS_SHADER_PROBE)
    es2_shader_probe(renderer,index_base_bytes);
#endif
    es2p_sequence_issue(renderer,index_base_bytes);
}

void
es2p_begin_3d(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Begin3D* command)
{
    const struct ToriDraw_ViewPort* viewport;
    int pass_w;
    int pass_h;
    int logical_x;
    int logical_y;
    int left;
    int top;
    int right;
    int bottom;
    uint32_t group;

    assert(renderer);
    assert(command);
    if( !renderer->gl_context )
        return;
    es2_sequence_reset(renderer);
    renderer->current_3d = *command;
#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
    g_chain_pass++;
#endif
#if defined(TORIRS_ANIM_CHAIN_CAPTURE)
    ToriDraw_AnimCaptureBeginPass();
#endif
    renderer->has_3d = true;
    renderer->in3d = true;
    /* Publish the prepared camera block: the prepared projection kernels are
     * gated on this pointer being the one the projection is called with. */
    if( renderer->scene )
        ToriDraw_ScenePrepareProjectionCamera(renderer->scene, &renderer->current_3d.camera);
    /* Every scene event of the frame has been dispatched by now (the frame
     * drains them before its first non-event command): a stage source may
     * start reading and posing models. */
    if( renderer->model_stage_source && renderer->model_stage_source->begin_3d )
        renderer->model_stage_source->begin_3d(renderer->model_stage_source->user, command);

    viewport = &renderer->current_3d.view_port;
    pass_w = viewport->width > 0 ? viewport->width : renderer->width;
    pass_h = viewport->height > 0 ? viewport->height : renderer->height;
    logical_x = viewport->x_center - pass_w / 2;
    logical_y = viewport->y_center - pass_h / 2;
    left = renderer->letterbox_x + (int)((int64_t)logical_x * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_top + (int)((int64_t)logical_y * renderer->letterbox_height / renderer->height);
    right = renderer->letterbox_x +
        (int)((int64_t)(logical_x + pass_w) * renderer->letterbox_width / renderer->width);
    bottom = renderer->letterbox_top +
        (int)((int64_t)(logical_y + pass_h) * renderer->letterbox_height / renderer->height);
    if( right <= left )
        right = left + 1;
    if( bottom <= top )
        bottom = top + 1;
    renderer->world_viewport.x = left;
    renderer->world_viewport.y = renderer->target_height - bottom;
    renderer->world_viewport.width = right - left;
    renderer->world_viewport.height = bottom - top;
    glViewport(
        renderer->world_viewport.x,
        renderer->world_viewport.y,
        renderer->world_viewport.width,
        renderer->world_viewport.height);

    trspk_compute_pass_matrices(
        renderer->view,
        renderer->projection,
        (float)command->camera_position.x,
        (float)command->camera_position.y,
        (float)command->camera_position.z,
        ToriDraw_AngleToRadians(command->camera.pitch),
        ToriDraw_AngleToRadians(command->camera.yaw),
        pass_w,
        pass_h,
        (int)command->camera.projection_mode,
        command->camera.projection_scale,
        command->camera.fov_rpi2048,
        command->camera.parallel_zoom16);
    /* No depth remap: trspk_compute_pass_matrices leaves clip z at a
     * constant, which is exactly right for a pass that never reads it. */
    es2_mat4_multiply(renderer->projection, renderer->view, renderer->model_view_projection);

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( renderer->groups[group].reset_each_frame )
            es2_reset_group(&renderer->groups[group]);
}

void
es2p_draw_model(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Model* command)
{
    struct ToriDraw_Position projected_position;
    struct ES2ModelPlacement placement;
    struct ES2ModelStage stage;
    struct ES2StaticPrimary static_placement;
    int static_state;
    bool staged = false;
    const int* face_order;
    bool projected_in_scene;
    int projected_depth;
    int face_count;
    int sorted_face_count = 0;
    bool dynamic;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    uint32_t page_base = 0u;
    uint32_t local_base;
    uint32_t binding;
    uint32_t group;

    assert(renderer);
    assert(command);
    /* The stage source, when one is installed, is asked about EVERY model
     * command before any early return: it hands results out in dispatch
     * order and pairs them with the asks by count. */
    if( renderer->model_stage_source )
        staged = renderer->model_stage_source->take(
            renderer->model_stage_source->user, command, &stage);
    if( !renderer->has_3d || !renderer->scene || command->model.kind == TORIDRAWMK_NONE )
        return;
#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
    es2_chain_capture(renderer, command);
#endif
    placement.page_id = UINT32_MAX;
    placement.batch_slot = UINT32_MAX;
    placement.entry_index = UINT32_MAX;
    placement.entry_vertex_count = 0u;
    if( staged )
    {
        /* Pose, cull, projection, pick test and sort were done by the source
         * (the dual-core lane's worker, on its own scratch view of the
         * scene); this thread consumes. The pose the source applied is the
         * one the bakes below read -- its results were published after it. */
        if( stage.cull != TORIDRAW_CULL_VISIBLE )
            return;
        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            stage.pick_hit )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;
        /* The painter draws sorted faces and nothing else; an unsorted
         * stage here is the producer's bug, not a case. */
        assert(stage.sorted);
        face_count = stage.sorted_face_count;
        sorted_face_count = face_count;
        face_order = stage.sorted ? stage.face_order : NULL;
        projected_depth = stage.projected_depth;
        projected_in_scene = false;
    }
    else
    {
        if( !renderer->poses_prepared && command->animation && command->element_id >= 0 )
        {
            es2_apply_animation(renderer, command);
        }
        projected_position = command->position;
        if( ToriDraw_RenderModel1ProjectWithTable(
                command->model,
                renderer->scene,
                &projected_position,
                &renderer->current_3d.view_port,
                &renderer->current_3d.camera,
                renderer->kernel) != TORIDRAW_CULL_VISIBLE )
            return;

        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            (command->pick_aabb
                 ? ToriDraw_ProjectedModelContainsAabb(
                       renderer->scene, renderer->pick_mouse_x, renderer->pick_mouse_y)
                 : command->pick_terrain
                     ? ToriDraw_ProjectedTileMouseHitTest(
                           renderer->scene,
                           command->model,
                           &renderer->current_3d.view_port,
                           renderer->pick_mouse_x,
                           renderer->pick_mouse_y)
                     : ToriDraw_ProjectedModelMouseHitTest(
                           renderer->scene,
                           command->model,
                           &renderer->current_3d.view_port,
                           renderer->pick_mouse_x,
                           renderer->pick_mouse_y)) )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;

        /* The painter must sort before it can count: the order it sorts
         * into IS the submission order. */
        face_count = es2p_sort_faces(renderer, command, &sorted_face_count);
        face_order = ToriDraw_FaceOrder(renderer->scene);
        projected_depth = renderer->scene->projected_vertex.z;
        projected_in_scene = true;
    }
    /* The sort census is a debug readout (TORIRS_GLES2_DEBUG); it costs a
     * second trspk_toridraw_face_count per model, so it is gated where it
     * is gathered, not only where it is printed. */
    if( renderer->debug )
    {
        int model_faces = trspk_toridraw_face_count(command->model);
        int bucket = model_faces <= 2 ? 0 : model_faces <= 16 ? 1 : model_faces <= 64 ? 2
                                                              : model_faces <= 256 ? 3 : 4;
        renderer->painter_stat_sort_models[bucket]++;
        renderer->painter_stat_sort_faces_in += (uint32_t)(model_faces > 0 ? model_faces : 0);
        renderer->painter_stat_sort_faces_out += (uint32_t)(face_count > 0 ? face_count : 0);
    }
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    dynamic = command->dynamic || command->element_id < 0;
    anim_index = es2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    pose_id = command->animation && command->anim_frame >= 0 ? command->anim_frame : 0;
    if( command->animation && command->animation->frame_count > 0 &&
        pose_id >= command->animation->frame_count )
        pose_id = 0;
    group = dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    binding = group;
    if( dynamic )
    {
        /* Painter path: an actor is baked straight into the frame stream in
         * its sorted face order, so it needs neither the dynamic arena nor an
         * index. The placement names the stream and the sorted count. */
        uint32_t first = es2_frame_stream_reserve(renderer, (uint32_t)sorted_face_count * 3u);
        if( !es2_bake_pose_vertices(
                renderer,
                renderer->frame_stream_cpu,
                &renderer->frame_stream_triangles,
                first,
                command->model,
                &command->world_position,
                face_order,
                sorted_face_count,
                /*ordered_painter=*/face_order != NULL) )
            return;
        placement.face_order = face_order;
        placement.projected_in_scene = projected_in_scene;
        placement.projected_depth = projected_depth;
        placement.binding = ES2_FRAME_STREAM_BINDING;
        placement.page_base = 0u;
        placement.local_base = first;
        placement.absolute_base = first;
        placement.face_count = face_count;
        placement.sorted_face_count = sorted_face_count;
        placement.anim_index = anim_index;
        placement.pose_id = pose_id;
        placement.dynamic = true;
        es2p_emit_model(renderer, &placement);
        return;
    }
    if( (static_state=es2_static_resolve_recorded(renderer,command->element_id,anim_index,pose_id,&static_placement))!=0 )
    {
        if( static_state<0 ) return;
        binding=ES2_STATIC_PAGE_BINDING;
        page_base=static_placement.page_base;
        placement.page_id=static_placement.page_id;
        placement.batch_slot=static_placement.batch_slot;
        placement.entry_index=static_placement.entry_index;
        placement.entry_vertex_count=static_placement.vertex_count;
        vertex_base=static_placement.vertex_base;
    }
    else if( !trspk_pose_table_get(
                 &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
    {
        /* A live static element without its load event (renderer creation,
         * an event-queue overflow): bake the complete animation once on the
         * first miss, not one pose every frame. */
        if( command->animation )
        {
            struct ToriRS_RenderCommand_AnimLoad load;
            memset(&load, 0, sizeof(load));
            load.element_id = command->element_id;
            load.anim_index = anim_index;
            load.animation = command->animation;
            load.model = command->model;
            load.world_position = command->world_position;
            es2p_animation_load(renderer, &load);
        }
        if( !trspk_pose_table_get(
                &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
            vertex_base = es2_bake_into_arena(
                renderer,
                &renderer->groups[group],
                command->element_id,
                anim_index,
                pose_id,
                command->model,
                &command->world_position,
                true);
    }
    if( vertex_base == UINT32_MAX )
        return;

    if( binding < ES2_STATIC_PAGE_BINDING )
    {
        page_base = vertex_base & ~(ES2_VBO_PAGE - 1u);
        local_base = vertex_base - page_base;
    }
    else
        local_base = vertex_base;
    placement.binding = binding;
    placement.page_base = page_base;
    placement.local_base = local_base;
    placement.absolute_base = page_base + local_base;
    placement.face_count = face_count;
    placement.sorted_face_count = sorted_face_count;
    placement.anim_index = anim_index;
    placement.pose_id = pose_id;
    placement.dynamic = dynamic;
    placement.face_order = face_order;
    placement.projected_in_scene = projected_in_scene;
    placement.projected_depth = projected_depth;
    es2p_emit_model(renderer, &placement);
}

void
es2p_end_3d(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    /* The prepared block describes a camera about to go out of scope;
     * unpublishing it is what stops a later pass reading a stale one. */
    if( renderer->scene )
        ToriDraw_SceneClearProjectionCamera(renderer->scene);
    if( !renderer->has_3d )
        goto done;
    if( !es2_upload_atlas(renderer) )
        goto done;
    /* The painter path draws the static world from its resident window
     * and everything else from the frame stream; the retained GPU pages
     * are never read here and never uploaded. */
    es2_painter_flush(renderer);
    es2_frame_stream_upload(renderer);
    es2p_sequence_draw(renderer);
    renderer->painter_stat_frames++;
    renderer->painter_stat_draws += renderer->draw_item_count;
    if( renderer->debug && renderer->painter_stat_frames == 300u )
    {
        es2_report_line(
            "gles2 painter/frame: faces indexed %.0f gathered %.0f actor %.0f; residents "
            "placed %.1f models %.0f vertices, serial hits %.0f; draws %.1f; compactions %u "
            "(held back %u frames, %u since last); ring head %llu; "
            "static pages %u %u vertices",
            renderer->painter_stat_faces_indexed / 300.0,
            renderer->painter_stat_faces_gathered / 300.0,
            renderer->painter_stat_faces_actor / 300.0,
            renderer->painter_stat_placed_models / 300.0,
            renderer->painter_stat_placed_vertices / 300.0,
            renderer->painter_stat_resident_hits / 300.0,
            renderer->painter_stat_draws / 300.0,
            renderer->painter_stat_compactions,
            renderer->painter_stat_compactions_deferred,
            renderer->hot_frames_since_compaction,
            (unsigned long long)renderer->hot_head,
            renderer->static_page_count,
            renderer->static_batch_gpu_vertex_used);
        es2_report_line(
            "gles2 sort/frame: models by bake size tile2 %.0f <=16 %.0f <=64 %.0f <=256 %.0f "
            "larger %.0f; faces in %.0f out %.0f; radix shallow %.1f two-pass %.1f; "
            "prio uniform %.1f varied %.1f; k16 %.1f declined %.1f",
            renderer->painter_stat_sort_models[0] / 300.0,
            renderer->painter_stat_sort_models[1] / 300.0,
            renderer->painter_stat_sort_models[2] / 300.0,
            renderer->painter_stat_sort_models[3] / 300.0,
            renderer->painter_stat_sort_models[4] / 300.0,
            renderer->painter_stat_sort_faces_in / 300.0,
            renderer->painter_stat_sort_faces_out / 300.0,
            g_toridraw_radix_shallow_models / 300.0,
            g_toridraw_radix_two_pass_models / 300.0,
            g_toridraw_prio_uniform_models / 300.0,
            g_toridraw_prio_varied_models / 300.0,
            g_toridraw_sort_k16_models / 300.0,
            g_toridraw_sort_k16_declined / 300.0);
        es2_report_line(
            "gles2 draws/frame: world %.1f; ui batches %.1f (ended by texture %.1f atlas "
            "%.1f scissor %.1f overflow %.1f asked %.1f) rotmask %.1f widget %.1f; ui "
            "upload %.0f B",
            renderer->painter_stat_draws / 300.0,
            renderer->ui_stat_draws_batch / 300.0,
            renderer->ui_stat_break_texture / 300.0,
            renderer->ui_stat_break_atlas / 300.0,
            renderer->ui_stat_break_scissor / 300.0,
            renderer->ui_stat_break_overflow / 300.0,
            ((double)renderer->ui_stat_draws_batch - renderer->ui_stat_break_texture -
             renderer->ui_stat_break_atlas - renderer->ui_stat_break_scissor -
             renderer->ui_stat_break_overflow) /
                300.0,
            renderer->ui_stat_draws_rotmask / 300.0,
            renderer->ui_stat_draws_widget / 300.0,
            renderer->ui_stat_upload_bytes / 300.0);
        es2_report_line(
            "project/frame: models %.1f cull_fast %.1f cull_aabb %.1f error %.1f projected "
            "%.1f vertices %.0f tail_models %.1f",
            g_toridraw_project_census.calls / 300.0,
            g_toridraw_project_census.cull_fast / 300.0,
            g_toridraw_project_census.cull_aabb / 300.0,
            g_toridraw_project_census.cull_error / 300.0,
            g_toridraw_project_census.projected / 300.0,
            g_toridraw_project_census.projected_vertices / 300.0,
            g_toridraw_project_census.tail_models / 300.0);
        es2_report_line(
            "paint/frame: walks %.2f same_inputs %.2f pops %.0f commands %.0f entities %.1f",
            g_torirs_paint_census.walks / 300.0,
            g_torirs_paint_census.same_inputs / 300.0,
            g_torirs_paint_census.pops / 300.0,
            g_torirs_paint_census.commands / 300.0,
            g_torirs_paint_census.entity_commands / 300.0);
        /* A call-site counting shim, when one was built in with -include
         * (scratch tooling; the symbol is absent in every normal build). */
        {
            extern void torirs_shim_dump(void) __attribute__((weak));
            if( torirs_shim_dump )
                torirs_shim_dump();
        }
    }
    if( renderer->painter_stat_frames >= 300u )
    {
        renderer->painter_stat_frames = 0u;
        renderer->painter_stat_faces_indexed = 0u;
        renderer->painter_stat_faces_gathered = 0u;
        renderer->painter_stat_faces_actor = 0u;
        renderer->painter_stat_placed_models = 0u;
        renderer->painter_stat_placed_vertices = 0u;
        renderer->painter_stat_resident_hits = 0u;
        renderer->painter_stat_draws = 0u;
        memset(
            renderer->painter_stat_sort_models, 0, sizeof(renderer->painter_stat_sort_models));
        renderer->painter_stat_sort_faces_in = 0u;
        renderer->painter_stat_sort_faces_out = 0u;
        g_toridraw_radix_shallow_models = 0;
        g_toridraw_radix_two_pass_models = 0;
        g_toridraw_prio_uniform_models = 0;
        g_toridraw_prio_varied_models = 0;
        g_toridraw_sort_k16_models = 0;
        g_toridraw_sort_k16_declined = 0;
        renderer->ui_stat_draws_batch = 0u;
        renderer->ui_stat_draws_rotmask = 0u;
        renderer->ui_stat_draws_widget = 0u;
        renderer->ui_stat_break_texture = 0u;
        renderer->ui_stat_break_atlas = 0u;
        renderer->ui_stat_break_scissor = 0u;
        renderer->ui_stat_break_overflow = 0u;
        renderer->ui_stat_upload_bytes = 0u;
        memset(&g_toridraw_project_census, 0, sizeof(g_toridraw_project_census));
        memset(&g_torirs_paint_census, 0, sizeof(g_torirs_paint_census));
    }

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    es2_sequence_reset(renderer);
    /* The world pass leaves a world-sized viewport and depth state; restore
     * so 2D that follows is neither clipped nor occluded. */
    es2_set_letterbox_viewport(renderer);
    es2_set_depth(renderer, false, false);
    es2_set_cull(renderer, false);
    es2_set_scissor(renderer, NULL);
}

/* ---- the retained model store ------------------------------------------------------- */

static void
es2p_model_load(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_ModelLoad* command)
{
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    /* A model replacement invalidates every pose from the old geometry. */
    (void)es2_model_unload(renderer, command->element_id);
    (void)es2_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->element_id,
        0,
        0,
        command->model,
        &command->world_position,
        true);
}

static void
es2p_animation_load(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command)
{
    int anim_index = 0;
    int frame;

    assert(renderer);
    assert(command);
    if( !es2_animation_load_check(command, &anim_index) )
        return;
    /* Pose keys do not carry a sequence id. Clear the old track so frame zero
     * cannot keep resolving to MODEL_LOAD's rest pose and a shorter
     * replacement cannot serve stale tail frames. */
    (void)es2_animation_track_unload(renderer, command->element_id, anim_index);
    for( frame = 0; frame < command->animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = es2_animation_pose_frame(command, frame);
        struct ToriDraw_ModelHandle handle;
        memset(&handle, 0, sizeof(handle));
        handle.kind = TORIDRAWMK_MODEL;
        handle.u.model.model = baked;
        (void)es2_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id,
            anim_index,
            frame,
            handle,
            &command->world_position,
            true);
        ToriDraw_ModelFree(baked);
    }
}

/* ---- batch commands ----------------------------------------------------------------- */

static void
es2p_batch_begin(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Batch* command)
{
    struct ES2StaticBatch* batch;
    int slot;
    assert(renderer);
    assert(command);
    if( command->batch_id < 0 )
        return;
    slot = es2_static_batch_slot(renderer, command->batch_id, true);
    if( slot < 0 )
        return;
    batch = &renderer->static_batches[slot];
    es2_painter_batch_reset(renderer, batch, 0u);
    es2_invalidate_batch_pages(renderer, batch);
    trspk_batch16_begin(batch->cpu);
    batch->active = false;
    batch->building = true;
    es2_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = slot;
}


static void
es2p_batch_add(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_Batch* command,
    bool animated)
{
    struct ES2StaticBatch* batch;
    struct TRSPK_Batch16Reservation reservation;
    int anim_index;
    int pose_id;
    int face_count;
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    if( renderer->current_batch_slot < 0 ||
        (uint32_t)renderer->current_batch_slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[renderer->current_batch_slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    face_count = trspk_toridraw_face_count(command->model);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    anim_index = animated ? es2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1) : 0;
    pose_id = command->pose_id >= 0 ? command->pose_id : 0;
    if( !trspk_batch16_reserve_pose(
            batch->cpu,
            command->element_id,
            anim_index,
            pose_id,
            (uint32_t)face_count * 3u,
            &reservation) )
        return;
    (void)es2_bake_pose_vertices(
        renderer,
        reservation.vbo,
        reservation.triangles,
        reservation.vertex_base,
        command->model,
        &command->world_position,
        NULL,
        0,
        false);
}


static void
es2p_batch_clear(struct TRSPK_Renderer_ES2* renderer, int batch_id, bool clear_all)
{
    uint32_t slot;
    assert(renderer);
    for( slot = 0u; slot < renderer->static_batch_count; slot++ )
    {
        struct ES2StaticBatch* batch = &renderer->static_batches[slot];
        if( !clear_all && batch->batch_id != batch_id )
            continue;
        es2_painter_batch_reset(renderer, batch, 0u);
        es2_invalidate_batch_pages(renderer, batch);
        trspk_batch16_clear(batch->cpu);
        batch->active = false;
        batch->building = false;
    }
    if( clear_all )
    {
        /* Nothing valid remains, so the bump allocator starts over: every
         * page re-allocates its range the next time its chunk commits. */
        uint32_t page_id;
        for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
            renderer->static_pages[page_id].gpu_capacity = 0u;
        renderer->static_batch_gpu_vertex_used = 0u;
    }
    es2_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = -1;
}


/* ---- dispatch ------------------------------------------------------------------------ */

static void
es2p_dispatch(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        es2p_begin_3d(renderer, &command->u.begin_3d);
        break;
    case TORIRSRC_END_3D:
        es2p_end_3d(renderer);
        break;
    case TORIRSRC_BEGIN_2D:
        es2_begin_2d(renderer);
        break;
    case TORIRSRC_END_2D:
        es2_end_2d(renderer);
        break;
    case TORIRSRC_TEX_LOAD:
        if( command->u.tex_load.texture )
            (void)es2_load_texture_object(
                renderer, command->u.tex_load.texture_id, command->u.tex_load.texture);
        break;
    case TORIRSRC_TEX_UNLOAD:
        es2_unload_texture(renderer, command->u.tex_load.texture_id);
        break;
    case TORIRSRC_MODEL_LOAD:
        es2p_model_load(renderer, &command->u.model_load);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        (void)es2_model_unload(renderer, command->u.model_load.element_id);
        break;
    case TORIRSRC_ANIM_LOAD:
        es2p_animation_load(renderer, &command->u.anim_load);
        break;
    case TORIRSRC_ANIM_UNLOAD:
        (void)es2_animation_track_unload(
            renderer, command->u.anim_load.element_id, command->u.anim_load.anim_index);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        es2p_batch_begin(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        es2p_batch_add(renderer, &command->u.batch, false);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        es2p_batch_add(renderer, &command->u.batch, true);
        break;
    case TORIRSRC_BATCH3D_END:
        es2_batch_end(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        es2p_batch_clear(renderer, command->u.batch.batch_id, command->u.batch.clear_all);
        if( command->u.batch.clear_all )
        {
            trspk_pose_table_clear(&renderer->poses);
            es2_reset_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        }
        break;
    case TORIRSRC_DRAW_MODEL:
        es2p_draw_model(renderer, &command->u.model);
        break;

    case TORIRSRC_CLEAR_RECT:
        es2_ui_draw_clear_rect(renderer, &command->u.clear_rect);
        break;
    case TORIRSRC_FILL_RECT:
        es2_ui_draw_fill_rect(renderer, &command->u.fill_rect);
        break;
    case TORIRSRC_DRAW_MODEL_WIDGET:
        es2_ui_draw_model_widget(renderer, &command->u.model_widget);
        break;
    case TORIRSRC_SPRITE:
        es2_ui_draw_sprite(renderer, &command->u.sprite);
        break;
    case TORIRSRC_FONT:
        es2_ui_draw_font(renderer, &command->u.font);
        break;
    case TORIRSRC_LINE:
        es2_ui_draw_line(renderer, &command->u.line);
        break;
    case TORIRSRC_POLYGON_BEGIN:
        es2_ui_polygon_begin(renderer, &command->u.polygon_begin);
        break;
    case TORIRSRC_POLYGON_POINT:
        es2_ui_polygon_point(renderer, &command->u.polygon_point);
        break;
    case TORIRSRC_POLYGON_END:
        es2_ui_polygon_end(renderer);
        break;
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        break;
    case TORIRSRC_SPRITE_LOAD:
        /* The scene owns pixels. Upload stays lazy so assets never drawn by
         * this backend consume atlas space or transfer bandwidth. */
        break;
    case TORIRSRC_SPRITE_UNLOAD:
        es2_ui_sprite_invalidate(renderer, command->u.sprite_load.element_id);
        break;
    case TORIRSRC_FONT_LOAD:
        es2_ui_font_load(renderer, command->u.font_load.font_id, command->u.font_load.font);
        break;
    case TORIRSRC_FONT_UNLOAD:
        es2_ui_font_unload(renderer, command->u.font_load.font_id);
        break;
    case TORIRSRC_NONE:
        break;
    }
}


/* ---- the frame ---------------------------------------------------------------------- */

/*
 * The surface, then the framebuffer this renderer draws into, then the
 * clear. Two toolkit calls with this renderer's own two lines between them.
 */
static bool
es2p_begin_frame(struct TRSPK_Renderer_ES2* renderer, bool clear_to_black_only, bool allow_offscreen)
{
    if( !es2_frame_surface_begin(renderer, allow_offscreen) )
        return false;
    if( renderer->target_offscreen )
    {
        es2_scale_target_ensure(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    }
    else
    {
        /* The limit is off or no longer binds: give the memory back now. */
        es2_scale_target_destroy_buffers(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    es2_frame_surface_clear(renderer, clear_to_black_only);
    return true;
}

void
TRSPK_Renderer_ES2_PainterDrawBootBar(
    struct TRSPK_Renderer_ES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    assert(renderer);
    /* progress < 0: clear only, no bar -- the post-login loading screen,
     * which is a black screen and the sentence alone on every lane. */
    if( !es2p_begin_frame(renderer, progress < 0, false) )
        return;
    if( progress >= 0 )
    {
        int bar_x;
        int bar_y;
        int fill_w;
        progress = es2_clampi(progress, 0, 100);
        /* The references' bar, not one of ours (engine/boot_bar.h): a filled
         * red track, a black inset one pixel in, then the fill two pixels in. */
        bar_x = renderer->width / 2 - BOOT_BAR_W / 2;
        bar_y = renderer->height / 2 - BOOT_BAR_ABOVE_CENTRE;
        fill_w = progress * BOOT_BAR_PX_PER_PERCENT;
        es2_draw_solid_rect(
            renderer, bar_x, bar_y, BOOT_BAR_W, BOOT_BAR_H, 0xff000000u | BOOT_BAR_COLOR);
        es2_draw_solid_rect(
            renderer, bar_x + 1, bar_y + 1, BOOT_BAR_W - 2, BOOT_BAR_H - 2, 0xff000000u);
        if( fill_w > 0 )
            es2_draw_solid_rect(
                renderer,
                bar_x + BOOT_BAR_INSET,
                bar_y + BOOT_BAR_INSET,
                fill_w,
                BOOT_BAR_FILL_H,
                0xff000000u | BOOT_BAR_COLOR);
    }
    if( caption && caption[0] && caption_font_id >= 0 )
        es2_draw_boot_caption(renderer, caption_font_id, caption);
}

bool
es2p_render_frame_begin(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    if( !es2p_begin_frame(renderer, false, true) )
        return false;
    renderer->has_3d = false;
    renderer->in3d = false;
    renderer->in2d = false;
    renderer->frame_clock += 1.0;
    return true;
}

void
es2p_render_frame_commands(struct TRSPK_Renderer_ES2* renderer, struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    assert(renderer);
    assert(frame);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        es2_prefetch_ahead(renderer, frame);
        es2p_dispatch(renderer, &command);
    }
}

void
es2p_render_frame_end(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
#if defined(TORIRS_ANIM_CHAIN_CAPTURE)
    ToriDraw_AnimCaptureEndPass();
#endif
#if defined(TORIRS_PLACEMENT_CAPTURE)
    es2_placement_capture_end();
#endif
    if( renderer->in3d )
        es2p_end_3d(renderer);
    if( renderer->in2d )
        es2_end_2d(renderer);
    if( renderer->target_offscreen )
        es2_scale_target_present(renderer);

    es2_frame_readback(renderer);
}

void
TRSPK_Renderer_ES2_PainterExecute(struct TRSPK_Renderer_ES2* renderer, struct ToriRS_RenderCommand const* command)
{
    es2p_dispatch(renderer, command);
}

void
TRSPK_Renderer_ES2_PainterRenderFrame(struct TRSPK_Renderer_ES2* renderer, struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !es2p_render_frame_begin(renderer) )
        return;
    ToriRS_FrameBegin(frame);
    es2p_render_frame_commands(renderer, frame);
    ToriRS_FrameEnd(frame);
    es2p_render_frame_end(renderer);
}

/* ---- lifetime ------------------------------------------------------------------------ */

/*
 * No depth bits: this path never reads or writes depth, and asking for a
 * buffer it will not use costs bandwidth on a tiler for nothing.
 */
bool
TRSPK_Renderer_ES2_PainterInit(
    struct TRSPK_Renderer_ES2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(renderer);
    assert(window);
    assert(scene);
    return es2_init_gl(renderer, window, scene, 0, "painter");
}
