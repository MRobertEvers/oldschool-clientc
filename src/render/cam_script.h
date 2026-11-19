#ifndef SRC_RENDER_CAM_SCRIPT_H
#define SRC_RENDER_CAM_SCRIPT_H

/*
 * The game's scripted camera: where the CAM_* packets have told the eye to go
 * and what to look at, how it gets there, and a record of every camera packet
 * the client has run.
 *
 * The packets set a destination, not a position. The eye keeps walking toward
 * its target every frame for as long as the script is up -- a flat rate plus
 * rate2/1000 of what is left -- and only a rate2 of 100 or more cuts straight
 * there. The timed variants (MOVETO_CYCLES, MOVETO_ARC, ROTATETO*, ROTATEBY,
 * LOOKAT_CYCLES) instead ease from where the camera was to the target over a
 * number of client cycles on one of 28 easing curves, and hold at the end
 * until the next plain MOVETO, LOOKAT or RESET. Each rotation axis runs its
 * own ease, so a ROTATEBY can restart one and leave the other in flight.
 * Heights are measured up from the ground under the tile, not in world space.
 *
 * Shake is per axis and all five can run at once: the encounter scripts send
 * one packet per axis and expect them to compound. A frame's roll is taken
 * once and shared by every paint of that frame while views are attached, so
 * an offscreen paint consumes no rand() of its own.
 *
 * CAM_MODE 1 is the free camera, whose focus the keys move instead of the
 * player; the oculus fields are that focus's speed, heading and height.
 * CAM_UNLOCK widens the pitch range the caller clamps to.
 *
 * `serial` and `events` are the read side: every camera packet the caller
 * records bumps the serial and lands one event in a ring, in world tiles. The
 * quest driver reads them; nothing that draws does.
 *
 * Nothing here reads a world, terrain, a view or a clock. The caller passes
 * the client cycle, measures heights against the ground under a point, aims
 * the eye at a point and clamps a pitch to the camera's limits, converts a
 * packet's tiles between scene and world, and writes the eye and angles this
 * hands back into its views.
 */

#include "render/cam_arc.h"

#include <stdint.h>

/** Which CAM_* packet a CamScriptEvent records. */
enum CamScriptOp
{
    CAM_SCRIPT_OP_NONE = 0,
    CAM_SCRIPT_OP_MOVETO = 1,
    CAM_SCRIPT_OP_LOOKAT = 2,
    CAM_SCRIPT_OP_SHAKE = 3,
    CAM_SCRIPT_OP_RESET = 4,
    CAM_SCRIPT_OP_MOVETO_CYCLES = 5,
    CAM_SCRIPT_OP_ROTATETO_COORDINATE = 6,
    CAM_SCRIPT_OP_MOVETO_ARC = 7,
    CAM_SCRIPT_OP_SMOOTHRESET = 8,
    CAM_SCRIPT_OP_ROTATEBY = 9,
    CAM_SCRIPT_OP_ROTATETO = 10,
    CAM_SCRIPT_OP_LOOKAT_CYCLES = 11,
};

/** Size of CamScript.events: the camera packets the driver can read back.
 *  A cutscene sends one to four per tick and the driver polls every frame. */
#define CAM_SCRIPT_EVENTS 64

/** Shake axes: x, y, z, yaw, pitch. */
#define CAM_SCRIPT_SHAKE_AXES 5

/** The flat turn rate the look-at ease follows its point at: class61.field571
 *  (100) in the client's 2048 angle units. */
#define CAM_SCRIPT_LOOK_CYCLES_FLAT_RATE 12

/** CAM_SHAKE's per-frame roll, shared by every paint of one frame. */
struct CamScriptShakeRoll
{
    uint64_t frame;
    int valid;
    int jitter[CAM_SCRIPT_SHAKE_AXES];
};

/**
 * CAM_MOVETO_CYCLES: the eye eased from where it was to a target over
 * `duration` client cycles (deob class568). While `active`, it replaces the
 * rate walk; once done it holds the eye at the end until the next
 * CAM_MOVETO/CAM_RESET. `y0`/`y1` are absolute scene y when `ignore_terrain`,
 * else height above the ground under the eye.
 */
struct CamScriptMoveCycles
{
    int active;
    int x0, z0, y0, x1, z1, y1;
    int ignore_terrain;
    int start_cycle, duration, easing;
    /** CAM_MOVETO_ARC (deob class561): x/z run along `arc_path`
     *  (render/cam_arc.h); y eases as above. */
    int arc;
    struct CamArc arc_path;
};

/** One axis of a timed turn. */
struct CamScriptAxisEase
{
    int active;
    int from, to;
    int start_cycle, duration, easing;
};

/**
 * CAM_ROTATETO_COORDINATE, CAM_ROTATEBY, CAM_ROTATETO: pitch and yaw eased
 * (deob class563 x2; class594 x2 at 241). Held at the end until the next
 * CAM_LOOKAT/CAM_RESET, like the move.
 */
struct CamScriptRotateCycles
{
    int active;
    struct CamScriptAxisEase pitch, yaw;
};

/**
 * CAM_LOOKAT_CYCLES: the look-at point eased to a tile, in world units
 * (deob class586). Done when it arrives; the look_lx/lz/height it finished on
 * then hold.
 */
struct CamScriptLookCycles
{
    int active;
    int x0, z0, height0;
    int x1, z1, height1;
    int start_cycle, duration, easing;
};

/** One camera packet as the read side sees it. */
struct CamScriptEvent
{
    int serial;
    int op;               /* enum CamScriptOp */
    int world_x, world_z; /* MOVETO/LOOKAT target tile; -1 otherwise */
    int height;           /* MOVETO/LOOKAT height; SHAKE amplitude */
    int rate, rate2;      /* MOVETO/LOOKAT speeds; SHAKE axis, speed */
    int tick;             /* the server tick the caller says it arrived on */
};

struct CamScript
{
    int scripted; /* 1 while a CAM_MOVETO/LOOKAT script overrides free-fly */
    /** CAM_SMOOTHRESET (deob client.field698): `scripted` stays 1 while the
     *  eye glides back to the follow camera, then both clear. */
    int smooth_reset;
    /** Views that reached the follow camera this frame; the glide ends when
     *  every one has. */
    int smooth_reset_arrived;
    /** Scene-local tiles, heights above the ground, and the walk's rates. */
    int move_lx, move_lz, move_height, move_rate, move_rate2;
    int look_lx, look_lz, look_height, look_rate, look_rate2;
    struct CamScriptMoveCycles move_cycles;
    struct CamScriptRotateCycles rotate_cycles;
    struct CamScriptLookCycles look_cycles;
    /** The last CAM_MOVETO's height: what a height-relative move adds to
     *  (deob Statics.field6040). */
    int last_move_height;
    int shake[CAM_SCRIPT_SHAKE_AXES];
    int shake_jitter[CAM_SCRIPT_SHAKE_AXES];    /* random spread either side of the sine */
    int shake_amplitude[CAM_SCRIPT_SHAKE_AXES]; /* sine amplitude */
    int shake_speed[CAM_SCRIPT_SHAKE_AXES];     /* sine rate, hundredths */
    int shake_cycle[CAM_SCRIPT_SHAKE_AXES];
    /** The shake's per-frame roll, shared by every paint of one frame while
     *  views are attached. */
    struct CamScriptShakeRoll shake_roll;
    /** CAM_MODE (deob client.java field1275): 1 is the free camera. */
    int cam_mode;
    /** OCULUS_SYNC's last value (field1183). */
    int oculus_sync;
    /** Bring the focus back to the player (field1184). */
    int oculus_recentre;
    /** The focus height, once the free camera has one (field5690). */
    int oculus_y_valid;
    int oculus_y;
    /** How fast the focus is moving, and which way (field1180..1182). */
    int oculus_speed;
    int oculus_angle;
    int oculus_dy;
    /** The keys, sampled with the frame's input. */
    int oculus_w, oculus_s, oculus_a, oculus_d, oculus_r, oculus_f, oculus_shift;
    /** Camera packets recorded this session; events[serial % RING] is the
     *  newest once serial > 0. */
    int serial;
    struct CamScriptEvent events[CAM_SCRIPT_EVENTS];
    /** CAM_UNLOCK: nonzero widens the pitch limits to the deob's unlocked
     *  range (client.java field758). */
    int unlock;
    /* CAM_TARGET: what the follow camera follows instead of the local player.
     * 0 is the local player (the login value, deob client.java:1734); else the
     * wire's type (CAM_SCRIPT_TARGET_*) and that entity's index. */
    int target_set;
    int target_type;
    int target_index;
};

/** CAM_TARGET's wire types (deob class284). */
#define CAM_SCRIPT_TARGET_PLAYER 0
#define CAM_SCRIPT_TARGET_NPC 1
#define CAM_SCRIPT_TARGET_WORLDENTITY 2
#define CAM_SCRIPT_TARGET_COORD 3

/* --- the arithmetic ------------------------------------------------------ */

/**
 * How far along (0..1) an ease is after `elapsed` of `duration` client cycles
 * on curve `easing`. A port of the deob's Statics.method7961, case for case
 * (ids per RSProx CameraEaseFunction: 0 linear, 1/2/3 sine in/out/in-out,
 * 4..6 quad, 7..9 cubic, 10..12 quart, 13..15 quint, 16..18 expo, 19..21 circ,
 * 22..24 back, 25..27 elastic). An id outside 0..27 is linear, as class560's
 * constructor makes it. A duration of 0 or less is already there.
 */
double
CamScript_Ease(
    int elapsed,
    int duration,
    int easing);

/** One step of the rate walk: `current` toward `target` by a flat `rate`
 *  plus `rate2`/1000 of what is left, never overshooting. */
int
CamScript_Walk(
    int current,
    int target,
    int rate,
    int rate2);

/** The same walk for a yaw in 0..2047: the short way round, stopping on the
 *  target when the sign of the remaining turn flips. */
int
CamScript_WalkYaw(
    int current,
    int target,
    int rate,
    int rate2);

/* --- what the packets set ------------------------------------------------ */

/** CAM_MOVETO, scene-local tiles. Leaves the look half as it is and cancels a
 *  timed move and a glide. Returns 1 when rate2 cuts (100 or more): the caller
 *  then puts every view's eye on the target. */
int
CamScript_MoveTo(
    struct CamScript* cam,
    int local_x,
    int local_z,
    int height,
    int rate,
    int rate2);

/** CAM_LOOKAT, scene-local tiles. Leaves the move half as it is and cancels
 *  the timed turns and a glide. Returns 1 when rate2 cuts. */
int
CamScript_LookAt(
    struct CamScript* cam,
    int local_x,
    int local_z,
    int height,
    int rate,
    int rate2);

/** CAM_MOVETO_CYCLES, once the caller has measured both ends: scene units,
 *  y as described on CamScriptMoveCycles. */
void
CamScript_MoveCycles(
    struct CamScript* cam,
    int x0,
    int z0,
    int y0,
    int x1,
    int z1,
    int y1,
    int ignore_terrain,
    int cycle,
    int duration,
    int easing);

/** CAM_MOVETO_ARC after CamScript_MoveCycles: x/z follow the arc through the
 *  via point (scene units). A move that is not running takes no arc. */
void
CamScript_MoveArc(
    struct CamScript* cam,
    int via_x,
    int via_z);

/** The height a CAM_MOVETO_CYCLES means: added to the last CAM_MOVETO's
 *  when `height_relative`. */
int
CamScript_MoveHeightFor(
    struct CamScript const* cam,
    int height,
    int height_relative);

/** The look height a timed turn or look means: added to the current one
 *  when `height_relative`. */
int
CamScript_LookHeightFor(
    struct CamScript const* cam,
    int height,
    int height_relative);

/** Start the pitch axis's ease from `from` to `to`. */
void
CamScript_StartPitch(
    struct CamScript* cam,
    int from,
    int to,
    int cycle,
    int duration,
    int easing);

/** Start the yaw axis's ease from `from` to `to` as given: a delta names its
 *  own direction. */
void
CamScript_StartYaw(
    struct CamScript* cam,
    int from,
    int to,
    int cycle,
    int duration,
    int easing);

/** Start the yaw axis's ease from `from` to `yaw` the short way round (deob
 *  Statics.method8200). */
void
CamScript_StartYawShortWay(
    struct CamScript* cam,
    int from,
    int yaw,
    int cycle,
    int duration,
    int easing);

/** What every timed turn leaves behind: the turn running, the look ease off,
 *  the script up and no glide. */
void
CamScript_RotateBegun(struct CamScript* cam);

/** CAM_ROTATETO_COORDINATE, once the caller has aimed at the tile: both axes
 *  from where the camera is, yaw the short way, and the look height the
 *  packet means (CamScript_LookHeightFor). */
void
CamScript_RotateCycles(
    struct CamScript* cam,
    int pitch_from,
    int pitch_to,
    int yaw_from,
    int yaw_to,
    int look_height,
    int cycle,
    int duration,
    int easing);

/** CAM_LOOKAT_CYCLES, scene-local tiles: the look-at point eased from the
 *  last one named to this one. */
void
CamScript_LookCycles(
    struct CamScript* cam,
    int local_x,
    int local_z,
    int height,
    int height_relative,
    int cycle,
    int duration,
    int easing);

/** CAM_SHAKE on one axis, 0..4; the caller drops any other. */
void
CamScript_Shake(
    struct CamScript* cam,
    int axis,
    int spread,
    int amplitude,
    int speed);

/** Every axis's shake off. */
void
CamScript_StopShakes(struct CamScript* cam);

/** End the script: targets, heights and rates to 0 and every ease off
 *  (deob client.java class243.field2937 and the tail of Statics.method3868).
 *  Shakes are left. */
void
CamScript_Release(struct CamScript* cam);

/** CAM_RESET: the release and every shake off. */
void
CamScript_Reset(struct CamScript* cam);

/** CAM_SMOOTHRESET: a script that is up starts gliding back to the follow
 *  camera, its timed eases and shakes off and its targets and rates kept.
 *  Returns 1 when it started one; a script that is not up has nothing to
 *  glide. */
int
CamScript_SmoothReset(struct CamScript* cam);

/** The scene moved under the script (deob client.java's rebase): the script
 *  is down and every shake off; the targets and rates stay. */
void
CamScript_Rebase(struct CamScript* cam);

/** CAM_UNLOCK. Returns 1 when the value changed. */
int
CamScript_SetUnlock(
    struct CamScript* cam,
    int unlock);

int
CamScript_Unlocked(struct CamScript const* cam);

/** CAM_MODE: the mode, and the free camera's focus height forgotten. */
void
CamScript_SetMode(
    struct CamScript* cam,
    int mode);

/** OCULUS_SYNC: a new value while in the free camera brings the focus back
 *  to the player. */
void
CamScript_OculusSync(
    struct CamScript* cam,
    int value);

/** CAM_TARGET (deob client.java class243.field2935 / field3045): the follow
 *  camera follows entity `index` of `type`. An index of -1 is "none", the
 *  local player again. Returns 1 when the target changed. */
int
CamScript_SetTarget(
    struct CamScript* cam,
    int type,
    int index);

/** 1 and the type and index when a CAM_TARGET names something other than the
 *  local player. */
int
CamScript_Target(
    struct CamScript const* cam,
    int* out_type,
    int* out_index);

/** What a logout clears: the mode, the free camera's sync, recentre and focus
 *  height, and the CAM_TARGET (the login puts the local player back). */
void
CamScript_Logout(struct CamScript* cam);

/* --- what a frame reads and steps ---------------------------------------- */

/** A CAM_MOVETO/LOOKAT script (or its glide home) is up. */
int
CamScript_IsScripted(struct CamScript const* cam);

/** The script owns the eye this frame: up, and not gliding home. While it
 *  glides the follow camera computes where home is. */
int
CamScript_HoldsEye(struct CamScript const* cam);

void
CamScript_MoveTarget(
    struct CamScript const* cam,
    int* out_local_x,
    int* out_local_z,
    int* out_height);

void
CamScript_LookTarget(
    struct CamScript const* cam,
    int* out_local_x,
    int* out_local_z,
    int* out_height);

void
CamScript_MoveRates(
    struct CamScript const* cam,
    int* out_rate,
    int* out_rate2);

void
CamScript_LookRates(
    struct CamScript const* cam,
    int* out_rate,
    int* out_rate2);

/** The timed move at `cycle`, if one is running: x/z in scene units (on the
 *  arc when it has one), y as described on CamScriptMoveCycles. Returns 0
 *  and leaves the outputs alone when none is. */
int
CamScript_MoveCyclesAt(
    struct CamScript const* cam,
    int cycle,
    int* out_x,
    int* out_z,
    int* out_y,
    int* out_ignore_terrain);

/** A timed turn owns the angles. */
int
CamScript_Rotating(struct CamScript const* cam);

/** The pitch axis at `cycle`, before the caller's clamp. Returns 0 when that
 *  axis is not easing. */
int
CamScript_RotatePitchAt(
    struct CamScript const* cam,
    int cycle,
    int* out_pitch);

/** The yaw axis at `cycle`, in 0..2047. Returns 0 when that axis is not
 *  easing. */
int
CamScript_RotateYawAt(
    struct CamScript const* cam,
    int cycle,
    int* out_yaw);

/**
 * One frame of CAM_LOOKAT_CYCLES at `cycle`: the eased look-at point in scene
 * units and its height above the ground, which the turn follows at the flat
 * rate. Ends the ease once its duration has passed. Returns 0 when none is
 * running.
 */
int
CamScript_LookCyclesStep(
    struct CamScript* cam,
    int cycle,
    int* out_x,
    int* out_z,
    int* out_height);

/** The glide's frame begins: no view has arrived yet. */
void
CamScript_GlideFrameBegin(struct CamScript* cam);

/** One view reached the follow camera, or has nowhere to glide to. */
void
CamScript_GlideArrived(struct CamScript* cam);

/** The frame's end: a glide every one of `view_count` views has finished
 *  releases the script. */
void
CamScript_GlideEnd(
    struct CamScript* cam,
    int view_count);

/** The axis is shaking. */
int
CamScript_ShakeOn(
    struct CamScript const* cam,
    int axis);

/**
 * Roll the shake for frame `frame`: each shaking axis a sine plus a random
 * spread (rand()), its cycle advanced. When `shared`, a roll already taken
 * for this frame is reused instead, and nothing is rolled or advanced.
 */
void
CamScript_RollShake(
    struct CamScript* cam,
    int shared,
    uint64_t frame);

/** The last roll's jitter on `axis`. Returns 0 when the axis is not shaking. */
int
CamScript_ShakeJitter(
    struct CamScript const* cam,
    int axis,
    int* out_jitter);

/* --- the free camera ----------------------------------------------------- */

/** CAM_MODE 1: the keys move the focus. */
int
CamScript_FreeCamera(struct CamScript const* cam);

/** Escape leaves the free camera; nothing else changes. */
void
CamScript_ExitFreeCamera(struct CamScript* cam);

/** The free camera's keys, sampled with the frame's input. */
void
CamScript_OculusKeys(
    struct CamScript* cam,
    int w,
    int s,
    int a,
    int d,
    int r,
    int f,
    int shift);

/** OCULUS_SYNC asked for the focus to come back to the player. */
int
CamScript_OculusRecentrePending(struct CamScript const* cam);

/** The caller put the focus on the player, whose ground is `ground_y`: the
 *  focus height comes down to it if it was above, and the request is done. */
void
CamScript_OculusRecentred(
    struct CamScript* cam,
    int ground_y);

/**
 * The free camera's focus for one client cycle (deob Statics.method1034, mode
 * 1): the speed eases toward the held keys' and R/F lift the focus height,
 * never above 0. Returns 1 while the focus moves, with `step` in scene units
 * and `angle` the heading relative to the camera's yaw, -1 for none.
 */
int
CamScript_OculusStep(
    struct CamScript* cam,
    int* out_step,
    int* out_angle);

/** The free camera's focus height: `ground_y` the first time it is asked,
 *  then whatever the keys have made of it. */
int
CamScript_OculusFocusY(
    struct CamScript* cam,
    int ground_y);

/* --- the read side ------------------------------------------------------- */

/** Record one camera packet; `world_x`/`world_z` are world tiles. */
void
CamScript_Record(
    struct CamScript* cam,
    enum CamScriptOp op,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2,
    int tick);

/** Camera packets recorded this session. */
int
CamScript_Serial(struct CamScript const* cam);

/** The event with `serial`, 1..CamScript_Serial; the ring holds the newest
 *  CAM_SCRIPT_EVENTS of them. */
struct CamScriptEvent const*
CamScript_Event(
    struct CamScript const* cam,
    int serial);

#endif
