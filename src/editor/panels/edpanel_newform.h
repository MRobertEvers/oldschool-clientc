#ifndef SRC_EDITOR_PANELS_EDPANEL_NEWFORM_H
#define SRC_EDITOR_PANELS_EDPANEL_NEWFORM_H

/**
 * The New square dialog's form, and the one question it answers: is this a
 * square the server will create, and if so, what DOC_CREATE says it
 * (docs/MAPED_DESIGN.html #squares).
 *
 *   where it lives  the coordinates and the storage: the content tree's
 *                   maps/ (what the cache pack reads), an existing lane
 *                   (ported/<lane>/maps/), or a lane the dialog creates by
 *                   naming it. A square whose coordinates are taken in that
 *                   storage is refused, naming the owner.
 *   what it starts  empty, a copy of the picker's selection, or a flat fill
 *                   at an authored height and underlay.
 *
 * Copy to... and Save as new are the same form with the template fixed to
 * the copy: Copy to asks coordinates and storage, Save as new the storage
 * alone (design: "the storage question alone").
 *
 * App-free and chrome-free: the modal in edpanel_newsquare.c reads its widgets
 * into this form; tools-panels' test validates forms against a real atlas.
 */

#include "editor/editor_host.h"
#include "edpanel_picker.h"
#include "edpanels.h"

#define EDITOR_LANE_NAME_MAX 48
#define EDITOR_NEWFORM_MESSAGE_MAX 160

struct Editor_NewSquareForm
{
    enum Editor_NewSquareMode mode;
    int map_x;
    int map_z;
    /** Index into the picker's storages, or -1: create the lane `new_lane`. */
    int storage;
    char new_lane[EDITOR_LANE_NAME_MAX];
    enum Editor_DocTemplate template_kind;
    /** The square a COPY copies ("m50_50", or with its storage). */
    char source_key[EDITOR_DOC_KEY_MAX];
    /** FLAT: the `h` raw byte (1..255) and the underlay id (0..255). */
    int fill_height;
    int fill_underlay;
};

struct Editor_NewSquareCheck
{
    int ok;
    /** Why not, or what will happen ("creates m50_50 in maps/"). */
    char message[EDITOR_NEWFORM_MESSAGE_MAX];
    /** The storage the request names ("" = maps/). */
    char storage[EDITOR_DOC_STORAGE_MAX];
    /** The lane is new: nothing of it exists until the first save. */
    int creates_lane;
};

/** A lane directory name the content tree accepts: [a-z0-9_-], 1..47. */
int
Editor_NewSquareLaneValid(const char* lane);

/**
 * Check `form` against the atlas and build the request. `request` may be NULL
 * (a live check while the user types). Returns check->ok.
 */
int
Editor_NewSquareValidate(
    const struct Editor_Picker* picker,
    const struct Editor_NewSquareForm* form,
    struct Editor_NewSquareCheck* check,
    struct EditorHost_DocCreate* request);

#endif
