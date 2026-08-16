#ifndef RSCACHE_DATATYPES_DAT2_ENTITY_OPS_H
#define RSCACHE_DATATYPES_DAT2_ENTITY_OPS_H

#include "../rsbuffer.h"

#include <stdbool.h>

/*
 * Rev 237 entity menu ops — mirrors RuneLite's EntityOpsDefinition /
 * EntityOpsLoader.
 *
 * Plain ops (opcodes 30-34 on npc/loc/obj) stay as five string slots so existing
 * `actions[]` consumers keep working; the decoder writes both. Sub-ops and
 * conditional forms are stored as ordered lists so encode can re-emit each
 * occurrence as its own opcode.
 */

#define RSCACHE_ENTITY_OPS_SLOTS 5

struct RSCache_EntitySubOp
{
    int index;
    int sub_id;
    char* text;
};

struct RSCache_EntityCondOp
{
    int index;
    int varp_id;
    int varbit_id;
    int min_value;
    int max_value;
    char* text;
};

struct RSCache_EntityCondSubOp
{
    int index;
    int sub_id;
    int varp_id;
    int varbit_id;
    int min_value;
    int max_value;
    char* text;
};

struct RSCache_EntityOps
{
    char* ops[RSCACHE_ENTITY_OPS_SLOTS];
    struct RSCache_EntitySubOp* sub_ops;
    int sub_ops_count;
    struct RSCache_EntityCondOp* cond_ops;
    int cond_ops_count;
    struct RSCache_EntityCondSubOp* cond_sub_ops;
    int cond_sub_ops_count;
};

void
RSCache_EntityOpsInit(struct RSCache_EntityOps* ops);

void
RSCache_EntityOpsFreeInplace(struct RSCache_EntityOps* ops);

/** Read a plain op string into slot `index`. "Hidden" clears the slot. */
void
RSCache_EntityOpsDecodeOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer,
    int index);

void
RSCache_EntityOpsDecodeSubOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer);

void
RSCache_EntityOpsDecodeCondOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer);

void
RSCache_EntityOpsDecodeCondSubOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer);

/**
 * Emit every stored entry. `op_base` is the first plain-op opcode (30 for
 * npc/loc/obj). `sub_op`/`cond_op`/`cond_sub_op` are the dedicated opcodes
 * (npc 251/252/253, loc 100/101/102, obj 200/201/202).
 */
void
RSCache_EntityOpsEncode(
    const struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer,
    int op_base,
    int sub_op,
    int cond_op,
    int cond_sub_op);

/** True when any plain op slot is non-NULL (used by loc wallOrDoor). */
bool
RSCache_EntityOpsHasAnyOp(const struct RSCache_EntityOps* ops);

/** Exactly the bytes `RSCache_EntityOpsEncode` will write, derived from it. */
uint32_t
RSCache_EntityOpsBound(const struct RSCache_EntityOps* ops);

#endif
