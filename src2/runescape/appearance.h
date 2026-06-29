#ifndef RUNESCAPE_APPEARANCE_H
#define RUNESCAPE_APPEARANCE_H

#include <stdint.h>

struct Dat1BuildCache;

#define RUNESCAPE_APPEARANCE_SLOT_COUNT 12

/* Example player outfit from src/server/server.c (rune scimitar + IDK gear). */
extern const int RUNESCAPE_EXAMPLE_PLAYER_APPEARANCE[RUNESCAPE_APPEARANCE_SLOT_COUNT];

int
runescape_appearance_collect_model_ids(
    struct Dat1BuildCache* dat1_bc,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int* model_ids,
    int capacity);

#endif
