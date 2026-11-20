#include "render/render_skip.h"

#include <assert.h>
#include <string.h>

void
RenderSkip_Reset(struct RenderSkip* skip)
{
    assert(skip);
    memset(skip, 0, sizeof(*skip));
}

void
RenderSkip_Set(
    struct RenderSkip* skip,
    int enabled)
{
    assert(skip);
    if( enabled && !skip->enabled )
    {
        skip->fresh = 1;
        skip->catch_up_ok = 0;
    }
    skip->enabled = enabled ? 1 : 0;
}

int
RenderSkip_Enabled(struct RenderSkip const* skip)
{
    assert(skip);
    return skip->enabled;
}

int
RenderSkip_Hold(
    struct RenderSkip* skip,
    int hold)
{
    int released = 0;

    assert(skip);
    if( !hold && skip->hold )
    {
        /* What was held back is drawn now, skip on or off. */
        skip->draw_frames = skip->draw_frames > 1 ? skip->draw_frames : 1;
        released = 1;
    }
    skip->hold = hold ? 1 : 0;
    return released;
}

int
RenderSkip_Held(struct RenderSkip const* skip)
{
    assert(skip);
    return skip->hold;
}

void
RenderSkip_RequestDraw(
    struct RenderSkip* skip,
    int frames)
{
    assert(skip);
    assert(frames > 0);
    if( skip->draw_frames < frames )
        skip->draw_frames = frames;
}

void
RenderSkip_NoteEmit(
    struct RenderSkip* skip,
    uint32_t tree)
{
    assert(skip);
    skip->emit_tree = tree;
}

void
RenderSkip_NoteTree(
    struct RenderSkip* skip,
    uint32_t tree)
{
    assert(skip);
    if( tree != skip->emit_tree )
        skip->catch_up_ok = 0;
}

enum RenderSkipCatchUp
RenderSkip_CatchUp(struct RenderSkip const* skip)
{
    assert(skip);
    if( !skip->enabled || skip->fresh )
        return RENDER_SKIP_CATCH_UP_FRESH;
    if( !skip->catch_up_ok )
        return RENDER_SKIP_CATCH_UP_LOST;
    return RENDER_SKIP_CATCH_UP_DRAW;
}

void
RenderSkip_NoteCaughtUp(struct RenderSkip* skip)
{
    assert(skip);
    skip->fresh = 1;
    skip->catch_up_ok = 0;
    skip->frames_caught_up++;
}

void
RenderSkip_NoteStateLost(struct RenderSkip* skip)
{
    assert(skip);
    skip->catch_up_ok = 0;
    skip->fresh = 0;
}

int
RenderSkip_AsksHulls(
    struct RenderSkip const* skip,
    int forced,
    int capture_pending)
{
    assert(skip);
    /* A hold that refuses the frame returns before the hulls are asked. */
    if( skip->hold && !forced && !capture_pending )
        return 0;
    return skip->enabled && !forced && !capture_pending && !(skip->draw_frames > 0);
}

int
RenderSkip_Frame(
    struct RenderSkip* skip,
    int redraw,
    int forced,
    int capture_pending,
    int hulls_in_scene)
{
    int draw = redraw;

    assert(skip);
    if( skip->hold && !forced )
    {
        /* The last drawn frame stays up. A capture still draws -- it was
         * asked for -- and a forced frame is not refused. */
        if( !capture_pending )
        {
            if( redraw )
                skip->fresh = 0;
            skip->catch_up_ok = 0;
            skip->frames_skipped++;
            return 0;
        }
    }
    if( skip->enabled )
    {
        int const must = forced || capture_pending || skip->draw_frames > 0 || hulls_in_scene > 0;

        /* Only ever TAKES a draw away. */
        draw = redraw && must;
        if( draw )
        {
            skip->fresh = 1;
            skip->catch_up_ok = 0;
        }
        else if( redraw )
        {
            /* Committed and skipped: the state goes stale, and this frame's
             * emit list -- the current one until the next commit -- can
             * still be drawn late. */
            skip->fresh = 0;
            skip->catch_up_ok = 1;
        }
        else
        {
            /* Not committed: with skip off nothing is drawn either, so a
             * fresh state stays fresh; a stale one can no longer be caught
             * up -- the tree may have moved under the old emit list. */
            skip->catch_up_ok = 0;
        }
    }
    if( draw )
        skip->frames_drawn++;
    else
        skip->frames_skipped++;
    /* A request is for frames DRAWN, not frames passed: one owed across an
     * uncommitted stretch is still owed when the stretch ends. */
    if( draw && skip->draw_frames > 0 )
        skip->draw_frames--;
    return draw;
}

uint64_t
RenderSkip_FramesDrawn(struct RenderSkip const* skip)
{
    assert(skip);
    return skip->frames_drawn;
}

uint64_t
RenderSkip_FramesSkipped(struct RenderSkip const* skip)
{
    assert(skip);
    return skip->frames_skipped;
}

uint64_t
RenderSkip_FramesCaughtUp(struct RenderSkip const* skip)
{
    assert(skip);
    return skip->frames_caught_up;
}
