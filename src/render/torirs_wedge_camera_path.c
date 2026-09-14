#include "render/torirs_wedge_camera_path.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
ToriRS_WedgeCameraPathParse(
    char const* spec,
    struct ToriRS_WedgeCameraPath* out_path)
{
    char mode[16];
    char wrap[16];
    long frames = 0;
    int consumed = 0;
    int count = 0;
    int i;
    char const* cursor = spec;

    assert(spec);
    assert(out_path);

    if( sscanf(cursor, " %15[^, ] , %ld , %15[^, ;]%n", mode, &frames, wrap, &consumed) != 3 )
        return 0;
    cursor += consumed;

    if( strcmp(mode, "linear") == 0 )
        out_path->mode = TORIRS_WEDGE_CAMERA_LINEAR;
    else if( strcmp(mode, "spline") == 0 )
        out_path->mode = TORIRS_WEDGE_CAMERA_SPLINE;
    else
        return 0;

    if( strcmp(wrap, "loop") == 0 )
        out_path->wrap = TORIRS_WEDGE_CAMERA_LOOP;
    else if( strcmp(wrap, "pingpong") == 0 )
        out_path->wrap = TORIRS_WEDGE_CAMERA_PINGPONG;
    else if( strcmp(wrap, "hold") == 0 )
        out_path->wrap = TORIRS_WEDGE_CAMERA_HOLD;
    else
        return 0;

    if( frames <= 0 )
        return 0;
    out_path->frames = frames;

    while( count < TORIRS_WEDGE_CAMERA_PATH_MAX )
    {
        struct ToriRS_WedgeCameraKey key;

        while( *cursor == ' ' )
            cursor++;
        if( *cursor != ';' )
            break;
        cursor++;
        consumed = 0;
        if( sscanf(
                cursor,
                " %d , %d , %d , %d , %d%n",
                &key.x,
                &key.y,
                &key.z,
                &key.pitch,
                &key.yaw,
                &consumed) != 5 )
            return 0;
        out_path->keys[count] = key;
        count++;
        cursor += consumed;
    }
    while( *cursor == ' ' )
        cursor++;
    if( *cursor != '\0' )
        return 0;
    if( count < 2 )
        return 0;
    out_path->count = count;

    /* Unwrap yaw into a monotone sequence before anything interpolates it.
     * 2048 units is a full turn, so 1900 -> 100 is +248, not -1800:
     * interpolating the raw pair would spin the camera almost the whole way
     * round to reach somewhere it was already next to. A deliberate orbit
     * still works -- give it four waypoints a quarter turn apart and every hop
     * is an unambiguous +512. */
    for( i = 1; i < count; i++ )
    {
        int delta = (out_path->keys[i].yaw - out_path->keys[i - 1].yaw) % 2048;
        if( delta > 1024 )
            delta -= 2048;
        if( delta < -1024 )
            delta += 2048;
        out_path->keys[i].yaw = out_path->keys[i - 1].yaw + delta;
    }

    out_path->turn = 0;
    if( out_path->wrap == TORIRS_WEDGE_CAMERA_LOOP )
    {
        /* The closing leg gets the same shortest-arc treatment, and what it
         * adds is what one lap is worth in yaw. */
        int delta = (out_path->keys[0].yaw - out_path->keys[count - 1].yaw) % 2048;
        if( delta > 1024 )
            delta -= 2048;
        if( delta < -1024 )
            delta += 2048;
        out_path->turn = (out_path->keys[count - 1].yaw + delta) - out_path->keys[0].yaw;
    }
    return 1;
}

/* Waypoint by index, with the index allowed to run off both ends -- Catmull-Rom
 * needs the neighbours of the leg it is on. A loop wraps and carries the lap's
 * worth of yaw with it; the others clamp, which duplicates the endpoint and is
 * the usual way to terminate a Catmull-Rom. */
static void
torirs_wedge_camera_path_key(
    struct ToriRS_WedgeCameraPath const* path,
    int index,
    struct ToriRS_WedgeCameraKey* out_key)
{
    int wrapped;
    long laps = 0;

    assert(path);
    assert(out_key);
    assert(path->count > 0);

    if( path->wrap == TORIRS_WEDGE_CAMERA_LOOP )
    {
        wrapped = index % path->count;
        laps = (index - wrapped) / path->count;
        if( wrapped < 0 )
        {
            wrapped += path->count;
            laps -= 1;
        }
    }
    else
    {
        wrapped = index;
        if( wrapped < 0 )
            wrapped = 0;
        if( wrapped > path->count - 1 )
            wrapped = path->count - 1;
    }
    *out_key = path->keys[wrapped];
    out_key->yaw = (int)(out_key->yaw + laps * path->turn);
}

/* Fixed point rather than float, all the way through: the phase has to be a
 * bit-exact function of the frame ordinal on every build this suite compares. */
static int
torirs_wedge_camera_lerp(
    int p0,
    int p1,
    int64_t t)
{
    return (int)((int64_t)p0 + ((((int64_t)p1 - (int64_t)p0) * t) >> 12));
}

static int
torirs_wedge_camera_spline(
    int p0,
    int p1,
    int p2,
    int p3,
    int64_t t)
{
    int64_t t2 = (t * t) >> 12;
    int64_t t3 = (t2 * t) >> 12;
    int64_t a = 2 * (int64_t)p1;
    int64_t b = (int64_t)p2 - (int64_t)p0;
    int64_t c = 2 * (int64_t)p0 - 5 * (int64_t)p1 + 4 * (int64_t)p2 - (int64_t)p3;
    int64_t d = -(int64_t)p0 + 3 * (int64_t)p1 - 3 * (int64_t)p2 + (int64_t)p3;

    return (int)((a + ((b * t) >> 12) + ((c * t2) >> 12) + ((d * t3) >> 12)) >> 1);
}

void
ToriRS_WedgeCameraPathEval(
    struct ToriRS_WedgeCameraPath const* path,
    long frame,
    struct ToriRS_WedgeCameraKey* out_key)
{
    struct ToriRS_WedgeCameraKey k0;
    struct ToriRS_WedgeCameraKey k1;
    struct ToriRS_WedgeCameraKey k2;
    struct ToriRS_WedgeCameraKey k3;
    int64_t segments;
    int64_t phase;
    int64_t step;
    int64_t t;
    int index;

    assert(path);
    assert(out_key);
    assert(path->count >= 2);
    assert(path->frames > 0);

    segments = (path->wrap == TORIRS_WEDGE_CAMERA_LOOP) ? path->count : path->count - 1;

    phase = frame;
    if( path->wrap == TORIRS_WEDGE_CAMERA_PINGPONG )
    {
        int64_t span = (int64_t)path->frames * 2;
        phase = frame % span;
        if( phase < 0 )
            phase += span;
        if( phase > path->frames )
            phase = span - phase;
    }
    else if( path->wrap == TORIRS_WEDGE_CAMERA_LOOP )
    {
        phase = frame % path->frames;
        if( phase < 0 )
            phase += path->frames;
    }
    else
    {
        if( phase < 0 )
            phase = 0;
        if( phase > path->frames )
            phase = path->frames;
    }

    /* Position along the whole path in Q12, so an uneven frames/segments split
     * does not park the camera on a waypoint for a frame and then jump. */
    step = (phase * segments * 4096) / path->frames;
    index = (int)(step >> 12);
    t = step & 4095;
    if( index >= segments )
    {
        index = (int)segments - 1;
        t = 4096;
    }

    torirs_wedge_camera_path_key(path, index - 1, &k0);
    torirs_wedge_camera_path_key(path, index, &k1);
    torirs_wedge_camera_path_key(path, index + 1, &k2);
    torirs_wedge_camera_path_key(path, index + 2, &k3);

    if( path->mode == TORIRS_WEDGE_CAMERA_SPLINE )
    {
        out_key->x = torirs_wedge_camera_spline(k0.x, k1.x, k2.x, k3.x, t);
        out_key->y = torirs_wedge_camera_spline(k0.y, k1.y, k2.y, k3.y, t);
        out_key->z = torirs_wedge_camera_spline(k0.z, k1.z, k2.z, k3.z, t);
        out_key->pitch = torirs_wedge_camera_spline(k0.pitch, k1.pitch, k2.pitch, k3.pitch, t);
        out_key->yaw = torirs_wedge_camera_spline(k0.yaw, k1.yaw, k2.yaw, k3.yaw, t);
    }
    else
    {
        out_key->x = torirs_wedge_camera_lerp(k1.x, k2.x, t);
        out_key->y = torirs_wedge_camera_lerp(k1.y, k2.y, t);
        out_key->z = torirs_wedge_camera_lerp(k1.z, k2.z, t);
        out_key->pitch = torirs_wedge_camera_lerp(k1.pitch, k2.pitch, t);
        out_key->yaw = torirs_wedge_camera_lerp(k1.yaw, k2.yaw, t);
    }

    /* Back into 0..2047. The unwrapped yaw above can be many turns from there,
     * and a Catmull-Rom overshoots its control points -- so normalise both
     * angles rather than hand a trig table an index it never expected. */
    out_key->pitch = ((out_key->pitch % 2048) + 2048) % 2048;
    out_key->yaw = ((out_key->yaw % 2048) + 2048) % 2048;
}
