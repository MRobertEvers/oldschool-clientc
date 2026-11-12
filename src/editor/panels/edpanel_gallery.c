/*
 * The Gallery panel (module "gallery", View > Gallery, Shift+F8; workbench
 * 7.2, docs/MAPED_DESIGN.html #catalog): the Catalog's search as pictures.
 *
 *   One query    the Gallery is a second VIEW of the Catalog's query, not a
 *                second query: its Find box and the Catalog's edit one text
 *                (whichever changed last wins), its kind chips choose the
 *                Catalog's kind, the filter chips are the Catalog's, and the
 *                rows are the shared Editor_CatalogModel's. The Catalog
 *                drives the query while either panel shows
 *                (Editor_Gallery_Visible), so the Gallery works with the
 *                Catalog closed. Both ask the model for the page their rows
 *                are on; the model holds a few pages, so two windows cost
 *                nothing once their pages landed.
 *   Tiles        a GRAPH well filling the panel: a picture per row at the
 *                tile size (S / M / L / XL / XXL = 32 / 48 / 64 / 96 / 128
 *                px, the device pref gallery_tile; a loc or npc is rendered
 *                at that size, an obj's 32 px icon is stretched to it), the
 *                pack name under it, ellipsised. An obj's
 *                picture is the game's inventory icon
 *                (app_services.obj_icon: the cache's zoom2d and 2D angles);
 *                a loc's or npc's the record renderer's
 *                (app_services.record_thumb, app_record_thumbs.c). A tile
 *                whose picture is still rendering shows the name's first
 *                letters until it lands. The web draws names only.
 *   Click        the row becomes the shared pick (Editor_Catalog_PickRow:
 *                published as STATE_CATALOG_PICK, the Preview follows).
 *   Double-click the pick's action: an obj goes into the account builder's
 *                selected slot when the Accounts panel is open on Items
 *                (Editor_Accounts_PutObj), else -- like a loc or npc --
 *                Place is armed (the Place tool stamps a loc, the spawn
 *                ghost an npc or obj).
 *   Right-click  the pick, and the Preview opened beside this panel (its
 *                action chips: Place, favourite, palette, record, triggers,
 *                find in world, copy).
 *   Star         the corner of a tile: a click there stars the record into
 *                editor/favourites.txt, or takes it out (the Catalog's star,
 *                Editor_Catalog_ToggleFavourite: one undo step, Save writes
 *                it). A starred tile wears a gold star; the tile under the
 *                pointer shows an empty one to click. The "favourites" chip
 *                lists the starred records alone, and again goes back.
 *   Scroll       the wheel and a trackpad scroll move the window a tile row
 *                at a time; the well's keys (Up / Down / Home / End) too while
 *                it has the focus. The bar down the well's right edge is
 *                where the window is in the whole list -- every match, not
 *                the hundred a page holds -- and a press or drag on it goes
 *                there; the count line says the same in numbers ("37 to 72
 *                of 63,334 loc").
 *   Controls     two dropdowns, each with a label a person can read and an
 *                agent can name (2026-11-11: they were chips drawn in a
 *                GRAPH, pressable only by their pixel): "Show" (all, loc,
 *                npc, obj -- the kinds with pictures -- then "favourites",
 *                the starred records alone, and the Catalog's kind when it
 *                is another) and "Tiles" (the five sizes). Two rows, so the
 *                well keeps the panel. The list of the same search is the
 *                Catalog (F8), whose "grid" chip opens this panel beside it.
 *   Names        two lines under a tile, so `timberwall_with_window` is not
 *                `timberw..`; the tooltip has the whole name and the id.
 */

#include "edpanel_catalog_model.h"
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/accounts/accounts_panel.h"
#include "editor/editor_prefs.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The panel's width at registration and the well's natural and least
 *  heights (logical px). */
#define GALLERY_W 260
#define GALLERY_WELL_H 320
#define GALLERY_WELL_MIN_H 96
/** A tile: the picture, a frame, the name's two lines. */
#define GALLERY_TILE_PAD 4
#define GALLERY_NAME_LINE_H 12
#define GALLERY_NAME_H (2 * GALLERY_NAME_LINE_H + 2)
#define GALLERY_GAP 4
/** Frames within which a second press on the same tile is a double-click. */
#define GALLERY_DOUBLE_FRAMES 20
/** Rows the window asks for beyond the screen (the prefetch's margin). */
#define GALLERY_ROWS_AHEAD 2

/** The kinds the Gallery's strip offers: the ones with pictures. */
static const uint32_t k_gallery_kinds[] = {
    TORIRSMAPED_CATALOG_ANY, TORIRSMAPED_CATALOG_LOC, TORIRSMAPED_CATALOG_NPC, TORIRSMAPED_CATALOG_OBJ,
};
#define GALLERY_KINDS ((int)(sizeof(k_gallery_kinds) / sizeof(k_gallery_kinds[0])))
/** The sizes Tiles offers, smallest first, and what it calls them. */
#define GALLERY_SIZES 5
static const int k_gallery_sizes[GALLERY_SIZES] = { EDITOR_PREFS_GALLERY_TILE_S, EDITOR_PREFS_GALLERY_TILE_M,
    EDITOR_PREFS_GALLERY_TILE_L, EDITOR_PREFS_GALLERY_TILE_XL, EDITOR_PREFS_GALLERY_TILE_XXL };
static const char* const k_gallery_size_names[GALLERY_SIZES] = { "S (32 px)", "M (48 px)", "L (64 px)", "XL (96 px)",
                                                                 "XXL (128 px)" };
/** The star in a tile's corner: its box, and its hit key (this plus the
 *  row; a tile's own key is 1 + row, far below). */
#define GALLERY_STAR 11
#define GALLERY_STAR_KEY (1 << 24)
/** The scroll bar: its width, its least thumb, its hit key. */
#define GALLERY_BAR_W 6
#define GALLERY_BAR_THUMB_MIN 12
#define GALLERY_BAR_KEY (1 << 25)

struct GalleryPanel
{
    int panel;
    int input_find;
    /** Kind: the kinds with pictures, and a fifth option naming the
     *  Catalog's kind while it is another. `kind_sel` / `size_sel` are the
     *  options last shown, so a change is the user's. */
    int drop_kind;
    int drop_size;
    const char* kind_options[GALLERY_KINDS + 2];
    int kind_option_count;
    int kind_sel;
    int size_sel;
    int row_count;
    int graph;

    /** The shared Find text as last seen by both boxes. */
    char seen_find[TORIRS_CHROME_INPUT_MAX];
    /** The window: the first tile row shown, the grid's shape at the last
     *  draw. */
    int first_row;
    int cols;
    int rows;
    int cell_w;
    int cell_h;
    int well_h;
    /** What the last draw showed: redrawn when any moves. */
    uint32_t drawn_answers;
    uint32_t drawn_key;
    int drawn_first;
    int drawn_px;
    int drawn_w;
    int drawn_pick_kind;
    int drawn_pick_id;
    int drawn_hover;
    uint32_t drawn_favourites;
    /** Pictures still rendering at the last draw: drawn again next tick. */
    int pending;
    int hover_row;
    /** The pointer is on the hovered tile's star. */
    int hover_star;
    /** editor/favourites.txt as this tick read it. */
    uint32_t favourites_serial;
    int favourites_count;
    /** A press on the scroll bar is being dragged. */
    int bar_drag;
    /** The last press: its row and frame (a second within
     *  GALLERY_DOUBLE_FRAMES is a double-click). */
    int press_row;
    int press_frame;
    int frame;
    /** 1/256 px of trackpad scroll not yet a whole row. */
    int scroll_q8;
    char tip[TORIRS_CHROME_LABEL_MAX];
};

/* ---- the shared query ------------------------------------------------------------- */

static struct GalleryPanel*
gallery_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "gallery");
}

static int
gallery_visible(
    struct EditorUi* ui,
    const struct GalleryPanel* self)
{
    return ui->chrome->panels[self->panel].visible;
}

/** Two boxes, one text: the one that changed since the last tick wins. */
static void
gallery_find_sync(
    struct EditorUi* ui,
    struct GalleryPanel* self)
{
    const char* ours = ToriRSChrome_Text(ui->chrome, self->input_find);
    const char* theirs = Editor_Catalog_FindText(ui);

    if( !ours )
        ours = "";
    if( strcmp(ours, self->seen_find) != 0 )
    {
        Editor_Catalog_SetFindText(ui, ours);
        snprintf(self->seen_find, sizeof(self->seen_find), "%s", ours);
        return;
    }
    /* The Catalog took a key=value word into a chip, or a kind prefix: its
     * box is the truth. */
    theirs = Editor_Catalog_FindText(ui);
    if( strcmp(theirs, self->seen_find) != 0 )
    {
        ToriRSChrome_SetText(ui->chrome, self->input_find, theirs);
        snprintf(self->seen_find, sizeof(self->seen_find), "%s", theirs);
    }
}

/** Show's option after the kinds: the starred records alone. */
#define GALLERY_SHOW_FAVOURITES GALLERY_KINDS

/** Show and Tiles follow the Catalog's kind, its favourites view and the
 *  tile pref, and set them when the user picks an option. */
static void
gallery_controls_sync(
    struct EditorUi* ui,
    struct GalleryPanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    uint32_t current;
    int sel;
    int want = GALLERY_KINDS + 1;
    int options = GALLERY_KINDS + 1;

    /* The user's choice first: an option other than the one last shown.
     * (Before the first sync nothing was shown: nothing is a choice.) */
    sel = ToriRSChrome_DropdownSelected(chrome, self->drop_kind);
    if( self->kind_sel >= 0 && sel != self->kind_sel && sel >= 0 && sel <= GALLERY_SHOW_FAVOURITES )
    {
        /* ShowFavourites is one switch, both ways. */
        if( (sel == GALLERY_SHOW_FAVOURITES) != (Editor_Catalog_ShowingFavourites(ui) != 0) )
            Editor_Catalog_ShowFavourites(ui);
        if( sel < GALLERY_KINDS )
            Editor_Catalog_SetKind(ui, k_gallery_kinds[sel]);
        self->first_row = 0;
    }
    sel = ToriRSChrome_DropdownSelected(chrome, self->drop_size);
    if( self->size_sel >= 0 && sel != self->size_sel && sel >= 0 && sel < GALLERY_SIZES
        && ui->prefs.gallery_tile != k_gallery_sizes[sel] )
    {
        ui->prefs.gallery_tile = k_gallery_sizes[sel];
        ui->prefs_serial++;
    }

    current = Editor_Catalog_Kind(ui);
    for( int i = 0; i < GALLERY_KINDS; i++ )
        if( k_gallery_kinds[i] == current )
            want = i;
    if( want == GALLERY_KINDS + 1 )
    {
        /* A kind with no pictures (seq, spotanim): listed while it is the
         * Catalog's, so the dropdown never names a kind that is not shown. */
        self->kind_options[GALLERY_KINDS + 1] = ToriRSMapEdCatalog_KindName(current);
        options = GALLERY_KINDS + 2;
    }
    if( Editor_Catalog_ShowingFavourites(ui) )
        want = GALLERY_SHOW_FAVOURITES;
    if( options != self->kind_option_count || want != self->kind_sel
        || ToriRSChrome_DropdownSelected(chrome, self->drop_kind) != want )
    {
        ToriRSChrome_DropdownSetOptions(chrome, self->drop_kind, self->kind_options, options, want);
        self->kind_option_count = options;
        self->kind_sel = want;
    }
    want = 1;
    for( int i = 0; i < GALLERY_SIZES; i++ )
        if( Editor_Gallery_TilePx(ui) == k_gallery_sizes[i] )
            want = i;
    if( want != self->size_sel || ToriRSChrome_DropdownSelected(chrome, self->drop_size) != want )
    {
        ToriRSChrome_DropdownSetSelected(chrome, self->drop_size, want);
        self->size_sel = want;
    }
}

/* ---- the well ------------------------------------------------------------------------ */

/** The well's width in logical px (the panel's content column). */
static int
gallery_well_w(
    struct EditorUi* ui,
    const struct GalleryPanel* self)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const w = ui->chrome->widgets[self->graph].w;
    return w > 0 ? w / (scale > 0 ? scale : 1) : GALLERY_W - 24;
}

/**
 * The well fills the panel: whatever the other rows leave of a panel the
 * layout or a resize gave a height, down to GALLERY_WELL_MIN_H; a
 * content-sized panel gets the natural height.
 */
static void
gallery_size_well(
    struct EditorUi* ui,
    struct GalleryPanel* self)
{
    struct ToriRSChromePanel const* p = &ui->chrome->panels[self->panel];
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];
    int const scale = ToriRSChrome_Scale(ui->chrome) > 0 ? ToriRSChrome_Scale(ui->chrome) : 1;
    int want = edp_px(ui, GALLERY_WELL_H);

    if( p->fixed_h > 0 && p->view_h > 0 && p->content_h > 0 )
    {
        want = p->view_h - (p->content_h - well->view_h);
        if( want < edp_px(ui, GALLERY_WELL_MIN_H) )
            want = edp_px(ui, GALLERY_WELL_MIN_H);
    }
    if( want != well->view_h )
        ToriRSChrome_SetCustomHeight(ui->chrome, self->graph, want);
    /* The grid works in the well's own (logical) pixels. */
    self->well_h = ui->chrome->widgets[self->graph].view_h / scale;
}

/** EditorUiPanel::stretch: the well is the part that fills. */
static int
gallery_stretch(
    struct EditorUi* ui,
    void* opaque,
    int* out_min_h,
    int* out_natural_h)
{
    struct GalleryPanel* self = opaque;

    assert(out_min_h);
    assert(out_natural_h);
    *out_min_h = edp_px(ui, GALLERY_WELL_MIN_H);
    *out_natural_h = edp_px(ui, GALLERY_WELL_H);
    return ui->chrome->widgets[self->graph].view_h;
}

/** The grid's shape at the tile size and the well's width. */
static void
gallery_grid(
    struct EditorUi* ui,
    struct GalleryPanel* self)
{
    int const px = Editor_Gallery_TilePx(ui);
    /* The scroll bar's column is not the tiles'. */
    int const w = gallery_well_w(ui, self) - GALLERY_BAR_W - GALLERY_GAP;

    self->cell_w = px + 2 * GALLERY_TILE_PAD;
    self->cell_h = px + 2 * GALLERY_TILE_PAD + GALLERY_NAME_H;
    self->cols = (w + GALLERY_GAP) / (self->cell_w + GALLERY_GAP);
    if( self->cols < 1 )
        self->cols = 1;
    self->rows = (self->well_h + GALLERY_GAP) / (self->cell_h + GALLERY_GAP);
    if( self->rows < 1 )
        self->rows = 1;
}

/** Tile rows the list has. */
static int
gallery_row_count(
    const struct GalleryPanel* self,
    const struct Editor_CatalogModel* model)
{
    int const count = Editor_CatalogModelViewCount(model);
    return (count + self->cols - 1) / self->cols;
}

static void
gallery_clamp(
    struct GalleryPanel* self,
    const struct Editor_CatalogModel* model)
{
    int const rows = gallery_row_count(self, model);
    int const last = rows - self->rows;

    if( self->first_row > last )
        self->first_row = last;
    if( self->first_row < 0 )
        self->first_row = 0;
}

/** The picture of `view`: a scene sprite id, 0 while it renders, -1 none. */
static int
gallery_picture(
    struct EditorUi* ui,
    const struct Editor_CatalogView* view,
    int px)
{
    const struct EditorUiAppServices* services = &ui->app_services;

    if( view->id < 0 )
        return -1;
    if( view->kind == TORIRSMAPED_CATALOG_OBJ )
        return services->obj_icon ? services->obj_icon(services->user_data, view->id, 1, NULL, 0) : -1;
    if( view->kind == TORIRSMAPED_CATALOG_LOC || view->kind == TORIRSMAPED_CATALOG_NPC )
        return services->record_thumb ? services->record_thumb(services->user_data, view->kind, view->id, px) : -1;
    return -1;
}

/** A five-pointed star in a GALLERY_STAR box at (x, y): the pentagram, each
 *  point joined to the next but one. */
static void
gallery_star(
    struct ToriRSChrome* chrome,
    int graph,
    int x,
    int y,
    uint32_t color)
{
    static const signed char k_points[5][2] = { { 5, 0 }, { 10, 4 }, { 8, 10 }, { 2, 10 }, { 0, 4 } };

    for( int i = 0; i < 5; i++ )
        ToriRSChrome_GraphLine(chrome, graph, x + k_points[i][0], y + k_points[i][1], x + k_points[(i + 2) % 5][0],
            y + k_points[(i + 2) % 5][1], color);
}

/** The scroll bar's thumb: its top and height in the well, 0 when the
 *  whole list is on screen (no bar). */
static int
gallery_bar_thumb(
    const struct GalleryPanel* self,
    const struct Editor_CatalogModel* model,
    int* out_y)
{
    int const total = gallery_row_count(self, model);
    int const beyond = total - self->rows;
    int h;

    *out_y = 0;
    if( beyond <= 0 || self->well_h <= GALLERY_BAR_THUMB_MIN )
        return 0;
    h = (int)((int64_t)self->well_h * self->rows / total);
    if( h < GALLERY_BAR_THUMB_MIN )
        h = GALLERY_BAR_THUMB_MIN;
    *out_y = (int)((int64_t)(self->well_h - h) * (self->first_row < beyond ? self->first_row : beyond) / beyond);
    return h;
}

/** The tile row a press at `y` on the scroll bar goes to: the thumb's
 *  middle under the pointer. */
static int
gallery_bar_row(
    const struct GalleryPanel* self,
    const struct Editor_CatalogModel* model,
    int y)
{
    int const beyond = gallery_row_count(self, model) - self->rows;
    int thumb_y;
    int const h = gallery_bar_thumb(self, model, &thumb_y);
    int row;

    if( h <= 0 || self->well_h - h <= 0 )
        return 0;
    row = (int)((int64_t)(y - h / 2) * beyond / (self->well_h - h));
    return row < 0 ? 0 : row > beyond ? beyond : row;
}

/** How many bytes of `text` fit on one name line `width` px wide, in the face
 *  graph text is drawn in; with `dots`, how many fit with ".." after them.
 *  Never more than a graph text item holds; at least one byte. */
static int
gallery_fit(
    const struct ToriRSChrome* chrome,
    const char* text,
    int width,
    int dots)
{
    int const face = chrome->theme.font_row;
    int used = dots ? 2 * ToriRSChrome_GlyphAdvanceQ(face, 4, '.') : 0;
    int n = 0;

    while( text[n] != '\0' && n < TORIRS_CHROME_GRAPH_TEXT_MAX - 3 )
    {
        used += ToriRSChrome_GlyphAdvanceQ(face, 4, (unsigned char)text[n]);
        if( used > width )
            break;
        n++;
    }
    return n > 0 ? n : 1;
}

static void
gallery_draw(
    struct EditorUi* ui,
    struct GalleryPanel* self,
    const struct Editor_CatalogModel* model)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const px = Editor_Gallery_TilePx(ui);
    int const count = Editor_CatalogModelViewCount(model);
    int const first = self->first_row * self->cols;
    /* The name lines' width, measured in the face graph text is drawn in
     * (2026-11-11: counted at 6 px a letter, "brownbed" ran into the next
     * tile's "orangebed"). */
    int const name_w = self->cell_w - 4;

    self->pending = 0;
    ToriRSChrome_GraphBegin(chrome, self->graph);
    for( int i = 0; i < self->rows * self->cols && first + i < count; i++ )
    {
        int const row = first + i;
        int const x = (i % self->cols) * (self->cell_w + GALLERY_GAP);
        int const y = (i / self->cols) * (self->cell_h + GALLERY_GAP);
        struct Editor_CatalogView view;
        int picture = 0;
        int picked;
        char name[48];

        ToriRSChrome_GraphRect(chrome, self->graph, x, y, self->cell_w, self->cell_h, 0x18150f, 1, 1 + row);
        if( !Editor_CatalogModelViewAt(model, row, &view) )
            continue; /* its page is still to land */
        picture = gallery_picture(ui, &view, px);
        if( picture > 0 )
            ToriRSChrome_GraphImage(chrome, self->graph, x + GALLERY_TILE_PAD, y + GALLERY_TILE_PAD, px, px, picture, 0,
                (uint32_t)picture);
        else
        {
            /* The name's first letters until (or where) a picture comes. */
            char shortname[8];
            if( picture == 0 )
                self->pending = 1;
            snprintf(shortname, sizeof(shortname), "%.6s", view.name);
            ToriRSChrome_GraphText(chrome, self->graph, x + GALLERY_TILE_PAD + 1, y + GALLERY_TILE_PAD + px / 2 + 4,
                picture == 0 ? 0x8a8478 : 0xc06050, shortname);
        }
        /* The name on two lines: what fits on the first, then the rest,
         * ellipsised only when the two do not hold it. */
        {
            int const line = gallery_fit(chrome, view.name, name_w, 0);
            snprintf(name, sizeof(name), "%.*s", line, view.name);
            ToriRSChrome_GraphText(
                chrome, self->graph, x + 2, y + self->cell_h - 4 - GALLERY_NAME_LINE_H, 0xd8d0c0, name);
            if( view.name[line] != '\0' )
            {
                const char* rest = view.name + line;
                int const fits = gallery_fit(chrome, rest, name_w, 0);
                if( rest[fits] == '\0' )
                    snprintf(name, sizeof(name), "%s", rest);
                else
                    snprintf(name, sizeof(name), "%.*s..", gallery_fit(chrome, rest, name_w, 1), rest);
                ToriRSChrome_GraphText(chrome, self->graph, x + 2, y + self->cell_h - 4, 0xd8d0c0, name);
            }
        }
        picked = (int)view.kind == ui->tools.pick_kind && view.id == ui->tools.pick_id && view.id >= 0;
        if( picked )
            ToriRSChrome_GraphRect(chrome, self->graph, x, y, self->cell_w, self->cell_h, 0xffd040, 0, 0);
        else if( row == self->hover_row )
            ToriRSChrome_GraphRect(chrome, self->graph, x, y, self->cell_w, self->cell_h, 0x8a8478, 0, 0);
        /* The star: gold on a favourite, empty on the tile under the
         * pointer (the thing to click), on a patch of the tile's own colour
         * so it reads over a picture. Loc, npc and obj are what the file
         * holds. */
        if( view.kind <= TORIRSMAPED_CATALOG_OBJ && view.name[0] )
        {
            int const starred = Editor_Catalog_RecordIsFavourite(ui, view.kind, view.name);
            if( starred || row == self->hover_row )
            {
                int const sx = x + self->cell_w - GALLERY_STAR - 2;
                int const sy = y + 2;
                ToriRSChrome_GraphRect(chrome, self->graph, sx - 1, sy - 1, GALLERY_STAR + 2, GALLERY_STAR + 2, 0x18150f, 1,
                    GALLERY_STAR_KEY + row);
                gallery_star(chrome, self->graph, sx, sy,
                    starred ? 0xffd040 : row == self->hover_row && self->hover_star ? 0xd8d0c0 : 0x8a8478);
            }
        }
    }
    {
        /* The scroll bar: where the window is in every row there is. */
        int thumb_y;
        int const thumb_h = gallery_bar_thumb(self, model, &thumb_y);
        int const bar_x = gallery_well_w(ui, self) - GALLERY_BAR_W;
        if( thumb_h > 0 )
        {
            ToriRSChrome_GraphRect(chrome, self->graph, bar_x, 0, GALLERY_BAR_W, self->well_h, 0x18150f, 1, GALLERY_BAR_KEY);
            ToriRSChrome_GraphRect(chrome, self->graph, bar_x, thumb_y, GALLERY_BAR_W, thumb_h,
                self->bar_drag ? 0xd8d0c0 : 0x8a8478, 1, GALLERY_BAR_KEY);
        }
    }
    ToriRSChrome_GraphEnd(chrome, self->graph);
    self->drawn_answers = model->answers;
    self->drawn_key = model->key_serial;
    self->drawn_first = self->first_row;
    self->drawn_px = px;
    self->drawn_w = self->cols;
    self->drawn_pick_kind = ui->tools.pick_kind;
    self->drawn_pick_id = ui->tools.pick_id;
    self->drawn_hover = self->hover_row * 2 + (self->hover_star ? 1 : 0);
    self->drawn_favourites = self->favourites_serial;
}

/** The tooltip follows the hovered tile. */
static void
gallery_tip(
    struct EditorUi* ui,
    struct GalleryPanel* self,
    const struct Editor_CatalogModel* model)
{
    struct Editor_CatalogView view;
    char tip[TORIRS_CHROME_LABEL_MAX];

    if( self->hover_row < 0 || !Editor_CatalogModelViewAt(model, self->hover_row, &view) )
        snprintf(tip, sizeof(tip), "click: pick; double-click: place (or put in the account's slot)");
    else if( self->hover_star && Editor_Catalog_RecordIsFavourite(ui, view.kind, view.name) )
        snprintf(tip, sizeof(tip), "%.32s: a favourite; click to unstar", view.name);
    else if( self->hover_star )
        snprintf(tip, sizeof(tip), "%.28s: click to star as a favourite", view.name);
    else if( view.kind == TORIRSMAPED_CATALOG_OBJ && view.wearpos >= 0 )
        snprintf(tip, sizeof(tip), "%.28s (%d) %.16s, %s", view.name, (int)view.id, view.display,
            Editor_CatalogWearName(view.wearpos));
    else if( view.display[0] && strcmp(view.display, view.name) != 0 )
        snprintf(tip, sizeof(tip), "%.32s (%d) %.20s", view.name, (int)view.id, view.display);
    else
        snprintf(tip, sizeof(tip), "%.48s (%d)", view.name, (int)view.id);
    if( strcmp(tip, self->tip) != 0 )
    {
        snprintf(self->tip, sizeof(self->tip), "%s", tip);
        ToriRSChrome_SetTooltip(ui->chrome, self->graph, tip);
    }
}

/* ---- the module ---------------------------------------------------------------------- */

static void
gallery_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct GalleryPanel* self = opaque;
    const struct Editor_CatalogModel* model;
    char line[TORIRS_CHROME_LABEL_MAX];

    self->frame++;
    if( !gallery_visible(ui, self) )
        return;
    model = Editor_Catalog_Model(ui);
    if( !model )
        return;
    gallery_find_sync(ui, self);
    self->favourites_serial = Editor_Catalog_FavouritesSerial(ui, &self->favourites_count);
    gallery_controls_sync(ui, self);
    gallery_size_well(ui, self);
    gallery_grid(ui, self);
    gallery_clamp(self, model);
    Editor_Catalog_WantWindow(ui, self->first_row * self->cols, (self->rows + GALLERY_ROWS_AHEAD) * self->cols);
    /* What is on screen, of everything the wheel and the bar reach. */
    Editor_CatalogModelWindowText(model, self->first_row * self->cols, self->rows * self->cols, line, sizeof(line));
    ToriRSChrome_SetText(ui->chrome, self->row_count, line);
    if( self->pending || model->answers != self->drawn_answers || model->key_serial != self->drawn_key
        || self->first_row != self->drawn_first || Editor_Gallery_TilePx(ui) != self->drawn_px || self->cols != self->drawn_w
        || ui->tools.pick_kind != self->drawn_pick_kind || ui->tools.pick_id != self->drawn_pick_id
        || self->hover_row * 2 + (self->hover_star ? 1 : 0) != self->drawn_hover
        || self->favourites_serial != self->drawn_favourites || self->bar_drag )
        gallery_draw(ui, self, model);
    gallery_tip(ui, self, model);
}

/** The pick's action on `row`: an obj into the account builder's slot when
 *  it is open on one, else Place armed. */
static void
gallery_primary(
    struct EditorUi* ui,
    int row)
{
    struct Editor_CatalogView view;
    const struct Editor_CatalogModel* model = Editor_Catalog_Model(ui);

    if( !model || !Editor_CatalogModelViewAt(model, row, &view) || view.id < 0 || view.kind > TORIRSMAPED_CATALOG_OBJ )
        return;
    if( view.kind == TORIRSMAPED_CATALOG_OBJ && Editor_Accounts_PutObj(ui, view.id) )
        return;
    Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    Editor_UiStatus(ui, "Place %s: click a tile", view.name);
}

static int
gallery_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct GalleryPanel* self = opaque;

    if( event->widget != self->graph )
        return 0;
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_RELEASE )
    {
        self->bar_drag = 0;
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_DRAG )
    {
        const struct Editor_CatalogModel* model = Editor_Catalog_Model(ui);
        if( self->bar_drag && model )
            self->first_row = gallery_bar_row(self, model, event->y);
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS && event->key >= GALLERY_BAR_KEY )
    {
        /* The scroll bar: the window goes where the press is, and follows
         * the drag. */
        const struct Editor_CatalogModel* model = Editor_Catalog_Model(ui);
        if( model )
        {
            self->first_row = gallery_bar_row(self, model, event->y);
            self->bar_drag = 1;
        }
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS && event->key >= GALLERY_STAR_KEY )
    {
        /* The star: the tile becomes the pick (the Preview follows, as a
         * click would have it), and the pick is starred or unstarred. */
        self->press_row = -1;
        if( Editor_Catalog_PickRow(ui, event->key - GALLERY_STAR_KEY) )
            (void)Editor_Catalog_ToggleFavourite(ui);
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS )
    {
        int const row = event->key - 1;
        if( row < 0 )
            return 1;
        if( row == self->press_row && self->frame - self->press_frame <= GALLERY_DOUBLE_FRAMES )
        {
            self->press_frame = -1000;
            gallery_primary(ui, row);
            return 1;
        }
        self->press_row = row;
        self->press_frame = self->frame;
        /* The Catalog says what the next click does with the pick. */
        (void)Editor_Catalog_PickRow(ui, row);
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_MENU )
    {
        int const row = event->key >= GALLERY_BAR_KEY    ? -1
                        : event->key >= GALLERY_STAR_KEY ? event->key - GALLERY_STAR_KEY
                                                         : event->key - 1;
        if( row >= 0 && Editor_Catalog_PickRow(ui, row) && Editor_UiFindPanel(ui, "preview") )
            Editor_UiPanelOpenBeside(ui, "preview", "gallery");
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER )
    {
        /* On a star the tile under it is still the hovered one. */
        self->hover_star = event->key >= GALLERY_STAR_KEY && event->key < GALLERY_BAR_KEY;
        self->hover_row = event->key >= GALLERY_BAR_KEY ? -1
                          : self->hover_star            ? event->key - GALLERY_STAR_KEY
                          : event->key > 0              ? event->key - 1
                                                        : -1;
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_WHEEL )
    {
        self->first_row += event->value;
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_SCROLL )
    {
        int const step = (self->cell_h + GALLERY_GAP) * 256;
        self->scroll_q8 -= event->dy;
        while( self->scroll_q8 >= step )
        {
            self->scroll_q8 -= step;
            self->first_row++;
        }
        while( self->scroll_q8 <= -step )
        {
            self->scroll_q8 += step;
            self->first_row--;
        }
        return 1;
    }
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_KEY )
    {
        int const key = -event->value;
        if( key == TORIRS_CHROME_KEY_DOWN )
            self->first_row++;
        else if( key == TORIRS_CHROME_KEY_UP )
            self->first_row--;
        else if( key == TORIRS_CHROME_KEY_HOME )
            self->first_row = 0;
        else if( key == TORIRS_CHROME_KEY_END )
            self->first_row = 1 << 20; /* clamped to the last row at the next tick */
        return 1;
    }
    return 1;
}

static int
gallery_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct GalleryPanel* self = opaque;

    (void)ui;
    return widget == self->input_find || widget == self->graph || widget == self->drop_kind || widget == self->drop_size;
}

static void
gallery_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

int
Editor_Gallery_Visible(struct EditorUi* ui)
{
    struct GalleryPanel* self;

    assert(ui);
    self = gallery_self(ui);
    return self && gallery_visible(ui, self);
}

int
Editor_Gallery_TilePx(struct EditorUi* ui)
{
    int const px = ui->prefs.gallery_tile;

    assert(ui);
    for( int i = 0; i < GALLERY_SIZES; i++ )
        if( px == k_gallery_sizes[i] )
            return px;
    return EDITOR_PREFS_GALLERY_TILE_M;
}

void
Editor_Gallery_Register(struct EditorUi* ui)
{
    struct GalleryPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->hover_row = -1;
    self->press_row = -1;
    self->press_frame = -1000;
    self->drawn_answers = 0xFFFFFFFFu;
    self->drawn_pick_kind = -2;
    self->drawn_pick_id = -2;
    self->drawn_hover = -2;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 420), edp_px(ui, GALLERY_W), "Gallery");
    assert(self->panel >= 0);
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "the Catalog's Find: name, #id, obj:/npc:/loc:, key=value");
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    for( int i = 0; i < GALLERY_KINDS; i++ )
        self->kind_options[i] =
            k_gallery_kinds[i] == TORIRSMAPED_CATALOG_ANY ? "all" : ToriRSMapEdCatalog_KindName(k_gallery_kinds[i]);
    self->kind_options[GALLERY_SHOW_FAVOURITES] = "favourites";
    self->kind_option_count = GALLERY_KINDS + 1;
    self->kind_sel = -1;
    self->size_sel = -1;
    self->drop_kind = ToriRSChrome_Dropdown(chrome, self->panel, "Show", self->kind_options, GALLERY_KINDS + 1, 0);
    ToriRSChrome_SetTooltip(chrome, self->drop_kind, "A kind, or favourites: the records you starred");
    self->drop_size = ToriRSChrome_Dropdown(chrome, self->panel, "Tiles", k_gallery_size_names, GALLERY_SIZES, 1);
    ToriRSChrome_SetTooltip(chrome, self->drop_size, "The pictures' size; this device remembers it");
    self->graph = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, GALLERY_WELL_H));
    assert(self->graph >= 0);
    /* The well pages its own tiles with the wheel (gallery_event). */
    ToriRSChrome_GraphSetWheel(chrome, self->graph, 1);
    ToriRSChrome_SetTooltip(chrome, self->graph, "click: pick; double-click: place (or put in the account's slot)");
    self->well_h = GALLERY_WELL_H;
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 0);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "gallery";
    desc.title = "Gallery";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = gallery_tick;
    desc.activate = gallery_activate;
    desc.event = gallery_event;
    desc.destroy = gallery_destroy;
    desc.stretch = gallery_stretch;
    Editor_UiAddPanel(ui, &desc);
}
