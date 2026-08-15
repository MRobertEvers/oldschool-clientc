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
#include <assert.h>

#include "ev_wire.h"

#include "toridraw.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EV_MAX_DIM 2048

static struct ToriDraw_Scene* g_scene = NULL;
static struct ToriDraw_Model* g_model = NULL;

/*
 * The HD model, when one has been adopted through ev_set_model_hd.
 *
 * Kept separate from g_model rather than replacing it: an HD model is a
 * different handle kind, it is not animated here (a file picked off disk has no
 * sequence), and the whole npc/player flow above must keep working untouched
 * while an HD model is on screen. When this is set it is what draws.
 */
static struct ToriDraw_ModelHD* g_model_hd = NULL;
static struct ToriDraw_HDRenderStats g_hd_stats;

/*
 * A placeholder material, off by default.
 *
 * A bare model file describes no textures, so the faithful answer is to draw
 * every textured face as flat colour — which is what happens with no material
 * table. But then the mapped kernels never run, and the routing readout the
 * whole view exists for reads texcube=0 no matter what the file contains.
 *
 * So this supplies a synthetic checkerboard for every texture id the model
 * names. It is a lie about the ASSET and it is labelled as one in the page; it
 * is the truth about the ROUTING, which is the question being asked.
 */
static int g_hd_placeholder = 0;
static int g_hd_tex[128 * 128];
static struct ToriDraw_HDMaterial* g_hd_mats = NULL;
static int g_hd_mat_count = 0;

/*
 * Real textures, as shipped by the server (ev_textures.c bakes them out of the
 * active cache; the wasm half has no cache of its own).
 *
 * Kept here as well as in the scene's texture map because the two render paths
 * read from different places: the classic raster looks textures up on the
 * scene, and ToriDraw_RenderHD takes a material table by argument. Both are
 * built from the same texel buffers, and this array owns them.
 */
struct EV_LoadedTexture
{
    int id;
    int* texels; /* owned */
    int size;
    int gate;
    int clamp_s;
    int clamp_t;
};
static struct EV_LoadedTexture* g_textures = NULL;
static int g_texture_count = 0;

static void
hd_materials_build(void);

static void
hd_placeholder_build(void)
{
    free(g_hd_mats);
    g_hd_mats = NULL;
    g_hd_mat_count = 0;

    /* Real textures win. The placeholder answers "which kernel did this face
     * reach"; once the actual texels are here that question is answered by the
     * thing itself. */
    if( g_texture_count > 0 )
    {
        hd_materials_build();
        return;
    }
    if( !g_model_hd || !g_hd_placeholder )
        return;

    for( int v = 0; v < 128; v++ )
        for( int u = 0; u < 128; u++ )
            g_hd_tex[u + v * 128] =
                (int)(0xFF000000u |
                      ((((u >> 4) ^ (v >> 4)) & 1) ? 0x00C08040u : 0x004080C0u));

    int maxid = 0;
    for( int f = 0; f < g_model_hd->base.face_count; f++ )
    {
        int id = g_model_hd->base.face_textures ? g_model_hd->base.face_textures[f] : -1;
        if( id > maxid )
            maxid = id;
    }
    if( maxid < 0 )
        return;

    g_hd_mats = (struct ToriDraw_HDMaterial*)calloc((size_t)maxid + 1, sizeof(*g_hd_mats));
    assert(g_hd_mats);
    for( int i = 0; i <= maxid; i++ )
    {
        g_hd_mats[i].texels = g_hd_tex;
        g_hd_mats[i].width = 128;
        g_hd_mats[i].gate = TORIDRAW_HD_GATE_OPAQUE;
    }
    g_hd_mat_count = maxid + 1;
}

/*
 * Build the HD material table from the loaded textures.
 *
 * Indexed by texture id directly, because that is what ToriDraw_HDMaterials is
 * — a flat array the raster indexes with the face's id. Ids the cache did not
 * have stay zeroed, and a zero `texels` is what makes the kernel fall back to
 * flat shading rather than sample garbage.
 */
static void
hd_materials_build(void)
{
    int maxid = -1;

    free(g_hd_mats);
    g_hd_mats = NULL;
    g_hd_mat_count = 0;

    for( int i = 0; i < g_texture_count; i++ )
        if( g_textures[i].id > maxid )
            maxid = g_textures[i].id;
    if( maxid < 0 )
        return;

    g_hd_mats = (struct ToriDraw_HDMaterial*)calloc((size_t)maxid + 1, sizeof(*g_hd_mats));
    assert(g_hd_mats);

    for( int i = 0; i < g_texture_count; i++ )
    {
        struct EV_LoadedTexture* t = &g_textures[i];
        struct ToriDraw_HDMaterial* m = &g_hd_mats[t->id];
        m->texels = t->texels;
        m->width = t->size;
        /*
         * The gate is the material's, decided server-side. It used to be
         * derived here from "does any texel have alpha < 255", which is a
         * property of the pixels and not a statement about the surface: a
         * procedural texture carries partial alpha as an ordinary intermediate,
         * so that rule put nearly every face on the blend path and every solid
         * rock face composited against the background as a dark smear.
         */
        m->gate = t->gate;
        m->clamp_s = t->clamp_s;
        m->clamp_t = t->clamp_t;
    }
    g_hd_mat_count = maxid + 1;
}

static void
ev_clear_textures(void)
{
    for( int i = 0; i < g_texture_count; i++ )
    {
        if( g_scene && g_textures[i].id >= 0 &&
            g_textures[i].id < TORIDRAW_TEXTURE_ID_CAPACITY )
        {
            /* The scene's map frees whatever it holds, and it holds wrappers
             * pointing at our texels — so drop its reference before we free
             * the pixels underneath it. */
            ToriDraw_TextureMapSet(
                &ToriDraw_SceneTexState(g_scene)->texture_map, g_textures[i].id, NULL);
        }
        free(g_textures[i].texels);
    }
    free(g_textures);
    g_textures = NULL;
    g_texture_count = 0;
}

/**
 * Install a texture set (an EVT1 blob) for both render paths.
 *
 * Replaces whatever was loaded: a texture set belongs to a cache, and mixing
 * two caches' ids would put one era's stone on another era's wall.
 */
int
ev_set_textures(const uint8_t* data, int len)
{
    int count = ev_wire_texture_count(data, (size_t)(len > 0 ? len : 0));

    ev_clear_textures();
    if( count <= 0 )
    {
        hd_placeholder_build();
        return 0;
    }

    g_textures = (struct EV_LoadedTexture*)calloc((size_t)count, sizeof(*g_textures));
    assert(g_textures);

    for( int i = 0; i < count; i++ )
    {
        struct EV_WireTexture wt;
        struct EV_LoadedTexture* slot;
        size_t texel_count;

        if( !ev_wire_read_texture(data, (size_t)len, i, &wt) )
            break;

        texel_count = (size_t)wt.size * (size_t)wt.size;
        slot = &g_textures[g_texture_count];
        slot->texels = (int*)malloc(texel_count * sizeof(*slot->texels));
        assert(slot->texels);
        for( size_t p = 0; p < texel_count; p++ )
            slot->texels[p] = (int)wt.texels[p];
        slot->id = wt.id;
        slot->size = wt.size;
        slot->gate = wt.gate;
        slot->clamp_s = wt.clamp_s;
        slot->clamp_t = wt.clamp_t;
        g_texture_count++;

        /*
         * The classic raster reads through the scene. The wrapper is a second
         * view of the same pixels, not a copy — which is why ev_clear_textures
         * has to unhook it before freeing them.
         */
        if( g_scene && wt.id >= 0 && wt.id < TORIDRAW_TEXTURE_ID_CAPACITY )
        {
            struct ToriDraw_Texture* tex =
                (struct ToriDraw_Texture*)calloc(1, sizeof(*tex));
            if( tex )
            {
                tex->texels = slot->texels;
                tex->width = wt.size;
                tex->height = wt.size;
                tex->opaque = (wt.gate == 0);
                ToriDraw_TextureMapSet(&ToriDraw_SceneTexState(g_scene)->texture_map, wt.id, tex);
            }
        }
    }

    hd_placeholder_build();
    return g_texture_count;
}

int
ev_texture_count(void)
{
    return g_texture_count;
}

void
ev_set_hd_placeholder(int on)
{
    g_hd_placeholder = on ? 1 : 0;
    hd_placeholder_build();
}
static struct ToriDraw_Animation* g_anim = NULL;

/*
 * The attached graphic.
 *
 * `g_spot` is the graphic's own model as the spotanim record built it, kept
 * pristine; `g_combined` is body+graphic as the client would have merged them,
 * rebuilt whenever the graphic's frame or height changes and posed by the
 * BODY's sequence thereafter. `g_combined_frame` is the graphic frame the
 * current merge was built for, so a body-only frame step does not rebuild it.
 */
static struct ToriDraw_Model* g_spot = NULL;
static struct ToriDraw_Animation* g_spot_anim = NULL;
static struct ToriDraw_Model* g_combined = NULL;
static int g_spot_frame = -1;
static int g_spot_height = 0;
static int g_combined_frame = -2; /* neither a frame nor the detached -1 */
static int g_combined_height = 0;
static int g_spot_vertex_first = -1;

/* The raster's own buffer, in toridraw's native pixel type. */
static toripixel_t* g_pixels = NULL;
/* What the page reads: the same image as RGBA bytes, which is what
 * ImageData wants. Kept separate rather than rasterising straight into RGBA
 * because the raster's blend maths is written against its own packing. */
static uint8_t* g_rgba = NULL;
static int g_pix_w = 0;
static int g_pix_h = 0;
static int g_last_cull = -1;
/* View pan in canvas pixels: where the orbit centre lands relative to the
 * canvas centre. Applied as a world-space offset to the model position, not to
 * view_port.x_center/y_center: the raster stage centres at width/2 regardless
 * of those fields (see toridraw_model_sprite.c's padded-buffer workaround), so
 * shifting the viewport centre moves nothing. The model centre sits at camera
 * depth `zoom` exactly, which makes the pixel→world conversion exact there. */
static int g_pan_x = 0;
static int g_pan_y = 0;

/*
 * A free-fly offset in WORLD units, distinct from the pan above.
 *
 * Pan is a screen-space nudge: it slides the image without changing depth, so
 * the subject never gets nearer or further and nothing about the view changes
 * except where it sits on the canvas. Flying is the opposite — it moves the
 * viewpoint through the scene, so depth, culling and apparent size all follow.
 * Keeping them separate means the drag-pan a user already has is not silently
 * redefined by adding movement keys.
 *
 * Held on the world axes, not the camera's: the caller decides how a key maps
 * to a direction (ev_move applies the current yaw), and the result accumulates
 * somewhere stable. Accumulating in camera space instead would make a turn
 * retroactively rotate every step already taken.
 */
static int g_fly_x = 0;
static int g_fly_y = 0;
static int g_fly_z = 0;
/* 0 = measure the lift off the model's own bounds, as before. See
 * ev_set_frame_height for why a caller stepping an animation pins it. */
static int g_frame_height = 0;
/* Render discipline, mirroring the client's per-npc choice (app_npc_wants_
 * zbuffer): 0 draws the painter's sort with face priorities — the authored
 * path — and 1 depth-tests instead, which also drops priorities at sort time
 * exactly as TORIDRAW_MODEL_FLAG_ZBUFFER does in-game. The page picks per
 * run: a search that rendered with --zbuffer is judged z-tested, so the
 * viewer must show the same picture. */
static int g_zbuffer = 0;

/** Background behind the model. Matches the page's panel colour so the canvas
 *  does not read as a hole when the model is small. */
#define EV_BG 0xFF141821u

void
ev_init(void)
{
    if( g_scene )
        return;
    ToriDraw_Init();
    /* MODEL_ZBUFFER only allocates the depth scratch lazily, on the first
     * raster of a model that opts in — painter-mode sessions pay nothing. */
    g_scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
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
/* Any change to a body, a graphic or the graphic's placement invalidates the
 * merge; the next pose rebuilds it. */
static void
drop_combined(void)
{
    ToriDraw_ModelFree(g_combined);
    g_combined = NULL;
    g_combined_frame = -2;
    g_spot_vertex_first = -1;
}

int
ev_set_model(const uint8_t* data, int len)
{
    struct ToriDraw_Model* next = ev_wire_read_model(data, (size_t)(len > 0 ? len : 0));
    if( !next )
        return 0;

    ToriDraw_ModelFree(g_model);
    g_model = next;
    drop_combined();
    if( g_zbuffer )
        g_model->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
    else
        g_model->flags &= (uint8_t)~TORIDRAW_MODEL_FLAG_ZBUFFER;
    return g_model->face_count;
}

/*
 * Adopt an HD model (ev_wire EVH1 bytes) and make it the subject.
 *
 * Returns the face count, 0 on a bad blob. Adopting one does not disturb the
 * npc/player model; clearing it goes straight back to whatever was there.
 */
int
ev_set_model_hd(const uint8_t* data, int len)
{
    struct ToriDraw_ModelHD* next =
        ev_wire_read_model_hd(data, (size_t)(len > 0 ? len : 0));
    if( !next )
        return 0;

    ToriDraw_ModelHDFree(g_model_hd);
    g_model_hd = next;
    memset(&g_hd_stats, 0, sizeof(g_hd_stats));
    hd_placeholder_build();
    return g_model_hd->base.face_count;
}

void
ev_clear_model_hd(void)
{
    ToriDraw_ModelHDFree(g_model_hd);
    g_model_hd = NULL;
    memset(&g_hd_stats, 0, sizeof(g_hd_stats));
    free(g_hd_mats);
    g_hd_mats = NULL;
    g_hd_mat_count = 0;
}

int
ev_model_hd_active(void)
{
    return g_model_hd != NULL;
}

/*
 * The last render's routing tally, as 13 ints in the order the header lists
 * them. Returned as a flat buffer because that is what crosses the wasm
 * boundary cheaply; the page names the fields.
 */
const int*
ev_hd_stats(void)
{
    return (const int*)&g_hd_stats;
}

int
ev_hd_stats_count(void)
{
    return (int)(sizeof(g_hd_stats) / sizeof(int));
}

void
ev_set_zbuffer(int on)
{
    g_zbuffer = on ? 1 : 0;
    /* The merged model carries its own copy of the flag (ToriDraw_ModelNewMerge
     * ORs the parts' flags into the result), so flipping this after a merge has
     * to reach it too or the page keeps drawing the old discipline. */
    struct ToriDraw_Model* targets[2] = { g_model, g_combined };
    for( int i = 0; i < 2; i++ )
    {
        if( !targets[i] )
            continue;
        if( g_zbuffer )
            targets[i]->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
        else
            targets[i]->flags &= (uint8_t)~TORIDRAW_MODEL_FLAG_ZBUFFER;
    }
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

/* ---- the attached graphic ------------------------------------------------ */

int
ev_set_spot_model(const uint8_t* data, int len)
{
    struct ToriDraw_Model* next = ev_wire_read_model(data, (size_t)(len > 0 ? len : 0));
    if( !next )
        return 0;

    ToriDraw_ModelFree(g_spot);
    g_spot = next;
    drop_combined();
    return g_spot->face_count;
}

int
ev_set_spot_anim(const uint8_t* data, int len)
{
    struct ToriDraw_Animation* next = ev_wire_read_anim(data, (size_t)(len > 0 ? len : 0));
    if( !next )
        return 0;

    ToriDraw_AnimationFree(g_spot_anim);
    g_spot_anim = next;
    drop_combined();
    return g_spot_anim->frame_count;
}

void
ev_clear_spot(void)
{
    ToriDraw_ModelFree(g_spot);
    g_spot = NULL;
    ToriDraw_AnimationFree(g_spot_anim);
    g_spot_anim = NULL;
    g_spot_frame = -1;
    drop_combined();
}

void
ev_set_spot_state(int height, int frame)
{
    g_spot_height = height;
    g_spot_frame = frame;
}

int
ev_spot_frame_count(void)
{
    return g_spot_anim ? g_spot_anim->frame_count : 0;
}

int
ev_spot_frame_delay(int index)
{
    if( !g_spot_anim || index < 0 || index >= g_spot_anim->frame_count )
        return 0;
    /* Same NULL-frames guard ev_frame_delay carries, and for the same reason:
     * reading through a skeletal animation's absent frame table does not trap
     * in wasm, it returns whatever bytes are at low linear memory. */
    if( g_spot_anim->skeletal || !g_spot_anim->frames )
        return 1;
    return g_spot_anim->frames[index].delay;
}

/*
 * Merge the graphic into the body, exactly as the client does.
 *
 * The three steps that matter, in the order app_world_sync_one_entity_spotanim
 * takes them:
 *
 *   1. pose the graphic's OWN copy at its own frame,
 *   2. null its labels, so the body's sequence cannot address its vertices —
 *      this is what freezes the graphic in place while the body keeps moving,
 *      and it is the mechanical reason a player-attached graphic can never
 *      track a swinging blade,
 *   3. translate it by -height in y (model y is negative-up) and combine.
 *
 * There is no rotation and no lateral term anywhere in that list. Height is the
 * only placement the caller can express; the graphic's position in the player's
 * local XZ plane is a property of the model's vertices and nothing else.
 */
static void
rebuild_combined(void)
{
    struct ToriDraw_Model* posed;
    struct ToriDraw_Model* parts[2];

    drop_combined();
    if( !g_model || !g_spot || g_spot_frame < 0 )
        return;

    posed = ToriDraw_ModelCopy(g_spot);
    if( !posed )
        return;

    /* A frame with no transforms holds the rest pose — the renderer's own
     * hole-frame rule. */
    if( g_spot_anim && !g_spot_anim->skeletal && g_spot_anim->base && g_spot_anim->frames &&
        g_spot_frame < g_spot_anim->frame_count &&
        g_spot_anim->frames[g_spot_frame].length > 0 )
    {
        ToriDraw_ModelAnimateReset(posed);
        ToriDraw_ModelAnimateFrame(posed, g_spot_anim->base, &g_spot_anim->frames[g_spot_frame]);
    }

    ToriDraw_BonesFree(posed->vertex_bones);
    posed->vertex_bones = NULL;
    ToriDraw_BonesFree(posed->face_bones);
    posed->face_bones = NULL;

    if( g_spot_height != 0 )
        ToriDraw_ModelTranslate(posed, 0, -g_spot_height, 0);

    /* The body goes in at its REST pose. The merged model is captured below and
     * posed from that capture every frame, so merging a posed body would bake
     * one frame of the swing into the rest pose and every later frame would
     * compound on it. */
    ToriDraw_ModelAnimateReset(g_model);
    parts[0] = g_model;
    parts[1] = posed;
    g_combined = ToriDraw_ModelMerge(parts, 2);
    ToriDraw_ModelFree(posed);
    if( !g_combined )
        return;

    g_spot_vertex_first = g_model->vertex_count;
    g_combined_frame = g_spot_frame;
    g_combined_height = g_spot_height;
    if( g_zbuffer )
        g_combined->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
    /* Capture AFTER the merge, so the per-frame reset restores the graphic's
     * posed alphas instead of clearing them: ToriDraw_ModelCaptureOriginal-
     * Vertices takes face_alphas too, and this sequence is an alpha animation. */
    ToriDraw_ModelCaptureOriginalVertices(g_combined);
    ToriDraw_ModelSetBoundsCylinder(g_combined);
}

/** What actually gets drawn and measured. */
static struct ToriDraw_Model*
draw_model(void)
{
    /*
     * An adopted HD model is the subject, and it is the subject HERE rather
     * than only at render time so everything downstream follows it: ev_pose
     * animates through this, ev_model_height frames the camera through this,
     * and the bounds cylinder is rebuilt on whatever this returns. Routing only
     * the render would leave an HD model posed by nothing and framed by the
     * npc it replaced.
     *
     * The graphic-merge branch below is deliberately not reachable while it is
     * set: a file off disk has no attached spotanim.
     */
    if( g_model_hd )
        return &g_model_hd->base;

    if( g_spot && g_spot_frame >= 0 )
    {
        if( !g_combined || g_combined_frame != g_spot_frame || g_combined_height != g_spot_height )
            rebuild_combined();
        if( g_combined )
            return g_combined;
    }
    else if( g_combined )
        drop_combined();
    return g_model;
}

struct ToriDraw_Model*
ev_drawn_model(void)
{
    return draw_model();
}

int
ev_spot_vertex_first(void)
{
    return g_combined ? g_spot_vertex_first : -1;
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
    /* The merged model when a graphic is attached — the body's sequence poses
     * body and graphic together from there, which is the client's arrangement
     * and the only one where the graphic sits still while the body swings. */
    struct ToriDraw_Model* m = draw_model();
    if( !m )
        return;

    ToriDraw_ModelAnimateReset(m);
    if( !g_anim || frame < 0 || frame >= g_anim->frame_count )
    {
        ToriDraw_ModelSetBoundsCylinder(m);
        return;
    }

    if( g_anim->skeletal )
    {
        if( m->animaya_group_counts && m->animaya_groups && m->animaya_scales &&
            m->animaya_vertex_count > 0 && frame < g_anim->skeletal->frame_count &&
            g_anim->skeletal->matrices )
            ToriDraw_ModelAnimateSkeletal(m, g_anim->skeletal, frame);
    }
    else if( g_anim->base && g_anim->frames )
        ToriDraw_ModelAnimateFrame(m, g_anim->base, &g_anim->frames[frame]);
}

int
ev_pose_moved_vertices(int frame)
{
    struct ToriDraw_Model* m = draw_model();
    if( !m || !m->original_vertices_x )
        return -1;

    ev_pose(frame);

    int moved = 0;
    for( int i = 0; i < m->vertex_count; i++ )
        if( m->vertices_x[i] != m->original_vertices_x[i] ||
            m->vertices_y[i] != m->original_vertices_y[i] ||
            m->vertices_z[i] != m->original_vertices_z[i] )
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
    struct ToriDraw_Model* m = draw_model();
    return m ? m->vertex_count : 0;
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
    assert(g_pixels);
    assert(g_rgba);
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
    /*
     * An adopted HD model is a subject in its own right, so `g_model` being
     * NULL is not "nothing to draw" any more. The page happens to always have
     * an npc loaded before a file is picked, which hides this — the standalone
     * harness that adopts only an HD model is what surfaced it.
     */
    if( !g_scene || (!g_model && !g_model_hd) || !ensure_buffers(width, height) )
        return NULL;
    /* Resolved before the pose so the handle below and ev_pose agree on which
     * model is current: draw_model() can rebuild the merge as a side effect. */
    struct ToriDraw_Model* subject = draw_model();
    if( !subject )
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
    hnd.u.model.model = subject;

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
    int model_height = g_frame_height > 0 ? g_frame_height
                                          : (bounds ? (bounds->max_y - bounds->min_y) : 0);

    /* Same placement as ToriDraw_SpriteNewFromModelRaster, minus its widget
     * term: that path blits into a widget rect and offsets by the widget's own
     * height, where this one centres in the viewport via y_center. */
    struct ToriDraw_Position position = { 0 };
    position.x = 0;
    position.y = sin_pitch + (model_height / 2);
    position.z = cos_pitch;
    position.yaw = yaw & 2047;

    /*
     * Pan, as a camera-space translation of the model.
     *
     * The projection is screen = centre + cam_coord * proj_scale / cam_z, and
     * the framing above puts the model centre at cam_z == zoom exactly
     * (cam_z = y*sin + z*cos = zoom*sin² + zoom*cos²), so a shift of
     * pan_px * zoom / proj_scale camera units moves the image by pan_px pixels.
     * Camera x is world x (camera yaw is 0); camera y maps back to world through
     * the inverse pitch rotation, with the z term keeping cam_z unchanged so the
     * pan never alters depth, culling, or apparent size.
     */
    if( g_pan_x || g_pan_y )
    {
        int dx_cam = (g_pan_x * zoom) / TORIDRAW_PROJ_SCALE_DEFAULT;
        int dy_cam = (g_pan_y * zoom) / TORIDRAW_PROJ_SCALE_DEFAULT;
        position.x += dx_cam;
        position.y += (int)(((int64_t)dy_cam * ToriDraw_Cos(camera.pitch)) >> 16);
        position.z -= (int)(((int64_t)dy_cam * ToriDraw_Sin(camera.pitch)) >> 16);
    }

    /*
     * The fly offset moves the VIEWPOINT, and `position` is the model's place
     * relative to the camera — so it subtracts. Adding would send the world the
     * same way as the key, which reads as the model running away from W.
     *
     * y is negated on top of that because RS models grow along -y from a floor
     * at y=0: "up" is negative y, and R has to raise the eye, not lower it.
     */
    position.x -= g_fly_x;
    position.y += g_fly_y;
    position.z -= g_fly_z;

    if( g_model_hd )
    {
        /*
         * The HD flow does its own project + sort + raster, because the routing
         * decision is per face and has to sit inside the raster loop.
         *
         * No material table is supplied: a model file picked off disk carries
         * no textures, and inventing some would show a texture the file does
         * not describe. Every textured face therefore falls back to its flat
         * colour and is counted in `fallback_no_texels` — the geometry and the
         * per-face routing are what this view is for, and both are visible.
         */
        struct ToriDraw_ModelHandle hd_hnd = ToriDraw_ModelHandleFromHD(g_model_hd);
        struct ToriDraw_HDMaterials table = { g_hd_mats, g_hd_mat_count };
        g_last_cull = ToriDraw_RenderHD(
            hd_hnd, g_scene, &position, &view_port, &camera, g_pixels,
            g_hd_mats ? &table : NULL, &g_hd_stats);
    }
    else
    {
        g_last_cull =
            ToriDraw_RenderModel1Project(hnd, g_scene, &position, &view_port, &camera);
        if( g_last_cull == TORIDRAW_CULL_VISIBLE )
        {
            ToriDraw_RenderModel2SortFaces(hnd, g_scene);
            ToriDraw_RenderModel3Raster(g_scene, &view_port, &camera, g_pixels, false);
        }
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
    struct ToriDraw_Model* m = draw_model();
    if( !m )
        return 0;
    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = m;
    struct ToriDraw_BoundsCylinder* b = ToriDraw_ModelGetBoundsCylinder(hnd);
    return b ? (b->max_y - b->min_y) : 0;
}

int
ev_last_cull(void)
{
    return g_last_cull;
}

void
ev_set_frame_height(int height)
{
    g_frame_height = height > 0 ? height : 0;
}

/**
 * Step the viewpoint, in the camera's own frame.
 *
 * ## Why there is no yaw term
 *
 * `yaw` in this viewer is the angle the MODEL is spun by — `camera.yaw` is
 * fixed at 0 and the camera only ever pitches. Dragging therefore spins the
 * subject rather than orbiting the eye, which looks identical and is not: the
 * screen axes never rotate. Right on screen is world x at every yaw, and
 * forward is world z at every yaw.
 *
 * Rotating the step by yaw — which is what this did first — makes the keys
 * correct near yaw 0 and INVERTED half a turn away, because it applies a
 * rotation the projection never applied. See ev_move_screen_probe, which
 * measures where the subject actually goes rather than asserting a convention.
 */
void
ev_move(int forward, int right, int up)
{
    g_fly_x += right;
    g_fly_z += forward;
    g_fly_y += up;
}

/** Back to the framed view. */
void
ev_move_reset(void)
{
    g_fly_x = 0;
    g_fly_y = 0;
    g_fly_z = 0;
}

/** The three world components, for a readout. */
void
ev_move_get(int* out_x, int* out_y, int* out_z)
{
    if( out_x )
        *out_x = g_fly_x;
    if( out_y )
        *out_y = g_fly_y;
    if( out_z )
        *out_z = g_fly_z;
}

void
ev_set_pan(int x, int y)
{
    /* Clamp to the raster's own dimension cap: a pan that far has already put
     * the model off every canvas this renderer can allocate. */
    g_pan_x = x < -EV_MAX_DIM ? -EV_MAX_DIM : (x > EV_MAX_DIM ? EV_MAX_DIM : x);
    g_pan_y = y < -EV_MAX_DIM ? -EV_MAX_DIM : (y > EV_MAX_DIM ? EV_MAX_DIM : y);
}
