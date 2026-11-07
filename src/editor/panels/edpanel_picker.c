#include "edpanel_picker.h"

#include "editor/editor_doc.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "log/torirs_log.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void
Editor_PickerInit(struct Editor_Picker* picker)
{
    assert(picker);
    memset(picker, 0, sizeof(*picker));
    picker->zoom = 8.0;
    picker->hover = -1;
    picker->focus = -1;
    picker->storage_filter = -1;
}

static void
picker_free_atlas(struct Editor_Picker* picker)
{
    struct ToriMem_Group* group = ToriMem_Editor();

    ToriMem_Free(group, picker->squares);
    ToriMem_Free(group, picker->area_refs);
    ToriMem_Free(group, picker->areas);
    ToriMem_Free(group, picker->storages);
    ToriMem_Free(group, picker->selected);
    picker->squares = NULL;
    picker->area_refs = NULL;
    picker->areas = NULL;
    picker->storages = NULL;
    picker->selected = NULL;
    picker->square_count = 0;
    picker->area_ref_count = 0;
    picker->area_count = 0;
    picker->storage_count = 0;
}

/** Rebuild the filter's per-square mask from its keys (the atlas changed,
 *  or the keys did). */
static void
picker_build_filter(struct Editor_Picker* picker)
{
    ToriMem_Free(ToriMem_Editor(), picker->filter_mask);
    picker->filter_mask = NULL;
    picker->filter_shown = 0;
    if( !picker->filter_on )
        return;
    picker->filter_mask = ToriMem_Calloc(ToriMem_Editor(), (size_t)picker->square_count + 1, 1);
    assert(picker->filter_mask);
    for( int i = 0; i < picker->square_count; i++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        Editor_PickerSquareKey(picker, i, key);
        for( int k = 0; k < picker->filter_key_count; k++ )
            if( strcmp(key, picker->filter_keys[k]) == 0 )
            {
                picker->filter_mask[i] = 1;
                picker->filter_shown++;
                break;
            }
    }
}

int
Editor_PickerSetFilter(
    struct Editor_Picker* picker,
    const char* const* keys,
    int count)
{
    assert(picker);
    ToriMem_Free(ToriMem_Editor(), picker->filter_keys);
    picker->filter_keys = NULL;
    picker->filter_key_count = 0;
    picker->filter_on = count >= 0;
    if( count > 0 )
    {
        assert(keys);
        picker->filter_keys = ToriMem_Calloc(ToriMem_Editor(), (size_t)count, sizeof(*picker->filter_keys));
        assert(picker->filter_keys);
        for( int k = 0; k < count; k++ )
        {
            assert(keys[k]);
            snprintf(picker->filter_keys[k], sizeof(picker->filter_keys[k]), "%s", keys[k]);
        }
        picker->filter_key_count = count;
    }
    picker->hover = -1;
    picker_build_filter(picker);
    picker->atlas_serial++;
    return picker->filter_shown;
}

void
Editor_PickerFree(struct Editor_Picker* picker)
{
    if( !picker )
        return;
    ToriMem_Free(ToriMem_Editor(), picker->filter_keys);
    ToriMem_Free(ToriMem_Editor(), picker->filter_mask);
    picker->filter_keys = NULL;
    picker->filter_mask = NULL;
    picker->filter_key_count = 0;
    picker->filter_on = 0;
    picker->filter_shown = 0;
    picker_free_atlas(picker);
    ToriMem_Free(ToriMem_Editor(), picker->tabs);
    ToriMem_Free(ToriMem_Editor(), picker->tab_titles);
    picker->tabs = NULL;
    picker->tab_titles = NULL;
    picker->tab_count = 0;
}

/* ---- tabs --------------------------------------------------------------------- */

int
Editor_PickerSquareOnTab(
    const struct Editor_Picker* picker,
    int index,
    int tab)
{
    const struct Editor_PickerSquare* square;
    int area;

    assert(picker);
    assert(index >= 0);
    assert(index < picker->square_count);
    assert(tab >= 0);
    assert(tab < picker->tab_count);

    square = &picker->squares[index];
    area = picker->tabs[tab].area;
    if( area == EDITOR_PICKER_TAB_FULL )
        return 1;
    if( area == EDITOR_PICKER_TAB_INSTANCE )
        return square->primary_area < 0 && Editor_PickerInInstanceSpace(square->map_x, square->map_z);
    if( area < 0 )
        return square->primary_area < 0;
    for( int i = 0; i < square->area_count; i++ )
        if( picker->area_refs[square->area_first + i] == area )
            return 1;
    return 0;
}

int
Editor_PickerTabOfSquare(
    const struct Editor_Picker* picker,
    int index)
{
    int const primary = picker->squares[index].primary_area;

    assert(index >= 0);
    assert(index < picker->square_count);

    for( int t = 0; t < picker->tab_count; t++ )
        if( picker->tabs[t].area == primary )
            return t;
    /* A primary area with no tab of its own cannot happen (every claiming
     * area gets one); fall back to any tab that shows it. */
    for( int t = 0; t < picker->tab_count; t++ )
        if( Editor_PickerSquareOnTab(picker, index, t) )
            return t;
    return 0;
}

int
Editor_PickerInInstanceSpace(
    int map_x,
    int map_z)
{
    return ToriRSMapEdCatalog_InInstanceSpace(map_x, map_z);
}

int
Editor_PickerInstanceTab(const struct Editor_Picker* picker)
{
    assert(picker);
    for( int t = 0; t < picker->tab_count; t++ )
        if( picker->tabs[t].area == EDITOR_PICKER_TAB_INSTANCE )
            return t;
    return -1;
}

struct PickerTabSort
{
    const struct Editor_Picker* picker;
};

static const struct Editor_Picker* g_sort_picker;

static int
picker_tab_cmp(
    const void* a,
    const void* b)
{
    const struct Editor_PickerTab* ta = a;
    const struct Editor_PickerTab* tb = b;
    const struct Editor_Picker* picker = g_sort_picker;
    /* The surface first, the Full map beside it, the other areas by what the
     * world map calls them, Instance space, Unmapped last. */
    int const rank_a = ta->area == EDITOR_PICKER_TAB_FULL       ? 1
                       : ta->area == EDITOR_PICKER_TAB_INSTANCE ? 3
                       : ta->area == EDITOR_PICKER_TAB_UNMAPPED ? 4
                       : picker->areas[ta->area].is_main        ? 0
                                                                : 2;
    int const rank_b = tb->area == EDITOR_PICKER_TAB_FULL       ? 1
                       : tb->area == EDITOR_PICKER_TAB_INSTANCE ? 3
                       : tb->area == EDITOR_PICKER_TAB_UNMAPPED ? 4
                       : picker->areas[tb->area].is_main        ? 0
                                                                : 2;

    if( rank_a != rank_b )
        return rank_a < rank_b ? -1 : 1;
    return strcmp(ta->title, tb->title);
}

static int
picker_rebuild_tabs(struct Editor_Picker* picker)
{
    struct ToriMem_Group* group = ToriMem_Editor();
    struct Editor_PickerTab* tabs;
    int count = 0;
    int unmapped = 0;
    int changed;
    int previous_area = picker->tab_count > 0 && picker->active_tab < picker->tab_count
                            ? picker->tabs[picker->active_tab].area
                            : -100;
    int* claimed = ToriMem_Calloc(group, (size_t)(picker->area_count + 1), sizeof(int));

    assert(claimed);
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        if( square->primary_area < 0 )
            unmapped = 1;
        for( int k = 0; k < square->area_count; k++ )
            claimed[picker->area_refs[square->area_first + k]] = 1;
    }

    tabs = ToriMem_Calloc(group, (size_t)(picker->area_count + 3), sizeof(*tabs));
    assert(tabs);
    for( int a = 0; a < picker->area_count; a++ )
    {
        const char* title;
        if( !claimed[a] )
            continue;
        title = picker->areas[a].display_name[0] ? picker->areas[a].display_name
                                                  : picker->areas[a].internal_name;
        tabs[count].area = a;
        snprintf(tabs[count].title, sizeof(tabs[count].title), "%s", title);
        count++;
    }
    if( unmapped || count == 0 )
    {
        tabs[count].area = EDITOR_PICKER_TAB_UNMAPPED;
        snprintf(tabs[count].title, sizeof(tabs[count].title), "Unmapped");
        count++;
    }
    if( picker->square_count > 0 )
    {
        tabs[count].area = EDITOR_PICKER_TAB_FULL;
        snprintf(tabs[count].title, sizeof(tabs[count].title), "Full map");
        count++;
    }
    /* Always: an empty instance space is where the instance helper and New
     * square make one. */
    tabs[count].area = EDITOR_PICKER_TAB_INSTANCE;
    snprintf(tabs[count].title, sizeof(tabs[count].title), "Instance space");
    count++;
    ToriMem_Free(group, claimed);

    g_sort_picker = picker;
    qsort(tabs, (size_t)count, sizeof(tabs[0]), picker_tab_cmp);
    g_sort_picker = NULL;

    changed = count != picker->tab_count;
    for( int t = 0; !changed && t < count; t++ )
        changed = tabs[t].area != picker->tabs[t].area
                  || strcmp(tabs[t].title, picker->tabs[t].title) != 0;

    /* Bounds and counts, per tab. */
    for( int t = 0; t < count; t++ )
    {
        tabs[t].min_x = tabs[t].min_z = 1 << 30;
        tabs[t].max_x = tabs[t].max_z = -(1 << 30);
    }
    ToriMem_Free(group, picker->tabs);
    picker->tabs = tabs;
    picker->tab_count = count;
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        for( int t = 0; t < count; t++ )
        {
            if( !Editor_PickerSquareOnTab(picker, i, t) )
                continue;
            tabs[t].count++;
            if( square->map_x < tabs[t].min_x )
                tabs[t].min_x = square->map_x;
            if( square->map_x > tabs[t].max_x )
                tabs[t].max_x = square->map_x;
            if( square->map_z < tabs[t].min_z )
                tabs[t].min_z = square->map_z;
            if( square->map_z > tabs[t].max_z )
                tabs[t].max_z = square->map_z;
        }
    }
    for( int t = 0; t < count; t++ )
        if( tabs[t].area == EDITOR_PICKER_TAB_INSTANCE )
        {
            /* The whole space, whatever is in it: Fit frames all of it. */
            tabs[t].min_x = TORIRSMAPED_INSTANCE_X0;
            tabs[t].max_x = TORIRSMAPED_INSTANCE_X1;
            tabs[t].min_z = TORIRSMAPED_INSTANCE_Z0;
            tabs[t].max_z = TORIRSMAPED_INSTANCE_Z1;
        }
        else if( tabs[t].count == 0 )
        {
            tabs[t].min_x = tabs[t].max_x = 50;
            tabs[t].min_z = tabs[t].max_z = 50;
        }

    if( changed )
    {
        /* A TABSTRIP borrows these; the caller re-points it before this
         * array is freed (the return value says so). */
        const char** titles = ToriMem_Calloc(group, (size_t)count, sizeof(*titles));
        assert(titles);
        for( int t = 0; t < count; t++ )
            titles[t] = picker->tabs[t].title;
        ToriMem_Free(group, picker->tab_titles);
        picker->tab_titles = titles;
    }
    else
    {
        for( int t = 0; t < count; t++ )
            picker->tab_titles[t] = picker->tabs[t].title;
    }

    picker->active_tab = 0;
    for( int t = 0; t < count; t++ )
        if( picker->tabs[t].area == previous_area )
            picker->active_tab = t;
    return changed;
}

int
Editor_PickerSetAtlas(
    struct Editor_Picker* picker,
    const struct EditorHost_CatalogSquares* atlas)
{
    struct ToriMem_Group* group = ToriMem_Editor();
    char focus_key[EDITOR_DOC_KEY_MAX] = { 0 };
    int had_focus;
    int refs = 0;

    assert(picker);
    assert(atlas);
    assert(atlas->square_count >= 0);
    assert(atlas->area_count >= 0);

    had_focus = picker->focus >= 0 && picker->focus < picker->square_count;
    if( had_focus )
        Editor_PickerSquareKey(picker, picker->focus, focus_key);

    /* The selection by key, so it survives the reorder a refresh may bring. */
    {
        int const selected_n = Editor_PickerSelectedCount(picker);
        char (*selected_keys)[EDITOR_DOC_KEY_MAX] = NULL;
        int kept = 0;

        if( selected_n > 0 )
        {
            selected_keys = ToriMem_Calloc(group, (size_t)selected_n, sizeof(*selected_keys));
            assert(selected_keys);
            for( int i = 0; i < picker->square_count; i++ )
                if( picker->selected[i] )
                    Editor_PickerSquareKey(picker, i, selected_keys[kept++]);
        }

        picker_free_atlas(picker);

        picker->storage_count = atlas->storage_count;
        picker->storages =
            ToriMem_Calloc(group, (size_t)(atlas->storage_count + 1), sizeof(*picker->storages));
        assert(picker->storages);
        for( int i = 0; i < atlas->storage_count; i++ )
        {
            assert(atlas->storages[i]);
            snprintf(picker->storages[i], sizeof(picker->storages[i]), "%s", atlas->storages[i]);
        }

        picker->area_count = atlas->area_count;
        picker->areas =
            ToriMem_Calloc(group, (size_t)(atlas->area_count + 1), sizeof(*picker->areas));
        assert(picker->areas);
        for( int i = 0; i < atlas->area_count; i++ )
        {
            const struct EditorHost_CatalogArea* from = &atlas->areas[i];
            snprintf(
                picker->areas[i].internal_name, sizeof(picker->areas[i].internal_name), "%s",
                from->internal_name ? from->internal_name : "");
            snprintf(
                picker->areas[i].display_name, sizeof(picker->areas[i].display_name), "%s",
                from->display_name ? from->display_name : "");
            picker->areas[i].is_main = from->is_main;
        }

        for( int i = 0; i < atlas->square_count; i++ )
            refs += atlas->squares[i].area_count;
        picker->square_count = atlas->square_count;
        picker->squares =
            ToriMem_Calloc(group, (size_t)(atlas->square_count + 1), sizeof(*picker->squares));
        picker->area_refs = ToriMem_Calloc(group, (size_t)(refs + 1), sizeof(int));
        picker->selected = ToriMem_Calloc(group, (size_t)(atlas->square_count + 1), 1);
        assert(picker->squares);
        assert(picker->area_refs);
        assert(picker->selected);
        picker->area_ref_count = 0;
        picker->square_count = 0;
        for( int i = 0; i < atlas->square_count; i++ )
        {
            const struct EditorHost_CatalogSquare* from = &atlas->squares[i];
            struct Editor_PickerSquare* to = &picker->squares[picker->square_count];

            /* A scratch square has no place on the map until it is placed
             * (EDITOR_DOC_SCRATCH_STORAGE); its storage stays listed, so an
             * Open naming it by index still resolves. */
            if( from->storage >= 0 && from->storage < atlas->storage_count
                && Editor_DocStorageIsScratch(picker->storages[from->storage]) )
                continue;
            picker->square_count++;
            to->map_x = from->map_x;
            to->map_z = from->map_z;
            to->storage =
                (from->storage >= 0 && from->storage < atlas->storage_count) ? from->storage : 0;
            to->flags = from->flags;
            to->spawn_count = from->spawn_count;
            to->loc_count = from->loc_count;
            to->rgb = from->rgb;
            to->primary_area =
                (from->primary_area >= 0 && from->primary_area < atlas->area_count)
                    ? from->primary_area
                    : -1;
            to->area_first = picker->area_ref_count;
            to->area_count = 0;
            for( int k = 0; k < from->area_count; k++ )
            {
                int const area = from->areas[k];
                if( area < 0 || area >= atlas->area_count )
                    continue;
                picker->area_refs[picker->area_ref_count++] = area;
                to->area_count++;
            }
            /* An unmapped square claims nothing; a primary area always claims. */
            if( to->primary_area >= 0 && to->area_count == 0 )
            {
                picker->area_refs[picker->area_ref_count++] = to->primary_area;
                to->area_count = 1;
            }
        }

        for( int k = 0; k < kept; k++ )
        {
            for( int i = 0; i < picker->square_count; i++ )
            {
                char key[EDITOR_DOC_KEY_MAX];
                Editor_PickerSquareKey(picker, i, key);
                if( strcmp(key, selected_keys[k]) == 0 )
                {
                    picker->selected[i] = 1;
                    break;
                }
            }
        }
        ToriMem_Free(group, selected_keys);
    }

    picker->focus = -1;
    if( had_focus )
        for( int i = 0; i < picker->square_count; i++ )
        {
            char key[EDITOR_DOC_KEY_MAX];
            Editor_PickerSquareKey(picker, i, key);
            if( strcmp(key, focus_key) == 0 )
                picker->focus = i;
        }
    picker->hover = -1;
    if( picker->storage_filter >= picker->storage_count )
        picker->storage_filter = -1;
    picker_build_filter(picker);
    picker->atlas_serial++;
    return picker_rebuild_tabs(picker);
}

/* ---- lookups ---------------------------------------------------------------------- */

int
Editor_PickerStorageIndex(
    const struct Editor_Picker* picker,
    const char* name)
{
    assert(picker);
    assert(name);

    for( int i = 0; i < picker->storage_count; i++ )
    {
        const char* s = picker->storages[i];
        int const is_maps = s[0] == '\0' || strcmp(s, "maps") == 0;
        if( (name[0] == '\0' || strcmp(name, "maps") == 0) ? is_maps : strcmp(s, name) == 0 )
            return i;
    }
    return -1;
}

static int
picker_storage_is_maps(
    const struct Editor_Picker* picker,
    int storage)
{
    const char* s;
    if( storage < 0 || storage >= picker->storage_count )
        return 1;
    s = picker->storages[storage];
    return s[0] == '\0' || strcmp(s, "maps") == 0;
}

int
Editor_PickerFind(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int storage)
{
    int found = -1;

    assert(picker);

    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        if( square->map_x != map_x || square->map_z != map_z )
            continue;
        if( storage >= 0 )
        {
            if( square->storage == storage )
                return i;
            continue;
        }
        if( picker_storage_is_maps(picker, square->storage) )
            return i;
        if( found < 0 )
            found = i;
    }
    return found;
}

void
Editor_PickerSquareKey(
    const struct Editor_Picker* picker,
    int index,
    char out[EDITOR_DOC_KEY_MAX])
{
    const struct Editor_PickerSquare* square;

    assert(picker);
    assert(out);
    assert(index >= 0);
    assert(index < picker->square_count);

    square = &picker->squares[index];
    if( picker_storage_is_maps(picker, square->storage) )
        snprintf(out, EDITOR_DOC_KEY_MAX, "m%d_%d", square->map_x, square->map_z);
    else
        snprintf(
            out, EDITOR_DOC_KEY_MAX, "%s/m%d_%d", picker->storages[square->storage], square->map_x,
            square->map_z);
}

void
Editor_PickerNoteDocument(
    struct Editor_Picker* picker,
    struct Editor_Doc* doc,
    const struct EditorUiViewFrame* self_frame)
{
    assert(picker);
    assert(doc);

    for( int i = 0; i < picker->square_count; i++ )
    {
        struct Editor_PickerSquare* square = &picker->squares[i];
        struct Editor_Square* open = Editor_DocFindSquare(doc, square->map_x, square->map_z);
        const char* storage;

        square->flags &= ~(uint32_t)(EDITOR_PICKER_OPEN_HERE | EDITOR_PICKER_DIRTY_HERE);
        if( !open || !open->loaded )
            continue;
        storage = Editor_DocSquareStorage(doc, open);
        if( Editor_PickerStorageIndex(picker, storage ? storage : "") != square->storage )
            continue;
        /* The mirror keeps every square it ever opened; "open here" is the
         * ones in this window's world now, so the mark goes when the world
         * moves on. Unsaved work is marked wherever it is. */
        if( self_frame && self_frame->used && square->map_x >= self_frame->map_x
            && square->map_x < self_frame->map_x + self_frame->w && square->map_z >= self_frame->map_z
            && square->map_z < self_frame->map_z + self_frame->h )
            square->flags |= EDITOR_PICKER_OPEN_HERE;
        if( open->dirty_map || open->dirty_loc )
            square->flags |= EDITOR_PICKER_DIRTY_HERE;
    }
}

/* ---- the view ------------------------------------------------------------------------ */

static int
picker_avail_w(const struct Editor_Picker* picker)
{
    int const w = picker->well_w - EDITOR_PICKER_LEFT - 2;
    return w > 8 ? w : 8;
}

static int
picker_avail_h(const struct Editor_Picker* picker)
{
    int const h = picker->well_h - EDITOR_PICKER_TOP - 2;
    return h > 8 ? h : 8;
}

/* The global pixel space at the zoom (game/rs_worldmap_overview.h): square
 * column m starts at floor(m * 64 * k), square row r (from the world's north
 * edge, r = 255 - map_z) at floor(r * 64 * k), k = zoom / 64. The map's
 * pictures are cut from the same space, so the grid and the map agree to the
 * pixel at every zoom. */

#define picker_edge Editor_PickerEdge

/** The global pixel at the well's grid corner (LEFT, TOP). */
static int
picker_ox(const struct Editor_Picker* picker)
{
    return (int)floor(picker->origin_x * picker->zoom + 0.5);
}

static int
picker_oy(const struct Editor_Picker* picker)
{
    return (int)floor(((double)EDITOR_PICKER_WORLD_SQUARES - picker->origin_z) * picker->zoom + 0.5);
}

void
Editor_PickerGlobalOrigin(
    const struct Editor_Picker* picker,
    int* out_x,
    int* out_y)
{
    assert(picker);
    assert(out_x);
    assert(out_y);
    *out_x = picker_ox(picker) - EDITOR_PICKER_LEFT;
    *out_y = picker_oy(picker) - EDITOR_PICKER_TOP;
}

double
Editor_PickerZoomMax(const struct Editor_Picker* picker)
{
    double one;

    assert(picker);
    /* One square filling the well, and never less than the CS2 map's
     * closest look at a small well (8 px a tile is 512 device px a square). */
    one = (double)(picker_avail_w(picker) < picker_avail_h(picker) ? picker_avail_w(picker)
                                                                     : picker_avail_h(picker));
    return one > EDITOR_PICKER_ZOOM_CLOSEST ? one : EDITOR_PICKER_ZOOM_CLOSEST;
}

/** The zoom that fits squares [min, max] x [min, max] into the well. */
static double
picker_fit_zoom(
    const struct Editor_Picker* picker,
    int min_x,
    int max_x,
    int min_z,
    int max_z)
{
    double const zx = (double)picker_avail_w(picker) / (double)(max_x - min_x + 1);
    double const zz = (double)picker_avail_h(picker) / (double)(max_z - min_z + 1);
    return zx < zz ? zx : zz;
}

double
Editor_PickerZoomMin(const struct Editor_Picker* picker)
{
    double fit;

    assert(picker);
    if( picker->tab_count == 0 )
        return EDITOR_PICKER_ZOOM_FLOOR;
    {
        const struct Editor_PickerTab* tab = &picker->tabs[picker->active_tab];
        fit = picker_fit_zoom(picker, tab->min_x, tab->max_x, tab->min_z, tab->max_z);
    }
    /* The whole page fits; a small page may still be backed out of to a
     * few px a square to see what is around it. */
    if( fit > EDITOR_PICKER_ZOOM_OUT_TO )
        fit = EDITOR_PICKER_ZOOM_OUT_TO;
    return fit < EDITOR_PICKER_ZOOM_FLOOR ? EDITOR_PICKER_ZOOM_FLOOR : fit;
}

static double
picker_clamp_zoom(
    const struct Editor_Picker* picker,
    double zoom)
{
    double const lo = Editor_PickerZoomMin(picker);
    double const hi = Editor_PickerZoomMax(picker);
    if( zoom < lo )
        zoom = lo;
    if( zoom > hi )
        zoom = hi;
    return zoom;
}

void
Editor_PickerCentre(
    struct Editor_Picker* picker,
    int map_x,
    int map_z)
{
    assert(picker);
    assert(picker->zoom > 0);

    picker->origin_x = (double)map_x + 0.5 - (double)picker_avail_w(picker) / (2.0 * picker->zoom);
    picker->origin_z = (double)map_z + 0.5 + (double)picker_avail_h(picker) / (2.0 * picker->zoom);
}

void
Editor_PickerFit(
    struct Editor_Picker* picker,
    int well_w,
    int well_h)
{
    const struct Editor_PickerTab* tab;
    struct Editor_PickerTab bounds;

    assert(picker);
    if( well_w > 0 )
        picker->well_w = well_w;
    if( well_h > 0 )
        picker->well_h = well_h;
    if( picker->tab_count == 0 )
        return;
    tab = &picker->tabs[picker->active_tab];
    if( picker->filter_on && picker->filter_shown > 0 )
    {
        /* Frame the collection's squares, not the page's. */
        memset(&bounds, 0, sizeof(bounds));
        bounds.min_x = bounds.min_z = 1 << 30;
        bounds.max_x = bounds.max_z = -(1 << 30);
        for( int i = 0; i < picker->square_count; i++ )
        {
            const struct Editor_PickerSquare* square = &picker->squares[i];
            if( !picker->filter_mask[i] )
                continue;
            bounds.min_x = square->map_x < bounds.min_x ? square->map_x : bounds.min_x;
            bounds.max_x = square->map_x > bounds.max_x ? square->map_x : bounds.max_x;
            bounds.min_z = square->map_z < bounds.min_z ? square->map_z : bounds.min_z;
            bounds.max_z = square->map_z > bounds.max_z ? square->map_z : bounds.max_z;
        }
        tab = &bounds;
    }
    /* Exactly the squares, edge to edge in the narrower direction. */
    picker->zoom = picker_fit_zoom(picker, tab->min_x, tab->max_x, tab->min_z, tab->max_z);
    if( picker->zoom > Editor_PickerZoomMax(picker) )
        picker->zoom = Editor_PickerZoomMax(picker);
    if( picker->zoom < EDITOR_PICKER_ZOOM_FLOOR )
        picker->zoom = EDITOR_PICKER_ZOOM_FLOOR;
    picker->origin_x = (tab->min_x + tab->max_x + 1) / 2.0 - (double)picker_avail_w(picker) / (2.0 * picker->zoom);
    picker->origin_z = (tab->min_z + tab->max_z + 1) / 2.0 + (double)picker_avail_h(picker) / (2.0 * picker->zoom);
}

void
Editor_PickerSetTab(
    struct Editor_Picker* picker,
    int tab)
{
    assert(picker);
    assert(tab >= 0);
    assert(tab < picker->tab_count);

    picker->active_tab = tab;
    picker->hover = -1;
    Editor_PickerFit(picker, 0, 0);
}

void
Editor_PickerZoomTo(
    struct Editor_Picker* picker,
    double zoom,
    int x,
    int y)
{
    double map_x;
    double map_z;

    assert(picker);
    assert(picker->zoom > 0);
    assert(zoom > 0);

    /* The point under (x, y) stays under it. */
    map_x = picker->origin_x + (double)(x - EDITOR_PICKER_LEFT) / picker->zoom;
    map_z = picker->origin_z - (double)(y - EDITOR_PICKER_TOP) / picker->zoom;
    picker->zoom = picker_clamp_zoom(picker, zoom);
    picker->origin_x = map_x - (double)(x - EDITOR_PICKER_LEFT) / picker->zoom;
    picker->origin_z = map_z + (double)(y - EDITOR_PICKER_TOP) / picker->zoom;
}

void
Editor_PickerZoom(
    struct Editor_Picker* picker,
    int delta,
    int x,
    int y)
{
    assert(picker);
    if( delta == 0 )
        return;
    /* Continuous, a fixed ratio a notch: the same feel at every zoom. */
    Editor_PickerZoomTo(picker, picker->zoom * pow(EDITOR_PICKER_ZOOM_STEP, (double)-delta), x, y);
}

void
Editor_PickerPan(
    struct Editor_Picker* picker,
    int dx,
    int dy)
{
    assert(picker);
    assert(picker->zoom > 0);

    picker->origin_x -= (double)dx / picker->zoom;
    picker->origin_z += (double)dy / picker->zoom;
}

void
Editor_PickerPanBy(
    struct Editor_Picker* picker,
    double dx,
    double dy)
{
    assert(picker);
    assert(picker->zoom > 0);

    picker->origin_x -= dx / picker->zoom;
    picker->origin_z += dy / picker->zoom;
}

#define picker_square_at Editor_PickerSquareOfPixel

int
Editor_PickerCellAt(
    const struct Editor_Picker* picker,
    int x,
    int y,
    int* out_map_x,
    int* out_map_z)
{
    int const gx = x - EDITOR_PICKER_LEFT;
    int const gy = y - EDITOR_PICKER_TOP;

    assert(picker);
    assert(out_map_x);
    assert(out_map_z);

    if( gx < 0 || gy < 0 || picker->zoom <= 0 )
        return 0;
    if( picker->well_w > 0 && (x >= picker->well_w || y >= picker->well_h) )
        return 0;
    *out_map_x = picker_square_at(picker->zoom, gx + picker_ox(picker));
    *out_map_z = EDITOR_PICKER_WORLD_SQUARES - 1 - picker_square_at(picker->zoom, gy + picker_oy(picker));
    return *out_map_x >= 0 && *out_map_z >= 0;
}

static int
picker_visible(
    const struct Editor_Picker* picker,
    int index)
{
    if( picker->tab_count == 0 )
        return 0;
    if( picker->storage_filter >= 0 && picker->squares[index].storage != picker->storage_filter )
        return 0;
    /* A collection is its own page: its squares show whatever their area. */
    if( picker->filter_on )
        return picker->filter_mask && picker->filter_mask[index];
    return Editor_PickerSquareOnTab(picker, index, picker->active_tab);
}

int
Editor_PickerSquareVisible(
    const struct Editor_Picker* picker,
    int index)
{
    assert(picker);
    assert(index >= 0);
    assert(index < picker->square_count);
    return picker_visible(picker, index);
}

int
Editor_PickerVisibleSquares(
    const struct Editor_Picker* picker,
    int* out_x0,
    int* out_z0,
    int* out_w,
    int* out_h)
{
    int x0;
    int x1;
    int z0;
    int z1;

    assert(picker);
    assert(out_x0);
    assert(out_z0);
    assert(out_w);
    assert(out_h);

    if( picker->zoom <= 0 || picker->well_w <= EDITOR_PICKER_LEFT || picker->well_h <= EDITOR_PICKER_TOP )
        return 0;
    /* A square is in the well when its rect (picker_cell_rect) overlaps the
     * grid area [LEFT, well_w) x [TOP, well_h). */
    x0 = (int)floor(picker->origin_x);
    x1 = (int)ceil(picker->origin_x + (double)(picker->well_w - EDITOR_PICKER_LEFT) / picker->zoom);
    z1 = (int)ceil(picker->origin_z);
    z0 = (int)floor(picker->origin_z - (double)(picker->well_h - EDITOR_PICKER_TOP) / picker->zoom);
    if( x0 < 0 )
        x0 = 0;
    if( z0 < 0 )
        z0 = 0;
    if( x1 <= x0 || z1 <= z0 )
        return 0;
    *out_x0 = x0;
    *out_z0 = z0;
    *out_w = x1 - x0;
    *out_h = z1 - z0;
    return 1;
}

int
Editor_PickerSquareAt(
    const struct Editor_Picker* picker,
    int x,
    int y)
{
    int map_x;
    int map_z;
    int found = -1;

    assert(picker);

    if( !Editor_PickerCellAt(picker, x, y, &map_x, &map_z) )
        return -1;
    /* Topmost wins, as drawn: the content tree's copy is drawn over a lane's
     * (the Storage filter reaches the lane's). */
    for( int i = 0; i < picker->square_count; i++ )
        if( picker->squares[i].map_x == map_x && picker->squares[i].map_z == map_z
            && picker_visible(picker, i) )
        {
            if( picker_storage_is_maps(picker, picker->squares[i].storage) )
                return i;
            found = i;
        }
    return found;
}

int
Editor_PickerSquareOfKey(
    const struct Editor_Picker* picker,
    int key)
{
    assert(picker);
    if( key <= 0 || key > picker->square_count )
        return -1;
    return key - 1;
}

int
Editor_PickerHover(
    struct Editor_Picker* picker,
    int x,
    int y)
{
    int const before = picker->hover;
    int const before_valid = picker->hover_valid;
    int const before_x = picker->hover_map_x;
    int const before_z = picker->hover_map_z;

    assert(picker);

    picker->hover = Editor_PickerSquareAt(picker, x, y);
    picker->hover_valid =
        Editor_PickerCellAt(picker, x, y, &picker->hover_map_x, &picker->hover_map_z);
    return before != picker->hover || before_valid != picker->hover_valid
           || before_x != picker->hover_map_x || before_z != picker->hover_map_z;
}

void
Editor_PickerSelect(
    struct Editor_Picker* picker,
    int index,
    int add)
{
    assert(picker);
    assert(index >= -1);
    assert(index < picker->square_count);

    if( !add )
        memset(picker->selected, 0, (size_t)picker->square_count);
    if( index >= 0 )
    {
        picker->selected[index] = add ? !picker->selected[index] : 1;
        picker->focus = index;
    }
    else if( !add )
        picker->focus = -1;
    picker->empty_valid = 0;
}

int
Editor_PickerSelectedCount(const struct Editor_Picker* picker)
{
    int count = 0;

    assert(picker);
    for( int i = 0; i < picker->square_count; i++ )
        count += picker->selected[i] ? 1 : 0;
    return count;
}

/* ---- search -------------------------------------------------------------------------- */

static int
picker_icontains(
    const char* haystack,
    const char* needle)
{
    size_t const n = strlen(needle);

    if( n == 0 )
        return 0;
    for( const char* at = haystack; *at; at++ )
    {
        size_t k = 0;
        while( k < n && at[k] && tolower((unsigned char)at[k]) == tolower((unsigned char)needle[k]) )
            k++;
        if( k == n )
            return 1;
    }
    return 0;
}

static int
picker_iequals(
    const char* a,
    const char* b)
{
    while( *a && *b )
    {
        if( tolower((unsigned char)*a) != tolower((unsigned char)*b) )
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/** Up to three non-negative integers separated by spaces or commas, and
 *  nothing else. Returns how many. */
static int
picker_numbers(
    const char* text,
    int out[3])
{
    int count = 0;
    const char* at = text;

    for( ;; )
    {
        long value = 0;
        int digits = 0;

        while( *at == ' ' || *at == ',' || *at == '\t' )
            at++;
        if( *at == '\0' )
            break;
        while( *at >= '0' && *at <= '9' )
        {
            value = value * 10 + (*at - '0');
            if( value > 100000 )
                return 0;
            at++;
            digits++;
        }
        if( digits == 0 || count == 3 )
            return 0;
        if( *at != '\0' && *at != ' ' && *at != ',' && *at != '\t' )
            return 0;
        out[count++] = (int)value;
    }
    return count;
}

int
Editor_PickerSearch(
    const struct Editor_Picker* picker,
    const char* text,
    struct Editor_PickerSearch* out)
{
    char trimmed[EDITOR_PICKER_NAME_MAX];
    int numbers[3];
    int n;
    int map_x;
    int map_z;
    size_t length;

    assert(picker);
    assert(text);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->square = -1;
    out->tab = picker->active_tab;

    while( *text == ' ' || *text == '\t' )
        text++;
    snprintf(trimmed, sizeof(trimmed), "%s", text);
    length = strlen(trimmed);
    while( length > 0 && (trimmed[length - 1] == ' ' || trimmed[length - 1] == '\t') )
        trimmed[--length] = '\0';
    if( length == 0 )
        return 0;

    if( (trimmed[0] == 'm' || trimmed[0] == 'M') && trimmed[1] >= '0' && trimmed[1] <= '9' )
    {
        trimmed[0] = 'm';
        if( !Editor_SquareKeyParse(trimmed, &map_x, &map_z) )
            return 0;
        out->kind = EDITOR_PICKER_SEARCH_SQUARE;
    }
    else if( (n = picker_numbers(trimmed, numbers)) >= 2 )
    {
        /* Two small numbers are a square, anything at tile scale is a tile:
         * no square coordinate reaches 256, no surface tile is below it. */
        if( n == 2 && numbers[0] < 256 && numbers[1] < 256 )
        {
            map_x = numbers[0];
            map_z = numbers[1];
            out->kind = EDITOR_PICKER_SEARCH_SQUARE;
        }
        else
        {
            if( n == 3 && numbers[2] > 3 )
                return 0;
            out->kind = EDITOR_PICKER_SEARCH_TILE;
            out->tile_x = numbers[0];
            out->tile_z = numbers[1];
            out->level = n == 3 ? numbers[2] : 0;
            map_x = numbers[0] / EDITOR_SQUARE_X;
            map_z = numbers[1] / EDITOR_SQUARE_Z;
        }
    }
    else
    {
        int best = -1;
        /* An exact name first, then the first that contains the text. */
        for( int t = 0; t < picker->tab_count && best < 0; t++ )
        {
            int const a = picker->tabs[t].area;
            if( picker_iequals(picker->tabs[t].title, trimmed)
                || (a >= 0 && picker_iequals(picker->areas[a].internal_name, trimmed)) )
                best = t;
        }
        for( int t = 0; t < picker->tab_count && best < 0; t++ )
        {
            int const a = picker->tabs[t].area;
            if( picker_icontains(picker->tabs[t].title, trimmed)
                || (a >= 0 && picker_icontains(picker->areas[a].internal_name, trimmed)) )
                best = t;
        }
        if( best < 0 )
            return 0;
        out->kind = EDITOR_PICKER_SEARCH_AREA;
        out->tab = best;
        return 1;
    }

    out->map_x = map_x;
    out->map_z = map_z;
    out->square = Editor_PickerFind(picker, map_x, map_z, -1);
    if( out->square >= 0 )
    {
        /* Stay on the current tab when it shows the square: searching from
         * a dungeon tab for a square that dungeon claims should not jump. */
        if( picker->tab_count == 0
            || !Editor_PickerSquareOnTab(picker, out->square, picker->active_tab) )
            out->tab = Editor_PickerTabOfSquare(picker, out->square);
    }
    return 1;
}

void
Editor_PickerShow(
    struct Editor_Picker* picker,
    const struct Editor_PickerSearch* result)
{
    assert(picker);
    assert(result);

    if( result->kind == EDITOR_PICKER_SEARCH_NONE || picker->tab_count == 0 )
        return;
    if( result->kind == EDITOR_PICKER_SEARCH_AREA )
    {
        Editor_PickerSetTab(picker, result->tab);
        return;
    }
    if( result->tab != picker->active_tab )
    {
        picker->active_tab = result->tab;
        picker->hover = -1;
    }
    if( picker->zoom < EDITOR_PICKER_ZOOM_SHOW )
        picker->zoom = picker_clamp_zoom(picker, EDITOR_PICKER_ZOOM_SHOW);
    Editor_PickerCentre(picker, result->map_x, result->map_z);
    if( result->square >= 0 )
        Editor_PickerSelect(picker, result->square, 0);
    else
    {
        Editor_PickerSelect(picker, -1, 0);
        picker->empty_valid = 1;
        picker->empty_map_x = result->map_x;
        picker->empty_map_z = result->map_z;
    }
}

/* ---- drawing -------------------------------------------------------------------------- */

void
Editor_PickerDefaultColors(struct Editor_PickerColors* colors)
{
    assert(colors);
    colors->background = 0x1a1712;
    colors->grid = 0x2c2820;
    colors->empty = 0x4a463c;
    colors->text = 0xd8cfb0;
    colors->open = 0x4fd0ff;
    colors->dirty = 0xff981f;
    colors->selected = 0xffff00;
    colors->created = 0x6fd06f;
    colors->frame = 0x4fa8ff;
    colors->self_frame = 0xff4fd0;
    colors->lane = 0x3f6fff;
    colors->hatch = 0x3a352b;
    colors->zone = 0x302c24;
    colors->spawn = 0xf0e0a0;
}

/** Cell rectangle of map square (x, z) in well pixels: the global pixel
 *  space's edges, so a cell is floor or ceil of the zoom wide. */
static void
picker_cell_rect(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int const row = EDITOR_PICKER_WORLD_SQUARES - 1 - map_z;
    int const x0 = picker_edge(picker->zoom, map_x);
    int const y0 = picker_edge(picker->zoom, row);
    *out_x = EDITOR_PICKER_LEFT + x0 - picker_ox(picker);
    *out_y = EDITOR_PICKER_TOP + y0 - picker_oy(picker);
    *out_w = picker_edge(picker->zoom, map_x + 1) - x0;
    *out_h = picker_edge(picker->zoom, row + 1) - y0;
}

void
Editor_PickerCellOrigin(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y)
{
    int w;
    int h;

    assert(picker);
    assert(out_x);
    assert(out_y);
    picker_cell_rect(picker, map_x, map_z, out_x, out_y, &w, &h);
}

void
Editor_PickerCellBox(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    assert(picker);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    picker_cell_rect(picker, map_x, map_z, out_x, out_y, out_w, out_h);
}

/** A rectangle clipped to the grid area; 0 when nothing is left. */
static int
picker_clip(
    const struct Editor_Picker* picker,
    int* x,
    int* y,
    int* w,
    int* h)
{
    int const x0 = EDITOR_PICKER_LEFT;
    int const y0 = EDITOR_PICKER_TOP;
    int const x1 = picker->well_w;
    int const y1 = picker->well_h;
    int ax = *x;
    int ay = *y;
    int bx = *x + *w;
    int by = *y + *h;

    if( ax < x0 )
        ax = x0;
    if( ay < y0 )
        ay = y0;
    if( bx > x1 )
        bx = x1;
    if( by > y1 )
        by = y1;
    if( bx <= ax || by <= ay )
        return 0;
    *x = ax;
    *y = ay;
    *w = bx - ax;
    *h = by - ay;
    return 1;
}

int
Editor_PickerLabelEvery(
    double zoom,
    int min_px)
{
    static const int k_steps[] = { 1, 2, 5, 10, 20, 50, 100 };

    assert(zoom > 0);
    for( int i = 0; i < (int)(sizeof(k_steps) / sizeof(k_steps[0])); i++ )
        if( (double)k_steps[i] * zoom >= (double)min_px )
            return k_steps[i];
    return 100;
}

/* ---- the budget ------------------------------------------------------------------------ */

/*
 * One draw is run twice. The first run counts what the essentials cost
 * (`counting`, nothing emitted, decoration skipped); the second draws. Three
 * tiers share the budget: the user's own marks (the selection, the hover and
 * its label, the empty cell for New, the view frames) are kept room for
 * whatever else is listed before them; the rest of the essentials (cells,
 * pictures, outlines, axis labels) take what the marks leave; decoration --
 * spawn dots, loc counts, zone lines, the map's icons and names, hatching --
 * only what the first run left over. So a crowded close zoom loses dots
 * before it loses an outline, a huge well loses far cells before it loses the
 * selection, and whatever is lost is counted by kind.
 */
struct PickerPaint
{
    struct ToriRSChrome* chrome;
    int graph;
    int counting;
    /** Items emitted (or, counting, that would be). */
    int items;
    /** Of them, decoration. */
    int decor;
    /** What decoration may spend: the budget less the essentials. */
    int decor_room;
    /** The marks (counted by the first run): the room kept for them. */
    int marks;
    int marks_reserve;
    int dropped[EDITOR_PICKER_DROP_KINDS];
};

/** Room for one more essential item, leaving the marks' room; counts it,
 *  or the drop. */
static int
paint_room(
    struct PickerPaint* paint,
    enum Editor_PickerDropKind kind)
{
    if( paint->counting )
    {
        paint->items++;
        return 0;
    }
    if( paint->items + (paint->marks_reserve - paint->marks) >= EDITOR_PICKER_ITEMS_BUDGET )
    {
        paint->dropped[kind]++;
        return 0;
    }
    paint->items++;
    return 1;
}

/** Room for one more of the user's marks: the room kept for them. */
static int
paint_mark(
    struct PickerPaint* paint,
    enum Editor_PickerDropKind kind)
{
    if( paint->counting )
    {
        paint->items++;
        paint->marks++;
        return 0;
    }
    if( paint->items >= EDITOR_PICKER_ITEMS_BUDGET )
    {
        paint->dropped[kind]++;
        return 0;
    }
    paint->items++;
    paint->marks++;
    return 1;
}

/** Room for one more decoration item: never asked while counting. */
static int
paint_decor(
    struct PickerPaint* paint,
    enum Editor_PickerDropKind kind)
{
    if( paint->counting )
        return 0;
    if( paint->decor >= paint->decor_room || paint->items >= EDITOR_PICKER_ITEMS_BUDGET )
    {
        paint->dropped[kind]++;
        return 0;
    }
    paint->decor++;
    paint->items++;
    return 1;
}

const char*
Editor_PickerDropName(enum Editor_PickerDropKind kind)
{
    static const char* const k_names[EDITOR_PICKER_DROP_KINDS] = {
        "cells",         "outlines",   "pictures",     "map icons",    "map names",
        "spawn dots",    "loc counts", "zone lines",   "hatch lines",  "labels",
    };
    assert(kind >= 0);
    assert(kind < EDITOR_PICKER_DROP_KINDS);
    return k_names[kind];
}

void
Editor_PickerLodAt(
    double zoom,
    struct Editor_PickerLod* out)
{
    assert(zoom > 0);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->band = zoom < EDITOR_PICKER_LOD_PICTURE_FROM  ? EDITOR_PICKER_LOD_COLOUR
                : zoom < EDITOR_PICKER_LOD_DETAIL_FROM ? EDITOR_PICKER_LOD_PICTURE
                : zoom < EDITOR_PICKER_LOD_CLOSE_FROM  ? EDITOR_PICKER_LOD_DETAIL
                                                       : EDITOR_PICKER_LOD_CLOSE;
    out->colour_cells = out->band == EDITOR_PICKER_LOD_COLOUR;
    out->page_outline = out->band == EDITOR_PICKER_LOD_COLOUR;
    out->pictures = out->band >= EDITOR_PICKER_LOD_PICTURE;
    out->lane_inset = out->band >= EDITOR_PICKER_LOD_PICTURE;
    out->state_outlines = out->band >= EDITOR_PICKER_LOD_DETAIL;
    out->spawn_dots = out->band >= EDITOR_PICKER_LOD_DETAIL;
    out->hover_label = out->band >= EDITOR_PICKER_LOD_DETAIL;
    out->icons = out->band >= EDITOR_PICKER_LOD_CLOSE;
    out->names = out->band >= EDITOR_PICKER_LOD_CLOSE;
    out->loc_count = out->band >= EDITOR_PICKER_LOD_CLOSE;
    out->zone_grid = out->band >= EDITOR_PICKER_LOD_CLOSE;
}

/** `text` cut to `width` logical px with "..." where it had to cut, measured
 *  in the face GRAPH text draws in. 0 when not even the ellipsis fits. */
static int
picker_fit_text(
    const struct ToriRSChrome* chrome,
    const char* text,
    int width,
    char out[TORIRS_CHROME_GRAPH_TEXT_MAX])
{
    int const face = chrome->theme.font_row;
    int kept;

    snprintf(out, TORIRS_CHROME_GRAPH_TEXT_MAX, "%s", text);
    if( ToriRSChrome_MeasureTextQ(face, 4, out) <= width )
        return 1;
    for( kept = (int)strlen(out); kept > 0; kept-- )
    {
        char cut[TORIRS_CHROME_GRAPH_TEXT_MAX + 4];
        memcpy(cut, out, (size_t)kept);
        memcpy(cut + kept, "...", 4);
        if( kept + 3 < TORIRS_CHROME_GRAPH_TEXT_MAX && ToriRSChrome_MeasureTextQ(face, 4, cut) <= width )
        {
            memcpy(out, cut, (size_t)kept + 4);
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/** Whether the active page shows instance space hatched (where no square
 *  is): the pages instance space belongs to. */
static int
picker_page_hatches(const struct Editor_Picker* picker)
{
    int area;
    if( picker->tab_count == 0 || picker->filter_on )
        return 0;
    area = picker->tabs[picker->active_tab].area;
    return area == EDITOR_PICKER_TAB_INSTANCE || area == EDITOR_PICKER_TAB_UNMAPPED
           || area == EDITOR_PICKER_TAB_FULL;
}

/** The well box of the map squares [x0, x1] x [z0, z1], clipped to the grid
 *  area; 0 when none of it is in the well. */
static int
picker_squares_box(
    const struct Editor_Picker* picker,
    int x0,
    int x1,
    int z0,
    int z1,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int x;
    int y;
    int w;
    int h;
    int bx;
    int by;
    int bw;
    int bh;
    picker_cell_rect(picker, x0, z1, &x, &y, &w, &h);
    picker_cell_rect(picker, x1, z0, &bx, &by, &bw, &bh);
    w = bx + bw - x;
    h = by + bh - y;
    if( !picker_clip(picker, &x, &y, &w, &h) )
        return 0;
    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
    return 1;
}

/** Instance space, hatched: "/" lines EDITOR_PICKER_HATCH_PX apart, anchored
 *  to the global pixel space so a pan does not make them swim. The squares
 *  that exist are filled over them. */
#define EDITOR_PICKER_HATCH_PX 8

static void
paint_hatch(
    const struct Editor_Picker* picker,
    const struct Editor_PickerColors* colors,
    struct PickerPaint* paint)
{
    int x;
    int y;
    int w;
    int h;
    int gox;
    int goy;
    int c0;

    if( paint->counting || !picker_page_hatches(picker) )
        return;
    if( !picker_squares_box(picker, TORIRSMAPED_INSTANCE_X0, TORIRSMAPED_INSTANCE_X1, TORIRSMAPED_INSTANCE_Z0,
            TORIRSMAPED_INSTANCE_Z1, &x, &y, &w, &h) )
        return;
    Editor_PickerGlobalOrigin(picker, &gox, &goy);
    /* x + y = c along a line; c a multiple of the spacing in global px. */
    c0 = x + y + gox + goy;
    c0 = c0 - ((c0 % EDITOR_PICKER_HATCH_PX) + EDITOR_PICKER_HATCH_PX) % EDITOR_PICKER_HATCH_PX - gox - goy;
    for( int c = c0; c <= x + w - 1 + y + h - 1; c += EDITOR_PICKER_HATCH_PX )
    {
        int ax = c - (y + h - 1);
        int bx = c - y;
        if( ax < x )
            ax = x;
        if( bx > x + w - 1 )
            bx = x + w - 1;
        if( bx < ax )
            continue;
        if( !paint_decor(paint, EDITOR_PICKER_DROP_HATCH) )
            continue;
        ToriRSChrome_GraphLine(paint->chrome, paint->graph, ax, c - ax, bx, c - bx, colors->hatch);
    }
}

/** The 8x8 zone lines across the grid, faint: every eighth of a square that
 *  is not a square's own edge. */
static void
paint_zone_grid(
    const struct Editor_Picker* picker,
    const struct Editor_PickerColors* colors,
    struct PickerPaint* paint)
{
    int vx0;
    int vz0;
    int vw;
    int vh;
    int gox;
    int goy;
    double const k = picker->zoom / 64.0;

    if( paint->counting || !Editor_PickerVisibleSquares(picker, &vx0, &vz0, &vw, &vh) )
        return;
    Editor_PickerGlobalOrigin(picker, &gox, &goy);
    for( int m = vx0; m < vx0 + vw; m++ )
        for( int z = 1; z < 8; z++ )
        {
            int const x = (int)floor((double)(m * 64 + z * 8) * k) - gox;
            if( x < EDITOR_PICKER_LEFT || x >= picker->well_w )
                continue;
            if( !paint_decor(paint, EDITOR_PICKER_DROP_ZONE_LINES) )
                continue;
            ToriRSChrome_GraphLine(
                paint->chrome, paint->graph, x, EDITOR_PICKER_TOP, x, picker->well_h - 1, colors->zone);
        }
    for( int mz = vz0; mz < vz0 + vh; mz++ )
    {
        int const row = EDITOR_PICKER_WORLD_SQUARES - 1 - mz;
        for( int z = 1; z < 8; z++ )
        {
            int const y = (int)floor((double)(row * 64 + z * 8) * k) - goy;
            if( y < EDITOR_PICKER_TOP || y >= picker->well_h )
                continue;
            if( !paint_decor(paint, EDITOR_PICKER_DROP_ZONE_LINES) )
                continue;
            ToriRSChrome_GraphLine(
                paint->chrome, paint->graph, EDITOR_PICKER_LEFT, y, picker->well_w - 1, y, colors->zone);
        }
    }
}

/** The page's squares' bounds (a collection's when one is the page). */
static void
picker_page_bounds(
    const struct Editor_Picker* picker,
    int* min_x,
    int* max_x,
    int* min_z,
    int* max_z)
{
    const struct Editor_PickerTab* tab = &picker->tabs[picker->active_tab];
    *min_x = tab->min_x;
    *max_x = tab->max_x;
    *min_z = tab->min_z;
    *max_z = tab->max_z;
    if( !picker->filter_on || picker->filter_shown <= 0 )
        return;
    *min_x = *min_z = 1 << 30;
    *max_x = *max_z = -(1 << 30);
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        if( !picker->filter_mask[i] )
            continue;
        *min_x = square->map_x < *min_x ? square->map_x : *min_x;
        *max_x = square->map_x > *max_x ? square->map_x : *max_x;
        *min_z = square->map_z < *min_z ? square->map_z : *min_z;
        *max_z = square->map_z > *max_z ? square->map_z : *max_z;
    }
}

/** One square's decoration at the close bands: a dot per ten spawns along
 *  its top, its loc count along its foot. */
static void
paint_square_detail(
    const struct Editor_Picker* picker,
    const struct Editor_PickerSquare* square,
    const struct Editor_PickerLod* lod,
    const struct Editor_PickerColors* colors,
    int x,
    int y,
    int w,
    int h,
    struct PickerPaint* paint)
{
    if( paint->counting )
        return;
    if( lod->spawn_dots && square->spawn_count >= EDITOR_PICKER_SPAWNS_PER_DOT && w >= 12 && h >= 12 )
    {
        int const size = w >= 96 ? 3 : 2;
        int const per_row = (w - 6) / (size + 2);
        int dots = square->spawn_count / EDITOR_PICKER_SPAWNS_PER_DOT;
        if( dots > EDITOR_PICKER_DOTS_MAX )
            dots = EDITOR_PICKER_DOTS_MAX;
        for( int d = 0; d < dots && per_row > 0; d++ )
        {
            int dx = x + 3 + (d % per_row) * (size + 2);
            int dy = y + 3 + (d / per_row) * (size + 2);
            int dw = size;
            int dh = size;
            /* The cell's own room is a cap like EDITOR_PICKER_DOTS_MAX (at
             * 40 px a square it holds more than the cap). */
            if( dy + size > y + h - 3 )
                break;
            if( !picker_clip(picker, &dx, &dy, &dw, &dh) )
                continue;
            if( !paint_decor(paint, EDITOR_PICKER_DROP_DOTS) )
                continue;
            ToriRSChrome_GraphRect(paint->chrome, paint->graph, dx, dy, dw, dh, colors->spawn, 1, 0);
        }
    }
    if( lod->loc_count && square->loc_count > 0 && w >= 24 && h >= 24 )
    {
        char text[TORIRS_CHROME_GRAPH_TEXT_MAX];
        char fitted[TORIRS_CHROME_GRAPH_TEXT_MAX];
        int const baseline = y + h - 4;
        snprintf(text, sizeof(text), "%d locs", square->loc_count);
        if( baseline - 10 < EDITOR_PICKER_TOP || baseline > picker->well_h || x + 3 < EDITOR_PICKER_LEFT
            || x + 3 >= picker->well_w )
            return;
        if( !picker_fit_text(paint->chrome, text, w - 6, fitted) )
            return;
        if( !paint_decor(paint, EDITOR_PICKER_DROP_LOC_COUNTS) )
            return;
        ToriRSChrome_GraphText(paint->chrome, paint->graph, x + 3, baseline, colors->text, fitted);
    }
}

/** The hovered cell's coordinates, on a dark plate so the map cannot hide
 *  them. Ellipsised to the cell, never clipped. */
static void
paint_hover_label(
    const struct Editor_Picker* picker,
    const struct Editor_PickerColors* colors,
    struct PickerPaint* paint)
{
    char text[TORIRS_CHROME_GRAPH_TEXT_MAX];
    char fitted[TORIRS_CHROME_GRAPH_TEXT_MAX];
    int x;
    int y;
    int w;
    int h;
    int tw;

    picker_cell_rect(picker, picker->hover_map_x, picker->hover_map_z, &x, &y, &w, &h);
    if( x + 3 < EDITOR_PICKER_LEFT || y + 2 < EDITOR_PICKER_TOP || x + 3 >= picker->well_w
        || y + 15 > picker->well_h )
        return;
    snprintf(text, sizeof(text), "m%d_%d", picker->hover_map_x, picker->hover_map_z);
    if( !picker_fit_text(paint->chrome, text, w - 8, fitted) )
        return;
    tw = ToriRSChrome_MeasureTextQ(paint->chrome->theme.font_row, 4, fitted);
    if( paint_mark(paint, EDITOR_PICKER_DROP_LABELS) )
        ToriRSChrome_GraphRect(paint->chrome, paint->graph, x + 2, y + 2, tw + 4, 13, colors->background, 1, 0);
    if( paint_mark(paint, EDITOR_PICKER_DROP_LABELS) )
        ToriRSChrome_GraphText(paint->chrome, paint->graph, x + 4, y + 12, colors->text, fitted);
}

/** One run over the picture (see struct PickerPaint). */
static void
picker_paint(
    struct Editor_Picker* picker,
    const struct Editor_PickerColors* colors,
    const struct EditorUiViewFrame* frames,
    int frame_count,
    int self_slot,
    struct PickerPaint* paint)
{
    struct ToriRSChrome* chrome = paint->chrome;
    int const graph = paint->graph;
    int const well_w = picker->well_w;
    int const well_h = picker->well_h;
    int const gap = picker->zoom >= 8.0 ? 1 : 0;
    const struct Editor_PickerUnderlay* map = &picker->underlay;
    struct Editor_PickerLod lod;
    int pictures;
    int hatches;
    int counting = paint->counting;

    Editor_PickerLodAt(picker->zoom, &lod);
    /* No picture below 12 px a square, nor without the App's map (the
     * headless harness): the cells are the atlas row's colour then. */
    pictures = map->active && lod.pictures;
    hatches = picker_page_hatches(picker);

    if( paint_room(paint, EDITOR_PICKER_DROP_CELLS) )
        ToriRSChrome_GraphRect(chrome, graph, 0, 0, well_w, well_h, colors->background, 1, 0);

    /* The world map under the grid: its pictures, then what 595 draws over
     * it at this zoom (icons, names), then the label margins painted over
     * whatever of it strays into them. */
    if( pictures )
    {
        if( paint_room(paint, EDITOR_PICKER_DROP_PICTURES) )
            ToriRSChrome_GraphRect(chrome, graph, EDITOR_PICKER_LEFT, EDITOR_PICKER_TOP, well_w - EDITOR_PICKER_LEFT,
                well_h - EDITOR_PICKER_TOP, 0x000000, 1, 0);
        for( int i = 0; i < map->count; i++ )
        {
            const struct Editor_PickerImage* image = &map->items[i];
            if( paint_room(paint, EDITOR_PICKER_DROP_PICTURES) )
                ToriRSChrome_GraphImage(chrome, graph, image->x, image->y, image->w, image->h, image->scene_id,
                    image->trans, image->version);
        }
    }
    if( hatches )
        paint_hatch(picker, colors, paint);
    if( pictures && lod.zone_grid )
        paint_zone_grid(picker, colors, paint);
    if( pictures && lod.icons )
        for( int i = 0; i < map->icon_count && !counting; i++ )
        {
            const struct Editor_PickerImage* icon = &map->icons[i];
            if( paint_decor(paint, EDITOR_PICKER_DROP_ICONS) )
                ToriRSChrome_GraphImage(
                    chrome, graph, icon->x, icon->y, icon->w, icon->h, icon->scene_id, icon->trans, icon->version);
        }
    if( pictures && lod.names )
        for( int i = 0; i < map->label_count && !counting; i++ )
        {
            const struct Editor_PickerLabel* label = &map->labels[i];
            if( paint_decor(paint, EDITOR_PICKER_DROP_NAMES) )
                ToriRSChrome_GraphText(chrome, graph, label->x, label->y, label->color, label->text);
        }
    if( pictures )
    {
        if( paint_room(paint, EDITOR_PICKER_DROP_PICTURES) )
            ToriRSChrome_GraphRect(chrome, graph, 0, 0, EDITOR_PICKER_LEFT, well_h, colors->background, 1, 0);
        if( paint_room(paint, EDITOR_PICKER_DROP_PICTURES) )
            ToriRSChrome_GraphRect(chrome, graph, 0, 0, well_w, EDITOR_PICKER_TOP, colors->background, 1, 0);
    }

    /* Axis labels: map x across the top, map z down the left, thinned so
     * neighbours never touch at any zoom. */
    {
        int const every_x = Editor_PickerLabelEvery(picker->zoom, EDITOR_PICKER_LABEL_W);
        int const every_z = Editor_PickerLabelEvery(picker->zoom, EDITOR_PICKER_LABEL_H);
        int const first_x = (int)floor(picker->origin_x);
        int const last_x = (int)ceil(picker->origin_x + picker_avail_w(picker) / picker->zoom) + 1;
        int const first_z = (int)floor(picker->origin_z);
        int const last_z = (int)floor(picker->origin_z - picker_avail_h(picker) / picker->zoom) - 1;
        char text[16];

        for( int mx = first_x - (first_x % every_x + every_x) % every_x; mx <= last_x; mx += every_x )
        {
            int px;
            int py;
            int pw;
            int ph;
            if( mx < 0 )
                continue;
            picker_cell_rect(picker, mx, 0, &px, &py, &pw, &ph);
            if( px < EDITOR_PICKER_LEFT || px >= well_w - 8 )
                continue;
            if( !paint_room(paint, EDITOR_PICKER_DROP_LABELS) )
                continue;
            snprintf(text, sizeof(text), "%d", mx);
            ToriRSChrome_GraphText(chrome, graph, px, EDITOR_PICKER_TOP - 2, colors->text, text);
        }
        for( int mz = first_z - (first_z % every_z + every_z) % every_z; mz >= last_z; mz -= every_z )
        {
            int px;
            int py;
            int pw;
            int ph;
            if( mz < 0 )
                break;
            picker_cell_rect(picker, 0, mz, &px, &py, &pw, &ph);
            if( py < EDITOR_PICKER_TOP || py >= well_h - 4 )
                continue;
            if( !paint_room(paint, EDITOR_PICKER_DROP_LABELS) )
                continue;
            snprintf(text, sizeof(text), "%d", mz);
            ToriRSChrome_GraphText(chrome, graph, 1, py + (ph < 10 ? ph : 10), colors->text, text);
        }
    }

    /* Cells, then outlines over them, so an outline is never painted over by
     * a neighbour's fill. Passes 0/1 fill, 2/3 outline; the even pass takes
     * the lanes, the odd one the content tree's maps/, so maps/ is on top. */
    for( int pass = 0; pass < 4; pass++ )
    {
        int const want_maps = pass & 1;
        /* The view frames go between the fills and the outlines: a frame
         * on the selected square must not hide its selection mark. */
        if( pass == 2 )
        {
            /* Every view's window, this window's in its own colour. */
            for( int f = 0; f < frame_count; f++ )
            {
                int x;
                int y;
                int w;
                int h;
                int x1;
                int y1;
                int w1;
                int h1;
                if( !frames[f].used )
                    continue;
                picker_cell_rect(picker, frames[f].map_x, frames[f].map_z + frames[f].h - 1, &x, &y, &w, &h);
                picker_cell_rect(picker, frames[f].map_x + frames[f].w - 1, frames[f].map_z, &x1, &y1, &w1, &h1);
                w = x1 + w1 - x;
                h = y1 + h1 - y;
                if( !picker_clip(picker, &x, &y, &w, &h) )
                    continue;
                if( !paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES) )
                    continue;
                ToriRSChrome_GraphRect(
                    chrome, graph, x, y, w, h, frames[f].slot == self_slot ? colors->self_frame : colors->frame, 0, 0);
            }
        }
        for( int i = 0; i < picker->square_count; i++ )
        {
            const struct Editor_PickerSquare* square = &picker->squares[i];
            int const is_maps = picker_storage_is_maps(picker, square->storage);
            int x;
            int y;
            int w;
            int h;
            uint32_t outline = 0;
            int has_outline = 0;

            if( !picker_visible(picker, i) || is_maps != want_maps )
                continue;
            picker_cell_rect(picker, square->map_x, square->map_z, &x, &y, &w, &h);
            if( x + w <= EDITOR_PICKER_LEFT || y + h <= EDITOR_PICKER_TOP || x >= well_w || y >= well_h )
                continue;

            if( pass < 2 )
            {
                int fx = x;
                int fy = y;
                int fw = w - gap;
                int fh = h - gap;
                /* The map is the fill from 12 px a square: a square's own
                 * colour would hide it. A square in hatched instance space
                 * is filled all the same (no area maps it), over the
                 * hatching. */
                if( pictures
                    && !(hatches && Editor_PickerInInstanceSpace(square->map_x, square->map_z)) )
                    continue;
                if( fw > 0 && fh > 0 && picker_clip(picker, &fx, &fy, &fw, &fh)
                    && paint_room(paint, EDITOR_PICKER_DROP_CELLS) )
                    ToriRSChrome_GraphRect(chrome, graph, fx, fy, fw, fh, square->rgb ? square->rgb : colors->empty,
                        1, EDITOR_PICKER_KEY_SQUARE(i));
                paint_square_detail(picker, square, &lod, colors, x, y, w, h, paint);
                continue;
            }

            /* Under a picture the fill passes skipped the square: its detail
             * goes on here (each square is visited by one outline pass). */
            if( pictures && !(hatches && Editor_PickerInInstanceSpace(square->map_x, square->map_z)) )
                paint_square_detail(picker, square, &lod, colors, x, y, w, h, paint);

            /* The selection (yellow) at every zoom; from 40 px a square
             * unsaved (accent) and created (green), the whole cell. A lane's
             * square: a blue 1 px inset. The squares in this window's own
             * world (EDITOR_PICKER_OPEN_HERE): an inset in their own colour,
             * so the open set never reads as a selection. */
            if( picker->selected[i] )
            {
                outline = colors->selected;
                has_outline = 1;
            }
            else if( lod.state_outlines && (square->flags & (TORIRSMAPED_SQUARE_DIRTY | EDITOR_PICKER_DIRTY_HERE)) )
            {
                outline = colors->dirty;
                has_outline = 1;
            }
            else if( lod.state_outlines && (square->flags & TORIRSMAPED_SQUARE_CREATED) )
            {
                outline = colors->created;
                has_outline = 1;
            }
            if( lod.lane_inset && !is_maps && w >= 5 && h >= 5 )
            {
                int ix = x + 1;
                int iy = y + 1;
                int iw = w - 2;
                int ih = h - 2;
                if( picker_clip(picker, &ix, &iy, &iw, &ih) && paint_room(paint, EDITOR_PICKER_DROP_OUTLINES) )
                    ToriRSChrome_GraphRect(chrome, graph, ix, iy, iw, ih, colors->lane, 0, 0);
            }
            if( lod.state_outlines && (square->flags & EDITOR_PICKER_OPEN_HERE) && w >= 9 && h >= 9 )
            {
                int ix = x + 3;
                int iy = y + 3;
                int iw = w - 6;
                int ih = h - 6;
                if( picker_clip(picker, &ix, &iy, &iw, &ih) && paint_room(paint, EDITOR_PICKER_DROP_OUTLINES) )
                    ToriRSChrome_GraphRect(chrome, graph, ix, iy, iw, ih, colors->open, 0, 0);
            }
            if( !has_outline || !picker_clip(picker, &x, &y, &w, &h) )
                continue;
            if( picker->selected[i] ? paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES)
                                    : paint_room(paint, EDITOR_PICKER_DROP_OUTLINES) )
                ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, outline, 0, 0);
        }
    }

    /* Below 12 px a square the page's outline, once: where the page is when
     * every cell is a pixel or two. */
    if( lod.page_outline && picker->tab_count > 0 )
    {
        int min_x;
        int max_x;
        int min_z;
        int max_z;
        int x;
        int y;
        int w;
        int h;
        picker_page_bounds(picker, &min_x, &max_x, &min_z, &max_z);
        if( min_x <= max_x && min_z <= max_z && picker_squares_box(picker, min_x, max_x, min_z, max_z, &x, &y, &w, &h)
            && paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES) )
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, colors->text, 0, 0);
    }

    /* The empty cell a click (or search) chose for New. */
    if( picker->empty_valid )
    {
        int x;
        int y;
        int w;
        int h;
        picker_cell_rect(picker, picker->empty_map_x, picker->empty_map_z, &x, &y, &w, &h);
        if( picker_clip(picker, &x, &y, &w, &h) && paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES) )
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, colors->created, 0, 0);
    }

    /* The instance helper's destination (its Find free), outlined. */
    if( picker->mark_w > 0 && picker->mark_h > 0 )
    {
        int x;
        int y;
        int w;
        int h;
        if( picker_squares_box(picker, picker->mark_x, picker->mark_x + picker->mark_w - 1, picker->mark_z,
                picker->mark_z + picker->mark_h - 1, &x, &y, &w, &h)
            && paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES) )
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, colors->created, 0, 0);
    }

    /* The Square map's pick: an Open squares sweep, a Place cell. Doubled,
     * so it reads over the open and selection outlines under it. */
    if( picker->sweep_w > 0 && picker->sweep_h > 0 )
    {
        int x;
        int y;
        int w;
        int h;
        uint32_t const color = picker->sweep_ok ? colors->created : colors->dirty;
        if( picker_squares_box(picker, picker->sweep_x, picker->sweep_x + picker->sweep_w - 1, picker->sweep_z,
                picker->sweep_z + picker->sweep_h - 1, &x, &y, &w, &h)
            && paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES) )
        {
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, color, 0, 0);
            if( w > 4 && h > 4 )
                ToriRSChrome_GraphRect(chrome, graph, x + 1, y + 1, w - 2, h - 2, color, 0, 0);
        }
    }

    /* Hover on top of everything but the frames; from 40 px a square its
     * coordinates in the cell (the axes carry them otherwise). */
    if( picker->hover_valid )
    {
        int x;
        int y;
        int w;
        int h;
        picker_cell_rect(picker, picker->hover_map_x, picker->hover_map_z, &x, &y, &w, &h);
        if( picker_clip(picker, &x, &y, &w, &h) && paint_mark(paint, EDITOR_PICKER_DROP_OUTLINES) )
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, colors->text, 0, 0);
        if( lod.hover_label )
            paint_hover_label(picker, colors, paint);
    }
}

int
Editor_PickerDraw(
    struct Editor_Picker* picker,
    struct ToriRSChrome* chrome,
    int graph,
    int well_w,
    int well_h,
    const struct Editor_PickerColors* colors,
    const struct EditorUiViewFrame* frames,
    int frame_count,
    int self_slot)
{
    struct PickerPaint paint;
    int any = 0;

    assert(picker);
    assert(chrome);
    assert(colors);
    assert(frames || frame_count == 0);
    assert(well_w > 0);
    assert(well_h > 0);

    picker->well_w = well_w;
    picker->well_h = well_h;

    /* What the essentials cost, then the picture. */
    memset(&paint, 0, sizeof(paint));
    paint.chrome = chrome;
    paint.graph = graph;
    paint.counting = 1;
    picker_paint(picker, colors, frames, frame_count, self_slot, &paint);
    {
        int const essentials = paint.items;
        int const marks = paint.marks;
        memset(&paint, 0, sizeof(paint));
        paint.chrome = chrome;
        paint.graph = graph;
        paint.decor_room = essentials < EDITOR_PICKER_ITEMS_BUDGET ? EDITOR_PICKER_ITEMS_BUDGET - essentials : 0;
        paint.marks_reserve = marks;
    }
    ToriRSChrome_GraphBegin(chrome, graph);
    picker_paint(picker, colors, frames, frame_count, self_slot, &paint);
    ToriRSChrome_GraphEnd(chrome, graph);

    /* Never silently: what was dropped, once per change. */
    for( int k = 0; k < EDITOR_PICKER_DROP_KINDS; k++ )
        any |= paint.dropped[k] > 0;
    if( memcmp(paint.dropped, picker->dropped, sizeof(paint.dropped)) != 0 )
    {
        memcpy(picker->dropped, paint.dropped, sizeof(paint.dropped));
        picker->drop_text[0] = '\0';
        if( any )
        {
            size_t used = (size_t)snprintf(picker->drop_text, sizeof(picker->drop_text),
                "picker: %d-item budget at %.1f px a square, dropped", EDITOR_PICKER_ITEMS_BUDGET, picker->zoom);
            int first = 1;
            for( int k = 0; k < EDITOR_PICKER_DROP_KINDS && used < sizeof(picker->drop_text); k++ )
            {
                if( !paint.dropped[k] )
                    continue;
                used += (size_t)snprintf(picker->drop_text + used, sizeof(picker->drop_text) - used, "%s %d %s",
                    first ? "" : ",", paint.dropped[k], Editor_PickerDropName((enum Editor_PickerDropKind)k));
                first = 0;
            }
            TORIRS_LOG("%s\n", picker->drop_text);
        }
    }
    picker->items_drawn = paint.items;
    picker->truncated = any;
    return paint.items;
}
