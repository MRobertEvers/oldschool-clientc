/*
 * mock230_pack — check the content tree against the cache, and optionally bake
 * server-authoritative overlays into a derived cache.
 *
 *   make -C src mock230-pack
 *   src/build/mock230_pack                       # validate
 *   src/build/mock230_pack --cache-out cache.mock  # validate + bake
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
 * `--cache-out` is the revision-loop pack step. The mock reads overlays from
 * server/scripts feature configs at boot, which is enough for the mock — but a
 * *cache* is the portable form. The export folds authored fields that have a
 * home in the client record into each record's param table:
 *
 *   - npc combat overlays (hitpoints, attacklevel, death_drop, anims, ...)
 *   - loc door stages (`next_loc_stage` -> param of the same name)
 *
 * using the ids in pack/param.pack. Pure server data (RuneScript, enums,
 * drop tables) stays in `server/` and is not baked.
 */

#include "content/content_fields.h"
#include "content/content_register.h"
#include "mock230.h"
#include "mock230_content.h"
#include "mock230_ids.h"

#include <rscache.h>

#include <stdarg.h>
#include <stddef.h>
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
/* The tree being checked. Set once from `--content`, because the field register is
 * read per record and threading a path through the encoders would touch every
 * signature between here and the baker for no gain. */
static const char* g_content_dir = "OSRS-Content/osrs239-content";

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

        /*
         * A spawn whose name belongs to somewhere else.
         *
         * OldSchool's gameval table qualifies most npcs by the content that
         * introduced them, so a roster imported by id rather than by name can
         * end up standing `sos_pest_giantspider1` — Pest Control's level-50
         * one — in the Lumbridge Swamp beside the level-2 `giantspider1`, and
         * nothing says so. That is precisely what it was doing, four times over,
         * until spawns moved to a name-keyed file
         * (docs/LOSTCITY_PORT_TRIAGE.md §10.2).
         *
         * The signal is narrow on purpose. A prefix alone means nothing — plenty
         * of variants are legitimately used outside the area they are named for —
         * so this fires only when the *un-prefixed* creature is also spawned in
         * this world. Having both is what says one of them is probably a
         * mis-import. Once God Wars is a real area with its own `.spawn` file,
         * `godwars_goblin2` standing there alone is silent, which is correct.
         *
         * A warning, never an error: it is a prompt to check a second source
         * (LostCity's own roster covers the same ground), not a fact.
         */
        if( symbol )
        {
            static const char* const k_area_prefixes[] = {
                "sos_pest_", "sos_fam_", "poh_", "godwars_", "barrows_",
                "raids_", "nightmare_", "dragonslayer_", "slayer_",
            };

            for( size_t k = 0; k < sizeof(k_area_prefixes) / sizeof(k_area_prefixes[0]); k++ )
            {
                size_t length = strlen(k_area_prefixes[k]);
                const char* base;
                int base_id;

                if( strncmp(symbol, k_area_prefixes[k], length) != 0 )
                    continue;
                base = symbol + length;
                base_id = mock230_content_symbol(MOCK230_PACK_NPC, base);
                if( base_id < 0 )
                    break;
                for( int j = 0; j < npc_count; j++ )
                {
                    if( npcs[j].npc_id != base_id )
                        continue;
                    report_warning(
                        "%s is spawned here and so is `%s` — a name qualified by "
                        "another content area beside the plain one is usually an "
                        "id-imported roster; check both against a second source",
                        symbol, base);
                    break;
                }
                break;
            }
        }
    }

    for( int i = 0; i < obj_count; i++ )
    {
        if( !mock230_objinfo(objs[i].obj_id)->name )
            report_error("obj %d spawned at %d,%d is not in the cache", objs[i].obj_id,
                         objs[i].x, objs[i].z);
    }
}

/*
 * Equipment requirements.
 *
 * The table is merged from two sources that disagree — cache.osrs239's own
 * params 434/436 + 435/437, and `skill_combat/configs/equipment.obj` imported
 * from Kronos — so what this checks is not "is it there" but "is it *sane*",
 * which for a requirement means four things:
 *
 *   - the skill is one the wire has (0..22),
 *   - the level is 1..99,
 *   - the obj it gates is actually wearable, because a requirement on something
 *     you cannot equip can never fire and is therefore a mis-typed symbol,
 *   - and the ladder still climbs. A scimitar ladder that reads 5/10/20/30/40/60
 *     is the one line of this report worth reading at a glance; a source swapped
 *     for a worse one shows up as a number out of order rather than as silence.
 *
 * The numbers were transcribed from a Kronos dump once and are authored now;
 * docs/mock230_content.md §5 for why neither source is trusted alone.
 */
static void
validate_requirements(void)
{
    /* The ladders. Every one of these is an OldSchool value nobody should be
     * able to change by accident, and they span both sources: steel through
     * adamant come from the overlay, rune and dragon from the cache. */
    static const struct
    {
        const char* symbol;
        int stat;
        int level;
    } k_expect[] = {
        { "bronze_scimitar", -1, 0 },  /* no requirement at all */
        { "iron_scimitar", -1, 0 },
        { "steel_scimitar", MOCK230_STAT_ATTACK, 5 },
        { "black_scimitar", MOCK230_STAT_ATTACK, 10 },
        { "mithril_scimitar", MOCK230_STAT_ATTACK, 20 },
        { "adamant_scimitar", MOCK230_STAT_ATTACK, 30 },
        { "rune_scimitar", MOCK230_STAT_ATTACK, 40 },
        { "dragon_scimitar", MOCK230_STAT_ATTACK, 60 },
        { "rune_platebody", MOCK230_STAT_DEFENCE, 40 },
        { "abyssal_whip", MOCK230_STAT_ATTACK, 70 },
        { "magic_shortbow", MOCK230_STAT_RANGED, 50 },
    };
    int total = 0;
    int from_cache = 0;
    int checked = 0;

    mock230_obj_require_counts(&total, &from_cache);
    fprintf(stderr, "equipment requirements\n");

    for( int obj_id = 0; obj_id < 40000; obj_id++ )
    {
        const struct Mock230ObjRequire* require = mock230_obj_require(obj_id);
        const struct Mock230ObjInfo* info;

        if( !require )
            continue;
        checked++;
        info = mock230_objinfo(obj_id);
        if( info->wearpos < 0 )
            report_error("obj %d (%s) has an equip requirement but cannot be worn",
                         obj_id, info->name ? info->name : "?");
        for( int i = 0; i < require->count; i++ )
        {
            if( require->req[i].stat < 0 || require->req[i].stat >= MOCK230_STAT_COUNT )
                report_error("obj %d (%s) requires skill %d, which is not a skill", obj_id,
                             info->name ? info->name : "?", require->req[i].stat);
            if( require->req[i].level < 1 || require->req[i].level > 99 )
                report_error("obj %d (%s) requires level %d", obj_id,
                             info->name ? info->name : "?", require->req[i].level);
        }
    }

    for( size_t i = 0; i < sizeof(k_expect) / sizeof(k_expect[0]); i++ )
    {
        int obj_id = mock230_content_symbol(MOCK230_PACK_OBJ, k_expect[i].symbol);
        const struct Mock230ObjRequire* require;
        int found = 0;

        if( obj_id < 0 )
        {
            report_error("%s is not in pack/obj.pack", k_expect[i].symbol);
            continue;
        }
        require = mock230_obj_require(obj_id);
        if( k_expect[i].stat < 0 )
        {
            if( require )
                report_error("%s should have no requirement, has %d", k_expect[i].symbol,
                             require->count);
            continue;
        }
        if( !require )
        {
            report_error("%s should require %d in skill %d, has no requirement",
                         k_expect[i].symbol, k_expect[i].level, k_expect[i].stat);
            continue;
        }
        for( int r = 0; r < require->count; r++ )
        {
            if( require->req[r].stat != k_expect[i].stat )
                continue;
            found = 1;
            if( require->req[r].level != k_expect[i].level )
                report_error("%s requires level %d in skill %d, expected %d",
                             k_expect[i].symbol, require->req[r].level, k_expect[i].stat,
                             k_expect[i].level);
        }
        if( !found )
            report_error("%s does not require skill %d at all", k_expect[i].symbol,
                         k_expect[i].stat);
    }

    report_info("%d objs carry a requirement — %d out of the cache's own params, %d "
                "added or corrected by the .obj overlay",
                total, from_cache, total - from_cache);
    fprintf(stderr, "        %d requirement rows checked, %zu ladder values pinned\n", checked,
            sizeof(k_expect) / sizeof(k_expect[0]));
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
 * This is the step that turns the door pairings' guesswork into data. They were
 * derived from naming conventions — the only way past OpenRune's 13 curated ones —
 * and about one in seven of those turns out to be
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

    snprintf(path, sizeof(path), "%s/server/scripts/doors/configs/doors.loc", content);
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

/*
 * The `names/pins.ini` symbol closure used to live here, and it is gone.
 *
 * It existed to make absorbing a *new cache* reviewable: pin every symbol the
 * server names to the id it resolved to, so `cachepack unpack --compare` could
 * report the six changes that land on something content refers to rather than
 * the 1,877 that do not. Real value, for a problem this tree no longer has —
 * the client cache is pinned to one revision (docs/CONTENT_PACK_PLAN.md §0,
 * decision 1), and a future release is absorbed by exporting it into a second
 * content tree and running `diff -r`, which is ordinary reviewable text and a
 * better artifact than a fingerprint file.
 *
 * Listed here rather than merely deleted so nobody re-adds it by reflex: what
 * would justify it is the cache stopping being frozen, and nothing else.
 */

/*
 * Every declared allocation base has to clear the cache's own high-water mark.
 *
 * The param block used to start at 2000 with the note "above every real param id
 * so they cannot collide with one". cache.osrs239's param group holds 2,634
 * records covering 0..2633 with no gaps, so all fifteen of them named a param the
 * cache already defines — `hitpoints` sat on 2100, which is a real record, and
 * `mock230_pack --cache-out` wrote over it.
 *
 * The check is not "are they above 2000", it is "are they above whatever this
 * cache actually goes up to", which is the only version that survives a bump. And
 * it is not about params: the register now declares a base for every namespace
 * (`server_base`, docs/CONTENT_PACK_PLAN.md §4.2), so the same question applies to
 * all of them and the same answer is checkable for all of them. A base that is a
 * *guess* is what went wrong; a base that is checked against the cache in front of
 * it is a fact.
 *
 * Two directions, both reported:
 *
 *   base vs cache    a declared base at or below the cache's largest id would
 *                    hand out ids the client already uses.
 *   ids vs base      an id in the pack file that sits above the cache's maximum
 *                    but below the base is ours-but-out-of-band: it works today
 *                    and will be handed out a second time.
 */
static void
validate_id_bases(struct RSCache_Dat2Disk* disk)
{
    /* The config group each namespace's records live in. Asset tables are left out
     * — they are addressed by reference table rather than by config group, and
     * cachepack is what walks those. */
    static const struct
    {
        enum Mock230PackKind kind;
        int config_kind;
    } k_groups[] = {
        { MOCK230_PACK_NPC,      RSCACHE_DAT2_CONFIG_KIND_NPC       },
        { MOCK230_PACK_OBJ,      RSCACHE_DAT2_CONFIG_KIND_OBJECT    },
        { MOCK230_PACK_LOC,      RSCACHE_DAT2_CONFIG_KIND_LOCS      },
        { MOCK230_PACK_SEQ,      RSCACHE_DAT2_CONFIG_KIND_SEQUENCE  },
        { MOCK230_PACK_SPOTANIM, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM  },
        { MOCK230_PACK_INV,      RSCACHE_DAT2_CONFIG_KIND_INV       },
        { MOCK230_PACK_VARP,     RSCACHE_DAT2_CONFIG_KIND_VARPLAYER },
        { MOCK230_PACK_VARBIT,   RSCACHE_DAT2_CONFIG_KIND_VARBIT    },
        { MOCK230_PACK_PARAM,    RSCACHE_DAT2_CONFIG_KIND_PARAMS    },
        { MOCK230_PACK_STRUCT,   RSCACHE_DAT2_CONFIG_KIND_STRUCT    },
        { MOCK230_PACK_ENUM,     RSCACHE_DAT2_CONFIG_KIND_ENUM      },
        { MOCK230_PACK_HITSPLAT, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT  },
        { MOCK230_PACK_DBTABLE,  RSCACHE_DAT2_CONFIG_KIND_DBTABLE   },
    };
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct ContentRegister reg;
    int checked = 0;
    int problems = 0;

    ContentRegister_Defaults(&reg);

    for( size_t g = 0; g < sizeof(k_groups) / sizeof(k_groups[0]); g++ )
    {
        const char* ns = mock230_content_pack_name(k_groups[g].kind);
        const struct ContentNamespace* row = ContentRegister_Find(&reg, ns);
        struct RSCache_Dat2DiskArchive* archive;
        int cache_max = -1;

        if( !row || row->server_base == 0 )
            continue; /* nothing to allocate here — see `server_base`'s docs */

        archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, k_groups[g].config_kind);
        if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
        {
            report_warning("no %s config archive — its allocation base is unchecked", ns);
            if( archive )
                RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        for( int i = 0; i < archive->file_count; i++ )
        {
            int id = archive->file_ids ? archive->file_ids[i] : i;
            if( id > cache_max )
                cache_max = id;
        }
        RSCache_Dat2DiskArchiveFree(archive);
        checked++;

        if( row->server_base <= cache_max )
        {
            report_error("%s: the allocation base is %d, but the cache defines ids up to "
                         "%d — raise the base in content_register.c",
                         ns, row->server_base, cache_max);
            problems++;
            continue;
        }

        /*
         * Ids above the cache's maximum but below the declared base.
         *
         * Not a hazard and not reused: the base is a *floor*, and the allocator
         * takes `max(base, highest_in_file + 1)` — `lc_pack_alloc_from` on the C
         * side, `ss_allocate.py` on the other — so a number already in the file is
         * never handed out again. What it means is that the ids predate the base
         * being written down, and the gap between them and the base will simply
         * stay empty. Reported because a base that does not match what is in use
         * is worth one line, and silently correct is worse than visibly odd.
         */
        int early = 0;
        for( int id = cache_max + 1; id < row->server_base; id++ )
        {
            if( mock230_content_symbol_name(k_groups[g].kind, id) )
                early++;
        }
        if( early )
            report_info("%s: %d id(s) sit between the cache's %d and the base %d — allocated "
                        "before the base was declared; the allocator skips past them",
                        ns, early, cache_max, row->server_base);
    }

    fprintf(stderr, "id bases\n        %d namespace(s) checked against the cache%s\n", checked,
            problems ? ", WITH COLLISIONS" : "");
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
     * a naming convention, which cannot tell a real pair from a
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
/*
 * Which struct field each authored name is, so the field register can reach it.
 *
 * The register states *which* fields project into params and under what name
 * (`src/content/content_fields.c`); this states where the value lives. Two tables,
 * and neither repeats the other — one is content's business and lives in a file a
 * content author can edit, the other is a property of the C struct and cannot.
 *
 * What this replaces is a single table that did both, in C, so adding a projected
 * field meant editing a struct, a parser and a baker, and forgetting the third
 * produced a field that loaded, worked in the mock, and never reached the cache.
 */
struct FieldOffset
{
    const char* name;
    size_t offset;
};

static const struct FieldOffset k_npc_fields[] = {
    { "hitpoints",   offsetof(struct Mock230NpcDef, hitpoints)   },
    { "attack",      offsetof(struct Mock230NpcDef, attack)      },
    { "strength",    offsetof(struct Mock230NpcDef, strength)    },
    { "defence",     offsetof(struct Mock230NpcDef, defence)     },
    { "magic",       offsetof(struct Mock230NpcDef, magic)       },
    { "ranged",      offsetof(struct Mock230NpcDef, ranged)      },
    { "respawnrate", offsetof(struct Mock230NpcDef, respawnrate) },
    { "wanderrange", offsetof(struct Mock230NpcDef, wanderrange) },
    { "nomove",      offsetof(struct Mock230NpcDef, nomove)      },
    { "huntrange",   offsetof(struct Mock230NpcDef, huntrange)   },
    { "attackrate",  offsetof(struct Mock230NpcDef, attackrate)  },
    { "attackrange", offsetof(struct Mock230NpcDef, attackrange) },
    { "damagetype",  offsetof(struct Mock230NpcDef, damagetype)  },
    { "attack_anim", offsetof(struct Mock230NpcDef, attack_anim) },
    { "defend_anim", offsetof(struct Mock230NpcDef, defend_anim) },
    { "death_anim",  offsetof(struct Mock230NpcDef, death_anim)  },
    { "death_drop",  offsetof(struct Mock230NpcDef, death_drop)  },
};

static const struct FieldOffset k_loc_fields[] = {
    { "next_loc_stage", offsetof(struct Mock230LocDef, next_loc_stage) },
};

/** The int this field holds on `record`, or 0 when the table does not know it. */
static int
field_value(
    const struct FieldOffset* table,
    size_t table_count,
    const char* name,
    const void* record,
    int* out_found)
{
    *out_found = 0;
    for( size_t i = 0; i < table_count; i++ )
    {
        if( strcmp(table[i].name, name) != 0 )
            continue;
        *out_found = 1;
        return *(const int*)((const char*)record + table[i].offset);
    }
    return 0;
}


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
 * Fold one record's authored fields into its param table, as the register says.
 *
 * `bake_npc_params` and `bake_loc_params` were two functions doing this from two
 * hardcoded lists; this is one, driven by `fields/<type>.ini`. A negative value is
 * "unset" for every field here — a param of -1 is not a value the client can use,
 * and `death_drop` spells "drops nothing" that way.
 */
static void
bake_params(
    struct RSCache_Params* params,
    const char* type,
    const void* record,
    const struct FieldOffset* offsets,
    size_t offset_count)
{
    struct ContentFields fields;

    if( !record )
        return;
    ContentFields_Load(&fields, g_content_dir, type);

    for( int i = 0; i < fields.count; i++ )
    {
        const struct ContentField* field = &fields.entries[i];
        int found = 0;
        int value = field_value(offsets, offset_count, field->name, record, &found);

        if( field->client == CONTENT_CLIENT_ERROR )
        {
            /* Declared as "must be expressible", and it is not. Reported at bake
             * time, which is the whole point of the disposition: the alternative is
             * finding out in game that a field never reached the client. */
            if( found && value != 0 )
                report_error("%s.%s is declared `client = error` and has a value (%d) — "
                             "give it a param or declare it `drop`",
                             type, field->name, value);
            continue;
        }
        if( field->client != CONTENT_CLIENT_PARAM )
            continue; /* native is the encoder's, drop is server-only */
        if( !found )
        {
            report_warning("%s.%s projects into param `%s` but no struct field holds "
                           "it — add it to k_%s_fields",
                           type, field->name, field->param_name, type);
            continue;
        }
        if( value < 0 )
            continue;

        int key = mock230_content_symbol(MOCK230_PACK_PARAM, field->param_name);
        if( key < 0 )
        {
            report_warning("pack/param.pack has no id for `%s` — %s.%s not baked",
                           field->param_name, type, field->name);
            continue;
        }
        param_set(params, key, value);
    }
}

/*
 * Write an npc's authored combat block into its param table.
 *
 * The equipment bonuses are already there — OldSchool puts them at param ids
 * 0..11 — so this only adds what a cache has no field for. `null` values (-1)
 * are skipped rather than written: a param that is present and -1 reads back as
 * "drops object -1", where an absent one correctly reads as "nothing was said".
 */

/*
 * Re-encode one config group with overlays folded into params.
 *
 * The encoders are semantic round trips, not byte-exact ones: they cannot tell
 * "field absent" from "field present and zero", so a re-encoded record is usually
 * a few bytes shorter than the original. Untouched records keep their original
 * bytes so the rest of the group stays identical.
 */
static int
bake_config_group(
    struct RSCache_Dat2Disk* disk,
    struct RSCache* profile,
    struct RSCache_Dat2Edit* edit,
    int table,
    int config_kind,
    enum RSCache_Type rs_type,
    int (*bake_one)(
        struct RSCache* profile,
        int id,
        const uint8_t* data,
        int size,
        uint8_t* out,
        uint32_t out_cap,
        uint32_t* out_size),
    const char* label,
    int* out_baked)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_FileList packed;
    uint8_t* encoded_group = NULL;
    uint32_t encoded_size;
    int baked = 0;
    int ok = 0;
    uint8_t buffer[8192];

    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, config_kind);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        fprintf(stderr, "mock230_pack: no %s config archive\n", label);
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    RSCache_ProfileSetGroupRevision(profile, rs_type, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    memset(&packed, 0, sizeof(packed));
    packed.file_count = files->file_count;
    packed.files = calloc((size_t)packed.file_count, sizeof(char*));
    packed.file_sizes = calloc((size_t)packed.file_count, sizeof(int));

    for( int i = 0; i < files->file_count; i++ )
    {
        int id = archive->file_ids ? archive->file_ids[i] : i;
        uint32_t size = 0;

        if( files->file_sizes[i] <= 0 )
            continue;
        if( !bake_one(profile, id, (const uint8_t*)files->files[i], files->file_sizes[i],
                      buffer, sizeof(buffer), &size) )
        {
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
        fprintf(stderr, "mock230_pack: the %s group did not encode\n", label);
    }
    else if( RSCache_Dat2EditPutArchive(edit, table, config_kind, encoded_group,
                                        encoded_size) )
    {
        fprintf(stderr, "        baked %d %s records\n", baked, label);
        ok = 1;
        if( out_baked )
            *out_baked = baked;
    }
    else
    {
        fprintf(stderr, "mock230_pack: could not stage the %s archive\n", label);
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
    return ok;
}

static int
bake_one_npc(
    struct RSCache* profile,
    int id,
    const uint8_t* data,
    int size,
    uint8_t* out,
    uint32_t out_cap,
    uint32_t* out_size)
{
    const struct Mock230NpcDef* def = mock230_content_npc(id);
    struct RSCache_Dat2ConfigNpc* npc;

    *out_size = 0;
    if( !def )
        return 0;
    npc = RSCache_Dat2ConfigNpcNewDecodeProfile(profile, (char*)data, size);
    if( !npc )
        return 0;
    bake_params(&npc->params, "npc", def, k_npc_fields,
                sizeof(k_npc_fields) / sizeof(k_npc_fields[0]));
    *out_size = RSCache_Dat2ConfigNpcEncodeProfile(profile, npc, out, out_cap);
    RSCache_Dat2ConfigNpcFree(npc);
    if( *out_size == 0 )
    {
        report_error("npc %d did not re-encode", id);
        return 0;
    }
    return 1;
}

static int
bake_one_loc(
    struct RSCache* profile,
    int id,
    const uint8_t* data,
    int size,
    uint8_t* out,
    uint32_t out_cap,
    uint32_t* out_size)
{
    const struct Mock230LocDef* def = mock230_content_loc(id);
    struct RSCache_Dat2ConfigLoc* loc;

    *out_size = 0;
    if( !def || def->next_loc_stage < 0 )
        return 0;
    loc = RSCache_Dat2ConfigLocNewDecodeProfile(profile, (char*)data, size);
    if( !loc )
        return 0;
    bake_params(&loc->params, "loc", def, k_loc_fields,
                sizeof(k_loc_fields) / sizeof(k_loc_fields[0]));
    *out_size = RSCache_Dat2ConfigLocEncode(profile, loc, out, out_cap);
    RSCache_Dat2ConfigLocFree(loc);
    if( *out_size == 0 )
    {
        report_error("loc %d did not re-encode", id);
        return 0;
    }
    return 1;
}

static int
export_cache(
    const char* cache,
    const char* cache_out)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2Edit* edit;
    int table;
    int ok = 0;

    fprintf(stderr, "exporting to %s (copying %s — this writes the whole cache)\n",
            cache_out, cache);
    if( !copy_cache(cache, cache_out) )
        return 0;

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = MOCK230_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_out);
    if( !disk )
    {
        fprintf(stderr, "mock230_pack: cannot open the copied cache at %s\n", cache_out);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    edit = RSCache_Dat2EditNew(disk);
    if( !edit )
    {
        fprintf(stderr, "mock230_pack: cannot begin an edit\n");
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    if( !bake_config_group(disk, &profile, edit, table, RSCACHE_DAT2_CONFIG_KIND_NPC,
                           RSCACHE_TYPE_NPC, bake_one_npc, "npc", NULL) )
    {
        RSCache_Dat2EditFree(edit);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    if( !bake_config_group(disk, &profile, edit, table, RSCACHE_DAT2_CONFIG_KIND_LOCS,
                           RSCACHE_TYPE_LOC, bake_one_loc, "loc", NULL) )
    {
        RSCache_Dat2EditFree(edit);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    if( RSCache_Dat2EditCommit(edit, cache_out) )
    {
        fprintf(stderr, "        wrote patched cache to %s\n", cache_out);
        ok = 1;
    }
    else
    {
        fprintf(stderr, "mock230_pack: the edit did not commit\n");
    }
    RSCache_Dat2EditFree(edit);
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
            "  --content DIR    content tree (default OSRS-Content/osrs239-content)\n"
            "  --cache DIR      source cache (default cache.osrs239)\n"
            "  --cache-out DIR  write a derived cache with server overlays baked in:\n"
            "                   npc combat params and loc next_loc_stage\n"
            "  --prune-doors    rewrite doors.loc without the pairs the cache\n"
            "                   rejects\n"
            "  -v               list every definition as it is checked\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* content = "OSRS-Content/osrs239-content";
    const char* cache = MOCK230_CACHE_DIR_DEFAULT;
    const char* cache_out = NULL;
    int prune = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--content") == 0 && i + 1 < argc )
        {
            content = argv[++i];
            g_content_dir = content;
        }
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
    /* The engine's own symbol table, which is a claim about the packs in
     * exactly the way a config line is: every name the C addresses has to be in
     * one. Resolving it here is what makes a renamed or dropped symbol a
     * validator failure rather than a dead interface at runtime. */
    mock230_ids_resolve();

    g_errors += mock230_content_error_count();

    validate_spawns();
    validate_requirements();
    {
        struct RSCache profile = RSCache_ProfileZero();
        struct RSCache_Dat2Disk* disk;

        profile.game = RSCACHE_GAME_OLDSCHOOL;
        profile.epoch = RSCACHE_EPOCH_DAT2;
        profile.revision = MOCK230_CACHE_REVISION;
        disk = RSCache_Dat2DiskNewFromDirectory(cache);
        if( disk )
        {
            RSCache_Dat2DiskSetProfile(disk, &profile);
            validate_id_bases(disk);
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
