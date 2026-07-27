#include "lc_pack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char* const pack_types[LC_PACK_COUNT] = {
    "npc", "seq", "spotanim", "loc", "model", "anim", "animset", "base", "map", "flo", "texture",
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

static int
pack_set(struct LC_Pack* pack, int id, const char* name)
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

static int
pack_load_file(struct LC_Pack* pack, const char* path)
{
    assert(pack && path);
    FILE* f = fopen(path, "rb");
    if( !f )
    {
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
        size_t n = strlen(name);
        while( n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r') )
            name[--n] = '\0';
        if( n == 0 )
            continue;
        /* The engine's loader requires the id to be leading digits (`/^\d+=/`);
         * anything else is a comment or stray line and is skipped there too. */
        char* end = NULL;
        long id = strtol(line, &end, 10);
        if( end == line || *end != '\0' )
            continue;
        if( !pack_set(pack, (int)id, name) )
        {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return 1;
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
        packs->packs[i].type = pack_types[i];
        char path[1024];
        snprintf(path, sizeof(path), "%s/pack/%s.pack", content_dir, pack_types[i]);
        if( !pack_load_file(&packs->packs[i], path) )
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
    {
        struct LC_Pack* pack = &packs->packs[i];
        for( int id = 0; id < pack->capacity; id++ )
            free(pack->names[id]);
        free(pack->names);
    }
    memset(packs, 0, sizeof(*packs));
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
    if( !pack_set(pack, id, name) )
        return -1;
    pack->added++;
    return id;
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
    }
    return 1;
}
