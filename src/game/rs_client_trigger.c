#include "game/rs_client_trigger.h"

#include <stdio.h>

int
RS_ClientTriggerHashSubject(int trigger, int subject)
{
    return subject * 256 + trigger;
}

int
RS_ClientTriggerHashCategory(int trigger, int category)
{
    return trigger - category * 256 - 0x300;
}

int
RS_ClientTriggerHashGlobal(int trigger)
{
    return trigger - 0x200;
}

int
RS_ClientTriggerNameHash(int trigger_hash)
{
    char name[16];
    int hash = 0;

    snprintf(name, sizeof(name), "%d", trigger_hash);
    /* djb2 as the cache spells it (RSCache_ArchiveNameHashDat2): 31, not 33,
     * and a signed 32-bit accumulator that is allowed to wrap. */
    for( int i = 0; name[i] != '\0'; i++ )
        hash = (int)((unsigned)hash * 31u + (unsigned)(unsigned char)name[i]);
    return hash;
}
