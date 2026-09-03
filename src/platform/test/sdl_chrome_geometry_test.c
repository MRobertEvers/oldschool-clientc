/*
 * The attached plugin pane's WINDOW-SIZE policy, and nothing else: no
 * executor, no pixels, so it runs on the macOS backend too (whose pane pixels
 * belong to a WKWebView and whose sdl_chrome_test therefore cannot pass).
 *
 * The policy under test (platform_window.h, "Growth is a courtesy"):
 *   - the rail and the page grow the window only where the display has room
 *     to the right; otherwise the pane is carved out of the game area;
 *   - Close gives back exactly what was grown, floored at the canvas minimum
 *     plus the rail that stays;
 *   - a pane that changed inside the frame still relayouts the canvas: the
 *     pump pushes TORIRS_CMD_WINDOW_RESIZE with the game area, since no
 *     SIZE_CHANGED will;
 *   - PlatformWindow_SetWindowSize and the fixed-mode snap size the GAME AREA
 *     and keep the pane's points beside it.
 *
 * The dummy driver centres an undefined position on a 1024x768 display and
 * honours SDL_SetWindowPosition, which is what lets "no room" and "room" both
 * be staged; the maximised/fullscreen lock cannot be (the dummy driver never
 * sets those flags), so that arm is a code-reading guarantee only.
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

int
main(void)
{
    struct PlatformWindow* platform;
    struct ToriRS_CmdBus bus;
    SDL_Window* window;
    SDL_Rect usable;
    int right_edge_x;
    int game_w;

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
    CHECK(usable.w >= 900 && usable.w < 500 + RAIL_POINTS + 360 + 360,
        "the harness needs a display that fits one page but not two");
    right_edge_x = usable.x + usable.w - window_width(window);

    /* ---- no room to the right: everything opens inside the frame -------- */
    SDL_SetWindowPosition(window, right_edge_x, usable.y);
    CHECK_EQ(window_width(window), 500, "the window sits flush with the display's right edge");
    (void)poll_resize_width(platform, &bus);

    CHECK(PlatformWindow_ChromeRailOpen(platform, RAIL_POINTS, "Plugins"), "rail opens");
    CHECK_EQ(window_width(window), 500, "a rail with no room to the right does not widen the window");
    CHECK_EQ(PlatformWindow_ChromeWidth(platform), RAIL_POINTS, "the rail still gets its full allocation");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS,
        "the pump relayouts the canvas to the game area the rail left");

    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens");
    CHECK_EQ(window_width(window), 500, "a page with no room to the right does not widen the window");
    CHECK_EQ(window_height(window), INIT_H, "nor heighten it: both axes are decided together");
    CHECK_EQ(PlatformWindow_ChromePageWidth(platform), 360, "the page still gets its full allocation");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS - 360,
        "the pump relayouts the canvas to the game area the page left");

    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 320), "page narrows");
    CHECK_EQ(window_width(window), 500, "narrowing an unfunded page does not move the window");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS - 320,
        "the game area takes the width the page gave up");

    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), 500, "closing a page that never grew the window never shrinks it");
    CHECK(!PlatformWindow_ChromeIsOpen(platform), "the page is closed");
    CHECK_EQ(PlatformWindow_ChromeWidth(platform), RAIL_POINTS, "the rail stays");
    CHECK_EQ(poll_resize_width(platform, &bus), 500 - RAIL_POINTS,
        "the game area gets the page's width back");

    /* ---- room to the right: attached grow, and Close gives it back ------ */
    SDL_SetWindowPosition(window, usable.x, usable.y);
    (void)poll_resize_width(platform, &bus);
    game_w = 500 - RAIL_POINTS;

    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens with room");
    CHECK_EQ(window_width(window), 500 + 360, "a page with room widens the window by the page");
    CHECK_EQ(window_height(window), 480, "and heightens it to the page's request");
    CHECK_EQ(poll_resize_width(platform, &bus), game_w, "the game area is untouched by a funded page");

    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 320), "funded page narrows");
    CHECK_EQ(window_width(window), 500 + 320, "the window follows a funded page narrower");
    CHECK_EQ(poll_resize_width(platform, &bus), game_w, "the game area is still untouched");
    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 360), "funded page widens");
    CHECK_EQ(window_width(window), 500 + 360, "and wider, while there is room");
    CHECK_EQ(poll_resize_width(platform, &bus), game_w, "the game area is still untouched");

    /* The window was moved (or the display shrank) under an open page: the
     * next widening has no room, so the page takes it from the game; the
     * next narrowing keeps the game where it is and gives the window back. */
    SDL_SetWindowPosition(window, usable.x + usable.w - window_width(window), usable.y);
    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 400), "page widens with no room");
    CHECK_EQ(window_width(window), 500 + 360, "a widening with no room leaves the window alone");
    CHECK_EQ(PlatformWindow_ChromePageWidth(platform), 400, "and the page still gets its allocation");
    CHECK_EQ(poll_resize_width(platform, &bus), game_w - 40, "the game area pays the difference");
    CHECK(PlatformWindow_ChromeSetPageWidth(platform, 300), "page narrows after an unfunded widening");
    CHECK_EQ(window_width(window), 500 + 260, "the window gives back only what it grew");
    CHECK_EQ(poll_resize_width(platform, &bus), game_w - 40, "and the game area stays where it was");

    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), 500, "Close gives back exactly what was grown");
    CHECK_EQ(poll_resize_width(platform, &bus), game_w, "and the game area is where it started");

    /* ---- the user narrowed the window while a funded page was open ------ */
    SDL_SetWindowPosition(window, usable.x, usable.y);
    CHECK(PlatformWindow_ChromeOpen(platform, 360, 480, "Plugins"), "page opens again");
    CHECK_EQ(window_width(window), 500 + 360, "funded again");
    SDL_SetWindowSize(window, 520, INIT_H);
    (void)poll_resize_width(platform, &bus);
    PlatformWindow_ChromeClose(platform);
    CHECK_EQ(window_width(window), MIN_W + RAIL_POINTS,
        "Close never shrinks below the canvas floor plus the rail that stays");
    (void)poll_resize_width(platform, &bus);

    /* ---- explicit sizes are the GAME AREA; the pane keeps its points ---- */
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
