#include "../rscache_valuetype.h"
#include "dat2_config_param.h"

#include <assert.h>
#include <stdbool.h>
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

char
RSCache_Dat2ConfigParamCharForTypeKey(int key)
{
    return rscache_param_jagex_char(key);
}

/*
 * getCharForTypeId: map a numeric type id to its char code, through the one
 * ScriptVarType table (rscache_valuetype.h). An id the table does not know
 * reads as 'i', as it always has here.
 */
char
RSCache_Dat2ConfigParamCharForTypeId(int id)
{
    const struct RSCache_ValueType* type = RSCache_ValueTypeOfId(id);

    return type ? (char)type->ch : 'i';
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
    /* Nothing stated until an opcode says so. */
    RSCache_PresenceReset(&entry->present);
    if( data_size == 1 && ((const uint8_t*)data)[0] == 0 )
    {
        entry->_consumed = 1;
        return;
    }

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
    RSCache_PresenceReset(&entry->present);
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
        entry->type_key = g1(buffer);
        entry->type = rscache_param_jagex_char(entry->type_key);
        RSCache_PresenceSet(&entry->present, RSCACHE_PARAM_FIELD_TYPE);
        return true;
    case 2:
        entry->default_int = g4(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_PARAM_FIELD_DEFAULT_INT);
        return true;
    case 4:
        entry->auto_disable = 0;
        RSCache_PresenceSet(&entry->present, RSCACHE_PARAM_FIELD_AUTO_DISABLE);
        return true;
    case 5:
    {
        char* str = gcstring(buffer);
        free(entry->default_string);
        entry->default_string = str;
        RSCache_PresenceSet(&entry->present, RSCACHE_PARAM_FIELD_DEFAULT_STRING);
        return true;
    }
    case 7:
        entry->default_long = (long long)g8(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_PARAM_FIELD_DEFAULT_LONG);
        return true;
    case 8:
        entry->type_id = g1(buffer);
        entry->type = RSCache_Dat2ConfigParamCharForTypeId(entry->type_id);
        RSCache_PresenceSet(&entry->present, RSCACHE_PARAM_FIELD_TYPE_ID);
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

    /* Exactly what the record stated. A default_int of 0 stated explicitly is
     * written back (every rev-239 int param states one), and so is opcode 8
     * beside opcode 1.
     *
     * The order is the era's, and the era shows in the record: the caches that
     * write opcode 8 (rev 239) put the bare opcode 4 last, every older one puts
     * it first. Keyed on opcode 8's presence, that reproduces every cache in the
     * tree byte for byte. */
    bool const auto_disable_last =
        RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_TYPE_ID);

    if( !auto_disable_last && RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_AUTO_DISABLE) )
        p1(&buf, 4);

    if( RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_TYPE) )
    {
        p1(&buf, 1);
        p1(&buf, entry->type_key);
    }

    if( RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_TYPE_ID) )
    {
        p1(&buf, 8);
        p1(&buf, entry->type_id);
    }

    if( RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_DEFAULT_INT) )
    {
        p1(&buf, 2);
        p4(&buf, entry->default_int);
    }

    if( RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_DEFAULT_STRING) )
    {
        assert(entry->default_string);
        p1(&buf, 5);
        pjstr(&buf, entry->default_string, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    if( RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_DEFAULT_LONG) )
    {
        p1(&buf, 7);
        p8(&buf, (int64_t)entry->default_long);
    }

    /* auto_disable defaults to 1; opcode 4 is a flag that clears it. */
    if( auto_disable_last && RSCache_PresenceHas(&entry->present, RSCACHE_PARAM_FIELD_AUTO_DISABLE) )
        p1(&buf, 4);

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
