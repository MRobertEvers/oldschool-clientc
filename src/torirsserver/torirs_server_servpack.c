#include "torirs_server_servpack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
ToriRSServer_ServPackDir(
    const char* content_dir,
    const char* cache_dir,
    char* out,
    size_t out_size)
{
    const char* env = getenv("TORIRSSERVER_PACK_DIR");

    assert(content_dir);
    assert(out);
    if( env && env[0] )
    {
        snprintf(out, out_size, "%s", env);
        return;
    }
    if( cache_dir && cache_dir[0] )
    {
        char probe[1100];
        size_t length = strlen(cache_dir);
        FILE* file;

        while( length > 1 && cache_dir[length - 1] == '/' )
            length--;
        snprintf(probe, sizeof(probe), "%.*s.serverpack/main_file_cache.dat2", (int)length,
                 cache_dir);
        file = fopen(probe, "rb");
        if( file )
        {
            fclose(file);
            snprintf(out, out_size, "%.*s.serverpack", (int)length, cache_dir);
            return;
        }
    }
    snprintf(out, out_size, "%s/server/pack", content_dir);
}

int
ToriRSServer_ServPackOpen(
    struct RSCache_ServerPack* pack,
    const char* pack_dir)
{
    assert(pack);
    assert(pack_dir);
    if( !RSCache_ServerPackOpen(pack, pack_dir) )
    {
        fprintf(stderr,
                "torirsserver: no server pack at %s — the server reads its config records from "
                "nothing else; run `%s`\n",
                pack_dir, TORIRSSERVER_SERVPACK_FIX);
        return 0;
    }
    return 1;
}

int
ToriRSServer_ServPackFresh(
    const char* pack_dir,
    const char* content_dir)
{
    struct RSCache_ServerPackStamp stamp;
    const char* lanes[RSCACHE_SERVERPACK_STAMP_LANES];
    const char* allow = getenv("TORIRSSERVER_ALLOW_STALE_PACK");

    assert(pack_dir);
    assert(content_dir);
    if( !RSCache_ServerPackStampRead(pack_dir, &stamp) )
    {
        fprintf(stderr,
                "torirsserver: the server pack at %s has no stamp (an interrupted or partial "
                "write) — run `%s`\n",
                pack_dir, TORIRSSERVER_SERVPACK_FIX);
        return allow && allow[0] == '1';
    }
    for( int l = 0; l < stamp.lane_count; l++ )
        lanes[l] = stamp.lanes[l];
    if( RSCache_ServerPackFingerprint(content_dir, lanes, stamp.lane_count) == stamp.fingerprint )
        return 1;
    fprintf(stderr,
            "torirsserver: the server pack at %s is STALE — %s changed since it was written; "
            "run `%s`%s\n",
            pack_dir, content_dir, TORIRSSERVER_SERVPACK_FIX,
            allow && allow[0] == '1' ? " (TORIRSSERVER_ALLOW_STALE_PACK=1: booting anyway)" : "");
    return allow && allow[0] == '1';
}

/** A refusal, said once per archive with the coordinates that name it. */
static void
report_invalid(
    const struct RSCache_ServerPack* pack,
    int group,
    int archive,
    const char* what)
{
    fprintf(stderr,
            "torirsserver: server pack %s: archive (%d, %d) %s — the pack is stale or "
            "truncated; rebuild it with `%s`\n",
            pack->dir, group, archive, what, TORIRSSERVER_SERVPACK_FIX);
}

int
ToriRSServer_ServPackEachRecord(
    struct RSCache_ServerPack* pack,
    int config_kind,
    ToriRSServerServPackRecordFn visit,
    void* context)
{
    int group = RSCache_ServerPackRecordsGroup(config_kind);
    int entries;
    int visited = 0;

    assert(pack);
    assert(visit);
    entries = RSCache_ServerPackEntryCount(pack, group);
    for( int archive = 0; archive < entries; archive++ )
    {
        struct RSCache_ServerPackRecords cursor;
        void* owned = NULL;
        const uint8_t* payload = NULL;
        uint32_t payload_size = 0;
        int read = RSCache_ServerPackRead(pack, group, archive, RSCACHE_SERVERPACK_KIND_RECORDS,
                                          &owned, &payload, &payload_size);

        if( read == RSCACHE_SERVERPACK_ABSENT )
            continue;
        if( read == RSCACHE_SERVERPACK_INVALID )
        {
            report_invalid(pack, group, archive,
                           "does not validate (magic, version, kind or CRC)");
            return -1;
        }
        if( !RSCache_ServerPackRecordsBegin(&cursor, payload, payload_size) )
        {
            report_invalid(pack, group, archive, "is too short to hold its record count");
            free(owned);
            return -1;
        }
        for( ;; )
        {
            int id;
            const uint8_t* body;
            uint32_t size;
            int next = RSCache_ServerPackRecordsNext(&cursor, &id, &body, &size);

            if( next == 0 )
                break;
            /* A record filed under another archive's ids is not one the writer
             * produced, whatever the cursor makes of its bytes. */
            if( next < 0 || RSCache_ServerPackRecordsArchive(id) != archive )
            {
                report_invalid(pack, group, archive, "holds a malformed record list");
                free(owned);
                return -1;
            }
            visit(context, id, body, size);
            visited++;
        }
        free(owned);
    }
    return visited;
}

int
ToriRSServer_ServPackEachBand(
    struct RSCache_ServerPack* pack,
    int config_kind,
    ToriRSServerServPackBandFn visit,
    void* context)
{
    int entries;
    int visited = 0;

    assert(pack);
    assert(visit);
    entries = RSCache_ServerPackEntryCount(pack, config_kind);
    for( int id = 0; id < entries; id++ )
    {
        void* owned = NULL;
        const uint8_t* band = NULL;
        uint32_t size = 0;
        int read = RSCache_ServerPackRead(pack, config_kind, id, RSCACHE_SERVERPACK_KIND_BAND,
                                          &owned, &band, &size);

        if( read == RSCACHE_SERVERPACK_ABSENT )
            continue;
        if( read == RSCACHE_SERVERPACK_INVALID )
        {
            report_invalid(pack, config_kind, id,
                           "does not validate (magic, version, kind or CRC)");
            return -1;
        }
        visit(context, id, band, size);
        visited++;
        free(owned);
    }
    return visited;
}

int
ToriRSServer_ServPackDefaults(
    struct RSCache_ServerPack* pack,
    int config_kind,
    void** out_owned,
    const uint8_t** out_band,
    uint32_t* out_size)
{
    int read;

    assert(pack);
    assert(out_owned);
    assert(out_band);
    assert(out_size);
    read = RSCache_ServerPackRead(pack, TORIRSSERVER_SERVPACK_DEFAULTS_GROUP, config_kind,
                                  RSCACHE_SERVERPACK_KIND_BAND, out_owned, out_band, out_size);
    if( read == 1 )
        return 1;
    report_invalid(pack, TORIRSSERVER_SERVPACK_DEFAULTS_GROUP, config_kind,
                   read == RSCACHE_SERVERPACK_ABSENT
                       ? "is missing: this pack predates the `[default]` band"
                       : "does not validate (magic, version, kind or CRC)");
    return 0;
}

static void
kind_collect(
    void* context,
    int id,
    const uint8_t* body,
    uint32_t size)
{
    struct ToriRSServerKindRecords* out = context;

    if( out->count == out->capacity )
    {
        out->capacity = out->capacity ? out->capacity * 2 : 1024;
        out->ids = realloc(out->ids, (size_t)out->capacity * sizeof(*out->ids));
        out->files = realloc(out->files, (size_t)out->capacity * sizeof(*out->files));
        out->sizes = realloc(out->sizes, (size_t)out->capacity * sizeof(*out->sizes));
        assert(out->ids);
        assert(out->files);
        assert(out->sizes);
    }
    /* One spare byte so an empty body is still a distinct allocation. */
    out->files[out->count] = malloc((size_t)size + 1);
    assert(out->files[out->count]);
    if( size )
        memcpy(out->files[out->count], body, size);
    out->ids[out->count] = id;
    out->sizes[out->count] = size;
    out->count++;
}

int
ToriRSServer_ServPackKindLoad(
    struct RSCache_ServerPack* pack,
    int config_kind,
    struct ToriRSServerKindRecords* out)
{
    assert(pack);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( ToriRSServer_ServPackEachRecord(pack, config_kind, kind_collect, out) < 0 )
    {
        ToriRSServer_ServPackKindFree(out);
        return 0;
    }
    return 1;
}

int
ToriRSServer_ServPackKindIdBound(const struct ToriRSServerKindRecords* records)
{
    assert(records);
    /* Ascending by construction (ToriRSServer_ServPackEachRecord). */
    return records->count ? records->ids[records->count - 1] + 1 : 0;
}

void
ToriRSServer_ServPackKindFree(struct ToriRSServerKindRecords* records)
{
    if( !records )
        return;
    for( int i = 0; i < records->count; i++ )
        free(records->files[i]);
    free(records->ids);
    free(records->files);
    free(records->sizes);
    memset(records, 0, sizeof(*records));
}

int
ToriRSServer_ServPackClientIdBound(
    struct RSCache_ServerPack* pack,
    int config_kind)
{
    void* owned = NULL;
    const uint8_t* payload = NULL;
    uint32_t size = 0;
    int read;
    int count;
    int bound = 0;

    assert(pack);
    read = RSCache_ServerPackRead(pack, RSCACHE_SERVERPACK_CLIENT_IDS_GROUP, config_kind,
                                  RSCACHE_SERVERPACK_KIND_IDS, &owned, &payload, &size);
    if( read != 1 )
    {
        report_invalid(pack, RSCACHE_SERVERPACK_CLIENT_IDS_GROUP, config_kind,
                       read == RSCACHE_SERVERPACK_ABSENT ? "is missing"
                                                         : "does not validate");
        return -1;
    }
    count = RSCache_ServerPackIdsDecode(payload, size, NULL, 0);
    if( count > 0 )
    {
        int* ids = malloc((size_t)count * sizeof(int));

        assert(ids);
        RSCache_ServerPackIdsDecode(payload, size, ids, count);
        bound = ids[count - 1] + 1;
        free(ids);
    }
    free(owned);
    if( count < 0 )
    {
        report_invalid(pack, RSCACHE_SERVERPACK_CLIENT_IDS_GROUP, config_kind,
                       "holds a malformed id list");
        return -1;
    }
    return bound;
}
