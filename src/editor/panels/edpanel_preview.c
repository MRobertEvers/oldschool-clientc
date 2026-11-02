/*
 * The Preview panel (module "preview", View > Preview, Ctrl+Shift+P): the
 * model view of what the Catalog picked, a dock panel of its own beside the
 * Catalog rather than a small well at the bottom of it (owner, 2026-11-02).
 *
 *   Subject    one row naming what the well shows: the Catalog's pick (a loc,
 *              npc or obj; for a multiloc, the rung the Catalog's variant
 *              dropdown chose -- Editor_Catalog_PreviewId), else, while nothing
 *              has been picked, the primary selected loc (ui->tools.locs[0]).
 *   Well       the MODELVIEW, filling the body: the panel's width, and every
 *              pixel of its height the other two rows leave when the layout or
 *              a resize gave the panel one. The App renders the subject into
 *              it at the well's own size (app_map_editor_preview_update), so a
 *              bigger panel is a bigger model. A click focuses it; while it
 *              has the focus WASD / arrows orbit and E / F zoom
 *              (app_loc_editor.c), and the editor's hotkeys stand aside
 *              (app_editor_hotkeys.c: a focused model view gates them).
 *   Reset view the camera back to the default pose and a fresh fit
 *              (EditorUiAppServices.preview_invalidate).
 *   Add to palette  the pick into the Catalog palette's first free slot
 *              (edpanel_catalog_palette.c).
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

#include "edpanel_common.h"
#include "edpanels.h"
#include "engine/cache_provider.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The panel's width at registration (logical px) and the well's natural
 *  and least heights: what the dock asks for and what it may shrink to. */
#define PREVIEW_W 240
#define PREVIEW_WELL_H 192
#define PREVIEW_WELL_MIN_H 64
/** TORIRS_EDITOR_PREVIEW entries. */
#define PREVIEW_ENV_MAX 4

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
    /** The pick into the Catalog palette's first free slot. */
    int item_add_palette;

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

    *out_kind = -1;
    *out_id = -1;
    if( ui->tools.pick_kind >= 0 && ui->tools.pick_id >= 0 )
    {
        *out_kind = ui->tools.pick_kind;
        *out_id = Editor_Catalog_PreviewId(ui);
        return *out_id >= 0;
    }
    if( ui->tools.loc_count > 0 && ui->tools.locs[0].loc_id >= 0 )
    {
        *out_kind = CACHEPROVIDER_CATALOG_LOC;
        *out_id = ui->tools.locs[0].loc_id;
        return 1;
    }
    return 0;
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
    int const picked = ui->tools.pick_kind >= 0 && ui->tools.pick_id >= 0;

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
    preview_size_well(ui, self);
}

static int
preview_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct PreviewPanel* self = opaque;

    if( widget == self->item_add_palette )
    {
        (void)Editor_CatalogPalette_AddPick(ui, 0);
        return 1;
    }
    if( widget == self->item_reset )
    {
        if( ui->app_services.preview_invalidate )
            ui->app_services.preview_invalidate(ui->app_services.user_data);
        return 1;
    }
    /* A click on the well focuses it (the orbit keys); nothing else to do. */
    return widget == self->view;
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
    ToriRSChrome_SetTooltip(chrome, self->view, "click, then WASD / arrows orbit, E / F zoom");
    self->item_reset = ToriRSChrome_MenuItem(chrome, self->panel, "Reset view");
    self->item_add_palette = ToriRSChrome_MenuItem(chrome, self->panel, "Add to palette");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "preview";
    desc.title = "Preview";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = preview_tick;
    desc.activate = preview_activate;
    desc.destroy = preview_destroy;
    desc.stretch = preview_stretch;
    Editor_UiAddPanel(ui, &desc);
}
