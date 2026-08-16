#include "dat2_entity_ops.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void
RSCache_EntityOpsInit(struct RSCache_EntityOps* ops)
{
    assert(ops);
    memset(ops, 0, sizeof(*ops));
}

void
RSCache_EntityOpsFreeInplace(struct RSCache_EntityOps* ops)
{
    int i;

    assert(ops);
    for( i = 0; i < RSCACHE_ENTITY_OPS_SLOTS; i++ )
    {
        free(ops->ops[i]);
        ops->ops[i] = NULL;
    }
    for( i = 0; i < ops->sub_ops_count; i++ )
        free(ops->sub_ops[i].text);
    free(ops->sub_ops);
    ops->sub_ops = NULL;
    ops->sub_ops_count = 0;

    for( i = 0; i < ops->cond_ops_count; i++ )
        free(ops->cond_ops[i].text);
    free(ops->cond_ops);
    ops->cond_ops = NULL;
    ops->cond_ops_count = 0;

    for( i = 0; i < ops->cond_sub_ops_count; i++ )
        free(ops->cond_sub_ops[i].text);
    free(ops->cond_sub_ops);
    ops->cond_sub_ops = NULL;
    ops->cond_sub_ops_count = 0;
}

static bool
entity_ops_is_hidden(const char* text)
{
    const char* hidden = "Hidden";
    int i;

    if( !text )
        return false;
    for( i = 0; hidden[i] != '\0'; i++ )
    {
        if( text[i] == '\0' )
            return false;
        if( tolower((unsigned char)text[i]) != tolower((unsigned char)hidden[i]) )
            return false;
    }
    return text[i] == '\0';
}

void
RSCache_EntityOpsDecodeOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer,
    int index)
{
    char* text;

    assert(ops);
    assert(buffer);
    assert(index >= 0 && index < RSCACHE_ENTITY_OPS_SLOTS);

    text = gcstring(buffer);
    free(ops->ops[index]);
    if( text && entity_ops_is_hidden(text) )
    {
        free(text);
        ops->ops[index] = NULL;
    }
    else
    {
        ops->ops[index] = text;
    }
}

void
RSCache_EntityOpsDecodeSubOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer)
{
    struct RSCache_EntitySubOp* grown;
    int index;
    int sub_id;

    assert(ops);
    assert(buffer);

    index = g1(buffer) & 0xFF;
    sub_id = g1(buffer) & 0xFF;

    grown = realloc(ops->sub_ops, (size_t)(ops->sub_ops_count + 1) * sizeof(*grown));
    assert(grown);
    ops->sub_ops = grown;
    ops->sub_ops[ops->sub_ops_count].index = index;
    ops->sub_ops[ops->sub_ops_count].sub_id = sub_id;
    ops->sub_ops[ops->sub_ops_count].text = gcstring(buffer);
    ops->sub_ops_count++;
}

void
RSCache_EntityOpsDecodeCondOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer)
{
    struct RSCache_EntityCondOp* grown;
    int index;

    assert(ops);
    assert(buffer);

    index = g1(buffer) & 0xFF;
    grown = realloc(ops->cond_ops, (size_t)(ops->cond_ops_count + 1) * sizeof(*grown));
    assert(grown);
    ops->cond_ops = grown;
    ops->cond_ops[ops->cond_ops_count].index = index;
    ops->cond_ops[ops->cond_ops_count].varp_id = g2(buffer);
    ops->cond_ops[ops->cond_ops_count].varbit_id = g2(buffer);
    ops->cond_ops[ops->cond_ops_count].min_value = g4(buffer);
    ops->cond_ops[ops->cond_ops_count].max_value = g4(buffer);
    ops->cond_ops[ops->cond_ops_count].text = gcstring(buffer);
    ops->cond_ops_count++;
}

void
RSCache_EntityOpsDecodeCondSubOp(
    struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer)
{
    struct RSCache_EntityCondSubOp* grown;
    int index;

    assert(ops);
    assert(buffer);

    index = g1(buffer) & 0xFF;
    grown =
        realloc(ops->cond_sub_ops, (size_t)(ops->cond_sub_ops_count + 1) * sizeof(*grown));
    assert(grown);
    ops->cond_sub_ops = grown;
    ops->cond_sub_ops[ops->cond_sub_ops_count].index = index;
    ops->cond_sub_ops[ops->cond_sub_ops_count].sub_id = g2(buffer);
    ops->cond_sub_ops[ops->cond_sub_ops_count].varp_id = g2(buffer);
    ops->cond_sub_ops[ops->cond_sub_ops_count].varbit_id = g2(buffer);
    ops->cond_sub_ops[ops->cond_sub_ops_count].min_value = g4(buffer);
    ops->cond_sub_ops[ops->cond_sub_ops_count].max_value = g4(buffer);
    ops->cond_sub_ops[ops->cond_sub_ops_count].text = gcstring(buffer);
    ops->cond_sub_ops_count++;
}

void
RSCache_EntityOpsEncode(
    const struct RSCache_EntityOps* ops,
    struct RSCache_Buffer* buffer,
    int op_base,
    int sub_op,
    int cond_op,
    int cond_sub_op)
{
    int i;

    assert(ops);
    assert(buffer);

    for( i = 0; i < RSCACHE_ENTITY_OPS_SLOTS; i++ )
    {
        if( !ops->ops[i] )
            continue;
        p1(buffer, op_base + i);
        pjstr(buffer, ops->ops[i], RSCACHE_JSTR_TERMINATOR_NULL);
    }

    for( i = 0; i < ops->sub_ops_count; i++ )
    {
        p1(buffer, sub_op);
        p1(buffer, ops->sub_ops[i].index);
        p1(buffer, ops->sub_ops[i].sub_id);
        pjstr(
            buffer,
            ops->sub_ops[i].text ? ops->sub_ops[i].text : "",
            RSCACHE_JSTR_TERMINATOR_NULL);
    }

    for( i = 0; i < ops->cond_ops_count; i++ )
    {
        p1(buffer, cond_op);
        p1(buffer, ops->cond_ops[i].index);
        p2(buffer, ops->cond_ops[i].varp_id);
        p2(buffer, ops->cond_ops[i].varbit_id);
        p4(buffer, ops->cond_ops[i].min_value);
        p4(buffer, ops->cond_ops[i].max_value);
        pjstr(
            buffer,
            ops->cond_ops[i].text ? ops->cond_ops[i].text : "",
            RSCACHE_JSTR_TERMINATOR_NULL);
    }

    for( i = 0; i < ops->cond_sub_ops_count; i++ )
    {
        p1(buffer, cond_sub_op);
        p1(buffer, ops->cond_sub_ops[i].index);
        p2(buffer, ops->cond_sub_ops[i].sub_id);
        p2(buffer, ops->cond_sub_ops[i].varp_id);
        p2(buffer, ops->cond_sub_ops[i].varbit_id);
        p4(buffer, ops->cond_sub_ops[i].min_value);
        p4(buffer, ops->cond_sub_ops[i].max_value);
        pjstr(
            buffer,
            ops->cond_sub_ops[i].text ? ops->cond_sub_ops[i].text : "",
            RSCACHE_JSTR_TERMINATOR_NULL);
    }
}

bool
RSCache_EntityOpsHasAnyOp(const struct RSCache_EntityOps* ops)
{
    int i;

    assert(ops);
    for( i = 0; i < RSCACHE_ENTITY_OPS_SLOTS; i++ )
    {
        if( ops->ops[i] )
            return true;
    }
    return false;
}

uint32_t
RSCache_EntityOpsBound(const struct RSCache_EntityOps* ops)
{
    /*
     * Derived from RSCache_EntityOpsEncode above, field for field, so the two
     * cannot drift: an opcode byte per record written, then that record's fixed
     * scalars, then its NUL-terminated string.
     *
     *   ops slot     1 + str
     *   sub op       1 + 1 + 1                     + str
     *   cond op      1 + 1 + 2 + 2 + 4 + 4         + str
     *   cond sub op  1 + 1 + 2 + 2 + 2 + 4 + 4     + str
     */
    uint32_t need = 0;
    int i;

    if( !ops )
        return 0;
    for( i = 0; i < RSCACHE_ENTITY_OPS_SLOTS; i++ )
    {
        if( ops->ops[i] )
            need += 1u + (uint32_t)strlen(ops->ops[i]) + 1u;
    }
    for( i = 0; i < ops->sub_ops_count; i++ )
    {
        const char* text = ops->sub_ops[i].text;

        need += 3u + (text ? (uint32_t)strlen(text) : 0u) + 1u;
    }
    for( i = 0; i < ops->cond_ops_count; i++ )
    {
        const char* text = ops->cond_ops[i].text;

        need += 14u + (text ? (uint32_t)strlen(text) : 0u) + 1u;
    }
    for( i = 0; i < ops->cond_sub_ops_count; i++ )
    {
        const char* text = ops->cond_sub_ops[i].text;

        need += 16u + (text ? (uint32_t)strlen(text) : 0u) + 1u;
    }
    return need;
}
