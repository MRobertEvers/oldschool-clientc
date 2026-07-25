#include "dat2_config_var.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- varbit --- */

void
RSCache_Dat2ConfigVarbitDecode(
    struct RSCache_Dat2ConfigVarbit* entry,
    struct RSCache_Buffer* buffer)
{
    if( !entry || !buffer )
        return;

    /* -1, not 0: a varbit that named no base variable is not one pointing at
     * varplayer 0. Matches Client-TS VarBitType. */
    entry->basevar = -1;
    entry->startbit = 0;
    entry->endbit = 0;

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            entry->basevar = g2(buffer);
            entry->startbit = g1(buffer);
            entry->endbit = g1(buffer);
        }
        else if( opcode == 10 )
        {
            char* name = gcstring(buffer);
            free(entry->debugname);
            entry->debugname = name;
        }
        else
        {
            /* Stop rather than guess a width — see the header. */
            break;
        }
    }
}

void
RSCache_Dat2ConfigVarbitDecodeInplace(
    struct RSCache_Dat2ConfigVarbit* entry,
    const void* data,
    int data_size)
{
    if( !entry )
        return;
    if( !data || data_size <= 0 )
    {
        entry->basevar = -1;
        return;
    }

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigVarbitDecode(entry, &buffer);
    entry->_consumed = (int)buffer.position;
}

uint32_t
RSCache_Dat2ConfigVarbitEncodeBound(const struct RSCache_Dat2ConfigVarbit* entry)
{
    if( !entry )
        return 0;
    /* opcode 1 + u16 + u8 + u8, then opcode 10 + string, then the terminator. */
    uint32_t bound = 5u + 1u;
    if( entry->debugname )
        bound += 1u + (uint32_t)strlen(entry->debugname) + 1u;
    return bound;
}

uint32_t
RSCache_Dat2ConfigVarbitEncode(
    const struct RSCache_Dat2ConfigVarbit* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;
    if( out_capacity < RSCache_Dat2ConfigVarbitEncodeBound(entry) )
        return 0;
    if( entry->startbit < 0 || entry->startbit > 255 )
        return 0;
    if( entry->endbit < 0 || entry->endbit > 255 )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* basevar is the only field opcode 1 can be inferred from: -1 is the "absent"
     * marker the decoder writes when the opcode was missing. */
    if( entry->basevar >= 0 )
    {
        p1(&buffer, 1);
        p2(&buffer, entry->basevar);
        p1(&buffer, entry->startbit);
        p1(&buffer, entry->endbit);
    }

    if( entry->debugname )
    {
        p1(&buffer, 10);
        pjstr(&buffer, entry->debugname, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    p1(&buffer, 0);
    return buffer.position;
}

void
RSCache_Dat2ConfigVarbitFreeInplace(struct RSCache_Dat2ConfigVarbit* entry)
{
    if( !entry )
        return;
    free(entry->debugname);
    entry->debugname = NULL;
}

void
RSCache_Dat2ConfigVarbitFree(struct RSCache_Dat2ConfigVarbit* entry)
{
    if( !entry )
        return;
    RSCache_Dat2ConfigVarbitFreeInplace(entry);
    free(entry);
}

/* ------------------------------------------------------------ varplayer --- */

void
RSCache_Dat2ConfigVarplayerDecode(
    struct RSCache_Dat2ConfigVarplayer* entry,
    struct RSCache_Buffer* buffer)
{
    if( !entry || !buffer )
        return;

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( opcode == 5 )
            entry->clientcode = g2(buffer);
        else
            break;
    }
}

void
RSCache_Dat2ConfigVarplayerDecodeInplace(
    struct RSCache_Dat2ConfigVarplayer* entry,
    const void* data,
    int data_size)
{
    if( !entry )
        return;
    if( !data || data_size <= 0 )
        return;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigVarplayerDecode(entry, &buffer);
    entry->_consumed = (int)buffer.position;
}

uint32_t
RSCache_Dat2ConfigVarplayerEncodeBound(const struct RSCache_Dat2ConfigVarplayer* entry)
{
    (void)entry;
    return 4u;
}

uint32_t
RSCache_Dat2ConfigVarplayerEncode(
    const struct RSCache_Dat2ConfigVarplayer* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out || out_capacity < 4u )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /*
     * Emitted when non-zero. Exact for the corpus: observed clientcodes run 1..22 and
     * no record carries an explicit zero, so "0" and "absent" do not collide in
     * practice. A record that did carry an explicit 0 would re-encode one opcode
     * shorter with the same meaning.
     */
    if( entry->clientcode != 0 )
    {
        p1(&buffer, 5);
        p2(&buffer, entry->clientcode);
    }

    p1(&buffer, 0);
    return buffer.position;
}

/* ------------------------------------------------------------ varclient --- */

void
RSCache_Dat2ConfigVarclientDecode(
    struct RSCache_Dat2ConfigVarclient* entry,
    struct RSCache_Buffer* buffer)
{
    if( !entry || !buffer )
        return;

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( opcode == 2 )
        {
            entry->persist = 1;
        }
        else if( opcode == 3 )
        {
            entry->opcode_3 = g2(buffer);
            entry->has_opcode_3 = true;
        }
        else
        {
            break;
        }
    }
}

void
RSCache_Dat2ConfigVarclientDecodeInplace(
    struct RSCache_Dat2ConfigVarclient* entry,
    const void* data,
    int data_size)
{
    if( !entry )
        return;
    if( !data || data_size <= 0 )
        return;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigVarclientDecode(entry, &buffer);
    entry->_consumed = (int)buffer.position;
}

uint32_t
RSCache_Dat2ConfigVarclientEncodeBound(const struct RSCache_Dat2ConfigVarclient* entry)
{
    (void)entry;
    return 5u;
}

uint32_t
RSCache_Dat2ConfigVarclientEncode(
    const struct RSCache_Dat2ConfigVarclient* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out || out_capacity < 5u )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( entry->persist )
        p1(&buffer, 2);

    /* Keyed on presence, not value: 40 records in the corpus carry opcode 3 with an
     * explicit 0, so a value test would drop the opcode and change the byte count. */
    if( entry->has_opcode_3 )
    {
        p1(&buffer, 3);
        p2(&buffer, entry->opcode_3);
    }

    p1(&buffer, 0);
    return buffer.position;
}
