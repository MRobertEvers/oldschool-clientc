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

/** Adopt an animation. Returns its frame count, 0 on a bad blob. */
int
ev_set_anim(const uint8_t* data, int len);

/** Drop the animation, leaving the model in its bind pose. */
void
ev_clear_anim(void);

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

/** Pan the view: shift where the orbit centre lands on the canvas, in canvas
 *  pixels (+x right, +y down). Persists until changed; 0,0 recentres. Kept
 *  out of ev_render's signature so existing embedders stay source- and
 *  wasm-ABI-compatible. */
void
ev_set_pan(int x, int y);

/** Choose the render discipline: 0 (default) is the painter's sort honouring
 *  face priorities; 1 depth-tests instead, which also drops priorities — the
 *  same trade TORIDRAW_MODEL_FLAG_ZBUFFER makes in-game. Applies to the
 *  current model and any adopted later. */
void
ev_set_zbuffer(int on);

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

#endif
