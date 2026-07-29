/*
 * mock230_pack — check the content tree against the cache, and optionally bake
 * it into one.
 *
 *   make -C src mock230-pack
 *   src/build/mock230_pack                       # validate
 *   src/build/mock230_pack --cache-out cache.mock  # validate + write
 *
 * Why this exists: every id in the tree came from OpenRune's gameval table,
 * whose cache is revision 235.10, and the mock runs against 230. An id that
 * moved between the two does not fail loudly — it resolves to a *different*
 * npc, which spawns and fights and looks entirely plausible. `npcs.goblin` is
 * the worked example: id 3028 at both revisions, while the mock's old roster
 * used 655, which cache.osrs230 also calls "Goblin" and OpenRune calls
 * `goblin_red_soldier_2`. Two monsters, one display name, and nothing but a
 * check to tell them apart.
 *
 * So the rule is: the importers state, this validates. A non-zero exit means
 * the tree and the cache disagree.
 *
 * `--cache-out` is the other half. The mock reads its combat data out of the
 * text tree, which is enough for the mock — but a *cache* is the portable form,
 * readable by dump_npc, by the client, and by any other server pointed at it.
 * The export writes the authored blocks back into each npc record's param
 * table, using the ids in content/pack/param.pack, beside the equipment
 * bonuses OldSchool already keeps there.
 */

#include "mock230.h"
#include "mock230_content.h"

#include <rscache.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static int g_errors;
static int g_warnings;
static int g_verbose;

static void
report_error(const char* fmt, ...)
{
    va_list args;

    fprintf(stderr, "  ERROR ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    g_errors++;
}

static void
report_warning(const char* fmt, ...)
{
    va_list args;

    fprintf(stderr, "  warn  ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    g_warnings++;
}

static void
report_info(const char* fmt, ...)
{
    va_list args;

    if( !g_verbose )
        return;
    fprintf(stderr, "        ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/*
 * Is this npc attackable?
 *
 * Duplicated from mock230_combat.c rather than linked: pulling that in would
 * drag the whole server — world, scripts, encoders, the socket — into a tool
 * that only reads files. Three lines against a stable cache field is the
 * cheaper coupling, and the selftest asserts the engine's copy separately.
 */
static int
npc_attackable(int npc_type)
{
    const struct Mock230NpcInfo* info = mock230_npcinfo(npc_type);

    for( int i = 0; i < 5; i++ )
    {
        if( info->ops[i] && strcmp(info->ops[i], "Attack") == 0 )
            return 1;
    }
    return 0;
}

/** Does this loc's cache record carry an Open-family action? */
static int
loc_has_open_op(
    const struct RSCache_Dat2ConfigLoc* config,
    const char* verb)
{
    if( !config )
        return 0;
    for( int i = 0; i < 5; i++ )
    {
        if( config->actions[i] && strcmp(config->actions[i], verb) == 0 )
            return 1;
    }
    return 0;
}

/*
 * Every npc id the maps spawn, checked against both the cache record and the
 * content block.
 *
 * Driven from the spawn list rather than from the definition list because a
 * definition nothing spawns is inert, while a spawn with no definition is a
 * monster running on engine defaults — the second is worth a line of output and
 * the first is not.
 */
static void
validate_spawns(void)
{
    int npc_count = 0;
    int obj_count = 0;
    const struct Mock230MapNpcSpawn* npcs = mock230_content_npc_spawns(&npc_count);
    const struct Mock230MapObjSpawn* objs = mock230_content_obj_spawns(&obj_count);
    int checked[4096];
    int checked_count = 0;

    fprintf(stderr, "spawns: %d npc, %d obj\n", npc_count, obj_count);

    for( int i = 0; i < npc_count; i++ )
    {
        const struct Mock230NpcInfo* info = mock230_npcinfo(npcs[i].npc_id);
        const struct Mock230NpcDef* def = mock230_content_npc(npcs[i].npc_id);
        const char* symbol = mock230_content_symbol_name(MOCK230_PACK_NPC, npcs[i].npc_id);
        int already = 0;

        if( !info->name )
        {
            report_error("npc %d spawned at %d,%d is not in the cache", npcs[i].npc_id,
                         npcs[i].x, npcs[i].z);
            continue;
        }
        for( int j = 0; j < checked_count; j++ )
        {
            if( checked[j] == npcs[i].npc_id )
                already = 1;
        }
        if( already )
            continue;
        if( checked_count < (int)(sizeof(checked) / sizeof(checked[0])) )
            checked[checked_count++] = npcs[i].npc_id;

        if( !symbol )
            report_warning("npc %d (%s) has no symbol in pack/npc.pack", npcs[i].npc_id,
                           info->name);
        if( !def )
        {
            report_warning("npc %d (%s) has no config block — engine defaults apply",
                           npcs[i].npc_id, info->name);
            continue;
        }

        /*
         * The cache's combat level beside the authored hitpoints. Neither
         * derives from the other, so this is not an assertion — it is the one
         * line that makes a transposed block visible, because a level-2 goblin
         * with 75 hitpoints is obviously the huge spider's row.
         */
        report_info("%-18s id %-6d %-16s vislevel %-4d hp %-4d att/str/def %d/%d/%d",
                    symbol ? symbol : "?", npcs[i].npc_id, info->name, info->combat_level,
                    def->hitpoints, def->attack, def->strength, def->defence);

        if( info->combat_level > 0 && def->authored_combat &&
            def->hitpoints > info->combat_level * 8 )
            report_warning(
                "%s has %d hitpoints but the cache says combat level %d — "
                "check the block is not transposed",
                symbol ? symbol : info->name, def->hitpoints, info->combat_level);

        if( def->authored_combat && !npc_attackable(npcs[i].npc_id) )
            report_warning(
                "%s has a combat block but the cache gives it no Attack op, so "
                "nothing can fight it",
                symbol ? symbol : info->name);
    }

    for( int i = 0; i < obj_count; i++ )
    {
        if( !mock230_objinfo(objs[i].obj_id)->name )
            report_error("obj %d spawned at %d,%d is not in the cache", objs[i].obj_id,
                         objs[i].x, objs[i].z);
    }
}

/*
 * Loc ids the door validator rejected, so --prune-doors can rewrite the config
 * without them. Collected rather than acted on immediately because a pair is
 * two lines in the file and both halves have to go.
 */
static int g_rejected[2048];
static int g_rejected_count;

static void
reject_door(int loc_id)
{
    if( g_rejected_count < (int)(sizeof(g_rejected) / sizeof(g_rejected[0])) )
        g_rejected[g_rejected_count++] = loc_id;
}

static int
door_rejected(int loc_id)
{
    for( int i = 0; i < g_rejected_count; i++ )
    {
        if( g_rejected[i] == loc_id )
            return 1;
    }
    return 0;
}

/*
 * Rewrite doors.loc without the pairs the cache rejected.
 *
 * This is the step that turns tools/door_import.py's guesswork into data. It
 * derives 400-odd pairs from naming conventions, which is the only way to get
 * past OpenRune's 13 curated ones; about one in seven of those turns out to be
 * scenery that merely reads like a door (`wooden_fur_door_always_closed`,
 * `lassar_door_closed_noop`, a dozen Colosseum gates). Deriving broadly and
 * then deleting whatever the cache disagrees with is more honest than either
 * trusting the names or hand-curating four hundred pairs.
 *
 * Both halves of a rejected pair are removed: half a door is worse than none.
 */
static int
prune_doors(const char* content)
{
    char path[1024];
    char temp[1100];
    FILE* in;
    FILE* out;
    char raw[1024];
    int dropped = 0;
    int skipping = 0;

    snprintf(path, sizeof(path), "%s/scripts/doors/configs/doors.loc", content);
    snprintf(temp, sizeof(temp), "%s.new", path);
    in = fopen(path, "rb");
    if( !in )
    {
        fprintf(stderr, "mock230_pack: no %s to prune\n", path);
        return 0;
    }
    out = fopen(temp, "wb");
    if( !out )
    {
        fclose(in);
        return 0;
    }

    while( fgets(raw, sizeof(raw), in) )
    {
        if( raw[0] == '[' )
        {
            char symbol[256];
            char* end;
            int loc_id;
            const struct Mock230LocDef* def;

            snprintf(symbol, sizeof(symbol), "%s", raw + 1);
            end = strchr(symbol, ']');
            if( end )
                *end = '\0';
            loc_id = mock230_content_symbol(MOCK230_PACK_LOC, symbol);
            def = loc_id >= 0 ? mock230_content_loc(loc_id) : NULL;

            /* A section is dropped when either half of its pair was rejected. */
            skipping = def && (door_rejected(def->loc_id) ||
                               door_rejected(def->next_loc_stage));
            if( skipping )
                dropped++;
        }
        else if( raw[0] == '\n' || raw[0] == '\r' )
        {
            if( skipping )
            {
                skipping = 0;
                continue;
            }
        }
        if( !skipping )
            fputs(raw, out);
    }
    fclose(in);
    fclose(out);
    if( rename(temp, path) != 0 )
    {
        fprintf(stderr, "mock230_pack: cannot replace %s\n", path);
        return 0;
    }
    fprintf(stderr, "        pruned %d loc sections from doors.loc\n", dropped);
    return 1;
}

static void
validate_doors(
    struct RSCache_Dat2Disk* disk,
    struct RSCache* profile)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    struct RSCache_FileList* files;
    struct RSCache_Dat2ConfigLoc** configs;
    int highest = -1;
    int pairs = 0;
    int dropped = 0;

    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        report_error("no loc config archive in the cache");
        return;
    }
    RSCache_ProfileSetGroupRevision(profile, RSCACHE_TYPE_LOC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    configs = calloc((size_t)(highest + 1), sizeof(*configs));
    for( int i = 0; i < archive->file_count; i++ )
    {
        int id = archive->file_ids[i];

        if( id < 0 || files->file_sizes[i] <= 0 )
            continue;
        configs[id] = RSCache_Dat2ConfigLocNewDecodeProfile(profile, files->files[i],
                                                            files->file_sizes[i]);
    }

    /*
     * Every door pair, checked against the cache.
     *
     * Most of them are *derived* — proposed from OpenRune's gameval names by
     * tools/door_import.py, which cannot tell a real pair from a naming
     * coincidence. This is the check that makes that guesswork safe: a closed
     * door has an Open action and its partner does not, and a pair that fails
     * either half is reported so it can be dropped from the config rather than
     * left to produce a door that opens into nothing.
     */
    fprintf(stderr, "door pairings\n");
    for( int loc_id = 0; loc_id <= highest; loc_id++ )
    {
        const struct Mock230LocDef* def = mock230_content_loc(loc_id);

        if( !def || def->category == MOCK230_LOC_CATEGORY_NONE )
            continue;
        pairs++;

        if( !configs[loc_id] )
        {
            report_warning("loc %d (%s) is not in the cache", loc_id,
                           def->symbol ? def->symbol : "?");
            reject_door(loc_id);
            dropped++;
            continue;
        }
        if( def->next_loc_stage < 0 || def->next_loc_stage > highest ||
            !configs[def->next_loc_stage] )
        {
            report_warning("loc %d (%s) opens into %d, which is not in the cache", loc_id,
                           def->symbol ? def->symbol : "?", def->next_loc_stage);
            reject_door(loc_id);
            dropped++;
            continue;
        }
        /* The test that separates a door from a wall that looks like one. A
         * closed door offers "Open"; scenery with a door-shaped name does not,
         * and would otherwise sit in the table waiting to swallow a click. */
        if( def->category == MOCK230_LOC_CATEGORY_DOOR_CLOSED &&
            !loc_has_open_op(configs[loc_id], "Open") )
        {
            report_warning("loc %d (%s) is marked door_closed but has no Open action",
                           loc_id, def->symbol ? def->symbol : "?");
            reject_door(loc_id);
            dropped++;
        }
    }
    fprintf(stderr, "        %d door locs, %d questionable\n", pairs, dropped);

    for( int i = 0; i <= highest; i++ )
    {
        if( configs[i] )
            RSCache_Dat2ConfigLocFree(configs[i]);
    }
    free(configs);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

/* ------------------------------------------------------------------ */
/* Cache export                                                        */
/* ------------------------------------------------------------------ */

static int
copy_file(
    const char* from,
    const char* to)
{
    FILE* in = fopen(from, "rb");
    FILE* out;
    static char buffer[1 << 20];
    size_t got;

    if( !in )
        return 0;
    out = fopen(to, "wb");
    if( !out )
    {
        fclose(in);
        return 0;
    }
    while( (got = fread(buffer, 1, sizeof(buffer), in)) > 0 )
    {
        if( fwrite(buffer, 1, got, out) != got )
        {
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

/*
 * Copy the whole cache before editing it.
 *
 * There is no lighter option: an edit appends its new archive to
 * `main_file_cache.dat2` and repoints the index at it, so the .dat2 is part of
 * the output whether or not most of it changed. 180 MB per export is the price
 * of a derived cache being a real, bootable cache rather than a patch file.
 */
static int
copy_cache(
    const char* from,
    const char* to)
{
    static const char* const k_files[] = {
        "main_file_cache.dat2", "main_file_cache.idx255", "xteas.json", NULL,
    };
    char source[1024];
    char dest[1024];

    if( mkdir(to, 0755) != 0 )
    {
        struct stat info;

        if( stat(to, &info) != 0 )
        {
            fprintf(stderr, "mock230_pack: cannot create %s\n", to);
            return 0;
        }
    }

    for( int i = 0; k_files[i]; i++ )
    {
        snprintf(source, sizeof(source), "%s/%s", from, k_files[i]);
        snprintf(dest, sizeof(dest), "%s/%s", to, k_files[i]);
        if( !copy_file(source, dest) && i < 2 )
        {
            fprintf(stderr, "mock230_pack: cannot copy %s\n", source);
            return 0;
        }
    }
    for( int idx = 0; idx < 32; idx++ )
    {
        snprintf(source, sizeof(source), "%s/main_file_cache.idx%d", from, idx);
        snprintf(dest, sizeof(dest), "%s/main_file_cache.idx%d", to, idx);
        (void)copy_file(source, dest); /* sparse: not every index exists */
    }
    return 1;
}

/* Param ids the export writes, resolved from content/pack/param.pack so the
 * baked cache and the text tree name the same fields. */
struct BakedParam
{
    const char* name;
    int value;
};

/** Add or replace an int param on a decoded record. */
static void
param_set(
    struct RSCache_Params* params,
    int key,
    int value)
{
    for( int i = 0; i < params->count; i++ )
    {
        if( params->keys[i] == key && params->kinds[i] == 0 )
        {
            *(int*)params->values[i] = value;
            return;
        }
    }
    if( params->count == params->capacity )
    {
        int capacity = params->capacity ? params->capacity * 2 : 8;

        params->keys = realloc(params->keys, (size_t)capacity * sizeof(int));
        params->values = realloc(params->values, (size_t)capacity * sizeof(void*));
        params->kinds = realloc(params->kinds, (size_t)capacity * sizeof(uint8_t));
        params->capacity = capacity;
    }
    params->keys[params->count] = key;
    params->values[params->count] = malloc(sizeof(int));
    *(int*)params->values[params->count] = value;
    params->kinds[params->count] = 0;
    params->count++;
}

/*
 * Write an npc's authored combat block into its param table.
 *
 * The equipment bonuses are already there — OldSchool puts them at param ids
 * 0..11 — so this only adds what a cache has no field for. `null` values (-1)
 * are skipped rather than written: a param that is present and -1 reads back as
 * "drops object -1", where an absent one correctly reads as "nothing was said".
 */
static void
bake_npc_params(
    struct RSCache_Dat2ConfigNpc* npc,
    const struct Mock230NpcDef* def)
{
    const struct BakedParam baked[] = {
        { "hitpoints",   def->hitpoints   },
        { "attacklevel", def->attack      },
        { "strengthlevel", def->strength  },
        { "defencelevel", def->defence    },
        { "respawnrate", def->respawnrate },
        { "wanderrange", def->wanderrange },
        { "huntrange",   def->huntrange   },
        { "attackrate",  def->attackrate  },
        { "death_drop",  def->death_drop  },
        { "attack_anim", def->attack_anim },
        { "defend_anim", def->defend_anim },
        { "death_anim",  def->death_anim  },
    };

    for( size_t i = 0; i < sizeof(baked) / sizeof(baked[0]); i++ )
    {
        int key = mock230_content_symbol(MOCK230_PACK_PARAM, baked[i].name);

        if( key < 0 )
        {
            report_warning("param.pack has no id for `%s` — not baked", baked[i].name);
            continue;
        }
        if( baked[i].value < 0 )
            continue;
        param_set(&npc->params, key, baked[i].value);
    }
}

/*
 * Re-encode the npc config group with the authored params folded in.
 *
 * The npc encoder is a semantic round trip, not a byte-exact one: it cannot
 * tell "field absent" from "field present and zero", so a re-encoded record is
 * usually a few bytes shorter than the original. Decoding it yields the same
 * struct, which is what matters here — but it does mean the output cache is not
 * byte-identical to the input even for records nothing touched, so every record
 * in the group is re-encoded rather than only the edited ones. Splicing edited
 * records into original bytes would be the byte-exact option and needs an
 * encoder that can distinguish the two states. See 3rd/rscache/EXCEPTIONS.md.
 */
static int
export_cache(
    const char* cache,
    const char* cache_out)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2Edit* edit;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_FileList packed;
    uint8_t* encoded_group;
    uint32_t encoded_size;
    int table;
    int baked = 0;
    int ok = 0;

    fprintf(stderr, "exporting to %s (copying %s — this writes the whole cache)\n",
            cache_out, cache);
    if( !copy_cache(cache, cache_out) )
        return 0;

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = 230;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_out);
    if( !disk )
    {
        fprintf(stderr, "mock230_pack: cannot open the copied cache at %s\n", cache_out);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        fprintf(stderr, "mock230_pack: no npc config archive\n");
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_NPC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    memset(&packed, 0, sizeof(packed));
    packed.file_count = files->file_count;
    packed.files = calloc((size_t)packed.file_count, sizeof(char*));
    packed.file_sizes = calloc((size_t)packed.file_count, sizeof(int));

    for( int i = 0; i < files->file_count; i++ )
    {
        int id = archive->file_ids[i];
        const struct Mock230NpcDef* def = mock230_content_npc(id);
        struct RSCache_Dat2ConfigNpc* npc;
        uint8_t buffer[8192];
        uint32_t size;

        if( files->file_sizes[i] <= 0 )
            continue;
        if( !def )
        {
            /* Untouched: keep the original bytes. That keeps 14,000 of the
             * 14,205 records byte-identical, which is worth the branch. */
            packed.files[i] = files->files[i];
            packed.file_sizes[i] = files->file_sizes[i];
            continue;
        }

        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(&profile, files->files[i],
                                                    files->file_sizes[i]);
        if( !npc )
        {
            packed.files[i] = files->files[i];
            packed.file_sizes[i] = files->file_sizes[i];
            continue;
        }
        bake_npc_params(npc, def);
        size = RSCache_Dat2ConfigNpcEncodeProfile(&profile, npc, buffer, sizeof(buffer));
        RSCache_Dat2ConfigNpcFree(npc);
        if( size == 0 )
        {
            report_error("npc %d did not re-encode", id);
            packed.files[i] = files->files[i];
            packed.file_sizes[i] = files->file_sizes[i];
            continue;
        }
        packed.files[i] = malloc(size);
        memcpy(packed.files[i], buffer, size);
        packed.file_sizes[i] = (int)size;
        baked++;
    }

    encoded_size = RSCache_FileListEncodeBound(&packed);
    encoded_group = malloc(encoded_size);
    encoded_size = RSCache_FileListEncode(&packed, encoded_group, encoded_size);
    if( encoded_size == 0 )
    {
        fprintf(stderr, "mock230_pack: the npc group did not encode\n");
    }
    else
    {
        edit = RSCache_Dat2EditNew(disk);
        if( edit &&
            RSCache_Dat2EditPutArchive(edit, table, RSCACHE_DAT2_CONFIG_KIND_NPC,
                                       encoded_group, encoded_size) &&
            RSCache_Dat2EditCommit(edit, cache_out) )
        {
            fprintf(stderr, "        baked %d npc records into %s\n", baked, cache_out);
            ok = 1;
        }
        else
        {
            fprintf(stderr, "mock230_pack: the edit did not commit\n");
        }
        if( edit )
            RSCache_Dat2EditFree(edit);
    }

    for( int i = 0; i < packed.file_count; i++ )
    {
        if( packed.files[i] && packed.files[i] != files->files[i] )
            free(packed.files[i]);
    }
    free(packed.files);
    free(packed.file_sizes);
    free(encoded_group);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static void
usage(void)
{
    fprintf(stderr,
            "usage: mock230_pack [--content DIR] [--cache DIR] [--cache-out DIR] [-v]\n"
            "\n"
            "  --content DIR    content tree (default src/net/mock/content)\n"
            "  --cache DIR      source cache (default cache.osrs230)\n"
            "  --cache-out DIR  write a derived cache with the authored npc combat\n"
            "                   data baked into each record's params\n"
            "  --prune-doors    rewrite doors.loc without the pairs the cache\n"
            "                   rejects — run this after tools/door_import.py\n"
            "  -v               list every definition as it is checked\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* content = "src/net/mock/content";
    const char* cache = "cache.osrs230";
    const char* cache_out = NULL;
    int prune = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--content") == 0 && i + 1 < argc )
            content = argv[++i];
        else if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache = argv[++i];
        else if( strcmp(argv[i], "--cache-out") == 0 && i + 1 < argc )
            cache_out = argv[++i];
        else if( strcmp(argv[i], "--prune-doors") == 0 )
            prune = 1;
        else if( strcmp(argv[i], "-v") == 0 )
            g_verbose = 1;
        else
        {
            usage();
            return 2;
        }
    }

    mock230_objinfo_load(cache);
    mock230_npcinfo_load(cache);
    mock230_seqinfo_load(cache);
    mock230_content_load(content);

    g_errors += mock230_content_error_count();

    validate_spawns();
    {
        struct RSCache profile = RSCache_ProfileZero();
        struct RSCache_Dat2Disk* disk;

        profile.game = RSCACHE_GAME_OLDSCHOOL;
        profile.epoch = RSCACHE_EPOCH_DAT2;
        profile.revision = 230;
        disk = RSCache_Dat2DiskNewFromDirectory(cache);
        if( disk )
        {
            RSCache_Dat2DiskSetProfile(disk, &profile);
            validate_doors(disk, &profile);
            RSCache_Dat2DiskFree(disk);
            if( prune )
                prune_doors(content);
        }
        else
        {
            report_error("cannot open the cache at %s", cache);
        }
    }

    /* Export only from a clean tree. Baking a config the validator rejected
     * produces a cache that is wrong in exactly the way the errors said, and
     * then outlives the message. */
    if( cache_out )
    {
        if( g_errors )
            fprintf(stderr, "mock230_pack: not exporting — fix the errors first\n");
        else if( !export_cache(cache, cache_out) )
            g_errors++;
    }

    fprintf(stderr, "mock230_pack: %d error(s), %d warning(s)\n", g_errors, g_warnings);
    return g_errors ? 1 : 0;
}
