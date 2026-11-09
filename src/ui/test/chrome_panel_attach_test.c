/*
 * ToriRSChrome compound windows and the clip rule, driven through the real
 * input entry points (src/ui/uitree_debug_overlay.h "attachment").
 *
 *   ./chrome_panel_attach_test
 *
 * The map editor's panel groups are a FRAME panel (title bar and tab strip)
 * with a member window attached under it. Before attachment existed the
 * editor moved the member to follow the frame on its own tick, a frame after
 * the drag had moved the frame, so the strip ran ahead of the window on every
 * drag frame. Each case here is one of the ways the two must be one piece:
 *
 *   - the attached window lands under its parent in the SAME Build the
 *     parent moved in (no host tick between);
 *   - Shift+Alt held, a press anywhere on either picks the compound up
 *     (ToriRSChrome_SetModifiers) -- and without the modifiers does not;
 *   - the window's grip and the compound's edges resize the pair: the
 *     frame's x and width, the window's height; the parent is what records
 *     `user_placed`;
 *   - a maximise of either maximises the compound, a restore puts both back;
 *   - no prim with an EMPTY clip reaches the display list, and every prim's
 *     clip lies inside its panel: a keycap on a row below the fold is not
 *     drawn below the panel (a zero scissor is "no scissor" to the renderers).
 */

#include "ui/uitree_debug_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        g_checks++;                                                            \
        if( !(cond) )                                                          \
        {                                                                      \
            g_failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while( 0 )

static struct ToriRSChrome g_ui; /* ~5 MB: never on the stack */

static void
drag(int x0, int y0, int x1, int y1)
{
    ToriRSChrome_MouseMove(&g_ui, x0, y0);
    ToriRSChrome_MouseDown(&g_ui, x0, y0);
    ToriRSChrome_MouseMove(&g_ui, x1, y1);
    ToriRSChrome_MouseUp(&g_ui, x1, y1);
}

static int
in_rect(struct ToriRSChromeRect r, struct ToriRSChromeRect in)
{
    return r.x >= in.x && r.y >= in.y && r.x + r.w <= in.x + in.w && r.y + r.h <= in.y + in.h;
}

static char const* const k_tabs[] = { "Catalog", "Locs", "Spawns" };

int
main(void)
{
    int frame;
    int member;
    int other;
    struct ToriRSChromeRect f;
    struct ToriRSChromeRect m;

    ToriRSChrome_Init(&g_ui);
    ToriRSChrome_SetSurface(&g_ui, 1280, 800);

    /* The frame: a titled window with a tab strip and a fixed width. The
     * member: a titleless window with rows and a grip, attached under it. */
    frame = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 100, 60, 240, "Catalog / Locs / Spawns");
    (void)ToriRSChrome_Tabs(&g_ui, frame, k_tabs, 3, 0);
    member = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 0, "");
    for( int i = 0; i < 6; i++ )
    {
        char line[32];
        snprintf(line, sizeof(line), "row %d", i);
        (void)ToriRSChrome_Label(&g_ui, member, line);
    }
    ToriRSChrome_PanelSetResizable(&g_ui, member, 1);
    ToriRSChrome_PanelSetFixedWidth(&g_ui, member, 240);
    ToriRSChrome_PanelSetFixedHeight(&g_ui, member, 200);
    ToriRSChrome_PanelSetScrollable(&g_ui, member, 1);
    ToriRSChrome_PanelAttach(&g_ui, member, frame);
    CHECK(ToriRSChrome_PanelAttachedTo(&g_ui, member) == frame, "the member says who it hangs under");
    CHECK(ToriRSChrome_PanelAttachedChild(&g_ui, frame) == member, "the frame names its visible child");
    /* An unrelated window beside them, for the drags that must not move it. */
    other = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 600, 60, 160, "Other");
    (void)ToriRSChrome_Label(&g_ui, other, "a row");

    /* ---- placed under the parent by the Build ------------------------------- */
    ToriRSChrome_Build(&g_ui);
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    CHECK(f.w == 240 && f.h > 0, "the frame built (%dx%d)", f.w, f.h);
    CHECK(m.x == f.x && m.y == f.y + f.h && m.w == f.w && m.h == 200,
        "the member sits under the frame, its width (%d,%d %dx%d under %d,%d %dx%d)", m.x, m.y, m.w, m.h, f.x,
        f.y, f.w, f.h);
    {
        struct ToriRSChromeRect const e = ToriRSChrome_PanelExtent(&g_ui, member);
        CHECK(e.x == f.x && e.y == f.y && e.w == f.w && e.h == f.h + m.h, "the extent is the pair's outline (%d,%d %dx%d)",
            e.x, e.y, e.w, e.h);
    }

    /* ---- a title drag moves both in ONE Build ------------------------------- */
    ToriRSChrome_MouseMove(&g_ui, f.x + 30, f.y + 6);
    ToriRSChrome_MouseDown(&g_ui, f.x + 30, f.y + 6);
    ToriRSChrome_MouseMove(&g_ui, f.x + 30 + 50, f.y + 6 + 30);
    /* Mid-drag: one Build, no host tick in between. */
    ToriRSChrome_Build(&g_ui);
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    CHECK(f.x == 150 && f.y == 90, "the frame followed the pointer (%d,%d)", f.x, f.y);
    CHECK(m.x == f.x && m.y == f.y + f.h, "and the member is under it in the same Build (%d,%d for %d,%d)", m.x,
        m.y, f.x, f.y + f.h);
    ToriRSChrome_MouseUp(&g_ui, f.x + 30, f.y + 6);
    ToriRSChrome_Build(&g_ui);
    CHECK(g_ui.panels[frame].user_placed, "a dragged frame is the user's");
    CHECK(!g_ui.panels[member].user_placed, "the member is not marked: the frame is the layout's handle");

    /* ---- Shift+Alt: anywhere on the member picks the compound up ------------ */
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    {
        int const x = m.x + m.w / 2;
        int const y = m.y + m.h / 2;
        struct ToriRSChromeRect o = ToriRSChrome_PanelRect(&g_ui, other);
        /* Without the modifiers a press on the body is a press on a row. */
        drag(x, y, x + 40, y + 40);
        ToriRSChrome_Build(&g_ui);
        CHECK(ToriRSChrome_PanelRect(&g_ui, frame).x == f.x && ToriRSChrome_PanelRect(&g_ui, frame).y == f.y,
            "a plain drag on the body moves nothing");
        ToriRSChrome_SetModifiers(&g_ui, TORIRS_CHROME_MOD_SHIFT | TORIRS_CHROME_MOD_ALT);
        drag(x, y, x + 40, y + 40);
        ToriRSChrome_Build(&g_ui);
        CHECK(ToriRSChrome_PanelRect(&g_ui, frame).x == f.x + 40 && ToriRSChrome_PanelRect(&g_ui, frame).y == f.y + 40,
            "Shift+Alt+drag on the member moved the frame (%d,%d for %d,%d)", ToriRSChrome_PanelRect(&g_ui, frame).x,
            ToriRSChrome_PanelRect(&g_ui, frame).y, f.x + 40, f.y + 40);
        m = ToriRSChrome_PanelRect(&g_ui, member);
        f = ToriRSChrome_PanelRect(&g_ui, frame);
        CHECK(m.x == f.x && m.y == f.y + f.h, "and the member came with it");
        CHECK(ToriRSChrome_PanelRect(&g_ui, other).x == o.x, "the other window did not move");
        /* The other window, by its body too. */
        drag(o.x + o.w / 2, o.y + o.h - 8, o.x + o.w / 2 + 25, o.y + o.h - 8 + 15);
        ToriRSChrome_Build(&g_ui);
        CHECK(ToriRSChrome_PanelRect(&g_ui, other).x == o.x + 25 && ToriRSChrome_PanelRect(&g_ui, other).y == o.y + 15,
            "Shift+Alt+drag moves a plain window by its body too (%d,%d)", ToriRSChrome_PanelRect(&g_ui, other).x,
            ToriRSChrome_PanelRect(&g_ui, other).y);
        ToriRSChrome_SetModifiers(&g_ui, 0);
    }

    /* ---- the member's grip resizes the compound ----------------------------- */
    g_ui.panels[frame].user_placed = 0;
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    drag(m.x + m.w - 3, m.y + m.h - 3, m.x + m.w - 3 + 60, m.y + m.h - 3 + 40);
    ToriRSChrome_Build(&g_ui);
    CHECK(g_ui.panels[frame].fixed_w == 300 && g_ui.panels[member].fixed_w == 300,
        "the grip widened frame and member alike (%d, %d)", g_ui.panels[frame].fixed_w, g_ui.panels[member].fixed_w);
    CHECK(g_ui.panels[member].fixed_h == 240, "and made the member taller by the drag (%d)", g_ui.panels[member].fixed_h);
    CHECK(ToriRSChrome_PanelRect(&g_ui, frame).x == f.x && ToriRSChrome_PanelRect(&g_ui, frame).y == f.y,
        "the frame's origin stayed");
    CHECK(ToriRSChrome_PanelRect(&g_ui, frame).h == f.h, "the frame kept its own height (%d)",
        ToriRSChrome_PanelRect(&g_ui, frame).h);
    CHECK(g_ui.panels[frame].user_placed, "a resize marks the frame the user's");

    /* ---- the compound's LEFT edge, pressed on the member -------------------- */
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    drag(m.x + 1, m.y + m.h / 2, m.x + 1 - 20, m.y + m.h / 2);
    ToriRSChrome_Build(&g_ui);
    CHECK(ToriRSChrome_PanelRect(&g_ui, frame).x == f.x - 20 && g_ui.panels[frame].fixed_w == 320,
        "the left edge moved the frame and widened it (%d, %d)", ToriRSChrome_PanelRect(&g_ui, frame).x,
        g_ui.panels[frame].fixed_w);
    CHECK(ToriRSChrome_PanelRect(&g_ui, member).x == f.x - 20 && g_ui.panels[member].fixed_w == 320,
        "the member with it");
    CHECK(g_ui.panels[member].fixed_h == 240, "its height untouched (%d)", g_ui.panels[member].fixed_h);

    /* ---- the compound's TOP edge, pressed on the frame ---------------------- */
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    drag(f.x + f.w / 2, f.y + 1, f.x + f.w / 2, f.y + 1 + 30);
    ToriRSChrome_Build(&g_ui);
    CHECK(ToriRSChrome_PanelRect(&g_ui, frame).y == f.y + 30, "the top edge moved the frame down (%d)",
        ToriRSChrome_PanelRect(&g_ui, frame).y);
    CHECK(g_ui.panels[member].fixed_h == 210, "and took the height off the member (%d)", g_ui.panels[member].fixed_h);
    CHECK(ToriRSChrome_PanelRect(&g_ui, member).y + ToriRSChrome_PanelRect(&g_ui, member).h == m.y + m.h,
        "the compound's bottom stayed where it was");
    /* The seam between the two is not an edge of anything: a press there is
     * no resize. */
    f = ToriRSChrome_PanelRect(&g_ui, frame);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    {
        int edge_panel = -1;
        CHECK(ToriRSChrome_PanelEdgeAt(&g_ui, m.x + m.w / 2, m.y + 1, &edge_panel) == 0,
            "the member's top (the seam) is no edge");
        CHECK(ToriRSChrome_PanelEdgeAt(&g_ui, f.x + f.w / 2, f.y + f.h - 1, &edge_panel) == 0,
            "the frame's bottom (the seam) is no edge");
        CHECK(ToriRSChrome_PanelEdgeAt(&g_ui, m.x + m.w - 2, m.y + m.h / 2, &edge_panel) == TORIRS_CHROME_EDGE_RIGHT
                  && edge_panel == frame,
            "the member's right is the compound's edge, named by the root (%d)", edge_panel);
    }

    /* ---- maximise the member: the compound fills the area ------------------- */
    {
        struct ToriRSChromeRect area = { 0, 20, 1280, 760 };
        struct ToriRSChromeRect before_f = ToriRSChrome_PanelRect(&g_ui, frame);
        int const before_h = g_ui.panels[member].fixed_h;
        int fx = 0;
        int fy = 0;
        int fw = 0;
        int fh = 0;
        int placed = 0;
        ToriRSChrome_PanelMaximise(&g_ui, member, area);
        ToriRSChrome_Build(&g_ui);
        f = ToriRSChrome_PanelRect(&g_ui, frame);
        m = ToriRSChrome_PanelRect(&g_ui, member);
        CHECK(ToriRSChrome_PanelIsMaximised(&g_ui, member) && ToriRSChrome_PanelIsMaximised(&g_ui, frame),
            "both members of the compound read as maximised");
        CHECK(f.x == 0 && f.y == 20 && f.w == 1280, "the frame heads the area (%d,%d %dx%d)", f.x, f.y, f.w, f.h);
        CHECK(m.x == 0 && m.y == f.y + f.h && m.w == 1280 && m.y + m.h == 780,
            "the member takes the rest of it (%d,%d %dx%d)", m.x, m.y, m.w, m.h);
        CHECK(ToriRSChrome_PanelRestoreBox(&g_ui, member, &fx, &fy, &fw, &fh, &placed) && fh == before_h,
            "the member keeps the height it restores to (%d)", fh);
        CHECK(ToriRSChrome_PanelRestoreBox(&g_ui, frame, &fx, &fy, &fw, &fh, &placed) && fx == before_f.x
                  && fy == before_f.y && placed,
            "the frame keeps its box and that it was the user's");
        ToriRSChrome_PanelRestore(&g_ui, frame);
        ToriRSChrome_Build(&g_ui);
        f = ToriRSChrome_PanelRect(&g_ui, frame);
        CHECK(!ToriRSChrome_PanelIsMaximised(&g_ui, member) && !ToriRSChrome_PanelIsMaximised(&g_ui, frame),
            "a restore of the frame restores both");
        CHECK(f.x == before_f.x && f.y == before_f.y && f.w == before_f.w && g_ui.panels[member].fixed_h == before_h,
            "to the boxes they had (%d,%d %dx%d, member %d)", f.x, f.y, f.w, f.h, g_ui.panels[member].fixed_h);
    }

    /* ---- detach: a window of its own again ---------------------------------- */
    ToriRSChrome_PanelAttach(&g_ui, member, -1);
    ToriRSChrome_PanelMove(&g_ui, member, 400, 400);
    ToriRSChrome_Build(&g_ui);
    m = ToriRSChrome_PanelRect(&g_ui, member);
    CHECK(m.x == 400 && m.y == 400, "detached, the member keeps its own place (%d,%d)", m.x, m.y);
    CHECK(ToriRSChrome_PanelAttachedChild(&g_ui, frame) < 0, "the frame has no child");

    /* ---- the clip rule: nothing drawn with an empty clip, nothing outside --- */
    {
        int list;
        int panel;
        int count = 0;
        struct ToriRSChromePrim const* prims;
        struct ToriRSChromeRect box;
        int empty = 0;
        int outside = 0;
        int keycaps = 0;

        ToriRSChrome_Init(&g_ui);
        ToriRSChrome_SetSurface(&g_ui, 1280, 800);
        /* A hand-sized, scrolling panel holding a cells list far taller than
         * it: its lower rows are past the fold. The Catalog's list is this
         * shape when a group's window is shorter than the list. */
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 40, 40, 400, "Catalog");
        list = ToriRSChrome_VListCells(&g_ui, panel, "", 24, 24);
        for( int row = 0; row < 24; row++ )
        {
            char line[128];
            snprintf(line, sizeof(line), "0" TORIRS_CHROME_CELL_SEP_STR "mcannon%d" TORIRS_CHROME_CELL_SEP_STR
                                         "Cannon" TORIRS_CHROME_CELL_SEP_STR "KA KB" TORIRS_CHROME_CELL_SEP_STR "%d",
                row, row);
            ToriRSChrome_VListSetRow(&g_ui, list, row, line);
        }
        ToriRSChrome_PanelSetFixedHeight(&g_ui, panel, 160);
        /* Scrolling, as a group's window is: a row half off the fold is
         * DRAWN against the scroll window's clip (a panel that does not
         * scroll drops the row whole). */
        ToriRSChrome_PanelSetScrollable(&g_ui, panel, 1);
        ToriRSChrome_Build(&g_ui);
        box = ToriRSChrome_PanelRect(&g_ui, panel);
        prims = ToriRSChrome_Prims(&g_ui, &count);
        CHECK(count > 20, "the list drew (%d prims)", count);
        for( int i = 0; i < count; i++ )
        {
            struct ToriRSChromePrim const* p = &prims[i];
            if( p->clip.w <= 0 || p->clip.h <= 0 )
                empty++;
            else if( !in_rect(p->clip, box) )
                outside++;
            if( p->kind == TORIRS_CHROME_PRIM_TEXT && p->text && (strcmp(p->text, "KA") == 0 || strcmp(p->text, "KB") == 0) )
                keycaps++;
        }
        CHECK(empty == 0, "no prim carries an empty clip (%d did: a zero scissor is no scissor downstream)", empty);
        CHECK(outside == 0, "every prim's clip lies inside its panel (%d did not)", outside);
        /* The keycaps that are drawn belong to rows inside the box: a panel
         * 160 px tall shows a few rows, not 24. */
        CHECK(keycaps > 0 && keycaps < 2 * 24, "keycaps drawn only for the rows that show (%d)", keycaps);
    }

    printf("chrome_panel_attach_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
