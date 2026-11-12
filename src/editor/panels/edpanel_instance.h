#ifndef SRC_EDITOR_PANELS_EDPANEL_INSTANCE_H
#define SRC_EDITOR_PANELS_EDPANEL_INSTANCE_H

/**
 * The instance helper's form, and the one question it answers before any
 * document is touched: is this an instance the authority will create, and
 * if not, why (design section 13; edpanel_instance.c is the modal).
 *
 * Chrome-free, like edpanel_newform.h: the modal reads its widgets into the
 * form; a test validates forms against a synthetic atlas.
 */

#include "editor/editor_instance.h"
#include "edpanel_newform.h"
#include "edpanel_picker.h"

/** A check row's sentence: what a chrome label holds
 *  (TORIRS_CHROME_INPUT_MAX). */
#define EDITOR_INSTANCE_MESSAGE_MAX 192
/** "areas/area_lumbridge/scripts/ladders.rs2:12": where a trigger is. */
#define EDITOR_INSTANCE_WHERE_FILE_MAX 96

/** What the lane's catalog says of a name the form holds. The dialog asks;
 *  a form that never asked (a test's, a window with no catalog) is
 *  UNCHECKED and passes. */
enum Editor_InstanceNameState
{
    EDITOR_INSTANCE_NAME_UNCHECKED = 0,
    /** A record of that name exists. */
    EDITOR_INSTANCE_NAME_KNOWN,
    /** No record: a typo, or a record made this session and not saved yet.
     *  Said as a note, not refused. */
    EDITOR_INSTANCE_NAME_ABSENT,
    /** A loc whose [oploc1,<loc>] the tree already declares: a second one
     *  does not compile. Refused, with where it is. */
    EDITOR_INSTANCE_NAME_TAKEN,
    /** A loc with no first option (op1): its [oploc1] is never clicked. A
     *  note: the Record panel gives it one. */
    EDITOR_INSTANCE_NAME_NO_OPTION,
    /** A loc whose CATEGORY's [oploc1,_<category>] the tree declares (the
     *  dairy churn, every range): a trigger of the loc's own name wins over
     *  it, for every loc of the type placed anywhere. A note, with the
     *  category and where: right for a loc made with Copy to new (it
     *  inherits the category), wrong for the game's own churn. */
    EDITOR_INSTANCE_NAME_CATEGORY,
};

struct Editor_InstanceForm
{
    int source_count;
    char sources[EDITOR_INSTANCE_SQUARES_MAX][EDITOR_DOC_KEY_MAX];
    /** EDITOR_INSTANCE_COPY_* */
    unsigned copy;
    int level_lo;
    int level_hi;
    /** The destination box's south-west square; -1 when not given. */
    int dst_x;
    int dst_z;
    /** Index into the picker's storages, or -1: the lane `new_lane`. */
    int storage;
    char new_lane[EDITOR_LANE_NAME_MAX];
    char name[EDITOR_INSTANCE_NAME_MAX];
    /** 1: add worldmap/areas/<name>.wma. */
    int map_area;
    enum Editor_InstanceEntry entry;
    /** The trigger's subject loc. */
    char loc[EDITOR_INSTANCE_LOC_MAX];
    /** Where the trigger lands ("" = the middle of the first square). */
    char coord[EDITOR_INSTANCE_COORD_MAX + 8];
    /** The lane's server reports TORIRSMAPED_CAP_MAP_INSTANCE. */
    int runtime_capable;

    /** The exit's subject loc ("" = no exit block). The trigger loc itself
     *  under a runtime entry: its copy inside the instance leaves. */
    char exit_loc[EDITOR_INSTANCE_LOC_MAX];
    /** Where the exit returns to, a tile of the real map ("" = `home`). */
    char exit_coord[EDITOR_INSTANCE_COORD_MAX + 8];
    /** The trigger loc's own tile, when the dialog knows one (the loc
     *  selected in the World, else its first placement in the open source
     *  squares): what an empty "Returns to" means. */
    int home_known;
    int home_level;
    int home_x;
    int home_z;
    /** The npc added on entering a runtime copy ("" = none). */
    char npc[EDITOR_INSTANCE_LOC_MAX];
    /** Its tile in the template ("" = two tiles north of the landing). */
    char npc_coord[EDITOR_INSTANCE_COORD_MAX + 8];

    /** enum Editor_InstanceNameState of `loc`, `exit_loc` and `npc`, and
     *  for a TAKEN loc where its trigger is declared. */
    int loc_state;
    int exit_state;
    int npc_state;
    char loc_where[EDITOR_INSTANCE_WHERE_FILE_MAX];
    char exit_where[EDITOR_INSTANCE_WHERE_FILE_MAX];
    /** A CATEGORY loc's category name, for the refusal. */
    char loc_category[EDITOR_INSTANCE_LOC_MAX];
    char exit_category[EDITOR_INSTANCE_LOC_MAX];
};

struct Editor_InstanceCheck
{
    int ok;
    /** Why not, or what Create will make. */
    char message[EDITOR_INSTANCE_MESSAGE_MAX];
    /** The squares' storage ("" = maps/). */
    char storage[EDITOR_DOC_STORAGE_MAX];
    int creates_lane;
    struct Editor_InstancePlan plan;
    /** The trigger's landing tile. */
    int entry_level;
    int entry_x;
    int entry_z;
    /** The exit block: 1 with the real-map tile it returns to. */
    int has_exit;
    int exit_level;
    int exit_x;
    int exit_z;
    /** The npc added on entering: 1 with its template tile. */
    int has_npc;
    int npc_level;
    int npc_x;
    int npc_z;
    /** Not a refusal: a name the catalog does not have yet. "" = none. */
    char note[EDITOR_INSTANCE_MESSAGE_MAX];
};

/**
 * Check `form` against the atlas: the sources, the destination (every
 * destination square free in EVERY storage, else refused with its owner
 * named), the storage, the name, the trigger's subject and landing tile,
 * the runtime chip, the exit (its loc, the real-map tile it returns to) and
 * the npc added on entering (its template tile). Returns check->ok; fills
 * check->plan when the sources and destination are good.
 */
int
Editor_InstanceValidate(
    const struct Editor_Picker* picker,
    const struct Editor_InstanceForm* form,
    struct Editor_InstanceCheck* check);

/** The trigger script's inputs, from a form that passed and its check. */
void
Editor_InstanceScriptOf(
    const struct Editor_InstanceForm* form,
    const struct Editor_InstanceCheck* check,
    struct Editor_InstanceScript* out);

/** The display name the .wma gives the map: "tutorial_cave" -> "Tutorial cave". */
void
Editor_InstanceDisplayName(
    const char* name,
    char* out,
    size_t out_size);

#endif
