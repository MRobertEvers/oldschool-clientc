#include "cs2_types.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

struct cs2_type_info
{
    const char* literal;
    uint8_t desc;
};

/* Descriptor bytes are windows-1252, which is what the wire carries: mapelement
 * is 0xB5 (the micro sign), not a multi-byte UTF-8 sequence. Writing it as a
 * hex escape keeps the table correct regardless of this file's own encoding. */
static const struct cs2_type_info cs2_types[RSCACHE_CS2_TYPE_COUNT] = {
    [RSCACHE_CS2_TYPE_AREA] = { "area", 'R' },
    [RSCACHE_CS2_TYPE_BOOLEAN] = { "boolean", '1' },
    [RSCACHE_CS2_TYPE_CATEGORY] = { "category", 'y' },
    [RSCACHE_CS2_TYPE_CHAR] = { "char", 'z' },
    [RSCACHE_CS2_TYPE_COMPONENT] = { "component", 'I' },
    [RSCACHE_CS2_TYPE_COORD] = { "coord", 'c' },
    [RSCACHE_CS2_TYPE_ENUM] = { "enum", 'g' },
    [RSCACHE_CS2_TYPE_FONTMETRICS] = { "fontmetrics", 'f' },
    [RSCACHE_CS2_TYPE_GRAPHIC] = { "graphic", 'd' },
    [RSCACHE_CS2_TYPE_INT] = { "int", 'i' },
    [RSCACHE_CS2_TYPE_INTERFACE] = { "interface", 'a' },
    [RSCACHE_CS2_TYPE_INV] = { "inv", 'v' },
    [RSCACHE_CS2_TYPE_LOC] = { "loc", 'l' },
    [RSCACHE_CS2_TYPE_MAPAREA] = { "maparea", '`' },
    [RSCACHE_CS2_TYPE_MAPELEMENT] = { "mapelement", 0xB5 },
    [RSCACHE_CS2_TYPE_MODEL] = { "model", 'm' },
    [RSCACHE_CS2_TYPE_NAMEDOBJ] = { "namedobj", 'O' },
    [RSCACHE_CS2_TYPE_NPC] = { "npc", 'n' },
    [RSCACHE_CS2_TYPE_OBJ] = { "obj", 'o' },
    [RSCACHE_CS2_TYPE_PARAM] = { "param", 0 },
    [RSCACHE_CS2_TYPE_SEQ] = { "seq", 'A' },
    [RSCACHE_CS2_TYPE_SPOTANIM] = { "spotanim", 't' },
    [RSCACHE_CS2_TYPE_STAT] = { "stat", 'S' },
    [RSCACHE_CS2_TYPE_STRING] = { "string", 's' },
    [RSCACHE_CS2_TYPE_STRUCT] = { "struct", 'J' },
    [RSCACHE_CS2_TYPE_SYNTH] = { "synth", 'P' },
    [RSCACHE_CS2_TYPE_NEWVAR] = { "newvar", '-' },
    [RSCACHE_CS2_TYPE_NPC_UID] = { "npc_uid", 'u' },
    [RSCACHE_CS2_TYPE_PLAYER_UID] = { "player_uid", 'p' },
    [RSCACHE_CS2_TYPE_TYPE] = { "type", 0 },
    [RSCACHE_CS2_TYPE_DBROW] = { "dbrow", 0xD0 },
};

const char*
RSCache_CS2_TypeLiteral(enum RSCache_CS2_Type type)
{
    if( type < 0 || type >= RSCACHE_CS2_TYPE_COUNT )
        return "?";
    return cs2_types[type].literal;
}

uint8_t
RSCache_CS2_TypeDesc(enum RSCache_CS2_Type type)
{
    if( type < 0 || type >= RSCACHE_CS2_TYPE_COUNT )
        return 0;
    return cs2_types[type].desc;
}

enum RSCache_CS2_Type
RSCache_CS2_TypeOfDesc(uint8_t desc)
{
    if( desc == 0 )
        return RSCACHE_CS2_TYPE_NONE;
    for( int i = 0; i < RSCACHE_CS2_TYPE_COUNT; i++ )
    {
        if( cs2_types[i].desc == desc )
            return (enum RSCache_CS2_Type)i;
    }
    return RSCACHE_CS2_TYPE_NONE;
}

enum RSCache_CS2_Type
RSCache_CS2_TypeOfDescAuto(uint8_t desc)
{
    if( desc == 0 )
        return RSCACHE_CS2_TYPE_INT;
    return RSCache_CS2_TypeOfDesc(desc);
}

enum RSCache_CS2_Type
RSCache_CS2_TypeOfLiteral(const char* literal)
{
    if( !literal )
        return RSCACHE_CS2_TYPE_NONE;
    for( int i = 0; i < RSCACHE_CS2_TYPE_COUNT; i++ )
    {
        if( strcmp(cs2_types[i].literal, literal) == 0 )
            return (enum RSCache_CS2_Type)i;
    }
    return RSCACHE_CS2_TYPE_NONE;
}

enum RSCache_CS2_StackType
RSCache_CS2_TypeStackType(enum RSCache_CS2_Type type)
{
    return type == RSCACHE_CS2_TYPE_STRING ? RSCACHE_CS2_STACK_STRING : RSCACHE_CS2_STACK_INT;
}

int
RSCache_CS2_TypeEpilogueDefault(enum RSCache_CS2_Type type)
{
    return type == RSCACHE_CS2_TYPE_INT ? 0 : -1;
}

static bool
cs2_type_set_has(const enum RSCache_CS2_Type* types, int count, enum RSCache_CS2_Type type)
{
    for( int i = 0; i < count; i++ )
    {
        if( types[i] == type )
            return true;
    }
    return false;
}

enum RSCache_CS2_Type
RSCache_CS2_TypeUnion(const enum RSCache_CS2_Type* types, int count)
{
    if( count == 0 )
        return RSCACHE_CS2_TYPE_NONE;
    if( count == 1 )
        return types[0];
    if( count == 2 )
    {
        if( cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_OBJ) &&
            cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_NAMEDOBJ) )
            return RSCACHE_CS2_TYPE_OBJ;
        if( cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_FONTMETRICS) &&
            cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_GRAPHIC) )
            return RSCACHE_CS2_TYPE_FONTMETRICS;
    }
    return RSCACHE_CS2_TYPE_NONE;
}

enum RSCache_CS2_Type
RSCache_CS2_TypeIntersection(const enum RSCache_CS2_Type* types, int count)
{
    if( count == 0 )
        return RSCACHE_CS2_TYPE_NONE;
    if( count == 1 )
        return types[0];
    if( count == 2 )
    {
        if( cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_OBJ) &&
            cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_NAMEDOBJ) )
            return RSCACHE_CS2_TYPE_NAMEDOBJ;
        if( cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_FONTMETRICS) &&
            cs2_type_set_has(types, count, RSCACHE_CS2_TYPE_GRAPHIC) )
            return RSCACHE_CS2_TYPE_GRAPHIC;
    }
    return RSCACHE_CS2_TYPE_NONE;
}

/* -------------------------------------------------------------------------
 * Triggers
 * ---------------------------------------------------------------------- */

struct cs2_trigger_info
{
    int id;
    const char* name;
};

static const struct cs2_trigger_info cs2_triggers[] = {
    { 10, "opworldmapelement1" },
    { 11, "opworldmapelement2" },
    { 12, "opworldmapelement3" },
    { 13, "opworldmapelement4" },
    { 14, "opworldmapelement5" },
    { 15, "worldmapelementmouseover" },
    { 16, "worldmapelementmouseleave" },
    { 17, "worldmapelementmouserepeat" },
    { 47, "trigger_47" },
    { 48, "trigger_48" },
    { 49, "trigger_49" },
    { 73, "proc" },
    { 76, "clientscript" },
};

#define CS2_TRIGGER_COUNT ((int)(sizeof(cs2_triggers) / sizeof(cs2_triggers[0])))

const char*
RSCache_CS2_TriggerName(enum RSCache_CS2_Trigger trigger)
{
    for( int i = 0; i < CS2_TRIGGER_COUNT; i++ )
    {
        if( cs2_triggers[i].id == (int)trigger )
            return cs2_triggers[i].name;
    }
    return NULL;
}

enum RSCache_CS2_Trigger
RSCache_CS2_TriggerOfName(const char* name)
{
    if( !name )
        return RSCACHE_CS2_TRIGGER_NONE;
    for( int i = 0; i < CS2_TRIGGER_COUNT; i++ )
    {
        if( strcmp(cs2_triggers[i].name, name) == 0 )
            return (enum RSCache_CS2_Trigger)cs2_triggers[i].id;
    }
    return RSCACHE_CS2_TRIGGER_NONE;
}

bool
RSCache_CS2_TriggerIsValid(int id)
{
    return RSCache_CS2_TriggerName((enum RSCache_CS2_Trigger)id) != NULL;
}

/* -------------------------------------------------------------------------
 * Script names
 * ---------------------------------------------------------------------- */

static bool
cs2_parse_int(const char* s, int* out)
{
    if( !s || !*s )
        return false;
    char* end = NULL;
    long value = strtol(s, &end, 10);
    if( end == s || *end != '\0' )
        return false;
    *out = (int)value;
    return true;
}

bool
RSCache_CS2_ScriptNameParse(
    const char* cache_name,
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_ScriptName* out)
{
    assert(cache_name && arena && out);

    int n = 0;
    if( !cs2_parse_int(cache_name, &n) )
    {
        size_t len = strlen(cache_name);
        const char* comma = strchr(cache_name, ',');
        if( len < 3 || cache_name[0] != '[' || cache_name[len - 1] != ']' || !comma )
            return false;

        char* trigger_name =
            RSCache_CS2_ArenaStrDupN(arena, cache_name + 1, (size_t)(comma - cache_name - 1));
        enum RSCache_CS2_Trigger trigger = RSCache_CS2_TriggerOfName(trigger_name);
        if( trigger == RSCACHE_CS2_TRIGGER_NONE )
            return false;

        out->trigger = trigger;
        out->name =
            RSCache_CS2_ArenaStrDupN(arena, comma + 1, (size_t)(cache_name + len - 1 - comma - 1));
        return true;
    }

    /* Global trigger: the id is a trigger offset from 512. */
    int t = n + 512;
    if( t >= 10 && t <= 50 )
    {
        if( !RSCache_CS2_TriggerIsValid(t) )
            return false;
        out->trigger = (enum RSCache_CS2_Trigger)t;
        out->name = "_";
        return true;
    }

    /* Category trigger: the subject is a category id packed alongside. */
    int c = (n << 8) - 3;
    if( c < 0 )
        c = -c;
    t = (c >> 8) + n + 768;
    if( t >= 0 && t <= 255 )
    {
        if( !RSCache_CS2_TriggerIsValid(n + 512) )
            return false;
        out->trigger = (enum RSCache_CS2_Trigger)(n + 512);
        char buf[32];
        snprintf(buf, sizeof(buf), "_category_%d", c);
        out->name = RSCache_CS2_ArenaStrDup(arena, buf);
        return true;
    }

    /* Type trigger: low byte is the trigger, high bytes the subject id. */
    if( !RSCache_CS2_TriggerIsValid(n & 0xFF) )
        return false;
    out->trigger = (enum RSCache_CS2_Trigger)(n & 0xFF);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", n >> 8);
    out->name = RSCache_CS2_ArenaStrDup(arena, buf);
    return true;
}

const char*
RSCache_CS2_ScriptNameFormat(
    const struct RSCache_CS2_ScriptName* script_name,
    struct RSCache_CS2_Arena* arena)
{
    const char* trigger = RSCache_CS2_TriggerName(script_name->trigger);
    if( !trigger )
        trigger = "?";
    size_t len = strlen(trigger) + strlen(script_name->name) + 4;
    char* out = (char*)RSCache_CS2_ArenaAlloc(arena, len);
    snprintf(out, len, "[%s,%s]", trigger, script_name->name);
    return out;
}

/* -------------------------------------------------------------------------
 * Prototypes
 * ---------------------------------------------------------------------- */

#define CS2_PLAIN(t) { RSCACHE_CS2_TYPE_##t, NULL }
#define CS2_NAMED(t, id) { RSCACHE_CS2_TYPE_##t, id }

static struct RSCache_CS2_Prototype cs2_prototypes[RSCACHE_CS2_PROTO_COUNT_] = {
    [RSCACHE_CS2_PROTO_INT] = CS2_PLAIN(INT),
    /* Type deliberately NONE — see the enum. The identifier is still `int` so an
     * otherwise-unconstrained local prints as `$int0` rather than losing a name. */
    [RSCACHE_CS2_PROTO_UNKNOWNINT] = { RSCACHE_CS2_TYPE_NONE, "int" },
    [RSCACHE_CS2_PROTO_STRING] = CS2_PLAIN(STRING),
    [RSCACHE_CS2_PROTO_COMPONENT] = CS2_PLAIN(COMPONENT),
    [RSCACHE_CS2_PROTO_BOOLEAN] = CS2_PLAIN(BOOLEAN),
    [RSCACHE_CS2_PROTO_OBJ] = CS2_PLAIN(OBJ),
    [RSCACHE_CS2_PROTO_ENUM] = CS2_PLAIN(ENUM),
    [RSCACHE_CS2_PROTO_STAT] = CS2_PLAIN(STAT),
    [RSCACHE_CS2_PROTO_GRAPHIC] = CS2_PLAIN(GRAPHIC),
    [RSCACHE_CS2_PROTO_INV] = CS2_PLAIN(INV),
    [RSCACHE_CS2_PROTO_MODEL] = CS2_PLAIN(MODEL),
    [RSCACHE_CS2_PROTO_COORD] = CS2_PLAIN(COORD),
    [RSCACHE_CS2_PROTO_CATEGORY] = CS2_PLAIN(CATEGORY),
    [RSCACHE_CS2_PROTO_LOC] = CS2_PLAIN(LOC),
    [RSCACHE_CS2_PROTO_AREA] = CS2_PLAIN(AREA),
    [RSCACHE_CS2_PROTO_MAPAREA] = CS2_PLAIN(MAPAREA),
    [RSCACHE_CS2_PROTO_NAMEDOBJ] = CS2_PLAIN(NAMEDOBJ),
    [RSCACHE_CS2_PROTO_FONTMETRICS] = CS2_PLAIN(FONTMETRICS),
    [RSCACHE_CS2_PROTO_CHAR] = CS2_PLAIN(CHAR),
    [RSCACHE_CS2_PROTO_STRUCT] = CS2_PLAIN(STRUCT),
    [RSCACHE_CS2_PROTO_SYNTH] = CS2_PLAIN(SYNTH),
    [RSCACHE_CS2_PROTO_MAPELEMENT] = CS2_PLAIN(MAPELEMENT),
    [RSCACHE_CS2_PROTO_NPC] = CS2_PLAIN(NPC),
    [RSCACHE_CS2_PROTO_SEQ] = CS2_PLAIN(SEQ),
    [RSCACHE_CS2_PROTO_INTERFACE] = CS2_PLAIN(INTERFACE),
    [RSCACHE_CS2_PROTO_TYPE] = CS2_PLAIN(TYPE),
    [RSCACHE_CS2_PROTO_PARAM] = CS2_PLAIN(PARAM),
    [RSCACHE_CS2_PROTO_NEWVAR] = CS2_PLAIN(NEWVAR),
    [RSCACHE_CS2_PROTO_NPC_UID] = CS2_PLAIN(NPC_UID),
    [RSCACHE_CS2_PROTO_PLAYER_UID] = CS2_PLAIN(PLAYER_UID),
    [RSCACHE_CS2_PROTO_SPOTANIM] = CS2_PLAIN(SPOTANIM),
    [RSCACHE_CS2_PROTO_DBROW] = CS2_PLAIN(DBROW),

    [RSCACHE_CS2_PROTO_OPBASE] = CS2_NAMED(STRING, "opbase"),
    [RSCACHE_CS2_PROTO_MOUSEX] = CS2_NAMED(INT, "mousex"),
    [RSCACHE_CS2_PROTO_MOUSEY] = CS2_NAMED(INT, "mousey"),
    [RSCACHE_CS2_PROTO_OPINDEX] = CS2_NAMED(INT, "opindex"),
    [RSCACHE_CS2_PROTO_COMSUBID] = CS2_NAMED(INT, "comsubid"),
    [RSCACHE_CS2_PROTO_DROP] = CS2_NAMED(COMPONENT, "drop"),
    [RSCACHE_CS2_PROTO_DROPSUBID] = CS2_NAMED(INT, "dropsubid"),
    [RSCACHE_CS2_PROTO_KEY] = CS2_NAMED(INT, "key"),
    [RSCACHE_CS2_PROTO_KEYCHAR] = CS2_NAMED(CHAR, "keychar"),
    [RSCACHE_CS2_PROTO_INDEX] = CS2_NAMED(INT, "index"),
    [RSCACHE_CS2_PROTO_LENGTH] = CS2_NAMED(INT, "length"),
    [RSCACHE_CS2_PROTO_COUNT] = CS2_NAMED(INT, "count"),
    [RSCACHE_CS2_PROTO_TOTAL] = CS2_NAMED(INT, "total"),
    [RSCACHE_CS2_PROTO_SIZE] = CS2_NAMED(INT, "size"),
    [RSCACHE_CS2_PROTO_NUM] = CS2_NAMED(INT, "num"),
    [RSCACHE_CS2_PROTO_X] = CS2_NAMED(INT, "x"),
    [RSCACHE_CS2_PROTO_Y] = CS2_NAMED(INT, "y"),
    [RSCACHE_CS2_PROTO_Z] = CS2_NAMED(INT, "z"),
    [RSCACHE_CS2_PROTO_WIDTH] = CS2_NAMED(INT, "width"),
    [RSCACHE_CS2_PROTO_HEIGHT] = CS2_NAMED(INT, "height"),
    [RSCACHE_CS2_PROTO_WORLD] = CS2_NAMED(INT, "world"),
    [RSCACHE_CS2_PROTO_RANK] = CS2_NAMED(INT, "rank"),
    [RSCACHE_CS2_PROTO_XP] = CS2_NAMED(INT, "xp"),
    [RSCACHE_CS2_PROTO_LVL] = CS2_NAMED(INT, "lvl"),
    [RSCACHE_CS2_PROTO_SLOT] = CS2_NAMED(INT, "slot"),
    [RSCACHE_CS2_PROTO_CLANSLOT] = CS2_NAMED(INT, "clanslot"),
    [RSCACHE_CS2_PROTO_CLOCK] = CS2_NAMED(INT, "clock"),
    [RSCACHE_CS2_PROTO_TRANS] = CS2_NAMED(INT, "trans"),
    [RSCACHE_CS2_PROTO_ANGLE] = CS2_NAMED(INT, "angle"),
    [RSCACHE_CS2_PROTO_CHATFILTER] = CS2_NAMED(INT, "chatfilter"),
    [RSCACHE_CS2_PROTO_MESUID] = CS2_NAMED(INT, "mesuid"),
    [RSCACHE_CS2_PROTO_FLAGS] = CS2_NAMED(INT, "flags"),
    [RSCACHE_CS2_PROTO_LAYER] = CS2_NAMED(COMPONENT, "layer"),
    [RSCACHE_CS2_PROTO_OP] = CS2_NAMED(STRING, "op"),
    [RSCACHE_CS2_PROTO_URL] = CS2_NAMED(STRING, "url"),
    [RSCACHE_CS2_PROTO_TEXT] = CS2_NAMED(STRING, "text"),
    [RSCACHE_CS2_PROTO_MES] = CS2_NAMED(STRING, "mes"),
    [RSCACHE_CS2_PROTO_USERNAME] = CS2_NAMED(STRING, "username"),
    [RSCACHE_CS2_PROTO_COLOUR] = CS2_NAMED(INT, "colour"),
    [RSCACHE_CS2_PROTO_IFTYPE] = CS2_NAMED(INT, "iftype"),
    [RSCACHE_CS2_PROTO_SETSIZE] = CS2_NAMED(INT, "setsize"),
    [RSCACHE_CS2_PROTO_SETPOSH] = CS2_NAMED(INT, "setposh"),
    [RSCACHE_CS2_PROTO_SETPOSV] = CS2_NAMED(INT, "setposv"),
    [RSCACHE_CS2_PROTO_SETTEXTALIGNH] = CS2_NAMED(INT, "settextalignh"),
    [RSCACHE_CS2_PROTO_SETTEXTALIGNV] = CS2_NAMED(INT, "settextalignv"),
    [RSCACHE_CS2_PROTO_CHATTYPE] = CS2_NAMED(INT, "chattype"),
    [RSCACHE_CS2_PROTO_BOOL] = CS2_NAMED(INT, "bool"),
    [RSCACHE_CS2_PROTO_WINDOWMODE] = CS2_NAMED(INT, "windowmode"),
    [RSCACHE_CS2_PROTO_CLIENTTYPE] = CS2_NAMED(INT, "clienttype"),
};

const struct RSCache_CS2_Prototype*
RSCache_CS2_ProtoGet(enum RSCache_CS2_ProtoId id)
{
    assert(id >= 0 && id < RSCACHE_CS2_PROTO_COUNT_);
    struct RSCache_CS2_Prototype* p = &cs2_prototypes[id];
    /* A plain prototype's identifier is its type's literal. Filled in lazily so
     * the table above stays a single flat list rather than repeating literals
     * that RSCache_CS2_TypeLiteral already owns. */
    if( !p->identifier )
        p->identifier = RSCache_CS2_TypeLiteral(p->type);
    return p;
}

enum RSCache_CS2_ProtoId
RSCache_CS2_ProtoForType(enum RSCache_CS2_Type type)
{
    if( type == RSCACHE_CS2_TYPE_NONE )
        return RSCACHE_CS2_PROTO_NONE;
    for( int i = 0; i < RSCACHE_CS2_PROTO_COUNT_; i++ )
    {
        if( cs2_prototypes[i].type != type )
            continue;
        if( cs2_prototypes[i].identifier == NULL )
            return (enum RSCache_CS2_ProtoId)i;
        if( strcmp(cs2_prototypes[i].identifier, RSCache_CS2_TypeLiteral(type)) == 0 )
            return (enum RSCache_CS2_ProtoId)i;
    }
    return RSCACHE_CS2_PROTO_NONE;
}

enum RSCache_CS2_Type
RSCache_CS2_TypeOfIdentifier(const char* identifier)
{
    if( !identifier || !*identifier )
        return RSCACHE_CS2_TYPE_NONE;
    enum RSCache_CS2_Type literal = RSCache_CS2_TypeOfLiteral(identifier);
    if( literal != RSCACHE_CS2_TYPE_NONE )
        return literal;
    for( int i = 0; i < RSCACHE_CS2_PROTO_COUNT_; i++ )
    {
        if( cs2_prototypes[i].identifier &&
            strcmp(cs2_prototypes[i].identifier, identifier) == 0 )
            return cs2_prototypes[i].type;
    }
    return RSCACHE_CS2_TYPE_NONE;
}

bool
RSCache_CS2_ProtoIsDefault(enum RSCache_CS2_ProtoId id)
{
    const struct RSCache_CS2_Prototype* p = RSCache_CS2_ProtoGet(id);
    /* UNKNOWNINT has no type to compare an identifier against, and it adds none
     * beyond what the solver will settle on, so it is default by definition. */
    if( p->type == RSCACHE_CS2_TYPE_NONE )
        return true;
    return strcmp(p->identifier, RSCache_CS2_TypeLiteral(p->type)) == 0;
}
