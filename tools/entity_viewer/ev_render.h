#ifndef EV_RENDER_H
#define EV_RENDER_H

/*
 * The viewer's render core: model + animation in, RGBA image out.
 *
 * Toridraw only — no cache, no decoders, no file access. Compiled into both the
 * WebAssembly module and the native server, so the browser and the command line
 * exercise the same code.
 */

#include <stdint.h>

void
ev_init(void);

void*
ev_alloc(int size);

void
ev_release(void* p);

/** Adopt a model from ev_wire bytes. Returns its face count, 0 on a bad blob. */
int
ev_set_model(const uint8_t* data, int len);

/* ---- the HD model ---------------------------------------------------------
 *
 * A model file picked off disk rather than pulled from the cache. It is drawn
 * through ToriDraw_RenderHD, which routes each face on its render type, its
 * material gate and its alpha — see toridraw_render_hd.h.
 *
 * No material table is supplied, because a bare model file describes no
 * textures. Textured faces therefore fall back to flat colour and say so in the
 * stats; the geometry and the per-face routing are what this view shows.
 *
 * While an HD model is adopted it is what draws, and the npc/player model is
 * left untouched underneath.
 */

/** Adopt an HD model (ev_wire EVH1 bytes). Face count, or 0 on a bad blob. */
int
ev_set_model_hd(const uint8_t* data, int len);

/** Drop it; the npc/player model draws again. */
void
ev_clear_model_hd(void);

int
ev_model_hd_active(void);

/**
 * Supply a synthetic checkerboard for every texture id the HD model names.
 *
 * Off by default, because a bare model file describes no textures and drawing
 * one would be a claim the file does not make. On, the mapped kernels actually
 * run — which is the only way the cylinder / cube / sphere routing is
 * observable at all. The page labels it.
 */
/**
 * Install the texture set both render paths draw with (an EVT1 blob).
 *
 * Textures come over the wire rather than out of a cache because this file is
 * compiled into the browser too, where there is no cache. Returns how many
 * textures were installed.
 */
int
ev_set_textures(const uint8_t* data, int len);

/** How many textures are currently installed. */
int
ev_texture_count(void);

/**
 * Draw as if the model carried no face priorities.
 *
 * The painter's sort ranks by priority before depth. If a model's priorities
 * are wrong — or mean something other than draw order, which is the open
 * question for the HD models — the result is faces stacked in front of things
 * they belong behind, and that is not distinguishable from a depth bug by
 * looking at it. Turning them off answers which one it is.
 *
 * Reversible: the arrays are hidden for the duration of a render, not freed.
 */
void
ev_set_ignore_priorities(int on);

int
ev_ignore_priorities(void);

void
ev_set_hd_placeholder(int on);

/** The last render's ToriDraw_HDRenderStats, as a flat int array. */
const int*
ev_hd_stats(void);

int
ev_hd_stats_count(void);

/** Adopt an animation. Returns its frame count, 0 on a bad blob. */
int
ev_set_anim(const uint8_t* data, int len);

/** Drop the animation, leaving the model in its bind pose. */
void
ev_clear_anim(void);

/* ---- the attached graphic ------------------------------------------------
 *
 * A player-attached spotanim is not a second thing in the scene. The client
 * poses the graphic, strips its labels, lifts it by the spotanim height and
 * MERGES it into the player's own model (app_world_sync_one_entity_spotanim,
 * src/app.c); the merged mesh becomes what the scene draws, and the body's
 * sequence animates it from there. So a viewer that draws the two models side
 * by side is not showing what the game shows — it has to merge, and it has to
 * merge the same way.
 *
 * With no spot model adopted, everything below is inert and this module behaves
 * exactly as it did before.
 */

/** Adopt the graphic's model (ev_wire bytes). Returns its face count, 0 on a
 *  bad blob. */
int
ev_set_spot_model(const uint8_t* data, int len);

/** Adopt the graphic's own animation. Returns its frame count, 0 on a bad blob. */
int
ev_set_spot_anim(const uint8_t* data, int len);

/** Drop the graphic; the model goes back to being drawn alone. */
void
ev_clear_spot(void);

/**
 * Where the graphic is in its own sequence, and how high it rides.
 *
 * `frame` < 0 detaches it for this render — which is the state during the part
 * of a swing before the graphic has been played, and is not the same as frame 0.
 * `height` is the spotanim height in model units, the third argument of
 * `spotanim_pl` and the only placement lever the server has.
 */
void
ev_set_spot_state(int height, int frame);

int
ev_spot_frame_count(void);

/** A graphic frame's duration in client cycles. 0 when unknown. */
int
ev_spot_frame_delay(int index);

int
ev_frame_count(void);

/** A frame's duration in client ticks (20ms each). 0 when unknown. */
int
ev_frame_delay(int index);

/**
 * Render one frame into a reused RGBA buffer of width*height*4 bytes.
 *
 * `yaw` and `pitch` are in the client's 2048-per-turn units, `zoom` is the
 * orbit distance in world units, and `frame` selects an animation frame or -1
 * for the bind pose. Returns NULL when there is nothing to draw.
 */
uint8_t*
ev_render(int width, int height, int yaw, int pitch, int zoom, int frame);

/**
 * Pin the height the framing lifts the model by, instead of measuring it off
 * the model's own bounds each render. 0 restores the measured behaviour.
 *
 * The framing raises the model by half its height so it sits centred rather
 * than hanging off the top. That is right for browsing one model and wrong for
 * comparing a series: merging a large attached graphic into a player grows the
 * combined bounds, so the lift changes, so the camera distance changes, and the
 * player drifts and rescales from frame to frame under whatever is being
 * measured. A caller stepping through an animation pins this once.
 */
void
ev_set_frame_height(int height);

/** Pan the view: shift where the orbit centre lands on the canvas, in canvas
 *  pixels (+x right, +y down). Persists until changed; 0,0 recentres. Kept
 *  out of ev_render's signature so existing embedders stay source- and
 *  wasm-ABI-compatible. */
void
ev_set_pan(int x, int y);

/**
 * Fly the viewpoint one step, in world units, along the camera's own axes.
 *
 * Takes no yaw: this viewer spins the MODEL and never yaws the camera, so the
 * screen axes are fixed — forward is always toward the subject and right is
 * always right, at every yaw. Passing a yaw rotation here is the bug that makes
 * the keys invert half a turn from where they were tuned.
 *
 * Steps accumulate; ev_move_reset returns to the framed view.
 *
 * This is not ev_set_pan. Pan slides the image on the canvas without changing
 * depth; this moves through the scene, so distance, culling and apparent size
 * all change with it.
 */
void
ev_move(int forward, int right, int up);

void
ev_move_reset(void);

void
ev_move_get(int* out_x, int* out_y, int* out_z);

/** Choose the render discipline: 0 (default) is the painter's sort honouring
 *  face priorities; 1 depth-tests instead, which also drops priorities — the
 *  same trade TORIDRAW_MODEL_FLAG_ZBUFFER makes in-game. Applies to the
 *  current model and any adopted later. */
void
ev_set_zbuffer(int on);

/**
 * Draw through the depth-tested (`zbuf`) kernels with NO face sort at all.
 *
 * A third discipline, not a stronger ev_set_zbuffer. That one sets
 * TORIDRAW_MODEL_FLAG_ZBUFFER, which keeps the painter's sort and depth-tests
 * underneath it; this calls ToriDraw_RenderZBuffered / ToriDraw_RenderHDZBuffered
 * instead, which rank nothing — faces are drawn in the model's own order and the
 * depth buffer alone decides. Face priorities cannot matter here, so a picture
 * that is still wrong under this toggle is wrong about DEPTH, not about order.
 *
 * Applies to the classic and HD subjects alike, and unlike the flag it needs
 * nothing written to the model — so it reaches an adopted HD model too.
 */
void
ev_set_zbuffer_kernels(int on);

int
ev_zbuffer_kernels(void);

/** Pose the model for `frame`; -1 is the bind pose. Branches between classic
 *  and skeletal animation, and is the only place that does. */
void
ev_pose(int frame);

/** Pose for `frame` and report how many vertices moved from the bind pose.
 *  -1 when there is no model. Zero means the animation does not drive this
 *  model's rig, whatever a catalog said. */
int
ev_pose_moved_vertices(int frame);

int
ev_anim_is_skeletal(void);

int
ev_model_has_animaya(void);

int
ev_model_vertex_count(void);

/** The model's height in world units, so a caller can frame it. */
int
ev_model_height(void);

/** Why the last ev_render drew nothing: the projection's cull result. */
int
ev_last_cull(void);

/* ---- measurement hooks ---------------------------------------------------
 *
 * The model as last posed, so a caller in the same process can read geometry
 * out of it instead of guessing at it from pixels. Native only in practice —
 * nothing in the browser calls these — but they are ordinary C, not a debug
 * build.
 */

struct ToriDraw_Model;

/** The model ev_render actually draws: the merged body+graphic when a graphic
 *  is attached, the plain model otherwise. Valid until the next adopt/pose. */
struct ToriDraw_Model*
ev_drawn_model(void);

/** Index of the graphic's first vertex inside ev_drawn_model(), or -1 when no
 *  graphic is merged in. Everything at or past it is the graphic. */
int
ev_spot_vertex_first(void);

#endif
