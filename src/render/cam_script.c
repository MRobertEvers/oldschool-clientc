/*
 * The game's scripted camera (render/cam_script.h).
 */
#include "render/cam_script.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#define CAM_SCRIPT_PI 3.14159265358979323846

/* --- the arithmetic ------------------------------------------------------ */

double
CamScript_Ease(
    int elapsed,
    int duration,
    int easing)
{
    const double c1 = 1.70158, c2 = 2.5949095, c3 = 2.70158;
    double t = 1.0;

    if( duration > 0 )
    {
        t = (double)elapsed / (double)duration;
        if( t < 0.0 )
            t = 0.0;
        if( t > 1.0 )
            t = 1.0;
    }
    if( t <= 0.0 )
        return 0.0;
    if( t >= 1.0 )
        return 1.0;
    switch( easing )
    {
    case 1: return 1.0 - cos(t * CAM_SCRIPT_PI / 2.0);
    case 2: return sin(t * CAM_SCRIPT_PI / 2.0);
    case 3: return -(cos(t * CAM_SCRIPT_PI) - 1.0) / 2.0;
    case 4: return t * t;
    case 5: return 1.0 - (1.0 - t) * (1.0 - t);
    case 6: return t < 0.5 ? 2.0 * t * t : 1.0 - pow(-2.0 * t + 2.0, 2.0) / 2.0;
    case 7: return t * t * t;
    case 8: return 1.0 - pow(1.0 - t, 3.0);
    case 9: return t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case 10: return t * t * t * t;
    case 11: return 1.0 - pow(1.0 - t, 4.0);
    case 12: return t < 0.5 ? 8.0 * t * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 4.0) / 2.0;
    case 13: return t * t * t * t * t;
    case 14: return 1.0 - pow(1.0 - t, 5.0);
    case 15: return t < 0.5 ? 16.0 * t * t * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 5.0) / 2.0;
    case 16: return pow(2.0, 10.0 * t - 10.0);
    case 17: return 1.0 - pow(2.0, -10.0 * t);
    /* The deob prints 2^(20t + 10) / 2 for the first half -- a value that is
     * not a curve on [0, 0.5) at all -- where easings.net (which the other 27
     * match term for term) has 2^(20t - 10) / 2. Taken as a decompiler sign
     * slip; the second half agrees. */
    case 18: return t < 0.5 ? pow(2.0, 20.0 * t - 10.0) / 2.0 : (2.0 - pow(2.0, -20.0 * t + 10.0)) / 2.0;
    case 19: return 1.0 - sqrt(1.0 - pow(t, 2.0));
    case 20: return sqrt(1.0 - pow(t - 1.0, 2.0));
    case 21:
        return t < 0.5 ? (1.0 - sqrt(1.0 - pow(2.0 * t, 2.0))) / 2.0
                       : (sqrt(1.0 - pow(-2.0 * t + 2.0, 2.0)) + 1.0) / 2.0;
    case 22: return c3 * t * t * t - c1 * t * t;
    case 23: return 1.0 + c3 * pow(t - 1.0, 3.0) + c1 * pow(t - 1.0, 2.0);
    case 24:
        return t < 0.5 ? (pow(2.0 * t, 2.0) * ((c2 + 1.0) * 2.0 * t - c2)) / 2.0
                       : (pow(2.0 * t - 2.0, 2.0) * ((c2 + 1.0) * (t * 2.0 - 2.0) + c2) + 2.0) / 2.0;
    case 25: return -pow(2.0, 10.0 * t - 10.0) * sin((t * 10.0 - 10.75) * (2.0 * CAM_SCRIPT_PI / 3.0));
    case 26: return pow(2.0, -10.0 * t) * sin((t * 10.0 - 0.75) * (2.0 * CAM_SCRIPT_PI / 3.0)) + 1.0;
    case 27:
    {
        double s = sin((20.0 * t - 11.125) * (4.0 * CAM_SCRIPT_PI / 9.0));
        return t < 0.5 ? -(pow(2.0, 20.0 * t - 10.0) * s) / 2.0 : pow(2.0, -20.0 * t + 10.0) * s / 2.0 + 1.0;
    }
    default: return t;
    }
}

int
CamScript_Walk(
    int current,
    int target,
    int rate,
    int rate2)
{
    if( current < target )
    {
        current += rate + (target - current) * rate2 / 1000;
        if( current > target )
            current = target;
    }
    else if( current > target )
    {
        current -= rate + (current - target) * rate2 / 1000;
        if( current < target )
            current = target;
    }
    return current;
}

int
CamScript_WalkYaw(
    int current,
    int target,
    int rate,
    int rate2)
{
    int delta = target - current;

    if( delta > 1024 )
        delta -= 2048;
    else if( delta < -1024 )
        delta += 2048;

    if( delta > 0 )
        current = (current + rate + delta * rate2 / 1000) & 0x7ff;
    else if( delta < 0 )
        current = (current - rate - -delta * rate2 / 1000) & 0x7ff;

    if( delta != 0 )
    {
        int remaining = target - current;
        if( remaining > 1024 )
            remaining -= 2048;
        else if( remaining < -1024 )
            remaining += 2048;
        if( (remaining < 0 && delta > 0) || (remaining > 0 && delta < 0) )
            current = target;
    }
    return current;
}

/* --- what the packets set ------------------------------------------------ */

int
CamScript_MoveTo(
    struct CamScript* cam,
    int local_x,
    int local_z,
    int height,
    int rate,
    int rate2)
{
    assert(cam);
    cam->move_lx = local_x;
    cam->move_lz = local_z;
    cam->move_height = height;
    cam->move_rate = rate;
    cam->move_rate2 = rate2;
    cam->move_cycles.active = 0;
    cam->last_move_height = height;
    /* The look half is left as it is (deob client.java, class243.field3026):
     * after a CAM_RESET its rates are 0, so the angles hold. */
    cam->scripted = 1;
    cam->smooth_reset = 0;
    return rate2 >= 100;
}

int
CamScript_LookAt(
    struct CamScript* cam,
    int local_x,
    int local_z,
    int height,
    int rate,
    int rate2)
{
    assert(cam);
    cam->look_lx = local_x;
    cam->look_lz = local_z;
    cam->look_height = height;
    cam->look_rate = rate;
    cam->look_rate2 = rate2;
    cam->rotate_cycles.active = 0;
    cam->look_cycles.active = 0;
    /* The move half is left as it is (deob client.java, class243.field3073):
     * after a CAM_RESET its rates are 0, so the eye holds where it was. */
    cam->scripted = 1;
    cam->smooth_reset = 0;
    return rate2 >= 100;
}

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
    int easing)
{
    struct CamScriptMoveCycles* m;

    assert(cam);
    m = &cam->move_cycles;
    m->x0 = x0;
    m->z0 = z0;
    m->x1 = x1;
    m->z1 = z1;
    m->ignore_terrain = ignore_terrain;
    m->y0 = y0;
    m->y1 = y1;
    m->start_cycle = cycle;
    m->duration = duration;
    m->easing = easing;
    m->arc = 0;
    m->active = 1;
    cam->scripted = 1;
    cam->smooth_reset = 0;
}

void
CamScript_MoveArc(
    struct CamScript* cam,
    int via_x,
    int via_z)
{
    struct CamScriptMoveCycles* m;

    assert(cam);
    m = &cam->move_cycles;
    if( !m->active )
        return;
    CamArc_Fit(&m->arc_path, m->x0, m->z0, via_x, via_z, m->x1, m->z1);
    m->arc = 1;
}

int
CamScript_MoveHeightFor(
    struct CamScript const* cam,
    int height,
    int height_relative)
{
    assert(cam);
    return height_relative ? cam->last_move_height + height : height;
}

int
CamScript_LookHeightFor(
    struct CamScript const* cam,
    int height,
    int height_relative)
{
    assert(cam);
    return height_relative ? cam->look_height + height : height;
}

static void
cam_script_axis_start(
    struct CamScriptAxisEase* axis,
    int from,
    int to,
    int cycle,
    int duration,
    int easing)
{
    axis->from = from;
    axis->to = to;
    axis->start_cycle = cycle;
    axis->duration = duration;
    axis->easing = easing;
    axis->active = 1;
}

void
CamScript_StartPitch(
    struct CamScript* cam,
    int from,
    int to,
    int cycle,
    int duration,
    int easing)
{
    assert(cam);
    cam_script_axis_start(&cam->rotate_cycles.pitch, from, to, cycle, duration, easing);
}

void
CamScript_StartYaw(
    struct CamScript* cam,
    int from,
    int to,
    int cycle,
    int duration,
    int easing)
{
    assert(cam);
    cam_script_axis_start(&cam->rotate_cycles.yaw, from, to, cycle, duration, easing);
}

void
CamScript_StartYawShortWay(
    struct CamScript* cam,
    int from,
    int yaw,
    int cycle,
    int duration,
    int easing)
{
    assert(cam);
    if( yaw - from > 1024 )
        yaw -= 2048;
    else if( yaw - from < -1024 )
        yaw += 2048;
    cam_script_axis_start(&cam->rotate_cycles.yaw, from, yaw, cycle, duration, easing);
}

/* deob field1150 = true, field1151 = false, field1281 = true,
 * field1243 = false. */
void
CamScript_RotateBegun(struct CamScript* cam)
{
    assert(cam);
    cam->rotate_cycles.active = 1;
    cam->look_cycles.active = 0;
    cam->scripted = 1;
    cam->smooth_reset = 0;
}

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
    int easing)
{
    assert(cam);
    CamScript_StartPitch(cam, pitch_from, pitch_to, cycle, duration, easing);
    CamScript_StartYawShortWay(cam, yaw_from, yaw_to, cycle, duration, easing);
    cam->look_height = look_height;
    CamScript_RotateBegun(cam);
}

void
CamScript_LookCycles(
    struct CamScript* cam,
    int local_x,
    int local_z,
    int height,
    int height_relative,
    int cycle,
    int duration,
    int easing)
{
    struct CamScriptLookCycles* l;

    assert(cam);
    l = &cam->look_cycles;
    l->x0 = cam->look_lx * 128 + 64;
    l->z0 = cam->look_lz * 128 + 64;
    l->height0 = cam->look_height;
    cam->look_lx = local_x;
    cam->look_lz = local_z;
    cam->look_height = height_relative ? cam->look_height + height : height;
    l->x1 = local_x * 128 + 64;
    l->z1 = local_z * 128 + 64;
    l->height1 = cam->look_height;
    l->start_cycle = cycle;
    l->duration = duration;
    l->easing = easing;
    l->active = 1;
    cam->rotate_cycles.active = 0;
    cam->scripted = 1;
    cam->smooth_reset = 0;
}

void
CamScript_Shake(
    struct CamScript* cam,
    int axis,
    int spread,
    int amplitude,
    int speed)
{
    assert(cam);
    assert(axis >= 0);
    assert(axis < CAM_SCRIPT_SHAKE_AXES);
    cam->shake[axis] = 1;
    cam->shake_jitter[axis] = spread;
    cam->shake_amplitude[axis] = amplitude;
    cam->shake_speed[axis] = speed;
    cam->shake_cycle[axis] = 0;
}

void
CamScript_StopShakes(struct CamScript* cam)
{
    assert(cam);
    for( int i = 0; i < CAM_SCRIPT_SHAKE_AXES; i++ )
        cam->shake[i] = 0;
}

void
CamScript_Release(struct CamScript* cam)
{
    assert(cam);
    cam->scripted = 0;
    cam->smooth_reset = 0;
    cam->move_lx = 0;
    cam->move_lz = 0;
    cam->move_height = 0;
    cam->move_rate = 0;
    cam->move_rate2 = 0;
    cam->look_lx = 0;
    cam->look_lz = 0;
    cam->look_height = 0;
    cam->look_rate = 0;
    cam->look_rate2 = 0;
    cam->move_cycles.active = 0;
    cam->rotate_cycles.active = 0;
    cam->look_cycles.active = 0;
}

void
CamScript_Reset(struct CamScript* cam)
{
    assert(cam);
    CamScript_Release(cam);
    CamScript_StopShakes(cam);
}

/* Deob client.java, class243.field2963 `&& field904`: only a script that is
 * up can glide back. */
int
CamScript_SmoothReset(struct CamScript* cam)
{
    assert(cam);
    if( !cam->scripted )
        return 0;
    cam->smooth_reset = 1;
    cam->move_cycles.active = 0;
    cam->rotate_cycles.active = 0;
    cam->look_cycles.active = 0;
    CamScript_StopShakes(cam);
    return 1;
}

void
CamScript_Rebase(struct CamScript* cam)
{
    assert(cam);
    cam->scripted = 0;
    CamScript_StopShakes(cam);
}

int
CamScript_SetUnlock(
    struct CamScript* cam,
    int unlock)
{
    assert(cam);
    if( cam->unlock == unlock )
        return 0;
    cam->unlock = unlock;
    return 1;
}

int
CamScript_Unlocked(struct CamScript const* cam)
{
    assert(cam);
    return cam->unlock;
}

void
CamScript_SetMode(
    struct CamScript* cam,
    int mode)
{
    assert(cam);
    cam->cam_mode = mode;
    cam->oculus_y_valid = 0;
}

/* deob client.java class259.field3148 -> Statics.method9192. */
void
CamScript_OculusSync(
    struct CamScript* cam,
    int value)
{
    assert(cam);
    if( cam->oculus_sync == value )
        return;
    cam->oculus_sync = value;
    if( cam->cam_mode == 1 )
        cam->oculus_recentre = 1;
}

void
CamScript_Logout(struct CamScript* cam)
{
    assert(cam);
    cam->cam_mode = 0;
    cam->oculus_sync = 0;
    cam->oculus_recentre = 0;
    cam->oculus_y_valid = 0;
    cam->target_set = 0;
    cam->target_type = 0;
    cam->target_index = 0;
}

int
CamScript_SetTarget(
    struct CamScript* cam,
    int type,
    int index)
{
    int const set = index >= 0;

    assert(cam);
    if( cam->target_set == set && (!set || (cam->target_type == type && cam->target_index == index)) )
        return 0;
    cam->target_set = set;
    cam->target_type = set ? type : 0;
    cam->target_index = set ? index : 0;
    return 1;
}

int
CamScript_Target(
    struct CamScript const* cam,
    int* out_type,
    int* out_index)
{
    assert(cam);
    assert(out_type);
    assert(out_index);
    *out_type = cam->target_type;
    *out_index = cam->target_index;
    return cam->target_set;
}

/* --- what a frame reads and steps ---------------------------------------- */

int
CamScript_IsScripted(struct CamScript const* cam)
{
    assert(cam);
    return cam->scripted;
}

int
CamScript_HoldsEye(struct CamScript const* cam)
{
    assert(cam);
    return cam->scripted && !cam->smooth_reset;
}

void
CamScript_MoveTarget(
    struct CamScript const* cam,
    int* out_local_x,
    int* out_local_z,
    int* out_height)
{
    assert(cam);
    assert(out_local_x);
    assert(out_local_z);
    assert(out_height);
    *out_local_x = cam->move_lx;
    *out_local_z = cam->move_lz;
    *out_height = cam->move_height;
}

void
CamScript_LookTarget(
    struct CamScript const* cam,
    int* out_local_x,
    int* out_local_z,
    int* out_height)
{
    assert(cam);
    assert(out_local_x);
    assert(out_local_z);
    assert(out_height);
    *out_local_x = cam->look_lx;
    *out_local_z = cam->look_lz;
    *out_height = cam->look_height;
}

void
CamScript_MoveRates(
    struct CamScript const* cam,
    int* out_rate,
    int* out_rate2)
{
    assert(cam);
    assert(out_rate);
    assert(out_rate2);
    *out_rate = cam->move_rate;
    *out_rate2 = cam->move_rate2;
}

void
CamScript_LookRates(
    struct CamScript const* cam,
    int* out_rate,
    int* out_rate2)
{
    assert(cam);
    assert(out_rate);
    assert(out_rate2);
    *out_rate = cam->look_rate;
    *out_rate2 = cam->look_rate2;
}

int
CamScript_MoveCyclesAt(
    struct CamScript const* cam,
    int cycle,
    int* out_x,
    int* out_z,
    int* out_y,
    int* out_ignore_terrain)
{
    struct CamScriptMoveCycles const* m;
    double f;
    int x, z;

    assert(cam);
    assert(out_x);
    assert(out_z);
    assert(out_y);
    assert(out_ignore_terrain);
    m = &cam->move_cycles;
    if( !m->active )
        return 0;
    f = CamScript_Ease(cycle - m->start_cycle, m->duration, m->easing);
    x = m->x0 + (int)lround((m->x1 - m->x0) * f);
    z = m->z0 + (int)lround((m->z1 - m->z0) * f);
    if( m->arc )
        CamArc_At(&m->arc_path, f, &x, &z);
    *out_x = x;
    *out_z = z;
    *out_y = m->y0 + (int)lround((m->y1 - m->y0) * f);
    *out_ignore_terrain = m->ignore_terrain;
    return 1;
}

int
CamScript_Rotating(struct CamScript const* cam)
{
    assert(cam);
    return cam->rotate_cycles.active;
}

int
CamScript_RotatePitchAt(
    struct CamScript const* cam,
    int cycle,
    int* out_pitch)
{
    struct CamScriptAxisEase const* a;
    double f;

    assert(cam);
    assert(out_pitch);
    a = &cam->rotate_cycles.pitch;
    if( !a->active )
        return 0;
    f = CamScript_Ease(cycle - a->start_cycle, a->duration, a->easing);
    *out_pitch = a->from + (int)lround((a->to - a->from) * f);
    return 1;
}

int
CamScript_RotateYawAt(
    struct CamScript const* cam,
    int cycle,
    int* out_yaw)
{
    struct CamScriptAxisEase const* a;
    double f;

    assert(cam);
    assert(out_yaw);
    a = &cam->rotate_cycles.yaw;
    if( !a->active )
        return 0;
    f = CamScript_Ease(cycle - a->start_cycle, a->duration, a->easing);
    *out_yaw = (a->from + (int)lround((a->to - a->from) * f)) & 0x7ff;
    return 1;
}

/* Deob Statics.method234's field1243 branch: the look-at point eases and the
 * ordinary turn follows it at a flat rate that outlasts the ease; on arrival
 * the ordinary look-at resumes on the same point. */
int
CamScript_LookCyclesStep(
    struct CamScript* cam,
    int cycle,
    int* out_x,
    int* out_z,
    int* out_height)
{
    struct CamScriptLookCycles* l;
    int elapsed;
    double f;

    assert(cam);
    assert(out_x);
    assert(out_z);
    assert(out_height);
    l = &cam->look_cycles;
    if( !l->active )
        return 0;
    elapsed = cycle - l->start_cycle;
    f = CamScript_Ease(elapsed, l->duration, l->easing);
    *out_x = l->x0 + (int)lround((l->x1 - l->x0) * f);
    *out_z = l->z0 + (int)lround((l->z1 - l->z0) * f);
    *out_height = l->height0 + (int)lround((l->height1 - l->height0) * f);
    cam->look_rate = CAM_SCRIPT_LOOK_CYCLES_FLAT_RATE;
    if( elapsed >= l->duration )
        l->active = 0;
    return 1;
}

void
CamScript_GlideFrameBegin(struct CamScript* cam)
{
    assert(cam);
    cam->smooth_reset_arrived = 0;
}

void
CamScript_GlideArrived(struct CamScript* cam)
{
    assert(cam);
    cam->smooth_reset_arrived++;
}

/* The tail of Statics.method3868, once per frame. */
void
CamScript_GlideEnd(
    struct CamScript* cam,
    int view_count)
{
    assert(cam);
    if( !cam->smooth_reset || !cam->scripted )
        return;
    if( cam->smooth_reset_arrived < view_count )
        return;
    CamScript_Release(cam);
}

int
CamScript_ShakeOn(
    struct CamScript const* cam,
    int axis)
{
    assert(cam);
    assert(axis >= 0);
    assert(axis < CAM_SCRIPT_SHAKE_AXES);
    return cam->shake[axis];
}

/* Reference Client-TS 4448: each axis is a sine plus a random spread, and all
 * five compound. */
void
CamScript_RollShake(
    struct CamScript* cam,
    int shared,
    uint64_t frame)
{
    assert(cam);
    if( shared && cam->shake_roll.valid && cam->shake_roll.frame == frame )
        return;
    for( int axis = 0; axis < CAM_SCRIPT_SHAKE_AXES; axis++ )
    {
        int spread, jitter;
        cam->shake_roll.jitter[axis] = 0;
        if( !cam->shake[axis] )
            continue;
        spread = cam->shake_jitter[axis];
        jitter = (int)((double)rand() / ((double)RAND_MAX + 1.0) * (spread * 2 + 1)) - spread;
        jitter += (int)(sin((double)cam->shake_cycle[axis] * ((double)cam->shake_speed[axis] / 100.0)) *
                        cam->shake_amplitude[axis]);
        cam->shake_roll.jitter[axis] = jitter;
        cam->shake_cycle[axis]++;
    }
    cam->shake_roll.frame = frame;
    cam->shake_roll.valid = 1;
}

int
CamScript_ShakeJitter(
    struct CamScript const* cam,
    int axis,
    int* out_jitter)
{
    assert(cam);
    assert(axis >= 0);
    assert(axis < CAM_SCRIPT_SHAKE_AXES);
    assert(out_jitter);
    if( !cam->shake[axis] )
        return 0;
    *out_jitter = cam->shake_roll.jitter[axis];
    return 1;
}

/* --- the free camera ----------------------------------------------------- */

int
CamScript_FreeCamera(struct CamScript const* cam)
{
    assert(cam);
    return cam->cam_mode == 1;
}

void
CamScript_ExitFreeCamera(struct CamScript* cam)
{
    assert(cam);
    cam->cam_mode = 0;
}

void
CamScript_OculusKeys(
    struct CamScript* cam,
    int w,
    int s,
    int a,
    int d,
    int r,
    int f,
    int shift)
{
    assert(cam);
    cam->oculus_w = w;
    cam->oculus_s = s;
    cam->oculus_a = a;
    cam->oculus_d = d;
    cam->oculus_r = r;
    cam->oculus_f = f;
    cam->oculus_shift = shift;
}

int
CamScript_OculusRecentrePending(struct CamScript const* cam)
{
    assert(cam);
    return cam->oculus_recentre;
}

void
CamScript_OculusRecentred(
    struct CamScript* cam,
    int ground_y)
{
    assert(cam);
    if( cam->oculus_y_valid && ground_y < cam->oculus_y )
        cam->oculus_y = ground_y;
    cam->oculus_recentre = 0;
}

int
CamScript_OculusStep(
    struct CamScript* cam,
    int* out_step,
    int* out_angle)
{
    int angle = -1;
    int dy = 0;
    int want = 0;

    assert(cam);
    assert(out_step);
    assert(out_angle);
    if( cam->oculus_w )
        angle = 0;
    else if( cam->oculus_s )
        angle = 1024;
    if( cam->oculus_a )
        angle = angle == 0 ? 1792 : angle == 1024 ? 1280 : 1536;
    else if( cam->oculus_d )
        angle = angle == 0 ? 256 : angle == 1024 ? 768 : 512;
    if( cam->oculus_r )
        dy = -1;
    else if( cam->oculus_f )
        dy = 1;
    if( angle >= 0 || dy != 0 )
    {
        /* field1005 (6) with Shift, field1277 (12) without. */
        want = (cam->oculus_shift ? 6 : 12) * 16;
        cam->oculus_angle = angle;
        cam->oculus_dy = dy;
    }
    if( cam->oculus_speed < want )
    {
        cam->oculus_speed += want / 8;
        if( cam->oculus_speed > want )
            cam->oculus_speed = want;
    }
    else if( cam->oculus_speed > want )
        cam->oculus_speed = cam->oculus_speed * 8 / 10;
    if( cam->oculus_speed > 0 )
    {
        int const step = cam->oculus_speed / 16;

        *out_step = step;
        *out_angle = cam->oculus_angle;
        if( cam->oculus_dy != 0 && cam->oculus_y_valid )
        {
            cam->oculus_y += cam->oculus_dy * step;
            if( cam->oculus_y > 0 )
                cam->oculus_y = 0;
        }
        return 1;
    }
    cam->oculus_angle = -1;
    cam->oculus_dy = 0;
    *out_step = 0;
    *out_angle = -1;
    return 0;
}

int
CamScript_OculusFocusY(
    struct CamScript* cam,
    int ground_y)
{
    assert(cam);
    if( !cam->oculus_y_valid )
    {
        cam->oculus_y = ground_y;
        cam->oculus_y_valid = 1;
    }
    return cam->oculus_y;
}

/* --- the read side ------------------------------------------------------- */

void
CamScript_Record(
    struct CamScript* cam,
    enum CamScriptOp op,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2,
    int tick)
{
    struct CamScriptEvent* event;

    assert(cam);
    cam->serial++;
    event = &cam->events[cam->serial % CAM_SCRIPT_EVENTS];
    event->serial = cam->serial;
    event->op = (int)op;
    event->world_x = world_x;
    event->world_z = world_z;
    event->height = height;
    event->rate = rate;
    event->rate2 = rate2;
    event->tick = tick;
}

int
CamScript_Serial(struct CamScript const* cam)
{
    assert(cam);
    return cam->serial;
}

struct CamScriptEvent const*
CamScript_Event(
    struct CamScript const* cam,
    int serial)
{
    assert(cam);
    assert(serial > 0);
    return &cam->events[serial % CAM_SCRIPT_EVENTS];
}
