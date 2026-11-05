#include "input/torirs_gesture.h"

#include "cmd/cmdbus.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char const* const gesture_kind_names[LIBTORIRS_GESTURE_KIND_COUNT] = {
    "pan",
    "pinch",
    "twist",
};

static char const* const gesture_phase_names[LIBTORIRS_GESTURE_PHASE_COUNT] = {
    "begin",
    "update",
    "end",
};

static int16_t
gesture_coord(int v)
{
    if( v > 32767 )
        return 32767;
    if( v < -32768 )
        return -32768;
    return (int16_t)v;
}

/* The bookkeeping every frame of `kind` shares; returns the phase it goes out
 * with. */
static int
gesture_stream_note(
    struct ToriRS_GestureStream* stream,
    enum LibToriRS_GestureKind kind,
    int x,
    int y,
    unsigned flags,
    uint64_t now_ms)
{
    int phase = LIBTORIRS_GESTURE_UPDATE;

    if( !stream->active[kind] )
    {
        phase = LIBTORIRS_GESTURE_BEGIN;
        stream->active[kind] = 1;
        stream->flags[kind] = (uint8_t)flags;
    }
    stream->last_ms[kind] = now_ms;
    stream->x[kind] = x;
    stream->y[kind] = y;
    return phase;
}

void
ToriRS_GestureStreamReset(struct ToriRS_GestureStream* stream)
{
    assert(stream);
    memset(stream, 0, sizeof(*stream));
    stream->pending_scale = 1.0;
}

int
ToriRS_GestureStreamActive(
    struct ToriRS_GestureStream const* stream,
    enum LibToriRS_GestureKind kind)
{
    assert(stream);
    assert(kind < LIBTORIRS_GESTURE_KIND_COUNT);
    return stream->active[kind] != 0;
}

void
ToriRS_GestureStreamPan(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double dx,
    double dy,
    int fingers,
    unsigned flags,
    uint64_t now_ms)
{
    int phase;

    assert(stream);
    assert(bus);
    assert(fingers >= 1);
    assert(fingers <= 255);
    /* Two fingers pan and three pitch: a hand that adds a finger has started
     * another gesture, not continued this one. */
    if( stream->active[LIBTORIRS_GESTURE_PAN] && stream->pan_fingers != fingers )
        ToriRS_GestureStreamEnd(stream, bus, LIBTORIRS_GESTURE_PAN);
    phase = gesture_stream_note(stream, LIBTORIRS_GESTURE_PAN, x, y, flags, now_ms);
    stream->pan_fingers = (uint8_t)fingers;
    CmdBus_PushGesturePan(
        bus, gesture_coord(x), gesture_coord(y), dx, dy, fingers, phase, flags);
}

void
ToriRS_GestureStreamPinch(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double scale,
    unsigned flags,
    uint64_t now_ms)
{
    int phase;

    assert(stream);
    assert(bus);
    assert(scale > 0.0);
    phase = gesture_stream_note(stream, LIBTORIRS_GESTURE_PINCH, x, y, flags, now_ms);
    CmdBus_PushGesturePinch(bus, gesture_coord(x), gesture_coord(y), scale, phase, flags);
}

void
ToriRS_GestureStreamTwist(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double radians,
    unsigned flags,
    uint64_t now_ms)
{
    int phase;

    assert(stream);
    assert(bus);
    phase = gesture_stream_note(stream, LIBTORIRS_GESTURE_TWIST, x, y, flags, now_ms);
    CmdBus_PushGestureTwist(bus, gesture_coord(x), gesture_coord(y), radians, phase, flags);
}

/* Both pair steps; `may_begin` 0 is the final one, which only reports. */
static void
gesture_stream_pair(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double dx,
    double dy,
    double scale,
    double radians,
    int fingers,
    unsigned flags,
    uint64_t now_ms,
    int may_begin)
{
    assert(stream);
    assert(bus);
    assert(scale > 0.0);
    assert(fingers >= 1);
    assert(fingers <= 255);

    /* Each kind on its own: a begun kind reports whatever moved, an unbegun
     * one waits for its own motion to pass its own threshold. Not "one kind
     * per step" as the notch synthesis has to be -- a continuous consumer can
     * zoom and pan together, which is what two fingers on a map mean -- but
     * not "every kind always" either, or every pinch would twist a little. */
    if( stream->active[LIBTORIRS_GESTURE_PAN] && stream->pan_fingers == fingers )
    {
        if( dx != 0.0 || dy != 0.0 )
            ToriRS_GestureStreamPan(stream, bus, x, y, dx, dy, fingers, flags, now_ms);
    }
    else
    {
        stream->pending_dx += dx;
        stream->pending_dy += dy;
        if( may_begin &&
            sqrt(stream->pending_dx * stream->pending_dx + stream->pending_dy * stream->pending_dy) >=
            TORIRS_GESTURE_PAN_START_PX )
        {
            ToriRS_GestureStreamPan(
                stream, bus, x, y, stream->pending_dx, stream->pending_dy, fingers, flags, now_ms);
            stream->pending_dx = 0.0;
            stream->pending_dy = 0.0;
        }
    }

    if( stream->active[LIBTORIRS_GESTURE_PINCH] )
    {
        if( scale != 1.0 )
            ToriRS_GestureStreamPinch(stream, bus, x, y, scale, flags, now_ms);
    }
    else
    {
        stream->pending_scale *= scale;
        if( may_begin && fabs(stream->pending_scale - 1.0) >= TORIRS_GESTURE_PINCH_START )
        {
            ToriRS_GestureStreamPinch(stream, bus, x, y, stream->pending_scale, flags, now_ms);
            stream->pending_scale = 1.0;
        }
    }

    /* ALSO_LEGACY never rides a TWIST: no legacy route carries a turn (the
     * wheel, SCROLL and INPUT_PINCH have no angle), so a pair whose pan and
     * pinch the old way also delivered (SDL_MULTIGESTURE) still hands its
     * twist to the consumers that skip the doubled frames. */
    flags &= ~LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY;
    if( stream->active[LIBTORIRS_GESTURE_TWIST] )
    {
        if( radians != 0.0 )
            ToriRS_GestureStreamTwist(stream, bus, x, y, radians, flags, now_ms);
    }
    else
    {
        stream->pending_radians += radians;
        if( may_begin && fabs(stream->pending_radians) >= TORIRS_GESTURE_TWIST_START )
        {
            ToriRS_GestureStreamTwist(stream, bus, x, y, stream->pending_radians, flags, now_ms);
            stream->pending_radians = 0.0;
        }
    }
}

void
ToriRS_GestureStreamPair(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double dx,
    double dy,
    double scale,
    double radians,
    int fingers,
    unsigned flags,
    uint64_t now_ms)
{
    gesture_stream_pair(stream, bus, x, y, dx, dy, scale, radians, fingers, flags, now_ms, 1);
}

void
ToriRS_GestureStreamPairFinal(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double dx,
    double dy,
    double scale,
    double radians,
    int fingers,
    unsigned flags,
    uint64_t now_ms)
{
    gesture_stream_pair(stream, bus, x, y, dx, dy, scale, radians, fingers, flags, now_ms, 0);
}

void
ToriRS_GestureStreamEnd(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    enum LibToriRS_GestureKind kind)
{
    int16_t x;
    int16_t y;
    unsigned flags;

    assert(stream);
    assert(bus);
    assert(kind < LIBTORIRS_GESTURE_KIND_COUNT);
    if( !stream->active[kind] )
        return;
    stream->active[kind] = 0;
    x = gesture_coord(stream->x[kind]);
    y = gesture_coord(stream->y[kind]);
    flags = stream->flags[kind];
    if( kind == LIBTORIRS_GESTURE_PAN )
        CmdBus_PushGesturePan(
            bus, x, y, 0.0, 0.0, stream->pan_fingers, LIBTORIRS_GESTURE_END, flags);
    else if( kind == LIBTORIRS_GESTURE_PINCH )
        CmdBus_PushGesturePinch(bus, x, y, 1.0, LIBTORIRS_GESTURE_END, flags);
    else
        CmdBus_PushGestureTwist(bus, x, y, 0.0, LIBTORIRS_GESTURE_END, flags);
}

void
ToriRS_GestureStreamEndAll(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus)
{
    assert(stream);
    assert(bus);
    for( int k = 0; k < LIBTORIRS_GESTURE_KIND_COUNT; k++ )
        ToriRS_GestureStreamEnd(stream, bus, (enum LibToriRS_GestureKind)k);
    stream->pending_dx = 0.0;
    stream->pending_dy = 0.0;
    stream->pending_scale = 1.0;
    stream->pending_radians = 0.0;
}

void
ToriRS_GestureStreamIdle(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    uint64_t now_ms,
    uint64_t idle_ms)
{
    assert(stream);
    assert(bus);
    for( int k = 0; k < LIBTORIRS_GESTURE_KIND_COUNT; k++ )
        if( stream->active[k] && now_ms - stream->last_ms[k] >= idle_ms )
            ToriRS_GestureStreamEnd(stream, bus, (enum LibToriRS_GestureKind)k);
}

/* ---- TORIRS_SIM_GESTURE --------------------------------------------------- */

int
ToriRS_SimGestureCapacity(char const* text)
{
    int capacity = 1;

    assert(text);
    for( char const* c = text; *c; c++ )
        if( *c == ';' )
            capacity++;
    return capacity;
}

/* One "<frame>,<kind>,<x>,<y>,<a>[,<b>]" entry, no ';'. 0 and a reason on a
 * malformed one. */
static int
sim_gesture_parse_entry(
    char const* entry,
    struct ToriRS_SimGesture* out,
    char const** why)
{
    char* end = NULL;
    char kind[24];
    char const* name;
    size_t len;

    memset(out, 0, sizeof(*out));
    out->frame = strtol(entry, &end, 10);
    if( end == entry || *end != ',' || out->frame < 0 )
    {
        *why = "expected a frame number (0 or more) and a comma";
        return 0;
    }
    entry = end + 1;
    end = strchr(entry, ',');
    len = end ? (size_t)(end - entry) : 0;
    if( !end || len == 0 || len >= sizeof(kind) )
    {
        *why = "missing kind (pan, pan<N>, pinch, twist, touch-<kind>)";
        return 0;
    }
    memcpy(kind, entry, len);
    kind[len] = '\0';
    entry = end + 1;

    name = kind;
    if( strncmp(name, "touch-", 6) == 0 )
    {
        out->flags = LIBTORIRS_GESTURE_FLAG_TOUCH;
        name += 6;
    }
    out->fingers = 0;
    if( strcmp(name, "pan") == 0 )
    {
        out->kind = LIBTORIRS_GESTURE_PAN;
        out->fingers = 2;
    }
    else if( strncmp(name, "pan", 3) == 0 && name[3] >= '1' && name[3] <= '9' && name[4] == '\0' )
    {
        out->kind = LIBTORIRS_GESTURE_PAN;
        out->fingers = (uint8_t)(name[3] - '0');
    }
    else if( strcmp(name, "pinch") == 0 )
        out->kind = LIBTORIRS_GESTURE_PINCH;
    else if( strcmp(name, "twist") == 0 )
        out->kind = LIBTORIRS_GESTURE_TWIST;
    else
    {
        *why = "unknown kind (pan, pan<N>, pinch, twist, touch-<kind>)";
        return 0;
    }

    out->x = (int)strtol(entry, &end, 10);
    if( end == entry || *end != ',' )
    {
        *why = "expected <x>,<y> after the kind";
        return 0;
    }
    entry = end + 1;
    out->y = (int)strtol(entry, &end, 10);
    if( end == entry || *end != ',' )
    {
        *why = "expected <x>,<y> after the kind";
        return 0;
    }
    entry = end + 1;
    out->a = strtod(entry, &end);
    if( end == entry )
    {
        *why = "expected the amount <a>";
        return 0;
    }
    entry = end;
    if( out->kind == LIBTORIRS_GESTURE_PAN )
    {
        if( *entry != ',' )
        {
            *why = "a pan takes <dx>,<dy>";
            return 0;
        }
        entry++;
        out->b = strtod(entry, &end);
        if( end == entry )
        {
            *why = "a pan takes <dx>,<dy>";
            return 0;
        }
        entry = end;
    }
    if( *entry != '\0' )
    {
        *why = out->kind == LIBTORIRS_GESTURE_PAN ? "a pan takes exactly <dx>,<dy>"
                                                  : "a pinch or a twist takes one amount";
        return 0;
    }
    if( out->kind == LIBTORIRS_GESTURE_PINCH && !(out->a > 0.0) )
    {
        *why = "a pinch's ratio must be greater than 0";
        return 0;
    }
    return 1;
}

int
ToriRS_SimGestureParse(
    char const* text,
    struct ToriRS_SimGesture* out,
    int cap,
    char* err,
    size_t err_size)
{
    int count = 0;
    char entry[128];

    assert(text);
    assert(out);
    assert(cap >= ToriRS_SimGestureCapacity(text));
    (void)cap;
    assert(err);
    assert(err_size > 0);
    err[0] = '\0';
    for( char const* t = text; *t; )
    {
        char const* semi = strchr(t, ';');
        size_t len = semi ? (size_t)(semi - t) : strlen(t);
        char const* why = NULL;

        if( len >= sizeof(entry) )
        {
            snprintf(err, err_size, "entry too long: '%.40s...'", t);
            return -1;
        }
        memcpy(entry, t, len);
        entry[len] = '\0';
        if( len > 0 )
        {
            if( !sim_gesture_parse_entry(entry, &out[count], &why) )
            {
                snprintf(err, err_size, "bad entry '%s': %s", entry, why);
                return -1;
            }
            count++;
        }
        if( !semi )
            break;
        t = semi + 1;
    }
    return count;
}

void
ToriRS_SimGestureStateInit(struct ToriRS_SimGestureState* state)
{
    assert(state);
    memset(state, 0, sizeof(*state));
    state->last_frame = -1;
}

/* A sim gesture's END, at its last centre. */
static void
sim_gesture_end(
    struct ToriRS_SimGestureState* state,
    struct ToriRS_CmdBus* bus,
    int kind,
    long frame)
{
    int16_t const x = gesture_coord(state->x[kind]);
    int16_t const y = gesture_coord(state->y[kind]);
    unsigned const flags = state->flags[kind];

    state->active[kind] = 0;
    if( kind == LIBTORIRS_GESTURE_PAN )
        CmdBus_PushGesturePan(bus, x, y, 0.0, 0.0, state->pan_fingers, LIBTORIRS_GESTURE_END, flags);
    else if( kind == LIBTORIRS_GESTURE_PINCH )
        CmdBus_PushGesturePinch(bus, x, y, 1.0, LIBTORIRS_GESTURE_END, flags);
    else
        CmdBus_PushGestureTwist(bus, x, y, 0.0, LIBTORIRS_GESTURE_END, flags);
    /* TORIRS_REPORT, not TORIRS_LOG: the receipt for a lever someone set,
     * and OPT builds (every photograph) compile TORIRS_LOG away. */
    TORIRS_REPORT(
        "sim_gesture: frame=%ld %s end at %d,%d\n", frame, gesture_kind_names[kind], x, y);
}

void
ToriRS_SimGestureStep(
    struct ToriRS_SimGesture* entries,
    int count,
    struct ToriRS_SimGestureState* state,
    struct ToriRS_CmdBus* bus,
    long frame)
{
    uint8_t due[LIBTORIRS_GESTURE_KIND_COUNT];

    assert(state);
    assert(bus);
    if( count == 0 )
        return;
    assert(entries);
    if( frame == state->last_frame )
        return;
    state->last_frame = frame;

    memset(due, 0, sizeof(due));
    for( int i = 0; i < count; i++ )
        if( !entries[i].done && frame >= entries[i].frame )
            due[entries[i].kind] = 1;
    for( int k = 0; k < LIBTORIRS_GESTURE_KIND_COUNT; k++ )
        if( state->active[k] && !due[k] )
            sim_gesture_end(state, bus, k, frame);

    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_SimGesture* e = &entries[i];
        int phase;
        int16_t x;
        int16_t y;

        if( e->done || frame < e->frame )
            continue;
        e->done = 1;
        if( e->kind == LIBTORIRS_GESTURE_PAN && state->active[e->kind] && state->pan_fingers != e->fingers )
            sim_gesture_end(state, bus, e->kind, frame);
        phase = state->active[e->kind] ? LIBTORIRS_GESTURE_UPDATE : LIBTORIRS_GESTURE_BEGIN;
        if( phase == LIBTORIRS_GESTURE_BEGIN )
            state->flags[e->kind] = e->flags;
        state->active[e->kind] = 1;
        state->x[e->kind] = e->x;
        state->y[e->kind] = e->y;
        x = gesture_coord(e->x);
        y = gesture_coord(e->y);
        if( e->kind == LIBTORIRS_GESTURE_PAN )
        {
            state->pan_fingers = e->fingers;
            CmdBus_PushGesturePan(bus, x, y, e->a, e->b, e->fingers, phase, e->flags);
            TORIRS_REPORT(
                "sim_gesture: frame=%ld pan %s at %d,%d d=%.2f,%.2f fingers=%d%s\n",
                frame,
                gesture_phase_names[phase],
                x,
                y,
                e->a,
                e->b,
                e->fingers,
                (e->flags & LIBTORIRS_GESTURE_FLAG_TOUCH) ? " touch" : "");
        }
        else
        {
            if( e->kind == LIBTORIRS_GESTURE_PINCH )
                CmdBus_PushGesturePinch(bus, x, y, e->a, phase, e->flags);
            else
                CmdBus_PushGestureTwist(bus, x, y, e->a, phase, e->flags);
            TORIRS_REPORT(
                "sim_gesture: frame=%ld %s %s at %d,%d %s=%.4f%s\n",
                frame,
                gesture_kind_names[e->kind],
                gesture_phase_names[phase],
                x,
                y,
                e->kind == LIBTORIRS_GESTURE_PINCH ? "scale" : "radians",
                e->a,
                (e->flags & LIBTORIRS_GESTURE_FLAG_TOUCH) ? " touch" : "");
        }
    }
}
