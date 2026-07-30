#include "dat2_config_param.h"

#include <assert.h>
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

    assert(entry != NULL);
    assert(data != NULL);
    assert(data_size > 0);
    entry->auto_disable = 1;
    if( !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        if( !RSCache_Dat2ConfigParamDecodeOp(entry, opcode, &buf, 0) )
            break;
    }
    entry->_consumed = (int)buf.position;
}

void
RSCache_Dat2ConfigParamInit(struct RSCache_Dat2ConfigParam* entry)
{
    assert(entry != NULL);
    /* Opcode 4 *clears* this, so the default has to be 1 — a zeroed record would
     * read as auto-disable off for every param that never mentions it. Held out
     * of the decode loop so a per-opcode caller cannot miss it. */
    entry->auto_disable = 1;
}

bool
RSCache_Dat2ConfigParamDecodeOp(
    struct RSCache_Dat2ConfigParam* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
    switch( opcode )
    {
    case 1:
        entry->type = rscache_param_jagex_char(g1(buffer));
        return true;
    case 2:
        entry->default_int = g4(buffer);
        return true;
    case 4:
        entry->auto_disable = 0;
        return true;
    case 5:
    {
        char* str = gcstring(buffer);
        free(entry->default_string);
        entry->default_string = str;
        return true;
    }
    case 7:
        entry->default_long = (long long)g8(buffer);
        return true;
    case 8:
        entry->type = rscache_param_char_for_type_id(g1(buffer));
        return true;
    default:
        /*
         * Stop, where this decoder used to skip and carry on.
         *
         * Skipping cannot be right: an unknown opcode's payload width is unknown,
         * so the next `g1` reads a payload byte as an opcode and the rest of the
         * record decodes from a false position. Every other type in this library
         * stops for that reason. Verified unobservable on osrs239 — the exact and
         * differ counts are unchanged — because no param record there carries an
         * opcode this decoder does not know.
         */
        return false;
    }
}

uint32_t
RSCache_Dat2ConfigParamEncode(
    const struct RSCache_Dat2ConfigParam* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(entry != NULL);
    assert(out != NULL);
    assert(out_capacity >= 4u);

    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, out_capacity);

    /* The type reaches the struct as a character, via either opcode 1 (the
     * character directly) or opcode 8 (a numeric id mapped to one). Which was
     * used is not recorded, so this always writes opcode 1 — that reproduces the
     * character exactly, at the cost of byte-exactness for records that used
     * opcode 8. */
    if( entry->type != 0 )
    {
        p1(&buf, 1);
        p1(&buf, (unsigned char)entry->type);
    }

    if( entry->default_int != 0 )
    {
        p1(&buf, 2);
        p4(&buf, entry->default_int);
    }

    /* auto_disable defaults to 1; opcode 4 is a flag that clears it. */
    if( !entry->auto_disable )
        p1(&buf, 4);

    if( entry->default_string )
    {
        p1(&buf, 5);
        pjstr(&buf, entry->default_string, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    if( entry->default_long != 0 )
    {
        p1(&buf, 7);
        p8(&buf, (int64_t)entry->default_long);
    }

    p1(&buf, 0);
    return buf.position;
}

void
RSCache_Dat2ConfigParamFreeInplace(struct RSCache_Dat2ConfigParam* entry)
{
    assert(entry != NULL);
    free(entry->default_string);
    entry->default_string = NULL;
}

void
RSCache_Dat2ConfigParamFree(struct RSCache_Dat2ConfigParam* entry)
{
    assert(entry != NULL);
    RSCache_Dat2ConfigParamFreeInplace(entry);
    free(entry);
}

uint32_t
RSCache_Dat2ConfigParamEncodeBound(const struct RSCache_Dat2ConfigParam* entry)
{
    assert(entry != NULL);
    /*
     * Every opcode this type can emit, summed rather than sampled: 1 (+u8),
     * 2 (+u32), 4 (bare), 7 (+u64), 8 (+u8), 5 (+string), then the terminator.
     */
    uint32_t need = (1u + 1u) + (1u + 4u) + 1u + (1u + 8u) + (1u + 1u) + 1u;

    if( entry->default_string )
        need += 1u + (uint32_t)strlen(entry->default_string) + 1u;
    return need;
}
