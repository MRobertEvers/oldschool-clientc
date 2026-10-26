#include "gameproto_revisions.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * Boot-manifest login-parameter setters. See gameproto_revisions.h. These
 * mutate the rev-table singleton returned by GameProtoRev_* getters; the
 * const-cast is intentional and safe only at boot before ToriRS_Network_Init.
 */

void
GameProtoRev_SetJagChecksums(
    struct GameProtoRevTable const* rev,
    int32_t const crc[9])
{
    assert(rev && crc);
    struct GameProtoRevTable* mut = (struct GameProtoRevTable*)rev;
    memcpy(mut->jag_checksum, crc, sizeof(mut->jag_checksum));
}

void
GameProtoRev_SetClientVersion(
    struct GameProtoRevTable const* rev,
    int version)
{
    assert(rev && version > 0);
    struct GameProtoRevTable* mut = (struct GameProtoRevTable*)rev;
    mut->client_version = version;
}

static struct GameProtoRevTable const* (*const k_osrs[])(void) = {
#define REV_FN(ID, NUMBER) GameProtoRev_##ID,
    REV_LIST(REV_FN)
#undef REV_FN
};

struct GameProtoRevTable const*
GameProtoRev_Osrs(int revision)
{
    for( size_t i = 0; i < sizeof(k_osrs) / sizeof(k_osrs[0]); i++ )
    {
        struct GameProtoRevTable const* t = k_osrs[i]();

        if( t->number == revision )
            return t;
    }
    return NULL;
}

struct GameProtoRevTable const*
GameProtoRev_OsrsByName(char const* name)
{
    assert(name);
    for( size_t i = 0; i < sizeof(k_osrs) / sizeof(k_osrs[0]); i++ )
    {
        struct GameProtoRevTable const* t = k_osrs[i]();

        if( strcmp(t->name, name) == 0 )
            return t;
    }
    return NULL;
}
