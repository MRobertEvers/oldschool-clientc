#include "cp_assets.h"

extern const struct CP_AssetCodec cp_codec_texture;
extern const struct CP_AssetCodec cp_codec_interface;
extern const struct CP_AssetCodec cp_codec_map;
extern const struct CP_AssetCodec cp_codec_script;
extern const struct CP_AssetCodec cp_codec_sprite;
extern const struct CP_AssetCodec cp_codec_worldmap;

#include "archive.h"
#include "checksum.h"
#include "dat2disk.h"
#include "filelist.h"
#include "reference_table.h"

#include "datatypes/dat2_config_idk.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_spotanim.h"
#include "datatypes/dat2_configs.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#define cp_mkdir(p) mkdir(p, 0755)
#endif

/*
 * The register.
 *
 * Directory names are LostCity's where LostCity has one, because someone who
 * knows that tree should not have to learn a second vocabulary to read this one.
 * `songs`, `jingles`, `synth`, `models`, `sprites`, `textures`, `fonts`, `maps`,
 * `binary` and `scripts` all carry across unchanged. The tables rev 254 does not
 * have — animayas, the world map, the db index — get names of their own.
 *
 * ## Which archives explode into a directory, and why most do not
 *
 * An archive holding several files could always become a directory of them, and
 * the first cut did exactly that. It produced 352,849 files, 209,295 of them
 * individual animation frames — and it is the wrong shape twice over. It is not
 * what LostCity does (an `.anim` *is* the archive, which is why its `models/`
 * holds 4,229 files and not a quarter of a million), and an animation frame is
 * not independently useful: the animset is the unit anyone edits, loads or names.
 *
 * So CP_ASSET_MULTIFILE is now the exception, kept only where the files inside
 * really are separate assets that something addresses one at a time:
 *
 *   textures  one archive, 210 material definitions, each with its own id and
 *             its own line in LostCity's texture.pack
 *   dbindex   the master index and one file per indexed column
 *
 * Everything else stores the archive's payload whole. That costs nothing in
 * fidelity — the payload is the container's own file table plus the files, and it
 * round-trips byte for byte either way — and it takes the tree from 352,849 files
 * to 117,000.
 */

/* clang-format off */
static const struct CP_Asset g_assets[CP_ASSET_COUNT] = {
    [CP_ASSET_FRAME] = {
        "animsets", "animset", "anim", RSCACHE_DAT2_TABLE_ANIMATIONS, 0, NULL },
    [CP_ASSET_FRAMEMAP] = {
        "framemaps", "base", "base", RSCACHE_DAT2_TABLE_SKELETONS, 0, NULL },
    [CP_ASSET_INTERFACE] = {
        "interfaces", "interface", "if", RSCACHE_DAT2_TABLE_INTERFACES, 0,
        &cp_codec_interface },
    [CP_ASSET_SYNTH] = {
        "synth", "synth", "synth", RSCACHE_DAT2_TABLE_SOUND_EFFECTS, 0, NULL },
    [CP_ASSET_MAP] = {
        "maps", "map", "map", RSCACHE_DAT2_TABLE_MAPS, CP_ASSET_ENCRYPTED, &cp_codec_map },
    [CP_ASSET_SONG] = {
        "songs", "song", "jmid", RSCACHE_DAT2_TABLE_MUSIC_TRACKS, 0, NULL },
    [CP_ASSET_MODEL] = {
        "models", "model", "model", RSCACHE_DAT2_TABLE_MODELS, 0, NULL },
    [CP_ASSET_SPRITE] = {
        "sprites", "sprite", "sprite", RSCACHE_DAT2_TABLE_SPRITES, 0, &cp_codec_sprite },
    [CP_ASSET_TEXTURE] = {
        "textures", "texture", "texture", RSCACHE_DAT2_TABLE_TEXTURES, CP_ASSET_MULTIFILE,
        &cp_codec_texture },
    [CP_ASSET_BINARY] = {
        "binary", "binary", "bin", RSCACHE_DAT2_TABLE_BINARY, 0, NULL },
    [CP_ASSET_JINGLE] = {
        "jingles", "jingle", "jmid", RSCACHE_DAT2_TABLE_MUSIC_JINGLES, 0, NULL },
    [CP_ASSET_SCRIPT] = {
        "scripts", "script", "bin", RSCACHE_DAT2_TABLE_CLIENTSCRIPT, 0, &cp_codec_script },
    [CP_ASSET_FONT] = {
        "fonts", "font", "fm", RSCACHE_DAT2_TABLE_FONTS, 0, NULL },
    [CP_ASSET_SAMPLE] = {
        "samples", "sample", "sample", RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0, NULL },
    [CP_ASSET_PATCH] = {
        "patches", "patch", "patch", RSCACHE_DAT2_TABLE_MUSIC_PATCHES, 0, NULL },
    [CP_ASSET_WORLDMAP_GEOGRAPHY] = {
        "worldmap/geography", "worldmapgeo", "wmg",
        RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY, 0, NULL },
    [CP_ASSET_WORLDMAP_AREA] = {
        "worldmap/areas", "worldmaparea", "bin", RSCACHE_DAT2_TABLE_WORLDMAP,
        CP_ASSET_MULTIFILE, &cp_codec_worldmap },
    [CP_ASSET_WORLDMAP_GROUND] = {
        "worldmap/ground", "worldmapground", "bin",
        RSCACHE_DAT2_TABLE_WORLDMAP_GROUND, 0, NULL },
    [CP_ASSET_DBINDEX] = {
        "dbindex", "dbindex", "dbidx", RSCACHE_DAT2_TABLE_DBTABLE_INDEX, CP_ASSET_MULTIFILE, NULL },
    [CP_ASSET_ANIMAYA] = {
        "animayas", "animaya", "animaya", RSCACHE_DAT2_TABLE_ANIMAYAS, 0, NULL },
};
/* clang-format on */

/* `--raw-assets` turns every friendly codec off in one place, which is how a
 * caller gets the payload back when a decoded form is in the way. */
static int g_raw_assets = 0;

void
cp_assets_set_raw(int raw)
{
    g_raw_assets = raw;
}

static const struct CP_AssetCodec*
asset_codec(const struct CP_Asset* asset)
{
    return g_raw_assets ? NULL : asset->codec;
}

const struct CP_Asset*
cp_asset(enum CP_AssetId id)
{
    if( id < 0 || id >= CP_ASSET_COUNT )
        return NULL;
    return &g_assets[id];
}

int
cp_asset_by_name(const char* dir)
{
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        if( strcmp(g_assets[i].dir, dir) == 0 || strcmp(g_assets[i].pack, dir) == 0 )
            return i;
    }
    return -1;
}

/* ---- extensions from the bytes ------------------------------------------ */

/**
 * A model's format lives in its last two bytes, not in its era.
 *
 * Mirrors the selector in `model.c`: the magic trailer picks the decoder, and it
 * is the only thing that does. Naming the file after it means an ob2 is called an
 * ob2 wherever one turns up, and an OldSchool model is never mislabelled as one.
 */
static const char*
model_extension(
    const uint8_t* data,
    int size)
{
    if( size < 2 )
        return "model";
    uint8_t hi = data[size - 2];
    uint8_t lo = data[size - 1];
    if( hi == 0xFF && lo == 0xFF )
        return "ob3";
    if( hi == 0xFF && lo == 0xFE )
        return "model"; /* OldSchool "version 2" — not an ob2 */
    if( hi == 0xFF && lo == 0xFD )
        return "model"; /* OldSchool "version 3" */
    return "ob2";
}

const char*
cp_asset_extension(
    enum CP_AssetId id,
    const uint8_t* payload,
    int size)
{
    const struct CP_Asset* asset = cp_asset(id);
    if( !payload || size <= 0 )
        return asset->ext;
    if( size == CP_ASSET_SIZE_ARCHIVE )
        return asset->ext;

    /* Container formats first: a table that happens to hold PNGs should say so,
     * whichever table it is. Table 20 is PNG and table 10 is JPEG in OldSchool,
     * but that is a fact about those caches, not a rule to hardcode. */
    if( size >= 8 && memcmp(payload, "\x89PNG\r\n\x1a\n", 8) == 0 )
        return "png";
    if( size >= 3 && payload[0] == 0xFF && payload[1] == 0xD8 && payload[2] == 0xFF )
        return "jpg";
    if( size >= 6 && (memcmp(payload, "GIF87a", 6) == 0 || memcmp(payload, "GIF89a", 6) == 0) )
        return "gif";
    if( size >= 4 && memcmp(payload, "MThd", 4) == 0 )
        return "mid";
    if( size >= 4 && memcmp(payload, "OggS", 4) == 0 )
        return "ogg";

    if( id == CP_ASSET_MODEL )
        return model_extension(payload, size);

    return asset->ext;
}

/* ---- paths -------------------------------------------------------------- */

static int
ensure_dir(const char* path)
{
    struct stat info;
    if( stat(path, &info) == 0 )
        return S_ISDIR(info.st_mode) ? 0 : -1;
    return cp_mkdir(path);
}

/** mkdir -p, so "worldmap/geography" and a renamed model's "models/npc" work. */
static int
ensure_dir_recursive(const char* path)
{
    char buf[1400];
    snprintf(buf, sizeof(buf), "%s", path);
    for( char* p = buf + 1; *p; p++ )
    {
        if( *p != '/' )
            continue;
        *p = '\0';
        if( ensure_dir(buf) != 0 )
            return -1;
        *p = '/';
    }
    return ensure_dir(buf);
}

/* ---- naming models after the configs that use them ---------------------- */

/*
 * A cache names its configs and never its models, so without this every model is
 * `model_24458` and the tree is no more readable than the raw idx dump it
 * replaces. LostCity solves it by naming a model after whatever references it,
 * and that is what this does.
 *
 * Order matters and is deliberate: npc, then obj, then loc, then spotanim, then
 * identkit. Whichever config claims a model first keeps it, so a model shared
 * between an npc and the loc it stands on is filed under the npc. Any order is
 * arbitrary; a fixed one at least makes the output reproducible.
 */

/** shape id -> LostCity's one-character suffix. Transcribed from its
 *  `LocShapeSuffix`, which the loc packer also reads back. */
static const char* const LOC_SHAPE_SUFFIX[23] = { "1", "2", "3", "4", "q", "w", "r", "e",
                                                  "t", "5", "8", "9", "a", "s", "d", "f",
                                                  "g", "h", "z", "x", "c", "v", "0" };

static void
name_model(
    struct CP_Ctx* ctx,
    int model_id,
    const char* dir,
    const char* base,
    const char* suffix)
{
    if( model_id < 0 )
        return;
    if( cp_asset_name_get(ctx, CP_ASSET_MODEL, model_id) )
        return; /* first claim wins */
    char name[300];
    snprintf(name, sizeof(name), "%s/%s%s", dir, base, suffix ? suffix : "");
    cp_asset_name_set(ctx, CP_ASSET_MODEL, model_id, name);
}

void
cp_assets_name_models(struct CP_Ctx* ctx)
{
    struct CP_Group group;
    int named_before = ctx->names.asset_packs[CP_ASSET_MODEL].max;

    /* --- npcs: body models and chatheads --- */
    if( cp_group_open(ctx, CP_TYPE_NPC, &group) )
    {
        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;
            struct RSCache_Dat2ConfigNpc* npc =
                RSCache_Dat2ConfigNpcNewDecodeProfile(&ctx->profile, (char*)record, size);
            if( !npc )
                continue;
            const char* base = cp_name_ensure(ctx, CP_TYPE_NPC, id);
            for( int m = 0; m < npc->models_count; m++ )
                name_model(ctx, npc->models[m], "npc", base, NULL);
            for( int m = 0; m < npc->chathead_models_count; m++ )
                name_model(ctx, npc->chathead_models[m], "npc", base, "_head");
            RSCache_Dat2ConfigNpcFree(npc);
        }
        cp_group_free(&group);
    }

    /* --- objs: the inventory icon and the worn models --- */
    if( cp_group_open(ctx, CP_TYPE_OBJ, &group) )
    {
        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;
            struct RSCache_Dat2ConfigObj* obj =
                RSCache_Dat2ConfigObjNewDecodeProfile(&ctx->profile, (char*)record, size);
            if( !obj )
                continue;
            const char* base = cp_name_ensure(ctx, CP_TYPE_OBJ, id);
            name_model(ctx, obj->inventory_model_id, "obj", base, NULL);
            name_model(ctx, obj->male_model_0, "obj", base, "_manwear");
            name_model(ctx, obj->male_model_1, "obj", base, "_manwear2");
            name_model(ctx, obj->male_model_2, "obj", base, "_manwear3");
            name_model(ctx, obj->male_head_model, "obj", base, "_manhead");
            name_model(ctx, obj->male_head_model_2, "obj", base, "_manhead2");
            name_model(ctx, obj->female_model_0, "obj", base, "_womanwear");
            name_model(ctx, obj->female_model_1, "obj", base, "_womanwear2");
            name_model(ctx, obj->female_model_2, "obj", base, "_womanwear3");
            name_model(ctx, obj->female_head_model, "obj", base, "_womanhead");
            name_model(ctx, obj->female_head_model_2, "obj", base, "_womanhead2");
            RSCache_Dat2ConfigObjFree(obj);
        }
        cp_group_free(&group);
    }

    /* --- locs: one name per (shape, model), with the shape's suffix --- */
    if( cp_group_open(ctx, CP_TYPE_LOC, &group) )
    {
        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;
            struct RSCache_Dat2ConfigLoc* loc =
                RSCache_Dat2ConfigLocNewDecodeProfile(&ctx->profile, (char*)record, size);
            if( !loc )
                continue;
            const char* base = cp_name_ensure(ctx, CP_TYPE_LOC, id);
            for( int s = 0; s < loc->shapes_and_model_count; s++ )
            {
                /* `shapes == NULL` is opcode 5's shapeless list — the models apply
                 * to whatever shape the map asks for, so there is no suffix. */
                const char* suffix = "";
                if( loc->shapes && loc->shapes[s] >= 0 && loc->shapes[s] < 23 )
                    suffix = LOC_SHAPE_SUFFIX[loc->shapes[s]];
                for( int m = 0; m < loc->lengths[s]; m++ )
                    name_model(ctx, loc->models[s][m], "loc", base, suffix);
            }
            RSCache_Dat2ConfigLocFree(loc);
        }
        cp_group_free(&group);
    }

    /* --- spotanims --- */
    if( cp_group_open(ctx, CP_TYPE_SPOTANIM, &group) )
    {
        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;
            struct RSCache_Dat2ConfigSpotanim* spot =
                RSCache_Dat2ConfigSpotanimNewDecode(ctx->profile.revision, (char*)record, size);
            if( !spot )
                continue;
            name_model(ctx, spot->model, "spot", cp_name_ensure(ctx, CP_TYPE_SPOTANIM, id), NULL);
            RSCache_Dat2ConfigSpotanimFree(spot);
        }
        cp_group_free(&group);
    }

    /* --- identkits: the player's own body parts --- */
    if( cp_group_open(ctx, CP_TYPE_IDK, &group) )
    {
        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;
            struct RSCache_Dat2ConfigIdk idk;
            memset(&idk, 0, sizeof(idk));
            RSCache_Dat2ConfigIdkDecodeInplace(&idk, (char*)record, size);
            const char* base = cp_name_ensure(ctx, CP_TYPE_IDK, id);
            for( int m = 0; m < idk.model_ids_count; m++ )
                name_model(ctx, idk.model_ids[m], "idk", base, NULL);
            for( int m = 0; m < 10; m++ )
                name_model(ctx, idk.if_model_ids[m], "idk", base, "_if");
            free(idk.model_ids);
            free(idk.recolors_from);
            free(idk.recolors_to);
            free(idk.retextures_from);
            free(idk.retextures_to);
        }
        cp_group_free(&group);
    }

    int named = ctx->names.asset_packs[CP_ASSET_MODEL].max - named_before;
    printf("  named %d models after the configs that reference them\n", named);
}

/*
 * Name map squares `m<x>_<z>`, the way LostCity's `maps/` is.
 *
 * A classic cache names its map archives itself, by hashing `m50_50` into the
 * reference table's identifier — one archive for terrain and another for locs.
 * OldSchool dropped identifiers and addresses one archive per region instead, at
 * `(x << 8) | z`, with terrain and locs as files inside it. Both are handled: the
 * hash is inverted where there is one (131,072 candidate names is a cheap table),
 * and the region id is decoded where there is not.
 *
 * Either way the file lands at `maps/m50_50.jm2`, which is the name the server's
 * spawns are already filed under and the whole reason the two trees can be one.
 */
void
cp_assets_name_maps(struct CP_Ctx* ctx)
{
    if( !ctx->cache_open )
        return;
    int table = RSCache_Dat2DiskTableId(ctx->cache.disk, cp_asset(CP_ASSET_MAP)->table);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT || !ctx->cache.disk->tables[table] )
        return;
    struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table];

    int identified = 0;
    for( int i = 0; i < rt->id_count; i++ )
    {
        int id = rt->ids[i];
        if( id < rt->archive_count && rt->archives[id].identifier != 0 )
            identified = 1;
    }

    /* Only built when the cache actually names its archives. */
    int(*hashes)[256][2] = NULL;
    if( identified )
    {
        hashes = malloc(256 * sizeof(*hashes));
        if( !hashes )
            return;
        for( int x = 0; x < 256; x++ )
        {
            for( int z = 0; z < 256; z++ )
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "m%d_%d", x, z);
                hashes[x][z][0] = RSCache_ArchiveNameHashDat2(buf);
                snprintf(buf, sizeof(buf), "l%d_%d", x, z);
                hashes[x][z][1] = RSCache_ArchiveNameHashDat2(buf);
            }
        }
    }

    int count = 0;
    for( int i = 0; i < rt->id_count; i++ )
    {
        int id = rt->ids[i];
        if( cp_asset_name_get(ctx, CP_ASSET_MAP, id) )
            continue; /* the tree already named it */
        char name[32];
        name[0] = '\0';
        if( identified )
        {
            int want = id < rt->archive_count ? rt->archives[id].identifier : 0;
            for( int x = 0; x < 256 && !name[0]; x++ )
            {
                for( int z = 0; z < 256 && !name[0]; z++ )
                {
                    if( hashes[x][z][0] == want )
                        snprintf(name, sizeof(name), "m%d_%d", x, z);
                    else if( hashes[x][z][1] == want )
                        snprintf(name, sizeof(name), "l%d_%d", x, z);
                }
            }
        }
        else if( id >= 0 && id <= 0xFFFF )
        {
            snprintf(name, sizeof(name), "m%d_%d", id >> 8, id & 0xFF);
        }
        if( !name[0] )
            continue;
        cp_asset_name_set(ctx, CP_ASSET_MAP, id, name);
        count++;
    }
    free(hashes);
    printf("  named %d map squares m<x>_<z>\n", count);
}

/* ---- export ------------------------------------------------------------- */

static int
write_file(
    const char* path,
    const uint8_t* data,
    int size)
{
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        fprintf(stderr, "cachepack: cannot write %s: %s\n", path, strerror(errno));
        return 0;
    }
    int ok = size == 0 || (int)fwrite(data, 1, (size_t)size, out) == size;
    fclose(out);
    return ok;
}

static int
parse_asset_list(
    const char* csv,
    unsigned* out_mask)
{
    *out_mask = 0;
    if( !csv || !*csv )
        return 1;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", csv);
    char* cursor = buf;
    while( cursor && *cursor )
    {
        char* comma = strchr(cursor, ',');
        if( comma )
            *comma = '\0';
        int asset = cp_asset_by_name(cursor);
        if( asset < 0 )
        {
            fprintf(stderr, "cachepack: unknown asset kind '%s'\n", cursor);
            return 0;
        }
        *out_mask |= 1u << asset;
        cursor = comma ? comma + 1 : NULL;
    }
    return 1;
}

static int
export_one(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    long long* out_bytes)
{
    const struct CP_Asset* asset = cp_asset(id);
    int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return 0;
    struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];
    if( !rt )
        return 0;

    char root[1300];
    snprintf(root, sizeof(root), "%s/%s", ctx->srcdir, asset->dir);
    if( ensure_dir_recursive(root) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", root);
        return -1;
    }

    int written = 0;
    long long bytes = 0;

    for( int a = 0; a < rt->id_count; a++ )
    {
        int archive_id = rt->ids[a];
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, table_id, archive_id);
        if( !archive )
        {
            /* Map archives before OldSchool 237 are XTEA encrypted, and a square
             * whose key is missing from xteas.json cannot be read at all. That is
             * a property of the dump, not a failure here, so it is counted and
             * reported rather than aborting the export. */
            if( asset->flags & CP_ASSET_ENCRYPTED )
                ctx->warn_short_decode++;
            continue;
        }
        if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        const char* name = cp_asset_name_ensure(ctx, id, archive_id);
        char path[1500];

        const struct CP_AssetCodec* codec = asset_codec(asset);
        int multifile = (asset->flags & CP_ASSET_MULTIFILE) && archive->file_count > 1;
        if( codec && codec->write && !multifile )
        {
            snprintf(path, sizeof(path), "%s/%s", root, name);
            char* slash = strrchr(path, '/');
            if( slash )
            {
                *slash = '\0';
                ensure_dir_recursive(path);
                *slash = '/';
            }
            if( codec->write(ctx, archive_id, (const uint8_t*)archive->data, archive->data_size,
                             archive->file_ids, archive->file_count, path) )
            {
                written++;
                bytes += archive->data_size;
                RSCache_Dat2DiskArchiveFree(archive);
                continue;
            }
            /* Declined: fall through and write the raw payload, so a record the
             * codec cannot express is still carried rather than dropped. */
            ctx->warn_unknown_key++;
        }

        if( multifile )
        {
            /* Several files under one id: the archive becomes a directory, so the
             * file ids inside it stay addressable and the round trip can put them
             * back in the same archive. */
            struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( files )
            {
                snprintf(path, sizeof(path), "%s/%s", root, name);
                if( ensure_dir_recursive(path) == 0 )
                {
                    for( int f = 0; f < files->file_count; f++ )
                    {
                        int file_id = archive->file_ids ? archive->file_ids[f] : f;
                        const uint8_t* payload = (const uint8_t*)files->files[f];
                        int payload_size = files->file_sizes[f];
                        if( codec && codec->write )
                        {
                            snprintf(path, sizeof(path), "%s/%s/%d", root, name, file_id);
                            if( codec->write(ctx, file_id, payload, payload_size, NULL, 1,
                                             path) )
                            {
                                written++;
                                bytes += payload_size;
                                continue;
                            }
                        }
                        const char* ext = cp_asset_extension(id, payload, payload_size);
                        snprintf(path, sizeof(path), "%s/%s/%d.%s", root, name, file_id, ext);
                        if( write_file(path, payload, payload_size) )
                        {
                            written++;
                            bytes += payload_size;
                        }
                    }
                }
                RSCache_FileListFree(files);
            }
        }
        else
        {
            const uint8_t* payload = (const uint8_t*)archive->data;
            int payload_size = archive->data_size;
            /* A whole archive's head is its first member's bytes, so sniffing it
             * would name the archive after one of the files inside it. */
            const char* ext = cp_asset_extension(
                id, payload, archive->file_count > 1 ? CP_ASSET_SIZE_ARCHIVE : payload_size);
            snprintf(path, sizeof(path), "%s/%s.%s", root, name, ext);
            /* A renamed model carries its subdirectory in the name. */
            char* slash = strrchr(path, '/');
            if( slash )
            {
                *slash = '\0';
                ensure_dir_recursive(path);
                *slash = '/';
            }
            if( write_file(path, payload, payload_size) )
            {
                written++;
                bytes += payload_size;
            }
        }

        RSCache_Dat2DiskArchiveFree(archive);
    }

    printf("  %-19s %6d files, %lld bytes\n", asset->dir, written, bytes);
    *out_bytes += bytes;
    return written;
}

int
cp_assets_export(
    struct CP_Ctx* ctx,
    const char* assets_csv)
{
    unsigned mask = 0;
    if( !parse_asset_list(assets_csv, &mask) )
        return 0;
    int all = mask == 0;

    /* Models first need their names, and the names come from the configs. */
    if( all || (mask & (1u << CP_ASSET_MODEL)) )
        cp_assets_name_models(ctx);
    if( all || (mask & (1u << CP_ASSET_MAP)) )
        cp_assets_name_maps(ctx);

    long long total_bytes = 0;
    int total_files = 0;
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        if( !all && !(mask & (1u << i)) )
            continue;
        int written = export_one(ctx, i, &total_bytes);
        if( written < 0 )
            return 0;
        total_files += written;
    }

    printf("Exported %d asset files, %lld bytes into %s\n", total_files, total_bytes,
           ctx->srcdir);
    return 1;
}

/* ---- import ------------------------------------------------------------- */

/*
 * Reading the tree back is a directory walk rather than a reference-table walk,
 * because the point of the tree is that it can be *added to*. A model that only
 * exists on disk still has a pack line, and that line is the id it goes to.
 */

struct CP_AssetFile
{
    int file_id;
    uint8_t* data;
    int size;
};

static int
read_file(
    const char* path,
    uint8_t** out_data,
    int* out_size)
{
    FILE* in = fopen(path, "rb");
    if( !in )
        return 0;
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(in);
        return 0;
    }
    uint8_t* data = malloc((size_t)size ? (size_t)size : 1);
    if( !data || (size > 0 && fread(data, 1, (size_t)size, in) != (size_t)size) )
    {
        free(data);
        fclose(in);
        return 0;
    }
    fclose(in);
    *out_data = data;
    *out_size = (int)size;
    return 1;
}

/** Strip the extension so a file's basename can be matched against the pack. */
static void
basename_no_ext(
    const char* filename,
    char* out,
    int out_size)
{
    snprintf(out, (size_t)out_size, "%s", filename);
    char* dot = strrchr(out, '.');
    if( dot )
        *dot = '\0';
}

static int
compare_file_id(
    const void* lhs,
    const void* rhs)
{
    return ((const struct CP_AssetFile*)lhs)->file_id -
           ((const struct CP_AssetFile*)rhs)->file_id;
}

/**
 * Rebuild one archive's payload from a directory of numbered files.
 *
 * The file ids have to come back sorted: the container stores them ascending and
 * the reference table's child list is read in the same order, so an archive
 * assembled in readdir order decodes with every file under the wrong id.
 */
static uint8_t*
build_multifile_payload(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset_id,
    const char* dir,
    int** out_ids,
    int* out_count,
    int* out_size)
{
    const struct CP_AssetCodec* codec = asset_codec(cp_asset(asset_id));
    DIR* handle = opendir(dir);
    if( !handle )
        return NULL;

    struct CP_AssetFile* files = NULL;
    int count = 0, capacity = 0;
    struct dirent* entry;
    while( (entry = readdir(handle)) )
    {
        if( entry->d_name[0] == '.' )
            continue;
        char stem[256];
        basename_no_ext(entry->d_name, stem, sizeof(stem));
        char* end = NULL;
        long file_id = strtol(stem, &end, 10);
        if( end == stem || *end )
            continue;

        char path[1600];
        uint8_t* data = NULL;
        int size = 0;
        if( codec && codec->read )
        {
            /* The member's friendly form sits at `<dir>/<id>` without an
             * extension, which is where the exporter put it. */
            int* ignored_ids = NULL;
            int ignored_count = 0;
            snprintf(path, sizeof(path), "%s/%d", dir, (int)file_id);
            data = codec->read(ctx, (int)file_id, path, &ignored_ids, &ignored_count, &size);
            free(ignored_ids);
        }
        if( !data )
        {
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            if( !read_file(path, &data, &size) )
                continue;
        }

        if( count == capacity )
        {
            int next = capacity ? capacity * 2 : 16;
            struct CP_AssetFile* grown = realloc(files, (size_t)next * sizeof(*grown));
            if( !grown )
            {
                free(data);
                break;
            }
            files = grown;
            capacity = next;
        }
        files[count].file_id = (int)file_id;
        files[count].data = data;
        files[count].size = size;
        count++;
    }
    closedir(handle);

    if( count == 0 )
    {
        free(files);
        return NULL;
    }
    qsort(files, (size_t)count, sizeof(*files), compare_file_id);

    struct RSCache_FileList list;
    memset(&list, 0, sizeof(list));
    list.file_count = count;
    list.files = malloc((size_t)count * sizeof(char*));
    list.file_sizes = malloc((size_t)count * sizeof(int));
    int* ids = malloc((size_t)count * sizeof(int));
    uint8_t* payload = NULL;
    if( list.files && list.file_sizes && ids )
    {
        for( int i = 0; i < count; i++ )
        {
            list.files[i] = (char*)files[i].data;
            list.file_sizes[i] = files[i].size;
            ids[i] = files[i].file_id;
        }
        uint32_t bound = RSCache_FileListEncodeBound(&list);
        payload = malloc(bound ? bound : 1);
        if( payload )
        {
            uint32_t written = RSCache_FileListEncode(&list, payload, bound);
            if( written == 0 )
            {
                free(payload);
                payload = NULL;
            }
            else
            {
                *out_size = (int)written;
            }
        }
    }

    for( int i = 0; i < count; i++ )
        free(files[i].data);
    free(files);
    free(list.files);
    free(list.file_sizes);
    if( !payload )
    {
        free(ids);
        return NULL;
    }
    *out_ids = ids;
    *out_count = count;
    return payload;
}

/**
 * Frame the payload and place it, updating the reference table to match.
 *
 * Shared with the raw binary importer through cp_binary.c, because the two write
 * the same thing to the same place — the only difference is that this one has to
 * build the container first, and re-encrypt it where the table is encrypted.
 */
static int
put_archive(
    struct CP_Ctx* ctx,
    const char* out_cache_dir,
    int table_id,
    int archive_id,
    const uint8_t* payload,
    int payload_size,
    const int* file_ids,
    int file_count,
    uint32_t* xtea_key,
    int* out_dirty)
{
    uint32_t bound = RSCache_ArchiveEncodeBound((uint32_t)payload_size,
                                                RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    uint8_t* container = malloc(bound ? bound : 1);
    if( !container )
        return 0;
    uint32_t container_size = RSCache_ArchiveEncode(
        container, bound, payload, (uint32_t)payload_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP,
        xtea_key);
    if( container_size == 0 )
    {
        free(container);
        return 0;
    }

    int rc = RSCache_Dat2DiskWriteArchive(out_cache_dir, table_id, archive_id, container,
                                          (int)container_size);
    if( rc == 0 )
        cp_reference_sync(ctx, table_id, archive_id, container, (int)container_size, file_ids,
                          file_count, out_dirty);
    free(container);
    return rc == 0;
}

static int
import_one(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    const char* out_cache_dir,
    int* out_files)
{
    const struct CP_Asset* asset = cp_asset(id);
    int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return 1;

    char root[1300];
    snprintf(root, sizeof(root), "%s/%s", ctx->srcdir, asset->dir);
    struct stat info;
    if( stat(root, &info) != 0 || !S_ISDIR(info.st_mode) )
        return 1;

    struct LC_Pack* pack = &ctx->names.asset_packs[id];
    int written = 0;
    int dirty = 0;

    /*
     * Walk the pack rather than the directory: the pack is the id authority, and
     * a file whose name it does not list has no id to be written to. That is the
     * same rule the config side follows, and it is what makes an added asset a
     * two-step (name it, then place it) rather than a silent no-op.
     */
    for( int archive_id = 0; archive_id < pack->max; archive_id++ )
    {
        const char* name = pack->names ? pack->names[archive_id] : NULL;
        if( !name )
            continue;

        char base[1600];
        snprintf(base, sizeof(base), "%s/%s", root, name);

        uint8_t* payload = NULL;
        int payload_size = 0;
        int* file_ids = NULL;
        int file_count = 0;

        const struct CP_AssetCodec* codec = asset_codec(asset);
        if( codec && codec->read && !(asset->flags & CP_ASSET_MULTIFILE) )
            payload = codec->read(ctx, archive_id, base, &file_ids, &file_count, &payload_size);

        if( payload )
        {
            /* handled by the codec */
        }
        else if( (asset->flags & CP_ASSET_MULTIFILE) && stat(base, &info) == 0 &&
            S_ISDIR(info.st_mode) )
        {
            payload = build_multifile_payload(ctx, id, base, &file_ids, &file_count,
                                              &payload_size);
        }
        else
        {
            /* Single file: the extension is whatever the export chose, so try the
             * ones this asset can produce rather than assuming one. */
            static const char* const CANDIDATES[] = { NULL, "png", "jpg", "gif", "mid",
                                                      "ogg",  "ob2", "ob3", "model" };
            char path[1700];
            for( size_t c = 0; c < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]); c++ )
            {
                const char* ext = CANDIDATES[c] ? CANDIDATES[c] : asset->ext;
                snprintf(path, sizeof(path), "%s.%s", base, ext);
                if( read_file(path, &payload, &payload_size) )
                    break;
            }
        }

        if( !payload )
            continue;

        uint32_t* key = NULL;
        if( asset->flags & CP_ASSET_ENCRYPTED )
            key = RSCache_Dat2DiskArchiveXteaKey(ctx->cache.disk, table_id, archive_id);

        if( put_archive(ctx, out_cache_dir, table_id, archive_id, payload, payload_size,
                        file_ids, file_count, key, &dirty) )
        {
            written++;
        }
        else
        {
            fprintf(stderr, "cachepack: failed to write %s/%s\n", asset->dir, name);
        }
        free(payload);
        free(file_ids);
    }

    if( dirty && !cp_reference_write(ctx, out_cache_dir, table_id) )
        return 0;
    if( written )
        printf("  %-19s %6d archives%s\n", asset->dir, written,
               dirty ? " (reference table updated)" : "");
    *out_files += written;
    return 1;
}

int
cp_assets_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir)
{
    int total = 0;
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        if( !import_one(ctx, i, out_cache_dir, &total) )
            return 0;
    }
    printf("Imported %d asset archives.\n", total);
    return 1;
}
