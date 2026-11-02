/*
 * The inspector (App-bound, through ui->app_services): what is under the
 * cursor or latched, read out.
 *
 *   tile   the square and tile, the authored tokens (u, o;shape;rot, f) and
 *          whether the height is AUTHORED (an h token) or GENERATED (the
 *          noise routine's), which the drawn ground cannot tell you; on a
 *          bridge column (link-below) the three level spaces: cache, draw,
 *          paint.
 *   loc    the primary selected loc's record: name, examine, size and
 *          blocking, models and animation, lighting, ops; its placement and
 *          the multiloc rung it resolves to.
 *
 * "View tile" moves the selection from the loc to the ground it stands on.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "engine/torirs_types.h"

#include <stdio.h>
#include <string.h>

struct InspectorPanel
{
    int panel;
    int row_status;
    int row_square;
    int row_tokens;
    int row_height;
    int row_bridge;
    int row_loc_name;
    int row_loc_desc;
    int row_loc_place;
    int row_loc_cfg;
    int row_loc_model;
    int row_loc_render;
    int row_loc_ops;
    int item_view_tile;
};

static void
inspector_loc_rows(
    struct EditorUi* ui,
    struct InspectorPanel* self)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    char line[TORIRS_CHROME_LABEL_MAX];
    const struct Editor_LocRef* ref;
    struct ToriRS_Location* cfg;
    int const has = ui->tools.loc_count > 0;
    int wx;
    int wz;

    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_name, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_desc, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_place, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_cfg, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_model, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_render, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->row_loc_ops, !has);
    ToriRSChrome_SetHidden(ui->chrome, self->item_view_tile, !has);
    if( !has )
        return;

    ref = &ui->tools.locs[0];
    Editor_LocRefWorld(ref, &wx, &wz);
    snprintf(line, sizeof(line), "%d,%d L%d  shape %d  rot %d%s", wx, wz, ref->level, ref->shape,
        ref->rotation, ui->tools.loc_count > 1 ? "  (+ more selected)" : "");
    ToriRSChrome_SetText(ui->chrome, self->row_loc_place, line);

    cfg = services->loc_record ? services->loc_record(services->user_data, ref->loc_id) : NULL;
    if( !cfg )
    {
        snprintf(line, sizeof(line), "loc %d  (config not resident)", ref->loc_id);
        ToriRSChrome_SetText(ui->chrome, self->row_loc_name, line);
        ToriRSChrome_SetText(ui->chrome, self->row_loc_desc, "");
        ToriRSChrome_SetText(ui->chrome, self->row_loc_cfg, "");
        ToriRSChrome_SetText(ui->chrome, self->row_loc_model, "");
        ToriRSChrome_SetText(ui->chrome, self->row_loc_render, "");
        ToriRSChrome_SetText(ui->chrome, self->row_loc_ops, "");
        return;
    }
    snprintf(line, sizeof(line), "%s  (loc %d)", cfg->name[0] ? cfg->name : "<unnamed>", ref->loc_id);
    ToriRSChrome_SetText(ui->chrome, self->row_loc_name, line);
    ToriRSChrome_SetText(ui->chrome, self->row_loc_desc, cfg->desc[0] ? cfg->desc : "(no examine)");
    snprintf(line, sizeof(line), "size %dx%d  walk %s  proj %s  approach %d", cfg->size_x, cfg->size_z,
        cfg->blocks_walk ? "block" : "-", cfg->blocks_projectiles ? "block" : "-", cfg->force_approach);
    ToriRSChrome_SetText(ui->chrome, self->row_loc_cfg, line);
    if( cfg->transform_count > 0 )
        snprintf(line, sizeof(line), "multiloc %s %d, %d rungs", cfg->transform_varbit != -1 ? "varbit" : "varp",
            cfg->transform_varbit != -1 ? cfg->transform_varbit : cfg->transform_varp, cfg->transform_count);
    else
        snprintf(line, sizeof(line), "models %d  seq %d  wallw %d", cfg->shapes_and_model_count, cfg->seq_id,
            cfg->wall_width);
    ToriRSChrome_SetText(ui->chrome, self->row_loc_model, line);
    snprintf(line, sizeof(line), "amb %d con %d%s%s%s", cfg->ambient, cfg->contrast,
        cfg->sharelight ? "  sharelight" : "", cfg->occlude ? "  occlude" : "",
        cfg->contoured_ground ? "  contoured" : "");
    ToriRSChrome_SetText(ui->chrome, self->row_loc_render, line);
    line[0] = '\0';
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        if( cfg->actions[i][0] )
        {
            size_t const at = strlen(line);
            snprintf(line + at, sizeof(line) - at, "%s%s", at ? " / " : "", cfg->actions[i]);
        }
    ToriRSChrome_SetText(ui->chrome, self->row_loc_ops, line[0] ? line : "(no ops)");
}

static void
inspector_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct InspectorPanel* self = opaque;
    const struct EditorUiAppServices* services = &ui->app_services;
    char line[TORIRS_CHROME_LABEL_MAX];
    int world_x;
    int world_z;
    int level;
    int latched = ui->tools.tile_valid;

    if( !ui->chrome->panels[self->panel].visible )
        return;

    if( ui->editor )
    {
        snprintf(line, sizeof(line), "%s%s", ui->editor->writable ? "writable" : "READ-ONLY",
            Editor_DocHasUnsaved(&ui->editor->doc) ? "  *unsaved*" : "");
        ToriRSChrome_SetText(ui->chrome, self->row_status, line);
    }
    inspector_loc_rows(ui, self);

    if( latched )
    {
        world_x = ui->tools.tile_x;
        world_z = ui->tools.tile_z;
        level = ui->tools.tile_level;
    }
    /* With a level pinned, the pinned plane's tile under the pointer and that
     * level (Editor_PanelHoverTile), not the first surface the pick met. */
    else if( !services->hover_tile || !services->hover_tile(services->user_data, &world_x, &world_z, &level) )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_square, "no tile under the cursor");
        ToriRSChrome_SetText(ui->chrome, self->row_tokens, "");
        ToriRSChrome_SetText(ui->chrome, self->row_height, "");
        ToriRSChrome_SetText(ui->chrome, self->row_bridge, "");
        return;
    }
    if( level < 0 || level > 3 )
        level = 0;

    snprintf(line, sizeof(line), "%sm%d_%d  tile %d,%d  level %d", latched ? "[latched] " : "",
        world_x / 64, world_z / 64, world_x % 64, world_z % 64, level);
    ToriRSChrome_SetText(ui->chrome, self->row_square, line);

    {
        struct Editor_Square* square =
            ui->editor ? Editor_DocFindSquare(&ui->editor->doc, world_x / 64, world_z / 64) : NULL;
        const struct Editor_Tile* tile;
        int derived = 0;

        if( !square || !square->loaded )
        {
            ToriRSChrome_SetText(ui->chrome, self->row_tokens, "square not open in the editor");
            ToriRSChrome_SetText(ui->chrome, self->row_height, "");
            ToriRSChrome_SetText(ui->chrome, self->row_bridge, "");
            return;
        }
        tile = &square->tiles[Editor_TileIndex(world_x % 64, world_z % 64, level)];
        snprintf(line, sizeof(line), "u%d o%d;%d;%d f%d", tile->underlay_id,
            tile->has_overlay ? tile->overlay_id : 0, tile->shape, tile->rotation, tile->settings);
        ToriRSChrome_SetText(ui->chrome, self->row_tokens, line);
        (void)Editor_OpsDerivedHeight(&ui->editor->doc, world_x, world_z, level, &derived);
        if( tile->has_height )
            snprintf(line, sizeof(line), "height h%d (authored), draws at %d", tile->height, derived);
        else
            snprintf(line, sizeof(line), "height generated (no h token), draws at %d", derived);
        ToriRSChrome_SetText(ui->chrome, self->row_height, line);

        /* A bridge column: level 1's link-below moves the tiles above down a
         * plane, so the cache level, the plane it draws on and the plane it
         * is painted with differ. */
        {
            const struct Editor_Tile* bridge = &square->tiles[Editor_TileIndex(world_x % 64, world_z % 64, 1)];
            if( bridge->settings & 0x2 )
            {
                int const draw = level > 0 ? level - 1 : 0;
                snprintf(line, sizeof(line), "bridge column: cache %d, draw %d, paint %d", level, draw, draw);
            }
            else
                snprintf(line, sizeof(line), " ");
            ToriRSChrome_SetText(ui->chrome, self->row_bridge, line);
        }
    }
}

static int
inspector_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct InspectorPanel* self = opaque;
    const struct EditorUiAppServices* services = &ui->app_services;

    if( widget != self->item_view_tile )
        return 0;
    if( ui->tools.loc_count > 0 && services->select_terrain )
    {
        const struct Editor_LocRef* ref = &ui->tools.locs[0];
        int wx;
        int wz;
        Editor_LocRefWorld(ref, &wx, &wz);
        services->select_terrain(services->user_data, wx, wz, ref->level);
        if( services->request_redraw )
            services->request_redraw(services->user_data);
    }
    return 1;
}

static void
inspector_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Inspector_Register(struct EditorUi* ui)
{
    struct InspectorPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int x;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    x = edp_surface_w(ui) > edp_px(ui, EDP_PICKER_W + 8)
            ? edp_surface_w(ui) - edp_px(ui, EDP_PICKER_W + 8)
            : edp_px(ui, EDP_COL_X + EDP_COL_W + 8);
    /* Under the picker, at the bottom of the right-hand column. */
    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, x,
        chrome->surface_h > edp_px(ui, 800) ? chrome->surface_h - edp_px(ui, 250) : edp_px(ui, 560),
        edp_px(ui, EDP_PICKER_W), "Inspector");
    assert(self->panel >= 0);
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_square = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_tokens = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_height = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_bridge = ToriRSChrome_Label(chrome, self->panel, " ");
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_loc_name = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_loc_desc = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_loc_place = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_loc_cfg = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_loc_model = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_loc_render = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_loc_ops = ToriRSChrome_Label(chrome, self->panel, "");
    self->item_view_tile = ToriRSChrome_MenuItem(chrome, self->panel, "View tile");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "inspector";
    desc.title = "Inspector";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = inspector_tick;
    desc.activate = inspector_activate;
    desc.destroy = inspector_destroy;
    Editor_UiAddPanel(ui, &desc);
}
