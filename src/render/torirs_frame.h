#ifndef SRC_RENDER_TORIRS_FRAME_H
#define SRC_RENDER_TORIRS_FRAME_H

#include "render/torirs_render.h"

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
    /* TORIRS_FRAME_DEBUG counters — painted commands that did / did not become
     * draws. Diagnostic only; nothing reads them outside the debug print. */
    int dbg_emit_element;
    int dbg_emit_terrain;
    int dbg_drop_not_live;
    int dbg_drop_no_model;
    /** Sub-step within UITREE_EMIT_SCROLLBAR_V/H expansion (0 = not mid-bar). */
    int scrollbar_step;
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

void
ToriRS_FrameBegin(struct ToriRS_Frame* frame);

bool
ToriRS_FrameNextCommand(
    struct ToriRS_Frame* frame,
    struct ToriRS_RenderCommand* out);

void
ToriRS_FrameEnd(struct ToriRS_Frame* frame);

#endif
