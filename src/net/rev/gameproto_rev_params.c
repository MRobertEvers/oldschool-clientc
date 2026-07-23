#include "gameproto_revisions.h"

#include <assert.h>
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
