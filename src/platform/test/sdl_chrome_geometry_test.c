/*
 * The attached plugin pane's WINDOW-SIZE policy, and nothing else: no
 * executor, no pixels, so it runs on the macOS backend too (whose pane pixels
 * belong to a WKWebView and whose sdl_chrome_test therefore cannot pass).
 *
 * The policy under test (platform_window.h, "Growth is a courtesy"):
 *   - the rail and the page grow the window only where the display has room:
 *     in place, or after sliding the window left by the overhang when there
 *     is room on that side; otherwise the pane is carved out of the game
 *     area -- unless that would take the game area below its floor, where
 *     the window grows off the edge of the display instead;
 *   - Close gives back exactly what was grown, floored at the canvas minimum
 *     plus the rail that stays, and never moves the window back;
 *   - a pane that changed inside the frame still relayouts the canvas: the
 *     pump pushes TORIRS_CMD_WINDOW_RESIZE with the game area, since no
 *     SIZE_CHANGED will;
 *   - PlatformWindow_SetWindowSize and the fixed-mode snap size the GAME AREA
 *     and keep the pane's points beside it.
 *
 * The dummy driver has a 1024x768 display and honours SDL_SetWindowPosition
 * and SDL_SetWindowSize, which is what lets every case be staged; the
 * maximised/fullscreen lock cannot be (the dummy driver never sets those
 * flags), so that arm is a code-reading guarantee only.
 */
#include "platform/platform_window.h"

#include "cmd/cmdbus.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, message)                                                   \
    do                                                                               \
    {                                                                                \
        if( !(condition) )                                                           \
        {                                                                            \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            failures++;                                                              \
        }                                                                            \
    } while( 0 )

#define CHECK_EQ(actual, expected, message)                                          \
    do                                                                               \
    {                                                                                \
        int const check_actual = (actual);                                           \
        int const check_expected = (expected);                                       \
        if( check_actual != check_expected )                                         \
        {                                                                            \
            fprintf(                                                                 \
                stderr,                                                              \
                "FAIL: %s: %d, expected %d (%s:%d)\n",                               \
                message,                                                             \
                check_actual,                                                        \
                check_expected,                                                      \
                __FILE__,                                                            \
                __LINE__);                                                           \
            failures++;                                                              \
        }                                                                            \
    } while( 0 )

#define MIN_W 400
#define MIN_H 300
#define INIT_H 400 /* above the floor, so fixed mode remembers the resizable size */
#define RAIL_POINTS 42

static int
window_width(SDL_Window* window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    return width;
}

static int
window_height(SDL_Window* window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    return height;
}

static int
window_x(SDL_Window* window)
{
    int x_position = 0;
    int y_position = 0;
    SDL_GetWindowPosition(window, &x_position, &y_position);
    return x_position;
}

/* The LAST resize the pump pushed this poll, or -1 when it pushed none. */
static int
take_resize_width(struct ToriRS_CmdBus* bus)
{
    struct ToriRS_CmdHeader header;
    unsigned char payload[TORIRS_CMD_MAX_PAYLOAD];
    int width = -1;

    while( CmdBus_Pop(bus, &header, payload) )
        if( header.type == TORIRS_CMD_WINDOW_RESIZE )
        {
            struct ToriRS_CmdWindowResize resize;
            memcpy(&resize, payload, sizeof(resize));
            width = resize.width;
        }
    return width;
}

static int
poll_resize_width(struct PlatformWindow* platform, struct ToriRS_CmdBus* bus)
{
    PlatformWindow_PollCommands(platform, bus);
    return take_resize_width(bus);
}

/* Stage a window of `width` points with its left edge at `x_position`, and
 * settle the pump so the next poll reports only what the pane does. */
static void
stage_window(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    SDL_Window* window,
    int x_position,
    int width)
{
    SDL_SetWindowSize(window, width, INIT_H);
    SDL_SetWindowPosition(window, x_position, 0);
    (void)poll_resize_width(platform, bus);
}

int
main(void)
{
    struct PlatformWindow* platform;
    struct ToriRS_CmdBus bus;
    SDL_Window* window;
    SDL_Rect usable;
    int display_w;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    platform = PlatformWindow_New();
    CHECK(platform != NULL, "platform allocated");
    CHECK(PlatformWindow_Init(platform, 500, INIT_H, "chrome-geometry-test"), "SDL window opened");
    if( !platform )
        return 1;
    window = (SDL_Window*)PlatformWindow_GLWindow(platform);
    CHECK(window != NULL, "the top-level window is exposed");
    CmdBus_Init(&bus);
    PlatformWindow_SetCanvasFollowsWindow(platform, &bus, true, MIN_W, MIN_H);
    (void)take_resize_width(&bus);

    if( SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(window), &usable) != 0 )
    {
        fprintf(stderr, "no display bounds: %s\n", SDL_GetError());
        return 1;
    }
    printf("display usable %dx%d at %d,%d\n", usable.w, usable.h, usable.x, usable.y);
    CHECK(usable.x == 0 && usable.y == 0 && usable.w >= 1000 && usable.w <= 1400,
        "the harness is staged for the dummy driver's display");
    display_w = usable.w;

    /* ---- A. no room anywhere: the window spans the display ------------- */
    stage_window(platform, &bus, window, 0, display_w);

    CHECK(PlatformWindow_ChromeRailOpen(platform, RAIL_POINTS, "Plugins"), "rail opens");
    CHECK_EQ(window_width(window), display_w, "a rail with no room does not widen the window");
    CHECK_EQ(window_x(window), 0, "nor move it");
    CHECK_EQ(PlatformWindow_ChromeWidth(platform), RAIL_POINTS, "the rail still gets its full allocation");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - RAIL_POINTS,
        "the pump relayouts the canvas to the game area the rail left");

    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens");
    CHECK_EQ(window_width(window), display_w, "a page with no room does not widen the window");
    CHECK_EQ(window_height(window), INIT_H, "nor heighten it: both axes are decided together");
    CHECK_EQ(PlatformWindow_ChromePageWidth(platform), 360, "the page still gets its full allocation");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - RAIL_POINTS - 360,
        "the pump relayouts the canvas to the game area the page left");

    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 320), "page narrows");
    CHECK_EQ(window_width(window), display_w, "narrowing an unfunded page does not move the window");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - RAIL_POINTS - 320,
        "the game area takes the width the page gave up");

    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), display_w, "closing a page that never grew the window never shrinks it");
    CHECK(!PlatformWindow_ChromeIsOpen(platform), "the page is closed");
    CHECK_EQ(PlatformWindow_ChromeWidth(platform), RAIL_POINTS, "the rail stays");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - RAIL_POINTS,
        "the game area gets the page's width back");

    /* ---- B. room in place: attached grow, and Close gives it back ------- */
    stage_window(platform, &bus, window, 0, 500);

    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens with room");
    CHECK_EQ(window_width(window), 500 + 360, "a page with room widens the window by the page");
    CHECK_EQ(window_x(window), 0, "and leaves it where it was");
    CHECK_EQ(window_height(window), 480, "and heightens it to the page's request");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "the game area is untouched by a funded page");

    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 320), "funded page narrows");
    CHECK_EQ(window_width(window), 500 + 320, "the window follows a funded page narrower");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "the game area is still untouched");
    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 360), "funded page widens");
    CHECK_EQ(window_width(window), 500 + 360, "and wider, while there is room");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "the game area is still untouched");

    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), 500, "Close gives back exactly what was grown");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "and the game area is where it started");

    /* ---- C. room only after a slide: the frame moves left by the overhang */
    stage_window(platform, &bus, window, display_w - 600, 500);

    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens near the right edge");
    CHECK_EQ(window_width(window), 500 + 360, "the page is funded in full");
    CHECK_EQ(window_x(window), display_w - 860, "by sliding the window left exactly the overhang");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "the game area is untouched");

    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 400), "funded page widens at the edge");
    CHECK_EQ(window_width(window), 500 + 400, "the widening is funded too");
    CHECK_EQ(window_x(window), display_w - 900, "with another slide of exactly its overhang");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "the game area is still untouched");

    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 300), "funded page narrows at the edge");
    CHECK_EQ(window_width(window), 500 + 300, "the window gives back the difference");
    CHECK_EQ(window_x(window), display_w - 900, "and stays where the slides put it");

    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), 500, "Close gives back what was grown");
    CHECK_EQ(window_x(window), display_w - 900, "and never slides the window back");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS, "the game area is where it started");

    /* ---- D. no room on either side, game area above its floor: it pays -- */
    stage_window(platform, &bus, window, 0, display_w - 100);

    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens with no room on either side");
    CHECK_EQ(window_width(window), display_w - 100, "the window is left alone");
    CHECK_EQ(window_x(window), 0, "and not moved");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - 100 - RAIL_POINTS - 360,
        "the game area pays for the page");
    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 300), "an unfunded page narrows");
    CHECK_EQ(window_width(window), display_w - 100, "nothing to give back: the window is left alone");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - 100 - RAIL_POINTS - 300,
        "the game area takes the width back");
    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), display_w - 100, "Close has nothing to give back");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - 100 - RAIL_POINTS,
        "the game area has the page's width back");

    /* ---- E. no room, and the carve would breach the floor: grow anyway -- */
    PlatformWindow_SetCanvasFollowsWindow(platform, &bus, true, display_w - 100 - RAIL_POINTS - 200, MIN_H);
    (void)take_resize_width(&bus);
    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens against the floor");
    CHECK_EQ(window_width(window), display_w - 100 + 360,
        "a carve below the floor grows the window off the display instead");
    CHECK_EQ(window_x(window), 0, "without moving it");
    CHECK_EQ(poll_resize_width(platform, &bus), display_w - 100 - RAIL_POINTS,
        "the game area keeps its width");
    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), display_w - 100, "Close gives the overhang back");
    PlatformWindow_SetCanvasFollowsWindow(platform, &bus, true, MIN_W, MIN_H);
    (void)take_resize_width(&bus);

    /* ---- F. the user narrowed the window while a funded page was open --- */
    stage_window(platform, &bus, window, 0, 500);
    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens again");
    CHECK_EQ(window_width(window), 500 + 360, "funded again");
    SDL_SetWindowSize(window, 520, INIT_H);
    (void)poll_resize_width(platform, &bus);
    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), MIN_W + RAIL_POINTS,
        "Close never shrinks below the canvas floor plus the rail that stays");
    (void)poll_resize_width(platform, &bus);

    /* ---- G. explicit sizes are the GAME AREA; the pane keeps its points - */
    PlatformWindow_SetWindowSize(platform, 600, INIT_H);
    CHECK_EQ(window_width(window), 600 + RAIL_POINTS, "SetWindowSize sizes the game area beside the rail");
    CHECK_EQ(poll_resize_width(platform, &bus), 600, "and the canvas is that game area");

    PlatformWindow_SetCanvasFollowsWindow(platform, &bus, false, MIN_W, MIN_H);
    CHECK_EQ(window_width(window), MIN_W + RAIL_POINTS, "the fixed-mode snap keeps the rail's points");
    (void)take_resize_width(&bus);
    PlatformWindow_SetCanvasFollowsWindow(platform, &bus, true, MIN_W, MIN_H);
    CHECK_EQ(window_width(window), 600 + RAIL_POINTS, "leaving fixed mode restores the resizable size");
    CHECK_EQ(take_resize_width(&bus), 600, "and pushes the game area, not the whole drawable");

    PlatformWindow_Free(platform);
    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("SDL plugin chrome window-size policy: ok\n");
    return 0;
}
