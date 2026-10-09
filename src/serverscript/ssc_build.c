/*
 * The incremental script pack build. See ssc_build.h for the contract and
 * docs/serverpack.md for the design and the measurements behind it.
 */

#include "ssc_build.h"
#include "ssc_hash.h"
#include "ssvm_provider.h"
#include "ssvm_script.h"

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
#include <utime.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define getcwd _getcwd
#else
#include <sys/file.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* ------------------------------------------------------------------ */
/* Platform                                                            */
/* ------------------------------------------------------------------ */

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
stat_mtime_ns(const struct stat* info)
{
#if defined(__APPLE__)
    return (int64_t)info->st_mtimespec.tv_sec * 1000000000 + info->st_mtimespec.tv_nsec;
#elif defined(_WIN32)
    return (int64_t)info->st_mtime * 1000000000;
#else
    return (int64_t)info->st_mtim.tv_sec * 1000000000 + info->st_mtim.tv_nsec;
#endif
}

/** Replace `to` with `from` in one step, so a reader never sees half a file. */
static int
replace_file(const char* from, const char* to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(from, to);
#endif
}

/*
 * The output directory's lock. Held for the whole build, so two sessions
 * building one pack take turns instead of interleaving their writes, and a
 * server that takes it shared while it reads (torirs_server_scripts.c) never
 * loads a script.dat from one build with a script.idx from another.
 */
struct BuildLock
{
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
};

static int
lock_take(struct BuildLock* lock, const char* dir)
{
    char path[1100];

    snprintf(path, sizeof(path), "%s/.pack.lock", dir);
#ifdef _WIN32
    OVERLAPPED overlapped;

    lock->handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if( lock->handle == INVALID_HANDLE_VALUE )
        return 0;
    memset(&overlapped, 0, sizeof(overlapped));
    return LockFileEx(lock->handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped) ? 1 : 0;
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
lock_release(struct BuildLock* lock)
{
#ifdef _WIN32
    if( lock->handle != INVALID_HANDLE_VALUE )
        CloseHandle(lock->handle);
#else
    if( lock->fd >= 0 )
        close(lock->fd);
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

/** The content hash of a file, or 0 with `*ok` cleared when it cannot be read. */
static uint64_t
hash_file(const char* path, int* ok)
{
    size_t length;
    char* data = read_whole(path, &length);
    uint64_t hash;

    *ok = data != NULL;
    if( !data )
        return 0;
    hash = ssc_hash_bytes(data, length, SSC_HASH_SEED);
    free(data);
    return hash;
}

uint64_t
SSC_BuildExecutableHash(const char* argv0)
{
    char path[2048];
    int ok = 0;
    uint64_t hash;

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
    hash = hash_file(path, &ok);
    return ok ? hash : 0;
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

/*
 * Absolute and lexically normal ("a/./b/../c" is "a/c"), so a manifest path
 * reads the same whichever spelling of the content root the build was given.
 * Lexical on purpose: realpath() on every input would cost more than the rest
 * of a no-op build, and the content tree is not reached through symlinks.
 */
static void
normal_path(const char* path, char* out, size_t capacity)
{
    char joined[4096];
    char* parts[512];
    int count = 0;
    size_t used = 0;
    char* cursor;

    if( path[0] == '/' || (path[0] && path[1] == ':') )
    {
        snprintf(joined, sizeof(joined), "%s", path);
    }
    else
    {
        char cwd[2048];

        if( !getcwd(cwd, sizeof(cwd)) )
            cwd[0] = '\0';
        snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    }
    for( char* c = joined; *c; c++ )
    {
        if( *c == '\\' )
            *c = '/';
    }
    cursor = joined;
    while( *cursor )
    {
        char* part;

        while( *cursor == '/' )
            cursor++;
        if( !*cursor )
            break;
        part = cursor;
        while( *cursor && *cursor != '/' )
            cursor++;
        if( *cursor )
            *cursor++ = '\0';
        if( strcmp(part, ".") == 0 )
            continue;
        if( strcmp(part, "..") == 0 )
        {
            if( count > 0 )
                count--;
            continue;
        }
        assert(count < (int)(sizeof(parts) / sizeof(parts[0])));
        parts[count++] = part;
    }
    out[0] = '\0';
    if( joined[0] != '/' && count == 0 )
        return;
    for( int i = 0; i < count && used < capacity; i++ )
    {
        int written = snprintf(out + used, capacity - used, "%s%s",
                               (i == 0 && parts[0][1] == ':') ? "" : "/", parts[i]);

        if( written < 0 )
            break;
        used += (size_t)written;
    }
}

/** `path` relative to the content root when it is under it, else absolute. */
static char*
manifest_path(const char* content_normal, const char* path)
{
    char normal[4096];
    size_t root_length = strlen(content_normal);

    normal_path(path, normal, sizeof(normal));
    if( root_length && strncmp(normal, content_normal, root_length) == 0 &&
        normal[root_length] == '/' )
        return strdup(normal + root_length + 1);
    return strdup(normal);
}

/* ------------------------------------------------------------------ */
/* Growable buffer, and the state file's encoding                      */
/* ------------------------------------------------------------------ */

struct Buf
{
    uint8_t* data;
    size_t length;
    size_t capacity;
};

static void
buf_reserve(struct Buf* buf, size_t extra)
{
    if( buf->length + extra <= buf->capacity )
        return;
    while( buf->length + extra > buf->capacity )
        buf->capacity = buf->capacity ? buf->capacity * 2 : 65536;
    buf->data = (uint8_t*)realloc(buf->data, buf->capacity);
    assert(buf->data);
}

static void
buf_put(struct Buf* buf, const void* data, size_t length)
{
    buf_reserve(buf, length);
    if( length )
        memcpy(buf->data + buf->length, data, length);
    buf->length += length;
}

static void
buf_u8(struct Buf* buf, uint8_t value)
{
    buf_put(buf, &value, 1);
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
    int needed;

    va_start(args, fmt);
    needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    assert(needed >= 0);
    buf_reserve(buf, (size_t)needed + 1);
    va_start(args, fmt);
    vsnprintf((char*)buf->data + buf->length, (size_t)needed + 1, fmt, args);
    va_end(args);
    buf->length += (size_t)needed;
}

/** A reader that fails closed: any read past the end sets `bad` and yields 0. */
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

static uint8_t
rd_u8(struct Reader* reader)
{
    const uint8_t* p = (const uint8_t*)rd_take(reader, 1);

    return p ? *p : 0;
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
    char* text;

    if( !p )
        return strdup("");
    text = (char*)malloc((size_t)length + 1);
    assert(text);
    memcpy(text, p, length);
    text[length] = '\0';
    return text;
}

/* ------------------------------------------------------------------ */
/* Open-addressed index from a 64-bit key hash to an int               */
/* ------------------------------------------------------------------ */

typedef int (*MapEqual)(void* context, int32_t value, const void* key);

struct Map
{
    uint64_t* hashes;
    int32_t* values; /* -1 empty */
    uint32_t capacity;
    uint32_t used;
};

static void
map_init(struct Map* map)
{
    memset(map, 0, sizeof(*map));
}

static void
map_free(struct Map* map)
{
    free(map->hashes);
    free(map->values);
    memset(map, 0, sizeof(*map));
}

static uint32_t
map_slot(
    const struct Map* map,
    uint64_t hash,
    const void* key,
    MapEqual equal,
    void* context)
{
    uint32_t mask = map->capacity - 1;
    uint32_t slot = (uint32_t)hash & mask;

    for( ;; )
    {
        if( map->values[slot] < 0 ||
            (map->hashes[slot] == hash && equal(context, map->values[slot], key)) )
            return slot;
        slot = (slot + 1) & mask;
    }
}

static int32_t
map_find(
    const struct Map* map,
    uint64_t hash,
    const void* key,
    MapEqual equal,
    void* context)
{
    if( !map->capacity )
        return -1;
    return map->values[map_slot(map, hash, key, equal, context)];
}

static void map_grow(struct Map* map);

/** Insert `value` under `key`; an existing entry keeps its value. */
static int32_t
map_insert(
    struct Map* map,
    uint64_t hash,
    const void* key,
    int32_t value,
    MapEqual equal,
    void* context)
{
    uint32_t slot;

    if( (map->used + 1) * 2 > map->capacity )
        map_grow(map);
    slot = map_slot(map, hash, key, equal, context);
    if( map->values[slot] >= 0 )
        return map->values[slot];
    map->hashes[slot] = hash;
    map->values[slot] = value;
    map->used++;
    return value;
}

static void
map_grow(struct Map* map)
{
    uint32_t old_capacity = map->capacity;
    uint64_t* old_hashes = map->hashes;
    int32_t* old_values = map->values;

    map->capacity = old_capacity ? old_capacity * 2 : 1024;
    map->hashes = (uint64_t*)calloc(map->capacity, sizeof(uint64_t));
    map->values = (int32_t*)malloc(map->capacity * sizeof(int32_t));
    assert(map->hashes);
    assert(map->values);
    memset(map->values, -1, map->capacity * sizeof(int32_t));
    for( uint32_t i = 0; i < old_capacity; i++ )
    {
        uint32_t slot;

        if( old_values[i] < 0 )
            continue;
        /* Keys are unique already, so a rehash only needs a free slot. */
        slot = (uint32_t)old_hashes[i] & (map->capacity - 1);
        while( map->values[slot] >= 0 )
            slot = (slot + 1) & (map->capacity - 1);
        map->hashes[slot] = old_hashes[i];
        map->values[slot] = old_values[i];
    }
    free(old_hashes);
    free(old_values);
}

/* ------------------------------------------------------------------ */
/* The build's records                                                 */
/* ------------------------------------------------------------------ */

enum
{
    STATE_MAGIC_0 = 0x54534353u, /* "SCST" */
    STATE_FORMAT = 1,
};

/** One recorded question and the digest of its answer. */
struct Query
{
    uint8_t op;
    int32_t arg;
    char* name;
    uint64_t digest;      /* the answer when it was recorded */
    uint64_t now;         /* the answer in this build's tables */
    int evaluated;        /* `now` is filled in */
    int changed;          /* now != digest */
    int32_t stamp;        /* the last unit that recorded it, for per-unit dedup */
    int referenced;       /* some current unit depends on it (kept on save) */
};

struct UnitScript
{
    int32_t id;
    uint64_t hash;
    uint32_t rev;
    uint32_t length;
    uint8_t* blob; /* NULL when the slot compiled to nothing */
};

enum UnitStatus
{
    UNIT_UNCHANGED = 0, /* same size and mtime */
    UNIT_TOUCHED,       /* mtime moved, content hash did not */
    UNIT_EDITED,
    UNIT_NEW,
};

struct Unit
{
    char* path; /* as the compiler is handed it, and as it embeds it */
    char* rel;  /* the manifest's spelling */
    int weak;
    int64_t size;
    int64_t mtime;
    uint64_t hash;
    uint32_t rev;

    struct SSC_Decl* decls;
    int decl_count;
    int32_t* ids; /* per decl: the id, or -1 for a seam a lane took over */

    uint32_t* queries;
    int query_count;
    int query_capacity;

    struct UnitScript* scripts;
    int script_count;

    struct Buf warnings; /* lines, each prefixed '0' or '1' (ambiguous) */
    int ambiguous;

    /* This build only. */
    enum UnitStatus status;
    int dirty;
    int compiled;
    int64_t previous_size;
    int32_t why_query; /* the first recorded answer that moved, or -1 */
    int changed_answers;
};

struct Input
{
    char* path; /* as opened */
    char* rel;
    int is_dir;
    int64_t size;
    int64_t mtime;
    uint64_t hash;
};

struct State
{
    int loaded;
    int units_valid; /* same config: units may be reused */
    uint64_t config_key;
    uint32_t generation;

    char** id_names; /* id -> name ever given it ("" = never) */
    int id_count;

    struct Query* queries;
    int query_count;
    int query_capacity;

    struct Input* inputs;
    int input_count;

    struct Unit* units;
    int unit_count;

    int64_t dat_size;
    uint64_t dat_hash;
    int64_t idx_size;
    uint64_t idx_hash;
};

struct Build
{
    const struct SSC_BuildOptions* options;
    char content_normal[4096];
    struct State previous;

    /* The current tree. */
    struct Unit* units;
    int unit_count;
    int unit_capacity;
    struct Map unit_by_path; /* over previous.units */

    /* Ids. */
    char** id_names;
    int id_count;
    int id_capacity;
    struct Map id_by_name;
    int ids_new;

    /* Queries: previous ones first, then those first recorded this build. */
    struct Query* queries;
    int query_count;
    int query_capacity;
    struct Map query_by_key;

    /* Symbol inputs seen while loading. */
    struct Input* inputs;
    int input_count;
    int input_capacity;
    struct Map input_by_path;
    struct Map previous_input_by_path;

    struct SSC_Symbols symbols;
    int symbols_loaded;
    struct SSC_Compiler* compiler;
    int32_t current_unit;

    FILE* log;
    struct Buf log_text;
};

static void
logf_(struct Build* build, int to_stdout, const char* fmt, ...)
{
    va_list args;
    char line[4096];

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    buf_put(&build->log_text, line, strlen(line));
    if( to_stdout )
        fputs(line, stdout);
}

/* --- map adaptors --- */

static int
unit_path_equal(void* context, int32_t value, const void* key)
{
    const struct State* state = (const struct State*)context;

    return strcmp(state->units[value].path, (const char*)key) == 0;
}

static int
id_name_equal(void* context, int32_t value, const void* key)
{
    const struct Build* build = (const struct Build*)context;

    return strcmp(build->id_names[value], (const char*)key) == 0;
}

struct QueryKey
{
    int op;
    int32_t arg;
    const char* name;
};

static uint64_t
query_key_hash(int op, int32_t arg, const char* name)
{
    return ssc_hash_str(ssc_hash_u64(ssc_hash_u64(SSC_HASH_SEED, (uint64_t)op), (uint64_t)(uint32_t)arg),
                        name);
}

static int
query_equal(void* context, int32_t value, const void* key)
{
    const struct Build* build = (const struct Build*)context;
    const struct QueryKey* k = (const struct QueryKey*)key;
    const struct Query* q = &build->queries[value];

    return q->op == k->op && q->arg == k->arg && strcmp(q->name, k->name) == 0;
}

static int
previous_input_equal(void* context, int32_t value, const void* key)
{
    const struct Build* build = (const struct Build*)context;

    return strcmp(build->previous.inputs[value].path, (const char*)key) == 0;
}

static int
input_path_equal(void* context, int32_t value, const void* key)
{
    const struct Build* build = (const struct Build*)context;

    return strcmp(build->inputs[value].path, (const char*)key) == 0;
}

/* ------------------------------------------------------------------ */
/* Units                                                               */
/* ------------------------------------------------------------------ */

static void
unit_free(struct Unit* unit)
{
    free(unit->path);
    free(unit->rel);
    free(unit->decls);
    free(unit->ids);
    free(unit->queries);
    for( int i = 0; i < unit->script_count; i++ )
        free(unit->scripts[i].blob);
    free(unit->scripts);
    free(unit->warnings.data);
    memset(unit, 0, sizeof(*unit));
}

/** Clear what a unit compiled to, before it compiles again. */
static void
unit_clear_output(struct Unit* unit)
{
    free(unit->queries);
    unit->queries = NULL;
    unit->query_count = 0;
    unit->query_capacity = 0;
    for( int i = 0; i < unit->script_count; i++ )
        free(unit->scripts[i].blob);
    free(unit->scripts);
    unit->scripts = NULL;
    unit->script_count = 0;
    free(unit->warnings.data);
    memset(&unit->warnings, 0, sizeof(unit->warnings));
    unit->ambiguous = 0;
}

/** Take the reusable half of `from` (a previous build's record of the same file). */
static void
unit_adopt(struct Unit* unit, struct Unit* from)
{
    unit->rev = from->rev;
    unit->decls = from->decls;
    unit->decl_count = from->decl_count;
    unit->queries = from->queries;
    unit->query_count = from->query_count;
    unit->query_capacity = from->query_count;
    unit->scripts = from->scripts;
    unit->script_count = from->script_count;
    unit->warnings = from->warnings;
    unit->ambiguous = from->ambiguous;
    from->decls = NULL;
    from->decl_count = 0;
    from->queries = NULL;
    from->query_count = 0;
    from->scripts = NULL;
    from->script_count = 0;
    memset(&from->warnings, 0, sizeof(from->warnings));
}

/* ------------------------------------------------------------------ */
/* State file                                                          */
/* ------------------------------------------------------------------ */

static void
state_free(struct State* state)
{
    for( int i = 0; i < state->id_count; i++ )
        free(state->id_names[i]);
    free(state->id_names);
    for( int i = 0; i < state->query_count; i++ )
        free(state->queries[i].name);
    free(state->queries);
    for( int i = 0; i < state->input_count; i++ )
    {
        free(state->inputs[i].path);
        free(state->inputs[i].rel);
    }
    free(state->inputs);
    for( int i = 0; i < state->unit_count; i++ )
        unit_free(&state->units[i]);
    free(state->units);
    memset(state, 0, sizeof(*state));
}

/*
 * Read `<out>/ssc.state`. Returns a short reason when it could not be used,
 * NULL when it was read. The id table is read even when the configuration
 * changed — ids outlive a compiler upgrade — and the units only when it did not:
 * their declarations are this binary's struct layout.
 */
static const char*
state_read(struct State* state, const char* path, uint64_t config_key)
{
    size_t length;
    char* data = read_whole(path, &length);
    struct Reader reader;
    uint32_t count;

    memset(state, 0, sizeof(*state));
    if( !data )
        return "no state file (first build into this directory)";
    memset(&reader, 0, sizeof(reader));
    reader.data = (const uint8_t*)data;
    reader.length = length;
    if( rd_u32(&reader) != STATE_MAGIC_0 || rd_u32(&reader) != STATE_FORMAT )
    {
        free(data);
        return "state file is from another format; numbering afresh";
    }
    state->config_key = rd_u64(&reader);
    state->generation = rd_u32(&reader);

    count = rd_u32(&reader);
    if( count > SSC_MAX_SCRIPTS )
        reader.bad = 1;
    state->id_names = (char**)calloc(count ? count : 1, sizeof(char*));
    assert(state->id_names);
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
        state->id_names[state->id_count++] = rd_str(&reader);
    if( reader.bad )
    {
        free(data);
        state_free(state);
        return "state file is truncated; numbering afresh";
    }
    state->loaded = 1;
    if( state->config_key != config_key )
    {
        free(data);
        return "the compiler or the build configuration changed; every unit recompiles";
    }

    count = rd_u32(&reader);
    state->queries = (struct Query*)calloc(count ? count : 1, sizeof(struct Query));
    assert(state->queries);
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        struct Query* q = &state->queries[state->query_count++];

        q->op = rd_u8(&reader);
        q->arg = (int32_t)rd_u32(&reader);
        q->name = rd_str(&reader);
        q->digest = rd_u64(&reader);
    }
    state->query_capacity = state->query_count;

    count = rd_u32(&reader);
    state->inputs = (struct Input*)calloc(count ? count : 1, sizeof(struct Input));
    assert(state->inputs);
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        struct Input* input = &state->inputs[state->input_count++];

        input->is_dir = rd_u8(&reader);
        input->size = (int64_t)rd_u64(&reader);
        input->mtime = (int64_t)rd_u64(&reader);
        input->hash = rd_u64(&reader);
        input->path = rd_str(&reader);
        input->rel = rd_str(&reader);
    }

    count = rd_u32(&reader);
    state->units = (struct Unit*)calloc(count ? count : 1, sizeof(struct Unit));
    assert(state->units);
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        struct Unit* unit = &state->units[state->unit_count++];
        uint32_t n;

        unit->path = rd_str(&reader);
        unit->rel = rd_str(&reader);
        unit->weak = rd_u8(&reader);
        unit->size = (int64_t)rd_u64(&reader);
        unit->mtime = (int64_t)rd_u64(&reader);
        unit->hash = rd_u64(&reader);
        unit->rev = rd_u32(&reader);

        n = rd_u32(&reader);
        if( n > SSC_MAX_SCRIPTS )
        {
            reader.bad = 1;
            break;
        }
        unit->decls = (struct SSC_Decl*)calloc(n ? n : 1, sizeof(struct SSC_Decl));
        unit->ids = (int32_t*)calloc(n ? n : 1, sizeof(int32_t));
        assert(unit->decls);
        assert(unit->ids);
        unit->decl_count = (int)n;
        {
            const void* p = rd_take(&reader, n * sizeof(struct SSC_Decl));

            if( p )
                memcpy(unit->decls, p, n * sizeof(struct SSC_Decl));
        }
        for( uint32_t d = 0; d < n; d++ )
            unit->ids[d] = (int32_t)rd_u32(&reader);

        n = rd_u32(&reader);
        if( n > (uint32_t)state->query_count )
        {
            reader.bad = 1;
            break;
        }
        unit->queries = (uint32_t*)malloc((n ? n : 1) * sizeof(uint32_t));
        assert(unit->queries);
        unit->query_count = (int)n;
        unit->query_capacity = (int)n;
        for( uint32_t q = 0; q < n; q++ )
        {
            unit->queries[q] = rd_u32(&reader);
            if( unit->queries[q] >= (uint32_t)state->query_count )
                reader.bad = 1;
        }

        n = rd_u32(&reader);
        if( n > SSC_MAX_SCRIPTS )
        {
            reader.bad = 1;
            break;
        }
        unit->scripts = (struct UnitScript*)calloc(n ? n : 1, sizeof(struct UnitScript));
        assert(unit->scripts);
        unit->script_count = (int)n;
        for( uint32_t s = 0; s < n && !reader.bad; s++ )
        {
            struct UnitScript* script = &unit->scripts[s];
            const void* p;

            script->id = (int32_t)rd_u32(&reader);
            script->hash = rd_u64(&reader);
            script->rev = rd_u32(&reader);
            script->length = rd_u32(&reader);
            p = rd_take(&reader, script->length);
            if( p && script->length )
            {
                script->blob = (uint8_t*)malloc(script->length);
                assert(script->blob);
                memcpy(script->blob, p, script->length);
            }
        }
        unit->ambiguous = (int)rd_u32(&reader);
        n = rd_u32(&reader);
        {
            const void* p = rd_take(&reader, n);

            if( p && n )
                buf_put(&unit->warnings, p, n);
        }
    }
    state->dat_size = (int64_t)rd_u64(&reader);
    state->dat_hash = rd_u64(&reader);
    state->idx_size = (int64_t)rd_u64(&reader);
    state->idx_hash = rd_u64(&reader);
    free(data);
    if( reader.bad )
    {
        /* The ids are intact (read above); only the reuse is lost. */
        for( int i = 0; i < state->unit_count; i++ )
            unit_free(&state->units[i]);
        state->unit_count = 0;
        return "state file is damaged past the id table; every unit recompiles";
    }
    state->units_valid = 1;
    return NULL;
}

static int
state_write(struct Build* build, const char* path, uint32_t generation, int64_t dat_size,
            uint64_t dat_hash, int64_t idx_size, uint64_t idx_hash)
{
    struct Buf buf;
    char temporary[1200];
    uint32_t* remap;
    uint32_t kept = 0;
    FILE* file;
    int ok;

    memset(&buf, 0, sizeof(buf));
    buf_u32(&buf, STATE_MAGIC_0);
    buf_u32(&buf, STATE_FORMAT);
    buf_u64(&buf, build->options->config_key);
    buf_u32(&buf, generation);

    buf_u32(&buf, (uint32_t)build->id_count);
    for( int i = 0; i < build->id_count; i++ )
        buf_str(&buf, build->id_names[i] ? build->id_names[i] : "");

    /* Only the queries some current unit still depends on. */
    remap = (uint32_t*)malloc(((size_t)build->query_count + 1) * sizeof(uint32_t));
    assert(remap);
    for( int i = 0; i < build->query_count; i++ )
        build->queries[i].referenced = 0;
    for( int u = 0; u < build->unit_count; u++ )
    {
        for( int q = 0; q < build->units[u].query_count; q++ )
            build->queries[build->units[u].queries[q]].referenced = 1;
    }
    for( int i = 0; i < build->query_count; i++ )
        remap[i] = build->queries[i].referenced ? kept++ : UINT32_MAX;
    buf_u32(&buf, kept);
    for( int i = 0; i < build->query_count; i++ )
    {
        const struct Query* q = &build->queries[i];

        if( !q->referenced )
            continue;
        assert(q->evaluated);
        buf_u8(&buf, q->op);
        buf_u32(&buf, (uint32_t)q->arg);
        buf_str(&buf, q->name);
        buf_u64(&buf, q->now);
    }

    buf_u32(&buf, (uint32_t)build->input_count);
    for( int i = 0; i < build->input_count; i++ )
    {
        const struct Input* input = &build->inputs[i];

        buf_u8(&buf, (uint8_t)input->is_dir);
        buf_u64(&buf, (uint64_t)input->size);
        buf_u64(&buf, (uint64_t)input->mtime);
        buf_u64(&buf, input->hash);
        buf_str(&buf, input->path);
        buf_str(&buf, input->rel);
    }

    buf_u32(&buf, (uint32_t)build->unit_count);
    for( int u = 0; u < build->unit_count; u++ )
    {
        const struct Unit* unit = &build->units[u];

        buf_str(&buf, unit->path);
        buf_str(&buf, unit->rel);
        buf_u8(&buf, (uint8_t)unit->weak);
        buf_u64(&buf, (uint64_t)unit->size);
        buf_u64(&buf, (uint64_t)unit->mtime);
        buf_u64(&buf, unit->hash);
        buf_u32(&buf, unit->rev);
        buf_u32(&buf, (uint32_t)unit->decl_count);
        buf_put(&buf, unit->decls, (size_t)unit->decl_count * sizeof(struct SSC_Decl));
        for( int d = 0; d < unit->decl_count; d++ )
            buf_u32(&buf, (uint32_t)unit->ids[d]);
        buf_u32(&buf, (uint32_t)unit->query_count);
        for( int q = 0; q < unit->query_count; q++ )
        {
            assert(remap[unit->queries[q]] != UINT32_MAX);
            buf_u32(&buf, remap[unit->queries[q]]);
        }
        buf_u32(&buf, (uint32_t)unit->script_count);
        for( int s = 0; s < unit->script_count; s++ )
        {
            const struct UnitScript* script = &unit->scripts[s];

            buf_u32(&buf, (uint32_t)script->id);
            buf_u64(&buf, script->hash);
            buf_u32(&buf, script->rev);
            buf_u32(&buf, script->length);
            buf_put(&buf, script->blob, script->length);
        }
        buf_u32(&buf, (uint32_t)unit->ambiguous);
        buf_u32(&buf, (uint32_t)unit->warnings.length);
        buf_put(&buf, unit->warnings.data, unit->warnings.length);
    }
    buf_u64(&buf, (uint64_t)dat_size);
    buf_u64(&buf, dat_hash);
    buf_u64(&buf, (uint64_t)idx_size);
    buf_u64(&buf, idx_hash);
    free(remap);

    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    ok = file && fwrite(buf.data, 1, buf.length, file) == buf.length;
    if( file && fclose(file) != 0 )
        ok = 0;
    if( ok )
        ok = replace_file(temporary, path) == 0;
    free(buf.data);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Walking the tree                                                    */
/* ------------------------------------------------------------------ */

static int
path_is_within(const char* path, const char* prefix)
{
    size_t length = strlen(prefix);

    while( length > 0 && prefix[length - 1] == '/' )
        length--;
    if( strncmp(path, prefix, length) != 0 )
        return 0;
    return path[length] == '\0' || path[length] == '/';
}

struct WalkFile
{
    char* path;
    int64_t size;
    int64_t mtime;
};

struct WalkList
{
    struct WalkFile* files;
    int count;
    int capacity;
};

/* SSC_CompileRoots' collect_sources, with the same path spelling, but reading
 * d_type instead of stat()ing every entry: only a `.rs2` is stat'd. */
static void
walk_sources(
    const char* dir,
    const char* const* excludes,
    int exclude_count,
    struct WalkList* list)
{
    DIR* handle;
    struct dirent* entry;

    for( int i = 0; i < exclude_count; i++ )
    {
        if( path_is_within(dir, excludes[i]) )
            return;
    }
    handle = opendir(dir);
    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char path[1024];
        size_t length;
        int is_dir;
        struct stat info;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        length = strlen(entry->d_name);
        is_dir = SSC_DirEntryIsDir(entry, path);
        if( is_dir < 0 )
            continue;
        if( is_dir )
        {
            walk_sources(path, excludes, exclude_count, list);
            continue;
        }
        if( length < 4 || strcmp(entry->d_name + length - 4, ".rs2") != 0 )
            continue;
        if( stat(path, &info) != 0 )
            continue;
        if( list->count == list->capacity )
        {
            list->capacity = list->capacity ? list->capacity * 2 : 1024;
            list->files =
                (struct WalkFile*)realloc(list->files, (size_t)list->capacity * sizeof(*list->files));
            assert(list->files);
        }
        list->files[list->count].path = strdup(path);
        list->files[list->count].size = (int64_t)info.st_size;
        list->files[list->count].mtime = stat_mtime_ns(&info);
        list->count++;
    }
    closedir(handle);
}

static int
walk_file_compare(const void* a, const void* b)
{
    return strcmp(((const struct WalkFile*)a)->path, ((const struct WalkFile*)b)->path);
}

/* ------------------------------------------------------------------ */
/* Inputs (the symbol table's files)                                   */
/* ------------------------------------------------------------------ */

/*
 * What a directory the loaders listed (or searched) holds that they could read:
 * its subdirectories and its files with a symbol-source extension, by name.
 *
 * Not its mtime. Packs are written into server/scripts/build, a selftest drops
 * screenshots into server/scripts/selftest, and the allocator keeps its stamp
 * in build/ — each moves a directory mtime that the symbol loaders walk, and
 * with the mtime as the test every build after one of those reloaded the whole
 * symbol table for nothing. A new `.constant` in an existing directory, or a new
 * subdirectory that may hold one, still changes this.
 */
static int
symbol_source_name(const char* name)
{
    static const char* const k_extensions[] = { ".pack", ".compack", ".alloc", ".constant",
                                                ".dbtable", ".varp", ".varbit" };
    size_t length = strlen(name);

    if( strncmp(name, "all.", 4) == 0 )
        return 1;
    for( size_t i = 0; i < sizeof(k_extensions) / sizeof(k_extensions[0]); i++ )
    {
        size_t ext = strlen(k_extensions[i]);

        if( length > ext && strcmp(name + length - ext, k_extensions[i]) == 0 )
            return 1;
    }
    return 0;
}

static int
compare_names(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static uint64_t
dir_listing_hash(const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    char** names = NULL;
    int count = 0;
    int capacity = 0;
    uint64_t hash = ssc_hash_u64(SSC_HASH_SEED, 0x6469726c697374ull);

    if( !handle )
        return 0;
    while( (entry = readdir(handle)) != NULL )
    {
        char path[1200];
        char name[600];
        int is_dir;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        is_dir = SSC_DirEntryIsDir(entry, path);
        if( is_dir < 0 || (!is_dir && !symbol_source_name(entry->d_name)) )
            continue;
        snprintf(name, sizeof(name), "%s%s", entry->d_name, is_dir ? "/" : "");
        if( count == capacity )
        {
            capacity = capacity ? capacity * 2 : 64;
            names = (char**)realloc(names, (size_t)capacity * sizeof(char*));
            assert(names);
        }
        names[count] = strdup(name);
        assert(names[count]);
        count++;
    }
    closedir(handle);
    qsort(names, (size_t)count, sizeof(char*), compare_names);
    for( int i = 0; i < count; i++ )
    {
        hash = ssc_hash_str(hash, names[i]);
        free(names[i]);
    }
    free(names);
    return hash;
}

/*
 * Called by the symbol loaders as they open each file. The file is stat'd and
 * hashed here, BEFORE the loader reads it, so the record can only ever be older
 * than what was compiled: an edit landing mid-build shows up as a change next
 * time instead of being stamped as already included.
 */
static void
observe_input(void* context, const char* path, int is_dir)
{
    struct Build* build = (struct Build*)context;
    uint64_t hash = ssc_hash_str(SSC_HASH_SEED, path);
    struct Input* input;
    struct stat info;
    int32_t found;
    int32_t index;

    found = map_find(&build->input_by_path, hash, path, input_path_equal, build);
    if( found >= 0 )
        return;
    if( build->input_count == build->input_capacity )
    {
        build->input_capacity = build->input_capacity ? build->input_capacity * 2 : 1024;
        build->inputs = (struct Input*)realloc(build->inputs,
                                               (size_t)build->input_capacity * sizeof(struct Input));
        assert(build->inputs);
    }
    index = build->input_count++;
    input = &build->inputs[index];
    memset(input, 0, sizeof(*input));
    input->path = strdup(path);
    input->rel = manifest_path(build->content_normal, path);
    input->is_dir = is_dir;
    map_insert(&build->input_by_path, hash, input->path, index, input_path_equal, build);
    if( stat(path, &info) != 0 )
        return;
    input->size = is_dir ? 0 : (int64_t)info.st_size;
    input->mtime = stat_mtime_ns(&info);
    if( is_dir )
    {
        input->hash = dir_listing_hash(path);
        return;
    }
    /* The previous build's hash, when nothing about the file moved. */
    found = map_find(&build->previous_input_by_path, hash, path, previous_input_equal, build);
    if( found >= 0 )
    {
        const struct Input* old = &build->previous.inputs[found];

        if( old->size == input->size && old->mtime == input->mtime && !old->is_dir )
        {
            input->hash = old->hash;
            return;
        }
    }
    {
        int ok;

        input->hash = hash_file(path, &ok);
    }
}

/**
 * Has any file the symbol table was built from changed since the last build?
 * Writes up to `max` names of changed inputs into `changed` for the log.
 */
static int
inputs_changed(struct Build* build, const char** changed, int max)
{
    int count = 0;

    for( int i = 0; i < build->previous.input_count; i++ )
    {
        const struct Input* input = &build->previous.inputs[i];
        struct stat info;
        int moved;

        if( stat(input->path, &info) != 0 )
        {
            moved = !input->is_dir || input->mtime != 0;
        }
        else if( input->is_dir )
        {
            moved = dir_listing_hash(input->path) != input->hash;
        }
        else if( (int64_t)info.st_size == input->size && stat_mtime_ns(&info) == input->mtime )
        {
            moved = 0;
        }
        else
        {
            int ok;
            uint64_t hash = hash_file(input->path, &ok);

            moved = !ok || hash != input->hash;
        }
        if( moved )
        {
            if( count < max )
                changed[count] = input->rel;
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

static uint64_t
query_answer(struct Build* build, const struct Query* q)
{
    if( q->op == SSC_QUERY_SCRIPT )
        return SSC_ScriptQueryDigest(build->compiler, q->name);
    return SSC_SymbolsQueryDigest(&build->symbols, q->op, q->name, q->arg);
}

static int32_t
query_intern(struct Build* build, int op, int32_t arg, const char* name)
{
    struct QueryKey key;
    uint64_t hash = query_key_hash(op, arg, name);
    int32_t found;
    struct Query* q;

    key.op = op;
    key.arg = arg;
    key.name = name;
    found = map_find(&build->query_by_key, hash, &key, query_equal, build);
    if( found >= 0 )
        return found;
    if( build->query_count == build->query_capacity )
    {
        build->query_capacity = build->query_capacity ? build->query_capacity * 2 : 65536;
        build->queries = (struct Query*)realloc(build->queries,
                                                (size_t)build->query_capacity * sizeof(struct Query));
        assert(build->queries);
    }
    q = &build->queries[build->query_count];
    memset(q, 0, sizeof(*q));
    q->op = (uint8_t)op;
    q->arg = arg;
    q->name = strdup(name);
    q->stamp = -1;
    key.name = q->name;
    map_insert(&build->query_by_key, hash, &key, build->query_count, query_equal, build);
    return build->query_count++;
}

/** The compiler's observer: record the question against the unit compiling. */
static void
observe_query(void* context, int op, const char* name, int32_t arg)
{
    struct Build* build = (struct Build*)context;
    struct Unit* unit;
    int32_t index;
    struct Query* q;

    if( build->current_unit < 0 )
        return;
    index = query_intern(build, op, arg, name);
    q = &build->queries[index];
    if( !q->evaluated )
    {
        q->now = query_answer(build, q);
        q->evaluated = 1;
    }
    if( q->stamp == build->current_unit )
        return;
    q->stamp = build->current_unit;
    unit = &build->units[build->current_unit];
    if( unit->query_count == unit->query_capacity )
    {
        unit->query_capacity = unit->query_capacity ? unit->query_capacity * 2 : 64;
        unit->queries =
            (uint32_t*)realloc(unit->queries, (size_t)unit->query_capacity * sizeof(uint32_t));
        assert(unit->queries);
    }
    unit->queries[unit->query_count++] = (uint32_t)index;
}

static void
warning_sink(void* context, int ambiguous, const char* text)
{
    struct Build* build = (struct Build*)context;
    struct Unit* unit;

    assert(build->current_unit >= 0);
    unit = &build->units[build->current_unit];
    buf_u8(&unit->warnings, ambiguous ? '1' : '0');
    buf_put(&unit->warnings, text, strlen(text));
    if( !text[0] || text[strlen(text) - 1] != '\n' )
        buf_u8(&unit->warnings, '\n');
}

static const char*
query_op_label(int op)
{
    switch( op )
    {
    case SSC_QUERY_FIND:
        return "symbol";
    case SSC_QUERY_VALUE:
        return "bare name";
    case SSC_QUERY_KINDS:
        return "namespaces of";
    case SSC_QUERY_CARRIER:
        return "varbits packed into varp";
    case SSC_QUERY_SCRIPT:
        return "script";
    default:
        return "?";
    }
}

/** "symbol obj:bronze_axe (now obj 1351)", for the log and --explain. */
static void
describe_query(struct Build* build, const struct Query* q, char* out, size_t capacity)
{
    if( q->op == SSC_QUERY_CARRIER )
    {
        snprintf(out, capacity, "%s %d", query_op_label(q->op), q->arg);
        return;
    }
    if( q->op == SSC_QUERY_FIND || q->op == SSC_QUERY_VALUE )
    {
        const struct SSC_Symbol* symbol;
        SSC_QueryObserver saved = build->symbols.observe;

        build->symbols.observe = NULL;
        symbol = q->op == SSC_QUERY_FIND
                     ? SSC_SymbolsFind(&build->symbols, q->name, (enum SSC_SymbolKind)q->arg)
                     : SSC_SymbolsFindValue(&build->symbols, q->name);
        build->symbols.observe = saved;
        if( !symbol )
            snprintf(out, capacity, "%s %s%s (now unresolved)", query_op_label(q->op),
                     q->op == SSC_QUERY_FIND && q->arg == SSC_SYM_CONSTANT ? "^" : "", q->name);
        else if( symbol->kind == SSC_SYM_CONSTANT )
            snprintf(out, capacity, "constant ^%s (now \"%.60s\")", q->name,
                     symbol->text ? symbol->text : "");
        else
            snprintf(out, capacity, "%s %s (now %s %d)", query_op_label(q->op), q->name,
                     SSC_SymbolKindLabel(symbol->kind), symbol->value);
        return;
    }
    snprintf(out, capacity, "%s %s", query_op_label(q->op), q->name);
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static void
put_be32(struct Buf* buf, uint32_t value)
{
    uint8_t bytes[4] = { (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                         (uint8_t)value };

    buf_put(buf, bytes, 4);
}

static int
write_file_atomic(const char* path, const void* data, size_t length)
{
    char temporary[1200];
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

/*
 * A successful build — a no-op included — leaves script.dat and script.idx
 * newer than every source. The pack's own staleness answer is the manifest, but
 * an mtime is what a server binary from before the manifest compares (the old
 * scripts_newer_than_pack), and what tools/server_scripts_stale.py reads; an
 * incremental build that rewrote nothing must not read as stale to them.
 */
static void
touch_outputs(const char* out)
{
    static const char* const names[] = { "script.dat", "script.idx" };

    for( size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++ )
    {
        char path[1100];

        snprintf(path, sizeof(path), "%s/%s", out, names[i]);
        utime(path, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* The build                                                           */
/* ------------------------------------------------------------------ */

static void
id_table_add(struct Build* build, int id, const char* name)
{
    while( id >= build->id_capacity )
    {
        build->id_capacity = build->id_capacity ? build->id_capacity * 2 : 65536;
        build->id_names = (char**)realloc(build->id_names, (size_t)build->id_capacity * sizeof(char*));
        assert(build->id_names);
    }
    while( build->id_count <= id )
        build->id_names[build->id_count++] = NULL;
    assert(!build->id_names[id]);
    build->id_names[id] = strdup(name);
    if( name[0] )
        map_insert(&build->id_by_name, ssc_hash_str(SSC_HASH_SEED, name), build->id_names[id], id,
                   id_name_equal, build);
}

static int32_t
id_for(struct Build* build, const char* name)
{
    uint64_t hash = ssc_hash_str(SSC_HASH_SEED, name);
    int32_t id = map_find(&build->id_by_name, hash, name, id_name_equal, build);

    if( id >= 0 )
        return id;
    id = build->id_count;
    id_table_add(build, id, name);
    build->ids_new++;
    return id;
}

static const char*
status_label(const struct Unit* unit)
{
    switch( unit->status )
    {
    case UNIT_NEW:
        return "new file";
    case UNIT_EDITED:
        return "edited";
    case UNIT_TOUCHED:
        return "touched, content unchanged";
    default:
        return "unchanged";
    }
}

static int
load_symbols_once(struct Build* build)
{
    if( build->symbols_loaded )
        return 1;
    SSC_SymbolsInit(&build->symbols);
    build->symbols.observe_input = observe_input;
    build->symbols.observe_context = build;
    if( !build->options->load_symbols(build->options->load_context, &build->symbols) )
        return 0;
    build->symbols.observe_input = NULL;
    build->symbols_loaded = 1;
    return 1;
}

/** Print each unit's warnings in unit order, cutting the ambiguous family at
 *  the 20 the old compiler showed (all of them under SSCOMPILE_AMBIGUOUS=all). */
static void
replay_warnings(struct Build* build)
{
    const char* setting = getenv("SSCOMPILE_AMBIGUOUS");
    int show_all = setting && strcmp(setting, "all") == 0;
    int ambiguous = 0;

    for( int u = 0; u < build->unit_count; u++ )
    {
        const struct Buf* warnings = &build->units[u].warnings;
        size_t at = 0;

        while( at < warnings->length )
        {
            const char* line = (const char*)warnings->data + at;
            const char* end = memchr(line, '\n', warnings->length - at);
            size_t length = end ? (size_t)(end - line) + 1 : warnings->length - at;

            at += length;
            if( line[0] == '1' )
            {
                ambiguous++;
                if( !show_all && ambiguous == 20 )
                {
                    fprintf(stderr, "sscompile: further ambiguous names not shown; "
                                    "SSCOMPILE_AMBIGUOUS=all lists every one\n");
                    continue;
                }
                if( !show_all && ambiguous > 20 )
                    continue;
            }
            fwrite(line + 1, 1, length - 1, stderr);
        }
    }
}

static int
compile_unit(struct Build* build, int u, struct SSC_Diag* diag)
{
    struct Unit* unit = &build->units[u];
    int before = SSC_AmbiguousNameCount(build->compiler);
    struct SSVM_Error err;
    int ok;

    unit_clear_output(unit);
    build->current_unit = u;
    SSC_SetWeakSource(build->compiler, unit->weak);
    memset(diag, 0, sizeof(*diag));
    ok = SSC_CompileFile(build->compiler, unit->path, diag);
    SSC_SetWeakSource(build->compiler, 0);
    build->current_unit = -1;
    unit->ambiguous = SSC_AmbiguousNameCount(build->compiler) - before;
    if( !ok )
        return 0;

    /* One blob per declared id, in declaration order: what script.dat holds. */
    unit->scripts = (struct UnitScript*)calloc(unit->decl_count ? (size_t)unit->decl_count : 1,
                                               sizeof(struct UnitScript));
    assert(unit->scripts);
    SSVM_ErrorClear(&err);
    for( int d = 0; d < unit->decl_count; d++ )
    {
        const struct SSVM_Script* script;
        struct UnitScript* out;
        size_t needed = 0;

        if( unit->ids[d] < 0 )
            continue;
        out = &unit->scripts[unit->script_count++];
        out->id = unit->ids[d];
        script = SSC_ScriptAt(build->compiler, out->id);
        /* SSC_Write's rule: nothing compiled into the slot is an empty slot. */
        if( script && script->op_count &&
            SSVM_ScriptEncode(script, NULL, 0, &needed, &err) && needed )
        {
            out->blob = (uint8_t*)malloc(needed);
            assert(out->blob);
            if( !SSVM_ScriptEncode(script, out->blob, needed, &needed, &err) )
            {
                snprintf(diag->file, sizeof(diag->file), "%s", unit->path);
                snprintf(diag->message, sizeof(diag->message), "encoding script %d: %s",
                         out->id, err.message);
                return 0;
            }
            out->length = (uint32_t)needed;
        }
        out->hash = ssc_hash_bytes(out->blob, out->length, SSC_HASH_SEED);
    }
    return 1;
}

int
SSC_BuildPack(const struct SSC_BuildOptions* options)
{
    struct Build build_storage;
    struct Build* build = &build_storage;
    struct BuildLock lock;
    char state_path[1100];
    char path[1100];
    const char* state_note;
    double t_start = now_seconds();
    double t_walk = 0, t_symbols = 0, t_declare = 0, t_check = 0, t_compile = 0, t_write = 0;
    double t_mark;
    double t_state = 0;
    double t_inputs = 0;
    struct WalkList strong;
    struct WalkList weak;
    int changed_files = 0;
    int removed_units = 0;
    int removed_scripts = 0;
    const char* changed_inputs[8];
    int changed_input_count = 0;
    int status = 0;
    int compiled = 0;
    int reused = 0;
    int errors = 0;
    int explain_found = 0;
    uint32_t generation;
    int outputs_changed = 0;

    assert(options);
    assert(options->out);
    assert(options->content_root);
    assert(options->roots);
    assert(options->load_symbols);

    memset(build, 0, sizeof(*build));
    build->options = options;
    build->current_unit = -1;
    normal_path(options->content_root, build->content_normal, sizeof(build->content_normal));
    map_init(&build->unit_by_path);
    map_init(&build->id_by_name);
    map_init(&build->query_by_key);
    map_init(&build->input_by_path);

    if( !lock_take(&lock, options->out) )
    {
        fprintf(stderr, "sscompile: cannot lock %s/.pack.lock\n", options->out);
        return 2;
    }

    /* --- previous state --- */
    snprintf(state_path, sizeof(state_path), "%s/ssc.state", options->out);
    t_mark = now_seconds();
    state_note = state_read(&build->previous, state_path, options->config_key);
    t_state = now_seconds() - t_mark;
    if( options->renumber )
    {
        state_free(&build->previous);
        state_note = "--renumber: the ids are numbered afresh";
    }
    if( !options->renumber && build->previous.loaded )
    {
        for( int i = 0; i < build->previous.id_count; i++ )
        {
            if( build->previous.id_names[i][0] )
                id_table_add(build, i, build->previous.id_names[i]);
            else
            {
                id_table_add(build, i, "");
            }
        }
    }
    if( options->full && build->previous.units_valid )
        state_note = "--full: every unit recompiles (ids kept)";
    map_init(&build->previous_input_by_path);
    for( int i = 0; i < build->previous.input_count; i++ )
        map_insert(&build->previous_input_by_path,
                   ssc_hash_str(SSC_HASH_SEED, build->previous.inputs[i].path),
                   build->previous.inputs[i].path, i, previous_input_equal, build);
    for( int i = 0; i < build->previous.unit_count; i++ )
        map_insert(&build->unit_by_path, ssc_hash_str(SSC_HASH_SEED, build->previous.units[i].path),
                   build->previous.units[i].path, i, unit_path_equal, &build->previous);
    /* The previous queries become this build's first ones, same indices. */
    for( int i = 0; i < build->previous.query_count; i++ )
    {
        struct Query* old = &build->previous.queries[i];
        int32_t index = query_intern(build, old->op, old->arg, old->name);

        assert(index == i);
        build->queries[index].digest = old->digest;
    }
    generation = build->previous.generation;

    snprintf(path, sizeof(path), "%s/pack.log", options->out);
    logf_(build, 0, "sscompile pack log: %s%s%s\n", options->out,
          options->lanes && options->lanes[0] ? " lanes: " : "",
          options->lanes ? options->lanes : "");
    if( state_note )
        logf_(build, !options->explain, "sscompile: %s\n", state_note);

    /* --- walk --- */
    t_mark = now_seconds();
    memset(&strong, 0, sizeof(strong));
    memset(&weak, 0, sizeof(weak));
    for( int i = 0; i < options->root_count; i++ )
    {
        const char* kept[128];
        int kept_count = 0;

        /* SSC_CompileRoots' rule: every other root is an exclusion too. */
        for( int j = 0; j < options->exclude_count + options->root_count; j++ )
        {
            const char* exclusion = j < options->exclude_count
                                        ? options->excludes[j]
                                        : options->roots[j - options->exclude_count].dir;

            if( path_is_within(options->roots[i].dir, exclusion) )
                continue;
            assert(kept_count < (int)(sizeof(kept) / sizeof(kept[0])));
            kept[kept_count++] = exclusion;
        }
        walk_sources(options->roots[i].dir, kept, kept_count,
                     options->roots[i].weak ? &weak : &strong);
    }
    qsort(strong.files, (size_t)strong.count, sizeof(struct WalkFile), walk_file_compare);
    qsort(weak.files, (size_t)weak.count, sizeof(struct WalkFile), walk_file_compare);

    build->unit_capacity = strong.count + weak.count;
    build->units = (struct Unit*)calloc(build->unit_capacity ? (size_t)build->unit_capacity : 1,
                                        sizeof(struct Unit));
    assert(build->units);
    for( int pass = 0; pass < 2; pass++ )
    {
        struct WalkList* list = pass == 0 ? &strong : &weak;

        for( int i = 0; i < list->count; i++ )
        {
            struct Unit* unit = &build->units[build->unit_count++];
            const char* file = list->files[i].path;
            int32_t found = build->previous.units_valid
                                ? map_find(&build->unit_by_path, ssc_hash_str(SSC_HASH_SEED, file),
                                           file, unit_path_equal, &build->previous)
                                : -1;

            unit->path = list->files[i].path;
            list->files[i].path = NULL;
            unit->rel = manifest_path(build->content_normal, unit->path);
            unit->weak = pass == 1;
            unit->size = list->files[i].size;
            unit->mtime = list->files[i].mtime;
            unit->why_query = -1;
            if( found >= 0 )
            {
                struct Unit* old = &build->previous.units[found];

                unit->previous_size = old->size;
                if( old->size == unit->size && old->mtime == unit->mtime && old->weak == unit->weak )
                {
                    unit->hash = old->hash;
                    unit->status = UNIT_UNCHANGED;
                }
                else
                {
                    int ok;

                    unit->hash = hash_file(unit->path, &ok);
                    unit->status = ok && unit->hash == old->hash && old->weak == unit->weak
                                       ? UNIT_TOUCHED
                                       : UNIT_EDITED;
                }
                if( unit->status != UNIT_EDITED )
                {
                    unit_adopt(unit, old);
                    unit->ids = old->ids; /* checked against this build's ids below */
                    old->ids = NULL;
                }
                else
                {
                    unit->rev = old->rev;
                }
                /* Claimed: whatever is left unclaimed was removed. */
                old->path[0] = '\0';
            }
            else
            {
                int ok;

                unit->hash = hash_file(unit->path, &ok);
                unit->status = UNIT_NEW;
            }
            if( unit->status == UNIT_EDITED || unit->status == UNIT_NEW )
                changed_files++;
        }
    }
    free(strong.files);
    free(weak.files);
    for( int i = 0; i < build->previous.unit_count; i++ )
    {
        const struct Unit* old = &build->previous.units[i];

        if( old->path[0] )
        {
            removed_units++;
            for( int s = 0; s < old->script_count; s++ )
                removed_scripts++;
            logf_(build, !options->explain, "removed   %s — %d script id(s) retired\n", old->rel,
                  old->script_count);
        }
    }
    t_walk = now_seconds() - t_mark;
    t_mark = now_seconds();
    if( build->previous.units_valid )
        changed_input_count = inputs_changed(build, changed_inputs, 8);
    t_inputs = now_seconds() - t_mark;

    /* --- the no-op path: nothing to load, nothing to ask --- */
    if( build->previous.units_valid && !options->full && !options->explain && !options->dry_run &&
        changed_files == 0 && removed_units == 0 && changed_input_count == 0 )
    {
        struct stat info;
        int outputs_ok = 1;

        snprintf(path, sizeof(path), "%s/script.dat", options->out);
        outputs_ok = stat(path, &info) == 0 && (int64_t)info.st_size == build->previous.dat_size;
        snprintf(path, sizeof(path), "%s/script.idx", options->out);
        outputs_ok = outputs_ok && stat(path, &info) == 0 &&
                     (int64_t)info.st_size == build->previous.idx_size;
        snprintf(path, sizeof(path), "%s/pack.manifest", options->out);
        outputs_ok = outputs_ok && stat(path, &info) == 0;
        if( outputs_ok )
        {
            int touched = 0;

            for( int u = 0; u < build->unit_count; u++ )
            {
                touched += build->units[u].status == UNIT_TOUCHED;
                logf_(build, options->verbose, "reused    %s — %s\n", build->units[u].rel,
                      status_label(&build->units[u]));
            }
            replay_warnings(build);
            /* Inputs are carried over unchanged; mtimes of touched units are
             * refreshed so the next check does not hash them again. */
            build->inputs = build->previous.inputs;
            build->input_count = build->previous.input_count;
            build->previous.inputs = NULL;
            build->previous.input_count = 0;
            if( touched )
            {
                for( int i = 0; i < build->query_count; i++ )
                {
                    build->queries[i].now = build->queries[i].digest;
                    build->queries[i].evaluated = 1;
                }
                state_write(build, state_path, generation, build->previous.dat_size,
                            build->previous.dat_hash, build->previous.idx_size,
                            build->previous.idx_hash);
            }
            logf_(build, 1,
                  "sscompile: up to date — 0 compiled, %d reused, %d touched; %d script ids -> "
                  "%s/script.dat [state %.2fs, walk %.2fs, inputs %.2fs; total %.2fs]\n",
                  build->unit_count, touched, build->id_count, options->out, t_state, t_walk,
                  t_inputs, now_seconds() - t_start);
            snprintf(path, sizeof(path), "%s/pack.log", options->out);
            write_file_atomic(path, build->log_text.data, build->log_text.length);
            touch_outputs(options->out);
            goto done;
        }
        logf_(build, 1, "sscompile: the outputs in %s are missing or were replaced; rewriting\n",
              options->out);
    }

    /* --- symbols --- */
    t_mark = now_seconds();
    if( !load_symbols_once(build) )
    {
        status = 1;
        goto done;
    }
    t_symbols = now_seconds() - t_mark;

    /* --- declare: every unit's names, at their stable ids --- */
    t_mark = now_seconds();
    build->compiler = SSC_New(&build->symbols);
    assert(build->compiler);
    for( int u = 0; u < build->unit_count && !errors; u++ )
    {
        struct Unit* unit = &build->units[u];
        struct SSC_Diag diag;

        memset(&diag, 0, sizeof(diag));
        if( !unit->decls )
        {
            if( !SSC_ScanDeclarations(unit->path, &unit->decls, &unit->decl_count, &diag) )
            {
                fprintf(stderr, "sscompile: %s\n", diag.message);
                errors++;
                break;
            }
            free(unit->ids);
            unit->ids = NULL;
        }
        {
            int32_t* ids = (int32_t*)malloc((unit->decl_count ? (size_t)unit->decl_count : 1) *
                                            sizeof(int32_t));
            int moved = 0;

            assert(ids);
            for( int d = 0; d < unit->decl_count; d++ )
            {
                int prepared = SSC_DeclarePrepare(build->compiler, &unit->decls[d], unit->path,
                                                  unit->weak, &diag);

                if( prepared < 0 )
                {
                    fprintf(stderr, "%s:%d: %s\n", diag.file, diag.line, diag.message);
                    errors++;
                    break;
                }
                ids[d] = -1;
                if( prepared == 0 )
                    continue;
                ids[d] = id_for(build, unit->decls[d].name);
                if( !SSC_DeclareAt(build->compiler, &unit->decls[d], ids[d], unit->weak) )
                {
                    errors++;
                    break;
                }
            }
            /* A reused unit whose names now sit at other ids (only possible
             * when a seam changed hands) compiles again. */
            if( unit->ids && !errors )
            {
                for( int d = 0; d < unit->decl_count; d++ )
                    moved |= unit->ids[d] != ids[d];
            }
            free(unit->ids);
            unit->ids = ids;
            if( moved )
                unit->dirty = 1;
        }
    }
    if( errors )
    {
        status = 1;
        goto done;
    }
    SSC_DeclareTableSize(build->compiler, build->id_count);
    t_declare = now_seconds() - t_mark;

    /* --- check: ask every recorded question again --- */
    t_mark = now_seconds();
    for( int i = 0; i < build->query_count; i++ )
    {
        struct Query* q = &build->queries[i];

        q->now = query_answer(build, q);
        q->evaluated = 1;
        q->changed = q->now != q->digest;
    }
    for( int u = 0; u < build->unit_count; u++ )
    {
        struct Unit* unit = &build->units[u];

        if( unit->status == UNIT_EDITED || unit->status == UNIT_NEW || options->full ||
            !build->previous.units_valid )
            unit->dirty = 1;
        for( int q = 0; q < unit->query_count; q++ )
        {
            if( build->queries[unit->queries[q]].changed )
            {
                if( unit->why_query < 0 )
                    unit->why_query = (int32_t)unit->queries[q];
                unit->changed_answers++;
                unit->dirty = 1;
            }
        }
    }
    t_check = now_seconds() - t_mark;

    if( options->explain || options->dry_run )
    {
        for( int u = 0; u < build->unit_count; u++ )
        {
            struct Unit* unit = &build->units[u];
            char why[512];

            if( options->explain && !strstr(unit->path, options->explain) &&
                !strstr(unit->rel, options->explain) )
                continue;
            explain_found++;
            if( !unit->dirty )
            {
                printf("fresh     %s — content hash %016llx and all %d recorded answer(s) "
                       "unchanged\n",
                       unit->rel, (unsigned long long)unit->hash, unit->query_count);
                continue;
            }
            if( unit->status == UNIT_EDITED || unit->status == UNIT_NEW )
                snprintf(why, sizeof(why), "%s (size %lld -> %lld)", status_label(unit),
                         (long long)unit->previous_size, (long long)unit->size);
            else if( unit->why_query >= 0 )
                snprintf(why, sizeof(why), "%d of %d recorded answer(s) changed",
                         unit->changed_answers, unit->query_count);
            else if( unit->weak )
                snprintf(why, sizeof(why), "lane seam (always compiled)");
            else
                snprintf(why, sizeof(why), "%s", state_note ? state_note : "configuration");
            printf("stale     %s — %s\n", unit->rel, why);
            if( options->explain && unit->why_query >= 0 )
            {
                for( int q = 0; q < unit->query_count; q++ )
                {
                    const struct Query* query = &build->queries[unit->queries[q]];
                    char described[512];

                    if( !query->changed )
                        continue;
                    describe_query(build, query, described, sizeof(described));
                    printf("            answer changed: %s\n", described);
                }
            }
        }
        if( options->explain && !explain_found )
        {
            int hit = 0;

            for( int i = 0; i < build->previous.input_count; i++ )
            {
                if( strstr(build->previous.inputs[i].rel, options->explain) )
                {
                    printf("input     %s — read by the symbol loader; a change to it recompiles "
                           "only the units whose recorded answers it moves\n",
                           build->previous.inputs[i].rel);
                    hit = 1;
                }
            }
            if( !hit )
            {
                printf("sscompile: %s is no unit or input of %s\n", options->explain, options->out);
                status = 2;
            }
        }
        goto done;
    }

    /* --- compile the dirty units --- */
    t_mark = now_seconds();
    SSC_SetObserver(build->compiler, observe_query, build);
    SSC_SetWarningSink(build->compiler, warning_sink, build);
    for( int u = 0; u < build->unit_count; u++ )
    {
        struct Unit* unit = &build->units[u];
        struct SSC_Diag diag;
        char why[600];

        if( !unit->dirty )
        {
            reused++;
            logf_(build, options->verbose, "reused    %s — %s\n", unit->rel, status_label(unit));
            continue;
        }
        if( unit->status == UNIT_EDITED || unit->status == UNIT_NEW )
            snprintf(why, sizeof(why), "%s", status_label(unit));
        else if( unit->why_query >= 0 )
        {
            char described[512];

            describe_query(build, &build->queries[unit->why_query], described, sizeof(described));
            snprintf(why, sizeof(why), "answer changed: %s%s", described,
                     unit->changed_answers > 1 ? " (and more)" : "");
        }
        else if( unit->weak )
            snprintf(why, sizeof(why), "lane seam");
        else
            snprintf(why, sizeof(why), "%s", state_note ? "full build" : "ids moved");
        if( !compile_unit(build, u, &diag) )
        {
            if( diag.file[0] )
                fprintf(stderr, "%s:%d: %s\n", diag.file, diag.line, diag.message);
            else
                fprintf(stderr, "sscompile: %s: %s\n", unit->path, diag.message);
            errors++;
            continue;
        }
        unit->compiled = 1;
        compiled++;
        /* A rebuild over a fresh state is every unit; one line each would be
         * the whole tree, so only incremental compiles name their units. */
        logf_(build, options->verbose || build->previous.units_valid, "compiled  %s — %s\n",
              unit->rel, why);
    }
    SSC_SetObserver(build->compiler, NULL, NULL);
    SSC_SetWarningSink(build->compiler, NULL, NULL);
    t_compile = now_seconds() - t_mark;
    replay_warnings(build);
    if( errors )
    {
        fprintf(stderr, "sscompile: %d unit(s) failed to compile; the pack in %s is unchanged\n",
                errors, options->out);
        status = 1;
        goto done;
    }

    /* --- write --- */
    t_mark = now_seconds();
    {
        struct Buf dat;
        struct Buf idx;
        struct Buf manifest;
        const struct UnitScript** by_id =
            (const struct UnitScript**)calloc((size_t)build->id_count + 1, sizeof(*by_id));
        int32_t* unit_of = (int32_t*)malloc(((size_t)build->id_count + 1) * sizeof(int32_t));
        int ambiguous = 0;
        int script_total = 0;
        uint64_t dat_hash;
        uint64_t idx_hash;
        int64_t dat_size;
        int64_t idx_size;
        uint32_t* old_rev = (uint32_t*)calloc((size_t)build->id_count + 1, sizeof(uint32_t));
        uint64_t* old_hash = (uint64_t*)calloc((size_t)build->id_count + 1, sizeof(uint64_t));

        assert(by_id);
        assert(unit_of);
        assert(old_rev);
        assert(old_hash);
        /* What each id held last time, for the per-script revision. */
        for( int i = 0; i < build->previous.unit_count; i++ )
        {
            const struct Unit* old = &build->previous.units[i];

            for( int s = 0; s < old->script_count; s++ )
            {
                if( old->scripts[s].id >= 0 && old->scripts[s].id < build->id_count )
                {
                    old_rev[old->scripts[s].id] = old->scripts[s].rev;
                    old_hash[old->scripts[s].id] = old->scripts[s].hash;
                }
            }
        }
        for( int u = 0; u < build->unit_count; u++ )
        {
            const struct Unit* unit = &build->units[u];

            for( int s = 0; s < unit->script_count; s++ )
            {
                if( unit->scripts[s].rev == 0 || unit->compiled )
                {
                    uint32_t rev = old_rev[unit->scripts[s].id];

                    ((struct UnitScript*)&unit->scripts[s])->rev =
                        (rev && old_hash[unit->scripts[s].id] == unit->scripts[s].hash) ? rev
                                                                                         : rev + 1;
                }
            }
        }
        for( int i = 0; i <= build->id_count; i++ )
            unit_of[i] = -1;
        for( int u = 0; u < build->unit_count; u++ )
        {
            struct Unit* unit = &build->units[u];
            int changed = unit->status != UNIT_UNCHANGED && unit->status != UNIT_TOUCHED;

            ambiguous += unit->ambiguous;
            for( int s = 0; s < unit->script_count; s++ )
            {
                by_id[unit->scripts[s].id] = &unit->scripts[s];
                unit_of[unit->scripts[s].id] = u;
                changed |= unit->scripts[s].rev != old_rev[unit->scripts[s].id];
            }
            if( unit->compiled && changed )
                unit->rev++;
            if( !unit->rev )
                unit->rev = 1;
        }
        free(old_rev);
        free(old_hash);

        memset(&dat, 0, sizeof(dat));
        memset(&idx, 0, sizeof(idx));
        put_be32(&dat, (uint32_t)build->id_count);
        put_be32(&dat, SSVM_COMPILER_VERSION); /* bytes 4..6 zero, 7 the version */
        put_be32(&idx, (uint32_t)build->id_count);
        for( int id = 0; id < build->id_count; id++ )
        {
            const struct UnitScript* script = by_id[id];

            if( !script || !script->length )
            {
                put_be32(&idx, 0);
                continue;
            }
            script_total++;
            put_be32(&idx, script->length);
            buf_put(&dat, script->blob, script->length);
        }
        dat_size = (int64_t)dat.length;
        idx_size = (int64_t)idx.length;
        dat_hash = ssc_hash_bytes(dat.data, dat.length, SSC_HASH_SEED);
        idx_hash = ssc_hash_bytes(idx.data, idx.length, SSC_HASH_SEED);
        /* The generation moves when the pack's content does; the files are
         * rewritten whenever what is on disk is not this content — a state file
         * copied into a fresh directory names outputs that are not there. */
        if( dat_hash != build->previous.dat_hash || idx_hash != build->previous.idx_hash ||
            dat_size != build->previous.dat_size || !build->previous.loaded )
            generation++;
        {
            int ok_dat;
            int ok_idx;
            uint64_t on_disk_dat;
            uint64_t on_disk_idx;

            snprintf(path, sizeof(path), "%s/script.dat", options->out);
            on_disk_dat = hash_file(path, &ok_dat);
            snprintf(path, sizeof(path), "%s/script.idx", options->out);
            on_disk_idx = hash_file(path, &ok_idx);
            outputs_changed = !ok_dat || !ok_idx || on_disk_dat != dat_hash ||
                              on_disk_idx != idx_hash;
        }

        /* The manifest: the server's staleness check and a reload's diff. */
        memset(&manifest, 0, sizeof(manifest));
        buf_printf(&manifest, "torirs-script-pack 1\n");
        buf_printf(&manifest, "# Written by sscompile. docs/serverpack.md describes every line.\n");
        buf_printf(&manifest, "config %016llx\n", (unsigned long long)options->config_key);
        buf_printf(&manifest, "generation %u\n", generation);
        buf_printf(&manifest, "lanes %s\n", options->lanes && options->lanes[0] ? options->lanes : "-");
        buf_printf(&manifest, "output script.dat %lld %016llx\n", (long long)dat_size,
                   (unsigned long long)dat_hash);
        buf_printf(&manifest, "output script.idx %lld %016llx\n", (long long)idx_size,
                   (unsigned long long)idx_hash);
        for( int i = 0; i < options->root_count; i++ )
        {
            char* rel = manifest_path(build->content_normal, options->roots[i].dir);

            buf_printf(&manifest, "root %s %s\n", options->roots[i].weak ? "weak" : "strong", rel);
            free(rel);
        }
        for( int i = 0; i < options->exclude_count; i++ )
        {
            char* rel = manifest_path(build->content_normal, options->excludes[i]);

            buf_printf(&manifest, "exclude %s\n", rel);
            free(rel);
        }
        for( int i = 0; i < build->input_count; i++ )
        {
            const struct Input* input = &build->inputs[i];

            if( input->is_dir )
                continue;
            buf_printf(&manifest, "input %lld %lld %016llx %s\n", (long long)input->size,
                       (long long)input->mtime, (unsigned long long)input->hash, input->rel);
        }
        for( int u = 0; u < build->unit_count; u++ )
        {
            const struct Unit* unit = &build->units[u];

            buf_printf(&manifest, "unit %lld %lld %016llx %u %s\n", (long long)unit->size,
                       (long long)unit->mtime, (unsigned long long)unit->hash, unit->rev, unit->rel);
        }
        for( int id = 0; id < build->id_count; id++ )
        {
            const struct UnitScript* script = by_id[id];

            if( script && script->length )
                buf_printf(&manifest, "script %d %016llx %u %d %s\n", id,
                           (unsigned long long)script->hash, script->rev, unit_of[id],
                           build->id_names[id]);
            else if( build->id_names[id] && build->id_names[id][0] && unit_of[id] < 0 )
                buf_printf(&manifest, "retired %d %s\n", id, build->id_names[id]);
        }

        if( outputs_changed )
        {
            snprintf(path, sizeof(path), "%s/script.dat", options->out);
            if( !write_file_atomic(path, dat.data, dat.length) )
                status = 2;
            snprintf(path, sizeof(path), "%s/script.idx", options->out);
            if( !status && !write_file_atomic(path, idx.data, idx.length) )
                status = 2;
        }
        snprintf(path, sizeof(path), "%s/pack.manifest", options->out);
        if( !status && !write_file_atomic(path, manifest.data, manifest.length) )
            status = 2;
        if( !status && !state_write(build, state_path, generation, dat_size, dat_hash, idx_size,
                                    idx_hash) )
            status = 2;
        if( status )
            fprintf(stderr, "sscompile: cannot write the pack into %s\n", options->out);
        free(dat.data);
        free(idx.data);
        free(manifest.data);
        free(by_id);
        free(unit_of);
        t_write = now_seconds() - t_mark;

        if( !status )
        {
            if( changed_input_count )
            {
                logf_(build, 1, "sscompile: %d symbol input(s) changed, e.g. %s\n",
                      changed_input_count, changed_inputs[0]);
            }
            logf_(build, 1,
                  "sscompile: %d compiled, %d reused, %d removed; %d scripts in %d ids (%d new, "
                  "%d retired) -> %s/script.dat%s\n",
                  compiled, reused, removed_units, script_total, build->id_count, build->ids_new,
                  build->id_count - script_total - 0, options->out,
                  outputs_changed ? "" : " (unchanged)");
            if( ambiguous )
                logf_(build, 1, "  %d bare name(s) resolved by namespace sort order alone\n",
                      ambiguous);
            logf_(build, 1,
                  "  phases: walk %.2fs, symbols %.2fs, declare %.2fs, check %.2fs, compile "
                  "%.2fs, write %.2fs; total %.2fs; log %s/pack.log\n",
                  t_walk, t_symbols, t_declare, t_check, t_compile, t_write,
                  now_seconds() - t_start, options->out);
        }
    }
    snprintf(path, sizeof(path), "%s/pack.log", options->out);
    write_file_atomic(path, build->log_text.data, build->log_text.length);
    if( !status )
        touch_outputs(options->out);

done:
    (void)removed_scripts;
    lock_release(&lock);
    if( build->compiler )
        SSC_Free(build->compiler);
    if( build->symbols_loaded )
        SSC_SymbolsFree(&build->symbols);
    for( int u = 0; u < build->unit_count; u++ )
        unit_free(&build->units[u]);
    free(build->units);
    for( int i = 0; i < build->id_count; i++ )
        free(build->id_names[i]);
    free(build->id_names);
    for( int i = 0; i < build->query_count; i++ )
        free(build->queries[i].name);
    free(build->queries);
    for( int i = 0; i < build->input_count; i++ )
    {
        free(build->inputs[i].path);
        free(build->inputs[i].rel);
    }
    free(build->inputs);
    free(build->log_text.data);
    map_free(&build->unit_by_path);
    map_free(&build->id_by_name);
    map_free(&build->query_by_key);
    map_free(&build->input_by_path);
    map_free(&build->previous_input_by_path);
    state_free(&build->previous);
    return status;
}
