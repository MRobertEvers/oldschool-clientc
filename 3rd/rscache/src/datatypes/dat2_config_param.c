#include "dat2_config_param.h"

#include <stdlib.h>
#include <string.h>

/*
 * getJagexChar: map a wire byte to a char. The 128..159 range maps to a
 * Windows-1252-style unicode table in the reference client; those codepoints
 * do not fit a single char, so they collapse to '?'. Only ASCII (e.g. 's' for
 * string params) is relevant here, and that passes through unchanged.
 */
static char
rscache_param_jagex_char(int c)
{
    if( c >= 128 && c < 160 )
        return '?';
    return (char)c;
}

/* getCharForTypeId: map a numeric type id to its char code. */
static char
rscache_param_char_for_type_id(int id)
{
    switch( id )
    {
    case 0:
        return 'i';
    case 1:
        return '1';
    case 6:
        return 'A';
    case 7:
        return 'C';
    case 8:
        return 'H';
    case 9:
        return 'I';
    case 10:
        return 'K';
    case 11:
        return 'M';
    case 13:
        return 'O';
    case 14:
        return 'P';
    case 17:
        return 'S';
    case 22:
        return 'c';
    case 23:
        return 'd';
    case 25:
        return 'f';
    case 26:
        return 'g';
    case 28:
        return 'j';
    case 30:
        return 'l';
    case 31:
        return 'm';
    case 32:
        return 'n';
    case 33:
        return 'o';
    case 36:
        return 's';
    case 37:
        return 't';
    case 39:
        return 'v';
    case 40:
        return 'x';
    case 41:
        return 'y';
    case 42:
        return 'z';
    case 73:
        return 'J';
    default:
        return 'i';
    }
}

void
RSCache_Dat2ConfigParamDecodeInplace(
    struct RSCache_Dat2ConfigParam* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    if( !entry )
        return;
    entry->auto_disable = 1;
    if( !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            entry->type = rscache_param_jagex_char(g1(&buf));
            break;
        case 2:
            entry->default_int = g4(&buf);
            break;
        case 4:
            entry->auto_disable = 0;
            break;
        case 5:
        {
            char* s = gcstring(&buf);
            free(entry->default_string);
            entry->default_string = s;
            break;
        }
        case 7:
            entry->default_long = (long long)g8(&buf);
            break;
        case 8:
            entry->type = rscache_param_char_for_type_id(g1(&buf));
            break;
        default:
            break;
        }
    }
}

void
RSCache_Dat2ConfigParamFreeInplace(struct RSCache_Dat2ConfigParam* entry)
{
    if( !entry )
        return;
    free(entry->default_string);
    entry->default_string = NULL;
}

void
RSCache_Dat2ConfigParamFree(struct RSCache_Dat2ConfigParam* entry)
{
    if( !entry )
        return;
    RSCache_Dat2ConfigParamFreeInplace(entry);
    free(entry);
}
