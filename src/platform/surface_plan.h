#ifndef SRC_PLATFORM_SURFACE_PLAN_H
#define SRC_PLATFORM_SURFACE_PLAN_H

/*
 * What a frame does with its picture when the lane may have nowhere to put
 * one (PlatformWindow_CanPresent false: Android while its activity is stopped
 * or its SurfaceView is being recreated, a phone-width plugin page over the
 * game). One decision per frame, made by the shared loop (frame_loop_step,
 * main.c) for every lane, and tested through a fake lane
 * (platform/test/loop_lane_test.c).
 *
 * The world, the network and the queues have run whole by the time this is
 * asked (App_RunOnce); only the draw is in question. With no surface:
 *
 *   - nothing is drawn or presented;
 *   - the scene's event queue and the poses held for the frame are SPENT, as
 *     a skipped frame spends them (ToriDraw_SceneFrameEnd), on EVERY lane.
 *     Not spending them let the queue grow to its cap
 *     (TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE), past which td_scene_emit drops
 *     every event silently -- the sound loads audio reads from the queue
 *     among them -- however long the surface stayed away;
 *   - a RETAINED renderer (GLES2/GLES3, any GPU lane), which learns the scene
 *     only from those events, is then BEHIND: what it was owed is gone. So
 *     the first frame with a surface again re-bakes it before it draws --
 *     the renderer restarted as itself and handed a replay of every load the
 *     scene holds (ToriDraw_SceneReemitRendererLoads), exactly what a
 *     renderer switch does. Soft3D reads the scene directly and is never
 *     behind.
 */

struct ToriRS_SurfacePlan
{
    /** A retained renderer missed events while there was no surface. */
    int behind;
    /** Frames run with no surface since it was last there. */
    int frames_without;
};

struct ToriRS_SurfaceFrame
{
    /** Draw and present this frame (the caller's own redraw decision stands). */
    int draw;
    /** Not drawn: spend the scene's queue and held poses as a skipped frame. */
    int spend;
    /** Before drawing: restart the retained renderer as itself and replay the
     *  scene's loads into it. */
    int rebake;
};

static inline void
ToriRS_SurfacePlan_Init(struct ToriRS_SurfacePlan* plan)
{
    plan->behind = 0;
    plan->frames_without = 0;
}

/*
 * The frame's decision. `can_present`: the lane has a surface now.
 * `retained`: the running renderer learns the scene from its events.
 * `redraw`: the frame wants a picture (the loop's own decision).
 */
static inline struct ToriRS_SurfaceFrame
ToriRS_SurfacePlan_Frame(
    struct ToriRS_SurfacePlan* plan,
    int can_present,
    int retained,
    int redraw)
{
    struct ToriRS_SurfaceFrame frame = { 0, 0, 0 };

    if( !can_present )
    {
        plan->frames_without++;
        frame.spend = 1;
        if( retained )
            plan->behind = 1;
        return frame;
    }
    plan->frames_without = 0;
    frame.draw = redraw;
    /* Owed before any draw, so the first frame back draws a scene the
     * renderer is level with. A frame that does not draw still re-bakes: the
     * next one that does must not find it behind. */
    if( plan->behind )
    {
        frame.rebake = retained;
        plan->behind = 0;
    }
    return frame;
}

#endif
