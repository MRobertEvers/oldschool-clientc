#include "lc_pack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char* const pack_types[LC_PACK_COUNT] = {
    "npc", "obj", "seq", "spotanim", "loc", "model", "anim", "animset", "base", "map", "flo", "texture",
};

static char*
sdup(const char* s)
{
    size_t n = strlen(s);
    char* d = malloc(n + 1);
    if( d )
        memcpy(d, s, n + 1);
    return d;
}

static int
pack_grow(struct LC_Pack* pack, int needed)
{
    assert(pack);
    if( needed <= pack->capacity )
        return 1;
    int cap = pack->capacity == 0 ? 1024 : pack->capacity;
    while( cap < needed )
        cap *= 2;
    char** names = realloc(pack->names, (size_t)cap * sizeof(char*));
    if( !names )
        return 0;
    memset(names + pack->capacity, 0, (size_t)(cap - pack->capacity) * sizeof(char*));
    pack->names = names;
    pack->capacity = cap;
    return 1;
}

int
lc_pack_set(struct LC_Pack* pack, int id, const char* name)
{
    assert(pack && name);
    if( id < 0 )
        return 0;
    if( !pack_grow(pack, id + 1) )
        return 0;
    free(pack->names[id]);
    pack->names[id] = sdup(name);
    if( !pack->names[id] )
        return 0;
    if( id + 1 > pack->max )
        pack->max = id + 1;
    return 1;
}

int
lc_pack_remove(struct LC_Pack* pack, int id)
{
    assert(pack);
    if( id < 0 || id >= pack->capacity || !pack->names[id] )
        return 0;
    free(pack->names[id]);
    pack->names[id] = NULL;
    /* The engine recomputes max from the file it reads, so dropping the top id
     * has to lower it here too — otherwise the next allocation lands past the
     * end of what gets written and leaves an id no line accounts for. */
    if( id + 1 == pack->max )
    {
        int max = 0;
        for( int i = id - 1; i >= 0; i-- )
        {
            if( pack->names[i] )
            {
                max = i + 1;
                break;
            }
        }
        pack->max = max;
    }
    pack->removed++;
    return 1;
}

int
lc_pack_load(
    struct LC_Pack* pack,
    const char* path,
    const char* type,
    int allow_missing)
{
    assert(pack && path && type);
    memset(pack, 0, sizeof(*pack));
    snprintf(pack->type, sizeof(pack->type), "%s", type);

    FILE* f = fopen(path, "rb");
    if( !f )
    {
        if( allow_missing )
            return 1;
        fprintf(stderr, "pack: cannot open %s\n", path);
        return 0;
    }

    char line[512];
    while( fgets(line, sizeof(line), f) )
    {
        char* eq = strchr(line, '=');
        if( !eq )
            continue;
        *eq = '\0';
        char* name = eq + 1;
        /* A trailing `//` comment. Layer-1 packs use one to carry the layer-0
         * name an alias stands in for (`3254=guard  // cache: guard1`), so a
         * reader can tell an alias from an unexamined claim about an id. Cut it
         * before the name is measured. */
        char* trailing = strstr(name, "//");
        if( trailing )
            *trailing = '\0';
        size_t n = strlen(name);
        while( n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r' || name[n - 1] == ' ' ||
                         name[n - 1] == '\t') )
            name[--n] = '\0';
        if( n == 0 )
            continue;
        /* The engine's loader requires the id to be leading digits (`/^\d+=/`);
         * anything else is a comment or stray line and is skipped there too. */
        char* end = NULL;
        long id = strtol(line, &end, 10);
        if( end == line || *end != '\0' )
            continue;
        if( !lc_pack_set(pack, (int)id, name) )
        {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return 1;
}

int
lc_pack_save(
    const struct LC_Pack* pack,
    const char* path)
{
    assert(pack && path);
    FILE* f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "pack: cannot write %s\n", path);
        return 0;
    }
    for( int id = 0; id < pack->capacity; id++ )
    {
        if( pack->names[id] )
            fprintf(f, "%d=%s\n", id, pack->names[id]);
    }
    fclose(f);
    return 1;
}

int
lc_pack_synthetic_id(
    const char* type,
    const char* name)
{
    assert(type && name);
    size_t type_len = strlen(type);

    if( strncmp(name, type, type_len) != 0 || name[type_len] != '_' )
        return -1;

    const char* digits = name + type_len + 1;
    if( !*digits )
        return -1;
    /* Leading zeros would make two spellings of one id, and nothing writes
     * them, so `param_007` is not param 7 — it is a name that happens to look
     * like one, and treating it as an id would bind it silently. */
    if( digits[0] == '0' && digits[1] )
        return -1;
    for( const char* c = digits; *c; c++ )
    {
        if( *c < '0' || *c > '9' )
            return -1;
    }

    long id = strtol(digits, NULL, 10);
    if( id < 0 || id > 0x7fffffff )
        return -1;
    return (int)id;
}

int
lc_pack_save_sparse(
    const struct LC_Pack* pack,
    const char* path)
{
    assert(pack && path);
    int real = 0;

    for( int id = 0; id < pack->capacity; id++ )
    {
        if( pack->names[id] && lc_pack_synthetic_id(pack->type, pack->names[id]) != id )
            real++;
    }

    if( real == 0 )
    {
        /* Not an empty namespace — a namespace the cache does not name. Leaving
         * a file of pure filler behind would say the opposite. */
        remove(path);
        return 1;
    }

    FILE* f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "pack: cannot write %s\n", path);
        return 0;
    }
    for( int id = 0; id < pack->capacity; id++ )
    {
        if( !pack->names[id] )
            continue;
        if( lc_pack_synthetic_id(pack->type, pack->names[id]) == id )
            continue;
        fprintf(f, "%d=%s\n", id, pack->names[id]);
    }
    fclose(f);
    return 1;
}

void
lc_pack_free(struct LC_Pack* pack)
{
    if( !pack )
        return;
    for( int id = 0; id < pack->capacity; id++ )
        free(pack->names[id]);
    free(pack->names);
    memset(pack, 0, sizeof(*pack));
}

int
lc_pack_find(
    const struct LC_Pack* pack,
    const char* name)
{
    assert(pack && name);
    for( int id = 0; id < pack->capacity; id++ )
    {
        if( pack->names[id] && strcmp(pack->names[id], name) == 0 )
            return id;
    }
    return -1;
}

int
lc_pack_alloc(
    struct LC_Pack* pack,
    const char* name)
{
    assert(pack && name);
    int existing = lc_pack_find(pack, name);
    if( existing >= 0 )
        return existing;
    int id = pack->max;
    if( !lc_pack_set(pack, id, name) )
        return -1;
    pack->added++;
    return id;
}

int
lc_packs_load(
    struct LC_Packs* packs,
    const char* content_dir)
{
    assert(packs && content_dir);
    memset(packs, 0, sizeof(*packs));
    for( int i = 0; i < LC_PACK_COUNT; i++ )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/pack/%s.pack", content_dir, pack_types[i]);
        if( !lc_pack_load(&packs->packs[i], path, pack_types[i], 0) )
            return 0;
    }
    return 1;
}

void
lc_packs_free(struct LC_Packs* packs)
{
    if( !packs )
        return;
    for( int i = 0; i < LC_PACK_COUNT; i++ )
        lc_pack_free(&packs->packs[i]);
    memset(packs, 0, sizeof(*packs));
}

int
lc_packs_write(
    const struct LC_Packs* packs,
    const char* content_dir)
{
    assert(packs && content_dir);
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/pack", content_dir);
    mkdir(dir, 0755);

    for( int i = 0; i < LC_PACK_COUNT; i++ )
    {
        const struct LC_Pack* pack = &packs->packs[i];
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.pack", dir, pack->type);
        if( !lc_pack_save(pack, path) )
            return 0;
    }
    return 1;
}
