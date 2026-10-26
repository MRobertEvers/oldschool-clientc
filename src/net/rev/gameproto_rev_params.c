#include "gameproto_revisions.h"

#include "log/torirs_log.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
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

static struct GameProtoRevTable const* (*const k_generated[])(void) = {
#define REV_FN(ID, NUMBER) GameProtoRev_##ID,
    REV_LIST(REV_FN)
#undef REV_FN
};

struct GameProtoRevTable const*
GameProtoRev_Osrs(int revision)
{
    for( size_t i = 0; i < sizeof(k_generated) / sizeof(k_generated[0]); i++ )
    {
        struct GameProtoRevTable const* t = k_generated[i]();

        if( t->revision == GAMEPROTO_REVISION_OSRS && t->number == revision )
            return t;
    }
    return NULL;
}

struct GameProtoRevTable const*
GameProtoRev_GeneratedByName(char const* name)
{
    assert(name);
    for( size_t i = 0; i < sizeof(k_generated) / sizeof(k_generated[0]); i++ )
    {
        struct GameProtoRevTable const* t = k_generated[i]();

        if( strcmp(t->name, name) == 0 )
            return t;
    }
    return NULL;
}

struct GameProtoRevTable const*
GameProtoRev_ByName(char const* name)
{
    /* The names launch manifests and replay files already carry: the
     * LostCity revisions' before they were RSProt's rs family, and "rsprox<N>"
     * for an OSRS revision. */
    static char const* const k_alias[][2] = {
        { "lc245_2", "rs245_2" },
        { "lc254", "rs254" },
        { "lc289", "rs289" },
    };

    assert(name);
    for( size_t i = 0; i < sizeof(k_alias) / sizeof(k_alias[0]); i++ )
        if( strcmp(name, k_alias[i][0]) == 0 )
            return GameProtoRev_GeneratedByName(k_alias[i][1]);
    if( strncmp(name, "rsprox", 6) == 0 )
    {
        char* end = NULL;
        long revision = strtol(name + 6, &end, 10);

        if( end && *end == '\0' && end != name + 6 )
            return GameProtoRev_Osrs((int)revision);
    }
    return GameProtoRev_GeneratedByName(name);
}

void
GameProtoRev_JagChecksumsFromEnv(struct GameProtoRevTable const* rev, char const* env)
{
    char const* value;
    char buf[256];
    int32_t crc[9] = { 0 };
    int i = 0;

    assert(rev);
    assert(env);
    value = getenv(env);
    if( !value )
        return;
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for( char* tok = strtok(buf, ","); tok && i < 9; tok = strtok(NULL, ",") )
        crc[i++] = (int32_t)strtol(tok, NULL, 10);
    if( i != 9 )
        TORIRS_LOG("%s: %s had %d of 9 values; login CRCs incomplete\n", rev->name, env, i);
    GameProtoRev_SetJagChecksums(rev, crc);
}
