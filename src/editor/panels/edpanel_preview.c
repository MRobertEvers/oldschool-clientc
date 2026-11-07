/*
 * The Preview panel (module "preview", View > Preview, Ctrl+Shift+P): the
 * model view of what the Catalog picked, a dock panel of its own beside the
 * Catalog rather than a small well at the bottom of it (owner, 2026-11-02).
 *
 *   Subject    one row naming what the well shows, by the follow rule
 *              (Editor_CatalogPreviewFollow, workbench 6.2): the Catalog's
 *              pick while placing; the primary selected loc
 *              (ui->tools.locs[0]) while selecting -- whatever was picked
 *              before (the old rule looked at the selection only until the
 *              first pick, then never again); with no selection the pick.
 *              A multiloc pick shows the rung the Catalog's variant dropdown
 *              (the Shell editor's readout) chose.
 *   Well       the MODELVIEW, filling the body: the panel's width, and every
 *              pixel of its height the other two rows leave when the layout or
 *              a resize gave the panel one. The App renders the subject into
 *              it at the well's own size (app_map_editor_preview_update), so a
 *              bigger panel is a bigger model. A click focuses it; while it
 *              has the focus WASD / arrows orbit and E / F zoom
 *              (app_loc_editor.c), and the editor's hotkeys stand aside
 *              (app_editor_hotkeys.c: a focused model view gates them).
 *              Gestures need no focus (workbench 6.1): a pan over the well
 *              orbits it (TORIRS_CHROME_EVENT_VIEW_ORBIT, the middle-drag's
 *              gearing: 4 yaw and 2 pitch a logical px, the way the orbit
 *              keys turn it) and a pinch zooms by its ratio
 *              (TORIRS_CHROME_EVENT_VIEW_ZOOM), through
 *              app_services.preview_orbit / preview_zoom.
 *   Reset view the camera back to the default pose and a fresh fit
 *              (EditorUiAppServices.preview_invalidate).
 *   Pose       the loc pick's shapes as chips ("0 wall", "2 wall L"; the
 *              current one lit, a click sets Place's shape) and the rotation
 *              row (0 1 2 3, and Fit, the walls-to-neighbours toggle): the
 *              Locs panel's Place pose, the same tools state, as chips
 *              (edpanel_chips.c). Then the pick's size, layer and ops.
 *   Actions    Place (arms the Place tool), the favourite star
 *              (editor/favourites.txt), Palette (the pick into the Catalog
 *              palette's first free slot), Record (the Record panel),
 *              Triggers, Find in world (every placement in the open squares
 *              selected), Copy to new (to the Catalog's New name). A refusal
 *              says why in the status bar.
 *
 * It follows the SHARED pick, so a popped-out Preview (its own process, solo
 * layout) shows what the main window's Catalog picks: the Catalog publishes
 * STATE_CATALOG_PICK and hears it back (edpanel_catalog.c catalog_on_state).
 *
 * Opening. When the Catalog opens, or a record is picked, while this panel is
 * closed, it opens against the Catalog's side (Editor_UiPanelOpenBeside: left
 * of it, top-aligned, a flyout like any panel a View toggle opens) -- unless
 * the user closed it in this session. At editor open it is part of the
 * default set right after the Catalog, so a column with room docks the two
 * one under the other; one without room hides both, and nothing opens until
 * the Catalog does. "Closed by the user" is this process's
 * memory, never the saved layout's: any close that was not the layout's own
 * (a relayout that had no room), the editor closing, Tab hiding every panel or
 * a pop-out counts.
 *
 * TORIRS_EDITOR_PREVIEW=<kind>:<name>[@<frame>][;<kind>:<name>@<frame>...]
 * (photographs; main window only, a popped-out window ignores it): at editor
 * frame <frame> (default 60) the Catalog picks record <name> of <kind> (loc,
 * npc, obj) as a row click would (Editor_Catalog_PickRecord).
 */

#include "edpanel_catalog_model.h"
#include "edpanel_chips.h"
#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanels.h"
#include "engine/cache_provider.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/** The panel's width at registration (logical px) and the well's natural
 *  and least heights: what the dock asks for and what it may shrink to. */
#define PREVIEW_W 240
#define PREVIEW_WELL_H 192
#define PREVIEW_WELL_MIN_H 64
/** TORIRS_EDITOR_PREVIEW entries. */
#define PREVIEW_ENV_MAX 4
/** A pan's orbit per logical px: the World's middle-drag gearing
 *  (APP_WORLD_MMB_YAW_PER_PX / _PITCH_PER_PX). */
#define PREVIEW_ORBIT_YAW_PER_PX 4
#define PREVIEW_ORBIT_PITCH_PER_PX 2

struct PreviewEnvPick
{
    uint32_t kind;
    char name[64];
    int frame;
};

struct PreviewPanel
{
    int panel;
    int row_subject;
    int view;
    int item_reset;
    /* The pose: the pick's shapes, the rotation row, the facts. */
    struct Edp_ChipStrip shape_strip;
    struct Edp_ChipStrip rotation_strip;
    int row_facts;
    /** The shapes the strip shows, in its order. */
    int shapes[EDITOR_LOC_SHAPE_COUNT];
    int shape_count;
    /** The pick's actions, a chip each (enum PreviewAction), their chords
     *  as keycaps: two lines of chips, not seven rows of buttons, so the
     *  pane stays the height a column gives it. */
    struct Edp_ChipStrip action_strip;

    /** The subject the row last named (kind, id, the pick's name). */
    int shown_kind;
    int shown_id;
    char shown_name[64];

    /* The auto-open rule. */
    int was_visible;
    int catalog_was_visible;
    int seen_pick_kind;
    int seen_pick_id;
    unsigned seen_layout_runs;
    int user_closed;
    /** How many times it opened itself (the tests read it). */
    int auto_opens;
    /** The part of an orbit step a pan has moved and no whole step has
     *  taken yet, 1/256 steps: a slow finger still turns the model. */
    int orbit_rem_yaw;
    int orbit_rem_pitch;

    /* TORIRS_EDITOR_PREVIEW */
    int env_read;
    int env_frames;
    int env_count;
    int env_next;
    struct PreviewEnvPick env[PREVIEW_ENV_MAX];
};

static struct PreviewPanel*
preview_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "preview");
}

int
Editor_Preview_Subject(
    struct EditorUi* ui,
    int* out_kind,
    int* out_id)
{
    assert(ui);
    assert(out_kind);
    assert(out_id);

    {
        struct Editor_CatalogPreviewInput input;
        memset(&input, 0, sizeof(input));
        input.placing = ui->tools.tool == EDITOR_TOOL_LOC_PLACE;
        input.pick_kind = ui->tools.pick_kind;
        input.pick_id = ui->tools.pick_id;
        input.rung_id = Editor_Catalog_PreviewRung(ui);
        input.selection_count = ui->tools.loc_count;
        input.selection_loc_id = ui->tools.loc_count > 0 ? ui->tools.locs[0].loc_id : -1;
        return Editor_CatalogPreviewFollow(&input, out_kind, out_id);
    }
}

int
Editor_Preview_ModelView(struct EditorUi* ui)
{
    struct PreviewPanel* self;

    assert(ui);
    self = preview_self(ui);
    if( !self || !ui->chrome->panels[self->panel].visible )
        return -1;
    return self->view;
}

int
Editor_Preview_AutoOpens(struct EditorUi* ui)
{
    struct PreviewPanel* self;

    assert(ui);
    self = preview_self(ui);
    return self ? self->auto_opens : 0;
}

/** The subject row, rewritten only when the subject changed. */
static void
preview_subject_row(
    struct EditorUi* ui,
    struct PreviewPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int kind;
    int id;
    int const has = Editor_Preview_Subject(ui, &kind, &id);
    int const selection = has && ui->tools.loc_count > 0 && kind == CACHEPROVIDER_CATALOG_LOC
                          && id == ui->tools.locs[0].loc_id
                          && !(ui->tools.tool == EDITOR_TOOL_LOC_PLACE && ui->tools.pick_id >= 0);
    int const picked = ui->tools.pick_kind >= 0 && ui->tools.pick_id >= 0 && !selection;

    if( kind == self->shown_kind && id == self->shown_id && strcmp(self->shown_name, ui->tools.pick_name) == 0 )
        return;
    self->shown_kind = kind;
    self->shown_id = id;
    snprintf(self->shown_name, sizeof(self->shown_name), "%s", ui->tools.pick_name);

    if( !has && picked )
        snprintf(line, sizeof(line), "%s %s: nothing at this value", ToriRSMapEdCatalog_KindName((uint32_t)kind),
            ui->tools.pick_name);
    else if( !has )
        snprintf(line, sizeof(line), "pick a loc, npc or obj in the Catalog");
    else if( picked && id != ui->tools.pick_id )
        snprintf(line, sizeof(line), "loc %d, a rung of %s", id, ui->tools.pick_name);
    else if( picked )
        snprintf(line, sizeof(line), "%s %s", ToriRSMapEdCatalog_KindName((uint32_t)kind), ui->tools.pick_name);
    else
    {
        const struct EditorUiAppServices* services = &ui->app_services;
        struct ToriRS_Location* cfg = services->loc_record ? services->loc_record(services->user_data, id) : NULL;
        if( cfg && cfg->name[0] )
            snprintf(line, sizeof(line), "selected loc %s (%d)", cfg->name, id);
        else
            snprintf(line, sizeof(line), "selected loc %d", id);
    }
    ToriRSChrome_SetText(ui->chrome, self->row_subject, line);
}

/**
 * The well fills the panel: its width is the content column's (as the last
 * Build laid it out), its height whatever the other rows leave of a panel
 * the layout or a resize gave a height, down to PREVIEW_WELL_MIN_H; a
 * content-sized panel gets the natural height. Converges in one Build: the
 * other rows' height does not depend on the well.
 */
static void
preview_size_well(
    struct EditorUi* ui,
    struct PreviewPanel* self)
{
    struct ToriRSChromePanel const* p = &ui->chrome->panels[self->panel];
    int box_w;
    int box_h;
    int well_w;
    int well_h;
    int want_w;
    int want_h = edp_px(ui, PREVIEW_WELL_H);

    if( !ToriRSChrome_ModelViewBox(ui->chrome, self->view, &box_w, &box_h, &well_w, &well_h) )
        return;
    want_w = ui->chrome->widgets[self->view].w > 0 ? ui->chrome->widgets[self->view].w : box_w;
    if( p->fixed_h > 0 && p->view_h > 0 && p->content_h > 0 )
    {
        want_h = p->view_h - (p->content_h - box_h);
        if( want_h < edp_px(ui, PREVIEW_WELL_MIN_H) )
            want_h = edp_px(ui, PREVIEW_WELL_MIN_H);
    }
    if( want_w != box_w || want_h != box_h )
        ToriRSChrome_ModelViewSetBox(ui->chrome, self->view, want_w, want_h);
}

/** EditorUiPanel::stretch: the well is the part that fills. */
static int
preview_stretch(
    struct EditorUi* ui,
    void* opaque,
    int* out_min_h,
    int* out_natural_h)
{
    struct PreviewPanel* self = opaque;
    int box_w = 0;
    int box_h = 0;
    int well_w = 0;
    int well_h = 0;

    assert(out_min_h);
    assert(out_natural_h);
    *out_min_h = edp_px(ui, PREVIEW_WELL_MIN_H);
    *out_natural_h = edp_px(ui, PREVIEW_WELL_H);
    (void)ToriRSChrome_ModelViewBox(ui->chrome, self->view, &box_w, &box_h, &well_w, &well_h);
    return box_h;
}

/* ---- the pose and the pick's actions --------------------------------------------- */

/** The action chips, in their order. */
enum PreviewAction
{
    PREVIEW_ACTION_PLACE = 0,
    PREVIEW_ACTION_STAR,
    PREVIEW_ACTION_PALETTE,
    PREVIEW_ACTION_RECORD,
    PREVIEW_ACTION_TRIGGERS,
    PREVIEW_ACTION_FIND,
    PREVIEW_ACTION_COPY,
};

/** The layer a loc of `shape` stands in (the world's four per tile). */
static const char*
preview_layer_name(int shape)
{
    if( shape >= 4 && shape <= 8 )
        return "wall decor layer";
    if( shape <= 3 || shape == 9 )
        return "wall layer";
    if( shape == 22 )
        return "ground decor layer";
    return "scenery layer";
}

/** `action`'s first chord as the table spells it ("" unbound). */
static void
preview_chord(
    const struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    char* out,
    int out_size)
{
    int const one = (int)action;
    Editor_HotkeysJoin(&ui->hotkeys, &one, 1, out, out_size);
}

/** The shape chips (a loc pick's shapes, the current one lit), the rotation
 *  row, the facts line and the star: restated each frame the panel shows,
 *  recorded only when they moved (the strips hash, the setters compare). */
static void
preview_pose_rows(
    struct EditorUi* ui,
    struct PreviewPanel* self)
{
    int const loc = ui->tools.pick_kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.pick_id >= 0;
    uint32_t const mask = loc ? Editor_Catalog_PickShapes(ui) : 0;
    char line[TORIRS_CHROME_LABEL_MAX];
    int star;

    self->shape_count = Editor_LocShapeList(mask, self->shapes);
    edp_chips_begin(&self->shape_strip);
    for( int i = 0; i < self->shape_count; i++ )
    {
        char label[EDP_CHIP_LABEL_MAX];
        char tip[EDP_CHIP_TIP_MAX];
        int const shape = self->shapes[i];
        snprintf(label, sizeof(label), "%d %s", shape, Editor_LocShapeName(shape));
        snprintf(tip, sizeof(tip), "Place at shape %d, %s (%s)", shape, Editor_LocShapeName(shape),
            Editor_CatalogShapeClassOf(shape));
        (void)edp_chips_push(&self->shape_strip, label, tip, shape == ui->tools.place_shape, 0);
    }
    edp_chips_end(ui, &self->shape_strip);
    ToriRSChrome_SetHidden(ui->chrome, self->shape_strip.graph, self->shape_count == 0);
    edp_chips_hover(ui, &self->shape_strip);

    edp_chips_begin(&self->rotation_strip);
    for( int r = 0; r < 4; r++ )
    {
        char label[8];
        char tip[EDP_CHIP_TIP_MAX];
        snprintf(label, sizeof(label), "%d", r);
        snprintf(tip, sizeof(tip), "Place turned %d quarter%s (Q turns it)", r, r == 1 ? "" : "s");
        (void)edp_chips_push(&self->rotation_strip, label, tip, (ui->tools.place_rotation & 3) == r, 0);
    }
    (void)edp_chips_push(&self->rotation_strip, "Fit", "fit walls to their neighbours (toggle)",
        edp_place_fit_enabled(ui), 0);
    edp_chips_end(ui, &self->rotation_strip);
    ToriRSChrome_SetHidden(ui->chrome, self->rotation_strip.graph, !loc);
    edp_chips_hover(ui, &self->rotation_strip);

    line[0] = '\0';
    if( loc )
    {
        const struct EditorUiAppServices* services = &ui->app_services;
        struct ToriRS_Location* cfg =
            services->loc_record ? services->loc_record(services->user_data, ui->tools.pick_id) : NULL;
        int const shape = ui->tools.place_shape >= 0 && ui->tools.place_shape < EDITOR_LOC_SHAPE_COUNT
                              ? ui->tools.place_shape
                              : 10;
        if( cfg )
        {
            int ops = 0;
            for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
                ops += cfg->actions[i][0] != '\0';
            snprintf(line, sizeof(line), "Size %dx%d, %s; %d op%s", cfg->size_x, cfg->size_z,
                preview_layer_name(shape), ops, ops == 1 ? "" : "s");
        }
        else
            snprintf(line, sizeof(line), "%s at shape %d", preview_layer_name(shape), shape);
    }
    ToriRSChrome_SetText(ui->chrome, self->row_facts, line);
    ToriRSChrome_SetHidden(ui->chrome, self->row_facts, !loc);

    /* The actions; the star lit while the pick is a favourite. */
    star = ui->tools.pick_kind >= 0 && ui->tools.pick_id >= 0 && Editor_Catalog_IsFavourite(ui);
    {
        char chord[EDP_CHIP_LABEL_MAX];
        int at;
        edp_chips_begin(&self->action_strip);
        at = edp_chips_push(&self->action_strip, "Place", "arm the Place tool with the pick", 0, 0);
        preview_chord(ui, EDITOR_HK_TOOL_PLACE, chord, sizeof(chord));
        edp_chips_set_chord(&self->action_strip, at, chord);
        (void)edp_chips_push(&self->action_strip, star ? "* favourite" : "* star",
            star ? "in favourites.txt; click takes it out" : "star the pick into editor/favourites.txt", star, 0);
        at = edp_chips_push(&self->action_strip, "Palette", "the pick into the Catalog palette", 0, 0);
        preview_chord(ui, EDITOR_HK_PALETTE_ADD_PICK, chord, sizeof(chord));
        edp_chips_set_chord(&self->action_strip, at, chord);
        (void)edp_chips_push(&self->action_strip, "Record", "every field of the pick, in the Record panel", 0, 0);
        at = edp_chips_push(&self->action_strip, "Triggers", "the pick's script triggers", 0, 0);
        preview_chord(ui, EDITOR_HK_VIEW_TRIGGERS, chord, sizeof(chord));
        edp_chips_set_chord(&self->action_strip, at, chord);
        (void)edp_chips_push(&self->action_strip, "Find in world", "select every placement in the open squares", 0, 0);
        (void)edp_chips_push(&self->action_strip, "Copy to new...", "copy the pick to the Catalog's New name", 0, 0);
        edp_chips_end(ui, &self->action_strip);
        edp_chips_hover(ui, &self->action_strip);
    }
}

/** One action chip pressed. A refusal says why (the callee's status). */
static void
preview_action(
    struct EditorUi* ui,
    int action)
{
    int const picked = ui->tools.pick_kind >= 0 && ui->tools.pick_id >= 0;

    switch( action )
    {
    case PREVIEW_ACTION_PLACE:
        if( !picked )
            Editor_UiStatus(ui, "Place refused: pick a loc, npc or obj in the Catalog first");
        else
            Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
        break;
    case PREVIEW_ACTION_STAR:
        (void)Editor_Catalog_ToggleFavourite(ui);
        break;
    case PREVIEW_ACTION_PALETTE:
        if( !picked )
            Editor_UiStatus(ui, "Palette refused: pick a loc, npc or obj first");
        else
            (void)Editor_CatalogPalette_AddPick(ui, 0);
        break;
    case PREVIEW_ACTION_RECORD:
        (void)Editor_Catalog_EditPick(ui);
        break;
    case PREVIEW_ACTION_TRIGGERS:
        if( !Editor_UiPanelSelf(ui, "triggers") )
            Editor_UiStatus(ui, "Triggers refused: this window has no Triggers panel");
        else if( Editor_Catalog_EditPick(ui) )
            Editor_Triggers_Follow(ui);
        break;
    case PREVIEW_ACTION_FIND:
        (void)Editor_Catalog_FindInWorld(ui);
        break;
    case PREVIEW_ACTION_COPY:
        (void)Editor_Catalog_CopyPick(ui);
        break;
    default:
        break;
    }
}

/** A press on a shape chip sets Place's shape; on a rotation chip its
 *  rotation; on Fit the toggle. */
static void
preview_pose_press(
    struct EditorUi* ui,
    struct PreviewPanel* self,
    const struct ToriRSChromeEvent* event)
{
    int on_x = 0;

    if( event->widget == self->action_strip.graph )
    {
        int const index = edp_chips_key_of(&self->action_strip, event->key, &on_x);
        if( index >= 0 )
            preview_action(ui, index);
        return;
    }
    if( event->widget == self->shape_strip.graph )
    {
        int const index = edp_chips_key_of(&self->shape_strip, event->key, &on_x);
        if( index >= 0 && index < self->shape_count )
        {
            ui->tools.place_shape = self->shapes[index];
            Editor_UiStatus(ui, "Place shape %d %s", self->shapes[index], Editor_LocShapeName(self->shapes[index]));
        }
        return;
    }
    {
        int const index = edp_chips_key_of(&self->rotation_strip, event->key, &on_x);
        if( index >= 0 && index < 4 )
            ui->tools.place_rotation = index;
        else if( index == 4 )
        {
            edp_place_fit_set(ui, !edp_place_fit_enabled(ui));
            Editor_UiStatus(ui, "fit walls to neighbours %s", edp_place_fit_enabled(ui) ? "on" : "off");
        }
    }
}

/* ---- opening beside the Catalog -------------------------------------------- */

static void
preview_follow_catalog(
    struct EditorUi* ui,
    struct PreviewPanel* self)
{
    struct EditorUiPanel* catalog = Editor_UiFindPanel(ui, "catalog");
    int const visible = ui->chrome->panels[self->panel].visible;
    int const catalog_visible = catalog && catalog->chrome_panel >= 0
                                && ui->chrome->panels[catalog->chrome_panel].visible;
    int const picked = ui->tools.pick_kind != self->seen_pick_kind || ui->tools.pick_id != self->seen_pick_id;
    int const opened = catalog_visible && !self->catalog_was_visible;
    /* Hidden by something that is not the user: the editor closing, Tab,
     * this window being a popped-out one, the panel popping out, or a
     * relayout this tick (a surface with no room for it). */
    int const others = !ui->visible || ui->panels_hidden || ui->solo_panel >= 0
                       || Editor_UiPanelPoppedOut(ui, "preview") || self->seen_layout_runs != ui->layout_runs;

    if( self->was_visible && !visible && !others )
        self->user_closed = 1;
    if( !visible && !self->user_closed && catalog_visible && (opened || (picked && ui->tools.pick_id >= 0))
        && ui->visible && !ui->panels_hidden && ui->solo_panel < 0 && !Editor_UiPanelPoppedOut(ui, "preview") )
    {
        /* Shown as View > Preview shows it, and put against the Catalog's
         * side rather than wherever the flyouts end. */
        Editor_UiPanelOpenBeside(ui, "preview", "catalog");
        self->auto_opens++;
    }
    self->was_visible = ui->chrome->panels[self->panel].visible;
    self->catalog_was_visible = catalog_visible;
    self->seen_pick_kind = ui->tools.pick_kind;
    self->seen_pick_id = ui->tools.pick_id;
    self->seen_layout_runs = ui->layout_runs;
}

/* ---- TORIRS_EDITOR_PREVIEW ---------------------------------------------------- */

static void
preview_env_read(struct PreviewPanel* self)
{
    const char* value = getenv("TORIRS_EDITOR_PREVIEW");
    char spec[256];
    char* next;

    self->env_read = 1;
    if( !value || !value[0] )
        return;
    snprintf(spec, sizeof(spec), "%s", value);
    for( char* entry = strtok_r(spec, ";", &next); entry && self->env_count < PREVIEW_ENV_MAX;
         entry = strtok_r(NULL, ";", &next) )
    {
        struct PreviewEnvPick* pick = &self->env[self->env_count];
        char kind[8] = { 0 };
        char* at = strchr(entry, '@');

        pick->frame = at ? atoi(at + 1) : 60;
        if( at )
            *at = '\0';
        if( sscanf(entry, "%7[a-z]:%63s", kind, pick->name) != 2 )
            continue;
        if( strcmp(kind, "loc") == 0 )
            pick->kind = TORIRSMAPED_CATALOG_LOC;
        else if( strcmp(kind, "npc") == 0 )
            pick->kind = TORIRSMAPED_CATALOG_NPC;
        else if( strcmp(kind, "obj") == 0 )
            pick->kind = TORIRSMAPED_CATALOG_OBJ;
        else
            continue;
        self->env_count++;
    }
}

static void
preview_env_tick(
    struct EditorUi* ui,
    struct PreviewPanel* self)
{
    if( !self->env_read )
        preview_env_read(self);
    if( self->env_next >= self->env_count || ui->solo_panel >= 0 )
        return;
    self->env_frames++;
    while( self->env_next < self->env_count && self->env_frames >= self->env[self->env_next].frame )
    {
        struct PreviewEnvPick const* pick = &self->env[self->env_next++];
        Editor_UiLog(ui, "editor: TORIRS_EDITOR_PREVIEW picks %s %s", ToriRSMapEdCatalog_KindName(pick->kind),
            pick->name);
        (void)Editor_Catalog_PickRecord(ui, pick->kind, pick->name);
    }
}

/* ---- the module ---------------------------------------------------------------- */

static void
preview_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct PreviewPanel* self = opaque;

    preview_env_tick(ui, self);
    preview_follow_catalog(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    preview_subject_row(ui, self);
    preview_pose_rows(ui, self);
    preview_size_well(ui, self);
}

static int
preview_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct PreviewPanel* self = opaque;

    if( widget == self->item_reset )
    {
        if( ui->app_services.preview_invalidate )
            ui->app_services.preview_invalidate(ui->app_services.user_data);
        return 1;
    }
    /* A click on the well focuses it (the orbit keys); nothing else to do. */
    return widget == self->view;
}

/** Whole steps out of a q8 accumulator, the rest kept. */
static int
preview_steps(int* rem_q8)
{
    int const steps = *rem_q8 / 256;
    *rem_q8 -= steps * 256;
    return steps;
}

static int
preview_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct PreviewPanel* self = opaque;

    if( event->widget == self->shape_strip.graph || event->widget == self->rotation_strip.graph
        || event->widget == self->action_strip.graph )
    {
        if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS )
            preview_pose_press(ui, self, event);
        return 1;
    }
    if( event->widget != self->view )
        return 0;
    if( event->kind == TORIRS_CHROME_EVENT_VIEW_ORBIT )
    {
        int yaw;
        int pitch;

        /* The fingers' way, as the keys: right is the right arrow's yaw,
         * down the down arrow's pitch. */
        self->orbit_rem_yaw += event->dx * PREVIEW_ORBIT_YAW_PER_PX;
        self->orbit_rem_pitch += event->dy * PREVIEW_ORBIT_PITCH_PER_PX;
        /* A twist: its turn in yaw steps (2048 a turn), 1/256 of one, with
         * the World's sign (EditorWorldGesture_TwistYaw): a clockwise twist
         * is a positive step. */
        if( event->value != 0 )
            self->orbit_rem_yaw +=
                (int)lround((double)event->value / 65536.0 * 2048.0 / (2.0 * 3.14159265358979323846) * 256.0);
        yaw = preview_steps(&self->orbit_rem_yaw);
        pitch = preview_steps(&self->orbit_rem_pitch);
        if( (yaw != 0 || pitch != 0) && ui->app_services.preview_orbit )
            ui->app_services.preview_orbit(ui->app_services.user_data, pitch, yaw);
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_VIEW_ZOOM )
    {
        if( event->value > 0 && ui->app_services.preview_zoom )
            ui->app_services.preview_zoom(ui->app_services.user_data, event->value);
        return 1;
    }
    return 0;
}

static void
preview_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Preview_Register(struct EditorUi* ui)
{
    struct PreviewPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int const well_w = edp_px(ui, PREVIEW_W - 24);

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->shown_kind = -2;
    self->shown_id = -2;
    self->seen_pick_kind = ui->tools.pick_kind;
    self->seen_pick_id = ui->tools.pick_id;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 420), edp_px(ui, PREVIEW_W), "Preview");
    assert(self->panel >= 0);
    self->row_subject = ToriRSChrome_Label(chrome, self->panel, "pick a loc, npc or obj in the Catalog");
    /* The reference well's aspect (120 x 96) at the panel's width; the tick
     * fits it to the panel from the first Build on. */
    self->view = ToriRSChrome_ModelView(chrome, self->panel, well_w, well_w * 4 / 5);
    assert(self->view >= 0);
    ToriRSChrome_SetTooltip(chrome, self->view, "click: WASD orbit, E/F zoom; or drag to orbit, pinch to zoom");
    self->item_reset = ToriRSChrome_MenuItem(chrome, self->panel, "Reset view");
    /* The pose: the Locs panel's Place pose as chips. */
    edp_chips_init(ui, &self->shape_strip, self->panel, 2);
    edp_chips_init(ui, &self->rotation_strip, self->panel, 1);
    self->row_facts = ToriRSChrome_Label(chrome, self->panel, "");
    /* The pick's actions. */
    edp_chips_init(ui, &self->action_strip, self->panel, 2);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    /* The pose and action rows under the well: a dock that gives the panel
     * less than all of them scrolls it rather than growing it. */
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "preview";
    desc.title = "Preview";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = preview_tick;
    desc.activate = preview_activate;
    desc.event = preview_event;
    desc.destroy = preview_destroy;
    desc.stretch = preview_stretch;
    Editor_UiAddPanel(ui, &desc);
}
