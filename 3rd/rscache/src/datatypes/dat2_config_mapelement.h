#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_MAPELEMENT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_MAPELEMENT_H

#include "../rsbuffer.h"

/**
 * Map element config ("mapFunctions", config group 35) — the icon, label and
 * category of one world map element. Only the fields the client reads back
 * through the MEC_* script opcodes are kept; the rest are consumed to keep the
 * record aligned.
 */
struct RSCache_MapElement
{
    int id;
    char* name;
    int text_size;
    int category;
    int sprite_id;
};

void
RSCache_MapElementDecodeInplace(
    struct RSCache_MapElement* entry,
    const void* data,
    int data_size);

/**
 * Encode a map element from the four fields the decoder retains.
 *
 * **Deliberately lossy.** The decoder consumes most of this record's two dozen
 * opcodes without storing them, so an encode can only reproduce the sprite, name,
 * text size and category. The output is a readable map element, not a copy of the
 * input: hover sprites, colours, visibility varbits, ops and params are gone by
 * the time this is called. Byte-exact round-trip is impossible here by
 * construction — extend the decoder first if a faithful copy is needed.
 */
uint32_t
RSCache_MapElementEncode(
    const struct RSCache_MapElement* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_MapElementFreeInplace(struct RSCache_MapElement* entry);

#endif
