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

#define EDITOR_INSTANCE_MESSAGE_MAX 160

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
};

/**
 * Check `form` against the atlas: the sources, the destination (every
 * destination square free in EVERY storage, else refused with its owner
 * named), the storage, the name, the trigger's subject and landing tile,
 * the runtime chip. Returns check->ok; fills check->plan when the sources
 * and destination are good.
 */
int
Editor_InstanceValidate(
    const struct Editor_Picker* picker,
    const struct Editor_InstanceForm* form,
    struct Editor_InstanceCheck* check);

/** The display name the .wma gives the map: "tutorial_cave" -> "Tutorial cave". */
void
Editor_InstanceDisplayName(
    const char* name,
    char* out,
    size_t out_size);

#endif
