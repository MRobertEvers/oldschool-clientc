#include "ssvm_provider.h"
#include <assert.h>

#include "ss_meta.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static uint64_t
hash_name(const char* s)
{
    /* FNV-1a. The index binary-searches on this and then confirms with strcmp,
     * so a collision costs a comparison rather than a wrong answer. */
    uint64_t h = 1469598103934665603ull;

    while( *s )
    {
        h ^= (uint64_t)(unsigned char)*s++;
        h *= 1099511628211ull;
    }
    return h;
}

static int
cmp_name_entry(const void* a, const void* b)
{
    const struct SSVM_NameEntry* x = (const struct SSVM_NameEntry*)a;
    const struct SSVM_NameEntry* y = (const struct SSVM_NameEntry*)b;

    if( x->hash < y->hash )
        return -1;
    if( x->hash > y->hash )
        return 1;
    return (x->id > y->id) - (x->id < y->id);
}

static int
cmp_key_entry(const void* a, const void* b)
{
    const struct SSVM_KeyEntry* x = (const struct SSVM_KeyEntry*)a;
    const struct SSVM_KeyEntry* y = (const struct SSVM_KeyEntry*)b;

    if( x->key < y->key )
        return -1;
    if( x->key > y->key )
        return 1;
    return (x->id > y->id) - (x->id < y->id);
}

/* rsab_wrap takes a mutable pointer because one struct serves both reads and
 * writes. Nothing in this file writes through the buffer, so the cast is safe;
 * keeping it in one place keeps that claim checkable. */
static void
wrap_readonly(struct RSAreaBuf* buf, const uint8_t* data, size_t length)
{
    union
    {
        const uint8_t* readable;
        void* writable;
    } alias;

    alias.readable = data;
    rsab_wrap(buf, alias.writable, length);
}

static uint8_t*
read_whole_file(const char* path, size_t* out_length)
{
    FILE* f;
    long size;
    uint8_t* data;

    *out_length = 0;

    f = fopen(path, "rb");
    if( !f )
        return NULL;

    if( fseek(f, 0, SEEK_END) != 0 )
    {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if( size < 0 )
    {
        fclose(f);
        return NULL;
    }
    rewind(f);

    data = (uint8_t*)malloc((size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_length = (size_t)size;
    return data;
}

/* ------------------------------------------------------------------ */
/* Load                                                                */
/* ------------------------------------------------------------------ */

static int
build_indices(struct SSVM_Provider* provider, struct SSVM_Error* err)
{
    int32_t i;

    if( provider->loaded == 0 )
        return 1;

    provider->by_name =
        (struct SSVM_NameEntry*)calloc((size_t)provider->loaded, sizeof(*provider->by_name));
    provider->by_key =
        (struct SSVM_KeyEntry*)calloc((size_t)provider->loaded, sizeof(*provider->by_key));
    if( !provider->by_name || !provider->by_key )
        return SSVM_ErrorSet(err, -1, -1, "out of memory building script indices");

    for( i = 0; i < provider->count; i++ )
    {
        const struct SSVM_Script* script = &provider->scripts[i];

        if( script->op_count == 0 )
            continue;

        if( script->name )
        {
            struct SSVM_NameEntry* e = &provider->by_name[provider->by_name_count++];
            e->hash = hash_name(script->name);
            e->id = script->id;
        }

        /* Name-addressed scripts carry -1 and must stay out of the trigger
         * index. The reference tests `lookupKey !== 0xffffffff` against a value
         * it read signed, which is always true, so all ~4,000 of them land
         * under key -1 and clobber each other — and any trigger miss can then
         * return whichever one happened to be last. Skipping them is both
         * correct and what the reference meant. */
        if( script->lookup_key >= 0 )
        {
            struct SSVM_KeyEntry* e = &provider->by_key[provider->by_key_count++];

            /* Zero-extended, not re-packed. The on-disk field is an i32 and
             * SSVM_LookupKey's layout puts the subject at bit 10, so a stored
             * key can only ever describe a subject below 2^21 — anything larger
             * cannot be compiled in the first place. Keeping the index 64-bit
             * therefore does not recover information the format lost; what it
             * buys is that a *lookup* computed from an oversized subject (a
             * packed rev-230 component uid, say) lands on no entry at all
             * instead of aliasing onto some unrelated script's key. Finding
             * nothing is debuggable; finding the wrong dialogue is not. */
            e->key = (uint64_t)(uint32_t)script->lookup_key;
            e->id = script->id;
        }
    }

    qsort(provider->by_name, (size_t)provider->by_name_count, sizeof(*provider->by_name),
          cmp_name_entry);
    qsort(provider->by_key, (size_t)provider->by_key_count, sizeof(*provider->by_key),
          cmp_key_entry);

    for( i = 1; i < provider->by_key_count; i++ )
    {
        if( provider->by_key[i].key == provider->by_key[i - 1].key )
            provider->duplicate_keys++;
    }
    return 1;
}

int
SSVM_ProviderLoadMem(
    struct SSVM_Provider* provider,
    const uint8_t* dat,
    size_t dat_length,
    const uint8_t* idx,
    size_t idx_length,
    struct SSVM_Error* err)
{
    struct RSAreaBuf dat_buf;
    struct RSAreaBuf idx_buf;
    int32_t dat_entries;
    int32_t idx_entries;
    int32_t i;
    size_t offset;

    assert(provider);

    memset(provider, 0, sizeof(*provider));

    if( !dat || !idx || dat_length < 8 || idx_length < 4 )
        return SSVM_ErrorSet(
            err, -1, -1, "script container is truncated (dat %zu, idx %zu bytes)",
            dat_length, idx_length);

    wrap_readonly(&dat_buf, dat, dat_length);
    wrap_readonly(&idx_buf, idx, idx_length);

    dat_entries = rsab_g4(&dat_buf);
    provider->compiler_version = rsab_g4(&dat_buf);
    idx_entries = rsab_g4(&idx_buf);

    if( provider->compiler_version != SSVM_COMPILER_VERSION )
        return SSVM_ErrorSet(
            err, -1, 4, "scripts were compiled by version %d, this reader speaks %d",
            provider->compiler_version, SSVM_COMPILER_VERSION);

    if( dat_entries != idx_entries )
        return SSVM_ErrorSet(
            err, -1, -1, "dat declares %d entries, idx declares %d", dat_entries,
            idx_entries);

    if( dat_entries < 0 )
        return SSVM_ErrorSet(err, -1, 0, "negative entry count %d", dat_entries);

    /* The .idx is a fixed-stride table, so its length pins the entry count
     * exactly. Checking it here means a mismatched pair fails immediately
     * rather than as a confusing decode error hundreds of scripts in. */
    if( idx_length != 4 + ((size_t)dat_entries * 4) )
        return SSVM_ErrorSet(
            err, -1, -1, "idx is %zu bytes, %d entries need %zu", idx_length,
            dat_entries, 4 + ((size_t)dat_entries * 4));

    provider->count = dat_entries;
    if( dat_entries > 0 )
    {
        provider->scripts =
            (struct SSVM_Script*)calloc((size_t)dat_entries, sizeof(struct SSVM_Script));
        if( !provider->scripts )
            return SSVM_ErrorSet(err, -1, -1, "out of memory for %d scripts", dat_entries);
    }

    offset = dat_buf.pos;
    for( i = 0; i < dat_entries; i++ )
    {
        int32_t size = rsab_g4(&idx_buf);

        provider->scripts[i].id = i;
        provider->scripts[i].lookup_key = -1;

        if( size == 0 )
            continue; /* no script compiled at this id */

        if( size < 0 || offset + (size_t)size > dat_length )
        {
            SSVM_ProviderFree(provider);
            return SSVM_ErrorSet(
                err, i, (long)offset, "size %d at offset %zu runs past the %zu byte dat",
                size, offset, dat_length);
        }

        if( !SSVM_ScriptDecode(i, dat + offset, (size_t)size, &provider->scripts[i], err) )
        {
            SSVM_ProviderFree(provider);
            return 0;
        }

        offset += (size_t)size;
        provider->loaded++;
    }

    /* The bodies are concatenated with no offsets of their own, so one wrong
     * size shifts every script after it — and each of those still decodes into
     * something plausible. Ending exactly at the end of the file is the only
     * evidence that never happened. */
    if( offset != dat_length )
    {
        SSVM_ProviderFree(provider);
        return SSVM_ErrorSet(
            err, -1, (long)offset,
            "script bodies consumed %zu of %zu bytes; the idx sizes do not match the dat",
            offset, dat_length);
    }

    if( !build_indices(provider, err) )
    {
        SSVM_ProviderFree(provider);
        return 0;
    }
    return 1;
}

int
SSVM_ProviderLoadDir(
    struct SSVM_Provider* provider,
    const char* dir,
    struct SSVM_Error* err)
{
    char dat_path[1024];
    char idx_path[1024];
    uint8_t* dat = NULL;
    uint8_t* idx = NULL;
    size_t dat_length = 0;
    size_t idx_length = 0;
    int ok;

    assert(provider);
    memset(provider, 0, sizeof(*provider));

    if( !dir )
        return SSVM_ErrorSet(err, -1, -1, "no script directory");

    snprintf(dat_path, sizeof(dat_path), "%s/script.dat", dir);
    snprintf(idx_path, sizeof(idx_path), "%s/script.idx", dir);

    dat = read_whole_file(dat_path, &dat_length);
    if( !dat )
        return SSVM_ErrorSet(err, -1, -1, "cannot read %s", dat_path);

    idx = read_whole_file(idx_path, &idx_length);
    if( !idx )
    {
        free(dat);
        return SSVM_ErrorSet(err, -1, -1, "cannot read %s", idx_path);
    }

    ok = SSVM_ProviderLoadMem(provider, dat, dat_length, idx, idx_length, err);

    free(dat);
    free(idx);
    return ok;
}

void
SSVM_ProviderFree(struct SSVM_Provider* provider)
{
    int32_t i;

    if( !provider )
        return;

    if( provider->scripts )
    {
        for( i = 0; i < provider->count; i++ )
            SSVM_ScriptFree(&provider->scripts[i]);
    }
    free(provider->scripts);
    free(provider->by_name);
    free(provider->by_key);
    memset(provider, 0, sizeof(*provider));
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

const struct SSVM_Script*
SSVM_ProviderGet(
    const struct SSVM_Provider* provider,
    int32_t id)
{
    assert(provider);
    if( !provider->scripts || id < 0 || id >= provider->count )
        return NULL;
    if( provider->scripts[id].op_count == 0 )
        return NULL;
    return &provider->scripts[id];
}

const struct SSVM_Script*
SSVM_ProviderGetByName(
    const struct SSVM_Provider* provider,
    const char* name)
{
    uint64_t hash;
    int32_t lo;
    int32_t hi;

    assert(provider);
    if( !provider->by_name )
        return NULL;
    assert(name);

    hash = hash_name(name);

    lo = 0;
    hi = provider->by_name_count - 1;
    while( lo <= hi )
    {
        int32_t mid = lo + ((hi - lo) / 2);

        if( provider->by_name[mid].hash < hash )
        {
            lo = mid + 1;
        }
        else if( provider->by_name[mid].hash > hash )
        {
            hi = mid - 1;
        }
        else
        {
            /* Walk back to the first entry with this hash, then compare names.
             * Two distinct names can hash alike; the strcmp is what makes the
             * index exact rather than probabilistic. */
            int32_t i = mid;

            while( i > 0 && provider->by_name[i - 1].hash == hash )
                i--;
            for( ; i < provider->by_name_count && provider->by_name[i].hash == hash; i++ )
            {
                const struct SSVM_Script* s = &provider->scripts[provider->by_name[i].id];

                if( s->name && strcmp(s->name, name) == 0 )
                    return s;
            }
            return NULL;
        }
    }
    return NULL;
}

static const struct SSVM_Script*
find_by_key(
    const struct SSVM_Provider* provider,
    uint64_t key)
{
    int32_t lo = 0;
    int32_t hi;

    assert(provider);
    if( !provider->by_key )
        return NULL;

    hi = provider->by_key_count - 1;
    while( lo <= hi )
    {
        int32_t mid = lo + ((hi - lo) / 2);

        if( provider->by_key[mid].key < key )
            lo = mid + 1;
        else if( provider->by_key[mid].key > key )
            hi = mid - 1;
        else
        {
            /* Duplicates are a content error (the same trigger+subject declared
             * twice). Return the lowest id so the choice is at least stable. */
            while( mid > 0 && provider->by_key[mid - 1].key == key )
                mid--;
            return &provider->scripts[provider->by_key[mid].id];
        }
    }
    return NULL;
}

const struct SSVM_Script*
SSVM_ProviderGetByTrigger(
    const struct SSVM_Provider* provider,
    int trigger,
    int32_t type,
    int32_t category)
{
    const struct SSVM_Script* script;

    script = find_by_key(provider, SSVM_LookupKey(trigger, SS_LOOKUP_TYPE, type));
    if( script )
        return script;

    script = find_by_key(provider, SSVM_LookupKey(trigger, SS_LOOKUP_CATEGORY, category));
    if( script )
        return script;

    return find_by_key(provider, SSVM_LookupKey(trigger, SS_LOOKUP_GLOBAL, 0));
}

const struct SSVM_Script*
SSVM_ProviderGetByTriggerSpecific(
    const struct SSVM_Provider* provider,
    int trigger,
    int32_t type,
    int32_t category)
{
    if( type != -1 )
        return find_by_key(provider, SSVM_LookupKey(trigger, SS_LOOKUP_TYPE, type));
    if( category != -1 )
        return find_by_key(provider, SSVM_LookupKey(trigger, SS_LOOKUP_CATEGORY, category));
    return find_by_key(provider, SSVM_LookupKey(trigger, SS_LOOKUP_GLOBAL, 0));
}
