#ifndef TORIRS_PLUGIN_NXT_COX_LAYOUT_H
#define TORIRS_PLUGIN_NXT_COX_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The Chambers of Xeric helper's room list, worked out the way the C++ client
 * has to: from the instance's TEMPLATE CHUNKS and nothing else.
 *
 * No var carries the layout. What the client is told is the dynamic rebuild --
 * for every zone of the instance, which template zone was copied into it and
 * how far it was turned -- and every CoX room is a fixed 32x32 cell of the
 * template map, so the template zone names the room:
 *
 *   template tile z / 32  -> which room row     (163..178, see the table)
 *   template plane        -> which room of that row
 *   template tile x / 32  -> which authored DOOR VARIANT (102 / 103 / 104)
 *
 * The three variants are the three things a path can do in a room -- turn
 * left, go straight, turn right -- authored for a path heading north. Turned
 * by the zone's rotation, a room's variant and rotation give the side it is
 * left by, which is the next room. So the path is walked, not guessed.
 *
 * Template coordinates: the server's own table (cox_layout.rs2,
 * ~cox_room_template / ~cox_room_src_plane / ~cox_room_src_cz), which agrees
 * with Zenyte's RaidRoom and RuneLite's InstanceTemplates.
 */

/** The helper's room kinds. `name` and `category` are the helper's text. */
struct NxtCoxRoomKind
{
    char const* category;
    char const* name;
    /** A start room is walked through but never listed (the helper's own
     *  example list begins at the first room after it). */
    bool listed;
};

/** The kind index for a template zone, or -1 for a zone that is no CoX room
 *  (the Olm chamber, anything outside the raid). */
int NxtCox_RoomKindOf(int template_x, int template_z, int template_plane);

struct NxtCoxRoomKind const* NxtCox_RoomKind(int kind);

/** 0, 1, 2 for the counter-clockwise, through and clockwise door variant, or
 *  -1 for a template x that is no raid room's. */
int NxtCox_RoomVariantOf(int template_x);

/** The variant's letter as the helper prints it. */
char NxtCox_VariantLetter(int variant);

/** The compass side (0 north, 1 east, 2 south, 3 west) a room of `variant`
 *  turned `rotation` clockwise quarter-turns is LEFT by. */
int NxtCox_ExitDir(int variant, int rotation);

/** Room cells remembered per raid: two floors of eight, with room to spare for
 *  a revision that lays out more. */
#define NXT_COX_MAX_CELLS 48
#define NXT_COX_MAX_LEVELS 4

struct NxtCoxCell
{
    /** Absolute room cell: (instance tile) / 32, and the level. */
    int cell_x;
    int cell_z;
    int8_t level;
    int8_t kind;
    int8_t variant;
    int8_t rotation;
};

struct NxtCoxLayout
{
    struct NxtCoxCell cells[NXT_COX_MAX_CELLS];
    int count;
    /** The levels the player has stood on, in the order first stood on.
     *  @see NxtCoxLayout_Order for why this decides the floor order. */
    int8_t visited[NXT_COX_MAX_LEVELS];
    int visited_count;
};

void NxtCoxLayout_Reset(struct NxtCoxLayout* layout);

/** Remember a room seen in a rebuild. A cell already known is replaced: a
 *  rebuild is the server's word on what is there now. Returns true when the
 *  layout changed. */
bool NxtCoxLayout_Note(
    struct NxtCoxLayout* layout,
    int level,
    int cell_x,
    int cell_z,
    int kind,
    int variant,
    int rotation);

/** Whether `kind` is the raid's own start room, of which a raid has one. */
bool NxtCox_RoomIsRaidStart(int kind);

/** Whether the layout remembers a raid start room other than the one at this
 *  cell: a scene that shows another start is another raid. A reroll goes from
 *  raid to raid with no scene between, so nothing else says the last one is
 *  over. */
bool NxtCoxLayout_IsAnotherRaid(
    struct NxtCoxLayout const* layout,
    int level,
    int cell_x,
    int cell_z);

/** The player stood on `level`. */
void NxtCoxLayout_Visit(struct NxtCoxLayout* layout, int level);

/**
 * The listed rooms in path order, as indices into `layout->cells`.
 *
 * Floors in the order the player first stood on them, then any floor not yet
 * stood on in level order. Within a floor, every chain is walked from its
 * head -- a room no known room leaves into -- along each room's exit, so a
 * floor seen only in part still lists what was seen in order.
 *
 * Returns the count written.
 */
int NxtCoxLayout_Order(struct NxtCoxLayout const* layout, int* out, int max);

#endif /* TORIRS_PLUGIN_NXT_COX_LAYOUT_H */
