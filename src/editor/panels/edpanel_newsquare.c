/*
 * The New square modal (docs/MAPED_DESIGN.html #squares, docs/MAPED_STATUS.md
 * "Scratch squares").
 *
 *   New          asks only what the square starts as -- empty, a copy of the
 *                picker's selection, or a flat fill at an authored height and
 *                underlay -- and makes it a SCRATCH square: DOC_CREATE in
 *                EDITOR_DOC_SCRATCH_STORAGE on the first free scratch slot,
 *                opened at once as the working set. Where it lives is asked
 *                when it is saved: Save places it on the Square map
 *                (Editor_SquarePicker_Place), where the cell is picked by eye
 *                instead of typed. A scratch square with something in it
 *                that Save has not placed yet is offered again here (Open
 *                unplaced scratch). The copy row names its source ("Copy of
 *                m50_50"; with nothing selected it says so and cannot be
 *                chosen) and brings the source's spawns with "with its
 *                spawns" (on): they stand on the scratch square, and its
 *                Place carries them on (edp_square_create). Flat fill's
 *                underlay shows its colour beside the number.
 *   Save as new  the open square again at its own coordinates, in another
 *                storage: the storage question alone.
 *   Copy to...   forwards to the Square map's Place with that square.
 *
 * Every keystroke re-checks the form (edpanel_newform.c for Save as new);
 * Create stays disabled until it is one the server will accept. Nothing
 * reaches disk until Save, like every other document.
 */

#include "edpanel_common.h"
#include "edpanel_newform.h"
#include "edpanel_picker.h"
#include "edpanel_picker_nav.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWSQUARE_STORAGE_ROWS 34
/* Flat fill's underlay when the palette has none (id 0 draws no ground): the
 * `.jm2` id of the grass most of Lumbridge stands on. A scratch square opens
 * alone, so a fill nothing draws is a black void with nothing to grab. */
#define NEWSQUARE_FLAT_UNDERLAY_DEFAULT 48
/* The underlay's colour beside its slider (1x px): the block, and the well. */
#define NEWSQUARE_SWATCH_W 36
#define NEWSQUARE_SWATCH_H 14
#define NEWSQUARE_SWATCH_ROW_H 18

enum
{
    NEWSQUARE_GROUP_TEMPLATE = 21,
};

static const char* const k_template_names[3] = { "Empty", "Copy of selection", "Flat fill" };

struct NewSquarePanel
{
    int panel;
    int row_intro;
    int dropdown_storage;
    int input_lane;
    int row_template;
    int radio_template[3];
    int row_source;
    /** New's copy: the source's spawns come too. */
    int check_spawns;
    int slider_height;
    int slider_underlay;
    /** The underlay's colour, as the Palette's swatch shows it. */
    int graph_underlay;
    int swatch_id;
    int swatch_known;
    uint32_t swatch_rgb;
    int swatch_asked;
    /** The template chosen last: what a refused "Copy of" falls back to. */
    int template_last;
    int row_check;
    int button_create;
    int button_scratch;
    int button_cancel;

    enum Editor_NewSquareMode mode;
    char source_key[EDITOR_DOC_KEY_MAX];
    /** The scratch square New offers to reopen ("" none). */
    char unplaced_key[EDITOR_DOC_KEY_MAX];

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

int
Editor_NewSquare_UnplacedScratch(
    struct EditorUi* ui,
    char out[EDITOR_DOC_KEY_MAX])
{
    struct Editor_Doc* doc;

    assert(ui);
    assert(out);
    out[0] = '\0';
    if( !ui->editor )
        return 0;
    doc = &ui->editor->doc;
    for( int i = 0; i < doc->square_count; i++ )
    {
        struct Editor_Square const* square = &doc->squares[i];
        if( !square->loaded || !Editor_DocStorageIsScratch(doc->square_storage[i])
            || Editor_SquareIsBlank(square) )
            continue;
        snprintf(out, EDITOR_DOC_KEY_MAX, "%s/m%d_%d", EDITOR_DOC_SCRATCH_STORAGE, square->map_x, square->map_z);
        return 1;
    }
    return 0;
}

/** The form, as the widgets say it now. Save as new's coordinates are its
 *  source's; New's are the scratch slot's, chosen at Create. */
static void
newsquare_read(
    struct EditorUi* ui,
    struct NewSquarePanel* self,
    struct Editor_NewSquareForm* form)
{
    int row;

    memset(form, 0, sizeof(*form));
    form->mode = self->mode;
    form->map_x = -1;
    form->map_z = -1;
    if( self->mode == EDITOR_NEW_SQUARE_SAVE_AS )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        if( Editor_DocSplitSquareKey(self->source_key, storage, bare) )
            (void)Editor_SquareKeyParse(bare, &form->map_x, &form->map_z);
    }
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
    int const save_as = self->mode == EDITOR_NEW_SQUARE_SAVE_AS;
    int template_kind = EDITOR_DOC_TEMPLATE_EMPTY;
    int row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_storage);
    int const new_lane = row >= 0 && row < self->storage_count && self->storage_index[row] < 0;

    for( int i = 0; i < 3; i++ )
        if( ToriRSChrome_Checked(ui->chrome, self->radio_template[i]) )
            template_kind = i;

    ToriRSChrome_SetHidden(ui->chrome, self->dropdown_storage, !save_as);
    ToriRSChrome_SetHidden(ui->chrome, self->input_lane, !save_as || !new_lane);
    ToriRSChrome_SetHidden(ui->chrome, self->row_template, save_as);
    for( int i = 0; i < 3; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_template[i], save_as);
    ToriRSChrome_SetHidden(
        ui->chrome, self->row_source, !(save_as || template_kind == EDITOR_DOC_TEMPLATE_COPY));
    ToriRSChrome_SetHidden(ui->chrome, self->check_spawns, save_as || template_kind != EDITOR_DOC_TEMPLATE_COPY);
    ToriRSChrome_SetHidden(
        ui->chrome, self->slider_height, save_as || template_kind != EDITOR_DOC_TEMPLATE_FLAT);
    ToriRSChrome_SetHidden(
        ui->chrome, self->slider_underlay, save_as || template_kind != EDITOR_DOC_TEMPLATE_FLAT);
    /* No colours without the window's floor records (a headless harness). */
    ToriRSChrome_SetHidden(ui->chrome, self->graph_underlay,
        save_as || template_kind != EDITOR_DOC_TEMPLATE_FLAT || !ui->hooks.floor_rgb);
    ToriRSChrome_SetHidden(ui->chrome, self->button_scratch, save_as || !self->unplaced_key[0]);
}

/** New's check: the template alone (the slot is chosen at Create, the place
 *  at Save). Returns 1 when Create may go. */
static int
newsquare_check_scratch(
    const struct Editor_NewSquareForm* form,
    char* message,
    size_t message_size)
{
    if( form->template_kind == EDITOR_DOC_TEMPLATE_COPY && !form->source_key[0] )
    {
        snprintf(message, message_size, "no square is selected: click one on the Square map, then New");
        return 0;
    }
    if( form->template_kind == EDITOR_DOC_TEMPLATE_FLAT
        && (form->fill_height < 0 || form->fill_height > 255 || form->fill_underlay < 0
            || form->fill_underlay > 255) )
    {
        snprintf(message, message_size, "flat fill: height 0..255, underlay 0..255");
        return 0;
    }
    snprintf(message, message_size, "it opens to edit; Save then places it");
    return 1;
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

    if( self->mode == EDITOR_NEW_SQUARE_NEW )
    {
        self->ok = newsquare_check_scratch(&form, check.message, sizeof(check.message));
    }
    else
    {
        picker = edp_squares_picker(ui);
        if( !picker )
        {
            ToriRSChrome_SetText(ui->chrome, self->row_check, "no square atlas: is the server reachable?");
            ToriRSChrome_SetDisabled(ui->chrome, self->button_create, 1);
            self->ok = 0;
            return;
        }
        self->ok = Editor_NewSquareValidate(picker, &form, &check, NULL);
    }
    ToriRSChrome_SetText(ui->chrome, self->row_check, check.message);
    ToriRSChrome_SetColor(ui->chrome, self->row_check, self->ok ? 0x9fdf8f : 0xff7f5f);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !self->ok);
}

/** The underlay slider's colour: the record's own (hooks.floor_rgb, what the
 *  Palette's swatches show), hatched grey while the record is not resident
 *  (asked for once an id), crossed out for 0, which draws no ground. Redrawn
 *  only when the id or its answer moves. */
static void
newsquare_swatch(
    struct EditorUi* ui,
    struct NewSquarePanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const id = ToriRSChrome_SliderValue(chrome, self->slider_underlay);
    uint32_t rgb = 0;
    int known = 0;
    char text[TORIRS_CHROME_LABEL_MAX];

    if( !ui->hooks.floor_rgb )
        return;
    if( id > 0 )
    {
        known = ui->hooks.floor_rgb(ui->hooks_user, 0, id, &rgb) ? 1 : 0;
        if( !known && ui->hooks.floor_request && self->swatch_asked != id )
        {
            ui->hooks.floor_request(ui->hooks_user, 0, id);
            self->swatch_asked = id;
        }
    }
    if( id == self->swatch_id && known == self->swatch_known && rgb == self->swatch_rgb )
        return;
    self->swatch_id = id;
    self->swatch_known = known;
    self->swatch_rgb = rgb;

    ToriRSChrome_GraphBegin(chrome, self->graph_underlay);
    if( id == 0 )
    {
        ToriRSChrome_GraphRect(chrome, self->graph_underlay, 0, 2, NEWSQUARE_SWATCH_W, NEWSQUARE_SWATCH_H, 0x201c16, 1,
            0);
        ToriRSChrome_GraphLine(chrome, self->graph_underlay, 0, 2 + NEWSQUARE_SWATCH_H - 1, NEWSQUARE_SWATCH_W - 1, 2,
            0xb04030);
        snprintf(text, sizeof(text), "underlay 0: none, the ground draws black");
    }
    else if( known )
    {
        ToriRSChrome_GraphRect(chrome, self->graph_underlay, 0, 2, NEWSQUARE_SWATCH_W, NEWSQUARE_SWATCH_H, rgb, 1, 0);
        snprintf(text, sizeof(text), "underlay %d", id);
    }
    else
    {
        ToriRSChrome_GraphRect(chrome, self->graph_underlay, 0, 2, NEWSQUARE_SWATCH_W, NEWSQUARE_SWATCH_H, 0x5a564c, 1,
            0);
        ToriRSChrome_GraphLine(chrome, self->graph_underlay, 0, 2, NEWSQUARE_SWATCH_W - 1, 2 + NEWSQUARE_SWATCH_H - 1,
            0x3a362c);
        snprintf(text, sizeof(text), "underlay %d: no colour (not loaded, or no such floor)", id);
    }
    ToriRSChrome_GraphText(chrome, self->graph_underlay, NEWSQUARE_SWATCH_W + 6, NEWSQUARE_SWATCH_ROW_H - 5,
        chrome->theme.text, text);
    ToriRSChrome_GraphEnd(chrome, self->graph_underlay);
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
            if( Editor_DocStorageIsScratch(s) )
                continue;
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
    const char* source_key)
{
    struct NewSquarePanel* self;
    const struct Editor_Picker* picker;
    char line[TORIRS_CHROME_LABEL_MAX];
    int prefer = 0;

    assert(ui);
    assert(mode == EDITOR_NEW_SQUARE_NEW || source_key);

    if( mode == EDITOR_NEW_SQUARE_COPY_TO )
    {
        Editor_SquarePicker_Place(ui, source_key, 0);
        return;
    }
    self = newsquare_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: no New square dialog in this window");
        return;
    }
    self->mode = mode;
    snprintf(self->source_key, sizeof(self->source_key), "%s", source_key ? source_key : "");
    (void)Editor_NewSquare_UnplacedScratch(ui, self->unplaced_key);

    picker = edp_squares_picker(ui);
    /* Save as new defaults to a storage the source is not in. */
    if( mode == EDITOR_NEW_SQUARE_SAVE_AS && picker )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        if( Editor_DocSplitSquareKey(source_key, storage, bare) )
        {
            int const source_storage = Editor_PickerStorageIndex(picker, storage);
            for( int i = 0; i < picker->storage_count; i++ )
                if( i != source_storage && !Editor_DocStorageIsScratch(picker->storages[i]) )
                {
                    prefer = i;
                    break;
                }
        }
    }
    ToriRSChrome_SetText(ui->chrome, self->input_lane, "");
    newsquare_fill_storages(ui, self, prefer);

    if( mode == EDITOR_NEW_SQUARE_NEW )
    {
        ToriRSChrome_PanelSetTitle(ui->chrome, self->panel, "New square");
        ToriRSChrome_SetText(ui->chrome, self->row_intro, "A scratch square: what it starts as");
        /* Flat fill: an Empty square draws nothing until it is painted. */
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_template[EDITOR_DOC_TEMPLATE_FLAT]);
        ToriRSChrome_SetText(ui->chrome, self->button_create, "Create");
        if( self->unplaced_key[0] )
        {
            snprintf(line, sizeof(line), "Open unplaced %s", self->unplaced_key);
            ToriRSChrome_SetText(ui->chrome, self->button_scratch, line);
        }
    }
    else
    {
        ToriRSChrome_PanelSetTitle(ui->chrome, self->panel, "Save square as new");
        ToriRSChrome_SetText(ui->chrome, self->row_intro, "Where the copy is stored");
        ToriRSChrome_SetText(ui->chrome, self->button_create, "Create");
    }
    if( self->source_key[0] )
        snprintf(line, sizeof(line), "copies %s", self->source_key);
    else
        snprintf(line, sizeof(line), "select a square in the picker to copy it");
    ToriRSChrome_SetText(ui->chrome, self->row_source, line);
    /* The copy row says WHICH square, or that there is none to copy (N2,
     * 2026-11-09): "Copy of selection" named nothing, and with nothing
     * selected it could be chosen and then only refused at Create. */
    if( self->source_key[0] )
    {
        char chip[EDITOR_PICKER_NAME_MAX];
        Editor_PickerChipText(self->source_key, chip);
        snprintf(line, sizeof(line), "Copy of %.40s", chip);
        ToriRSChrome_SetTooltip(ui->chrome, self->radio_template[EDITOR_DOC_TEMPLATE_COPY],
            "Its terrain and locs, and its spawns with the box below ticked");
    }
    else
    {
        snprintf(line, sizeof(line), "Copy of selection (no square selected)");
        ToriRSChrome_SetTooltip(ui->chrome, self->radio_template[EDITOR_DOC_TEMPLATE_COPY],
            "Open a square, or click one on the Square map, to copy it");
    }
    ToriRSChrome_SetLabel(ui->chrome, self->radio_template[EDITOR_DOC_TEMPLATE_COPY], line);
    ToriRSChrome_SetDisabled(ui->chrome, self->radio_template[EDITOR_DOC_TEMPLATE_COPY],
        mode == EDITOR_NEW_SQUARE_NEW && !self->source_key[0]);
    self->template_last = EDITOR_DOC_TEMPLATE_FLAT;
    ToriRSChrome_SliderSet(ui->chrome, self->slider_underlay,
        ui->tools.underlay_id > 0 ? ui->tools.underlay_id : NEWSQUARE_FLAT_UNDERLAY_DEFAULT);
    self->swatch_id = -1;
    newsquare_swatch(ui, self);

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

/** Documents a save would write: the unsaved count less the scratch
 *  squares holding work, which a save never writes (they are placed). */
static int
newsquare_saveable_unsaved(struct EditorUi* ui)
{
    struct Editor_Doc const* doc = &ui->editor->doc;
    int count = Editor_DocUnsavedCount(doc);

    for( int i = 0; i < doc->square_count; i++ )
        if( Editor_DocStorageIsScratch(doc->square_storage[i])
            && (doc->squares[i].dirty_map || doc->squares[i].dirty_loc) && !Editor_SquareIsBlank(&doc->squares[i]) )
            count--;
    return count;
}

/** Open scratch square (map_x, map_z) as the working set, past the save
 *  guard when a save has anything to write (what is unsaved stays in the
 *  session either way); a scratch square's own work is no reason to ask. */
static void
newsquare_open_scratch(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    const struct Editor_Picker* picker = edp_squares_picker(ui);
    struct EditorUiGuard guard;

    memset(&guard, 0, sizeof(guard));
    guard.action = EDITOR_UI_GUARD_OPEN_SQUARE;
    guard.map_x = map_x;
    guard.map_z = map_z;
    snprintf(guard.storage, sizeof(guard.storage), "%s", EDITOR_DOC_SCRATCH_STORAGE);
    guard.storage_index = picker ? Editor_PickerStorageIndex(picker, EDITOR_DOC_SCRATCH_STORAGE) : -1;
    if( newsquare_saveable_unsaved(ui) > 0 )
        (void)Editor_UiGuarded(ui, &guard);
    else
        Editor_UiRunGuarded(ui, &guard);
}

/** New's Create: the first scratch slot the authority takes (a free one, or
 *  a blank scratch square a Place left), then open it. */
static void
newsquare_create_scratch(
    struct EditorUi* ui,
    struct NewSquarePanel* self,
    const struct Editor_NewSquareForm* form)
{
    struct EditorHost_DocCreate request;
    int const with_spawns = ToriRSChrome_Checked(ui->chrome, self->check_spawns);

    memset(&request, 0, sizeof(request));
    request.kind = EDITOR_DOC_SQUARE;
    snprintf(request.storage, sizeof(request.storage), "%s", EDITOR_DOC_SCRATCH_STORAGE);
    request.template_kind = form->template_kind;
    if( form->template_kind == EDITOR_DOC_TEMPLATE_COPY )
        snprintf(request.source_key, sizeof(request.source_key), "%s", form->source_key);
    request.fill_height = form->fill_height;
    request.fill_underlay = form->fill_underlay;

    for( int slot = 0; slot < EDITOR_DOC_SCRATCH_SLOTS; slot++ )
    {
        int map_x;
        int map_z;
        struct Editor_Square* held;
        enum EditorHost_Status status;
        int spawns = 0;

        Editor_DocScratchSlot(slot, &map_x, &map_z);
        /* This mirror's view first: a slot holding work is not asked for. */
        held = Editor_DocFindSquare(&ui->editor->doc, map_x, map_z);
        if( held && held->loaded && !Editor_SquareIsBlank(held) )
            continue;
        Editor_SquareKey(request.key, map_x, map_z);
        /* A copy brings the source's spawns (the box), in the create's step. */
        status = edp_square_create(ui, &request, with_spawns, &spawns);
        if( status == EDITOR_HOST_REFUSED )
            continue; /* another window's scratch work holds it */
        if( status != EDITOR_HOST_OK )
        {
            ToriRSChrome_SetText(ui->chrome, self->row_check, "the server did not create it");
            Editor_UiLog(ui, "editor: DOC_CREATE scratch/%s failed (status %d)", request.key, (int)status);
            return;
        }
        if( form->template_kind != EDITOR_DOC_TEMPLATE_COPY )
            Editor_UiStatus(ui, "New scratch square: edit it, then Save places it on the Square map");
        else if( with_spawns )
            Editor_UiStatus(ui, "New scratch square, a copy of %s with its %d spawn%s: Save places it", form->source_key,
                spawns, spawns == 1 ? "" : "s");
        else
            Editor_UiStatus(ui, "New scratch square, a copy of %s; its spawns stay behind (the box is off)",
                form->source_key);
        Editor_UiLog(ui, "editor: scratch square %s: edit it, then Save places it", request.key);
        /* Underlay 0 paints void: on an Empty square the first stroke would
         * change nothing. */
        if( form->template_kind == EDITOR_DOC_TEMPLATE_EMPTY && ui->tools.underlay_id == 0 )
            ui->tools.underlay_id = NEWSQUARE_FLAT_UNDERLAY_DEFAULT;
        newsquare_close(ui, self);
        Editor_SquarePicker_Refresh(ui);
        newsquare_open_scratch(ui, map_x, map_z);
        return;
    }
    ToriRSChrome_SetText(ui->chrome, self->row_check, "every scratch slot holds work: Save to place one");
    ToriRSChrome_SetColor(ui->chrome, self->row_check, 0xff7f5f);
    Editor_UiLog(ui, "editor: all %d scratch slots hold unplaced work", EDITOR_DOC_SCRATCH_SLOTS);
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
    if( !ui->editor )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_check, "no editor session");
        return;
    }
    if( self->mode == EDITOR_NEW_SQUARE_NEW )
    {
        if( !newsquare_check_scratch(&form, check.message, sizeof(check.message)) )
        {
            newsquare_check(ui, self, 1);
            return;
        }
        newsquare_create_scratch(ui, self, &form);
        return;
    }
    if( !picker || !Editor_NewSquareValidate(picker, &form, &check, &request) )
    {
        newsquare_check(ui, self, 1);
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
    newsquare_swatch(ui, self);
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
    if( widget == self->button_scratch )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        int map_x;
        int map_z;
        if( self->unplaced_key[0] && Editor_DocSplitSquareKey(self->unplaced_key, storage, bare)
            && Editor_SquareKeyParse(bare, &map_x, &map_z) )
        {
            newsquare_close(ui, self);
            newsquare_open_scratch(ui, map_x, map_z);
        }
        return 1;
    }
    if( widget == self->button_cancel )
    {
        newsquare_close(ui, self);
        return 1;
    }
    if( widget == self->radio_template[EDITOR_DOC_TEMPLATE_COPY] && self->mode == EDITOR_NEW_SQUARE_NEW
        && !self->source_key[0] )
    {
        /* Not a choice with nothing to copy: back to what was chosen, and
         * the reason where the form's verdict is read. */
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_template[self->template_last]);
        newsquare_check(ui, self, 1);
        ToriRSChrome_SetText(ui->chrome, self->row_check,
            "nothing to copy: click a square on the Square map, then New");
        ToriRSChrome_SetColor(ui->chrome, self->row_check, 0xff7f5f);
        return 1;
    }
    if( widget == self->input_lane || widget == self->dropdown_storage || widget == self->slider_height
        || widget == self->slider_underlay || widget == self->radio_template[0]
        || widget == self->radio_template[1] || widget == self->radio_template[2] || widget == self->check_spawns )
    {
        for( int i = 0; i < 3; i++ )
            if( widget == self->radio_template[i] )
                self->template_last = i;
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
    self->dropdown_storage = ToriRSChrome_Dropdown(chrome, self->panel, "Stored in", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_storage,
        "maps/ is what the cache pack reads; a lane ships with its lane");
    self->input_lane = ToriRSChrome_TextInput(chrome, self->panel, "Lane", "");
    ToriRSChrome_SetTooltip(chrome, self->input_lane, "The new lane's directory: ported/<lane>/maps");
    self->row_template = ToriRSChrome_Label(chrome, self->panel, "Starts as");
    for( int i = 0; i < 3; i++ )
        self->radio_template[i] =
            ToriRSChrome_Radio(chrome, self->panel, k_template_names[i], NEWSQUARE_GROUP_TEMPLATE, i == 0);
    self->row_source = ToriRSChrome_Label(chrome, self->panel, "");
    self->check_spawns = ToriRSChrome_Checkbox(chrome, self->panel, "with its spawns", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_spawns,
        "The copy brings the source square's npcs and ground items");
    self->slider_height = ToriRSChrome_Slider(chrome, self->panel, "Height h", 0, 255, 1, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_height, "Each ground tile's height: 0 is rolling noise, 1 is flat");
    self->slider_underlay = ToriRSChrome_Slider(chrome, self->panel, "Underlay", 0, 255, 0, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_underlay, "The ground's floor id; its colour shows below");
    self->graph_underlay = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, NEWSQUARE_SWATCH_ROW_H));
    self->swatch_id = -1;
    self->template_last = EDITOR_DOC_TEMPLATE_FLAT;
    self->row_check = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_create = ToriRSChrome_Button(chrome, self->panel, "Create");
    ToriRSChrome_SetTooltip(chrome, self->button_create,
        "Opens it as the working set; Save picks its place");
    self->button_scratch = ToriRSChrome_Button(chrome, self->panel, "Open unplaced scratch");
    ToriRSChrome_SetTooltip(chrome, self->button_scratch, "A scratch square with work in it that is not placed yet");
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
