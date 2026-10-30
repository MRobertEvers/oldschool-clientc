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
        && (form->fill_height < 1 || form->fill_height > 255 || form->fill_underlay < 0
            || form->fill_underlay > 255) )
    {
        snprintf(check->message, sizeof(check->message), "flat fill: height 1..255, underlay 0..255");
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
