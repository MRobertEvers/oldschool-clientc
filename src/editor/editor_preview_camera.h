#ifndef SRC_EDITOR_EDITOR_PREVIEW_CAMERA_H
#define SRC_EDITOR_EDITOR_PREVIEW_CAMERA_H

/*
 * The camera on the editor's model-view well -- the little box that shows what
 * the catalog has picked, or what the loc editor has selected.
 *
 * Two halves that look like one. The player's half is orbit and zoom, driven
 * by the keys the loc editor routes; the fit's half is the zoom that frames a
 * model the first time it is shown. They share the zoom, which is why "keep
 * the camera" is a flag rather than something the caller can infer: an orbit
 * must survive the re-render it causes, and a NEW model must not inherit the
 * angle the last one was left at.
 */

#include <stdbool.h>

/**
 * How far in and how far out the player may take the well.
 *
 * Wider than the fit's own bounds on purpose: a fit frames a model, and a
 * person looking at one is allowed to go closer than framing and further out
 * than framing. Both ends are clamps and not asserts -- this is a key being
 * held down, not a caller's arithmetic.
 */
enum
{
    EDITOR_PREVIEW_ZOOM_MIN = 300,
    EDITOR_PREVIEW_ZOOM_MAX = 16000,
    /** What a fit may choose between. Inside the player's range at both ends,
     *  so a fitted model can always be pushed further either way. */
    EDITOR_PREVIEW_FIT_ZOOM_MIN = 500,
    EDITOR_PREVIEW_FIT_ZOOM_MAX = 12000,
    /** Where a fresh model is shown from: three quarters round and tipped
     *  down, which is the angle the catalog's own thumbnails are drawn at. */
    EDITOR_PREVIEW_DEFAULT_PITCH = 160,
    EDITOR_PREVIEW_DEFAULT_YAW = 300,
    /** The well the fit is calibrated for: a fitted model fills a 120 x 96
     *  well at the reference projection (screen = coord * scale / z). A larger
     *  well shows the same framing larger (EditorPreviewCamera_ProjectionScale).
     *  The scale is 3/4 of the raster's default 512: the calibrated fit
     *  ((size * 9) / 2) projects a model's larger dimension at 512 / 4.5 =
     *  114 px, a fifth more than the 96 px well, and the catalog's old well
     *  cut the top off every tree and every npc; at 384 it is 85 px, the whole
     *  model with a margin. */
    EDITOR_PREVIEW_REF_W = 120,
    EDITOR_PREVIEW_REF_H = 96,
    EDITOR_PREVIEW_REF_PROJECTION = 384,
    /** The one size the preview sprite is rendered at, whatever the well:
     *  a renderer that packs sprites into a shared atlas reuses a sprite's
     *  tile only at the same size (src/platform/platform_sdl2_renderer_gl3.c
     *  gl3_sprite_upload_rgba), so a well rendered at its own size would take
     *  a new tile on every resize until the atlas is full and the preview,
     *  and every sprite after it, silently stop drawing. The model is drawn
     *  at the sprite's middle and the well shows the sprite centred, clipped.
     *  A well larger than this frames the model at this size (5:4, the
     *  reference's aspect, a little over 5x it). */
    EDITOR_PREVIEW_SPRITE_W = 640,
    EDITOR_PREVIEW_SPRITE_H = 512,
};

struct EditorPreviewCamera
{
    /** Pitch and yaw, 0..2047, wrapping. */
    int pitch;
    int yaw;
    int zoom;
    /** The well's content changed and has to be rendered again. */
    bool dirty;
    /** The next render should choose a zoom that frames what it is given. */
    bool fit_pending;
};

void
EditorPreviewCamera_Reset(struct EditorPreviewCamera* camera);

/**
 * The well has to be re-rendered.
 *
 * `same_model` is the difference between an orbit and a new pick, and it is
 * acted on HERE rather than remembered for the render. An orbit keeps the
 * camera -- otherwise every key press snaps the model back to its default
 * angle and the well cannot be turned at all. A new pick resets it and asks
 * for a fit, because the angle and distance that framed a candle do not frame
 * a castle gate.
 *
 * Deciding it here is what keeps it from being two answers. A flag that says
 * "the render after this one is the same model" is true until something else
 * invalidates, and every reader in between has to know that.
 */
void
EditorPreviewCamera_Invalidate(
    struct EditorPreviewCamera* camera,
    bool same_model);

/** Orbit by `pitch_step` / `yaw_step`, wrapping. Either may be 0. */
void
EditorPreviewCamera_Orbit(
    struct EditorPreviewCamera* camera,
    int pitch_step,
    int yaw_step);

/** Step the zoom in (negative) or out (positive), by fiftieths, clamped. */
void
EditorPreviewCamera_Zoom(
    struct EditorPreviewCamera* camera,
    int direction);

/**
 * Take the pending render.
 *
 * Returns false when nothing needs rendering. Call it once where the render
 * happens: the flag is consumed here, so a caller that asks twice in a frame
 * renders once.
 */
bool
EditorPreviewCamera_TakeRender(struct EditorPreviewCamera* camera);

/**
 * The zoom that frames a model of these bounds, if one was asked for.
 *
 * Returns false when no fit is pending, and then the camera's current zoom
 * stands -- which is what makes an orbit survive its own re-render.
 *
 * The rule is the larger of the model's diameter and its height, because a
 * fit has to hold whichever way the model is long: fitting on height alone
 * crops a gate to a wall of pixels, and on width alone loses a candle in the
 * middle of the well. The constant is calibrated against the obj-icon
 * pipeline, where zoom 2000 frames a typical item in about thirty pixels.
 */
bool
EditorPreviewCamera_TakeFit(
    struct EditorPreviewCamera* camera,
    int radius,
    int min_y,
    int max_y);

/**
 * The projection scale that shows a well of `well_w` x `well_h` pixels the
 * framing the reference 120 x 96 well shows, scaled by the smaller of the two
 * ratios (so the fitted model stays whole whichever way the well is long).
 *
 * The scale, not the zoom: the zoom is the camera's DISTANCE, and moving the
 * camera closer to fill a bigger well changes the perspective and walks the
 * raster's vertical placement off the middle; scaling the projection draws the
 * very same view bigger, and leaves the orbit and zoom keys meaning what they
 * meant. Re-fitting on a resize is this and nothing else.
 */
int
EditorPreviewCamera_ProjectionScale(
    int well_w,
    int well_h);

#endif /* SRC_EDITOR_EDITOR_PREVIEW_CAMERA_H */
