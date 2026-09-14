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

#endif /* SRC_EDITOR_EDITOR_PREVIEW_CAMERA_H */
