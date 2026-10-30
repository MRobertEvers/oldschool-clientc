/*
 * The output console: the registry's log ring (Editor_UiLog) as a VLIST, the
 * newest line at the bottom. Every module reports here -- refusals, saves,
 * what a stroke did -- so the user sees why a click did nothing without a
 * terminal. The chrome holds only the rows on screen; the ring is the
 * registry's.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

/* A short strip along the bottom of the world (Editor_UiLayout). */
#define CONSOLE_ROWS 5

struct ConsolePanel
{
    int panel;
    int list;
    int item_clear;
    int shown_serial;
    int shown_count;
    /** The console serial at the last Clear: only lines after it list (the
     *  ring keeps the rest). */
    int cleared_serial;
    int follow;
};

static int
console_visible_count(
    const struct EditorUi* ui,
    const struct ConsolePanel* self)
{
    int const since = ui->console_serial - self->cleared_serial;
    int const count = since < ui->console_count ? since : ui->console_count;
    return count > 0 ? count : 0;
}

static void
console_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct ConsolePanel* self = opaque;
    int const count = console_visible_count(ui, self);
    int first;
    int shown;

    if( self->list < 0 )
        return;
    if( self->shown_serial != ui->console_serial )
    {
        ToriRSChrome_VListSetCount(ui->chrome, self->list, count);
        /* Follow the tail unless the user scrolled up to read. */
        if( self->follow || self->shown_count == 0 )
            ToriRSChrome_VListSetScroll(ui->chrome, self->list, count - CONSOLE_ROWS);
        self->shown_serial = ui->console_serial;
        self->shown_count = count;
    }
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    self->follow = first + shown >= count;
    for( int row = first; row < first + shown; row++ )
    {
        /* Row 0 is the oldest still listed. */
        const char* line = Editor_UiConsoleLine(ui, count - 1 - row);
        ToriRSChrome_VListSetRow(ui->chrome, self->list, row, line ? line : "");
    }
}

static int
console_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct ConsolePanel* self = opaque;

    if( widget == self->list )
        return 1;
    if( widget == self->item_clear )
    {
        self->cleared_serial = ui->console_serial;
        self->shown_serial = -1;
        return 1;
    }
    return 0;
}

static void
console_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Console_Register(struct EditorUi* ui)
{
    struct ConsolePanel* self;
    struct EditorUiPanel desc;
    int const surface_h = ui->chrome->surface_h > 0 ? ui->chrome->surface_h : edp_px(ui, 480);

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->follow = 1;
    self->shown_serial = -1;
    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X),
        surface_h - edp_px(ui, 200), edp_px(ui, 420), "Output");
    assert(self->panel >= 0);
    self->list = ToriRSChrome_VList(ui->chrome, self->panel, "", CONSOLE_ROWS, 0);
    self->item_clear = ToriRSChrome_MenuItem(ui->chrome, self->panel, "Clear");
    ToriRSChrome_PanelSetResizable(ui->chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "console";
    desc.title = "Output";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = console_tick;
    desc.activate = console_activate;
    desc.destroy = console_destroy;
    Editor_UiAddPanel(ui, &desc);
}
