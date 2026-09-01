/**
 * The painter's-algorithm world path for the GLES2 renderer.
 *
 * The legacy RS ordering: no depth buffer at all. Every model's faces are
 * sorted back-to-front on the CPU by ToriDraw_RenderModel2SortFaces and drawn
 * in that order, and correctness lives entirely in the submission order.
 *
 * WHY THE STATIC WORLD DRAWS FROM A RESIDENT WINDOW
 *
 * The D3D9 lane keeps every model retained on the GPU and rebuilds a U16 index
 * stream per frame; a page change there is a BaseVertexIndex argument. GLES2
 * has no base vertex: an index reaches 65536 vertices from wherever the
 * attributes are bound, and rebinding is a fresh draw call. The retained
 * world is far larger than that (a loaded region is ~960k static vertices in
 * 15 chunks) and painter order hops between chunks tile by tile -- terrain
 * and the locs standing on it were baked hundreds of thousands of vertices
 * apart -- so indexing the retained pages directly was measured at 750
 * window changes a frame. Copying every sorted face into an ordered stream
 * instead (the first design) drew the whole world in one call but cost a
 * random 84-byte gather per face, ten thousand faces a frame.
 *
 * What a frame actually draws is ~40k static vertices: it fits a U16 window.
 * So the painter keeps a RESIDENT WINDOW -- a ring of GLES2_HOT_RING_VERTICES
 * on the GPU. A static model is copied into it the first time it is drawn
 * (one sequential copy of its bake, staged and sent once per frame), stays
 * while it keeps being drawn, and is evicted when the ring wraps over it.
 * Every frame after the first, a resident model costs six bytes a face of
 * indices and no vertex traffic at all. The visible set changes slowly, so
 * placements are a trickle, not a gather.
 *
 * Actors are baked into the per-frame stream in sorted order, as before, and
 * a static model that cannot be resident (bigger than the ring, or not a
 * batch entry) is gathered into the stream as before. Every item asks for the
 * cutout program, and order is the sequence order, whichever buffer an item
 * draws from. platform_android_renderer_gles2_zbuffer.c is the depth-tested
 * alternative; the two are peers and neither calls the other.
 */

#include "platform/platform_android_renderer_gles2_core.h"

#include "toridraw.h"

#include <assert.h>
#include <string.h>

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
struct GLES2FaceVertices
{
    struct TRSPK_VertexGLES2 corner[3];
};
_Static_assert(sizeof(struct GLES2FaceVertices) == 84u, "three packed vertices");

static inline void
gles2_face_copy(struct GLES2FaceVertices* destination, const struct GLES2FaceVertices* source)
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

void
gles2_painter_setup_projection(struct ToriRS_GLES2* renderer)
{
    /* trspk_compute_pass_matrices leaves clip z at a constant, which is right
     * for a pass that never reads depth. Nothing to remap. */
    assert(renderer);
    (void)renderer;
}

void
gles2_painter_apply_world_states(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    /* Painter order: the submission order IS the depth order, so the depth
     * test must never reject. And no culling: the painter sorts faces and has
     * its own reasons to see every one of them. */
    gles2_set_depth(renderer, false, false);
    gles2_set_cull(renderer, false);
    gles2_set_blend(renderer, true);
}

int
gles2_painter_sort_faces(
    struct ToriRS_GLES2* renderer,
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
gles2_painter_gather(
    struct GLES2FaceVertices* destination,
    const struct GLES2FaceVertices* source,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    /* The gather is a random walk over the bake -- sorted order is depth
     * order, not memory order -- so every face is a cold cache line pair.
     * The order array names the faces ahead of time, so ask for them ahead
     * of time: a prefetch a few faces out overlaps that miss with this
     * face's copy. */
    enum { GLES2_GATHER_PREFETCH_AHEAD = 4 };
    uint32_t index;

    assert(destination);
    assert(source);
    assert(faces);
    for( index = 0u; index < count; index++ )
    {
        uint32_t face = (uint32_t)faces[index];
        if( index + GLES2_GATHER_PREFETCH_AHEAD < count )
        {
            uint32_t ahead = (uint32_t)faces[index + GLES2_GATHER_PREFETCH_AHEAD];
            if( ahead < source_face_limit )
            {
                __builtin_prefetch(&source[ahead], 0, 0);
                __builtin_prefetch((const uint8_t*)&source[ahead] + 64, 0, 0);
            }
        }
        if( face < source_face_limit )
            gles2_face_copy(&destination[index], &source[face]);
        else
            memset(&destination[index], 0, sizeof(destination[index]));
    }
}

/* ---- the resident window ---------------------------------------------------------- */

static bool
gles2_painter_hot_ensure(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    if( renderer->hot_vbo )
        return true;
    if( !renderer->gl_context )
        return false;
    glGenBuffers(1, &renderer->hot_vbo);
    gles2_bind_array_buffer(renderer, renderer->hot_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)((size_t)GLES2_HOT_RING_VERTICES * sizeof(struct TRSPK_VertexGLES2)),
        NULL,
        GL_DYNAMIC_DRAW);
    if( !gles2_check_error("resident window buffer") )
    {
        glDeleteBuffers(1, &renderer->hot_vbo);
        renderer->hot_vbo = 0u;
        return false;
    }
    /* Serial 0 is "never placed"; start the head a whole ring in so no
     * placement can ever be handed serial 0. */
    renderer->hot_head = GLES2_HOT_RING_VERTICES;
    return true;
}

void
gles2_painter_batch_reset(
    struct ToriRS_GLES2* renderer,
    struct GLES2StaticBatch* batch,
    uint32_t entry_count)
{
    assert(renderer);
    assert(batch);
    (void)renderer;
    if( entry_count > batch->hot_serial_capacity )
    {
        uint32_t capacity = batch->hot_serial_capacity ? batch->hot_serial_capacity : 256u;
        uint32_t* grown;
        while( capacity < entry_count )
            capacity *= 2u;
        grown = (uint32_t*)realloc(batch->hot_serial, (size_t)capacity * sizeof(*grown));
        assert(grown);
        batch->hot_serial = grown;
        batch->hot_serial_capacity = capacity;
    }
    if( batch->hot_serial_capacity )
        memset(batch->hot_serial, 0, (size_t)batch->hot_serial_capacity * sizeof(*batch->hot_serial));
}

void
gles2_painter_flush(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    /* Fragmentation check, on the frame that is ending: a long walk places
     * new residents at the head, far from the neighbours they are drawn
     * between, and every such seam is a window switch and a draw. Past the
     * threshold, empty the ring -- one frame of re-placing the live set in
     * painter order puts everything back into a few windows. Advancing the
     * head a whole ring is the eviction: every serial fails the residency
     * test. */
    if( renderer->hot_vbo && renderer->draw_item_count > GLES2_HOT_COMPACT_DRAWS )
    {
        renderer->hot_head += GLES2_HOT_RING_VERTICES;
        renderer->painter_stat_compactions++;
    }
    if( renderer->hot_stage_count == 0u )
        return;
    assert(renderer->hot_vbo);
    assert(renderer->hot_stage_address + renderer->hot_stage_count <= GLES2_HOT_RING_VERTICES);
    gles2_bind_array_buffer(renderer, renderer->hot_vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        (GLintptr)((size_t)renderer->hot_stage_address * sizeof(struct TRSPK_VertexGLES2)),
        (GLsizeiptr)((size_t)renderer->hot_stage_count * sizeof(struct TRSPK_VertexGLES2)),
        renderer->hot_stage);
    renderer->painter_stat_placed_vertices += renderer->hot_stage_count;
    renderer->hot_stage_count = 0u;
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
gles2_painter_place(
    struct ToriRS_GLES2* renderer,
    struct GLES2StaticBatch* batch,
    uint32_t entry_index,
    const struct TRSPK_VertexGLES2* vertices,
    uint32_t span)
{
    uint32_t serial;
    uint32_t address;

    assert(renderer);
    assert(batch);
    assert(vertices);
    /* A model must fit one draw window, not merely the ring. */
    if( span == 0u || span > GLES2_HOT_WINDOW_VERTICES )
        return UINT32_MAX;
    if( entry_index >= batch->hot_serial_capacity )
        return UINT32_MAX;
    if( !gles2_painter_hot_ensure(renderer) )
        return UINT32_MAX;

    serial = batch->hot_serial[entry_index];
    if( serial != 0u && renderer->hot_head - serial <= GLES2_HOT_RING_VERTICES )
    {
        if( serial < renderer->hot_frame_oldest_serial )
            renderer->hot_frame_oldest_serial = serial;
        return serial % GLES2_HOT_RING_VERTICES;
    }

    /* Not resident: place at the head, on a fresh lap if it would not fit
     * the rest of this one. The staging run must stay contiguous in the
     * ring, so a lap change sends what is staged first. */
    address = renderer->hot_head % GLES2_HOT_RING_VERTICES;
    {
        uint32_t head_after = renderer->hot_head + span;
        if( address + span > GLES2_HOT_RING_VERTICES )
            head_after = renderer->hot_head + (GLES2_HOT_RING_VERTICES - address) + span;
        /* The overwrite guard: this frame's draw reads every resident it
         * has been handed, from the oldest one on, and those bytes must
         * survive until it runs. A frame whose live set outgrows the ring
         * gathers the overflow instead of eating its own tail. */
        if( renderer->hot_frame_oldest_serial != UINT32_MAX &&
            head_after - renderer->hot_frame_oldest_serial > GLES2_HOT_RING_VERTICES )
            return UINT32_MAX;
    }
    if( address + span > GLES2_HOT_RING_VERTICES )
    {
        gles2_painter_flush(renderer);
        renderer->hot_head += GLES2_HOT_RING_VERTICES - address;
        address = 0u;
    }
    if( renderer->hot_stage_count == 0u )
        renderer->hot_stage_address = address;
    if( renderer->hot_stage_count + span > renderer->hot_stage_capacity )
    {
        uint32_t capacity = renderer->hot_stage_capacity ? renderer->hot_stage_capacity
                                                         : GLES2_HOT_STAGE_INIT_VERTICES;
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
gles2_painter_push_resident(
    struct ToriRS_GLES2* renderer,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    uint16_t* indices;
    uint32_t index;
    uint32_t window = address;
    uint32_t span = source_face_limit * 3u;

    assert(renderer);
    assert(faces);
    if( count == 0u )
        return;
    if( renderer->draw_item_count > 0u )
    {
        const struct GLES2DrawItem* open = &renderer->draw_items[renderer->draw_item_count - 1u];
        if( open->indexed && open->binding == GLES2_HOT_BINDING && address >= open->page_base &&
            address + span <= open->page_base + GLES2_HOT_WINDOW_VERTICES )
            window = open->page_base;
    }
    renderer->painter_stat_faces_indexed += count;
    /* Written straight into the draw sequence's staging: no scratch, no copy. */
    indices = gles2_sequence_reserve_indexed(renderer, count * 3u);
    assert(indices);
    assert(address - window + span <= GLES2_HOT_WINDOW_VERTICES);
    address -= window;
    for( index = 0u; index < count; index++ )
    {
        uint32_t face = (uint32_t)faces[index];
        uint32_t vertex = face < source_face_limit ? address + (face * 3u) : address;
        uint32_t step = face < source_face_limit ? 1u : 0u;
        uint16_t* triplet = indices + (index * 3u);
        triplet[0] = (uint16_t)vertex;
        triplet[1] = (uint16_t)(vertex + step);
        triplet[2] = (uint16_t)(vertex + step + step);
    }
    gles2_sequence_commit_indexed(renderer, GLES2_HOT_BINDING, window, true, false, count * 3u);
}

/* ---- emission ---------------------------------------------------------------------- */

void
gles2_painter_emit_model(
    struct ToriRS_GLES2* renderer,
    const struct GLES2ModelPlacement* placement)
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
    if( placement->binding == GLES2_FRAME_STREAM_BINDING )
    {
        renderer->painter_stat_faces_actor += face_count;
        gles2_sequence_push_array(
            renderer, GLES2_FRAME_STREAM_BINDING, placement->absolute_base, face_count * 3u, true,
            false);
        return;
    }

    /* A retained model. */
    if( !gles2_binding_cpu_source(
            renderer, placement->binding, placement->page_id, &source_vbo, &source_triangles) ||
        source_vbo->format != TRSPK_VERTEX_FORMAT_GLES2 )
        return;
    (void)source_triangles;
    /* A Batch16 chunk's CPU copy is the chunk alone and starts at zero; an
     * arena's is the whole buffer. */
    source_base = placement->binding == GLES2_STATIC_PAGE_BINDING
        ? placement->local_base
        : placement->page_base + placement->local_base;
    if( source_base > source_vbo->vertex_count )
        return;
    /* The order names faces by their index in the MODEL, which runs past the
     * sorted count -- placement->face_count is that sorted count here, so it
     * is NOT a bound on face indices. The only bound is the bake itself. */
    source_face_limit = (source_vbo->vertex_count - source_base) / 3u;
    face_order = ToriDraw_FaceOrder(renderer->scene);

    /* A batch entry lives in the resident window: the entry's whole bake is
     * what gets placed, since any of its faces may be named this frame or
     * the next. */
    if( placement->binding == GLES2_STATIC_PAGE_BINDING &&
        placement->batch_slot < renderer->static_batch_count &&
        placement->entry_index != UINT32_MAX && placement->entry_vertex_count > 0u )
    {
        uint32_t entry_faces = placement->entry_vertex_count / 3u;
        uint32_t address;
        if( entry_faces < source_face_limit )
            source_face_limit = entry_faces;
        address = gles2_painter_place(
            renderer,
            &renderer->static_batches[placement->batch_slot],
            placement->entry_index,
            source_vbo->vertices.as_gles2 + source_base,
            source_face_limit * 3u);
        if( address != UINT32_MAX )
        {
            gles2_painter_push_resident(renderer, address, source_face_limit, face_order, face_count);
            return;
        }
    }

    /* Everything else is gathered into the stream in sorted order. */
    renderer->painter_stat_faces_gathered += face_count;
    first = gles2_frame_stream_reserve(renderer, face_count * 3u);
    gles2_painter_gather(
        (struct GLES2FaceVertices*)&renderer->frame_stream_cpu->vertices.as_gles2[first],
        (const struct GLES2FaceVertices*)(source_vbo->vertices.as_gles2 + source_base),
        source_face_limit,
        face_order,
        face_count);
    gles2_sequence_push_array(
        renderer, GLES2_FRAME_STREAM_BINDING, first, face_count * 3u, true, false);
}
