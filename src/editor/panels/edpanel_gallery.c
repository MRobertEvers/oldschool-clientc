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
 *   Chips        two lines: the kinds with pictures (all, loc, npc, obj; the
 *                Catalog's kind when it is another), the sizes, list | grid
 *                -- the Catalog's "grid" chip opens this panel beside it
 *                (Editor_UiPanelOpenBeside); this panel's "list" chip opens
 *                the Catalog beside this one -- and favourites.
 */

#include "edpanel_catalog_model.h"
#include "edpanel_chips.h"
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
/** A tile: the picture, a frame, the name line. */
#define GALLERY_TILE_PAD 4
#define GALLERY_NAME_H 14
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
/** What a chip past the kinds means (kind_of). */
#define GALLERY_CHIP_SIZE (-10)
#define GALLERY_CHIP_LIST (-20)
#define GALLERY_CHIP_GRID (-21)
#define GALLERY_CHIP_FAVOURITES (-30)
/** The sizes the strip offers, smallest first. */
#define GALLERY_SIZES 5
static const int k_gallery_sizes[GALLERY_SIZES] = { EDITOR_PREFS_GALLERY_TILE_S, EDITOR_PREFS_GALLERY_TILE_M,
    EDITOR_PREFS_GALLERY_TILE_L, EDITOR_PREFS_GALLERY_TILE_XL, EDITOR_PREFS_GALLERY_TILE_XXL };
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
    /** One line of chips: the kinds, then S M L, then list | grid; each
     *  chip's meaning by index (kind_of >= 0 a kind, GALLERY_CHIP_SIZE +
     *  0..2 a size, GALLERY_CHIP_LIST / GRID the views). */
    struct Edp_ChipStrip strip;
    int kind_of[EDP_CHIPS_MAX];
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

static void
gallery_strips_draw(
    struct EditorUi* ui,
    struct GalleryPanel* self)
{
    uint32_t const current = Editor_Catalog_Kind(ui);
    int const px = Editor_Gallery_TilePx(ui);
    static const char* const size_names[GALLERY_SIZES] = { "S", "M", "L", "XL", "XXL" };
    static const char* const size_tips[GALLERY_SIZES] = { "32 px pictures: the game's icon at its own size",
        "48 px pictures", "64 px pictures", "96 px pictures", "128 px pictures" };
    char label[EDP_CHIP_LABEL_MAX];
    int shown = 0;
    int index;

    for( int i = 0; i < EDP_CHIPS_MAX; i++ )
        self->kind_of[i] = -1;
    edp_chips_begin(&self->strip);
    for( int i = 0; i < GALLERY_KINDS; i++ )
    {
        uint32_t const kind = k_gallery_kinds[i];
        const char* name = kind == TORIRSMAPED_CATALOG_ANY ? "all" : ToriRSMapEdCatalog_KindName(kind);
        index = edp_chips_push(&self->strip, name,
            kind == TORIRSMAPED_CATALOG_ANY ? "every kind at once" : "only this kind", kind == current, 0);
        if( index >= 0 )
            self->kind_of[index] = (int)kind;
        shown |= kind == current;
    }
    if( !shown )
    {
        index = edp_chips_push(&self->strip, ToriRSMapEdCatalog_KindName(current), "the Catalog's kind", 1, 0);
        if( index >= 0 )
            self->kind_of[index] = (int)current;
    }
    for( int i = 0; i < GALLERY_SIZES; i++ )
    {
        index = edp_chips_push(&self->strip, size_names[i], size_tips[i], px == k_gallery_sizes[i], 0);
        if( index >= 0 )
            self->kind_of[index] = GALLERY_CHIP_SIZE + i;
    }
    index = edp_chips_push(&self->strip, "list", "the Catalog: the same search as a list (F8)", 0, 0);
    if( index >= 0 )
        self->kind_of[index] = GALLERY_CHIP_LIST;
    index = edp_chips_push(&self->strip, "grid", "this grid", 1, 0);
    if( index >= 0 )
        self->kind_of[index] = GALLERY_CHIP_GRID;
    /* Last, so the chips before it keep their places whatever it counts. */
    snprintf(label, sizeof(label), "favourites (%d)", self->favourites_count);
    index = edp_chips_push(&self->strip, label,
        Editor_Catalog_ShowingFavourites(ui) ? "back to the index" : "the records you starred, and only them",
        Editor_Catalog_ShowingFavourites(ui), 0);
    if( index >= 0 )
        self->kind_of[index] = GALLERY_CHIP_FAVOURITES;
    edp_chips_end(ui, &self->strip);
    edp_chips_hover(ui, &self->strip);
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

static void
gallery_draw(
    struct EditorUi* ui,
    struct GalleryPanel* self,
    const struct Editor_CatalogModel* model)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const px = Editor_Gallery_TilePx(ui);
    int const count = Editor_CatalogModelViewCount(model);
    int const chars = (self->cell_w - 2) / 6 > 3 ? (self->cell_w - 2) / 6 : 3;
    int const first = self->first_row * self->cols;

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
        if( (int)strlen(view.name) > chars )
            snprintf(name, sizeof(name), "%.*s..", chars > 2 ? chars - 2 : 1, view.name);
        else
            snprintf(name, sizeof(name), "%s", view.name);
        ToriRSChrome_GraphText(chrome, self->graph, x + 2, y + self->cell_h - 4, 0xd8d0c0, name);
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
    gallery_strips_draw(ui, self);
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
    int on_x = 0;
    int index;

    if( event->widget == self->strip.graph )
    {
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->strip, event->key, &on_x);
        if( index < 0 || index >= EDP_CHIPS_MAX )
            return 1;
        if( self->kind_of[index] >= 0 )
        {
            Editor_Catalog_SetKind(ui, (uint32_t)self->kind_of[index]);
            self->first_row = 0;
        }
        else if( self->kind_of[index] >= GALLERY_CHIP_SIZE && self->kind_of[index] < GALLERY_CHIP_SIZE + GALLERY_SIZES )
        {
            int const px = k_gallery_sizes[self->kind_of[index] - GALLERY_CHIP_SIZE];
            if( ui->prefs.gallery_tile != px )
            {
                ui->prefs.gallery_tile = px;
                ui->prefs_serial++;
            }
        }
        else if( self->kind_of[index] == GALLERY_CHIP_LIST )
            Editor_UiPanelOpenBeside(ui, "catalog", "gallery");
        else if( self->kind_of[index] == GALLERY_CHIP_FAVOURITES )
        {
            Editor_Catalog_ShowFavourites(ui);
            self->first_row = 0;
        }
        return 1;
    }
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
    return widget == self->input_find || widget == self->graph || widget == self->strip.graph;
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
    edp_chips_init(ui, &self->strip, self->panel, 2);
    self->graph = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, GALLERY_WELL_H));
    assert(self->graph >= 0);
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
