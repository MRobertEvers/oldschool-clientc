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

static bool
cs2_command_name_equals(const char* candidate, const char* name)
{
    while( *candidate && *name )
    {
        char ca = *candidate;
        char cb = *name;
        if( cb >= 'A' && cb <= 'Z' )
            cb = (char)(cb - 'A' + 'a');
        if( ca != cb )
            return false;
        candidate++;
        name++;
    }
    return !*candidate && !*name;
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
        if( cs2_command_name_equals(candidate, name) )
            return i;
    }

    /* Semantic spellings emitted by this tree before the canonical
     * osrs-cache command catalogue was imported. Keep them as source aliases
     * just like the numeric compatibility form below. */
    static const struct
    {
        const char* name;
        int opcode;
    } aliases[] = {
        { "activeplayer_setlocal", 6901 },
        { "activeplayer_getroutelength", 6902 },
        { "activeplayer_getroutecoord", 6903 },
        { "activeplayer_getuid", 6904 },
        { "localplayer_getuid", 6905 },
        { "highlight_opgroup_setup", 7040 },
        { "highlight_opgroup_on", 7041 },
        { "highlight_opgroup_off", 7042 },
        { "highlight_opgroup_get", 7043 },
        { "highlight_opgroup_clear", 7044 },
    };
    for( size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++ )
        if( cs2_command_name_equals(aliases[i].name, name) )
            return aliases[i].opcode;

    /* Numeric command spellings are the decompiler's compatibility format
     * for opcodes that were unnamed at the time source was produced.  Once an
     * opcode gains a semantic name the generated row changes, but that should
     * not make an older source tree stop compiling.  Accept `_1234` only when
     * that opcode has a real command row; arbitrary numeric identifiers remain
     * errors.  The decompiler still prints the row's current canonical name. */
    if( name[0] == '_' && name[1] >= '0' && name[1] <= '9' )
    {
        int opcode = 0;
        for( const char* p = name + 1; *p; p++ )
        {
            if( *p < '0' || *p > '9' )
                return -1;
            if( opcode > (CS2_COMMAND_TABLE_COUNT - 1 - (*p - '0')) / 10 )
                return -1;
            opcode = opcode * 10 + (*p - '0');
        }
        if( RSCache_CS2_CommandGet(opcode) )
            return opcode;
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
