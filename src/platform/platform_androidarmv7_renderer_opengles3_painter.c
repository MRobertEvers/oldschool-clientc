/**
 * The painter's-algorithm world path for the WebGL2 renderer.
 *
 * The legacy RS ordering: no depth buffer at all. Every model's faces are
 * sorted back-to-front on the CPU by ToriDraw_RenderModel2SortFaces and drawn
 * in that order, and correctness lives entirely in the submission order.
 *
 * WHY THIS ONE IS SO MUCH SMALLER THAN THE GLES2 PAINTER
 *
 * The retained world is far larger than a 16-bit index can address: a loaded
 * region is ~960k static vertices, and painter order hops between chunks tile
 * by tile, because the terrain and the locs standing on it were baked
 * hundreds of thousands of vertices apart. On OpenGL ES 2.0 an index reaches
 * 65,536 vertices from wherever the attributes are bound and rebinding means
 * a fresh draw call, so the GLES2 painter cannot index that world at all. It
 * carries two machines to get around it:
 *
 *   - a RESIDENT WINDOW, a GPU ring that each drawn static model is copied
 *     into so that it lands somewhere a U16 index can reach, with per-entry
 *     placement serials, an overwrite guard, and a fragmentation compaction
 *     with hysteresis;
 *   - a GATHER, which copies each sorted face's 84 bytes out of the retained
 *     bake into one ordered per-frame stream for everything the ring refuses.
 *
 * A 32-bit index makes both unnecessary. Here a retained model is drawn
 * WHERE IT WAS BAKED: six bytes of index per face, no vertex traffic, no
 * residency to track, and no copy. Consecutive models merge into one draw
 * whatever part of the buffer they live in.
 *
 * What that is worth, measured on a settled Lumbridge scene in a browser
 * (TORIRS_ES3_DEBUG=1 against TORIRS_GLES2_DEBUG=1, same world, same
 * camera): both renderers index the same 7,187 static faces a frame and
 * issue a similar handful of draws, because the ES2 ring does its job while
 * the camera is still. What this renderer does not do is the rest of it --
 * ~670 residency lookups a frame, a placement and its upload every time the
 * camera reaches new geometry, a 4 x 65,536-vertex ring on the GPU, its CPU
 * staging buffer, and a 64-bit serial per batch entry. The draw count is not
 * where the difference is; the machinery is.
 *
 * What still goes through the per-frame stream is an actor: its pose is this
 * frame's, it has no retained home, and the core bakes it straight into the
 * stream in sorted face order, which is an array draw with no indices at all.
 *
 * Every item asks for the cutout program, and order is the sequence order,
 * whichever buffer an item draws from. platform_renderer_es3_zbuffer.c is
 * the depth-tested alternative; the two are peers and neither calls the other.
 */

#include "platform/platform_renderer_es3_core.h"

#include "platform/platform_renderer_es3_indices.h"
#include "toridraw.h"

#include <assert.h>
#include <string.h>

void
es3_painter_setup_projection(struct ToriRS_ES3* renderer)
{
    /* trspk_compute_pass_matrices leaves clip z at a constant, which is right
     * for a pass that never reads depth. Nothing to remap. */
    assert(renderer);
    (void)renderer;
}

void
es3_painter_apply_world_states(struct ToriRS_ES3* renderer)
{
    assert(renderer);
    /* Painter order: the submission order IS the depth order, so the depth
     * test must never reject. And no culling: the painter sorts faces and has
     * its own reasons to see every one of them. */
    es3_set_depth(renderer, false, false);
    es3_set_cull(renderer, false);
    es3_set_blend(renderer, true);
}

int
es3_painter_sort_faces(
    struct ToriRS_ES3* renderer,
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

void
es3_painter_flush(struct ToriRS_ES3* renderer)
{
    /* Nothing is staged on the way to the GPU any more: a retained model is
     * drawn where it was baked. Kept as the core's end-of-pass hook because
     * that is a seam worth having, and because the depth path's peer call
     * sits next to it. */
    assert(renderer);
    (void)renderer;
}

void
es3_painter_batch_reset(
    struct ToriRS_ES3* renderer,
    struct ES3StaticBatch* batch,
    uint32_t entry_count)
{
    /* The GLES2 painter keeps a placement serial per batch entry and clears
     * it here. There is no placement to forget. */
    assert(renderer);
    assert(batch);
    (void)renderer;
    (void)batch;
    (void)entry_count;
}

/* ---- emission ---------------------------------------------------------------------- */

/*
 * Push one retained model's sorted faces as indices into its own buffer.
 *
 * `address` is the model's first vertex in that buffer, and the run's vertex
 * range is [address, address + source_face_limit * 3) -- which is what
 * glDrawRangeElements is told, so the draw item can carry the union of every
 * model merged into it.
 */
static void
es3_painter_push_indexed(
    struct ToriRS_ES3* renderer,
    uint32_t binding,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    uint32_t* indices;
    uint32_t span;

    assert(renderer);
    assert(faces);
    if( count == 0u || source_face_limit == 0u )
        return;
    span = source_face_limit * 3u;
    renderer->painter_stat_faces_indexed += count;
    /* Written straight into the draw sequence's staging: no scratch, no copy. */
    indices = es3_sequence_reserve_indexed(renderer, count * 3u);
    assert(indices);
    es3_painter_write_indices_ex(
        indices, address, source_face_limit, faces, count, /*use_neon=*/true);
    es3_sequence_commit_indexed(
        renderer, binding, address, address + span - 1u, true, false, count * 3u);
}

void
es3_painter_emit_model(
    struct ToriRS_ES3* renderer,
    const struct ES3ModelPlacement* placement)
{
    const struct TRSPK_VBO* source_vbo;
    const struct TRSPK_Triangles* source_triangles;
    const int* face_order;
    uint32_t source_face_limit;
    uint32_t face_count;

    assert(renderer);
    assert(placement);
    if( placement->sorted_face_count <= 0 )
        return;
    face_count = (uint32_t)placement->sorted_face_count;

    /* An actor was baked into the stream in sorted order by the core; its
     * placement already names the stream. */
    if( placement->binding == ES3_FRAME_STREAM_BINDING )
    {
        renderer->painter_stat_faces_actor += face_count;
        es3_sequence_push_array(
            renderer,
            ES3_FRAME_STREAM_BINDING,
            placement->absolute_base,
            face_count * 3u,
            true,
            false);
        return;
    }

    /* Whose ever bench it came from -- the scene's, or a stage source's. */
    face_order = placement->face_order;
    assert(face_order);

    /*
     * A batch entry knows its own baked vertex count, which is the bound on
     * the faces the order may name; that is the whole per-model cost here,
     * with no CPU source to resolve. The GLES2 painter has to reach the
     * chunk's CPU vertices for every model it places, because placing means
     * copying them.
     */
    if( placement->binding == ES3_STATIC_PAGE_BINDING &&
        placement->entry_index != UINT32_MAX && placement->entry_vertex_count > 0u )
    {
        es3_painter_push_indexed(
            renderer,
            ES3_STATIC_PAGE_BINDING,
            placement->absolute_base,
            placement->entry_vertex_count / 3u,
            face_order,
            face_count);
        return;
    }

    /*
     * Anything else retained -- an arena pose, or a static page whose Batch16
     * entry is not known -- still needs its bake's extent, and only the CPU
     * copy knows it. The order names faces by their index in the MODEL, which
     * runs past the sorted count, so placement->face_count is NOT a bound on
     * face indices; the bake is.
     */
    if( !es3_binding_cpu_source(
            renderer, placement->binding, placement->page_id, &source_vbo, &source_triangles) ||
        source_vbo->format != TRSPK_VERTEX_FORMAT_GLES2 )
        return;
    (void)source_triangles;
    if( placement->chunk_base > source_vbo->vertex_count )
        return;
    source_face_limit = (source_vbo->vertex_count - placement->chunk_base) / 3u;
    es3_painter_push_indexed(
        renderer,
        placement->binding,
        placement->absolute_base,
        source_face_limit,
        face_order,
        face_count);
}
