/*
 * The loc / npc / obj catalog (App-bound): what the provider has decoded,
 * filtered by a typed substring, with the picked entry's model in a host-fed
 * well and, for a multiloc, its transform table as pickable rungs.
 *
 * Ported from the old editor_panel.c catalog unchanged in behaviour. The pick
 * is shared tool state (tools.pick_kind / pick_id / pick_name): the Place
 * tool stamps it, the spawns panel resolves an npc / obj pick to its pack
 * symbol, the locs panel swaps to it. The server-side catalog (design
 * #catalog's panel over CATALOG_QUERY) is a later panel; this one needs no
 * server binding, which is what keeps it working on a half-broken tree.
 */

#include "app.h"
#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

#include <stdio.h>
#include <string.h>

#define CATALOG_MAX 512
#define CATALOG_LABEL_MAX 48
#define CATALOG_VARIANT_MAX 33

static const char* const k_kind_names[] = { "Locs", "NPCs", "Objs" };
#define CATALOG_KIND_COUNT ((int)(sizeof(k_kind_names) / sizeof(k_kind_names[0])))

struct CatalogRows
{
    int name;
    int desc;
    int cfg;
    int model;
    int render;
    int ops;
};

struct CatalogPanel
{
    int panel;
    int dropdown_kind;
    int input_search;
    int dropdown_list;
    int row_picked;
    int row_multi;
    int dropdown_variant;
    int view;
    int item_reset;
    int row_count;
    struct CatalogRows var_rows;

    int kind;
    char shown_search[CATALOG_LABEL_MAX];
    int shown_kind;
    int built_epoch;

    char labels[CATALOG_MAX][CATALOG_LABEL_MAX];
    const char* options[CATALOG_MAX];
    int ids[CATALOG_MAX];
    int count;

    char var_labels[CATALOG_VARIANT_MAX][CATALOG_LABEL_MAX];
    const char* var_options[CATALOG_VARIANT_MAX];
    int var_ids[CATALOG_VARIANT_MAX];
    int var_count;
    int var_choice;
    int var_shown_id;
    int var_shown_kind;
    int var_shown_epoch;
};

static struct App*
catalog_app(struct EditorUi* ui)
{
    assert(ui->app);
    return ui->app;
}

static char
catalog_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int
catalog_matches(
    const char* name,
    const char* filter)
{
    if( !filter || !filter[0] )
        return 1;
    if( !name || !name[0] )
        return 0;
    for( int i = 0; name[i]; i++ )
    {
        int j = 0;
        while( filter[j] && catalog_lower(name[i + j]) == catalog_lower(filter[j]) )
            j++;
        if( !filter[j] )
            return 1;
        if( !name[i + j] )
            break;
    }
    return 0;
}

struct CatalogFill
{
    struct CatalogPanel* self;
    const char* filter;
};

static void
catalog_count_only(
    void* user,
    int id,
    const char* name)
{
    (void)user;
    (void)id;
    (void)name;
}

static void
catalog_visit(
    void* user,
    int id,
    const char* name)
{
    struct CatalogFill* fill = user;
    struct CatalogPanel* self = fill->self;
    int const named = name && name[0];

    if( self->count >= CATALOG_MAX )
        return;
    /* An unnamed record is still placeable, but filtering by text excludes it. */
    if( named ? !catalog_matches(name, fill->filter) : (fill->filter && fill->filter[0]) )
        return;
    snprintf(self->labels[self->count], CATALOG_LABEL_MAX, "%s (%d)", named ? name : "<unnamed>", id);
    self->options[self->count] = self->labels[self->count];
    self->ids[self->count] = id;
    self->count++;
}

static void
catalog_rebuild(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct App* app = catalog_app(ui);
    struct CatalogFill fill;
    const char* search = ToriRSChrome_Text(ui->chrome, self->input_search);

    self->count = 0;
    fill.self = self;
    fill.filter = search;
    if( app->provider )
        CacheProvider_VisitLoaded(
            app->provider, (enum CacheProvider_CatalogKind)self->kind, catalog_visit, &fill);
    snprintf(self->shown_search, sizeof(self->shown_search), "%s", search ? search : "");
    self->shown_kind = self->kind;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_list, self->options, self->count, -1);
}

static void
catalog_rows_hidden(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    int hidden)
{
    ToriRSChrome_SetHidden(ui->chrome, self->row_multi, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->dropdown_variant, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.name, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.desc, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.cfg, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.model, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.render, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.ops, hidden);
}

static void
catalog_rows_fill(
    struct EditorUi* ui,
    const struct CatalogRows* rows,
    const struct ToriRS_Location* cfg,
    int loc_id)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    snprintf(line, sizeof(line), "%s  (loc %d)", cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    ToriRSChrome_SetText(ui->chrome, rows->name, line);
    ToriRSChrome_SetText(ui->chrome, rows->desc, cfg->desc[0] ? cfg->desc : "(no examine)");
    snprintf(line, sizeof(line), "size %dx%d  walk %s  proj %s  approach %d", cfg->size_x, cfg->size_z,
        cfg->blocks_walk ? "block" : "-", cfg->blocks_projectiles ? "block" : "-", cfg->force_approach);
    ToriRSChrome_SetText(ui->chrome, rows->cfg, line);
    snprintf(line, sizeof(line), "models %d  seq %d  wallw %d", cfg->shapes_and_model_count, cfg->seq_id,
        cfg->wall_width);
    ToriRSChrome_SetText(ui->chrome, rows->model, line);
    snprintf(line, sizeof(line), "amb %d con %d%s%s%s%s", cfg->ambient, cfg->contrast,
        cfg->sharelight ? "  sharelight" : "", cfg->occlude ? "  occlude" : "",
        cfg->shadowed ? "  shadowed" : "", cfg->contoured_ground ? "  contoured" : "");
    ToriRSChrome_SetText(ui->chrome, rows->render, line);
    line[0] = '\0';
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        if( cfg->actions[i][0] )
        {
            size_t const at = strlen(line);
            snprintf(line + at, sizeof(line) - at, "%s%s", at ? " / " : "", cfg->actions[i]);
        }
    ToriRSChrome_SetText(ui->chrome, rows->ops, line[0] ? line : "(no ops)");
}

static void
catalog_rows_blank(
    struct EditorUi* ui,
    const struct CatalogRows* rows,
    const char* why)
{
    ToriRSChrome_SetText(ui->chrome, rows->name, why);
    ToriRSChrome_SetText(ui->chrome, rows->desc, "");
    ToriRSChrome_SetText(ui->chrome, rows->cfg, "");
    ToriRSChrome_SetText(ui->chrome, rows->model, "");
    ToriRSChrome_SetText(ui->chrome, rows->render, "");
    ToriRSChrome_SetText(ui->chrome, rows->ops, "");
}

static int
catalog_variant_id(const struct CatalogPanel* self)
{
    if( self->var_choice <= 0 || self->var_choice >= self->var_count )
        return -1;
    return self->var_ids[self->var_choice];
}

static void
catalog_variant_readout(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct App* app = catalog_app(ui);
    int const loc_id = self->var_choice > 0 ? catalog_variant_id(self) : ui->tools.pick_id;
    struct ToriRS_Location* cfg;

    if( loc_id < 0 )
    {
        catalog_rows_blank(ui, &self->var_rows, "(nothing at this value)");
        return;
    }
    cfg = app->provider ? CacheProvider_LocationGet(app->provider, loc_id) : NULL;
    if( cfg )
        catalog_rows_fill(ui, &self->var_rows, cfg, loc_id);
    else
        catalog_rows_blank(ui, &self->var_rows, "(config not resident)");
}

static void
catalog_variant_rebuild(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct App* app = catalog_app(ui);
    struct ToriRS_Location* cfg = NULL;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( self->var_shown_id != ui->tools.pick_id || self->var_shown_kind != self->kind )
        self->var_choice = 0;
    self->var_shown_id = ui->tools.pick_id;
    self->var_shown_kind = self->kind;
    self->var_shown_epoch = self->built_epoch;
    self->var_count = 0;

    if( app->provider && self->kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.pick_id >= 0 )
        cfg = CacheProvider_LocationGet(app->provider, ui->tools.pick_id);
    if( !cfg || cfg->transform_count <= 0 || !cfg->transforms )
    {
        self->var_choice = 0;
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_variant, NULL, 0, -1);
        catalog_rows_hidden(ui, self, 1);
        return;
    }

    snprintf(self->var_labels[0], CATALOG_LABEL_MAX, "shell: %s (%d)", cfg->name[0] ? cfg->name : "<unnamed>",
        ui->tools.pick_id);
    self->var_options[0] = self->var_labels[0];
    self->var_ids[0] = -1;
    self->var_count = 1;
    for( int i = 0; i < cfg->transform_count && self->var_count < CATALOG_VARIANT_MAX; i++ )
    {
        int const rung = cfg->transforms[i];
        int const row = self->var_count;
        int const fallback = i == cfg->transform_count - 1;
        struct ToriRS_Location* rung_cfg = rung > 0 ? CacheProvider_LocationGet(app->provider, rung) : NULL;
        char key[8];

        if( fallback )
            snprintf(key, sizeof(key), "else");
        else
            snprintf(key, sizeof(key), "%d", i);
        if( rung <= 0 )
            snprintf(self->var_labels[row], CATALOG_LABEL_MAX, "%s: none", key);
        else if( rung_cfg )
            snprintf(self->var_labels[row], CATALOG_LABEL_MAX, "%s: %s (%d)", key,
                rung_cfg->name[0] ? rung_cfg->name : "<unnamed>", rung);
        else
            snprintf(self->var_labels[row], CATALOG_LABEL_MAX, "%s: loc %d (loading)", key, rung);
        self->var_options[row] = self->var_labels[row];
        self->var_ids[row] = rung > 0 ? rung : -1;
        self->var_count++;
        if( rung > 0 && !rung_cfg )
        {
            struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, rung);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
    }
    if( self->var_choice >= self->var_count )
        self->var_choice = 0;
    if( cfg->transform_varbit != -1 )
        snprintf(line, sizeof(line), "multiloc: varbit %d, %d variants", cfg->transform_varbit,
            cfg->transform_count);
    else if( cfg->transform_varp != -1 )
        snprintf(line, sizeof(line), "multiloc: varp %d, %d variants", cfg->transform_varp, cfg->transform_count);
    else
        snprintf(line, sizeof(line), "multiloc: no selector, %d variants", cfg->transform_count);
    ToriRSChrome_SetText(ui->chrome, self->row_multi, line);
    ToriRSChrome_DropdownSetOptions(
        ui->chrome, self->dropdown_variant, self->var_options, self->var_count, self->var_choice);
    catalog_rows_hidden(ui, self, 0);
    catalog_variant_readout(ui, self);
}

static void
catalog_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatalogPanel* self = opaque;
    struct App* app = catalog_app(ui);
    const char* search;
    int loaded;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( !ui->chrome->panels[self->panel].visible )
        return;
    search = ToriRSChrome_Text(ui->chrome, self->input_search);
    loaded = app->provider ? CacheProvider_VisitLoaded(
                                 app->provider, (enum CacheProvider_CatalogKind)self->kind,
                                 catalog_count_only, self)
                           : 0;
    if( self->shown_kind != self->kind || strcmp(self->shown_search, search ? search : "") != 0
        || loaded != self->built_epoch )
    {
        self->built_epoch = loaded;
        catalog_rebuild(ui, self);
    }
    snprintf(line, sizeof(line), "%d of %d %s", self->count, loaded, k_kind_names[self->kind]);
    ToriRSChrome_SetText(ui->chrome, self->row_count, line);
    if( self->var_shown_id != ui->tools.pick_id || self->var_shown_kind != self->kind
        || self->var_shown_epoch != self->built_epoch )
        catalog_variant_rebuild(ui, self);
}

static void
catalog_picked_label(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    if( ui->tools.pick_id >= 0 )
        snprintf(line, sizeof(line), "%s %s", k_kind_names[self->kind], ui->tools.pick_name);
    else
        snprintf(line, sizeof(line), "nothing picked");
    ToriRSChrome_SetText(ui->chrome, self->row_picked, line);
}

static int
catalog_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct CatalogPanel* self = opaque;
    struct App* app = catalog_app(ui);

    if( widget == self->dropdown_kind )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->kind = choice >= 0 && choice < CATALOG_KIND_COUNT ? choice : CACHEPROVIDER_CATALOG_LOC;
        return 1;
    }
    if( widget == self->dropdown_list )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( choice >= 0 && choice < self->count )
        {
            ui->tools.pick_kind = self->kind;
            ui->tools.pick_id = self->ids[choice];
            snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s", self->labels[choice]);
            /* A loc pick arms Place: picking one is asking to put it down. */
            if( self->kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.tool == EDITOR_TOOL_SELECT )
                Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
            {
                int32_t values[2] = { self->kind, self->ids[choice] };
                (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_CATALOG_PICK, values, 2);
            }
        }
        else
        {
            ui->tools.pick_kind = -1;
            ui->tools.pick_id = -1;
            ui->tools.pick_name[0] = '\0';
        }
        catalog_picked_label(ui, self);
        return 1;
    }
    if( widget == self->dropdown_variant )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->var_choice = choice >= 0 && choice < self->var_count ? choice : 0;
        catalog_variant_readout(ui, self);
        return 1;
    }
    if( widget == self->item_reset )
    {
        EditorPreviewCamera_Invalidate(&app->preview_camera, false);
        return 1;
    }
    return widget == self->input_search || widget == self->view;
}

static void
catalog_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

int
Editor_Catalog_PreviewId(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind != self->kind )
        return -1;
    if( self->kind != CACHEPROVIDER_CATALOG_LOC )
        return ui->tools.pick_id;
    if( self->var_count > 0 && self->var_choice > 0 )
        return catalog_variant_id(self);
    return ui->tools.pick_id;
}

int
Editor_Catalog_ModelView(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    return self ? self->view : -1;
}

void
Editor_Catalog_Adopt(
    struct EditorUi* ui,
    int loc_id)
{
    struct CatalogPanel* self;
    struct App* app;
    struct ToriRS_Location* cfg;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || loc_id < 0 )
        return;
    app = catalog_app(ui);
    self->kind = CACHEPROVIDER_CATALOG_LOC;
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_kind, CACHEPROVIDER_CATALOG_LOC);
    ui->tools.pick_kind = CACHEPROVIDER_CATALOG_LOC;
    ui->tools.pick_id = loc_id;
    cfg = app->provider ? CacheProvider_LocationGet(app->provider, loc_id) : NULL;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)",
        cfg && cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    catalog_picked_label(ui, self);
    for( int i = 0; i < self->count; i++ )
        if( self->ids[i] == loc_id )
        {
            ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_list, i);
            break;
        }
}

void
Editor_Catalog_Register(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    assert(ui->app);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->kind = CACHEPROVIDER_CATALOG_LOC;
    self->shown_kind = -1;
    self->var_shown_id = -1;
    self->var_shown_kind = -1;
    self->var_shown_epoch = -1;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 420),
        edp_px(ui, 200), "Catalog");
    assert(self->panel >= 0);
    self->dropdown_kind =
        ToriRSChrome_Dropdown(chrome, self->panel, "Kind", k_kind_names, CATALOG_KIND_COUNT, 0);
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    self->dropdown_list = ToriRSChrome_Dropdown(chrome, self->panel, "", NULL, 0, -1);
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_picked = ToriRSChrome_Label(chrome, self->panel, "nothing picked");
    self->row_multi = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_variant = ToriRSChrome_Dropdown(chrome, self->panel, "", NULL, 0, -1);
    self->view = ToriRSChrome_ModelView(chrome, self->panel, 120, 96);
    self->item_reset = ToriRSChrome_MenuItem(chrome, self->panel, "Reset view");
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.name = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.desc = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.cfg = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.model = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.render = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.ops = ToriRSChrome_Label(chrome, self->panel, "");
    catalog_rows_hidden(ui, self, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "catalog";
    desc.title = "Catalog";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = catalog_tick;
    desc.activate = catalog_activate;
    desc.destroy = catalog_destroy;
    Editor_UiAddPanel(ui, &desc);
}
