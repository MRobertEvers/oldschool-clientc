#ifndef SRC_GAME_RS_HINT_ARROW_H
#define SRC_GAME_RS_HINT_ARROW_H

/*
 * The hint arrow: what the server is pointing at, in the one form every pass
 * that draws it reads.
 *
 * HINT_ARROW's type byte selects what its id and z mean: 1 is an npc by slot,
 * 10 a player by pid, and 2..6 a tile in absolute coordinates, where the type
 * also picks which point of the tile the arrow floats over (2 the centre, 3/4
 * the west/east edge, 5/6 the south/north edge). 255 clears it.
 *
 * Two foldings happen on the way in, so nothing downstream has to know the
 * wire's spellings:
 *
 *   - 255 becomes 0, the one "no arrow" value.
 *   - The four edge forms become RS_HINT_ARROW_COORD plus a fine-unit offset
 *     inside the tile (0, 64 or 128 on each axis; 64 is the centre). The
 *     offset is written for every packet, including the entity forms and the
 *     clear, where it means nothing.
 *
 * A packet overwrites every field, the clear included: a cleared arrow still
 * holds the id, z and height that packet carried.
 *
 * This does not know where an npc or a player is, what the scene's base tile
 * is, or whether the arrow is blinking this cycle. The caller looks the entity
 * up, passes the base, and decides what to draw.
 */

/*
 * HINT_ARROW's `type` byte: what the packet's `id`/`z` fields mean.
 *
 * The reference's own values -- `PktHintArrow` states them, LostCity's
 * `HintArrowEncoder` writes them and rev 239's own `class268.method6676`
 * reads them: 1 is an NPC slot, 2..6 a tile, 10 a player pid. 255 means
 * "clear" and is normalised to 0 by RS_HintArrow_Set, so 0 here is
 * simply "no arrow" and needs no name.
 *
 * These were 1=coord/2=npc until 2026-09-15, which no lane agreed with: the
 * embedded server mirrored the same two wrong numbers, so the pair only ever
 * talked to itself, and a real rev289 server pointing at an npc put the arrow
 * over whatever tile the slot number happened to name.
 *
 * The five tile forms differ only in where INSIDE the tile the arrow floats,
 * which the wire says by the type itself and the client keeps as a fine-unit
 * offset pair; `_COORD` is the centred one and the others normalise to it.
 */
#define RS_HINT_ARROW_NPC 1
#define RS_HINT_ARROW_COORD 2
/** The wire's edge forms. Never stored: Set folds them to RS_HINT_ARROW_COORD. */
#define RS_HINT_ARROW_COORD_WEST 3
#define RS_HINT_ARROW_COORD_EAST 4
#define RS_HINT_ARROW_COORD_SOUTH 5
#define RS_HINT_ARROW_COORD_NORTH 6
#define RS_HINT_ARROW_PLAYER 10
/** The wire's clear. Never stored: Set folds it to 0. */
#define RS_HINT_ARROW_WIRE_CLEAR 255

struct RS_HintArrow
{
    /** RS_HINT_ARROW_NPC / _COORD / _PLAYER, or 0 for none. */
    int type;
    int target; /* npc slot, player pid, or the absolute tile x */
    int tile_z;
    int height;
    /** Where in the tile a _COORD arrow floats, in fine units (64 is the
     *  centre). Meaningless for the two entity forms, which follow the
     *  entity. */
    int offset_x;
    int offset_z;
};

/** Every field zero: no arrow. */
void
RS_HintArrow_Clear(struct RS_HintArrow* arrow);

/**
 * One HINT_ARROW packet, as the wire carries it: its type byte, id, z and
 * height. Folds the type and the tile anchor as described above and overwrites
 * every field.
 */
void
RS_HintArrow_Set(
    struct RS_HintArrow* arrow,
    int wire_type,
    int id,
    int z,
    int height);

/**
 * A _COORD arrow's x / z in the scene's fine units: the tile relative to the
 * scene's base tile, times 128, plus the anchor offset. Only meaningful for
 * RS_HINT_ARROW_COORD.
 */
int
RS_HintArrow_CoordFineX(
    struct RS_HintArrow const* arrow,
    int base_x);
int
RS_HintArrow_CoordFineZ(
    struct RS_HintArrow const* arrow,
    int base_z);

#endif /* SRC_GAME_RS_HINT_ARROW_H */
