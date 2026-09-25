/*
 * The two canvas-floor measurements: how narrow the canvas may get before the
 * lane's own toplevel stops fitting inside it.
 *
 * Both are scans with a long list of independent rejects, and every one of
 * those rejects is there because something in a real cache tree matched
 * without it. They are also the only two readers of their own shape, so a
 * reject that quietly stops rejecting does not fail anywhere -- it just widens
 * the canvas floor, and the window refuses to be resized as small as it used
 * to. That is what these cases have to catch.
 *
 * Every case is checked twice over: against the number it should be, and
 * against UITree_CanvasMeasureReference, a second, independent implementation
 * of the same two answers living in the canvas-measure header. The explicit
 * numbers say what the scan is FOR; the parity says the scan and the compact
 * query cannot drift apart.
 */
#include "test_harness.h"

#include "ui/uitree_canvas_floor.h"
#include "ui/uitree_canvas_measure.h"

#define CANVAS_W 720
#define CANVAS_H 480

/* The strip the fixture docks on the right, and the block the lane lays inside
 * what is left of the canvas -- 765 wide in a 678-wide area, which is the
 * overflow the core measurement exists to read. */
#define FIXTURE_STRIP_W 42
#define FIXTURE_CORE_W 765
#define FIXTURE_AREA_W (CANVAS_W - FIXTURE_STRIP_W)

static void
check_floor(struct UITree* tree, int strip_expected, int core_expected, char const* what)
{
    struct UITreeCanvasWidths reference;
    int strip;
    int core;

    strip = UITree_MeasureRightChromeStripWidth(tree);
    core = UITree_MeasureLaneFrameCoreWidth(tree);
    if( strip != strip_expected || core != core_expected )
    {
        fprintf(
            stderr,
            "FAIL: %s -- strip %d (want %d), core %d (want %d) (%s:%d)\n",
            what,
            strip,
            strip_expected,
            core,
            core_expected,
            __FILE__,
            __LINE__);
        g_failures++;
    }

    reference = UITree_CanvasMeasureReference(tree, CANVAS_W, CANVAS_H);
    if( strip != reference.strip || core != reference.core )
    {
        fprintf(
            stderr,
            "FAIL: %s -- measured %d/%d, reference says %d/%d (%s:%d)\n",
            what,
            strip,
            core,
            reference.strip,
            reference.core,
            __FILE__,
            __LINE__);
        g_failures++;
    }
}

static void
canvas_floor_cases(char const* mode)
{
    struct UITree* tree = UITree_New(4);
    int32_t root;
    int32_t strip;
    int32_t area;
    int32_t core;
    int32_t decoy;
    int32_t modal;
    int32_t short_parent;
    int32_t rival;
    int32_t absolute;
    int32_t filler;
    int32_t narrow;
    int32_t fixed_area;
    int32_t short_wrapper;
    int32_t short_area;
    int32_t offset_area;
    int32_t block;
    char what[160];
    int core_w;
    int n;

    /*
     * The shape a resizable OldSchool toplevel actually has: a fill-size root,
     * a fixed-width full-height strip docked on the right edge, a fill-width
     * area holding what is left of the canvas, and one fixed-size block laid
     * inside that area.
     */
    root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 1, 0, 0, 0, 0);
    UITree_SetSizeModesAt(tree, root, 0, 0, 1, 1);
    strip = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 2, 0, 0, FIXTURE_STRIP_W, 0);
    UITree_SetSizeModesAt(tree, strip, FIXTURE_STRIP_W, 0, 0, 1);
    UITree_SetPositionModesAt(tree, strip, 0, 0, 2, 0);
    area = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 3, 0, 0, FIXTURE_STRIP_W, 0);
    UITree_SetSizeModesAt(tree, area, FIXTURE_STRIP_W, 0, 1, 1);
    core = UITree_TestPushXy(tree, area, UIELEM_RS_RECT, 4, 0, 0, FIXTURE_CORE_W, 100);
    UITree_SetSizeModesAt(tree, core, FIXTURE_CORE_W, 100, 0, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);

    /* The geometry every case below leans on, stated rather than assumed. */
    TEST_ASSERT(
        tree->components[strip].position.abs_x == CANVAS_W - FIXTURE_STRIP_W,
        "fixture strip is not docked on the right edge");
    TEST_ASSERT(
        tree->components[strip].position.abs_h == CANVAS_H,
        "fixture strip is not full canvas height");
    TEST_ASSERT(
        tree->components[area].position.abs_w == FIXTURE_AREA_W,
        "fixture area is not the canvas minus the strip");

    snprintf(what, sizeof(what), "[%s] the shipping shape", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /*
     * A layer as wide as the whole canvas is NOT chrome, however much it looks
     * like the strip otherwise -- right-anchored, fixed width, full height. It
     * differs only in starting at x 0, because it covers everything. Count it
     * and the canvas floor becomes the canvas, so each resize hands the next
     * frame a wider floor than the last and the window grows on its own.
     */
    decoy = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 5, 0, 0, CANVAS_W, 0);
    UITree_SetSizeModesAt(tree, decoy, CANVAS_W, 0, 0, 1);
    UITree_SetPositionModesAt(tree, decoy, 0, 0, 2, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[decoy].position.abs_x == 0,
        "the canvas-wide decoy did not land at x 0");
    snprintf(what, sizeof(what), "[%s] a canvas-wide right-docked layer is not a strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /*
     * A right-docked fixed-width box that stops short of the right edge is not
     * the strip either. The strip is docked ON the edge; a box inset from it is
     * a scrollbar, or a column inside something that is itself inset.
     */
    UITree_SetSizeAt(tree, decoy, 60, 0);
    UITree_SetSizeModesAt(tree, decoy, 60, 0, 0, 1);
    UITree_SetPositionModesAt(tree, decoy, 1, 0, 2, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[decoy].position.abs_x + tree->components[decoy].position.abs_w ==
            CANVAS_W - 1,
        "the inset decoy is not one pixel short of the right edge");
    snprintf(what, sizeof(what), "[%s] a box inset from the right edge is not a strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /* Out of the way for everything below. */
    UITree_SetHideAt(tree, decoy, 1);

    /*
     * The rest of the strip's signature, one attribute at a time. Each of these
     * is a full-height box whose right edge is the canvas's right edge, and
     * each differs from the real strip in exactly one thing -- so each of them
     * is what gets measured as chrome if that one test stops being made.
     */

    /* Placed at an absolute x rather than anchored to the right edge. It lands
     * on the edge at this canvas size and comes away from it at every other
     * one, which is what makes it content rather than furniture. */
    absolute = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 9, CANVAS_W - 60, 0, 60, 0);
    UITree_SetSizeModesAt(tree, absolute, 60, 0, 0, 1);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[absolute].position.abs_x + tree->components[absolute].position.abs_w ==
            CANVAS_W,
        "the absolutely-placed decoy is not on the right edge");
    snprintf(what, sizeof(what), "[%s] an absolutely placed column is not the strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, absolute, 1);

    /* A mounted interface root that FILLS from a positive x to the right edge.
     * This is the one that bites: the strip is fixed-width, so the canvas floor
     * can hold it, but a fill-width root is as wide as whatever canvas it is
     * given. Measure it as chrome and each frame's floor is built out of the
     * last frame's width, so the window walks itself wider until it stops. */
    filler = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 10, 0, 0, 60, 0);
    UITree_SetSizeModesAt(tree, filler, 60, 0, 1, 1);
    UITree_SetPositionModesAt(tree, filler, 0, 0, 2, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[filler].position.abs_w == CANVAS_W - 60 &&
            tree->components[filler].position.abs_x == 60,
        "the fill-width decoy does not span from a positive x to the right edge");
    snprintf(what, sizeof(what), "[%s] a fill-width root reaching the edge is not the strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, filler, 1);

    /* Two real strips, the narrower one found LAST. The answer is the widest,
     * not the most recent: a 20-column docked over the 42-column strip does not
     * make the chrome narrower, it sits on top of it. */
    narrow = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 11, 0, 0, 20, 0);
    UITree_SetSizeModesAt(tree, narrow, 20, 0, 0, 1);
    UITree_SetPositionModesAt(tree, narrow, 0, 0, 2, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] a narrower strip found later does not win", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, narrow, 1);

    /*
     * How tall a right-docked column has to be before it counts as the strip.
     * The test is `abs_h < canvas_h - 2`, and the two-pixel slack is not
     * decoration -- the authored strip is laid at full parent height and comes
     * back a pixel or two short at some canvas sizes. Both sides of the
     * boundary are pinned: tighten it and the real strip stops being found,
     * loosen it and a minimap orb column starts counting.
     */
    short_parent = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 6, 0, 0, 0, CANVAS_H - 3);
    UITree_SetSizeModesAt(tree, short_parent, 0, CANVAS_H - 3, 1, 0);
    rival = UITree_TestPushXy(tree, short_parent, UIELEM_RS_LAYER, 7, 0, 0, 60, 0);
    UITree_SetSizeModesAt(tree, rival, 60, 0, 0, 1);
    UITree_SetPositionModesAt(tree, rival, 0, 0, 2, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[rival].position.abs_h == CANVAS_H - 3,
        "the rival column is not three pixels short of the canvas");
    snprintf(what, sizeof(what), "[%s] three pixels short of full height is not the strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /*
     * Two pixels short, and it IS the strip -- and the coupling between the two
     * answers shows immediately: the lane's area is the canvas minus the OLD
     * strip, so a wider strip means the area is no longer the carved area and
     * the core has nothing to report. This is why the core scan reads the strip
     * instead of a constant.
     */
    UITree_SetSizeAt(tree, short_parent, 0, CANVAS_H - 2);
    UITree_SetSizeModesAt(tree, short_parent, 0, CANVAS_H - 2, 1, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] two pixels short is the strip, and the widest wins", mode);
    check_floor(tree, 60, 0, what);

    /*
     * Hidden ancestors. The CS2 runtime bakes a panel the moment a script
     * touches it, and a baked-but-unopened panel is hidden at its group root
     * while the right-docked column inside it keeps a clear flag and a
     * full-height box -- the strip's exact signature. Reading it grows the
     * canvas floor by a panel nobody opened.
     */
    UITree_SetHideAt(tree, short_parent, 1);
    snprintf(what, sizeof(what), "[%s] a column under a hidden root is not the strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, short_parent, 0);
    snprintf(what, sizeof(what), "[%s] unhiding the root brings the wider strip back", mode);
    check_floor(tree, 60, 0, what);

    /* Unmounted is hidden too: an interface the runtime baked but has not
     * mounted is `mount_hidden` at its root, with every box below it live. */
    UITree_SetMountHiddenAt(tree, short_parent, 1);
    snprintf(what, sizeof(what), "[%s] a column under an unmounted root is not the strip", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetMountHiddenAt(tree, short_parent, 0);
    snprintf(what, sizeof(what), "[%s] mounting the root brings the wider strip back", mode);
    check_floor(tree, 60, 0, what);

    /* Done with the rival; back to the shipping shape. */
    UITree_SetHideAt(tree, short_parent, 1);
    snprintf(what, sizeof(what), "[%s] back to the shipping shape", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /*
     * The core's own rejects.
     *
     * A block bigger than the container it hangs in is common -- a 512x334
     * modal parked inside the 42-column popout strip is one, and it is not
     * short of anything. What makes the lane's block different is WHERE it
     * hangs: in a fill-width, full-height area docked at x 0 that is exactly
     * the strip narrower than the canvas.
     */
    modal = UITree_TestPushXy(tree, strip, UIELEM_RS_RECT, 8, 0, 0, 512, 334);
    UITree_SetSizeModesAt(tree, modal, 512, 334, 0, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] a modal overflowing the strip is not the lane block", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /* Hung off the root instead: the root is the whole canvas, not the carved
     * area, and a block that fits inside it is short of nothing. */
    UITree_Reparent(tree, core, root);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] the block hung off the root measures nothing", mode);
    check_floor(tree, FIXTURE_STRIP_W, 0, what);
    UITree_Reparent(tree, core, area);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);

    /* A block that FITS says nothing about how small it could have been. The
     * area is 678 and the test is a strict `<=`, so 678 reports nothing and 679
     * reports itself. */
    UITree_SetSizeAt(tree, core, FIXTURE_AREA_W, 100);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] a block that exactly fits is not an overflow", mode);
    check_floor(tree, FIXTURE_STRIP_W, 0, what);
    UITree_SetSizeAt(tree, core, FIXTURE_AREA_W + 1, 100);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] one pixel over the area is an overflow", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_AREA_W + 1, what);

    /* A block that follows whatever it is put in cannot overflow it. */
    UITree_SetSizeModesAt(tree, core, 0, 100, 1, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[core].position.abs_w == FIXTURE_AREA_W,
        "the fill-width block did not take its area's width");
    snprintf(what, sizeof(what), "[%s] a fill-width block cannot overflow its area", mode);
    check_floor(tree, FIXTURE_STRIP_W, 0, what);

    /* A fill-width block CAN come out wider than its area, if it is authored to
     * overhang -- the inset is signed. It is still not the lane's floor: it is
     * a hundred pixels wider than whatever it is put in, at every canvas size,
     * so it says nothing about how small the canvas may get. */
    UITree_SetSizeModesAt(tree, core, -100, 100, 1, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[core].position.abs_w == FIXTURE_AREA_W + 100,
        "the overhanging fill-width block did not come out wider than its area");
    snprintf(what, sizeof(what), "[%s] an overhanging fill-width block is not the floor", mode);
    check_floor(tree, FIXTURE_STRIP_W, 0, what);

    UITree_SetSizeModesAt(tree, core, FIXTURE_CORE_W, 100, 0, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] the fixed-width block overflows again", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);

    /* Hidden the same way the strip is. */
    UITree_SetHideAt(tree, core, 1);
    snprintf(what, sizeof(what), "[%s] a hidden block is not laid out short", mode);
    check_floor(tree, FIXTURE_STRIP_W, 0, what);
    UITree_SetHideAt(tree, core, 0);

    /* The widest overflow wins, not the first one found. */
    UITree_SetSizeAt(tree, modal, 900, 100);
    UITree_SetSizeModesAt(tree, modal, 900, 100, 0, 0);
    UITree_Reparent(tree, modal, area);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] the widest overflowing block wins", mode);
    check_floor(tree, FIXTURE_STRIP_W, 900, what);
    UITree_SetSizeAt(tree, modal, 512, 334);
    UITree_SetSizeModesAt(tree, modal, 512, 334, 0, 0);
    UITree_Reparent(tree, modal, strip);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);

    /*
     * The rest of the carved area's signature, one attribute at a time. Each of
     * these holds a block 800 wide -- wider than the lane's own 765 -- so if the
     * scan accepts the container, it says so loudly.
     *
     * What they have in common is that none of them is short of anything. The
     * carved area is the one container in the tree whose width is dictated by
     * the canvas, and that is the only reason a block overflowing it says
     * anything about how wide the canvas has to be. A block overflowing some
     * other box is just a box that was authored too small.
     */

    /* Fixed-width, not fill-width. It happens to be 678 at this canvas size and
     * stays 678 at every other one, so nothing about it follows the canvas. */
    fixed_area = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 12, 0, 0, FIXTURE_AREA_W, 0);
    UITree_SetSizeModesAt(tree, fixed_area, FIXTURE_AREA_W, 0, 0, 1);
    block = UITree_TestPushXy(tree, fixed_area, UIELEM_RS_RECT, 13, 0, 0, 800, 100);
    UITree_SetSizeModesAt(tree, block, 800, 100, 0, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[fixed_area].position.abs_w == FIXTURE_AREA_W &&
            tree->components[fixed_area].position.abs_h == CANVAS_H,
        "the fixed-width area decoy is not the carved area's size");
    TEST_ASSERT(
        tree->components[block].position.abs_w == 800,
        "the fixed-width area decoy's block is not 800 wide");
    snprintf(what, sizeof(what), "[%s] a fixed-width container is not the carved area", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, fixed_area, 1);

    /* Fill-width and the right width, but only 300 tall. The carved area is the
     * full height of the canvas; a 300-tall band across it is a panel. */
    short_wrapper = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 14, 0, 0, 0, 300);
    UITree_SetSizeModesAt(tree, short_wrapper, 0, 300, 1, 0);
    short_area = UITree_TestPushXy(tree, short_wrapper, UIELEM_RS_LAYER, 15, 0, 0, 42, 0);
    UITree_SetSizeModesAt(tree, short_area, FIXTURE_STRIP_W, 0, 1, 1);
    block = UITree_TestPushXy(tree, short_area, UIELEM_RS_RECT, 16, 0, 0, 800, 100);
    UITree_SetSizeModesAt(tree, block, 800, 100, 0, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[short_area].position.abs_w == FIXTURE_AREA_W &&
            tree->components[short_area].position.abs_h == 300,
        "the short area decoy is not the carved area's width at a third its height");
    TEST_ASSERT(
        tree->components[block].position.abs_w == 800,
        "the short area decoy's block is not 800 wide");
    snprintf(what, sizeof(what), "[%s] a short container is not the carved area", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, short_wrapper, 1);

    /* Fill-width, full height, the right width -- and inset from the left edge.
     * The carved area starts where the canvas starts; the strip is carved off
     * the other end. A band the same width sitting 20 pixels in is something
     * laid inside the canvas, not the canvas minus its furniture. */
    offset_area = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 17, 20, 0, 42, 0);
    UITree_SetSizeModesAt(tree, offset_area, FIXTURE_STRIP_W, 0, 1, 1);
    block = UITree_TestPushXy(tree, offset_area, UIELEM_RS_RECT, 18, 0, 0, 800, 100);
    UITree_SetSizeModesAt(tree, block, 800, 100, 0, 0);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    TEST_ASSERT(
        tree->components[offset_area].position.abs_w == FIXTURE_AREA_W &&
            tree->components[offset_area].position.abs_x == 20,
        "the offset area decoy is not the carved area's width, inset from the left");
    TEST_ASSERT(
        tree->components[block].position.abs_w == 800,
        "the offset area decoy's block is not 800 wide");
    snprintf(what, sizeof(what), "[%s] a container inset from the left is not the carved area", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetHideAt(tree, offset_area, 1);

    /* A narrower overflowing block found LATER, in the real carved area. The
     * floor is the widest thing that has to fit, not the last one measured. */
    UITree_SetSizeAt(tree, modal, 700, 100);
    UITree_SetSizeModesAt(tree, modal, 700, 100, 0, 0);
    UITree_Reparent(tree, modal, area);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
    snprintf(what, sizeof(what), "[%s] a narrower overflow found later does not win", mode);
    check_floor(tree, FIXTURE_STRIP_W, FIXTURE_CORE_W, what);
    UITree_SetSizeAt(tree, modal, 512, 334);
    UITree_SetSizeModesAt(tree, modal, 512, 334, 0, 0);
    UITree_Reparent(tree, modal, strip);
    UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);

    /*
     * Both answers are memoised on the tree's dirty and layout generations, and
     * a memo missing a term serves last frame's floor forever. Walk the two
     * kinds of change it has to notice -- a visibility flip, which moves no
     * boxes, and a resize, which moves boxes without touching a flag -- and
     * read the answer after each one.
     */
    core_w = FIXTURE_CORE_W;
    for( n = 0; n < 64; n++ )
    {
        UITree_SetHideAt(tree, strip, 1);
        snprintf(what, sizeof(what), "[%s] memo: strip hidden, pass %d", mode, n);
        check_floor(tree, 0, 0, what);

        UITree_SetHideAt(tree, strip, 0);
        snprintf(what, sizeof(what), "[%s] memo: strip visible, pass %d", mode, n);
        check_floor(tree, FIXTURE_STRIP_W, core_w, what);

        core_w = 700 + n;
        UITree_SetSizeAt(tree, core, core_w, 100);
        UITree_LayoutResolve(tree, 0, 0, CANVAS_W, CANVAS_H);
        snprintf(what, sizeof(what), "[%s] memo: block resized, pass %d", mode, n);
        check_floor(tree, FIXTURE_STRIP_W, core_w, what);
    }

    /*
     * A tree with nothing in it has no chrome and no lane block.
     *
     * This is the case the memo gets wrong if it is keyed on the tree's size
     * rather than its identity. UITree_Clear reclaims every slot in place --
     * the array does not shrink, and no node's dirty flag moves -- so a cleared
     * tree and the tree it used to be agree on the component count, the dirty
     * generation and the layout sequence. The chrome of a logged-in frame then
     * stays in the canvas floor of a login screen that has none.
     */
    UITree_Clear(tree);
    snprintf(what, sizeof(what), "[%s] a cleared tree measures nothing", mode);
    check_floor(tree, 0, 0, what);

    UITree_Free(tree);
}

void
test_canvas_floor_measures(void)
{
    int const compact_was = UITree_CanvasQueryCompactEnabled();
    int const root_w_was = UITREE_LAYOUT_ROOT_W;
    int const root_h_was = UITREE_LAYOUT_ROOT_H;

    printf("TEST: canvas floor measurements\n");

    /* Both measurements read the canvas size from the layout root, not from
     * whatever size the tree was last resolved at -- they are asked from the
     * resize path, where the tree may not have been laid out at the new size
     * yet. So the root has to say 720x480 here, not just the resolve call. */
    UITree_LayoutSetRootSize(CANVAS_W, CANVAS_H);

    /* The scans themselves. */
    UITree_CanvasQuerySetCompact(0);
    canvas_floor_cases("scan");

    /* And the compact query these same two entry points hand off to on ARM32,
     * which has to answer with the same two numbers in the same two slots --
     * the strip from the strip call, the core from the core call. */
    UITree_CanvasQuerySetCompact(1);
    canvas_floor_cases("compact");

    UITree_CanvasQuerySetCompact(compact_was);
    UITree_LayoutSetRootSize(root_w_was, root_h_was);
}
