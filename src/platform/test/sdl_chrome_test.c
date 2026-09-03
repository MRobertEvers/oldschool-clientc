#include "platform/platform_window.h"

#include "cmd/cmdbus.h"
#include "ui/torirs_chrome_exec.h"
#include "ui/torirs_chrome_shell.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int raster_calls;

#define CHECK(condition, message)                                                   \
    do                                                                               \
    {                                                                                \
        if( !(condition) )                                                           \
        {                                                                            \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            failures++;                                                              \
        }                                                                            \
    } while( 0 )

/* The SDL executor's optional borderless callback references this model
 * helper. This harness keeps the native OS frame, so an empty region is the
 * exact production answer it exercises. */
int
ToriRSChromeDragRegion_Contains(
    struct ToriRSChromeDragRegion const* region, int x, int y)
{
    (void)region;
    (void)x;
    (void)y;
    return 0;
}

static void
rasterise_page(
    void* user,
    int* pixels,
    int width,
    int height,
    struct ToriRSChromePrim const* prims,
    int count)
{
    (void)user;
    (void)prims;
    (void)count;
    raster_calls++;
    for( int y = 0; y < height; y++ )
        for( int x = 0; x < width; x++ )
            pixels[y * width + x] =
                (int)(0xff372e22u + (uint32_t)((x / 24 + y / 24) & 1) * 0x00070605u);
}

static void
drain_bus(struct ToriRS_CmdBus* bus, int* mouse_events)
{
    struct ToriRS_CmdHeader header;
    unsigned char payload[TORIRS_CMD_MAX_PAYLOAD];

    if( mouse_events )
        *mouse_events = 0;
    while( CmdBus_Pop(bus, &header, payload) )
        if( mouse_events &&
            (header.type == TORIRS_CMD_INPUT_MOUSE_DOWN ||
             header.type == TORIRS_CMD_INPUT_MOUSE_UP) )
            (*mouse_events)++;
}

static void
push_click(SDL_Window* window, int x, int y)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
    event.type = SDL_MOUSEBUTTONUP;
    SDL_PushEvent(&event);
}

static void
push_wheel(SDL_Window* window, int amount)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEWHEEL;
    event.wheel.windowID = SDL_GetWindowID(window);
    event.wheel.y = amount;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    SDL_PushEvent(&event);
}

static void
push_finger(SDL_Window* window, int x, int y)
{
    SDL_Event event;
    int width = 0;
    int height = 0;

    SDL_GetWindowSize(window, &width, &height);
    memset(&event, 0, sizeof(event));
    event.type = SDL_FINGERDOWN;
    event.tfinger.windowID = SDL_GetWindowID(window);
    event.tfinger.touchId = 1;
    event.tfinger.fingerId = 1;
    event.tfinger.x = width > 0 ? (float)x / (float)width : 0.0f;
    event.tfinger.y = height > 0 ? (float)y / (float)height : 0.0f;
    SDL_PushEvent(&event);
    event.type = SDL_FINGERUP;
    SDL_PushEvent(&event);
}

static int
take_selection(
    struct ToriRSChromeExec* exec,
    int* plugin,
    uint32_t* generation,
    struct ToriRSChromeRailIntent* layout)
{
    struct ToriRSChromeRailIntent intents[8];
    int found = 0;
    int count = exec->rail_poll(exec->user, intents, 8);

    for( int i = 0; i < count; i++ )
    {
        if( intents[i].kind == TORIRS_CHROME_RAIL_INTENT_SELECT )
        {
            if( plugin )
                *plugin = intents[i].plugin_index;
            if( generation )
                *generation = intents[i].selection_generation;
            found = 1;
        }
        else if( layout && intents[i].kind == TORIRS_CHROME_RAIL_INTENT_LAYOUT )
            *layout = intents[i];
    }
    return found;
}

static int
surface_has_color(struct PlatformWindow* platform, uint32_t color)
{
    int const* pixels = PlatformWindow_ChromePixels(platform);
    int const count = PlatformWindow_ChromeWidth(platform) *
                      PlatformWindow_ChromeHeight(platform);
    for( int i = 0; pixels && i < count; i++ )
        if( (uint32_t)pixels[i] == color )
            return 1;
    return 0;
}

static int
capture_main_window(struct PlatformWindow* platform, char const* path)
{
    SDL_Window* window = (SDL_Window*)PlatformWindow_GLWindow(platform);
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : NULL;
    SDL_Surface* image;
    int width = 0;
    int height = 0;
    int ok = 0;

    if( !renderer || !path || !path[0] ||
        SDL_GetRendererOutputSize(renderer, &width, &height) != 0 ||
        width <= 0 || height <= 0 )
        return 0;
    image = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if( !image )
        return 0;
    if( SDL_RenderReadPixels(
            renderer, NULL, SDL_PIXELFORMAT_ARGB8888, image->pixels, image->pitch) == 0 &&
        SDL_SaveBMP(image, path) == 0 )
        ok = 1;
    SDL_FreeSurface(image);
    return ok;
}

int
main(void)
{
    char const* capture = getenv("TORIRS_PLUGIN_CHROME_CAPTURE");
    struct PlatformWindow* platform;
    struct ToriRSChromeExec exec;
    struct ToriRSChromeRailSnapshot snapshot;
    struct ToriRSChromeRailIcon icon;
    struct ToriRSChromeRailIntent layout;
    struct ToriRSChromeSurfaceInput page_input;
    struct ToriRS_CmdBus bus;
    SDL_Window* window;
    int before_w = 0;
    int before_h = 0;
    int window_w = 0;
    int window_h = 0;
    int plugin = -99;
    int mouse_events = 0;
    uint32_t generation = 0;

    if( !capture || !capture[0] )
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    platform = PlatformWindow_New();
    CHECK(platform != NULL, "platform allocated");
    /* 560 wide at the display's origin: the dummy display is 1024 wide, and
     * attached grow only widens a window the display has room for (see
     * sdl_chrome_geometry_test.c). 765 centred would refuse the page. */
    CHECK(PlatformWindow_Init(platform, 560, 503, "chrome-test"), "SDL window opened");
    if( !platform )
        return 1;
    window = (SDL_Window*)PlatformWindow_GLWindow(platform);
    CHECK(window != NULL, "the existing top-level window is exposed");
    if( window )
        SDL_SetWindowPosition(window, 0, 0);
    SDL_GetWindowSize(window, &before_w, &before_h);
    CmdBus_Init(&bus);

    exec = ToriRSChromeExec_Sdl(platform, rasterise_page, NULL);
    CHECK(exec.rail_sync && exec.rail_icon && exec.rail_poll,
        "SDL executor exposes the persistent rail contract");

    ToriRSChromeRailSnapshot_Init(&snapshot);
    snapshot.registry_revision = 4;
    snapshot.selection_generation = 1;
    snapshot.page_generation = 1;
    snapshot.selected_entry = TORIRS_CHROME_SHELL_PAGE_MANAGE;
    snapshot.expanded = 0;
    CHECK(ToriRSChromeRailSnapshot_AddManage(
              &snapshot, TORIRS_CHROME_SHELL_PAGE_MANAGE, "Manage Plugins"),
        "Manage occupies the permanent first destination");
    for( int i = 0; i < 32; i++ )
    {
        char title[32];
        char badge[8];
        snprintf(title, sizeof(title), "Plugin %d", i);
        snprintf(badge, sizeof(badge), "%d", i);
        CHECK(ToriRSChromeRailSnapshot_Add(
                  &snapshot, i, title, "authored.png", i == 0 ? 360 : 320, badge, i == 31),
            "all 32 plugin destinations fit beside Manage");
    }
    exec.rail_sync(exec.user, &snapshot);
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(snapshot.entry_count == 33, "the runtime snapshot carries 33 destinations");
    CHECK(window_w == before_w + 40 && window_h == before_h,
        "publishing the registry immediately adds only the far-right rail");
    CHECK(!PlatformWindow_ChromeIsOpen(platform) && !PlatformWindow_AuxIsOpen(platform),
        "the collapsed rail creates neither a page nor an auxiliary window");
    CHECK(PlatformWindow_ChromeRailWidth(platform) == PlatformWindow_ChromeWidth(platform) &&
              PlatformWindow_ChromePageWidth(platform) == 0,
        "collapsed surface belongs wholly to the rail");
    CHECK(surface_has_color(platform, 0xffffff00u),
        "host-owned selected treatment is present on the Manage entry");
    CHECK(PlatformWindow_ChromeTakeDirty(platform),
        "the first retained rail frame requests one upload");
    CHECK(!PlatformWindow_ChromeTakeDirty(platform),
        "an unchanged retained rail frame requests no second upload");

    /* Manage click: concrete sentinel + generation, and no game command. */
    push_click(window, window_w - 20, 20);
    PlatformWindow_PollCommands(platform, &bus);
    drain_bus(&bus, &mouse_events);
    CHECK(take_selection(&exec, &plugin, &generation, &layout) &&
              plugin == TORIRS_CHROME_SHELL_PAGE_MANAGE && generation == 1,
        "Manage click queues its sentinel and displayed generation");
    CHECK(mouse_events == 0, "collapsed rail click cannot reach game input");
    push_finger(window, window_w - 20, 60);
    PlatformWindow_PollCommands(platform, &bus);
    drain_bus(&bus, &mouse_events);
    CHECK(take_selection(&exec, &plugin, &generation, &layout) &&
              plugin == 0 && generation == 1,
        "touch on the rail uses the same concrete selection queue");
    CHECK(mouse_events == 0, "rail touch cannot become a game tap");

    /* The last plugin's real pixels, reached through the scrollable rail. */
    memset(&icon, 0, sizeof(icon));
    icon.plugin_index = 31;
    icon.revision = 2;
    icon.width = 2;
    icon.height = 2;
    for( int i = 0; i < 4; i++ )
        icon.argb[i] = 0xff12ab34u;
    exec.rail_icon(exec.user, &icon);
    push_click(window, window_w - 20, 20); /* focus the rail for wheel input */
    push_wheel(window, -20);
    PlatformWindow_PollCommands(platform, &bus);
    (void)take_selection(&exec, &plugin, &generation, &layout);
    CHECK(surface_has_color(platform, 0xff12ab34u),
        "the scrolled 33rd entry renders its cached authored ARGB icon");
    CHECK(surface_has_color(platform, 0xffffff00u) &&
              surface_has_color(platform, 0xffff981fu),
        "attention and badge treatments are painted with host-owned chrome");
    push_click(window, window_w - 20, before_h - 41);
    PlatformWindow_PollCommands(platform, &bus);
    CHECK(take_selection(&exec, &plugin, &generation, &layout) &&
              plugin == 31 && generation == 1,
        "the last authored destination remains selectable after scrolling");

    /* Expand the same shell: page left, rail right, no second SDL window. */
    snapshot.selection_generation = 2;
    snapshot.page_generation = 2;
    snapshot.active_plugin = 31;
    snapshot.last_selected_plugin = 31;
    snapshot.selected_entry = 31;
    snapshot.expanded = 1;
    exec.rail_sync(exec.user, &snapshot);
    CHECK(exec.begin(exec.user), "selected page expands in the main window");
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(window_w == before_w + 360 && window_h >= before_h,
        "attached grow reserves one 320-point page plus the existing rail");
    CHECK(!PlatformWindow_AuxIsOpen(platform), "ordinary expansion still creates no aux window");
    {
        int page_w = 0;
        int page_h = 0;
        CHECK(exec.surface_size(exec.user, &page_w, &page_h) && page_w == 320 && page_h > 0,
            "the chrome model receives page-only geometry, excluding the rail");
    }
    memset(&layout, 0, sizeof(layout));
    (void)take_selection(&exec, &plugin, &generation, &layout);
    CHECK(layout.kind == TORIRS_CHROME_RAIL_INTENT_LAYOUT && layout.visible &&
              layout.width == 320 && layout.game_visible,
        "SDL reports its real attached page allocation without a platform enum");
    {
        struct ToriRSChromePrim prim;
        memset(&prim, 0, sizeof(prim));
        exec.present(exec.user, &prim, 1);
    }
    CHECK(raster_calls == 1, "the representative page uses the production surface callback");
    CHECK(surface_has_color(platform, 0xff12ab34u),
        "authored rail icon remains visible beside the expanded page");

    /* Page input is local to the page, and the far-right rail is disjoint. */
    drain_bus(&bus, NULL);
    push_click(window, before_w + 12, 80);
    PlatformWindow_PollCommands(platform, &bus);
    memset(&page_input, 0, sizeof(page_input));
    CHECK(exec.surface_input(exec.user, &page_input) && page_input.mouse_down &&
              page_input.mouse_x < PlatformWindow_ChromePageWidth(platform),
        "page click reaches page-local hit testing");
    drain_bus(&bus, &mouse_events);
    CHECK(mouse_events == 0, "page click cannot leak to the game");

    push_click(window, window_w - 20, 20);
    PlatformWindow_PollCommands(platform, &bus);
    memset(&page_input, 0, sizeof(page_input));
    CHECK(!exec.surface_input(exec.user, &page_input),
        "rail click is excluded from page surface input");
    CHECK(take_selection(&exec, &plugin, &generation, &layout) && generation == 2,
        "expanded rail still queues a generation-fenced selection");
    drain_bus(&bus, &mouse_events);
    CHECK(mouse_events == 0, "expanded rail click cannot leak to the game");

    push_click(window, 100, 100);
    PlatformWindow_PollCommands(platform, &bus);
    drain_bus(&bus, &mouse_events);
    CHECK(mouse_events == 2, "game-region click remains ordinary game input");

    /* Replacing selection repaints only retained rail state, not another window. */
    snapshot.selection_generation = 3;
    snapshot.page_generation = 3;
    snapshot.active_plugin = 0;
    snapshot.last_selected_plugin = 0;
    snapshot.selected_entry = 0;
    exec.rail_sync(exec.user, &snapshot);
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(window_w == before_w + 400 && !PlatformWindow_AuxIsOpen(platform),
        "selection replacement resizes the same shell to its preferred page width");
    memset(&layout, 0, sizeof(layout));
    (void)take_selection(&exec, &plugin, &generation, &layout);
    CHECK(layout.kind == TORIRS_CHROME_RAIL_INTENT_LAYOUT && layout.width == 360,
        "replacement publishes the new preferred page allocation");
    push_click(window, window_w - 20, 60);
    PlatformWindow_PollCommands(platform, &bus);
    CHECK(take_selection(&exec, &plugin, &generation, &layout) &&
              plugin == 0 && generation == 3,
        "clicking the selected expanded entry queues authoritative collapse");

    if( capture && capture[0] )
    {
        char const* path = strcmp(capture, "1") == 0
                               ? "plugin_chrome_capture.bmp"
                               : capture;
        PlatformWindow_Present(platform);
        CHECK(capture_main_window(platform, path),
            "deterministic production main-window BMP was captured");
        if( failures == 0 )
            printf("SDL plugin chrome capture: %s\n", path);
    }

    exec.end(exec.user);
    snapshot.selection_generation = 4;
    snapshot.page_generation = 4;
    snapshot.active_plugin = -1;
    snapshot.selected_entry = 0;
    snapshot.expanded = 0;
    exec.rail_sync(exec.user, &snapshot);
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(window_w == before_w + 40 && !PlatformWindow_ChromeIsOpen(platform),
        "collapse retains only the rail and reverses page growth");
    push_click(window, window_w - 20, 60);
    PlatformWindow_PollCommands(platform, &bus);
    CHECK(take_selection(&exec, &plugin, &generation, &layout) &&
              plugin == 0 && generation == 4,
        "collapsed selected plugin queues reopen through the same shell");
    CHECK(exec.begin(exec.user), "collapsed selection reopens the retained page");
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(window_w == before_w + 400, "reopen restores exactly one preferred page width");
    exec.end(exec.user);
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(window_w == before_w + 40, "second collapse has no cumulative width drift");
    CHECK(!PlatformWindow_AuxIsOpen(platform), "whole default lifecycle used one SDL window");

    /* Detachment remains an explicit opt-in and does not replace the default. */
    SDL_setenv("TORIRS_CHROME_DETACHED", "1", 1);
    snapshot.selection_generation = 5;
    snapshot.page_generation = 5;
    snapshot.active_plugin = 0;
    snapshot.selected_entry = 0;
    snapshot.expanded = 1;
    exec.rail_sync(exec.user, &snapshot);
    CHECK(exec.begin(exec.user) && PlatformWindow_AuxIsOpen(platform),
        "explicit detached mode alone creates the optional auxiliary page");
    SDL_GetWindowSize(window, &window_w, &window_h);
    CHECK(window_w == before_w + 40,
        "detached page leaves the retained main-window rail as the only inset");
    exec.end(exec.user);
    SDL_setenv("TORIRS_CHROME_DETACHED", "", 1);

    PlatformWindow_Free(platform);
    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("SDL attached multi-entry plugin chrome: ok\n");
    return 0;
}
