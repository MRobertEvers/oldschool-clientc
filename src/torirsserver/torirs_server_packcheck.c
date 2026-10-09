#include "torirs_server_packcheck.h"

#include "rscache_serverpack.h"
#include "ssc_hash.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

enum
{
    PACKCHECK_SHOWN = 20,
};

/* The builders' mtime, to the nanosecond where the platform has it. */
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

struct Entry
{
    const char* rel;
    long long size;
    long long mtime;
    unsigned long long hash;
};

struct Check
{
    const char* label;
    const char* content_dir;
    struct Entry* entries;
    int count;
    int capacity;
    /* rel -> entry index, for "is this file one the build saw?" */
    int32_t* slots;
    uint32_t slot_capacity;
    int stale;
    int server_pack;
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
known(const struct Check* check, const char* rel)
{
    uint32_t slot = rel_hash(rel) & (check->slot_capacity - 1);

    while( check->slots[slot] >= 0 )
    {
        if( strcmp(check->entries[check->slots[slot]].rel, rel) == 0 )
            return 1;
        slot = (slot + 1) & (check->slot_capacity - 1);
    }
    return 0;
}

/** Which unit of the server pack a file feeds, for the report. */
static void
server_unit_of(const char* rel, char* out, size_t capacity)
{
    const char* base = strrchr(rel, '/');
    const char* dot;

    base = base ? base + 1 : rel;
    if( strncmp(base, "all.", 4) == 0 )
    {
        const char* end = strchr(base + 4, '.');

        snprintf(out, capacity, "%.*s%s", (int)(end ? end - (base + 4) : (long)strlen(base + 4)),
                 base + 4, end ? " (names)" : "");
        return;
    }
    dot = strrchr(base, '.');
    if( dot && strcmp(dot, ".constant") == 0 )
        snprintf(out, capacity, "the types that read it");
    else if( dot && (strcmp(dot, ".alloc") == 0 || strcmp(dot, ".pack") == 0) )
        snprintf(out, capacity, "names");
    else if( dot && (strcmp(dot, ".client") == 0 || strcmp(dot, ".server") == 0) )
        snprintf(out, capacity, "%.*s (routing)", (int)(dot - base), base);
    else if( dot && strcmp(dot, ".compack") != 0 && strncmp(rel, "fields/", 7) != 0 &&
             strcmp(base, "meta.ini") != 0 && strcmp(base, "content.ini") != 0 )
        snprintf(out, capacity, "%s", dot + 1);
    else
        snprintf(out, capacity, "every type");
}

static void
report(struct Check* check, const char* what, const char* rel)
{
    if( check->stale == 0 )
    {
        fprintf(stderr,
                "torirsserver: ------------------------------------------------------------\n"
                "torirsserver: the %s was built before these change(s) to its sources:\n",
                check->label);
    }
    check->stale++;
    if( check->stale > PACKCHECK_SHOWN )
        return;
    if( check->server_pack )
    {
        char unit[128];

        server_unit_of(rel, unit, sizeof(unit));
        fprintf(stderr, "torirsserver:   %-8s %s  [%s]\n", what, rel, unit);
    }
    else
    {
        fprintf(stderr, "torirsserver:   %-8s %s\n", what, rel);
    }
}

static void
check_entry(struct Check* check, const struct Entry* entry)
{
    char path[2048];
    struct stat info;

    if( entry->rel[0] == '/' )
        snprintf(path, sizeof(path), "%s", entry->rel);
    else
        snprintf(path, sizeof(path), "%s/%s", check->content_dir, entry->rel);
    if( stat(path, &info) != 0 )
    {
        report(check, "removed", entry->rel);
        return;
    }
    if( (long long)info.st_size == entry->size && mtime_ns(&info) == entry->mtime )
        return;
    {
        size_t length;
        char* data = read_whole(path, &length);
        uint64_t hash = data ? ssc_hash_bytes(data, length, SSC_HASH_SEED) : 0;

        free(data);
        /* Touched, not changed: the same bytes the build compiled. */
        if( data && hash == entry->hash )
            return;
    }
    report(check, "edited", entry->rel);
}

static int
is_script_source(const char* name)
{
    const char* dot = strrchr(name, '.');

    return dot && (strcmp(dot, ".rs2") == 0 || strcmp(dot, ".constant") == 0 ||
                   strcmp(dot, ".dbtable") == 0 || strcmp(dot, ".varp") == 0);
}

static int
excluded(const char* rel, char* const* excludes, int exclude_count)
{
    for( int i = 0; i < exclude_count; i++ )
    {
        size_t length = strlen(excludes[i]);

        if( strncmp(rel, excludes[i], length) == 0 && (rel[length] == '/' || rel[length] == '\0') )
            return 1;
    }
    return 0;
}

/* Sources under a script root the build never saw. d_type, so the selftest
 * screenshots under server/scripts cost a readdir and nothing else. */
static void
walk_new_sources(
    struct Check* check,
    const char* rel_dir,
    char* const* excludes,
    int exclude_count)
{
    char dir[2048];
    DIR* handle;
    struct dirent* entry;

    if( excluded(rel_dir, excludes, exclude_count) )
        return;
    snprintf(dir, sizeof(dir), "%s/%s", check->content_dir, rel_dir);
    handle = opendir(dir);
    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char rel[2048];
        int is_dir = -1;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(rel, sizeof(rel), "%s/%s", rel_dir, entry->d_name);
#ifdef DT_DIR
        if( entry->d_type == DT_DIR )
            is_dir = 1;
        else if( entry->d_type == DT_REG )
            is_dir = 0;
#endif
        if( is_dir < 0 )
        {
            char path[2400];
            struct stat info;

            snprintf(path, sizeof(path), "%s/%s", check->content_dir, rel);
            if( stat(path, &info) != 0 )
                continue;
            is_dir = (info.st_mode & S_IFDIR) != 0;
        }
        if( is_dir )
            walk_new_sources(check, rel, excludes, exclude_count);
        else if( is_script_source(entry->d_name) && !known(check, rel) )
            report(check, "new", rel);
    }
    closedir(handle);
}

int
ToriRSServer_PackCheck(
    const char* label,
    const char* pack_dir,
    const char* content_dir)
{
    char path[1200];
    size_t length;
    char* text;
    char* line;
    char* roots[64];
    int root_count = 0;
    char* excludes[128];
    int exclude_count = 0;
    char* lanes[16];
    int lane_count = 0;
    struct Check check;

    assert(label);
    assert(pack_dir);
    assert(content_dir);
    snprintf(path, sizeof(path), "%s/pack.manifest", pack_dir);
    text = read_whole(path, &length);
    if( !text )
        return TORIRSSERVER_PACKCHECK_NO_MANIFEST;

    memset(&check, 0, sizeof(check));
    check.label = label;
    check.content_dir = content_dir;
    check.server_pack = strncmp(text, "torirs-server-pack ", 19) == 0;
    for( line = strtok(text, "\n"); line; line = strtok(NULL, "\n") )
    {
        struct Entry entry;
        char* rel = NULL;
        int fields = 0;

        if( strncmp(line, "root ", 5) == 0 && root_count < 64 )
        {
            char* space = strchr(line + 5, ' ');

            if( space )
                roots[root_count++] = space + 1;
            continue;
        }
        if( strncmp(line, "exclude ", 8) == 0 && exclude_count < 128 )
        {
            excludes[exclude_count++] = line + 8;
            continue;
        }
        if( strncmp(line, "lanes ", 6) == 0 )
        {
            for( char* word = line + 6; *word && lane_count < 16; )
            {
                char* end = strchr(word, ' ');

                if( end )
                    *end = '\0';
                if( strcmp(word, "-") != 0 )
                    lanes[lane_count++] = word;
                if( !end )
                    break;
                word = end + 1;
            }
            continue;
        }
        /* `input <size> <mtime> <hash> <rel>` in both manifests, and the script
         * pack's `unit` line, which adds a revision before the path. */
        memset(&entry, 0, sizeof(entry));
        if( strncmp(line, "input ", 6) == 0 )
        {
            int at = 0;

            fields = sscanf(line + 6, "%lld %lld %llx %n", &entry.size, &entry.mtime, &entry.hash,
                            &at);
            rel = fields == 3 ? line + 6 + at : NULL;
        }
        else if( !check.server_pack && strncmp(line, "unit ", 5) == 0 )
        {
            unsigned rev;
            int at = 0;

            fields = sscanf(line + 5, "%lld %lld %llx %u %n", &entry.size, &entry.mtime,
                            &entry.hash, &rev, &at);
            rel = fields == 4 ? line + 5 + at : NULL;
        }
        if( !rel || !*rel )
            continue;
        entry.rel = rel;
        if( check.count == check.capacity )
        {
            check.capacity = check.capacity ? check.capacity * 2 : 4096;
            check.entries =
                (struct Entry*)realloc(check.entries, (size_t)check.capacity * sizeof(*check.entries));
            assert(check.entries);
        }
        check.entries[check.count++] = entry;
    }

    check.slot_capacity = 1024;
    while( check.slot_capacity < (uint32_t)check.count * 2 + 16 )
        check.slot_capacity *= 2;
    check.slots = (int32_t*)malloc(check.slot_capacity * sizeof(int32_t));
    assert(check.slots);
    memset(check.slots, -1, check.slot_capacity * sizeof(int32_t));
    for( int i = 0; i < check.count; i++ )
    {
        uint32_t slot = rel_hash(check.entries[i].rel) & (check.slot_capacity - 1);

        while( check.slots[slot] >= 0 )
            slot = (slot + 1) & (check.slot_capacity - 1);
        check.slots[slot] = i;
    }

    for( int i = 0; i < check.count; i++ )
        check_entry(&check, &check.entries[i]);
    if( check.server_pack )
    {
        char** inputs = NULL;
        int input_count = RSCache_ServerPackInputs(content_dir, (const char* const*)lanes, lane_count,
                                                   &inputs);

        for( int i = 0; i < input_count; i++ )
        {
            if( !known(&check, inputs[i]) )
                report(&check, "new", inputs[i]);
            free(inputs[i]);
        }
        free(inputs);
    }
    else
    {
        /* The compiler's rule (SSC_CompileRoots): every other root is an
         * exclusion too, and an exclusion never cancels the root it is. */
        for( int i = 0; i < root_count; i++ )
        {
            char* kept[192];
            int kept_count = 0;

            for( int j = 0; j < exclude_count + root_count; j++ )
            {
                char* exclusion = j < exclude_count ? excludes[j] : roots[j - exclude_count];

                if( excluded(roots[i], &exclusion, 1) )
                    continue;
                assert(kept_count < (int)(sizeof(kept) / sizeof(kept[0])));
                kept[kept_count++] = exclusion;
            }
            walk_new_sources(&check, roots[i], kept, kept_count);
        }
    }

    if( check.stale )
    {
        if( check.stale > PACKCHECK_SHOWN )
            fprintf(stderr, "torirsserver:   ... and %d more\n", check.stale - PACKCHECK_SHOWN);
        fprintf(stderr,
                "torirsserver: %d stale; the pack at %s runs as it was built.\n"
                "torirsserver: Rebuild (incremental, seconds): make -C src torirsserver-packs\n"
                "torirsserver: Why is a file stale?  sscompile ... --explain <file>\n"
                "torirsserver: TORIRSSERVER_STALE=refuse makes this a refusal.\n"
                "torirsserver: ------------------------------------------------------------\n",
                check.stale, pack_dir);
    }
    free(check.slots);
    free(check.entries);
    free(text);
    return check.stale;
}

int
ToriRSServer_PackStaleRefuses(void)
{
    const char* policy = getenv("TORIRSSERVER_STALE");

    return policy && strcmp(policy, "refuse") == 0;
}

long
ToriRSServer_PackLockShared(const char* pack_dir)
{
    char path[1200];

    assert(pack_dir);
    snprintf(path, sizeof(path), "%s/.pack.lock", pack_dir);
#ifdef _WIN32
    {
        HANDLE handle = CreateFileA(path, GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        OVERLAPPED overlapped;

        if( handle == INVALID_HANDLE_VALUE )
            return -1;
        memset(&overlapped, 0, sizeof(overlapped));
        if( !LockFileEx(handle, 0, 0, 1, 0, &overlapped) )
        {
            CloseHandle(handle);
            return -1;
        }
        return (long)(intptr_t)handle;
    }
#else
    {
        int fd = open(path, O_RDONLY);

        if( fd < 0 )
            return -1;
        while( flock(fd, LOCK_SH) != 0 )
        {
            if( errno != EINTR )
            {
                close(fd);
                return -1;
            }
        }
        return fd;
    }
#endif
}

void
ToriRSServer_PackUnlock(long handle)
{
    if( handle < 0 )
        return;
#ifdef _WIN32
    CloseHandle((HANDLE)(intptr_t)handle);
#else
    close((int)handle);
#endif
}
