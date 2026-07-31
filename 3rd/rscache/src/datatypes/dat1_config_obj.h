#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_OBJ_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_OBJ_H

#include "../rsbuffer.h"

#include <stdbool.h>

struct RSCache_Dat1ConfigObj
{
    int model;
    char* name;
    char* desc;
    int* recol_s;
    int* recol_d;
    int recol_count;
    int zoom2d;
    int xan2d;
    int yan2d;
    int zan2d;
    int xof2d;
    int yof2d;
    bool code9;
    int code10;
    bool stackable;
    int cost;
    bool members;
    int manwearOffsetY;
    int womanwearOffsetY;
    int manwear;
    int manwear2;
    int womanwear;
    int womanwear2;
    int manwear3;
    int womanwear3;
    int manhead;
    int manhead2;
    int womanhead;
    int womanhead2;
    int certlink;
    int certtemplate;
    int resizex;
    int resizey;
    int resizez;
    int ambient;
    int contrast;
    int* countobj;
    int* countco;
    int countobj_count;
    char* op[5];  // Options 30-34
    char* iop[5]; // Inventory options 35-39
    /**
     * The opcodes in the order the record carried them.
     *
     * Replayed by the encoder. dat1 config records are not sorted — see the note
     * in `dat1_config_idk.h` — and the order is not recoverable from the fields.
     */
    int opcodes[128];
    int opcode_count;
};

struct RSCache_Dat1ConfigObjList
{
    struct RSCache_Dat1ConfigObj** objs;
    int objs_count;
};

struct RSCache_Dat1ConfigObjList*
RSCache_Dat1ConfigObjListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/** Decode a single obj from a raw data buffer. Ownership is transferred to the caller. */
struct RSCache_Dat1ConfigObj*
RSCache_Dat1ConfigObjDecodeOne(void* data, int size);

/**
 * Bring a zeroed allocation to this type's defaults.
 *
 * Ten of them are non-zero and none is cosmetic: `zoom2d` 2000, `cost` 1,
 * `resizex/y/z` 128, and the wearable/cert ids at -1 where 0 is model 0 and obj
 * 0. A record that skipped this would decode with no error and render at the
 * wrong scale, price at 0, and link every uncertificated item to obj 0.
 */
void
RSCache_Dat1ConfigObjInit(struct RSCache_Dat1ConfigObj* obj);

/**
 * Handle one opcode, advancing `buffer` past its payload.
 *
 * False means "not mine", which ends the stream — the width of an unknown
 * opcode cannot be guessed. See `opcode_codec.h`.
 */
bool
RSCache_Dat1ConfigObjDecodeOp(
    struct RSCache_Dat1ConfigObj* obj,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Release what the record owns, leaving the struct itself to the caller. */
void
RSCache_Dat1ConfigObjFreeInplace(struct RSCache_Dat1ConfigObj* obj);

/**
 * Encode one dat1 obj record, replaying its decoded opcode order.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_Dat1ConfigObjEncode(
    const struct RSCache_Dat1ConfigObj* obj,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `RSCache_Dat1ConfigObjEncode` will write. */
uint32_t
RSCache_Dat1ConfigObjEncodeBound(const struct RSCache_Dat1ConfigObj* obj);

void
RSCache_Dat1ConfigObjFree(struct RSCache_Dat1ConfigObj* obj);

#endif
