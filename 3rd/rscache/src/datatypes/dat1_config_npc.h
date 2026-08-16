#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_NPC_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_NPC_H

#include "../rsbuffer.h"

#include <stdbool.h>

struct RSCache_Dat1ConfigNpc
{
    char* name;
    char* desc;
    int size;
    int* models;
    int models_count;
    int* heads;
    int heads_count;
    int readyanim;
    int walkanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    bool animHasAlpha;
    int* recol_s;
    int* recol_d;
    int recol_count;
    char* op[5]; // Options 30-34
    int resizex;
    int resizey;
    int resizez;
    bool minimap;
    int vislevel;
    int resizeh;
    int resizev;
    bool alwaysontop;
    int headicon;
    int ambient;
    int contrast;
    /** NpcType.turnspeed (opcode 103), default 32; 0 = never turns. */
    int turnspeed;
};

struct RSCache_Dat1ConfigNpcList
{
    struct RSCache_Dat1ConfigNpc** npcs;
    int npcs_count;
};

struct RSCache_Dat1ConfigNpcList*
RSCache_Dat1ConfigNpcListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/** Decode a single NPC from a buffer positioned at the start of an entry. Ownership is
 * transferred to the caller. */
struct RSCache_Dat1ConfigNpc*
RSCache_Dat1ConfigNpcDecodeOne(struct RSCache_Buffer* buffer);

/**
 * Bring a zeroed allocation to this type's defaults.
 *
 * Nineteen fields here differ from zero, and every one of them is a value the
 * type also uses legitimately: `size` 1, the five anim ids at -1 where 0 is a
 * real sequence, `minimap` true, `resizeh`/`resizev` 128, `turnspeed` 32 where
 * 0 means "never turns". Zero-initialising instead would decode with no error
 * and give every NPC sequence 0, a 0-scale model and a frozen heading.
 */
void
RSCache_Dat1ConfigNpcInit(struct RSCache_Dat1ConfigNpc* npc);

/**
 * Handle one opcode, advancing `buffer` past its payload.
 *
 * False means "not mine", which ends the stream — the width of an unknown
 * opcode cannot be guessed. See `opcode_codec.h`.
 */
bool
RSCache_Dat1ConfigNpcDecodeOp(
    struct RSCache_Dat1ConfigNpc* npc,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Release what the record owns, leaving the struct itself to the caller. */
void
RSCache_Dat1ConfigNpcFreeInplace(struct RSCache_Dat1ConfigNpc* npc);

void
RSCache_Dat1ConfigNpcFree(struct RSCache_Dat1ConfigNpc* npc);

void
RSCache_Dat1ConfigNpcListFree(struct RSCache_Dat1ConfigNpcList* list);

/** Byte count needed to encode `npc`, including the trailing opcode 0. */
uint32_t
RSCache_Dat1ConfigNpcEncodeBound(const struct RSCache_Dat1ConfigNpc* npc);

/**
 * Encode one NPC record. Strings are newline-terminated. Only non-default fields
 * are written. Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_Dat1ConfigNpcEncode(
    const struct RSCache_Dat1ConfigNpc* npc,
    uint8_t* out,
    uint32_t out_capacity);

#endif
