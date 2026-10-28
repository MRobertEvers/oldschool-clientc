/*
 * The SDL desktop's native rail and surface page (ui/torirs_chrome_surface_sdl.c)
 * against a fake platform strip: what it refuses before the platform opened the
 * rail, that a rail press is the same generation-fenced SELECT the browser rail
 * sends, that the pop-out mark moves the page between the strip and the aux
 * window (giving the page width back), and that the aux window's close box docks
 * it. TORIRS_SURFACE_TEST_BMP=<path> writes the painted strip for a look.
 */

#include "ui/torirs_chrome_exec.h"
#include "ui/torirs_chrome_shell.h"
#include "platform/platform_window.h"

#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if( !(x) )                                                                                 \
        {                                                                                          \
            fprintf(stderr, "surface sdl: %s (line %d)\n", #x, __LINE__);                         \
            exit(1);                                                                               \
        }                                                                                          \
    } while( 0 )

#define STRIP_H 300
#define RAIL_W 42
#define PAGE_W 320
#define AUX_W 320
#define AUX_H 480

struct PlatformWindow
{
    int rail_open;
    int page_open;
    int pixels[(RAIL_W + PAGE_W) * STRIP_H];
    int presents;
    struct PlatformWindow_AuxInput rail_input;
    int rail_have_input;
    int aux_open;
    int aux_pixels[AUX_W * AUX_H];
    int aux_presents;
    int aux_close;
    int chrome_opens;
    int chrome_closes;
};

static struct PlatformWindow platform;

int PlatformWindow_ChromeRailWidth(struct PlatformWindow const* p)
{ return p->rail_open ? RAIL_W : 0; }
int PlatformWindow_ChromePageWidth(struct PlatformWindow const* p)
{ return p->page_open ? PAGE_W : 0; }
int PlatformWindow_ChromeWidth(struct PlatformWindow const* p)
{ return PlatformWindow_ChromeRailWidth(p) + PlatformWindow_ChromePageWidth(p); }
int PlatformWindow_ChromeHeight(struct PlatformWindow const* p)
{ return p->rail_open ? STRIP_H : 0; }
int* PlatformWindow_ChromePixels(struct PlatformWindow* p)
{ return p->rail_open ? p->pixels : NULL; }
void PlatformWindow_ChromePresent(struct PlatformWindow* p)
{ p->presents++; }
bool PlatformWindow_ChromeOpen(struct PlatformWindow* p, int width, int height, char const* title)
{
    CHECK(width == PAGE_W && height > 0 && title);
    p->page_open = 1;
    p->chrome_opens++;
    return true;
}
void PlatformWindow_ChromeClose(struct PlatformWindow* p)
{
    if( p->page_open )
        p->chrome_closes++;
    p->page_open = 0;
}
bool PlatformWindow_ChromeTakeRailInput(
    struct PlatformWindow* p, struct PlatformWindow_AuxInput* out)
{
    if( !p->rail_have_input )
        return false;
    *out = p->rail_input;
    p->rail_input.mouse_down = 0;
    p->rail_input.mouse_up = 0;
    p->rail_input.wheel = 0;
    p->rail_have_input = 0;
    return true;
}
bool PlatformWindow_ChromeTakeInput(struct PlatformWindow* p, struct PlatformWindow_AuxInput* out)
{
    (void)p;
    (void)out;
    return false;
}
bool PlatformWindow_AuxOpen(struct PlatformWindow* p, int width, int height, char const* title)
{
    CHECK(width > 0 && height > 0 && title);
    p->aux_open = 1;
    return true;
}
void PlatformWindow_AuxClose(struct PlatformWindow* p)
{ p->aux_open = 0; }
int* PlatformWindow_AuxPixels(struct PlatformWindow* p)
{ return p->aux_open ? p->aux_pixels : NULL; }
int PlatformWindow_AuxWidth(struct PlatformWindow const* p)
{ return p->aux_open ? AUX_W : 0; }
int PlatformWindow_AuxHeight(struct PlatformWindow const* p)
{ return p->aux_open ? AUX_H : 0; }
void PlatformWindow_AuxPresent(struct PlatformWindow* p)
{ p->aux_presents++; }
bool PlatformWindow_AuxTakeInput(struct PlatformWindow* p, struct PlatformWindow_AuxInput* out)
{
    (void)p;
    (void)out;
    return false;
}
bool PlatformWindow_AuxResize(struct PlatformWindow* p, int width, int height)
{
    (void)p;
    return width == AUX_W && height == AUX_H;
}
bool PlatformWindow_AuxTakeCloseRequest(struct PlatformWindow* p)
{
    int const closed = p->aux_close;
    p->aux_close = 0;
    return closed != 0;
}

/* A press and release at one rail-local point, drained by one poll. */
static int
click(struct ToriRSChromeExec const* exec, int x, int y, struct ToriRSChromeRailIntent* out, int max)
{
    memset(&platform.rail_input, 0, sizeof(platform.rail_input));
    platform.rail_input.mouse_x = x;
    platform.rail_input.mouse_y = y;
    platform.rail_input.mouse_down = 1;
    platform.rail_input.mouse_up = 1;
    platform.rail_have_input = 1;
    return exec->rail_poll(exec->user, out, max);
}

int
main(void)
{
    struct ToriRSChromeExec exec = ToriRSChromeExec_SdlSurface(&platform);
    struct ToriRSChromeRailSnapshot rail;
    struct ToriRSChromeRailIcon icon;
    struct ToriRSChromeRailIntent intents[4];
    uint32_t page[PAGE_W * STRIP_H];
    int w = 0;
    int h = 0;

    CHECK(exec.begin && exec.end && exec.rail_sync && exec.rail_icon && exec.rail_poll);
    CHECK(exec.surface_size && exec.surface_input && exec.surface_present);
    /* BUFFER's page: no semantic stream to consume. */
    CHECK(!exec.apply && !exec.poll);

    memset(&rail, 0, sizeof(rail));
    rail.registry_revision = 1;
    rail.selection_generation = 5;
    rail.selected_entry = -1;
    (void)ToriRSChromeRailSnapshot_AddManage(&rail, TORIRS_CHROME_SHELL_PAGE_MANAGE, "Manage Plugins");
    (void)ToriRSChromeRailSnapshot_Add(&rail, 3, "Loot Tracker", "loot.png", 320, "", 0);
    (void)ToriRSChromeRailSnapshot_Add(&rail, 4, "XP Tracker", "", 320, "12", 1);

    /* Nothing to draw into until the platform opened the strip. */
    CHECK(exec.rail_sync(exec.user, &rail) == 0);
    CHECK(!exec.begin(exec.user));
    platform.rail_open = 1;
    CHECK(exec.rail_sync(exec.user, &rail) == 1);
    CHECK(platform.presents > 0);

    memset(&icon, 0, sizeof(icon));
    icon.plugin_index = 3;
    icon.revision = 2;
    icon.width = icon.height = 8;
    for( int i = 0; i < 64; i++ )
        icon.argb[i] = (i / 8 + i) & 1 ? 0xffff981fu : 0xff000000u;
    CHECK(exec.rail_icon(exec.user, &icon) == 1);

    /* Row 1 (6px frame + one 36px row down) is Loot Tracker: a press there is
     * the SELECT the browser rail sends, fenced by the shown generation. */
    CHECK(click(&exec, 20, 6 + 36 + 10, intents, 4) == 1);
    CHECK(intents[0].kind == TORIRS_CHROME_RAIL_INTENT_SELECT);
    CHECK(intents[0].plugin_index == 3);
    CHECK(intents[0].selection_generation == 5);
    CHECK(click(&exec, 20, 6 + 10, intents, 4) == 1);
    CHECK(intents[0].plugin_index == TORIRS_CHROME_SHELL_PAGE_MANAGE);
    CHECK(intents[0].sequence > 0);
    /* Below the last entry is not a destination. */
    CHECK(click(&exec, 20, 6 + 3 * 36 + 10, intents, 4) == 0);

    /* The page beside the rail. */
    CHECK(!exec.surface_size(exec.user, &w, &h));
    CHECK(exec.begin(exec.user));
    CHECK(platform.page_open && platform.chrome_opens == 1);
    CHECK(exec.surface_size(exec.user, &w, &h) && w == PAGE_W && h == STRIP_H);
    for( int i = 0; i < PAGE_W * STRIP_H; i++ )
        page[i] = 0xff5d5447u;
    exec.surface_present(exec.user, page, PAGE_W, STRIP_H);
    CHECK((uint32_t)platform.pixels[10 * (RAIL_W + PAGE_W) + 10] == 0xff5d5447u);
    rail.selected_entry = 3;
    rail.expanded = 1;
    rail.selection_generation = 6;
    CHECK(exec.rail_sync(exec.user, &rail) == 1);
    /* The rail repaint keeps the page it was given. */
    CHECK((uint32_t)platform.pixels[10 * (RAIL_W + PAGE_W) + 10] == 0xff5d5447u);

    {
        char const* dump = getenv("TORIRS_SURFACE_TEST_BMP");
        if( dump && dump[0] )
            bmp_write_file(dump, platform.pixels, RAIL_W + PAGE_W, STRIP_H);
    }

    /* The pop-out mark sits at the foot of the rail while a page is up: a
     * press there moves the page into the aux window and gives its width back
     * beside the game; the rail stays. */
    CHECK(click(&exec, RAIL_W / 2, STRIP_H - 6 - 4 - 8, intents, 4) == 0);
    CHECK(platform.aux_open && !platform.page_open && platform.chrome_closes == 1);
    CHECK(platform.rail_open);
    CHECK(exec.surface_size(exec.user, &w, &h) && w == AUX_W && h == AUX_H);
    exec.surface_present(exec.user, page, PAGE_W, STRIP_H);
    CHECK(platform.aux_presents == 0); /* a picture for the size it left */
    {
        static uint32_t aux_page[AUX_W * AUX_H];
        for( int i = 0; i < AUX_W * AUX_H; i++ )
            aux_page[i] = 0xff112233u;
        exec.surface_present(exec.user, aux_page, AUX_W, AUX_H);
        CHECK(platform.aux_presents == 1 && (uint32_t)platform.aux_pixels[0] == 0xff112233u);
    }

    /* The aux window's close box docks it again, beside the rail. */
    platform.aux_close = 1;
    CHECK(exec.rail_poll(exec.user, intents, 4) == 0);
    CHECK(!platform.aux_open && platform.page_open && platform.chrome_opens == 2);
    CHECK(exec.surface_size(exec.user, &w, &h) && w == PAGE_W);

    /* Pressed again it goes out; collapsing the page takes the window with it
     * and asks for no width back. */
    CHECK(click(&exec, RAIL_W / 2, STRIP_H - 6 - 4 - 8, intents, 4) == 0);
    CHECK(platform.aux_open);
    exec.end(exec.user);
    CHECK(!platform.aux_open && !platform.page_open && platform.chrome_opens == 2);
    CHECK(!exec.surface_size(exec.user, &w, &h));
    /* With no page, the foot of the rail is not a control. */
    CHECK(click(&exec, RAIL_W / 2, STRIP_H - 6 - 4 - 8, intents, 4) == 0);
    CHECK(!platform.aux_open && !platform.page_open);

    ToriRSChromeExec_SdlSurfaceShutdown();
    puts("sdl surface chrome: ok");
    return 0;
}
