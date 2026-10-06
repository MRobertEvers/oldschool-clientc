#include "rscache_serverpack.h"

#include "archive.h"
#include "checksum.h"
#include "dat2disk.h"

#include <assert.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

/** A dat2 idx entry is 3 bytes of length + 3 bytes of sector. */
enum
{
    SERVERPACK_IDX_ENTRY = 6,
};

static void
put_u2(
    uint8_t* at,
    uint32_t value)
{
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

static void
put_u4(
    uint8_t* at,
    uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

static uint32_t
get_u2(const uint8_t* at)
{
    return ((uint32_t)at[0] << 8) | (uint32_t)at[1];
}

static uint32_t
get_u4(const uint8_t* at)
{
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) | ((uint32_t)at[2] << 8) |
           (uint32_t)at[3];
}

int
RSCache_ServerPackRecordsGroup(int config_kind)
{
    assert(config_kind >= 0);
    assert(config_kind < RSCACHE_SERVERPACK_RECORDS_GROUP_OFFSET);
    return config_kind + RSCACHE_SERVERPACK_RECORDS_GROUP_OFFSET;
}

int
RSCache_ServerPackRecordsArchive(int id)
{
    assert(id >= 0);
    return id >> RSCACHE_SERVERPACK_RECORDS_SHIFT;
}

/* ---- framing ------------------------------------------------------------- */

uint32_t
RSCache_ServerPackFrame(
    enum RSCache_ServerPackKind kind,
    const uint8_t* payload,
    uint32_t payload_size,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(payload);
    assert(out);
    assert(payload_size > 0);
    assert(out_capacity >= payload_size + RSCACHE_SERVERPACK_HEADER);
    out[0] = 'S';
    out[1] = 'P';
    out[2] = RSCACHE_SERVERPACK_VERSION;
    out[3] = (uint8_t)kind;
    put_u4(out + 4, RSCache_Crc32Buffer(payload, (size_t)payload_size));
    memmove(out + RSCACHE_SERVERPACK_HEADER, payload, (size_t)payload_size);
    return payload_size + RSCACHE_SERVERPACK_HEADER;
}

int
RSCache_ServerPackUnframe(
    const uint8_t* data,
    uint32_t size,
    enum RSCache_ServerPackKind kind,
    const uint8_t** out_payload,
    uint32_t* out_payload_size)
{
    assert(data);
    assert(out_payload);
    assert(out_payload_size);
    if( size < RSCACHE_SERVERPACK_HEADER || data[0] != 'S' || data[1] != 'P' )
        return 0;
    /* Exact, not `>=`: a version this build has never seen may have changed a
     * payload grammar, and decoding it as if it had not is the silent failure
     * the version byte exists to prevent. */
    if( data[2] != RSCACHE_SERVERPACK_VERSION || data[3] != (uint8_t)kind )
        return 0;
    if( get_u4(data + 4) != RSCache_Crc32Buffer(data + RSCACHE_SERVERPACK_HEADER,
                                                (size_t)(size - RSCACHE_SERVERPACK_HEADER)) )
        return 0;
    *out_payload = data + RSCACHE_SERVERPACK_HEADER;
    *out_payload_size = size - RSCACHE_SERVERPACK_HEADER;
    return 1;
}

/* ---- name tables --------------------------------------------------------- */

uint32_t
RSCache_ServerPackNamesEncode(
    const int* ids,
    const char* const* names,
    int count,
    uint8_t* out,
    uint32_t out_capacity)
{
    uint32_t need = 2;
    uint32_t at = 2;

    assert(ids);
    assert(names);
    assert(out);
    assert(count >= 0);
    assert(count <= 0xFFFF);
    /* Sized before anything is written. */
    for( int i = 0; i < count; i++ )
        need += 4 + (uint32_t)strlen(names[i] ? names[i] : "") + 1;
    if( out_capacity < need )
        return 0;
    put_u2(out, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        const char* name = names[i] ? names[i] : "";
        size_t length = strlen(name) + 1;

        put_u4(out + at, (uint32_t)ids[i]);
        at += 4;
        memcpy(out + at, name, length);
        at += (uint32_t)length;
    }
    return at;
}

int
RSCache_ServerPackNamesDecode(
    const uint8_t* data,
    uint32_t size,
    int* out_ids,
    const char** out_names,
    int max)
{
    uint32_t at = 2;
    int count;

    assert(data);
    if( size < 2 )
        return -1;
    count = (int)get_u2(data);
    for( int i = 0; i < count; i++ )
    {
        int id;
        const char* name;

        if( size - at < 5 )
            return -1;
        id = (int)get_u4(data + at);
        at += 4;
        name = (const char*)data + at;
        while( at < size && data[at] )
            at++;
        /* Terminated INSIDE the payload, or the caller gets a pointer into
         * whatever follows the archive. */
        if( at >= size )
            return -1;
        at++;
        if( i < max )
        {
            if( out_ids )
                out_ids[i] = id;
            if( out_names )
                out_names[i] = name;
        }
    }
    return count;
}

/* ---- client records ------------------------------------------------------ */

uint32_t
RSCache_ServerPackRecordsBound(
    int count,
    uint32_t body_bytes)
{
    assert(count >= 0);
    return 2 + (uint32_t)count * 8 + body_bytes;
}

uint32_t
RSCache_ServerPackRecordsEncode(
    const int* ids,
    const uint8_t* const* bodies,
    const uint32_t* sizes,
    int count,
    uint8_t* out,
    uint32_t out_capacity)
{
    uint32_t body_bytes = 0;
    uint32_t at = 2;

    assert(ids);
    assert(bodies);
    assert(sizes);
    assert(out);
    assert(count > 0);
    assert(count <= 0xFFFF);
    for( int i = 0; i < count; i++ )
    {
        assert(ids[i] >= 0);
        assert(i == 0 || ids[i] > ids[i - 1]);
        assert(RSCache_ServerPackRecordsArchive(ids[i]) == RSCache_ServerPackRecordsArchive(ids[0]));
        body_bytes += sizes[i];
    }
    assert(out_capacity >= RSCache_ServerPackRecordsBound(count, body_bytes));
    put_u2(out, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        put_u4(out + at, (uint32_t)ids[i]);
        put_u4(out + at + 4, sizes[i]);
        at += 8;
        if( sizes[i] )
            memcpy(out + at, bodies[i], sizes[i]);
        at += sizes[i];
    }
    return at;
}

int
RSCache_ServerPackRecordsBegin(
    struct RSCache_ServerPackRecords* cursor,
    const uint8_t* payload,
    uint32_t payload_size)
{
    assert(cursor);
    assert(payload);
    memset(cursor, 0, sizeof(*cursor));
    if( payload_size < 2 )
        return 0;
    cursor->data = payload;
    cursor->size = payload_size;
    cursor->position = 2;
    cursor->remaining = (int)get_u2(payload);
    cursor->last_id = -1;
    return 1;
}

int
RSCache_ServerPackRecordsNext(
    struct RSCache_ServerPackRecords* cursor,
    int* out_id,
    const uint8_t** out_body,
    uint32_t* out_size)
{
    uint32_t id;
    uint32_t size;

    assert(cursor);
    assert(out_id);
    assert(out_body);
    assert(out_size);
    if( cursor->remaining == 0 )
        return cursor->position == cursor->size ? 0 : -1;
    if( cursor->size - cursor->position < 8 )
        return -1;
    id = get_u4(cursor->data + cursor->position);
    size = get_u4(cursor->data + cursor->position + 4);
    if( id > 0x7FFFFFFFu || cursor->size - cursor->position - 8 < size )
        return -1;
    /* Ascending, as written: a payload whose ids go backwards is not one the
     * writer produced. */
    if( (int)id <= cursor->last_id )
        return -1;
    cursor->last_id = (int)id;
    *out_id = (int)id;
    *out_body = cursor->data + cursor->position + 8;
    *out_size = size;
    cursor->position += 8 + size;
    cursor->remaining--;
    return 1;
}

/* ---- id lists ------------------------------------------------------------ */

uint32_t
RSCache_ServerPackIdsEncode(
    const int* ids,
    int count,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(count >= 0);
    assert(count == 0 || ids);
    assert(out);
    assert(out_capacity >= 4 + 4 * (uint32_t)count);
    put_u4(out, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        assert(ids[i] >= 0);
        assert(i == 0 || ids[i] > ids[i - 1]);
        put_u4(out + 4 + 4 * (uint32_t)i, (uint32_t)ids[i]);
    }
    return 4 + 4 * (uint32_t)count;
}

int
RSCache_ServerPackIdsDecode(
    const uint8_t* payload,
    uint32_t size,
    int* out_ids,
    int max)
{
    uint32_t count;
    int64_t last = -1;

    assert(payload);
    if( size < 4 )
        return -1;
    count = get_u4(payload);
    if( count > 0x7FFFFFFFu || (uint64_t)size != 4 + 4 * (uint64_t)count )
        return -1;
    for( uint32_t i = 0; i < count; i++ )
    {
        uint32_t id = get_u4(payload + 4 + 4 * i);

        if( id > 0x7FFFFFFFu || (int64_t)id <= last )
            return -1;
        last = id;
        if( out_ids && (int)i < max )
            out_ids[i] = (int)id;
    }
    return (int)count;
}

/* ---- freshness --------------------------------------------------------------- */

struct FingerprintFiles
{
    char** paths;
    int count;
    int capacity;
};

static void
fingerprint_push(
    struct FingerprintFiles* files,
    const char* path)
{
    if( files->count == files->capacity )
    {
        files->capacity = files->capacity ? files->capacity * 2 : 1024;
        files->paths = realloc(files->paths, (size_t)files->capacity * sizeof(*files->paths));
        assert(files->paths);
    }
    files->paths[files->count] = strdup(path);
    assert(files->paths[files->count]);
    files->count++;
}

/* A config file of server/scripts: a config type's extension, a constant, or a
 * member index. */
static int
fingerprint_config_file(const char* name)
{
    static const char* const k_extensions[] = {
        "underlay", "overlay", "idk",    "inv",       "loc",     "enum",      "npc",
        "obj",      "param",   "seq",    "spotanim",  "varbit",  "varp",      "varc",
        "hitsplat", "healthbar", "struct", "mapelement", "dbrow", "dbtable", "constant",
        "compack",
    };
    const char* dot = strrchr(name, '.');

    if( !dot )
        return 0;
    for( size_t i = 0; i < sizeof(k_extensions) / sizeof(k_extensions[0]); i++ )
    {
        if( strcmp(dot + 1, k_extensions[i]) == 0 )
            return 1;
    }
    return 0;
}

/* Every file under `dir` (relative path `rel`), or only config files. */
static void
fingerprint_walk(
    struct FingerprintFiles* files,
    const char* srcdir,
    const char* rel,
    int configs_only,
    const char* only_extension)
{
    char dir[2048];
    DIR* handle;
    struct dirent* entry;

    snprintf(dir, sizeof(dir), "%s/%s", srcdir, rel);
    handle = opendir(dir);
    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char child[2048];
        char path[2048];
        struct stat info;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(child, sizeof(child), "%s/%s", rel, entry->d_name);
        snprintf(path, sizeof(path), "%s/%s", srcdir, child);
        if( stat(path, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
        {
            fingerprint_walk(files, srcdir, child, configs_only, only_extension);
            continue;
        }
        if( configs_only && !fingerprint_config_file(entry->d_name) )
            continue;
        if( only_extension )
        {
            const char* dot = strrchr(entry->d_name, '.');

            if( !dot || strcmp(dot + 1, only_extension) != 0 )
                continue;
        }
        fingerprint_push(files, child);
    }
    closedir(handle);
}

static int
fingerprint_order(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static uint64_t
fnv1a(
    uint64_t hash,
    const void* data,
    size_t size)
{
    const uint8_t* bytes = data;

    for( size_t i = 0; i < size; i++ )
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/* Eight bytes at a time (a 200 MB tree is read on every boot), then the tail. */
static uint64_t
mix_bytes(
    uint64_t hash,
    const uint8_t* bytes,
    size_t size)
{
    size_t i = 0;

    for( ; i + 8 <= size; i += 8 )
    {
        uint64_t word;

        memcpy(&word, bytes + i, 8);
        hash ^= word;
        hash *= 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 29;
    }
    return fnv1a(hash, bytes + i, size - i);
}

uint64_t
RSCache_ServerPackFingerprint(
    const char* srcdir,
    const char* const* lanes,
    int lane_count)
{
    enum
    {
        CHUNK = 1 << 20,
    };
    uint8_t* chunk = malloc(CHUNK);
    struct FingerprintFiles files;
    uint64_t hash = 14695981039346656037ull;
    int version = RSCACHE_SERVERPACK_VERSION;

    assert(srcdir);
    assert(lane_count >= 0);
    assert(lane_count == 0 || lanes);
    assert(chunk);
    memset(&files, 0, sizeof(files));
    fingerprint_walk(&files, srcdir, "configs", 0, NULL);
    fingerprint_walk(&files, srcdir, "fields", 0, NULL);
    fingerprint_walk(&files, srcdir, "pack", 0, NULL);
    fingerprint_walk(&files, srcdir, "interfaces", 0, "compack");
    fingerprint_walk(&files, srcdir, "server/scripts", 1, NULL);
    for( int l = 0; l < lane_count; l++ )
    {
        char rel[512];

        snprintf(rel, sizeof(rel), "ported/%s/configs", lanes[l]);
        fingerprint_walk(&files, srcdir, rel, 0, NULL);
    }
    {
        char dir[2048];
        DIR* handle;
        struct dirent* entry;

        snprintf(dir, sizeof(dir), "%s/ported", srcdir);
        handle = opendir(dir);
        while( handle && (entry = readdir(handle)) != NULL )
        {
            char rel[512];

            if( entry->d_name[0] == '.' )
                continue;
            snprintf(rel, sizeof(rel), "ported/%s/pack", entry->d_name);
            fingerprint_walk(&files, srcdir, rel, 0, NULL);
        }
        if( handle )
            closedir(handle);
    }
    qsort(files.paths, (size_t)files.count, sizeof(*files.paths), fingerprint_order);

    hash = fnv1a(hash, &version, sizeof(version));
    for( int l = 0; l < lane_count; l++ )
        hash = fnv1a(hash, lanes[l], strlen(lanes[l]) + 1);
    for( int i = 0; i < files.count; i++ )
    {
        char path[2048];
        FILE* file;
        int64_t size = 0;

        snprintf(path, sizeof(path), "%s/%s", srcdir, files.paths[i]);
        hash = fnv1a(hash, files.paths[i], strlen(files.paths[i]) + 1);
        /* The bytes, not the modification time: a pack shipped in a zip, or a tree
         * freshly checked out, has new mtimes and the same content. */
        file = fopen(path, "rb");
        if( file )
        {
            size_t got;

            while( (got = fread(chunk, 1, CHUNK, file)) > 0 )
            {
                hash = mix_bytes(hash, chunk, got);
                size += (int64_t)got;
            }
            fclose(file);
        }
        else
            size = -1;
        hash = fnv1a(hash, &size, sizeof(size));
        free(files.paths[i]);
    }
    free(files.paths);
    free(chunk);
    return hash;
}

int
RSCache_ServerPackStampWrite(
    const char* dir,
    const struct RSCache_ServerPackStamp* stamp)
{
    char path[1100];
    FILE* file;

    assert(dir);
    assert(stamp);
    snprintf(path, sizeof(path), "%s/stamp.txt", dir);
    file = fopen(path, "wb");
    if( !file )
        return 0;
    fprintf(file, "serverpack %d\nfingerprint %016" PRIx64 "\nwriter %016" PRIx64 "\n",
            RSCACHE_SERVERPACK_VERSION, stamp->fingerprint, stamp->writer);
    for( int l = 0; l < stamp->lane_count; l++ )
        fprintf(file, "lane %s\n", stamp->lanes[l]);
    fclose(file);
    return 1;
}

int
RSCache_ServerPackStampRead(
    const char* dir,
    struct RSCache_ServerPackStamp* stamp)
{
    char path[1100];
    char line[256];
    FILE* file;
    int version = -1;
    int have_fingerprint = 0;

    assert(dir);
    assert(stamp);
    memset(stamp, 0, sizeof(*stamp));
    snprintf(path, sizeof(path), "%s/stamp.txt", dir);
    file = fopen(path, "rb");
    if( !file )
        return 0;
    while( fgets(line, sizeof(line), file) )
    {
        char word[64];
        char lane[64];

        if( sscanf(line, "serverpack %d", &version) == 1 )
            continue;
        if( sscanf(line, "fingerprint %" SCNx64, &stamp->fingerprint) == 1 )
        {
            have_fingerprint = 1;
            continue;
        }
        if( sscanf(line, "writer %" SCNx64, &stamp->writer) == 1 )
            continue;
        if( sscanf(line, "%63s %63s", word, lane) == 2 && strcmp(word, "lane") == 0 &&
            stamp->lane_count < RSCACHE_SERVERPACK_STAMP_LANES )
            snprintf(stamp->lanes[stamp->lane_count++], sizeof(stamp->lanes[0]), "%s", lane);
    }
    fclose(file);
    return version == RSCACHE_SERVERPACK_VERSION && have_fingerprint;
}

/* ---- reading the directory ------------------------------------------------ */

int
RSCache_ServerPackOpen(
    struct RSCache_ServerPack* pack,
    const char* dir)
{
    char path[1100];

    assert(pack);
    assert(dir);
    memset(pack, 0, sizeof(*pack));
    snprintf(pack->dir, sizeof(pack->dir), "%s", dir);
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", pack->dir);
    pack->dat2 = fopen(path, "rb");
    return pack->dat2 != NULL;
}

void
RSCache_ServerPackClose(struct RSCache_ServerPack* pack)
{
    if( !pack )
        return;
    if( pack->dat2 )
        fclose(pack->dat2);
    for( int i = 0; i < pack->idx_count; i++ )
    {
        if( pack->idx[i].file )
            fclose(pack->idx[i].file);
    }
    memset(pack, 0, sizeof(*pack));
}

static struct RSCache_ServerPackIdx*
idx_for_group(
    struct RSCache_ServerPack* pack,
    int group)
{
    char path[1100];
    struct RSCache_ServerPackIdx* idx;
    long size;

    for( int i = 0; i < pack->idx_count; i++ )
    {
        if( pack->idx[i].group == group )
            return &pack->idx[i];
    }
    assert(pack->idx_count < (int)(sizeof(pack->idx) / sizeof(pack->idx[0])));

    /* A group with no idx file is recorded too (file NULL, 0 entries), so a scan
     * over an absent group asks the filesystem once, not per id. */
    idx = &pack->idx[pack->idx_count++];
    idx->group = group;
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", pack->dir, group);
    idx->file = fopen(path, "rb");
    if( !idx->file )
        return idx;
    fseek(idx->file, 0, SEEK_END);
    size = ftell(idx->file);
    idx->entries = size > 0 ? (int)(size / SERVERPACK_IDX_ENTRY) : 0;
    return idx;
}

int
RSCache_ServerPackEntryCount(
    struct RSCache_ServerPack* pack,
    int group)
{
    assert(pack);
    return idx_for_group(pack, group)->entries;
}

int
RSCache_ServerPackRead(
    struct RSCache_ServerPack* pack,
    int group,
    int archive_id,
    enum RSCache_ServerPackKind kind,
    void** out_owned,
    const uint8_t** out_payload,
    uint32_t* out_size)
{
    struct RSCache_ServerPackIdx* idx;
    struct RSCache_Dat2DiskIndexRecord record = { 0 };
    struct RSCache_Dat2DiskArchive archive = { 0 };

    assert(pack);
    assert(pack->dat2);
    assert(out_owned);
    assert(out_payload);
    assert(out_size);
    *out_owned = NULL;
    idx = idx_for_group(pack, group);
    if( !idx->file || archive_id < 0 || archive_id >= idx->entries )
        return RSCACHE_SERVERPACK_ABSENT;
    /* Nonzero is "no such archive", not corruption: the idx zero-fills the gaps
     * between the ids that exist. A damaged archive shows only once there are
     * bytes to hold to the header, below. */
    if( RSCache_Dat2DiskIndexFileReadRecord(idx->file, archive_id, &record) != 0 )
        return RSCACHE_SERVERPACK_ABSENT;
    if( record.sector <= 0 || record.length <= 0 )
        return RSCACHE_SERVERPACK_ABSENT;
    if( RSCache_Dat2DiskDat2FileReadArchive(pack->dat2, group, archive_id, record.sector,
                                            record.length, &archive) != 0 )
        return RSCACHE_SERVERPACK_INVALID;
    if( !RSCache_ArchiveDecryptDecompress(&archive, NULL) ||
        !RSCache_ServerPackUnframe((const uint8_t*)archive.data, (uint32_t)archive.data_size,
                                   kind, out_payload, out_size) )
    {
        free(archive.data);
        return RSCACHE_SERVERPACK_INVALID;
    }
    *out_owned = archive.data;
    return 1;
}
