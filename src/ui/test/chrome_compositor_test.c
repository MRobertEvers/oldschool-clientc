/*
 * The chrome lists a frame hands downstream (ui/chrome_compositor.h).
 *
 * A created compositor is zeroed and borrows the renderer it is given. The
 * merge is for exactly the serials and revision it was committed with, holds
 * the developer chrome's primitives then the window's, and stops at its bound;
 * a steady frame gets the same array and count back. The surface list is
 * allocated on first use and takes at most TORIRS_CHROME_MAX_PRIMS of the
 * window's primitives; the surface is drawn for exactly its keys and size, and
 * forgetting the size makes it stale. Its picture grows to exactly width *
 * height and never shrinks.
 *
 * Run: make -C src test-chrome-compositor
 */

#include "ui/chrome_compositor.h"

#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* The compositor never looks inside the renderer it borrows. */
static int g_soft_stand_in;
#define SOFT ((struct ToriPlatform_Renderer_Soft3D*)&g_soft_stand_in)

/* Primitives tagged by their x, so a copy can be traced to its source. */
static struct ToriRSChromePrim g_dbg[CHROME_COMPOSITOR_PRIMS_MAX + 1];
static struct ToriRSChromePrim g_win[TORIRS_CHROME_MAX_PRIMS + 8];

static void
tag(
    struct ToriRSChromePrim* prims,
    int count,
    int base)
{
    for( int i = 0; i < count; i++ )
    {
        memset(&prims[i], 0, sizeof(prims[i]));
        prims[i].x = base + i;
    }
}

static void
test_create_is_zeroed(void)
{
    struct ChromeCompositor* comp = ChromeCompositor_Create(SOFT);
    int count = -1;

    TEST_ASSERT(ChromeCompositor_Soft(comp) == SOFT, "borrows the renderer it is given");
    TEST_ASSERT(ChromeCompositor_Merged(comp, &count) == comp->merged, "merged is the array");
    TEST_ASSERT(count == 0, "nothing merged yet");
    TEST_ASSERT(ChromeCompositor_MergedFor(comp, 0, 0, 0), "a zeroed merge is for zero keys");
    TEST_ASSERT(!ChromeCompositor_MergedFor(comp, 1, 0, 0), "and for nothing else");
    TEST_ASSERT(ChromeCompositor_SurfacePrims(comp) == NULL, "no surface list yet");
    TEST_ASSERT(comp->surface_pixels == NULL, "no surface picture yet");
    TEST_ASSERT(comp->surface_pixel_capacity == 0, "no surface capacity");
    TEST_ASSERT(ChromeCompositor_SurfaceFor(comp, 0, 0, 0, 0), "a zeroed surface is for zeroes");
    ChromeCompositor_Free(comp);
    ChromeCompositor_Free(NULL);
}

static void
test_merge(void)
{
    struct ChromeCompositor* comp = ChromeCompositor_Create(SOFT);
    struct ToriRSChromePrim const* got;
    int count = 0;
    int n;

    tag(g_dbg, 3, 100);
    tag(g_win, 2, 200);
    TEST_ASSERT(!ChromeCompositor_MergedFor(comp, 4, 7, 9), "stale for new serials");
    n = ChromeCompositor_MergeLists(comp, g_dbg, 3, g_win, 2);
    TEST_ASSERT(n == 5, "developer then window");
    ChromeCompositor_MergedSlot(comp, n)->x = 300;
    n++;
    ChromeCompositor_MergeCommit(comp, n, 4, 7, 9);
    got = ChromeCompositor_Merged(comp, &count);
    TEST_ASSERT(count == 6, "the appended well primitive counts");
    TEST_ASSERT(got[0].x == 100 && got[2].x == 102, "developer chrome first");
    TEST_ASSERT(got[3].x == 200 && got[4].x == 201, "window second");
    TEST_ASSERT(got[5].x == 300, "well last");

    TEST_ASSERT(ChromeCompositor_MergedFor(comp, 4, 7, 9), "current for the committed keys");
    TEST_ASSERT(!ChromeCompositor_MergedFor(comp, 5, 7, 9), "a developer rebuild is stale");
    TEST_ASSERT(!ChromeCompositor_MergedFor(comp, 4, 8, 9), "a window rebuild is stale");
    TEST_ASSERT(!ChromeCompositor_MergedFor(comp, 4, 7, 10), "a wells change is stale");
    TEST_ASSERT(
        ChromeCompositor_Merged(comp, &count) == got && count == 6,
        "a steady frame gets the same array and count");
    ChromeCompositor_Free(comp);
}

static void
test_merge_bound(void)
{
    struct ChromeCompositor* comp = ChromeCompositor_Create(SOFT);
    int n;

    tag(g_dbg, CHROME_COMPOSITOR_PRIMS_MAX + 1, 0);
    tag(g_win, TORIRS_CHROME_MAX_PRIMS + 8, 100000);
    n = ChromeCompositor_MergeLists(
        comp, g_dbg, TORIRS_CHROME_MAX_PRIMS, g_win, TORIRS_CHROME_MAX_PRIMS + 8);
    TEST_ASSERT(n == 2 * TORIRS_CHROME_MAX_PRIMS + 8, "both lists whole while they fit");

    n = ChromeCompositor_MergeLists(comp, g_dbg, CHROME_COMPOSITOR_PRIMS_MAX + 1, g_win, 1);
    TEST_ASSERT(n == CHROME_COMPOSITOR_PRIMS_MAX, "the developer list is cut at the bound");

    n = ChromeCompositor_MergeLists(comp, g_dbg, CHROME_COMPOSITOR_PRIMS_MAX - 2, g_win, 5);
    TEST_ASSERT(n == CHROME_COMPOSITOR_PRIMS_MAX, "the window takes only the room left");
    TEST_ASSERT(
        comp->merged[CHROME_COMPOSITOR_PRIMS_MAX - 1].x == 100001, "and its first prims");
    ChromeCompositor_Free(comp);
}

static void
test_surface_lists(void)
{
    struct ChromeCompositor* comp = ChromeCompositor_Create(SOFT);
    struct ToriRSChromePrim const* prims;
    int n;

    tag(g_win, TORIRS_CHROME_MAX_PRIMS + 8, 500);
    n = ChromeCompositor_SurfaceLists(comp, g_win, 0);
    prims = ChromeCompositor_SurfacePrims(comp);
    TEST_ASSERT(n == 0, "an empty window takes nothing");
    TEST_ASSERT(prims != NULL, "but the list exists from the first use");

    n = ChromeCompositor_SurfaceLists(comp, g_win, 3);
    TEST_ASSERT(n == 3, "the window's prims");
    TEST_ASSERT(ChromeCompositor_SurfacePrims(comp) == prims, "the list is allocated once");
    TEST_ASSERT(prims[0].x == 500 && prims[2].x == 502, "copied in order");
    ChromeCompositor_SurfaceSlot(comp, n)->x = 900;
    TEST_ASSERT(prims[3].x == 900, "a well primitive lands after them");

    n = ChromeCompositor_SurfaceLists(comp, g_win, TORIRS_CHROME_MAX_PRIMS + 8);
    TEST_ASSERT(n == TORIRS_CHROME_MAX_PRIMS, "the window is cut at one instance's bound");
    ChromeCompositor_SurfaceSlot(comp, CHROME_COMPOSITOR_SURFACE_PRIMS_MAX - 1)->x = 901;
    TEST_ASSERT(
        prims[CHROME_COMPOSITOR_SURFACE_PRIMS_MAX - 1].x == 901, "the wells' room is there");
    ChromeCompositor_Free(comp);
}

static void
test_surface_keys_and_pixels(void)
{
    struct ChromeCompositor* comp = ChromeCompositor_Create(SOFT);
    uint32_t* pixels;
    uint32_t* again;

    TEST_ASSERT(!ChromeCompositor_SurfaceFor(comp, 3, 4, 200, 100), "stale before a present");
    pixels = ChromeCompositor_SurfacePixels(comp, 200, 100);
    TEST_ASSERT(pixels != NULL, "a picture for the surface");
    TEST_ASSERT(comp->surface_pixel_capacity == 200 * 100, "exactly width * height");
    pixels[200 * 100 - 1] = 0xABCDEFu;
    ChromeCompositor_SurfaceCommit(comp, 3, 4, 200, 100);
    TEST_ASSERT(ChromeCompositor_SurfaceFor(comp, 3, 4, 200, 100), "current after the commit");
    TEST_ASSERT(!ChromeCompositor_SurfaceFor(comp, 5, 4, 200, 100), "a window rebuild");
    TEST_ASSERT(!ChromeCompositor_SurfaceFor(comp, 3, 6, 200, 100), "a wells change");
    TEST_ASSERT(!ChromeCompositor_SurfaceFor(comp, 3, 4, 201, 100), "a wider surface");
    TEST_ASSERT(!ChromeCompositor_SurfaceFor(comp, 3, 4, 200, 99), "a shorter surface");

    again = ChromeCompositor_SurfacePixels(comp, 100, 50);
    TEST_ASSERT(again == pixels, "a smaller surface reuses the picture");
    TEST_ASSERT(comp->surface_pixel_capacity == 200 * 100, "and it never shrinks");
    TEST_ASSERT(again[200 * 100 - 1] == 0xABCDEFu, "nor is it cleared");

    again = ChromeCompositor_SurfacePixels(comp, 300, 100);
    TEST_ASSERT(comp->surface_pixel_capacity == 300 * 100, "a bigger one grows to fit exactly");
    TEST_ASSERT(again[200 * 100 - 1] == 0xABCDEFu, "keeping what it held");

    ChromeCompositor_SurfaceForgetSize(comp);
    TEST_ASSERT(!ChromeCompositor_SurfaceFor(comp, 3, 4, 200, 100), "forgetting makes it stale");
    TEST_ASSERT(ChromeCompositor_SurfaceFor(comp, 3, 4, 0, 0), "it forgot only the size");
    ChromeCompositor_Free(comp);
}

int
main(void)
{
    test_create_is_zeroed();
    test_merge();
    test_merge_bound();
    test_surface_lists();
    test_surface_keys_and_pixels();
    if( g_failures )
    {
        printf("chrome_compositor_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("chrome_compositor_test: all passed\n");
    return 0;
}
