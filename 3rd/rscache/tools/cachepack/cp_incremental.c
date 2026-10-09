/*
 * The incremental server pack. See cp_incremental.h for the contract and
 * docs/serverpack.md for the design and the measurements behind it.
 */

#include "cp_incremental.h"

#include "cp_walk.h"
#include "dat2disk.h"
#include "rscache_serverpack.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define cp_dup _dup
#define cp_dup2 _dup2
#define cp_close _close
#define cp_fileno _fileno
#define cp_getpid _getpid
#define cp_mkdir(path) _mkdir(path)
#else
#include <sys/file.h>
#include <unistd.h>
#define cp_dup dup
#define cp_dup2 dup2
#define cp_close close
#define cp_fileno fileno
#define cp_getpid getpid
#define cp_mkdir(path) mkdir(path, 0755)
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

enum
{
    STATE_MAGIC = 0x54535043u, /* "CPST" */
    STATE_FORMAT = 1,
    NAMES_UNIT = CP_TYPE_COUNT, /* the stat/category name tables, after every type */
    UNIT_COUNT = CP_TYPE_COUNT + 1,
};

/* ------------------------------------------------------------------ */
/* Hashing and small platform pieces                                   */
/* ------------------------------------------------------------------ */

static uint64_t
mix(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

static uint64_t
hash_bytes(const void* data, size_t length, uint64_t seed)
{
    const unsigned char* bytes = (const unsigned char*)data;
    uint64_t h = seed ^ (length * 0x9ddfea08eb382d69ull);
    size_t i = 0;

    for( ; i + 8 <= length; i += 8 )
    {
        uint64_t word;

        memcpy(&word, bytes + i, 8);
        h = (h ^ mix(word)) * 0x9e3779b97f4a7c15ull;
    }
    if( i < length )
    {
        uint64_t word = 0;

        memcpy(&word, bytes + i, length - i);
        h = (h ^ mix(word ^ 0x5bd1e995ull)) * 0x9e3779b97f4a7c15ull;
    }
    return mix(h);
}

static uint64_t
hash_u64(uint64_t h, uint64_t value)
{
    return mix((h ^ mix(value + 0x632be59bd9b4e019ull)) * 0x9e3779b97f4a7c15ull);
}

static uint64_t
hash_str(uint64_t h, const char* text)
{
    return hash_u64(h, hash_bytes(text ? text : "", text ? strlen(text) : 0, 0x9e3779b97f4a7c15ull));
}

static double
now_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
#endif
}

static int64_t
mtime_ns(const struct stat* info)
{
#if defined(__APPLE__)
    return (int64_t)info->st_mtimespec.tv_sec * 1000000000 + info->st_mtimespec.tv_nsec;
#elif defined(_WIN32)
    return (int64_t)info->st_mtime * 1000000000;
#else
    return (int64_t)info->st_mtim.tv_sec * 1000000000 + info->st_mtim.tv_nsec;
#endif
}

static int
replace_file(const char* from, const char* to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(from, to);
#endif
}

static char*
read_whole(const char* path, size_t* out_length)
{
    FILE* file = fopen(path, "rb");
    char* data;
    long size;

    *out_length = 0;
    if( !file )
        return NULL;
    if( fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0 )
    {
        fclose(file);
        return NULL;
    }
    data = (char*)malloc((size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return NULL;
    }
    data[size] = '\0';
    fclose(file);
    *out_length = (size_t)size;
    return data;
}

static uint64_t
hash_file(const char* path, int* ok)
{
    size_t length;
    char* data = read_whole(path, &length);
    uint64_t h;

    *ok = data != NULL;
    if( !data )
        return 0;
    h = hash_bytes(data, length, 0x9e3779b97f4a7c15ull);
    free(data);
    return h;
}

static uint64_t
executable_hash(const char* argv0)
{
    char path[2048];
    int ok = 0;
    uint64_t h;

    path[0] = '\0';
#if defined(__APPLE__)
    {
        uint32_t size = sizeof(path);

        if( _NSGetExecutablePath(path, &size) != 0 )
            path[0] = '\0';
    }
#elif defined(_WIN32)
    if( !GetModuleFileNameA(NULL, path, sizeof(path)) )
        path[0] = '\0';
#else
    {
        ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);

        path[length > 0 ? length : 0] = '\0';
    }
#endif
    if( !path[0] && argv0 )
        snprintf(path, sizeof(path), "%s", argv0);
    h = hash_file(path, &ok);
    return ok ? h : 0;
}

/* The output directory's lock, held for the whole build; the server takes it
 * shared while it reads the pack, so it never sees half of a swap. */
struct Lock
{
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
};

static int
lock_take(struct Lock* lock, const char* dir)
{
    char path[1200];

    snprintf(path, sizeof(path), "%s/.pack.lock", dir);
#ifdef _WIN32
    {
        OVERLAPPED overlapped;

        lock->handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if( lock->handle == INVALID_HANDLE_VALUE )
            return 0;
        memset(&overlapped, 0, sizeof(overlapped));
        return LockFileEx(lock->handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped) ? 1 : 0;
    }
#else
    lock->fd = open(path, O_RDWR | O_CREAT, 0644);
    if( lock->fd < 0 )
        return 0;
    while( flock(lock->fd, LOCK_EX) != 0 )
    {
        if( errno != EINTR )
            return 0;
    }
    return 1;
#endif
}

static void
lock_release(struct Lock* lock)
{
#ifdef _WIN32
    if( lock->handle != INVALID_HANDLE_VALUE )
        CloseHandle(lock->handle);
#else
    if( lock->fd >= 0 )
        close(lock->fd);
#endif
}

/* ------------------------------------------------------------------ */
/* Buffers and the state file's encoding                               */
/* ------------------------------------------------------------------ */

struct Buf
{
    uint8_t* data;
    size_t length;
    size_t capacity;
};

static void
buf_put(struct Buf* buf, const void* data, size_t length)
{
    if( buf->length + length > buf->capacity )
    {
        while( buf->length + length > buf->capacity )
            buf->capacity = buf->capacity ? buf->capacity * 2 : 65536;
        buf->data = (uint8_t*)realloc(buf->data, buf->capacity);
        assert(buf->data);
    }
    if( length )
        memcpy(buf->data + buf->length, data, length);
    buf->length += length;
}

static void
buf_u32(struct Buf* buf, uint32_t value)
{
    buf_put(buf, &value, 4);
}

static void
buf_u64(struct Buf* buf, uint64_t value)
{
    buf_put(buf, &value, 8);
}

static void
buf_str(struct Buf* buf, const char* text)
{
    uint32_t length = text ? (uint32_t)strlen(text) : 0;

    buf_u32(buf, length);
    buf_put(buf, text ? text : "", length);
}

static void
buf_printf(struct Buf* buf, const char* fmt, ...)
{
    va_list args;
    char line[4096];
    int n;

    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    assert(n >= 0);
    assert((size_t)n < sizeof(line));
    buf_put(buf, line, (size_t)n);
}

struct Reader
{
    const uint8_t* data;
    size_t length;
    size_t at;
    int bad;
};

static const void*
rd_take(struct Reader* reader, size_t length)
{
    const void* here;

    if( reader->bad || reader->length - reader->at < length )
    {
        reader->bad = 1;
        return NULL;
    }
    here = reader->data + reader->at;
    reader->at += length;
    return here;
}

static uint32_t
rd_u32(struct Reader* reader)
{
    uint32_t value = 0;
    const void* p = rd_take(reader, 4);

    if( p )
        memcpy(&value, p, 4);
    return value;
}

static uint64_t
rd_u64(struct Reader* reader)
{
    uint64_t value = 0;
    const void* p = rd_take(reader, 8);

    if( p )
        memcpy(&value, p, 8);
    return value;
}

static char*
rd_str(struct Reader* reader)
{
    uint32_t length = rd_u32(reader);
    const char* p = (const char*)rd_take(reader, length);
    char* text = (char*)malloc((size_t)length + 1);

    assert(text);
    if( p )
        memcpy(text, p, length);
    text[p ? length : 0] = '\0';
    return text;
}

static uint8_t*
rd_bytes(struct Reader* reader, uint32_t length)
{
    const void* p = rd_take(reader, length);
    uint8_t* copy;

    if( !p )
        return NULL;
    copy = (uint8_t*)malloc(length ? length : 1);
    assert(copy);
    memcpy(copy, p, length);
    return copy;
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

/** A file the pack is built from, relative to the tree. */
struct Input
{
    char* rel;
    int64_t size;
    int64_t mtime;
    uint64_t hash;
    int hashed;
    int present;
};

struct Lookup
{
    int op;
    int type;
    int id;
    char* name;
    uint64_t digest;
};

struct Archive
{
    int table;
    int archive;
    uint32_t length;
    uint8_t* data;
    uint64_t hash;
    uint32_t rev;
};

struct Unit
{
    int present;   /* the type packed (or was skipped) in that build */
    int result;    /* cp_server_pack_type's answer: 1 wrote, 0 nothing to write */
    uint64_t key;  /* hash of the type's own files */
    int mutates;   /* minted names while packing: never reused */
    uint32_t rev;

    struct Lookup* lookups;
    int lookup_count;
    int lookup_capacity;

    struct Archive* archives;
    int archive_count;
    int archive_capacity;

    char* out_text;
    char* err_text;
    int server_records;
    int membership_errors;
    int unresolved;
};

struct State
{
    int loaded;
    uint64_t global;
    uint32_t generation;
    struct Input* inputs;
    int input_count;
    struct Unit units[UNIT_COUNT];
    uint64_t store_hash;
};

static void
unit_free(struct Unit* unit)
{
    for( int i = 0; i < unit->lookup_count; i++ )
        free(unit->lookups[i].name);
    free(unit->lookups);
    for( int i = 0; i < unit->archive_count; i++ )
        free(unit->archives[i].data);
    free(unit->archives);
    free(unit->out_text);
    free(unit->err_text);
    memset(unit, 0, sizeof(*unit));
}

static void
state_free(struct State* state)
{
    for( int i = 0; i < state->input_count; i++ )
        free(state->inputs[i].rel);
    free(state->inputs);
    for( int u = 0; u < UNIT_COUNT; u++ )
        unit_free(&state->units[u]);
    memset(state, 0, sizeof(*state));
}

static const char*
state_read(struct State* state, const char* path)
{
    size_t length;
    char* data = read_whole(path, &length);
    struct Reader reader;
    uint32_t count;

    memset(state, 0, sizeof(*state));
    if( !data )
        return "no state (first build into this directory)";
    memset(&reader, 0, sizeof(reader));
    reader.data = (const uint8_t*)data;
    reader.length = length;
    if( rd_u32(&reader) != STATE_MAGIC || rd_u32(&reader) != STATE_FORMAT )
    {
        free(data);
        return "state from another format";
    }
    state->global = rd_u64(&reader);
    state->generation = rd_u32(&reader);
    state->store_hash = rd_u64(&reader);
    count = rd_u32(&reader);
    if( count > 1000000 )
        reader.bad = 1;
    state->inputs = (struct Input*)calloc(count ? count : 1, sizeof(struct Input));
    assert(state->inputs);
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        struct Input* input = &state->inputs[state->input_count++];

        input->rel = rd_str(&reader);
        input->size = (int64_t)rd_u64(&reader);
        input->mtime = (int64_t)rd_u64(&reader);
        input->hash = rd_u64(&reader);
        input->present = (int)rd_u32(&reader);
        input->hashed = 1;
    }
    for( int u = 0; u < UNIT_COUNT && !reader.bad; u++ )
    {
        struct Unit* unit = &state->units[u];

        unit->present = (int)rd_u32(&reader);
        unit->result = (int)rd_u32(&reader);
        unit->key = rd_u64(&reader);
        unit->mutates = (int)rd_u32(&reader);
        unit->rev = rd_u32(&reader);
        unit->server_records = (int)rd_u32(&reader);
        unit->membership_errors = (int)rd_u32(&reader);
        unit->unresolved = (int)rd_u32(&reader);
        unit->out_text = rd_str(&reader);
        unit->err_text = rd_str(&reader);
        count = rd_u32(&reader);
        if( count > 10000000 )
        {
            reader.bad = 1;
            break;
        }
        unit->lookups = (struct Lookup*)calloc(count ? count : 1, sizeof(struct Lookup));
        assert(unit->lookups);
        unit->lookup_capacity = (int)(count ? count : 1);
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            struct Lookup* lookup = &unit->lookups[unit->lookup_count++];

            lookup->op = (int)rd_u32(&reader);
            lookup->type = (int)rd_u32(&reader);
            lookup->id = (int)rd_u32(&reader);
            lookup->name = rd_str(&reader);
            lookup->digest = rd_u64(&reader);
        }
        count = rd_u32(&reader);
        if( count > 10000000 )
        {
            reader.bad = 1;
            break;
        }
        unit->archives = (struct Archive*)calloc(count ? count : 1, sizeof(struct Archive));
        assert(unit->archives);
        unit->archive_capacity = (int)(count ? count : 1);
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            struct Archive* archive = &unit->archives[unit->archive_count++];

            archive->table = (int)rd_u32(&reader);
            archive->archive = (int)rd_u32(&reader);
            archive->rev = rd_u32(&reader);
            archive->length = rd_u32(&reader);
            archive->data = rd_bytes(&reader, archive->length);
            archive->hash = hash_bytes(archive->data, archive->length, 0x9e3779b97f4a7c15ull);
        }
    }
    free(data);
    if( reader.bad )
    {
        state_free(state);
        return "state is damaged; every type repacks";
    }
    state->loaded = 1;
    return NULL;
}

static int
state_write(const struct State* state, const char* path)
{
    struct Buf buf;
    char temporary[1300];
    FILE* file;
    int ok;

    memset(&buf, 0, sizeof(buf));
    buf_u32(&buf, STATE_MAGIC);
    buf_u32(&buf, STATE_FORMAT);
    buf_u64(&buf, state->global);
    buf_u32(&buf, state->generation);
    buf_u64(&buf, state->store_hash);
    buf_u32(&buf, (uint32_t)state->input_count);
    for( int i = 0; i < state->input_count; i++ )
    {
        const struct Input* input = &state->inputs[i];

        buf_str(&buf, input->rel);
        buf_u64(&buf, (uint64_t)input->size);
        buf_u64(&buf, (uint64_t)input->mtime);
        buf_u64(&buf, input->hash);
        buf_u32(&buf, (uint32_t)input->present);
    }
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        const struct Unit* unit = &state->units[u];

        buf_u32(&buf, (uint32_t)unit->present);
        buf_u32(&buf, (uint32_t)unit->result);
        buf_u64(&buf, unit->key);
        buf_u32(&buf, (uint32_t)unit->mutates);
        buf_u32(&buf, unit->rev);
        buf_u32(&buf, (uint32_t)unit->server_records);
        buf_u32(&buf, (uint32_t)unit->membership_errors);
        buf_u32(&buf, (uint32_t)unit->unresolved);
        buf_str(&buf, unit->out_text);
        buf_str(&buf, unit->err_text);
        buf_u32(&buf, (uint32_t)unit->lookup_count);
        for( int i = 0; i < unit->lookup_count; i++ )
        {
            buf_u32(&buf, (uint32_t)unit->lookups[i].op);
            buf_u32(&buf, (uint32_t)unit->lookups[i].type);
            buf_u32(&buf, (uint32_t)unit->lookups[i].id);
            buf_str(&buf, unit->lookups[i].name);
            buf_u64(&buf, unit->lookups[i].digest);
        }
        buf_u32(&buf, (uint32_t)unit->archive_count);
        for( int i = 0; i < unit->archive_count; i++ )
        {
            buf_u32(&buf, (uint32_t)unit->archives[i].table);
            buf_u32(&buf, (uint32_t)unit->archives[i].archive);
            buf_u32(&buf, unit->archives[i].rev);
            buf_u32(&buf, unit->archives[i].length);
            buf_put(&buf, unit->archives[i].data, unit->archives[i].length);
        }
    }
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    ok = file && fwrite(buf.data, 1, buf.length, file) == buf.length;
    if( file && fclose(file) != 0 )
        ok = 0;
    ok = ok && replace_file(temporary, path) == 0;
    free(buf.data);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Recording                                                           */
/* ------------------------------------------------------------------ */

int g_cp_recording;

static struct Unit* g_unit;      /* the unit lookups and writes go to, or NULL */
static int g_unit_writes;        /* capture archive writes too */
static struct CP_Ctx* g_ctx;
/* Per-unit dedup of lookups: hashes of what this unit already recorded. */
static uint64_t* g_seen;
static uint32_t g_seen_capacity;
static uint32_t g_seen_used;

static uint64_t
lookup_key(int op, int type, int id, const char* name)
{
    return hash_str(hash_u64(hash_u64(hash_u64(0x51ed270b27f84ac5ull, (uint64_t)op), (uint64_t)type),
                             (uint64_t)(uint32_t)id),
                    name);
}

static uint64_t lookup_answer(int op, int type, int id, const char* name);

void
cp_lookup_note(
    int op,
    int type,
    int id,
    const char* name)
{
    uint64_t key;
    uint32_t slot;
    struct Lookup* lookup;

    if( !g_cp_recording || !g_unit )
        return;
    key = lookup_key(op, type, id, name) | 1; /* 0 marks an empty slot */
    if( (g_seen_used + 1) * 2 > g_seen_capacity )
    {
        uint32_t old_capacity = g_seen_capacity;
        uint64_t* old = g_seen;

        g_seen_capacity = old_capacity ? old_capacity * 2 : 4096;
        g_seen = (uint64_t*)calloc(g_seen_capacity, sizeof(uint64_t));
        assert(g_seen);
        for( uint32_t i = 0; i < old_capacity; i++ )
        {
            if( !old[i] )
                continue;
            slot = (uint32_t)old[i] & (g_seen_capacity - 1);
            while( g_seen[slot] )
                slot = (slot + 1) & (g_seen_capacity - 1);
            g_seen[slot] = old[i];
        }
        free(old);
    }
    slot = (uint32_t)key & (g_seen_capacity - 1);
    while( g_seen[slot] )
    {
        if( g_seen[slot] == key )
            return;
        slot = (slot + 1) & (g_seen_capacity - 1);
    }
    g_seen[slot] = key;
    g_seen_used++;

    if( g_unit->lookup_count == g_unit->lookup_capacity )
    {
        g_unit->lookup_capacity = g_unit->lookup_capacity ? g_unit->lookup_capacity * 2 : 256;
        g_unit->lookups = (struct Lookup*)realloc(
            g_unit->lookups, (size_t)g_unit->lookup_capacity * sizeof(struct Lookup));
        assert(g_unit->lookups);
    }
    lookup = &g_unit->lookups[g_unit->lookup_count++];
    lookup->op = op;
    lookup->type = type;
    lookup->id = id;
    lookup->name = strdup(name ? name : "");
    assert(lookup->name);
    lookup->digest = lookup_answer(op, type, id, lookup->name);
}

void
cp_lookup_note_mutation(void)
{
    if( g_cp_recording && g_unit )
        g_unit->mutates = 1;
}

/** Ask again, unrecorded, and hash the answer. */
static uint64_t
lookup_answer(int op, int type, int id, const char* name)
{
    int saved = g_cp_recording;
    uint64_t h = hash_u64(0x2545f4914f6cdd1dull, (uint64_t)op);

    assert(g_ctx);
    g_cp_recording = 0;
    switch( op )
    {
    case CP_LOOKUP_NAME_FIND:
        h = hash_u64(h, (uint64_t)(uint32_t)cp_name_find(g_ctx, (enum CP_TypeId)type, name));
        break;
    case CP_LOOKUP_NAME_ALLOC:
        h = hash_u64(h, (uint64_t)(uint32_t)cp_name_find_alloc(g_ctx, (enum CP_TypeId)type, name));
        break;
    case CP_LOOKUP_NAME_GET:
    {
        const char* got = cp_name_get(g_ctx, (enum CP_TypeId)type, id);

        h = got ? hash_str(h, got) : hash_u64(h, 0x6e756c6cull);
        break;
    }
    case CP_LOOKUP_NAME_LANE:
        h = hash_u64(h, (uint64_t)cp_name_is_lane(g_ctx, (enum CP_TypeId)type, name));
        break;
    case CP_LOOKUP_CONST_INT:
    {
        char caret[1100];
        int value = 0;
        int found;

        snprintf(caret, sizeof(caret), "^%s", name);
        found = cp_resolve_caret(g_ctx, caret, &value);
        h = hash_u64(hash_u64(h, (uint64_t)found), (uint64_t)(uint32_t)(found ? value : 0));
        break;
    }
    case CP_LOOKUP_CONST_TEXT:
    {
        const char* text = cp_value_constant_text(g_ctx, name);

        h = text ? hash_str(h, text) : hash_u64(h, 0x6e756c6cull);
        break;
    }
    default:
        assert(!"cp_incremental: unknown lookup");
    }
    g_cp_recording = saved;
    return h;
}

int
cp_server_write(
    const char* server_dir,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size)
{
    struct Archive* archive;

    if( !g_unit || !g_unit_writes )
        return RSCache_Dat2DiskWriteArchive(server_dir, table_id, archive_id, data, data_size);
    assert(data_size >= 0);
    if( g_unit->archive_count == g_unit->archive_capacity )
    {
        g_unit->archive_capacity = g_unit->archive_capacity ? g_unit->archive_capacity * 2 : 64;
        g_unit->archives = (struct Archive*)realloc(
            g_unit->archives, (size_t)g_unit->archive_capacity * sizeof(struct Archive));
        assert(g_unit->archives);
    }
    archive = &g_unit->archives[g_unit->archive_count++];
    memset(archive, 0, sizeof(*archive));
    archive->table = table_id;
    archive->archive = archive_id;
    archive->length = (uint32_t)data_size;
    archive->data = (uint8_t*)malloc(data_size ? (size_t)data_size : 1);
    assert(archive->data);
    memcpy(archive->data, data, (size_t)data_size);
    archive->hash = hash_bytes(archive->data, archive->length, 0x9e3779b97f4a7c15ull);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Capturing what a unit prints                                        */
/* ------------------------------------------------------------------ */

struct Capture
{
    int saved_out;
    int saved_err;
    FILE* out;
    FILE* err;
};

/*
 * Named files beside the pack rather than tmpfile(): a unit that dies mid-pack
 * (an assert, a crash) takes its process with it, and with an anonymous file
 * its last words went too. These survive it, and the next build prints them
 * (capture_report_orphans) before it does anything else.
 */
static void
capture_paths(const char* dir, char* out, char* err, size_t capacity)
{
    snprintf(out, capacity, "%s/.unit.stdout", dir);
    snprintf(err, capacity, "%s/.unit.stderr", dir);
}

static void
capture_report_orphans(const char* dir)
{
    char out[1300];
    char err[1300];
    const char* paths[2] = { out, err };

    capture_paths(dir, out, err, sizeof(out));
    for( int i = 0; i < 2; i++ )
    {
        size_t length;
        char* text = read_whole(paths[i], &length);

        if( !text )
            continue;
        fprintf(stderr,
                "cachepack: the previous build died while packing a type; what it printed "
                "(%s):\n%s\n",
                paths[i], text);
        free(text);
        remove(paths[i]);
    }
}

static int
capture_begin(struct Capture* capture, const char* dir)
{
    char out[1300];
    char err[1300];

    fflush(stdout);
    fflush(stderr);
    capture_paths(dir, out, err, sizeof(out));
    capture->out = fopen(out, "w+b");
    capture->err = fopen(err, "w+b");
    if( !capture->out || !capture->err )
        return 0;
    capture->saved_out = cp_dup(cp_fileno(stdout));
    capture->saved_err = cp_dup(cp_fileno(stderr));
    cp_dup2(cp_fileno(capture->out), cp_fileno(stdout));
    cp_dup2(cp_fileno(capture->err), cp_fileno(stderr));
    return 1;
}

static char*
capture_read(FILE* file)
{
    long size;
    char* text;

    fflush(file);
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    text = (char*)malloc((size_t)(size > 0 ? size : 0) + 1);
    assert(text);
    if( size > 0 && fread(text, 1, (size_t)size, file) != (size_t)size )
        size = 0;
    text[size > 0 ? size : 0] = '\0';
    fclose(file);
    return text;
}

/** Restore stdout/stderr and hand back what was written, after echoing it. */
static void
capture_end(struct Capture* capture, const char* dir, char** out_text, char** err_text)
{
    char out[1300];
    char err[1300];

    fflush(stdout);
    fflush(stderr);
    cp_dup2(capture->saved_out, cp_fileno(stdout));
    cp_dup2(capture->saved_err, cp_fileno(stderr));
    cp_close(capture->saved_out);
    cp_close(capture->saved_err);
    *out_text = capture_read(capture->out);
    *err_text = capture_read(capture->err);
    capture_paths(dir, out, err, sizeof(out));
    remove(out);
    remove(err);
    fputs(*out_text, stdout);
    fputs(*err_text, stderr);
}

/* ------------------------------------------------------------------ */
/* Inputs                                                              */
/* ------------------------------------------------------------------ */

struct Inputs
{
    struct Input* items;
    int count;
    int capacity;
    /* rel -> index */
    int32_t* slots;
    uint32_t slot_capacity;
};

static uint32_t
rel_hash(const char* rel)
{
    uint32_t h = 2166136261u;

    for( ; *rel; rel++ )
        h = (h ^ (uint8_t)*rel) * 16777619u;
    return h;
}

static int
inputs_find(const struct Inputs* inputs, const char* rel)
{
    uint32_t slot;

    if( !inputs->slot_capacity )
        return -1;
    slot = rel_hash(rel) & (inputs->slot_capacity - 1);
    while( inputs->slots[slot] >= 0 )
    {
        if( strcmp(inputs->items[inputs->slots[slot]].rel, rel) == 0 )
            return inputs->slots[slot];
        slot = (slot + 1) & (inputs->slot_capacity - 1);
    }
    return -1;
}

static void
inputs_reindex(struct Inputs* inputs)
{
    uint32_t capacity = 1024;

    while( capacity < (uint32_t)inputs->count * 2 + 16 )
        capacity *= 2;
    free(inputs->slots);
    inputs->slots = (int32_t*)malloc(capacity * sizeof(int32_t));
    assert(inputs->slots);
    memset(inputs->slots, -1, capacity * sizeof(int32_t));
    inputs->slot_capacity = capacity;
    for( int i = 0; i < inputs->count; i++ )
    {
        uint32_t slot = rel_hash(inputs->items[i].rel) & (capacity - 1);

        while( inputs->slots[slot] >= 0 )
            slot = (slot + 1) & (capacity - 1);
        inputs->slots[slot] = i;
    }
}

/**
 * The input `rel` (relative to srcdir), stat'd now and hashed when its size or
 * mtime differs from what the previous build recorded. Added once; later calls
 * return the same record.
 */
static struct Input*
input_get(
    struct Inputs* inputs,
    const struct State* previous,
    const struct Inputs* previous_index,
    const char* srcdir,
    const char* rel)
{
    int found = inputs_find(inputs, rel);
    struct Input* input;
    char path[2400];
    struct stat info;

    if( found >= 0 )
        return &inputs->items[found];
    if( inputs->count == inputs->capacity )
    {
        inputs->capacity = inputs->capacity ? inputs->capacity * 2 : 4096;
        inputs->items =
            (struct Input*)realloc(inputs->items, (size_t)inputs->capacity * sizeof(struct Input));
        assert(inputs->items);
    }
    input = &inputs->items[inputs->count++];
    memset(input, 0, sizeof(*input));
    input->rel = strdup(rel);
    assert(input->rel);
    if( (uint32_t)inputs->count * 2 > inputs->slot_capacity )
        inputs_reindex(inputs);
    else
    {
        uint32_t slot = rel_hash(rel) & (inputs->slot_capacity - 1);

        while( inputs->slots[slot] >= 0 )
            slot = (slot + 1) & (inputs->slot_capacity - 1);
        inputs->slots[slot] = inputs->count - 1;
    }
    snprintf(path, sizeof(path), "%s/%s", srcdir, rel);
    if( stat(path, &info) != 0 )
    {
        input->present = 0;
        input->hashed = 1;
        return input;
    }
    input->present = 1;
    input->size = (int64_t)info.st_size;
    input->mtime = mtime_ns(&info);
    found = previous_index ? inputs_find(previous_index, rel) : -1;
    if( found >= 0 )
    {
        const struct Input* old = &previous->inputs[found];

        if( old->present && old->size == input->size && old->mtime == input->mtime )
        {
            input->hash = old->hash;
            input->hashed = 1;
            return input;
        }
    }
    {
        int ok;

        input->hash = hash_file(path, &ok);
        input->hashed = 1;
    }
    return input;
}

static uint64_t
input_digest(uint64_t h, const struct Input* input)
{
    h = hash_str(h, input->rel);
    return hash_u64(h, input->present ? input->hash : 0x6d697373696e67ull);
}

/* A path under srcdir, as srcdir-relative, or NULL when it is not under it. */
static const char*
relative_to(const char* srcdir, const char* path)
{
    size_t length = strlen(srcdir);

    if( strncmp(path, srcdir, length) == 0 && path[length] == '/' )
        return path + length + 1;
    return NULL;
}

/* Every file the pack is built from, as the fingerprint has always defined
 * them (RSCache_ServerPackFingerprint), plus meta.ini and content.ini. */
static int
collect_input_list(
    const char* srcdir,
    const char* const* lanes,
    int lane_count,
    char*** out_rels)
{
    int count = RSCache_ServerPackInputs(srcdir, lanes, lane_count, out_rels);
    char** rels = *out_rels;

    rels = (char**)realloc(rels, (size_t)(count + 2) * sizeof(char*));
    assert(rels);
    rels[count++] = strdup("meta.ini");
    rels[count++] = strdup("content.ini");
    *out_rels = rels;
    return count;
}

/* ------------------------------------------------------------------ */
/* The up-to-date check, from stats alone                              */
/* ------------------------------------------------------------------ */

static uint64_t
lanes_digest(const char* const* lanes, int lane_count)
{
    uint64_t h = hash_u64(0x7f4a7c159e3779b9ull, (uint64_t)lane_count);

    for( int l = 0; l < lane_count; l++ )
        h = hash_str(h, lanes[l]);
    return h;
}

int
cp_server_up_to_date(
    const char* srcdir,
    const char* server_dir,
    const char* const* lanes,
    int lane_count,
    const char* argv0)
{
    char path[1300];
    struct State state;
    char** rels = NULL;
    int count;
    int fresh = 1;
    double started = now_seconds();
    struct Inputs index;
    uint64_t binary = executable_hash(argv0);

    snprintf(path, sizeof(path), "%s/cp.state", server_dir);
    if( state_read(&state, path) )
        return 0;
    /* The binary and the lanes are inside the global key, which needs the
     * whole tree to recompute; the stat check below cannot see them, so the
     * state records them in the first two inputs' place: see stat_identity. */
    memset(&index, 0, sizeof(index));
    index.items = state.inputs;
    index.count = state.input_count;
    inputs_reindex(&index);
    {
        int at = inputs_find(&index, "#identity");

        if( at < 0 || state.inputs[at].hash != hash_u64(binary, lanes_digest(lanes, lane_count)) )
            fresh = 0;
    }
    count = fresh ? collect_input_list(srcdir, lanes, lane_count, &rels) : 0;
    for( int i = 0; i < count && fresh; i++ )
    {
        struct stat info;
        int at = inputs_find(&index, rels[i]);

        snprintf(path, sizeof(path), "%s/%s", srcdir, rels[i]);
        if( at < 0 )
        {
            fresh = 0; /* a file the last build never saw */
            break;
        }
        if( stat(path, &info) != 0 )
            fresh = !state.inputs[at].present;
        else
            fresh = state.inputs[at].present && state.inputs[at].size == (int64_t)info.st_size &&
                    state.inputs[at].mtime == mtime_ns(&info);
    }
    /* A recorded input that is no longer listed (a deleted file) is a change. */
    if( fresh )
    {
        int listed = 0;

        for( int i = 0; i < state.input_count; i++ )
            listed += state.inputs[i].rel[0] != '#' && state.inputs[i].present;
        if( listed != count )
            fresh = 0;
    }
    if( fresh )
    {
        struct stat info;
        size_t length;
        char* data;

        snprintf(path, sizeof(path), "%s/main_file_cache.dat2", server_dir);
        data = stat(path, &info) == 0 ? read_whole(path, &length) : NULL;
        fresh = data && hash_bytes(data, length, 0x9e3779b97f4a7c15ull) == state.store_hash;
        free(data);
        snprintf(path, sizeof(path), "%s/pack.manifest", server_dir);
        fresh = fresh && stat(path, &info) == 0;
    }
    for( int i = 0; i < count; i++ )
        free(rels[i]);
    free(rels);
    free(index.slots);
    if( fresh )
        printf("Server pack: up to date — 0 types repacked, %d input file(s) unchanged "
               "[check %.2fs]\n",
               count, now_seconds() - started);
    state_free(&state);
    return fresh;
}

/* ------------------------------------------------------------------ */
/* The build                                                           */
/* ------------------------------------------------------------------ */

static int
unit_reusable(
    const struct Unit* unit,
    uint64_t key,
    const char** why,
    char* why_buf,
    size_t why_size)
{
    if( !unit->present )
    {
        *why = "new";
        return 0;
    }
    if( unit->mutates )
    {
        *why = "mints names while packing";
        return 0;
    }
    if( unit->key != key )
    {
        *why = "its files changed";
        return 0;
    }
    for( int i = 0; i < unit->lookup_count; i++ )
    {
        const struct Lookup* lookup = &unit->lookups[i];

        if( lookup_answer(lookup->op, lookup->type, lookup->id, lookup->name) != lookup->digest )
        {
            static const char* const ops[] = { "?",           "name",        "allocated name",
                                               "name of id",  "lane name",   "constant",
                                               "constant" };

            if( lookup->op == CP_LOOKUP_NAME_GET )
                snprintf(why_buf, why_size, "%s %s %d changed", ops[lookup->op],
                         cp_type(lookup->type)->name, lookup->id);
            else if( lookup->op >= CP_LOOKUP_CONST_INT )
                snprintf(why_buf, why_size, "%s ^%s changed", ops[lookup->op], lookup->name);
            else
                snprintf(why_buf, why_size, "%s %s:%s changed", ops[lookup->op],
                         cp_type(lookup->type)->name, lookup->name);
            *why = why_buf;
            return 0;
        }
    }
    return 1;
}

static int
write_text_atomic(const char* path, const void* data, size_t length)
{
    char temporary[1300];
    FILE* file;
    int ok;

    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if( !file )
        return 0;
    ok = fwrite(data, 1, length, file) == length;
    if( fclose(file) != 0 )
        ok = 0;
    return ok && replace_file(temporary, path) == 0;
}

/** Write every unit's archives, in unit order, into a fresh store in `dir`. */
static int
write_store(const struct Unit* units, const char* dir)
{
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        for( int i = 0; i < units[u].archive_count; i++ )
        {
            const struct Archive* archive = &units[u].archives[i];

            if( RSCache_Dat2DiskWriteArchive(dir, archive->table, archive->archive, archive->data,
                                             (int)archive->length) != 0 )
                return 0;
        }
    }
    RSCache_Dat2DiskWriteFlush();
    return 1;
}

static int
is_store_file(const char* name)
{
    return strcmp(name, "main_file_cache.dat2") == 0 ||
           strncmp(name, "main_file_cache.idx", 19) == 0;
}

/* Move the staged store over the live one: each file replaced in one step, and
 * any store file the new pack no longer has removed. The stamp goes first, so a
 * reader without the lock sees "no stamp" rather than a mixed pack. */
static int
swap_store(const char* staging, const char* server_dir)
{
    DIR* handle;
    struct dirent* entry;
    char from[1400];
    char to[1400];
    int ok = 1;

    snprintf(to, sizeof(to), "%s/stamp.txt", server_dir);
    remove(to);
    handle = opendir(server_dir);
    while( handle && (entry = readdir(handle)) != NULL )
    {
        struct stat info;

        if( !is_store_file(entry->d_name) )
            continue;
        snprintf(from, sizeof(from), "%s/%s", staging, entry->d_name);
        if( stat(from, &info) != 0 )
        {
            snprintf(to, sizeof(to), "%s/%s", server_dir, entry->d_name);
            remove(to);
        }
    }
    if( handle )
        closedir(handle);
    handle = opendir(staging);
    while( handle && (entry = readdir(handle)) != NULL )
    {
        if( !is_store_file(entry->d_name) )
            continue;
        snprintf(from, sizeof(from), "%s/%s", staging, entry->d_name);
        snprintf(to, sizeof(to), "%s/%s", server_dir, entry->d_name);
        if( replace_file(from, to) != 0 )
            ok = 0;
    }
    if( handle )
        closedir(handle);
    rmdir(staging);
    return ok;
}

int
cp_pack_server_incremental(
    struct CP_Ctx* ctx,
    const char* server_dir)
{
    struct State previous;
    struct State next;
    struct Inputs inputs;
    struct Inputs previous_index;
    struct Lock lock;
    char path[1400];
    char staging[1400];
    const char* note;
    char** rels = NULL;
    int rel_count;
    uint64_t global;
    int global_changed;
    int server_total = 0;
    int membership_errors = 0;
    int repacked = 0;
    int reused = 0;
    int archives_changed = 0;
    int archives_total = 0;
    double started = now_seconds();
    double t_inputs;
    double t_units = 0;
    double t_write = 0;
    double mark;
    int status = 1;
    struct Buf log;
    struct Buf manifest;
    uint64_t store_hash = 0;
    int64_t store_size = 0;
    uint32_t revision_capacity = 0;
    uint64_t* revision_keys = NULL;
    uint32_t* revision_revs = NULL;
    uint64_t* revision_hashes = NULL;

    assert(ctx);
    assert(server_dir);
    memset(&inputs, 0, sizeof(inputs));
    memset(&previous_index, 0, sizeof(previous_index));
    memset(&next, 0, sizeof(next));
    memset(&log, 0, sizeof(log));
    memset(&manifest, 0, sizeof(manifest));
    g_ctx = ctx;

    cp_mkdir(server_dir);
    if( !lock_take(&lock, server_dir) )
    {
        fprintf(stderr, "cachepack: cannot lock %s/.pack.lock\n", server_dir);
        return 0;
    }
    capture_report_orphans(server_dir);
    snprintf(path, sizeof(path), "%s/cp.state", server_dir);
    note = state_read(&previous, path);
    if( ctx->force_server && !note )
        note = "--force: every type repacks";
    if( previous.loaded )
    {
        previous_index.items = previous.inputs;
        previous_index.count = previous.input_count;
        inputs_reindex(&previous_index);
    }

    /* --- the inputs, and the global key --- */
    mark = now_seconds();
    rel_count = collect_input_list(ctx->srcdir, ctx->lanes, ctx->lane_count, &rels);
    global = hash_u64(0x8bb84b93962eacc9ull, STATE_FORMAT);
    global = hash_u64(global, executable_hash(NULL));
    global = hash_u64(global, lanes_digest(ctx->lanes, ctx->lane_count));
    global = hash_u64(global, (uint64_t)ctx->profile.game);
    global = hash_u64(global, (uint64_t)ctx->profile.revision);
    global = hash_str(global, ctx->srcdir);
    global = hash_u64(global, hash_bytes(ctx->param_types,
                                         (size_t)ctx->param_types_count * sizeof(*ctx->param_types),
                                         0x9e3779b97f4a7c15ull));
    for( int i = 0; i < rel_count; i++ )
    {
        struct Input* input = input_get(&inputs, &previous, previous.loaded ? &previous_index : NULL,
                                        ctx->srcdir, rels[i]);
        const char* rel = rels[i];
        size_t length = strlen(rel);

        /* What no encoder's lookups can be traced through: the registers, the
         * meta, the asset/stat/category indexes and the interface members. The
         * name tables (the all.<type>.compack and <type>.alloc files) and the constants
         * are not here — their lookups are recorded per type instead. */
        if( strcmp(rel, "meta.ini") == 0 || strcmp(rel, "content.ini") == 0 ||
            strncmp(rel, "fields/", 7) == 0 || strncmp(rel, "interfaces/", 11) == 0 ||
            strcmp(rel, "pack/stat.pack") == 0 || strcmp(rel, "pack/category.pack") == 0 ||
            (strncmp(rel, "pack/", 5) == 0 && rel[5] >= '0' && rel[5] <= '9' && length > 5 &&
             strcmp(rel + length - 5, ".pack") == 0) )
            global = input_digest(global, input);
    }
    global_changed = !previous.loaded || previous.global != global;
    if( !note && global_changed )
        note = "a global input changed (fields, content.ini, meta.ini, an asset or interface "
               "index, the param types, the binary or the lanes): every type repacks";
    t_inputs = now_seconds() - mark;
    if( note )
        printf("cachepack: %s\n", note);

    /* Every archive the previous build wrote, by (table, archive), for the
     * revisions below: a reused unit hands its records over. */
    revision_capacity = 1024;
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        while( revision_capacity < (uint32_t)(previous.units[u].archive_count) * 4 + 1024 )
            revision_capacity *= 2;
    }
    {
        uint32_t total = 0;

        for( int u = 0; u < UNIT_COUNT; u++ )
            total += (uint32_t)previous.units[u].archive_count;
        while( revision_capacity < total * 2 + 1024 )
            revision_capacity *= 2;
    }
    revision_keys = (uint64_t*)calloc(revision_capacity, sizeof(uint64_t));
    revision_revs = (uint32_t*)calloc(revision_capacity, sizeof(uint32_t));
    revision_hashes = (uint64_t*)calloc(revision_capacity, sizeof(uint64_t));
    assert(revision_keys);
    assert(revision_revs);
    assert(revision_hashes);
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        for( int i = 0; i < previous.units[u].archive_count; i++ )
        {
            const struct Archive* archive = &previous.units[u].archives[i];
            uint64_t where = ((uint64_t)(uint32_t)archive->table << 32) | (uint32_t)archive->archive;
            uint32_t slot = (uint32_t)mix(where) & (revision_capacity - 1);

            while( revision_keys[slot] )
                slot = (slot + 1) & (revision_capacity - 1);
            revision_keys[slot] = where | 0x8000000000000000ull;
            revision_revs[slot] = archive->rev;
            revision_hashes[slot] = archive->hash;
        }
    }

    printf("Packing the server bands from %s\n", ctx->srcdir);
    mark = now_seconds();
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        struct Unit* old = &previous.units[u];
        struct Unit* unit = &next.units[u];
        uint64_t key = hash_u64(0x94d049bb133111ebull, (uint64_t)u);
        const char* why = NULL;
        char why_buf[512];
        struct Capture capture;
        int before_unresolved;
        const char* label = u == NAMES_UNIT ? "names" : cp_type(u)->name;

        /* The type's own files: every *.<type> the walk found, its routing
         * files, and for dbrow the *.dbtable files its schemas come from. */
        if( u < CP_TYPE_COUNT )
        {
            const char* found[CP_PACK_MAX_SOURCES];
            int found_count = cp_walk_find(&ctx->walk, cp_type(u)->name, found, CP_PACK_MAX_SOURCES);
            char rel[600];

            for( int f = 0; f < found_count; f++ )
            {
                const char* r = relative_to(ctx->srcdir, found[f]);

                assert(r);
                key = input_digest(key, input_get(&inputs, &previous,
                                                  previous.loaded ? &previous_index : NULL,
                                                  ctx->srcdir, r));
            }
            snprintf(rel, sizeof(rel), "pack/%s.client", cp_type(u)->name);
            key = input_digest(key, input_get(&inputs, &previous,
                                              previous.loaded ? &previous_index : NULL, ctx->srcdir,
                                              rel));
            snprintf(rel, sizeof(rel), "pack/%s.server", cp_type(u)->name);
            key = input_digest(key, input_get(&inputs, &previous,
                                              previous.loaded ? &previous_index : NULL, ctx->srcdir,
                                              rel));
            if( u == CP_TYPE_DBROW )
            {
                found_count = cp_walk_find(&ctx->walk, "dbtable", found, CP_PACK_MAX_SOURCES);
                for( int f = 0; f < found_count; f++ )
                {
                    const char* r = relative_to(ctx->srcdir, found[f]);

                    assert(r);
                    key = input_digest(key, input_get(&inputs, &previous,
                                                      previous.loaded ? &previous_index : NULL,
                                                      ctx->srcdir, r));
                }
            }
        }

        if( !global_changed && !ctx->force_server &&
            unit_reusable(old, key, &why, why_buf, sizeof(why_buf)) )
        {
            /* Byte for byte what it packed last time, and what it said. */
            *unit = *old;
            memset(old, 0, sizeof(*old));
            fputs(unit->out_text ? unit->out_text : "", stdout);
            fputs(unit->err_text ? unit->err_text : "", stderr);
            ctx->warn_unresolved_name += unit->unresolved;
            server_total += unit->server_records;
            membership_errors += unit->membership_errors;
            if( unit->result > 0 )
            {
                reused++;
                buf_printf(&log, "reused    %-10s %d archive(s)\n", label, unit->archive_count);
            }
            continue;
        }
        if( global_changed || ctx->force_server )
            why = note;

        unit->present = 1;
        unit->key = key;
        g_unit = unit;
        g_unit_writes = 1;
        g_seen_used = 0;
        if( g_seen )
            memset(g_seen, 0, g_seen_capacity * sizeof(uint64_t));
        before_unresolved = ctx->warn_unresolved_name;
        if( !capture_begin(&capture, server_dir) )
        {
            fprintf(stderr, "cachepack: cannot capture output for %s\n", label);
            status = 0;
            break;
        }
        g_cp_recording = 1;
        if( u == NAMES_UNIT )
            unit->result = cp_server_pack_names(ctx, server_dir) ? 1 : -1;
        else
            unit->result = cp_server_pack_type(ctx, (enum CP_TypeId)u, server_dir,
                                               &unit->server_records, &unit->membership_errors);
        g_cp_recording = 0;
        g_unit = NULL;
        g_unit_writes = 0;
        capture_end(&capture, server_dir, &unit->out_text, &unit->err_text);
        unit->unresolved = ctx->warn_unresolved_name - before_unresolved;
        if( unit->result < 0 )
        {
            status = 0;
            break;
        }
        server_total += unit->server_records;
        membership_errors += unit->membership_errors;
        if( unit->result > 0 )
        {
            repacked++;
            buf_printf(&log, "repacked  %-10s %d archive(s) — %s%s\n", label, unit->archive_count,
                       why ? why : "",
                       unit->mutates ? " (minted names: never reused)" : "");
            if( previous.loaded && !global_changed && !ctx->force_server )
                printf("cachepack: repacked %s — %s\n", label, why ? why : "");
        }
    }
    t_units = now_seconds() - mark;
    if( !status )
        goto done;

    printf("Server pack: %d record(s) in %s, %d unresolved names\n", server_total, server_dir,
           ctx->warn_unresolved_name);
    if( membership_errors )
        printf("Membership: %d record(s) state a server field pack/<ns>.server does not "
               "claim (docs/PACK_ENTITY_SPLIT_PLAN.md §2 cell (a))\n",
               membership_errors);
    /* As the full build: an unresolved name or a membership error is a failed
     * pack. The live one is left as it was rather than replaced by half a pack. */
    if( ctx->warn_unresolved_name != 0 || membership_errors != 0 )
    {
        status = 0;
        goto done;
    }

    /* --- revisions: an archive keeps its number while its bytes do --- */
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        struct Unit* unit = &next.units[u];
        int changed = 0;

        for( int i = 0; i < unit->archive_count; i++ )
        {
            struct Archive* archive = &unit->archives[i];
            uint64_t where = ((uint64_t)(uint32_t)archive->table << 32) | (uint32_t)archive->archive;
            uint32_t slot = (uint32_t)mix(where) & (revision_capacity - 1);
            uint32_t rev = 0;
            uint64_t old_hash = 0;

            while( revision_keys[slot] )
            {
                if( revision_keys[slot] == (where | 0x8000000000000000ull) )
                {
                    rev = revision_revs[slot];
                    old_hash = revision_hashes[slot];
                    break;
                }
                slot = (slot + 1) & (revision_capacity - 1);
            }
            if( !rev || old_hash != archive->hash )
            {
                archive->rev = rev + 1;
                changed = 1;
                archives_changed++;
            }
            else
            {
                archive->rev = rev;
            }
            archives_total++;
        }
        if( changed || !unit->rev )
            unit->rev++;
    }

    /* --- write the store, the stamp, the manifest, the state --- */
    mark = now_seconds();
    {
        size_t length;
        char* data;
        int rewrite = repacked > 0 || archives_changed > 0 || !previous.loaded;

        /* Nothing repacked and the live store is the one the last build wrote:
         * leave it, and its stamp, alone. */
        snprintf(path, sizeof(path), "%s/main_file_cache.dat2", server_dir);
        data = rewrite ? NULL : read_whole(path, &length);
        if( data && hash_bytes(data, length, 0x9e3779b97f4a7c15ull) == previous.store_hash )
        {
            snprintf(path, sizeof(path), "%s/stamp.txt", server_dir);
            rewrite = !RSCache_ServerPackStampRead(server_dir, &(struct RSCache_ServerPackStamp){ 0 });
        }
        else
        {
            rewrite = 1;
        }
        free(data);
        if( rewrite )
        {
            snprintf(staging, sizeof(staging), "%s/.staging.%d", server_dir, (int)cp_getpid());
            cp_mkdir(staging);
            if( !write_store(next.units, staging) || !swap_store(staging, server_dir) )
            {
                fprintf(stderr, "cachepack: cannot write the server pack into %s\n", server_dir);
                status = 0;
                goto done;
            }
            if( !cp_server_stamp_write(ctx, server_dir) )
            {
                status = 0;
                goto done;
            }
        }
        else if( !cp_server_stamp_write(ctx, server_dir) )
        {
            /* The store stands, but the stamp is the whole-tree fingerprint a
             * server binary from before the manifest checks, and an input moved
             * (or this would have been the up-to-date path), so it is rewritten. */
            status = 0;
            goto done;
        }
        snprintf(path, sizeof(path), "%s/main_file_cache.dat2", server_dir);
        data = read_whole(path, &length);
        store_hash = data ? hash_bytes(data, length, 0x9e3779b97f4a7c15ull) : 0;
        store_size = data ? (int64_t)length : 0;
        free(data);
    }
    next.global = global;
    next.store_hash = store_hash;
    next.generation = previous.generation + (store_hash != previous.store_hash ? 1 : 0);

    buf_printf(&manifest, "torirs-server-pack 1\n");
    buf_printf(&manifest, "# Written by cachepack. docs/serverpack.md describes every line.\n");
    buf_printf(&manifest, "config %016llx\n", (unsigned long long)global);
    buf_printf(&manifest, "generation %u\n", next.generation);
    buf_printf(&manifest, "lanes");
    for( int l = 0; l < ctx->lane_count; l++ )
        buf_printf(&manifest, " %s", ctx->lanes[l]);
    buf_printf(&manifest, "%s\n", ctx->lane_count ? "" : " -");
    buf_printf(&manifest, "output main_file_cache.dat2 %lld %016llx\n", (long long)store_size,
               (unsigned long long)store_hash);
    for( int i = 0; i < inputs.count; i++ )
    {
        const struct Input* input = &inputs.items[i];

        if( input->present )
            buf_printf(&manifest, "input %lld %lld %016llx %s\n", (long long)input->size,
                       (long long)input->mtime, (unsigned long long)input->hash, input->rel);
    }
    for( int u = 0; u < UNIT_COUNT; u++ )
    {
        const struct Unit* unit = &next.units[u];

        if( unit->result <= 0 )
            continue;
        buf_printf(&manifest, "unit %s %016llx %u %d\n",
                   u == NAMES_UNIT ? "names" : cp_type(u)->name, (unsigned long long)unit->key,
                   unit->rev, unit->archive_count);
        for( int i = 0; i < unit->archive_count; i++ )
            buf_printf(&manifest, "archive %d %d %016llx %u %s\n", unit->archives[i].table,
                       unit->archives[i].archive, (unsigned long long)unit->archives[i].hash,
                       unit->archives[i].rev, u == NAMES_UNIT ? "names" : cp_type(u)->name);
    }
    snprintf(path, sizeof(path), "%s/pack.manifest", server_dir);
    if( !write_text_atomic(path, manifest.data, manifest.length) )
        status = 0;

    /* The state keeps every input with its hash, and one pseudo-input carrying
     * the binary and the lanes for cp_server_up_to_date. */
    next.input_count = inputs.count + 1;
    next.inputs = (struct Input*)calloc((size_t)next.input_count, sizeof(struct Input));
    assert(next.inputs);
    for( int i = 0; i < inputs.count; i++ )
    {
        next.inputs[i] = inputs.items[i];
        inputs.items[i].rel = NULL;
    }
    next.inputs[inputs.count].rel = strdup("#identity");
    next.inputs[inputs.count].hash =
        hash_u64(executable_hash(NULL), lanes_digest(ctx->lanes, ctx->lane_count));
    snprintf(path, sizeof(path), "%s/cp.state", server_dir);
    if( status && !state_write(&next, path) )
        status = 0;
    t_write = now_seconds() - mark;

    if( status )
    {
        printf("Server pack: %d type(s) repacked, %d reused; %d of %d archive(s) changed "
               "[inputs %.2fs, types %.2fs, write %.2fs; total %.2fs]\n",
               repacked, reused, archives_changed, archives_total, t_inputs, t_units, t_write,
               now_seconds() - started);
        buf_printf(&log, "%d repacked, %d reused, %d of %d archives changed\n", repacked, reused,
                   archives_changed, archives_total);
        snprintf(path, sizeof(path), "%s/pack.log", server_dir);
        write_text_atomic(path, log.data, log.length);
    }

done:
    g_unit = NULL;
    g_cp_recording = 0;
    for( int i = 0; i < rel_count; i++ )
        free(rels[i]);
    free(rels);
    for( int i = 0; i < inputs.count; i++ )
        free(inputs.items[i].rel);
    free(inputs.items);
    free(inputs.slots);
    free(previous_index.slots);
    free(revision_keys);
    free(revision_revs);
    free(revision_hashes);
    free(log.data);
    free(manifest.data);
    state_free(&previous);
    state_free(&next);
    lock_release(&lock);
    return status;
}
