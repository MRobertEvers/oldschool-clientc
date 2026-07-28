#include "cs2_command.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cs2_command.gen.h"

#define CS2_COMMAND_TABLE_COUNT ((int)(sizeof(cs2_command_table) / sizeof(cs2_command_table[0])))

const struct RSCache_CS2_CommandInfo*
RSCache_CS2_CommandGet(int opcode)
{
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
    return cs2_proto_pool[info->arg_offset + index];
}

enum RSCache_CS2_ProtoId
RSCache_CS2_CommandDef(const struct RSCache_CS2_CommandInfo* info, int index)
{
    assert(info && index >= 0 && index < info->def_count);
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
