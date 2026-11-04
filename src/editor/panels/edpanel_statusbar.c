/*
 * The status bar (module "statusbar"; design #workbench "Status bar and hint
 * strip", #hotkeys surface 4, rules R4 and R8): the one place the editor
 * talks back without a modal, a band along the foot of the window that the
 * layout reserves (Editor_UiLayout: the world, the dock column and the
 * console stop above it). Always on while the editor shows; it is no View-menu
 * row and has no title bar, frame or furniture.
 *
 * Left to right, one run per widget in the bar's one row:
 *
 *   the hint strip  the ARMED tool's live chords, "[Q] rotate", from
 *                   Editor_HotkeysHint over the contexts the shared tools make
 *                   live now (Editor_HotkeyContextsFrom: the same rules the
 *                   key dispatcher resolves by), so the strip lists what a key
 *                   does this frame and a rebind relabels it the next;
 *   the hover       the tile under the pointer, the level edits go to and the
 *                   square ("3222,3218 L1 | m50_50"), from
 *                   app_services.hover_tile; empty when nothing is hovered;
 *   the status      the last Editor_UiStatus sentence, for its 6 s;
 *   the server      "read-only" when FACT_HELLO said the server cannot save
 *                   its tree (the reason is the tooltip). FACT_HELLO carries
 *                   no lane name, and the Publish panel keeps its lane to
 *                   itself, so the bar names no lane rather than guess one.
 *
 * Every run is measured against its track (rule R2): hint rows that do not fit
 * the hint track are left out whole, the hover and the status end in "..." with
 * the full text as the tooltip. Nothing is clipped, and a frame where nothing
 * changed sets nothing and measures nothing.
 *
 * The bar is a MENUBAR-style strip -- the chrome's one horizontal row of
 * runs, its line box plus 4 px (20 px at 1x) -- holding LABELs, the widgets
 * the menu bar's own Level caption already is, so the web executor draws it
 * with no new command.
 */
#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "ui/torirs_chrome_metrics.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The tracks' shares of the bar's width, in percent: what is left after the
 * hint rows and the hover goes to the status sentence and the server chip. */
#define STATUSBAR_HINT_SHARE 45
#define STATUSBAR_HOVER_SHARE 20
#define STATUSBAR_HOVER_MAX 64
#define STATUSBAR_SERVER_MAX 32

struct StatusBarPanel
{
    int panel;
    int label_hints[EDITOR_HK_HINT_MAX];
    int label_hover;
    int label_status;
    int label_server;

    /* What the tracks hold, as last composed. */
    struct Editor_HotkeyHint hints[EDITOR_HK_HINT_MAX];
    int hint_count;
    char hover[STATUSBAR_HOVER_MAX];
    char status[EDITOR_UI_CONSOLE_LINE_MAX];
    char server[STATUSBAR_SERVER_MAX];

    /* The hint's inputs last frame: the rows are composed again when one
     * moves (a tool armed, a selection made, a rebind). */
    int hint_tool;
    unsigned hint_contexts;
    uint32_t hint_serial;
    /* The width and scale the runs were last fitted to; `refit` set when a
     * track's text changed since. */
    int fit_w;
    int fit_scale;
    int refit;
};

/** Pixels a run takes in the bar: its text in the bar's face plus the pad
 *  either side (dbg_build_menubar's pen advance). */
static int
statusbar_advance(
    const struct EditorUi* ui,
    const char* text)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    return ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, scale, text) + 2 * edp_px(ui, TORIRS_CHROME_M_PAD);
}

/**
 * `text` as a run of at most `width` pixels: whole when it fits, else its
 * longest prefix that fits with "..." after it, else "" (the run is left
 * out). Returns 1 when it was shortened.
 */
static int
statusbar_fit_text(
    const struct EditorUi* ui,
    const char* text,
    int width,
    char* out,
    int out_size)
{
    int length = (int)strlen(text);

    assert(out_size > 4);
    snprintf(out, (size_t)out_size, "%s", text);
    if( !text[0] || statusbar_advance(ui, out) <= width )
        return 0;
    if( length > out_size - 4 )
        length = out_size - 4;
    for( ; length > 0; length-- )
    {
        /* Never end on a space before the ellipsis: "tile ..." reads as a
         * word that is missing, "tile..." as a sentence that goes on. */
        if( text[length - 1] == ' ' )
            continue;
        snprintf(out, (size_t)out_size, "%.*s...", length, text);
        if( statusbar_advance(ui, out) <= width )
            return 1;
    }
    out[0] = '\0';
    return 1;
}

/** The tooltip of a shortened run: the full text, as it is when it fits the
 *  chrome's label, else shortened by hand (the chrome would log the cut). ""
 *  for a run shown whole. The console keeps every status sentence whole. */
static void
statusbar_tip_text(
    int shortened,
    const char* text,
    char* tip,
    int tip_size)
{
    assert(tip_size == TORIRS_CHROME_LABEL_MAX);
    if( !shortened )
        tip[0] = '\0';
    else if( (int)strlen(text) < tip_size )
        snprintf(tip, (size_t)tip_size, "%s", text);
    else
        snprintf(tip, (size_t)tip_size, "%.*s...", tip_size - 4, text);
}

/** One label: its run (hidden when empty) and its tooltip. */
static void
statusbar_set(
    struct EditorUi* ui,
    int widget,
    const char* run,
    const char* tip)
{
    ToriRSChrome_SetText(ui->chrome, widget, run);
    ToriRSChrome_SetHidden(ui->chrome, widget, run[0] == '\0');
    ToriRSChrome_SetTooltip(ui->chrome, widget, tip);
}

/** Lay the runs out on the bar's width: hint rows while they fit their
 *  track, then the hover, the server chip and the status in what is left. */
static void
statusbar_fit(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    int const width = ui->chrome->surface_w;
    int const budget = width - edp_px(ui, TORIRS_CHROME_M_PAD);
    int used = 0;
    int stopped = 0;
    char run[EDITOR_UI_CONSOLE_LINE_MAX];
    char tip[TORIRS_CHROME_LABEL_MAX];
    int shortened;

    for( int i = 0; i < EDITOR_HK_HINT_MAX; i++ )
    {
        int advance;

        run[0] = '\0';
        tip[0] = '\0';
        if( i < self->hint_count && !stopped )
        {
            snprintf(run, sizeof(run), "[%s] %s", self->hints[i].chord, self->hints[i].verb);
            advance = statusbar_advance(ui, run);
            /* A row is shown whole or not at all, and the rows after one
             * that does not fit stay out too: the order is the tool's. */
            if( used + advance > budget * STATUSBAR_HINT_SHARE / 100 )
            {
                stopped = 1;
                run[0] = '\0';
            }
            else
            {
                used += advance;
                if( self->hints[i].action >= 0 )
                    edp_hotkey_tip(ui, (enum Editor_HotkeyAction)self->hints[i].action,
                        Editor_HotkeyInfo((enum Editor_HotkeyAction)self->hints[i].action)->name, tip,
                        (int)sizeof(tip));
            }
        }
        statusbar_set(ui, self->label_hints[i], run, tip);
    }

    shortened = statusbar_fit_text(ui, self->hover, budget * STATUSBAR_HOVER_SHARE / 100, run, (int)sizeof(run));
    statusbar_tip_text(shortened, self->hover, tip, (int)sizeof(tip));
    statusbar_set(ui, self->label_hover, run, tip);
    if( run[0] )
        used += statusbar_advance(ui, run);

    /* The server chip is a word: whole, or not at all. */
    run[0] = '\0';
    if( self->server[0] && used + statusbar_advance(ui, self->server) <= budget )
    {
        snprintf(run, sizeof(run), "%s", self->server);
        used += statusbar_advance(ui, run);
    }
    ToriRSChrome_SetText(ui->chrome, self->label_server, run);
    ToriRSChrome_SetHidden(ui->chrome, self->label_server, run[0] == '\0');

    shortened = statusbar_fit_text(ui, self->status, budget - used, run, (int)sizeof(run));
    statusbar_tip_text(shortened, self->status, tip, (int)sizeof(tip));
    statusbar_set(ui, self->label_status, run, tip);

    self->fit_w = width;
    self->fit_scale = ToriRSChrome_Scale(ui->chrome);
    self->refit = 0;
}

/** Copy `text` into a track; 1 when it changed. */
static int
statusbar_track(
    char* track,
    int track_size,
    const char* text)
{
    if( strncmp(track, text, (size_t)track_size - 1) == 0 )
        return 0;
    snprintf(track, (size_t)track_size, "%s", text);
    return 1;
}

static void
statusbar_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct StatusBarPanel* self = opaque;
    struct Editor_HotkeyContextFacts facts;
    char hover[STATUSBAR_HOVER_MAX];
    int hovered = 0;
    int world_x = 0;
    int world_z = 0;
    int level = 0;
    unsigned contexts;

    if( !ui->chrome->panels[self->panel].visible )
        return;

    /* The hover readout, and whether the pointer is over the world: the
     * service answers only there. */
    hover[0] = '\0';
    if( ui->app_services.hover_tile )
        hovered = ui->app_services.hover_tile(ui->app_services.user_data, &world_x, &world_z, &level);
    if( hovered )
        snprintf(hover, sizeof(hover), "%d,%d L%d | m%d_%d", world_x, world_z, level, world_x >> 6, world_z >> 6);
    self->refit |= statusbar_track(self->hover, (int)sizeof(self->hover), hover);

    /* The hint: the armed tool under the contexts the dispatcher resolved
     * the keys in (app_editor_hotkeys.c leaves its mask on the EditorUi), so
     * a W-gated row shows exactly when the key acts -- over void or sky, or a
     * pinned level before its hover resolves, as much as over a tile. With
     * no dispatcher (a headless test) the same rules over what the editor
     * knows, the hover service standing in for the pointer. */
    memset(&facts, 0, sizeof(facts));
    facts.tool = (int)ui->tools.tool;
    if( ui->hotkey_contexts_valid )
        contexts = ui->hotkey_contexts;
    else
    {
        facts.pointer_in_world = hovered;
        facts.loc_count = ui->tools.loc_count;
        facts.spawn_count = Editor_Spawns_SelectionCount(ui);
        facts.loc_pick = ui->tools.pick_kind == 0 && ui->tools.pick_id >= 0;
        facts.rect_valid = ui->tools.rect.valid;
        contexts = Editor_HotkeyContextsFrom(&facts);
    }
    if( facts.tool != self->hint_tool || contexts != self->hint_contexts || ui->hotkeys.serial != self->hint_serial )
    {
        self->hint_tool = facts.tool;
        self->hint_contexts = contexts;
        self->hint_serial = ui->hotkeys.serial;
        self->hint_count = Editor_HotkeysHint(&ui->hotkeys, contexts, facts.tool, self->hints, EDITOR_HK_HINT_MAX);
        self->refit = 1;
    }

    self->refit |= statusbar_track(self->status, (int)sizeof(self->status), Editor_UiStatusText(ui));

    /* What FACT_HELLO said of the server's tree; the reason is the tip. */
    if( statusbar_track(
            self->server, (int)sizeof(self->server), ui->editor && !ui->editor->writable ? "read-only" : "") )
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        statusbar_tip_text(self->server[0] != '\0',
            ui->editor && ui->editor->read_only_reason[0] ? ui->editor->read_only_reason
                                                          : "the server cannot save its tree",
            tip, (int)sizeof(tip));
        ToriRSChrome_SetTooltip(ui->chrome, self->label_server, tip);
        self->refit = 1;
    }

    if( self->refit || self->fit_w != ui->chrome->surface_w || self->fit_scale != ToriRSChrome_Scale(ui->chrome) )
        statusbar_fit(ui, self);
}

/** The bar's runs are captions: a click on one is the bar's, and does
 *  nothing. */
static int
statusbar_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct StatusBarPanel* self = opaque;

    (void)ui;
    if( widget == self->label_hover || widget == self->label_status || widget == self->label_server )
        return 1;
    for( int i = 0; i < EDITOR_HK_HINT_MAX; i++ )
        if( widget == self->label_hints[i] )
            return 1;
    return 0;
}

static void
statusbar_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct StatusBarPanel* self = opaque;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, visible);
}

static void
statusbar_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_StatusBar_Register(struct EditorUi* ui)
{
    struct StatusBarPanel* self;
    struct EditorUiPanel desc;

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    /* No tool has been hinted yet: the first tick composes the rows. */
    self->hint_tool = -1;
    self->refit = 1;
    /* Titleless: no title bar, no frame, no furniture. Editor_UiLayout gives
     * it the surface's width and the foot. */
    self->panel =
        ToriRSChrome_PanelAdd(ui->chrome, TORIRS_CHROME_PANEL_MENUBAR, 0, 0, UITREE_LAYOUT_ROOT_W, "");
    assert(self->panel >= 0);
    for( int i = 0; i < EDITOR_HK_HINT_MAX; i++ )
    {
        self->label_hints[i] = ToriRSChrome_Label(ui->chrome, self->panel, "");
        assert(self->label_hints[i] >= 0);
        ToriRSChrome_SetHidden(ui->chrome, self->label_hints[i], 1);
    }
    self->label_hover = ToriRSChrome_Label(ui->chrome, self->panel, "");
    assert(self->label_hover >= 0);
    self->label_status = ToriRSChrome_Label(ui->chrome, self->panel, "");
    assert(self->label_status >= 0);
    self->label_server = ToriRSChrome_Label(ui->chrome, self->panel, "");
    assert(self->label_server >= 0);
    ToriRSChrome_SetHidden(ui->chrome, self->label_hover, 1);
    ToriRSChrome_SetHidden(ui->chrome, self->label_status, 1);
    ToriRSChrome_SetHidden(ui->chrome, self->label_server, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "statusbar";
    /* Shown and hidden with the editor; never a View-menu row (design: it is
     * always on). */
    desc.title = NULL;
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = statusbar_tick;
    desc.activate = statusbar_activate;
    desc.set_visible = statusbar_set_visible;
    desc.destroy = statusbar_destroy;
    Editor_UiAddPanel(ui, &desc);
}
