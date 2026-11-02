/*
 * The SDL desktop's wheel-shaped events as the command bus sees them
 * (PlatformWindow_PollCommands): a mouse wheel is the integer notches it
 * always was plus a TORIRS_CMD_INPUT_SCROLL with whole deltas and no trackpad
 * flag; a trackpad's fractional deltas are a SCROLL flagged as a trackpad
 * (and integer notches only when SDL's own accumulation rounds one out); the
 * platform's natural-scrolling direction is kept on the exact deltas and
 * reported, while the integer wheel stays normalised as before; and a
 * multigesture with no fingers down pushes no pinch.
 *
 * The dummy driver: events are SDL_PushEvent'd into the main window. The
 * macOS trackpad path (NSEventTypeMagnify, hasPreciseScrollingDeltas) needs a
 * real NSEvent and cannot be staged here; on a Mac this exercises the SDL
 * path a mouse wheel takes.
 */
#include "platform/platform_sdl2_input.h"
#include "platform/platform_window.h"

#include "cmd/cmdbus.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition, ...)                                                        \
    do                                                                               \
    {                                                                                \
        checks++;                                                                    \
        if( !(condition) )                                                           \
        {                                                                            \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);                   \
            fprintf(stderr, __VA_ARGS__);                                            \
            fprintf(stderr, "\n");                                                   \
            failures++;                                                              \
        }                                                                            \
    } while( 0 )

struct Seen
{
    int wheels;
    int wheel_sum;
    int scrolls;
    struct ToriRS_CmdScroll scroll;
    int pinches;
};

static void
drain(struct ToriRS_CmdBus* bus, struct Seen* seen)
{
    struct ToriRS_CmdHeader header;
    unsigned char payload[TORIRS_CMD_MAX_PAYLOAD];

    memset(seen, 0, sizeof(*seen));
    while( CmdBus_Pop(bus, &header, payload) )
    {
        if( header.type == TORIRS_CMD_INPUT_MOUSE_WHEEL )
        {
            struct ToriRS_CmdMouseWheel wheel;
            memcpy(&wheel, payload, sizeof(wheel));
            seen->wheels++;
            seen->wheel_sum += wheel.wheel_y;
        }
        else if( header.type == TORIRS_CMD_INPUT_SCROLL )
        {
            seen->scrolls++;
            memcpy(&seen->scroll, payload, sizeof(seen->scroll));
        }
        else if( header.type == TORIRS_CMD_INPUT_PINCH )
            seen->pinches++;
    }
}

/* Straight into the translation: SDL's own queue (sdl2-compat's
 * SDL_PushEvent) does not carry the precise fields through. */
static void
push_wheel(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    Uint32 window_id,
    int y,
    float precise_x,
    float precise_y,
    int flipped)
{
    SDL_MouseWheelEvent event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEWHEEL;
    event.windowID = window_id;
    event.y = y;
    event.x = (int)precise_x;
    event.preciseX = precise_x;
    event.preciseY = precise_y;
    event.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
    PlatformSdl_PushWheel(platform, bus, &event);
}

/* The scroll's deltas in notches, which is what the platform's own precise
 * fields carry, whatever its pixels per notch. */
static double
notches(int32_t milli, uint16_t px_per_notch)
{
    return (double)milli / 1000.0 / (double)px_per_notch;
}

int
main(void)
{
    struct PlatformWindow* platform;
    struct ToriRS_CmdBus bus;
    struct Seen seen;
    SDL_Window* window;
    Uint32 id;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    platform = PlatformWindow_New();
    CHECK(platform != NULL, "platform allocated");
    if( !platform )
        return 1;
    CHECK(PlatformWindow_Init(platform, 640, 480, "gesture-test"), "SDL window opened");
    window = (SDL_Window*)PlatformWindow_GLWindow(platform);
    id = SDL_GetWindowID(window);
    CmdBus_Init(&bus);
    PlatformWindow_PollCommands(platform, &bus);
    drain(&bus, &seen);

    /* A mouse wheel: one notch up, whole deltas. */
    push_wheel(platform, &bus, id, 1, 0.0f, 1.0f, 0);
    drain(&bus, &seen);
    CHECK(seen.wheels == 1 && seen.wheel_sum == 1, "a notch is still one integer notch (%d, %d)",
        seen.wheels, seen.wheel_sum);
    CHECK(seen.scrolls == 1, "beside it the exact deltas (%d)", seen.scrolls);
    CHECK(!seen.scroll.trackpad, "a whole notch is a mouse, not a trackpad");
    CHECK(seen.scroll.px_per_notch > 0, "the scroll says what a notch is worth (%d)", seen.scroll.px_per_notch);
    CHECK(fabs(notches(seen.scroll.dy_milli, seen.scroll.px_per_notch) - 1.0) < 0.02,
        "and its dy is one notch of it (%.3f)", notches(seen.scroll.dy_milli, seen.scroll.px_per_notch));
    CHECK(seen.pinches == 0, "a wheel is never a pinch");

    /* A trackpad: fractions, no integer notch rounded out yet. */
    push_wheel(platform, &bus, id, 0, -0.25f, 0.3f, 0);
    drain(&bus, &seen);
    CHECK(seen.wheel_sum == 0, "a fraction of a notch moves no integer wheel (%d)", seen.wheel_sum);
    CHECK(seen.scrolls == 1 && seen.scroll.trackpad, "it is a scroll flagged as a trackpad");
    CHECK(fabs(notches(seen.scroll.dy_milli, seen.scroll.px_per_notch) - 0.3) < 0.02,
        "dy keeps the fraction (%.3f)", notches(seen.scroll.dy_milli, seen.scroll.px_per_notch));
    CHECK(fabs(notches(seen.scroll.dx_milli, seen.scroll.px_per_notch) + 0.25) < 0.02,
        "dx too, in SDL's sign (%.3f)", notches(seen.scroll.dx_milli, seen.scroll.px_per_notch));

    /* Natural scrolling: the integer wheel normalised as before, the exact
     * deltas left as the platform delivered them and flagged. */
    push_wheel(platform, &bus, id, 2, 0.0f, 2.0f, 1);
    drain(&bus, &seen);
    CHECK(seen.wheel_sum == -2, "a flipped wheel's notches are negated as before (%d)", seen.wheel_sum);
    CHECK(seen.scrolls == 1 && seen.scroll.flipped, "the scroll reports the flip");
    CHECK(notches(seen.scroll.dy_milli, seen.scroll.px_per_notch) > 1.9,
        "and keeps the platform's own sign (%.3f)", notches(seen.scroll.dy_milli, seen.scroll.px_per_notch));

    /* Through SDL's own queue a whole notch still arrives as a wheel. */
    {
        SDL_Event event;
        memset(&event, 0, sizeof(event));
        event.type = SDL_MOUSEWHEEL;
        event.wheel.windowID = id;
        event.wheel.y = 1;
        event.wheel.preciseY = 1.0f;
        SDL_PushEvent(&event);
        PlatformWindow_PollCommands(platform, &bus);
        drain(&bus, &seen);
        CHECK(seen.wheels == 1, "the poll loop routes SDL_MOUSEWHEEL to the translation (%d)", seen.wheels);
        CHECK(seen.scrolls == 1, "with its scroll (%d)", seen.scrolls);
    }

    /* A multigesture with no fingers down measures nothing. */
    {
        SDL_Event event;
        memset(&event, 0, sizeof(event));
        event.type = SDL_MULTIGESTURE;
        event.mgesture.touchId = 1;
        event.mgesture.numFingers = 2;
        event.mgesture.dDist = 0.1f;
        PlatformSdl_PushMultiGesture(platform, &bus, &event.mgesture);
        drain(&bus, &seen);
        CHECK(seen.pinches == 0, "no fingers, no pinch (%d)", seen.pinches);
    }

    PlatformWindow_Free(platform);
    printf("sdl_gesture_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
