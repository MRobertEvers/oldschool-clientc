#include "cachepack.h"

#include "cp_assets.h"
#include "cp_register.h"

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

    /* What the tree says about who owns each namespace. Consulted only when
     * *writing*; loading is unconditional. */
    cp_register_load(srcdir);

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

    /*
     * Layer 1, into a pack of its own.
     *
     * Kept separate from `packs` rather than overlaid onto it, and the reason is
     * the save path: `cp_names_save` writes `packs` straight back out to layer
     * 0, so an authored name merged in here would be spliced into the
     * machine-owned file on the next unpack — which is precisely the corruption
     * the two-layer split exists to stop.
     *
     * So layer 1 is consulted for name -> id (`cp_name_find`), which is what
     * `cachepack pack` needs in order to resolve a config block headed
     * `[bankmain]`, and is deliberately NOT consulted for id -> name, so unpack
     * output and the layer-0 file are both unchanged by its presence.
     */
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* t = cp_type(i);
        char path[1200];
        snprintf(path, sizeof(path), "%s/names/%s.pack", srcdir, t->name);
        if( !lc_pack_load(&names->authored[i], path, t->name, 1) )
        {
            fprintf(stderr, "cachepack: failed to read %s\n", path);
            return 0;
        }
    }
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        const struct CP_Asset* asset = cp_asset(i);
        char path[1200];
        snprintf(path, sizeof(path), "%s/pack/%s.pack", srcdir, asset->pack);
        if( !lc_pack_load(&names->asset_packs[i], path, asset->pack, 1) )
        {
            fprintf(stderr, "cachepack: failed to read %s\n", path);
            return 0;
        }
        snprintf(path, sizeof(path), "%s/names/%s.pack", srcdir, asset->pack);
        if( !lc_pack_load(&names->asset_authored[i], path, asset->pack, 1) )
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

        /*
         * Only layer 0, and only where layer 0 is ours.
         *
         * `lc_pack_save` emits nothing but `id=name` lines, so rewriting a pack
         * whose names a human wrote destroys the prose justifying them — which
         * is measurable: `pack/param.pack` lost all 58 of its comment lines to
         * one unpack, and that header is the record of which param-id claims
         * were checked against a cache and how.
         *
         * A namespace declaring `names` as anything but `cache` has no
         * machine-owned layer 0 to regenerate: `stat` and `category` have no
         * cache table at all, and `component` has no gameval archive, so every
         * one of its names is imported and lives in `names/component.pack`.
         */
        if( !cp_register_may_write_pack(cp_type(i)->name) )
            continue;

        snprintf(path, sizeof(path), "%s/%s.pack", dir, cp_type(i)->name);
        /* Sparse: a `<type>_<id>` line says the id twice and resolves without
         * being stored (cp_name_find). Writing them cost this tree 93,000 of
         * its 306,818 pack lines. */
        if( !lc_pack_save_sparse(&names->packs[i], path) )
        {
            fprintf(stderr, "cachepack: failed to write %s\n", path);
            return 0;
        }
    }
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        /* An asset pack with nothing in it means that table was never exported.
         * Writing an empty file would suggest the table is empty, which is a
         * different claim. */
        if( names->asset_packs[i].max == 0 )
            continue;
        if( !cp_register_may_write_pack(cp_asset(i)->pack) )
            continue;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s.pack", dir, cp_asset(i)->pack);
        if( !lc_pack_save_sparse(&names->asset_packs[i], path) )
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
    {
        lc_pack_free(&names->packs[i]);
        lc_pack_free(&names->authored[i]);
    }
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        lc_pack_free(&names->asset_packs[i]);
        lc_pack_free(&names->asset_authored[i]);
    }
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

/**
 * Seed one pack from one gameval archive.
 *
 * `ids`/`id_count` is the ascending list of ids the *records* actually use — a
 * config group's record ids, or an asset table's archive ids. It is what makes
 * the claim checkable: the archive-id-to-kind mapping is recorded nowhere in
 * the cache, so "archive 14 names interfaces" is an assertion until 969 of its
 * 969 ids turn out to be interfaces that exist.
 *
 * Returns 1 when the archive was trusted and the pack seeded.
 */
static int
seed_pack_from_gameval(
    struct CP_Ctx* ctx,
    int gv_table,
    int gameval_archive,
    const char* kind,
    struct LC_Pack* pack,
    const int* ids,
    int id_count)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, gv_table, gameval_archive);
    if( !archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    /*
     * Verify before trusting. An archive naming some other kind fails
     * immediately and loudly; a partially-stale one (a handful of ids the group
     * has since dropped) is tolerated, because that is what a live index looks
     * like and refusing it would throw away every good name with it.
     */
    int matched = 0;
    if( ids && id_count > 0 )
    {
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
        fprintf(stderr,
                "cachepack: gameval archive %d does not name %s (%d/%d ids match) — "
                "using %s_<id>\n",
                gameval_archive, kind, matched, total, kind);
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    for( int f = 0; f < files->file_count; f++ )
    {
        int fid = archive->file_ids ? archive->file_ids[f] : f;
        if( files->file_sizes[f] <= 0 )
            continue;
        /*
         * A name already in the pack wins over the gameval, so re-unpacking a
         * cache over a tree never renames anything. On a first unpack that is
         * every id; thereafter it is whatever a previous unpack of the same
         * cache already wrote, which is the same answer.
         *
         * Layer 1 is deliberately not consulted here: an authored name that
         * shadows a cache name is an *alias*, and suppressing the cache's own
         * line would make the alias the only way to spell the record.
         */
        if( fid >= 0 && fid < pack->capacity && pack->names && pack->names[fid] )
            continue;
        char name[256];
        sanitise_name(files->files[f], files->file_sizes[f], name, sizeof(name));
        if( !name[0] )
            continue;
        uniquify(pack, name, sizeof(name));
        lc_pack_set(pack, fid, name);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return 1;
}

/*
 * Gameval archive 14 is not a flat name list — it names interfaces *and their
 * components*, and one record holds both:
 *
 *     <interface name> \0
 *     repeat:  u16 child_id   <component name> \0
 *     0xffff
 *
 * Reading it as a flat string produces exactly what you would expect —
 * `messagebox...safezone...continue...` as one 63-character "name" — which is
 * how it read before this function existed.
 *
 * Across cache.osrs239 it decodes to 969 interfaces and 26,519 components with
 * no malformed record, and it settles a question the tree had been answering
 * with a *different server's* table: the cache says `bankmain:items` is child
 * 12, and the imported name for child 13 called it `bankmain_items`. Child 13
 * is `scrollbar`.
 *
 * Components are written straight out rather than kept in `CP_Names`, because
 * they are not a cache table: no config record references a component, so
 * nothing in cachepack ever resolves one. The file exists for the server and
 * the script compiler, and being wholly derived from the cache it is safe to
 * regenerate every time.
 */
static int
seed_interface_names(
    struct CP_Ctx* ctx,
    int gv_table,
    const char* srcdir)
{
    const struct CP_Asset* asset = cp_asset(CP_ASSET_INTERFACE);
    struct LC_Pack* pack = &ctx->names.asset_packs[CP_ASSET_INTERFACE];

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, gv_table, asset->gameval_archive);
    if( !archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    struct LC_Pack components;
    memset(&components, 0, sizeof(components));
    snprintf(components.type, sizeof(components.type), "%s", "component");

    int interfaces = 0;
    int coms = 0;
    int malformed = 0;

    for( int f = 0; f < files->file_count; f++ )
    {
        int iface = archive->file_ids ? archive->file_ids[f] : f;
        const unsigned char* bytes = (const unsigned char*)files->files[f];
        int len = files->file_sizes[f];
        int at = 0;

        if( len <= 0 )
            continue;

        while( at < len && bytes[at] )
            at++;
        char iface_name[256];
        sanitise_name((const char*)bytes, at, iface_name, sizeof(iface_name));
        if( !iface_name[0] )
            continue;
        at++;

        /* Layer 0 is regenerated, but never renamed: a previous unpack of the
         * same cache already wrote this, and an authored rename lives in
         * layer 1 where nothing here can reach it. */
        if( !(iface >= 0 && iface < pack->capacity && pack->names && pack->names[iface]) )
        {
            char unique[256];
            snprintf(unique, sizeof(unique), "%s", iface_name);
            uniquify(pack, unique, sizeof(unique));
            lc_pack_set(pack, iface, unique);
        }
        interfaces++;

        while( at + 2 <= len )
        {
            int child = (bytes[at] << 8) | bytes[at + 1];
            if( child == 0xffff )
                break;
            at += 2;
            int start = at;
            while( at < len && bytes[at] )
                at++;

            char child_name[256];
            sanitise_name((const char*)bytes + start, at - start, child_name, sizeof(child_name));
            at++;
            if( !child_name[0] )
                continue;

            /*
             * `<interface>:<child>`, which is LostCity's own spelling for the
             * same thing — and the qualification is not decoration: 96 of these
             * interfaces have a component called `universe` and 40 have one
             * called `frame`.
             */
            char full[520];
            snprintf(full, sizeof(full), "%s:%s", iface_name, child_name);
            lc_pack_set(&components, (iface << 16) | child, full);
            coms++;
        }
        if( at + 2 > len || ((bytes[at] << 8) | bytes[at + 1]) != 0xffff )
            malformed++;
    }

    if( malformed )
        fprintf(stderr, "cachepack: %d of %d interface name records did not end cleanly\n",
                malformed, files->file_count);

    int wrote = 0;
    if( cp_register_may_write_pack("component") )
    {
        /* Seeding runs before cp_names_save, so on a tree whose `pack/` has
         * been cleared this is the first thing to need the directory. */
        char dir[1300];
        snprintf(dir, sizeof(dir), "%s/pack", srcdir);
        cp_mkdir(dir);

        char path[1400];
        snprintf(path, sizeof(path), "%s/component.pack", dir);
        wrote = lc_pack_save_sparse(&components, path);
    }
    printf("  %-11s %6d interfaces, %d components%s\n", "gameval", interfaces, coms,
           wrote ? " -> pack/component.pack" : "");

    lc_pack_free(&components);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return 1;
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

        int* ids = NULL;
        int id_count = 0;
        cp_record_ids(ctx, t, &ids, &id_count);
        if( seed_pack_from_gameval(ctx, gv_table, type->gameval_archive, type->name,
                                   &ctx->names.packs[t], ids, id_count) )
            ctx->names.from_gameval[t] = true;
        free(ids);
    }

    /*
     * The asset kinds the cache also names.
     *
     * Archives 11, 12 and 14 name songs, sprites and interfaces, and nothing
     * read them until now — so `pack/interface.pack` was 934 lines of
     * `interface_<id>` with 34 names copied from another server's table
     * spliced in, when the cache says `toplevel_osrs_stretch` for 161 and
     * `bankmain` for 12 itself.
     *
     * An asset's ids are its table's archive ids, which is the list that makes
     * the claim checkable in exactly the way a config group's record ids do.
     */
    for( int a = 0; a < CP_ASSET_COUNT; a++ )
    {
        const struct CP_Asset* asset = cp_asset(a);
        if( asset->gameval_archive < 0 )
            continue;

        /* The one structured archive: it carries each interface's components
         * alongside its name, so it needs its own reader. */
        if( a == CP_ASSET_INTERFACE )
        {
            seed_interface_names(ctx, gv_table, ctx->srcdir);
            continue;
        }

        int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
        if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
            continue;
        struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];
        if( !rt )
            continue;

        seed_pack_from_gameval(ctx, gv_table, asset->gameval_archive, asset->pack,
                               &ctx->names.asset_packs[a], rt->ids, rt->id_count);
    }
}

const char*
cp_name_get(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id)
{
    /*
     * Layer 1 wins, same rule as the runtime's.
     *
     * It has to: `param` has no gameval archive, so layer 0 is 2,598 lines of
     * `param_<id>` and the 36 names that mean anything all live in
     * `names/param.pack`. Reading layer 0 first would head every unpacked block
     * `[param_0]` instead of `[stabattack]`.
     *
     * Safe with respect to `cp_names_save`, which writes `names.packs` and has
     * no path to `names.authored` — so preferring layer 1 for reads cannot leak
     * an authored name into the machine-owned file.
     */
    struct LC_Pack* authored = &ctx->names.authored[type];
    struct LC_Pack* pack;

    if( id >= 0 && id < authored->capacity && authored->names && authored->names[id] )
        return authored->names[id];

    pack = &ctx->names.packs[type];
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
    /* Authored first: a config block headed `[bankmain]` names interface 12 by
     * the name this tree gave it, which layer 0 does not carry. Layer 0 still
     * resolves, so the cache's own `interface_12` keeps working too. */
    int id = lc_pack_find(&ctx->names.authored[type], name);

    if( id >= 0 )
        return id;
    id = lc_pack_find(&ctx->names.packs[type], name);
    if( id >= 0 )
        return id;
    /*
     * Last: the name is its own id.
     *
     * `param_2633` is what `cp_name_ensure` invents for a record the cache does
     * not name, and it is a function of the id — so it resolves without a pack
     * line, and the 2,598 lines that used to store it are gone. Tried last, so
     * a real name that happens to look synthetic (a layer-1 `4=npc_9`) still
     * wins wherever it is written down.
     */
    return lc_pack_synthetic_id(cp_type(type)->name, name);
}

/* ---- asset names -------------------------------------------------------- */

const char*
cp_asset_name_get(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id)
{
    /* Layer 1 wins, exactly as it does for a config type. An asset kind needs
     * it for the same reason: `hitsplat` and the handful of interfaces this
     * world renamed are authored names over ids the cache states. */
    struct LC_Pack* authored = &ctx->names.asset_authored[asset];
    struct LC_Pack* pack;

    if( id >= 0 && id < authored->capacity && authored->names && authored->names[id] )
        return authored->names[id];

    pack = &ctx->names.asset_packs[asset];
    if( id < 0 || id >= pack->capacity || !pack->names )
        return NULL;
    return pack->names[id];
}

void
cp_asset_name_set(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id,
    const char* name)
{
    char unique[300];
    snprintf(unique, sizeof(unique), "%s", name);
    uniquify(&ctx->names.asset_packs[asset], unique, sizeof(unique));
    lc_pack_set(&ctx->names.asset_packs[asset], id, unique);
}

const char*
cp_asset_name_ensure(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id)
{
    const char* existing = cp_asset_name_get(ctx, asset, id);
    if( existing )
        return existing;
    char name[256];
    snprintf(name, sizeof(name), "%s_%d", cp_asset(asset)->pack, id);
    cp_asset_name_set(ctx, asset, id, name);
    return cp_asset_name_get(ctx, asset, id);
}

int
cp_asset_name_find(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    const char* name)
{
    int id = lc_pack_find(&ctx->names.asset_authored[asset], name);

    if( id >= 0 )
        return id;
    id = lc_pack_find(&ctx->names.asset_packs[asset], name);
    if( id >= 0 )
        return id;
    /* `interface_412.if` on disk is interface 412 by construction, so the file
     * repacks with no pack line behind it. */
    return lc_pack_synthetic_id(cp_asset(asset)->pack, name);
}
