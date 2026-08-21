/*
 * The CS2 chrome executor: the plugin window as a game interface.
 *
 * A NATIVE-WIDGET executor (see the two kinds in torirs_chrome_exec.h) whose
 * "native" is the game's own interface tree. Every row becomes real components
 * -- a RECT for a checkbox's box, a TEXT for its label -- built through the
 * same UITree_PushBuildComponent the client uses for any programmatic panel,
 * and clicked through the same hit test as any cache-authored interface.
 *
 * WHY THIS ONE IS DIFFERENT. The other executors reach for a foreign toolkit;
 * this one reaches for the toolkit the game already is. That buys the one thing
 * none of the others can: the plugin window looks like the game, scales with
 * the game, and is drawn by the same pass in the same order -- no second
 * window, no second theme, no second thing to keep in step when the gameframe
 * changes. It also needs no new platform code at all, which is why it is the
 * only executor that works on every lane at once.
 *
 * COMPONENT IDS ARE ALLOCATED FROM A PRIVATE RANGE. A component id is how a
 * click gets home: the tree reports `clicked_com_id`, the host recognises the
 * range as this executor's and turns it into an intent. The range is high and
 * fixed (TORIRS_CHROME_CS2_ID_BASE), well clear of anything a cache ships, so
 * the recognition is a bounds test rather than a registry.
 *
 * The tree and its mounting point are INJECTED, not reached for: ui/ owns the
 * tree type but not the App that holds one, and which slot a panel mounts in is
 * a gameframe question this file has no business answering.
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_mirror.h"
#include "uitree.h"
#include "uitree_build.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Layout, in interface pixels. The gameframe's own scale applies on top. */
#define CS2_PAD 6
#define CS2_ROW_H 18
#define CS2_ROW_GAP 3
#define CS2_LABEL_W 104
#define CS2_TAB_H 18
#define CS2_TAB_W 92
#define CS2_BOX 10
#define CS2_PANEL_W 280
#define CS2_PANEL_H 240

/* The reference interface palette, the same values toridbg_theme_osrs carries:
 * a window built here and a cache-authored one on screen together have to read
 * as one system. */
#define CS2_COL_BODY 0x5D5447
#define CS2_COL_CHROME 0x000000
#define CS2_COL_TEXT 0xFFFFFF
#define CS2_COL_DIM 0xC8C8C8
#define CS2_COL_ACCENT 0xFFFF00
#define CS2_COL_ON 0x00FF00

/* The two scroll arrows' component ids, in the executor's own range. */
#define CS2_ID_SCROLL_UP (TORIRS_CHROME_CS2_ID_BASE + 0x20)
#define CS2_ID_SCROLL_DOWN (TORIRS_CHROME_CS2_ID_BASE + 0x21)
/** Width of the scroll column. Always reserved -- see cs2_rebuild. */
#define CS2_SCROLL_W 14

struct ChromeCs2
{
    struct UITree* tree;
    /** Node index of the container everything mounts under, from the host;
     *  -1 means "its own root". */
    int32_t mount;
    /** Scene font id every TEXT component draws in. Zero draws nothing, which
     *  is exactly what the first version of this did. */
    int font_id;
    /** Node index of the panel layer this executor owns, or -1. */
    int32_t panel_node;
    int open;
    int dirty;
    struct ToriRSChromeMirror mirror;

    /**
     * Rows scrolled past, in whole rows.
     *
     * Rows rather than pixels because every row here is the same height, so a
     * row index is the honest unit and there is no partial row to clip. The
     * in-canvas chrome scrolls by pixels because its rows are NOT uniform -- a
     * model view, a separator and a text input are three different heights.
     */
    int scroll_row;
    /** Which panel owns the strip, and its titles. One window, one strip. */
    int tab_panel;
    int tab_strip_widget;
    char tabs[TORIRS_CHROME_CS2_TABS_MAX][48];
    int tab_count;

    /* Per widget, the strings the tree needs. The tree BORROWS text pointers,
     * so unlike the other executors this one must hold the strings itself --
     * a DOM node and an EDIT control own their text; a TEXT component points
     * at yours. */
    char label[TORIDBG_MAX_WIDGETS][TORIDBG_LABEL_MAX];
    char text[TORIDBG_MAX_WIDGETS][TORIRS_CHROME_TEXT_MAX];
    int checked[TORIDBG_MAX_WIDGETS];
};

static struct ChromeCs2 g_chrome_cs2;

int
ToriRSChrome_TreeAcceptsChrome(struct UITree const* tree)
{
    int group;

    if( !tree || tree->root_index < 0 )
        return 0;
    if( (uint32_t)tree->root_index >= tree->component_count )
        return 0;
    group = (tree->components[tree->root_index].component_id >> 16) & 0xffff;
    /* Our own group already sitting first means chrome got in ahead of the
     * gameframe -- the state this exists to keep out of. */
    return group != TORIRS_CHROME_CS2_GROUP;
}

/* ---- building the interface ---------------------------------------------- */

static int
cs2_resolve_sprite(void* ud, int graphic)
{
    (void)ud;
    /* Nothing here draws a cache sprite: the window is rectangles and text, so
     * every graphic id would be a lookup with no answer. */
    (void)graphic;
    return -1;
}

static int
cs2_resolve_font(void* ud, int font)
{
    (void)ud;
    /* Identity: the ids handed to cs2_text are already scene font ids, because
     * this panel is built from the gameframe's own resolved faces rather than
     * from cache font ids that would still need looking up. */
    return font;
}

/** A filled or outlined rectangle. */
static int32_t
cs2_rect(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w, int h,
    int color, int filled)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = com_id;
    comp.type = UIBUILD_RECT;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = y;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.color = color;
    comp.filled = filled;
    return UITree_PushBuildComponent(
        s->tree, parent, &comp, cs2_resolve_sprite, cs2_resolve_font, s);
}

/**
 * A line of text.
 *
 * The string is handed to the BUILDER, not written into the component
 * afterwards: the tree OWNS its text (UITree_PushBuildComponent strdups it and
 * the node teardown frees it), so assigning a pointer of ours into the field
 * would have the tree free memory it never allocated the next time the panel
 * was rebuilt. That is a heap corruption rather than a leak, and it is exactly
 * what the first version of this did.
 */
static int32_t
cs2_text(
    struct ChromeCs2* s, int32_t parent, int com_id, int x, int y, int w,
    char const* str, int color)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = com_id;
    comp.type = UIBUILD_TEXT;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = y;
    comp.base_width = w;
    comp.base_height = CS2_ROW_H;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.text = str;
    comp.color = color;
    comp.shadowed = 1;
    comp.font_id = s->font_id;
    /* Centred vertically in its own row box. A TEXT component's y is the box
     * TOP and the glyphs hang from a baseline inside it, so without this every
     * label sits high in its row and a row beside a checkbox looks misaligned
     * with the box it labels. */
    comp.text_v_align = 1;
    return UITree_PushBuildComponent(
        s->tree, parent, &comp, cs2_resolve_sprite, cs2_resolve_font, s);
}

/*
 * Is the panel we built still in the tree?
 *
 * It is not enough to remember the node index. The client rebuilds the whole
 * interface tree whenever the gameframe changes -- a resize, a revision's
 * onload, an interface opening -- and every node in it goes, this executor's
 * root included. Nothing tells us: the index stays in range and simply names
 * something else now, so the panel silently stops existing while the executor
 * believes it is up. Checking the COMPONENT ID rather than the index is what
 * catches that, because a rebuilt tree will not have put ours back.
 *
 * Same failure as cache-authored hooks being dropped by a rebuild: the fix is
 * to notice and re-declare, not to hold an index tighter.
 */
static int
cs2_panel_alive(struct ChromeCs2 const* s)
{
    if( !s->tree || s->panel_node < 0 )
        return 0;
    if( (uint32_t)s->panel_node >= s->tree->component_count )
        return 0;
    return s->tree->components[s->panel_node].component_id == TORIRS_CHROME_CS2_ID_BASE;
}

/*
 * Rebuild the whole panel.
 *
 * Wholesale, unlike every other executor: the tree has no cheap "move this one
 * component" for a panel whose row set just changed, and a rebuild of a
 * hundred components is a handful of microseconds that runs only when the
 * window's SHAPE moved. Property changes (a label, a checkbox) write into the
 * component that already exists and do not come through here at all -- which is
 * what keeps this off the per-frame path.
 */
static void
cs2_rebuild(struct ChromeCs2* s)
{
    int32_t panel;
    int y;
    int panel_w;
    int panel_h;
    int com = TORIRS_CHROME_CS2_ID_BASE;

    if( !s->tree || !ToriRSChrome_TreeAcceptsChrome(s->tree) )
        return;

    if( cs2_panel_alive(s) )
        UITree_ClearChildren(s->tree, s->panel_node);
    else
    {
        /* Gone with a tree rebuild, or never made. Either way the index we
         * hold means nothing now, so a fresh one rather than a clear. */
        struct UIBuildComponent layer;
        s->panel_node = -1;
        memset(&layer, 0, sizeof(layer));
        layer.id = com++;
        layer.type = UIBUILD_LAYER;
        layer.parent_id = -1;
        layer.if3 = 1;
        layer.graphic = -1;
        layer.graphic_active = -1;
        layer.model_active_id = -1;
        layer.model_seq_id = -1;
        if( s->mount >= 0 )
        {
            /*
             * Pure fill-parent, exactly as xptracker, loottools and hiscores
             * are authored: their roots carry no chrome because the strip's
             * nine-slice frame is a SIBLING under popout:frame. A panel that
             * placed and sized itself here would sit inside that frame at its
             * own size, which is a box in a box.
             */
            layer.x_mode = 1;
            layer.y_mode = 1;
            layer.width_mode = 1;
            layer.height_mode = 1;
        }
        else
        {
            layer.base_x = 8;
            layer.base_y = 8;
            layer.base_width = CS2_PANEL_W;
            layer.base_height = CS2_PANEL_H;
        }
        s->panel_node = UITree_PushBuildComponent(
            s->tree, s->mount, &layer, cs2_resolve_sprite, cs2_resolve_font, s);
        if( s->panel_node < 0 )
            return;
    }
    panel = s->panel_node;

    /*
     * The panel's own box.
     *
     * Mounted in the strip we take the slot's resolved size and draw NO body
     * and NO border: popout:frame already draws both around us, and a second
     * set inside it is the box-in-a-box the fill-parent modes above exist to
     * avoid. Standalone we are the whole window and have to draw them.
     */
    if( s->mount >= 0 )
    {
        struct UITreeComponent const* box = &s->tree->components[panel];
        panel_w = box->position.width > 0 ? box->position.width : CS2_PANEL_W;
        panel_h = box->position.height > 0 ? box->position.height : CS2_PANEL_H;
    }
    else
    {
        panel_w = CS2_PANEL_W;
        panel_h = CS2_PANEL_H;
        cs2_rect(s, panel, com++, 0, 0, panel_w, panel_h, CS2_COL_BODY, 1);
        cs2_rect(s, panel, com++, 0, 0, panel_w, panel_h, CS2_COL_CHROME, 0);
    }

    /* The tab strip, pinned above the rows -- the same rule the in-canvas
     * chrome follows, and for the same reason. */
    y = CS2_PAD;
    if( s->tab_count > 1 )
    {
        for( int t = 0; t < s->tab_count; t++ )
        {
            int const x = CS2_PAD + t * (CS2_TAB_W + 2);
            int const on = (t == s->mirror.panels[s->tab_panel].active_tab);
            int32_t const box = cs2_rect(
                s,
                panel,
                TORIRS_CHROME_CS2_ID_TAB_BASE + t,
                x,
                y,
                CS2_TAB_W,
                CS2_TAB_H,
                CS2_COL_CHROME,
                on ? 0 : 1);
            /* By COMPONENT ID, not node index: ApplyClickMask looks the
             * component up, and handing it an index silently masks whatever
             * component happens to carry that number -- or nothing at all,
             * which is a control that draws and cannot be clicked. */
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, TORIRS_CHROME_CS2_ID_TAB_BASE + t, 1);
            cs2_text(
                s, panel, -1, x + 4, y + 3, CS2_TAB_W - 8, s->tabs[t],
                on ? CS2_COL_TEXT : CS2_COL_DIM);
        }
        y += CS2_TAB_H + CS2_ROW_GAP;
    }

    /* In ROW order, not handle order: the free list recycles handles, so a
     * rebuilt panel walked by index puts Save above the settings it commits. */
    {
    int order[TORIDBG_MAX_WIDGETS];
    int const shown_count = ToriRSChromeMirror_Order(&s->mirror, order, TORIDBG_MAX_WIDGETS);
    /*
     * The scroll column is reserved up front, not once an overflow is found.
     *
     * A column that appeared only when rows overflowed would reflow every row
     * the moment one was added, and a settings tab that jumps sideways as it
     * fills reads as a bug rather than as a scrollbar arriving.
     */
    int const row_w = panel_w - 2 * CS2_PAD - CS2_SCROLL_W;
    int visible = 0;
    int drawn = 0;
    int skipped = 0;

    for( int oi = 0; oi < shown_count; oi++ )
    {
        int const i = order[oi];
        if( i == s->tab_strip_widget )
            continue;
        if( ToriRSChromeMirror_Shown(&s->mirror, i) )
            visible++;
    }
    /* Clamped here rather than at the arrows: a tab switch or a reload can
     * shorten the list under a scroll offset, and an offset past the end shows
     * an empty panel with no way back. */
    if( s->scroll_row > visible - 1 )
        s->scroll_row = visible - 1;
    if( s->scroll_row < 0 )
        s->scroll_row = 0;

    for( int oi = 0; oi < shown_count; oi++ )
    {
        int const i = order[oi];
        struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, i);
        int const id = TORIRS_CHROME_CS2_ID_BASE + 0x100 + i;

        if( !w || !ToriRSChromeMirror_Shown(&s->mirror, i) )
            continue;
        if( i == s->tab_strip_widget )
            continue;
        /* Scrolled past: skipped, and NOT counted as placed. */
        if( skipped < s->scroll_row )
        {
            skipped++;
            continue;
        }
        /* Out of room. The break comes BEFORE the count, so the row that did
         * not fit is not counted as one that did -- counting it made
         * `placed + skipped == visible` and the arrows never appeared. */
        if( y + CS2_ROW_H > panel_h - CS2_PAD )
            break;
        drawn++;

        switch( w->kind )
        {
        case TORIDBG_W_CHECKBOX:
        {
            int32_t const box = cs2_rect(
                s, panel, id, CS2_PAD, y + 2, CS2_BOX, CS2_BOX, CS2_COL_CHROME, 1);
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, id, 1);
            if( s->checked[i] )
                cs2_rect(
                    s, panel, -1, CS2_PAD + 2, y + 4, CS2_BOX - 4, CS2_BOX - 4,
                    CS2_COL_ON, 1);
            cs2_text(
                s, panel, -1, CS2_PAD + CS2_BOX + 5, y, CS2_PANEL_W, s->label[i],
                CS2_COL_TEXT);
            break;
        }

        case TORIDBG_W_TEXTINPUT:
        case TORIDBG_W_DROPDOWN:
        {
            int const bx = CS2_PAD + CS2_LABEL_W;
            int const bw = CS2_PAD + row_w - bx;
            int32_t box;
            cs2_text(s, panel, -1, CS2_PAD, y, CS2_LABEL_W, s->label[i], CS2_COL_DIM);
            box = cs2_rect(s, panel, id, bx, y, bw, CS2_ROW_H, CS2_COL_CHROME, 1);
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, id, 1);
            cs2_text(s, panel, -1, bx + 3, y, bw - 6, s->text[i], CS2_COL_TEXT);
            break;
        }

        case TORIDBG_W_BUTTON:
        case TORIDBG_W_MENUITEM:
        {
            char const* caption = s->text[i][0] ? s->text[i] : s->label[i];
            int const bw = CS2_LABEL_W;
            int32_t const box =
                cs2_rect(s, panel, id, CS2_PAD, y, bw, CS2_ROW_H, CS2_COL_CHROME, 1);
            if( box >= 0 )
                UITree_ApplyClickMask(s->tree, id, 1);
            cs2_text(s, panel, -1, CS2_PAD + 6, y, bw - 12, caption, CS2_COL_ACCENT);
            break;
        }

        case TORIDBG_W_SEPARATOR:
            cs2_rect(
                s, panel, -1, CS2_PAD, y + CS2_ROW_H / 2, row_w, 1, CS2_COL_CHROME, 1);
            break;

        case TORIDBG_W_LABEL:
        default:
            cs2_text(
                s, panel, -1, CS2_PAD, y, row_w,
                s->text[i][0] ? s->text[i] : s->label[i], CS2_COL_TEXT);
            break;
        }
        y += CS2_ROW_H + CS2_ROW_GAP;
    }

    /*
     * Two arrows rather than a draggable bar.
     *
     * A bar means a grip whose length is a function of the content, a track to
     * hit-test against, and a drag held across frames -- three things the
     * in-canvas chrome already implements and that this would be a second copy
     * of, in a toolkit that has no drag. Two buttons are the whole affordance a
     * settings tab needs, and they are two more components.
     */
    if( s->scroll_row > 0 || skipped + drawn < visible )
    {
        int const ax = panel_w - CS2_PAD - CS2_SCROLL_W;
        int const top = CS2_PAD + (s->tab_count > 1 ? CS2_TAB_H + CS2_ROW_GAP : 0);
        int const bottom = panel_h - CS2_PAD - CS2_ROW_H;
        int32_t box;

        box = cs2_rect(
            s, panel, CS2_ID_SCROLL_UP, ax, top, CS2_SCROLL_W, CS2_ROW_H,
            CS2_COL_CHROME, 1);
        if( box >= 0 )
            UITree_ApplyClickMask(s->tree, CS2_ID_SCROLL_UP, 1);
        /* Dimmed at the end of its travel: an arrow that looks live and does
         * nothing is worse than one that says it cannot. */
        cs2_text(
            s, panel, -1, ax + 4, top, CS2_SCROLL_W, "^",
            s->scroll_row > 0 ? CS2_COL_ACCENT : CS2_COL_DIM);

        box = cs2_rect(
            s, panel, CS2_ID_SCROLL_DOWN, ax, bottom, CS2_SCROLL_W, CS2_ROW_H,
            CS2_COL_CHROME, 1);
        if( box >= 0 )
            UITree_ApplyClickMask(s->tree, CS2_ID_SCROLL_DOWN, 1);
        cs2_text(
            s, panel, -1, ax + 4, bottom, CS2_SCROLL_W, "v",
            skipped + drawn < visible ? CS2_COL_ACCENT : CS2_COL_DIM);
    }
    }

    UITree_MarkAllDirty(s->tree);
}

/* ---- the executor -------------------------------------------------------- */

static int
chrome_cs2_begin(void* user)
{
    struct ChromeCs2* s = user;

    assert(s);
    if( !s->tree )
    {
        fprintf(stderr, "chrome: no interface tree to build the plugin window in\n");
        return 0;
    }
    ToriRSChromeMirror_Init(&s->mirror);
    s->panel_node = -1;
    s->tab_panel = -1;
    s->tab_strip_widget = -1;
    s->tab_count = 0;
    s->open = 1;
    return 1;
}

static void
chrome_cs2_end(void* user)
{
    struct ChromeCs2* s = user;

    assert(s);
    if( !s->open )
        return;
    if( s->tree && s->panel_node >= 0 )
    {
        UITree_ClearChildren(s->tree, s->panel_node);
        UITree_MarkAllDirty(s->tree);
    }
    s->panel_node = -1;
    s->open = 0;
}

static void
chrome_cs2_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeCs2* s = user;
    int shape;

    assert(s);
    assert(cmd);
    if( !s->open )
        return;

    shape = ToriRSChromeMirror_Apply(&s->mirror, cmd);

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_SYNC_END:
        /* One rebuild per frame at most, and only when something moved -- or
         * when the tree threw our panel away. A rebuild per command would tear
         * the panel down and put it back up several times for a single tab
         * switch. */
        if( s->dirty || !cs2_panel_alive(s) )
        {
            cs2_rebuild(s);
            s->dirty = 0;
        }
        return;

    case TORIRS_CHROME_CMD_WIDGET_ADD:
        if( cmd->widget >= 0 && cmd->widget < TORIDBG_MAX_WIDGETS )
        {
            snprintf(
                s->label[cmd->widget], sizeof(s->label[0]), "%s", cmd->label);
            snprintf(s->text[cmd->widget], sizeof(s->text[0]), "%s", cmd->text);
            s->checked[cmd->widget] = 0;
        }
        if( cmd->value == TORIDBG_W_TABSTRIP )
        {
            s->tab_panel = cmd->panel;
            s->tab_strip_widget = cmd->widget;
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_LABEL:
        if( cmd->widget >= 0 && cmd->widget < TORIDBG_MAX_WIDGETS )
            snprintf(s->label[cmd->widget], sizeof(s->label[0]), "%s", cmd->label);
        /* The tree BORROWS the string, so a rewrite in place is already on
         * screen -- but only where the layout does not depend on it. It does
         * not here: every row is a fixed height. */
        break;

    case TORIRS_CHROME_CMD_WIDGET_TEXT:
        if( cmd->widget >= 0 && cmd->widget < TORIDBG_MAX_WIDGETS )
            snprintf(s->text[cmd->widget], sizeof(s->text[0]), "%s", cmd->text);
        break;

    case TORIRS_CHROME_CMD_WIDGET_CHECKED:
        if( cmd->widget >= 0 && cmd->widget < TORIDBG_MAX_WIDGETS )
            s->checked[cmd->widget] = cmd->value;
        /* The tick is its own component, so this one DOES change the shape. */
        s->dirty = 1;
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTION:
        if( cmd->widget == s->tab_strip_widget && cmd->value >= 0 &&
            cmd->value < TORIRS_CHROME_CS2_TABS_MAX )
        {
            snprintf(s->tabs[cmd->value], sizeof(s->tabs[0]), "%s", cmd->text);
            if( cmd->value + 1 > s->tab_count )
                s->tab_count = cmd->value + 1;
            s->dirty = 1;
        }
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTIONS:
        if( cmd->widget == s->tab_strip_widget )
        {
            s->tab_count = 0;
            memset(s->tabs, 0, sizeof(s->tabs));
            s->dirty = 1;
        }
        break;

    default:
        break;
    }

    if( shape )
        s->dirty = 1;
}

static int
chrome_cs2_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeCs2* s = user;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;
    /* The clicks themselves arrive through ToriRSChromeExecCs2_Click, called by
     * the host's interface dispatch when it recognises a component id in this
     * executor's range. Nothing is pumped here. */
    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

int
ToriRSChromeExecCs2_Click(int component_id)
{
    struct ChromeCs2* s = &g_chrome_cs2;

    if( !s->open || component_id < TORIRS_CHROME_CS2_ID_BASE )
        return 0;

    if( component_id == CS2_ID_SCROLL_UP || component_id == CS2_ID_SCROLL_DOWN )
    {
        int const before = s->scroll_row;
        s->scroll_row += component_id == CS2_ID_SCROLL_DOWN ? 1 : -1;
        if( s->scroll_row < 0 )
            s->scroll_row = 0;
        /* The upper bound is the rebuild's job -- it is the only place that
         * knows how many rows are visible on this tab. */
        if( s->scroll_row != before )
            s->dirty = 1;
        return 1;
    }

    if( component_id >= TORIRS_CHROME_CS2_ID_TAB_BASE &&
        component_id < TORIRS_CHROME_CS2_ID_TAB_BASE + TORIRS_CHROME_CS2_TABS_MAX )
    {
        struct ToriRSChromeIntent intent;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_TAB;
        intent.panel = s->tab_panel;
        intent.widget = s->tab_strip_widget;
        intent.value = component_id - TORIRS_CHROME_CS2_ID_TAB_BASE;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        /* A new tab is a different list, so an offset measured against the old
         * one means nothing -- and a tab that opens already scrolled reads as
         * a broken panel. */
        s->scroll_row = 0;
        return 1;
    }

    {
        int const handle = component_id - TORIRS_CHROME_CS2_ID_BASE - 0x100;
        struct ToriRSChromeMirrorWidget* w =
            ToriRSChromeMirror_Widget(&s->mirror, handle);
        if( !w )
            return 0;

        switch( w->kind )
        {
        case TORIDBG_W_CHECKBOX:
            /* Toggled from what this executor last drew, not from the model:
             * the model is what the intent is about to change, and reading it
             * here would need a second path back into it. */
            ToriRSChromeMirror_PushToggle(
                &s->mirror, w->panel, handle, !s->checked[handle]);
            return 1;

        case TORIDBG_W_TEXTINPUT:
        case TORIDBG_W_DROPDOWN:
            /*
             * Not editable yet: an interface text field means taking the
             * keyboard from the chatbox and giving it back, which is the
             * chat-input handoff the client already has one of and which needs
             * its own wiring here. Reported as an activation so a caller can
             * see the row was pressed rather than have it silently do nothing.
             */
            ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
            return 1;

        default:
            ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
            return 1;
        }
    }
}

struct ToriRSChromeExec
ToriRSChromeExec_Cs2(struct UITree* tree, int32_t mount_node, int font_id)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    memset(&g_chrome_cs2, 0, sizeof(g_chrome_cs2));
    g_chrome_cs2.tree = tree;
    g_chrome_cs2.mount = mount_node;
    g_chrome_cs2.font_id = font_id;
    g_chrome_cs2.panel_node = -1;

    exec.user = &g_chrome_cs2;
    exec.begin = chrome_cs2_begin;
    exec.apply = chrome_cs2_apply;
    exec.end = chrome_cs2_end;
    exec.poll = chrome_cs2_poll;
    return exec;
}
