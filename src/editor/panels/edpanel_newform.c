#include "edpanel_newform.h"

#include "editor/editor_doc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int
Editor_NewSquareLaneValid(const char* lane)
{
    size_t length;

    assert(lane);

    length = strlen(lane);
    if( length == 0 || length >= EDITOR_LANE_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = lane[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

static const char*
newform_storage_label(const char* storage)
{
    return (storage[0] == '\0' || strcmp(storage, "maps") == 0) ? "maps/" : storage;
}

/** Who holds (map_x, map_z) in storage `index`: "maps/ (Gielinor Surface)". */
static void
newform_owner(
    const struct Editor_Picker* picker,
    int square,
    char* out,
    size_t out_size)
{
    const struct Editor_PickerSquare* row = &picker->squares[square];
    const char* storage = picker->storages[row->storage];
    const char* area = row->primary_area >= 0 ? picker->areas[row->primary_area].display_name
                                              : "unmapped";

    snprintf(out, out_size, "%s, %s", newform_storage_label(storage), area[0] ? area : "unmapped");
}

int
Editor_NewSquareValidate(
    const struct Editor_Picker* picker,
    const struct Editor_NewSquareForm* form,
    struct Editor_NewSquareCheck* check,
    struct EditorHost_DocCreate* request)
{
    char key[EDITOR_DOC_KEY_MAX];
    enum Editor_DocTemplate template_kind;
    int storage_index = -1;
    int taken;

    assert(picker);
    assert(form);
    assert(check);

    memset(check, 0, sizeof(*check));

    if( form->map_x < 0 || form->map_x > 255 || form->map_z < 0 || form->map_z > 255 )
    {
        snprintf(check->message, sizeof(check->message), "coordinates must be 0..255");
        return 0;
    }
    Editor_SquareKey(key, form->map_x, form->map_z);
    if( Editor_DocIsScratchSlot(form->map_x, form->map_z) )
    {
        snprintf(check->message, sizeof(check->message), "%s is kept for scratch squares", key);
        return 0;
    }

    /* Where it lives. */
    if( form->storage < 0 )
    {
        if( !Editor_NewSquareLaneValid(form->new_lane) )
        {
            snprintf(
                check->message, sizeof(check->message),
                "name the new lane: letters, digits, '_' or '-'");
            return 0;
        }
        snprintf(check->storage, sizeof(check->storage), "ported/%s/maps", form->new_lane);
        storage_index = Editor_PickerStorageIndex(picker, check->storage);
        check->creates_lane = storage_index < 0;
    }
    else
    {
        if( form->storage >= picker->storage_count )
        {
            snprintf(check->message, sizeof(check->message), "pick where the square is stored");
            return 0;
        }
        storage_index = form->storage;
        snprintf(check->storage, sizeof(check->storage), "%s", picker->storages[storage_index]);
        if( strcmp(check->storage, "maps") == 0 )
            check->storage[0] = '\0';
    }
    if( !Editor_DocStorageValid(check->storage) )
    {
        snprintf(check->message, sizeof(check->message), "not a storage the tree accepts");
        return 0;
    }

    /* Taken in that storage: refused, with the owner named. */
    taken = storage_index >= 0 ? Editor_PickerFind(picker, form->map_x, form->map_z, storage_index)
                               : -1;
    if( taken >= 0 )
    {
        char owner[EDITOR_NEWFORM_MESSAGE_MAX / 2];
        newform_owner(picker, taken, owner, sizeof(owner));
        snprintf(check->message, sizeof(check->message), "%s already exists (%s)", key, owner);
        return 0;
    }

    /* What it starts as. */
    template_kind = form->template_kind;
    if( form->mode != EDITOR_NEW_SQUARE_NEW )
        template_kind = EDITOR_DOC_TEMPLATE_COPY;
    if( template_kind == EDITOR_DOC_TEMPLATE_COPY )
    {
        char source_storage[EDITOR_DOC_STORAGE_MAX];
        char source_bare[EDITOR_DOC_KEY_MAX];
        if( !form->source_key[0]
            || !Editor_DocSplitSquareKey(form->source_key, source_storage, source_bare) )
        {
            snprintf(
                check->message, sizeof(check->message),
                "select the square to copy in the picker first");
            return 0;
        }
        if( form->mode == EDITOR_NEW_SQUARE_SAVE_AS
            && strcmp(source_storage, check->storage) == 0 )
        {
            snprintf(
                check->message, sizeof(check->message), "%s is already stored there; pick another",
                source_bare);
            return 0;
        }
    }
    if( template_kind == EDITOR_DOC_TEMPLATE_FLAT
        && (form->fill_height < 0 || form->fill_height > 255 || form->fill_underlay < 0
            || form->fill_underlay > EDITOR_UNDERLAY_MAX) )
    {
        snprintf(check->message, sizeof(check->message), "flat fill: height 0..255, underlay 0..%d",
            EDITOR_UNDERLAY_MAX);
        return 0;
    }

    {
        int const elsewhere = Editor_PickerFind(picker, form->map_x, form->map_z, -1);
        const char* lane_note = check->creates_lane ? " (new lane)" : "";
        if( elsewhere >= 0 )
        {
            char owner[EDITOR_NEWFORM_MESSAGE_MAX / 2];
            newform_owner(picker, elsewhere, owner, sizeof(owner));
            snprintf(
                check->message, sizeof(check->message), "creates %s in %s%s; also in %s", key,
                newform_storage_label(check->storage), lane_note, owner);
        }
        else
            snprintf(
                check->message, sizeof(check->message), "creates %s in %s%s", key,
                newform_storage_label(check->storage), lane_note);
    }

    if( request )
    {
        memset(request, 0, sizeof(*request));
        request->kind = EDITOR_DOC_SQUARE;
        snprintf(request->key, sizeof(request->key), "%s", key);
        snprintf(request->storage, sizeof(request->storage), "%s", check->storage);
        request->template_kind = template_kind;
        if( template_kind == EDITOR_DOC_TEMPLATE_COPY )
            snprintf(request->source_key, sizeof(request->source_key), "%s", form->source_key);
        request->fill_height = form->fill_height;
        request->fill_underlay = form->fill_underlay;
    }
    check->ok = 1;
    return 1;
}

/* ---- New at a place: one square or a block ---------------------------------- */

int
Editor_NewSquarePlaceParse(
    const char* text,
    int* out_map_x,
    int* out_map_z)
{
    char trimmed[EDITOR_NEWFORM_PLACE_MAX];
    size_t length;
    int x;
    int z;
    char tail;

    assert(text);
    assert(out_map_x);
    assert(out_map_z);

    while( *text == ' ' || *text == '\t' )
        text++;
    if( strlen(text) >= sizeof(trimmed) )
        return 0;
    snprintf(trimmed, sizeof(trimmed), "%s", text);
    length = strlen(trimmed);
    while( length > 0 && (trimmed[length - 1] == ' ' || trimmed[length - 1] == '\t') )
        trimmed[--length] = '\0';
    if( length == 0 )
        return 0;
    if( trimmed[0] == 'M' )
        trimmed[0] = 'm';
    if( trimmed[0] == 'm' )
    {
        if( !Editor_SquareKeyParse(trimmed, &x, &z) )
            return 0;
    }
    else if( sscanf(trimmed, "%d %d%c", &x, &z, &tail) != 2 && sscanf(trimmed, "%d , %d%c", &x, &z, &tail) != 2 )
        return 0;
    if( x < 0 || z < 0 )
        return 0;
    *out_map_x = x;
    *out_map_z = z;
    return 1;
}

int
Editor_NewSquareBlockCheck(
    const struct Editor_Picker* picker,
    const struct Editor_NewSquareForm* form,
    struct Editor_NewSquareBlock* out)
{
    struct Editor_NewSquareForm cell;
    struct Editor_NewSquareCheck check;
    char first[EDITOR_DOC_KEY_MAX];
    char last[EDITOR_DOC_KEY_MAX];

    assert(picker);
    assert(form);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->w = form->block_w > 0 ? form->block_w : 1;
    out->h = form->block_h > 0 ? form->block_h : 1;
    assert(out->w <= EDITOR_NEWFORM_BLOCK_SIDE_MAX);
    assert(out->h <= EDITOR_NEWFORM_BLOCK_SIDE_MAX);

    if( !Editor_NewSquarePlaceParse(form->place, &out->map_x, &out->map_z) )
    {
        snprintf(out->message, sizeof(out->message), "'%.20s' is not a square key like m72_56", form->place);
        return 0;
    }
    Editor_SquareKey(first, out->map_x, out->map_z);
    if( out->map_x > 255 || out->map_z > 255 || out->map_x + out->w - 1 > 255 || out->map_z + out->h - 1 > 255 )
    {
        snprintf(out->message, sizeof(out->message), "a %dx%d block at %s leaves the map (0..255)", out->w, out->h,
            first);
        return 0;
    }
    Editor_SquareKey(last, out->map_x + out->w - 1, out->map_z + out->h - 1);

    /* Each cell as a New in maps/ with the form's template: the cell that
     * cannot be made refuses the block, by name. */
    cell = *form;
    cell.mode = EDITOR_NEW_SQUARE_NEW;
    cell.storage = Editor_PickerStorageIndex(picker, "maps");
    if( cell.storage < 0 )
    {
        snprintf(out->message, sizeof(out->message), "this server lists no maps/ to make %s in", first);
        return 0;
    }
    cell.new_lane[0] = '\0';
    for( int dz = 0; dz < out->h; dz++ )
    {
        for( int dx = 0; dx < out->w; dx++ )
        {
            cell.map_x = out->map_x + dx;
            cell.map_z = out->map_z + dz;
            if( !Editor_NewSquareValidate(picker, &cell, &check, NULL) )
            {
                snprintf(out->message, sizeof(out->message), "%s", check.message);
                return 0;
            }
            out->cells[out->count][0] = cell.map_x;
            out->cells[out->count][1] = cell.map_z;
            out->count++;
        }
    }
    if( out->count == 1 )
        snprintf(out->message, sizeof(out->message), "creates %s in maps/, saved, and opens it", first);
    else
        snprintf(out->message, sizeof(out->message), "creates %s .. %s (%d) in maps/ and opens them", first, last,
            out->count);
    out->ok = 1;
    return 1;
}

/** Whether (map_x, map_z) holds nothing a new square would collide with:
 *  no square in any storage, and not a scratch slot. Off the map is not free. */
static int
newform_cell_free(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z)
{
    if( map_x < 0 || map_x > 255 || map_z < 0 || map_z > 255 )
        return 0;
    return !Editor_DocIsScratchSlot(map_x, map_z) && Editor_PickerFind(picker, map_x, map_z, -1) < 0;
}

int
Editor_NewSquareFindFree(
    const struct Editor_Picker* picker,
    int w,
    int h,
    int* out_map_x,
    int* out_map_z)
{
    int found = 0;

    assert(picker);
    assert(w >= 1);
    assert(h >= 1);
    assert(out_map_x);
    assert(out_map_z);

    for( int z = EDITOR_NEWFORM_FREE_Z0; z + h - 1 <= EDITOR_NEWFORM_FREE_Z1; z++ )
    {
        for( int x = EDITOR_NEWFORM_FREE_X0; x + w - 1 <= EDITOR_NEWFORM_FREE_X1; x++ )
        {
            int block_free = 1;
            int alone = 1;

            for( int dz = -1; dz <= h && block_free; dz++ )
            {
                for( int dx = -1; dx <= w; dx++ )
                {
                    int const inside = dx >= 0 && dx < w && dz >= 0 && dz < h;
                    if( inside )
                    {
                        if( !newform_cell_free(picker, x + dx, z + dz) )
                        {
                            block_free = 0;
                            break;
                        }
                    }
                    else if( Editor_PickerFind(picker, x + dx, z + dz, -1) >= 0 )
                        alone = 0;
                }
            }
            if( !block_free )
                continue;
            if( alone )
            {
                *out_map_x = x;
                *out_map_z = z;
                return 1;
            }
            /* Free but against a neighbour: kept in case nothing stands alone. */
            if( !found )
            {
                found = 1;
                *out_map_x = x;
                *out_map_z = z;
            }
        }
    }
    return found;
}
