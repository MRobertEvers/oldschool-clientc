#ifndef SRC_RENDER_TORIRS_FRAME_H
#define SRC_RENDER_TORIRS_FRAME_H

#include "render/torirs_render.h"
#include "ui/uitree_scroll.h"

#include <stdbool.h>

struct PaintersBuffer;
struct ToriDraw_Scene;
struct UITreeEmitBuffer;
struct UITreeEmitDesc;
struct World;

/** Pass coalescing for BEGIN/END_2D/3D. */
enum ToriRS_FramePassKind
{
    TORIRS_FRAME_PASS_NONE = 0,
    TORIRS_FRAME_PASS_2D,
    TORIRS_FRAME_PASS_3D,
};

/** Views the frame can hold transforms for; matches PAINTER_MAX_WORLD_VIEWS
 * and WORLDVIEW_MAX, and doubles as the descent depth cap. */
#define TORIRS_FRAME_MAX_VIEWS 16
/** Slots in the emit loop's lookahead ring; a power of two, and more than
 *  the deepest prefetch pipeline reaches (cur..cur+4). */
#define TORIRS_FRAME_LOOKAHEAD_RING 8

/**
 * One world view's descent transform (SAILING_PLAN C3, SAILING.md §5.2).
 *
 * Applied to every element of the view, in this order, before the camera
 * subtract: deck-local → pivot recenter → [flatten scale + y offset] →
 * [animation bob/roll] → yaw rotate → entity translate → parent space.
 *
 * The bracketed steps are RESERVED, not implemented: `flatten_scale_q16` is
 * 65536 and `flatten_y_offset` 0 until C4 fills them, `flat_hsl` is -1 (no
 * override), and the animation matrix is deferred with no slot spent yet.
 */
struct ToriRS_FrameViewXform
{
    /** Terrain commands inside this view resolve through its own World. */
    struct World* world;
    /** -size_tiles*64 - cfgPivot, per axis: recenters on the rotation pivot. */
    int recenter_x;
    int recenter_z;
    /** The entity pose in its PARENT view's scene-local fine units. */
    int translate_x;
    int translate_y;
    int translate_z;
    /** Entity heading, 0..2047; composes additively down the stack. */
    int yaw;
    /* Reserved for C4 — identity values today. */
    int flatten_scale_q16;
    int flatten_y_offset;
    int flat_hsl;
    bool live;
};

/**
 * Greedy frame emitter: one GFX command per ToriRS_FrameNextCommand call.
 * Translates UITreeEmitBuffer; WORLD opens a 3D pass and walks PaintersBuffer
 * when world/painters are attached.
 */
struct ToriRS_Frame
{
    struct ToriDraw_Scene* scene;
    struct UITreeEmitDesc const* emit_cmds;
    int emit_count;

    struct World* world;
    struct PaintersBuffer* painters;
    /** Slot 0 is the root (identity, world == frame->world); 1.. are entities.
     * Rebuilt every frame by the App; a cleared table means no boats. */
    struct ToriRS_FrameViewXform views[TORIRS_FRAME_MAX_VIEWS];
    /**
     * The descent stack the BEGIN_WORLD / END_WORLD commands drive, holding the
     * transform COMPOSED from the root down — `root = R(yaw) * local + off` —
     * so an element costs one rotate regardless of how deep it is nested.
     */
    struct
    {
        struct World* world;
        int off_x;
        int off_y;
        int off_z;
        int yaw;
        /** Which view opened this level; slot 0 (the root) is 0. Kept so a
         *  close that names a different view than the open is caught here
         *  rather than resolving terrain against the wrong world. */
        int view_id;
    } view_stack[TORIRS_FRAME_MAX_VIEWS];
    int view_depth;
    struct ToriDraw_Camera world_camera;
    int cam_x;
    int cam_y;
    int cam_z;
    int canvas_w;
    int canvas_h;
    bool has_world_camera;

    enum ToriRS_FramePassKind pass;
    int emit_index;
    int painters_index;
    /* The element ids of the next few painter commands, resolved once when
     * they are prefetched (a terrain command's id is a pool lookup) and
     * reused when the command comes round. Slot i & (RING-1) holds command
     * i. Eight slots, not four: the deepest pipeline the emit loop offers
     * (TORIRS_FRAME_PREFETCH_MODEL=2) resolves cur+4 while cur is still
     * pending, and a four-slot ring put both in the same slot -- the
     * prefetch evicted the current command's own id, which was then resolved
     * a second time at its turn. */
    int lookahead_index[TORIRS_FRAME_LOOKAHEAD_RING];
    int lookahead_id[TORIRS_FRAME_LOOKAHEAD_RING];
    /** ToriDraw_SceneEvents(scene), taken once at ToriRS_FrameBegin instead of
     *  per command (TORIRS_FRAME_TRIM); `scene_events_of` is the scene it was
     *  taken from, so a re-pointed scene cannot be served a stale queue. */
    struct ToriDraw_EventQueue* scene_events;
    const struct ToriDraw_Scene* scene_events_of;
    /* TORIRS_FRAME_DEBUG counters — painted commands that did / did not become
     * draws. Diagnostic only; nothing reads them outside the debug print. */
    int dbg_emit_element;
    int dbg_emit_terrain;
    int dbg_drop_not_live;
    int dbg_drop_no_model;
    /** One-shot latch so the wev trace reports the FIRST draw of each descent
     * and not all hundred of them. Cleared by frame_view_push. */
    bool dbg_view_traced;
    /** Sub-step within UITREE_EMIT_SCROLLBAR_V/H expansion (0 = not mid-bar). */
    int scrollbar_step;
    /**
     * Scene ids of a sprite-drawn vertical scrollbar, or all zero for the
     * client's own filled-rect one. @see UITreeScrollbarSkinPiece.
     *
     * On the FRAME and not on each desc, because it is one fact about the
     * client and not a property of a particular bar: every scrollbar on screen
     * belongs to the same frame and wears the same art. A copy per desc would
     * be twenty-four bytes on every draw descriptor to say the same six
     * numbers over and over.
     */
    int scrollbar_skin[UITREE_SCROLLBAR_SKIN_COUNT];
    bool in_world;
    bool world_begun;
    bool has_queued;
    /** Cursor into ToriDraw_SceneEvents for unload/clear → TORIRSRC_* drain. */
    int event_index;
    struct ToriRS_RenderCommand queued;
    struct ToriRS_RenderCommand_Begin3D pending_begin_3d;
};

void
ToriRS_FrameInit(struct ToriRS_Frame* frame);

void
ToriRS_FrameSetEmit(
    struct ToriRS_Frame* frame,
    struct UITreeEmitDesc const* cmds,
    int count);

void
ToriRS_FrameSetEmitBuffer(
    struct ToriRS_Frame* frame,
    struct UITreeEmitBuffer const* buf);

void
ToriRS_FrameSetScene(
    struct ToriRS_Frame* frame,
    struct ToriDraw_Scene* scene);

/**
 * Dress every vertical scrollbar in art, or (NULL) put them back in paint.
 *
 * `pieces` is UITREE_SCROLLBAR_SKIN_COUNT scene ids in UITreeScrollbarSkinPiece
 * order; a zero anywhere in it is refused as a whole, because a bar missing
 * one of its six is worse than one drawn the old way.
 */
void
ToriRS_FrameSetScrollbarSkin(
    struct ToriRS_Frame* frame,
    int const* pieces);

void
ToriRS_FrameSetCanvas(
    struct ToriRS_Frame* frame,
    int width,
    int height);

/** Viewport for a UITREE_EMIT_WORLD desc — shared by the render pass and the
 * pick pass so hittest and drawn pixels always agree. */
void
ToriRS_Frame_BuildWorldViewPort(
    struct UITreeEmitDesc const* desc,
    int canvas_w,
    int canvas_h,
    struct ToriDraw_ViewPort* out);

/** Optional world draw inputs for UITREE_EMIT_WORLD. */
void
ToriRS_FrameSetWorld(
    struct ToriRS_Frame* frame,
    struct World* world,
    struct PaintersBuffer* painters,
    struct ToriDraw_Camera const* camera,
    int cam_x,
    int cam_y,
    int cam_z);

/**
 * Drop every world-entity transform, keeping the root. Called once per frame
 * before the entity walk: a despawned boat must not leave a stale World here.
 */
void
ToriRS_FrameClearViewXforms(struct ToriRS_Frame* frame);

/**
 * Bind one world entity's view (SAILING_PLAN C3). `view_id` is 1..15 — view 0
 * is the root and is set by ToriRS_FrameSetWorld. `translate_*` is the entity
 * pose in its PARENT view's scene-local fine units; `recenter_*` is
 * `-size_tiles*64 - cfgPivot`. The reserved flatten/HSL slots are set to
 * identity here; C4 grows the setter rather than the caller.
 */
void
ToriRS_FrameSetViewXform(
    struct ToriRS_Frame* frame,
    int view_id,
    struct World* world,
    int recenter_x,
    int recenter_z,
    int translate_x,
    int translate_y,
    int translate_z,
    int yaw);

void
ToriRS_FrameBegin(struct ToriRS_Frame* frame);

/*
 * The element id of the painter command `distance` (1..3) after the one the
 * last ToriRS_FrameNextCommand emitted, when the emit loop's lookahead has
 * already resolved it; -1 outside the world pass, past the end, at a view
 * marker, or when it is not resolved. A renderer uses it to warm its own
 * per-element tables a step ahead of the dispatch that reads them.
 */
int
ToriRS_FrameLookaheadElementId(
    const struct ToriRS_Frame* frame,
    int distance);

bool
ToriRS_FrameNextCommand(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out);

void
ToriRS_FrameEnd(struct ToriRS_Frame* frame);

/*
 * Painter-command stepping (the v0 client's `cc` cap, docs/ORANGE_WEDGE.md).
 *
 * A limit >= 0 truncates the world pass after that many painter commands —
 * the painter's stream is back-to-front, so limit N shows exactly what the
 * scene looks like after the N-th paint and nothing later. -1 = unlimited.
 *
 * Seeded from TORIRS_PAINT_LIMIT on first use; TORIRS_PAINT_LIMIT_STEP=<s>
 * then advances the limit by s every frame (pair with TORIRS_BMP_SERIES for a
 * flip-book of the paint order). The app's debug keys (J/K +-1, L/, +-100,
 * I toggles unlimited) call the setter for interactive runs.
 */
void
ToriRS_Frame_PaintLimitSet(int limit);

int
ToriRS_Frame_PaintLimitGet(void);

#endif
