#include "dat2_config_var.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- varbit --- */

void
RSCache_Dat2ConfigVarbitDecode(
    struct RSCache_Dat2ConfigVarbit* entry,
    struct RSCache_Buffer* buffer)
{
    assert(entry != NULL);
    assert(buffer != NULL);

    RSCache_Dat2ConfigVarbitInit(entry);

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigVarbitDecodeOp(entry, opcode, buffer, 0) )
            break;
    }
}

void
RSCache_Dat2ConfigVarbitInit(struct RSCache_Dat2ConfigVarbit* entry)
{
    assert(entry != NULL);
    /* -1, not 0: a varbit that named no base variable is not one pointing at
     * varplayer 0. Matches Client-TS VarBitType. Held out of the decode loop so a
     * per-opcode caller cannot skip it — see `opcode_codec.h`. */
    entry->basevar = -1;
    entry->startbit = 0;
    entry->endbit = 0;
    RSCache_PresenceReset(&entry->present);
}

bool
RSCache_Dat2ConfigVarbitDecodeOp(
    struct RSCache_Dat2ConfigVarbit* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
    if( opcode == 1 )
    {
        entry->basevar = g2(buffer);
        entry->startbit = g1(buffer);
        entry->endbit = g1(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_VARBIT_FIELD_BITS);
        return true;
    }
    if( opcode == 10 )
    {
        char* name = gcstring(buffer);
        free(entry->debugname);
        entry->debugname = name;
        RSCache_PresenceSet(&entry->present, RSCACHE_VARBIT_FIELD_DEBUGNAME);
        return true;
    }
    /* Stop rather than guess a width — see the header. */
    return false;
}

void
RSCache_Dat2ConfigVarbitDecodeInplace(
    struct RSCache_Dat2ConfigVarbit* entry,
    const void* data,
    int data_size)
{
    assert(entry != NULL);
    if( !data || data_size <= 0 )
    {
        /* No bytes is an empty record, not a bad argument: the defaults, nothing
         * stated. */
        RSCache_Dat2ConfigVarbitInit(entry);
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
    assert(entry);
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
    assert(entry);
    assert(out);
    assert(out_capacity >= RSCache_Dat2ConfigVarbitEncodeBound(entry));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Stated, not inferred: this used to write opcode 1 only when basevar >= 0,
     * reading the decoder's -1 default as "absent". */
    if( RSCache_PresenceHas(&entry->present, RSCACHE_VARBIT_FIELD_BITS) )
    {
        assert(entry->basevar >= 0);
        assert(entry->basevar <= 0xFFFF);
        assert(entry->startbit >= 0);
        assert(entry->startbit <= 255);
        assert(entry->endbit >= 0);
        assert(entry->endbit <= 255);
        p1(&buffer, 1);
        p2(&buffer, entry->basevar);
        p1(&buffer, entry->startbit);
        p1(&buffer, entry->endbit);
    }

    if( RSCache_PresenceHas(&entry->present, RSCACHE_VARBIT_FIELD_DEBUGNAME) )
    {
        assert(entry->debugname);
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
    assert(entry != NULL);
    assert(buffer != NULL);

    RSCache_PresenceReset(&entry->present);

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigVarplayerDecodeOp(entry, opcode, buffer, 0) )
            break;
    }
}

bool
RSCache_Dat2ConfigVarplayerDecodeOp(
    struct RSCache_Dat2ConfigVarplayer* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
    if( opcode == 5 )
    {
        entry->clientcode = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_VARP_FIELD_CLIENTCODE);
        return true;
    }
    return false;
}

void
RSCache_Dat2ConfigVarplayerDecodeInplace(
    struct RSCache_Dat2ConfigVarplayer* entry,
    const void* data,
    int data_size)
{
    assert(entry != NULL);
    assert(data != NULL);
    assert(data_size > 0);

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
    assert(entry != NULL);
    assert(out != NULL);
    assert(out_capacity >= 4u);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Stated, not inferred: this used to write the opcode when the value was
     * non-zero, so a record carrying an explicit 0 re-encoded one opcode short. */
    if( RSCache_PresenceHas(&entry->present, RSCACHE_VARP_FIELD_CLIENTCODE) )
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
    assert(entry != NULL);
    assert(buffer != NULL);

    RSCache_PresenceReset(&entry->present);

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigVarclientDecodeOp(entry, opcode, buffer, 0) )
            break;
    }
}

bool
RSCache_Dat2ConfigVarclientDecodeOp(
    struct RSCache_Dat2ConfigVarclient* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
    if( opcode == 2 )
    {
        entry->persist = 1;
        RSCache_PresenceSet(&entry->present, RSCACHE_VARC_FIELD_PERSIST);
        return true;
    }
    if( opcode == 3 )
    {
        entry->opcode_3 = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_VARC_FIELD_OPCODE_3);
        return true;
    }
    return false;
}

void
RSCache_Dat2ConfigVarclientDecodeInplace(
    struct RSCache_Dat2ConfigVarclient* entry,
    const void* data,
    int data_size)
{
    assert(entry != NULL);
    assert(data != NULL);
    assert(data_size > 0);

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
    assert(entry != NULL);
    assert(out != NULL);
    assert(out_capacity >= 5u);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( RSCache_PresenceHas(&entry->present, RSCACHE_VARC_FIELD_PERSIST) )
        p1(&buffer, 2);

    /* Keyed on presence, not value: 40 records in the corpus carry opcode 3 with an
     * explicit 0, so a value test would drop the opcode and change the byte count. */
    if( RSCache_PresenceHas(&entry->present, RSCACHE_VARC_FIELD_OPCODE_3) )
    {
        p1(&buffer, 3);
        p2(&buffer, entry->opcode_3);
    }

    p1(&buffer, 0);
    return buffer.position;
}
