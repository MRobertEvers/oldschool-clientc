/*
 * Where a mouse press lands in the client's layout, against where the present
 * DRAWS that layout pixel (raid seam25, watched_client_mouse_mapping: "the
 * mouse coords are WAYYY OFF" in ./launch run osrs239-scripts on a Retina Mac).
 *
 * Part 1 is the pure function (platform/platform_pointer_map.h) across every
 * combination that decides the mapping: 1x and 2x density; the pane closed,
 * the rail alone, the page with the rail; the title screen's 765x503 layout
 * and the in-game resizable layout (the game area at the HighDPI unit); the
 * default 765x503 window grown for the pane, and the same window with the pane
 * carved out of it (no room to grow). For each, a press at the window point
 * where a layout pixel is drawn must come back as that layout pixel.
 *
 * It also states the two WRONG mappings the seam had as candidates, so the
 * size of "way off" is a number: a mapping that forgets the density (points
 * read as pixels), and one that forgets the pane.
 *
 * Part 2 is the real thing: platform_sdl2.c under SDL_VIDEODRIVER=dummy, its
 * own PlatformWindow_MapMouse against PlatformWindow_GameAreaPixels and
 * ClientScale_Present (what the soft and GL3 presents place the frame by),
 * with the pane opened by PlatformWindow_ChromeRailOpen / ChromeOpen as the
 * plugin chrome opens it. TORIRS_SIM_PIXEL_DENSITY=2 makes the dummy window
 * claim a Retina drawable; the make target runs this binary at 1 and at 2.
 */
#include "platform/platform_pointer_map.h"
#include "platform/platform_window.h"

#include <SDL.h>
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
            failures++;                                                              \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                     \
            fprintf(stderr, __VA_ARGS__);                                            \
            fprintf(stderr, "\n");                                                   \
        }                                                                            \
    } while( 0 )

/* Layout pixels one window point spans: a press cannot be finer than that. */
static int
point_tolerance(
    struct ClientScaleSettings const* settings,
    struct PlatformPointerGeometry const* geometry)
{
    struct ClientScalePresent present;
    int area_w = 0;
    int area_h = 0;
    int density_x;
    int tolerance_x;
    int tolerance_y;

    PlatformPointer_GameArea(geometry, &area_w, &area_h);
    ClientScale_Present(settings, geometry->layout_w, geometry->layout_h, area_w, area_h, &present);
    density_x = (geometry->drawable_w + geometry->point_w - 1) / geometry->point_w;
    /* One window point spans this many layout pixels, rounded up; at least
     * one for the floor in each direction of a scaled frame. "Way off" is
     * tens to hundreds. */
    tolerance_x = (geometry->layout_w * density_x + present.output.w - 1) / present.output.w;
    tolerance_y = (geometry->layout_h * density_x + present.output.h - 1) / present.output.h;
    if( tolerance_y > tolerance_x )
        tolerance_x = tolerance_y;
    return tolerance_x < 1 ? 1 : tolerance_x;
}

/* The layout points a person presses: the title's Existing User and Login
 * buttons (osrs239_ui.ini btn_existing_user 389,271 147x41; btn_login
 * 229,301), the corners, and a point near the far edge. */
static int const k_layout_points[][2] = {
    { 462, 291 }, { 302, 321 }, { 0, 0 }, { 1, 1 }, { 380, 250 }, { 700, 480 },
};

static void
check_round_trip(
    char const* label,
    struct ClientScaleSettings const* settings,
    struct PlatformPointerGeometry const* geometry)
{
    int const tolerance = point_tolerance(settings, geometry);

    for( size_t i = 0; i < sizeof(k_layout_points) / sizeof(k_layout_points[0]); i++ )
    {
        int const lx = k_layout_points[i][0] < geometry->layout_w ? k_layout_points[i][0]
                                                                  : geometry->layout_w - 1;
        int const ly = k_layout_points[i][1] < geometry->layout_h ? k_layout_points[i][1]
                                                                  : geometry->layout_h - 1;
        int wx = 0;
        int wy = 0;
        int mx = -1;
        int my = -1;
        int dx;
        int dy;

        CHECK(PlatformPointer_LayoutToWindow(settings, geometry, lx, ly, &wx, &wy),
            "%s: layout %d,%d has a window point", label, lx, ly);
        CHECK(PlatformPointer_WindowToLayout(settings, geometry, wx, wy, &mx, &my),
            "%s: window %d,%d maps", label, wx, wy);
        dx = mx > lx ? mx - lx : lx - mx;
        dy = my > ly ? my - ly : ly - my;
        CHECK(dx <= tolerance && dy <= tolerance,
            "%s: drawn at layout %d,%d (window %d,%d) but pressed at %d,%d (tolerance %d)",
            label, lx, ly, wx, wy, mx, my, tolerance);
    }
}

/* One window state: density, window points, pane points, and which layout
 * the client holds (the title's 765x503, or the resizable one that follows
 * the game area in the HighDPI unit). */
static void
geometry_for(
    struct PlatformPointerGeometry* geometry,
    int density,
    int point_w,
    int point_h,
    int pane_point_w,
    int title_layout)
{
    int area_w;
    int area_h;

    memset(geometry, 0, sizeof(*geometry));
    geometry->point_w = point_w;
    geometry->point_h = point_h;
    geometry->drawable_w = point_w * density;
    geometry->drawable_h = point_h * density;
    geometry->pane_point_w = pane_point_w;
    PlatformPointer_GameArea(geometry, &area_w, &area_h);
    /* WINDOW_POINTS (the manifest's hidpi=0 = HighDPI automatic): 100% is a
     * window point, so the resizable layout is the game area / density. */
    geometry->layout_w = title_layout ? 765 : area_w / density;
    geometry->layout_h = title_layout ? 503 : area_h / density;
}

static void
part1_pure(void)
{
    struct ClientScaleSettings settings;
    struct PlatformPointerGeometry geometry;
    static int const densities[] = { 1, 2 };
    /* pane points: none, rail (TORIRS_CHROME_M_RAIL_W-sized), page 320 + rail */
    static int const panes[] = { 0, 40, 360 };
    char label[160];

    memset(&settings, 0, sizeof(settings));
    settings.fit = CLIENT_SCALE_FIT_KEEP_ASPECT;
    for( size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); d++ )
    {
        settings.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
        settings.density_percent = densities[d] * 100;
        for( size_t p = 0; p < sizeof(panes) / sizeof(panes[0]); p++ )
        {
            for( int title = 0; title <= 1; title++ )
            {
                /* grown: the 765x503 game area kept, the pane beside it */
                geometry_for(&geometry, densities[d], 765 + panes[p], 503, panes[p], title);
                snprintf(label, sizeof(label), "density %dx pane %d grown %s",
                    densities[d], panes[p], title ? "title 765x503" : "in-game resizable");
                check_round_trip(label, &settings, &geometry);
                /* carved: no room to grow, the pane taken from the game area */
                geometry_for(&geometry, densities[d], 765, 503, panes[p], title);
                snprintf(label, sizeof(label), "density %dx pane %d carved %s",
                    densities[d], panes[p], title ? "title 765x503" : "in-game resizable");
                check_round_trip(label, &settings, &geometry);
                /* a large resizable window (the default window dragged) */
                geometry_for(&geometry, densities[d], 1440 + panes[p], 900, panes[p], title);
                snprintf(label, sizeof(label), "density %dx pane %d window 1440x900 %s",
                    densities[d], panes[p], title ? "title 765x503" : "in-game resizable");
                check_round_trip(label, &settings, &geometry);
            }
        }
    }

    /* The two wrong mappings, measured, at the title's Existing User button
     * on a 2x window with the rail open. Each is what "way off" would be. */
    {
        int wx = 0;
        int wy = 0;
        int mx = 0;
        int my = 0;
        struct PlatformPointerGeometry wrong;

        settings.density_percent = 200;
        geometry_for(&geometry, 2, 765 + 40, 503, 40, 1);
        CHECK(PlatformPointer_LayoutToWindow(&settings, &geometry, 462, 291, &wx, &wy), "title point");
        /* density forgotten: the window point read as a drawable pixel,
         * against the frame the present placed in drawable pixels */
        {
            struct ClientScalePresent present;
            int area_w = 0;
            int area_h = 0;
            PlatformPointer_GameArea(&geometry, &area_w, &area_h);
            ClientScale_Present(&settings, 765, 503, area_w, area_h, &present);
            ClientScale_OutputToLayout(&present.output, 765, 503, wx, wy, &mx, &my);
        }
        printf("candidate (a) density forgotten: Existing User 462,291 pressed at %d,%d "
               "-> layout %d,%d\n", wx, wy, mx, my);
        CHECK(mx != 462 || my != 291, "a density-blind mapping is visibly wrong");
        /* pane forgotten: the whole window taken as the game area */
        wrong = geometry;
        wrong.pane_point_w = 0;
        (void)PlatformPointer_WindowToLayout(&settings, &wrong, wx, wy, &mx, &my);
        printf("candidate (b) pane forgotten (rail 40): Existing User 462,291 -> layout %d,%d\n",
            mx, my);
        geometry_for(&geometry, 2, 765, 503, 360, 1);
        CHECK(PlatformPointer_LayoutToWindow(&settings, &geometry, 462, 291, &wx, &wy), "carved");
        wrong = geometry;
        wrong.pane_point_w = 0;
        (void)PlatformPointer_WindowToLayout(&settings, &wrong, wx, wy, &mx, &my);
        printf("candidate (b) pane forgotten (page 360 carved): Existing User 462,291 -> layout "
               "%d,%d\n", mx, my);
        CHECK(mx != 462, "a pane-blind mapping of a carved window is visibly wrong");
    }
}

/* Part 2: the real platform's MapMouse against the real present geometry. */
static void
check_platform(
    char const* label,
    struct PlatformWindow* platform,
    int title_layout)
{
    struct ClientScaleSettings settings;
    struct ClientScalePresent present;
    struct PlatformPointerGeometry geometry;
    int area_w = 0;
    int area_h = 0;
    int point_w = 0;
    int point_h = 0;
    int density;
    char full[200];

    SDL_GetWindowSize((SDL_Window*)PlatformWindow_GLWindow(platform), &point_w, &point_h);
    PlatformWindow_GameAreaPixels(platform, &area_w, &area_h);
    density = PlatformWindow_PixelDensity(platform);
    memset(&settings, 0, sizeof(settings));
    settings.fit = CLIENT_SCALE_FIT_KEEP_ASPECT;
    settings.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
    settings.density_percent = density * 100;
    PlatformWindow_SetClientScaling(platform, &settings);
    if( title_layout )
        PlatformWindow_SetLayoutSize(platform, 765, 503);
    else
        PlatformWindow_SetLayoutSize(platform, area_w / density, area_h / density);

    /* The present's placement, from the platform's own answers. */
    memset(&geometry, 0, sizeof(geometry));
    geometry.point_w = point_w;
    geometry.point_h = point_h;
    geometry.layout_w = title_layout ? 765 : area_w / density;
    geometry.layout_h = title_layout ? 503 : area_h / density;
    ClientScale_Present(&settings, geometry.layout_w, geometry.layout_h, area_w, area_h, &present);
    printf("%s: window %dx%d points, density %d, game area %dx%d px, pane %d px, layout %dx%d, "
           "frame at %d,%d %dx%d\n",
        label, point_w, point_h, density, area_w, area_h, PlatformWindow_ChromeWidth(platform),
        geometry.layout_w, geometry.layout_h, present.output.x, present.output.y,
        present.output.w, present.output.h);
    for( size_t i = 0; i < sizeof(k_layout_points) / sizeof(k_layout_points[0]); i++ )
    {
        int const lx = k_layout_points[i][0] < geometry.layout_w ? k_layout_points[i][0]
                                                                 : geometry.layout_w - 1;
        int const ly = k_layout_points[i][1] < geometry.layout_h ? k_layout_points[i][1]
                                                                 : geometry.layout_h - 1;
        /* Where the present draws it, in drawable pixels, then in points. */
        long long const px = present.output.x +
                             ((long long)lx * 2 + 1) * present.output.w / (2LL * geometry.layout_w);
        long long const py = present.output.y +
                             ((long long)ly * 2 + 1) * present.output.h / (2LL * geometry.layout_h);
        int const wx = (int)(px / density);
        int const wy = (int)(py / density);
        int mx = -1;
        int my = -1;
        int const tolerance = (geometry.layout_w * density + present.output.w - 1) / present.output.w;

        PlatformWindow_MapMouse(platform, wx, wy, &mx, &my);
        snprintf(full, sizeof(full), "%s layout %d,%d", label, lx, ly);
        CHECK(abs(mx - lx) <= (tolerance > 1 ? tolerance : 1) &&
                  abs(my - ly) <= (tolerance > 1 ? tolerance : 1),
            "%s: drawn at window %d,%d, MapMouse answered %d,%d", full, wx, wy, mx, my);
    }
}

static void
part2_platform(void)
{
    struct PlatformWindow* platform;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    platform = PlatformWindow_New();
    CHECK(platform != NULL, "platform allocated");
    if( !platform )
        return;
    CHECK(PlatformWindow_Init(platform, 765, 503, "pointer-map-test"), "dummy window opened");
    PlatformWindow_SetCanvasFollowsWindow(platform, NULL, true, 765, 503);

    check_platform("no pane, title", platform, 1);
    check_platform("no pane, in-game", platform, 0);
    CHECK(PlatformWindow_ChromeRailOpen(platform, 40, "Plugins"), "rail opened");
    check_platform("rail, title", platform, 1);
    check_platform("rail, in-game", platform, 0);
    CHECK(PlatformWindow_ChromeOpen(platform, 320, 480, "Plugins"), "page opened");
    check_platform("page+rail, title", platform, 1);
    check_platform("page+rail, in-game", platform, 0);
    PlatformWindow_ChromeClose(platform);
    check_platform("page closed, rail stays, title", platform, 1);
    PlatformWindow_Free(platform);
}

int
main(void)
{
    part1_pure();
    part2_platform();
    printf("sdl_pointer_map_test: density env %s, %d checks, %d failures\n",
        getenv("TORIRS_SIM_PIXEL_DENSITY") ? getenv("TORIRS_SIM_PIXEL_DENSITY") : "(unset)",
        checks, failures);
    return failures ? 1 : 0;
}
