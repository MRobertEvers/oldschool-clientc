/*
 * ev_render — the viewer's render core.
 *
 * Links toridraw and nothing else: it takes a built model and a built animation
 * in ev_wire format (the server did the cache work), applies a frame,
 * rasterises with the software renderer, and leaves the image in a buffer.
 *
 * It lives apart from ev_wasm.c so the same code can run natively. That is not
 * tidiness — when the browser first showed an empty canvas there was no way to
 * tell a wasm problem from a projection problem, and `ev_server --rendertest`
 * answers that in one line by running this exact path outside the browser.
 */

#include "ev_render.h"

#include "ev_wire.h"

#include "toridraw.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EV_MAX_DIM 2048

static struct ToriDraw_Scene* g_scene = NULL;
static struct ToriDraw_Model* g_model = NULL;
static struct ToriDraw_Animation* g_anim = NULL;

/* The raster's own buffer, in toridraw's native pixel type. */
static toripixel_t* g_pixels = NULL;
/* What the page reads: the same image as RGBA bytes, which is what
 * ImageData wants. Kept separate rather than rasterising straight into RGBA
 * because the raster's blend maths is written against its own packing. */
static uint8_t* g_rgba = NULL;
static int g_pix_w = 0;
static int g_pix_h = 0;
static int g_last_cull = -1;

/** Background behind the model. Matches the page's panel colour so the canvas
 *  does not read as a hole when the model is small. */
#define EV_BG 0xFF141821u

void
ev_init(void)
{
    if( g_scene )
        return;
    ToriDraw_Init();
    g_scene = ToriDraw_SceneNew(0);
}

/* JS hands bytes over by writing into a block it asked for here. */
void*
ev_alloc(int size)
{
    return malloc((size_t)(size > 0 ? size : 1));
}

void
ev_release(void* p)
{
    free(p);
}

/**
 * Adopt a model. Returns its face count, or 0 when the blob did not parse —
 * which is a hard failure rather than an empty render, so the page can say so.
 */
int
ev_set_model(const uint8_t* data, int len)
{
    struct ToriDraw_Model* next = ev_wire_read_model(data, (size_t)(len > 0 ? len : 0));
    if( !next )
        return 0;

    ToriDraw_ModelFree(g_model);
    g_model = next;
    return g_model->face_count;
}

/** Adopt an animation. Returns its frame count, 0 when the blob did not parse. */
int
ev_set_anim(const uint8_t* data, int len)
{
    struct ToriDraw_Animation* next = ev_wire_read_anim(data, (size_t)(len > 0 ? len : 0));
    if( !next )
        return 0;

    ToriDraw_AnimationFree(g_anim);
    g_anim = next;
    return g_anim->frame_count;
}

/** Drop the animation, so the model renders in its bind pose. */
void
ev_clear_anim(void)
{
    ToriDraw_AnimationFree(g_anim);
    g_anim = NULL;
}

int
ev_frame_count(void)
{
    return g_anim ? g_anim->frame_count : 0;
}

/** A frame's own duration, in client ticks (20ms each). 0 when unknown. */
int
ev_frame_delay(int index)
{
    if( !g_anim || index < 0 || index >= g_anim->frame_count )
        return 0;

    /*
     * A skeletal animation has no `frames` array at all — it is a baked matrix
     * palette — so this must not index one. The client's own tick makes the
     * same split: its skeletal branch advances one baked frame per client tick
     * unconditionally (app.c's `anim_cycle++; if (anim_cycle >= 1)`), where the
     * classic branch consults each frame's own length.
     *
     * Reading `frames[index]` through a NULL pointer does not trap in wasm —
     * low linear memory is ordinary addressable memory — so it returned
     * whatever bytes happened to live there. The player multiplied that by the
     * tick length and stalled at whichever index landed on a large value, the
     * same index every time because the layout is deterministic. It looked like
     * the sequence ending at frame 37.
     */
    if( g_anim->skeletal || !g_anim->frames )
        return 1;

    return g_anim->frames[index].delay;
}

/*
 * Put the model in the pose for `frame`; -1 is the bind pose.
 *
 * Two kinds of animation, one frame index. A skeletal (Animaya) sequence has no
 * base and no frames — it is a baked matrix palette, and posing goes through the
 * model's per-vertex bone influences instead. Only this call branches;
 * everything around it, including how frames are stepped, is the same.
 *
 * The guards are not decoration. ToriDraw_ModelAnimateSkeletal asserts on a
 * model with no Animaya skin, and such a model cannot play a skeletal sequence
 * at all — the catalog's `animaya_skinned` column asks the same question ahead
 * of time.
 *
 * Exported because the self-test needs the same pose the renderer uses. A
 * second copy of this branch is how the self-test crashed on the first skeletal
 * npc it was pointed at: it called ToriDraw_ModelAnimateFrame unconditionally,
 * on an animation whose `base` is NULL.
 */
void
ev_pose(int frame)
{
    if( !g_model )
        return;

    ToriDraw_ModelAnimateReset(g_model);
    if( !g_anim || frame < 0 || frame >= g_anim->frame_count )
        return;

    if( g_anim->skeletal )
    {
        if( g_model->animaya_group_counts && g_model->animaya_groups &&
            g_model->animaya_scales && g_model->animaya_vertex_count > 0 &&
            frame < g_anim->skeletal->frame_count && g_anim->skeletal->matrices )
            ToriDraw_ModelAnimateSkeletal(g_model, g_anim->skeletal, frame);
    }
    else if( g_anim->base && g_anim->frames )
        ToriDraw_ModelAnimateFrame(g_model, g_anim->base, &g_anim->frames[frame]);
}

int
ev_pose_moved_vertices(int frame)
{
    if( !g_model || !g_model->original_vertices_x )
        return -1;

    ev_pose(frame);

    int moved = 0;
    for( int i = 0; i < g_model->vertex_count; i++ )
        if( g_model->vertices_x[i] != g_model->original_vertices_x[i] ||
            g_model->vertices_y[i] != g_model->original_vertices_y[i] ||
            g_model->vertices_z[i] != g_model->original_vertices_z[i] )
            moved++;
    return moved;
}

/** Whether the loaded animation is a skeletal (Animaya) one. */
int
ev_anim_is_skeletal(void)
{
    return g_anim && g_anim->skeletal ? 1 : 0;
}

/** Whether the loaded model carries an Animaya skin, i.e. can be posed
 *  skeletally at all. */
int
ev_model_has_animaya(void)
{
    return g_model && g_model->animaya_vertex_count > 0 && g_model->animaya_group_counts ? 1 : 0;
}

int
ev_model_vertex_count(void)
{
    return g_model ? g_model->vertex_count : 0;
}

static int
ensure_buffers(int w, int h)
{
    if( w <= 0 || h <= 0 || w > EV_MAX_DIM || h > EV_MAX_DIM )
        return 0;
    if( g_pix_w == w && g_pix_h == h && g_pixels && g_rgba )
        return 1;

    free(g_pixels);
    free(g_rgba);
    g_pixels = malloc((size_t)w * (size_t)h * sizeof(toripixel_t));
    g_rgba = malloc((size_t)w * (size_t)h * 4);
    if( !g_pixels || !g_rgba )
    {
        free(g_pixels);
        free(g_rgba);
        g_pixels = NULL;
        g_rgba = NULL;
        g_pix_w = 0;
        g_pix_h = 0;
        return 0;
    }
    g_pix_w = w;
    g_pix_h = h;
    return 1;
}

/**
 * Render one frame.
 *
 * `yaw` and `pitch` are in the client's 2048-per-turn units; `zoom` is the
 * orbit distance in world units. `frame` selects the animation frame, or -1 for
 * the bind pose.
 *
 * Returns a pointer to width*height RGBA bytes, or NULL. The buffer is reused
 * between calls, so the page must copy it into the canvas before calling again.
 */
uint8_t*
ev_render(int width, int height, int yaw, int pitch, int zoom, int frame)
{
    if( !g_scene || !g_model || !ensure_buffers(width, height) )
        return NULL;

    /*
     * Reset before applying, every frame.
     *
     * Frames are absolute poses expressed as transforms from the bind pose, not
     * deltas from the previous frame. Without the reset each frame compounds on
     * the last and the model tears itself apart over a few seconds — slowly
     * enough to look like a decode bug rather than a missing call.
     */
    ev_pose(frame);

    for( int i = 0; i < width * height; i++ )
        g_pixels[i] = (toripixel_t)EV_BG;

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = g_model;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = width;
    view_port.height = height;
    view_port.stride = width;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = width;
    view_port.clip_bottom = height;
    view_port.x_center = width / 2;
    view_port.y_center = height / 2;

    struct ToriDraw_Camera camera = { 0 };
    camera.pitch = pitch & 2047;
    camera.yaw = 0;
    camera.roll = 0;
    camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    camera.proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
    /*
     * 1, not the world's 50.
     *
     * 50 is the scene near plane — Client-TS's `midZ - radiusZ <= 50` — and it
     * is right for a camera looking across a map. This camera orbits a single
     * model at roughly its own height, so a 50-unit near plane sits inside big
     * models and clips their nearest faces away. `ToriDraw_SpriteNewFromModelRaster`
     * uses 1 for the same reason: it is a close-up preview, and so is this.
     */
    camera.near_plane_z = 1;

    /*
     * The same framing ToriDraw_ModelSprite uses: a pitched orbit, with the
     * model lifted by half its own height. RS models grow along -y from a floor
     * at y=0, so without the lift an npc hangs off the top of the canvas.
     *
     * The bounds are read after the pose is applied, so a crouched or reared
     * frame stays centred instead of drifting.
     */
    int sin_pitch = (ToriDraw_Sin(camera.pitch) * zoom) >> 16;
    int cos_pitch = (ToriDraw_Cos(camera.pitch) * zoom) >> 16;

    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_height = bounds ? (bounds->max_y - bounds->min_y) : 0;

    /* Same placement as ToriDraw_SpriteNewFromModelRaster, minus its widget
     * term: that path blits into a widget rect and offsets by the widget's own
     * height, where this one centres in the viewport via y_center. */
    struct ToriDraw_Position position = { 0 };
    position.x = 0;
    position.y = sin_pitch + (model_height / 2);
    position.z = cos_pitch;
    position.yaw = yaw & 2047;

    g_last_cull = ToriDraw_RenderModel1Project(hnd, g_scene, &position, &view_port, &camera);
    if( g_last_cull == TORIDRAW_CULL_VISIBLE )
    {
        ToriDraw_RenderModel2SortFaces(hnd, g_scene);
        ToriDraw_RenderModel3Raster(g_scene, &view_port, &camera, g_pixels, false);
    }

    /* toripixel_t is 0x00RRGGBB; the canvas wants RGBA bytes and every pixel is
     * opaque because the background was painted first. */
    for( int i = 0; i < width * height; i++ )
    {
        uint32_t p = (uint32_t)g_pixels[i];
        g_rgba[i * 4 + 0] = (uint8_t)((p >> 16) & 0xFF);
        g_rgba[i * 4 + 1] = (uint8_t)((p >> 8) & 0xFF);
        g_rgba[i * 4 + 2] = (uint8_t)(p & 0xFF);
        g_rgba[i * 4 + 3] = 0xFF;
    }

    return g_rgba;
}

/** The model's height in world units, so the page can pick a sane zoom. */
int
ev_model_height(void)
{
    if( !g_model )
        return 0;
    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = g_model;
    struct ToriDraw_BoundsCylinder* b = ToriDraw_ModelGetBoundsCylinder(hnd);
    return b ? (b->max_y - b->min_y) : 0;
}

int
ev_last_cull(void)
{
    return g_last_cull;
}
