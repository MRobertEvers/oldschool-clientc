/*
 * The square picker's navigation and panes, the pure half
 * (edpanel_picker_nav.h).
 */

#include "edpanel_picker_nav.h"
#include "edpanel_picker_find.h"
#include "editor/editor_doc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- the focused cell ------------------------------------------------------- */

int
Editor_PickerFocusCell(
    const struct Editor_Picker* picker,
    int* out_map_x,
    int* out_map_z)
{
    assert(picker);
    assert(out_map_x);
    assert(out_map_z);

    if( picker->focus >= 0 && picker->focus < picker->square_count )
    {
        *out_map_x = picker->squares[picker->focus].map_x;
        *out_map_z = picker->squares[picker->focus].map_z;
        return 1;
    }
    if( picker->empty_valid )
    {
        *out_map_x = picker->empty_map_x;
        *out_map_z = picker->empty_map_z;
        return 1;
    }
    return 0;
}

void
Editor_PickerFocusSet(
    struct Editor_Picker* picker,
    int map_x,
    int map_z)
{
    int square = -1;

    assert(picker);
    assert(map_x >= 0);
    assert(map_z >= 0);

    if( picker->storage_filter >= 0 )
        square = Editor_PickerFind(picker, map_x, map_z, picker->storage_filter);
    if( square < 0 )
        square = Editor_PickerFind(picker, map_x, map_z, -1);
    if( square >= 0 )
    {
        Editor_PickerSelect(picker, square, 0);
        return;
    }
    Editor_PickerSelect(picker, -1, 0);
    picker->empty_valid = 1;
    picker->empty_map_x = map_x;
    picker->empty_map_z = map_z;
}

void
Editor_PickerKeepInView(
    struct Editor_Picker* picker,
    int map_x,
    int map_z)
{
    int x;
    int y;
    int w;
    int h;
    int dx = 0;
    int dy = 0;
    int const left = EDITOR_PICKER_LEFT;
    int const top = EDITOR_PICKER_TOP;

    assert(picker);
    if( picker->well_w <= left || picker->well_h <= top )
        return;
    Editor_PickerCellBox(picker, map_x, map_z, &x, &y, &w, &h);
    if( w > picker->well_w - left || h > picker->well_h - top )
    {
        Editor_PickerCentre(picker, map_x, map_z);
        return;
    }
    if( x < left )
        dx = left - x;
    else if( x + w > picker->well_w )
        dx = picker->well_w - (x + w);
    if( y < top )
        dy = top - y;
    else if( y + h > picker->well_h )
        dy = picker->well_h - (y + h);
    if( dx != 0 || dy != 0 )
        Editor_PickerPan(picker, dx, dy);
}

int
Editor_PickerFocusStep(
    struct Editor_Picker* picker,
    int dx,
    int dz)
{
    int map_x;
    int map_z;
    int to_x;
    int to_z;

    assert(picker);
    if( !Editor_PickerFocusCell(picker, &map_x, &map_z) )
    {
        /* Nothing focused: the arrows start where the eye is. */
        int const mid_x = (EDITOR_PICKER_LEFT + picker->well_w) / 2;
        int const mid_y = (EDITOR_PICKER_TOP + picker->well_h) / 2;
        if( !Editor_PickerCellAt(picker, mid_x, mid_y, &map_x, &map_z) )
            return 0;
        Editor_PickerFocusSet(picker, map_x, map_z);
        Editor_PickerKeepInView(picker, map_x, map_z);
        return 1;
    }
    to_x = map_x + dx;
    to_z = map_z + dz;
    if( to_x < 0 )
        to_x = 0;
    if( to_z < 0 )
        to_z = 0;
    if( to_x > EDITOR_PICKER_WORLD_SQUARES - 1 )
        to_x = EDITOR_PICKER_WORLD_SQUARES - 1;
    if( to_z > EDITOR_PICKER_WORLD_SQUARES - 1 )
        to_z = EDITOR_PICKER_WORLD_SQUARES - 1;
    if( to_x == map_x && to_z == map_z )
        return 0;
    Editor_PickerFocusSet(picker, to_x, to_z);
    Editor_PickerKeepInView(picker, to_x, to_z);
    return 1;
}

/* ---- the open set ------------------------------------------------------------- */

int
Editor_PickerOpenSetFind(
    const struct Editor_PickerOpenSet* set,
    const char* key)
{
    assert(set);
    assert(key);
    for( int i = 0; i < set->count; i++ )
        if( strcmp(set->items[i].key, key) == 0 )
            return i;
    return -1;
}

int
Editor_PickerOpenSetSync(
    struct Editor_PickerOpenSet* set,
    const struct Editor_Picker* picker)
{
    unsigned char seen[EDITOR_PICKER_OPEN_SET_MAX];
    int changed = 0;
    int kept = 0;

    assert(set);
    assert(picker);

    memset(seen, 0, sizeof(seen));
    /* The scratch squares are not the map's to drop. */
    for( int i = 0; i < set->count; i++ )
        seen[i] = set->items[i].scratch ? 1 : 0;
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        uint32_t const held = square->flags & (uint32_t)(EDITOR_PICKER_OPEN_HERE | EDITOR_PICKER_DIRTY_HERE);
        struct Editor_PickerOpenItem* item;
        char key[EDITOR_DOC_KEY_MAX];
        int at;

        if( !held )
            continue;
        Editor_PickerSquareKey(picker, i, key);
        at = Editor_PickerOpenSetFind(set, key);
        if( at < 0 )
        {
            /* A window holds at most the document's squares; past that the
             * strip has said all it can. */
            if( set->count >= EDITOR_PICKER_OPEN_SET_MAX )
                continue;
            at = set->count++;
            item = &set->items[at];
            memset(item, 0, sizeof(*item));
            snprintf(item->key, sizeof(item->key), "%s", key);
            item->map_x = square->map_x;
            item->map_z = square->map_z;
            changed = 1;
        }
        item = &set->items[at];
        seen[at] = 1;
        if( item->here != ((held & EDITOR_PICKER_OPEN_HERE) != 0)
            || item->dirty != ((held & EDITOR_PICKER_DIRTY_HERE) != 0) )
        {
            item->here = (held & EDITOR_PICKER_OPEN_HERE) != 0;
            item->dirty = (held & EDITOR_PICKER_DIRTY_HERE) != 0;
            changed = 1;
        }
    }
    for( int i = 0; i < set->count; i++ )
    {
        if( !seen[i] )
        {
            changed = 1;
            continue;
        }
        if( kept != i )
            set->items[kept] = set->items[i];
        kept++;
    }
    set->count = kept;
    return changed;
}

int
Editor_PickerOpenSetSyncScratch(
    struct Editor_PickerOpenSet* set,
    const struct Editor_PickerOpenItem* held,
    int count)
{
    int changed = 0;
    int kept = 0;

    assert(set);
    assert(held || count == 0);
    assert(count >= 0);

    for( int i = 0; i < set->count; i++ )
    {
        struct Editor_PickerOpenItem* item = &set->items[i];
        if( item->scratch )
        {
            int found = -1;
            for( int k = 0; k < count && found < 0; k++ )
                if( strcmp(held[k].key, item->key) == 0 )
                    found = k;
            if( found < 0 )
            {
                changed = 1;
                continue;
            }
            if( item->here != (held[found].here != 0) || item->dirty != (held[found].dirty != 0) )
            {
                item->here = held[found].here != 0;
                item->dirty = held[found].dirty != 0;
                changed = 1;
            }
        }
        if( kept != i )
            set->items[kept] = set->items[i];
        kept++;
    }
    set->count = kept;
    for( int k = 0; k < count && set->count < EDITOR_PICKER_OPEN_SET_MAX; k++ )
    {
        struct Editor_PickerOpenItem* item;
        if( Editor_PickerOpenSetFind(set, held[k].key) >= 0 )
            continue;
        item = &set->items[set->count++];
        memset(item, 0, sizeof(*item));
        snprintf(item->key, sizeof(item->key), "%s", held[k].key);
        item->map_x = held[k].map_x;
        item->map_z = held[k].map_z;
        item->here = held[k].here != 0;
        item->dirty = held[k].dirty != 0;
        item->scratch = 1;
        changed = 1;
    }
    return changed;
}

void
Editor_PickerOpenSetMove(
    struct Editor_PickerOpenSet* set,
    int from,
    int to)
{
    struct Editor_PickerOpenItem moving;

    assert(set);
    assert(from >= 0);
    assert(from < set->count);
    assert(to >= 0);
    assert(to < set->count);

    if( from == to )
        return;
    moving = set->items[from];
    if( from < to )
        memmove(&set->items[from], &set->items[from + 1], (size_t)(to - from) * sizeof(set->items[0]));
    else
        memmove(&set->items[to + 1], &set->items[to], (size_t)(from - to) * sizeof(set->items[0]));
    set->items[to] = moving;
}

void
Editor_PickerChipText(
    const char* key,
    char out[EDITOR_PICKER_NAME_MAX])
{
    const char* square;
    char lane[EDITOR_PICKER_NAME_MAX];

    assert(key);
    assert(out);
    square = strrchr(key, '/');
    if( !square )
    {
        snprintf(out, EDITOR_PICKER_NAME_MAX, "%s", key);
        return;
    }
    /* A scratch square by its slot: where it is parked is not where it is. */
    if( strncmp(key, EDITOR_DOC_SCRATCH_STORAGE "/", sizeof(EDITOR_DOC_SCRATCH_STORAGE)) == 0 )
    {
        int map_x;
        int map_z;
        if( Editor_SquareKeyParse(square + 1, &map_x, &map_z) )
            for( int slot = 0; slot < EDITOR_DOC_SCRATCH_SLOTS; slot++ )
            {
                int slot_x;
                int slot_z;
                Editor_DocScratchSlot(slot, &slot_x, &slot_z);
                if( slot_x == map_x && slot_z == map_z )
                {
                    snprintf(out, EDITOR_PICKER_NAME_MAX, "scratch %d", slot + 1);
                    return;
                }
            }
    }
    if( Editor_PickerLaneName(key, lane) )
        snprintf(out, EDITOR_PICKER_NAME_MAX, "%s/%s", lane, square + 1);
    else
        snprintf(out, EDITOR_PICKER_NAME_MAX, "%s", key);
}

/* ---- the strip's collapse rule --------------------------------------------------- */

void
Editor_PickerStripLay(
    const int* widths,
    int count,
    int track,
    int gap,
    int (*more_width)(void* user, int hidden),
    void* user,
    struct Editor_PickerStripLayout* out)
{
    int total = 0;
    int shown;

    assert(out);
    assert(count >= 0);
    assert(count <= EDITOR_PICKER_OPEN_SET_MAX);
    if( count > 0 )
        assert(widths);
    assert(more_width);
    assert(gap >= 0);

    memset(out, 0, sizeof(*out));
    for( int i = 0; i < count; i++ )
        total += widths[i] + (i > 0 ? gap : 0);
    if( total <= track )
        shown = count;
    else
    {
        /* The most chips that fit beside the "+N" for the rest. */
        for( shown = count - 1; shown > 0; shown-- )
        {
            int used = 0;
            for( int i = 0; i < shown; i++ )
                used += widths[i] + gap;
            if( used + more_width(user, count - shown) <= track )
                break;
        }
    }
    out->shown = shown;
    out->hidden = count - shown;
    {
        int x = 0;
        for( int i = 0; i < shown; i++ )
        {
            out->x[i] = x;
            out->w[i] = widths[i];
            x += widths[i] + gap;
        }
        if( out->hidden > 0 )
        {
            out->more_x = x;
            out->more_w = more_width(user, out->hidden);
        }
    }
}

/* ---- Recent ------------------------------------------------------------------------ */

void
Editor_PickerRecentPush(
    struct Editor_PickerRecent* recent,
    const char* key)
{
    int at;

    assert(recent);
    assert(key);
    assert(key[0]);

    for( at = 0; at < recent->count; at++ )
        if( strcmp(recent->keys[at], key) == 0 )
            break;
    if( at == recent->count )
    {
        if( recent->count < EDITOR_PICKER_RECENT_MAX )
            recent->count++;
        at = recent->count - 1;
    }
    memmove(recent->keys[1], recent->keys[0], (size_t)at * sizeof(recent->keys[0]));
    snprintf(recent->keys[0], sizeof(recent->keys[0]), "%s", key);
}

/* ---- text on a track ----------------------------------------------------------------- */

int
Editor_PickerFitText(
    const char* text,
    int width,
    int (*measure)(void* user, const char* s),
    void* user,
    char* out,
    int out_size)
{
    int length;

    assert(text);
    assert(measure);
    assert(out);
    assert(out_size > 4);

    snprintf(out, (size_t)out_size, "%s", text);
    length = (int)strlen(text);
    if( length < out_size && measure(user, out) <= width )
        return 0;
    if( length > out_size - 4 )
        length = out_size - 4;
    for( ; length > 0; length-- )
    {
        /* "tile..." reads as a sentence that goes on, "tile ..." as a word
         * that is missing. */
        if( text[length - 1] == ' ' )
            continue;
        snprintf(out, (size_t)out_size, "%.*s...", length, text);
        if( measure(user, out) <= width )
            return 1;
    }
    snprintf(out, (size_t)out_size, "...");
    return 1;
}

/* ---- the Square map's sweep ---------------------------------------------------- */

static void
sweep_axis(
    int a,
    int b,
    int max_side,
    int* out_start,
    int* out_length)
{
    int d = b - a;

    if( d > max_side - 1 )
        d = max_side - 1;
    if( d < -(max_side - 1) )
        d = -(max_side - 1);
    *out_start = d < 0 ? a + d : a;
    *out_length = (d < 0 ? -d : d) + 1;
}

void
Editor_PickerSweepRect(
    int ax,
    int az,
    int bx,
    int bz,
    int max_side,
    int* out_x,
    int* out_z,
    int* out_w,
    int* out_h)
{
    assert(max_side >= 1);
    assert(out_x);
    assert(out_z);
    assert(out_w);
    assert(out_h);
    sweep_axis(ax, bx, max_side, out_x, out_w);
    sweep_axis(az, bz, max_side, out_z, out_h);
}
