#include "cs2_command.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cs2_command.gen.h"

#define CS2_COMMAND_TABLE_COUNT ((int)(sizeof(cs2_command_table) / sizeof(cs2_command_table[0])))

/* Run-time overrides, consulted ahead of the generated table. Flat and small:
 * a caller installs a handful, and lookups are frequent enough that scanning a
 * few entries beats anything with a hash in it. */
#define CS2_COMMAND_MAX_OVERRIDES 128
#define CS2_COMMAND_MAX_OVERRIDE_PROTOS 16

static struct
{
    int opcode;
    struct RSCache_CS2_CommandInfo info;
    char name[64];
} cs2_command_overrides[CS2_COMMAND_MAX_OVERRIDES];
static int cs2_command_override_count = 0;

/* An overridden row's offsets index this pool instead of the generated one, so
 * RSCache_CS2_CommandArg/Def stay a single indexed read either way. */
static enum RSCache_CS2_ProtoId
    cs2_command_override_pool[CS2_COMMAND_MAX_OVERRIDES * 2 * CS2_COMMAND_MAX_OVERRIDE_PROTOS];
static int cs2_command_override_pool_used = 0;

static bool
cs2_command_is_override(const struct RSCache_CS2_CommandInfo* info)
{
    for( int i = 0; i < cs2_command_override_count; i++ )
    {
        if( &cs2_command_overrides[i].info == info )
            return true;
    }
    return false;
}

void
RSCache_CS2_CommandClearOverrides(void)
{
    cs2_command_override_count = 0;
    cs2_command_override_pool_used = 0;
}

bool
RSCache_CS2_CommandOverride(
    int opcode,
    const char* name,
    const enum RSCache_CS2_ProtoId* args,
    int arg_count,
    const enum RSCache_CS2_ProtoId* defs,
    int def_count,
    bool dot_capable)
{
    for( int i = 0; i < cs2_command_override_count; i++ )
    {
        if( cs2_command_overrides[i].opcode != opcode )
            continue;
        cs2_command_overrides[i] = cs2_command_overrides[cs2_command_override_count - 1];
        /* `info.name` points into the entry's own `name` array, so a plain
         * struct copy leaves it aimed at the slot it came from. Re-point it, or
         * every later lookup reports the moved entry's name. */
        cs2_command_overrides[i].info.name = cs2_command_overrides[i].name;
        cs2_command_override_count--;
        break;
    }
    if( arg_count < 0 )
        return true;
    if( cs2_command_override_count == CS2_COMMAND_MAX_OVERRIDES )
        return false;
    if( arg_count > CS2_COMMAND_MAX_OVERRIDE_PROTOS ||
        def_count > CS2_COMMAND_MAX_OVERRIDE_PROTOS )
        return false;
    /* The pool is never compacted, so a caller that reinstalls repeatedly (the
     * inference search does, thousands of times) resets it when it fills. Safe
     * because an override's protos are only read while it is installed. */
    if( cs2_command_override_pool_used + arg_count + def_count >
        (int)(sizeof(cs2_command_override_pool) / sizeof(cs2_command_override_pool[0])) )
        cs2_command_override_pool_used = 0;

    int index = cs2_command_override_count++;
    cs2_command_overrides[index].opcode = opcode;

    const char* existing = RSCache_CS2_CommandName(opcode);
    const char* chosen = name ? name : existing;
    if( chosen )
        snprintf(
            cs2_command_overrides[index].name,
            sizeof(cs2_command_overrides[index].name),
            "%s",
            chosen);
    else
        snprintf(
            cs2_command_overrides[index].name,
            sizeof(cs2_command_overrides[index].name),
            "_%d",
            opcode);

    int arg_offset = cs2_command_override_pool_used;
    for( int i = 0; i < arg_count; i++ )
        cs2_command_override_pool[cs2_command_override_pool_used++] = args[i];
    int def_offset = cs2_command_override_pool_used;
    for( int i = 0; i < def_count; i++ )
        cs2_command_override_pool[cs2_command_override_pool_used++] = defs[i];

    struct RSCache_CS2_CommandInfo* info = &cs2_command_overrides[index].info;
    info->name = cs2_command_overrides[index].name;
    info->kind = RSCACHE_CS2_CMD_BASIC;
    info->arg_offset = arg_offset;
    info->arg_count = arg_count;
    info->def_offset = def_offset;
    info->def_count = def_count;
    info->dot_capable = dot_capable;
    info->extra = 0;
    return true;
}

const struct RSCache_CS2_CommandInfo*
RSCache_CS2_CommandGet(int opcode)
{
    for( int i = 0; i < cs2_command_override_count; i++ )
    {
        if( cs2_command_overrides[i].opcode == opcode )
            return &cs2_command_overrides[i].info;
    }
    if( opcode < 0 || opcode >= CS2_COMMAND_TABLE_COUNT )
        return NULL;
    const struct RSCache_CS2_CommandInfo* info = &cs2_command_table[opcode];
    if( !info->name && info->kind == RSCACHE_CS2_CMD_UNKNOWN )
        return NULL;
    return info;
}

const char*
RSCache_CS2_CommandName(int opcode)
{
    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
    return info ? info->name : NULL;
}

int
RSCache_CS2_CommandOfName(const char* name)
{
    if( !name )
        return -1;
    for( int i = 0; i < CS2_COMMAND_TABLE_COUNT; i++ )
    {
        const char* candidate = cs2_command_table[i].name;
        if( !candidate )
            continue;
        /* The table stores lowercase; source may be written in any case. */
        const char* a = candidate;
        const char* b = name;
        while( *a && *b )
        {
            char ca = *a;
            char cb = *b;
            if( cb >= 'A' && cb <= 'Z' )
                cb = (char)(cb - 'A' + 'a');
            if( ca != cb )
                break;
            a++;
            b++;
        }
        if( !*a && !*b )
            return i;
    }
    return -1;
}

enum RSCache_CS2_ProtoId
RSCache_CS2_CommandArg(const struct RSCache_CS2_CommandInfo* info, int index)
{
    assert(info && index >= 0 && index < info->arg_count);
    if( cs2_command_is_override(info) )
        return cs2_command_override_pool[info->arg_offset + index];
    return cs2_proto_pool[info->arg_offset + index];
}

enum RSCache_CS2_ProtoId
RSCache_CS2_CommandDef(const struct RSCache_CS2_CommandInfo* info, int index)
{
    assert(info && index >= 0 && index < info->def_count);
    if( cs2_command_is_override(info) )
        return cs2_command_override_pool[info->def_offset + index];
    return cs2_proto_pool[info->def_offset + index];
}

const char*
RSCache_CS2_CommandCalcInfix(int opcode)
{
    switch( opcode )
    {
    case RSCACHE_CS2_OP_ADD:
        return "+";
    case RSCACHE_CS2_OP_SUB:
        return "-";
    case RSCACHE_CS2_OP_MULTIPLY:
        return "*";
    case RSCACHE_CS2_OP_DIV:
        return "/";
    case RSCACHE_CS2_OP_MOD:
        return "%";
    case RSCACHE_CS2_OP_AND:
        return "&";
    case RSCACHE_CS2_OP_OR:
        return "|";
    default:
        return NULL;
    }
}

const char*
RSCache_CS2_CommandBranchInfix(int opcode)
{
    switch( opcode )
    {
    case RSCACHE_CS2_OP_BRANCH_EQUALS:
        return "=";
    case RSCACHE_CS2_OP_BRANCH_GREATER_THAN:
        return ">";
    case RSCACHE_CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
        return ">=";
    case RSCACHE_CS2_OP_BRANCH_LESS_THAN:
        return "<";
    case RSCACHE_CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
        return "<=";
    case RSCACHE_CS2_OP_BRANCH_NOT:
        return "!";
    case RSCACHE_CS2_OP_SS_OR:
        return "|";
    case RSCACHE_CS2_OP_SS_AND:
        return "&";
    default:
        return NULL;
    }
}

int
RSCache_CS2_CommandPrecedence(int opcode)
{
    switch( opcode )
    {
    case RSCACHE_CS2_OP_MULTIPLY:
    case RSCACHE_CS2_OP_DIV:
    case RSCACHE_CS2_OP_MOD:
        return 1;
    case RSCACHE_CS2_OP_ADD:
    case RSCACHE_CS2_OP_SUB:
        return 2;
    case RSCACHE_CS2_OP_BRANCH_GREATER_THAN:
    case RSCACHE_CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
    case RSCACHE_CS2_OP_BRANCH_LESS_THAN:
    case RSCACHE_CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
        return 3;
    case RSCACHE_CS2_OP_BRANCH_EQUALS:
    case RSCACHE_CS2_OP_BRANCH_NOT:
        return 4;
    case RSCACHE_CS2_OP_AND:
        return 5;
    case RSCACHE_CS2_OP_OR:
        return 6;
    case RSCACHE_CS2_OP_SS_AND:
        return 7;
    case RSCACHE_CS2_OP_SS_OR:
        return 8;
    default:
        return 0;
    }
}
