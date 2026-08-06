#include "cp_assets.h"
#include "tool_posix_compat.h"
#include "tool_progress.h"
#include "tool_prefetch.h"

extern const struct CP_AssetCodec cp_codec_texture;
extern const struct CP_AssetCodec cp_codec_interface;
extern const struct CP_AssetCodec cp_codec_map;
extern const struct CP_AssetCodec cp_codec_script;
extern const struct CP_AssetCodec cp_codec_sprite;
extern const struct CP_AssetCodec cp_codec_worldmap;
extern const struct CP_AssetCodec cp_codec_worldmapgeo;
extern const struct CP_AssetCodec cp_codec_dbindex;
extern const struct CP_AssetCodec cp_codec_font;

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
#include "datatypes/dat2_worldmap.h"

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
 * **No archive explodes into a directory any more.** It used to, for the three
 * tables whose members really are separate assets — and the shape was wrong for a
 * reason that took a while to see: a member written as `<file_id>.<ext>` carries its
 * id in its *filename* and nowhere else, so it cannot be renamed, and the importer
 * had to recover the member set by listing the directory. Which files an archive
 * contained, and in what order, was whatever `readdir` returned.
 *
 * The fix was not to stop splitting — it was to *state* the split. A table whose
 * members have a text form gets one file of named blocks plus a `.compack` tying
 * each block to a file id (`texture`, `interface`); one whose members are opaque
 * bytes gets a directory of files plus a `.filepack` tying each file to a file id
 * (`dbindex`, `worldmaparea`). Either way the member list is a file someone can read
 * and edit, ascending ids give the container its order, and a member can be renamed
 * without moving an id.
 *
 * A pack file still indexes **archives** either way — `pack/texture.pack` is one
 * line for the one archive, exactly as `pack/interface.pack` is one line per
 * interface. The members are named by their block headers inside the archive's file.
 */

/* clang-format off */
static const struct CP_Asset g_assets[CP_ASSET_COUNT] = {
    [CP_ASSET_FRAME] = {
        "animsets", "0_animations", "animset", "anim", RSCACHE_DAT2_TABLE_ANIMATIONS, 0, -1, NULL },
    [CP_ASSET_FRAMEMAP] = {
        "framemaps", "1_skeletons", "base", "base", RSCACHE_DAT2_TABLE_SKELETONS, 0, -1, NULL },
    [CP_ASSET_INTERFACE] = {
        "interfaces", "3_interfaces", "interface", "ifb", RSCACHE_DAT2_TABLE_INTERFACES, 0, 14,
        &cp_codec_interface },
    [CP_ASSET_SYNTH] = {
        "synth", "4_soundeffects", "synth", "synth", RSCACHE_DAT2_TABLE_SOUND_EFFECTS, 0, -1, NULL },
    [CP_ASSET_MAP] = {
        "maps", "5_maps", "map", "map", RSCACHE_DAT2_TABLE_MAPS, CP_ASSET_ENCRYPTED, -1, &cp_codec_map },
    /* Archive 11 names 1,196 tracks and the music table holds 881, so the
     * verifier rejects it — 881 songs plus 315 jingles is exactly 1,196, so it
     * names both tables in one id space and neither table alone can check it.
     * Unclaimed rather than half-trusted; nothing content-side names a song. */
    [CP_ASSET_SONG] = {
        "songs", "6_musictracks", "song", "jmid", RSCACHE_DAT2_TABLE_MUSIC_TRACKS, 0, -1, NULL },
    [CP_ASSET_MODEL] = {
        "models", "7_models", "model", "model", RSCACHE_DAT2_TABLE_MODELS, 0, -1, NULL },
    [CP_ASSET_SPRITE] = {
        "sprites", "8_sprites", "sprite", "sprite", RSCACHE_DAT2_TABLE_SPRITES, 0, 12, &cp_codec_sprite },
    [CP_ASSET_TEXTURE] = {
        "textures", "9_textures", "texture", "texb", RSCACHE_DAT2_TABLE_TEXTURES, 0, -1,
        &cp_codec_texture },
    [CP_ASSET_BINARY] = {
        "binary", "10_binary", "binary", "bin", RSCACHE_DAT2_TABLE_BINARY, 0, -1, NULL },
    [CP_ASSET_JINGLE] = {
        "jingles", "11_musicjingles", "jingle", "jmid", RSCACHE_DAT2_TABLE_MUSIC_JINGLES, 0, -1, NULL },
    [CP_ASSET_SCRIPT] = {
        "scripts", "12_clientscripts", "script", "cs2b", RSCACHE_DAT2_TABLE_CLIENTSCRIPT, 0, -1, &cp_codec_script },
    [CP_ASSET_FONT] = {
        "fonts", "13_fonts", "font", "fmb", RSCACHE_DAT2_TABLE_FONTS, 0, -1, &cp_codec_font },
    [CP_ASSET_SAMPLE] = {
        "samples", "14_musicsamples", "sample", "sample", RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0, -1, NULL },
    [CP_ASSET_PATCH] = {
        "patches", "15_musicpatches", "patch", "patch", RSCACHE_DAT2_TABLE_MUSIC_PATCHES, 0, -1, NULL },
    /*
     * 2,057 of osrs239's 2,101 geography archives hold one file and 44 hold two or
     * three, so the table is split: a one-file archive is still a bare `.wmg` and a
     * multi-file one becomes a directory plus a filepack.
     *
     * Storing a multi-file archive whole made its `.wmg` the concatenation of its
     * members *plus the container's own trailer*, which is why exactly those 44 were
     * the only files a correct tile decoder could not read to exact consumption. The
     * grammar was never the problem; the container was.
     */
    [CP_ASSET_WORLDMAP_GEOGRAPHY] = {
        "worldmap/geography", "18_worldmapgeography", "worldmapgeo", "wmgb",
        RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY, CP_ASSET_SPLIT, -1, &cp_codec_worldmapgeo },
    /*
     * Two of its archives have a text form and two do not, so it carries both a
     * codec and the split flag: the codec gets first refusal on `details` and
     * `compositemap`, and `compositetexture` (PNGs) and `labels` fall through to
     * files plus a filepack.
     *
     * The codec used to be told it decoded 1 member in 104. It was being handed
     * every member of every archive, PNG bytes included — the archive is the *kind*
     * here, not one map.
     */
    [CP_ASSET_WORLDMAP_AREA] = {
        "worldmap/areas", "19_worldmap", "worldmaparea", "wmab", RSCACHE_DAT2_TABLE_WORLDMAP, CP_ASSET_SPLIT, -1,
        &cp_codec_worldmap },
    [CP_ASSET_WORLDMAP_GROUND] = {
        "worldmap/ground", "20_worldmapground", "worldmapground", "wmgr",
        RSCACHE_DAT2_TABLE_WORLDMAP_GROUND, 0, -1, NULL },
    /*
     * One archive per dbtable: the master index and one file per indexed column.
     *
     * The codec renders all of them as blocks in a single `.dbi`, so this is the
     * texture/interface shape rather than the dbindex-of-.bin-files it used to be.
     * `CP_ASSET_SPLIT` stays as the fallback: a member the codec cannot prove a
     * byte-exact round-trip for still lands as bytes plus a filepack.
     */
    [CP_ASSET_DBINDEX] = {
        "dbindex", "21_dbtableindex", "dbindex", "dbib", RSCACHE_DAT2_TABLE_DBTABLE_INDEX,
        CP_ASSET_SPLIT, -1, &cp_codec_dbindex },
    [CP_ASSET_ANIMAYA] = {
        "animayas", "22_animayas", "animaya", "animaya", RSCACHE_DAT2_TABLE_ANIMAYAS, 0, -1, NULL },
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

/**
 * A codec's extension must differ from the raw fallback's.
 *
 * They share a directory and a stem, so if they share an extension too then a
 * record whose decode declines writes its raw payload over the same name — and on
 * import, `read` declines, the caller falls back to the raw path, and the *text*
 * gets packed into the cache as if it were the payload. The archive that produces
 * is malformed in a way nothing downstream is expecting: it took out
 * RSCache_FileListNewFromDecode with a free of an unallocated pointer.
 *
 * Checked once at startup because it is a property of the register, not of any
 * cache — a new codec row that gets it wrong should fail immediately and loudly
 * rather than the first time a record declines.
 */
/** Every extension a table can write: its codec's one or two, then the raw one. */
static int
asset_extensions(const struct CP_Asset* asset, const char* out[3])
{
    int n = 0;

    if( asset->codec )
    {
        out[n++] = asset->codec->ext;
        if( asset->codec->ext2 )
            out[n++] = asset->codec->ext2;
    }
    out[n++] = asset->ext;
    return n;
}

/*
 * No extension belongs to two tables, and no table writes one extension two ways.
 *
 * This used to compare a codec's extension against its *own* row's raw fallback
 * and nothing else — so `.bin` being the fallback for nine tables and `.jmid` for
 * two passed silently. Those were harmless only because each table happens to own
 * a distinct directory, and nothing enforced that either.
 *
 * The pair `.jmid` on songs and jingles is the one sanctioned collision: they are
 * the same Jagex MIDI container, and naming one of them after a format it does not
 * contain would be the mistake `.ob2`-for-`.model` was avoided for. It is allowed
 * by name here so that adding a second such pair is a deliberate edit rather than a
 * silent pass.
 */
static int
extension_pair_allowed(const char* a, const char* b, const char* ext)
{
    if( strcmp(ext, "jmid") != 0 )
        return 0;
    return (strcmp(a, "songs") == 0 && strcmp(b, "jingles") == 0) ||
           (strcmp(a, "jingles") == 0 && strcmp(b, "songs") == 0);
}

static void
assert_extensions_distinct(void)
{
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        const struct CP_Asset* a = &g_assets[i];
        const char* mine[3];
        int n = asset_extensions(a, mine);

        for( int x = 0; x < n; x++ )
        {
            for( int y = x + 1; y < n; y++ )
            {
                if( strcmp(mine[x], mine[y]) == 0 )
                {
                    fprintf(stderr,
                            "cachepack: %s writes .%s two ways — a declined record "
                            "would be packed back as its own text\n",
                            a->dir, mine[x]);
                    abort();
                }
            }
        }

        for( int j = i + 1; j < CP_ASSET_COUNT; j++ )
        {
            const struct CP_Asset* b = &g_assets[j];
            const char* theirs[3];
            int m = asset_extensions(b, theirs);

            if( strcmp(a->dir, b->dir) == 0 )
            {
                fprintf(stderr, "cachepack: %s and %s share a directory\n", a->pack,
                        b->pack);
                abort();
            }
            for( int x = 0; x < n; x++ )
            {
                for( int y = 0; y < m; y++ )
                {
                    if( strcmp(mine[x], theirs[y]) != 0 )
                        continue;
                    if( extension_pair_allowed(a->dir, b->dir, mine[x]) )
                        continue;
                    fprintf(stderr,
                            "cachepack: .%s is written by both %s and %s — an "
                            "extension must name one thing\n",
                            mine[x], a->dir, b->dir);
                    abort();
                }
            }
        }
    }
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
    /*
     * EEXIST is success -- the ordinary mkdir -p rule, and on Windows it is
     * also what makes an ABSOLUTE path work: callers split on '/' from index 1,
     * so the first component of "C:/Users/..." is the bare drive designator
     * "C:", where MSVCRT's stat() fails with ENOENT and the mkdir then fails
     * with EEXIST. Without this, every absolute path is an error.
     */
    if( cp_mkdir(path) == 0 )
        return 0;
    return errno == EEXIST ? 0 : -1;
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
    /*
     * First *real* claim wins. `model_1234` is not a claim — it is the id spelled
     * twice, which is what the index writes for an id nothing has identified — so
     * it must not block a name derived from the configs. Anything else is someone's
     * decision and is left alone.
     */
    const char* existing = cp_asset_name_get(ctx, CP_ASSET_MODEL, model_id);
    if( existing && lc_pack_synthetic_id("model", existing) != model_id )
        return;
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
        /* The tree already named it — unless the "name" is `map_<id>` filler,
         * which is the index recording that nothing has identified the square yet
         * and must not block the identification. Same rule as `name_model`. */
        const char* existing = cp_asset_name_get(ctx, CP_ASSET_MAP, id);
        if( existing && lc_pack_synthetic_id("map", existing) != id )
            continue;
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

/* ---- the world map ------------------------------------------------------- */

/*
 * File id -> map name, from decoding the details archive. See the header.
 *
 * A flat table rather than a pack file because it is not an index anyone edits: it
 * is a *decode result*, re-derived on every run from the only place the names are
 * written down. What the tree keeps is the compack/filepack the export writes from
 * it, which is where a rename would go.
 */
enum
{
    CP_WORLDMAP_MAX_MAPS = 256,
};
static char g_worldmap_names[CP_WORLDMAP_MAX_MAPS][64];

const char*
cp_assets_worldmap_member(int file_id)
{
    if( file_id < 0 || file_id >= CP_WORLDMAP_MAX_MAPS || !g_worldmap_names[file_id][0] )
        return NULL;
    return g_worldmap_names[file_id];
}

void
cp_assets_name_worldmap(struct CP_Ctx* ctx)
{
    if( !ctx->cache_open )
        return;
    const struct CP_Asset* asset = cp_asset(CP_ASSET_WORLDMAP_AREA);
    int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT || !ctx->cache.disk->tables[table_id] )
        return;
    struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];

    /*
     * The kinds, in the order `class305` declares them. Only the first three are
     * whole archives here; `labels` is one archive per map and `area` is declared
     * by the client and never referenced, so neither gets a fixed slot.
     */
    static const char* const k_kinds[] = { "details", "compositemap", "compositetexture" };
    struct LC_Pack* pack = &ctx->names.asset_packs[CP_ASSET_WORLDMAP_AREA];
    int named = 0;

    for( int i = 0; i < rt->id_count; i++ )
    {
        int archive_id = rt->ids[i];
        char name[64];

        if( archive_id >= 0 && archive_id < pack->capacity && pack->names &&
            pack->names[archive_id] &&
            lc_pack_synthetic_id(asset->filler, pack->names[archive_id]) != archive_id )
            continue; /* someone named it; a re-seed never renames */

        if( archive_id < (int)(sizeof(k_kinds) / sizeof(k_kinds[0])) )
            snprintf(name, sizeof(name), "%s", k_kinds[archive_id]);
        else
            snprintf(name, sizeof(name), "labels_%d", archive_id);
        lc_pack_set(pack, archive_id, name);
        named++;
    }

    /* Then the maps, from the details archive itself. */
    memset(g_worldmap_names, 0, sizeof(g_worldmap_names));
    int maps = 0;
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, table_id, 0);
    if( archive && RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) )
    {
        struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
            archive->data, archive->data_size, archive->file_count);
        if( files )
        {
            for( int f = 0; f < files->file_count; f++ )
            {
                int file_id = archive->file_ids ? archive->file_ids[f] : f;
                struct RSCache_WorldMapArea area;

                memset(&area, 0, sizeof(area));
                if( RSCache_WorldMapAreaDecodeInplace(&area, file_id, files->files[f],
                                                      files->file_sizes[f]) &&
                    area.internal_name && area.internal_name[0] && file_id >= 0 &&
                    file_id < CP_WORLDMAP_MAX_MAPS )
                {
                    snprintf(g_worldmap_names[file_id], sizeof(g_worldmap_names[0]), "%s",
                             area.internal_name);
                    maps++;
                }
                RSCache_WorldMapAreaFreeInplace(&area);
            }
            RSCache_FileListFree(files);
        }
    }
    if( archive )
        RSCache_Dat2DiskArchiveFree(archive);

    if( named || maps )
        printf("  %-11s %6d world-map archives, %d maps named from the details\n", "worldmap",
               named, maps);
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

/**
 * Is any prefix of this path a name the index lists?
 *
 * `rel` is a file's path under the table's root, e.g. `npc/goblin.model`,
 * `m15_32.extra2.bin`, `bankmain/12.bmp`, `texture_0/47.texture`. The export names
 * a file after a pack entry and then decorates it — an extension, a second
 * extension, a directory of members or of sprite frames — so a file belongs to the
 * table exactly when some prefix of its path is an entry.
 *
 * Prefixes are tried longest-first by cutting at each `/` and `.` from the right,
 * which is deliberately generous: the cost of a false *orphan* is telling someone
 * to delete real content, and the cost of a false *match* is one unreported stale
 * file. Only the first is worth avoiding.
 */
static int
path_belongs(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    const char* rel)
{
    char stem[1600];
    snprintf(stem, sizeof(stem), "%s", rel);

    for( ;; )
    {
        /*
         * Resolve the stem to an id, then check the id still *answers to that name*.
         *
         * `cp_asset_name_find` alone is not enough, and the difference is the whole
         * check: it falls back to `<ns>_<id>`, so `interface_0.if` resolves to id 0
         * whatever the index calls interface 0 today. That is right for reading a
         * reference and wrong for deciding whether a file is live — it accepted 934
         * of the tree's 942 stale interface files, which were named
         * `interface_<id>` before archive 14 was decoded.
         */
        int found = cp_asset_name_find(ctx, id, stem);
        if( found >= 0 )
        {
            const char* current = cp_asset_name_get(ctx, id, found);
            if( current && strcmp(current, stem) == 0 )
                return 1;
        }
        /* A multi-file member is `<archive>/<member>`; its archive is the entry, and
         * the loop reaches that by cutting the member off. */
        char* cut = NULL;
        for( char* scan = stem; *scan; scan++ )
        {
            if( *scan == '/' || *scan == '.' )
                cut = scan;
        }
        if( !cut )
            return 0;
        *cut = '\0';
        if( !stem[0] )
            return 0;
    }
}

/**
 * Report files under `root` that no index entry accounts for.
 *
 * A rename leaves the old file behind: re-exporting interfaces under the cache's
 * own names wrote 968 files and left 942 older-named ones next to them, and nothing
 * said so. That matters beyond untidiness — the tree stops being a faithful
 * statement of the cache, and the next reader cannot tell which of two plausible
 * files is live.
 *
 * Reported, never deleted. The prefix rule below is generous on purpose, and a tool
 * that removes content on a heuristic is a worse trade than a line of output.
 */
static void
report_orphans(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    const char* root)
{
    /*
     * Depth-first, iteratively: a recursive walk over 61,615 models is a lot of
     * stack for no reason.
     *
     * The stack grows. It used to be `char dirs[64][1400]` on the reasoning that
     * "the asset tree is at most two levels deep" — but a DFS stack holds the
     * *breadth* it has discovered, not the depth. `sprites/` is 8,534 sibling
     * directories, so the first pass pushed 64 and silently dropped the rest, and
     * the report said 42 orphans where there were 8,554. Two runs disagreeing on
     * which files were orphaned is what made it visible; a wrong count on its own
     * looks exactly like a right one.
     */
    char (*dirs)[1400] = NULL;
    int depth = 0;
    int capacity = 0;
    int orphans = 0;
    char first[1500] = "";

    dirs = malloc(sizeof(*dirs) * 64);
    if( !dirs )
        return;
    capacity = 64;
    snprintf(dirs[depth++], sizeof(dirs[0]), "%s", root);
    while( depth > 0 )
    {
        char here[1400];
        snprintf(here, sizeof(here), "%s", dirs[--depth]);
        DIR* handle = opendir(here);
        struct dirent* entry;

        if( !handle )
            continue;
        while( (entry = readdir(handle)) )
        {
            if( entry->d_name[0] == '.' )
                continue;
            char path[1500];
            snprintf(path, sizeof(path), "%s/%s", here, entry->d_name);
            struct stat info;
            if( stat(path, &info) != 0 )
                continue;
            if( S_ISDIR(info.st_mode) )
            {
                if( depth == capacity )
                {
                    int next = capacity * 2;
                    char (*grown)[1400] = realloc(dirs, sizeof(*dirs) * (size_t)next);

                    if( !grown )
                    {
                        closedir(handle);
                        free(dirs);
                        return;
                    }
                    dirs = grown;
                    capacity = next;
                }
                snprintf(dirs[depth++], sizeof(dirs[0]), "%s", path);
                continue;
            }
            /* Relative to the table's root, which is what an index entry names. */
            const char* rel = path + strlen(root);
            while( *rel == '/' )
                rel++;
            if( path_belongs(ctx, id, rel) )
                continue;
            if( !orphans )
                snprintf(first, sizeof(first), "%s", rel);
            /*
             * Every path, not just a count and an example, when asked.
             *
             * A count is enough to notice the problem and not enough to act on it:
             * deleting requires knowing exactly which files, and re-deriving the rule
             * outside this function gets it wrong — a reimplementation that missed
             * the `<ns>_<id>` fallback called 20,266 of these orphaned instead of 42.
             */
            if( getenv("CACHEPACK_LIST_ORPHANS") )
                printf("    orphan %s/%s\n", cp_asset(id)->dir, rel);
            orphans++;
        }
        closedir(handle);
    }

    free(dirs);
    if( orphans )
        printf("  %-19s %6d file(s) no index entry accounts for, e.g. %s\n", "  orphaned",
               orphans, first);
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

        if( codec && codec->write )
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

        /*
         * Then the split, for an archive the codec declined.
         *
         * A table can have both. The world map's `details` and `compositemap`
         * archives read as text and its `compositetexture` PNGs do not, and only the
         * codec knows which is which — declining is how it says "not this one".
         */
        /*
         * Splitting needs more than one member.
         *
         * A single-member archive has no member list worth stating: its payload *is*
         * the file, and a `.filepack` naming one entry is a file that says nothing.
         * The world map has 51 single-member `labels` archives, and splitting them
         * produced 51 one-line filepacks beside 51 one-file directories.
         */
        if( (asset->flags & CP_ASSET_SPLIT) && archive->file_count > 1 )
        {
            /*
             * No text form, several members: each becomes a file under
             * `<dir>/<archive>/`, and `<dir>/<archive>.filepack` states which file id
             * each one is. The filepack sits in the table's folder beside the
             * archive, which is where every other index for this archive lives.
             */
            struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( files )
            {
                struct LC_Pack members;
                char stem[1600];
                snprintf(stem, sizeof(stem), "%s/%s", root, name);
                cp_member_pack_load(&members, stem, "filepack", asset->pack);
                if( ensure_dir_recursive(stem) == 0 )
                {
                    for( int f = 0; f < files->file_count; f++ )
                    {
                        int file_id = archive->file_ids ? archive->file_ids[f] : f;
                        const uint8_t* member = (const uint8_t*)files->files[f];
                        int member_size = files->file_sizes[f];
                        const char* listed = NULL;
                        char fallback[128];

                        if( file_id >= 0 && file_id < members.capacity && members.names )
                            listed = members.names[file_id];
                        if( !listed )
                        {
                            /* The world map names its members after the maps, so a
                             * compositemap member is `compositemap/main.bin` rather
                             * than `1/0.bin`. Every other table has nothing to say
                             * and falls back to the file id. */
                            const char* member_name =
                                id == CP_ASSET_WORLDMAP_AREA
                                    ? cp_assets_worldmap_member(file_id)
                                    : NULL;
                            char stem_buf[32];
                            if( !member_name )
                            {
                                snprintf(stem_buf, sizeof(stem_buf), "%d", file_id);
                                member_name = stem_buf;
                            }
                            snprintf(fallback, sizeof(fallback), "%s/%s.%s", name, member_name,
                                     cp_asset_extension(id, member, member_size));
                            listed = fallback;
                            lc_pack_set(&members, file_id, listed);
                        }
                        snprintf(path, sizeof(path), "%s/%s", root, listed);
                        char* slash = strrchr(path, '/');
                        if( slash )
                        {
                            *slash = '\0';
                            ensure_dir_recursive(path);
                            *slash = '/';
                        }
                        if( write_file(path, member, member_size) )
                        {
                            written++;
                            bytes += member_size;
                        }
                    }
                    cp_member_pack_save(&members, stem, "filepack");
                }
                lc_pack_free(&members);
                RSCache_FileListFree(files);
            }
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }


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
    report_orphans(ctx, id, root);
    *out_bytes += bytes;
    return written;
}

int
cp_assets_export(
    struct CP_Ctx* ctx,
    const char* assets_csv)
{
    assert_extensions_distinct();

    unsigned mask = 0;
    if( !parse_asset_list(assets_csv, &mask) )
        return 0;
    int all = mask == 0;

    /* Models first need their names, and the names come from the configs. */
    if( all || (mask & (1u << CP_ASSET_MODEL)) )
        cp_assets_name_models(ctx);
    if( all || (mask & (1u << CP_ASSET_MAP)) )
        cp_assets_name_maps(ctx);
    if( all || (mask & (1u << CP_ASSET_WORLDMAP_AREA)) )
        cp_assets_name_worldmap(ctx);

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

/* Forward: the fidelity pass reuses the import side's file reader, because
 * measuring a different code path than the build runs would measure the wrong
 * thing. */
static int
read_file(
    const char* path,
    uint8_t** out_data,
    int* out_size);

/** 1 when `path` can be opened for reading. */
static int
file_exists(const char* path)
{
    FILE* f = fopen(path, "rb");

    if( !f )
        return 0;
    fclose(f);
    return 1;
}

/**
 * 1 when the cache's reference table actually lists this archive.
 *
 * An index entry can name an archive the cache never had: `3_interfaces.pack`
 * carries 969 names against 968 archives, because gameval archive 14 names one
 * this revision does not ship. `archives` is indexed by archive id with
 * `index == -1` in the gaps — the convention `cp_reference_sync` maintains.
 */
static int
archive_in_cache(struct CP_Ctx* ctx, int table_id, int archive_id)
{
    struct RSCache_ReferenceTable* rt;

    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT || !ctx->cache.disk )
        return 0;
    rt = ctx->cache.disk->tables[table_id];
    if( !rt || !rt->archives || archive_id < 0 || archive_id >= rt->archive_count )
        return 0;
    return rt->archives[archive_id].index >= 0;
}

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
    /* The archive's pack name, so the reference table can carry the identifier
     * the client resolves by. NULL where the caller has no name for it. */
    const char* name,
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
    /* The archive's name is only known here, and without it the entry the sync
     * just made is unreachable by name (see cp_reference_set_name). */
    cp_reference_set_name(ctx, table_id, archive_id, name, out_dirty);
    free(container);
    return rc == 0;
}

static int
import_one(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    const char* out_cache_dir,
    int* out_files,
    int* out_missing)
{
    const struct CP_Asset* asset = cp_asset(id);
    int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
    int missing = 0;
    int declined = 0;
    int phantom = 0;
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
    /*
     * Warm this kind's files while the loop below consumes them.
     *
     * The loop reads one file per archive and each first touch costs ~12ms
     * (tool_prefetch.h has the measurement); the prefetcher pays that same cost
     * 16-way, so it runs about fifteen times faster than the loop that follows
     * it and stays comfortably ahead after the first few files. It hands back
     * no data and holds no state the loop can see — losing the race on a file
     * just means that one file is read cold, exactly as before.
     */
    struct Tool_Prefetch prefetch;
    tool_prefetch_begin(&prefetch, root);

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

        /* Codec first, then the split — the same order the export and the fidelity
         * pass use. A table can have both, and only the codec knows which of its
         * archives it owns. */
        if( codec && codec->read )
            payload = codec->read(ctx, archive_id, base, &file_ids, &file_count, &payload_size);

        if( !payload && (asset->flags & CP_ASSET_SPLIT) )
        {
            /*
             * Rebuilt from the filepack, not from a directory listing: it says which
             * file each member is and its ascending ids are the order the container
             * gets. A member it lists and the disk does not is reported — writing a
             * shorter archive would put every later member under the wrong id.
             */
            struct LC_Pack members;
            cp_member_pack_load(&members, base, "filepack", asset->pack);
            if( members.max > 0 )
            {
                struct RSCache_FileList list;
                memset(&list, 0, sizeof(list));
                list.files = calloc((size_t)members.max, sizeof(char*));
                list.file_sizes = calloc((size_t)members.max, sizeof(int));
                int* ids = calloc((size_t)members.max, sizeof(int));
                int missing = 0;

                if( list.files && list.file_sizes && ids )
                {
                    for( int member = 0; member < members.max; member++ )
                    {
                        const char* listed = members.names[member];
                        uint8_t* bytes = NULL;
                        int bytes_size = 0;
                        char member_path[1700];

                        if( !listed )
                            continue;
                        snprintf(member_path, sizeof(member_path), "%s/%s", root, listed);
                        if( !read_file(member_path, &bytes, &bytes_size) )
                        {
                            missing++;
                            continue;
                        }
                        list.files[list.file_count] = (char*)bytes;
                        list.file_sizes[list.file_count] = bytes_size;
                        ids[list.file_count] = member;
                        list.file_count++;
                    }
                    if( missing )
                        cp_warn(ctx, &ctx->warn_unresolved_name,
                                "cachepack: %s/%s.filepack lists %d member(s) that are not on "
                                "disk\n",
                                asset->dir, name, missing);
                    if( list.file_count > 0 )
                    {
                        uint32_t bound = RSCache_FileListEncodeBound(&list);
                        payload = malloc(bound ? bound : 1);
                        uint32_t encoded =
                            payload ? RSCache_FileListEncode(&list, payload, bound) : 0;
                        if( encoded == 0 )
                        {
                            free(payload);
                            payload = NULL;
                        }
                        else
                        {
                            payload_size = (int)encoded;
                            file_ids = ids;
                            file_count = list.file_count;
                            ids = NULL;
                        }
                    }
                }
                for( int f = 0; f < list.file_count; f++ )
                    free(list.files[f]);
                free(list.files);
                free(list.file_sizes);
                free(ids);
            }
            lc_pack_free(&members);
        }

        if( payload )
        {
            /* Handled above, and the member list came back with it — in *file order*,
             * so the container and the reference table's child list agree with what
             * the tree states rather than with a sort. */
        }
        else
        {
            /*
             * Single file: try every extension this table can produce.
             *
             * **A codec extension is deliberately not in this list.** The codec's own
             * `read` has already had first refusal on `<base>.<codec ext>`; if it
             * declined, that file is its *friendly* form and reading it here as a raw
             * payload would pack decompiled text into the cache as though it were
             * bytecode. Declining and keeping the cache's own bytes is the correct
             * answer, and the `codec-declined` counter is how it is reported rather
             * than hidden. (I added the codec extensions here and had to take them
             * back out — the exclusion is the design, not an oversight.)
             */
            static const char* const SNIFFED[] = { "png", "jpg", "gif", "mid",
                                                   "ogg", "ob2", "ob3", "model" };
            char path[1700];

            snprintf(path, sizeof(path), "%s.%s", base, asset->ext);
            read_file(path, &payload, &payload_size);
            for( size_t c = 0; c < sizeof(SNIFFED) / sizeof(SNIFFED[0]) && !payload; c++ )
            {
                snprintf(path, sizeof(path), "%s.%s", base, SNIFFED[c]);
                read_file(path, &payload, &payload_size);
            }
        }

        if( !payload )
        {
            /*
             * Nothing was read for an archive the index lists. Three different
             * things look identical here and only one of them is a defect, so they
             * are told apart rather than lumped into one count.
             *
             * This used to be a bare `continue`, which is a silent data path:
             * `pack --base` copies the base cache first, so a skipped archive keeps
             * the *base* bytes, and the output is byte-indistinguishable from one
             * where the file was found and unchanged. Rename an extension and the
             * tree's version stops shipping with nothing said. Two branches up, the
             * filepack path already warns that it "lists N member(s) that are not on
             * disk" — the asymmetry was the bug.
             */
            char probe[1700];
            int on_disk = 0;

            if( asset->codec )
            {
                snprintf(probe, sizeof(probe), "%s.%s", base, asset->codec->ext);
                on_disk = file_exists(probe);
            }
            if( on_disk )
            {
                /* The friendly form is there; the codec would not turn it back into
                 * bytes. For `script` that is the documented semantic bar — 219 of
                 * osrs239's clientscripts decompile but do not recompile — and
                 * keeping the base cache's bytes is the right answer. Reported
                 * because "the tree's copy was not used" should never be silent. */
                declined++;
            }
            else if( archive_in_cache(ctx, table_id, archive_id) )
            {
                /* The cache has this archive and the tree has no file for it. This
                 * is the real defect the bar exists to catch. */
                missing++;
                if( missing <= 5 )
                    fprintf(stderr, "cachepack: %s/%s is indexed and in the cache, "
                                    "but no file is on disk\n",
                            asset->dir, name);
            }
            else
            {
                /* The index names an archive the cache does not have either —
                 * `3_interfaces.pack` carries 969 names for 968 archives because
                 * gameval archive 14 names one this revision never shipped. Nothing
                 * to write and nothing wrong. */
                phantom++;
            }
            free(file_ids);
            continue;
        }

        uint32_t* key = NULL;
        if( asset->flags & CP_ASSET_ENCRYPTED )
            key = RSCache_Dat2DiskArchiveXteaKey(ctx->cache.disk, table_id, archive_id);

        if( !out_cache_dir )
        {
            /* --check-only: the question is whether a file backs every indexed
             * archive, and that has already been answered by getting here. Writing
             * would copy a 180 MB cache and emit 116,450 archives, which is far too
             * much for a routine test run — it was OOM-killed when this bar first
             * ran as a full pack. */
            written++;
        }
        else if( put_archive(ctx, out_cache_dir, table_id, archive_id, name, payload,
                             payload_size, file_ids, file_count, key, &dirty) )
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

    tool_prefetch_end(&prefetch);

    if( dirty && out_cache_dir && !cp_reference_write(ctx, out_cache_dir, table_id) )
        return 0;
    if( written && out_cache_dir )
        printf("  %-19s %6d archives%s\n", asset->dir, written,
               dirty ? " (reference table updated)" : "");
    if( missing || declined || phantom )
    {
        /* `import_one` is reached by `pack --assets` and by nothing else — `verify`
         * never calls it — so this is the only place the on-disk set is checked
         * against the index at all. */
        printf("  %-19s %6d missing, %d codec-declined, %d not in cache\n", asset->dir,
               missing, declined, phantom);
        *out_missing += missing;
    }
    *out_files += written;
    return 1;
}

int
cp_assets_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir)
{
    assert_extensions_distinct();

    int total = 0;
    int missing = 0;
    struct Tool_Progress progress;

    /*
     * Over asset kinds rather than archives: the archive count is only known
     * once each pack has been walked, and a bar that cannot start until the
     * work is half done is not a bar. Kinds are known up front and each one
     * names itself as it goes, which is the question being asked here anyway —
     * "what is it doing", not "how many bytes are left".
     */
    tool_progress_begin(&progress, "assets", CP_ASSET_COUNT);
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        /*
         * Per-kind wall time. The kinds are not remotely the same size — models
         * alone is tens of thousands of archives against a handful for fonts —
         * so a single total says nothing about where a slow run went, and
         * "optimise the packer" without this is guesswork.
         */
        time_t kind_started = time(NULL);
        int before = total;

        if( !import_one(ctx, i, out_cache_dir, &total, &missing) )
        {
            tool_progress_end(&progress);
            return 0;
        }
        tool_progress_step(&progress, 1);

        long kind_secs = (long)(time(NULL) - kind_started);
        if( kind_secs >= 1 || total != before )
            printf("  %-14s %6d archive(s) in %ld:%02lds\n", cp_asset(i)->dir, total - before,
                   kind_secs / 60, kind_secs % 60);
    }
    tool_progress_end(&progress);
    /*
     * Always printed, including the zero, so a bar can grep for the line rather
     * than for its absence — an absent line and a passing run look identical.
     */
    printf("%s %d asset archives, %d indexed but missing.\n",
           out_cache_dir ? "Imported" : "Checked", total, missing);
    return missing == 0;
}

/* ---- fidelity ----------------------------------------------------------- */

/*
 * The bar for the asset half of the tree, and it is not the same bar for every
 * table (docs/CONTENT_PACK_PLAN.md §0.2).
 *
 * **Whole-cache byte-identity is the wrong bar and is not used.** It is
 * unreachable for a reason that loses nothing: Jagex's packer does not write
 * config opcodes in ascending order and the encoders do, so records come back at
 * identical length with different byte order (EXCEPTIONS.md B3). Chasing it would
 * raise one column and change nothing observable.
 *
 * What the tables here divide into:
 *
 *   pass-through   No codec and one payload per archive: the file *is* the
 *                  payload, so there is nothing between the bytes on disk and the
 *                  bytes in the cache and nothing that could lose a field. These
 *                  are reported as pass-through rather than measured, because a
 *                  measurement of an identity function is theatre.
 *
 *   round-tripped  A codec. It re-derives the payload from its friendly form and
 *                  can therefore lose or reorder bytes — including, for a codec
 *                  that owns an archive's whole member list, losing a member. These
 *                  are measured, and the enforced bar is `differ == 0`: a length
 *                  change is a field (or a member) that did not survive, while a
 *                  same-length mismatch is ordering and costs nothing.
 *
 * Sprites are the worked example of why the bar is length rather than bytes: they
 * measure 100% same-length and 24-29% exact, because the decoder does not retain
 * the per-sprite flags byte and always writes row-major. Nothing is lost; the
 * bytes are simply in a different order.
 */

struct CP_AssetFidelity
{
    /** Archives examined. An archive counts once, as one payload, however many
     *  members its codec writes into one file. */
    int records;
    int exact;
    /** Same length, different bytes: ordering, not loss. */
    int same_len;
    /** Different length: a field did not survive. This is the one that fails. */
    int differ;
    /** Went through the friendly codec rather than the raw fallback. */
    int via_codec;
    /** The codec declined and the raw payload was used — normal, and counted so
     *  a `via_codec` of zero is distinguishable from a codec that never ran. */
    int declined;
    /** Could not be loaded at all: an XTEA-encrypted map with no key. A property
     *  of the dump, not of the codec, so it is excluded from the bar. */
    int unreadable;
};

/**
 * Round-trip one split archive: members to files plus a filepack, then back.
 *
 * Measured rather than assumed even though nothing transcodes, because the split and
 * the reassembly are a real transformation — the FileList is decoded and re-encoded,
 * and a member the filepack failed to record would come back missing.
 */
static void
verify_split_archive(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    struct RSCache_Dat2DiskArchive* archive,
    const char* root,
    int archive_id,
    struct CP_AssetFidelity* fid)
{
    const struct CP_Asset* asset = cp_asset(id);
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);

    fid->records++;
    if( !files )
    {
        fid->unreadable++;
        return;
    }

    char stem[1600];
    char dir[1700];
    snprintf(stem, sizeof(stem), "%s/%d", root, archive_id);
    snprintf(dir, sizeof(dir), "%s/%d", root, archive_id);
    struct LC_Pack members;
    memset(&members, 0, sizeof(members));
    snprintf(members.type, sizeof(members.type), "%s", asset->pack);

    if( ensure_dir_recursive(dir) == 0 )
    {
        for( int f = 0; f < files->file_count; f++ )
        {
            int file_id = archive->file_ids ? archive->file_ids[f] : f;
            char listed[128];
            char path[1800];
            snprintf(listed, sizeof(listed), "%d/%d.%s", archive_id, file_id,
                     cp_asset_extension(id, (const uint8_t*)files->files[f],
                                        files->file_sizes[f]));
            lc_pack_set(&members, file_id, listed);
            snprintf(path, sizeof(path), "%s/%s", root, listed);
            write_file(path, (const uint8_t*)files->files[f], files->file_sizes[f]);
        }
        cp_member_pack_save(&members, stem, "filepack");
    }
    RSCache_FileListFree(files);

    /* Read it back the way the build does. */
    struct LC_Pack back;
    cp_member_pack_load(&back, stem, "filepack", asset->pack);
    struct RSCache_FileList list;
    memset(&list, 0, sizeof(list));
    list.files = calloc((size_t)(back.max > 0 ? back.max : 1), sizeof(char*));
    list.file_sizes = calloc((size_t)(back.max > 0 ? back.max : 1), sizeof(int));
    for( int member = 0; member < back.max; member++ )
    {
        uint8_t* bytes = NULL;
        int bytes_size = 0;
        char path[1800];

        if( !back.names[member] )
            continue;
        snprintf(path, sizeof(path), "%s/%s", root, back.names[member]);
        if( !read_file(path, &bytes, &bytes_size) )
            continue;
        list.files[list.file_count] = (char*)bytes;
        list.file_sizes[list.file_count] = bytes_size;
        list.file_count++;
    }

    uint32_t bound = RSCache_FileListEncodeBound(&list);
    uint8_t* rebuilt = malloc(bound ? bound : 1);
    uint32_t written = rebuilt ? RSCache_FileListEncode(&list, rebuilt, bound) : 0;
    if( written == 0 )
        fid->differ++;
    else if( (int)written != archive->data_size )
        fid->differ++;
    else if( memcmp(rebuilt, archive->data, (size_t)written) == 0 )
        fid->exact++;
    else
        fid->same_len++;

    free(rebuilt);
    for( int f = 0; f < list.file_count; f++ )
        free(list.files[f]);
    free(list.files);
    free(list.file_sizes);
    lc_pack_free(&back);
    lc_pack_free(&members);
}

/** The read half, for an archive whose friendly form has already been written. */
static void
verify_codec_written(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    struct RSCache_Dat2DiskArchive* archive,
    const char* stem,
    struct CP_AssetFidelity* fid)
{
    const struct CP_AssetCodec* codec = asset_codec(cp_asset(id));
    int* ids = NULL;
    int count = 0;
    int size = 0;

    fid->records++;
    fid->via_codec++;
    uint8_t* rebuilt = codec->read(ctx, archive->archive_id, stem, &ids, &count, &size);
    if( !rebuilt )
    {
        /* Written but not readable back is a real defect: the exporter would have put
         * the friendly form in the tree and the build would then fall back to a raw
         * file that is not there. */
        fid->differ++;
        return;
    }
    if( size != archive->data_size )
        fid->differ++;
    else if( memcmp(rebuilt, archive->data, (size_t)size) == 0 )
        fid->exact++;
    else
        fid->same_len++;
    free(rebuilt);
    free(ids);
}


static int
verify_one_asset(
    struct CP_Ctx* ctx,
    enum CP_AssetId id,
    const char* tmpdir)
{
    const struct CP_Asset* asset = cp_asset(id);
    int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        printf("  %-19s %8s  absent from this cache\n", asset->dir, "-");
        return 1;
    }
    struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];
    if( !rt )
    {
        printf("  %-19s %8s  no reference table\n", asset->dir, "-");
        return 1;
    }

    const struct CP_AssetCodec* codec = asset_codec(asset);
    int split = (asset->flags & CP_ASSET_SPLIT) != 0;
    if( !codec && !split )
    {
        /* Nothing sits between the file and the payload. Stated, not measured. */
        printf("  %-19s %8d  pass-through (payload is the file)\n", asset->dir, rt->id_count);
        return 1;
    }

    char root[1500];
    snprintf(root, sizeof(root), "%s/%s", tmpdir, asset->pack);
    if( ensure_dir_recursive(root) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", root);
        return 0;
    }

    struct CP_AssetFidelity fid;
    memset(&fid, 0, sizeof(fid));

    for( int a = 0; a < rt->id_count; a++ )
    {
        int archive_id = rt->ids[a];
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, table_id, archive_id);
        if( !archive )
        {
            fid.unreadable++;
            continue;
        }
        if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            fid.unreadable++;
            continue;
        }

        char path[1600];
        snprintf(path, sizeof(path), "%s/%d", root, archive_id);
        /* Codec first, then the split — the same order the export uses, so this
         * measures the path the build actually takes. A table can have both. */
        if( codec && codec->write && codec->read &&
            codec->write(ctx, archive_id, (const uint8_t*)archive->data, archive->data_size,
                         archive->file_ids, archive->file_count, path) )
            verify_codec_written(ctx, id, archive, path, &fid);
        else if( split )
            verify_split_archive(ctx, id, archive, root, archive_id, &fid);
        else
        {
            /* A codec with only one half: the raw payload is what gets written. */
            fid.records++;
            fid.exact++;
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }

    /*
     * A semantic-only codec is exempt, and the exemption is reported rather than
     * folded away — a table nobody is holding to a byte bar should look different
     * in the output from one that met it.
     */
    int semantic = codec && codec->semantic_only;
    printf("  %-19s %8d %8d %8d %8d   %6d %6d %6d%s\n", asset->dir, fid.records, fid.exact,
           fid.same_len, fid.differ, fid.via_codec, fid.declined, fid.unreadable,
           semantic ? "  (semantic bar; see test_cs2)" : fid.differ ? "  <-- length changed" : "");
    return semantic || fid.differ == 0;
}

int
cp_assets_verify(
    struct CP_Ctx* ctx,
    const char* assets_csv,
    const char* tmpdir)
{
    assert_extensions_distinct();

    unsigned mask = 0;
    if( !parse_asset_list(assets_csv, &mask) )
        return 0;
    int all = mask == 0;

    /* The same naming pass the export does, so a codec that writes into a
     * name-derived path is exercised the way the build exercises it. */
    if( all || (mask & (1u << CP_ASSET_MODEL)) )
        cp_assets_name_models(ctx);
    if( all || (mask & (1u << CP_ASSET_MAP)) )
        cp_assets_name_maps(ctx);
    if( all || (mask & (1u << CP_ASSET_WORLDMAP_AREA)) )
        cp_assets_name_worldmap(ctx);

    if( ensure_dir_recursive(tmpdir) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create the scratch directory %s\n", tmpdir);
        return 0;
    }
    printf("Asset fidelity (scratch: %s)\n", tmpdir);
    printf("  %-19s %8s %8s %8s %8s   %6s %6s %6s\n", "table", "records", "exact", "same-len",
           "differ", "codec", "declin", "unread");

    int ok = 1;
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        if( !all && !(mask & (1u << i)) )
            continue;
        if( !verify_one_asset(ctx, i, tmpdir) )
            ok = 0;
    }

    printf("\n`differ` is the only column that fails a run: it means the payload came back a\n"
           "different length, i.e. a field did not survive. `same-len` is byte ordering —\n"
           "sprites are 100%% same-length and 24-29%% exact, and lose nothing.\n"
           "`unread` is almost always an XTEA-encrypted map square whose key is not in\n"
           "xteas.json; that is a property of the dump, not of the codec.\n");
    return ok;
}
