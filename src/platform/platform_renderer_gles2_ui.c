/**
 * The GLES2 renderer's 2D stack: the retained sprite atlas, retained font
 * atlases, rectangles, lines, convex polygons, the rotated-masked chrome
 * (minimap, compass), widget models and the boot bar.
 *
 * The shape is the D3D9 renderer's native UI composition
 * (WINDOWS-D3D9-2D-001): static pixels enter a cache once and a normal frame
 * only resubmits a compact vertex stream. What is GL here:
 *
 *   - the vertex stream is one ring buffer (gles2_ring_upload), appended at
 *     the head so no flush ever waits on the draw before it. With
 *     TORIRS_GLES2_UI_DEFER (the default) a 2D pass is ONE append: every
 *     batch is recorded as a range of one CPU array and the whole pass is
 *     uploaded, bound and drawn by gles2_ui_submit at the end -- see
 *     struct GLES2UIDrawRecord and the submit for the GL argument;
 *   - fonts are GL_LUMINANCE_ALPHA textures, two bytes a texel, so glyph
 *     quads go through the same texture * colour program as everything else
 *     with no text mode to switch;
 *   - the rotated-masked sprite is one draw with two samplers, the source
 *     through the rotated quad and the mask axis-aligned over the box;
 *   - widget models are transient triangles with the view depth in the
 *     vertex's w, which is how the UI program gets perspective-correct
 *     interpolation without a second projection.
 *
 * Every draw here alpha-tests at 1/255 and blends, as the D3D9 UI states do.
 */

#include "platform/platform_renderer_gles2_core.h"

#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "render/trspk_sprite.h"

#include "core/trspk_math.h"
#include "toridraw.h"
#include "toridraw_math.h"
#include "toridraw_model_sprite.h"
#include "toridraw_sprite.h"

#include <assert.h>
#include <limits.h>
#include <math.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Implemented in the core beside the world atlas upload; it shares the
 * packed-row sub-rectangle path. */
bool
gles2_upload_ui_atlas_texture(struct ToriRS_GLES2* renderer, int64_t* out_bytes);

/* ---- helpers ------------------------------------------------------------------ */

static bool
gles2_ui_rect_equal(const struct GLES2Rect* a, const struct GLES2Rect* b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

/*
 * The logical clip a command's scissor names, clamped to the canvas. False
 * when nothing can show. Twin of gles2_scissor_rect for the CPU-clipped
 * quad path; the GL-space rect is still built where a scissor state is
 * genuinely needed (rotated sprites, polygons, widget models).
 */
static bool
gles2_ui_clip_from(
    const struct ToriRS_GLES2* renderer,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    struct GLES2Clip* out)
{
    assert(renderer);
    assert(out);
    if( scissor_w <= 0 || scissor_h <= 0 || renderer->width <= 0 || renderer->height <= 0 )
        return false;
    out->x0 = gles2_clampi(scissor_x, 0, renderer->width);
    out->y0 = gles2_clampi(scissor_y, 0, renderer->height);
    out->x1 = gles2_clampi(scissor_x + scissor_w, 0, renderer->width);
    out->y1 = gles2_clampi(scissor_y + scissor_h, 0, renderer->height);
    return out->x1 > out->x0 && out->y1 > out->y0;
}

/* The command's scissor cut down to a destination box, as a logical clip. */
static bool
gles2_ui_clip_intersect(
    const struct ToriRS_GLES2* renderer,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    struct GLES2Clip* out)
{
    int x;
    int y;
    int w;
    int h;
    trspk_rect_intersect(
        scissor_x, scissor_y, scissor_w, scissor_h, box_x, box_y, box_w, box_h, &x, &y, &w, &h);
    return gles2_ui_clip_from(renderer, x, y, w, h, out);
}

static bool
gles2_ui_intersect_scissor_rect(
    const struct ToriRS_GLES2* renderer,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    struct GLES2Rect* out)
{
    int x;
    int y;
    int w;
    int h;
    trspk_rect_intersect(
        scissor_x, scissor_y, scissor_w, scissor_h, box_x, box_y, box_w, box_h, &x, &y, &w, &h);
    return gles2_scissor_rect(renderer, x, y, w, h, out);
}

static void
gles2_ui_set_texture_filter(struct ToriRS_GLES2* renderer, GLuint texture)
{
    GLenum filter = gles2_ui_filter(renderer);
    if( !texture )
        return;
    gles2_bind_texture0(renderer, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
}

static GLuint
gles2_ui_new_texture(struct ToriRS_GLES2* renderer)
{
    GLuint texture = 0u;
    GLenum filter = gles2_ui_filter(renderer);
    glGenTextures(1, &texture);
    assert(texture);
    gles2_bind_texture0(renderer, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

static void
gles2_ui_delete_texture(struct ToriRS_GLES2* renderer, GLuint* texture)
{
    if( !*texture )
        return;
    if( renderer->bound_texture0 == *texture )
        gles2_bind_texture0(renderer, 0u);
    /* Unit 1 too: a deleted name comes back from glGenTextures, and a cache
     * still holding it would skip the bind and sample an incomplete texture
     * (which reads as alpha 0 -- the minimap vanished this way once). */
    if( renderer->bound_texture1 == *texture )
        gles2_bind_texture1(renderer, 0u);
    glDeleteTextures(1, texture);
    *texture = 0u;
}

/* ---- the batch ------------------------------------------------------------------ */

void
gles2_ui_batch_reset(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    renderer->ui_batch.vertex_count = 0u;
    renderer->ui_batch.first = renderer->ui_pass_vertex_count;
    renderer->ui_batch.texture1 = 0u;
    renderer->ui_batch.uses_sprite_atlas = false;
    renderer->ui_batch.scissor_enabled = false;
    memset(&renderer->ui_batch.scissor, 0, sizeof(renderer->ui_batch.scissor));
}

/* ---- the deferred pass (TORIRS_GLES2_UI_DEFER) ---------------------------------- */

/* Room for `additional` UI vertices at the end of the pass array. */
static void
gles2_ui_pass_reserve_vertices(struct ToriRS_GLES2* renderer, uint32_t additional)
{
    uint32_t needed = renderer->ui_pass_vertex_count + additional;
    uint32_t capacity;
    struct GLES2VertexUI* grown;
    if( needed <= renderer->ui_pass_vertex_capacity )
        return;
    capacity = renderer->ui_pass_vertex_capacity ? renderer->ui_pass_vertex_capacity
                                                 : GLES2_UI_PASS_INIT_VERTICES;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct GLES2VertexUI*)realloc(
        renderer->ui_pass_vertices, (size_t)capacity * sizeof(*grown));
    assert(grown);
    renderer->ui_pass_vertices = grown;
    renderer->ui_pass_vertex_capacity = capacity;
}

static void
gles2_ui_pass_reserve_rotmask_vertices(struct ToriRS_GLES2* renderer, uint32_t additional)
{
    uint32_t needed = renderer->ui_pass_rotmask_count + additional;
    uint32_t capacity;
    struct GLES2VertexRotmask* grown;
    if( needed <= renderer->ui_pass_rotmask_capacity )
        return;
    capacity = renderer->ui_pass_rotmask_capacity ? renderer->ui_pass_rotmask_capacity : 24u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct GLES2VertexRotmask*)realloc(
        renderer->ui_pass_rotmask_vertices, (size_t)capacity * sizeof(*grown));
    assert(grown);
    renderer->ui_pass_rotmask_vertices = grown;
    renderer->ui_pass_rotmask_capacity = capacity;
}

static struct GLES2UIDrawRecord*
gles2_ui_pass_record_append(struct ToriRS_GLES2* renderer)
{
    struct GLES2UIDrawRecord* record;
    if( renderer->ui_pass_record_count >= renderer->ui_pass_record_capacity )
    {
        uint32_t capacity = renderer->ui_pass_record_capacity
            ? renderer->ui_pass_record_capacity * 2u
            : GLES2_UI_PASS_INIT_RECORDS;
        struct GLES2UIDrawRecord* grown = (struct GLES2UIDrawRecord*)realloc(
            renderer->ui_pass_records, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->ui_pass_records = grown;
        renderer->ui_pass_record_capacity = capacity;
    }
    record = &renderer->ui_pass_records[renderer->ui_pass_record_count++];
    memset(record, 0, sizeof(*record));
    return record;
}

/* The UI program with this frame's projection and the UI blend/depth state.
 * Idempotent through the core's state cache. The projection uniform is
 * program-object state in GL (ES 2.0 §2.10.4: glUniform writes into the
 * program, and the value survives glUseProgram away and back), so on the
 * deferred arm it is pushed only when projection_2d changed; the control arm
 * pushes it every time, as it always did. */
static void
gles2_ui_apply_states(struct ToriRS_GLES2* renderer)
{
    gles2_use_program(renderer, &renderer->program_ui);
    if( !renderer->lever_ui_defer || !renderer->ui_projection_pushed )
    {
        glUniformMatrix4fv(renderer->program_ui.u_matrix, 1, GL_FALSE, renderer->projection_2d);
        renderer->ui_projection_pushed = true;
    }
    gles2_set_blend(renderer, true);
    gles2_set_depth(renderer, false, false);
    gles2_set_cull(renderer, false);
}

/*
 * Upload and draw everything the pass recorded. The GL facts this rests on:
 *
 *   one upload      the pass array (UI vertices, then the few rotmask-layout
 *                   vertices packed after them, 4-byte aligned because 28 is)
 *                   goes up as ONE glBufferSubData into this frame's buffer of
 *                   the rotating 2D set; that buffer was last read four frames
 *                   ago, so the write meets no outstanding read and the driver
 *                   has no reason to ghost it.
 *   one bind        glVertexAttribPointer captures (buffer, offset, stride)
 *                   into context state that persists across draws and across
 *                   glUseProgram (ES 2.0 has no VAO; §2.8), so the UI layout
 *                   is pointed at the array's base ONCE and each record is
 *                   glDrawArrays(first, count): vertex i is read at base +
 *                   i * stride, with no 65536 ceiling (that is the U16 index
 *                   limit, and nothing here is indexed). A rotmask record
 *                   re-points the attributes at its own stride and the next
 *                   UI record points them back; that is two rebinds a frame.
 *   per-draw state  scissor, the two texture units and the program are
 *                   applied when a draw is ISSUED, not when it was recorded,
 *                   so each record carries them and the loop re-applies them
 *                   through the state cache (a no-op when unchanged).
 *   textures        the sprite atlas is uploaded once, before the first draw,
 *                   because every record that samples it is issued after that
 *                   point and the atlas only GROWS within a pass -- a sprite
 *                   replaced in place arrives as a SPRITE_UNLOAD command, and
 *                   that path submits (gles2_ui_flush) before touching the
 *                   tile. The same argument covers the rotmask textures and
 *                   the world atlas, whose writers all flush first.
 *   deletion        glDeleteTextures on a name a RECORDED draw still wants
 *                   would make that draw sample texture 0. Every deleter
 *                   (sprite invalidate, font release, unload) calls
 *                   gles2_ui_flush, which issues the records first; an
 *                   issued draw keeps its texture by GL's own rules.
 */
static void
gles2_ui_submit(struct ToriRS_GLES2* renderer)
{
    uint32_t ui_bytes;
    uint32_t rotmask_bytes;
    uint32_t total_bytes;
    uint32_t base_offset;
    uint32_t rotmask_offset;
    uint32_t record_index;
    bool needs_sprite_atlas = false;
    bool sprite_atlas_ok = true;

    assert(renderer);
    assert(renderer->lever_ui_defer);
    if( renderer->ui_pass_record_count == 0u )
    {
        renderer->ui_pass_vertex_count = 0u;
        renderer->ui_pass_rotmask_count = 0u;
        renderer->ui_batch.first = 0u;
        return;
    }
    for( record_index = 0u; record_index < renderer->ui_pass_record_count; record_index++ )
        if( renderer->ui_pass_records[record_index].uses_sprite_atlas )
            needs_sprite_atlas = true;
    if( needs_sprite_atlas &&
        (trspk_atlas_is_dirty(&renderer->ui_sprite_atlas) || !renderer->ui_sprite_atlas_allocated) )
    {
        int64_t bytes = 0;
        if( !renderer->ui_sprite_atlas_texture )
            renderer->ui_sprite_atlas_texture = gles2_ui_new_texture(renderer);
        sprite_atlas_ok = gles2_upload_ui_atlas_texture(renderer, &bytes);
    }

    /* The rotmask vertices ride in the tail of the same array so the pass is
     * one append (see gles2_stream_set_append on why one matters). */
    ui_bytes = renderer->ui_pass_vertex_count * (uint32_t)sizeof(struct GLES2VertexUI);
    rotmask_bytes = renderer->ui_pass_rotmask_count * (uint32_t)sizeof(struct GLES2VertexRotmask);
    total_bytes = ui_bytes + rotmask_bytes;
    if( rotmask_bytes )
    {
        gles2_ui_pass_reserve_vertices(
            renderer, rotmask_bytes / (uint32_t)sizeof(struct GLES2VertexUI) + 1u);
        memcpy(
            (uint8_t*)renderer->ui_pass_vertices + ui_bytes,
            renderer->ui_pass_rotmask_vertices,
            rotmask_bytes);
    }
    _Static_assert(sizeof(struct GLES2VertexUI) % 4u == 0u, "rotmask tail stays 4-byte aligned");
    /* Every earlier append into the 2D stream this frame came from an earlier
     * submit (or the immediate boot-bar path), each of which drew before
     * returning: the growth contract holds. */
    base_offset = gles2_ring_upload(renderer, renderer->ui_pass_vertices, total_bytes, true);
    rotmask_offset = base_offset + ui_bytes;
    renderer->ui_stat_upload_bytes += total_bytes;

    gles2_ui_apply_states(renderer);
    for( record_index = 0u; record_index < renderer->ui_pass_record_count; record_index++ )
    {
        const struct GLES2UIDrawRecord* record = &renderer->ui_pass_records[record_index];
        if( record->layout == GLES2_UI_RECORD_LAYOUT_ROTMASK )
        {
            gles2_use_program(renderer, &renderer->program_rotmask);
            if( !renderer->rotmask_projection_pushed )
            {
                glUniformMatrix4fv(
                    renderer->program_rotmask.u_matrix, 1, GL_FALSE, renderer->projection_2d);
                renderer->rotmask_projection_pushed = true;
            }
            glUniform1f(renderer->program_rotmask.u_mask_invert, record->mask_invert);
            gles2_set_blend(renderer, true);
            gles2_set_depth(renderer, false, false);
            gles2_set_scissor(renderer, record->scissor_enabled ? &record->scissor : NULL);
            /* Bound outright, not through the cache, as the immediate path
             * does: the two draws a frame do not earn a cache miss's risk. */
            renderer->bound_texture1 = 0u;
            gles2_bind_texture1(renderer, record->texture1);
            renderer->bound_texture0 = 0u;
            gles2_bind_texture0(renderer, record->texture0);
            gles2_bind_rotmask_stream(renderer, rotmask_offset);
            glDrawArrays(GL_TRIANGLES, (GLint)record->first, (GLsizei)record->count);
            renderer->ui_stat_draws_rotmask++;
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
            /* Debug only: glGetError is a pipeline drain (on a browser a
             * synchronous round trip to the GPU process). The Adreno 320 was
             * seen to DROP this draw silently when an attribute array its
             * program lacks was left enabled, which is what this catches. */
            if( renderer->debug )
                (void)gles2_check_error("rotmask draw");
            continue;
        }
        if( record->uses_sprite_atlas && !sprite_atlas_ok )
            continue;
        gles2_use_program(renderer, &renderer->program_ui);
        gles2_set_scissor(renderer, record->scissor_enabled ? &record->scissor : NULL);
        gles2_bind_texture0(
            renderer,
            record->uses_sprite_atlas ? renderer->ui_sprite_atlas_texture
                                      : (record->texture0 ? record->texture0 : renderer->white_texture));
        gles2_bind_texture1(renderer, record->texture1 ? record->texture1 : renderer->white_texture);
        gles2_bind_ui_stream(renderer, base_offset);
        glDrawArrays(GL_TRIANGLES, (GLint)record->first, (GLsizei)record->count);
        if( record->layout == GLES2_UI_RECORD_LAYOUT_WIDGET )
            renderer->ui_stat_draws_widget++;
        else
        {
            renderer->ui_stat_draws_batch++;
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_UI_BATCH_DRAWS, 1);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_2D_BATCH_FLUSHES, 1);
        }
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
    }
    /* Leave nothing on unit 1 that an unload can delete before the next draw. */
    gles2_bind_texture1(renderer, 0u);

    renderer->ui_pass_record_count = 0u;
    renderer->ui_pass_vertex_count = 0u;
    renderer->ui_pass_rotmask_count = 0u;
    renderer->ui_batch.first = 0u;
}

/*
 * End the open batch. Control arm: upload it and draw it now (the flush this
 * was). Deferred arm: record it as a range of the pass array; nothing
 * reaches GL until gles2_ui_submit.
 */
static void
gles2_ui_batch_close(struct ToriRS_GLES2* renderer)
{
    struct GLES2UIBatch* batch;
    GLuint texture0;
    uint32_t offset;

    assert(renderer);
    batch = &renderer->ui_batch;
    if( batch->vertex_count == 0u )
        return;
    if( renderer->lever_ui_defer )
    {
        struct GLES2UIDrawRecord* record = gles2_ui_pass_record_append(renderer);
        assert(batch->first + batch->vertex_count == renderer->ui_pass_vertex_count);
        record->layout = GLES2_UI_RECORD_LAYOUT_UI;
        record->first = batch->first;
        record->count = batch->vertex_count;
        record->texture1 = batch->texture1;
        record->uses_sprite_atlas = batch->uses_sprite_atlas ? 1u : 0u;
        record->scissor_enabled = batch->scissor_enabled ? 1u : 0u;
        record->scissor = batch->scissor;
        batch->vertex_count = 0u;
        batch->first = renderer->ui_pass_vertex_count;
        return;
    }
    /* Unit 0 is the sprite atlas whenever a quad in the batch samples it
     * (uploaded first if it changed); unit 1 the batch's own texture. A unit
     * a batch does not sample still has to hold a complete texture -- the
     * shader fetches both -- so white stands in. */
    texture0 = renderer->white_texture;
    if( batch->uses_sprite_atlas )
    {
        int64_t bytes = 0;
        if( trspk_atlas_is_dirty(&renderer->ui_sprite_atlas) ||
            !renderer->ui_sprite_atlas_allocated )
        {
            if( !renderer->ui_sprite_atlas_texture )
                renderer->ui_sprite_atlas_texture = gles2_ui_new_texture(renderer);
            if( !gles2_upload_ui_atlas_texture(renderer, &bytes) )
            {
                batch->vertex_count = 0u;
                return;
            }
        }
        texture0 = renderer->ui_sprite_atlas_texture;
    }

    gles2_ui_apply_states(renderer);
    gles2_set_scissor(renderer, batch->scissor_enabled ? &batch->scissor : NULL);
    gles2_bind_texture0(renderer, texture0);
    gles2_bind_texture1(renderer, batch->texture1 ? batch->texture1 : renderer->white_texture);
    /* Immediate path: the previous batch was drawn before this append. */
    offset = gles2_ring_upload(
        renderer, batch->vertices, batch->vertex_count * (uint32_t)sizeof(struct GLES2VertexUI),
        true);
    gles2_bind_ui_stream(renderer, offset);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch->vertex_count);
    renderer->ui_stat_draws_batch++;
    renderer->ui_stat_upload_bytes += batch->vertex_count * (uint32_t)sizeof(struct GLES2VertexUI);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_UI_BATCH_DRAWS, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_2D_BATCH_FLUSHES, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
    batch->vertex_count = 0u;
}

void
gles2_ui_flush(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    gles2_ui_batch_close(renderer);
    if( renderer->lever_ui_defer )
        gles2_ui_submit(renderer);
}

/*
 * Make room for a quad in the batch, ending the batch first when the quad
 * cannot join it: it names a unit-1 texture the batch already holds a
 * different one of, its scissor state differs, or the vertex cap is hit.
 * `texture1` is 0 for a quad that samples the atlas or nothing.
 */
static bool
gles2_ui_prepare_batch(
    struct ToriRS_GLES2* renderer,
    GLuint texture1,
    bool uses_sprite_atlas,
    const struct GLES2Rect* scissor,
    uint32_t additional_vertices)
{
    struct GLES2UIBatch* batch = &renderer->ui_batch;
    bool texture_changed =
        batch->vertex_count > 0u && texture1 && batch->texture1 && batch->texture1 != texture1;
    bool scissor_changed = batch->vertex_count > 0u &&
        (batch->scissor_enabled != (scissor != NULL) ||
         (scissor && !gles2_ui_rect_equal(&batch->scissor, scissor)));
    if( texture_changed || scissor_changed ||
        batch->vertex_count + additional_vertices > GLES2_UI_BATCH_MAX_VERTS )
    {
        if( batch->vertex_count > 0u )
        {
            if( texture_changed )
                renderer->ui_stat_break_texture++;
            else if( scissor_changed )
                renderer->ui_stat_break_scissor++;
            else
                renderer->ui_stat_break_overflow++;
        }
        gles2_ui_batch_close(renderer);
    }
    if( additional_vertices > GLES2_UI_BATCH_MAX_VERTS || !batch->vertices )
        return false;
    if( texture1 )
        batch->texture1 = texture1;
    if( uses_sprite_atlas )
        batch->uses_sprite_atlas = true;
    batch->scissor_enabled = scissor != NULL;
    if( scissor )
        batch->scissor = *scissor;
    return true;
}

static void
gles2_ui_append_quad_vertices(
    struct ToriRS_GLES2* renderer,
    GLuint texture,
    bool uses_sprite_atlas,
    const struct GLES2Rect* scissor,
    const float positions[4][2],
    const float uv[4][2],
    uint32_t rgba)
{
    static const uint8_t order[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    struct GLES2VertexUI* dst;
    uint32_t corner_index;
    float sel;
    GLuint texture1 = 0u;
    /* Which sampler the fragment multiplies by (see GLES2VertexUI.sel). A
     * quad with no uv, or the white texture, is a flat fill and joins any
     * batch; only a real unit-1 texture can end one. */
    if( uses_sprite_atlas )
        sel = 0.0f;
    else if( !uv || texture == renderer->white_texture || texture == 0u )
        sel = 2.0f;
    else
    {
        sel = 1.0f;
        texture1 = texture;
    }
    if( !gles2_ui_prepare_batch(renderer, texture1, uses_sprite_atlas, scissor, 6u) )
        return;
    if( renderer->lever_ui_defer )
    {
        /* Straight into the pass array; the open batch is its tail. */
        gles2_ui_pass_reserve_vertices(renderer, 6u);
        dst = &renderer->ui_pass_vertices[renderer->ui_pass_vertex_count];
        renderer->ui_pass_vertex_count += 6u;
    }
    else
        dst = &renderer->ui_batch.vertices[renderer->ui_batch.vertex_count];
    for( corner_index = 0u; corner_index < 6u; corner_index++ )
    {
        uint8_t corner = order[corner_index];
        dst[corner_index].x = positions[corner][0];
        dst[corner_index].y = positions[corner][1];
        dst[corner_index].w = 1.0f;
        dst[corner_index].u = uv ? uv[corner][0] : 0.0f;
        dst[corner_index].v = uv ? uv[corner][1] : 0.0f;
        dst[corner_index].rgba = rgba;
        dst[corner_index].sel = sel;
    }
    renderer->ui_batch.vertex_count += 6u;
}

static void
gles2_ui_append_quad(
    struct ToriRS_GLES2* renderer,
    GLuint texture,
    bool uses_sprite_atlas,
    const struct GLES2Rect* scissor,
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    uint32_t rgba)
{
    const float positions[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    const float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
    gles2_ui_append_quad_vertices(renderer, texture, uses_sprite_atlas, scissor, positions, uv, rgba);
}

/*
 * An axis-aligned quad clipped on the CPU to a logical rectangle -- the quad
 * is cut and its uv follows linearly -- so it carries no scissor state into
 * the batch. This is what most of the UI is: sprites, glyphs, fills, lines.
 * A quad wholly outside the clip appends nothing.
 */
static void
gles2_ui_append_quad_clipped(
    struct ToriRS_GLES2* renderer,
    GLuint texture,
    bool uses_sprite_atlas,
    const struct GLES2Clip* clip,
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    uint32_t rgba)
{
    float cx0;
    float cy0;
    float cx1;
    float cy1;
    assert(clip);
    if( x1 <= x0 || y1 <= y0 )
        return;
    cx0 = x0 < (float)clip->x0 ? (float)clip->x0 : x0;
    cy0 = y0 < (float)clip->y0 ? (float)clip->y0 : y0;
    cx1 = x1 > (float)clip->x1 ? (float)clip->x1 : x1;
    cy1 = y1 > (float)clip->y1 ? (float)clip->y1 : y1;
    if( cx1 <= cx0 || cy1 <= cy0 )
        return;
    if( cx0 != x0 || cx1 != x1 )
    {
        float const du = (u1 - u0) / (x1 - x0);
        float const nu0 = u0 + (cx0 - x0) * du;
        float const nu1 = u0 + (cx1 - x0) * du;
        u0 = nu0;
        u1 = nu1;
    }
    if( cy0 != y0 || cy1 != y1 )
    {
        float const dv = (v1 - v0) / (y1 - y0);
        float const nv0 = v0 + (cy0 - y0) * dv;
        float const nv1 = v0 + (cy1 - y0) * dv;
        v0 = nv0;
        v1 = nv1;
    }
    gles2_ui_append_quad(
        renderer, texture, uses_sprite_atlas, NULL, cx0, cy0, cx1, cy1, u0, v0, u1, v1, rgba);
}

/* ---- passes ------------------------------------------------------------------------ */

static void
gles2_ui_refresh_filters(struct ToriRS_GLES2* renderer)
{
    int font;
    uint32_t slot;
    if( !renderer->ui_filter_dirty )
        return;
    renderer->ui_filter_dirty = false;
    gles2_ui_set_texture_filter(renderer, renderer->ui_sprite_atlas_texture);
    gles2_ui_set_texture_filter(renderer, renderer->white_texture);
    for( font = 0; font < GLES2_UI_FONT_CAP; font++ )
        gles2_ui_set_texture_filter(renderer, renderer->ui_fonts[font].texture);
    for( slot = 0u; slot < renderer->ui_rotmask_count; slot++ )
    {
        gles2_ui_set_texture_filter(renderer, renderer->ui_rotmasks[slot].source_texture);
        gles2_ui_set_texture_filter(renderer, renderer->ui_rotmasks[slot].mask_texture);
    }
}

void
gles2_begin_2d(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    if( !renderer->scene || !renderer->ui_batch.vertices || !renderer->gl_context )
    {
        renderer->in2d = false;
        return;
    }
    glViewport(
        renderer->letterbox_x,
        renderer->letterbox_y,
        renderer->letterbox_width,
        renderer->letterbox_height);
    gles2_ui_refresh_filters(renderer);
    gles2_ui_apply_states(renderer);
    gles2_set_scissor(renderer, NULL);
    gles2_ui_batch_reset(renderer);
    renderer->in2d = true;
}

void
gles2_end_2d(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    if( !renderer->in2d )
        return;
    gles2_ui_flush(renderer);
    gles2_set_scissor(renderer, NULL);
    renderer->in2d = false;
}

void
gles2_draw_solid_rect(
    struct ToriRS_GLES2* renderer,
    int logical_x,
    int logical_y,
    int width,
    int height,
    uint32_t argb)
{
    assert(renderer);
    if( width <= 0 || height <= 0 || !renderer->ui_batch.vertices )
        return;
    gles2_ui_apply_states(renderer);
    gles2_ui_batch_reset(renderer);
    gles2_ui_append_quad(
        renderer,
        renderer->white_texture,
        false,
        NULL,
        (float)logical_x,
        (float)logical_y,
        (float)(logical_x + width),
        (float)(logical_y + height),
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        gles2_argb_to_rgba_bytes(argb));
    gles2_ui_flush(renderer);
}

/* ---- sprites --------------------------------------------------------------------- */

static void
gles2_ui_rotmask_release_slot(struct ToriRS_GLES2* renderer, struct GLES2UIRotmaskSlot* slot)
{
    assert(slot);
    gles2_ui_delete_texture(renderer, &slot->source_texture);
    gles2_ui_delete_texture(renderer, &slot->mask_texture);
    memset(slot, 0, sizeof(*slot));
}

static void
gles2_ui_rotmask_invalidate(struct ToriRS_GLES2* renderer, int scene_id)
{
    uint32_t slot_index;
    if( scene_id <= 0 )
        return;
    for( slot_index = 0u; slot_index < renderer->ui_rotmask_count; slot_index++ )
    {
        struct GLES2UIRotmaskSlot* slot = &renderer->ui_rotmasks[slot_index];
        if( slot->used && (slot->scene_id == scene_id || slot->mask_scene_id == scene_id) )
            gles2_ui_rotmask_release_slot(renderer, slot);
    }
}

static int
gles2_ui_sprite_slot_index(struct ToriRS_GLES2* renderer, int scene_id, bool create)
{
    int free_index = -1;
    int slot;
    for( slot = 0; slot < GLES2_UI_SPRITE_CAP; slot++ )
    {
        if( renderer->ui_sprite_slots[slot].scene_id == scene_id )
            return slot;
        if( free_index < 0 && renderer->ui_sprite_slots[slot].scene_id <= 0 )
            free_index = slot;
    }
    if( !create || free_index < 0 )
        return -1;
    renderer->ui_sprite_slots[free_index].scene_id = scene_id;
    renderer->ui_sprite_slots[free_index].count = 0;
    free(renderer->ui_sprite_slots[free_index].uvs);
    free(renderer->ui_sprite_slots[free_index].loaded);
    /* A slot handed to a different scene id keeps none of the old one's
     * atlas tiles: they belong to whatever sprite used to live here. */
    free(renderer->ui_sprite_slots[free_index].tiles);
    renderer->ui_sprite_slots[free_index].uvs = NULL;
    renderer->ui_sprite_slots[free_index].loaded = NULL;
    renderer->ui_sprite_slots[free_index].tiles = NULL;
    return free_index;
}

void
gles2_ui_sprite_invalidate(struct ToriRS_GLES2* renderer, int scene_id)
{
    int slot_index;
    uint32_t variant;
    assert(renderer);
    gles2_ui_flush(renderer);
    slot_index = gles2_ui_sprite_slot_index(renderer, scene_id, false);
    if( slot_index >= 0 )
    {
        struct GLES2UISpriteSlot* slot = &renderer->ui_sprite_slots[slot_index];
        /* Mark the pixels stale; keep the slot and its tiles. The next draw
         * re-uploads into the tile this slot already owns whenever the size is
         * unchanged, which for a sprite replaced in place it always is. */
        if( slot->loaded )
            memset(slot->loaded, 0, (size_t)slot->count * sizeof(*slot->loaded));
    }
    for( variant = 0u; variant < GLES2_UI_VARIANT_CAP; variant++ )
        if( renderer->ui_variants[variant].valid &&
            renderer->ui_variants[variant].scene_id == scene_id )
            renderer->ui_variants[variant].valid = false;
    gles2_ui_rotmask_invalidate(renderer, scene_id);
}

/*
 * Copy a sprite into the UI atlas with a one-texel repeated border, converting
 * to RGBA bytes on the way.
 *
 * The border keeps linear filtering's half-texel reach inside this sprite
 * rather than the next tightly packed entry. The conversion honours what the
 * producer DECLARED about the alpha byte (ToriDraw_Sprite::alpha_channel): a
 * real channel is kept, the legacy convention derives coverage from the colour
 * key. No per-pixel guessing.
 */
static bool
gles2_ui_upload_sprite_pixels(
    struct ToriRS_GLES2* renderer,
    const uint32_t* source,
    int width,
    int height,
    int alpha_channel,
    struct GLES2UISpriteTile* tile_io,
    float out_uv[4])
{
    struct TRSPK_AtlasTile tile;
    uint32_t* padded;
    uint32_t padded_width;
    uint32_t padded_height;
    int x;
    int y;
    bool inserted;

    assert(renderer);
    assert(source);
    assert(out_uv);
    if( width <= 0 || height <= 0 || !trspk_atlas_is_initialized(&renderer->ui_sprite_atlas) ||
        renderer->ui_sprite_atlas.width < 3u || renderer->ui_sprite_atlas.height < 3u ||
        (uint32_t)width > renderer->ui_sprite_atlas.width - 2u ||
        (uint32_t)height > renderer->ui_sprite_atlas.height - 2u )
        return false;
    padded_width = (uint32_t)width + 2u;
    padded_height = (uint32_t)height + 2u;
    padded = (uint32_t*)malloc((size_t)padded_width * (size_t)padded_height * sizeof(*padded));
    assert(padded);
    for( y = 0; y < (int)padded_height; y++ )
    {
        int source_y = gles2_clampi(y - 1, 0, height - 1);
        for( x = 0; x < (int)padded_width; x++ )
        {
            int source_x = gles2_clampi(x - 1, 0, width - 1);
            padded[(size_t)y * padded_width + (uint32_t)x] =
                source[(size_t)source_y * (size_t)width + (size_t)source_x];
        }
    }
    trspk_sprite_argb_to_rgba_for(
        alpha_channel, padded, padded, (size_t)padded_width * (size_t)padded_height);
    /* Reuse the tile this sprite already holds when the replacement is the
     * same size, and only ask the packer for a new one otherwise. */
    if( tile_io && tile_io->valid && tile_io->w == padded_width && tile_io->h == padded_height )
    {
        inserted = trspk_atlas_update_rect(
            &renderer->ui_sprite_atlas,
            tile_io->x,
            tile_io->y,
            (const uint8_t*)padded,
            padded_width * 4u,
            padded_width,
            padded_height);
        tile.x = tile_io->x;
        tile.y = tile_io->y;
    }
    else
        inserted = trspk_atlas_binpack_insert(
            &renderer->ui_sprite_atlas,
            (const uint8_t*)padded,
            padded_width * 4u,
            padded_width,
            padded_height,
            &tile);
    free(padded);
    if( !inserted )
        return false;
    if( tile_io )
    {
        tile_io->x = tile.x;
        tile_io->y = tile.y;
        tile_io->w = padded_width;
        tile_io->h = padded_height;
        tile_io->valid = 1u;
    }
    out_uv[0] = (float)(tile.x + 1u) / (float)renderer->ui_sprite_atlas.width;
    out_uv[1] = (float)(tile.y + 1u) / (float)renderer->ui_sprite_atlas.height;
    out_uv[2] = (float)(tile.x + 1u + (uint32_t)width) / (float)renderer->ui_sprite_atlas.width;
    out_uv[3] = (float)(tile.y + 1u + (uint32_t)height) / (float)renderer->ui_sprite_atlas.height;
    return true;
}

static bool
gles2_ui_sprite_ensure_base(
    struct ToriRS_GLES2* renderer,
    int scene_id,
    int atlas_index,
    struct ToriDraw_Sprite** out_sprite,
    float out_uv[4])
{
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Sprite* sprite;
    struct GLES2UISpriteSlot* slot;
    int count = 0;
    int slot_index;

    assert(renderer);
    assert(out_sprite);
    if( !renderer->scene || scene_id <= 0 )
        return false;
    sprites = ToriDraw_SceneSpriteGet(renderer->scene, scene_id, &count);
    if( !sprites || atlas_index < 0 || atlas_index >= count )
        return false;
    sprite = sprites[atlas_index];
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return false;
    slot_index = gles2_ui_sprite_slot_index(renderer, scene_id, true);
    if( slot_index < 0 )
        return false;
    slot = &renderer->ui_sprite_slots[slot_index];
    if( slot->count != count || !slot->tiles )
    {
        float* uvs = (float*)calloc((size_t)count * 4u, sizeof(float));
        uint8_t* loaded = (uint8_t*)calloc((size_t)count, sizeof(uint8_t));
        struct GLES2UISpriteTile* tiles =
            (struct GLES2UISpriteTile*)calloc((size_t)count, sizeof(*tiles));
        assert(uvs);
        assert(loaded);
        assert(tiles);
        free(slot->uvs);
        free(slot->loaded);
        free(slot->tiles);
        slot->uvs = uvs;
        slot->loaded = loaded;
        slot->tiles = tiles;
        slot->count = count;
    }
    if( !slot->loaded[atlas_index] )
    {
        float uv[4];
        if( !gles2_ui_upload_sprite_pixels(
                renderer,
                sprite->pixels_argb,
                sprite->width,
                sprite->height,
                sprite->alpha_channel,
                &slot->tiles[atlas_index],
                uv) )
            return false;
        memcpy(&slot->uvs[atlas_index * 4], uv, sizeof(uv));
        slot->loaded[atlas_index] = 1u;
    }
    *out_sprite = sprite;
    memcpy(out_uv, &slot->uvs[atlas_index * 4], sizeof(float) * 4u);
    return true;
}

static struct GLES2UISpriteVariant*
gles2_ui_sprite_variant(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Sprite* command,
    bool create)
{
    struct GLES2UISpriteVariant* free_variant = NULL;
    uint32_t index;
    for( index = 0u; index < GLES2_UI_VARIANT_CAP; index++ )
    {
        struct GLES2UISpriteVariant* variant = &renderer->ui_variants[index];
        if( !variant->valid )
        {
            if( !free_variant )
                free_variant = variant;
            continue;
        }
        if( variant->scene_id == command->scene_id && variant->atlas_index == command->atlas_index &&
            variant->outline == command->outline &&
            variant->graphic_shadow == command->graphic_shadow &&
            variant->angle == command->sprite_angle_r2pi65536 &&
            variant->flip_h == command->flip_h && variant->flip_v == command->flip_v &&
            variant->if3_transform == (uint8_t)(command->if3 && !command->tiled) )
            return variant;
    }
    if( !create || !free_variant )
        return NULL;
    memset(free_variant, 0, sizeof(*free_variant));
    free_variant->scene_id = command->scene_id;
    free_variant->atlas_index = command->atlas_index;
    free_variant->outline = command->outline;
    free_variant->graphic_shadow = command->graphic_shadow;
    free_variant->angle = command->sprite_angle_r2pi65536;
    free_variant->flip_h = command->flip_h;
    free_variant->flip_v = command->flip_v;
    free_variant->if3_transform = (uint8_t)(command->if3 && !command->tiled);
    return free_variant;
}

static uint32_t*
gles2_ui_clamp_sprite(
    const uint32_t* source,
    int source_width,
    int source_height,
    int offset_x,
    int offset_y,
    int width,
    int height)
{
    uint32_t* result;
    int x;
    int y;
    assert(source);
    if( source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0 )
        return NULL;
    result = (uint32_t*)calloc((size_t)width * (size_t)height, sizeof(*result));
    assert(result);
    for( y = 0; y < source_height; y++ )
    {
        int dst_y = y + offset_y;
        if( dst_y < 0 || dst_y >= height )
            continue;
        for( x = 0; x < source_width; x++ )
        {
            int dst_x = x + offset_x;
            if( dst_x >= 0 && dst_x < width )
                result[dst_y * width + dst_x] = source[y * source_width + x];
        }
    }
    return result;
}

static bool
gles2_ui_sprite_ensure_variant(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Sprite* command,
    struct ToriDraw_Sprite** out_sprite,
    float out_uv[4],
    int* out_offset_x,
    int* out_offset_y,
    int* out_width,
    int* out_height)
{
    struct ToriDraw_Sprite* sprite;
    struct GLES2UISpriteVariant* variant;
    float base_uv[4];
    uint32_t* pixels;
    int width;
    int height;
    int offset_x;
    int offset_y;
    int nominal_width;
    int nominal_height;

    if( !gles2_ui_sprite_ensure_base(
            renderer, command->scene_id, command->atlas_index, &sprite, base_uv) )
        return false;
    if( command->outline <= 0 && command->graphic_shadow == 0 && !command->flip_h &&
        !command->flip_v && command->sprite_angle_r2pi65536 == 0 )
    {
        *out_sprite = sprite;
        memcpy(out_uv, base_uv, sizeof(base_uv));
        *out_offset_x = sprite->crop_x;
        *out_offset_y = sprite->crop_y;
        *out_width = sprite->width;
        *out_height = sprite->height;
        return true;
    }
    variant = gles2_ui_sprite_variant(renderer, command, true);
    if( !variant )
        return false;
    if( variant->valid )
    {
        *out_sprite = sprite;
        out_uv[0] = variant->u0;
        out_uv[1] = variant->v0;
        out_uv[2] = variant->u1;
        out_uv[3] = variant->v1;
        *out_offset_x = variant->ox;
        *out_offset_y = variant->oy;
        *out_width = variant->width;
        *out_height = variant->height;
        return true;
    }
    width = sprite->width;
    height = sprite->height;
    offset_x = sprite->crop_x;
    offset_y = sprite->crop_y;
    nominal_width = sprite->width;
    nominal_height = sprite->height;
    pixels = (uint32_t*)malloc((size_t)width * (size_t)height * sizeof(*pixels));
    assert(pixels);
    memcpy(pixels, sprite->pixels_argb, (size_t)width * (size_t)height * sizeof(*pixels));
    if( command->outline > 0 )
    {
        int next_width = 0;
        int next_height = 0;
        uint32_t* next = ToriDraw_SpriteNewGraphicOutline(
            pixels, width, height, command->outline, &next_width, &next_height);
        if( next )
        {
            free(pixels);
            pixels = next;
            width = next_width;
            height = next_height;
        }
    }
    if( command->graphic_shadow != 0 )
    {
        int next_width = 0;
        int next_height = 0;
        uint32_t* next = ToriDraw_SpriteNewGraphicShadow(
            pixels, width, height, command->graphic_shadow, &next_width, &next_height);
        if( next )
        {
            free(pixels);
            pixels = next;
            width = next_width;
            height = next_height;
        }
    }
    if( command->if3 && !command->tiled )
    {
        ToriDraw_SpriteTransformPixels(&pixels, &width, &height, command->flip_h, command->flip_v, 0);
        if( offset_x != 0 || offset_y != 0 || width != nominal_width || height != nominal_height )
        {
            uint32_t* next = gles2_ui_clamp_sprite(
                pixels, width, height, offset_x, offset_y, nominal_width, nominal_height);
            if( next )
            {
                free(pixels);
                pixels = next;
                width = nominal_width;
                height = nominal_height;
                offset_x = 0;
                offset_y = 0;
            }
        }
        ToriDraw_SpriteTransformPixels(&pixels, &width, &height, 0, 0, command->sprite_angle_r2pi65536);
    }
    else
        ToriDraw_SpriteTransformPixels(
            &pixels, &width, &height, command->flip_h, command->flip_v,
            command->sprite_angle_r2pi65536);
    if( !gles2_ui_upload_sprite_pixels(
            renderer, pixels, width, height, sprite->alpha_channel, NULL, out_uv) )
    {
        free(pixels);
        return false;
    }
    free(pixels);
    variant->u0 = out_uv[0];
    variant->v0 = out_uv[1];
    variant->u1 = out_uv[2];
    variant->v1 = out_uv[3];
    variant->ox = offset_x;
    variant->oy = offset_y;
    variant->width = width;
    variant->height = height;
    variant->valid = true;
    *out_sprite = sprite;
    *out_offset_x = offset_x;
    *out_offset_y = offset_y;
    *out_width = width;
    *out_height = height;
    return true;
}

/* ---- rotated + masked chrome ------------------------------------------------------- */

static struct GLES2UIRotmaskSlot*
gles2_ui_rotmask_slot(
    struct ToriRS_GLES2* renderer,
    int scene_id,
    int atlas_index,
    int mask_scene_id,
    int mask_atlas_index,
    int width,
    int height,
    int source_width,
    int source_height)
{
    uint32_t free_index = UINT32_MAX;
    uint32_t slot_index;
    for( slot_index = 0u; slot_index < renderer->ui_rotmask_count; slot_index++ )
    {
        struct GLES2UIRotmaskSlot* slot = &renderer->ui_rotmasks[slot_index];
        if( slot->used && slot->scene_id == scene_id && slot->atlas_index == atlas_index &&
            slot->mask_scene_id == mask_scene_id && slot->mask_atlas_index == mask_atlas_index &&
            slot->width == width && slot->height == height && slot->source_width == source_width &&
            slot->source_height == source_height )
            return slot;
        if( free_index == UINT32_MAX && !slot->used )
            free_index = slot_index;
    }
    if( free_index == UINT32_MAX )
    {
        if( renderer->ui_rotmask_count == renderer->ui_rotmask_capacity )
        {
            uint32_t old_capacity = renderer->ui_rotmask_capacity;
            uint32_t new_capacity = old_capacity ? old_capacity * 2u : GLES2_UI_ROTMASK_INIT_CAP;
            struct GLES2UIRotmaskSlot* grown = (struct GLES2UIRotmaskSlot*)realloc(
                renderer->ui_rotmasks, (size_t)new_capacity * sizeof(*grown));
            assert(grown);
            memset(grown + old_capacity, 0, (size_t)(new_capacity - old_capacity) * sizeof(*grown));
            renderer->ui_rotmasks = grown;
            renderer->ui_rotmask_capacity = new_capacity;
        }
        free_index = renderer->ui_rotmask_count++;
    }
    gles2_ui_rotmask_release_slot(renderer, &renderer->ui_rotmasks[free_index]);
    renderer->ui_rotmasks[free_index].scene_id = scene_id;
    renderer->ui_rotmasks[free_index].atlas_index = atlas_index;
    renderer->ui_rotmasks[free_index].mask_scene_id = mask_scene_id;
    renderer->ui_rotmasks[free_index].mask_atlas_index = mask_atlas_index;
    renderer->ui_rotmasks[free_index].width = width;
    renderer->ui_rotmasks[free_index].height = height;
    renderer->ui_rotmasks[free_index].source_width = source_width;
    renderer->ui_rotmasks[free_index].source_height = source_height;
    renderer->ui_rotmasks[free_index].used = true;
    return &renderer->ui_rotmasks[free_index];
}

/* FNV-1a over the sprite's pixels and the fields the uploads read. The
 * sprites behind a rotmask slot (minimap area, compass) are rewritten in
 * place without a generation counter, so content identity is the only way to
 * tell an untouched frame from a real refresh. */
static uint32_t
gles2_ui_rotmask_content_hash(const struct ToriDraw_Sprite* sprite)
{
    uint32_t hash = 2166136261u;
    size_t count;
    size_t index = 0;
    assert(sprite);
    assert(sprite->pixels_argb);
    count = (size_t)sprite->width * (size_t)sprite->height;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    /* Four FNV-1a streams side by side, one per lane, folded at the end.
     * Not the same value as the scalar hash below -- it does not have to be:
     * the hash is only ever compared with its own earlier result. The
     * minimap's quarter-megabyte was a serial multiply chain of 65k steps
     * every eighth frame; four independent chains run at the multiplier's
     * throughput instead of its latency. */
    if( count >= 4u )
    {
        uint32x4_t lanes = vdupq_n_u32(2166136261u);
        uint32x4_t const prime = vdupq_n_u32(16777619u);
        for( ; index + 4u <= count; index += 4u )
            lanes = vmulq_u32(veorq_u32(lanes, vld1q_u32(sprite->pixels_argb + index)), prime);
        hash = (hash ^ vgetq_lane_u32(lanes, 0)) * 16777619u;
        hash = (hash ^ vgetq_lane_u32(lanes, 1)) * 16777619u;
        hash = (hash ^ vgetq_lane_u32(lanes, 2)) * 16777619u;
        hash = (hash ^ vgetq_lane_u32(lanes, 3)) * 16777619u;
    }
#endif
    for( ; index < count; index++ )
        hash = (hash ^ sprite->pixels_argb[index]) * 16777619u;
    hash = (hash ^ (uint32_t)sprite->crop_x) * 16777619u;
    hash = (hash ^ (uint32_t)sprite->crop_y) * 16777619u;
    hash = (hash ^ (uint32_t)sprite->width) * 16777619u;
    hash = (hash ^ (uint32_t)sprite->height) * 16777619u;
    return hash;
}

/* Whether this frame is one of the slot's turns to re-hash its sprite.
 * Hashing is a full read of the sprite -- the minimap's is a quarter of a
 * megabyte -- and its pixels change on a region load, not per frame, so each
 * slot checks every GLES2_ROTMASK_HASH_PERIOD frames, staggered so two slots
 * never both pay in the same frame. The first upload always hashes. */
static bool
gles2_ui_rotmask_hash_due(
    const struct ToriRS_GLES2* renderer,
    const struct GLES2UIRotmaskSlot* slot,
    bool have_texture)
{
    uint32_t slot_index = (uint32_t)(slot - renderer->ui_rotmasks);
    if( !have_texture )
        return true;
    return (((uint32_t)renderer->frame_clock + slot_index) % GLES2_ROTMASK_HASH_PERIOD) == 0u;
}

/*
 * Is a rotmask texture up to date? TORIRS_GLES2_ROTMASK_GEN (the default):
 * the producer says when it rewrote the pixels (ToriRS_GLES2_RotmaskSourceChanged
 * from app_rebuild_world_map), so a texture uploaded at the current generation
 * is current, and a frame costs one integer compare instead of a walk over
 * the 512x512 bake. Under TORIRS_GLES2_DEBUG the hash still runs on its old
 * schedule as a CROSS-CHECK: a hash that changes while the generation did not
 * names an in-place writer nobody told the renderer about, and is logged.
 * The control arm (=0) is the hash alone, as before.
 *
 * Returns true when the texture needs (re)uploading; `*out_hash` carries the
 * hash when one was computed (UINT32_MAX otherwise) so the upload can record
 * it.
 */
static bool
gles2_ui_rotmask_needs_upload(
    struct ToriRS_GLES2* renderer,
    struct GLES2UIRotmaskSlot* slot,
    const struct ToriDraw_Sprite* sprite,
    GLuint texture,
    uint32_t generation_uploaded,
    uint32_t hash_uploaded,
    bool hash_valid,
    const char* what,
    uint32_t* out_hash)
{
    *out_hash = UINT32_MAX;
    if( renderer->lever_rotmask_gen )
    {
        uint32_t generation = gles2_rotmask_source_generation();
        if( texture && generation_uploaded == generation )
        {
            if( renderer->debug && gles2_ui_rotmask_hash_due(renderer, slot, true) )
            {
                uint32_t hash = gles2_ui_rotmask_content_hash(sprite);
                if( hash_valid && hash != hash_uploaded )
                    TORIRS_ERR(
                        "GLES2: rotmask %s pixels changed with no generation bump "
                        "(scene %d): a writer is missing ToriRS_GLES2_RotmaskSourceChanged\n",
                        what,
                        slot->scene_id);
                *out_hash = hash;
            }
            return false;
        }
        if( renderer->debug )
            *out_hash = gles2_ui_rotmask_content_hash(sprite);
        return true;
    }
    if( !gles2_ui_rotmask_hash_due(renderer, slot, texture && hash_valid) )
        return false;
    *out_hash = gles2_ui_rotmask_content_hash(sprite);
    return !(texture && hash_valid && hash_uploaded == *out_hash);
}

static bool
gles2_ui_rotmask_upload_source(
    struct ToriRS_GLES2* renderer,
    struct GLES2UIRotmaskSlot* slot,
    const struct ToriDraw_Sprite* sprite)
{
    uint32_t content_hash;
    uint32_t* staged;
    bool fresh;
    int source_y;

    assert(renderer);
    assert(slot);
    assert(sprite);
    if( !sprite->pixels_argb )
        return false;
    if( !gles2_ui_rotmask_needs_upload(
            renderer,
            slot,
            sprite,
            slot->source_texture,
            slot->source_generation,
            slot->source_hash,
            slot->source_hash_valid,
            "source",
            &content_hash) )
    {
        if( content_hash != UINT32_MAX )
        {
            slot->source_hash = content_hash;
            slot->source_hash_valid = true;
        }
        return true;
    }
    /* Staged through the renderer's packed-row buffer, not a calloc per
     * upload; the rows are cleared because the crop can leave a border the
     * loop never writes. */
    gles2_reserve_upload_stage(
        renderer, (size_t)slot->source_width * (size_t)slot->source_height * sizeof(*staged));
    staged = (uint32_t*)renderer->upload_stage;
    memset(staged, 0, (size_t)slot->source_width * (size_t)slot->source_height * sizeof(*staged));
    for( source_y = 0; source_y < slot->source_height; source_y++ )
    {
        int sprite_y = sprite->crop_y + source_y;
        int source_x;
        if( sprite_y < 0 || sprite_y >= sprite->height )
            continue;
        for( source_x = 0; source_x < slot->source_width; source_x++ )
        {
            int sprite_x = sprite->crop_x + source_x;
            if( sprite_x >= 0 && sprite_x < sprite->width )
                staged[(size_t)source_y * slot->source_width + source_x] =
                    sprite->pixels_argb[(size_t)sprite_y * sprite->width + sprite_x];
        }
    }
    trspk_sprite_argb_to_rgba_for(
        sprite->alpha_channel, staged, staged, (size_t)slot->source_width * (size_t)slot->source_height);
    fresh = slot->source_texture == 0u;
    if( fresh )
        slot->source_texture = gles2_ui_new_texture(renderer);
    else
        gles2_bind_texture0(renderer, slot->source_texture);
    if( fresh )
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA, slot->source_width, slot->source_height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, staged);
    else
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, slot->source_width, slot->source_height, GL_RGBA,
            GL_UNSIGNED_BYTE, staged);
    slot->source_generation = gles2_rotmask_source_generation();
    if( content_hash != UINT32_MAX )
    {
        slot->source_hash = content_hash;
        slot->source_hash_valid = true;
    }
    return true;
}

static bool
gles2_ui_rotmask_upload_mask(
    struct ToriRS_GLES2* renderer,
    struct GLES2UIRotmaskSlot* slot,
    const struct ToriDraw_Sprite* mask)
{
    uint32_t content_hash;
    uint8_t* staged;
    bool fresh;
    int x;
    int y;

    assert(renderer);
    assert(slot);
    assert(mask);
    if( !mask->pixels_argb )
        return false;
    if( !gles2_ui_rotmask_needs_upload(
            renderer,
            slot,
            mask,
            slot->mask_texture,
            slot->mask_generation,
            slot->mask_hash,
            slot->mask_hash_valid,
            "mask",
            &content_hash) )
    {
        if( content_hash != UINT32_MAX )
        {
            slot->mask_hash = content_hash;
            slot->mask_hash_valid = true;
        }
        return true;
    }
    gles2_reserve_upload_stage(renderer, (size_t)slot->width * (size_t)slot->height);
    staged = renderer->upload_stage;
    memset(staged, 0, (size_t)slot->width * (size_t)slot->height);
    for( y = 0; y < mask->height; y++ )
    {
        int dst_y = mask->crop_y + y;
        if( dst_y < 0 || dst_y >= slot->height )
            continue;
        for( x = 0; x < mask->width; x++ )
        {
            int dst_x = mask->crop_x + x;
            if( dst_x >= 0 && dst_x < slot->width && mask->pixels_argb[(size_t)y * mask->width + x] != 0u )
                staged[(size_t)dst_y * slot->width + dst_x] = 0xffu;
        }
    }
    fresh = slot->mask_texture == 0u;
    if( fresh )
        slot->mask_texture = gles2_ui_new_texture(renderer);
    else
        gles2_bind_texture0(renderer, slot->mask_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if( fresh )
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_ALPHA, slot->width, slot->height, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
            staged);
    else
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, slot->width, slot->height, GL_ALPHA, GL_UNSIGNED_BYTE, staged);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    slot->mask_generation = gles2_rotmask_source_generation();
    if( content_hash != UINT32_MAX )
    {
        slot->mask_hash = content_hash;
        slot->mask_hash_valid = true;
    }
    return true;
}

/* The software rotated blit inverse-maps an axis-aligned destination box and
 * skips pixels whose source coordinate falls outside the sprite. Mapping the
 * whole box as one textured quad cannot express that skip: its corner UVs
 * leave the tile. The inverse map's valid domain is the forward-rotated
 * source rectangle, so that rectangle is drawn and scissored to the box. The
 * half-pixel terms are the algebraic inverse of trspk_sprite_local_to_uv(). */
static void
gles2_ui_rotated_sprite_quad(
    int dst_x,
    int dst_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int src_width,
    int src_height,
    int rotation_r2pi2048,
    const float tile_uv[4],
    float positions[4][2],
    float uv[4][2])
{
    const float source_corners[4][2] = {
        { -0.5f, -0.5f },
        { (float)src_width - 0.5f, -0.5f },
        { (float)src_width - 0.5f, (float)src_height - 0.5f },
        { -0.5f, (float)src_height - 0.5f },
    };
    const int angle = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    const float cosine = (float)ToriDraw_Cos(angle) / 65536.0f;
    const float sine = (float)ToriDraw_Sin(angle) / 65536.0f;
    int corner;
    for( corner = 0; corner < 4; corner++ )
    {
        float rel_x = source_corners[corner][0] - (float)src_anchor_x;
        float rel_y = source_corners[corner][1] - (float)src_anchor_y;
        positions[corner][0] =
            (float)dst_x + 0.5f + (float)dst_anchor_x + rel_x * cosine - rel_y * sine;
        positions[corner][1] =
            (float)dst_y + 0.5f + (float)dst_anchor_y + rel_x * sine + rel_y * cosine;
    }
    uv[0][0] = tile_uv[0];
    uv[0][1] = tile_uv[1];
    uv[1][0] = tile_uv[2];
    uv[1][1] = tile_uv[1];
    uv[2][0] = tile_uv[2];
    uv[2][1] = tile_uv[3];
    uv[3][0] = tile_uv[0];
    uv[3][1] = tile_uv[3];
}

static void
gles2_ui_draw_rotmask_native(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Sprite* command,
    const struct GLES2UIRotmaskSlot* slot,
    const struct GLES2Rect* scissor,
    uint32_t rgba)
{
    static const uint8_t order[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    static const float source_tile_uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    float positions[4][2];
    float source_uv[4][2];
    struct GLES2VertexRotmask corners[4];
    struct GLES2VertexRotmask vertices[6];
    uint32_t offset;
    int corner;

    assert(renderer);
    assert(command);
    assert(slot);
    assert(scissor);
    if( !slot->source_texture || !slot->mask_texture )
        return;
    gles2_ui_rotated_sprite_quad(
        command->x,
        command->y,
        command->dst_anchor_x,
        command->dst_anchor_y,
        command->src_anchor_x,
        command->src_anchor_y,
        slot->source_width,
        slot->source_height,
        command->rotation_r2pi2048,
        source_tile_uv,
        positions,
        source_uv);
    for( corner = 0; corner < 4; corner++ )
    {
        corners[corner].x = positions[corner][0];
        corners[corner].y = positions[corner][1];
        corners[corner].w = 1.0f;
        corners[corner].u = source_uv[corner][0];
        corners[corner].v = source_uv[corner][1];
        corners[corner].rgba = rgba;
        /* The stencil is axis aligned even though these vertices are not:
         * after the box scissor, every surviving pixel addresses the matching
         * mask pixel. */
        corners[corner].mask_u = (positions[corner][0] - (float)command->x) / (float)slot->width;
        corners[corner].mask_v = (positions[corner][1] - (float)command->y) / (float)slot->height;
    }
    for( corner = 0; corner < 6; corner++ )
        vertices[corner] = corners[order[corner]];

    if( renderer->lever_ui_defer )
    {
        /* Recorded in sequence with the batches around it and issued by
         * gles2_ui_submit. The source and mask textures were uploaded above
         * (at most once per frame per slot: the generation is constant for
         * the frame and the hash schedule visits a slot once a frame), so
         * the deferred draw samples what this record meant. */
        struct GLES2UIDrawRecord* record;
        gles2_ui_batch_close(renderer);
        gles2_ui_pass_reserve_rotmask_vertices(renderer, 6u);
        record = gles2_ui_pass_record_append(renderer);
        record->layout = GLES2_UI_RECORD_LAYOUT_ROTMASK;
        record->first = renderer->ui_pass_rotmask_count;
        record->count = 6u;
        record->texture0 = slot->source_texture;
        record->texture1 = slot->mask_texture;
        record->scissor_enabled = 1u;
        record->scissor = *scissor;
        record->mask_invert = command->mask_keep_opaque ? 0.0f : 1.0f;
        memcpy(
            renderer->ui_pass_rotmask_vertices + renderer->ui_pass_rotmask_count,
            vertices,
            sizeof(vertices));
        renderer->ui_pass_rotmask_count += 6u;
        return;
    }

    gles2_ui_flush(renderer);
    gles2_use_program(renderer, &renderer->program_rotmask);
    glUniformMatrix4fv(renderer->program_rotmask.u_matrix, 1, GL_FALSE, renderer->projection_2d);
    glUniform1f(renderer->program_rotmask.u_mask_invert, command->mask_keep_opaque ? 0.0f : 1.0f);
    gles2_set_blend(renderer, true);
    gles2_set_depth(renderer, false, false);
    gles2_set_scissor(renderer, scissor);
    /* Bound outright, not through the cache: the mask and source textures
     * are re-uploaded on the active unit above this, and two draws a frame
     * do not earn a cache miss's worth of risk. */
    renderer->bound_texture1 = 0u;
    gles2_bind_texture1(renderer, slot->mask_texture);
    renderer->bound_texture0 = 0u;
    gles2_bind_texture0(renderer, slot->source_texture);
    offset = gles2_ring_upload(renderer, vertices, (uint32_t)sizeof(vertices), true);
    gles2_bind_rotmask_stream(renderer, offset);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    /* No glGetError in the shipping path. It ran after every rotmask draw
     * once, and on a browser glGetError is a synchronous round trip to the
     * GPU process that drains the command queue -- twice a frame, for the
     * minimap and the compass. The init-time check covers the programs and
     * buffers this draw uses; a draw that fails afterwards is visible on
     * screen. Under TORIRS_GLES2_DEBUG it is worth the drain: the Adreno 320
     * drops this draw silently when an attribute array the program lacks is
     * left enabled. */
    if( renderer->debug )
        (void)gles2_check_error("rotmask draw");
    renderer->ui_stat_draws_rotmask++;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
    /* Leave nothing on unit 1 that an unload can delete before the next draw. */
    gles2_bind_texture1(renderer, 0u);
    gles2_ui_apply_states(renderer);
}

void
gles2_ui_draw_sprite(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Sprite* command)
{
    struct ToriDraw_Sprite* sprite = NULL;
    struct GLES2Clip clip;
    float uv[4];
    int width;
    int height;
    int offset_x;
    int offset_y;
    int alpha;
    uint32_t rgba;

    assert(renderer);
    assert(command);
    if( !renderer->in2d || !renderer->scene || command->scene_id <= 0 )
        return;
    if( !gles2_ui_clip_from(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &clip) )
        return;
    alpha = gles2_clampi(255 - command->trans, 0, 255);
    rgba = 0x00ffffffu | ((uint32_t)alpha << 24);
    if( command->rotated && command->mask_scene_id > 0 )
    {
        struct ToriDraw_Sprite** sprites;
        struct ToriDraw_Sprite** masks;
        struct ToriDraw_Sprite* mask;
        struct GLES2UIRotmaskSlot* slot;
        struct GLES2Rect box_scissor;
        int sprite_count = 0;
        int mask_count = 0;
        int dst_width;
        int dst_height;
        int source_width;
        int source_height;
        sprites = ToriDraw_SceneSpriteGet(renderer->scene, command->scene_id, &sprite_count);
        masks = ToriDraw_SceneSpriteGet(renderer->scene, command->mask_scene_id, &mask_count);
        if( !sprites || !masks || command->atlas_index < 0 || command->atlas_index >= sprite_count ||
            command->mask_atlas_index < 0 || command->mask_atlas_index >= mask_count )
            return;
        sprite = sprites[command->atlas_index];
        mask = masks[command->mask_atlas_index];
        if( !sprite || !mask || !sprite->pixels_argb || !mask->pixels_argb )
            return;
        dst_width = command->w > 0 ? command->w : sprite->width;
        dst_height = command->h > 0 ? command->h : sprite->height;
        source_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
        source_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
        if( dst_width <= 0 || dst_height <= 0 || source_width <= 0 || source_height <= 0 ||
            !gles2_ui_intersect_scissor_rect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                dst_width,
                dst_height,
                &box_scissor) )
            return;
        slot = gles2_ui_rotmask_slot(
            renderer,
            command->scene_id,
            command->atlas_index,
            command->mask_scene_id,
            command->mask_atlas_index,
            dst_width,
            dst_height,
            source_width,
            source_height);
        if( slot && gles2_ui_rotmask_upload_source(renderer, slot, sprite) &&
            gles2_ui_rotmask_upload_mask(renderer, slot, mask) )
            gles2_ui_draw_rotmask_native(renderer, command, slot, &box_scissor, rgba);
        return;
    }
    if( !gles2_ui_sprite_ensure_variant(
            renderer, command, &sprite, uv, &offset_x, &offset_y, &width, &height) )
        return;
    if( command->rotated )
    {
        struct GLES2Rect box_scissor;
        float positions[4][2];
        float rotated_uv[4][2];
        int dst_width = command->w > 0 ? command->w : width;
        int dst_height = command->h > 0 ? command->h : height;
        int src_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
        int src_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
        if( dst_width <= 0 || dst_height <= 0 || src_width <= 0 || src_height <= 0 ||
            !gles2_ui_intersect_scissor_rect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                dst_width,
                dst_height,
                &box_scissor) )
            return;
        gles2_ui_rotated_sprite_quad(
            command->x,
            command->y,
            command->dst_anchor_x,
            command->dst_anchor_y,
            command->src_anchor_x,
            command->src_anchor_y,
            src_width,
            src_height,
            command->rotation_r2pi2048,
            uv,
            positions,
            rotated_uv);
        gles2_ui_append_quad_vertices(
            renderer, renderer->ui_sprite_atlas_texture, true, &box_scissor, positions, rotated_uv,
            rgba);
        return;
    }
    if( command->tiled )
    {
        struct GLES2Clip tile_clip;
        int tile_width = width > 0 ? width : 1;
        int tile_height = height > 0 ? height : 1;
        int dst_width = command->w > 0 ? command->w : tile_width;
        int dst_height = command->h > 0 ? command->h : tile_height;
        int start_x;
        int start_y;
        int x;
        int y;
        if( !gles2_ui_clip_intersect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                dst_width,
                dst_height,
                &tile_clip) )
            return;
        trspk_sprite_tile_phase_origin(
            command->x,
            command->y,
            command->x + sprite->crop_x,
            command->y + sprite->crop_y,
            tile_width,
            tile_height,
            &start_x,
            &start_y);
        for( y = start_y; y < command->y + dst_height; y += tile_height )
            for( x = start_x; x < command->x + dst_width; x += tile_width )
                gles2_ui_append_quad_clipped(
                    renderer,
                    renderer->ui_sprite_atlas_texture,
                    true,
                    &tile_clip,
                    (float)x,
                    (float)y,
                    (float)(x + tile_width),
                    (float)(y + tile_height),
                    uv[0],
                    uv[1],
                    uv[2],
                    uv[3],
                    rgba);
        return;
    }
    if( command->if3 )
    {
        struct GLES2Clip box_clip;
        int nominal_width = sprite->width > 0 ? sprite->width : (width > 0 ? width : 1);
        int nominal_height = sprite->height > 0 ? sprite->height : (height > 0 ? height : 1);
        int box_width = command->w > 0 ? command->w : nominal_width;
        int box_height = command->h > 0 ? command->h : nominal_height;
        float scale_x = (float)box_width / (float)nominal_width;
        float scale_y = (float)box_height / (float)nominal_height;
        float x0 = (float)command->x + offset_x * scale_x;
        float y0 = (float)command->y + offset_y * scale_y;
        if( !gles2_ui_clip_intersect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                box_width,
                box_height,
                &box_clip) )
            return;
        gles2_ui_append_quad_clipped(
            renderer,
            renderer->ui_sprite_atlas_texture,
            true,
            &box_clip,
            x0,
            y0,
            x0 + width * scale_x,
            y0 + height * scale_y,
            uv[0],
            uv[1],
            uv[2],
            uv[3],
            rgba);
        return;
    }
    gles2_ui_append_quad_clipped(
        renderer,
        renderer->ui_sprite_atlas_texture,
        true,
        &clip,
        (float)(command->x + offset_x),
        (float)(command->y + offset_y),
        (float)(command->x + offset_x + width),
        (float)(command->y + offset_y + height),
        uv[0],
        uv[1],
        uv[2],
        uv[3],
        rgba);
}

/* ---- rectangles, lines, polygons ---------------------------------------------------- */

void
gles2_ui_draw_clear_rect(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_ClearRect* command)
{
    struct GLES2Clip clip;
    assert(renderer);
    assert(command);
    if( !renderer->in2d || command->w <= 0 || command->h <= 0 ||
        !gles2_ui_clip_from(renderer, 0, 0, renderer->width, renderer->height, &clip) )
        return;
    gles2_ui_append_quad_clipped(
        renderer,
        renderer->white_texture,
        false,
        &clip,
        (float)command->x,
        (float)command->y,
        (float)(command->x + command->w),
        (float)(command->y + command->h),
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        gles2_argb_to_rgba_bytes(TORIRS_GLES2_BG));
}

static uint32_t
gles2_ui_solid_rgba(int argb)
{
    uint32_t value = (uint32_t)argb;
    /* An ARGB word without an alpha byte is opaque: the convention every
     * fill-rect emitter in the tree writes against. */
    if( (value & 0xff000000u) == 0u )
        value |= 0xff000000u;
    return gles2_argb_to_rgba_bytes(value);
}

void
gles2_ui_draw_fill_rect(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_FillRect* command)
{
    struct GLES2Clip clip;
    uint32_t rgba;
    GLuint white;
    assert(renderer);
    assert(command);
    if( !renderer->in2d || command->w <= 0 || command->h <= 0 ||
        !gles2_ui_clip_from(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &clip) )
        return;
    rgba = gles2_ui_solid_rgba(command->argb);
    white = renderer->white_texture;
    if( command->filled )
    {
        gles2_ui_append_quad_clipped(
            renderer, white, false, &clip, (float)command->x, (float)command->y,
            (float)(command->x + command->w), (float)(command->y + command->h), 0, 0, 1, 1, rgba);
        return;
    }
    gles2_ui_append_quad_clipped(
        renderer, white, false, &clip, (float)command->x, (float)command->y,
        (float)(command->x + command->w), (float)(command->y + 1), 0, 0, 1, 1, rgba);
    if( command->h > 1 )
        gles2_ui_append_quad_clipped(
            renderer, white, false, &clip, (float)command->x,
            (float)(command->y + command->h - 1), (float)(command->x + command->w),
            (float)(command->y + command->h), 0, 0, 1, 1, rgba);
    if( command->h > 2 )
    {
        gles2_ui_append_quad_clipped(
            renderer, white, false, &clip, (float)command->x, (float)(command->y + 1),
            (float)(command->x + 1), (float)(command->y + command->h - 1), 0, 0, 1, 1, rgba);
        if( command->w > 1 )
            gles2_ui_append_quad_clipped(
                renderer, white, false, &clip, (float)(command->x + command->w - 1),
                (float)(command->y + 1), (float)(command->x + command->w),
                (float)(command->y + command->h - 1), 0, 0, 1, 1, rgba);
    }
}

void
gles2_ui_draw_line(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_Line* command)
{
    struct GLES2Rect scissor;
    float positions[4][2];
    float dx;
    float dy;
    float length;
    float half;
    float px;
    float py;
    float x0;
    float y0;
    float x1;
    float y1;
    int thickness;
    uint32_t rgba;

    assert(renderer);
    assert(command);
    if( !renderer->in2d ||
        !gles2_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    rgba = gles2_ui_solid_rgba(command->argb);
    thickness = command->line_width > 0 ? command->line_width : 1;
    x0 = (float)command->x;
    y0 = (float)(command->line_direction ? command->y + command->h : command->y);
    x1 = (float)(command->x + command->w);
    y1 = (float)(command->line_direction ? command->y : command->y + command->h);
    dx = x1 - x0;
    dy = y1 - y0;
    length = sqrtf(dx * dx + dy * dy);
    half = (float)thickness * 0.5f;
    if( length <= 0.0001f )
    {
        gles2_ui_append_quad(
            renderer, renderer->white_texture, false, &scissor, x0 - half, y0 - half, x0 + half,
            y0 + half, 0, 0, 1, 1, rgba);
        return;
    }
    /* A quad extruded along its own direction: a true diagonal, not the
     * axis-aligned L that turned sloped outlines into staircases. */
    px = -dy * half / length;
    py = dx * half / length;
    positions[0][0] = x0 + px;
    positions[0][1] = y0 + py;
    positions[1][0] = x1 + px;
    positions[1][1] = y1 + py;
    positions[2][0] = x1 - px;
    positions[2][1] = y1 - py;
    positions[3][0] = x0 - px;
    positions[3][1] = y0 - py;
    gles2_ui_append_quad_vertices(
        renderer, renderer->white_texture, false, &scissor, positions, NULL, rgba);
}

void
gles2_ui_polygon_begin(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_PolygonBegin* command)
{
    assert(renderer);
    assert(command);
    renderer->polygon = *command;
    renderer->polygon_open = 1;
    renderer->polygon_count = 0;
}

void
gles2_ui_polygon_point(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_PolygonPoint* command)
{
    assert(renderer);
    assert(command);
    if( !renderer->polygon_open || renderer->polygon_count >= TORIRS_POLYGON_MAX_POINTS )
        return;
    renderer->polygon_x[renderer->polygon_count] = command->x;
    renderer->polygon_y[renderer->polygon_count] = command->y;
    renderer->polygon_count++;
}

struct GLES2PolygonSpanContext
{
    struct ToriRS_GLES2* renderer;
    struct GLES2Rect scissor;
    bool scissor_set;
    uint32_t rgba;
};

/* The shared decomposition turns the polygon into horizontal runs and this
 * draws each as a one-pixel-tall quad, so all four backends run the SAME
 * geometry for a highlight. */
static void
gles2_ui_polygon_span(void* user_data, int x, int y, int count)
{
    struct GLES2PolygonSpanContext* context = (struct GLES2PolygonSpanContext*)user_data;
    if( count <= 0 )
        return;
    gles2_ui_append_quad(
        context->renderer,
        context->renderer->white_texture,
        false,
        context->scissor_set ? &context->scissor : NULL,
        (float)x,
        (float)y,
        (float)(x + count),
        (float)(y + 1),
        0,
        0,
        1,
        1,
        context->rgba);
}

void
gles2_ui_polygon_end(struct ToriRS_GLES2* renderer)
{
    struct GLES2PolygonSpanContext context;
    uint32_t argb;
    int alpha;
    assert(renderer);
    if( !renderer->polygon_open )
        return;
    renderer->polygon_open = 0;
    if( !renderer->in2d )
        return;
    context.renderer = renderer;
    context.scissor_set = renderer->polygon.scissor_w > 0 && renderer->polygon.scissor_h > 0 &&
        gles2_scissor_rect(
            renderer,
            renderer->polygon.scissor_x,
            renderer->polygon.scissor_y,
            renderer->polygon.scissor_w,
            renderer->polygon.scissor_h,
            &context.scissor);
    /* `trans` is the sprite path's sense: 0 opaque, 255 invisible. */
    alpha = 255 - (renderer->polygon.trans & 0xff);
    argb = ((uint32_t)renderer->polygon.argb & 0x00ffffffu) | ((uint32_t)alpha << 24);
    context.rgba = gles2_argb_to_rgba_bytes(argb);
    ToriRS_PolygonFillConvex(
        renderer->polygon_x,
        renderer->polygon_y,
        renderer->polygon_count,
        renderer->polygon.scissor_w > 0 ? renderer->polygon.scissor_x : 0,
        renderer->polygon.scissor_h > 0 ? renderer->polygon.scissor_y : 0,
        renderer->polygon.scissor_w > 0 ? renderer->polygon.scissor_w : 1 << 15,
        renderer->polygon.scissor_h > 0 ? renderer->polygon.scissor_h : 1 << 15,
        gles2_ui_polygon_span,
        &context);
}

/* ---- fonts ------------------------------------------------------------------------------ */

static int
gles2_ui_font_slot_index(struct ToriRS_GLES2* renderer, int font_id, bool create)
{
    int free_index = -1;
    int slot;
    if( font_id < 0 )
        return -1;
    for( slot = 0; slot < GLES2_UI_FONT_CAP; slot++ )
    {
        if( renderer->ui_fonts[slot].font_id == font_id )
            return slot;
        if( free_index < 0 && renderer->ui_fonts[slot].font_id < 0 )
            free_index = slot;
    }
    if( !create || free_index < 0 )
        return -1;
    renderer->ui_fonts[free_index].font_id = font_id;
    renderer->ui_fonts[free_index].font = NULL;
    renderer->ui_fonts[free_index].baked = false;
    return free_index;
}

static void
gles2_ui_font_release_slot(struct ToriRS_GLES2* renderer, struct GLES2UIFontSlot* slot)
{
    gles2_ui_delete_texture(renderer, &slot->texture);
    slot->texture_width = 0;
    slot->texture_height = 0;
    slot->baked = false;
    memset(slot->glyph_uv, 0, sizeof(slot->glyph_uv));
}

/*
 * Bake a font into one GL_LUMINANCE_ALPHA texture: luminance 255 everywhere,
 * alpha the glyph coverage. Sampling it through the texture * colour program
 * gives the glyph in the vertex colour with no text mode to switch.
 *
 * Each glyph gets a one-texel extruded border so linear UI scaling cannot
 * blend the glyph above or below across the quad (the horizontal streak).
 */
static bool
gles2_ui_bake_font(struct ToriRS_GLES2* renderer, struct GLES2UIFontSlot* slot)
{
    struct ToriDraw_Font* font;
    uint8_t* texels;
    int atlas_width = 0;
    int atlas_height = 0;
    int atlas_y = 0;
    int glyph;
    int y;

    assert(renderer);
    assert(slot);
    font = slot->font;
    if( !font )
        return false;
    if( slot->baked )
        return true;
    for( glyph = 0; glyph < TORIDRAW_FONT_GLYPH_COUNT; glyph++ )
    {
        if( font->glyph_width[glyph] > atlas_width )
            atlas_width = font->glyph_width[glyph];
        if( font->glyph_width[glyph] > 0 && font->glyph_height[glyph] > 0 && font->glyph_alpha[glyph] )
            atlas_height += font->glyph_height[glyph] + 2;
    }
    if( atlas_width <= 0 || atlas_height <= 0 )
        return false;
    atlas_width += 2;
    gles2_ui_flush(renderer);
    gles2_ui_font_release_slot(renderer, slot);
    texels = (uint8_t*)calloc((size_t)atlas_width * (size_t)atlas_height, 2u);
    assert(texels);
    for( glyph = 0; glyph < TORIDRAW_FONT_GLYPH_COUNT; glyph++ )
    {
        int glyph_width = font->glyph_width[glyph];
        int glyph_height = font->glyph_height[glyph];
        const uint8_t* alpha = font->glyph_alpha[glyph];
        int x;
        if( !alpha || glyph_width <= 0 || glyph_height <= 0 )
        {
            memset(&slot->glyph_uv[glyph * 4], 0, 4u * sizeof(float));
            continue;
        }
        for( y = -1; y <= glyph_height; y++ )
        {
            int source_y = gles2_clampi(y, 0, glyph_height - 1);
            for( x = -1; x <= glyph_width; x++ )
            {
                int source_x = gles2_clampi(x, 0, glyph_width - 1);
                uint8_t* texel =
                    texels + ((size_t)(atlas_y + y + 1) * (size_t)atlas_width + (size_t)(x + 1)) * 2u;
                texel[0] = 0xffu;
                texel[1] = alpha[(size_t)source_y * (size_t)glyph_width + (size_t)source_x];
            }
        }
        slot->glyph_uv[glyph * 4 + 0] = 1.0f / (float)atlas_width;
        slot->glyph_uv[glyph * 4 + 1] = (float)(atlas_y + 1) / (float)atlas_height;
        slot->glyph_uv[glyph * 4 + 2] = (float)(glyph_width + 1) / (float)atlas_width;
        slot->glyph_uv[glyph * 4 + 3] = (float)(atlas_y + glyph_height + 1) / (float)atlas_height;
        atlas_y += glyph_height + 2;
    }
    slot->texture = gles2_ui_new_texture(renderer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, atlas_width, atlas_height, 0, GL_LUMINANCE_ALPHA,
        GL_UNSIGNED_BYTE, texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    free(texels);
    slot->texture_width = atlas_width;
    slot->texture_height = atlas_height;
    slot->baked = true;
    return true;
}

static struct GLES2UIFontSlot*
gles2_ui_ensure_font(struct ToriRS_GLES2* renderer, int font_id)
{
    int slot_index = gles2_ui_font_slot_index(renderer, font_id, true);
    struct GLES2UIFontSlot* slot;
    if( slot_index < 0 )
        return NULL;
    slot = &renderer->ui_fonts[slot_index];
    if( !slot->font && renderer->scene )
        slot->font = ToriDraw_SceneFontGet(renderer->scene, font_id);
    if( slot->font && !slot->baked && !gles2_ui_bake_font(renderer, slot) )
        return NULL;
    return slot;
}

void
gles2_ui_font_load(struct ToriRS_GLES2* renderer, int font_id, struct ToriDraw_Font* font)
{
    int slot_index;
    assert(renderer);
    slot_index = gles2_ui_font_slot_index(renderer, font_id, true);
    if( slot_index < 0 )
        return;
    if( renderer->ui_fonts[slot_index].font != font )
    {
        gles2_ui_flush(renderer);
        gles2_ui_font_release_slot(renderer, &renderer->ui_fonts[slot_index]);
        renderer->ui_fonts[slot_index].font = font;
    }
}

void
gles2_ui_font_unload(struct ToriRS_GLES2* renderer, int font_id)
{
    int slot_index;
    assert(renderer);
    slot_index = gles2_ui_font_slot_index(renderer, font_id, false);
    if( slot_index < 0 )
        return;
    gles2_ui_flush(renderer);
    gles2_ui_font_release_slot(renderer, &renderer->ui_fonts[slot_index]);
    renderer->ui_fonts[slot_index].font = NULL;
    renderer->ui_fonts[slot_index].font_id = -1;
}

struct GLES2UIFontGlyphContext
{
    struct ToriRS_GLES2* renderer;
    struct GLES2UIFontSlot* slot;
    struct GLES2Clip clip;
    bool shadow;
};

static void
gles2_ui_font_glyph(
    void* opaque,
    struct ToriDraw_Font* font,
    int glyph_index,
    int x,
    int y,
    int color_rgb)
{
    struct GLES2UIFontGlyphContext* context = (struct GLES2UIFontGlyphContext*)opaque;
    struct GLES2UIFontSlot* slot = context->slot;
    int width;
    int height;
    (void)font;
    if( glyph_index < 0 || glyph_index >= TORIDRAW_FONT_GLYPH_COUNT )
        return;
    width = slot->font->glyph_width[glyph_index];
    height = slot->font->glyph_height[glyph_index];
    if( width <= 0 || height <= 0 )
        return;
    if( context->shadow )
        color_rgb = 0;
    gles2_ui_append_quad_clipped(
        context->renderer,
        slot->texture,
        false,
        &context->clip,
        (float)x,
        (float)y,
        (float)(x + width),
        (float)(y + height),
        slot->glyph_uv[glyph_index * 4 + 0],
        slot->glyph_uv[glyph_index * 4 + 1],
        slot->glyph_uv[glyph_index * 4 + 2],
        slot->glyph_uv[glyph_index * 4 + 3],
        gles2_ui_solid_rgba(color_rgb & 0x00ffffff));
}

static void
gles2_ui_draw_font_rules(
    struct ToriRS_GLES2* renderer,
    struct ToriDraw_Font* font,
    const struct GLES2Clip* clip,
    const char* text,
    int x,
    int y,
    bool center);

static void
gles2_ui_draw_font_text(
    struct ToriRS_GLES2* renderer,
    struct GLES2UIFontSlot* slot,
    const struct GLES2Clip* clip,
    const char* text,
    int x,
    int y,
    int color,
    bool shadow,
    bool center)
{
    struct GLES2UIFontGlyphContext context;
    if( !text || !text[0] || !slot || !slot->font || !slot->baked || !slot->texture )
        return;
    context.renderer = renderer;
    context.slot = slot;
    context.clip = *clip;
    context.shadow = shadow;
    ToriDraw_FontVisitGlyphsStyled(
        slot->font, text, x, y, color, center, gles2_ui_font_glyph, &context);
    if( !shadow )
        gles2_ui_draw_font_rules(renderer, slot->font, clip, text, x, y, center);
}

static bool
gles2_ui_char_equal_ignore_case(char a, char b)
{
    if( a >= 'A' && a <= 'Z' )
        a = (char)(a + ('a' - 'A'));
    if( b >= 'A' && b <= 'Z' )
        b = (char)(b + ('a' - 'A'));
    return a == b;
}

static bool
gles2_ui_font_line_break(const char* text, int* advance)
{
    assert(text);
    if( !text[0] )
        return false;
    if( text[0] == '\\' && text[1] == 'n' )
    {
        *advance = 2;
        return true;
    }
    if( text[0] == '\r' && text[1] == '\n' )
    {
        *advance = 2;
        return true;
    }
    if( text[0] == '\r' || text[0] == '\n' )
    {
        *advance = 1;
        return true;
    }
    /* Guard each successive byte: this runs at every byte, including a
     * trailing "<" or "<b", so a fixed-index probe must not read past NUL. */
    if( text[0] == '<' && text[1] != '\0' && text[2] != '\0' &&
        gles2_ui_char_equal_ignore_case(text[1], 'b') &&
        gles2_ui_char_equal_ignore_case(text[2], 'r') && text[3] == '>' )
    {
        *advance = 4;
        return true;
    }
    if( text[0] == '<' && text[1] != '\0' && text[2] != '\0' && text[3] != '\0' &&
        gles2_ui_char_equal_ignore_case(text[1], 'b') &&
        gles2_ui_char_equal_ignore_case(text[2], 'r') && text[3] == '/' && text[4] == '>' )
    {
        *advance = 5;
        return true;
    }
    return false;
}

static const char*
gles2_ui_font_next_line(const char* text, int* length, int* advance)
{
    const char* cursor = text;
    while( cursor[0] )
    {
        if( gles2_ui_font_line_break(cursor, advance) )
        {
            *length = (int)(cursor - text);
            return cursor;
        }
        cursor++;
    }
    *length = (int)(cursor - text);
    *advance = 0;
    return cursor;
}

static int
gles2_ui_font_measure_range(struct ToriDraw_Font* font, const char* text, int length)
{
    char buffer[4096];
    if( length <= 0 )
        return 0;
    if( length >= (int)sizeof(buffer) )
        length = (int)sizeof(buffer) - 1;
    memcpy(buffer, text, (size_t)length);
    buffer[length] = '\0';
    return ToriDraw2D_MeasureString(font, buffer);
}

static int
gles2_ui_font_char_advance(const struct ToriDraw_Font* font, unsigned char character)
{
    int glyph_index;
    int advance;
    if( character == ' ' || character == '|' )
        glyph_index = TORIDRAW_FONT_GLYPH_COUNT;
    else
    {
        glyph_index = (unsigned char)font->charcodeset[character];
        if( glyph_index > TORIDRAW_FONT_GLYPH_COUNT )
        {
            glyph_index = (unsigned char)font->charcodeset[(unsigned char)' '];
            if( glyph_index > TORIDRAW_FONT_GLYPH_COUNT )
                glyph_index = TORIDRAW_FONT_GLYPH_COUNT;
        }
    }
    advance = font->advance[glyph_index];
    return advance > 0 ? advance : 4;
}

static void
gles2_ui_append_rule_quad(
    struct ToriRS_GLES2* renderer,
    const struct GLES2Clip* clip,
    int x,
    int y,
    int advance,
    int rgb)
{
    gles2_ui_append_quad_clipped(
        renderer, renderer->white_texture, false, clip, (float)x, (float)y, (float)(x + advance),
        (float)(y + 1), 0, 0, 1, 1, gles2_ui_solid_rgba(rgb & 0x00ffffff));
}

/* Underline and strikethrough: the glyph pass draws glyphs only, so the rules
 * get their own walk over the same tokens. */
static void
gles2_ui_draw_font_rule_range(
    struct ToriRS_GLES2* renderer,
    struct ToriDraw_Font* font,
    const struct GLES2Clip* clip,
    const char* text,
    int length,
    int x,
    int y)
{
    int ascent = font->line_height > 0 ? font->line_height : 1;
    int underline_rgb = -1;
    int strike_rgb = -1;
    int underline_y = y + ascent + 1;
    int strike_y = y + (ascent * 7) / 10;
    int index = 0;
    while( index < length )
    {
        unsigned char emit_char = 0;
        int consumed = ToriDraw_FontMarkupTokenLength(text, length, index, &emit_char);
        int advance;
        if( consumed > 0 )
        {
            if( consumed == 4 && strncmp(text + index, "</u>", 4) == 0 )
                underline_rgb = -1;
            else if( consumed == 3 && strncmp(text + index, "<u>", 3) == 0 )
                underline_rgb = 0;
            else if( (consumed == 10 || consumed == 12) && strncmp(text + index, "<u=", 3) == 0 )
            {
                int parsed = ToriDraw_FontParseHexColor(text + index + 3, consumed - 4);
                if( parsed >= 0 )
                    underline_rgb = parsed;
            }
            else if( consumed == 6 && strncmp(text + index, "</str>", 6) == 0 )
                strike_rgb = -1;
            else if( consumed == 5 && strncmp(text + index, "<str>", 5) == 0 )
                strike_rgb = TORIDRAW_FONT_STRIKE_DEFAULT_RGB;
            else if( (consumed == 12 || consumed == 14) && strncmp(text + index, "<str=", 5) == 0 )
            {
                int parsed = ToriDraw_FontParseHexColor(text + index + 5, consumed - 6);
                if( parsed >= 0 )
                    strike_rgb = parsed;
            }
            index += consumed;
            if( emit_char == 0 )
                continue;
        }
        else
            emit_char = (unsigned char)text[index++];
        advance = gles2_ui_font_char_advance(font, emit_char);
        if( advance > 0 )
        {
            if( strike_rgb >= 0 )
                gles2_ui_append_rule_quad(renderer, clip, x, strike_y, advance, strike_rgb);
            if( underline_rgb >= 0 )
                gles2_ui_append_rule_quad(renderer, clip, x, underline_y, advance, underline_rgb);
        }
        x += advance;
    }
}

static void
gles2_ui_draw_font_rules(
    struct ToriRS_GLES2* renderer,
    struct ToriDraw_Font* font,
    const struct GLES2Clip* clip,
    const char* text,
    int x,
    int y,
    bool center)
{
    int line_step = font->line_height > 0 ? font->line_height : 1;
    const char* rest = text;
    for( ;; )
    {
        int length = 0;
        int advance = 0;
        int line_x = x;
        const char* break_at = gles2_ui_font_next_line(rest, &length, &advance);
        if( center && length > 0 )
            line_x -= gles2_ui_font_measure_range(font, rest, length) / 2;
        if( length > 0 )
            gles2_ui_draw_font_rule_range(renderer, font, clip, rest, length, line_x, y);
        if( advance == 0 )
            break;
        y += line_step;
        rest = break_at + advance;
    }
}

static void
gles2_ui_font_vertical_metrics(
    const struct ToriDraw_Font* font,
    int* ascent_out,
    int* descent_out)
{
    int fallback = font->line_height > 0 ? font->line_height : 1;
    int min_y = 0;
    int max_bottom = 0;
    bool any = false;
    int glyph;
    for( glyph = 0; glyph < TORIDRAW_FONT_GLYPH_COUNT; glyph++ )
    {
        int bottom;
        if( font->glyph_width[glyph] <= 0 || font->glyph_height[glyph] <= 0 || !font->glyph_alpha[glyph] )
            continue;
        bottom = font->offset_y[glyph] + font->glyph_height[glyph];
        if( !any || font->offset_y[glyph] < min_y )
            min_y = font->offset_y[glyph];
        if( !any || bottom > max_bottom )
            max_bottom = bottom;
        any = true;
    }
    if( !any )
    {
        *ascent_out = fallback;
        *descent_out = 0;
        return;
    }
    *ascent_out = fallback - min_y;
    *descent_out = max_bottom - fallback;
    if( *ascent_out <= 0 )
        *ascent_out = fallback;
    if( *descent_out < 0 )
        *descent_out = 0;
}

static bool
gles2_ui_font_append_line(
    const char* lines[],
    int lengths[],
    int* count,
    const char* text,
    int length)
{
    if( *count >= GLES2_UI_FONT_BOX_MAX_LINES )
        return false;
    lines[*count] = text;
    lengths[*count] = length;
    (*count)++;
    return true;
}

static bool
gles2_ui_font_segment_has_visible_content(const char* text, int length)
{
    int index;
    assert(text);
    if( length <= 0 )
        return false;
    for( index = 0; index < length; index++ )
    {
        unsigned char emit_char = 0;
        int consumed;
        if( text[index] == ' ' || text[index] == '|' )
            continue;
        consumed = ToriDraw_FontMarkupTokenLength(text, length, index, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char != 0 )
                return true;
            index += consumed - 1;
            continue;
        }
        return true;
    }
    return false;
}

static bool
gles2_ui_font_wrap_segment(
    struct ToriDraw_Font* font,
    const char* text,
    int length,
    int max_width,
    const char* lines[],
    int lengths[],
    int* count)
{
    int space_width = gles2_ui_font_measure_range(font, " ", 1);
    int current_start = -1;
    int current_length = 0;
    int current_width = 0;
    int word_start = 0;
    int index;
    if( length <= 0 )
        return gles2_ui_font_append_line(lines, lengths, count, text, 0);
    if( !gles2_ui_font_segment_has_visible_content(text, length) )
        return gles2_ui_font_append_line(lines, lengths, count, text, 0);
    for( index = 0; index <= length; index++ )
    {
        bool at_end = index == length;
        bool space = !at_end && (text[index] == ' ' || text[index] == '|');
        int word_length;
        int word_width;
        if( !at_end && !space )
            continue;
        word_length = index - word_start;
        if( word_length <= 0 )
        {
            word_start = at_end ? index : index + 1;
            continue;
        }
        word_width = gles2_ui_font_measure_range(font, text + word_start, word_length);
        if( current_length <= 0 )
        {
            current_start = word_start;
            current_length = word_length;
            current_width = word_width;
        }
        else if( current_width + space_width + word_width > max_width )
        {
            if( !gles2_ui_font_append_line(lines, lengths, count, text + current_start, current_length) )
                return false;
            current_start = word_start;
            current_length = word_length;
            current_width = word_width;
        }
        else
        {
            current_length = index - current_start;
            current_width += space_width + word_width;
        }
        word_start = at_end ? index : index + 1;
    }
    if( current_length > 0 )
        return gles2_ui_font_append_line(lines, lengths, count, text + current_start, current_length);
    return true;
}

static int
gles2_ui_font_collect_lines(
    struct ToriDraw_Font* font,
    const struct ToriRS_RenderCommand_Font* command,
    const char* lines[],
    int lengths[])
{
    const char* rest = command->text;
    int line_height = command->line_height > 0 ? command->line_height
                                               : (font->line_height > 0 ? font->line_height : 1);
    int ascent;
    int descent;
    int count = 0;
    bool wrap;
    gles2_ui_font_vertical_metrics(font, &ascent, &descent);
    wrap = command->w > 0 && command->h > 0 &&
        !(command->h < line_height + ascent + descent && command->h < line_height * 2);
    while( rest && rest[0] && count < GLES2_UI_FONT_BOX_MAX_LINES )
    {
        int length = 0;
        int advance = 0;
        const char* line_end = gles2_ui_font_next_line(rest, &length, &advance);
        if( wrap )
        {
            if( !gles2_ui_font_wrap_segment(
                    font, rest, length, command->w > 0 ? command->w : 1, lines, lengths, &count) )
                break;
        }
        else if( !gles2_ui_font_append_line(lines, lengths, &count, rest, length) )
            break;
        if( advance == 0 )
            break;
        rest = line_end + advance;
    }
    return count;
}

static void
gles2_ui_draw_font_range(
    struct ToriRS_GLES2* renderer,
    struct GLES2UIFontSlot* slot,
    const struct GLES2Clip* clip,
    const char* text,
    int length,
    int x,
    int y,
    int color,
    bool shadow)
{
    char buffer[4096];
    if( length <= 0 )
        return;
    if( length >= (int)sizeof(buffer) )
        length = (int)sizeof(buffer) - 1;
    memcpy(buffer, text, (size_t)length);
    buffer[length] = '\0';
    gles2_ui_draw_font_text(renderer, slot, clip, buffer, x, y, color, shadow, false);
}

void
gles2_ui_draw_font(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_Font* command)
{
    struct GLES2UIFontSlot* slot;
    struct ToriDraw_Font* font;
    struct GLES2Clip clip;

    assert(renderer);
    assert(command);
    if( !renderer->in2d || command->font_id < 0 || !command->text || !command->text[0] ||
        !gles2_ui_clip_from(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &clip) )
        return;
    slot = gles2_ui_ensure_font(renderer, command->font_id);
    font = slot ? slot->font : NULL;
    if( !font || !slot->baked || !ToriDraw_FontValidate(font) )
        return;
    if( command->baseline )
    {
        int y = command->y - font->line_height;
        bool center = command->center != 0;
        if( command->shadowed )
            gles2_ui_draw_font_text(
                renderer, slot, &clip, command->text, command->x + 1, y + 1, command->color,
                true, center);
        gles2_ui_draw_font_text(
            renderer, slot, &clip, command->text, command->x, y, command->color, false, center);
        return;
    }
    {
        const char* lines[GLES2_UI_FONT_BOX_MAX_LINES];
        int lengths[GLES2_UI_FONT_BOX_MAX_LINES];
        int count = gles2_ui_font_collect_lines(font, command, lines, lengths);
        int line_height = command->line_height > 0
            ? command->line_height
            : (font->line_height > 0 ? font->line_height : 1);
        int font_ascent = font->line_height > 0 ? font->line_height : line_height;
        int ascent;
        int descent;
        int logical_height;
        int first_baseline;
        int line;
        if( count <= 0 )
            return;
        gles2_ui_font_vertical_metrics(font, &ascent, &descent);
        logical_height = command->h > 0 ? command->h
                                        : line_height * (count - 1) + ascent + descent;
        first_baseline = ascent;
        if( command->y_align == 1 )
            first_baseline = ascent + (logical_height - ascent - descent - line_height * (count - 1)) / 2;
        else if( command->y_align == 2 )
            first_baseline = logical_height - descent - line_height * (count - 1);
        for( line = 0; line < count; line++ )
        {
            int x = command->x;
            int y;
            if( lengths[line] <= 0 )
                continue;
            if( command->center != 0 )
            {
                int text_width = gles2_ui_font_measure_range(font, lines[line], lengths[line]);
                if( command->center == 1 )
                    x += ((command->w > 0 ? command->w : 1) - text_width) / 2;
                else if( command->center == 2 )
                    x += (command->w > 0 ? command->w : 1) - text_width;
            }
            y = command->y + first_baseline + line * line_height - font_ascent;
            if( command->shadowed )
                gles2_ui_draw_font_range(
                    renderer, slot, &clip, lines[line], lengths[line], x + 1, y + 1,
                    command->color, true);
            gles2_ui_draw_font_range(
                renderer, slot, &clip, lines[line], lengths[line], x, y, command->color, false);
        }
    }
}

/* ---- widget models ----------------------------------------------------------------------- */

struct GLES2WidgetVertex
{
    float cx;
    float cy;
    float cz;
    float color[4];
    float u;
    float v;
};

static void
gles2_widget_model_transform_vertex(
    const struct ToriDraw_WidgetModelTransform* transform,
    int vx,
    int vy,
    int vz,
    int* out_x,
    int* out_y,
    int* out_z)
{
    int rotated;
    if( transform->var3 != 0 )
    {
        rotated = (vy * transform->var14 + vx * transform->var15) >> 16;
        vy = (vy * transform->var15 - vx * transform->var14) >> 16;
        vx = rotated;
    }
    if( transform->var10 != 0 )
    {
        rotated = (vy * transform->var11 - vz * transform->var10) >> 16;
        vz = (vy * transform->var10 + vz * transform->var11) >> 16;
        vy = rotated;
    }
    if( transform->var2 != 0 )
    {
        rotated = (vz * transform->var12 + vx * transform->var13) >> 16;
        vz = (vz * transform->var13 - vx * transform->var12) >> 16;
        vx = rotated;
    }
    vx += transform->var5;
    vy += transform->var6;
    vz += transform->var7;
    rotated = (vy * transform->var17 - vz * transform->var16) >> 16;
    vz = (vy * transform->var16 + vz * transform->var17) >> 16;
    *out_x = vx;
    *out_y = rotated;
    *out_z = vz;
}

/* Populate the same projected arrays the reference widget rasterizer uses, so
 * the scene's bounded face sort can order the faces. */
static bool
gles2_widget_model_project(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_ModelWidget* command,
    const struct ToriDraw_WidgetModelTransform* transform,
    float* out_origin_x,
    float* out_origin_y)
{
    struct ToriDraw_Model* model = command->model.u.model.model;
    struct ToriDraw_Scene* scene = renderer->scene;
    int vertex_count = model->vertex_count;
    int vertex;
    if( !model->vertices_x || !model->vertices_y || !model->vertices_z || !model->face_indices_a ||
        !model->face_indices_b || !model->face_indices_c || !model->face_colors_a ||
        !model->face_colors_b || !model->face_colors_c || vertex_count <= 0 ||
        vertex_count > scene->max_vertices || model->face_count <= 0 ||
        model->face_count > scene->max_faces )
        return false;
    scene->active_hnd = command->model;
    if( transform->orthographic )
    {
        int bounds_width = 0;
        int bounds_height = 0;
        int bounds_dx = 0;
        int bounds_dy = 0;
        int orthographic_scale = transform->zoom2d > 100 ? transform->zoom2d : 100;
        int64_t depth_sum = 0;
        int depth_mid;
        if( !ToriDraw_WidgetModelBounds(
                command->model, transform, &bounds_width, &bounds_height, &bounds_dx, &bounds_dy) )
            return false;
        *out_origin_x = (float)(command->x + command->w / 2 - bounds_width / 2);
        *out_origin_y = (float)(command->y + command->h / 2 - bounds_height / 2);
        for( vertex = 0; vertex < vertex_count; vertex++ )
        {
            int cx;
            int cy;
            int cz;
            gles2_widget_model_transform_vertex(
                transform, model->vertices_x[vertex], model->vertices_y[vertex],
                model->vertices_z[vertex], &cx, &cy, &cz);
            scene->orthographic_vertices_x[vertex] = cx;
            scene->orthographic_vertices_y[vertex] = cy;
            scene->orthographic_vertices_z[vertex] = cz;
            scene->screen_vertices_x[vertex] =
                (int)*out_origin_x + (int)((int64_t)cx * transform->zoom3d / orthographic_scale);
            scene->screen_vertices_y[vertex] =
                (int)*out_origin_y + (int)((int64_t)cy * transform->zoom3d / orthographic_scale);
            scene->screen_vertices_z[vertex] = cz;
            depth_sum += cz;
        }
        depth_mid = (int)(depth_sum / vertex_count);
        for( vertex = 0; vertex < vertex_count; vertex++ )
            scene->screen_vertices_z[vertex] -= depth_mid;
        return true;
    }
    else
    {
        int depth_mid = (transform->var6 * transform->var16 + transform->var7 * transform->var17) >> 16;
        int visible_vertices = 0;
        *out_origin_x = (float)(command->x + command->w / 2);
        *out_origin_y = (float)(command->y + command->h / 2);
        for( vertex = 0; vertex < vertex_count; vertex++ )
        {
            int cx;
            int cy;
            int cz;
            gles2_widget_model_transform_vertex(
                transform, model->vertices_x[vertex], model->vertices_y[vertex],
                model->vertices_z[vertex], &cx, &cy, &cz);
            scene->orthographic_vertices_x[vertex] = cx;
            scene->orthographic_vertices_y[vertex] = cy;
            scene->orthographic_vertices_z[vertex] = cz;
            scene->screen_vertices_z[vertex] = cz - depth_mid;
            if( (float)cz <= GLES2_WIDGET_MODEL_NEAR )
            {
                scene->screen_vertices_x[vertex] = -5000;
                scene->screen_vertices_y[vertex] = 0;
                continue;
            }
            scene->screen_vertices_x[vertex] =
                (int)*out_origin_x + (int)((int64_t)cx * transform->zoom3d / cz);
            scene->screen_vertices_y[vertex] =
                (int)*out_origin_y + (int)((int64_t)cy * transform->zoom3d / cz);
            visible_vertices++;
        }
        return visible_vertices > 0;
    }
}

static struct GLES2WidgetVertex
gles2_widget_vertex_lerp(
    const struct GLES2WidgetVertex* a,
    const struct GLES2WidgetVertex* b,
    float amount)
{
    struct GLES2WidgetVertex out;
    int channel;
    out.cx = a->cx + (b->cx - a->cx) * amount;
    out.cy = a->cy + (b->cy - a->cy) * amount;
    out.cz = a->cz + (b->cz - a->cz) * amount;
    for( channel = 0; channel < 4; channel++ )
        out.color[channel] = a->color[channel] + (b->color[channel] - a->color[channel]) * amount;
    out.u = a->u + (b->u - a->u) * amount;
    out.v = a->v + (b->v - a->v) * amount;
    return out;
}

static int
gles2_widget_model_clip_near(
    const struct GLES2WidgetVertex input[3],
    struct GLES2WidgetVertex output[4])
{
    int output_count = 0;
    int edge;
    for( edge = 0; edge < 3; edge++ )
    {
        const struct GLES2WidgetVertex* a = &input[edge];
        const struct GLES2WidgetVertex* b = &input[(edge + 1) % 3];
        bool a_inside = a->cz > GLES2_WIDGET_MODEL_NEAR;
        bool b_inside = b->cz > GLES2_WIDGET_MODEL_NEAR;
        if( a_inside )
            output[output_count++] = *a;
        if( a_inside != b_inside )
        {
            float amount = (GLES2_WIDGET_MODEL_NEAR - a->cz) / (b->cz - a->cz);
            output[output_count++] = gles2_widget_vertex_lerp(a, b, amount);
        }
    }
    return output_count;
}

static uint32_t
gles2_pack_float_rgba(const float color[4])
{
    uint32_t r = (uint32_t)gles2_clampi((int)(color[0] * 255.0f + 0.5f), 0, 255);
    uint32_t g = (uint32_t)gles2_clampi((int)(color[1] * 255.0f + 0.5f), 0, 255);
    uint32_t b = (uint32_t)gles2_clampi((int)(color[2] * 255.0f + 0.5f), 0, 255);
    uint32_t a = (uint32_t)gles2_clampi((int)(color[3] * 255.0f + 0.5f), 0, 255);
    return r | (g << 8) | (b << 16) | (a << 24);
}

static void
gles2_widget_model_output_vertex(
    const struct ToriDraw_WidgetModelTransform* transform,
    float origin_x,
    float origin_y,
    const struct GLES2WidgetVertex* source,
    struct GLES2VertexUI* out)
{
    if( transform->orthographic )
    {
        float scale = (float)(transform->zoom2d > 100 ? transform->zoom2d : 100);
        out->x = origin_x + source->cx * (float)transform->zoom3d / scale;
        out->y = origin_y + source->cy * (float)transform->zoom3d / scale;
        out->w = 1.0f;
    }
    else
    {
        out->x = origin_x + source->cx * (float)transform->zoom3d / source->cz;
        out->y = origin_y + source->cy * (float)transform->zoom3d / source->cz;
        /* The view depth: the UI program multiplies x and y back up by it, so
         * the divide the hardware performs is this one and the varyings come
         * out perspective-correct. */
        out->w = source->cz;
    }
    out->u = source->u;
    out->v = source->v;
    out->rgba = gles2_pack_float_rgba(source->color);
}

static void
gles2_reserve_widget_vertices(struct ToriRS_GLES2* renderer, uint32_t needed)
{
    uint32_t capacity;
    struct GLES2VertexUI* grown;
    if( needed <= renderer->widget_vertex_capacity )
        return;
    capacity = renderer->widget_vertex_capacity ? renderer->widget_vertex_capacity : 1024u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct GLES2VertexUI*)realloc(renderer->widget_vertices, (size_t)capacity * sizeof(*grown));
    assert(grown);
    renderer->widget_vertices = grown;
    renderer->widget_vertex_capacity = capacity;
}

static void
gles2_widget_flush_vertices(
    struct ToriRS_GLES2* renderer,
    const struct GLES2Rect* scissor,
    uint32_t vertex_count)
{
    uint32_t first = 0u;
    if( vertex_count == 0u )
        return;
    if( renderer->lever_ui_defer )
    {
        /* One record over the world atlas, appended to the pass array in
         * sequence; no size cap, since nothing indexes it. The open batch
         * was closed by the caller before the model was built. */
        struct GLES2UIDrawRecord* record;
        assert(renderer->ui_batch.vertex_count == 0u);
        gles2_ui_pass_reserve_vertices(renderer, vertex_count);
        memcpy(
            renderer->ui_pass_vertices + renderer->ui_pass_vertex_count,
            renderer->widget_vertices,
            (size_t)vertex_count * sizeof(struct GLES2VertexUI));
        record = gles2_ui_pass_record_append(renderer);
        record->layout = GLES2_UI_RECORD_LAYOUT_WIDGET;
        record->first = renderer->ui_pass_vertex_count;
        record->count = vertex_count;
        record->texture0 = renderer->atlas_texture;
        record->scissor_enabled = 1u;
        record->scissor = *scissor;
        renderer->ui_pass_vertex_count += vertex_count;
        return;
    }
    gles2_ui_apply_states(renderer);
    gles2_set_scissor(renderer, scissor);
    gles2_bind_texture0(renderer, renderer->atlas_texture);
    /* The ring holds four maximal UI batches; a widget model larger than one
     * batch goes up in pieces. */
    while( first < vertex_count )
    {
        uint32_t count = vertex_count - first;
        uint32_t offset;
        if( count > GLES2_UI_BATCH_MAX_VERTS )
            count = GLES2_UI_BATCH_MAX_VERTS - (GLES2_UI_BATCH_MAX_VERTS % 3u);
        /* Immediate path: each piece is drawn before the next append. */
        offset = gles2_ring_upload(
            renderer, renderer->widget_vertices + first, count * (uint32_t)sizeof(struct GLES2VertexUI),
            true);
        gles2_bind_ui_stream(renderer, offset);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);
        renderer->ui_stat_draws_widget++;
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
        first += count;
    }
}

/*
 * Widget models are drawn straight into the frame as sorted transient
 * triangles: one native pass, no canvas-sized raster, no per-frame texture
 * upload. Perspective faces are clipped at the legacy near plane before GL
 * receives them, and w carries the depth for perspective-correct UVs.
 *
 * Every face samples the world atlas -- untextured ones through the white
 * tile -- so the whole model is one texture binding and, unless it is larger
 * than a batch, one draw.
 */
void
gles2_ui_draw_model_widget(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_ModelWidget* command)
{
    struct ToriDraw_WidgetModelTransform transform;
    struct ToriDraw_Model* model;
    struct GLES2Rect scissor;
    const int* face_order;
    int sorted_face_count;
    float origin_x;
    float origin_y;
    uint32_t pending_vertices = 0u;
    int order_index;

    assert(renderer);
    assert(command);
    if( !renderer->in2d || !renderer->scene || !ToriDraw_ModelKindIsFull(command->model.kind) ||
        !(model = command->model.u.model.model) || command->w <= 0 || command->h <= 0 )
        return;
    if( !gles2_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    ToriDraw_WidgetModelTransformInit(
        &transform,
        command->model_zoom > 0 ? command->model_zoom : 2000,
        command->model_xan,
        command->model_yan,
        command->model_zan,
        command->model_x_offset,
        command->model_y_offset,
        command->model_center_y,
        command->model_orthog != 0,
        command->model_fixed_zoom != 0);
    if( !gles2_widget_model_project(renderer, command, &transform, &origin_x, &origin_y) )
        return;
    sorted_face_count =
        ToriDraw_RenderModel2SortFacesWithTable(command->model, renderer->scene, renderer->kernel);
    if( sorted_face_count <= 0 )
        return;
    face_order = ToriDraw_FaceOrder(renderer->scene);
    /* The open batch ends here so the model keeps its place in the
     * sequence; on the deferred arm that is a record, not a draw. */
    gles2_ui_batch_close(renderer);
    /* Every face's texture is reserved (and uploaded when present) before the
     * loop, so the atlas is pushed once rather than mid-model. */
    if( model->face_textures )
        for( order_index = 0; order_index < sorted_face_count; order_index++ )
        {
            int face = face_order[order_index];
            if( face >= 0 && face < model->face_count )
                (void)gles2_ensure_texture(renderer, (int)model->face_textures[face]);
        }
    if( !gles2_upload_atlas(renderer) )
        return;
    gles2_reserve_widget_vertices(renderer, (uint32_t)sorted_face_count * 6u);

    for( order_index = 0; order_index < sorted_face_count; order_index++ )
    {
        struct TRSPK_ToriDrawBakeFaceVerts face;
        struct GLES2WidgetVertex input[3];
        struct GLES2WidgetVertex clipped[4];
        const float* colors[3];
        float uv[3][2];
        int indices[3];
        int clipped_count;
        int face_index = face_order[order_index];
        int corner;
        int triangle;
        int slot = -1;
        if( face_index < 0 || face_index >= model->face_count )
            continue;
        indices[0] = (int)model->face_indices_a[face_index];
        indices[1] = (int)model->face_indices_b[face_index];
        indices[2] = (int)model->face_indices_c[face_index];
        if( indices[0] < 0 || indices[0] >= model->vertex_count || indices[1] < 0 ||
            indices[1] >= model->vertex_count || indices[2] < 0 || indices[2] >= model->vertex_count )
            continue;
        if( !trspk_toridraw_bake_face_handle(
                command->model,
                (uint32_t)face_index,
                &trspk_world_placement_identity,
                renderer->scene,
                true,
                TRSPK_BAKE_COLOR_FLOAT,
                &face) ||
            (face.color_a[3] <= (1.0f / 255.0f) && face.color_b[3] <= (1.0f / 255.0f) &&
             face.color_c[3] <= (1.0f / 255.0f)) )
            continue;
        if( face.tex_id >= 0 )
        {
            slot = renderer->tex_slot_of_id[face.tex_id < TORIDRAW_TEXTURE_ID_CAPACITY ? face.tex_id : 0];
            if( face.tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || slot < 0 )
                continue; /* the atlas could not take it: invisible, as on D3D9 */
        }
        colors[0] = face.color_a;
        colors[1] = face.color_b;
        colors[2] = face.color_c;
        gles2_map_atlas_uv(slot, face.uv.u1, face.uv.v1, &uv[0][0], &uv[0][1]);
        gles2_map_atlas_uv(slot, face.uv.u2, face.uv.v2, &uv[1][0], &uv[1][1]);
        gles2_map_atlas_uv(slot, face.uv.u3, face.uv.v3, &uv[2][0], &uv[2][1]);
        for( corner = 0; corner < 3; corner++ )
        {
            int channel;
            int vertex_index = indices[corner];
            input[corner].cx = (float)renderer->scene->orthographic_vertices_x[vertex_index];
            input[corner].cy = (float)renderer->scene->orthographic_vertices_y[vertex_index];
            input[corner].cz = (float)renderer->scene->orthographic_vertices_z[vertex_index];
            for( channel = 0; channel < 4; channel++ )
                input[corner].color[channel] = colors[corner][channel];
            input[corner].u = uv[corner][0];
            input[corner].v = uv[corner][1];
        }
        if( transform.orthographic )
        {
            clipped[0] = input[0];
            clipped[1] = input[1];
            clipped[2] = input[2];
            clipped_count = 3;
        }
        else
            clipped_count = gles2_widget_model_clip_near(input, clipped);
        for( triangle = 1; triangle + 1 < clipped_count; triangle++ )
        {
            const int polygon_indices[3] = { 0, triangle, triangle + 1 };
            gles2_reserve_widget_vertices(renderer, pending_vertices + 3u);
            for( corner = 0; corner < 3; corner++ )
                gles2_widget_model_output_vertex(
                    &transform,
                    origin_x,
                    origin_y,
                    &clipped[polygon_indices[corner]],
                    &renderer->widget_vertices[pending_vertices++]);
        }
    }
    gles2_widget_flush_vertices(renderer, &scissor, pending_vertices);
    gles2_ui_batch_reset(renderer);
}

/* ---- lifetime ------------------------------------------------------------------------ */

void
gles2_ui_init_state(struct ToriRS_GLES2* renderer)
{
    int font;
    assert(renderer);
    for( font = 0; font < GLES2_UI_FONT_CAP; font++ )
        renderer->ui_fonts[font].font_id = -1;
    renderer->ui_batch.vertices = (struct GLES2VertexUI*)malloc(
        GLES2_UI_BATCH_MAX_VERTS * sizeof(renderer->ui_batch.vertices[0]));
    assert(renderer->ui_batch.vertices);
    if( !trspk_atlas_init_binpack(
            &renderer->ui_sprite_atlas, GLES2_UI_ATLAS_DIM, GLES2_UI_ATLAS_DIM, 4u) )
        assert(!"the UI sprite atlas could not be allocated");
    gles2_ui_batch_reset(renderer);
}

bool
gles2_ui_create_gl(struct ToriRS_GLES2* renderer)
{
    static const uint32_t white_pixel = 0xffffffffu;
    assert(renderer);
    renderer->white_texture = gles2_ui_new_texture(renderer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
    /* The 2D stream buffers belong to the core's ui_stream set and rotate per
     * frame; nothing to allocate here. The sprite atlas texture is created by
     * the first upload: an untouched
     * sheet is 16 MB of GPU memory a title screen may never need. */
    renderer->ui_sprite_atlas_allocated = false;
    return glGetError() == GL_NO_ERROR;
}

void
gles2_ui_destroy_gl(struct ToriRS_GLES2* renderer)
{
    int font;
    uint32_t slot;
    assert(renderer);
    gles2_ui_delete_texture(renderer, &renderer->ui_sprite_atlas_texture);
    renderer->ui_sprite_atlas_allocated = false;
    gles2_ui_delete_texture(renderer, &renderer->white_texture);
    for( font = 0; font < GLES2_UI_FONT_CAP; font++ )
        gles2_ui_font_release_slot(renderer, &renderer->ui_fonts[font]);
    for( slot = 0u; slot < renderer->ui_rotmask_count; slot++ )
        gles2_ui_rotmask_release_slot(renderer, &renderer->ui_rotmasks[slot]);
}

void
gles2_ui_free(struct ToriRS_GLES2* renderer)
{
    int slot;
    assert(renderer);
    if( trspk_atlas_is_initialized(&renderer->ui_sprite_atlas) )
        trspk_atlas_free(&renderer->ui_sprite_atlas);
    for( slot = 0; slot < GLES2_UI_SPRITE_CAP; slot++ )
    {
        free(renderer->ui_sprite_slots[slot].uvs);
        free(renderer->ui_sprite_slots[slot].loaded);
        free(renderer->ui_sprite_slots[slot].tiles);
    }
    free(renderer->ui_rotmasks);
    free(renderer->ui_batch.vertices);
    free(renderer->widget_vertices);
    free(renderer->ui_pass_vertices);
    free(renderer->ui_pass_rotmask_vertices);
    free(renderer->ui_pass_records);
    renderer->ui_batch.vertices = NULL;
    renderer->widget_vertices = NULL;
    renderer->ui_pass_vertices = NULL;
    renderer->ui_pass_rotmask_vertices = NULL;
    renderer->ui_pass_records = NULL;
}

void
gles2_ui_report_memory(struct ToriRS_GLES2* renderer)
{
    int fonts = 0;
    uint64_t font_bytes = 0u;
    int slot;
    assert(renderer);
    for( slot = 0; slot < GLES2_UI_FONT_CAP; slot++ )
        if( renderer->ui_fonts[slot].baked )
        {
            fonts++;
            font_bytes += (uint64_t)renderer->ui_fonts[slot].texture_width *
                (uint64_t)renderer->ui_fonts[slot].texture_height * 2u;
        }
    TORIRS_LOG("gles2_mem: ui_batch_cpu          %10.2f MB\n"
               "gles2_mem: ui_stream_gpu         %10.2f MB (one of %u)\n"
               "gles2_mem: ui_fonts_gpu          %10.2f MB (%d baked)\n"
               "gles2_mem: ui_rotmask_slots      %10u\n",
        (double)GLES2_UI_BATCH_MAX_VERTS * sizeof(struct GLES2VertexUI) / 1048576.0,
        (double)renderer->ui_stream.capacities[renderer->frame_slot] / 1048576.0,
        GLES2_FRAMES_IN_FLIGHT,
        (double)font_bytes / 1048576.0,
        fonts,
        renderer->ui_rotmask_count);
}
