#include "rscache_valuetype.h"

#include <assert.h>
#include <string.h>

static const struct RSCache_ValueType k_value_types[] = {
    /* id   char  word */
    { 0, 'i', "int" },
    { 1, '1', "boolean" },
    { 6, 'A', "seq" },
    { 7, 'C', "colour" },
    { 8, 'H', "locshape" },
    { 9, 'I', "component" },
    { 10, 'K', "idkit" },
    { 11, 'M', "midi" },
    { 13, 'O', "namedobj" },
    { 14, 'P', "synth" },
    { 17, 'S', "stat" },
    { 22, 'c', "coord" },
    { 23, 'd', "graphic" },
    { 25, 'f', "fontmetrics" },
    { 26, 'g', "enum" },
    { 28, 'j', "jingle" },
    { 30, 'l', "loc" },
    { 31, 'm', "model" },
    { 32, 'n', "npc" },
    { 33, 'o', "obj" },
    { 36, 's', "string" },
    { 37, 't', "spotanim" },
    { 39, 'v', "inv" },
    { 40, 'x', "texture" },
    { 41, 'y', "category" },
    { 42, 'z', "char" },
    { 55, 0xA3, "mapsceneicon" },
    { 59, 0xB5, "mapelement" },
    { 62, 0xD7, "hitmark" },
    { 73, 'J', "struct" },
    { 74, 0xD0, "dbrow" },
    { 118, 0xD8, "dbtable" },
    { 209, '7', "varp" },
    /* A character, no published id. */
    { -1, 'R', "area" },
    { -1, '`', "maparea" },
    { -1, 'a', "interface" },
    /* This server's own. */
    { -1, 0x81, "varbit" },
};

#define VALUE_TYPE_COUNT ((int)(sizeof(k_value_types) / sizeof(k_value_types[0])))

int
RSCache_ValueTypeCount(void)
{
    return VALUE_TYPE_COUNT;
}

const struct RSCache_ValueType*
RSCache_ValueTypeAt(int index)
{
    assert(index >= 0);
    assert(index < VALUE_TYPE_COUNT);
    return &k_value_types[index];
}

const struct RSCache_ValueType*
RSCache_ValueTypeOfId(int id)
{
    if( id < 0 )
        return NULL;
    for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
    {
        if( k_value_types[i].id == id )
            return &k_value_types[i];
    }
    return NULL;
}

const struct RSCache_ValueType*
RSCache_ValueTypeOfChar(int ch)
{
    ch &= 0xFF;
    if( ch == 0 )
        return NULL;
    for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
    {
        if( k_value_types[i].ch == ch )
            return &k_value_types[i];
    }
    return NULL;
}

const struct RSCache_ValueType*
RSCache_ValueTypeNamed(const char* word)
{
    assert(word);
    for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
    {
        if( strcmp(k_value_types[i].word, word) == 0 )
            return &k_value_types[i];
    }
    return NULL;
}
