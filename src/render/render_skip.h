#ifndef SRC_RENDER_RENDER_SKIP_H
#define SRC_RENDER_RENDER_SKIP_H

/*
 * Render skip: which committed frames are drawn, and whether a skipped one
 * can still be drawn late.
 *
 * With skip on, a frame the host would draw is drawn only when something
 * needs the picture: the host forces it, a capture is waiting, a draw is
 * owed, or a world-entity view stands in the loaded scene. Skip only takes
 * draws away; a frame the host would not draw is never drawn for any of
 * these. With skip off every frame the host would draw is drawn.
 *
 * The render-time state (the pickset, the posed bounds) is fresh when the
 * most recent committed frame was drawn. A committed frame that was skipped
 * leaves it stale, and its emit list -- the current one until the next commit
 * -- can be drawn late to catch the state up. A frame that was not committed
 * after it means it cannot: the tree may have moved under that emit list.
 *
 * Owed draws are counted in frames drawn, not frames passed. A hold (a replay
 * seek) draws nothing but forced frames and captures whatever `enabled` says;
 * releasing it owes one drawn frame.
 *
 * Nothing here draws, reads the renderer, the world or the captures, or
 * prints. The caller says whether a capture is waiting and how many hulls
 * stand in the scene, draws the frames it is told to and the late frame into
 * `scratch`, which it allocates and frees, and reports the counts.
 */

#include <stdint.h>

struct RenderSkip
{
    /** 1: draw only the frames RenderSkip_Frame says must be drawn. */
    int enabled;
    /** Frames still owed a draw, the current one first, counted in frames
     *  DRAWN. */
    int draw_frames;
    /** The render-time state is the most recent committed frame's. */
    int fresh;
    /** The most recent frame was committed and skipped, so its emit list is
     *  the current one and may still be drawn late. */
    int catch_up_ok;
    /** The caller's layout-sized buffer a late draw rasterises into; NULL
     *  until the first one. Never allocated or freed here. */
    int* scratch;
    /** Loop iterations drawn and not drawn, skip on or off, and skipped
     *  frames drawn late. */
    uint64_t frames_drawn;
    uint64_t frames_skipped;
    uint64_t frames_caught_up;
    /** A replay seek holds the screen. Independent of `enabled`. */
    int hold;
    /** The caller's stamp of the tree the latest committed emit list was
     *  built from (RenderSkip_NoteEmit): a late draw needs the same tree. */
    uint32_t emit_tree;
};

/** What a late draw would do now (RenderSkip_CatchUp). */
enum RenderSkipCatchUp
{
    /** Skip is off or the most recent committed frame was drawn: nothing to
     *  draw, the state is current. */
    RENDER_SKIP_CATCH_UP_FRESH = 0,
    /** Stale, and an uncommitted frame since means it cannot be caught up. */
    RENDER_SKIP_CATCH_UP_LOST = 1,
    /** Stale, and the skipped frame's emit list is current: draw it. */
    RENDER_SKIP_CATCH_UP_DRAW = 2,
};

/** Off, not held, nothing owed or counted, no scratch. */
void
RenderSkip_Reset(struct RenderSkip* skip);

/** Switch skip on or off. Turning it on starts fresh: the frame before was
 *  drawn. */
void
RenderSkip_Set(
    struct RenderSkip* skip,
    int enabled);

int
RenderSkip_Enabled(struct RenderSkip const* skip);

/** Hold the screen (1) or release it (0). Returns 1 when this releases a
 *  hold: one drawn frame is owed, and the caller asks for a redraw. */
int
RenderSkip_Hold(
    struct RenderSkip* skip,
    int hold);

int
RenderSkip_Held(struct RenderSkip const* skip);

/** Owe `frames` drawn frames, starting with the current one. A larger debt
 *  already owed stands. */
void
RenderSkip_RequestDraw(
    struct RenderSkip* skip,
    int frames);

/** A frame is being committed, its emit list built from the tree whose
 *  mutation stamp is `tree` (the App's UITree dirty_gen). */
void
RenderSkip_NoteEmit(
    struct RenderSkip* skip,
    uint32_t tree);

/** Before a late draw: the tree's stamp now. A tree that moved since the
 *  skipped frame's list was built cannot be drawn under it -- the list
 *  borrows the tree (a text command points at the node's own string) -- so
 *  the catch-up is lost, as after an uncommitted frame. */
void
RenderSkip_NoteTree(
    struct RenderSkip* skip,
    uint32_t tree);

/** Is a late draw needed, possible, or neither? */
enum RenderSkipCatchUp
RenderSkip_CatchUp(struct RenderSkip const* skip);

/** The skipped frame was drawn late: the state is fresh. */
void
RenderSkip_NoteCaughtUp(struct RenderSkip* skip);

/** The state was thrown away under the caller (a checkpoint restore): stale,
 *  and nothing to catch up from. */
void
RenderSkip_NoteStateLost(struct RenderSkip* skip);

/**
 * Does RenderSkip_Frame, given `forced` and `capture_pending`, need the
 * count of hulls in the scene? Only when every other reason to draw is
 * absent; otherwise the caller passes 0 without counting.
 */
int
RenderSkip_AsksHulls(
    struct RenderSkip const* skip,
    int forced,
    int capture_pending);

/**
 * The frame's last word on drawing. `redraw` is the host's answer, `forced`
 * its own must-draw, `capture_pending` whether a capture is waiting and
 * `hulls_in_scene` the hulls standing in the loaded scene (read only when
 * RenderSkip_AsksHulls says so). Returns whether the frame is drawn, and
 * counts it.
 */
int
RenderSkip_Frame(
    struct RenderSkip* skip,
    int redraw,
    int forced,
    int capture_pending,
    int hulls_in_scene);

uint64_t
RenderSkip_FramesDrawn(struct RenderSkip const* skip);

uint64_t
RenderSkip_FramesSkipped(struct RenderSkip const* skip);

uint64_t
RenderSkip_FramesCaughtUp(struct RenderSkip const* skip);

#endif /* SRC_RENDER_RENDER_SKIP_H */
