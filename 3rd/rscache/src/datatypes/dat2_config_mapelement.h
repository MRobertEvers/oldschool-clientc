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

void
RSCache_MapElementFreeInplace(struct RSCache_MapElement* entry);

#endif
