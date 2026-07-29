#include "cachepack.h"

#include "dat2disk.h"
#include "filelist.h"
#include "reference_table.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define cp_mkdir(p) mkdir(p, 0755)
#endif

int
cp_names_load(
    struct CP_Names* names,
    const char* srcdir)
{
    memset(names, 0, sizeof(*names));
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* t = cp_type(i);
        char path[1200];
        snprintf(path, sizeof(path), "%s/pack/%s.pack", srcdir, t->name);
        if( !lc_pack_load(&names->packs[i], path, t->name, 1) )
        {
            fprintf(stderr, "cachepack: failed to read %s\n", path);
            return 0;
        }
    }
    return 1;
}

int
cp_names_save(
    const struct CP_Names* names,
    const char* srcdir)
{
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/pack", srcdir);
    cp_mkdir(dir);
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s.pack", dir, cp_type(i)->name);
        if( !lc_pack_save(&names->packs[i], path) )
        {
            fprintf(stderr, "cachepack: failed to write %s\n", path);
            return 0;
        }
    }
    return 1;
}

void
cp_names_free(struct CP_Names* names)
{
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
        lc_pack_free(&names->packs[i]);
}

/*
 * A gameval name is content-authored and mostly already in the shape a pack file
 * wants, but nothing guarantees it: the format is a free string. A name carrying a
 * space, an `=` or a `]` would produce a pack line or a block header that reads
 * back as something else, so it is normalised here rather than trusted.
 */
static void
sanitise_name(
    const char* in,
    int in_len,
    char* out,
    int out_size)
{
    int w = 0;
    for( int i = 0; i < in_len && w < out_size - 1; i++ )
    {
        unsigned char c = (unsigned char)in[i];
        if( isalnum(c) || c == '_' || c == '.' || c == '+' || c == '-' )
            out[w++] = (char)c;
        else
            out[w++] = '_';
    }
    out[w] = '\0';
}

/**
 * Make `base` unique within `pack` by appending `i2`, `i3`, ... on collision.
 *
 * Duplicate gameval names do occur (the index names two records the same thing),
 * and a pack file that binds one name to two ids loses whichever it reads second.
 * The suffix is the same shape LostCity's unpacker uses for the same problem.
 */
static void
uniquify(
    struct LC_Pack* pack,
    char* name,
    int name_size)
{
    if( lc_pack_find(pack, name) < 0 )
        return;
    char base[240];
    snprintf(base, sizeof(base), "%s", name);
    for( int i = 2; i < 10000; i++ )
    {
        snprintf(name, (size_t)name_size, "%si%d", base, i);
        if( lc_pack_find(pack, name) < 0 )
            return;
    }
}

void
cp_names_seed_from_cache(struct CP_Ctx* ctx)
{
    if( !ctx->cache_open )
        return;
    int gv_table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_GAMEVALS);
    if( gv_table == RSCACHE_DAT2_DISK_TABLE_ABSENT || !ctx->cache.disk->tables[gv_table] )
    {
        fprintf(stderr, "cachepack: no gameval table; names will be <type>_<id>\n");
        return;
    }

    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        const struct CP_Type* type = cp_type(t);
        if( type->gameval_archive < 0 )
            continue;

        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, gv_table, type->gameval_archive);
        if( !archive )
            continue;
        if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) ||
            archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        /*
         * Verify before trusting. The archive-id-to-type mapping is not recorded
         * anywhere in the cache, so the check is that every id this archive names is
         * an id the config group actually holds. A archive naming some other type
         * fails immediately and loudly; a partially-stale one (a handful of ids the
         * group has since dropped) is tolerated, because that is what a live index
         * looks like and refusing it would throw away every good name with it.
         */
        int* ids = NULL;
        int id_count = 0;
        int matched = 0;
        if( cp_record_ids(ctx, t, &ids, &id_count) && id_count > 0 )
        {
            /* ids are ascending; binary search each gameval file id. */
            for( int f = 0; f < files->file_count; f++ )
            {
                int fid = archive->file_ids ? archive->file_ids[f] : f;
                int lo = 0, hi = id_count - 1;
                while( lo <= hi )
                {
                    int mid = (lo + hi) / 2;
                    if( ids[mid] == fid )
                    {
                        matched++;
                        break;
                    }
                    if( ids[mid] < fid )
                        lo = mid + 1;
                    else
                        hi = mid - 1;
                }
            }
        }

        int total = files->file_count;
        if( total == 0 || matched * 10 < total * 9 )
        {
            fprintf(
                stderr,
                "cachepack: gameval archive %d does not name %s (%d/%d ids match) — "
                "using %s_<id>\n",
                type->gameval_archive,
                type->name,
                matched,
                total,
                type->name);
            free(ids);
            RSCache_FileListFree(files);
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        for( int f = 0; f < files->file_count; f++ )
        {
            int fid = archive->file_ids ? archive->file_ids[f] : f;
            if( files->file_sizes[f] <= 0 )
                continue;
            char name[256];
            sanitise_name(files->files[f], files->file_sizes[f], name, sizeof(name));
            if( !name[0] )
                continue;
            uniquify(&ctx->names.packs[t], name, sizeof(name));
            lc_pack_set(&ctx->names.packs[t], fid, name);
        }
        ctx->names.from_gameval[t] = true;

        free(ids);
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }
}

const char*
cp_name_get(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id)
{
    struct LC_Pack* pack = &ctx->names.packs[type];
    if( id < 0 || id >= pack->capacity || !pack->names )
        return NULL;
    return pack->names[id];
}

const char*
cp_name_ensure(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id)
{
    const char* existing = cp_name_get(ctx, type, id);
    if( existing )
        return existing;
    char name[256];
    snprintf(name, sizeof(name), "%s_%d", cp_type(type)->name, id);
    uniquify(&ctx->names.packs[type], name, sizeof(name));
    lc_pack_set(&ctx->names.packs[type], id, name);
    return cp_name_get(ctx, type, id);
}

int
cp_name_find(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* name)
{
    return lc_pack_find(&ctx->names.packs[type], name);
}
