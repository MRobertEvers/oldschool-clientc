#ifndef RUNESCAPE_APPEARANCE_H
#define RUNESCAPE_APPEARANCE_H

#include <stdint.h>

struct Dat1BuildCache;
struct Dat2BuildCache;

#define RUNESCAPE_APPEARANCE_SLOT_COUNT 12

/* Fallback player body model when appearance resolves to no pieces. */
#define RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID 230

/* Example player outfit from src/server/server.c (rune scimitar + IDK gear). */
extern const int RUNESCAPE_EXAMPLE_PLAYER_APPEARANCE[RUNESCAPE_APPEARANCE_SLOT_COUNT];

/* Standard human animation IDs from src/server/server.c. */
#define RUNESCAPE_EXAMPLE_PLAYER_READYANIM 808
#define RUNESCAPE_EXAMPLE_PLAYER_WALKANIM 819
#define RUNESCAPE_EXAMPLE_PLAYER_TURNANIM 823
#define RUNESCAPE_EXAMPLE_PLAYER_RUNANIM 824
#define RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_B 820
#define RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_R 821
#define RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_L 822

int
runescape_appearance_collect_model_ids(
    struct Dat1BuildCache* dat1_bc,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int* model_ids,
    int capacity);

int
runescape_appearance_collect_model_ids_dat2(
    struct Dat2BuildCache* dat2_bc,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int* model_ids,
    int capacity);

void
runescape_appearance_collect_config_ids(
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int* idk_ids,
    int* idk_count,
    int idk_capacity,
    int* obj_ids,
    int* obj_count,
    int obj_capacity);

#endif
