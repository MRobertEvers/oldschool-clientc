/*
 * The New square modal (docs/MAPED_DESIGN.html #squares). Asks, before
 * anything is written:
 *
 *   where it lives  the coordinates (prefilled from the picker's empty cell,
 *                   or typed) and the storage: the content tree's maps/, an
 *                   existing lane (every ported/<lane>/maps the atlas lists,
 *                   empty lanes included), or a NEW lane named here.
 *   what it starts  empty, a copy of the picker's selection, or a flat fill
 *                   at an authored height and underlay.
 *
 * Every keystroke re-checks the form against the atlas (edpanel_newform.c):
 * a square taken in that storage is refused with its owner named, and Create
 * stays disabled until the form is one the server will accept. Create sends
 * DOC_CREATE through Editor_CreateSquare; nothing reaches disk until Save,
 * like every other document. The created square opens in this window and
 * its atlas row appears in every window when the server broadcasts it.
 *
 * Copy to... and Save as new are this modal with the template fixed to a copy
 * of `source`: Copy to asks coordinates and storage, Save as new the storage
 * question alone.
 */

#include "edpanel_common.h"
#include "edpanel_newform.h"
#include "edpanel_picker.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWSQUARE_STORAGE_ROWS 34

enum
{
    NEWSQUARE_GROUP_TEMPLATE = 21,
};

static const char* const k_template_names[3] = { "Empty", "Copy of selection", "Flat fill" };

struct NewSquarePanel
{
    int panel;
    int row_intro;
    int input_x;
    int input_z;
    int dropdown_storage;
    int input_lane;
    int row_template;
    int radio_template[3];
    int row_source;
    int slider_height;
    int slider_underlay;
    int row_check;
    int button_create;
    int button_cancel;

    enum Editor_NewSquareMode mode;
    char source_key[EDITOR_DOC_KEY_MAX];

    /** Storage rows: label, and the picker storage index (-1 = new lane). */
    char storage_labels[NEWSQUARE_STORAGE_ROWS][TORIRS_CHROME_LABEL_MAX];
    const char* storage_rows[NEWSQUARE_STORAGE_ROWS];
    int storage_index[NEWSQUARE_STORAGE_ROWS];
    int storage_count;

    int shown;
    /** The form the last check ran on, so a quiet frame checks nothing. */
    struct Editor_NewSquareForm checked;
    int checked_valid;
    int ok;
};

static struct NewSquarePanel*
newsquare_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "new_square");
}

static int
newsquare_int(
    struct EditorUi* ui,
    int widget,
    int fallback)
{
    const char* text = ToriRSChrome_Text(ui->chrome, widget);
    char* end = NULL;
    long value;

    if( !text || !text[0] )
        return fallback;
    value = strtol(text, &end, 10);
    if( !end || *end != '\0' || value < -1 || value > 100000 )
        return -2;
    return (int)value;
}

/** The form, as the widgets say it now. */
static void
newsquare_read(
    struct EditorUi* ui,
    struct NewSquarePanel* self,
    struct Editor_NewSquareForm* form)
{
    int row;

    memset(form, 0, sizeof(*form));
    form->mode = self->mode;
    form->map_x = newsquare_int(ui, self->input_x, -1);
    form->map_z = newsquare_int(ui, self->input_z, -1);
    row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_storage);
    form->storage = row >= 0 && row < self->storage_count ? self->storage_index[row] : -2;
    if( form->storage == -2 )
        form->storage = 1 << 20; /* nothing chosen: out of range, refused */
    snprintf(form->new_lane, sizeof(form->new_lane), "%s", ToriRSChrome_Text(ui->chrome, self->input_lane));
    form->template_kind = EDITOR_DOC_TEMPLATE_EMPTY;
    for( int i = 0; i < 3; i++ )
        if( ToriRSChrome_Checked(ui->chrome, self->radio_template[i]) )
            form->template_kind = (enum Editor_DocTemplate)i;
    snprintf(form->source_key, sizeof(form->source_key), "%s", self->source_key);
    form->fill_height = ToriRSChrome_SliderValue(ui->chrome, self->slider_height);
    form->fill_underlay = ToriRSChrome_SliderValue(ui->chrome, self->slider_underlay);
}

static void
newsquare_layout(
    struct EditorUi* ui,
    struct NewSquarePanel* self)
{
    int const copy_mode = self->mode != EDITOR_NEW_SQUARE_NEW;
    int const save_as = self->mode == EDITOR_NEW_SQUARE_SAVE_AS;
    int template_kind = EDITOR_DOC_TEMPLATE_EMPTY;
    int row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_storage);
    int const new_lane = row >= 0 && row < self->storage_count && self->storage_index[row] < 0;

    for( int i = 0; i < 3; i++ )
        if( ToriRSChrome_Checked(ui->chrome, self->radio_template[i]) )
            template_kind = i;

    ToriRSChrome_SetHidden(ui->chrome, self->input_x, save_as);
    ToriRSChrome_SetHidden(ui->chrome, self->input_z, save_as);
    ToriRSChrome_SetHidden(ui->chrome, self->input_lane, !new_lane);
    ToriRSChrome_SetHidden(ui->chrome, self->row_template, copy_mode);
    for( int i = 0; i < 3; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_template[i], copy_mode);
    ToriRSChrome_SetHidden(
        ui->chrome, self->row_source, !(copy_mode || template_kind == EDITOR_DOC_TEMPLATE_COPY));
    ToriRSChrome_SetHidden(
        ui->chrome, self->slider_height, copy_mode || template_kind != EDITOR_DOC_TEMPLATE_FLAT);
    ToriRSChrome_SetHidden(
        ui->chrome, self->slider_underlay, copy_mode || template_kind != EDITOR_DOC_TEMPLATE_FLAT);
}

static void
newsquare_check(
    struct EditorUi* ui,
    struct NewSquarePanel* self,
    int force)
{
    struct Editor_NewSquareForm form;
    struct Editor_NewSquareCheck check;
    const struct Editor_Picker* picker;

    newsquare_read(ui, self, &form);
    if( !force && self->checked_valid && memcmp(&form, &self->checked, sizeof(form)) == 0 )
        return;
    self->checked = form;
    self->checked_valid = 1;
    newsquare_layout(ui, self);

    picker = edp_squares_picker(ui);
    if( !picker )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_check, "no square atlas: is the server reachable?");
        ToriRSChrome_SetDisabled(ui->chrome, self->button_create, 1);
        self->ok = 0;
        return;
    }
    self->ok = Editor_NewSquareValidate(picker, &form, &check, NULL);
    ToriRSChrome_SetText(ui->chrome, self->row_check, check.message);
    ToriRSChrome_SetColor(ui->chrome, self->row_check, self->ok ? 0x9fdf8f : 0xff7f5f);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !self->ok);
}

static void
newsquare_fill_storages(
    struct EditorUi* ui,
    struct NewSquarePanel* self,
    int prefer)
{
    const struct Editor_Picker* picker = edp_squares_picker(ui);
    int selected = 0;
    int n = 0;

    if( picker )
    {
        for( int i = 0; i < picker->storage_count && n < NEWSQUARE_STORAGE_ROWS - 1; i++ )
        {
            const char* s = picker->storages[i];
            if( strcmp(s, "maps") == 0 || s[0] == '\0' )
                snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "maps/ (content tree)");
            else
                snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "%s/", s);
            self->storage_rows[n] = self->storage_labels[n];
            self->storage_index[n] = i;
            if( i == prefer )
                selected = n;
            n++;
        }
    }
    snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "New lane...");
    self->storage_rows[n] = self->storage_labels[n];
    self->storage_index[n] = -1;
    n++;
    self->storage_count = n;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_storage, self->storage_rows, n, selected);
}

void
Editor_NewSquare_Open(
    struct EditorUi* ui,
    enum Editor_NewSquareMode mode,
    const char* source_key,
    int map_x,
    int map_z)
{
    struct NewSquarePanel* self;
    const struct Editor_Picker* picker;
    char text[32];
    char line[TORIRS_CHROME_LABEL_MAX];
    int prefer = 0;

    assert(ui);
    assert(mode == EDITOR_NEW_SQUARE_NEW || source_key);

    self = newsquare_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: no New square dialog in this window");
        return;
    }
    self->mode = mode;
    snprintf(self->source_key, sizeof(self->source_key), "%s", source_key ? source_key : "");

    picker = edp_squares_picker(ui);
    /* Prefill: New takes the empty cell it was given, Copy to asks afresh,
     * Save as new keeps the source's own coordinates and defaults to a
     * storage the source is not in. */
    if( mode == EDITOR_NEW_SQUARE_COPY_TO )
        map_x = map_z = -1;
    else if( mode == EDITOR_NEW_SQUARE_SAVE_AS )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        map_x = map_z = -1;
        if( Editor_DocSplitSquareKey(source_key, storage, bare) )
        {
            (void)Editor_SquareKeyParse(bare, &map_x, &map_z);
            if( picker )
            {
                int const source_storage = Editor_PickerStorageIndex(picker, storage);
                for( int i = 0; i < picker->storage_count; i++ )
                    if( i != source_storage )
                    {
                        prefer = i;
                        break;
                    }
            }
        }
    }
    else if( picker )
        prefer = Editor_PickerStorageIndex(picker, "maps") >= 0 ? Editor_PickerStorageIndex(picker, "maps") : 0;

    if( map_x >= 0 )
        snprintf(text, sizeof(text), "%d", map_x);
    else
        text[0] = '\0';
    ToriRSChrome_SetText(ui->chrome, self->input_x, text);
    if( map_z >= 0 )
        snprintf(text, sizeof(text), "%d", map_z);
    else
        text[0] = '\0';
    ToriRSChrome_SetText(ui->chrome, self->input_z, text);
    ToriRSChrome_SetText(ui->chrome, self->input_lane, "");
    newsquare_fill_storages(ui, self, prefer);

    if( mode == EDITOR_NEW_SQUARE_NEW )
    {
        ToriRSChrome_PanelSetTitle(ui->chrome, self->panel, "New square");
        ToriRSChrome_SetText(ui->chrome, self->row_intro, "Where it lives, and what it starts as");
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_template[0]);
    }
    else if( mode == EDITOR_NEW_SQUARE_COPY_TO )
    {
        ToriRSChrome_PanelSetTitle(ui->chrome, self->panel, "Copy square to");
        ToriRSChrome_SetText(ui->chrome, self->row_intro, "New coordinates and storage for the copy");
    }
    else
    {
        ToriRSChrome_PanelSetTitle(ui->chrome, self->panel, "Save square as new");
        ToriRSChrome_SetText(ui->chrome, self->row_intro, "Where the copy is stored");
    }
    if( self->source_key[0] )
        snprintf(line, sizeof(line), "copies %s", self->source_key);
    else
        snprintf(line, sizeof(line), "select a square in the picker to copy it");
    ToriRSChrome_SetText(ui->chrome, self->row_source, line);
    ToriRSChrome_SliderSet(ui->chrome, self->slider_underlay, ui->tools.underlay_id);

    self->shown = 1;
    self->checked_valid = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    newsquare_check(ui, self, 1);
}

static void
newsquare_close(
    struct EditorUi* ui,
    struct NewSquarePanel* self)
{
    self->shown = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
newsquare_create(
    struct EditorUi* ui,
    struct NewSquarePanel* self)
{
    struct Editor_NewSquareForm form;
    struct Editor_NewSquareCheck check;
    struct EditorHost_DocCreate request;
    const struct Editor_Picker* picker = edp_squares_picker(ui);
    enum EditorHost_Status status;

    newsquare_read(ui, self, &form);
    if( !picker || !Editor_NewSquareValidate(picker, &form, &check, &request) )
    {
        newsquare_check(ui, self, 1);
        return;
    }
    if( !ui->editor )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_check, "no editor session");
        return;
    }
    status = Editor_CreateSquare(ui->editor, &request);
    if( status == EDITOR_HOST_REFUSED )
    {
        ToriRSChrome_SetText(
            ui->chrome, self->row_check, "refused: it exists there, or is open from another storage");
        ToriRSChrome_SetColor(ui->chrome, self->row_check, 0xff7f5f);
        Editor_UiLog(ui, "editor: DOC_CREATE %s refused", request.key);
        return;
    }
    if( status != EDITOR_HOST_OK )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_check, "the server did not create it");
        Editor_UiLog(ui, "editor: DOC_CREATE %s failed (status %d)", request.key, (int)status);
        return;
    }
    Editor_UiLog(ui, "editor: %s (unsaved until Save)", check.message);
    newsquare_close(ui, self);
    Editor_SquarePicker_Refresh(ui);
    if( ui->hooks.open_square )
        ui->hooks.open_square(ui->hooks_user, form.map_x, form.map_z, request.storage, 1);
}

static void
newsquare_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct NewSquarePanel* self = opaque;

    if( !self->shown )
        return;
    if( !ui->chrome->panels[self->panel].visible )
    {
        self->shown = 0; /* Escape or the close box */
        return;
    }
    newsquare_check(ui, self, 0);
}

static int
newsquare_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct NewSquarePanel* self = opaque;

    if( widget == self->button_create )
    {
        newsquare_create(ui, self);
        return 1;
    }
    if( widget == self->button_cancel )
    {
        newsquare_close(ui, self);
        return 1;
    }
    if( widget == self->input_x || widget == self->input_z || widget == self->input_lane
        || widget == self->dropdown_storage || widget == self->slider_height
        || widget == self->slider_underlay || widget == self->radio_template[0]
        || widget == self->radio_template[1] || widget == self->radio_template[2] )
    {
        newsquare_check(ui, self, 1);
        return 1;
    }
    return 0;
}

static void
newsquare_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_NewSquare_Register(struct EditorUi* ui)
{
    struct NewSquarePanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->panel = ToriRSChrome_ModalAdd(chrome, "New square", edp_px(ui, 300));
    assert(self->panel >= 0);
    self->row_intro = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_x = ToriRSChrome_TextInput(chrome, self->panel, "Map x", "");
    self->input_z = ToriRSChrome_TextInput(chrome, self->panel, "Map z", "");
    self->dropdown_storage = ToriRSChrome_Dropdown(chrome, self->panel, "Stored in", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_storage,
        "maps/ is what the cache pack reads; a lane ships behind its lane descriptor");
    self->input_lane = ToriRSChrome_TextInput(chrome, self->panel, "Lane", "");
    ToriRSChrome_SetTooltip(chrome, self->input_lane, "The new lane's directory: ported/<lane>/maps");
    self->row_template = ToriRSChrome_Label(chrome, self->panel, "Starts as");
    for( int i = 0; i < 3; i++ )
        self->radio_template[i] =
            ToriRSChrome_Radio(chrome, self->panel, k_template_names[i], NEWSQUARE_GROUP_TEMPLATE, i == 0);
    self->row_source = ToriRSChrome_Label(chrome, self->panel, "");
    self->slider_height = ToriRSChrome_Slider(chrome, self->panel, "Height h", 1, 255, 1, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_height, "The h token every level-0 tile gets (1 = ground)");
    self->slider_underlay = ToriRSChrome_Slider(chrome, self->panel, "Underlay", 0, 255, 0, 1);
    self->row_check = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_create = ToriRSChrome_Button(chrome, self->panel, "Create");
    self->button_cancel = ToriRSChrome_Button(chrome, self->panel, "Cancel");
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "new_square";
    desc.title = NULL;
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = newsquare_tick;
    desc.activate = newsquare_activate;
    desc.destroy = newsquare_destroy;
    Editor_UiAddPanel(ui, &desc);
}
