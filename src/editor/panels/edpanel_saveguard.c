/*
 * The unsaved-edit guard (docs/MAPED_DESIGN.html #chrome: "MODAL save /
 * discard / cancel ... the missing piece for the unsaved-edit guard").
 *
 * A guarded action -- open a square in place of the current set, close the
 * editor -- asks Editor_UiGuarded first; with unsaved documents the registry
 * parks the action in `ui->guard` and this modal asks what to do with them:
 *
 *   Save        save every dirty document on the authority, then run it.
 *   Don't save  run it with the edits left unsaved. They are not lost: the
 *               authority keeps every open document and its history, so the
 *               edits stay undoable and a later Save still writes them.
 *   Cancel      drop the action (Escape and the close box are Cancel).
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

struct SaveGuardPanel
{
    int panel;
    int row_message;
    int row_detail;
    int button_save;
    int button_dont;
    int button_cancel;
    int shown;
};

static int
saveguard_unsaved_count(struct EditorUi* ui)
{
    struct Editor_Doc* doc;
    int count = 0;

    if( !ui->editor )
        return 0;
    doc = &ui->editor->doc;
    for( int i = 0; i < doc->square_count; i++ )
        if( doc->squares[i].loaded && (doc->squares[i].dirty_map || doc->squares[i].dirty_loc) )
            count++;
    for( int i = 0; i < doc->spawn_count; i++ )
        if( doc->spawns[i]->dirty )
            count++;
    for( int i = 0; i < doc->text_count; i++ )
        if( doc->texts[i]->dirty )
            count++;
    return count;
}

static void
saveguard_close(
    struct EditorUi* ui,
    struct SaveGuardPanel* self)
{
    self->shown = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    memset(&ui->guard, 0, sizeof(ui->guard));
}

static void
saveguard_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct SaveGuardPanel* self = opaque;

    if( self->shown && !ui->chrome->panels[self->panel].visible )
    {
        /* Escape or the close box: Cancel. */
        Editor_UiLog(ui, "editor: cancelled");
        saveguard_close(ui, self);
        return;
    }
    if( ui->guard.action == EDITOR_UI_GUARD_NONE || self->shown )
        return;

    {
        char line[TORIRS_CHROME_LABEL_MAX];
        int const unsaved = saveguard_unsaved_count(ui);

        snprintf(line, sizeof(line), "%d document%s with unsaved edits", unsaved,
            unsaved == 1 ? "" : "s");
        ToriRSChrome_SetText(ui->chrome, self->row_message, line);
        if( ui->guard.action == EDITOR_UI_GUARD_OPEN_SQUARE )
            snprintf(line, sizeof(line), "before opening m%d_%d", ui->guard.map_x, ui->guard.map_z);
        else if( ui->guard.action == EDITOR_UI_GUARD_CLOSE_SQUARE )
            snprintf(line, sizeof(line), "before closing m%d_%d", ui->guard.map_x, ui->guard.map_z);
        else
            snprintf(line, sizeof(line), "before closing the editor");
        ToriRSChrome_SetText(ui->chrome, self->row_detail, line);
    }
    self->shown = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
}

static int
saveguard_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct SaveGuardPanel* self = opaque;
    struct EditorUiGuard guard;

    if( widget != self->button_save && widget != self->button_dont && widget != self->button_cancel )
        return 0;
    guard = ui->guard;
    if( widget == self->button_cancel || guard.action == EDITOR_UI_GUARD_NONE )
    {
        saveguard_close(ui, self);
        return 1;
    }
    if( widget == self->button_save )
    {
        int const saved = ui->editor ? Editor_SaveAll(ui->editor) : -1;
        if( saved < 0 )
        {
            /* A read-only session cannot save; the modal stays up so the user
             * can choose Don't save or Cancel knowingly. */
            Editor_UiLog(ui, "editor: this session is read-only; nothing was saved");
            ToriRSChrome_SetText(ui->chrome, self->row_detail, "read-only: cannot save");
            return 1;
        }
        Editor_UiLog(ui, "editor: saved %d document(s)", saved);
    }
    else
        Editor_UiLog(ui, "editor: continuing; the edits stay unsaved (and undoable) in the session");
    saveguard_close(ui, self);
    Editor_UiRunGuarded(ui, &guard);
    return 1;
}

static void
saveguard_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_SaveGuard_Register(struct EditorUi* ui)
{
    struct SaveGuardPanel* self;
    struct EditorUiPanel desc;

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->panel = ToriRSChrome_ModalAdd(ui->chrome, "Unsaved changes", edp_px(ui, 240));
    assert(self->panel >= 0);
    self->row_message = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->row_detail = ToriRSChrome_Label(ui->chrome, self->panel, "");
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->button_save = ToriRSChrome_Button(ui->chrome, self->panel, "Save");
    self->button_dont = ToriRSChrome_Button(ui->chrome, self->panel, "Don't save");
    self->button_cancel = ToriRSChrome_Button(ui->chrome, self->panel, "Cancel");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_dont,
        "Continue; the edits stay in the session, unsaved and undoable");
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "save_guard";
    desc.title = NULL;
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = saveguard_tick;
    desc.activate = saveguard_activate;
    desc.destroy = saveguard_destroy;
    Editor_UiAddPanel(ui, &desc);
}
