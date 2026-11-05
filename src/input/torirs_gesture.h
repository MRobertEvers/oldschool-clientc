#ifndef SRC_INPUT_TORIRS_GESTURE_H
#define SRC_INPUT_TORIRS_GESTURE_H

#include "input/torirs_input.h"

#include <stddef.h>
#include <stdint.h>

struct ToriRS_CmdBus;

/*
 * The gesture frames' producers' half: who says BEGIN, who says END.
 *
 * A bus frame (TORIRS_CMD_GESTURE_PAN / _PINCH / _TWIST) is phased, and most
 * of what feeds one is not. A touchscreen knows when its fingers land and
 * lift; a trackpad's two-finger scroll reaches SDL as a stream of wheel events
 * with no start and no stop; SDL_MULTIGESTURE is a stream of deltas; Safari's
 * gesturechange has a start and an end but the ctrl+wheel the other browsers
 * send for the same pinch has neither. So every producer hands its motion to a
 * ToriRS_GestureStream, which says BEGIN the first time a kind moves, UPDATE
 * after, and END when the producer says the fingers left -- or, for a stream
 * that never says so, once it has been quiet for TORIRS_GESTURE_IDLE_END_MS.
 *
 * Two ways in:
 *
 *   ToriRS_GestureStreamPan / Pinch / Twist   one kind, no threshold: a wheel
 *       event is already deliberate, so it begins its gesture at once.
 *   ToriRS_GestureStreamPair                  all three from one step of two
 *       fingers (a touch pair, SDL_MULTIGESTURE). Each kind begins only once
 *       its own motion since the fingers landed passes its start threshold,
 *       and that BEGIN carries all of it, so nothing before the lock-in is
 *       lost and a pinch does not leave a jitter of twists behind it.
 *
 * The stream allocates nothing and reaches nothing but the bus.
 */

/** A stream that has said nothing for this long has ended. Longer than a
 *  trackpad's event gap (8..17 ms) at any frame rate the client draws at,
 *  shorter than the pause between two deliberate scrolls. */
#define TORIRS_GESTURE_IDLE_END_MS 120

/** ToriRS_GestureStreamPair's start thresholds, from the motion since the
 *  fingers landed. The pan's is the tap slop (TORIRS_TOUCH_SLOP): two fingers
 *  that wander less are resting. */
#define TORIRS_GESTURE_PAN_START_PX 12.0
/** |ratio - 1|: 8% is 16 px of spread on fingers 200 px apart, the touch
 *  policy's own pinch step. */
#define TORIRS_GESTURE_PINCH_START 0.08
/** About 8.6 degrees: well past the roll of two fingers pinching. */
#define TORIRS_GESTURE_TWIST_START 0.15

struct ToriRS_GestureStream
{
    /** BEGIN sent and END not yet, per enum LibToriRS_GestureKind. */
    uint8_t active[LIBTORIRS_GESTURE_KIND_COUNT];
    /** The flags each active kind began with, which its END repeats. */
    uint8_t flags[LIBTORIRS_GESTURE_KIND_COUNT];
    /** Each kind's last frame, for the idle END. */
    uint64_t last_ms[LIBTORIRS_GESTURE_KIND_COUNT];
    /** Each kind's last centre: where its END is. */
    int x[LIBTORIRS_GESTURE_KIND_COUNT];
    int y[LIBTORIRS_GESTURE_KIND_COUNT];
    /** The active pan's fingers. A pan whose finger count changes is a new
     *  pan (three fingers pitch where two pan). */
    uint8_t pan_fingers;
    /** ToriRS_GestureStreamPair's motion since the fingers landed, for each
     *  kind not yet begun. */
    double pending_dx;
    double pending_dy;
    double pending_scale;
    double pending_radians;
};

void
ToriRS_GestureStreamReset(struct ToriRS_GestureStream* stream);

/** Is `kind` between its BEGIN and its END? */
int
ToriRS_GestureStreamActive(
    struct ToriRS_GestureStream const* stream,
    enum LibToriRS_GestureKind kind);

/** One PAN step: BEGIN when no pan is active (or its fingers changed, which
 *  ends the old one first), UPDATE otherwise. fingers 1..255. */
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
    uint64_t now_ms);

/** One PINCH step; scale > 0. */
void
ToriRS_GestureStreamPinch(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double scale,
    unsigned flags,
    uint64_t now_ms);

/** One TWIST step. */
void
ToriRS_GestureStreamTwist(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    int x,
    int y,
    double radians,
    unsigned flags,
    uint64_t now_ms);

/**
 * One step of a finger pair: the centroid's travel, the spread's ratio and the
 * turn since the previous step, centred on (x, y). A kind already begun
 * reports any motion as an UPDATE; one not yet begun accumulates until its
 * threshold, then BEGINs with the lot. scale > 0; fingers 1..255.
 */
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
    uint64_t now_ms);

/**
 * The pair's last step before ToriRS_GestureStreamEndAll (a finger lifting or
 * landing): the motion since the previous step for the kinds already begun,
 * and NOTHING begins. A finger rolls as it lifts, and on two fingers 60 px
 * apart its 10 px is a 17% pinch -- which, begun here, would be a pinch that
 * starts and ends in the frame the pan stopped.
 */
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
    uint64_t now_ms);

/** END `kind` if it is active; a no-op otherwise (an END is idempotent). */
void
ToriRS_GestureStreamEnd(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    enum LibToriRS_GestureKind kind);

/** END every active kind and forget the pair's pending motion: the fingers
 *  left, or their number changed. */
void
ToriRS_GestureStreamEndAll(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus);

/** END every active kind that has been quiet for `idle_ms` or longer. Once a
 *  frame, for the producers that never say when they stop. */
void
ToriRS_GestureStreamIdle(
    struct ToriRS_GestureStream* stream,
    struct ToriRS_CmdBus* bus,
    uint64_t now_ms,
    uint64_t idle_ms);

/*
 * TORIRS_SIM_GESTURE, for photographs:
 *
 *   TORIRS_SIM_GESTURE="<frame>,<kind>,<x>,<y>,<a>[,<b>][;...]"
 *
 *   kind  pan      a = dx, b = dy (canvas px), two fingers
 *         pan<N>   the same with N fingers (1..9): pan3 is the touch pitch
 *         pinch    a = the ratio for that frame (2 = twice as far apart)
 *         twist    a = radians for that frame, + clockwise
 *         touch-<kind>   the same, flagged LIBTORIRS_GESTURE_FLAG_TOUCH
 *
 * Entries of one kind on consecutive frames are one gesture: the first is its
 * BEGIN, the rest UPDATEs, and its END goes out on the first frame after the
 * run with no entry of that kind. Frames need not be in order. (x, y) is the
 * centre; the sim moves no pointer.
 */
struct ToriRS_SimGesture
{
    long frame;
    uint8_t kind;    /* enum LibToriRS_GestureKind */
    uint8_t fingers; /* PAN */
    uint8_t flags;
    /** Pushed already. */
    uint8_t done;
    int x;
    int y;
    /** pan: dx; pinch: ratio; twist: radians. */
    double a;
    /** pan: dy. */
    double b;
};

struct ToriRS_SimGestureState
{
    /** The kind's gesture is between BEGIN and END. */
    uint8_t active[LIBTORIRS_GESTURE_KIND_COUNT];
    uint8_t flags[LIBTORIRS_GESTURE_KIND_COUNT];
    int x[LIBTORIRS_GESTURE_KIND_COUNT];
    int y[LIBTORIRS_GESTURE_KIND_COUNT];
    uint8_t pan_fingers;
    /** The last frame stepped, so a frame the loop repeats does not push
     *  twice; -1 before the first. */
    long last_frame;
};

/** An upper bound on the entries in `text`: one more than its ';'s. */
int
ToriRS_SimGestureCapacity(char const* text);

/**
 * Parse the whole variable into `out` (capacity `cap`, at least
 * ToriRS_SimGestureCapacity(text)). Returns the entry count, or -1 with a
 * reason in `err` naming the entry: a typo that pushed nothing would make a
 * photograph prove the wrong thing, so the caller stops the run.
 */
int
ToriRS_SimGestureParse(
    char const* text,
    struct ToriRS_SimGesture* out,
    int cap,
    char* err,
    size_t err_size);

void
ToriRS_SimGestureStateInit(struct ToriRS_SimGestureState* state);

/** One main-loop frame: the ENDs of runs that stopped, then the entries due
 *  (frame <= `frame`, not yet pushed), each logged as `sim_gesture:`. */
void
ToriRS_SimGestureStep(
    struct ToriRS_SimGesture* entries,
    int count,
    struct ToriRS_SimGestureState* state,
    struct ToriRS_CmdBus* bus,
    long frame);

#endif
