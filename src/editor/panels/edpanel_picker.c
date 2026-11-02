#include "edpanel_picker.h"

#include "editor/editor_doc.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int k_cells[] = { 3, 4, 6, 8, 12, 16, 24, 32, 48 };
#define PICKER_CELL_STEPS ((int)(sizeof(k_cells) / sizeof(k_cells[0])))

void
Editor_PickerInit(struct Editor_Picker* picker)
{
    assert(picker);
    memset(picker, 0, sizeof(*picker));
    picker->cell = 8;
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

    /* Surface first, Unmapped last, the rest by what the world map calls
     * them. */
    if( ta->area < 0 || tb->area < 0 )
        return ta->area < 0 ? (tb->area < 0 ? 0 : 1) : -1;
    if( picker->areas[ta->area].is_main != picker->areas[tb->area].is_main )
        return picker->areas[ta->area].is_main ? -1 : 1;
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
                            : -2;
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

    tabs = ToriMem_Calloc(group, (size_t)(picker->area_count + 1), sizeof(*tabs));
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
        tabs[count].area = -1;
        snprintf(tabs[count].title, sizeof(tabs[count].title), "Unmapped");
        count++;
    }
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
        if( tabs[t].count == 0 )
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
        for( int i = 0; i < atlas->square_count; i++ )
        {
            const struct EditorHost_CatalogSquare* from = &atlas->squares[i];
            struct Editor_PickerSquare* to = &picker->squares[i];

            to->map_x = from->map_x;
            to->map_z = from->map_z;
            to->storage =
                (from->storage >= 0 && from->storage < atlas->storage_count) ? from->storage : 0;
            to->flags = from->flags;
            to->spawn_count = from->spawn_count;
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
    struct Editor_Doc* doc)
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

void
Editor_PickerCentre(
    struct Editor_Picker* picker,
    int map_x,
    int map_z)
{
    assert(picker);
    assert(picker->cell > 0);

    picker->origin_x = (double)map_x + 0.5 - (double)picker_avail_w(picker) / (2.0 * picker->cell);
    picker->origin_z = (double)map_z + 0.5 + (double)picker_avail_h(picker) / (2.0 * picker->cell);
}

void
Editor_PickerFit(
    struct Editor_Picker* picker,
    int well_w,
    int well_h)
{
    const struct Editor_PickerTab* tab;
    struct Editor_PickerTab bounds;
    int span_x;
    int span_z;
    int fit;

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
    span_x = tab->max_x - tab->min_x + 1;
    span_z = tab->max_z - tab->min_z + 1;
    fit = picker_avail_w(picker) / span_x;
    if( picker_avail_h(picker) / span_z < fit )
        fit = picker_avail_h(picker) / span_z;
    /* The largest ladder step that fits; the smallest when none does (the
     * surface at full width is a pan, not an unreadable smear). */
    picker->cell = k_cells[0];
    for( int i = 0; i < PICKER_CELL_STEPS; i++ )
        if( k_cells[i] <= fit )
            picker->cell = k_cells[i];
    picker->origin_x = (tab->min_x + tab->max_x + 1) / 2.0
                       - (double)picker_avail_w(picker) / (2.0 * picker->cell);
    picker->origin_z = (tab->min_z + tab->max_z + 1) / 2.0
                       + (double)picker_avail_h(picker) / (2.0 * picker->cell);
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
Editor_PickerZoom(
    struct Editor_Picker* picker,
    int delta,
    int x,
    int y)
{
    int step = 0;
    int next;
    double map_x;
    double map_z;

    assert(picker);

    for( int i = 0; i < PICKER_CELL_STEPS; i++ )
        if( k_cells[i] <= picker->cell )
            step = i;
    next = step + (delta < 0 ? 1 : delta > 0 ? -1 : 0);
    if( next < 0 )
        next = 0;
    if( next >= PICKER_CELL_STEPS )
        next = PICKER_CELL_STEPS - 1;
    if( k_cells[next] == picker->cell )
        return;

    /* The square under the pointer stays under it. */
    map_x = picker->origin_x + (double)(x - EDITOR_PICKER_LEFT) / picker->cell;
    map_z = picker->origin_z - (double)(y - EDITOR_PICKER_TOP) / picker->cell;
    picker->cell = k_cells[next];
    picker->origin_x = map_x - (double)(x - EDITOR_PICKER_LEFT) / picker->cell;
    picker->origin_z = map_z + (double)(y - EDITOR_PICKER_TOP) / picker->cell;
}

void
Editor_PickerPan(
    struct Editor_Picker* picker,
    int dx,
    int dy)
{
    assert(picker);
    assert(picker->cell > 0);

    picker->origin_x -= (double)dx / picker->cell;
    picker->origin_z += (double)dy / picker->cell;
}

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

    if( gx < 0 || gy < 0 || picker->cell <= 0 )
        return 0;
    if( picker->well_w > 0 && (x >= picker->well_w || y >= picker->well_h) )
        return 0;
    *out_map_x = (int)floor(picker->origin_x + ((double)gx + 0.5) / picker->cell);
    *out_map_z = (int)floor(picker->origin_z - ((double)gy + 0.5) / picker->cell);
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
    if( picker->cell < 12 )
        picker->cell = 12;
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
    colors->open = 0xffffff;
    colors->dirty = 0xff981f;
    colors->selected = 0xffff00;
    colors->created = 0x6fd06f;
    colors->frame = 0x4fa8ff;
    colors->self_frame = 0xff4fd0;
}

/** Cell rectangle of map square (x, z) in well pixels. */
static void
picker_cell_rect(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y)
{
    *out_x = EDITOR_PICKER_LEFT + (int)floor((map_x - picker->origin_x) * picker->cell + 0.5);
    *out_y = EDITOR_PICKER_TOP + (int)floor((picker->origin_z - (map_z + 1)) * picker->cell + 0.5);
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

static int
picker_label_every(int cell)
{
    static const int k_steps[] = { 1, 2, 5, 10, 20, 50, 100 };
    for( int i = 0; i < (int)(sizeof(k_steps) / sizeof(k_steps[0])); i++ )
        if( k_steps[i] * cell >= 26 )
            return k_steps[i];
    return 100;
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
    int items = 0;
    int budget = EDITOR_PICKER_ITEMS_BUDGET;
    int truncated = 0;
    int const cell = picker ? picker->cell : 0;
    int gap;

    assert(picker);
    assert(chrome);
    assert(colors);
    assert(frames || frame_count == 0);
    assert(well_w > 0);
    assert(well_h > 0);

    picker->well_w = well_w;
    picker->well_h = well_h;
    gap = cell >= 8 ? 1 : 0;

    ToriRSChrome_GraphBegin(chrome, graph);
    ToriRSChrome_GraphRect(chrome, graph, 0, 0, well_w, well_h, colors->background, 1, 0);
    items++;

    /* Axis labels: map x across the top, map z down the left, at a spacing
     * that keeps them readable at any zoom. */
    {
        int const every = picker_label_every(cell);
        int const first_x = (int)floor(picker->origin_x);
        int const last_x = first_x + picker_avail_w(picker) / cell + 1;
        int const first_z = (int)floor(picker->origin_z);
        int const last_z = first_z - picker_avail_h(picker) / cell - 1;
        char text[16];

        for( int mx = first_x - first_x % every; mx <= last_x && items < budget; mx += every )
        {
            int px;
            int py;
            if( mx < 0 )
                continue;
            picker_cell_rect(picker, mx, 0, &px, &py);
            if( px < EDITOR_PICKER_LEFT || px >= well_w - 8 )
                continue;
            snprintf(text, sizeof(text), "%d", mx);
            ToriRSChrome_GraphText(chrome, graph, px, EDITOR_PICKER_TOP - 2, colors->text, text);
            items++;
        }
        for( int mz = first_z - first_z % every; mz >= last_z && items < budget; mz -= every )
        {
            int px;
            int py;
            if( mz < 0 )
                break;
            picker_cell_rect(picker, 0, mz, &px, &py);
            if( py < EDITOR_PICKER_TOP || py >= well_h - 4 )
                continue;
            snprintf(text, sizeof(text), "%d", mz);
            ToriRSChrome_GraphText(chrome, graph, 1, py + (cell < 10 ? cell : 10), colors->text, text);
            items++;
        }
    }

    /* Cells, then outlines over them, so an outline is never painted over by
     * a neighbour's fill. Two passes over the visible set; the budget keeps
     * the shared item pool for the page's other wells. */
    for( int pass = 0; pass < 4; pass++ )
    {
        /* Passes 0/1 fill, 2/3 outline; the even pass takes the lanes, the
         * odd one the content tree's maps/, so maps/ is on top. */
        int const want_maps = pass & 1;
        for( int i = 0; i < picker->square_count; i++ )
        {
            const struct Editor_PickerSquare* square = &picker->squares[i];
            int x;
            int y;
            int w = cell - gap;
            int h = cell - gap;
            uint32_t outline = 0;
            int has_outline = 0;

            if( !picker_visible(picker, i)
                || picker_storage_is_maps(picker, square->storage) != want_maps )
                continue;
            picker_cell_rect(picker, square->map_x, square->map_z, &x, &y);
            if( x + w <= EDITOR_PICKER_LEFT || y + h <= EDITOR_PICKER_TOP || x >= well_w
                || y >= well_h )
                continue;

            if( pass < 2 )
            {
                if( items >= budget )
                {
                    truncated = 1;
                    continue;
                }
                if( !picker_clip(picker, &x, &y, &w, &h) )
                    continue;
                ToriRSChrome_GraphRect(
                    chrome, graph, x, y, w, h, square->rgb ? square->rgb : colors->empty, 1,
                    EDITOR_PICKER_KEY_SQUARE(i));
                items++;
                continue;
            }

            if( picker->selected[i] )
            {
                outline = colors->selected;
                has_outline = 1;
            }
            else if( square->flags & (TORIRSMAPED_SQUARE_DIRTY | EDITOR_PICKER_DIRTY_HERE) )
            {
                outline = colors->dirty;
                has_outline = 1;
            }
            else if( square->flags & (TORIRSMAPED_SQUARE_OPEN | EDITOR_PICKER_OPEN_HERE) )
            {
                outline = colors->open;
                has_outline = 1;
            }
            else if( square->flags & TORIRSMAPED_SQUARE_CREATED )
            {
                outline = colors->created;
                has_outline = 1;
            }
            if( !has_outline || items >= budget )
                continue;
            if( !picker_clip(picker, &x, &y, &w, &h) )
                continue;
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, outline, 0, 0);
            items++;
        }
    }

    /* The empty cell a click (or search) chose for New. */
    if( picker->empty_valid && items < budget )
    {
        int x;
        int y;
        int w = cell;
        int h = cell;
        picker_cell_rect(picker, picker->empty_map_x, picker->empty_map_z, &x, &y);
        if( picker_clip(picker, &x, &y, &w, &h) )
        {
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, colors->created, 0, 0);
            items++;
        }
    }

    /* Hover on top of everything but the frames. */
    if( picker->hover_valid && items < budget )
    {
        int x;
        int y;
        int w = cell;
        int h = cell;
        picker_cell_rect(picker, picker->hover_map_x, picker->hover_map_z, &x, &y);
        if( picker_clip(picker, &x, &y, &w, &h) )
        {
            ToriRSChrome_GraphRect(chrome, graph, x, y, w, h, colors->text, 0, 0);
            items++;
        }
    }

    /* Every view's window, this window's in its own colour. */
    for( int f = 0; f < frame_count && items < budget; f++ )
    {
        int x;
        int y;
        int w;
        int h;
        if( !frames[f].used )
            continue;
        picker_cell_rect(picker, frames[f].map_x, frames[f].map_z + frames[f].h - 1, &x, &y);
        w = frames[f].w * cell;
        h = frames[f].h * cell;
        if( !picker_clip(picker, &x, &y, &w, &h) )
            continue;
        ToriRSChrome_GraphRect(
            chrome, graph, x, y, w, h,
            frames[f].slot == self_slot ? colors->self_frame : colors->frame, 0, 0);
        items++;
    }

    ToriRSChrome_GraphEnd(chrome, graph);
    picker->items_drawn = items;
    picker->truncated = truncated;
    return items;
}
